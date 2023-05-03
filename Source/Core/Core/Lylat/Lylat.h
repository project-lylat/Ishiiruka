#pragma once

//#define LYLAT_ENV_STAGING "STAGING"
//#define LYLAT_ENV_SLIPPI "SLIPPI"
//#define LYLAT_ENV_DEV "DEV"
#define LYLAT_ENV_PRODUCTION "PRODUCTION"

#ifdef LYLAT_ENV_STAGING
#define LYLAT_STAGING
#elif defined(LYLAT_ENV_DEV)
#define LYLAT_DEV
#elif defined(LYLAT_ENV_SLIPPI)
#define LYLAT_SLIPPI
#else
#define LYLAT_PRODUCTION
#endif

class Lylat
{
  public:
	static const inline std::string SLIPPI_HOSTNAME = "https://slippi.gg";
	static const inline std::string SLIPPI_USER_JSON_URL = "https://users-rest-dot-slippi.uc.r.appspot.com/user";
	static const inline std::string SLIPPI_GAME_REPORT_URL = "https://rankings-dot-slippi.uc.r.appspot.com/report";
	static const inline std::string SLIPPI_ABANDON_GAME_REPORT_URL = "https://rankings-dot-slippi.uc.r.appspot.com/abandon";
	static const inline std::string SLIPPI_COMPLETE_GAME_REPORT_URL = "https://rankings-dot-slippi.uc.r.appspot.com/complete";
	static const inline std::string SLIPPI_USER_LOGIN_URL = SLIPPI_HOSTNAME + "/online/enable";
	static const inline std::string SLIPPI_MATCHMAKING_URL = "mm.slippi.gg";
	static const inline std::string SLIPPI_DEV_MATCHMAKING_URL = "mm2.slippi.gg";
	static const inline std::string LYLAT_STAGING_MATCHMAKING_URL = "159.203.86.6";

#ifdef LYLAT_STAGING
	static const inline std::string HOSTNAME = "http://159.203.86.6";
	static const inline std::string MATCHMAKING_URL = HOSTNAME;
#elif defined(LYLAT_DEV)
	static const inline std::string HOSTNAME = "http://localhost:3000";
	static const inline std::string MATCHMAKING_URL = "http://localhost";
#elif defined(LYLAT_SLIPPI)
	static const inline std::string HOSTNAME = "https://slippi.gg";
	static const inline std::string MATCHMAKING_URL = HOSTNAME;
#else
	static const inline std::string HOSTNAME = "https://lylat.gg";
	static const inline std::string MATCHMAKING_URL = HOSTNAME;
#endif

#if defined(LYLAT_SLIPPI)
	static const inline std::string USER_LOGIN_URL = SLIPPI_USER_LOGIN_URL;
	static const inline std::string GAME_REPORT_URL = SLIPPI_GAME_REPORT_URL;
	static const inline std::string USER_JSON_URL = SLIPPI_USER_JSON_URL;
#else
	static const inline std::string USER_LOGIN_URL = HOSTNAME + "/users/enable";
	static const inline std::string GAME_REPORT_URL = HOSTNAME + "/reports";
	static const inline std::string ABANDON_GAME_REPORT_URL = HOSTNAME + "/reports/abandon"; // TODO: create on server
	static const inline std::string COMPLETE_GAME_REPORT_URL = HOSTNAME + "/reports/complete"; // TODO: create on server
	static const inline std::string USER_JSON_URL = HOSTNAME + "/users/slp";
	static const inline std::string UPDATE_URL = HOSTNAME + "/downloads/melee";
#endif
};