// Throwaway harness: runs the DLL's Bridge outside the game against a fake launcher
#include "Bridge.h"
#include "Log.h"
#include <chrono>
#include <cstdio>
#include <thread>

int main(int argc, char** argv)
{
	Bridge bridge;
	if (!bridge.Start())
	{
		std::printf("bridge off (SC4MP_BRIDGE_PORT not set)\n");
		return 0;
	}

	// Queued like the game thread would, possibly before the bridge connects
	JsonWriter loaded;
	loaded.Add("type", "city_loaded").Add("name", std::string("Caf\xc3\xa9 \"Town\"")).Add("established", true);
	bridge.Send(loaded);

	JsonWriter preSave;
	preSave.Add("type", "pre_save").Add("name", "Cafe Town");
	bridge.Send(preSave);

	JsonWriter saved;
	saved.Add("type", "saved").Add("name", "Cafe Town");
	bridge.Send(saved);

	JsonWriter chat;
	chat.Add("type", "chat").Add("message", "hello everyone");
	bridge.Send(chat);

	// The fake launcher drops the connection after the first batch; these must arrive after reconnecting
	std::this_thread::sleep_for(std::chrono::milliseconds(1500));
	JsonWriter exited;
	exited.Add("type", "city_exited").Add("name", "Cafe Town");
	bridge.Send(exited);

	std::this_thread::sleep_for(std::chrono::milliseconds(4000));

	auto start = std::chrono::steady_clock::now();
	bridge.Stop();
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
	std::printf("Stop() took %lld ms\n", static_cast<long long>(ms));
	return 0;
}
