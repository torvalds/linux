/*
 * QNX6 file system, Linux implementation.
 *
 * Version : 1.0.0
 *
 * History :
 *
 * 01-02-2012 by Kai Bankett (chaosman@ontika.net) : first release.
 * 16-02-2012 page map extension by Al Viro
 *
 */

#include <linux/fs.h>
#include <linux/pagemap.h>

typedef __u16 __bitwise __fs16;
typedef __u32 __bitwise __fs32;
typedef __u64 __bitwise __fs64;

#include <linux/qnx6_fs.h>

#ifdef CONFIG_QNX6FS_DEBUG
#define QNX6DEBUG(X) printk X
#else
#define QNX6DEBUG(X) (void) 0
#endif

struct qnx6_sb_info {
	struct buffer_head	*sb_buf;	/* superblock buffer */
	struct qnx6_super_block	*sb;		/* our superblock */
	int			s_blks_off;	/* blkoffset fs-startpoint */
	int			s_ptrbits;	/* indirect pointer bitfield */
	unsigned long		s_mount_opt;	/* all mount options */
	int			s_bytesex;	/* holds endianess info */
	struct inode *		inodes;
	struct inode *		longfile;
};

struct qnx6_inode_info {
	__fs32			di_block_ptr[QNX6_NO_DIRECT_POINTERS];
	__u8			di_filelevels;
	__u32			i_dir_start_lookup;
	struct inode		vfs_inode;
};

extern struct inode *qnx6_iget(struct super_block *sb, unsigned ino);
extern struct dentry *qnx6_lookup(struct inode *dir, struct dentry *dentry,
					struct nameidata *nd);

#ifdef CONFIG_QNX6FS_DEBUG
extern void qnx6_superblock_debug(struct qnx6_super_block *,
						struct super_block *);
#endif

extern const struct inode_operations qnx6_dir_inode_operations;
extern const struct file_operations qnx6_dir_operations;

static inline struct qnx6_sb_info *QNX6_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct qnx6_inode_info *QNX6_I(struct inode *inode)
{
	return container_of(inode, struct qnx6_inode_info, vfs_inode);
}

#define clear_opt(o, opt)		(o &= ~(QNX6_MOUNT_##opt))
#define set_opt(o, opt)			(o |= (QNX6_MOUNT_##opt))
#define test_opt(sb, opt)		(QNX6_SB(sb)->s_mount_opt & \
					 QNX6_MOUNT_##opt)
enum {
	BYTESEX_LE,
	BYTESEX_BE,
};

static inline __u64 fs64_to_cpu(struct qnx6_sb_info *sbi, __fs64 n)
{
	if (sbi->s_bytesex == BYTESEX_LE)
		return le64_to_cpu((__force __le64)n);
	else
		return be64_to_cpu((__force __be64)n);
}

static inline __fs64 cpu_to_fs64(struct qnx6_sb_info *sbi, __u64 n)
{
	if (sbi->s_bytesex == BYTESEX_LE)
		return (__force __fs64)cpu_to_le64(n);
	else
		return (__force __fs64)cpu_to_be64(n);
}

static inline __u32 fs32_to_cpu(struct qnx6_sb_info *sbi, __fs32 n)
{
	if (sbi->s_bytesex == BYTESEX_LE)
		return le32_to_cpu((__force __le32)n);
	else
		return be32_to_cpu((__force __be32)n);
}

static inline __fs32 cpu_to_fs32(struct qnx6_sb_info *sbi, __u32 n)
{
	if (sbi->s_bytesex == BYTESEX_LE)
		return (__force __fs32)cpu_to_le32(n);
	else
		return (__force __fs32)cpu_to_be32(n);
}

static inline __u16 fs16_to_cpu(struct qnx6_sb_info *sbi, __fs16 n)
{
	if (sbi->s_bytesex == BYTESEX_LE)
		return le16_to_cpu((__force __le16)n);
	else
		return be16_to_cpu((__force __be16)n);
}

static inline __fs16 cpu_to_fs16(struct qnx6_sb_info *sbi, __u16 n)
{
	if (sbi->s_bytesex == BYTESEX_LE)
		return (__force __fs16)cpu_to_le16(n);
	else
		return (__force __fs16)cpu_to_be16(n);
}

extern struct qnx6_super_block *qnx6_mmi_fill_super(struct super_block *s,
						    int silent);

static inline void qnx6_put_page(struct page *page)
{
	kunmap(page);
	page_cache_release(page);
}

extern unsigned qnx6_find_entry(int len, struct inode *dir, const char *name,
				struct page **res_page);
