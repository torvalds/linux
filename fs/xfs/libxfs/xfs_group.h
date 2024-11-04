/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 Red Hat, Inc.
 */
#ifndef __LIBXFS_GROUP_H
#define __LIBXFS_GROUP_H 1

struct xfs_group {
	struct xfs_mount	*xg_mount;
	uint32_t		xg_gno;
	enum xfs_group_type	xg_type;
	atomic_t		xg_ref;		/* passive reference count */
	atomic_t		xg_active_ref;	/* active reference count */
};

struct xfs_group *xfs_group_get(struct xfs_mount *mp, uint32_t index,
		enum xfs_group_type type);
struct xfs_group *xfs_group_hold(struct xfs_group *xg);
void xfs_group_put(struct xfs_group *xg);

struct xfs_group *xfs_group_grab(struct xfs_mount *mp, uint32_t index,
		enum xfs_group_type type);
struct xfs_group *xfs_group_grab_next_mark(struct xfs_mount *mp,
		struct xfs_group *xg, xa_mark_t mark, enum xfs_group_type type);
void xfs_group_rele(struct xfs_group *xg);

void xfs_group_free(struct xfs_mount *mp, uint32_t index,
		enum xfs_group_type type, void (*uninit)(struct xfs_group *xg));
int xfs_group_insert(struct xfs_mount *mp, struct xfs_group *xg,
		uint32_t index, enum xfs_group_type);

#define xfs_group_set_mark(_xg, _mark) \
	xa_set_mark(&(_xg)->xg_mount->m_groups[(_xg)->xg_type].xa, \
			(_xg)->xg_gno, (_mark))
#define xfs_group_clear_mark(_xg, _mark) \
	xa_clear_mark(&(_xg)->xg_mount->m_groups[(_xg)->xg_type].xa, \
			(_xg)->xg_gno, (_mark))
#define xfs_group_marked(_mp, _type, _mark) \
	xa_marked(&(_mp)->m_groups[(_type)].xa, (_mark))

#endif /* __LIBXFS_GROUP_H */
