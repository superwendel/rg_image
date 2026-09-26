#include <stddef.h>
#include <stdint.h>

// Called outside measured intervals and compiled separately, without LTO.
uint64_t bench_consume(const void* data, size_t size)
{
	const unsigned char* bytes = (const unsigned char*)data;
	uint64_t hash = UINT64_C(14695981039346656037);
	for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
	return hash;
}
