/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1999-2005 Apple Inc.
 * Copyright (c) 2016-2017 Robert N. M. Watson
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
#include <sys/filedesc.h>
#include <sys/capsicum.h>
#include <sys/ipc.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/protosw.h>
#include <sys/domain.h>
#include <sys/sbuf.h>
#include <sys/systm.h>
#include <sys/un.h>
#include <sys/vnode.h>

#include <netinet/in.h>
#include <netinet/in_pcb.h>

#include <security/audit/audit.h>
#include <security/audit/audit_private.h>

/*
 * Calls to manipulate elements of the audit record structure from system
 * call code.  Macro wrappers will prevent this functions from being entered
 * if auditing is disabled, avoiding the function call cost.  We check the
 * thread audit record pointer anyway, as the audit condition could change,
 * and pre-selection may not have allocated an audit record for this event.
 *
 * XXXAUDIT: Should we assert, in each case, that this field of the record
 * hasn't already been filled in?
 */
void
audit_arg_addr(void *addr)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_addr = addr;
	ARG_SET_VALID(ar, ARG_ADDR);
}

void
audit_arg_exit(int status, int retval)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_exitstatus = status;
	ar->k_ar.ar_arg_exitretval = retval;
	ARG_SET_VALID(ar, ARG_EXIT);
}

void
audit_arg_len(int len)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_len = len;
	ARG_SET_VALID(ar, ARG_LEN);
}

void
audit_arg_atfd1(int atfd)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_atfd1 = atfd;
	ARG_SET_VALID(ar, ARG_ATFD1);
}

void
audit_arg_atfd2(int atfd)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_atfd2 = atfd;
	ARG_SET_VALID(ar, ARG_ATFD2);
}

void
audit_arg_fd(int fd)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_fd = fd;
	ARG_SET_VALID(ar, ARG_FD);
}

void
audit_arg_fflags(int fflags)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_fflags = fflags;
	ARG_SET_VALID(ar, ARG_FFLAGS);
}

void
audit_arg_gid(gid_t gid)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_gid = gid;
	ARG_SET_VALID(ar, ARG_GID);
}

void
audit_arg_uid(uid_t uid)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_uid = uid;
	ARG_SET_VALID(ar, ARG_UID);
}

void
audit_arg_egid(gid_t egid)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_egid = egid;
	ARG_SET_VALID(ar, ARG_EGID);
}

void
audit_arg_euid(uid_t euid)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_euid = euid;
	ARG_SET_VALID(ar, ARG_EUID);
}

void
audit_arg_rgid(gid_t rgid)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_rgid = rgid;
	ARG_SET_VALID(ar, ARG_RGID);
}

void
audit_arg_ruid(uid_t ruid)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_ruid = ruid;
	ARG_SET_VALID(ar, ARG_RUID);
}

void
audit_arg_sgid(gid_t sgid)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_sgid = sgid;
	ARG_SET_VALID(ar, ARG_SGID);
}

void
audit_arg_suid(uid_t suid)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_suid = suid;
	ARG_SET_VALID(ar, ARG_SUID);
}

void
audit_arg_groupset(gid_t *gidset, u_int gidset_size)
{
	u_int i;
	struct kaudit_record *ar;

	KASSERT(gidset_size <= ngroups_max + 1,
	    ("audit_arg_groupset: gidset_size > (kern.ngroups + 1)"));

	ar = currecord();
	if (ar == NULL)
		return;

	if (ar->k_ar.ar_arg_groups.gidset == NULL)
		ar->k_ar.ar_arg_groups.gidset = malloc(
		    sizeof(gid_t) * gidset_size, M_AUDITGIDSET, M_WAITOK);

	for (i = 0; i < gidset_size; i++)
		ar->k_ar.ar_arg_groups.gidset[i] = gidset[i];
	ar->k_ar.ar_arg_groups.gidset_size = gidset_size;
	ARG_SET_VALID(ar, ARG_GROUPSET);
}

void
audit_arg_login(char *login)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	strlcpy(ar->k_ar.ar_arg_login, login, MAXLOGNAME);
	ARG_SET_VALID(ar, ARG_LOGIN);
}

void
audit_arg_ctlname(int *name, int namelen)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	bcopy(name, &ar->k_ar.ar_arg_ctlname, namelen * sizeof(int));
	ar->k_ar.ar_arg_len = namelen;
	ARG_SET_VALID(ar, ARG_CTLNAME | ARG_LEN);
}

void
audit_arg_mask(int mask)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_mask = mask;
	ARG_SET_VALID(ar, ARG_MASK);
}

void
audit_arg_mode(mode_t mode)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_mode = mode;
	ARG_SET_VALID(ar, ARG_MODE);
}

void
audit_arg_dev(int dev)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_dev = dev;
	ARG_SET_VALID(ar, ARG_DEV);
}

void
audit_arg_value(long value)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_value = value;
	ARG_SET_VALID(ar, ARG_VALUE);
}

void
audit_arg_owner(uid_t uid, gid_t gid)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_uid = uid;
	ar->k_ar.ar_arg_gid = gid;
	ARG_SET_VALID(ar, ARG_UID | ARG_GID);
}

void
audit_arg_pid(pid_t pid)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_pid = pid;
	ARG_SET_VALID(ar, ARG_PID);
}

void
audit_arg_process(struct proc *p)
{
	struct kaudit_record *ar;
	struct ucred *cred;

	KASSERT(p != NULL, ("audit_arg_process: p == NULL"));

	PROC_LOCK_ASSERT(p, MA_OWNED);

	ar = currecord();
	if (ar == NULL)
		return;

	cred = p->p_ucred;
	ar->k_ar.ar_arg_auid = cred->cr_audit.ai_auid;
	ar->k_ar.ar_arg_euid = cred->cr_uid;
	ar->k_ar.ar_arg_egid = cred->cr_groups[0];
	ar->k_ar.ar_arg_ruid = cred->cr_ruid;
	ar->k_ar.ar_arg_rgid = cred->cr_rgid;
	ar->k_ar.ar_arg_asid = cred->cr_audit.ai_asid;
	ar->k_ar.ar_arg_termid_addr = cred->cr_audit.ai_termid;
	ar->k_ar.ar_arg_pid = p->p_pid;
	ARG_SET_VALID(ar, ARG_AUID | ARG_EUID | ARG_EGID | ARG_RUID |
	    ARG_RGID | ARG_ASID | ARG_TERMID_ADDR | ARG_PID | ARG_PROCESS);
}

void
audit_arg_signum(u_int signum)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_signum = signum;
	ARG_SET_VALID(ar, ARG_SIGNUM);
}

void
audit_arg_socket(int sodomain, int sotype, int soprotocol)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_sockinfo.so_domain = sodomain;
	ar->k_ar.ar_arg_sockinfo.so_type = sotype;
	ar->k_ar.ar_arg_sockinfo.so_protocol = soprotocol;
	ARG_SET_VALID(ar, ARG_SOCKINFO);
}

void
audit_arg_sockaddr(struct thread *td, int dirfd, struct sockaddr *sa)
{
	struct kaudit_record *ar;

	KASSERT(td != NULL, ("audit_arg_sockaddr: td == NULL"));
	KASSERT(sa != NULL, ("audit_arg_sockaddr: sa == NULL"));

	ar = currecord();
	if (ar == NULL)
		return;

	bcopy(sa, &ar->k_ar.ar_arg_sockaddr, sa->sa_len);
	switch (sa->sa_family) {
	case AF_INET:
		ARG_SET_VALID(ar, ARG_SADDRINET);
		break;

	case AF_INET6:
		ARG_SET_VALID(ar, ARG_SADDRINET6);
		break;

	case AF_UNIX:
		if (dirfd != AT_FDCWD)
			audit_arg_atfd1(dirfd);
		audit_arg_upath1(td, dirfd,
		    ((struct sockaddr_un *)sa)->sun_path);
		ARG_SET_VALID(ar, ARG_SADDRUNIX);
		break;
	/* XXXAUDIT: default:? */
	}
}

void
audit_arg_auid(uid_t auid)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_auid = auid;
	ARG_SET_VALID(ar, ARG_AUID);
}

void
audit_arg_auditinfo(struct auditinfo *au_info)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_auid = au_info->ai_auid;
	ar->k_ar.ar_arg_asid = au_info->ai_asid;
	ar->k_ar.ar_arg_amask.am_success = au_info->ai_mask.am_success;
	ar->k_ar.ar_arg_amask.am_failure = au_info->ai_mask.am_failure;
	ar->k_ar.ar_arg_termid.port = au_info->ai_termid.port;
	ar->k_ar.ar_arg_termid.machine = au_info->ai_termid.machine;
	ARG_SET_VALID(ar, ARG_AUID | ARG_ASID | ARG_AMASK | ARG_TERMID);
}

void
audit_arg_auditinfo_addr(struct auditinfo_addr *au_info)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_auid = au_info->ai_auid;
	ar->k_ar.ar_arg_asid = au_info->ai_asid;
	ar->k_ar.ar_arg_amask.am_success = au_info->ai_mask.am_success;
	ar->k_ar.ar_arg_amask.am_failure = au_info->ai_mask.am_failure;
	ar->k_ar.ar_arg_termid_addr.at_type = au_info->ai_termid.at_type;
	ar->k_ar.ar_arg_termid_addr.at_port = au_info->ai_termid.at_port;
	ar->k_ar.ar_arg_termid_addr.at_addr[0] = au_info->ai_termid.at_addr[0];
	ar->k_ar.ar_arg_termid_addr.at_addr[1] = au_info->ai_termid.at_addr[1];
	ar->k_ar.ar_arg_termid_addr.at_addr[2] = au_info->ai_termid.at_addr[2];
	ar->k_ar.ar_arg_termid_addr.at_addr[3] = au_info->ai_termid.at_addr[3];
	ARG_SET_VALID(ar, ARG_AUID | ARG_ASID | ARG_AMASK | ARG_TERMID_ADDR);
}

void
audit_arg_text(const char *text)
{
	struct kaudit_record *ar;

	KASSERT(text != NULL, ("audit_arg_text: text == NULL"));

	ar = currecord();
	if (ar == NULL)
		return;

	/* Invalidate the text string */
	ar->k_ar.ar_valid_arg &= (ARG_ALL ^ ARG_TEXT);

	if (ar->k_ar.ar_arg_text == NULL)
		ar->k_ar.ar_arg_text = malloc(MAXPATHLEN, M_AUDITTEXT,
		    M_WAITOK);

	strncpy(ar->k_ar.ar_arg_text, text, MAXPATHLEN);
	ARG_SET_VALID(ar, ARG_TEXT);
}

void
audit_arg_cmd(int cmd)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_cmd = cmd;
	ARG_SET_VALID(ar, ARG_CMD);
}

void
audit_arg_svipc_cmd(int cmd)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_svipc_cmd = cmd;
	ARG_SET_VALID(ar, ARG_SVIPC_CMD);
}

void
audit_arg_svipc_perm(struct ipc_perm *perm)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	bcopy(perm, &ar->k_ar.ar_arg_svipc_perm,
	    sizeof(ar->k_ar.ar_arg_svipc_perm));
	ARG_SET_VALID(ar, ARG_SVIPC_PERM);
}

void
audit_arg_svipc_id(int id)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_svipc_id = id;
	ARG_SET_VALID(ar, ARG_SVIPC_ID);
}

void
audit_arg_svipc_addr(void * addr)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_svipc_addr = addr;
	ARG_SET_VALID(ar, ARG_SVIPC_ADDR);
}

void
audit_arg_svipc_which(int which)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_svipc_which = which;
	ARG_SET_VALID(ar, ARG_SVIPC_WHICH);
}

void
audit_arg_posix_ipc_perm(uid_t uid, gid_t gid, mode_t mode)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_pipc_perm.pipc_uid = uid;
	ar->k_ar.ar_arg_pipc_perm.pipc_gid = gid;
	ar->k_ar.ar_arg_pipc_perm.pipc_mode = mode;
	ARG_SET_VALID(ar, ARG_POSIX_IPC_PERM);
}

void
audit_arg_auditon(union auditon_udata *udata)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	bcopy((void *)udata, &ar->k_ar.ar_arg_auditon,
	    sizeof(ar->k_ar.ar_arg_auditon));
	ARG_SET_VALID(ar, ARG_AUDITON);
}

/*
 * Audit information about a file, either the file's vnode info, or its
 * socket address info.
 */
void
audit_arg_file(struct proc *p, struct file *fp)
{
	struct kaudit_record *ar;
	struct socket *so;
	struct inpcb *pcb;
	struct vnode *vp;

	ar = currecord();
	if (ar == NULL)
		return;

	switch (fp->f_type) {
	case DTYPE_VNODE:
	case DTYPE_FIFO:
		/*
		 * XXXAUDIT: Only possibly to record as first vnode?
		 */
		vp = fp->f_vnode;
		vn_lock(vp, LK_SHARED | LK_RETRY);
		audit_arg_vnode1(vp);
		VOP_UNLOCK(vp, 0);
		break;

	case DTYPE_SOCKET:
		so = (struct socket *)fp->f_data;
		if (INP_CHECK_SOCKAF(so, PF_INET)) {
			SOCK_LOCK(so);
			ar->k_ar.ar_arg_sockinfo.so_type =
			    so->so_type;
			ar->k_ar.ar_arg_sockinfo.so_domain =
			    INP_SOCKAF(so);
			ar->k_ar.ar_arg_sockinfo.so_protocol =
			    so->so_proto->pr_protocol;
			SOCK_UNLOCK(so);
			pcb = (struct inpcb *)so->so_pcb;
			INP_RLOCK(pcb);
			ar->k_ar.ar_arg_sockinfo.so_raddr =
			    pcb->inp_faddr.s_addr;
			ar->k_ar.ar_arg_sockinfo.so_laddr =
			    pcb->inp_laddr.s_addr;
			ar->k_ar.ar_arg_sockinfo.so_rport =
			    pcb->inp_fport;
			ar->k_ar.ar_arg_sockinfo.so_lport =
			    pcb->inp_lport;
			INP_RUNLOCK(pcb);
			ARG_SET_VALID(ar, ARG_SOCKINFO);
		}
		break;

	default:
		/* XXXAUDIT: else? */
		break;
	}
}

/*
 * Store a path as given by the user process for auditing into the audit
 * record stored on the user thread.  This function will allocate the memory
 * to store the path info if not already available.  This memory will be
 * freed when the audit record is freed.  The path is canonlicalised with
 * respect to the thread and directory descriptor passed.
 */
static void
audit_arg_upath(struct thread *td, int dirfd, char *upath, char **pathp)
{

	if (*pathp == NULL)
		*pathp = malloc(MAXPATHLEN, M_AUDITPATH, M_WAITOK);
	audit_canon_path(td, dirfd, upath, *pathp);
}

void
audit_arg_upath1(struct thread *td, int dirfd, char *upath)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	audit_arg_upath(td, dirfd, upath, &ar->k_ar.ar_arg_upath1);
	ARG_SET_VALID(ar, ARG_UPATH1);
}

void
audit_arg_upath2(struct thread *td, int dirfd, char *upath)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	audit_arg_upath(td, dirfd, upath, &ar->k_ar.ar_arg_upath2);
	ARG_SET_VALID(ar, ARG_UPATH2);
}

/*
 * Variants on path auditing that do not canonicalise the path passed in;
 * these are for use with filesystem-like subsystems that employ string names,
 * but do not support a hierarchical namespace -- for example, POSIX IPC
 * objects.  The subsystem should have performed any necessary
 * canonicalisation required to make the paths useful to audit analysis.
 */
static void
audit_arg_upath_canon(char *upath, char **pathp)
{

	if (*pathp == NULL)
		*pathp = malloc(MAXPATHLEN, M_AUDITPATH, M_WAITOK);
	(void)snprintf(*pathp, MAXPATHLEN, "%s", upath);
}

void
audit_arg_upath1_canon(char *upath)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	audit_arg_upath_canon(upath, &ar->k_ar.ar_arg_upath1);
	ARG_SET_VALID(ar, ARG_UPATH1);
}

void
audit_arg_upath2_canon(char *upath)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	audit_arg_upath_canon(upath, &ar->k_ar.ar_arg_upath2);
	ARG_SET_VALID(ar, ARG_UPATH2);
}

/*
 * Function to save the path and vnode attr information into the audit
 * record.
 *
 * It is assumed that the caller will hold any vnode locks necessary to
 * perform a VOP_GETATTR() on the passed vnode.
 *
 * XXX: The attr code is very similar to vfs_vnops.c:vn_stat(), but always
 * provides access to the generation number as we need that to construct the
 * BSM file ID.
 *
 * XXX: We should accept the process argument from the caller, since it's
 * very likely they already have a reference.
 *
 * XXX: Error handling in this function is poor.
 *
 * XXXAUDIT: Possibly KASSERT the path pointer is NULL?
 */
static int
audit_arg_vnode(struct vnode *vp, struct vnode_au_info *vnp)
{
	struct vattr vattr;
	int error;

	ASSERT_VOP_LOCKED(vp, "audit_arg_vnode");

	error = VOP_GETATTR(vp, &vattr, curthread->td_ucred);
	if (error) {
		/* XXX: How to handle this case? */
		return (error);
	}

	vnp->vn_mode = vattr.va_mode;
	vnp->vn_uid = vattr.va_uid;
	vnp->vn_gid = vattr.va_gid;
	vnp->vn_dev = vattr.va_rdev;
	vnp->vn_fsid = vattr.va_fsid;
	vnp->vn_fileid = vattr.va_fileid;
	vnp->vn_gen = vattr.va_gen;
	return (0);
}

void
audit_arg_vnode1(struct vnode *vp)
{
	struct kaudit_record *ar;
	int error;

	ar = currecord();
	if (ar == NULL)
		return;

	ARG_CLEAR_VALID(ar, ARG_VNODE1);
	error = audit_arg_vnode(vp, &ar->k_ar.ar_arg_vnode1);
	if (error == 0)
		ARG_SET_VALID(ar, ARG_VNODE1);
}

void
audit_arg_vnode2(struct vnode *vp)
{
	struct kaudit_record *ar;
	int error;

	ar = currecord();
	if (ar == NULL)
		return;

	ARG_CLEAR_VALID(ar, ARG_VNODE2);
	error = audit_arg_vnode(vp, &ar->k_ar.ar_arg_vnode2);
	if (error == 0)
		ARG_SET_VALID(ar, ARG_VNODE2);
}

/*
 * Audit the argument strings passed to exec.
 */
void
audit_arg_argv(char *argv, int argc, int length)
{
	struct kaudit_record *ar;

	if (audit_argv == 0)
		return;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_argv = malloc(length, M_AUDITTEXT, M_WAITOK);
	bcopy(argv, ar->k_ar.ar_arg_argv, length);
	ar->k_ar.ar_arg_argc = argc;
	ARG_SET_VALID(ar, ARG_ARGV);
}

/*
 * Audit the environment strings passed to exec.
 */
void
audit_arg_envv(char *envv, int envc, int length)
{
	struct kaudit_record *ar;

	if (audit_arge == 0)
		return;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_envv = malloc(length, M_AUDITTEXT, M_WAITOK);
	bcopy(envv, ar->k_ar.ar_arg_envv, length);
	ar->k_ar.ar_arg_envc = envc;
	ARG_SET_VALID(ar, ARG_ENVV);
}

void
audit_arg_rights(cap_rights_t *rightsp)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_rights = *rightsp;
	ARG_SET_VALID(ar, ARG_RIGHTS);
}

void
audit_arg_fcntl_rights(uint32_t fcntlrights)
{
	struct kaudit_record *ar;

	ar = currecord();
	if (ar == NULL)
		return;

	ar->k_ar.ar_arg_fcntl_rights = fcntlrights;
	ARG_SET_VALID(ar, ARG_FCNTL_RIGHTS);
}

/*
 * The close() system call uses it's own audit call to capture the path/vnode
 * information because those pieces are not easily obtained within the system
 * call itself.
 */
void
audit_sysclose(struct thread *td, int fd)
{
	cap_rights_t rights;
	struct kaudit_record *ar;
	struct vnode *vp;
	struct file *fp;

	KASSERT(td != NULL, ("audit_sysclose: td == NULL"));

	ar = currecord();
	if (ar == NULL)
		return;

	audit_arg_fd(fd);

	if (getvnode(td, fd, cap_rights_init(&rights), &fp) != 0)
		return;

	vp = fp->f_vnode;
	vn_lock(vp, LK_SHARED | LK_RETRY);
	audit_arg_vnode1(vp);
	VOP_UNLOCK(vp, 0);
	fdrop(fp, td);
}
