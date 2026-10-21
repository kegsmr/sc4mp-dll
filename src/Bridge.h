#pragma once
#include "Json.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

// Talks to the SC4MP launcher over a local socket, using one JSON object per line.
//
// The game thread only adds messages to a queue. A background thread connects, sends and
// receives, so the game never waits on the network, and reconnects if the launcher goes away.
class Bridge
{
public:
	~Bridge();

	// Starts the background thread if the launcher passed a port in SC4MP_BRIDGE_PORT.
	// Returns false if it didn't (eg. single player), and then everything else does nothing.
	bool Start();

	// Stops the background thread. Call it before the DLL unloads.
	void Stop();

	bool IsEnabled() const;

	// Called on the bridge thread with each line the launcher sends to show in game (chat, save results and alerts).
	// `history` is true for the recent lines the launcher sends when the game connects. Set it before Start().
	using NotifyHandler = std::function<void(const std::string& text, bool history)>;
	void SetNotifyHandler(NotifyHandler handler);

	// Called on the bridge thread with the launcher's save status (eg. "saved", "Saved 13:01") and the server's name. Set them before Start().
	using StatusHandler = std::function<void(const std::string& state, const std::string& text)>;
	using ServerHandler = std::function<void(const std::string& name)>;
	void SetStatusHandler(StatusHandler handler);
	void SetServerHandler(ServerHandler handler);

	// Queues a message for the launcher, adding the current time. Messages wait in the queue
	// while the launcher isn't connected.
	void Send(JsonWriter message);

private:
	void Run();
	void Talk(uintptr_t s);
	bool SendLine(uintptr_t s, const std::string& line);
	void HandleLine(const std::string& line);

	// Waits up to `milliseconds`, returning early if the bridge is stopping
	void Wait(int milliseconds);

	unsigned short port = 0;
	std::thread thread;
	std::atomic<bool> stopping = false;
	NotifyHandler notifyHandler;
	StatusHandler statusHandler;
	ServerHandler serverHandler;

	std::mutex mutex;
	std::condition_variable stopped;
	std::deque<std::string> queue;
};
