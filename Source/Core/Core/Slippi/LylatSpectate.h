#pragma once

// Spectating through Lylat. Every stream goes through the Lylat relay: the
// player being watched publishes one stream, the relay sends it to every
// spectator, and nobody learns anyone else's address.
//
// Matchmaking daemon (ENet JSON, the matchmaking port): spectate-presence from
// a source, spectate-request and spectate-cancel from a spectator, and the
// spectate-start and spectate-end pushes.
//
// Relay (ENet binary, channel 0, reliable): u8 type, u32 BE cursor, payload.
//   1 connect_request  spectator to relay; payload: the session token
//   2 connect_reply    relay to spectator; payload: u32 BE game index, u8 in_game
//   3 start_game       source to relay, relay to spectator; payload: u32 BE game index
//   4 game_event       raw .slp event bytes; cursor = stream offset of the first byte
//   5 end_game         cursor = final offset
//   6 keep_alive       both ways
//   7 refused          relay to either; payload: text for the player
//   8 publish          source to relay; payload: the publish token

#include <atomic>
#include <mutex>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "Common/CommonTypes.h"
#include "Core/Slippi/SlippiUser.h"

class LylatSpectateSource
{
  public:
	static LylatSpectateSource &Get();

	// Called from the EXI device on the CPU thread. A game starts with the
	// match it belongs to; bytes written after that are the game's stream.
	void GameStart(const std::string &matchId, const SlippiUser::UserInfo &user);
	void Write(const u8 *data, u32 length);
	void GameEnd();
	// The set is over (connection cleanup or the device closing).
	void Leave();

	LylatSpectateSource(LylatSpectateSource const &) = delete;
	void operator=(LylatSpectateSource const &) = delete;

  private:
	LylatSpectateSource() = default;
	~LylatSpectateSource();

	void Run();

	std::mutex m_mutex;
	std::thread m_thread;
	std::atomic<bool> m_running{false};

	// Shared with the CPU thread, under m_mutex.
	SlippiUser::UserInfo m_user;
	std::string m_matchId;
	int m_gameIndex = -1;
	bool m_inGame = false;
	bool m_left = true;
	std::vector<u8> m_stream;
	std::vector<std::string> m_presence; // states waiting to be sent
};

// The Gecko codes this build ships for the running game. A spectated replay's code list comes from
// another player's Dolphin, so the viewer runs this build's copy of each code instead of the sent one.
class LylatShippedCodes
{
  public:
	LylatShippedCodes();

	// This build's copy of the replay's code, or nullptr when the build ships no code with the same
	// first line and length. A replay holds codes as they sat in memory: the branch back the code
	// handler writes and the data some codes keep inside themselves differ from the ini, so the copy
	// with the fewest different bytes wins.
	const std::vector<u8> *Find(const u8 *code, size_t length) const;

  private:
	std::multimap<std::pair<u32, size_t>, std::vector<u8>> m_codes; // by first word and length
};

class LylatSpectateViewer
{
  public:
	enum class Status
	{
		Connecting,
		Waiting,
		Live,
		Refused,
		Ended,
	};

	// Starts watching connectCode. The stream goes to files in
	// User/Slippi/Spectate and the mirror comm file at CommPath().
	static void Start(const std::string &connectCode, SlippiUser *user);
	static void Stop();
	static std::string CommPath();

	~LylatSpectateViewer();

  private:
	LylatSpectateViewer(const std::string &connectCode, SlippiUser *user);

	void Run();
	void WriteComm(const std::string &replayPath);
	void Show(Status status, const std::string &text);

	std::string m_code;
	SlippiUser *m_user;
	std::thread m_thread;
	std::atomic<bool> m_running{true};
	int m_fileCount = 0;
};
