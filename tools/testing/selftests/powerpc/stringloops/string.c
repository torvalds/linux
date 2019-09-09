// SPDX-License-Identifier: GPL-2.0
/*
 * Copied from linux/lib/string.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <stddef.h>

/**
 * strlen - Find the length of a string
 * @s: The string to be sized
 */
size_t test_strlen(const char *s)
{
	const char *sc;

	for (sc = s; *sc != '\0'; ++sc)
		/* nothing */;
	return sc - s;
}
