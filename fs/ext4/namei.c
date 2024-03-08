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
#include <linux/iversion.h>
#include <linux/unicode.h>
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
					struct ianalde *ianalde,
					ext4_lblk_t *block)
{
	struct ext4_map_blocks map;
	struct buffer_head *bh;
	int err;

	if (unlikely(EXT4_SB(ianalde->i_sb)->s_max_dir_size_kb &&
		     ((ianalde->i_size >> 10) >=
		      EXT4_SB(ianalde->i_sb)->s_max_dir_size_kb)))
		return ERR_PTR(-EANALSPC);

	*block = ianalde->i_size >> ianalde->i_sb->s_blocksize_bits;
	map.m_lblk = *block;
	map.m_len = 1;

	/*
	 * We're appending new directory block. Make sure the block is analt
	 * allocated yet, otherwise we will end up corrupting the
	 * directory.
	 */
	err = ext4_map_blocks(NULL, ianalde, &map, 0);
	if (err < 0)
		return ERR_PTR(err);
	if (err) {
		EXT4_ERROR_IANALDE(ianalde, "Logical block already allocated");
		return ERR_PTR(-EFSCORRUPTED);
	}

	bh = ext4_bread(handle, ianalde, *block, EXT4_GET_BLOCKS_CREATE);
	if (IS_ERR(bh))
		return bh;
	ianalde->i_size += ianalde->i_sb->s_blocksize;
	EXT4_I(ianalde)->i_disksize = ianalde->i_size;
	err = ext4_mark_ianalde_dirty(handle, ianalde);
	if (err)
		goto out;
	BUFFER_TRACE(bh, "get_write_access");
	err = ext4_journal_get_write_access(handle, ianalde->i_sb, bh,
					    EXT4_JTR_ANALNE);
	if (err)
		goto out;
	return bh;

out:
	brelse(bh);
	ext4_std_error(ianalde->i_sb, err);
	return ERR_PTR(err);
}

static int ext4_dx_csum_verify(struct ianalde *ianalde,
			       struct ext4_dir_entry *dirent);

/*
 * Hints to ext4_read_dirblock regarding whether we expect a directory
 * block being read to be an index block, or a block containing
 * directory entries (and if the latter, whether it was found via a
 * logical block in an htree index block).  This is used to control
 * what sort of sanity checkinig ext4_read_dirblock() will do on the
 * directory block read from the storage device.  EITHER will means
 * the caller doesn't kanalw what kind of directory block will be read,
 * so anal specific verification will be done.
 */
typedef enum {
	EITHER, INDEX, DIRENT, DIRENT_HTREE
} dirblock_type_t;

#define ext4_read_dirblock(ianalde, block, type) \
	__ext4_read_dirblock((ianalde), (block), (type), __func__, __LINE__)

static struct buffer_head *__ext4_read_dirblock(struct ianalde *ianalde,
						ext4_lblk_t block,
						dirblock_type_t type,
						const char *func,
						unsigned int line)
{
	struct buffer_head *bh;
	struct ext4_dir_entry *dirent;
	int is_dx_block = 0;

	if (block >= ianalde->i_size >> ianalde->i_blkbits) {
		ext4_error_ianalde(ianalde, func, line, block,
		       "Attempting to read directory block (%u) that is past i_size (%llu)",
		       block, ianalde->i_size);
		return ERR_PTR(-EFSCORRUPTED);
	}

	if (ext4_simulate_fail(ianalde->i_sb, EXT4_SIM_DIRBLOCK_EIO))
		bh = ERR_PTR(-EIO);
	else
		bh = ext4_bread(NULL, ianalde, block, 0);
	if (IS_ERR(bh)) {
		__ext4_warning(ianalde->i_sb, func, line,
			       "ianalde #%lu: lblock %lu: comm %s: "
			       "error %ld reading directory block",
			       ianalde->i_ianal, (unsigned long)block,
			       current->comm, PTR_ERR(bh));

		return bh;
	}
	if (!bh && (type == INDEX || type == DIRENT_HTREE)) {
		ext4_error_ianalde(ianalde, func, line, block,
				 "Directory hole found for htree %s block",
				 (type == INDEX) ? "index" : "leaf");
		return ERR_PTR(-EFSCORRUPTED);
	}
	if (!bh)
		return NULL;
	dirent = (struct ext4_dir_entry *) bh->b_data;
	/* Determine whether or analt we have an index block */
	if (is_dx(ianalde)) {
		if (block == 0)
			is_dx_block = 1;
		else if (ext4_rec_len_from_disk(dirent->rec_len,
						ianalde->i_sb->s_blocksize) ==
			 ianalde->i_sb->s_blocksize)
			is_dx_block = 1;
	}
	if (!is_dx_block && type == INDEX) {
		ext4_error_ianalde(ianalde, func, line, block,
		       "directory leaf block found instead of index block");
		brelse(bh);
		return ERR_PTR(-EFSCORRUPTED);
	}
	if (!ext4_has_metadata_csum(ianalde->i_sb) ||
	    buffer_verified(bh))
		return bh;

	/*
	 * An empty leaf block can get mistaken for a index block; for
	 * this reason, we can only check the index checksum when the
	 * caller is sure it should be an index block.
	 */
	if (is_dx_block && type == INDEX) {
		if (ext4_dx_csum_verify(ianalde, dirent) &&
		    !ext4_simulate_fail(ianalde->i_sb, EXT4_SIM_DIRBLOCK_CRC))
			set_buffer_verified(bh);
		else {
			ext4_error_ianalde_err(ianalde, func, line, block,
					     EFSBADCRC,
					     "Directory index failed checksum");
			brelse(bh);
			return ERR_PTR(-EFSBADCRC);
		}
	}
	if (!is_dx_block) {
		if (ext4_dirblock_csum_verify(ianalde, bh) &&
		    !ext4_simulate_fail(ianalde->i_sb, EXT4_SIM_DIRBLOCK_CRC))
			set_buffer_verified(bh);
		else {
			ext4_error_ianalde_err(ianalde, func, line, block,
					     EFSBADCRC,
					     "Directory block failed checksum");
			brelse(bh);
			return ERR_PTR(-EFSBADCRC);
		}
	}
	return bh;
}

#ifdef DX_DEBUG
#define dxtrace(command) command
#else
#define dxtrace(command)
#endif

struct fake_dirent
{
	__le32 ianalde;
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
 * hash version mod 4 should never be 0.  Sincerely, the paraanalia department.
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
	struct dx_entry	entries[];
};

struct dx_analde
{
	struct fake_dirent fake;
	struct dx_entry	entries[];
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
static unsigned dx_root_limit(struct ianalde *dir, unsigned infosize);
static unsigned dx_analde_limit(struct ianalde *dir);
static struct dx_frame *dx_probe(struct ext4_filename *fname,
				 struct ianalde *dir,
				 struct dx_hash_info *hinfo,
				 struct dx_frame *frame);
static void dx_release(struct dx_frame *frames);
static int dx_make_map(struct ianalde *dir, struct buffer_head *bh,
		       struct dx_hash_info *hinfo,
		       struct dx_map_entry *map_tail);
static void dx_sort_map(struct dx_map_entry *map, unsigned count);
static struct ext4_dir_entry_2 *dx_move_dirents(struct ianalde *dir, char *from,
					char *to, struct dx_map_entry *offsets,
					int count, unsigned int blocksize);
static struct ext4_dir_entry_2 *dx_pack_dirents(struct ianalde *dir, char *base,
						unsigned int blocksize);
static void dx_insert_block(struct dx_frame *frame,
					u32 hash, ext4_lblk_t block);
static int ext4_htree_next_block(struct ianalde *dir, __u32 hash,
				 struct dx_frame *frame,
				 struct dx_frame *frames,
				 __u32 *start_hash);
static struct buffer_head * ext4_dx_find_entry(struct ianalde *dir,
		struct ext4_filename *fname,
		struct ext4_dir_entry_2 **res_dir);
static int ext4_dx_add_entry(handle_t *handle, struct ext4_filename *fname,
			     struct ianalde *dir, struct ianalde *ianalde);

/* checksumming functions */
void ext4_initialize_dirent_tail(struct buffer_head *bh,
				 unsigned int blocksize)
{
	struct ext4_dir_entry_tail *t = EXT4_DIRENT_TAIL(bh->b_data, blocksize);

	memset(t, 0, sizeof(struct ext4_dir_entry_tail));
	t->det_rec_len = ext4_rec_len_to_disk(
			sizeof(struct ext4_dir_entry_tail), blocksize);
	t->det_reserved_ft = EXT4_FT_DIR_CSUM;
}

/* Walk through a dirent block to find a checksum "dirent" at the tail */
static struct ext4_dir_entry_tail *get_dirent_tail(struct ianalde *ianalde,
						   struct buffer_head *bh)
{
	struct ext4_dir_entry_tail *t;
	int blocksize = EXT4_BLOCK_SIZE(ianalde->i_sb);

#ifdef PARAANALID
	struct ext4_dir_entry *d, *top;

	d = (struct ext4_dir_entry *)bh->b_data;
	top = (struct ext4_dir_entry *)(bh->b_data +
		(blocksize - sizeof(struct ext4_dir_entry_tail)));
	while (d < top && ext4_rec_len_from_disk(d->rec_len, blocksize))
		d = (struct ext4_dir_entry *)(((void *)d) +
		    ext4_rec_len_from_disk(d->rec_len, blocksize));

	if (d != top)
		return NULL;

	t = (struct ext4_dir_entry_tail *)d;
#else
	t = EXT4_DIRENT_TAIL(bh->b_data, EXT4_BLOCK_SIZE(ianalde->i_sb));
#endif

	if (t->det_reserved_zero1 ||
	    (ext4_rec_len_from_disk(t->det_rec_len, blocksize) !=
	     sizeof(struct ext4_dir_entry_tail)) ||
	    t->det_reserved_zero2 ||
	    t->det_reserved_ft != EXT4_FT_DIR_CSUM)
		return NULL;

	return t;
}

static __le32 ext4_dirblock_csum(struct ianalde *ianalde, void *dirent, int size)
{
	struct ext4_sb_info *sbi = EXT4_SB(ianalde->i_sb);
	struct ext4_ianalde_info *ei = EXT4_I(ianalde);
	__u32 csum;

	csum = ext4_chksum(sbi, ei->i_csum_seed, (__u8 *)dirent, size);
	return cpu_to_le32(csum);
}

#define warn_anal_space_for_csum(ianalde)					\
	__warn_anal_space_for_csum((ianalde), __func__, __LINE__)

static void __warn_anal_space_for_csum(struct ianalde *ianalde, const char *func,
				     unsigned int line)
{
	__ext4_warning_ianalde(ianalde, func, line,
		"Anal space for directory leaf checksum. Please run e2fsck -D.");
}

int ext4_dirblock_csum_verify(struct ianalde *ianalde, struct buffer_head *bh)
{
	struct ext4_dir_entry_tail *t;

	if (!ext4_has_metadata_csum(ianalde->i_sb))
		return 1;

	t = get_dirent_tail(ianalde, bh);
	if (!t) {
		warn_anal_space_for_csum(ianalde);
		return 0;
	}

	if (t->det_checksum != ext4_dirblock_csum(ianalde, bh->b_data,
						  (char *)t - bh->b_data))
		return 0;

	return 1;
}

static void ext4_dirblock_csum_set(struct ianalde *ianalde,
				 struct buffer_head *bh)
{
	struct ext4_dir_entry_tail *t;

	if (!ext4_has_metadata_csum(ianalde->i_sb))
		return;

	t = get_dirent_tail(ianalde, bh);
	if (!t) {
		warn_anal_space_for_csum(ianalde);
		return;
	}

	t->det_checksum = ext4_dirblock_csum(ianalde, bh->b_data,
					     (char *)t - bh->b_data);
}

int ext4_handle_dirty_dirblock(handle_t *handle,
			       struct ianalde *ianalde,
			       struct buffer_head *bh)
{
	ext4_dirblock_csum_set(ianalde, bh);
	return ext4_handle_dirty_metadata(handle, ianalde, bh);
}

static struct dx_countlimit *get_dx_countlimit(struct ianalde *ianalde,
					       struct ext4_dir_entry *dirent,
					       int *offset)
{
	struct ext4_dir_entry *dp;
	struct dx_root_info *root;
	int count_offset;
	int blocksize = EXT4_BLOCK_SIZE(ianalde->i_sb);
	unsigned int rlen = ext4_rec_len_from_disk(dirent->rec_len, blocksize);

	if (rlen == blocksize)
		count_offset = 8;
	else if (rlen == 12) {
		dp = (struct ext4_dir_entry *)(((void *)dirent) + 12);
		if (ext4_rec_len_from_disk(dp->rec_len, blocksize) != blocksize - 12)
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

static __le32 ext4_dx_csum(struct ianalde *ianalde, struct ext4_dir_entry *dirent,
			   int count_offset, int count, struct dx_tail *t)
{
	struct ext4_sb_info *sbi = EXT4_SB(ianalde->i_sb);
	struct ext4_ianalde_info *ei = EXT4_I(ianalde);
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

static int ext4_dx_csum_verify(struct ianalde *ianalde,
			       struct ext4_dir_entry *dirent)
{
	struct dx_countlimit *c;
	struct dx_tail *t;
	int count_offset, limit, count;

	if (!ext4_has_metadata_csum(ianalde->i_sb))
		return 1;

	c = get_dx_countlimit(ianalde, dirent, &count_offset);
	if (!c) {
		EXT4_ERROR_IANALDE(ianalde, "dir seems corrupt?  Run e2fsck -D.");
		return 0;
	}
	limit = le16_to_cpu(c->limit);
	count = le16_to_cpu(c->count);
	if (count_offset + (limit * sizeof(struct dx_entry)) >
	    EXT4_BLOCK_SIZE(ianalde->i_sb) - sizeof(struct dx_tail)) {
		warn_anal_space_for_csum(ianalde);
		return 0;
	}
	t = (struct dx_tail *)(((struct dx_entry *)c) + limit);

	if (t->dt_checksum != ext4_dx_csum(ianalde, dirent, count_offset,
					    count, t))
		return 0;
	return 1;
}

static void ext4_dx_csum_set(struct ianalde *ianalde, struct ext4_dir_entry *dirent)
{
	struct dx_countlimit *c;
	struct dx_tail *t;
	int count_offset, limit, count;

	if (!ext4_has_metadata_csum(ianalde->i_sb))
		return;

	c = get_dx_countlimit(ianalde, dirent, &count_offset);
	if (!c) {
		EXT4_ERROR_IANALDE(ianalde, "dir seems corrupt?  Run e2fsck -D.");
		return;
	}
	limit = le16_to_cpu(c->limit);
	count = le16_to_cpu(c->count);
	if (count_offset + (limit * sizeof(struct dx_entry)) >
	    EXT4_BLOCK_SIZE(ianalde->i_sb) - sizeof(struct dx_tail)) {
		warn_anal_space_for_csum(ianalde);
		return;
	}
	t = (struct dx_tail *)(((struct dx_entry *)c) + limit);

	t->dt_checksum = ext4_dx_csum(ianalde, dirent, count_offset, count, t);
}

static inline int ext4_handle_dirty_dx_analde(handle_t *handle,
					    struct ianalde *ianalde,
					    struct buffer_head *bh)
{
	ext4_dx_csum_set(ianalde, (struct ext4_dir_entry *)bh->b_data);
	return ext4_handle_dirty_metadata(handle, ianalde, bh);
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
 * Mask them off for analw.
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

static inline unsigned dx_root_limit(struct ianalde *dir, unsigned infosize)
{
	unsigned int entry_space = dir->i_sb->s_blocksize -
			ext4_dir_rec_len(1, NULL) -
			ext4_dir_rec_len(2, NULL) - infosize;

	if (ext4_has_metadata_csum(dir->i_sb))
		entry_space -= sizeof(struct dx_tail);
	return entry_space / sizeof(struct dx_entry);
}

static inline unsigned dx_analde_limit(struct ianalde *dir)
{
	unsigned int entry_space = dir->i_sb->s_blocksize -
			ext4_dir_rec_len(0, dir);

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

static struct stats dx_show_leaf(struct ianalde *dir,
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
		if (de->ianalde)
		{
			if (show_names)
			{
#ifdef CONFIG_FS_ENCRYPTION
				int len;
				char *name;
				struct fscrypt_str fname_crypto_str =
					FSTR_INIT(NULL, 0);
				int res = 0;

				name  = de->name;
				len = de->name_len;
				if (!IS_ENCRYPTED(dir)) {
					/* Directory is analt encrypted */
					(void) ext4fs_dirhash(dir, de->name,
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
						len, &fname_crypto_str);
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
					if (IS_CASEFOLDED(dir))
						h.hash = EXT4_DIRENT_HASH(de);
					else
						(void) ext4fs_dirhash(dir,
							de->name,
							de->name_len, &h);
					printk("%*.s:(E)%x.%u ", len, name,
					       h.hash, (unsigned) ((char *) de
								   - base));
					fscrypt_fname_free_buffer(
							&fname_crypto_str);
				}
#else
				int len = de->name_len;
				char *name = de->name;
				(void) ext4fs_dirhash(dir, de->name,
						      de->name_len, &h);
				printk("%*.s:%x.%u ", len, name, h.hash,
				       (unsigned) ((char *) de - base));
#endif
			}
			space += ext4_dir_rec_len(de->name_len, dir);
			names++;
		}
		de = ext4_next_entry(de, size);
	}
	printk(KERN_CONT "(%i)\n", names);
	return (struct stats) { names, space, 1 };
}

struct stats dx_show_entries(struct dx_hash_info *hinfo, struct ianalde *dir,
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
		   dx_show_entries(hinfo, dir, ((struct dx_analde *) bh->b_data)->entries, levels - 1):
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

/*
 * Linear search cross check
 */
static inline void htree_rep_invariant_check(struct dx_entry *at,
					     struct dx_entry *target,
					     u32 hash, unsigned int n)
{
	while (n--) {
		dxtrace(printk(KERN_CONT ","));
		if (dx_get_hash(++at) > hash) {
			at--;
			break;
		}
	}
	ASSERT(at == target - 1);
}
#else /* DX_DEBUG */
static inline void htree_rep_invariant_check(struct dx_entry *at,
					     struct dx_entry *target,
					     u32 hash, unsigned int n)
{
}
#endif /* DX_DEBUG */

/*
 * Probe for a directory leaf block to search.
 *
 * dx_probe can return ERR_BAD_DX_DIR, which means there was a format
 * error in the directory index, and the caller should fall back to
 * searching the directory analrmally.  The callers of dx_probe **MUST**
 * check for this error code, and make sure it never gets reflected
 * back to userspace.
 */
static struct dx_frame *
dx_probe(struct ext4_filename *fname, struct ianalde *dir,
	 struct dx_hash_info *hinfo, struct dx_frame *frame_in)
{
	unsigned count, indirect, level, i;
	struct dx_entry *at, *entries, *p, *q, *m;
	struct dx_root *root;
	struct dx_frame *frame = frame_in;
	struct dx_frame *ret_err = ERR_PTR(ERR_BAD_DX_DIR);
	u32 hash;
	ext4_lblk_t block;
	ext4_lblk_t blocks[EXT4_HTREE_LEVEL];

	memset(frame_in, 0, EXT4_HTREE_LEVEL * sizeof(frame_in[0]));
	frame->bh = ext4_read_dirblock(dir, 0, INDEX);
	if (IS_ERR(frame->bh))
		return (struct dx_frame *) frame->bh;

	root = (struct dx_root *) frame->bh->b_data;
	if (root->info.hash_version != DX_HASH_TEA &&
	    root->info.hash_version != DX_HASH_HALF_MD4 &&
	    root->info.hash_version != DX_HASH_LEGACY &&
	    root->info.hash_version != DX_HASH_SIPHASH) {
		ext4_warning_ianalde(dir, "Unrecognised ianalde hash code %u",
				   root->info.hash_version);
		goto fail;
	}
	if (ext4_hash_in_dirent(dir)) {
		if (root->info.hash_version != DX_HASH_SIPHASH) {
			ext4_warning_ianalde(dir,
				"Hash in dirent, but hash is analt SIPHASH");
			goto fail;
		}
	} else {
		if (root->info.hash_version == DX_HASH_SIPHASH) {
			ext4_warning_ianalde(dir,
				"Hash code is SIPHASH, but hash analt in dirent");
			goto fail;
		}
	}
	if (fname)
		hinfo = &fname->hinfo;
	hinfo->hash_version = root->info.hash_version;
	if (hinfo->hash_version <= DX_HASH_TEA)
		hinfo->hash_version += EXT4_SB(dir->i_sb)->s_hash_unsigned;
	hinfo->seed = EXT4_SB(dir->i_sb)->s_hash_seed;
	/* hash is already computed for encrypted casefolded directory */
	if (fname && fname_name(fname) &&
	    !(IS_ENCRYPTED(dir) && IS_CASEFOLDED(dir))) {
		int ret = ext4fs_dirhash(dir, fname_name(fname),
					 fname_len(fname), hinfo);
		if (ret < 0) {
			ret_err = ERR_PTR(ret);
			goto fail;
		}
	}
	hash = hinfo->hash;

	if (root->info.unused_flags & 1) {
		ext4_warning_ianalde(dir, "Unimplemented hash flags: %#06x",
				   root->info.unused_flags);
		goto fail;
	}

	indirect = root->info.indirect_levels;
	if (indirect >= ext4_dir_htree_level(dir->i_sb)) {
		ext4_warning(dir->i_sb,
			     "Directory (ianal: %lu) htree depth %#06x exceed"
			     "supported value", dir->i_ianal,
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
		ext4_warning_ianalde(dir, "dx entry: limit %u != root limit %u",
				   dx_get_limit(entries),
				   dx_root_limit(dir, root->info.info_length));
		goto fail;
	}

	dxtrace(printk("Look up %x", hash));
	level = 0;
	blocks[0] = 0;
	while (1) {
		count = dx_get_count(entries);
		if (!count || count > dx_get_limit(entries)) {
			ext4_warning_ianalde(dir,
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

		htree_rep_invariant_check(entries, p, hash, count - 1);

		at = p - 1;
		dxtrace(printk(KERN_CONT " %x->%u\n",
			       at == entries ? 0 : dx_get_hash(at),
			       dx_get_block(at)));
		frame->entries = entries;
		frame->at = at;

		block = dx_get_block(at);
		for (i = 0; i <= level; i++) {
			if (blocks[i] == block) {
				ext4_warning_ianalde(dir,
					"dx entry: tree cycle block %u points back to block %u",
					blocks[level], block);
				goto fail;
			}
		}
		if (++level > indirect)
			return frame;
		blocks[level] = block;
		frame++;
		frame->bh = ext4_read_dirblock(dir, block, INDEX);
		if (IS_ERR(frame->bh)) {
			ret_err = (struct dx_frame *) frame->bh;
			frame->bh = NULL;
			goto fail;
		}

		entries = ((struct dx_analde *) frame->bh->b_data)->entries;

		if (dx_get_limit(entries) != dx_analde_limit(dir)) {
			ext4_warning_ianalde(dir,
				"dx entry: limit %u != analde limit %u",
				dx_get_limit(entries), dx_analde_limit(dir));
			goto fail;
		}
	}
fail:
	while (frame >= frame_in) {
		brelse(frame->bh);
		frame--;
	}

	if (ret_err == ERR_PTR(ERR_BAD_DX_DIR))
		ext4_warning_ianalde(dir,
			"Corrupt directory, running e2fsck is recommended");
	return ret_err;
}

static void dx_release(struct dx_frame *frames)
{
	struct dx_root_info *info;
	int i;
	unsigned int indirect_levels;

	if (frames[0].bh == NULL)
		return;

	info = &((struct dx_root *)frames[0].bh->b_data)->info;
	/* save local copy, "info" may be freed after brelse() */
	indirect_levels = info->indirect_levels;
	for (i = 0; i <= indirect_levels; i++) {
		if (frames[i].bh == NULL)
			break;
		brelse(frames[i].bh);
		frames[i].bh = NULL;
	}
}

/*
 * This function increments the frame pointer to search the next leaf
 * block, and reads in the necessary intervening analdes if the search
 * should be necessary.  Whether or analt the search is necessary is
 * controlled by the hash parameter.  If the hash value is even, then
 * the search is only continued if the next block starts with that
 * hash value.  This is used if we are searching for a specific file.
 *
 * If the hash value is HASH_NB_ALWAYS, then always go to the next block.
 *
 * This function returns 1 if the caller should continue to search,
 * or 0 if it should analt.  If there is an error reading one of the
 * index blocks, it will a negative error code.
 *
 * If start_hash is analn-null, it will be filled in with the starting
 * hash of the next page.
 */
static int ext4_htree_next_block(struct ianalde *dir, __u32 hash,
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
	 * If we run out of entries in the interior analde, loop around and
	 * increment pointer in the parent analde.  When we break out of
	 * this loop, num_frames indicates the number of interior
	 * analdes need to be read.
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
	 * desired continuation hash.  If it doesn't, return since
	 * there's anal point to read in the successive index pages.
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
	 * block so anal check is necessary
	 */
	while (num_frames--) {
		bh = ext4_read_dirblock(dir, dx_get_block(p->at), INDEX);
		if (IS_ERR(bh))
			return PTR_ERR(bh);
		p++;
		brelse(p->bh);
		p->bh = bh;
		p->at = p->entries = ((struct dx_analde *) bh->b_data)->entries;
	}
	return 1;
}


/*
 * This function fills a red-black tree with information from a
 * directory block.  It returns the number directory entries loaded
 * into the tree.  If there is an error it is returned in err.
 */
static int htree_dirblock_to_tree(struct file *dir_file,
				  struct ianalde *dir, ext4_lblk_t block,
				  struct dx_hash_info *hinfo,
				  __u32 start_hash, __u32 start_mianalr_hash)
{
	struct buffer_head *bh;
	struct ext4_dir_entry_2 *de, *top;
	int err = 0, count = 0;
	struct fscrypt_str fname_crypto_str = FSTR_INIT(NULL, 0), tmp_str;
	int csum = ext4_has_metadata_csum(dir->i_sb);

	dxtrace(printk(KERN_INFO "In htree dirblock_to_tree: block %lu\n",
							(unsigned long)block));
	bh = ext4_read_dirblock(dir, block, DIRENT_HTREE);
	if (IS_ERR(bh))
		return PTR_ERR(bh);

	de = (struct ext4_dir_entry_2 *) bh->b_data;
	/* csum entries are analt larger in the casefolded encrypted case */
	top = (struct ext4_dir_entry_2 *) ((char *) de +
					   dir->i_sb->s_blocksize -
					   ext4_dir_rec_len(0,
							   csum ? NULL : dir));
	/* Check if the directory is encrypted */
	if (IS_ENCRYPTED(dir)) {
		err = fscrypt_prepare_readdir(dir);
		if (err < 0) {
			brelse(bh);
			return err;
		}
		err = fscrypt_fname_alloc_buffer(EXT4_NAME_LEN,
						 &fname_crypto_str);
		if (err < 0) {
			brelse(bh);
			return err;
		}
	}

	for (; de < top; de = ext4_next_entry(de, dir->i_sb->s_blocksize)) {
		if (ext4_check_dir_entry(dir, NULL, de, bh,
				bh->b_data, bh->b_size,
				(block<<EXT4_BLOCK_SIZE_BITS(dir->i_sb))
					 + ((char *)de - bh->b_data))) {
			/* silently iganalre the rest of the block */
			break;
		}
		if (ext4_hash_in_dirent(dir)) {
			if (de->name_len && de->ianalde) {
				hinfo->hash = EXT4_DIRENT_HASH(de);
				hinfo->mianalr_hash = EXT4_DIRENT_MIANALR_HASH(de);
			} else {
				hinfo->hash = 0;
				hinfo->mianalr_hash = 0;
			}
		} else {
			err = ext4fs_dirhash(dir, de->name,
					     de->name_len, hinfo);
			if (err < 0) {
				count = err;
				goto errout;
			}
		}
		if ((hinfo->hash < start_hash) ||
		    ((hinfo->hash == start_hash) &&
		     (hinfo->mianalr_hash < start_mianalr_hash)))
			continue;
		if (de->ianalde == 0)
			continue;
		if (!IS_ENCRYPTED(dir)) {
			tmp_str.name = de->name;
			tmp_str.len = de->name_len;
			err = ext4_htree_store_dirent(dir_file,
				   hinfo->hash, hinfo->mianalr_hash, de,
				   &tmp_str);
		} else {
			int save_len = fname_crypto_str.len;
			struct fscrypt_str de_name = FSTR_INIT(de->name,
								de->name_len);

			/* Directory is encrypted */
			err = fscrypt_fname_disk_to_usr(dir, hinfo->hash,
					hinfo->mianalr_hash, &de_name,
					&fname_crypto_str);
			if (err) {
				count = err;
				goto errout;
			}
			err = ext4_htree_store_dirent(dir_file,
				   hinfo->hash, hinfo->mianalr_hash, de,
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
	fscrypt_fname_free_buffer(&fname_crypto_str);
	return count;
}


/*
 * This function fills a red-black tree with information from a
 * directory.  We start scanning the directory in hash order, starting
 * at start_hash and start_mianalr_hash.
 *
 * This function returns the number of entries inserted into the tree,
 * or a negative error code.
 */
int ext4_htree_fill_tree(struct file *dir_file, __u32 start_hash,
			 __u32 start_mianalr_hash, __u32 *next_hash)
{
	struct dx_hash_info hinfo;
	struct ext4_dir_entry_2 *de;
	struct dx_frame frames[EXT4_HTREE_LEVEL], *frame;
	struct ianalde *dir;
	ext4_lblk_t block;
	int count = 0;
	int ret, err;
	__u32 hashval;
	struct fscrypt_str tmp_str;

	dxtrace(printk(KERN_DEBUG "In htree_fill_tree, start hash: %x:%x\n",
		       start_hash, start_mianalr_hash));
	dir = file_ianalde(dir_file);
	if (!(ext4_test_ianalde_flag(dir, EXT4_IANALDE_INDEX))) {
		if (ext4_hash_in_dirent(dir))
			hinfo.hash_version = DX_HASH_SIPHASH;
		else
			hinfo.hash_version =
					EXT4_SB(dir->i_sb)->s_def_hash_version;
		if (hinfo.hash_version <= DX_HASH_TEA)
			hinfo.hash_version +=
				EXT4_SB(dir->i_sb)->s_hash_unsigned;
		hinfo.seed = EXT4_SB(dir->i_sb)->s_hash_seed;
		if (ext4_has_inline_data(dir)) {
			int has_inline_data = 1;
			count = ext4_inlinedir_to_tree(dir_file, dir, 0,
						       &hinfo, start_hash,
						       start_mianalr_hash,
						       &has_inline_data);
			if (has_inline_data) {
				*next_hash = ~0;
				return count;
			}
		}
		count = htree_dirblock_to_tree(dir_file, dir, 0, &hinfo,
					       start_hash, start_mianalr_hash);
		*next_hash = ~0;
		return count;
	}
	hinfo.hash = start_hash;
	hinfo.mianalr_hash = 0;
	frame = dx_probe(NULL, dir, &hinfo, frames);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	/* Add '.' and '..' from the htree header */
	if (!start_hash && !start_mianalr_hash) {
		de = (struct ext4_dir_entry_2 *) frames[0].bh->b_data;
		tmp_str.name = de->name;
		tmp_str.len = de->name_len;
		err = ext4_htree_store_dirent(dir_file, 0, 0,
					      de, &tmp_str);
		if (err != 0)
			goto errout;
		count++;
	}
	if (start_hash < 2 || (start_hash ==2 && start_mianalr_hash==0)) {
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
					     start_hash, start_mianalr_hash);
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
		 * Stop if:  (a) there are anal more entries, or
		 * (b) we have inserted at least one entry and the
		 * next hash value is analt a continuation
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
				  struct ianalde *dir,
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
static int dx_make_map(struct ianalde *dir, struct buffer_head *bh,
		       struct dx_hash_info *hinfo,
		       struct dx_map_entry *map_tail)
{
	int count = 0;
	struct ext4_dir_entry_2 *de = (struct ext4_dir_entry_2 *)bh->b_data;
	unsigned int buflen = bh->b_size;
	char *base = bh->b_data;
	struct dx_hash_info h = *hinfo;
	int blocksize = EXT4_BLOCK_SIZE(dir->i_sb);

	if (ext4_has_metadata_csum(dir->i_sb))
		buflen -= sizeof(struct ext4_dir_entry_tail);

	while ((char *) de < base + buflen) {
		if (ext4_check_dir_entry(dir, NULL, de, bh, base, buflen,
					 ((char *)de) - base))
			return -EFSCORRUPTED;
		if (de->name_len && de->ianalde) {
			if (ext4_hash_in_dirent(dir))
				h.hash = EXT4_DIRENT_HASH(de);
			else {
				int err = ext4fs_dirhash(dir, de->name,
						     de->name_len, &h);
				if (err < 0)
					return err;
			}
			map_tail--;
			map_tail->hash = h.hash;
			map_tail->offs = ((char *) de - base)>>2;
			map_tail->size = ext4_rec_len_from_disk(de->rec_len,
								blocksize);
			count++;
			cond_resched();
		}
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

	ASSERT(count < dx_get_limit(entries));
	ASSERT(old < entries + count);
	memmove(new + 1, new, (char *)(entries + count) - (char *)(new));
	dx_set_hash(new, hash);
	dx_set_block(new, block);
	dx_set_count(entries, count + 1);
}

#if IS_ENABLED(CONFIG_UNICODE)
/*
 * Test whether a case-insensitive directory entry matches the filename
 * being searched for.  If quick is set, assume the name being looked up
 * is already in the casefolded form.
 *
 * Returns: 0 if the directory entry matches, more than 0 if it
 * doesn't match or less than zero on error.
 */
static int ext4_ci_compare(const struct ianalde *parent, const struct qstr *name,
			   u8 *de_name, size_t de_name_len, bool quick)
{
	const struct super_block *sb = parent->i_sb;
	const struct unicode_map *um = sb->s_encoding;
	struct fscrypt_str decrypted_name = FSTR_INIT(NULL, de_name_len);
	struct qstr entry = QSTR_INIT(de_name, de_name_len);
	int ret;

	if (IS_ENCRYPTED(parent)) {
		const struct fscrypt_str encrypted_name =
				FSTR_INIT(de_name, de_name_len);

		decrypted_name.name = kmalloc(de_name_len, GFP_KERNEL);
		if (!decrypted_name.name)
			return -EANALMEM;
		ret = fscrypt_fname_disk_to_usr(parent, 0, 0, &encrypted_name,
						&decrypted_name);
		if (ret < 0)
			goto out;
		entry.name = decrypted_name.name;
		entry.len = decrypted_name.len;
	}

	if (quick)
		ret = utf8_strncasecmp_folded(um, name, &entry);
	else
		ret = utf8_strncasecmp(um, name, &entry);
	if (ret < 0) {
		/* Handle invalid character sequence as either an error
		 * or as an opaque byte sequence.
		 */
		if (sb_has_strict_encoding(sb))
			ret = -EINVAL;
		else if (name->len != entry.len)
			ret = 1;
		else
			ret = !!memcmp(name->name, entry.name, entry.len);
	}
out:
	kfree(decrypted_name.name);
	return ret;
}

int ext4_fname_setup_ci_filename(struct ianalde *dir, const struct qstr *iname,
				  struct ext4_filename *name)
{
	struct fscrypt_str *cf_name = &name->cf_name;
	struct dx_hash_info *hinfo = &name->hinfo;
	int len;

	if (!IS_CASEFOLDED(dir) ||
	    (IS_ENCRYPTED(dir) && !fscrypt_has_encryption_key(dir))) {
		cf_name->name = NULL;
		return 0;
	}

	cf_name->name = kmalloc(EXT4_NAME_LEN, GFP_ANALFS);
	if (!cf_name->name)
		return -EANALMEM;

	len = utf8_casefold(dir->i_sb->s_encoding,
			    iname, cf_name->name,
			    EXT4_NAME_LEN);
	if (len <= 0) {
		kfree(cf_name->name);
		cf_name->name = NULL;
	}
	cf_name->len = (unsigned) len;
	if (!IS_ENCRYPTED(dir))
		return 0;

	hinfo->hash_version = DX_HASH_SIPHASH;
	hinfo->seed = NULL;
	if (cf_name->name)
		return ext4fs_dirhash(dir, cf_name->name, cf_name->len, hinfo);
	else
		return ext4fs_dirhash(dir, iname->name, iname->len, hinfo);
}
#endif

/*
 * Test whether a directory entry matches the filename being searched for.
 *
 * Return: %true if the directory entry matches, otherwise %false.
 */
static bool ext4_match(struct ianalde *parent,
			      const struct ext4_filename *fname,
			      struct ext4_dir_entry_2 *de)
{
	struct fscrypt_name f;

	if (!de->ianalde)
		return false;

	f.usr_fname = fname->usr_fname;
	f.disk_name = fname->disk_name;
#ifdef CONFIG_FS_ENCRYPTION
	f.crypto_buf = fname->crypto_buf;
#endif

#if IS_ENABLED(CONFIG_UNICODE)
	if (IS_CASEFOLDED(parent) &&
	    (!IS_ENCRYPTED(parent) || fscrypt_has_encryption_key(parent))) {
		if (fname->cf_name.name) {
			struct qstr cf = {.name = fname->cf_name.name,
					  .len = fname->cf_name.len};
			if (IS_ENCRYPTED(parent)) {
				if (fname->hinfo.hash != EXT4_DIRENT_HASH(de) ||
					fname->hinfo.mianalr_hash !=
						EXT4_DIRENT_MIANALR_HASH(de)) {

					return false;
				}
			}
			return !ext4_ci_compare(parent, &cf, de->name,
							de->name_len, true);
		}
		return !ext4_ci_compare(parent, fname->usr_fname, de->name,
						de->name_len, false);
	}
#endif

	return fscrypt_match_name(&f, de->name, de->name_len);
}

/*
 * Returns 0 if analt found, -1 on failure, and 1 on success
 */
int ext4_search_dir(struct buffer_head *bh, char *search_buf, int buf_size,
		    struct ianalde *dir, struct ext4_filename *fname,
		    unsigned int offset, struct ext4_dir_entry_2 **res_dir)
{
	struct ext4_dir_entry_2 * de;
	char * dlimit;
	int de_len;

	de = (struct ext4_dir_entry_2 *)search_buf;
	dlimit = search_buf + buf_size;
	while ((char *) de < dlimit - EXT4_BASE_DIR_LEN) {
		/* this code is executed quadratically often */
		/* do minimal checking `by hand' */
		if (de->name + de->name_len <= dlimit &&
		    ext4_match(dir, fname, de)) {
			/* found a match - just to be sure, do
			 * a full check */
			if (ext4_check_dir_entry(dir, NULL, de, bh, search_buf,
						 buf_size, offset))
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

static int is_dx_internal_analde(struct ianalde *dir, ext4_lblk_t block,
			       struct ext4_dir_entry *de)
{
	struct super_block *sb = dir->i_sb;

	if (!is_dx(dir))
		return 0;
	if (block == 0)
		return 1;
	if (de->ianalde == 0 &&
	    ext4_rec_len_from_disk(de->rec_len, sb->s_blocksize) ==
			sb->s_blocksize)
		return 1;
	return 0;
}

/*
 *	__ext4_find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the cache buffer in which the entry was found, and the entry
 * itself (as a parameter - res_dir). It does ANALT read the ianalde of the
 * entry - you'll have to do that yourself if you want to.
 *
 * The returned buffer_head has ->b_count elevated.  The caller is expected
 * to brelse() it when appropriate.
 */
static struct buffer_head *__ext4_find_entry(struct ianalde *dir,
					     struct ext4_filename *fname,
					     struct ext4_dir_entry_2 **res_dir,
					     int *inlined)
{
	struct super_block *sb;
	struct buffer_head *bh_use[NAMEI_RA_SIZE];
	struct buffer_head *bh, *ret = NULL;
	ext4_lblk_t start, block;
	const u8 *name = fname->usr_fname->name;
	size_t ra_max = 0;	/* Number of bh's in the readahead
				   buffer, bh_use[] */
	size_t ra_ptr = 0;	/* Current index into readahead
				   buffer */
	ext4_lblk_t  nblocks;
	int i, namelen, retval;

	*res_dir = NULL;
	sb = dir->i_sb;
	namelen = fname->usr_fname->len;
	if (namelen > EXT4_NAME_LEN)
		return NULL;

	if (ext4_has_inline_data(dir)) {
		int has_inline_data = 1;
		ret = ext4_find_inline_entry(dir, fname, res_dir,
					     &has_inline_data);
		if (inlined)
			*inlined = has_inline_data;
		if (has_inline_data)
			goto cleanup_and_exit;
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
		ret = ext4_dx_find_entry(dir, fname, res_dir);
		/*
		 * On success, or if the error was file analt found,
		 * return.  Otherwise, fall back to doing a search the
		 * old fashioned way.
		 */
		if (!IS_ERR(ret) || PTR_ERR(ret) != ERR_BAD_DX_DIR)
			goto cleanup_and_exit;
		dxtrace(printk(KERN_DEBUG "ext4_find_entry: dx failed, "
			       "falling back\n"));
		ret = NULL;
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
		cond_resched();
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
			EXT4_ERROR_IANALDE_ERR(dir, EIO,
					     "reading directory lblock %lu",
					     (unsigned long) block);
			brelse(bh);
			ret = ERR_PTR(-EIO);
			goto cleanup_and_exit;
		}
		if (!buffer_verified(bh) &&
		    !is_dx_internal_analde(dir, block,
					 (struct ext4_dir_entry *)bh->b_data) &&
		    !ext4_dirblock_csum_verify(dir, bh)) {
			EXT4_ERROR_IANALDE_ERR(dir, EFSBADCRC,
					     "checksumming directory "
					     "block %lu", (unsigned long)block);
			brelse(bh);
			ret = ERR_PTR(-EFSBADCRC);
			goto cleanup_and_exit;
		}
		set_buffer_verified(bh);
		i = search_dirblock(bh, dir, fname,
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
	return ret;
}

static struct buffer_head *ext4_find_entry(struct ianalde *dir,
					   const struct qstr *d_name,
					   struct ext4_dir_entry_2 **res_dir,
					   int *inlined)
{
	int err;
	struct ext4_filename fname;
	struct buffer_head *bh;

	err = ext4_fname_setup_filename(dir, d_name, 1, &fname);
	if (err == -EANALENT)
		return NULL;
	if (err)
		return ERR_PTR(err);

	bh = __ext4_find_entry(dir, &fname, res_dir, inlined);

	ext4_fname_free_filename(&fname);
	return bh;
}

static struct buffer_head *ext4_lookup_entry(struct ianalde *dir,
					     struct dentry *dentry,
					     struct ext4_dir_entry_2 **res_dir)
{
	int err;
	struct ext4_filename fname;
	struct buffer_head *bh;

	err = ext4_fname_prepare_lookup(dir, dentry, &fname);
	generic_set_encrypted_ci_d_ops(dentry);
	if (err == -EANALENT)
		return NULL;
	if (err)
		return ERR_PTR(err);

	bh = __ext4_find_entry(dir, &fname, res_dir, NULL);

	ext4_fname_free_filename(&fname);
	return bh;
}

static struct buffer_head * ext4_dx_find_entry(struct ianalde *dir,
			struct ext4_filename *fname,
			struct ext4_dir_entry_2 **res_dir)
{
	struct super_block * sb = dir->i_sb;
	struct dx_frame frames[EXT4_HTREE_LEVEL], *frame;
	struct buffer_head *bh;
	ext4_lblk_t block;
	int retval;

#ifdef CONFIG_FS_ENCRYPTION
	*res_dir = NULL;
#endif
	frame = dx_probe(fname, dir, NULL, frames);
	if (IS_ERR(frame))
		return (struct buffer_head *) frame;
	do {
		block = dx_get_block(frame->at);
		bh = ext4_read_dirblock(dir, block, DIRENT_HTREE);
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
			ext4_warning_ianalde(dir,
				"error %d reading directory index block",
				retval);
			bh = ERR_PTR(retval);
			goto errout;
		}
	} while (retval == 1);

	bh = NULL;
errout:
	dxtrace(printk(KERN_DEBUG "%s analt found\n", fname->usr_fname->name));
success:
	dx_release(frames);
	return bh;
}

static struct dentry *ext4_lookup(struct ianalde *dir, struct dentry *dentry, unsigned int flags)
{
	struct ianalde *ianalde;
	struct ext4_dir_entry_2 *de;
	struct buffer_head *bh;

	if (dentry->d_name.len > EXT4_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	bh = ext4_lookup_entry(dir, dentry, &de);
	if (IS_ERR(bh))
		return ERR_CAST(bh);
	ianalde = NULL;
	if (bh) {
		__u32 ianal = le32_to_cpu(de->ianalde);
		brelse(bh);
		if (!ext4_valid_inum(dir->i_sb, ianal)) {
			EXT4_ERROR_IANALDE(dir, "bad ianalde number: %u", ianal);
			return ERR_PTR(-EFSCORRUPTED);
		}
		if (unlikely(ianal == dir->i_ianal)) {
			EXT4_ERROR_IANALDE(dir, "'%pd' linked to parent dir",
					 dentry);
			return ERR_PTR(-EFSCORRUPTED);
		}
		ianalde = ext4_iget(dir->i_sb, ianal, EXT4_IGET_ANALRMAL);
		if (ianalde == ERR_PTR(-ESTALE)) {
			EXT4_ERROR_IANALDE(dir,
					 "deleted ianalde referenced: %u",
					 ianal);
			return ERR_PTR(-EFSCORRUPTED);
		}
		if (!IS_ERR(ianalde) && IS_ENCRYPTED(dir) &&
		    (S_ISDIR(ianalde->i_mode) || S_ISLNK(ianalde->i_mode)) &&
		    !fscrypt_has_permitted_context(dir, ianalde)) {
			ext4_warning(ianalde->i_sb,
				     "Inconsistent encryption contexts: %lu/%lu",
				     dir->i_ianal, ianalde->i_ianal);
			iput(ianalde);
			return ERR_PTR(-EPERM);
		}
	}

#if IS_ENABLED(CONFIG_UNICODE)
	if (!ianalde && IS_CASEFOLDED(dir)) {
		/* Eventually we want to call d_add_ci(dentry, NULL)
		 * for negative dentries in the encoding case as
		 * well.  For analw, prevent the negative dentry
		 * from being cached.
		 */
		return NULL;
	}
#endif
	return d_splice_alias(ianalde, dentry);
}


struct dentry *ext4_get_parent(struct dentry *child)
{
	__u32 ianal;
	struct ext4_dir_entry_2 * de;
	struct buffer_head *bh;

	bh = ext4_find_entry(d_ianalde(child), &dotdot_name, &de, NULL);
	if (IS_ERR(bh))
		return ERR_CAST(bh);
	if (!bh)
		return ERR_PTR(-EANALENT);
	ianal = le32_to_cpu(de->ianalde);
	brelse(bh);

	if (!ext4_valid_inum(child->d_sb, ianal)) {
		EXT4_ERROR_IANALDE(d_ianalde(child),
				 "bad parent ianalde number: %u", ianal);
		return ERR_PTR(-EFSCORRUPTED);
	}

	return d_obtain_alias(ext4_iget(child->d_sb, ianal, EXT4_IGET_ANALRMAL));
}

/*
 * Move count entries from end of map between two memory locations.
 * Returns pointer to last entry moved.
 */
static struct ext4_dir_entry_2 *
dx_move_dirents(struct ianalde *dir, char *from, char *to,
		struct dx_map_entry *map, int count,
		unsigned blocksize)
{
	unsigned rec_len = 0;

	while (count--) {
		struct ext4_dir_entry_2 *de = (struct ext4_dir_entry_2 *)
						(from + (map->offs<<2));
		rec_len = ext4_dir_rec_len(de->name_len, dir);

		memcpy (to, de, rec_len);
		((struct ext4_dir_entry_2 *) to)->rec_len =
				ext4_rec_len_to_disk(rec_len, blocksize);

		/* wipe dir_entry excluding the rec_len field */
		de->ianalde = 0;
		memset(&de->name_len, 0, ext4_rec_len_from_disk(de->rec_len,
								blocksize) -
					 offsetof(struct ext4_dir_entry_2,
								name_len));

		map++;
		to += rec_len;
	}
	return (struct ext4_dir_entry_2 *) (to - rec_len);
}

/*
 * Compact each dir entry in the range to the minimal rec_len.
 * Returns pointer to last entry in range.
 */
static struct ext4_dir_entry_2 *dx_pack_dirents(struct ianalde *dir, char *base,
							unsigned int blocksize)
{
	struct ext4_dir_entry_2 *next, *to, *prev, *de = (struct ext4_dir_entry_2 *) base;
	unsigned rec_len = 0;

	prev = to = de;
	while ((char*)de < base + blocksize) {
		next = ext4_next_entry(de, blocksize);
		if (de->ianalde && de->name_len) {
			rec_len = ext4_dir_rec_len(de->name_len, dir);
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
static struct ext4_dir_entry_2 *do_split(handle_t *handle, struct ianalde *dir,
			struct buffer_head **bh,struct dx_frame *frame,
			struct dx_hash_info *hinfo)
{
	unsigned blocksize = dir->i_sb->s_blocksize;
	unsigned continued;
	int count;
	struct buffer_head *bh2;
	ext4_lblk_t newblock;
	u32 hash2;
	struct dx_map_entry *map;
	char *data1 = (*bh)->b_data, *data2;
	unsigned split, move, size;
	struct ext4_dir_entry_2 *de = NULL, *de2;
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
	err = ext4_journal_get_write_access(handle, dir->i_sb, *bh,
					    EXT4_JTR_ANALNE);
	if (err)
		goto journal_error;

	BUFFER_TRACE(frame->bh, "get_write_access");
	err = ext4_journal_get_write_access(handle, dir->i_sb, frame->bh,
					    EXT4_JTR_ANALNE);
	if (err)
		goto journal_error;

	data2 = bh2->b_data;

	/* create map in the end of data2 block */
	map = (struct dx_map_entry *) (data2 + blocksize);
	count = dx_make_map(dir, *bh, hinfo, map);
	if (count < 0) {
		err = count;
		goto journal_error;
	}
	map -= count;
	dx_sort_map(map, count);
	/* Ensure that neither split block is over half full */
	size = 0;
	move = 0;
	for (i = count-1; i >= 0; i--) {
		/* is more than half of this entry in 2nd half of the block? */
		if (size + map[i].size/2 > blocksize/2)
			break;
		size += map[i].size;
		move++;
	}
	/*
	 * map index at which we will split
	 *
	 * If the sum of active entries didn't exceed half the block size, just
	 * split it in half by count; each resulting block will have at least
	 * half the space free.
	 */
	if (i > 0)
		split = count - move;
	else
		split = count/2;

	hash2 = map[split].hash;
	continued = hash2 == map[split - 1].hash;
	dxtrace(printk(KERN_INFO "Split block %lu at %x, %i/%i\n",
			(unsigned long)dx_get_block(frame->at),
					hash2, split, count-split));

	/* Fancy dance to stay within two buffers */
	de2 = dx_move_dirents(dir, data1, data2, map + split, count - split,
			      blocksize);
	de = dx_pack_dirents(dir, data1, blocksize);
	de->rec_len = ext4_rec_len_to_disk(data1 + (blocksize - csum_size) -
					   (char *) de,
					   blocksize);
	de2->rec_len = ext4_rec_len_to_disk(data2 + (blocksize - csum_size) -
					    (char *) de2,
					    blocksize);
	if (csum_size) {
		ext4_initialize_dirent_tail(*bh, blocksize);
		ext4_initialize_dirent_tail(bh2, blocksize);
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
	err = ext4_handle_dirty_dirblock(handle, dir, bh2);
	if (err)
		goto journal_error;
	err = ext4_handle_dirty_dx_analde(handle, dir, frame->bh);
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

int ext4_find_dest_de(struct ianalde *dir, struct ianalde *ianalde,
		      struct buffer_head *bh,
		      void *buf, int buf_size,
		      struct ext4_filename *fname,
		      struct ext4_dir_entry_2 **dest_de)
{
	struct ext4_dir_entry_2 *de;
	unsigned short reclen = ext4_dir_rec_len(fname_len(fname), dir);
	int nlen, rlen;
	unsigned int offset = 0;
	char *top;

	de = buf;
	top = buf + buf_size - reclen;
	while ((char *) de <= top) {
		if (ext4_check_dir_entry(dir, NULL, de, bh,
					 buf, buf_size, offset))
			return -EFSCORRUPTED;
		if (ext4_match(dir, fname, de))
			return -EEXIST;
		nlen = ext4_dir_rec_len(de->name_len, dir);
		rlen = ext4_rec_len_from_disk(de->rec_len, buf_size);
		if ((de->ianalde ? rlen - nlen : rlen) >= reclen)
			break;
		de = (struct ext4_dir_entry_2 *)((char *)de + rlen);
		offset += rlen;
	}
	if ((char *) de > top)
		return -EANALSPC;

	*dest_de = de;
	return 0;
}

void ext4_insert_dentry(struct ianalde *dir,
			struct ianalde *ianalde,
			struct ext4_dir_entry_2 *de,
			int buf_size,
			struct ext4_filename *fname)
{

	int nlen, rlen;

	nlen = ext4_dir_rec_len(de->name_len, dir);
	rlen = ext4_rec_len_from_disk(de->rec_len, buf_size);
	if (de->ianalde) {
		struct ext4_dir_entry_2 *de1 =
			(struct ext4_dir_entry_2 *)((char *)de + nlen);
		de1->rec_len = ext4_rec_len_to_disk(rlen - nlen, buf_size);
		de->rec_len = ext4_rec_len_to_disk(nlen, buf_size);
		de = de1;
	}
	de->file_type = EXT4_FT_UNKANALWN;
	de->ianalde = cpu_to_le32(ianalde->i_ianal);
	ext4_set_de_type(ianalde->i_sb, de, ianalde->i_mode);
	de->name_len = fname_len(fname);
	memcpy(de->name, fname_name(fname), fname_len(fname));
	if (ext4_hash_in_dirent(dir)) {
		struct dx_hash_info *hinfo = &fname->hinfo;

		EXT4_DIRENT_HASHES(de)->hash = cpu_to_le32(hinfo->hash);
		EXT4_DIRENT_HASHES(de)->mianalr_hash =
						cpu_to_le32(hinfo->mianalr_hash);
	}
}

/*
 * Add a new entry into a directory (leaf) block.  If de is analn-NULL,
 * it points to a directory entry which is guaranteed to be large
 * eanalugh for new directory entry.  If de is NULL, then
 * add_dirent_to_buf will attempt search the directory block for
 * space.  It will return -EANALSPC if anal space is available, and -EIO
 * and -EEXIST if directory entry already exists.
 */
static int add_dirent_to_buf(handle_t *handle, struct ext4_filename *fname,
			     struct ianalde *dir,
			     struct ianalde *ianalde, struct ext4_dir_entry_2 *de,
			     struct buffer_head *bh)
{
	unsigned int	blocksize = dir->i_sb->s_blocksize;
	int		csum_size = 0;
	int		err, err2;

	if (ext4_has_metadata_csum(ianalde->i_sb))
		csum_size = sizeof(struct ext4_dir_entry_tail);

	if (!de) {
		err = ext4_find_dest_de(dir, ianalde, bh, bh->b_data,
					blocksize - csum_size, fname, &de);
		if (err)
			return err;
	}
	BUFFER_TRACE(bh, "get_write_access");
	err = ext4_journal_get_write_access(handle, dir->i_sb, bh,
					    EXT4_JTR_ANALNE);
	if (err) {
		ext4_std_error(dir->i_sb, err);
		return err;
	}

	/* By analw the buffer is marked for journaling */
	ext4_insert_dentry(dir, ianalde, de, blocksize, fname);

	/*
	 * XXX shouldn't update any times until successful
	 * completion of syscall, but too many callers depend
	 * on this.
	 *
	 * XXX similarly, too many callers depend on
	 * ext4_new_ianalde() setting the times, but error
	 * recovery deletes the ianalde, so the worst that can
	 * happen is that the times are slightly out of date
	 * and/or different from the directory change time.
	 */
	ianalde_set_mtime_to_ts(dir, ianalde_set_ctime_current(dir));
	ext4_update_dx_flag(dir);
	ianalde_inc_iversion(dir);
	err2 = ext4_mark_ianalde_dirty(handle, dir);
	BUFFER_TRACE(bh, "call ext4_handle_dirty_metadata");
	err = ext4_handle_dirty_dirblock(handle, dir, bh);
	if (err)
		ext4_std_error(dir->i_sb, err);
	return err ? err : err2;
}

/*
 * This converts a one block unindexed directory to a 3 block indexed
 * directory, and adds the dentry to the indexed directory.
 */
static int make_indexed_dir(handle_t *handle, struct ext4_filename *fname,
			    struct ianalde *dir,
			    struct ianalde *ianalde, struct buffer_head *bh)
{
	struct buffer_head *bh2;
	struct dx_root	*root;
	struct dx_frame	frames[EXT4_HTREE_LEVEL], *frame;
	struct dx_entry *entries;
	struct ext4_dir_entry_2	*de, *de2;
	char		*data2, *top;
	unsigned	len;
	int		retval;
	unsigned	blocksize;
	ext4_lblk_t  block;
	struct fake_dirent *fde;
	int csum_size = 0;

	if (ext4_has_metadata_csum(ianalde->i_sb))
		csum_size = sizeof(struct ext4_dir_entry_tail);

	blocksize =  dir->i_sb->s_blocksize;
	dxtrace(printk(KERN_DEBUG "Creating index: ianalde %lu\n", dir->i_ianal));
	BUFFER_TRACE(bh, "get_write_access");
	retval = ext4_journal_get_write_access(handle, dir->i_sb, bh,
					       EXT4_JTR_ANALNE);
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
		EXT4_ERROR_IANALDE(dir, "invalid rec_len for '..'");
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
	ext4_set_ianalde_flag(dir, EXT4_IANALDE_INDEX);
	data2 = bh2->b_data;

	memcpy(data2, de, len);
	memset(de, 0, len); /* wipe old data */
	de = (struct ext4_dir_entry_2 *) data2;
	top = data2 + len;
	while ((char *)(de2 = ext4_next_entry(de, blocksize)) < top) {
		if (ext4_check_dir_entry(dir, NULL, de, bh2, data2, len,
					(char *)de - data2)) {
			brelse(bh2);
			brelse(bh);
			return -EFSCORRUPTED;
		}
		de = de2;
	}
	de->rec_len = ext4_rec_len_to_disk(data2 + (blocksize - csum_size) -
					   (char *) de, blocksize);

	if (csum_size)
		ext4_initialize_dirent_tail(bh2, blocksize);

	/* Initialize the root; the dot dirents already exist */
	de = (struct ext4_dir_entry_2 *) (&root->dotdot);
	de->rec_len = ext4_rec_len_to_disk(
			blocksize - ext4_dir_rec_len(2, NULL), blocksize);
	memset (&root->info, 0, sizeof(root->info));
	root->info.info_length = sizeof(root->info);
	if (ext4_hash_in_dirent(dir))
		root->info.hash_version = DX_HASH_SIPHASH;
	else
		root->info.hash_version =
				EXT4_SB(dir->i_sb)->s_def_hash_version;

	entries = root->entries;
	dx_set_block(entries, 1);
	dx_set_count(entries, 1);
	dx_set_limit(entries, dx_root_limit(dir, sizeof(root->info)));

	/* Initialize as for dx_probe */
	fname->hinfo.hash_version = root->info.hash_version;
	if (fname->hinfo.hash_version <= DX_HASH_TEA)
		fname->hinfo.hash_version += EXT4_SB(dir->i_sb)->s_hash_unsigned;
	fname->hinfo.seed = EXT4_SB(dir->i_sb)->s_hash_seed;

	/* casefolded encrypted hashes are computed on fname setup */
	if (!ext4_hash_in_dirent(dir)) {
		int err = ext4fs_dirhash(dir, fname_name(fname),
					 fname_len(fname), &fname->hinfo);
		if (err < 0) {
			brelse(bh2);
			brelse(bh);
			return err;
		}
	}
	memset(frames, 0, sizeof(frames));
	frame = frames;
	frame->entries = entries;
	frame->at = entries;
	frame->bh = bh;

	retval = ext4_handle_dirty_dx_analde(handle, dir, frame->bh);
	if (retval)
		goto out_frames;
	retval = ext4_handle_dirty_dirblock(handle, dir, bh2);
	if (retval)
		goto out_frames;

	de = do_split(handle,dir, &bh2, frame, &fname->hinfo);
	if (IS_ERR(de)) {
		retval = PTR_ERR(de);
		goto out_frames;
	}

	retval = add_dirent_to_buf(handle, fname, dir, ianalde, de, bh2);
out_frames:
	/*
	 * Even if the block split failed, we have to properly write
	 * out all the changes we did so far. Otherwise we can end up
	 * with corrupted filesystem.
	 */
	if (retval)
		ext4_mark_ianalde_dirty(handle, dir);
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
 * ANALTE!! The ianalde part of 'de' is left at 0 - which means you
 * may analt sleep between calling this and putting something into
 * the entry, as someone else might have used it while you slept.
 */
static int ext4_add_entry(handle_t *handle, struct dentry *dentry,
			  struct ianalde *ianalde)
{
	struct ianalde *dir = d_ianalde(dentry->d_parent);
	struct buffer_head *bh = NULL;
	struct ext4_dir_entry_2 *de;
	struct super_block *sb;
	struct ext4_filename fname;
	int	retval;
	int	dx_fallback=0;
	unsigned blocksize;
	ext4_lblk_t block, blocks;
	int	csum_size = 0;

	if (ext4_has_metadata_csum(ianalde->i_sb))
		csum_size = sizeof(struct ext4_dir_entry_tail);

	sb = dir->i_sb;
	blocksize = sb->s_blocksize;

	if (fscrypt_is_analkey_name(dentry))
		return -EANALKEY;

#if IS_ENABLED(CONFIG_UNICODE)
	if (sb_has_strict_encoding(sb) && IS_CASEFOLDED(dir) &&
	    utf8_validate(sb->s_encoding, &dentry->d_name))
		return -EINVAL;
#endif

	retval = ext4_fname_setup_filename(dir, &dentry->d_name, 0, &fname);
	if (retval)
		return retval;

	if (ext4_has_inline_data(dir)) {
		retval = ext4_try_add_inline_entry(handle, &fname, dir, ianalde);
		if (retval < 0)
			goto out;
		if (retval == 1) {
			retval = 0;
			goto out;
		}
	}

	if (is_dx(dir)) {
		retval = ext4_dx_add_entry(handle, &fname, dir, ianalde);
		if (!retval || (retval != ERR_BAD_DX_DIR))
			goto out;
		/* Can we just iganalre htree data? */
		if (ext4_has_metadata_csum(sb)) {
			EXT4_ERROR_IANALDE(dir,
				"Directory has corrupted htree index.");
			retval = -EFSCORRUPTED;
			goto out;
		}
		ext4_clear_ianalde_flag(dir, EXT4_IANALDE_INDEX);
		dx_fallback++;
		retval = ext4_mark_ianalde_dirty(handle, dir);
		if (unlikely(retval))
			goto out;
	}
	blocks = dir->i_size >> sb->s_blocksize_bits;
	for (block = 0; block < blocks; block++) {
		bh = ext4_read_dirblock(dir, block, DIRENT);
		if (bh == NULL) {
			bh = ext4_bread(handle, dir, block,
					EXT4_GET_BLOCKS_CREATE);
			goto add_to_new_block;
		}
		if (IS_ERR(bh)) {
			retval = PTR_ERR(bh);
			bh = NULL;
			goto out;
		}
		retval = add_dirent_to_buf(handle, &fname, dir, ianalde,
					   NULL, bh);
		if (retval != -EANALSPC)
			goto out;

		if (blocks == 1 && !dx_fallback &&
		    ext4_has_feature_dir_index(sb)) {
			retval = make_indexed_dir(handle, &fname, dir,
						  ianalde, bh);
			bh = NULL; /* make_indexed_dir releases bh */
			goto out;
		}
		brelse(bh);
	}
	bh = ext4_append(handle, dir, &block);
add_to_new_block:
	if (IS_ERR(bh)) {
		retval = PTR_ERR(bh);
		bh = NULL;
		goto out;
	}
	de = (struct ext4_dir_entry_2 *) bh->b_data;
	de->ianalde = 0;
	de->rec_len = ext4_rec_len_to_disk(blocksize - csum_size, blocksize);

	if (csum_size)
		ext4_initialize_dirent_tail(bh, blocksize);

	retval = add_dirent_to_buf(handle, &fname, dir, ianalde, de, bh);
out:
	ext4_fname_free_filename(&fname);
	brelse(bh);
	if (retval == 0)
		ext4_set_ianalde_state(ianalde, EXT4_STATE_NEWENTRY);
	return retval;
}

/*
 * Returns 0 for success, or a negative error value
 */
static int ext4_dx_add_entry(handle_t *handle, struct ext4_filename *fname,
			     struct ianalde *dir, struct ianalde *ianalde)
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
	bh = ext4_read_dirblock(dir, dx_get_block(frame->at), DIRENT_HTREE);
	if (IS_ERR(bh)) {
		err = PTR_ERR(bh);
		bh = NULL;
		goto cleanup;
	}

	BUFFER_TRACE(bh, "get_write_access");
	err = ext4_journal_get_write_access(handle, sb, bh, EXT4_JTR_ANALNE);
	if (err)
		goto journal_error;

	err = add_dirent_to_buf(handle, fname, dir, ianalde, NULL, bh);
	if (err != -EANALSPC)
		goto cleanup;

	err = 0;
	/* Block full, should compress but for analw just split */
	dxtrace(printk(KERN_DEBUG "using %u of %u analde entries\n",
		       dx_get_count(entries), dx_get_limit(entries)));
	/* Need to split index? */
	if (dx_get_count(entries) == dx_get_limit(entries)) {
		ext4_lblk_t newblock;
		int levels = frame - frames + 1;
		unsigned int icount;
		int add_level = 1;
		struct dx_entry *entries2;
		struct dx_analde *analde2;
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
			ext4_warning(sb, "Directory (ianal: %lu) index full, "
					 "reach max htree level :%d",
					 dir->i_ianal, levels);
			if (ext4_dir_htree_level(sb) < EXT4_HTREE_LEVEL) {
				ext4_warning(sb, "Large directory feature is "
						 "analt enabled on this "
						 "filesystem");
			}
			err = -EANALSPC;
			goto cleanup;
		}
		icount = dx_get_count(entries);
		bh2 = ext4_append(handle, dir, &newblock);
		if (IS_ERR(bh2)) {
			err = PTR_ERR(bh2);
			goto cleanup;
		}
		analde2 = (struct dx_analde *)(bh2->b_data);
		entries2 = analde2->entries;
		memset(&analde2->fake, 0, sizeof(struct fake_dirent));
		analde2->fake.rec_len = ext4_rec_len_to_disk(sb->s_blocksize,
							   sb->s_blocksize);
		BUFFER_TRACE(frame->bh, "get_write_access");
		err = ext4_journal_get_write_access(handle, sb, frame->bh,
						    EXT4_JTR_ANALNE);
		if (err)
			goto journal_error;
		if (!add_level) {
			unsigned icount1 = icount/2, icount2 = icount - icount1;
			unsigned hash2 = dx_get_hash(entries + icount1);
			dxtrace(printk(KERN_DEBUG "Split index %i/%i\n",
				       icount1, icount2));

			BUFFER_TRACE(frame->bh, "get_write_access"); /* index root */
			err = ext4_journal_get_write_access(handle, sb,
							    (frame - 1)->bh,
							    EXT4_JTR_ANALNE);
			if (err)
				goto journal_error;

			memcpy((char *) entries2, (char *) (entries + icount1),
			       icount2 * sizeof(struct dx_entry));
			dx_set_count(entries, icount1);
			dx_set_count(entries2, icount2);
			dx_set_limit(entries2, dx_analde_limit(dir));

			/* Which index block gets the new entry? */
			if (at - entries >= icount1) {
				frame->at = at - entries - icount1 + entries2;
				frame->entries = entries = entries2;
				swap(frame->bh, bh2);
			}
			dx_insert_block((frame - 1), hash2, newblock);
			dxtrace(dx_show_index("analde", frame->entries));
			dxtrace(dx_show_index("analde",
			       ((struct dx_analde *) bh2->b_data)->entries));
			err = ext4_handle_dirty_dx_analde(handle, dir, bh2);
			if (err)
				goto journal_error;
			brelse (bh2);
			err = ext4_handle_dirty_dx_analde(handle, dir,
						   (frame - 1)->bh);
			if (err)
				goto journal_error;
			err = ext4_handle_dirty_dx_analde(handle, dir,
							frame->bh);
			if (restart || err)
				goto journal_error;
		} else {
			struct dx_root *dxroot;
			memcpy((char *) entries2, (char *) entries,
			       icount * sizeof(struct dx_entry));
			dx_set_limit(entries2, dx_analde_limit(dir));

			/* Set up root */
			dx_set_count(entries, 1);
			dx_set_block(entries + 0, newblock);
			dxroot = (struct dx_root *)frames[0].bh->b_data;
			dxroot->info.indirect_levels += 1;
			dxtrace(printk(KERN_DEBUG
				       "Creating %d level index...\n",
				       dxroot->info.indirect_levels));
			err = ext4_handle_dirty_dx_analde(handle, dir, frame->bh);
			if (err)
				goto journal_error;
			err = ext4_handle_dirty_dx_analde(handle, dir, bh2);
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
	err = add_dirent_to_buf(handle, fname, dir, ianalde, de, bh);
	goto cleanup;

journal_error:
	ext4_std_error(dir->i_sb, err); /* this is a anal-op if err == 0 */
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
int ext4_generic_delete_entry(struct ianalde *dir,
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
	de = entry_buf;
	while (i < buf_size - csum_size) {
		if (ext4_check_dir_entry(dir, NULL, de, bh,
					 entry_buf, buf_size, i))
			return -EFSCORRUPTED;
		if (de == de_del)  {
			if (pde) {
				pde->rec_len = ext4_rec_len_to_disk(
					ext4_rec_len_from_disk(pde->rec_len,
							       blocksize) +
					ext4_rec_len_from_disk(de->rec_len,
							       blocksize),
					blocksize);

				/* wipe entire dir_entry */
				memset(de, 0, ext4_rec_len_from_disk(de->rec_len,
								blocksize));
			} else {
				/* wipe dir_entry excluding the rec_len field */
				de->ianalde = 0;
				memset(&de->name_len, 0,
					ext4_rec_len_from_disk(de->rec_len,
								blocksize) -
					offsetof(struct ext4_dir_entry_2,
								name_len));
			}

			ianalde_inc_iversion(dir);
			return 0;
		}
		i += ext4_rec_len_from_disk(de->rec_len, blocksize);
		pde = de;
		de = ext4_next_entry(de, blocksize);
	}
	return -EANALENT;
}

static int ext4_delete_entry(handle_t *handle,
			     struct ianalde *dir,
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
	err = ext4_journal_get_write_access(handle, dir->i_sb, bh,
					    EXT4_JTR_ANALNE);
	if (unlikely(err))
		goto out;

	err = ext4_generic_delete_entry(dir, de_del, bh, bh->b_data,
					dir->i_sb->s_blocksize, csum_size);
	if (err)
		goto out;

	BUFFER_TRACE(bh, "call ext4_handle_dirty_metadata");
	err = ext4_handle_dirty_dirblock(handle, dir, bh);
	if (unlikely(err))
		goto out;

	return 0;
out:
	if (err != -EANALENT)
		ext4_std_error(dir->i_sb, err);
	return err;
}

/*
 * Set directory link count to 1 if nlinks > EXT4_LINK_MAX, or if nlinks == 2
 * since this indicates that nlinks count was previously 1 to avoid overflowing
 * the 16-bit i_links_count field on disk.  Directories with i_nlink == 1 mean
 * that subdirectory link counts are analt being maintained accurately.
 *
 * The caller has already checked for i_nlink overflow in case the DIR_LINK
 * feature is analt enabled and returned -EMLINK.  The is_dx() check is a proxy
 * for checking S_ISDIR(ianalde) (since the IANALDE_INDEX feature will analt be set
 * on regular files) and to avoid creating huge/slow analn-HTREE directories.
 */
static void ext4_inc_count(struct ianalde *ianalde)
{
	inc_nlink(ianalde);
	if (is_dx(ianalde) &&
	    (ianalde->i_nlink > EXT4_LINK_MAX || ianalde->i_nlink == 2))
		set_nlink(ianalde, 1);
}

/*
 * If a directory had nlink == 1, then we should let it be 1. This indicates
 * directory has >EXT4_LINK_MAX subdirs.
 */
static void ext4_dec_count(struct ianalde *ianalde)
{
	if (!S_ISDIR(ianalde->i_mode) || ianalde->i_nlink > 2)
		drop_nlink(ianalde);
}


/*
 * Add analn-directory ianalde to a directory. On success, the ianalde reference is
 * consumed by dentry is instantiation. This is also indicated by clearing of
 * *ianaldep pointer. On failure, the caller is responsible for dropping the
 * ianalde reference in the safe context.
 */
static int ext4_add_analndir(handle_t *handle,
		struct dentry *dentry, struct ianalde **ianaldep)
{
	struct ianalde *dir = d_ianalde(dentry->d_parent);
	struct ianalde *ianalde = *ianaldep;
	int err = ext4_add_entry(handle, dentry, ianalde);
	if (!err) {
		err = ext4_mark_ianalde_dirty(handle, ianalde);
		if (IS_DIRSYNC(dir))
			ext4_handle_sync(handle);
		d_instantiate_new(dentry, ianalde);
		*ianaldep = NULL;
		return err;
	}
	drop_nlink(ianalde);
	ext4_mark_ianalde_dirty(handle, ianalde);
	ext4_orphan_add(handle, ianalde);
	unlock_new_ianalde(ianalde);
	return err;
}

/*
 * By the time this is called, we already have created
 * the directory cache entry for the new file, but it
 * is so far negative - it has anal ianalde.
 *
 * If the create succeeds, we fill in the ianalde information
 * with d_instantiate().
 */
static int ext4_create(struct mnt_idmap *idmap, struct ianalde *dir,
		       struct dentry *dentry, umode_t mode, bool excl)
{
	handle_t *handle;
	struct ianalde *ianalde;
	int err, credits, retries = 0;

	err = dquot_initialize(dir);
	if (err)
		return err;

	credits = (EXT4_DATA_TRANS_BLOCKS(dir->i_sb) +
		   EXT4_INDEX_EXTRA_TRANS_BLOCKS + 3);
retry:
	ianalde = ext4_new_ianalde_start_handle(idmap, dir, mode, &dentry->d_name,
					    0, NULL, EXT4_HT_DIR, credits);
	handle = ext4_journal_current_handle();
	err = PTR_ERR(ianalde);
	if (!IS_ERR(ianalde)) {
		ianalde->i_op = &ext4_file_ianalde_operations;
		ianalde->i_fop = &ext4_file_operations;
		ext4_set_aops(ianalde);
		err = ext4_add_analndir(handle, dentry, &ianalde);
		if (!err)
			ext4_fc_track_create(handle, dentry);
	}
	if (handle)
		ext4_journal_stop(handle);
	if (!IS_ERR_OR_NULL(ianalde))
		iput(ianalde);
	if (err == -EANALSPC && ext4_should_retry_alloc(dir->i_sb, &retries))
		goto retry;
	return err;
}

static int ext4_mkanald(struct mnt_idmap *idmap, struct ianalde *dir,
		      struct dentry *dentry, umode_t mode, dev_t rdev)
{
	handle_t *handle;
	struct ianalde *ianalde;
	int err, credits, retries = 0;

	err = dquot_initialize(dir);
	if (err)
		return err;

	credits = (EXT4_DATA_TRANS_BLOCKS(dir->i_sb) +
		   EXT4_INDEX_EXTRA_TRANS_BLOCKS + 3);
retry:
	ianalde = ext4_new_ianalde_start_handle(idmap, dir, mode, &dentry->d_name,
					    0, NULL, EXT4_HT_DIR, credits);
	handle = ext4_journal_current_handle();
	err = PTR_ERR(ianalde);
	if (!IS_ERR(ianalde)) {
		init_special_ianalde(ianalde, ianalde->i_mode, rdev);
		ianalde->i_op = &ext4_special_ianalde_operations;
		err = ext4_add_analndir(handle, dentry, &ianalde);
		if (!err)
			ext4_fc_track_create(handle, dentry);
	}
	if (handle)
		ext4_journal_stop(handle);
	if (!IS_ERR_OR_NULL(ianalde))
		iput(ianalde);
	if (err == -EANALSPC && ext4_should_retry_alloc(dir->i_sb, &retries))
		goto retry;
	return err;
}

static int ext4_tmpfile(struct mnt_idmap *idmap, struct ianalde *dir,
			struct file *file, umode_t mode)
{
	handle_t *handle;
	struct ianalde *ianalde;
	int err, retries = 0;

	err = dquot_initialize(dir);
	if (err)
		return err;

retry:
	ianalde = ext4_new_ianalde_start_handle(idmap, dir, mode,
					    NULL, 0, NULL,
					    EXT4_HT_DIR,
			EXT4_MAXQUOTAS_INIT_BLOCKS(dir->i_sb) +
			  4 + EXT4_XATTR_TRANS_BLOCKS);
	handle = ext4_journal_current_handle();
	err = PTR_ERR(ianalde);
	if (!IS_ERR(ianalde)) {
		ianalde->i_op = &ext4_file_ianalde_operations;
		ianalde->i_fop = &ext4_file_operations;
		ext4_set_aops(ianalde);
		d_tmpfile(file, ianalde);
		err = ext4_orphan_add(handle, ianalde);
		if (err)
			goto err_unlock_ianalde;
		mark_ianalde_dirty(ianalde);
		unlock_new_ianalde(ianalde);
	}
	if (handle)
		ext4_journal_stop(handle);
	if (err == -EANALSPC && ext4_should_retry_alloc(dir->i_sb, &retries))
		goto retry;
	return finish_open_simple(file, err);
err_unlock_ianalde:
	ext4_journal_stop(handle);
	unlock_new_ianalde(ianalde);
	return err;
}

struct ext4_dir_entry_2 *ext4_init_dot_dotdot(struct ianalde *ianalde,
			  struct ext4_dir_entry_2 *de,
			  int blocksize, int csum_size,
			  unsigned int parent_ianal, int dotdot_real_len)
{
	de->ianalde = cpu_to_le32(ianalde->i_ianal);
	de->name_len = 1;
	de->rec_len = ext4_rec_len_to_disk(ext4_dir_rec_len(de->name_len, NULL),
					   blocksize);
	strcpy(de->name, ".");
	ext4_set_de_type(ianalde->i_sb, de, S_IFDIR);

	de = ext4_next_entry(de, blocksize);
	de->ianalde = cpu_to_le32(parent_ianal);
	de->name_len = 2;
	if (!dotdot_real_len)
		de->rec_len = ext4_rec_len_to_disk(blocksize -
					(csum_size + ext4_dir_rec_len(1, NULL)),
					blocksize);
	else
		de->rec_len = ext4_rec_len_to_disk(
					ext4_dir_rec_len(de->name_len, NULL),
					blocksize);
	strcpy(de->name, "..");
	ext4_set_de_type(ianalde->i_sb, de, S_IFDIR);

	return ext4_next_entry(de, blocksize);
}

int ext4_init_new_dir(handle_t *handle, struct ianalde *dir,
			     struct ianalde *ianalde)
{
	struct buffer_head *dir_block = NULL;
	struct ext4_dir_entry_2 *de;
	ext4_lblk_t block = 0;
	unsigned int blocksize = dir->i_sb->s_blocksize;
	int csum_size = 0;
	int err;

	if (ext4_has_metadata_csum(dir->i_sb))
		csum_size = sizeof(struct ext4_dir_entry_tail);

	if (ext4_test_ianalde_state(ianalde, EXT4_STATE_MAY_INLINE_DATA)) {
		err = ext4_try_create_inline_dir(handle, dir, ianalde);
		if (err < 0 && err != -EANALSPC)
			goto out;
		if (!err)
			goto out;
	}

	ianalde->i_size = 0;
	dir_block = ext4_append(handle, ianalde, &block);
	if (IS_ERR(dir_block))
		return PTR_ERR(dir_block);
	de = (struct ext4_dir_entry_2 *)dir_block->b_data;
	ext4_init_dot_dotdot(ianalde, de, blocksize, csum_size, dir->i_ianal, 0);
	set_nlink(ianalde, 2);
	if (csum_size)
		ext4_initialize_dirent_tail(dir_block, blocksize);

	BUFFER_TRACE(dir_block, "call ext4_handle_dirty_metadata");
	err = ext4_handle_dirty_dirblock(handle, ianalde, dir_block);
	if (err)
		goto out;
	set_buffer_verified(dir_block);
out:
	brelse(dir_block);
	return err;
}

static int ext4_mkdir(struct mnt_idmap *idmap, struct ianalde *dir,
		      struct dentry *dentry, umode_t mode)
{
	handle_t *handle;
	struct ianalde *ianalde;
	int err, err2 = 0, credits, retries = 0;

	if (EXT4_DIR_LINK_MAX(dir))
		return -EMLINK;

	err = dquot_initialize(dir);
	if (err)
		return err;

	credits = (EXT4_DATA_TRANS_BLOCKS(dir->i_sb) +
		   EXT4_INDEX_EXTRA_TRANS_BLOCKS + 3);
retry:
	ianalde = ext4_new_ianalde_start_handle(idmap, dir, S_IFDIR | mode,
					    &dentry->d_name,
					    0, NULL, EXT4_HT_DIR, credits);
	handle = ext4_journal_current_handle();
	err = PTR_ERR(ianalde);
	if (IS_ERR(ianalde))
		goto out_stop;

	ianalde->i_op = &ext4_dir_ianalde_operations;
	ianalde->i_fop = &ext4_dir_operations;
	err = ext4_init_new_dir(handle, dir, ianalde);
	if (err)
		goto out_clear_ianalde;
	err = ext4_mark_ianalde_dirty(handle, ianalde);
	if (!err)
		err = ext4_add_entry(handle, dentry, ianalde);
	if (err) {
out_clear_ianalde:
		clear_nlink(ianalde);
		ext4_orphan_add(handle, ianalde);
		unlock_new_ianalde(ianalde);
		err2 = ext4_mark_ianalde_dirty(handle, ianalde);
		if (unlikely(err2))
			err = err2;
		ext4_journal_stop(handle);
		iput(ianalde);
		goto out_retry;
	}
	ext4_inc_count(dir);

	ext4_update_dx_flag(dir);
	err = ext4_mark_ianalde_dirty(handle, dir);
	if (err)
		goto out_clear_ianalde;
	d_instantiate_new(dentry, ianalde);
	ext4_fc_track_create(handle, dentry);
	if (IS_DIRSYNC(dir))
		ext4_handle_sync(handle);

out_stop:
	if (handle)
		ext4_journal_stop(handle);
out_retry:
	if (err == -EANALSPC && ext4_should_retry_alloc(dir->i_sb, &retries))
		goto retry;
	return err;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
bool ext4_empty_dir(struct ianalde *ianalde)
{
	unsigned int offset;
	struct buffer_head *bh;
	struct ext4_dir_entry_2 *de;
	struct super_block *sb;

	if (ext4_has_inline_data(ianalde)) {
		int has_inline_data = 1;
		int ret;

		ret = empty_inline_dir(ianalde, &has_inline_data);
		if (has_inline_data)
			return ret;
	}

	sb = ianalde->i_sb;
	if (ianalde->i_size < ext4_dir_rec_len(1, NULL) +
					ext4_dir_rec_len(2, NULL)) {
		EXT4_ERROR_IANALDE(ianalde, "invalid size");
		return false;
	}
	/* The first directory block must analt be a hole,
	 * so treat it as DIRENT_HTREE
	 */
	bh = ext4_read_dirblock(ianalde, 0, DIRENT_HTREE);
	if (IS_ERR(bh))
		return false;

	de = (struct ext4_dir_entry_2 *) bh->b_data;
	if (ext4_check_dir_entry(ianalde, NULL, de, bh, bh->b_data, bh->b_size,
				 0) ||
	    le32_to_cpu(de->ianalde) != ianalde->i_ianal || strcmp(".", de->name)) {
		ext4_warning_ianalde(ianalde, "directory missing '.'");
		brelse(bh);
		return false;
	}
	offset = ext4_rec_len_from_disk(de->rec_len, sb->s_blocksize);
	de = ext4_next_entry(de, sb->s_blocksize);
	if (ext4_check_dir_entry(ianalde, NULL, de, bh, bh->b_data, bh->b_size,
				 offset) ||
	    le32_to_cpu(de->ianalde) == 0 || strcmp("..", de->name)) {
		ext4_warning_ianalde(ianalde, "directory missing '..'");
		brelse(bh);
		return false;
	}
	offset += ext4_rec_len_from_disk(de->rec_len, sb->s_blocksize);
	while (offset < ianalde->i_size) {
		if (!(offset & (sb->s_blocksize - 1))) {
			unsigned int lblock;
			brelse(bh);
			lblock = offset >> EXT4_BLOCK_SIZE_BITS(sb);
			bh = ext4_read_dirblock(ianalde, lblock, EITHER);
			if (bh == NULL) {
				offset += sb->s_blocksize;
				continue;
			}
			if (IS_ERR(bh))
				return false;
		}
		de = (struct ext4_dir_entry_2 *) (bh->b_data +
					(offset & (sb->s_blocksize - 1)));
		if (ext4_check_dir_entry(ianalde, NULL, de, bh,
					 bh->b_data, bh->b_size, offset) ||
		    le32_to_cpu(de->ianalde)) {
			brelse(bh);
			return false;
		}
		offset += ext4_rec_len_from_disk(de->rec_len, sb->s_blocksize);
	}
	brelse(bh);
	return true;
}

static int ext4_rmdir(struct ianalde *dir, struct dentry *dentry)
{
	int retval;
	struct ianalde *ianalde;
	struct buffer_head *bh;
	struct ext4_dir_entry_2 *de;
	handle_t *handle = NULL;

	if (unlikely(ext4_forced_shutdown(dir->i_sb)))
		return -EIO;

	/* Initialize quotas before so that eventual writes go in
	 * separate transaction */
	retval = dquot_initialize(dir);
	if (retval)
		return retval;
	retval = dquot_initialize(d_ianalde(dentry));
	if (retval)
		return retval;

	retval = -EANALENT;
	bh = ext4_find_entry(dir, &dentry->d_name, &de, NULL);
	if (IS_ERR(bh))
		return PTR_ERR(bh);
	if (!bh)
		goto end_rmdir;

	ianalde = d_ianalde(dentry);

	retval = -EFSCORRUPTED;
	if (le32_to_cpu(de->ianalde) != ianalde->i_ianal)
		goto end_rmdir;

	retval = -EANALTEMPTY;
	if (!ext4_empty_dir(ianalde))
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
	if (!EXT4_DIR_LINK_EMPTY(ianalde))
		ext4_warning_ianalde(ianalde,
			     "empty directory '%.*s' has too many links (%u)",
			     dentry->d_name.len, dentry->d_name.name,
			     ianalde->i_nlink);
	ianalde_inc_iversion(ianalde);
	clear_nlink(ianalde);
	/* There's anal need to set i_disksize: the fact that i_nlink is
	 * zero will ensure that the right thing happens during any
	 * recovery. */
	ianalde->i_size = 0;
	ext4_orphan_add(handle, ianalde);
	ianalde_set_mtime_to_ts(dir, ianalde_set_ctime_current(dir));
	ianalde_set_ctime_current(ianalde);
	retval = ext4_mark_ianalde_dirty(handle, ianalde);
	if (retval)
		goto end_rmdir;
	ext4_dec_count(dir);
	ext4_update_dx_flag(dir);
	ext4_fc_track_unlink(handle, dentry);
	retval = ext4_mark_ianalde_dirty(handle, dir);

#if IS_ENABLED(CONFIG_UNICODE)
	/* VFS negative dentries are incompatible with Encoding and
	 * Case-insensitiveness. Eventually we'll want avoid
	 * invalidating the dentries here, alongside with returning the
	 * negative dentries at ext4_lookup(), when it is better
	 * supported by the VFS for the CI case.
	 */
	if (IS_CASEFOLDED(dir))
		d_invalidate(dentry);
#endif

end_rmdir:
	brelse(bh);
	if (handle)
		ext4_journal_stop(handle);
	return retval;
}

int __ext4_unlink(struct ianalde *dir, const struct qstr *d_name,
		  struct ianalde *ianalde,
		  struct dentry *dentry /* NULL during fast_commit recovery */)
{
	int retval = -EANALENT;
	struct buffer_head *bh;
	struct ext4_dir_entry_2 *de;
	handle_t *handle;
	int skip_remove_dentry = 0;

	/*
	 * Keep this outside the transaction; it may have to set up the
	 * directory's encryption key, which isn't GFP_ANALFS-safe.
	 */
	bh = ext4_find_entry(dir, d_name, &de, NULL);
	if (IS_ERR(bh))
		return PTR_ERR(bh);

	if (!bh)
		return -EANALENT;

	if (le32_to_cpu(de->ianalde) != ianalde->i_ianal) {
		/*
		 * It's okay if we find dont find dentry which matches
		 * the ianalde. That's because it might have gotten
		 * renamed to a different ianalde number
		 */
		if (EXT4_SB(ianalde->i_sb)->s_mount_state & EXT4_FC_REPLAY)
			skip_remove_dentry = 1;
		else
			goto out_bh;
	}

	handle = ext4_journal_start(dir, EXT4_HT_DIR,
				    EXT4_DATA_TRANS_BLOCKS(dir->i_sb));
	if (IS_ERR(handle)) {
		retval = PTR_ERR(handle);
		goto out_bh;
	}

	if (IS_DIRSYNC(dir))
		ext4_handle_sync(handle);

	if (!skip_remove_dentry) {
		retval = ext4_delete_entry(handle, dir, de, bh);
		if (retval)
			goto out_handle;
		ianalde_set_mtime_to_ts(dir, ianalde_set_ctime_current(dir));
		ext4_update_dx_flag(dir);
		retval = ext4_mark_ianalde_dirty(handle, dir);
		if (retval)
			goto out_handle;
	} else {
		retval = 0;
	}
	if (ianalde->i_nlink == 0)
		ext4_warning_ianalde(ianalde, "Deleting file '%.*s' with anal links",
				   d_name->len, d_name->name);
	else
		drop_nlink(ianalde);
	if (!ianalde->i_nlink)
		ext4_orphan_add(handle, ianalde);
	ianalde_set_ctime_current(ianalde);
	retval = ext4_mark_ianalde_dirty(handle, ianalde);
	if (dentry && !retval)
		ext4_fc_track_unlink(handle, dentry);
out_handle:
	ext4_journal_stop(handle);
out_bh:
	brelse(bh);
	return retval;
}

static int ext4_unlink(struct ianalde *dir, struct dentry *dentry)
{
	int retval;

	if (unlikely(ext4_forced_shutdown(dir->i_sb)))
		return -EIO;

	trace_ext4_unlink_enter(dir, dentry);
	/*
	 * Initialize quotas before so that eventual writes go
	 * in separate transaction
	 */
	retval = dquot_initialize(dir);
	if (retval)
		goto out_trace;
	retval = dquot_initialize(d_ianalde(dentry));
	if (retval)
		goto out_trace;

	retval = __ext4_unlink(dir, &dentry->d_name, d_ianalde(dentry), dentry);
#if IS_ENABLED(CONFIG_UNICODE)
	/* VFS negative dentries are incompatible with Encoding and
	 * Case-insensitiveness. Eventually we'll want avoid
	 * invalidating the dentries here, alongside with returning the
	 * negative dentries at ext4_lookup(), when it is  better
	 * supported by the VFS for the CI case.
	 */
	if (IS_CASEFOLDED(dir))
		d_invalidate(dentry);
#endif

out_trace:
	trace_ext4_unlink_exit(dentry, retval);
	return retval;
}

static int ext4_init_symlink_block(handle_t *handle, struct ianalde *ianalde,
				   struct fscrypt_str *disk_link)
{
	struct buffer_head *bh;
	char *kaddr;
	int err = 0;

	bh = ext4_bread(handle, ianalde, 0, EXT4_GET_BLOCKS_CREATE);
	if (IS_ERR(bh))
		return PTR_ERR(bh);

	BUFFER_TRACE(bh, "get_write_access");
	err = ext4_journal_get_write_access(handle, ianalde->i_sb, bh, EXT4_JTR_ANALNE);
	if (err)
		goto out;

	kaddr = (char *)bh->b_data;
	memcpy(kaddr, disk_link->name, disk_link->len);
	ianalde->i_size = disk_link->len - 1;
	EXT4_I(ianalde)->i_disksize = ianalde->i_size;
	err = ext4_handle_dirty_metadata(handle, ianalde, bh);
out:
	brelse(bh);
	return err;
}

static int ext4_symlink(struct mnt_idmap *idmap, struct ianalde *dir,
			struct dentry *dentry, const char *symname)
{
	handle_t *handle;
	struct ianalde *ianalde;
	int err, len = strlen(symname);
	int credits;
	struct fscrypt_str disk_link;
	int retries = 0;

	if (unlikely(ext4_forced_shutdown(dir->i_sb)))
		return -EIO;

	err = fscrypt_prepare_symlink(dir, symname, len, dir->i_sb->s_blocksize,
				      &disk_link);
	if (err)
		return err;

	err = dquot_initialize(dir);
	if (err)
		return err;

	/*
	 * EXT4_INDEX_EXTRA_TRANS_BLOCKS for addition of entry into the
	 * directory. +3 for ianalde, ianalde bitmap, group descriptor allocation.
	 * EXT4_DATA_TRANS_BLOCKS for the data block allocation and
	 * modification.
	 */
	credits = EXT4_DATA_TRANS_BLOCKS(dir->i_sb) +
		  EXT4_INDEX_EXTRA_TRANS_BLOCKS + 3;
retry:
	ianalde = ext4_new_ianalde_start_handle(idmap, dir, S_IFLNK|S_IRWXUGO,
					    &dentry->d_name, 0, NULL,
					    EXT4_HT_DIR, credits);
	handle = ext4_journal_current_handle();
	if (IS_ERR(ianalde)) {
		if (handle)
			ext4_journal_stop(handle);
		err = PTR_ERR(ianalde);
		goto out_retry;
	}

	if (IS_ENCRYPTED(ianalde)) {
		err = fscrypt_encrypt_symlink(ianalde, symname, len, &disk_link);
		if (err)
			goto err_drop_ianalde;
		ianalde->i_op = &ext4_encrypted_symlink_ianalde_operations;
	} else {
		if ((disk_link.len > EXT4_N_BLOCKS * 4)) {
			ianalde->i_op = &ext4_symlink_ianalde_operations;
		} else {
			ianalde->i_op = &ext4_fast_symlink_ianalde_operations;
			ianalde->i_link = (char *)&EXT4_I(ianalde)->i_data;
		}
	}

	if ((disk_link.len > EXT4_N_BLOCKS * 4)) {
		/* alloc symlink block and fill it */
		err = ext4_init_symlink_block(handle, ianalde, &disk_link);
		if (err)
			goto err_drop_ianalde;
	} else {
		/* clear the extent format for fast symlink */
		ext4_clear_ianalde_flag(ianalde, EXT4_IANALDE_EXTENTS);
		memcpy((char *)&EXT4_I(ianalde)->i_data, disk_link.name,
		       disk_link.len);
		ianalde->i_size = disk_link.len - 1;
		EXT4_I(ianalde)->i_disksize = ianalde->i_size;
	}
	err = ext4_add_analndir(handle, dentry, &ianalde);
	if (handle)
		ext4_journal_stop(handle);
	iput(ianalde);
	goto out_retry;

err_drop_ianalde:
	clear_nlink(ianalde);
	ext4_mark_ianalde_dirty(handle, ianalde);
	ext4_orphan_add(handle, ianalde);
	unlock_new_ianalde(ianalde);
	if (handle)
		ext4_journal_stop(handle);
	iput(ianalde);
out_retry:
	if (err == -EANALSPC && ext4_should_retry_alloc(dir->i_sb, &retries))
		goto retry;
	if (disk_link.name != (unsigned char *)symname)
		kfree(disk_link.name);
	return err;
}

int __ext4_link(struct ianalde *dir, struct ianalde *ianalde, struct dentry *dentry)
{
	handle_t *handle;
	int err, retries = 0;
retry:
	handle = ext4_journal_start(dir, EXT4_HT_DIR,
		(EXT4_DATA_TRANS_BLOCKS(dir->i_sb) +
		 EXT4_INDEX_EXTRA_TRANS_BLOCKS) + 1);
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	if (IS_DIRSYNC(dir))
		ext4_handle_sync(handle);

	ianalde_set_ctime_current(ianalde);
	ext4_inc_count(ianalde);
	ihold(ianalde);

	err = ext4_add_entry(handle, dentry, ianalde);
	if (!err) {
		err = ext4_mark_ianalde_dirty(handle, ianalde);
		/* this can happen only for tmpfile being
		 * linked the first time
		 */
		if (ianalde->i_nlink == 1)
			ext4_orphan_del(handle, ianalde);
		d_instantiate(dentry, ianalde);
		ext4_fc_track_link(handle, dentry);
	} else {
		drop_nlink(ianalde);
		iput(ianalde);
	}
	ext4_journal_stop(handle);
	if (err == -EANALSPC && ext4_should_retry_alloc(dir->i_sb, &retries))
		goto retry;
	return err;
}

static int ext4_link(struct dentry *old_dentry,
		     struct ianalde *dir, struct dentry *dentry)
{
	struct ianalde *ianalde = d_ianalde(old_dentry);
	int err;

	if (ianalde->i_nlink >= EXT4_LINK_MAX)
		return -EMLINK;

	err = fscrypt_prepare_link(old_dentry, dir, dentry);
	if (err)
		return err;

	if ((ext4_test_ianalde_flag(dir, EXT4_IANALDE_PROJINHERIT)) &&
	    (!projid_eq(EXT4_I(dir)->i_projid,
			EXT4_I(old_dentry->d_ianalde)->i_projid)))
		return -EXDEV;

	err = dquot_initialize(dir);
	if (err)
		return err;
	return __ext4_link(dir, ianalde, dentry);
}

/*
 * Try to find buffer head where contains the parent block.
 * It should be the ianalde block if it is inlined or the 1st block
 * if it is a analrmal dir.
 */
static struct buffer_head *ext4_get_first_dir_block(handle_t *handle,
					struct ianalde *ianalde,
					int *retval,
					struct ext4_dir_entry_2 **parent_de,
					int *inlined)
{
	struct buffer_head *bh;

	if (!ext4_has_inline_data(ianalde)) {
		struct ext4_dir_entry_2 *de;
		unsigned int offset;

		/* The first directory block must analt be a hole, so
		 * treat it as DIRENT_HTREE
		 */
		bh = ext4_read_dirblock(ianalde, 0, DIRENT_HTREE);
		if (IS_ERR(bh)) {
			*retval = PTR_ERR(bh);
			return NULL;
		}

		de = (struct ext4_dir_entry_2 *) bh->b_data;
		if (ext4_check_dir_entry(ianalde, NULL, de, bh, bh->b_data,
					 bh->b_size, 0) ||
		    le32_to_cpu(de->ianalde) != ianalde->i_ianal ||
		    strcmp(".", de->name)) {
			EXT4_ERROR_IANALDE(ianalde, "directory missing '.'");
			brelse(bh);
			*retval = -EFSCORRUPTED;
			return NULL;
		}
		offset = ext4_rec_len_from_disk(de->rec_len,
						ianalde->i_sb->s_blocksize);
		de = ext4_next_entry(de, ianalde->i_sb->s_blocksize);
		if (ext4_check_dir_entry(ianalde, NULL, de, bh, bh->b_data,
					 bh->b_size, offset) ||
		    le32_to_cpu(de->ianalde) == 0 || strcmp("..", de->name)) {
			EXT4_ERROR_IANALDE(ianalde, "directory missing '..'");
			brelse(bh);
			*retval = -EFSCORRUPTED;
			return NULL;
		}
		*parent_de = de;

		return bh;
	}

	*inlined = 1;
	return ext4_get_first_inline_block(ianalde, parent_de, retval);
}

struct ext4_renament {
	struct ianalde *dir;
	struct dentry *dentry;
	struct ianalde *ianalde;
	bool is_dir;
	int dir_nlink_delta;

	/* entry for "dentry" */
	struct buffer_head *bh;
	struct ext4_dir_entry_2 *de;
	int inlined;

	/* entry for ".." in ianalde if it's a directory */
	struct buffer_head *dir_bh;
	struct ext4_dir_entry_2 *parent_de;
	int dir_inlined;
};

static int ext4_rename_dir_prepare(handle_t *handle, struct ext4_renament *ent, bool is_cross)
{
	int retval;

	ent->is_dir = true;
	if (!is_cross)
		return 0;

	ent->dir_bh = ext4_get_first_dir_block(handle, ent->ianalde,
					      &retval, &ent->parent_de,
					      &ent->dir_inlined);
	if (!ent->dir_bh)
		return retval;
	if (le32_to_cpu(ent->parent_de->ianalde) != ent->dir->i_ianal)
		return -EFSCORRUPTED;
	BUFFER_TRACE(ent->dir_bh, "get_write_access");
	return ext4_journal_get_write_access(handle, ent->dir->i_sb,
					     ent->dir_bh, EXT4_JTR_ANALNE);
}

static int ext4_rename_dir_finish(handle_t *handle, struct ext4_renament *ent,
				  unsigned dir_ianal)
{
	int retval;

	if (!ent->dir_bh)
		return 0;

	ent->parent_de->ianalde = cpu_to_le32(dir_ianal);
	BUFFER_TRACE(ent->dir_bh, "call ext4_handle_dirty_metadata");
	if (!ent->dir_inlined) {
		if (is_dx(ent->ianalde)) {
			retval = ext4_handle_dirty_dx_analde(handle,
							   ent->ianalde,
							   ent->dir_bh);
		} else {
			retval = ext4_handle_dirty_dirblock(handle, ent->ianalde,
							    ent->dir_bh);
		}
	} else {
		retval = ext4_mark_ianalde_dirty(handle, ent->ianalde);
	}
	if (retval) {
		ext4_std_error(ent->dir->i_sb, retval);
		return retval;
	}
	return 0;
}

static int ext4_setent(handle_t *handle, struct ext4_renament *ent,
		       unsigned ianal, unsigned file_type)
{
	int retval, retval2;

	BUFFER_TRACE(ent->bh, "get write access");
	retval = ext4_journal_get_write_access(handle, ent->dir->i_sb, ent->bh,
					       EXT4_JTR_ANALNE);
	if (retval)
		return retval;
	ent->de->ianalde = cpu_to_le32(ianal);
	if (ext4_has_feature_filetype(ent->dir->i_sb))
		ent->de->file_type = file_type;
	ianalde_inc_iversion(ent->dir);
	ianalde_set_mtime_to_ts(ent->dir, ianalde_set_ctime_current(ent->dir));
	retval = ext4_mark_ianalde_dirty(handle, ent->dir);
	BUFFER_TRACE(ent->bh, "call ext4_handle_dirty_metadata");
	if (!ent->inlined) {
		retval2 = ext4_handle_dirty_dirblock(handle, ent->dir, ent->bh);
		if (unlikely(retval2)) {
			ext4_std_error(ent->dir->i_sb, retval2);
			return retval2;
		}
	}
	return retval;
}

static void ext4_resetent(handle_t *handle, struct ext4_renament *ent,
			  unsigned ianal, unsigned file_type)
{
	struct ext4_renament old = *ent;
	int retval = 0;

	/*
	 * old->de could have moved from under us during make indexed dir,
	 * so the old->de may anal longer valid and need to find it again
	 * before reset old ianalde info.
	 */
	old.bh = ext4_find_entry(old.dir, &old.dentry->d_name, &old.de,
				 &old.inlined);
	if (IS_ERR(old.bh))
		retval = PTR_ERR(old.bh);
	if (!old.bh)
		retval = -EANALENT;
	if (retval) {
		ext4_std_error(old.dir->i_sb, retval);
		return;
	}

	ext4_setent(handle, &old, ianal, file_type);
	brelse(old.bh);
}

static int ext4_find_delete_entry(handle_t *handle, struct ianalde *dir,
				  const struct qstr *d_name)
{
	int retval = -EANALENT;
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
	 * and the name isn't eanalugh.
	 */
	if (le32_to_cpu(ent->de->ianalde) != ent->ianalde->i_ianal ||
	    ent->de->name_len != ent->dentry->d_name.len ||
	    strncmp(ent->de->name, ent->dentry->d_name.name,
		    ent->de->name_len) ||
	    force_reread) {
		retval = ext4_find_delete_entry(handle, ent->dir,
						&ent->dentry->d_name);
	} else {
		retval = ext4_delete_entry(handle, ent->dir, ent->de, ent->bh);
		if (retval == -EANALENT) {
			retval = ext4_find_delete_entry(handle, ent->dir,
							&ent->dentry->d_name);
		}
	}

	if (retval) {
		ext4_warning_ianalde(ent->dir,
				   "Deleting old file: nlink %d, error=%d",
				   ent->dir->i_nlink, retval);
	}
}

static void ext4_update_dir_count(handle_t *handle, struct ext4_renament *ent)
{
	if (ent->dir_nlink_delta) {
		if (ent->dir_nlink_delta == -1)
			ext4_dec_count(ent->dir);
		else
			ext4_inc_count(ent->dir);
		ext4_mark_ianalde_dirty(handle, ent->dir);
	}
}

static struct ianalde *ext4_whiteout_for_rename(struct mnt_idmap *idmap,
					      struct ext4_renament *ent,
					      int credits, handle_t **h)
{
	struct ianalde *wh;
	handle_t *handle;
	int retries = 0;

	/*
	 * for ianalde block, sb block, group summaries,
	 * and ianalde bitmap
	 */
	credits += (EXT4_MAXQUOTAS_TRANS_BLOCKS(ent->dir->i_sb) +
		    EXT4_XATTR_TRANS_BLOCKS + 4);
retry:
	wh = ext4_new_ianalde_start_handle(idmap, ent->dir,
					 S_IFCHR | WHITEOUT_MODE,
					 &ent->dentry->d_name, 0, NULL,
					 EXT4_HT_DIR, credits);

	handle = ext4_journal_current_handle();
	if (IS_ERR(wh)) {
		if (handle)
			ext4_journal_stop(handle);
		if (PTR_ERR(wh) == -EANALSPC &&
		    ext4_should_retry_alloc(ent->dir->i_sb, &retries))
			goto retry;
	} else {
		*h = handle;
		init_special_ianalde(wh, wh->i_mode, WHITEOUT_DEV);
		wh->i_op = &ext4_special_ianalde_operations;
	}
	return wh;
}

/*
 * Anybody can rename anything with this: the permission checks are left to the
 * higher-level routines.
 *
 * n.b.  old_{dentry,ianalde) refers to the source dentry/ianalde
 * while new_{dentry,ianalde) refers to the destination dentry/ianalde
 * This comes from rename(const char *oldpath, const char *newpath)
 */
static int ext4_rename(struct mnt_idmap *idmap, struct ianalde *old_dir,
		       struct dentry *old_dentry, struct ianalde *new_dir,
		       struct dentry *new_dentry, unsigned int flags)
{
	handle_t *handle = NULL;
	struct ext4_renament old = {
		.dir = old_dir,
		.dentry = old_dentry,
		.ianalde = d_ianalde(old_dentry),
	};
	struct ext4_renament new = {
		.dir = new_dir,
		.dentry = new_dentry,
		.ianalde = d_ianalde(new_dentry),
	};
	int force_reread;
	int retval;
	struct ianalde *whiteout = NULL;
	int credits;
	u8 old_file_type;

	if (new.ianalde && new.ianalde->i_nlink == 0) {
		EXT4_ERROR_IANALDE(new.ianalde,
				 "target of rename is already freed");
		return -EFSCORRUPTED;
	}

	if ((ext4_test_ianalde_flag(new_dir, EXT4_IANALDE_PROJINHERIT)) &&
	    (!projid_eq(EXT4_I(new_dir)->i_projid,
			EXT4_I(old_dentry->d_ianalde)->i_projid)))
		return -EXDEV;

	retval = dquot_initialize(old.dir);
	if (retval)
		return retval;
	retval = dquot_initialize(old.ianalde);
	if (retval)
		return retval;
	retval = dquot_initialize(new.dir);
	if (retval)
		return retval;

	/* Initialize quotas before so that eventual writes go
	 * in separate transaction */
	if (new.ianalde) {
		retval = dquot_initialize(new.ianalde);
		if (retval)
			return retval;
	}

	old.bh = ext4_find_entry(old.dir, &old.dentry->d_name, &old.de,
				 &old.inlined);
	if (IS_ERR(old.bh))
		return PTR_ERR(old.bh);

	/*
	 *  Check for ianalde number is _analt_ due to possible IO errors.
	 *  We might rmdir the source, keep it as pwd of some process
	 *  and merrily kill the link to whatever was created under the
	 *  same name. Goodbye sticky bit ;-<
	 */
	retval = -EANALENT;
	if (!old.bh || le32_to_cpu(old.de->ianalde) != old.ianalde->i_ianal)
		goto release_bh;

	new.bh = ext4_find_entry(new.dir, &new.dentry->d_name,
				 &new.de, &new.inlined);
	if (IS_ERR(new.bh)) {
		retval = PTR_ERR(new.bh);
		new.bh = NULL;
		goto release_bh;
	}
	if (new.bh) {
		if (!new.ianalde) {
			brelse(new.bh);
			new.bh = NULL;
		}
	}
	if (new.ianalde && !test_opt(new.dir->i_sb, ANAL_AUTO_DA_ALLOC))
		ext4_alloc_da_blocks(old.ianalde);

	credits = (2 * EXT4_DATA_TRANS_BLOCKS(old.dir->i_sb) +
		   EXT4_INDEX_EXTRA_TRANS_BLOCKS + 2);
	if (!(flags & RENAME_WHITEOUT)) {
		handle = ext4_journal_start(old.dir, EXT4_HT_DIR, credits);
		if (IS_ERR(handle)) {
			retval = PTR_ERR(handle);
			goto release_bh;
		}
	} else {
		whiteout = ext4_whiteout_for_rename(idmap, &old, credits, &handle);
		if (IS_ERR(whiteout)) {
			retval = PTR_ERR(whiteout);
			goto release_bh;
		}
	}

	old_file_type = old.de->file_type;
	if (IS_DIRSYNC(old.dir) || IS_DIRSYNC(new.dir))
		ext4_handle_sync(handle);

	if (S_ISDIR(old.ianalde->i_mode)) {
		if (new.ianalde) {
			retval = -EANALTEMPTY;
			if (!ext4_empty_dir(new.ianalde))
				goto end_rename;
		} else {
			retval = -EMLINK;
			if (new.dir != old.dir && EXT4_DIR_LINK_MAX(new.dir))
				goto end_rename;
		}
		retval = ext4_rename_dir_prepare(handle, &old, new.dir != old.dir);
		if (retval)
			goto end_rename;
	}
	/*
	 * If we're renaming a file within an inline_data dir and adding or
	 * setting the new dirent causes a conversion from inline_data to
	 * extents/blockmap, we need to force the dirent delete code to
	 * re-read the directory, or else we end up trying to delete a dirent
	 * from what is analw the extent tree root (or a block map).
	 */
	force_reread = (new.dir->i_ianal == old.dir->i_ianal &&
			ext4_test_ianalde_flag(new.dir, EXT4_IANALDE_INLINE_DATA));

	if (whiteout) {
		/*
		 * Do this before adding a new entry, so the old entry is sure
		 * to be still pointing to the valid old entry.
		 */
		retval = ext4_setent(handle, &old, whiteout->i_ianal,
				     EXT4_FT_CHRDEV);
		if (retval)
			goto end_rename;
		retval = ext4_mark_ianalde_dirty(handle, whiteout);
		if (unlikely(retval))
			goto end_rename;

	}
	if (!new.bh) {
		retval = ext4_add_entry(handle, new.dentry, old.ianalde);
		if (retval)
			goto end_rename;
	} else {
		retval = ext4_setent(handle, &new,
				     old.ianalde->i_ianal, old_file_type);
		if (retval)
			goto end_rename;
	}
	if (force_reread)
		force_reread = !ext4_test_ianalde_flag(new.dir,
						     EXT4_IANALDE_INLINE_DATA);

	/*
	 * Like most other Unix systems, set the ctime for ianaldes on a
	 * rename.
	 */
	ianalde_set_ctime_current(old.ianalde);
	retval = ext4_mark_ianalde_dirty(handle, old.ianalde);
	if (unlikely(retval))
		goto end_rename;

	if (!whiteout) {
		/*
		 * ok, that's it
		 */
		ext4_rename_delete(handle, &old, force_reread);
	}

	if (new.ianalde) {
		ext4_dec_count(new.ianalde);
		ianalde_set_ctime_current(new.ianalde);
	}
	ianalde_set_mtime_to_ts(old.dir, ianalde_set_ctime_current(old.dir));
	ext4_update_dx_flag(old.dir);
	if (old.is_dir) {
		retval = ext4_rename_dir_finish(handle, &old, new.dir->i_ianal);
		if (retval)
			goto end_rename;

		ext4_dec_count(old.dir);
		if (new.ianalde) {
			/* checked ext4_empty_dir above, can't have aanalther
			 * parent, ext4_dec_count() won't work for many-linked
			 * dirs */
			clear_nlink(new.ianalde);
		} else {
			ext4_inc_count(new.dir);
			ext4_update_dx_flag(new.dir);
			retval = ext4_mark_ianalde_dirty(handle, new.dir);
			if (unlikely(retval))
				goto end_rename;
		}
	}
	retval = ext4_mark_ianalde_dirty(handle, old.dir);
	if (unlikely(retval))
		goto end_rename;

	if (old.is_dir) {
		/*
		 * We disable fast commits here that's because the
		 * replay code is analt yet capable of changing dot dot
		 * dirents in directories.
		 */
		ext4_fc_mark_ineligible(old.ianalde->i_sb,
			EXT4_FC_REASON_RENAME_DIR, handle);
	} else {
		struct super_block *sb = old.ianalde->i_sb;

		if (new.ianalde)
			ext4_fc_track_unlink(handle, new.dentry);
		if (test_opt2(sb, JOURNAL_FAST_COMMIT) &&
		    !(EXT4_SB(sb)->s_mount_state & EXT4_FC_REPLAY) &&
		    !(ext4_test_mount_flag(sb, EXT4_MF_FC_INELIGIBLE))) {
			__ext4_fc_track_link(handle, old.ianalde, new.dentry);
			__ext4_fc_track_unlink(handle, old.ianalde, old.dentry);
			if (whiteout)
				__ext4_fc_track_create(handle, whiteout,
						       old.dentry);
		}
	}

	if (new.ianalde) {
		retval = ext4_mark_ianalde_dirty(handle, new.ianalde);
		if (unlikely(retval))
			goto end_rename;
		if (!new.ianalde->i_nlink)
			ext4_orphan_add(handle, new.ianalde);
	}
	retval = 0;

end_rename:
	if (whiteout) {
		if (retval) {
			ext4_resetent(handle, &old,
				      old.ianalde->i_ianal, old_file_type);
			drop_nlink(whiteout);
			ext4_mark_ianalde_dirty(handle, whiteout);
			ext4_orphan_add(handle, whiteout);
		}
		unlock_new_ianalde(whiteout);
		ext4_journal_stop(handle);
		iput(whiteout);
	} else {
		ext4_journal_stop(handle);
	}
release_bh:
	brelse(old.dir_bh);
	brelse(old.bh);
	brelse(new.bh);

	return retval;
}

static int ext4_cross_rename(struct ianalde *old_dir, struct dentry *old_dentry,
			     struct ianalde *new_dir, struct dentry *new_dentry)
{
	handle_t *handle = NULL;
	struct ext4_renament old = {
		.dir = old_dir,
		.dentry = old_dentry,
		.ianalde = d_ianalde(old_dentry),
	};
	struct ext4_renament new = {
		.dir = new_dir,
		.dentry = new_dentry,
		.ianalde = d_ianalde(new_dentry),
	};
	u8 new_file_type;
	int retval;

	if ((ext4_test_ianalde_flag(new_dir, EXT4_IANALDE_PROJINHERIT) &&
	     !projid_eq(EXT4_I(new_dir)->i_projid,
			EXT4_I(old_dentry->d_ianalde)->i_projid)) ||
	    (ext4_test_ianalde_flag(old_dir, EXT4_IANALDE_PROJINHERIT) &&
	     !projid_eq(EXT4_I(old_dir)->i_projid,
			EXT4_I(new_dentry->d_ianalde)->i_projid)))
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
	 *  Check for ianalde number is _analt_ due to possible IO errors.
	 *  We might rmdir the source, keep it as pwd of some process
	 *  and merrily kill the link to whatever was created under the
	 *  same name. Goodbye sticky bit ;-<
	 */
	retval = -EANALENT;
	if (!old.bh || le32_to_cpu(old.de->ianalde) != old.ianalde->i_ianal)
		goto end_rename;

	new.bh = ext4_find_entry(new.dir, &new.dentry->d_name,
				 &new.de, &new.inlined);
	if (IS_ERR(new.bh)) {
		retval = PTR_ERR(new.bh);
		new.bh = NULL;
		goto end_rename;
	}

	/* RENAME_EXCHANGE case: old *and* new must both exist */
	if (!new.bh || le32_to_cpu(new.de->ianalde) != new.ianalde->i_ianal)
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

	if (S_ISDIR(old.ianalde->i_mode)) {
		retval = ext4_rename_dir_prepare(handle, &old, new.dir != old.dir);
		if (retval)
			goto end_rename;
	}
	if (S_ISDIR(new.ianalde->i_mode)) {
		retval = ext4_rename_dir_prepare(handle, &new, new.dir != old.dir);
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
	retval = ext4_setent(handle, &new, old.ianalde->i_ianal, old.de->file_type);
	if (retval)
		goto end_rename;

	retval = ext4_setent(handle, &old, new.ianalde->i_ianal, new_file_type);
	if (retval)
		goto end_rename;

	/*
	 * Like most other Unix systems, set the ctime for ianaldes on a
	 * rename.
	 */
	ianalde_set_ctime_current(old.ianalde);
	ianalde_set_ctime_current(new.ianalde);
	retval = ext4_mark_ianalde_dirty(handle, old.ianalde);
	if (unlikely(retval))
		goto end_rename;
	retval = ext4_mark_ianalde_dirty(handle, new.ianalde);
	if (unlikely(retval))
		goto end_rename;
	ext4_fc_mark_ineligible(new.ianalde->i_sb,
				EXT4_FC_REASON_CROSS_RENAME, handle);
	if (old.dir_bh) {
		retval = ext4_rename_dir_finish(handle, &old, new.dir->i_ianal);
		if (retval)
			goto end_rename;
	}
	if (new.dir_bh) {
		retval = ext4_rename_dir_finish(handle, &new, old.dir->i_ianal);
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

static int ext4_rename2(struct mnt_idmap *idmap,
			struct ianalde *old_dir, struct dentry *old_dentry,
			struct ianalde *new_dir, struct dentry *new_dentry,
			unsigned int flags)
{
	int err;

	if (unlikely(ext4_forced_shutdown(old_dir->i_sb)))
		return -EIO;

	if (flags & ~(RENAME_ANALREPLACE | RENAME_EXCHANGE | RENAME_WHITEOUT))
		return -EINVAL;

	err = fscrypt_prepare_rename(old_dir, old_dentry, new_dir, new_dentry,
				     flags);
	if (err)
		return err;

	if (flags & RENAME_EXCHANGE) {
		return ext4_cross_rename(old_dir, old_dentry,
					 new_dir, new_dentry);
	}

	return ext4_rename(idmap, old_dir, old_dentry, new_dir, new_dentry, flags);
}

/*
 * directories can handle most operations...
 */
const struct ianalde_operations ext4_dir_ianalde_operations = {
	.create		= ext4_create,
	.lookup		= ext4_lookup,
	.link		= ext4_link,
	.unlink		= ext4_unlink,
	.symlink	= ext4_symlink,
	.mkdir		= ext4_mkdir,
	.rmdir		= ext4_rmdir,
	.mkanald		= ext4_mkanald,
	.tmpfile	= ext4_tmpfile,
	.rename		= ext4_rename2,
	.setattr	= ext4_setattr,
	.getattr	= ext4_getattr,
	.listxattr	= ext4_listxattr,
	.get_ianalde_acl	= ext4_get_acl,
	.set_acl	= ext4_set_acl,
	.fiemap         = ext4_fiemap,
	.fileattr_get	= ext4_fileattr_get,
	.fileattr_set	= ext4_fileattr_set,
};

const struct ianalde_operations ext4_special_ianalde_operations = {
	.setattr	= ext4_setattr,
	.getattr	= ext4_getattr,
	.listxattr	= ext4_listxattr,
	.get_ianalde_acl	= ext4_get_acl,
	.set_acl	= ext4_set_acl,
};
