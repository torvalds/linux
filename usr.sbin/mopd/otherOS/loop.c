/*	$OpenBSD: loop.c,v 1.8 2009/10/27 23:59:52 deraadt Exp $ */

/*
 * Copyright (c) 1993-95 Mats O Jansson.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "os.h"
#include "common/common.h"
#include "common/mopdef.h"

/*
 * The list of all interfaces that are being listened to.  loop()
 * "selects" on the descriptors in this list.
 */
struct if_info *iflist;
u_char	buf[BUFSIZE];

void   mopProcess   (/* struct if_info *, u_char * */);

int
mopOpenRC(p, trans)
	struct if_info *p;
	int	trans;
{
#ifndef NORC
	int	fd;

	fd = (*(p->iopen))(p->if_name,
			   O_RDWR,
			   MOP_K_PROTO_RC,
			   trans);
	if (fd >= 0) {
		pfAddMulti(fd, p->if_name, rc_mcst);
		pfEthAddr(fd, p->eaddr);
	}

	return fd;
#else
	return -1;
#endif
}

int
mopOpenDL(p, trans)
	struct if_info *p;
	int	trans;
{
#ifndef NODL
	int	fd;

	fd = (*(p->iopen))(p->if_name,
			   O_RDWR,
			   MOP_K_PROTO_DL,
			   trans);
	if (fd >= 0) {
		pfAddMulti(fd, p->if_name, dl_mcst);
		pfEthAddr(fd, p->eaddr);
	}

	return fd;
#else
	return -1;
#endif
}

void
mopReadRC(p, fd)
	struct if_info *p;
	int	fd;
{
	int cc;

	if ((cc = pfRead(fd, buf+HDRSIZ, BUFSIZE-HDRSIZ)) < 0) {
		return;
	}

	if (cc == 0)
		return;

	mopProcess(p, buf+HDRSIZ);

	return;
}

void
mopReadDL(p, fd)
	struct if_info *p;
	int	fd;
{
	int cc;

	if ((cc = pfRead(fd, buf+HDRSIZ, BUFSIZE-HDRSIZ)) < 0) {
		return;
	}

	if (cc == 0)
		return;

	mopProcess(p, buf+HDRSIZ);

	return;
}

/*
 * Loop indefinitely listening for MOP requests on the
 * interfaces in 'iflist'.
 */
void
Loop()
{
	fd_set  fds, listeners;
	int     maxfd = 0;
	struct if_info *ii;

	if (iflist == 0) {
		fprintf(stderr,"no interfaces");
		exit(0);
	}
	
	/*
         * Find the highest numbered file descriptor for select().
         * Initialize the set of descriptors to listen to.
         */
	FD_ZERO(&fds);
	for (ii = iflist; ii; ii = ii->next) {
		if (ii->fd != -1) {
			FD_SET(ii->fd, &fds);
			if (ii->fd > maxfd)
				maxfd = ii->fd;
	        }
	}
	while (1) {
		listeners = fds;
		if (select(maxfd + 1, &listeners, (fd_set *) 0,
			(fd_set *) 0, (struct timeval *) 0) < 0) {
			fprintf(stderr, "select: %s", strerror(errno));
			exit(0);
		}
		for (ii = iflist; ii; ii = ii->next) {
			if (ii->fd != -1) {
				if (FD_ISSET(ii->fd, &listeners))
					(*(ii->read))(ii,ii->fd);
			}
		}
	}
}





