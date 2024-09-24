// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/ufs/ufs_dir.c
 *
 * Copyright (C) 1996
 * Adrian Rodriguez (adrian@franklins-tower.rutgers.edu)
 * Laboratory for Computer Science Research Computing Facility
 * Rutgers, The State University of New Jersey
 *
 * swab support by Francois-Rene Rideau <fare@tunes.org> 19970406
 *
 * 4.4BSD (FreeBSD) support added on February 1st 1998 by
 * Niels Kristian Bech Jensen <nkbj@image.dk> partially based
 * on code by Martin von Loewis <martin@mira.isdn.cs.tu-berlin.de>.
 *
 * Migration to usage of "page cache" on May 2006 by
 * Evgeniy Dushistov <dushistov@mail.ru> based on ext2 code base.
 */

#include <linux/time.h>
#include <linux/fs.h>
#include <linux/swap.h>
#include <linux/iversion.h>

#include "ufs_fs.h"
#include "ufs.h"
#include "swab.h"
#include "util.h"

/*
 * NOTE! unlike strncmp, ufs_match returns 1 for success, 0 for failure.
 *
 * len <= UFS_MAXNAMLEN and de != NULL are guaranteed by caller.
 */
static inline int ufs_match(struct super_block *sb, int len,
		const unsigned char *name, struct ufs_dir_entry *de)
{
	if (len != ufs_get_de_namlen(sb, de))
		return 0;
	if (!de->d_ino)
		return 0;
	return !memcmp(name, de->d_name, len);
}

static void ufs_commit_chunk(struct folio *folio, loff_t pos, unsigned len)
{
	struct address_space *mapping = folio->mapping;
	struct inode *dir = mapping->host;

	inode_inc_iversion(dir);
	block_write_end(NULL, mapping, pos, len, len, folio, NULL);
	if (pos+len > dir->i_size) {
		i_size_write(dir, pos+len);
		mark_inode_dirty(dir);
	}
	folio_unlock(folio);
}

static int ufs_handle_dirsync(struct inode *dir)
{
	int err;

	err = filemap_write_and_wait(dir->i_mapping);
	if (!err)
		err = sync_inode_metadata(dir, 1);
	return err;
}

ino_t ufs_inode_by_name(struct inode *dir, const struct qstr *qstr)
{
	ino_t res = 0;
	struct ufs_dir_entry *de;
	struct folio *folio;
	
	de = ufs_find_entry(dir, qstr, &folio);
	if (de) {
		res = fs32_to_cpu(dir->i_sb, de->d_ino);
		folio_release_kmap(folio, de);
	}
	return res;
}


/* Releases the page */
void ufs_set_link(struct inode *dir, struct ufs_dir_entry *de,
		  struct folio *folio, struct inode *inode,
		  bool update_times)
{
	loff_t pos = folio_pos(folio) + offset_in_folio(folio, de);
	unsigned len = fs16_to_cpu(dir->i_sb, de->d_reclen);
	int err;

	folio_lock(folio);
	err = ufs_prepare_chunk(folio, pos, len);
	BUG_ON(err);

	de->d_ino = cpu_to_fs32(dir->i_sb, inode->i_ino);
	ufs_set_de_type(dir->i_sb, de, inode->i_mode);

	ufs_commit_chunk(folio, pos, len);
	folio_release_kmap(folio, de);
	if (update_times)
		inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
	mark_inode_dirty(dir);
	ufs_handle_dirsync(dir);
}

static bool ufs_check_folio(struct folio *folio, char *kaddr)
{
	struct inode *dir = folio->mapping->host;
	struct super_block *sb = dir->i_sb;
	unsigned offs, rec_len;
	unsigned limit = folio_size(folio);
	const unsigned chunk_mask = UFS_SB(sb)->s_uspi->s_dirblksize - 1;
	struct ufs_dir_entry *p;
	char *error;

	if (dir->i_size < folio_pos(folio) + limit) {
		limit = offset_in_folio(folio, dir->i_size);
		if (limit & chunk_mask)
			goto Ebadsize;
		if (!limit)
			goto out;
	}
	for (offs = 0; offs <= limit - UFS_DIR_REC_LEN(1); offs += rec_len) {
		p = (struct ufs_dir_entry *)(kaddr + offs);
		rec_len = fs16_to_cpu(sb, p->d_reclen);

		if (rec_len < UFS_DIR_REC_LEN(1))
			goto Eshort;
		if (rec_len & 3)
			goto Ealign;
		if (rec_len < UFS_DIR_REC_LEN(ufs_get_de_namlen(sb, p)))
			goto Enamelen;
		if (((offs + rec_len - 1) ^ offs) & ~chunk_mask)
			goto Espan;
		if (fs32_to_cpu(sb, p->d_ino) > (UFS_SB(sb)->s_uspi->s_ipg *
						  UFS_SB(sb)->s_uspi->s_ncg))
			goto Einumber;
	}
	if (offs != limit)
		goto Eend;
out:
	folio_set_checked(folio);
	return true;

	/* Too bad, we had an error */

Ebadsize:
	ufs_error(sb, __func__,
		  "size of directory #%lu is not a multiple of chunk size",
		  dir->i_ino
	);
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
	error = "inode out of bounds";
bad_entry:
	ufs_error(sb, __func__, "bad entry in directory #%lu: %s - "
		   "offset=%llu, rec_len=%d, name_len=%d",
		   dir->i_ino, error, folio_pos(folio) + offs,
		   rec_len, ufs_get_de_namlen(sb, p));
	goto fail;
Eend:
	p = (struct ufs_dir_entry *)(kaddr + offs);
	ufs_error(sb, __func__,
		   "entry in directory #%lu spans the page boundary"
		   "offset=%llu",
		   dir->i_ino, folio_pos(folio) + offs);
fail:
	return false;
}

static void *ufs_get_folio(struct inode *dir, unsigned long n,
		struct folio **foliop)
{
	struct address_space *mapping = dir->i_mapping;
	struct folio *folio = read_mapping_folio(mapping, n, NULL);
	void *kaddr;

	if (IS_ERR(folio))
		return ERR_CAST(folio);
	kaddr = kmap_local_folio(folio, 0);
	if (unlikely(!folio_test_checked(folio))) {
		if (!ufs_check_folio(folio, kaddr))
			goto fail;
	}
	*foliop = folio;
	return kaddr;

fail:
	folio_release_kmap(folio, kaddr);
	return ERR_PTR(-EIO);
}

/*
 * Return the offset into page `page_nr' of the last valid
 * byte in that page, plus one.
 */
static unsigned
ufs_last_byte(struct inode *inode, unsigned long page_nr)
{
	unsigned last_byte = inode->i_size;

	last_byte -= page_nr << PAGE_SHIFT;
	if (last_byte > PAGE_SIZE)
		last_byte = PAGE_SIZE;
	return last_byte;
}

static inline struct ufs_dir_entry *
ufs_next_entry(struct super_block *sb, struct ufs_dir_entry *p)
{
	return (struct ufs_dir_entry *)((char *)p +
					fs16_to_cpu(sb, p->d_reclen));
}

struct ufs_dir_entry *ufs_dotdot(struct inode *dir, struct folio **foliop)
{
	struct ufs_dir_entry *de = ufs_get_folio(dir, 0, foliop);

	if (!IS_ERR(de))
		return ufs_next_entry(dir->i_sb, de);

	return NULL;
}

/*
 *	ufs_find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the page in which the entry was found, and the entry itself
 * (as a parameter - res_dir). Page is returned mapped and unlocked.
 * Entry is guaranteed to be valid.
 */
struct ufs_dir_entry *ufs_find_entry(struct inode *dir, const struct qstr *qstr,
				     struct folio **foliop)
{
	struct super_block *sb = dir->i_sb;
	const unsigned char *name = qstr->name;
	int namelen = qstr->len;
	unsigned reclen = UFS_DIR_REC_LEN(namelen);
	unsigned long start, n;
	unsigned long npages = dir_pages(dir);
	struct ufs_inode_info *ui = UFS_I(dir);
	struct ufs_dir_entry *de;

	UFSD("ENTER, dir_ino %lu, name %s, namlen %u\n", dir->i_ino, name, namelen);

	if (npages == 0 || namelen > UFS_MAXNAMLEN)
		goto out;

	start = ui->i_dir_start_lookup;

	if (start >= npages)
		start = 0;
	n = start;
	do {
		char *kaddr = ufs_get_folio(dir, n, foliop);

		if (!IS_ERR(kaddr)) {
			de = (struct ufs_dir_entry *)kaddr;
			kaddr += ufs_last_byte(dir, n) - reclen;
			while ((char *) de <= kaddr) {
				if (ufs_match(sb, namelen, name, de))
					goto found;
				de = ufs_next_entry(sb, de);
			}
			folio_release_kmap(*foliop, kaddr);
		}
		if (++n >= npages)
			n = 0;
	} while (n != start);
out:
	return NULL;

found:
	ui->i_dir_start_lookup = n;
	return de;
}

/*
 *	Parent is locked.
 */
int ufs_add_link(struct dentry *dentry, struct inode *inode)
{
	struct inode *dir = d_inode(dentry->d_parent);
	const unsigned char *name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	struct super_block *sb = dir->i_sb;
	unsigned reclen = UFS_DIR_REC_LEN(namelen);
	const unsigned int chunk_size = UFS_SB(sb)->s_uspi->s_dirblksize;
	unsigned short rec_len, name_len;
	struct folio *folio = NULL;
	struct ufs_dir_entry *de;
	unsigned long npages = dir_pages(dir);
	unsigned long n;
	loff_t pos;
	int err;

	UFSD("ENTER, name %s, namelen %u\n", name, namelen);

	/*
	 * We take care of directory expansion in the same loop.
	 * This code plays outside i_size, so it locks the folio
	 * to protect that region.
	 */
	for (n = 0; n <= npages; n++) {
		char *kaddr = ufs_get_folio(dir, n, &folio);
		char *dir_end;

		if (IS_ERR(kaddr))
			return PTR_ERR(kaddr);
		folio_lock(folio);
		dir_end = kaddr + ufs_last_byte(dir, n);
		de = (struct ufs_dir_entry *)kaddr;
		kaddr += folio_size(folio) - reclen;
		while ((char *)de <= kaddr) {
			if ((char *)de == dir_end) {
				/* We hit i_size */
				name_len = 0;
				rec_len = chunk_size;
				de->d_reclen = cpu_to_fs16(sb, chunk_size);
				de->d_ino = 0;
				goto got_it;
			}
			if (de->d_reclen == 0) {
				ufs_error(dir->i_sb, __func__,
					  "zero-length directory entry");
				err = -EIO;
				goto out_unlock;
			}
			err = -EEXIST;
			if (ufs_match(sb, namelen, name, de))
				goto out_unlock;
			name_len = UFS_DIR_REC_LEN(ufs_get_de_namlen(sb, de));
			rec_len = fs16_to_cpu(sb, de->d_reclen);
			if (!de->d_ino && rec_len >= reclen)
				goto got_it;
			if (rec_len >= name_len + reclen)
				goto got_it;
			de = (struct ufs_dir_entry *) ((char *) de + rec_len);
		}
		folio_unlock(folio);
		folio_release_kmap(folio, kaddr);
	}
	BUG();
	return -EINVAL;

got_it:
	pos = folio_pos(folio) + offset_in_folio(folio, de);
	err = ufs_prepare_chunk(folio, pos, rec_len);
	if (err)
		goto out_unlock;
	if (de->d_ino) {
		struct ufs_dir_entry *de1 =
			(struct ufs_dir_entry *) ((char *) de + name_len);
		de1->d_reclen = cpu_to_fs16(sb, rec_len - name_len);
		de->d_reclen = cpu_to_fs16(sb, name_len);

		de = de1;
	}

	ufs_set_de_namlen(sb, de, namelen);
	memcpy(de->d_name, name, namelen + 1);
	de->d_ino = cpu_to_fs32(sb, inode->i_ino);
	ufs_set_de_type(sb, de, inode->i_mode);

	ufs_commit_chunk(folio, pos, rec_len);
	inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));

	mark_inode_dirty(dir);
	err = ufs_handle_dirsync(dir);
	/* OFFSET_CACHE */
out_put:
	folio_release_kmap(folio, de);
	return err;
out_unlock:
	folio_unlock(folio);
	goto out_put;
}

static inline unsigned
ufs_validate_entry(struct super_block *sb, char *base,
		   unsigned offset, unsigned mask)
{
	struct ufs_dir_entry *de = (struct ufs_dir_entry*)(base + offset);
	struct ufs_dir_entry *p = (struct ufs_dir_entry*)(base + (offset&mask));
	while ((char*)p < (char*)de)
		p = ufs_next_entry(sb, p);
	return (char *)p - base;
}


/*
 * This is blatantly stolen from ext2fs
 */
static int
ufs_readdir(struct file *file, struct dir_context *ctx)
{
	loff_t pos = ctx->pos;
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	unsigned int offset = pos & ~PAGE_MASK;
	unsigned long n = pos >> PAGE_SHIFT;
	unsigned long npages = dir_pages(inode);
	unsigned chunk_mask = ~(UFS_SB(sb)->s_uspi->s_dirblksize - 1);
	bool need_revalidate = !inode_eq_iversion(inode, *(u64 *)file->private_data);
	unsigned flags = UFS_SB(sb)->s_flags;

	UFSD("BEGIN\n");

	if (pos > inode->i_size - UFS_DIR_REC_LEN(1))
		return 0;

	for ( ; n < npages; n++, offset = 0) {
		struct ufs_dir_entry *de;
		struct folio *folio;
		char *kaddr = ufs_get_folio(inode, n, &folio);
		char *limit;

		if (IS_ERR(kaddr)) {
			ufs_error(sb, __func__,
				  "bad page in #%lu",
				  inode->i_ino);
			ctx->pos += PAGE_SIZE - offset;
			return PTR_ERR(kaddr);
		}
		if (unlikely(need_revalidate)) {
			if (offset) {
				offset = ufs_validate_entry(sb, kaddr, offset, chunk_mask);
				ctx->pos = (n<<PAGE_SHIFT) + offset;
			}
			*(u64 *)file->private_data = inode_query_iversion(inode);
			need_revalidate = false;
		}
		de = (struct ufs_dir_entry *)(kaddr+offset);
		limit = kaddr + ufs_last_byte(inode, n) - UFS_DIR_REC_LEN(1);
		for ( ;(char*)de <= limit; de = ufs_next_entry(sb, de)) {
			if (de->d_ino) {
				unsigned char d_type = DT_UNKNOWN;

				UFSD("filldir(%s,%u)\n", de->d_name,
				      fs32_to_cpu(sb, de->d_ino));
				UFSD("namlen %u\n", ufs_get_de_namlen(sb, de));

				if ((flags & UFS_DE_MASK) == UFS_DE_44BSD)
					d_type = de->d_u.d_44.d_type;

				if (!dir_emit(ctx, de->d_name,
					       ufs_get_de_namlen(sb, de),
					       fs32_to_cpu(sb, de->d_ino),
					       d_type)) {
					folio_release_kmap(folio, de);
					return 0;
				}
			}
			ctx->pos += fs16_to_cpu(sb, de->d_reclen);
		}
		folio_release_kmap(folio, kaddr);
	}
	return 0;
}


/*
 * ufs_delete_entry deletes a directory entry by merging it with the
 * previous entry.
 */
int ufs_delete_entry(struct inode *inode, struct ufs_dir_entry *dir,
		     struct folio *folio)
{
	struct super_block *sb = inode->i_sb;
	size_t from, to;
	char *kaddr;
	loff_t pos;
	struct ufs_dir_entry *de, *pde = NULL;
	int err;

	UFSD("ENTER\n");

	from = offset_in_folio(folio, dir);
	to = from + fs16_to_cpu(sb, dir->d_reclen);
	kaddr = (char *)dir - from;
	from &= ~(UFS_SB(sb)->s_uspi->s_dirblksize - 1);
	de = (struct ufs_dir_entry *) (kaddr + from);

	UFSD("ino %u, reclen %u, namlen %u, name %s\n",
	      fs32_to_cpu(sb, de->d_ino),
	      fs16_to_cpu(sb, de->d_reclen),
	      ufs_get_de_namlen(sb, de), de->d_name);

	while ((char*)de < (char*)dir) {
		if (de->d_reclen == 0) {
			ufs_error(inode->i_sb, __func__,
				  "zero-length directory entry");
			err = -EIO;
			goto out;
		}
		pde = de;
		de = ufs_next_entry(sb, de);
	}
	if (pde)
		from = offset_in_folio(folio, pde);
	pos = folio_pos(folio) + from;
	folio_lock(folio);
	err = ufs_prepare_chunk(folio, pos, to - from);
	BUG_ON(err);
	if (pde)
		pde->d_reclen = cpu_to_fs16(sb, to - from);
	dir->d_ino = 0;
	ufs_commit_chunk(folio, pos, to - from);
	inode_set_mtime_to_ts(inode, inode_set_ctime_current(inode));
	mark_inode_dirty(inode);
	err = ufs_handle_dirsync(inode);
out:
	folio_release_kmap(folio, kaddr);
	UFSD("EXIT\n");
	return err;
}

int ufs_make_empty(struct inode * inode, struct inode *dir)
{
	struct super_block * sb = dir->i_sb;
	struct address_space *mapping = inode->i_mapping;
	struct folio *folio = filemap_grab_folio(mapping, 0);
	const unsigned int chunk_size = UFS_SB(sb)->s_uspi->s_dirblksize;
	struct ufs_dir_entry * de;
	int err;
	char *kaddr;

	if (IS_ERR(folio))
		return PTR_ERR(folio);

	err = ufs_prepare_chunk(folio, 0, chunk_size);
	if (err) {
		folio_unlock(folio);
		goto fail;
	}

	kaddr = kmap_local_folio(folio, 0);
	memset(kaddr, 0, folio_size(folio));

	de = (struct ufs_dir_entry *)kaddr;

	de->d_ino = cpu_to_fs32(sb, inode->i_ino);
	ufs_set_de_type(sb, de, inode->i_mode);
	ufs_set_de_namlen(sb, de, 1);
	de->d_reclen = cpu_to_fs16(sb, UFS_DIR_REC_LEN(1));
	strcpy (de->d_name, ".");
	de = (struct ufs_dir_entry *)
		((char *)de + fs16_to_cpu(sb, de->d_reclen));
	de->d_ino = cpu_to_fs32(sb, dir->i_ino);
	ufs_set_de_type(sb, de, dir->i_mode);
	de->d_reclen = cpu_to_fs16(sb, chunk_size - UFS_DIR_REC_LEN(1));
	ufs_set_de_namlen(sb, de, 2);
	strcpy (de->d_name, "..");
	kunmap_local(kaddr);

	ufs_commit_chunk(folio, 0, chunk_size);
	err = ufs_handle_dirsync(inode);
fail:
	folio_put(folio);
	return err;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
int ufs_empty_dir(struct inode * inode)
{
	struct super_block *sb = inode->i_sb;
	struct folio *folio;
	char *kaddr;
	unsigned long i, npages = dir_pages(inode);

	for (i = 0; i < npages; i++) {
		struct ufs_dir_entry *de;

		kaddr = ufs_get_folio(inode, i, &folio);
		if (IS_ERR(kaddr))
			continue;

		de = (struct ufs_dir_entry *)kaddr;
		kaddr += ufs_last_byte(inode, i) - UFS_DIR_REC_LEN(1);

		while ((char *)de <= kaddr) {
			if (de->d_reclen == 0) {
				ufs_error(inode->i_sb, __func__,
					"zero-length directory entry: "
					"kaddr=%p, de=%p\n", kaddr, de);
				goto not_empty;
			}
			if (de->d_ino) {
				u16 namelen=ufs_get_de_namlen(sb, de);
				/* check for . and .. */
				if (de->d_name[0] != '.')
					goto not_empty;
				if (namelen > 2)
					goto not_empty;
				if (namelen < 2) {
					if (inode->i_ino !=
					    fs32_to_cpu(sb, de->d_ino))
						goto not_empty;
				} else if (de->d_name[1] != '.')
					goto not_empty;
			}
			de = ufs_next_entry(sb, de);
		}
		folio_release_kmap(folio, kaddr);
	}
	return 1;

not_empty:
	folio_release_kmap(folio, kaddr);
	return 0;
}

static int ufs_dir_open(struct inode *inode, struct file *file)
{
	file->private_data = kzalloc(sizeof(u64), GFP_KERNEL);
	if (!file->private_data)
		return -ENOMEM;
	return 0;
}

static int ufs_dir_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

static loff_t ufs_dir_llseek(struct file *file, loff_t offset, int whence)
{
	return generic_llseek_cookie(file, offset, whence,
				     (u64 *)file->private_data);
}

const struct file_operations ufs_dir_operations = {
	.open		= ufs_dir_open,
	.release	= ufs_dir_release,
	.read		= generic_read_dir,
	.iterate_shared	= ufs_readdir,
	.fsync		= generic_file_fsync,
	.llseek		= ufs_dir_llseek,
};
