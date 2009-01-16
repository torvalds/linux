/*
 * Copyright (c) 2001-2002,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_types.h"
#include "xfs_bit.h"
#include "xfs_inum.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_acl.h"
#include "xfs_attr.h"
#include "xfs_vnodeops.h"

#include <linux/capability.h>
#include <linux/posix_acl_xattr.h>

STATIC int	xfs_acl_setmode(struct inode *, xfs_acl_t *, int *);
STATIC void     xfs_acl_filter_mode(mode_t, xfs_acl_t *);
STATIC void	xfs_acl_get_endian(xfs_acl_t *);
STATIC int	xfs_acl_access(uid_t, gid_t, xfs_acl_t *, mode_t, cred_t *);
STATIC int	xfs_acl_invalid(xfs_acl_t *);
STATIC void	xfs_acl_sync_mode(mode_t, xfs_acl_t *);
STATIC void	xfs_acl_get_attr(struct inode *, xfs_acl_t *, int, int, int *);
STATIC void	xfs_acl_set_attr(struct inode *, xfs_acl_t *, int, int *);
STATIC int	xfs_acl_allow_set(struct inode *, int);

kmem_zone_t *xfs_acl_zone;


/*
 * Test for existence of access ACL attribute as efficiently as possible.
 */
int
xfs_acl_vhasacl_access(
	struct inode	*vp)
{
	int		error;

	xfs_acl_get_attr(vp, NULL, _ACL_TYPE_ACCESS, ATTR_KERNOVAL, &error);
	return (error == 0);
}

/*
 * Test for existence of default ACL attribute as efficiently as possible.
 */
int
xfs_acl_vhasacl_default(
	struct inode	*vp)
{
	int		error;

	if (!S_ISDIR(vp->i_mode))
		return 0;
	xfs_acl_get_attr(vp, NULL, _ACL_TYPE_DEFAULT, ATTR_KERNOVAL, &error);
	return (error == 0);
}

/*
 * Convert from extended attribute representation to in-memory for XFS.
 */
STATIC int
posix_acl_xattr_to_xfs(
	posix_acl_xattr_header	*src,
	size_t			size,
	xfs_acl_t		*dest)
{
	posix_acl_xattr_entry	*src_entry;
	xfs_acl_entry_t		*dest_entry;
	int			n;

	if (!src || !dest)
		return EINVAL;

	if (size < sizeof(posix_acl_xattr_header))
		return EINVAL;

	if (src->a_version != cpu_to_le32(POSIX_ACL_XATTR_VERSION))
		return EOPNOTSUPP;

	memset(dest, 0, sizeof(xfs_acl_t));
	dest->acl_cnt = posix_acl_xattr_count(size);
	if (dest->acl_cnt < 0 || dest->acl_cnt > XFS_ACL_MAX_ENTRIES)
		return EINVAL;

	/*
	 * acl_set_file(3) may request that we set default ACLs with
	 * zero length -- defend (gracefully) against that here.
	 */
	if (!dest->acl_cnt)
		return 0;

	src_entry = (posix_acl_xattr_entry *)((char *)src + sizeof(*src));
	dest_entry = &dest->acl_entry[0];

	for (n = 0; n < dest->acl_cnt; n++, src_entry++, dest_entry++) {
		dest_entry->ae_perm = le16_to_cpu(src_entry->e_perm);
		if (_ACL_PERM_INVALID(dest_entry->ae_perm))
			return EINVAL;
		dest_entry->ae_tag  = le16_to_cpu(src_entry->e_tag);
		switch(dest_entry->ae_tag) {
		case ACL_USER:
		case ACL_GROUP:
			dest_entry->ae_id = le32_to_cpu(src_entry->e_id);
			break;
		case ACL_USER_OBJ:
		case ACL_GROUP_OBJ:
		case ACL_MASK:
		case ACL_OTHER:
			dest_entry->ae_id = ACL_UNDEFINED_ID;
			break;
		default:
			return EINVAL;
		}
	}
	if (xfs_acl_invalid(dest))
		return EINVAL;

	return 0;
}

/*
 * Comparison function called from xfs_sort().
 * Primary key is ae_tag, secondary key is ae_id.
 */
STATIC int
xfs_acl_entry_compare(
	const void	*va,
	const void	*vb)
{
	xfs_acl_entry_t	*a = (xfs_acl_entry_t *)va,
			*b = (xfs_acl_entry_t *)vb;

	if (a->ae_tag == b->ae_tag)
		return (a->ae_id - b->ae_id);
	return (a->ae_tag - b->ae_tag);
}

/*
 * Convert from in-memory XFS to extended attribute representation.
 */
STATIC int
posix_acl_xfs_to_xattr(
	xfs_acl_t		*src,
	posix_acl_xattr_header	*dest,
	size_t			size)
{
	int			n;
	size_t			new_size = posix_acl_xattr_size(src->acl_cnt);
	posix_acl_xattr_entry	*dest_entry;
	xfs_acl_entry_t		*src_entry;

	if (size < new_size)
		return -ERANGE;

	/* Need to sort src XFS ACL by <ae_tag,ae_id> */
	xfs_sort(src->acl_entry, src->acl_cnt, sizeof(src->acl_entry[0]),
		 xfs_acl_entry_compare);

	dest->a_version = cpu_to_le32(POSIX_ACL_XATTR_VERSION);
	dest_entry = &dest->a_entries[0];
	src_entry = &src->acl_entry[0];
	for (n = 0; n < src->acl_cnt; n++, dest_entry++, src_entry++) {
		dest_entry->e_perm = cpu_to_le16(src_entry->ae_perm);
		if (_ACL_PERM_INVALID(src_entry->ae_perm))
			return -EINVAL;
		dest_entry->e_tag  = cpu_to_le16(src_entry->ae_tag);
		switch (src_entry->ae_tag) {
		case ACL_USER:
		case ACL_GROUP:
			dest_entry->e_id = cpu_to_le32(src_entry->ae_id);
				break;
		case ACL_USER_OBJ:
		case ACL_GROUP_OBJ:
		case ACL_MASK:
		case ACL_OTHER:
			dest_entry->e_id = cpu_to_le32(ACL_UNDEFINED_ID);
			break;
		default:
			return -EINVAL;
		}
	}
	return new_size;
}

int
xfs_acl_vget(
	struct inode	*vp,
	void		*acl,
	size_t		size,
	int		kind)
{
	int			error;
	xfs_acl_t		*xfs_acl = NULL;
	posix_acl_xattr_header	*ext_acl = acl;
	int			flags = 0;

	if(size) {
		if (!(_ACL_ALLOC(xfs_acl))) {
			error = ENOMEM;
			goto out;
		}
		memset(xfs_acl, 0, sizeof(xfs_acl_t));
	} else
		flags = ATTR_KERNOVAL;

	xfs_acl_get_attr(vp, xfs_acl, kind, flags, &error);
	if (error)
		goto out;

	if (!size) {
		error = -posix_acl_xattr_size(XFS_ACL_MAX_ENTRIES);
	} else {
		if (xfs_acl_invalid(xfs_acl)) {
			error = EINVAL;
			goto out;
		}
		if (kind == _ACL_TYPE_ACCESS)
			xfs_acl_sync_mode(XFS_I(vp)->i_d.di_mode, xfs_acl);
		error = -posix_acl_xfs_to_xattr(xfs_acl, ext_acl, size);
	}
out:
	if(xfs_acl)
		_ACL_FREE(xfs_acl);
	return -error;
}

int
xfs_acl_vremove(
	struct inode	*vp,
	int		kind)
{
	int		error;

	error = xfs_acl_allow_set(vp, kind);
	if (!error) {
		error = xfs_attr_remove(XFS_I(vp),
						kind == _ACL_TYPE_DEFAULT?
						SGI_ACL_DEFAULT: SGI_ACL_FILE,
						ATTR_ROOT);
		if (error == ENOATTR)
			error = 0;	/* 'scool */
	}
	return -error;
}

int
xfs_acl_vset(
	struct inode		*vp,
	void			*acl,
	size_t			size,
	int			kind)
{
	posix_acl_xattr_header	*ext_acl = acl;
	xfs_acl_t		*xfs_acl;
	int			error;
	int			basicperms = 0; /* more than std unix perms? */

	if (!acl)
		return -EINVAL;

	if (!(_ACL_ALLOC(xfs_acl)))
		return -ENOMEM;

	error = posix_acl_xattr_to_xfs(ext_acl, size, xfs_acl);
	if (error) {
		_ACL_FREE(xfs_acl);
		return -error;
	}
	if (!xfs_acl->acl_cnt) {
		_ACL_FREE(xfs_acl);
		return 0;
	}

	error = xfs_acl_allow_set(vp, kind);

	/* Incoming ACL exists, set file mode based on its value */
	if (!error && kind == _ACL_TYPE_ACCESS)
		error = xfs_acl_setmode(vp, xfs_acl, &basicperms);

	if (error)
		goto out;

	/*
	 * If we have more than std unix permissions, set up the actual attr.
	 * Otherwise, delete any existing attr.  This prevents us from
	 * having actual attrs for permissions that can be stored in the
	 * standard permission bits.
	 */
	if (!basicperms) {
		xfs_acl_set_attr(vp, xfs_acl, kind, &error);
	} else {
		error = -xfs_acl_vremove(vp, _ACL_TYPE_ACCESS);
	}

out:
	_ACL_FREE(xfs_acl);
	return -error;
}

int
xfs_acl_iaccess(
	xfs_inode_t	*ip,
	mode_t		mode,
	cred_t		*cr)
{
	xfs_acl_t	*acl;
	int		rval;
	struct xfs_name	acl_name = {SGI_ACL_FILE, SGI_ACL_FILE_SIZE};

	if (!(_ACL_ALLOC(acl)))
		return -1;

	/* If the file has no ACL return -1. */
	rval = sizeof(xfs_acl_t);
	if (xfs_attr_fetch(ip, &acl_name, (char *)acl, &rval, ATTR_ROOT)) {
		_ACL_FREE(acl);
		return -1;
	}
	xfs_acl_get_endian(acl);

	/* If the file has an empty ACL return -1. */
	if (acl->acl_cnt == XFS_ACL_NOT_PRESENT) {
		_ACL_FREE(acl);
		return -1;
	}

	/* Synchronize ACL with mode bits */
	xfs_acl_sync_mode(ip->i_d.di_mode, acl);

	rval = xfs_acl_access(ip->i_d.di_uid, ip->i_d.di_gid, acl, mode, cr);
	_ACL_FREE(acl);
	return rval;
}

STATIC int
xfs_acl_allow_set(
	struct inode	*vp,
	int		kind)
{
	if (vp->i_flags & (S_IMMUTABLE|S_APPEND))
		return EPERM;
	if (kind == _ACL_TYPE_DEFAULT && !S_ISDIR(vp->i_mode))
		return ENOTDIR;
	if (vp->i_sb->s_flags & MS_RDONLY)
		return EROFS;
	if (XFS_I(vp)->i_d.di_uid != current_fsuid() && !capable(CAP_FOWNER))
		return EPERM;
	return 0;
}

/*
 * Note: cr is only used here for the capability check if the ACL test fails.
 *       It is not used to find out the credentials uid or groups etc, as was
 *       done in IRIX. It is assumed that the uid and groups for the current
 *       thread are taken from "current" instead of the cr parameter.
 */
STATIC int
xfs_acl_access(
	uid_t		fuid,
	gid_t		fgid,
	xfs_acl_t	*fap,
	mode_t		md,
	cred_t		*cr)
{
	xfs_acl_entry_t	matched;
	int		i, allows;
	int		maskallows = -1;	/* true, but not 1, either */
	int		seen_userobj = 0;

	matched.ae_tag = 0;	/* Invalid type */
	matched.ae_perm = 0;

	for (i = 0; i < fap->acl_cnt; i++) {
		/*
		 * Break out if we've got a user_obj entry or
		 * a user entry and the mask (and have processed USER_OBJ)
		 */
		if (matched.ae_tag == ACL_USER_OBJ)
			break;
		if (matched.ae_tag == ACL_USER) {
			if (maskallows != -1 && seen_userobj)
				break;
			if (fap->acl_entry[i].ae_tag != ACL_MASK &&
			    fap->acl_entry[i].ae_tag != ACL_USER_OBJ)
				continue;
		}
		/* True if this entry allows the requested access */
		allows = ((fap->acl_entry[i].ae_perm & md) == md);

		switch (fap->acl_entry[i].ae_tag) {
		case ACL_USER_OBJ:
			seen_userobj = 1;
			if (fuid != current_fsuid())
				continue;
			matched.ae_tag = ACL_USER_OBJ;
			matched.ae_perm = allows;
			break;
		case ACL_USER:
			if (fap->acl_entry[i].ae_id != current_fsuid())
				continue;
			matched.ae_tag = ACL_USER;
			matched.ae_perm = allows;
			break;
		case ACL_GROUP_OBJ:
			if ((matched.ae_tag == ACL_GROUP_OBJ ||
			    matched.ae_tag == ACL_GROUP) && !allows)
				continue;
			if (!in_group_p(fgid))
				continue;
			matched.ae_tag = ACL_GROUP_OBJ;
			matched.ae_perm = allows;
			break;
		case ACL_GROUP:
			if ((matched.ae_tag == ACL_GROUP_OBJ ||
			    matched.ae_tag == ACL_GROUP) && !allows)
				continue;
			if (!in_group_p(fap->acl_entry[i].ae_id))
				continue;
			matched.ae_tag = ACL_GROUP;
			matched.ae_perm = allows;
			break;
		case ACL_MASK:
			maskallows = allows;
			break;
		case ACL_OTHER:
			if (matched.ae_tag != 0)
				continue;
			matched.ae_tag = ACL_OTHER;
			matched.ae_perm = allows;
			break;
		}
	}
	/*
	 * First possibility is that no matched entry allows access.
	 * The capability to override DAC may exist, so check for it.
	 */
	switch (matched.ae_tag) {
	case ACL_OTHER:
	case ACL_USER_OBJ:
		if (matched.ae_perm)
			return 0;
		break;
	case ACL_USER:
	case ACL_GROUP_OBJ:
	case ACL_GROUP:
		if (maskallows && matched.ae_perm)
			return 0;
		break;
	case 0:
		break;
	}

	/* EACCES tells generic_permission to check for capability overrides */
	return EACCES;
}

/*
 * ACL validity checker.
 *   This acl validation routine checks each ACL entry read in makes sense.
 */
STATIC int
xfs_acl_invalid(
	xfs_acl_t	*aclp)
{
	xfs_acl_entry_t	*entry, *e;
	int		user = 0, group = 0, other = 0, mask = 0;
	int		mask_required = 0;
	int		i, j;

	if (!aclp)
		goto acl_invalid;

	if (aclp->acl_cnt > XFS_ACL_MAX_ENTRIES)
		goto acl_invalid;

	for (i = 0; i < aclp->acl_cnt; i++) {
		entry = &aclp->acl_entry[i];
		switch (entry->ae_tag) {
		case ACL_USER_OBJ:
			if (user++)
				goto acl_invalid;
			break;
		case ACL_GROUP_OBJ:
			if (group++)
				goto acl_invalid;
			break;
		case ACL_OTHER:
			if (other++)
				goto acl_invalid;
			break;
		case ACL_USER:
		case ACL_GROUP:
			for (j = i + 1; j < aclp->acl_cnt; j++) {
				e = &aclp->acl_entry[j];
				if (e->ae_id == entry->ae_id &&
				    e->ae_tag == entry->ae_tag)
					goto acl_invalid;
			}
			mask_required++;
			break;
		case ACL_MASK:
			if (mask++)
				goto acl_invalid;
			break;
		default:
			goto acl_invalid;
		}
	}
	if (!user || !group || !other || (mask_required && !mask))
		goto acl_invalid;
	else
		return 0;
acl_invalid:
	return EINVAL;
}

/*
 * Do ACL endian conversion.
 */
STATIC void
xfs_acl_get_endian(
	xfs_acl_t	*aclp)
{
	xfs_acl_entry_t	*ace, *end;

	INT_SET(aclp->acl_cnt, ARCH_CONVERT, aclp->acl_cnt);
	end = &aclp->acl_entry[0]+aclp->acl_cnt;
	for (ace = &aclp->acl_entry[0]; ace < end; ace++) {
		INT_SET(ace->ae_tag, ARCH_CONVERT, ace->ae_tag);
		INT_SET(ace->ae_id, ARCH_CONVERT, ace->ae_id);
		INT_SET(ace->ae_perm, ARCH_CONVERT, ace->ae_perm);
	}
}

/*
 * Get the ACL from the EA and do endian conversion.
 */
STATIC void
xfs_acl_get_attr(
	struct inode	*vp,
	xfs_acl_t	*aclp,
	int		kind,
	int		flags,
	int		*error)
{
	int		len = sizeof(xfs_acl_t);

	ASSERT((flags & ATTR_KERNOVAL) ? (aclp == NULL) : 1);
	flags |= ATTR_ROOT;
	*error = xfs_attr_get(XFS_I(vp),
					kind == _ACL_TYPE_ACCESS ?
					SGI_ACL_FILE : SGI_ACL_DEFAULT,
					(char *)aclp, &len, flags);
	if (*error || (flags & ATTR_KERNOVAL))
		return;
	xfs_acl_get_endian(aclp);
}

/*
 * Set the EA with the ACL and do endian conversion.
 */
STATIC void
xfs_acl_set_attr(
	struct inode	*vp,
	xfs_acl_t	*aclp,
	int		kind,
	int		*error)
{
	xfs_acl_entry_t	*ace, *newace, *end;
	xfs_acl_t	*newacl;
	int		len;

	if (!(_ACL_ALLOC(newacl))) {
		*error = ENOMEM;
		return;
	}

	len = sizeof(xfs_acl_t) -
	      (sizeof(xfs_acl_entry_t) * (XFS_ACL_MAX_ENTRIES - aclp->acl_cnt));
	end = &aclp->acl_entry[0]+aclp->acl_cnt;
	for (ace = &aclp->acl_entry[0], newace = &newacl->acl_entry[0];
	     ace < end;
	     ace++, newace++) {
		INT_SET(newace->ae_tag, ARCH_CONVERT, ace->ae_tag);
		INT_SET(newace->ae_id, ARCH_CONVERT, ace->ae_id);
		INT_SET(newace->ae_perm, ARCH_CONVERT, ace->ae_perm);
	}
	INT_SET(newacl->acl_cnt, ARCH_CONVERT, aclp->acl_cnt);
	*error = xfs_attr_set(XFS_I(vp),
				kind == _ACL_TYPE_ACCESS ?
				SGI_ACL_FILE: SGI_ACL_DEFAULT,
				(char *)newacl, len, ATTR_ROOT);
	_ACL_FREE(newacl);
}

int
xfs_acl_vtoacl(
	struct inode	*vp,
	xfs_acl_t	*access_acl,
	xfs_acl_t	*default_acl)
{
	int		error = 0;

	if (access_acl) {
		/*
		 * Get the Access ACL and the mode.  If either cannot
		 * be obtained for some reason, invalidate the access ACL.
		 */
		xfs_acl_get_attr(vp, access_acl, _ACL_TYPE_ACCESS, 0, &error);
		if (error)
			access_acl->acl_cnt = XFS_ACL_NOT_PRESENT;
		else /* We have a good ACL and the file mode, synchronize. */
			xfs_acl_sync_mode(XFS_I(vp)->i_d.di_mode, access_acl);
	}

	if (default_acl) {
		xfs_acl_get_attr(vp, default_acl, _ACL_TYPE_DEFAULT, 0, &error);
		if (error)
			default_acl->acl_cnt = XFS_ACL_NOT_PRESENT;
	}
	return error;
}

/*
 * This function retrieves the parent directory's acl, processes it
 * and lets the child inherit the acl(s) that it should.
 */
int
xfs_acl_inherit(
	struct inode	*vp,
	mode_t		mode,
	xfs_acl_t	*pdaclp)
{
	xfs_acl_t	*cacl;
	int		error = 0;
	int		basicperms = 0;

	/*
	 * If the parent does not have a default ACL, or it's an
	 * invalid ACL, we're done.
	 */
	if (!vp)
		return 0;
	if (!pdaclp || xfs_acl_invalid(pdaclp))
		return 0;

	/*
	 * Copy the default ACL of the containing directory to
	 * the access ACL of the new file and use the mode that
	 * was passed in to set up the correct initial values for
	 * the u::,g::[m::], and o:: entries.  This is what makes
	 * umask() "work" with ACL's.
	 */

	if (!(_ACL_ALLOC(cacl)))
		return ENOMEM;

	memcpy(cacl, pdaclp, sizeof(xfs_acl_t));
	xfs_acl_filter_mode(mode, cacl);
	error = xfs_acl_setmode(vp, cacl, &basicperms);
	if (error)
		goto out_error;

	/*
	 * Set the Default and Access ACL on the file.  The mode is already
	 * set on the file, so we don't need to worry about that.
	 *
	 * If the new file is a directory, its default ACL is a copy of
	 * the containing directory's default ACL.
	 */
	if (S_ISDIR(vp->i_mode))
		xfs_acl_set_attr(vp, pdaclp, _ACL_TYPE_DEFAULT, &error);
	if (!error && !basicperms)
		xfs_acl_set_attr(vp, cacl, _ACL_TYPE_ACCESS, &error);
out_error:
	_ACL_FREE(cacl);
	return error;
}

/*
 * Set up the correct mode on the file based on the supplied ACL.  This
 * makes sure that the mode on the file reflects the state of the
 * u::,g::[m::], and o:: entries in the ACL.  Since the mode is where
 * the ACL is going to get the permissions for these entries, we must
 * synchronize the mode whenever we set the ACL on a file.
 */
STATIC int
xfs_acl_setmode(
	struct inode	*vp,
	xfs_acl_t	*acl,
	int		*basicperms)
{
	struct iattr	iattr;
	xfs_acl_entry_t	*ap;
	xfs_acl_entry_t	*gap = NULL;
	int		i, nomask = 1;

	*basicperms = 1;

	if (acl->acl_cnt == XFS_ACL_NOT_PRESENT)
		return 0;

	/*
	 * Copy the u::, g::, o::, and m:: bits from the ACL into the
	 * mode.  The m:: bits take precedence over the g:: bits.
	 */
	iattr.ia_valid = ATTR_MODE;
	iattr.ia_mode = XFS_I(vp)->i_d.di_mode;
	iattr.ia_mode &= ~(S_IRWXU|S_IRWXG|S_IRWXO);
	ap = acl->acl_entry;
	for (i = 0; i < acl->acl_cnt; ++i) {
		switch (ap->ae_tag) {
		case ACL_USER_OBJ:
			iattr.ia_mode |= ap->ae_perm << 6;
			break;
		case ACL_GROUP_OBJ:
			gap = ap;
			break;
		case ACL_MASK:	/* more than just standard modes */
			nomask = 0;
			iattr.ia_mode |= ap->ae_perm << 3;
			*basicperms = 0;
			break;
		case ACL_OTHER:
			iattr.ia_mode |= ap->ae_perm;
			break;
		default:	/* more than just standard modes */
			*basicperms = 0;
			break;
		}
		ap++;
	}

	/* Set the group bits from ACL_GROUP_OBJ if there's no ACL_MASK */
	if (gap && nomask)
		iattr.ia_mode |= gap->ae_perm << 3;

	return xfs_setattr(XFS_I(vp), &iattr, 0);
}

/*
 * The permissions for the special ACL entries (u::, g::[m::], o::) are
 * actually stored in the file mode (if there is both a group and a mask,
 * the group is stored in the ACL entry and the mask is stored on the file).
 * This allows the mode to remain automatically in sync with the ACL without
 * the need for a call-back to the ACL system at every point where the mode
 * could change.  This function takes the permissions from the specified mode
 * and places it in the supplied ACL.
 *
 * This implementation draws its validity from the fact that, when the ACL
 * was assigned, the mode was copied from the ACL.
 * If the mode did not change, therefore, the mode remains exactly what was
 * taken from the special ACL entries at assignment.
 * If a subsequent chmod() was done, the POSIX spec says that the change in
 * mode must cause an update to the ACL seen at user level and used for
 * access checks.  Before and after a mode change, therefore, the file mode
 * most accurately reflects what the special ACL entries should permit/deny.
 *
 * CAVEAT: If someone sets the SGI_ACL_FILE attribute directly,
 *         the existing mode bits will override whatever is in the
 *         ACL. Similarly, if there is a pre-existing ACL that was
 *         never in sync with its mode (owing to a bug in 6.5 and
 *         before), it will now magically (or mystically) be
 *         synchronized.  This could cause slight astonishment, but
 *         it is better than inconsistent permissions.
 *
 * The supplied ACL is a template that may contain any combination
 * of special entries.  These are treated as place holders when we fill
 * out the ACL.  This routine does not add or remove special entries, it
 * simply unites each special entry with its associated set of permissions.
 */
STATIC void
xfs_acl_sync_mode(
	mode_t		mode,
	xfs_acl_t	*acl)
{
	int		i, nomask = 1;
	xfs_acl_entry_t	*ap;
	xfs_acl_entry_t	*gap = NULL;

	/*
	 * Set ACL entries. POSIX1003.1eD16 requires that the MASK
	 * be set instead of the GROUP entry, if there is a MASK.
	 */
	for (ap = acl->acl_entry, i = 0; i < acl->acl_cnt; ap++, i++) {
		switch (ap->ae_tag) {
		case ACL_USER_OBJ:
			ap->ae_perm = (mode >> 6) & 0x7;
			break;
		case ACL_GROUP_OBJ:
			gap = ap;
			break;
		case ACL_MASK:
			nomask = 0;
			ap->ae_perm = (mode >> 3) & 0x7;
			break;
		case ACL_OTHER:
			ap->ae_perm = mode & 0x7;
			break;
		default:
			break;
		}
	}
	/* Set the ACL_GROUP_OBJ if there's no ACL_MASK */
	if (gap && nomask)
		gap->ae_perm = (mode >> 3) & 0x7;
}

/*
 * When inheriting an Access ACL from a directory Default ACL,
 * the ACL bits are set to the intersection of the ACL default
 * permission bits and the file permission bits in mode. If there
 * are no permission bits on the file then we must not give them
 * the ACL. This is what what makes umask() work with ACLs.
 */
STATIC void
xfs_acl_filter_mode(
	mode_t		mode,
	xfs_acl_t	*acl)
{
	int		i, nomask = 1;
	xfs_acl_entry_t	*ap;
	xfs_acl_entry_t	*gap = NULL;

	/*
	 * Set ACL entries. POSIX1003.1eD16 requires that the MASK
	 * be merged with GROUP entry, if there is a MASK.
	 */
	for (ap = acl->acl_entry, i = 0; i < acl->acl_cnt; ap++, i++) {
		switch (ap->ae_tag) {
		case ACL_USER_OBJ:
			ap->ae_perm &= (mode >> 6) & 0x7;
			break;
		case ACL_GROUP_OBJ:
			gap = ap;
			break;
		case ACL_MASK:
			nomask = 0;
			ap->ae_perm &= (mode >> 3) & 0x7;
			break;
		case ACL_OTHER:
			ap->ae_perm &= mode & 0x7;
			break;
		default:
			break;
		}
	}
	/* Set the ACL_GROUP_OBJ if there's no ACL_MASK */
	if (gap && nomask)
		gap->ae_perm &= (mode >> 3) & 0x7;
}
