// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/sysv/dir.c
 *
 *  minix/dir.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  coh/dir.c
 *  Copyright (C) 1993  Pascal Haible, Bruno Haible
 *
 *  sysv/dir.c
 *  Copyright (C) 1993  Bruno Haible
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

static void dir_commit_chunk(struct folio *folio, loff_t pos, unsigned len)
{
	struct address_space *mapping = folio->mapping;
	struct inode *dir = mapping->host;

	block_write_end(NULL, mapping, pos, len, len, folio, NULL);
	if (pos+len > dir->i_size) {
		i_size_write(dir, pos+len);
		mark_inode_dirty(dir);
	}
	folio_unlock(folio);
}

static int sysv_handle_dirsync(struct inode *dir)
{
	int err;

	err = filemap_write_and_wait(dir->i_mapping);
	if (!err)
		err = sync_inode_metadata(dir, 1);
	return err;
}

/*
 * Calls to dir_get_folio()/folio_release_kmap() must be nested according to the
 * rules documented in mm/highmem.rst.
 *
 * NOTE: sysv_find_entry() and sysv_dotdot() act as calls to dir_get_folio()
 * and must be treated accordingly for nesting purposes.
 */
static void *dir_get_folio(struct inode *dir, unsigned long n,
		struct folio **foliop)
{
	struct folio *folio = read_mapping_folio(dir->i_mapping, n, NULL);

	if (IS_ERR(folio))
		return ERR_CAST(folio);
	*foliop = folio;
	return kmap_local_folio(folio, 0);
}

static int sysv_readdir(struct file *file, struct dir_context *ctx)
{
	unsigned long pos = ctx->pos;
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	unsigned long npages = dir_pages(inode);
	unsigned offset;
	unsigned long n;

	ctx->pos = pos = (pos + SYSV_DIRSIZE-1) & ~(SYSV_DIRSIZE-1);
	if (pos >= inode->i_size)
		return 0;

	offset = pos & ~PAGE_MASK;
	n = pos >> PAGE_SHIFT;

	for ( ; n < npages; n++, offset = 0) {
		char *kaddr, *limit;
		struct sysv_dir_entry *de;
		struct folio *folio;

		kaddr = dir_get_folio(inode, n, &folio);
		if (IS_ERR(kaddr))
			continue;
		de = (struct sysv_dir_entry *)(kaddr+offset);
		limit = kaddr + PAGE_SIZE - SYSV_DIRSIZE;
		for ( ;(char*)de <= limit; de++, ctx->pos += sizeof(*de)) {
			char *name = de->name;

			if (!de->inode)
				continue;

			if (!dir_emit(ctx, name, strnlen(name,SYSV_NAMELEN),
					fs16_to_cpu(SYSV_SB(sb), de->inode),
					DT_UNKNOWN)) {
				folio_release_kmap(folio, kaddr);
				return 0;
			}
		}
		folio_release_kmap(folio, kaddr);
	}
	return 0;
}

/* compare strings: name[0..len-1] (not zero-terminated) and
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
 * finds an entry in the specified directory with the wanted name.
 * It does NOT read the inode of the
 * entry - you'll have to do that yourself if you want to.
 *
 * On Success folio_release_kmap() should be called on *foliop.
 *
 * sysv_find_entry() acts as a call to dir_get_folio() and must be treated
 * accordingly for nesting purposes.
 */
struct sysv_dir_entry *sysv_find_entry(struct dentry *dentry, struct folio **foliop)
{
	const char * name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	struct inode * dir = d_inode(dentry->d_parent);
	unsigned long start, n;
	unsigned long npages = dir_pages(dir);
	struct sysv_dir_entry *de;

	start = SYSV_I(dir)->i_dir_start_lookup;
	if (start >= npages)
		start = 0;
	n = start;

	do {
		char *kaddr = dir_get_folio(dir, n, foliop);

		if (!IS_ERR(kaddr)) {
			de = (struct sysv_dir_entry *)kaddr;
			kaddr += folio_size(*foliop) - SYSV_DIRSIZE;
			for ( ; (char *) de <= kaddr ; de++) {
				if (!de->inode)
					continue;
				if (namecompare(namelen, SYSV_NAMELEN,
							name, de->name))
					goto found;
			}
			folio_release_kmap(*foliop, kaddr);
		}

		if (++n >= npages)
			n = 0;
	} while (n != start);

	return NULL;

found:
	SYSV_I(dir)->i_dir_start_lookup = n;
	return de;
}

int sysv_add_link(struct dentry *dentry, struct inode *inode)
{
	struct inode *dir = d_inode(dentry->d_parent);
	const char * name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	struct folio *folio = NULL;
	struct sysv_dir_entry * de;
	unsigned long npages = dir_pages(dir);
	unsigned long n;
	char *kaddr;
	loff_t pos;
	int err;

	/* We take care of directory expansion in the same loop */
	for (n = 0; n <= npages; n++) {
		kaddr = dir_get_folio(dir, n, &folio);
		if (IS_ERR(kaddr))
			return PTR_ERR(kaddr);
		de = (struct sysv_dir_entry *)kaddr;
		kaddr += PAGE_SIZE - SYSV_DIRSIZE;
		while ((char *)de <= kaddr) {
			if (!de->inode)
				goto got_it;
			err = -EEXIST;
			if (namecompare(namelen, SYSV_NAMELEN, name, de->name)) 
				goto out_folio;
			de++;
		}
		folio_release_kmap(folio, kaddr);
	}
	BUG();
	return -EINVAL;

got_it:
	pos = folio_pos(folio) + offset_in_folio(folio, de);
	folio_lock(folio);
	err = sysv_prepare_chunk(folio, pos, SYSV_DIRSIZE);
	if (err)
		goto out_unlock;
	memcpy (de->name, name, namelen);
	memset (de->name + namelen, 0, SYSV_DIRSIZE - namelen - 2);
	de->inode = cpu_to_fs16(SYSV_SB(inode->i_sb), inode->i_ino);
	dir_commit_chunk(folio, pos, SYSV_DIRSIZE);
	inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
	mark_inode_dirty(dir);
	err = sysv_handle_dirsync(dir);
out_folio:
	folio_release_kmap(folio, kaddr);
	return err;
out_unlock:
	folio_unlock(folio);
	goto out_folio;
}

int sysv_delete_entry(struct sysv_dir_entry *de, struct folio *folio)
{
	struct inode *inode = folio->mapping->host;
	loff_t pos = folio_pos(folio) + offset_in_folio(folio, de);
	int err;

	folio_lock(folio);
	err = sysv_prepare_chunk(folio, pos, SYSV_DIRSIZE);
	if (err) {
		folio_unlock(folio);
		return err;
	}
	de->inode = 0;
	dir_commit_chunk(folio, pos, SYSV_DIRSIZE);
	inode_set_mtime_to_ts(inode, inode_set_ctime_current(inode));
	mark_inode_dirty(inode);
	return sysv_handle_dirsync(inode);
}

int sysv_make_empty(struct inode *inode, struct inode *dir)
{
	struct folio *folio = filemap_grab_folio(inode->i_mapping, 0);
	struct sysv_dir_entry * de;
	char *kaddr;
	int err;

	if (IS_ERR(folio))
		return PTR_ERR(folio);
	err = sysv_prepare_chunk(folio, 0, 2 * SYSV_DIRSIZE);
	if (err) {
		folio_unlock(folio);
		goto fail;
	}
	kaddr = kmap_local_folio(folio, 0);
	memset(kaddr, 0, folio_size(folio));

	de = (struct sysv_dir_entry *)kaddr;
	de->inode = cpu_to_fs16(SYSV_SB(inode->i_sb), inode->i_ino);
	strcpy(de->name,".");
	de++;
	de->inode = cpu_to_fs16(SYSV_SB(inode->i_sb), dir->i_ino);
	strcpy(de->name,"..");

	kunmap_local(kaddr);
	dir_commit_chunk(folio, 0, 2 * SYSV_DIRSIZE);
	err = sysv_handle_dirsync(inode);
fail:
	folio_put(folio);
	return err;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
int sysv_empty_dir(struct inode * inode)
{
	struct super_block *sb = inode->i_sb;
	struct folio *folio = NULL;
	unsigned long i, npages = dir_pages(inode);
	char *kaddr;

	for (i = 0; i < npages; i++) {
		struct sysv_dir_entry *de;

		kaddr = dir_get_folio(inode, i, &folio);
		if (IS_ERR(kaddr))
			continue;

		de = (struct sysv_dir_entry *)kaddr;
		kaddr += folio_size(folio) - SYSV_DIRSIZE;

		for ( ;(char *)de <= kaddr; de++) {
			if (!de->inode)
				continue;
			/* check for . and .. */
			if (de->name[0] != '.')
				goto not_empty;
			if (!de->name[1]) {
				if (de->inode == cpu_to_fs16(SYSV_SB(sb),
							inode->i_ino))
					continue;
				goto not_empty;
			}
			if (de->name[1] != '.' || de->name[2])
				goto not_empty;
		}
		folio_release_kmap(folio, kaddr);
	}
	return 1;

not_empty:
	folio_release_kmap(folio, kaddr);
	return 0;
}

/* Releases the page */
int sysv_set_link(struct sysv_dir_entry *de, struct folio *folio,
		struct inode *inode)
{
	struct inode *dir = folio->mapping->host;
	loff_t pos = folio_pos(folio) + offset_in_folio(folio, de);
	int err;

	folio_lock(folio);
	err = sysv_prepare_chunk(folio, pos, SYSV_DIRSIZE);
	if (err) {
		folio_unlock(folio);
		return err;
	}
	de->inode = cpu_to_fs16(SYSV_SB(inode->i_sb), inode->i_ino);
	dir_commit_chunk(folio, pos, SYSV_DIRSIZE);
	inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
	mark_inode_dirty(dir);
	return sysv_handle_dirsync(inode);
}

/*
 * Calls to dir_get_folio()/folio_release_kmap() must be nested according to the
 * rules documented in mm/highmem.rst.
 *
 * sysv_dotdot() acts as a call to dir_get_folio() and must be treated
 * accordingly for nesting purposes.
 */
struct sysv_dir_entry *sysv_dotdot(struct inode *dir, struct folio **foliop)
{
	struct sysv_dir_entry *de = dir_get_folio(dir, 0, foliop);

	if (IS_ERR(de))
		return NULL;
	/* ".." is the second directory entry */
	return de + 1;
}

ino_t sysv_inode_by_name(struct dentry *dentry)
{
	struct folio *folio;
	struct sysv_dir_entry *de = sysv_find_entry (dentry, &folio);
	ino_t res = 0;
	
	if (de) {
		res = fs16_to_cpu(SYSV_SB(dentry->d_sb), de->inode);
		folio_release_kmap(folio, de);
	}
	return res;
}
