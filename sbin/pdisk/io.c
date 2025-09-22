/*	$OpenBSD: io.c,v 1.31 2016/02/01 18:55:00 krw Exp $	*/

/*
 * io.c - simple io and input parsing routines
 *
 * Written by Eryk Vershen
 */

/*
 * Copyright 1996,1997,1998 by Apple Computer, Inc.
 *              All Rights Reserved
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright notice appears in all copies and
 * that both the copyright notice and this permission notice appear in
 * supporting documentation.
 *
 * APPLE COMPUTER DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE.
 *
 * IN NO EVENT SHALL APPLE COMPUTER BE LIABLE FOR ANY SPECIAL, INDIRECT, OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN ACTION OF CONTRACT,
 * NEGLIGENCE, OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/queue.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "partition_map.h"
#include "io.h"

#define UNGET_MAX_COUNT 10

short	unget_buf[UNGET_MAX_COUNT + 1];
int	unget_count;

static int	get_number(long *);
static char    *get_string(int);
static int	my_getch (void);

int
my_getch()
{
	if (unget_count > 0)
		return unget_buf[--unget_count];
	else
		return getc(stdin);
}


void
my_ungetch(int c)
{
	/*
         * In practice there is never more than one character in
         * the unget_buf, but what's a little overkill among friends?
         */
	if (unget_count < UNGET_MAX_COUNT)
		unget_buf[unget_count++] = c;
	else
		errx(1, "Programmer error in my_ungetch().");
}

void
flush_to_newline(int keep_newline)
{
	int c;

	for (;;) {
		c = my_getch();

		if (c <= 0) {
			break;
		} else if (c == '\n') {
			if (keep_newline)
				my_ungetch(c);
			break;
		} else {
			/* skip */
		}
	}
	return;
}


int
get_okay(const char *prompt, int default_value)
{
	int c;

	flush_to_newline(0);
	printf("%s", prompt);

	for (;;) {
		c = my_getch();

		if (c <= 0) {
			break;
		} else if (c == ' ' || c == '\t') {
			/* skip blanks and tabs */
		} else if (c == '\n') {
			my_ungetch(c);
			return default_value;
		} else if (c == 'y' || c == 'Y') {
			return 1;
		} else if (c == 'n' || c == 'N') {
			return 0;
		} else {
			flush_to_newline(0);
			printf("%s", prompt);
		}
	}
	return -1;
}

int
get_command(const char *prompt, int promptBeforeGet, int *command)
{
	int c;

	if (promptBeforeGet)
		printf("%s", prompt);

	for (;;) {
		c = my_getch();

		if (c <= 0) {
			break;
		} else if (c == ' ' || c == '\t') {
			/* skip blanks and tabs */
		} else if (c == '\n') {
			printf("%s", prompt);
		} else {
			*command = c;
			return 1;
		}
	}
	return 0;
}

int
get_number_argument(const char *prompt, long *number)
{
	int c;
	int result = 0;

	for (;;) {
		c = my_getch();

		if (c <= 0) {
			break;
		} else if (c == ' ' || c == '\t') {
			/* skip blanks and tabs */
		} else if (c == '\n') {
			printf("%s", prompt);
		} else if ('0' <= c && c <= '9') {
			my_ungetch(c);
			result = get_number(number);
			break;
		} else {
			my_ungetch(c);
			*number = 0;
			break;
		}
	}
	return result;
}


int
get_number(long *number)
{
	long value;
	int c;

	value = 0;
	while ((c = my_getch())) {
		if (c >= '0' && c <= '9') {
			value = value * 10 + (c - '0');
		} else if (c == ' ' || c == '\t' || c == '\n') {
			my_ungetch(c);
			*number = value;
			return 1;
		} else {
			return 0;
		}
	}

	return 0;
}

char *
get_dpistr_argument(const char *prompt)
{
	int c;

	for (;;) {
		c = my_getch();

		if (c <= 0) {
			break;
		} else if (c == ' ' || c == '\t') {
			/* skip blanks and tabs */
		} else if (c == '\n') {
			printf("%s", prompt);
		} else if (c == '"' || c == '\'') {
			return get_string(c);
		} else if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') ||
		    (c == '-' || c == '/' || c == '.' || c == ':')) {
			my_ungetch(c);
			return get_string(' ');
		} else {
			my_ungetch(c);
			return NULL;
		}
	}
	return NULL;
}


char *
get_string(int eos)
{
	char buf[DPISTRLEN+1];
	char *s, *limit;
	int c;

	memset(buf, 0, sizeof(buf));
	limit = buf + sizeof(buf);

	c = my_getch();
	for (s = buf;; c = my_getch()) {
		if (c <= 0 || c == eos || (eos == ' ' && c == '\t')) {
			*s = 0;
			break;
		} else if (c == '\n') {
			*s = 0;
			my_ungetch(c);
			break;
		} else {
			*s++ = c;
			if (s >= limit)
				return NULL;
		}
	}
	return strdup(buf);
}


unsigned long
get_multiplier(long divisor)
{
	unsigned long result, extra;
	int c;

	c = my_getch();

	extra = 1;
	if (c <= 0 || divisor <= 0) {
		result = 0;
	} else if (c == 't' || c == 'T') {
		result = 1024 * 1024;
		extra = 1024 * 1024;
	} else if (c == 'g' || c == 'G') {
		result = 1024 * 1024 * 1024;
	} else if (c == 'm' || c == 'M') {
		result = 1024 * 1024;
	} else if (c == 'k' || c == 'K') {
		result = 1024;
	} else {
		my_ungetch(c);
		result = 1;
	}
	if (result > 1) {
		if (extra > 1) {
			result /= divisor;
			if (result >= 4096)
				result = 0; /* overflow -> 20bits + >12bits */
			else
				result *= extra;
		} else if (result >= divisor) {
			result /= divisor;
		} else {
			result = 1;
		}
	}
	return result;
}


int
get_partition_modifier(void)
{
	int c, result;

	result = 0;

	c = my_getch();

	if (c == 'p' || c == 'P')
		result = 1;
	else if (c > 0)
		my_ungetch(c);

	return result;
}


int
number_of_digits(unsigned long value)
{
	int j;

	j = 1;
	while (value > 9) {
		j++;
		value = value / 10;
	}
	return j;
}


/*
 * Print a message on standard error & flush the input.
 */
void
bad_input(const char *fmt,...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	flush_to_newline(1);
}
