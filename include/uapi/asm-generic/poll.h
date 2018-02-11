/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __ASM_GENERIC_POLL_H
#define __ASM_GENERIC_POLL_H

/* These are specified by iBCS2 */
#define POLLIN		(__force __poll_t)0x0001
#define POLLPRI		(__force __poll_t)0x0002
#define POLLOUT		(__force __poll_t)0x0004
#define POLLERR		(__force __poll_t)0x0008
#define POLLHUP		(__force __poll_t)0x0010
#define POLLNVAL	(__force __poll_t)0x0020

/* The rest seem to be more-or-less nonstandard. Check them! */
#define POLLRDNORM	(__force __poll_t)0x0040
#define POLLRDBAND	(__force __poll_t)0x0080
#ifndef POLLWRNORM
#define POLLWRNORM	(__force __poll_t)0x0100
#endif
#ifndef POLLWRBAND
#define POLLWRBAND	(__force __poll_t)0x0200
#endif
#ifndef POLLMSG
#define POLLMSG		(__force __poll_t)0x0400
#endif
#ifndef POLLREMOVE
#define POLLREMOVE	(__force __poll_t)0x1000
#endif
#ifndef POLLRDHUP
#define POLLRDHUP       (__force __poll_t)0x2000
#endif

#define POLLFREE	(__force __poll_t)0x4000	/* currently only for epoll */

#define POLL_BUSY_LOOP	(__force __poll_t)0x8000

#ifdef __KERNEL__
#ifndef __ARCH_HAS_MANGLED_POLL
static inline __u16 mangle_poll(__poll_t val)
{
	return (__force __u16)val;
}

static inline __poll_t demangle_poll(__u16 v)
{
	return (__force __poll_t)v;
}
#endif
#endif

struct pollfd {
	int fd;
	short events;
	short revents;
};

#endif	/* __ASM_GENERIC_POLL_H */
