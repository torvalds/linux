/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Mike Barcroft <mike@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <fmtmsg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Default value for MSGVERB. */
#define	DFLT_MSGVERB	"label:severity:text:action:tag"

/* Maximum valid size for a MSGVERB. */
#define	MAX_MSGVERB	sizeof(DFLT_MSGVERB)

static char	*printfmt(char *, long, const char *, int, const char *,
		    const char *, const char *);
static char	*nextcomp(const char *);
static const char
		*sevinfo(int);
static int	 validmsgverb(const char *);

int
fmtmsg(long class, const char *label, int sev, const char *text,
    const char *action, const char *tag)
{
	FILE *fp;
	char *env, *msgverb, *output;

	if (class & MM_PRINT) {
		if ((env = getenv("MSGVERB")) != NULL && *env != '\0' &&
		    strlen(env) <= strlen(DFLT_MSGVERB)) {
			if ((msgverb = strdup(env)) == NULL)
				return (MM_NOTOK);
			else if (validmsgverb(msgverb) == 0) {
				free(msgverb);
				goto def;
			}
		} else {
def:
			if ((msgverb = strdup(DFLT_MSGVERB)) == NULL)
				return (MM_NOTOK);
		}
		output = printfmt(msgverb, class, label, sev, text, action,
		    tag);
		if (output == NULL) {
			free(msgverb);
			return (MM_NOTOK);
		}
		if (*output != '\0')
			fprintf(stderr, "%s", output);
		free(msgverb);
		free(output);
	}
	if (class & MM_CONSOLE) {
		output = printfmt(DFLT_MSGVERB, class, label, sev, text,
		    action, tag);
		if (output == NULL)
			return (MM_NOCON);
		if (*output != '\0') {
			if ((fp = fopen("/dev/console", "ae")) == NULL) {
				free(output);
				return (MM_NOCON);
			}
			fprintf(fp, "%s", output);
			fclose(fp);
		}
		free(output);
	}
	return (MM_OK);
}

#define INSERT_COLON							\
	if (*output != '\0')						\
		strlcat(output, ": ", size)
#define INSERT_NEWLINE							\
	if (*output != '\0')						\
		strlcat(output, "\n", size)
#define INSERT_SPACE							\
	if (*output != '\0')						\
		strlcat(output, " ", size)

/*
 * Returns NULL on memory allocation failure, otherwise returns a pointer to
 * a newly malloc()'d output buffer.
 */
static char *
printfmt(char *msgverb, long class, const char *label, int sev,
    const char *text, const char *act, const char *tag)
{
	size_t size;
	char *comp, *output;
	const char *sevname;

	size = 32;
	if (label != MM_NULLLBL)
		size += strlen(label);
	if ((sevname = sevinfo(sev)) != NULL)
		size += strlen(sevname);
	if (text != MM_NULLTXT)
		size += strlen(text);
	if (act != MM_NULLACT)
		size += strlen(act);
	if (tag != MM_NULLTAG)
		size += strlen(tag);

	if ((output = malloc(size)) == NULL)
		return (NULL);
	*output = '\0';
	while ((comp = nextcomp(msgverb)) != NULL) {
		if (strcmp(comp, "label") == 0 && label != MM_NULLLBL) {
			INSERT_COLON;
			strlcat(output, label, size);
		} else if (strcmp(comp, "severity") == 0 && sevname != NULL) {
			INSERT_COLON;
			strlcat(output, sevinfo(sev), size);
		} else if (strcmp(comp, "text") == 0 && text != MM_NULLTXT) {
			INSERT_COLON;
			strlcat(output, text, size);
		} else if (strcmp(comp, "action") == 0 && act != MM_NULLACT) {
			INSERT_NEWLINE;
			strlcat(output, "TO FIX: ", size);
			strlcat(output, act, size);
		} else if (strcmp(comp, "tag") == 0 && tag != MM_NULLTAG) {
			INSERT_SPACE;
			strlcat(output, tag, size);
		}
	}
	INSERT_NEWLINE;
	return (output);
}

/*
 * Returns a component of a colon delimited string.  NULL is returned to
 * indicate that there are no remaining components.  This function must be
 * called until it returns NULL in order for the local state to be cleared.
 */
static char *
nextcomp(const char *msgverb)
{
	static char lmsgverb[MAX_MSGVERB], *state;
	char *retval;
	
	if (*lmsgverb == '\0') {
		strlcpy(lmsgverb, msgverb, sizeof(lmsgverb));
		retval = strtok_r(lmsgverb, ":", &state);
	} else {
		retval = strtok_r(NULL, ":", &state);
	}
	if (retval == NULL)
		*lmsgverb = '\0';
	return (retval);
}

static const char *
sevinfo(int sev)
{

	switch (sev) {
	case MM_HALT:
		return ("HALT");
	case MM_ERROR:
		return ("ERROR");
	case MM_WARNING:
		return ("WARNING");
	case MM_INFO:
		return ("INFO");
	default:
		return (NULL);
	}
}

/*
 * Returns 1 if the msgverb list is valid, otherwise 0.
 */
static int
validmsgverb(const char *msgverb)
{
	const char *validlist = "label\0severity\0text\0action\0tag\0";
	char *msgcomp;
	size_t len1, len2;
	const char *p;
	int equality;

	equality = 0;
	while ((msgcomp = nextcomp(msgverb)) != NULL) {
		equality--;
		len1 = strlen(msgcomp);
		for (p = validlist; (len2 = strlen(p)) != 0; p += len2 + 1) {
			if (len1 == len2 && memcmp(msgcomp, p, len1) == 0)
				equality++;
		}
	}
	return (!equality);
}
