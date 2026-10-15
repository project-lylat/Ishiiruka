// Lylat QR sign-in: the device-code client (OAuth 2.0 Device Authorization Grant with
// number matching). See the Lylat server's docs/device-login.md.
//
// POST <origin>/api/device/code, then POST <origin>/api/device/token every `interval`
// seconds on this object's own thread. The 200 token body is the lylat.json account file;
// it is written where the browser login writes it (File::GetSlippiUserJSONPath), and the
// SlippiUser file watcher picks it up from there.
//
// The device_code is a secret: it lives only in the worker thread's locals, never in the
// view, a log or a status line.

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

class LylatDeviceLogin
{
  public:
	enum class State
	{
		Idle,
		Loading,     // asking for a code
		Showing,     // QR + code + number are up, polling
		Approved,    // the account file was saved, waiting for the game to load it
		LoggedIn,    // the game picked up the account
		Expired,     // the code ran out (or the server says it is unknown)
		Denied,      // the user denied it, or picked the wrong number
		Error,       // network or server error; `error` says which
		Unsupported, // 404 from /api/device/code: use the browser login
	};

	struct View
	{
		State state = State::Idle;
		std::string userCode;
		std::string matchNumber;
		std::string verificationUri;
		std::string verificationUriComplete;
		int secondsLeft = 0;
		std::string error;
		unsigned serial = 0; // bumps whenever a new code arrives
	};

	LylatDeviceLogin();
	~LylatDeviceLogin();

	// Starts a new attempt (cancelling any current one).
	void Begin();
	// Cancels the current attempt; aborts an in-flight request and joins the thread.
	void Cancel();
	View GetView();

	// Set by the caller when the game has loaded the saved account (Approved -> LoggedIn).
	void MarkLoggedIn();

  private:
	void Run();
	void Set(State state, const std::string& error = "");
	bool Wait(double seconds);
	struct Reply
	{
		int rc = -1;
		long http = 0;
		std::string body;
	};
	Reply Post(const std::string& url, const std::string& json);
	static bool SaveAccount(const std::string& body);

	std::mutex m_mutex;
	std::condition_variable m_cv;
	std::thread m_thread;
	std::atomic<bool> m_cancel{false};
	View m_view;
	std::chrono::steady_clock::time_point m_deadline{};
};
