/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Isilon Inc http://www.isilon.com/
 * Copyright (c) 2013 Spectra Logic Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <fs/nfs/nfsport.h>

#include <rpc/rpc.h>
#include <nfs/nfs_fha.h>
#include <fs/nfs/xdr_subs.h>
#include <fs/nfs/nfs.h>
#include <fs/nfs/nfsproto.h>
#include <fs/nfs/nfsm_subs.h>
#include <fs/nfsserver/nfs_fha_new.h>

static void fhanew_init(void *foo);
static void fhanew_uninit(void *foo);
rpcproc_t fhanew_get_procnum(rpcproc_t procnum);
int fhanew_realign(struct mbuf **mb, int malloc_flags);
int fhanew_get_fh(uint64_t *fh, int v3, struct mbuf **md, caddr_t *dpos);
int fhanew_is_read(rpcproc_t procnum);
int fhanew_is_write(rpcproc_t procnum);
int fhanew_get_offset(struct mbuf **md, caddr_t *dpos, int v3,
		      struct fha_info *info);
int fhanew_no_offset(rpcproc_t procnum);
void fhanew_set_locktype(rpcproc_t procnum, struct fha_info *info);
static int fhenew_stats_sysctl(SYSCTL_HANDLER_ARGS);

static struct fha_params fhanew_softc;

SYSCTL_DECL(_vfs_nfsd);

extern int newnfs_nfsv3_procid[];
extern SVCPOOL	*nfsrvd_pool;

SYSINIT(nfs_fhanew, SI_SUB_ROOT_CONF, SI_ORDER_ANY, fhanew_init, NULL);
SYSUNINIT(nfs_fhanew, SI_SUB_ROOT_CONF, SI_ORDER_ANY, fhanew_uninit, NULL);

static void
fhanew_init(void *foo)
{
	struct fha_params *softc;

	softc = &fhanew_softc;

	bzero(softc, sizeof(*softc));

	/*
	 * Setup the callbacks for this FHA personality.
	 */
	softc->callbacks.get_procnum = fhanew_get_procnum;
	softc->callbacks.realign = fhanew_realign;
	softc->callbacks.get_fh = fhanew_get_fh;
	softc->callbacks.is_read = fhanew_is_read;
	softc->callbacks.is_write = fhanew_is_write;
	softc->callbacks.get_offset = fhanew_get_offset;
	softc->callbacks.no_offset = fhanew_no_offset;
	softc->callbacks.set_locktype = fhanew_set_locktype;
	softc->callbacks.fhe_stats_sysctl = fhenew_stats_sysctl;

	snprintf(softc->server_name, sizeof(softc->server_name),
	    FHANEW_SERVER_NAME);

	softc->pool = &nfsrvd_pool;

	/*
	 * Initialize the sysctl context list for the fha module.
	 */
	sysctl_ctx_init(&softc->sysctl_ctx);
	softc->sysctl_tree = SYSCTL_ADD_NODE(&softc->sysctl_ctx,
	    SYSCTL_STATIC_CHILDREN(_vfs_nfsd), OID_AUTO, "fha", CTLFLAG_RD,
	    0, "NFS File Handle Affinity (FHA)");
	if (softc->sysctl_tree == NULL) {
		printf("%s: unable to allocate sysctl tree\n", __func__);
		return;
	}

	fha_init(softc);
}

static void
fhanew_uninit(void *foo)
{
	struct fha_params *softc;

	softc = &fhanew_softc;

	fha_uninit(softc);
}

rpcproc_t
fhanew_get_procnum(rpcproc_t procnum)
{
	if (procnum > NFSV2PROC_STATFS)
		return (-1);

	return (newnfs_nfsv3_procid[procnum]);
}

int
fhanew_realign(struct mbuf **mb, int malloc_flags)
{
	return (newnfs_realign(mb, malloc_flags));
}

int
fhanew_get_fh(uint64_t *fh, int v3, struct mbuf **md, caddr_t *dpos)
{
	struct nfsrv_descript lnd, *nd;
	uint32_t *tl;
	uint8_t *buf;
	uint64_t t;
	int error, len, i;

	error = 0;
	len = 0;
	nd = &lnd;

	nd->nd_md = *md;
	nd->nd_dpos = *dpos;

	if (v3) {
		NFSM_DISSECT_NONBLOCK(tl, uint32_t *, NFSX_UNSIGNED);
		if ((len = fxdr_unsigned(int, *tl)) <= 0 || len > NFSX_FHMAX) {
			error = EBADRPC;
			goto nfsmout;
		}
	} else {
		len = NFSX_V2FH;
	}

	t = 0;
	if (len != 0) {
		NFSM_DISSECT_NONBLOCK(buf, uint8_t *, len);
		for (i = 0; i < len; i++)
			t ^= ((uint64_t)buf[i] << (i & 7) * 8);
	}
	*fh = t;

nfsmout:
	*md = nd->nd_md;
	*dpos = nd->nd_dpos;

	return (error);
}

int
fhanew_is_read(rpcproc_t procnum)
{
	if (procnum == NFSPROC_READ)
		return (1);
	else
		return (0);
}

int
fhanew_is_write(rpcproc_t procnum)
{
	if (procnum == NFSPROC_WRITE)
		return (1);
	else
		return (0);
}

int
fhanew_get_offset(struct mbuf **md, caddr_t *dpos, int v3,
		  struct fha_info *info)
{
	struct nfsrv_descript lnd, *nd;
	uint32_t *tl;
	int error;

	error = 0;

	nd = &lnd;
	nd->nd_md = *md;
	nd->nd_dpos = *dpos;

	if (v3) {
		NFSM_DISSECT_NONBLOCK(tl, uint32_t *, 2 * NFSX_UNSIGNED);
		info->offset = fxdr_hyper(tl);
	} else {
		NFSM_DISSECT_NONBLOCK(tl, uint32_t *, NFSX_UNSIGNED);
		info->offset = fxdr_unsigned(uint32_t, *tl);
	}

nfsmout:
	*md = nd->nd_md;
	*dpos = nd->nd_dpos;

	return (error);
}

int
fhanew_no_offset(rpcproc_t procnum)
{
	if (procnum == NFSPROC_FSSTAT ||
	    procnum == NFSPROC_FSINFO ||
	    procnum == NFSPROC_PATHCONF ||
	    procnum == NFSPROC_NOOP ||
	    procnum == NFSPROC_NULL)
		return (1);
	else
		return (0);
}

void
fhanew_set_locktype(rpcproc_t procnum, struct fha_info *info)
{
	switch (procnum) {
	case NFSPROC_NULL:
	case NFSPROC_GETATTR:
	case NFSPROC_LOOKUP:
	case NFSPROC_ACCESS:
	case NFSPROC_READLINK:
	case NFSPROC_READ:
	case NFSPROC_READDIR:
	case NFSPROC_READDIRPLUS:
	case NFSPROC_WRITE:
		info->locktype = LK_SHARED;
		break;
	case NFSPROC_SETATTR:
	case NFSPROC_CREATE:
	case NFSPROC_MKDIR:
	case NFSPROC_SYMLINK:
	case NFSPROC_MKNOD:
	case NFSPROC_REMOVE:
	case NFSPROC_RMDIR:
	case NFSPROC_RENAME:
	case NFSPROC_LINK:
	case NFSPROC_FSSTAT:
	case NFSPROC_FSINFO:
	case NFSPROC_PATHCONF:
	case NFSPROC_COMMIT:
	case NFSPROC_NOOP:
		info->locktype = LK_EXCLUSIVE;
		break;
	}
}

static int
fhenew_stats_sysctl(SYSCTL_HANDLER_ARGS)
{
	return (fhe_stats_sysctl(oidp, arg1, arg2, req, &fhanew_softc));
}


SVCTHREAD *
fhanew_assign(SVCTHREAD *this_thread, struct svc_req *req)
{
	return (fha_assign(this_thread, req, &fhanew_softc));
}
