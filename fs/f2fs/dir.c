// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/dir.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */
#include <linux/unaligned.h>
#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/sched/signal.h>
#include <linux/unicode.h>
#include "f2fs.h"
#include "node.h"
#include "acl.h"
#include "xattr.h"
#include <trace/events/f2fs.h>

static inline bool f2fs_should_fallback_to_linear(struct inode *dir)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);

	switch (F2FS_OPTION(sbi).lookup_mode) {
	case LOOKUP_PERF:
		return false;
	case LOOKUP_COMPAT:
		return true;
	case LOOKUP_AUTO:
		return !sb_no_casefold_compat_fallback(sbi->sb);
	}
	return false;
}

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
		return BIT(level + dir_level);
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

#if IS_ENABLED(CONFIG_UNICODE)
/* If @dir is casefolded, initialize @fname->cf_name from @fname->usr_fname. */
int f2fs_init_casefolded_name(const struct inode *dir,
			      struct f2fs_filename *fname)
{
	struct super_block *sb = dir->i_sb;
	unsigned char *buf;
	int len;

	if (IS_CASEFOLDED(dir) &&
	    !is_dot_dotdot(fname->usr_fname->name, fname->usr_fname->len)) {
		buf = f2fs_kmem_cache_alloc(f2fs_cf_name_slab,
					    GFP_NOFS, false, F2FS_SB(sb));
		if (!buf)
			return -ENOMEM;

		len = utf8_casefold(sb->s_encoding, fname->usr_fname,
				    buf, F2FS_NAME_LEN);
		if (len <= 0) {
			kmem_cache_free(f2fs_cf_name_slab, buf);
			if (sb_has_strict_encoding(sb))
				return -EINVAL;
			/* fall back to treating name as opaque byte sequence */
			return 0;
		}
		fname->cf_name.name = buf;
		fname->cf_name.len = len;
	}

	return 0;
}

void f2fs_free_casefolded_name(struct f2fs_filename *fname)
{
	unsigned char *buf = (unsigned char *)fname->cf_name.name;

	if (buf) {
		kmem_cache_free(f2fs_cf_name_slab, buf);
		fname->cf_name.name = NULL;
	}
}
#endif /* CONFIG_UNICODE */

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
	f2fs_free_casefolded_name(fname);
}

static unsigned long dir_block_index(unsigned int level,
				int dir_level, unsigned int idx)
{
	unsigned long i;
	unsigned long bidx = 0;

	for (i = 0; i < level; i++)
		bidx += mul_u32_u32(dir_buckets(i, dir_level),
				    bucket_blocks(i));
	bidx += idx * bucket_blocks(level);
	return bidx;
}

static struct f2fs_dir_entry *find_in_block(struct inode *dir,
				struct folio *dentry_folio,
				const struct f2fs_filename *fname,
				int *max_slots,
				bool use_hash)
{
	struct f2fs_dentry_block *dentry_blk;
	struct f2fs_dentry_ptr d;

	dentry_blk = folio_address(dentry_folio);

	make_dentry_ptr_block(dir, &d, dentry_blk);
	return f2fs_find_target_dentry(&d, fname, max_slots, use_hash);
}

static inline int f2fs_match_name(const struct inode *dir,
				   const struct f2fs_filename *fname,
				   const u8 *de_name, u32 de_name_len)
{
	struct fscrypt_name f;

#if IS_ENABLED(CONFIG_UNICODE)
	if (fname->cf_name.name)
		return generic_ci_match(dir, fname->usr_fname,
					&fname->cf_name,
					de_name, de_name_len);

#endif
	f.usr_fname = fname->usr_fname;
	f.disk_name = fname->disk_name;
#ifdef CONFIG_FS_ENCRYPTION
	f.crypto_buf = fname->crypto_buf;
#endif
	return fscrypt_match_name(&f, de_name, de_name_len);
}

struct f2fs_dir_entry *f2fs_find_target_dentry(const struct f2fs_dentry_ptr *d,
			const struct f2fs_filename *fname, int *max_slots,
			bool use_hash)
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

		if (!use_hash || de->hash_code == fname->hash) {
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
					struct folio **res_folio,
					bool use_hash)
{
	int s = GET_DENTRY_SLOTS(fname->disk_name.len);
	unsigned int nbucket, nblock;
	unsigned int bidx, end_block, bucket_no;
	struct f2fs_dir_entry *de = NULL;
	pgoff_t next_pgofs;
	bool room = false;
	int max_slots;

	nbucket = dir_buckets(level, F2FS_I(dir)->i_dir_level);
	nblock = bucket_blocks(level);

	bucket_no = use_hash ? le32_to_cpu(fname->hash) % nbucket : 0;

start_find_bucket:
	bidx = dir_block_index(level, F2FS_I(dir)->i_dir_level,
			       bucket_no);
	end_block = bidx + nblock;

	while (bidx < end_block) {
		/* no need to allocate new dentry pages to all the indices */
		struct folio *dentry_folio;
		dentry_folio = f2fs_find_data_folio(dir, bidx, &next_pgofs);
		if (IS_ERR(dentry_folio)) {
			if (PTR_ERR(dentry_folio) == -ENOENT) {
				room = true;
				bidx = next_pgofs;
				continue;
			} else {
				*res_folio = dentry_folio;
				break;
			}
		}

		de = find_in_block(dir, dentry_folio, fname, &max_slots, use_hash);
		if (IS_ERR(de)) {
			*res_folio = ERR_CAST(de);
			de = NULL;
			break;
		} else if (de) {
			*res_folio = dentry_folio;
			break;
		}

		if (max_slots >= s)
			room = true;
		f2fs_folio_put(dentry_folio, false);

		bidx++;
	}

	if (de)
		return de;

	if (likely(use_hash)) {
		if (room && F2FS_I(dir)->chash != fname->hash) {
			F2FS_I(dir)->chash = fname->hash;
			F2FS_I(dir)->clevel = level;
		}
	} else if (++bucket_no < nbucket) {
		goto start_find_bucket;
	}
	return NULL;
}

struct f2fs_dir_entry *__f2fs_find_entry(struct inode *dir,
					 const struct f2fs_filename *fname,
					 struct folio **res_folio)
{
	unsigned long npages = dir_blocks(dir);
	struct f2fs_dir_entry *de = NULL;
	unsigned int max_depth;
	unsigned int level;
	bool use_hash = true;

	*res_folio = NULL;

#if IS_ENABLED(CONFIG_UNICODE)
start_find_entry:
#endif
	if (f2fs_has_inline_dentry(dir)) {
		de = f2fs_find_in_inline_dir(dir, fname, res_folio, use_hash);
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
		de = find_in_level(dir, level, fname, res_folio, use_hash);
		if (de || IS_ERR(*res_folio))
			break;
	}

out:
#if IS_ENABLED(CONFIG_UNICODE)
	if (f2fs_should_fallback_to_linear(dir) &&
		IS_CASEFOLDED(dir) && !de && use_hash) {
		use_hash = false;
		goto start_find_entry;
	}
#endif
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
			const struct qstr *child, struct folio **res_folio)
{
	struct f2fs_dir_entry *de = NULL;
	struct f2fs_filename fname;
	int err;

	err = f2fs_setup_filename(dir, child, 1, &fname);
	if (err) {
		if (err == -ENOENT)
			*res_folio = NULL;
		else
			*res_folio = ERR_PTR(err);
		return NULL;
	}

	de = __f2fs_find_entry(dir, &fname, res_folio);

	f2fs_free_filename(&fname);
	return de;
}

struct f2fs_dir_entry *f2fs_parent_dir(struct inode *dir, struct folio **f)
{
	return f2fs_find_entry(dir, &dotdot_name, f);
}

ino_t f2fs_inode_by_name(struct inode *dir, const struct qstr *qstr,
							struct folio **folio)
{
	ino_t res = 0;
	struct f2fs_dir_entry *de;

	de = f2fs_find_entry(dir, qstr, folio);
	if (de) {
		res = le32_to_cpu(de->ino);
		f2fs_folio_put(*folio, false);
	}

	return res;
}

void f2fs_set_link(struct inode *dir, struct f2fs_dir_entry *de,
		struct folio *folio, struct inode *inode)
{
	enum page_type type = f2fs_has_inline_dentry(dir) ? NODE : DATA;

	folio_lock(folio);
	f2fs_folio_wait_writeback(folio, type, true, true);
	de->ino = cpu_to_le32(inode->i_ino);
	de->file_type = fs_umode_to_ftype(inode->i_mode);
	folio_mark_dirty(folio);

	inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
	f2fs_mark_inode_dirty_sync(dir, false);
	f2fs_folio_put(folio, true);
}

static void init_dent_inode(struct inode *dir, struct inode *inode,
			    const struct f2fs_filename *fname,
			    struct folio *ifolio)
{
	struct f2fs_inode *ri;

	if (!fname) /* tmpfile case? */
		return;

	f2fs_folio_wait_writeback(ifolio, NODE, true, true);

	/* copy name info. to this inode folio */
	ri = F2FS_INODE(ifolio);
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
	folio_mark_dirty(ifolio);
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
		struct inode *parent, struct folio *folio)
{
	struct folio *dentry_folio;
	struct f2fs_dentry_block *dentry_blk;
	struct f2fs_dentry_ptr d;

	if (f2fs_has_inline_dentry(inode))
		return f2fs_make_empty_inline_dir(inode, parent, folio);

	dentry_folio = f2fs_get_new_data_folio(inode, folio, 0, true);
	if (IS_ERR(dentry_folio))
		return PTR_ERR(dentry_folio);

	dentry_blk = folio_address(dentry_folio);

	make_dentry_ptr_block(NULL, &d, dentry_blk);
	f2fs_do_make_empty_dir(inode, parent, &d);

	folio_mark_dirty(dentry_folio);
	f2fs_folio_put(dentry_folio, true);
	return 0;
}

struct folio *f2fs_init_inode_metadata(struct inode *inode, struct inode *dir,
		const struct f2fs_filename *fname, struct folio *dfolio)
{
	struct folio *folio;
	int err;

	if (is_inode_flag_set(inode, FI_NEW_INODE)) {
		folio = f2fs_new_inode_folio(inode);
		if (IS_ERR(folio))
			return folio;

		if (S_ISDIR(inode->i_mode)) {
			/* in order to handle error case */
			folio_get(folio);
			err = make_empty_dir(inode, dir, folio);
			if (err) {
				folio_lock(folio);
				goto put_error;
			}
			folio_put(folio);
		}

		err = f2fs_init_acl(inode, dir, folio, dfolio);
		if (err)
			goto put_error;

		err = f2fs_init_security(inode, dir,
					 fname ? fname->usr_fname : NULL,
					 folio);
		if (err)
			goto put_error;

		if (IS_ENCRYPTED(inode)) {
			err = fscrypt_set_context(inode, folio);
			if (err)
				goto put_error;
		}
	} else {
		folio = f2fs_get_inode_folio(F2FS_I_SB(dir), inode->i_ino);
		if (IS_ERR(folio))
			return folio;
	}

	init_dent_inode(dir, inode, fname, folio);

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
	return folio;

put_error:
	clear_nlink(inode);
	f2fs_update_inode(inode, folio);
	f2fs_folio_put(folio, true);
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
	inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
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

bool f2fs_has_enough_room(struct inode *dir, struct folio *ifolio,
			  const struct f2fs_filename *fname)
{
	struct f2fs_dentry_ptr d;
	unsigned int bit_pos;
	int slots = GET_DENTRY_SLOTS(fname->disk_name.len);

	make_dentry_ptr_inline(dir, &d, inline_data_addr(dir, ifolio));

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
	de->file_type = fs_umode_to_ftype(mode);
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
	struct folio *dentry_folio = NULL;
	struct f2fs_dentry_block *dentry_blk = NULL;
	struct f2fs_dentry_ptr d;
	struct folio *folio = NULL;
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
		dentry_folio = f2fs_get_new_data_folio(dir, NULL, block, true);
		if (IS_ERR(dentry_folio))
			return PTR_ERR(dentry_folio);

		dentry_blk = folio_address(dentry_folio);
		bit_pos = f2fs_room_for_filename(&dentry_blk->dentry_bitmap,
						slots, NR_DENTRY_IN_BLOCK);
		if (bit_pos < NR_DENTRY_IN_BLOCK)
			goto add_dentry;

		f2fs_folio_put(dentry_folio, true);
	}

	/* Move to next level to find the empty slot for new dentry */
	++level;
	goto start;
add_dentry:
	f2fs_folio_wait_writeback(dentry_folio, DATA, true, true);

	if (inode) {
		f2fs_down_write(&F2FS_I(inode)->i_sem);
		folio = f2fs_init_inode_metadata(inode, dir, fname, NULL);
		if (IS_ERR(folio)) {
			err = PTR_ERR(folio);
			goto fail;
		}
	}

	make_dentry_ptr_block(NULL, &d, dentry_blk);
	f2fs_update_dentry(ino, mode, &d, &fname->disk_name, fname->hash,
			   bit_pos);

	folio_mark_dirty(dentry_folio);

	if (inode) {
		f2fs_i_pino_write(inode, dir->i_ino);

		/* synchronize inode page's data from inode cache */
		if (is_inode_flag_set(inode, FI_NEW_INODE))
			f2fs_update_inode(inode, folio);

		f2fs_folio_put(folio, true);
	}

	f2fs_update_parent_metadata(dir, inode, current_depth);
fail:
	if (inode)
		f2fs_up_write(&F2FS_I(inode)->i_sem);

	f2fs_folio_put(dentry_folio, true);

	return err;
}

int f2fs_add_dentry(struct inode *dir, const struct f2fs_filename *fname,
		    struct inode *inode, nid_t ino, umode_t mode)
{
	int err = -EAGAIN;

	if (f2fs_has_inline_dentry(dir)) {
		/*
		 * Should get i_xattr_sem to keep the lock order:
		 * i_xattr_sem -> inode_page lock used by f2fs_setxattr.
		 */
		f2fs_down_read(&F2FS_I(dir)->i_xattr_sem);
		err = f2fs_add_inline_entry(dir, fname, inode, ino, mode);
		f2fs_up_read(&F2FS_I(dir)->i_xattr_sem);
	}
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
	struct folio *folio = NULL;
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
		de = __f2fs_find_entry(dir, &fname, &folio);
		F2FS_I(dir)->task = NULL;
	}
	if (de) {
		f2fs_folio_put(folio, false);
		err = -EEXIST;
	} else if (IS_ERR(folio)) {
		err = PTR_ERR(folio);
	} else {
		err = f2fs_add_dentry(dir, &fname, inode, ino, mode);
	}
	f2fs_free_filename(&fname);
	return err;
}

int f2fs_do_tmpfile(struct inode *inode, struct inode *dir,
					struct f2fs_filename *fname)
{
	struct folio *folio;
	int err = 0;

	f2fs_down_write(&F2FS_I(inode)->i_sem);
	folio = f2fs_init_inode_metadata(inode, dir, fname, NULL);
	if (IS_ERR(folio)) {
		err = PTR_ERR(folio);
		goto fail;
	}
	f2fs_folio_put(folio, true);

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
	inode_set_ctime_current(inode);

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
void f2fs_delete_entry(struct f2fs_dir_entry *dentry, struct folio *folio,
					struct inode *dir, struct inode *inode)
{
	struct f2fs_dentry_block *dentry_blk;
	unsigned int bit_pos;
	int slots = GET_DENTRY_SLOTS(le16_to_cpu(dentry->name_len));
	pgoff_t index = folio->index;
	int i;

	f2fs_update_time(F2FS_I_SB(dir), REQ_TIME);

	if (F2FS_OPTION(F2FS_I_SB(dir)).fsync_mode == FSYNC_MODE_STRICT)
		f2fs_add_ino_entry(F2FS_I_SB(dir), dir->i_ino, TRANS_DIR_INO);

	if (f2fs_has_inline_dentry(dir))
		return f2fs_delete_inline_entry(dentry, folio, dir, inode);

	folio_lock(folio);
	f2fs_folio_wait_writeback(folio, DATA, true, true);

	dentry_blk = folio_address(folio);
	bit_pos = dentry - dentry_blk->dentry;
	for (i = 0; i < slots; i++)
		__clear_bit_le(bit_pos + i, &dentry_blk->dentry_bitmap);

	/* Let's check and deallocate this dentry page */
	bit_pos = find_next_bit_le(&dentry_blk->dentry_bitmap,
			NR_DENTRY_IN_BLOCK,
			0);
	folio_mark_dirty(folio);

	if (bit_pos == NR_DENTRY_IN_BLOCK &&
		!f2fs_truncate_hole(dir, index, index + 1)) {
		f2fs_clear_page_cache_dirty_tag(folio);
		folio_clear_dirty_for_io(folio);
		folio_clear_uptodate(folio);
		folio_detach_private(folio);

		inode_dec_dirty_pages(dir);
		f2fs_remove_dirty_inode(dir);
	}
	f2fs_folio_put(folio, true);

	inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
	f2fs_mark_inode_dirty_sync(dir, false);

	if (inode)
		f2fs_drop_nlink(dir, inode);
}

bool f2fs_empty_dir(struct inode *dir)
{
	unsigned long bidx = 0;
	unsigned int bit_pos;
	struct f2fs_dentry_block *dentry_blk;
	unsigned long nblock = dir_blocks(dir);

	if (f2fs_has_inline_dentry(dir))
		return f2fs_empty_inline_dir(dir);

	while (bidx < nblock) {
		pgoff_t next_pgofs;
		struct folio *dentry_folio;

		dentry_folio = f2fs_find_data_folio(dir, bidx, &next_pgofs);
		if (IS_ERR(dentry_folio)) {
			if (PTR_ERR(dentry_folio) == -ENOENT) {
				bidx = next_pgofs;
				continue;
			} else {
				return false;
			}
		}

		dentry_blk = folio_address(dentry_folio);
		if (bidx == 0)
			bit_pos = 2;
		else
			bit_pos = 0;
		bit_pos = find_next_bit_le(&dentry_blk->dentry_bitmap,
						NR_DENTRY_IN_BLOCK,
						bit_pos);

		f2fs_folio_put(dentry_folio, false);

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
				f2fs_warn_ratelimited(sbi,
					"invalid namelen(0), ino:%u, run fsck to fix.",
					le32_to_cpu(de->ino));
				set_sbi_flag(sbi, SBI_NEED_FSCK);
			}
			bit_pos++;
			ctx->pos = start_pos + bit_pos;
			continue;
		}

		d_type = fs_ftype_to_dtype(de->file_type);

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
		struct folio *dentry_folio;
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

		dentry_folio = f2fs_find_data_folio(inode, n, &next_pgofs);
		if (IS_ERR(dentry_folio)) {
			err = PTR_ERR(dentry_folio);
			if (err == -ENOENT) {
				err = 0;
				n = next_pgofs;
				continue;
			} else {
				goto out_free;
			}
		}

		dentry_blk = folio_address(dentry_folio);

		make_dentry_ptr_block(inode, &d, dentry_blk);

		err = f2fs_fill_dentries(ctx, &d,
				n * NR_DENTRY_IN_BLOCK, &fstr);
		f2fs_folio_put(dentry_folio, false);
		if (err)
			break;

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
