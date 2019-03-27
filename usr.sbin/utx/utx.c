/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011-2012 Ed Schouten <ed@FreeBSD.org>
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

#include <sys/time.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utmpx.h>

static int
b16_pton(const char *in, char *out, size_t len)
{
	size_t i;

	for (i = 0; i < len * 2; i++)
		if (!isxdigit((unsigned char)in[i]))
			return (1);
	for (i = 0; i < len; i++)
		sscanf(&in[i * 2], "%02hhx", &out[i]);
	return (0);
}

static int
rm(char *id[])
{
	struct utmpx utx = { .ut_type = DEAD_PROCESS };
	size_t len;
	int ret = 0;

	(void)gettimeofday(&utx.ut_tv, NULL);
	for (; *id != NULL; id++) {
		len = strlen(*id);
		if (len <= sizeof(utx.ut_id)) {
			/* Identifier as string. */
			strncpy(utx.ut_id, *id, sizeof(utx.ut_id));
		} else if (len != sizeof(utx.ut_id) * 2 ||
		    b16_pton(*id, utx.ut_id, sizeof(utx.ut_id)) != 0) {
			/* Also not hexadecimal. */
			fprintf(stderr, "%s: Invalid identifier format\n", *id);
			ret = 1;
			continue;
		}

		/* Zap the entry. */
		if (pututxline(&utx) == NULL) {
			perror(*id);
			ret = 1;
		}
	}
	return (ret);
}

static int
boot(short type)
{
	struct utmpx utx = { .ut_type = type };

	(void)gettimeofday(&utx.ut_tv, NULL);
	if (pututxline(&utx) == NULL) {
		perror("pututxline");
		return (1);
	}
	return (0);
}

int
main(int argc, char *argv[])
{

	if (argc == 2 && strcmp(argv[1], "boot") == 0)
		return (boot(BOOT_TIME));
	else if (argc == 2 && strcmp(argv[1], "shutdown") == 0)
		return (boot(SHUTDOWN_TIME));
	else if (argc >= 3 && strcmp(argv[1], "rm") == 0)
		return (rm(&argv[2]));

	fprintf(stderr,
	    "usage: utx boot\n"
	    "       utx shutdown\n"
	    "       utx rm identifier ...\n");
	exit(1);
}
