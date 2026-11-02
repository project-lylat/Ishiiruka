#pragma once

// Spectating through the Lylat relay. Relay messages: u8 type, u32 BE cursor, payload.

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

	// Called from the EXI device on the CPU thread.
	void GameStart(const std::string &matchId, const SlippiUser::UserInfo &user);
	void Write(const u8 *data, u32 length);
	void GameEnd();
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
	std::vector<std::string> m_presence;
};

// The Gecko codes this build ships, run in place of a spectated replay's codes.
class LylatShippedCodes
{
  public:
	LylatShippedCodes();

	// The closest shipped code with the same first word and length, or nullptr.
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
