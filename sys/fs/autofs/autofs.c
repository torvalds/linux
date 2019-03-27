/*-
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
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
 *
 */
/*-
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/dirent.h>
#include <sys/ioccom.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/mount.h>
#include <sys/refcount.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/syscallsubr.h>
#include <sys/taskqueue.h>
#include <sys/tree.h>
#include <sys/vnode.h>
#include <machine/atomic.h>
#include <vm/uma.h>

#include <fs/autofs/autofs.h>
#include <fs/autofs/autofs_ioctl.h>

MALLOC_DEFINE(M_AUTOFS, "autofs", "Automounter filesystem");

uma_zone_t autofs_request_zone;
uma_zone_t autofs_node_zone;

static int	autofs_open(struct cdev *dev, int flags, int fmt,
		    struct thread *td);
static int	autofs_close(struct cdev *dev, int flag, int fmt,
		    struct thread *td);
static int	autofs_ioctl(struct cdev *dev, u_long cmd, caddr_t arg,
		    int mode, struct thread *td);

static struct cdevsw autofs_cdevsw = {
     .d_version = D_VERSION,
     .d_open   = autofs_open,
     .d_close   = autofs_close,
     .d_ioctl   = autofs_ioctl,
     .d_name    = "autofs",
};

/*
 * List of signals that can interrupt an autofs trigger.  Might be a good
 * idea to keep it synchronised with list in sys/fs/nfs/nfs_commonkrpc.c.
 */
int autofs_sig_set[] = {
	SIGINT,
	SIGTERM,
	SIGHUP,
	SIGKILL,
	SIGQUIT
};

struct autofs_softc	*autofs_softc;

SYSCTL_NODE(_vfs, OID_AUTO, autofs, CTLFLAG_RD, 0, "Automounter filesystem");
int autofs_debug = 1;
TUNABLE_INT("vfs.autofs.debug", &autofs_debug);
SYSCTL_INT(_vfs_autofs, OID_AUTO, debug, CTLFLAG_RWTUN,
    &autofs_debug, 1, "Enable debug messages");
int autofs_mount_on_stat = 0;
TUNABLE_INT("vfs.autofs.mount_on_stat", &autofs_mount_on_stat);
SYSCTL_INT(_vfs_autofs, OID_AUTO, mount_on_stat, CTLFLAG_RWTUN,
    &autofs_mount_on_stat, 0, "Trigger mount on stat(2) on mountpoint");
int autofs_timeout = 30;
TUNABLE_INT("vfs.autofs.timeout", &autofs_timeout);
SYSCTL_INT(_vfs_autofs, OID_AUTO, timeout, CTLFLAG_RWTUN,
    &autofs_timeout, 30, "Number of seconds to wait for automountd(8)");
int autofs_cache = 600;
TUNABLE_INT("vfs.autofs.cache", &autofs_cache);
SYSCTL_INT(_vfs_autofs, OID_AUTO, cache, CTLFLAG_RWTUN,
    &autofs_cache, 600, "Number of seconds to wait before reinvoking "
    "automountd(8) for any given file or directory");
int autofs_retry_attempts = 3;
TUNABLE_INT("vfs.autofs.retry_attempts", &autofs_retry_attempts);
SYSCTL_INT(_vfs_autofs, OID_AUTO, retry_attempts, CTLFLAG_RWTUN,
    &autofs_retry_attempts, 3, "Number of attempts before failing mount");
int autofs_retry_delay = 1;
TUNABLE_INT("vfs.autofs.retry_delay", &autofs_retry_delay);
SYSCTL_INT(_vfs_autofs, OID_AUTO, retry_delay, CTLFLAG_RWTUN,
    &autofs_retry_delay, 1, "Number of seconds before retrying");
int autofs_interruptible = 1;
TUNABLE_INT("vfs.autofs.interruptible", &autofs_interruptible);
SYSCTL_INT(_vfs_autofs, OID_AUTO, interruptible, CTLFLAG_RWTUN,
    &autofs_interruptible, 1, "Allow requests to be interrupted by signal");

static int
autofs_node_cmp(const struct autofs_node *a, const struct autofs_node *b)
{

	return (strcmp(a->an_name, b->an_name));
}

RB_GENERATE(autofs_node_tree, autofs_node, an_link, autofs_node_cmp);

int
autofs_init(struct vfsconf *vfsp)
{
	int error;

	KASSERT(autofs_softc == NULL,
	    ("softc %p, should be NULL", autofs_softc));

	autofs_softc = malloc(sizeof(*autofs_softc), M_AUTOFS,
	    M_WAITOK | M_ZERO);

	autofs_request_zone = uma_zcreate("autofs_request",
	    sizeof(struct autofs_request), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, 0);
	autofs_node_zone = uma_zcreate("autofs_node",
	    sizeof(struct autofs_node), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, 0);

	TAILQ_INIT(&autofs_softc->sc_requests);
	cv_init(&autofs_softc->sc_cv, "autofscv");
	sx_init(&autofs_softc->sc_lock, "autofslk");

	error = make_dev_p(MAKEDEV_CHECKNAME, &autofs_softc->sc_cdev,
	    &autofs_cdevsw, NULL, UID_ROOT, GID_WHEEL, 0600, "autofs");
	if (error != 0) {
		AUTOFS_WARN("failed to create device node, error %d", error);
		uma_zdestroy(autofs_request_zone);
		uma_zdestroy(autofs_node_zone);
		free(autofs_softc, M_AUTOFS);

		return (error);
	}
	autofs_softc->sc_cdev->si_drv1 = autofs_softc;

	return (0);
}

int
autofs_uninit(struct vfsconf *vfsp)
{

	sx_xlock(&autofs_softc->sc_lock);
	if (autofs_softc->sc_dev_opened) {
		sx_xunlock(&autofs_softc->sc_lock);
		return (EBUSY);
	}
	if (autofs_softc->sc_cdev != NULL)
		destroy_dev(autofs_softc->sc_cdev);

	uma_zdestroy(autofs_request_zone);
	uma_zdestroy(autofs_node_zone);

	sx_xunlock(&autofs_softc->sc_lock);
	/*
	 * XXX: Race with open?
	 */
	free(autofs_softc, M_AUTOFS);

	return (0);
}

bool
autofs_ignore_thread(const struct thread *td)
{
	struct proc *p;

	p = td->td_proc;

	if (autofs_softc->sc_dev_opened == false)
		return (false);

	PROC_LOCK(p);
	if (p->p_session->s_sid == autofs_softc->sc_dev_sid) {
		PROC_UNLOCK(p);
		return (true);
	}
	PROC_UNLOCK(p);

	return (false);
}

static char *
autofs_path(struct autofs_node *anp)
{
	struct autofs_mount *amp;
	char *path, *tmp;

	amp = anp->an_mount;

	path = strdup("", M_AUTOFS);
	for (; anp->an_parent != NULL; anp = anp->an_parent) {
		tmp = malloc(strlen(anp->an_name) + strlen(path) + 2,
		    M_AUTOFS, M_WAITOK);
		strcpy(tmp, anp->an_name);
		strcat(tmp, "/");
		strcat(tmp, path);
		free(path, M_AUTOFS);
		path = tmp;
	}

	tmp = malloc(strlen(amp->am_mountpoint) + strlen(path) + 2,
	    M_AUTOFS, M_WAITOK);
	strcpy(tmp, amp->am_mountpoint);
	strcat(tmp, "/");
	strcat(tmp, path);
	free(path, M_AUTOFS);
	path = tmp;

	return (path);
}

static void
autofs_task(void *context, int pending)
{
	struct autofs_request *ar;

	ar = context;

	sx_xlock(&autofs_softc->sc_lock);
	AUTOFS_WARN("request %d for %s timed out after %d seconds",
	    ar->ar_id, ar->ar_path, autofs_timeout);
	/*
	 * XXX: EIO perhaps?
	 */
	ar->ar_error = ETIMEDOUT;
	ar->ar_wildcards = true;
	ar->ar_done = true;
	ar->ar_in_progress = false;
	cv_broadcast(&autofs_softc->sc_cv);
	sx_xunlock(&autofs_softc->sc_lock);
}

bool
autofs_cached(struct autofs_node *anp, const char *component, int componentlen)
{
	int error;
	struct autofs_mount *amp;

	amp = anp->an_mount;

	AUTOFS_ASSERT_UNLOCKED(amp);

	/*
	 * For root node we need to request automountd(8) assistance even
	 * if the node is marked as cached, but the requested top-level
	 * directory does not exist.  This is necessary for wildcard indirect
	 * map keys to work.  We don't do this if we know that there are
	 * no wildcards.
	 */
	if (anp->an_parent == NULL && componentlen != 0 && anp->an_wildcards) {
		AUTOFS_SLOCK(amp);
		error = autofs_node_find(anp, component, componentlen, NULL);
		AUTOFS_SUNLOCK(amp);
		if (error != 0)
			return (false);
	}

	return (anp->an_cached);
}

static void
autofs_cache_callout(void *context)
{
	struct autofs_node *anp;

	anp = context;
	anp->an_cached = false;
}

void
autofs_flush(struct autofs_mount *amp)
{

	/*
	 * XXX: This will do for now, but ideally we should iterate
	 * 	over all the nodes.
	 */
	amp->am_root->an_cached = false;
	AUTOFS_DEBUG("%s flushed", amp->am_mountpoint);
}

/*
 * The set/restore sigmask functions are used to (temporarily) overwrite
 * the thread td_sigmask during triggering.
 */
static void
autofs_set_sigmask(sigset_t *oldset)
{
	sigset_t newset;
	int i;

	SIGFILLSET(newset);
	/* Remove the autofs set of signals from newset */
	PROC_LOCK(curproc);
	mtx_lock(&curproc->p_sigacts->ps_mtx);
	for (i = 0 ; i < nitems(autofs_sig_set); i++) {
		/*
		 * But make sure we leave the ones already masked
		 * by the process, i.e. remove the signal from the
		 * temporary signalmask only if it wasn't already
		 * in p_sigmask.
		 */
		if (!SIGISMEMBER(curthread->td_sigmask, autofs_sig_set[i]) &&
		    !SIGISMEMBER(curproc->p_sigacts->ps_sigignore,
		    autofs_sig_set[i])) {
			SIGDELSET(newset, autofs_sig_set[i]);
		}
	}
	mtx_unlock(&curproc->p_sigacts->ps_mtx);
	kern_sigprocmask(curthread, SIG_SETMASK, &newset, oldset,
	    SIGPROCMASK_PROC_LOCKED);
	PROC_UNLOCK(curproc);
}

static void
autofs_restore_sigmask(sigset_t *set)
{

	kern_sigprocmask(curthread, SIG_SETMASK, set, NULL, 0);
}

static int
autofs_trigger_one(struct autofs_node *anp,
    const char *component, int componentlen)
{
	sigset_t oldset;
	struct autofs_mount *amp;
	struct autofs_node *firstanp;
	struct autofs_request *ar;
	char *key, *path;
	int error = 0, request_error, last;
	bool wildcards;

	amp = anp->an_mount;

	sx_assert(&autofs_softc->sc_lock, SA_XLOCKED);

	if (anp->an_parent == NULL) {
		key = strndup(component, componentlen, M_AUTOFS);
	} else {
		for (firstanp = anp; firstanp->an_parent->an_parent != NULL;
		    firstanp = firstanp->an_parent)
			continue;
		key = strdup(firstanp->an_name, M_AUTOFS);
	}

	path = autofs_path(anp);

	TAILQ_FOREACH(ar, &autofs_softc->sc_requests, ar_next) {
		if (strcmp(ar->ar_path, path) != 0)
			continue;
		if (strcmp(ar->ar_key, key) != 0)
			continue;

		KASSERT(strcmp(ar->ar_from, amp->am_from) == 0,
		    ("from changed; %s != %s", ar->ar_from, amp->am_from));
		KASSERT(strcmp(ar->ar_prefix, amp->am_prefix) == 0,
		    ("prefix changed; %s != %s",
		     ar->ar_prefix, amp->am_prefix));
		KASSERT(strcmp(ar->ar_options, amp->am_options) == 0,
		    ("options changed; %s != %s",
		     ar->ar_options, amp->am_options));

		break;
	}

	if (ar != NULL) {
		refcount_acquire(&ar->ar_refcount);
	} else {
		ar = uma_zalloc(autofs_request_zone, M_WAITOK | M_ZERO);
		ar->ar_mount = amp;

		ar->ar_id =
		    atomic_fetchadd_int(&autofs_softc->sc_last_request_id, 1);
		strlcpy(ar->ar_from, amp->am_from, sizeof(ar->ar_from));
		strlcpy(ar->ar_path, path, sizeof(ar->ar_path));
		strlcpy(ar->ar_prefix, amp->am_prefix, sizeof(ar->ar_prefix));
		strlcpy(ar->ar_key, key, sizeof(ar->ar_key));
		strlcpy(ar->ar_options,
		    amp->am_options, sizeof(ar->ar_options));

		TIMEOUT_TASK_INIT(taskqueue_thread, &ar->ar_task, 0,
		    autofs_task, ar);
		taskqueue_enqueue_timeout(taskqueue_thread, &ar->ar_task,
		    autofs_timeout * hz);
		refcount_init(&ar->ar_refcount, 1);
		TAILQ_INSERT_TAIL(&autofs_softc->sc_requests, ar, ar_next);
	}

	cv_broadcast(&autofs_softc->sc_cv);
	while (ar->ar_done == false) {
		if (autofs_interruptible != 0) {
			autofs_set_sigmask(&oldset);
			error = cv_wait_sig(&autofs_softc->sc_cv,
			    &autofs_softc->sc_lock);
			autofs_restore_sigmask(&oldset);
			if (error != 0) {
				AUTOFS_WARN("cv_wait_sig for %s failed "
				    "with error %d", ar->ar_path, error);
				break;
			}
		} else {
			cv_wait(&autofs_softc->sc_cv, &autofs_softc->sc_lock);
		}
	}

	request_error = ar->ar_error;
	if (request_error != 0) {
		AUTOFS_WARN("request for %s completed with error %d",
		    ar->ar_path, request_error);
	}

	wildcards = ar->ar_wildcards;

	last = refcount_release(&ar->ar_refcount);
	if (last) {
		TAILQ_REMOVE(&autofs_softc->sc_requests, ar, ar_next);
		/*
		 * Unlock the sc_lock, so that autofs_task() can complete.
		 */
		sx_xunlock(&autofs_softc->sc_lock);
		taskqueue_cancel_timeout(taskqueue_thread, &ar->ar_task, NULL);
		taskqueue_drain_timeout(taskqueue_thread, &ar->ar_task);
		uma_zfree(autofs_request_zone, ar);
		sx_xlock(&autofs_softc->sc_lock);
	}

	/*
	 * Note that we do not do negative caching on purpose.  This
	 * way the user can retry access at any time, e.g. after fixing
	 * the failure reason, without waiting for cache timer to expire.
	 */
	if (error == 0 && request_error == 0 && autofs_cache > 0) {
		anp->an_cached = true;
		anp->an_wildcards = wildcards;
		callout_reset(&anp->an_callout, autofs_cache * hz,
		    autofs_cache_callout, anp);
	}

	free(key, M_AUTOFS);
	free(path, M_AUTOFS);

	if (error != 0)
		return (error);
	return (request_error);
}

/*
 * Send request to automountd(8) and wait for completion.
 */
int
autofs_trigger(struct autofs_node *anp,
    const char *component, int componentlen)
{
	int error;

	for (;;) {
		error = autofs_trigger_one(anp, component, componentlen);
		if (error == 0) {
			anp->an_retries = 0;
			return (0);
		}
		if (error == EINTR || error == ERESTART) {
			AUTOFS_DEBUG("trigger interrupted by signal, "
			    "not retrying");
			anp->an_retries = 0;
			return (error);
		}
		anp->an_retries++;
		if (anp->an_retries >= autofs_retry_attempts) {
			AUTOFS_DEBUG("trigger failed %d times; returning "
			    "error %d", anp->an_retries, error);
			anp->an_retries = 0;
			return (error);

		}
		AUTOFS_DEBUG("trigger failed with error %d; will retry in "
		    "%d seconds, %d attempts left", error, autofs_retry_delay,
		    autofs_retry_attempts - anp->an_retries);
		sx_xunlock(&autofs_softc->sc_lock);
		pause("autofs_retry", autofs_retry_delay * hz);
		sx_xlock(&autofs_softc->sc_lock);
	}
}

static int
autofs_ioctl_request(struct autofs_daemon_request *adr)
{
	struct autofs_request *ar;
	int error;

	sx_xlock(&autofs_softc->sc_lock);
	for (;;) {
		TAILQ_FOREACH(ar, &autofs_softc->sc_requests, ar_next) {
			if (ar->ar_done)
				continue;
			if (ar->ar_in_progress)
				continue;

			break;
		}

		if (ar != NULL)
			break;

		error = cv_wait_sig(&autofs_softc->sc_cv,
		    &autofs_softc->sc_lock);
		if (error != 0) {
			sx_xunlock(&autofs_softc->sc_lock);
			return (error);
		}
	}

	ar->ar_in_progress = true;
	sx_xunlock(&autofs_softc->sc_lock);

	adr->adr_id = ar->ar_id;
	strlcpy(adr->adr_from, ar->ar_from, sizeof(adr->adr_from));
	strlcpy(adr->adr_path, ar->ar_path, sizeof(adr->adr_path));
	strlcpy(adr->adr_prefix, ar->ar_prefix, sizeof(adr->adr_prefix));
	strlcpy(adr->adr_key, ar->ar_key, sizeof(adr->adr_key));
	strlcpy(adr->adr_options, ar->ar_options, sizeof(adr->adr_options));

	PROC_LOCK(curproc);
	autofs_softc->sc_dev_sid = curproc->p_session->s_sid;
	PROC_UNLOCK(curproc);

	return (0);
}

static int
autofs_ioctl_done_101(struct autofs_daemon_done_101 *add)
{
	struct autofs_request *ar;

	sx_xlock(&autofs_softc->sc_lock);
	TAILQ_FOREACH(ar, &autofs_softc->sc_requests, ar_next) {
		if (ar->ar_id == add->add_id)
			break;
	}

	if (ar == NULL) {
		sx_xunlock(&autofs_softc->sc_lock);
		AUTOFS_DEBUG("id %d not found", add->add_id);
		return (ESRCH);
	}

	ar->ar_error = add->add_error;
	ar->ar_wildcards = true;
	ar->ar_done = true;
	ar->ar_in_progress = false;
	cv_broadcast(&autofs_softc->sc_cv);

	sx_xunlock(&autofs_softc->sc_lock);

	return (0);
}

static int
autofs_ioctl_done(struct autofs_daemon_done *add)
{
	struct autofs_request *ar;

	sx_xlock(&autofs_softc->sc_lock);
	TAILQ_FOREACH(ar, &autofs_softc->sc_requests, ar_next) {
		if (ar->ar_id == add->add_id)
			break;
	}

	if (ar == NULL) {
		sx_xunlock(&autofs_softc->sc_lock);
		AUTOFS_DEBUG("id %d not found", add->add_id);
		return (ESRCH);
	}

	ar->ar_error = add->add_error;
	ar->ar_wildcards = add->add_wildcards;
	ar->ar_done = true;
	ar->ar_in_progress = false;
	cv_broadcast(&autofs_softc->sc_cv);

	sx_xunlock(&autofs_softc->sc_lock);

	return (0);
}

static int
autofs_open(struct cdev *dev, int flags, int fmt, struct thread *td)
{

	sx_xlock(&autofs_softc->sc_lock);
	/*
	 * We must never block automountd(8) and its descendants, and we use
	 * session ID to determine that: we store session id of the process
	 * that opened the device, and then compare it with session ids
	 * of triggering processes.  This means running a second automountd(8)
	 * instance would break the previous one.  The check below prevents
	 * it from happening.
	 */
	if (autofs_softc->sc_dev_opened) {
		sx_xunlock(&autofs_softc->sc_lock);
		return (EBUSY);
	}

	autofs_softc->sc_dev_opened = true;
	sx_xunlock(&autofs_softc->sc_lock);

	return (0);
}

static int
autofs_close(struct cdev *dev, int flag, int fmt, struct thread *td)
{

	sx_xlock(&autofs_softc->sc_lock);
	KASSERT(autofs_softc->sc_dev_opened, ("not opened?"));
	autofs_softc->sc_dev_opened = false;
	sx_xunlock(&autofs_softc->sc_lock);

	return (0);
}

static int
autofs_ioctl(struct cdev *dev, u_long cmd, caddr_t arg, int mode,
    struct thread *td)
{

	KASSERT(autofs_softc->sc_dev_opened, ("not opened?"));

	switch (cmd) {
	case AUTOFSREQUEST:
		return (autofs_ioctl_request(
		    (struct autofs_daemon_request *)arg));
	case AUTOFSDONE101:
		return (autofs_ioctl_done_101(
		    (struct autofs_daemon_done_101 *)arg));
	case AUTOFSDONE:
		return (autofs_ioctl_done(
		    (struct autofs_daemon_done *)arg));
	default:
		AUTOFS_DEBUG("invalid cmd %lx", cmd);
		return (EINVAL);
	}
}
