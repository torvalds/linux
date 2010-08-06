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
		mutex_lock(&c->erase_free_sem);
		spin_lock(&c->erase_completion_lock);
		list_move(&jeb->list, &c->erase_pending_list);
		c->erasing_size -= c->sector_size;
		c->dirty_size += c->sector_size;
		jeb->dirty_size = c->sector_size;
		spin_unlock(&c->erase_completion_lock);
		mutex_unlock(&c->erase_free_sem);
		return;
	}

	memset(instr, 0, sizeof(*instr));

	instr->mtd = c->mtd;
	instr->addr = jeb->offset;
	instr->len = c->sector_size;
	instr->callback = jffs2_erase_callback;
	instr->priv = (unsigned long)(&instr[1]);
	instr->fail_addr = MTD_FAIL_ADDR_UNKNOWN;

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
		mutex_lock(&c->erase_free_sem);
		spin_lock(&c->erase_completion_lock);
		list_move(&jeb->list, &c->erase_pending_list);
		c->erasing_size -= c->sector_size;
		c->dirty_size += c->sector_size;
		jeb->dirty_size = c->sector_size;
		spin_unlock(&c->erase_completion_lock);
		mutex_unlock(&c->erase_free_sem);
		return;
	}

	if (ret == -EROFS)
		printk(KERN_WARNING "Erase at 0x%08x failed immediately: -EROFS. Is the sector locked?\n", jeb->offset);
	else
		printk(KERN_WARNING "Erase at 0x%08x failed immediately: errno %d\n", jeb->offset, ret);

	jffs2_erase_failed(c, jeb, bad_offset);
}

int jffs2_erase_pending_blocks(struct jffs2_sb_info *c, int count)
{
	struct jffs2_eraseblock *jeb;
	int work_done = 0;

	mutex_lock(&c->erase_free_sem);

	spin_lock(&c->erase_completion_lock);

	while (!list_empty(&c->erase_complete_list) ||
	       !list_empty(&c->erase_pending_list)) {

		if (!list_empty(&c->erase_complete_list)) {
			jeb = list_entry(c->erase_complete_list.next, struct jffs2_eraseblock, list);
			list_move(&jeb->list, &c->erase_checking_list);
			spin_unlock(&c->erase_completion_lock);
			mutex_unlock(&c->erase_free_sem);
			jffs2_mark_erased_block(c, jeb);

			work_done++;
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
			jffs2_free_jeb_node_refs(c, jeb);
			list_add(&jeb->list, &c->erasing_list);
			spin_unlock(&c->erase_completion_lock);
			mutex_unlock(&c->erase_free_sem);

			jffs2_erase_block(c, jeb);

		} else {
			BUG();
		}

		/* Be nice */
		yield();
		mutex_lock(&c->erase_free_sem);
		spin_lock(&c->erase_completion_lock);
	}

	spin_unlock(&c->erase_completion_lock);
	mutex_unlock(&c->erase_free_sem);
 done:
	D1(printk(KERN_DEBUG "jffs2_erase_pending_blocks completed\n"));
	return work_done;
}

static void jffs2_erase_succeeded(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb)
{
	D1(printk(KERN_DEBUG "Erase completed successfully at 0x%08x\n", jeb->offset));
	mutex_lock(&c->erase_free_sem);
	spin_lock(&c->erase_completion_lock);
	list_move_tail(&jeb->list, &c->erase_complete_list);
	/* Wake the GC thread to mark them clean */
	jffs2_garbage_collect_trigger(c);
	spin_unlock(&c->erase_completion_lock);
	mutex_unlock(&c->erase_free_sem);
	wake_up(&c->erase_wait);
}

static void jffs2_erase_failed(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb, uint32_t bad_offset)
{
	/* For NAND, if the failure did not occur at the device level for a
	   specific physical page, don't bother updating the bad block table. */
	if (jffs2_cleanmarker_oob(c) && (bad_offset != (uint32_t)MTD_FAIL_ADDR_UNKNOWN)) {
		/* We had a device-level failure to erase.  Let's see if we've
		   failed too many times. */
		if (!jffs2_write_nand_badblock(c, jeb, bad_offset)) {
			/* We'd like to give this block another try. */
			mutex_lock(&c->erase_free_sem);
			spin_lock(&c->erase_completion_lock);
			list_move(&jeb->list, &c->erase_pending_list);
			c->erasing_size -= c->sector_size;
			c->dirty_size += c->sector_size;
			jeb->dirty_size = c->sector_size;
			spin_unlock(&c->erase_completion_lock);
			mutex_unlock(&c->erase_free_sem);
			return;
		}
	}

	mutex_lock(&c->erase_free_sem);
	spin_lock(&c->erase_completion_lock);
	c->erasing_size -= c->sector_size;
	c->bad_size += c->sector_size;
	list_move(&jeb->list, &c->bad_list);
	c->nr_erasing_blocks--;
	spin_unlock(&c->erase_completion_lock);
	mutex_unlock(&c->erase_free_sem);
	wake_up(&c->erase_wait);
}

#ifndef __ECOS
static void jffs2_erase_callback(struct erase_info *instr)
{
	struct erase_priv_struct *priv = (void *)instr->priv;

	if(instr->state != MTD_ERASE_DONE) {
		printk(KERN_WARNING "Erase at 0x%08llx finished, but state != MTD_ERASE_DONE. State is 0x%x instead.\n",
			(unsigned long long)instr->addr, instr->state);
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
		JFFS2_WARNING("inode_cache/xattr_datum/xattr_ref"
			      " not found in remove_node_refs()!!\n");
		return;
	}

	D1(printk(KERN_DEBUG "Removed nodes in range 0x%08x-0x%08x from ino #%u\n",
		  jeb->offset, jeb->offset + c->sector_size, ic->ino));

	D2({
		int i=0;
		struct jffs2_raw_node_ref *this;
		printk(KERN_DEBUG "After remove_node_refs_from_ino_list: \n");

		this = ic->nodes;

		printk(KERN_DEBUG);
		while(this) {
			printk(KERN_CONT "0x%08x(%d)->",
			       ref_offset(this), ref_flags(this));
			if (++i == 5) {
				printk(KERN_DEBUG);
				i=0;
			}
			this = this->next_in_ino;
		}
		printk(KERN_CONT "\n");
	});

	switch (ic->class) {
#ifdef CONFIG_JFFS2_FS_XATTR
		case RAWNODE_CLASS_XATTR_DATUM:
			jffs2_release_xattr_datum(c, (struct jffs2_xattr_datum *)ic);
			break;
		case RAWNODE_CLASS_XATTR_REF:
			jffs2_release_xattr_ref(c, (struct jffs2_xattr_ref *)ic);
			break;
#endif
		default:
			if (ic->nodes == (void *)ic && ic->pino_nlink == 0)
				jffs2_del_ino_cache(c, ic);
	}
}

void jffs2_free_jeb_node_refs(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb)
{
	struct jffs2_raw_node_ref *block, *ref;
	D1(printk(KERN_DEBUG "Freeing all node refs for eraseblock offset 0x%08x\n", jeb->offset));

	block = ref = jeb->first_node;

	while (ref) {
		if (ref->flash_offset == REF_LINK_NODE) {
			ref = ref->next_in_ino;
			jffs2_free_refblock(block);
			block = ref;
			continue;
		}
		if (ref->flash_offset != REF_EMPTY_NODE && ref->next_in_ino)
			jffs2_remove_node_refs_from_ino_list(c, ref, jeb);
		/* else it was a non-inode node or already removed, so don't bother */

		ref++;
	}
	jeb->first_node = jeb->last_node = NULL;
}

static int jffs2_block_check_erase(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb, uint32_t *bad_offset)
{
	void *ebuf;
	uint32_t ofs;
	size_t retlen;
	int ret = -EIO;

	if (c->mtd->point) {
		unsigned long *wordebuf;

		ret = c->mtd->point(c->mtd, jeb->offset, c->sector_size,
				    &retlen, &ebuf, NULL);
		if (ret) {
			D1(printk(KERN_DEBUG "MTD point failed %d\n", ret));
			goto do_flash_read;
		}
		if (retlen < c->sector_size) {
			/* Don't muck about if it won't let us point to the whole erase sector */
			D1(printk(KERN_DEBUG "MTD point returned len too short: 0x%zx\n", retlen));
			c->mtd->unpoint(c->mtd, jeb->offset, retlen);
			goto do_flash_read;
		}
		wordebuf = ebuf-sizeof(*wordebuf);
		retlen /= sizeof(*wordebuf);
		do {
		   if (*++wordebuf != ~0)
			   break;
		} while(--retlen);
		c->mtd->unpoint(c->mtd, jeb->offset, c->sector_size);
		if (retlen) {
			printk(KERN_WARNING "Newly-erased block contained word 0x%lx at offset 0x%08tx\n",
			       *wordebuf, jeb->offset + c->sector_size-retlen*sizeof(*wordebuf));
			return -EIO;
		}
		return 0;
	}
 do_flash_read:
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

		ret = c->mtd->read(c->mtd, ofs, readlen, &retlen, ebuf);
		if (ret) {
			printk(KERN_WARNING "Read of newly-erased block at 0x%08x failed: %d. Putting on bad_list\n", ofs, ret);
			ret = -EIO;
			goto fail;
		}
		if (retlen != readlen) {
			printk(KERN_WARNING "Short read from newly-erased block at 0x%08x. Wanted %d, got %zd\n", ofs, readlen, retlen);
			ret = -EIO;
			goto fail;
		}
		for (i=0; i<readlen; i += sizeof(unsigned long)) {
			/* It's OK. We know it's properly aligned */
			unsigned long *datum = ebuf + i;
			if (*datum + 1) {
				*bad_offset += i;
				printk(KERN_WARNING "Newly-erased block contained word 0x%lx at offset 0x%08x\n", *datum, *bad_offset);
				ret = -EIO;
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
	size_t retlen;
	int ret;
	uint32_t uninitialized_var(bad_offset);

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
	} else {

		struct kvec vecs[1];
		struct jffs2_unknown_node marker = {
			.magic =	cpu_to_je16(JFFS2_MAGIC_BITMASK),
			.nodetype =	cpu_to_je16(JFFS2_NODETYPE_CLEANMARKER),
			.totlen =	cpu_to_je32(c->cleanmarker_size)
		};

		jffs2_prealloc_raw_node_refs(c, jeb, 1);

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

			goto filebad;
		}
	}
	/* Everything else got zeroed before the erase */
	jeb->free_size = c->sector_size;

	mutex_lock(&c->erase_free_sem);
	spin_lock(&c->erase_completion_lock);

	c->erasing_size -= c->sector_size;
	c->free_size += c->sector_size;

	/* Account for cleanmarker now, if it's in-band */
	if (c->cleanmarker_size && !jffs2_cleanmarker_oob(c))
		jffs2_link_node_ref(c, jeb, jeb->offset | REF_NORMAL, c->cleanmarker_size, NULL);

	list_move_tail(&jeb->list, &c->free_list);
	c->nr_erasing_blocks--;
	c->nr_free_blocks++;

	jffs2_dbg_acct_sanity_check_nolock(c, jeb);
	jffs2_dbg_acct_paranoia_check_nolock(c, jeb);

	spin_unlock(&c->erase_completion_lock);
	mutex_unlock(&c->erase_free_sem);
	wake_up(&c->erase_wait);
	return;

filebad:
	jffs2_erase_failed(c, jeb, bad_offset);
	return;

refile:
	/* Stick it back on the list from whence it came and come back later */
	mutex_lock(&c->erase_free_sem);
	spin_lock(&c->erase_completion_lock);
	jffs2_garbage_collect_trigger(c);
	list_move(&jeb->list, &c->erase_complete_list);
	spin_unlock(&c->erase_completion_lock);
	mutex_unlock(&c->erase_free_sem);
	return;
}
