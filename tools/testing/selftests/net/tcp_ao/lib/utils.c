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

__printf(3, 4) int test_echo(const char *fname, bool append,
			     const char *fmt, ...)
{
	size_t len, written;
	va_list vargs;
	char *msg;
	FILE *f;

	f = fopen(fname, append ? "a" : "w");
	if (!f)
		return -errno;

	va_start(vargs, fmt);
	msg = test_snprintf(fmt, vargs);
	va_end(vargs);
	if (!msg) {
		fclose(f);
		return -1;
	}
	len = strlen(msg);
	written = fwrite(msg, 1, len, f);
	fclose(f);
	free(msg);
	return written == len ? 0 : -1;
}

const struct sockaddr_in6 addr_any6 = {
	.sin6_family	= AF_INET6,
};

const struct sockaddr_in addr_any4 = {
	.sin_family	= AF_INET,
};
