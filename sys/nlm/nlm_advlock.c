/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Isilon Inc http://www.isilon.com/
 * Authors: Doug Rabson <dfr@rabson.org>
 * Developed with Red Inc: Alfred Perlstein <alfred@freebsd.org>
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

#include <sys/param.h>
#include <sys/fcntl.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/lockf.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/systm.h>
#include <sys/unistd.h>
#include <sys/vnode.h>

#include <nfs/nfsproto.h>
#include <nfsclient/nfs.h>
#include <nfsclient/nfsmount.h>

#include <nlm/nlm_prot.h>
#include <nlm/nlm.h>

/*
 * We need to keep track of the svid values used for F_FLOCK locks.
 */
struct nlm_file_svid {
	int		ns_refs;	/* thread count + 1 if active */
	int		ns_svid;	/* on-the-wire SVID for this file */
	struct ucred	*ns_ucred;	/* creds to use for lock recovery */
	void		*ns_id;		/* local struct file pointer */
	bool_t		ns_active;	/* TRUE if we own a lock */
	LIST_ENTRY(nlm_file_svid) ns_link;
};
LIST_HEAD(nlm_file_svid_list, nlm_file_svid);

#define NLM_SVID_HASH_SIZE	256
struct nlm_file_svid_list nlm_file_svids[NLM_SVID_HASH_SIZE];

struct mtx nlm_svid_lock;
static struct unrhdr *nlm_svid_allocator;
static volatile u_int nlm_xid = 1;

static int nlm_setlock(struct nlm_host *host, struct rpc_callextra *ext,
    rpcvers_t vers, struct timeval *timo, int retries,
    struct vnode *vp, int op, struct flock *fl, int flags,
    int svid, size_t fhlen, void *fh, off_t size, bool_t reclaim);
static int nlm_clearlock(struct nlm_host *host,  struct rpc_callextra *ext,
    rpcvers_t vers, struct timeval *timo, int retries,
    struct vnode *vp, int op, struct flock *fl, int flags,
    int svid, size_t fhlen, void *fh, off_t size);
static int nlm_getlock(struct nlm_host *host, struct rpc_callextra *ext,
    rpcvers_t vers, struct timeval *timo, int retries,
    struct vnode *vp, int op, struct flock *fl, int flags,
    int svid, size_t fhlen, void *fh, off_t size);
static int nlm_map_status(nlm4_stats stat);
static struct nlm_file_svid *nlm_find_svid(void *id);
static void nlm_free_svid(struct nlm_file_svid *nf);
static int nlm_init_lock(struct flock *fl, int flags, int svid,
    rpcvers_t vers, size_t fhlen, void *fh, off_t size,
    struct nlm4_lock *lock, char oh_space[32]);

static void
nlm_client_init(void *dummy)
{
	int i;

	mtx_init(&nlm_svid_lock, "NLM svid lock", NULL, MTX_DEF);
	/* pid_max cannot be greater than PID_MAX */
	nlm_svid_allocator = new_unrhdr(PID_MAX + 2, INT_MAX, &nlm_svid_lock);
	for (i = 0; i < NLM_SVID_HASH_SIZE; i++)
		LIST_INIT(&nlm_file_svids[i]);
}
SYSINIT(nlm_client_init, SI_SUB_LOCK, SI_ORDER_FIRST, nlm_client_init, NULL);

static int
nlm_msg(struct thread *td, const char *server, const char *msg, int error)
{
	struct proc *p;

	p = td ? td->td_proc : NULL;
	if (error) {
		tprintf(p, LOG_INFO, "nfs server %s: %s, error %d\n", server,
		    msg, error);
	} else {
		tprintf(p, LOG_INFO, "nfs server %s: %s\n", server, msg);
	}
	return (0);
}

struct nlm_feedback_arg {
	bool_t	nf_printed;
	struct nfsmount *nf_nmp;
};

static void
nlm_down(struct nlm_feedback_arg *nf, struct thread *td,
    const char *msg, int error)
{
	struct nfsmount *nmp = nf->nf_nmp;

	if (nmp == NULL)
		return;
	mtx_lock(&nmp->nm_mtx);
	if (!(nmp->nm_state & NFSSTA_LOCKTIMEO)) {
		nmp->nm_state |= NFSSTA_LOCKTIMEO;
		mtx_unlock(&nmp->nm_mtx);
		vfs_event_signal(&nmp->nm_mountp->mnt_stat.f_fsid,
		    VQ_NOTRESPLOCK, 0);
	} else {
		mtx_unlock(&nmp->nm_mtx);
	}

	nf->nf_printed = TRUE;
	nlm_msg(td, nmp->nm_mountp->mnt_stat.f_mntfromname, msg, error);
}

static void
nlm_up(struct nlm_feedback_arg *nf, struct thread *td,
    const char *msg)
{
	struct nfsmount *nmp = nf->nf_nmp;

	if (!nf->nf_printed)
		return;

	nlm_msg(td, nmp->nm_mountp->mnt_stat.f_mntfromname, msg, 0);

	mtx_lock(&nmp->nm_mtx);
	if (nmp->nm_state & NFSSTA_LOCKTIMEO) {
		nmp->nm_state &= ~NFSSTA_LOCKTIMEO;
		mtx_unlock(&nmp->nm_mtx);
		vfs_event_signal(&nmp->nm_mountp->mnt_stat.f_fsid,
		    VQ_NOTRESPLOCK, 1);
	} else {
		mtx_unlock(&nmp->nm_mtx);
	}
}

static void
nlm_feedback(int type, int proc, void *arg)
{
	struct thread *td = curthread;
	struct nlm_feedback_arg *nf = (struct nlm_feedback_arg *) arg;

	switch (type) {
	case FEEDBACK_REXMIT2:
	case FEEDBACK_RECONNECT:
		nlm_down(nf, td, "lockd not responding", 0);
		break;

	case FEEDBACK_OK:
		nlm_up(nf, td, "lockd is alive again");
		break;
	}
}

/*
 * nlm_advlock --
 *      NFS advisory byte-level locks.
 */
static int
nlm_advlock_internal(struct vnode *vp, void *id, int op, struct flock *fl,
    int flags, bool_t reclaim, bool_t unlock_vp)
{
	struct thread *td = curthread;
	struct nfsmount *nmp;
	off_t size;
	size_t fhlen;
	union nfsfh fh;
	struct sockaddr *sa;
	struct sockaddr_storage ss;
	char *servername;
	struct timeval timo;
	int retries;
	rpcvers_t vers;
	struct nlm_host *host;
	struct rpc_callextra ext;
	struct nlm_feedback_arg nf;
	AUTH *auth;
	struct ucred *cred, *cred1;
	struct nlm_file_svid *ns;
	int svid;
	int error;
	int is_v3;

	ASSERT_VOP_LOCKED(vp, "nlm_advlock_1");

	servername = malloc(MNAMELEN, M_TEMP, M_WAITOK); /* XXXKIB vp locked */
	nmp = VFSTONFS(vp->v_mount);
	/*
	 * Push any pending writes to the server and flush our cache
	 * so that if we are contending with another machine for a
	 * file, we get whatever they wrote and vice-versa.
	 */
	if (op == F_SETLK || op == F_UNLCK)
		nmp->nm_vinvalbuf(vp, V_SAVE, td, 1);

	strcpy(servername, nmp->nm_hostname);
	nmp->nm_getinfo(vp, fh.fh_bytes, &fhlen, &ss, &is_v3, &size, &timo);
	sa = (struct sockaddr *) &ss;
	if (is_v3 != 0)
		vers = NLM_VERS4;
	else
		vers = NLM_VERS;

	if (nmp->nm_flag & NFSMNT_SOFT)
		retries = nmp->nm_retry;
	else
		retries = INT_MAX;

	/*
	 * We need to switch to mount-point creds so that we can send
	 * packets from a privileged port.  Reference mnt_cred and
	 * switch to them before unlocking the vnode, since mount
	 * point could be unmounted right after unlock.
	 */
	cred = td->td_ucred;
	td->td_ucred = vp->v_mount->mnt_cred;
	crhold(td->td_ucred);
	if (unlock_vp)
		VOP_UNLOCK(vp, 0);

	host = nlm_find_host_by_name(servername, sa, vers);
	auth = authunix_create(cred);
	memset(&ext, 0, sizeof(ext));

	nf.nf_printed = FALSE;
	nf.nf_nmp = nmp;
	ext.rc_auth = auth;

	ext.rc_feedback = nlm_feedback;
	ext.rc_feedback_arg = &nf;
	ext.rc_timers = NULL;

	ns = NULL;
	if (flags & F_FLOCK) {
		ns = nlm_find_svid(id);
		KASSERT(fl->l_start == 0 && fl->l_len == 0,
		    ("F_FLOCK lock requests must be whole-file locks"));
		if (!ns->ns_ucred) {
			/*
			 * Remember the creds used for locking in case
			 * we need to recover the lock later.
			 */
			ns->ns_ucred = crdup(cred);
		}
		svid = ns->ns_svid;
	} else if (flags & F_REMOTE) {
		/*
		 * If we are recovering after a server restart or
		 * trashing locks on a force unmount, use the same
		 * svid as last time.
		 */
		svid = fl->l_pid;
	} else {
		svid = ((struct proc *) id)->p_pid;
	}

	switch(op) {
	case F_SETLK:
		if ((flags & (F_FLOCK|F_WAIT)) == (F_FLOCK|F_WAIT)
		    && fl->l_type == F_WRLCK) {
			/*
			 * The semantics for flock(2) require that any
			 * shared lock on the file must be released
			 * before an exclusive lock is granted. The
			 * local locking code interprets this by
			 * unlocking the file before sleeping on a
			 * blocked exclusive lock request. We
			 * approximate this by first attempting
			 * non-blocking and if that fails, we unlock
			 * the file and block.
			 */
			error = nlm_setlock(host, &ext, vers, &timo, retries,
			    vp, F_SETLK, fl, flags & ~F_WAIT,
			    svid, fhlen, &fh.fh_bytes, size, reclaim);
			if (error == EAGAIN) {
				fl->l_type = F_UNLCK;
				error = nlm_clearlock(host, &ext, vers, &timo,
				    retries, vp, F_UNLCK, fl, flags,
				    svid, fhlen, &fh.fh_bytes, size);
				fl->l_type = F_WRLCK;
				if (!error) {
					mtx_lock(&nlm_svid_lock);
					if (ns->ns_active) {
						ns->ns_refs--;
						ns->ns_active = FALSE;
					}
					mtx_unlock(&nlm_svid_lock);
					flags |= F_WAIT;
					error = nlm_setlock(host, &ext, vers,
					    &timo, retries, vp, F_SETLK, fl,
					    flags, svid, fhlen, &fh.fh_bytes,
					    size, reclaim);
				}
			}
		} else {
			error = nlm_setlock(host, &ext, vers, &timo, retries,
			    vp, op, fl, flags, svid, fhlen, &fh.fh_bytes,
			    size, reclaim);
		}
		if (!error && ns) {
			mtx_lock(&nlm_svid_lock);
			if (!ns->ns_active) {
				/*
				 * Add one to the reference count to
				 * hold onto the SVID for the lifetime
				 * of the lock. Note that since
				 * F_FLOCK only supports whole-file
				 * locks, there can only be one active
				 * lock for this SVID.
				 */
				ns->ns_refs++;
				ns->ns_active = TRUE;
			}
			mtx_unlock(&nlm_svid_lock);
		}
		break;

	case F_UNLCK:
		error = nlm_clearlock(host, &ext, vers, &timo, retries,
		    vp, op, fl, flags, svid, fhlen, &fh.fh_bytes, size);
		if (!error && ns) {
			mtx_lock(&nlm_svid_lock);
			if (ns->ns_active) {
				ns->ns_refs--;
				ns->ns_active = FALSE;
			}
			mtx_unlock(&nlm_svid_lock);
		}
		break;

	case F_GETLK:
		error = nlm_getlock(host, &ext, vers, &timo, retries,
		    vp, op, fl, flags, svid, fhlen, &fh.fh_bytes, size);
		break;

	default:
		error = EINVAL;
		break;
	}

	if (ns)
		nlm_free_svid(ns);

	cred1 = td->td_ucred;
	td->td_ucred = cred;
	crfree(cred1);
	AUTH_DESTROY(auth);

	nlm_host_release(host);
	free(servername, M_TEMP);
	return (error);
}

int
nlm_advlock(struct vop_advlock_args *ap)
{

	return (nlm_advlock_internal(ap->a_vp, ap->a_id, ap->a_op, ap->a_fl,
		ap->a_flags, FALSE, TRUE));
}

/*
 * Set the creds of td to the creds of the given lock's owner. The new
 * creds reference count will be incremented via crhold. The caller is
 * responsible for calling crfree and restoring td's original creds.
 */
static void
nlm_set_creds_for_lock(struct thread *td, struct flock *fl)
{
	int i;
	struct nlm_file_svid *ns;
	struct proc *p;
	struct ucred *cred;

	cred = NULL;
	if (fl->l_pid > PID_MAX) {
		/*
		 * If this was originally a F_FLOCK-style lock, we
		 * recorded the creds used when it was originally
		 * locked in the nlm_file_svid structure.
		 */
		mtx_lock(&nlm_svid_lock);
		for (i = 0; i < NLM_SVID_HASH_SIZE; i++) {
			for (ns = LIST_FIRST(&nlm_file_svids[i]); ns;
			     ns = LIST_NEXT(ns, ns_link)) {
				if (ns->ns_svid == fl->l_pid) {
					cred = crhold(ns->ns_ucred);
					break;
				}
			}
		}
		mtx_unlock(&nlm_svid_lock);
	} else {
		/*
		 * This lock is owned by a process. Get a reference to
		 * the process creds.
		 */
		p = pfind(fl->l_pid);
		if (p) {
			cred = crhold(p->p_ucred);
			PROC_UNLOCK(p);
		}
	}

	/*
	 * If we can't find a cred, fall back on the recovery
	 * thread's cred.
	 */
	if (!cred) {
		cred = crhold(td->td_ucred);
	}

	td->td_ucred = cred;
}

static int
nlm_reclaim_free_lock(struct vnode *vp, struct flock *fl, void *arg)
{
	struct flock newfl;
	struct thread *td = curthread;
	struct ucred *oldcred;
	int error;

	newfl = *fl;
	newfl.l_type = F_UNLCK;

	oldcred = td->td_ucred;
	nlm_set_creds_for_lock(td, &newfl);

	error = nlm_advlock_internal(vp, NULL, F_UNLCK, &newfl, F_REMOTE,
	    FALSE, FALSE);

	crfree(td->td_ucred);
	td->td_ucred = oldcred;

	return (error);
}

int
nlm_reclaim(struct vop_reclaim_args *ap)
{

	nlm_cancel_wait(ap->a_vp);
	lf_iteratelocks_vnode(ap->a_vp, nlm_reclaim_free_lock, NULL);
	return (0);
}

struct nlm_recovery_context {
	struct nlm_host	*nr_host;	/* host we are recovering */
	int		nr_state;	/* remote NSM state for recovery */
};

static int
nlm_client_recover_lock(struct vnode *vp, struct flock *fl, void *arg)
{
	struct nlm_recovery_context *nr = (struct nlm_recovery_context *) arg;
	struct thread *td = curthread;
	struct ucred *oldcred;
	int state, error;

	/*
	 * If the remote NSM state changes during recovery, the host
	 * must have rebooted a second time. In that case, we must
	 * restart the recovery.
	 */
	state = nlm_host_get_state(nr->nr_host);
	if (nr->nr_state != state)
		return (ERESTART);

	error = vn_lock(vp, LK_SHARED);
	if (error)
		return (error);

	oldcred = td->td_ucred;
	nlm_set_creds_for_lock(td, fl);

	error = nlm_advlock_internal(vp, NULL, F_SETLK, fl, F_REMOTE,
	    TRUE, TRUE);

	crfree(td->td_ucred);
	td->td_ucred = oldcred;

	return (error);
}

void
nlm_client_recovery(struct nlm_host *host)
{
	struct nlm_recovery_context nr;
	int sysid, error;

	sysid = NLM_SYSID_CLIENT | nlm_host_get_sysid(host);
	do {
		nr.nr_host = host;
		nr.nr_state = nlm_host_get_state(host);
		error = lf_iteratelocks_sysid(sysid,
		    nlm_client_recover_lock, &nr);
	} while (error == ERESTART);
}

static void
nlm_convert_to_nlm_lock(struct nlm_lock *dst, struct nlm4_lock *src)
{

	dst->caller_name = src->caller_name;
	dst->fh = src->fh;
	dst->oh = src->oh;
	dst->svid = src->svid;
	dst->l_offset = src->l_offset;
	dst->l_len = src->l_len;
}

static void
nlm_convert_to_nlm4_holder(struct nlm4_holder *dst, struct nlm_holder *src)
{

	dst->exclusive = src->exclusive;
	dst->svid = src->svid;
	dst->oh = src->oh;
	dst->l_offset = src->l_offset;
	dst->l_len = src->l_len;
}

static void
nlm_convert_to_nlm4_res(struct nlm4_res *dst, struct nlm_res *src)
{
	dst->cookie = src->cookie;
	dst->stat.stat = (enum nlm4_stats) src->stat.stat;
}

static enum clnt_stat
nlm_test_rpc(rpcvers_t vers, nlm4_testargs *args, nlm4_testres *res, CLIENT *client,
    struct rpc_callextra *ext, struct timeval timo)
{
	if (vers == NLM_VERS4) {
		return nlm4_test_4(args, res, client, ext, timo);
	} else {
		nlm_testargs args1;
		nlm_testres res1;
		enum clnt_stat stat;

		args1.cookie = args->cookie;
		args1.exclusive = args->exclusive;
		nlm_convert_to_nlm_lock(&args1.alock, &args->alock);
		memset(&res1, 0, sizeof(res1));

		stat = nlm_test_1(&args1, &res1, client, ext, timo);

		if (stat == RPC_SUCCESS) {
			res->cookie = res1.cookie;
			res->stat.stat = (enum nlm4_stats) res1.stat.stat;
			if (res1.stat.stat == nlm_denied)
				nlm_convert_to_nlm4_holder(
					&res->stat.nlm4_testrply_u.holder,
					&res1.stat.nlm_testrply_u.holder);
		}

		return (stat);
	}
}

static enum clnt_stat
nlm_lock_rpc(rpcvers_t vers, nlm4_lockargs *args, nlm4_res *res, CLIENT *client,
    struct rpc_callextra *ext, struct timeval timo)
{
	if (vers == NLM_VERS4) {
		return nlm4_lock_4(args, res, client, ext, timo);
	} else {
		nlm_lockargs args1;
		nlm_res res1;
		enum clnt_stat stat;

		args1.cookie = args->cookie;
		args1.block = args->block;
		args1.exclusive = args->exclusive;
		nlm_convert_to_nlm_lock(&args1.alock, &args->alock);
		args1.reclaim = args->reclaim;
		args1.state = args->state;
		memset(&res1, 0, sizeof(res1));

		stat = nlm_lock_1(&args1, &res1, client, ext, timo);

		if (stat == RPC_SUCCESS) {
			nlm_convert_to_nlm4_res(res, &res1);
		}

		return (stat);
	}
}

static enum clnt_stat
nlm_cancel_rpc(rpcvers_t vers, nlm4_cancargs *args, nlm4_res *res, CLIENT *client,
    struct rpc_callextra *ext, struct timeval timo)
{
	if (vers == NLM_VERS4) {
		return nlm4_cancel_4(args, res, client, ext, timo);
	} else {
		nlm_cancargs args1;
		nlm_res res1;
		enum clnt_stat stat;

		args1.cookie = args->cookie;
		args1.block = args->block;
		args1.exclusive = args->exclusive;
		nlm_convert_to_nlm_lock(&args1.alock, &args->alock);
		memset(&res1, 0, sizeof(res1));

		stat = nlm_cancel_1(&args1, &res1, client, ext, timo);

		if (stat == RPC_SUCCESS) {
			nlm_convert_to_nlm4_res(res, &res1);
		}

		return (stat);
	}
}

static enum clnt_stat
nlm_unlock_rpc(rpcvers_t vers, nlm4_unlockargs *args, nlm4_res *res, CLIENT *client,
    struct rpc_callextra *ext, struct timeval timo)
{
	if (vers == NLM_VERS4) {
		return nlm4_unlock_4(args, res, client, ext, timo);
	} else {
		nlm_unlockargs args1;
		nlm_res res1;
		enum clnt_stat stat;

		args1.cookie = args->cookie;
		nlm_convert_to_nlm_lock(&args1.alock, &args->alock);
		memset(&res1, 0, sizeof(res1));

		stat = nlm_unlock_1(&args1, &res1, client, ext, timo);

		if (stat == RPC_SUCCESS) {
			nlm_convert_to_nlm4_res(res, &res1);
		}

		return (stat);
	}
}

/*
 * Called after a lock request (set or clear) succeeded. We record the
 * details in the local lock manager. Note that since the remote
 * server has granted the lock, we can be sure that it doesn't
 * conflict with any other locks we have in the local lock manager.
 *
 * Since it is possible that host may also make NLM client requests to
 * our NLM server, we use a different sysid value to record our own
 * client locks.
 *
 * Note that since it is possible for us to receive replies from the
 * server in a different order than the locks were granted (e.g. if
 * many local threads are contending for the same lock), we must use a
 * blocking operation when registering with the local lock manager.
 * We expect that any actual wait will be rare and short hence we
 * ignore signals for this.
 */
static void
nlm_record_lock(struct vnode *vp, int op, struct flock *fl,
    int svid, int sysid, off_t size)
{
	struct vop_advlockasync_args a;
	struct flock newfl;
	struct proc *p;
	int error, stops_deferred;

	a.a_vp = vp;
	a.a_id = NULL;
	a.a_op = op;
	a.a_fl = &newfl;
	a.a_flags = F_REMOTE|F_WAIT|F_NOINTR;
	a.a_task = NULL;
	a.a_cookiep = NULL;
	newfl.l_start = fl->l_start;
	newfl.l_len = fl->l_len;
	newfl.l_type = fl->l_type;
	newfl.l_whence = fl->l_whence;
	newfl.l_pid = svid;
	newfl.l_sysid = NLM_SYSID_CLIENT | sysid;

	for (;;) {
		error = lf_advlockasync(&a, &vp->v_lockf, size);
		if (error == EDEADLK) {
			/*
			 * Locks are associated with the processes and
			 * not with threads.  Suppose we have two
			 * threads A1 A2 in one process, A1 locked
			 * file f1, A2 is locking file f2, and A1 is
			 * unlocking f1. Then remote server may
			 * already unlocked f1, while local still not
			 * yet scheduled A1 to make the call to local
			 * advlock manager. The process B owns lock on
			 * f2 and issued the lock on f1.  Remote would
			 * grant B the request on f1, but local would
			 * return EDEADLK.
			*/
			pause("nlmdlk", 1);
			p = curproc;
			stops_deferred = sigdeferstop(SIGDEFERSTOP_OFF);
			PROC_LOCK(p);
			thread_suspend_check(0);
			PROC_UNLOCK(p);
			sigallowstop(stops_deferred);
		} else if (error == EINTR) {
			/*
			 * lf_purgelocks() might wake up the lock
			 * waiter and removed our lock graph edges.
			 * There is no sense in re-trying recording
			 * the lock to the local manager after
			 * reclaim.
			 */
			error = 0;
			break;
		} else
			break;
	}
	KASSERT(error == 0 || error == ENOENT,
	    ("Failed to register NFS lock locally - error=%d", error));
}

static int
nlm_setlock(struct nlm_host *host, struct rpc_callextra *ext,
    rpcvers_t vers, struct timeval *timo, int retries,
    struct vnode *vp, int op, struct flock *fl, int flags,
    int svid, size_t fhlen, void *fh, off_t size, bool_t reclaim)
{
	struct nlm4_lockargs args;
	char oh_space[32];
	struct nlm4_res res;
	u_int xid;
	CLIENT *client;
	enum clnt_stat stat;
	int retry, block, exclusive;
	void *wait_handle = NULL;
	int error;

	memset(&args, 0, sizeof(args));
	memset(&res, 0, sizeof(res));

	block = (flags & F_WAIT) ? TRUE : FALSE;
	exclusive = (fl->l_type == F_WRLCK);

	error = nlm_init_lock(fl, flags, svid, vers, fhlen, fh, size,
	    &args.alock, oh_space);
	if (error)
		return (error);
	args.block = block;
	args.exclusive = exclusive;
	args.reclaim = reclaim;
	args.state = nlm_nsm_state;

	retry = 5*hz;
	for (;;) {
		client = nlm_host_get_rpc(host, FALSE);
		if (!client)
			return (ENOLCK); /* XXX retry? */

		if (block)
			wait_handle = nlm_register_wait_lock(&args.alock, vp);

		xid = atomic_fetchadd_int(&nlm_xid, 1);
		args.cookie.n_len = sizeof(xid);
		args.cookie.n_bytes = (char*) &xid;

		stat = nlm_lock_rpc(vers, &args, &res, client, ext, *timo);

		CLNT_RELEASE(client);

		if (stat != RPC_SUCCESS) {
			if (block)
				nlm_deregister_wait_lock(wait_handle);
			if (retries) {
				retries--;
				continue;
			}
			return (EINVAL);
		}

		/*
		 * Free res.cookie.
		 */
		xdr_free((xdrproc_t) xdr_nlm4_res, &res);

		if (block && res.stat.stat != nlm4_blocked)
			nlm_deregister_wait_lock(wait_handle);

		if (res.stat.stat == nlm4_denied_grace_period) {
			/*
			 * The server has recently rebooted and is
			 * giving old clients a change to reclaim
			 * their locks. Wait for a few seconds and try
			 * again.
			 */
			error = tsleep(&args, PCATCH, "nlmgrace", retry);
			if (error && error != EWOULDBLOCK)
				return (error);
			retry = 2*retry;
			if (retry > 30*hz)
				retry = 30*hz;
			continue;
		}

		if (block && res.stat.stat == nlm4_blocked) {
			/*
			 * The server should call us back with a
			 * granted message when the lock succeeds. In
			 * order to deal with broken servers, lost
			 * granted messages and server reboots, we
			 * will also re-try every few seconds.
			 */
			error = nlm_wait_lock(wait_handle, retry);
			if (error == EWOULDBLOCK) {
				retry = 2*retry;
				if (retry > 30*hz)
					retry = 30*hz;
				continue;
			}
			if (error) {
				/*
				 * We need to call the server to
				 * cancel our lock request.
				 */
				nlm4_cancargs cancel;

				memset(&cancel, 0, sizeof(cancel));

				xid = atomic_fetchadd_int(&nlm_xid, 1);
				cancel.cookie.n_len = sizeof(xid);
				cancel.cookie.n_bytes = (char*) &xid;
				cancel.block = block;
				cancel.exclusive = exclusive;
				cancel.alock = args.alock;

				do {
					client = nlm_host_get_rpc(host, FALSE);
					if (!client)
						/* XXX retry? */
						return (ENOLCK);

					stat = nlm_cancel_rpc(vers, &cancel,
					    &res, client, ext, *timo);

					CLNT_RELEASE(client);

					if (stat != RPC_SUCCESS) {
						/*
						 * We need to cope
						 * with temporary
						 * network partitions
						 * as well as server
						 * reboots. This means
						 * we have to keep
						 * trying to cancel
						 * until the server
						 * wakes up again.
						 */
						pause("nlmcancel", 10*hz);
					}
				} while (stat != RPC_SUCCESS);

				/*
				 * Free res.cookie.
				 */
				xdr_free((xdrproc_t) xdr_nlm4_res, &res);

				switch (res.stat.stat) {
				case nlm_denied:
					/*
					 * There was nothing
					 * to cancel. We are
					 * going to go ahead
					 * and assume we got
					 * the lock.
					 */
					error = 0;
					break;

				case nlm4_denied_grace_period:
					/*
					 * The server has
					 * recently rebooted -
					 * treat this as a
					 * successful
					 * cancellation.
					 */
					break;

				case nlm4_granted:
					/*
					 * We managed to
					 * cancel.
					 */
					break;

				default:
					/*
					 * Broken server
					 * implementation -
					 * can't really do
					 * anything here.
					 */
					break;
				}

			}
		} else {
			error = nlm_map_status(res.stat.stat);
		}

		if (!error && !reclaim) {
			nlm_record_lock(vp, op, fl, args.alock.svid,
			    nlm_host_get_sysid(host), size);
			nlm_host_monitor(host, 0);
		}

		return (error);
	}
}

static int
nlm_clearlock(struct nlm_host *host, struct rpc_callextra *ext,
    rpcvers_t vers, struct timeval *timo, int retries,
    struct vnode *vp, int op, struct flock *fl, int flags,
    int svid, size_t fhlen, void *fh, off_t size)
{
	struct nlm4_unlockargs args;
	char oh_space[32];
	struct nlm4_res res;
	u_int xid;
	CLIENT *client;
	enum clnt_stat stat;
	int error;

	memset(&args, 0, sizeof(args));
	memset(&res, 0, sizeof(res));

	error = nlm_init_lock(fl, flags, svid, vers, fhlen, fh, size,
	    &args.alock, oh_space);
	if (error)
		return (error);

	for (;;) {
		client = nlm_host_get_rpc(host, FALSE);
		if (!client)
			return (ENOLCK); /* XXX retry? */

		xid = atomic_fetchadd_int(&nlm_xid, 1);
		args.cookie.n_len = sizeof(xid);
		args.cookie.n_bytes = (char*) &xid;

		stat = nlm_unlock_rpc(vers, &args, &res, client, ext, *timo);

		CLNT_RELEASE(client);

		if (stat != RPC_SUCCESS) {
			if (retries) {
				retries--;
				continue;
			}
			return (EINVAL);
		}

		/*
		 * Free res.cookie.
		 */
		xdr_free((xdrproc_t) xdr_nlm4_res, &res);

		if (res.stat.stat == nlm4_denied_grace_period) {
			/*
			 * The server has recently rebooted and is
			 * giving old clients a change to reclaim
			 * their locks. Wait for a few seconds and try
			 * again.
			 */
			error = tsleep(&args, PCATCH, "nlmgrace", 5*hz);
			if (error && error != EWOULDBLOCK)
				return (error);
			continue;
		}

		/*
		 * If we are being called via nlm_reclaim (which will
		 * use the F_REMOTE flag), don't record the lock
		 * operation in the local lock manager since the vnode
		 * is going away.
		 */
		if (!(flags & F_REMOTE))
			nlm_record_lock(vp, op, fl, args.alock.svid,
			    nlm_host_get_sysid(host), size);

		return (0);
	}
}

static int
nlm_getlock(struct nlm_host *host, struct rpc_callextra *ext,
    rpcvers_t vers, struct timeval *timo, int retries,
    struct vnode *vp, int op, struct flock *fl, int flags,
    int svid, size_t fhlen, void *fh, off_t size)
{
	struct nlm4_testargs args;
	char oh_space[32];
	struct nlm4_testres res;
	u_int xid;
	CLIENT *client;
	enum clnt_stat stat;
	int exclusive;
	int error;

	KASSERT(!(flags & F_FLOCK), ("unexpected F_FLOCK for F_GETLK"));

	memset(&args, 0, sizeof(args));
	memset(&res, 0, sizeof(res));

	exclusive = (fl->l_type == F_WRLCK);

	error = nlm_init_lock(fl, flags, svid, vers, fhlen, fh, size,
	    &args.alock, oh_space);
	if (error)
		return (error);
	args.exclusive = exclusive;

	for (;;) {
		client = nlm_host_get_rpc(host, FALSE);
		if (!client)
			return (ENOLCK); /* XXX retry? */

		xid = atomic_fetchadd_int(&nlm_xid, 1);
		args.cookie.n_len = sizeof(xid);
		args.cookie.n_bytes = (char*) &xid;

		stat = nlm_test_rpc(vers, &args, &res, client, ext, *timo);

		CLNT_RELEASE(client);

		if (stat != RPC_SUCCESS) {
			if (retries) {
				retries--;
				continue;
			}
			return (EINVAL);
		}

		if (res.stat.stat == nlm4_denied_grace_period) {
			/*
			 * The server has recently rebooted and is
			 * giving old clients a change to reclaim
			 * their locks. Wait for a few seconds and try
			 * again.
			 */
			xdr_free((xdrproc_t) xdr_nlm4_testres, &res);
			error = tsleep(&args, PCATCH, "nlmgrace", 5*hz);
			if (error && error != EWOULDBLOCK)
				return (error);
			continue;
		}

		if (res.stat.stat == nlm4_denied) {
			struct nlm4_holder *h =
				&res.stat.nlm4_testrply_u.holder;
			fl->l_start = h->l_offset;
			fl->l_len = h->l_len;
			fl->l_pid = h->svid;
			if (h->exclusive)
				fl->l_type = F_WRLCK;
			else
				fl->l_type = F_RDLCK;
			fl->l_whence = SEEK_SET;
			fl->l_sysid = 0;
		} else {
			fl->l_type = F_UNLCK;
		}

		xdr_free((xdrproc_t) xdr_nlm4_testres, &res);

		return (0);
	}
}

static int
nlm_map_status(nlm4_stats stat)
{
	switch (stat) {
	case nlm4_granted:
		return (0);

	case nlm4_denied:
		return (EAGAIN);

	case nlm4_denied_nolocks:
		return (ENOLCK);

	case nlm4_deadlck:
		return (EDEADLK);

	case nlm4_rofs:
		return (EROFS);

	case nlm4_stale_fh:
		return (ESTALE);

	case nlm4_fbig:
		return (EFBIG);

	case nlm4_failed:
		return (EACCES);

	default:
		return (EINVAL);
	}
}

static struct nlm_file_svid *
nlm_find_svid(void *id)
{
	struct nlm_file_svid *ns, *newns;
	int h;

	h = (((uintptr_t) id) >> 7) % NLM_SVID_HASH_SIZE;

	mtx_lock(&nlm_svid_lock);
	LIST_FOREACH(ns, &nlm_file_svids[h], ns_link) {
		if (ns->ns_id == id) {
			ns->ns_refs++;
			break;
		}
	}
	mtx_unlock(&nlm_svid_lock);
	if (!ns) {
		int svid = alloc_unr(nlm_svid_allocator);
		newns = malloc(sizeof(struct nlm_file_svid), M_NLM,
		    M_WAITOK);
		newns->ns_refs = 1;
		newns->ns_id = id;
		newns->ns_svid = svid;
		newns->ns_ucred = NULL;
		newns->ns_active = FALSE;

		/*
		 * We need to check for a race with some other
		 * thread allocating a svid for this file.
		 */
		mtx_lock(&nlm_svid_lock);
		LIST_FOREACH(ns, &nlm_file_svids[h], ns_link) {
			if (ns->ns_id == id) {
				ns->ns_refs++;
				break;
			}
		}
		if (ns) {
			mtx_unlock(&nlm_svid_lock);
			free_unr(nlm_svid_allocator, newns->ns_svid);
			free(newns, M_NLM);
		} else {
			LIST_INSERT_HEAD(&nlm_file_svids[h], newns,
			    ns_link);
			ns = newns;
			mtx_unlock(&nlm_svid_lock);
		}
	}

	return (ns);
}

static void
nlm_free_svid(struct nlm_file_svid *ns)
{

	mtx_lock(&nlm_svid_lock);
	ns->ns_refs--;
	if (!ns->ns_refs) {
		KASSERT(!ns->ns_active, ("Freeing active SVID"));
		LIST_REMOVE(ns, ns_link);
		mtx_unlock(&nlm_svid_lock);
		free_unr(nlm_svid_allocator, ns->ns_svid);
		if (ns->ns_ucred)
			crfree(ns->ns_ucred);
		free(ns, M_NLM);
	} else {
		mtx_unlock(&nlm_svid_lock);
	}
}

static int
nlm_init_lock(struct flock *fl, int flags, int svid,
    rpcvers_t vers, size_t fhlen, void *fh, off_t size,
    struct nlm4_lock *lock, char oh_space[32])
{
	size_t oh_len;
	off_t start, len;

	if (fl->l_whence == SEEK_END) {
		if (size > OFF_MAX
		    || (fl->l_start > 0 && size > OFF_MAX - fl->l_start))
			return (EOVERFLOW);
		start = size + fl->l_start;
	} else if (fl->l_whence == SEEK_SET || fl->l_whence == SEEK_CUR) {
		start = fl->l_start;
	} else {
		return (EINVAL);
	}
	if (start < 0)
		return (EINVAL);
	if (fl->l_len < 0) {
		len = -fl->l_len;
		start -= len;
		if (start < 0)
			return (EINVAL);
	} else {
		len = fl->l_len;
	}

	if (vers == NLM_VERS) {
		/*
		 * Enforce range limits on V1 locks
		 */
		if (start > 0xffffffffLL || len > 0xffffffffLL)
			return (EOVERFLOW);
	}

	snprintf(oh_space, 32, "%d@", svid);
	oh_len = strlen(oh_space);
	getcredhostname(NULL, oh_space + oh_len, 32 - oh_len);
	oh_len = strlen(oh_space);

	memset(lock, 0, sizeof(*lock));
	lock->caller_name = prison0.pr_hostname;
	lock->fh.n_len = fhlen;
	lock->fh.n_bytes = fh;
	lock->oh.n_len = oh_len;
	lock->oh.n_bytes = oh_space;
	lock->svid = svid;
	lock->l_offset = start;
	lock->l_len = len;

	return (0);
}
