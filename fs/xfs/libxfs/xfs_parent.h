// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022-2024 Oracle.
 * All Rights Reserved.
 */
#ifndef	__XFS_PARENT_H__
#define	__XFS_PARENT_H__

/* Metadata validators */
bool xfs_parent_namecheck(unsigned int attr_flags, const void *name,
		size_t length);
bool xfs_parent_valuecheck(struct xfs_mount *mp, const void *value,
		size_t valuelen);

xfs_dahash_t xfs_parent_hashval(struct xfs_mount *mp, const uint8_t *name,
		int namelen, xfs_ino_t parent_ino);
xfs_dahash_t xfs_parent_hashattr(struct xfs_mount *mp, const uint8_t *name,
		int namelen, const void *value, int valuelen);

/* Initializes a xfs_parent_rec to be stored as an attribute name. */
static inline void
xfs_parent_rec_init(
	struct xfs_parent_rec	*rec,
	xfs_ino_t		ino,
	uint32_t		gen)
{
	rec->p_ino = cpu_to_be64(ino);
	rec->p_gen = cpu_to_be32(gen);
}

/* Initializes a xfs_parent_rec to be stored as an attribute name. */
static inline void
xfs_inode_to_parent_rec(
	struct xfs_parent_rec	*rec,
	const struct xfs_inode	*dp)
{
	xfs_parent_rec_init(rec, dp->i_ino, VFS_IC(dp)->i_generation);
}

extern struct kmem_cache	*xfs_parent_args_cache;

/*
 * Parent pointer information needed to pass around the deferred xattr update
 * machinery.
 */
struct xfs_parent_args {
	struct xfs_parent_rec	rec;
	struct xfs_parent_rec	new_rec;
	struct xfs_da_args	args;
};

/*
 * Start a parent pointer update by allocating the context object we need to
 * perform a parent pointer update.
 */
static inline int
xfs_parent_start(
	struct xfs_mount	*mp,
	struct xfs_parent_args	**ppargsp)
{
	if (!xfs_has_parent(mp)) {
		*ppargsp = NULL;
		return 0;
	}

	*ppargsp = kmem_cache_zalloc(xfs_parent_args_cache, GFP_KERNEL);
	if (!*ppargsp)
		return -ENOMEM;
	return 0;
}

/* Finish a parent pointer update by freeing the context object. */
static inline void
xfs_parent_finish(
	struct xfs_mount	*mp,
	struct xfs_parent_args	*ppargs)
{
	if (ppargs)
		kmem_cache_free(xfs_parent_args_cache, ppargs);
}

int xfs_parent_addname(struct xfs_trans *tp, struct xfs_parent_args *ppargs,
		struct xfs_inode *dp, const struct xfs_name *parent_name,
		struct xfs_inode *child);
int xfs_parent_removename(struct xfs_trans *tp, struct xfs_parent_args *ppargs,
		struct xfs_inode *dp, const struct xfs_name *parent_name,
		struct xfs_inode *child);
int xfs_parent_replacename(struct xfs_trans *tp,
		struct xfs_parent_args *ppargs,
		struct xfs_inode *old_dp, const struct xfs_name *old_name,
		struct xfs_inode *new_dp, const struct xfs_name *new_name,
		struct xfs_inode *child);

int xfs_parent_from_attr(struct xfs_mount *mp, unsigned int attr_flags,
		const unsigned char *name, unsigned int namelen,
		const void *value, unsigned int valuelen,
		xfs_ino_t *parent_ino, uint32_t *parent_gen);

/* Repair functions */
int xfs_parent_lookup(struct xfs_trans *tp, struct xfs_inode *ip,
		const struct xfs_name *name, struct xfs_parent_rec *pptr,
		struct xfs_da_args *scratch);
int xfs_parent_set(struct xfs_inode *ip, xfs_ino_t owner,
		const struct xfs_name *name, struct xfs_parent_rec *pptr,
		struct xfs_da_args *scratch);
int xfs_parent_unset(struct xfs_inode *ip, xfs_ino_t owner,
		const struct xfs_name *name, struct xfs_parent_rec *pptr,
		struct xfs_da_args *scratch);

#endif /* __XFS_PARENT_H__ */
