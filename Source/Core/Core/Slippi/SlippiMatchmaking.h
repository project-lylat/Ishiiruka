#pragma once

#include "Common/CommonTypes.h"
#include "Common/Thread.h"
#include "Core/Lylat/Lylat.h"
#include "Core/Slippi/SlippiNetplay.h"
#include "Core/Slippi/SlippiUser.h"

#ifndef _WIN32
#include <arpa/inet.h>
#include <netdb.h>
#endif

#include <json.hpp>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

class SlippiMatchmaking
{
  public:
	SlippiMatchmaking(SlippiUser *user, bool isMex);
	~SlippiMatchmaking();

	enum OnlinePlayMode
	{
		RANKED = 0,
		UNRANKED = 1,
		DIRECT = 2,
		TEAMS = 3,
	};

	enum ProcessState
	{
		IDLE,
		INITIALIZING,
		MATCHMAKING,
		OPPONENT_CONNECTING,
		CONNECTION_SUCCESS,
		ERROR_ENCOUNTERED,
	};

	struct MatchSearchSettings
	{
		OnlinePlayMode mode = OnlinePlayMode::RANKED;
		std::string connectCode = "";
	};

	struct MatchmakeResult
	{
		std::string id = "";
		std::vector<SlippiUser::UserInfo> players;
		std::vector<u16> stages;
	};

	void FindMatch(MatchSearchSettings settings, bool isMex);
	void MatchmakeThread();
	ProcessState GetMatchmakeState();
	bool IsSearching();
	std::unique_ptr<SlippiNetplayClient> GetNetplayClient();
	std::string GetErrorMessage();
	int LocalPlayerIndex();
	std::vector<SlippiUser::UserInfo> GetPlayerInfo();
	std::string GetPlayerName(u8 port);
	std::vector<u16> GetStages();
	u8 RemotePlayerCount();
	MatchmakeResult GetMatchmakeResult();
	static bool IsFixedRulesMode(OnlinePlayMode mode);
	static bool IsMexMode(SlippiUser *user, OnlinePlayMode mode);
	static bool IsCurrentGameMex();

  protected:
	const std::string MM_HOST_DEV = Lylat::SLIPPI_DEV_MATCHMAKING_URL;
	const std::string MM_HOST_PROD = Lylat::SLIPPI_MATCHMAKING_URL;
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

	const std::unordered_map<ProcessState, bool> searchingStates = {
	    {ProcessState::INITIALIZING, true},
	    {ProcessState::MATCHMAKING, true},
	    {ProcessState::OPPONENT_CONNECTING, true},
	};

	std::string getSlippiMMHost();
	std::string getMexMMHost();
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
