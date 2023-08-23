#include "Lylat.h"



const std::string Lylat::SLIPPI_HOSTNAME = "https://slippi.gg";
const std::string Lylat::SLIPPI_USER_JSON_URL = "https://users-rest-dot-slippi.uc.r.appspot.com/user";
const std::string Lylat::SLIPPI_GAME_REPORT_URL = "https://gql-gateway-dev-dot-slippi.uc.r.appspot.com/graphql";
const std::string Lylat::SLIPPI_ABANDON_GAME_REPORT_URL = SLIPPI_GAME_REPORT_URL;
const std::string Lylat::SLIPPI_COMPLETE_GAME_REPORT_URL = SLIPPI_GAME_REPORT_URL;
const std::string Lylat::SLIPPI_USER_LOGIN_URL = SLIPPI_HOSTNAME + "/online/enable";
const std::string Lylat::SLIPPI_MATCHMAKING_URL = "mm.slippi.gg";
const std::string Lylat::SLIPPI_DEV_MATCHMAKING_URL = "mm2.slippi.gg";
const std::string Lylat::LYLAT_STAGING_MATCHMAKING_URL = "159.203.86.6";

#ifdef LYLAT_STAGING
const std::string Lylat::HOSTNAME = "http://159.203.86.6";
const std::string Lylat::MATCHMAKING_URL = HOSTNAME;
#elif defined(LYLAT_DEV)
const std::string Lylat::HOSTNAME = "http://localhost:3000";
const std::string Lylat::MATCHMAKING_URL = "http://localhost";
#elif defined(LYLAT_SLIPPI)
const std::string Lylat::HOSTNAME = "https://slippi.gg";
const std::string Lylat::MATCHMAKING_URL = HOSTNAME;
#else
const std::string Lylat::HOSTNAME = "https://lylat.gg";
const std::string Lylat::MATCHMAKING_URL = HOSTNAME;
#endif

#if defined(LYLAT_SLIPPI)
const std::string Lylat::USER_LOGIN_URL = SLIPPI_USER_LOGIN_URL;
const std::string Lylat::GAME_REPORT_URL = SLIPPI_GAME_REPORT_URL;
const std::string Lylat::USER_JSON_URL = SLIPPI_USER_JSON_URL;
#else
const std::string Lylat::USER_LOGIN_URL = HOSTNAME + "/users/enable";
const std::string Lylat::GAME_REPORT_URL = HOSTNAME + "/reports";
const std::string Lylat::ABANDON_GAME_REPORT_URL = HOSTNAME + "/reports/abandon";   // TODO: create on server
const std::string Lylat::COMPLETE_GAME_REPORT_URL = HOSTNAME + "/reports/complete"; // TODO: create on server
const std::string Lylat::USER_JSON_URL = HOSTNAME + "/users/slp";
const std::string Lylat::UPDATE_URL = HOSTNAME + "/downloads/melee";
#endif