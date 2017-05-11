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
struct rpc_task;

/*
 * This is the set of functions for lockd->nfsd communication
 */
struct nlmsvc_binding {
	__be32			(*fopen)(struct svc_rqst *,
						struct nfs_fh *,
						struct file **);
	void			(*fclose)(struct file *);
};

extern const struct nlmsvc_binding *nlmsvc_ops;

/*
 * Similar to nfs_client_initdata, but without the NFS-specific
 * rpc_ops field.
 */
struct nlmclnt_initdata {
	const char		*hostname;
	const struct sockaddr	*address;
	size_t			addrlen;
	unsigned short		protocol;
	u32			nfs_version;
	int			noresvport;
	struct net		*net;
	const struct nlmclnt_operations	*nlmclnt_ops;
};

/*
 * Functions exported by the lockd module
 */

extern struct nlm_host *nlmclnt_init(const struct nlmclnt_initdata *nlm_init);
extern void	nlmclnt_done(struct nlm_host *host);

/*
 * NLM client operations provide a means to modify RPC processing of NLM
 * requests.  Callbacks receive a pointer to data passed into the call to
 * nlmclnt_proc().
 */
struct nlmclnt_operations {
	/* Called on successful allocation of nlm_rqst, use for allocation or
	 * reference counting. */
	void (*nlmclnt_alloc_call)(void *);

	/* Called in rpc_task_prepare for unlock.  A return value of true
	 * indicates the callback has put the task to sleep on a waitqueue
	 * and NLM should not call rpc_call_start(). */
	bool (*nlmclnt_unlock_prepare)(struct rpc_task*, void *);

	/* Called when the nlm_rqst is freed, callbacks should clean up here */
	void (*nlmclnt_release_call)(void *);
};

extern int	nlmclnt_proc(struct nlm_host *host, int cmd, struct file_lock *fl, void *data);
extern int	lockd_up(struct net *net);
extern void	lockd_down(struct net *net);

#endif /* LINUX_LOCKD_BIND_H */
