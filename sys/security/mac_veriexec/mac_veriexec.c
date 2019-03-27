/*
 * $FreeBSD$
 *
 * Copyright (c) 2011, 2012, 2013, 2015, 2016, Juniper Networks, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>

#include "opt_capsicum.h"
#include "opt_mac.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/capsicum.h>
#include <sys/eventhandler.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/imgact.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/mac.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>
#include <fs/nullfs/null.h>
#include <security/mac/mac_policy.h>

#include "mac_veriexec.h"
#include "mac_veriexec_internal.h"

#define	SLOT(l) \
	mac_label_get((l), mac_veriexec_slot)
#define	SLOT_SET(l, v) \
	mac_label_set((l), mac_veriexec_slot, (v))

#ifdef MAC_DEBUG
#define	MAC_VERIEXEC_DBG(_lvl, _fmt, ...)				\
	do {								\
		VERIEXEC_DEBUG((_lvl), (MAC_VERIEXEC_FULLNAME ": " _fmt	\
		     "\n", ##__VA_ARGS__));				\
	} while(0)
#else
#define	MAC_VERIEXEC_DBG(_lvl, _fmt, ...)
#endif

static int sysctl_mac_veriexec_state(SYSCTL_HANDLER_ARGS);
static int sysctl_mac_veriexec_db(SYSCTL_HANDLER_ARGS);

SYSCTL_DECL(_security_mac);

SYSCTL_NODE(_security_mac, OID_AUTO, veriexec, CTLFLAG_RW, 0,
    "MAC/veriexec policy controls");

int	mac_veriexec_debug;
SYSCTL_INT(_security_mac_veriexec, OID_AUTO, debug, CTLFLAG_RW,
    &mac_veriexec_debug, 0, "Debug level");

static int	mac_veriexec_state;
SYSCTL_PROC(_security_mac_veriexec, OID_AUTO, state,
    CTLTYPE_STRING | CTLFLAG_RD, 0, 0, sysctl_mac_veriexec_state, "A",
    "Verified execution subsystem state");

SYSCTL_PROC(_security_mac_veriexec, OID_AUTO, db,
    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_SKIP, 0, 0, sysctl_mac_veriexec_db,
    "A", "Verified execution fingerprint database");

static int mac_veriexec_slot;

MALLOC_DEFINE(M_VERIEXEC, "veriexec", "Verified execution data");

/**
 * @internal
 * @brief Handler for security.mac.veriexec.db sysctl
 *
 * Display a human-readable form of the current fingerprint database.
 */
static int
sysctl_mac_veriexec_db(SYSCTL_HANDLER_ARGS)
{
	struct sbuf sb;
	int error;

	error = sysctl_wire_old_buffer(req, 0);
	if (error != 0)
		return (error);

	sbuf_new_for_sysctl(&sb, NULL, 1024, req);
	mac_veriexec_metadata_print_db(&sb);
	error = sbuf_finish(&sb);
	sbuf_delete(&sb);

	return (error);
}

/**
 * @internal
 * @brief Generate human-readable output about the current verified execution
 *        state.
 *
 * @param sbp		sbuf to write output to
 */
static void
mac_veriexec_print_state(struct sbuf *sbp)
{

	if (mac_veriexec_state & VERIEXEC_STATE_INACTIVE)
		sbuf_printf(sbp, "inactive ");
	if (mac_veriexec_state & VERIEXEC_STATE_LOADED)
		sbuf_printf(sbp, "loaded ");
	if (mac_veriexec_state & VERIEXEC_STATE_ACTIVE)
		sbuf_printf(sbp, "active ");
	if (mac_veriexec_state & VERIEXEC_STATE_ENFORCE)
		sbuf_printf(sbp, "enforce ");
	if (mac_veriexec_state & VERIEXEC_STATE_LOCKED)
		sbuf_printf(sbp, "locked ");
	if (mac_veriexec_state != 0)
		sbuf_trim(sbp);
}

/**
 * @internal
 * @brief Handler for security.mac.veriexec.state sysctl
 *
 * Display a human-readable form of the current verified execution subsystem
 * state.
 */
static int
sysctl_mac_veriexec_state(SYSCTL_HANDLER_ARGS)
{
	struct sbuf sb;
	int error;

	sbuf_new(&sb, NULL, 128, SBUF_AUTOEXTEND);
	mac_veriexec_print_state(&sb);
	sbuf_finish(&sb);

	error = SYSCTL_OUT(req, sbuf_data(&sb), sbuf_len(&sb));
	sbuf_delete(&sb);
	return (error);
}

/**
 * @internal
 * @brief Event handler called when a virtual file system is mounted.
 *
 * We need to record the file system identifier in the MAC per-policy slot
 * assigned to veriexec, so we have a key to use in order to reference the
 * mount point in the meta-data store.
 *
 * @param arg		unused argument
 * @param mp		mount point that is being mounted
 * @param fsrootvp	vnode of the file system root
 * @param td		calling thread
 */
static void
mac_veriexec_vfs_mounted(void *arg __unused, struct mount *mp,
    struct vnode *fsrootvp, struct thread *td)
{
	struct vattr va;
	int error;

	error = VOP_GETATTR(fsrootvp, &va, td->td_ucred);
	if (error)
		return;

	SLOT_SET(mp->mnt_label, va.va_fsid);
#ifdef MAC_DEBUG
	MAC_VERIEXEC_DBG(3, "set fsid to %u for mount %p", va.va_fsid, mp);
#endif
}

/**
 * @internal
 * @brief Event handler called when a virtual file system is unmounted.
 *
 * If we recorded a file system identifier in the MAC per-policy slot assigned
 * to veriexec, then we need to tell the meta-data store to clean up.
 *
 * @param arg		unused argument
 * @param mp		mount point that is being unmounted
 * @param td		calling thread
 */
static void
mac_veriexec_vfs_unmounted(void *arg __unused, struct mount *mp,
    struct thread *td)
{
	dev_t fsid;

	fsid = SLOT(mp->mnt_label);
	if (fsid) {
		MAC_VERIEXEC_DBG(3, "fsid %u, cleaning up mount", fsid);
		mac_veriexec_metadata_unmounted(fsid, td);
	}
}

/**
 * @internal
 * @brief The mount point is being initialized, set the value in the MAC
 *     per-policy slot for veriexec to zero.
 *
 * @note A value of zero in this slot indicates no file system identifier
 *     is assigned.
 *
 * @param label the label that is being initialized
 */
static void 
mac_veriexec_mount_init_label(struct label *label) 
{

	SLOT_SET(label, 0);
}

/**
 * @internal
 * @brief The mount-point is being destroyed, reset the value in the MAC
 *     per-policy slot for veriexec back to zero.
 *
 * @note A value of zero in this slot indicates no file system identifier
 *     is assigned.
 *
 * @param label the label that is being destroyed
 */
static void 
mac_veriexec_mount_destroy_label(struct label *label) 
{

	SLOT_SET(label, 0);
}

/**
 * @internal
 * @brief The vnode label is being initialized, set the value in the MAC
 *     per-policy slot for veriexec to @c FINGERPRINT_INVALID
 *
 * @note @c FINGERPRINT_INVALID indicates the fingerprint is invalid.
 *
 * @param label		the label that is being initialized
 */
static void
mac_veriexec_vnode_init_label(struct label *label)
{

	SLOT_SET(label, FINGERPRINT_INVALID);
}

/**
 * @internal
 * @brief The vnode label is being destroyed, reset the value in the MAC
 *        per-policy slot for veriexec back to @c FINGERPRINT_INVALID
 *
 * @note @c FINGERPRINT_INVALID indicates the fingerprint is invalid.
 *
 * @param label		the label that is being destroyed
 */
static void
mac_veriexec_vnode_destroy_label(struct label *label)
{

	SLOT_SET(label, FINGERPRINT_INVALID);
}

/**
 * @internal
 * @brief Copy the value in the MAC per-policy slot assigned to veriexec from
 *        the @p src label to the @p dest label
 */
static void 
mac_veriexec_copy_label(struct label *src, struct label *dest)
{

	SLOT_SET(dest, SLOT(src));
}

/**
 * @internal
 * @brief Check if the requested process can be debugged
 *
 * @param cred		credentials to use
 * @param p		process to debug
 *
 * @return 0 if debugging is allowed, otherwise an error code.
 */
static int
mac_veriexec_proc_check_debug(struct ucred *cred, struct proc *p)
{
	int error, flags;

	/* If we are not enforcing veriexec, nothing for us to check */
	if ((mac_veriexec_state & VERIEXEC_STATE_ENFORCE) == 0)
		return (0);

	error = mac_veriexec_metadata_get_executable_flags(cred, p, &flags, 0);
	if (error != 0)
		return (0);

	return ((flags & VERIEXEC_NOTRACE) ? EACCES : 0);
}

/**
 * @internal
 * @brief A KLD load has been requested and needs to be validated.
 *
 * @param cred		credentials to use
 * @param vp		vnode of the KLD that has been requested
 * @param vlabel	vnode label assigned to the vnode
 *
 * @return 0 if the KLD load is allowed, otherwise an error code.
 */
static int
mac_veriexec_kld_check_load(struct ucred *cred, struct vnode *vp,
    struct label *vlabel)
{
	struct vattr va;
	struct thread *td = curthread;
	fingerprint_status_t status;
	int error;

	/*
	 * If we are not actively enforcing, allow it
	 */
	if ((mac_veriexec_state & VERIEXEC_STATE_ENFORCE) == 0)
		return (0);

	/* Get vnode attributes */
	error = VOP_GETATTR(vp, &va, cred);
	if (error)
		return (error);

	/*
	 * Fetch the fingerprint status for the vnode
	 * (starting with files first)
	 */
	error = mac_veriexec_metadata_fetch_fingerprint_status(vp, &va, td,
	    VERIEXEC_FILES_FIRST);
	if (error && error != EAUTH)
		return (error);

	/*
	 * By now we should have status...
	 */
	status = mac_veriexec_get_fingerprint_status(vp);
	switch (status) {
	case FINGERPRINT_FILE:
	case FINGERPRINT_VALID:
	case FINGERPRINT_INDIRECT:
		if (error)
			return (error);
		break;
	default:
		/*
		 * kldload should fail unless there is a valid fingerprint
		 * registered.
		 */
		MAC_VERIEXEC_DBG(2, "fingerprint status is %d for dev %u, "
			"file %lu.%lu\n", status, va.va_fsid, va.va_fileid,
			 va.va_gen);
		return (EAUTH);
	}

	/* Everything is good, allow the KLD to be loaded */
	return (0);
}

/**
 * @internal
 * @brief Check privileges that veriexec needs to be concerned about.
 *
 * The following privileges are checked by this function:
 *  - PRIV_KMEM_WRITE\n
 *    Check if writes to /dev/mem and /dev/kmem are allowed\n
 *    (Only trusted processes are allowed)
 *
 * @param cred		credentials to use
 * @param priv		privilege to check
 *
 * @return 0 if the privilege is allowed, error code otherwise.
 */
static int
mac_veriexec_priv_check(struct ucred *cred, int priv)
{

	/* If we are not enforcing veriexec, nothing for us to check */
	if ((mac_veriexec_state & VERIEXEC_STATE_ENFORCE) == 0)
		return (0);

	switch (priv) {
	case PRIV_KMEM_WRITE:
		if (!mac_veriexec_proc_is_trusted(cred, curproc))
			return (EPERM);
		break;
	default:
		break;
	}
	return (0);
}

/**
 * @internal
 * @brief A program is being executed and needs to be validated.
 *
 * @param cred		credentials to use
 * @param vp		vnode of the program that is being executed
 * @param label		vnode label assigned to the vnode
 * @param imgp		parameters for the image to be executed
 * @param execlabel	optional exec label
 *
 * @return 0 if the program should be allowed to execute, otherwise an error
 *     code.
 */
static int
mac_veriexec_vnode_check_exec(struct ucred *cred __unused,
    struct vnode *vp __unused, struct label *label __unused,
    struct image_params *imgp, struct label *execlabel __unused)
{
	struct thread *td = curthread;
	int error;

	error = mac_veriexec_fingerprint_check_image(imgp, 0, td);
	return (error);
}

/**
 * @brief Check fingerprint for the specified vnode and validate it
 *
 * @param cred		credentials to use
 * @param vp		vnode of the file
 * @param accmode	access mode to check (read, write, append, create,
 *			verify, etc.)
 *
 * @return 0 if the file validated, otherwise an error code.
 */
static int
mac_veriexec_check_vp(struct ucred *cred, struct vnode *vp, accmode_t accmode)
{
	struct vattr va;
	struct thread *td = curthread;
	fingerprint_status_t status;
	int error;

	/* Get vnode attributes */
	error = VOP_GETATTR(vp, &va, cred);
	if (error)
		return (error);

	/* Get the fingerprint status for the file */
	error = mac_veriexec_metadata_fetch_fingerprint_status(vp, &va, td,
	    VERIEXEC_FILES_FIRST);
	if (error && error != EAUTH)
		return (error);

	/*
	 * By now we should have status...
	 */
	status = mac_veriexec_get_fingerprint_status(vp);
	if (accmode & VWRITE) {
		/*
		 * If file has a fingerprint then deny the write request,
		 * otherwise invalidate the status so we don't keep checking
		 * for the file having a fingerprint. 
		 */
		switch (status) {
		case FINGERPRINT_FILE:
		case FINGERPRINT_VALID:
		case FINGERPRINT_INDIRECT:
			MAC_VERIEXEC_DBG(2,
			    "attempted write to fingerprinted file for dev "
			    "%u, file %lu.%lu\n", va.va_fsid,
			    va.va_fileid, va.va_gen);
			return (EPERM);
		default:
			break;
		}
	}
	if (accmode & VVERIFY) {
		switch (status) {
		case FINGERPRINT_FILE:
		case FINGERPRINT_VALID:
		case FINGERPRINT_INDIRECT:
			if (error)
				return (error);
			break;
		default:
			/*
			 * Caller wants open to fail unless there is a valid
			 * fingerprint registered. 
			 */
			MAC_VERIEXEC_DBG(2, "fingerprint status is %d for dev "
			    "%u, file %lu.%lu\n", status, va.va_fsid,
			    va.va_fileid, va.va_gen);
			return (EAUTH);
		}
	}
	return (0);
}

/**
 * @brief Opening a file has been requested and may need to be validated.
 *
 * @param cred		credentials to use
 * @param vp		vnode of the file to open
 * @param label		vnode label assigned to the vnode
 * @param accmode	access mode to use for opening the file (read, write,
 * 			append, create, verify, etc.)
 *
 * @return 0 if opening the file should be allowed, otherwise an error code.
 */
static int
mac_veriexec_vnode_check_open(struct ucred *cred, struct vnode *vp,
	struct label *label __unused, accmode_t accmode)
{
	int error;

	/*
	 * Look for the file on the fingerprint lists iff it has not been seen
	 * before.
	 */
	if ((mac_veriexec_state & VERIEXEC_STATE_ENFORCE) == 0)
		return (0);

	error = mac_veriexec_check_vp(cred, vp, accmode);
	return (error);
}

/**
 * @brief Check mode changes on file to ensure they should be allowed.
 *
 * We cannot allow chmod of SUID or SGID on verified files.
 *
 * @param cred		credentials to use
 * @param vp		vnode of the file to open
 * @param label		vnode label assigned to the vnode
 * @param mode		mode flags to set
 *
 * @return 0 if the mode change should be allowed, EAUTH otherwise.
 */
static int
mac_veriexec_vnode_check_setmode(struct ucred *cred, struct vnode *vp,
    struct label *label __unused, mode_t mode)
{
	int error;

	if ((mac_veriexec_state & VERIEXEC_STATE_ENFORCE) == 0)
		return (0);

	/*
	 * Do not allow chmod (set-[gu]id) of verified file
	 */
	error = mac_veriexec_check_vp(cred, vp, VVERIFY);
	if (error == EAUTH)             /* it isn't verified */
		return (0);
	if (error == 0 && (mode & (S_ISUID|S_ISGID)) != 0)
		return (EAUTH);
	return (0);
}

/**
 * @internal
 * @brief Initialize the mac_veriexec MAC policy
 *
 * @param mpc		MAC policy configuration
 */
static void
mac_veriexec_init(struct mac_policy_conf *mpc __unused)
{
	/* Initialize state */
	mac_veriexec_state = VERIEXEC_STATE_INACTIVE;

	/* Initialize meta-data storage */
	mac_veriexec_metadata_init();

	/* Initialize fingerprint ops */
	mac_veriexec_fingerprint_init();

	/* Register event handlers */
	EVENTHANDLER_REGISTER(vfs_mounted, mac_veriexec_vfs_mounted, NULL,
	    EVENTHANDLER_PRI_FIRST);
	EVENTHANDLER_REGISTER(vfs_unmounted, mac_veriexec_vfs_unmounted, NULL,
	    EVENTHANDLER_PRI_LAST);
}

/**
 * @internal
 * @brief MAC policy-specific syscall for mac_veriexec
 *
 * The following syscalls are implemented:
 *   - @c MAC_VERIEXEC_CHECK_SYSCALL
 *        Check if the file referenced by a file descriptor has a fingerprint
 *        registered in the meta-data store.
 *
 * @param td		calling thread
 * @param call		system call number
 * @param arg		arugments to the syscall
 *
 * @return 0 on success, otherwise an error code.
 */
static int
mac_veriexec_syscall(struct thread *td, int call, void *arg)
{
	struct image_params img;
	struct nameidata nd;
	cap_rights_t rights;
	struct vattr va;
	struct file *fp;
	int error;

	switch (call) {
	case MAC_VERIEXEC_CHECK_FD_SYSCALL:
		/* Get the vnode associated with the file descriptor passed */
		error = getvnode(td, (uintptr_t) arg, cap_rights_init(&rights,
		    CAP_READ), &fp);
		if (error)
			return (error);
		if (fp->f_type != DTYPE_VNODE) {
			MAC_VERIEXEC_DBG(3, "MAC_VERIEXEC_CHECK_SYSCALL: "
			    "file is not vnode type (type=0x%x)",
			    fp->f_type);
			error = EINVAL;
			goto cleanup_file;
		}

		/*
		 * setup the bits of image_params that are used by
		 * mac_veriexec_check_fingerprint().
		 */
		bzero(&img, sizeof(img));
		img.proc = td->td_proc;
		img.vp = fp->f_vnode;
		img.attr = &va;

		/*
		 * Get vnode attributes
		 * (need to obtain a lock on the vnode first)
		 */
		vn_lock(img.vp, LK_EXCLUSIVE | LK_RETRY);
		error = VOP_GETATTR(fp->f_vnode, &va,  td->td_ucred);
		if (error)
			goto check_done;
		       
		MAC_VERIEXEC_DBG(2, "mac_veriexec_fingerprint_check_image: "
		    "va_mode=%o, check_files=%d\n", va.va_mode,
		    ((va.va_mode & (S_IXUSR|S_IXGRP|S_IXOTH)) == 0));
		error = mac_veriexec_fingerprint_check_image(&img,
		    ((va.va_mode & (S_IXUSR|S_IXGRP|S_IXOTH)) == 0), td);
check_done:
		/* Release the lock we obtained earlier */
		VOP_UNLOCK(img.vp, 0);
cleanup_file:
		fdrop(fp, td);
		break;
	case MAC_VERIEXEC_CHECK_PATH_SYSCALL:
		/* Look up the path to get the vnode */
		NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF | AUDITVNODE1,
		    UIO_USERSPACE, arg, td);
		error = namei(&nd);
		if (error != 0)
			break;
		NDFREE(&nd, NDF_ONLY_PNBUF);

		/* Check the fingerprint status of the vnode */
		error = mac_veriexec_check_vp(td->td_ucred, nd.ni_vp, VVERIFY);
		vput(nd.ni_vp);
		break;
	default:
		error = EOPNOTSUPP;
	}
	return (error);
}

static struct mac_policy_ops mac_veriexec_ops =
{
	.mpo_init = mac_veriexec_init,
	.mpo_syscall = mac_veriexec_syscall,
	.mpo_kld_check_load = mac_veriexec_kld_check_load,
	.mpo_mount_destroy_label = mac_veriexec_mount_destroy_label,
	.mpo_mount_init_label = mac_veriexec_mount_init_label,
	.mpo_priv_check = mac_veriexec_priv_check,
	.mpo_proc_check_debug = mac_veriexec_proc_check_debug,
	.mpo_vnode_check_exec = mac_veriexec_vnode_check_exec,
	.mpo_vnode_check_open = mac_veriexec_vnode_check_open,
	.mpo_vnode_check_setmode = mac_veriexec_vnode_check_setmode,
	.mpo_vnode_copy_label = mac_veriexec_copy_label,
	.mpo_vnode_destroy_label = mac_veriexec_vnode_destroy_label,
	.mpo_vnode_init_label = mac_veriexec_vnode_init_label,
};

MAC_POLICY_SET(&mac_veriexec_ops, mac_veriexec, MAC_VERIEXEC_FULLNAME,
    MPC_LOADTIME_FLAG_NOTLATE, &mac_veriexec_slot);
MODULE_VERSION(mac_veriexec, 1);

/**
 * @brief Get the fingerprint status set on a vnode.
 *
 * @param vp		vnode to obtain fingerprint status from
 *
 * @return Fingerprint status assigned to the vnode.
 */
fingerprint_status_t
mac_veriexec_get_fingerprint_status(struct vnode *vp)
{
	fingerprint_status_t fps;

	fps = SLOT(vp->v_label);
	switch (fps) {
	case FINGERPRINT_VALID:
	case FINGERPRINT_INDIRECT:
	case FINGERPRINT_FILE:
		break;
	default:
		/* we may need to recurse */
		if (strcmp(vp->v_tag, "null") == 0) {
			struct vnode *ldvp;

			ldvp = NULLVPTOLOWERVP(vp);
			return mac_veriexec_get_fingerprint_status(ldvp);
		}
		break;
	}
	return fps;
}

/**
 * @brief Get the current verified execution subsystem state.
 *
 * @return Current set of verified execution subsystem state flags.
 */
int
mac_veriexec_get_state(void)
{

	return (mac_veriexec_state);
}

/**
 * @brief Determine if the verified execution subsystem state has specific
 *     flags set.
 *
 * @param state		mask of flags to check
 *
 * @return State flags set within the masked bits
 */
int
mac_veriexec_in_state(int state)
{

	return (mac_veriexec_state & state);
}

/**
 * @brief Set the fingerprint status for a vnode
 *
 * Fingerprint status is stored in the MAC per-policy slot assigned to
 * mac_veriexec.
 *
 * @param vp		vnode to store the fingerprint status on
 * @param fp_status	fingerprint status to store
 */
void
mac_veriexec_set_fingerprint_status(struct vnode *vp,
    fingerprint_status_t fp_status)
{

	/* recurse until we find the real storage */
	if (strcmp(vp->v_tag, "null") == 0) {
		struct vnode *ldvp;

		ldvp = NULLVPTOLOWERVP(vp);
		mac_veriexec_set_fingerprint_status(ldvp, fp_status);
		return;
	}
	SLOT_SET(vp->v_label, fp_status);
}

/**
 * @brief Set verified execution subsystem state flags
 *
 * @note Flags can only be added to the current state, not removed.
 *
 * @param state		state flags to add to the current state
 */
void
mac_veriexec_set_state(int state)
{

	mac_veriexec_state |= state;
}

/**
 * @brief Determine if the process is trusted
 *
 * @param cred		credentials to use
 * @param p		the process in question
 *
 * @return 1 if the process is trusted, otherwise 0.
 */
int
mac_veriexec_proc_is_trusted(struct ucred *cred, struct proc *p)
{
	int error, flags;

	error = mac_veriexec_metadata_get_executable_flags(cred, p, &flags, 0);

	/* Any errors, deny access */
	if (error != 0)
		return (0);

	/* Check that the trusted flag is set */
	return ((flags & VERIEXEC_TRUSTED) == VERIEXEC_TRUSTED);
}
