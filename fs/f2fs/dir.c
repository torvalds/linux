// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/dir.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */
#include <asm/unaligned.h>
#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/sched/signal.h>
#include <linux/unicode.h>
#include "f2fs.h"
#include "node.h"
#include "acl.h"
#include "xattr.h"
#include <trace/events/f2fs.h>

#if IS_ENABLED(CONFIG_UNICODE)
extern struct kmem_cache *f2fs_cf_name_slab;
#endif

static unsigned long dir_blocks(struct inode *inode)
{
	return ((unsigned long long) (i_size_read(inode) + PAGE_SIZE - 1))
							>> PAGE_SHIFT;
}

static unsigned int dir_buckets(unsigned int level, int dir_level)
{
	if (level + dir_level < MAX_DIR_HASH_DEPTH / 2)
		return 1 << (level + dir_level);
	else
		return MAX_DIR_BUCKETS;
}

static unsigned int bucket_blocks(unsigned int level)
{
	if (level < MAX_DIR_HASH_DEPTH / 2)
		return 2;
	else
		return 4;
}

static unsigned char f2fs_filetype_table[F2FS_FT_MAX] = {
	[F2FS_FT_UNKNOWN]	= DT_UNKNOWN,
	[F2FS_FT_REG_FILE]	= DT_REG,
	[F2FS_FT_DIR]		= DT_DIR,
	[F2FS_FT_CHRDEV]	= DT_CHR,
	[F2FS_FT_BLKDEV]	= DT_BLK,
	[F2FS_FT_FIFO]		= DT_FIFO,
	[F2FS_FT_SOCK]		= DT_SOCK,
	[F2FS_FT_SYMLINK]	= DT_LNK,
};

static unsigned char f2fs_type_by_mode[S_IFMT >> S_SHIFT] = {
	[S_IFREG >> S_SHIFT]	= F2FS_FT_REG_FILE,
	[S_IFDIR >> S_SHIFT]	= F2FS_FT_DIR,
	[S_IFCHR >> S_SHIFT]	= F2FS_FT_CHRDEV,
	[S_IFBLK >> S_SHIFT]	= F2FS_FT_BLKDEV,
	[S_IFIFO >> S_SHIFT]	= F2FS_FT_FIFO,
	[S_IFSOCK >> S_SHIFT]	= F2FS_FT_SOCK,
	[S_IFLNK >> S_SHIFT]	= F2FS_FT_SYMLINK,
};

static void set_de_type(struct f2fs_dir_entry *de, umode_t mode)
{
	de->file_type = f2fs_type_by_mode[(mode & S_IFMT) >> S_SHIFT];
}

unsigned char f2fs_get_de_type(struct f2fs_dir_entry *de)
{
	if (de->file_type < F2FS_FT_MAX)
		return f2fs_filetype_table[de->file_type];
	return DT_UNKNOWN;
}

/* If @dir is casefolded, initialize @fname->cf_name from @fname->usr_fname. */
int f2fs_init_casefolded_name(const struct inode *dir,
			      struct f2fs_filename *fname)
{
#if IS_ENABLED(CONFIG_UNICODE)
	struct super_block *sb = dir->i_sb;

	if (IS_CASEFOLDED(dir) &&
	    !is_dot_dotdot(fname->usr_fname->name, fname->usr_fname->len)) {
		fname->cf_name.name = f2fs_kmem_cache_alloc(f2fs_cf_name_slab,
					GFP_NOFS, false, F2FS_SB(sb));
		if (!fname->cf_name.name)
			return -ENOMEM;
		fname->cf_name.len = utf8_casefold(sb->s_encoding,
						   fname->usr_fname,
						   fname->cf_name.name,
						   F2FS_NAME_LEN);
		if ((int)fname->cf_name.len <= 0) {
			kmem_cache_free(f2fs_cf_name_slab, fname->cf_name.name);
			fname->cf_name.name = NULL;
			if (sb_has_strict_encoding(sb))
				return -EINVAL;
			/* fall back to treating name as opaque byte sequence */
		}
	}
#endif
	return 0;
}

static int __f2fs_setup_filename(const struct inode *dir,
				 const struct fscrypt_name *crypt_name,
				 struct f2fs_filename *fname)
{
	int err;

	memset(fname, 0, sizeof(*fname));

	fname->usr_fname = crypt_name->usr_fname;
	fname->disk_name = crypt_name->disk_name;
#ifdef CONFIG_FS_ENCRYPTION
	fname->crypto_buf = crypt_name->crypto_buf;
#endif
	if (crypt_name->is_nokey_name) {
		/* hash was decoded from the no-key name */
		fname->hash = cpu_to_le32(crypt_name->hash);
	} else {
		err = f2fs_init_casefolded_name(dir, fname);
		if (err) {
			f2fs_free_filename(fname);
			return err;
		}
		f2fs_hash_filename(dir, fname);
	}
	return 0;
}

/*
 * Prepare to search for @iname in @dir.  This is similar to
 * fscrypt_setup_filename(), but this also handles computing the casefolded name
 * and the f2fs dirhash if needed, then packing all the information about this
 * filename up into a 'struct f2fs_filename'.
 */
int f2fs_setup_filename(struct inode *dir, const struct qstr *iname,
			int lookup, struct f2fs_filename *fname)
{
	struct fscrypt_name crypt_name;
	int err;

	err = fscrypt_setup_filename(dir, iname, lookup, &crypt_name);
	if (err)
		return err;

	return __f2fs_setup_filename(dir, &crypt_name, fname);
}

/*
 * Prepare to look up @dentry in @dir.  This is similar to
 * fscrypt_prepare_lookup(), but this also handles computing the casefolded name
 * and the f2fs dirhash if needed, then packing all the information about this
 * filename up into a 'struct f2fs_filename'.
 */
int f2fs_prepare_lookup(struct inode *dir, struct dentry *dentry,
			struct f2fs_filename *fname)
{
	struct fscrypt_name crypt_name;
	int err;

	err = fscrypt_prepare_lookup(dir, dentry, &crypt_name);
	if (err)
		return err;

	return __f2fs_setup_filename(dir, &crypt_name, fname);
}

void f2fs_free_filename(struct f2fs_filename *fname)
{
#ifdef CONFIG_FS_ENCRYPTION
	kfree(fname->crypto_buf.name);
	fname->crypto_buf.name = NULL;
#endif
#if IS_ENABLED(CONFIG_UNICODE)
	if (fname->cf_name.name) {
		kmem_cache_free(f2fs_cf_name_slab, fname->cf_name.name);
		fname->cf_name.name = NULL;
	}
#endif
}

static unsigned long dir_block_index(unsigned int level,
				int dir_level, unsigned int idx)
{
	unsigned long i;
	unsigned long bidx = 0;

	for (i = 0; i < level; i++)
		bidx += dir_buckets(i, dir_level) * bucket_blocks(i);
	bidx += idx * bucket_blocks(level);
	return bidx;
}

static struct f2fs_dir_entry *find_in_block(struct inode *dir,
				struct page *dentry_page,
				const struct f2fs_filename *fname,
				int *max_slots)
{
	struct f2fs_dentry_block *dentry_blk;
	struct f2fs_dentry_ptr d;

	dentry_blk = (struct f2fs_dentry_block *)page_address(dentry_page);

	make_dentry_ptr_block(dir, &d, dentry_blk);
	return f2fs_find_target_dentry(&d, fname, max_slots);
}

#if IS_ENABLED(CONFIG_UNICODE)
/*
 * Test whether a case-insensitive directory entry matches the filename
 * being searched for.
 *
 * Returns 1 for a match, 0 for no match, and -errno on an error.
 */
static int f2fs_match_ci_name(const struct inode *dir, const struct qstr *name,
			       const u8 *de_name, u32 de_name_len)
{
	const struct super_block *sb = dir->i_sb;
	const struct unicode_map *um = sb->s_encoding;
	struct fscrypt_str decrypted_name = FSTR_INIT(NULL, de_name_len);
	struct qstr entry = QSTR_INIT(de_name, de_name_len);
	int res;

	if (IS_ENCRYPTED(dir)) {
		const struct fscrypt_str encrypted_name =
			FSTR_INIT((u8 *)de_name, de_name_len);

		if (WARN_ON_ONCE(!fscrypt_has_encryption_key(dir)))
			return -EINVAL;

		decrypted_name.name = kmalloc(de_name_len, GFP_KERNEL);
		if (!decrypted_name.name)
			return -ENOMEM;
		res = fscrypt_fname_disk_to_usr(dir, 0, 0, &encrypted_name,
						&decrypted_name);
		if (res < 0)
			goto out;
		entry.name = decrypted_name.name;
		entry.len = decrypted_name.len;
	}

	res = utf8_strncasecmp_folded(um, name, &entry);
	/*
	 * In strict mode, ignore invalid names.  In non-strict mode,
	 * fall back to treating them as opaque byte sequences.
	 */
	if (res < 0 && !sb_has_strict_encoding(sb)) {
		res = name->len == entry.len &&
				memcmp(name->name, entry.name, name->len) == 0;
	} else {
		/* utf8_strncasecmp_folded returns 0 on match */
		res = (res == 0);
	}
out:
	kfree(decrypted_name.name);
	return res;
}
#endif /* CONFIG_UNICODE */

static inline int f2fs_match_name(const struct inode *dir,
				   const struct f2fs_filename *fname,
				   const u8 *de_name, u32 de_name_len)
{
	struct fscrypt_name f;

#if IS_ENABLED(CONFIG_UNICODE)
	if (fname->cf_name.name) {
		struct qstr cf = FSTR_TO_QSTR(&fname->cf_name);

		return f2fs_match_ci_name(dir, &cf, de_name, de_name_len);
	}
#endif
	f.usr_fname = fname->usr_fname;
	f.disk_name = fname->disk_name;
#ifdef CONFIG_FS_ENCRYPTION
	f.crypto_buf = fname->crypto_buf;
#endif
	return fscrypt_match_name(&f, de_name, de_name_len);
}

struct f2fs_dir_entry *f2fs_find_target_dentry(const struct f2fs_dentry_ptr *d,
			const struct f2fs_filename *fname, int *max_slots)
{
	struct f2fs_dir_entry *de;
	unsigned long bit_pos = 0;
	int max_len = 0;
	int res = 0;

	if (max_slots)
		*max_slots = 0;
	while (bit_pos < d->max) {
		if (!test_bit_le(bit_pos, d->bitmap)) {
			bit_pos++;
			max_len++;
			continue;
		}

		de = &d->dentry[bit_pos];

		if (unlikely(!de->name_len)) {
			bit_pos++;
			continue;
		}

		if (de->hash_code == fname->hash) {
			res = f2fs_match_name(d->inode, fname,
					      d->filename[bit_pos],
					      le16_to_cpu(de->name_len));
			if (res < 0)
				return ERR_PTR(res);
			if (res)
				goto found;
		}

		if (max_slots && max_len > *max_slots)
			*max_slots = max_len;
		max_len = 0;

		bit_pos += GET_DENTRY_SLOTS(le16_to_cpu(de->name_len));
	}

	de = NULL;
found:
	if (max_slots && max_len > *max_slots)
		*max_slots = max_len;
	return de;
}

static struct f2fs_dir_entry *find_in_level(struct inode *dir,
					unsigned int level,
					const struct f2fs_filename *fname,
					struct page **res_page)
{
	int s = GET_DENTRY_SLOTS(fname->disk_name.len);
	unsigned int nbucket, nblock;
	unsigned int bidx, end_block;
	struct page *dentry_page;
	struct f2fs_dir_entry *de = NULL;
	pgoff_t next_pgofs;
	bool room = false;
	int max_slots;

	nbucket = dir_buckets(level, F2FS_I(dir)->i_dir_level);
	nblock = bucket_blocks(level);

	bidx = dir_block_index(level, F2FS_I(dir)->i_dir_level,
			       le32_to_cpu(fname->hash) % nbucket);
	end_block = bidx + nblock;

	while (bidx < end_block) {
		/* no need to allocate new dentry pages to all the indices */
		dentry_page = f2fs_find_data_page(dir, bidx, &next_pgofs);
		if (IS_ERR(dentry_page)) {
			if (PTR_ERR(dentry_page) == -ENOENT) {
				room = true;
				bidx = next_pgofs;
				continue;
			} else {
				*res_page = dentry_page;
				break;
			}
		}

		de = find_in_block(dir, dentry_page, fname, &max_slots);
		if (IS_ERR(de)) {
			*res_page = ERR_CAST(de);
			de = NULL;
			break;
		} else if (de) {
			*res_page = dentry_page;
			break;
		}

		if (max_slots >= s)
			room = true;
		f2fs_put_page(dentry_page, 0);

		bidx++;
	}

	if (!de && room && F2FS_I(dir)->chash != fname->hash) {
		F2FS_I(dir)->chash = fname->hash;
		F2FS_I(dir)->clevel = level;
	}

	return de;
}

struct f2fs_dir_entry *__f2fs_find_entry(struct inode *dir,
					 const struct f2fs_filename *fname,
					 struct page **res_page)
{
	unsigned long npages = dir_blocks(dir);
	struct f2fs_dir_entry *de = NULL;
	unsigned int max_depth;
	unsigned int level;

	*res_page = NULL;

	if (f2fs_has_inline_dentry(dir)) {
		de = f2fs_find_in_inline_dir(dir, fname, res_page);
		goto out;
	}

	if (npages == 0)
		goto out;

	max_depth = F2FS_I(dir)->i_current_depth;
	if (unlikely(max_depth > MAX_DIR_HASH_DEPTH)) {
		f2fs_warn(F2FS_I_SB(dir), "Corrupted max_depth of %lu: %u",
			  dir->i_ino, max_depth);
		max_depth = MAX_DIR_HASH_DEPTH;
		f2fs_i_depth_write(dir, max_depth);
	}

	for (level = 0; level < max_depth; level++) {
		de = find_in_level(dir, level, fname, res_page);
		if (de || IS_ERR(*res_page))
			break;
	}
out:
	/* This is to increase the speed of f2fs_create */
	if (!de)
		F2FS_I(dir)->task = current;
	return de;
}

/*
 * Find an entry in the specified directory with the wanted name.
 * It returns the page where the entry was found (as a parameter - res_page),
 * and the entry itself. Page is returned mapped and unlocked.
 * Entry is guaranteed to be valid.
 */
struct f2fs_dir_entry *f2fs_find_entry(struct inode *dir,
			const struct qstr *child, struct page **res_page)
{
	struct f2fs_dir_entry *de = NULL;
	struct f2fs_filename fname;
	int err;

	err = f2fs_setup_filename(dir, child, 1, &fname);
	if (err) {
		if (err == -ENOENT)
			*res_page = NULL;
		else
			*res_page = ERR_PTR(err);
		return NULL;
	}

	de = __f2fs_find_entry(dir, &fname, res_page);

	f2fs_free_filename(&fname);
	return de;
}

struct f2fs_dir_entry *f2fs_parent_dir(struct inode *dir, struct page **p)
{
	return f2fs_find_entry(dir, &dotdot_name, p);
}

ino_t f2fs_inode_by_name(struct inode *dir, const struct qstr *qstr,
							struct page **page)
{
	ino_t res = 0;
	struct f2fs_dir_entry *de;

	de = f2fs_find_entry(dir, qstr, page);
	if (de) {
		res = le32_to_cpu(de->ino);
		f2fs_put_page(*page, 0);
	}

	return res;
}

void f2fs_set_link(struct inode *dir, struct f2fs_dir_entry *de,
		struct page *page, struct inode *inode)
{
	enum page_type type = f2fs_has_inline_dentry(dir) ? NODE : DATA;

	lock_page(page);
	f2fs_wait_on_page_writeback(page, type, true, true);
	de->ino = cpu_to_le32(inode->i_ino);
	set_de_type(de, inode->i_mode);
	set_page_dirty(page);

	dir->i_mtime = dir->i_ctime = current_time(dir);
	f2fs_mark_inode_dirty_sync(dir, false);
	f2fs_put_page(page, 1);
}

static void init_dent_inode(struct inode *dir, struct inode *inode,
			    const struct f2fs_filename *fname,
			    struct page *ipage)
{
	struct f2fs_inode *ri;

	if (!fname) /* tmpfile case? */
		return;

	f2fs_wait_on_page_writeback(ipage, NODE, true, true);

	/* copy name info. to this inode page */
	ri = F2FS_INODE(ipage);
	ri->i_namelen = cpu_to_le32(fname->disk_name.len);
	memcpy(ri->i_name, fname->disk_name.name, fname->disk_name.len);
	if (IS_ENCRYPTED(dir)) {
		file_set_enc_name(inode);
		/*
		 * Roll-forward recovery doesn't have encryption keys available,
		 * so it can't compute the dirhash for encrypted+casefolded
		 * filenames.  Append it to i_name if possible.  Else, disable
		 * roll-forward recovery of the dentry (i.e., make fsync'ing the
		 * file force a checkpoint) by setting LOST_PINO.
		 */
		if (IS_CASEFOLDED(dir)) {
			if (fname->disk_name.len + sizeof(f2fs_hash_t) <=
			    F2FS_NAME_LEN)
				put_unaligned(fname->hash, (f2fs_hash_t *)
					&ri->i_name[fname->disk_name.len]);
			else
				file_lost_pino(inode);
		}
	}
	set_page_dirty(ipage);
}

void f2fs_do_make_empty_dir(struct inode *inode, struct inode *parent,
					struct f2fs_dentry_ptr *d)
{
	struct fscrypt_str dot = FSTR_INIT(".", 1);
	struct fscrypt_str dotdot = FSTR_INIT("..", 2);

	/* update dirent of "." */
	f2fs_update_dentry(inode->i_ino, inode->i_mode, d, &dot, 0, 0);

	/* update dirent of ".." */
	f2fs_update_dentry(parent->i_ino, parent->i_mode, d, &dotdot, 0, 1);
}

static int make_empty_dir(struct inode *inode,
		struct inode *parent, struct page *page)
{
	struct page *dentry_page;
	struct f2fs_dentry_block *dentry_blk;
	struct f2fs_dentry_ptr d;

	if (f2fs_has_inline_dentry(inode))
		return f2fs_make_empty_inline_dir(inode, parent, page);

	dentry_page = f2fs_get_new_data_page(inode, page, 0, true);
	if (IS_ERR(dentry_page))
		return PTR_ERR(dentry_page);

	dentry_blk = page_address(dentry_page);

	make_dentry_ptr_block(NULL, &d, dentry_blk);
	f2fs_do_make_empty_dir(inode, parent, &d);

	set_page_dirty(dentry_page);
	f2fs_put_page(dentry_page, 1);
	return 0;
}

struct page *f2fs_init_inode_metadata(struct inode *inode, struct inode *dir,
			const struct f2fs_filename *fname, struct page *dpage)
{
	struct page *page;
	int err;

	if (is_inode_flag_set(inode, FI_NEW_INODE)) {
		page = f2fs_new_inode_page(inode);
		if (IS_ERR(page))
			return page;

		if (S_ISDIR(inode->i_mode)) {
			/* in order to handle error case */
			get_page(page);
			err = make_empty_dir(inode, dir, page);
			if (err) {
				lock_page(page);
				goto put_error;
			}
			put_page(page);
		}

		err = f2fs_init_acl(inode, dir, page, dpage);
		if (err)
			goto put_error;

		err = f2fs_init_security(inode, dir,
					 fname ? fname->usr_fname : NULL, page);
		if (err)
			goto put_error;

		if (IS_ENCRYPTED(inode)) {
			err = fscrypt_set_context(inode, page);
			if (err)
				goto put_error;
		}
	} else {
		page = f2fs_get_node_page(F2FS_I_SB(dir), inode->i_ino);
		if (IS_ERR(page))
			return page;
	}

	init_dent_inode(dir, inode, fname, page);

	/*
	 * This file should be checkpointed during fsync.
	 * We lost i_pino from now on.
	 */
	if (is_inode_flag_set(inode, FI_INC_LINK)) {
		if (!S_ISDIR(inode->i_mode))
			file_lost_pino(inode);
		/*
		 * If link the tmpfile to alias through linkat path,
		 * we should remove this inode from orphan list.
		 */
		if (inode->i_nlink == 0)
			f2fs_remove_orphan_inode(F2FS_I_SB(dir), inode->i_ino);
		f2fs_i_links_write(inode, true);
	}
	return page;

put_error:
	clear_nlink(inode);
	f2fs_update_inode(inode, page);
	f2fs_put_page(page, 1);
	return ERR_PTR(err);
}

void f2fs_update_parent_metadata(struct inode *dir, struct inode *inode,
						unsigned int current_depth)
{
	if (inode && is_inode_flag_set(inode, FI_NEW_INODE)) {
		if (S_ISDIR(inode->i_mode))
			f2fs_i_links_write(dir, true);
		clear_inode_flag(inode, FI_NEW_INODE);
	}
	dir->i_mtime = dir->i_ctime = current_time(dir);
	f2fs_mark_inode_dirty_sync(dir, false);

	if (F2FS_I(dir)->i_current_depth != current_depth)
		f2fs_i_depth_write(dir, current_depth);

	if (inode && is_inode_flag_set(inode, FI_INC_LINK))
		clear_inode_flag(inode, FI_INC_LINK);
}

int f2fs_room_for_filename(const void *bitmap, int slots, int max_slots)
{
	int bit_start = 0;
	int zero_start, zero_end;
next:
	zero_start = find_next_zero_bit_le(bitmap, max_slots, bit_start);
	if (zero_start >= max_slots)
		return max_slots;

	zero_end = find_next_bit_le(bitmap, max_slots, zero_start);
	if (zero_end - zero_start >= slots)
		return zero_start;

	bit_start = zero_end + 1;

	if (zero_end + 1 >= max_slots)
		return max_slots;
	goto next;
}

bool f2fs_has_enough_room(struct inode *dir, struct page *ipage,
			  const struct f2fs_filename *fname)
{
	struct f2fs_dentry_ptr d;
	unsigned int bit_pos;
	int slots = GET_DENTRY_SLOTS(fname->disk_name.len);

	make_dentry_ptr_inline(dir, &d, inline_data_addr(dir, ipage));

	bit_pos = f2fs_room_for_filename(d.bitmap, slots, d.max);

	return bit_pos < d.max;
}

void f2fs_update_dentry(nid_t ino, umode_t mode, struct f2fs_dentry_ptr *d,
			const struct fscrypt_str *name, f2fs_hash_t name_hash,
			unsigned int bit_pos)
{
	struct f2fs_dir_entry *de;
	int slots = GET_DENTRY_SLOTS(name->len);
	int i;

	de = &d->dentry[bit_pos];
	de->hash_code = name_hash;
	de->name_len = cpu_to_le16(name->len);
	memcpy(d->filename[bit_pos], name->name, name->len);
	de->ino = cpu_to_le32(ino);
	set_de_type(de, mode);
	for (i = 0; i < slots; i++) {
		__set_bit_le(bit_pos + i, (void *)d->bitmap);
		/* avoid wrong garbage data for readdir */
		if (i)
			(de + i)->name_len = 0;
	}
}

int f2fs_add_regular_entry(struct inode *dir, const struct f2fs_filename *fname,
			   struct inode *inode, nid_t ino, umode_t mode)
{
	unsigned int bit_pos;
	unsigned int level;
	unsigned int current_depth;
	unsigned long bidx, block;
	unsigned int nbucket, nblock;
	struct page *dentry_page = NULL;
	struct f2fs_dentry_block *dentry_blk = NULL;
	struct f2fs_dentry_ptr d;
	struct page *page = NULL;
	int slots, err = 0;

	level = 0;
	slots = GET_DENTRY_SLOTS(fname->disk_name.len);

	current_depth = F2FS_I(dir)->i_current_depth;
	if (F2FS_I(dir)->chash == fname->hash) {
		level = F2FS_I(dir)->clevel;
		F2FS_I(dir)->chash = 0;
	}

start:
	if (time_to_inject(F2FS_I_SB(dir), FAULT_DIR_DEPTH))
		return -ENOSPC;

	if (unlikely(current_depth == MAX_DIR_HASH_DEPTH))
		return -ENOSPC;

	/* Increase the depth, if required */
	if (level == current_depth)
		++current_depth;

	nbucket = dir_buckets(level, F2FS_I(dir)->i_dir_level);
	nblock = bucket_blocks(level);

	bidx = dir_block_index(level, F2FS_I(dir)->i_dir_level,
				(le32_to_cpu(fname->hash) % nbucket));

	for (block = bidx; block <= (bidx + nblock - 1); block++) {
		dentry_page = f2fs_get_new_data_page(dir, NULL, block, true);
		if (IS_ERR(dentry_page))
			return PTR_ERR(dentry_page);

		dentry_blk = page_address(dentry_page);
		bit_pos = f2fs_room_for_filename(&dentry_blk->dentry_bitmap,
						slots, NR_DENTRY_IN_BLOCK);
		if (bit_pos < NR_DENTRY_IN_BLOCK)
			goto add_dentry;

		f2fs_put_page(dentry_page, 1);
	}

	/* Move to next level to find the empty slot for new dentry */
	++level;
	goto start;
add_dentry:
	f2fs_wait_on_page_writeback(dentry_page, DATA, true, true);

	if (inode) {
		f2fs_down_write(&F2FS_I(inode)->i_sem);
		page = f2fs_init_inode_metadata(inode, dir, fname, NULL);
		if (IS_ERR(page)) {
			err = PTR_ERR(page);
			goto fail;
		}
	}

	make_dentry_ptr_block(NULL, &d, dentry_blk);
	f2fs_update_dentry(ino, mode, &d, &fname->disk_name, fname->hash,
			   bit_pos);

	set_page_dirty(dentry_page);

	if (inode) {
		f2fs_i_pino_write(inode, dir->i_ino);

		/* synchronize inode page's data from inode cache */
		if (is_inode_flag_set(inode, FI_NEW_INODE))
			f2fs_update_inode(inode, page);

		f2fs_put_page(page, 1);
	}

	f2fs_update_parent_metadata(dir, inode, current_depth);
fail:
	if (inode)
		f2fs_up_write(&F2FS_I(inode)->i_sem);

	f2fs_put_page(dentry_page, 1);

	return err;
}

int f2fs_add_dentry(struct inode *dir, const struct f2fs_filename *fname,
		    struct inode *inode, nid_t ino, umode_t mode)
{
	int err = -EAGAIN;

	if (f2fs_has_inline_dentry(dir))
		err = f2fs_add_inline_entry(dir, fname, inode, ino, mode);
	if (err == -EAGAIN)
		err = f2fs_add_regular_entry(dir, fname, inode, ino, mode);

	f2fs_update_time(F2FS_I_SB(dir), REQ_TIME);
	return err;
}

/*
 * Caller should grab and release a rwsem by calling f2fs_lock_op() and
 * f2fs_unlock_op().
 */
int f2fs_do_add_link(struct inode *dir, const struct qstr *name,
				struct inode *inode, nid_t ino, umode_t mode)
{
	struct f2fs_filename fname;
	struct page *page = NULL;
	struct f2fs_dir_entry *de = NULL;
	int err;

	err = f2fs_setup_filename(dir, name, 0, &fname);
	if (err)
		return err;

	/*
	 * An immature stackable filesystem shows a race condition between lookup
	 * and create. If we have same task when doing lookup and create, it's
	 * definitely fine as expected by VFS normally. Otherwise, let's just
	 * verify on-disk dentry one more time, which guarantees filesystem
	 * consistency more.
	 */
	if (current != F2FS_I(dir)->task) {
		de = __f2fs_find_entry(dir, &fname, &page);
		F2FS_I(dir)->task = NULL;
	}
	if (de) {
		f2fs_put_page(page, 0);
		err = -EEXIST;
	} else if (IS_ERR(page)) {
		err = PTR_ERR(page);
	} else {
		err = f2fs_add_dentry(dir, &fname, inode, ino, mode);
	}
	f2fs_free_filename(&fname);
	return err;
}

int f2fs_do_tmpfile(struct inode *inode, struct inode *dir)
{
	struct page *page;
	int err = 0;

	f2fs_down_write(&F2FS_I(inode)->i_sem);
	page = f2fs_init_inode_metadata(inode, dir, NULL, NULL);
	if (IS_ERR(page)) {
		err = PTR_ERR(page);
		goto fail;
	}
	f2fs_put_page(page, 1);

	clear_inode_flag(inode, FI_NEW_INODE);
	f2fs_update_time(F2FS_I_SB(inode), REQ_TIME);
fail:
	f2fs_up_write(&F2FS_I(inode)->i_sem);
	return err;
}

void f2fs_drop_nlink(struct inode *dir, struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);

	f2fs_down_write(&F2FS_I(inode)->i_sem);

	if (S_ISDIR(inode->i_mode))
		f2fs_i_links_write(dir, false);
	inode->i_ctime = current_time(inode);

	f2fs_i_links_write(inode, false);
	if (S_ISDIR(inode->i_mode)) {
		f2fs_i_links_write(inode, false);
		f2fs_i_size_write(inode, 0);
	}
	f2fs_up_write(&F2FS_I(inode)->i_sem);

	if (inode->i_nlink == 0)
		f2fs_add_orphan_inode(inode);
	else
		f2fs_release_orphan_inode(sbi);
}

/*
 * It only removes the dentry from the dentry page, corresponding name
 * entry in name page does not need to be touched during deletion.
 */
void f2fs_delete_entry(struct f2fs_dir_entry *dentry, struct page *page,
					struct inode *dir, struct inode *inode)
{
	struct	f2fs_dentry_block *dentry_blk;
	unsigned int bit_pos;
	int slots = GET_DENTRY_SLOTS(le16_to_cpu(dentry->name_len));
	int i;

	f2fs_update_time(F2FS_I_SB(dir), REQ_TIME);

	if (F2FS_OPTION(F2FS_I_SB(dir)).fsync_mode == FSYNC_MODE_STRICT)
		f2fs_add_ino_entry(F2FS_I_SB(dir), dir->i_ino, TRANS_DIR_INO);

	if (f2fs_has_inline_dentry(dir))
		return f2fs_delete_inline_entry(dentry, page, dir, inode);

	lock_page(page);
	f2fs_wait_on_page_writeback(page, DATA, true, true);

	dentry_blk = page_address(page);
	bit_pos = dentry - dentry_blk->dentry;
	for (i = 0; i < slots; i++)
		__clear_bit_le(bit_pos + i, &dentry_blk->dentry_bitmap);

	/* Let's check and deallocate this dentry page */
	bit_pos = find_next_bit_le(&dentry_blk->dentry_bitmap,
			NR_DENTRY_IN_BLOCK,
			0);
	set_page_dirty(page);

	if (bit_pos == NR_DENTRY_IN_BLOCK &&
		!f2fs_truncate_hole(dir, page->index, page->index + 1)) {
		f2fs_clear_page_cache_dirty_tag(page);
		clear_page_dirty_for_io(page);
		ClearPageUptodate(page);

		clear_page_private_gcing(page);

		inode_dec_dirty_pages(dir);
		f2fs_remove_dirty_inode(dir);

		detach_page_private(page);
		set_page_private(page, 0);
	}
	f2fs_put_page(page, 1);

	dir->i_ctime = dir->i_mtime = current_time(dir);
	f2fs_mark_inode_dirty_sync(dir, false);

	if (inode)
		f2fs_drop_nlink(dir, inode);
}

bool f2fs_empty_dir(struct inode *dir)
{
	unsigned long bidx = 0;
	struct page *dentry_page;
	unsigned int bit_pos;
	struct f2fs_dentry_block *dentry_blk;
	unsigned long nblock = dir_blocks(dir);

	if (f2fs_has_inline_dentry(dir))
		return f2fs_empty_inline_dir(dir);

	while (bidx < nblock) {
		pgoff_t next_pgofs;

		dentry_page = f2fs_find_data_page(dir, bidx, &next_pgofs);
		if (IS_ERR(dentry_page)) {
			if (PTR_ERR(dentry_page) == -ENOENT) {
				bidx = next_pgofs;
				continue;
			} else {
				return false;
			}
		}

		dentry_blk = page_address(dentry_page);
		if (bidx == 0)
			bit_pos = 2;
		else
			bit_pos = 0;
		bit_pos = find_next_bit_le(&dentry_blk->dentry_bitmap,
						NR_DENTRY_IN_BLOCK,
						bit_pos);

		f2fs_put_page(dentry_page, 0);

		if (bit_pos < NR_DENTRY_IN_BLOCK)
			return false;

		bidx++;
	}
	return true;
}

int f2fs_fill_dentries(struct dir_context *ctx, struct f2fs_dentry_ptr *d,
			unsigned int start_pos, struct fscrypt_str *fstr)
{
	unsigned char d_type = DT_UNKNOWN;
	unsigned int bit_pos;
	struct f2fs_dir_entry *de = NULL;
	struct fscrypt_str de_name = FSTR_INIT(NULL, 0);
	struct f2fs_sb_info *sbi = F2FS_I_SB(d->inode);
	struct blk_plug plug;
	bool readdir_ra = sbi->readdir_ra;
	bool found_valid_dirent = false;
	int err = 0;

	bit_pos = ((unsigned long)ctx->pos % d->max);

	if (readdir_ra)
		blk_start_plug(&plug);

	while (bit_pos < d->max) {
		bit_pos = find_next_bit_le(d->bitmap, d->max, bit_pos);
		if (bit_pos >= d->max)
			break;

		de = &d->dentry[bit_pos];
		if (de->name_len == 0) {
			if (found_valid_dirent || !bit_pos) {
				printk_ratelimited(
					"%sF2FS-fs (%s): invalid namelen(0), ino:%u, run fsck to fix.",
					KERN_WARNING, sbi->sb->s_id,
					le32_to_cpu(de->ino));
				set_sbi_flag(sbi, SBI_NEED_FSCK);
			}
			bit_pos++;
			ctx->pos = start_pos + bit_pos;
			continue;
		}

		d_type = f2fs_get_de_type(de);

		de_name.name = d->filename[bit_pos];
		de_name.len = le16_to_cpu(de->name_len);

		/* check memory boundary before moving forward */
		bit_pos += GET_DENTRY_SLOTS(le16_to_cpu(de->name_len));
		if (unlikely(bit_pos > d->max ||
				le16_to_cpu(de->name_len) > F2FS_NAME_LEN)) {
			f2fs_warn(sbi, "%s: corrupted namelen=%d, run fsck to fix.",
				  __func__, le16_to_cpu(de->name_len));
			set_sbi_flag(sbi, SBI_NEED_FSCK);
			err = -EFSCORRUPTED;
			f2fs_handle_error(sbi, ERROR_CORRUPTED_DIRENT);
			goto out;
		}

		if (IS_ENCRYPTED(d->inode)) {
			int save_len = fstr->len;

			err = fscrypt_fname_disk_to_usr(d->inode,
						(u32)le32_to_cpu(de->hash_code),
						0, &de_name, fstr);
			if (err)
				goto out;

			de_name = *fstr;
			fstr->len = save_len;
		}

		if (!dir_emit(ctx, de_name.name, de_name.len,
					le32_to_cpu(de->ino), d_type)) {
			err = 1;
			goto out;
		}

		if (readdir_ra)
			f2fs_ra_node_page(sbi, le32_to_cpu(de->ino));

		ctx->pos = start_pos + bit_pos;
		found_valid_dirent = true;
	}
out:
	if (readdir_ra)
		blk_finish_plug(&plug);
	return err;
}

static int f2fs_readdir(struct file *file, struct dir_context *ctx)
{
	struct inode *inode = file_inode(file);
	unsigned long npages = dir_blocks(inode);
	struct f2fs_dentry_block *dentry_blk = NULL;
	struct page *dentry_page = NULL;
	struct file_ra_state *ra = &file->f_ra;
	loff_t start_pos = ctx->pos;
	unsigned int n = ((unsigned long)ctx->pos / NR_DENTRY_IN_BLOCK);
	struct f2fs_dentry_ptr d;
	struct fscrypt_str fstr = FSTR_INIT(NULL, 0);
	int err = 0;

	if (IS_ENCRYPTED(inode)) {
		err = fscrypt_prepare_readdir(inode);
		if (err)
			goto out;

		err = fscrypt_fname_alloc_buffer(F2FS_NAME_LEN, &fstr);
		if (err < 0)
			goto out;
	}

	if (f2fs_has_inline_dentry(inode)) {
		err = f2fs_read_inline_dir(file, ctx, &fstr);
		goto out_free;
	}

	for (; n < npages; ctx->pos = n * NR_DENTRY_IN_BLOCK) {
		pgoff_t next_pgofs;

		/* allow readdir() to be interrupted */
		if (fatal_signal_pending(current)) {
			err = -ERESTARTSYS;
			goto out_free;
		}
		cond_resched();

		/* readahead for multi pages of dir */
		if (npages - n > 1 && !ra_has_index(ra, n))
			page_cache_sync_readahead(inode->i_mapping, ra, file, n,
				min(npages - n, (pgoff_t)MAX_DIR_RA_PAGES));

		dentry_page = f2fs_find_data_page(inode, n, &next_pgofs);
		if (IS_ERR(dentry_page)) {
			err = PTR_ERR(dentry_page);
			if (err == -ENOENT) {
				err = 0;
				n = next_pgofs;
				continue;
			} else {
				goto out_free;
			}
		}

		dentry_blk = page_address(dentry_page);

		make_dentry_ptr_block(inode, &d, dentry_blk);

		err = f2fs_fill_dentries(ctx, &d,
				n * NR_DENTRY_IN_BLOCK, &fstr);
		if (err) {
			f2fs_put_page(dentry_page, 0);
			break;
		}

		f2fs_put_page(dentry_page, 0);

		n++;
	}
out_free:
	fscrypt_fname_free_buffer(&fstr);
out:
	trace_f2fs_readdir(inode, start_pos, ctx->pos, err);
	return err < 0 ? err : 0;
}

const struct file_operations f2fs_dir_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= f2fs_readdir,
	.fsync		= f2fs_sync_file,
	.unlocked_ioctl	= f2fs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = f2fs_compat_ioctl,
#endif
};
