/*
 * linux/include/linux/lockd/bind.h
 *
 * This is the part of lockd visible to nfsd and the nfs client.
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#ifndef LINUX_LOCKD_BIND_H
#define LINUX_LOCKD_BIND_H

#include <linux/lockd/nlm.h>
/* need xdr-encoded error codes too, so... */
#include <linux/lockd/xdr.h>
#ifdef CONFIG_LOCKD_V4
#include <linux/lockd/xdr4.h>
#endif

/* Dummy declarations */
struct svc_rqst;

/*
 * This is the set of functions for lockd->nfsd communication
 */
struct nlmsvc_binding {
	__be32			(*fopen)(struct svc_rqst *,
						struct nfs_fh *,
						struct file **);
	void			(*fclose)(struct file *);
	unsigned long		(*get_grace_period)(void);
};

extern struct nlmsvc_binding *	nlmsvc_ops;

/*
 * Functions exported by the lockd module
 */
extern int	nlmclnt_proc(struct inode *, int, struct file_lock *);
extern int	lockd_up(int proto);
extern void	lockd_down(void);

unsigned long get_nfs_grace_period(void);

#ifdef CONFIG_NFSD_V4
unsigned long get_nfs4_grace_period(void);
#else
static inline unsigned long get_nfs4_grace_period(void) {return 0;}
#endif

#endif /* LINUX_LOCKD_BIND_H */
