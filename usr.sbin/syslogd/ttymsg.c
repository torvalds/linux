/*	$OpenBSD: ttymsg.c,v 1.19 2021/09/03 16:28:33 bluhm Exp $	*/
/*	$NetBSD: ttymsg.c,v 1.3 1994/11/17 07:17:55 jtc Exp $	*/

/*
 * Copyright (c) 2014-2017 Alexander Bluhm <bluhm@genua.de>
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

/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/stat.h>
#include <sys/syslog.h>

#include <dirent.h>
#include <errno.h>
#include <event.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "syslogd.h"

struct tty_delay {
	struct event	 td_event;
	size_t		 td_length;
	char		 td_line[LOG_MAXLINE];
};
int tty_delayed = 0;
void ttycb(int, short, void *);

/*
 * Display the contents of a uio structure on a terminal.
 * Schedules an event if write would block, waiting up to TTYMSGTIME
 * seconds.
 */
void
ttymsg(char *utline, struct iovec *iov)
{
	static char device[MAXNAMLEN] = _PATH_DEV;
	struct iovec localiov[IOVCNT];
	int iovcnt = IOVCNT;
	int cnt, fd;
	size_t left;
	ssize_t wret;

	/*
	 * Ignore lines that start with "ftp" or "uucp".
	 */
	if ((strncmp(utline, "ftp", 3) == 0) ||
	    (strncmp(utline, "uucp", 4) == 0))
		return;

	(void) strlcpy(device + sizeof(_PATH_DEV) - 1, utline,
	    sizeof(device) - (sizeof(_PATH_DEV) - 1));
	if (strchr(device + sizeof(_PATH_DEV) - 1, '/')) {
		/* A slash is an attempt to break security... */
		log_warnx("'/' in tty device \"%s\"", device);
		return;
	}

	/*
	 * open will fail on slip lines or exclusive-use lines
	 * if not running as root; not an error.
	 */
	if ((fd = priv_open_tty(device)) == -1) {
		if (errno != EBUSY && errno != EACCES)
			log_warn("priv_open_tty device \"%s\"", device);
		return;
	}

	left = 0;
	for (cnt = 0; cnt < iovcnt; ++cnt)
		left += iov[cnt].iov_len;

	for (;;) {
		wret = writev(fd, iov, iovcnt);
		if (wret >= 0) {
			if ((size_t)wret >= left)
				break;
			left -= wret;
			if (iov != localiov) {
				memmove(localiov, iov,
				    iovcnt * sizeof(struct iovec));
				iov = localiov;
			}
			while ((size_t)wret >= iov->iov_len) {
				wret -= iov->iov_len;
				++iov;
				--iovcnt;
			}
			if (wret) {
				iov->iov_base = (char *)iov->iov_base + wret;
				iov->iov_len -= wret;
			}
			continue;
		}
		if (errno == EWOULDBLOCK) {
			struct tty_delay	*td;
			struct timeval		 to;

			if (tty_delayed >= TTYMAXDELAY) {
				log_warnx("tty device \"%s\": %s",
				    device, "too many delayed writes");
				break;
			}
			log_debug("ttymsg delayed write");
			if (iov != localiov) {
				memmove(localiov, iov,
				    iovcnt * sizeof(struct iovec));
				iov = localiov;
			}
			if ((td = malloc(sizeof(*td))) == NULL) {
				log_warn("allocate delay tty device \"%s\"",
				    device);
				break;
			}
			td->td_length = 0;
			if (left > LOG_MAXLINE)
				left = LOG_MAXLINE;
			while (iovcnt && left) {
				if (iov->iov_len > left)
					iov->iov_len = left;
				memcpy(td->td_line + td->td_length,
				    iov->iov_base, iov->iov_len);
				td->td_length += iov->iov_len;
				left -= iov->iov_len;
				++iov;
				--iovcnt;
			}
			tty_delayed++;
			event_set(&td->td_event, fd, EV_WRITE, ttycb, td);
			to.tv_sec = TTYMSGTIME;
			to.tv_usec = 0;
			event_add(&td->td_event, &to);
			return;
		}
		/*
		 * We get ENODEV on a slip line if we're running as root,
		 * and EIO if the line just went away.
		 */
		if (errno != ENODEV && errno != EIO)
			log_warn("writev tty device \"%s\"", device);
		break;
	}

	(void) close(fd);
	return;
}

void
ttycb(int fd, short event, void *arg)
{
	struct tty_delay	*td = arg;
	struct timeval		 to;
	ssize_t			 wret;

	if (event != EV_WRITE)
		goto done;

	wret = write(fd, td->td_line, td->td_length);
	if (wret == -1 && errno != EINTR && errno != EWOULDBLOCK)
		goto done;
	if (wret > 0) {
		td->td_length -= wret;
		if (td->td_length == 0)
			goto done;
		memmove(td->td_line, td->td_line + wret, td->td_length);
	}
	to.tv_sec = TTYMSGTIME;
	to.tv_usec = 0;
	event_add(&td->td_event, &to);
	return;

 done:
	tty_delayed--;
	close(fd);
	free(td);
}
