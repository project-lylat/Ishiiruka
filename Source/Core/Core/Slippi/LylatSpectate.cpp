#include "Core/Slippi/LylatSpectate.h"

#include <algorithm>
#include <memory>

#ifdef _WIN32
#include <share.h>
#endif

#include <unordered_map>

#include <SlippiLib/SlippiGame.h>
#include <enet/enet.h>
#include <json.hpp>

#include "Common/Common.h"
#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Common/Logging/Log.h"
#include "Common/StringUtil.h"
#include "Common/Timer.h"
#include "Core/ConfigManager.h"
#include "Core/GeckoCodeConfig.h"
#include "VideoCommon/OnScreenDisplay.h"

#include "SlippiRustExtensions.h"

using json = nlohmann::json;

namespace
{
enum RelayType : u8
{
	CONNECT_REQUEST = 1,
	CONNECT_REPLY = 2,
	START_GAME = 3,
	GAME_EVENT = 4,
	END_GAME = 5,
	KEEP_ALIVE = 6,
	REFUSED = 7,
	PUBLISH = 8,
};

constexpr u16 DEFAULT_MM_PORT = 43113;
constexpr size_t EVENT_CHUNK = 16 * 1024;
constexpr u32 MAX_IN_FLIGHT = 512 * 1024;
// enet_peer_send only queues: reliableDataInTransit counts what ENet already sent. Queuing stops
// while this many commands still wait, so a reconnect does not queue the whole game at once.
constexpr size_t MAX_QUEUED_COMMANDS = 64;
constexpr u64 MM_RETRY_MS = 2000;
constexpr size_t MAX_STREAM = 64 * 1024 * 1024;
constexpr u64 PRESENCE_HEARTBEAT_MS = 10000;
constexpr u64 KEEP_ALIVE_MS = 2000;
constexpr u64 RELAY_RETRY_MS = 1000;
constexpr int SPECTATE_CHANNELS = 3;
// The daemon, the relay, and a relay link still closing while the next game's link opens.
constexpr int SPECTATE_PEERS = 4;

void ConnectAddress(ENetAddress &addr, const std::string &host, u16 port)
{
	enet_address_set_host(&addr, host.c_str());
	addr.port = port;
}

void MatchmakingAddress(ENetAddress &addr)
{
	char *rsHost = slprs_lylat_mm_host();
	std::string host(rsHost);
	slprs_lylat_free_string(rsHost);
	u16 port = slprs_lylat_mm_port();
	ConnectAddress(addr, host, port != 0 ? port : DEFAULT_MM_PORT);
}

json Request(const std::string &type, const SlippiUser::UserInfo &user)
{
	json msg;
	msg["type"] = type;
	msg["user"] = {{"uid", user.uid},
	               {"playKey", user.playKey},
	               {"connectCode", user.connectCode},
	               {"displayName", user.displayName}};
	msg["appVersion"] = scm_slippi_semver_str;
	return msg;
}

void SendJson(ENetPeer *peer, const json &msg)
{
	std::string text = msg.dump();
	enet_peer_send(peer, 0, enet_packet_create(text.data(), text.size(), ENET_PACKET_FLAG_RELIABLE));
}

void SendRelay(ENetPeer *peer, u8 type, u32 cursor, const u8 *payload = nullptr, size_t length = 0)
{
	ENetPacket *packet = enet_packet_create(nullptr, 5 + length, ENET_PACKET_FLAG_RELIABLE);
	packet->data[0] = type;
	packet->data[1] = (u8)(cursor >> 24);
	packet->data[2] = (u8)(cursor >> 16);
	packet->data[3] = (u8)(cursor >> 8);
	packet->data[4] = (u8)cursor;
	if (length)
		memcpy(packet->data + 5, payload, length);
	enet_peer_send(peer, 0, packet);
}

void SendRelayText(ENetPeer *peer, u8 type, const std::string &text)
{
	SendRelay(peer, type, 0, (const u8 *)text.data(), text.size());
}

void SendRelayWord(ENetPeer *peer, u8 type, u32 word)
{
	u8 payload[4] = {(u8)(word >> 24), (u8)(word >> 16), (u8)(word >> 8), (u8)word};
	SendRelay(peer, type, 0, payload, sizeof(payload));
}

u32 ReadWord(const u8 *data)
{
	return (u32)data[0] << 24 | (u32)data[1] << 16 | (u32)data[2] << 8 | data[3];
}

bool ParseJson(const ENetPacket *packet, json &out)
{
	std::string text((const char *)packet->data, packet->dataLength);
	out = json::parse(text, nullptr, false);
	return !out.is_discarded() && out.is_object();
}

// Checks a spectated stream's events as they arrive: the payload size table first, then only
// commands from that table, each with its listed size. A stream comes from another player's
// Dolphin, so anything else stops the session before it reaches the replay reader.
class StreamCheck
{
  public:
	// Returns false once the bytes so far are not a well formed stream.
	bool Feed(const u8 *data, size_t length)
	{
		m_pending.insert(m_pending.end(), data, data + length);
		size_t pos = 0;
		bool ok = true;
		while (ok)
		{
			size_t left = m_pending.size() - pos;
			const u8 *p = m_pending.data() + pos;
			if (!m_haveSizes)
			{
				if (left < 2)
					break;
				// The length counts itself, followed by 3 bytes per command.
				u8 len = p[1];
				ok = p[0] == Slippi::EVENT_PAYLOAD_SIZES && len >= 4 && (len - 1) % 3 == 0;
				if (!ok || left < 1u + len)
					break;
				for (size_t i = 2; i < 1u + len; i += 3)
					m_sizes[p[i]] = (u16)(p[i + 1] << 8 | p[i + 2]);
				m_haveSizes = true;
				pos += 1 + len;
				continue;
			}
			if (left < 1)
				break;
			auto size = m_sizes.find(p[0]);
			ok = size != m_sizes.end() && p[0] != Slippi::EVENT_PAYLOAD_SIZES;
			if (!ok || left < 1u + size->second)
				break;
			if (p[0] == Slippi::EVENT_SPLIT_MESSAGE)
			{
				// A data block, its 2 byte length, the inner command and a done flag.
				const u32 block = Slippi::SPLIT_MESSAGE_INTERNAL_DATA_LEN;
				ok = size->second >= block + 4 && (u32)(p[1 + block] << 8 | p[2 + block]) <= block;
				if (!ok)
					break;
			}
			pos += 1 + size->second;
		}
		m_pending.erase(m_pending.begin(), m_pending.begin() + pos);
		return ok;
	}

  private:
	bool m_haveSizes = false;
	std::unordered_map<u8, u16> m_sizes;
	std::vector<u8> m_pending; // the start of an event still arriving
};

// The length of the Gecko code that starts at code, as the replay code list splits them.
u64 GeckoCodeLength(const u8 *code)
{
	u32 count = ReadWord(code + 4);
	switch (code[0] & 0xFE)
	{
	case 0xC0:
	case 0xC2:
		return 8 + (u64)count * 8;
	case 0x08:
		return 16;
	case 0x06:
		return 8 + (((u64)count + 7) & ~(u64)7);
	default:
		return 8;
	}
}

// How long the peer was silent (including ENet's own pings) before this event. A gap of 5 s or
// more before a disconnect means an ENet timeout rather than a close from the other side. ENet resets
// the peer before it reports the disconnect, so callers pass the receive time saved before servicing.
unsigned long long SilenceMs(const ENetHost *host, enet_uint32 lastReceiveTime)
{
	// ENet times are 32-bit milliseconds; unsigned subtraction handles the wrap.
	return (enet_uint32)(host->serviceTime - lastReceiveTime);
}

std::string SpectateDir()
{
	return File::GetUserPath(D_SLIPPI_IDX) + "Spectate" DIR_SEP;
}

std::unique_ptr<LylatSpectateViewer> s_viewer;
} // namespace

// ---- Source -----------------------------------------------------------------

LylatSpectateSource &LylatSpectateSource::Get()
{
	static LylatSpectateSource instance;
	return instance;
}

LylatSpectateSource::~LylatSpectateSource()
{
	m_running = false;
	if (m_thread.joinable())
		m_thread.join();
}

void LylatSpectateSource::GameStart(const std::string &matchId, const SlippiUser::UserInfo &user)
{
	if (!SConfig::GetInstance().m_lylatAllowSpectators || matchId.empty())
		return;

	std::lock_guard<std::mutex> lock(m_mutex);
	if (matchId != m_matchId)
		m_gameIndex = -1;
	m_matchId = matchId;
	m_user = user;
	m_gameIndex++;
	m_inGame = true;
	m_left = false;
	m_stream.clear();

	json msg = Request("spectate-presence", user);
	msg["matchId"] = matchId;
	msg["gameIndex"] = m_gameIndex;
	msg["state"] = "game_start";
	msg["allowSpectators"] = true;
	m_presence.push_back(msg.dump());

	if (!m_running)
	{
		if (m_thread.joinable())
			m_thread.join();
		m_running = true;
		m_thread = std::thread(&LylatSpectateSource::Run, this);
	}
}

void LylatSpectateSource::Write(const u8 *data, u32 length)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_inGame || m_stream.size() + length > MAX_STREAM)
		return;
	m_stream.insert(m_stream.end(), data, data + length);
}

void LylatSpectateSource::GameEnd()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_inGame)
		return;
	m_inGame = false;

	json msg = Request("spectate-presence", m_user);
	msg["matchId"] = m_matchId;
	msg["gameIndex"] = m_gameIndex;
	msg["state"] = "game_end";
	msg["allowSpectators"] = true;
	m_presence.push_back(msg.dump());
}

void LylatSpectateSource::Leave()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_left || m_matchId.empty())
		return;
	m_inGame = false;
	m_left = true;

	json msg = Request("spectate-presence", m_user);
	msg["matchId"] = m_matchId;
	msg["gameIndex"] = std::max(m_gameIndex, 0);
	msg["state"] = "left";
	msg["allowSpectators"] = true;
	m_presence.push_back(msg.dump());
}

void LylatSpectateSource::Run()
{
	// Each user of ENet initializes it; Winsock counts the references.
	enet_initialize();
	ENetHost *host = enet_host_create(nullptr, SPECTATE_PEERS, SPECTATE_CHANNELS, 0, 0);
	if (!host)
	{
		ERROR_LOG(SLIPPI_ONLINE, "[Spectate] Could not create the spectate host");
		enet_deinitialize();
		m_running = false;
		return;
	}

	ENetPeer *mm = nullptr;
	bool mmUp = false;
	ENetPeer *relay = nullptr;
	bool relayUp = false;

	std::string relayHost, token;
	u16 relayPort = 0;
	int tokenGame = -1;
	bool tokenRefused = false;

	int relayGame = -1; // the game announced on the current relay link
	size_t sent = 0;    // stream bytes sent on the current relay link
	int doneGame = -1;  // the last game whose end_game went out
	std::string lastPresence;
	u64 lastPresenceAt = 0, lastKeepAliveAt = 0, relayRetryAt = 0;

	while (m_running)
	{
		u64 now = Common::Timer::GetTimeMs();
		int gameIndex;
		bool inGame, left, hasMatch;
		size_t size;
		std::vector<std::string> presence;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			gameIndex = m_gameIndex;
			inGame = m_inGame;
			left = m_left;
			hasMatch = !m_matchId.empty();
			size = m_stream.size();
			if (mmUp)
				presence.swap(m_presence);
		}

		if (!mm && hasMatch && !left)
		{
			ENetAddress addr;
			MatchmakingAddress(addr);
			mm = enet_host_connect(host, &addr, SPECTATE_CHANNELS, 0);
		}

		if (mmUp)
		{
			for (const std::string &text : presence)
			{
				enet_peer_send(mm, 0, enet_packet_create(text.data(), text.size(), ENET_PACKET_FLAG_RELIABLE));
				lastPresence = text;
				lastPresenceAt = now;
			}
			if (left && presence.empty() && !lastPresence.empty())
			{
				// "left" went out on an earlier pass; close once it is delivered.
				enet_peer_disconnect_later(mm, 0);
				lastPresence.clear();
			}
			else if (!left && !lastPresence.empty() && now - lastPresenceAt >= PRESENCE_HEARTBEAT_MS)
			{
				enet_peer_send(mm, 0,
				               enet_packet_create(lastPresence.data(), lastPresence.size(), ENET_PACKET_FLAG_RELIABLE));
				lastPresenceAt = now;
			}
		}

		bool unpublished = gameIndex >= 0 && doneGame != gameIndex && (inGame || size > 0);
		bool haveToken = !token.empty() && !tokenRefused && (relayUp || tokenGame == gameIndex);
		if (!relay && unpublished && haveToken && now >= relayRetryAt)
		{
			ENetAddress addr;
			ConnectAddress(addr, relayHost, relayPort);
			relay = enet_host_connect(host, &addr, 1, 0);
			if (relay)
				enet_peer_timeout(relay, 16, 3000, 8000);
			relayRetryAt = now + RELAY_RETRY_MS;
		}

		if (relayUp)
		{
			if (unpublished && relayGame != gameIndex)
			{
				SendRelayWord(relay, START_GAME, (u32)gameIndex);
				relayGame = gameIndex;
				sent = 0;
			}
			if (relayGame == gameIndex)
			{
				while (sent < size && relay->reliableDataInTransit < MAX_IN_FLIGHT &&
				       enet_list_size(&relay->outgoingReliableCommands) < MAX_QUEUED_COMMANDS)
				{
					std::vector<u8> chunk;
					{
						std::lock_guard<std::mutex> lock(m_mutex);
						if (m_gameIndex != gameIndex)
							break;
						size_t n = std::min(EVENT_CHUNK, m_stream.size() - sent);
						chunk.assign(m_stream.begin() + sent, m_stream.begin() + sent + n);
					}
					SendRelay(relay, GAME_EVENT, (u32)sent, chunk.data(), chunk.size());
					sent += chunk.size();
				}
				if (!inGame && sent == size && doneGame != gameIndex)
				{
					SendRelay(relay, END_GAME, (u32)size);
					doneGame = gameIndex;
				}
			}
			if (now - lastKeepAliveAt >= KEEP_ALIVE_MS)
			{
				SendRelay(relay, KEEP_ALIVE, 0);
				lastKeepAliveAt = now;
			}
		}

		enet_uint32 mmHeard = mm ? mm->lastReceiveTime : 0;
		enet_uint32 relayHeard = relay ? relay->lastReceiveTime : 0;
		ENetEvent ev;
		while (enet_host_service(host, &ev, 5) > 0)
		{
			switch (ev.type)
			{
			case ENET_EVENT_TYPE_CONNECT:
				if (ev.peer == mm)
				{
					mmUp = true;
				}
				else if (ev.peer == relay)
				{
					relayUp = true;
					relayGame = -1;
					SendRelayText(relay, PUBLISH, token);
					INFO_LOG(SLIPPI_ONLINE, "[Spectate] Publishing to the relay %s:%d", relayHost.c_str(), relayPort);
				}
				break;
			case ENET_EVENT_TYPE_RECEIVE:
				if (ev.peer == mm)
				{
					json msg;
					if (ParseJson(ev.packet, msg) && msg.value("type", "") == "spectate-presence-resp")
					{
						std::string error = msg.value("error", "");
						if (!error.empty())
						{
							WARN_LOG(SLIPPI_ONLINE, "[Spectate] Presence refused: %s", error.c_str());
							// The match is over on the server: stop the heartbeat until the next game.
							lastPresence.clear();
						}
						auto info = msg.find("relay");
						if (info != msg.end() && info->is_object())
						{
							std::string newToken = info->value("publishToken", "");
							relayHost = info->value("host", "");
							relayPort = info->value("port", 0);
							tokenGame = msg.value("gameIndex", -1);
							if (newToken != token)
								tokenRefused = false;
							token = newToken;
						}
					}
				}
				else if (ev.peer == relay && ev.packet->dataLength >= 5 && ev.packet->data[0] == REFUSED)
				{
					std::string text((const char *)ev.packet->data + 5, ev.packet->dataLength - 5);
					WARN_LOG(SLIPPI_ONLINE, "[Spectate] The relay refused the stream: %s", text.c_str());
					tokenRefused = true;
				}
				enet_packet_destroy(ev.packet);
				break;
			case ENET_EVENT_TYPE_DISCONNECT:
				if (ev.peer == mm)
				{
					// The server marks the source gone when this link drops, and the relay closes its stream.
					WARN_LOG(SLIPPI_ONLINE, "[Spectate] The source's daemon connection closed (data %u), silent %llu ms",
					         ev.data, SilenceMs(host, mmHeard));
					mm = nullptr;
					mmUp = false;
				}
				else if (ev.peer == relay)
				{
					WARN_LOG(SLIPPI_ONLINE,
					         "[Spectate] The relay link closed after %zu of %zu bytes of game %d, silent %llu ms, rtt %u ms",
					         sent, size, gameIndex, SilenceMs(host, relayHeard), ev.peer->roundTripTime);
					relay = nullptr;
					relayUp = false;
					relayRetryAt = Common::Timer::GetTimeMs() + RELAY_RETRY_MS;
				}
				break;
			default:
				break;
			}
		}
	}

	if (relay)
		enet_peer_disconnect_now(relay, 0);
	if (mm)
		enet_peer_disconnect_now(mm, 0);
	enet_host_flush(host);
	enet_host_destroy(host);
	enet_deinitialize();
}

// ---- Viewer -----------------------------------------------------------------

void LylatSpectateViewer::Start(const std::string &connectCode, SlippiUser *user)
{
	s_viewer.reset();
	s_viewer.reset(new LylatSpectateViewer(connectCode, user));
}

void LylatSpectateViewer::Stop()
{
	s_viewer.reset();
}

std::string LylatSpectateViewer::CommPath()
{
	return SpectateDir() + "comm.json";
}

std::set<std::vector<u8>> LylatSpectateViewer::TrustedCodes()
{
	// The online codes in Sys/GameSettings, not the playback ones this session boots with.
	const SConfig &config = SConfig::GetInstance();
	IniFile ini;
	for (const std::string &name : SConfig::GetGameIniFilenames(config.GetGameID(), config.m_revision))
		ini.Load(File::GetSysDirectory() + GAMESETTINGS_DIR DIR_SEP + name, true);
	std::vector<Gecko::GeckoCode> codes;
	Gecko::ParseCodes(ini, codes, false);

	std::set<std::vector<u8>> trusted;
	for (const Gecko::GeckoCode &code : codes)
	{
		std::vector<u8> bytes;
		for (const Gecko::GeckoCode::Code &line : code.codes)
		{
			for (u32 word : {line.address, line.data})
				bytes.insert(bytes.end(), {(u8)(word >> 24), (u8)(word >> 16), (u8)(word >> 8), (u8)word});
		}
		// One ini entry can hold several codes; the replay lists each on its own.
		size_t at = 0;
		while (bytes.size() - at >= 8)
		{
			u64 length = GeckoCodeLength(bytes.data() + at);
			if (length > bytes.size() - at)
				break;
			trusted.emplace(bytes.begin() + at, bytes.begin() + at + (size_t)length);
			at += (size_t)length;
		}
	}
	return trusted;
}

// The watched games are temporary: they belong to the player being watched.
static void DeleteSpectateFiles()
{
	for (const auto &entry : File::ScanDirectoryTree(SpectateDir(), false).children)
	{
		if (!entry.isDirectory)
			File::Delete(entry.physicalName);
	}
}

LylatSpectateViewer::LylatSpectateViewer(const std::string &connectCode, SlippiUser *user)
    : m_code(connectCode), m_user(user)
{
	File::CreateFullPath(SpectateDir());
	DeleteSpectateFiles();
	WriteComm("");
	m_thread = std::thread(&LylatSpectateViewer::Run, this);
}

LylatSpectateViewer::~LylatSpectateViewer()
{
	m_running = false;
	if (m_thread.joinable())
		m_thread.join();
	DeleteSpectateFiles();
}

void LylatSpectateViewer::WriteComm(const std::string &replayPath)
{
	json comm;
	comm["mode"] = "mirror";
	comm["replay"] = replayPath;
	comm["isRealTimeMode"] = true;
	comm["commandId"] = std::to_string(m_fileCount);
	File::WriteStringToFile(comm.dump(), CommPath());
}

void LylatSpectateViewer::Show(Status status, const std::string &text)
{
	NOTICE_LOG(SLIPPI_ONLINE, "[Spectate] %s", text.c_str());
	// One status line at a time: each new status replaces the last one.
	u32 ms = status == Status::Live ? 4000 : status == Status::Refused ? 15000 : 600000;
	OSD::AddTypedMessage(OSD::MessageType::SpectateStatus, text, ms);
}

void LylatSpectateViewer::Run()
{
	Show(Status::Connecting, "Connecting to Lylat...");

	// The account loads in the background after the game boots.
	while (m_running && !m_user->IsLoggedIn())
		Common::SleepCurrentThread(250);

	enet_initialize();
	ENetHost *host = enet_host_create(nullptr, SPECTATE_PEERS, SPECTATE_CHANNELS, 0, 0);
	if (!host)
	{
		Show(Status::Refused, "Could not start spectating.");
		enet_deinitialize();
		return;
	}

	ENetAddress mmAddr;
	MatchmakingAddress(mmAddr);
	ENetPeer *mm = enet_host_connect(host, &mmAddr, SPECTATE_CHANNELS, 0);
	u64 mmRetryAt = 0;
	ENetPeer *relay = nullptr;
	std::string token, sourceName;
	File::IOFile file;
	std::string filePath;
	StreamCheck check;
	u64 written = 0;
	bool commPointed = false;
	u64 lastKeepAliveAt = 0;
	bool done = false;

	auto request = [&]() {
		json msg = Request("spectate-request", m_user->GetUserInfo());
		msg["connectCode"] = m_code;
		SendJson(mm, msg);
	};

	auto goLive = [&](const json &msg) {
		auto info = msg.find("relay");
		if (info == msg.end() || !info->is_object())
			return;
		token = msg.value("token", "");
		auto source = msg.find("source");
		sourceName = source != msg.end() && source->is_object() ? source->value("displayName", m_code) : m_code;
		if (relay)
			enet_peer_disconnect_now(relay, 0);
		ENetAddress addr;
		ConnectAddress(addr, info->value("host", ""), info->value("port", 0));
		relay = enet_host_connect(host, &addr, 1, 0);
		if (relay)
			enet_peer_timeout(relay, 16, 3000, 8000);
		Show(Status::Live, "Watching " + sourceName);
	};

	auto newFile = [&]() {
		m_fileCount++;
		filePath = SpectateDir() + StringFromFormat("game-%d.slp", m_fileCount);
#ifdef _WIN32
		// Shared so the replay reader can follow the file while it grows.
		file = File::IOFile(filePath, "wb", _SH_DENYWR);
#else
		file = File::IOFile(filePath, "wb");
#endif
		written = 0;
		check = StreamCheck();
		commPointed = false;
	};

	while (m_running && !done)
	{
		enet_uint32 mmHeard = mm ? mm->lastReceiveTime : 0;
		ENetEvent ev;
		while (enet_host_service(host, &ev, 10) > 0)
		{
			switch (ev.type)
			{
			case ENET_EVENT_TYPE_CONNECT:
				if (ev.peer == mm)
					request();
				else if (ev.peer == relay)
					SendRelayText(relay, CONNECT_REQUEST, token);
				break;
			case ENET_EVENT_TYPE_RECEIVE:
				if (ev.peer == mm)
				{
					json msg;
					if (!ParseJson(ev.packet, msg))
						break;
					std::string type = msg.value("type", "");
					std::string error = msg.value("error", "");
					INFO_LOG(SLIPPI_ONLINE, "[Spectate] Daemon: %s %s %s", type.c_str(), msg.value("state", "").c_str(),
					         error.c_str());
					if (type == "spectate-request-resp" && !error.empty())
					{
						Show(Status::Refused, error);
						done = true;
					}
					else if (type == "spectate-request-resp" && msg.value("state", "") == "waiting")
					{
						Show(Status::Waiting, "Waiting for " + m_code + "'s next game...");
					}
					else if ((type == "spectate-request-resp" && msg.value("state", "") == "live") ||
					         type == "spectate-start")
					{
						goLive(msg);
					}
					else if (type == "spectate-end")
					{
						// The relay closes the link after end_game. This push can arrive before the relay's
						// last bytes, so the link stays open until then.
						std::string reason = msg.value("reason", "");
						if (reason == "game_end")
						{
							Show(Status::Waiting, "Waiting for " + sourceName + "'s next game...");
						}
						else
						{
							Show(Status::Ended, sourceName + " stopped playing. Waiting for their next game...");
							request();
						}
					}
				}
				else if (ev.peer == relay && ev.packet->dataLength >= 5)
				{
					const u8 *data = ev.packet->data;
					size_t length = ev.packet->dataLength;
					u32 cursor = ReadWord(data + 1);
					switch (data[0])
					{
					case START_GAME:
						newFile();
						break;
					case GAME_EVENT:
					{
						if (!file.IsOpen())
							newFile();
						u64 end = (u64)cursor + (length - 5);
						if (cursor > written || end <= written)
							break; // a gap, or bytes we already have
						size_t skip = (size_t)(written - cursor);
						if (end > MAX_STREAM || !check.Feed(data + 5 + skip, length - 5 - skip))
						{
							ERROR_LOG(SLIPPI_ONLINE, "[Spectate] Stopped: the stream is not a valid game after %llu bytes",
							          (unsigned long long)written);
							Show(Status::Refused, "This game's data is not valid. Stopped watching.");
							file.Close();
							done = true;
							break;
						}
						file.WriteBytes(data + 5 + skip, length - 5 - skip);
						file.Flush();
						written = end;
						if (!commPointed)
						{
							WriteComm(filePath);
							commPointed = true;
						}
						break;
					}
					case END_GAME:
						file.Close();
						break;
					case REFUSED:
						Show(Status::Refused, std::string((const char *)data + 5, length - 5));
						break;
					default:
						break;
					}
				}
				enet_packet_destroy(ev.packet);
				break;
			case ENET_EVENT_TYPE_DISCONNECT:
				if (ev.peer == mm)
				{
					// Ask again on a new connection; the server dropped the old request with it.
					WARN_LOG(SLIPPI_ONLINE, "[Spectate] The daemon connection closed (data %u), silent %llu ms",
					         ev.data, SilenceMs(host, mmHeard));
					Show(Status::Connecting, "Reconnecting to Lylat...");
					mm = nullptr;
					mmRetryAt = Common::Timer::GetTimeMs() + MM_RETRY_MS;
				}
				else if (ev.peer == relay)
				{
					relay = nullptr;
					file.Close();
				}
				break;
			default:
				break;
			}
		}

		u64 now = Common::Timer::GetTimeMs();
		if (!mm && now >= mmRetryAt)
		{
			mm = enet_host_connect(host, &mmAddr, SPECTATE_CHANNELS, 0);
			mmRetryAt = now + MM_RETRY_MS;
		}
		if (relay && now - lastKeepAliveAt >= KEEP_ALIVE_MS)
		{
			SendRelay(relay, KEEP_ALIVE, 0);
			lastKeepAliveAt = now;
		}
	}

	// Stopped by the player, or the session ended: let the server drop the wait.
	while (m_running)
		Common::SleepCurrentThread(100);
	if (mm)
	{
		SendJson(mm, Request("spectate-cancel", m_user->GetUserInfo()));
		enet_peer_disconnect_later(mm, 0);
	}
	if (relay)
		enet_peer_disconnect_later(relay, 0);
	ENetEvent ev;
	u64 deadline = Common::Timer::GetTimeMs() + 500;
	while ((mm || relay) && Common::Timer::GetTimeMs() < deadline)
	{
		if (enet_host_service(host, &ev, 50) <= 0)
			continue;
		if (ev.type == ENET_EVENT_TYPE_RECEIVE)
			enet_packet_destroy(ev.packet);
		else if (ev.type == ENET_EVENT_TYPE_DISCONNECT)
			(ev.peer == mm ? mm : relay) = nullptr;
	}
	enet_host_destroy(host);
	enet_deinitialize();
}
