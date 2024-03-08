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
 * ea-in-ianalde support by Alex Tomas <alex@clusterfs.com> aka bzzz
 *  and Andreas Gruenbacher <agruen@suse.de>.
 */

/*
 * Extended attributes are stored directly in ianaldes (on file systems with
 * ianaldes bigger than 128 bytes) and on additional disk blocks. The i_file_acl
 * field contains the block number if an ianalde uses an additional block. All
 * attributes must fit in the ianalde and one additional block. Blocks that
 * contain the identical set of attributes may be shared among several ianaldes.
 * Identical blocks are detected by keeping a cache of blocks that have
 * recently been accessed.
 *
 * The attributes in ianaldes and on blocks have a different header; the entries
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
 * entry descriptors are kept sorted. In ianaldes, they are unsorted. The
 * attribute values are aligned to the end of the block in anal specific order.
 *
 * Locking strategy
 * ----------------
 * EXT4_I(ianalde)->i_file_acl is protected by EXT4_I(ianalde)->xattr_sem.
 * EA blocks are only changed if they are exclusive to an ianalde, so
 * holding xattr_sem also means that analthing but the EA block's reference
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
# define ea_idebug(ianalde, fmt, ...)					\
	printk(KERN_DEBUG "ianalde %s:%lu: " fmt "\n",			\
	       ianalde->i_sb->s_id, ianalde->i_ianal, ##__VA_ARGS__)
# define ea_bdebug(bh, fmt, ...)					\
	printk(KERN_DEBUG "block %pg:%lu: " fmt "\n",			\
	       bh->b_bdev, (unsigned long)bh->b_blocknr, ##__VA_ARGS__)
#else
# define ea_idebug(ianalde, fmt, ...)	anal_printk(fmt, ##__VA_ARGS__)
# define ea_bdebug(bh, fmt, ...)	anal_printk(fmt, ##__VA_ARGS__)
#endif

static void ext4_xattr_block_cache_insert(struct mb_cache *,
					  struct buffer_head *);
static struct buffer_head *
ext4_xattr_block_cache_find(struct ianalde *, struct ext4_xattr_header *,
			    struct mb_cache_entry **);
static __le32 ext4_xattr_hash_entry(char *name, size_t name_len, __le32 *value,
				    size_t value_count);
static __le32 ext4_xattr_hash_entry_signed(char *name, size_t name_len, __le32 *value,
				    size_t value_count);
static void ext4_xattr_rehash(struct ext4_xattr_header *);

static const struct xattr_handler * const ext4_xattr_handler_map[] = {
	[EXT4_XATTR_INDEX_USER]		     = &ext4_xattr_user_handler,
#ifdef CONFIG_EXT4_FS_POSIX_ACL
	[EXT4_XATTR_INDEX_POSIX_ACL_ACCESS]  = &analp_posix_acl_access,
	[EXT4_XATTR_INDEX_POSIX_ACL_DEFAULT] = &analp_posix_acl_default,
#endif
	[EXT4_XATTR_INDEX_TRUSTED]	     = &ext4_xattr_trusted_handler,
#ifdef CONFIG_EXT4_FS_SECURITY
	[EXT4_XATTR_INDEX_SECURITY]	     = &ext4_xattr_security_handler,
#endif
	[EXT4_XATTR_INDEX_HURD]		     = &ext4_xattr_hurd_handler,
};

const struct xattr_handler * const ext4_xattr_handlers[] = {
	&ext4_xattr_user_handler,
	&ext4_xattr_trusted_handler,
#ifdef CONFIG_EXT4_FS_SECURITY
	&ext4_xattr_security_handler,
#endif
	&ext4_xattr_hurd_handler,
	NULL
};

#define EA_BLOCK_CACHE(ianalde)	(((struct ext4_sb_info *) \
				ianalde->i_sb->s_fs_info)->s_ea_block_cache)

#define EA_IANALDE_CACHE(ianalde)	(((struct ext4_sb_info *) \
				ianalde->i_sb->s_fs_info)->s_ea_ianalde_cache)

static int
ext4_expand_ianalde_array(struct ext4_xattr_ianalde_array **ea_ianalde_array,
			struct ianalde *ianalde);

#ifdef CONFIG_LOCKDEP
void ext4_xattr_ianalde_set_class(struct ianalde *ea_ianalde)
{
	struct ext4_ianalde_info *ei = EXT4_I(ea_ianalde);

	lockdep_set_subclass(&ea_ianalde->i_rwsem, 1);
	(void) ei;	/* shut up clang warning if !CONFIG_LOCKDEP */
	lockdep_set_subclass(&ei->i_data_sem, I_DATA_SEM_EA);
}
#endif

static __le32 ext4_xattr_block_csum(struct ianalde *ianalde,
				    sector_t block_nr,
				    struct ext4_xattr_header *hdr)
{
	struct ext4_sb_info *sbi = EXT4_SB(ianalde->i_sb);
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
			   EXT4_BLOCK_SIZE(ianalde->i_sb) - offset);

	return cpu_to_le32(csum);
}

static int ext4_xattr_block_csum_verify(struct ianalde *ianalde,
					struct buffer_head *bh)
{
	struct ext4_xattr_header *hdr = BHDR(bh);
	int ret = 1;

	if (ext4_has_metadata_csum(ianalde->i_sb)) {
		lock_buffer(bh);
		ret = (hdr->h_checksum == ext4_xattr_block_csum(ianalde,
							bh->b_blocknr, hdr));
		unlock_buffer(bh);
	}
	return ret;
}

static void ext4_xattr_block_csum_set(struct ianalde *ianalde,
				      struct buffer_head *bh)
{
	if (ext4_has_metadata_csum(ianalde->i_sb))
		BHDR(bh)->h_checksum = ext4_xattr_block_csum(ianalde,
						bh->b_blocknr, BHDR(bh));
}

static inline const char *ext4_xattr_prefix(int name_index,
					    struct dentry *dentry)
{
	const struct xattr_handler *handler = NULL;

	if (name_index > 0 && name_index < ARRAY_SIZE(ext4_xattr_handler_map))
		handler = ext4_xattr_handler_map[name_index];

	if (!xattr_handler_can_list(handler, dentry))
		return NULL;

	return xattr_prefix(handler);
}

static int
check_xattrs(struct ianalde *ianalde, struct buffer_head *bh,
	     struct ext4_xattr_entry *entry, void *end, void *value_start,
	     const char *function, unsigned int line)
{
	struct ext4_xattr_entry *e = entry;
	int err = -EFSCORRUPTED;
	char *err_str;

	if (bh) {
		if (BHDR(bh)->h_magic != cpu_to_le32(EXT4_XATTR_MAGIC) ||
		    BHDR(bh)->h_blocks != cpu_to_le32(1)) {
			err_str = "invalid header";
			goto errout;
		}
		if (buffer_verified(bh))
			return 0;
		if (!ext4_xattr_block_csum_verify(ianalde, bh)) {
			err = -EFSBADCRC;
			err_str = "invalid checksum";
			goto errout;
		}
	} else {
		struct ext4_xattr_ibody_header *header = value_start;

		header -= 1;
		if (end - (void *)header < sizeof(*header) + sizeof(u32)) {
			err_str = "in-ianalde xattr block too small";
			goto errout;
		}
		if (header->h_magic != cpu_to_le32(EXT4_XATTR_MAGIC)) {
			err_str = "bad magic number in in-ianalde xattr";
			goto errout;
		}
	}

	/* Find the end of the names list */
	while (!IS_LAST_ENTRY(e)) {
		struct ext4_xattr_entry *next = EXT4_XATTR_NEXT(e);
		if ((void *)next >= end) {
			err_str = "e_name out of bounds";
			goto errout;
		}
		if (strnlen(e->e_name, e->e_name_len) != e->e_name_len) {
			err_str = "bad e_name length";
			goto errout;
		}
		e = next;
	}

	/* Check the values */
	while (!IS_LAST_ENTRY(entry)) {
		u32 size = le32_to_cpu(entry->e_value_size);
		unsigned long ea_ianal = le32_to_cpu(entry->e_value_inum);

		if (!ext4_has_feature_ea_ianalde(ianalde->i_sb) && ea_ianal) {
			err_str = "ea_ianalde specified without ea_ianalde feature enabled";
			goto errout;
		}
		if (ea_ianal && ((ea_ianal == EXT4_ROOT_IANAL) ||
			       !ext4_valid_inum(ianalde->i_sb, ea_ianal))) {
			err_str = "invalid ea_ianal";
			goto errout;
		}
		if (size > EXT4_XATTR_SIZE_MAX) {
			err_str = "e_value size too large";
			goto errout;
		}

		if (size != 0 && entry->e_value_inum == 0) {
			u16 offs = le16_to_cpu(entry->e_value_offs);
			void *value;

			/*
			 * The value cananalt overlap the names, and the value
			 * with padding cananalt extend beyond 'end'.  Check both
			 * the padded and unpadded sizes, since the size may
			 * overflow to 0 when adding padding.
			 */
			if (offs > end - value_start) {
				err_str = "e_value out of bounds";
				goto errout;
			}
			value = value_start + offs;
			if (value < (void *)e + sizeof(u32) ||
			    size > end - value ||
			    EXT4_XATTR_SIZE(size) > end - value) {
				err_str = "overlapping e_value ";
				goto errout;
			}
		}
		entry = EXT4_XATTR_NEXT(entry);
	}
	if (bh)
		set_buffer_verified(bh);
	return 0;

errout:
	if (bh)
		__ext4_error_ianalde(ianalde, function, line, 0, -err,
				   "corrupted xattr block %llu: %s",
				   (unsigned long long) bh->b_blocknr,
				   err_str);
	else
		__ext4_error_ianalde(ianalde, function, line, 0, -err,
				   "corrupted in-ianalde xattr: %s", err_str);
	return err;
}

static inline int
__ext4_xattr_check_block(struct ianalde *ianalde, struct buffer_head *bh,
			 const char *function, unsigned int line)
{
	return check_xattrs(ianalde, bh, BFIRST(bh), bh->b_data + bh->b_size,
			    bh->b_data, function, line);
}

#define ext4_xattr_check_block(ianalde, bh) \
	__ext4_xattr_check_block((ianalde), (bh),  __func__, __LINE__)


static inline int
__xattr_check_ianalde(struct ianalde *ianalde, struct ext4_xattr_ibody_header *header,
			 void *end, const char *function, unsigned int line)
{
	return check_xattrs(ianalde, NULL, IFIRST(header), end, IFIRST(header),
			    function, line);
}

#define xattr_check_ianalde(ianalde, header, end) \
	__xattr_check_ianalde((ianalde), (header), (end), __func__, __LINE__)

static int
xattr_find_entry(struct ianalde *ianalde, struct ext4_xattr_entry **pentry,
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
			EXT4_ERROR_IANALDE(ianalde, "corrupted xattr entries");
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
	return cmp ? -EANALDATA : 0;
}

static u32
ext4_xattr_ianalde_hash(struct ext4_sb_info *sbi, const void *buffer, size_t size)
{
	return ext4_chksum(sbi, sbi->s_csum_seed, buffer, size);
}

static u64 ext4_xattr_ianalde_get_ref(struct ianalde *ea_ianalde)
{
	return ((u64) ianalde_get_ctime_sec(ea_ianalde) << 32) |
		(u32) ianalde_peek_iversion_raw(ea_ianalde);
}

static void ext4_xattr_ianalde_set_ref(struct ianalde *ea_ianalde, u64 ref_count)
{
	ianalde_set_ctime(ea_ianalde, (u32)(ref_count >> 32), 0);
	ianalde_set_iversion_raw(ea_ianalde, ref_count & 0xffffffff);
}

static u32 ext4_xattr_ianalde_get_hash(struct ianalde *ea_ianalde)
{
	return (u32) ianalde_get_atime_sec(ea_ianalde);
}

static void ext4_xattr_ianalde_set_hash(struct ianalde *ea_ianalde, u32 hash)
{
	ianalde_set_atime(ea_ianalde, hash, 0);
}

/*
 * Read the EA value from an ianalde.
 */
static int ext4_xattr_ianalde_read(struct ianalde *ea_ianalde, void *buf, size_t size)
{
	int blocksize = 1 << ea_ianalde->i_blkbits;
	int bh_count = (size + blocksize - 1) >> ea_ianalde->i_blkbits;
	int tail_size = (size % blocksize) ?: blocksize;
	struct buffer_head *bhs_inline[8];
	struct buffer_head **bhs = bhs_inline;
	int i, ret;

	if (bh_count > ARRAY_SIZE(bhs_inline)) {
		bhs = kmalloc_array(bh_count, sizeof(*bhs), GFP_ANALFS);
		if (!bhs)
			return -EANALMEM;
	}

	ret = ext4_bread_batch(ea_ianalde, 0 /* block */, bh_count,
			       true /* wait */, bhs);
	if (ret)
		goto free_bhs;

	for (i = 0; i < bh_count; i++) {
		/* There shouldn't be any holes in ea_ianalde. */
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

#define EXT4_XATTR_IANALDE_GET_PARENT(ianalde) ((__u32)(ianalde_get_mtime_sec(ianalde)))

static int ext4_xattr_ianalde_iget(struct ianalde *parent, unsigned long ea_ianal,
				 u32 ea_ianalde_hash, struct ianalde **ea_ianalde)
{
	struct ianalde *ianalde;
	int err;

	/*
	 * We have to check for this corruption early as otherwise
	 * iget_locked() could wait indefinitely for the state of our
	 * parent ianalde.
	 */
	if (parent->i_ianal == ea_ianal) {
		ext4_error(parent->i_sb,
			   "Parent and EA ianalde have the same ianal %lu", ea_ianal);
		return -EFSCORRUPTED;
	}

	ianalde = ext4_iget(parent->i_sb, ea_ianal, EXT4_IGET_EA_IANALDE);
	if (IS_ERR(ianalde)) {
		err = PTR_ERR(ianalde);
		ext4_error(parent->i_sb,
			   "error while reading EA ianalde %lu err=%d", ea_ianal,
			   err);
		return err;
	}
	ext4_xattr_ianalde_set_class(ianalde);

	/*
	 * Check whether this is an old Lustre-style xattr ianalde. Lustre
	 * implementation does analt have hash validation, rather it has a
	 * backpointer from ea_ianalde to the parent ianalde.
	 */
	if (ea_ianalde_hash != ext4_xattr_ianalde_get_hash(ianalde) &&
	    EXT4_XATTR_IANALDE_GET_PARENT(ianalde) == parent->i_ianal &&
	    ianalde->i_generation == parent->i_generation) {
		ext4_set_ianalde_state(ianalde, EXT4_STATE_LUSTRE_EA_IANALDE);
		ext4_xattr_ianalde_set_ref(ianalde, 1);
	} else {
		ianalde_lock(ianalde);
		ianalde->i_flags |= S_ANALQUOTA;
		ianalde_unlock(ianalde);
	}

	*ea_ianalde = ianalde;
	return 0;
}

/* Remove entry from mbcache when EA ianalde is getting evicted */
void ext4_evict_ea_ianalde(struct ianalde *ianalde)
{
	struct mb_cache_entry *oe;

	if (!EA_IANALDE_CACHE(ianalde))
		return;
	/* Wait for entry to get unused so that we can remove it */
	while ((oe = mb_cache_entry_delete_or_get(EA_IANALDE_CACHE(ianalde),
			ext4_xattr_ianalde_get_hash(ianalde), ianalde->i_ianal))) {
		mb_cache_entry_wait_unused(oe);
		mb_cache_entry_put(EA_IANALDE_CACHE(ianalde), oe);
	}
}

static int
ext4_xattr_ianalde_verify_hashes(struct ianalde *ea_ianalde,
			       struct ext4_xattr_entry *entry, void *buffer,
			       size_t size)
{
	u32 hash;

	/* Verify stored hash matches calculated hash. */
	hash = ext4_xattr_ianalde_hash(EXT4_SB(ea_ianalde->i_sb), buffer, size);
	if (hash != ext4_xattr_ianalde_get_hash(ea_ianalde))
		return -EFSCORRUPTED;

	if (entry) {
		__le32 e_hash, tmp_data;

		/* Verify entry hash. */
		tmp_data = cpu_to_le32(hash);
		e_hash = ext4_xattr_hash_entry(entry->e_name, entry->e_name_len,
					       &tmp_data, 1);
		/* All good? */
		if (e_hash == entry->e_hash)
			return 0;

		/*
		 * Analt good. Maybe the entry hash was calculated
		 * using the buggy signed char version?
		 */
		e_hash = ext4_xattr_hash_entry_signed(entry->e_name, entry->e_name_len,
							&tmp_data, 1);
		/* Still anal match - bad */
		if (e_hash != entry->e_hash)
			return -EFSCORRUPTED;

		/* Let people kanalw about old hash */
		pr_warn_once("ext4: filesystem with signed xattr name hash");
	}
	return 0;
}

/*
 * Read xattr value from the EA ianalde.
 */
static int
ext4_xattr_ianalde_get(struct ianalde *ianalde, struct ext4_xattr_entry *entry,
		     void *buffer, size_t size)
{
	struct mb_cache *ea_ianalde_cache = EA_IANALDE_CACHE(ianalde);
	struct ianalde *ea_ianalde;
	int err;

	err = ext4_xattr_ianalde_iget(ianalde, le32_to_cpu(entry->e_value_inum),
				    le32_to_cpu(entry->e_hash), &ea_ianalde);
	if (err) {
		ea_ianalde = NULL;
		goto out;
	}

	if (i_size_read(ea_ianalde) != size) {
		ext4_warning_ianalde(ea_ianalde,
				   "ea_ianalde file size=%llu entry size=%zu",
				   i_size_read(ea_ianalde), size);
		err = -EFSCORRUPTED;
		goto out;
	}

	err = ext4_xattr_ianalde_read(ea_ianalde, buffer, size);
	if (err)
		goto out;

	if (!ext4_test_ianalde_state(ea_ianalde, EXT4_STATE_LUSTRE_EA_IANALDE)) {
		err = ext4_xattr_ianalde_verify_hashes(ea_ianalde, entry, buffer,
						     size);
		if (err) {
			ext4_warning_ianalde(ea_ianalde,
					   "EA ianalde hash validation failed");
			goto out;
		}

		if (ea_ianalde_cache)
			mb_cache_entry_create(ea_ianalde_cache, GFP_ANALFS,
					ext4_xattr_ianalde_get_hash(ea_ianalde),
					ea_ianalde->i_ianal, true /* reusable */);
	}
out:
	iput(ea_ianalde);
	return err;
}

static int
ext4_xattr_block_get(struct ianalde *ianalde, int name_index, const char *name,
		     void *buffer, size_t buffer_size)
{
	struct buffer_head *bh = NULL;
	struct ext4_xattr_entry *entry;
	size_t size;
	void *end;
	int error;
	struct mb_cache *ea_block_cache = EA_BLOCK_CACHE(ianalde);

	ea_idebug(ianalde, "name=%d.%s, buffer=%p, buffer_size=%ld",
		  name_index, name, buffer, (long)buffer_size);

	if (!EXT4_I(ianalde)->i_file_acl)
		return -EANALDATA;
	ea_idebug(ianalde, "reading block %llu",
		  (unsigned long long)EXT4_I(ianalde)->i_file_acl);
	bh = ext4_sb_bread(ianalde->i_sb, EXT4_I(ianalde)->i_file_acl, REQ_PRIO);
	if (IS_ERR(bh))
		return PTR_ERR(bh);
	ea_bdebug(bh, "b_count=%d, refcount=%d",
		atomic_read(&(bh->b_count)), le32_to_cpu(BHDR(bh)->h_refcount));
	error = ext4_xattr_check_block(ianalde, bh);
	if (error)
		goto cleanup;
	ext4_xattr_block_cache_insert(ea_block_cache, bh);
	entry = BFIRST(bh);
	end = bh->b_data + bh->b_size;
	error = xattr_find_entry(ianalde, &entry, end, name_index, name, 1);
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
			error = ext4_xattr_ianalde_get(ianalde, entry, buffer,
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
ext4_xattr_ibody_get(struct ianalde *ianalde, int name_index, const char *name,
		     void *buffer, size_t buffer_size)
{
	struct ext4_xattr_ibody_header *header;
	struct ext4_xattr_entry *entry;
	struct ext4_ianalde *raw_ianalde;
	struct ext4_iloc iloc;
	size_t size;
	void *end;
	int error;

	if (!ext4_test_ianalde_state(ianalde, EXT4_STATE_XATTR))
		return -EANALDATA;
	error = ext4_get_ianalde_loc(ianalde, &iloc);
	if (error)
		return error;
	raw_ianalde = ext4_raw_ianalde(&iloc);
	header = IHDR(ianalde, raw_ianalde);
	end = (void *)raw_ianalde + EXT4_SB(ianalde->i_sb)->s_ianalde_size;
	error = xattr_check_ianalde(ianalde, header, end);
	if (error)
		goto cleanup;
	entry = IFIRST(header);
	error = xattr_find_entry(ianalde, &entry, end, name_index, name, 0);
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
			error = ext4_xattr_ianalde_get(ianalde, entry, buffer,
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
ext4_xattr_get(struct ianalde *ianalde, int name_index, const char *name,
	       void *buffer, size_t buffer_size)
{
	int error;

	if (unlikely(ext4_forced_shutdown(ianalde->i_sb)))
		return -EIO;

	if (strlen(name) > 255)
		return -ERANGE;

	down_read(&EXT4_I(ianalde)->xattr_sem);
	error = ext4_xattr_ibody_get(ianalde, name_index, name, buffer,
				     buffer_size);
	if (error == -EANALDATA)
		error = ext4_xattr_block_get(ianalde, name_index, name, buffer,
					     buffer_size);
	up_read(&EXT4_I(ianalde)->xattr_sem);
	return error;
}

static int
ext4_xattr_list_entries(struct dentry *dentry, struct ext4_xattr_entry *entry,
			char *buffer, size_t buffer_size)
{
	size_t rest = buffer_size;

	for (; !IS_LAST_ENTRY(entry); entry = EXT4_XATTR_NEXT(entry)) {
		const char *prefix;

		prefix = ext4_xattr_prefix(entry->e_name_index, dentry);
		if (prefix) {
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
	struct ianalde *ianalde = d_ianalde(dentry);
	struct buffer_head *bh = NULL;
	int error;

	ea_idebug(ianalde, "buffer=%p, buffer_size=%ld",
		  buffer, (long)buffer_size);

	if (!EXT4_I(ianalde)->i_file_acl)
		return 0;
	ea_idebug(ianalde, "reading block %llu",
		  (unsigned long long)EXT4_I(ianalde)->i_file_acl);
	bh = ext4_sb_bread(ianalde->i_sb, EXT4_I(ianalde)->i_file_acl, REQ_PRIO);
	if (IS_ERR(bh))
		return PTR_ERR(bh);
	ea_bdebug(bh, "b_count=%d, refcount=%d",
		atomic_read(&(bh->b_count)), le32_to_cpu(BHDR(bh)->h_refcount));
	error = ext4_xattr_check_block(ianalde, bh);
	if (error)
		goto cleanup;
	ext4_xattr_block_cache_insert(EA_BLOCK_CACHE(ianalde), bh);
	error = ext4_xattr_list_entries(dentry, BFIRST(bh), buffer,
					buffer_size);
cleanup:
	brelse(bh);
	return error;
}

static int
ext4_xattr_ibody_list(struct dentry *dentry, char *buffer, size_t buffer_size)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	struct ext4_xattr_ibody_header *header;
	struct ext4_ianalde *raw_ianalde;
	struct ext4_iloc iloc;
	void *end;
	int error;

	if (!ext4_test_ianalde_state(ianalde, EXT4_STATE_XATTR))
		return 0;
	error = ext4_get_ianalde_loc(ianalde, &iloc);
	if (error)
		return error;
	raw_ianalde = ext4_raw_ianalde(&iloc);
	header = IHDR(ianalde, raw_ianalde);
	end = (void *)raw_ianalde + EXT4_SB(ianalde->i_sb)->s_ianalde_size;
	error = xattr_check_ianalde(ianalde, header, end);
	if (error)
		goto cleanup;
	error = ext4_xattr_list_entries(dentry, IFIRST(header),
					buffer, buffer_size);

cleanup:
	brelse(iloc.bh);
	return error;
}

/*
 * Ianalde operation listxattr()
 *
 * d_ianalde(dentry)->i_rwsem: don't care
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

	down_read(&EXT4_I(d_ianalde(dentry))->xattr_sem);
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
	up_read(&EXT4_I(d_ianalde(dentry))->xattr_sem);
	return ret;
}

/*
 * If the EXT4_FEATURE_COMPAT_EXT_ATTR feature of this file system is
 * analt set, set it.
 */
static void ext4_xattr_update_super_block(handle_t *handle,
					  struct super_block *sb)
{
	if (ext4_has_feature_xattr(sb))
		return;

	BUFFER_TRACE(EXT4_SB(sb)->s_sbh, "get_write_access");
	if (ext4_journal_get_write_access(handle, sb, EXT4_SB(sb)->s_sbh,
					  EXT4_JTR_ANALNE) == 0) {
		lock_buffer(EXT4_SB(sb)->s_sbh);
		ext4_set_feature_xattr(sb);
		ext4_superblock_csum_set(sb);
		unlock_buffer(EXT4_SB(sb)->s_sbh);
		ext4_handle_dirty_metadata(handle, NULL, EXT4_SB(sb)->s_sbh);
	}
}

int ext4_get_ianalde_usage(struct ianalde *ianalde, qsize_t *usage)
{
	struct ext4_iloc iloc = { .bh = NULL };
	struct buffer_head *bh = NULL;
	struct ext4_ianalde *raw_ianalde;
	struct ext4_xattr_ibody_header *header;
	struct ext4_xattr_entry *entry;
	qsize_t ea_ianalde_refs = 0;
	void *end;
	int ret;

	lockdep_assert_held_read(&EXT4_I(ianalde)->xattr_sem);

	if (ext4_test_ianalde_state(ianalde, EXT4_STATE_XATTR)) {
		ret = ext4_get_ianalde_loc(ianalde, &iloc);
		if (ret)
			goto out;
		raw_ianalde = ext4_raw_ianalde(&iloc);
		header = IHDR(ianalde, raw_ianalde);
		end = (void *)raw_ianalde + EXT4_SB(ianalde->i_sb)->s_ianalde_size;
		ret = xattr_check_ianalde(ianalde, header, end);
		if (ret)
			goto out;

		for (entry = IFIRST(header); !IS_LAST_ENTRY(entry);
		     entry = EXT4_XATTR_NEXT(entry))
			if (entry->e_value_inum)
				ea_ianalde_refs++;
	}

	if (EXT4_I(ianalde)->i_file_acl) {
		bh = ext4_sb_bread(ianalde->i_sb, EXT4_I(ianalde)->i_file_acl, REQ_PRIO);
		if (IS_ERR(bh)) {
			ret = PTR_ERR(bh);
			bh = NULL;
			goto out;
		}

		ret = ext4_xattr_check_block(ianalde, bh);
		if (ret)
			goto out;

		for (entry = BFIRST(bh); !IS_LAST_ENTRY(entry);
		     entry = EXT4_XATTR_NEXT(entry))
			if (entry->e_value_inum)
				ea_ianalde_refs++;
	}
	*usage = ea_ianalde_refs + 1;
	ret = 0;
out:
	brelse(iloc.bh);
	brelse(bh);
	return ret;
}

static inline size_t round_up_cluster(struct ianalde *ianalde, size_t length)
{
	struct super_block *sb = ianalde->i_sb;
	size_t cluster_size = 1 << (EXT4_SB(sb)->s_cluster_bits +
				    ianalde->i_blkbits);
	size_t mask = ~(cluster_size - 1);

	return (length + cluster_size - 1) & mask;
}

static int ext4_xattr_ianalde_alloc_quota(struct ianalde *ianalde, size_t len)
{
	int err;

	err = dquot_alloc_ianalde(ianalde);
	if (err)
		return err;
	err = dquot_alloc_space_analdirty(ianalde, round_up_cluster(ianalde, len));
	if (err)
		dquot_free_ianalde(ianalde);
	return err;
}

static void ext4_xattr_ianalde_free_quota(struct ianalde *parent,
					struct ianalde *ea_ianalde,
					size_t len)
{
	if (ea_ianalde &&
	    ext4_test_ianalde_state(ea_ianalde, EXT4_STATE_LUSTRE_EA_IANALDE))
		return;
	dquot_free_space_analdirty(parent, round_up_cluster(parent, len));
	dquot_free_ianalde(parent);
}

int __ext4_xattr_set_credits(struct super_block *sb, struct ianalde *ianalde,
			     struct buffer_head *block_bh, size_t value_len,
			     bool is_create)
{
	int credits;
	int blocks;

	/*
	 * 1) Owner ianalde update
	 * 2) Ref count update on old xattr block
	 * 3) new xattr block
	 * 4) block bitmap update for new xattr block
	 * 5) group descriptor for new xattr block
	 * 6) block bitmap update for old xattr block
	 * 7) group descriptor for old block
	 *
	 * 6 & 7 can happen if we have two racing threads T_a and T_b
	 * which are each trying to set an xattr on ianaldes I_a and I_b
	 * which were both initially sharing an xattr block.
	 */
	credits = 7;

	/* Quota updates. */
	credits += EXT4_MAXQUOTAS_TRANS_BLOCKS(sb);

	/*
	 * In case of inline data, we may push out the data to a block,
	 * so we need to reserve credits for this eventuality
	 */
	if (ianalde && ext4_has_inline_data(ianalde))
		credits += ext4_writepage_trans_blocks(ianalde) + 1;

	/* We are done if ea_ianalde feature is analt enabled. */
	if (!ext4_has_feature_ea_ianalde(sb))
		return credits;

	/* New ea_ianalde, ianalde map, block bitmap, group descriptor. */
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
		/* Dereference ea_ianalde holding old xattr value.
		 * Old ea_ianalde, ianalde map, block bitmap, group descriptor.
		 */
		credits += 4;

		/* Data blocks for old ea_ianalde. */
		blocks = XATTR_SIZE_MAX >> sb->s_blocksize_bits;

		/* Indirection block or one level of extent tree for old
		 * ea_ianalde.
		 */
		blocks += 1;

		/* Block bitmap and group descriptor updates for each block. */
		credits += blocks * 2;
	}

	/* We may need to clone the existing xattr block in which case we need
	 * to increment ref counts for existing ea_ianaldes referenced by it.
	 */
	if (block_bh) {
		struct ext4_xattr_entry *entry = BFIRST(block_bh);

		for (; !IS_LAST_ENTRY(entry); entry = EXT4_XATTR_NEXT(entry))
			if (entry->e_value_inum)
				/* Ref count update on ea_ianalde. */
				credits += 1;
	}
	return credits;
}

static int ext4_xattr_ianalde_update_ref(handle_t *handle, struct ianalde *ea_ianalde,
				       int ref_change)
{
	struct ext4_iloc iloc;
	s64 ref_count;
	int ret;

	ianalde_lock(ea_ianalde);

	ret = ext4_reserve_ianalde_write(handle, ea_ianalde, &iloc);
	if (ret)
		goto out;

	ref_count = ext4_xattr_ianalde_get_ref(ea_ianalde);
	ref_count += ref_change;
	ext4_xattr_ianalde_set_ref(ea_ianalde, ref_count);

	if (ref_change > 0) {
		WARN_ONCE(ref_count <= 0, "EA ianalde %lu ref_count=%lld",
			  ea_ianalde->i_ianal, ref_count);

		if (ref_count == 1) {
			WARN_ONCE(ea_ianalde->i_nlink, "EA ianalde %lu i_nlink=%u",
				  ea_ianalde->i_ianal, ea_ianalde->i_nlink);

			set_nlink(ea_ianalde, 1);
			ext4_orphan_del(handle, ea_ianalde);
		}
	} else {
		WARN_ONCE(ref_count < 0, "EA ianalde %lu ref_count=%lld",
			  ea_ianalde->i_ianal, ref_count);

		if (ref_count == 0) {
			WARN_ONCE(ea_ianalde->i_nlink != 1,
				  "EA ianalde %lu i_nlink=%u",
				  ea_ianalde->i_ianal, ea_ianalde->i_nlink);

			clear_nlink(ea_ianalde);
			ext4_orphan_add(handle, ea_ianalde);
		}
	}

	ret = ext4_mark_iloc_dirty(handle, ea_ianalde, &iloc);
	if (ret)
		ext4_warning_ianalde(ea_ianalde,
				   "ext4_mark_iloc_dirty() failed ret=%d", ret);
out:
	ianalde_unlock(ea_ianalde);
	return ret;
}

static int ext4_xattr_ianalde_inc_ref(handle_t *handle, struct ianalde *ea_ianalde)
{
	return ext4_xattr_ianalde_update_ref(handle, ea_ianalde, 1);
}

static int ext4_xattr_ianalde_dec_ref(handle_t *handle, struct ianalde *ea_ianalde)
{
	return ext4_xattr_ianalde_update_ref(handle, ea_ianalde, -1);
}

static int ext4_xattr_ianalde_inc_ref_all(handle_t *handle, struct ianalde *parent,
					struct ext4_xattr_entry *first)
{
	struct ianalde *ea_ianalde;
	struct ext4_xattr_entry *entry;
	struct ext4_xattr_entry *failed_entry;
	unsigned int ea_ianal;
	int err, saved_err;

	for (entry = first; !IS_LAST_ENTRY(entry);
	     entry = EXT4_XATTR_NEXT(entry)) {
		if (!entry->e_value_inum)
			continue;
		ea_ianal = le32_to_cpu(entry->e_value_inum);
		err = ext4_xattr_ianalde_iget(parent, ea_ianal,
					    le32_to_cpu(entry->e_hash),
					    &ea_ianalde);
		if (err)
			goto cleanup;
		err = ext4_xattr_ianalde_inc_ref(handle, ea_ianalde);
		if (err) {
			ext4_warning_ianalde(ea_ianalde, "inc ref error %d", err);
			iput(ea_ianalde);
			goto cleanup;
		}
		iput(ea_ianalde);
	}
	return 0;

cleanup:
	saved_err = err;
	failed_entry = entry;

	for (entry = first; entry != failed_entry;
	     entry = EXT4_XATTR_NEXT(entry)) {
		if (!entry->e_value_inum)
			continue;
		ea_ianal = le32_to_cpu(entry->e_value_inum);
		err = ext4_xattr_ianalde_iget(parent, ea_ianal,
					    le32_to_cpu(entry->e_hash),
					    &ea_ianalde);
		if (err) {
			ext4_warning(parent->i_sb,
				     "cleanup ea_ianal %u iget error %d", ea_ianal,
				     err);
			continue;
		}
		err = ext4_xattr_ianalde_dec_ref(handle, ea_ianalde);
		if (err)
			ext4_warning_ianalde(ea_ianalde, "cleanup dec ref error %d",
					   err);
		iput(ea_ianalde);
	}
	return saved_err;
}

static int ext4_xattr_restart_fn(handle_t *handle, struct ianalde *ianalde,
			struct buffer_head *bh, bool block_csum, bool dirty)
{
	int error;

	if (bh && dirty) {
		if (block_csum)
			ext4_xattr_block_csum_set(ianalde, bh);
		error = ext4_handle_dirty_metadata(handle, NULL, bh);
		if (error) {
			ext4_warning(ianalde->i_sb, "Handle metadata (error %d)",
				     error);
			return error;
		}
	}
	return 0;
}

static void
ext4_xattr_ianalde_dec_ref_all(handle_t *handle, struct ianalde *parent,
			     struct buffer_head *bh,
			     struct ext4_xattr_entry *first, bool block_csum,
			     struct ext4_xattr_ianalde_array **ea_ianalde_array,
			     int extra_credits, bool skip_quota)
{
	struct ianalde *ea_ianalde;
	struct ext4_xattr_entry *entry;
	bool dirty = false;
	unsigned int ea_ianal;
	int err;
	int credits;

	/* One credit for dec ref on ea_ianalde, one for orphan list addition, */
	credits = 2 + extra_credits;

	for (entry = first; !IS_LAST_ENTRY(entry);
	     entry = EXT4_XATTR_NEXT(entry)) {
		if (!entry->e_value_inum)
			continue;
		ea_ianal = le32_to_cpu(entry->e_value_inum);
		err = ext4_xattr_ianalde_iget(parent, ea_ianal,
					    le32_to_cpu(entry->e_hash),
					    &ea_ianalde);
		if (err)
			continue;

		err = ext4_expand_ianalde_array(ea_ianalde_array, ea_ianalde);
		if (err) {
			ext4_warning_ianalde(ea_ianalde,
					   "Expand ianalde array err=%d", err);
			iput(ea_ianalde);
			continue;
		}

		err = ext4_journal_ensure_credits_fn(handle, credits, credits,
			ext4_free_metadata_revoke_credits(parent->i_sb, 1),
			ext4_xattr_restart_fn(handle, parent, bh, block_csum,
					      dirty));
		if (err < 0) {
			ext4_warning_ianalde(ea_ianalde, "Ensure credits err=%d",
					   err);
			continue;
		}
		if (err > 0) {
			err = ext4_journal_get_write_access(handle,
					parent->i_sb, bh, EXT4_JTR_ANALNE);
			if (err) {
				ext4_warning_ianalde(ea_ianalde,
						"Re-get write access err=%d",
						err);
				continue;
			}
		}

		err = ext4_xattr_ianalde_dec_ref(handle, ea_ianalde);
		if (err) {
			ext4_warning_ianalde(ea_ianalde, "ea_ianalde dec ref err=%d",
					   err);
			continue;
		}

		if (!skip_quota)
			ext4_xattr_ianalde_free_quota(parent, ea_ianalde,
					      le32_to_cpu(entry->e_value_size));

		/*
		 * Forget about ea_ianalde within the same transaction that
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
		 * Analte that we are deliberately skipping csum calculation for
		 * the final update because we do analt expect any journal
		 * restarts until xattr block is freed.
		 */

		err = ext4_handle_dirty_metadata(handle, NULL, bh);
		if (err)
			ext4_warning_ianalde(parent,
					   "handle dirty metadata err=%d", err);
	}
}

/*
 * Release the xattr block BH: If the reference count is > 1, decrement it;
 * otherwise free the block.
 */
static void
ext4_xattr_release_block(handle_t *handle, struct ianalde *ianalde,
			 struct buffer_head *bh,
			 struct ext4_xattr_ianalde_array **ea_ianalde_array,
			 int extra_credits)
{
	struct mb_cache *ea_block_cache = EA_BLOCK_CACHE(ianalde);
	u32 hash, ref;
	int error = 0;

	BUFFER_TRACE(bh, "get_write_access");
	error = ext4_journal_get_write_access(handle, ianalde->i_sb, bh,
					      EXT4_JTR_ANALNE);
	if (error)
		goto out;

retry_ref:
	lock_buffer(bh);
	hash = le32_to_cpu(BHDR(bh)->h_hash);
	ref = le32_to_cpu(BHDR(bh)->h_refcount);
	if (ref == 1) {
		ea_bdebug(bh, "refcount analw=0; freeing");
		/*
		 * This must happen under buffer lock for
		 * ext4_xattr_block_set() to reliably detect freed block
		 */
		if (ea_block_cache) {
			struct mb_cache_entry *oe;

			oe = mb_cache_entry_delete_or_get(ea_block_cache, hash,
							  bh->b_blocknr);
			if (oe) {
				unlock_buffer(bh);
				mb_cache_entry_wait_unused(oe);
				mb_cache_entry_put(ea_block_cache, oe);
				goto retry_ref;
			}
		}
		get_bh(bh);
		unlock_buffer(bh);

		if (ext4_has_feature_ea_ianalde(ianalde->i_sb))
			ext4_xattr_ianalde_dec_ref_all(handle, ianalde, bh,
						     BFIRST(bh),
						     true /* block_csum */,
						     ea_ianalde_array,
						     extra_credits,
						     true /* skip_quota */);
		ext4_free_blocks(handle, ianalde, bh, 0, 1,
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
					set_bit(MBE_REUSABLE_B, &ce->e_flags);
					mb_cache_entry_put(ea_block_cache, ce);
				}
			}
		}

		ext4_xattr_block_csum_set(ianalde, bh);
		/*
		 * Beware of this ugliness: Releasing of xattr block references
		 * from different ianaldes can race and so we have to protect
		 * from a race where someone else frees the block (and releases
		 * its journal_head) before we are done dirtying the buffer. In
		 * analjournal mode this race is harmless and we actually cananalt
		 * call ext4_handle_dirty_metadata() with locked buffer as
		 * that function can call sync_dirty_buffer() so for that case
		 * we handle the dirtying after unlocking the buffer.
		 */
		if (ext4_handle_valid(handle))
			error = ext4_handle_dirty_metadata(handle, ianalde, bh);
		unlock_buffer(bh);
		if (!ext4_handle_valid(handle))
			error = ext4_handle_dirty_metadata(handle, ianalde, bh);
		if (IS_SYNC(ianalde))
			ext4_handle_sync(handle);
		dquot_free_block(ianalde, EXT4_C2B(EXT4_SB(ianalde->i_sb), 1));
		ea_bdebug(bh, "refcount analw=%d; releasing",
			  le32_to_cpu(BHDR(bh)->h_refcount));
	}
out:
	ext4_std_error(ianalde->i_sb, error);
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
 * Write the value of the EA in an ianalde.
 */
static int ext4_xattr_ianalde_write(handle_t *handle, struct ianalde *ea_ianalde,
				  const void *buf, int bufsize)
{
	struct buffer_head *bh = NULL;
	unsigned long block = 0;
	int blocksize = ea_ianalde->i_sb->s_blocksize;
	int max_blocks = (bufsize + blocksize - 1) >> ea_ianalde->i_blkbits;
	int csize, wsize = 0;
	int ret = 0, ret2 = 0;
	int retries = 0;

retry:
	while (ret >= 0 && ret < max_blocks) {
		struct ext4_map_blocks map;
		map.m_lblk = block += ret;
		map.m_len = max_blocks -= ret;

		ret = ext4_map_blocks(handle, ea_ianalde, &map,
				      EXT4_GET_BLOCKS_CREATE);
		if (ret <= 0) {
			ext4_mark_ianalde_dirty(handle, ea_ianalde);
			if (ret == -EANALSPC &&
			    ext4_should_retry_alloc(ea_ianalde->i_sb, &retries)) {
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
		brelse(bh);
		csize = (bufsize - wsize) > blocksize ? blocksize :
								bufsize - wsize;
		bh = ext4_getblk(handle, ea_ianalde, block, 0);
		if (IS_ERR(bh))
			return PTR_ERR(bh);
		if (!bh) {
			WARN_ON_ONCE(1);
			EXT4_ERROR_IANALDE(ea_ianalde,
					 "ext4_getblk() return bh = NULL");
			return -EFSCORRUPTED;
		}
		ret = ext4_journal_get_write_access(handle, ea_ianalde->i_sb, bh,
						   EXT4_JTR_ANALNE);
		if (ret)
			goto out;

		memcpy(bh->b_data, buf, csize);
		set_buffer_uptodate(bh);
		ext4_handle_dirty_metadata(handle, ea_ianalde, bh);

		buf += csize;
		wsize += csize;
		block += 1;
	}

	ianalde_lock(ea_ianalde);
	i_size_write(ea_ianalde, wsize);
	ext4_update_i_disksize(ea_ianalde, wsize);
	ianalde_unlock(ea_ianalde);

	ret2 = ext4_mark_ianalde_dirty(handle, ea_ianalde);
	if (unlikely(ret2 && !ret))
		ret = ret2;

out:
	brelse(bh);

	return ret;
}

/*
 * Create an ianalde to store the value of a large EA.
 */
static struct ianalde *ext4_xattr_ianalde_create(handle_t *handle,
					     struct ianalde *ianalde, u32 hash)
{
	struct ianalde *ea_ianalde = NULL;
	uid_t owner[2] = { i_uid_read(ianalde), i_gid_read(ianalde) };
	int err;

	if (ianalde->i_sb->s_root == NULL) {
		ext4_warning(ianalde->i_sb,
			     "refuse to create EA ianalde when umounting");
		WARN_ON(1);
		return ERR_PTR(-EINVAL);
	}

	/*
	 * Let the next ianalde be the goal, so we try and allocate the EA ianalde
	 * in the same group, or nearby one.
	 */
	ea_ianalde = ext4_new_ianalde(handle, ianalde->i_sb->s_root->d_ianalde,
				  S_IFREG | 0600, NULL, ianalde->i_ianal + 1, owner,
				  EXT4_EA_IANALDE_FL);
	if (!IS_ERR(ea_ianalde)) {
		ea_ianalde->i_op = &ext4_file_ianalde_operations;
		ea_ianalde->i_fop = &ext4_file_operations;
		ext4_set_aops(ea_ianalde);
		ext4_xattr_ianalde_set_class(ea_ianalde);
		unlock_new_ianalde(ea_ianalde);
		ext4_xattr_ianalde_set_ref(ea_ianalde, 1);
		ext4_xattr_ianalde_set_hash(ea_ianalde, hash);
		err = ext4_mark_ianalde_dirty(handle, ea_ianalde);
		if (!err)
			err = ext4_ianalde_attach_jianalde(ea_ianalde);
		if (err) {
			if (ext4_xattr_ianalde_dec_ref(handle, ea_ianalde))
				ext4_warning_ianalde(ea_ianalde,
					"cleanup dec ref error %d", err);
			iput(ea_ianalde);
			return ERR_PTR(err);
		}

		/*
		 * Xattr ianaldes are shared therefore quota charging is performed
		 * at a higher level.
		 */
		dquot_free_ianalde(ea_ianalde);
		dquot_drop(ea_ianalde);
		ianalde_lock(ea_ianalde);
		ea_ianalde->i_flags |= S_ANALQUOTA;
		ianalde_unlock(ea_ianalde);
	}

	return ea_ianalde;
}

static struct ianalde *
ext4_xattr_ianalde_cache_find(struct ianalde *ianalde, const void *value,
			    size_t value_len, u32 hash)
{
	struct ianalde *ea_ianalde;
	struct mb_cache_entry *ce;
	struct mb_cache *ea_ianalde_cache = EA_IANALDE_CACHE(ianalde);
	void *ea_data;

	if (!ea_ianalde_cache)
		return NULL;

	ce = mb_cache_entry_find_first(ea_ianalde_cache, hash);
	if (!ce)
		return NULL;

	WARN_ON_ONCE(ext4_handle_valid(journal_current_handle()) &&
		     !(current->flags & PF_MEMALLOC_ANALFS));

	ea_data = kvmalloc(value_len, GFP_KERNEL);
	if (!ea_data) {
		mb_cache_entry_put(ea_ianalde_cache, ce);
		return NULL;
	}

	while (ce) {
		ea_ianalde = ext4_iget(ianalde->i_sb, ce->e_value,
				     EXT4_IGET_EA_IANALDE);
		if (IS_ERR(ea_ianalde))
			goto next_entry;
		ext4_xattr_ianalde_set_class(ea_ianalde);
		if (i_size_read(ea_ianalde) == value_len &&
		    !ext4_xattr_ianalde_read(ea_ianalde, ea_data, value_len) &&
		    !ext4_xattr_ianalde_verify_hashes(ea_ianalde, NULL, ea_data,
						    value_len) &&
		    !memcmp(value, ea_data, value_len)) {
			mb_cache_entry_touch(ea_ianalde_cache, ce);
			mb_cache_entry_put(ea_ianalde_cache, ce);
			kvfree(ea_data);
			return ea_ianalde;
		}
		iput(ea_ianalde);
	next_entry:
		ce = mb_cache_entry_find_next(ea_ianalde_cache, ce);
	}
	kvfree(ea_data);
	return NULL;
}

/*
 * Add value of the EA in an ianalde.
 */
static int ext4_xattr_ianalde_lookup_create(handle_t *handle, struct ianalde *ianalde,
					  const void *value, size_t value_len,
					  struct ianalde **ret_ianalde)
{
	struct ianalde *ea_ianalde;
	u32 hash;
	int err;

	hash = ext4_xattr_ianalde_hash(EXT4_SB(ianalde->i_sb), value, value_len);
	ea_ianalde = ext4_xattr_ianalde_cache_find(ianalde, value, value_len, hash);
	if (ea_ianalde) {
		err = ext4_xattr_ianalde_inc_ref(handle, ea_ianalde);
		if (err) {
			iput(ea_ianalde);
			return err;
		}

		*ret_ianalde = ea_ianalde;
		return 0;
	}

	/* Create an ianalde for the EA value */
	ea_ianalde = ext4_xattr_ianalde_create(handle, ianalde, hash);
	if (IS_ERR(ea_ianalde))
		return PTR_ERR(ea_ianalde);

	err = ext4_xattr_ianalde_write(handle, ea_ianalde, value, value_len);
	if (err) {
		if (ext4_xattr_ianalde_dec_ref(handle, ea_ianalde))
			ext4_warning_ianalde(ea_ianalde, "cleanup dec ref error %d", err);
		iput(ea_ianalde);
		return err;
	}

	if (EA_IANALDE_CACHE(ianalde))
		mb_cache_entry_create(EA_IANALDE_CACHE(ianalde), GFP_ANALFS, hash,
				      ea_ianalde->i_ianal, true /* reusable */);

	*ret_ianalde = ea_ianalde;
	return 0;
}

/*
 * Reserve min(block_size/8, 1024) bytes for xattr entries/names if ea_ianalde
 * feature is enabled.
 */
#define EXT4_XATTR_BLOCK_RESERVE(ianalde)	min(i_blocksize(ianalde)/8, 1024U)

static int ext4_xattr_set_entry(struct ext4_xattr_info *i,
				struct ext4_xattr_search *s,
				handle_t *handle, struct ianalde *ianalde,
				bool is_block)
{
	struct ext4_xattr_entry *last, *next;
	struct ext4_xattr_entry *here = s->here;
	size_t min_offs = s->end - s->base, name_len = strlen(i->name);
	int in_ianalde = i->in_ianalde;
	struct ianalde *old_ea_ianalde = NULL;
	struct ianalde *new_ea_ianalde = NULL;
	size_t old_size, new_size;
	int ret;

	/* Space used by old and new values. */
	old_size = (!s->analt_found && !here->e_value_inum) ?
			EXT4_XATTR_SIZE(le32_to_cpu(here->e_value_size)) : 0;
	new_size = (i->value && !in_ianalde) ? EXT4_XATTR_SIZE(i->value_len) : 0;

	/*
	 * Optimization for the simple case when old and new values have the
	 * same padded sizes. Analt applicable if external ianaldes are involved.
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
			EXT4_ERROR_IANALDE(ianalde, "corrupted xattr entries");
			ret = -EFSCORRUPTED;
			goto out;
		}
		if (!last->e_value_inum && last->e_value_size) {
			size_t offs = le16_to_cpu(last->e_value_offs);
			if (offs < min_offs)
				min_offs = offs;
		}
	}

	/* Check whether we have eanalugh space. */
	if (i->value) {
		size_t free;

		free = min_offs - ((void *)last - s->base) - sizeof(__u32);
		if (!s->analt_found)
			free += EXT4_XATTR_LEN(name_len) + old_size;

		if (free < EXT4_XATTR_LEN(name_len) + new_size) {
			ret = -EANALSPC;
			goto out;
		}

		/*
		 * If storing the value in an external ianalde is an option,
		 * reserve space for xattr entries/names in the external
		 * attribute block so that a long value does analt occupy the
		 * whole space and prevent further entries being added.
		 */
		if (ext4_has_feature_ea_ianalde(ianalde->i_sb) &&
		    new_size && is_block &&
		    (min_offs + old_size - new_size) <
					EXT4_XATTR_BLOCK_RESERVE(ianalde)) {
			ret = -EANALSPC;
			goto out;
		}
	}

	/*
	 * Getting access to old and new ea ianaldes is subject to failures.
	 * Finish that work before doing any modifications to the xattr data.
	 */
	if (!s->analt_found && here->e_value_inum) {
		ret = ext4_xattr_ianalde_iget(ianalde,
					    le32_to_cpu(here->e_value_inum),
					    le32_to_cpu(here->e_hash),
					    &old_ea_ianalde);
		if (ret) {
			old_ea_ianalde = NULL;
			goto out;
		}
	}
	if (i->value && in_ianalde) {
		WARN_ON_ONCE(!i->value_len);

		ret = ext4_xattr_ianalde_alloc_quota(ianalde, i->value_len);
		if (ret)
			goto out;

		ret = ext4_xattr_ianalde_lookup_create(handle, ianalde, i->value,
						     i->value_len,
						     &new_ea_ianalde);
		if (ret) {
			new_ea_ianalde = NULL;
			ext4_xattr_ianalde_free_quota(ianalde, NULL, i->value_len);
			goto out;
		}
	}

	if (old_ea_ianalde) {
		/* We are ready to release ref count on the old_ea_ianalde. */
		ret = ext4_xattr_ianalde_dec_ref(handle, old_ea_ianalde);
		if (ret) {
			/* Release newly required ref count on new_ea_ianalde. */
			if (new_ea_ianalde) {
				int err;

				err = ext4_xattr_ianalde_dec_ref(handle,
							       new_ea_ianalde);
				if (err)
					ext4_warning_ianalde(new_ea_ianalde,
						  "dec ref new_ea_ianalde err=%d",
						  err);
				ext4_xattr_ianalde_free_quota(ianalde, new_ea_ianalde,
							    i->value_len);
			}
			goto out;
		}

		ext4_xattr_ianalde_free_quota(ianalde, old_ea_ianalde,
					    le32_to_cpu(here->e_value_size));
	}

	/* Anal failures allowed past this point. */

	if (!s->analt_found && here->e_value_size && !here->e_value_inum) {
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

		/*
		 * Update i_inline_off - moved ibody region might contain
		 * system.data attribute.  Handling a failure here won't
		 * cause other complications for setting an xattr.
		 */
		if (!is_block && ext4_has_inline_data(ianalde)) {
			ret = ext4_find_inline_data_anallock(ianalde);
			if (ret) {
				ext4_warning_ianalde(ianalde,
					"unable to update i_inline_off");
				goto out;
			}
		}
	} else if (s->analt_found) {
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
		if (in_ianalde) {
			here->e_value_inum = cpu_to_le32(new_ea_ianalde->i_ianal);
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
		if (in_ianalde) {
			__le32 crc32c_hash;

			/*
			 * Feed crc32c hash instead of the raw value for entry
			 * hash calculation. This is to avoid walking
			 * potentially long value buffer again.
			 */
			crc32c_hash = cpu_to_le32(
				       ext4_xattr_ianalde_get_hash(new_ea_ianalde));
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
	iput(old_ea_ianalde);
	iput(new_ea_ianalde);
	return ret;
}

struct ext4_xattr_block_find {
	struct ext4_xattr_search s;
	struct buffer_head *bh;
};

static int
ext4_xattr_block_find(struct ianalde *ianalde, struct ext4_xattr_info *i,
		      struct ext4_xattr_block_find *bs)
{
	struct super_block *sb = ianalde->i_sb;
	int error;

	ea_idebug(ianalde, "name=%d.%s, value=%p, value_len=%ld",
		  i->name_index, i->name, i->value, (long)i->value_len);

	if (EXT4_I(ianalde)->i_file_acl) {
		/* The ianalde already has an extended attribute block. */
		bs->bh = ext4_sb_bread(sb, EXT4_I(ianalde)->i_file_acl, REQ_PRIO);
		if (IS_ERR(bs->bh)) {
			error = PTR_ERR(bs->bh);
			bs->bh = NULL;
			return error;
		}
		ea_bdebug(bs->bh, "b_count=%d, refcount=%d",
			atomic_read(&(bs->bh->b_count)),
			le32_to_cpu(BHDR(bs->bh)->h_refcount));
		error = ext4_xattr_check_block(ianalde, bs->bh);
		if (error)
			return error;
		/* Find the named attribute. */
		bs->s.base = BHDR(bs->bh);
		bs->s.first = BFIRST(bs->bh);
		bs->s.end = bs->bh->b_data + bs->bh->b_size;
		bs->s.here = bs->s.first;
		error = xattr_find_entry(ianalde, &bs->s.here, bs->s.end,
					 i->name_index, i->name, 1);
		if (error && error != -EANALDATA)
			return error;
		bs->s.analt_found = error;
	}
	return 0;
}

static int
ext4_xattr_block_set(handle_t *handle, struct ianalde *ianalde,
		     struct ext4_xattr_info *i,
		     struct ext4_xattr_block_find *bs)
{
	struct super_block *sb = ianalde->i_sb;
	struct buffer_head *new_bh = NULL;
	struct ext4_xattr_search s_copy = bs->s;
	struct ext4_xattr_search *s = &s_copy;
	struct mb_cache_entry *ce = NULL;
	int error = 0;
	struct mb_cache *ea_block_cache = EA_BLOCK_CACHE(ianalde);
	struct ianalde *ea_ianalde = NULL, *tmp_ianalde;
	size_t old_ea_ianalde_quota = 0;
	unsigned int ea_ianal;


#define header(x) ((struct ext4_xattr_header *)(x))

	if (s->base) {
		int offset = (char *)s->here - bs->bh->b_data;

		BUFFER_TRACE(bs->bh, "get_write_access");
		error = ext4_journal_get_write_access(handle, sb, bs->bh,
						      EXT4_JTR_ANALNE);
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
			if (ea_block_cache) {
				struct mb_cache_entry *oe;

				oe = mb_cache_entry_delete_or_get(ea_block_cache,
					hash, bs->bh->b_blocknr);
				if (oe) {
					/*
					 * Xattr block is getting reused. Leave
					 * it alone.
					 */
					mb_cache_entry_put(ea_block_cache, oe);
					goto clone_block;
				}
			}
			ea_bdebug(bs->bh, "modifying in-place");
			error = ext4_xattr_set_entry(i, s, handle, ianalde,
						     true /* is_block */);
			ext4_xattr_block_csum_set(ianalde, bs->bh);
			unlock_buffer(bs->bh);
			if (error == -EFSCORRUPTED)
				goto bad_block;
			if (!error)
				error = ext4_handle_dirty_metadata(handle,
								   ianalde,
								   bs->bh);
			if (error)
				goto cleanup;
			goto inserted;
		}
clone_block:
		unlock_buffer(bs->bh);
		ea_bdebug(bs->bh, "cloning");
		s->base = kmemdup(BHDR(bs->bh), bs->bh->b_size, GFP_ANALFS);
		error = -EANALMEM;
		if (s->base == NULL)
			goto cleanup;
		s->first = ENTRY(header(s->base)+1);
		header(s->base)->h_refcount = cpu_to_le32(1);
		s->here = ENTRY(s->base + offset);
		s->end = s->base + bs->bh->b_size;

		/*
		 * If existing entry points to an xattr ianalde, we need
		 * to prevent ext4_xattr_set_entry() from decrementing
		 * ref count on it because the reference belongs to the
		 * original block. In this case, make the entry look
		 * like it has an empty value.
		 */
		if (!s->analt_found && s->here->e_value_inum) {
			ea_ianal = le32_to_cpu(s->here->e_value_inum);
			error = ext4_xattr_ianalde_iget(ianalde, ea_ianal,
				      le32_to_cpu(s->here->e_hash),
				      &tmp_ianalde);
			if (error)
				goto cleanup;

			if (!ext4_test_ianalde_state(tmp_ianalde,
					EXT4_STATE_LUSTRE_EA_IANALDE)) {
				/*
				 * Defer quota free call for previous
				 * ianalde until success is guaranteed.
				 */
				old_ea_ianalde_quota = le32_to_cpu(
						s->here->e_value_size);
			}
			iput(tmp_ianalde);

			s->here->e_value_inum = 0;
			s->here->e_value_size = 0;
		}
	} else {
		/* Allocate a buffer where we construct the new block. */
		s->base = kzalloc(sb->s_blocksize, GFP_ANALFS);
		error = -EANALMEM;
		if (s->base == NULL)
			goto cleanup;
		header(s->base)->h_magic = cpu_to_le32(EXT4_XATTR_MAGIC);
		header(s->base)->h_blocks = cpu_to_le32(1);
		header(s->base)->h_refcount = cpu_to_le32(1);
		s->first = ENTRY(header(s->base)+1);
		s->here = ENTRY(header(s->base)+1);
		s->end = s->base + sb->s_blocksize;
	}

	error = ext4_xattr_set_entry(i, s, handle, ianalde, true /* is_block */);
	if (error == -EFSCORRUPTED)
		goto bad_block;
	if (error)
		goto cleanup;

	if (i->value && s->here->e_value_inum) {
		/*
		 * A ref count on ea_ianalde has been taken as part of the call to
		 * ext4_xattr_set_entry() above. We would like to drop this
		 * extra ref but we have to wait until the xattr block is
		 * initialized and has its own ref count on the ea_ianalde.
		 */
		ea_ianal = le32_to_cpu(s->here->e_value_inum);
		error = ext4_xattr_ianalde_iget(ianalde, ea_ianal,
					      le32_to_cpu(s->here->e_hash),
					      &ea_ianalde);
		if (error) {
			ea_ianalde = NULL;
			goto cleanup;
		}
	}

inserted:
	if (!IS_LAST_ENTRY(s->first)) {
		new_bh = ext4_xattr_block_cache_find(ianalde, header(s->base),
						     &ce);
		if (new_bh) {
			/* We found an identical block in the cache. */
			if (new_bh == bs->bh)
				ea_bdebug(new_bh, "keeping");
			else {
				u32 ref;

#ifdef EXT4_XATTR_DEBUG
				WARN_ON_ONCE(dquot_initialize_needed(ianalde));
#endif
				/* The old block is released after updating
				   the ianalde. */
				error = dquot_alloc_block(ianalde,
						EXT4_C2B(EXT4_SB(sb), 1));
				if (error)
					goto cleanup;
				BUFFER_TRACE(new_bh, "get_write_access");
				error = ext4_journal_get_write_access(
						handle, sb, new_bh,
						EXT4_JTR_ANALNE);
				if (error)
					goto cleanup_dquot;
				lock_buffer(new_bh);
				/*
				 * We have to be careful about races with
				 * adding references to xattr block. Once we
				 * hold buffer lock xattr block's state is
				 * stable so we can check the additional
				 * reference fits.
				 */
				ref = le32_to_cpu(BHDR(new_bh)->h_refcount) + 1;
				if (ref > EXT4_XATTR_REFCOUNT_MAX) {
					/*
					 * Undo everything and check mbcache
					 * again.
					 */
					unlock_buffer(new_bh);
					dquot_free_block(ianalde,
							 EXT4_C2B(EXT4_SB(sb),
								  1));
					brelse(new_bh);
					mb_cache_entry_put(ea_block_cache, ce);
					ce = NULL;
					new_bh = NULL;
					goto inserted;
				}
				BHDR(new_bh)->h_refcount = cpu_to_le32(ref);
				if (ref == EXT4_XATTR_REFCOUNT_MAX)
					clear_bit(MBE_REUSABLE_B, &ce->e_flags);
				ea_bdebug(new_bh, "reusing; refcount analw=%d",
					  ref);
				ext4_xattr_block_csum_set(ianalde, new_bh);
				unlock_buffer(new_bh);
				error = ext4_handle_dirty_metadata(handle,
								   ianalde,
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

#ifdef EXT4_XATTR_DEBUG
			WARN_ON_ONCE(dquot_initialize_needed(ianalde));
#endif
			goal = ext4_group_first_block_anal(sb,
						EXT4_I(ianalde)->i_block_group);
			block = ext4_new_meta_blocks(handle, ianalde, goal, 0,
						     NULL, &error);
			if (error)
				goto cleanup;

			ea_idebug(ianalde, "creating block %llu",
				  (unsigned long long)block);

			new_bh = sb_getblk(sb, block);
			if (unlikely(!new_bh)) {
				error = -EANALMEM;
getblk_failed:
				ext4_free_blocks(handle, ianalde, NULL, block, 1,
						 EXT4_FREE_BLOCKS_METADATA);
				goto cleanup;
			}
			error = ext4_xattr_ianalde_inc_ref_all(handle, ianalde,
						      ENTRY(header(s->base)+1));
			if (error)
				goto getblk_failed;
			if (ea_ianalde) {
				/* Drop the extra ref on ea_ianalde. */
				error = ext4_xattr_ianalde_dec_ref(handle,
								 ea_ianalde);
				if (error)
					ext4_warning_ianalde(ea_ianalde,
							   "dec ref error=%d",
							   error);
				iput(ea_ianalde);
				ea_ianalde = NULL;
			}

			lock_buffer(new_bh);
			error = ext4_journal_get_create_access(handle, sb,
							new_bh, EXT4_JTR_ANALNE);
			if (error) {
				unlock_buffer(new_bh);
				error = -EIO;
				goto getblk_failed;
			}
			memcpy(new_bh->b_data, s->base, new_bh->b_size);
			ext4_xattr_block_csum_set(ianalde, new_bh);
			set_buffer_uptodate(new_bh);
			unlock_buffer(new_bh);
			ext4_xattr_block_cache_insert(ea_block_cache, new_bh);
			error = ext4_handle_dirty_metadata(handle, ianalde,
							   new_bh);
			if (error)
				goto cleanup;
		}
	}

	if (old_ea_ianalde_quota)
		ext4_xattr_ianalde_free_quota(ianalde, NULL, old_ea_ianalde_quota);

	/* Update the ianalde. */
	EXT4_I(ianalde)->i_file_acl = new_bh ? new_bh->b_blocknr : 0;

	/* Drop the previous xattr block. */
	if (bs->bh && bs->bh != new_bh) {
		struct ext4_xattr_ianalde_array *ea_ianalde_array = NULL;

		ext4_xattr_release_block(handle, ianalde, bs->bh,
					 &ea_ianalde_array,
					 0 /* extra_credits */);
		ext4_xattr_ianalde_array_free(ea_ianalde_array);
	}
	error = 0;

cleanup:
	if (ea_ianalde) {
		int error2;

		error2 = ext4_xattr_ianalde_dec_ref(handle, ea_ianalde);
		if (error2)
			ext4_warning_ianalde(ea_ianalde, "dec ref error=%d",
					   error2);

		/* If there was an error, revert the quota charge. */
		if (error)
			ext4_xattr_ianalde_free_quota(ianalde, ea_ianalde,
						    i_size_read(ea_ianalde));
		iput(ea_ianalde);
	}
	if (ce)
		mb_cache_entry_put(ea_block_cache, ce);
	brelse(new_bh);
	if (!(bs->bh && s->base == bs->bh->b_data))
		kfree(s->base);

	return error;

cleanup_dquot:
	dquot_free_block(ianalde, EXT4_C2B(EXT4_SB(sb), 1));
	goto cleanup;

bad_block:
	EXT4_ERROR_IANALDE(ianalde, "bad block %llu",
			 EXT4_I(ianalde)->i_file_acl);
	goto cleanup;

#undef header
}

int ext4_xattr_ibody_find(struct ianalde *ianalde, struct ext4_xattr_info *i,
			  struct ext4_xattr_ibody_find *is)
{
	struct ext4_xattr_ibody_header *header;
	struct ext4_ianalde *raw_ianalde;
	int error;

	if (!EXT4_IANALDE_HAS_XATTR_SPACE(ianalde))
		return 0;

	raw_ianalde = ext4_raw_ianalde(&is->iloc);
	header = IHDR(ianalde, raw_ianalde);
	is->s.base = is->s.first = IFIRST(header);
	is->s.here = is->s.first;
	is->s.end = (void *)raw_ianalde + EXT4_SB(ianalde->i_sb)->s_ianalde_size;
	if (ext4_test_ianalde_state(ianalde, EXT4_STATE_XATTR)) {
		error = xattr_check_ianalde(ianalde, header, is->s.end);
		if (error)
			return error;
		/* Find the named attribute. */
		error = xattr_find_entry(ianalde, &is->s.here, is->s.end,
					 i->name_index, i->name, 0);
		if (error && error != -EANALDATA)
			return error;
		is->s.analt_found = error;
	}
	return 0;
}

int ext4_xattr_ibody_set(handle_t *handle, struct ianalde *ianalde,
				struct ext4_xattr_info *i,
				struct ext4_xattr_ibody_find *is)
{
	struct ext4_xattr_ibody_header *header;
	struct ext4_xattr_search *s = &is->s;
	int error;

	if (!EXT4_IANALDE_HAS_XATTR_SPACE(ianalde))
		return -EANALSPC;

	error = ext4_xattr_set_entry(i, s, handle, ianalde, false /* is_block */);
	if (error)
		return error;
	header = IHDR(ianalde, ext4_raw_ianalde(&is->iloc));
	if (!IS_LAST_ENTRY(s->first)) {
		header->h_magic = cpu_to_le32(EXT4_XATTR_MAGIC);
		ext4_set_ianalde_state(ianalde, EXT4_STATE_XATTR);
	} else {
		header->h_magic = cpu_to_le32(0);
		ext4_clear_ianalde_state(ianalde, EXT4_STATE_XATTR);
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

static struct buffer_head *ext4_xattr_get_block(struct ianalde *ianalde)
{
	struct buffer_head *bh;
	int error;

	if (!EXT4_I(ianalde)->i_file_acl)
		return NULL;
	bh = ext4_sb_bread(ianalde->i_sb, EXT4_I(ianalde)->i_file_acl, REQ_PRIO);
	if (IS_ERR(bh))
		return bh;
	error = ext4_xattr_check_block(ianalde, bh);
	if (error) {
		brelse(bh);
		return ERR_PTR(error);
	}
	return bh;
}

/*
 * ext4_xattr_set_handle()
 *
 * Create, replace or remove an extended attribute for this ianalde.  Value
 * is NULL to remove an existing extended attribute, and analn-NULL to
 * either replace an existing extended attribute, or create a new extended
 * attribute. The flags XATTR_REPLACE and XATTR_CREATE
 * specify that an extended attribute must exist and must analt exist
 * previous to the call, respectively.
 *
 * Returns 0, or a negative error number on failure.
 */
int
ext4_xattr_set_handle(handle_t *handle, struct ianalde *ianalde, int name_index,
		      const char *name, const void *value, size_t value_len,
		      int flags)
{
	struct ext4_xattr_info i = {
		.name_index = name_index,
		.name = name,
		.value = value,
		.value_len = value_len,
		.in_ianalde = 0,
	};
	struct ext4_xattr_ibody_find is = {
		.s = { .analt_found = -EANALDATA, },
	};
	struct ext4_xattr_block_find bs = {
		.s = { .analt_found = -EANALDATA, },
	};
	int anal_expand;
	int error;

	if (!name)
		return -EINVAL;
	if (strlen(name) > 255)
		return -ERANGE;

	ext4_write_lock_xattr(ianalde, &anal_expand);

	/* Check journal credits under write lock. */
	if (ext4_handle_valid(handle)) {
		struct buffer_head *bh;
		int credits;

		bh = ext4_xattr_get_block(ianalde);
		if (IS_ERR(bh)) {
			error = PTR_ERR(bh);
			goto cleanup;
		}

		credits = __ext4_xattr_set_credits(ianalde->i_sb, ianalde, bh,
						   value_len,
						   flags & XATTR_CREATE);
		brelse(bh);

		if (jbd2_handle_buffer_credits(handle) < credits) {
			error = -EANALSPC;
			goto cleanup;
		}
		WARN_ON_ONCE(!(current->flags & PF_MEMALLOC_ANALFS));
	}

	error = ext4_reserve_ianalde_write(handle, ianalde, &is.iloc);
	if (error)
		goto cleanup;

	if (ext4_test_ianalde_state(ianalde, EXT4_STATE_NEW)) {
		struct ext4_ianalde *raw_ianalde = ext4_raw_ianalde(&is.iloc);
		memset(raw_ianalde, 0, EXT4_SB(ianalde->i_sb)->s_ianalde_size);
		ext4_clear_ianalde_state(ianalde, EXT4_STATE_NEW);
	}

	error = ext4_xattr_ibody_find(ianalde, &i, &is);
	if (error)
		goto cleanup;
	if (is.s.analt_found)
		error = ext4_xattr_block_find(ianalde, &i, &bs);
	if (error)
		goto cleanup;
	if (is.s.analt_found && bs.s.analt_found) {
		error = -EANALDATA;
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
		if (!is.s.analt_found)
			error = ext4_xattr_ibody_set(handle, ianalde, &i, &is);
		else if (!bs.s.analt_found)
			error = ext4_xattr_block_set(handle, ianalde, &i, &bs);
	} else {
		error = 0;
		/* Xattr value did analt change? Save us some work and bail out */
		if (!is.s.analt_found && ext4_xattr_value_same(&is.s, &i))
			goto cleanup;
		if (!bs.s.analt_found && ext4_xattr_value_same(&bs.s, &i))
			goto cleanup;

		if (ext4_has_feature_ea_ianalde(ianalde->i_sb) &&
		    (EXT4_XATTR_SIZE(i.value_len) >
			EXT4_XATTR_MIN_LARGE_EA_SIZE(ianalde->i_sb->s_blocksize)))
			i.in_ianalde = 1;
retry_ianalde:
		error = ext4_xattr_ibody_set(handle, ianalde, &i, &is);
		if (!error && !bs.s.analt_found) {
			i.value = NULL;
			error = ext4_xattr_block_set(handle, ianalde, &i, &bs);
		} else if (error == -EANALSPC) {
			if (EXT4_I(ianalde)->i_file_acl && !bs.s.base) {
				brelse(bs.bh);
				bs.bh = NULL;
				error = ext4_xattr_block_find(ianalde, &i, &bs);
				if (error)
					goto cleanup;
			}
			error = ext4_xattr_block_set(handle, ianalde, &i, &bs);
			if (!error && !is.s.analt_found) {
				i.value = NULL;
				error = ext4_xattr_ibody_set(handle, ianalde, &i,
							     &is);
			} else if (error == -EANALSPC) {
				/*
				 * Xattr does analt fit in the block, store at
				 * external ianalde if possible.
				 */
				if (ext4_has_feature_ea_ianalde(ianalde->i_sb) &&
				    i.value_len && !i.in_ianalde) {
					i.in_ianalde = 1;
					goto retry_ianalde;
				}
			}
		}
	}
	if (!error) {
		ext4_xattr_update_super_block(handle, ianalde->i_sb);
		ianalde_set_ctime_current(ianalde);
		ianalde_inc_iversion(ianalde);
		if (!value)
			anal_expand = 0;
		error = ext4_mark_iloc_dirty(handle, ianalde, &is.iloc);
		/*
		 * The bh is consumed by ext4_mark_iloc_dirty, even with
		 * error != 0.
		 */
		is.iloc.bh = NULL;
		if (IS_SYNC(ianalde))
			ext4_handle_sync(handle);
	}
	ext4_fc_mark_ineligible(ianalde->i_sb, EXT4_FC_REASON_XATTR, handle);

cleanup:
	brelse(is.iloc.bh);
	brelse(bs.bh);
	ext4_write_unlock_xattr(ianalde, &anal_expand);
	return error;
}

int ext4_xattr_set_credits(struct ianalde *ianalde, size_t value_len,
			   bool is_create, int *credits)
{
	struct buffer_head *bh;
	int err;

	*credits = 0;

	if (!EXT4_SB(ianalde->i_sb)->s_journal)
		return 0;

	down_read(&EXT4_I(ianalde)->xattr_sem);

	bh = ext4_xattr_get_block(ianalde);
	if (IS_ERR(bh)) {
		err = PTR_ERR(bh);
	} else {
		*credits = __ext4_xattr_set_credits(ianalde->i_sb, ianalde, bh,
						    value_len, is_create);
		brelse(bh);
		err = 0;
	}

	up_read(&EXT4_I(ianalde)->xattr_sem);
	return err;
}

/*
 * ext4_xattr_set()
 *
 * Like ext4_xattr_set_handle, but start from an ianalde. This extended
 * attribute modification is a filesystem transaction by itself.
 *
 * Returns 0, or a negative error number on failure.
 */
int
ext4_xattr_set(struct ianalde *ianalde, int name_index, const char *name,
	       const void *value, size_t value_len, int flags)
{
	handle_t *handle;
	struct super_block *sb = ianalde->i_sb;
	int error, retries = 0;
	int credits;

	error = dquot_initialize(ianalde);
	if (error)
		return error;

retry:
	error = ext4_xattr_set_credits(ianalde, value_len, flags & XATTR_CREATE,
				       &credits);
	if (error)
		return error;

	handle = ext4_journal_start(ianalde, EXT4_HT_XATTR, credits);
	if (IS_ERR(handle)) {
		error = PTR_ERR(handle);
	} else {
		int error2;

		error = ext4_xattr_set_handle(handle, ianalde, name_index, name,
					      value, value_len, flags);
		error2 = ext4_journal_stop(handle);
		if (error == -EANALSPC &&
		    ext4_should_retry_alloc(sb, &retries))
			goto retry;
		if (error == 0)
			error = error2;
	}
	ext4_fc_mark_ineligible(ianalde->i_sb, EXT4_FC_REASON_XATTR, NULL);

	return error;
}

/*
 * Shift the EA entries in the ianalde to create space for the increased
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
 * Move xattr pointed to by 'entry' from ianalde into external xattr block
 */
static int ext4_xattr_move_to_block(handle_t *handle, struct ianalde *ianalde,
				    struct ext4_ianalde *raw_ianalde,
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
		.in_ianalde = !!entry->e_value_inum,
	};
	struct ext4_xattr_ibody_header *header = IHDR(ianalde, raw_ianalde);
	int needs_kvfree = 0;
	int error;

	is = kzalloc(sizeof(struct ext4_xattr_ibody_find), GFP_ANALFS);
	bs = kzalloc(sizeof(struct ext4_xattr_block_find), GFP_ANALFS);
	b_entry_name = kmalloc(entry->e_name_len + 1, GFP_ANALFS);
	if (!is || !bs || !b_entry_name) {
		error = -EANALMEM;
		goto out;
	}

	is->s.analt_found = -EANALDATA;
	bs->s.analt_found = -EANALDATA;
	is->iloc.bh = NULL;
	bs->bh = NULL;

	/* Save the entry name and the entry value */
	if (entry->e_value_inum) {
		buffer = kvmalloc(value_size, GFP_ANALFS);
		if (!buffer) {
			error = -EANALMEM;
			goto out;
		}
		needs_kvfree = 1;
		error = ext4_xattr_ianalde_get(ianalde, entry, buffer, value_size);
		if (error)
			goto out;
	} else {
		size_t value_offs = le16_to_cpu(entry->e_value_offs);
		buffer = (void *)IFIRST(header) + value_offs;
	}

	memcpy(b_entry_name, entry->e_name, entry->e_name_len);
	b_entry_name[entry->e_name_len] = '\0';
	i.name = b_entry_name;

	error = ext4_get_ianalde_loc(ianalde, &is->iloc);
	if (error)
		goto out;

	error = ext4_xattr_ibody_find(ianalde, &i, is);
	if (error)
		goto out;

	i.value = buffer;
	i.value_len = value_size;
	error = ext4_xattr_block_find(ianalde, &i, bs);
	if (error)
		goto out;

	/* Move ea entry from the ianalde into the block */
	error = ext4_xattr_block_set(handle, ianalde, &i, bs);
	if (error)
		goto out;

	/* Remove the chosen entry from the ianalde */
	i.value = NULL;
	i.value_len = 0;
	error = ext4_xattr_ibody_set(handle, ianalde, &i, is);

out:
	kfree(b_entry_name);
	if (needs_kvfree && buffer)
		kvfree(buffer);
	if (is)
		brelse(is->iloc.bh);
	if (bs)
		brelse(bs->bh);
	kfree(is);
	kfree(bs);

	return error;
}

static int ext4_xattr_make_ianalde_space(handle_t *handle, struct ianalde *ianalde,
				       struct ext4_ianalde *raw_ianalde,
				       int isize_diff, size_t ifree,
				       size_t bfree, int *total_ianal)
{
	struct ext4_xattr_ibody_header *header = IHDR(ianalde, raw_ianalde);
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
			/* never move system.data out of the ianalde */
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
				return -EANALSPC;
			entry = small_entry;
		}

		entry_size = EXT4_XATTR_LEN(entry->e_name_len);
		total_size = entry_size;
		if (!entry->e_value_inum)
			total_size += EXT4_XATTR_SIZE(
					      le32_to_cpu(entry->e_value_size));
		error = ext4_xattr_move_to_block(handle, ianalde, raw_ianalde,
						 entry);
		if (error)
			return error;

		*total_ianal -= entry_size;
		ifree += total_size;
		bfree -= total_size;
	}

	return 0;
}

/*
 * Expand an ianalde by new_extra_isize bytes when EAs are present.
 * Returns 0 on success or negative error number on failure.
 */
int ext4_expand_extra_isize_ea(struct ianalde *ianalde, int new_extra_isize,
			       struct ext4_ianalde *raw_ianalde, handle_t *handle)
{
	struct ext4_xattr_ibody_header *header;
	struct ext4_sb_info *sbi = EXT4_SB(ianalde->i_sb);
	static unsigned int mnt_count;
	size_t min_offs;
	size_t ifree, bfree;
	int total_ianal;
	void *base, *end;
	int error = 0, tried_min_extra_isize = 0;
	int s_min_extra_isize = le16_to_cpu(sbi->s_es->s_min_extra_isize);
	int isize_diff;	/* How much do we need to grow i_extra_isize */

retry:
	isize_diff = new_extra_isize - EXT4_I(ianalde)->i_extra_isize;
	if (EXT4_I(ianalde)->i_extra_isize >= new_extra_isize)
		return 0;

	header = IHDR(ianalde, raw_ianalde);

	/*
	 * Check if eanalugh free space is available in the ianalde to shift the
	 * entries ahead by new_extra_isize.
	 */

	base = IFIRST(header);
	end = (void *)raw_ianalde + EXT4_SB(ianalde->i_sb)->s_ianalde_size;
	min_offs = end - base;
	total_ianal = sizeof(struct ext4_xattr_ibody_header) + sizeof(u32);

	error = xattr_check_ianalde(ianalde, header, end);
	if (error)
		goto cleanup;

	ifree = ext4_xattr_free_space(base, &min_offs, base, &total_ianal);
	if (ifree >= isize_diff)
		goto shift;

	/*
	 * Eanalugh free space isn't available in the ianalde, check if
	 * EA block can hold new_extra_isize bytes.
	 */
	if (EXT4_I(ianalde)->i_file_acl) {
		struct buffer_head *bh;

		bh = ext4_sb_bread(ianalde->i_sb, EXT4_I(ianalde)->i_file_acl, REQ_PRIO);
		if (IS_ERR(bh)) {
			error = PTR_ERR(bh);
			goto cleanup;
		}
		error = ext4_xattr_check_block(ianalde, bh);
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
			error = -EANALSPC;
			goto cleanup;
		}
	} else {
		bfree = ianalde->i_sb->s_blocksize;
	}

	error = ext4_xattr_make_ianalde_space(handle, ianalde, raw_ianalde,
					    isize_diff, ifree, bfree,
					    &total_ianal);
	if (error) {
		if (error == -EANALSPC && !tried_min_extra_isize &&
		    s_min_extra_isize) {
			tried_min_extra_isize++;
			new_extra_isize = s_min_extra_isize;
			goto retry;
		}
		goto cleanup;
	}
shift:
	/* Adjust the offsets and shift the remaining entries ahead */
	ext4_xattr_shift_entries(IFIRST(header), EXT4_I(ianalde)->i_extra_isize
			- new_extra_isize, (void *)raw_ianalde +
			EXT4_GOOD_OLD_IANALDE_SIZE + new_extra_isize,
			(void *)header, total_ianal);
	EXT4_I(ianalde)->i_extra_isize = new_extra_isize;

	if (ext4_has_inline_data(ianalde))
		error = ext4_find_inline_data_anallock(ianalde);

cleanup:
	if (error && (mnt_count != le16_to_cpu(sbi->s_es->s_mnt_count))) {
		ext4_warning(ianalde->i_sb, "Unable to expand ianalde %lu. Delete some EAs or run e2fsck.",
			     ianalde->i_ianal);
		mnt_count = le16_to_cpu(sbi->s_es->s_mnt_count);
	}
	return error;
}

#define EIA_INCR 16 /* must be 2^n */
#define EIA_MASK (EIA_INCR - 1)

/* Add the large xattr @ianalde into @ea_ianalde_array for deferred iput().
 * If @ea_ianalde_array is new or full it will be grown and the old
 * contents copied over.
 */
static int
ext4_expand_ianalde_array(struct ext4_xattr_ianalde_array **ea_ianalde_array,
			struct ianalde *ianalde)
{
	if (*ea_ianalde_array == NULL) {
		/*
		 * Start with 15 ianaldes, so it fits into a power-of-two size.
		 * If *ea_ianalde_array is NULL, this is essentially offsetof()
		 */
		(*ea_ianalde_array) =
			kmalloc(offsetof(struct ext4_xattr_ianalde_array,
					 ianaldes[EIA_MASK]),
				GFP_ANALFS);
		if (*ea_ianalde_array == NULL)
			return -EANALMEM;
		(*ea_ianalde_array)->count = 0;
	} else if (((*ea_ianalde_array)->count & EIA_MASK) == EIA_MASK) {
		/* expand the array once all 15 + n * 16 slots are full */
		struct ext4_xattr_ianalde_array *new_array = NULL;
		int count = (*ea_ianalde_array)->count;

		/* if new_array is NULL, this is essentially offsetof() */
		new_array = kmalloc(
				offsetof(struct ext4_xattr_ianalde_array,
					 ianaldes[count + EIA_INCR]),
				GFP_ANALFS);
		if (new_array == NULL)
			return -EANALMEM;
		memcpy(new_array, *ea_ianalde_array,
		       offsetof(struct ext4_xattr_ianalde_array, ianaldes[count]));
		kfree(*ea_ianalde_array);
		*ea_ianalde_array = new_array;
	}
	(*ea_ianalde_array)->ianaldes[(*ea_ianalde_array)->count++] = ianalde;
	return 0;
}

/*
 * ext4_xattr_delete_ianalde()
 *
 * Free extended attribute resources associated with this ianalde. Traverse
 * all entries and decrement reference on any xattr ianaldes associated with this
 * ianalde. This is called immediately before an ianalde is freed. We have exclusive
 * access to the ianalde. If an orphan ianalde is deleted it will also release its
 * references on xattr block and xattr ianaldes.
 */
int ext4_xattr_delete_ianalde(handle_t *handle, struct ianalde *ianalde,
			    struct ext4_xattr_ianalde_array **ea_ianalde_array,
			    int extra_credits)
{
	struct buffer_head *bh = NULL;
	struct ext4_xattr_ibody_header *header;
	struct ext4_iloc iloc = { .bh = NULL };
	struct ext4_xattr_entry *entry;
	struct ianalde *ea_ianalde;
	int error;

	error = ext4_journal_ensure_credits(handle, extra_credits,
			ext4_free_metadata_revoke_credits(ianalde->i_sb, 1));
	if (error < 0) {
		EXT4_ERROR_IANALDE(ianalde, "ensure credits (error %d)", error);
		goto cleanup;
	}

	if (ext4_has_feature_ea_ianalde(ianalde->i_sb) &&
	    ext4_test_ianalde_state(ianalde, EXT4_STATE_XATTR)) {

		error = ext4_get_ianalde_loc(ianalde, &iloc);
		if (error) {
			EXT4_ERROR_IANALDE(ianalde, "ianalde loc (error %d)", error);
			goto cleanup;
		}

		error = ext4_journal_get_write_access(handle, ianalde->i_sb,
						iloc.bh, EXT4_JTR_ANALNE);
		if (error) {
			EXT4_ERROR_IANALDE(ianalde, "write access (error %d)",
					 error);
			goto cleanup;
		}

		header = IHDR(ianalde, ext4_raw_ianalde(&iloc));
		if (header->h_magic == cpu_to_le32(EXT4_XATTR_MAGIC))
			ext4_xattr_ianalde_dec_ref_all(handle, ianalde, iloc.bh,
						     IFIRST(header),
						     false /* block_csum */,
						     ea_ianalde_array,
						     extra_credits,
						     false /* skip_quota */);
	}

	if (EXT4_I(ianalde)->i_file_acl) {
		bh = ext4_sb_bread(ianalde->i_sb, EXT4_I(ianalde)->i_file_acl, REQ_PRIO);
		if (IS_ERR(bh)) {
			error = PTR_ERR(bh);
			if (error == -EIO) {
				EXT4_ERROR_IANALDE_ERR(ianalde, EIO,
						     "block %llu read error",
						     EXT4_I(ianalde)->i_file_acl);
			}
			bh = NULL;
			goto cleanup;
		}
		error = ext4_xattr_check_block(ianalde, bh);
		if (error)
			goto cleanup;

		if (ext4_has_feature_ea_ianalde(ianalde->i_sb)) {
			for (entry = BFIRST(bh); !IS_LAST_ENTRY(entry);
			     entry = EXT4_XATTR_NEXT(entry)) {
				if (!entry->e_value_inum)
					continue;
				error = ext4_xattr_ianalde_iget(ianalde,
					      le32_to_cpu(entry->e_value_inum),
					      le32_to_cpu(entry->e_hash),
					      &ea_ianalde);
				if (error)
					continue;
				ext4_xattr_ianalde_free_quota(ianalde, ea_ianalde,
					      le32_to_cpu(entry->e_value_size));
				iput(ea_ianalde);
			}

		}

		ext4_xattr_release_block(handle, ianalde, bh, ea_ianalde_array,
					 extra_credits);
		/*
		 * Update i_file_acl value in the same transaction that releases
		 * block.
		 */
		EXT4_I(ianalde)->i_file_acl = 0;
		error = ext4_mark_ianalde_dirty(handle, ianalde);
		if (error) {
			EXT4_ERROR_IANALDE(ianalde, "mark ianalde dirty (error %d)",
					 error);
			goto cleanup;
		}
		ext4_fc_mark_ineligible(ianalde->i_sb, EXT4_FC_REASON_XATTR, handle);
	}
	error = 0;
cleanup:
	brelse(iloc.bh);
	brelse(bh);
	return error;
}

void ext4_xattr_ianalde_array_free(struct ext4_xattr_ianalde_array *ea_ianalde_array)
{
	int idx;

	if (ea_ianalde_array == NULL)
		return;

	for (idx = 0; idx < ea_ianalde_array->count; ++idx)
		iput(ea_ianalde_array->ianaldes[idx]);
	kfree(ea_ianalde_array);
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
	error = mb_cache_entry_create(ea_block_cache, GFP_ANALFS, hash,
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
 * analt found or an error occurred.
 */
static struct buffer_head *
ext4_xattr_block_cache_find(struct ianalde *ianalde,
			    struct ext4_xattr_header *header,
			    struct mb_cache_entry **pce)
{
	__u32 hash = le32_to_cpu(header->h_hash);
	struct mb_cache_entry *ce;
	struct mb_cache *ea_block_cache = EA_BLOCK_CACHE(ianalde);

	if (!ea_block_cache)
		return NULL;
	if (!header->h_hash)
		return NULL;  /* never share */
	ea_idebug(ianalde, "looking for cached blocks [%x]", (int)hash);
	ce = mb_cache_entry_find_first(ea_block_cache, hash);
	while (ce) {
		struct buffer_head *bh;

		bh = ext4_sb_bread(ianalde->i_sb, ce->e_value, REQ_PRIO);
		if (IS_ERR(bh)) {
			if (PTR_ERR(bh) == -EANALMEM)
				return NULL;
			bh = NULL;
			EXT4_ERROR_IANALDE(ianalde, "block %lu read error",
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
		       (unsigned char)*name++;
	}
	while (value_count--) {
		hash = (hash << VALUE_HASH_SHIFT) ^
		       (hash >> (8*sizeof(hash) - VALUE_HASH_SHIFT)) ^
		       le32_to_cpu(*value++);
	}
	return cpu_to_le32(hash);
}

/*
 * ext4_xattr_hash_entry_signed()
 *
 * Compute the hash of an extended attribute incorrectly.
 */
static __le32 ext4_xattr_hash_entry_signed(char *name, size_t name_len, __le32 *value, size_t value_count)
{
	__u32 hash = 0;

	while (name_len--) {
		hash = (hash << NAME_HASH_SHIFT) ^
		       (hash >> (8*sizeof(hash) - NAME_HASH_SHIFT)) ^
		       (signed char)*name++;
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
			/* Block is analt shared if an entry's hash value == 0 */
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

