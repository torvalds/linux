/*
 * linux/include/linux/lockd/xdr.h
 *
 * XDR types for the NLM protocol
 *
 * Copyright (C) 1996 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef LOCKD_XDR_H
#define LOCKD_XDR_H

#include <linux/fs.h>
#include <linux/nfs.h>
#include <linux/sunrpc/xdr.h>

struct svc_rqst;

#define NLM_MAXCOOKIELEN    	32
#define NLM_MAXSTRLEN		1024

#define	nlm_granted		__constant_htonl(NLM_LCK_GRANTED)
#define	nlm_lck_denied		__constant_htonl(NLM_LCK_DENIED)
#define	nlm_lck_denied_nolocks	__constant_htonl(NLM_LCK_DENIED_NOLOCKS)
#define	nlm_lck_blocked		__constant_htonl(NLM_LCK_BLOCKED)
#define	nlm_lck_denied_grace_period	__constant_htonl(NLM_LCK_DENIED_GRACE_PERIOD)

#define nlm_drop_reply		__constant_htonl(30000)

/* Lock info passed via NLM */
struct nlm_lock {
	char *			caller;
	int			len; 	/* length of "caller" */
	struct nfs_fh		fh;
	struct xdr_netobj	oh;
	u32			svid;
	struct file_lock	fl;
};

/*
 *	NLM cookies. Technically they can be 1K, but Linux only uses 8 bytes.
 *	FreeBSD uses 16, Apple Mac OS X 10.3 uses 20. Therefore we set it to
 *	32 bytes.
 */
 
struct nlm_cookie
{
	unsigned char data[NLM_MAXCOOKIELEN];
	unsigned int len;
};

/*
 * Generic lockd arguments for all but sm_notify
 */
struct nlm_args {
	struct nlm_cookie	cookie;
	struct nlm_lock		lock;
	u32			block;
	u32			reclaim;
	u32			state;
	u32			monitor;
	u32			fsm_access;
	u32			fsm_mode;
};

typedef struct nlm_args nlm_args;

/*
 * Generic lockd result
 */
struct nlm_res {
	struct nlm_cookie	cookie;
	u32			status;
	struct nlm_lock		lock;
};

/*
 * statd callback when client has rebooted
 */
struct nlm_reboot {
	char *		mon;
	int		len;
	u32		state;
	u32		addr;
	u32		vers;
	u32		proto;
};

/*
 * Contents of statd callback when monitored host rebooted
 */
#define NLMSVC_XDRSIZE		sizeof(struct nlm_args)

int	nlmsvc_decode_testargs(struct svc_rqst *, __be32 *, struct nlm_args *);
int	nlmsvc_encode_testres(struct svc_rqst *, __be32 *, struct nlm_res *);
int	nlmsvc_decode_lockargs(struct svc_rqst *, __be32 *, struct nlm_args *);
int	nlmsvc_decode_cancargs(struct svc_rqst *, __be32 *, struct nlm_args *);
int	nlmsvc_decode_unlockargs(struct svc_rqst *, __be32 *, struct nlm_args *);
int	nlmsvc_encode_res(struct svc_rqst *, __be32 *, struct nlm_res *);
int	nlmsvc_decode_res(struct svc_rqst *, __be32 *, struct nlm_res *);
int	nlmsvc_encode_void(struct svc_rqst *, __be32 *, void *);
int	nlmsvc_decode_void(struct svc_rqst *, __be32 *, void *);
int	nlmsvc_decode_shareargs(struct svc_rqst *, __be32 *, struct nlm_args *);
int	nlmsvc_encode_shareres(struct svc_rqst *, __be32 *, struct nlm_res *);
int	nlmsvc_decode_notify(struct svc_rqst *, __be32 *, struct nlm_args *);
int	nlmsvc_decode_reboot(struct svc_rqst *, __be32 *, struct nlm_reboot *);
/*
int	nlmclt_encode_testargs(struct rpc_rqst *, u32 *, struct nlm_args *);
int	nlmclt_encode_lockargs(struct rpc_rqst *, u32 *, struct nlm_args *);
int	nlmclt_encode_cancargs(struct rpc_rqst *, u32 *, struct nlm_args *);
int	nlmclt_encode_unlockargs(struct rpc_rqst *, u32 *, struct nlm_args *);
 */

#endif /* LOCKD_XDR_H */
