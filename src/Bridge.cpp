// Winsock has to be included before Windows.h
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>

#include "Bridge.h"
#include "Log.h"
#include "Text.h"
#include "version.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <map>

#pragma comment(lib, "Ws2_32.lib")

static constexpr int kReconnectDelay = 2000;	// milliseconds
static constexpr int kPollDelay = 100;			// milliseconds between checking for messages to send
static constexpr size_t kMaxQueuedMessages = 256;	// the oldest are dropped if the launcher stops reading
static constexpr size_t kMaxLineLength = 64 * 1024;	// longer lines from the launcher are garbage, so the connection is reset

Bridge::~Bridge()
{
	// Joining a thread while the DLL unloads can deadlock, so if Stop() wasn't called, let the thread die with the game
	if (thread.joinable())
	{
		thread.detach();
	}
}

bool Bridge::Start()
{
	char* value = nullptr;
	size_t length = 0;
	if (_dupenv_s(&value, &length, "SC4MP_BRIDGE_PORT") != 0 || !value)
	{
		return false;
	}

	int number = std::atoi(value);
	std::free(value);

	if (number <= 0 || number > 65535)
	{
		Log::Line("SC4MP_BRIDGE_PORT isn't a valid port, so the bridge is off");
		return false;
	}

	port = static_cast<unsigned short>(number);
	thread = std::thread(&Bridge::Run, this);

	return true;
}

void Bridge::Stop()
{
	if (!thread.joinable())
	{
		return;
	}

	{
		// Set under the lock, so Wait() can't miss it
		std::lock_guard<std::mutex> lock(mutex);
		stopping = true;
	}
	stopped.notify_all();
	thread.join();
}

bool Bridge::IsEnabled() const
{
	return port != 0;
}

void Bridge::Send(JsonWriter message)
{
	if (!IsEnabled())
	{
		return;
	}

	message.Add("time", static_cast<int64_t>(std::time(nullptr)));

	std::lock_guard<std::mutex> lock(mutex);
	queue.push_back(message.ToString());
	while (queue.size() > kMaxQueuedMessages)
	{
		queue.pop_front();
	}
}

void Bridge::Run()
{
	WSADATA wsaData{};
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		Log::Line("Unable to start Winsock, so the bridge is off");
		return;
	}

	Log::Line("Connecting to the launcher on port " + std::to_string(port));

	bool failedBefore = false;

	while (!stopping)
	{
		SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (s == INVALID_SOCKET)
		{
			Wait(kReconnectDelay);
			continue;
		}

		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_port = htons(port);
		inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

		// Connecting to localhost fails right away if nothing is listening, so this doesn't hang
		if (connect(s, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0)
		{
			Log::Line("Connected to the launcher");
			failedBefore = false;

			// Send events right away instead of batching them
			BOOL noDelay = TRUE;
			setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));

			Talk(s);

			Log::Line("Disconnected from the launcher");
		}
		else if (!failedBefore)
		{
			// Only once, so the log isn't flooded while the launcher is gone
			Log::Line("Unable to connect to the launcher, retrying every few seconds");
			failedBefore = true;
		}

		closesocket(s);
		Wait(kReconnectDelay);
	}

	WSACleanup();
}

void Bridge::Talk(uintptr_t s)
{
	JsonWriter hello;
	hello.Add("type", "hello");
	hello.Add("version", PLUGIN_VERSION_STR);
	hello.Add("time", static_cast<int64_t>(std::time(nullptr)));
	if (!SendLine(s, hello.ToString()))
	{
		return;
	}

	std::string incoming;

	while (!stopping)
	{
		// Send what the game queued
		std::deque<std::string> outgoing;
		{
			std::lock_guard<std::mutex> lock(mutex);
			outgoing.swap(queue);
		}
		while (!outgoing.empty())
		{
			if (!SendLine(s, outgoing.front()))
			{
				// Put the unsent messages back, so they're sent after reconnecting
				std::lock_guard<std::mutex> lock(mutex);
				queue.insert(queue.begin(), outgoing.begin(), outgoing.end());
				return;
			}
			outgoing.pop_front();
		}

		// Wait a little for messages from the launcher
		fd_set readable;
		FD_ZERO(&readable);
		FD_SET(s, &readable);
		timeval timeout{ 0, kPollDelay * 1000 };

		int ready = select(0, &readable, nullptr, nullptr, &timeout);
		if (ready == SOCKET_ERROR)
		{
			return;
		}
		if (ready == 0)
		{
			continue;
		}

		char buffer[4096];
		int received = recv(s, buffer, sizeof(buffer), 0);
		if (received <= 0)
		{
			return;	// the launcher closed the connection
		}
		incoming.append(buffer, received);

		size_t end;
		while ((end = incoming.find('\n')) != std::string::npos)
		{
			std::string line = incoming.substr(0, end);
			incoming.erase(0, end + 1);
			HandleLine(line);
		}

		if (incoming.size() > kMaxLineLength)
		{
			Log::Line("Received a line that's too long from the launcher, reconnecting");
			return;
		}
	}
}

bool Bridge::SendLine(uintptr_t s, const std::string& line)
{
	std::string data = line + "\n";

	size_t sent = 0;
	while (sent < data.size())
	{
		int result = send(s, data.data() + sent, static_cast<int>(data.size() - sent), 0);
		if (result == SOCKET_ERROR)
		{
			return false;
		}
		sent += result;
	}

	return true;
}

void Bridge::HandleLine(const std::string& line)
{
	if (line.empty() || line == "\r")
	{
		return;
	}

	std::map<std::string, std::string> message;
	if (!Json::ParseObject(line, message))
	{
		Log::Line("Ignored a message from the launcher that isn't a JSON object: " + line.substr(0, 200));
		return;
	}

	const std::string& type = message["type"];
	const std::string& text = message["text"];

	if (type == "notify")
	{
		// Chat and save results. The launcher's overlay already shows these, so they're only logged for now
		Log::Line("Launcher: " + text);
	}
	else if (type == "alert")
	{
		Log::Line("Launcher (alert): " + text);

		// Shown on its own thread, so neither the game nor the bridge waits for the player to close it
		std::wstring wideText = Text::Utf8ToWide(text);
		std::thread([wideText]()
		{
			MessageBoxW(nullptr, wideText.c_str(), L"SC4MP", MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
		}).detach();
	}
	else
	{
		// Newer launchers may send more types, which this version just ignores
		Log::Line("Ignored a message from the launcher with type \"" + type + "\"");
	}
}

void Bridge::Wait(int milliseconds)
{
	std::unique_lock<std::mutex> lock(mutex);
	stopped.wait_for(lock, std::chrono::milliseconds(milliseconds), [this]() { return stopping.load(); });
}
