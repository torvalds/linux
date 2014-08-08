/*
 * Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README
 */

/*
 * Now we have all buffers that must be used in balancing of the tree
 * Further calculations can not cause schedule(), and thus the buffer
 * tree will be stable until the balancing will be finished
 * balance the tree according to the analysis made before,
 * and using buffers obtained after all above.
 */

#include <linux/uaccess.h>
#include <linux/time.h>
#include "reiserfs.h"
#include <linux/buffer_head.h>
#include <linux/kernel.h>

static inline void buffer_info_init_left(struct tree_balance *tb,
                                         struct buffer_info *bi)
{
	bi->tb          = tb;
	bi->bi_bh       = tb->L[0];
	bi->bi_parent   = tb->FL[0];
	bi->bi_position = get_left_neighbor_position(tb, 0);
}

static inline void buffer_info_init_right(struct tree_balance *tb,
                                          struct buffer_info *bi)
{
	bi->tb          = tb;
	bi->bi_bh       = tb->R[0];
	bi->bi_parent   = tb->FR[0];
	bi->bi_position = get_right_neighbor_position(tb, 0);
}

static inline void buffer_info_init_tbS0(struct tree_balance *tb,
                                         struct buffer_info *bi)
{
	bi->tb          = tb;
	bi->bi_bh        = PATH_PLAST_BUFFER(tb->tb_path);
	bi->bi_parent   = PATH_H_PPARENT(tb->tb_path, 0);
	bi->bi_position = PATH_H_POSITION(tb->tb_path, 1);
}

static inline void buffer_info_init_bh(struct tree_balance *tb,
                                       struct buffer_info *bi,
                                       struct buffer_head *bh)
{
	bi->tb          = tb;
	bi->bi_bh       = bh;
	bi->bi_parent   = NULL;
	bi->bi_position = 0;
}

inline void do_balance_mark_leaf_dirty(struct tree_balance *tb,
				       struct buffer_head *bh, int flag)
{
	journal_mark_dirty(tb->transaction_handle, bh);
}

#define do_balance_mark_internal_dirty do_balance_mark_leaf_dirty
#define do_balance_mark_sb_dirty do_balance_mark_leaf_dirty

/*
 * summary:
 *  if deleting something ( tb->insert_size[0] < 0 )
 *    return(balance_leaf_when_delete()); (flag d handled here)
 *  else
 *    if lnum is larger than 0 we put items into the left node
 *    if rnum is larger than 0 we put items into the right node
 *    if snum1 is larger than 0 we put items into the new node s1
 *    if snum2 is larger than 0 we put items into the new node s2
 * Note that all *num* count new items being created.
 */

static void balance_leaf_when_delete_del(struct tree_balance *tb)
{
	struct buffer_head *tbS0 = PATH_PLAST_BUFFER(tb->tb_path);
	int item_pos = PATH_LAST_POSITION(tb->tb_path);
	struct buffer_info bi;
#ifdef CONFIG_REISERFS_CHECK
	struct item_head *ih = item_head(tbS0, item_pos);
#endif

	RFALSE(ih_item_len(ih) + IH_SIZE != -tb->insert_size[0],
	       "vs-12013: mode Delete, insert size %d, ih to be deleted %h",
	       -tb->insert_size[0], ih);

	buffer_info_init_tbS0(tb, &bi);
	leaf_delete_items(&bi, 0, item_pos, 1, -1);

	if (!item_pos && tb->CFL[0]) {
		if (B_NR_ITEMS(tbS0)) {
			replace_key(tb, tb->CFL[0], tb->lkey[0], tbS0, 0);
		} else {
			if (!PATH_H_POSITION(tb->tb_path, 1))
				replace_key(tb, tb->CFL[0], tb->lkey[0],
					    PATH_H_PPARENT(tb->tb_path, 0), 0);
		}
	}

	RFALSE(!item_pos && !tb->CFL[0],
	       "PAP-12020: tb->CFL[0]==%p, tb->L[0]==%p", tb->CFL[0],
	       tb->L[0]);
}

/* cut item in S[0] */
static void balance_leaf_when_delete_cut(struct tree_balance *tb)
{
	struct buffer_head *tbS0 = PATH_PLAST_BUFFER(tb->tb_path);
	int item_pos = PATH_LAST_POSITION(tb->tb_path);
	struct item_head *ih = item_head(tbS0, item_pos);
	int pos_in_item = tb->tb_path->pos_in_item;
	struct buffer_info bi;
	buffer_info_init_tbS0(tb, &bi);

	if (is_direntry_le_ih(ih)) {
		/*
		 * UFS unlink semantics are such that you can only
		 * delete one directory entry at a time.
		 *
		 * when we cut a directory tb->insert_size[0] means
		 * number of entries to be cut (always 1)
		 */
		tb->insert_size[0] = -1;
		leaf_cut_from_buffer(&bi, item_pos, pos_in_item,
				     -tb->insert_size[0]);

		RFALSE(!item_pos && !pos_in_item && !tb->CFL[0],
		       "PAP-12030: can not change delimiting key. CFL[0]=%p",
		       tb->CFL[0]);

		if (!item_pos && !pos_in_item && tb->CFL[0])
			replace_key(tb, tb->CFL[0], tb->lkey[0], tbS0, 0);
	} else {
		leaf_cut_from_buffer(&bi, item_pos, pos_in_item,
				     -tb->insert_size[0]);

		RFALSE(!ih_item_len(ih),
		       "PAP-12035: cut must leave non-zero dynamic "
		       "length of item");
	}
}

static int balance_leaf_when_delete_left(struct tree_balance *tb)
{
	struct buffer_head *tbS0 = PATH_PLAST_BUFFER(tb->tb_path);
	int n = B_NR_ITEMS(tbS0);

	/* L[0] must be joined with S[0] */
	if (tb->lnum[0] == -1) {
		/* R[0] must be also joined with S[0] */
		if (tb->rnum[0] == -1) {
			if (tb->FR[0] == PATH_H_PPARENT(tb->tb_path, 0)) {
				/*
				 * all contents of all the
				 * 3 buffers will be in L[0]
				 */
				if (PATH_H_POSITION(tb->tb_path, 1) == 0 &&
				    1 < B_NR_ITEMS(tb->FR[0]))
					replace_key(tb, tb->CFL[0],
						    tb->lkey[0], tb->FR[0], 1);

				leaf_move_items(LEAF_FROM_S_TO_L, tb, n, -1,
						NULL);
				leaf_move_items(LEAF_FROM_R_TO_L, tb,
						B_NR_ITEMS(tb->R[0]), -1,
						NULL);

				reiserfs_invalidate_buffer(tb, tbS0);
				reiserfs_invalidate_buffer(tb, tb->R[0]);

				return 0;
			}

			/* all contents of all the 3 buffers will be in R[0] */
			leaf_move_items(LEAF_FROM_S_TO_R, tb, n, -1, NULL);
			leaf_move_items(LEAF_FROM_L_TO_R, tb,
					B_NR_ITEMS(tb->L[0]), -1, NULL);

			/* right_delimiting_key is correct in R[0] */
			replace_key(tb, tb->CFR[0], tb->rkey[0], tb->R[0], 0);

			reiserfs_invalidate_buffer(tb, tbS0);
			reiserfs_invalidate_buffer(tb, tb->L[0]);

			return -1;
		}

		RFALSE(tb->rnum[0] != 0,
		       "PAP-12045: rnum must be 0 (%d)", tb->rnum[0]);
		/* all contents of L[0] and S[0] will be in L[0] */
		leaf_shift_left(tb, n, -1);

		reiserfs_invalidate_buffer(tb, tbS0);

		return 0;
	}

	/*
	 * a part of contents of S[0] will be in L[0] and
	 * the rest part of S[0] will be in R[0]
	 */

	RFALSE((tb->lnum[0] + tb->rnum[0] < n) ||
	       (tb->lnum[0] + tb->rnum[0] > n + 1),
	       "PAP-12050: rnum(%d) and lnum(%d) and item "
	       "number(%d) in S[0] are not consistent",
	       tb->rnum[0], tb->lnum[0], n);
	RFALSE((tb->lnum[0] + tb->rnum[0] == n) &&
	       (tb->lbytes != -1 || tb->rbytes != -1),
	       "PAP-12055: bad rbytes (%d)/lbytes (%d) "
	       "parameters when items are not split",
	       tb->rbytes, tb->lbytes);
	RFALSE((tb->lnum[0] + tb->rnum[0] == n + 1) &&
	       (tb->lbytes < 1 || tb->rbytes != -1),
	       "PAP-12060: bad rbytes (%d)/lbytes (%d) "
	       "parameters when items are split",
	       tb->rbytes, tb->lbytes);

	leaf_shift_left(tb, tb->lnum[0], tb->lbytes);
	leaf_shift_right(tb, tb->rnum[0], tb->rbytes);

	reiserfs_invalidate_buffer(tb, tbS0);

	return 0;
}

/*
 * Balance leaf node in case of delete or cut: insert_size[0] < 0
 *
 * lnum, rnum can have values >= -1
 *	-1 means that the neighbor must be joined with S
 *	 0 means that nothing should be done with the neighbor
 *	>0 means to shift entirely or partly the specified number of items
 *         to the neighbor
 */
static int balance_leaf_when_delete(struct tree_balance *tb, int flag)
{
	struct buffer_head *tbS0 = PATH_PLAST_BUFFER(tb->tb_path);
	int item_pos = PATH_LAST_POSITION(tb->tb_path);
	struct buffer_info bi;
	int n;
	struct item_head *ih;

	RFALSE(tb->FR[0] && B_LEVEL(tb->FR[0]) != DISK_LEAF_NODE_LEVEL + 1,
	       "vs- 12000: level: wrong FR %z", tb->FR[0]);
	RFALSE(tb->blknum[0] > 1,
	       "PAP-12005: tb->blknum == %d, can not be > 1", tb->blknum[0]);
	RFALSE(!tb->blknum[0] && !PATH_H_PPARENT(tb->tb_path, 0),
	       "PAP-12010: tree can not be empty");

	ih = item_head(tbS0, item_pos);
	buffer_info_init_tbS0(tb, &bi);

	/* Delete or truncate the item */

	BUG_ON(flag != M_DELETE && flag != M_CUT);
	if (flag == M_DELETE)
		balance_leaf_when_delete_del(tb);
	else /* M_CUT */
		balance_leaf_when_delete_cut(tb);


	/*
	 * the rule is that no shifting occurs unless by shifting
	 * a node can be freed
	 */
	n = B_NR_ITEMS(tbS0);


	/* L[0] takes part in balancing */
	if (tb->lnum[0])
		return balance_leaf_when_delete_left(tb);

	if (tb->rnum[0] == -1) {
		/* all contents of R[0] and S[0] will be in R[0] */
		leaf_shift_right(tb, n, -1);
		reiserfs_invalidate_buffer(tb, tbS0);
		return 0;
	}

	RFALSE(tb->rnum[0],
	       "PAP-12065: bad rnum parameter must be 0 (%d)", tb->rnum[0]);
	return 0;
}

static void balance_leaf_insert_left(struct tree_balance *tb,
				     struct item_head *ih, const char *body)
{
	int ret;
	struct buffer_info bi;
	int n = B_NR_ITEMS(tb->L[0]);

	if (tb->item_pos == tb->lnum[0] - 1 && tb->lbytes != -1) {
		/* part of new item falls into L[0] */
		int new_item_len, shift;
		int version;

		ret = leaf_shift_left(tb, tb->lnum[0] - 1, -1);

		/* Calculate item length to insert to S[0] */
		new_item_len = ih_item_len(ih) - tb->lbytes;

		/* Calculate and check item length to insert to L[0] */
		put_ih_item_len(ih, ih_item_len(ih) - new_item_len);

		RFALSE(ih_item_len(ih) <= 0,
		       "PAP-12080: there is nothing to insert into L[0]: "
		       "ih_item_len=%d", ih_item_len(ih));

		/* Insert new item into L[0] */
		buffer_info_init_left(tb, &bi);
		leaf_insert_into_buf(&bi, n + tb->item_pos - ret, ih, body,
			     min_t(int, tb->zeroes_num, ih_item_len(ih)));

		version = ih_version(ih);

		/*
		 * Calculate key component, item length and body to
		 * insert into S[0]
		 */
		shift = 0;
		if (is_indirect_le_ih(ih))
			shift = tb->tb_sb->s_blocksize_bits - UNFM_P_SHIFT;

		add_le_ih_k_offset(ih, tb->lbytes << shift);

		put_ih_item_len(ih, new_item_len);
		if (tb->lbytes > tb->zeroes_num) {
			body += (tb->lbytes - tb->zeroes_num);
			tb->zeroes_num = 0;
		} else
			tb->zeroes_num -= tb->lbytes;

		RFALSE(ih_item_len(ih) <= 0,
		       "PAP-12085: there is nothing to insert into S[0]: "
		       "ih_item_len=%d", ih_item_len(ih));
	} else {
		/* new item in whole falls into L[0] */
		/* Shift lnum[0]-1 items to L[0] */
		ret = leaf_shift_left(tb, tb->lnum[0] - 1, tb->lbytes);

		/* Insert new item into L[0] */
		buffer_info_init_left(tb, &bi);
		leaf_insert_into_buf(&bi, n + tb->item_pos - ret, ih, body,
				     tb->zeroes_num);
		tb->insert_size[0] = 0;
		tb->zeroes_num = 0;
	}
}

static void balance_leaf_paste_left_shift_dirent(struct tree_balance *tb,
						 struct item_head *ih,
						 const char *body)
{
	int n = B_NR_ITEMS(tb->L[0]);
	struct buffer_info bi;

	RFALSE(tb->zeroes_num,
	       "PAP-12090: invalid parameter in case of a directory");

	/* directory item */
	if (tb->lbytes > tb->pos_in_item) {
		/* new directory entry falls into L[0] */
		struct item_head *pasted;
		int ret, l_pos_in_item = tb->pos_in_item;

		/*
		 * Shift lnum[0] - 1 items in whole.
		 * Shift lbytes - 1 entries from given directory item
		 */
		ret = leaf_shift_left(tb, tb->lnum[0], tb->lbytes - 1);
		if (ret && !tb->item_pos) {
			pasted = item_head(tb->L[0], B_NR_ITEMS(tb->L[0]) - 1);
			l_pos_in_item += ih_entry_count(pasted) -
					 (tb->lbytes - 1);
		}

		/* Append given directory entry to directory item */
		buffer_info_init_left(tb, &bi);
		leaf_paste_in_buffer(&bi, n + tb->item_pos - ret,
				     l_pos_in_item, tb->insert_size[0],
				     body, tb->zeroes_num);

		/*
		 * previous string prepared space for pasting new entry,
		 * following string pastes this entry
		 */

		/*
		 * when we have merge directory item, pos_in_item
		 * has been changed too
		 */

		/* paste new directory entry. 1 is entry number */
		leaf_paste_entries(&bi, n + tb->item_pos - ret,
				   l_pos_in_item, 1,
				   (struct reiserfs_de_head *) body,
				   body + DEH_SIZE, tb->insert_size[0]);
		tb->insert_size[0] = 0;
	} else {
		/* new directory item doesn't fall into L[0] */
		/*
		 * Shift lnum[0]-1 items in whole. Shift lbytes
		 * directory entries from directory item number lnum[0]
		 */
		leaf_shift_left(tb, tb->lnum[0], tb->lbytes);
	}

	/* Calculate new position to append in item body */
	tb->pos_in_item -= tb->lbytes;
}

static void balance_leaf_paste_left_shift(struct tree_balance *tb,
					  struct item_head *ih,
					  const char *body)
{
	struct buffer_head *tbS0 = PATH_PLAST_BUFFER(tb->tb_path);
	int n = B_NR_ITEMS(tb->L[0]);
	struct buffer_info bi;

	if (is_direntry_le_ih(item_head(tbS0, tb->item_pos))) {
		balance_leaf_paste_left_shift_dirent(tb, ih, body);
		return;
	}

	RFALSE(tb->lbytes <= 0,
	       "PAP-12095: there is nothing to shift to L[0]. "
	       "lbytes=%d", tb->lbytes);
	RFALSE(tb->pos_in_item != ih_item_len(item_head(tbS0, tb->item_pos)),
	       "PAP-12100: incorrect position to paste: "
	       "item_len=%d, pos_in_item=%d",
	       ih_item_len(item_head(tbS0, tb->item_pos)), tb->pos_in_item);

	/* appended item will be in L[0] in whole */
	if (tb->lbytes >= tb->pos_in_item) {
		struct item_head *tbS0_pos_ih, *tbL0_ih;
		struct item_head *tbS0_0_ih;
		struct reiserfs_key *left_delim_key;
		int ret, l_n, version, temp_l;

		tbS0_pos_ih = item_head(tbS0, tb->item_pos);
		tbS0_0_ih = item_head(tbS0, 0);

		/*
		 * this bytes number must be appended
		 * to the last item of L[h]
		 */
		l_n = tb->lbytes - tb->pos_in_item;

		/* Calculate new insert_size[0] */
		tb->insert_size[0] -= l_n;

		RFALSE(tb->insert_size[0] <= 0,
		       "PAP-12105: there is nothing to paste into "
		       "L[0]. insert_size=%d", tb->insert_size[0]);

		ret = leaf_shift_left(tb, tb->lnum[0],
				      ih_item_len(tbS0_pos_ih));

		tbL0_ih = item_head(tb->L[0], n + tb->item_pos - ret);

		/* Append to body of item in L[0] */
		buffer_info_init_left(tb, &bi);
		leaf_paste_in_buffer(&bi, n + tb->item_pos - ret,
				     ih_item_len(tbL0_ih), l_n, body,
				     min_t(int, l_n, tb->zeroes_num));

		/*
		 * 0-th item in S0 can be only of DIRECT type
		 * when l_n != 0
		 */
		temp_l = l_n;

		RFALSE(ih_item_len(tbS0_0_ih),
		       "PAP-12106: item length must be 0");
		RFALSE(comp_short_le_keys(&tbS0_0_ih->ih_key,
		       leaf_key(tb->L[0], n + tb->item_pos - ret)),
		       "PAP-12107: items must be of the same file");

		if (is_indirect_le_ih(tbL0_ih)) {
			int shift = tb->tb_sb->s_blocksize_bits - UNFM_P_SHIFT;
			temp_l = l_n << shift;
		}
		/* update key of first item in S0 */
		version = ih_version(tbS0_0_ih);
		add_le_key_k_offset(version, &tbS0_0_ih->ih_key, temp_l);

		/* update left delimiting key */
		left_delim_key = internal_key(tb->CFL[0], tb->lkey[0]);
		add_le_key_k_offset(version, left_delim_key, temp_l);

		/*
		 * Calculate new body, position in item and
		 * insert_size[0]
		 */
		if (l_n > tb->zeroes_num) {
			body += (l_n - tb->zeroes_num);
			tb->zeroes_num = 0;
		} else
			tb->zeroes_num -= l_n;
		tb->pos_in_item = 0;

		RFALSE(comp_short_le_keys(&tbS0_0_ih->ih_key,
					  leaf_key(tb->L[0],
						 B_NR_ITEMS(tb->L[0]) - 1)) ||
		       !op_is_left_mergeable(leaf_key(tbS0, 0), tbS0->b_size) ||
		       !op_is_left_mergeable(left_delim_key, tbS0->b_size),
		       "PAP-12120: item must be merge-able with left "
		       "neighboring item");
	} else {
		/* only part of the appended item will be in L[0] */

		/* Calculate position in item for append in S[0] */
		tb->pos_in_item -= tb->lbytes;

		RFALSE(tb->pos_in_item <= 0,
		       "PAP-12125: no place for paste. pos_in_item=%d",
		       tb->pos_in_item);

		/*
		 * Shift lnum[0] - 1 items in whole.
		 * Shift lbytes - 1 byte from item number lnum[0]
		 */
		leaf_shift_left(tb, tb->lnum[0], tb->lbytes);
	}
}


/* appended item will be in L[0] in whole */
static void balance_leaf_paste_left_whole(struct tree_balance *tb,
					  struct item_head *ih,
					  const char *body)
{
	struct buffer_head *tbS0 = PATH_PLAST_BUFFER(tb->tb_path);
	int n = B_NR_ITEMS(tb->L[0]);
	struct buffer_info bi;
	struct item_head *pasted;
	int ret;

	/* if we paste into first item of S[0] and it is left mergable */
	if (!tb->item_pos &&
	    op_is_left_mergeable(leaf_key(tbS0, 0), tbS0->b_size)) {
		/*
		 * then increment pos_in_item by the size of the
		 * last item in L[0]
		 */
		pasted = item_head(tb->L[0], n - 1);
		if (is_direntry_le_ih(pasted))
			tb->pos_in_item += ih_entry_count(pasted);
		else
			tb->pos_in_item += ih_item_len(pasted);
	}

	/*
	 * Shift lnum[0] - 1 items in whole.
	 * Shift lbytes - 1 byte from item number lnum[0]
	 */
	ret = leaf_shift_left(tb, tb->lnum[0], tb->lbytes);

	/* Append to body of item in L[0] */
	buffer_info_init_left(tb, &bi);
	leaf_paste_in_buffer(&bi, n + tb->item_pos - ret, tb->pos_in_item,
			     tb->insert_size[0], body, tb->zeroes_num);

	/* if appended item is directory, paste entry */
	pasted = item_head(tb->L[0], n + tb->item_pos - ret);
	if (is_direntry_le_ih(pasted))
		leaf_paste_entries(&bi, n + tb->item_pos - ret,
				   tb->pos_in_item, 1,
				   (struct reiserfs_de_head *)body,
				   body + DEH_SIZE, tb->insert_size[0]);

	/*
	 * if appended item is indirect item, put unformatted node
	 * into un list
	 */
	if (is_indirect_le_ih(pasted))
		set_ih_free_space(pasted, 0);

	tb->insert_size[0] = 0;
	tb->zeroes_num = 0;
}

static void balance_leaf_paste_left(struct tree_balance *tb,
				    struct item_head *ih, const char *body)
{
	/* we must shift the part of the appended item */
	if (tb->item_pos == tb->lnum[0] - 1 && tb->lbytes != -1)
		balance_leaf_paste_left_shift(tb, ih, body);
	else
		balance_leaf_paste_left_whole(tb, ih, body);
}

/* Shift lnum[0] items from S[0] to the left neighbor L[0] */
static void balance_leaf_left(struct tree_balance *tb, struct item_head *ih,
			      const char *body, int flag)
{
	if (tb->lnum[0] <= 0)
		return;

	/* new item or it part falls to L[0], shift it too */
	if (tb->item_pos < tb->lnum[0]) {
		BUG_ON(flag != M_INSERT && flag != M_PASTE);

		if (flag == M_INSERT)
			balance_leaf_insert_left(tb, ih, body);
		else /* M_PASTE */
			balance_leaf_paste_left(tb, ih, body);
	} else
		/* new item doesn't fall into L[0] */
		leaf_shift_left(tb, tb->lnum[0], tb->lbytes);
}


static void balance_leaf_insert_right(struct tree_balance *tb,
				      struct item_head *ih, const char *body)
{

	struct buffer_head *tbS0 = PATH_PLAST_BUFFER(tb->tb_path);
	int n = B_NR_ITEMS(tbS0);
	struct buffer_info bi;
	int ret;

	/* new item or part of it doesn't fall into R[0] */
	if (n - tb->rnum[0] >= tb->item_pos) {
		leaf_shift_right(tb, tb->rnum[0], tb->rbytes);
		return;
	}

	/* new item or its part falls to R[0] */

	/* part of new item falls into R[0] */
	if (tb->item_pos == n - tb->rnum[0] + 1 && tb->rbytes != -1) {
		loff_t old_key_comp, old_len, r_zeroes_number;
		const char *r_body;
		int version, shift;
		loff_t offset;

		leaf_shift_right(tb, tb->rnum[0] - 1, -1);

		version = ih_version(ih);

		/* Remember key component and item length */
		old_key_comp = le_ih_k_offset(ih);
		old_len = ih_item_len(ih);

		/*
		 * Calculate key component and item length to insert
		 * into R[0]
		 */
		shift = 0;
		if (is_indirect_le_ih(ih))
			shift = tb->tb_sb->s_blocksize_bits - UNFM_P_SHIFT;
		offset = le_ih_k_offset(ih) + ((old_len - tb->rbytes) << shift);
		set_le_ih_k_offset(ih, offset);
		put_ih_item_len(ih, tb->rbytes);

		/* Insert part of the item into R[0] */
		buffer_info_init_right(tb, &bi);
		if ((old_len - tb->rbytes) > tb->zeroes_num) {
			r_zeroes_number = 0;
			r_body = body + (old_len - tb->rbytes) - tb->zeroes_num;
		} else {
			r_body = body;
			r_zeroes_number = tb->zeroes_num -
					  (old_len - tb->rbytes);
			tb->zeroes_num -= r_zeroes_number;
		}

		leaf_insert_into_buf(&bi, 0, ih, r_body, r_zeroes_number);

		/* Replace right delimiting key by first key in R[0] */
		replace_key(tb, tb->CFR[0], tb->rkey[0], tb->R[0], 0);

		/*
		 * Calculate key component and item length to
		 * insert into S[0]
		 */
		set_le_ih_k_offset(ih, old_key_comp);
		put_ih_item_len(ih, old_len - tb->rbytes);

		tb->insert_size[0] -= tb->rbytes;

	} else {
		/* whole new item falls into R[0] */

		/* Shift rnum[0]-1 items to R[0] */
		ret = leaf_shift_right(tb, tb->rnum[0] - 1, tb->rbytes);

		/* Insert new item into R[0] */
		buffer_info_init_right(tb, &bi);
		leaf_insert_into_buf(&bi, tb->item_pos - n + tb->rnum[0] - 1,
				     ih, body, tb->zeroes_num);

		if (tb->item_pos - n + tb->rnum[0] - 1 == 0)
			replace_key(tb, tb->CFR[0], tb->rkey[0], tb->R[0], 0);

		tb->zeroes_num = tb->insert_size[0] = 0;
	}
}


static void balance_leaf_paste_right_shift_dirent(struct tree_balance *tb,
				     struct item_head *ih, const char *body)
{
	struct buffer_head *tbS0 = PATH_PLAST_BUFFER(tb->tb_path);
	struct buffer_info bi;
	int entry_count;

	RFALSE(tb->zeroes_num,
	       "PAP-12145: invalid parameter in case of a directory");
	entry_count = ih_entry_count(item_head(tbS0, tb->item_pos));

	/* new directory entry falls into R[0] */
	if (entry_count - tb->rbytes < tb->pos_in_item) {
		int paste_entry_position;

		RFALSE(tb->rbytes - 1 >= entry_count || !tb->insert_size[0],
		       "PAP-12150: no enough of entries to shift to R[0]: "
		       "rbytes=%d, entry_count=%d", tb->rbytes, entry_count);

		/*
		 * Shift rnum[0]-1 items in whole.
		 * Shift rbytes-1 directory entries from directory
		 * item number rnum[0]
		 */
		leaf_shift_right(tb, tb->rnum[0], tb->rbytes - 1);

		/* Paste given directory entry to directory item */
		paste_entry_position = tb->pos_in_item - entry_count +
				       tb->rbytes - 1;
		buffer_info_init_right(tb, &bi);
		leaf_paste_in_buffer(&bi, 0, paste_entry_position,
				     tb->insert_size[0], body, tb->zeroes_num);

		/* paste entry */
		leaf_paste_entries(&bi, 0, paste_entry_position, 1,
				   (struct reiserfs_de_head *) body,
				   body + DEH_SIZE, tb->insert_size[0]);

		/* change delimiting keys */
		if (paste_entry_position == 0)
			replace_key(tb, tb->CFR[0], tb->rkey[0], tb->R[0], 0);

		tb->insert_size[0] = 0;
		tb->pos_in_item++;
	} else {
		/* new directory entry doesn't fall into R[0] */
		leaf_shift_right(tb, tb->rnum[0], tb->rbytes);
	}
}

static void balance_leaf_paste_right_shift(struct tree_balance *tb,
				     struct item_head *ih, const char *body)
{
	struct buffer_head *tbS0 = PATH_PLAST_BUFFER(tb->tb_path);
	int n_shift, n_rem, r_zeroes_number, version;
	unsigned long temp_rem;
	const char *r_body;
	struct buffer_info bi;

	/* we append to directory item */
	if (is_direntry_le_ih(item_head(tbS0, tb->item_pos))) {
		balance_leaf_paste_right_shift_dirent(tb, ih, body);
		return;
	}

	/* regular object */

	/*
	 * Calculate number of bytes which must be shifted
	 * from appended item
	 */
	n_shift = tb->rbytes - tb->insert_size[0];
	if (n_shift < 0)
		n_shift = 0;

	RFALSE(tb->pos_in_item != ih_item_len(item_head(tbS0, tb->item_pos)),
	       "PAP-12155: invalid position to paste. ih_item_len=%d, "
	       "pos_in_item=%d", tb->pos_in_item,
	       ih_item_len(item_head(tbS0, tb->item_pos)));

	leaf_shift_right(tb, tb->rnum[0], n_shift);

	/*
	 * Calculate number of bytes which must remain in body
	 * after appending to R[0]
	 */
	n_rem = tb->insert_size[0] - tb->rbytes;
	if (n_rem < 0)
		n_rem = 0;

	temp_rem = n_rem;

	version = ih_version(item_head(tb->R[0], 0));

	if (is_indirect_le_key(version, leaf_key(tb->R[0], 0))) {
		int shift = tb->tb_sb->s_blocksize_bits - UNFM_P_SHIFT;
		temp_rem = n_rem << shift;
	}

	add_le_key_k_offset(version, leaf_key(tb->R[0], 0), temp_rem);
	add_le_key_k_offset(version, internal_key(tb->CFR[0], tb->rkey[0]),
			    temp_rem);

	do_balance_mark_internal_dirty(tb, tb->CFR[0], 0);

	/* Append part of body into R[0] */
	buffer_info_init_right(tb, &bi);
	if (n_rem > tb->zeroes_num) {
		r_zeroes_number = 0;
		r_body = body + n_rem - tb->zeroes_num;
	} else {
		r_body = body;
		r_zeroes_number = tb->zeroes_num - n_rem;
		tb->zeroes_num -= r_zeroes_number;
	}

	leaf_paste_in_buffer(&bi, 0, n_shift, tb->insert_size[0] - n_rem,
			     r_body, r_zeroes_number);

	if (is_indirect_le_ih(item_head(tb->R[0], 0)))
		set_ih_free_space(item_head(tb->R[0], 0), 0);

	tb->insert_size[0] = n_rem;
	if (!n_rem)
		tb->pos_in_item++;
}

static void balance_leaf_paste_right_whole(struct tree_balance *tb,
				     struct item_head *ih, const char *body)
{
	struct buffer_head *tbS0 = PATH_PLAST_BUFFER(tb->tb_path);
	int n = B_NR_ITEMS(tbS0);
	struct item_head *pasted;
	struct buffer_info bi;

							buffer_info_init_right(tb, &bi);
	leaf_shift_right(tb, tb->rnum[0], tb->rbytes);

	/* append item in R[0] */
	if (tb->pos_in_item >= 0) {
		buffer_info_init_right(tb, &bi);
		leaf_paste_in_buffer(&bi, tb->item_pos - n + tb->rnum[0],
				     tb->pos_in_item, tb->insert_size[0], body,
				     tb->zeroes_num);
	}

	/* paste new entry, if item is directory item */
	pasted = item_head(tb->R[0], tb->item_pos - n + tb->rnum[0]);
	if (is_direntry_le_ih(pasted) && tb->pos_in_item >= 0) {
		leaf_paste_entries(&bi, tb->item_pos - n + tb->rnum[0],
				   tb->pos_in_item, 1,
				   (struct reiserfs_de_head *)body,
				   body + DEH_SIZE, tb->insert_size[0]);

		if (!tb->pos_in_item) {

			RFALSE(tb->item_pos - n + tb->rnum[0],
			       "PAP-12165: directory item must be first "
			       "item of node when pasting is in 0th position");

			/* update delimiting keys */
			replace_key(tb, tb->CFR[0], tb->rkey[0], tb->R[0], 0);
		}
	}

	if (is_indirect_le_ih(pasted))
		set_ih_free_space(pasted, 0);
	tb->zeroes_num = tb->insert_size[0] = 0;
}

static void balance_leaf_paste_right(struct tree_balance *tb,
				     struct item_head *ih, const char *body)
{
	struct buffer_head *tbS0 = PATH_PLAST_BUFFER(tb->tb_path);
	int n = B_NR_ITEMS(tbS0);

	/* new item doesn't fall into R[0] */
	if (n - tb->rnum[0] > tb->item_pos) {
		leaf_shift_right(tb, tb->rnum[0], tb->rbytes);
		return;
	}

	/* pasted item or part of it falls to R[0] */

	if (tb->item_pos == n - tb->rnum[0] && tb->rbytes != -1)
		/* we must shift the part of the appended item */
		balance_leaf_paste_right_shift(tb, ih, body);
	else
		/* pasted item in whole falls into R[0] */
		balance_leaf_paste_right_whole(tb, ih, body);
}

/* shift rnum[0] items from S[0] to the right neighbor R[0] */
static void balance_leaf_right(struct tree_balance *tb, struct item_head *ih,
			       const char *body, int flag)
{
	if (tb->rnum[0] <= 0)
		return;

	BUG_ON(flag != M_INSERT && flag != M_PASTE);

	if (flag == M_INSERT)
		balance_leaf_insert_right(tb, ih, body);
	else /* M_PASTE */
		balance_leaf_paste_right(tb, ih, body);
}

static void balance_leaf_new_nodes_insert(struct tree_balance *tb,
					  struct item_head *ih,
					  const char *body,
					  struct item_head *insert_key,
					  struct buffer_head **insert_ptr,
					  int i)
{
	struct buffer_head *tbS0 = PATH_PLAST_BUFFER(tb->tb_path);
	int n = B_NR_ITEMS(tbS0);
	struct buffer_info bi;
	int shift;

	/* new item or it part don't falls into S_new[i] */
	if (n - tb->snum[i] >= tb->item_pos) {
		leaf_move_items(LEAF_FROM_S_TO_SNEW, tb,
				tb->snum[i], tb->sbytes[i], tb->S_new[i]);
		return;
	}

	/* new item or it's part falls to first new node S_new[i] */

	/* part of new item falls into S_new[i] */
	if (tb->item_pos == n - tb->snum[i] + 1 && tb->sbytes[i] != -1) {
		int old_key_comp, old_len, r_zeroes_number;
		const char *r_body;
		int version;

		/* Move snum[i]-1 items from S[0] to S_new[i] */
		leaf_move_items(LEAF_FROM_S_TO_SNEW, tb, tb->snum[i] - 1, -1,
				tb->S_new[i]);

		/* Remember key component and item length */
		version = ih_version(ih);
		old_key_comp = le_ih_k_offset(ih);
		old_len = ih_item_len(ih);

		/*
		 * Calculate key component and item length to insert
		 * into S_new[i]
		 */
		shift = 0;
		if (is_indirect_le_ih(ih))
			shift = tb->tb_sb->s_blocksize_bits - UNFM_P_SHIFT;
		set_le_ih_k_offset(ih,
				   le_ih_k_offset(ih) +
				   ((old_len - tb->sbytes[i]) << shift));

		put_ih_item_len(ih, tb->sbytes[i]);

		/* Insert part of the item into S_new[i] before 0-th item */
		buffer_info_init_bh(tb, &bi, tb->S_new[i]);

		if ((old_len - tb->sbytes[i]) > tb->zeroes_num) {
			r_zeroes_number = 0;
			r_body = body + (old_len - tb->sbytes[i]) -
					 tb->zeroes_num;
		} else {
			r_body = body;
			r_zeroes_number = tb->zeroes_num - (old_len -
					  tb->sbytes[i]);
			tb->zeroes_num -= r_zeroes_number;
		}

		leaf_insert_into_buf(&bi, 0, ih, r_body, r_zeroes_number);

		/*
		 * Calculate key component and item length to
		 * insert into S[i]
		 */
		set_le_ih_k_offset(ih, old_key_comp);
		put_ih_item_len(ih, old_len - tb->sbytes[i]);
		tb->insert_size[0] -= tb->sbytes[i];
	} else {
		/* whole new item falls into S_new[i] */

		/*
		 * Shift snum[0] - 1 items to S_new[i]
		 * (sbytes[i] of split item)
		 */
		leaf_move_items(LEAF_FROM_S_TO_SNEW, tb,
				tb->snum[i] - 1, tb->sbytes[i], tb->S_new[i]);

		/* Insert new item into S_new[i] */
		buffer_info_init_bh(tb, &bi, tb->S_new[i]);
		leaf_insert_into_buf(&bi, tb->item_pos - n + tb->snum[i] - 1,
				     ih, body, tb->zeroes_num);

		tb->zeroes_num = tb->insert_size[0] = 0;
	}
}

/* we append to directory item */
static void balance_leaf_new_nodes_paste_dirent(struct tree_balance *tb,
					 struct item_head *ih,
					 const char *body,
					 struct item_head *insert_key,
					 struct buffer_head **insert_ptr,
					 int i)
{
	struct buffer_head *tbS0 = PATH_PLAST_BUFFER(tb->tb_path);
	struct item_head *aux_ih = item_head(tbS0, tb->item_pos);
	int entry_count = ih_entry_count(aux_ih);
	struct buffer_info bi;

	if (entry_count - tb->sbytes[i] < tb->pos_in_item &&
	    tb->pos_in_item <= entry_count) {
		/* new directory entry falls into S_new[i] */

		RFALSE(!tb->insert_size[0],
		       "PAP-12215: insert_size is already 0");
		RFALSE(tb->sbytes[i] - 1 >= entry_count,
		       "PAP-12220: there are no so much entries (%d), only %d",
		       tb->sbytes[i] - 1, entry_count);

		/*
		 * Shift snum[i]-1 items in whole.
		 * Shift sbytes[i] directory entries
		 * from directory item number snum[i]
		 */
		leaf_move_items(LEAF_FROM_S_TO_SNEW, tb, tb->snum[i],
				tb->sbytes[i] - 1, tb->S_new[i]);

		/*
		 * Paste given directory entry to
		 * directory item
		 */
		buffer_info_init_bh(tb, &bi, tb->S_new[i]);
		leaf_paste_in_buffer(&bi, 0, tb->pos_in_item - entry_count +
				     tb->sbytes[i] - 1, tb->insert_size[0],
				     body, tb->zeroes_num);

		/* paste new directory entry */
		leaf_paste_entries(&bi, 0, tb->pos_in_item - entry_count +
				   tb->sbytes[i] - 1, 1,
				   (struct reiserfs_de_head *) body,
				   body + DEH_SIZE, tb->insert_size[0]);

		tb->insert_size[0] = 0;
		tb->pos_in_item++;
	} else {
		/* new directory entry doesn't fall into S_new[i] */
		leaf_move_items(LEAF_FROM_S_TO_SNEW, tb, tb->snum[i],
				tb->sbytes[i], tb->S_new[i]);
	}

}

static void balance_leaf_new_nodes_paste_shift(struct tree_balance *tb,
					 struct item_head *ih,
					 const char *body,
					 struct item_head *insert_key,
					 struct buffer_head **insert_ptr,
					 int i)
{
	struct buffer_head *tbS0 = PATH_PLAST_BUFFER(tb->tb_path);
	struct item_head *aux_ih = item_head(tbS0, tb->item_pos);
	int n_shift, n_rem, r_zeroes_number, shift;
	const char *r_body;
	struct item_head *tmp;
	struct buffer_info bi;

	RFALSE(ih, "PAP-12210: ih must be 0");

	if (is_direntry_le_ih(aux_ih)) {
		balance_leaf_new_nodes_paste_dirent(tb, ih, body, insert_key,
						    insert_ptr, i);
		return;
	}

	/* regular object */


	RFALSE(tb->pos_in_item != ih_item_len(item_head(tbS0, tb->item_pos)) ||
	       tb->insert_size[0] <= 0,
	       "PAP-12225: item too short or insert_size <= 0");

	/*
	 * Calculate number of bytes which must be shifted from appended item
	 */
	n_shift = tb->sbytes[i] - tb->insert_size[0];
	if (n_shift < 0)
		n_shift = 0;
	leaf_move_items(LEAF_FROM_S_TO_SNEW, tb, tb->snum[i], n_shift,
			tb->S_new[i]);

	/*
	 * Calculate number of bytes which must remain in body after
	 * append to S_new[i]
	 */
	n_rem = tb->insert_size[0] - tb->sbytes[i];
	if (n_rem < 0)
		n_rem = 0;

	/* Append part of body into S_new[0] */
	buffer_info_init_bh(tb, &bi, tb->S_new[i]);
	if (n_rem > tb->zeroes_num) {
		r_zeroes_number = 0;
		r_body = body + n_rem - tb->zeroes_num;
	} else {
		r_body = body;
		r_zeroes_number = tb->zeroes_num - n_rem;
		tb->zeroes_num -= r_zeroes_number;
	}

	leaf_paste_in_buffer(&bi, 0, n_shift, tb->insert_size[0] - n_rem,
			     r_body, r_zeroes_number);

	tmp = item_head(tb->S_new[i], 0);
	shift = 0;
	if (is_indirect_le_ih(tmp)) {
		set_ih_free_space(tmp, 0);
		shift = tb->tb_sb->s_blocksize_bits - UNFM_P_SHIFT;
	}
	add_le_ih_k_offset(tmp, n_rem << shift);

	tb->insert_size[0] = n_rem;
	if (!n_rem)
		tb->pos_in_item++;
}

static void balance_leaf_new_nodes_paste_whole(struct tree_balance *tb,
					       struct item_head *ih,
					       const char *body,
					       struct item_head *insert_key,
					       struct buffer_head **insert_ptr,
					       int i)

{
	struct buffer_head *tbS0 = PATH_PLAST_BUFFER(tb->tb_path);
	int n = B_NR_ITEMS(tbS0);
	int leaf_mi;
	struct item_head *pasted;
	struct buffer_info bi;

#ifdef CONFIG_REISERFS_CHECK
	struct item_head *ih_check = item_head(tbS0, tb->item_pos);

	if (!is_direntry_le_ih(ih_check) &&
	    (tb->pos_in_item != ih_item_len(ih_check) ||
	    tb->insert_size[0] <= 0))
		reiserfs_panic(tb->tb_sb,
			     "PAP-12235",
			     "pos_in_item must be equal to ih_item_len");
#endif

	leaf_mi = leaf_move_items(LEAF_FROM_S_TO_SNEW, tb, tb->snum[i],
				  tb->sbytes[i], tb->S_new[i]);

	RFALSE(leaf_mi,
	       "PAP-12240: unexpected value returned by leaf_move_items (%d)",
	       leaf_mi);

	/* paste into item */
	buffer_info_init_bh(tb, &bi, tb->S_new[i]);
	leaf_paste_in_buffer(&bi, tb->item_pos - n + tb->snum[i],
			     tb->pos_in_item, tb->insert_size[0],
			     body, tb->zeroes_num);

	pasted = item_head(tb->S_new[i], tb->item_pos - n +
			   tb->snum[i]);
	if (is_direntry_le_ih(pasted))
		leaf_paste_entries(&bi, tb->item_pos - n + tb->snum[i],
				   tb->pos_in_item, 1,
				   (struct reiserfs_de_head *)body,
				   body + DEH_SIZE, tb->insert_size[0]);

	/* if we paste to indirect item update ih_free_space */
	if (is_indirect_le_ih(pasted))
		set_ih_free_space(pasted, 0);

	tb->zeroes_num = tb->insert_size[0] = 0;

}
static void balance_leaf_new_nodes_paste(struct tree_balance *tb,
					 struct item_head *ih,
					 const char *body,
					 struct item_head *insert_key,
					 struct buffer_head **insert_ptr,
					 int i)
{
	struct buffer_head *tbS0 = PATH_PLAST_BUFFER(tb->tb_path);
	int n = B_NR_ITEMS(tbS0);

	/* pasted item doesn't fall into S_new[i] */
	if (n - tb->snum[i] > tb->item_pos) {
		leaf_move_items(LEAF_FROM_S_TO_SNEW, tb,
				tb->snum[i], tb->sbytes[i], tb->S_new[i]);
		return;
	}

	/* pasted item or part if it falls to S_new[i] */

	if (tb->item_pos == n - tb->snum[i] && tb->sbytes[i] != -1)
		/* we must shift part of the appended item */
		balance_leaf_new_nodes_paste_shift(tb, ih, body, insert_key,
						   insert_ptr, i);
	else
		/* item falls wholly into S_new[i] */
		balance_leaf_new_nodes_paste_whole(tb, ih, body, insert_key,
						   insert_ptr, i);
}

/* Fill new nodes that appear in place of S[0] */
static void balance_leaf_new_nodes(struct tree_balance *tb,
				   struct item_head *ih,
				   const char *body,
				   struct item_head *insert_key,
				   struct buffer_head **insert_ptr,
				   int flag)
{
	int i;
	for (i = tb->blknum[0] - 2; i >= 0; i--) {
		BUG_ON(flag != M_INSERT && flag != M_PASTE);

		RFALSE(!tb->snum[i],
		       "PAP-12200: snum[%d] == %d. Must be > 0", i,
		       tb->snum[i]);

		/* here we shift from S to S_new nodes */

		tb->S_new[i] = get_FEB(tb);

		/* initialized block type and tree level */
		set_blkh_level(B_BLK_HEAD(tb->S_new[i]), DISK_LEAF_NODE_LEVEL);

		if (flag == M_INSERT)
			balance_leaf_new_nodes_insert(tb, ih, body, insert_key,
						      insert_ptr, i);
		else /* M_PASTE */
			balance_leaf_new_nodes_paste(tb, ih, body, insert_key,
						     insert_ptr, i);

		memcpy(insert_key + i, leaf_key(tb->S_new[i], 0), KEY_SIZE);
		insert_ptr[i] = tb->S_new[i];

		RFALSE(!buffer_journaled(tb->S_new[i])
		       || buffer_journal_dirty(tb->S_new[i])
		       || buffer_dirty(tb->S_new[i]),
		       "PAP-12247: S_new[%d] : (%b)",
		       i, tb->S_new[i]);
	}
}

static void balance_leaf_finish_node_insert(struct tree_balance *tb,
					    struct item_head *ih,
					    const char *body)
{
	struct buffer_head *tbS0 = PATH_PLAST_BUFFER(tb->tb_path);
	struct buffer_info bi;
	buffer_info_init_tbS0(tb, &bi);
	leaf_insert_into_buf(&bi, tb->item_pos, ih, body, tb->zeroes_num);

	/* If we insert the first key change the delimiting key */
	if (tb->item_pos == 0) {
		if (tb->CFL[0])	/* can be 0 in reiserfsck */
			replace_key(tb, tb->CFL[0], tb->lkey[0], tbS0, 0);

	}
}

static void balance_leaf_finish_node_paste_dirent(struct tree_balance *tb,
						  struct item_head *ih,
						  const char *body)
{
	struct buffer_head *tbS0 = PATH_PLAST_BUFFER(tb->tb_path);
	struct item_head *pasted = item_head(tbS0, tb->item_pos);
	struct buffer_info bi;

	if (tb->pos_in_item >= 0 && tb->pos_in_item <= ih_entry_count(pasted)) {
		RFALSE(!tb->insert_size[0],
		       "PAP-12260: insert_size is 0 already");

		/* prepare space */
		buffer_info_init_tbS0(tb, &bi);
		leaf_paste_in_buffer(&bi, tb->item_pos, tb->pos_in_item,
				     tb->insert_size[0], body, tb->zeroes_num);

		/* paste entry */
		leaf_paste_entries(&bi, tb->item_pos, tb->pos_in_item, 1,
				   (struct reiserfs_de_head *)body,
				   body + DEH_SIZE, tb->insert_size[0]);

		if (!tb->item_pos && !tb->pos_in_item) {
			RFALSE(!tb->CFL[0] || !tb->L[0],
			       "PAP-12270: CFL[0]/L[0] must  be specified");
			if (tb->CFL[0])
				replace_key(tb, tb->CFL[0], tb->lkey[0],
					    tbS0, 0);
		}

		tb->insert_size[0] = 0;
	}
}

static void balance_leaf_finish_node_paste(struct tree_balance *tb,
					   struct item_head *ih,
					   const char *body)
{
	struct buffer_head *tbS0 = PATH_PLAST_BUFFER(tb->tb_path);
	struct buffer_info bi;
	struct item_head *pasted = item_head(tbS0, tb->item_pos);

	/* when directory, may be new entry already pasted */
	if (is_direntry_le_ih(pasted)) {
		balance_leaf_finish_node_paste_dirent(tb, ih, body);
		return;
	}

	/* regular object */

	if (tb->pos_in_item == ih_item_len(pasted)) {
		RFALSE(tb->insert_size[0] <= 0,
		       "PAP-12275: insert size must not be %d",
		       tb->insert_size[0]);
		buffer_info_init_tbS0(tb, &bi);
		leaf_paste_in_buffer(&bi, tb->item_pos,
				     tb->pos_in_item, tb->insert_size[0], body,
				     tb->zeroes_num);

		if (is_indirect_le_ih(pasted))
			set_ih_free_space(pasted, 0);

		tb->insert_size[0] = 0;
	}
#ifdef CONFIG_REISERFS_CHECK
	else if (tb->insert_size[0]) {
		print_cur_tb("12285");
		reiserfs_panic(tb->tb_sb, "PAP-12285",
		    "insert_size must be 0 (%d)", tb->insert_size[0]);
	}
#endif
}

/*
 * if the affected item was not wholly shifted then we
 * perform all necessary operations on that part or whole
 * of the affected item which remains in S
 */
static void balance_leaf_finish_node(struct tree_balance *tb,
				      struct item_head *ih,
				      const char *body, int flag)
{
	/* if we must insert or append into buffer S[0] */
	if (0 <= tb->item_pos && tb->item_pos < tb->s0num) {
		if (flag == M_INSERT)
			balance_leaf_finish_node_insert(tb, ih, body);
		else /* M_PASTE */
			balance_leaf_finish_node_paste(tb, ih, body);
	}
}

/**
 * balance_leaf - reiserfs tree balancing algorithm
 * @tb: tree balance state
 * @ih: item header of inserted item (little endian)
 * @body: body of inserted item or bytes to paste
 * @flag: i - insert, d - delete, c - cut, p - paste (see do_balance)
 * passed back:
 * @insert_key: key to insert new nodes
 * @insert_ptr: array of nodes to insert at the next level
 *
 * In our processing of one level we sometimes determine what must be
 * inserted into the next higher level.  This insertion consists of a
 * key or two keys and their corresponding pointers.
 */
static int balance_leaf(struct tree_balance *tb, struct item_head *ih,
			const char *body, int flag,
			struct item_head *insert_key,
			struct buffer_head **insert_ptr)
{
	struct buffer_head *tbS0 = PATH_PLAST_BUFFER(tb->tb_path);

	PROC_INFO_INC(tb->tb_sb, balance_at[0]);

	/* Make balance in case insert_size[0] < 0 */
	if (tb->insert_size[0] < 0)
		return balance_leaf_when_delete(tb, flag);

	tb->item_pos = PATH_LAST_POSITION(tb->tb_path),
	tb->pos_in_item = tb->tb_path->pos_in_item,
	tb->zeroes_num = 0;
	if (flag == M_INSERT && !body)
		tb->zeroes_num = ih_item_len(ih);

	/*
	 * for indirect item pos_in_item is measured in unformatted node
	 * pointers. Recalculate to bytes
	 */
	if (flag != M_INSERT
	    && is_indirect_le_ih(item_head(tbS0, tb->item_pos)))
		tb->pos_in_item *= UNFM_P_SIZE;

	balance_leaf_left(tb, ih, body, flag);

	/* tb->lnum[0] > 0 */
	/* Calculate new item position */
	tb->item_pos -= (tb->lnum[0] - ((tb->lbytes != -1) ? 1 : 0));

	balance_leaf_right(tb, ih, body, flag);

	/* tb->rnum[0] > 0 */
	RFALSE(tb->blknum[0] > 3,
	       "PAP-12180: blknum can not be %d. It must be <= 3", tb->blknum[0]);
	RFALSE(tb->blknum[0] < 0,
	       "PAP-12185: blknum can not be %d. It must be >= 0", tb->blknum[0]);

	/*
	 * if while adding to a node we discover that it is possible to split
	 * it in two, and merge the left part into the left neighbor and the
	 * right part into the right neighbor, eliminating the node
	 */
	if (tb->blknum[0] == 0) {	/* node S[0] is empty now */

		RFALSE(!tb->lnum[0] || !tb->rnum[0],
		       "PAP-12190: lnum and rnum must not be zero");
		/*
		 * if insertion was done before 0-th position in R[0], right
		 * delimiting key of the tb->L[0]'s and left delimiting key are
		 * not set correctly
		 */
		if (tb->CFL[0]) {
			if (!tb->CFR[0])
				reiserfs_panic(tb->tb_sb, "vs-12195",
					       "CFR not initialized");
			copy_key(internal_key(tb->CFL[0], tb->lkey[0]),
				 internal_key(tb->CFR[0], tb->rkey[0]));
			do_balance_mark_internal_dirty(tb, tb->CFL[0], 0);
		}

		reiserfs_invalidate_buffer(tb, tbS0);
		return 0;
	}

	balance_leaf_new_nodes(tb, ih, body, insert_key, insert_ptr, flag);

	balance_leaf_finish_node(tb, ih, body, flag);

#ifdef CONFIG_REISERFS_CHECK
	if (flag == M_PASTE && tb->insert_size[0]) {
		print_cur_tb("12290");
		reiserfs_panic(tb->tb_sb,
			       "PAP-12290", "insert_size is still not 0 (%d)",
			       tb->insert_size[0]);
	}
#endif

	/* Leaf level of the tree is balanced (end of balance_leaf) */
	return 0;
}

/* Make empty node */
void make_empty_node(struct buffer_info *bi)
{
	struct block_head *blkh;

	RFALSE(bi->bi_bh == NULL, "PAP-12295: pointer to the buffer is NULL");

	blkh = B_BLK_HEAD(bi->bi_bh);
	set_blkh_nr_item(blkh, 0);
	set_blkh_free_space(blkh, MAX_CHILD_SIZE(bi->bi_bh));

	if (bi->bi_parent)
		B_N_CHILD(bi->bi_parent, bi->bi_position)->dc_size = 0;	/* Endian safe if 0 */
}

/* Get first empty buffer */
struct buffer_head *get_FEB(struct tree_balance *tb)
{
	int i;
	struct buffer_info bi;

	for (i = 0; i < MAX_FEB_SIZE; i++)
		if (tb->FEB[i] != NULL)
			break;

	if (i == MAX_FEB_SIZE)
		reiserfs_panic(tb->tb_sb, "vs-12300", "FEB list is empty");

	buffer_info_init_bh(tb, &bi, tb->FEB[i]);
	make_empty_node(&bi);
	set_buffer_uptodate(tb->FEB[i]);
	tb->used[i] = tb->FEB[i];
	tb->FEB[i] = NULL;

	return tb->used[i];
}

/* This is now used because reiserfs_free_block has to be able to schedule. */
static void store_thrown(struct tree_balance *tb, struct buffer_head *bh)
{
	int i;

	if (buffer_dirty(bh))
		reiserfs_warning(tb->tb_sb, "reiserfs-12320",
				 "called with dirty buffer");
	for (i = 0; i < ARRAY_SIZE(tb->thrown); i++)
		if (!tb->thrown[i]) {
			tb->thrown[i] = bh;
			get_bh(bh);	/* free_thrown puts this */
			return;
		}
	reiserfs_warning(tb->tb_sb, "reiserfs-12321",
			 "too many thrown buffers");
}

static void free_thrown(struct tree_balance *tb)
{
	int i;
	b_blocknr_t blocknr;
	for (i = 0; i < ARRAY_SIZE(tb->thrown); i++) {
		if (tb->thrown[i]) {
			blocknr = tb->thrown[i]->b_blocknr;
			if (buffer_dirty(tb->thrown[i]))
				reiserfs_warning(tb->tb_sb, "reiserfs-12322",
						 "called with dirty buffer %d",
						 blocknr);
			brelse(tb->thrown[i]);	/* incremented in store_thrown */
			reiserfs_free_block(tb->transaction_handle, NULL,
					    blocknr, 0);
		}
	}
}

void reiserfs_invalidate_buffer(struct tree_balance *tb, struct buffer_head *bh)
{
	struct block_head *blkh;
	blkh = B_BLK_HEAD(bh);
	set_blkh_level(blkh, FREE_LEVEL);
	set_blkh_nr_item(blkh, 0);

	clear_buffer_dirty(bh);
	store_thrown(tb, bh);
}

/* Replace n_dest'th key in buffer dest by n_src'th key of buffer src.*/
void replace_key(struct tree_balance *tb, struct buffer_head *dest, int n_dest,
		 struct buffer_head *src, int n_src)
{

	RFALSE(dest == NULL || src == NULL,
	       "vs-12305: source or destination buffer is 0 (src=%p, dest=%p)",
	       src, dest);
	RFALSE(!B_IS_KEYS_LEVEL(dest),
	       "vs-12310: invalid level (%z) for destination buffer. dest must be leaf",
	       dest);
	RFALSE(n_dest < 0 || n_src < 0,
	       "vs-12315: src(%d) or dest(%d) key number < 0", n_src, n_dest);
	RFALSE(n_dest >= B_NR_ITEMS(dest) || n_src >= B_NR_ITEMS(src),
	       "vs-12320: src(%d(%d)) or dest(%d(%d)) key number is too big",
	       n_src, B_NR_ITEMS(src), n_dest, B_NR_ITEMS(dest));

	if (B_IS_ITEMS_LEVEL(src))
		/* source buffer contains leaf node */
		memcpy(internal_key(dest, n_dest), item_head(src, n_src),
		       KEY_SIZE);
	else
		memcpy(internal_key(dest, n_dest), internal_key(src, n_src),
		       KEY_SIZE);

	do_balance_mark_internal_dirty(tb, dest, 0);
}

int get_left_neighbor_position(struct tree_balance *tb, int h)
{
	int Sh_position = PATH_H_POSITION(tb->tb_path, h + 1);

	RFALSE(PATH_H_PPARENT(tb->tb_path, h) == NULL || tb->FL[h] == NULL,
	       "vs-12325: FL[%d](%p) or F[%d](%p) does not exist",
	       h, tb->FL[h], h, PATH_H_PPARENT(tb->tb_path, h));

	if (Sh_position == 0)
		return B_NR_ITEMS(tb->FL[h]);
	else
		return Sh_position - 1;
}

int get_right_neighbor_position(struct tree_balance *tb, int h)
{
	int Sh_position = PATH_H_POSITION(tb->tb_path, h + 1);

	RFALSE(PATH_H_PPARENT(tb->tb_path, h) == NULL || tb->FR[h] == NULL,
	       "vs-12330: F[%d](%p) or FR[%d](%p) does not exist",
	       h, PATH_H_PPARENT(tb->tb_path, h), h, tb->FR[h]);

	if (Sh_position == B_NR_ITEMS(PATH_H_PPARENT(tb->tb_path, h)))
		return 0;
	else
		return Sh_position + 1;
}

#ifdef CONFIG_REISERFS_CHECK

int is_reusable(struct super_block *s, b_blocknr_t block, int bit_value);
static void check_internal_node(struct super_block *s, struct buffer_head *bh,
				char *mes)
{
	struct disk_child *dc;
	int i;

	RFALSE(!bh, "PAP-12336: bh == 0");

	if (!bh || !B_IS_IN_TREE(bh))
		return;

	RFALSE(!buffer_dirty(bh) &&
	       !(buffer_journaled(bh) || buffer_journal_dirty(bh)),
	       "PAP-12337: buffer (%b) must be dirty", bh);
	dc = B_N_CHILD(bh, 0);

	for (i = 0; i <= B_NR_ITEMS(bh); i++, dc++) {
		if (!is_reusable(s, dc_block_number(dc), 1)) {
			print_cur_tb(mes);
			reiserfs_panic(s, "PAP-12338",
				       "invalid child pointer %y in %b",
				       dc, bh);
		}
	}
}

static int locked_or_not_in_tree(struct tree_balance *tb,
				  struct buffer_head *bh, char *which)
{
	if ((!buffer_journal_prepared(bh) && buffer_locked(bh)) ||
	    !B_IS_IN_TREE(bh)) {
		reiserfs_warning(tb->tb_sb, "vs-12339", "%s (%b)", which, bh);
		return 1;
	}
	return 0;
}

static int check_before_balancing(struct tree_balance *tb)
{
	int retval = 0;

	if (REISERFS_SB(tb->tb_sb)->cur_tb) {
		reiserfs_panic(tb->tb_sb, "vs-12335", "suspect that schedule "
			       "occurred based on cur_tb not being null at "
			       "this point in code. do_balance cannot properly "
			       "handle concurrent tree accesses on a same "
			       "mount point.");
	}

	/*
	 * double check that buffers that we will modify are unlocked.
	 * (fix_nodes should already have prepped all of these for us).
	 */
	if (tb->lnum[0]) {
		retval |= locked_or_not_in_tree(tb, tb->L[0], "L[0]");
		retval |= locked_or_not_in_tree(tb, tb->FL[0], "FL[0]");
		retval |= locked_or_not_in_tree(tb, tb->CFL[0], "CFL[0]");
		check_leaf(tb->L[0]);
	}
	if (tb->rnum[0]) {
		retval |= locked_or_not_in_tree(tb, tb->R[0], "R[0]");
		retval |= locked_or_not_in_tree(tb, tb->FR[0], "FR[0]");
		retval |= locked_or_not_in_tree(tb, tb->CFR[0], "CFR[0]");
		check_leaf(tb->R[0]);
	}
	retval |= locked_or_not_in_tree(tb, PATH_PLAST_BUFFER(tb->tb_path),
					"S[0]");
	check_leaf(PATH_PLAST_BUFFER(tb->tb_path));

	return retval;
}

static void check_after_balance_leaf(struct tree_balance *tb)
{
	if (tb->lnum[0]) {
		if (B_FREE_SPACE(tb->L[0]) !=
		    MAX_CHILD_SIZE(tb->L[0]) -
		    dc_size(B_N_CHILD
			    (tb->FL[0], get_left_neighbor_position(tb, 0)))) {
			print_cur_tb("12221");
			reiserfs_panic(tb->tb_sb, "PAP-12355",
				       "shift to left was incorrect");
		}
	}
	if (tb->rnum[0]) {
		if (B_FREE_SPACE(tb->R[0]) !=
		    MAX_CHILD_SIZE(tb->R[0]) -
		    dc_size(B_N_CHILD
			    (tb->FR[0], get_right_neighbor_position(tb, 0)))) {
			print_cur_tb("12222");
			reiserfs_panic(tb->tb_sb, "PAP-12360",
				       "shift to right was incorrect");
		}
	}
	if (PATH_H_PBUFFER(tb->tb_path, 1) &&
	    (B_FREE_SPACE(PATH_H_PBUFFER(tb->tb_path, 0)) !=
	     (MAX_CHILD_SIZE(PATH_H_PBUFFER(tb->tb_path, 0)) -
	      dc_size(B_N_CHILD(PATH_H_PBUFFER(tb->tb_path, 1),
				PATH_H_POSITION(tb->tb_path, 1)))))) {
		int left = B_FREE_SPACE(PATH_H_PBUFFER(tb->tb_path, 0));
		int right = (MAX_CHILD_SIZE(PATH_H_PBUFFER(tb->tb_path, 0)) -
			     dc_size(B_N_CHILD(PATH_H_PBUFFER(tb->tb_path, 1),
					       PATH_H_POSITION(tb->tb_path,
							       1))));
		print_cur_tb("12223");
		reiserfs_warning(tb->tb_sb, "reiserfs-12363",
				 "B_FREE_SPACE (PATH_H_PBUFFER(tb->tb_path,0)) = %d; "
				 "MAX_CHILD_SIZE (%d) - dc_size( %y, %d ) [%d] = %d",
				 left,
				 MAX_CHILD_SIZE(PATH_H_PBUFFER(tb->tb_path, 0)),
				 PATH_H_PBUFFER(tb->tb_path, 1),
				 PATH_H_POSITION(tb->tb_path, 1),
				 dc_size(B_N_CHILD
					 (PATH_H_PBUFFER(tb->tb_path, 1),
					  PATH_H_POSITION(tb->tb_path, 1))),
				 right);
		reiserfs_panic(tb->tb_sb, "PAP-12365", "S is incorrect");
	}
}

static void check_leaf_level(struct tree_balance *tb)
{
	check_leaf(tb->L[0]);
	check_leaf(tb->R[0]);
	check_leaf(PATH_PLAST_BUFFER(tb->tb_path));
}

static void check_internal_levels(struct tree_balance *tb)
{
	int h;

	/* check all internal nodes */
	for (h = 1; tb->insert_size[h]; h++) {
		check_internal_node(tb->tb_sb, PATH_H_PBUFFER(tb->tb_path, h),
				    "BAD BUFFER ON PATH");
		if (tb->lnum[h])
			check_internal_node(tb->tb_sb, tb->L[h], "BAD L");
		if (tb->rnum[h])
			check_internal_node(tb->tb_sb, tb->R[h], "BAD R");
	}

}

#endif

/*
 * Now we have all of the buffers that must be used in balancing of
 * the tree.  We rely on the assumption that schedule() will not occur
 * while do_balance works. ( Only interrupt handlers are acceptable.)
 * We balance the tree according to the analysis made before this,
 * using buffers already obtained.  For SMP support it will someday be
 * necessary to add ordered locking of tb.
 */

/*
 * Some interesting rules of balancing:
 * we delete a maximum of two nodes per level per balancing: we never
 * delete R, when we delete two of three nodes L, S, R then we move
 * them into R.
 *
 * we only delete L if we are deleting two nodes, if we delete only
 * one node we delete S
 *
 * if we shift leaves then we shift as much as we can: this is a
 * deliberate policy of extremism in node packing which results in
 * higher average utilization after repeated random balance operations
 * at the cost of more memory copies and more balancing as a result of
 * small insertions to full nodes.
 *
 * if we shift internal nodes we try to evenly balance the node
 * utilization, with consequent less balancing at the cost of lower
 * utilization.
 *
 * one could argue that the policy for directories in leaves should be
 * that of internal nodes, but we will wait until another day to
 * evaluate this....  It would be nice to someday measure and prove
 * these assumptions as to what is optimal....
 */

static inline void do_balance_starts(struct tree_balance *tb)
{
	/* use print_cur_tb() to see initial state of struct tree_balance */

	/* store_print_tb (tb); */

	/* do not delete, just comment it out */
	/*
	print_tb(flag, PATH_LAST_POSITION(tb->tb_path),
		 tb->tb_path->pos_in_item, tb, "check");
	*/
	RFALSE(check_before_balancing(tb), "PAP-12340: locked buffers in TB");
#ifdef CONFIG_REISERFS_CHECK
	REISERFS_SB(tb->tb_sb)->cur_tb = tb;
#endif
}

static inline void do_balance_completed(struct tree_balance *tb)
{

#ifdef CONFIG_REISERFS_CHECK
	check_leaf_level(tb);
	check_internal_levels(tb);
	REISERFS_SB(tb->tb_sb)->cur_tb = NULL;
#endif

	/*
	 * reiserfs_free_block is no longer schedule safe.  So, we need to
	 * put the buffers we want freed on the thrown list during do_balance,
	 * and then free them now
	 */

	REISERFS_SB(tb->tb_sb)->s_do_balance++;

	/* release all nodes hold to perform the balancing */
	unfix_nodes(tb);

	free_thrown(tb);
}

/*
 * do_balance - balance the tree
 *
 * @tb: tree_balance structure
 * @ih: item header of inserted item
 * @body: body of inserted item or bytes to paste
 * @flag: 'i' - insert, 'd' - delete, 'c' - cut, 'p' paste
 *
 * Cut means delete part of an item (includes removing an entry from a
 * directory).
 *
 * Delete means delete whole item.
 *
 * Insert means add a new item into the tree.
 *
 * Paste means to append to the end of an existing file or to
 * insert a directory entry.
 */
void do_balance(struct tree_balance *tb, struct item_head *ih,
		const char *body, int flag)
{
	int child_pos;		/* position of a child node in its parent */
	int h;			/* level of the tree being processed */

	/*
	 * in our processing of one level we sometimes determine what
	 * must be inserted into the next higher level.  This insertion
	 * consists of a key or two keys and their corresponding
	 * pointers
	 */
	struct item_head insert_key[2];

	/* inserted node-ptrs for the next level */
	struct buffer_head *insert_ptr[2];

	tb->tb_mode = flag;
	tb->need_balance_dirty = 0;

	if (FILESYSTEM_CHANGED_TB(tb)) {
		reiserfs_panic(tb->tb_sb, "clm-6000", "fs generation has "
			       "changed");
	}
	/* if we have no real work to do  */
	if (!tb->insert_size[0]) {
		reiserfs_warning(tb->tb_sb, "PAP-12350",
				 "insert_size == 0, mode == %c", flag);
		unfix_nodes(tb);
		return;
	}

	atomic_inc(&fs_generation(tb->tb_sb));
	do_balance_starts(tb);

	/*
	 * balance_leaf returns 0 except if combining L R and S into
	 * one node.  see balance_internal() for explanation of this
	 * line of code.
	 */
	child_pos = PATH_H_B_ITEM_ORDER(tb->tb_path, 0) +
	    balance_leaf(tb, ih, body, flag, insert_key, insert_ptr);

#ifdef CONFIG_REISERFS_CHECK
	check_after_balance_leaf(tb);
#endif

	/* Balance internal level of the tree. */
	for (h = 1; h < MAX_HEIGHT && tb->insert_size[h]; h++)
		child_pos = balance_internal(tb, h, child_pos, insert_key,
					     insert_ptr);

	do_balance_completed(tb);
}
