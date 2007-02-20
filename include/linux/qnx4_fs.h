/*
 *  Name                         : qnx4_fs.h
 *  Author                       : Richard Frowijn
 *  Function                     : qnx4 global filesystem definitions
 *  Version                      : 1.0.2
 *  Last modified                : 2000-01-31
 *
 *  History                      : 23-03-1998 created
 */
#ifndef _LINUX_QNX4_FS_H
#define _LINUX_QNX4_FS_H

#include <linux/qnxtypes.h>
#include <linux/magic.h>

#define QNX4_ROOT_INO 1

#define QNX4_MAX_XTNTS_PER_XBLK	60
/* for di_status */
#define QNX4_FILE_USED          0x01
#define QNX4_FILE_MODIFIED      0x02
#define QNX4_FILE_BUSY          0x04
#define QNX4_FILE_LINK          0x08
#define QNX4_FILE_INODE         0x10
#define QNX4_FILE_FSYSCLEAN     0x20

#define QNX4_I_MAP_SLOTS	8
#define QNX4_Z_MAP_SLOTS	64
#define QNX4_VALID_FS		0x0001	/* Clean fs. */
#define QNX4_ERROR_FS		0x0002	/* fs has errors. */
#define QNX4_BLOCK_SIZE         0x200	/* blocksize of 512 bytes */
#define QNX4_BLOCK_SIZE_BITS    9	/* blocksize shift */
#define QNX4_DIR_ENTRY_SIZE     0x040	/* dir entry size of 64 bytes */
#define QNX4_DIR_ENTRY_SIZE_BITS 6	/* dir entry size shift */
#define QNX4_XBLK_ENTRY_SIZE    0x200	/* xblk entry size */
#define QNX4_INODES_PER_BLOCK   0x08	/* 512 / 64 */

/* for filenames */
#define QNX4_SHORT_NAME_MAX	16
#define QNX4_NAME_MAX		48

/*
 * This is the original qnx4 inode layout on disk.
 */
struct qnx4_inode_entry {
	char		di_fname[QNX4_SHORT_NAME_MAX];
	qnx4_off_t	di_size;
	qnx4_xtnt_t	di_first_xtnt;
	__le32		di_xblk;
	__le32		di_ftime;
	__le32		di_mtime;
	__le32		di_atime;
	__le32		di_ctime;
	qnx4_nxtnt_t	di_num_xtnts;
	qnx4_mode_t	di_mode;
	qnx4_muid_t	di_uid;
	qnx4_mgid_t	di_gid;
	qnx4_nlink_t	di_nlink;
	__u8		di_zero[4];
	qnx4_ftype_t	di_type;
	__u8		di_status;
};

struct qnx4_link_info {
	char		dl_fname[QNX4_NAME_MAX];
	__le32		dl_inode_blk;
	__u8		dl_inode_ndx;
	__u8		dl_spare[10];
	__u8		dl_status;
};

struct qnx4_xblk {
	__le32		xblk_next_xblk;
	__le32		xblk_prev_xblk;
	__u8		xblk_num_xtnts;
	__u8		xblk_spare[3];
	__le32		xblk_num_blocks;
	qnx4_xtnt_t	xblk_xtnts[QNX4_MAX_XTNTS_PER_XBLK];
	char		xblk_signature[8];
	qnx4_xtnt_t	xblk_first_xtnt;
};

struct qnx4_super_block {
	struct qnx4_inode_entry RootDir;
	struct qnx4_inode_entry Inode;
	struct qnx4_inode_entry Boot;
	struct qnx4_inode_entry AltBoot;
};

#ifdef __KERNEL__

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
extern int qnx4_sync_file(struct file *file, struct dentry *dentry, int);
extern int qnx4_sync_inode(struct inode *inode);

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

#endif				/* __KERNEL__ */

#endif
