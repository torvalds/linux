/*
 * Copyright (C) 2005, 2006
 * Avishay Traeger (avishay@gmail.com)
 * Copyright (C) 2008, 2009
 * Boaz Harrosh <ooo@electrozaur.com>
 *
 * Copyrights for code taken from ext2:
 *     Copyright (C) 1992, 1993, 1994, 1995
 *     Remy Card (card@masi.ibp.fr)
 *     Laboratoire MASI - Institut Blaise Pascal
 *     Universite Pierre et Marie Curie (Paris VI)
 *     from
 *     linux/fs/minix/inode.c
 *     Copyright (C) 1991, 1992  Linus Torvalds
 *
 * This file is part of exofs.
 *
 * exofs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.  Since it is based on ext2, and the only
 * valid version of GPL for the Linux kernel is version 2, the only valid
 * version of GPL for exofs is version 2.
 *
 * exofs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with exofs; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "exofs.h"

static inline unsigned exofs_chunk_size(struct inode *inode)
{
	return inode->i_sb->s_blocksize;
}

static inline void exofs_put_page(struct page *page)
{
	kunmap(page);
	page_cache_release(page);
}

/* Accesses dir's inode->i_size must be called under inode lock */
static inline unsigned long dir_pages(struct inode *inode)
{
	return (inode->i_size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
}

static unsigned exofs_last_byte(struct inode *inode, unsigned long page_nr)
{
	loff_t last_byte = inode->i_size;

	last_byte -= page_nr << PAGE_CACHE_SHIFT;
	if (last_byte > PAGE_CACHE_SIZE)
		last_byte = PAGE_CACHE_SIZE;
	return last_byte;
}

static int exofs_commit_chunk(struct page *page, loff_t pos, unsigned len)
{
	struct address_space *mapping = page->mapping;
	struct inode *dir = mapping->host;
	int err = 0;

	dir->i_version++;

	if (!PageUptodate(page))
		SetPageUptodate(page);

	if (pos+len > dir->i_size) {
		i_size_write(dir, pos+len);
		mark_inode_dirty(dir);
	}
	set_page_dirty(page);

	if (IS_DIRSYNC(dir))
		err = write_one_page(page, 1);
	else
		unlock_page(page);

	return err;
}

static void exofs_check_page(struct page *page)
{
	struct inode *dir = page->mapping->host;
	unsigned chunk_size = exofs_chunk_size(dir);
	char *kaddr = page_address(page);
	unsigned offs, rec_len;
	unsigned limit = PAGE_CACHE_SIZE;
	struct exofs_dir_entry *p;
	char *error;

	/* if the page is the last one in the directory */
	if ((dir->i_size >> PAGE_CACHE_SHIFT) == page->index) {
		limit = dir->i_size & ~PAGE_CACHE_MASK;
		if (limit & (chunk_size - 1))
			goto Ebadsize;
		if (!limit)
			goto out;
	}
	for (offs = 0; offs <= limit - EXOFS_DIR_REC_LEN(1); offs += rec_len) {
		p = (struct exofs_dir_entry *)(kaddr + offs);
		rec_len = le16_to_cpu(p->rec_len);

		if (rec_len < EXOFS_DIR_REC_LEN(1))
			goto Eshort;
		if (rec_len & 3)
			goto Ealign;
		if (rec_len < EXOFS_DIR_REC_LEN(p->name_len))
			goto Enamelen;
		if (((offs + rec_len - 1) ^ offs) & ~(chunk_size-1))
			goto Espan;
	}
	if (offs != limit)
		goto Eend;
out:
	SetPageChecked(page);
	return;

Ebadsize:
	EXOFS_ERR("ERROR [exofs_check_page]: "
		"size of directory(0x%lx) is not a multiple of chunk size\n",
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
bad_entry:
	EXOFS_ERR(
		"ERROR [exofs_check_page]: bad entry in directory(0x%lx): %s - "
		"offset=%lu, inode=0x%llu, rec_len=%d, name_len=%d\n",
		dir->i_ino, error, (page->index<<PAGE_CACHE_SHIFT)+offs,
		_LLU(le64_to_cpu(p->inode_no)),
		rec_len, p->name_len);
	goto fail;
Eend:
	p = (struct exofs_dir_entry *)(kaddr + offs);
	EXOFS_ERR("ERROR [exofs_check_page]: "
		"entry in directory(0x%lx) spans the page boundary"
		"offset=%lu, inode=0x%llx\n",
		dir->i_ino, (page->index<<PAGE_CACHE_SHIFT)+offs,
		_LLU(le64_to_cpu(p->inode_no)));
fail:
	SetPageChecked(page);
	SetPageError(page);
}

static struct page *exofs_get_page(struct inode *dir, unsigned long n)
{
	struct address_space *mapping = dir->i_mapping;
	struct page *page = read_mapping_page(mapping, n, NULL);

	if (!IS_ERR(page)) {
		kmap(page);
		if (!PageChecked(page))
			exofs_check_page(page);
		if (PageError(page))
			goto fail;
	}
	return page;

fail:
	exofs_put_page(page);
	return ERR_PTR(-EIO);
}

static inline int exofs_match(int len, const unsigned char *name,
					struct exofs_dir_entry *de)
{
	if (len != de->name_len)
		return 0;
	if (!de->inode_no)
		return 0;
	return !memcmp(name, de->name, len);
}

static inline
struct exofs_dir_entry *exofs_next_entry(struct exofs_dir_entry *p)
{
	return (struct exofs_dir_entry *)((char *)p + le16_to_cpu(p->rec_len));
}

static inline unsigned
exofs_validate_entry(char *base, unsigned offset, unsigned mask)
{
	struct exofs_dir_entry *de = (struct exofs_dir_entry *)(base + offset);
	struct exofs_dir_entry *p =
			(struct exofs_dir_entry *)(base + (offset&mask));
	while ((char *)p < (char *)de) {
		if (p->rec_len == 0)
			break;
		p = exofs_next_entry(p);
	}
	return (char *)p - base;
}

static unsigned char exofs_filetype_table[EXOFS_FT_MAX] = {
	[EXOFS_FT_UNKNOWN]	= DT_UNKNOWN,
	[EXOFS_FT_REG_FILE]	= DT_REG,
	[EXOFS_FT_DIR]		= DT_DIR,
	[EXOFS_FT_CHRDEV]	= DT_CHR,
	[EXOFS_FT_BLKDEV]	= DT_BLK,
	[EXOFS_FT_FIFO]		= DT_FIFO,
	[EXOFS_FT_SOCK]		= DT_SOCK,
	[EXOFS_FT_SYMLINK]	= DT_LNK,
};

#define S_SHIFT 12
static unsigned char exofs_type_by_mode[S_IFMT >> S_SHIFT] = {
	[S_IFREG >> S_SHIFT]	= EXOFS_FT_REG_FILE,
	[S_IFDIR >> S_SHIFT]	= EXOFS_FT_DIR,
	[S_IFCHR >> S_SHIFT]	= EXOFS_FT_CHRDEV,
	[S_IFBLK >> S_SHIFT]	= EXOFS_FT_BLKDEV,
	[S_IFIFO >> S_SHIFT]	= EXOFS_FT_FIFO,
	[S_IFSOCK >> S_SHIFT]	= EXOFS_FT_SOCK,
	[S_IFLNK >> S_SHIFT]	= EXOFS_FT_SYMLINK,
};

static inline
void exofs_set_de_type(struct exofs_dir_entry *de, struct inode *inode)
{
	umode_t mode = inode->i_mode;
	de->file_type = exofs_type_by_mode[(mode & S_IFMT) >> S_SHIFT];
}

static int
exofs_readdir(struct file *file, struct dir_context *ctx)
{
	loff_t pos = ctx->pos;
	struct inode *inode = file_inode(file);
	unsigned int offset = pos & ~PAGE_CACHE_MASK;
	unsigned long n = pos >> PAGE_CACHE_SHIFT;
	unsigned long npages = dir_pages(inode);
	unsigned chunk_mask = ~(exofs_chunk_size(inode)-1);
	int need_revalidate = (file->f_version != inode->i_version);

	if (pos > inode->i_size - EXOFS_DIR_REC_LEN(1))
		return 0;

	for ( ; n < npages; n++, offset = 0) {
		char *kaddr, *limit;
		struct exofs_dir_entry *de;
		struct page *page = exofs_get_page(inode, n);

		if (IS_ERR(page)) {
			EXOFS_ERR("ERROR: bad page in directory(0x%lx)\n",
				  inode->i_ino);
			ctx->pos += PAGE_CACHE_SIZE - offset;
			return PTR_ERR(page);
		}
		kaddr = page_address(page);
		if (unlikely(need_revalidate)) {
			if (offset) {
				offset = exofs_validate_entry(kaddr, offset,
								chunk_mask);
				ctx->pos = (n<<PAGE_CACHE_SHIFT) + offset;
			}
			file->f_version = inode->i_version;
			need_revalidate = 0;
		}
		de = (struct exofs_dir_entry *)(kaddr + offset);
		limit = kaddr + exofs_last_byte(inode, n) -
							EXOFS_DIR_REC_LEN(1);
		for (; (char *)de <= limit; de = exofs_next_entry(de)) {
			if (de->rec_len == 0) {
				EXOFS_ERR("ERROR: "
				     "zero-length entry in directory(0x%lx)\n",
				     inode->i_ino);
				exofs_put_page(page);
				return -EIO;
			}
			if (de->inode_no) {
				unsigned char t;

				if (de->file_type < EXOFS_FT_MAX)
					t = exofs_filetype_table[de->file_type];
				else
					t = DT_UNKNOWN;

				if (!dir_emit(ctx, de->name, de->name_len,
						le64_to_cpu(de->inode_no),
						t)) {
					exofs_put_page(page);
					return 0;
				}
			}
			ctx->pos += le16_to_cpu(de->rec_len);
		}
		exofs_put_page(page);
	}
	return 0;
}

struct exofs_dir_entry *exofs_find_entry(struct inode *dir,
			struct dentry *dentry, struct page **res_page)
{
	const unsigned char *name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	unsigned reclen = EXOFS_DIR_REC_LEN(namelen);
	unsigned long start, n;
	unsigned long npages = dir_pages(dir);
	struct page *page = NULL;
	struct exofs_i_info *oi = exofs_i(dir);
	struct exofs_dir_entry *de;

	if (npages == 0)
		goto out;

	*res_page = NULL;

	start = oi->i_dir_start_lookup;
	if (start >= npages)
		start = 0;
	n = start;
	do {
		char *kaddr;
		page = exofs_get_page(dir, n);
		if (!IS_ERR(page)) {
			kaddr = page_address(page);
			de = (struct exofs_dir_entry *) kaddr;
			kaddr += exofs_last_byte(dir, n) - reclen;
			while ((char *) de <= kaddr) {
				if (de->rec_len == 0) {
					EXOFS_ERR("ERROR: zero-length entry in "
						  "directory(0x%lx)\n",
						  dir->i_ino);
					exofs_put_page(page);
					goto out;
				}
				if (exofs_match(namelen, name, de))
					goto found;
				de = exofs_next_entry(de);
			}
			exofs_put_page(page);
		}
		if (++n >= npages)
			n = 0;
	} while (n != start);
out:
	return NULL;

found:
	*res_page = page;
	oi->i_dir_start_lookup = n;
	return de;
}

struct exofs_dir_entry *exofs_dotdot(struct inode *dir, struct page **p)
{
	struct page *page = exofs_get_page(dir, 0);
	struct exofs_dir_entry *de = NULL;

	if (!IS_ERR(page)) {
		de = exofs_next_entry(
				(struct exofs_dir_entry *)page_address(page));
		*p = page;
	}
	return de;
}

ino_t exofs_parent_ino(struct dentry *child)
{
	struct page *page;
	struct exofs_dir_entry *de;
	ino_t ino;

	de = exofs_dotdot(d_inode(child), &page);
	if (!de)
		return 0;

	ino = le64_to_cpu(de->inode_no);
	exofs_put_page(page);
	return ino;
}

ino_t exofs_inode_by_name(struct inode *dir, struct dentry *dentry)
{
	ino_t res = 0;
	struct exofs_dir_entry *de;
	struct page *page;

	de = exofs_find_entry(dir, dentry, &page);
	if (de) {
		res = le64_to_cpu(de->inode_no);
		exofs_put_page(page);
	}
	return res;
}

int exofs_set_link(struct inode *dir, struct exofs_dir_entry *de,
			struct page *page, struct inode *inode)
{
	loff_t pos = page_offset(page) +
			(char *) de - (char *) page_address(page);
	unsigned len = le16_to_cpu(de->rec_len);
	int err;

	lock_page(page);
	err = exofs_write_begin(NULL, page->mapping, pos, len,
				AOP_FLAG_UNINTERRUPTIBLE, &page, NULL);
	if (err)
		EXOFS_ERR("exofs_set_link: exofs_write_begin FAILED => %d\n",
			  err);

	de->inode_no = cpu_to_le64(inode->i_ino);
	exofs_set_de_type(de, inode);
	if (likely(!err))
		err = exofs_commit_chunk(page, pos, len);
	exofs_put_page(page);
	dir->i_mtime = dir->i_ctime = CURRENT_TIME;
	mark_inode_dirty(dir);
	return err;
}

int exofs_add_link(struct dentry *dentry, struct inode *inode)
{
	struct inode *dir = d_inode(dentry->d_parent);
	const unsigned char *name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	unsigned chunk_size = exofs_chunk_size(dir);
	unsigned reclen = EXOFS_DIR_REC_LEN(namelen);
	unsigned short rec_len, name_len;
	struct page *page = NULL;
	struct exofs_sb_info *sbi = inode->i_sb->s_fs_info;
	struct exofs_dir_entry *de;
	unsigned long npages = dir_pages(dir);
	unsigned long n;
	char *kaddr;
	loff_t pos;
	int err;

	for (n = 0; n <= npages; n++) {
		char *dir_end;

		page = exofs_get_page(dir, n);
		err = PTR_ERR(page);
		if (IS_ERR(page))
			goto out;
		lock_page(page);
		kaddr = page_address(page);
		dir_end = kaddr + exofs_last_byte(dir, n);
		de = (struct exofs_dir_entry *)kaddr;
		kaddr += PAGE_CACHE_SIZE - reclen;
		while ((char *)de <= kaddr) {
			if ((char *)de == dir_end) {
				name_len = 0;
				rec_len = chunk_size;
				de->rec_len = cpu_to_le16(chunk_size);
				de->inode_no = 0;
				goto got_it;
			}
			if (de->rec_len == 0) {
				EXOFS_ERR("ERROR: exofs_add_link: "
				      "zero-length entry in directory(0x%lx)\n",
				      inode->i_ino);
				err = -EIO;
				goto out_unlock;
			}
			err = -EEXIST;
			if (exofs_match(namelen, name, de))
				goto out_unlock;
			name_len = EXOFS_DIR_REC_LEN(de->name_len);
			rec_len = le16_to_cpu(de->rec_len);
			if (!de->inode_no && rec_len >= reclen)
				goto got_it;
			if (rec_len >= name_len + reclen)
				goto got_it;
			de = (struct exofs_dir_entry *) ((char *) de + rec_len);
		}
		unlock_page(page);
		exofs_put_page(page);
	}

	EXOFS_ERR("exofs_add_link: BAD dentry=%p or inode=0x%lx\n",
		  dentry, inode->i_ino);
	return -EINVAL;

got_it:
	pos = page_offset(page) +
		(char *)de - (char *)page_address(page);
	err = exofs_write_begin(NULL, page->mapping, pos, rec_len, 0,
							&page, NULL);
	if (err)
		goto out_unlock;
	if (de->inode_no) {
		struct exofs_dir_entry *de1 =
			(struct exofs_dir_entry *)((char *)de + name_len);
		de1->rec_len = cpu_to_le16(rec_len - name_len);
		de->rec_len = cpu_to_le16(name_len);
		de = de1;
	}
	de->name_len = namelen;
	memcpy(de->name, name, namelen);
	de->inode_no = cpu_to_le64(inode->i_ino);
	exofs_set_de_type(de, inode);
	err = exofs_commit_chunk(page, pos, rec_len);
	dir->i_mtime = dir->i_ctime = CURRENT_TIME;
	mark_inode_dirty(dir);
	sbi->s_numfiles++;

out_put:
	exofs_put_page(page);
out:
	return err;
out_unlock:
	unlock_page(page);
	goto out_put;
}

int exofs_delete_entry(struct exofs_dir_entry *dir, struct page *page)
{
	struct address_space *mapping = page->mapping;
	struct inode *inode = mapping->host;
	struct exofs_sb_info *sbi = inode->i_sb->s_fs_info;
	char *kaddr = page_address(page);
	unsigned from = ((char *)dir - kaddr) & ~(exofs_chunk_size(inode)-1);
	unsigned to = ((char *)dir - kaddr) + le16_to_cpu(dir->rec_len);
	loff_t pos;
	struct exofs_dir_entry *pde = NULL;
	struct exofs_dir_entry *de = (struct exofs_dir_entry *) (kaddr + from);
	int err;

	while (de < dir) {
		if (de->rec_len == 0) {
			EXOFS_ERR("ERROR: exofs_delete_entry:"
				  "zero-length entry in directory(0x%lx)\n",
				  inode->i_ino);
			err = -EIO;
			goto out;
		}
		pde = de;
		de = exofs_next_entry(de);
	}
	if (pde)
		from = (char *)pde - (char *)page_address(page);
	pos = page_offset(page) + from;
	lock_page(page);
	err = exofs_write_begin(NULL, page->mapping, pos, to - from, 0,
							&page, NULL);
	if (err)
		EXOFS_ERR("exofs_delete_entry: exofs_write_begin FAILED => %d\n",
			  err);
	if (pde)
		pde->rec_len = cpu_to_le16(to - from);
	dir->inode_no = 0;
	if (likely(!err))
		err = exofs_commit_chunk(page, pos, to - from);
	inode->i_ctime = inode->i_mtime = CURRENT_TIME;
	mark_inode_dirty(inode);
	sbi->s_numfiles--;
out:
	exofs_put_page(page);
	return err;
}

/* kept aligned on 4 bytes */
#define THIS_DIR ".\0\0"
#define PARENT_DIR "..\0"

int exofs_make_empty(struct inode *inode, struct inode *parent)
{
	struct address_space *mapping = inode->i_mapping;
	struct page *page = grab_cache_page(mapping, 0);
	unsigned chunk_size = exofs_chunk_size(inode);
	struct exofs_dir_entry *de;
	int err;
	void *kaddr;

	if (!page)
		return -ENOMEM;

	err = exofs_write_begin(NULL, page->mapping, 0, chunk_size, 0,
							&page, NULL);
	if (err) {
		unlock_page(page);
		goto fail;
	}

	kaddr = kmap_atomic(page);
	de = (struct exofs_dir_entry *)kaddr;
	de->name_len = 1;
	de->rec_len = cpu_to_le16(EXOFS_DIR_REC_LEN(1));
	memcpy(de->name, THIS_DIR, sizeof(THIS_DIR));
	de->inode_no = cpu_to_le64(inode->i_ino);
	exofs_set_de_type(de, inode);

	de = (struct exofs_dir_entry *)(kaddr + EXOFS_DIR_REC_LEN(1));
	de->name_len = 2;
	de->rec_len = cpu_to_le16(chunk_size - EXOFS_DIR_REC_LEN(1));
	de->inode_no = cpu_to_le64(parent->i_ino);
	memcpy(de->name, PARENT_DIR, sizeof(PARENT_DIR));
	exofs_set_de_type(de, inode);
	kunmap_atomic(kaddr);
	err = exofs_commit_chunk(page, 0, chunk_size);
fail:
	page_cache_release(page);
	return err;
}

int exofs_empty_dir(struct inode *inode)
{
	struct page *page = NULL;
	unsigned long i, npages = dir_pages(inode);

	for (i = 0; i < npages; i++) {
		char *kaddr;
		struct exofs_dir_entry *de;
		page = exofs_get_page(inode, i);

		if (IS_ERR(page))
			continue;

		kaddr = page_address(page);
		de = (struct exofs_dir_entry *)kaddr;
		kaddr += exofs_last_byte(inode, i) - EXOFS_DIR_REC_LEN(1);

		while ((char *)de <= kaddr) {
			if (de->rec_len == 0) {
				EXOFS_ERR("ERROR: exofs_empty_dir: "
					  "zero-length directory entry"
					  "kaddr=%p, de=%p\n", kaddr, de);
				goto not_empty;
			}
			if (de->inode_no != 0) {
				/* check for . and .. */
				if (de->name[0] != '.')
					goto not_empty;
				if (de->name_len > 2)
					goto not_empty;
				if (de->name_len < 2) {
					if (le64_to_cpu(de->inode_no) !=
					    inode->i_ino)
						goto not_empty;
				} else if (de->name[1] != '.')
					goto not_empty;
			}
			de = exofs_next_entry(de);
		}
		exofs_put_page(page);
	}
	return 1;

not_empty:
	exofs_put_page(page);
	return 0;
}

const struct file_operations exofs_dir_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate	= exofs_readdir,
};
