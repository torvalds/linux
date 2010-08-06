/*
 * fs/logfs/gc.c	- garbage collection code
 *
 * As should be obvious for Linux kernel code, license is GPLv2
 *
 * Copyright (c) 2005-2008 Joern Engel <joern@logfs.org>
 */
#include "logfs.h"
#include <linux/sched.h>
#include <linux/slab.h>

/*
 * Wear leveling needs to kick in when the difference between low erase
 * counts and high erase counts gets too big.  A good value for "too big"
 * may be somewhat below 10% of maximum erase count for the device.
 * Why not 397, to pick a nice round number with no specific meaning? :)
 *
 * WL_RATELIMIT is the minimum time between two wear level events.  A huge
 * number of segments may fulfil the requirements for wear leveling at the
 * same time.  If that happens we don't want to cause a latency from hell,
 * but just gently pick one segment every so often and minimize overhead.
 */
#define WL_DELTA 397
#define WL_RATELIMIT 100
#define MAX_OBJ_ALIASES	2600
#define SCAN_RATIO 512	/* number of scanned segments per gc'd segment */
#define LIST_SIZE 64	/* base size of candidate lists */
#define SCAN_ROUNDS 128	/* maximum number of complete medium scans */
#define SCAN_ROUNDS_HIGH 4 /* maximum number of higher-level scans */

static int no_free_segments(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);

	return super->s_free_list.count;
}

/* journal has distance -1, top-most ifile layer distance 0 */
static u8 root_distance(struct super_block *sb, gc_level_t __gc_level)
{
	struct logfs_super *super = logfs_super(sb);
	u8 gc_level = (__force u8)__gc_level;

	switch (gc_level) {
	case 0: /* fall through */
	case 1: /* fall through */
	case 2: /* fall through */
	case 3:
		/* file data or indirect blocks */
		return super->s_ifile_levels + super->s_iblock_levels - gc_level;
	case 6: /* fall through */
	case 7: /* fall through */
	case 8: /* fall through */
	case 9:
		/* inode file data or indirect blocks */
		return super->s_ifile_levels - (gc_level - 6);
	default:
		printk(KERN_ERR"LOGFS: segment of unknown level %x found\n",
				gc_level);
		WARN_ON(1);
		return super->s_ifile_levels + super->s_iblock_levels;
	}
}

static int segment_is_reserved(struct super_block *sb, u32 segno)
{
	struct logfs_super *super = logfs_super(sb);
	struct logfs_area *area;
	void *reserved;
	int i;

	/* Some segments are reserved.  Just pretend they were all valid */
	reserved = btree_lookup32(&super->s_reserved_segments, segno);
	if (reserved)
		return 1;

	/* Currently open segments */
	for_each_area(i) {
		area = super->s_area[i];
		if (area->a_is_open && area->a_segno == segno)
			return 1;
	}

	return 0;
}

static void logfs_mark_segment_bad(struct super_block *sb, u32 segno)
{
	BUG();
}

/*
 * Returns the bytes consumed by valid objects in this segment.  Object headers
 * are counted, the segment header is not.
 */
static u32 logfs_valid_bytes(struct super_block *sb, u32 segno, u32 *ec,
		gc_level_t *gc_level)
{
	struct logfs_segment_entry se;
	u32 ec_level;

	logfs_get_segment_entry(sb, segno, &se);
	if (se.ec_level == cpu_to_be32(BADSEG) ||
			se.valid == cpu_to_be32(RESERVED))
		return RESERVED;

	ec_level = be32_to_cpu(se.ec_level);
	*ec = ec_level >> 4;
	*gc_level = GC_LEVEL(ec_level & 0xf);
	return be32_to_cpu(se.valid);
}

static void logfs_cleanse_block(struct super_block *sb, u64 ofs, u64 ino,
		u64 bix, gc_level_t gc_level)
{
	struct inode *inode;
	int err, cookie;

	inode = logfs_safe_iget(sb, ino, &cookie);
	err = logfs_rewrite_block(inode, bix, ofs, gc_level, 0);
	BUG_ON(err);
	logfs_safe_iput(inode, cookie);
}

static u32 logfs_gc_segment(struct super_block *sb, u32 segno)
{
	struct logfs_super *super = logfs_super(sb);
	struct logfs_segment_header sh;
	struct logfs_object_header oh;
	u64 ofs, ino, bix;
	u32 seg_ofs, logical_segno, cleaned = 0;
	int err, len, valid;
	gc_level_t gc_level;

	LOGFS_BUG_ON(segment_is_reserved(sb, segno), sb);

	btree_insert32(&super->s_reserved_segments, segno, (void *)1, GFP_NOFS);
	err = wbuf_read(sb, dev_ofs(sb, segno, 0), sizeof(sh), &sh);
	BUG_ON(err);
	gc_level = GC_LEVEL(sh.level);
	logical_segno = be32_to_cpu(sh.segno);
	if (sh.crc != logfs_crc32(&sh, sizeof(sh), 4)) {
		logfs_mark_segment_bad(sb, segno);
		cleaned = -1;
		goto out;
	}

	for (seg_ofs = LOGFS_SEGMENT_HEADERSIZE;
			seg_ofs + sizeof(oh) < super->s_segsize; ) {
		ofs = dev_ofs(sb, logical_segno, seg_ofs);
		err = wbuf_read(sb, dev_ofs(sb, segno, seg_ofs), sizeof(oh),
				&oh);
		BUG_ON(err);

		if (!memchr_inv(&oh, 0xff, sizeof(oh)))
			break;

		if (oh.crc != logfs_crc32(&oh, sizeof(oh) - 4, 4)) {
			logfs_mark_segment_bad(sb, segno);
			cleaned = super->s_segsize - 1;
			goto out;
		}

		ino = be64_to_cpu(oh.ino);
		bix = be64_to_cpu(oh.bix);
		len = sizeof(oh) + be16_to_cpu(oh.len);
		valid = logfs_is_valid_block(sb, ofs, ino, bix, gc_level);
		if (valid == 1) {
			logfs_cleanse_block(sb, ofs, ino, bix, gc_level);
			cleaned += len;
		} else if (valid == 2) {
			/* Will be invalid upon journal commit */
			cleaned += len;
		}
		seg_ofs += len;
	}
out:
	btree_remove32(&super->s_reserved_segments, segno);
	return cleaned;
}

static struct gc_candidate *add_list(struct gc_candidate *cand,
		struct candidate_list *list)
{
	struct rb_node **p = &list->rb_tree.rb_node;
	struct rb_node *parent = NULL;
	struct gc_candidate *cur;
	int comp;

	cand->list = list;
	while (*p) {
		parent = *p;
		cur = rb_entry(parent, struct gc_candidate, rb_node);

		if (list->sort_by_ec)
			comp = cand->erase_count < cur->erase_count;
		else
			comp = cand->valid < cur->valid;

		if (comp)
			p = &parent->rb_left;
		else
			p = &parent->rb_right;
	}
	rb_link_node(&cand->rb_node, parent, p);
	rb_insert_color(&cand->rb_node, &list->rb_tree);

	if (list->count <= list->maxcount) {
		list->count++;
		return NULL;
	}
	cand = rb_entry(rb_last(&list->rb_tree), struct gc_candidate, rb_node);
	rb_erase(&cand->rb_node, &list->rb_tree);
	cand->list = NULL;
	return cand;
}

static void remove_from_list(struct gc_candidate *cand)
{
	struct candidate_list *list = cand->list;

	rb_erase(&cand->rb_node, &list->rb_tree);
	list->count--;
}

static void free_candidate(struct super_block *sb, struct gc_candidate *cand)
{
	struct logfs_super *super = logfs_super(sb);

	btree_remove32(&super->s_cand_tree, cand->segno);
	kfree(cand);
}

u32 get_best_cand(struct super_block *sb, struct candidate_list *list, u32 *ec)
{
	struct gc_candidate *cand;
	u32 segno;

	BUG_ON(list->count == 0);

	cand = rb_entry(rb_first(&list->rb_tree), struct gc_candidate, rb_node);
	remove_from_list(cand);
	segno = cand->segno;
	if (ec)
		*ec = cand->erase_count;
	free_candidate(sb, cand);
	return segno;
}

/*
 * We have several lists to manage segments with.  The reserve_list is used to
 * deal with bad blocks.  We try to keep the best (lowest ec) segments on this
 * list.
 * The free_list contains free segments for normal usage.  It usually gets the
 * second pick after the reserve_list.  But when the free_list is running short
 * it is more important to keep the free_list full than to keep a reserve.
 *
 * Segments that are not free are put onto a per-level low_list.  If we have
 * to run garbage collection, we pick a candidate from there.  All segments on
 * those lists should have at least some free space so GC will make progress.
 *
 * And last we have the ec_list, which is used to pick segments for wear
 * leveling.
 *
 * If all appropriate lists are full, we simply free the candidate and forget
 * about that segment for a while.  We have better candidates for each purpose.
 */
static void __add_candidate(struct super_block *sb, struct gc_candidate *cand)
{
	struct logfs_super *super = logfs_super(sb);
	u32 full = super->s_segsize - LOGFS_SEGMENT_RESERVE;

	if (cand->valid == 0) {
		/* 100% free segments */
		log_gc_noisy("add reserve segment %x (ec %x) at %llx\n",
				cand->segno, cand->erase_count,
				dev_ofs(sb, cand->segno, 0));
		cand = add_list(cand, &super->s_reserve_list);
		if (cand) {
			log_gc_noisy("add free segment %x (ec %x) at %llx\n",
					cand->segno, cand->erase_count,
					dev_ofs(sb, cand->segno, 0));
			cand = add_list(cand, &super->s_free_list);
		}
	} else {
		/* good candidates for Garbage Collection */
		if (cand->valid < full)
			cand = add_list(cand, &super->s_low_list[cand->dist]);
		/* good candidates for wear leveling,
		 * segments that were recently written get ignored */
		if (cand)
			cand = add_list(cand, &super->s_ec_list);
	}
	if (cand)
		free_candidate(sb, cand);
}

static int add_candidate(struct super_block *sb, u32 segno, u32 valid, u32 ec,
		u8 dist)
{
	struct logfs_super *super = logfs_super(sb);
	struct gc_candidate *cand;

	cand = kmalloc(sizeof(*cand), GFP_NOFS);
	if (!cand)
		return -ENOMEM;

	cand->segno = segno;
	cand->valid = valid;
	cand->erase_count = ec;
	cand->dist = dist;

	btree_insert32(&super->s_cand_tree, segno, cand, GFP_NOFS);
	__add_candidate(sb, cand);
	return 0;
}

static void remove_segment_from_lists(struct super_block *sb, u32 segno)
{
	struct logfs_super *super = logfs_super(sb);
	struct gc_candidate *cand;

	cand = btree_lookup32(&super->s_cand_tree, segno);
	if (cand) {
		remove_from_list(cand);
		free_candidate(sb, cand);
	}
}

static void scan_segment(struct super_block *sb, u32 segno)
{
	u32 valid, ec = 0;
	gc_level_t gc_level = 0;
	u8 dist;

	if (segment_is_reserved(sb, segno))
		return;

	remove_segment_from_lists(sb, segno);
	valid = logfs_valid_bytes(sb, segno, &ec, &gc_level);
	if (valid == RESERVED)
		return;

	dist = root_distance(sb, gc_level);
	add_candidate(sb, segno, valid, ec, dist);
}

static struct gc_candidate *first_in_list(struct candidate_list *list)
{
	if (list->count == 0)
		return NULL;
	return rb_entry(rb_first(&list->rb_tree), struct gc_candidate, rb_node);
}

/*
 * Find the best segment for garbage collection.  Main criterion is
 * the segment requiring the least effort to clean.  Secondary
 * criterion is to GC on the lowest level available.
 *
 * So we search the least effort segment on the lowest level first,
 * then move up and pick another segment iff is requires significantly
 * less effort.  Hence the LOGFS_MAX_OBJECTSIZE in the comparison.
 */
static struct gc_candidate *get_candidate(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	int i, max_dist;
	struct gc_candidate *cand = NULL, *this;

	max_dist = min(no_free_segments(sb), LOGFS_NO_AREAS);

	for (i = max_dist; i >= 0; i--) {
		this = first_in_list(&super->s_low_list[i]);
		if (!this)
			continue;
		if (!cand)
			cand = this;
		if (this->valid + LOGFS_MAX_OBJECTSIZE <= cand->valid)
			cand = this;
	}
	return cand;
}

static int __logfs_gc_once(struct super_block *sb, struct gc_candidate *cand)
{
	struct logfs_super *super = logfs_super(sb);
	gc_level_t gc_level;
	u32 cleaned, valid, segno, ec;
	u8 dist;

	if (!cand) {
		log_gc("GC attempted, but no candidate found\n");
		return 0;
	}

	segno = cand->segno;
	dist = cand->dist;
	valid = logfs_valid_bytes(sb, segno, &ec, &gc_level);
	free_candidate(sb, cand);
	log_gc("GC segment #%02x at %llx, %x required, %x free, %x valid, %llx free\n",
			segno, (u64)segno << super->s_segshift,
			dist, no_free_segments(sb), valid,
			super->s_free_bytes);
	cleaned = logfs_gc_segment(sb, segno);
	log_gc("GC segment #%02x complete - now %x valid\n", segno,
			valid - cleaned);
	BUG_ON(cleaned != valid);
	return 1;
}

static int logfs_gc_once(struct super_block *sb)
{
	struct gc_candidate *cand;

	cand = get_candidate(sb);
	if (cand)
		remove_from_list(cand);
	return __logfs_gc_once(sb, cand);
}

/* returns 1 if a wrap occurs, 0 otherwise */
static int logfs_scan_some(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	u32 segno;
	int i, ret = 0;

	segno = super->s_sweeper;
	for (i = SCAN_RATIO; i > 0; i--) {
		segno++;
		if (segno >= super->s_no_segs) {
			segno = 0;
			ret = 1;
			/* Break out of the loop.  We want to read a single
			 * block from the segment size on next invocation if
			 * SCAN_RATIO is set to match block size
			 */
			break;
		}

		scan_segment(sb, segno);
	}
	super->s_sweeper = segno;
	return ret;
}

/*
 * In principle, this function should loop forever, looking for GC candidates
 * and moving data.  LogFS is designed in such a way that this loop is
 * guaranteed to terminate.
 *
 * Limiting the loop to some iterations serves purely to catch cases when
 * these guarantees have failed.  An actual endless loop is an obvious bug
 * and should be reported as such.
 */
static void __logfs_gc_pass(struct super_block *sb, int target)
{
	struct logfs_super *super = logfs_super(sb);
	struct logfs_block *block;
	int round, progress, last_progress = 0;

	/*
	 * Doing too many changes to the segfile at once would result
	 * in a large number of aliases.  Write the journal before
	 * things get out of hand.
	 */
	if (super->s_shadow_tree.no_shadowed_segments >= MAX_OBJ_ALIASES)
		logfs_write_anchor(sb);

	if (no_free_segments(sb) >= target &&
			super->s_no_object_aliases < MAX_OBJ_ALIASES)
		return;

	log_gc("__logfs_gc_pass(%x)\n", target);
	for (round = 0; round < SCAN_ROUNDS; ) {
		if (no_free_segments(sb) >= target)
			goto write_alias;

		/* Sync in-memory state with on-medium state in case they
		 * diverged */
		logfs_write_anchor(sb);
		round += logfs_scan_some(sb);
		if (no_free_segments(sb) >= target)
			goto write_alias;
		progress = logfs_gc_once(sb);
		if (progress)
			last_progress = round;
		else if (round - last_progress > 2)
			break;
		continue;

		/*
		 * The goto logic is nasty, I just don't know a better way to
		 * code it.  GC is supposed to ensure two things:
		 * 1. Enough free segments are available.
		 * 2. The number of aliases is bounded.
		 * When 1. is achieved, we take a look at 2. and write back
		 * some alias-containing blocks, if necessary.  However, after
		 * each such write we need to go back to 1., as writes can
		 * consume free segments.
		 */
write_alias:
		if (super->s_no_object_aliases < MAX_OBJ_ALIASES)
			return;
		if (list_empty(&super->s_object_alias)) {
			/* All aliases are still in btree */
			return;
		}
		log_gc("Write back one alias\n");
		block = list_entry(super->s_object_alias.next,
				struct logfs_block, alias_list);
		block->ops->write_block(block);
		/*
		 * To round off the nasty goto logic, we reset round here.  It
		 * is a safety-net for GC not making any progress and limited
		 * to something reasonably small.  If incremented it for every
		 * single alias, the loop could terminate rather quickly.
		 */
		round = 0;
	}
	LOGFS_BUG(sb);
}

static int wl_ratelimit(struct super_block *sb, u64 *next_event)
{
	struct logfs_super *super = logfs_super(sb);

	if (*next_event < super->s_gec) {
		*next_event = super->s_gec + WL_RATELIMIT;
		return 0;
	}
	return 1;
}

static void logfs_wl_pass(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	struct gc_candidate *wl_cand, *free_cand;

	if (wl_ratelimit(sb, &super->s_wl_gec_ostore))
		return;

	wl_cand = first_in_list(&super->s_ec_list);
	if (!wl_cand)
		return;
	free_cand = first_in_list(&super->s_free_list);
	if (!free_cand)
		return;

	if (wl_cand->erase_count < free_cand->erase_count + WL_DELTA) {
		remove_from_list(wl_cand);
		__logfs_gc_once(sb, wl_cand);
	}
}

/*
 * The journal needs wear leveling as well.  But moving the journal is an
 * expensive operation so we try to avoid it as much as possible.  And if we
 * have to do it, we move the whole journal, not individual segments.
 *
 * Ratelimiting is not strictly necessary here, it mainly serves to avoid the
 * calculations.  First we check whether moving the journal would be a
 * significant improvement.  That means that a) the current journal segments
 * have more wear than the future journal segments and b) the current journal
 * segments have more wear than normal ostore segments.
 * Rationale for b) is that we don't have to move the journal if it is aging
 * less than the ostore, even if the reserve segments age even less (they are
 * excluded from wear leveling, after all).
 * Next we check that the superblocks have less wear than the journal.  Since
 * moving the journal requires writing the superblocks, we have to protect the
 * superblocks even more than the journal.
 *
 * Also we double the acceptable wear difference, compared to ostore wear
 * leveling.  Journal data is read and rewritten rapidly, comparatively.  So
 * soft errors have much less time to accumulate and we allow the journal to
 * be a bit worse than the ostore.
 */
static void logfs_journal_wl_pass(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	struct gc_candidate *cand;
	u32 min_journal_ec = -1, max_reserve_ec = 0;
	int i;

	if (wl_ratelimit(sb, &super->s_wl_gec_journal))
		return;

	if (super->s_reserve_list.count < super->s_no_journal_segs) {
		/* Reserve is not full enough to move complete journal */
		return;
	}

	journal_for_each(i)
		if (super->s_journal_seg[i])
			min_journal_ec = min(min_journal_ec,
					super->s_journal_ec[i]);
	cand = rb_entry(rb_first(&super->s_free_list.rb_tree),
			struct gc_candidate, rb_node);
	max_reserve_ec = cand->erase_count;
	for (i = 0; i < 2; i++) {
		struct logfs_segment_entry se;
		u32 segno = seg_no(sb, super->s_sb_ofs[i]);
		u32 ec;

		logfs_get_segment_entry(sb, segno, &se);
		ec = be32_to_cpu(se.ec_level) >> 4;
		max_reserve_ec = max(max_reserve_ec, ec);
	}

	if (min_journal_ec > max_reserve_ec + 2 * WL_DELTA) {
		do_logfs_journal_wl_pass(sb);
	}
}

void logfs_gc_pass(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);

	//BUG_ON(mutex_trylock(&logfs_super(sb)->s_w_mutex));
	/* Write journal before free space is getting saturated with dirty
	 * objects.
	 */
	if (super->s_dirty_used_bytes + super->s_dirty_free_bytes
			+ LOGFS_MAX_OBJECTSIZE >= super->s_free_bytes)
		logfs_write_anchor(sb);
	__logfs_gc_pass(sb, super->s_total_levels);
	logfs_wl_pass(sb);
	logfs_journal_wl_pass(sb);
}

static int check_area(struct super_block *sb, int i)
{
	struct logfs_super *super = logfs_super(sb);
	struct logfs_area *area = super->s_area[i];
	gc_level_t gc_level;
	u32 cleaned, valid, ec;
	u32 segno = area->a_segno;
	u64 ofs = dev_ofs(sb, area->a_segno, area->a_written_bytes);

	if (!area->a_is_open)
		return 0;

	if (super->s_devops->can_write_buf(sb, ofs) == 0)
		return 0;

	printk(KERN_INFO"LogFS: Possibly incomplete write at %llx\n", ofs);
	/*
	 * The device cannot write back the write buffer.  Most likely the
	 * wbuf was already written out and the system crashed at some point
	 * before the journal commit happened.  In that case we wouldn't have
	 * to do anything.  But if the crash happened before the wbuf was
	 * written out correctly, we must GC this segment.  So assume the
	 * worst and always do the GC run.
	 */
	area->a_is_open = 0;
	valid = logfs_valid_bytes(sb, segno, &ec, &gc_level);
	cleaned = logfs_gc_segment(sb, segno);
	if (cleaned != valid)
		return -EIO;
	return 0;
}

int logfs_check_areas(struct super_block *sb)
{
	int i, err;

	for_each_area(i) {
		err = check_area(sb, i);
		if (err)
			return err;
	}
	return 0;
}

static void logfs_init_candlist(struct candidate_list *list, int maxcount,
		int sort_by_ec)
{
	list->count = 0;
	list->maxcount = maxcount;
	list->sort_by_ec = sort_by_ec;
	list->rb_tree = RB_ROOT;
}

int logfs_init_gc(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	int i;

	btree_init_mempool32(&super->s_cand_tree, super->s_btree_pool);
	logfs_init_candlist(&super->s_free_list, LIST_SIZE + SCAN_RATIO, 1);
	logfs_init_candlist(&super->s_reserve_list,
			super->s_bad_seg_reserve, 1);
	for_each_area(i)
		logfs_init_candlist(&super->s_low_list[i], LIST_SIZE, 0);
	logfs_init_candlist(&super->s_ec_list, LIST_SIZE, 1);
	return 0;
}

static void logfs_cleanup_list(struct super_block *sb,
		struct candidate_list *list)
{
	struct gc_candidate *cand;

	while (list->count) {
		cand = rb_entry(list->rb_tree.rb_node, struct gc_candidate,
				rb_node);
		remove_from_list(cand);
		free_candidate(sb, cand);
	}
	BUG_ON(list->rb_tree.rb_node);
}

void logfs_cleanup_gc(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	int i;

	if (!super->s_free_list.count)
		return;

	/*
	 * FIXME: The btree may still contain a single empty node.  So we
	 * call the grim visitor to clean up that mess.  Btree code should
	 * do it for us, really.
	 */
	btree_grim_visitor32(&super->s_cand_tree, 0, NULL);
	logfs_cleanup_list(sb, &super->s_free_list);
	logfs_cleanup_list(sb, &super->s_reserve_list);
	for_each_area(i)
		logfs_cleanup_list(sb, &super->s_low_list[i]);
	logfs_cleanup_list(sb, &super->s_ec_list);
}
