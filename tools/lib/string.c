#include <stdlib.h>
#include <string.h>
#include <linux/string.h>

/**
 * memdup - duplicate region of memory
 *
 * @src: memory region to duplicate
 * @len: memory region length
 */
void *memdup(const void *src, size_t len)
{
	void *p = malloc(len);

	if (p)
		memcpy(p, src, len);

	return p;
}
