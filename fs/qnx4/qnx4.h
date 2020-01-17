/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/fs.h>
#include <linux/qnx4_fs.h>

#define QNX4_DEBUG 0

#if QNX4_DEBUG
#define QNX4DEBUG(X) printk X
#else
#define QNX4DEBUG(X) (void) 0
#endif

struct qnx4_sb_info {
	unsigned int		Version;	/* may be useful */
	struct qnx4_iyesde_entry	*BitMap;	/* useful */
};

struct qnx4_iyesde_info {
	struct qnx4_iyesde_entry raw;
	loff_t mmu_private;
	struct iyesde vfs_iyesde;
};

extern struct iyesde *qnx4_iget(struct super_block *, unsigned long);
extern struct dentry *qnx4_lookup(struct iyesde *dir, struct dentry *dentry, unsigned int flags);
extern unsigned long qnx4_count_free_blocks(struct super_block *sb);
extern unsigned long qnx4_block_map(struct iyesde *iyesde, long iblock);

extern const struct iyesde_operations qnx4_dir_iyesde_operations;
extern const struct file_operations qnx4_dir_operations;
extern int qnx4_is_free(struct super_block *sb, long block);

static inline struct qnx4_sb_info *qnx4_sb(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct qnx4_iyesde_info *qnx4_i(struct iyesde *iyesde)
{
	return container_of(iyesde, struct qnx4_iyesde_info, vfs_iyesde);
}

static inline struct qnx4_iyesde_entry *qnx4_raw_iyesde(struct iyesde *iyesde)
{
	return &qnx4_i(iyesde)->raw;
}
