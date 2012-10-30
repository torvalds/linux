/*
 * Copyright 2011 The Chromium Authors, All Rights Reserved.
 * Copyright 2008 Jon Loeliger, Freescale Semiconductor, Inc.
 *
 * util_is_printable_string contributed by
 *	Pantelis Antoniou <pantelis.antoniou AT gmail.com>
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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "libfdt.h"
#include "util.h"

char *xstrdup(const char *s)
{
	int len = strlen(s) + 1;
	char *dup = xmalloc(len);

	memcpy(dup, s, len);

	return dup;
}

char *join_path(const char *path, const char *name)
{
	int lenp = strlen(path);
	int lenn = strlen(name);
	int len;
	int needslash = 1;
	char *str;

	len = lenp + lenn + 2;
	if ((lenp > 0) && (path[lenp-1] == '/')) {
		needslash = 0;
		len--;
	}

	str = xmalloc(len);
	memcpy(str, path, lenp);
	if (needslash) {
		str[lenp] = '/';
		lenp++;
	}
	memcpy(str+lenp, name, lenn+1);
	return str;
}

int util_is_printable_string(const void *data, int len)
{
	const char *s = data;
	const char *ss;

	/* zero length is not */
	if (len == 0)
		return 0;

	/* must terminate with zero */
	if (s[len - 1] != '\0')
		return 0;

	ss = s;
	while (*s && isprint(*s))
		s++;

	/* not zero, or not done yet */
	if (*s != '\0' || (s + 1 - ss) < len)
		return 0;

	return 1;
}

/*
 * Parse a octal encoded character starting at index i in string s.  The
 * resulting character will be returned and the index i will be updated to
 * point at the character directly after the end of the encoding, this may be
 * the '\0' terminator of the string.
 */
static char get_oct_char(const char *s, int *i)
{
	char x[4];
	char *endx;
	long val;

	x[3] = '\0';
	strncpy(x, s + *i, 3);

	val = strtol(x, &endx, 8);

	assert(endx > x);

	(*i) += endx - x;
	return val;
}

/*
 * Parse a hexadecimal encoded character starting at index i in string s.  The
 * resulting character will be returned and the index i will be updated to
 * point at the character directly after the end of the encoding, this may be
 * the '\0' terminator of the string.
 */
static char get_hex_char(const char *s, int *i)
{
	char x[3];
	char *endx;
	long val;

	x[2] = '\0';
	strncpy(x, s + *i, 2);

	val = strtol(x, &endx, 16);
	if (!(endx  > x))
		die("\\x used with no following hex digits\n");

	(*i) += endx - x;
	return val;
}

char get_escape_char(const char *s, int *i)
{
	char	c = s[*i];
	int	j = *i + 1;
	char	val;

	assert(c);
	switch (c) {
	case 'a':
		val = '\a';
		break;
	case 'b':
		val = '\b';
		break;
	case 't':
		val = '\t';
		break;
	case 'n':
		val = '\n';
		break;
	case 'v':
		val = '\v';
		break;
	case 'f':
		val = '\f';
		break;
	case 'r':
		val = '\r';
		break;
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
		j--; /* need to re-read the first digit as
		      * part of the octal value */
		val = get_oct_char(s, &j);
		break;
	case 'x':
		val = get_hex_char(s, &j);
		break;
	default:
		val = c;
	}

	(*i) = j;
	return val;
}

int utilfdt_read_err(const char *filename, char **buffp)
{
	int fd = 0;	/* assume stdin */
	char *buf = NULL;
	off_t bufsize = 1024, offset = 0;
	int ret = 0;

	*buffp = NULL;
	if (strcmp(filename, "-") != 0) {
		fd = open(filename, O_RDONLY);
		if (fd < 0)
			return errno;
	}

	/* Loop until we have read everything */
	buf = malloc(bufsize);
	do {
		/* Expand the buffer to hold the next chunk */
		if (offset == bufsize) {
			bufsize *= 2;
			buf = realloc(buf, bufsize);
			if (!buf) {
				ret = ENOMEM;
				break;
			}
		}

		ret = read(fd, &buf[offset], bufsize - offset);
		if (ret < 0) {
			ret = errno;
			break;
		}
		offset += ret;
	} while (ret != 0);

	/* Clean up, including closing stdin; return errno on error */
	close(fd);
	if (ret)
		free(buf);
	else
		*buffp = buf;
	return ret;
}

char *utilfdt_read(const char *filename)
{
	char *buff;
	int ret = utilfdt_read_err(filename, &buff);

	if (ret) {
		fprintf(stderr, "Couldn't open blob from '%s': %s\n", filename,
			strerror(ret));
		return NULL;
	}
	/* Successful read */
	return buff;
}

int utilfdt_write_err(const char *filename, const void *blob)
{
	int fd = 1;	/* assume stdout */
	int totalsize;
	int offset;
	int ret = 0;
	const char *ptr = blob;

	if (strcmp(filename, "-") != 0) {
		fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (fd < 0)
			return errno;
	}

	totalsize = fdt_totalsize(blob);
	offset = 0;

	while (offset < totalsize) {
		ret = write(fd, ptr + offset, totalsize - offset);
		if (ret < 0) {
			ret = -errno;
			break;
		}
		offset += ret;
	}
	/* Close the file/stdin; return errno on error */
	if (fd != 1)
		close(fd);
	return ret < 0 ? -ret : 0;
}


int utilfdt_write(const char *filename, const void *blob)
{
	int ret = utilfdt_write_err(filename, blob);

	if (ret) {
		fprintf(stderr, "Couldn't write blob to '%s': %s\n", filename,
			strerror(ret));
	}
	return ret ? -1 : 0;
}

int utilfdt_decode_type(const char *fmt, int *type, int *size)
{
	int qualifier = 0;

	if (!*fmt)
		return -1;

	/* get the conversion qualifier */
	*size = -1;
	if (strchr("hlLb", *fmt)) {
		qualifier = *fmt++;
		if (qualifier == *fmt) {
			switch (*fmt++) {
/* TODO:		case 'l': qualifier = 'L'; break;*/
			case 'h':
				qualifier = 'b';
				break;
			}
		}
	}

	/* we should now have a type */
	if ((*fmt == '\0') || !strchr("iuxs", *fmt))
		return -1;

	/* convert qualifier (bhL) to byte size */
	if (*fmt != 's')
		*size = qualifier == 'b' ? 1 :
				qualifier == 'h' ? 2 :
				qualifier == 'l' ? 4 : -1;
	*type = *fmt++;

	/* that should be it! */
	if (*fmt)
		return -1;
	return 0;
}
