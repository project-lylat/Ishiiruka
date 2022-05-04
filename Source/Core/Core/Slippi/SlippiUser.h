#pragma once

#include "Common/CommonTypes.h"
#include <atomic>
#include <curl/curl.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class SlippiUser
{
  public:
	struct UserInfo
	{
		std::string uid = "";
		std::string playKey = "";
		std::string displayName = "";
		std::string connectCode = "";
		std::string latestVersion = "";
		std::string lylatVersion = "";
		std::string fileContents = "";

		std::string slippi_uid = "";
		std::string slippi_playKey = "";
		std::string slippi_connectCode = "";


		int port;
	};

	SlippiUser();
	~SlippiUser();

	bool AttemptLogin();
	void OpenLogInPage();
	bool UpdateApp();
	void ListenForLogIn();
	void LogOut();
	void OverwriteLatestVersion(std::string version);
	UserInfo GetUserInfo();
	bool IsLoggedIn();
	void FileListenThread();
	bool HasSlippiInfo();


  protected:
	UserInfo parseFile(std::string fileContents);
	void deleteFile();
	void overwriteFromServer();

	UserInfo userInfo;
	bool isLoggedIn = false;

	// TODO: use production lylat route
//	const std::string URL_START = "https://lylat.gg/users";
//	const std::string URL_START = "http://localhost:3000/users/slp";
//	const std::string URL_START = "https://users-rest-dot-slippi.uc.r.appspot.com/user";
	const std::string URL_START = "https://lylat.gg/users/slp";

	CURL *m_curl = nullptr;
	struct curl_slist *m_curlHeaderList = nullptr;
	std::vector<char> receiveBuf;

	std::thread fileListenThread;
	std::atomic<bool> runThread;
};
