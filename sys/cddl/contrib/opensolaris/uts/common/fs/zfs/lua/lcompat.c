/*
 * Copyright (c) 2016 by Delphix. All rights reserved.
 */

#include "lua.h"

#include <sys/zfs_context.h>

ssize_t
lcompat_sprintf(char *buf, const char *fmt, ...)
{
	ssize_t res;
	va_list args;

	va_start(args, fmt);
	res = vsnprintf(buf, INT_MAX, fmt, args);
	va_end(args);

	return (res);
}

int64_t
lcompat_strtoll(const char *str, char **ptr)
{
	int base;
	const char *cp;
	int digits;
	int64_t value;
	boolean_t is_negative;

	cp = str;
	while (*cp == ' ' || *cp == '\t' || *cp == '\n') {
		cp++;
	}
	is_negative = (*cp == '-');
	if (is_negative) {
		cp++;
	}
	base = 10;

	if (*cp == '0') {
		base = 8;
		cp++;
		if (*cp == 'x' || *cp == 'X') {
			base = 16;
			cp++;
		}
	}

	value = 0;
	for (; *cp != '\0'; cp++) {
		if (*cp >= '0' && *cp <= '9') {
			digits = *cp - '0';
		} else if (*cp >= 'a' && *cp <= 'f') {
			digits = *cp - 'a' + 10;
		} else if (*cp >= 'A' && *cp <= 'F') {
			digits = *cp - 'A' + 10;
		} else {
			break;
		}
		if (digits >= base) {
			break;
		}
		value = (value * base) + digits;
	}

	if (ptr != NULL) {
		*ptr = (char *)cp;
	}
	if (is_negative) {
		value = -value;
	}
	return (value);
}

int64_t
lcompat_pow(int64_t x, int64_t y)
{
	int64_t result = 1;
	if (y < 0)
		return (0);

	while (y) {
		if (y & 1)
			result *= x;
		y >>= 1;
		x *= x;
	}
	return (result);
}

int
lcompat_hashnum(int64_t x)
{
	x = (~x) + (x << 18);
	x = x ^ (x >> 31);
	x = x * 21;
	x = x ^ (x >> 11);
	x = x + (x << 6);
	x = x ^ (x >> 22);
	return ((int)x);
}
