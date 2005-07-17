/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001-2003 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: debug.h,v 1.1 2005/07/17 06:56:20 dedekind Exp $
 *
 */
#ifndef _JFFS2_DEBUG_H_
#define _JFFS2_DEBUG_H_

#include <linux/config.h>

#ifndef CONFIG_JFFS2_FS_DEBUG
#define CONFIG_JFFS2_FS_DEBUG 1
#endif

#if CONFIG_JFFS2_FS_DEBUG > 0
#define JFFS2_DBG_PARANOIA_CHECKS
#define D1(x) x
#else
#define D1(x)
#endif

#if CONFIG_JFFS2_FS_DEBUG > 1
#define D2(x) x
#else
#define D2(x)
#endif

/* Enable JFFS2 sanity checks */
#define JFFS2_DBG_SANITY_CHECKS

#if CONFIG_JFFS2_FS_DEBUG > 0
void
jffs2_dbg_dump_block_lists(struct jffs2_sb_info *c);

void
jffs2_dbg_dump_node_refs(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb);

void
jffs2_dbg_dump_fragtree(struct jffs2_inode_info *f);

void
jffs2_dbg_dump_buffer(char *buf, int len, uint32_t offs);
#endif

#ifdef JFFS2_DBG_PARANOIA_CHECKS
void
jffs2_dbg_fragtree_paranoia_check(struct jffs2_inode_info *f);

void
jffs2_dbg_acct_paranoia_check(struct jffs2_sb_info *c,
			      struct jffs2_eraseblock *jeb);

void
jffs2_dbg_prewrite_paranoia_check(struct jffs2_sb_info *c,
				  uint32_t ofs, int len);
#else
#define jffs2_dbg_fragtree_paranoia_check(f)
#define jffs2_dbg_acct_paranoia_check(c, jeb)
#define jffs2_dbg_prewrite_paranoia_check(c, ofs, len)
#endif /* !JFFS2_PARANOIA_CHECKS */

#ifdef JFFS2_DBG_SANITY_CHECKS
/*
 * Check the space accounting of the file system and of
 * the JFFS3 erasable block 'jeb'.
 */
static inline void
jffs2_dbg_acct_sanity_check(struct jffs2_sb_info *c,
			    struct jffs2_eraseblock *jeb)
{
	if (unlikely(jeb && jeb->used_size + jeb->dirty_size +
			jeb->free_size + jeb->wasted_size +
			jeb->unchecked_size != c->sector_size)) {
		printk(KERN_ERR "Eeep. Space accounting for block at 0x%08x is screwed\n", jeb->offset);
		printk(KERN_ERR "free %#08x + dirty %#08x + used %#08x + wasted %#08x + unchecked "
				"%#08x != total %#08x\n", jeb->free_size, jeb->dirty_size, jeb->used_size,
				jeb->wasted_size, jeb->unchecked_size, c->sector_size);
		BUG();
	}

	if (unlikely(c->used_size + c->dirty_size + c->free_size + c->erasing_size + c->bad_size
				+ c->wasted_size + c->unchecked_size != c->flash_size)) {
		printk(KERN_ERR "Eeep. Space accounting superblock info is screwed\n");
		printk(KERN_ERR "free %#08x + dirty %#08x + used %#08x + erasing %#08x + bad %#08x + "
				"wasted %#08x + unchecked %#08x != total %#08x\n",
				c->free_size, c->dirty_size, c->used_size, c->erasing_size, c->bad_size,
				c->wasted_size, c->unchecked_size, c->flash_size);
		BUG();
	}
}
#else
static inline void
jffs2_dbg_acct_sanity_check(struct jffs2_sb_info *c,
			    struct jffs2_eraseblock *jeb);
#endif /* !JFFS2_DBG_SANITY_CHECKS */

#endif /* _JFFS2_DEBUG_H_ */
