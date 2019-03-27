/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Kai Wang
 * Copyright (c) 2007 Tim Kientzle
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

#include <sys/queue.h>
#include <sys/stat.h>
#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>

#include "ar.h"

static void read_archive(struct bsdar *bsdar, char mode);

void
ar_mode_p(struct bsdar *bsdar)
{

	read_archive(bsdar, 'p');
}

void
ar_mode_t(struct bsdar *bsdar)
{

	read_archive(bsdar, 't');
}

void
ar_mode_x(struct bsdar *bsdar)
{

	read_archive(bsdar, 'x');
}

/*
 * Handle read modes: 'x', 't' and 'p'.
 */
static void
read_archive(struct bsdar *bsdar, char mode)
{
	struct archive		 *a;
	struct archive_entry	 *entry;
	struct stat		  sb;
	struct tm		 *tp;
	const char		 *bname;
	const char		 *name;
	mode_t			  md;
	size_t			  size;
	time_t			  mtime;
	uid_t			  uid;
	gid_t			  gid;
	char			**av;
	char			  buf[25];
	char			  find;
	int			  flags, r, i;

	if ((a = archive_read_new()) == NULL)
		bsdar_errc(bsdar, EX_SOFTWARE, 0, "archive_read_new failed");
	archive_read_support_format_ar(a);
	AC(archive_read_open_filename(a, bsdar->filename, DEF_BLKSZ));

	for (;;) {
		r = archive_read_next_header(a, &entry);
		if (r == ARCHIVE_WARN || r == ARCHIVE_RETRY ||
		    r == ARCHIVE_FATAL)
			bsdar_warnc(bsdar, archive_errno(a), "%s",
			    archive_error_string(a));
		if (r == ARCHIVE_EOF || r == ARCHIVE_FATAL)
			break;
		if (r == ARCHIVE_RETRY) {
			bsdar_warnc(bsdar, 0, "Retrying...");
			continue;
		}

		if ((name = archive_entry_pathname(entry)) == NULL)
			break;

		/* Skip pseudo members. */
		if (strcmp(name, "/") == 0 || strcmp(name, "//") == 0)
			continue;

		if (bsdar->argc > 0) {
			find = 0;
			for(i = 0; i < bsdar->argc; i++) {
				av = &bsdar->argv[i];
				if (*av == NULL)
					continue;
				if ((bname = basename(*av)) == NULL)
					bsdar_errc(bsdar, EX_SOFTWARE, errno,
					    "basename failed");
				if (strcmp(bname, name) != 0)
					continue;

				*av = NULL;
				find = 1;
				break;
			}
			if (!find)
				continue;
		}

		if (mode == 't') {
			if (bsdar->options & AR_V) {
				md = archive_entry_mode(entry);
				uid = archive_entry_uid(entry);
				gid = archive_entry_gid(entry);
				size = archive_entry_size(entry);
				mtime = archive_entry_mtime(entry);
				(void)strmode(md, buf);
				(void)fprintf(stdout, "%s %6d/%-6d %8ju ",
				    buf + 1, uid, gid, (uintmax_t)size);
				tp = localtime(&mtime);
				(void)strftime(buf, sizeof(buf),
				    "%b %e %H:%M %Y", tp);
				(void)fprintf(stdout, "%s %s", buf, name);
			} else
				(void)fprintf(stdout, "%s", name);
			r = archive_read_data_skip(a);
			if (r == ARCHIVE_WARN || r == ARCHIVE_RETRY ||
			    r == ARCHIVE_FATAL) {
				(void)fprintf(stdout, "\n");
				bsdar_warnc(bsdar, archive_errno(a), "%s",
				    archive_error_string(a));
			}

			if (r == ARCHIVE_FATAL)
				break;

			(void)fprintf(stdout, "\n");
		} else {
			/* mode == 'x' || mode = 'p' */
			if (mode == 'p') {
				if (bsdar->options & AR_V) {
					(void)fprintf(stdout, "\n<%s>\n\n",
					    name);
					fflush(stdout);
				}
				r = archive_read_data_into_fd(a, 1);
			} else {
				/* mode == 'x' */
				if (stat(name, &sb) != 0) {
					if (errno != ENOENT) {
						bsdar_warnc(bsdar, 0,
						    "stat %s failed",
						    bsdar->filename);
						continue;
					}
				} else {
					/* stat success, file exist */
					if (bsdar->options & AR_CC)
						continue;
					if (bsdar->options & AR_U &&
					    archive_entry_mtime(entry) <=
					    sb.st_mtime)
						continue;
				}

				if (bsdar->options & AR_V)
					(void)fprintf(stdout, "x - %s\n", name);
				/* Disallow absolute paths. */
				if (name[0] == '/') {
					bsdar_warnc(bsdar, 0,
					    "Absolute path '%s'", name);
					continue;
				}
				/* Basic path security flags. */
				flags = ARCHIVE_EXTRACT_SECURE_SYMLINKS |
				    ARCHIVE_EXTRACT_SECURE_NODOTDOT;
				if (bsdar->options & AR_O)
					flags |= ARCHIVE_EXTRACT_TIME;

				r = archive_read_extract(a, entry, flags);
			}

			if (r)
				bsdar_warnc(bsdar, archive_errno(a), "%s",
				    archive_error_string(a));
		}
	}
	AC(archive_read_close(a));
	AC(archive_read_free(a));
}
