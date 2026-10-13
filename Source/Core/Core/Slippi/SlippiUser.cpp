#include "SlippiUser.h"

#include "Common/Common.h"
#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "Common/StringUtil.h"
#include "Common/Thread.h"

#include "Core/ConfigManager.h"

#include "SlippiRustExtensions.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

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

static size_t receive(char *ptr, size_t size, size_t nmemb, void *rcvBuf)
{
	size_t len = size * nmemb;
	INFO_LOG(SLIPPI_ONLINE, "[User] Received data: %d", (int)len);

	std::string *buf = (std::string *)rcvBuf;

	buf->insert(buf->end(), ptr, ptr + len);

	return len;
}

SlippiUser::SlippiUser(uintptr_t rs_exi_device_ptr)
{
	slprs_exi_device_ptr = rs_exi_device_ptr;

	CURL *curl = curl_easy_init();
	if (curl)
	{
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &receive);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000);

		// Set up HTTP Headers
		m_curlHeaderList = curl_slist_append(m_curlHeaderList, "Content-Type: application/json");
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, m_curlHeaderList);

#ifdef _WIN32
		// ALPN support is enabled by default but requires Windows >= 8.1.
		curl_easy_setopt(curl, CURLOPT_SSL_ENABLE_ALPN, false);
#endif

		m_curl = curl;
	}
}

SlippiUser::~SlippiUser()
{
	// Wait for thread to terminate
	runThread = false;
	if (fileListenThread.joinable())
		fileListenThread.join();

	if (m_curl)
	{
		curl_slist_free_all(m_curlHeaderList);
		curl_easy_cleanup(m_curl);
	}
}

std::string SlippiUser::GetLylatOrigin()
{
	std::string origin = SConfig::GetInstance().m_lylatServerOrigin;
	if (origin.empty())
		origin = "https://lylat.gg";
	while (!origin.empty() && origin.back() == '/')
		origin.pop_back();
	return origin;
}

bool SlippiUser::AttemptLogin()
{
	std::string userFilePath = File::GetSlippiUserJSONPath();

	{
		std::string userFilePathTxt =
		    userFilePath + ".txt"; // Put the filename here in its own scope because we don't really need it elsewhere
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
	}

	// Get user file
	std::string userFileContents;
	File::ReadFileToString(userFilePath, userFileContents);

	UserInfo info = parseFile(userFileContents);
	{
		std::lock_guard<std::mutex> lk(userInfoMutex);
		userInfo = info;
	}

	bool loggedIn = !info.uid.empty();
	if (loggedIn)
	{
		overwriteFromServer();
		WARN_LOG(SLIPPI_ONLINE, "Found user %s (%s)", info.displayName.c_str(), info.uid.c_str());
	}

	isLoggedIn = loggedIn;
	return loggedIn;
}

// Lylat: the browser login page (the "manual login" flow). The page writes lylat.json to
// the given path, the same way the Slippi page wrote user.json.
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

	OpenBrowserLogInPage();
}

bool SlippiUser::UpdateApp()
{
	CriticalAlertT("Automatic updates are not currently available for Lylat. Please get the latest update from "
	               "https://lylat.gg/download.");
	return true;
}

void SlippiUser::ListenForLogIn()
{
	if (runThread)
		return;

	if (fileListenThread.joinable())
		fileListenThread.join();

	runThread = true;
	fileListenThread = std::thread(&SlippiUser::FileListenThread, this);
}

void SlippiUser::LogOut()
{
	runThread = false;
	deleteFile();

	UserInfo emptyUser;
	isLoggedIn = false;
	std::lock_guard<std::mutex> lk(userInfoMutex);
	userInfo = emptyUser;
}

void SlippiUser::OverwriteLatestVersion(std::string version)
{
	std::lock_guard<std::mutex> lk(userInfoMutex);
	userInfo.latestVersion = version;
}

SlippiUser::UserInfo SlippiUser::GetUserInfo()
{
	std::lock_guard<std::mutex> lk(userInfoMutex);
	return userInfo;
}

std::vector<std::string> SlippiUser::GetDefaultChatMessages()
{
	RustChatMessages *chatMessages = slprs_user_get_default_messages(slprs_exi_device_ptr);
	return ConvertChatMessagesFromRust(chatMessages);
}

std::vector<std::string> SlippiUser::GetUserChatMessages()
{
	auto messages = GetUserInfo().chatMessages;
	if (messages.size() == 16)
		return messages;

	return GetDefaultChatMessages();
}

bool SlippiUser::IsLoggedIn()
{
	return isLoggedIn;
}

bool SlippiUser::HasSlippiInfo()
{
	auto info = GetUserInfo();
	return !(info.slippi_playKey.empty() || info.slippi_uid.empty());
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

static std::string readString(const json &obj, const std::string &key)
{
	auto item = obj.find(key);
	if (item == obj.end() || !item.value().is_string())
	{
		return "";
	}

	return item.value().get<std::string>();
}

static std::vector<std::string> readChatMessages(const json &obj)
{
	std::vector<std::string> messages;
	auto item = obj.find("chatMessages");
	if (item == obj.end() || !item.value().is_array())
		return messages;

	for (auto &m : item.value())
	{
		if (m.is_string())
			messages.push_back(m.get<std::string>());
	}

	return messages;
}

SlippiUser::UserInfo SlippiUser::parseFile(std::string fileContents)
{
	UserInfo info;
	info.fileContents = fileContents;

	auto res = json::parse(fileContents, nullptr, false);
	if (res.is_discarded() || !res.is_object())
	{
		return info;
	}

	info.uid = readString(res, "uid");
	info.displayName = readString(res, "displayName");
	info.playKey = readString(res, "playKey");
	info.connectCode = readString(res, "connectCode");
	info.lylatVersion = readString(res, "latestVersion");
	info.chatMessages = readChatMessages(res);
	auto slp = res.find("slippi");
	if (slp != res.end() && slp.value().is_object())
	{
		info.slippi_uid = readString(slp.value(), "uid");
		info.slippi_playKey = readString(slp.value(), "playKey");
		info.slippi_connectCode = readString(slp.value(), "connectCode");
		info.latestVersion = readString(slp.value(), "latestVersion");
	}

	return info;
}

void SlippiUser::deleteFile()
{
	std::string userFilePath = File::GetSlippiUserJSONPath();
	File::Delete(userFilePath);
}

void SlippiUser::overwriteFromServer()
{
	if (!m_curl)
		return;

	// Generate URL. If this is a beta version, use the beta endpoint
	std::string url = GetLylatOrigin() + "/users/slp";
	if (scm_slippi_semver_str.find("beta") != std::string::npos)
	{
		url = url + "-beta";
	}

	auto uid = GetUserInfo().uid;

	// Perform curl request
	std::string resp;
	curl_easy_setopt(m_curl, CURLOPT_URL, (url + "/" + uid).c_str());
	curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &resp);
	CURLcode res = curl_easy_perform(m_curl);

	if (res != 0)
	{
		ERROR_LOG(SLIPPI, "[User] Error fetching user info from server, code: %d", res);
		return;
	}

	long responseCode;
	curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &responseCode);
	if (responseCode != 200)
	{
		ERROR_LOG(SLIPPI, "[User] Server responded with non-success status: %ld", responseCode);
		return;
	}

	auto r = json::parse(resp, nullptr, false);
	if (r.is_discarded() || !r.is_object())
	{
		ERROR_LOG(SLIPPI, "[User] Server response is not a JSON object");
		return;
	}

	// Overwrite userInfo with data from server
	std::lock_guard<std::mutex> lk(userInfoMutex);
	std::string displayName = readString(r, "displayName");
	if (!displayName.empty())
		userInfo.displayName = displayName;
	std::string lylatVersion = readString(r, "lylatVersion");
	if (!lylatVersion.empty())
		userInfo.lylatVersion = lylatVersion;
	auto chatMessages = readChatMessages(r);
	if (chatMessages.size() == 16)
		userInfo.chatMessages = chatMessages;

	// The connect code shown in game is the linked Slippi one. Accounts with no Slippi
	// link get a placeholder, as before the merge.
	auto slp = r.find("slippi");
	if (slp != r.end() && slp.value().is_object())
	{
		std::string code = readString(slp.value(), "connectCode");
		std::string version = readString(slp.value(), "latestVersion");
		userInfo.connectCode = code.empty() ? "TMP#1234" : code;
		userInfo.latestVersion = version.empty() ? userInfo.lylatVersion : version;
	}
	else
	{
		userInfo.connectCode = "TMP#1234";
		userInfo.latestVersion = userInfo.lylatVersion;
	}
}
