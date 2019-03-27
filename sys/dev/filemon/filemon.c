/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011, David E. O'Brien.
 * Copyright (c) 2009-2011, Juniper Networks, Inc.
 * Copyright (c) 2015-2016, EMC Corp.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY JUNIPER NETWORKS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL JUNIPER NETWORKS OR CONTRIBUTORS BE LIABLE
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
#include <sys/file.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/capsicum.h>
#include <sys/condvar.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/ioccom.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/sx.h>
#include <sys/syscall.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/uio.h>

#include "filemon.h"

#if defined(COMPAT_FREEBSD32)
#include <compat/freebsd32/freebsd32_syscall.h>
#include <compat/freebsd32/freebsd32_proto.h>
#include <compat/freebsd32/freebsd32_util.h>
#endif

static d_close_t	filemon_close;
static d_ioctl_t	filemon_ioctl;
static d_open_t		filemon_open;

static struct cdevsw filemon_cdevsw = {
	.d_version	= D_VERSION,
	.d_close	= filemon_close,
	.d_ioctl	= filemon_ioctl,
	.d_open		= filemon_open,
	.d_name		= "filemon",
};

MALLOC_DECLARE(M_FILEMON);
MALLOC_DEFINE(M_FILEMON, "filemon", "File access monitor");

/*
 * The filemon->lock protects several things currently:
 * - fname1/fname2/msgbufr are pre-allocated and used per syscall
 *   for logging and copyins rather than stack variables.
 * - Serializing the filemon's log output.
 * - Preventing inheritance or removal of the filemon into proc.p_filemon.
 */
struct filemon {
	struct sx	lock;		/* Lock for this filemon. */
	struct file	*fp;		/* Output file pointer. */
	struct ucred	*cred;		/* Credential of tracer. */
	char		fname1[MAXPATHLEN]; /* Temporary filename buffer. */
	char		fname2[MAXPATHLEN]; /* Temporary filename buffer. */
	char		msgbufr[2*MAXPATHLEN + 100];	/* Output message buffer. */
	int		error;		/* Log write error, returned on close(2). */
	u_int		refcnt;		/* Pointer reference count. */
	u_int		proccnt;	/* Process count. */
};

static struct cdev *filemon_dev;
static void filemon_output(struct filemon *filemon, char *msg, size_t len);

static __inline struct filemon *
filemon_acquire(struct filemon *filemon)
{

	if (filemon != NULL)
		refcount_acquire(&filemon->refcnt);
	return (filemon);
}

/*
 * Release a reference and free on the last one.
 */
static void
filemon_release(struct filemon *filemon)
{

	if (refcount_release(&filemon->refcnt) == 0)
		return;
	/*
	 * There are valid cases of releasing while locked, such as in
	 * filemon_untrack_processes, but none which are done where there
	 * is not at least 1 reference remaining.
	 */
	sx_assert(&filemon->lock, SA_UNLOCKED);

	if (filemon->cred != NULL)
		crfree(filemon->cred);
	sx_destroy(&filemon->lock);
	free(filemon, M_FILEMON);
}

/*
 * Acquire the proc's p_filemon reference and lock the filemon.
 * The proc's p_filemon may not match this filemon on return.
 */
static struct filemon *
filemon_proc_get(struct proc *p)
{
	struct filemon *filemon;

	if (p->p_filemon == NULL)
		return (NULL);
	PROC_LOCK(p);
	filemon = filemon_acquire(p->p_filemon);
	PROC_UNLOCK(p);

	if (filemon == NULL)
		return (NULL);
	/*
	 * The p->p_filemon may have changed by now.  That case is handled
	 * by the exit and fork hooks and filemon_attach_proc specially.
	 */
	sx_xlock(&filemon->lock);
	return (filemon);
}

/* Remove and release the filemon on the given process. */
static void
filemon_proc_drop(struct proc *p)
{
	struct filemon *filemon;

	KASSERT(p->p_filemon != NULL, ("%s: proc %p NULL p_filemon",
	    __func__, p));
	sx_assert(&p->p_filemon->lock, SA_XLOCKED);
	PROC_LOCK(p);
	filemon = p->p_filemon;
	p->p_filemon = NULL;
	--filemon->proccnt;
	PROC_UNLOCK(p);
	/*
	 * This should not be the last reference yet.  filemon_release()
	 * cannot be called with filemon locked, which the caller expects
	 * will stay locked.
	 */
	KASSERT(filemon->refcnt > 1, ("%s: proc %p dropping filemon %p "
	    "with last reference", __func__, p, filemon));
	filemon_release(filemon);
}

/* Unlock and release the filemon. */
static __inline void
filemon_drop(struct filemon *filemon)
{

	sx_xunlock(&filemon->lock);
	filemon_release(filemon);
}

#include "filemon_wrapper.c"

static void
filemon_write_header(struct filemon *filemon)
{
	int len;
	struct timeval now;

	getmicrotime(&now);

	len = snprintf(filemon->msgbufr, sizeof(filemon->msgbufr),
	    "# filemon version %d\n# Target pid %d\n# Start %ju.%06ju\nV %d\n",
	    FILEMON_VERSION, curproc->p_pid, (uintmax_t)now.tv_sec,
	    (uintmax_t)now.tv_usec, FILEMON_VERSION);
	if (len < sizeof(filemon->msgbufr))
		filemon_output(filemon, filemon->msgbufr, len);
}

/*
 * Invalidate the passed filemon in all processes.
 */
static void
filemon_untrack_processes(struct filemon *filemon)
{
	struct proc *p;

	sx_assert(&filemon->lock, SA_XLOCKED);

	/* Avoid allproc loop if there is no need. */
	if (filemon->proccnt == 0)
		return;

	/*
	 * Processes in this list won't go away while here since
	 * filemon_event_process_exit() will lock on filemon->lock
	 * which we hold.
	 */
	sx_slock(&allproc_lock);
	FOREACH_PROC_IN_SYSTEM(p) {
		/*
		 * No PROC_LOCK is needed to compare here since it is
		 * guaranteed to not change since we have its filemon
		 * locked.  Everything that changes this p_filemon will
		 * be locked on it.
		 */
		if (p->p_filemon == filemon)
			filemon_proc_drop(p);
	}
	sx_sunlock(&allproc_lock);

	/*
	 * It's possible some references were acquired but will be
	 * dropped shortly as they are restricted from being
	 * inherited.  There is at least the reference in cdevpriv remaining.
	 */
	KASSERT(filemon->refcnt > 0, ("%s: filemon %p should have "
	    "references still.", __func__, filemon));
	KASSERT(filemon->proccnt == 0, ("%s: filemon %p should not have "
	    "attached procs still.", __func__, filemon));
}

/*
 * Close out the log.
 */
static void
filemon_close_log(struct filemon *filemon)
{
	struct file *fp;
	struct timeval now;
	size_t len;

	sx_assert(&filemon->lock, SA_XLOCKED);
	if (filemon->fp == NULL)
		return;

	getmicrotime(&now);

	len = snprintf(filemon->msgbufr,
	    sizeof(filemon->msgbufr),
	    "# Stop %ju.%06ju\n# Bye bye\n",
	    (uintmax_t)now.tv_sec, (uintmax_t)now.tv_usec);

	if (len < sizeof(filemon->msgbufr))
		filemon_output(filemon, filemon->msgbufr, len);
	fp = filemon->fp;
	filemon->fp = NULL;

	sx_xunlock(&filemon->lock);
	fdrop(fp, curthread);
	sx_xlock(&filemon->lock);
}

/*
 * The devfs file is being closed.  Untrace all processes.  It is possible
 * filemon_close/close(2) was not called.
 */
static void
filemon_dtr(void *data)
{
	struct filemon *filemon = data;

	if (filemon == NULL)
		return;

	sx_xlock(&filemon->lock);
	/*
	 * Detach the filemon.  It cannot be inherited after this.
	 */
	filemon_untrack_processes(filemon);
	filemon_close_log(filemon);
	filemon_drop(filemon);
}

/* Attach the filemon to the process. */
static int
filemon_attach_proc(struct filemon *filemon, struct proc *p)
{
	struct filemon *filemon2;

	sx_assert(&filemon->lock, SA_XLOCKED);
	PROC_LOCK_ASSERT(p, MA_OWNED);
	KASSERT((p->p_flag & P_WEXIT) == 0,
	    ("%s: filemon %p attaching to exiting process %p",
	    __func__, filemon, p));
	KASSERT((p->p_flag & P_INEXEC) == 0,
	    ("%s: filemon %p attaching to execing process %p",
	    __func__, filemon, p));

	if (p->p_filemon == filemon)
		return (0);
	/*
	 * Don't allow truncating other process traces.  It is
	 * not really intended to trace procs other than curproc
	 * anyhow.
	 */
	if (p->p_filemon != NULL && p != curproc)
		return (EBUSY);
	/*
	 * Historic behavior of filemon has been to let a child initiate
	 * tracing on itself and cease existing tracing.  Bmake
	 * .META + .MAKE relies on this.  It is only relevant for attaching to
	 * curproc.
	 */
	while (p->p_filemon != NULL) {
		PROC_UNLOCK(p);
		sx_xunlock(&filemon->lock);
		while ((filemon2 = filemon_proc_get(p)) != NULL) {
			/* It may have changed. */
			if (p->p_filemon == filemon2)
				filemon_proc_drop(p);
			filemon_drop(filemon2);
		}
		sx_xlock(&filemon->lock);
		PROC_LOCK(p);
		/*
		 * It may have been attached to, though unlikely.
		 * Try again if needed.
		 */
	}

	KASSERT(p->p_filemon == NULL,
	    ("%s: proc %p didn't detach filemon %p", __func__, p,
	    p->p_filemon));
	p->p_filemon = filemon_acquire(filemon);
	++filemon->proccnt;

	return (0);
}

static int
filemon_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag __unused,
    struct thread *td)
{
	int error = 0;
	struct filemon *filemon;
	struct proc *p;

	if ((error = devfs_get_cdevpriv((void **) &filemon)) != 0)
		return (error);

	sx_xlock(&filemon->lock);

	switch (cmd) {
	/* Set the output file descriptor. */
	case FILEMON_SET_FD:
		if (filemon->fp != NULL) {
			error = EEXIST;
			break;
		}

		error = fget_write(td, *(int *)data,
		    &cap_pwrite_rights,
		    &filemon->fp);
		if (error == 0)
			/* Write the file header. */
			filemon_write_header(filemon);
		break;

	/* Set the monitored process ID. */
	case FILEMON_SET_PID:
		/* Invalidate any existing processes already set. */
		filemon_untrack_processes(filemon);

		error = pget(*((pid_t *)data),
		    PGET_CANDEBUG | PGET_NOTWEXIT | PGET_NOTINEXEC, &p);
		if (error == 0) {
			KASSERT(p->p_filemon != filemon,
			    ("%s: proc %p didn't untrack filemon %p",
			    __func__, p, filemon));
			error = filemon_attach_proc(filemon, p);
			PROC_UNLOCK(p);
		}
		break;

	default:
		error = EINVAL;
		break;
	}

	sx_xunlock(&filemon->lock);
	return (error);
}

static int
filemon_open(struct cdev *dev, int oflags __unused, int devtype __unused,
    struct thread *td)
{
	int error;
	struct filemon *filemon;

	filemon = malloc(sizeof(*filemon), M_FILEMON,
	    M_WAITOK | M_ZERO);
	sx_init(&filemon->lock, "filemon");
	refcount_init(&filemon->refcnt, 1);
	filemon->cred = crhold(td->td_ucred);

	error = devfs_set_cdevpriv(filemon, filemon_dtr);
	if (error != 0)
		filemon_release(filemon);

	return (error);
}

/* Called on close of last devfs file handle, before filemon_dtr(). */
static int
filemon_close(struct cdev *dev __unused, int flag __unused, int fmt __unused,
    struct thread *td __unused)
{
	struct filemon *filemon;
	int error;

	if ((error = devfs_get_cdevpriv((void **) &filemon)) != 0)
		return (error);

	sx_xlock(&filemon->lock);
	filemon_close_log(filemon);
	error = filemon->error;
	sx_xunlock(&filemon->lock);
	/*
	 * Processes are still being traced but won't log anything
	 * now.  After this call returns filemon_dtr() is called which
	 * will detach processes.
	 */

	return (error);
}

static void
filemon_load(void *dummy __unused)
{

	/* Install the syscall wrappers. */
	filemon_wrapper_install();

	filemon_dev = make_dev(&filemon_cdevsw, 0, UID_ROOT, GID_WHEEL, 0666,
	    "filemon");
}

static int
filemon_unload(void)
{

	destroy_dev(filemon_dev);
	filemon_wrapper_deinstall();

	return (0);
}

static int
filemon_modevent(module_t mod __unused, int type, void *data)
{
	int error = 0;

	switch (type) {
	case MOD_LOAD:
		filemon_load(data);
		break;

	case MOD_UNLOAD:
		error = filemon_unload();
		break;

	case MOD_QUIESCE:
		/*
		 * The wrapper implementation is unsafe for reliable unload.
		 * Require forcing an unload.
		 */
		error = EBUSY;
		break;

	case MOD_SHUTDOWN:
		break;

	default:
		error = EOPNOTSUPP;
		break;

	}

	return (error);
}

DEV_MODULE(filemon, filemon_modevent, NULL);
MODULE_VERSION(filemon, 1);
