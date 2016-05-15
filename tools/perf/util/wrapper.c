/*
 * Various trivial helper wrappers around standard functions
 */
#include "cache.h"

/*
 * There's no pack memory to release - but stay close to the Git
 * version so wrap this away:
 */
static inline void release_pack_memory(size_t size __maybe_unused,
				       int flag __maybe_unused)
{
}

void *xrealloc(void *ptr, size_t size)
{
	void *ret = realloc(ptr, size);
	if (!ret && !size)
		ret = realloc(ptr, 1);
	if (!ret) {
		release_pack_memory(size, -1);
		ret = realloc(ptr, size);
		if (!ret && !size)
			ret = realloc(ptr, 1);
		if (!ret)
			die("Out of memory, realloc failed");
	}
	return ret;
}
