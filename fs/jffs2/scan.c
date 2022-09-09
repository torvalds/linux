/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright © 2001-2007 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/pagemap.h>
#include <linux/crc32.h>
#include <linux/compiler.h>
#include "nodelist.h"
#include "summary.h"
#include "debug.h"

#define DEFAULT_EMPTY_SCAN_SIZE 256

#define noisy_printk(noise, fmt, ...)					\
do {									\
	if (*(noise)) {							\
		pr_notice(fmt, ##__VA_ARGS__);				\
		(*(noise))--;						\
		if (!(*(noise)))					\
			pr_notice("Further such events for this erase block will not be printed\n"); \
	}								\
} while (0)

static uint32_t pseudo_random;

static int jffs2_scan_eraseblock (struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
				  unsigned char *buf, uint32_t buf_size, struct jffs2_summary *s);

/* These helper functions _must_ increase ofs and also do the dirty/used space accounting.
 * Returning an error will abort the mount - bad checksums etc. should just mark the space
 * as dirty.
 */
static int jffs2_scan_inode_node(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
				 struct jffs2_raw_inode *ri, uint32_t ofs, struct jffs2_summary *s);
static int jffs2_scan_dirent_node(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
				 struct jffs2_raw_dirent *rd, uint32_t ofs, struct jffs2_summary *s);

static inline int min_free(struct jffs2_sb_info *c)
{
	uint32_t min = 2 * sizeof(struct jffs2_raw_inode);
#ifdef CONFIG_JFFS2_FS_WRITEBUFFER
	if (!jffs2_can_mark_obsolete(c) && min < c->wbuf_pagesize)
		return c->wbuf_pagesize;
#endif
	return min;

}

static inline uint32_t EMPTY_SCAN_SIZE(uint32_t sector_size) {
	if (sector_size < DEFAULT_EMPTY_SCAN_SIZE)
		return sector_size;
	else
		return DEFAULT_EMPTY_SCAN_SIZE;
}

static int file_dirty(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb)
{
	int ret;

	if ((ret = jffs2_prealloc_raw_node_refs(c, jeb, 1)))
		return ret;
	if ((ret = jffs2_scan_dirty_space(c, jeb, jeb->free_size)))
		return ret;
	/* Turned wasted size into dirty, since we apparently 
	   think it's recoverable now. */
	jeb->dirty_size += jeb->wasted_size;
	c->dirty_size += jeb->wasted_size;
	c->wasted_size -= jeb->wasted_size;
	jeb->wasted_size = 0;
	if (VERYDIRTY(c, jeb->dirty_size)) {
		list_add(&jeb->list, &c->very_dirty_list);
	} else {
		list_add(&jeb->list, &c->dirty_list);
	}
	return 0;
}

int jffs2_scan_medium(struct jffs2_sb_info *c)
{
	int i, ret;
	uint32_t empty_blocks = 0, bad_blocks = 0;
	unsigned char *flashbuf = NULL;
	uint32_t buf_size = 0;
	struct jffs2_summary *s = NULL; /* summary info collected by the scan process */
#ifndef __ECOS
	size_t pointlen, try_size;

	ret = mtd_point(c->mtd, 0, c->mtd->size, &pointlen,
			(void **)&flashbuf, NULL);
	if (!ret && pointlen < c->mtd->size) {
		/* Don't muck about if it won't let us point to the whole flash */
		jffs2_dbg(1, "MTD point returned len too short: 0x%zx\n",
			  pointlen);
		mtd_unpoint(c->mtd, 0, pointlen);
		flashbuf = NULL;
	}
	if (ret && ret != -EOPNOTSUPP)
		jffs2_dbg(1, "MTD point failed %d\n", ret);
#endif
	if (!flashbuf) {
		/* For NAND it's quicker to read a whole eraseblock at a time,
		   apparently */
		if (jffs2_cleanmarker_oob(c))
			try_size = c->sector_size;
		else
			try_size = PAGE_SIZE;

		jffs2_dbg(1, "Trying to allocate readbuf of %zu "
			  "bytes\n", try_size);

		flashbuf = mtd_kmalloc_up_to(c->mtd, &try_size);
		if (!flashbuf)
			return -ENOMEM;

		jffs2_dbg(1, "Allocated readbuf of %zu bytes\n",
			  try_size);

		buf_size = (uint32_t)try_size;
	}

	if (jffs2_sum_active()) {
		s = kzalloc(sizeof(struct jffs2_summary), GFP_KERNEL);
		if (!s) {
			JFFS2_WARNING("Can't allocate memory for summary\n");
			ret = -ENOMEM;
			goto out_buf;
		}
	}

	for (i=0; i<c->nr_blocks; i++) {
		struct jffs2_eraseblock *jeb = &c->blocks[i];

		cond_resched();

		/* reset summary info for next eraseblock scan */
		jffs2_sum_reset_collected(s);

		ret = jffs2_scan_eraseblock(c, jeb, buf_size?flashbuf:(flashbuf+jeb->offset),
						buf_size, s);

		if (ret < 0)
			goto out;

		jffs2_dbg_acct_paranoia_check_nolock(c, jeb);

		/* Now decide which list to put it on */
		switch(ret) {
		case BLK_STATE_ALLFF:
			/*
			 * Empty block.   Since we can't be sure it
			 * was entirely erased, we just queue it for erase
			 * again.  It will be marked as such when the erase
			 * is complete.  Meanwhile we still count it as empty
			 * for later checks.
			 */
			empty_blocks++;
			list_add(&jeb->list, &c->erase_pending_list);
			c->nr_erasing_blocks++;
			break;

		case BLK_STATE_CLEANMARKER:
			/* Only a CLEANMARKER node is valid */
			if (!jeb->dirty_size) {
				/* It's actually free */
				list_add(&jeb->list, &c->free_list);
				c->nr_free_blocks++;
			} else {
				/* Dirt */
				jffs2_dbg(1, "Adding all-dirty block at 0x%08x to erase_pending_list\n",
					  jeb->offset);
				list_add(&jeb->list, &c->erase_pending_list);
				c->nr_erasing_blocks++;
			}
			break;

		case BLK_STATE_CLEAN:
			/* Full (or almost full) of clean data. Clean list */
			list_add(&jeb->list, &c->clean_list);
			break;

		case BLK_STATE_PARTDIRTY:
			/* Some data, but not full. Dirty list. */
			/* We want to remember the block with most free space
			and stick it in the 'nextblock' position to start writing to it. */
			if (jeb->free_size > min_free(c) &&
					(!c->nextblock || c->nextblock->free_size < jeb->free_size)) {
				/* Better candidate for the next writes to go to */
				if (c->nextblock) {
					ret = file_dirty(c, c->nextblock);
					if (ret)
						goto out;
					/* deleting summary information of the old nextblock */
					jffs2_sum_reset_collected(c->summary);
				}
				/* update collected summary information for the current nextblock */
				jffs2_sum_move_collected(c, s);
				jffs2_dbg(1, "%s(): new nextblock = 0x%08x\n",
					  __func__, jeb->offset);
				c->nextblock = jeb;
			} else {
				ret = file_dirty(c, jeb);
				if (ret)
					goto out;
			}
			break;

		case BLK_STATE_ALLDIRTY:
			/* Nothing valid - not even a clean marker. Needs erasing. */
			/* For now we just put it on the erasing list. We'll start the erases later */
			jffs2_dbg(1, "Erase block at 0x%08x is not formatted. It will be erased\n",
				  jeb->offset);
			list_add(&jeb->list, &c->erase_pending_list);
			c->nr_erasing_blocks++;
			break;

		case BLK_STATE_BADBLOCK:
			jffs2_dbg(1, "Block at 0x%08x is bad\n", jeb->offset);
			list_add(&jeb->list, &c->bad_list);
			c->bad_size += c->sector_size;
			c->free_size -= c->sector_size;
			bad_blocks++;
			break;
		default:
			pr_warn("%s(): unknown block state\n", __func__);
			BUG();
		}
	}

	/* Nextblock dirty is always seen as wasted, because we cannot recycle it now */
	if (c->nextblock && (c->nextblock->dirty_size)) {
		c->nextblock->wasted_size += c->nextblock->dirty_size;
		c->wasted_size += c->nextblock->dirty_size;
		c->dirty_size -= c->nextblock->dirty_size;
		c->nextblock->dirty_size = 0;
	}
#ifdef CONFIG_JFFS2_FS_WRITEBUFFER
	if (!jffs2_can_mark_obsolete(c) && c->wbuf_pagesize && c->nextblock && (c->nextblock->free_size % c->wbuf_pagesize)) {
		/* If we're going to start writing into a block which already
		   contains data, and the end of the data isn't page-aligned,
		   skip a little and align it. */

		uint32_t skip = c->nextblock->free_size % c->wbuf_pagesize;

		jffs2_dbg(1, "%s(): Skipping %d bytes in nextblock to ensure page alignment\n",
			  __func__, skip);
		jffs2_prealloc_raw_node_refs(c, c->nextblock, 1);
		jffs2_scan_dirty_space(c, c->nextblock, skip);
	}
#endif
	if (c->nr_erasing_blocks) {
		if (!c->used_size && !c->unchecked_size &&
			((c->nr_free_blocks+empty_blocks+bad_blocks) != c->nr_blocks || bad_blocks == c->nr_blocks)) {
			pr_notice("Cowardly refusing to erase blocks on filesystem with no valid JFFS2 nodes\n");
			pr_notice("empty_blocks %d, bad_blocks %d, c->nr_blocks %d\n",
				  empty_blocks, bad_blocks, c->nr_blocks);
			ret = -EIO;
			goto out;
		}
		spin_lock(&c->erase_completion_lock);
		jffs2_garbage_collect_trigger(c);
		spin_unlock(&c->erase_completion_lock);
	}
	ret = 0;
 out:
	jffs2_sum_reset_collected(s);
	kfree(s);
 out_buf:
	if (buf_size)
		kfree(flashbuf);
#ifndef __ECOS
	else
		mtd_unpoint(c->mtd, 0, c->mtd->size);
#endif
	return ret;
}

static int jffs2_fill_scan_buf(struct jffs2_sb_info *c, void *buf,
			       uint32_t ofs, uint32_t len)
{
	int ret;
	size_t retlen;

	ret = jffs2_flash_read(c, ofs, len, &retlen, buf);
	if (ret) {
		jffs2_dbg(1, "mtd->read(0x%x bytes from 0x%x) returned %d\n",
			  len, ofs, ret);
		return ret;
	}
	if (retlen < len) {
		jffs2_dbg(1, "Read at 0x%x gave only 0x%zx bytes\n",
			  ofs, retlen);
		return -EIO;
	}
	return 0;
}

int jffs2_scan_classify_jeb(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb)
{
	if ((jeb->used_size + jeb->unchecked_size) == PAD(c->cleanmarker_size) && !jeb->dirty_size
	    && (!jeb->first_node || !ref_next(jeb->first_node)) )
		return BLK_STATE_CLEANMARKER;

	/* move blocks with max 4 byte dirty space to cleanlist */
	else if (!ISDIRTY(c->sector_size - (jeb->used_size + jeb->unchecked_size))) {
		c->dirty_size -= jeb->dirty_size;
		c->wasted_size += jeb->dirty_size;
		jeb->wasted_size += jeb->dirty_size;
		jeb->dirty_size = 0;
		return BLK_STATE_CLEAN;
	} else if (jeb->used_size || jeb->unchecked_size)
		return BLK_STATE_PARTDIRTY;
	else
		return BLK_STATE_ALLDIRTY;
}

#ifdef CONFIG_JFFS2_FS_XATTR
static int jffs2_scan_xattr_node(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
				 struct jffs2_raw_xattr *rx, uint32_t ofs,
				 struct jffs2_summary *s)
{
	struct jffs2_xattr_datum *xd;
	uint32_t xid, version, totlen, crc;
	int err;

	crc = crc32(0, rx, sizeof(struct jffs2_raw_xattr) - 4);
	if (crc != je32_to_cpu(rx->node_crc)) {
		JFFS2_WARNING("node CRC failed at %#08x, read=%#08x, calc=%#08x\n",
			      ofs, je32_to_cpu(rx->node_crc), crc);
		if ((err = jffs2_scan_dirty_space(c, jeb, je32_to_cpu(rx->totlen))))
			return err;
		return 0;
	}

	xid = je32_to_cpu(rx->xid);
	version = je32_to_cpu(rx->version);

	totlen = PAD(sizeof(struct jffs2_raw_xattr)
			+ rx->name_len + 1 + je16_to_cpu(rx->value_len));
	if (totlen != je32_to_cpu(rx->totlen)) {
		JFFS2_WARNING("node length mismatch at %#08x, read=%u, calc=%u\n",
			      ofs, je32_to_cpu(rx->totlen), totlen);
		if ((err = jffs2_scan_dirty_space(c, jeb, je32_to_cpu(rx->totlen))))
			return err;
		return 0;
	}

	xd = jffs2_setup_xattr_datum(c, xid, version);
	if (IS_ERR(xd))
		return PTR_ERR(xd);

	if (xd->version > version) {
		struct jffs2_raw_node_ref *raw
			= jffs2_link_node_ref(c, jeb, ofs | REF_PRISTINE, totlen, NULL);
		raw->next_in_ino = xd->node->next_in_ino;
		xd->node->next_in_ino = raw;
	} else {
		xd->version = version;
		xd->xprefix = rx->xprefix;
		xd->name_len = rx->name_len;
		xd->value_len = je16_to_cpu(rx->value_len);
		xd->data_crc = je32_to_cpu(rx->data_crc);

		jffs2_link_node_ref(c, jeb, ofs | REF_PRISTINE, totlen, (void *)xd);
	}

	if (jffs2_sum_active())
		jffs2_sum_add_xattr_mem(s, rx, ofs - jeb->offset);
	dbg_xattr("scanning xdatum at %#08x (xid=%u, version=%u)\n",
		  ofs, xd->xid, xd->version);
	return 0;
}

static int jffs2_scan_xref_node(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
				struct jffs2_raw_xref *rr, uint32_t ofs,
				struct jffs2_summary *s)
{
	struct jffs2_xattr_ref *ref;
	uint32_t crc;
	int err;

	crc = crc32(0, rr, sizeof(*rr) - 4);
	if (crc != je32_to_cpu(rr->node_crc)) {
		JFFS2_WARNING("node CRC failed at %#08x, read=%#08x, calc=%#08x\n",
			      ofs, je32_to_cpu(rr->node_crc), crc);
		if ((err = jffs2_scan_dirty_space(c, jeb, PAD(je32_to_cpu(rr->totlen)))))
			return err;
		return 0;
	}

	if (PAD(sizeof(struct jffs2_raw_xref)) != je32_to_cpu(rr->totlen)) {
		JFFS2_WARNING("node length mismatch at %#08x, read=%u, calc=%zd\n",
			      ofs, je32_to_cpu(rr->totlen),
			      PAD(sizeof(struct jffs2_raw_xref)));
		if ((err = jffs2_scan_dirty_space(c, jeb, je32_to_cpu(rr->totlen))))
			return err;
		return 0;
	}

	ref = jffs2_alloc_xattr_ref();
	if (!ref)
		return -ENOMEM;

	/* BEFORE jffs2_build_xattr_subsystem() called, 
	 * and AFTER xattr_ref is marked as a dead xref,
	 * ref->xid is used to store 32bit xid, xd is not used
	 * ref->ino is used to store 32bit inode-number, ic is not used
	 * Thoes variables are declared as union, thus using those
	 * are exclusive. In a similar way, ref->next is temporarily
	 * used to chain all xattr_ref object. It's re-chained to
	 * jffs2_inode_cache in jffs2_build_xattr_subsystem() correctly.
	 */
	ref->ino = je32_to_cpu(rr->ino);
	ref->xid = je32_to_cpu(rr->xid);
	ref->xseqno = je32_to_cpu(rr->xseqno);
	if (ref->xseqno > c->highest_xseqno)
		c->highest_xseqno = (ref->xseqno & ~XREF_DELETE_MARKER);
	ref->next = c->xref_temp;
	c->xref_temp = ref;

	jffs2_link_node_ref(c, jeb, ofs | REF_PRISTINE, PAD(je32_to_cpu(rr->totlen)), (void *)ref);

	if (jffs2_sum_active())
		jffs2_sum_add_xref_mem(s, rr, ofs - jeb->offset);
	dbg_xattr("scan xref at %#08x (xid=%u, ino=%u)\n",
		  ofs, ref->xid, ref->ino);
	return 0;
}
#endif

/* Called with 'buf_size == 0' if buf is in fact a pointer _directly_ into
   the flash, XIP-style */
static int jffs2_scan_eraseblock (struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
				  unsigned char *buf, uint32_t buf_size, struct jffs2_summary *s) {
	struct jffs2_unknown_node *node;
	struct jffs2_unknown_node crcnode;
	uint32_t ofs, prevofs, max_ofs;
	uint32_t hdr_crc, buf_ofs, buf_len;
	int err;
	int noise = 0;


#ifdef CONFIG_JFFS2_FS_WRITEBUFFER
	int cleanmarkerfound = 0;
#endif

	ofs = jeb->offset;
	prevofs = jeb->offset - 1;

	jffs2_dbg(1, "%s(): Scanning block at 0x%x\n", __func__, ofs);

#ifdef CONFIG_JFFS2_FS_WRITEBUFFER
	if (jffs2_cleanmarker_oob(c)) {
		int ret;

		if (mtd_block_isbad(c->mtd, jeb->offset))
			return BLK_STATE_BADBLOCK;

		ret = jffs2_check_nand_cleanmarker(c, jeb);
		jffs2_dbg(2, "jffs_check_nand_cleanmarker returned %d\n", ret);

		/* Even if it's not found, we still scan to see
		   if the block is empty. We use this information
		   to decide whether to erase it or not. */
		switch (ret) {
		case 0:		cleanmarkerfound = 1; break;
		case 1: 	break;
		default: 	return ret;
		}
	}
#endif

	if (jffs2_sum_active()) {
		struct jffs2_sum_marker *sm;
		void *sumptr = NULL;
		uint32_t sumlen;
	      
		if (!buf_size) {
			/* XIP case. Just look, point at the summary if it's there */
			sm = (void *)buf + c->sector_size - sizeof(*sm);
			if (je32_to_cpu(sm->magic) == JFFS2_SUM_MAGIC) {
				sumptr = buf + je32_to_cpu(sm->offset);
				sumlen = c->sector_size - je32_to_cpu(sm->offset);
			}
		} else {
			/* If NAND flash, read a whole page of it. Else just the end */
			if (c->wbuf_pagesize)
				buf_len = c->wbuf_pagesize;
			else
				buf_len = sizeof(*sm);

			/* Read as much as we want into the _end_ of the preallocated buffer */
			err = jffs2_fill_scan_buf(c, buf + buf_size - buf_len, 
						  jeb->offset + c->sector_size - buf_len,
						  buf_len);				
			if (err)
				return err;

			sm = (void *)buf + buf_size - sizeof(*sm);
			if (je32_to_cpu(sm->magic) == JFFS2_SUM_MAGIC) {
				sumlen = c->sector_size - je32_to_cpu(sm->offset);
				sumptr = buf + buf_size - sumlen;

				/* sm->offset maybe wrong but MAGIC maybe right */
				if (sumlen > c->sector_size)
					goto full_scan;

				/* Now, make sure the summary itself is available */
				if (sumlen > buf_size) {
					/* Need to kmalloc for this. */
					sumptr = kmalloc(sumlen, GFP_KERNEL);
					if (!sumptr)
						return -ENOMEM;
					memcpy(sumptr + sumlen - buf_len, buf + buf_size - buf_len, buf_len);
				}
				if (buf_len < sumlen) {
					/* Need to read more so that the entire summary node is present */
					err = jffs2_fill_scan_buf(c, sumptr, 
								  jeb->offset + c->sector_size - sumlen,
								  sumlen - buf_len);				
					if (err) {
						if (sumlen > buf_size)
							kfree(sumptr);
						return err;
					}
				}
			}

		}

		if (sumptr) {
			err = jffs2_sum_scan_sumnode(c, jeb, sumptr, sumlen, &pseudo_random);

			if (buf_size && sumlen > buf_size)
				kfree(sumptr);
			/* If it returns with a real error, bail. 
			   If it returns positive, that's a block classification
			   (i.e. BLK_STATE_xxx) so return that too.
			   If it returns zero, fall through to full scan. */
			if (err)
				return err;
		}
	}

full_scan:
	buf_ofs = jeb->offset;

	if (!buf_size) {
		/* This is the XIP case -- we're reading _directly_ from the flash chip */
		buf_len = c->sector_size;
	} else {
		buf_len = EMPTY_SCAN_SIZE(c->sector_size);
		err = jffs2_fill_scan_buf(c, buf, buf_ofs, buf_len);
		if (err)
			return err;
	}

	/* We temporarily use 'ofs' as a pointer into the buffer/jeb */
	ofs = 0;
	max_ofs = EMPTY_SCAN_SIZE(c->sector_size);
	/* Scan only EMPTY_SCAN_SIZE of 0xFF before declaring it's empty */
	while(ofs < max_ofs && *(uint32_t *)(&buf[ofs]) == 0xFFFFFFFF)
		ofs += 4;

	if (ofs == max_ofs) {
#ifdef CONFIG_JFFS2_FS_WRITEBUFFER
		if (jffs2_cleanmarker_oob(c)) {
			/* scan oob, take care of cleanmarker */
			int ret = jffs2_check_oob_empty(c, jeb, cleanmarkerfound);
			jffs2_dbg(2, "jffs2_check_oob_empty returned %d\n",
				  ret);
			switch (ret) {
			case 0:		return cleanmarkerfound ? BLK_STATE_CLEANMARKER : BLK_STATE_ALLFF;
			case 1: 	return BLK_STATE_ALLDIRTY;
			default: 	return ret;
			}
		}
#endif
		jffs2_dbg(1, "Block at 0x%08x is empty (erased)\n",
			  jeb->offset);
		if (c->cleanmarker_size == 0)
			return BLK_STATE_CLEANMARKER;	/* don't bother with re-erase */
		else
			return BLK_STATE_ALLFF;	/* OK to erase if all blocks are like this */
	}
	if (ofs) {
		jffs2_dbg(1, "Free space at %08x ends at %08x\n", jeb->offset,
			  jeb->offset + ofs);
		if ((err = jffs2_prealloc_raw_node_refs(c, jeb, 1)))
			return err;
		if ((err = jffs2_scan_dirty_space(c, jeb, ofs)))
			return err;
	}

	/* Now ofs is a complete physical flash offset as it always was... */
	ofs += jeb->offset;

	noise = 10;

	dbg_summary("no summary found in jeb 0x%08x. Apply original scan.\n",jeb->offset);

scan_more:
	while(ofs < jeb->offset + c->sector_size) {

		jffs2_dbg_acct_paranoia_check_nolock(c, jeb);

		/* Make sure there are node refs available for use */
		err = jffs2_prealloc_raw_node_refs(c, jeb, 2);
		if (err)
			return err;

		cond_resched();

		if (ofs & 3) {
			pr_warn("Eep. ofs 0x%08x not word-aligned!\n", ofs);
			ofs = PAD(ofs);
			continue;
		}
		if (ofs == prevofs) {
			pr_warn("ofs 0x%08x has already been seen. Skipping\n",
				ofs);
			if ((err = jffs2_scan_dirty_space(c, jeb, 4)))
				return err;
			ofs += 4;
			continue;
		}
		prevofs = ofs;

		if (jeb->offset + c->sector_size < ofs + sizeof(*node)) {
			jffs2_dbg(1, "Fewer than %zd bytes left to end of block. (%x+%x<%x+%zx) Not reading\n",
				  sizeof(struct jffs2_unknown_node),
				  jeb->offset, c->sector_size, ofs,
				  sizeof(*node));
			if ((err = jffs2_scan_dirty_space(c, jeb, (jeb->offset + c->sector_size)-ofs)))
				return err;
			break;
		}

		if (buf_ofs + buf_len < ofs + sizeof(*node)) {
			buf_len = min_t(uint32_t, buf_size, jeb->offset + c->sector_size - ofs);
			jffs2_dbg(1, "Fewer than %zd bytes (node header) left to end of buf. Reading 0x%x at 0x%08x\n",
				  sizeof(struct jffs2_unknown_node),
				  buf_len, ofs);
			err = jffs2_fill_scan_buf(c, buf, ofs, buf_len);
			if (err)
				return err;
			buf_ofs = ofs;
		}

		node = (struct jffs2_unknown_node *)&buf[ofs-buf_ofs];

		if (*(uint32_t *)(&buf[ofs-buf_ofs]) == 0xffffffff) {
			uint32_t inbuf_ofs;
			uint32_t empty_start, scan_end;

			empty_start = ofs;
			ofs += 4;
			scan_end = min_t(uint32_t, EMPTY_SCAN_SIZE(c->sector_size)/8, buf_len);

			jffs2_dbg(1, "Found empty flash at 0x%08x\n", ofs);
		more_empty:
			inbuf_ofs = ofs - buf_ofs;
			while (inbuf_ofs < scan_end) {
				if (unlikely(*(uint32_t *)(&buf[inbuf_ofs]) != 0xffffffff)) {
					pr_warn("Empty flash at 0x%08x ends at 0x%08x\n",
						empty_start, ofs);
					if ((err = jffs2_scan_dirty_space(c, jeb, ofs-empty_start)))
						return err;
					goto scan_more;
				}

				inbuf_ofs+=4;
				ofs += 4;
			}
			/* Ran off end. */
			jffs2_dbg(1, "Empty flash to end of buffer at 0x%08x\n",
				  ofs);

			/* If we're only checking the beginning of a block with a cleanmarker,
			   bail now */
			if (buf_ofs == jeb->offset && jeb->used_size == PAD(c->cleanmarker_size) &&
			    c->cleanmarker_size && !jeb->dirty_size && !ref_next(jeb->first_node)) {
				jffs2_dbg(1, "%d bytes at start of block seems clean... assuming all clean\n",
					  EMPTY_SCAN_SIZE(c->sector_size));
				return BLK_STATE_CLEANMARKER;
			}
			if (!buf_size && (scan_end != buf_len)) {/* XIP/point case */
				scan_end = buf_len;
				goto more_empty;
			}
			
			/* See how much more there is to read in this eraseblock... */
			buf_len = min_t(uint32_t, buf_size, jeb->offset + c->sector_size - ofs);
			if (!buf_len) {
				/* No more to read. Break out of main loop without marking
				   this range of empty space as dirty (because it's not) */
				jffs2_dbg(1, "Empty flash at %08x runs to end of block. Treating as free_space\n",
					  empty_start);
				break;
			}
			/* point never reaches here */
			scan_end = buf_len;
			jffs2_dbg(1, "Reading another 0x%x at 0x%08x\n",
				  buf_len, ofs);
			err = jffs2_fill_scan_buf(c, buf, ofs, buf_len);
			if (err)
				return err;
			buf_ofs = ofs;
			goto more_empty;
		}

		if (ofs == jeb->offset && je16_to_cpu(node->magic) == KSAMTIB_CIGAM_2SFFJ) {
			pr_warn("Magic bitmask is backwards at offset 0x%08x. Wrong endian filesystem?\n",
				ofs);
			if ((err = jffs2_scan_dirty_space(c, jeb, 4)))
				return err;
			ofs += 4;
			continue;
		}
		if (je16_to_cpu(node->magic) == JFFS2_DIRTY_BITMASK) {
			jffs2_dbg(1, "Dirty bitmask at 0x%08x\n", ofs);
			if ((err = jffs2_scan_dirty_space(c, jeb, 4)))
				return err;
			ofs += 4;
			continue;
		}
		if (je16_to_cpu(node->magic) == JFFS2_OLD_MAGIC_BITMASK) {
			pr_warn("Old JFFS2 bitmask found at 0x%08x\n", ofs);
			pr_warn("You cannot use older JFFS2 filesystems with newer kernels\n");
			if ((err = jffs2_scan_dirty_space(c, jeb, 4)))
				return err;
			ofs += 4;
			continue;
		}
		if (je16_to_cpu(node->magic) != JFFS2_MAGIC_BITMASK) {
			/* OK. We're out of possibilities. Whinge and move on */
			noisy_printk(&noise, "%s(): Magic bitmask 0x%04x not found at 0x%08x: 0x%04x instead\n",
				     __func__,
				     JFFS2_MAGIC_BITMASK, ofs,
				     je16_to_cpu(node->magic));
			if ((err = jffs2_scan_dirty_space(c, jeb, 4)))
				return err;
			ofs += 4;
			continue;
		}
		/* We seem to have a node of sorts. Check the CRC */
		crcnode.magic = node->magic;
		crcnode.nodetype = cpu_to_je16( je16_to_cpu(node->nodetype) | JFFS2_NODE_ACCURATE);
		crcnode.totlen = node->totlen;
		hdr_crc = crc32(0, &crcnode, sizeof(crcnode)-4);

		if (hdr_crc != je32_to_cpu(node->hdr_crc)) {
			noisy_printk(&noise, "%s(): Node at 0x%08x {0x%04x, 0x%04x, 0x%08x) has invalid CRC 0x%08x (calculated 0x%08x)\n",
				     __func__,
				     ofs, je16_to_cpu(node->magic),
				     je16_to_cpu(node->nodetype),
				     je32_to_cpu(node->totlen),
				     je32_to_cpu(node->hdr_crc),
				     hdr_crc);
			if ((err = jffs2_scan_dirty_space(c, jeb, 4)))
				return err;
			ofs += 4;
			continue;
		}

		if (ofs + je32_to_cpu(node->totlen) > jeb->offset + c->sector_size) {
			/* Eep. Node goes over the end of the erase block. */
			pr_warn("Node at 0x%08x with length 0x%08x would run over the end of the erase block\n",
				ofs, je32_to_cpu(node->totlen));
			pr_warn("Perhaps the file system was created with the wrong erase size?\n");
			if ((err = jffs2_scan_dirty_space(c, jeb, 4)))
				return err;
			ofs += 4;
			continue;
		}

		if (!(je16_to_cpu(node->nodetype) & JFFS2_NODE_ACCURATE)) {
			/* Wheee. This is an obsoleted node */
			jffs2_dbg(2, "Node at 0x%08x is obsolete. Skipping\n",
				  ofs);
			if ((err = jffs2_scan_dirty_space(c, jeb, PAD(je32_to_cpu(node->totlen)))))
				return err;
			ofs += PAD(je32_to_cpu(node->totlen));
			continue;
		}

		switch(je16_to_cpu(node->nodetype)) {
		case JFFS2_NODETYPE_INODE:
			if (buf_ofs + buf_len < ofs + sizeof(struct jffs2_raw_inode)) {
				buf_len = min_t(uint32_t, buf_size, jeb->offset + c->sector_size - ofs);
				jffs2_dbg(1, "Fewer than %zd bytes (inode node) left to end of buf. Reading 0x%x at 0x%08x\n",
					  sizeof(struct jffs2_raw_inode),
					  buf_len, ofs);
				err = jffs2_fill_scan_buf(c, buf, ofs, buf_len);
				if (err)
					return err;
				buf_ofs = ofs;
				node = (void *)buf;
			}
			err = jffs2_scan_inode_node(c, jeb, (void *)node, ofs, s);
			if (err) return err;
			ofs += PAD(je32_to_cpu(node->totlen));
			break;

		case JFFS2_NODETYPE_DIRENT:
			if (buf_ofs + buf_len < ofs + je32_to_cpu(node->totlen)) {
				buf_len = min_t(uint32_t, buf_size, jeb->offset + c->sector_size - ofs);
				jffs2_dbg(1, "Fewer than %d bytes (dirent node) left to end of buf. Reading 0x%x at 0x%08x\n",
					  je32_to_cpu(node->totlen), buf_len,
					  ofs);
				err = jffs2_fill_scan_buf(c, buf, ofs, buf_len);
				if (err)
					return err;
				buf_ofs = ofs;
				node = (void *)buf;
			}
			err = jffs2_scan_dirent_node(c, jeb, (void *)node, ofs, s);
			if (err) return err;
			ofs += PAD(je32_to_cpu(node->totlen));
			break;

#ifdef CONFIG_JFFS2_FS_XATTR
		case JFFS2_NODETYPE_XATTR:
			if (buf_ofs + buf_len < ofs + je32_to_cpu(node->totlen)) {
				buf_len = min_t(uint32_t, buf_size, jeb->offset + c->sector_size - ofs);
				jffs2_dbg(1, "Fewer than %d bytes (xattr node) left to end of buf. Reading 0x%x at 0x%08x\n",
					  je32_to_cpu(node->totlen), buf_len,
					  ofs);
				err = jffs2_fill_scan_buf(c, buf, ofs, buf_len);
				if (err)
					return err;
				buf_ofs = ofs;
				node = (void *)buf;
			}
			err = jffs2_scan_xattr_node(c, jeb, (void *)node, ofs, s);
			if (err)
				return err;
			ofs += PAD(je32_to_cpu(node->totlen));
			break;
		case JFFS2_NODETYPE_XREF:
			if (buf_ofs + buf_len < ofs + je32_to_cpu(node->totlen)) {
				buf_len = min_t(uint32_t, buf_size, jeb->offset + c->sector_size - ofs);
				jffs2_dbg(1, "Fewer than %d bytes (xref node) left to end of buf. Reading 0x%x at 0x%08x\n",
					  je32_to_cpu(node->totlen), buf_len,
					  ofs);
				err = jffs2_fill_scan_buf(c, buf, ofs, buf_len);
				if (err)
					return err;
				buf_ofs = ofs;
				node = (void *)buf;
			}
			err = jffs2_scan_xref_node(c, jeb, (void *)node, ofs, s);
			if (err)
				return err;
			ofs += PAD(je32_to_cpu(node->totlen));
			break;
#endif	/* CONFIG_JFFS2_FS_XATTR */

		case JFFS2_NODETYPE_CLEANMARKER:
			jffs2_dbg(1, "CLEANMARKER node found at 0x%08x\n", ofs);
			if (je32_to_cpu(node->totlen) != c->cleanmarker_size) {
				pr_notice("CLEANMARKER node found at 0x%08x has totlen 0x%x != normal 0x%x\n",
					  ofs, je32_to_cpu(node->totlen),
					  c->cleanmarker_size);
				if ((err = jffs2_scan_dirty_space(c, jeb, PAD(sizeof(struct jffs2_unknown_node)))))
					return err;
				ofs += PAD(sizeof(struct jffs2_unknown_node));
			} else if (jeb->first_node) {
				pr_notice("CLEANMARKER node found at 0x%08x, not first node in block (0x%08x)\n",
					  ofs, jeb->offset);
				if ((err = jffs2_scan_dirty_space(c, jeb, PAD(sizeof(struct jffs2_unknown_node)))))
					return err;
				ofs += PAD(sizeof(struct jffs2_unknown_node));
			} else {
				jffs2_link_node_ref(c, jeb, ofs | REF_NORMAL, c->cleanmarker_size, NULL);

				ofs += PAD(c->cleanmarker_size);
			}
			break;

		case JFFS2_NODETYPE_PADDING:
			if (jffs2_sum_active())
				jffs2_sum_add_padding_mem(s, je32_to_cpu(node->totlen));
			if ((err = jffs2_scan_dirty_space(c, jeb, PAD(je32_to_cpu(node->totlen)))))
				return err;
			ofs += PAD(je32_to_cpu(node->totlen));
			break;

		default:
			switch (je16_to_cpu(node->nodetype) & JFFS2_COMPAT_MASK) {
			case JFFS2_FEATURE_ROCOMPAT:
				pr_notice("Read-only compatible feature node (0x%04x) found at offset 0x%08x\n",
					  je16_to_cpu(node->nodetype), ofs);
				c->flags |= JFFS2_SB_FLAG_RO;
				if (!(jffs2_is_readonly(c)))
					return -EROFS;
				if ((err = jffs2_scan_dirty_space(c, jeb, PAD(je32_to_cpu(node->totlen)))))
					return err;
				ofs += PAD(je32_to_cpu(node->totlen));
				break;

			case JFFS2_FEATURE_INCOMPAT:
				pr_notice("Incompatible feature node (0x%04x) found at offset 0x%08x\n",
					  je16_to_cpu(node->nodetype), ofs);
				return -EINVAL;

			case JFFS2_FEATURE_RWCOMPAT_DELETE:
				jffs2_dbg(1, "Unknown but compatible feature node (0x%04x) found at offset 0x%08x\n",
					  je16_to_cpu(node->nodetype), ofs);
				if ((err = jffs2_scan_dirty_space(c, jeb, PAD(je32_to_cpu(node->totlen)))))
					return err;
				ofs += PAD(je32_to_cpu(node->totlen));
				break;

			case JFFS2_FEATURE_RWCOMPAT_COPY: {
				jffs2_dbg(1, "Unknown but compatible feature node (0x%04x) found at offset 0x%08x\n",
					  je16_to_cpu(node->nodetype), ofs);

				jffs2_link_node_ref(c, jeb, ofs | REF_PRISTINE, PAD(je32_to_cpu(node->totlen)), NULL);

				/* We can't summarise nodes we don't grok */
				jffs2_sum_disable_collecting(s);
				ofs += PAD(je32_to_cpu(node->totlen));
				break;
				}
			}
		}
	}

	if (jffs2_sum_active()) {
		if (PAD(s->sum_size + JFFS2_SUMMARY_FRAME_SIZE) > jeb->free_size) {
			dbg_summary("There is not enough space for "
				"summary information, disabling for this jeb!\n");
			jffs2_sum_disable_collecting(s);
		}
	}

	jffs2_dbg(1, "Block at 0x%08x: free 0x%08x, dirty 0x%08x, unchecked 0x%08x, used 0x%08x, wasted 0x%08x\n",
		  jeb->offset, jeb->free_size, jeb->dirty_size,
		  jeb->unchecked_size, jeb->used_size, jeb->wasted_size);
	
	/* mark_node_obsolete can add to wasted !! */
	if (jeb->wasted_size) {
		jeb->dirty_size += jeb->wasted_size;
		c->dirty_size += jeb->wasted_size;
		c->wasted_size -= jeb->wasted_size;
		jeb->wasted_size = 0;
	}

	return jffs2_scan_classify_jeb(c, jeb);
}

struct jffs2_inode_cache *jffs2_scan_make_ino_cache(struct jffs2_sb_info *c, uint32_t ino)
{
	struct jffs2_inode_cache *ic;

	ic = jffs2_get_ino_cache(c, ino);
	if (ic)
		return ic;

	if (ino > c->highest_ino)
		c->highest_ino = ino;

	ic = jffs2_alloc_inode_cache();
	if (!ic) {
		pr_notice("%s(): allocation of inode cache failed\n", __func__);
		return NULL;
	}
	memset(ic, 0, sizeof(*ic));

	ic->ino = ino;
	ic->nodes = (void *)ic;
	jffs2_add_ino_cache(c, ic);
	if (ino == 1)
		ic->pino_nlink = 1;
	return ic;
}

static int jffs2_scan_inode_node(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
				 struct jffs2_raw_inode *ri, uint32_t ofs, struct jffs2_summary *s)
{
	struct jffs2_inode_cache *ic;
	uint32_t crc, ino = je32_to_cpu(ri->ino);

	jffs2_dbg(1, "%s(): Node at 0x%08x\n", __func__, ofs);

	/* We do very little here now. Just check the ino# to which we should attribute
	   this node; we can do all the CRC checking etc. later. There's a tradeoff here --
	   we used to scan the flash once only, reading everything we want from it into
	   memory, then building all our in-core data structures and freeing the extra
	   information. Now we allow the first part of the mount to complete a lot quicker,
	   but we have to go _back_ to the flash in order to finish the CRC checking, etc.
	   Which means that the _full_ amount of time to get to proper write mode with GC
	   operational may actually be _longer_ than before. Sucks to be me. */

	/* Check the node CRC in any case. */
	crc = crc32(0, ri, sizeof(*ri)-8);
	if (crc != je32_to_cpu(ri->node_crc)) {
		pr_notice("%s(): CRC failed on node at 0x%08x: Read 0x%08x, calculated 0x%08x\n",
			  __func__, ofs, je32_to_cpu(ri->node_crc), crc);
		/*
		 * We believe totlen because the CRC on the node
		 * _header_ was OK, just the node itself failed.
		 */
		return jffs2_scan_dirty_space(c, jeb,
					      PAD(je32_to_cpu(ri->totlen)));
	}

	ic = jffs2_get_ino_cache(c, ino);
	if (!ic) {
		ic = jffs2_scan_make_ino_cache(c, ino);
		if (!ic)
			return -ENOMEM;
	}

	/* Wheee. It worked */
	jffs2_link_node_ref(c, jeb, ofs | REF_UNCHECKED, PAD(je32_to_cpu(ri->totlen)), ic);

	jffs2_dbg(1, "Node is ino #%u, version %d. Range 0x%x-0x%x\n",
		  je32_to_cpu(ri->ino), je32_to_cpu(ri->version),
		  je32_to_cpu(ri->offset),
		  je32_to_cpu(ri->offset)+je32_to_cpu(ri->dsize));

	pseudo_random += je32_to_cpu(ri->version);

	if (jffs2_sum_active()) {
		jffs2_sum_add_inode_mem(s, ri, ofs - jeb->offset);
	}

	return 0;
}

static int jffs2_scan_dirent_node(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
				  struct jffs2_raw_dirent *rd, uint32_t ofs, struct jffs2_summary *s)
{
	struct jffs2_full_dirent *fd;
	struct jffs2_inode_cache *ic;
	uint32_t checkedlen;
	uint32_t crc;
	int err;

	jffs2_dbg(1, "%s(): Node at 0x%08x\n", __func__, ofs);

	/* We don't get here unless the node is still valid, so we don't have to
	   mask in the ACCURATE bit any more. */
	crc = crc32(0, rd, sizeof(*rd)-8);

	if (crc != je32_to_cpu(rd->node_crc)) {
		pr_notice("%s(): Node CRC failed on node at 0x%08x: Read 0x%08x, calculated 0x%08x\n",
			  __func__, ofs, je32_to_cpu(rd->node_crc), crc);
		/* We believe totlen because the CRC on the node _header_ was OK, just the node itself failed. */
		if ((err = jffs2_scan_dirty_space(c, jeb, PAD(je32_to_cpu(rd->totlen)))))
			return err;
		return 0;
	}

	pseudo_random += je32_to_cpu(rd->version);

	/* Should never happen. Did. (OLPC trac #4184)*/
	checkedlen = strnlen(rd->name, rd->nsize);
	if (checkedlen < rd->nsize) {
		pr_err("Dirent at %08x has zeroes in name. Truncating to %d chars\n",
		       ofs, checkedlen);
	}
	fd = jffs2_alloc_full_dirent(checkedlen+1);
	if (!fd) {
		return -ENOMEM;
	}
	memcpy(&fd->name, rd->name, checkedlen);
	fd->name[checkedlen] = 0;

	crc = crc32(0, fd->name, checkedlen);
	if (crc != je32_to_cpu(rd->name_crc)) {
		pr_notice("%s(): Name CRC failed on node at 0x%08x: Read 0x%08x, calculated 0x%08x\n",
			  __func__, ofs, je32_to_cpu(rd->name_crc), crc);
		jffs2_dbg(1, "Name for which CRC failed is (now) '%s', ino #%d\n",
			  fd->name, je32_to_cpu(rd->ino));
		jffs2_free_full_dirent(fd);
		/* FIXME: Why do we believe totlen? */
		/* We believe totlen because the CRC on the node _header_ was OK, just the name failed. */
		if ((err = jffs2_scan_dirty_space(c, jeb, PAD(je32_to_cpu(rd->totlen)))))
			return err;
		return 0;
	}
	ic = jffs2_scan_make_ino_cache(c, je32_to_cpu(rd->pino));
	if (!ic) {
		jffs2_free_full_dirent(fd);
		return -ENOMEM;
	}

	fd->raw = jffs2_link_node_ref(c, jeb, ofs | dirent_node_state(rd),
				      PAD(je32_to_cpu(rd->totlen)), ic);

	fd->next = NULL;
	fd->version = je32_to_cpu(rd->version);
	fd->ino = je32_to_cpu(rd->ino);
	fd->nhash = full_name_hash(NULL, fd->name, checkedlen);
	fd->type = rd->type;
	jffs2_add_fd_to_list(c, fd, &ic->scan_dents);

	if (jffs2_sum_active()) {
		jffs2_sum_add_dirent_mem(s, rd, ofs - jeb->offset);
	}

	return 0;
}

static int count_list(struct list_head *l)
{
	uint32_t count = 0;
	struct list_head *tmp;

	list_for_each(tmp, l) {
		count++;
	}
	return count;
}

/* Note: This breaks if list_empty(head). I don't care. You
   might, if you copy this code and use it elsewhere :) */
static void rotate_list(struct list_head *head, uint32_t count)
{
	struct list_head *n = head->next;

	list_del(head);
	while(count--) {
		n = n->next;
	}
	list_add(head, n);
}

void jffs2_rotate_lists(struct jffs2_sb_info *c)
{
	uint32_t x;
	uint32_t rotateby;

	x = count_list(&c->clean_list);
	if (x) {
		rotateby = pseudo_random % x;
		rotate_list((&c->clean_list), rotateby);
	}

	x = count_list(&c->very_dirty_list);
	if (x) {
		rotateby = pseudo_random % x;
		rotate_list((&c->very_dirty_list), rotateby);
	}

	x = count_list(&c->dirty_list);
	if (x) {
		rotateby = pseudo_random % x;
		rotate_list((&c->dirty_list), rotateby);
	}

	x = count_list(&c->erasable_list);
	if (x) {
		rotateby = pseudo_random % x;
		rotate_list((&c->erasable_list), rotateby);
	}

	if (c->nr_erasing_blocks) {
		rotateby = pseudo_random % c->nr_erasing_blocks;
		rotate_list((&c->erase_pending_list), rotateby);
	}

	if (c->nr_free_blocks) {
		rotateby = pseudo_random % c->nr_free_blocks;
		rotate_list((&c->free_list), rotateby);
	}
}
