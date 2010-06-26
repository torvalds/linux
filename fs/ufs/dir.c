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

static int ufs_commit_chunk(struct page *page, loff_t pos, unsigned len)
{
	struct address_space *mapping = page->mapping;
	struct inode *dir = mapping->host;
	int err = 0;

	dir->i_version++;
	block_write_end(NULL, mapping, pos, len, len, page, NULL);
	if (pos+len > dir->i_size) {
		i_size_write(dir, pos+len);
		mark_inode_dirty(dir);
	}
	if (IS_DIRSYNC(dir))
		err = write_one_page(page, 1);
	else
		unlock_page(page);
	return err;
}

static inline void ufs_put_page(struct page *page)
{
	kunmap(page);
	page_cache_release(page);
}

static inline unsigned long ufs_dir_pages(struct inode *inode)
{
	return (inode->i_size+PAGE_CACHE_SIZE-1)>>PAGE_CACHE_SHIFT;
}

ino_t ufs_inode_by_name(struct inode *dir, const struct qstr *qstr)
{
	ino_t res = 0;
	struct ufs_dir_entry *de;
	struct page *page;
	
	de = ufs_find_entry(dir, qstr, &page);
	if (de) {
		res = fs32_to_cpu(dir->i_sb, de->d_ino);
		ufs_put_page(page);
	}
	return res;
}


/* Releases the page */
void ufs_set_link(struct inode *dir, struct ufs_dir_entry *de,
		  struct page *page, struct inode *inode)
{
	loff_t pos = page_offset(page) +
			(char *) de - (char *) page_address(page);
	unsigned len = fs16_to_cpu(dir->i_sb, de->d_reclen);
	int err;

	lock_page(page);
	err = __ufs_write_begin(NULL, page->mapping, pos, len,
				AOP_FLAG_UNINTERRUPTIBLE, &page, NULL);
	BUG_ON(err);

	de->d_ino = cpu_to_fs32(dir->i_sb, inode->i_ino);
	ufs_set_de_type(dir->i_sb, de, inode->i_mode);

	err = ufs_commit_chunk(page, pos, len);
	ufs_put_page(page);
	dir->i_mtime = dir->i_ctime = CURRENT_TIME_SEC;
	mark_inode_dirty(dir);
}


static void ufs_check_page(struct page *page)
{
	struct inode *dir = page->mapping->host;
	struct super_block *sb = dir->i_sb;
	char *kaddr = page_address(page);
	unsigned offs, rec_len;
	unsigned limit = PAGE_CACHE_SIZE;
	const unsigned chunk_mask = UFS_SB(sb)->s_uspi->s_dirblksize - 1;
	struct ufs_dir_entry *p;
	char *error;

	if ((dir->i_size >> PAGE_CACHE_SHIFT) == page->index) {
		limit = dir->i_size & ~PAGE_CACHE_MASK;
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
	SetPageChecked(page);
	return;

	/* Too bad, we had an error */

Ebadsize:
	ufs_error(sb, "ufs_check_page",
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
	ufs_error (sb, "ufs_check_page", "bad entry in directory #%lu: %s - "
		   "offset=%lu, rec_len=%d, name_len=%d",
		   dir->i_ino, error, (page->index<<PAGE_CACHE_SHIFT)+offs,
		   rec_len, ufs_get_de_namlen(sb, p));
	goto fail;
Eend:
	p = (struct ufs_dir_entry *)(kaddr + offs);
	ufs_error(sb, __func__,
		   "entry in directory #%lu spans the page boundary"
		   "offset=%lu",
		   dir->i_ino, (page->index<<PAGE_CACHE_SHIFT)+offs);
fail:
	SetPageChecked(page);
	SetPageError(page);
}

static struct page *ufs_get_page(struct inode *dir, unsigned long n)
{
	struct address_space *mapping = dir->i_mapping;
	struct page *page = read_mapping_page(mapping, n, NULL);
	if (!IS_ERR(page)) {
		kmap(page);
		if (!PageChecked(page))
			ufs_check_page(page);
		if (PageError(page))
			goto fail;
	}
	return page;

fail:
	ufs_put_page(page);
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

	last_byte -= page_nr << PAGE_CACHE_SHIFT;
	if (last_byte > PAGE_CACHE_SIZE)
		last_byte = PAGE_CACHE_SIZE;
	return last_byte;
}

static inline struct ufs_dir_entry *
ufs_next_entry(struct super_block *sb, struct ufs_dir_entry *p)
{
	return (struct ufs_dir_entry *)((char *)p +
					fs16_to_cpu(sb, p->d_reclen));
}

struct ufs_dir_entry *ufs_dotdot(struct inode *dir, struct page **p)
{
	struct page *page = ufs_get_page(dir, 0);
	struct ufs_dir_entry *de = NULL;

	if (!IS_ERR(page)) {
		de = ufs_next_entry(dir->i_sb,
				    (struct ufs_dir_entry *)page_address(page));
		*p = page;
	}
	return de;
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
				     struct page **res_page)
{
	struct super_block *sb = dir->i_sb;
	const unsigned char *name = qstr->name;
	int namelen = qstr->len;
	unsigned reclen = UFS_DIR_REC_LEN(namelen);
	unsigned long start, n;
	unsigned long npages = ufs_dir_pages(dir);
	struct page *page = NULL;
	struct ufs_inode_info *ui = UFS_I(dir);
	struct ufs_dir_entry *de;

	UFSD("ENTER, dir_ino %lu, name %s, namlen %u\n", dir->i_ino, name, namelen);

	if (npages == 0 || namelen > UFS_MAXNAMLEN)
		goto out;

	/* OFFSET_CACHE */
	*res_page = NULL;

	start = ui->i_dir_start_lookup;

	if (start >= npages)
		start = 0;
	n = start;
	do {
		char *kaddr;
		page = ufs_get_page(dir, n);
		if (!IS_ERR(page)) {
			kaddr = page_address(page);
			de = (struct ufs_dir_entry *) kaddr;
			kaddr += ufs_last_byte(dir, n) - reclen;
			while ((char *) de <= kaddr) {
				if (de->d_reclen == 0) {
					ufs_error(dir->i_sb, __func__,
						  "zero-length directory entry");
					ufs_put_page(page);
					goto out;
				}
				if (ufs_match(sb, namelen, name, de))
					goto found;
				de = ufs_next_entry(sb, de);
			}
			ufs_put_page(page);
		}
		if (++n >= npages)
			n = 0;
	} while (n != start);
out:
	return NULL;

found:
	*res_page = page;
	ui->i_dir_start_lookup = n;
	return de;
}

/*
 *	Parent is locked.
 */
int ufs_add_link(struct dentry *dentry, struct inode *inode)
{
	struct inode *dir = dentry->d_parent->d_inode;
	const unsigned char *name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	struct super_block *sb = dir->i_sb;
	unsigned reclen = UFS_DIR_REC_LEN(namelen);
	const unsigned int chunk_size = UFS_SB(sb)->s_uspi->s_dirblksize;
	unsigned short rec_len, name_len;
	struct page *page = NULL;
	struct ufs_dir_entry *de;
	unsigned long npages = ufs_dir_pages(dir);
	unsigned long n;
	char *kaddr;
	loff_t pos;
	int err;

	UFSD("ENTER, name %s, namelen %u\n", name, namelen);

	/*
	 * We take care of directory expansion in the same loop.
	 * This code plays outside i_size, so it locks the page
	 * to protect that region.
	 */
	for (n = 0; n <= npages; n++) {
		char *dir_end;

		page = ufs_get_page(dir, n);
		err = PTR_ERR(page);
		if (IS_ERR(page))
			goto out;
		lock_page(page);
		kaddr = page_address(page);
		dir_end = kaddr + ufs_last_byte(dir, n);
		de = (struct ufs_dir_entry *)kaddr;
		kaddr += PAGE_CACHE_SIZE - reclen;
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
		unlock_page(page);
		ufs_put_page(page);
	}
	BUG();
	return -EINVAL;

got_it:
	pos = page_offset(page) +
			(char*)de - (char*)page_address(page);
	err = __ufs_write_begin(NULL, page->mapping, pos, rec_len,
				AOP_FLAG_UNINTERRUPTIBLE, &page, NULL);
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

	err = ufs_commit_chunk(page, pos, rec_len);
	dir->i_mtime = dir->i_ctime = CURRENT_TIME_SEC;

	mark_inode_dirty(dir);
	/* OFFSET_CACHE */
out_put:
	ufs_put_page(page);
out:
	return err;
out_unlock:
	unlock_page(page);
	goto out_put;
}

static inline unsigned
ufs_validate_entry(struct super_block *sb, char *base,
		   unsigned offset, unsigned mask)
{
	struct ufs_dir_entry *de = (struct ufs_dir_entry*)(base + offset);
	struct ufs_dir_entry *p = (struct ufs_dir_entry*)(base + (offset&mask));
	while ((char*)p < (char*)de) {
		if (p->d_reclen == 0)
			break;
		p = ufs_next_entry(sb, p);
	}
	return (char *)p - base;
}


/*
 * This is blatantly stolen from ext2fs
 */
static int
ufs_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	loff_t pos = filp->f_pos;
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct super_block *sb = inode->i_sb;
	unsigned int offset = pos & ~PAGE_CACHE_MASK;
	unsigned long n = pos >> PAGE_CACHE_SHIFT;
	unsigned long npages = ufs_dir_pages(inode);
	unsigned chunk_mask = ~(UFS_SB(sb)->s_uspi->s_dirblksize - 1);
	int need_revalidate = filp->f_version != inode->i_version;
	unsigned flags = UFS_SB(sb)->s_flags;

	UFSD("BEGIN\n");

	if (pos > inode->i_size - UFS_DIR_REC_LEN(1))
		return 0;

	for ( ; n < npages; n++, offset = 0) {
		char *kaddr, *limit;
		struct ufs_dir_entry *de;

		struct page *page = ufs_get_page(inode, n);

		if (IS_ERR(page)) {
			ufs_error(sb, __func__,
				  "bad page in #%lu",
				  inode->i_ino);
			filp->f_pos += PAGE_CACHE_SIZE - offset;
			return -EIO;
		}
		kaddr = page_address(page);
		if (unlikely(need_revalidate)) {
			if (offset) {
				offset = ufs_validate_entry(sb, kaddr, offset, chunk_mask);
				filp->f_pos = (n<<PAGE_CACHE_SHIFT) + offset;
			}
			filp->f_version = inode->i_version;
			need_revalidate = 0;
		}
		de = (struct ufs_dir_entry *)(kaddr+offset);
		limit = kaddr + ufs_last_byte(inode, n) - UFS_DIR_REC_LEN(1);
		for ( ;(char*)de <= limit; de = ufs_next_entry(sb, de)) {
			if (de->d_reclen == 0) {
				ufs_error(sb, __func__,
					"zero-length directory entry");
				ufs_put_page(page);
				return -EIO;
			}
			if (de->d_ino) {
				int over;
				unsigned char d_type = DT_UNKNOWN;

				offset = (char *)de - kaddr;

				UFSD("filldir(%s,%u)\n", de->d_name,
				      fs32_to_cpu(sb, de->d_ino));
				UFSD("namlen %u\n", ufs_get_de_namlen(sb, de));

				if ((flags & UFS_DE_MASK) == UFS_DE_44BSD)
					d_type = de->d_u.d_44.d_type;

				over = filldir(dirent, de->d_name,
					       ufs_get_de_namlen(sb, de),
						(n<<PAGE_CACHE_SHIFT) | offset,
					       fs32_to_cpu(sb, de->d_ino), d_type);
				if (over) {
					ufs_put_page(page);
					return 0;
				}
			}
			filp->f_pos += fs16_to_cpu(sb, de->d_reclen);
		}
		ufs_put_page(page);
	}
	return 0;
}


/*
 * ufs_delete_entry deletes a directory entry by merging it with the
 * previous entry.
 */
int ufs_delete_entry(struct inode *inode, struct ufs_dir_entry *dir,
		     struct page * page)
{
	struct super_block *sb = inode->i_sb;
	struct address_space *mapping = page->mapping;
	char *kaddr = page_address(page);
	unsigned from = ((char*)dir - kaddr) & ~(UFS_SB(sb)->s_uspi->s_dirblksize - 1);
	unsigned to = ((char*)dir - kaddr) + fs16_to_cpu(sb, dir->d_reclen);
	loff_t pos;
	struct ufs_dir_entry *pde = NULL;
	struct ufs_dir_entry *de = (struct ufs_dir_entry *) (kaddr + from);
	int err;

	UFSD("ENTER\n");

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
		from = (char*)pde - (char*)page_address(page);

	pos = page_offset(page) + from;
	lock_page(page);
	err = __ufs_write_begin(NULL, mapping, pos, to - from,
				AOP_FLAG_UNINTERRUPTIBLE, &page, NULL);
	BUG_ON(err);
	if (pde)
		pde->d_reclen = cpu_to_fs16(sb, to - from);
	dir->d_ino = 0;
	err = ufs_commit_chunk(page, pos, to - from);
	inode->i_ctime = inode->i_mtime = CURRENT_TIME_SEC;
	mark_inode_dirty(inode);
out:
	ufs_put_page(page);
	UFSD("EXIT\n");
	return err;
}

int ufs_make_empty(struct inode * inode, struct inode *dir)
{
	struct super_block * sb = dir->i_sb;
	struct address_space *mapping = inode->i_mapping;
	struct page *page = grab_cache_page(mapping, 0);
	const unsigned int chunk_size = UFS_SB(sb)->s_uspi->s_dirblksize;
	struct ufs_dir_entry * de;
	char *base;
	int err;

	if (!page)
		return -ENOMEM;

	err = __ufs_write_begin(NULL, mapping, 0, chunk_size,
				AOP_FLAG_UNINTERRUPTIBLE, &page, NULL);
	if (err) {
		unlock_page(page);
		goto fail;
	}

	kmap(page);
	base = (char*)page_address(page);
	memset(base, 0, PAGE_CACHE_SIZE);

	de = (struct ufs_dir_entry *) base;

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
	kunmap(page);

	err = ufs_commit_chunk(page, 0, chunk_size);
fail:
	page_cache_release(page);
	return err;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
int ufs_empty_dir(struct inode * inode)
{
	struct super_block *sb = inode->i_sb;
	struct page *page = NULL;
	unsigned long i, npages = ufs_dir_pages(inode);

	for (i = 0; i < npages; i++) {
		char *kaddr;
		struct ufs_dir_entry *de;
		page = ufs_get_page(inode, i);

		if (IS_ERR(page))
			continue;

		kaddr = page_address(page);
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
		ufs_put_page(page);
	}
	return 1;

not_empty:
	ufs_put_page(page);
	return 0;
}

const struct file_operations ufs_dir_operations = {
	.read		= generic_read_dir,
	.readdir	= ufs_readdir,
	.fsync		= generic_file_fsync,
	.llseek		= generic_file_llseek,
};
