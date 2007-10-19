/*
 * Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README
 */
/* Reiserfs block (de)allocator, bitmap-based. */

#include <linux/time.h>
#include <linux/reiserfs_fs.h>
#include <linux/errno.h>
#include <linux/buffer_head.h>
#include <linux/kernel.h>
#include <linux/pagemap.h>
#include <linux/vmalloc.h>
#include <linux/reiserfs_fs_sb.h>
#include <linux/reiserfs_fs_i.h>
#include <linux/quotaops.h>

#define PREALLOCATION_SIZE 9

/* different reiserfs block allocator options */

#define SB_ALLOC_OPTS(s) (REISERFS_SB(s)->s_alloc_options.bits)

#define  _ALLOC_concentrating_formatted_nodes 0
#define  _ALLOC_displacing_large_files 1
#define  _ALLOC_displacing_new_packing_localities 2
#define  _ALLOC_old_hashed_relocation 3
#define  _ALLOC_new_hashed_relocation 4
#define  _ALLOC_skip_busy 5
#define  _ALLOC_displace_based_on_dirid 6
#define  _ALLOC_hashed_formatted_nodes 7
#define  _ALLOC_old_way 8
#define  _ALLOC_hundredth_slices 9
#define  _ALLOC_dirid_groups 10
#define  _ALLOC_oid_groups 11
#define  _ALLOC_packing_groups 12

#define  concentrating_formatted_nodes(s)	test_bit(_ALLOC_concentrating_formatted_nodes, &SB_ALLOC_OPTS(s))
#define  displacing_large_files(s)		test_bit(_ALLOC_displacing_large_files, &SB_ALLOC_OPTS(s))
#define  displacing_new_packing_localities(s)	test_bit(_ALLOC_displacing_new_packing_localities, &SB_ALLOC_OPTS(s))

#define SET_OPTION(optname) \
   do { \
        reiserfs_warning(s, "reiserfs: option \"%s\" is set", #optname); \
        set_bit(_ALLOC_ ## optname , &SB_ALLOC_OPTS(s)); \
    } while(0)
#define TEST_OPTION(optname, s) \
    test_bit(_ALLOC_ ## optname , &SB_ALLOC_OPTS(s))

static inline void get_bit_address(struct super_block *s,
				   b_blocknr_t block,
				   unsigned int *bmap_nr,
				   unsigned int *offset)
{
	/* It is in the bitmap block number equal to the block
	 * number divided by the number of bits in a block. */
	*bmap_nr = block >> (s->s_blocksize_bits + 3);
	/* Within that bitmap block it is located at bit offset *offset. */
	*offset = block & ((s->s_blocksize << 3) - 1);
}

int is_reusable(struct super_block *s, b_blocknr_t block, int bit_value)
{
	unsigned int bmap, offset;

	if (block == 0 || block >= SB_BLOCK_COUNT(s)) {
		reiserfs_warning(s,
				 "vs-4010: is_reusable: block number is out of range %lu (%u)",
				 block, SB_BLOCK_COUNT(s));
		return 0;
	}

	get_bit_address(s, block, &bmap, &offset);

	/* Old format filesystem? Unlikely, but the bitmaps are all up front so
	 * we need to account for it. */
	if (unlikely(test_bit(REISERFS_OLD_FORMAT,
			      &(REISERFS_SB(s)->s_properties)))) {
		b_blocknr_t bmap1 = REISERFS_SB(s)->s_sbh->b_blocknr + 1;
		if (block >= bmap1 && block <= bmap1 + SB_BMAP_NR(s)) {
			reiserfs_warning(s, "vs: 4019: is_reusable: "
					 "bitmap block %lu(%u) can't be freed or reused",
					 block, SB_BMAP_NR(s));
			return 0;
		}
	} else {
		if (offset == 0) {
			reiserfs_warning(s, "vs: 4020: is_reusable: "
					 "bitmap block %lu(%u) can't be freed or reused",
					 block, SB_BMAP_NR(s));
			return 0;
		}
	}

	if (bmap >= SB_BMAP_NR(s)) {
		reiserfs_warning(s,
				 "vs-4030: is_reusable: there is no so many bitmap blocks: "
				 "block=%lu, bitmap_nr=%d", block, bmap);
		return 0;
	}

	if (bit_value == 0 && block == SB_ROOT_BLOCK(s)) {
		reiserfs_warning(s,
				 "vs-4050: is_reusable: this is root block (%u), "
				 "it must be busy", SB_ROOT_BLOCK(s));
		return 0;
	}

	return 1;
}

/* searches in journal structures for a given block number (bmap, off). If block
   is found in reiserfs journal it suggests next free block candidate to test. */
static inline int is_block_in_journal(struct super_block *s, unsigned int bmap,
				      int off, int *next)
{
	b_blocknr_t tmp;

	if (reiserfs_in_journal(s, bmap, off, 1, &tmp)) {
		if (tmp) {	/* hint supplied */
			*next = tmp;
			PROC_INFO_INC(s, scan_bitmap.in_journal_hint);
		} else {
			(*next) = off + 1;	/* inc offset to avoid looping. */
			PROC_INFO_INC(s, scan_bitmap.in_journal_nohint);
		}
		PROC_INFO_INC(s, scan_bitmap.retry);
		return 1;
	}
	return 0;
}

/* it searches for a window of zero bits with given minimum and maximum lengths in one bitmap
 * block; */
static int scan_bitmap_block(struct reiserfs_transaction_handle *th,
			     unsigned int bmap_n, int *beg, int boundary,
			     int min, int max, int unfm)
{
	struct super_block *s = th->t_super;
	struct reiserfs_bitmap_info *bi = &SB_AP_BITMAP(s)[bmap_n];
	struct buffer_head *bh;
	int end, next;
	int org = *beg;

	BUG_ON(!th->t_trans_id);

	RFALSE(bmap_n >= SB_BMAP_NR(s), "Bitmap %d is out of range (0..%d)",
	       bmap_n, SB_BMAP_NR(s) - 1);
	PROC_INFO_INC(s, scan_bitmap.bmap);
/* this is unclear and lacks comments, explain how journal bitmaps
   work here for the reader.  Convey a sense of the design here. What
   is a window? */
/* - I mean `a window of zero bits' as in description of this function - Zam. */

	if (!bi) {
		reiserfs_warning(s, "NULL bitmap info pointer for bitmap %d",
				 bmap_n);
		return 0;
	}

	bh = reiserfs_read_bitmap_block(s, bmap_n);
	if (bh == NULL)
		return 0;

	while (1) {
	      cont:
		if (bi->free_count < min) {
			brelse(bh);
			return 0;	// No free blocks in this bitmap
		}

		/* search for a first zero bit -- beggining of a window */
		*beg = reiserfs_find_next_zero_le_bit
		    ((unsigned long *)(bh->b_data), boundary, *beg);

		if (*beg + min > boundary) {	/* search for a zero bit fails or the rest of bitmap block
						 * cannot contain a zero window of minimum size */
			brelse(bh);
			return 0;
		}

		if (unfm && is_block_in_journal(s, bmap_n, *beg, beg))
			continue;
		/* first zero bit found; we check next bits */
		for (end = *beg + 1;; end++) {
			if (end >= *beg + max || end >= boundary
			    || reiserfs_test_le_bit(end, bh->b_data)) {
				next = end;
				break;
			}
			/* finding the other end of zero bit window requires looking into journal structures (in
			 * case of searching for free blocks for unformatted nodes) */
			if (unfm && is_block_in_journal(s, bmap_n, end, &next))
				break;
		}

		/* now (*beg) points to beginning of zero bits window,
		 * (end) points to one bit after the window end */
		if (end - *beg >= min) {	/* it seems we have found window of proper size */
			int i;
			reiserfs_prepare_for_journal(s, bh, 1);
			/* try to set all blocks used checking are they still free */
			for (i = *beg; i < end; i++) {
				/* It seems that we should not check in journal again. */
				if (reiserfs_test_and_set_le_bit
				    (i, bh->b_data)) {
					/* bit was set by another process
					 * while we slept in prepare_for_journal() */
					PROC_INFO_INC(s, scan_bitmap.stolen);
					if (i >= *beg + min) {	/* we can continue with smaller set of allocated blocks,
								 * if length of this set is more or equal to `min' */
						end = i;
						break;
					}
					/* otherwise we clear all bit were set ... */
					while (--i >= *beg)
						reiserfs_test_and_clear_le_bit
						    (i, bh->b_data);
					reiserfs_restore_prepared_buffer(s, bh);
					*beg = org;
					/* ... and search again in current block from beginning */
					goto cont;
				}
			}
			bi->free_count -= (end - *beg);
			journal_mark_dirty(th, s, bh);
			brelse(bh);

			/* free block count calculation */
			reiserfs_prepare_for_journal(s, SB_BUFFER_WITH_SB(s),
						     1);
			PUT_SB_FREE_BLOCKS(s, SB_FREE_BLOCKS(s) - (end - *beg));
			journal_mark_dirty(th, s, SB_BUFFER_WITH_SB(s));

			return end - (*beg);
		} else {
			*beg = next;
		}
	}
}

static int bmap_hash_id(struct super_block *s, u32 id)
{
	char *hash_in = NULL;
	unsigned long hash;
	unsigned bm;

	if (id <= 2) {
		bm = 1;
	} else {
		hash_in = (char *)(&id);
		hash = keyed_hash(hash_in, 4);
		bm = hash % SB_BMAP_NR(s);
		if (!bm)
			bm = 1;
	}
	/* this can only be true when SB_BMAP_NR = 1 */
	if (bm >= SB_BMAP_NR(s))
		bm = 0;
	return bm;
}

/*
 * hashes the id and then returns > 0 if the block group for the
 * corresponding hash is full
 */
static inline int block_group_used(struct super_block *s, u32 id)
{
	int bm = bmap_hash_id(s, id);
	struct reiserfs_bitmap_info *info = &SB_AP_BITMAP(s)[bm];

	/* If we don't have cached information on this bitmap block, we're
	 * going to have to load it later anyway. Loading it here allows us
	 * to make a better decision. This favors long-term performace gain
	 * with a better on-disk layout vs. a short term gain of skipping the
	 * read and potentially having a bad placement. */
	if (info->free_count == UINT_MAX) {
		struct buffer_head *bh = reiserfs_read_bitmap_block(s, bm);
		brelse(bh);
	}

	if (info->free_count > ((s->s_blocksize << 3) * 60 / 100)) {
		return 0;
	}
	return 1;
}

/*
 * the packing is returned in disk byte order
 */
__le32 reiserfs_choose_packing(struct inode * dir)
{
	__le32 packing;
	if (TEST_OPTION(packing_groups, dir->i_sb)) {
		u32 parent_dir = le32_to_cpu(INODE_PKEY(dir)->k_dir_id);
		/*
		 * some versions of reiserfsck expect packing locality 1 to be
		 * special
		 */
		if (parent_dir == 1 || block_group_used(dir->i_sb, parent_dir))
			packing = INODE_PKEY(dir)->k_objectid;
		else
			packing = INODE_PKEY(dir)->k_dir_id;
	} else
		packing = INODE_PKEY(dir)->k_objectid;
	return packing;
}

/* Tries to find contiguous zero bit window (given size) in given region of
 * bitmap and place new blocks there. Returns number of allocated blocks. */
static int scan_bitmap(struct reiserfs_transaction_handle *th,
		       b_blocknr_t * start, b_blocknr_t finish,
		       int min, int max, int unfm, sector_t file_block)
{
	int nr_allocated = 0;
	struct super_block *s = th->t_super;
	/* find every bm and bmap and bmap_nr in this file, and change them all to bitmap_blocknr
	 * - Hans, it is not a block number - Zam. */

	unsigned int bm, off;
	unsigned int end_bm, end_off;
	unsigned int off_max = s->s_blocksize << 3;

	BUG_ON(!th->t_trans_id);

	PROC_INFO_INC(s, scan_bitmap.call);
	if (SB_FREE_BLOCKS(s) <= 0)
		return 0;	// No point in looking for more free blocks

	get_bit_address(s, *start, &bm, &off);
	get_bit_address(s, finish, &end_bm, &end_off);
	if (bm > SB_BMAP_NR(s))
		return 0;
	if (end_bm > SB_BMAP_NR(s))
		end_bm = SB_BMAP_NR(s);

	/* When the bitmap is more than 10% free, anyone can allocate.
	 * When it's less than 10% free, only files that already use the
	 * bitmap are allowed. Once we pass 80% full, this restriction
	 * is lifted.
	 *
	 * We do this so that files that grow later still have space close to
	 * their original allocation. This improves locality, and presumably
	 * performance as a result.
	 *
	 * This is only an allocation policy and does not make up for getting a
	 * bad hint. Decent hinting must be implemented for this to work well.
	 */
	if (TEST_OPTION(skip_busy, s)
	    && SB_FREE_BLOCKS(s) > SB_BLOCK_COUNT(s) / 20) {
		for (; bm < end_bm; bm++, off = 0) {
			if ((off && (!unfm || (file_block != 0)))
			    || SB_AP_BITMAP(s)[bm].free_count >
			    (s->s_blocksize << 3) / 10)
				nr_allocated =
				    scan_bitmap_block(th, bm, &off, off_max,
						      min, max, unfm);
			if (nr_allocated)
				goto ret;
		}
		/* we know from above that start is a reasonable number */
		get_bit_address(s, *start, &bm, &off);
	}

	for (; bm < end_bm; bm++, off = 0) {
		nr_allocated =
		    scan_bitmap_block(th, bm, &off, off_max, min, max, unfm);
		if (nr_allocated)
			goto ret;
	}

	nr_allocated =
	    scan_bitmap_block(th, bm, &off, end_off + 1, min, max, unfm);

      ret:
	*start = bm * off_max + off;
	return nr_allocated;

}

static void _reiserfs_free_block(struct reiserfs_transaction_handle *th,
				 struct inode *inode, b_blocknr_t block,
				 int for_unformatted)
{
	struct super_block *s = th->t_super;
	struct reiserfs_super_block *rs;
	struct buffer_head *sbh, *bmbh;
	struct reiserfs_bitmap_info *apbi;
	unsigned int nr, offset;

	BUG_ON(!th->t_trans_id);

	PROC_INFO_INC(s, free_block);

	rs = SB_DISK_SUPER_BLOCK(s);
	sbh = SB_BUFFER_WITH_SB(s);
	apbi = SB_AP_BITMAP(s);

	get_bit_address(s, block, &nr, &offset);

	if (nr >= sb_bmap_nr(rs)) {
		reiserfs_warning(s, "vs-4075: reiserfs_free_block: "
				 "block %lu is out of range on %s",
				 block, reiserfs_bdevname(s));
		return;
	}

	bmbh = reiserfs_read_bitmap_block(s, nr);
	if (!bmbh)
		return;

	reiserfs_prepare_for_journal(s, bmbh, 1);

	/* clear bit for the given block in bit map */
	if (!reiserfs_test_and_clear_le_bit(offset, bmbh->b_data)) {
		reiserfs_warning(s, "vs-4080: reiserfs_free_block: "
				 "free_block (%s:%lu)[dev:blocknr]: bit already cleared",
				 reiserfs_bdevname(s), block);
	}
	apbi[nr].free_count++;
	journal_mark_dirty(th, s, bmbh);
	brelse(bmbh);

	reiserfs_prepare_for_journal(s, sbh, 1);
	/* update super block */
	set_sb_free_blocks(rs, sb_free_blocks(rs) + 1);

	journal_mark_dirty(th, s, sbh);
	if (for_unformatted)
		DQUOT_FREE_BLOCK_NODIRTY(inode, 1);
}

void reiserfs_free_block(struct reiserfs_transaction_handle *th,
			 struct inode *inode, b_blocknr_t block,
			 int for_unformatted)
{
	struct super_block *s = th->t_super;
	BUG_ON(!th->t_trans_id);

	RFALSE(!s, "vs-4061: trying to free block on nonexistent device");
	if (!is_reusable(s, block, 1))
		return;

	if (block > sb_block_count(REISERFS_SB(s)->s_rs)) {
		reiserfs_panic(th->t_super, "bitmap-4072",
			       "Trying to free block outside file system "
			       "boundaries (%lu > %lu)",
			       block, sb_block_count(REISERFS_SB(s)->s_rs));
		return;
	}
	/* mark it before we clear it, just in case */
	journal_mark_freed(th, s, block);
	_reiserfs_free_block(th, inode, block, for_unformatted);
}

/* preallocated blocks don't need to be run through journal_mark_freed */
static void reiserfs_free_prealloc_block(struct reiserfs_transaction_handle *th,
					 struct inode *inode, b_blocknr_t block)
{
	BUG_ON(!th->t_trans_id);
	RFALSE(!th->t_super,
	       "vs-4060: trying to free block on nonexistent device");
	if (!is_reusable(th->t_super, block, 1))
		return;
	_reiserfs_free_block(th, inode, block, 1);
}

static void __discard_prealloc(struct reiserfs_transaction_handle *th,
			       struct reiserfs_inode_info *ei)
{
	unsigned long save = ei->i_prealloc_block;
	int dirty = 0;
	struct inode *inode = &ei->vfs_inode;
	BUG_ON(!th->t_trans_id);
#ifdef CONFIG_REISERFS_CHECK
	if (ei->i_prealloc_count < 0)
		reiserfs_warning(th->t_super,
				 "zam-4001:%s: inode has negative prealloc blocks count.",
				 __FUNCTION__);
#endif
	while (ei->i_prealloc_count > 0) {
		reiserfs_free_prealloc_block(th, inode, ei->i_prealloc_block);
		ei->i_prealloc_block++;
		ei->i_prealloc_count--;
		dirty = 1;
	}
	if (dirty)
		reiserfs_update_sd(th, inode);
	ei->i_prealloc_block = save;
	list_del_init(&(ei->i_prealloc_list));
}

/* FIXME: It should be inline function */
void reiserfs_discard_prealloc(struct reiserfs_transaction_handle *th,
			       struct inode *inode)
{
	struct reiserfs_inode_info *ei = REISERFS_I(inode);
	BUG_ON(!th->t_trans_id);
	if (ei->i_prealloc_count)
		__discard_prealloc(th, ei);
}

void reiserfs_discard_all_prealloc(struct reiserfs_transaction_handle *th)
{
	struct list_head *plist = &SB_JOURNAL(th->t_super)->j_prealloc_list;

	BUG_ON(!th->t_trans_id);

	while (!list_empty(plist)) {
		struct reiserfs_inode_info *ei;
		ei = list_entry(plist->next, struct reiserfs_inode_info,
				i_prealloc_list);
#ifdef CONFIG_REISERFS_CHECK
		if (!ei->i_prealloc_count) {
			reiserfs_warning(th->t_super,
					 "zam-4001:%s: inode is in prealloc list but has no preallocated blocks.",
					 __FUNCTION__);
		}
#endif
		__discard_prealloc(th, ei);
	}
}

void reiserfs_init_alloc_options(struct super_block *s)
{
	set_bit(_ALLOC_skip_busy, &SB_ALLOC_OPTS(s));
	set_bit(_ALLOC_dirid_groups, &SB_ALLOC_OPTS(s));
	set_bit(_ALLOC_packing_groups, &SB_ALLOC_OPTS(s));
}

/* block allocator related options are parsed here */
int reiserfs_parse_alloc_options(struct super_block *s, char *options)
{
	char *this_char, *value;

	REISERFS_SB(s)->s_alloc_options.bits = 0;	/* clear default settings */

	while ((this_char = strsep(&options, ":")) != NULL) {
		if ((value = strchr(this_char, '=')) != NULL)
			*value++ = 0;

		if (!strcmp(this_char, "concentrating_formatted_nodes")) {
			int temp;
			SET_OPTION(concentrating_formatted_nodes);
			temp = (value
				&& *value) ? simple_strtoul(value, &value,
							    0) : 10;
			if (temp <= 0 || temp > 100) {
				REISERFS_SB(s)->s_alloc_options.border = 10;
			} else {
				REISERFS_SB(s)->s_alloc_options.border =
				    100 / temp;
			}
			continue;
		}
		if (!strcmp(this_char, "displacing_large_files")) {
			SET_OPTION(displacing_large_files);
			REISERFS_SB(s)->s_alloc_options.large_file_size =
			    (value
			     && *value) ? simple_strtoul(value, &value, 0) : 16;
			continue;
		}
		if (!strcmp(this_char, "displacing_new_packing_localities")) {
			SET_OPTION(displacing_new_packing_localities);
			continue;
		};

		if (!strcmp(this_char, "old_hashed_relocation")) {
			SET_OPTION(old_hashed_relocation);
			continue;
		}

		if (!strcmp(this_char, "new_hashed_relocation")) {
			SET_OPTION(new_hashed_relocation);
			continue;
		}

		if (!strcmp(this_char, "dirid_groups")) {
			SET_OPTION(dirid_groups);
			continue;
		}
		if (!strcmp(this_char, "oid_groups")) {
			SET_OPTION(oid_groups);
			continue;
		}
		if (!strcmp(this_char, "packing_groups")) {
			SET_OPTION(packing_groups);
			continue;
		}
		if (!strcmp(this_char, "hashed_formatted_nodes")) {
			SET_OPTION(hashed_formatted_nodes);
			continue;
		}

		if (!strcmp(this_char, "skip_busy")) {
			SET_OPTION(skip_busy);
			continue;
		}

		if (!strcmp(this_char, "hundredth_slices")) {
			SET_OPTION(hundredth_slices);
			continue;
		}

		if (!strcmp(this_char, "old_way")) {
			SET_OPTION(old_way);
			continue;
		}

		if (!strcmp(this_char, "displace_based_on_dirid")) {
			SET_OPTION(displace_based_on_dirid);
			continue;
		}

		if (!strcmp(this_char, "preallocmin")) {
			REISERFS_SB(s)->s_alloc_options.preallocmin =
			    (value
			     && *value) ? simple_strtoul(value, &value, 0) : 4;
			continue;
		}

		if (!strcmp(this_char, "preallocsize")) {
			REISERFS_SB(s)->s_alloc_options.preallocsize =
			    (value
			     && *value) ? simple_strtoul(value, &value,
							 0) :
			    PREALLOCATION_SIZE;
			continue;
		}

		reiserfs_warning(s, "zam-4001: %s : unknown option - %s",
				 __FUNCTION__, this_char);
		return 1;
	}

	reiserfs_warning(s, "allocator options = [%08x]\n", SB_ALLOC_OPTS(s));
	return 0;
}

static inline void new_hashed_relocation(reiserfs_blocknr_hint_t * hint)
{
	char *hash_in;
	if (hint->formatted_node) {
		hash_in = (char *)&hint->key.k_dir_id;
	} else {
		if (!hint->inode) {
			//hint->search_start = hint->beg;
			hash_in = (char *)&hint->key.k_dir_id;
		} else
		    if (TEST_OPTION(displace_based_on_dirid, hint->th->t_super))
			hash_in = (char *)(&INODE_PKEY(hint->inode)->k_dir_id);
		else
			hash_in =
			    (char *)(&INODE_PKEY(hint->inode)->k_objectid);
	}

	hint->search_start =
	    hint->beg + keyed_hash(hash_in, 4) % (hint->end - hint->beg);
}

/*
 * Relocation based on dirid, hashing them into a given bitmap block
 * files. Formatted nodes are unaffected, a seperate policy covers them
 */
static void dirid_groups(reiserfs_blocknr_hint_t * hint)
{
	unsigned long hash;
	__u32 dirid = 0;
	int bm = 0;
	struct super_block *sb = hint->th->t_super;
	if (hint->inode)
		dirid = le32_to_cpu(INODE_PKEY(hint->inode)->k_dir_id);
	else if (hint->formatted_node)
		dirid = hint->key.k_dir_id;

	if (dirid) {
		bm = bmap_hash_id(sb, dirid);
		hash = bm * (sb->s_blocksize << 3);
		/* give a portion of the block group to metadata */
		if (hint->inode)
			hash += sb->s_blocksize / 2;
		hint->search_start = hash;
	}
}

/*
 * Relocation based on oid, hashing them into a given bitmap block
 * files. Formatted nodes are unaffected, a seperate policy covers them
 */
static void oid_groups(reiserfs_blocknr_hint_t * hint)
{
	if (hint->inode) {
		unsigned long hash;
		__u32 oid;
		__u32 dirid;
		int bm;

		dirid = le32_to_cpu(INODE_PKEY(hint->inode)->k_dir_id);

		/* keep the root dir and it's first set of subdirs close to
		 * the start of the disk
		 */
		if (dirid <= 2)
			hash = (hint->inode->i_sb->s_blocksize << 3);
		else {
			oid = le32_to_cpu(INODE_PKEY(hint->inode)->k_objectid);
			bm = bmap_hash_id(hint->inode->i_sb, oid);
			hash = bm * (hint->inode->i_sb->s_blocksize << 3);
		}
		hint->search_start = hash;
	}
}

/* returns 1 if it finds an indirect item and gets valid hint info
 * from it, otherwise 0
 */
static int get_left_neighbor(reiserfs_blocknr_hint_t * hint)
{
	struct treepath *path;
	struct buffer_head *bh;
	struct item_head *ih;
	int pos_in_item;
	__le32 *item;
	int ret = 0;

	if (!hint->path)	/* reiserfs code can call this function w/o pointer to path
				 * structure supplied; then we rely on supplied search_start */
		return 0;

	path = hint->path;
	bh = get_last_bh(path);
	RFALSE(!bh, "green-4002: Illegal path specified to get_left_neighbor");
	ih = get_ih(path);
	pos_in_item = path->pos_in_item;
	item = get_item(path);

	hint->search_start = bh->b_blocknr;

	if (!hint->formatted_node && is_indirect_le_ih(ih)) {
		/* for indirect item: go to left and look for the first non-hole entry
		   in the indirect item */
		if (pos_in_item == I_UNFM_NUM(ih))
			pos_in_item--;
//          pos_in_item = I_UNFM_NUM (ih) - 1;
		while (pos_in_item >= 0) {
			int t = get_block_num(item, pos_in_item);
			if (t) {
				hint->search_start = t;
				ret = 1;
				break;
			}
			pos_in_item--;
		}
	}

	/* does result value fit into specified region? */
	return ret;
}

/* should be, if formatted node, then try to put on first part of the device
   specified as number of percent with mount option device, else try to put
   on last of device.  This is not to say it is good code to do so,
   but the effect should be measured.  */
static inline void set_border_in_hint(struct super_block *s,
				      reiserfs_blocknr_hint_t * hint)
{
	b_blocknr_t border =
	    SB_BLOCK_COUNT(s) / REISERFS_SB(s)->s_alloc_options.border;

	if (hint->formatted_node)
		hint->end = border - 1;
	else
		hint->beg = border;
}

static inline void displace_large_file(reiserfs_blocknr_hint_t * hint)
{
	if (TEST_OPTION(displace_based_on_dirid, hint->th->t_super))
		hint->search_start =
		    hint->beg +
		    keyed_hash((char *)(&INODE_PKEY(hint->inode)->k_dir_id),
			       4) % (hint->end - hint->beg);
	else
		hint->search_start =
		    hint->beg +
		    keyed_hash((char *)(&INODE_PKEY(hint->inode)->k_objectid),
			       4) % (hint->end - hint->beg);
}

static inline void hash_formatted_node(reiserfs_blocknr_hint_t * hint)
{
	char *hash_in;

	if (!hint->inode)
		hash_in = (char *)&hint->key.k_dir_id;
	else if (TEST_OPTION(displace_based_on_dirid, hint->th->t_super))
		hash_in = (char *)(&INODE_PKEY(hint->inode)->k_dir_id);
	else
		hash_in = (char *)(&INODE_PKEY(hint->inode)->k_objectid);

	hint->search_start =
	    hint->beg + keyed_hash(hash_in, 4) % (hint->end - hint->beg);
}

static inline int
this_blocknr_allocation_would_make_it_a_large_file(reiserfs_blocknr_hint_t *
						   hint)
{
	return hint->block ==
	    REISERFS_SB(hint->th->t_super)->s_alloc_options.large_file_size;
}

#ifdef DISPLACE_NEW_PACKING_LOCALITIES
static inline void displace_new_packing_locality(reiserfs_blocknr_hint_t * hint)
{
	struct in_core_key *key = &hint->key;

	hint->th->displace_new_blocks = 0;
	hint->search_start =
	    hint->beg + keyed_hash((char *)(&key->k_objectid),
				   4) % (hint->end - hint->beg);
}
#endif

static inline int old_hashed_relocation(reiserfs_blocknr_hint_t * hint)
{
	b_blocknr_t border;
	u32 hash_in;

	if (hint->formatted_node || hint->inode == NULL) {
		return 0;
	}

	hash_in = le32_to_cpu((INODE_PKEY(hint->inode))->k_dir_id);
	border =
	    hint->beg + (u32) keyed_hash(((char *)(&hash_in)),
					 4) % (hint->end - hint->beg - 1);
	if (border > hint->search_start)
		hint->search_start = border;

	return 1;
}

static inline int old_way(reiserfs_blocknr_hint_t * hint)
{
	b_blocknr_t border;

	if (hint->formatted_node || hint->inode == NULL) {
		return 0;
	}

	border =
	    hint->beg +
	    le32_to_cpu(INODE_PKEY(hint->inode)->k_dir_id) % (hint->end -
							      hint->beg);
	if (border > hint->search_start)
		hint->search_start = border;

	return 1;
}

static inline void hundredth_slices(reiserfs_blocknr_hint_t * hint)
{
	struct in_core_key *key = &hint->key;
	b_blocknr_t slice_start;

	slice_start =
	    (keyed_hash((char *)(&key->k_dir_id), 4) % 100) * (hint->end / 100);
	if (slice_start > hint->search_start
	    || slice_start + (hint->end / 100) <= hint->search_start) {
		hint->search_start = slice_start;
	}
}

static void determine_search_start(reiserfs_blocknr_hint_t * hint,
				   int amount_needed)
{
	struct super_block *s = hint->th->t_super;
	int unfm_hint;

	hint->beg = 0;
	hint->end = SB_BLOCK_COUNT(s) - 1;

	/* This is former border algorithm. Now with tunable border offset */
	if (concentrating_formatted_nodes(s))
		set_border_in_hint(s, hint);

#ifdef DISPLACE_NEW_PACKING_LOCALITIES
	/* whenever we create a new directory, we displace it.  At first we will
	   hash for location, later we might look for a moderately empty place for
	   it */
	if (displacing_new_packing_localities(s)
	    && hint->th->displace_new_blocks) {
		displace_new_packing_locality(hint);

		/* we do not continue determine_search_start,
		 * if new packing locality is being displaced */
		return;
	}
#endif

	/* all persons should feel encouraged to add more special cases here and
	 * test them */

	if (displacing_large_files(s) && !hint->formatted_node
	    && this_blocknr_allocation_would_make_it_a_large_file(hint)) {
		displace_large_file(hint);
		return;
	}

	/* if none of our special cases is relevant, use the left neighbor in the
	   tree order of the new node we are allocating for */
	if (hint->formatted_node && TEST_OPTION(hashed_formatted_nodes, s)) {
		hash_formatted_node(hint);
		return;
	}

	unfm_hint = get_left_neighbor(hint);

	/* Mimic old block allocator behaviour, that is if VFS allowed for preallocation,
	   new blocks are displaced based on directory ID. Also, if suggested search_start
	   is less than last preallocated block, we start searching from it, assuming that
	   HDD dataflow is faster in forward direction */
	if (TEST_OPTION(old_way, s)) {
		if (!hint->formatted_node) {
			if (!reiserfs_hashed_relocation(s))
				old_way(hint);
			else if (!reiserfs_no_unhashed_relocation(s))
				old_hashed_relocation(hint);

			if (hint->inode
			    && hint->search_start <
			    REISERFS_I(hint->inode)->i_prealloc_block)
				hint->search_start =
				    REISERFS_I(hint->inode)->i_prealloc_block;
		}
		return;
	}

	/* This is an approach proposed by Hans */
	if (TEST_OPTION(hundredth_slices, s)
	    && !(displacing_large_files(s) && !hint->formatted_node)) {
		hundredth_slices(hint);
		return;
	}

	/* old_hashed_relocation only works on unformatted */
	if (!unfm_hint && !hint->formatted_node &&
	    TEST_OPTION(old_hashed_relocation, s)) {
		old_hashed_relocation(hint);
	}
	/* new_hashed_relocation works with both formatted/unformatted nodes */
	if ((!unfm_hint || hint->formatted_node) &&
	    TEST_OPTION(new_hashed_relocation, s)) {
		new_hashed_relocation(hint);
	}
	/* dirid grouping works only on unformatted nodes */
	if (!unfm_hint && !hint->formatted_node && TEST_OPTION(dirid_groups, s)) {
		dirid_groups(hint);
	}
#ifdef DISPLACE_NEW_PACKING_LOCALITIES
	if (hint->formatted_node && TEST_OPTION(dirid_groups, s)) {
		dirid_groups(hint);
	}
#endif

	/* oid grouping works only on unformatted nodes */
	if (!unfm_hint && !hint->formatted_node && TEST_OPTION(oid_groups, s)) {
		oid_groups(hint);
	}
	return;
}

static int determine_prealloc_size(reiserfs_blocknr_hint_t * hint)
{
	/* make minimum size a mount option and benchmark both ways */
	/* we preallocate blocks only for regular files, specific size */
	/* benchmark preallocating always and see what happens */

	hint->prealloc_size = 0;

	if (!hint->formatted_node && hint->preallocate) {
		if (S_ISREG(hint->inode->i_mode)
		    && hint->inode->i_size >=
		    REISERFS_SB(hint->th->t_super)->s_alloc_options.
		    preallocmin * hint->inode->i_sb->s_blocksize)
			hint->prealloc_size =
			    REISERFS_SB(hint->th->t_super)->s_alloc_options.
			    preallocsize - 1;
	}
	return CARRY_ON;
}

/* XXX I know it could be merged with upper-level function;
   but may be result function would be too complex. */
static inline int allocate_without_wrapping_disk(reiserfs_blocknr_hint_t * hint,
						 b_blocknr_t * new_blocknrs,
						 b_blocknr_t start,
						 b_blocknr_t finish, int min,
						 int amount_needed,
						 int prealloc_size)
{
	int rest = amount_needed;
	int nr_allocated;

	while (rest > 0 && start <= finish) {
		nr_allocated = scan_bitmap(hint->th, &start, finish, min,
					   rest + prealloc_size,
					   !hint->formatted_node, hint->block);

		if (nr_allocated == 0)	/* no new blocks allocated, return */
			break;

		/* fill free_blocknrs array first */
		while (rest > 0 && nr_allocated > 0) {
			*new_blocknrs++ = start++;
			rest--;
			nr_allocated--;
		}

		/* do we have something to fill prealloc. array also ? */
		if (nr_allocated > 0) {
			/* it means prealloc_size was greater that 0 and we do preallocation */
			list_add(&REISERFS_I(hint->inode)->i_prealloc_list,
				 &SB_JOURNAL(hint->th->t_super)->
				 j_prealloc_list);
			REISERFS_I(hint->inode)->i_prealloc_block = start;
			REISERFS_I(hint->inode)->i_prealloc_count =
			    nr_allocated;
			break;
		}
	}

	return (amount_needed - rest);
}

static inline int blocknrs_and_prealloc_arrays_from_search_start
    (reiserfs_blocknr_hint_t * hint, b_blocknr_t * new_blocknrs,
     int amount_needed) {
	struct super_block *s = hint->th->t_super;
	b_blocknr_t start = hint->search_start;
	b_blocknr_t finish = SB_BLOCK_COUNT(s) - 1;
	int passno = 0;
	int nr_allocated = 0;

	determine_prealloc_size(hint);
	if (!hint->formatted_node) {
		int quota_ret;
#ifdef REISERQUOTA_DEBUG
		reiserfs_debug(s, REISERFS_DEBUG_CODE,
			       "reiserquota: allocating %d blocks id=%u",
			       amount_needed, hint->inode->i_uid);
#endif
		quota_ret =
		    DQUOT_ALLOC_BLOCK_NODIRTY(hint->inode, amount_needed);
		if (quota_ret)	/* Quota exceeded? */
			return QUOTA_EXCEEDED;
		if (hint->preallocate && hint->prealloc_size) {
#ifdef REISERQUOTA_DEBUG
			reiserfs_debug(s, REISERFS_DEBUG_CODE,
				       "reiserquota: allocating (prealloc) %d blocks id=%u",
				       hint->prealloc_size, hint->inode->i_uid);
#endif
			quota_ret =
			    DQUOT_PREALLOC_BLOCK_NODIRTY(hint->inode,
							 hint->prealloc_size);
			if (quota_ret)
				hint->preallocate = hint->prealloc_size = 0;
		}
		/* for unformatted nodes, force large allocations */
	}

	do {
		switch (passno++) {
		case 0:	/* Search from hint->search_start to end of disk */
			start = hint->search_start;
			finish = SB_BLOCK_COUNT(s) - 1;
			break;
		case 1:	/* Search from hint->beg to hint->search_start */
			start = hint->beg;
			finish = hint->search_start;
			break;
		case 2:	/* Last chance: Search from 0 to hint->beg */
			start = 0;
			finish = hint->beg;
			break;
		default:	/* We've tried searching everywhere, not enough space */
			/* Free the blocks */
			if (!hint->formatted_node) {
#ifdef REISERQUOTA_DEBUG
				reiserfs_debug(s, REISERFS_DEBUG_CODE,
					       "reiserquota: freeing (nospace) %d blocks id=%u",
					       amount_needed +
					       hint->prealloc_size -
					       nr_allocated,
					       hint->inode->i_uid);
#endif
				DQUOT_FREE_BLOCK_NODIRTY(hint->inode, amount_needed + hint->prealloc_size - nr_allocated);	/* Free not allocated blocks */
			}
			while (nr_allocated--)
				reiserfs_free_block(hint->th, hint->inode,
						    new_blocknrs[nr_allocated],
						    !hint->formatted_node);

			return NO_DISK_SPACE;
		}
	} while ((nr_allocated += allocate_without_wrapping_disk(hint,
								 new_blocknrs +
								 nr_allocated,
								 start, finish,
								 1,
								 amount_needed -
								 nr_allocated,
								 hint->
								 prealloc_size))
		 < amount_needed);
	if (!hint->formatted_node &&
	    amount_needed + hint->prealloc_size >
	    nr_allocated + REISERFS_I(hint->inode)->i_prealloc_count) {
		/* Some of preallocation blocks were not allocated */
#ifdef REISERQUOTA_DEBUG
		reiserfs_debug(s, REISERFS_DEBUG_CODE,
			       "reiserquota: freeing (failed prealloc) %d blocks id=%u",
			       amount_needed + hint->prealloc_size -
			       nr_allocated -
			       REISERFS_I(hint->inode)->i_prealloc_count,
			       hint->inode->i_uid);
#endif
		DQUOT_FREE_BLOCK_NODIRTY(hint->inode, amount_needed +
					 hint->prealloc_size - nr_allocated -
					 REISERFS_I(hint->inode)->
					 i_prealloc_count);
	}

	return CARRY_ON;
}

/* grab new blocknrs from preallocated list */
/* return amount still needed after using them */
static int use_preallocated_list_if_available(reiserfs_blocknr_hint_t * hint,
					      b_blocknr_t * new_blocknrs,
					      int amount_needed)
{
	struct inode *inode = hint->inode;

	if (REISERFS_I(inode)->i_prealloc_count > 0) {
		while (amount_needed) {

			*new_blocknrs++ = REISERFS_I(inode)->i_prealloc_block++;
			REISERFS_I(inode)->i_prealloc_count--;

			amount_needed--;

			if (REISERFS_I(inode)->i_prealloc_count <= 0) {
				list_del(&REISERFS_I(inode)->i_prealloc_list);
				break;
			}
		}
	}
	/* return amount still needed after using preallocated blocks */
	return amount_needed;
}

int reiserfs_allocate_blocknrs(reiserfs_blocknr_hint_t * hint, b_blocknr_t * new_blocknrs, int amount_needed, int reserved_by_us	/* Amount of blocks we have
																	   already reserved */ )
{
	int initial_amount_needed = amount_needed;
	int ret;
	struct super_block *s = hint->th->t_super;

	/* Check if there is enough space, taking into account reserved space */
	if (SB_FREE_BLOCKS(s) - REISERFS_SB(s)->reserved_blocks <
	    amount_needed - reserved_by_us)
		return NO_DISK_SPACE;
	/* should this be if !hint->inode &&  hint->preallocate? */
	/* do you mean hint->formatted_node can be removed ? - Zam */
	/* hint->formatted_node cannot be removed because we try to access
	   inode information here, and there is often no inode assotiated with
	   metadata allocations - green */

	if (!hint->formatted_node && hint->preallocate) {
		amount_needed = use_preallocated_list_if_available
		    (hint, new_blocknrs, amount_needed);
		if (amount_needed == 0)	/* all blocknrs we need we got from
					   prealloc. list */
			return CARRY_ON;
		new_blocknrs += (initial_amount_needed - amount_needed);
	}

	/* find search start and save it in hint structure */
	determine_search_start(hint, amount_needed);
	if (hint->search_start >= SB_BLOCK_COUNT(s))
		hint->search_start = SB_BLOCK_COUNT(s) - 1;

	/* allocation itself; fill new_blocknrs and preallocation arrays */
	ret = blocknrs_and_prealloc_arrays_from_search_start
	    (hint, new_blocknrs, amount_needed);

	/* we used prealloc. list to fill (partially) new_blocknrs array. If final allocation fails we
	 * need to return blocks back to prealloc. list or just free them. -- Zam (I chose second
	 * variant) */

	if (ret != CARRY_ON) {
		while (amount_needed++ < initial_amount_needed) {
			reiserfs_free_block(hint->th, hint->inode,
					    *(--new_blocknrs), 1);
		}
	}
	return ret;
}

void reiserfs_cache_bitmap_metadata(struct super_block *sb,
                                    struct buffer_head *bh,
                                    struct reiserfs_bitmap_info *info)
{
	unsigned long *cur = (unsigned long *)(bh->b_data + bh->b_size);

	/* The first bit must ALWAYS be 1 */
	BUG_ON(!reiserfs_test_le_bit(0, (unsigned long *)bh->b_data));

	info->free_count = 0;

	while (--cur >= (unsigned long *)bh->b_data) {
		int i;

		/* 0 and ~0 are special, we can optimize for them */
		if (*cur == 0)
			info->free_count += BITS_PER_LONG;
		else if (*cur != ~0L)	/* A mix, investigate */
			for (i = BITS_PER_LONG - 1; i >= 0; i--)
				if (!reiserfs_test_le_bit(i, cur))
					info->free_count++;
	}
}

struct buffer_head *reiserfs_read_bitmap_block(struct super_block *sb,
                                               unsigned int bitmap)
{
	b_blocknr_t block = (sb->s_blocksize << 3) * bitmap;
	struct reiserfs_bitmap_info *info = SB_AP_BITMAP(sb) + bitmap;
	struct buffer_head *bh;

	/* Way old format filesystems had the bitmaps packed up front.
	 * I doubt there are any of these left, but just in case... */
	if (unlikely(test_bit(REISERFS_OLD_FORMAT,
	                      &(REISERFS_SB(sb)->s_properties))))
		block = REISERFS_SB(sb)->s_sbh->b_blocknr + 1 + bitmap;
	else if (bitmap == 0)
		block = (REISERFS_DISK_OFFSET_IN_BYTES >> sb->s_blocksize_bits) + 1;

	bh = sb_bread(sb, block);
	if (bh == NULL)
		reiserfs_warning(sb, "sh-2029: %s: bitmap block (#%u) "
		                 "reading failed", __FUNCTION__, block);
	else {
		if (buffer_locked(bh)) {
			PROC_INFO_INC(sb, scan_bitmap.wait);
			__wait_on_buffer(bh);
		}
		BUG_ON(!buffer_uptodate(bh));
		BUG_ON(atomic_read(&bh->b_count) == 0);

		if (info->free_count == UINT_MAX)
			reiserfs_cache_bitmap_metadata(sb, bh, info);
	}

	return bh;
}

int reiserfs_init_bitmap_cache(struct super_block *sb)
{
	struct reiserfs_bitmap_info *bitmap;

	bitmap = vmalloc(sizeof (*bitmap) * SB_BMAP_NR(sb));
	if (bitmap == NULL)
		return -ENOMEM;

	memset(bitmap, 0xff, sizeof(*bitmap) * SB_BMAP_NR(sb));

	SB_AP_BITMAP(sb) = bitmap;

	return 0;
}

void reiserfs_free_bitmap_cache(struct super_block *sb)
{
	if (SB_AP_BITMAP(sb)) {
		vfree(SB_AP_BITMAP(sb));
		SB_AP_BITMAP(sb) = NULL;
	}
}
