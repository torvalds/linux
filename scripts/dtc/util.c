// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2011 The Chromium Authors, All Rights Reserved.
 * Copyright 2008 Jon Loeliger, Freescale Semiconductor, Inc.
 *
 * util_is_printable_string contributed by
 *	Pantelis Antoniou <pantelis.antoniou AT gmail.com>
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
#include "version_gen.h"

char *xstrdup(const char *s)
{
	int len = strlen(s) + 1;
	char *d = xmalloc(len);

	memcpy(d, s, len);

	return d;
}

int xavsprintf_append(char **strp, const char *fmt, va_list ap)
{
	int n, size = 0;	/* start with 128 bytes */
	char *p;
	va_list ap_copy;

	p = *strp;
	if (p)
		size = strlen(p);

	va_copy(ap_copy, ap);
	n = vsnprintf(NULL, 0, fmt, ap_copy) + 1;
	va_end(ap_copy);

	p = xrealloc(p, size + n);

	n = vsnprintf(p + size, n, fmt, ap);

	*strp = p;
	return strlen(p);
}

int xasprintf_append(char **strp, const char *fmt, ...)
{
	int n;
	va_list ap;

	va_start(ap, fmt);
	n = xavsprintf_append(strp, fmt, ap);
	va_end(ap);

	return n;
}

int xasprintf(char **strp, const char *fmt, ...)
{
	int n;
	va_list ap;

	*strp = NULL;

	va_start(ap, fmt);
	n = xavsprintf_append(strp, fmt, ap);
	va_end(ap);

	return n;
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

bool util_is_printable_string(const void *data, int len)
{
	const char *s = data;
	const char *ss, *se;

	/* zero length is not */
	if (len == 0)
		return 0;

	/* must terminate with zero */
	if (s[len - 1] != '\0')
		return 0;

	se = s + len;

	while (s < se) {
		ss = s;
		while (s < se && *s && isprint((unsigned char)*s))
			s++;

		/* not zero, or not done yet */
		if (*s != '\0' || s == ss)
			return 0;

		s++;
	}

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

int utilfdt_read_err(const char *filename, char **buffp, size_t *len)
{
	int fd = 0;	/* assume stdin */
	char *buf = NULL;
	size_t bufsize = 1024, offset = 0;
	int ret = 0;

	*buffp = NULL;
	if (strcmp(filename, "-") != 0) {
		fd = open(filename, O_RDONLY);
		if (fd < 0)
			return errno;
	}

	/* Loop until we have read everything */
	buf = xmalloc(bufsize);
	do {
		/* Expand the buffer to hold the next chunk */
		if (offset == bufsize) {
			bufsize *= 2;
			buf = xrealloc(buf, bufsize);
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
	if (len)
		*len = bufsize;
	return ret;
}

char *utilfdt_read(const char *filename, size_t *len)
{
	char *buff;
	int ret = utilfdt_read_err(filename, &buff, len);

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

void utilfdt_print_data(const char *data, int len)
{
	int i;
	const char *s;

	/* no data, don't print */
	if (len == 0)
		return;

	if (util_is_printable_string(data, len)) {
		printf(" = ");

		s = data;
		do {
			printf("\"%s\"", s);
			s += strlen(s) + 1;
			if (s < data + len)
				printf(", ");
		} while (s < data + len);

	} else if ((len % 4) == 0) {
		const fdt32_t *cell = (const fdt32_t *)data;

		printf(" = <");
		for (i = 0, len /= 4; i < len; i++)
			printf("0x%08x%s", fdt32_to_cpu(cell[i]),
			       i < (len - 1) ? " " : "");
		printf(">");
	} else {
		const unsigned char *p = (const unsigned char *)data;
		printf(" = [");
		for (i = 0; i < len; i++)
			printf("%02x%s", *p++, i < len - 1 ? " " : "");
		printf("]");
	}
}

void NORETURN util_version(void)
{
	printf("Version: %s\n", DTC_VERSION);
	exit(0);
}

void NORETURN util_usage(const char *errmsg, const char *synopsis,
			 const char *short_opts,
			 struct option const long_opts[],
			 const char * const opts_help[])
{
	FILE *fp = errmsg ? stderr : stdout;
	const char a_arg[] = "<arg>";
	size_t a_arg_len = strlen(a_arg) + 1;
	size_t i;
	int optlen;

	fprintf(fp,
		"Usage: %s\n"
		"\n"
		"Options: -[%s]\n", synopsis, short_opts);

	/* prescan the --long opt length to auto-align */
	optlen = 0;
	for (i = 0; long_opts[i].name; ++i) {
		/* +1 is for space between --opt and help text */
		int l = strlen(long_opts[i].name) + 1;
		if (long_opts[i].has_arg == a_argument)
			l += a_arg_len;
		if (optlen < l)
			optlen = l;
	}

	for (i = 0; long_opts[i].name; ++i) {
		/* helps when adding new applets or options */
		assert(opts_help[i] != NULL);

		/* first output the short flag if it has one */
		if (long_opts[i].val > '~')
			fprintf(fp, "      ");
		else
			fprintf(fp, "  -%c, ", long_opts[i].val);

		/* then the long flag */
		if (long_opts[i].has_arg == no_argument)
			fprintf(fp, "--%-*s", optlen, long_opts[i].name);
		else
			fprintf(fp, "--%s %s%*s", long_opts[i].name, a_arg,
				(int)(optlen - strlen(long_opts[i].name) - a_arg_len), "");

		/* finally the help text */
		fprintf(fp, "%s\n", opts_help[i]);
	}

	if (errmsg) {
		fprintf(fp, "\nError: %s\n", errmsg);
		exit(EXIT_FAILURE);
	} else
		exit(EXIT_SUCCESS);
}
