/*
 * include/asm-xtensa/poll.h
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_POLL_H
#define _XTENSA_POLL_H


#define POLLIN		0x0001
#define POLLPRI		0x0002
#define POLLOUT		0x0004

#define POLLERR		0x0008
#define POLLHUP		0x0010
#define POLLNVAL	0x0020

#define POLLRDNORM	0x0040
#define POLLRDBAND	0x0080
#define POLLWRNORM	POLLOUT
#define POLLWRBAND	0x0100

#define POLLMSG		0x0400
#define POLLREMOVE	0x0800

struct pollfd {
	int fd;
	short events;
	short revents;
};

#endif /* _XTENSA_POLL_H */
