/*	$OpenBSD: ntp_msg.c,v 1.22 2016/09/03 11:52:06 reyk Exp $ */

/*
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2004 Alexander Guy <alexander.guy@andern.org>
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

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "ntpd.h"

int
ntp_getmsg(struct sockaddr *sa, char *p, ssize_t len, struct ntp_msg *msg)
{
	if (len != NTP_MSGSIZE_NOAUTH && len != NTP_MSGSIZE) {
		log_debug("malformed packet received from %s",
		    log_sockaddr(sa));
		return (-1);
	}

	memcpy(msg, p, sizeof(*msg));

	return (0);
}

int
ntp_sendmsg(int fd, struct sockaddr *sa, struct ntp_msg *msg)
{
	socklen_t	sa_len;
	ssize_t		n;

	if (sa != NULL)
		sa_len = SA_LEN(sa);
	else
		sa_len = 0;

	n = sendto(fd, msg, sizeof(*msg), 0, sa, sa_len);
	if (n == -1) {
		if (errno == ENOBUFS || errno == EHOSTUNREACH ||
		    errno == ENETDOWN || errno == EHOSTDOWN) {
			/* logging is futile */
			return (-1);
		}
		log_warn("sendto");
		return (-1);
	}

	if (n != sizeof(*msg)) {
		log_warnx("ntp_sendmsg: only %zd of %zu bytes sent", n,
		    sizeof(*msg));
		return (-1);
	}

	return (0);
}
