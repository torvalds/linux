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
	u32			(*fopen)(struct svc_rqst *,
						struct nfs_fh *,
						struct file **);
	void			(*fclose)(struct file *);
};

extern struct nlmsvc_binding *	nlmsvc_ops;

/*
 * Functions exported by the lockd module
 */
extern int	nlmclnt_proc(struct inode *, int, struct file_lock *);
extern int	lockd_up(int proto);
extern void	lockd_down(void);

#endif /* LINUX_LOCKD_BIND_H */
