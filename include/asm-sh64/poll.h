#ifndef __ASM_SH64_POLL_H
#define __ASM_SH64_POLL_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/poll.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 *
 */

/* These are specified by iBCS2 */
#define POLLIN		0x0001
#define POLLPRI		0x0002
#define POLLOUT		0x0004
#define POLLERR		0x0008
#define POLLHUP		0x0010
#define POLLNVAL	0x0020

/* The rest seem to be more-or-less nonstandard. Check them! */
#define POLLRDNORM	0x0040
#define POLLRDBAND	0x0080
#define POLLWRNORM	0x0100
#define POLLWRBAND	0x0200
#define POLLMSG		0x0400

struct pollfd {
	int fd;
	short events;
	short revents;
};

#endif /* __ASM_SH64_POLL_H */
