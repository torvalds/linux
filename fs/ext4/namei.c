// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/ext4/namei.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/namei.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 *  Directory entry file type support and forward compatibility hooks
 *	for B-tree directories by Theodore Ts'o (tytso@mit.edu), 1998
 *  Hash Tree Directory indexing (c)
 *	Daniel Phillips, 2001
 *  Hash Tree Directory indexing porting
 *	Christopher Li, 2002
 *  Hash Tree Directory indexing cleanup
 *	Theodore Ts'o, 2002
 */

#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/time.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/quotaops.h>
#include <linux/buffer_head.h>
#include <linux/bio.h>
#include "ext4.h"
#include "ext4_jbd2.h"

#include "xattr.h"
#include "acl.h"

#include <trace/events/ext4.h>
/*
 * define how far ahead to read directories while searching them.
 */
#define NAMEI_RA_CHUNKS  2
#define NAMEI_RA_BLOCKS  4
#define NAMEI_RA_SIZE	     (NAMEI_RA_CHUNKS * NAMEI_RA_BLOCKS)

static struct buffer_head *ext4_append(handle_t *handle,
					struct inode *inode,
					ext4_lblk_t *block)
{
	struct buffer_head *bh;
	int err;

	if (unlikely(EXT4_SB(inode->i_sb)->s_max_dir_size_kb &&
		     ((inode->i_size >> 10) >=
		      EXT4_SB(inode->i_sb)->s_max_dir_size_kb)))
		return ERR_PTR(-ENOSPC);

	*block = inode->i_size >> inode->i_sb->s_blocksize_bits;

	bh = ext4_bread(handle, inode, *block, EXT4_GET_BLOCKS_CREATE);
	if (IS_ERR(bh))
		return bh;
	inode->i_size += inode->i_sb->s_blocksize;
	EXT4_I(inode)->i_disksize = inode->i_size;
	BUFFER_TRACE(bh, "get_write_access");
	err = ext4_journal_get_write_access(handle, bh);
	if (err) {
		brelse(bh);
		ext4_std_error(inode->i_sb, err);
		return ERR_PTR(err);
	}
	return bh;
}

static int ext4_dx_csum_verify(struct inode *inode,
			       struct ext4_dir_entry *dirent);

typedef enum {
	EITHER, INDEX, DIRENT
} dirblock_type_t;

#define ext4_read_dirblock(inode, block, type) \
	__ext4_read_dirblock((inode), (block), (type), __func__, __LINE__)

static struct buffer_head *__ext4_read_dirblock(struct inode *inode,
						ext4_lblk_t block,
						dirblock_type_t type,
						const char *func,
						unsigned int line)
{
	struct buffer_head *bh;
	struct ext4_dir_entry *dirent;
	int is_dx_block = 0;

	bh = ext4_bread(NULL, inode, block, 0);
	if (IS_ERR(bh)) {
		__ext4_warning(inode->i_sb, func, line,
			       "inode #%lu: lblock %lu: comm %s: "
			       "error %ld reading directory block",
			       inode->i_ino, (unsigned long)block,
			       current->comm, PTR_ERR(bh));

		return bh;
	}
	if (!bh) {
		ext4_error_inode(inode, func, line, block,
				 "Directory hole found");
		return ERR_PTR(-EFSCORRUPTED);
	}
	dirent = (struct ext4_dir_entry *) bh->b_data;
	/* Determine whether or not we have an index block */
	if (is_dx(inode)) {
		if (block == 0)
			is_dx_block = 1;
		else if (ext4_rec_len_from_disk(dirent->rec_len,
						inode->i_sb->s_blocksize) ==
			 inode->i_sb->s_blocksize)
			is_dx_block = 1;
	}
	if (!is_dx_block && type == INDEX) {
		ext4_error_inode(inode, func, line, block,
		       "directory leaf block found instead of index block");
		return ERR_PTR(-EFSCORRUPTED);
	}
	if (!ext4_has_metadata_csum(inode->i_sb) ||
	    buffer_verified(bh))
		return bh;

	/*
	 * An empty leaf block can get mistaken for a index block; for
	 * this reason, we can only check the index checksum when the
	 * caller is sure it should be an index block.
	 */
	if (is_dx_block && type == INDEX) {
		if (ext4_dx_csum_verify(inode, dirent))
			set_buffer_verified(bh);
		else {
			ext4_error_inode(inode, func, line, block,
					 "Directory index failed checksum");
			brelse(bh);
			return ERR_PTR(-EFSBADCRC);
		}
	}
	if (!is_dx_block) {
		if (ext4_dirent_csum_verify(inode, dirent))
			set_buffer_verified(bh);
		else {
			ext4_error_inode(inode, func, line, block,
					 "Directory block failed checksum");
			brelse(bh);
			return ERR_PTR(-EFSBADCRC);
		}
	}
	return bh;
}

#ifndef assert
#define assert(test) J_ASSERT(test)
#endif

#ifdef DX_DEBUG
#define dxtrace(command) command
#else
#define dxtrace(command)
#endif

struct fake_dirent
{
	__le32 inode;
	__le16 rec_len;
	u8 name_len;
	u8 file_type;
};

struct dx_countlimit
{
	__le16 limit;
	__le16 count;
};

struct dx_entry
{
	__le32 hash;
	__le32 block;
};

/*
 * dx_root_info is laid out so that if it should somehow get overlaid by a
 * dirent the two low bits of the hash version will be zero.  Therefore, the
 * hash version mod 4 should never be 0.  Sincerely, the paranoia department.
 */

struct dx_root
{
	struct fake_dirent dot;
	char dot_name[4];
	struct fake_dirent dotdot;
	char dotdot_name[4];
	struct dx_root_info
	{
		__le32 reserved_zero;
		u8 hash_version;
		u8 info_length; /* 8 */
		u8 indirect_levels;
		u8 unused_flags;
	}
	info;
	struct dx_entry	entries[0];
};

struct dx_node
{
	struct fake_dirent fake;
	struct dx_entry	entries[0];
};


struct dx_frame
{
	struct buffer_head *bh;
	struct dx_entry *entries;
	struct dx_entry *at;
};

struct dx_map_entry
{
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

static inline ext4_lblk_t dx_get_block(struct dx_entry *entry);
static void dx_set_block(struct dx_entry *entry, ext4_lblk_t value);
static inline unsigned dx_get_hash(struct dx_entry *entry);
static void dx_set_hash(struct dx_entry *entry, unsigned value);
static unsigned dx_get_count(struct dx_entry *entries);
static unsigned dx_get_limit(struct dx_entry *entries);
static void dx_set_count(struct dx_entry *entries, unsigned value);
static void dx_set_limit(struct dx_entry *entries, unsigned value);
static unsigned dx_root_limit(struct inode *dir, unsigned infosize);
static unsigned dx_node_limit(struct inode *dir);
static struct dx_frame *dx_probe(struct ext4_filename *fname,
				 struct inode *dir,
				 struct dx_hash_info *hinfo,
				 struct dx_frame *frame);
static void dx_release(struct dx_frame *frames);
static int dx_make_map(struct inode *dir, struct ext4_dir_entry_2 *de,
		       unsigned blocksize, struct dx_hash_info *hinfo,
		       struct dx_map_entry map[]);
static void dx_sort_map(struct dx_map_entry *map, unsigned count);
static struct ext4_dir_entry_2 *dx_move_dirents(char *from, char *to,
		struct dx_map_entry *offsets, int count, unsigned blocksize);
static struct ext4_dir_entry_2* dx_pack_dirents(char *base, unsigned blocksize);
static void dx_insert_block(struct dx_frame *frame,
					u32 hash, ext4_lblk_t block);
static int ext4_htree_next_block(struct inode *dir, __u32 hash,
				 struct dx_frame *frame,
				 struct dx_frame *frames,
				 __u32 *start_hash);
static struct buffer_head * ext4_dx_find_entry(struct inode *dir,
		struct ext4_filename *fname,
		struct ext4_dir_entry_2 **res_dir);
static int ext4_dx_add_entry(handle_t *handle, struct ext4_filename *fname,
			     struct inode *dir, struct inode *inode);

/* checksumming functions */
void initialize_dirent_tail(struct ext4_dir_entry_tail *t,
			    unsigned int blocksize)
{
	memset(t, 0, sizeof(struct ext4_dir_entry_tail));
	t->det_rec_len = ext4_rec_len_to_disk(
			sizeof(struct ext4_dir_entry_tail), blocksize);
	t->det_reserved_ft = EXT4_FT_DIR_CSUM;
}

/* Walk through a dirent block to find a checksum "dirent" at the tail */
static struct ext4_dir_entry_tail *get_dirent_tail(struct inode *inode,
						   struct ext4_dir_entry *de)
{
	struct ext4_dir_entry_tail *t;

#ifdef PARANOID
	struct ext4_dir_entry *d, *top;

	d = de;
	top = (struct ext4_dir_entry *)(((void *)de) +
		(EXT4_BLOCK_SIZE(inode->i_sb) -
		sizeof(struct ext4_dir_entry_tail)));
	while (d < top && d->rec_len)
		d = (struct ext4_dir_entry *)(((void *)d) +
		    le16_to_cpu(d->rec_len));

	if (d != top)
		return NULL;

	t = (struct ext4_dir_entry_tail *)d;
#else
	t = EXT4_DIRENT_TAIL(de, EXT4_BLOCK_SIZE(inode->i_sb));
#endif

	if (t->det_reserved_zero1 ||
	    le16_to_cpu(t->det_rec_len) != sizeof(struct ext4_dir_entry_tail) ||
	    t->det_reserved_zero2 ||
	    t->det_reserved_ft != EXT4_FT_DIR_CSUM)
		return NULL;

	return t;
}

static __le32 ext4_dirent_csum(struct inode *inode,
			       struct ext4_dir_entry *dirent, int size)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct ext4_inode_info *ei = EXT4_I(inode);
	__u32 csum;

	csum = ext4_chksum(sbi, ei->i_csum_seed, (__u8 *)dirent, size);
	return cpu_to_le32(csum);
}

#define warn_no_space_for_csum(inode)					\
	__warn_no_space_for_csum((inode), __func__, __LINE__)

static void __warn_no_space_for_csum(struct inode *inode, const char *func,
				     unsigned int line)
{
	__ext4_warning_inode(inode, func, line,
		"No space for directory leaf checksum. Please run e2fsck -D.");
}

int ext4_dirent_csum_verify(struct inode *inode, struct ext4_dir_entry *dirent)
{
	struct ext4_dir_entry_tail *t;

	if (!ext4_has_metadata_csum(inode->i_sb))
		return 1;

	t = get_dirent_tail(inode, dirent);
	if (!t) {
		warn_no_space_for_csum(inode);
		return 0;
	}

	if (t->det_checksum != ext4_dirent_csum(inode, dirent,
						(void *)t - (void *)dirent))
		return 0;

	return 1;
}

static void ext4_dirent_csum_set(struct inode *inode,
				 struct ext4_dir_entry *dirent)
{
	struct ext4_dir_entry_tail *t;

	if (!ext4_has_metadata_csum(inode->i_sb))
		return;

	t = get_dirent_tail(inode, dirent);
	if (!t) {
		warn_no_space_for_csum(inode);
		return;
	}

	t->det_checksum = ext4_dirent_csum(inode, dirent,
					   (void *)t - (void *)dirent);
}

int ext4_handle_dirty_dirent_node(handle_t *handle,
				  struct inode *inode,
				  struct buffer_head *bh)
{
	ext4_dirent_csum_set(inode, (struct ext4_dir_entry *)bh->b_data);
	return ext4_handle_dirty_metadata(handle, inode, bh);
}

static struct dx_countlimit *get_dx_countlimit(struct inode *inode,
					       struct ext4_dir_entry *dirent,
					       int *offset)
{
	struct ext4_dir_entry *dp;
	struct dx_root_info *root;
	int count_offset;

	if (le16_to_cpu(dirent->rec_len) == EXT4_BLOCK_SIZE(inode->i_sb))
		count_offset = 8;
	else if (le16_to_cpu(dirent->rec_len) == 12) {
		dp = (struct ext4_dir_entry *)(((void *)dirent) + 12);
		if (le16_to_cpu(dp->rec_len) !=
		    EXT4_BLOCK_SIZE(inode->i_sb) - 12)
			return NULL;
		root = (struct dx_root_info *)(((void *)dp + 12));
		if (root->reserved_zero ||
		    root->info_length != sizeof(struct dx_root_info))
			return NULL;
		count_offset = 32;
	} else
		return NULL;

	if (offset)
		*offset = count_offset;
	return (struct dx_countlimit *)(((void *)dirent) + count_offset);
}

static __le32 ext4_dx_csum(struct inode *inode, struct ext4_dir_entry *dirent,
			   int count_offset, int count, struct dx_tail *t)
{
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct ext4_inode_info *ei = EXT4_I(inode);
	__u32 csum;
	int size;
	__u32 dummy_csum = 0;
	int offset = offsetof(struct dx_tail, dt_checksum);

	size = count_offset + (count * sizeof(struct dx_entry));
	csum = ext4_chksum(sbi, ei->i_csum_seed, (__u8 *)dirent, size);
	csum = ext4_chksum(sbi, csum, (__u8 *)t, offset);
	csum = ext4_chksum(sbi, csum, (__u8 *)&dummy_csum, sizeof(dummy_csum));

	return cpu_to_le32(csum);
}

static int ext4_dx_csum_verify(struct inode *inode,
			       struct ext4_dir_entry *dirent)
{
	struct dx_countlimit *c;
	struct dx_tail *t;
	int count_offset, limit, count;

	if (!ext4_has_metadata_csum(inode->i_sb))
		return 1;

	c = get_dx_countlimit(inode, dirent, &count_offset);
	if (!c) {
		EXT4_ERROR_INODE(inode, "dir seems corrupt?  Run e2fsck -D.");
		return 0;
	}
	limit = le16_to_cpu(c->limit);
	count = le16_to_cpu(c->count);
	if (count_offset + (limit * sizeof(struct dx_entry)) >
	    EXT4_BLOCK_SIZE(inode->i_sb) - sizeof(struct dx_tail)) {
		warn_no_space_for_csum(inode);
		return 0;
	}
	t = (struct dx_tail *)(((struct dx_entry *)c) + limit);

	if (t->dt_checksum != ext4_dx_csum(inode, dirent, count_offset,
					    count, t))
		return 0;
	return 1;
}

static void ext4_dx_csum_set(struct inode *inode, struct ext4_dir_entry *dirent)
{
	struct dx_countlimit *c;
	struct dx_tail *t;
	int count_offset, limit, count;

	if (!ext4_has_metadata_csum(inode->i_sb))
		return;

	c = get_dx_countlimit(inode, dirent, &count_offset);
	if (!c) {
		EXT4_ERROR_INODE(inode, "dir seems corrupt?  Run e2fsck -D.");
		return;
	}
	limit = le16_to_cpu(c->limit);
	count = le16_to_cpu(c->count);
	if (count_offset + (limit * sizeof(struct dx_entry)) >
	    EXT4_BLOCK_SIZE(inode->i_sb) - sizeof(struct dx_tail)) {
		warn_no_space_for_csum(inode);
		return;
	}
	t = (struct dx_tail *)(((struct dx_entry *)c) + limit);

	t->dt_checksum = ext4_dx_csum(inode, dirent, count_offset, count, t);
}

static inline int ext4_handle_dirty_dx_node(handle_t *handle,
					    struct inode *inode,
					    struct buffer_head *bh)
{
	ext4_dx_csum_set(inode, (struct ext4_dir_entry *)bh->b_data);
	return ext4_handle_dirty_metadata(handle, inode, bh);
}

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

static inline unsigned dx_get_hash(struct dx_entry *entry)
{
	return le32_to_cpu(entry->hash);
}

static inline void dx_set_hash(struct dx_entry *entry, unsigned value)
{
	entry->hash = cpu_to_le32(value);
}

static inline unsigned dx_get_count(struct dx_entry *entries)
{
	return le16_to_cpu(((struct dx_countlimit *) entries)->count);
}

static inline unsigned dx_get_limit(struct dx_entry *entries)
{
	return le16_to_cpu(((struct dx_countlimit *) entries)->limit);
}

static inline void dx_set_count(struct dx_entry *entries, unsigned value)
{
	((struct dx_countlimit *) entries)->count = cpu_to_le16(value);
}

static inline void dx_set_limit(struct dx_entry *entries, unsigned value)
{
	((struct dx_countlimit *) entries)->limit = cpu_to_le16(value);
}

static inline unsigned dx_root_limit(struct inode *dir, unsigned infosize)
{
	unsigned entry_space = dir->i_sb->s_blocksize - EXT4_DIR_REC_LEN(1) -
		EXT4_DIR_REC_LEN(2) - infosize;

	if (ext4_has_metadata_csum(dir->i_sb))
		entry_space -= sizeof(struct dx_tail);
	return entry_space / sizeof(struct dx_entry);
}

static inline unsigned dx_node_limit(struct inode *dir)
{
	unsigned entry_space = dir->i_sb->s_blocksize - EXT4_DIR_REC_LEN(0);

	if (ext4_has_metadata_csum(dir->i_sb))
		entry_space -= sizeof(struct dx_tail);
	return entry_space / sizeof(struct dx_entry);
}

/*
 * Debug
 */
#ifdef DX_DEBUG
static void dx_show_index(char * label, struct dx_entry *entries)
{
	int i, n = dx_get_count (entries);
	printk(KERN_DEBUG "%s index", label);
	for (i = 0; i < n; i++) {
		printk(KERN_CONT " %x->%lu",
		       i ? dx_get_hash(entries + i) : 0,
		       (unsigned long)dx_get_block(entries + i));
	}
	printk(KERN_CONT "\n");
}

struct stats
{
	unsigned names;
	unsigned space;
	unsigned bcount;
};

static struct stats dx_show_leaf(struct inode *dir,
				struct dx_hash_info *hinfo,
				struct ext4_dir_entry_2 *de,
				int size, int show_names)
{
	unsigned names = 0, space = 0;
	char *base = (char *) de;
	struct dx_hash_info h = *hinfo;

	printk("names: ");
	while ((char *) de < base + size)
	{
		if (de->inode)
		{
			if (show_names)
			{
#ifdef CONFIG_EXT4_FS_ENCRYPTION
				int len;
				char *name;
				struct fscrypt_str fname_crypto_str =
					FSTR_INIT(NULL, 0);
				int res = 0;

				name  = de->name;
				len = de->name_len;
				if (ext4_encrypted_inode(dir))
					res = fscrypt_get_encryption_info(dir);
				if (res) {
					printk(KERN_WARNING "Error setting up"
					       " fname crypto: %d\n", res);
				}
				if (!fscrypt_has_encryption_key(dir)) {
					/* Directory is not encrypted */
					ext4fs_dirhash(de->name,
						de->name_len, &h);
					printk("%*.s:(U)%x.%u ", len,
					       name, h.hash,
					       (unsigned) ((char *) de
							   - base));
				} else {
					struct fscrypt_str de_name =
						FSTR_INIT(name, len);

					/* Directory is encrypted */
					res = fscrypt_fname_alloc_buffer(
						dir, len,
						&fname_crypto_str);
					if (res)
						printk(KERN_WARNING "Error "
							"allocating crypto "
							"buffer--skipping "
							"crypto\n");
					res = fscrypt_fname_disk_to_usr(dir,
						0, 0, &de_name,
						&fname_crypto_str);
					if (res) {
						printk(KERN_WARNING "Error "
							"converting filename "
							"from disk to usr"
							"\n");
						name = "??";
						len = 2;
					} else {
						name = fname_crypto_str.name;
						len = fname_crypto_str.len;
					}
					ext4fs_dirhash(de->name, de->name_len,
						       &h);
					printk("%*.s:(E)%x.%u ", len, name,
					       h.hash, (unsigned) ((char *) de
								   - base));
					fscrypt_fname_free_buffer(
							&fname_crypto_str);
				}
#else
				int len = de->name_len;
				char *name = de->name;
				ext4fs_dirhash(de->name, de->name_len, &h);
				printk("%*.s:%x.%u ", len, name, h.hash,
				       (unsigned) ((char *) de - base));
#endif
			}
			space += EXT4_DIR_REC_LEN(de->name_len);
			names++;
		}
		de = ext4_next_entry(de, size);
	}
	printk(KERN_CONT "(%i)\n", names);
	return (struct stats) { names, space, 1 };
}

struct stats dx_show_entries(struct dx_hash_info *hinfo, struct inode *dir,
			     struct dx_entry *entries, int levels)
{
	unsigned blocksize = dir->i_sb->s_blocksize;
	unsigned count = dx_get_count(entries), names = 0, space = 0, i;
	unsigned bcount = 0;
	struct buffer_head *bh;
	printk("%i indexed blocks...\n", count);
	for (i = 0; i < count; i++, entries++)
	{
		ext4_lblk_t block = dx_get_block(entries);
		ext4_lblk_t hash  = i ? dx_get_hash(entries): 0;
		u32 range = i < count - 1? (dx_get_hash(entries + 1) - hash): ~hash;
		struct stats stats;
		printk("%s%3u:%03u hash %8x/%8x ",levels?"":"   ", i, block, hash, range);
		bh = ext4_bread(NULL,dir, block, 0);
		if (!bh || IS_ERR(bh))
			continue;
		stats = levels?
		   dx_show_entries(hinfo, dir, ((struct dx_node *) bh->b_data)->entries, levels - 1):
		   dx_show_leaf(dir, hinfo, (struct ext4_dir_entry_2 *)
			bh->b_data, blocksize, 0);
		names += stats.names;
		space += stats.space;
		bcount += stats.bcount;
		brelse(bh);
	}
	if (bcount)
		printk(KERN_DEBUG "%snames %u, fullness %u (%u%%)\n",
		       levels ? "" : "   ", names, space/bcount,
		       (space/bcount)*100/blocksize);
	return (struct stats) { names, space, bcount};
}
#endif /* DX_DEBUG */

/*
 * Probe for a directory leaf block to search.
 *
 * dx_probe can return ERR_BAD_DX_DIR, which means there was a format
 * error in the directory index, and the caller should fall back to
 * searching the directory normally.  The callers of dx_probe **MUST**
 * check for this error code, and make sure it never gets reflected
 * back to userspace.
 */
static struct dx_frame *
dx_probe(struct ext4_filename *fname, struct inode *dir,
	 struct dx_hash_info *hinfo, struct dx_frame *frame_in)
{
	unsigned count, indirect;
	struct dx_entry *at, *entries, *p, *q, *m;
	struct dx_root *root;
	struct dx_frame *frame = frame_in;
	struct dx_frame *ret_err = ERR_PTR(ERR_BAD_DX_DIR);
	u32 hash;

	memset(frame_in, 0, EXT4_HTREE_LEVEL * sizeof(frame_in[0]));
	frame->bh = ext4_read_dirblock(dir, 0, INDEX);
	if (IS_ERR(frame->bh))
		return (struct dx_frame *) frame->bh;

	root = (struct dx_root *) frame->bh->b_data;
	if (root->info.hash_version != DX_HASH_TEA &&
	    root->info.hash_version != DX_HASH_HALF_MD4 &&
	    root->info.hash_version != DX_HASH_LEGACY) {
		ext4_warning_inode(dir, "Unrecognised inode hash code %u",
				   root->info.hash_version);
		goto fail;
	}
	if (fname)
		hinfo = &fname->hinfo;
	hinfo->hash_version = root->info.hash_version;
	if (hinfo->hash_version <= DX_HASH_TEA)
		hinfo->hash_version += EXT4_SB(dir->i_sb)->s_hash_unsigned;
	hinfo->seed = EXT4_SB(dir->i_sb)->s_hash_seed;
	if (fname && fname_name(fname))
		ext4fs_dirhash(fname_name(fname), fname_len(fname), hinfo);
	hash = hinfo->hash;

	if (root->info.unused_flags & 1) {
		ext4_warning_inode(dir, "Unimplemented hash flags: %#06x",
				   root->info.unused_flags);
		goto fail;
	}

	indirect = root->info.indirect_levels;
	if (indirect >= ext4_dir_htree_level(dir->i_sb)) {
		ext4_warning(dir->i_sb,
			     "Directory (ino: %lu) htree depth %#06x exceed"
			     "supported value", dir->i_ino,
			     ext4_dir_htree_level(dir->i_sb));
		if (ext4_dir_htree_level(dir->i_sb) < EXT4_HTREE_LEVEL) {
			ext4_warning(dir->i_sb, "Enable large directory "
						"feature to access it");
		}
		goto fail;
	}

	entries = (struct dx_entry *)(((char *)&root->info) +
				      root->info.info_length);

	if (dx_get_limit(entries) != dx_root_limit(dir,
						   root->info.info_length)) {
		ext4_warning_inode(dir, "dx entry: limit %u != root limit %u",
				   dx_get_limit(entries),
				   dx_root_limit(dir, root->info.info_length));
		goto fail;
	}

	dxtrace(printk("Look up %x", hash));
	while (1) {
		count = dx_get_count(entries);
		if (!count || count > dx_get_limit(entries)) {
			ext4_warning_inode(dir,
					   "dx entry: count %u beyond limit %u",
					   count, dx_get_limit(entries));
			goto fail;
		}

		p = entries + 1;
		q = entries + count - 1;
		while (p <= q) {
			m = p + (q - p) / 2;
			dxtrace(printk(KERN_CONT "."));
			if (dx_get_hash(m) > hash)
				q = m - 1;
			else
				p = m + 1;
		}

		if (0) { // linear search cross check
			unsigned n = count - 1;
			at = entries;
			while (n--)
			{
				dxtrace(printk(KERN_CONT ","));
				if (dx_get_hash(++at) > hash)
				{
					at--;
					break;
				}
			}
			assert (at == p - 1);
		}

		at = p - 1;
		dxtrace(printk(KERN_CONT " %x->%u\n",
			       at == entries ? 0 : dx_get_hash(at),
			       dx_get_block(at)));
		frame->entries = entries;
		frame->at = at;
		if (!indirect--)
			return frame;
		frame++;
		frame->bh = ext4_read_dirblock(dir, dx_get_block(at), INDEX);
		if (IS_ERR(frame->bh)) {
			ret_err = (struct dx_frame *) frame->bh;
			frame->bh = NULL;
			goto fail;
		}
		entries = ((struct dx_node *) frame->bh->b_data)->entries;

		if (dx_get_limit(entries) != dx_node_limit(dir)) {
			ext4_warning_inode(dir,
				"dx entry: limit %u != node limit %u",
				dx_get_limit(entries), dx_node_limit(dir));
			goto fail;
		}
	}
fail:
	while (frame >= frame_in) {
		brelse(frame->bh);
		frame--;
	}

	if (ret_err == ERR_PTR(ERR_BAD_DX_DIR))
		ext4_warning_inode(dir,
			"Corrupt directory, running e2fsck is recommended");
	return ret_err;
}

static void dx_release(struct dx_frame *frames)
{
	struct dx_root_info *info;
	int i;

	if (frames[0].bh == NULL)
		return;

	info = &((struct dx_root *)frames[0].bh->b_data)->info;
	for (i = 0; i <= info->indirect_levels; i++) {
		if (frames[i].bh == NULL)
			break;
		brelse(frames[i].bh);
		frames[i].bh = NULL;
	}
}

/*
 * This function increments the frame pointer to search the next leaf
 * block, and reads in the necessary intervening nodes if the search
 * should be necessary.  Whether or not the search is necessary is
 * controlled by the hash parameter.  If the hash value is even, then
 * the search is only continued if the next block starts with that
 * hash value.  This is used if we are searching for a specific file.
 *
 * If the hash value is HASH_NB_ALWAYS, then always go to the next block.
 *
 * This function returns 1 if the caller should continue to search,
 * or 0 if it should not.  If there is an error reading one of the
 * index blocks, it will a negative error code.
 *
 * If start_hash is non-null, it will be filled in with the starting
 * hash of the next page.
 */
static int ext4_htree_next_block(struct inode *dir, __u32 hash,
				 struct dx_frame *frame,
				 struct dx_frame *frames,
				 __u32 *start_hash)
{
	struct dx_frame *p;
	struct buffer_head *bh;
	int num_frames = 0;
	__u32 bhash;

	p = frame;
	/*
	 * Find the next leaf page by incrementing the frame pointer.
	 * If we run out of entries in the interior node, loop around and
	 * increment pointer in the parent node.  When we break out of
	 * this loop, num_frames indicates the number of interior
	 * nodes need to be read.
	 */
	while (1) {
		if (++(p->at) < p->entries + dx_get_count(p->entries))
			break;
		if (p == frames)
			return 0;
		num_frames++;
		p--;
	}

	/*
	 * If the hash is 1, then continue only if the next page has a
	 * continuation hash of any value.  This is used for readdir
	 * handling.  Otherwise, check to see if the hash matches the
	 * desired contiuation hash.  If it doesn't, return since
	 * there's no point to read in the successive index pages.
	 */
	bhash = dx_get_hash(p->at);
	if (start_hash)
		*start_hash = bhash;
	if ((hash & 1) == 0) {
		if ((bhash & ~1) != hash)
			return 0;
	}
	/*
	 * If the hash is HASH_NB_ALWAYS, we always go to the next
	 * block so no check is necessary
	 */
	while (num_frames--) {
		bh = ext4_read_dirblock(dir, dx_get_block(p->at), INDEX);
		if (IS_ERR(bh))
			return PTR_ERR(bh);
		p++;
		brelse(p->bh);
		p->bh = bh;
		p->at = p->entries = ((struct dx_node *) bh->b_data)->entries;
	}
	return 1;
}


/*
 * This function fills a red-black tree with information from a
 * directory block.  It returns the number directory entries loaded
 * into the tree.  If there is an error it is returned in err.
 */
static int htree_dirblock_to_tree(struct file *dir_file,
				  struct inode *dir, ext4_lblk_t block,
				  struct dx_hash_info *hinfo,
				  __u32 start_hash, __u32 start_minor_hash)
{
	struct buffer_head *bh;
	struct ext4_dir_entry_2 *de, *top;
	int err = 0, count = 0;
	struct fscrypt_str fname_crypto_str = FSTR_INIT(NULL, 0), tmp_str;

	dxtrace(printk(KERN_INFO "In htree dirblock_to_tree: block %lu\n",
							(unsigned long)block));
	bh = ext4_read_dirblock(dir, block, DIRENT);
	if (IS_ERR(bh))
		return PTR_ERR(bh);

	de = (struct ext4_dir_entry_2 *) bh->b_data;
	top = (struct ext4_dir_entry_2 *) ((char *) de +
					   dir->i_sb->s_blocksize -
					   EXT4_DIR_REC_LEN(0));
#ifdef CONFIG_EXT4_FS_ENCRYPTION
	/* Check if the directory is encrypted */
	if (ext4_encrypted_inode(dir)) {
		err = fscrypt_get_encryption_info(dir);
		if (err < 0) {
			brelse(bh);
			return err;
		}
		err = fscrypt_fname_alloc_buffer(dir, EXT4_NAME_LEN,
						     &fname_crypto_str);
		if (err < 0) {
			brelse(bh);
			return err;
		}
	}
#endif
	for (; de < top; de = ext4_next_entry(de, dir->i_sb->s_blocksize)) {
		if (ext4_check_dir_entry(dir, NULL, de, bh,
				bh->b_data, bh->b_size,
				(block<<EXT4_BLOCK_SIZE_BITS(dir->i_sb))
					 + ((char *)de - bh->b_data))) {
			/* silently ignore the rest of the block */
			break;
		}
		ext4fs_dirhash(de->name, de->name_len, hinfo);
		if ((hinfo->hash < start_hash) ||
		    ((hinfo->hash == start_hash) &&
		     (hinfo->minor_hash < start_minor_hash)))
			continue;
		if (de->inode == 0)
			continue;
		if (!ext4_encrypted_inode(dir)) {
			tmp_str.name = de->name;
			tmp_str.len = de->name_len;
			err = ext4_htree_store_dirent(dir_file,
				   hinfo->hash, hinfo->minor_hash, de,
				   &tmp_str);
		} else {
			int save_len = fname_crypto_str.len;
			struct fscrypt_str de_name = FSTR_INIT(de->name,
								de->name_len);

			/* Directory is encrypted */
			err = fscrypt_fname_disk_to_usr(dir, hinfo->hash,
					hinfo->minor_hash, &de_name,
					&fname_crypto_str);
			if (err) {
				count = err;
				goto errout;
			}
			err = ext4_htree_store_dirent(dir_file,
				   hinfo->hash, hinfo->minor_hash, de,
					&fname_crypto_str);
			fname_crypto_str.len = save_len;
		}
		if (err != 0) {
			count = err;
			goto errout;
		}
		count++;
	}
errout:
	brelse(bh);
#ifdef CONFIG_EXT4_FS_ENCRYPTION
	fscrypt_fname_free_buffer(&fname_crypto_str);
#endif
	return count;
}


/*
 * This function fills a red-black tree with information from a
 * directory.  We start scanning the directory in hash order, starting
 * at start_hash and start_minor_hash.
 *
 * This function returns the number of entries inserted into the tree,
 * or a negative error code.
 */
int ext4_htree_fill_tree(struct file *dir_file, __u32 start_hash,
			 __u32 start_minor_hash, __u32 *next_hash)
{
	struct dx_hash_info hinfo;
	struct ext4_dir_entry_2 *de;
	struct dx_frame frames[EXT4_HTREE_LEVEL], *frame;
	struct inode *dir;
	ext4_lblk_t block;
	int count = 0;
	int ret, err;
	__u32 hashval;
	struct fscrypt_str tmp_str;

	dxtrace(printk(KERN_DEBUG "In htree_fill_tree, start hash: %x:%x\n",
		       start_hash, start_minor_hash));
	dir = file_inode(dir_file);
	if (!(ext4_test_inode_flag(dir, EXT4_INODE_INDEX))) {
		hinfo.hash_version = EXT4_SB(dir->i_sb)->s_def_hash_version;
		if (hinfo.hash_version <= DX_HASH_TEA)
			hinfo.hash_version +=
				EXT4_SB(dir->i_sb)->s_hash_unsigned;
		hinfo.seed = EXT4_SB(dir->i_sb)->s_hash_seed;
		if (ext4_has_inline_data(dir)) {
			int has_inline_data = 1;
			count = htree_inlinedir_to_tree(dir_file, dir, 0,
							&hinfo, start_hash,
							start_minor_hash,
							&has_inline_data);
			if (has_inline_data) {
				*next_hash = ~0;
				return count;
			}
		}
		count = htree_dirblock_to_tree(dir_file, dir, 0, &hinfo,
					       start_hash, start_minor_hash);
		*next_hash = ~0;
		return count;
	}
	hinfo.hash = start_hash;
	hinfo.minor_hash = 0;
	frame = dx_probe(NULL, dir, &hinfo, frames);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	/* Add '.' and '..' from the htree header */
	if (!start_hash && !start_minor_hash) {
		de = (struct ext4_dir_entry_2 *) frames[0].bh->b_data;
		tmp_str.name = de->name;
		tmp_str.len = de->name_len;
		err = ext4_htree_store_dirent(dir_file, 0, 0,
					      de, &tmp_str);
		if (err != 0)
			goto errout;
		count++;
	}
	if (start_hash < 2 || (start_hash ==2 && start_minor_hash==0)) {
		de = (struct ext4_dir_entry_2 *) frames[0].bh->b_data;
		de = ext4_next_entry(de, dir->i_sb->s_blocksize);
		tmp_str.name = de->name;
		tmp_str.len = de->name_len;
		err = ext4_htree_store_dirent(dir_file, 2, 0,
					      de, &tmp_str);
		if (err != 0)
			goto errout;
		count++;
	}

	while (1) {
		if (fatal_signal_pending(current)) {
			err = -ERESTARTSYS;
			goto errout;
		}
		cond_resched();
		block = dx_get_block(frame->at);
		ret = htree_dirblock_to_tree(dir_file, dir, block, &hinfo,
					     start_hash, start_minor_hash);
		if (ret < 0) {
			err = ret;
			goto errout;
		}
		count += ret;
		hashval = ~0;
		ret = ext4_htree_next_block(dir, HASH_NB_ALWAYS,
					    frame, frames, &hashval);
		*next_hash = hashval;
		if (ret < 0) {
			err = ret;
			goto errout;
		}
		/*
		 * Stop if:  (a) there are no more entries, or
		 * (b) we have inserted at least one entry and the
		 * next hash value is not a continuation
		 */
		if ((ret == 0) ||
		    (count && ((hashval & 1) == 0)))
			break;
	}
	dx_release(frames);
	dxtrace(printk(KERN_DEBUG "Fill tree: returned %d entries, "
		       "next hash: %x\n", count, *next_hash));
	return count;
errout:
	dx_release(frames);
	return (err);
}

static inline int search_dirblock(struct buffer_head *bh,
				  struct inode *dir,
				  struct ext4_filename *fname,
				  unsigned int offset,
				  struct ext4_dir_entry_2 **res_dir)
{
	return ext4_search_dir(bh, bh->b_data, dir->i_sb->s_blocksize, dir,
			       fname, offset, res_dir);
}

/*
 * Directory block splitting, compacting
 */

/*
 * Create map of hash values, offsets, and sizes, stored at end of block.
 * Returns number of entries mapped.
 */
static int dx_make_map(struct inode *dir, struct ext4_dir_entry_2 *de,
		       unsigned blocksize, struct dx_hash_info *hinfo,
		       struct dx_map_entry *map_tail)
{
	int count = 0;
	char *base = (char *) de;
	struct dx_hash_info h = *hinfo;

	while ((char *) de < base + blocksize) {
		if (de->name_len && de->inode) {
			ext4fs_dirhash(de->name, de->name_len, &h);
			map_tail--;
			map_tail->hash = h.hash;
			map_tail->offs = ((char *) de - base)>>2;
			map_tail->size = le16_to_cpu(de->rec_len);
			count++;
			cond_resched();
		}
		/* XXX: do we need to check rec_len == 0 case? -Chris */
		de = ext4_next_entry(de, blocksize);
	}
	return count;
}

/* Sort map by hash value */
static void dx_sort_map (struct dx_map_entry *map, unsigned count)
{
	struct dx_map_entry *p, *q, *top = map + count - 1;
	int more;
	/* Combsort until bubble sort doesn't suck */
	while (count > 2) {
		count = count*10/13;
		if (count - 9 < 2) /* 9, 10 -> 11 */
			count = 11;
		for (p = top, q = p - count; q >= map; p--, q--)
			if (p->hash < q->hash)
				swap(*p, *q);
	}
	/* Garden variety bubble sort */
	do {
		more = 0;
		q = top;
		while (q-- > map) {
			if (q[1].hash >= q[0].hash)
				continue;
			swap(*(q+1), *q);
			more = 1;
		}
	} while(more);
}

static void dx_insert_block(struct dx_frame *frame, u32 hash, ext4_lblk_t block)
{
	struct dx_entry *entries = frame->entries;
	struct dx_entry *old = frame->at, *new = old + 1;
	int count = dx_get_count(entries);

	assert(count < dx_get_limit(entries));
	assert(old < entries + count);
	memmove(new + 1, new, (char *)(entries + count) - (char *)(new));
	dx_set_hash(new, hash);
	dx_set_block(new, block);
	dx_set_count(entries, count + 1);
}

/*
 * Test whether a directory entry matches the filename being searched for.
 *
 * Return: %true if the directory entry matches, otherwise %false.
 */
static inline bool ext4_match(const struct ext4_filename *fname,
			      const struct ext4_dir_entry_2 *de)
{
	struct fscrypt_name f;

	if (!de->inode)
		return false;

	f.usr_fname = fname->usr_fname;
	f.disk_name = fname->disk_name;
#ifdef CONFIG_EXT4_FS_ENCRYPTION
	f.crypto_buf = fname->crypto_buf;
#endif
	return fscrypt_match_name(&f, de->name, de->name_len);
}

/*
 * Returns 0 if not found, -1 on failure, and 1 on success
 */
int ext4_search_dir(struct buffer_head *bh, char *search_buf, int buf_size,
		    struct inode *dir, struct ext4_filename *fname,
		    unsigned int offset, struct ext4_dir_entry_2 **res_dir)
{
	struct ext4_dir_entry_2 * de;
	char * dlimit;
	int de_len;

	de = (struct ext4_dir_entry_2 *)search_buf;
	dlimit = search_buf + buf_size;
	while ((char *) de < dlimit) {
		/* this code is executed quadratically often */
		/* do minimal checking `by hand' */
		if ((char *) de + de->name_len <= dlimit &&
		    ext4_match(fname, de)) {
			/* found a match - just to be sure, do
			 * a full check */
			if (ext4_check_dir_entry(dir, NULL, de, bh, bh->b_data,
						 bh->b_size, offset))
				return -1;
			*res_dir = de;
			return 1;
		}
		/* prevent looping on a bad block */
		de_len = ext4_rec_len_from_disk(de->rec_len,
						dir->i_sb->s_blocksize);
		if (de_len <= 0)
			return -1;
		offset += de_len;
		de = (struct ext4_dir_entry_2 *) ((char *) de + de_len);
	}
	return 0;
}

static int is_dx_internal_node(struct inode *dir, ext4_lblk_t block,
			       struct ext4_dir_entry *de)
{
	struct super_block *sb = dir->i_sb;

	if (!is_dx(dir))
		return 0;
	if (block == 0)
		return 1;
	if (de->inode == 0 &&
	    ext4_rec_len_from_disk(de->rec_len, sb->s_blocksize) ==
			sb->s_blocksize)
		return 1;
	return 0;
}

/*
 *	ext4_find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the cache buffer in which the entry was found, and the entry
 * itself (as a parameter - res_dir). It does NOT read the inode of the
 * entry - you'll have to do that yourself if you want to.
 *
 * The returned buffer_head has ->b_count elevated.  The caller is expected
 * to brelse() it when appropriate.
 */
static struct buffer_head * ext4_find_entry (struct inode *dir,
					const struct qstr *d_name,
					struct ext4_dir_entry_2 **res_dir,
					int *inlined)
{
	struct super_block *sb;
	struct buffer_head *bh_use[NAMEI_RA_SIZE];
	struct buffer_head *bh, *ret = NULL;
	ext4_lblk_t start, block;
	const u8 *name = d_name->name;
	size_t ra_max = 0;	/* Number of bh's in the readahead
				   buffer, bh_use[] */
	size_t ra_ptr = 0;	/* Current index into readahead
				   buffer */
	ext4_lblk_t  nblocks;
	int i, namelen, retval;
	struct ext4_filename fname;

	*res_dir = NULL;
	sb = dir->i_sb;
	namelen = d_name->len;
	if (namelen > EXT4_NAME_LEN)
		return NULL;

	retval = ext4_fname_setup_filename(dir, d_name, 1, &fname);
	if (retval == -ENOENT)
		return NULL;
	if (retval)
		return ERR_PTR(retval);

	if (ext4_has_inline_data(dir)) {
		int has_inline_data = 1;
		ret = ext4_find_inline_entry(dir, &fname, res_dir,
					     &has_inline_data);
		if (has_inline_data) {
			if (inlined)
				*inlined = 1;
			goto cleanup_and_exit;
		}
	}

	if ((namelen <= 2) && (name[0] == '.') &&
	    (name[1] == '.' || name[1] == '\0')) {
		/*
		 * "." or ".." will only be in the first block
		 * NFS may look up ".."; "." should be handled by the VFS
		 */
		block = start = 0;
		nblocks = 1;
		goto restart;
	}
	if (is_dx(dir)) {
		ret = ext4_dx_find_entry(dir, &fname, res_dir);
		/*
		 * On success, or if the error was file not found,
		 * return.  Otherwise, fall back to doing a search the
		 * old fashioned way.
		 */
		if (!IS_ERR(ret) || PTR_ERR(ret) != ERR_BAD_DX_DIR)
			goto cleanup_and_exit;
		dxtrace(printk(KERN_DEBUG "ext4_find_entry: dx failed, "
			       "falling back\n"));
	}
	nblocks = dir->i_size >> EXT4_BLOCK_SIZE_BITS(sb);
	if (!nblocks) {
		ret = NULL;
		goto cleanup_and_exit;
	}
	start = EXT4_I(dir)->i_dir_start_lookup;
	if (start >= nblocks)
		start = 0;
	block = start;
restart:
	do {
		/*
		 * We deal with the read-ahead logic here.
		 */
		if (ra_ptr >= ra_max) {
			/* Refill the readahead buffer */
			ra_ptr = 0;
			if (block < start)
				ra_max = start - block;
			else
				ra_max = nblocks - block;
			ra_max = min(ra_max, ARRAY_SIZE(bh_use));
			retval = ext4_bread_batch(dir, block, ra_max,
						  false /* wait */, bh_use);
			if (retval) {
				ret = ERR_PTR(retval);
				ra_max = 0;
				goto cleanup_and_exit;
			}
		}
		if ((bh = bh_use[ra_ptr++]) == NULL)
			goto next;
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh)) {
			EXT4_ERROR_INODE(dir, "reading directory lblock %lu",
					 (unsigned long) block);
			brelse(bh);
			ret = ERR_PTR(-EIO);
			goto cleanup_and_exit;
		}
		if (!buffer_verified(bh) &&
		    !is_dx_internal_node(dir, block,
					 (struct ext4_dir_entry *)bh->b_data) &&
		    !ext4_dirent_csum_verify(dir,
				(struct ext4_dir_entry *)bh->b_data)) {
			EXT4_ERROR_INODE(dir, "checksumming directory "
					 "block %lu", (unsigned long)block);
			brelse(bh);
			ret = ERR_PTR(-EFSBADCRC);
			goto cleanup_and_exit;
		}
		set_buffer_verified(bh);
		i = search_dirblock(bh, dir, &fname,
			    block << EXT4_BLOCK_SIZE_BITS(sb), res_dir);
		if (i == 1) {
			EXT4_I(dir)->i_dir_start_lookup = block;
			ret = bh;
			goto cleanup_and_exit;
		} else {
			brelse(bh);
			if (i < 0)
				goto cleanup_and_exit;
		}
	next:
		if (++block >= nblocks)
			block = 0;
	} while (block != start);

	/*
	 * If the directory has grown while we were searching, then
	 * search the last part of the directory before giving up.
	 */
	block = nblocks;
	nblocks = dir->i_size >> EXT4_BLOCK_SIZE_BITS(sb);
	if (block < nblocks) {
		start = 0;
		goto restart;
	}

cleanup_and_exit:
	/* Clean up the read-ahead blocks */
	for (; ra_ptr < ra_max; ra_ptr++)
		brelse(bh_use[ra_ptr]);
	ext4_fname_free_filename(&fname);
	return ret;
}

static struct buffer_head * ext4_dx_find_entry(struct inode *dir,
			struct ext4_filename *fname,
			struct ext4_dir_entry_2 **res_dir)
{
	struct super_block * sb = dir->i_sb;
	struct dx_frame frames[EXT4_HTREE_LEVEL], *frame;
	struct buffer_head *bh;
	ext4_lblk_t block;
	int retval;

#ifdef CONFIG_EXT4_FS_ENCRYPTION
	*res_dir = NULL;
#endif
	frame = dx_probe(fname, dir, NULL, frames);
	if (IS_ERR(frame))
		return (struct buffer_head *) frame;
	do {
		block = dx_get_block(frame->at);
		bh = ext4_read_dirblock(dir, block, DIRENT);
		if (IS_ERR(bh))
			goto errout;

		retval = search_dirblock(bh, dir, fname,
					 block << EXT4_BLOCK_SIZE_BITS(sb),
					 res_dir);
		if (retval == 1)
			goto success;
		brelse(bh);
		if (retval == -1) {
			bh = ERR_PTR(ERR_BAD_DX_DIR);
			goto errout;
		}

		/* Check to see if we should continue to search */
		retval = ext4_htree_next_block(dir, fname->hinfo.hash, frame,
					       frames, NULL);
		if (retval < 0) {
			ext4_warning_inode(dir,
				"error %d reading directory index block",
				retval);
			bh = ERR_PTR(retval);
			goto errout;
		}
	} while (retval == 1);

	bh = NULL;
errout:
	dxtrace(printk(KERN_DEBUG "%s not found\n", fname->usr_fname->name));
success:
	dx_release(frames);
	return bh;
}

static struct dentry *ext4_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
	struct inode *inode;
	struct ext4_dir_entry_2 *de;
	struct buffer_head *bh;

	if (ext4_encrypted_inode(dir)) {
		int res = fscrypt_get_encryption_info(dir);

		/*
		 * DCACHE_ENCRYPTED_WITH_KEY is set if the dentry is
		 * created while the directory was encrypted and we
		 * have access to the key.
		 */
		if (fscrypt_has_encryption_key(dir))
			fscrypt_set_encrypted_dentry(dentry);
		fscrypt_set_d_op(dentry);
		if (res && res != -ENOKEY)
			return ERR_PTR(res);
	}

       if (dentry->d_name.len > EXT4_NAME_LEN)
	       return ERR_PTR(-ENAMETOOLONG);

	bh = ext4_find_entry(dir, &dentry->d_name, &de, NULL);
	if (IS_ERR(bh))
		return (struct dentry *) bh;
	inode = NULL;
	if (bh) {
		__u32 ino = le32_to_cpu(de->inode);
		brelse(bh);
		if (!ext4_valid_inum(dir->i_sb, ino)) {
			EXT4_ERROR_INODE(dir, "bad inode number: %u", ino);
			return ERR_PTR(-EFSCORRUPTED);
		}
		if (unlikely(ino == dir->i_ino)) {
			EXT4_ERROR_INODE(dir, "'%pd' linked to parent dir",
					 dentry);
			return ERR_PTR(-EFSCORRUPTED);
		}
		inode = ext4_iget_normal(dir->i_sb, ino);
		if (inode == ERR_PTR(-ESTALE)) {
			EXT4_ERROR_INODE(dir,
					 "deleted inode referenced: %u",
					 ino);
			return ERR_PTR(-EFSCORRUPTED);
		}
		if (!IS_ERR(inode) && ext4_encrypted_inode(dir) &&
		    (S_ISDIR(inode->i_mode) || S_ISLNK(inode->i_mode)) &&
		    !fscrypt_has_permitted_context(dir, inode)) {
			ext4_warning(inode->i_sb,
				     "Inconsistent encryption contexts: %lu/%lu",
				     dir->i_ino, inode->i_ino);
			iput(inode);
			return ERR_PTR(-EPERM);
		}
	}
	return d_splice_alias(inode, dentry);
}


struct dentry *ext4_get_parent(struct dentry *child)
{
	__u32 ino;
	static const struct qstr dotdot = QSTR_INIT("..", 2);
	struct ext4_dir_entry_2 * de;
	struct buffer_head *bh;

	bh = ext4_find_entry(d_inode(child), &dotdot, &de, NULL);
	if (IS_ERR(bh))
		return (struct dentry *) bh;
	if (!bh)
		return ERR_PTR(-ENOENT);
	ino = le32_to_cpu(de->inode);
	brelse(bh);

	if (!ext4_valid_inum(child->d_sb, ino)) {
		EXT4_ERROR_INODE(d_inode(child),
				 "bad parent inode number: %u", ino);
		return ERR_PTR(-EFSCORRUPTED);
	}

	return d_obtain_alias(ext4_iget_normal(child->d_sb, ino));
}

/*
 * Move count entries from end of map between two memory locations.
 * Returns pointer to last entry moved.
 */
static struct ext4_dir_entry_2 *
dx_move_dirents(char *from, char *to, struct dx_map_entry *map, int count,
		unsigned blocksize)
{
	unsigned rec_len = 0;

	while (count--) {
		struct ext4_dir_entry_2 *de = (struct ext4_dir_entry_2 *)
						(from + (map->offs<<2));
		rec_len = EXT4_DIR_REC_LEN(de->name_len);
		memcpy (to, de, rec_len);
		((struct ext4_dir_entry_2 *) to)->rec_len =
				ext4_rec_len_to_disk(rec_len, blocksize);
		de->inode = 0;
		map++;
		to += rec_len;
	}
	return (struct ext4_dir_entry_2 *) (to - rec_len);
}

/*
 * Compact each dir entry in the range to the minimal rec_len.
 * Returns pointer to last entry in range.
 */
static struct ext4_dir_entry_2* dx_pack_dirents(char *base, unsigned blocksize)
{
	struct ext4_dir_entry_2 *next, *to, *prev, *de = (struct ext4_dir_entry_2 *) base;
	unsigned rec_len = 0;

	prev = to = de;
	while ((char*)de < base + blocksize) {
		next = ext4_next_entry(de, blocksize);
		if (de->inode && de->name_len) {
			rec_len = EXT4_DIR_REC_LEN(de->name_len);
			if (de > to)
				memmove(to, de, rec_len);
			to->rec_len = ext4_rec_len_to_disk(rec_len, blocksize);
			prev = to;
			to = (struct ext4_dir_entry_2 *) (((char *) to) + rec_len);
		}
		de = next;
	}
	return prev;
}

/*
 * Split a full leaf block to make room for a new dir entry.
 * Allocate a new block, and move entries so that they are approx. equally full.
 * Returns pointer to de in block into which the new entry will be inserted.
 */
static struct ext4_dir_entry_2 *do_split(handle_t *handle, struct inode *dir,
			struct buffer_head **bh,struct dx_frame *frame,
			struct dx_hash_info *hinfo)
{
	unsigned blocksize = dir->i_sb->s_blocksize;
	unsigned count, continued;
	struct buffer_head *bh2;
	ext4_lblk_t newblock;
	u32 hash2;
	struct dx_map_entry *map;
	char *data1 = (*bh)->b_data, *data2;
	unsigned split, move, size;
	struct ext4_dir_entry_2 *de = NULL, *de2;
	struct ext4_dir_entry_tail *t;
	int	csum_size = 0;
	int	err = 0, i;

	if (ext4_has_metadata_csum(dir->i_sb))
		csum_size = sizeof(struct ext4_dir_entry_tail);

	bh2 = ext4_append(handle, dir, &newblock);
	if (IS_ERR(bh2)) {
		brelse(*bh);
		*bh = NULL;
		return (struct ext4_dir_entry_2 *) bh2;
	}

	BUFFER_TRACE(*bh, "get_write_access");
	err = ext4_journal_get_write_access(handle, *bh);
	if (err)
		goto journal_error;

	BUFFER_TRACE(frame->bh, "get_write_access");
	err = ext4_journal_get_write_access(handle, frame->bh);
	if (err)
		goto journal_error;

	data2 = bh2->b_data;

	/* create map in the end of data2 block */
	map = (struct dx_map_entry *) (data2 + blocksize);
	count = dx_make_map(dir, (struct ext4_dir_entry_2 *) data1,
			     blocksize, hinfo, map);
	map -= count;
	dx_sort_map(map, count);
	/* Split the existing block in the middle, size-wise */
	size = 0;
	move = 0;
	for (i = count-1; i >= 0; i--) {
		/* is more than half of this entry in 2nd half of the block? */
		if (size + map[i].size/2 > blocksize/2)
			break;
		size += map[i].size;
		move++;
	}
	/* map index at which we will split */
	split = count - move;
	hash2 = map[split].hash;
	continued = hash2 == map[split - 1].hash;
	dxtrace(printk(KERN_INFO "Split block %lu at %x, %i/%i\n",
			(unsigned long)dx_get_block(frame->at),
					hash2, split, count-split));

	/* Fancy dance to stay within two buffers */
	de2 = dx_move_dirents(data1, data2, map + split, count - split,
			      blocksize);
	de = dx_pack_dirents(data1, blocksize);
	de->rec_len = ext4_rec_len_to_disk(data1 + (blocksize - csum_size) -
					   (char *) de,
					   blocksize);
	de2->rec_len = ext4_rec_len_to_disk(data2 + (blocksize - csum_size) -
					    (char *) de2,
					    blocksize);
	if (csum_size) {
		t = EXT4_DIRENT_TAIL(data2, blocksize);
		initialize_dirent_tail(t, blocksize);

		t = EXT4_DIRENT_TAIL(data1, blocksize);
		initialize_dirent_tail(t, blocksize);
	}

	dxtrace(dx_show_leaf(dir, hinfo, (struct ext4_dir_entry_2 *) data1,
			blocksize, 1));
	dxtrace(dx_show_leaf(dir, hinfo, (struct ext4_dir_entry_2 *) data2,
			blocksize, 1));

	/* Which block gets the new entry? */
	if (hinfo->hash >= hash2) {
		swap(*bh, bh2);
		de = de2;
	}
	dx_insert_block(frame, hash2 + continued, newblock);
	err = ext4_handle_dirty_dirent_node(handle, dir, bh2);
	if (err)
		goto journal_error;
	err = ext4_handle_dirty_dx_node(handle, dir, frame->bh);
	if (err)
		goto journal_error;
	brelse(bh2);
	dxtrace(dx_show_index("frame", frame->entries));
	return de;

journal_error:
	brelse(*bh);
	brelse(bh2);
	*bh = NULL;
	ext4_std_error(dir->i_sb, err);
	return ERR_PTR(err);
}

int ext4_find_dest_de(struct inode *dir, struct inode *inode,
		      struct buffer_head *bh,
		      void *buf, int buf_size,
		      struct ext4_filename *fname,
		      struct ext4_dir_entry_2 **dest_de)
{
	struct ext4_dir_entry_2 *de;
	unsigned short reclen = EXT4_DIR_REC_LEN(fname_len(fname));
	int nlen, rlen;
	unsigned int offset = 0;
	char *top;

	de = (struct ext4_dir_entry_2 *)buf;
	top = buf + buf_size - reclen;
	while ((char *) de <= top) {
		if (ext4_check_dir_entry(dir, NULL, de, bh,
					 buf, buf_size, offset))
			return -EFSCORRUPTED;
		if (ext4_match(fname, de))
			return -EEXIST;
		nlen = EXT4_DIR_REC_LEN(de->name_len);
		rlen = ext4_rec_len_from_disk(de->rec_len, buf_size);
		if ((de->inode ? rlen - nlen : rlen) >= reclen)
			break;
		de = (struct ext4_dir_entry_2 *)((char *)de + rlen);
		offset += rlen;
	}
	if ((char *) de > top)
		return -ENOSPC;

	*dest_de = de;
	return 0;
}

void ext4_insert_dentry(struct inode *inode,
			struct ext4_dir_entry_2 *de,
			int buf_size,
			struct ext4_filename *fname)
{

	int nlen, rlen;

	nlen = EXT4_DIR_REC_LEN(de->name_len);
	rlen = ext4_rec_len_from_disk(de->rec_len, buf_size);
	if (de->inode) {
		struct ext4_dir_entry_2 *de1 =
			(struct ext4_dir_entry_2 *)((char *)de + nlen);
		de1->rec_len = ext4_rec_len_to_disk(rlen - nlen, buf_size);
		de->rec_len = ext4_rec_len_to_disk(nlen, buf_size);
		de = de1;
	}
	de->file_type = EXT4_FT_UNKNOWN;
	de->inode = cpu_to_le32(inode->i_ino);
	ext4_set_de_type(inode->i_sb, de, inode->i_mode);
	de->name_len = fname_len(fname);
	memcpy(de->name, fname_name(fname), fname_len(fname));
}

/*
 * Add a new entry into a directory (leaf) block.  If de is non-NULL,
 * it points to a directory entry which is guaranteed to be large
 * enough for new directory entry.  If de is NULL, then
 * add_dirent_to_buf will attempt search the directory block for
 * space.  It will return -ENOSPC if no space is available, and -EIO
 * and -EEXIST if directory entry already exists.
 */
static int add_dirent_to_buf(handle_t *handle, struct ext4_filename *fname,
			     struct inode *dir,
			     struct inode *inode, struct ext4_dir_entry_2 *de,
			     struct buffer_head *bh)
{
	unsigned int	blocksize = dir->i_sb->s_blocksize;
	int		csum_size = 0;
	int		err;

	if (ext4_has_metadata_csum(inode->i_sb))
		csum_size = sizeof(struct ext4_dir_entry_tail);

	if (!de) {
		err = ext4_find_dest_de(dir, inode, bh, bh->b_data,
					blocksize - csum_size, fname, &de);
		if (err)
			return err;
	}
	BUFFER_TRACE(bh, "get_write_access");
	err = ext4_journal_get_write_access(handle, bh);
	if (err) {
		ext4_std_error(dir->i_sb, err);
		return err;
	}

	/* By now the buffer is marked for journaling */
	ext4_insert_dentry(inode, de, blocksize, fname);

	/*
	 * XXX shouldn't update any times until successful
	 * completion of syscall, but too many callers depend
	 * on this.
	 *
	 * XXX similarly, too many callers depend on
	 * ext4_new_inode() setting the times, but error
	 * recovery deletes the inode, so the worst that can
	 * happen is that the times are slightly out of date
	 * and/or different from the directory change time.
	 */
	dir->i_mtime = dir->i_ctime = current_time(dir);
	ext4_update_dx_flag(dir);
	inode_inc_iversion(dir);
	ext4_mark_inode_dirty(handle, dir);
	BUFFER_TRACE(bh, "call ext4_handle_dirty_metadata");
	err = ext4_handle_dirty_dirent_node(handle, dir, bh);
	if (err)
		ext4_std_error(dir->i_sb, err);
	return 0;
}

/*
 * This converts a one block unindexed directory to a 3 block indexed
 * directory, and adds the dentry to the indexed directory.
 */
static int make_indexed_dir(handle_t *handle, struct ext4_filename *fname,
			    struct inode *dir,
			    struct inode *inode, struct buffer_head *bh)
{
	struct buffer_head *bh2;
	struct dx_root	*root;
	struct dx_frame	frames[EXT4_HTREE_LEVEL], *frame;
	struct dx_entry *entries;
	struct ext4_dir_entry_2	*de, *de2;
	struct ext4_dir_entry_tail *t;
	char		*data1, *top;
	unsigned	len;
	int		retval;
	unsigned	blocksize;
	ext4_lblk_t  block;
	struct fake_dirent *fde;
	int csum_size = 0;

	if (ext4_has_metadata_csum(inode->i_sb))
		csum_size = sizeof(struct ext4_dir_entry_tail);

	blocksize =  dir->i_sb->s_blocksize;
	dxtrace(printk(KERN_DEBUG "Creating index: inode %lu\n", dir->i_ino));
	BUFFER_TRACE(bh, "get_write_access");
	retval = ext4_journal_get_write_access(handle, bh);
	if (retval) {
		ext4_std_error(dir->i_sb, retval);
		brelse(bh);
		return retval;
	}
	root = (struct dx_root *) bh->b_data;

	/* The 0th block becomes the root, move the dirents out */
	fde = &root->dotdot;
	de = (struct ext4_dir_entry_2 *)((char *)fde +
		ext4_rec_len_from_disk(fde->rec_len, blocksize));
	if ((char *) de >= (((char *) root) + blocksize)) {
		EXT4_ERROR_INODE(dir, "invalid rec_len for '..'");
		brelse(bh);
		return -EFSCORRUPTED;
	}
	len = ((char *) root) + (blocksize - csum_size) - (char *) de;

	/* Allocate new block for the 0th block's dirents */
	bh2 = ext4_append(handle, dir, &block);
	if (IS_ERR(bh2)) {
		brelse(bh);
		return PTR_ERR(bh2);
	}
	ext4_set_inode_flag(dir, EXT4_INODE_INDEX);
	data1 = bh2->b_data;

	memcpy (data1, de, len);
	de = (struct ext4_dir_entry_2 *) data1;
	top = data1 + len;
	while ((char *)(de2 = ext4_next_entry(de, blocksize)) < top)
		de = de2;
	de->rec_len = ext4_rec_len_to_disk(data1 + (blocksize - csum_size) -
					   (char *) de,
					   blocksize);

	if (csum_size) {
		t = EXT4_DIRENT_TAIL(data1, blocksize);
		initialize_dirent_tail(t, blocksize);
	}

	/* Initialize the root; the dot dirents already exist */
	de = (struct ext4_dir_entry_2 *) (&root->dotdot);
	de->rec_len = ext4_rec_len_to_disk(blocksize - EXT4_DIR_REC_LEN(2),
					   blocksize);
	memset (&root->info, 0, sizeof(root->info));
	root->info.info_length = sizeof(root->info);
	root->info.hash_version = EXT4_SB(dir->i_sb)->s_def_hash_version;
	entries = root->entries;
	dx_set_block(entries, 1);
	dx_set_count(entries, 1);
	dx_set_limit(entries, dx_root_limit(dir, sizeof(root->info)));

	/* Initialize as for dx_probe */
	fname->hinfo.hash_version = root->info.hash_version;
	if (fname->hinfo.hash_version <= DX_HASH_TEA)
		fname->hinfo.hash_version += EXT4_SB(dir->i_sb)->s_hash_unsigned;
	fname->hinfo.seed = EXT4_SB(dir->i_sb)->s_hash_seed;
	ext4fs_dirhash(fname_name(fname), fname_len(fname), &fname->hinfo);

	memset(frames, 0, sizeof(frames));
	frame = frames;
	frame->entries = entries;
	frame->at = entries;
	frame->bh = bh;

	retval = ext4_handle_dirty_dx_node(handle, dir, frame->bh);
	if (retval)
		goto out_frames;	
	retval = ext4_handle_dirty_dirent_node(handle, dir, bh2);
	if (retval)
		goto out_frames;	

	de = do_split(handle,dir, &bh2, frame, &fname->hinfo);
	if (IS_ERR(de)) {
		retval = PTR_ERR(de);
		goto out_frames;
	}

	retval = add_dirent_to_buf(handle, fname, dir, inode, de, bh2);
out_frames:
	/*
	 * Even if the block split failed, we have to properly write
	 * out all the changes we did so far. Otherwise we can end up
	 * with corrupted filesystem.
	 */
	if (retval)
		ext4_mark_inode_dirty(handle, dir);
	dx_release(frames);
	brelse(bh2);
	return retval;
}

/*
 *	ext4_add_entry()
 *
 * adds a file entry to the specified directory, using the same
 * semantics as ext4_find_entry(). It returns NULL if it failed.
 *
 * NOTE!! The inode part of 'de' is left at 0 - which means you
 * may not sleep between calling this and putting something into
 * the entry, as someone else might have used it while you slept.
 */
static int ext4_add_entry(handle_t *handle, struct dentry *dentry,
			  struct inode *inode)
{
	struct inode *dir = d_inode(dentry->d_parent);
	struct buffer_head *bh = NULL;
	struct ext4_dir_entry_2 *de;
	struct ext4_dir_entry_tail *t;
	struct super_block *sb;
	struct ext4_filename fname;
	int	retval;
	int	dx_fallback=0;
	unsigned blocksize;
	ext4_lblk_t block, blocks;
	int	csum_size = 0;

	if (ext4_has_metadata_csum(inode->i_sb))
		csum_size = sizeof(struct ext4_dir_entry_tail);

	sb = dir->i_sb;
	blocksize = sb->s_blocksize;
	if (!dentry->d_name.len)
		return -EINVAL;

	retval = ext4_fname_setup_filename(dir, &dentry->d_name, 0, &fname);
	if (retval)
		return retval;

	if (ext4_has_inline_data(dir)) {
		retval = ext4_try_add_inline_entry(handle, &fname, dir, inode);
		if (retval < 0)
			goto out;
		if (retval == 1) {
			retval = 0;
			goto out;
		}
	}

	if (is_dx(dir)) {
		retval = ext4_dx_add_entry(handle, &fname, dir, inode);
		if (!retval || (retval != ERR_BAD_DX_DIR))
			goto out;
		ext4_clear_inode_flag(dir, EXT4_INODE_INDEX);
		dx_fallback++;
		ext4_mark_inode_dirty(handle, dir);
	}
	blocks = dir->i_size >> sb->s_blocksize_bits;
	for (block = 0; block < blocks; block++) {
		bh = ext4_read_dirblock(dir, block, DIRENT);
		if (IS_ERR(bh)) {
			retval = PTR_ERR(bh);
			bh = NULL;
			goto out;
		}
		retval = add_dirent_to_buf(handle, &fname, dir, inode,
					   NULL, bh);
		if (retval != -ENOSPC)
			goto out;

		if (blocks == 1 && !dx_fallback &&
		    ext4_has_feature_dir_index(sb)) {
			retval = make_indexed_dir(handle, &fname, dir,
						  inode, bh);
			bh = NULL; /* make_indexed_dir releases bh */
			goto out;
		}
		brelse(bh);
	}
	bh = ext4_append(handle, dir, &block);
	if (IS_ERR(bh)) {
		retval = PTR_ERR(bh);
		bh = NULL;
		goto out;
	}
	de = (struct ext4_dir_entry_2 *) bh->b_data;
	de->inode = 0;
	de->rec_len = ext4_rec_len_to_disk(blocksize - csum_size, blocksize);

	if (csum_size) {
		t = EXT4_DIRENT_TAIL(bh->b_data, blocksize);
		initialize_dirent_tail(t, blocksize);
	}

	retval = add_dirent_to_buf(handle, &fname, dir, inode, de, bh);
out:
	ext4_fname_free_filename(&fname);
	brelse(bh);
	if (retval == 0)
		ext4_set_inode_state(inode, EXT4_STATE_NEWENTRY);
	return retval;
}

/*
 * Returns 0 for success, or a negative error value
 */
static int ext4_dx_add_entry(handle_t *handle, struct ext4_filename *fname,
			     struct inode *dir, struct inode *inode)
{
	struct dx_frame frames[EXT4_HTREE_LEVEL], *frame;
	struct dx_entry *entries, *at;
	struct buffer_head *bh;
	struct super_block *sb = dir->i_sb;
	struct ext4_dir_entry_2 *de;
	int restart;
	int err;

again:
	restart = 0;
	frame = dx_probe(fname, dir, NULL, frames);
	if (IS_ERR(frame))
		return PTR_ERR(frame);
	entries = frame->entries;
	at = frame->at;
	bh = ext4_read_dirblock(dir, dx_get_block(frame->at), DIRENT);
	if (IS_ERR(bh)) {
		err = PTR_ERR(bh);
		bh = NULL;
		goto cleanup;
	}

	BUFFER_TRACE(bh, "get_write_access");
	err = ext4_journal_get_write_access(handle, bh);
	if (err)
		goto journal_error;

	err = add_dirent_to_buf(handle, fname, dir, inode, NULL, bh);
	if (err != -ENOSPC)
		goto cleanup;

	err = 0;
	/* Block full, should compress but for now just split */
	dxtrace(printk(KERN_DEBUG "using %u of %u node entries\n",
		       dx_get_count(entries), dx_get_limit(entries)));
	/* Need to split index? */
	if (dx_get_count(entries) == dx_get_limit(entries)) {
		ext4_lblk_t newblock;
		int levels = frame - frames + 1;
		unsigned int icount;
		int add_level = 1;
		struct dx_entry *entries2;
		struct dx_node *node2;
		struct buffer_head *bh2;

		while (frame > frames) {
			if (dx_get_count((frame - 1)->entries) <
			    dx_get_limit((frame - 1)->entries)) {
				add_level = 0;
				break;
			}
			frame--; /* split higher index block */
			at = frame->at;
			entries = frame->entries;
			restart = 1;
		}
		if (add_level && levels == ext4_dir_htree_level(sb)) {
			ext4_warning(sb, "Directory (ino: %lu) index full, "
					 "reach max htree level :%d",
					 dir->i_ino, levels);
			if (ext4_dir_htree_level(sb) < EXT4_HTREE_LEVEL) {
				ext4_warning(sb, "Large directory feature is "
						 "not enabled on this "
						 "filesystem");
			}
			err = -ENOSPC;
			goto cleanup;
		}
		icount = dx_get_count(entries);
		bh2 = ext4_append(handle, dir, &newblock);
		if (IS_ERR(bh2)) {
			err = PTR_ERR(bh2);
			goto cleanup;
		}
		node2 = (struct dx_node *)(bh2->b_data);
		entries2 = node2->entries;
		memset(&node2->fake, 0, sizeof(struct fake_dirent));
		node2->fake.rec_len = ext4_rec_len_to_disk(sb->s_blocksize,
							   sb->s_blocksize);
		BUFFER_TRACE(frame->bh, "get_write_access");
		err = ext4_journal_get_write_access(handle, frame->bh);
		if (err)
			goto journal_error;
		if (!add_level) {
			unsigned icount1 = icount/2, icount2 = icount - icount1;
			unsigned hash2 = dx_get_hash(entries + icount1);
			dxtrace(printk(KERN_DEBUG "Split index %i/%i\n",
				       icount1, icount2));

			BUFFER_TRACE(frame->bh, "get_write_access"); /* index root */
			err = ext4_journal_get_write_access(handle,
							     (frame - 1)->bh);
			if (err)
				goto journal_error;

			memcpy((char *) entries2, (char *) (entries + icount1),
			       icount2 * sizeof(struct dx_entry));
			dx_set_count(entries, icount1);
			dx_set_count(entries2, icount2);
			dx_set_limit(entries2, dx_node_limit(dir));

			/* Which index block gets the new entry? */
			if (at - entries >= icount1) {
				frame->at = at = at - entries - icount1 + entries2;
				frame->entries = entries = entries2;
				swap(frame->bh, bh2);
			}
			dx_insert_block((frame - 1), hash2, newblock);
			dxtrace(dx_show_index("node", frame->entries));
			dxtrace(dx_show_index("node",
			       ((struct dx_node *) bh2->b_data)->entries));
			err = ext4_handle_dirty_dx_node(handle, dir, bh2);
			if (err)
				goto journal_error;
			brelse (bh2);
			err = ext4_handle_dirty_dx_node(handle, dir,
						   (frame - 1)->bh);
			if (err)
				goto journal_error;
			if (restart) {
				err = ext4_handle_dirty_dx_node(handle, dir,
							   frame->bh);
				goto journal_error;
			}
		} else {
			struct dx_root *dxroot;
			memcpy((char *) entries2, (char *) entries,
			       icount * sizeof(struct dx_entry));
			dx_set_limit(entries2, dx_node_limit(dir));

			/* Set up root */
			dx_set_count(entries, 1);
			dx_set_block(entries + 0, newblock);
			dxroot = (struct dx_root *)frames[0].bh->b_data;
			dxroot->info.indirect_levels += 1;
			dxtrace(printk(KERN_DEBUG
				       "Creating %d level index...\n",
				       info->indirect_levels));
			err = ext4_handle_dirty_dx_node(handle, dir, frame->bh);
			if (err)
				goto journal_error;
			err = ext4_handle_dirty_dx_node(handle, dir, bh2);
			brelse(bh2);
			restart = 1;
			goto journal_error;
		}
	}
	de = do_split(handle, dir, &bh, frame, &fname->hinfo);
	if (IS_ERR(de)) {
		err = PTR_ERR(de);
		goto cleanup;
	}
	err = add_dirent_to_buf(handle, fname, dir, inode, de, bh);
	goto cleanup;

journal_error:
	ext4_std_error(dir->i_sb, err); /* this is a no-op if err == 0 */
cleanup:
	brelse(bh);
	dx_release(frames);
	/* @restart is true means htree-path has been changed, we need to
	 * repeat dx_probe() to find out valid htree-path
	 */
	if (restart && err == 0)
		goto again;
	return err;
}

/*
 * ext4_generic_delete_entry deletes a directory entry by merging it
 * with the previous entry
 */
int ext4_generic_delete_entry(handle_t *handle,
			      struct inode *dir,
			      struct ext4_dir_entry_2 *de_del,
			      struct buffer_head *bh,
			      void *entry_buf,
			      int buf_size,
			      int csum_size)
{
	struct ext4_dir_entry_2 *de, *pde;
	unsigned int blocksize = dir->i_sb->s_blocksize;
	int i;

	i = 0;
	pde = NULL;
	de = (struct ext4_dir_entry_2 *)entry_buf;
	while (i < buf_size - csum_size) {
		if (ext4_check_dir_entry(dir, NULL, de, bh,
					 bh->b_data, bh->b_size, i))
			return -EFSCORRUPTED;
		if (de == de_del)  {
			if (pde)
				pde->rec_len = ext4_rec_len_to_disk(
					ext4_rec_len_from_disk(pde->rec_len,
							       blocksize) +
					ext4_rec_len_from_disk(de->rec_len,
							       blocksize),
					blocksize);
			else
				de->inode = 0;
			inode_inc_iversion(dir);
			return 0;
		}
		i += ext4_rec_len_from_disk(de->rec_len, blocksize);
		pde = de;
		de = ext4_next_entry(de, blocksize);
	}
	return -ENOENT;
}

static int ext4_delete_entry(handle_t *handle,
			     struct inode *dir,
			     struct ext4_dir_entry_2 *de_del,
			     struct buffer_head *bh)
{
	int err, csum_size = 0;

	if (ext4_has_inline_data(dir)) {
		int has_inline_data = 1;
		err = ext4_delete_inline_entry(handle, dir, de_del, bh,
					       &has_inline_data);
		if (has_inline_data)
			return err;
	}

	if (ext4_has_metadata_csum(dir->i_sb))
		csum_size = sizeof(struct ext4_dir_entry_tail);

	BUFFER_TRACE(bh, "get_write_access");
	err = ext4_journal_get_write_access(handle, bh);
	if (unlikely(err))
		goto out;

	err = ext4_generic_delete_entry(handle, dir, de_del,
					bh, bh->b_data,
					dir->i_sb->s_blocksize, csum_size);
	if (err)
		goto out;

	BUFFER_TRACE(bh, "call ext4_handle_dirty_metadata");
	err = ext4_handle_dirty_dirent_node(handle, dir, bh);
	if (unlikely(err))
		goto out;

	return 0;
out:
	if (err != -ENOENT)
		ext4_std_error(dir->i_sb, err);
	return err;
}

/*
 * Set directory link count to 1 if nlinks > EXT4_LINK_MAX, or if nlinks == 2
 * since this indicates that nlinks count was previously 1 to avoid overflowing
 * the 16-bit i_links_count field on disk.  Directories with i_nlink == 1 mean
 * that subdirectory link counts are not being maintained accurately.
 *
 * The caller has already checked for i_nlink overflow in case the DIR_LINK
 * feature is not enabled and returned -EMLINK.  The is_dx() check is a proxy
 * for checking S_ISDIR(inode) (since the INODE_INDEX feature will not be set
 * on regular files) and to avoid creating huge/slow non-HTREE directories.
 */
static void ext4_inc_count(handle_t *handle, struct inode *inode)
{
	inc_nlink(inode);
	if (is_dx(inode) &&
	    (inode->i_nlink > EXT4_LINK_MAX || inode->i_nlink == 2))
		set_nlink(inode, 1);
}

/*
 * If a directory had nlink == 1, then we should let it be 1. This indicates
 * directory has >EXT4_LINK_MAX subdirs.
 */
static void ext4_dec_count(handle_t *handle, struct inode *inode)
{
	if (!S_ISDIR(inode->i_mode) || inode->i_nlink > 2)
		drop_nlink(inode);
}


static int ext4_add_nondir(handle_t *handle,
		struct dentry *dentry, struct inode *inode)
{
	int err = ext4_add_entry(handle, dentry, inode);
	if (!err) {
		ext4_mark_inode_dirty(handle, inode);
		unlock_new_inode(inode);
		d_instantiate(dentry, inode);
		return 0;
	}
	drop_nlink(inode);
	unlock_new_inode(inode);
	iput(inode);
	return err;
}

/*
 * By the time this is called, we already have created
 * the directory cache entry for the new file, but it
 * is so far negative - it has no inode.
 *
 * If the create succeeds, we fill in the inode information
 * with d_instantiate().
 */
static int ext4_create(struct inode *dir, struct dentry *dentry, umode_t mode,
		       bool excl)
{
	handle_t *handle;
	struct inode *inode;
	int err, credits, retries = 0;

	err = dquot_initialize(dir);
	if (err)
		return err;

	credits = (EXT4_DATA_TRANS_BLOCKS(dir->i_sb) +
		   EXT4_INDEX_EXTRA_TRANS_BLOCKS + 3);
retry:
	inode = ext4_new_inode_start_handle(dir, mode, &dentry->d_name, 0,
					    NULL, EXT4_HT_DIR, credits);
	handle = ext4_journal_current_handle();
	err = PTR_ERR(inode);
	if (!IS_ERR(inode)) {
		inode->i_op = &ext4_file_inode_operations;
		inode->i_fop = &ext4_file_operations;
		ext4_set_aops(inode);
		err = ext4_add_nondir(handle, dentry, inode);
		if (!err && IS_DIRSYNC(dir))
			ext4_handle_sync(handle);
	}
	if (handle)
		ext4_journal_stop(handle);
	if (err == -ENOSPC && ext4_should_retry_alloc(dir->i_sb, &retries))
		goto retry;
	return err;
}

static int ext4_mknod(struct inode *dir, struct dentry *dentry,
		      umode_t mode, dev_t rdev)
{
	handle_t *handle;
	struct inode *inode;
	int err, credits, retries = 0;

	err = dquot_initialize(dir);
	if (err)
		return err;

	credits = (EXT4_DATA_TRANS_BLOCKS(dir->i_sb) +
		   EXT4_INDEX_EXTRA_TRANS_BLOCKS + 3);
retry:
	inode = ext4_new_inode_start_handle(dir, mode, &dentry->d_name, 0,
					    NULL, EXT4_HT_DIR, credits);
	handle = ext4_journal_current_handle();
	err = PTR_ERR(inode);
	if (!IS_ERR(inode)) {
		init_special_inode(inode, inode->i_mode, rdev);
		inode->i_op = &ext4_special_inode_operations;
		err = ext4_add_nondir(handle, dentry, inode);
		if (!err && IS_DIRSYNC(dir))
			ext4_handle_sync(handle);
	}
	if (handle)
		ext4_journal_stop(handle);
	if (err == -ENOSPC && ext4_should_retry_alloc(dir->i_sb, &retries))
		goto retry;
	return err;
}

static int ext4_tmpfile(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	handle_t *handle;
	struct inode *inode;
	int err, retries = 0;

	err = dquot_initialize(dir);
	if (err)
		return err;

retry:
	inode = ext4_new_inode_start_handle(dir, mode,
					    NULL, 0, NULL,
					    EXT4_HT_DIR,
			EXT4_MAXQUOTAS_INIT_BLOCKS(dir->i_sb) +
			  4 + EXT4_XATTR_TRANS_BLOCKS);
	handle = ext4_journal_current_handle();
	err = PTR_ERR(inode);
	if (!IS_ERR(inode)) {
		inode->i_op = &ext4_file_inode_operations;
		inode->i_fop = &ext4_file_operations;
		ext4_set_aops(inode);
		d_tmpfile(dentry, inode);
		err = ext4_orphan_add(handle, inode);
		if (err)
			goto err_unlock_inode;
		mark_inode_dirty(inode);
		unlock_new_inode(inode);
	}
	if (handle)
		ext4_journal_stop(handle);
	if (err == -ENOSPC && ext4_should_retry_alloc(dir->i_sb, &retries))
		goto retry;
	return err;
err_unlock_inode:
	ext4_journal_stop(handle);
	unlock_new_inode(inode);
	return err;
}

struct ext4_dir_entry_2 *ext4_init_dot_dotdot(struct inode *inode,
			  struct ext4_dir_entry_2 *de,
			  int blocksize, int csum_size,
			  unsigned int parent_ino, int dotdot_real_len)
{
	de->inode = cpu_to_le32(inode->i_ino);
	de->name_len = 1;
	de->rec_len = ext4_rec_len_to_disk(EXT4_DIR_REC_LEN(de->name_len),
					   blocksize);
	strcpy(de->name, ".");
	ext4_set_de_type(inode->i_sb, de, S_IFDIR);

	de = ext4_next_entry(de, blocksize);
	de->inode = cpu_to_le32(parent_ino);
	de->name_len = 2;
	if (!dotdot_real_len)
		de->rec_len = ext4_rec_len_to_disk(blocksize -
					(csum_size + EXT4_DIR_REC_LEN(1)),
					blocksize);
	else
		de->rec_len = ext4_rec_len_to_disk(
				EXT4_DIR_REC_LEN(de->name_len), blocksize);
	strcpy(de->name, "..");
	ext4_set_de_type(inode->i_sb, de, S_IFDIR);

	return ext4_next_entry(de, blocksize);
}

static int ext4_init_new_dir(handle_t *handle, struct inode *dir,
			     struct inode *inode)
{
	struct buffer_head *dir_block = NULL;
	struct ext4_dir_entry_2 *de;
	struct ext4_dir_entry_tail *t;
	ext4_lblk_t block = 0;
	unsigned int blocksize = dir->i_sb->s_blocksize;
	int csum_size = 0;
	int err;

	if (ext4_has_metadata_csum(dir->i_sb))
		csum_size = sizeof(struct ext4_dir_entry_tail);

	if (ext4_test_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA)) {
		err = ext4_try_create_inline_dir(handle, dir, inode);
		if (err < 0 && err != -ENOSPC)
			goto out;
		if (!err)
			goto out;
	}

	inode->i_size = 0;
	dir_block = ext4_append(handle, inode, &block);
	if (IS_ERR(dir_block))
		return PTR_ERR(dir_block);
	de = (struct ext4_dir_entry_2 *)dir_block->b_data;
	ext4_init_dot_dotdot(inode, de, blocksize, csum_size, dir->i_ino, 0);
	set_nlink(inode, 2);
	if (csum_size) {
		t = EXT4_DIRENT_TAIL(dir_block->b_data, blocksize);
		initialize_dirent_tail(t, blocksize);
	}

	BUFFER_TRACE(dir_block, "call ext4_handle_dirty_metadata");
	err = ext4_handle_dirty_dirent_node(handle, inode, dir_block);
	if (err)
		goto out;
	set_buffer_verified(dir_block);
out:
	brelse(dir_block);
	return err;
}

static int ext4_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	handle_t *handle;
	struct inode *inode;
	int err, credits, retries = 0;

	if (EXT4_DIR_LINK_MAX(dir))
		return -EMLINK;

	err = dquot_initialize(dir);
	if (err)
		return err;

	credits = (EXT4_DATA_TRANS_BLOCKS(dir->i_sb) +
		   EXT4_INDEX_EXTRA_TRANS_BLOCKS + 3);
retry:
	inode = ext4_new_inode_start_handle(dir, S_IFDIR | mode,
					    &dentry->d_name,
					    0, NULL, EXT4_HT_DIR, credits);
	handle = ext4_journal_current_handle();
	err = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto out_stop;

	inode->i_op = &ext4_dir_inode_operations;
	inode->i_fop = &ext4_dir_operations;
	err = ext4_init_new_dir(handle, dir, inode);
	if (err)
		goto out_clear_inode;
	err = ext4_mark_inode_dirty(handle, inode);
	if (!err)
		err = ext4_add_entry(handle, dentry, inode);
	if (err) {
out_clear_inode:
		clear_nlink(inode);
		unlock_new_inode(inode);
		ext4_mark_inode_dirty(handle, inode);
		iput(inode);
		goto out_stop;
	}
	ext4_inc_count(handle, dir);
	ext4_update_dx_flag(dir);
	err = ext4_mark_inode_dirty(handle, dir);
	if (err)
		goto out_clear_inode;
	unlock_new_inode(inode);
	d_instantiate(dentry, inode);
	if (IS_DIRSYNC(dir))
		ext4_handle_sync(handle);

out_stop:
	if (handle)
		ext4_journal_stop(handle);
	if (err == -ENOSPC && ext4_should_retry_alloc(dir->i_sb, &retries))
		goto retry;
	return err;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
bool ext4_empty_dir(struct inode *inode)
{
	unsigned int offset;
	struct buffer_head *bh;
	struct ext4_dir_entry_2 *de, *de1;
	struct super_block *sb;

	if (ext4_has_inline_data(inode)) {
		int has_inline_data = 1;
		int ret;

		ret = empty_inline_dir(inode, &has_inline_data);
		if (has_inline_data)
			return ret;
	}

	sb = inode->i_sb;
	if (inode->i_size < EXT4_DIR_REC_LEN(1) + EXT4_DIR_REC_LEN(2)) {
		EXT4_ERROR_INODE(inode, "invalid size");
		return true;
	}
	bh = ext4_read_dirblock(inode, 0, EITHER);
	if (IS_ERR(bh))
		return true;

	de = (struct ext4_dir_entry_2 *) bh->b_data;
	de1 = ext4_next_entry(de, sb->s_blocksize);
	if (le32_to_cpu(de->inode) != inode->i_ino ||
			le32_to_cpu(de1->inode) == 0 ||
			strcmp(".", de->name) || strcmp("..", de1->name)) {
		ext4_warning_inode(inode, "directory missing '.' and/or '..'");
		brelse(bh);
		return true;
	}
	offset = ext4_rec_len_from_disk(de->rec_len, sb->s_blocksize) +
		 ext4_rec_len_from_disk(de1->rec_len, sb->s_blocksize);
	de = ext4_next_entry(de1, sb->s_blocksize);
	while (offset < inode->i_size) {
		if ((void *) de >= (void *) (bh->b_data+sb->s_blocksize)) {
			unsigned int lblock;
			brelse(bh);
			lblock = offset >> EXT4_BLOCK_SIZE_BITS(sb);
			bh = ext4_read_dirblock(inode, lblock, EITHER);
			if (IS_ERR(bh))
				return true;
			de = (struct ext4_dir_entry_2 *) bh->b_data;
		}
		if (ext4_check_dir_entry(inode, NULL, de, bh,
					 bh->b_data, bh->b_size, offset)) {
			de = (struct ext4_dir_entry_2 *)(bh->b_data +
							 sb->s_blocksize);
			offset = (offset | (sb->s_blocksize - 1)) + 1;
			continue;
		}
		if (le32_to_cpu(de->inode)) {
			brelse(bh);
			return false;
		}
		offset += ext4_rec_len_from_disk(de->rec_len, sb->s_blocksize);
		de = ext4_next_entry(de, sb->s_blocksize);
	}
	brelse(bh);
	return true;
}

/*
 * ext4_orphan_add() links an unlinked or truncated inode into a list of
 * such inodes, starting at the superblock, in case we crash before the
 * file is closed/deleted, or in case the inode truncate spans multiple
 * transactions and the last transaction is not recovered after a crash.
 *
 * At filesystem recovery time, we walk this list deleting unlinked
 * inodes and truncating linked inodes in ext4_orphan_cleanup().
 *
 * Orphan list manipulation functions must be called under i_mutex unless
 * we are just creating the inode or deleting it.
 */
int ext4_orphan_add(handle_t *handle, struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_iloc iloc;
	int err = 0, rc;
	bool dirty = false;

	if (!sbi->s_journal || is_bad_inode(inode))
		return 0;

	WARN_ON_ONCE(!(inode->i_state & (I_NEW | I_FREEING)) &&
		     !inode_is_locked(inode));
	/*
	 * Exit early if inode already is on orphan list. This is a big speedup
	 * since we don't have to contend on the global s_orphan_lock.
	 */
	if (!list_empty(&EXT4_I(inode)->i_orphan))
		return 0;

	/*
	 * Orphan handling is only valid for files with data blocks
	 * being truncated, or files being unlinked. Note that we either
	 * hold i_mutex, or the inode can not be referenced from outside,
	 * so i_nlink should not be bumped due to race
	 */
	J_ASSERT((S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
		  S_ISLNK(inode->i_mode)) || inode->i_nlink == 0);

	BUFFER_TRACE(sbi->s_sbh, "get_write_access");
	err = ext4_journal_get_write_access(handle, sbi->s_sbh);
	if (err)
		goto out;

	err = ext4_reserve_inode_write(handle, inode, &iloc);
	if (err)
		goto out;

	mutex_lock(&sbi->s_orphan_lock);
	/*
	 * Due to previous errors inode may be already a part of on-disk
	 * orphan list. If so skip on-disk list modification.
	 */
	if (!NEXT_ORPHAN(inode) || NEXT_ORPHAN(inode) >
	    (le32_to_cpu(sbi->s_es->s_inodes_count))) {
		/* Insert this inode at the head of the on-disk orphan list */
		NEXT_ORPHAN(inode) = le32_to_cpu(sbi->s_es->s_last_orphan);
		sbi->s_es->s_last_orphan = cpu_to_le32(inode->i_ino);
		dirty = true;
	}
	list_add(&EXT4_I(inode)->i_orphan, &sbi->s_orphan);
	mutex_unlock(&sbi->s_orphan_lock);

	if (dirty) {
		err = ext4_handle_dirty_super(handle, sb);
		rc = ext4_mark_iloc_dirty(handle, inode, &iloc);
		if (!err)
			err = rc;
		if (err) {
			/*
			 * We have to remove inode from in-memory list if
			 * addition to on disk orphan list failed. Stray orphan
			 * list entries can cause panics at unmount time.
			 */
			mutex_lock(&sbi->s_orphan_lock);
			list_del_init(&EXT4_I(inode)->i_orphan);
			mutex_unlock(&sbi->s_orphan_lock);
		}
	}
	jbd_debug(4, "superblock will point to %lu\n", inode->i_ino);
	jbd_debug(4, "orphan inode %lu will point to %d\n",
			inode->i_ino, NEXT_ORPHAN(inode));
out:
	ext4_std_error(sb, err);
	return err;
}

/*
 * ext4_orphan_del() removes an unlinked or truncated inode from the list
 * of such inodes stored on disk, because it is finally being cleaned up.
 */
int ext4_orphan_del(handle_t *handle, struct inode *inode)
{
	struct list_head *prev;
	struct ext4_inode_info *ei = EXT4_I(inode);
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	__u32 ino_next;
	struct ext4_iloc iloc;
	int err = 0;

	if (!sbi->s_journal && !(sbi->s_mount_state & EXT4_ORPHAN_FS))
		return 0;

	WARN_ON_ONCE(!(inode->i_state & (I_NEW | I_FREEING)) &&
		     !inode_is_locked(inode));
	/* Do this quick check before taking global s_orphan_lock. */
	if (list_empty(&ei->i_orphan))
		return 0;

	if (handle) {
		/* Grab inode buffer early before taking global s_orphan_lock */
		err = ext4_reserve_inode_write(handle, inode, &iloc);
	}

	mutex_lock(&sbi->s_orphan_lock);
	jbd_debug(4, "remove inode %lu from orphan list\n", inode->i_ino);

	prev = ei->i_orphan.prev;
	list_del_init(&ei->i_orphan);

	/* If we're on an error path, we may not have a valid
	 * transaction handle with which to update the orphan list on
	 * disk, but we still need to remove the inode from the linked
	 * list in memory. */
	if (!handle || err) {
		mutex_unlock(&sbi->s_orphan_lock);
		goto out_err;
	}

	ino_next = NEXT_ORPHAN(inode);
	if (prev == &sbi->s_orphan) {
		jbd_debug(4, "superblock will point to %u\n", ino_next);
		BUFFER_TRACE(sbi->s_sbh, "get_write_access");
		err = ext4_journal_get_write_access(handle, sbi->s_sbh);
		if (err) {
			mutex_unlock(&sbi->s_orphan_lock);
			goto out_brelse;
		}
		sbi->s_es->s_last_orphan = cpu_to_le32(ino_next);
		mutex_unlock(&sbi->s_orphan_lock);
		err = ext4_handle_dirty_super(handle, inode->i_sb);
	} else {
		struct ext4_iloc iloc2;
		struct inode *i_prev =
			&list_entry(prev, struct ext4_inode_info, i_orphan)->vfs_inode;

		jbd_debug(4, "orphan inode %lu will point to %u\n",
			  i_prev->i_ino, ino_next);
		err = ext4_reserve_inode_write(handle, i_prev, &iloc2);
		if (err) {
			mutex_unlock(&sbi->s_orphan_lock);
			goto out_brelse;
		}
		NEXT_ORPHAN(i_prev) = ino_next;
		err = ext4_mark_iloc_dirty(handle, i_prev, &iloc2);
		mutex_unlock(&sbi->s_orphan_lock);
	}
	if (err)
		goto out_brelse;
	NEXT_ORPHAN(inode) = 0;
	err = ext4_mark_iloc_dirty(handle, inode, &iloc);
out_err:
	ext4_std_error(inode->i_sb, err);
	return err;

out_brelse:
	brelse(iloc.bh);
	goto out_err;
}

static int ext4_rmdir(struct inode *dir, struct dentry *dentry)
{
	int retval;
	struct inode *inode;
	struct buffer_head *bh;
	struct ext4_dir_entry_2 *de;
	handle_t *handle = NULL;

	if (unlikely(ext4_forced_shutdown(EXT4_SB(dir->i_sb))))
		return -EIO;

	/* Initialize quotas before so that eventual writes go in
	 * separate transaction */
	retval = dquot_initialize(dir);
	if (retval)
		return retval;
	retval = dquot_initialize(d_inode(dentry));
	if (retval)
		return retval;

	retval = -ENOENT;
	bh = ext4_find_entry(dir, &dentry->d_name, &de, NULL);
	if (IS_ERR(bh))
		return PTR_ERR(bh);
	if (!bh)
		goto end_rmdir;

	inode = d_inode(dentry);

	retval = -EFSCORRUPTED;
	if (le32_to_cpu(de->inode) != inode->i_ino)
		goto end_rmdir;

	retval = -ENOTEMPTY;
	if (!ext4_empty_dir(inode))
		goto end_rmdir;

	handle = ext4_journal_start(dir, EXT4_HT_DIR,
				    EXT4_DATA_TRANS_BLOCKS(dir->i_sb));
	if (IS_ERR(handle)) {
		retval = PTR_ERR(handle);
		handle = NULL;
		goto end_rmdir;
	}

	if (IS_DIRSYNC(dir))
		ext4_handle_sync(handle);

	retval = ext4_delete_entry(handle, dir, de, bh);
	if (retval)
		goto end_rmdir;
	if (!EXT4_DIR_LINK_EMPTY(inode))
		ext4_warning_inode(inode,
			     "empty directory '%.*s' has too many links (%u)",
			     dentry->d_name.len, dentry->d_name.name,
			     inode->i_nlink);
	inode->i_version++;
	clear_nlink(inode);
	/* There's no need to set i_disksize: the fact that i_nlink is
	 * zero will ensure that the right thing happens during any
	 * recovery. */
	inode->i_size = 0;
	ext4_orphan_add(handle, inode);
	inode->i_ctime = dir->i_ctime = dir->i_mtime = current_time(inode);
	ext4_mark_inode_dirty(handle, inode);
	ext4_dec_count(handle, dir);
	ext4_update_dx_flag(dir);
	ext4_mark_inode_dirty(handle, dir);

end_rmdir:
	brelse(bh);
	if (handle)
		ext4_journal_stop(handle);
	return retval;
}

static int ext4_unlink(struct inode *dir, struct dentry *dentry)
{
	int retval;
	struct inode *inode;
	struct buffer_head *bh;
	struct ext4_dir_entry_2 *de;
	handle_t *handle = NULL;

	if (unlikely(ext4_forced_shutdown(EXT4_SB(dir->i_sb))))
		return -EIO;

	trace_ext4_unlink_enter(dir, dentry);
	/* Initialize quotas before so that eventual writes go
	 * in separate transaction */
	retval = dquot_initialize(dir);
	if (retval)
		return retval;
	retval = dquot_initialize(d_inode(dentry));
	if (retval)
		return retval;

	retval = -ENOENT;
	bh = ext4_find_entry(dir, &dentry->d_name, &de, NULL);
	if (IS_ERR(bh))
		return PTR_ERR(bh);
	if (!bh)
		goto end_unlink;

	inode = d_inode(dentry);

	retval = -EFSCORRUPTED;
	if (le32_to_cpu(de->inode) != inode->i_ino)
		goto end_unlink;

	handle = ext4_journal_start(dir, EXT4_HT_DIR,
				    EXT4_DATA_TRANS_BLOCKS(dir->i_sb));
	if (IS_ERR(handle)) {
		retval = PTR_ERR(handle);
		handle = NULL;
		goto end_unlink;
	}

	if (IS_DIRSYNC(dir))
		ext4_handle_sync(handle);

	if (inode->i_nlink == 0) {
		ext4_warning_inode(inode, "Deleting file '%.*s' with no links",
				   dentry->d_name.len, dentry->d_name.name);
		set_nlink(inode, 1);
	}
	retval = ext4_delete_entry(handle, dir, de, bh);
	if (retval)
		goto end_unlink;
	dir->i_ctime = dir->i_mtime = current_time(dir);
	ext4_update_dx_flag(dir);
	ext4_mark_inode_dirty(handle, dir);
	drop_nlink(inode);
	if (!inode->i_nlink)
		ext4_orphan_add(handle, inode);
	inode->i_ctime = current_time(inode);
	ext4_mark_inode_dirty(handle, inode);

end_unlink:
	brelse(bh);
	if (handle)
		ext4_journal_stop(handle);
	trace_ext4_unlink_exit(dentry, retval);
	return retval;
}

static int ext4_symlink(struct inode *dir,
			struct dentry *dentry, const char *symname)
{
	handle_t *handle;
	struct inode *inode;
	int err, len = strlen(symname);
	int credits;
	bool encryption_required;
	struct fscrypt_str disk_link;
	struct fscrypt_symlink_data *sd = NULL;

	if (unlikely(ext4_forced_shutdown(EXT4_SB(dir->i_sb))))
		return -EIO;

	disk_link.len = len + 1;
	disk_link.name = (char *) symname;

	encryption_required = (ext4_encrypted_inode(dir) ||
			       DUMMY_ENCRYPTION_ENABLED(EXT4_SB(dir->i_sb)));
	if (encryption_required) {
		err = fscrypt_get_encryption_info(dir);
		if (err)
			return err;
		if (!fscrypt_has_encryption_key(dir))
			return -ENOKEY;
		disk_link.len = (fscrypt_fname_encrypted_size(dir, len) +
				 sizeof(struct fscrypt_symlink_data));
		sd = kzalloc(disk_link.len, GFP_KERNEL);
		if (!sd)
			return -ENOMEM;
	}

	if (disk_link.len > dir->i_sb->s_blocksize) {
		err = -ENAMETOOLONG;
		goto err_free_sd;
	}

	err = dquot_initialize(dir);
	if (err)
		goto err_free_sd;

	if ((disk_link.len > EXT4_N_BLOCKS * 4)) {
		/*
		 * For non-fast symlinks, we just allocate inode and put it on
		 * orphan list in the first transaction => we need bitmap,
		 * group descriptor, sb, inode block, quota blocks, and
		 * possibly selinux xattr blocks.
		 */
		credits = 4 + EXT4_MAXQUOTAS_INIT_BLOCKS(dir->i_sb) +
			  EXT4_XATTR_TRANS_BLOCKS;
	} else {
		/*
		 * Fast symlink. We have to add entry to directory
		 * (EXT4_DATA_TRANS_BLOCKS + EXT4_INDEX_EXTRA_TRANS_BLOCKS),
		 * allocate new inode (bitmap, group descriptor, inode block,
		 * quota blocks, sb is already counted in previous macros).
		 */
		credits = EXT4_DATA_TRANS_BLOCKS(dir->i_sb) +
			  EXT4_INDEX_EXTRA_TRANS_BLOCKS + 3;
	}

	inode = ext4_new_inode_start_handle(dir, S_IFLNK|S_IRWXUGO,
					    &dentry->d_name, 0, NULL,
					    EXT4_HT_DIR, credits);
	handle = ext4_journal_current_handle();
	if (IS_ERR(inode)) {
		if (handle)
			ext4_journal_stop(handle);
		err = PTR_ERR(inode);
		goto err_free_sd;
	}

	if (encryption_required) {
		struct qstr istr;
		struct fscrypt_str ostr =
			FSTR_INIT(sd->encrypted_path, disk_link.len);

		istr.name = (const unsigned char *) symname;
		istr.len = len;
		err = fscrypt_fname_usr_to_disk(inode, &istr, &ostr);
		if (err)
			goto err_drop_inode;
		sd->len = cpu_to_le16(ostr.len);
		disk_link.name = (char *) sd;
		inode->i_op = &ext4_encrypted_symlink_inode_operations;
	}

	if ((disk_link.len > EXT4_N_BLOCKS * 4)) {
		if (!encryption_required)
			inode->i_op = &ext4_symlink_inode_operations;
		inode_nohighmem(inode);
		ext4_set_aops(inode);
		/*
		 * We cannot call page_symlink() with transaction started
		 * because it calls into ext4_write_begin() which can wait
		 * for transaction commit if we are running out of space
		 * and thus we deadlock. So we have to stop transaction now
		 * and restart it when symlink contents is written.
		 * 
		 * To keep fs consistent in case of crash, we have to put inode
		 * to orphan list in the mean time.
		 */
		drop_nlink(inode);
		err = ext4_orphan_add(handle, inode);
		ext4_journal_stop(handle);
		handle = NULL;
		if (err)
			goto err_drop_inode;
		err = __page_symlink(inode, disk_link.name, disk_link.len, 1);
		if (err)
			goto err_drop_inode;
		/*
		 * Now inode is being linked into dir (EXT4_DATA_TRANS_BLOCKS
		 * + EXT4_INDEX_EXTRA_TRANS_BLOCKS), inode is also modified
		 */
		handle = ext4_journal_start(dir, EXT4_HT_DIR,
				EXT4_DATA_TRANS_BLOCKS(dir->i_sb) +
				EXT4_INDEX_EXTRA_TRANS_BLOCKS + 1);
		if (IS_ERR(handle)) {
			err = PTR_ERR(handle);
			handle = NULL;
			goto err_drop_inode;
		}
		set_nlink(inode, 1);
		err = ext4_orphan_del(handle, inode);
		if (err)
			goto err_drop_inode;
	} else {
		/* clear the extent format for fast symlink */
		ext4_clear_inode_flag(inode, EXT4_INODE_EXTENTS);
		if (!encryption_required) {
			inode->i_op = &ext4_fast_symlink_inode_operations;
			inode->i_link = (char *)&EXT4_I(inode)->i_data;
		}
		memcpy((char *)&EXT4_I(inode)->i_data, disk_link.name,
		       disk_link.len);
		inode->i_size = disk_link.len - 1;
	}
	EXT4_I(inode)->i_disksize = inode->i_size;
	err = ext4_add_nondir(handle, dentry, inode);
	if (!err && IS_DIRSYNC(dir))
		ext4_handle_sync(handle);

	if (handle)
		ext4_journal_stop(handle);
	kfree(sd);
	return err;
err_drop_inode:
	if (handle)
		ext4_journal_stop(handle);
	clear_nlink(inode);
	unlock_new_inode(inode);
	iput(inode);
err_free_sd:
	kfree(sd);
	return err;
}

static int ext4_link(struct dentry *old_dentry,
		     struct inode *dir, struct dentry *dentry)
{
	handle_t *handle;
	struct inode *inode = d_inode(old_dentry);
	int err, retries = 0;

	if (inode->i_nlink >= EXT4_LINK_MAX)
		return -EMLINK;
	if (ext4_encrypted_inode(dir) &&
			!fscrypt_has_permitted_context(dir, inode))
		return -EPERM;

       if ((ext4_test_inode_flag(dir, EXT4_INODE_PROJINHERIT)) &&
	   (!projid_eq(EXT4_I(dir)->i_projid,
		       EXT4_I(old_dentry->d_inode)->i_projid)))
		return -EXDEV;

	err = dquot_initialize(dir);
	if (err)
		return err;

retry:
	handle = ext4_journal_start(dir, EXT4_HT_DIR,
		(EXT4_DATA_TRANS_BLOCKS(dir->i_sb) +
		 EXT4_INDEX_EXTRA_TRANS_BLOCKS) + 1);
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	if (IS_DIRSYNC(dir))
		ext4_handle_sync(handle);

	inode->i_ctime = current_time(inode);
	ext4_inc_count(handle, inode);
	ihold(inode);

	err = ext4_add_entry(handle, dentry, inode);
	if (!err) {
		ext4_mark_inode_dirty(handle, inode);
		/* this can happen only for tmpfile being
		 * linked the first time
		 */
		if (inode->i_nlink == 1)
			ext4_orphan_del(handle, inode);
		d_instantiate(dentry, inode);
	} else {
		drop_nlink(inode);
		iput(inode);
	}
	ext4_journal_stop(handle);
	if (err == -ENOSPC && ext4_should_retry_alloc(dir->i_sb, &retries))
		goto retry;
	return err;
}


/*
 * Try to find buffer head where contains the parent block.
 * It should be the inode block if it is inlined or the 1st block
 * if it is a normal dir.
 */
static struct buffer_head *ext4_get_first_dir_block(handle_t *handle,
					struct inode *inode,
					int *retval,
					struct ext4_dir_entry_2 **parent_de,
					int *inlined)
{
	struct buffer_head *bh;

	if (!ext4_has_inline_data(inode)) {
		bh = ext4_read_dirblock(inode, 0, EITHER);
		if (IS_ERR(bh)) {
			*retval = PTR_ERR(bh);
			return NULL;
		}
		*parent_de = ext4_next_entry(
					(struct ext4_dir_entry_2 *)bh->b_data,
					inode->i_sb->s_blocksize);
		return bh;
	}

	*inlined = 1;
	return ext4_get_first_inline_block(inode, parent_de, retval);
}

struct ext4_renament {
	struct inode *dir;
	struct dentry *dentry;
	struct inode *inode;
	bool is_dir;
	int dir_nlink_delta;

	/* entry for "dentry" */
	struct buffer_head *bh;
	struct ext4_dir_entry_2 *de;
	int inlined;

	/* entry for ".." in inode if it's a directory */
	struct buffer_head *dir_bh;
	struct ext4_dir_entry_2 *parent_de;
	int dir_inlined;
};

static int ext4_rename_dir_prepare(handle_t *handle, struct ext4_renament *ent)
{
	int retval;

	ent->dir_bh = ext4_get_first_dir_block(handle, ent->inode,
					      &retval, &ent->parent_de,
					      &ent->dir_inlined);
	if (!ent->dir_bh)
		return retval;
	if (le32_to_cpu(ent->parent_de->inode) != ent->dir->i_ino)
		return -EFSCORRUPTED;
	BUFFER_TRACE(ent->dir_bh, "get_write_access");
	return ext4_journal_get_write_access(handle, ent->dir_bh);
}

static int ext4_rename_dir_finish(handle_t *handle, struct ext4_renament *ent,
				  unsigned dir_ino)
{
	int retval;

	ent->parent_de->inode = cpu_to_le32(dir_ino);
	BUFFER_TRACE(ent->dir_bh, "call ext4_handle_dirty_metadata");
	if (!ent->dir_inlined) {
		if (is_dx(ent->inode)) {
			retval = ext4_handle_dirty_dx_node(handle,
							   ent->inode,
							   ent->dir_bh);
		} else {
			retval = ext4_handle_dirty_dirent_node(handle,
							       ent->inode,
							       ent->dir_bh);
		}
	} else {
		retval = ext4_mark_inode_dirty(handle, ent->inode);
	}
	if (retval) {
		ext4_std_error(ent->dir->i_sb, retval);
		return retval;
	}
	return 0;
}

static int ext4_setent(handle_t *handle, struct ext4_renament *ent,
		       unsigned ino, unsigned file_type)
{
	int retval;

	BUFFER_TRACE(ent->bh, "get write access");
	retval = ext4_journal_get_write_access(handle, ent->bh);
	if (retval)
		return retval;
	ent->de->inode = cpu_to_le32(ino);
	if (ext4_has_feature_filetype(ent->dir->i_sb))
		ent->de->file_type = file_type;
	ent->dir->i_version++;
	ent->dir->i_ctime = ent->dir->i_mtime =
		current_time(ent->dir);
	ext4_mark_inode_dirty(handle, ent->dir);
	BUFFER_TRACE(ent->bh, "call ext4_handle_dirty_metadata");
	if (!ent->inlined) {
		retval = ext4_handle_dirty_dirent_node(handle,
						       ent->dir, ent->bh);
		if (unlikely(retval)) {
			ext4_std_error(ent->dir->i_sb, retval);
			return retval;
		}
	}
	brelse(ent->bh);
	ent->bh = NULL;

	return 0;
}

static int ext4_find_delete_entry(handle_t *handle, struct inode *dir,
				  const struct qstr *d_name)
{
	int retval = -ENOENT;
	struct buffer_head *bh;
	struct ext4_dir_entry_2 *de;

	bh = ext4_find_entry(dir, d_name, &de, NULL);
	if (IS_ERR(bh))
		return PTR_ERR(bh);
	if (bh) {
		retval = ext4_delete_entry(handle, dir, de, bh);
		brelse(bh);
	}
	return retval;
}

static void ext4_rename_delete(handle_t *handle, struct ext4_renament *ent,
			       int force_reread)
{
	int retval;
	/*
	 * ent->de could have moved from under us during htree split, so make
	 * sure that we are deleting the right entry.  We might also be pointing
	 * to a stale entry in the unused part of ent->bh so just checking inum
	 * and the name isn't enough.
	 */
	if (le32_to_cpu(ent->de->inode) != ent->inode->i_ino ||
	    ent->de->name_len != ent->dentry->d_name.len ||
	    strncmp(ent->de->name, ent->dentry->d_name.name,
		    ent->de->name_len) ||
	    force_reread) {
		retval = ext4_find_delete_entry(handle, ent->dir,
						&ent->dentry->d_name);
	} else {
		retval = ext4_delete_entry(handle, ent->dir, ent->de, ent->bh);
		if (retval == -ENOENT) {
			retval = ext4_find_delete_entry(handle, ent->dir,
							&ent->dentry->d_name);
		}
	}

	if (retval) {
		ext4_warning_inode(ent->dir,
				   "Deleting old file: nlink %d, error=%d",
				   ent->dir->i_nlink, retval);
	}
}

static void ext4_update_dir_count(handle_t *handle, struct ext4_renament *ent)
{
	if (ent->dir_nlink_delta) {
		if (ent->dir_nlink_delta == -1)
			ext4_dec_count(handle, ent->dir);
		else
			ext4_inc_count(handle, ent->dir);
		ext4_mark_inode_dirty(handle, ent->dir);
	}
}

static struct inode *ext4_whiteout_for_rename(struct ext4_renament *ent,
					      int credits, handle_t **h)
{
	struct inode *wh;
	handle_t *handle;
	int retries = 0;

	/*
	 * for inode block, sb block, group summaries,
	 * and inode bitmap
	 */
	credits += (EXT4_MAXQUOTAS_TRANS_BLOCKS(ent->dir->i_sb) +
		    EXT4_XATTR_TRANS_BLOCKS + 4);
retry:
	wh = ext4_new_inode_start_handle(ent->dir, S_IFCHR | WHITEOUT_MODE,
					 &ent->dentry->d_name, 0, NULL,
					 EXT4_HT_DIR, credits);

	handle = ext4_journal_current_handle();
	if (IS_ERR(wh)) {
		if (handle)
			ext4_journal_stop(handle);
		if (PTR_ERR(wh) == -ENOSPC &&
		    ext4_should_retry_alloc(ent->dir->i_sb, &retries))
			goto retry;
	} else {
		*h = handle;
		init_special_inode(wh, wh->i_mode, WHITEOUT_DEV);
		wh->i_op = &ext4_special_inode_operations;
	}
	return wh;
}

/*
 * Anybody can rename anything with this: the permission checks are left to the
 * higher-level routines.
 *
 * n.b.  old_{dentry,inode) refers to the source dentry/inode
 * while new_{dentry,inode) refers to the destination dentry/inode
 * This comes from rename(const char *oldpath, const char *newpath)
 */
static int ext4_rename(struct inode *old_dir, struct dentry *old_dentry,
		       struct inode *new_dir, struct dentry *new_dentry,
		       unsigned int flags)
{
	handle_t *handle = NULL;
	struct ext4_renament old = {
		.dir = old_dir,
		.dentry = old_dentry,
		.inode = d_inode(old_dentry),
	};
	struct ext4_renament new = {
		.dir = new_dir,
		.dentry = new_dentry,
		.inode = d_inode(new_dentry),
	};
	int force_reread;
	int retval;
	struct inode *whiteout = NULL;
	int credits;
	u8 old_file_type;

	if ((ext4_test_inode_flag(new_dir, EXT4_INODE_PROJINHERIT)) &&
	    (!projid_eq(EXT4_I(new_dir)->i_projid,
			EXT4_I(old_dentry->d_inode)->i_projid)))
		return -EXDEV;

	if ((ext4_encrypted_inode(old_dir) &&
	     !fscrypt_has_encryption_key(old_dir)) ||
	    (ext4_encrypted_inode(new_dir) &&
	     !fscrypt_has_encryption_key(new_dir)))
		return -ENOKEY;

	retval = dquot_initialize(old.dir);
	if (retval)
		return retval;
	retval = dquot_initialize(new.dir);
	if (retval)
		return retval;

	/* Initialize quotas before so that eventual writes go
	 * in separate transaction */
	if (new.inode) {
		retval = dquot_initialize(new.inode);
		if (retval)
			return retval;
	}

	old.bh = ext4_find_entry(old.dir, &old.dentry->d_name, &old.de, NULL);
	if (IS_ERR(old.bh))
		return PTR_ERR(old.bh);
	/*
	 *  Check for inode number is _not_ due to possible IO errors.
	 *  We might rmdir the source, keep it as pwd of some process
	 *  and merrily kill the link to whatever was created under the
	 *  same name. Goodbye sticky bit ;-<
	 */
	retval = -ENOENT;
	if (!old.bh || le32_to_cpu(old.de->inode) != old.inode->i_ino)
		goto end_rename;

	if ((old.dir != new.dir) &&
	    ext4_encrypted_inode(new.dir) &&
	    !fscrypt_has_permitted_context(new.dir, old.inode)) {
		retval = -EPERM;
		goto end_rename;
	}

	new.bh = ext4_find_entry(new.dir, &new.dentry->d_name,
				 &new.de, &new.inlined);
	if (IS_ERR(new.bh)) {
		retval = PTR_ERR(new.bh);
		new.bh = NULL;
		goto end_rename;
	}
	if (new.bh) {
		if (!new.inode) {
			brelse(new.bh);
			new.bh = NULL;
		}
	}
	if (new.inode && !test_opt(new.dir->i_sb, NO_AUTO_DA_ALLOC))
		ext4_alloc_da_blocks(old.inode);

	credits = (2 * EXT4_DATA_TRANS_BLOCKS(old.dir->i_sb) +
		   EXT4_INDEX_EXTRA_TRANS_BLOCKS + 2);
	if (!(flags & RENAME_WHITEOUT)) {
		handle = ext4_journal_start(old.dir, EXT4_HT_DIR, credits);
		if (IS_ERR(handle)) {
			retval = PTR_ERR(handle);
			handle = NULL;
			goto end_rename;
		}
	} else {
		whiteout = ext4_whiteout_for_rename(&old, credits, &handle);
		if (IS_ERR(whiteout)) {
			retval = PTR_ERR(whiteout);
			whiteout = NULL;
			goto end_rename;
		}
	}

	if (IS_DIRSYNC(old.dir) || IS_DIRSYNC(new.dir))
		ext4_handle_sync(handle);

	if (S_ISDIR(old.inode->i_mode)) {
		if (new.inode) {
			retval = -ENOTEMPTY;
			if (!ext4_empty_dir(new.inode))
				goto end_rename;
		} else {
			retval = -EMLINK;
			if (new.dir != old.dir && EXT4_DIR_LINK_MAX(new.dir))
				goto end_rename;
		}
		retval = ext4_rename_dir_prepare(handle, &old);
		if (retval)
			goto end_rename;
	}
	/*
	 * If we're renaming a file within an inline_data dir and adding or
	 * setting the new dirent causes a conversion from inline_data to
	 * extents/blockmap, we need to force the dirent delete code to
	 * re-read the directory, or else we end up trying to delete a dirent
	 * from what is now the extent tree root (or a block map).
	 */
	force_reread = (new.dir->i_ino == old.dir->i_ino &&
			ext4_test_inode_flag(new.dir, EXT4_INODE_INLINE_DATA));

	old_file_type = old.de->file_type;
	if (whiteout) {
		/*
		 * Do this before adding a new entry, so the old entry is sure
		 * to be still pointing to the valid old entry.
		 */
		retval = ext4_setent(handle, &old, whiteout->i_ino,
				     EXT4_FT_CHRDEV);
		if (retval)
			goto end_rename;
		ext4_mark_inode_dirty(handle, whiteout);
	}
	if (!new.bh) {
		retval = ext4_add_entry(handle, new.dentry, old.inode);
		if (retval)
			goto end_rename;
	} else {
		retval = ext4_setent(handle, &new,
				     old.inode->i_ino, old_file_type);
		if (retval)
			goto end_rename;
	}
	if (force_reread)
		force_reread = !ext4_test_inode_flag(new.dir,
						     EXT4_INODE_INLINE_DATA);

	/*
	 * Like most other Unix systems, set the ctime for inodes on a
	 * rename.
	 */
	old.inode->i_ctime = current_time(old.inode);
	ext4_mark_inode_dirty(handle, old.inode);

	if (!whiteout) {
		/*
		 * ok, that's it
		 */
		ext4_rename_delete(handle, &old, force_reread);
	}

	if (new.inode) {
		ext4_dec_count(handle, new.inode);
		new.inode->i_ctime = current_time(new.inode);
	}
	old.dir->i_ctime = old.dir->i_mtime = current_time(old.dir);
	ext4_update_dx_flag(old.dir);
	if (old.dir_bh) {
		retval = ext4_rename_dir_finish(handle, &old, new.dir->i_ino);
		if (retval)
			goto end_rename;

		ext4_dec_count(handle, old.dir);
		if (new.inode) {
			/* checked ext4_empty_dir above, can't have another
			 * parent, ext4_dec_count() won't work for many-linked
			 * dirs */
			clear_nlink(new.inode);
		} else {
			ext4_inc_count(handle, new.dir);
			ext4_update_dx_flag(new.dir);
			ext4_mark_inode_dirty(handle, new.dir);
		}
	}
	ext4_mark_inode_dirty(handle, old.dir);
	if (new.inode) {
		ext4_mark_inode_dirty(handle, new.inode);
		if (!new.inode->i_nlink)
			ext4_orphan_add(handle, new.inode);
	}
	retval = 0;

end_rename:
	brelse(old.dir_bh);
	brelse(old.bh);
	brelse(new.bh);
	if (whiteout) {
		if (retval)
			drop_nlink(whiteout);
		unlock_new_inode(whiteout);
		iput(whiteout);
	}
	if (handle)
		ext4_journal_stop(handle);
	return retval;
}

static int ext4_cross_rename(struct inode *old_dir, struct dentry *old_dentry,
			     struct inode *new_dir, struct dentry *new_dentry)
{
	handle_t *handle = NULL;
	struct ext4_renament old = {
		.dir = old_dir,
		.dentry = old_dentry,
		.inode = d_inode(old_dentry),
	};
	struct ext4_renament new = {
		.dir = new_dir,
		.dentry = new_dentry,
		.inode = d_inode(new_dentry),
	};
	u8 new_file_type;
	int retval;
	struct timespec ctime;

	if ((ext4_encrypted_inode(old_dir) &&
	     !fscrypt_has_encryption_key(old_dir)) ||
	    (ext4_encrypted_inode(new_dir) &&
	     !fscrypt_has_encryption_key(new_dir)))
		return -ENOKEY;

	if ((ext4_encrypted_inode(old_dir) ||
	     ext4_encrypted_inode(new_dir)) &&
	    (old_dir != new_dir) &&
	    (!fscrypt_has_permitted_context(new_dir, old.inode) ||
	     !fscrypt_has_permitted_context(old_dir, new.inode)))
		return -EPERM;

	if ((ext4_test_inode_flag(new_dir, EXT4_INODE_PROJINHERIT) &&
	     !projid_eq(EXT4_I(new_dir)->i_projid,
			EXT4_I(old_dentry->d_inode)->i_projid)) ||
	    (ext4_test_inode_flag(old_dir, EXT4_INODE_PROJINHERIT) &&
	     !projid_eq(EXT4_I(old_dir)->i_projid,
			EXT4_I(new_dentry->d_inode)->i_projid)))
		return -EXDEV;

	retval = dquot_initialize(old.dir);
	if (retval)
		return retval;
	retval = dquot_initialize(new.dir);
	if (retval)
		return retval;

	old.bh = ext4_find_entry(old.dir, &old.dentry->d_name,
				 &old.de, &old.inlined);
	if (IS_ERR(old.bh))
		return PTR_ERR(old.bh);
	/*
	 *  Check for inode number is _not_ due to possible IO errors.
	 *  We might rmdir the source, keep it as pwd of some process
	 *  and merrily kill the link to whatever was created under the
	 *  same name. Goodbye sticky bit ;-<
	 */
	retval = -ENOENT;
	if (!old.bh || le32_to_cpu(old.de->inode) != old.inode->i_ino)
		goto end_rename;

	new.bh = ext4_find_entry(new.dir, &new.dentry->d_name,
				 &new.de, &new.inlined);
	if (IS_ERR(new.bh)) {
		retval = PTR_ERR(new.bh);
		new.bh = NULL;
		goto end_rename;
	}

	/* RENAME_EXCHANGE case: old *and* new must both exist */
	if (!new.bh || le32_to_cpu(new.de->inode) != new.inode->i_ino)
		goto end_rename;

	handle = ext4_journal_start(old.dir, EXT4_HT_DIR,
		(2 * EXT4_DATA_TRANS_BLOCKS(old.dir->i_sb) +
		 2 * EXT4_INDEX_EXTRA_TRANS_BLOCKS + 2));
	if (IS_ERR(handle)) {
		retval = PTR_ERR(handle);
		handle = NULL;
		goto end_rename;
	}

	if (IS_DIRSYNC(old.dir) || IS_DIRSYNC(new.dir))
		ext4_handle_sync(handle);

	if (S_ISDIR(old.inode->i_mode)) {
		old.is_dir = true;
		retval = ext4_rename_dir_prepare(handle, &old);
		if (retval)
			goto end_rename;
	}
	if (S_ISDIR(new.inode->i_mode)) {
		new.is_dir = true;
		retval = ext4_rename_dir_prepare(handle, &new);
		if (retval)
			goto end_rename;
	}

	/*
	 * Other than the special case of overwriting a directory, parents'
	 * nlink only needs to be modified if this is a cross directory rename.
	 */
	if (old.dir != new.dir && old.is_dir != new.is_dir) {
		old.dir_nlink_delta = old.is_dir ? -1 : 1;
		new.dir_nlink_delta = -old.dir_nlink_delta;
		retval = -EMLINK;
		if ((old.dir_nlink_delta > 0 && EXT4_DIR_LINK_MAX(old.dir)) ||
		    (new.dir_nlink_delta > 0 && EXT4_DIR_LINK_MAX(new.dir)))
			goto end_rename;
	}

	new_file_type = new.de->file_type;
	retval = ext4_setent(handle, &new, old.inode->i_ino, old.de->file_type);
	if (retval)
		goto end_rename;

	retval = ext4_setent(handle, &old, new.inode->i_ino, new_file_type);
	if (retval)
		goto end_rename;

	/*
	 * Like most other Unix systems, set the ctime for inodes on a
	 * rename.
	 */
	ctime = current_time(old.inode);
	old.inode->i_ctime = ctime;
	new.inode->i_ctime = ctime;
	ext4_mark_inode_dirty(handle, old.inode);
	ext4_mark_inode_dirty(handle, new.inode);

	if (old.dir_bh) {
		retval = ext4_rename_dir_finish(handle, &old, new.dir->i_ino);
		if (retval)
			goto end_rename;
	}
	if (new.dir_bh) {
		retval = ext4_rename_dir_finish(handle, &new, old.dir->i_ino);
		if (retval)
			goto end_rename;
	}
	ext4_update_dir_count(handle, &old);
	ext4_update_dir_count(handle, &new);
	retval = 0;

end_rename:
	brelse(old.dir_bh);
	brelse(new.dir_bh);
	brelse(old.bh);
	brelse(new.bh);
	if (handle)
		ext4_journal_stop(handle);
	return retval;
}

static int ext4_rename2(struct inode *old_dir, struct dentry *old_dentry,
			struct inode *new_dir, struct dentry *new_dentry,
			unsigned int flags)
{
	if (unlikely(ext4_forced_shutdown(EXT4_SB(old_dir->i_sb))))
		return -EIO;

	if (flags & ~(RENAME_NOREPLACE | RENAME_EXCHANGE | RENAME_WHITEOUT))
		return -EINVAL;

	if (flags & RENAME_EXCHANGE) {
		return ext4_cross_rename(old_dir, old_dentry,
					 new_dir, new_dentry);
	}

	return ext4_rename(old_dir, old_dentry, new_dir, new_dentry, flags);
}

/*
 * directories can handle most operations...
 */
const struct inode_operations ext4_dir_inode_operations = {
	.create		= ext4_create,
	.lookup		= ext4_lookup,
	.link		= ext4_link,
	.unlink		= ext4_unlink,
	.symlink	= ext4_symlink,
	.mkdir		= ext4_mkdir,
	.rmdir		= ext4_rmdir,
	.mknod		= ext4_mknod,
	.tmpfile	= ext4_tmpfile,
	.rename		= ext4_rename2,
	.setattr	= ext4_setattr,
	.getattr	= ext4_getattr,
	.listxattr	= ext4_listxattr,
	.get_acl	= ext4_get_acl,
	.set_acl	= ext4_set_acl,
	.fiemap         = ext4_fiemap,
};

const struct inode_operations ext4_special_inode_operations = {
	.setattr	= ext4_setattr,
	.getattr	= ext4_getattr,
	.listxattr	= ext4_listxattr,
	.get_acl	= ext4_get_acl,
	.set_acl	= ext4_set_acl,
};
