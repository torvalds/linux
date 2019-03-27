/*-
 * Copyright (c) 2007 Michael Telahun Makonnen
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

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_subr.h"

int g_total;
int g_pass;
int g_fail;
char g_funcname[FUNCNAMESIZE];
char g_testdesc[LINESIZE];
char g_errbuf[LINESIZE];

void
set_funcname(char *bufp, size_t bufsize)
{
	strlcpy(g_funcname, bufp,
	    bufsize < FUNCNAMESIZE ? bufsize : FUNCNAMESIZE);
}

/*
 * desc is a NULL-terminated string.
 */
void
checkptr(caddr_t expected, caddr_t got, const char *desc)
{
	int  len;
	int  failed;
	char sbuf[LINESIZE];

	memset((void *)sbuf, 0, LINESIZE);
	snprintf(g_testdesc, LINESIZE, desc);

	failed = 1;
	g_total++;
	if (got == expected) {
		len = snprintf(sbuf, LINESIZE, "ok");
		g_pass++;
		failed = 0;
	} else {
		len = snprintf(sbuf, LINESIZE, "not ok");
		snprintf(g_errbuf, LINESIZE, " : Expected %#x, but got %#x",
		    (unsigned int)expected, (unsigned int)got);
		g_fail++;
	}
	snprintf(sbuf + len, LINESIZE - len, " %d - %s (%s)",
	    g_total, g_funcname, g_testdesc);
	printf(sbuf);
	if (failed)
		printf(g_errbuf);
	printf("\n");
	fflush(NULL);
	memset((void *)g_errbuf, 0, LINESIZE);
	memset((void *)g_testdesc, 0, LINESIZE);
}

void
checkstr(const char *expected, const char *got, size_t explen, const char *desc)
{
	int  len;
	int  failed;
	char sbuf[LINESIZE];

	memset((void *)sbuf, 0, LINESIZE);
	snprintf(g_testdesc, LINESIZE, desc);

	failed = 1;
	g_total++;
	if (strncmp(expected, got, explen) == 0) {
		len = snprintf(sbuf, LINESIZE, "ok");
		g_pass++;
		failed = 0;
	} else {
		len = snprintf(sbuf, LINESIZE, "not ok");
		snprintf(g_errbuf, LINESIZE,
		    " : Expected %s, but got %s", expected, got);
		g_fail++;
	}
	snprintf(sbuf + len, LINESIZE - len, " %d - %s (%s)",
	    g_total, g_funcname, g_testdesc);
	printf(sbuf);
	if (failed)
		printf(g_errbuf);
	printf("\n");
	fflush(NULL);
	memset((void *)g_errbuf, 0, LINESIZE);
	memset((void *)g_testdesc, 0, LINESIZE);
}

void
checknum(int expected, int got, int cmp, const char *desc)
{
	int  len;
	int  pass;
	int  failed;
	char sbuf[LINESIZE];

	memset((void *)sbuf, 0, LINESIZE);
	snprintf(g_testdesc, LINESIZE, desc);

	failed = 1;
	pass = 0;
	g_total++;
	switch(cmp) {
	case 0:
		pass = (got == expected) ? 1 : 0;
		break;
	case 1:
		pass = (got > expected) ? 1 : 0;
		break;
	case -1:
		pass = (got < expected) ? 1 : 0;
		break;
	}
	if (pass != 0) {
		len = snprintf(sbuf, LINESIZE, "ok");
		g_pass++;
		failed = 0;
	} else {
		len = snprintf(sbuf, LINESIZE, "not ok");
		snprintf(g_errbuf, LINESIZE,
		    " : Expected %d, but got %d", expected, got);
		g_fail++;
	}
	snprintf(sbuf + len, LINESIZE - len, " %d - %s (%s)",
	    g_total, g_funcname, g_testdesc);
	printf(sbuf);
	if (failed)
		printf(g_errbuf);
	printf("\n");
	fflush(NULL);
	memset((void *)g_errbuf, 0, LINESIZE);
	memset((void *)g_testdesc, 0, LINESIZE);
}
