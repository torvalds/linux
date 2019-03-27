/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_kgssapi.h"

#include <fs/nfs/nfsport.h>

#include <rpc/rpc.h>
#include <rpc/rpcsec_gss.h>
#include <rpc/replay.h>


NFSDLOCKMUTEX;

extern SVCPOOL	*nfscbd_pool;

static int nfs_cbproc(struct nfsrv_descript *, u_int32_t);

extern u_long sb_max_adj;
extern int nfs_numnfscbd;
extern int nfscl_debuglevel;

/*
 * NFS client system calls for handling callbacks.
 */

/*
 * Handles server to client callbacks.
 */
static void
nfscb_program(struct svc_req *rqst, SVCXPRT *xprt)
{
	struct nfsrv_descript nd;
	int cacherep, credflavor;

	memset(&nd, 0, sizeof(nd));
	if (rqst->rq_proc != NFSPROC_NULL &&
	    rqst->rq_proc != NFSV4PROC_CBCOMPOUND) {
		svcerr_noproc(rqst);
		svc_freereq(rqst);
		return;
	}
	nd.nd_procnum = rqst->rq_proc;
	nd.nd_flag = (ND_NFSCB | ND_NFSV4);

	/*
	 * Note: we want rq_addr, not svc_getrpccaller for nd_nam2 -
	 * NFS_SRVMAXDATA uses a NULL value for nd_nam2 to detect TCP
	 * mounts.
	 */
	nd.nd_mrep = rqst->rq_args;
	rqst->rq_args = NULL;
	newnfs_realign(&nd.nd_mrep, M_WAITOK);
	nd.nd_md = nd.nd_mrep;
	nd.nd_dpos = mtod(nd.nd_md, caddr_t);
	nd.nd_nam = svc_getrpccaller(rqst);
	nd.nd_nam2 = rqst->rq_addr;
	nd.nd_mreq = NULL;
	nd.nd_cred = NULL;

	NFSCL_DEBUG(1, "cbproc=%d\n",nd.nd_procnum);
	if (nd.nd_procnum != NFSPROC_NULL) {
		if (!svc_getcred(rqst, &nd.nd_cred, &credflavor)) {
			svcerr_weakauth(rqst);
			svc_freereq(rqst);
			m_freem(nd.nd_mrep);
			return;
		}

		/* For now, I don't care what credential flavor was used. */
#ifdef notyet
#ifdef MAC
		mac_cred_associate_nfsd(nd.nd_cred);
#endif
#endif
		cacherep = nfs_cbproc(&nd, rqst->rq_xid);
	} else {
		NFSMGET(nd.nd_mreq);
		nd.nd_mreq->m_len = 0;
		cacherep = RC_REPLY;
	}
	if (nd.nd_mrep != NULL)
		m_freem(nd.nd_mrep);

	if (nd.nd_cred != NULL)
		crfree(nd.nd_cred);

	if (cacherep == RC_DROPIT) {
		if (nd.nd_mreq != NULL)
			m_freem(nd.nd_mreq);
		svc_freereq(rqst);
		return;
	}

	if (nd.nd_mreq == NULL) {
		svcerr_decode(rqst);
		svc_freereq(rqst);
		return;
	}

	if (nd.nd_repstat & NFSERR_AUTHERR) {
		svcerr_auth(rqst, nd.nd_repstat & ~NFSERR_AUTHERR);
		if (nd.nd_mreq != NULL)
			m_freem(nd.nd_mreq);
	} else if (!svc_sendreply_mbuf(rqst, nd.nd_mreq))
		svcerr_systemerr(rqst);
	else
		NFSCL_DEBUG(1, "cbrep sent\n");
	svc_freereq(rqst);
}

/*
 * Check the cache and, optionally, do the RPC.
 * Return the appropriate cache response.
 */
static int
nfs_cbproc(struct nfsrv_descript *nd, u_int32_t xid)
{
	struct thread *td = curthread;
	int cacherep;

	if (nd->nd_nam2 == NULL)
		nd->nd_flag |= ND_STREAMSOCK;

	nfscl_docb(nd, td);
	if (nd->nd_repstat == NFSERR_DONTREPLY)
		cacherep = RC_DROPIT;
	else
		cacherep = RC_REPLY;
	return (cacherep);
}

/*
 * Adds a socket to the list for servicing by nfscbds.
 */
int
nfscbd_addsock(struct file *fp)
{
	int siz;
	struct socket *so;
	int error;
	SVCXPRT *xprt;

	so = fp->f_data;

	siz = sb_max_adj;
	error = soreserve(so, siz, siz);
	if (error)
		return (error);

	/*
	 * Steal the socket from userland so that it doesn't close
	 * unexpectedly.
	 */
	if (so->so_type == SOCK_DGRAM)
		xprt = svc_dg_create(nfscbd_pool, so, 0, 0);
	else
		xprt = svc_vc_create(nfscbd_pool, so, 0, 0);
	if (xprt) {
		fp->f_ops = &badfileops;
		fp->f_data = NULL;
		svc_reg(xprt, NFS_CALLBCKPROG, NFSV4_CBVERS, nfscb_program,
		    NULL);
		SVC_RELEASE(xprt);
	}

	return (0);
}

/*
 * Called by nfssvc() for nfscbds. Just loops around servicing rpc requests
 * until it is killed by a signal.
 *
 * For now, only support callbacks via RPCSEC_GSS if there is a KerberosV
 * keytab entry with a host based entry in it on the client. (I'm not even
 * sure that getting Acceptor credentials for a user principal with a
 * credentials cache is possible, but even if it is, major changes to the
 * kgssapi would be required.)
 * I don't believe that this is a serious limitation since, as of 2009, most
 * NFSv4 servers supporting callbacks are using AUTH_SYS for callbacks even
 * when the client is using RPCSEC_GSS. (This BSD server uses AUTH_SYS
 * for callbacks unless nfsrv_gsscallbackson is set non-zero.)
 */
int
nfscbd_nfsd(struct thread *td, struct nfsd_nfscbd_args *args)
{
	char principal[128];
	int error;

	if (args != NULL) {
		error = copyinstr(args->principal, principal,
		    sizeof(principal), NULL);
		if (error)
			return (error);
	} else {
		principal[0] = '\0';
	}

	/*
	 * Only the first nfsd actually does any work. The RPC code
	 * adds threads to it as needed. Any extra processes offered
	 * by nfsd just exit. If nfsd is new enough, it will call us
	 * once with a structure that specifies how many threads to
	 * use.
	 */
	NFSD_LOCK();
	if (nfs_numnfscbd == 0) {
		nfs_numnfscbd++;

		NFSD_UNLOCK();

		if (principal[0] != '\0')
			rpc_gss_set_svc_name_call(principal, "kerberosv5",
			    GSS_C_INDEFINITE, NFS_CALLBCKPROG, NFSV4_CBVERS);

		nfscbd_pool->sp_minthreads = 4;
		nfscbd_pool->sp_maxthreads = 4;
			
		svc_run(nfscbd_pool);

		rpc_gss_clear_svc_name_call(NFS_CALLBCKPROG, NFSV4_CBVERS);

		NFSD_LOCK();
		nfs_numnfscbd--;
		nfsrvd_cbinit(1);
	}
	NFSD_UNLOCK();

	return (0);
}

/*
 * Initialize the data structures for the server.
 * Handshake with any new nfsds starting up to avoid any chance of
 * corruption.
 */
void
nfsrvd_cbinit(int terminating)
{

	NFSD_LOCK_ASSERT();

	if (terminating) {
		/* Wait for any xprt registrations to complete. */
		while (nfs_numnfscbd > 0)
			msleep(&nfs_numnfscbd, NFSDLOCKMUTEXPTR, PZERO, 
			    "nfscbdt", 0);
		if (nfscbd_pool != NULL) {
			NFSD_UNLOCK();
			svcpool_close(nfscbd_pool);
			NFSD_LOCK();
		}
	}

	if (nfscbd_pool == NULL) {
		NFSD_UNLOCK();
		nfscbd_pool = svcpool_create("nfscbd", NULL);
		nfscbd_pool->sp_rcache = NULL;
		nfscbd_pool->sp_assign = NULL;
		nfscbd_pool->sp_done = NULL;
		NFSD_LOCK();
	}
}

