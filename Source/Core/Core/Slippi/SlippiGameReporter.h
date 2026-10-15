#pragma once

#include "Common/CommonTypes.h"
#include "Common/FifoQueue.h"
#include "Core/Slippi/SlippiUser.h"
#include "SlippiMatchmaking.h"

#include <atomic>
#include <condition_variable> // std::condition_variable
#include <curl/curl.h>
#include <mutex> // std::mutex, std::unique_lock
#include <string>
#include <thread>
#include <vector>

// Lylat: reports games that were routed to the Lylat matchmaking server. They are posted
// to the custom reporting URL (SlippiCustomMatchmakingReportingURL) with the Lylat account.
// Games routed to Slippi are reported by the Rust game reporter instead.
class SlippiGameReporter
{
  public:
	struct PlayerReport
	{
		std::string uid;
		float damageDone;
		u8 stocksRemaining;
	};
	struct GameReport
	{
		SlippiMatchmaking::OnlinePlayMode mode = SlippiMatchmaking::OnlinePlayMode::UNRANKED;
		u32 durationFrames = 0;
		std::vector<PlayerReport> players;
	};

	SlippiGameReporter(SlippiUser *user, bool isMex);
	~SlippiGameReporter();

	void StartReport(GameReport report);
	void StartNewSession();
	void ReportThreadHandler();

  protected:
	CURL *m_curl = nullptr;
	struct curl_slist *m_curlHeaderList = nullptr;
	bool isMex = false;

	u32 gameIndex = 1;

	SlippiUser *m_user;
	Common::FifoQueue<GameReport, false> gameReportQueue;
	std::thread reportingThread;
	std::mutex mtx;
	std::condition_variable cv;
	std::atomic<bool> runThread;
};
