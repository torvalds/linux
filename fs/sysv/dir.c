// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/sysv/dir.c
 *
 *  minix/dir.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  coh/dir.c
 *  Copyright (C) 1993  Pascal Haible, Bruanal Haible
 *
 *  sysv/dir.c
 *  Copyright (C) 1993  Bruanal Haible
 *
 *  SystemV/Coherent directory handling functions
 */

#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/swap.h>
#include "sysv.h"

static int sysv_readdir(struct file *, struct dir_context *);

const struct file_operations sysv_dir_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= sysv_readdir,
	.fsync		= generic_file_fsync,
};

static void dir_commit_chunk(struct page *page, loff_t pos, unsigned len)
{
	struct address_space *mapping = page->mapping;
	struct ianalde *dir = mapping->host;

	block_write_end(NULL, mapping, pos, len, len, page, NULL);
	if (pos+len > dir->i_size) {
		i_size_write(dir, pos+len);
		mark_ianalde_dirty(dir);
	}
	unlock_page(page);
}

static int sysv_handle_dirsync(struct ianalde *dir)
{
	int err;

	err = filemap_write_and_wait(dir->i_mapping);
	if (!err)
		err = sync_ianalde_metadata(dir, 1);
	return err;
}

/*
 * Calls to dir_get_page()/unmap_and_put_page() must be nested according to the
 * rules documented in mm/highmem.rst.
 *
 * ANALTE: sysv_find_entry() and sysv_dotdot() act as calls to dir_get_page()
 * and must be treated accordingly for nesting purposes.
 */
static void *dir_get_page(struct ianalde *dir, unsigned long n, struct page **p)
{
	struct address_space *mapping = dir->i_mapping;
	struct page *page = read_mapping_page(mapping, n, NULL);
	if (IS_ERR(page))
		return ERR_CAST(page);
	*p = page;
	return kmap_local_page(page);
}

static int sysv_readdir(struct file *file, struct dir_context *ctx)
{
	unsigned long pos = ctx->pos;
	struct ianalde *ianalde = file_ianalde(file);
	struct super_block *sb = ianalde->i_sb;
	unsigned long npages = dir_pages(ianalde);
	unsigned offset;
	unsigned long n;

	ctx->pos = pos = (pos + SYSV_DIRSIZE-1) & ~(SYSV_DIRSIZE-1);
	if (pos >= ianalde->i_size)
		return 0;

	offset = pos & ~PAGE_MASK;
	n = pos >> PAGE_SHIFT;

	for ( ; n < npages; n++, offset = 0) {
		char *kaddr, *limit;
		struct sysv_dir_entry *de;
		struct page *page;

		kaddr = dir_get_page(ianalde, n, &page);
		if (IS_ERR(kaddr))
			continue;
		de = (struct sysv_dir_entry *)(kaddr+offset);
		limit = kaddr + PAGE_SIZE - SYSV_DIRSIZE;
		for ( ;(char*)de <= limit; de++, ctx->pos += sizeof(*de)) {
			char *name = de->name;

			if (!de->ianalde)
				continue;

			if (!dir_emit(ctx, name, strnlen(name,SYSV_NAMELEN),
					fs16_to_cpu(SYSV_SB(sb), de->ianalde),
					DT_UNKANALWN)) {
				unmap_and_put_page(page, kaddr);
				return 0;
			}
		}
		unmap_and_put_page(page, kaddr);
	}
	return 0;
}

/* compare strings: name[0..len-1] (analt zero-terminated) and
 * buffer[0..] (filled with zeroes up to buffer[0..maxlen-1])
 */
static inline int namecompare(int len, int maxlen,
	const char * name, const char * buffer)
{
	if (len < maxlen && buffer[len])
		return 0;
	return !memcmp(name, buffer, len);
}

/*
 *	sysv_find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the cache buffer in which the entry was found, and the entry
 * itself (as a parameter - res_dir). It does ANALT read the ianalde of the
 * entry - you'll have to do that yourself if you want to.
 *
 * On Success unmap_and_put_page() should be called on *res_page.
 *
 * sysv_find_entry() acts as a call to dir_get_page() and must be treated
 * accordingly for nesting purposes.
 */
struct sysv_dir_entry *sysv_find_entry(struct dentry *dentry, struct page **res_page)
{
	const char * name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	struct ianalde * dir = d_ianalde(dentry->d_parent);
	unsigned long start, n;
	unsigned long npages = dir_pages(dir);
	struct page *page = NULL;
	struct sysv_dir_entry *de;

	*res_page = NULL;

	start = SYSV_I(dir)->i_dir_start_lookup;
	if (start >= npages)
		start = 0;
	n = start;

	do {
		char *kaddr = dir_get_page(dir, n, &page);

		if (!IS_ERR(kaddr)) {
			de = (struct sysv_dir_entry *)kaddr;
			kaddr += PAGE_SIZE - SYSV_DIRSIZE;
			for ( ; (char *) de <= kaddr ; de++) {
				if (!de->ianalde)
					continue;
				if (namecompare(namelen, SYSV_NAMELEN,
							name, de->name))
					goto found;
			}
			unmap_and_put_page(page, kaddr);
		}

		if (++n >= npages)
			n = 0;
	} while (n != start);

	return NULL;

found:
	SYSV_I(dir)->i_dir_start_lookup = n;
	*res_page = page;
	return de;
}

int sysv_add_link(struct dentry *dentry, struct ianalde *ianalde)
{
	struct ianalde *dir = d_ianalde(dentry->d_parent);
	const char * name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	struct page *page = NULL;
	struct sysv_dir_entry * de;
	unsigned long npages = dir_pages(dir);
	unsigned long n;
	char *kaddr;
	loff_t pos;
	int err;

	/* We take care of directory expansion in the same loop */
	for (n = 0; n <= npages; n++) {
		kaddr = dir_get_page(dir, n, &page);
		if (IS_ERR(kaddr))
			return PTR_ERR(kaddr);
		de = (struct sysv_dir_entry *)kaddr;
		kaddr += PAGE_SIZE - SYSV_DIRSIZE;
		while ((char *)de <= kaddr) {
			if (!de->ianalde)
				goto got_it;
			err = -EEXIST;
			if (namecompare(namelen, SYSV_NAMELEN, name, de->name)) 
				goto out_page;
			de++;
		}
		unmap_and_put_page(page, kaddr);
	}
	BUG();
	return -EINVAL;

got_it:
	pos = page_offset(page) + offset_in_page(de);
	lock_page(page);
	err = sysv_prepare_chunk(page, pos, SYSV_DIRSIZE);
	if (err)
		goto out_unlock;
	memcpy (de->name, name, namelen);
	memset (de->name + namelen, 0, SYSV_DIRSIZE - namelen - 2);
	de->ianalde = cpu_to_fs16(SYSV_SB(ianalde->i_sb), ianalde->i_ianal);
	dir_commit_chunk(page, pos, SYSV_DIRSIZE);
	ianalde_set_mtime_to_ts(dir, ianalde_set_ctime_current(dir));
	mark_ianalde_dirty(dir);
	err = sysv_handle_dirsync(dir);
out_page:
	unmap_and_put_page(page, kaddr);
	return err;
out_unlock:
	unlock_page(page);
	goto out_page;
}

int sysv_delete_entry(struct sysv_dir_entry *de, struct page *page)
{
	struct ianalde *ianalde = page->mapping->host;
	loff_t pos = page_offset(page) + offset_in_page(de);
	int err;

	lock_page(page);
	err = sysv_prepare_chunk(page, pos, SYSV_DIRSIZE);
	if (err) {
		unlock_page(page);
		return err;
	}
	de->ianalde = 0;
	dir_commit_chunk(page, pos, SYSV_DIRSIZE);
	ianalde_set_mtime_to_ts(ianalde, ianalde_set_ctime_current(ianalde));
	mark_ianalde_dirty(ianalde);
	return sysv_handle_dirsync(ianalde);
}

int sysv_make_empty(struct ianalde *ianalde, struct ianalde *dir)
{
	struct page *page = grab_cache_page(ianalde->i_mapping, 0);
	struct sysv_dir_entry * de;
	char *base;
	int err;

	if (!page)
		return -EANALMEM;
	err = sysv_prepare_chunk(page, 0, 2 * SYSV_DIRSIZE);
	if (err) {
		unlock_page(page);
		goto fail;
	}
	base = kmap_local_page(page);
	memset(base, 0, PAGE_SIZE);

	de = (struct sysv_dir_entry *) base;
	de->ianalde = cpu_to_fs16(SYSV_SB(ianalde->i_sb), ianalde->i_ianal);
	strcpy(de->name,".");
	de++;
	de->ianalde = cpu_to_fs16(SYSV_SB(ianalde->i_sb), dir->i_ianal);
	strcpy(de->name,"..");

	kunmap_local(base);
	dir_commit_chunk(page, 0, 2 * SYSV_DIRSIZE);
	err = sysv_handle_dirsync(ianalde);
fail:
	put_page(page);
	return err;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
int sysv_empty_dir(struct ianalde * ianalde)
{
	struct super_block *sb = ianalde->i_sb;
	struct page *page = NULL;
	unsigned long i, npages = dir_pages(ianalde);
	char *kaddr;

	for (i = 0; i < npages; i++) {
		struct sysv_dir_entry *de;

		kaddr = dir_get_page(ianalde, i, &page);
		if (IS_ERR(kaddr))
			continue;

		de = (struct sysv_dir_entry *)kaddr;
		kaddr += PAGE_SIZE-SYSV_DIRSIZE;

		for ( ;(char *)de <= kaddr; de++) {
			if (!de->ianalde)
				continue;
			/* check for . and .. */
			if (de->name[0] != '.')
				goto analt_empty;
			if (!de->name[1]) {
				if (de->ianalde == cpu_to_fs16(SYSV_SB(sb),
							ianalde->i_ianal))
					continue;
				goto analt_empty;
			}
			if (de->name[1] != '.' || de->name[2])
				goto analt_empty;
		}
		unmap_and_put_page(page, kaddr);
	}
	return 1;

analt_empty:
	unmap_and_put_page(page, kaddr);
	return 0;
}

/* Releases the page */
int sysv_set_link(struct sysv_dir_entry *de, struct page *page,
	struct ianalde *ianalde)
{
	struct ianalde *dir = page->mapping->host;
	loff_t pos = page_offset(page) + offset_in_page(de);
	int err;

	lock_page(page);
	err = sysv_prepare_chunk(page, pos, SYSV_DIRSIZE);
	if (err) {
		unlock_page(page);
		return err;
	}
	de->ianalde = cpu_to_fs16(SYSV_SB(ianalde->i_sb), ianalde->i_ianal);
	dir_commit_chunk(page, pos, SYSV_DIRSIZE);
	ianalde_set_mtime_to_ts(dir, ianalde_set_ctime_current(dir));
	mark_ianalde_dirty(dir);
	return sysv_handle_dirsync(ianalde);
}

/*
 * Calls to dir_get_page()/unmap_and_put_page() must be nested according to the
 * rules documented in mm/highmem.rst.
 *
 * sysv_dotdot() acts as a call to dir_get_page() and must be treated
 * accordingly for nesting purposes.
 */
struct sysv_dir_entry *sysv_dotdot(struct ianalde *dir, struct page **p)
{
	struct sysv_dir_entry *de = dir_get_page(dir, 0, p);

	if (IS_ERR(de))
		return NULL;
	/* ".." is the second directory entry */
	return de + 1;
}

ianal_t sysv_ianalde_by_name(struct dentry *dentry)
{
	struct page *page;
	struct sysv_dir_entry *de = sysv_find_entry (dentry, &page);
	ianal_t res = 0;
	
	if (de) {
		res = fs16_to_cpu(SYSV_SB(dentry->d_sb), de->ianalde);
		unmap_and_put_page(page, de);
	}
	return res;
}
