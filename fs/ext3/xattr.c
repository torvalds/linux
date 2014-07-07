/*
 * linux/fs/ext3/xattr.c
 *
 * Copyright (C) 2001-2003 Andreas Gruenbacher, <agruen@suse.de>
 *
 * Fix by Harrison Xing <harrison@mountainviewdata.com>.
 * Ext3 code with a lot of help from Eric Jarman <ejarman@acm.org>.
 * Extended attributes for symlinks and special files added per
 *  suggestion of Luka Renko <luka.renko@hermes.si>.
 * xattr consolidation Copyright (c) 2004 James Morris <jmorris@redhat.com>,
 *  Red Hat Inc.
 * ea-in-inode support by Alex Tomas <alex@clusterfs.com> aka bzzz
 *  and Andreas Gruenbacher <agruen@suse.de>.
 */

/*
 * Extended attributes are stored directly in inodes (on file systems with
 * inodes bigger than 128 bytes) and on additional disk blocks. The i_file_acl
 * field contains the block number if an inode uses an additional block. All
 * attributes must fit in the inode and one additional block. Blocks that
 * contain the identical set of attributes may be shared among several inodes.
 * Identical blocks are detected by keeping a cache of blocks that have
 * recently been accessed.
 *
 * The attributes in inodes and on blocks have a different header; the entries
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
 * entry descriptors are kept sorted. In inodes, they are unsorted. The
 * attribute values are aligned to the end of the block in no specific order.
 *
 * Locking strategy
 * ----------------
 * EXT3_I(inode)->i_file_acl is protected by EXT3_I(inode)->xattr_sem.
 * EA blocks are only changed if they are exclusive to an inode, so
 * holding xattr_sem also means that nothing but the EA block's reference
 * count can change. Multiple writers to the same block are synchronized
 * by the buffer lock.
 */

#include "ext3.h"
#include <linux/mbcache.h>
#include <linux/quotaops.h>
#include "xattr.h"
#include "acl.h"

#define BHDR(bh) ((struct ext3_xattr_header *)((bh)->b_data))
#define ENTRY(ptr) ((struct ext3_xattr_entry *)(ptr))
#define BFIRST(bh) ENTRY(BHDR(bh)+1)
#define IS_LAST_ENTRY(entry) (*(__u32 *)(entry) == 0)

#define IHDR(inode, raw_inode) \
	((struct ext3_xattr_ibody_header *) \
		((void *)raw_inode + \
		 EXT3_GOOD_OLD_INODE_SIZE + \
		 EXT3_I(inode)->i_extra_isize))
#define IFIRST(hdr) ((struct ext3_xattr_entry *)((hdr)+1))

#ifdef EXT3_XATTR_DEBUG
# define ea_idebug(inode, f...) do { \
		printk(KERN_DEBUG "inode %s:%lu: ", \
			inode->i_sb->s_id, inode->i_ino); \
		printk(f); \
		printk("\n"); \
	} while (0)
# define ea_bdebug(bh, f...) do { \
		char b[BDEVNAME_SIZE]; \
		printk(KERN_DEBUG "block %s:%lu: ", \
			bdevname(bh->b_bdev, b), \
			(unsigned long) bh->b_blocknr); \
		printk(f); \
		printk("\n"); \
	} while (0)
#else
# define ea_idebug(f...)
# define ea_bdebug(f...)
#endif

static void ext3_xattr_cache_insert(struct buffer_head *);
static struct buffer_head *ext3_xattr_cache_find(struct inode *,
						 struct ext3_xattr_header *,
						 struct mb_cache_entry **);
static void ext3_xattr_rehash(struct ext3_xattr_header *,
			      struct ext3_xattr_entry *);
static int ext3_xattr_list(struct dentry *dentry, char *buffer,
			   size_t buffer_size);

static struct mb_cache *ext3_xattr_cache;

static const struct xattr_handler *ext3_xattr_handler_map[] = {
	[EXT3_XATTR_INDEX_USER]		     = &ext3_xattr_user_handler,
#ifdef CONFIG_EXT3_FS_POSIX_ACL
	[EXT3_XATTR_INDEX_POSIX_ACL_ACCESS]  = &posix_acl_access_xattr_handler,
	[EXT3_XATTR_INDEX_POSIX_ACL_DEFAULT] = &posix_acl_default_xattr_handler,
#endif
	[EXT3_XATTR_INDEX_TRUSTED]	     = &ext3_xattr_trusted_handler,
#ifdef CONFIG_EXT3_FS_SECURITY
	[EXT3_XATTR_INDEX_SECURITY]	     = &ext3_xattr_security_handler,
#endif
};

const struct xattr_handler *ext3_xattr_handlers[] = {
	&ext3_xattr_user_handler,
	&ext3_xattr_trusted_handler,
#ifdef CONFIG_EXT3_FS_POSIX_ACL
	&posix_acl_access_xattr_handler,
	&posix_acl_default_xattr_handler,
#endif
#ifdef CONFIG_EXT3_FS_SECURITY
	&ext3_xattr_security_handler,
#endif
	NULL
};

static inline const struct xattr_handler *
ext3_xattr_handler(int name_index)
{
	const struct xattr_handler *handler = NULL;

	if (name_index > 0 && name_index < ARRAY_SIZE(ext3_xattr_handler_map))
		handler = ext3_xattr_handler_map[name_index];
	return handler;
}

/*
 * Inode operation listxattr()
 *
 * dentry->d_inode->i_mutex: don't care
 */
ssize_t
ext3_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	return ext3_xattr_list(dentry, buffer, size);
}

static int
ext3_xattr_check_names(struct ext3_xattr_entry *entry, void *end)
{
	while (!IS_LAST_ENTRY(entry)) {
		struct ext3_xattr_entry *next = EXT3_XATTR_NEXT(entry);
		if ((void *)next >= end)
			return -EIO;
		entry = next;
	}
	return 0;
}

static inline int
ext3_xattr_check_block(struct buffer_head *bh)
{
	int error;

	if (BHDR(bh)->h_magic != cpu_to_le32(EXT3_XATTR_MAGIC) ||
	    BHDR(bh)->h_blocks != cpu_to_le32(1))
		return -EIO;
	error = ext3_xattr_check_names(BFIRST(bh), bh->b_data + bh->b_size);
	return error;
}

static inline int
ext3_xattr_check_entry(struct ext3_xattr_entry *entry, size_t size)
{
	size_t value_size = le32_to_cpu(entry->e_value_size);

	if (entry->e_value_block != 0 || value_size > size ||
	    le16_to_cpu(entry->e_value_offs) + value_size > size)
		return -EIO;
	return 0;
}

static int
ext3_xattr_find_entry(struct ext3_xattr_entry **pentry, int name_index,
		      const char *name, size_t size, int sorted)
{
	struct ext3_xattr_entry *entry;
	size_t name_len;
	int cmp = 1;

	if (name == NULL)
		return -EINVAL;
	name_len = strlen(name);
	entry = *pentry;
	for (; !IS_LAST_ENTRY(entry); entry = EXT3_XATTR_NEXT(entry)) {
		cmp = name_index - entry->e_name_index;
		if (!cmp)
			cmp = name_len - entry->e_name_len;
		if (!cmp)
			cmp = memcmp(name, entry->e_name, name_len);
		if (cmp <= 0 && (sorted || cmp == 0))
			break;
	}
	*pentry = entry;
	if (!cmp && ext3_xattr_check_entry(entry, size))
			return -EIO;
	return cmp ? -ENODATA : 0;
}

static int
ext3_xattr_block_get(struct inode *inode, int name_index, const char *name,
		     void *buffer, size_t buffer_size)
{
	struct buffer_head *bh = NULL;
	struct ext3_xattr_entry *entry;
	size_t size;
	int error;

	ea_idebug(inode, "name=%d.%s, buffer=%p, buffer_size=%ld",
		  name_index, name, buffer, (long)buffer_size);

	error = -ENODATA;
	if (!EXT3_I(inode)->i_file_acl)
		goto cleanup;
	ea_idebug(inode, "reading block %u", EXT3_I(inode)->i_file_acl);
	bh = sb_bread(inode->i_sb, EXT3_I(inode)->i_file_acl);
	if (!bh)
		goto cleanup;
	ea_bdebug(bh, "b_count=%d, refcount=%d",
		atomic_read(&(bh->b_count)), le32_to_cpu(BHDR(bh)->h_refcount));
	if (ext3_xattr_check_block(bh)) {
bad_block:	ext3_error(inode->i_sb, __func__,
			   "inode %lu: bad block "E3FSBLK, inode->i_ino,
			   EXT3_I(inode)->i_file_acl);
		error = -EIO;
		goto cleanup;
	}
	ext3_xattr_cache_insert(bh);
	entry = BFIRST(bh);
	error = ext3_xattr_find_entry(&entry, name_index, name, bh->b_size, 1);
	if (error == -EIO)
		goto bad_block;
	if (error)
		goto cleanup;
	size = le32_to_cpu(entry->e_value_size);
	if (buffer) {
		error = -ERANGE;
		if (size > buffer_size)
			goto cleanup;
		memcpy(buffer, bh->b_data + le16_to_cpu(entry->e_value_offs),
		       size);
	}
	error = size;

cleanup:
	brelse(bh);
	return error;
}

static int
ext3_xattr_ibody_get(struct inode *inode, int name_index, const char *name,
		     void *buffer, size_t buffer_size)
{
	struct ext3_xattr_ibody_header *header;
	struct ext3_xattr_entry *entry;
	struct ext3_inode *raw_inode;
	struct ext3_iloc iloc;
	size_t size;
	void *end;
	int error;

	if (!ext3_test_inode_state(inode, EXT3_STATE_XATTR))
		return -ENODATA;
	error = ext3_get_inode_loc(inode, &iloc);
	if (error)
		return error;
	raw_inode = ext3_raw_inode(&iloc);
	header = IHDR(inode, raw_inode);
	entry = IFIRST(header);
	end = (void *)raw_inode + EXT3_SB(inode->i_sb)->s_inode_size;
	error = ext3_xattr_check_names(entry, end);
	if (error)
		goto cleanup;
	error = ext3_xattr_find_entry(&entry, name_index, name,
				      end - (void *)entry, 0);
	if (error)
		goto cleanup;
	size = le32_to_cpu(entry->e_value_size);
	if (buffer) {
		error = -ERANGE;
		if (size > buffer_size)
			goto cleanup;
		memcpy(buffer, (void *)IFIRST(header) +
		       le16_to_cpu(entry->e_value_offs), size);
	}
	error = size;

cleanup:
	brelse(iloc.bh);
	return error;
}

/*
 * ext3_xattr_get()
 *
 * Copy an extended attribute into the buffer
 * provided, or compute the buffer size required.
 * Buffer is NULL to compute the size of the buffer required.
 *
 * Returns a negative error number on failure, or the number of bytes
 * used / required on success.
 */
int
ext3_xattr_get(struct inode *inode, int name_index, const char *name,
	       void *buffer, size_t buffer_size)
{
	int error;

	down_read(&EXT3_I(inode)->xattr_sem);
	error = ext3_xattr_ibody_get(inode, name_index, name, buffer,
				     buffer_size);
	if (error == -ENODATA)
		error = ext3_xattr_block_get(inode, name_index, name, buffer,
					     buffer_size);
	up_read(&EXT3_I(inode)->xattr_sem);
	return error;
}

static int
ext3_xattr_list_entries(struct dentry *dentry, struct ext3_xattr_entry *entry,
			char *buffer, size_t buffer_size)
{
	size_t rest = buffer_size;

	for (; !IS_LAST_ENTRY(entry); entry = EXT3_XATTR_NEXT(entry)) {
		const struct xattr_handler *handler =
			ext3_xattr_handler(entry->e_name_index);

		if (handler) {
			size_t size = handler->list(dentry, buffer, rest,
						    entry->e_name,
						    entry->e_name_len,
						    handler->flags);
			if (buffer) {
				if (size > rest)
					return -ERANGE;
				buffer += size;
			}
			rest -= size;
		}
	}
	return buffer_size - rest;
}

static int
ext3_xattr_block_list(struct dentry *dentry, char *buffer, size_t buffer_size)
{
	struct inode *inode = dentry->d_inode;
	struct buffer_head *bh = NULL;
	int error;

	ea_idebug(inode, "buffer=%p, buffer_size=%ld",
		  buffer, (long)buffer_size);

	error = 0;
	if (!EXT3_I(inode)->i_file_acl)
		goto cleanup;
	ea_idebug(inode, "reading block %u", EXT3_I(inode)->i_file_acl);
	bh = sb_bread(inode->i_sb, EXT3_I(inode)->i_file_acl);
	error = -EIO;
	if (!bh)
		goto cleanup;
	ea_bdebug(bh, "b_count=%d, refcount=%d",
		atomic_read(&(bh->b_count)), le32_to_cpu(BHDR(bh)->h_refcount));
	if (ext3_xattr_check_block(bh)) {
		ext3_error(inode->i_sb, __func__,
			   "inode %lu: bad block "E3FSBLK, inode->i_ino,
			   EXT3_I(inode)->i_file_acl);
		error = -EIO;
		goto cleanup;
	}
	ext3_xattr_cache_insert(bh);
	error = ext3_xattr_list_entries(dentry, BFIRST(bh), buffer, buffer_size);

cleanup:
	brelse(bh);

	return error;
}

static int
ext3_xattr_ibody_list(struct dentry *dentry, char *buffer, size_t buffer_size)
{
	struct inode *inode = dentry->d_inode;
	struct ext3_xattr_ibody_header *header;
	struct ext3_inode *raw_inode;
	struct ext3_iloc iloc;
	void *end;
	int error;

	if (!ext3_test_inode_state(inode, EXT3_STATE_XATTR))
		return 0;
	error = ext3_get_inode_loc(inode, &iloc);
	if (error)
		return error;
	raw_inode = ext3_raw_inode(&iloc);
	header = IHDR(inode, raw_inode);
	end = (void *)raw_inode + EXT3_SB(inode->i_sb)->s_inode_size;
	error = ext3_xattr_check_names(IFIRST(header), end);
	if (error)
		goto cleanup;
	error = ext3_xattr_list_entries(dentry, IFIRST(header),
					buffer, buffer_size);

cleanup:
	brelse(iloc.bh);
	return error;
}

/*
 * ext3_xattr_list()
 *
 * Copy a list of attribute names into the buffer
 * provided, or compute the buffer size required.
 * Buffer is NULL to compute the size of the buffer required.
 *
 * Returns a negative error number on failure, or the number of bytes
 * used / required on success.
 */
static int
ext3_xattr_list(struct dentry *dentry, char *buffer, size_t buffer_size)
{
	int i_error, b_error;

	down_read(&EXT3_I(dentry->d_inode)->xattr_sem);
	i_error = ext3_xattr_ibody_list(dentry, buffer, buffer_size);
	if (i_error < 0) {
		b_error = 0;
	} else {
		if (buffer) {
			buffer += i_error;
			buffer_size -= i_error;
		}
		b_error = ext3_xattr_block_list(dentry, buffer, buffer_size);
		if (b_error < 0)
			i_error = 0;
	}
	up_read(&EXT3_I(dentry->d_inode)->xattr_sem);
	return i_error + b_error;
}

/*
 * If the EXT3_FEATURE_COMPAT_EXT_ATTR feature of this file system is
 * not set, set it.
 */
static void ext3_xattr_update_super_block(handle_t *handle,
					  struct super_block *sb)
{
	if (EXT3_HAS_COMPAT_FEATURE(sb, EXT3_FEATURE_COMPAT_EXT_ATTR))
		return;

	if (ext3_journal_get_write_access(handle, EXT3_SB(sb)->s_sbh) == 0) {
		EXT3_SET_COMPAT_FEATURE(sb, EXT3_FEATURE_COMPAT_EXT_ATTR);
		ext3_journal_dirty_metadata(handle, EXT3_SB(sb)->s_sbh);
	}
}

/*
 * Release the xattr block BH: If the reference count is > 1, decrement
 * it; otherwise free the block.
 */
static void
ext3_xattr_release_block(handle_t *handle, struct inode *inode,
			 struct buffer_head *bh)
{
	struct mb_cache_entry *ce = NULL;
	int error = 0;

	ce = mb_cache_entry_get(ext3_xattr_cache, bh->b_bdev, bh->b_blocknr);
	error = ext3_journal_get_write_access(handle, bh);
	if (error)
		 goto out;

	lock_buffer(bh);

	if (BHDR(bh)->h_refcount == cpu_to_le32(1)) {
		ea_bdebug(bh, "refcount now=0; freeing");
		if (ce)
			mb_cache_entry_free(ce);
		ext3_free_blocks(handle, inode, bh->b_blocknr, 1);
		get_bh(bh);
		ext3_forget(handle, 1, inode, bh, bh->b_blocknr);
	} else {
		le32_add_cpu(&BHDR(bh)->h_refcount, -1);
		error = ext3_journal_dirty_metadata(handle, bh);
		if (IS_SYNC(inode))
			handle->h_sync = 1;
		dquot_free_block(inode, 1);
		ea_bdebug(bh, "refcount now=%d; releasing",
			  le32_to_cpu(BHDR(bh)->h_refcount));
		if (ce)
			mb_cache_entry_release(ce);
	}
	unlock_buffer(bh);
out:
	ext3_std_error(inode->i_sb, error);
	return;
}

struct ext3_xattr_info {
	int name_index;
	const char *name;
	const void *value;
	size_t value_len;
};

struct ext3_xattr_search {
	struct ext3_xattr_entry *first;
	void *base;
	void *end;
	struct ext3_xattr_entry *here;
	int not_found;
};

static int
ext3_xattr_set_entry(struct ext3_xattr_info *i, struct ext3_xattr_search *s)
{
	struct ext3_xattr_entry *last;
	size_t free, min_offs = s->end - s->base, name_len = strlen(i->name);

	/* Compute min_offs and last. */
	last = s->first;
	for (; !IS_LAST_ENTRY(last); last = EXT3_XATTR_NEXT(last)) {
		if (!last->e_value_block && last->e_value_size) {
			size_t offs = le16_to_cpu(last->e_value_offs);
			if (offs < min_offs)
				min_offs = offs;
		}
	}
	free = min_offs - ((void *)last - s->base) - sizeof(__u32);
	if (!s->not_found) {
		if (!s->here->e_value_block && s->here->e_value_size) {
			size_t size = le32_to_cpu(s->here->e_value_size);
			free += EXT3_XATTR_SIZE(size);
		}
		free += EXT3_XATTR_LEN(name_len);
	}
	if (i->value) {
		if (free < EXT3_XATTR_SIZE(i->value_len) ||
		    free < EXT3_XATTR_LEN(name_len) +
			   EXT3_XATTR_SIZE(i->value_len))
			return -ENOSPC;
	}

	if (i->value && s->not_found) {
		/* Insert the new name. */
		size_t size = EXT3_XATTR_LEN(name_len);
		size_t rest = (void *)last - (void *)s->here + sizeof(__u32);
		memmove((void *)s->here + size, s->here, rest);
		memset(s->here, 0, size);
		s->here->e_name_index = i->name_index;
		s->here->e_name_len = name_len;
		memcpy(s->here->e_name, i->name, name_len);
	} else {
		if (!s->here->e_value_block && s->here->e_value_size) {
			void *first_val = s->base + min_offs;
			size_t offs = le16_to_cpu(s->here->e_value_offs);
			void *val = s->base + offs;
			size_t size = EXT3_XATTR_SIZE(
				le32_to_cpu(s->here->e_value_size));

			if (i->value && size == EXT3_XATTR_SIZE(i->value_len)) {
				/* The old and the new value have the same
				   size. Just replace. */
				s->here->e_value_size =
					cpu_to_le32(i->value_len);
				memset(val + size - EXT3_XATTR_PAD, 0,
				       EXT3_XATTR_PAD); /* Clear pad bytes. */
				memcpy(val, i->value, i->value_len);
				return 0;
			}

			/* Remove the old value. */
			memmove(first_val + size, first_val, val - first_val);
			memset(first_val, 0, size);
			s->here->e_value_size = 0;
			s->here->e_value_offs = 0;
			min_offs += size;

			/* Adjust all value offsets. */
			last = s->first;
			while (!IS_LAST_ENTRY(last)) {
				size_t o = le16_to_cpu(last->e_value_offs);
				if (!last->e_value_block &&
				    last->e_value_size && o < offs)
					last->e_value_offs =
						cpu_to_le16(o + size);
				last = EXT3_XATTR_NEXT(last);
			}
		}
		if (!i->value) {
			/* Remove the old name. */
			size_t size = EXT3_XATTR_LEN(name_len);
			last = ENTRY((void *)last - size);
			memmove(s->here, (void *)s->here + size,
				(void *)last - (void *)s->here + sizeof(__u32));
			memset(last, 0, size);
		}
	}

	if (i->value) {
		/* Insert the new value. */
		s->here->e_value_size = cpu_to_le32(i->value_len);
		if (i->value_len) {
			size_t size = EXT3_XATTR_SIZE(i->value_len);
			void *val = s->base + min_offs - size;
			s->here->e_value_offs = cpu_to_le16(min_offs - size);
			memset(val + size - EXT3_XATTR_PAD, 0,
			       EXT3_XATTR_PAD); /* Clear the pad bytes. */
			memcpy(val, i->value, i->value_len);
		}
	}
	return 0;
}

struct ext3_xattr_block_find {
	struct ext3_xattr_search s;
	struct buffer_head *bh;
};

static int
ext3_xattr_block_find(struct inode *inode, struct ext3_xattr_info *i,
		      struct ext3_xattr_block_find *bs)
{
	struct super_block *sb = inode->i_sb;
	int error;

	ea_idebug(inode, "name=%d.%s, value=%p, value_len=%ld",
		  i->name_index, i->name, i->value, (long)i->value_len);

	if (EXT3_I(inode)->i_file_acl) {
		/* The inode already has an extended attribute block. */
		bs->bh = sb_bread(sb, EXT3_I(inode)->i_file_acl);
		error = -EIO;
		if (!bs->bh)
			goto cleanup;
		ea_bdebug(bs->bh, "b_count=%d, refcount=%d",
			atomic_read(&(bs->bh->b_count)),
			le32_to_cpu(BHDR(bs->bh)->h_refcount));
		if (ext3_xattr_check_block(bs->bh)) {
			ext3_error(sb, __func__,
				"inode %lu: bad block "E3FSBLK, inode->i_ino,
				EXT3_I(inode)->i_file_acl);
			error = -EIO;
			goto cleanup;
		}
		/* Find the named attribute. */
		bs->s.base = BHDR(bs->bh);
		bs->s.first = BFIRST(bs->bh);
		bs->s.end = bs->bh->b_data + bs->bh->b_size;
		bs->s.here = bs->s.first;
		error = ext3_xattr_find_entry(&bs->s.here, i->name_index,
					      i->name, bs->bh->b_size, 1);
		if (error && error != -ENODATA)
			goto cleanup;
		bs->s.not_found = error;
	}
	error = 0;

cleanup:
	return error;
}

static int
ext3_xattr_block_set(handle_t *handle, struct inode *inode,
		     struct ext3_xattr_info *i,
		     struct ext3_xattr_block_find *bs)
{
	struct super_block *sb = inode->i_sb;
	struct buffer_head *new_bh = NULL;
	struct ext3_xattr_search *s = &bs->s;
	struct mb_cache_entry *ce = NULL;
	int error = 0;

#define header(x) ((struct ext3_xattr_header *)(x))

	if (i->value && i->value_len > sb->s_blocksize)
		return -ENOSPC;
	if (s->base) {
		ce = mb_cache_entry_get(ext3_xattr_cache, bs->bh->b_bdev,
					bs->bh->b_blocknr);
		error = ext3_journal_get_write_access(handle, bs->bh);
		if (error)
			goto cleanup;
		lock_buffer(bs->bh);

		if (header(s->base)->h_refcount == cpu_to_le32(1)) {
			if (ce) {
				mb_cache_entry_free(ce);
				ce = NULL;
			}
			ea_bdebug(bs->bh, "modifying in-place");
			error = ext3_xattr_set_entry(i, s);
			if (!error) {
				if (!IS_LAST_ENTRY(s->first))
					ext3_xattr_rehash(header(s->base),
							  s->here);
				ext3_xattr_cache_insert(bs->bh);
			}
			unlock_buffer(bs->bh);
			if (error == -EIO)
				goto bad_block;
			if (!error)
				error = ext3_journal_dirty_metadata(handle,
								    bs->bh);
			if (error)
				goto cleanup;
			goto inserted;
		} else {
			int offset = (char *)s->here - bs->bh->b_data;

			unlock_buffer(bs->bh);
			journal_release_buffer(handle, bs->bh);

			if (ce) {
				mb_cache_entry_release(ce);
				ce = NULL;
			}
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
		}
	} else {
		/* Allocate a buffer where we construct the new block. */
		s->base = kzalloc(sb->s_blocksize, GFP_NOFS);
		/* assert(header == s->base) */
		error = -ENOMEM;
		if (s->base == NULL)
			goto cleanup;
		header(s->base)->h_magic = cpu_to_le32(EXT3_XATTR_MAGIC);
		header(s->base)->h_blocks = cpu_to_le32(1);
		header(s->base)->h_refcount = cpu_to_le32(1);
		s->first = ENTRY(header(s->base)+1);
		s->here = ENTRY(header(s->base)+1);
		s->end = s->base + sb->s_blocksize;
	}

	error = ext3_xattr_set_entry(i, s);
	if (error == -EIO)
		goto bad_block;
	if (error)
		goto cleanup;
	if (!IS_LAST_ENTRY(s->first))
		ext3_xattr_rehash(header(s->base), s->here);

inserted:
	if (!IS_LAST_ENTRY(s->first)) {
		new_bh = ext3_xattr_cache_find(inode, header(s->base), &ce);
		if (new_bh) {
			/* We found an identical block in the cache. */
			if (new_bh == bs->bh)
				ea_bdebug(new_bh, "keeping");
			else {
				/* The old block is released after updating
				   the inode. */
				error = dquot_alloc_block(inode, 1);
				if (error)
					goto cleanup;
				error = ext3_journal_get_write_access(handle,
								      new_bh);
				if (error)
					goto cleanup_dquot;
				lock_buffer(new_bh);
				le32_add_cpu(&BHDR(new_bh)->h_refcount, 1);
				ea_bdebug(new_bh, "reusing; refcount now=%d",
					le32_to_cpu(BHDR(new_bh)->h_refcount));
				unlock_buffer(new_bh);
				error = ext3_journal_dirty_metadata(handle,
								    new_bh);
				if (error)
					goto cleanup_dquot;
			}
			mb_cache_entry_release(ce);
			ce = NULL;
		} else if (bs->bh && s->base == bs->bh->b_data) {
			/* We were modifying this block in-place. */
			ea_bdebug(bs->bh, "keeping this block");
			new_bh = bs->bh;
			get_bh(new_bh);
		} else {
			/* We need to allocate a new block */
			ext3_fsblk_t goal = ext3_group_first_block_no(sb,
						EXT3_I(inode)->i_block_group);
			ext3_fsblk_t block;

			/*
			 * Protect us agaist concurrent allocations to the
			 * same inode from ext3_..._writepage(). Reservation
			 * code does not expect racing allocations.
			 */
			mutex_lock(&EXT3_I(inode)->truncate_mutex);
			block = ext3_new_block(handle, inode, goal, &error);
			mutex_unlock(&EXT3_I(inode)->truncate_mutex);
			if (error)
				goto cleanup;
			ea_idebug(inode, "creating block %d", block);

			new_bh = sb_getblk(sb, block);
			if (unlikely(!new_bh)) {
getblk_failed:
				ext3_free_blocks(handle, inode, block, 1);
				error = -ENOMEM;
				goto cleanup;
			}
			lock_buffer(new_bh);
			error = ext3_journal_get_create_access(handle, new_bh);
			if (error) {
				unlock_buffer(new_bh);
				goto getblk_failed;
			}
			memcpy(new_bh->b_data, s->base, new_bh->b_size);
			set_buffer_uptodate(new_bh);
			unlock_buffer(new_bh);
			ext3_xattr_cache_insert(new_bh);
			error = ext3_journal_dirty_metadata(handle, new_bh);
			if (error)
				goto cleanup;
		}
	}

	/* Update the inode. */
	EXT3_I(inode)->i_file_acl = new_bh ? new_bh->b_blocknr : 0;

	/* Drop the previous xattr block. */
	if (bs->bh && bs->bh != new_bh)
		ext3_xattr_release_block(handle, inode, bs->bh);
	error = 0;

cleanup:
	if (ce)
		mb_cache_entry_release(ce);
	brelse(new_bh);
	if (!(bs->bh && s->base == bs->bh->b_data))
		kfree(s->base);

	return error;

cleanup_dquot:
	dquot_free_block(inode, 1);
	goto cleanup;

bad_block:
	ext3_error(inode->i_sb, __func__,
		   "inode %lu: bad block "E3FSBLK, inode->i_ino,
		   EXT3_I(inode)->i_file_acl);
	goto cleanup;

#undef header
}

struct ext3_xattr_ibody_find {
	struct ext3_xattr_search s;
	struct ext3_iloc iloc;
};

static int
ext3_xattr_ibody_find(struct inode *inode, struct ext3_xattr_info *i,
		      struct ext3_xattr_ibody_find *is)
{
	struct ext3_xattr_ibody_header *header;
	struct ext3_inode *raw_inode;
	int error;

	if (EXT3_I(inode)->i_extra_isize == 0)
		return 0;
	raw_inode = ext3_raw_inode(&is->iloc);
	header = IHDR(inode, raw_inode);
	is->s.base = is->s.first = IFIRST(header);
	is->s.here = is->s.first;
	is->s.end = (void *)raw_inode + EXT3_SB(inode->i_sb)->s_inode_size;
	if (ext3_test_inode_state(inode, EXT3_STATE_XATTR)) {
		error = ext3_xattr_check_names(IFIRST(header), is->s.end);
		if (error)
			return error;
		/* Find the named attribute. */
		error = ext3_xattr_find_entry(&is->s.here, i->name_index,
					      i->name, is->s.end -
					      (void *)is->s.base, 0);
		if (error && error != -ENODATA)
			return error;
		is->s.not_found = error;
	}
	return 0;
}

static int
ext3_xattr_ibody_set(handle_t *handle, struct inode *inode,
		     struct ext3_xattr_info *i,
		     struct ext3_xattr_ibody_find *is)
{
	struct ext3_xattr_ibody_header *header;
	struct ext3_xattr_search *s = &is->s;
	int error;

	if (EXT3_I(inode)->i_extra_isize == 0)
		return -ENOSPC;
	error = ext3_xattr_set_entry(i, s);
	if (error)
		return error;
	header = IHDR(inode, ext3_raw_inode(&is->iloc));
	if (!IS_LAST_ENTRY(s->first)) {
		header->h_magic = cpu_to_le32(EXT3_XATTR_MAGIC);
		ext3_set_inode_state(inode, EXT3_STATE_XATTR);
	} else {
		header->h_magic = cpu_to_le32(0);
		ext3_clear_inode_state(inode, EXT3_STATE_XATTR);
	}
	return 0;
}

/*
 * ext3_xattr_set_handle()
 *
 * Create, replace or remove an extended attribute for this inode.  Value
 * is NULL to remove an existing extended attribute, and non-NULL to
 * either replace an existing extended attribute, or create a new extended
 * attribute. The flags XATTR_REPLACE and XATTR_CREATE
 * specify that an extended attribute must exist and must not exist
 * previous to the call, respectively.
 *
 * Returns 0, or a negative error number on failure.
 */
int
ext3_xattr_set_handle(handle_t *handle, struct inode *inode, int name_index,
		      const char *name, const void *value, size_t value_len,
		      int flags)
{
	struct ext3_xattr_info i = {
		.name_index = name_index,
		.name = name,
		.value = value,
		.value_len = value_len,

	};
	struct ext3_xattr_ibody_find is = {
		.s = { .not_found = -ENODATA, },
	};
	struct ext3_xattr_block_find bs = {
		.s = { .not_found = -ENODATA, },
	};
	int error;

	if (!name)
		return -EINVAL;
	if (strlen(name) > 255)
		return -ERANGE;
	down_write(&EXT3_I(inode)->xattr_sem);
	error = ext3_get_inode_loc(inode, &is.iloc);
	if (error)
		goto cleanup;

	error = ext3_journal_get_write_access(handle, is.iloc.bh);
	if (error)
		goto cleanup;

	if (ext3_test_inode_state(inode, EXT3_STATE_NEW)) {
		struct ext3_inode *raw_inode = ext3_raw_inode(&is.iloc);
		memset(raw_inode, 0, EXT3_SB(inode->i_sb)->s_inode_size);
		ext3_clear_inode_state(inode, EXT3_STATE_NEW);
	}

	error = ext3_xattr_ibody_find(inode, &i, &is);
	if (error)
		goto cleanup;
	if (is.s.not_found)
		error = ext3_xattr_block_find(inode, &i, &bs);
	if (error)
		goto cleanup;
	if (is.s.not_found && bs.s.not_found) {
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
		if (!is.s.not_found)
			error = ext3_xattr_ibody_set(handle, inode, &i, &is);
		else if (!bs.s.not_found)
			error = ext3_xattr_block_set(handle, inode, &i, &bs);
	} else {
		error = ext3_xattr_ibody_set(handle, inode, &i, &is);
		if (!error && !bs.s.not_found) {
			i.value = NULL;
			error = ext3_xattr_block_set(handle, inode, &i, &bs);
		} else if (error == -ENOSPC) {
			if (EXT3_I(inode)->i_file_acl && !bs.s.base) {
				error = ext3_xattr_block_find(inode, &i, &bs);
				if (error)
					goto cleanup;
			}
			error = ext3_xattr_block_set(handle, inode, &i, &bs);
			if (error)
				goto cleanup;
			if (!is.s.not_found) {
				i.value = NULL;
				error = ext3_xattr_ibody_set(handle, inode, &i,
							     &is);
			}
		}
	}
	if (!error) {
		ext3_xattr_update_super_block(handle, inode->i_sb);
		inode->i_ctime = CURRENT_TIME_SEC;
		error = ext3_mark_iloc_dirty(handle, inode, &is.iloc);
		/*
		 * The bh is consumed by ext3_mark_iloc_dirty, even with
		 * error != 0.
		 */
		is.iloc.bh = NULL;
		if (IS_SYNC(inode))
			handle->h_sync = 1;
	}

cleanup:
	brelse(is.iloc.bh);
	brelse(bs.bh);
	up_write(&EXT3_I(inode)->xattr_sem);
	return error;
}

/*
 * ext3_xattr_set()
 *
 * Like ext3_xattr_set_handle, but start from an inode. This extended
 * attribute modification is a filesystem transaction by itself.
 *
 * Returns 0, or a negative error number on failure.
 */
int
ext3_xattr_set(struct inode *inode, int name_index, const char *name,
	       const void *value, size_t value_len, int flags)
{
	handle_t *handle;
	int error, retries = 0;

retry:
	handle = ext3_journal_start(inode, EXT3_DATA_TRANS_BLOCKS(inode->i_sb));
	if (IS_ERR(handle)) {
		error = PTR_ERR(handle);
	} else {
		int error2;

		error = ext3_xattr_set_handle(handle, inode, name_index, name,
					      value, value_len, flags);
		error2 = ext3_journal_stop(handle);
		if (error == -ENOSPC &&
		    ext3_should_retry_alloc(inode->i_sb, &retries))
			goto retry;
		if (error == 0)
			error = error2;
	}

	return error;
}

/*
 * ext3_xattr_delete_inode()
 *
 * Free extended attribute resources associated with this inode. This
 * is called immediately before an inode is freed. We have exclusive
 * access to the inode.
 */
void
ext3_xattr_delete_inode(handle_t *handle, struct inode *inode)
{
	struct buffer_head *bh = NULL;

	if (!EXT3_I(inode)->i_file_acl)
		goto cleanup;
	bh = sb_bread(inode->i_sb, EXT3_I(inode)->i_file_acl);
	if (!bh) {
		ext3_error(inode->i_sb, __func__,
			"inode %lu: block "E3FSBLK" read error", inode->i_ino,
			EXT3_I(inode)->i_file_acl);
		goto cleanup;
	}
	if (BHDR(bh)->h_magic != cpu_to_le32(EXT3_XATTR_MAGIC) ||
	    BHDR(bh)->h_blocks != cpu_to_le32(1)) {
		ext3_error(inode->i_sb, __func__,
			"inode %lu: bad block "E3FSBLK, inode->i_ino,
			EXT3_I(inode)->i_file_acl);
		goto cleanup;
	}
	ext3_xattr_release_block(handle, inode, bh);
	EXT3_I(inode)->i_file_acl = 0;

cleanup:
	brelse(bh);
}

/*
 * ext3_xattr_put_super()
 *
 * This is called when a file system is unmounted.
 */
void
ext3_xattr_put_super(struct super_block *sb)
{
	mb_cache_shrink(sb->s_bdev);
}

/*
 * ext3_xattr_cache_insert()
 *
 * Create a new entry in the extended attribute cache, and insert
 * it unless such an entry is already in the cache.
 *
 * Returns 0, or a negative error number on failure.
 */
static void
ext3_xattr_cache_insert(struct buffer_head *bh)
{
	__u32 hash = le32_to_cpu(BHDR(bh)->h_hash);
	struct mb_cache_entry *ce;
	int error;

	ce = mb_cache_entry_alloc(ext3_xattr_cache, GFP_NOFS);
	if (!ce) {
		ea_bdebug(bh, "out of memory");
		return;
	}
	error = mb_cache_entry_insert(ce, bh->b_bdev, bh->b_blocknr, hash);
	if (error) {
		mb_cache_entry_free(ce);
		if (error == -EBUSY) {
			ea_bdebug(bh, "already in cache");
			error = 0;
		}
	} else {
		ea_bdebug(bh, "inserting [%x]", (int)hash);
		mb_cache_entry_release(ce);
	}
}

/*
 * ext3_xattr_cmp()
 *
 * Compare two extended attribute blocks for equality.
 *
 * Returns 0 if the blocks are equal, 1 if they differ, and
 * a negative error number on errors.
 */
static int
ext3_xattr_cmp(struct ext3_xattr_header *header1,
	       struct ext3_xattr_header *header2)
{
	struct ext3_xattr_entry *entry1, *entry2;

	entry1 = ENTRY(header1+1);
	entry2 = ENTRY(header2+1);
	while (!IS_LAST_ENTRY(entry1)) {
		if (IS_LAST_ENTRY(entry2))
			return 1;
		if (entry1->e_hash != entry2->e_hash ||
		    entry1->e_name_index != entry2->e_name_index ||
		    entry1->e_name_len != entry2->e_name_len ||
		    entry1->e_value_size != entry2->e_value_size ||
		    memcmp(entry1->e_name, entry2->e_name, entry1->e_name_len))
			return 1;
		if (entry1->e_value_block != 0 || entry2->e_value_block != 0)
			return -EIO;
		if (memcmp((char *)header1 + le16_to_cpu(entry1->e_value_offs),
			   (char *)header2 + le16_to_cpu(entry2->e_value_offs),
			   le32_to_cpu(entry1->e_value_size)))
			return 1;

		entry1 = EXT3_XATTR_NEXT(entry1);
		entry2 = EXT3_XATTR_NEXT(entry2);
	}
	if (!IS_LAST_ENTRY(entry2))
		return 1;
	return 0;
}

/*
 * ext3_xattr_cache_find()
 *
 * Find an identical extended attribute block.
 *
 * Returns a pointer to the block found, or NULL if such a block was
 * not found or an error occurred.
 */
static struct buffer_head *
ext3_xattr_cache_find(struct inode *inode, struct ext3_xattr_header *header,
		      struct mb_cache_entry **pce)
{
	__u32 hash = le32_to_cpu(header->h_hash);
	struct mb_cache_entry *ce;

	if (!header->h_hash)
		return NULL;  /* never share */
	ea_idebug(inode, "looking for cached blocks [%x]", (int)hash);
again:
	ce = mb_cache_entry_find_first(ext3_xattr_cache, inode->i_sb->s_bdev,
				       hash);
	while (ce) {
		struct buffer_head *bh;

		if (IS_ERR(ce)) {
			if (PTR_ERR(ce) == -EAGAIN)
				goto again;
			break;
		}
		bh = sb_bread(inode->i_sb, ce->e_block);
		if (!bh) {
			ext3_error(inode->i_sb, __func__,
				"inode %lu: block %lu read error",
				inode->i_ino, (unsigned long) ce->e_block);
		} else if (le32_to_cpu(BHDR(bh)->h_refcount) >=
				EXT3_XATTR_REFCOUNT_MAX) {
			ea_idebug(inode, "block %lu refcount %d>=%d",
				  (unsigned long) ce->e_block,
				  le32_to_cpu(BHDR(bh)->h_refcount),
					  EXT3_XATTR_REFCOUNT_MAX);
		} else if (ext3_xattr_cmp(header, BHDR(bh)) == 0) {
			*pce = ce;
			return bh;
		}
		brelse(bh);
		ce = mb_cache_entry_find_next(ce, inode->i_sb->s_bdev, hash);
	}
	return NULL;
}

#define NAME_HASH_SHIFT 5
#define VALUE_HASH_SHIFT 16

/*
 * ext3_xattr_hash_entry()
 *
 * Compute the hash of an extended attribute.
 */
static inline void ext3_xattr_hash_entry(struct ext3_xattr_header *header,
					 struct ext3_xattr_entry *entry)
{
	__u32 hash = 0;
	char *name = entry->e_name;
	int n;

	for (n=0; n < entry->e_name_len; n++) {
		hash = (hash << NAME_HASH_SHIFT) ^
		       (hash >> (8*sizeof(hash) - NAME_HASH_SHIFT)) ^
		       *name++;
	}

	if (entry->e_value_block == 0 && entry->e_value_size != 0) {
		__le32 *value = (__le32 *)((char *)header +
			le16_to_cpu(entry->e_value_offs));
		for (n = (le32_to_cpu(entry->e_value_size) +
		     EXT3_XATTR_ROUND) >> EXT3_XATTR_PAD_BITS; n; n--) {
			hash = (hash << VALUE_HASH_SHIFT) ^
			       (hash >> (8*sizeof(hash) - VALUE_HASH_SHIFT)) ^
			       le32_to_cpu(*value++);
		}
	}
	entry->e_hash = cpu_to_le32(hash);
}

#undef NAME_HASH_SHIFT
#undef VALUE_HASH_SHIFT

#define BLOCK_HASH_SHIFT 16

/*
 * ext3_xattr_rehash()
 *
 * Re-compute the extended attribute hash value after an entry has changed.
 */
static void ext3_xattr_rehash(struct ext3_xattr_header *header,
			      struct ext3_xattr_entry *entry)
{
	struct ext3_xattr_entry *here;
	__u32 hash = 0;

	ext3_xattr_hash_entry(header, entry);
	here = ENTRY(header+1);
	while (!IS_LAST_ENTRY(here)) {
		if (!here->e_hash) {
			/* Block is not shared if an entry's hash value == 0 */
			hash = 0;
			break;
		}
		hash = (hash << BLOCK_HASH_SHIFT) ^
		       (hash >> (8*sizeof(hash) - BLOCK_HASH_SHIFT)) ^
		       le32_to_cpu(here->e_hash);
		here = EXT3_XATTR_NEXT(here);
	}
	header->h_hash = cpu_to_le32(hash);
}

#undef BLOCK_HASH_SHIFT

int __init
init_ext3_xattr(void)
{
	ext3_xattr_cache = mb_cache_create("ext3_xattr", 6);
	if (!ext3_xattr_cache)
		return -ENOMEM;
	return 0;
}

void
exit_ext3_xattr(void)
{
	if (ext3_xattr_cache)
		mb_cache_destroy(ext3_xattr_cache);
	ext3_xattr_cache = NULL;
}
