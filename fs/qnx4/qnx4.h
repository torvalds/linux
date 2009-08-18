#include <linux/fs.h>
#include <linux/qnx4_fs.h>

#define QNX4_DEBUG 0

#if QNX4_DEBUG
#define QNX4DEBUG(X) printk X
#else
#define QNX4DEBUG(X) (void) 0
#endif

struct qnx4_sb_info {
	struct buffer_head	*sb_buf;	/* superblock buffer */
	struct qnx4_super_block	*sb;		/* our superblock */
	unsigned int		Version;	/* may be useful */
	struct qnx4_inode_entry	*BitMap;	/* useful */
};

struct qnx4_inode_info {
	struct qnx4_inode_entry raw;
	loff_t mmu_private;
	struct inode vfs_inode;
};

extern struct inode *qnx4_iget(struct super_block *, unsigned long);
extern struct dentry *qnx4_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd);
extern unsigned long qnx4_count_free_blocks(struct super_block *sb);
extern unsigned long qnx4_block_map(struct inode *inode, long iblock);

extern struct buffer_head *qnx4_bread(struct inode *, int, int);

extern const struct inode_operations qnx4_file_inode_operations;
extern const struct inode_operations qnx4_dir_inode_operations;
extern const struct file_operations qnx4_file_operations;
extern const struct file_operations qnx4_dir_operations;
extern int qnx4_is_free(struct super_block *sb, long block);
extern int qnx4_set_bitmap(struct super_block *sb, long block, int busy);
extern int qnx4_create(struct inode *inode, struct dentry *dentry, int mode, struct nameidata *nd);
extern void qnx4_truncate(struct inode *inode);
extern void qnx4_free_inode(struct inode *inode);
extern int qnx4_unlink(struct inode *dir, struct dentry *dentry);
extern int qnx4_rmdir(struct inode *dir, struct dentry *dentry);

static inline struct qnx4_sb_info *qnx4_sb(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct qnx4_inode_info *qnx4_i(struct inode *inode)
{
	return container_of(inode, struct qnx4_inode_info, vfs_inode);
}

static inline struct qnx4_inode_entry *qnx4_raw_inode(struct inode *inode)
{
	return &qnx4_i(inode)->raw;
}
