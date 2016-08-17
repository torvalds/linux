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

#ifndef	_SYS_FS_ZFS_FUID_H
#define	_SYS_FS_ZFS_FUID_H

#ifdef _KERNEL
#include <sys/kidmap.h>
#include <sys/sid.h>
#include <sys/dmu.h>
#include <sys/zfs_vfsops.h>
#endif
#include <sys/avl.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef enum {
	ZFS_OWNER,
	ZFS_GROUP,
	ZFS_ACE_USER,
	ZFS_ACE_GROUP
} zfs_fuid_type_t;

/*
 * Estimate space needed for one more fuid table entry.
 * for now assume its current size + 1K
 */
#define	FUID_SIZE_ESTIMATE(z) ((z)->z_fuid_size + (SPA_MINBLOCKSIZE << 1))

#define	FUID_INDEX(x)	((x) >> 32)
#define	FUID_RID(x)	((x) & 0xffffffff)
#define	FUID_ENCODE(idx, rid) (((uint64_t)(idx) << 32) | (rid))
/*
 * FUIDs cause problems for the intent log
 * we need to replay the creation of the FUID,
 * but we can't count on the idmapper to be around
 * and during replay the FUID index may be different than
 * before.  Also, if an ACL has 100 ACEs and 12 different
 * domains we don't want to log 100 domain strings, but rather
 * just the unique 12.
 */

/*
 * The FUIDs in the log will index into
 * domain string table and the bottom half will be the rid.
 * Used for mapping ephemeral uid/gid during ACL setting to FUIDs
 */
typedef struct zfs_fuid {
	list_node_t 	z_next;
	uint64_t 	z_id;		/* uid/gid being converted to fuid */
	uint64_t	z_domidx;	/* index in AVL domain table */
	uint64_t	z_logfuid;	/* index for domain in log */
} zfs_fuid_t;

/* list of unique domains */
typedef struct zfs_fuid_domain {
	list_node_t	z_next;
	uint64_t	z_domidx;	/* AVL tree idx */
	const char	*z_domain;	/* domain string */
} zfs_fuid_domain_t;

/*
 * FUID information necessary for logging create, setattr, and setacl.
 */
typedef struct zfs_fuid_info {
	list_t	z_fuids;
	list_t	z_domains;
	uint64_t z_fuid_owner;
	uint64_t z_fuid_group;
	char **z_domain_table;  /* Used during replay */
	uint32_t z_fuid_cnt;	/* How many fuids in z_fuids */
	uint32_t z_domain_cnt;	/* How many domains */
	size_t	z_domain_str_sz; /* len of domain strings z_domain list */
} zfs_fuid_info_t;

#ifdef _KERNEL
struct znode;
extern uid_t zfs_fuid_map_id(zfsvfs_t *, uint64_t, cred_t *, zfs_fuid_type_t);
extern void zfs_fuid_node_add(zfs_fuid_info_t **, const char *, uint32_t,
    uint64_t, uint64_t, zfs_fuid_type_t);
extern void zfs_fuid_destroy(zfsvfs_t *);
extern uint64_t zfs_fuid_create_cred(zfsvfs_t *, zfs_fuid_type_t,
    cred_t *, zfs_fuid_info_t **);
extern uint64_t zfs_fuid_create(zfsvfs_t *, uint64_t, cred_t *, zfs_fuid_type_t,
    zfs_fuid_info_t **);
extern void zfs_fuid_map_ids(struct znode *zp, cred_t *cr,
    uid_t *uid, uid_t *gid);
extern zfs_fuid_info_t *zfs_fuid_info_alloc(void);
extern void zfs_fuid_info_free(zfs_fuid_info_t *);
extern boolean_t zfs_groupmember(zfsvfs_t *, uint64_t, cred_t *);
void zfs_fuid_sync(zfsvfs_t *, dmu_tx_t *);
extern int zfs_fuid_find_by_domain(zfsvfs_t *, const char *domain,
    char **retdomain, boolean_t addok);
extern const char *zfs_fuid_find_by_idx(zfsvfs_t *zfsvfs, uint32_t idx);
extern void zfs_fuid_txhold(zfsvfs_t *zfsvfs, dmu_tx_t *tx);
#endif

char *zfs_fuid_idx_domain(avl_tree_t *, uint32_t);
void zfs_fuid_avl_tree_create(avl_tree_t *, avl_tree_t *);
uint64_t zfs_fuid_table_load(objset_t *, uint64_t, avl_tree_t *, avl_tree_t *);
void zfs_fuid_table_destroy(avl_tree_t *, avl_tree_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_ZFS_FUID_H */
