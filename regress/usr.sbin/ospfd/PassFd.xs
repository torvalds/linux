/*	$OpenBSD: PassFd.xs,v 1.2 2014/07/11 22:28:51 bluhm Exp $ */

/*
 * Copyright (c) 2014 Alexander Bluhm <bluhm@openbsd.org>
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

#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include <sys/socket.h>

MODULE = PassFd	PACKAGE = PassFd

SV *
sendfd(PerlIO *so, PerlIO *fh)
    PREINIT:
	int		 s, fd;
	struct msghdr	 msg;
	struct cmsghdr	*cmsg;
	union {
		struct cmsghdr	 hdr;
		unsigned char	 buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;
    CODE:
	s = PerlIO_fileno(so);
	fd = PerlIO_fileno(fh);

	memset(&msg, 0, sizeof(msg));
	msg.msg_control = &cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	*(int *)CMSG_DATA(cmsg) = fd;

	if (sendmsg(s, &msg, 0) == -1)
		XSRETURN_UNDEF;
	RETVAL = &PL_sv_yes;
    OUTPUT:
	RETVAL

PerlIO *
recvfd(PerlIO *so)
    PREINIT:
	PerlIO		*fh = NULL;
	int		 s, fd;
	struct msghdr	 msg;
	struct cmsghdr	*cmsg;
	union {
		struct cmsghdr	 hdr;
		unsigned char	 buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;
    CODE:
	s = PerlIO_fileno(so);

	memset(&msg, 0, sizeof(msg));
	msg.msg_control = &cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);

	if (recvmsg(s, &msg, 0) == -1)
		XSRETURN_UNDEF;
	if ((msg.msg_flags & MSG_TRUNC) || (msg.msg_flags & MSG_CTRUNC)) {
		errno = EMSGSIZE;
		XSRETURN_UNDEF;
	}
	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
	    cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (cmsg->cmsg_len == CMSG_LEN(sizeof(int)) &&
		    cmsg->cmsg_level == SOL_SOCKET &&
		    cmsg->cmsg_type == SCM_RIGHTS) {
			fd = *(int *)CMSG_DATA(cmsg);
			if ((fh = PerlIO_fdopen(fd, "r+")) == NULL)
				XSRETURN_UNDEF;
			RETVAL = fh;
			break;
		}
	}
	if (fh == NULL) {
		errno = ESRCH;
		XSRETURN_UNDEF;
	}
    OUTPUT:
	RETVAL
