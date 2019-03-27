/*-
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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
#include <sys/priv.h>
#include <sys/vnode.h>
#include <sys/mntent.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/jail.h>
#include <sys/policy.h>
#include <sys/zfs_vfsops.h>

int
secpolicy_nfs(cred_t *cr)
{

	return (priv_check_cred(cr, PRIV_NFS_DAEMON));
}

int
secpolicy_zfs(cred_t *cr)
{

	return (priv_check_cred(cr, PRIV_VFS_MOUNT));
}

int
secpolicy_sys_config(cred_t *cr, int checkonly __unused)
{

	return (priv_check_cred(cr, PRIV_ZFS_POOL_CONFIG));
}

int
secpolicy_zinject(cred_t *cr)
{

	return (priv_check_cred(cr, PRIV_ZFS_INJECT));
}

int
secpolicy_fs_unmount(cred_t *cr, struct mount *vfsp __unused)
{

	return (priv_check_cred(cr, PRIV_VFS_UNMOUNT));
}

int
secpolicy_fs_owner(struct mount *mp, cred_t *cr)
{

	if (zfs_super_owner) {
		if (cr->cr_uid == mp->mnt_cred->cr_uid &&
		    cr->cr_prison == mp->mnt_cred->cr_prison) {
			return (0);
		}
	}
	return (EPERM);
}

/*
 * This check is done in kern_link(), so we could just return 0 here.
 */
extern int hardlink_check_uid;
int
secpolicy_basic_link(vnode_t *vp, cred_t *cr)
{

	if (!hardlink_check_uid)
		return (0);
	if (secpolicy_fs_owner(vp->v_mount, cr) == 0)
		return (0);
	return (priv_check_cred(cr, PRIV_VFS_LINK));
}

int
secpolicy_vnode_stky_modify(cred_t *cr)
{

	return (EPERM);
}

int
secpolicy_vnode_remove(vnode_t *vp, cred_t *cr)
{

	if (secpolicy_fs_owner(vp->v_mount, cr) == 0)
		return (0);
	return (priv_check_cred(cr, PRIV_VFS_ADMIN));
}

int
secpolicy_vnode_access(cred_t *cr, vnode_t *vp, uid_t owner, accmode_t accmode)
{

	if (secpolicy_fs_owner(vp->v_mount, cr) == 0)
		return (0);

	if ((accmode & VREAD) && priv_check_cred(cr, PRIV_VFS_READ) != 0)
		return (EACCES);
	if ((accmode & VWRITE) &&
	    priv_check_cred(cr, PRIV_VFS_WRITE) != 0) {
		return (EACCES);
	}
	if (accmode & VEXEC) {
		if (vp->v_type == VDIR) {
			if (priv_check_cred(cr, PRIV_VFS_LOOKUP) != 0)
				return (EACCES);
		} else {
			if (priv_check_cred(cr, PRIV_VFS_EXEC) != 0)
				return (EACCES);
		}
	}
	return (0);
}

/*
 * Like secpolicy_vnode_access() but we get the actual wanted mode and the
 * current mode of the file, not the missing bits.
 */
int
secpolicy_vnode_access2(cred_t *cr, vnode_t *vp, uid_t owner,
    accmode_t curmode, accmode_t wantmode)
{
	accmode_t mode;

	mode = ~curmode & wantmode;

	if (mode == 0)
		return (0);

	return (secpolicy_vnode_access(cr, vp, owner, mode));
}

int
secpolicy_vnode_any_access(cred_t *cr, vnode_t *vp, uid_t owner)
{
	static int privs[] = {
	    PRIV_VFS_ADMIN,
	    PRIV_VFS_READ,
	    PRIV_VFS_WRITE,
	    PRIV_VFS_EXEC,
	    PRIV_VFS_LOOKUP
	};
	int i;

	if (secpolicy_fs_owner(vp->v_mount, cr) == 0)
		return (0);

	/* Same as secpolicy_vnode_setdac */
	if (owner == cr->cr_uid)
		return (0);

	for (i = 0; i < sizeof (privs)/sizeof (int); i++) {
		boolean_t allzone = B_FALSE;
		int priv;

		switch (priv = privs[i]) {
		case PRIV_VFS_EXEC:
			if (vp->v_type == VDIR)
				continue;
			break;
		case PRIV_VFS_LOOKUP:
			if (vp->v_type != VDIR)
				continue;
			break;
		}
		if (priv_check_cred(cr, priv) == 0)
			return (0);
	}
	return (EPERM);
}

int
secpolicy_vnode_setdac(vnode_t *vp, cred_t *cr, uid_t owner)
{

	if (owner == cr->cr_uid)
		return (0);
	if (secpolicy_fs_owner(vp->v_mount, cr) == 0)
		return (0);
	return (priv_check_cred(cr, PRIV_VFS_ADMIN));
}

int
secpolicy_vnode_setattr(cred_t *cr, vnode_t *vp, struct vattr *vap,
    const struct vattr *ovap, int flags,
    int unlocked_access(void *, int, cred_t *), void *node)
{
	int mask = vap->va_mask;
	int error;

	if (mask & AT_SIZE) {
		if (vp->v_type == VDIR)
			return (EISDIR);
		error = unlocked_access(node, VWRITE, cr);
		if (error)
			return (error);
	}
	if (mask & AT_MODE) {
		/*
		 * If not the owner of the file then check privilege
		 * for two things: the privilege to set the mode at all
		 * and, if we're setting setuid, we also need permissions
		 * to add the set-uid bit, if we're not the owner.
		 * In the specific case of creating a set-uid root
		 * file, we need even more permissions.
		 */
		error = secpolicy_vnode_setdac(vp, cr, ovap->va_uid);
		if (error)
			return (error);
		error = secpolicy_setid_setsticky_clear(vp, vap, ovap, cr);
		if (error)
			return (error);
	} else {
		vap->va_mode = ovap->va_mode;
	}
	if (mask & (AT_UID | AT_GID)) {
		error = secpolicy_vnode_setdac(vp, cr, ovap->va_uid);
		if (error)
			return (error);

		/*
		 * To change the owner of a file, or change the group of a file to a
		 * group of which we are not a member, the caller must have
		 * privilege.
		 */
		if (((mask & AT_UID) && vap->va_uid != ovap->va_uid) ||
		    ((mask & AT_GID) && vap->va_gid != ovap->va_gid &&
		     !groupmember(vap->va_gid, cr))) {
			if (secpolicy_fs_owner(vp->v_mount, cr) != 0) {
				error = priv_check_cred(cr, PRIV_VFS_CHOWN);
				if (error)
					return (error);
			}
		}

		if (((mask & AT_UID) && vap->va_uid != ovap->va_uid) ||
		    ((mask & AT_GID) && vap->va_gid != ovap->va_gid)) {
			secpolicy_setid_clear(vap, vp, cr);
		}
	}
	if (mask & (AT_ATIME | AT_MTIME)) {
		/*
		 * From utimes(2):
		 * If times is NULL, ... The caller must be the owner of
		 * the file, have permission to write the file, or be the
		 * super-user.
		 * If times is non-NULL, ... The caller must be the owner of
		 * the file or be the super-user.
		 */
		error = secpolicy_vnode_setdac(vp, cr, ovap->va_uid);
		if (error && (vap->va_vaflags & VA_UTIMES_NULL))
			error = unlocked_access(node, VWRITE, cr);
		if (error)
			return (error);
	}
	return (0);
}

int
secpolicy_vnode_create_gid(cred_t *cr)
{

	return (EPERM);
}

int
secpolicy_vnode_setids_setgids(vnode_t *vp, cred_t *cr, gid_t gid)
{

	if (groupmember(gid, cr))
		return (0);
	if (secpolicy_fs_owner(vp->v_mount, cr) == 0)
		return (0);
	return (priv_check_cred(cr, PRIV_VFS_SETGID));
}

int
secpolicy_vnode_setid_retain(vnode_t *vp, cred_t *cr,
    boolean_t issuidroot __unused)
{

	if (secpolicy_fs_owner(vp->v_mount, cr) == 0)
		return (0);
	return (priv_check_cred(cr, PRIV_VFS_RETAINSUGID));
}

void
secpolicy_setid_clear(struct vattr *vap, vnode_t *vp, cred_t *cr)
{

	if (secpolicy_fs_owner(vp->v_mount, cr) == 0)
		return;

	if ((vap->va_mode & (S_ISUID | S_ISGID)) != 0) {
		if (priv_check_cred(cr, PRIV_VFS_RETAINSUGID)) {
			vap->va_mask |= AT_MODE;
			vap->va_mode &= ~(S_ISUID|S_ISGID);
		}
	}
}

int
secpolicy_setid_setsticky_clear(vnode_t *vp, struct vattr *vap,
    const struct vattr *ovap, cred_t *cr)
{
        int error;

	if (secpolicy_fs_owner(vp->v_mount, cr) == 0)
		return (0);

	/*
	 * Privileged processes may set the sticky bit on non-directories,
	 * as well as set the setgid bit on a file with a group that the process
	 * is not a member of. Both of these are allowed in jail(8).
	 */
	if (vp->v_type != VDIR && (vap->va_mode & S_ISTXT)) {
		if (priv_check_cred(cr, PRIV_VFS_STICKYFILE))
			return (EFTYPE);
	}
	/*
	 * Check for privilege if attempting to set the
	 * group-id bit.
	 */
	if ((vap->va_mode & S_ISGID) != 0) {
		error = secpolicy_vnode_setids_setgids(vp, cr, ovap->va_gid);
		if (error)
			return (error);
	}
	/*
	 * Deny setting setuid if we are not the file owner.
	 */
	if ((vap->va_mode & S_ISUID) && ovap->va_uid != cr->cr_uid) {
		error = priv_check_cred(cr, PRIV_VFS_ADMIN);
		if (error)
			return (error);
	}
	return (0);
}

int
secpolicy_fs_mount(cred_t *cr, vnode_t *mvp, struct mount *vfsp)
{

	return (priv_check_cred(cr, PRIV_VFS_MOUNT));
}

int
secpolicy_vnode_owner(vnode_t *vp, cred_t *cr, uid_t owner)
{

	if (owner == cr->cr_uid)
		return (0);
	if (secpolicy_fs_owner(vp->v_mount, cr) == 0)
		return (0);

	/* XXX: vfs_suser()? */
	return (priv_check_cred(cr, PRIV_VFS_MOUNT_OWNER));
}

int
secpolicy_vnode_chown(vnode_t *vp, cred_t *cr, uid_t owner)
{

	if (secpolicy_fs_owner(vp->v_mount, cr) == 0)
		return (0);
	return (priv_check_cred(cr, PRIV_VFS_CHOWN));
}

void
secpolicy_fs_mount_clearopts(cred_t *cr, struct mount *vfsp)
{

	if (priv_check_cred(cr, PRIV_VFS_MOUNT_NONUSER) != 0) {
		MNT_ILOCK(vfsp);
		vfsp->vfs_flag |= VFS_NOSETUID | MNT_USER;
		vfs_clearmntopt(vfsp, MNTOPT_SETUID);
		vfs_setmntopt(vfsp, MNTOPT_NOSETUID, NULL, 0);
		MNT_IUNLOCK(vfsp);
	}
}

/*
 * Check privileges for setting xvattr attributes
 */
int
secpolicy_xvattr(vnode_t *vp, xvattr_t *xvap, uid_t owner, cred_t *cr,
    vtype_t vtype)
{

	if (secpolicy_fs_owner(vp->v_mount, cr) == 0)
		return (0);
	return (priv_check_cred(cr, PRIV_VFS_SYSFLAGS));
}

int
secpolicy_smb(cred_t *cr)
{

	return (priv_check_cred(cr, PRIV_NETSMB));
}
