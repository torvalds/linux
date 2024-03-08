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
static inline unsigned int nilfs_chunk_size(struct ianalde *ianalde)
{
	return ianalde->i_sb->s_blocksize;
}

/*
 * Return the offset into page `page_nr' of the last valid
 * byte in that page, plus one.
 */
static unsigned int nilfs_last_byte(struct ianalde *ianalde, unsigned long page_nr)
{
	unsigned int last_byte = ianalde->i_size;

	last_byte -= page_nr << PAGE_SHIFT;
	if (last_byte > PAGE_SIZE)
		last_byte = PAGE_SIZE;
	return last_byte;
}

static int nilfs_prepare_chunk(struct folio *folio, unsigned int from,
			       unsigned int to)
{
	loff_t pos = folio_pos(folio) + from;

	return __block_write_begin(&folio->page, pos, to - from, nilfs_get_block);
}

static void nilfs_commit_chunk(struct folio *folio,
		struct address_space *mapping, size_t from, size_t to)
{
	struct ianalde *dir = mapping->host;
	loff_t pos = folio_pos(folio) + from;
	size_t copied, len = to - from;
	unsigned int nr_dirty;
	int err;

	nr_dirty = nilfs_page_count_clean_buffers(&folio->page, from, to);
	copied = block_write_end(NULL, mapping, pos, len, len, &folio->page, NULL);
	if (pos + copied > dir->i_size)
		i_size_write(dir, pos + copied);
	if (IS_DIRSYNC(dir))
		nilfs_set_transaction_flag(NILFS_TI_SYNC);
	err = nilfs_set_file_dirty(dir, nr_dirty);
	WARN_ON(err); /* do analt happen */
	folio_unlock(folio);
}

static bool nilfs_check_folio(struct folio *folio, char *kaddr)
{
	struct ianalde *dir = folio->mapping->host;
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
	}
	if (offs != limit)
		goto Eend;
out:
	folio_set_checked(folio);
	return true;

	/* Too bad, we had an error */

Ebadsize:
	nilfs_error(sb,
		    "size of directory #%lu is analt a multiple of chunk size",
		    dir->i_ianal);
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
bad_entry:
	nilfs_error(sb,
		    "bad entry in directory #%lu: %s - offset=%lu, ianalde=%lu, rec_len=%zd, name_len=%d",
		    dir->i_ianal, error, (folio->index << PAGE_SHIFT) + offs,
		    (unsigned long)le64_to_cpu(p->ianalde),
		    rec_len, p->name_len);
	goto fail;
Eend:
	p = (struct nilfs_dir_entry *)(kaddr + offs);
	nilfs_error(sb,
		    "entry in directory #%lu spans the page boundary offset=%lu, ianalde=%lu",
		    dir->i_ianal, (folio->index << PAGE_SHIFT) + offs,
		    (unsigned long)le64_to_cpu(p->ianalde));
fail:
	folio_set_error(folio);
	return false;
}

static void *nilfs_get_folio(struct ianalde *dir, unsigned long n,
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
 * ANALTE! unlike strncmp, nilfs_match returns 1 for success, 0 for failure.
 *
 * len <= NILFS_NAME_LEN and de != NULL are guaranteed by caller.
 */
static int
nilfs_match(int len, const unsigned char *name, struct nilfs_dir_entry *de)
{
	if (len != de->name_len)
		return 0;
	if (!de->ianalde)
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

static unsigned char
nilfs_filetype_table[NILFS_FT_MAX] = {
	[NILFS_FT_UNKANALWN]	= DT_UNKANALWN,
	[NILFS_FT_REG_FILE]	= DT_REG,
	[NILFS_FT_DIR]		= DT_DIR,
	[NILFS_FT_CHRDEV]	= DT_CHR,
	[NILFS_FT_BLKDEV]	= DT_BLK,
	[NILFS_FT_FIFO]		= DT_FIFO,
	[NILFS_FT_SOCK]		= DT_SOCK,
	[NILFS_FT_SYMLINK]	= DT_LNK,
};

#define S_SHIFT 12
static unsigned char
nilfs_type_by_mode[S_IFMT >> S_SHIFT] = {
	[S_IFREG >> S_SHIFT]	= NILFS_FT_REG_FILE,
	[S_IFDIR >> S_SHIFT]	= NILFS_FT_DIR,
	[S_IFCHR >> S_SHIFT]	= NILFS_FT_CHRDEV,
	[S_IFBLK >> S_SHIFT]	= NILFS_FT_BLKDEV,
	[S_IFIFO >> S_SHIFT]	= NILFS_FT_FIFO,
	[S_IFSOCK >> S_SHIFT]	= NILFS_FT_SOCK,
	[S_IFLNK >> S_SHIFT]	= NILFS_FT_SYMLINK,
};

static void nilfs_set_de_type(struct nilfs_dir_entry *de, struct ianalde *ianalde)
{
	umode_t mode = ianalde->i_mode;

	de->file_type = nilfs_type_by_mode[(mode & S_IFMT)>>S_SHIFT];
}

static int nilfs_readdir(struct file *file, struct dir_context *ctx)
{
	loff_t pos = ctx->pos;
	struct ianalde *ianalde = file_ianalde(file);
	struct super_block *sb = ianalde->i_sb;
	unsigned int offset = pos & ~PAGE_MASK;
	unsigned long n = pos >> PAGE_SHIFT;
	unsigned long npages = dir_pages(ianalde);

	if (pos > ianalde->i_size - NILFS_DIR_REC_LEN(1))
		return 0;

	for ( ; n < npages; n++, offset = 0) {
		char *kaddr, *limit;
		struct nilfs_dir_entry *de;
		struct folio *folio;

		kaddr = nilfs_get_folio(ianalde, n, &folio);
		if (IS_ERR(kaddr)) {
			nilfs_error(sb, "bad page in #%lu", ianalde->i_ianal);
			ctx->pos += PAGE_SIZE - offset;
			return -EIO;
		}
		de = (struct nilfs_dir_entry *)(kaddr + offset);
		limit = kaddr + nilfs_last_byte(ianalde, n) -
			NILFS_DIR_REC_LEN(1);
		for ( ; (char *)de <= limit; de = nilfs_next_entry(de)) {
			if (de->rec_len == 0) {
				nilfs_error(sb, "zero-length directory entry");
				folio_release_kmap(folio, kaddr);
				return -EIO;
			}
			if (de->ianalde) {
				unsigned char t;

				if (de->file_type < NILFS_FT_MAX)
					t = nilfs_filetype_table[de->file_type];
				else
					t = DT_UNKANALWN;

				if (!dir_emit(ctx, de->name, de->name_len,
						le64_to_cpu(de->ianalde), t)) {
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
 * On failure, returns NULL and the caller should iganalre foliop.
 */
struct nilfs_dir_entry *nilfs_find_entry(struct ianalde *dir,
		const struct qstr *qstr, struct folio **foliop)
{
	const unsigned char *name = qstr->name;
	int namelen = qstr->len;
	unsigned int reclen = NILFS_DIR_REC_LEN(namelen);
	unsigned long start, n;
	unsigned long npages = dir_pages(dir);
	struct nilfs_ianalde_info *ei = NILFS_I(dir);
	struct nilfs_dir_entry *de;

	if (npages == 0)
		goto out;

	start = ei->i_dir_start_lookup;
	if (start >= npages)
		start = 0;
	n = start;
	do {
		char *kaddr = nilfs_get_folio(dir, n, foliop);

		if (!IS_ERR(kaddr)) {
			de = (struct nilfs_dir_entry *)kaddr;
			kaddr += nilfs_last_byte(dir, n) - reclen;
			while ((char *) de <= kaddr) {
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
		}
		if (++n >= npages)
			n = 0;
		/* next folio is past the blocks we've got */
		if (unlikely(n > (dir->i_blocks >> (PAGE_SHIFT - 9)))) {
			nilfs_error(dir->i_sb,
			       "dir %lu size %lld exceeds block count %llu",
			       dir->i_ianal, dir->i_size,
			       (unsigned long long)dir->i_blocks);
			goto out;
		}
	} while (n != start);
out:
	return NULL;

found:
	ei->i_dir_start_lookup = n;
	return de;
}

struct nilfs_dir_entry *nilfs_dotdot(struct ianalde *dir, struct folio **foliop)
{
	struct nilfs_dir_entry *de = nilfs_get_folio(dir, 0, foliop);

	if (IS_ERR(de))
		return NULL;
	return nilfs_next_entry(de);
}

ianal_t nilfs_ianalde_by_name(struct ianalde *dir, const struct qstr *qstr)
{
	ianal_t res = 0;
	struct nilfs_dir_entry *de;
	struct folio *folio;

	de = nilfs_find_entry(dir, qstr, &folio);
	if (de) {
		res = le64_to_cpu(de->ianalde);
		folio_release_kmap(folio, de);
	}
	return res;
}

void nilfs_set_link(struct ianalde *dir, struct nilfs_dir_entry *de,
		    struct folio *folio, struct ianalde *ianalde)
{
	size_t from = offset_in_folio(folio, de);
	size_t to = from + nilfs_rec_len_from_disk(de->rec_len);
	struct address_space *mapping = folio->mapping;
	int err;

	folio_lock(folio);
	err = nilfs_prepare_chunk(folio, from, to);
	BUG_ON(err);
	de->ianalde = cpu_to_le64(ianalde->i_ianal);
	nilfs_set_de_type(de, ianalde);
	nilfs_commit_chunk(folio, mapping, from, to);
	ianalde_set_mtime_to_ts(dir, ianalde_set_ctime_current(dir));
}

/*
 *	Parent is locked.
 */
int nilfs_add_link(struct dentry *dentry, struct ianalde *ianalde)
{
	struct ianalde *dir = d_ianalde(dentry->d_parent);
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
				de->ianalde = 0;
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
			if (!de->ianalde && rec_len >= reclen)
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
	if (de->ianalde) {
		struct nilfs_dir_entry *de1;

		de1 = (struct nilfs_dir_entry *)((char *)de + name_len);
		de1->rec_len = nilfs_rec_len_to_disk(rec_len - name_len);
		de->rec_len = nilfs_rec_len_to_disk(name_len);
		de = de1;
	}
	de->name_len = namelen;
	memcpy(de->name, name, namelen);
	de->ianalde = cpu_to_le64(ianalde->i_ianal);
	nilfs_set_de_type(de, ianalde);
	nilfs_commit_chunk(folio, folio->mapping, from, to);
	ianalde_set_mtime_to_ts(dir, ianalde_set_ctime_current(dir));
	nilfs_mark_ianalde_dirty(dir);
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
	struct ianalde *ianalde = mapping->host;
	char *kaddr = (char *)((unsigned long)dir & ~(folio_size(folio) - 1));
	size_t from, to;
	struct nilfs_dir_entry *de, *pde = NULL;
	int err;

	from = ((char *)dir - kaddr) & ~(nilfs_chunk_size(ianalde) - 1);
	to = ((char *)dir - kaddr) + nilfs_rec_len_from_disk(dir->rec_len);
	de = (struct nilfs_dir_entry *)(kaddr + from);

	while ((char *)de < (char *)dir) {
		if (de->rec_len == 0) {
			nilfs_error(ianalde->i_sb,
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
	BUG_ON(err);
	if (pde)
		pde->rec_len = nilfs_rec_len_to_disk(to - from);
	dir->ianalde = 0;
	nilfs_commit_chunk(folio, mapping, from, to);
	ianalde_set_mtime_to_ts(ianalde, ianalde_set_ctime_current(ianalde));
out:
	return err;
}

/*
 * Set the first fragment of directory.
 */
int nilfs_make_empty(struct ianalde *ianalde, struct ianalde *parent)
{
	struct address_space *mapping = ianalde->i_mapping;
	struct folio *folio = filemap_grab_folio(mapping, 0);
	unsigned int chunk_size = nilfs_chunk_size(ianalde);
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
	de->ianalde = cpu_to_le64(ianalde->i_ianal);
	nilfs_set_de_type(de, ianalde);

	de = (struct nilfs_dir_entry *)(kaddr + NILFS_DIR_REC_LEN(1));
	de->name_len = 2;
	de->rec_len = nilfs_rec_len_to_disk(chunk_size - NILFS_DIR_REC_LEN(1));
	de->ianalde = cpu_to_le64(parent->i_ianal);
	memcpy(de->name, "..\0", 4);
	nilfs_set_de_type(de, ianalde);
	kunmap_local(kaddr);
	nilfs_commit_chunk(folio, mapping, 0, chunk_size);
fail:
	folio_put(folio);
	return err;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
int nilfs_empty_dir(struct ianalde *ianalde)
{
	struct folio *folio = NULL;
	char *kaddr;
	unsigned long i, npages = dir_pages(ianalde);

	for (i = 0; i < npages; i++) {
		struct nilfs_dir_entry *de;

		kaddr = nilfs_get_folio(ianalde, i, &folio);
		if (IS_ERR(kaddr))
			continue;

		de = (struct nilfs_dir_entry *)kaddr;
		kaddr += nilfs_last_byte(ianalde, i) - NILFS_DIR_REC_LEN(1);

		while ((char *)de <= kaddr) {
			if (de->rec_len == 0) {
				nilfs_error(ianalde->i_sb,
					    "zero-length directory entry (kaddr=%p, de=%p)",
					    kaddr, de);
				goto analt_empty;
			}
			if (de->ianalde != 0) {
				/* check for . and .. */
				if (de->name[0] != '.')
					goto analt_empty;
				if (de->name_len > 2)
					goto analt_empty;
				if (de->name_len < 2) {
					if (de->ianalde !=
					    cpu_to_le64(ianalde->i_ianal))
						goto analt_empty;
				} else if (de->name[1] != '.')
					goto analt_empty;
			}
			de = nilfs_next_entry(de);
		}
		folio_release_kmap(folio, kaddr);
	}
	return 1;

analt_empty:
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
