/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017, Fedor Uporov
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
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/vnode.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/endian.h>
#include <sys/conf.h>
#include <sys/mount.h>
#include <sys/extattr.h>

#include <fs/ext2fs/fs.h>
#include <fs/ext2fs/ext2fs.h>
#include <fs/ext2fs/inode.h>
#include <fs/ext2fs/ext2_acl.h>
#include <fs/ext2fs/ext2_extattr.h>
#include <fs/ext2fs/ext2_extern.h>
#include <fs/ext2fs/ext2_dinode.h>
#include <fs/ext2fs/ext2_mount.h>

#ifdef UFS_ACL

void
ext2_sync_acl_from_inode(struct inode *ip, struct acl *acl)
{
	struct acl_entry	*acl_mask, *acl_group_obj;
	int	i;

	/*
	 * Update ACL_USER_OBJ, ACL_OTHER, but simply identify ACL_MASK
	 * and ACL_GROUP_OBJ for use after we know whether ACL_MASK is
	 * present.
	 */
	acl_mask = NULL;
	acl_group_obj = NULL;
	for (i = 0; i < acl->acl_cnt; i++) {
		switch (acl->acl_entry[i].ae_tag) {
		case ACL_USER_OBJ:
			acl->acl_entry[i].ae_perm = acl_posix1e_mode_to_perm(
			    ACL_USER_OBJ, ip->i_mode);
			acl->acl_entry[i].ae_id = ACL_UNDEFINED_ID;
			break;

		case ACL_GROUP_OBJ:
			acl_group_obj = &acl->acl_entry[i];
			acl->acl_entry[i].ae_id = ACL_UNDEFINED_ID;
			break;

		case ACL_OTHER:
			acl->acl_entry[i].ae_perm = acl_posix1e_mode_to_perm(
			    ACL_OTHER, ip->i_mode);
			acl->acl_entry[i].ae_id = ACL_UNDEFINED_ID;
			break;

		case ACL_MASK:
			acl_mask = &acl->acl_entry[i];
			acl->acl_entry[i].ae_id = ACL_UNDEFINED_ID;
			break;

		case ACL_USER:
		case ACL_GROUP:
			break;

		default:
			panic("ext2_sync_acl_from_inode(): bad ae_tag");
		}
	}

	if (acl_group_obj == NULL)
		panic("ext2_sync_acl_from_inode(): no ACL_GROUP_OBJ");

	if (acl_mask == NULL) {
		/*
		 * There is no ACL_MASK, so update ACL_GROUP_OBJ.
		 */
		acl_group_obj->ae_perm = acl_posix1e_mode_to_perm(
		    ACL_GROUP_OBJ, ip->i_mode);
	} else {
		/*
		 * Update the ACL_MASK entry instead of ACL_GROUP_OBJ.
		 */
		acl_mask->ae_perm = acl_posix1e_mode_to_perm(ACL_GROUP_OBJ,
		    ip->i_mode);
	}
}

static void
ext2_sync_inode_from_acl(struct acl *acl, struct inode *ip)
{

	ip->i_mode &= ACL_PRESERVE_MASK;
	ip->i_mode |= acl_posix1e_acl_to_mode(acl);
}

/*
 * Convert from filesystem to in-memory representation.
 */
static int
ext4_acl_from_disk(char *value, size_t size, struct acl *acl)
{
	const char *end;
	int n, count, s;

	if (value == NULL)
		return (EINVAL);

	end = value + size;

	if (((struct ext2_acl_header *)value)->a_version != EXT4_ACL_VERSION)
		return (EINVAL);

	if (size < sizeof(struct ext2_acl_header))
		return (EINVAL);

	s = size - sizeof(struct ext2_acl_header);
	s -= 4 * sizeof(struct ext2_acl_entry_short);
	if (s < 0)
		if ((size - sizeof(struct ext2_acl_header)) %
		    sizeof(struct ext2_acl_entry_short))
			count = -1;
		else
			count = (size - sizeof(struct ext2_acl_header)) /
			    sizeof(struct ext2_acl_entry_short);
	else
		if (s % sizeof(struct ext2_acl_entry))
			count = -1;
		else
			count = s / sizeof(struct ext2_acl_entry) + 4;

	if (count <= 0 || count > acl->acl_maxcnt)
		return (EINVAL);

	value = value + sizeof(struct ext2_acl_header);

	for (n = 0; n < count; n++) {
		struct ext2_acl_entry *entry = (struct ext2_acl_entry *)value;
		if ((char *)value + sizeof(struct ext2_acl_entry_short) > end)
			return (EINVAL);

		acl->acl_entry[n].ae_tag  = entry->ae_tag;
		acl->acl_entry[n].ae_perm = entry->ae_perm;

		switch (acl->acl_entry[n].ae_tag) {
		case ACL_USER_OBJ:
		case ACL_GROUP_OBJ:
		case ACL_MASK:
		case ACL_OTHER:
			value = (char *)value + sizeof(struct ext2_acl_entry_short);
			break;

		case ACL_USER:
			value = (char *)value + sizeof(struct ext2_acl_entry);
			if ((char *)value > end)
				return (EINVAL);

			acl->acl_entry[n].ae_id = entry->ae_id;
			break;

		case ACL_GROUP:
			value = (char *)value + sizeof(struct ext2_acl_entry);
			if ((char *)value > end)
				return (EINVAL);

			acl->acl_entry[n].ae_id = entry->ae_id;
			break;

		default:
			return (EINVAL);
		}
	}

	if (value != end)
		return (EINVAL);

	acl->acl_cnt = count;

	return (0);
}

static int
ext2_getacl_posix1e(struct vop_getacl_args *ap)
{
	int attrnamespace;
	const char *attrname;
	char *value;
	int len;
	int error;

	switch (ap->a_type) {
	case ACL_TYPE_DEFAULT:
		attrnamespace = POSIX1E_ACL_DEFAULT_EXTATTR_NAMESPACE;
		attrname = POSIX1E_ACL_DEFAULT_EXTATTR_NAME;
		break;
	case ACL_TYPE_ACCESS:
		attrnamespace = POSIX1E_ACL_ACCESS_EXTATTR_NAMESPACE;
		attrname = POSIX1E_ACL_ACCESS_EXTATTR_NAME;
		break;
	default:
		return (EINVAL);
	}

	len = sizeof(*ap->a_aclp) + sizeof(struct ext2_acl_header);
	value = malloc(len, M_ACL, M_WAITOK);
	if (!value)
		return (ENOMEM);

	error = vn_extattr_get(ap->a_vp, IO_NODELOCKED, attrnamespace, attrname,
	    &len, value, ap->a_td);
	if (error == ENOATTR) {
		switch (ap->a_type) {
		case ACL_TYPE_ACCESS:
			ap->a_aclp->acl_cnt = 3;
			ap->a_aclp->acl_entry[0].ae_tag = ACL_USER_OBJ;
			ap->a_aclp->acl_entry[0].ae_id = ACL_UNDEFINED_ID;
			ap->a_aclp->acl_entry[0].ae_perm = ACL_PERM_NONE;
			ap->a_aclp->acl_entry[1].ae_tag = ACL_GROUP_OBJ;
			ap->a_aclp->acl_entry[1].ae_id = ACL_UNDEFINED_ID;
			ap->a_aclp->acl_entry[1].ae_perm = ACL_PERM_NONE;
			ap->a_aclp->acl_entry[2].ae_tag = ACL_OTHER;
			ap->a_aclp->acl_entry[2].ae_id = ACL_UNDEFINED_ID;
			ap->a_aclp->acl_entry[2].ae_perm = ACL_PERM_NONE;
			break;

		case ACL_TYPE_DEFAULT:
			ap->a_aclp->acl_cnt = 0;
			break;
		}
	} else if (error != 0)
		goto out;

	if (!error) {
		error = ext4_acl_from_disk(value, len, ap->a_aclp);
		if (error)
			goto out;
	}

	if (error == ENOATTR)
		error = 0;

	if (ap->a_type == ACL_TYPE_ACCESS)
		ext2_sync_acl_from_inode(VTOI(ap->a_vp), ap->a_aclp);

out:
	free(value, M_TEMP);
	return (error);
}

int
ext2_getacl(struct vop_getacl_args *ap)
{

	if (((ap->a_vp->v_mount->mnt_flag & MNT_ACLS) == 0) ||
	    ((ap->a_vp->v_mount->mnt_flag & MNT_NFS4ACLS) != 0))
		return (EOPNOTSUPP);

	if (ap->a_type == ACL_TYPE_NFS4)
		return (ENOTSUP);

	return (ext2_getacl_posix1e(ap));
}

/*
 * Convert from in-memory to filesystem representation.
 */
static int
ext4_acl_to_disk(const struct acl *acl, size_t *size, char *value)
{
	struct ext2_acl_header *ext_acl;
	int disk_size;
	char *e;
	size_t n;

	if (acl->acl_cnt <= 4)
		disk_size = sizeof(struct ext2_acl_header) +
		   acl->acl_cnt * sizeof(struct ext2_acl_entry_short);
	else
		disk_size = sizeof(struct ext2_acl_header) +
		    4 * sizeof(struct ext2_acl_entry_short) +
		    (acl->acl_cnt - 4) * sizeof(struct ext2_acl_entry);

	if (disk_size > *size)
		return (EINVAL);

	*size = disk_size;
	ext_acl = (struct ext2_acl_header *)value;

	ext_acl->a_version = EXT4_ACL_VERSION;
	e = (char *)ext_acl + sizeof(struct ext2_acl_header);
	for (n = 0; n < acl->acl_cnt; n++) {
		const struct acl_entry *acl_e = &acl->acl_entry[n];
		struct ext2_acl_entry *entry = (struct ext2_acl_entry *)e;
		entry->ae_tag  = acl_e->ae_tag;
		entry->ae_perm = acl_e->ae_perm;
		switch (acl_e->ae_tag) {
		case ACL_USER:
			entry->ae_id = acl_e->ae_id;
			e += sizeof(struct ext2_acl_entry);
			break;

		case ACL_GROUP:
			entry->ae_id = acl_e->ae_id;
			e += sizeof(struct ext2_acl_entry);
			break;

		case ACL_USER_OBJ:
		case ACL_GROUP_OBJ:
		case ACL_MASK:
		case ACL_OTHER:
			e += sizeof(struct ext2_acl_entry_short);
			break;

		default:
			return (EINVAL);
		}
	}

	return (0);
}

static int
ext2_setacl_posix1e(struct vop_setacl_args *ap)
{
	struct inode *ip = VTOI(ap->a_vp);
	char *value;
	size_t len;
	int error;

	if ((ap->a_vp->v_mount->mnt_flag & MNT_ACLS) == 0)
		return (EINVAL);

	/*
	 * If this is a set operation rather than a delete operation,
	 * invoke VOP_ACLCHECK() on the passed ACL to determine if it is
	 * valid for the target.  This will include a check on ap->a_type.
	 */
	if (ap->a_aclp != NULL) {
		/*
		 * Set operation.
		 */
		error = VOP_ACLCHECK(ap->a_vp, ap->a_type, ap->a_aclp,
		    ap->a_cred, ap->a_td);
		if (error)
			return (error);
	} else {
		/*
		 * Delete operation.
		 * POSIX.1e allows only deletion of the default ACL on a
		 * directory (ACL_TYPE_DEFAULT).
		 */
		if (ap->a_type != ACL_TYPE_DEFAULT)
			return (EINVAL);
		if (ap->a_vp->v_type != VDIR)
			return (ENOTDIR);
	}

	if (ap->a_vp->v_mount->mnt_flag & MNT_RDONLY)
		return (EROFS);

	/*
	 * Authorize the ACL operation.
	 */
	if (ip->i_flags & (IMMUTABLE | APPEND))
		return (EPERM);

	/*
	 * Must hold VADMIN (be file owner) or have appropriate privilege.
	 */
	if ((error = VOP_ACCESS(ap->a_vp, VADMIN, ap->a_cred, ap->a_td)))
		return (error);

	switch (ap->a_type) {
	case ACL_TYPE_ACCESS:
		len = sizeof(*ap->a_aclp) + sizeof(struct ext2_acl_header);
		value = malloc(len, M_ACL, M_WAITOK | M_ZERO);
		error = ext4_acl_to_disk(ap->a_aclp, &len, value);
		if (error == 0)
			error = vn_extattr_set(ap->a_vp, IO_NODELOCKED,
			    POSIX1E_ACL_ACCESS_EXTATTR_NAMESPACE,
			    POSIX1E_ACL_ACCESS_EXTATTR_NAME, len,
			    value, ap->a_td);

		free(value, M_ACL);
		break;

	case ACL_TYPE_DEFAULT:
		if (ap->a_aclp == NULL) {
			error = vn_extattr_rm(ap->a_vp, IO_NODELOCKED,
			    POSIX1E_ACL_DEFAULT_EXTATTR_NAMESPACE,
			    POSIX1E_ACL_DEFAULT_EXTATTR_NAME, ap->a_td);

			/*
			 * Attempting to delete a non-present default ACL
			 * will return success for portability purposes.
			 * (TRIX)
			 *
			 * XXX: Note that since we can't distinguish
			 * "that EA is not supported" from "that EA is not
			 * defined", the success case here overlaps the
			 * the ENOATTR->EOPNOTSUPP case below.
			 */
			if (error == ENOATTR)
				error = 0;
		} else {
			len = sizeof(*ap->a_aclp) + sizeof(struct ext2_acl_header);
			value = malloc(len, M_ACL, M_WAITOK | M_ZERO);
			error = ext4_acl_to_disk(ap->a_aclp, &len, value);
			if (error == 0)
				error = vn_extattr_set(ap->a_vp, IO_NODELOCKED,
				    POSIX1E_ACL_DEFAULT_EXTATTR_NAMESPACE,
				    POSIX1E_ACL_DEFAULT_EXTATTR_NAME, len,
				    value, ap->a_td);

			free(value, M_ACL);
		}
		break;

	default:
		error = EINVAL;
	}

	/*
	 * Map lack of attribute definition in UFS_EXTATTR into lack of
	 * support for ACLs on the filesystem.
	 */
	if (error == ENOATTR)
		return (EOPNOTSUPP);

	if (error != 0)
		return (error);

	if (ap->a_type == ACL_TYPE_ACCESS) {
		/*
		 * Now that the EA is successfully updated, update the
		 * inode and mark it as changed.
		 */
		ext2_sync_inode_from_acl(ap->a_aclp, ip);
		ip->i_flag |= IN_CHANGE;
		error = ext2_update(ip->i_vnode, 1);
	}

	VN_KNOTE_UNLOCKED(ap->a_vp, NOTE_ATTRIB);

	return (error);
}

int
ext2_setacl(struct vop_setacl_args *ap)
{
	if (((ap->a_vp->v_mount->mnt_flag & MNT_ACLS) == 0) ||
	    ((ap->a_vp->v_mount->mnt_flag & MNT_NFS4ACLS) != 0))
		return (EOPNOTSUPP);

	if (ap->a_type == ACL_TYPE_NFS4)
		return (ENOTSUP);

	return (ext2_setacl_posix1e(ap));
}

/*
 * Check the validity of an ACL for a file.
 */
int
ext2_aclcheck(struct vop_aclcheck_args *ap)
{

	if (((ap->a_vp->v_mount->mnt_flag & MNT_ACLS) == 0) ||
	    ((ap->a_vp->v_mount->mnt_flag & MNT_NFS4ACLS) != 0))
		return (EOPNOTSUPP);

	if (ap->a_type == ACL_TYPE_NFS4)
		return (ENOTSUP);

	if ((ap->a_vp->v_mount->mnt_flag & MNT_ACLS) == 0)
		return (EINVAL);

	/*
	 * Verify we understand this type of ACL, and that it applies
	 * to this kind of object.
	 * Rely on the acl_posix1e_check() routine to verify the contents.
	 */
	switch (ap->a_type) {
		case ACL_TYPE_ACCESS:
		break;

		case ACL_TYPE_DEFAULT:
			if (ap->a_vp->v_type != VDIR)
				return (EINVAL);
		break;

		default:
			return (EINVAL);
	}

	return (acl_posix1e_check(ap->a_aclp));
}

#endif /* UFS_ACL */
