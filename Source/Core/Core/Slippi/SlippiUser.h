#pragma once

#include "Common/CommonTypes.h"
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// This class is currently a shim for the Rust user interface. We're doing it this way
// to begin migrating things over without needing to do larger invasive changes.
//
// The remaining methods on here are simply layers that direct the call over to the Rust
// side. A quirk of this is that we're using the EXI device pointer, so this class absolutely
// cannot outlive the EXI device - but we control that and just need to do our due diligence
// when making changes.
//
// Lylat: the Rust device reads the account (see CEXISlippi's constructor). Vanilla takes Slippi
// credentials only: the Slippi account linked in `lylat.json`, else `user.json`. Mods take the
// Lylat account in `lylat.json` and nothing else. This class keeps the Lylat sign-in: the QR
// dialog, the browser page and the "logged in" callback.
class SlippiUser
{
  public:
	// This type is filled in with data from the Rust side.
	// Eventually, this entire class will disappear.
	struct UserInfo
	{
		std::string uid = "";
		std::string playKey = "";
		std::string displayName = "";
		std::string connectCode = "";
		std::string latestVersion = "";

		int port;

		std::vector<std::string> chatMessages;

		bool isBot = false;

		float rankedRating = 0;
		int rankedUpdateCount = 0;
		int rankedGlobalPlacement = 0;
		int rankedRegionalPlacement = 0;
		float rankedMmr = 0;
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

	// Lylat: the base URL of the Lylat site (QR device sign-in).
	static std::string GetLylatOrigin();
	// Lylat: opens the browser login page (the "manual login" flow).
	static void OpenBrowserLogInPage();

	// Lylat: set by the UI. Called by OpenLogInPage to show the QR sign-in dialog; when it
	// is not set, the browser login page opens instead. Called from the emulation thread.
	static std::function<void()> s_showLoginDialog;
	// Lylat: set by the UI. Called once a login is picked up by the file watcher.
	static std::function<void()> s_onLoggedIn;

  protected:
	void FileListenThread();

	// A pointer to a "shadow" EXI Device that lives on the Rust side of things.
	// Do *not* do any cleanup of this! The EXI device will handle it.
	uintptr_t slprs_exi_device_ptr;

	// Lylat: waits for the sign-in to write the account file, then calls s_onLoggedIn.
	std::thread fileListenThread;
	std::atomic<bool> runThread{false};
};
