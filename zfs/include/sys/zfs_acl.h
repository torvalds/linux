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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_FS_ZFS_ACL_H
#define	_SYS_FS_ZFS_ACL_H

#ifdef _KERNEL
#include <sys/isa_defs.h>
#include <sys/types32.h>
#include <sys/xvattr.h>
#endif
#include <sys/acl.h>
#include <sys/dmu.h>
#include <sys/zfs_fuid.h>
#include <sys/sa.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct znode_phys;

#define	ACE_SLOT_CNT	6
#define	ZFS_ACL_VERSION_INITIAL 0ULL
#define	ZFS_ACL_VERSION_FUID	1ULL
#define	ZFS_ACL_VERSION		ZFS_ACL_VERSION_FUID

/*
 * ZFS ACLs (Access Control Lists) are stored in various forms.
 *
 * Files created with ACL version ZFS_ACL_VERSION_INITIAL
 * will all be created with fixed length ACEs of type
 * zfs_oldace_t.
 *
 * Files with ACL version ZFS_ACL_VERSION_FUID will be created
 * with various sized ACEs.  The abstraction entries will utilize
 * zfs_ace_hdr_t, normal user/group entries will use zfs_ace_t
 * and some specialized CIFS ACEs will use zfs_object_ace_t.
 */

/*
 * All ACEs have a common hdr.  For
 * owner@, group@, and everyone@ this is all
 * thats needed.
 */
typedef struct zfs_ace_hdr {
	uint16_t z_type;
	uint16_t z_flags;
	uint32_t z_access_mask;
} zfs_ace_hdr_t;

typedef zfs_ace_hdr_t zfs_ace_abstract_t;

/*
 * Standard ACE
 */
typedef struct zfs_ace {
	zfs_ace_hdr_t	z_hdr;
	uint64_t	z_fuid;
} zfs_ace_t;

/*
 * The following type only applies to ACE_ACCESS_ALLOWED|DENIED_OBJECT_ACE_TYPE
 * and will only be set/retrieved in a CIFS context.
 */

typedef struct zfs_object_ace {
	zfs_ace_t	z_ace;
	uint8_t		z_object_type[16]; /* object type */
	uint8_t		z_inherit_type[16]; /* inherited object type */
} zfs_object_ace_t;

typedef struct zfs_oldace {
	uint32_t	z_fuid;		/* "who" */
	uint32_t	z_access_mask;  /* access mask */
	uint16_t	z_flags;	/* flags, i.e inheritance */
	uint16_t	z_type;		/* type of entry allow/deny */
} zfs_oldace_t;

typedef struct zfs_acl_phys_v0 {
	uint64_t	z_acl_extern_obj;	/* ext acl pieces */
	uint32_t	z_acl_count;		/* Number of ACEs */
	uint16_t	z_acl_version;		/* acl version */
	uint16_t	z_acl_pad;		/* pad */
	zfs_oldace_t	z_ace_data[ACE_SLOT_CNT]; /* 6 standard ACEs */
} zfs_acl_phys_v0_t;

#define	ZFS_ACE_SPACE	(sizeof (zfs_oldace_t) * ACE_SLOT_CNT)

/*
 * Size of ACL count is always 2 bytes.
 * Necessary to for dealing with both V0 ACL and V1 ACL layout
 */
#define	ZFS_ACL_COUNT_SIZE	(sizeof (uint16_t))

typedef struct zfs_acl_phys {
	uint64_t	z_acl_extern_obj;	  /* ext acl pieces */
	uint32_t	z_acl_size;		  /* Number of bytes in ACL */
	uint16_t	z_acl_version;		  /* acl version */
	uint16_t	z_acl_count;		  /* ace count */
	uint8_t	z_ace_data[ZFS_ACE_SPACE]; /* space for embedded ACEs */
} zfs_acl_phys_t;

typedef struct acl_ops {
	uint32_t	(*ace_mask_get) (void *acep); /* get  access mask */
	void 		(*ace_mask_set) (void *acep,
			    uint32_t mask); /* set access mask */
	uint16_t	(*ace_flags_get) (void *acep);	/* get flags */
	void		(*ace_flags_set) (void *acep,
			    uint16_t flags); /* set flags */
	uint16_t	(*ace_type_get)(void *acep); /* get type */
	void		(*ace_type_set)(void *acep,
			    uint16_t type); /* set type */
	uint64_t	(*ace_who_get)(void *acep); /* get who/fuid */
	void		(*ace_who_set)(void *acep,
			    uint64_t who); /* set who/fuid */
	size_t		(*ace_size)(void *acep); /* how big is this ace */
	size_t		(*ace_abstract_size)(void); /* sizeof abstract entry */
	int		(*ace_mask_off)(void); /* off of access mask in ace */
	/* ptr to data if any */
	int		(*ace_data)(void *acep, void **datap);
} acl_ops_t;

/*
 * A zfs_acl_t structure is composed of a list of zfs_acl_node_t's.
 * Each node will have one or more ACEs associated with it.  You will
 * only have multiple nodes during a chmod operation.   Normally only
 * one node is required.
 */
typedef struct zfs_acl_node {
	list_node_t	z_next;		/* Next chunk of ACEs */
	void		*z_acldata;	/* pointer into actual ACE(s) */
	void		*z_allocdata;	/* pointer to kmem allocated memory */
	size_t		z_allocsize;	/* Size of blob in bytes */
	size_t		z_size;		/* length of ACL data */
	uint64_t	z_ace_count;	/* number of ACEs in this acl node */
	int		z_ace_idx;	/* ace iterator positioned on */
} zfs_acl_node_t;

typedef struct zfs_acl {
	uint64_t	z_acl_count;	/* Number of ACEs */
	size_t		z_acl_bytes;	/* Number of bytes in ACL */
	uint_t		z_version;	/* version of ACL */
	void		*z_next_ace;	/* pointer to next ACE */
	uint64_t	z_hints;	/* ACL hints (ZFS_INHERIT_ACE ...) */
	zfs_acl_node_t	*z_curr_node;	/* current node iterator is handling */
	list_t		z_acl;		/* chunks of ACE data */
	acl_ops_t	*z_ops;		/* ACL operations */
} zfs_acl_t;

typedef struct acl_locator_cb {
	zfs_acl_t *cb_aclp;
	zfs_acl_node_t *cb_acl_node;
} zfs_acl_locator_cb_t;

#define	ACL_DATA_ALLOCED	0x1
#define	ZFS_ACL_SIZE(aclcnt)	(sizeof (ace_t) * (aclcnt))

struct zfs_fuid_info;

typedef struct zfs_acl_ids {
	uint64_t		z_fuid;		/* file owner fuid */
	uint64_t		z_fgid;		/* file group owner fuid */
	uint64_t		z_mode;		/* mode to set on create */
	zfs_acl_t		*z_aclp;	/* ACL to create with file */
	struct zfs_fuid_info 	*z_fuidp;	/* for tracking fuids for log */
} zfs_acl_ids_t;

/*
 * Property values for acl_mode and acl_inherit.
 *
 * acl_mode can take discard, noallow, groupmask and passthrough.
 * whereas acl_inherit has secure instead of groupmask.
 */

#define	ZFS_ACL_DISCARD		0
#define	ZFS_ACL_NOALLOW		1
#define	ZFS_ACL_GROUPMASK	2
#define	ZFS_ACL_PASSTHROUGH	3
#define	ZFS_ACL_RESTRICTED	4
#define	ZFS_ACL_PASSTHROUGH_X	5

struct znode;
struct zfs_sb;

#ifdef _KERNEL
int zfs_acl_ids_create(struct znode *, int, vattr_t *,
    cred_t *, vsecattr_t *, zfs_acl_ids_t *);
void zfs_acl_ids_free(zfs_acl_ids_t *);
boolean_t zfs_acl_ids_overquota(struct zfs_sb *, zfs_acl_ids_t *);
int zfs_getacl(struct znode *, vsecattr_t *, boolean_t, cred_t *);
int zfs_setacl(struct znode *, vsecattr_t *, boolean_t, cred_t *);
void zfs_acl_rele(void *);
void zfs_oldace_byteswap(ace_t *, int);
void zfs_ace_byteswap(void *, size_t, boolean_t);
extern boolean_t zfs_has_access(struct znode *zp, cred_t *cr);
extern int zfs_zaccess(struct znode *, int, int, boolean_t, cred_t *);
int zfs_fastaccesschk_execute(struct znode *, cred_t *);
extern int zfs_zaccess_rwx(struct znode *, mode_t, int, cred_t *);
extern int zfs_zaccess_unix(struct znode *, mode_t, cred_t *);
extern int zfs_acl_access(struct znode *, int, cred_t *);
void zfs_acl_chmod_setattr(struct znode *, zfs_acl_t **, uint64_t);
int zfs_zaccess_delete(struct znode *, struct znode *, cred_t *);
int zfs_zaccess_rename(struct znode *, struct znode *,
    struct znode *, struct znode *, cred_t *cr);
void zfs_acl_free(zfs_acl_t *);
int zfs_vsec_2_aclp(struct zfs_sb *, umode_t, vsecattr_t *, cred_t *,
    struct zfs_fuid_info **, zfs_acl_t **);
int zfs_aclset_common(struct znode *, zfs_acl_t *, cred_t *, dmu_tx_t *);
uint64_t zfs_external_acl(struct znode *);
int zfs_znode_acl_version(struct znode *);
int zfs_acl_size(struct znode *, int *);
zfs_acl_t *zfs_acl_alloc(int);
zfs_acl_node_t *zfs_acl_node_alloc(size_t);
void zfs_acl_xform(struct znode *, zfs_acl_t *, cred_t *);
void zfs_acl_data_locator(void **, uint32_t *, uint32_t, boolean_t, void *);
uint64_t zfs_mode_compute(uint64_t, zfs_acl_t *,
    uint64_t *, uint64_t, uint64_t);
int zfs_acl_chown_setattr(struct znode *);

#endif

#ifdef	__cplusplus
}
#endif
#endif	/* _SYS_FS_ZFS_ACL_H */
