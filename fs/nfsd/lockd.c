// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains all the stubs needed when communicating with lockd.
 * This level of indirection is necessary so we can run nfsd+lockd without
 * requiring the nfs client to be compiled in/loaded, and vice versa.
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/file.h>
#include <linux/lockd/bind.h>
#include "nfsd.h"
#include "vfs.h"

#define NFSDDBG_FACILITY		NFSDDBG_LOCKD

/**
 * nlm_fopen - Open an NFSD file
 * @rqstp: NLM RPC procedure execution context
 * @f: NFS file handle to be opened
 * @filp: OUT: an opened struct file
 * @flags: the POSIX open flags to use
 *
 * nlm_fopen() holds the dentry reference until nlm_fclose() releases it.
 *
 * Returns zero on success or a negative errno value if the file
 * cannot be opened.
 */
static int nlm_fopen(struct svc_rqst *rqstp, struct nfs_fh *f,
		     struct file **filp, int flags)
{
	__be32		nfserr;
	int		access;
	struct svc_fh	fh;

	/* must initialize before using! but maxsize doesn't matter */
	fh_init(&fh,0);
	fh.fh_handle.fh_size = f->size;
	memcpy(&fh.fh_handle.fh_raw, f->data, f->size);
	fh.fh_export = NULL;

	/*
	 * Allow BYPASS_GSS as some client implementations use AUTH_SYS
	 * for NLM even when GSS is used for NFS.
	 * Allow OWNER_OVERRIDE as permission might have been changed
	 * after the file was opened.
	 * Pass MAY_NLM so that authentication can be completely bypassed
	 * if NFSEXP_NOAUTHNLM is set.  Some older clients use AUTH_NULL
	 * for NLM requests.
	 */
	access = (flags == O_WRONLY) ? NFSD_MAY_WRITE : NFSD_MAY_READ;
	access |= NFSD_MAY_NLM | NFSD_MAY_OWNER_OVERRIDE | NFSD_MAY_BYPASS_GSS;
	nfserr = nfsd_open(rqstp, &fh, S_IFREG, access, filp);
	fh_put(&fh);

	switch (nfserr) {
	case nfs_ok:
		break;
	case nfserr_jukebox:
		/*
		 * This error can indicate a presence of a conflicting
		 * delegation to an NLM lock request. Options are:
		 * (1) For now, drop this request and make the client
		 * retry. When delegation is returned, client's lock retry
		 * will complete.
		 * (2) NLM4_DENIED as per "spec" signals to the client
		 * that the lock is unavailable now but client can retry.
		 * Linux client implementation does not. It treats
		 * NLM4_DENIED same as NLM4_FAILED and fails the request.
		 * (3) For the future, treat this as blocked lock and try
		 * to callback when the delegation is returned but might
		 * not have a proper lock request to block on.
		 */
		return -EWOULDBLOCK;
	case nfserr_stale:
		return -ESTALE;
	default:
		return -ENOLCK;
	}

	return 0;
}

/**
 * nlm_fclose - Close an NFSD file
 * @filp: a struct file that was opened by nlm_fopen()
 */
static void
nlm_fclose(struct file *filp)
{
	fput(filp);
}

static const struct nlmsvc_binding nfsd_nlm_ops = {
	.owner		= THIS_MODULE,
	.fopen		= nlm_fopen,		/* open file for locking */
	.fclose		= nlm_fclose,		/* close file */
};

void
nfsd_lockd_init(void)
{
	dprintk("nfsd: initializing lockd\n");
	rcu_assign_pointer(nlmsvc_ops, &nfsd_nlm_ops);
}

void
nfsd_lockd_shutdown(void)
{
	RCU_INIT_POINTER(nlmsvc_ops, NULL);
	synchronize_rcu();
}
