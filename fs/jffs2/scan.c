/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001-2003 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: scan.c,v 1.121 2005/07/20 15:32:28 dedekind Exp $
 *
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/pagemap.h>
#include <linux/crc32.h>
#include <linux/compiler.h>
#include "nodelist.h"

#define DEFAULT_EMPTY_SCAN_SIZE 1024

#define DIRTY_SPACE(x) do { typeof(x) _x = (x); \
		c->free_size -= _x; c->dirty_size += _x; \
		jeb->free_size -= _x ; jeb->dirty_size += _x; \
		}while(0)
#define USED_SPACE(x) do { typeof(x) _x = (x); \
		c->free_size -= _x; c->used_size += _x; \
		jeb->free_size -= _x ; jeb->used_size += _x; \
		}while(0)
#define UNCHECKED_SPACE(x) do { typeof(x) _x = (x); \
		c->free_size -= _x; c->unchecked_size += _x; \
		jeb->free_size -= _x ; jeb->unchecked_size += _x; \
		}while(0)

#define noisy_printk(noise, args...) do { \
	if (*(noise)) { \
		printk(KERN_NOTICE args); \
		 (*(noise))--; \
		 if (!(*(noise))) { \
			 printk(KERN_NOTICE "Further such events for this erase block will not be printed\n"); \
		 } \
	} \
} while(0)

static uint32_t pseudo_random;

static int jffs2_scan_eraseblock (struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
				  unsigned char *buf, uint32_t buf_size);

/* These helper functions _must_ increase ofs and also do the dirty/used space accounting. 
 * Returning an error will abort the mount - bad checksums etc. should just mark the space
 * as dirty.
 */
static int jffs2_scan_inode_node(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb, 
				 struct jffs2_raw_inode *ri, uint32_t ofs);
static int jffs2_scan_dirent_node(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
				 struct jffs2_raw_dirent *rd, uint32_t ofs);

#define BLK_STATE_ALLFF		0
#define BLK_STATE_CLEAN		1
#define BLK_STATE_PARTDIRTY	2
#define BLK_STATE_CLEANMARKER	3
#define BLK_STATE_ALLDIRTY	4
#define BLK_STATE_BADBLOCK	5

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

int jffs2_scan_medium(struct jffs2_sb_info *c)
{
	int i, ret;
	uint32_t empty_blocks = 0, bad_blocks = 0;
	unsigned char *flashbuf = NULL;
	uint32_t buf_size = 0;
#ifndef __ECOS
	size_t pointlen;

	if (c->mtd->point) {
		ret = c->mtd->point (c->mtd, 0, c->mtd->size, &pointlen, &flashbuf);
		if (!ret && pointlen < c->mtd->size) {
			/* Don't muck about if it won't let us point to the whole flash */
			D1(printk(KERN_DEBUG "MTD point returned len too short: 0x%zx\n", pointlen));
			c->mtd->unpoint(c->mtd, flashbuf, 0, c->mtd->size);
			flashbuf = NULL;
		}
		if (ret)
			D1(printk(KERN_DEBUG "MTD point failed %d\n", ret));
	}
#endif
	if (!flashbuf) {
		/* For NAND it's quicker to read a whole eraseblock at a time,
		   apparently */
		if (jffs2_cleanmarker_oob(c))
			buf_size = c->sector_size;
		else
			buf_size = PAGE_SIZE;

		/* Respect kmalloc limitations */
		if (buf_size > 128*1024)
			buf_size = 128*1024;

		D1(printk(KERN_DEBUG "Allocating readbuf of %d bytes\n", buf_size));
		flashbuf = kmalloc(buf_size, GFP_KERNEL);
		if (!flashbuf)
			return -ENOMEM;
	}

	for (i=0; i<c->nr_blocks; i++) {
		struct jffs2_eraseblock *jeb = &c->blocks[i];

		ret = jffs2_scan_eraseblock(c, jeb, buf_size?flashbuf:(flashbuf+jeb->offset), buf_size);

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
				D1(printk(KERN_DEBUG "Adding all-dirty block at 0x%08x to erase_pending_list\n", jeb->offset));
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
					c->nextblock->dirty_size += c->nextblock->free_size + c->nextblock->wasted_size;
					c->dirty_size += c->nextblock->free_size + c->nextblock->wasted_size;
					c->free_size -= c->nextblock->free_size;
					c->wasted_size -= c->nextblock->wasted_size;
					c->nextblock->free_size = c->nextblock->wasted_size = 0;
					if (VERYDIRTY(c, c->nextblock->dirty_size)) {
						list_add(&c->nextblock->list, &c->very_dirty_list);
					} else {
						list_add(&c->nextblock->list, &c->dirty_list);
					}
				}
                                c->nextblock = jeb;
                        } else {
				jeb->dirty_size += jeb->free_size + jeb->wasted_size;
				c->dirty_size += jeb->free_size + jeb->wasted_size;
				c->free_size -= jeb->free_size;
				c->wasted_size -= jeb->wasted_size;
				jeb->free_size = jeb->wasted_size = 0;
				if (VERYDIRTY(c, jeb->dirty_size)) {
					list_add(&jeb->list, &c->very_dirty_list);
				} else {
					list_add(&jeb->list, &c->dirty_list);
				}
                        }
			break;

		case BLK_STATE_ALLDIRTY:
			/* Nothing valid - not even a clean marker. Needs erasing. */
                        /* For now we just put it on the erasing list. We'll start the erases later */
			D1(printk(KERN_NOTICE "JFFS2: Erase block at 0x%08x is not formatted. It will be erased\n", jeb->offset));
                        list_add(&jeb->list, &c->erase_pending_list);
			c->nr_erasing_blocks++;
			break;
			
		case BLK_STATE_BADBLOCK:
			D1(printk(KERN_NOTICE "JFFS2: Block at 0x%08x is bad\n", jeb->offset));
                        list_add(&jeb->list, &c->bad_list);
			c->bad_size += c->sector_size;
			c->free_size -= c->sector_size;
			bad_blocks++;
			break;
		default:
			printk(KERN_WARNING "jffs2_scan_medium(): unknown block state\n");
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
	if (!jffs2_can_mark_obsolete(c) && c->nextblock && (c->nextblock->free_size & (c->wbuf_pagesize-1))) {
		/* If we're going to start writing into a block which already 
		   contains data, and the end of the data isn't page-aligned,
		   skip a little and align it. */

		uint32_t skip = c->nextblock->free_size & (c->wbuf_pagesize-1);

		D1(printk(KERN_DEBUG "jffs2_scan_medium(): Skipping %d bytes in nextblock to ensure page alignment\n",
			  skip));
		c->nextblock->wasted_size += skip;
		c->wasted_size += skip;

		c->nextblock->free_size -= skip;
		c->free_size -= skip;
	}
#endif
	if (c->nr_erasing_blocks) {
		if ( !c->used_size && ((c->nr_free_blocks+empty_blocks+bad_blocks)!= c->nr_blocks || bad_blocks == c->nr_blocks) ) { 
			printk(KERN_NOTICE "Cowardly refusing to erase blocks on filesystem with no valid JFFS2 nodes\n");
			printk(KERN_NOTICE "empty_blocks %d, bad_blocks %d, c->nr_blocks %d\n",empty_blocks,bad_blocks,c->nr_blocks);
			ret = -EIO;
			goto out;
		}
		jffs2_erase_pending_trigger(c);
	}
	ret = 0;
 out:
	if (buf_size)
		kfree(flashbuf);
#ifndef __ECOS
	else 
		c->mtd->unpoint(c->mtd, flashbuf, 0, c->mtd->size);
#endif
	return ret;
}

static int jffs2_fill_scan_buf (struct jffs2_sb_info *c, unsigned char *buf,
				uint32_t ofs, uint32_t len)
{
	int ret;
	size_t retlen;

	ret = jffs2_flash_read(c, ofs, len, &retlen, buf);
	if (ret) {
		D1(printk(KERN_WARNING "mtd->read(0x%x bytes from 0x%x) returned %d\n", len, ofs, ret));
		return ret;
	}
	if (retlen < len) {
		D1(printk(KERN_WARNING "Read at 0x%x gave only 0x%zx bytes\n", ofs, retlen));
		return -EIO;
	}
	D2(printk(KERN_DEBUG "Read 0x%x bytes from 0x%08x into buf\n", len, ofs));
	D2(printk(KERN_DEBUG "000: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
		  buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf[8], buf[9], buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]));
	return 0;
}

static int jffs2_scan_eraseblock (struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
				  unsigned char *buf, uint32_t buf_size) {
	struct jffs2_unknown_node *node;
	struct jffs2_unknown_node crcnode;
	uint32_t ofs, prevofs;
	uint32_t hdr_crc, buf_ofs, buf_len;
	int err;
	int noise = 0;
#ifdef CONFIG_JFFS2_FS_WRITEBUFFER
	int cleanmarkerfound = 0;
#endif

	ofs = jeb->offset;
	prevofs = jeb->offset - 1;

	D1(printk(KERN_DEBUG "jffs2_scan_eraseblock(): Scanning block at 0x%x\n", ofs));

#ifdef CONFIG_JFFS2_FS_WRITEBUFFER
	if (jffs2_cleanmarker_oob(c)) {
		int ret = jffs2_check_nand_cleanmarker(c, jeb);
		D2(printk(KERN_NOTICE "jffs_check_nand_cleanmarker returned %d\n",ret));
		/* Even if it's not found, we still scan to see
		   if the block is empty. We use this information
		   to decide whether to erase it or not. */
		switch (ret) {
		case 0:		cleanmarkerfound = 1; break;
		case 1: 	break;
		case 2: 	return BLK_STATE_BADBLOCK;
		case 3:		return BLK_STATE_ALLDIRTY; /* Block has failed to erase min. once */
		default: 	return ret;
		}
	}
#endif
	buf_ofs = jeb->offset;

	if (!buf_size) {
		buf_len = c->sector_size;
	} else {
		buf_len = EMPTY_SCAN_SIZE(c->sector_size);
		err = jffs2_fill_scan_buf(c, buf, buf_ofs, buf_len);
		if (err)
			return err;
	}
	
	/* We temporarily use 'ofs' as a pointer into the buffer/jeb */
	ofs = 0;

	/* Scan only 4KiB of 0xFF before declaring it's empty */
	while(ofs < EMPTY_SCAN_SIZE(c->sector_size) && *(uint32_t *)(&buf[ofs]) == 0xFFFFFFFF)
		ofs += 4;

	if (ofs == EMPTY_SCAN_SIZE(c->sector_size)) {
#ifdef CONFIG_JFFS2_FS_WRITEBUFFER
		if (jffs2_cleanmarker_oob(c)) {
			/* scan oob, take care of cleanmarker */
			int ret = jffs2_check_oob_empty(c, jeb, cleanmarkerfound);
			D2(printk(KERN_NOTICE "jffs2_check_oob_empty returned %d\n",ret));
			switch (ret) {
			case 0:		return cleanmarkerfound ? BLK_STATE_CLEANMARKER : BLK_STATE_ALLFF;
			case 1: 	return BLK_STATE_ALLDIRTY;
			default: 	return ret;
			}
		}
#endif
		D1(printk(KERN_DEBUG "Block at 0x%08x is empty (erased)\n", jeb->offset));
		if (c->cleanmarker_size == 0)
			return BLK_STATE_CLEANMARKER;	/* don't bother with re-erase */
		else
			return BLK_STATE_ALLFF;	/* OK to erase if all blocks are like this */
	}
	if (ofs) {
		D1(printk(KERN_DEBUG "Free space at %08x ends at %08x\n", jeb->offset,
			  jeb->offset + ofs));
		DIRTY_SPACE(ofs);
	}

	/* Now ofs is a complete physical flash offset as it always was... */
	ofs += jeb->offset;

	noise = 10;

scan_more:	
	while(ofs < jeb->offset + c->sector_size) {

		jffs2_dbg_acct_paranoia_check_nolock(c, jeb);

		cond_resched();

		if (ofs & 3) {
			printk(KERN_WARNING "Eep. ofs 0x%08x not word-aligned!\n", ofs);
			ofs = PAD(ofs);
			continue;
		}
		if (ofs == prevofs) {
			printk(KERN_WARNING "ofs 0x%08x has already been seen. Skipping\n", ofs);
			DIRTY_SPACE(4);
			ofs += 4;
			continue;
		}
		prevofs = ofs;

		if (jeb->offset + c->sector_size < ofs + sizeof(*node)) {
			D1(printk(KERN_DEBUG "Fewer than %zd bytes left to end of block. (%x+%x<%x+%zx) Not reading\n", sizeof(struct jffs2_unknown_node),
				  jeb->offset, c->sector_size, ofs, sizeof(*node)));
			DIRTY_SPACE((jeb->offset + c->sector_size)-ofs);
			break;
		}

		if (buf_ofs + buf_len < ofs + sizeof(*node)) {
			buf_len = min_t(uint32_t, buf_size, jeb->offset + c->sector_size - ofs);
			D1(printk(KERN_DEBUG "Fewer than %zd bytes (node header) left to end of buf. Reading 0x%x at 0x%08x\n",
				  sizeof(struct jffs2_unknown_node), buf_len, ofs));
			err = jffs2_fill_scan_buf(c, buf, ofs, buf_len);
			if (err)
				return err;
			buf_ofs = ofs;
		}

		node = (struct jffs2_unknown_node *)&buf[ofs-buf_ofs];

		if (*(uint32_t *)(&buf[ofs-buf_ofs]) == 0xffffffff) {
			uint32_t inbuf_ofs;
			uint32_t empty_start;

			empty_start = ofs;
			ofs += 4;

			D1(printk(KERN_DEBUG "Found empty flash at 0x%08x\n", ofs));
		more_empty:
			inbuf_ofs = ofs - buf_ofs;
			while (inbuf_ofs < buf_len) {
				if (*(uint32_t *)(&buf[inbuf_ofs]) != 0xffffffff) {
					printk(KERN_WARNING "Empty flash at 0x%08x ends at 0x%08x\n",
					       empty_start, ofs);
					DIRTY_SPACE(ofs-empty_start);
					goto scan_more;
				}

				inbuf_ofs+=4;
				ofs += 4;
			}
			/* Ran off end. */
			D1(printk(KERN_DEBUG "Empty flash to end of buffer at 0x%08x\n", ofs));

			/* If we're only checking the beginning of a block with a cleanmarker,
			   bail now */
			if (buf_ofs == jeb->offset && jeb->used_size == PAD(c->cleanmarker_size) && 
			    c->cleanmarker_size && !jeb->dirty_size && !jeb->first_node->next_phys) {
				D1(printk(KERN_DEBUG "%d bytes at start of block seems clean... assuming all clean\n", EMPTY_SCAN_SIZE(c->sector_size)));
				return BLK_STATE_CLEANMARKER;
			}

			/* See how much more there is to read in this eraseblock... */
			buf_len = min_t(uint32_t, buf_size, jeb->offset + c->sector_size - ofs);
			if (!buf_len) {
				/* No more to read. Break out of main loop without marking 
				   this range of empty space as dirty (because it's not) */
				D1(printk(KERN_DEBUG "Empty flash at %08x runs to end of block. Treating as free_space\n",
					  empty_start));
				break;
			}
			D1(printk(KERN_DEBUG "Reading another 0x%x at 0x%08x\n", buf_len, ofs));
			err = jffs2_fill_scan_buf(c, buf, ofs, buf_len);
			if (err)
				return err;
			buf_ofs = ofs;
			goto more_empty;
		}

		if (ofs == jeb->offset && je16_to_cpu(node->magic) == KSAMTIB_CIGAM_2SFFJ) {
			printk(KERN_WARNING "Magic bitmask is backwards at offset 0x%08x. Wrong endian filesystem?\n", ofs);
			DIRTY_SPACE(4);
			ofs += 4;
			continue;
		}
		if (je16_to_cpu(node->magic) == JFFS2_DIRTY_BITMASK) {
			D1(printk(KERN_DEBUG "Dirty bitmask at 0x%08x\n", ofs));
			DIRTY_SPACE(4);
			ofs += 4;
			continue;
		}
		if (je16_to_cpu(node->magic) == JFFS2_OLD_MAGIC_BITMASK) {
			printk(KERN_WARNING "Old JFFS2 bitmask found at 0x%08x\n", ofs);
			printk(KERN_WARNING "You cannot use older JFFS2 filesystems with newer kernels\n");
			DIRTY_SPACE(4);
			ofs += 4;
			continue;
		}
		if (je16_to_cpu(node->magic) != JFFS2_MAGIC_BITMASK) {
			/* OK. We're out of possibilities. Whinge and move on */
			noisy_printk(&noise, "jffs2_scan_eraseblock(): Magic bitmask 0x%04x not found at 0x%08x: 0x%04x instead\n", 
				     JFFS2_MAGIC_BITMASK, ofs, 
				     je16_to_cpu(node->magic));
			DIRTY_SPACE(4);
			ofs += 4;
			continue;
		}
		/* We seem to have a node of sorts. Check the CRC */
		crcnode.magic = node->magic;
		crcnode.nodetype = cpu_to_je16( je16_to_cpu(node->nodetype) | JFFS2_NODE_ACCURATE);
		crcnode.totlen = node->totlen;
		hdr_crc = crc32(0, &crcnode, sizeof(crcnode)-4);

		if (hdr_crc != je32_to_cpu(node->hdr_crc)) {
			noisy_printk(&noise, "jffs2_scan_eraseblock(): Node at 0x%08x {0x%04x, 0x%04x, 0x%08x) has invalid CRC 0x%08x (calculated 0x%08x)\n",
				     ofs, je16_to_cpu(node->magic),
				     je16_to_cpu(node->nodetype), 
				     je32_to_cpu(node->totlen),
				     je32_to_cpu(node->hdr_crc),
				     hdr_crc);
			DIRTY_SPACE(4);
			ofs += 4;
			continue;
		}

		if (ofs + je32_to_cpu(node->totlen) > 
		    jeb->offset + c->sector_size) {
			/* Eep. Node goes over the end of the erase block. */
			printk(KERN_WARNING "Node at 0x%08x with length 0x%08x would run over the end of the erase block\n",
			       ofs, je32_to_cpu(node->totlen));
			printk(KERN_WARNING "Perhaps the file system was created with the wrong erase size?\n");
			DIRTY_SPACE(4);
			ofs += 4;
			continue;
		}

		if (!(je16_to_cpu(node->nodetype) & JFFS2_NODE_ACCURATE)) {
			/* Wheee. This is an obsoleted node */
			D2(printk(KERN_DEBUG "Node at 0x%08x is obsolete. Skipping\n", ofs));
			DIRTY_SPACE(PAD(je32_to_cpu(node->totlen)));
			ofs += PAD(je32_to_cpu(node->totlen));
			continue;
		}

		switch(je16_to_cpu(node->nodetype)) {
		case JFFS2_NODETYPE_INODE:
			if (buf_ofs + buf_len < ofs + sizeof(struct jffs2_raw_inode)) {
				buf_len = min_t(uint32_t, buf_size, jeb->offset + c->sector_size - ofs);
				D1(printk(KERN_DEBUG "Fewer than %zd bytes (inode node) left to end of buf. Reading 0x%x at 0x%08x\n",
					  sizeof(struct jffs2_raw_inode), buf_len, ofs));
				err = jffs2_fill_scan_buf(c, buf, ofs, buf_len);
				if (err)
					return err;
				buf_ofs = ofs;
				node = (void *)buf;
			}
			err = jffs2_scan_inode_node(c, jeb, (void *)node, ofs);
			if (err) return err;
			ofs += PAD(je32_to_cpu(node->totlen));
			break;
			
		case JFFS2_NODETYPE_DIRENT:
			if (buf_ofs + buf_len < ofs + je32_to_cpu(node->totlen)) {
				buf_len = min_t(uint32_t, buf_size, jeb->offset + c->sector_size - ofs);
				D1(printk(KERN_DEBUG "Fewer than %d bytes (dirent node) left to end of buf. Reading 0x%x at 0x%08x\n",
					  je32_to_cpu(node->totlen), buf_len, ofs));
				err = jffs2_fill_scan_buf(c, buf, ofs, buf_len);
				if (err)
					return err;
				buf_ofs = ofs;
				node = (void *)buf;
			}
			err = jffs2_scan_dirent_node(c, jeb, (void *)node, ofs);
			if (err) return err;
			ofs += PAD(je32_to_cpu(node->totlen));
			break;

		case JFFS2_NODETYPE_CLEANMARKER:
			D1(printk(KERN_DEBUG "CLEANMARKER node found at 0x%08x\n", ofs));
			if (je32_to_cpu(node->totlen) != c->cleanmarker_size) {
				printk(KERN_NOTICE "CLEANMARKER node found at 0x%08x has totlen 0x%x != normal 0x%x\n", 
				       ofs, je32_to_cpu(node->totlen), c->cleanmarker_size);
				DIRTY_SPACE(PAD(sizeof(struct jffs2_unknown_node)));
				ofs += PAD(sizeof(struct jffs2_unknown_node));
			} else if (jeb->first_node) {
				printk(KERN_NOTICE "CLEANMARKER node found at 0x%08x, not first node in block (0x%08x)\n", ofs, jeb->offset);
				DIRTY_SPACE(PAD(sizeof(struct jffs2_unknown_node)));
				ofs += PAD(sizeof(struct jffs2_unknown_node));
			} else {
				struct jffs2_raw_node_ref *marker_ref = jffs2_alloc_raw_node_ref();
				if (!marker_ref) {
					printk(KERN_NOTICE "Failed to allocate node ref for clean marker\n");
					return -ENOMEM;
				}
				marker_ref->next_in_ino = NULL;
				marker_ref->next_phys = NULL;
				marker_ref->flash_offset = ofs | REF_NORMAL;
				marker_ref->__totlen = c->cleanmarker_size;
				jeb->first_node = jeb->last_node = marker_ref;
			     
				USED_SPACE(PAD(c->cleanmarker_size));
				ofs += PAD(c->cleanmarker_size);
			}
			break;

		case JFFS2_NODETYPE_PADDING:
			DIRTY_SPACE(PAD(je32_to_cpu(node->totlen)));
			ofs += PAD(je32_to_cpu(node->totlen));
			break;

		default:
			switch (je16_to_cpu(node->nodetype) & JFFS2_COMPAT_MASK) {
			case JFFS2_FEATURE_ROCOMPAT:
				printk(KERN_NOTICE "Read-only compatible feature node (0x%04x) found at offset 0x%08x\n", je16_to_cpu(node->nodetype), ofs);
			        c->flags |= JFFS2_SB_FLAG_RO;
				if (!(jffs2_is_readonly(c)))
					return -EROFS;
				DIRTY_SPACE(PAD(je32_to_cpu(node->totlen)));
				ofs += PAD(je32_to_cpu(node->totlen));
				break;

			case JFFS2_FEATURE_INCOMPAT:
				printk(KERN_NOTICE "Incompatible feature node (0x%04x) found at offset 0x%08x\n", je16_to_cpu(node->nodetype), ofs);
				return -EINVAL;

			case JFFS2_FEATURE_RWCOMPAT_DELETE:
				D1(printk(KERN_NOTICE "Unknown but compatible feature node (0x%04x) found at offset 0x%08x\n", je16_to_cpu(node->nodetype), ofs));
				DIRTY_SPACE(PAD(je32_to_cpu(node->totlen)));
				ofs += PAD(je32_to_cpu(node->totlen));
				break;

			case JFFS2_FEATURE_RWCOMPAT_COPY:
				D1(printk(KERN_NOTICE "Unknown but compatible feature node (0x%04x) found at offset 0x%08x\n", je16_to_cpu(node->nodetype), ofs));
				USED_SPACE(PAD(je32_to_cpu(node->totlen)));
				ofs += PAD(je32_to_cpu(node->totlen));
				break;
			}
		}
	}


	D1(printk(KERN_DEBUG "Block at 0x%08x: free 0x%08x, dirty 0x%08x, unchecked 0x%08x, used 0x%08x\n", jeb->offset, 
		  jeb->free_size, jeb->dirty_size, jeb->unchecked_size, jeb->used_size));

	/* mark_node_obsolete can add to wasted !! */
	if (jeb->wasted_size) {
		jeb->dirty_size += jeb->wasted_size;
		c->dirty_size += jeb->wasted_size;
		c->wasted_size -= jeb->wasted_size;
		jeb->wasted_size = 0;
	}

	if ((jeb->used_size + jeb->unchecked_size) == PAD(c->cleanmarker_size) && !jeb->dirty_size 
		&& (!jeb->first_node || !jeb->first_node->next_phys) )
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

static struct jffs2_inode_cache *jffs2_scan_make_ino_cache(struct jffs2_sb_info *c, uint32_t ino)
{
	struct jffs2_inode_cache *ic;

	ic = jffs2_get_ino_cache(c, ino);
	if (ic)
		return ic;

	if (ino > c->highest_ino)
		c->highest_ino = ino;

	ic = jffs2_alloc_inode_cache();
	if (!ic) {
		printk(KERN_NOTICE "jffs2_scan_make_inode_cache(): allocation of inode cache failed\n");
		return NULL;
	}
	memset(ic, 0, sizeof(*ic));

	ic->ino = ino;
	ic->nodes = (void *)ic;
	jffs2_add_ino_cache(c, ic);
	if (ino == 1)
		ic->nlink = 1;
	return ic;
}

static int jffs2_scan_inode_node(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb, 
				 struct jffs2_raw_inode *ri, uint32_t ofs)
{
	struct jffs2_raw_node_ref *raw;
	struct jffs2_inode_cache *ic;
	uint32_t ino = je32_to_cpu(ri->ino);

	D1(printk(KERN_DEBUG "jffs2_scan_inode_node(): Node at 0x%08x\n", ofs));

	/* We do very little here now. Just check the ino# to which we should attribute
	   this node; we can do all the CRC checking etc. later. There's a tradeoff here -- 
	   we used to scan the flash once only, reading everything we want from it into
	   memory, then building all our in-core data structures and freeing the extra
	   information. Now we allow the first part of the mount to complete a lot quicker,
	   but we have to go _back_ to the flash in order to finish the CRC checking, etc. 
	   Which means that the _full_ amount of time to get to proper write mode with GC
	   operational may actually be _longer_ than before. Sucks to be me. */

	raw = jffs2_alloc_raw_node_ref();
	if (!raw) {
		printk(KERN_NOTICE "jffs2_scan_inode_node(): allocation of node reference failed\n");
		return -ENOMEM;
	}

	ic = jffs2_get_ino_cache(c, ino);
	if (!ic) {
		/* Inocache get failed. Either we read a bogus ino# or it's just genuinely the
		   first node we found for this inode. Do a CRC check to protect against the former
		   case */
		uint32_t crc = crc32(0, ri, sizeof(*ri)-8);

		if (crc != je32_to_cpu(ri->node_crc)) {
			printk(KERN_NOTICE "jffs2_scan_inode_node(): CRC failed on node at 0x%08x: Read 0x%08x, calculated 0x%08x\n",
			       ofs, je32_to_cpu(ri->node_crc), crc);
			/* We believe totlen because the CRC on the node _header_ was OK, just the node itself failed. */
			DIRTY_SPACE(PAD(je32_to_cpu(ri->totlen)));
			jffs2_free_raw_node_ref(raw);
			return 0;
		}
		ic = jffs2_scan_make_ino_cache(c, ino);
		if (!ic) {
			jffs2_free_raw_node_ref(raw);
			return -ENOMEM;
		}
	}

	/* Wheee. It worked */

	raw->flash_offset = ofs | REF_UNCHECKED;
	raw->__totlen = PAD(je32_to_cpu(ri->totlen));
	raw->next_phys = NULL;
	raw->next_in_ino = ic->nodes;

	ic->nodes = raw;
	if (!jeb->first_node)
		jeb->first_node = raw;
	if (jeb->last_node)
		jeb->last_node->next_phys = raw;
	jeb->last_node = raw;

	D1(printk(KERN_DEBUG "Node is ino #%u, version %d. Range 0x%x-0x%x\n", 
		  je32_to_cpu(ri->ino), je32_to_cpu(ri->version),
		  je32_to_cpu(ri->offset),
		  je32_to_cpu(ri->offset)+je32_to_cpu(ri->dsize)));

	pseudo_random += je32_to_cpu(ri->version);

	UNCHECKED_SPACE(PAD(je32_to_cpu(ri->totlen)));
	return 0;
}

static int jffs2_scan_dirent_node(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb, 
				  struct jffs2_raw_dirent *rd, uint32_t ofs)
{
	struct jffs2_raw_node_ref *raw;
	struct jffs2_full_dirent *fd;
	struct jffs2_inode_cache *ic;
	uint32_t crc;

	D1(printk(KERN_DEBUG "jffs2_scan_dirent_node(): Node at 0x%08x\n", ofs));

	/* We don't get here unless the node is still valid, so we don't have to
	   mask in the ACCURATE bit any more. */
	crc = crc32(0, rd, sizeof(*rd)-8);

	if (crc != je32_to_cpu(rd->node_crc)) {
		printk(KERN_NOTICE "jffs2_scan_dirent_node(): Node CRC failed on node at 0x%08x: Read 0x%08x, calculated 0x%08x\n",
		       ofs, je32_to_cpu(rd->node_crc), crc);
		/* We believe totlen because the CRC on the node _header_ was OK, just the node itself failed. */
		DIRTY_SPACE(PAD(je32_to_cpu(rd->totlen)));
		return 0;
	}

	pseudo_random += je32_to_cpu(rd->version);

	fd = jffs2_alloc_full_dirent(rd->nsize+1);
	if (!fd) {
		return -ENOMEM;
	}
	memcpy(&fd->name, rd->name, rd->nsize);
	fd->name[rd->nsize] = 0;

	crc = crc32(0, fd->name, rd->nsize);
	if (crc != je32_to_cpu(rd->name_crc)) {
		printk(KERN_NOTICE "jffs2_scan_dirent_node(): Name CRC failed on node at 0x%08x: Read 0x%08x, calculated 0x%08x\n",
		       ofs, je32_to_cpu(rd->name_crc), crc);	
		D1(printk(KERN_NOTICE "Name for which CRC failed is (now) '%s', ino #%d\n", fd->name, je32_to_cpu(rd->ino)));
		jffs2_free_full_dirent(fd);
		/* FIXME: Why do we believe totlen? */
		/* We believe totlen because the CRC on the node _header_ was OK, just the name failed. */
		DIRTY_SPACE(PAD(je32_to_cpu(rd->totlen)));
		return 0;
	}
	raw = jffs2_alloc_raw_node_ref();
	if (!raw) {
		jffs2_free_full_dirent(fd);
		printk(KERN_NOTICE "jffs2_scan_dirent_node(): allocation of node reference failed\n");
		return -ENOMEM;
	}
	ic = jffs2_scan_make_ino_cache(c, je32_to_cpu(rd->pino));
	if (!ic) {
		jffs2_free_full_dirent(fd);
		jffs2_free_raw_node_ref(raw);
		return -ENOMEM;
	}
	
	raw->__totlen = PAD(je32_to_cpu(rd->totlen));
	raw->flash_offset = ofs | REF_PRISTINE;
	raw->next_phys = NULL;
	raw->next_in_ino = ic->nodes;
	ic->nodes = raw;
	if (!jeb->first_node)
		jeb->first_node = raw;
	if (jeb->last_node)
		jeb->last_node->next_phys = raw;
	jeb->last_node = raw;

	fd->raw = raw;
	fd->next = NULL;
	fd->version = je32_to_cpu(rd->version);
	fd->ino = je32_to_cpu(rd->ino);
	fd->nhash = full_name_hash(fd->name, rd->nsize);
	fd->type = rd->type;
	USED_SPACE(PAD(je32_to_cpu(rd->totlen)));
	jffs2_add_fd_to_list(c, fd, &ic->scan_dents);

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
		D1(printk(KERN_DEBUG "Rotating clean_list by %d\n", rotateby));

		rotate_list((&c->clean_list), rotateby);

		D1(printk(KERN_DEBUG "Erase block at front of clean_list is at %08x\n",
			  list_entry(c->clean_list.next, struct jffs2_eraseblock, list)->offset));
	} else {
		D1(printk(KERN_DEBUG "Not rotating empty clean_list\n"));
	}

	x = count_list(&c->very_dirty_list);
	if (x) {
		rotateby = pseudo_random % x;
		D1(printk(KERN_DEBUG "Rotating very_dirty_list by %d\n", rotateby));

		rotate_list((&c->very_dirty_list), rotateby);

		D1(printk(KERN_DEBUG "Erase block at front of very_dirty_list is at %08x\n",
			  list_entry(c->very_dirty_list.next, struct jffs2_eraseblock, list)->offset));
	} else {
		D1(printk(KERN_DEBUG "Not rotating empty very_dirty_list\n"));
	}

	x = count_list(&c->dirty_list);
	if (x) {
		rotateby = pseudo_random % x;
		D1(printk(KERN_DEBUG "Rotating dirty_list by %d\n", rotateby));

		rotate_list((&c->dirty_list), rotateby);

		D1(printk(KERN_DEBUG "Erase block at front of dirty_list is at %08x\n",
			  list_entry(c->dirty_list.next, struct jffs2_eraseblock, list)->offset));
	} else {
		D1(printk(KERN_DEBUG "Not rotating empty dirty_list\n"));
	}

	x = count_list(&c->erasable_list);
	if (x) {
		rotateby = pseudo_random % x;
		D1(printk(KERN_DEBUG "Rotating erasable_list by %d\n", rotateby));

		rotate_list((&c->erasable_list), rotateby);

		D1(printk(KERN_DEBUG "Erase block at front of erasable_list is at %08x\n",
			  list_entry(c->erasable_list.next, struct jffs2_eraseblock, list)->offset));
	} else {
		D1(printk(KERN_DEBUG "Not rotating empty erasable_list\n"));
	}

	if (c->nr_erasing_blocks) {
		rotateby = pseudo_random % c->nr_erasing_blocks;
		D1(printk(KERN_DEBUG "Rotating erase_pending_list by %d\n", rotateby));

		rotate_list((&c->erase_pending_list), rotateby);

		D1(printk(KERN_DEBUG "Erase block at front of erase_pending_list is at %08x\n",
			  list_entry(c->erase_pending_list.next, struct jffs2_eraseblock, list)->offset));
	} else {
		D1(printk(KERN_DEBUG "Not rotating empty erase_pending_list\n"));
	}

	if (c->nr_free_blocks) {
		rotateby = pseudo_random % c->nr_free_blocks;
		D1(printk(KERN_DEBUG "Rotating free_list by %d\n", rotateby));

		rotate_list((&c->free_list), rotateby);

		D1(printk(KERN_DEBUG "Erase block at front of free_list is at %08x\n",
			  list_entry(c->free_list.next, struct jffs2_eraseblock, list)->offset));
	} else {
		D1(printk(KERN_DEBUG "Not rotating empty free_list\n"));
	}
}
