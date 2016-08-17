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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_FS_ZFS_RLOCK_H
#define	_SYS_FS_ZFS_RLOCK_H

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _KERNEL

#include <sys/list.h>
#include <sys/avl.h>
#include <sys/condvar.h>

typedef enum {
	RL_READER,
	RL_WRITER,
	RL_APPEND
} rl_type_t;

typedef struct zfs_rlock {
	kmutex_t zr_mutex;	/* protects changes to zr_avl */
	avl_tree_t zr_avl;	/* avl tree of range locks */
	uint64_t *zr_size;	/* points to znode->z_size */
	uint_t *zr_blksz;	/* points to znode->z_blksz */
	uint64_t *zr_max_blksz; /* points to zsb->z_max_blksz */
} zfs_rlock_t;

typedef struct rl {
	zfs_rlock_t *r_zrl;
	avl_node_t r_node;	/* avl node link */
	uint64_t r_off;		/* file range offset */
	uint64_t r_len;		/* file range length */
	uint_t r_cnt;		/* range reference count in tree */
	rl_type_t r_type;	/* range type */
	kcondvar_t r_wr_cv;	/* cv for waiting writers */
	kcondvar_t r_rd_cv;	/* cv for waiting readers */
	uint8_t r_proxy;	/* acting for original range */
	uint8_t r_write_wanted;	/* writer wants to lock this range */
	uint8_t r_read_wanted;	/* reader wants to lock this range */
	list_node_t rl_node;	/* used for deferred release */
} rl_t;

/*
 * Lock a range (offset, length) as either shared (RL_READER)
 * or exclusive (RL_WRITER or RL_APPEND).  RL_APPEND is a special type that
 * is converted to RL_WRITER that specified to lock from the start of the
 * end of file.  Returns the range lock structure.
 */
rl_t *zfs_range_lock(zfs_rlock_t *zrl, uint64_t off, uint64_t len,
    rl_type_t type);

/* Unlock range and destroy range lock structure. */
void zfs_range_unlock(rl_t *rl);

/*
 * Reduce range locked as RW_WRITER from whole file to specified range.
 * Asserts the whole file was previously locked.
 */
void zfs_range_reduce(rl_t *rl, uint64_t off, uint64_t len);

/*
 * AVL comparison function used to order range locks
 * Locks are ordered on the start offset of the range.
 */
int zfs_range_compare(const void *arg1, const void *arg2);

static inline void
zfs_rlock_init(zfs_rlock_t *zrl)
{
	mutex_init(&zrl->zr_mutex, NULL, MUTEX_DEFAULT, NULL);
	avl_create(&zrl->zr_avl, zfs_range_compare,
	    sizeof (rl_t), offsetof(rl_t, r_node));
	zrl->zr_size = NULL;
	zrl->zr_blksz = NULL;
	zrl->zr_max_blksz = NULL;
}

static inline void
zfs_rlock_destroy(zfs_rlock_t *zrl)
{
	avl_destroy(&zrl->zr_avl);
	mutex_destroy(&zrl->zr_mutex);
}
#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_ZFS_RLOCK_H */
