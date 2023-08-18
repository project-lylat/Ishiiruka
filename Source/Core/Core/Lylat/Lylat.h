#pragma once
#include <string>

// #define LYLAT_ENV_STAGING "STAGING"
// #define LYLAT_ENV_SLIPPI "SLIPPI"
// #define LYLAT_ENV_DEV "DEV"
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
	static const std::string SLIPPI_HOSTNAME;
	static const std::string SLIPPI_USER_JSON_URL;
	static const std::string SLIPPI_GAME_REPORT_URL;
	static const std::string SLIPPI_ABANDON_GAME_REPORT_URL;
	static const std::string SLIPPI_COMPLETE_GAME_REPORT_URL;
	static const std::string SLIPPI_USER_LOGIN_URL;
	static const std::string SLIPPI_MATCHMAKING_URL;
	static const std::string SLIPPI_DEV_MATCHMAKING_URL;
	static const std::string LYLAT_STAGING_MATCHMAKING_URL;

	static const std::string HOSTNAME;
	static const std::string MATCHMAKING_URL;

	static const std::string USER_LOGIN_URL;
	static const std::string GAME_REPORT_URL;
	static const std::string ABANDON_GAME_REPORT_URL;
	static const std::string COMPLETE_GAME_REPORT_URL;
	static const std::string USER_JSON_URL;
	static const std::string UPDATE_URL;
};