#include <cstddef>
#include <cstdint>

// EASTL leaves its allocation functions for the program to define.
// Alignment is ignored (as in the EASTL examples), since nothing here allocates over-aligned types.

void* operator new[](size_t size, const char* pName, int flags, unsigned debugFlags, const char* file, int line)
{
	return new uint8_t[size];
}

void* operator new[](size_t size, size_t alignment, size_t alignmentOffset, const char* pName, int flags, unsigned debugFlags, const char* file, int line)
{
	return new uint8_t[size];
}
