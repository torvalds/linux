/*
 * linux/include/linux/sunrpc/stats.h
 *
 * Client statistics collection for SUN RPC
 *
 * Copyright (C) 1996 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LINUX_SUNRPC_STATS_H
#define _LINUX_SUNRPC_STATS_H

#include <linux/config.h>
#include <linux/proc_fs.h>

struct rpc_stat {
	struct rpc_program *	program;

	unsigned int		netcnt,
				netudpcnt,
				nettcpcnt,
				nettcpconn,
				netreconn;
	unsigned int		rpccnt,
				rpcretrans,
				rpcauthrefresh,
				rpcgarbage;
};

struct svc_stat {
	struct svc_program *	program;

	unsigned int		netcnt,
				netudpcnt,
				nettcpcnt,
				nettcpconn;
	unsigned int		rpccnt,
				rpcbadfmt,
				rpcbadauth,
				rpcbadclnt;
};

void			rpc_proc_init(void);
void			rpc_proc_exit(void);
#ifdef MODULE
void			rpc_modcount(struct inode *, int);
#endif

#ifdef CONFIG_PROC_FS
struct proc_dir_entry *	rpc_proc_register(struct rpc_stat *);
void			rpc_proc_unregister(const char *);
void			rpc_proc_zero(struct rpc_program *);
struct proc_dir_entry *	svc_proc_register(struct svc_stat *,
					  const struct file_operations *);
void			svc_proc_unregister(const char *);

void			svc_seq_show(struct seq_file *,
				     const struct svc_stat *);

extern struct proc_dir_entry	*proc_net_rpc;

#else

static inline struct proc_dir_entry *rpc_proc_register(struct rpc_stat *s) { return NULL; }
static inline void rpc_proc_unregister(const char *p) {}
static inline void rpc_proc_zero(struct rpc_program *p) {}

static inline struct proc_dir_entry *svc_proc_register(struct svc_stat *s,
						       const struct file_operations *f) { return NULL; }
static inline void svc_proc_unregister(const char *p) {}

static inline void svc_seq_show(struct seq_file *seq,
				const struct svc_stat *st) {}

#define proc_net_rpc NULL

#endif

#endif /* _LINUX_SUNRPC_STATS_H */
