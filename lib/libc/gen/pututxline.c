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
#include <sys/stat.h>
#include <sys/uio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <utmpx.h>
#include "utxdb.h"
#include "un-namespace.h"

static FILE *
futx_open(const char *file)
{
	FILE *fp;
	struct stat sb;
	int fd;

	fd = _open(file, O_CREAT|O_RDWR|O_EXLOCK|O_CLOEXEC, 0644);
	if (fd < 0)
		return (NULL);

	/* Safety check: never use broken files. */
	if (_fstat(fd, &sb) != -1 && sb.st_size % sizeof(struct futx) != 0) {
		_close(fd);
		errno = EFTYPE;
		return (NULL);
	}

	fp = fdopen(fd, "r+");
	if (fp == NULL) {
		_close(fd);
		return (NULL);
	}
	return (fp);
}

static int
utx_active_add(const struct futx *fu)
{
	FILE *fp;
	struct futx fe;
	off_t partial;
	int error, ret;

	partial = -1;
	ret = 0;

	/*
	 * Register user login sessions.  Overwrite entries of sessions
	 * that have already been terminated.
	 */
	fp = futx_open(_PATH_UTX_ACTIVE);
	if (fp == NULL)
		return (-1);
	while (fread(&fe, sizeof(fe), 1, fp) == 1) {
		switch (fe.fu_type) {
		case BOOT_TIME:
			/* Leave these intact. */
			break;
		case USER_PROCESS:
		case INIT_PROCESS:
		case LOGIN_PROCESS:
		case DEAD_PROCESS:
			/* Overwrite when ut_id matches. */
			if (memcmp(fu->fu_id, fe.fu_id, sizeof(fe.fu_id)) ==
			    0) {
				ret = fseeko(fp, -(off_t)sizeof(fe), SEEK_CUR);
				goto exact;
			}
			if (fe.fu_type != DEAD_PROCESS)
				break;
			/* FALLTHROUGH */
		default:
			/* Allow us to overwrite unused records. */
			if (partial == -1) {
				partial = ftello(fp);
				/*
				 * Distinguish errors from valid values so we
				 * don't overwrite good data by accident.
				 */
				if (partial != -1)
					partial -= (off_t)sizeof(fe);
			}
			break;
		}
	}

	/*
	 * No exact match found.  Use the partial match.  If no partial
	 * match was found, just append a new record.
	 */
	if (partial != -1)
		ret = fseeko(fp, partial, SEEK_SET);
exact:
	if (ret == -1)
		error = errno;
	else if (fwrite(fu, sizeof(*fu), 1, fp) < 1)
		error = errno;
	else
		error = 0;
	fclose(fp);
	if (error != 0)
		errno = error;
	return (error == 0 ? 0 : 1);
}

static int
utx_active_remove(struct futx *fu)
{
	FILE *fp;
	struct futx fe;
	int error, ret;

	/*
	 * Remove user login sessions, having the same ut_id.
	 */
	fp = futx_open(_PATH_UTX_ACTIVE);
	if (fp == NULL)
		return (-1);
	error = ESRCH;
	ret = -1;
	while (fread(&fe, sizeof(fe), 1, fp) == 1 && ret != 0)
		switch (fe.fu_type) {
		case USER_PROCESS:
		case INIT_PROCESS:
		case LOGIN_PROCESS:
			if (memcmp(fu->fu_id, fe.fu_id, sizeof(fe.fu_id)) != 0)
				continue;

			/* Terminate session. */
			if (fseeko(fp, -(off_t)sizeof(fe), SEEK_CUR) == -1)
				error = errno;
			else if (fwrite(fu, sizeof(*fu), 1, fp) < 1)
				error = errno;
			else
				ret = 0;

		}

	fclose(fp);
	if (ret != 0)
		errno = error;
	return (ret);
}

static void
utx_active_init(const struct futx *fu)
{
	int fd;

	/* Initialize utx.active with a single BOOT_TIME record. */
	fd = _open(_PATH_UTX_ACTIVE, O_CREAT|O_RDWR|O_TRUNC, 0644);
	if (fd < 0)
		return;
	_write(fd, fu, sizeof(*fu));
	_close(fd);
}

static void
utx_active_purge(void)
{

	truncate(_PATH_UTX_ACTIVE, 0);
}

static int
utx_lastlogin_add(const struct futx *fu)
{
	struct futx fe;
	FILE *fp;
	int error, ret;

	ret = 0;

	/*
	 * Write an entry to lastlogin.  Overwrite the entry if the
	 * current user already has an entry.  If not, append a new
	 * entry.
	 */
	fp = futx_open(_PATH_UTX_LASTLOGIN);
	if (fp == NULL)
		return (-1);
	while (fread(&fe, sizeof fe, 1, fp) == 1) {
		if (strncmp(fu->fu_user, fe.fu_user, sizeof fe.fu_user) != 0)
			continue;

		/* Found a previous lastlogin entry for this user. */
		ret = fseeko(fp, -(off_t)sizeof fe, SEEK_CUR);
		break;
	}
	if (ret == -1)
		error = errno;
	else if (fwrite(fu, sizeof *fu, 1, fp) < 1) {
		error = errno;
		ret = -1;
	}
	fclose(fp);
	if (ret == -1)
		errno = error;
	return (ret);
}

static void
utx_lastlogin_upgrade(void)
{
	struct stat sb;
	int fd;

	fd = _open(_PATH_UTX_LASTLOGIN, O_RDWR|O_CLOEXEC, 0644);
	if (fd < 0)
		return;

	/*
	 * Truncate broken lastlogin files.  In the future we should
	 * check for older versions of the file format here and try to
	 * upgrade it.
	 */
	if (_fstat(fd, &sb) != -1 && sb.st_size % sizeof(struct futx) != 0)
		ftruncate(fd, 0);
	_close(fd);
}

static int
utx_log_add(const struct futx *fu)
{
	struct iovec vec[2];
	int error, fd;
	uint16_t l;

	/*
	 * Append an entry to the log file.  We only need to append
	 * records to this file, so to conserve space, trim any trailing
	 * zero-bytes.  Prepend a length field, indicating the length of
	 * the record, excluding the length field itself.
	 */
	for (l = sizeof(*fu); l > 0 && ((const char *)fu)[l - 1] == '\0'; l--) ;
	vec[0].iov_base = &l;
	vec[0].iov_len = sizeof(l);
	vec[1].iov_base = __DECONST(void *, fu);
	vec[1].iov_len = l;
	l = htobe16(l);

	fd = _open(_PATH_UTX_LOG, O_CREAT|O_WRONLY|O_APPEND|O_CLOEXEC, 0644);
	if (fd < 0)
		return (-1);
	if (_writev(fd, vec, 2) == -1)
		error = errno;
	else
		error = 0;
	_close(fd);
	if (error != 0)
		errno = error;
	return (error == 0 ? 0 : 1);
}

struct utmpx *
pututxline(const struct utmpx *utmpx)
{
	struct futx fu;
	int bad;

	bad = 0;

	utx_to_futx(utmpx, &fu);

	switch (fu.fu_type) {
	case BOOT_TIME:
		utx_active_init(&fu);
		utx_lastlogin_upgrade();
		break;
	case SHUTDOWN_TIME:
		utx_active_purge();
		break;
	case OLD_TIME:
	case NEW_TIME:
		break;
	case USER_PROCESS:
		bad |= utx_active_add(&fu);
		bad |= utx_lastlogin_add(&fu);
		break;
#if 0 /* XXX: Are these records of any use to us? */
	case INIT_PROCESS:
	case LOGIN_PROCESS:
		bad |= utx_active_add(&fu);
		break;
#endif
	case DEAD_PROCESS:
		/*
		 * In case writing a logout entry fails, never attempt
		 * to write it to utx.log.  The logout entry's ut_id
		 * might be invalid.
		 */
		if (utx_active_remove(&fu) != 0)
			return (NULL);
		break;
	default:
		errno = EINVAL;
		return (NULL);
	}

	bad |= utx_log_add(&fu);
	return (bad ? NULL : futx_to_utx(&fu));
}
