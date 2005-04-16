/*
 * linux/include/linux/lockd/debug.h
 *
 * Debugging stuff.
 *
 * Copyright (C) 1996 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef LINUX_LOCKD_DEBUG_H
#define LINUX_LOCKD_DEBUG_H

#ifdef __KERNEL__

#include <linux/sunrpc/debug.h>

/*
 * Enable lockd debugging.
 * Requires RPC_DEBUG.
 */
#ifdef RPC_DEBUG
# define LOCKD_DEBUG		1
#endif

#undef ifdebug
#if defined(RPC_DEBUG) && defined(LOCKD_DEBUG)
# define ifdebug(flag)		if (unlikely(nlm_debug & NLMDBG_##flag))
#else
# define ifdebug(flag)		if (0)
#endif

#endif /* __KERNEL__ */

/*
 * Debug flags
 */
#define NLMDBG_SVC		0x0001
#define NLMDBG_CLIENT		0x0002
#define NLMDBG_CLNTLOCK		0x0004
#define NLMDBG_SVCLOCK		0x0008
#define NLMDBG_MONITOR		0x0010
#define NLMDBG_CLNTSUBS		0x0020
#define NLMDBG_SVCSUBS		0x0040
#define NLMDBG_HOSTCACHE	0x0080
#define NLMDBG_XDR		0x0100
#define NLMDBG_ALL		0x7fff


/*
 * Support for printing NLM cookies in dprintk()
 */
#ifdef RPC_DEBUG
struct nlm_cookie;
/* Call this function with the BKL held (it uses a static buffer) */
extern const char *nlmdbg_cookie2a(const struct nlm_cookie *);
#endif

#endif /* LINUX_LOCKD_DEBUG_H */
