/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _XFS_ZONE_ALLOC_H
#define _XFS_ZONE_ALLOC_H

struct iomap_ioend;
struct xfs_open_zone;

struct xfs_zone_alloc_ctx {
	struct xfs_open_zone	*open_zone;
	xfs_filblks_t		reserved_blocks;
};

/*
 * Grab any available space, even if it is less than what the caller asked for.
 */
#define XFS_ZR_GREEDY		(1U << 0)
/*
 * Only grab instantly available space, don't wait or GC.
 */
#define XFS_ZR_NOWAIT		(1U << 1)
/*
 * Dip into the reserved pool.
 */
#define XFS_ZR_RESERVED		(1U << 2)

int xfs_zoned_space_reserve(struct xfs_mount *mp, xfs_filblks_t count_fsb,
		unsigned int flags, struct xfs_zone_alloc_ctx *ac);
void xfs_zoned_space_unreserve(struct xfs_mount *mp,
		struct xfs_zone_alloc_ctx *ac);
void xfs_zoned_add_available(struct xfs_mount *mp, xfs_filblks_t count_fsb);

void xfs_zone_alloc_and_submit(struct iomap_ioend *ioend,
		struct xfs_open_zone **oz);
int xfs_zone_free_blocks(struct xfs_trans *tp, struct xfs_rtgroup *rtg,
		xfs_fsblock_t fsbno, xfs_filblks_t len);
int xfs_zoned_end_io(struct xfs_inode *ip, xfs_off_t offset, xfs_off_t count,
		xfs_daddr_t daddr, struct xfs_open_zone *oz,
		xfs_fsblock_t old_startblock);
void xfs_open_zone_put(struct xfs_open_zone *oz);

void xfs_zoned_wake_all(struct xfs_mount *mp);
bool xfs_zone_rgbno_is_valid(struct xfs_rtgroup *rtg, xfs_rgnumber_t rgbno);
void xfs_mark_rtg_boundary(struct iomap_ioend *ioend);

uint64_t xfs_zoned_default_resblks(struct xfs_mount *mp,
		enum xfs_free_counter ctr);
void xfs_zoned_show_stats(struct seq_file *m, struct xfs_mount *mp);

#ifdef CONFIG_XFS_RT
int xfs_mount_zones(struct xfs_mount *mp);
void xfs_unmount_zones(struct xfs_mount *mp);
void xfs_zone_gc_start(struct xfs_mount *mp);
void xfs_zone_gc_stop(struct xfs_mount *mp);
#else
static inline int xfs_mount_zones(struct xfs_mount *mp)
{
	return -EIO;
}
static inline void xfs_unmount_zones(struct xfs_mount *mp)
{
}
static inline void xfs_zone_gc_start(struct xfs_mount *mp)
{
}
static inline void xfs_zone_gc_stop(struct xfs_mount *mp)
{
}
#endif /* CONFIG_XFS_RT */

#endif /* _XFS_ZONE_ALLOC_H */
