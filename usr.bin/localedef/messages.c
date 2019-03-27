/*-
 * Copyright 2010 Nexenta Systems, Inc.  All rights reserved.
 * Copyright 2015 John Marino <draco@marino.st>
 *
 * This source code is derived from the illumos localedef command, and
 * provided under BSD-style license terms by Nexenta Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * LC_MESSAGES database generation routines for localedef.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include "localedef.h"
#include "parser.h"
#include "lmessages.h"

static struct lc_messages_T msgs;

void
init_messages(void)
{
	(void) memset(&msgs, 0, sizeof (msgs));
}

void
add_message(wchar_t *wcs)
{
	char *str;

	if ((str = to_mb_string(wcs)) == NULL) {
		INTERR;
		return;
	}
	free(wcs);

	switch (last_kw) {
	case T_YESSTR:
		msgs.yesstr = str;
		break;
	case T_NOSTR:
		msgs.nostr = str;
		break;
	case T_YESEXPR:
		msgs.yesexpr = str;
		break;
	case T_NOEXPR:
		msgs.noexpr = str;
		break;
	default:
		free(str);
		INTERR;
		break;
	}
}

void
dump_messages(void)
{
	FILE *f;
	char *ptr;

	if (msgs.yesstr == NULL) {
		warn("missing field 'yesstr'");
		msgs.yesstr = "";
	}
	if (msgs.nostr == NULL) {
		warn("missing field 'nostr'");
		msgs.nostr = "";
	}

	/*
	 * CLDR likes to add : separated lists for yesstr and nostr.
	 * Legacy Solaris code does not seem to grok this.  Fix it.
	 */
	if ((ptr = strchr(msgs.yesstr, ':')) != NULL)
		*ptr = 0;
	if ((ptr = strchr(msgs.nostr, ':')) != NULL)
		*ptr = 0;

	if ((f = open_category()) == NULL) {
		return;
	}

	if ((putl_category(msgs.yesexpr, f) == EOF) ||
	    (putl_category(msgs.noexpr, f) == EOF) ||
	    (putl_category(msgs.yesstr, f) == EOF) ||
	    (putl_category(msgs.nostr, f) == EOF)) {
		return;
	}
	close_category(f);
}
