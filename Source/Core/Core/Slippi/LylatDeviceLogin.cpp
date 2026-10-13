// Lylat QR sign-in: the device-code client. See LylatDeviceLogin.h.

#include "Core/Slippi/LylatDeviceLogin.h"

#include <cstdio>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include "Common/Common.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Core/Slippi/SlippiUser.h"

using json = nlohmann::json;

namespace
{
const char* Platform()
{
#if defined(_WIN32)
	return "windows";
#elif defined(__APPLE__)
	return "macos";
#else
	return "linux";
#endif
}

const char* DeviceName()
{
#if defined(_WIN32)
	return "Lylat Dolphin on Windows";
#elif defined(__APPLE__)
	return "Lylat Dolphin on macOS";
#else
	return "Lylat Dolphin on Linux";
#endif
}

size_t Receive(char* ptr, size_t size, size_t nmemb, void* out)
{
	size_t len = size * nmemb;
	std::string* body = static_cast<std::string*>(out);
	if (body->size() + len > 64 * 1024)
		return 0; // an account file is small; refuse anything huge
	body->append(ptr, len);
	return len;
}

int AbortCheck(void* p, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	return static_cast<std::atomic<bool>*>(p)->load() ? 1 : 0;
}

std::string StringField(const json& obj, const char* key)
{
	auto it = obj.find(key);
	if (it == obj.end())
		return "";
	if (it->is_string())
		return it->get<std::string>();
	if (it->is_number_integer())
		return std::to_string(it->get<long long>());
	return "";
}

int IntField(const json& obj, const char* key, int fallback)
{
	auto it = obj.find(key);
	if (it == obj.end())
		return fallback;
	if (it->is_number_integer())
		return it->get<int>();
	if (it->is_string())
	{
		const std::string s = it->get<std::string>();
		if (!s.empty() && s.size() < 9 && s.find_first_not_of("0123456789") == std::string::npos)
			return std::stoi(s);
	}
	return fallback;
}

std::string ErrorOf(const std::string& body)
{
	auto root = json::parse(body, nullptr, false);
	if (root.is_discarded() || !root.is_object())
		return "";
	return StringField(root, "error");
}
} // namespace

LylatDeviceLogin::LylatDeviceLogin() = default;

LylatDeviceLogin::~LylatDeviceLogin()
{
	Cancel();
}

void LylatDeviceLogin::Begin()
{
	Cancel();
	m_cancel = false;
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		unsigned serial = m_view.serial;
		m_view = View();
		m_view.serial = serial;
		m_view.state = State::Loading;
	}
	m_thread = std::thread([this] { Run(); });
}

void LylatDeviceLogin::Cancel()
{
	{
		// Set under the lock so a Wait() that is about to sleep cannot miss the wakeup.
		std::lock_guard<std::mutex> lk(m_mutex);
		m_cancel = true;
	}
	m_cv.notify_all();
	if (m_thread.joinable())
		m_thread.join();
}

LylatDeviceLogin::View LylatDeviceLogin::GetView()
{
	std::lock_guard<std::mutex> lk(m_mutex);
	View v = m_view;
	if (v.state == State::Showing || v.state == State::Approved)
	{
		auto left = std::chrono::duration_cast<std::chrono::seconds>(m_deadline - std::chrono::steady_clock::now())
		                .count();
		v.secondsLeft = left > 0 ? (int)left : 0;
	}
	return v;
}

void LylatDeviceLogin::MarkLoggedIn()
{
	std::lock_guard<std::mutex> lk(m_mutex);
	if (m_view.state == State::Approved)
		m_view.state = State::LoggedIn;
}

void LylatDeviceLogin::Set(State state, const std::string& error)
{
	if (state == State::Error || state == State::Expired || state == State::Denied || state == State::Unsupported)
		WARN_LOG(SLIPPI_ONLINE, "[QR sign-in] state %d%s%s", (int)state, error.empty() ? "" : ": ", error.c_str());
	std::lock_guard<std::mutex> lk(m_mutex);
	m_view.state = state;
	m_view.error = error;
}

bool LylatDeviceLogin::Wait(double seconds)
{
	std::unique_lock<std::mutex> lk(m_mutex);
	return !m_cv.wait_for(lk, std::chrono::milliseconds((long long)(seconds * 1000)),
	                      [this] { return m_cancel.load(); });
}

LylatDeviceLogin::Reply LylatDeviceLogin::Post(const std::string& url, const std::string& body)
{
	Reply r;
	CURL* c = curl_easy_init();
	if (!c)
		return r;
	curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/json");
	headers = curl_slist_append(headers, "Accept: application/json");
	curl_easy_setopt(c, CURLOPT_URL, url.c_str());
	curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(c, CURLOPT_POST, 1L);
	curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
	curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)body.size());
	curl_easy_setopt(c, CURLOPT_USERAGENT, "LylatDolphin/DeviceLogin");
	curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 4000L);
	curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 8000L);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, &Receive);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &r.body);
	curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, &AbortCheck);
	curl_easy_setopt(c, CURLOPT_XFERINFODATA, &m_cancel);
#ifdef _WIN32
	// ALPN support is enabled by default but requires Windows >= 8.1.
	curl_easy_setopt(c, CURLOPT_SSL_ENABLE_ALPN, false);
#endif
	// No redirects and no verbose output: the body holds the secret.
	r.rc = (int)curl_easy_perform(c);
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.http);
	curl_easy_cleanup(c);
	curl_slist_free_all(headers);
	return r;
}

// Writes the account file where the browser login writes it: a temp file next to it,
// owner read/write only, then a rename over the old one.
bool LylatDeviceLogin::SaveAccount(const std::string& body)
{
	const std::string path = File::GetSlippiUserJSONPath();
	const std::string tmp = path + ".qr-login.tmp";
	File::CreateFullPath(path);
	if (!File::WriteStringToFile(body, tmp))
		return false;
#ifndef _WIN32
	chmod(tmp.c_str(), S_IRUSR | S_IWUSR);
#endif
	if (!File::Rename(tmp, path))
	{
		File::Delete(tmp);
		return false;
	}
	return true;
}

void LylatDeviceLogin::Run()
{
	const std::string origin = SlippiUser::GetLylatOrigin();
	std::string deviceCode; // secret
	int interval = 5;
	{
		const std::string req = std::string("{\"client\":\"lylat-dolphin\",\"platform\":\"") + Platform() +
		                        "\",\"name\":\"" + DeviceName() + "\"}";
		Reply r = Post(origin + "/api/device/code", req);
		if (m_cancel)
			return;
		if (r.rc == CURLE_OK && r.http == 404)
		{
			Set(State::Unsupported);
			return;
		}
		if (r.rc != CURLE_OK)
		{
			Set(State::Error, "Could not reach the Lylat server.");
			return;
		}
		if (r.http == 429)
		{
			Set(State::Error, "Too many codes. Try again in a few minutes.");
			return;
		}
		auto root = json::parse(r.body, nullptr, false);
		if (r.http != 200 || root.is_discarded() || !root.is_object())
		{
			Set(State::Error, "Server error (" + std::to_string(r.http) + ").");
			return;
		}
		deviceCode = StringField(root, "device_code");
		const std::string userCode = StringField(root, "user_code");
		const std::string uri = StringField(root, "verification_uri");
		const std::string complete = StringField(root, "verification_uri_complete");
		// The contract sends match_number as a string ("47"); accept a JSON number too.
		int number = IntField(root, "match_number", -1);
		int expires = IntField(root, "expires_in", 600);
		interval = IntField(root, "interval", 5);
		if (interval < 1)
			interval = 1;
		if (interval > 30)
			interval = 30;
		if (expires < 10)
			expires = 10;
		if (expires > 3600)
			expires = 3600;
		if (deviceCode.empty() || userCode.empty() || userCode.size() > 32 || complete.empty() ||
		    complete.size() > 512 || uri.size() > 512 || number < 0 || number > 99)
		{
			Set(State::Error, "The server sent an unusable code.");
			return;
		}
		char numberText[8];
		snprintf(numberText, sizeof(numberText), "%02d", number);

		std::lock_guard<std::mutex> lk(m_mutex);
		m_view.userCode = userCode;
		m_view.matchNumber = numberText;
		m_view.verificationUri = uri;
		m_view.verificationUriComplete = complete;
		m_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(expires);
		++m_view.serial;
		m_view.state = State::Showing;
		m_view.error.clear();
	}

	int failures = 0;
	const std::string tokenJson = "{\"device_code\":\"" + deviceCode + "\"}";
	for (;;)
	{
		if (!Wait(interval))
			return;
		if (std::chrono::steady_clock::now() >= m_deadline)
		{
			Set(State::Expired);
			return;
		}
		Reply r = Post(origin + "/api/device/token", tokenJson);
		if (m_cancel)
			return;
		if (r.rc != CURLE_OK || r.http >= 500)
		{
			if (++failures >= 6)
			{
				Set(State::Error, "Lost contact with the Lylat server.");
				return;
			}
			continue;
		}
		failures = 0;
		if (r.http == 200)
		{
			auto account = json::parse(r.body, nullptr, false);
			if (account.is_discarded() || !account.is_object() || StringField(account, "uid").empty() ||
			    StringField(account, "playKey").empty())
			{
				Set(State::Error, "The server sent an invalid account.");
				return;
			}
			if (!SaveAccount(r.body))
			{
				Set(State::Error, "Could not save the account file.");
				return;
			}
			Set(State::Approved);
			return;
		}
		const std::string error = ErrorOf(r.body);
		if (error == "authorization_pending")
			continue;
		if (error == "slow_down")
		{
			interval += 5;
			if (interval > 60)
				interval = 60;
			continue;
		}
		if (error == "expired_token" || error == "invalid_grant")
		{
			Set(State::Expired);
			return;
		}
		if (error == "access_denied")
		{
			Set(State::Denied);
			return;
		}
		if (r.http == 404)
		{
			Set(State::Unsupported);
			return;
		}
		if (++failures >= 6)
		{
			Set(State::Error, "The server refused the request.");
			return;
		}
	}
}
