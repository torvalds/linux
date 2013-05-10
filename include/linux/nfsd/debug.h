/*
 * linux/include/linux/nfsd/debug.h
 *
 * Debugging-related stuff for nfsd
 *
 * Copyright (C) 1995 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef LINUX_NFSD_DEBUG_H
#define LINUX_NFSD_DEBUG_H

#include <linux/sunrpc/debug.h>

/*
 * Enable debugging for nfsd.
 * Requires RPC_DEBUG.
 */
#ifdef RPC_DEBUG
# define NFSD_DEBUG		1
#endif

/*
 * knfsd debug flags
 */
#define NFSDDBG_SOCK		0x0001
#define NFSDDBG_FH		0x0002
#define NFSDDBG_EXPORT		0x0004
#define NFSDDBG_SVC		0x0008
#define NFSDDBG_PROC		0x0010
#define NFSDDBG_FILEOP		0x0020
#define NFSDDBG_AUTH		0x0040
#define NFSDDBG_REPCACHE	0x0080
#define NFSDDBG_XDR		0x0100
#define NFSDDBG_LOCKD		0x0200
#define NFSDDBG_ALL		0x7FFF
#define NFSDDBG_NOCHANGE	0xFFFF


#ifdef __KERNEL__
# undef ifdebug
# ifdef NFSD_DEBUG
#  define ifdebug(flag)		if (nfsd_debug & NFSDDBG_##flag)
# else
#  define ifdebug(flag)		if (0)
# endif
#endif /* __KERNEL__ */

#endif /* LINUX_NFSD_DEBUG_H */
