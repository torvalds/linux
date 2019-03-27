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

#ifndef	_SYS_ZFS_SA_H
#define	_SYS_ZFS_SA_H

#ifdef _KERNEL
#include <sys/list.h>
#include <sys/dmu.h>
#include <sys/zfs_acl.h>
#include <sys/zfs_znode.h>
#include <sys/sa.h>
#include <sys/zil.h>


#endif

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * This is the list of known attributes
 * to the ZPL.  The values of the actual
 * attributes are not defined by the order
 * the enums.  It is controlled by the attribute
 * registration mechanism.  Two different file system
 * could have different numeric values for the same
 * attributes.  this list is only used for dereferencing
 * into the table that will hold the actual numeric value.
 */
typedef enum zpl_attr {
	ZPL_ATIME,
	ZPL_MTIME,
	ZPL_CTIME,
	ZPL_CRTIME,
	ZPL_GEN,
	ZPL_MODE,
	ZPL_SIZE,
	ZPL_PARENT,
	ZPL_LINKS,
	ZPL_XATTR,
	ZPL_RDEV,
	ZPL_FLAGS,
	ZPL_UID,
	ZPL_GID,
	ZPL_PAD,
	ZPL_ZNODE_ACL,
	ZPL_DACL_COUNT,
	ZPL_SYMLINK,
	ZPL_SCANSTAMP,
	ZPL_DACL_ACES,
	ZPL_END
} zpl_attr_t;

#define	ZFS_OLD_ZNODE_PHYS_SIZE	0x108
#define	ZFS_SA_BASE_ATTR_SIZE	(ZFS_OLD_ZNODE_PHYS_SIZE - \
    sizeof (zfs_acl_phys_t))

#define	SA_MODE_OFFSET		0
#define	SA_SIZE_OFFSET		8
#define	SA_GEN_OFFSET		16
#define	SA_UID_OFFSET		24
#define	SA_GID_OFFSET		32
#define	SA_PARENT_OFFSET	40

extern sa_attr_reg_t zfs_attr_table[ZPL_END + 1];
extern sa_attr_reg_t zfs_legacy_attr_table[ZPL_END + 1];

/*
 * This is a deprecated data structure that only exists for
 * dealing with file systems create prior to ZPL version 5.
 */
typedef struct znode_phys {
	uint64_t zp_atime[2];		/*  0 - last file access time */
	uint64_t zp_mtime[2];		/* 16 - last file modification time */
	uint64_t zp_ctime[2];		/* 32 - last file change time */
	uint64_t zp_crtime[2];		/* 48 - creation time */
	uint64_t zp_gen;		/* 64 - generation (txg of creation) */
	uint64_t zp_mode;		/* 72 - file mode bits */
	uint64_t zp_size;		/* 80 - size of file */
	uint64_t zp_parent;		/* 88 - directory parent (`..') */
	uint64_t zp_links;		/* 96 - number of links to file */
	uint64_t zp_xattr;		/* 104 - DMU object for xattrs */
	uint64_t zp_rdev;		/* 112 - dev_t for VBLK & VCHR files */
	uint64_t zp_flags;		/* 120 - persistent flags */
	uint64_t zp_uid;		/* 128 - file owner */
	uint64_t zp_gid;		/* 136 - owning group */
	uint64_t zp_zap;		/* 144 - extra attributes */
	uint64_t zp_pad[3];		/* 152 - future */
	zfs_acl_phys_t zp_acl;		/* 176 - 263 ACL */
	/*
	 * Data may pad out any remaining bytes in the znode buffer, eg:
	 *
	 * |<---------------------- dnode_phys (512) ------------------------>|
	 * |<-- dnode (192) --->|<----------- "bonus" buffer (320) ---------->|
	 *			|<---- znode (264) ---->|<---- data (56) ---->|
	 *
	 * At present, we use this space for the following:
	 *  - symbolic links
	 *  - 32-byte anti-virus scanstamp (regular files only)
	 */
} znode_phys_t;

#ifdef _KERNEL
int zfs_sa_readlink(struct znode *, uio_t *);
void zfs_sa_symlink(struct znode *, char *link, int len, dmu_tx_t *);
void zfs_sa_upgrade(struct sa_handle  *, dmu_tx_t *);
void zfs_sa_get_scanstamp(struct znode *, xvattr_t *);
void zfs_sa_set_scanstamp(struct znode *, xvattr_t *, dmu_tx_t *);
void zfs_sa_uprade_pre(struct sa_handle *, void *, dmu_tx_t *);
void zfs_sa_upgrade_post(struct sa_handle *, void *, dmu_tx_t *);
void zfs_sa_upgrade_txholds(dmu_tx_t *, struct znode *);
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZFS_SA_H */
