/*	$OpenBSD: misc.c,v 1.89 2025/07/31 13:37:06 krw Exp $	*/

/*
 * Copyright (c) 1997 Tobias Weingartner
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/disklabel.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uuid.h>

#include "part.h"
#include "disk.h"
#include "misc.h"

const struct unit_type	unit_types[] = {
	{ "b"	, 1LL				, "Bytes"	},
	{ " "	, 0LL				, "Sectors"	},
	{ "K"	, 1024LL			, "Kilobytes"	},
	{ "M"	, 1024LL * 1024			, "Megabytes"	},
	{ "G"	, 1024LL * 1024 *1024		, "Gigabytes"	},
	{ "T"	, 1024LL * 1024 * 1024 * 1024	, "Terabytes"	},
};
#define	SECTORS		1

double
units_size(const char *units, const uint64_t sectors,
    const struct unit_type **ut)
{
	double			size;
	unsigned int		i;

	*ut = &unit_types[SECTORS];
	size = sectors;

	for (i = 0; i < nitems(unit_types); i++) {
		if (strncasecmp(unit_types[i].ut_abbr, units, 1) == 0)
			*ut = &unit_types[i];
	}

	if ((*ut)->ut_conversion == 0)
		return size;
	else
		return (size * dl.d_secsize) / (*ut)->ut_conversion;
}

void
string_from_line(char *buf, const size_t buflen, const int trim)
{
	static char		*line;
	static size_t		 sz;
	ssize_t			 len;
	size_t			 n;
	unsigned int		 i;

	len = getline(&line, &sz, stdin);
	if (len == -1)
		errx(1, "eof");

	switch (trim) {
	case UNTRIMMED:
		line[strcspn(line, "\n")] = '\0';
		n = strlcpy(buf, line, buflen);
		break;
	case TRIMMED:
		for (i = strlen(line); i > 0; i--) {
			if (isspace((unsigned char)line[i - 1]) == 0)
				break;
			line[i - 1] = '\0';
		}
		n = strlcpy(buf, line + strspn(line, WHITESPACE), buflen);
		break;
	}

	if (n >= buflen) {
		printf("input too long\n");
		memset(buf, 0, buflen);
	}
}

int
ask_yn(const char *str)
{
	int			ch, first;
	extern int		y_flag;

	if (y_flag)
		return 1;

	printf("%s [n] ", str);
	fflush(stdout);

	first = ch = getchar();
	while (ch != '\n' && ch != EOF)
		ch = getchar();

	if (ch == EOF || first == EOF)
		errx(1, "eof");

	return first == 'y' || first == 'Y';
}

/*
 * adapted from sbin/disklabel/editor.c
 */
uint64_t
getuint64(const char *prompt, uint64_t oval, const uint64_t minval,
    const uint64_t maxval)
{
	char			buf[BUFSIZ], *endptr, *p, operator = '\0';
	const int		secsize = dl.d_secsize;
	size_t			n;
	int64_t			mult = 1;
	double			d, d2;
	int			rslt, secpercyl, saveerr;
	char			unit;

	if (oval > maxval)
		oval = maxval;
	if (oval < minval)
		oval = minval;

	secpercyl = disk.dk_sectors * disk.dk_heads;

	do {
		printf("%s [%llu - %llu]: [%llu] ", prompt, minval, maxval,
		    oval);
		string_from_line(buf, sizeof(buf), TRIMMED);

		if (buf[0] == '\0') {
			rslt = snprintf(buf, sizeof(buf), "%llu", oval);
			if (rslt < 0 || rslt >= sizeof(buf))
				errx(1, "default value too long");
		} else if (buf[0] == '*' && buf[1] == '\0') {
			return maxval;
		}

		/* deal with units */
		n = strlen(buf);
		switch (tolower((unsigned char)buf[n-1])) {
		case 'c':
			unit = 'c';
			mult = secpercyl;
			buf[--n] = '\0';
			break;
		case 'b':
			unit = 'b';
			mult = -(int64_t)secsize;
			buf[--n] = '\0';
			break;
		case 's':
			unit = 's';
			mult = 1;
			buf[--n] = '\0';
			break;
		case 'k':
			unit = 'k';
			if (secsize > 1024)
				mult = -(int64_t)secsize / 1024LL;
			else
				mult = 1024LL / secsize;
			buf[--n] = '\0';
			break;
		case 'm':
			unit = 'm';
			mult = (1024LL * 1024) / secsize;
			buf[--n] = '\0';
			break;
		case 'g':
			unit = 'g';
			mult = (1024LL * 1024 * 1024) / secsize;
			buf[--n] = '\0';
			break;
		case 't':
			unit = 't';
			mult = (1024LL * 1024 * 1024 * 1024) / secsize;
			buf[--n] = '\0';
			break;
		default:
			unit = ' ';
			mult = 1;
			break;
		}

		/* deal with the operator */
		p = &buf[0];
		if (*p == '+' || *p == '-')
			operator = *p++;
		else
			operator = ' ';

		endptr = p;
		errno = 0;
		d = strtod(p, &endptr);
		saveerr = errno;
		d2 = d;
		if (mult > 0)
			d *= mult;
		else {
			d /= (-mult);
			d2 = d;
		}

		/* Apply the operator */
		if (operator == '+')
			d = oval + d;
		else if (operator == '-') {
			d = oval - d;
			d2 = d;
		}

		if (saveerr == ERANGE || d > maxval || d < minval || d < d2) {
			printf("%s is out of range: %c%s%c\n", prompt, operator,
			    p, unit);
		} else if (*endptr != '\0') {
			printf("%s is invalid: %c%s%c\n", prompt, operator,
			    p, unit);
		} else {
			break;
		}
	} while (1);

	return (uint64_t)d;
}

int
hex_octet(char *buf)
{
	char			*cp;
	long			 num;

	cp = buf;
	num = strtol(buf, &cp, 16);

	if (cp == buf || *cp != '\0')
		return -1;

	if (num < 0 || num > 0xff)
		return -1;

	return num;
}

uint32_t
string_to_uuid(const char *uuidstr, struct uuid *uuid)
{
	uint32_t		status;

	uuid_from_string(uuidstr, uuid, &status);

	return status == uuid_s_bad_version ? uuid_s_ok : status;
}
