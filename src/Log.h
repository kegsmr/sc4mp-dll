#pragma once
#include <string>

namespace Log
{
	// Writes a line to SC4MP.log next to the DLL. Safe to call from any thread.
	void Line(const std::string& line);
}
