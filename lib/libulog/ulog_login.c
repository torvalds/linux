/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Ed Schouten <ed@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/time.h>
#include <paths.h>
#include <sha.h>
#include <string.h>
#include <unistd.h>
#include <utmpx.h>
#include "ulog.h"

static void
ulog_fill(struct utmpx *utx, const char *line)
{
	SHA_CTX c;
	char id[SHA_DIGEST_LENGTH];

	/* Remove /dev/ component. */
	if (strncmp(line, _PATH_DEV, sizeof _PATH_DEV - 1) == 0)
		line += sizeof _PATH_DEV - 1;

	memset(utx, 0, sizeof *utx);

	utx->ut_pid = getpid();
	gettimeofday(&utx->ut_tv, NULL);
	strncpy(utx->ut_line, line, sizeof utx->ut_line);

	SHA1_Init(&c);
	SHA1_Update(&c, "libulog", 7);
	SHA1_Update(&c, utx->ut_line, sizeof utx->ut_line);
	SHA1_Final(id, &c);

	memcpy(utx->ut_id, id, MIN(sizeof utx->ut_id, sizeof id));
}

void
ulog_login(const char *line, const char *user, const char *host)
{
	struct utmpx utx;

	ulog_fill(&utx, line);
	utx.ut_type = USER_PROCESS;
	strncpy(utx.ut_user, user, sizeof utx.ut_user);
	if (host != NULL)
		strncpy(utx.ut_host, host, sizeof utx.ut_host);
	pututxline(&utx);
}

void
ulog_logout(const char *line)
{
	struct utmpx utx;

	ulog_fill(&utx, line);
	utx.ut_type = DEAD_PROCESS;
	pututxline(&utx);
}
