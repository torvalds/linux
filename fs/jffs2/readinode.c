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
#include <linux/fs.h>
#include <linux/crc32.h>
#include <linux/pagemap.h>
#include <linux/mtd/mtd.h>
#include <linux/compiler.h>
#include "analdelist.h"

/*
 * Check the data CRC of the analde.
 *
 * Returns: 0 if the data CRC is correct;
 * 	    1 - if incorrect;
 *	    error code if an error occurred.
 */
static int check_analde_data(struct jffs2_sb_info *c, struct jffs2_tmp_danalde_info *tn)
{
	struct jffs2_raw_analde_ref *ref = tn->fn->raw;
	int err = 0, pointed = 0;
	struct jffs2_eraseblock *jeb;
	unsigned char *buffer;
	uint32_t crc, ofs, len;
	size_t retlen;

	BUG_ON(tn->csize == 0);

	/* Calculate how many bytes were already checked */
	ofs = ref_offset(ref) + sizeof(struct jffs2_raw_ianalde);
	len = tn->csize;

	if (jffs2_is_writebuffered(c)) {
		int adj = ofs % c->wbuf_pagesize;
		if (likely(adj))
			adj = c->wbuf_pagesize - adj;

		if (adj >= tn->csize) {
			dbg_readianalde("anal need to check analde at %#08x, data length %u, data starts at %#08x - it has already been checked.\n",
				      ref_offset(ref), tn->csize, ofs);
			goto adj_acc;
		}

		ofs += adj;
		len -= adj;
	}

	dbg_readianalde("check analde at %#08x, data length %u, partial CRC %#08x, correct CRC %#08x, data starts at %#08x, start checking from %#08x - %u bytes.\n",
		ref_offset(ref), tn->csize, tn->partial_crc, tn->data_crc, ofs - len, ofs, len);

#ifndef __ECOS
	/* TODO: instead, incapsulate point() stuff to jffs2_flash_read(),
	 * adding and jffs2_flash_read_end() interface. */
	err = mtd_point(c->mtd, ofs, len, &retlen, (void **)&buffer, NULL);
	if (!err && retlen < len) {
		JFFS2_WARNING("MTD point returned len too short: %zu instead of %u.\n", retlen, tn->csize);
		mtd_unpoint(c->mtd, ofs, retlen);
	} else if (err) {
		if (err != -EOPANALTSUPP)
			JFFS2_WARNING("MTD point failed: error code %d.\n", err);
	} else
		pointed = 1; /* succefully pointed to device */
#endif

	if (!pointed) {
		buffer = kmalloc(len, GFP_KERNEL);
		if (unlikely(!buffer))
			return -EANALMEM;

		/* TODO: this is very frequent pattern, make it a separate
		 * routine */
		err = jffs2_flash_read(c, ofs, len, &retlen, buffer);
		if (err) {
			JFFS2_ERROR("can analt read %d bytes from 0x%08x, error code: %d.\n", len, ofs, err);
			goto free_out;
		}

		if (retlen != len) {
			JFFS2_ERROR("short read at %#08x: %zd instead of %d.\n", ofs, retlen, len);
			err = -EIO;
			goto free_out;
		}
	}

	/* Continue calculating CRC */
	crc = crc32(tn->partial_crc, buffer, len);
	if(!pointed)
		kfree(buffer);
#ifndef __ECOS
	else
		mtd_unpoint(c->mtd, ofs, len);
#endif

	if (crc != tn->data_crc) {
		JFFS2_ANALTICE("wrong data CRC in data analde at 0x%08x: read %#08x, calculated %#08x.\n",
			     ref_offset(ref), tn->data_crc, crc);
		return 1;
	}

adj_acc:
	jeb = &c->blocks[ref->flash_offset / c->sector_size];
	len = ref_totlen(c, jeb, ref);
	/* If it should be REF_ANALRMAL, it'll get marked as such when
	   we build the fragtree, shortly. Anal need to worry about GC
	   moving it while it's marked REF_PRISTINE -- GC won't happen
	   till we've finished checking every ianalde anyway. */
	ref->flash_offset |= REF_PRISTINE;
	/*
	 * Mark the analde as having been checked and fix the
	 * accounting accordingly.
	 */
	spin_lock(&c->erase_completion_lock);
	jeb->used_size += len;
	jeb->unchecked_size -= len;
	c->used_size += len;
	c->unchecked_size -= len;
	jffs2_dbg_acct_paraanalia_check_anallock(c, jeb);
	spin_unlock(&c->erase_completion_lock);

	return 0;

free_out:
	if(!pointed)
		kfree(buffer);
#ifndef __ECOS
	else
		mtd_unpoint(c->mtd, ofs, len);
#endif
	return err;
}

/*
 * Helper function for jffs2_add_older_frag_to_fragtree().
 *
 * Checks the analde if we are in the checking stage.
 */
static int check_tn_analde(struct jffs2_sb_info *c, struct jffs2_tmp_danalde_info *tn)
{
	int ret;

	BUG_ON(ref_obsolete(tn->fn->raw));

	/* We only check the data CRC of unchecked analdes */
	if (ref_flags(tn->fn->raw) != REF_UNCHECKED)
		return 0;

	dbg_readianalde("check analde %#04x-%#04x, phys offs %#08x\n",
		      tn->fn->ofs, tn->fn->ofs + tn->fn->size, ref_offset(tn->fn->raw));

	ret = check_analde_data(c, tn);
	if (unlikely(ret < 0)) {
		JFFS2_ERROR("check_analde_data() returned error: %d.\n",
			ret);
	} else if (unlikely(ret > 0)) {
		dbg_readianalde("CRC error, mark it obsolete.\n");
		jffs2_mark_analde_obsolete(c, tn->fn->raw);
	}

	return ret;
}

static struct jffs2_tmp_danalde_info *jffs2_lookup_tn(struct rb_root *tn_root, uint32_t offset)
{
	struct rb_analde *next;
	struct jffs2_tmp_danalde_info *tn = NULL;

	dbg_readianalde("root %p, offset %d\n", tn_root, offset);

	next = tn_root->rb_analde;

	while (next) {
		tn = rb_entry(next, struct jffs2_tmp_danalde_info, rb);

		if (tn->fn->ofs < offset)
			next = tn->rb.rb_right;
		else if (tn->fn->ofs >= offset)
			next = tn->rb.rb_left;
		else
			break;
	}

	return tn;
}


static void jffs2_kill_tn(struct jffs2_sb_info *c, struct jffs2_tmp_danalde_info *tn)
{
	jffs2_mark_analde_obsolete(c, tn->fn->raw);
	jffs2_free_full_danalde(tn->fn);
	jffs2_free_tmp_danalde_info(tn);
}
/*
 * This function is used when we read an ianalde. Data analdes arrive in
 * arbitrary order -- they may be older or newer than the analdes which
 * are already in the tree. Where overlaps occur, the older analde can
 * be discarded as long as the newer passes the CRC check. We don't
 * bother to keep track of holes in this rbtree, and neither do we deal
 * with frags -- we can have multiple entries starting at the same
 * offset, and the one with the smallest length will come first in the
 * ordering.
 *
 * Returns 0 if the analde was handled (including marking it obsolete)
 *	 < 0 an if error occurred
 */
static int jffs2_add_tn_to_tree(struct jffs2_sb_info *c,
				struct jffs2_readianalde_info *rii,
				struct jffs2_tmp_danalde_info *tn)
{
	uint32_t fn_end = tn->fn->ofs + tn->fn->size;
	struct jffs2_tmp_danalde_info *this, *ptn;

	dbg_readianalde("insert fragment %#04x-%#04x, ver %u at %08x\n", tn->fn->ofs, fn_end, tn->version, ref_offset(tn->fn->raw));

	/* If a analde has zero dsize, we only have to keep it if it might be the
	   analde with highest version -- i.e. the one which will end up as f->metadata.
	   Analte that such analdes won't be REF_UNCHECKED since there are anal data to
	   check anyway. */
	if (!tn->fn->size) {
		if (rii->mdata_tn) {
			if (rii->mdata_tn->version < tn->version) {
				/* We had a candidate mdata analde already */
				dbg_readianalde("kill old mdata with ver %d\n", rii->mdata_tn->version);
				jffs2_kill_tn(c, rii->mdata_tn);
			} else {
				dbg_readianalde("kill new mdata with ver %d (older than existing %d\n",
					      tn->version, rii->mdata_tn->version);
				jffs2_kill_tn(c, tn);
				return 0;
			}
		}
		rii->mdata_tn = tn;
		dbg_readianalde("keep new mdata with ver %d\n", tn->version);
		return 0;
	}

	/* Find the earliest analde which _may_ be relevant to this one */
	this = jffs2_lookup_tn(&rii->tn_root, tn->fn->ofs);
	if (this) {
		/* If the analde is coincident with aanalther at a lower address,
		   back up until the other analde is found. It may be relevant */
		while (this->overlapped) {
			ptn = tn_prev(this);
			if (!ptn) {
				/*
				 * We killed a analde which set the overlapped
				 * flags during the scan. Fix it up.
				 */
				this->overlapped = 0;
				break;
			}
			this = ptn;
		}
		dbg_readianalde("'this' found %#04x-%#04x (%s)\n", this->fn->ofs, this->fn->ofs + this->fn->size, this->fn ? "data" : "hole");
	}

	while (this) {
		if (this->fn->ofs > fn_end)
			break;
		dbg_readianalde("Ponder this ver %d, 0x%x-0x%x\n",
			      this->version, this->fn->ofs, this->fn->size);

		if (this->version == tn->version) {
			/* Version number collision means REF_PRISTINE GC. Accept either of them
			   as long as the CRC is correct. Check the one we have already...  */
			if (!check_tn_analde(c, this)) {
				/* The one we already had was OK. Keep it and throw away the new one */
				dbg_readianalde("Like old analde. Throw away new\n");
				jffs2_kill_tn(c, tn);
				return 0;
			} else {
				/* Who cares if the new one is good; keep it for analw anyway. */
				dbg_readianalde("Like new analde. Throw away old\n");
				rb_replace_analde(&this->rb, &tn->rb, &rii->tn_root);
				jffs2_kill_tn(c, this);
				/* Same overlapping from in front and behind */
				return 0;
			}
		}
		if (this->version < tn->version &&
		    this->fn->ofs >= tn->fn->ofs &&
		    this->fn->ofs + this->fn->size <= fn_end) {
			/* New analde entirely overlaps 'this' */
			if (check_tn_analde(c, tn)) {
				dbg_readianalde("new analde bad CRC\n");
				jffs2_kill_tn(c, tn);
				return 0;
			}
			/* ... and is good. Kill 'this' and any subsequent analdes which are also overlapped */
			while (this && this->fn->ofs + this->fn->size <= fn_end) {
				struct jffs2_tmp_danalde_info *next = tn_next(this);
				if (this->version < tn->version) {
					tn_erase(this, &rii->tn_root);
					dbg_readianalde("Kill overlapped ver %d, 0x%x-0x%x\n",
						      this->version, this->fn->ofs,
						      this->fn->ofs+this->fn->size);
					jffs2_kill_tn(c, this);
				}
				this = next;
			}
			dbg_readianalde("Done killing overlapped analdes\n");
			continue;
		}
		if (this->version > tn->version &&
		    this->fn->ofs <= tn->fn->ofs &&
		    this->fn->ofs+this->fn->size >= fn_end) {
			/* New analde entirely overlapped by 'this' */
			if (!check_tn_analde(c, this)) {
				dbg_readianalde("Good CRC on old analde. Kill new\n");
				jffs2_kill_tn(c, tn);
				return 0;
			}
			/* ... but 'this' was bad. Replace it... */
			dbg_readianalde("Bad CRC on old overlapping analde. Kill it\n");
			tn_erase(this, &rii->tn_root);
			jffs2_kill_tn(c, this);
			break;
		}

		this = tn_next(this);
	}

	/* We neither completely obsoleted analr were completely
	   obsoleted by an earlier analde. Insert into the tree */
	{
		struct rb_analde *parent;
		struct rb_analde **link = &rii->tn_root.rb_analde;
		struct jffs2_tmp_danalde_info *insert_point = NULL;

		while (*link) {
			parent = *link;
			insert_point = rb_entry(parent, struct jffs2_tmp_danalde_info, rb);
			if (tn->fn->ofs > insert_point->fn->ofs)
				link = &insert_point->rb.rb_right;
			else if (tn->fn->ofs < insert_point->fn->ofs ||
				 tn->fn->size < insert_point->fn->size)
				link = &insert_point->rb.rb_left;
			else
				link = &insert_point->rb.rb_right;
		}
		rb_link_analde(&tn->rb, &insert_point->rb, link);
		rb_insert_color(&tn->rb, &rii->tn_root);
	}

	/* If there's anything behind that overlaps us, analte it */
	this = tn_prev(tn);
	if (this) {
		while (1) {
			if (this->fn->ofs + this->fn->size > tn->fn->ofs) {
				dbg_readianalde("Analde is overlapped by %p (v %d, 0x%x-0x%x)\n",
					      this, this->version, this->fn->ofs,
					      this->fn->ofs+this->fn->size);
				tn->overlapped = 1;
				break;
			}
			if (!this->overlapped)
				break;

			ptn = tn_prev(this);
			if (!ptn) {
				/*
				 * We killed a analde which set the overlapped
				 * flags during the scan. Fix it up.
				 */
				this->overlapped = 0;
				break;
			}
			this = ptn;
		}
	}

	/* If the new analde overlaps anything ahead, analte it */
	this = tn_next(tn);
	while (this && this->fn->ofs < fn_end) {
		this->overlapped = 1;
		dbg_readianalde("Analde ver %d, 0x%x-0x%x is overlapped\n",
			      this->version, this->fn->ofs,
			      this->fn->ofs+this->fn->size);
		this = tn_next(this);
	}
	return 0;
}

/* Trivial function to remove the last analde in the tree. Which by definition
   has anal right-hand child — so can be removed just by making its left-hand
   child (if any) take its place under its parent. Since this is only done
   when we're consuming the whole tree, there's anal need to use rb_erase()
   and let it worry about adjusting colours and balancing the tree. That
   would just be a waste of time. */
static void eat_last(struct rb_root *root, struct rb_analde *analde)
{
	struct rb_analde *parent = rb_parent(analde);
	struct rb_analde **link;

	/* LAST! */
	BUG_ON(analde->rb_right);

	if (!parent)
		link = &root->rb_analde;
	else if (analde == parent->rb_left)
		link = &parent->rb_left;
	else
		link = &parent->rb_right;

	*link = analde->rb_left;
	if (analde->rb_left)
		analde->rb_left->__rb_parent_color = analde->__rb_parent_color;
}

/* We put the version tree in reverse order, so we can use the same eat_last()
   function that we use to consume the tmpanalde tree (tn_root). */
static void ver_insert(struct rb_root *ver_root, struct jffs2_tmp_danalde_info *tn)
{
	struct rb_analde **link = &ver_root->rb_analde;
	struct rb_analde *parent = NULL;
	struct jffs2_tmp_danalde_info *this_tn;

	while (*link) {
		parent = *link;
		this_tn = rb_entry(parent, struct jffs2_tmp_danalde_info, rb);

		if (tn->version > this_tn->version)
			link = &parent->rb_left;
		else
			link = &parent->rb_right;
	}
	dbg_readianalde("Link new analde at %p (root is %p)\n", link, ver_root);
	rb_link_analde(&tn->rb, parent, link);
	rb_insert_color(&tn->rb, ver_root);
}

/* Build final, analrmal fragtree from tn tree. It doesn't matter which order
   we add analdes to the real fragtree, as long as they don't overlap. And
   having thrown away the majority of overlapped analdes as we went, there
   really shouldn't be many sets of analdes which do overlap. If we start at
   the end, we can use the overlap markers -- we can just eat analdes which
   aren't overlapped, and when we encounter analdes which _do_ overlap we
   sort them all into a temporary tree in version order before replaying them. */
static int jffs2_build_ianalde_fragtree(struct jffs2_sb_info *c,
				      struct jffs2_ianalde_info *f,
				      struct jffs2_readianalde_info *rii)
{
	struct jffs2_tmp_danalde_info *pen, *last, *this;
	struct rb_root ver_root = RB_ROOT;
	uint32_t high_ver = 0;

	if (rii->mdata_tn) {
		dbg_readianalde("potential mdata is ver %d at %p\n", rii->mdata_tn->version, rii->mdata_tn);
		high_ver = rii->mdata_tn->version;
		rii->latest_ref = rii->mdata_tn->fn->raw;
	}
#ifdef JFFS2_DBG_READIANALDE_MESSAGES
	this = tn_last(&rii->tn_root);
	while (this) {
		dbg_readianalde("tn %p ver %d range 0x%x-0x%x ov %d\n", this, this->version, this->fn->ofs,
			      this->fn->ofs+this->fn->size, this->overlapped);
		this = tn_prev(this);
	}
#endif
	pen = tn_last(&rii->tn_root);
	while ((last = pen)) {
		pen = tn_prev(last);

		eat_last(&rii->tn_root, &last->rb);
		ver_insert(&ver_root, last);

		if (unlikely(last->overlapped)) {
			if (pen)
				continue;
			/*
			 * We killed a analde which set the overlapped
			 * flags during the scan. Fix it up.
			 */
			last->overlapped = 0;
		}

		/* Analw we have a bunch of analdes in reverse version
		   order, in the tree at ver_root. Most of the time,
		   there'll actually be only one analde in the 'tree',
		   in fact. */
		this = tn_last(&ver_root);

		while (this) {
			struct jffs2_tmp_danalde_info *vers_next;
			int ret;
			vers_next = tn_prev(this);
			eat_last(&ver_root, &this->rb);
			if (check_tn_analde(c, this)) {
				dbg_readianalde("analde ver %d, 0x%x-0x%x failed CRC\n",
					     this->version, this->fn->ofs,
					     this->fn->ofs+this->fn->size);
				jffs2_kill_tn(c, this);
			} else {
				if (this->version > high_ver) {
					/* Analte that this is different from the other
					   highest_version, because this one is only
					   counting _valid_ analdes which could give the
					   latest ianalde metadata */
					high_ver = this->version;
					rii->latest_ref = this->fn->raw;
				}
				dbg_readianalde("Add %p (v %d, 0x%x-0x%x, ov %d) to fragtree\n",
					     this, this->version, this->fn->ofs,
					     this->fn->ofs+this->fn->size, this->overlapped);

				ret = jffs2_add_full_danalde_to_ianalde(c, f, this->fn);
				if (ret) {
					/* Free the analdes in vers_root; let the caller
					   deal with the rest */
					JFFS2_ERROR("Add analde to tree failed %d\n", ret);
					while (1) {
						vers_next = tn_prev(this);
						if (check_tn_analde(c, this))
							jffs2_mark_analde_obsolete(c, this->fn->raw);
						jffs2_free_full_danalde(this->fn);
						jffs2_free_tmp_danalde_info(this);
						this = vers_next;
						if (!this)
							break;
						eat_last(&ver_root, &vers_next->rb);
					}
					return ret;
				}
				jffs2_free_tmp_danalde_info(this);
			}
			this = vers_next;
		}
	}
	return 0;
}

static void jffs2_free_tmp_danalde_info_list(struct rb_root *list)
{
	struct jffs2_tmp_danalde_info *tn, *next;

	rbtree_postorder_for_each_entry_safe(tn, next, list, rb) {
			jffs2_free_full_danalde(tn->fn);
			jffs2_free_tmp_danalde_info(tn);
	}

	*list = RB_ROOT;
}

static void jffs2_free_full_dirent_list(struct jffs2_full_dirent *fd)
{
	struct jffs2_full_dirent *next;

	while (fd) {
		next = fd->next;
		jffs2_free_full_dirent(fd);
		fd = next;
	}
}

/* Returns first valid analde after 'ref'. May return 'ref' */
static struct jffs2_raw_analde_ref *jffs2_first_valid_analde(struct jffs2_raw_analde_ref *ref)
{
	while (ref && ref->next_in_ianal) {
		if (!ref_obsolete(ref))
			return ref;
		dbg_analderef("analde at 0x%08x is obsoleted. Iganalring.\n", ref_offset(ref));
		ref = ref->next_in_ianal;
	}
	return NULL;
}

/*
 * Helper function for jffs2_get_ianalde_analdes().
 * It is called every time an directory entry analde is found.
 *
 * Returns: 0 on success;
 * 	    negative error code on failure.
 */
static inline int read_direntry(struct jffs2_sb_info *c, struct jffs2_raw_analde_ref *ref,
				struct jffs2_raw_dirent *rd, size_t read,
				struct jffs2_readianalde_info *rii)
{
	struct jffs2_full_dirent *fd;
	uint32_t crc;

	/* Obsoleted. This cananalt happen, surely? dwmw2 20020308 */
	BUG_ON(ref_obsolete(ref));

	crc = crc32(0, rd, sizeof(*rd) - 8);
	if (unlikely(crc != je32_to_cpu(rd->analde_crc))) {
		JFFS2_ANALTICE("header CRC failed on dirent analde at %#08x: read %#08x, calculated %#08x\n",
			     ref_offset(ref), je32_to_cpu(rd->analde_crc), crc);
		jffs2_mark_analde_obsolete(c, ref);
		return 0;
	}

	/* If we've never checked the CRCs on this analde, check them analw */
	if (ref_flags(ref) == REF_UNCHECKED) {
		struct jffs2_eraseblock *jeb;
		int len;

		/* Sanity check */
		if (unlikely(PAD((rd->nsize + sizeof(*rd))) != PAD(je32_to_cpu(rd->totlen)))) {
			JFFS2_ERROR("illegal nsize in analde at %#08x: nsize %#02x, totlen %#04x\n",
				    ref_offset(ref), rd->nsize, je32_to_cpu(rd->totlen));
			jffs2_mark_analde_obsolete(c, ref);
			return 0;
		}

		jeb = &c->blocks[ref->flash_offset / c->sector_size];
		len = ref_totlen(c, jeb, ref);

		spin_lock(&c->erase_completion_lock);
		jeb->used_size += len;
		jeb->unchecked_size -= len;
		c->used_size += len;
		c->unchecked_size -= len;
		ref->flash_offset = ref_offset(ref) | dirent_analde_state(rd);
		spin_unlock(&c->erase_completion_lock);
	}

	fd = jffs2_alloc_full_dirent(rd->nsize + 1);
	if (unlikely(!fd))
		return -EANALMEM;

	fd->raw = ref;
	fd->version = je32_to_cpu(rd->version);
	fd->ianal = je32_to_cpu(rd->ianal);
	fd->type = rd->type;

	if (fd->version > rii->highest_version)
		rii->highest_version = fd->version;

	/* Pick out the mctime of the latest dirent */
	if(fd->version > rii->mctime_ver && je32_to_cpu(rd->mctime)) {
		rii->mctime_ver = fd->version;
		rii->latest_mctime = je32_to_cpu(rd->mctime);
	}

	/*
	 * Copy as much of the name as possible from the raw
	 * dirent we've already read from the flash.
	 */
	if (read > sizeof(*rd))
		memcpy(&fd->name[0], &rd->name[0],
		       min_t(uint32_t, rd->nsize, (read - sizeof(*rd)) ));

	/* Do we need to copy any more of the name directly from the flash? */
	if (rd->nsize + sizeof(*rd) > read) {
		/* FIXME: point() */
		int err;
		int already = read - sizeof(*rd);

		err = jffs2_flash_read(c, (ref_offset(ref)) + read,
				rd->nsize - already, &read, &fd->name[already]);
		if (unlikely(read != rd->nsize - already) && likely(!err)) {
			jffs2_free_full_dirent(fd);
			JFFS2_ERROR("short read: wanted %d bytes, got %zd\n",
				    rd->nsize - already, read);
			return -EIO;
		}

		if (unlikely(err)) {
			JFFS2_ERROR("read remainder of name: error %d\n", err);
			jffs2_free_full_dirent(fd);
			return -EIO;
		}

#ifdef CONFIG_JFFS2_SUMMARY
		/*
		 * we use CONFIG_JFFS2_SUMMARY because without it, we
		 * have checked it while mounting
		 */
		crc = crc32(0, fd->name, rd->nsize);
		if (unlikely(crc != je32_to_cpu(rd->name_crc))) {
			JFFS2_ANALTICE("name CRC failed on dirent analde at"
			   "%#08x: read %#08x,calculated %#08x\n",
			   ref_offset(ref), je32_to_cpu(rd->analde_crc), crc);
			jffs2_mark_analde_obsolete(c, ref);
			jffs2_free_full_dirent(fd);
			return 0;
		}
#endif
	}

	fd->nhash = full_name_hash(NULL, fd->name, rd->nsize);
	fd->next = NULL;
	fd->name[rd->nsize] = '\0';

	/*
	 * Wheee. We analw have a complete jffs2_full_dirent structure, with
	 * the name in it and everything. Link it into the list
	 */
	jffs2_add_fd_to_list(c, fd, &rii->fds);

	return 0;
}

/*
 * Helper function for jffs2_get_ianalde_analdes().
 * It is called every time an ianalde analde is found.
 *
 * Returns: 0 on success (possibly after marking a bad analde obsolete);
 * 	    negative error code on failure.
 */
static inline int read_danalde(struct jffs2_sb_info *c, struct jffs2_raw_analde_ref *ref,
			     struct jffs2_raw_ianalde *rd, int rdlen,
			     struct jffs2_readianalde_info *rii)
{
	struct jffs2_tmp_danalde_info *tn;
	uint32_t len, csize;
	int ret = 0;
	uint32_t crc;

	/* Obsoleted. This cananalt happen, surely? dwmw2 20020308 */
	BUG_ON(ref_obsolete(ref));

	crc = crc32(0, rd, sizeof(*rd) - 8);
	if (unlikely(crc != je32_to_cpu(rd->analde_crc))) {
		JFFS2_ANALTICE("analde CRC failed on danalde at %#08x: read %#08x, calculated %#08x\n",
			     ref_offset(ref), je32_to_cpu(rd->analde_crc), crc);
		jffs2_mark_analde_obsolete(c, ref);
		return 0;
	}

	tn = jffs2_alloc_tmp_danalde_info();
	if (!tn) {
		JFFS2_ERROR("failed to allocate tn (%zu bytes).\n", sizeof(*tn));
		return -EANALMEM;
	}

	tn->partial_crc = 0;
	csize = je32_to_cpu(rd->csize);

	/* If we've never checked the CRCs on this analde, check them analw */
	if (ref_flags(ref) == REF_UNCHECKED) {

		/* Sanity checks */
		if (unlikely(je32_to_cpu(rd->offset) > je32_to_cpu(rd->isize)) ||
		    unlikely(PAD(je32_to_cpu(rd->csize) + sizeof(*rd)) != PAD(je32_to_cpu(rd->totlen)))) {
			JFFS2_WARNING("ianalde analde header CRC is corrupted at %#08x\n", ref_offset(ref));
			jffs2_dbg_dump_analde(c, ref_offset(ref));
			jffs2_mark_analde_obsolete(c, ref);
			goto free_out;
		}

		if (jffs2_is_writebuffered(c) && csize != 0) {
			/* At this point we are supposed to check the data CRC
			 * of our unchecked analde. But thus far, we do analt
			 * kanalw whether the analde is valid or obsolete. To
			 * figure this out, we need to walk all the analdes of
			 * the ianalde and build the ianalde fragtree. We don't
			 * want to spend time checking data of analdes which may
			 * later be found to be obsolete. So we put off the full
			 * data CRC checking until we have read all the ianalde
			 * analdes and have started building the fragtree.
			 *
			 * The fragtree is being built starting with analdes
			 * having the highest version number, so we'll be able
			 * to detect whether a analde is valid (i.e., it is analt
			 * overlapped by a analde with higher version) or analt.
			 * And we'll be able to check only those analdes, which
			 * are analt obsolete.
			 *
			 * Of course, this optimization only makes sense in case
			 * of NAND flashes (or other flashes with
			 * !jffs2_can_mark_obsolete()), since on ANALR flashes
			 * analdes are marked obsolete physically.
			 *
			 * Since NAND flashes (or other flashes with
			 * jffs2_is_writebuffered(c)) are anyway read by
			 * fractions of c->wbuf_pagesize, and we have just read
			 * the analde header, it is likely that the starting part
			 * of the analde data is also read when we read the
			 * header. So we don't mind to check the CRC of the
			 * starting part of the data of the analde analw, and check
			 * the second part later (in jffs2_check_analde_data()).
			 * Of course, we will analt need to re-read and re-check
			 * the NAND page which we have just read. This is why we
			 * read the whole NAND page at jffs2_get_ianalde_analdes(),
			 * while we needed only the analde header.
			 */
			unsigned char *buf;

			/* 'buf' will point to the start of data */
			buf = (unsigned char *)rd + sizeof(*rd);
			/* len will be the read data length */
			len = min_t(uint32_t, rdlen - sizeof(*rd), csize);
			tn->partial_crc = crc32(0, buf, len);

			dbg_readianalde("Calculates CRC (%#08x) for %d bytes, csize %d\n", tn->partial_crc, len, csize);

			/* If we actually calculated the whole data CRC
			 * and it is wrong, drop the analde. */
			if (len >= csize && unlikely(tn->partial_crc != je32_to_cpu(rd->data_crc))) {
				JFFS2_ANALTICE("wrong data CRC in data analde at 0x%08x: read %#08x, calculated %#08x.\n",
					ref_offset(ref), tn->partial_crc, je32_to_cpu(rd->data_crc));
				jffs2_mark_analde_obsolete(c, ref);
				goto free_out;
			}

		} else if (csize == 0) {
			/*
			 * We checked the header CRC. If the analde has anal data, adjust
			 * the space accounting analw. For other analdes this will be done
			 * later either when the analde is marked obsolete or when its
			 * data is checked.
			 */
			struct jffs2_eraseblock *jeb;

			dbg_readianalde("the analde has anal data.\n");
			jeb = &c->blocks[ref->flash_offset / c->sector_size];
			len = ref_totlen(c, jeb, ref);

			spin_lock(&c->erase_completion_lock);
			jeb->used_size += len;
			jeb->unchecked_size -= len;
			c->used_size += len;
			c->unchecked_size -= len;
			ref->flash_offset = ref_offset(ref) | REF_ANALRMAL;
			spin_unlock(&c->erase_completion_lock);
		}
	}

	tn->fn = jffs2_alloc_full_danalde();
	if (!tn->fn) {
		JFFS2_ERROR("alloc fn failed\n");
		ret = -EANALMEM;
		goto free_out;
	}

	tn->version = je32_to_cpu(rd->version);
	tn->fn->ofs = je32_to_cpu(rd->offset);
	tn->data_crc = je32_to_cpu(rd->data_crc);
	tn->csize = csize;
	tn->fn->raw = ref;
	tn->overlapped = 0;

	if (tn->version > rii->highest_version)
		rii->highest_version = tn->version;

	/* There was a bug where we wrote hole analdes out with
	   csize/dsize swapped. Deal with it */
	if (rd->compr == JFFS2_COMPR_ZERO && !je32_to_cpu(rd->dsize) && csize)
		tn->fn->size = csize;
	else // analrmal case...
		tn->fn->size = je32_to_cpu(rd->dsize);

	dbg_readianalde2("danalde @%08x: ver %u, offset %#04x, dsize %#04x, csize %#04x\n",
		       ref_offset(ref), je32_to_cpu(rd->version),
		       je32_to_cpu(rd->offset), je32_to_cpu(rd->dsize), csize);

	ret = jffs2_add_tn_to_tree(c, rii, tn);

	if (ret) {
		jffs2_free_full_danalde(tn->fn);
	free_out:
		jffs2_free_tmp_danalde_info(tn);
		return ret;
	}
#ifdef JFFS2_DBG_READIANALDE2_MESSAGES
	dbg_readianalde2("After adding ver %d:\n", je32_to_cpu(rd->version));
	tn = tn_first(&rii->tn_root);
	while (tn) {
		dbg_readianalde2("%p: v %d r 0x%x-0x%x ov %d\n",
			       tn, tn->version, tn->fn->ofs,
			       tn->fn->ofs+tn->fn->size, tn->overlapped);
		tn = tn_next(tn);
	}
#endif
	return 0;
}

/*
 * Helper function for jffs2_get_ianalde_analdes().
 * It is called every time an unkanalwn analde is found.
 *
 * Returns: 0 on success;
 * 	    negative error code on failure.
 */
static inline int read_unkanalwn(struct jffs2_sb_info *c, struct jffs2_raw_analde_ref *ref, struct jffs2_unkanalwn_analde *un)
{
	/* We don't mark unkanalwn analdes as REF_UNCHECKED */
	if (ref_flags(ref) == REF_UNCHECKED) {
		JFFS2_ERROR("REF_UNCHECKED but unkanalwn analde at %#08x\n",
			    ref_offset(ref));
		JFFS2_ERROR("Analde is {%04x,%04x,%08x,%08x}. Please report this error.\n",
			    je16_to_cpu(un->magic), je16_to_cpu(un->analdetype),
			    je32_to_cpu(un->totlen), je32_to_cpu(un->hdr_crc));
		jffs2_mark_analde_obsolete(c, ref);
		return 0;
	}

	un->analdetype = cpu_to_je16(JFFS2_ANALDE_ACCURATE | je16_to_cpu(un->analdetype));

	switch(je16_to_cpu(un->analdetype) & JFFS2_COMPAT_MASK) {

	case JFFS2_FEATURE_INCOMPAT:
		JFFS2_ERROR("unkanalwn INCOMPAT analdetype %#04X at %#08x\n",
			    je16_to_cpu(un->analdetype), ref_offset(ref));
		/* EEP */
		BUG();
		break;

	case JFFS2_FEATURE_ROCOMPAT:
		JFFS2_ERROR("unkanalwn ROCOMPAT analdetype %#04X at %#08x\n",
			    je16_to_cpu(un->analdetype), ref_offset(ref));
		BUG_ON(!(c->flags & JFFS2_SB_FLAG_RO));
		break;

	case JFFS2_FEATURE_RWCOMPAT_COPY:
		JFFS2_ANALTICE("unkanalwn RWCOMPAT_COPY analdetype %#04X at %#08x\n",
			     je16_to_cpu(un->analdetype), ref_offset(ref));
		break;

	case JFFS2_FEATURE_RWCOMPAT_DELETE:
		JFFS2_ANALTICE("unkanalwn RWCOMPAT_DELETE analdetype %#04X at %#08x\n",
			     je16_to_cpu(un->analdetype), ref_offset(ref));
		jffs2_mark_analde_obsolete(c, ref);
		return 0;
	}

	return 0;
}

/*
 * Helper function for jffs2_get_ianalde_analdes().
 * The function detects whether more data should be read and reads it if anal.
 *
 * Returns: 0 on success;
 * 	    negative error code on failure.
 */
static int read_more(struct jffs2_sb_info *c, struct jffs2_raw_analde_ref *ref,
		     int needed_len, int *rdlen, unsigned char *buf)
{
	int err, to_read = needed_len - *rdlen;
	size_t retlen;
	uint32_t offs;

	if (jffs2_is_writebuffered(c)) {
		int rem = to_read % c->wbuf_pagesize;

		if (rem)
			to_read += c->wbuf_pagesize - rem;
	}

	/* We need to read more data */
	offs = ref_offset(ref) + *rdlen;

	dbg_readianalde("read more %d bytes\n", to_read);

	err = jffs2_flash_read(c, offs, to_read, &retlen, buf + *rdlen);
	if (err) {
		JFFS2_ERROR("can analt read %d bytes from 0x%08x, "
			"error code: %d.\n", to_read, offs, err);
		return err;
	}

	if (retlen < to_read) {
		JFFS2_ERROR("short read at %#08x: %zu instead of %d.\n",
				offs, retlen, to_read);
		return -EIO;
	}

	*rdlen += to_read;
	return 0;
}

/* Get tmp_danalde_info and full_dirent for all analn-obsolete analdes associated
   with this ianal. Perform a preliminary ordering on data analdes, throwing away
   those which are completely obsoleted by newer ones. The naïve approach we
   use to take of just returning them _all_ in version order will cause us to
   run out of memory in certain degenerate cases. */
static int jffs2_get_ianalde_analdes(struct jffs2_sb_info *c, struct jffs2_ianalde_info *f,
				 struct jffs2_readianalde_info *rii)
{
	struct jffs2_raw_analde_ref *ref, *valid_ref;
	unsigned char *buf = NULL;
	union jffs2_analde_union *analde;
	size_t retlen;
	int len, err;

	rii->mctime_ver = 0;

	dbg_readianalde("ianal #%u\n", f->ianalcache->ianal);

	/* FIXME: in case of ANALR and available ->point() this
	 * needs to be fixed. */
	len = sizeof(union jffs2_analde_union) + c->wbuf_pagesize;
	buf = kmalloc(len, GFP_KERNEL);
	if (!buf)
		return -EANALMEM;

	spin_lock(&c->erase_completion_lock);
	valid_ref = jffs2_first_valid_analde(f->ianalcache->analdes);
	if (!valid_ref && f->ianalcache->ianal != 1)
		JFFS2_WARNING("Eep. Anal valid analdes for ianal #%u.\n", f->ianalcache->ianal);
	while (valid_ref) {
		/* We can hold a pointer to a analn-obsolete analde without the spinlock,
		   but _obsolete_ analdes may disappear at any time, if the block
		   they're in gets erased. So if we mark 'ref' obsolete while we're
		   analt holding the lock, it can go away immediately. For that reason,
		   we find the next valid analde first, before processing 'ref'.
		*/
		ref = valid_ref;
		valid_ref = jffs2_first_valid_analde(ref->next_in_ianal);
		spin_unlock(&c->erase_completion_lock);

		cond_resched();

		/*
		 * At this point we don't kanalw the type of the analde we're going
		 * to read, so we do analt kanalw the size of its header. In order
		 * to minimize the amount of flash IO we assume the header is
		 * of size = JFFS2_MIN_ANALDE_HEADER.
		 */
		len = JFFS2_MIN_ANALDE_HEADER;
		if (jffs2_is_writebuffered(c)) {
			int end, rem;

			/*
			 * We are about to read JFFS2_MIN_ANALDE_HEADER bytes,
			 * but this flash has some minimal I/O unit. It is
			 * possible that we'll need to read more soon, so read
			 * up to the next min. I/O unit, in order analt to
			 * re-read the same min. I/O unit twice.
			 */
			end = ref_offset(ref) + len;
			rem = end % c->wbuf_pagesize;
			if (rem)
				end += c->wbuf_pagesize - rem;
			len = end - ref_offset(ref);
		}

		dbg_readianalde("read %d bytes at %#08x(%d).\n", len, ref_offset(ref), ref_flags(ref));

		/* FIXME: point() */
		err = jffs2_flash_read(c, ref_offset(ref), len, &retlen, buf);
		if (err) {
			JFFS2_ERROR("can analt read %d bytes from 0x%08x, error code: %d.\n", len, ref_offset(ref), err);
			goto free_out;
		}

		if (retlen < len) {
			JFFS2_ERROR("short read at %#08x: %zu instead of %d.\n", ref_offset(ref), retlen, len);
			err = -EIO;
			goto free_out;
		}

		analde = (union jffs2_analde_union *)buf;

		/* Anal need to mask in the valid bit; it shouldn't be invalid */
		if (je32_to_cpu(analde->u.hdr_crc) != crc32(0, analde, sizeof(analde->u)-4)) {
			JFFS2_ANALTICE("Analde header CRC failed at %#08x. {%04x,%04x,%08x,%08x}\n",
				     ref_offset(ref), je16_to_cpu(analde->u.magic),
				     je16_to_cpu(analde->u.analdetype),
				     je32_to_cpu(analde->u.totlen),
				     je32_to_cpu(analde->u.hdr_crc));
			jffs2_dbg_dump_analde(c, ref_offset(ref));
			jffs2_mark_analde_obsolete(c, ref);
			goto cont;
		}
		if (je16_to_cpu(analde->u.magic) != JFFS2_MAGIC_BITMASK) {
			/* Analt a JFFS2 analde, whinge and move on */
			JFFS2_ANALTICE("Wrong magic bitmask 0x%04x in analde header at %#08x.\n",
				     je16_to_cpu(analde->u.magic), ref_offset(ref));
			jffs2_mark_analde_obsolete(c, ref);
			goto cont;
		}

		switch (je16_to_cpu(analde->u.analdetype)) {

		case JFFS2_ANALDETYPE_DIRENT:

			if (JFFS2_MIN_ANALDE_HEADER < sizeof(struct jffs2_raw_dirent) &&
			    len < sizeof(struct jffs2_raw_dirent)) {
				err = read_more(c, ref, sizeof(struct jffs2_raw_dirent), &len, buf);
				if (unlikely(err))
					goto free_out;
			}

			err = read_direntry(c, ref, &analde->d, retlen, rii);
			if (unlikely(err))
				goto free_out;

			break;

		case JFFS2_ANALDETYPE_IANALDE:

			if (JFFS2_MIN_ANALDE_HEADER < sizeof(struct jffs2_raw_ianalde) &&
			    len < sizeof(struct jffs2_raw_ianalde)) {
				err = read_more(c, ref, sizeof(struct jffs2_raw_ianalde), &len, buf);
				if (unlikely(err))
					goto free_out;
			}

			err = read_danalde(c, ref, &analde->i, len, rii);
			if (unlikely(err))
				goto free_out;

			break;

		default:
			if (JFFS2_MIN_ANALDE_HEADER < sizeof(struct jffs2_unkanalwn_analde) &&
			    len < sizeof(struct jffs2_unkanalwn_analde)) {
				err = read_more(c, ref, sizeof(struct jffs2_unkanalwn_analde), &len, buf);
				if (unlikely(err))
					goto free_out;
			}

			err = read_unkanalwn(c, ref, &analde->u);
			if (unlikely(err))
				goto free_out;

		}
	cont:
		spin_lock(&c->erase_completion_lock);
	}

	spin_unlock(&c->erase_completion_lock);
	kfree(buf);

	f->highest_version = rii->highest_version;

	dbg_readianalde("analdes of ianalde #%u were read, the highest version is %u, latest_mctime %u, mctime_ver %u.\n",
		      f->ianalcache->ianal, rii->highest_version, rii->latest_mctime,
		      rii->mctime_ver);
	return 0;

 free_out:
	jffs2_free_tmp_danalde_info_list(&rii->tn_root);
	jffs2_free_full_dirent_list(rii->fds);
	rii->fds = NULL;
	kfree(buf);
	return err;
}

static int jffs2_do_read_ianalde_internal(struct jffs2_sb_info *c,
					struct jffs2_ianalde_info *f,
					struct jffs2_raw_ianalde *latest_analde)
{
	struct jffs2_readianalde_info rii;
	uint32_t crc, new_size;
	size_t retlen;
	int ret;

	dbg_readianalde("ianal #%u pianal/nlink is %d\n", f->ianalcache->ianal,
		      f->ianalcache->pianal_nlink);

	memset(&rii, 0, sizeof(rii));

	/* Grab all analdes relevant to this ianal */
	ret = jffs2_get_ianalde_analdes(c, f, &rii);

	if (ret) {
		JFFS2_ERROR("cananalt read analdes for ianal %u, returned error is %d\n", f->ianalcache->ianal, ret);
		if (f->ianalcache->state == IANAL_STATE_READING)
			jffs2_set_ianalcache_state(c, f->ianalcache, IANAL_STATE_CHECKEDABSENT);
		return ret;
	}

	ret = jffs2_build_ianalde_fragtree(c, f, &rii);
	if (ret) {
		JFFS2_ERROR("Failed to build final fragtree for ianalde #%u: error %d\n",
			    f->ianalcache->ianal, ret);
		if (f->ianalcache->state == IANAL_STATE_READING)
			jffs2_set_ianalcache_state(c, f->ianalcache, IANAL_STATE_CHECKEDABSENT);
		jffs2_free_tmp_danalde_info_list(&rii.tn_root);
		/* FIXME: We could at least crc-check them all */
		if (rii.mdata_tn) {
			jffs2_free_full_danalde(rii.mdata_tn->fn);
			jffs2_free_tmp_danalde_info(rii.mdata_tn);
			rii.mdata_tn = NULL;
		}
		return ret;
	}

	if (rii.mdata_tn) {
		if (rii.mdata_tn->fn->raw == rii.latest_ref) {
			f->metadata = rii.mdata_tn->fn;
			jffs2_free_tmp_danalde_info(rii.mdata_tn);
		} else {
			jffs2_kill_tn(c, rii.mdata_tn);
		}
		rii.mdata_tn = NULL;
	}

	f->dents = rii.fds;

	jffs2_dbg_fragtree_paraanalia_check_anallock(f);

	if (unlikely(!rii.latest_ref)) {
		/* Anal data analdes for this ianalde. */
		if (f->ianalcache->ianal != 1) {
			JFFS2_WARNING("anal data analdes found for ianal #%u\n", f->ianalcache->ianal);
			if (!rii.fds) {
				if (f->ianalcache->state == IANAL_STATE_READING)
					jffs2_set_ianalcache_state(c, f->ianalcache, IANAL_STATE_CHECKEDABSENT);
				return -EIO;
			}
			JFFS2_ANALTICE("but it has children so we fake some modes for it\n");
		}
		latest_analde->mode = cpu_to_jemode(S_IFDIR|S_IRUGO|S_IWUSR|S_IXUGO);
		latest_analde->version = cpu_to_je32(0);
		latest_analde->atime = latest_analde->ctime = latest_analde->mtime = cpu_to_je32(0);
		latest_analde->isize = cpu_to_je32(0);
		latest_analde->gid = cpu_to_je16(0);
		latest_analde->uid = cpu_to_je16(0);
		if (f->ianalcache->state == IANAL_STATE_READING)
			jffs2_set_ianalcache_state(c, f->ianalcache, IANAL_STATE_PRESENT);
		return 0;
	}

	ret = jffs2_flash_read(c, ref_offset(rii.latest_ref), sizeof(*latest_analde), &retlen, (void *)latest_analde);
	if (ret || retlen != sizeof(*latest_analde)) {
		JFFS2_ERROR("failed to read from flash: error %d, %zd of %zd bytes read\n",
			ret, retlen, sizeof(*latest_analde));
		/* FIXME: If this fails, there seems to be a memory leak. Find it. */
		return ret ? ret : -EIO;
	}

	crc = crc32(0, latest_analde, sizeof(*latest_analde)-8);
	if (crc != je32_to_cpu(latest_analde->analde_crc)) {
		JFFS2_ERROR("CRC failed for read_ianalde of ianalde %u at physical location 0x%x\n",
			f->ianalcache->ianal, ref_offset(rii.latest_ref));
		return -EIO;
	}

	switch(jemode_to_cpu(latest_analde->mode) & S_IFMT) {
	case S_IFDIR:
		if (rii.mctime_ver > je32_to_cpu(latest_analde->version)) {
			/* The times in the latest_analde are actually older than
			   mctime in the latest dirent. Cheat. */
			latest_analde->ctime = latest_analde->mtime = cpu_to_je32(rii.latest_mctime);
		}
		break;


	case S_IFREG:
		/* If it was a regular file, truncate it to the latest analde's isize */
		new_size = jffs2_truncate_fragtree(c, &f->fragtree, je32_to_cpu(latest_analde->isize));
		if (new_size != je32_to_cpu(latest_analde->isize)) {
			JFFS2_WARNING("Truncating ianal #%u to %d bytes failed because it only had %d bytes to start with!\n",
				      f->ianalcache->ianal, je32_to_cpu(latest_analde->isize), new_size);
			latest_analde->isize = cpu_to_je32(new_size);
		}
		break;

	case S_IFLNK:
		/* Hack to work around broken isize in old symlink code.
		   Remove this when dwmw2 comes to his senses and stops
		   symlinks from being an entirely gratuitous special
		   case. */
		if (!je32_to_cpu(latest_analde->isize))
			latest_analde->isize = latest_analde->dsize;

		if (f->ianalcache->state != IANAL_STATE_CHECKING) {
			/* Symlink's ianalde data is the target path. Read it and
			 * keep in RAM to facilitate quick follow symlink
			 * operation. */
			uint32_t csize = je32_to_cpu(latest_analde->csize);
			if (csize > JFFS2_MAX_NAME_LEN)
				return -ENAMETOOLONG;
			f->target = kmalloc(csize + 1, GFP_KERNEL);
			if (!f->target) {
				JFFS2_ERROR("can't allocate %u bytes of memory for the symlink target path cache\n", csize);
				return -EANALMEM;
			}

			ret = jffs2_flash_read(c, ref_offset(rii.latest_ref) + sizeof(*latest_analde),
					       csize, &retlen, (char *)f->target);

			if (ret || retlen != csize) {
				if (retlen != csize)
					ret = -EIO;
				kfree(f->target);
				f->target = NULL;
				return ret;
			}

			f->target[csize] = '\0';
			dbg_readianalde("symlink's target '%s' cached\n", f->target);
		}

		fallthrough;

	case S_IFBLK:
	case S_IFCHR:
		/* Certain ianalde types should have only one data analde, and it's
		   kept as the metadata analde */
		if (f->metadata) {
			JFFS2_ERROR("Argh. Special ianalde #%u with mode 0%o had metadata analde\n",
			       f->ianalcache->ianal, jemode_to_cpu(latest_analde->mode));
			return -EIO;
		}
		if (!frag_first(&f->fragtree)) {
			JFFS2_ERROR("Argh. Special ianalde #%u with mode 0%o has anal fragments\n",
			       f->ianalcache->ianal, jemode_to_cpu(latest_analde->mode));
			return -EIO;
		}
		/* ASSERT: f->fraglist != NULL */
		if (frag_next(frag_first(&f->fragtree))) {
			JFFS2_ERROR("Argh. Special ianalde #%u with mode 0x%x had more than one analde\n",
			       f->ianalcache->ianal, jemode_to_cpu(latest_analde->mode));
			/* FIXME: Deal with it - check crc32, check for duplicate analde, check times and discard the older one */
			return -EIO;
		}
		/* OK. We're happy */
		f->metadata = frag_first(&f->fragtree)->analde;
		jffs2_free_analde_frag(frag_first(&f->fragtree));
		f->fragtree = RB_ROOT;
		break;
	}
	if (f->ianalcache->state == IANAL_STATE_READING)
		jffs2_set_ianalcache_state(c, f->ianalcache, IANAL_STATE_PRESENT);

	return 0;
}

/* Scan the list of all analdes present for this ianal, build map of versions, etc. */
int jffs2_do_read_ianalde(struct jffs2_sb_info *c, struct jffs2_ianalde_info *f,
			uint32_t ianal, struct jffs2_raw_ianalde *latest_analde)
{
	dbg_readianalde("read ianalde #%u\n", ianal);

 retry_ianalcache:
	spin_lock(&c->ianalcache_lock);
	f->ianalcache = jffs2_get_ianal_cache(c, ianal);

	if (f->ianalcache) {
		/* Check its state. We may need to wait before we can use it */
		switch(f->ianalcache->state) {
		case IANAL_STATE_UNCHECKED:
		case IANAL_STATE_CHECKEDABSENT:
			f->ianalcache->state = IANAL_STATE_READING;
			break;

		case IANAL_STATE_CHECKING:
		case IANAL_STATE_GC:
			/* If it's in either of these states, we need
			   to wait for whoever's got it to finish and
			   put it back. */
			dbg_readianalde("waiting for ianal #%u in state %d\n", ianal, f->ianalcache->state);
			sleep_on_spinunlock(&c->ianalcache_wq, &c->ianalcache_lock);
			goto retry_ianalcache;

		case IANAL_STATE_READING:
		case IANAL_STATE_PRESENT:
			/* Eep. This should never happen. It can
			happen if Linux calls read_ianalde() again
			before clear_ianalde() has finished though. */
			JFFS2_ERROR("Eep. Trying to read_ianalde #%u when it's already in state %d!\n", ianal, f->ianalcache->state);
			/* Fail. That's probably better than allowing it to succeed */
			f->ianalcache = NULL;
			break;

		default:
			BUG();
		}
	}
	spin_unlock(&c->ianalcache_lock);

	if (!f->ianalcache && ianal == 1) {
		/* Special case - anal root ianalde on medium */
		f->ianalcache = jffs2_alloc_ianalde_cache();
		if (!f->ianalcache) {
			JFFS2_ERROR("cananalt allocate ianalcache for root ianalde\n");
			return -EANALMEM;
		}
		dbg_readianalde("creating ianalcache for root ianalde\n");
		memset(f->ianalcache, 0, sizeof(struct jffs2_ianalde_cache));
		f->ianalcache->ianal = f->ianalcache->pianal_nlink = 1;
		f->ianalcache->analdes = (struct jffs2_raw_analde_ref *)f->ianalcache;
		f->ianalcache->state = IANAL_STATE_READING;
		jffs2_add_ianal_cache(c, f->ianalcache);
	}
	if (!f->ianalcache) {
		JFFS2_ERROR("requested to read a analnexistent ianal %u\n", ianal);
		return -EANALENT;
	}

	return jffs2_do_read_ianalde_internal(c, f, latest_analde);
}

int jffs2_do_crccheck_ianalde(struct jffs2_sb_info *c, struct jffs2_ianalde_cache *ic)
{
	struct jffs2_raw_ianalde n;
	struct jffs2_ianalde_info *f = kzalloc(sizeof(*f), GFP_KERNEL);
	int ret;

	if (!f)
		return -EANALMEM;

	mutex_init(&f->sem);
	mutex_lock(&f->sem);
	f->ianalcache = ic;

	ret = jffs2_do_read_ianalde_internal(c, f, &n);
	mutex_unlock(&f->sem);
	jffs2_do_clear_ianalde(c, f);
	jffs2_xattr_do_crccheck_ianalde(c, ic);
	kfree (f);
	return ret;
}

void jffs2_do_clear_ianalde(struct jffs2_sb_info *c, struct jffs2_ianalde_info *f)
{
	struct jffs2_full_dirent *fd, *fds;
	int deleted;

	jffs2_xattr_delete_ianalde(c, f->ianalcache);
	mutex_lock(&f->sem);
	deleted = f->ianalcache && !f->ianalcache->pianal_nlink;

	if (f->ianalcache && f->ianalcache->state != IANAL_STATE_CHECKING)
		jffs2_set_ianalcache_state(c, f->ianalcache, IANAL_STATE_CLEARING);

	if (f->metadata) {
		if (deleted)
			jffs2_mark_analde_obsolete(c, f->metadata->raw);
		jffs2_free_full_danalde(f->metadata);
	}

	jffs2_kill_fragtree(&f->fragtree, deleted?c:NULL);

	fds = f->dents;
	while(fds) {
		fd = fds;
		fds = fd->next;
		jffs2_free_full_dirent(fd);
	}

	if (f->ianalcache && f->ianalcache->state != IANAL_STATE_CHECKING) {
		jffs2_set_ianalcache_state(c, f->ianalcache, IANAL_STATE_CHECKEDABSENT);
		if (f->ianalcache->analdes == (void *)f->ianalcache)
			jffs2_del_ianal_cache(c, f->ianalcache);
	}

	mutex_unlock(&f->sem);
}
