/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/include/linux/sunrpc/de.h
 *
 * Deging support for sunrpc module
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */
#ifndef _LINUX_SUNRPC_DE_H_
#define _LINUX_SUNRPC_DE_H_

#include <uapi/linux/sunrpc/de.h>

/*
 * Deging macros etc
 */
#if IS_ENABLED(CONFIG_SUNRPC_DE)
extern unsigned int		rpc_de;
extern unsigned int		nfs_de;
extern unsigned int		nfsd_de;
extern unsigned int		nlm_de;
#endif

#define dprintk(fmt, ...)						\
	dfprintk(FACILITY, fmt, ##__VA_ARGS__)
#define dprintk_cont(fmt, ...)						\
	dfprintk_cont(FACILITY, fmt, ##__VA_ARGS__)
#define dprintk_rcu(fmt, ...)						\
	dfprintk_rcu(FACILITY, fmt, ##__VA_ARGS__)
#define dprintk_rcu_cont(fmt, ...)					\
	dfprintk_rcu_cont(FACILITY, fmt, ##__VA_ARGS__)

#undef ifde
#if IS_ENABLED(CONFIG_SUNRPC_DE)
# define ifde(fac)		if (unlikely(rpc_de & RPCDBG_##fac))

# define dfprintk(fac, fmt, ...)					\
do {									\
	ifde(fac)							\
		printk(KERN_DEFAULT fmt, ##__VA_ARGS__);		\
} while (0)

# define dfprintk_cont(fac, fmt, ...)					\
do {									\
	ifde(fac)							\
		printk(KERN_CONT fmt, ##__VA_ARGS__);			\
} while (0)

# define dfprintk_rcu(fac, fmt, ...)					\
do {									\
	ifde(fac) {							\
		rcu_read_lock();					\
		printk(KERN_DEFAULT fmt, ##__VA_ARGS__);		\
		rcu_read_unlock();					\
	}								\
} while (0)

# define dfprintk_rcu_cont(fac, fmt, ...)				\
do {									\
	ifde(fac) {							\
		rcu_read_lock();					\
		printk(KERN_CONT fmt, ##__VA_ARGS__);			\
		rcu_read_unlock();					\
	}								\
} while (0)

# define RPC_IFDE(x)		x
#else
# define ifde(fac)		if (0)
# define dfprintk(fac, fmt, ...)	do {} while (0)
# define dfprintk_cont(fac, fmt, ...)	do {} while (0)
# define dfprintk_rcu(fac, fmt, ...)	do {} while (0)
# define RPC_IFDE(x)
#endif

/*
 * Sysctl interface for RPC deging
 */

struct rpc_clnt;
struct rpc_xprt;

#if IS_ENABLED(CONFIG_SUNRPC_DE)
void		rpc_register_sysctl(void);
void		rpc_unregister_sysctl(void);
void		sunrpc_defs_init(void);
void		sunrpc_defs_exit(void);
void		rpc_clnt_defs_register(struct rpc_clnt *);
void		rpc_clnt_defs_unregister(struct rpc_clnt *);
void		rpc_xprt_defs_register(struct rpc_xprt *);
void		rpc_xprt_defs_unregister(struct rpc_xprt *);
#else
static inline void
sunrpc_defs_init(void)
{
	return;
}

static inline void
sunrpc_defs_exit(void)
{
	return;
}

static inline void
rpc_clnt_defs_register(struct rpc_clnt *clnt)
{
	return;
}

static inline void
rpc_clnt_defs_unregister(struct rpc_clnt *clnt)
{
	return;
}

static inline void
rpc_xprt_defs_register(struct rpc_xprt *xprt)
{
	return;
}

static inline void
rpc_xprt_defs_unregister(struct rpc_xprt *xprt)
{
	return;
}
#endif

#endif /* _LINUX_SUNRPC_DE_H_ */
