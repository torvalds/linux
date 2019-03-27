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

#include <sys/eventhandler.h>
#include <sys/filedesc.h>
#include <sys/imgact.h>
#include <sys/priv.h>
#include <sys/sx.h>
#include <sys/sysent.h>
#include <sys/vnode.h>

#include <machine/stdarg.h>

static void filemon_output_event(struct filemon *filemon, const char *fmt, ...)
    __printflike(2, 3);

static eventhandler_tag filemon_exec_tag;
static eventhandler_tag filemon_exit_tag;
static eventhandler_tag filemon_fork_tag;

static void
filemon_output(struct filemon *filemon, char *msg, size_t len)
{
	struct uio auio;
	struct iovec aiov;
	int error;

	if (filemon->fp == NULL)
		return;

	aiov.iov_base = msg;
	aiov.iov_len = len;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_resid = len;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_rw = UIO_WRITE;
	auio.uio_td = curthread;
	auio.uio_offset = (off_t) -1;

	if (filemon->fp->f_type == DTYPE_VNODE)
		bwillwrite();

	error = fo_write(filemon->fp, &auio, filemon->cred, 0, curthread);
	if (error != 0 && filemon->error == 0)
		filemon->error = error;
}

static void
filemon_output_event(struct filemon *filemon, const char *fmt, ...)
{
	va_list ap;
	size_t len;

	va_start(ap, fmt);
	len = vsnprintf(filemon->msgbufr, sizeof(filemon->msgbufr), fmt, ap);
	va_end(ap);
	/* The event is truncated but still worth logging. */
	if (len >= sizeof(filemon->msgbufr))
		len = sizeof(filemon->msgbufr) - 1;
	filemon_output(filemon, filemon->msgbufr, len);
}

static int
filemon_wrapper_chdir(struct thread *td, struct chdir_args *uap)
{
	int error, ret;
	struct filemon *filemon;

	if ((ret = sys_chdir(td, uap)) == 0) {
		if ((filemon = filemon_proc_get(curproc)) != NULL) {
			if ((error = copyinstr(uap->path, filemon->fname1,
			    sizeof(filemon->fname1), NULL)) != 0) {
				filemon->error = error;
				goto copyfail;
			}

			filemon_output_event(filemon, "C %d %s\n",
			    curproc->p_pid, filemon->fname1);
copyfail:
			filemon_drop(filemon);
		}
	}

	return (ret);
}

static void
filemon_event_process_exec(void *arg __unused, struct proc *p,
    struct image_params *imgp)
{
	struct filemon *filemon;

	if ((filemon = filemon_proc_get(p)) != NULL) {
		filemon_output_event(filemon, "E %d %s\n",
		    p->p_pid,
		    imgp->execpath != NULL ? imgp->execpath : "<unknown>");

		/* If the credentials changed then cease tracing. */
		if (imgp->newcred != NULL &&
		    imgp->credential_setid &&
		    priv_check_cred(filemon->cred, PRIV_DEBUG_DIFFCRED) != 0) {
			/*
			 * It may have changed to NULL already, but
			 * will not be re-attached by anything else.
			 */
			if (p->p_filemon != NULL) {
				KASSERT(p->p_filemon == filemon,
				    ("%s: proc %p didn't have expected"
				    " filemon %p", __func__, p, filemon));
				filemon_proc_drop(p);
			}
		}


		filemon_drop(filemon);
	}
}

static void
_filemon_wrapper_openat(struct thread *td, const char *upath, int flags,
    int fd)
{
	int error;
	struct file *fp;
	struct filemon *filemon;
	char *atpath, *freepath;
	cap_rights_t rights;

	if ((filemon = filemon_proc_get(curproc)) != NULL) {
		atpath = "";
		freepath = NULL;
		fp = NULL;

		if ((error = copyinstr(upath, filemon->fname1,
		    sizeof(filemon->fname1), NULL)) != 0) {
			filemon->error = error;
			goto copyfail;
		}

		if (filemon->fname1[0] != '/' && fd != AT_FDCWD) {
			/*
			 * rats - we cannot do too much about this.
			 * the trace should show a dir we read
			 * recently.. output an A record as a clue
			 * until we can do better.
			 * XXX: This may be able to come out with
			 * the namecache lookup now.
			 */
			filemon_output_event(filemon, "A %d %s\n",
			    curproc->p_pid, filemon->fname1);
			/*
			 * Try to resolve the path from the vnode using the
			 * namecache.  It may be inaccurate, but better
			 * than nothing.
			 */
			if (getvnode(td, fd,
			    cap_rights_init(&rights, CAP_LOOKUP), &fp) == 0) {
				vn_fullpath(td, fp->f_vnode, &atpath,
				    &freepath);
			}
		}
		if (flags & O_RDWR) {
			/*
			 * We'll get the W record below, but need
			 * to also output an R to distinguish from
			 * O_WRONLY.
			 */
			filemon_output_event(filemon, "R %d %s%s%s\n",
			    curproc->p_pid, atpath,
			    atpath[0] != '\0' ? "/" : "", filemon->fname1);
		}

		filemon_output_event(filemon, "%c %d %s%s%s\n",
		    (flags & O_ACCMODE) ? 'W':'R',
		    curproc->p_pid, atpath,
		    atpath[0] != '\0' ? "/" : "", filemon->fname1);
copyfail:
		filemon_drop(filemon);
		if (fp != NULL)
			fdrop(fp, td);
		free(freepath, M_TEMP);
	}
}

static int
filemon_wrapper_open(struct thread *td, struct open_args *uap)
{
	int ret;

	if ((ret = sys_open(td, uap)) == 0)
		_filemon_wrapper_openat(td, uap->path, uap->flags, AT_FDCWD);

	return (ret);
}

static int
filemon_wrapper_openat(struct thread *td, struct openat_args *uap)
{
	int ret;

	if ((ret = sys_openat(td, uap)) == 0)
		_filemon_wrapper_openat(td, uap->path, uap->flag, uap->fd);

	return (ret);
}

static int
filemon_wrapper_rename(struct thread *td, struct rename_args *uap)
{
	int error, ret;
	struct filemon *filemon;

	if ((ret = sys_rename(td, uap)) == 0) {
		if ((filemon = filemon_proc_get(curproc)) != NULL) {
			if (((error = copyinstr(uap->from, filemon->fname1,
			     sizeof(filemon->fname1), NULL)) != 0) ||
			    ((error = copyinstr(uap->to, filemon->fname2,
			     sizeof(filemon->fname2), NULL)) != 0)) {
				filemon->error = error;
				goto copyfail;
			}

			filemon_output_event(filemon, "M %d '%s' '%s'\n",
			    curproc->p_pid, filemon->fname1, filemon->fname2);
copyfail:
			filemon_drop(filemon);
		}
	}

	return (ret);
}

static void
_filemon_wrapper_link(struct thread *td, const char *upath1,
    const char *upath2)
{
	struct filemon *filemon;
	int error;

	if ((filemon = filemon_proc_get(curproc)) != NULL) {
		if (((error = copyinstr(upath1, filemon->fname1,
		     sizeof(filemon->fname1), NULL)) != 0) ||
		    ((error = copyinstr(upath2, filemon->fname2,
		     sizeof(filemon->fname2), NULL)) != 0)) {
			filemon->error = error;
			goto copyfail;
		}

		filemon_output_event(filemon, "L %d '%s' '%s'\n",
		    curproc->p_pid, filemon->fname1, filemon->fname2);
copyfail:
		filemon_drop(filemon);
	}
}

static int
filemon_wrapper_link(struct thread *td, struct link_args *uap)
{
	int ret;

	if ((ret = sys_link(td, uap)) == 0)
		_filemon_wrapper_link(td, uap->path, uap->link);

	return (ret);
}

static int
filemon_wrapper_symlink(struct thread *td, struct symlink_args *uap)
{
	int ret;

	if ((ret = sys_symlink(td, uap)) == 0)
		_filemon_wrapper_link(td, uap->path, uap->link);

	return (ret);
}

static int
filemon_wrapper_linkat(struct thread *td, struct linkat_args *uap)
{
	int ret;

	if ((ret = sys_linkat(td, uap)) == 0)
		_filemon_wrapper_link(td, uap->path1, uap->path2);

	return (ret);
}

static void
filemon_event_process_exit(void *arg __unused, struct proc *p)
{
	struct filemon *filemon;

	if ((filemon = filemon_proc_get(p)) != NULL) {
		filemon_output_event(filemon, "X %d %d %d\n",
		    p->p_pid, p->p_xexit, p->p_xsig);

		/*
		 * filemon_untrack_processes() may have dropped this p_filemon
		 * already while in filemon_proc_get() before acquiring the
		 * filemon lock.
		 */
		KASSERT(p->p_filemon == NULL || p->p_filemon == filemon,
		    ("%s: p %p was attached while exiting, expected "
		    "filemon %p or NULL", __func__, p, filemon));
		if (p->p_filemon == filemon)
			filemon_proc_drop(p);

		filemon_drop(filemon);
	}
}

static int
filemon_wrapper_unlink(struct thread *td, struct unlink_args *uap)
{
	int error, ret;
	struct filemon *filemon;

	if ((ret = sys_unlink(td, uap)) == 0) {
		if ((filemon = filemon_proc_get(curproc)) != NULL) {
			if ((error = copyinstr(uap->path, filemon->fname1,
			    sizeof(filemon->fname1), NULL)) != 0) {
				filemon->error = error;
				goto copyfail;
			}

			filemon_output_event(filemon, "D %d %s\n",
			    curproc->p_pid, filemon->fname1);
copyfail:
			filemon_drop(filemon);
		}
	}

	return (ret);
}

static void
filemon_event_process_fork(void *arg __unused, struct proc *p1,
    struct proc *p2, int flags __unused)
{
	struct filemon *filemon;

	if ((filemon = filemon_proc_get(p1)) != NULL) {
		filemon_output_event(filemon, "F %d %d\n",
		    p1->p_pid, p2->p_pid);

		/*
		 * filemon_untrack_processes() or
		 * filemon_ioctl(FILEMON_SET_PID) may have changed the parent's
		 * p_filemon while in filemon_proc_get() before acquiring the
		 * filemon lock.  Only inherit if the parent is still traced by
		 * this filemon.
		 */
		if (p1->p_filemon == filemon) {
			PROC_LOCK(p2);
			/*
			 * It may have been attached to already by a new
			 * filemon.
			 */
			if (p2->p_filemon == NULL) {
				p2->p_filemon = filemon_acquire(filemon);
				++filemon->proccnt;
			}
			PROC_UNLOCK(p2);
		}

		filemon_drop(filemon);
	}
}

static void
filemon_wrapper_install(void)
{

	sysent[SYS_chdir].sy_call = (sy_call_t *) filemon_wrapper_chdir;
	sysent[SYS_open].sy_call = (sy_call_t *) filemon_wrapper_open;
	sysent[SYS_openat].sy_call = (sy_call_t *) filemon_wrapper_openat;
	sysent[SYS_rename].sy_call = (sy_call_t *) filemon_wrapper_rename;
	sysent[SYS_unlink].sy_call = (sy_call_t *) filemon_wrapper_unlink;
	sysent[SYS_link].sy_call = (sy_call_t *) filemon_wrapper_link;
	sysent[SYS_symlink].sy_call = (sy_call_t *) filemon_wrapper_symlink;
	sysent[SYS_linkat].sy_call = (sy_call_t *) filemon_wrapper_linkat;

#if defined(COMPAT_FREEBSD32)
	freebsd32_sysent[FREEBSD32_SYS_chdir].sy_call = (sy_call_t *) filemon_wrapper_chdir;
	freebsd32_sysent[FREEBSD32_SYS_open].sy_call = (sy_call_t *) filemon_wrapper_open;
	freebsd32_sysent[FREEBSD32_SYS_openat].sy_call = (sy_call_t *) filemon_wrapper_openat;
	freebsd32_sysent[FREEBSD32_SYS_rename].sy_call = (sy_call_t *) filemon_wrapper_rename;
	freebsd32_sysent[FREEBSD32_SYS_unlink].sy_call = (sy_call_t *) filemon_wrapper_unlink;
	freebsd32_sysent[FREEBSD32_SYS_link].sy_call = (sy_call_t *) filemon_wrapper_link;
	freebsd32_sysent[FREEBSD32_SYS_symlink].sy_call = (sy_call_t *) filemon_wrapper_symlink;
	freebsd32_sysent[FREEBSD32_SYS_linkat].sy_call = (sy_call_t *) filemon_wrapper_linkat;
#endif	/* COMPAT_FREEBSD32 */

	filemon_exec_tag = EVENTHANDLER_REGISTER(process_exec,
	    filemon_event_process_exec, NULL, EVENTHANDLER_PRI_LAST);
	filemon_exit_tag = EVENTHANDLER_REGISTER(process_exit,
	    filemon_event_process_exit, NULL, EVENTHANDLER_PRI_LAST);
	filemon_fork_tag = EVENTHANDLER_REGISTER(process_fork,
	    filemon_event_process_fork, NULL, EVENTHANDLER_PRI_LAST);
}

static void
filemon_wrapper_deinstall(void)
{

	sysent[SYS_chdir].sy_call = (sy_call_t *)sys_chdir;
	sysent[SYS_open].sy_call = (sy_call_t *)sys_open;
	sysent[SYS_openat].sy_call = (sy_call_t *)sys_openat;
	sysent[SYS_rename].sy_call = (sy_call_t *)sys_rename;
	sysent[SYS_unlink].sy_call = (sy_call_t *)sys_unlink;
	sysent[SYS_link].sy_call = (sy_call_t *)sys_link;
	sysent[SYS_symlink].sy_call = (sy_call_t *)sys_symlink;
	sysent[SYS_linkat].sy_call = (sy_call_t *)sys_linkat;

#if defined(COMPAT_FREEBSD32)
	freebsd32_sysent[FREEBSD32_SYS_chdir].sy_call = (sy_call_t *)sys_chdir;
	freebsd32_sysent[FREEBSD32_SYS_open].sy_call = (sy_call_t *)sys_open;
	freebsd32_sysent[FREEBSD32_SYS_openat].sy_call = (sy_call_t *)sys_openat;
	freebsd32_sysent[FREEBSD32_SYS_rename].sy_call = (sy_call_t *)sys_rename;
	freebsd32_sysent[FREEBSD32_SYS_unlink].sy_call = (sy_call_t *)sys_unlink;
	freebsd32_sysent[FREEBSD32_SYS_link].sy_call = (sy_call_t *)sys_link;
	freebsd32_sysent[FREEBSD32_SYS_symlink].sy_call = (sy_call_t *)sys_symlink;
	freebsd32_sysent[FREEBSD32_SYS_linkat].sy_call = (sy_call_t *)sys_linkat;
#endif	/* COMPAT_FREEBSD32 */

	EVENTHANDLER_DEREGISTER(process_exec, filemon_exec_tag);
	EVENTHANDLER_DEREGISTER(process_exit, filemon_exit_tag);
	EVENTHANDLER_DEREGISTER(process_fork, filemon_fork_tag);
}
