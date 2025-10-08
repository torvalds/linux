// SPDX-License-Identifier: GPL-2.0+
/*
 * NILFS directory entry operations
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * Modified for NILFS by Amagai Yoshiji.
 */
/*
 *  linux/fs/ext2/dir.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/dir.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext2 directory handling functions
 *
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 *
 * All code that works with directory layout had been switched to pagecache
 * and moved here. AV
 */

#include <linux/pagemap.h>
#include "nilfs.h"
#include "page.h"

static inline unsigned int nilfs_rec_len_from_disk(__le16 dlen)
{
	unsigned int len = le16_to_cpu(dlen);

#if (PAGE_SIZE >= 65536)
	if (len == NILFS_MAX_REC_LEN)
		return 1 << 16;
#endif
	return len;
}

static inline __le16 nilfs_rec_len_to_disk(unsigned int len)
{
#if (PAGE_SIZE >= 65536)
	if (len == (1 << 16))
		return cpu_to_le16(NILFS_MAX_REC_LEN);

	BUG_ON(len > (1 << 16));
#endif
	return cpu_to_le16(len);
}

/*
 * nilfs uses block-sized chunks. Arguably, sector-sized ones would be
 * more robust, but we have what we have
 */
static inline unsigned int nilfs_chunk_size(struct inode *inode)
{
	return inode->i_sb->s_blocksize;
}

/*
 * Return the offset into page `page_nr' of the last valid
 * byte in that page, plus one.
 */
static unsigned int nilfs_last_byte(struct inode *inode, unsigned long page_nr)
{
	u64 last_byte = inode->i_size;

	last_byte -= page_nr << PAGE_SHIFT;
	if (last_byte > PAGE_SIZE)
		last_byte = PAGE_SIZE;
	return last_byte;
}

static int nilfs_prepare_chunk(struct folio *folio, unsigned int from,
			       unsigned int to)
{
	loff_t pos = folio_pos(folio) + from;

	return __block_write_begin(folio, pos, to - from, nilfs_get_block);
}

static void nilfs_commit_chunk(struct folio *folio,
		struct address_space *mapping, size_t from, size_t to)
{
	struct inode *dir = mapping->host;
	loff_t pos = folio_pos(folio) + from;
	size_t copied, len = to - from;
	unsigned int nr_dirty;
	int err;

	nr_dirty = nilfs_page_count_clean_buffers(folio, from, to);
	copied = block_write_end(pos, len, len, folio);
	if (pos + copied > dir->i_size)
		i_size_write(dir, pos + copied);
	if (IS_DIRSYNC(dir))
		nilfs_set_transaction_flag(NILFS_TI_SYNC);
	err = nilfs_set_file_dirty(dir, nr_dirty);
	WARN_ON(err); /* do not happen */
	folio_unlock(folio);
}

static bool nilfs_check_folio(struct folio *folio, char *kaddr)
{
	struct inode *dir = folio->mapping->host;
	struct super_block *sb = dir->i_sb;
	unsigned int chunk_size = nilfs_chunk_size(dir);
	size_t offs, rec_len;
	size_t limit = folio_size(folio);
	struct nilfs_dir_entry *p;
	char *error;

	if (dir->i_size < folio_pos(folio) + limit) {
		limit = dir->i_size - folio_pos(folio);
		if (limit & (chunk_size - 1))
			goto Ebadsize;
		if (!limit)
			goto out;
	}
	for (offs = 0; offs <= limit - NILFS_DIR_REC_LEN(1); offs += rec_len) {
		p = (struct nilfs_dir_entry *)(kaddr + offs);
		rec_len = nilfs_rec_len_from_disk(p->rec_len);

		if (rec_len < NILFS_DIR_REC_LEN(1))
			goto Eshort;
		if (rec_len & 3)
			goto Ealign;
		if (rec_len < NILFS_DIR_REC_LEN(p->name_len))
			goto Enamelen;
		if (((offs + rec_len - 1) ^ offs) & ~(chunk_size-1))
			goto Espan;
		if (unlikely(p->inode &&
			     NILFS_PRIVATE_INODE(le64_to_cpu(p->inode))))
			goto Einumber;
	}
	if (offs != limit)
		goto Eend;
out:
	folio_set_checked(folio);
	return true;

	/* Too bad, we had an error */

Ebadsize:
	nilfs_error(sb,
		    "size of directory #%lu is not a multiple of chunk size",
		    dir->i_ino);
	goto fail;
Eshort:
	error = "rec_len is smaller than minimal";
	goto bad_entry;
Ealign:
	error = "unaligned directory entry";
	goto bad_entry;
Enamelen:
	error = "rec_len is too small for name_len";
	goto bad_entry;
Espan:
	error = "directory entry across blocks";
	goto bad_entry;
Einumber:
	error = "disallowed inode number";
bad_entry:
	nilfs_error(sb,
		    "bad entry in directory #%lu: %s - offset=%lu, inode=%lu, rec_len=%zd, name_len=%d",
		    dir->i_ino, error, (folio->index << PAGE_SHIFT) + offs,
		    (unsigned long)le64_to_cpu(p->inode),
		    rec_len, p->name_len);
	goto fail;
Eend:
	p = (struct nilfs_dir_entry *)(kaddr + offs);
	nilfs_error(sb,
		    "entry in directory #%lu spans the page boundary offset=%lu, inode=%lu",
		    dir->i_ino, (folio->index << PAGE_SHIFT) + offs,
		    (unsigned long)le64_to_cpu(p->inode));
fail:
	return false;
}

static void *nilfs_get_folio(struct inode *dir, unsigned long n,
		struct folio **foliop)
{
	struct address_space *mapping = dir->i_mapping;
	struct folio *folio = read_mapping_folio(mapping, n, NULL);
	void *kaddr;

	if (IS_ERR(folio))
		return folio;

	kaddr = kmap_local_folio(folio, 0);
	if (unlikely(!folio_test_checked(folio))) {
		if (!nilfs_check_folio(folio, kaddr))
			goto fail;
	}

	*foliop = folio;
	return kaddr;

fail:
	folio_release_kmap(folio, kaddr);
	return ERR_PTR(-EIO);
}

/*
 * NOTE! unlike strncmp, nilfs_match returns 1 for success, 0 for failure.
 *
 * len <= NILFS_NAME_LEN and de != NULL are guaranteed by caller.
 */
static int
nilfs_match(int len, const unsigned char *name, struct nilfs_dir_entry *de)
{
	if (len != de->name_len)
		return 0;
	if (!de->inode)
		return 0;
	return !memcmp(name, de->name, len);
}

/*
 * p is at least 6 bytes before the end of page
 */
static struct nilfs_dir_entry *nilfs_next_entry(struct nilfs_dir_entry *p)
{
	return (struct nilfs_dir_entry *)((char *)p +
					  nilfs_rec_len_from_disk(p->rec_len));
}

static int nilfs_readdir(struct file *file, struct dir_context *ctx)
{
	loff_t pos = ctx->pos;
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	unsigned int offset = pos & ~PAGE_MASK;
	unsigned long n = pos >> PAGE_SHIFT;
	unsigned long npages = dir_pages(inode);

	if (pos > inode->i_size - NILFS_DIR_REC_LEN(1))
		return 0;

	for ( ; n < npages; n++, offset = 0) {
		char *kaddr, *limit;
		struct nilfs_dir_entry *de;
		struct folio *folio;

		kaddr = nilfs_get_folio(inode, n, &folio);
		if (IS_ERR(kaddr)) {
			nilfs_error(sb, "bad page in #%lu", inode->i_ino);
			ctx->pos += PAGE_SIZE - offset;
			return -EIO;
		}
		de = (struct nilfs_dir_entry *)(kaddr + offset);
		limit = kaddr + nilfs_last_byte(inode, n) -
			NILFS_DIR_REC_LEN(1);
		for ( ; (char *)de <= limit; de = nilfs_next_entry(de)) {
			if (de->rec_len == 0) {
				nilfs_error(sb, "zero-length directory entry");
				folio_release_kmap(folio, kaddr);
				return -EIO;
			}
			if (de->inode) {
				unsigned char t;

				t = fs_ftype_to_dtype(de->file_type);

				if (!dir_emit(ctx, de->name, de->name_len,
						le64_to_cpu(de->inode), t)) {
					folio_release_kmap(folio, kaddr);
					return 0;
				}
			}
			ctx->pos += nilfs_rec_len_from_disk(de->rec_len);
		}
		folio_release_kmap(folio, kaddr);
	}
	return 0;
}

/*
 * nilfs_find_entry()
 *
 * Finds an entry in the specified directory with the wanted name. It
 * returns the folio in which the entry was found, and the entry itself.
 * The folio is mapped and unlocked.  When the caller is finished with
 * the entry, it should call folio_release_kmap().
 *
 * On failure, returns an error pointer and the caller should ignore foliop.
 */
struct nilfs_dir_entry *nilfs_find_entry(struct inode *dir,
		const struct qstr *qstr, struct folio **foliop)
{
	const unsigned char *name = qstr->name;
	int namelen = qstr->len;
	unsigned int reclen = NILFS_DIR_REC_LEN(namelen);
	unsigned long start, n;
	unsigned long npages = dir_pages(dir);
	struct nilfs_inode_info *ei = NILFS_I(dir);
	struct nilfs_dir_entry *de;

	if (npages == 0)
		goto out;

	start = ei->i_dir_start_lookup;
	if (start >= npages)
		start = 0;
	n = start;
	do {
		char *kaddr = nilfs_get_folio(dir, n, foliop);

		if (IS_ERR(kaddr))
			return ERR_CAST(kaddr);

		de = (struct nilfs_dir_entry *)kaddr;
		kaddr += nilfs_last_byte(dir, n) - reclen;
		while ((char *)de <= kaddr) {
			if (de->rec_len == 0) {
				nilfs_error(dir->i_sb,
					    "zero-length directory entry");
				folio_release_kmap(*foliop, kaddr);
				goto out;
			}
			if (nilfs_match(namelen, name, de))
				goto found;
			de = nilfs_next_entry(de);
		}
		folio_release_kmap(*foliop, kaddr);

		if (++n >= npages)
			n = 0;
		/* next folio is past the blocks we've got */
		if (unlikely(n > (dir->i_blocks >> (PAGE_SHIFT - 9)))) {
			nilfs_error(dir->i_sb,
			       "dir %lu size %lld exceeds block count %llu",
			       dir->i_ino, dir->i_size,
			       (unsigned long long)dir->i_blocks);
			goto out;
		}
	} while (n != start);
out:
	return ERR_PTR(-ENOENT);

found:
	ei->i_dir_start_lookup = n;
	return de;
}

struct nilfs_dir_entry *nilfs_dotdot(struct inode *dir, struct folio **foliop)
{
	struct folio *folio;
	struct nilfs_dir_entry *de, *next_de;
	size_t limit;
	char *msg;

	de = nilfs_get_folio(dir, 0, &folio);
	if (IS_ERR(de))
		return NULL;

	limit = nilfs_last_byte(dir, 0);  /* is a multiple of chunk size */
	if (unlikely(!limit || le64_to_cpu(de->inode) != dir->i_ino ||
		     !nilfs_match(1, ".", de))) {
		msg = "missing '.'";
		goto fail;
	}

	next_de = nilfs_next_entry(de);
	/*
	 * If "next_de" has not reached the end of the chunk, there is
	 * at least one more record.  Check whether it matches "..".
	 */
	if (unlikely((char *)next_de == (char *)de + nilfs_chunk_size(dir) ||
		     !nilfs_match(2, "..", next_de))) {
		msg = "missing '..'";
		goto fail;
	}
	*foliop = folio;
	return next_de;

fail:
	nilfs_error(dir->i_sb, "directory #%lu %s", dir->i_ino, msg);
	folio_release_kmap(folio, de);
	return NULL;
}

int nilfs_inode_by_name(struct inode *dir, const struct qstr *qstr, ino_t *ino)
{
	struct nilfs_dir_entry *de;
	struct folio *folio;

	de = nilfs_find_entry(dir, qstr, &folio);
	if (IS_ERR(de))
		return PTR_ERR(de);

	*ino = le64_to_cpu(de->inode);
	folio_release_kmap(folio, de);
	return 0;
}

int nilfs_set_link(struct inode *dir, struct nilfs_dir_entry *de,
		    struct folio *folio, struct inode *inode)
{
	size_t from = offset_in_folio(folio, de);
	size_t to = from + nilfs_rec_len_from_disk(de->rec_len);
	struct address_space *mapping = folio->mapping;
	int err;

	folio_lock(folio);
	err = nilfs_prepare_chunk(folio, from, to);
	if (unlikely(err)) {
		folio_unlock(folio);
		return err;
	}
	de->inode = cpu_to_le64(inode->i_ino);
	de->file_type = fs_umode_to_ftype(inode->i_mode);
	nilfs_commit_chunk(folio, mapping, from, to);
	inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
	return 0;
}

/*
 *	Parent is locked.
 */
int nilfs_add_link(struct dentry *dentry, struct inode *inode)
{
	struct inode *dir = d_inode(dentry->d_parent);
	const unsigned char *name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	unsigned int chunk_size = nilfs_chunk_size(dir);
	unsigned int reclen = NILFS_DIR_REC_LEN(namelen);
	unsigned short rec_len, name_len;
	struct folio *folio = NULL;
	struct nilfs_dir_entry *de;
	unsigned long npages = dir_pages(dir);
	unsigned long n;
	size_t from, to;
	int err;

	/*
	 * We take care of directory expansion in the same loop.
	 * This code plays outside i_size, so it locks the folio
	 * to protect that region.
	 */
	for (n = 0; n <= npages; n++) {
		char *kaddr = nilfs_get_folio(dir, n, &folio);
		char *dir_end;

		if (IS_ERR(kaddr))
			return PTR_ERR(kaddr);
		folio_lock(folio);
		dir_end = kaddr + nilfs_last_byte(dir, n);
		de = (struct nilfs_dir_entry *)kaddr;
		kaddr += folio_size(folio) - reclen;
		while ((char *)de <= kaddr) {
			if ((char *)de == dir_end) {
				/* We hit i_size */
				name_len = 0;
				rec_len = chunk_size;
				de->rec_len = nilfs_rec_len_to_disk(chunk_size);
				de->inode = 0;
				goto got_it;
			}
			if (de->rec_len == 0) {
				nilfs_error(dir->i_sb,
					    "zero-length directory entry");
				err = -EIO;
				goto out_unlock;
			}
			err = -EEXIST;
			if (nilfs_match(namelen, name, de))
				goto out_unlock;
			name_len = NILFS_DIR_REC_LEN(de->name_len);
			rec_len = nilfs_rec_len_from_disk(de->rec_len);
			if (!de->inode && rec_len >= reclen)
				goto got_it;
			if (rec_len >= name_len + reclen)
				goto got_it;
			de = (struct nilfs_dir_entry *)((char *)de + rec_len);
		}
		folio_unlock(folio);
		folio_release_kmap(folio, kaddr);
	}
	BUG();
	return -EINVAL;

got_it:
	from = offset_in_folio(folio, de);
	to = from + rec_len;
	err = nilfs_prepare_chunk(folio, from, to);
	if (err)
		goto out_unlock;
	if (de->inode) {
		struct nilfs_dir_entry *de1;

		de1 = (struct nilfs_dir_entry *)((char *)de + name_len);
		de1->rec_len = nilfs_rec_len_to_disk(rec_len - name_len);
		de->rec_len = nilfs_rec_len_to_disk(name_len);
		de = de1;
	}
	de->name_len = namelen;
	memcpy(de->name, name, namelen);
	de->inode = cpu_to_le64(inode->i_ino);
	de->file_type = fs_umode_to_ftype(inode->i_mode);
	nilfs_commit_chunk(folio, folio->mapping, from, to);
	inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
	nilfs_mark_inode_dirty(dir);
	/* OFFSET_CACHE */
out_put:
	folio_release_kmap(folio, de);
	return err;
out_unlock:
	folio_unlock(folio);
	goto out_put;
}

/*
 * nilfs_delete_entry deletes a directory entry by merging it with the
 * previous entry. Folio is up-to-date.
 */
int nilfs_delete_entry(struct nilfs_dir_entry *dir, struct folio *folio)
{
	struct address_space *mapping = folio->mapping;
	struct inode *inode = mapping->host;
	char *kaddr = (char *)((unsigned long)dir & ~(folio_size(folio) - 1));
	size_t from, to;
	struct nilfs_dir_entry *de, *pde = NULL;
	int err;

	from = ((char *)dir - kaddr) & ~(nilfs_chunk_size(inode) - 1);
	to = ((char *)dir - kaddr) + nilfs_rec_len_from_disk(dir->rec_len);
	de = (struct nilfs_dir_entry *)(kaddr + from);

	while ((char *)de < (char *)dir) {
		if (de->rec_len == 0) {
			nilfs_error(inode->i_sb,
				    "zero-length directory entry");
			err = -EIO;
			goto out;
		}
		pde = de;
		de = nilfs_next_entry(de);
	}
	if (pde)
		from = (char *)pde - kaddr;
	folio_lock(folio);
	err = nilfs_prepare_chunk(folio, from, to);
	if (unlikely(err)) {
		folio_unlock(folio);
		goto out;
	}
	if (pde)
		pde->rec_len = nilfs_rec_len_to_disk(to - from);
	dir->inode = 0;
	nilfs_commit_chunk(folio, mapping, from, to);
	inode_set_mtime_to_ts(inode, inode_set_ctime_current(inode));
out:
	return err;
}

/*
 * Set the first fragment of directory.
 */
int nilfs_make_empty(struct inode *inode, struct inode *parent)
{
	struct address_space *mapping = inode->i_mapping;
	struct folio *folio = filemap_grab_folio(mapping, 0);
	unsigned int chunk_size = nilfs_chunk_size(inode);
	struct nilfs_dir_entry *de;
	int err;
	void *kaddr;

	if (IS_ERR(folio))
		return PTR_ERR(folio);

	err = nilfs_prepare_chunk(folio, 0, chunk_size);
	if (unlikely(err)) {
		folio_unlock(folio);
		goto fail;
	}
	kaddr = kmap_local_folio(folio, 0);
	memset(kaddr, 0, chunk_size);
	de = (struct nilfs_dir_entry *)kaddr;
	de->name_len = 1;
	de->rec_len = nilfs_rec_len_to_disk(NILFS_DIR_REC_LEN(1));
	memcpy(de->name, ".\0\0", 4);
	de->inode = cpu_to_le64(inode->i_ino);
	de->file_type = fs_umode_to_ftype(inode->i_mode);

	de = (struct nilfs_dir_entry *)(kaddr + NILFS_DIR_REC_LEN(1));
	de->name_len = 2;
	de->rec_len = nilfs_rec_len_to_disk(chunk_size - NILFS_DIR_REC_LEN(1));
	de->inode = cpu_to_le64(parent->i_ino);
	memcpy(de->name, "..\0", 4);
	de->file_type = fs_umode_to_ftype(inode->i_mode);
	kunmap_local(kaddr);
	nilfs_commit_chunk(folio, mapping, 0, chunk_size);
fail:
	folio_put(folio);
	return err;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
int nilfs_empty_dir(struct inode *inode)
{
	struct folio *folio = NULL;
	char *kaddr;
	unsigned long i, npages = dir_pages(inode);

	for (i = 0; i < npages; i++) {
		struct nilfs_dir_entry *de;

		kaddr = nilfs_get_folio(inode, i, &folio);
		if (IS_ERR(kaddr))
			return 0;

		de = (struct nilfs_dir_entry *)kaddr;
		kaddr += nilfs_last_byte(inode, i) - NILFS_DIR_REC_LEN(1);

		while ((char *)de <= kaddr) {
			if (de->rec_len == 0) {
				nilfs_error(inode->i_sb,
					    "zero-length directory entry (kaddr=%p, de=%p)",
					    kaddr, de);
				goto not_empty;
			}
			if (de->inode != 0) {
				/* check for . and .. */
				if (de->name[0] != '.')
					goto not_empty;
				if (de->name_len > 2)
					goto not_empty;
				if (de->name_len < 2) {
					if (de->inode !=
					    cpu_to_le64(inode->i_ino))
						goto not_empty;
				} else if (de->name[1] != '.')
					goto not_empty;
			}
			de = nilfs_next_entry(de);
		}
		folio_release_kmap(folio, kaddr);
	}
	return 1;

not_empty:
	folio_release_kmap(folio, kaddr);
	return 0;
}

const struct file_operations nilfs_dir_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= nilfs_readdir,
	.unlocked_ioctl	= nilfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= nilfs_compat_ioctl,
#endif	/* CONFIG_COMPAT */
	.fsync		= nilfs_sync_file,

};
