#pragma once

// Lylat: matchmaking for a Lylat build.
//
// Slippi's SlippiMatchmaking talks to Slippi's WebSocket matchmaking service (through the Rust
// extensions) with STUN. The Lylat server still speaks the older ENet protocol, so mods use
// LylatMatchmaking, which is the ENet client Ishiiruka had before Slippi's change, kept as it was.
// The EXI device holds either one behind the Matchmaker interface, chosen when the game boots
// (CreateMatchmaker): vanilla gets Slippi's, mods get Lylat's. SlippiMatchmaking stays exactly as
// upstream wrote it.

#include "Common/CommonTypes.h"
#include "Common/Thread.h"
#include "Core/Slippi/SlippiMatchmaking.h"
#include "Core/Slippi/SlippiNetplay.h"
#include "Core/Slippi/SlippiUser.h"

#ifndef _WIN32
#include <arpa/inet.h>
#include <netdb.h>
#endif

#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <json.hpp>
using json = nlohmann::json;

// What the EXI device needs from a matchmaker. The types are SlippiMatchmaking's.
class Matchmaker
{
  public:
	using OnlinePlayMode = SlippiMatchmaking::OnlinePlayMode;
	using ProcessState = SlippiMatchmaking::ProcessState;
	using SlippiRank = SlippiMatchmaking::SlippiRank;
	using MatchSearchSettings = SlippiMatchmaking::MatchSearchSettings;
	using MatchmakeResult = SlippiMatchmaking::MatchmakeResult;

	virtual ~Matchmaker() = default;

	virtual void FindMatch(MatchSearchSettings settings) = 0;
	virtual ProcessState GetMatchmakeState() = 0;
	virtual bool IsSearching() = 0;
	virtual std::unique_ptr<SlippiNetplayClient> GetNetplayClient() = 0;
	virtual std::string GetErrorMessage() = 0;
	virtual int LocalPlayerIndex() = 0;
	virtual std::vector<SlippiUser::UserInfo> GetPlayerInfo() = 0;
	virtual std::string GetPlayerName(u8 port) = 0;
	virtual SlippiRank GetPlayerRank(u8 port) = 0;
	virtual std::vector<u16> GetStages() = 0;
	virtual u8 RemotePlayerCount() = 0;
	virtual MatchmakeResult GetMatchmakeResult() = 0;
};

// Slippi's matchmaking (vanilla), unchanged, behind the Matchmaker interface.
class SlippiMatchmaker : public Matchmaker
{
  public:
	SlippiMatchmaker(uintptr_t rs_exi_device_ptr, SlippiUser *user)
	    : m_mm(std::make_unique<SlippiMatchmaking>(rs_exi_device_ptr, user))
	{
	}

	void FindMatch(MatchSearchSettings settings) override { m_mm->FindMatch(settings); }
	ProcessState GetMatchmakeState() override { return m_mm->GetMatchmakeState(); }
	bool IsSearching() override { return m_mm->IsSearching(); }
	std::unique_ptr<SlippiNetplayClient> GetNetplayClient() override { return m_mm->GetNetplayClient(); }
	std::string GetErrorMessage() override { return m_mm->GetErrorMessage(); }
	int LocalPlayerIndex() override { return m_mm->LocalPlayerIndex(); }
	std::vector<SlippiUser::UserInfo> GetPlayerInfo() override { return m_mm->GetPlayerInfo(); }
	std::string GetPlayerName(u8 port) override { return m_mm->GetPlayerName(port); }
	SlippiRank GetPlayerRank(u8 port) override { return m_mm->GetPlayerRank(port); }
	std::vector<u16> GetStages() override { return m_mm->GetStages(); }
	u8 RemotePlayerCount() override { return m_mm->RemotePlayerCount(); }
	MatchmakeResult GetMatchmakeResult() override { return m_mm->GetMatchmakeResult(); }

  private:
	std::unique_ptr<SlippiMatchmaking> m_mm;
};

// The ENet matchmaking client for the Lylat server (mods).
class LylatMatchmaking : public Matchmaker
{
  public:
	LylatMatchmaking(uintptr_t rs_exi_device_ptr, SlippiUser *user, bool isMex);
	~LylatMatchmaking();

	void FindMatch(MatchSearchSettings settings) override;
	void MatchmakeThread();
	ProcessState GetMatchmakeState() override;
	bool IsSearching() override;
	std::unique_ptr<SlippiNetplayClient> GetNetplayClient() override;
	std::string GetErrorMessage() override;
	int LocalPlayerIndex() override;
	std::vector<SlippiUser::UserInfo> GetPlayerInfo() override;
	std::string GetPlayerName(u8 port) override;
	SlippiRank GetPlayerRank(u8 port) override;
	std::vector<u16> GetStages() override;
	u8 RemotePlayerCount() override;
	MatchmakeResult GetMatchmakeResult() override;

	// Whether a game is played on the Lylat servers: every mode of a mod (a sub game other than
	// GALE01, or an m-ex disc); vanilla goes to Slippi. `user` and `mode` are unused.
	static bool IsMexMode(SlippiUser *user, bool isCurrentGameMex, OnlinePlayMode mode);

  protected:
	const std::string MM_HOST_DEV = "mm2.slippi.gg";
	const std::string MM_HOST_PROD = "mm.slippi.gg";
	const u16 MM_PORT = 43113;

	std::string MM_HOST = "";
	bool isMex = false;

	ENetHost *m_client;
	ENetPeer *m_server;

	std::default_random_engine generator;

	bool isMmConnected = false;
	bool isMmTerminated = false;

	std::thread m_matchmakeThread;

	MatchSearchSettings m_searchSettings;

	ProcessState m_state;
	std::string m_errorMsg = "";

	SlippiUser *m_user;

	int m_isSwapAttempt = false;

	int m_hostPort;
	int m_localPlayerIndex;
	std::vector<std::string> m_remoteIps;
	MatchmakeResult m_mmResult;
	std::vector<SlippiUser::UserInfo> m_playerInfo;
	std::vector<u16> m_allowedStages;
	bool m_joinedLobby;
	bool m_isHost;

	std::unique_ptr<SlippiNetplayClient> m_netplayClient;

	// A pointer to a "shadow" EXI Device that lives on the Rust side of things.
	// Do *not* do any cleanup of this! The EXI device will handle it.
	uintptr_t slprs_exi_device_ptr;

	const std::unordered_map<ProcessState, bool> searchingStates = {
	    {ProcessState::INITIALIZING, true},
	    {ProcessState::MATCHMAKING, true},
	    {ProcessState::OPPONENT_CONNECTING, true},
	};

	std::string getSlippiMMHost();
	std::string getMexMMHost();
	u16 getMMPortForSearchMode();
	std::string getMMHostForSearchMode();

	void disconnectFromServer();
	void terminateMmConnection();
	void sendMessage(json msg);
	int receiveMessage(json &msg, int maxAttempts);

	void sendHolePunchMsg(std::string remoteIp, u16 remotePort, u16 localPort);

	void startMatchmaking();
	void handleMatchmaking();
	void handleConnecting();
};

// Slippi's matchmaking for vanilla, Lylat's for mods (LylatMatchmaking::IsMexMode).
inline std::unique_ptr<Matchmaker> CreateMatchmaker(uintptr_t rs_exi_device_ptr, SlippiUser *user, bool lylatServers,
                                                    bool isMex)
{
	if (lylatServers)
		return std::make_unique<LylatMatchmaking>(rs_exi_device_ptr, user, isMex);
	return std::make_unique<SlippiMatchmaker>(rs_exi_device_ptr, user);
}
