/*
 * linux/include/linux/sunrpc/debug.h
 *
 * Debugging support for sunrpc module
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LINUX_SUNRPC_DEBUG_H_
#define _LINUX_SUNRPC_DEBUG_H_

#include <linux/config.h>

#include <linux/timer.h>
#include <linux/workqueue.h>

/*
 * Enable RPC debugging/profiling.
 */
#ifdef CONFIG_SYSCTL
#define  RPC_DEBUG
#endif
/* #define  RPC_PROFILE */

/*
 * RPC debug facilities
 */
#define RPCDBG_XPRT		0x0001
#define RPCDBG_CALL		0x0002
#define RPCDBG_DEBUG		0x0004
#define RPCDBG_NFS		0x0008
#define RPCDBG_AUTH		0x0010
#define RPCDBG_PMAP		0x0020
#define RPCDBG_SCHED		0x0040
#define RPCDBG_TRANS		0x0080
#define RPCDBG_SVCSOCK		0x0100
#define RPCDBG_SVCDSP		0x0200
#define RPCDBG_MISC		0x0400
#define RPCDBG_CACHE		0x0800
#define RPCDBG_ALL		0x7fff

#ifdef __KERNEL__

/*
 * Debugging macros etc
 */
#ifdef RPC_DEBUG
extern unsigned int		rpc_debug;
extern unsigned int		nfs_debug;
extern unsigned int		nfsd_debug;
extern unsigned int		nlm_debug;
#endif

#define dprintk(args...)	dfprintk(FACILITY, ## args)

#undef ifdebug
#ifdef RPC_DEBUG			
# define ifdebug(fac)		if (unlikely(rpc_debug & RPCDBG_##fac))
# define dfprintk(fac, args...)	do { ifdebug(fac) printk(args); } while(0)
# define RPC_IFDEBUG(x)		x
#else
# define ifdebug(fac)		if (0)
# define dfprintk(fac, args...)	do ; while (0)
# define RPC_IFDEBUG(x)
#endif

#ifdef RPC_PROFILE
# define pprintk(args...)	printk(## args)
#else
# define pprintk(args...)	do ; while (0)
#endif

/*
 * Sysctl interface for RPC debugging
 */
#ifdef RPC_DEBUG
void		rpc_register_sysctl(void);
void		rpc_unregister_sysctl(void);
#endif

#endif /* __KERNEL__ */

/*
 * Declarations for the sysctl debug interface, which allows to read or
 * change the debug flags for rpc, nfs, nfsd, and lockd. Since the sunrpc
 * module currently registers its sysctl table dynamically, the sysctl path
 * for module FOO is <CTL_SUNRPC, CTL_FOODEBUG>.
 */
#define CTL_SUNRPC	7249	/* arbitrary and hopefully unused */

enum {
	CTL_RPCDEBUG = 1,
	CTL_NFSDEBUG,
	CTL_NFSDDEBUG,
	CTL_NLMDEBUG,
	CTL_SLOTTABLE_UDP,
	CTL_SLOTTABLE_TCP,
	CTL_MIN_RESVPORT,
	CTL_MAX_RESVPORT,
};

#endif /* _LINUX_SUNRPC_DEBUG_H_ */
