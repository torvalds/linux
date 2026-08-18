/* SPDX-License-Identifier: GPL-2.0 */
/*
 * XDR types for the NLM protocol
 *
 * Copyright (C) 1996 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LOCKD_XDR_H
#define _LOCKD_XDR_H

#include <linux/fs.h>
#include <linux/filelock.h>
#include <linux/nfs.h>
#include <linux/sunrpc/xdr.h>

#define SM_MAXSTRLEN		1024
#define SM_PRIV_SIZE		16

struct nsm_private {
	unsigned char		data[SM_PRIV_SIZE];
};

#define NLM_MAXCOOKIELEN    	32
#define NLM_MAXSTRLEN		1024

#define	nlm_granted		cpu_to_be32(NLM_LCK_GRANTED)
#define	nlm_lck_denied		cpu_to_be32(NLM_LCK_DENIED)
#define	nlm_lck_denied_nolocks	cpu_to_be32(NLM_LCK_DENIED_NOLOCKS)
#define	nlm_lck_blocked		cpu_to_be32(NLM_LCK_BLOCKED)
#define	nlm_lck_denied_grace_period	cpu_to_be32(NLM_LCK_DENIED_GRACE_PERIOD)

/* Lock info passed via NLM */
struct lockd_lock {
	char *			caller;
	unsigned int		len; 	/* length of "caller" */
	struct nfs_fh		fh;
	struct xdr_netobj	oh;
	u32			svid;
	u64			lock_start;
	u64			lock_len;
	struct file_lock	fl;
};

/*
 *	NLM cookies. Technically they can be 1K, but Linux only uses 8 bytes.
 *	FreeBSD uses 16, Apple Mac OS X 10.3 uses 20. Therefore we set it to
 *	32 bytes.
 */

struct lockd_cookie {
	unsigned char data[NLM_MAXCOOKIELEN];
	unsigned int len;
};

/*
 * Generic lockd arguments for all but sm_notify
 */
struct lockd_args {
	struct lockd_cookie	cookie;
	struct lockd_lock	lock;
	u32			block;
	u32			reclaim;
	u32			state;
};

/*
 * Generic lockd result
 */
struct lockd_res {
	struct lockd_cookie	cookie;
	__be32			status;
	struct lockd_lock	lock;
};

/*
 * statd callback when client has rebooted
 */
struct lockd_reboot {
	char			*mon;
	unsigned int		len;
	u32			state;
	struct nsm_private	priv;
};

#endif /* _LOCKD_XDR_H */
