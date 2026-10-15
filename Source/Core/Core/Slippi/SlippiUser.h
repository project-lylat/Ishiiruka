#pragma once

#include "Common/CommonTypes.h"
#include <atomic>
#include <curl/curl.h>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Upstream turned this class into a shim over the Rust user manager, which reads
// `user.json` and talks to the Slippi servers.
//
// Lylat: the account still lives in the C++ side. It is read from `lylat.json`
// (File::GetSlippiUserJSONPath), refreshed from the Lylat server and may hold a linked
// Slippi account under its `slippi` key. The Rust device is still used for default chat
// messages; everything else about the account is handled here.
//
// A quirk of this is that we're using the EXI device pointer, so this class absolutely
// cannot outlive the EXI device - but we control that and just need to do our due diligence
// when making changes.
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

		std::vector<std::string> chatMessages;

		bool isBot = false;

		float rankedRating = 0;
		int rankedUpdateCount = 0;
		int rankedGlobalPlacement = 0;
		int rankedRegionalPlacement = 0;
	};

	SlippiUser(uintptr_t rs_exi_device_ptr);
	~SlippiUser();

	bool AttemptLogin();
	void OpenLogInPage();
	bool UpdateApp();
	void ListenForLogIn();
	void LogOut();
	void OverwriteLatestVersion(std::string version);
	UserInfo GetUserInfo();
	std::vector<std::string> GetUserChatMessages();
	std::vector<std::string> GetDefaultChatMessages();
	bool IsLoggedIn();
	void FileListenThread();
	bool HasSlippiInfo();

	// Lylat: the base URL of the Lylat site (login page, user API, device sign-in).
	static std::string GetLylatOrigin();
	// Lylat: opens the browser login page (the "manual login" flow).
	static void OpenBrowserLogInPage();

	// Lylat: set by the UI. Called by OpenLogInPage to show the QR sign-in dialog; when it
	// is not set, the browser login page opens instead. Called from the emulation thread.
	static std::function<void()> s_showLoginDialog;
	// Lylat: set by the UI. Called once a login is picked up by the file watcher.
	static std::function<void()> s_onLoggedIn;

  protected:
	UserInfo parseFile(std::string fileContents);
	void deleteFile();
	void overwriteFromServer();

	// A pointer to a "shadow" EXI Device that lives on the Rust side of things.
	// Do *not* do any cleanup of this! The EXI device will handle it.
	uintptr_t slprs_exi_device_ptr;

	std::mutex userInfoMutex;
	UserInfo userInfo;
	std::atomic<bool> isLoggedIn{false};

	CURL *m_curl = nullptr;
	struct curl_slist *m_curlHeaderList = nullptr;

	std::thread fileListenThread;
	std::atomic<bool> runThread{false};
};
