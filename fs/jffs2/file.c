/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright © 2001-2007 Red Hat, Inc.
 * Copyright © 2004-2010 David Woodhouse <dwmw2@infradead.org>
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/time.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/crc32.h>
#include <linux/jffs2.h>
#include "nodelist.h"

static int jffs2_write_end(struct file *filp, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *pg, void *fsdata);
static int jffs2_write_begin(struct file *filp, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata);
static int jffs2_readpage (struct file *filp, struct page *pg);

int jffs2_fsync(struct file *filp, loff_t start, loff_t end, int datasync)
{
	struct inode *inode = filp->f_mapping->host;
	struct jffs2_sb_info *c = JFFS2_SB_INFO(inode->i_sb);
	int ret;

	ret = file_write_and_wait_range(filp, start, end);
	if (ret)
		return ret;

	inode_lock(inode);
	/* Trigger GC to flush any pending writes for this inode */
	jffs2_flush_wbuf_gc(c, inode->i_ino);
	inode_unlock(inode);

	return 0;
}

const struct file_operations jffs2_file_operations =
{
	.llseek =	generic_file_llseek,
	.open =		generic_file_open,
 	.read_iter =	generic_file_read_iter,
 	.write_iter =	generic_file_write_iter,
	.unlocked_ioctl=jffs2_ioctl,
	.mmap =		generic_file_readonly_mmap,
	.fsync =	jffs2_fsync,
	.splice_read =	generic_file_splice_read,
	.splice_write = iter_file_splice_write,
};

/* jffs2_file_inode_operations */

const struct inode_operations jffs2_file_inode_operations =
{
	.get_acl =	jffs2_get_acl,
	.set_acl =	jffs2_set_acl,
	.setattr =	jffs2_setattr,
	.listxattr =	jffs2_listxattr,
};

const struct address_space_operations jffs2_file_address_operations =
{
	.readpage =	jffs2_readpage,
	.write_begin =	jffs2_write_begin,
	.write_end =	jffs2_write_end,
};

static int jffs2_do_readpage_nolock (struct inode *inode, struct page *pg)
{
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(inode);
	struct jffs2_sb_info *c = JFFS2_SB_INFO(inode->i_sb);
	unsigned char *pg_buf;
	int ret;

	jffs2_dbg(2, "%s(): ino #%lu, page at offset 0x%lx\n",
		  __func__, inode->i_ino, pg->index << PAGE_SHIFT);

	BUG_ON(!PageLocked(pg));

	pg_buf = kmap(pg);
	/* FIXME: Can kmap fail? */

	ret = jffs2_read_inode_range(c, f, pg_buf, pg->index << PAGE_SHIFT,
				     PAGE_SIZE);

	if (ret) {
		ClearPageUptodate(pg);
		SetPageError(pg);
	} else {
		SetPageUptodate(pg);
		ClearPageError(pg);
	}

	flush_dcache_page(pg);
	kunmap(pg);

	jffs2_dbg(2, "readpage finished\n");
	return ret;
}

int jffs2_do_readpage_unlock(void *data, struct page *pg)
{
	int ret = jffs2_do_readpage_nolock(data, pg);
	unlock_page(pg);
	return ret;
}


static int jffs2_readpage (struct file *filp, struct page *pg)
{
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(pg->mapping->host);
	int ret;

	mutex_lock(&f->sem);
	ret = jffs2_do_readpage_unlock(pg->mapping->host, pg);
	mutex_unlock(&f->sem);
	return ret;
}

static int jffs2_write_begin(struct file *filp, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	struct page *pg;
	struct inode *inode = mapping->host;
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(inode);
	struct jffs2_sb_info *c = JFFS2_SB_INFO(inode->i_sb);
	pgoff_t index = pos >> PAGE_SHIFT;
	uint32_t pageofs = index << PAGE_SHIFT;
	int ret = 0;

	jffs2_dbg(1, "%s()\n", __func__);

	if (pageofs > inode->i_size) {
		/* Make new hole frag from old EOF to new page */
		struct jffs2_raw_inode ri;
		struct jffs2_full_dnode *fn;
		uint32_t alloc_len;

		jffs2_dbg(1, "Writing new hole frag 0x%x-0x%x between current EOF and new page\n",
			  (unsigned int)inode->i_size, pageofs);

		ret = jffs2_reserve_space(c, sizeof(ri), &alloc_len,
					  ALLOC_NORMAL, JFFS2_SUMMARY_INODE_SIZE);
		if (ret)
			goto out_err;

		mutex_lock(&f->sem);
		memset(&ri, 0, sizeof(ri));

		ri.magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
		ri.nodetype = cpu_to_je16(JFFS2_NODETYPE_INODE);
		ri.totlen = cpu_to_je32(sizeof(ri));
		ri.hdr_crc = cpu_to_je32(crc32(0, &ri, sizeof(struct jffs2_unknown_node)-4));

		ri.ino = cpu_to_je32(f->inocache->ino);
		ri.version = cpu_to_je32(++f->highest_version);
		ri.mode = cpu_to_jemode(inode->i_mode);
		ri.uid = cpu_to_je16(i_uid_read(inode));
		ri.gid = cpu_to_je16(i_gid_read(inode));
		ri.isize = cpu_to_je32(max((uint32_t)inode->i_size, pageofs));
		ri.atime = ri.ctime = ri.mtime = cpu_to_je32(JFFS2_NOW());
		ri.offset = cpu_to_je32(inode->i_size);
		ri.dsize = cpu_to_je32(pageofs - inode->i_size);
		ri.csize = cpu_to_je32(0);
		ri.compr = JFFS2_COMPR_ZERO;
		ri.node_crc = cpu_to_je32(crc32(0, &ri, sizeof(ri)-8));
		ri.data_crc = cpu_to_je32(0);

		fn = jffs2_write_dnode(c, f, &ri, NULL, 0, ALLOC_NORMAL);

		if (IS_ERR(fn)) {
			ret = PTR_ERR(fn);
			jffs2_complete_reservation(c);
			mutex_unlock(&f->sem);
			goto out_err;
		}
		ret = jffs2_add_full_dnode_to_inode(c, f, fn);
		if (f->metadata) {
			jffs2_mark_node_obsolete(c, f->metadata->raw);
			jffs2_free_full_dnode(f->metadata);
			f->metadata = NULL;
		}
		if (ret) {
			jffs2_dbg(1, "Eep. add_full_dnode_to_inode() failed in write_begin, returned %d\n",
				  ret);
			jffs2_mark_node_obsolete(c, fn->raw);
			jffs2_free_full_dnode(fn);
			jffs2_complete_reservation(c);
			mutex_unlock(&f->sem);
			goto out_err;
		}
		jffs2_complete_reservation(c);
		inode->i_size = pageofs;
		mutex_unlock(&f->sem);
	}

	/*
	 * While getting a page and reading data in, lock c->alloc_sem until
	 * the page is Uptodate. Otherwise GC task may attempt to read the same
	 * page in read_cache_page(), which causes a deadlock.
	 */
	mutex_lock(&c->alloc_sem);
	pg = grab_cache_page_write_begin(mapping, index, flags);
	if (!pg) {
		ret = -ENOMEM;
		goto release_sem;
	}
	*pagep = pg;

	/*
	 * Read in the page if it wasn't already present. Cannot optimize away
	 * the whole page write case until jffs2_write_end can handle the
	 * case of a short-copy.
	 */
	if (!PageUptodate(pg)) {
		mutex_lock(&f->sem);
		ret = jffs2_do_readpage_nolock(inode, pg);
		mutex_unlock(&f->sem);
		if (ret) {
			unlock_page(pg);
			put_page(pg);
			goto release_sem;
		}
	}
	jffs2_dbg(1, "end write_begin(). pg->flags %lx\n", pg->flags);

release_sem:
	mutex_unlock(&c->alloc_sem);
out_err:
	return ret;
}

static int jffs2_write_end(struct file *filp, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *pg, void *fsdata)
{
	/* Actually commit the write from the page cache page we're looking at.
	 * For now, we write the full page out each time. It sucks, but it's simple
	 */
	struct inode *inode = mapping->host;
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(inode);
	struct jffs2_sb_info *c = JFFS2_SB_INFO(inode->i_sb);
	struct jffs2_raw_inode *ri;
	unsigned start = pos & (PAGE_SIZE - 1);
	unsigned end = start + copied;
	unsigned aligned_start = start & ~3;
	int ret = 0;
	uint32_t writtenlen = 0;

	jffs2_dbg(1, "%s(): ino #%lu, page at 0x%lx, range %d-%d, flags %lx\n",
		  __func__, inode->i_ino, pg->index << PAGE_SHIFT,
		  start, end, pg->flags);

	/* We need to avoid deadlock with page_cache_read() in
	   jffs2_garbage_collect_pass(). So the page must be
	   up to date to prevent page_cache_read() from trying
	   to re-lock it. */
	BUG_ON(!PageUptodate(pg));

	if (end == PAGE_SIZE) {
		/* When writing out the end of a page, write out the
		   _whole_ page. This helps to reduce the number of
		   nodes in files which have many short writes, like
		   syslog files. */
		aligned_start = 0;
	}

	ri = jffs2_alloc_raw_inode();

	if (!ri) {
		jffs2_dbg(1, "%s(): Allocation of raw inode failed\n",
			  __func__);
		unlock_page(pg);
		put_page(pg);
		return -ENOMEM;
	}

	/* Set the fields that the generic jffs2_write_inode_range() code can't find */
	ri->ino = cpu_to_je32(inode->i_ino);
	ri->mode = cpu_to_jemode(inode->i_mode);
	ri->uid = cpu_to_je16(i_uid_read(inode));
	ri->gid = cpu_to_je16(i_gid_read(inode));
	ri->isize = cpu_to_je32((uint32_t)inode->i_size);
	ri->atime = ri->ctime = ri->mtime = cpu_to_je32(JFFS2_NOW());

	/* In 2.4, it was already kmapped by generic_file_write(). Doesn't
	   hurt to do it again. The alternative is ifdefs, which are ugly. */
	kmap(pg);

	ret = jffs2_write_inode_range(c, f, ri, page_address(pg) + aligned_start,
				      (pg->index << PAGE_SHIFT) + aligned_start,
				      end - aligned_start, &writtenlen);

	kunmap(pg);

	if (ret) {
		/* There was an error writing. */
		SetPageError(pg);
	}

	/* Adjust writtenlen for the padding we did, so we don't confuse our caller */
	writtenlen -= min(writtenlen, (start - aligned_start));

	if (writtenlen) {
		if (inode->i_size < pos + writtenlen) {
			inode->i_size = pos + writtenlen;
			inode->i_blocks = (inode->i_size + 511) >> 9;

			inode->i_ctime = inode->i_mtime = ITIME(je32_to_cpu(ri->ctime));
		}
	}

	jffs2_free_raw_inode(ri);

	if (start+writtenlen < end) {
		/* generic_file_write has written more to the page cache than we've
		   actually written to the medium. Mark the page !Uptodate so that
		   it gets reread */
		jffs2_dbg(1, "%s(): Not all bytes written. Marking page !uptodate\n",
			__func__);
		SetPageError(pg);
		ClearPageUptodate(pg);
	}

	jffs2_dbg(1, "%s() returning %d\n",
		  __func__, writtenlen > 0 ? writtenlen : ret);
	unlock_page(pg);
	put_page(pg);
	return writtenlen > 0 ? writtenlen : ret;
}
