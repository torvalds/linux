// SPDX-License-Identifier: GPL-2.0
#include "aolib.h"
#include <string.h>

void randomize_buffer(void *buf, size_t buflen)
{
	int *p = (int *)buf;
	size_t words = buflen / sizeof(int);
	size_t leftover = buflen % sizeof(int);

	if (!buflen)
		return;

	while (words--)
		*p++ = rand();

	if (leftover) {
		int tmp = rand();

		memcpy(buf + buflen - leftover, &tmp, leftover);
	}
}

const struct sockaddr_in6 addr_any6 = {
	.sin6_family	= AF_INET6,
};

const struct sockaddr_in addr_any4 = {
	.sin_family	= AF_INET,
};
