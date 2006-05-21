/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001-2003 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: erase.c,v 1.85 2005/09/20 14:53:15 dedekind Exp $
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/compiler.h>
#include <linux/crc32.h>
#include <linux/sched.h>
#include <linux/pagemap.h>
#include "nodelist.h"

struct erase_priv_struct {
	struct jffs2_eraseblock *jeb;
	struct jffs2_sb_info *c;
};

#ifndef __ECOS
static void jffs2_erase_callback(struct erase_info *);
#endif
static void jffs2_erase_failed(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb, uint32_t bad_offset);
static void jffs2_erase_succeeded(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb);
static void jffs2_mark_erased_block(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb);

static void jffs2_erase_block(struct jffs2_sb_info *c,
			      struct jffs2_eraseblock *jeb)
{
	int ret;
	uint32_t bad_offset;
#ifdef __ECOS
       ret = jffs2_flash_erase(c, jeb);
       if (!ret) {
               jffs2_erase_succeeded(c, jeb);
               return;
       }
       bad_offset = jeb->offset;
#else /* Linux */
	struct erase_info *instr;

	D1(printk(KERN_DEBUG "jffs2_erase_block(): erase block %#08x (range %#08x-%#08x)\n",
				jeb->offset, jeb->offset, jeb->offset + c->sector_size));
	instr = kmalloc(sizeof(struct erase_info) + sizeof(struct erase_priv_struct), GFP_KERNEL);
	if (!instr) {
		printk(KERN_WARNING "kmalloc for struct erase_info in jffs2_erase_block failed. Refiling block for later\n");
		spin_lock(&c->erase_completion_lock);
		list_del(&jeb->list);
		list_add(&jeb->list, &c->erase_pending_list);
		c->erasing_size -= c->sector_size;
		c->dirty_size += c->sector_size;
		jeb->dirty_size = c->sector_size;
		spin_unlock(&c->erase_completion_lock);
		return;
	}

	memset(instr, 0, sizeof(*instr));

	instr->mtd = c->mtd;
	instr->addr = jeb->offset;
	instr->len = c->sector_size;
	instr->callback = jffs2_erase_callback;
	instr->priv = (unsigned long)(&instr[1]);
	instr->fail_addr = 0xffffffff;

	((struct erase_priv_struct *)instr->priv)->jeb = jeb;
	((struct erase_priv_struct *)instr->priv)->c = c;

	ret = c->mtd->erase(c->mtd, instr);
	if (!ret)
		return;

	bad_offset = instr->fail_addr;
	kfree(instr);
#endif /* __ECOS */

	if (ret == -ENOMEM || ret == -EAGAIN) {
		/* Erase failed immediately. Refile it on the list */
		D1(printk(KERN_DEBUG "Erase at 0x%08x failed: %d. Refiling on erase_pending_list\n", jeb->offset, ret));
		spin_lock(&c->erase_completion_lock);
		list_del(&jeb->list);
		list_add(&jeb->list, &c->erase_pending_list);
		c->erasing_size -= c->sector_size;
		c->dirty_size += c->sector_size;
		jeb->dirty_size = c->sector_size;
		spin_unlock(&c->erase_completion_lock);
		return;
	}

	if (ret == -EROFS)
		printk(KERN_WARNING "Erase at 0x%08x failed immediately: -EROFS. Is the sector locked?\n", jeb->offset);
	else
		printk(KERN_WARNING "Erase at 0x%08x failed immediately: errno %d\n", jeb->offset, ret);

	jffs2_erase_failed(c, jeb, bad_offset);
}

void jffs2_erase_pending_blocks(struct jffs2_sb_info *c, int count)
{
	struct jffs2_eraseblock *jeb;

	down(&c->erase_free_sem);

	spin_lock(&c->erase_completion_lock);

	while (!list_empty(&c->erase_complete_list) ||
	       !list_empty(&c->erase_pending_list)) {

		if (!list_empty(&c->erase_complete_list)) {
			jeb = list_entry(c->erase_complete_list.next, struct jffs2_eraseblock, list);
			list_del(&jeb->list);
			spin_unlock(&c->erase_completion_lock);
			jffs2_mark_erased_block(c, jeb);

			if (!--count) {
				D1(printk(KERN_DEBUG "Count reached. jffs2_erase_pending_blocks leaving\n"));
				goto done;
			}

		} else if (!list_empty(&c->erase_pending_list)) {
			jeb = list_entry(c->erase_pending_list.next, struct jffs2_eraseblock, list);
			D1(printk(KERN_DEBUG "Starting erase of pending block 0x%08x\n", jeb->offset));
			list_del(&jeb->list);
			c->erasing_size += c->sector_size;
			c->wasted_size -= jeb->wasted_size;
			c->free_size -= jeb->free_size;
			c->used_size -= jeb->used_size;
			c->dirty_size -= jeb->dirty_size;
			jeb->wasted_size = jeb->used_size = jeb->dirty_size = jeb->free_size = 0;
			jffs2_free_all_node_refs(c, jeb);
			list_add(&jeb->list, &c->erasing_list);
			spin_unlock(&c->erase_completion_lock);

			jffs2_erase_block(c, jeb);

		} else {
			BUG();
		}

		/* Be nice */
		cond_resched();
		spin_lock(&c->erase_completion_lock);
	}

	spin_unlock(&c->erase_completion_lock);
 done:
	D1(printk(KERN_DEBUG "jffs2_erase_pending_blocks completed\n"));

	up(&c->erase_free_sem);
}

static void jffs2_erase_succeeded(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb)
{
	D1(printk(KERN_DEBUG "Erase completed successfully at 0x%08x\n", jeb->offset));
	spin_lock(&c->erase_completion_lock);
	list_del(&jeb->list);
	list_add_tail(&jeb->list, &c->erase_complete_list);
	spin_unlock(&c->erase_completion_lock);
	/* Ensure that kupdated calls us again to mark them clean */
	jffs2_erase_pending_trigger(c);
}

static void jffs2_erase_failed(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb, uint32_t bad_offset)
{
	/* For NAND, if the failure did not occur at the device level for a
	   specific physical page, don't bother updating the bad block table. */
	if (jffs2_cleanmarker_oob(c) && (bad_offset != 0xffffffff)) {
		/* We had a device-level failure to erase.  Let's see if we've
		   failed too many times. */
		if (!jffs2_write_nand_badblock(c, jeb, bad_offset)) {
			/* We'd like to give this block another try. */
			spin_lock(&c->erase_completion_lock);
			list_del(&jeb->list);
			list_add(&jeb->list, &c->erase_pending_list);
			c->erasing_size -= c->sector_size;
			c->dirty_size += c->sector_size;
			jeb->dirty_size = c->sector_size;
			spin_unlock(&c->erase_completion_lock);
			return;
		}
	}

	spin_lock(&c->erase_completion_lock);
	c->erasing_size -= c->sector_size;
	c->bad_size += c->sector_size;
	list_del(&jeb->list);
	list_add(&jeb->list, &c->bad_list);
	c->nr_erasing_blocks--;
	spin_unlock(&c->erase_completion_lock);
	wake_up(&c->erase_wait);
}

#ifndef __ECOS
static void jffs2_erase_callback(struct erase_info *instr)
{
	struct erase_priv_struct *priv = (void *)instr->priv;

	if(instr->state != MTD_ERASE_DONE) {
		printk(KERN_WARNING "Erase at 0x%08x finished, but state != MTD_ERASE_DONE. State is 0x%x instead.\n", instr->addr, instr->state);
		jffs2_erase_failed(priv->c, priv->jeb, instr->fail_addr);
	} else {
		jffs2_erase_succeeded(priv->c, priv->jeb);
	}
	kfree(instr);
}
#endif /* !__ECOS */

/* Hmmm. Maybe we should accept the extra space it takes and make
   this a standard doubly-linked list? */
static inline void jffs2_remove_node_refs_from_ino_list(struct jffs2_sb_info *c,
			struct jffs2_raw_node_ref *ref, struct jffs2_eraseblock *jeb)
{
	struct jffs2_inode_cache *ic = NULL;
	struct jffs2_raw_node_ref **prev;

	prev = &ref->next_in_ino;

	/* Walk the inode's list once, removing any nodes from this eraseblock */
	while (1) {
		if (!(*prev)->next_in_ino) {
			/* We're looking at the jffs2_inode_cache, which is
			   at the end of the linked list. Stash it and continue
			   from the beginning of the list */
			ic = (struct jffs2_inode_cache *)(*prev);
			prev = &ic->nodes;
			continue;
		}

		if (SECTOR_ADDR((*prev)->flash_offset) == jeb->offset) {
			/* It's in the block we're erasing */
			struct jffs2_raw_node_ref *this;

			this = *prev;
			*prev = this->next_in_ino;
			this->next_in_ino = NULL;

			if (this == ref)
				break;

			continue;
		}
		/* Not to be deleted. Skip */
		prev = &((*prev)->next_in_ino);
	}

	/* PARANOIA */
	if (!ic) {
		printk(KERN_WARNING "inode_cache not found in remove_node_refs()!!\n");
		return;
	}

	D1(printk(KERN_DEBUG "Removed nodes in range 0x%08x-0x%08x from ino #%u\n",
		  jeb->offset, jeb->offset + c->sector_size, ic->ino));

	D2({
		int i=0;
		struct jffs2_raw_node_ref *this;
		printk(KERN_DEBUG "After remove_node_refs_from_ino_list: \n" KERN_DEBUG);

		this = ic->nodes;

		while(this) {
			printk( "0x%08x(%d)->", ref_offset(this), ref_flags(this));
			if (++i == 5) {
				printk("\n" KERN_DEBUG);
				i=0;
			}
			this = this->next_in_ino;
		}
		printk("\n");
	});

	if (ic->nodes == (void *)ic && ic->nlink == 0)
		jffs2_del_ino_cache(c, ic);
}

void jffs2_free_all_node_refs(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb)
{
	struct jffs2_raw_node_ref *ref;
	D1(printk(KERN_DEBUG "Freeing all node refs for eraseblock offset 0x%08x\n", jeb->offset));
	while(jeb->first_node) {
		ref = jeb->first_node;
		jeb->first_node = ref->next_phys;

		/* Remove from the inode-list */
		if (ref->next_in_ino)
			jffs2_remove_node_refs_from_ino_list(c, ref, jeb);
		/* else it was a non-inode node or already removed, so don't bother */

		jffs2_free_raw_node_ref(ref);
	}
	jeb->last_node = NULL;
}

static int jffs2_block_check_erase(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb, uint32_t *bad_offset)
{
	void *ebuf;
	uint32_t ofs;
	size_t retlen;
	int ret = -EIO;

	ebuf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!ebuf) {
		printk(KERN_WARNING "Failed to allocate page buffer for verifying erase at 0x%08x. Refiling\n", jeb->offset);
		return -EAGAIN;
	}

	D1(printk(KERN_DEBUG "Verifying erase at 0x%08x\n", jeb->offset));

	for (ofs = jeb->offset; ofs < jeb->offset + c->sector_size; ) {
		uint32_t readlen = min((uint32_t)PAGE_SIZE, jeb->offset + c->sector_size - ofs);
		int i;

		*bad_offset = ofs;

		ret = jffs2_flash_read(c, ofs, readlen, &retlen, ebuf);
		if (ret) {
			printk(KERN_WARNING "Read of newly-erased block at 0x%08x failed: %d. Putting on bad_list\n", ofs, ret);
			goto fail;
		}
		if (retlen != readlen) {
			printk(KERN_WARNING "Short read from newly-erased block at 0x%08x. Wanted %d, got %zd\n", ofs, readlen, retlen);
			goto fail;
		}
		for (i=0; i<readlen; i += sizeof(unsigned long)) {
			/* It's OK. We know it's properly aligned */
			unsigned long *datum = ebuf + i;
			if (*datum + 1) {
				*bad_offset += i;
				printk(KERN_WARNING "Newly-erased block contained word 0x%lx at offset 0x%08x\n", *datum, *bad_offset);
				goto fail;
			}
		}
		ofs += readlen;
		cond_resched();
	}
	ret = 0;
fail:
	kfree(ebuf);
	return ret;
}

static void jffs2_mark_erased_block(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb)
{
	struct jffs2_raw_node_ref *marker_ref = NULL;
	size_t retlen;
	int ret;
	uint32_t bad_offset;

	switch (jffs2_block_check_erase(c, jeb, &bad_offset)) {
	case -EAGAIN:	goto refile;
	case -EIO:	goto filebad;
	}

	/* Write the erase complete marker */
	D1(printk(KERN_DEBUG "Writing erased marker to block at 0x%08x\n", jeb->offset));
	bad_offset = jeb->offset;

	/* Cleanmarker in oob area or no cleanmarker at all ? */
	if (jffs2_cleanmarker_oob(c) || c->cleanmarker_size == 0) {

		if (jffs2_cleanmarker_oob(c)) {
			if (jffs2_write_nand_cleanmarker(c, jeb))
				goto filebad;
		}

		/* Everything else got zeroed before the erase */
		jeb->free_size = c->sector_size;
	} else {

		struct kvec vecs[1];
		struct jffs2_unknown_node marker = {
			.magic =	cpu_to_je16(JFFS2_MAGIC_BITMASK),
			.nodetype =	cpu_to_je16(JFFS2_NODETYPE_CLEANMARKER),
			.totlen =	cpu_to_je32(c->cleanmarker_size)
		};

		marker_ref = jffs2_alloc_raw_node_ref();
		if (!marker_ref) {
			printk(KERN_WARNING "Failed to allocate raw node ref for clean marker. Refiling\n");
			goto refile;
		}

		marker.hdr_crc = cpu_to_je32(crc32(0, &marker, sizeof(struct jffs2_unknown_node)-4));

		vecs[0].iov_base = (unsigned char *) &marker;
		vecs[0].iov_len = sizeof(marker);
		ret = jffs2_flash_direct_writev(c, vecs, 1, jeb->offset, &retlen);

		if (ret || retlen != sizeof(marker)) {
			if (ret)
				printk(KERN_WARNING "Write clean marker to block at 0x%08x failed: %d\n",
				       jeb->offset, ret);
			else
				printk(KERN_WARNING "Short write to newly-erased block at 0x%08x: Wanted %zd, got %zd\n",
				       jeb->offset, sizeof(marker), retlen);

			jffs2_free_raw_node_ref(marker_ref);
			goto filebad;
		}

		/* Everything else got zeroed before the erase */
		jeb->free_size = c->sector_size;

		marker_ref->next_in_ino = NULL;
		marker_ref->flash_offset = jeb->offset | REF_NORMAL;

		jffs2_link_node_ref(c, jeb, marker_ref, c->cleanmarker_size);
	}

	spin_lock(&c->erase_completion_lock);
	c->erasing_size -= c->sector_size;
	c->free_size += jeb->free_size;
	c->used_size += jeb->used_size;

	jffs2_dbg_acct_sanity_check_nolock(c,jeb);
	jffs2_dbg_acct_paranoia_check_nolock(c, jeb);

	list_add_tail(&jeb->list, &c->free_list);
	c->nr_erasing_blocks--;
	c->nr_free_blocks++;
	spin_unlock(&c->erase_completion_lock);
	wake_up(&c->erase_wait);
	return;

filebad:
	spin_lock(&c->erase_completion_lock);
	/* Stick it on a list (any list) so erase_failed can take it
	   right off again.  Silly, but shouldn't happen often. */
	list_add(&jeb->list, &c->erasing_list);
	spin_unlock(&c->erase_completion_lock);
	jffs2_erase_failed(c, jeb, bad_offset);
	return;

refile:
	/* Stick it back on the list from whence it came and come back later */
	jffs2_erase_pending_trigger(c);
	spin_lock(&c->erase_completion_lock);
	list_add(&jeb->list, &c->erase_complete_list);
	spin_unlock(&c->erase_completion_lock);
	return;
}
