/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1999-2009 Apple Inc.
 * Copyright (c) 2005, 2016-2017 Robert N. M. Watson
 * All rights reserved.
 *
 * Portions of this software were developed by BAE Systems, the University of
 * Cambridge Computer Laboratory, and Memorial University under DARPA/AFRL
 * contract FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent
 * Computing (TC) research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/fcntl.h>
#include <sys/filedesc.h>
#include <sys/libkern.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/sem.h>
#include <sys/sbuf.h>
#include <sys/sx.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/vnode.h>

#include <bsm/audit.h>
#include <bsm/audit_kevents.h>
#include <security/audit/audit.h>
#include <security/audit/audit_private.h>

struct aue_open_event {
	int		aoe_flags;
	au_event_t	aoe_event;
};

static const struct aue_open_event aue_open[] = {
	{ O_RDONLY,					AUE_OPEN_R },
	{ (O_RDONLY | O_CREAT),				AUE_OPEN_RC },
	{ (O_RDONLY | O_CREAT | O_TRUNC),		AUE_OPEN_RTC },
	{ (O_RDONLY | O_TRUNC),				AUE_OPEN_RT },
	{ O_RDWR,					AUE_OPEN_RW },
	{ (O_RDWR | O_CREAT),				AUE_OPEN_RWC },
	{ (O_RDWR | O_CREAT | O_TRUNC),			AUE_OPEN_RWTC },
	{ (O_RDWR | O_TRUNC),				AUE_OPEN_RWT },
	{ O_WRONLY,					AUE_OPEN_W },
	{ (O_WRONLY | O_CREAT),				AUE_OPEN_WC },
	{ (O_WRONLY | O_CREAT | O_TRUNC),		AUE_OPEN_WTC },
	{ (O_WRONLY | O_TRUNC),				AUE_OPEN_WT },
};

static const struct aue_open_event aue_openat[] = {
	{ O_RDONLY,					AUE_OPENAT_R },
	{ (O_RDONLY | O_CREAT),				AUE_OPENAT_RC },
	{ (O_RDONLY | O_CREAT | O_TRUNC),		AUE_OPENAT_RTC },
	{ (O_RDONLY | O_TRUNC),				AUE_OPENAT_RT },
	{ O_RDWR,					AUE_OPENAT_RW },
	{ (O_RDWR | O_CREAT),				AUE_OPENAT_RWC },
	{ (O_RDWR | O_CREAT | O_TRUNC),			AUE_OPENAT_RWTC },
	{ (O_RDWR | O_TRUNC),				AUE_OPENAT_RWT },
	{ O_WRONLY,					AUE_OPENAT_W },
	{ (O_WRONLY | O_CREAT),				AUE_OPENAT_WC },
	{ (O_WRONLY | O_CREAT | O_TRUNC),		AUE_OPENAT_WTC },
	{ (O_WRONLY | O_TRUNC),				AUE_OPENAT_WT },
};

static const int aue_msgsys[] = {
	/* 0 */ AUE_MSGCTL,
	/* 1 */ AUE_MSGGET,
	/* 2 */ AUE_MSGSND,
	/* 3 */ AUE_MSGRCV,
};
static const int aue_msgsys_count = sizeof(aue_msgsys) / sizeof(int);

static const int aue_semsys[] = {
	/* 0 */ AUE_SEMCTL,
	/* 1 */ AUE_SEMGET,
	/* 2 */ AUE_SEMOP,
};
static const int aue_semsys_count = sizeof(aue_semsys) / sizeof(int);

static const int aue_shmsys[] = {
	/* 0 */ AUE_SHMAT,
	/* 1 */ AUE_SHMDT,
	/* 2 */ AUE_SHMGET,
	/* 3 */ AUE_SHMCTL,
};
static const int aue_shmsys_count = sizeof(aue_shmsys) / sizeof(int);

/*
 * Check whether an event is auditable by comparing the mask of classes this
 * event is part of against the given mask.
 */
int
au_preselect(au_event_t event, au_class_t class, au_mask_t *mask_p, int sorf)
{
	au_class_t effmask = 0;

	if (mask_p == NULL)
		return (-1);

	/*
	 * Perform the actual check of the masks against the event.
	 */
	if (sorf & AU_PRS_SUCCESS)
		effmask |= (mask_p->am_success & class);

	if (sorf & AU_PRS_FAILURE)
		effmask |= (mask_p->am_failure & class);

	if (effmask)
		return (1);
	else
		return (0);
}

/*
 * Convert sysctl names and present arguments to events.
 */
au_event_t
audit_ctlname_to_sysctlevent(int name[], uint64_t valid_arg)
{

	/* can't parse it - so return the worst case */
	if ((valid_arg & (ARG_CTLNAME | ARG_LEN)) != (ARG_CTLNAME | ARG_LEN))
		return (AUE_SYSCTL);

	switch (name[0]) {
	/* non-admin "lookups" treat them special */
	case KERN_OSTYPE:
	case KERN_OSRELEASE:
	case KERN_OSREV:
	case KERN_VERSION:
	case KERN_ARGMAX:
	case KERN_CLOCKRATE:
	case KERN_BOOTTIME:
	case KERN_POSIX1:
	case KERN_NGROUPS:
	case KERN_JOB_CONTROL:
	case KERN_SAVED_IDS:
	case KERN_OSRELDATE:
	case KERN_DUMMY:
		return (AUE_SYSCTL_NONADMIN);

	/* only treat the changeable controls as admin */
	case KERN_MAXVNODES:
	case KERN_MAXPROC:
	case KERN_MAXFILES:
	case KERN_MAXPROCPERUID:
	case KERN_MAXFILESPERPROC:
	case KERN_HOSTID:
	case KERN_SECURELVL:
	case KERN_HOSTNAME:
	case KERN_VNODE:
	case KERN_PROC:
	case KERN_FILE:
	case KERN_PROF:
	case KERN_NISDOMAINNAME:
	case KERN_UPDATEINTERVAL:
	case KERN_NTP_PLL:
	case KERN_BOOTFILE:
	case KERN_DUMPDEV:
	case KERN_IPC:
	case KERN_PS_STRINGS:
	case KERN_USRSTACK:
	case KERN_LOGSIGEXIT:
	case KERN_IOV_MAX:
		return ((valid_arg & ARG_VALUE) ?
		    AUE_SYSCTL : AUE_SYSCTL_NONADMIN);

	default:
		return (AUE_SYSCTL);
	}
	/* NOTREACHED */
}

/*
 * Convert an open flags specifier into a specific type of open event for
 * auditing purposes.
 */
au_event_t
audit_flags_and_error_to_openevent(int oflags, int error)
{
	int i;

	/*
	 * Need to check only those flags we care about.
	 */
	oflags = oflags & (O_RDONLY | O_CREAT | O_TRUNC | O_RDWR | O_WRONLY);
	for (i = 0; i < nitems(aue_open); i++) {
		if (aue_open[i].aoe_flags == oflags)
			return (aue_open[i].aoe_event);
	}
	return (AUE_OPEN);
}

au_event_t
audit_flags_and_error_to_openatevent(int oflags, int error)
{
	int i;

	/*
	 * Need to check only those flags we care about.
	 */
	oflags = oflags & (O_RDONLY | O_CREAT | O_TRUNC | O_RDWR | O_WRONLY);
	for (i = 0; i < nitems(aue_openat); i++) {
		if (aue_openat[i].aoe_flags == oflags)
			return (aue_openat[i].aoe_event);
	}
	return (AUE_OPENAT);
}

/*
 * Convert a MSGCTL command to a specific event.
 */
au_event_t
audit_msgctl_to_event(int cmd)
{

	switch (cmd) {
	case IPC_RMID:
		return (AUE_MSGCTL_RMID);

	case IPC_SET:
		return (AUE_MSGCTL_SET);

	case IPC_STAT:
		return (AUE_MSGCTL_STAT);

	default:
		/* We will audit a bad command. */
		return (AUE_MSGCTL);
	}
}

/*
 * Convert a SEMCTL command to a specific event.
 */
au_event_t
audit_semctl_to_event(int cmd)
{

	switch (cmd) {
	case GETALL:
		return (AUE_SEMCTL_GETALL);

	case GETNCNT:
		return (AUE_SEMCTL_GETNCNT);

	case GETPID:
		return (AUE_SEMCTL_GETPID);

	case GETVAL:
		return (AUE_SEMCTL_GETVAL);

	case GETZCNT:
		return (AUE_SEMCTL_GETZCNT);

	case IPC_RMID:
		return (AUE_SEMCTL_RMID);

	case IPC_SET:
		return (AUE_SEMCTL_SET);

	case SETALL:
		return (AUE_SEMCTL_SETALL);

	case SETVAL:
		return (AUE_SEMCTL_SETVAL);

	case IPC_STAT:
		return (AUE_SEMCTL_STAT);

	default:
		/* We will audit a bad command. */
		return (AUE_SEMCTL);
	}
}

/*
 * Convert msgsys(2), semsys(2), and shmsys(2) system-call variations into
 * audit events, if possible.
 */
au_event_t
audit_msgsys_to_event(int which)
{

	if ((which >= 0) && (which < aue_msgsys_count))
		return (aue_msgsys[which]);

	/* Audit a bad command. */
	return (AUE_MSGSYS);
}

au_event_t
audit_semsys_to_event(int which)
{

	if ((which >= 0) && (which < aue_semsys_count))
		return (aue_semsys[which]);

	/* Audit a bad command. */
	return (AUE_SEMSYS);
}

au_event_t
audit_shmsys_to_event(int which)
{

	if ((which >= 0) && (which < aue_shmsys_count))
		return (aue_shmsys[which]);

	/* Audit a bad command. */
	return (AUE_SHMSYS);
}

/*
 * Convert a command for the auditon() system call to a audit event.
 */
au_event_t
auditon_command_event(int cmd)
{

	switch(cmd) {
	case A_GETPOLICY:
		return (AUE_AUDITON_GPOLICY);

	case A_SETPOLICY:
		return (AUE_AUDITON_SPOLICY);

	case A_GETKMASK:
		return (AUE_AUDITON_GETKMASK);

	case A_SETKMASK:
		return (AUE_AUDITON_SETKMASK);

	case A_GETQCTRL:
		return (AUE_AUDITON_GQCTRL);

	case A_SETQCTRL:
		return (AUE_AUDITON_SQCTRL);

	case A_GETCWD:
		return (AUE_AUDITON_GETCWD);

	case A_GETCAR:
		return (AUE_AUDITON_GETCAR);

	case A_GETSTAT:
		return (AUE_AUDITON_GETSTAT);

	case A_SETSTAT:
		return (AUE_AUDITON_SETSTAT);

	case A_SETUMASK:
		return (AUE_AUDITON_SETUMASK);

	case A_SETSMASK:
		return (AUE_AUDITON_SETSMASK);

	case A_GETCOND:
		return (AUE_AUDITON_GETCOND);

	case A_SETCOND:
		return (AUE_AUDITON_SETCOND);

	case A_GETCLASS:
		return (AUE_AUDITON_GETCLASS);

	case A_SETCLASS:
		return (AUE_AUDITON_SETCLASS);

	case A_GETPINFO:
	case A_SETPMASK:
	case A_SETFSIZE:
	case A_GETFSIZE:
	case A_GETPINFO_ADDR:
	case A_GETKAUDIT:
	case A_SETKAUDIT:
	default:
		return (AUE_AUDITON);	/* No special record */
	}
}

/*
 * Create a canonical path from given path by prefixing either the root
 * directory, or the current working directory.  If the process working
 * directory is NULL, we could use 'rootvnode' to obtain the root directory,
 * but this results in a volfs name written to the audit log. So we will
 * leave the filename starting with '/' in the audit log in this case.
 */
void
audit_canon_path(struct thread *td, int dirfd, char *path, char *cpath)
{
	struct vnode *cvnp, *rvnp;
	char *rbuf, *fbuf, *copy;
	struct filedesc *fdp;
	struct sbuf sbf;
	cap_rights_t rights;
	int error, needslash;

	WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL, "%s: at %s:%d",
	    __func__,  __FILE__, __LINE__);

	copy = path;
	rvnp = cvnp = NULL;
	fdp = td->td_proc->p_fd;
	FILEDESC_SLOCK(fdp);
	/*
	 * Make sure that we handle the chroot(2) case.  If there is an
	 * alternate root directory, prepend it to the audited pathname.
	 */
	if (fdp->fd_rdir != NULL && fdp->fd_rdir != rootvnode) {
		rvnp = fdp->fd_rdir;
		vhold(rvnp);
	}
	/*
	 * If the supplied path is relative, make sure we capture the current
	 * working directory so we can prepend it to the supplied relative
	 * path.
	 */
	if (*path != '/') {
		if (dirfd == AT_FDCWD) {
			cvnp = fdp->fd_cdir;
			vhold(cvnp);
		} else {
			/* XXX: fgetvp() that vhold()s vnode instead of vref()ing it would be better */
			error = fgetvp(td, dirfd, cap_rights_init(&rights), &cvnp);
			if (error) {
				FILEDESC_SUNLOCK(fdp);
				cpath[0] = '\0';
				if (rvnp != NULL)
					vdrop(rvnp);
				return;
			}
			vhold(cvnp);
			vrele(cvnp);
		}
		needslash = (fdp->fd_rdir != cvnp);
	} else {
		needslash = 1;
	}
	FILEDESC_SUNLOCK(fdp);
	/*
	 * NB: We require that the supplied array be at least MAXPATHLEN bytes
	 * long.  If this is not the case, then we can run into serious trouble.
	 */
	(void) sbuf_new(&sbf, cpath, MAXPATHLEN, SBUF_FIXEDLEN);
	/*
	 * Strip leading forward slashes.
	 */
	while (*copy == '/')
		copy++;
	/*
	 * Make sure we handle chroot(2) and prepend the global path to these
	 * environments.
	 *
	 * NB: vn_fullpath(9) on FreeBSD is less reliable than vn_getpath(9)
	 * on Darwin.  As a result, this may need some additional attention
	 * in the future.
	 */
	if (rvnp != NULL) {
		error = vn_fullpath_global(td, rvnp, &rbuf, &fbuf);
		vdrop(rvnp);
		if (error) {
			cpath[0] = '\0';
			if (cvnp != NULL)
				vdrop(cvnp);
			return;
		}
		(void) sbuf_cat(&sbf, rbuf);
		free(fbuf, M_TEMP);
	}
	if (cvnp != NULL) {
		error = vn_fullpath(td, cvnp, &rbuf, &fbuf);
		vdrop(cvnp);
		if (error) {
			cpath[0] = '\0';
			return;
		}
		(void) sbuf_cat(&sbf, rbuf);
		free(fbuf, M_TEMP);
	}
	if (needslash)
		(void) sbuf_putc(&sbf, '/');
	/*
	 * Now that we have processed any alternate root and relative path
	 * names, add the supplied pathname.
	 */
        (void) sbuf_cat(&sbf, copy);
	/*
	 * One or more of the previous sbuf operations could have resulted in
	 * the supplied buffer being overflowed.  Check to see if this is the
	 * case.
	 */
	if (sbuf_error(&sbf) != 0) {
		cpath[0] = '\0';
		return;
	}
	sbuf_finish(&sbf);
}
