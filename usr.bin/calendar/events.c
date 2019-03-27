/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1992-2009 Edwin Groothuis <edwin@FreeBSD.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/time.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef WITH_ICONV
#include <iconv.h>
#include <errno.h>
#include <langinfo.h>

static iconv_t conv = (iconv_t)-1;
static char *currentEncoding = NULL;

#endif

#include "pathnames.h"
#include "calendar.h"

#ifdef WITH_ICONV
void
set_new_encoding(void)
{
	const char *newenc;

	newenc = nl_langinfo(CODESET);
	if (currentEncoding == NULL) {
		currentEncoding = strdup(newenc);
		if (currentEncoding == NULL)
			errx(1, "set_new_encoding: cannot allocate memory");
		return;
	}
	if (strcmp(currentEncoding, newenc) == 0)
		return;
	free(currentEncoding);
	currentEncoding = strdup(newenc);
	if (currentEncoding == NULL)
		errx(1, "set_new_encoding: cannot allocate memory");
	if (conv != (iconv_t) -1) {
		iconv_close(conv);
		conv = (iconv_t) -1;
	}
}
#endif

static char *
convert(char *input)
{
	char *output;
#ifdef WITH_ICONV
	size_t inleft, outleft, converted = 0;
	char *outbuf, *tmp;
	char *inbuf;
	size_t outlen;

	if (currentEncoding == NULL) {
		output = strdup(input);
		if (output == NULL)
			errx(1, "convert: cannot allocate memory");
		return (output);
	}
	if (conv == (iconv_t)-1) {
		conv = iconv_open(outputEncoding, currentEncoding);
		if (conv == (iconv_t)-1) {
			if (errno == EINVAL)
				errx(1, "Conversion is not supported");
			else
				err(1, "Initialization failure");
		}
	}

	inleft = strlen(input);
	inbuf = input;

	outlen = inleft;
	if ((output = malloc(outlen + 1)) == NULL)
		errx(1, "convert: cannot allocate memory");

	for (;;) {
		errno = 0;
		outbuf = output + converted;
		outleft = outlen - converted;

		converted = iconv(conv, (char **) &inbuf, &inleft, &outbuf, &outleft);
		if (converted != (size_t) -1 || errno == EINVAL) {
			/* finished or invalid multibyte, so truncate and ignore */
			break;
		}

		if (errno != E2BIG) {
			free(output);
			err(1, "convert");
		}

		converted = outbuf - output;
		outlen += inleft * 2;

		if ((tmp = realloc(output, outlen + 1)) == NULL) {
			free(output);
			errx(1, "convert: cannot allocate memory");
		}

		output = tmp;
		outbuf = output + converted;
	}

	/* flush the iconv conversion */
	iconv(conv, NULL, NULL, &outbuf, &outleft);

	/* null terminate the string */
	*outbuf = '\0';
#else
	output = strdup(input);
	if (output == NULL)
		errx(1, "convert: cannot allocate memory");
#endif

	return (output);
}

struct event *
event_add(int year, int month, int day, char *date, int var, char *txt,
    char *extra)
{
	struct event *e;

	/*
	 * Creating a new event:
	 * - Create a new event
	 * - Copy the machine readable day and month
	 * - Copy the human readable and language specific date
	 * - Copy the text of the event
	 */
	e = (struct event *)calloc(1, sizeof(struct event));
	if (e == NULL)
		errx(1, "event_add: cannot allocate memory");
	e->month = month;
	e->day = day;
	e->var = var;
	e->date = convert(date);
	if (e->date == NULL)
		errx(1, "event_add: cannot allocate memory");
	e->text = convert(txt);
	if (e->text == NULL)
		errx(1, "event_add: cannot allocate memory");
	e->extra = NULL;
	if (extra != NULL && extra[0] != '\0')
		e->extra = convert(extra);
	addtodate(e, year, month, day);
	return (e);
}

void
event_continue(struct event *e, char *txt)
{
	char *oldtext, *text;

	text = convert(txt);
	oldtext = e->text;
	if (oldtext == NULL)
		errx(1, "event_continue: cannot allocate memory");

	asprintf(&e->text, "%s\n%s", oldtext, text);
	if (e->text == NULL)
		errx(1, "event_continue: cannot allocate memory");
	free(oldtext);
	free(text);

	return;
}

void
event_print_all(FILE *fp)
{
	struct event *e;

	while (walkthrough_dates(&e) != 0) {
#ifdef DEBUG
		fprintf(stderr, "event_print_allmonth: %d, day: %d\n",
		    month, day);
#endif

		/*
		 * Go through all events and print the text of the matching
		 * dates
		 */
		while (e != NULL) {
			(void)fprintf(fp, "%s%c%s%s%s%s\n", e->date,
			    e->var ? '*' : ' ', e->text,
			    e->extra != NULL ? " (" : "",
			    e->extra != NULL ? e->extra : "",
			    e->extra != NULL ? ")" : ""
			);

			e = e->next;
		}
	}
}
