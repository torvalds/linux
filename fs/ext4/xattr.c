// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/ext4/xattr.c
 *
 * Copyright (C) 2001-2003 Andreas Gruenbacher, <agruen@suse.de>
 *
 * Fix by Harrison Xing <harrison@mountainviewdata.com>.
 * Ext4 code with a lot of help from Eric Jarman <ejarman@acm.org>.
 * Extended attributes for symlinks and special files added per
 *  suggestion of Luka Renko <luka.renko@hermes.si>.
 * xattr consolidation Copyright (c) 2004 James Morris <jmorris@redhat.com>,
 *  Red Hat Inc.
 * ea-in-iyesde support by Alex Tomas <alex@clusterfs.com> aka bzzz
 *  and Andreas Gruenbacher <agruen@suse.de>.
 */

/*
 * Extended attributes are stored directly in iyesdes (on file systems with
 * iyesdes bigger than 128 bytes) and on additional disk blocks. The i_file_acl
 * field contains the block number if an iyesde uses an additional block. All
 * attributes must fit in the iyesde and one additional block. Blocks that
 * contain the identical set of attributes may be shared among several iyesdes.
 * Identical blocks are detected by keeping a cache of blocks that have
 * recently been accessed.
 *
 * The attributes in iyesdes and on blocks have a different header; the entries
 * are stored in the same format:
 *
 *   +------------------+
 *   | header           |
 *   | entry 1          | |
 *   | entry 2          | | growing downwards
 *   | entry 3          | v
 *   | four null bytes  |
 *   | . . .            |
 *   | value 1          | ^
 *   | value 3          | | growing upwards
 *   | value 2          | |
 *   +------------------+
 *
 * The header is followed by multiple entry descriptors. In disk blocks, the
 * entry descriptors are kept sorted. In iyesdes, they are unsorted. The
 * attribute values are aligned to the end of the block in yes specific order.
 *
 * Locking strategy
 * ----------------
 * EXT4_I(iyesde)->i_file_acl is protected by EXT4_I(iyesde)->xattr_sem.
 * EA blocks are only changed if they are exclusive to an iyesde, so
 * holding xattr_sem also means that yesthing but the EA block's reference
 * count can change. Multiple writers to the same block are synchronized
 * by the buffer lock.
 */

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/mbcache.h>
#include <linux/quotaops.h>
#include <linux/iversion.h>
#include "ext4_jbd2.h"
#include "ext4.h"
#include "xattr.h"
#include "acl.h"

#ifdef EXT4_XATTR_DEBUG
# define ea_idebug(iyesde, fmt, ...)					\
	printk(KERN_DEBUG "iyesde %s:%lu: " fmt "\n",			\
	       iyesde->i_sb->s_id, iyesde->i_iyes, ##__VA_ARGS__)
# define ea_bdebug(bh, fmt, ...)					\
	printk(KERN_DEBUG "block %pg:%lu: " fmt "\n",			\
	       bh->b_bdev, (unsigned long)bh->b_blocknr, ##__VA_ARGS__)
#else
# define ea_idebug(iyesde, fmt, ...)	yes_printk(fmt, ##__VA_ARGS__)
# define ea_bdebug(bh, fmt, ...)	yes_printk(fmt, ##__VA_ARGS__)
#endif

static void ext4_xattr_block_cache_insert(struct mb_cache *,
					  struct buffer_head *);
static struct buffer_head *
ext4_xattr_block_cache_find(struct iyesde *, struct ext4_xattr_header *,
			    struct mb_cache_entry **);
static __le32 ext4_xattr_hash_entry(char *name, size_t name_len, __le32 *value,
				    size_t value_count);
static void ext4_xattr_rehash(struct ext4_xattr_header *);

static const struct xattr_handler * const ext4_xattr_handler_map[] = {
	[EXT4_XATTR_INDEX_USER]		     = &ext4_xattr_user_handler,
#ifdef CONFIG_EXT4_FS_POSIX_ACL
	[EXT4_XATTR_INDEX_POSIX_ACL_ACCESS]  = &posix_acl_access_xattr_handler,
	[EXT4_XATTR_INDEX_POSIX_ACL_DEFAULT] = &posix_acl_default_xattr_handler,
#endif
	[EXT4_XATTR_INDEX_TRUSTED]	     = &ext4_xattr_trusted_handler,
#ifdef CONFIG_EXT4_FS_SECURITY
	[EXT4_XATTR_INDEX_SECURITY]	     = &ext4_xattr_security_handler,
#endif
};

const struct xattr_handler *ext4_xattr_handlers[] = {
	&ext4_xattr_user_handler,
	&ext4_xattr_trusted_handler,
#ifdef CONFIG_EXT4_FS_POSIX_ACL
	&posix_acl_access_xattr_handler,
	&posix_acl_default_xattr_handler,
#endif
#ifdef CONFIG_EXT4_FS_SECURITY
	&ext4_xattr_security_handler,
#endif
	NULL
};

#define EA_BLOCK_CACHE(iyesde)	(((struct ext4_sb_info *) \
				iyesde->i_sb->s_fs_info)->s_ea_block_cache)

#define EA_INODE_CACHE(iyesde)	(((struct ext4_sb_info *) \
				iyesde->i_sb->s_fs_info)->s_ea_iyesde_cache)

static int
ext4_expand_iyesde_array(struct ext4_xattr_iyesde_array **ea_iyesde_array,
			struct iyesde *iyesde);

#ifdef CONFIG_LOCKDEP
void ext4_xattr_iyesde_set_class(struct iyesde *ea_iyesde)
{
	lockdep_set_subclass(&ea_iyesde->i_rwsem, 1);
}
#endif

static __le32 ext4_xattr_block_csum(struct iyesde *iyesde,
				    sector_t block_nr,
				    struct ext4_xattr_header *hdr)
{
	struct ext4_sb_info *sbi = EXT4_SB(iyesde->i_sb);
	__u32 csum;
	__le64 dsk_block_nr = cpu_to_le64(block_nr);
	__u32 dummy_csum = 0;
	int offset = offsetof(struct ext4_xattr_header, h_checksum);

	csum = ext4_chksum(sbi, sbi->s_csum_seed, (__u8 *)&dsk_block_nr,
			   sizeof(dsk_block_nr));
	csum = ext4_chksum(sbi, csum, (__u8 *)hdr, offset);
	csum = ext4_chksum(sbi, csum, (__u8 *)&dummy_csum, sizeof(dummy_csum));
	offset += sizeof(dummy_csum);
	csum = ext4_chksum(sbi, csum, (__u8 *)hdr + offset,
			   EXT4_BLOCK_SIZE(iyesde->i_sb) - offset);

	return cpu_to_le32(csum);
}

static int ext4_xattr_block_csum_verify(struct iyesde *iyesde,
					struct buffer_head *bh)
{
	struct ext4_xattr_header *hdr = BHDR(bh);
	int ret = 1;

	if (ext4_has_metadata_csum(iyesde->i_sb)) {
		lock_buffer(bh);
		ret = (hdr->h_checksum == ext4_xattr_block_csum(iyesde,
							bh->b_blocknr, hdr));
		unlock_buffer(bh);
	}
	return ret;
}

static void ext4_xattr_block_csum_set(struct iyesde *iyesde,
				      struct buffer_head *bh)
{
	if (ext4_has_metadata_csum(iyesde->i_sb))
		BHDR(bh)->h_checksum = ext4_xattr_block_csum(iyesde,
						bh->b_blocknr, BHDR(bh));
}

static inline const struct xattr_handler *
ext4_xattr_handler(int name_index)
{
	const struct xattr_handler *handler = NULL;

	if (name_index > 0 && name_index < ARRAY_SIZE(ext4_xattr_handler_map))
		handler = ext4_xattr_handler_map[name_index];
	return handler;
}

static int
ext4_xattr_check_entries(struct ext4_xattr_entry *entry, void *end,
			 void *value_start)
{
	struct ext4_xattr_entry *e = entry;

	/* Find the end of the names list */
	while (!IS_LAST_ENTRY(e)) {
		struct ext4_xattr_entry *next = EXT4_XATTR_NEXT(e);
		if ((void *)next >= end)
			return -EFSCORRUPTED;
		if (strnlen(e->e_name, e->e_name_len) != e->e_name_len)
			return -EFSCORRUPTED;
		e = next;
	}

	/* Check the values */
	while (!IS_LAST_ENTRY(entry)) {
		u32 size = le32_to_cpu(entry->e_value_size);

		if (size > EXT4_XATTR_SIZE_MAX)
			return -EFSCORRUPTED;

		if (size != 0 && entry->e_value_inum == 0) {
			u16 offs = le16_to_cpu(entry->e_value_offs);
			void *value;

			/*
			 * The value canyest overlap the names, and the value
			 * with padding canyest extend beyond 'end'.  Check both
			 * the padded and unpadded sizes, since the size may
			 * overflow to 0 when adding padding.
			 */
			if (offs > end - value_start)
				return -EFSCORRUPTED;
			value = value_start + offs;
			if (value < (void *)e + sizeof(u32) ||
			    size > end - value ||
			    EXT4_XATTR_SIZE(size) > end - value)
				return -EFSCORRUPTED;
		}
		entry = EXT4_XATTR_NEXT(entry);
	}

	return 0;
}

static inline int
__ext4_xattr_check_block(struct iyesde *iyesde, struct buffer_head *bh,
			 const char *function, unsigned int line)
{
	int error = -EFSCORRUPTED;

	if (BHDR(bh)->h_magic != cpu_to_le32(EXT4_XATTR_MAGIC) ||
	    BHDR(bh)->h_blocks != cpu_to_le32(1))
		goto errout;
	if (buffer_verified(bh))
		return 0;

	error = -EFSBADCRC;
	if (!ext4_xattr_block_csum_verify(iyesde, bh))
		goto errout;
	error = ext4_xattr_check_entries(BFIRST(bh), bh->b_data + bh->b_size,
					 bh->b_data);
errout:
	if (error)
		__ext4_error_iyesde(iyesde, function, line, 0,
				   "corrupted xattr block %llu",
				   (unsigned long long) bh->b_blocknr);
	else
		set_buffer_verified(bh);
	return error;
}

#define ext4_xattr_check_block(iyesde, bh) \
	__ext4_xattr_check_block((iyesde), (bh),  __func__, __LINE__)


static int
__xattr_check_iyesde(struct iyesde *iyesde, struct ext4_xattr_ibody_header *header,
			 void *end, const char *function, unsigned int line)
{
	int error = -EFSCORRUPTED;

	if (end - (void *)header < sizeof(*header) + sizeof(u32) ||
	    (header->h_magic != cpu_to_le32(EXT4_XATTR_MAGIC)))
		goto errout;
	error = ext4_xattr_check_entries(IFIRST(header), end, IFIRST(header));
errout:
	if (error)
		__ext4_error_iyesde(iyesde, function, line, 0,
				   "corrupted in-iyesde xattr");
	return error;
}

#define xattr_check_iyesde(iyesde, header, end) \
	__xattr_check_iyesde((iyesde), (header), (end), __func__, __LINE__)

static int
xattr_find_entry(struct iyesde *iyesde, struct ext4_xattr_entry **pentry,
		 void *end, int name_index, const char *name, int sorted)
{
	struct ext4_xattr_entry *entry, *next;
	size_t name_len;
	int cmp = 1;

	if (name == NULL)
		return -EINVAL;
	name_len = strlen(name);
	for (entry = *pentry; !IS_LAST_ENTRY(entry); entry = next) {
		next = EXT4_XATTR_NEXT(entry);
		if ((void *) next >= end) {
			EXT4_ERROR_INODE(iyesde, "corrupted xattr entries");
			return -EFSCORRUPTED;
		}
		cmp = name_index - entry->e_name_index;
		if (!cmp)
			cmp = name_len - entry->e_name_len;
		if (!cmp)
			cmp = memcmp(name, entry->e_name, name_len);
		if (cmp <= 0 && (sorted || cmp == 0))
			break;
	}
	*pentry = entry;
	return cmp ? -ENODATA : 0;
}

static u32
ext4_xattr_iyesde_hash(struct ext4_sb_info *sbi, const void *buffer, size_t size)
{
	return ext4_chksum(sbi, sbi->s_csum_seed, buffer, size);
}

static u64 ext4_xattr_iyesde_get_ref(struct iyesde *ea_iyesde)
{
	return ((u64)ea_iyesde->i_ctime.tv_sec << 32) |
		(u32) iyesde_peek_iversion_raw(ea_iyesde);
}

static void ext4_xattr_iyesde_set_ref(struct iyesde *ea_iyesde, u64 ref_count)
{
	ea_iyesde->i_ctime.tv_sec = (u32)(ref_count >> 32);
	iyesde_set_iversion_raw(ea_iyesde, ref_count & 0xffffffff);
}

static u32 ext4_xattr_iyesde_get_hash(struct iyesde *ea_iyesde)
{
	return (u32)ea_iyesde->i_atime.tv_sec;
}

static void ext4_xattr_iyesde_set_hash(struct iyesde *ea_iyesde, u32 hash)
{
	ea_iyesde->i_atime.tv_sec = hash;
}

/*
 * Read the EA value from an iyesde.
 */
static int ext4_xattr_iyesde_read(struct iyesde *ea_iyesde, void *buf, size_t size)
{
	int blocksize = 1 << ea_iyesde->i_blkbits;
	int bh_count = (size + blocksize - 1) >> ea_iyesde->i_blkbits;
	int tail_size = (size % blocksize) ?: blocksize;
	struct buffer_head *bhs_inline[8];
	struct buffer_head **bhs = bhs_inline;
	int i, ret;

	if (bh_count > ARRAY_SIZE(bhs_inline)) {
		bhs = kmalloc_array(bh_count, sizeof(*bhs), GFP_NOFS);
		if (!bhs)
			return -ENOMEM;
	}

	ret = ext4_bread_batch(ea_iyesde, 0 /* block */, bh_count,
			       true /* wait */, bhs);
	if (ret)
		goto free_bhs;

	for (i = 0; i < bh_count; i++) {
		/* There shouldn't be any holes in ea_iyesde. */
		if (!bhs[i]) {
			ret = -EFSCORRUPTED;
			goto put_bhs;
		}
		memcpy((char *)buf + blocksize * i, bhs[i]->b_data,
		       i < bh_count - 1 ? blocksize : tail_size);
	}
	ret = 0;
put_bhs:
	for (i = 0; i < bh_count; i++)
		brelse(bhs[i]);
free_bhs:
	if (bhs != bhs_inline)
		kfree(bhs);
	return ret;
}

#define EXT4_XATTR_INODE_GET_PARENT(iyesde) ((__u32)(iyesde)->i_mtime.tv_sec)

static int ext4_xattr_iyesde_iget(struct iyesde *parent, unsigned long ea_iyes,
				 u32 ea_iyesde_hash, struct iyesde **ea_iyesde)
{
	struct iyesde *iyesde;
	int err;

	iyesde = ext4_iget(parent->i_sb, ea_iyes, EXT4_IGET_NORMAL);
	if (IS_ERR(iyesde)) {
		err = PTR_ERR(iyesde);
		ext4_error(parent->i_sb,
			   "error while reading EA iyesde %lu err=%d", ea_iyes,
			   err);
		return err;
	}

	if (is_bad_iyesde(iyesde)) {
		ext4_error(parent->i_sb,
			   "error while reading EA iyesde %lu is_bad_iyesde",
			   ea_iyes);
		err = -EIO;
		goto error;
	}

	if (!(EXT4_I(iyesde)->i_flags & EXT4_EA_INODE_FL)) {
		ext4_error(parent->i_sb,
			   "EA iyesde %lu does yest have EXT4_EA_INODE_FL flag",
			    ea_iyes);
		err = -EINVAL;
		goto error;
	}

	ext4_xattr_iyesde_set_class(iyesde);

	/*
	 * Check whether this is an old Lustre-style xattr iyesde. Lustre
	 * implementation does yest have hash validation, rather it has a
	 * backpointer from ea_iyesde to the parent iyesde.
	 */
	if (ea_iyesde_hash != ext4_xattr_iyesde_get_hash(iyesde) &&
	    EXT4_XATTR_INODE_GET_PARENT(iyesde) == parent->i_iyes &&
	    iyesde->i_generation == parent->i_generation) {
		ext4_set_iyesde_state(iyesde, EXT4_STATE_LUSTRE_EA_INODE);
		ext4_xattr_iyesde_set_ref(iyesde, 1);
	} else {
		iyesde_lock(iyesde);
		iyesde->i_flags |= S_NOQUOTA;
		iyesde_unlock(iyesde);
	}

	*ea_iyesde = iyesde;
	return 0;
error:
	iput(iyesde);
	return err;
}

static int
ext4_xattr_iyesde_verify_hashes(struct iyesde *ea_iyesde,
			       struct ext4_xattr_entry *entry, void *buffer,
			       size_t size)
{
	u32 hash;

	/* Verify stored hash matches calculated hash. */
	hash = ext4_xattr_iyesde_hash(EXT4_SB(ea_iyesde->i_sb), buffer, size);
	if (hash != ext4_xattr_iyesde_get_hash(ea_iyesde))
		return -EFSCORRUPTED;

	if (entry) {
		__le32 e_hash, tmp_data;

		/* Verify entry hash. */
		tmp_data = cpu_to_le32(hash);
		e_hash = ext4_xattr_hash_entry(entry->e_name, entry->e_name_len,
					       &tmp_data, 1);
		if (e_hash != entry->e_hash)
			return -EFSCORRUPTED;
	}
	return 0;
}

/*
 * Read xattr value from the EA iyesde.
 */
static int
ext4_xattr_iyesde_get(struct iyesde *iyesde, struct ext4_xattr_entry *entry,
		     void *buffer, size_t size)
{
	struct mb_cache *ea_iyesde_cache = EA_INODE_CACHE(iyesde);
	struct iyesde *ea_iyesde;
	int err;

	err = ext4_xattr_iyesde_iget(iyesde, le32_to_cpu(entry->e_value_inum),
				    le32_to_cpu(entry->e_hash), &ea_iyesde);
	if (err) {
		ea_iyesde = NULL;
		goto out;
	}

	if (i_size_read(ea_iyesde) != size) {
		ext4_warning_iyesde(ea_iyesde,
				   "ea_iyesde file size=%llu entry size=%zu",
				   i_size_read(ea_iyesde), size);
		err = -EFSCORRUPTED;
		goto out;
	}

	err = ext4_xattr_iyesde_read(ea_iyesde, buffer, size);
	if (err)
		goto out;

	if (!ext4_test_iyesde_state(ea_iyesde, EXT4_STATE_LUSTRE_EA_INODE)) {
		err = ext4_xattr_iyesde_verify_hashes(ea_iyesde, entry, buffer,
						     size);
		if (err) {
			ext4_warning_iyesde(ea_iyesde,
					   "EA iyesde hash validation failed");
			goto out;
		}

		if (ea_iyesde_cache)
			mb_cache_entry_create(ea_iyesde_cache, GFP_NOFS,
					ext4_xattr_iyesde_get_hash(ea_iyesde),
					ea_iyesde->i_iyes, true /* reusable */);
	}
out:
	iput(ea_iyesde);
	return err;
}

static int
ext4_xattr_block_get(struct iyesde *iyesde, int name_index, const char *name,
		     void *buffer, size_t buffer_size)
{
	struct buffer_head *bh = NULL;
	struct ext4_xattr_entry *entry;
	size_t size;
	void *end;
	int error;
	struct mb_cache *ea_block_cache = EA_BLOCK_CACHE(iyesde);

	ea_idebug(iyesde, "name=%d.%s, buffer=%p, buffer_size=%ld",
		  name_index, name, buffer, (long)buffer_size);

	if (!EXT4_I(iyesde)->i_file_acl)
		return -ENODATA;
	ea_idebug(iyesde, "reading block %llu",
		  (unsigned long long)EXT4_I(iyesde)->i_file_acl);
	bh = ext4_sb_bread(iyesde->i_sb, EXT4_I(iyesde)->i_file_acl, REQ_PRIO);
	if (IS_ERR(bh))
		return PTR_ERR(bh);
	ea_bdebug(bh, "b_count=%d, refcount=%d",
		atomic_read(&(bh->b_count)), le32_to_cpu(BHDR(bh)->h_refcount));
	error = ext4_xattr_check_block(iyesde, bh);
	if (error)
		goto cleanup;
	ext4_xattr_block_cache_insert(ea_block_cache, bh);
	entry = BFIRST(bh);
	end = bh->b_data + bh->b_size;
	error = xattr_find_entry(iyesde, &entry, end, name_index, name, 1);
	if (error)
		goto cleanup;
	size = le32_to_cpu(entry->e_value_size);
	error = -ERANGE;
	if (unlikely(size > EXT4_XATTR_SIZE_MAX))
		goto cleanup;
	if (buffer) {
		if (size > buffer_size)
			goto cleanup;
		if (entry->e_value_inum) {
			error = ext4_xattr_iyesde_get(iyesde, entry, buffer,
						     size);
			if (error)
				goto cleanup;
		} else {
			u16 offset = le16_to_cpu(entry->e_value_offs);
			void *p = bh->b_data + offset;

			if (unlikely(p + size > end))
				goto cleanup;
			memcpy(buffer, p, size);
		}
	}
	error = size;

cleanup:
	brelse(bh);
	return error;
}

int
ext4_xattr_ibody_get(struct iyesde *iyesde, int name_index, const char *name,
		     void *buffer, size_t buffer_size)
{
	struct ext4_xattr_ibody_header *header;
	struct ext4_xattr_entry *entry;
	struct ext4_iyesde *raw_iyesde;
	struct ext4_iloc iloc;
	size_t size;
	void *end;
	int error;

	if (!ext4_test_iyesde_state(iyesde, EXT4_STATE_XATTR))
		return -ENODATA;
	error = ext4_get_iyesde_loc(iyesde, &iloc);
	if (error)
		return error;
	raw_iyesde = ext4_raw_iyesde(&iloc);
	header = IHDR(iyesde, raw_iyesde);
	end = (void *)raw_iyesde + EXT4_SB(iyesde->i_sb)->s_iyesde_size;
	error = xattr_check_iyesde(iyesde, header, end);
	if (error)
		goto cleanup;
	entry = IFIRST(header);
	error = xattr_find_entry(iyesde, &entry, end, name_index, name, 0);
	if (error)
		goto cleanup;
	size = le32_to_cpu(entry->e_value_size);
	error = -ERANGE;
	if (unlikely(size > EXT4_XATTR_SIZE_MAX))
		goto cleanup;
	if (buffer) {
		if (size > buffer_size)
			goto cleanup;
		if (entry->e_value_inum) {
			error = ext4_xattr_iyesde_get(iyesde, entry, buffer,
						     size);
			if (error)
				goto cleanup;
		} else {
			u16 offset = le16_to_cpu(entry->e_value_offs);
			void *p = (void *)IFIRST(header) + offset;

			if (unlikely(p + size > end))
				goto cleanup;
			memcpy(buffer, p, size);
		}
	}
	error = size;

cleanup:
	brelse(iloc.bh);
	return error;
}

/*
 * ext4_xattr_get()
 *
 * Copy an extended attribute into the buffer
 * provided, or compute the buffer size required.
 * Buffer is NULL to compute the size of the buffer required.
 *
 * Returns a negative error number on failure, or the number of bytes
 * used / required on success.
 */
int
ext4_xattr_get(struct iyesde *iyesde, int name_index, const char *name,
	       void *buffer, size_t buffer_size)
{
	int error;

	if (unlikely(ext4_forced_shutdown(EXT4_SB(iyesde->i_sb))))
		return -EIO;

	if (strlen(name) > 255)
		return -ERANGE;

	down_read(&EXT4_I(iyesde)->xattr_sem);
	error = ext4_xattr_ibody_get(iyesde, name_index, name, buffer,
				     buffer_size);
	if (error == -ENODATA)
		error = ext4_xattr_block_get(iyesde, name_index, name, buffer,
					     buffer_size);
	up_read(&EXT4_I(iyesde)->xattr_sem);
	return error;
}

static int
ext4_xattr_list_entries(struct dentry *dentry, struct ext4_xattr_entry *entry,
			char *buffer, size_t buffer_size)
{
	size_t rest = buffer_size;

	for (; !IS_LAST_ENTRY(entry); entry = EXT4_XATTR_NEXT(entry)) {
		const struct xattr_handler *handler =
			ext4_xattr_handler(entry->e_name_index);

		if (handler && (!handler->list || handler->list(dentry))) {
			const char *prefix = handler->prefix ?: handler->name;
			size_t prefix_len = strlen(prefix);
			size_t size = prefix_len + entry->e_name_len + 1;

			if (buffer) {
				if (size > rest)
					return -ERANGE;
				memcpy(buffer, prefix, prefix_len);
				buffer += prefix_len;
				memcpy(buffer, entry->e_name, entry->e_name_len);
				buffer += entry->e_name_len;
				*buffer++ = 0;
			}
			rest -= size;
		}
	}
	return buffer_size - rest;  /* total size */
}

static int
ext4_xattr_block_list(struct dentry *dentry, char *buffer, size_t buffer_size)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	struct buffer_head *bh = NULL;
	int error;

	ea_idebug(iyesde, "buffer=%p, buffer_size=%ld",
		  buffer, (long)buffer_size);

	if (!EXT4_I(iyesde)->i_file_acl)
		return 0;
	ea_idebug(iyesde, "reading block %llu",
		  (unsigned long long)EXT4_I(iyesde)->i_file_acl);
	bh = ext4_sb_bread(iyesde->i_sb, EXT4_I(iyesde)->i_file_acl, REQ_PRIO);
	if (IS_ERR(bh))
		return PTR_ERR(bh);
	ea_bdebug(bh, "b_count=%d, refcount=%d",
		atomic_read(&(bh->b_count)), le32_to_cpu(BHDR(bh)->h_refcount));
	error = ext4_xattr_check_block(iyesde, bh);
	if (error)
		goto cleanup;
	ext4_xattr_block_cache_insert(EA_BLOCK_CACHE(iyesde), bh);
	error = ext4_xattr_list_entries(dentry, BFIRST(bh), buffer,
					buffer_size);
cleanup:
	brelse(bh);
	return error;
}

static int
ext4_xattr_ibody_list(struct dentry *dentry, char *buffer, size_t buffer_size)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	struct ext4_xattr_ibody_header *header;
	struct ext4_iyesde *raw_iyesde;
	struct ext4_iloc iloc;
	void *end;
	int error;

	if (!ext4_test_iyesde_state(iyesde, EXT4_STATE_XATTR))
		return 0;
	error = ext4_get_iyesde_loc(iyesde, &iloc);
	if (error)
		return error;
	raw_iyesde = ext4_raw_iyesde(&iloc);
	header = IHDR(iyesde, raw_iyesde);
	end = (void *)raw_iyesde + EXT4_SB(iyesde->i_sb)->s_iyesde_size;
	error = xattr_check_iyesde(iyesde, header, end);
	if (error)
		goto cleanup;
	error = ext4_xattr_list_entries(dentry, IFIRST(header),
					buffer, buffer_size);

cleanup:
	brelse(iloc.bh);
	return error;
}

/*
 * Iyesde operation listxattr()
 *
 * d_iyesde(dentry)->i_rwsem: don't care
 *
 * Copy a list of attribute names into the buffer
 * provided, or compute the buffer size required.
 * Buffer is NULL to compute the size of the buffer required.
 *
 * Returns a negative error number on failure, or the number of bytes
 * used / required on success.
 */
ssize_t
ext4_listxattr(struct dentry *dentry, char *buffer, size_t buffer_size)
{
	int ret, ret2;

	down_read(&EXT4_I(d_iyesde(dentry))->xattr_sem);
	ret = ret2 = ext4_xattr_ibody_list(dentry, buffer, buffer_size);
	if (ret < 0)
		goto errout;
	if (buffer) {
		buffer += ret;
		buffer_size -= ret;
	}
	ret = ext4_xattr_block_list(dentry, buffer, buffer_size);
	if (ret < 0)
		goto errout;
	ret += ret2;
errout:
	up_read(&EXT4_I(d_iyesde(dentry))->xattr_sem);
	return ret;
}

/*
 * If the EXT4_FEATURE_COMPAT_EXT_ATTR feature of this file system is
 * yest set, set it.
 */
static void ext4_xattr_update_super_block(handle_t *handle,
					  struct super_block *sb)
{
	if (ext4_has_feature_xattr(sb))
		return;

	BUFFER_TRACE(EXT4_SB(sb)->s_sbh, "get_write_access");
	if (ext4_journal_get_write_access(handle, EXT4_SB(sb)->s_sbh) == 0) {
		ext4_set_feature_xattr(sb);
		ext4_handle_dirty_super(handle, sb);
	}
}

int ext4_get_iyesde_usage(struct iyesde *iyesde, qsize_t *usage)
{
	struct ext4_iloc iloc = { .bh = NULL };
	struct buffer_head *bh = NULL;
	struct ext4_iyesde *raw_iyesde;
	struct ext4_xattr_ibody_header *header;
	struct ext4_xattr_entry *entry;
	qsize_t ea_iyesde_refs = 0;
	void *end;
	int ret;

	lockdep_assert_held_read(&EXT4_I(iyesde)->xattr_sem);

	if (ext4_test_iyesde_state(iyesde, EXT4_STATE_XATTR)) {
		ret = ext4_get_iyesde_loc(iyesde, &iloc);
		if (ret)
			goto out;
		raw_iyesde = ext4_raw_iyesde(&iloc);
		header = IHDR(iyesde, raw_iyesde);
		end = (void *)raw_iyesde + EXT4_SB(iyesde->i_sb)->s_iyesde_size;
		ret = xattr_check_iyesde(iyesde, header, end);
		if (ret)
			goto out;

		for (entry = IFIRST(header); !IS_LAST_ENTRY(entry);
		     entry = EXT4_XATTR_NEXT(entry))
			if (entry->e_value_inum)
				ea_iyesde_refs++;
	}

	if (EXT4_I(iyesde)->i_file_acl) {
		bh = ext4_sb_bread(iyesde->i_sb, EXT4_I(iyesde)->i_file_acl, REQ_PRIO);
		if (IS_ERR(bh)) {
			ret = PTR_ERR(bh);
			bh = NULL;
			goto out;
		}

		ret = ext4_xattr_check_block(iyesde, bh);
		if (ret)
			goto out;

		for (entry = BFIRST(bh); !IS_LAST_ENTRY(entry);
		     entry = EXT4_XATTR_NEXT(entry))
			if (entry->e_value_inum)
				ea_iyesde_refs++;
	}
	*usage = ea_iyesde_refs + 1;
	ret = 0;
out:
	brelse(iloc.bh);
	brelse(bh);
	return ret;
}

static inline size_t round_up_cluster(struct iyesde *iyesde, size_t length)
{
	struct super_block *sb = iyesde->i_sb;
	size_t cluster_size = 1 << (EXT4_SB(sb)->s_cluster_bits +
				    iyesde->i_blkbits);
	size_t mask = ~(cluster_size - 1);

	return (length + cluster_size - 1) & mask;
}

static int ext4_xattr_iyesde_alloc_quota(struct iyesde *iyesde, size_t len)
{
	int err;

	err = dquot_alloc_iyesde(iyesde);
	if (err)
		return err;
	err = dquot_alloc_space_yesdirty(iyesde, round_up_cluster(iyesde, len));
	if (err)
		dquot_free_iyesde(iyesde);
	return err;
}

static void ext4_xattr_iyesde_free_quota(struct iyesde *parent,
					struct iyesde *ea_iyesde,
					size_t len)
{
	if (ea_iyesde &&
	    ext4_test_iyesde_state(ea_iyesde, EXT4_STATE_LUSTRE_EA_INODE))
		return;
	dquot_free_space_yesdirty(parent, round_up_cluster(parent, len));
	dquot_free_iyesde(parent);
}

int __ext4_xattr_set_credits(struct super_block *sb, struct iyesde *iyesde,
			     struct buffer_head *block_bh, size_t value_len,
			     bool is_create)
{
	int credits;
	int blocks;

	/*
	 * 1) Owner iyesde update
	 * 2) Ref count update on old xattr block
	 * 3) new xattr block
	 * 4) block bitmap update for new xattr block
	 * 5) group descriptor for new xattr block
	 * 6) block bitmap update for old xattr block
	 * 7) group descriptor for old block
	 *
	 * 6 & 7 can happen if we have two racing threads T_a and T_b
	 * which are each trying to set an xattr on iyesdes I_a and I_b
	 * which were both initially sharing an xattr block.
	 */
	credits = 7;

	/* Quota updates. */
	credits += EXT4_MAXQUOTAS_TRANS_BLOCKS(sb);

	/*
	 * In case of inline data, we may push out the data to a block,
	 * so we need to reserve credits for this eventuality
	 */
	if (iyesde && ext4_has_inline_data(iyesde))
		credits += ext4_writepage_trans_blocks(iyesde) + 1;

	/* We are done if ea_iyesde feature is yest enabled. */
	if (!ext4_has_feature_ea_iyesde(sb))
		return credits;

	/* New ea_iyesde, iyesde map, block bitmap, group descriptor. */
	credits += 4;

	/* Data blocks. */
	blocks = (value_len + sb->s_blocksize - 1) >> sb->s_blocksize_bits;

	/* Indirection block or one level of extent tree. */
	blocks += 1;

	/* Block bitmap and group descriptor updates for each block. */
	credits += blocks * 2;

	/* Blocks themselves. */
	credits += blocks;

	if (!is_create) {
		/* Dereference ea_iyesde holding old xattr value.
		 * Old ea_iyesde, iyesde map, block bitmap, group descriptor.
		 */
		credits += 4;

		/* Data blocks for old ea_iyesde. */
		blocks = XATTR_SIZE_MAX >> sb->s_blocksize_bits;

		/* Indirection block or one level of extent tree for old
		 * ea_iyesde.
		 */
		blocks += 1;

		/* Block bitmap and group descriptor updates for each block. */
		credits += blocks * 2;
	}

	/* We may need to clone the existing xattr block in which case we need
	 * to increment ref counts for existing ea_iyesdes referenced by it.
	 */
	if (block_bh) {
		struct ext4_xattr_entry *entry = BFIRST(block_bh);

		for (; !IS_LAST_ENTRY(entry); entry = EXT4_XATTR_NEXT(entry))
			if (entry->e_value_inum)
				/* Ref count update on ea_iyesde. */
				credits += 1;
	}
	return credits;
}

static int ext4_xattr_iyesde_update_ref(handle_t *handle, struct iyesde *ea_iyesde,
				       int ref_change)
{
	struct mb_cache *ea_iyesde_cache = EA_INODE_CACHE(ea_iyesde);
	struct ext4_iloc iloc;
	s64 ref_count;
	u32 hash;
	int ret;

	iyesde_lock(ea_iyesde);

	ret = ext4_reserve_iyesde_write(handle, ea_iyesde, &iloc);
	if (ret)
		goto out;

	ref_count = ext4_xattr_iyesde_get_ref(ea_iyesde);
	ref_count += ref_change;
	ext4_xattr_iyesde_set_ref(ea_iyesde, ref_count);

	if (ref_change > 0) {
		WARN_ONCE(ref_count <= 0, "EA iyesde %lu ref_count=%lld",
			  ea_iyesde->i_iyes, ref_count);

		if (ref_count == 1) {
			WARN_ONCE(ea_iyesde->i_nlink, "EA iyesde %lu i_nlink=%u",
				  ea_iyesde->i_iyes, ea_iyesde->i_nlink);

			set_nlink(ea_iyesde, 1);
			ext4_orphan_del(handle, ea_iyesde);

			if (ea_iyesde_cache) {
				hash = ext4_xattr_iyesde_get_hash(ea_iyesde);
				mb_cache_entry_create(ea_iyesde_cache,
						      GFP_NOFS, hash,
						      ea_iyesde->i_iyes,
						      true /* reusable */);
			}
		}
	} else {
		WARN_ONCE(ref_count < 0, "EA iyesde %lu ref_count=%lld",
			  ea_iyesde->i_iyes, ref_count);

		if (ref_count == 0) {
			WARN_ONCE(ea_iyesde->i_nlink != 1,
				  "EA iyesde %lu i_nlink=%u",
				  ea_iyesde->i_iyes, ea_iyesde->i_nlink);

			clear_nlink(ea_iyesde);
			ext4_orphan_add(handle, ea_iyesde);

			if (ea_iyesde_cache) {
				hash = ext4_xattr_iyesde_get_hash(ea_iyesde);
				mb_cache_entry_delete(ea_iyesde_cache, hash,
						      ea_iyesde->i_iyes);
			}
		}
	}

	ret = ext4_mark_iloc_dirty(handle, ea_iyesde, &iloc);
	if (ret)
		ext4_warning_iyesde(ea_iyesde,
				   "ext4_mark_iloc_dirty() failed ret=%d", ret);
out:
	iyesde_unlock(ea_iyesde);
	return ret;
}

static int ext4_xattr_iyesde_inc_ref(handle_t *handle, struct iyesde *ea_iyesde)
{
	return ext4_xattr_iyesde_update_ref(handle, ea_iyesde, 1);
}

static int ext4_xattr_iyesde_dec_ref(handle_t *handle, struct iyesde *ea_iyesde)
{
	return ext4_xattr_iyesde_update_ref(handle, ea_iyesde, -1);
}

static int ext4_xattr_iyesde_inc_ref_all(handle_t *handle, struct iyesde *parent,
					struct ext4_xattr_entry *first)
{
	struct iyesde *ea_iyesde;
	struct ext4_xattr_entry *entry;
	struct ext4_xattr_entry *failed_entry;
	unsigned int ea_iyes;
	int err, saved_err;

	for (entry = first; !IS_LAST_ENTRY(entry);
	     entry = EXT4_XATTR_NEXT(entry)) {
		if (!entry->e_value_inum)
			continue;
		ea_iyes = le32_to_cpu(entry->e_value_inum);
		err = ext4_xattr_iyesde_iget(parent, ea_iyes,
					    le32_to_cpu(entry->e_hash),
					    &ea_iyesde);
		if (err)
			goto cleanup;
		err = ext4_xattr_iyesde_inc_ref(handle, ea_iyesde);
		if (err) {
			ext4_warning_iyesde(ea_iyesde, "inc ref error %d", err);
			iput(ea_iyesde);
			goto cleanup;
		}
		iput(ea_iyesde);
	}
	return 0;

cleanup:
	saved_err = err;
	failed_entry = entry;

	for (entry = first; entry != failed_entry;
	     entry = EXT4_XATTR_NEXT(entry)) {
		if (!entry->e_value_inum)
			continue;
		ea_iyes = le32_to_cpu(entry->e_value_inum);
		err = ext4_xattr_iyesde_iget(parent, ea_iyes,
					    le32_to_cpu(entry->e_hash),
					    &ea_iyesde);
		if (err) {
			ext4_warning(parent->i_sb,
				     "cleanup ea_iyes %u iget error %d", ea_iyes,
				     err);
			continue;
		}
		err = ext4_xattr_iyesde_dec_ref(handle, ea_iyesde);
		if (err)
			ext4_warning_iyesde(ea_iyesde, "cleanup dec ref error %d",
					   err);
		iput(ea_iyesde);
	}
	return saved_err;
}

static int ext4_xattr_restart_fn(handle_t *handle, struct iyesde *iyesde,
			struct buffer_head *bh, bool block_csum, bool dirty)
{
	int error;

	if (bh && dirty) {
		if (block_csum)
			ext4_xattr_block_csum_set(iyesde, bh);
		error = ext4_handle_dirty_metadata(handle, NULL, bh);
		if (error) {
			ext4_warning(iyesde->i_sb, "Handle metadata (error %d)",
				     error);
			return error;
		}
	}
	return 0;
}

static void
ext4_xattr_iyesde_dec_ref_all(handle_t *handle, struct iyesde *parent,
			     struct buffer_head *bh,
			     struct ext4_xattr_entry *first, bool block_csum,
			     struct ext4_xattr_iyesde_array **ea_iyesde_array,
			     int extra_credits, bool skip_quota)
{
	struct iyesde *ea_iyesde;
	struct ext4_xattr_entry *entry;
	bool dirty = false;
	unsigned int ea_iyes;
	int err;
	int credits;

	/* One credit for dec ref on ea_iyesde, one for orphan list addition, */
	credits = 2 + extra_credits;

	for (entry = first; !IS_LAST_ENTRY(entry);
	     entry = EXT4_XATTR_NEXT(entry)) {
		if (!entry->e_value_inum)
			continue;
		ea_iyes = le32_to_cpu(entry->e_value_inum);
		err = ext4_xattr_iyesde_iget(parent, ea_iyes,
					    le32_to_cpu(entry->e_hash),
					    &ea_iyesde);
		if (err)
			continue;

		err = ext4_expand_iyesde_array(ea_iyesde_array, ea_iyesde);
		if (err) {
			ext4_warning_iyesde(ea_iyesde,
					   "Expand iyesde array err=%d", err);
			iput(ea_iyesde);
			continue;
		}

		err = ext4_journal_ensure_credits_fn(handle, credits, credits,
			ext4_free_metadata_revoke_credits(parent->i_sb, 1),
			ext4_xattr_restart_fn(handle, parent, bh, block_csum,
					      dirty));
		if (err < 0) {
			ext4_warning_iyesde(ea_iyesde, "Ensure credits err=%d",
					   err);
			continue;
		}
		if (err > 0) {
			err = ext4_journal_get_write_access(handle, bh);
			if (err) {
				ext4_warning_iyesde(ea_iyesde,
						"Re-get write access err=%d",
						err);
				continue;
			}
		}

		err = ext4_xattr_iyesde_dec_ref(handle, ea_iyesde);
		if (err) {
			ext4_warning_iyesde(ea_iyesde, "ea_iyesde dec ref err=%d",
					   err);
			continue;
		}

		if (!skip_quota)
			ext4_xattr_iyesde_free_quota(parent, ea_iyesde,
					      le32_to_cpu(entry->e_value_size));

		/*
		 * Forget about ea_iyesde within the same transaction that
		 * decrements the ref count. This avoids duplicate decrements in
		 * case the rest of the work spills over to subsequent
		 * transactions.
		 */
		entry->e_value_inum = 0;
		entry->e_value_size = 0;

		dirty = true;
	}

	if (dirty) {
		/*
		 * Note that we are deliberately skipping csum calculation for
		 * the final update because we do yest expect any journal
		 * restarts until xattr block is freed.
		 */

		err = ext4_handle_dirty_metadata(handle, NULL, bh);
		if (err)
			ext4_warning_iyesde(parent,
					   "handle dirty metadata err=%d", err);
	}
}

/*
 * Release the xattr block BH: If the reference count is > 1, decrement it;
 * otherwise free the block.
 */
static void
ext4_xattr_release_block(handle_t *handle, struct iyesde *iyesde,
			 struct buffer_head *bh,
			 struct ext4_xattr_iyesde_array **ea_iyesde_array,
			 int extra_credits)
{
	struct mb_cache *ea_block_cache = EA_BLOCK_CACHE(iyesde);
	u32 hash, ref;
	int error = 0;

	BUFFER_TRACE(bh, "get_write_access");
	error = ext4_journal_get_write_access(handle, bh);
	if (error)
		goto out;

	lock_buffer(bh);
	hash = le32_to_cpu(BHDR(bh)->h_hash);
	ref = le32_to_cpu(BHDR(bh)->h_refcount);
	if (ref == 1) {
		ea_bdebug(bh, "refcount yesw=0; freeing");
		/*
		 * This must happen under buffer lock for
		 * ext4_xattr_block_set() to reliably detect freed block
		 */
		if (ea_block_cache)
			mb_cache_entry_delete(ea_block_cache, hash,
					      bh->b_blocknr);
		get_bh(bh);
		unlock_buffer(bh);

		if (ext4_has_feature_ea_iyesde(iyesde->i_sb))
			ext4_xattr_iyesde_dec_ref_all(handle, iyesde, bh,
						     BFIRST(bh),
						     true /* block_csum */,
						     ea_iyesde_array,
						     extra_credits,
						     true /* skip_quota */);
		ext4_free_blocks(handle, iyesde, bh, 0, 1,
				 EXT4_FREE_BLOCKS_METADATA |
				 EXT4_FREE_BLOCKS_FORGET);
	} else {
		ref--;
		BHDR(bh)->h_refcount = cpu_to_le32(ref);
		if (ref == EXT4_XATTR_REFCOUNT_MAX - 1) {
			struct mb_cache_entry *ce;

			if (ea_block_cache) {
				ce = mb_cache_entry_get(ea_block_cache, hash,
							bh->b_blocknr);
				if (ce) {
					ce->e_reusable = 1;
					mb_cache_entry_put(ea_block_cache, ce);
				}
			}
		}

		ext4_xattr_block_csum_set(iyesde, bh);
		/*
		 * Beware of this ugliness: Releasing of xattr block references
		 * from different iyesdes can race and so we have to protect
		 * from a race where someone else frees the block (and releases
		 * its journal_head) before we are done dirtying the buffer. In
		 * yesjournal mode this race is harmless and we actually canyest
		 * call ext4_handle_dirty_metadata() with locked buffer as
		 * that function can call sync_dirty_buffer() so for that case
		 * we handle the dirtying after unlocking the buffer.
		 */
		if (ext4_handle_valid(handle))
			error = ext4_handle_dirty_metadata(handle, iyesde, bh);
		unlock_buffer(bh);
		if (!ext4_handle_valid(handle))
			error = ext4_handle_dirty_metadata(handle, iyesde, bh);
		if (IS_SYNC(iyesde))
			ext4_handle_sync(handle);
		dquot_free_block(iyesde, EXT4_C2B(EXT4_SB(iyesde->i_sb), 1));
		ea_bdebug(bh, "refcount yesw=%d; releasing",
			  le32_to_cpu(BHDR(bh)->h_refcount));
	}
out:
	ext4_std_error(iyesde->i_sb, error);
	return;
}

/*
 * Find the available free space for EAs. This also returns the total number of
 * bytes used by EA entries.
 */
static size_t ext4_xattr_free_space(struct ext4_xattr_entry *last,
				    size_t *min_offs, void *base, int *total)
{
	for (; !IS_LAST_ENTRY(last); last = EXT4_XATTR_NEXT(last)) {
		if (!last->e_value_inum && last->e_value_size) {
			size_t offs = le16_to_cpu(last->e_value_offs);
			if (offs < *min_offs)
				*min_offs = offs;
		}
		if (total)
			*total += EXT4_XATTR_LEN(last->e_name_len);
	}
	return (*min_offs - ((void *)last - base) - sizeof(__u32));
}

/*
 * Write the value of the EA in an iyesde.
 */
static int ext4_xattr_iyesde_write(handle_t *handle, struct iyesde *ea_iyesde,
				  const void *buf, int bufsize)
{
	struct buffer_head *bh = NULL;
	unsigned long block = 0;
	int blocksize = ea_iyesde->i_sb->s_blocksize;
	int max_blocks = (bufsize + blocksize - 1) >> ea_iyesde->i_blkbits;
	int csize, wsize = 0;
	int ret = 0;
	int retries = 0;

retry:
	while (ret >= 0 && ret < max_blocks) {
		struct ext4_map_blocks map;
		map.m_lblk = block += ret;
		map.m_len = max_blocks -= ret;

		ret = ext4_map_blocks(handle, ea_iyesde, &map,
				      EXT4_GET_BLOCKS_CREATE);
		if (ret <= 0) {
			ext4_mark_iyesde_dirty(handle, ea_iyesde);
			if (ret == -ENOSPC &&
			    ext4_should_retry_alloc(ea_iyesde->i_sb, &retries)) {
				ret = 0;
				goto retry;
			}
			break;
		}
	}

	if (ret < 0)
		return ret;

	block = 0;
	while (wsize < bufsize) {
		if (bh != NULL)
			brelse(bh);
		csize = (bufsize - wsize) > blocksize ? blocksize :
								bufsize - wsize;
		bh = ext4_getblk(handle, ea_iyesde, block, 0);
		if (IS_ERR(bh))
			return PTR_ERR(bh);
		if (!bh) {
			WARN_ON_ONCE(1);
			EXT4_ERROR_INODE(ea_iyesde,
					 "ext4_getblk() return bh = NULL");
			return -EFSCORRUPTED;
		}
		ret = ext4_journal_get_write_access(handle, bh);
		if (ret)
			goto out;

		memcpy(bh->b_data, buf, csize);
		set_buffer_uptodate(bh);
		ext4_handle_dirty_metadata(handle, ea_iyesde, bh);

		buf += csize;
		wsize += csize;
		block += 1;
	}

	iyesde_lock(ea_iyesde);
	i_size_write(ea_iyesde, wsize);
	ext4_update_i_disksize(ea_iyesde, wsize);
	iyesde_unlock(ea_iyesde);

	ext4_mark_iyesde_dirty(handle, ea_iyesde);

out:
	brelse(bh);

	return ret;
}

/*
 * Create an iyesde to store the value of a large EA.
 */
static struct iyesde *ext4_xattr_iyesde_create(handle_t *handle,
					     struct iyesde *iyesde, u32 hash)
{
	struct iyesde *ea_iyesde = NULL;
	uid_t owner[2] = { i_uid_read(iyesde), i_gid_read(iyesde) };
	int err;

	/*
	 * Let the next iyesde be the goal, so we try and allocate the EA iyesde
	 * in the same group, or nearby one.
	 */
	ea_iyesde = ext4_new_iyesde(handle, iyesde->i_sb->s_root->d_iyesde,
				  S_IFREG | 0600, NULL, iyesde->i_iyes + 1, owner,
				  EXT4_EA_INODE_FL);
	if (!IS_ERR(ea_iyesde)) {
		ea_iyesde->i_op = &ext4_file_iyesde_operations;
		ea_iyesde->i_fop = &ext4_file_operations;
		ext4_set_aops(ea_iyesde);
		ext4_xattr_iyesde_set_class(ea_iyesde);
		unlock_new_iyesde(ea_iyesde);
		ext4_xattr_iyesde_set_ref(ea_iyesde, 1);
		ext4_xattr_iyesde_set_hash(ea_iyesde, hash);
		err = ext4_mark_iyesde_dirty(handle, ea_iyesde);
		if (!err)
			err = ext4_iyesde_attach_jiyesde(ea_iyesde);
		if (err) {
			iput(ea_iyesde);
			return ERR_PTR(err);
		}

		/*
		 * Xattr iyesdes are shared therefore quota charging is performed
		 * at a higher level.
		 */
		dquot_free_iyesde(ea_iyesde);
		dquot_drop(ea_iyesde);
		iyesde_lock(ea_iyesde);
		ea_iyesde->i_flags |= S_NOQUOTA;
		iyesde_unlock(ea_iyesde);
	}

	return ea_iyesde;
}

static struct iyesde *
ext4_xattr_iyesde_cache_find(struct iyesde *iyesde, const void *value,
			    size_t value_len, u32 hash)
{
	struct iyesde *ea_iyesde;
	struct mb_cache_entry *ce;
	struct mb_cache *ea_iyesde_cache = EA_INODE_CACHE(iyesde);
	void *ea_data;

	if (!ea_iyesde_cache)
		return NULL;

	ce = mb_cache_entry_find_first(ea_iyesde_cache, hash);
	if (!ce)
		return NULL;

	ea_data = ext4_kvmalloc(value_len, GFP_NOFS);
	if (!ea_data) {
		mb_cache_entry_put(ea_iyesde_cache, ce);
		return NULL;
	}

	while (ce) {
		ea_iyesde = ext4_iget(iyesde->i_sb, ce->e_value,
				     EXT4_IGET_NORMAL);
		if (!IS_ERR(ea_iyesde) &&
		    !is_bad_iyesde(ea_iyesde) &&
		    (EXT4_I(ea_iyesde)->i_flags & EXT4_EA_INODE_FL) &&
		    i_size_read(ea_iyesde) == value_len &&
		    !ext4_xattr_iyesde_read(ea_iyesde, ea_data, value_len) &&
		    !ext4_xattr_iyesde_verify_hashes(ea_iyesde, NULL, ea_data,
						    value_len) &&
		    !memcmp(value, ea_data, value_len)) {
			mb_cache_entry_touch(ea_iyesde_cache, ce);
			mb_cache_entry_put(ea_iyesde_cache, ce);
			kvfree(ea_data);
			return ea_iyesde;
		}

		if (!IS_ERR(ea_iyesde))
			iput(ea_iyesde);
		ce = mb_cache_entry_find_next(ea_iyesde_cache, ce);
	}
	kvfree(ea_data);
	return NULL;
}

/*
 * Add value of the EA in an iyesde.
 */
static int ext4_xattr_iyesde_lookup_create(handle_t *handle, struct iyesde *iyesde,
					  const void *value, size_t value_len,
					  struct iyesde **ret_iyesde)
{
	struct iyesde *ea_iyesde;
	u32 hash;
	int err;

	hash = ext4_xattr_iyesde_hash(EXT4_SB(iyesde->i_sb), value, value_len);
	ea_iyesde = ext4_xattr_iyesde_cache_find(iyesde, value, value_len, hash);
	if (ea_iyesde) {
		err = ext4_xattr_iyesde_inc_ref(handle, ea_iyesde);
		if (err) {
			iput(ea_iyesde);
			return err;
		}

		*ret_iyesde = ea_iyesde;
		return 0;
	}

	/* Create an iyesde for the EA value */
	ea_iyesde = ext4_xattr_iyesde_create(handle, iyesde, hash);
	if (IS_ERR(ea_iyesde))
		return PTR_ERR(ea_iyesde);

	err = ext4_xattr_iyesde_write(handle, ea_iyesde, value, value_len);
	if (err) {
		ext4_xattr_iyesde_dec_ref(handle, ea_iyesde);
		iput(ea_iyesde);
		return err;
	}

	if (EA_INODE_CACHE(iyesde))
		mb_cache_entry_create(EA_INODE_CACHE(iyesde), GFP_NOFS, hash,
				      ea_iyesde->i_iyes, true /* reusable */);

	*ret_iyesde = ea_iyesde;
	return 0;
}

/*
 * Reserve min(block_size/8, 1024) bytes for xattr entries/names if ea_iyesde
 * feature is enabled.
 */
#define EXT4_XATTR_BLOCK_RESERVE(iyesde)	min(i_blocksize(iyesde)/8, 1024U)

static int ext4_xattr_set_entry(struct ext4_xattr_info *i,
				struct ext4_xattr_search *s,
				handle_t *handle, struct iyesde *iyesde,
				bool is_block)
{
	struct ext4_xattr_entry *last, *next;
	struct ext4_xattr_entry *here = s->here;
	size_t min_offs = s->end - s->base, name_len = strlen(i->name);
	int in_iyesde = i->in_iyesde;
	struct iyesde *old_ea_iyesde = NULL;
	struct iyesde *new_ea_iyesde = NULL;
	size_t old_size, new_size;
	int ret;

	/* Space used by old and new values. */
	old_size = (!s->yest_found && !here->e_value_inum) ?
			EXT4_XATTR_SIZE(le32_to_cpu(here->e_value_size)) : 0;
	new_size = (i->value && !in_iyesde) ? EXT4_XATTR_SIZE(i->value_len) : 0;

	/*
	 * Optimization for the simple case when old and new values have the
	 * same padded sizes. Not applicable if external iyesdes are involved.
	 */
	if (new_size && new_size == old_size) {
		size_t offs = le16_to_cpu(here->e_value_offs);
		void *val = s->base + offs;

		here->e_value_size = cpu_to_le32(i->value_len);
		if (i->value == EXT4_ZERO_XATTR_VALUE) {
			memset(val, 0, new_size);
		} else {
			memcpy(val, i->value, i->value_len);
			/* Clear padding bytes. */
			memset(val + i->value_len, 0, new_size - i->value_len);
		}
		goto update_hash;
	}

	/* Compute min_offs and last. */
	last = s->first;
	for (; !IS_LAST_ENTRY(last); last = next) {
		next = EXT4_XATTR_NEXT(last);
		if ((void *)next >= s->end) {
			EXT4_ERROR_INODE(iyesde, "corrupted xattr entries");
			ret = -EFSCORRUPTED;
			goto out;
		}
		if (!last->e_value_inum && last->e_value_size) {
			size_t offs = le16_to_cpu(last->e_value_offs);
			if (offs < min_offs)
				min_offs = offs;
		}
	}

	/* Check whether we have eyesugh space. */
	if (i->value) {
		size_t free;

		free = min_offs - ((void *)last - s->base) - sizeof(__u32);
		if (!s->yest_found)
			free += EXT4_XATTR_LEN(name_len) + old_size;

		if (free < EXT4_XATTR_LEN(name_len) + new_size) {
			ret = -ENOSPC;
			goto out;
		}

		/*
		 * If storing the value in an external iyesde is an option,
		 * reserve space for xattr entries/names in the external
		 * attribute block so that a long value does yest occupy the
		 * whole space and prevent futher entries being added.
		 */
		if (ext4_has_feature_ea_iyesde(iyesde->i_sb) &&
		    new_size && is_block &&
		    (min_offs + old_size - new_size) <
					EXT4_XATTR_BLOCK_RESERVE(iyesde)) {
			ret = -ENOSPC;
			goto out;
		}
	}

	/*
	 * Getting access to old and new ea iyesdes is subject to failures.
	 * Finish that work before doing any modifications to the xattr data.
	 */
	if (!s->yest_found && here->e_value_inum) {
		ret = ext4_xattr_iyesde_iget(iyesde,
					    le32_to_cpu(here->e_value_inum),
					    le32_to_cpu(here->e_hash),
					    &old_ea_iyesde);
		if (ret) {
			old_ea_iyesde = NULL;
			goto out;
		}
	}
	if (i->value && in_iyesde) {
		WARN_ON_ONCE(!i->value_len);

		ret = ext4_xattr_iyesde_alloc_quota(iyesde, i->value_len);
		if (ret)
			goto out;

		ret = ext4_xattr_iyesde_lookup_create(handle, iyesde, i->value,
						     i->value_len,
						     &new_ea_iyesde);
		if (ret) {
			new_ea_iyesde = NULL;
			ext4_xattr_iyesde_free_quota(iyesde, NULL, i->value_len);
			goto out;
		}
	}

	if (old_ea_iyesde) {
		/* We are ready to release ref count on the old_ea_iyesde. */
		ret = ext4_xattr_iyesde_dec_ref(handle, old_ea_iyesde);
		if (ret) {
			/* Release newly required ref count on new_ea_iyesde. */
			if (new_ea_iyesde) {
				int err;

				err = ext4_xattr_iyesde_dec_ref(handle,
							       new_ea_iyesde);
				if (err)
					ext4_warning_iyesde(new_ea_iyesde,
						  "dec ref new_ea_iyesde err=%d",
						  err);
				ext4_xattr_iyesde_free_quota(iyesde, new_ea_iyesde,
							    i->value_len);
			}
			goto out;
		}

		ext4_xattr_iyesde_free_quota(iyesde, old_ea_iyesde,
					    le32_to_cpu(here->e_value_size));
	}

	/* No failures allowed past this point. */

	if (!s->yest_found && here->e_value_size && !here->e_value_inum) {
		/* Remove the old value. */
		void *first_val = s->base + min_offs;
		size_t offs = le16_to_cpu(here->e_value_offs);
		void *val = s->base + offs;

		memmove(first_val + old_size, first_val, val - first_val);
		memset(first_val, 0, old_size);
		min_offs += old_size;

		/* Adjust all value offsets. */
		last = s->first;
		while (!IS_LAST_ENTRY(last)) {
			size_t o = le16_to_cpu(last->e_value_offs);

			if (!last->e_value_inum &&
			    last->e_value_size && o < offs)
				last->e_value_offs = cpu_to_le16(o + old_size);
			last = EXT4_XATTR_NEXT(last);
		}
	}

	if (!i->value) {
		/* Remove old name. */
		size_t size = EXT4_XATTR_LEN(name_len);

		last = ENTRY((void *)last - size);
		memmove(here, (void *)here + size,
			(void *)last - (void *)here + sizeof(__u32));
		memset(last, 0, size);
	} else if (s->yest_found) {
		/* Insert new name. */
		size_t size = EXT4_XATTR_LEN(name_len);
		size_t rest = (void *)last - (void *)here + sizeof(__u32);

		memmove((void *)here + size, here, rest);
		memset(here, 0, size);
		here->e_name_index = i->name_index;
		here->e_name_len = name_len;
		memcpy(here->e_name, i->name, name_len);
	} else {
		/* This is an update, reset value info. */
		here->e_value_inum = 0;
		here->e_value_offs = 0;
		here->e_value_size = 0;
	}

	if (i->value) {
		/* Insert new value. */
		if (in_iyesde) {
			here->e_value_inum = cpu_to_le32(new_ea_iyesde->i_iyes);
		} else if (i->value_len) {
			void *val = s->base + min_offs - new_size;

			here->e_value_offs = cpu_to_le16(min_offs - new_size);
			if (i->value == EXT4_ZERO_XATTR_VALUE) {
				memset(val, 0, new_size);
			} else {
				memcpy(val, i->value, i->value_len);
				/* Clear padding bytes. */
				memset(val + i->value_len, 0,
				       new_size - i->value_len);
			}
		}
		here->e_value_size = cpu_to_le32(i->value_len);
	}

update_hash:
	if (i->value) {
		__le32 hash = 0;

		/* Entry hash calculation. */
		if (in_iyesde) {
			__le32 crc32c_hash;

			/*
			 * Feed crc32c hash instead of the raw value for entry
			 * hash calculation. This is to avoid walking
			 * potentially long value buffer again.
			 */
			crc32c_hash = cpu_to_le32(
				       ext4_xattr_iyesde_get_hash(new_ea_iyesde));
			hash = ext4_xattr_hash_entry(here->e_name,
						     here->e_name_len,
						     &crc32c_hash, 1);
		} else if (is_block) {
			__le32 *value = s->base + le16_to_cpu(
							here->e_value_offs);

			hash = ext4_xattr_hash_entry(here->e_name,
						     here->e_name_len, value,
						     new_size >> 2);
		}
		here->e_hash = hash;
	}

	if (is_block)
		ext4_xattr_rehash((struct ext4_xattr_header *)s->base);

	ret = 0;
out:
	iput(old_ea_iyesde);
	iput(new_ea_iyesde);
	return ret;
}

struct ext4_xattr_block_find {
	struct ext4_xattr_search s;
	struct buffer_head *bh;
};

static int
ext4_xattr_block_find(struct iyesde *iyesde, struct ext4_xattr_info *i,
		      struct ext4_xattr_block_find *bs)
{
	struct super_block *sb = iyesde->i_sb;
	int error;

	ea_idebug(iyesde, "name=%d.%s, value=%p, value_len=%ld",
		  i->name_index, i->name, i->value, (long)i->value_len);

	if (EXT4_I(iyesde)->i_file_acl) {
		/* The iyesde already has an extended attribute block. */
		bs->bh = ext4_sb_bread(sb, EXT4_I(iyesde)->i_file_acl, REQ_PRIO);
		if (IS_ERR(bs->bh))
			return PTR_ERR(bs->bh);
		ea_bdebug(bs->bh, "b_count=%d, refcount=%d",
			atomic_read(&(bs->bh->b_count)),
			le32_to_cpu(BHDR(bs->bh)->h_refcount));
		error = ext4_xattr_check_block(iyesde, bs->bh);
		if (error)
			return error;
		/* Find the named attribute. */
		bs->s.base = BHDR(bs->bh);
		bs->s.first = BFIRST(bs->bh);
		bs->s.end = bs->bh->b_data + bs->bh->b_size;
		bs->s.here = bs->s.first;
		error = xattr_find_entry(iyesde, &bs->s.here, bs->s.end,
					 i->name_index, i->name, 1);
		if (error && error != -ENODATA)
			return error;
		bs->s.yest_found = error;
	}
	return 0;
}

static int
ext4_xattr_block_set(handle_t *handle, struct iyesde *iyesde,
		     struct ext4_xattr_info *i,
		     struct ext4_xattr_block_find *bs)
{
	struct super_block *sb = iyesde->i_sb;
	struct buffer_head *new_bh = NULL;
	struct ext4_xattr_search s_copy = bs->s;
	struct ext4_xattr_search *s = &s_copy;
	struct mb_cache_entry *ce = NULL;
	int error = 0;
	struct mb_cache *ea_block_cache = EA_BLOCK_CACHE(iyesde);
	struct iyesde *ea_iyesde = NULL, *tmp_iyesde;
	size_t old_ea_iyesde_quota = 0;
	unsigned int ea_iyes;


#define header(x) ((struct ext4_xattr_header *)(x))

	if (s->base) {
		BUFFER_TRACE(bs->bh, "get_write_access");
		error = ext4_journal_get_write_access(handle, bs->bh);
		if (error)
			goto cleanup;
		lock_buffer(bs->bh);

		if (header(s->base)->h_refcount == cpu_to_le32(1)) {
			__u32 hash = le32_to_cpu(BHDR(bs->bh)->h_hash);

			/*
			 * This must happen under buffer lock for
			 * ext4_xattr_block_set() to reliably detect modified
			 * block
			 */
			if (ea_block_cache)
				mb_cache_entry_delete(ea_block_cache, hash,
						      bs->bh->b_blocknr);
			ea_bdebug(bs->bh, "modifying in-place");
			error = ext4_xattr_set_entry(i, s, handle, iyesde,
						     true /* is_block */);
			ext4_xattr_block_csum_set(iyesde, bs->bh);
			unlock_buffer(bs->bh);
			if (error == -EFSCORRUPTED)
				goto bad_block;
			if (!error)
				error = ext4_handle_dirty_metadata(handle,
								   iyesde,
								   bs->bh);
			if (error)
				goto cleanup;
			goto inserted;
		} else {
			int offset = (char *)s->here - bs->bh->b_data;

			unlock_buffer(bs->bh);
			ea_bdebug(bs->bh, "cloning");
			s->base = kmalloc(bs->bh->b_size, GFP_NOFS);
			error = -ENOMEM;
			if (s->base == NULL)
				goto cleanup;
			memcpy(s->base, BHDR(bs->bh), bs->bh->b_size);
			s->first = ENTRY(header(s->base)+1);
			header(s->base)->h_refcount = cpu_to_le32(1);
			s->here = ENTRY(s->base + offset);
			s->end = s->base + bs->bh->b_size;

			/*
			 * If existing entry points to an xattr iyesde, we need
			 * to prevent ext4_xattr_set_entry() from decrementing
			 * ref count on it because the reference belongs to the
			 * original block. In this case, make the entry look
			 * like it has an empty value.
			 */
			if (!s->yest_found && s->here->e_value_inum) {
				ea_iyes = le32_to_cpu(s->here->e_value_inum);
				error = ext4_xattr_iyesde_iget(iyesde, ea_iyes,
					      le32_to_cpu(s->here->e_hash),
					      &tmp_iyesde);
				if (error)
					goto cleanup;

				if (!ext4_test_iyesde_state(tmp_iyesde,
						EXT4_STATE_LUSTRE_EA_INODE)) {
					/*
					 * Defer quota free call for previous
					 * iyesde until success is guaranteed.
					 */
					old_ea_iyesde_quota = le32_to_cpu(
							s->here->e_value_size);
				}
				iput(tmp_iyesde);

				s->here->e_value_inum = 0;
				s->here->e_value_size = 0;
			}
		}
	} else {
		/* Allocate a buffer where we construct the new block. */
		s->base = kzalloc(sb->s_blocksize, GFP_NOFS);
		/* assert(header == s->base) */
		error = -ENOMEM;
		if (s->base == NULL)
			goto cleanup;
		header(s->base)->h_magic = cpu_to_le32(EXT4_XATTR_MAGIC);
		header(s->base)->h_blocks = cpu_to_le32(1);
		header(s->base)->h_refcount = cpu_to_le32(1);
		s->first = ENTRY(header(s->base)+1);
		s->here = ENTRY(header(s->base)+1);
		s->end = s->base + sb->s_blocksize;
	}

	error = ext4_xattr_set_entry(i, s, handle, iyesde, true /* is_block */);
	if (error == -EFSCORRUPTED)
		goto bad_block;
	if (error)
		goto cleanup;

	if (i->value && s->here->e_value_inum) {
		/*
		 * A ref count on ea_iyesde has been taken as part of the call to
		 * ext4_xattr_set_entry() above. We would like to drop this
		 * extra ref but we have to wait until the xattr block is
		 * initialized and has its own ref count on the ea_iyesde.
		 */
		ea_iyes = le32_to_cpu(s->here->e_value_inum);
		error = ext4_xattr_iyesde_iget(iyesde, ea_iyes,
					      le32_to_cpu(s->here->e_hash),
					      &ea_iyesde);
		if (error) {
			ea_iyesde = NULL;
			goto cleanup;
		}
	}

inserted:
	if (!IS_LAST_ENTRY(s->first)) {
		new_bh = ext4_xattr_block_cache_find(iyesde, header(s->base),
						     &ce);
		if (new_bh) {
			/* We found an identical block in the cache. */
			if (new_bh == bs->bh)
				ea_bdebug(new_bh, "keeping");
			else {
				u32 ref;

				WARN_ON_ONCE(dquot_initialize_needed(iyesde));

				/* The old block is released after updating
				   the iyesde. */
				error = dquot_alloc_block(iyesde,
						EXT4_C2B(EXT4_SB(sb), 1));
				if (error)
					goto cleanup;
				BUFFER_TRACE(new_bh, "get_write_access");
				error = ext4_journal_get_write_access(handle,
								      new_bh);
				if (error)
					goto cleanup_dquot;
				lock_buffer(new_bh);
				/*
				 * We have to be careful about races with
				 * freeing, rehashing or adding references to
				 * xattr block. Once we hold buffer lock xattr
				 * block's state is stable so we can check
				 * whether the block got freed / rehashed or
				 * yest.  Since we unhash mbcache entry under
				 * buffer lock when freeing / rehashing xattr
				 * block, checking whether entry is still
				 * hashed is reliable. Same rules hold for
				 * e_reusable handling.
				 */
				if (hlist_bl_unhashed(&ce->e_hash_list) ||
				    !ce->e_reusable) {
					/*
					 * Undo everything and check mbcache
					 * again.
					 */
					unlock_buffer(new_bh);
					dquot_free_block(iyesde,
							 EXT4_C2B(EXT4_SB(sb),
								  1));
					brelse(new_bh);
					mb_cache_entry_put(ea_block_cache, ce);
					ce = NULL;
					new_bh = NULL;
					goto inserted;
				}
				ref = le32_to_cpu(BHDR(new_bh)->h_refcount) + 1;
				BHDR(new_bh)->h_refcount = cpu_to_le32(ref);
				if (ref >= EXT4_XATTR_REFCOUNT_MAX)
					ce->e_reusable = 0;
				ea_bdebug(new_bh, "reusing; refcount yesw=%d",
					  ref);
				ext4_xattr_block_csum_set(iyesde, new_bh);
				unlock_buffer(new_bh);
				error = ext4_handle_dirty_metadata(handle,
								   iyesde,
								   new_bh);
				if (error)
					goto cleanup_dquot;
			}
			mb_cache_entry_touch(ea_block_cache, ce);
			mb_cache_entry_put(ea_block_cache, ce);
			ce = NULL;
		} else if (bs->bh && s->base == bs->bh->b_data) {
			/* We were modifying this block in-place. */
			ea_bdebug(bs->bh, "keeping this block");
			ext4_xattr_block_cache_insert(ea_block_cache, bs->bh);
			new_bh = bs->bh;
			get_bh(new_bh);
		} else {
			/* We need to allocate a new block */
			ext4_fsblk_t goal, block;

			WARN_ON_ONCE(dquot_initialize_needed(iyesde));

			goal = ext4_group_first_block_yes(sb,
						EXT4_I(iyesde)->i_block_group);

			/* yesn-extent files can't have physical blocks past 2^32 */
			if (!(ext4_test_iyesde_flag(iyesde, EXT4_INODE_EXTENTS)))
				goal = goal & EXT4_MAX_BLOCK_FILE_PHYS;

			block = ext4_new_meta_blocks(handle, iyesde, goal, 0,
						     NULL, &error);
			if (error)
				goto cleanup;

			if (!(ext4_test_iyesde_flag(iyesde, EXT4_INODE_EXTENTS)))
				BUG_ON(block > EXT4_MAX_BLOCK_FILE_PHYS);

			ea_idebug(iyesde, "creating block %llu",
				  (unsigned long long)block);

			new_bh = sb_getblk(sb, block);
			if (unlikely(!new_bh)) {
				error = -ENOMEM;
getblk_failed:
				ext4_free_blocks(handle, iyesde, NULL, block, 1,
						 EXT4_FREE_BLOCKS_METADATA);
				goto cleanup;
			}
			error = ext4_xattr_iyesde_inc_ref_all(handle, iyesde,
						      ENTRY(header(s->base)+1));
			if (error)
				goto getblk_failed;
			if (ea_iyesde) {
				/* Drop the extra ref on ea_iyesde. */
				error = ext4_xattr_iyesde_dec_ref(handle,
								 ea_iyesde);
				if (error)
					ext4_warning_iyesde(ea_iyesde,
							   "dec ref error=%d",
							   error);
				iput(ea_iyesde);
				ea_iyesde = NULL;
			}

			lock_buffer(new_bh);
			error = ext4_journal_get_create_access(handle, new_bh);
			if (error) {
				unlock_buffer(new_bh);
				error = -EIO;
				goto getblk_failed;
			}
			memcpy(new_bh->b_data, s->base, new_bh->b_size);
			ext4_xattr_block_csum_set(iyesde, new_bh);
			set_buffer_uptodate(new_bh);
			unlock_buffer(new_bh);
			ext4_xattr_block_cache_insert(ea_block_cache, new_bh);
			error = ext4_handle_dirty_metadata(handle, iyesde,
							   new_bh);
			if (error)
				goto cleanup;
		}
	}

	if (old_ea_iyesde_quota)
		ext4_xattr_iyesde_free_quota(iyesde, NULL, old_ea_iyesde_quota);

	/* Update the iyesde. */
	EXT4_I(iyesde)->i_file_acl = new_bh ? new_bh->b_blocknr : 0;

	/* Drop the previous xattr block. */
	if (bs->bh && bs->bh != new_bh) {
		struct ext4_xattr_iyesde_array *ea_iyesde_array = NULL;

		ext4_xattr_release_block(handle, iyesde, bs->bh,
					 &ea_iyesde_array,
					 0 /* extra_credits */);
		ext4_xattr_iyesde_array_free(ea_iyesde_array);
	}
	error = 0;

cleanup:
	if (ea_iyesde) {
		int error2;

		error2 = ext4_xattr_iyesde_dec_ref(handle, ea_iyesde);
		if (error2)
			ext4_warning_iyesde(ea_iyesde, "dec ref error=%d",
					   error2);

		/* If there was an error, revert the quota charge. */
		if (error)
			ext4_xattr_iyesde_free_quota(iyesde, ea_iyesde,
						    i_size_read(ea_iyesde));
		iput(ea_iyesde);
	}
	if (ce)
		mb_cache_entry_put(ea_block_cache, ce);
	brelse(new_bh);
	if (!(bs->bh && s->base == bs->bh->b_data))
		kfree(s->base);

	return error;

cleanup_dquot:
	dquot_free_block(iyesde, EXT4_C2B(EXT4_SB(sb), 1));
	goto cleanup;

bad_block:
	EXT4_ERROR_INODE(iyesde, "bad block %llu",
			 EXT4_I(iyesde)->i_file_acl);
	goto cleanup;

#undef header
}

int ext4_xattr_ibody_find(struct iyesde *iyesde, struct ext4_xattr_info *i,
			  struct ext4_xattr_ibody_find *is)
{
	struct ext4_xattr_ibody_header *header;
	struct ext4_iyesde *raw_iyesde;
	int error;

	if (EXT4_I(iyesde)->i_extra_isize == 0)
		return 0;
	raw_iyesde = ext4_raw_iyesde(&is->iloc);
	header = IHDR(iyesde, raw_iyesde);
	is->s.base = is->s.first = IFIRST(header);
	is->s.here = is->s.first;
	is->s.end = (void *)raw_iyesde + EXT4_SB(iyesde->i_sb)->s_iyesde_size;
	if (ext4_test_iyesde_state(iyesde, EXT4_STATE_XATTR)) {
		error = xattr_check_iyesde(iyesde, header, is->s.end);
		if (error)
			return error;
		/* Find the named attribute. */
		error = xattr_find_entry(iyesde, &is->s.here, is->s.end,
					 i->name_index, i->name, 0);
		if (error && error != -ENODATA)
			return error;
		is->s.yest_found = error;
	}
	return 0;
}

int ext4_xattr_ibody_inline_set(handle_t *handle, struct iyesde *iyesde,
				struct ext4_xattr_info *i,
				struct ext4_xattr_ibody_find *is)
{
	struct ext4_xattr_ibody_header *header;
	struct ext4_xattr_search *s = &is->s;
	int error;

	if (EXT4_I(iyesde)->i_extra_isize == 0)
		return -ENOSPC;
	error = ext4_xattr_set_entry(i, s, handle, iyesde, false /* is_block */);
	if (error)
		return error;
	header = IHDR(iyesde, ext4_raw_iyesde(&is->iloc));
	if (!IS_LAST_ENTRY(s->first)) {
		header->h_magic = cpu_to_le32(EXT4_XATTR_MAGIC);
		ext4_set_iyesde_state(iyesde, EXT4_STATE_XATTR);
	} else {
		header->h_magic = cpu_to_le32(0);
		ext4_clear_iyesde_state(iyesde, EXT4_STATE_XATTR);
	}
	return 0;
}

static int ext4_xattr_ibody_set(handle_t *handle, struct iyesde *iyesde,
				struct ext4_xattr_info *i,
				struct ext4_xattr_ibody_find *is)
{
	struct ext4_xattr_ibody_header *header;
	struct ext4_xattr_search *s = &is->s;
	int error;

	if (EXT4_I(iyesde)->i_extra_isize == 0)
		return -ENOSPC;
	error = ext4_xattr_set_entry(i, s, handle, iyesde, false /* is_block */);
	if (error)
		return error;
	header = IHDR(iyesde, ext4_raw_iyesde(&is->iloc));
	if (!IS_LAST_ENTRY(s->first)) {
		header->h_magic = cpu_to_le32(EXT4_XATTR_MAGIC);
		ext4_set_iyesde_state(iyesde, EXT4_STATE_XATTR);
	} else {
		header->h_magic = cpu_to_le32(0);
		ext4_clear_iyesde_state(iyesde, EXT4_STATE_XATTR);
	}
	return 0;
}

static int ext4_xattr_value_same(struct ext4_xattr_search *s,
				 struct ext4_xattr_info *i)
{
	void *value;

	/* When e_value_inum is set the value is stored externally. */
	if (s->here->e_value_inum)
		return 0;
	if (le32_to_cpu(s->here->e_value_size) != i->value_len)
		return 0;
	value = ((void *)s->base) + le16_to_cpu(s->here->e_value_offs);
	return !memcmp(value, i->value, i->value_len);
}

static struct buffer_head *ext4_xattr_get_block(struct iyesde *iyesde)
{
	struct buffer_head *bh;
	int error;

	if (!EXT4_I(iyesde)->i_file_acl)
		return NULL;
	bh = ext4_sb_bread(iyesde->i_sb, EXT4_I(iyesde)->i_file_acl, REQ_PRIO);
	if (IS_ERR(bh))
		return bh;
	error = ext4_xattr_check_block(iyesde, bh);
	if (error) {
		brelse(bh);
		return ERR_PTR(error);
	}
	return bh;
}

/*
 * ext4_xattr_set_handle()
 *
 * Create, replace or remove an extended attribute for this iyesde.  Value
 * is NULL to remove an existing extended attribute, and yesn-NULL to
 * either replace an existing extended attribute, or create a new extended
 * attribute. The flags XATTR_REPLACE and XATTR_CREATE
 * specify that an extended attribute must exist and must yest exist
 * previous to the call, respectively.
 *
 * Returns 0, or a negative error number on failure.
 */
int
ext4_xattr_set_handle(handle_t *handle, struct iyesde *iyesde, int name_index,
		      const char *name, const void *value, size_t value_len,
		      int flags)
{
	struct ext4_xattr_info i = {
		.name_index = name_index,
		.name = name,
		.value = value,
		.value_len = value_len,
		.in_iyesde = 0,
	};
	struct ext4_xattr_ibody_find is = {
		.s = { .yest_found = -ENODATA, },
	};
	struct ext4_xattr_block_find bs = {
		.s = { .yest_found = -ENODATA, },
	};
	int yes_expand;
	int error;

	if (!name)
		return -EINVAL;
	if (strlen(name) > 255)
		return -ERANGE;

	ext4_write_lock_xattr(iyesde, &yes_expand);

	/* Check journal credits under write lock. */
	if (ext4_handle_valid(handle)) {
		struct buffer_head *bh;
		int credits;

		bh = ext4_xattr_get_block(iyesde);
		if (IS_ERR(bh)) {
			error = PTR_ERR(bh);
			goto cleanup;
		}

		credits = __ext4_xattr_set_credits(iyesde->i_sb, iyesde, bh,
						   value_len,
						   flags & XATTR_CREATE);
		brelse(bh);

		if (jbd2_handle_buffer_credits(handle) < credits) {
			error = -ENOSPC;
			goto cleanup;
		}
	}

	error = ext4_reserve_iyesde_write(handle, iyesde, &is.iloc);
	if (error)
		goto cleanup;

	if (ext4_test_iyesde_state(iyesde, EXT4_STATE_NEW)) {
		struct ext4_iyesde *raw_iyesde = ext4_raw_iyesde(&is.iloc);
		memset(raw_iyesde, 0, EXT4_SB(iyesde->i_sb)->s_iyesde_size);
		ext4_clear_iyesde_state(iyesde, EXT4_STATE_NEW);
	}

	error = ext4_xattr_ibody_find(iyesde, &i, &is);
	if (error)
		goto cleanup;
	if (is.s.yest_found)
		error = ext4_xattr_block_find(iyesde, &i, &bs);
	if (error)
		goto cleanup;
	if (is.s.yest_found && bs.s.yest_found) {
		error = -ENODATA;
		if (flags & XATTR_REPLACE)
			goto cleanup;
		error = 0;
		if (!value)
			goto cleanup;
	} else {
		error = -EEXIST;
		if (flags & XATTR_CREATE)
			goto cleanup;
	}

	if (!value) {
		if (!is.s.yest_found)
			error = ext4_xattr_ibody_set(handle, iyesde, &i, &is);
		else if (!bs.s.yest_found)
			error = ext4_xattr_block_set(handle, iyesde, &i, &bs);
	} else {
		error = 0;
		/* Xattr value did yest change? Save us some work and bail out */
		if (!is.s.yest_found && ext4_xattr_value_same(&is.s, &i))
			goto cleanup;
		if (!bs.s.yest_found && ext4_xattr_value_same(&bs.s, &i))
			goto cleanup;

		if (ext4_has_feature_ea_iyesde(iyesde->i_sb) &&
		    (EXT4_XATTR_SIZE(i.value_len) >
			EXT4_XATTR_MIN_LARGE_EA_SIZE(iyesde->i_sb->s_blocksize)))
			i.in_iyesde = 1;
retry_iyesde:
		error = ext4_xattr_ibody_set(handle, iyesde, &i, &is);
		if (!error && !bs.s.yest_found) {
			i.value = NULL;
			error = ext4_xattr_block_set(handle, iyesde, &i, &bs);
		} else if (error == -ENOSPC) {
			if (EXT4_I(iyesde)->i_file_acl && !bs.s.base) {
				brelse(bs.bh);
				bs.bh = NULL;
				error = ext4_xattr_block_find(iyesde, &i, &bs);
				if (error)
					goto cleanup;
			}
			error = ext4_xattr_block_set(handle, iyesde, &i, &bs);
			if (!error && !is.s.yest_found) {
				i.value = NULL;
				error = ext4_xattr_ibody_set(handle, iyesde, &i,
							     &is);
			} else if (error == -ENOSPC) {
				/*
				 * Xattr does yest fit in the block, store at
				 * external iyesde if possible.
				 */
				if (ext4_has_feature_ea_iyesde(iyesde->i_sb) &&
				    !i.in_iyesde) {
					i.in_iyesde = 1;
					goto retry_iyesde;
				}
			}
		}
	}
	if (!error) {
		ext4_xattr_update_super_block(handle, iyesde->i_sb);
		iyesde->i_ctime = current_time(iyesde);
		if (!value)
			yes_expand = 0;
		error = ext4_mark_iloc_dirty(handle, iyesde, &is.iloc);
		/*
		 * The bh is consumed by ext4_mark_iloc_dirty, even with
		 * error != 0.
		 */
		is.iloc.bh = NULL;
		if (IS_SYNC(iyesde))
			ext4_handle_sync(handle);
	}

cleanup:
	brelse(is.iloc.bh);
	brelse(bs.bh);
	ext4_write_unlock_xattr(iyesde, &yes_expand);
	return error;
}

int ext4_xattr_set_credits(struct iyesde *iyesde, size_t value_len,
			   bool is_create, int *credits)
{
	struct buffer_head *bh;
	int err;

	*credits = 0;

	if (!EXT4_SB(iyesde->i_sb)->s_journal)
		return 0;

	down_read(&EXT4_I(iyesde)->xattr_sem);

	bh = ext4_xattr_get_block(iyesde);
	if (IS_ERR(bh)) {
		err = PTR_ERR(bh);
	} else {
		*credits = __ext4_xattr_set_credits(iyesde->i_sb, iyesde, bh,
						    value_len, is_create);
		brelse(bh);
		err = 0;
	}

	up_read(&EXT4_I(iyesde)->xattr_sem);
	return err;
}

/*
 * ext4_xattr_set()
 *
 * Like ext4_xattr_set_handle, but start from an iyesde. This extended
 * attribute modification is a filesystem transaction by itself.
 *
 * Returns 0, or a negative error number on failure.
 */
int
ext4_xattr_set(struct iyesde *iyesde, int name_index, const char *name,
	       const void *value, size_t value_len, int flags)
{
	handle_t *handle;
	struct super_block *sb = iyesde->i_sb;
	int error, retries = 0;
	int credits;

	error = dquot_initialize(iyesde);
	if (error)
		return error;

retry:
	error = ext4_xattr_set_credits(iyesde, value_len, flags & XATTR_CREATE,
				       &credits);
	if (error)
		return error;

	handle = ext4_journal_start(iyesde, EXT4_HT_XATTR, credits);
	if (IS_ERR(handle)) {
		error = PTR_ERR(handle);
	} else {
		int error2;

		error = ext4_xattr_set_handle(handle, iyesde, name_index, name,
					      value, value_len, flags);
		error2 = ext4_journal_stop(handle);
		if (error == -ENOSPC &&
		    ext4_should_retry_alloc(sb, &retries))
			goto retry;
		if (error == 0)
			error = error2;
	}

	return error;
}

/*
 * Shift the EA entries in the iyesde to create space for the increased
 * i_extra_isize.
 */
static void ext4_xattr_shift_entries(struct ext4_xattr_entry *entry,
				     int value_offs_shift, void *to,
				     void *from, size_t n)
{
	struct ext4_xattr_entry *last = entry;
	int new_offs;

	/* We always shift xattr headers further thus offsets get lower */
	BUG_ON(value_offs_shift > 0);

	/* Adjust the value offsets of the entries */
	for (; !IS_LAST_ENTRY(last); last = EXT4_XATTR_NEXT(last)) {
		if (!last->e_value_inum && last->e_value_size) {
			new_offs = le16_to_cpu(last->e_value_offs) +
							value_offs_shift;
			last->e_value_offs = cpu_to_le16(new_offs);
		}
	}
	/* Shift the entries by n bytes */
	memmove(to, from, n);
}

/*
 * Move xattr pointed to by 'entry' from iyesde into external xattr block
 */
static int ext4_xattr_move_to_block(handle_t *handle, struct iyesde *iyesde,
				    struct ext4_iyesde *raw_iyesde,
				    struct ext4_xattr_entry *entry)
{
	struct ext4_xattr_ibody_find *is = NULL;
	struct ext4_xattr_block_find *bs = NULL;
	char *buffer = NULL, *b_entry_name = NULL;
	size_t value_size = le32_to_cpu(entry->e_value_size);
	struct ext4_xattr_info i = {
		.value = NULL,
		.value_len = 0,
		.name_index = entry->e_name_index,
		.in_iyesde = !!entry->e_value_inum,
	};
	struct ext4_xattr_ibody_header *header = IHDR(iyesde, raw_iyesde);
	int error;

	is = kzalloc(sizeof(struct ext4_xattr_ibody_find), GFP_NOFS);
	bs = kzalloc(sizeof(struct ext4_xattr_block_find), GFP_NOFS);
	buffer = kmalloc(value_size, GFP_NOFS);
	b_entry_name = kmalloc(entry->e_name_len + 1, GFP_NOFS);
	if (!is || !bs || !buffer || !b_entry_name) {
		error = -ENOMEM;
		goto out;
	}

	is->s.yest_found = -ENODATA;
	bs->s.yest_found = -ENODATA;
	is->iloc.bh = NULL;
	bs->bh = NULL;

	/* Save the entry name and the entry value */
	if (entry->e_value_inum) {
		error = ext4_xattr_iyesde_get(iyesde, entry, buffer, value_size);
		if (error)
			goto out;
	} else {
		size_t value_offs = le16_to_cpu(entry->e_value_offs);
		memcpy(buffer, (void *)IFIRST(header) + value_offs, value_size);
	}

	memcpy(b_entry_name, entry->e_name, entry->e_name_len);
	b_entry_name[entry->e_name_len] = '\0';
	i.name = b_entry_name;

	error = ext4_get_iyesde_loc(iyesde, &is->iloc);
	if (error)
		goto out;

	error = ext4_xattr_ibody_find(iyesde, &i, is);
	if (error)
		goto out;

	/* Remove the chosen entry from the iyesde */
	error = ext4_xattr_ibody_set(handle, iyesde, &i, is);
	if (error)
		goto out;

	i.value = buffer;
	i.value_len = value_size;
	error = ext4_xattr_block_find(iyesde, &i, bs);
	if (error)
		goto out;

	/* Add entry which was removed from the iyesde into the block */
	error = ext4_xattr_block_set(handle, iyesde, &i, bs);
	if (error)
		goto out;
	error = 0;
out:
	kfree(b_entry_name);
	kfree(buffer);
	if (is)
		brelse(is->iloc.bh);
	if (bs)
		brelse(bs->bh);
	kfree(is);
	kfree(bs);

	return error;
}

static int ext4_xattr_make_iyesde_space(handle_t *handle, struct iyesde *iyesde,
				       struct ext4_iyesde *raw_iyesde,
				       int isize_diff, size_t ifree,
				       size_t bfree, int *total_iyes)
{
	struct ext4_xattr_ibody_header *header = IHDR(iyesde, raw_iyesde);
	struct ext4_xattr_entry *small_entry;
	struct ext4_xattr_entry *entry;
	struct ext4_xattr_entry *last;
	unsigned int entry_size;	/* EA entry size */
	unsigned int total_size;	/* EA entry size + value size */
	unsigned int min_total_size;
	int error;

	while (isize_diff > ifree) {
		entry = NULL;
		small_entry = NULL;
		min_total_size = ~0U;
		last = IFIRST(header);
		/* Find the entry best suited to be pushed into EA block */
		for (; !IS_LAST_ENTRY(last); last = EXT4_XATTR_NEXT(last)) {
			/* never move system.data out of the iyesde */
			if ((last->e_name_len == 4) &&
			    (last->e_name_index == EXT4_XATTR_INDEX_SYSTEM) &&
			    !memcmp(last->e_name, "data", 4))
				continue;
			total_size = EXT4_XATTR_LEN(last->e_name_len);
			if (!last->e_value_inum)
				total_size += EXT4_XATTR_SIZE(
					       le32_to_cpu(last->e_value_size));
			if (total_size <= bfree &&
			    total_size < min_total_size) {
				if (total_size + ifree < isize_diff) {
					small_entry = last;
				} else {
					entry = last;
					min_total_size = total_size;
				}
			}
		}

		if (entry == NULL) {
			if (small_entry == NULL)
				return -ENOSPC;
			entry = small_entry;
		}

		entry_size = EXT4_XATTR_LEN(entry->e_name_len);
		total_size = entry_size;
		if (!entry->e_value_inum)
			total_size += EXT4_XATTR_SIZE(
					      le32_to_cpu(entry->e_value_size));
		error = ext4_xattr_move_to_block(handle, iyesde, raw_iyesde,
						 entry);
		if (error)
			return error;

		*total_iyes -= entry_size;
		ifree += total_size;
		bfree -= total_size;
	}

	return 0;
}

/*
 * Expand an iyesde by new_extra_isize bytes when EAs are present.
 * Returns 0 on success or negative error number on failure.
 */
int ext4_expand_extra_isize_ea(struct iyesde *iyesde, int new_extra_isize,
			       struct ext4_iyesde *raw_iyesde, handle_t *handle)
{
	struct ext4_xattr_ibody_header *header;
	struct ext4_sb_info *sbi = EXT4_SB(iyesde->i_sb);
	static unsigned int mnt_count;
	size_t min_offs;
	size_t ifree, bfree;
	int total_iyes;
	void *base, *end;
	int error = 0, tried_min_extra_isize = 0;
	int s_min_extra_isize = le16_to_cpu(sbi->s_es->s_min_extra_isize);
	int isize_diff;	/* How much do we need to grow i_extra_isize */

retry:
	isize_diff = new_extra_isize - EXT4_I(iyesde)->i_extra_isize;
	if (EXT4_I(iyesde)->i_extra_isize >= new_extra_isize)
		return 0;

	header = IHDR(iyesde, raw_iyesde);

	/*
	 * Check if eyesugh free space is available in the iyesde to shift the
	 * entries ahead by new_extra_isize.
	 */

	base = IFIRST(header);
	end = (void *)raw_iyesde + EXT4_SB(iyesde->i_sb)->s_iyesde_size;
	min_offs = end - base;
	total_iyes = sizeof(struct ext4_xattr_ibody_header) + sizeof(u32);

	error = xattr_check_iyesde(iyesde, header, end);
	if (error)
		goto cleanup;

	ifree = ext4_xattr_free_space(base, &min_offs, base, &total_iyes);
	if (ifree >= isize_diff)
		goto shift;

	/*
	 * Eyesugh free space isn't available in the iyesde, check if
	 * EA block can hold new_extra_isize bytes.
	 */
	if (EXT4_I(iyesde)->i_file_acl) {
		struct buffer_head *bh;

		bh = ext4_sb_bread(iyesde->i_sb, EXT4_I(iyesde)->i_file_acl, REQ_PRIO);
		if (IS_ERR(bh)) {
			error = PTR_ERR(bh);
			goto cleanup;
		}
		error = ext4_xattr_check_block(iyesde, bh);
		if (error) {
			brelse(bh);
			goto cleanup;
		}
		base = BHDR(bh);
		end = bh->b_data + bh->b_size;
		min_offs = end - base;
		bfree = ext4_xattr_free_space(BFIRST(bh), &min_offs, base,
					      NULL);
		brelse(bh);
		if (bfree + ifree < isize_diff) {
			if (!tried_min_extra_isize && s_min_extra_isize) {
				tried_min_extra_isize++;
				new_extra_isize = s_min_extra_isize;
				goto retry;
			}
			error = -ENOSPC;
			goto cleanup;
		}
	} else {
		bfree = iyesde->i_sb->s_blocksize;
	}

	error = ext4_xattr_make_iyesde_space(handle, iyesde, raw_iyesde,
					    isize_diff, ifree, bfree,
					    &total_iyes);
	if (error) {
		if (error == -ENOSPC && !tried_min_extra_isize &&
		    s_min_extra_isize) {
			tried_min_extra_isize++;
			new_extra_isize = s_min_extra_isize;
			goto retry;
		}
		goto cleanup;
	}
shift:
	/* Adjust the offsets and shift the remaining entries ahead */
	ext4_xattr_shift_entries(IFIRST(header), EXT4_I(iyesde)->i_extra_isize
			- new_extra_isize, (void *)raw_iyesde +
			EXT4_GOOD_OLD_INODE_SIZE + new_extra_isize,
			(void *)header, total_iyes);
	EXT4_I(iyesde)->i_extra_isize = new_extra_isize;

cleanup:
	if (error && (mnt_count != le16_to_cpu(sbi->s_es->s_mnt_count))) {
		ext4_warning(iyesde->i_sb, "Unable to expand iyesde %lu. Delete some EAs or run e2fsck.",
			     iyesde->i_iyes);
		mnt_count = le16_to_cpu(sbi->s_es->s_mnt_count);
	}
	return error;
}

#define EIA_INCR 16 /* must be 2^n */
#define EIA_MASK (EIA_INCR - 1)

/* Add the large xattr @iyesde into @ea_iyesde_array for deferred iput().
 * If @ea_iyesde_array is new or full it will be grown and the old
 * contents copied over.
 */
static int
ext4_expand_iyesde_array(struct ext4_xattr_iyesde_array **ea_iyesde_array,
			struct iyesde *iyesde)
{
	if (*ea_iyesde_array == NULL) {
		/*
		 * Start with 15 iyesdes, so it fits into a power-of-two size.
		 * If *ea_iyesde_array is NULL, this is essentially offsetof()
		 */
		(*ea_iyesde_array) =
			kmalloc(offsetof(struct ext4_xattr_iyesde_array,
					 iyesdes[EIA_MASK]),
				GFP_NOFS);
		if (*ea_iyesde_array == NULL)
			return -ENOMEM;
		(*ea_iyesde_array)->count = 0;
	} else if (((*ea_iyesde_array)->count & EIA_MASK) == EIA_MASK) {
		/* expand the array once all 15 + n * 16 slots are full */
		struct ext4_xattr_iyesde_array *new_array = NULL;
		int count = (*ea_iyesde_array)->count;

		/* if new_array is NULL, this is essentially offsetof() */
		new_array = kmalloc(
				offsetof(struct ext4_xattr_iyesde_array,
					 iyesdes[count + EIA_INCR]),
				GFP_NOFS);
		if (new_array == NULL)
			return -ENOMEM;
		memcpy(new_array, *ea_iyesde_array,
		       offsetof(struct ext4_xattr_iyesde_array, iyesdes[count]));
		kfree(*ea_iyesde_array);
		*ea_iyesde_array = new_array;
	}
	(*ea_iyesde_array)->iyesdes[(*ea_iyesde_array)->count++] = iyesde;
	return 0;
}

/*
 * ext4_xattr_delete_iyesde()
 *
 * Free extended attribute resources associated with this iyesde. Traverse
 * all entries and decrement reference on any xattr iyesdes associated with this
 * iyesde. This is called immediately before an iyesde is freed. We have exclusive
 * access to the iyesde. If an orphan iyesde is deleted it will also release its
 * references on xattr block and xattr iyesdes.
 */
int ext4_xattr_delete_iyesde(handle_t *handle, struct iyesde *iyesde,
			    struct ext4_xattr_iyesde_array **ea_iyesde_array,
			    int extra_credits)
{
	struct buffer_head *bh = NULL;
	struct ext4_xattr_ibody_header *header;
	struct ext4_iloc iloc = { .bh = NULL };
	struct ext4_xattr_entry *entry;
	struct iyesde *ea_iyesde;
	int error;

	error = ext4_journal_ensure_credits(handle, extra_credits,
			ext4_free_metadata_revoke_credits(iyesde->i_sb, 1));
	if (error < 0) {
		EXT4_ERROR_INODE(iyesde, "ensure credits (error %d)", error);
		goto cleanup;
	}

	if (ext4_has_feature_ea_iyesde(iyesde->i_sb) &&
	    ext4_test_iyesde_state(iyesde, EXT4_STATE_XATTR)) {

		error = ext4_get_iyesde_loc(iyesde, &iloc);
		if (error) {
			EXT4_ERROR_INODE(iyesde, "iyesde loc (error %d)", error);
			goto cleanup;
		}

		error = ext4_journal_get_write_access(handle, iloc.bh);
		if (error) {
			EXT4_ERROR_INODE(iyesde, "write access (error %d)",
					 error);
			goto cleanup;
		}

		header = IHDR(iyesde, ext4_raw_iyesde(&iloc));
		if (header->h_magic == cpu_to_le32(EXT4_XATTR_MAGIC))
			ext4_xattr_iyesde_dec_ref_all(handle, iyesde, iloc.bh,
						     IFIRST(header),
						     false /* block_csum */,
						     ea_iyesde_array,
						     extra_credits,
						     false /* skip_quota */);
	}

	if (EXT4_I(iyesde)->i_file_acl) {
		bh = ext4_sb_bread(iyesde->i_sb, EXT4_I(iyesde)->i_file_acl, REQ_PRIO);
		if (IS_ERR(bh)) {
			error = PTR_ERR(bh);
			if (error == -EIO)
				EXT4_ERROR_INODE(iyesde, "block %llu read error",
						 EXT4_I(iyesde)->i_file_acl);
			bh = NULL;
			goto cleanup;
		}
		error = ext4_xattr_check_block(iyesde, bh);
		if (error)
			goto cleanup;

		if (ext4_has_feature_ea_iyesde(iyesde->i_sb)) {
			for (entry = BFIRST(bh); !IS_LAST_ENTRY(entry);
			     entry = EXT4_XATTR_NEXT(entry)) {
				if (!entry->e_value_inum)
					continue;
				error = ext4_xattr_iyesde_iget(iyesde,
					      le32_to_cpu(entry->e_value_inum),
					      le32_to_cpu(entry->e_hash),
					      &ea_iyesde);
				if (error)
					continue;
				ext4_xattr_iyesde_free_quota(iyesde, ea_iyesde,
					      le32_to_cpu(entry->e_value_size));
				iput(ea_iyesde);
			}

		}

		ext4_xattr_release_block(handle, iyesde, bh, ea_iyesde_array,
					 extra_credits);
		/*
		 * Update i_file_acl value in the same transaction that releases
		 * block.
		 */
		EXT4_I(iyesde)->i_file_acl = 0;
		error = ext4_mark_iyesde_dirty(handle, iyesde);
		if (error) {
			EXT4_ERROR_INODE(iyesde, "mark iyesde dirty (error %d)",
					 error);
			goto cleanup;
		}
	}
	error = 0;
cleanup:
	brelse(iloc.bh);
	brelse(bh);
	return error;
}

void ext4_xattr_iyesde_array_free(struct ext4_xattr_iyesde_array *ea_iyesde_array)
{
	int idx;

	if (ea_iyesde_array == NULL)
		return;

	for (idx = 0; idx < ea_iyesde_array->count; ++idx)
		iput(ea_iyesde_array->iyesdes[idx]);
	kfree(ea_iyesde_array);
}

/*
 * ext4_xattr_block_cache_insert()
 *
 * Create a new entry in the extended attribute block cache, and insert
 * it unless such an entry is already in the cache.
 *
 * Returns 0, or a negative error number on failure.
 */
static void
ext4_xattr_block_cache_insert(struct mb_cache *ea_block_cache,
			      struct buffer_head *bh)
{
	struct ext4_xattr_header *header = BHDR(bh);
	__u32 hash = le32_to_cpu(header->h_hash);
	int reusable = le32_to_cpu(header->h_refcount) <
		       EXT4_XATTR_REFCOUNT_MAX;
	int error;

	if (!ea_block_cache)
		return;
	error = mb_cache_entry_create(ea_block_cache, GFP_NOFS, hash,
				      bh->b_blocknr, reusable);
	if (error) {
		if (error == -EBUSY)
			ea_bdebug(bh, "already in cache");
	} else
		ea_bdebug(bh, "inserting [%x]", (int)hash);
}

/*
 * ext4_xattr_cmp()
 *
 * Compare two extended attribute blocks for equality.
 *
 * Returns 0 if the blocks are equal, 1 if they differ, and
 * a negative error number on errors.
 */
static int
ext4_xattr_cmp(struct ext4_xattr_header *header1,
	       struct ext4_xattr_header *header2)
{
	struct ext4_xattr_entry *entry1, *entry2;

	entry1 = ENTRY(header1+1);
	entry2 = ENTRY(header2+1);
	while (!IS_LAST_ENTRY(entry1)) {
		if (IS_LAST_ENTRY(entry2))
			return 1;
		if (entry1->e_hash != entry2->e_hash ||
		    entry1->e_name_index != entry2->e_name_index ||
		    entry1->e_name_len != entry2->e_name_len ||
		    entry1->e_value_size != entry2->e_value_size ||
		    entry1->e_value_inum != entry2->e_value_inum ||
		    memcmp(entry1->e_name, entry2->e_name, entry1->e_name_len))
			return 1;
		if (!entry1->e_value_inum &&
		    memcmp((char *)header1 + le16_to_cpu(entry1->e_value_offs),
			   (char *)header2 + le16_to_cpu(entry2->e_value_offs),
			   le32_to_cpu(entry1->e_value_size)))
			return 1;

		entry1 = EXT4_XATTR_NEXT(entry1);
		entry2 = EXT4_XATTR_NEXT(entry2);
	}
	if (!IS_LAST_ENTRY(entry2))
		return 1;
	return 0;
}

/*
 * ext4_xattr_block_cache_find()
 *
 * Find an identical extended attribute block.
 *
 * Returns a pointer to the block found, or NULL if such a block was
 * yest found or an error occurred.
 */
static struct buffer_head *
ext4_xattr_block_cache_find(struct iyesde *iyesde,
			    struct ext4_xattr_header *header,
			    struct mb_cache_entry **pce)
{
	__u32 hash = le32_to_cpu(header->h_hash);
	struct mb_cache_entry *ce;
	struct mb_cache *ea_block_cache = EA_BLOCK_CACHE(iyesde);

	if (!ea_block_cache)
		return NULL;
	if (!header->h_hash)
		return NULL;  /* never share */
	ea_idebug(iyesde, "looking for cached blocks [%x]", (int)hash);
	ce = mb_cache_entry_find_first(ea_block_cache, hash);
	while (ce) {
		struct buffer_head *bh;

		bh = ext4_sb_bread(iyesde->i_sb, ce->e_value, REQ_PRIO);
		if (IS_ERR(bh)) {
			if (PTR_ERR(bh) == -ENOMEM)
				return NULL;
			bh = NULL;
			EXT4_ERROR_INODE(iyesde, "block %lu read error",
					 (unsigned long)ce->e_value);
		} else if (ext4_xattr_cmp(header, BHDR(bh)) == 0) {
			*pce = ce;
			return bh;
		}
		brelse(bh);
		ce = mb_cache_entry_find_next(ea_block_cache, ce);
	}
	return NULL;
}

#define NAME_HASH_SHIFT 5
#define VALUE_HASH_SHIFT 16

/*
 * ext4_xattr_hash_entry()
 *
 * Compute the hash of an extended attribute.
 */
static __le32 ext4_xattr_hash_entry(char *name, size_t name_len, __le32 *value,
				    size_t value_count)
{
	__u32 hash = 0;

	while (name_len--) {
		hash = (hash << NAME_HASH_SHIFT) ^
		       (hash >> (8*sizeof(hash) - NAME_HASH_SHIFT)) ^
		       *name++;
	}
	while (value_count--) {
		hash = (hash << VALUE_HASH_SHIFT) ^
		       (hash >> (8*sizeof(hash) - VALUE_HASH_SHIFT)) ^
		       le32_to_cpu(*value++);
	}
	return cpu_to_le32(hash);
}

#undef NAME_HASH_SHIFT
#undef VALUE_HASH_SHIFT

#define BLOCK_HASH_SHIFT 16

/*
 * ext4_xattr_rehash()
 *
 * Re-compute the extended attribute hash value after an entry has changed.
 */
static void ext4_xattr_rehash(struct ext4_xattr_header *header)
{
	struct ext4_xattr_entry *here;
	__u32 hash = 0;

	here = ENTRY(header+1);
	while (!IS_LAST_ENTRY(here)) {
		if (!here->e_hash) {
			/* Block is yest shared if an entry's hash value == 0 */
			hash = 0;
			break;
		}
		hash = (hash << BLOCK_HASH_SHIFT) ^
		       (hash >> (8*sizeof(hash) - BLOCK_HASH_SHIFT)) ^
		       le32_to_cpu(here->e_hash);
		here = EXT4_XATTR_NEXT(here);
	}
	header->h_hash = cpu_to_le32(hash);
}

#undef BLOCK_HASH_SHIFT

#define	HASH_BUCKET_BITS	10

struct mb_cache *
ext4_xattr_create_cache(void)
{
	return mb_cache_create(HASH_BUCKET_BITS);
}

void ext4_xattr_destroy_cache(struct mb_cache *cache)
{
	if (cache)
		mb_cache_destroy(cache);
}

