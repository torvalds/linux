#ifndef _UTIL_H
#define _UTIL_H

#include <stdarg.h>

/*
 * Copyright 2011 The Chromium Authors, All Rights Reserved.
 * Copyright 2008 Jon Loeliger, Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *                                                                   USA
 */

static inline void __attribute__((noreturn)) die(char * str, ...)
{
	va_list ap;

	va_start(ap, str);
	fprintf(stderr, "FATAL ERROR: ");
	vfprintf(stderr, str, ap);
	exit(1);
}

static inline void *xmalloc(size_t len)
{
	void *new = malloc(len);

	if (!new)
		die("malloc() failed\n");

	return new;
}

static inline void *xrealloc(void *p, size_t len)
{
	void *new = realloc(p, len);

	if (!new)
		die("realloc() failed (len=%d)\n", len);

	return new;
}

extern char *xstrdup(const char *s);
extern char *join_path(const char *path, const char *name);

/**
 * Check a string of a given length to see if it is all printable and
 * has a valid terminator.
 *
 * @param data	The string to check
 * @param len	The string length including terminator
 * @return 1 if a valid printable string, 0 if not */
int util_is_printable_string(const void *data, int len);

/*
 * Parse an escaped character starting at index i in string s.  The resulting
 * character will be returned and the index i will be updated to point at the
 * character directly after the end of the encoding, this may be the '\0'
 * terminator of the string.
 */
char get_escape_char(const char *s, int *i);

/**
 * Read a device tree file into a buffer. This will report any errors on
 * stderr.
 *
 * @param filename	The filename to read, or - for stdin
 * @return Pointer to allocated buffer containing fdt, or NULL on error
 */
char *utilfdt_read(const char *filename);

/**
 * Read a device tree file into a buffer. Does not report errors, but only
 * returns them. The value returned can be passed to strerror() to obtain
 * an error message for the user.
 *
 * @param filename	The filename to read, or - for stdin
 * @param buffp		Returns pointer to buffer containing fdt
 * @return 0 if ok, else an errno value representing the error
 */
int utilfdt_read_err(const char *filename, char **buffp);


/**
 * Write a device tree buffer to a file. This will report any errors on
 * stderr.
 *
 * @param filename	The filename to write, or - for stdout
 * @param blob		Poiner to buffer containing fdt
 * @return 0 if ok, -1 on error
 */
int utilfdt_write(const char *filename, const void *blob);

/**
 * Write a device tree buffer to a file. Does not report errors, but only
 * returns them. The value returned can be passed to strerror() to obtain
 * an error message for the user.
 *
 * @param filename	The filename to write, or - for stdout
 * @param blob		Poiner to buffer containing fdt
 * @return 0 if ok, else an errno value representing the error
 */
int utilfdt_write_err(const char *filename, const void *blob);

/**
 * Decode a data type string. The purpose of this string
 *
 * The string consists of an optional character followed by the type:
 *	Modifier characters:
 *		hh or b	1 byte
 *		h	2 byte
 *		l	4 byte, default
 *
 *	Type character:
 *		s	string
 *		i	signed integer
 *		u	unsigned integer
 *		x	hex
 *
 * TODO: Implement ll modifier (8 bytes)
 * TODO: Implement o type (octal)
 *
 * @param fmt		Format string to process
 * @param type		Returns type found(s/d/u/x), or 0 if none
 * @param size		Returns size found(1,2,4,8) or 4 if none
 * @return 0 if ok, -1 on error (no type given, or other invalid format)
 */
int utilfdt_decode_type(const char *fmt, int *type, int *size);

/*
 * This is a usage message fragment for the -t option. It is the format
 * supported by utilfdt_decode_type.
 */

#define USAGE_TYPE_MSG \
	"<type>\ts=string, i=int, u=unsigned, x=hex\n" \
	"\tOptional modifier prefix:\n" \
	"\t\thh or b=byte, h=2 byte, l=4 byte (default)\n";

#endif /* _UTIL_H */
