/*-
 * Copyright (c) 2005 Michael Bushkov <bushman@rsu.ru>
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

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

#include "log.h"

void
__log_msg(int level, const char *sender, const char *message, ...)
{
	va_list ap;
	char	*fmessage;

	fmessage = NULL;
	va_start(ap, message);
	vasprintf(&fmessage, message, ap);
	va_end(ap);
	assert(fmessage != NULL);

	printf("M%d from %s: %s\n", level, sender, fmessage);
#ifndef NO_SYSLOG
	if (level == 0)
		syslog(LOG_INFO, "nscd message (from %s): %s", sender,
		fmessage);
#endif
	free(fmessage);
}

void
__log_err(int level, const char *sender, const char *error, ...)
{
	va_list ap;
	char	*ferror;

	ferror = NULL;
	va_start(ap, error);
	vasprintf(&ferror, error, ap);
	va_end(ap);
	assert(ferror != NULL);

	printf("E%d from %s: %s\n", level, sender, ferror);

#ifndef NO_SYSLOG
	if (level == 0)
		syslog(LOG_ERR, "nscd error (from %s): %s", sender, ferror);
#endif
	free(ferror);
}
