/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1991, 1993, 1995
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

/*
 * Socket operations for use by nfs
 */

#include "opt_kgssapi.h"
#include "opt_nfs.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/syscallsubr.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/vnode.h>

#include <rpc/rpc.h>
#include <rpc/krpc.h>

#include <kgssapi/krb5/kcrypto.h>

#include <fs/nfs/nfsport.h>

#ifdef KDTRACE_HOOKS
#include <sys/dtrace_bsd.h>

dtrace_nfsclient_nfs23_start_probe_func_t
		dtrace_nfscl_nfs234_start_probe;

dtrace_nfsclient_nfs23_done_probe_func_t
		dtrace_nfscl_nfs234_done_probe;

/*
 * Registered probes by RPC type.
 */
uint32_t	nfscl_nfs2_start_probes[NFSV41_NPROCS + 1];
uint32_t	nfscl_nfs2_done_probes[NFSV41_NPROCS + 1];

uint32_t	nfscl_nfs3_start_probes[NFSV41_NPROCS + 1];
uint32_t	nfscl_nfs3_done_probes[NFSV41_NPROCS + 1];

uint32_t	nfscl_nfs4_start_probes[NFSV41_NPROCS + 1];
uint32_t	nfscl_nfs4_done_probes[NFSV41_NPROCS + 1];
#endif

NFSSTATESPINLOCK;
NFSREQSPINLOCK;
NFSDLOCKMUTEX;
NFSCLSTATEMUTEX;
extern struct nfsstatsv1 nfsstatsv1;
extern struct nfsreqhead nfsd_reqq;
extern int nfscl_ticks;
extern void (*ncl_call_invalcaches)(struct vnode *);
extern int nfs_numnfscbd;
extern int nfscl_debuglevel;
extern int nfsrv_lease;

SVCPOOL		*nfscbd_pool;
static int	nfsrv_gsscallbackson = 0;
static int	nfs_bufpackets = 4;
static int	nfs_reconnects;
static int	nfs3_jukebox_delay = 10;
static int	nfs_skip_wcc_data_onerr = 1;
static int	nfs_dsretries = 2;

SYSCTL_DECL(_vfs_nfs);

SYSCTL_INT(_vfs_nfs, OID_AUTO, bufpackets, CTLFLAG_RW, &nfs_bufpackets, 0,
    "Buffer reservation size 2 < x < 64");
SYSCTL_INT(_vfs_nfs, OID_AUTO, reconnects, CTLFLAG_RD, &nfs_reconnects, 0,
    "Number of times the nfs client has had to reconnect");
SYSCTL_INT(_vfs_nfs, OID_AUTO, nfs3_jukebox_delay, CTLFLAG_RW, &nfs3_jukebox_delay, 0,
    "Number of seconds to delay a retry after receiving EJUKEBOX");
SYSCTL_INT(_vfs_nfs, OID_AUTO, skip_wcc_data_onerr, CTLFLAG_RW, &nfs_skip_wcc_data_onerr, 0,
    "Disable weak cache consistency checking when server returns an error");
SYSCTL_INT(_vfs_nfs, OID_AUTO, dsretries, CTLFLAG_RW, &nfs_dsretries, 0,
    "Number of retries for a DS RPC before failure");

static void	nfs_down(struct nfsmount *, struct thread *, const char *,
    int, int);
static void	nfs_up(struct nfsmount *, struct thread *, const char *,
    int, int);
static int	nfs_msg(struct thread *, const char *, const char *, int);

struct nfs_cached_auth {
	int		ca_refs; /* refcount, including 1 from the cache */
	uid_t		ca_uid;	 /* uid that corresponds to this auth */
	AUTH		*ca_auth; /* RPC auth handle */
};

static int nfsv2_procid[NFS_V3NPROCS] = {
	NFSV2PROC_NULL,
	NFSV2PROC_GETATTR,
	NFSV2PROC_SETATTR,
	NFSV2PROC_LOOKUP,
	NFSV2PROC_NOOP,
	NFSV2PROC_READLINK,
	NFSV2PROC_READ,
	NFSV2PROC_WRITE,
	NFSV2PROC_CREATE,
	NFSV2PROC_MKDIR,
	NFSV2PROC_SYMLINK,
	NFSV2PROC_CREATE,
	NFSV2PROC_REMOVE,
	NFSV2PROC_RMDIR,
	NFSV2PROC_RENAME,
	NFSV2PROC_LINK,
	NFSV2PROC_READDIR,
	NFSV2PROC_NOOP,
	NFSV2PROC_STATFS,
	NFSV2PROC_NOOP,
	NFSV2PROC_NOOP,
	NFSV2PROC_NOOP,
};

/*
 * Initialize sockets and congestion for a new NFS connection.
 * We do not free the sockaddr if error.
 * Which arguments are set to NULL indicate what kind of call it is.
 * cred == NULL --> a call to connect to a pNFS DS
 * nmp == NULL --> indicates an upcall to userland or a NFSv4.0 callback
 */
int
newnfs_connect(struct nfsmount *nmp, struct nfssockreq *nrp,
    struct ucred *cred, NFSPROC_T *p, int callback_retry_mult)
{
	int rcvreserve, sndreserve;
	int pktscale, pktscalesav;
	struct sockaddr *saddr;
	struct ucred *origcred;
	CLIENT *client;
	struct netconfig *nconf;
	struct socket *so;
	int one = 1, retries, error = 0;
	struct thread *td = curthread;
	SVCXPRT *xprt;
	struct timeval timo;

	/*
	 * We need to establish the socket using the credentials of
	 * the mountpoint.  Some parts of this process (such as
	 * sobind() and soconnect()) will use the curent thread's
	 * credential instead of the socket credential.  To work
	 * around this, temporarily change the current thread's
	 * credential to that of the mountpoint.
	 *
	 * XXX: It would be better to explicitly pass the correct
	 * credential to sobind() and soconnect().
	 */
	origcred = td->td_ucred;

	/*
	 * Use the credential in nr_cred, if not NULL.
	 */
	if (nrp->nr_cred != NULL)
		td->td_ucred = nrp->nr_cred;
	else
		td->td_ucred = cred;
	saddr = nrp->nr_nam;

	if (saddr->sa_family == AF_INET)
		if (nrp->nr_sotype == SOCK_DGRAM)
			nconf = getnetconfigent("udp");
		else
			nconf = getnetconfigent("tcp");
	else if (saddr->sa_family == AF_LOCAL)
		nconf = getnetconfigent("local");
	else
		if (nrp->nr_sotype == SOCK_DGRAM)
			nconf = getnetconfigent("udp6");
		else
			nconf = getnetconfigent("tcp6");
			
	pktscale = nfs_bufpackets;
	if (pktscale < 2)
		pktscale = 2;
	if (pktscale > 64)
		pktscale = 64;
	pktscalesav = pktscale;
	/*
	 * soreserve() can fail if sb_max is too small, so shrink pktscale
	 * and try again if there is an error.
	 * Print a log message suggesting increasing sb_max.
	 * Creating a socket and doing this is necessary since, if the
	 * reservation sizes are too large and will make soreserve() fail,
	 * the connection will work until a large send is attempted and
	 * then it will loop in the krpc code.
	 */
	so = NULL;
	saddr = NFSSOCKADDR(nrp->nr_nam, struct sockaddr *);
	error = socreate(saddr->sa_family, &so, nrp->nr_sotype, 
	    nrp->nr_soproto, td->td_ucred, td);
	if (error) {
		td->td_ucred = origcred;
		goto out;
	}
	do {
	    if (error != 0 && pktscale > 2) {
		if (nmp != NULL && nrp->nr_sotype == SOCK_STREAM &&
		    pktscale == pktscalesav)
		    printf("Consider increasing kern.ipc.maxsockbuf\n");
		pktscale--;
	    }
	    if (nrp->nr_sotype == SOCK_DGRAM) {
		if (nmp != NULL) {
			sndreserve = (NFS_MAXDGRAMDATA + NFS_MAXPKTHDR) *
			    pktscale;
			rcvreserve = (NFS_MAXDGRAMDATA + NFS_MAXPKTHDR) *
			    pktscale;
		} else {
			sndreserve = rcvreserve = 1024 * pktscale;
		}
	    } else {
		if (nrp->nr_sotype != SOCK_STREAM)
			panic("nfscon sotype");
		if (nmp != NULL) {
			sndreserve = (NFS_MAXBSIZE + NFS_MAXXDR +
			    sizeof (u_int32_t)) * pktscale;
			rcvreserve = (NFS_MAXBSIZE + NFS_MAXXDR +
			    sizeof (u_int32_t)) * pktscale;
		} else {
			sndreserve = rcvreserve = 1024 * pktscale;
		}
	    }
	    error = soreserve(so, sndreserve, rcvreserve);
	    if (error != 0 && nmp != NULL && nrp->nr_sotype == SOCK_STREAM &&
		pktscale <= 2)
		printf("Must increase kern.ipc.maxsockbuf or reduce"
		    " rsize, wsize\n");
	} while (error != 0 && pktscale > 2);
	soclose(so);
	if (error) {
		td->td_ucred = origcred;
		goto out;
	}

	client = clnt_reconnect_create(nconf, saddr, nrp->nr_prog,
	    nrp->nr_vers, sndreserve, rcvreserve);
	CLNT_CONTROL(client, CLSET_WAITCHAN, "nfsreq");
	if (nmp != NULL) {
		if ((nmp->nm_flag & NFSMNT_INT))
			CLNT_CONTROL(client, CLSET_INTERRUPTIBLE, &one);
		if ((nmp->nm_flag & NFSMNT_RESVPORT))
			CLNT_CONTROL(client, CLSET_PRIVPORT, &one);
		if (NFSHASSOFT(nmp)) {
			if (nmp->nm_sotype == SOCK_DGRAM)
				/*
				 * For UDP, the large timeout for a reconnect
				 * will be set to "nm_retry * nm_timeo / 2", so
				 * we only want to do 2 reconnect timeout
				 * retries.
				 */
				retries = 2;
			else
				retries = nmp->nm_retry;
		} else
			retries = INT_MAX;
		if (NFSHASNFSV4N(nmp)) {
			if (cred != NULL) {
				if (NFSHASSOFT(nmp)) {
					/*
					 * This should be a DS mount.
					 * Use CLSET_TIMEOUT to set the timeout
					 * for connections to DSs instead of
					 * specifying a timeout on each RPC.
					 * This is done so that SO_SNDTIMEO
					 * is set on the TCP socket as well
					 * as specifying a time limit when
					 * waiting for an RPC reply.  Useful
					 * if the send queue for the TCP
					 * connection has become constipated,
					 * due to a failed DS.
					 * The choice of lease_duration / 4 is
					 * fairly arbitrary, but seems to work
					 * ok, with a lower bound of 10sec.
					 */
					timo.tv_sec = nfsrv_lease / 4;
					if (timo.tv_sec < 10)
						timo.tv_sec = 10;
					timo.tv_usec = 0;
					CLNT_CONTROL(client, CLSET_TIMEOUT,
					    &timo);
				}
				/*
				 * Make sure the nfscbd_pool doesn't get
				 * destroyed while doing this.
				 */
				NFSD_LOCK();
				if (nfs_numnfscbd > 0) {
					nfs_numnfscbd++;
					NFSD_UNLOCK();
					xprt = svc_vc_create_backchannel(
					    nfscbd_pool);
					CLNT_CONTROL(client, CLSET_BACKCHANNEL,
					    xprt);
					NFSD_LOCK();
					nfs_numnfscbd--;
					if (nfs_numnfscbd == 0)
						wakeup(&nfs_numnfscbd);
				}
				NFSD_UNLOCK();
			} else {
				/*
				 * cred == NULL for a DS connect.
				 * For connects to a DS, set a retry limit
				 * so that failed DSs will be detected.
				 * This is ok for NFSv4.1, since a DS does
				 * not maintain open/lock state and is the
				 * only case where using a "soft" mount is
				 * recommended for NFSv4.
				 * For mounts from the MDS to DS, this is done
				 * via mount options, but that is not the case
				 * here.  The retry limit here can be adjusted
				 * via the sysctl vfs.nfs.dsretries.
				 * See the comment above w.r.t. timeout.
				 */
				timo.tv_sec = nfsrv_lease / 4;
				if (timo.tv_sec < 10)
					timo.tv_sec = 10;
				timo.tv_usec = 0;
				CLNT_CONTROL(client, CLSET_TIMEOUT, &timo);
				retries = nfs_dsretries;
			}
		}
	} else {
		/*
		 * Three cases:
		 * - Null RPC callback to client
		 * - Non-Null RPC callback to client, wait a little longer
		 * - upcalls to nfsuserd and gssd (clp == NULL)
		 */
		if (callback_retry_mult == 0) {
			retries = NFSV4_UPCALLRETRY;
			CLNT_CONTROL(client, CLSET_PRIVPORT, &one);
		} else {
			retries = NFSV4_CALLBACKRETRY * callback_retry_mult;
		}
	}
	CLNT_CONTROL(client, CLSET_RETRIES, &retries);

	if (nmp != NULL) {
		/*
		 * For UDP, there are 2 timeouts:
		 * - CLSET_RETRY_TIMEOUT sets the initial timeout for the timer
		 *   that does a retransmit of an RPC request using the same 
		 *   socket and xid. This is what you normally want to do,
		 *   since NFS servers depend on "same xid" for their
		 *   Duplicate Request Cache.
		 * - timeout specified in CLNT_CALL_MBUF(), which specifies when
		 *   retransmits on the same socket should fail and a fresh
		 *   socket created. Each of these timeouts counts as one
		 *   CLSET_RETRIES as set above.
		 * Set the initial retransmit timeout for UDP. This timeout
		 * doesn't exist for TCP and the following call just fails,
		 * which is ok.
		 */
		timo.tv_sec = nmp->nm_timeo / NFS_HZ;
		timo.tv_usec = (nmp->nm_timeo % NFS_HZ) * 1000000 / NFS_HZ;
		CLNT_CONTROL(client, CLSET_RETRY_TIMEOUT, &timo);
	}

	mtx_lock(&nrp->nr_mtx);
	if (nrp->nr_client != NULL) {
		mtx_unlock(&nrp->nr_mtx);
		/*
		 * Someone else already connected.
		 */
		CLNT_RELEASE(client);
	} else {
		nrp->nr_client = client;
		/*
		 * Protocols that do not require connections may be optionally
		 * left unconnected for servers that reply from a port other
		 * than NFS_PORT.
		 */
		if (nmp == NULL || (nmp->nm_flag & NFSMNT_NOCONN) == 0) {
			mtx_unlock(&nrp->nr_mtx);
			CLNT_CONTROL(client, CLSET_CONNECT, &one);
		} else
			mtx_unlock(&nrp->nr_mtx);
	}


	/* Restore current thread's credentials. */
	td->td_ucred = origcred;

out:
	NFSEXITCODE(error);
	return (error);
}

/*
 * NFS disconnect. Clean up and unlink.
 */
void
newnfs_disconnect(struct nfssockreq *nrp)
{
	CLIENT *client;

	mtx_lock(&nrp->nr_mtx);
	if (nrp->nr_client != NULL) {
		client = nrp->nr_client;
		nrp->nr_client = NULL;
		mtx_unlock(&nrp->nr_mtx);
		rpc_gss_secpurge_call(client);
		CLNT_CLOSE(client);
		CLNT_RELEASE(client);
	} else {
		mtx_unlock(&nrp->nr_mtx);
	}
}

static AUTH *
nfs_getauth(struct nfssockreq *nrp, int secflavour, char *clnt_principal,
    char *srv_principal, gss_OID mech_oid, struct ucred *cred)
{
	rpc_gss_service_t svc;
	AUTH *auth;

	switch (secflavour) {
	case RPCSEC_GSS_KRB5:
	case RPCSEC_GSS_KRB5I:
	case RPCSEC_GSS_KRB5P:
		if (!mech_oid) {
			if (!rpc_gss_mech_to_oid_call("kerberosv5", &mech_oid))
				return (NULL);
		}
		if (secflavour == RPCSEC_GSS_KRB5)
			svc = rpc_gss_svc_none;
		else if (secflavour == RPCSEC_GSS_KRB5I)
			svc = rpc_gss_svc_integrity;
		else
			svc = rpc_gss_svc_privacy;

		if (clnt_principal == NULL)
			auth = rpc_gss_secfind_call(nrp->nr_client, cred,
			    srv_principal, mech_oid, svc);
		else {
			auth = rpc_gss_seccreate_call(nrp->nr_client, cred,
			    clnt_principal, srv_principal, "kerberosv5",
			    svc, NULL, NULL, NULL);
			return (auth);
		}
		if (auth != NULL)
			return (auth);
		/* fallthrough */
	case AUTH_SYS:
	default:
		return (authunix_create(cred));

	}
}

/*
 * Callback from the RPC code to generate up/down notifications.
 */

struct nfs_feedback_arg {
	struct nfsmount *nf_mount;
	int		nf_lastmsg;	/* last tprintf */
	int		nf_tprintfmsg;
	struct thread	*nf_td;
};

static void
nfs_feedback(int type, int proc, void *arg)
{
	struct nfs_feedback_arg *nf = (struct nfs_feedback_arg *) arg;
	struct nfsmount *nmp = nf->nf_mount;
	time_t now;

	switch (type) {
	case FEEDBACK_REXMIT2:
	case FEEDBACK_RECONNECT:
		now = NFSD_MONOSEC;
		if (nf->nf_lastmsg + nmp->nm_tprintf_delay < now) {
			nfs_down(nmp, nf->nf_td,
			    "not responding", 0, NFSSTA_TIMEO);
			nf->nf_tprintfmsg = TRUE;
			nf->nf_lastmsg = now;
		}
		break;

	case FEEDBACK_OK:
		nfs_up(nf->nf_mount, nf->nf_td,
		    "is alive again", NFSSTA_TIMEO, nf->nf_tprintfmsg);
		break;
	}
}

/*
 * newnfs_request - goes something like this
 *	- does the rpc by calling the krpc layer
 *	- break down rpc header and return with nfs reply
 * nb: always frees up nd_mreq mbuf list
 */
int
newnfs_request(struct nfsrv_descript *nd, struct nfsmount *nmp,
    struct nfsclient *clp, struct nfssockreq *nrp, vnode_t vp,
    struct thread *td, struct ucred *cred, u_int32_t prog, u_int32_t vers,
    u_char *retsum, int toplevel, u_int64_t *xidp, struct nfsclsession *dssep)
{
	uint32_t retseq, retval, slotseq, *tl;
	time_t waituntil;
	int i = 0, j = 0, opcnt, set_sigset = 0, slot;
	int error = 0, usegssname = 0, secflavour = AUTH_SYS;
	int freeslot, maxslot, reterr, slotpos, timeo;
	u_int16_t procnum;
	u_int trylater_delay = 1;
	struct nfs_feedback_arg nf;
	struct timeval timo;
	AUTH *auth;
	struct rpc_callextra ext;
	enum clnt_stat stat;
	struct nfsreq *rep = NULL;
	char *srv_principal = NULL, *clnt_principal = NULL;
	sigset_t oldset;
	struct ucred *authcred;
	struct nfsclsession *sep;
	uint8_t sessionid[NFSX_V4SESSIONID];

	sep = dssep;
	if (xidp != NULL)
		*xidp = 0;
	/* Reject requests while attempting a forced unmount. */
	if (nmp != NULL && NFSCL_FORCEDISM(nmp->nm_mountp)) {
		m_freem(nd->nd_mreq);
		return (ESTALE);
	}

	/*
	 * Set authcred, which is used to acquire RPC credentials to
	 * the cred argument, by default. The crhold() should not be
	 * necessary, but will ensure that some future code change
	 * doesn't result in the credential being free'd prematurely.
	 */
	authcred = crhold(cred);

	/* For client side interruptible mounts, mask off the signals. */
	if (nmp != NULL && td != NULL && NFSHASINT(nmp)) {
		newnfs_set_sigmask(td, &oldset);
		set_sigset = 1;
	}

	/*
	 * XXX if not already connected call nfs_connect now. Longer
	 * term, change nfs_mount to call nfs_connect unconditionally
	 * and let clnt_reconnect_create handle reconnects.
	 */
	if (nrp->nr_client == NULL)
		newnfs_connect(nmp, nrp, cred, td, 0);

	/*
	 * For a client side mount, nmp is != NULL and clp == NULL. For
	 * server calls (callbacks or upcalls), nmp == NULL.
	 */
	if (clp != NULL) {
		NFSLOCKSTATE();
		if ((clp->lc_flags & LCL_GSS) && nfsrv_gsscallbackson) {
			secflavour = RPCSEC_GSS_KRB5;
			if (nd->nd_procnum != NFSPROC_NULL) {
				if (clp->lc_flags & LCL_GSSINTEGRITY)
					secflavour = RPCSEC_GSS_KRB5I;
				else if (clp->lc_flags & LCL_GSSPRIVACY)
					secflavour = RPCSEC_GSS_KRB5P;
			}
		}
		NFSUNLOCKSTATE();
	} else if (nmp != NULL && NFSHASKERB(nmp) &&
	     nd->nd_procnum != NFSPROC_NULL) {
		if (NFSHASALLGSSNAME(nmp) && nmp->nm_krbnamelen > 0)
			nd->nd_flag |= ND_USEGSSNAME;
		if ((nd->nd_flag & ND_USEGSSNAME) != 0) {
			/*
			 * If there is a client side host based credential,
			 * use that, otherwise use the system uid, if set.
			 * The system uid is in the nmp->nm_sockreq.nr_cred
			 * credentials.
			 */
			if (nmp->nm_krbnamelen > 0) {
				usegssname = 1;
				clnt_principal = nmp->nm_krbname;
			} else if (nmp->nm_uid != (uid_t)-1) {
				KASSERT(nmp->nm_sockreq.nr_cred != NULL,
				    ("newnfs_request: NULL nr_cred"));
				crfree(authcred);
				authcred = crhold(nmp->nm_sockreq.nr_cred);
			}
		} else if (nmp->nm_krbnamelen == 0 &&
		    nmp->nm_uid != (uid_t)-1 && cred->cr_uid == (uid_t)0) {
			/*
			 * If there is no host based principal name and
			 * the system uid is set and this is root, use the
			 * system uid, since root won't have user
			 * credentials in a credentials cache file.
			 * The system uid is in the nmp->nm_sockreq.nr_cred
			 * credentials.
			 */
			KASSERT(nmp->nm_sockreq.nr_cred != NULL,
			    ("newnfs_request: NULL nr_cred"));
			crfree(authcred);
			authcred = crhold(nmp->nm_sockreq.nr_cred);
		}
		if (NFSHASINTEGRITY(nmp))
			secflavour = RPCSEC_GSS_KRB5I;
		else if (NFSHASPRIVACY(nmp))
			secflavour = RPCSEC_GSS_KRB5P;
		else
			secflavour = RPCSEC_GSS_KRB5;
		srv_principal = NFSMNT_SRVKRBNAME(nmp);
	} else if (nmp != NULL && !NFSHASKERB(nmp) &&
	    nd->nd_procnum != NFSPROC_NULL &&
	    (nd->nd_flag & ND_USEGSSNAME) != 0) {
		/*
		 * Use the uid that did the mount when the RPC is doing
		 * NFSv4 system operations, as indicated by the
		 * ND_USEGSSNAME flag, for the AUTH_SYS case.
		 * The credentials in nm_sockreq.nr_cred were used for the
		 * mount.
		 */
		KASSERT(nmp->nm_sockreq.nr_cred != NULL,
		    ("newnfs_request: NULL nr_cred"));
		crfree(authcred);
		authcred = crhold(nmp->nm_sockreq.nr_cred);
	}

	if (nmp != NULL) {
		bzero(&nf, sizeof(struct nfs_feedback_arg));
		nf.nf_mount = nmp;
		nf.nf_td = td;
		nf.nf_lastmsg = NFSD_MONOSEC -
		    ((nmp->nm_tprintf_delay)-(nmp->nm_tprintf_initial_delay));
	}

	if (nd->nd_procnum == NFSPROC_NULL)
		auth = authnone_create();
	else if (usegssname) {
		/*
		 * For this case, the authenticator is held in the
		 * nfssockreq structure, so don't release the reference count
		 * held on it. --> Don't AUTH_DESTROY() it in this function.
		 */
		if (nrp->nr_auth == NULL)
			nrp->nr_auth = nfs_getauth(nrp, secflavour,
			    clnt_principal, srv_principal, NULL, authcred);
		else
			rpc_gss_refresh_auth_call(nrp->nr_auth);
		auth = nrp->nr_auth;
	} else
		auth = nfs_getauth(nrp, secflavour, NULL,
		    srv_principal, NULL, authcred);
	crfree(authcred);
	if (auth == NULL) {
		m_freem(nd->nd_mreq);
		if (set_sigset)
			newnfs_restore_sigmask(td, &oldset);
		return (EACCES);
	}
	bzero(&ext, sizeof(ext));
	ext.rc_auth = auth;
	if (nmp != NULL) {
		ext.rc_feedback = nfs_feedback;
		ext.rc_feedback_arg = &nf;
	}

	procnum = nd->nd_procnum;
	if ((nd->nd_flag & ND_NFSV4) &&
	    nd->nd_procnum != NFSPROC_NULL &&
	    nd->nd_procnum != NFSV4PROC_CBCOMPOUND)
		procnum = NFSV4PROC_COMPOUND;

	if (nmp != NULL) {
		NFSINCRGLOBAL(nfsstatsv1.rpcrequests);

		/* Map the procnum to the old NFSv2 one, as required. */
		if ((nd->nd_flag & ND_NFSV2) != 0) {
			if (nd->nd_procnum < NFS_V3NPROCS)
				procnum = nfsv2_procid[nd->nd_procnum];
			else
				procnum = NFSV2PROC_NOOP;
		}

		/*
		 * Now only used for the R_DONTRECOVER case, but until that is
		 * supported within the krpc code, I need to keep a queue of
		 * outstanding RPCs for nfsv4 client requests.
		 */
		if ((nd->nd_flag & ND_NFSV4) && procnum == NFSV4PROC_COMPOUND)
			rep = malloc(sizeof(struct nfsreq),
			    M_NFSDREQ, M_WAITOK);
#ifdef KDTRACE_HOOKS
		if (dtrace_nfscl_nfs234_start_probe != NULL) {
			uint32_t probe_id;
			int probe_procnum;
	
			if (nd->nd_flag & ND_NFSV4) {
				probe_id =
				    nfscl_nfs4_start_probes[nd->nd_procnum];
				probe_procnum = nd->nd_procnum;
			} else if (nd->nd_flag & ND_NFSV3) {
				probe_id = nfscl_nfs3_start_probes[procnum];
				probe_procnum = procnum;
			} else {
				probe_id =
				    nfscl_nfs2_start_probes[nd->nd_procnum];
				probe_procnum = procnum;
			}
			if (probe_id != 0)
				(dtrace_nfscl_nfs234_start_probe)
				    (probe_id, vp, nd->nd_mreq, cred,
				     probe_procnum);
		}
#endif
	}
	freeslot = -1;		/* Set to slot that needs to be free'd */
tryagain:
	slot = -1;		/* Slot that needs a sequence# increment. */
	/*
	 * This timeout specifies when a new socket should be created,
	 * along with new xid values. For UDP, this should be done
	 * infrequently, since retransmits of RPC requests should normally
	 * use the same xid.
	 */
	if (nmp == NULL) {
		timo.tv_usec = 0;
		if (clp == NULL)
			timo.tv_sec = NFSV4_UPCALLTIMEO;
		else
			timo.tv_sec = NFSV4_CALLBACKTIMEO;
	} else {
		if (nrp->nr_sotype != SOCK_DGRAM) {
			timo.tv_usec = 0;
			if ((nmp->nm_flag & NFSMNT_NFSV4))
				timo.tv_sec = INT_MAX;
			else
				timo.tv_sec = NFS_TCPTIMEO;
		} else {
			if (NFSHASSOFT(nmp)) {
				/*
				 * CLSET_RETRIES is set to 2, so this should be
				 * half of the total timeout required.
				 */
				timeo = nmp->nm_retry * nmp->nm_timeo / 2;
				if (timeo < 1)
					timeo = 1;
				timo.tv_sec = timeo / NFS_HZ;
				timo.tv_usec = (timeo % NFS_HZ) * 1000000 /
				    NFS_HZ;
			} else {
				/* For UDP hard mounts, use a large value. */
				timo.tv_sec = NFS_MAXTIMEO / NFS_HZ;
				timo.tv_usec = 0;
			}
		}

		if (rep != NULL) {
			rep->r_flags = 0;
			rep->r_nmp = nmp;
			/*
			 * Chain request into list of outstanding requests.
			 */
			NFSLOCKREQ();
			TAILQ_INSERT_TAIL(&nfsd_reqq, rep, r_chain);
			NFSUNLOCKREQ();
		}
	}

	nd->nd_mrep = NULL;
	if (clp != NULL && sep != NULL)
		stat = clnt_bck_call(nrp->nr_client, &ext, procnum,
		    nd->nd_mreq, &nd->nd_mrep, timo, sep->nfsess_xprt);
	else
		stat = CLNT_CALL_MBUF(nrp->nr_client, &ext, procnum,
		    nd->nd_mreq, &nd->nd_mrep, timo);
	NFSCL_DEBUG(2, "clnt call=%d\n", stat);

	if (rep != NULL) {
		/*
		 * RPC done, unlink the request.
		 */
		NFSLOCKREQ();
		TAILQ_REMOVE(&nfsd_reqq, rep, r_chain);
		NFSUNLOCKREQ();
	}

	/*
	 * If there was a successful reply and a tprintf msg.
	 * tprintf a response.
	 */
	if (stat == RPC_SUCCESS) {
		error = 0;
	} else if (stat == RPC_TIMEDOUT) {
		NFSINCRGLOBAL(nfsstatsv1.rpctimeouts);
		error = ETIMEDOUT;
	} else if (stat == RPC_VERSMISMATCH) {
		NFSINCRGLOBAL(nfsstatsv1.rpcinvalid);
		error = EOPNOTSUPP;
	} else if (stat == RPC_PROGVERSMISMATCH) {
		NFSINCRGLOBAL(nfsstatsv1.rpcinvalid);
		error = EPROTONOSUPPORT;
	} else if (stat == RPC_INTR) {
		error = EINTR;
	} else if (stat == RPC_CANTSEND || stat == RPC_CANTRECV ||
	     stat == RPC_SYSTEMERROR) {
		/* Check for a session slot that needs to be free'd. */
		if ((nd->nd_flag & (ND_NFSV41 | ND_HASSLOTID)) ==
		    (ND_NFSV41 | ND_HASSLOTID) && nmp != NULL &&
		    nd->nd_procnum != NFSPROC_NULL) {
			/*
			 * This should only occur when either the MDS or
			 * a client has an RPC against a DS fail.
			 * This happens because these cases use "soft"
			 * connections that can time out and fail.
			 * The slot used for this RPC is now in a
			 * non-deterministic state, but if the slot isn't
			 * free'd, threads can get stuck waiting for a slot.
			 */
			if (sep == NULL)
				sep = nfsmnt_mdssession(nmp);
			/*
			 * Bump the sequence# out of range, so that reuse of
			 * this slot will result in an NFSERR_SEQMISORDERED
			 * error and not a bogus cached RPC reply.
			 */
			mtx_lock(&sep->nfsess_mtx);
			sep->nfsess_slotseq[nd->nd_slotid] += 10;
			mtx_unlock(&sep->nfsess_mtx);
			/* And free the slot. */
			nfsv4_freeslot(sep, nd->nd_slotid);
		}
		NFSINCRGLOBAL(nfsstatsv1.rpcinvalid);
		error = ENXIO;
	} else {
		NFSINCRGLOBAL(nfsstatsv1.rpcinvalid);
		error = EACCES;
	}
	if (error) {
		m_freem(nd->nd_mreq);
		if (usegssname == 0)
			AUTH_DESTROY(auth);
		if (rep != NULL)
			free(rep, M_NFSDREQ);
		if (set_sigset)
			newnfs_restore_sigmask(td, &oldset);
		return (error);
	}

	KASSERT(nd->nd_mrep != NULL, ("mrep shouldn't be NULL if no error\n"));

	/*
	 * Search for any mbufs that are not a multiple of 4 bytes long
	 * or with m_data not longword aligned.
	 * These could cause pointer alignment problems, so copy them to
	 * well aligned mbufs.
	 */
	newnfs_realign(&nd->nd_mrep, M_WAITOK);
	nd->nd_md = nd->nd_mrep;
	nd->nd_dpos = NFSMTOD(nd->nd_md, caddr_t);
	nd->nd_repstat = 0;
	if (nd->nd_procnum != NFSPROC_NULL &&
	    nd->nd_procnum != NFSV4PROC_CBNULL) {
		/* If sep == NULL, set it to the default in nmp. */
		if (sep == NULL && nmp != NULL)
			sep = nfsmnt_mdssession(nmp);
		/*
		 * and now the actual NFS xdr.
		 */
		NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
		nd->nd_repstat = fxdr_unsigned(u_int32_t, *tl);
		if (nd->nd_repstat >= 10000)
			NFSCL_DEBUG(1, "proc=%d reps=%d\n", (int)nd->nd_procnum,
			    (int)nd->nd_repstat);

		/*
		 * Get rid of the tag, return count and SEQUENCE result for
		 * NFSv4.
		 */
		if ((nd->nd_flag & ND_NFSV4) != 0) {
			NFSM_DISSECT(tl, u_int32_t *, NFSX_UNSIGNED);
			i = fxdr_unsigned(int, *tl);
			error = nfsm_advance(nd, NFSM_RNDUP(i), -1);
			if (error)
				goto nfsmout;
			NFSM_DISSECT(tl, u_int32_t *, 3 * NFSX_UNSIGNED);
			opcnt = fxdr_unsigned(int, *tl++);
			i = fxdr_unsigned(int, *tl++);
			j = fxdr_unsigned(int, *tl);
			if (j >= 10000)
				NFSCL_DEBUG(1, "fop=%d fst=%d\n", i, j);
			/*
			 * If the first op is Sequence, free up the slot.
			 */
			if ((nmp != NULL && i == NFSV4OP_SEQUENCE && j != 0) ||
			    (clp != NULL && i == NFSV4OP_CBSEQUENCE && j != 0))
				NFSCL_DEBUG(1, "failed seq=%d\n", j);
			if (((nmp != NULL && i == NFSV4OP_SEQUENCE && j == 0) ||
			    (clp != NULL && i == NFSV4OP_CBSEQUENCE &&
			    j == 0)) && sep != NULL) {
				if (i == NFSV4OP_SEQUENCE)
					NFSM_DISSECT(tl, uint32_t *,
					    NFSX_V4SESSIONID +
					    5 * NFSX_UNSIGNED);
				else
					NFSM_DISSECT(tl, uint32_t *,
					    NFSX_V4SESSIONID +
					    4 * NFSX_UNSIGNED);
				mtx_lock(&sep->nfsess_mtx);
				if (bcmp(tl, sep->nfsess_sessionid,
				    NFSX_V4SESSIONID) == 0) {
					tl += NFSX_V4SESSIONID / NFSX_UNSIGNED;
					retseq = fxdr_unsigned(uint32_t, *tl++);
					slot = fxdr_unsigned(int, *tl++);
					freeslot = slot;
					if (retseq != sep->nfsess_slotseq[slot])
						printf("retseq diff 0x%x\n",
						    retseq);
					retval = fxdr_unsigned(uint32_t, *++tl);
					if ((retval + 1) < sep->nfsess_foreslots
					    )
						sep->nfsess_foreslots = (retval
						    + 1);
					else if ((retval + 1) >
					    sep->nfsess_foreslots)
						sep->nfsess_foreslots = (retval
						    < 64) ? (retval + 1) : 64;
				}
				mtx_unlock(&sep->nfsess_mtx);

				/* Grab the op and status for the next one. */
				if (opcnt > 1) {
					NFSM_DISSECT(tl, uint32_t *,
					    2 * NFSX_UNSIGNED);
					i = fxdr_unsigned(int, *tl++);
					j = fxdr_unsigned(int, *tl);
				}
			}
		}
		if (nd->nd_repstat != 0) {
			if (nd->nd_repstat == NFSERR_BADSESSION &&
			    nmp != NULL && dssep == NULL &&
			    (nd->nd_flag & ND_NFSV41) != 0) {
				/*
				 * If this is a client side MDS RPC, mark
				 * the MDS session defunct and initiate
				 * recovery, as required.
				 * The nfsess_defunct field is protected by
				 * the NFSLOCKMNT()/nm_mtx lock and not the
				 * nfsess_mtx lock to simplify its handling,
				 * for the MDS session. This lock is also
				 * sufficient for nfsess_sessionid, since it
				 * never changes in the structure.
				 */
				NFSCL_DEBUG(1, "Got badsession\n");
				NFSLOCKCLSTATE();
				NFSLOCKMNT(nmp);
				sep = NFSMNT_MDSSESSION(nmp);
				if (bcmp(sep->nfsess_sessionid, nd->nd_sequence,
				    NFSX_V4SESSIONID) == 0) {
					/* Initiate recovery. */
					sep->nfsess_defunct = 1;
					NFSCL_DEBUG(1, "Marked defunct\n");
					if (nmp->nm_clp != NULL) {
						nmp->nm_clp->nfsc_flags |=
						    NFSCLFLAGS_RECOVER;
						wakeup(nmp->nm_clp);
					}
				}
				NFSUNLOCKCLSTATE();
				/*
				 * Sleep for up to 1sec waiting for a new
				 * session.
				 */
				mtx_sleep(&nmp->nm_sess, &nmp->nm_mtx, PZERO,
				    "nfsbadsess", hz);
				/*
				 * Get the session again, in case a new one
				 * has been created during the sleep.
				 */
				sep = NFSMNT_MDSSESSION(nmp);
				NFSUNLOCKMNT(nmp);
				if ((nd->nd_flag & ND_LOOPBADSESS) != 0) {
					reterr = nfsv4_sequencelookup(nmp, sep,
					    &slotpos, &maxslot, &slotseq,
					    sessionid);
					if (reterr == 0) {
						/* Fill in new session info. */
						NFSCL_DEBUG(1,
						  "Filling in new sequence\n");
						tl = nd->nd_sequence;
						bcopy(sessionid, tl,
						    NFSX_V4SESSIONID);
						tl += NFSX_V4SESSIONID /
						    NFSX_UNSIGNED;
						*tl++ = txdr_unsigned(slotseq);
						*tl++ = txdr_unsigned(slotpos);
						*tl = txdr_unsigned(maxslot);
					}
					if (reterr == NFSERR_BADSESSION ||
					    reterr == 0) {
						NFSCL_DEBUG(1,
						    "Badsession looping\n");
						m_freem(nd->nd_mrep);
						nd->nd_mrep = NULL;
						goto tryagain;
					}
					nd->nd_repstat = reterr;
					NFSCL_DEBUG(1, "Got err=%d\n", reterr);
				}
			}
			/*
			 * When clp != NULL, it is a callback and all
			 * callback operations can be retried for NFSERR_DELAY.
			 */
			if (((nd->nd_repstat == NFSERR_DELAY ||
			      nd->nd_repstat == NFSERR_GRACE) &&
			     (nd->nd_flag & ND_NFSV4) && (clp != NULL ||
			     (nd->nd_procnum != NFSPROC_DELEGRETURN &&
			     nd->nd_procnum != NFSPROC_SETATTR &&
			     nd->nd_procnum != NFSPROC_READ &&
			     nd->nd_procnum != NFSPROC_READDS &&
			     nd->nd_procnum != NFSPROC_WRITE &&
			     nd->nd_procnum != NFSPROC_WRITEDS &&
			     nd->nd_procnum != NFSPROC_OPEN &&
			     nd->nd_procnum != NFSPROC_CREATE &&
			     nd->nd_procnum != NFSPROC_OPENCONFIRM &&
			     nd->nd_procnum != NFSPROC_OPENDOWNGRADE &&
			     nd->nd_procnum != NFSPROC_CLOSE &&
			     nd->nd_procnum != NFSPROC_LOCK &&
			     nd->nd_procnum != NFSPROC_LOCKU))) ||
			    (nd->nd_repstat == NFSERR_DELAY &&
			     (nd->nd_flag & ND_NFSV4) == 0) ||
			    nd->nd_repstat == NFSERR_RESOURCE) {
				if (trylater_delay > NFS_TRYLATERDEL)
					trylater_delay = NFS_TRYLATERDEL;
				waituntil = NFSD_MONOSEC + trylater_delay;
				while (NFSD_MONOSEC < waituntil)
					(void) nfs_catnap(PZERO, 0, "nfstry");
				trylater_delay *= 2;
				if (slot != -1) {
					mtx_lock(&sep->nfsess_mtx);
					sep->nfsess_slotseq[slot]++;
					*nd->nd_slotseq = txdr_unsigned(
					    sep->nfsess_slotseq[slot]);
					mtx_unlock(&sep->nfsess_mtx);
				}
				m_freem(nd->nd_mrep);
				nd->nd_mrep = NULL;
				goto tryagain;
			}

			/*
			 * If the File Handle was stale, invalidate the
			 * lookup cache, just in case.
			 * (vp != NULL implies a client side call)
			 */
			if (nd->nd_repstat == ESTALE && vp != NULL) {
				cache_purge(vp);
				if (ncl_call_invalcaches != NULL)
					(*ncl_call_invalcaches)(vp);
			}
		}
		if ((nd->nd_flag & ND_NFSV4) != 0) {
			/* Free the slot, as required. */
			if (freeslot != -1)
				nfsv4_freeslot(sep, freeslot);
			/*
			 * If this op is Putfh, throw its results away.
			 */
			if (j >= 10000)
				NFSCL_DEBUG(1, "nop=%d nst=%d\n", i, j);
			if (nmp != NULL && i == NFSV4OP_PUTFH && j == 0) {
				NFSM_DISSECT(tl,u_int32_t *,2 * NFSX_UNSIGNED);
				i = fxdr_unsigned(int, *tl++);
				j = fxdr_unsigned(int, *tl);
				if (j >= 10000)
					NFSCL_DEBUG(1, "n2op=%d n2st=%d\n", i,
					    j);
				/*
				 * All Compounds that do an Op that must
				 * be in sequence consist of NFSV4OP_PUTFH
				 * followed by one of these. As such, we
				 * can determine if the seqid# should be
				 * incremented, here.
				 */
				if ((i == NFSV4OP_OPEN ||
				     i == NFSV4OP_OPENCONFIRM ||
				     i == NFSV4OP_OPENDOWNGRADE ||
				     i == NFSV4OP_CLOSE ||
				     i == NFSV4OP_LOCK ||
				     i == NFSV4OP_LOCKU) &&
				    (j == 0 ||
				     (j != NFSERR_STALECLIENTID &&
				      j != NFSERR_STALESTATEID &&
				      j != NFSERR_BADSTATEID &&
				      j != NFSERR_BADSEQID &&
				      j != NFSERR_BADXDR &&	 
				      j != NFSERR_RESOURCE &&
				      j != NFSERR_NOFILEHANDLE)))		 
					nd->nd_flag |= ND_INCRSEQID;
			}
			/*
			 * If this op's status is non-zero, mark
			 * that there is no more data to process.
			 * The exception is Setattr, which always has xdr
			 * when it has failed.
			 */
			if (j != 0 && i != NFSV4OP_SETATTR)
				nd->nd_flag |= ND_NOMOREDATA;

			/*
			 * If R_DONTRECOVER is set, replace the stale error
			 * reply, so that recovery isn't initiated.
			 */
			if ((nd->nd_repstat == NFSERR_STALECLIENTID ||
			     nd->nd_repstat == NFSERR_BADSESSION ||
			     nd->nd_repstat == NFSERR_STALESTATEID) &&
			    rep != NULL && (rep->r_flags & R_DONTRECOVER))
				nd->nd_repstat = NFSERR_STALEDONTRECOVER;
		}
	}

#ifdef KDTRACE_HOOKS
	if (nmp != NULL && dtrace_nfscl_nfs234_done_probe != NULL) {
		uint32_t probe_id;
		int probe_procnum;

		if (nd->nd_flag & ND_NFSV4) {
			probe_id = nfscl_nfs4_done_probes[nd->nd_procnum];
			probe_procnum = nd->nd_procnum;
		} else if (nd->nd_flag & ND_NFSV3) {
			probe_id = nfscl_nfs3_done_probes[procnum];
			probe_procnum = procnum;
		} else {
			probe_id = nfscl_nfs2_done_probes[nd->nd_procnum];
			probe_procnum = procnum;
		}
		if (probe_id != 0)
			(dtrace_nfscl_nfs234_done_probe)(probe_id, vp,
			    nd->nd_mreq, cred, probe_procnum, 0);
	}
#endif

	m_freem(nd->nd_mreq);
	if (usegssname == 0)
		AUTH_DESTROY(auth);
	if (rep != NULL)
		free(rep, M_NFSDREQ);
	if (set_sigset)
		newnfs_restore_sigmask(td, &oldset);
	return (0);
nfsmout:
	mbuf_freem(nd->nd_mrep);
	mbuf_freem(nd->nd_mreq);
	if (usegssname == 0)
		AUTH_DESTROY(auth);
	if (rep != NULL)
		free(rep, M_NFSDREQ);
	if (set_sigset)
		newnfs_restore_sigmask(td, &oldset);
	return (error);
}

/*
 * Mark all of an nfs mount's outstanding requests with R_SOFTTERM and
 * wait for all requests to complete. This is used by forced unmounts
 * to terminate any outstanding RPCs.
 */
int
newnfs_nmcancelreqs(struct nfsmount *nmp)
{
	struct nfsclds *dsp;
	struct __rpc_client *cl;

	if (nmp->nm_sockreq.nr_client != NULL)
		CLNT_CLOSE(nmp->nm_sockreq.nr_client);
lookformore:
	NFSLOCKMNT(nmp);
	TAILQ_FOREACH(dsp, &nmp->nm_sess, nfsclds_list) {
		NFSLOCKDS(dsp);
		if (dsp != TAILQ_FIRST(&nmp->nm_sess) &&
		    (dsp->nfsclds_flags & NFSCLDS_CLOSED) == 0 &&
		    dsp->nfsclds_sockp != NULL &&
		    dsp->nfsclds_sockp->nr_client != NULL) {
			dsp->nfsclds_flags |= NFSCLDS_CLOSED;
			cl = dsp->nfsclds_sockp->nr_client;
			NFSUNLOCKDS(dsp);
			NFSUNLOCKMNT(nmp);
			CLNT_CLOSE(cl);
			goto lookformore;
		}
		NFSUNLOCKDS(dsp);
	}
	NFSUNLOCKMNT(nmp);
	return (0);
}

/*
 * Any signal that can interrupt an NFS operation in an intr mount
 * should be added to this set. SIGSTOP and SIGKILL cannot be masked.
 */
int newnfs_sig_set[] = {
	SIGINT,
	SIGTERM,
	SIGHUP,
	SIGKILL,
	SIGQUIT
};

/*
 * Check to see if one of the signals in our subset is pending on
 * the process (in an intr mount).
 */
static int
nfs_sig_pending(sigset_t set)
{
	int i;
	
	for (i = 0 ; i < nitems(newnfs_sig_set); i++)
		if (SIGISMEMBER(set, newnfs_sig_set[i]))
			return (1);
	return (0);
}
 
/*
 * The set/restore sigmask functions are used to (temporarily) overwrite
 * the thread td_sigmask during an RPC call (for example). These are also
 * used in other places in the NFS client that might tsleep().
 */
void
newnfs_set_sigmask(struct thread *td, sigset_t *oldset)
{
	sigset_t newset;
	int i;
	struct proc *p;
	
	SIGFILLSET(newset);
	if (td == NULL)
		td = curthread; /* XXX */
	p = td->td_proc;
	/* Remove the NFS set of signals from newset */
	PROC_LOCK(p);
	mtx_lock(&p->p_sigacts->ps_mtx);
	for (i = 0 ; i < nitems(newnfs_sig_set); i++) {
		/*
		 * But make sure we leave the ones already masked
		 * by the process, ie. remove the signal from the
		 * temporary signalmask only if it wasn't already
		 * in p_sigmask.
		 */
		if (!SIGISMEMBER(td->td_sigmask, newnfs_sig_set[i]) &&
		    !SIGISMEMBER(p->p_sigacts->ps_sigignore, newnfs_sig_set[i]))
			SIGDELSET(newset, newnfs_sig_set[i]);
	}
	mtx_unlock(&p->p_sigacts->ps_mtx);
	kern_sigprocmask(td, SIG_SETMASK, &newset, oldset,
	    SIGPROCMASK_PROC_LOCKED);
	PROC_UNLOCK(p);
}

void
newnfs_restore_sigmask(struct thread *td, sigset_t *set)
{
	if (td == NULL)
		td = curthread; /* XXX */
	kern_sigprocmask(td, SIG_SETMASK, set, NULL, 0);
}

/*
 * NFS wrapper to msleep(), that shoves a new p_sigmask and restores the
 * old one after msleep() returns.
 */
int
newnfs_msleep(struct thread *td, void *ident, struct mtx *mtx, int priority, char *wmesg, int timo)
{
	sigset_t oldset;
	int error;

	if ((priority & PCATCH) == 0)
		return msleep(ident, mtx, priority, wmesg, timo);
	if (td == NULL)
		td = curthread; /* XXX */
	newnfs_set_sigmask(td, &oldset);
	error = msleep(ident, mtx, priority, wmesg, timo);
	newnfs_restore_sigmask(td, &oldset);
	return (error);
}

/*
 * Test for a termination condition pending on the process.
 * This is used for NFSMNT_INT mounts.
 */
int
newnfs_sigintr(struct nfsmount *nmp, struct thread *td)
{
	struct proc *p;
	sigset_t tmpset;
	
	/* Terminate all requests while attempting a forced unmount. */
	if (NFSCL_FORCEDISM(nmp->nm_mountp))
		return (EIO);
	if (!(nmp->nm_flag & NFSMNT_INT))
		return (0);
	if (td == NULL)
		return (0);
	p = td->td_proc;
	PROC_LOCK(p);
	tmpset = p->p_siglist;
	SIGSETOR(tmpset, td->td_siglist);
	SIGSETNAND(tmpset, td->td_sigmask);
	mtx_lock(&p->p_sigacts->ps_mtx);
	SIGSETNAND(tmpset, p->p_sigacts->ps_sigignore);
	mtx_unlock(&p->p_sigacts->ps_mtx);
	if ((SIGNOTEMPTY(p->p_siglist) || SIGNOTEMPTY(td->td_siglist))
	    && nfs_sig_pending(tmpset)) {
		PROC_UNLOCK(p);
		return (EINTR);
	}
	PROC_UNLOCK(p);
	return (0);
}

static int
nfs_msg(struct thread *td, const char *server, const char *msg, int error)
{
	struct proc *p;

	p = td ? td->td_proc : NULL;
	if (error) {
		tprintf(p, LOG_INFO, "nfs server %s: %s, error %d\n",
		    server, msg, error);
	} else {
		tprintf(p, LOG_INFO, "nfs server %s: %s\n", server, msg);
	}
	return (0);
}

static void
nfs_down(struct nfsmount *nmp, struct thread *td, const char *msg,
    int error, int flags)
{
	if (nmp == NULL)
		return;
	mtx_lock(&nmp->nm_mtx);
	if ((flags & NFSSTA_TIMEO) && !(nmp->nm_state & NFSSTA_TIMEO)) {
		nmp->nm_state |= NFSSTA_TIMEO;
		mtx_unlock(&nmp->nm_mtx);
		vfs_event_signal(&nmp->nm_mountp->mnt_stat.f_fsid,
		    VQ_NOTRESP, 0);
	} else
		mtx_unlock(&nmp->nm_mtx);
	mtx_lock(&nmp->nm_mtx);
	if ((flags & NFSSTA_LOCKTIMEO) && !(nmp->nm_state & NFSSTA_LOCKTIMEO)) {
		nmp->nm_state |= NFSSTA_LOCKTIMEO;
		mtx_unlock(&nmp->nm_mtx);
		vfs_event_signal(&nmp->nm_mountp->mnt_stat.f_fsid,
		    VQ_NOTRESPLOCK, 0);
	} else
		mtx_unlock(&nmp->nm_mtx);
	nfs_msg(td, nmp->nm_mountp->mnt_stat.f_mntfromname, msg, error);
}

static void
nfs_up(struct nfsmount *nmp, struct thread *td, const char *msg,
    int flags, int tprintfmsg)
{
	if (nmp == NULL)
		return;
	if (tprintfmsg) {
		nfs_msg(td, nmp->nm_mountp->mnt_stat.f_mntfromname, msg, 0);
	}

	mtx_lock(&nmp->nm_mtx);
	if ((flags & NFSSTA_TIMEO) && (nmp->nm_state & NFSSTA_TIMEO)) {
		nmp->nm_state &= ~NFSSTA_TIMEO;
		mtx_unlock(&nmp->nm_mtx);
		vfs_event_signal(&nmp->nm_mountp->mnt_stat.f_fsid,
		    VQ_NOTRESP, 1);
	} else
		mtx_unlock(&nmp->nm_mtx);
	
	mtx_lock(&nmp->nm_mtx);
	if ((flags & NFSSTA_LOCKTIMEO) && (nmp->nm_state & NFSSTA_LOCKTIMEO)) {
		nmp->nm_state &= ~NFSSTA_LOCKTIMEO;
		mtx_unlock(&nmp->nm_mtx);
		vfs_event_signal(&nmp->nm_mountp->mnt_stat.f_fsid,
		    VQ_NOTRESPLOCK, 1);
	} else
		mtx_unlock(&nmp->nm_mtx);
}

