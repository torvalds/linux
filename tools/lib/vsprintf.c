// SPDX-License-Identifier: GPL-2.0
#include <sys/types.h>
#include <linux/kernel.h>
#include <stdio.h>

int vscnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
       int i = vsnprintf(buf, size, fmt, args);
       ssize_t ssize = size;

       return (i >= ssize) ? (ssize - 1) : i;
}

int scnprintf(char * buf, size_t size, const char * fmt, ...)
{
       ssize_t ssize = size;
       va_list args;
       int i;

       va_start(args, fmt);
       i = vsnprintf(buf, size, fmt, args);
       va_end(args);

       return (i >= ssize) ? (ssize - 1) : i;
}

int scnprintf_pad(char * buf, size_t size, const char * fmt, ...)
{
	ssize_t ssize = size;
	va_list args;
	int i;

	va_start(args, fmt);
	i = vscnprintf(buf, size, fmt, args);
	va_end(args);

	if (i < (int) size) {
		for (; i < (int) size; i++)
			buf[i] = ' ';
		buf[i] = 0x0;
	}

	return (i >= ssize) ? (ssize - 1) : i;
}
