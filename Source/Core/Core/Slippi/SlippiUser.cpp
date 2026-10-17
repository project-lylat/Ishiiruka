#include "SlippiUser.h"

#include "Common/Common.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/StringUtil.h"
#include "Common/Thread.h"

#include "SlippiRustExtensions.h"

std::function<void()> SlippiUser::s_showLoginDialog;
std::function<void()> SlippiUser::s_onLoggedIn;

// Takes a RustChatMessages pointer and extracts messages from them, then
// frees the underlying memory safely.
static std::vector<std::string> ConvertChatMessagesFromRust(RustChatMessages *rsMessages)
{
	std::vector<std::string> chatMessages;

	for (int i = 0; i < rsMessages->len; i++)
	{
		std::string message = std::string(rsMessages->data[i]);
		chatMessages.push_back(message);
	}

	slprs_user_free_messages(rsMessages);

	return chatMessages;
}

static void RunSystemCommand(const std::string &command)
{
#ifdef _WIN32
	_wsystem(UTF8ToUTF16(command).c_str());
#else
	system(command.c_str());
#endif
}

SlippiUser::SlippiUser(uintptr_t rs_exi_device_ptr)
{
	slprs_exi_device_ptr = rs_exi_device_ptr;
}

SlippiUser::~SlippiUser()
{
	// Wait for thread to terminate
	runThread = false;
	if (fileListenThread.joinable())
		fileListenThread.join();
}

// Lylat: the site of the environment the Rust extensions were built for (SLIPPI_ENV, or the
// SLIPPI_LYLAT_ORIGIN override).
std::string SlippiUser::GetLylatOrigin()
{
	char *rsOrigin = slprs_lylat_origin();
	std::string origin(rsOrigin);
	slprs_lylat_free_string(rsOrigin);
	while (!origin.empty() && origin.back() == '/')
		origin.pop_back();
	return origin;
}

bool SlippiUser::AttemptLogin()
{
	// Lylat: some browsers save the file the login page writes as lylat.json.txt.
	std::string userFilePath = File::GetSlippiUserJSONPath();
	std::string userFilePathTxt = userFilePath + ".txt";
	if (File::Exists(userFilePathTxt))
	{
		// If both files exist we just log they exist and take no further action
		if (File::Exists(userFilePath))
		{
			INFO_LOG(SLIPPI_ONLINE,
			         "Found both .json.txt and .json file for user data. Using .json and ignoring the .json.txt");
		}
		// If only the .txt file exists move the contents to a json file and log if it fails
		else if (!File::Rename(userFilePathTxt, userFilePath))
		{
			WARN_LOG(SLIPPI_ONLINE, "Could not move file %s to %s", userFilePathTxt.c_str(), userFilePath.c_str());
		}
	}

	return slprs_user_attempt_login(slprs_exi_device_ptr);
}

// Lylat: the browser login page (the "manual login" flow), for the QR dialog's "Manual login".
// The page writes lylat.json to the given path, the same way the Slippi page wrote user.json.
void SlippiUser::OpenBrowserLogInPage()
{
	std::string url = GetLylatOrigin() + "/users/enable";
	std::string path = File::GetSlippiUserJSONPath();

#ifdef _WIN32
	// On windows, sometimes the path can have backslashes and slashes mixed, convert all to backslashes
	path = ReplaceAll(path, "/", "\\");
#endif

	std::string fullUrl = url + "?path=" + path;
	INFO_LOG(SLIPPI_ONLINE, "[User] Login at path: %s", fullUrl.c_str());

#ifdef _WIN32
	std::string command = "explorer \"" + fullUrl + "\"";
#elif defined(__APPLE__)
	std::string command = "open \"" + fullUrl + "\"";
#else
	std::string command = "xdg-open \"" + fullUrl + "\""; // Linux
#endif

	RunSystemCommand(command);
}

// Lylat: the in-game "log in" action. The UI shows the QR sign-in dialog when it has
// registered one; the dialog falls back to the browser page itself (404 or "Manual login").
void SlippiUser::OpenLogInPage()
{
	if (s_showLoginDialog)
	{
		s_showLoginDialog();
		return;
	}

	slprs_user_open_login_page(slprs_exi_device_ptr);
}

bool SlippiUser::UpdateApp()
{
	return slprs_user_update_app(slprs_exi_device_ptr);
}

// Lylat: polls like the Rust watcher (slprs_user_listen_for_login) so the UI can be told
// when the sign-in lands.
void SlippiUser::ListenForLogIn()
{
	if (runThread)
		return;

	if (fileListenThread.joinable())
		fileListenThread.join();

	runThread = true;
	fileListenThread = std::thread(&SlippiUser::FileListenThread, this);
}

void SlippiUser::FileListenThread()
{
	while (runThread)
	{
		if (AttemptLogin())
		{
			runThread = false;
			if (s_onLoggedIn)
				s_onLoggedIn();
			break;
		}

		Common::SleepCurrentThread(500);
	}
}

void SlippiUser::LogOut()
{
	runThread = false;
	slprs_user_logout(slprs_exi_device_ptr);
}

void SlippiUser::OverwriteLatestVersion(std::string version)
{
	slprs_user_overwrite_latest_version(slprs_exi_device_ptr, version.c_str());
}

SlippiUser::UserInfo SlippiUser::GetUserInfo()
{
	SlippiUser::UserInfo userInfo;

	RustUserInfo *info = slprs_user_get_info(slprs_exi_device_ptr);
	userInfo.uid = std::string(info->uid);
	userInfo.playKey = std::string(info->play_key);
	userInfo.displayName = std::string(info->display_name);
	userInfo.connectCode = std::string(info->connect_code);
	userInfo.latestVersion = std::string(info->latest_version);
	slprs_user_free_info(info);

	return userInfo;
}

std::vector<std::string> SlippiUser::GetDefaultChatMessages()
{
	RustChatMessages *chatMessages = slprs_user_get_default_messages(slprs_exi_device_ptr);
	return ConvertChatMessagesFromRust(chatMessages);
}

std::vector<std::string> SlippiUser::GetUserChatMessages()
{
	RustChatMessages *chatMessages = slprs_user_get_messages(slprs_exi_device_ptr);
	return ConvertChatMessagesFromRust(chatMessages);
}

bool SlippiUser::IsLoggedIn()
{
	return slprs_user_get_is_logged_in(slprs_exi_device_ptr);
}
