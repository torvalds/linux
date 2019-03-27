/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Ed Schouten <ed@FreeBSD.org>
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

#include "namespace.h"
#include <sys/endian.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <utmpx.h>
#include "utxdb.h"
#include "un-namespace.h"

#ifdef __NO_TLS
static FILE *uf = NULL;
static int udb;
#else
static _Thread_local FILE *uf = NULL;
static _Thread_local int udb;
#endif

int
setutxdb(int db, const char *file)
{
	struct stat sb;

	switch (db) {
	case UTXDB_ACTIVE:
		if (file == NULL)
			file = _PATH_UTX_ACTIVE;
		break;
	case UTXDB_LASTLOGIN:
		if (file == NULL)
			file = _PATH_UTX_LASTLOGIN;
		break;
	case UTXDB_LOG:
		if (file == NULL)
			file = _PATH_UTX_LOG;
		break;
	default:
		errno = EINVAL;
		return (-1);
	}

	if (uf != NULL)
		fclose(uf);
	uf = fopen(file, "re");
	if (uf == NULL)
		return (-1);

	if (db != UTXDB_LOG) {
		/* Safety check: never use broken files. */
		if (_fstat(fileno(uf), &sb) != -1 &&
		    sb.st_size % sizeof(struct futx) != 0) {
			fclose(uf);
			uf = NULL;
			errno = EFTYPE;
			return (-1);
		}
		/* Prevent reading of partial records. */
		(void)setvbuf(uf, NULL, _IOFBF,
		    rounddown(BUFSIZ, sizeof(struct futx)));
	}

	udb = db;
	return (0);
}

void
setutxent(void)
{

	setutxdb(UTXDB_ACTIVE, NULL);
}

void
endutxent(void)
{

	if (uf != NULL) {
		fclose(uf);
		uf = NULL;
	}
}

static int
getfutxent(struct futx *fu)
{

	if (uf == NULL)
		setutxent();
	if (uf == NULL)
		return (-1);

	if (udb == UTXDB_LOG) {
		uint16_t len;

retry:
		if (fread(&len, sizeof(len), 1, uf) != 1)
			return (-1);
		len = be16toh(len);
		if (len == 0) {
			/*
			 * XXX: Though zero-size records are valid in theory,
			 * they can never occur in practice. Zero-size records
			 * indicate file corruption. Seek one byte forward, to
			 * see if we can find a record there.
			 */
			ungetc('\0', uf);
			goto retry;
		}
		if (len > sizeof *fu) {
			/* Forward compatibility. */
			if (fread(fu, sizeof(*fu), 1, uf) != 1)
				return (-1);
			fseek(uf, len - sizeof(*fu), SEEK_CUR);
		} else {
			/* Partial record. */
			memset(fu, 0, sizeof(*fu));
			if (fread(fu, len, 1, uf) != 1)
				return (-1);
		}
	} else {
		if (fread(fu, sizeof(*fu), 1, uf) != 1)
			return (-1);
	}
	return (0);
}

struct utmpx *
getutxent(void)
{
	struct futx fu;

	if (getfutxent(&fu) != 0)
		return (NULL);
	return (futx_to_utx(&fu));
}

struct utmpx *
getutxid(const struct utmpx *id)
{
	struct futx fu;

	for (;;) {
		if (getfutxent(&fu) != 0)
			return (NULL);

		switch (fu.fu_type) {
		case USER_PROCESS:
		case INIT_PROCESS:
		case LOGIN_PROCESS:
		case DEAD_PROCESS:
			switch (id->ut_type) {
			case USER_PROCESS:
			case INIT_PROCESS:
			case LOGIN_PROCESS:
			case DEAD_PROCESS:
				if (memcmp(fu.fu_id, id->ut_id,
				    MIN(sizeof(fu.fu_id), sizeof(id->ut_id))) ==
				    0)
					goto found;
			}
			break;
		default:
			if (fu.fu_type == id->ut_type)
				goto found;
			break;
		}
	}

found:
	return (futx_to_utx(&fu));
}

struct utmpx *
getutxline(const struct utmpx *line)
{
	struct futx fu;

	for (;;) {
		if (getfutxent(&fu) != 0)
			return (NULL);

		switch (fu.fu_type) {
		case USER_PROCESS:
		case LOGIN_PROCESS:
			if (strncmp(fu.fu_line, line->ut_line,
			    MIN(sizeof(fu.fu_line), sizeof(line->ut_line))) ==
			    0)
				goto found;
			break;
		}
	}

found:
	return (futx_to_utx(&fu));
}

struct utmpx *
getutxuser(const char *user)
{
	struct futx fu;

	for (;;) {
		if (getfutxent(&fu) != 0)
			return (NULL);

		switch (fu.fu_type) {
		case USER_PROCESS:
			if (strncmp(fu.fu_user, user, sizeof(fu.fu_user)) == 0)
				goto found;
			break;
		}
	}

found:
	return (futx_to_utx(&fu));
}
