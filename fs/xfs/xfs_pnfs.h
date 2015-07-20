#ifndef _XFS_PNFS_H
#define _XFS_PNFS_H 1

#ifdef CONFIG_NFSD_PNFS
int xfs_fs_get_uuid(struct super_block *sb, u8 *buf, u32 *len, u64 *offset);
int xfs_fs_map_blocks(struct inode *inode, loff_t offset, u64 length,
		struct iomap *iomap, bool write, u32 *device_generation);
int xfs_fs_commit_blocks(struct inode *inode, struct iomap *maps, int nr_maps,
		struct iattr *iattr);

int xfs_break_layouts(struct inode *inode, uint *iolock, bool with_imutex);
#else
static inline int
xfs_break_layouts(struct inode *inode, uint *iolock, bool with_imutex)
{
	return 0;
}
#endif /* CONFIG_NFSD_PNFS */
#endif /* _XFS_PNFS_H */
