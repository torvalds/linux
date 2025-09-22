/*	$OpenBSD: mda_mbox.c,v 1.3 2021/06/14 17:58:15 eric Exp $	*/

/*
 * Copyright (c) 2018 Gilles Chehade <gilles@poolp.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <unistd.h>

#include "smtpd.h"

void
mda_mbox(struct deliver *deliver)
{
	int		ret;
	char		sender[LINE_MAX];
	char		*envp[] = {
		"HOME=/",
		"PATH=" _PATH_DEFPATH,
		"LOGNAME=root",
		"USER=root",
		NULL,
	};

	if (deliver->sender.user[0] == '\0' &&
	    deliver->sender.domain[0] == '\0')
		ret = snprintf(sender, sizeof sender, "MAILER-DAEMON");
	else
		ret = snprintf(sender, sizeof sender, "%s@%s",
			       deliver->sender.user, deliver->sender.domain);
	if (ret < 0 || (size_t)ret >= sizeof sender)
		errx(EX_TEMPFAIL, "sender address too long");

	execle(PATH_MAILLOCAL, PATH_MAILLOCAL, "-f",
	       sender, deliver->userinfo.username, (char *)NULL, envp);
	perror("execl");
	_exit(EX_TEMPFAIL);
}

void
mda_mbox_init(struct deliver *deliver)
{
	int	fd;
	int	ret;
	char	buffer[LINE_MAX];

	ret = snprintf(buffer, sizeof buffer, "%s/%s",
	    _PATH_MAILDIR, deliver->userinfo.username);
	if (ret < 0 || (size_t)ret >= sizeof buffer)
		errx(EX_TEMPFAIL, "mailbox pathname too long");

	if ((fd = open(buffer, O_CREAT|O_EXCL, 0)) == -1) {
		if (errno == EEXIST)
			return;
		err(EX_TEMPFAIL, "open");
	}

	if (fchown(fd, deliver->userinfo.uid, deliver->userinfo.gid) == -1)
		err(EX_TEMPFAIL, "fchown");

	if (fchmod(fd, S_IRUSR|S_IWUSR) == -1)
		err(EX_TEMPFAIL, "fchown");
}
