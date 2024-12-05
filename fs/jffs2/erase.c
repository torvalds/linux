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
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/compiler.h>
#include <linux/crc32.h>
#include <linux/sched.h>
#include <linux/pagemap.h>
#include "nodelist.h"

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

	jffs2_dbg(1, "%s(): erase block %#08x (range %#08x-%#08x)\n",
		  __func__,
		  jeb->offset, jeb->offset, jeb->offset + c->sector_size);
	instr = kzalloc(sizeof(struct erase_info), GFP_KERNEL);
	if (!instr) {
		pr_warn("kzalloc for struct erase_info in jffs2_erase_block failed. Refiling block for later\n");
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

	instr->addr = jeb->offset;
	instr->len = c->sector_size;

	ret = mtd_erase(c->mtd, instr);
	if (!ret) {
		jffs2_erase_succeeded(c, jeb);
		kfree(instr);
		return;
	}

	bad_offset = instr->fail_addr;
	kfree(instr);
#endif /* __ECOS */

	if (ret == -ENOMEM || ret == -EAGAIN) {
		/* Erase failed immediately. Refile it on the list */
		jffs2_dbg(1, "Erase at 0x%08x failed: %d. Refiling on erase_pending_list\n",
			  jeb->offset, ret);
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
		pr_warn("Erase at 0x%08x failed immediately: -EROFS. Is the sector locked?\n",
			jeb->offset);
	else
		pr_warn("Erase at 0x%08x failed immediately: errno %d\n",
			jeb->offset, ret);

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
				jffs2_dbg(1, "Count reached. jffs2_erase_pending_blocks leaving\n");
				goto done;
			}

		} else if (!list_empty(&c->erase_pending_list)) {
			jeb = list_entry(c->erase_pending_list.next, struct jffs2_eraseblock, list);
			jffs2_dbg(1, "Starting erase of pending block 0x%08x\n",
				  jeb->offset);
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
		cond_resched();
		mutex_lock(&c->erase_free_sem);
		spin_lock(&c->erase_completion_lock);
	}

	spin_unlock(&c->erase_completion_lock);
	mutex_unlock(&c->erase_free_sem);
 done:
	jffs2_dbg(1, "jffs2_erase_pending_blocks completed\n");
	return work_done;
}

static void jffs2_erase_succeeded(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb)
{
	jffs2_dbg(1, "Erase completed successfully at 0x%08x\n", jeb->offset);
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

	jffs2_dbg(1, "Removed nodes in range 0x%08x-0x%08x from ino #%u\n",
		  jeb->offset, jeb->offset + c->sector_size, ic->ino);

	D2({
		int i=0;
		struct jffs2_raw_node_ref *this;
		printk(KERN_DEBUG "After remove_node_refs_from_ino_list: \n");

		this = ic->nodes;

		printk(KERN_DEBUG);
		while(this) {
			pr_cont("0x%08x(%d)->",
			       ref_offset(this), ref_flags(this));
			if (++i == 5) {
				printk(KERN_DEBUG);
				i=0;
			}
			this = this->next_in_ino;
		}
		pr_cont("\n");
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
	jffs2_dbg(1, "Freeing all node refs for eraseblock offset 0x%08x\n",
		  jeb->offset);

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
	int ret;
	unsigned long *wordebuf;

	ret = mtd_point(c->mtd, jeb->offset, c->sector_size, &retlen,
			&ebuf, NULL);
	if (ret != -EOPNOTSUPP) {
		if (ret) {
			jffs2_dbg(1, "MTD point failed %d\n", ret);
			goto do_flash_read;
		}
		if (retlen < c->sector_size) {
			/* Don't muck about if it won't let us point to the whole erase sector */
			jffs2_dbg(1, "MTD point returned len too short: 0x%zx\n",
				  retlen);
			mtd_unpoint(c->mtd, jeb->offset, retlen);
			goto do_flash_read;
		}
		wordebuf = ebuf-sizeof(*wordebuf);
		retlen /= sizeof(*wordebuf);
		do {
		   if (*++wordebuf != ~0)
			   break;
		} while(--retlen);
		mtd_unpoint(c->mtd, jeb->offset, c->sector_size);
		if (retlen) {
			*bad_offset = jeb->offset + c->sector_size - retlen * sizeof(*wordebuf);
			pr_warn("Newly-erased block contained word 0x%lx at offset 0x%08x\n",
				*wordebuf, *bad_offset);
			return -EIO;
		}
		return 0;
	}
 do_flash_read:
	ebuf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!ebuf) {
		pr_warn("Failed to allocate page buffer for verifying erase at 0x%08x. Refiling\n",
			jeb->offset);
		return -EAGAIN;
	}

	jffs2_dbg(1, "Verifying erase at 0x%08x\n", jeb->offset);

	for (ofs = jeb->offset; ofs < jeb->offset + c->sector_size; ) {
		uint32_t readlen = min((uint32_t)PAGE_SIZE, jeb->offset + c->sector_size - ofs);
		int i;

		*bad_offset = ofs;

		ret = mtd_read(c->mtd, ofs, readlen, &retlen, ebuf);
		if (ret) {
			pr_warn("Read of newly-erased block at 0x%08x failed: %d. Putting on bad_list\n",
				ofs, ret);
			ret = -EIO;
			goto fail;
		}
		if (retlen != readlen) {
			pr_warn("Short read from newly-erased block at 0x%08x. Wanted %d, got %zd\n",
				ofs, readlen, retlen);
			ret = -EIO;
			goto fail;
		}
		for (i=0; i<readlen; i += sizeof(unsigned long)) {
			/* It's OK. We know it's properly aligned */
			unsigned long *datum = ebuf + i;
			if (*datum + 1) {
				*bad_offset += i;
				pr_warn("Newly-erased block contained word 0x%lx at offset 0x%08x\n",
					*datum, *bad_offset);
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
	uint32_t bad_offset;

	switch (jffs2_block_check_erase(c, jeb, &bad_offset)) {
	case -EAGAIN:	goto refile;
	case -EIO:	goto filebad;
	}

	/* Write the erase complete marker */
	jffs2_dbg(1, "Writing erased marker to block at 0x%08x\n", jeb->offset);
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
				pr_warn("Write clean marker to block at 0x%08x failed: %d\n",
				       jeb->offset, ret);
			else
				pr_warn("Short write to newly-erased block at 0x%08x: Wanted %zd, got %zd\n",
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
