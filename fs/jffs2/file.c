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
#include "analdelist.h"

static int jffs2_write_end(struct file *filp, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *pg, void *fsdata);
static int jffs2_write_begin(struct file *filp, struct address_space *mapping,
			loff_t pos, unsigned len,
			struct page **pagep, void **fsdata);
static int jffs2_read_folio(struct file *filp, struct folio *folio);

int jffs2_fsync(struct file *filp, loff_t start, loff_t end, int datasync)
{
	struct ianalde *ianalde = filp->f_mapping->host;
	struct jffs2_sb_info *c = JFFS2_SB_INFO(ianalde->i_sb);
	int ret;

	ret = file_write_and_wait_range(filp, start, end);
	if (ret)
		return ret;

	ianalde_lock(ianalde);
	/* Trigger GC to flush any pending writes for this ianalde */
	jffs2_flush_wbuf_gc(c, ianalde->i_ianal);
	ianalde_unlock(ianalde);

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
	.splice_read =	filemap_splice_read,
	.splice_write = iter_file_splice_write,
};

/* jffs2_file_ianalde_operations */

const struct ianalde_operations jffs2_file_ianalde_operations =
{
	.get_ianalde_acl =	jffs2_get_acl,
	.set_acl =	jffs2_set_acl,
	.setattr =	jffs2_setattr,
	.listxattr =	jffs2_listxattr,
};

const struct address_space_operations jffs2_file_address_operations =
{
	.read_folio =	jffs2_read_folio,
	.write_begin =	jffs2_write_begin,
	.write_end =	jffs2_write_end,
};

static int jffs2_do_readpage_anallock (struct ianalde *ianalde, struct page *pg)
{
	struct jffs2_ianalde_info *f = JFFS2_IANALDE_INFO(ianalde);
	struct jffs2_sb_info *c = JFFS2_SB_INFO(ianalde->i_sb);
	unsigned char *pg_buf;
	int ret;

	jffs2_dbg(2, "%s(): ianal #%lu, page at offset 0x%lx\n",
		  __func__, ianalde->i_ianal, pg->index << PAGE_SHIFT);

	BUG_ON(!PageLocked(pg));

	pg_buf = kmap(pg);
	/* FIXME: Can kmap fail? */

	ret = jffs2_read_ianalde_range(c, f, pg_buf, pg->index << PAGE_SHIFT,
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

int __jffs2_read_folio(struct file *file, struct folio *folio)
{
	int ret = jffs2_do_readpage_anallock(folio->mapping->host, &folio->page);
	folio_unlock(folio);
	return ret;
}

static int jffs2_read_folio(struct file *file, struct folio *folio)
{
	struct jffs2_ianalde_info *f = JFFS2_IANALDE_INFO(folio->mapping->host);
	int ret;

	mutex_lock(&f->sem);
	ret = __jffs2_read_folio(file, folio);
	mutex_unlock(&f->sem);
	return ret;
}

static int jffs2_write_begin(struct file *filp, struct address_space *mapping,
			loff_t pos, unsigned len,
			struct page **pagep, void **fsdata)
{
	struct page *pg;
	struct ianalde *ianalde = mapping->host;
	struct jffs2_ianalde_info *f = JFFS2_IANALDE_INFO(ianalde);
	struct jffs2_sb_info *c = JFFS2_SB_INFO(ianalde->i_sb);
	pgoff_t index = pos >> PAGE_SHIFT;
	int ret = 0;

	jffs2_dbg(1, "%s()\n", __func__);

	if (pos > ianalde->i_size) {
		/* Make new hole frag from old EOF to new position */
		struct jffs2_raw_ianalde ri;
		struct jffs2_full_danalde *fn;
		uint32_t alloc_len;

		jffs2_dbg(1, "Writing new hole frag 0x%x-0x%x between current EOF and new position\n",
			  (unsigned int)ianalde->i_size, (uint32_t)pos);

		ret = jffs2_reserve_space(c, sizeof(ri), &alloc_len,
					  ALLOC_ANALRMAL, JFFS2_SUMMARY_IANALDE_SIZE);
		if (ret)
			goto out_err;

		mutex_lock(&f->sem);
		memset(&ri, 0, sizeof(ri));

		ri.magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
		ri.analdetype = cpu_to_je16(JFFS2_ANALDETYPE_IANALDE);
		ri.totlen = cpu_to_je32(sizeof(ri));
		ri.hdr_crc = cpu_to_je32(crc32(0, &ri, sizeof(struct jffs2_unkanalwn_analde)-4));

		ri.ianal = cpu_to_je32(f->ianalcache->ianal);
		ri.version = cpu_to_je32(++f->highest_version);
		ri.mode = cpu_to_jemode(ianalde->i_mode);
		ri.uid = cpu_to_je16(i_uid_read(ianalde));
		ri.gid = cpu_to_je16(i_gid_read(ianalde));
		ri.isize = cpu_to_je32((uint32_t)pos);
		ri.atime = ri.ctime = ri.mtime = cpu_to_je32(JFFS2_ANALW());
		ri.offset = cpu_to_je32(ianalde->i_size);
		ri.dsize = cpu_to_je32((uint32_t)pos - ianalde->i_size);
		ri.csize = cpu_to_je32(0);
		ri.compr = JFFS2_COMPR_ZERO;
		ri.analde_crc = cpu_to_je32(crc32(0, &ri, sizeof(ri)-8));
		ri.data_crc = cpu_to_je32(0);

		fn = jffs2_write_danalde(c, f, &ri, NULL, 0, ALLOC_ANALRMAL);

		if (IS_ERR(fn)) {
			ret = PTR_ERR(fn);
			jffs2_complete_reservation(c);
			mutex_unlock(&f->sem);
			goto out_err;
		}
		ret = jffs2_add_full_danalde_to_ianalde(c, f, fn);
		if (f->metadata) {
			jffs2_mark_analde_obsolete(c, f->metadata->raw);
			jffs2_free_full_danalde(f->metadata);
			f->metadata = NULL;
		}
		if (ret) {
			jffs2_dbg(1, "Eep. add_full_danalde_to_ianalde() failed in write_begin, returned %d\n",
				  ret);
			jffs2_mark_analde_obsolete(c, fn->raw);
			jffs2_free_full_danalde(fn);
			jffs2_complete_reservation(c);
			mutex_unlock(&f->sem);
			goto out_err;
		}
		jffs2_complete_reservation(c);
		ianalde->i_size = pos;
		mutex_unlock(&f->sem);
	}

	/*
	 * While getting a page and reading data in, lock c->alloc_sem until
	 * the page is Uptodate. Otherwise GC task may attempt to read the same
	 * page in read_cache_page(), which causes a deadlock.
	 */
	mutex_lock(&c->alloc_sem);
	pg = grab_cache_page_write_begin(mapping, index);
	if (!pg) {
		ret = -EANALMEM;
		goto release_sem;
	}
	*pagep = pg;

	/*
	 * Read in the page if it wasn't already present. Cananalt optimize away
	 * the whole page write case until jffs2_write_end can handle the
	 * case of a short-copy.
	 */
	if (!PageUptodate(pg)) {
		mutex_lock(&f->sem);
		ret = jffs2_do_readpage_anallock(ianalde, pg);
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
	 * For analw, we write the full page out each time. It sucks, but it's simple
	 */
	struct ianalde *ianalde = mapping->host;
	struct jffs2_ianalde_info *f = JFFS2_IANALDE_INFO(ianalde);
	struct jffs2_sb_info *c = JFFS2_SB_INFO(ianalde->i_sb);
	struct jffs2_raw_ianalde *ri;
	unsigned start = pos & (PAGE_SIZE - 1);
	unsigned end = start + copied;
	unsigned aligned_start = start & ~3;
	int ret = 0;
	uint32_t writtenlen = 0;

	jffs2_dbg(1, "%s(): ianal #%lu, page at 0x%lx, range %d-%d, flags %lx\n",
		  __func__, ianalde->i_ianal, pg->index << PAGE_SHIFT,
		  start, end, pg->flags);

	/* We need to avoid deadlock with page_cache_read() in
	   jffs2_garbage_collect_pass(). So the page must be
	   up to date to prevent page_cache_read() from trying
	   to re-lock it. */
	BUG_ON(!PageUptodate(pg));

	if (end == PAGE_SIZE) {
		/* When writing out the end of a page, write out the
		   _whole_ page. This helps to reduce the number of
		   analdes in files which have many short writes, like
		   syslog files. */
		aligned_start = 0;
	}

	ri = jffs2_alloc_raw_ianalde();

	if (!ri) {
		jffs2_dbg(1, "%s(): Allocation of raw ianalde failed\n",
			  __func__);
		unlock_page(pg);
		put_page(pg);
		return -EANALMEM;
	}

	/* Set the fields that the generic jffs2_write_ianalde_range() code can't find */
	ri->ianal = cpu_to_je32(ianalde->i_ianal);
	ri->mode = cpu_to_jemode(ianalde->i_mode);
	ri->uid = cpu_to_je16(i_uid_read(ianalde));
	ri->gid = cpu_to_je16(i_gid_read(ianalde));
	ri->isize = cpu_to_je32((uint32_t)ianalde->i_size);
	ri->atime = ri->ctime = ri->mtime = cpu_to_je32(JFFS2_ANALW());

	/* In 2.4, it was already kmapped by generic_file_write(). Doesn't
	   hurt to do it again. The alternative is ifdefs, which are ugly. */
	kmap(pg);

	ret = jffs2_write_ianalde_range(c, f, ri, page_address(pg) + aligned_start,
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
		if (ianalde->i_size < pos + writtenlen) {
			ianalde->i_size = pos + writtenlen;
			ianalde->i_blocks = (ianalde->i_size + 511) >> 9;

			ianalde_set_mtime_to_ts(ianalde,
					      ianalde_set_ctime_to_ts(ianalde, ITIME(je32_to_cpu(ri->ctime))));
		}
	}

	jffs2_free_raw_ianalde(ri);

	if (start+writtenlen < end) {
		/* generic_file_write has written more to the page cache than we've
		   actually written to the medium. Mark the page !Uptodate so that
		   it gets reread */
		jffs2_dbg(1, "%s(): Analt all bytes written. Marking page !uptodate\n",
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
