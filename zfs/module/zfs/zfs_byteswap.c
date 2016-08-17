/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/zfs_context.h>
#include <sys/vfs.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_sa.h>
#include <sys/zfs_acl.h>

void
zfs_oldace_byteswap(ace_t *ace, int ace_cnt)
{
	int i;

	for (i = 0; i != ace_cnt; i++, ace++) {
		ace->a_who = BSWAP_32(ace->a_who);
		ace->a_access_mask = BSWAP_32(ace->a_access_mask);
		ace->a_flags = BSWAP_16(ace->a_flags);
		ace->a_type = BSWAP_16(ace->a_type);
	}
}

/*
 * swap ace_t and ace_oject_t
 */
void
zfs_ace_byteswap(void *buf, size_t size, boolean_t zfs_layout)
{
	caddr_t end;
	caddr_t ptr;
	zfs_ace_t *zacep = NULL;
	ace_t *acep;
	uint16_t entry_type;
	size_t entry_size;
	int ace_type;

	end = (caddr_t)buf + size;
	ptr = buf;

	while (ptr < end) {
		if (zfs_layout) {
			/*
			 * Avoid overrun.  Embedded aces can have one
			 * of several sizes.  We don't know exactly
			 * how many our present, only the size of the
			 * buffer containing them.  That size may be
			 * larger than needed to hold the aces
			 * present.  As long as we do not do any
			 * swapping beyond the end of our block we are
			 * okay.  It it safe to swap any non-ace data
			 * within the block since it is just zeros.
			 */
			if (ptr + sizeof (zfs_ace_hdr_t) > end) {
				break;
			}
			zacep = (zfs_ace_t *)ptr;
			zacep->z_hdr.z_access_mask =
			    BSWAP_32(zacep->z_hdr.z_access_mask);
			zacep->z_hdr.z_flags = BSWAP_16(zacep->z_hdr.z_flags);
			ace_type = zacep->z_hdr.z_type =
			    BSWAP_16(zacep->z_hdr.z_type);
			entry_type = zacep->z_hdr.z_flags & ACE_TYPE_FLAGS;
		} else {
			/* Overrun avoidance */
			if (ptr + sizeof (ace_t) > end) {
				break;
			}
			acep = (ace_t *)ptr;
			acep->a_access_mask = BSWAP_32(acep->a_access_mask);
			acep->a_flags = BSWAP_16(acep->a_flags);
			ace_type = acep->a_type = BSWAP_16(acep->a_type);
			acep->a_who = BSWAP_32(acep->a_who);
			entry_type = acep->a_flags & ACE_TYPE_FLAGS;
		}
		switch (entry_type) {
		case ACE_OWNER:
		case ACE_EVERYONE:
		case (ACE_IDENTIFIER_GROUP | ACE_GROUP):
			entry_size = zfs_layout ?
			    sizeof (zfs_ace_hdr_t) : sizeof (ace_t);
			break;
		case ACE_IDENTIFIER_GROUP:
		default:
			/* Overrun avoidance */
			if (zfs_layout) {
				if (ptr + sizeof (zfs_ace_t) <= end) {
					zacep->z_fuid = BSWAP_64(zacep->z_fuid);
				} else {
					entry_size = sizeof (zfs_ace_t);
					break;
				}
			}
			switch (ace_type) {
			case ACE_ACCESS_ALLOWED_OBJECT_ACE_TYPE:
			case ACE_ACCESS_DENIED_OBJECT_ACE_TYPE:
			case ACE_SYSTEM_AUDIT_OBJECT_ACE_TYPE:
			case ACE_SYSTEM_ALARM_OBJECT_ACE_TYPE:
				entry_size = zfs_layout ?
				    sizeof (zfs_object_ace_t) :
				    sizeof (ace_object_t);
				break;
			default:
				entry_size = zfs_layout ? sizeof (zfs_ace_t) :
				    sizeof (ace_t);
				break;
			}
		}
		ptr = ptr + entry_size;
	}
}

/* ARGSUSED */
void
zfs_oldacl_byteswap(void *buf, size_t size)
{
	int cnt;

	/*
	 * Arggh, since we don't know how many ACEs are in
	 * the array, we have to swap the entire block
	 */

	cnt = size / sizeof (ace_t);

	zfs_oldace_byteswap((ace_t *)buf, cnt);
}

/* ARGSUSED */
void
zfs_acl_byteswap(void *buf, size_t size)
{
	zfs_ace_byteswap(buf, size, B_TRUE);
}

void
zfs_znode_byteswap(void *buf, size_t size)
{
	znode_phys_t *zp = buf;

	ASSERT(size >= sizeof (znode_phys_t));

	zp->zp_crtime[0] = BSWAP_64(zp->zp_crtime[0]);
	zp->zp_crtime[1] = BSWAP_64(zp->zp_crtime[1]);
	zp->zp_atime[0] = BSWAP_64(zp->zp_atime[0]);
	zp->zp_atime[1] = BSWAP_64(zp->zp_atime[1]);
	zp->zp_mtime[0] = BSWAP_64(zp->zp_mtime[0]);
	zp->zp_mtime[1] = BSWAP_64(zp->zp_mtime[1]);
	zp->zp_ctime[0] = BSWAP_64(zp->zp_ctime[0]);
	zp->zp_ctime[1] = BSWAP_64(zp->zp_ctime[1]);
	zp->zp_gen = BSWAP_64(zp->zp_gen);
	zp->zp_mode = BSWAP_64(zp->zp_mode);
	zp->zp_size = BSWAP_64(zp->zp_size);
	zp->zp_parent = BSWAP_64(zp->zp_parent);
	zp->zp_links = BSWAP_64(zp->zp_links);
	zp->zp_xattr = BSWAP_64(zp->zp_xattr);
	zp->zp_rdev = BSWAP_64(zp->zp_rdev);
	zp->zp_flags = BSWAP_64(zp->zp_flags);
	zp->zp_uid = BSWAP_64(zp->zp_uid);
	zp->zp_gid = BSWAP_64(zp->zp_gid);
	zp->zp_zap = BSWAP_64(zp->zp_zap);
	zp->zp_pad[0] = BSWAP_64(zp->zp_pad[0]);
	zp->zp_pad[1] = BSWAP_64(zp->zp_pad[1]);
	zp->zp_pad[2] = BSWAP_64(zp->zp_pad[2]);

	zp->zp_acl.z_acl_extern_obj = BSWAP_64(zp->zp_acl.z_acl_extern_obj);
	zp->zp_acl.z_acl_size = BSWAP_32(zp->zp_acl.z_acl_size);
	zp->zp_acl.z_acl_version = BSWAP_16(zp->zp_acl.z_acl_version);
	zp->zp_acl.z_acl_count = BSWAP_16(zp->zp_acl.z_acl_count);
	if (zp->zp_acl.z_acl_version == ZFS_ACL_VERSION) {
		zfs_acl_byteswap((void *)&zp->zp_acl.z_ace_data[0],
		    ZFS_ACE_SPACE);
	} else {
		zfs_oldace_byteswap((ace_t *)&zp->zp_acl.z_ace_data[0],
		    ACE_SLOT_CNT);
	}
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(zfs_oldacl_byteswap);
EXPORT_SYMBOL(zfs_acl_byteswap);
EXPORT_SYMBOL(zfs_znode_byteswap);
#endif
