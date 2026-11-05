/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal definitions and helpers for fs/ext4/namei.c
 */

#ifndef _EXT4_NAMEI_H
#define _EXT4_NAMEI_H

#include "ext4.h"

/*
 * Hints to ext4_read_dirblock regarding whether we expect a directory
 * block being read to be an index block, or a block containing
 * directory entries (and if the latter, whether it was found via a
 * logical block in an htree index block).  This is used to control
 * what sort of sanity checkinig ext4_read_dirblock() will do on the
 * directory block read from the storage device.  EITHER will means
 * the caller doesn't know what kind of directory block will be read,
 * so no specific verification will be done.
 */
enum dirblock_type {
	EITHER, INDEX, DIRENT, DIRENT_HTREE
};

struct fake_dirent {
	__le32 inode;
	__le16 rec_len;
	u8 name_len;
	u8 file_type;
};

struct dx_countlimit {
	__le16 limit;
	__le16 count;
};

struct dx_entry {
	__le32 hash;
	__le32 block;
};

/*
 * dx_root_info is laid out so that if it should somehow get overlaid by a
 * dirent the two low bits of the hash version will be zero.  Therefore, the
 * hash version mod 4 should never be 0.  Sincerely, the paranoia department.
 */
struct dx_root {
	struct fake_dirent dot;
	char dot_name[4];
	struct fake_dirent dotdot;
	char dotdot_name[4];
	struct dx_root_info {
		__le32 reserved_zero;
		u8 hash_version;
		u8 info_length; /* 8 */
		u8 indirect_levels;
		u8 unused_flags;
	} info;
	struct dx_entry	entries[];
};

struct dx_node {
	struct fake_dirent fake;
	struct dx_entry	entries[];
};

struct dx_frame {
	struct buffer_head *bh;
	struct dx_entry *entries;
	struct dx_entry *at;
};

struct dx_map_entry {
	u32 hash;
	u16 offs;
	u16 size;
};

/*
 * This goes at the end of each htree block.
 */
struct dx_tail {
	u32 dt_reserved;
	__le32 dt_checksum;	/* crc32c(uuid+inum+dirblock) */
};

/*
 * p is at least 6 bytes before the end of page
 */
static inline struct ext4_dir_entry_2 *
ext4_next_entry(struct ext4_dir_entry_2 *p, unsigned long blocksize)
{
	return (struct ext4_dir_entry_2 *)((char *)p +
		ext4_rec_len_from_disk(p->rec_len, blocksize));
}

/*
 * Future: use high four bits of block for coalesce-on-delete flags
 * Mask them off for now.
 */
static inline ext4_lblk_t dx_get_block(struct dx_entry *entry)
{
	return le32_to_cpu(entry->block) & 0x0fffffff;
}

static inline void dx_set_block(struct dx_entry *entry, ext4_lblk_t value)
{
	entry->block = cpu_to_le32(value);
}

static inline unsigned int dx_get_hash(struct dx_entry *entry)
{
	return le32_to_cpu(entry->hash);
}

static inline void dx_set_hash(struct dx_entry *entry, unsigned int value)
{
	entry->hash = cpu_to_le32(value);
}

static inline unsigned int dx_get_count(struct dx_entry *entries)
{
	return le16_to_cpu(((struct dx_countlimit *)entries)->count);
}

static inline unsigned int dx_get_limit(struct dx_entry *entries)
{
	return le16_to_cpu(((struct dx_countlimit *)entries)->limit);
}

static inline void dx_set_count(struct dx_entry *entries, unsigned int value)
{
	((struct dx_countlimit *)entries)->count = cpu_to_le16(value);
}

static inline void dx_set_limit(struct dx_entry *entries, unsigned int value)
{
	((struct dx_countlimit *)entries)->limit = cpu_to_le16(value);
}

static inline unsigned int dx_root_limit(struct inode *dir, unsigned int infosize)
{
	unsigned int entry_space = dir->i_sb->s_blocksize -
			ext4_dir_rec_len(1, NULL) -
			ext4_dir_rec_len(2, NULL) - infosize;

	if (ext4_has_feature_metadata_csum(dir->i_sb))
		entry_space -= sizeof(struct dx_tail);
	return entry_space / sizeof(struct dx_entry);
}

static inline unsigned int dx_node_limit(struct inode *dir)
{
	unsigned int entry_space = dir->i_sb->s_blocksize -
			ext4_dir_rec_len(0, dir);

	if (ext4_has_feature_metadata_csum(dir->i_sb))
		entry_space -= sizeof(struct dx_tail);
	return entry_space / sizeof(struct dx_entry);
}

#if IS_ENABLED(CONFIG_EXT4_KUNIT_TESTS)
struct buffer_head *ext4_append_test(handle_t *handle, struct inode *inode,
				     ext4_lblk_t *block);
struct buffer_head *ext4_read_dirblock_test(struct inode *inode,
					    ext4_lblk_t block,
					    enum dirblock_type type);
void ext4_dx_csum_set_test(struct inode *inode, struct ext4_dir_entry *dirent);
int ext4_dx_csum_verify_test(struct inode *inode,
			     struct ext4_dir_entry *dirent);
struct dx_frame *dx_probe_test(struct ext4_filename *fname, struct inode *dir,
			       struct dx_hash_info *hinfo,
			       struct dx_frame *frame_in);
void dx_release_test(struct dx_frame *frames);
int ext4_htree_next_block_test(struct inode *dir, __u32 hash,
			       struct dx_frame *frame,
			       struct dx_frame *frames, __u32 *start_hash);
int dx_make_map_test(struct inode *dir, struct buffer_head *bh,
		     struct dx_hash_info *hinfo,
		     struct dx_map_entry *map_tail);
void dx_sort_map_test(struct dx_map_entry *map, unsigned int count);
void dx_insert_block_test(struct dx_frame *frame, u32 hash, ext4_lblk_t block);
struct ext4_dir_entry_2 *dx_move_dirents_test(struct inode *dir, char *from,
					      char *to,
					      struct dx_map_entry *map,
					      int count,
					      unsigned int blocksize);
struct ext4_dir_entry_2 *dx_pack_dirents_test(struct inode *dir, char *base,
					      unsigned int blocksize);
struct ext4_dir_entry_2 *do_split_test(handle_t *handle, struct inode *dir,
				       struct buffer_head **bh,
				       struct dx_frame *frame,
				       struct dx_hash_info *hinfo);
bool ext4_check_dx_root_test(struct inode *dir, struct dx_root *root);
struct buffer_head *ext4_find_entry_test(struct inode *dir,
					 const struct qstr *d_name,
					 struct ext4_dir_entry_2 **res_dir,
					 int *inlined);
struct buffer_head *ext4_lookup_entry_test(struct inode *dir,
					   struct dentry *dentry,
					   struct ext4_dir_entry_2 **res_dir);
int ext4_add_entry_test(handle_t *handle, struct dentry *dentry,
			struct inode *inode);
int ext4_delete_entry_test(handle_t *handle, struct inode *dir,
			   struct ext4_dir_entry_2 *de_del,
			   struct buffer_head *bh);
void ext4_inc_count_test(struct inode *inode);
void ext4_dec_count_test(struct inode *inode);
int ext4_init_symlink_block_test(handle_t *handle, struct inode *inode,
				 struct fscrypt_str *disk_link);
#endif /* CONFIG_EXT4_KUNIT_TESTS */

#endif /* _EXT4_NAMEI_H */
