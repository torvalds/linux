/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "calendar.h"

const char *fdays[] = {
	"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
	"Saturday", NULL,
};

const char *days[] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", NULL,
};

const char *fmonths[] = {
	"January", "February", "March", "April", "May", "June", "Juli",
	"August", "September", "October", "November", "December", NULL,
};

const char *months[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec", NULL,
};

const char *sequences[] = {
	"First", "Second", "Third", "Fourth", "Fifth", "Last"
};

struct fixs fndays[8];		/* full national days names */
struct fixs ndays[8];		/* short national days names */
struct fixs fnmonths[13];	/* full national months names */
struct fixs nmonths[13];	/* short national month names */
struct fixs nsequences[10];	/* national sequence names */


void
setnnames(void)
{
	char buf[80];
	int i, l;
	struct tm tm;

	memset(&tm, 0, sizeof(struct tm));
	for (i = 0; i < 7; i++) {
		tm.tm_wday = i;
		strftime(buf, sizeof(buf), "%a", &tm);
		for (l = strlen(buf);
		     l > 0 && isspace((unsigned char)buf[l - 1]);
		     l--)
			;
		buf[l] = '\0';
		if (ndays[i].name != NULL)
			free(ndays[i].name);
		if ((ndays[i].name = strdup(buf)) == NULL)
			errx(1, "cannot allocate memory");
		ndays[i].len = strlen(buf);

		strftime(buf, sizeof(buf), "%A", &tm);
		for (l = strlen(buf);
		     l > 0 && isspace((unsigned char)buf[l - 1]);
		     l--)
			;
		buf[l] = '\0';
		if (fndays[i].name != NULL)
			free(fndays[i].name);
		if ((fndays[i].name = strdup(buf)) == NULL)
			errx(1, "cannot allocate memory");
		fndays[i].len = strlen(buf);
	}

	memset(&tm, 0, sizeof(struct tm));
	for (i = 0; i < 12; i++) {
		tm.tm_mon = i;
		strftime(buf, sizeof(buf), "%b", &tm);
		for (l = strlen(buf);
		     l > 0 && isspace((unsigned char)buf[l - 1]);
		     l--)
			;
		buf[l] = '\0';
		if (nmonths[i].name != NULL)
			free(nmonths[i].name);
		if ((nmonths[i].name = strdup(buf)) == NULL)
			errx(1, "cannot allocate memory");
		nmonths[i].len = strlen(buf);

		strftime(buf, sizeof(buf), "%B", &tm);
		for (l = strlen(buf);
		     l > 0 && isspace((unsigned char)buf[l - 1]);
		     l--)
			;
		buf[l] = '\0';
		if (fnmonths[i].name != NULL)
			free(fnmonths[i].name);
		if ((fnmonths[i].name = strdup(buf)) == NULL)
			errx(1, "cannot allocate memory");
		fnmonths[i].len = strlen(buf);
	}
}

void
setnsequences(char *seq)
{
	int i;
	char *p;

	p = seq;
	for (i = 0; i < 5; i++) {
		nsequences[i].name = p;
		if ((p = strchr(p, ' ')) == NULL) {
			/* Oh oh there is something wrong. Erase! Erase! */
			for (i = 0; i < 5; i++) {
				nsequences[i].name = NULL;
				nsequences[i].len = 0;
			}
			return;
		}
		*p = '\0';
		p++;
	}
	nsequences[i].name = p;

	for (i = 0; i < 5; i++) {
		nsequences[i].name = strdup(nsequences[i].name);
		nsequences[i].len = nsequences[i + 1].name - nsequences[i].name;
	}
	nsequences[i].name = strdup(nsequences[i].name);
	nsequences[i].len = strlen(nsequences[i].name);

	return;
}
