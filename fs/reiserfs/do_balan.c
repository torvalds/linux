/*
 * Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README
 */

/* Now we have all buffers that must be used in balancing of the tree 	*/
/* Further calculations can not cause schedule(), and thus the buffer 	*/
/* tree will be stable until the balancing will be finished 		*/
/* balance the tree according to the analysis made before,		*/
/* and using buffers obtained after all above.				*/

/**
 ** balance_leaf_when_delete
 ** balance_leaf
 ** do_balance
 **
 **/

#include <linux/config.h>
#include <asm/uaccess.h>
#include <linux/time.h>
#include <linux/reiserfs_fs.h>
#include <linux/buffer_head.h>

#ifdef CONFIG_REISERFS_CHECK

struct tree_balance *cur_tb = NULL;	/* detects whether more than one
					   copy of tb exists as a means
					   of checking whether schedule
					   is interrupting do_balance */
#endif

inline void do_balance_mark_leaf_dirty(struct tree_balance *tb,
				       struct buffer_head *bh, int flag)
{
	journal_mark_dirty(tb->transaction_handle,
			   tb->transaction_handle->t_super, bh);
}

#define do_balance_mark_internal_dirty do_balance_mark_leaf_dirty
#define do_balance_mark_sb_dirty do_balance_mark_leaf_dirty

/* summary: 
 if deleting something ( tb->insert_size[0] < 0 )
   return(balance_leaf_when_delete()); (flag d handled here)
 else
   if lnum is larger than 0 we put items into the left node
   if rnum is larger than 0 we put items into the right node
   if snum1 is larger than 0 we put items into the new node s1
   if snum2 is larger than 0 we put items into the new node s2 
Note that all *num* count new items being created.

It would be easier to read balance_leaf() if each of these summary
lines was a separate procedure rather than being inlined.  I think
that there are many passages here and in balance_leaf_when_delete() in
which two calls to one procedure can replace two passages, and it
might save cache space and improve software maintenance costs to do so.  

Vladimir made the perceptive comment that we should offload most of
the decision making in this function into fix_nodes/check_balance, and
then create some sort of structure in tb that says what actions should
be performed by do_balance.

-Hans */

/* Balance leaf node in case of delete or cut: insert_size[0] < 0
 *
 * lnum, rnum can have values >= -1
 *	-1 means that the neighbor must be joined with S
 *	 0 means that nothing should be done with the neighbor
 *	>0 means to shift entirely or partly the specified number of items to the neighbor
 */
static int balance_leaf_when_delete(struct tree_balance *tb, int flag)
{
	struct buffer_head *tbS0 = PATH_PLAST_BUFFER(tb->tb_path);
	int item_pos = PATH_LAST_POSITION(tb->tb_path);
	int pos_in_item = tb->tb_path->pos_in_item;
	struct buffer_info bi;
	int n;
	struct item_head *ih;

	RFALSE(tb->FR[0] && B_LEVEL(tb->FR[0]) != DISK_LEAF_NODE_LEVEL + 1,
	       "vs- 12000: level: wrong FR %z", tb->FR[0]);
	RFALSE(tb->blknum[0] > 1,
	       "PAP-12005: tb->blknum == %d, can not be > 1", tb->blknum[0]);
	RFALSE(!tb->blknum[0] && !PATH_H_PPARENT(tb->tb_path, 0),
	       "PAP-12010: tree can not be empty");

	ih = B_N_PITEM_HEAD(tbS0, item_pos);

	/* Delete or truncate the item */

	switch (flag) {
	case M_DELETE:		/* delete item in S[0] */

		RFALSE(ih_item_len(ih) + IH_SIZE != -tb->insert_size[0],
		       "vs-12013: mode Delete, insert size %d, ih to be deleted %h",
		       -tb->insert_size[0], ih);

		bi.tb = tb;
		bi.bi_bh = tbS0;
		bi.bi_parent = PATH_H_PPARENT(tb->tb_path, 0);
		bi.bi_position = PATH_H_POSITION(tb->tb_path, 1);
		leaf_delete_items(&bi, 0, item_pos, 1, -1);

		if (!item_pos && tb->CFL[0]) {
			if (B_NR_ITEMS(tbS0)) {
				replace_key(tb, tb->CFL[0], tb->lkey[0], tbS0,
					    0);
			} else {
				if (!PATH_H_POSITION(tb->tb_path, 1))
					replace_key(tb, tb->CFL[0], tb->lkey[0],
						    PATH_H_PPARENT(tb->tb_path,
								   0), 0);
			}
		}

		RFALSE(!item_pos && !tb->CFL[0],
		       "PAP-12020: tb->CFL[0]==%p, tb->L[0]==%p", tb->CFL[0],
		       tb->L[0]);

		break;

	case M_CUT:{		/* cut item in S[0] */
			bi.tb = tb;
			bi.bi_bh = tbS0;
			bi.bi_parent = PATH_H_PPARENT(tb->tb_path, 0);
			bi.bi_position = PATH_H_POSITION(tb->tb_path, 1);
			if (is_direntry_le_ih(ih)) {

				/* UFS unlink semantics are such that you can only delete one directory entry at a time. */
				/* when we cut a directory tb->insert_size[0] means number of entries to be cut (always 1) */
				tb->insert_size[0] = -1;
				leaf_cut_from_buffer(&bi, item_pos, pos_in_item,
						     -tb->insert_size[0]);

				RFALSE(!item_pos && !pos_in_item && !tb->CFL[0],
				       "PAP-12030: can not change delimiting key. CFL[0]=%p",
				       tb->CFL[0]);

				if (!item_pos && !pos_in_item && tb->CFL[0]) {
					replace_key(tb, tb->CFL[0], tb->lkey[0],
						    tbS0, 0);
				}
			} else {
				leaf_cut_from_buffer(&bi, item_pos, pos_in_item,
						     -tb->insert_size[0]);

				RFALSE(!ih_item_len(ih),
				       "PAP-12035: cut must leave non-zero dynamic length of item");
			}
			break;
		}

	default:
		print_cur_tb("12040");
		reiserfs_panic(tb->tb_sb,
			       "PAP-12040: balance_leaf_when_delete: unexpectable mode: %s(%d)",
			       (flag ==
				M_PASTE) ? "PASTE" : ((flag ==
						       M_INSERT) ? "INSERT" :
						      "UNKNOWN"), flag);
	}

	/* the rule is that no shifting occurs unless by shifting a node can be freed */
	n = B_NR_ITEMS(tbS0);
	if (tb->lnum[0]) {	/* L[0] takes part in balancing */
		if (tb->lnum[0] == -1) {	/* L[0] must be joined with S[0] */
			if (tb->rnum[0] == -1) {	/* R[0] must be also joined with S[0] */
				if (tb->FR[0] == PATH_H_PPARENT(tb->tb_path, 0)) {
					/* all contents of all the 3 buffers will be in L[0] */
					if (PATH_H_POSITION(tb->tb_path, 1) == 0
					    && 1 < B_NR_ITEMS(tb->FR[0]))
						replace_key(tb, tb->CFL[0],
							    tb->lkey[0],
							    tb->FR[0], 1);

					leaf_move_items(LEAF_FROM_S_TO_L, tb, n,
							-1, NULL);
					leaf_move_items(LEAF_FROM_R_TO_L, tb,
							B_NR_ITEMS(tb->R[0]),
							-1, NULL);

					reiserfs_invalidate_buffer(tb, tbS0);
					reiserfs_invalidate_buffer(tb,
								   tb->R[0]);

					return 0;
				}
				/* all contents of all the 3 buffers will be in R[0] */
				leaf_move_items(LEAF_FROM_S_TO_R, tb, n, -1,
						NULL);
				leaf_move_items(LEAF_FROM_L_TO_R, tb,
						B_NR_ITEMS(tb->L[0]), -1, NULL);

				/* right_delimiting_key is correct in R[0] */
				replace_key(tb, tb->CFR[0], tb->rkey[0],
					    tb->R[0], 0);

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
		/* a part of contents of S[0] will be in L[0] and the rest part of S[0] will be in R[0] */

		RFALSE((tb->lnum[0] + tb->rnum[0] < n) ||
		       (tb->lnum[0] + tb->rnum[0] > n + 1),
		       "PAP-12050: rnum(%d) and lnum(%d) and item number(%d) in S[0] are not consistent",
		       tb->rnum[0], tb->lnum[0], n);
		RFALSE((tb->lnum[0] + tb->rnum[0] == n) &&
		       (tb->lbytes != -1 || tb->rbytes != -1),
		       "PAP-12055: bad rbytes (%d)/lbytes (%d) parameters when items are not split",
		       tb->rbytes, tb->lbytes);
		RFALSE((tb->lnum[0] + tb->rnum[0] == n + 1) &&
		       (tb->lbytes < 1 || tb->rbytes != -1),
		       "PAP-12060: bad rbytes (%d)/lbytes (%d) parameters when items are split",
		       tb->rbytes, tb->lbytes);

		leaf_shift_left(tb, tb->lnum[0], tb->lbytes);
		leaf_shift_right(tb, tb->rnum[0], tb->rbytes);

		reiserfs_invalidate_buffer(tb, tbS0);

		return 0;
	}

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

static int balance_leaf(struct tree_balance *tb, struct item_head *ih,	/* item header of inserted item (this is on little endian) */
			const char *body,	/* body  of inserted item or bytes to paste */
			int flag,	/* i - insert, d - delete, c - cut, p - paste
					   (see comment to do_balance) */
			struct item_head *insert_key,	/* in our processing of one level we sometimes determine what
							   must be inserted into the next higher level.  This insertion
							   consists of a key or two keys and their corresponding
							   pointers */
			struct buffer_head **insert_ptr	/* inserted node-ptrs for the next level */
    )
{
	struct buffer_head *tbS0 = PATH_PLAST_BUFFER(tb->tb_path);
	int item_pos = PATH_LAST_POSITION(tb->tb_path);	/*  index into the array of item headers in S[0] 
							   of the affected item */
	struct buffer_info bi;
	struct buffer_head *S_new[2];	/* new nodes allocated to hold what could not fit into S */
	int snum[2];		/* number of items that will be placed
				   into S_new (includes partially shifted
				   items) */
	int sbytes[2];		/* if an item is partially shifted into S_new then 
				   if it is a directory item 
				   it is the number of entries from the item that are shifted into S_new
				   else
				   it is the number of bytes from the item that are shifted into S_new
				 */
	int n, i;
	int ret_val;
	int pos_in_item;
	int zeros_num;

	PROC_INFO_INC(tb->tb_sb, balance_at[0]);

	/* Make balance in case insert_size[0] < 0 */
	if (tb->insert_size[0] < 0)
		return balance_leaf_when_delete(tb, flag);

	zeros_num = 0;
	if (flag == M_INSERT && body == 0)
		zeros_num = ih_item_len(ih);

	pos_in_item = tb->tb_path->pos_in_item;
	/* for indirect item pos_in_item is measured in unformatted node
	   pointers. Recalculate to bytes */
	if (flag != M_INSERT
	    && is_indirect_le_ih(B_N_PITEM_HEAD(tbS0, item_pos)))
		pos_in_item *= UNFM_P_SIZE;

	if (tb->lnum[0] > 0) {
		/* Shift lnum[0] items from S[0] to the left neighbor L[0] */
		if (item_pos < tb->lnum[0]) {
			/* new item or it part falls to L[0], shift it too */
			n = B_NR_ITEMS(tb->L[0]);

			switch (flag) {
			case M_INSERT:	/* insert item into L[0] */

				if (item_pos == tb->lnum[0] - 1
				    && tb->lbytes != -1) {
					/* part of new item falls into L[0] */
					int new_item_len;
					int version;

					ret_val =
					    leaf_shift_left(tb, tb->lnum[0] - 1,
							    -1);

					/* Calculate item length to insert to S[0] */
					new_item_len =
					    ih_item_len(ih) - tb->lbytes;
					/* Calculate and check item length to insert to L[0] */
					put_ih_item_len(ih,
							ih_item_len(ih) -
							new_item_len);

					RFALSE(ih_item_len(ih) <= 0,
					       "PAP-12080: there is nothing to insert into L[0]: ih_item_len=%d",
					       ih_item_len(ih));

					/* Insert new item into L[0] */
					bi.tb = tb;
					bi.bi_bh = tb->L[0];
					bi.bi_parent = tb->FL[0];
					bi.bi_position =
					    get_left_neighbor_position(tb, 0);
					leaf_insert_into_buf(&bi,
							     n + item_pos -
							     ret_val, ih, body,
							     zeros_num >
							     ih_item_len(ih) ?
							     ih_item_len(ih) :
							     zeros_num);

					version = ih_version(ih);

					/* Calculate key component, item length and body to insert into S[0] */
					set_le_ih_k_offset(ih,
							   le_ih_k_offset(ih) +
							   (tb->
							    lbytes <<
							    (is_indirect_le_ih
							     (ih) ? tb->tb_sb->
							     s_blocksize_bits -
							     UNFM_P_SHIFT :
							     0)));

					put_ih_item_len(ih, new_item_len);
					if (tb->lbytes > zeros_num) {
						body +=
						    (tb->lbytes - zeros_num);
						zeros_num = 0;
					} else
						zeros_num -= tb->lbytes;

					RFALSE(ih_item_len(ih) <= 0,
					       "PAP-12085: there is nothing to insert into S[0]: ih_item_len=%d",
					       ih_item_len(ih));
				} else {
					/* new item in whole falls into L[0] */
					/* Shift lnum[0]-1 items to L[0] */
					ret_val =
					    leaf_shift_left(tb, tb->lnum[0] - 1,
							    tb->lbytes);
					/* Insert new item into L[0] */
					bi.tb = tb;
					bi.bi_bh = tb->L[0];
					bi.bi_parent = tb->FL[0];
					bi.bi_position =
					    get_left_neighbor_position(tb, 0);
					leaf_insert_into_buf(&bi,
							     n + item_pos -
							     ret_val, ih, body,
							     zeros_num);
					tb->insert_size[0] = 0;
					zeros_num = 0;
				}
				break;

			case M_PASTE:	/* append item in L[0] */

				if (item_pos == tb->lnum[0] - 1
				    && tb->lbytes != -1) {
					/* we must shift the part of the appended item */
					if (is_direntry_le_ih
					    (B_N_PITEM_HEAD(tbS0, item_pos))) {

						RFALSE(zeros_num,
						       "PAP-12090: invalid parameter in case of a directory");
						/* directory item */
						if (tb->lbytes > pos_in_item) {
							/* new directory entry falls into L[0] */
							struct item_head
							    *pasted;
							int l_pos_in_item =
							    pos_in_item;

							/* Shift lnum[0] - 1 items in whole. Shift lbytes - 1 entries from given directory item */
							ret_val =
							    leaf_shift_left(tb,
									    tb->
									    lnum
									    [0],
									    tb->
									    lbytes
									    -
									    1);
							if (ret_val
							    && !item_pos) {
								pasted =
								    B_N_PITEM_HEAD
								    (tb->L[0],
								     B_NR_ITEMS
								     (tb->
								      L[0]) -
								     1);
								l_pos_in_item +=
								    I_ENTRY_COUNT
								    (pasted) -
								    (tb->
								     lbytes -
								     1);
							}

							/* Append given directory entry to directory item */
							bi.tb = tb;
							bi.bi_bh = tb->L[0];
							bi.bi_parent =
							    tb->FL[0];
							bi.bi_position =
							    get_left_neighbor_position
							    (tb, 0);
							leaf_paste_in_buffer
							    (&bi,
							     n + item_pos -
							     ret_val,
							     l_pos_in_item,
							     tb->insert_size[0],
							     body, zeros_num);

							/* previous string prepared space for pasting new entry, following string pastes this entry */

							/* when we have merge directory item, pos_in_item has been changed too */

							/* paste new directory entry. 1 is entry number */
							leaf_paste_entries(bi.
									   bi_bh,
									   n +
									   item_pos
									   -
									   ret_val,
									   l_pos_in_item,
									   1,
									   (struct
									    reiserfs_de_head
									    *)
									   body,
									   body
									   +
									   DEH_SIZE,
									   tb->
									   insert_size
									   [0]
							    );
							tb->insert_size[0] = 0;
						} else {
							/* new directory item doesn't fall into L[0] */
							/* Shift lnum[0]-1 items in whole. Shift lbytes directory entries from directory item number lnum[0] */
							leaf_shift_left(tb,
									tb->
									lnum[0],
									tb->
									lbytes);
						}
						/* Calculate new position to append in item body */
						pos_in_item -= tb->lbytes;
					} else {
						/* regular object */
						RFALSE(tb->lbytes <= 0,
						       "PAP-12095: there is nothing to shift to L[0]. lbytes=%d",
						       tb->lbytes);
						RFALSE(pos_in_item !=
						       ih_item_len
						       (B_N_PITEM_HEAD
							(tbS0, item_pos)),
						       "PAP-12100: incorrect position to paste: item_len=%d, pos_in_item=%d",
						       ih_item_len
						       (B_N_PITEM_HEAD
							(tbS0, item_pos)),
						       pos_in_item);

						if (tb->lbytes >= pos_in_item) {
							/* appended item will be in L[0] in whole */
							int l_n;

							/* this bytes number must be appended to the last item of L[h] */
							l_n =
							    tb->lbytes -
							    pos_in_item;

							/* Calculate new insert_size[0] */
							tb->insert_size[0] -=
							    l_n;

							RFALSE(tb->
							       insert_size[0] <=
							       0,
							       "PAP-12105: there is nothing to paste into L[0]. insert_size=%d",
							       tb->
							       insert_size[0]);
							ret_val =
							    leaf_shift_left(tb,
									    tb->
									    lnum
									    [0],
									    ih_item_len
									    (B_N_PITEM_HEAD
									     (tbS0,
									      item_pos)));
							/* Append to body of item in L[0] */
							bi.tb = tb;
							bi.bi_bh = tb->L[0];
							bi.bi_parent =
							    tb->FL[0];
							bi.bi_position =
							    get_left_neighbor_position
							    (tb, 0);
							leaf_paste_in_buffer
							    (&bi,
							     n + item_pos -
							     ret_val,
							     ih_item_len
							     (B_N_PITEM_HEAD
							      (tb->L[0],
							       n + item_pos -
							       ret_val)), l_n,
							     body,
							     zeros_num >
							     l_n ? l_n :
							     zeros_num);
							/* 0-th item in S0 can be only of DIRECT type when l_n != 0 */
							{
								int version;
								int temp_l =
								    l_n;

								RFALSE
								    (ih_item_len
								     (B_N_PITEM_HEAD
								      (tbS0,
								       0)),
								     "PAP-12106: item length must be 0");
								RFALSE
								    (comp_short_le_keys
								     (B_N_PKEY
								      (tbS0, 0),
								      B_N_PKEY
								      (tb->L[0],
								       n +
								       item_pos
								       -
								       ret_val)),
								     "PAP-12107: items must be of the same file");
								if (is_indirect_le_ih(B_N_PITEM_HEAD(tb->L[0], n + item_pos - ret_val))) {
									temp_l =
									    l_n
									    <<
									    (tb->
									     tb_sb->
									     s_blocksize_bits
									     -
									     UNFM_P_SHIFT);
								}
								/* update key of first item in S0 */
								version =
								    ih_version
								    (B_N_PITEM_HEAD
								     (tbS0, 0));
								set_le_key_k_offset
								    (version,
								     B_N_PKEY
								     (tbS0, 0),
								     le_key_k_offset
								     (version,
								      B_N_PKEY
								      (tbS0,
								       0)) +
								     temp_l);
								/* update left delimiting key */
								set_le_key_k_offset
								    (version,
								     B_N_PDELIM_KEY
								     (tb->
								      CFL[0],
								      tb->
								      lkey[0]),
								     le_key_k_offset
								     (version,
								      B_N_PDELIM_KEY
								      (tb->
								       CFL[0],
								       tb->
								       lkey[0]))
								     + temp_l);
							}

							/* Calculate new body, position in item and insert_size[0] */
							if (l_n > zeros_num) {
								body +=
								    (l_n -
								     zeros_num);
								zeros_num = 0;
							} else
								zeros_num -=
								    l_n;
							pos_in_item = 0;

							RFALSE
							    (comp_short_le_keys
							     (B_N_PKEY(tbS0, 0),
							      B_N_PKEY(tb->L[0],
								       B_NR_ITEMS
								       (tb->
									L[0]) -
								       1))
							     ||
							     !op_is_left_mergeable
							     (B_N_PKEY(tbS0, 0),
							      tbS0->b_size)
							     ||
							     !op_is_left_mergeable
							     (B_N_PDELIM_KEY
							      (tb->CFL[0],
							       tb->lkey[0]),
							      tbS0->b_size),
							     "PAP-12120: item must be merge-able with left neighboring item");
						} else {	/* only part of the appended item will be in L[0] */

							/* Calculate position in item for append in S[0] */
							pos_in_item -=
							    tb->lbytes;

							RFALSE(pos_in_item <= 0,
							       "PAP-12125: no place for paste. pos_in_item=%d",
							       pos_in_item);

							/* Shift lnum[0] - 1 items in whole. Shift lbytes - 1 byte from item number lnum[0] */
							leaf_shift_left(tb,
									tb->
									lnum[0],
									tb->
									lbytes);
						}
					}
				} else {	/* appended item will be in L[0] in whole */

					struct item_head *pasted;

					if (!item_pos && op_is_left_mergeable(B_N_PKEY(tbS0, 0), tbS0->b_size)) {	/* if we paste into first item of S[0] and it is left mergable */
						/* then increment pos_in_item by the size of the last item in L[0] */
						pasted =
						    B_N_PITEM_HEAD(tb->L[0],
								   n - 1);
						if (is_direntry_le_ih(pasted))
							pos_in_item +=
							    ih_entry_count
							    (pasted);
						else
							pos_in_item +=
							    ih_item_len(pasted);
					}

					/* Shift lnum[0] - 1 items in whole. Shift lbytes - 1 byte from item number lnum[0] */
					ret_val =
					    leaf_shift_left(tb, tb->lnum[0],
							    tb->lbytes);
					/* Append to body of item in L[0] */
					bi.tb = tb;
					bi.bi_bh = tb->L[0];
					bi.bi_parent = tb->FL[0];
					bi.bi_position =
					    get_left_neighbor_position(tb, 0);
					leaf_paste_in_buffer(&bi,
							     n + item_pos -
							     ret_val,
							     pos_in_item,
							     tb->insert_size[0],
							     body, zeros_num);

					/* if appended item is directory, paste entry */
					pasted =
					    B_N_PITEM_HEAD(tb->L[0],
							   n + item_pos -
							   ret_val);
					if (is_direntry_le_ih(pasted))
						leaf_paste_entries(bi.bi_bh,
								   n +
								   item_pos -
								   ret_val,
								   pos_in_item,
								   1,
								   (struct
								    reiserfs_de_head
								    *)body,
								   body +
								   DEH_SIZE,
								   tb->
								   insert_size
								   [0]
						    );
					/* if appended item is indirect item, put unformatted node into un list */
					if (is_indirect_le_ih(pasted))
						set_ih_free_space(pasted, 0);
					tb->insert_size[0] = 0;
					zeros_num = 0;
				}
				break;
			default:	/* cases d and t */
				reiserfs_panic(tb->tb_sb,
					       "PAP-12130: balance_leaf: lnum > 0: unexpectable mode: %s(%d)",
					       (flag ==
						M_DELETE) ? "DELETE" : ((flag ==
									 M_CUT)
									? "CUT"
									:
									"UNKNOWN"),
					       flag);
			}
		} else {
			/* new item doesn't fall into L[0] */
			leaf_shift_left(tb, tb->lnum[0], tb->lbytes);
		}
	}

	/* tb->lnum[0] > 0 */
	/* Calculate new item position */
	item_pos -= (tb->lnum[0] - ((tb->lbytes != -1) ? 1 : 0));

	if (tb->rnum[0] > 0) {
		/* shift rnum[0] items from S[0] to the right neighbor R[0] */
		n = B_NR_ITEMS(tbS0);
		switch (flag) {

		case M_INSERT:	/* insert item */
			if (n - tb->rnum[0] < item_pos) {	/* new item or its part falls to R[0] */
				if (item_pos == n - tb->rnum[0] + 1 && tb->rbytes != -1) {	/* part of new item falls into R[0] */
					loff_t old_key_comp, old_len,
					    r_zeros_number;
					const char *r_body;
					int version;
					loff_t offset;

					leaf_shift_right(tb, tb->rnum[0] - 1,
							 -1);

					version = ih_version(ih);
					/* Remember key component and item length */
					old_key_comp = le_ih_k_offset(ih);
					old_len = ih_item_len(ih);

					/* Calculate key component and item length to insert into R[0] */
					offset =
					    le_ih_k_offset(ih) +
					    ((old_len -
					      tb->
					      rbytes) << (is_indirect_le_ih(ih)
							  ? tb->tb_sb->
							  s_blocksize_bits -
							  UNFM_P_SHIFT : 0));
					set_le_ih_k_offset(ih, offset);
					put_ih_item_len(ih, tb->rbytes);
					/* Insert part of the item into R[0] */
					bi.tb = tb;
					bi.bi_bh = tb->R[0];
					bi.bi_parent = tb->FR[0];
					bi.bi_position =
					    get_right_neighbor_position(tb, 0);
					if ((old_len - tb->rbytes) > zeros_num) {
						r_zeros_number = 0;
						r_body =
						    body + (old_len -
							    tb->rbytes) -
						    zeros_num;
					} else {
						r_body = body;
						r_zeros_number =
						    zeros_num - (old_len -
								 tb->rbytes);
						zeros_num -= r_zeros_number;
					}

					leaf_insert_into_buf(&bi, 0, ih, r_body,
							     r_zeros_number);

					/* Replace right delimiting key by first key in R[0] */
					replace_key(tb, tb->CFR[0], tb->rkey[0],
						    tb->R[0], 0);

					/* Calculate key component and item length to insert into S[0] */
					set_le_ih_k_offset(ih, old_key_comp);
					put_ih_item_len(ih,
							old_len - tb->rbytes);

					tb->insert_size[0] -= tb->rbytes;

				} else {	/* whole new item falls into R[0] */

					/* Shift rnum[0]-1 items to R[0] */
					ret_val =
					    leaf_shift_right(tb,
							     tb->rnum[0] - 1,
							     tb->rbytes);
					/* Insert new item into R[0] */
					bi.tb = tb;
					bi.bi_bh = tb->R[0];
					bi.bi_parent = tb->FR[0];
					bi.bi_position =
					    get_right_neighbor_position(tb, 0);
					leaf_insert_into_buf(&bi,
							     item_pos - n +
							     tb->rnum[0] - 1,
							     ih, body,
							     zeros_num);

					if (item_pos - n + tb->rnum[0] - 1 == 0) {
						replace_key(tb, tb->CFR[0],
							    tb->rkey[0],
							    tb->R[0], 0);

					}
					zeros_num = tb->insert_size[0] = 0;
				}
			} else {	/* new item or part of it doesn't fall into R[0] */

				leaf_shift_right(tb, tb->rnum[0], tb->rbytes);
			}
			break;

		case M_PASTE:	/* append item */

			if (n - tb->rnum[0] <= item_pos) {	/* pasted item or part of it falls to R[0] */
				if (item_pos == n - tb->rnum[0] && tb->rbytes != -1) {	/* we must shift the part of the appended item */
					if (is_direntry_le_ih(B_N_PITEM_HEAD(tbS0, item_pos))) {	/* we append to directory item */
						int entry_count;

						RFALSE(zeros_num,
						       "PAP-12145: invalid parameter in case of a directory");
						entry_count =
						    I_ENTRY_COUNT(B_N_PITEM_HEAD
								  (tbS0,
								   item_pos));
						if (entry_count - tb->rbytes <
						    pos_in_item)
							/* new directory entry falls into R[0] */
						{
							int paste_entry_position;

							RFALSE(tb->rbytes - 1 >=
							       entry_count
							       || !tb->
							       insert_size[0],
							       "PAP-12150: no enough of entries to shift to R[0]: rbytes=%d, entry_count=%d",
							       tb->rbytes,
							       entry_count);
							/* Shift rnum[0]-1 items in whole. Shift rbytes-1 directory entries from directory item number rnum[0] */
							leaf_shift_right(tb,
									 tb->
									 rnum
									 [0],
									 tb->
									 rbytes
									 - 1);
							/* Paste given directory entry to directory item */
							paste_entry_position =
							    pos_in_item -
							    entry_count +
							    tb->rbytes - 1;
							bi.tb = tb;
							bi.bi_bh = tb->R[0];
							bi.bi_parent =
							    tb->FR[0];
							bi.bi_position =
							    get_right_neighbor_position
							    (tb, 0);
							leaf_paste_in_buffer
							    (&bi, 0,
							     paste_entry_position,
							     tb->insert_size[0],
							     body, zeros_num);
							/* paste entry */
							leaf_paste_entries(bi.
									   bi_bh,
									   0,
									   paste_entry_position,
									   1,
									   (struct
									    reiserfs_de_head
									    *)
									   body,
									   body
									   +
									   DEH_SIZE,
									   tb->
									   insert_size
									   [0]
							    );

							if (paste_entry_position
							    == 0) {
								/* change delimiting keys */
								replace_key(tb,
									    tb->
									    CFR
									    [0],
									    tb->
									    rkey
									    [0],
									    tb->
									    R
									    [0],
									    0);
							}

							tb->insert_size[0] = 0;
							pos_in_item++;
						} else {	/* new directory entry doesn't fall into R[0] */

							leaf_shift_right(tb,
									 tb->
									 rnum
									 [0],
									 tb->
									 rbytes);
						}
					} else {	/* regular object */

						int n_shift, n_rem,
						    r_zeros_number;
						const char *r_body;

						/* Calculate number of bytes which must be shifted from appended item */
						if ((n_shift =
						     tb->rbytes -
						     tb->insert_size[0]) < 0)
							n_shift = 0;

						RFALSE(pos_in_item !=
						       ih_item_len
						       (B_N_PITEM_HEAD
							(tbS0, item_pos)),
						       "PAP-12155: invalid position to paste. ih_item_len=%d, pos_in_item=%d",
						       pos_in_item,
						       ih_item_len
						       (B_N_PITEM_HEAD
							(tbS0, item_pos)));

						leaf_shift_right(tb,
								 tb->rnum[0],
								 n_shift);
						/* Calculate number of bytes which must remain in body after appending to R[0] */
						if ((n_rem =
						     tb->insert_size[0] -
						     tb->rbytes) < 0)
							n_rem = 0;

						{
							int version;
							unsigned long temp_rem =
							    n_rem;

							version =
							    ih_version
							    (B_N_PITEM_HEAD
							     (tb->R[0], 0));
							if (is_indirect_le_key
							    (version,
							     B_N_PKEY(tb->R[0],
								      0))) {
								temp_rem =
								    n_rem <<
								    (tb->tb_sb->
								     s_blocksize_bits
								     -
								     UNFM_P_SHIFT);
							}
							set_le_key_k_offset
							    (version,
							     B_N_PKEY(tb->R[0],
								      0),
							     le_key_k_offset
							     (version,
							      B_N_PKEY(tb->R[0],
								       0)) +
							     temp_rem);
							set_le_key_k_offset
							    (version,
							     B_N_PDELIM_KEY(tb->
									    CFR
									    [0],
									    tb->
									    rkey
									    [0]),
							     le_key_k_offset
							     (version,
							      B_N_PDELIM_KEY
							      (tb->CFR[0],
							       tb->rkey[0])) +
							     temp_rem);
						}
/*		  k_offset (B_N_PKEY(tb->R[0],0)) += n_rem;
		  k_offset (B_N_PDELIM_KEY(tb->CFR[0],tb->rkey[0])) += n_rem;*/
						do_balance_mark_internal_dirty
						    (tb, tb->CFR[0], 0);

						/* Append part of body into R[0] */
						bi.tb = tb;
						bi.bi_bh = tb->R[0];
						bi.bi_parent = tb->FR[0];
						bi.bi_position =
						    get_right_neighbor_position
						    (tb, 0);
						if (n_rem > zeros_num) {
							r_zeros_number = 0;
							r_body =
							    body + n_rem -
							    zeros_num;
						} else {
							r_body = body;
							r_zeros_number =
							    zeros_num - n_rem;
							zeros_num -=
							    r_zeros_number;
						}

						leaf_paste_in_buffer(&bi, 0,
								     n_shift,
								     tb->
								     insert_size
								     [0] -
								     n_rem,
								     r_body,
								     r_zeros_number);

						if (is_indirect_le_ih
						    (B_N_PITEM_HEAD
						     (tb->R[0], 0))) {
#if 0
							RFALSE(n_rem,
							       "PAP-12160: paste more than one unformatted node pointer");
#endif
							set_ih_free_space
							    (B_N_PITEM_HEAD
							     (tb->R[0], 0), 0);
						}
						tb->insert_size[0] = n_rem;
						if (!n_rem)
							pos_in_item++;
					}
				} else {	/* pasted item in whole falls into R[0] */

					struct item_head *pasted;

					ret_val =
					    leaf_shift_right(tb, tb->rnum[0],
							     tb->rbytes);
					/* append item in R[0] */
					if (pos_in_item >= 0) {
						bi.tb = tb;
						bi.bi_bh = tb->R[0];
						bi.bi_parent = tb->FR[0];
						bi.bi_position =
						    get_right_neighbor_position
						    (tb, 0);
						leaf_paste_in_buffer(&bi,
								     item_pos -
								     n +
								     tb->
								     rnum[0],
								     pos_in_item,
								     tb->
								     insert_size
								     [0], body,
								     zeros_num);
					}

					/* paste new entry, if item is directory item */
					pasted =
					    B_N_PITEM_HEAD(tb->R[0],
							   item_pos - n +
							   tb->rnum[0]);
					if (is_direntry_le_ih(pasted)
					    && pos_in_item >= 0) {
						leaf_paste_entries(bi.bi_bh,
								   item_pos -
								   n +
								   tb->rnum[0],
								   pos_in_item,
								   1,
								   (struct
								    reiserfs_de_head
								    *)body,
								   body +
								   DEH_SIZE,
								   tb->
								   insert_size
								   [0]
						    );
						if (!pos_in_item) {

							RFALSE(item_pos - n +
							       tb->rnum[0],
							       "PAP-12165: directory item must be first item of node when pasting is in 0th position");

							/* update delimiting keys */
							replace_key(tb,
								    tb->CFR[0],
								    tb->rkey[0],
								    tb->R[0],
								    0);
						}
					}

					if (is_indirect_le_ih(pasted))
						set_ih_free_space(pasted, 0);
					zeros_num = tb->insert_size[0] = 0;
				}
			} else {	/* new item doesn't fall into R[0] */

				leaf_shift_right(tb, tb->rnum[0], tb->rbytes);
			}
			break;
		default:	/* cases d and t */
			reiserfs_panic(tb->tb_sb,
				       "PAP-12175: balance_leaf: rnum > 0: unexpectable mode: %s(%d)",
				       (flag ==
					M_DELETE) ? "DELETE" : ((flag ==
								 M_CUT) ? "CUT"
								: "UNKNOWN"),
				       flag);
		}

	}

	/* tb->rnum[0] > 0 */
	RFALSE(tb->blknum[0] > 3,
	       "PAP-12180: blknum can not be %d. It must be <= 3",
	       tb->blknum[0]);
	RFALSE(tb->blknum[0] < 0,
	       "PAP-12185: blknum can not be %d. It must be >= 0",
	       tb->blknum[0]);

	/* if while adding to a node we discover that it is possible to split
	   it in two, and merge the left part into the left neighbor and the
	   right part into the right neighbor, eliminating the node */
	if (tb->blknum[0] == 0) {	/* node S[0] is empty now */

		RFALSE(!tb->lnum[0] || !tb->rnum[0],
		       "PAP-12190: lnum and rnum must not be zero");
		/* if insertion was done before 0-th position in R[0], right
		   delimiting key of the tb->L[0]'s and left delimiting key are
		   not set correctly */
		if (tb->CFL[0]) {
			if (!tb->CFR[0])
				reiserfs_panic(tb->tb_sb,
					       "vs-12195: balance_leaf: CFR not initialized");
			copy_key(B_N_PDELIM_KEY(tb->CFL[0], tb->lkey[0]),
				 B_N_PDELIM_KEY(tb->CFR[0], tb->rkey[0]));
			do_balance_mark_internal_dirty(tb, tb->CFL[0], 0);
		}

		reiserfs_invalidate_buffer(tb, tbS0);
		return 0;
	}

	/* Fill new nodes that appear in place of S[0] */

	/* I am told that this copying is because we need an array to enable
	   the looping code. -Hans */
	snum[0] = tb->s1num, snum[1] = tb->s2num;
	sbytes[0] = tb->s1bytes;
	sbytes[1] = tb->s2bytes;
	for (i = tb->blknum[0] - 2; i >= 0; i--) {

		RFALSE(!snum[i], "PAP-12200: snum[%d] == %d. Must be > 0", i,
		       snum[i]);

		/* here we shift from S to S_new nodes */

		S_new[i] = get_FEB(tb);

		/* initialized block type and tree level */
		set_blkh_level(B_BLK_HEAD(S_new[i]), DISK_LEAF_NODE_LEVEL);

		n = B_NR_ITEMS(tbS0);

		switch (flag) {
		case M_INSERT:	/* insert item */

			if (n - snum[i] < item_pos) {	/* new item or it's part falls to first new node S_new[i] */
				if (item_pos == n - snum[i] + 1 && sbytes[i] != -1) {	/* part of new item falls into S_new[i] */
					int old_key_comp, old_len,
					    r_zeros_number;
					const char *r_body;
					int version;

					/* Move snum[i]-1 items from S[0] to S_new[i] */
					leaf_move_items(LEAF_FROM_S_TO_SNEW, tb,
							snum[i] - 1, -1,
							S_new[i]);
					/* Remember key component and item length */
					version = ih_version(ih);
					old_key_comp = le_ih_k_offset(ih);
					old_len = ih_item_len(ih);

					/* Calculate key component and item length to insert into S_new[i] */
					set_le_ih_k_offset(ih,
							   le_ih_k_offset(ih) +
							   ((old_len -
							     sbytes[i]) <<
							    (is_indirect_le_ih
							     (ih) ? tb->tb_sb->
							     s_blocksize_bits -
							     UNFM_P_SHIFT :
							     0)));

					put_ih_item_len(ih, sbytes[i]);

					/* Insert part of the item into S_new[i] before 0-th item */
					bi.tb = tb;
					bi.bi_bh = S_new[i];
					bi.bi_parent = NULL;
					bi.bi_position = 0;

					if ((old_len - sbytes[i]) > zeros_num) {
						r_zeros_number = 0;
						r_body =
						    body + (old_len -
							    sbytes[i]) -
						    zeros_num;
					} else {
						r_body = body;
						r_zeros_number =
						    zeros_num - (old_len -
								 sbytes[i]);
						zeros_num -= r_zeros_number;
					}

					leaf_insert_into_buf(&bi, 0, ih, r_body,
							     r_zeros_number);

					/* Calculate key component and item length to insert into S[i] */
					set_le_ih_k_offset(ih, old_key_comp);
					put_ih_item_len(ih,
							old_len - sbytes[i]);
					tb->insert_size[0] -= sbytes[i];
				} else {	/* whole new item falls into S_new[i] */

					/* Shift snum[0] - 1 items to S_new[i] (sbytes[i] of split item) */
					leaf_move_items(LEAF_FROM_S_TO_SNEW, tb,
							snum[i] - 1, sbytes[i],
							S_new[i]);

					/* Insert new item into S_new[i] */
					bi.tb = tb;
					bi.bi_bh = S_new[i];
					bi.bi_parent = NULL;
					bi.bi_position = 0;
					leaf_insert_into_buf(&bi,
							     item_pos - n +
							     snum[i] - 1, ih,
							     body, zeros_num);

					zeros_num = tb->insert_size[0] = 0;
				}
			}

			else {	/* new item or it part don't falls into S_new[i] */

				leaf_move_items(LEAF_FROM_S_TO_SNEW, tb,
						snum[i], sbytes[i], S_new[i]);
			}
			break;

		case M_PASTE:	/* append item */

			if (n - snum[i] <= item_pos) {	/* pasted item or part if it falls to S_new[i] */
				if (item_pos == n - snum[i] && sbytes[i] != -1) {	/* we must shift part of the appended item */
					struct item_head *aux_ih;

					RFALSE(ih, "PAP-12210: ih must be 0");

					if (is_direntry_le_ih
					    (aux_ih =
					     B_N_PITEM_HEAD(tbS0, item_pos))) {
						/* we append to directory item */

						int entry_count;

						entry_count =
						    ih_entry_count(aux_ih);

						if (entry_count - sbytes[i] <
						    pos_in_item
						    && pos_in_item <=
						    entry_count) {
							/* new directory entry falls into S_new[i] */

							RFALSE(!tb->
							       insert_size[0],
							       "PAP-12215: insert_size is already 0");
							RFALSE(sbytes[i] - 1 >=
							       entry_count,
							       "PAP-12220: there are no so much entries (%d), only %d",
							       sbytes[i] - 1,
							       entry_count);

							/* Shift snum[i]-1 items in whole. Shift sbytes[i] directory entries from directory item number snum[i] */
							leaf_move_items
							    (LEAF_FROM_S_TO_SNEW,
							     tb, snum[i],
							     sbytes[i] - 1,
							     S_new[i]);
							/* Paste given directory entry to directory item */
							bi.tb = tb;
							bi.bi_bh = S_new[i];
							bi.bi_parent = NULL;
							bi.bi_position = 0;
							leaf_paste_in_buffer
							    (&bi, 0,
							     pos_in_item -
							     entry_count +
							     sbytes[i] - 1,
							     tb->insert_size[0],
							     body, zeros_num);
							/* paste new directory entry */
							leaf_paste_entries(bi.
									   bi_bh,
									   0,
									   pos_in_item
									   -
									   entry_count
									   +
									   sbytes
									   [i] -
									   1, 1,
									   (struct
									    reiserfs_de_head
									    *)
									   body,
									   body
									   +
									   DEH_SIZE,
									   tb->
									   insert_size
									   [0]
							    );
							tb->insert_size[0] = 0;
							pos_in_item++;
						} else {	/* new directory entry doesn't fall into S_new[i] */
							leaf_move_items
							    (LEAF_FROM_S_TO_SNEW,
							     tb, snum[i],
							     sbytes[i],
							     S_new[i]);
						}
					} else {	/* regular object */

						int n_shift, n_rem,
						    r_zeros_number;
						const char *r_body;

						RFALSE(pos_in_item !=
						       ih_item_len
						       (B_N_PITEM_HEAD
							(tbS0, item_pos))
						       || tb->insert_size[0] <=
						       0,
						       "PAP-12225: item too short or insert_size <= 0");

						/* Calculate number of bytes which must be shifted from appended item */
						n_shift =
						    sbytes[i] -
						    tb->insert_size[0];
						if (n_shift < 0)
							n_shift = 0;
						leaf_move_items
						    (LEAF_FROM_S_TO_SNEW, tb,
						     snum[i], n_shift,
						     S_new[i]);

						/* Calculate number of bytes which must remain in body after append to S_new[i] */
						n_rem =
						    tb->insert_size[0] -
						    sbytes[i];
						if (n_rem < 0)
							n_rem = 0;
						/* Append part of body into S_new[0] */
						bi.tb = tb;
						bi.bi_bh = S_new[i];
						bi.bi_parent = NULL;
						bi.bi_position = 0;

						if (n_rem > zeros_num) {
							r_zeros_number = 0;
							r_body =
							    body + n_rem -
							    zeros_num;
						} else {
							r_body = body;
							r_zeros_number =
							    zeros_num - n_rem;
							zeros_num -=
							    r_zeros_number;
						}

						leaf_paste_in_buffer(&bi, 0,
								     n_shift,
								     tb->
								     insert_size
								     [0] -
								     n_rem,
								     r_body,
								     r_zeros_number);
						{
							struct item_head *tmp;

							tmp =
							    B_N_PITEM_HEAD(S_new
									   [i],
									   0);
							if (is_indirect_le_ih
							    (tmp)) {
								set_ih_free_space
								    (tmp, 0);
								set_le_ih_k_offset
								    (tmp,
								     le_ih_k_offset
								     (tmp) +
								     (n_rem <<
								      (tb->
								       tb_sb->
								       s_blocksize_bits
								       -
								       UNFM_P_SHIFT)));
							} else {
								set_le_ih_k_offset
								    (tmp,
								     le_ih_k_offset
								     (tmp) +
								     n_rem);
							}
						}

						tb->insert_size[0] = n_rem;
						if (!n_rem)
							pos_in_item++;
					}
				} else
					/* item falls wholly into S_new[i] */
				{
					int ret_val;
					struct item_head *pasted;

#ifdef CONFIG_REISERFS_CHECK
					struct item_head *ih =
					    B_N_PITEM_HEAD(tbS0, item_pos);

					if (!is_direntry_le_ih(ih)
					    && (pos_in_item != ih_item_len(ih)
						|| tb->insert_size[0] <= 0))
						reiserfs_panic(tb->tb_sb,
							       "PAP-12235: balance_leaf: pos_in_item must be equal to ih_item_len");
#endif				/* CONFIG_REISERFS_CHECK */

					ret_val =
					    leaf_move_items(LEAF_FROM_S_TO_SNEW,
							    tb, snum[i],
							    sbytes[i],
							    S_new[i]);

					RFALSE(ret_val,
					       "PAP-12240: unexpected value returned by leaf_move_items (%d)",
					       ret_val);

					/* paste into item */
					bi.tb = tb;
					bi.bi_bh = S_new[i];
					bi.bi_parent = NULL;
					bi.bi_position = 0;
					leaf_paste_in_buffer(&bi,
							     item_pos - n +
							     snum[i],
							     pos_in_item,
							     tb->insert_size[0],
							     body, zeros_num);

					pasted =
					    B_N_PITEM_HEAD(S_new[i],
							   item_pos - n +
							   snum[i]);
					if (is_direntry_le_ih(pasted)) {
						leaf_paste_entries(bi.bi_bh,
								   item_pos -
								   n + snum[i],
								   pos_in_item,
								   1,
								   (struct
								    reiserfs_de_head
								    *)body,
								   body +
								   DEH_SIZE,
								   tb->
								   insert_size
								   [0]
						    );
					}

					/* if we paste to indirect item update ih_free_space */
					if (is_indirect_le_ih(pasted))
						set_ih_free_space(pasted, 0);
					zeros_num = tb->insert_size[0] = 0;
				}
			}

			else {	/* pasted item doesn't fall into S_new[i] */

				leaf_move_items(LEAF_FROM_S_TO_SNEW, tb,
						snum[i], sbytes[i], S_new[i]);
			}
			break;
		default:	/* cases d and t */
			reiserfs_panic(tb->tb_sb,
				       "PAP-12245: balance_leaf: blknum > 2: unexpectable mode: %s(%d)",
				       (flag ==
					M_DELETE) ? "DELETE" : ((flag ==
								 M_CUT) ? "CUT"
								: "UNKNOWN"),
				       flag);
		}

		memcpy(insert_key + i, B_N_PKEY(S_new[i], 0), KEY_SIZE);
		insert_ptr[i] = S_new[i];

		RFALSE(!buffer_journaled(S_new[i])
		       || buffer_journal_dirty(S_new[i])
		       || buffer_dirty(S_new[i]), "PAP-12247: S_new[%d] : (%b)",
		       i, S_new[i]);
	}

	/* if the affected item was not wholly shifted then we perform all necessary operations on that part or whole of the
	   affected item which remains in S */
	if (0 <= item_pos && item_pos < tb->s0num) {	/* if we must insert or append into buffer S[0] */

		switch (flag) {
		case M_INSERT:	/* insert item into S[0] */
			bi.tb = tb;
			bi.bi_bh = tbS0;
			bi.bi_parent = PATH_H_PPARENT(tb->tb_path, 0);
			bi.bi_position = PATH_H_POSITION(tb->tb_path, 1);
			leaf_insert_into_buf(&bi, item_pos, ih, body,
					     zeros_num);

			/* If we insert the first key change the delimiting key */
			if (item_pos == 0) {
				if (tb->CFL[0])	/* can be 0 in reiserfsck */
					replace_key(tb, tb->CFL[0], tb->lkey[0],
						    tbS0, 0);

			}
			break;

		case M_PASTE:{	/* append item in S[0] */
				struct item_head *pasted;

				pasted = B_N_PITEM_HEAD(tbS0, item_pos);
				/* when directory, may be new entry already pasted */
				if (is_direntry_le_ih(pasted)) {
					if (pos_in_item >= 0 &&
					    pos_in_item <=
					    ih_entry_count(pasted)) {

						RFALSE(!tb->insert_size[0],
						       "PAP-12260: insert_size is 0 already");

						/* prepare space */
						bi.tb = tb;
						bi.bi_bh = tbS0;
						bi.bi_parent =
						    PATH_H_PPARENT(tb->tb_path,
								   0);
						bi.bi_position =
						    PATH_H_POSITION(tb->tb_path,
								    1);
						leaf_paste_in_buffer(&bi,
								     item_pos,
								     pos_in_item,
								     tb->
								     insert_size
								     [0], body,
								     zeros_num);

						/* paste entry */
						leaf_paste_entries(bi.bi_bh,
								   item_pos,
								   pos_in_item,
								   1,
								   (struct
								    reiserfs_de_head
								    *)body,
								   body +
								   DEH_SIZE,
								   tb->
								   insert_size
								   [0]
						    );
						if (!item_pos && !pos_in_item) {
							RFALSE(!tb->CFL[0]
							       || !tb->L[0],
							       "PAP-12270: CFL[0]/L[0] must be specified");
							if (tb->CFL[0]) {
								replace_key(tb,
									    tb->
									    CFL
									    [0],
									    tb->
									    lkey
									    [0],
									    tbS0,
									    0);

							}
						}
						tb->insert_size[0] = 0;
					}
				} else {	/* regular object */
					if (pos_in_item == ih_item_len(pasted)) {

						RFALSE(tb->insert_size[0] <= 0,
						       "PAP-12275: insert size must not be %d",
						       tb->insert_size[0]);
						bi.tb = tb;
						bi.bi_bh = tbS0;
						bi.bi_parent =
						    PATH_H_PPARENT(tb->tb_path,
								   0);
						bi.bi_position =
						    PATH_H_POSITION(tb->tb_path,
								    1);
						leaf_paste_in_buffer(&bi,
								     item_pos,
								     pos_in_item,
								     tb->
								     insert_size
								     [0], body,
								     zeros_num);

						if (is_indirect_le_ih(pasted)) {
#if 0
							RFALSE(tb->
							       insert_size[0] !=
							       UNFM_P_SIZE,
							       "PAP-12280: insert_size for indirect item must be %d, not %d",
							       UNFM_P_SIZE,
							       tb->
							       insert_size[0]);
#endif
							set_ih_free_space
							    (pasted, 0);
						}
						tb->insert_size[0] = 0;
					}
#ifdef CONFIG_REISERFS_CHECK
					else {
						if (tb->insert_size[0]) {
							print_cur_tb("12285");
							reiserfs_panic(tb->
								       tb_sb,
								       "PAP-12285: balance_leaf: insert_size must be 0 (%d)",
								       tb->
								       insert_size
								       [0]);
						}
					}
#endif				/* CONFIG_REISERFS_CHECK */

				}
			}	/* case M_PASTE: */
		}
	}
#ifdef CONFIG_REISERFS_CHECK
	if (flag == M_PASTE && tb->insert_size[0]) {
		print_cur_tb("12290");
		reiserfs_panic(tb->tb_sb,
			       "PAP-12290: balance_leaf: insert_size is still not 0 (%d)",
			       tb->insert_size[0]);
	}
#endif				/* CONFIG_REISERFS_CHECK */

	return 0;
}				/* Leaf level of the tree is balanced (end of balance_leaf) */

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
	struct buffer_head *first_b;
	struct buffer_info bi;

	for (i = 0; i < MAX_FEB_SIZE; i++)
		if (tb->FEB[i] != 0)
			break;

	if (i == MAX_FEB_SIZE)
		reiserfs_panic(tb->tb_sb,
			       "vs-12300: get_FEB: FEB list is empty");

	bi.tb = tb;
	bi.bi_bh = first_b = tb->FEB[i];
	bi.bi_parent = NULL;
	bi.bi_position = 0;
	make_empty_node(&bi);
	set_buffer_uptodate(first_b);
	tb->FEB[i] = NULL;
	tb->used[i] = first_b;

	return (first_b);
}

/* This is now used because reiserfs_free_block has to be able to
** schedule.
*/
static void store_thrown(struct tree_balance *tb, struct buffer_head *bh)
{
	int i;

	if (buffer_dirty(bh))
		reiserfs_warning(tb->tb_sb,
				 "store_thrown deals with dirty buffer");
	for (i = 0; i < sizeof(tb->thrown) / sizeof(tb->thrown[0]); i++)
		if (!tb->thrown[i]) {
			tb->thrown[i] = bh;
			get_bh(bh);	/* free_thrown puts this */
			return;
		}
	reiserfs_warning(tb->tb_sb, "store_thrown: too many thrown buffers");
}

static void free_thrown(struct tree_balance *tb)
{
	int i;
	b_blocknr_t blocknr;
	for (i = 0; i < sizeof(tb->thrown) / sizeof(tb->thrown[0]); i++) {
		if (tb->thrown[i]) {
			blocknr = tb->thrown[i]->b_blocknr;
			if (buffer_dirty(tb->thrown[i]))
				reiserfs_warning(tb->tb_sb,
						 "free_thrown deals with dirty buffer %d",
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
		memcpy(B_N_PDELIM_KEY(dest, n_dest), B_N_PITEM_HEAD(src, n_src),
		       KEY_SIZE);
	else
		memcpy(B_N_PDELIM_KEY(dest, n_dest), B_N_PDELIM_KEY(src, n_src),
		       KEY_SIZE);

	do_balance_mark_internal_dirty(tb, dest, 0);
}

int get_left_neighbor_position(struct tree_balance *tb, int h)
{
	int Sh_position = PATH_H_POSITION(tb->tb_path, h + 1);

	RFALSE(PATH_H_PPARENT(tb->tb_path, h) == 0 || tb->FL[h] == 0,
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

	RFALSE(PATH_H_PPARENT(tb->tb_path, h) == 0 || tb->FR[h] == 0,
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
			reiserfs_panic(s,
				       "PAP-12338: check_internal_node: invalid child pointer %y in %b",
				       dc, bh);
		}
	}
}

static int locked_or_not_in_tree(struct buffer_head *bh, char *which)
{
	if ((!buffer_journal_prepared(bh) && buffer_locked(bh)) ||
	    !B_IS_IN_TREE(bh)) {
		reiserfs_warning(NULL,
				 "vs-12339: locked_or_not_in_tree: %s (%b)",
				 which, bh);
		return 1;
	}
	return 0;
}

static int check_before_balancing(struct tree_balance *tb)
{
	int retval = 0;

	if (cur_tb) {
		reiserfs_panic(tb->tb_sb, "vs-12335: check_before_balancing: "
			       "suspect that schedule occurred based on cur_tb not being null at this point in code. "
			       "do_balance cannot properly handle schedule occurring while it runs.");
	}

	/* double check that buffers that we will modify are unlocked. (fix_nodes should already have
	   prepped all of these for us). */
	if (tb->lnum[0]) {
		retval |= locked_or_not_in_tree(tb->L[0], "L[0]");
		retval |= locked_or_not_in_tree(tb->FL[0], "FL[0]");
		retval |= locked_or_not_in_tree(tb->CFL[0], "CFL[0]");
		check_leaf(tb->L[0]);
	}
	if (tb->rnum[0]) {
		retval |= locked_or_not_in_tree(tb->R[0], "R[0]");
		retval |= locked_or_not_in_tree(tb->FR[0], "FR[0]");
		retval |= locked_or_not_in_tree(tb->CFR[0], "CFR[0]");
		check_leaf(tb->R[0]);
	}
	retval |= locked_or_not_in_tree(PATH_PLAST_BUFFER(tb->tb_path), "S[0]");
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
			reiserfs_panic(tb->tb_sb,
				       "PAP-12355: check_after_balance_leaf: shift to left was incorrect");
		}
	}
	if (tb->rnum[0]) {
		if (B_FREE_SPACE(tb->R[0]) !=
		    MAX_CHILD_SIZE(tb->R[0]) -
		    dc_size(B_N_CHILD
			    (tb->FR[0], get_right_neighbor_position(tb, 0)))) {
			print_cur_tb("12222");
			reiserfs_panic(tb->tb_sb,
				       "PAP-12360: check_after_balance_leaf: shift to right was incorrect");
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
		reiserfs_warning(tb->tb_sb,
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
		reiserfs_panic(tb->tb_sb,
			       "PAP-12365: check_after_balance_leaf: S is incorrect");
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

/* Now we have all of the buffers that must be used in balancing of
   the tree.  We rely on the assumption that schedule() will not occur
   while do_balance works. ( Only interrupt handlers are acceptable.)
   We balance the tree according to the analysis made before this,
   using buffers already obtained.  For SMP support it will someday be
   necessary to add ordered locking of tb. */

/* Some interesting rules of balancing:

   we delete a maximum of two nodes per level per balancing: we never
   delete R, when we delete two of three nodes L, S, R then we move
   them into R.

   we only delete L if we are deleting two nodes, if we delete only
   one node we delete S

   if we shift leaves then we shift as much as we can: this is a
   deliberate policy of extremism in node packing which results in
   higher average utilization after repeated random balance operations
   at the cost of more memory copies and more balancing as a result of
   small insertions to full nodes.

   if we shift internal nodes we try to evenly balance the node
   utilization, with consequent less balancing at the cost of lower
   utilization.

   one could argue that the policy for directories in leaves should be
   that of internal nodes, but we will wait until another day to
   evaluate this....  It would be nice to someday measure and prove
   these assumptions as to what is optimal....

*/

static inline void do_balance_starts(struct tree_balance *tb)
{
	/* use print_cur_tb() to see initial state of struct
	   tree_balance */

	/* store_print_tb (tb); */

	/* do not delete, just comment it out */
/*    print_tb(flag, PATH_LAST_POSITION(tb->tb_path), tb->tb_path->pos_in_item, tb, 
	     "check");*/
	RFALSE(check_before_balancing(tb), "PAP-12340: locked buffers in TB");
#ifdef CONFIG_REISERFS_CHECK
	cur_tb = tb;
#endif
}

static inline void do_balance_completed(struct tree_balance *tb)
{

#ifdef CONFIG_REISERFS_CHECK
	check_leaf_level(tb);
	check_internal_levels(tb);
	cur_tb = NULL;
#endif

	/* reiserfs_free_block is no longer schedule safe.  So, we need to
	 ** put the buffers we want freed on the thrown list during do_balance,
	 ** and then free them now
	 */

	REISERFS_SB(tb->tb_sb)->s_do_balance++;

	/* release all nodes hold to perform the balancing */
	unfix_nodes(tb);

	free_thrown(tb);
}

void do_balance(struct tree_balance *tb,	/* tree_balance structure */
		struct item_head *ih,	/* item header of inserted item */
		const char *body,	/* body  of inserted item or bytes to paste */
		int flag)
{				/* i - insert, d - delete
				   c - cut, p - paste

				   Cut means delete part of an item
				   (includes removing an entry from a
				   directory).

				   Delete means delete whole item.

				   Insert means add a new item into the
				   tree.

				   Paste means to append to the end of an
				   existing file or to insert a directory
				   entry.  */
	int child_pos,		/* position of a child node in its parent */
	 h;			/* level of the tree being processed */
	struct item_head insert_key[2];	/* in our processing of one level
					   we sometimes determine what
					   must be inserted into the next
					   higher level.  This insertion
					   consists of a key or two keys
					   and their corresponding
					   pointers */
	struct buffer_head *insert_ptr[2];	/* inserted node-ptrs for the next
						   level */

	tb->tb_mode = flag;
	tb->need_balance_dirty = 0;

	if (FILESYSTEM_CHANGED_TB(tb)) {
		reiserfs_panic(tb->tb_sb,
			       "clm-6000: do_balance, fs generation has changed\n");
	}
	/* if we have no real work to do  */
	if (!tb->insert_size[0]) {
		reiserfs_warning(tb->tb_sb,
				 "PAP-12350: do_balance: insert_size == 0, mode == %c",
				 flag);
		unfix_nodes(tb);
		return;
	}

	atomic_inc(&(fs_generation(tb->tb_sb)));
	do_balance_starts(tb);

	/* balance leaf returns 0 except if combining L R and S into
	   one node.  see balance_internal() for explanation of this
	   line of code. */
	child_pos = PATH_H_B_ITEM_ORDER(tb->tb_path, 0) +
	    balance_leaf(tb, ih, body, flag, insert_key, insert_ptr);

#ifdef CONFIG_REISERFS_CHECK
	check_after_balance_leaf(tb);
#endif

	/* Balance internal level of the tree. */
	for (h = 1; h < MAX_HEIGHT && tb->insert_size[h]; h++)
		child_pos =
		    balance_internal(tb, h, child_pos, insert_key, insert_ptr);

	do_balance_completed(tb);

}
