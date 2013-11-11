/* Copyright (C) 2003 Krzysztof Benedyczak & Michal Wronski

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   It is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this software; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

#ifndef _LINUX_MQUEUE_H
#define _LINUX_MQUEUE_H

#define MQ_PRIO_MAX 	32768
/* per-uid limit of kernel memory used by mqueue, in bytes */
#define MQ_BYTES_MAX	819200

struct mq_attr {
	long	mq_flags;	/* message queue flags			*/
	long	mq_maxmsg;	/* maximum number of messages		*/
	long	mq_msgsize;	/* maximum message size			*/
	long	mq_curmsgs;	/* number of messages currently queued	*/
	long	__reserved[4];	/* ignored for input, zeroed for output */
};

/*
 * SIGEV_THREAD implementation:
 * SIGEV_THREAD must be implemented in user space. If SIGEV_THREAD is passed
 * to mq_notify, then
 * - sigev_signo must be the file descriptor of an AF_NETLINK socket. It's not
 *   necessary that the socket is bound.
 * - sigev_value.sival_ptr must point to a cookie that is NOTIFY_COOKIE_LEN
 *   bytes long.
 * If the notification is triggered, then the cookie is sent to the netlink
 * socket. The last byte of the cookie is replaced with the NOTIFY_?? codes:
 * NOTIFY_WOKENUP if the notification got triggered, NOTIFY_REMOVED if it was
 * removed, either due to a close() on the message queue fd or due to a
 * mq_notify() that removed the notification.
 */
#define NOTIFY_NONE	0
#define NOTIFY_WOKENUP	1
#define NOTIFY_REMOVED	2

#define NOTIFY_COOKIE_LEN	32

#endif
