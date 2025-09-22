/*	$OpenBSD: lockd_lock.c,v 1.12 2023/03/08 04:43:15 guenther Exp $	*/

/*
 * Copyright (c) 2000 Manuel Bouyer.
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

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <rpc/rpc.h>
#include <rpcsvc/sm_inter.h>
#include <rpcsvc/nlm_prot.h>
#include "lockd_lock.h"
#include "lockd.h"

/* A set of utilities for managing file locking */
LIST_HEAD(lcklst_head, file_lock);
struct lcklst_head lcklst_head = LIST_HEAD_INITIALIZER(lcklst_head);

#define	FHANDLE_SIZE_MAX	1024	/* arbitrary big enough value */
typedef struct {
	size_t fhsize;
	char *fhdata;
} nfs_fhandle_t;

static int
fhcmp(const nfs_fhandle_t *fh1, const nfs_fhandle_t *fh2)
{
	return memcmp(fh1->fhdata, fh2->fhdata, sizeof(fhandle_t));
}

static int
fhconv(nfs_fhandle_t *fh, const netobj *rfh)
{
	size_t sz;

	sz = rfh->n_len;
	if (sz > FHANDLE_SIZE_MAX) {
		syslog(LOG_DEBUG,
		    "received fhandle size %zd, max supported size %d",
		    sz, FHANDLE_SIZE_MAX);
		errno = EINVAL;
		return -1;
	}
	fh->fhdata = malloc(sz);
	if (fh->fhdata == NULL) {
		return -1;
	}
	fh->fhsize = sz;
	memcpy(fh->fhdata, rfh->n_bytes, sz);
	return 0;
}

static void
fhfree(nfs_fhandle_t *fh)
{

	free(fh->fhdata);
}

/* struct describing a lock */
struct file_lock {
	LIST_ENTRY(file_lock) lcklst;
	nfs_fhandle_t filehandle; /* NFS filehandle */
	struct sockaddr_in *addr;
	struct nlm4_holder client; /* lock holder */
	netobj client_cookie; /* cookie sent by the client */
	char client_name[128];
	int nsm_status; /* status from the remote lock manager */
	int status; /* lock status, see below */
	int flags; /* lock flags, see lockd_lock.h */
	pid_t locker; /* pid of the child process trying to get the lock */
	int fd;	/* file descriptor for this lock */
};

/* lock status */
#define LKST_LOCKED	1 /* lock is locked */
#define LKST_WAITING	2 /* file is already locked by another host */
#define LKST_PROCESSING	3 /* child is trying to acquire the lock */
#define LKST_DYING	4 /* must die when we get news from the child */

static struct file_lock *lalloc(void);
void lfree(struct file_lock *);
enum nlm_stats do_lock(struct file_lock *, int);
enum nlm_stats do_unlock(struct file_lock *);
void send_granted(struct file_lock *, int);
void siglock(void);
void sigunlock(void);

/* list of hosts we monitor */
LIST_HEAD(hostlst_head, host);
struct hostlst_head hostlst_head = LIST_HEAD_INITIALIZER(hostlst_head);

/* struct describing a lock */
struct host {
	LIST_ENTRY(host) hostlst;
	char name[SM_MAXSTRLEN+1];
	int refcnt;
};

void do_mon(const char *);

#define	LL_FH	0x01
#define	LL_NAME	0x02
#define	LL_SVID	0x04

static struct file_lock *lock_lookup(struct file_lock *, int);

/*
 * lock_lookup: lookup a matching lock.
 * called with siglock held.
 */
static struct file_lock *
lock_lookup(struct file_lock *newfl, int flags)
{
	struct file_lock *fl;

	LIST_FOREACH(fl, &lcklst_head, lcklst) {
		if ((flags & LL_SVID) != 0 &&
		    newfl->client.svid != fl->client.svid)
			continue;
		if ((flags & LL_NAME) != 0 &&
		    strcmp(newfl->client_name, fl->client_name) != 0)
			continue;
		if ((flags & LL_FH) != 0 &&
		    fhcmp(&newfl->filehandle, &fl->filehandle) != 0)
			continue;
		/* found */
		break;
	}

	return fl;
}

/*
 * testlock(): inform the caller if the requested lock would be granted or not
 * returns NULL if lock would granted, or pointer to the current nlm4_holder
 * otherwise.
 */

struct nlm4_holder *
testlock(struct nlm4_lock *lock, int flags)
{
	struct file_lock *fl;
	nfs_fhandle_t filehandle;

	/* convert lock to a local filehandle */
	if (fhconv(&filehandle, &lock->fh)) {
		syslog(LOG_NOTICE, "fhconv failed (%m)");
		return NULL; /* XXX */
	}

	siglock();
	/* search through the list for lock holder */
	LIST_FOREACH(fl, &lcklst_head, lcklst) {
		if (fl->status != LKST_LOCKED)
			continue;
		if (fhcmp(&fl->filehandle, &filehandle) != 0)
			continue;
		/* got it ! */
		syslog(LOG_DEBUG, "test for %s: found lock held by %s",
		    lock->caller_name, fl->client_name);
		sigunlock();
		fhfree(&filehandle);
		return (&fl->client);
	}
	/* not found */
	sigunlock();
	fhfree(&filehandle);
	syslog(LOG_DEBUG, "test for %s: no lock found", lock->caller_name);
	return NULL;
}

/*
 * getlock: try to acquire the lock. 
 * If file is already locked and we can sleep, put the lock in the list with
 * status LKST_WAITING; it'll be processed later.
 * Otherwise try to lock. If we're allowed to block, fork a child which
 * will do the blocking lock.
 */
enum nlm_stats
getlock(nlm4_lockargs * lckarg, struct svc_req *rqstp, int flags)
{
	struct file_lock *fl, *newfl;
	enum nlm_stats retval;
	struct sockaddr_in *addr;

	if (grace_expired == 0 && lckarg->reclaim == 0)
		return (flags & LOCK_V4) ?
		    nlm4_denied_grace_period : nlm_denied_grace_period;
			
	/* allocate new file_lock for this request */
	newfl = lalloc();
	if (newfl == NULL) {
		syslog(LOG_NOTICE, "malloc failed (%m)");
		/* failed */
		return (flags & LOCK_V4) ?
		    nlm4_denied_nolock : nlm_denied_nolocks;
	}
	if (fhconv(&newfl->filehandle, &lckarg->alock.fh)) {
		syslog(LOG_NOTICE, "fhconv failed (%m)");
		lfree(newfl);
		/* failed */
		return (flags & LOCK_V4) ?
		    nlm4_denied_nolock : nlm_denied_nolocks;
	}
	addr = svc_getcaller(rqstp->rq_xprt);
	newfl->addr = malloc(addr->sin_len);
	if (newfl->addr == NULL) {
		syslog(LOG_NOTICE, "malloc failed (%m)");
		lfree(newfl);
		/* failed */
		return (flags & LOCK_V4) ?
		    nlm4_denied_nolock : nlm_denied_nolocks;
	}
	memcpy(newfl->addr, addr, addr->sin_len);
	newfl->client.exclusive = lckarg->exclusive;
	newfl->client.svid = lckarg->alock.svid;
	newfl->client.oh.n_bytes = malloc(lckarg->alock.oh.n_len);
	if (newfl->client.oh.n_bytes == NULL) {
		syslog(LOG_NOTICE, "malloc failed (%m)");
		lfree(newfl);
		return (flags & LOCK_V4) ?
		    nlm4_denied_nolock : nlm_denied_nolocks;
	}
	newfl->client.oh.n_len = lckarg->alock.oh.n_len;
	memcpy(newfl->client.oh.n_bytes, lckarg->alock.oh.n_bytes,
	    lckarg->alock.oh.n_len);
	newfl->client.l_offset = lckarg->alock.l_offset;
	newfl->client.l_len = lckarg->alock.l_len;
	newfl->client_cookie.n_len = lckarg->cookie.n_len;
	newfl->client_cookie.n_bytes = malloc(lckarg->cookie.n_len);
	if (newfl->client_cookie.n_bytes == NULL) {
		syslog(LOG_NOTICE, "malloc failed (%m)");
		lfree(newfl);
		return (flags & LOCK_V4) ? 
		    nlm4_denied_nolock : nlm_denied_nolocks;
	}
	memcpy(newfl->client_cookie.n_bytes, lckarg->cookie.n_bytes,
	    lckarg->cookie.n_len);
	strlcpy(newfl->client_name, lckarg->alock.caller_name,
	    sizeof(newfl->client_name));
	newfl->nsm_status = lckarg->state;
	newfl->status = 0;
	newfl->flags = flags;
	siglock();
	/* look for a lock rq from this host for this fh */
	fl = lock_lookup(newfl, LL_FH|LL_NAME|LL_SVID);
	if (fl) {
		/* already locked by this host ??? */
		sigunlock();
		syslog(LOG_NOTICE, "duplicate lock from %s.%"
		    PRIu32,
		    newfl->client_name, newfl->client.svid);
		lfree(newfl);
		switch(fl->status) {
		case LKST_LOCKED:
			return (flags & LOCK_V4) ?
			    nlm4_granted : nlm_granted;
		case LKST_WAITING:
		case LKST_PROCESSING:
			return (flags & LOCK_V4) ?
			    nlm4_blocked : nlm_blocked;
		case LKST_DYING:
			return (flags & LOCK_V4) ?
			    nlm4_denied : nlm_denied;
		default:
			syslog(LOG_NOTICE, "bad status %d",
			    fl->status);
			return (flags & LOCK_V4) ?
			    nlm4_failed : nlm_denied;
		}
		/* NOTREACHED */
	}
	fl = lock_lookup(newfl, LL_FH);
	if (fl) {
		/*
		 * We already have a lock for this file.
		 * Put this one in waiting state if allowed to block
		 */
		if (lckarg->block) {
			syslog(LOG_DEBUG, "lock from %s.%" PRIu32 ": "
			    "already locked, waiting",
			    lckarg->alock.caller_name,
			    lckarg->alock.svid);
			newfl->status = LKST_WAITING;
			LIST_INSERT_HEAD(&lcklst_head, newfl, lcklst);
			do_mon(lckarg->alock.caller_name);
			sigunlock();
			return (flags & LOCK_V4) ?
			    nlm4_blocked : nlm_blocked;
		} else {
			sigunlock();
			syslog(LOG_DEBUG, "lock from %s.%" PRIu32 ": "
			    "already locked, failed",
			    lckarg->alock.caller_name,
			    lckarg->alock.svid);
			lfree(newfl);
			return (flags & LOCK_V4) ?
			    nlm4_denied : nlm_denied;
		}
		/* NOTREACHED */
	}

	/* no entry for this file yet; add to list */
	LIST_INSERT_HEAD(&lcklst_head, newfl, lcklst);
	/* do the lock */
	retval = do_lock(newfl, lckarg->block);
	switch (retval) {
	case nlm4_granted:
	/* case nlm_granted: is the same as nlm4_granted */
	case nlm4_blocked:
	/* case nlm_blocked: is the same as nlm4_blocked */
		do_mon(lckarg->alock.caller_name);
		break;
	default:
		lfree(newfl);
		break;
	}
	sigunlock();
	return retval;
}

/* unlock a filehandle */
enum nlm_stats
unlock(nlm4_lock *lck, int flags)
{
	struct file_lock *fl;
	nfs_fhandle_t filehandle;
	int err = (flags & LOCK_V4) ? nlm4_granted : nlm_granted;

	if (fhconv(&filehandle, &lck->fh)) {
		syslog(LOG_NOTICE, "fhconv failed (%m)");
		return (flags & LOCK_V4) ? nlm4_denied : nlm_denied;
	}
	siglock();
	LIST_FOREACH(fl, &lcklst_head, lcklst) {
		if (strcmp(fl->client_name, lck->caller_name) ||
		    fhcmp(&filehandle, &fl->filehandle) != 0 ||
		    fl->client.oh.n_len != lck->oh.n_len ||
		    memcmp(fl->client.oh.n_bytes, lck->oh.n_bytes,
			fl->client.oh.n_len) != 0 ||
		    fl->client.svid != lck->svid)
			continue;
		/* Got it, unlock and remove from the queue */
		syslog(LOG_DEBUG, "unlock from %s.%" PRIu32 ": found struct, "
		    "status %d", lck->caller_name, lck->svid, fl->status);
		switch (fl->status) {
		case LKST_LOCKED:
			err = do_unlock(fl);
			break;
		case LKST_WAITING:
			/* remove from the list */
			LIST_REMOVE(fl, lcklst);
			lfree(fl);
			break;
		case LKST_PROCESSING:
			/*
			 * being handled by a child; will clean up
			 * when the child exits
			 */
			fl->status = LKST_DYING;
			break;
		case LKST_DYING:
			/* nothing to do */
			break;
		default:
			syslog(LOG_NOTICE, "unknown status %d for %s",
			    fl->status, fl->client_name);
		}
		sigunlock();
		fhfree(&filehandle);
		return err;
	}
	sigunlock();
	/* didn't find a matching entry; log anyway */
	syslog(LOG_NOTICE, "no matching entry for %s",
	    lck->caller_name);
	fhfree(&filehandle);
	return (flags & LOCK_V4) ? nlm4_granted : nlm_granted;
}

static struct file_lock *
lalloc(void)
{
	return calloc(1, sizeof(struct file_lock));
}

void
lfree(struct file_lock *fl)
{
	free(fl->addr);
	free(fl->client.oh.n_bytes);
	free(fl->client_cookie.n_bytes);
	fhfree(&fl->filehandle);
	free(fl);
}

void
sigchild_handler(int sig)
{
	int sstatus;
	pid_t pid;
	struct file_lock *fl;

	for (;;) {
		pid = wait4(-1, &sstatus, WNOHANG, NULL);
		if (pid == -1) {
			if (errno != ECHILD)
				syslog(LOG_NOTICE, "wait failed (%m)");
			else
				syslog(LOG_DEBUG, "wait failed (%m)");
			return;
		}
		if (pid == 0) {
			/* no more child to handle yet */
			return;
		}
		/*
		 * if we're here we have a child that exited
		 * Find the associated file_lock.
		 */
		LIST_FOREACH(fl, &lcklst_head, lcklst) {
			if (pid == fl->locker)
				break;
		}
		if (fl == NULL) {
			syslog(LOG_NOTICE, "unknown child %d", pid);
		} else {
			/* protect from pid reusing. */
			fl->locker = 0;
			if (!WIFEXITED(sstatus) || WEXITSTATUS(sstatus) != 0) {
				syslog(LOG_NOTICE, "child %d failed", pid);
				/*
				 * can't do much here; we can't reply
				 * anything but OK for blocked locks
				 * Eventually the client will time out
				 * and retry.
				 */
				do_unlock(fl);
				return;
			}
			    
			/* check lock status */
			syslog(LOG_DEBUG, "processing child %d, status %d",
			    pid, fl->status);
			switch(fl->status) {
			case LKST_PROCESSING:
				fl->status = LKST_LOCKED;
				send_granted(fl, (fl->flags & LOCK_V4) ?
				    nlm4_granted : nlm_granted);
				break;
			case LKST_DYING:
				do_unlock(fl);
				break;
			default:
				syslog(LOG_NOTICE, "bad lock status (%d) for"
				   " child %d", fl->status, pid);
			}
		}
	}
}

/*
 *
 * try to acquire the lock described by fl. Eventually fork a child to do a
 * blocking lock if allowed and required.
 */

enum nlm_stats
do_lock(struct file_lock *fl, int block)
{
	int lflags, error;
	struct stat st;

	fl->fd = fhopen((fhandle_t *)fl->filehandle.fhdata, O_RDWR);
	if (fl->fd == -1) {
		switch (errno) {
		case ESTALE:
			error = nlm4_stale_fh;
			break;
		case EROFS:
			error = nlm4_rofs;
			break;
		default:
			error = nlm4_failed;
		}
		if ((fl->flags & LOCK_V4) == 0)
			error = nlm_denied;
		syslog(LOG_NOTICE, "fhopen failed (from %s) (%m)",
		    fl->client_name);
		LIST_REMOVE(fl, lcklst);
		return error;
	}
	if (fstat(fl->fd, &st) == -1) {
		syslog(LOG_NOTICE, "fstat failed (from %s) (%m)",
		    fl->client_name);
	}
	syslog(LOG_DEBUG, "lock from %s.%" PRIu32 " for file%s%s: "
	    "dev %u ino %llu (uid %d), flags %d",
	    fl->client_name, fl->client.svid,
	    fl->client.exclusive ? " (exclusive)":"", block ? " (block)":"",
	    st.st_dev, (unsigned long long)st.st_ino, st.st_uid, fl->flags);
	lflags = LOCK_NB;
	if (fl->client.exclusive == 0)
		lflags |= LOCK_SH;
	else
		lflags |= LOCK_EX;
	error = flock(fl->fd, lflags);
	if (error != 0 && errno == EAGAIN && block) {
		switch (fl->locker = fork()) {
		case -1: /* fork failed */
			syslog(LOG_NOTICE, "fork failed (%m)");
			LIST_REMOVE(fl, lcklst);
			close(fl->fd);
			return (fl->flags & LOCK_V4) ?
			    nlm4_denied_nolock : nlm_denied_nolocks;
		case 0:
			/*
			 * Attempt a blocking lock. Will have to call
			 * NLM_GRANTED later.
			 */
			setproctitle("%s.%" PRIu32,
			    fl->client_name, fl->client.svid);
			lflags &= ~LOCK_NB;
			if(flock(fl->fd, lflags) != 0) {
				syslog(LOG_NOTICE, "flock failed (%m)");
				_exit(1);
			}
			/* lock granted */	
			_exit(0);
			/*NOTREACHED*/
		default:
			syslog(LOG_DEBUG, "lock request from %s.%" PRIu32 ": "
			    "forked %d",
			    fl->client_name, fl->client.svid, fl->locker);
			fl->status = LKST_PROCESSING;
			return (fl->flags & LOCK_V4) ?
			    nlm4_blocked : nlm_blocked;
		}
	}
	/* non block case */
	if (error != 0) {
		switch (errno) {
		case EAGAIN:
			error = nlm4_denied;
			break;
		case ESTALE:
			error = nlm4_stale_fh;
			break;
		case EROFS:
			error = nlm4_rofs;
			break;
		default:
			error = nlm4_failed;
		}
		if ((fl->flags & LOCK_V4) == 0)
			error = nlm_denied;
		if (errno != EAGAIN)
			syslog(LOG_NOTICE, "flock for %s failed (%m)",
			    fl->client_name);
		else syslog(LOG_DEBUG, "flock for %s failed (%m)",
			    fl->client_name);
		LIST_REMOVE(fl, lcklst);
		close(fl->fd);
		return error;
	}
	fl->status = LKST_LOCKED;
	return (fl->flags & LOCK_V4) ? nlm4_granted : nlm_granted;
}

void
send_granted(struct file_lock *fl, int opcode)
{
	CLIENT *cli;
	static char dummy;
	struct timeval timeo;
	int success;
	static struct nlm_res retval;
	static struct nlm4_res retval4;

	cli = get_client(fl->addr,
	    (fl->flags & LOCK_V4) ? NLM_VERS4 : NLM_VERS);
	if (cli == NULL) {
		syslog(LOG_NOTICE, "failed to get CLIENT for %s.%" PRIu32,
		    fl->client_name, fl->client.svid);
		/*
		 * We fail to notify remote that the lock has been granted.
		 * The client will timeout and retry, the lock will be
		 * granted at this time.
		 */
		return;
	}
	timeo.tv_sec = 0;
	timeo.tv_usec = (fl->flags & LOCK_ASYNC) ? 0 : 500000; /* 0.5s */

	if (fl->flags & LOCK_V4) {
		static nlm4_testargs result;
		result.cookie = fl->client_cookie;
		result.exclusive = fl->client.exclusive;
		result.alock.caller_name = fl->client_name;
		result.alock.fh.n_len = fl->filehandle.fhsize;
		result.alock.fh.n_bytes = fl->filehandle.fhdata;
		result.alock.oh = fl->client.oh;
		result.alock.svid = fl->client.svid;
		result.alock.l_offset = fl->client.l_offset;
		result.alock.l_len = fl->client.l_len;
		syslog(LOG_DEBUG, "sending v4 reply%s",
		    (fl->flags & LOCK_ASYNC) ? " (async)":"");
		if (fl->flags & LOCK_ASYNC) {
			success = clnt_call(cli, NLM4_GRANTED_MSG,
			    xdr_nlm4_testargs, &result, xdr_void, &dummy, timeo);
		} else {
			success = clnt_call(cli, NLM4_GRANTED,
			    xdr_nlm4_testargs, &result, xdr_nlm4_res,
			    &retval4, timeo);
		}
	} else {
		static nlm_testargs result;

		result.cookie = fl->client_cookie;
		result.exclusive = fl->client.exclusive;
		result.alock.caller_name = fl->client_name;
		result.alock.fh.n_len = fl->filehandle.fhsize;
		result.alock.fh.n_bytes = fl->filehandle.fhdata;
		result.alock.oh = fl->client.oh;
		result.alock.svid = fl->client.svid;
		result.alock.l_offset =
		    (unsigned int)fl->client.l_offset;
		result.alock.l_len =
		    (unsigned int)fl->client.l_len;
		syslog(LOG_DEBUG, "sending v1 reply%s",
		    (fl->flags & LOCK_ASYNC) ? " (async)":"");
		if (fl->flags & LOCK_ASYNC) {
			success = clnt_call(cli, NLM_GRANTED_MSG,
			    xdr_nlm_testargs, &result, xdr_void, &dummy, timeo);
		} else {
			success = clnt_call(cli, NLM_GRANTED,
			    xdr_nlm_testargs, &result, xdr_nlm_res,
			    &retval, timeo);
		}
	}
	if (debug_level > 2)
		syslog(LOG_DEBUG, "clnt_call returns %d(%s) for granted",
		    success, clnt_sperrno(success));

}

enum nlm_stats
do_unlock(struct file_lock *rfl)
{
	struct file_lock *fl;
	int error;
	int lockst;

	/* unlock the file: closing is enough ! */
	if (close(rfl->fd) == -1) {
		if (errno == ESTALE)
			error = nlm4_stale_fh;
		else
			error = nlm4_failed;
		if ((rfl->flags & LOCK_V4) == 0)
			error = nlm_denied;
		syslog(LOG_NOTICE, "close failed (from %s) (%m)",
		    rfl->client_name);
	} else {
		error = (rfl->flags & LOCK_V4) ?
		    nlm4_granted : nlm_granted;
	}
	LIST_REMOVE(rfl, lcklst);

	/* process the next LKST_WAITING lock request for this fh */
	LIST_FOREACH(fl, &lcklst_head, lcklst) {
		if (fl->status != LKST_WAITING ||
		    fhcmp(&rfl->filehandle, &fl->filehandle) != 0)
			continue;

		lockst = do_lock(fl, 1); /* If it's LKST_WAITING we can block */
		switch (lockst) {
		case nlm4_granted:
		/* case nlm_granted: same as nlm4_granted */
			send_granted(fl, (fl->flags & LOCK_V4) ?
			    nlm4_granted : nlm_granted);
			break;
		case nlm4_blocked:
		/* case nlm_blocked: same as nlm4_blocked */
			break;
		default:
			lfree(fl);
			break;
		}
		break;
	}
	lfree(rfl);
	return error;
}

void
siglock(void)
{
	sigset_t block;
	
	sigemptyset(&block);
	sigaddset(&block, SIGCHLD);

	if (sigprocmask(SIG_BLOCK, &block, NULL) == -1) {
		syslog(LOG_WARNING, "siglock failed (%m)");
	}
}

void
sigunlock(void)
{
	sigset_t block;
	
	sigemptyset(&block);
	sigaddset(&block, SIGCHLD);

	if (sigprocmask(SIG_UNBLOCK, &block, NULL) == -1) {
		syslog(LOG_WARNING, "sigunlock failed (%m)");
	}
}

/* monitor a host through rpc.statd, and keep a ref count */
void
do_mon(const char *hostname)
{
	static char localhost[] = "localhost";
	struct host *hp;
	struct mon my_mon;
	struct sm_stat_res result;
	int retval;

	LIST_FOREACH(hp, &hostlst_head, hostlst) {
		if (strcmp(hostname, hp->name) == 0) {
			/* already monitored, just bump refcnt */
			hp->refcnt++;
			return;
		}
	}
	/* not found, have to create an entry for it */
	hp = malloc(sizeof(struct host));
	if (hp == NULL) {
		syslog(LOG_WARNING, "can't monitor host %s (%m)", hostname);
		return;
	}
	strlcpy(hp->name, hostname, sizeof(hp->name));
	hp->refcnt = 1;
	syslog(LOG_DEBUG, "monitoring host %s", hostname);
	memset(&my_mon, 0, sizeof(my_mon));
	my_mon.mon_id.mon_name = hp->name;
	my_mon.mon_id.my_id.my_name = localhost;
	my_mon.mon_id.my_id.my_prog = NLM_PROG;
	my_mon.mon_id.my_id.my_vers = NLM_SM;
	my_mon.mon_id.my_id.my_proc = NLM_SM_NOTIFY;
	if ((retval = callrpc(localhost, SM_PROG, SM_VERS, SM_MON, xdr_mon,
	    (void *)&my_mon, xdr_sm_stat_res, (void *)&result)) != 0) {
		syslog(LOG_WARNING, "rpc to statd failed (%s)",
		    clnt_sperrno((enum clnt_stat)retval));
		free(hp);
		return;
	}
	if (result.res_stat == stat_fail) {
		syslog(LOG_WARNING, "statd failed");
		free(hp);
		return;
	}
	LIST_INSERT_HEAD(&hostlst_head, hp, hostlst);
}

void
notify(const char *hostname, int state)
{
	struct file_lock *fl, *next_fl;
	int err;
	syslog(LOG_DEBUG, "notify from %s, new state %d", hostname, state);
	/* search all lock for this host; if status changed, release the lock */
	siglock();
	for (fl = LIST_FIRST(&lcklst_head); fl != NULL; fl = next_fl) {
		next_fl = LIST_NEXT(fl, lcklst);
		if (strcmp(hostname, fl->client_name) == 0 &&
		    fl->nsm_status != state) {
			syslog(LOG_DEBUG, "state %d, nsm_state %d, unlocking",
			    fl->status, fl->nsm_status);
			switch(fl->status) {
			case LKST_LOCKED:
				err = do_unlock(fl);
				if (err != nlm_granted)
					syslog(LOG_DEBUG,
					    "notify: unlock failed for %s (%d)",
			    		    hostname, err);
				break;
			case LKST_WAITING:
				LIST_REMOVE(fl, lcklst);
				lfree(fl);
				break;
			case LKST_PROCESSING:
				fl->status = LKST_DYING;
				break;
			case LKST_DYING:
				break;
			default:
				syslog(LOG_NOTICE, "unknown status %d for %s",
				    fl->status, fl->client_name);
			}
		}
	}
	sigunlock();
}
