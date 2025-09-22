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

#include <errno.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#if defined(__bsdi__) || defined(__FreeBSD__)
#include <sys/time.h>
#endif
#include <sys/ioctl.h>

#include "os.h"
#include "common/common.h"
#include "common/mopdef.h"

int
mopOpenRC(p, trans)
	struct if_info *p;
	int	trans;
{
#ifndef NORC
	return (*(p->iopen))(p->if_name,
			     O_RDWR,
			     MOP_K_PROTO_RC,
			     trans);
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
	return (*(p->iopen))(p->if_name,
			     O_RDWR,
			     MOP_K_PROTO_DL,
			     trans);
#else
	return -1;
#endif
}

void
mopReadRC()
{
}

void
mopReadDL()
{
}

/*
 * The list of all interfaces that are being listened to.  loop()
 * "selects" on the descriptors in this list.
 */
struct if_info *iflist;

void   mopProcess(struct if_info *, u_char *);

/*
 * Loop indefinitely listening for MOP requests on the
 * interfaces in 'iflist'.
 */
void
Loop()
{
	u_char *buf, *bp, *ep;
	int     cc;
	fd_set  fds, listeners;
	int     bufsize = 1100, maxfd =0;
	struct if_info *ii;

/* FIXME : this is a hack, for some reason specifying an interface would 
 * cause it to fail because bufsize is an impossible number, so I added a 
 * sanity check because I'm too lazy to figure out why. -- Karl
 */
	if (bufsize > 1100)
	 	bufsize = 64;

	if (iflist == 0) {
		syslog(LOG_ERR, "no interfaces");
		exit(0);
	}

	 buf = (u_char *) malloc((unsigned) bufsize); 

	if (buf == 0) {
		syslog(LOG_ERR, "malloc: %m");
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
			syslog(LOG_ERR, "select: %m");
			exit(0);
		}
		for (ii = iflist; ii; ii = ii->next) {
			if (ii->fd != -1) {
				if (!FD_ISSET(ii->fd, &listeners))
					continue;
			}
	again:
			cc = read(ii->fd, (char *) buf, bufsize);
			/* Don't choke when we get ptraced */
			if (cc < 0 && errno == EINTR)
				goto again;

			bp = buf;
			ep = bp + cc;
	
if(bp < ep)
	{
	mopProcess(ii,buf);
	}

}

}		
}	

