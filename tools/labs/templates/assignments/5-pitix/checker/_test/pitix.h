#ifndef PITIX_H_
#define PITIX_H_

#include <linux/fs.h>

#define PITIX_MAGIC		0x58495450 /* ascii little endian for PTIX */
#define PITIX_VERSION		2
#define PITIX_SUPERBLOCK_SIZE	4096
#define PITIX_NAME_LEN		16
#define IZONE_BLOCKS		32
#define INODE_DIRECT_DATA_BLOCKS 5

/*
 * filesystem layout:
 *
 *    SB    IMAP	DMAP	   IZONE	DATA
 *    ^	    ^ (1 block) (1 block)  (32 blocks)
 *    |     |
 *    +-0   +-- 4096
 */

/* PITIX super block on disk
 * could be reused for in-memory super block
 */
struct pitix_super_block {
	unsigned long magic;
	__u8 version;
	__u8 block_size_bits;
	__u8 imap_block;
	__u8 dmap_block;
	__u8 izone_block;
	__u8 dzone_block;
	__u16 bfree;
	__u16 ffree;
#ifdef __KERNEL__
	struct buffer_head *sb_bh, *dmap_bh, *imap_bh;
	__u8 *dmap, *imap;
#endif
};

/* PITIX dir entry on disk */
struct __attribute__((__packed__)) pitix_dir_entry {
	__u32 ino;
	char name[PITIX_NAME_LEN];
};

/* PITIX inode on disk */
struct __attribute__((__packed__)) pitix_inode {
	__u32 mode;
	uid_t uid;
	gid_t gid;
	__u32 size;
	__u32 time;
	__u16 direct_data_blocks[INODE_DIRECT_DATA_BLOCKS];
	__u16 indirect_data_block;
};

/* returns size of PITIX inode on disk */
static inline int inode_size(void)
{
	return sizeof(struct pitix_inode);
}

/* returns size of PITIX dir entry on disk */
static inline int dir_entry_size(void)
{
	return sizeof(struct pitix_dir_entry);
}

#ifdef __KERNEL__

/* returns number of PITIX inodes on disk */
static inline long get_inodes(struct super_block *sb)
{
	return IZONE_BLOCKS * sb->s_blocksize / inode_size();
}

/* returns number of PITIX dir entries per block */
static inline int dir_entries_per_block(struct super_block *sb)
{
	return sb->s_blocksize / dir_entry_size();
}

/* returns number of data blocks on disk */
static inline long get_blocks(struct super_block *sb)
{
	return 8 * sb->s_blocksize;
}

/* file system ops */
extern struct file_system_type pitix_fs_type;
extern struct dentry *pitix_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data);

/* super block ops */
extern const struct super_operations pitix_sops;
extern struct inode *pitix_alloc_inode(struct super_block *sb);
extern void pitix_destroy_inode(struct inode *inode);
extern int pitix_write_inode(struct inode *inode,
	struct writeback_control *wbc);
extern void pitix_evict_inode(struct inode *inode);
extern int pitix_statfs(struct dentry *dentry, struct kstatfs *buf);
extern void pitix_put_super(struct super_block *sb);

/* file ops */
extern const struct file_operations pitix_file_operations;

/* file inode ops */
extern const struct inode_operations pitix_file_inode_operations;
extern int pitix_setattr(struct dentry *dentry, struct iattr *attr);

/* dir ops */
extern const struct file_operations pitix_dir_operations;
extern int pitix_readdir(struct file *filp, struct dir_context *ctx);

/* dir inode ops */
extern const struct inode_operations pitix_dir_inode_operations;
extern struct dentry *pitix_lookup(struct inode *dir,
	struct dentry *dentry, unsigned int flags);
extern int pitix_create(struct inode *dir, struct dentry *dentry, umode_t mode,
	bool excl);
extern int pitix_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);
extern int pitix_unlink(struct inode *dir, struct dentry *dentry);
extern int pitix_rmdir(struct inode *dir, struct dentry *dentry);

/* addr space ops */
extern const struct address_space_operations pitix_aops;
extern int pitix_readpage(struct file *file, struct page *page);
extern int pitix_writepage(struct page *page, struct writeback_control *wbc);
extern int pitix_write_begin(struct file *file, struct address_space *mapping,
	loff_t pos, unsigned int len, unsigned int flags,
	struct page **pagep, void **fsdata);
extern sector_t pitix_bmap(struct address_space *mapping, sector_t block);

#endif

#endif

