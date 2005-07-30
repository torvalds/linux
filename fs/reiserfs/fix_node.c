/*
 * Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README
 */

/**
 ** old_item_num
 ** old_entry_num
 ** set_entry_sizes
 ** create_virtual_node
 ** check_left
 ** check_right
 ** directory_part_size
 ** get_num_ver
 ** set_parameters
 ** is_leaf_removable
 ** are_leaves_removable
 ** get_empty_nodes
 ** get_lfree
 ** get_rfree
 ** is_left_neighbor_in_cache
 ** decrement_key
 ** get_far_parent
 ** get_parents
 ** can_node_be_removed
 ** ip_check_balance
 ** dc_check_balance_internal
 ** dc_check_balance_leaf
 ** dc_check_balance
 ** check_balance
 ** get_direct_parent
 ** get_neighbors
 ** fix_nodes
 ** 
 ** 
 **/

#include <linux/config.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/reiserfs_fs.h>
#include <linux/buffer_head.h>

/* To make any changes in the tree we find a node, that contains item
   to be changed/deleted or position in the node we insert a new item
   to. We call this node S. To do balancing we need to decide what we
   will shift to left/right neighbor, or to a new node, where new item
   will be etc. To make this analysis simpler we build virtual
   node. Virtual node is an array of items, that will replace items of
   node S. (For instance if we are going to delete an item, virtual
   node does not contain it). Virtual node keeps information about
   item sizes and types, mergeability of first and last items, sizes
   of all entries in directory item. We use this array of items when
   calculating what we can shift to neighbors and how many nodes we
   have to have if we do not any shiftings, if we shift to left/right
   neighbor or to both. */

/* taking item number in virtual node, returns number of item, that it has in source buffer */
static inline int old_item_num(int new_num, int affected_item_num, int mode)
{
	if (mode == M_PASTE || mode == M_CUT || new_num < affected_item_num)
		return new_num;

	if (mode == M_INSERT) {

		RFALSE(new_num == 0,
		       "vs-8005: for INSERT mode and item number of inserted item");

		return new_num - 1;
	}

	RFALSE(mode != M_DELETE,
	       "vs-8010: old_item_num: mode must be M_DELETE (mode = \'%c\'",
	       mode);
	/* delete mode */
	return new_num + 1;
}

static void create_virtual_node(struct tree_balance *tb, int h)
{
	struct item_head *ih;
	struct virtual_node *vn = tb->tb_vn;
	int new_num;
	struct buffer_head *Sh;	/* this comes from tb->S[h] */

	Sh = PATH_H_PBUFFER(tb->tb_path, h);

	/* size of changed node */
	vn->vn_size =
	    MAX_CHILD_SIZE(Sh) - B_FREE_SPACE(Sh) + tb->insert_size[h];

	/* for internal nodes array if virtual items is not created */
	if (h) {
		vn->vn_nr_item = (vn->vn_size - DC_SIZE) / (DC_SIZE + KEY_SIZE);
		return;
	}

	/* number of items in virtual node  */
	vn->vn_nr_item =
	    B_NR_ITEMS(Sh) + ((vn->vn_mode == M_INSERT) ? 1 : 0) -
	    ((vn->vn_mode == M_DELETE) ? 1 : 0);

	/* first virtual item */
	vn->vn_vi = (struct virtual_item *)(tb->tb_vn + 1);
	memset(vn->vn_vi, 0, vn->vn_nr_item * sizeof(struct virtual_item));
	vn->vn_free_ptr += vn->vn_nr_item * sizeof(struct virtual_item);

	/* first item in the node */
	ih = B_N_PITEM_HEAD(Sh, 0);

	/* define the mergeability for 0-th item (if it is not being deleted) */
	if (op_is_left_mergeable(&(ih->ih_key), Sh->b_size)
	    && (vn->vn_mode != M_DELETE || vn->vn_affected_item_num))
		vn->vn_vi[0].vi_type |= VI_TYPE_LEFT_MERGEABLE;

	/* go through all items those remain in the virtual node (except for the new (inserted) one) */
	for (new_num = 0; new_num < vn->vn_nr_item; new_num++) {
		int j;
		struct virtual_item *vi = vn->vn_vi + new_num;
		int is_affected =
		    ((new_num != vn->vn_affected_item_num) ? 0 : 1);

		if (is_affected && vn->vn_mode == M_INSERT)
			continue;

		/* get item number in source node */
		j = old_item_num(new_num, vn->vn_affected_item_num,
				 vn->vn_mode);

		vi->vi_item_len += ih_item_len(ih + j) + IH_SIZE;
		vi->vi_ih = ih + j;
		vi->vi_item = B_I_PITEM(Sh, ih + j);
		vi->vi_uarea = vn->vn_free_ptr;

		// FIXME: there is no check, that item operation did not
		// consume too much memory
		vn->vn_free_ptr +=
		    op_create_vi(vn, vi, is_affected, tb->insert_size[0]);
		if (tb->vn_buf + tb->vn_buf_size < vn->vn_free_ptr)
			reiserfs_panic(tb->tb_sb,
				       "vs-8030: create_virtual_node: "
				       "virtual node space consumed");

		if (!is_affected)
			/* this is not being changed */
			continue;

		if (vn->vn_mode == M_PASTE || vn->vn_mode == M_CUT) {
			vn->vn_vi[new_num].vi_item_len += tb->insert_size[0];
			vi->vi_new_data = vn->vn_data;	// pointer to data which is going to be pasted
		}
	}

	/* virtual inserted item is not defined yet */
	if (vn->vn_mode == M_INSERT) {
		struct virtual_item *vi = vn->vn_vi + vn->vn_affected_item_num;

		RFALSE(vn->vn_ins_ih == 0,
		       "vs-8040: item header of inserted item is not specified");
		vi->vi_item_len = tb->insert_size[0];
		vi->vi_ih = vn->vn_ins_ih;
		vi->vi_item = vn->vn_data;
		vi->vi_uarea = vn->vn_free_ptr;

		op_create_vi(vn, vi, 0 /*not pasted or cut */ ,
			     tb->insert_size[0]);
	}

	/* set right merge flag we take right delimiting key and check whether it is a mergeable item */
	if (tb->CFR[0]) {
		struct reiserfs_key *key;

		key = B_N_PDELIM_KEY(tb->CFR[0], tb->rkey[0]);
		if (op_is_left_mergeable(key, Sh->b_size)
		    && (vn->vn_mode != M_DELETE
			|| vn->vn_affected_item_num != B_NR_ITEMS(Sh) - 1))
			vn->vn_vi[vn->vn_nr_item - 1].vi_type |=
			    VI_TYPE_RIGHT_MERGEABLE;

#ifdef CONFIG_REISERFS_CHECK
		if (op_is_left_mergeable(key, Sh->b_size) &&
		    !(vn->vn_mode != M_DELETE
		      || vn->vn_affected_item_num != B_NR_ITEMS(Sh) - 1)) {
			/* we delete last item and it could be merged with right neighbor's first item */
			if (!
			    (B_NR_ITEMS(Sh) == 1
			     && is_direntry_le_ih(B_N_PITEM_HEAD(Sh, 0))
			     && I_ENTRY_COUNT(B_N_PITEM_HEAD(Sh, 0)) == 1)) {
				/* node contains more than 1 item, or item is not directory item, or this item contains more than 1 entry */
				print_block(Sh, 0, -1, -1);
				reiserfs_panic(tb->tb_sb,
					       "vs-8045: create_virtual_node: rdkey %k, affected item==%d (mode==%c) Must be %c",
					       key, vn->vn_affected_item_num,
					       vn->vn_mode, M_DELETE);
			} else
				/* we can delete directory item, that has only one directory entry in it */
				;
		}
#endif

	}
}

/* using virtual node check, how many items can be shifted to left
   neighbor */
static void check_left(struct tree_balance *tb, int h, int cur_free)
{
	int i;
	struct virtual_node *vn = tb->tb_vn;
	struct virtual_item *vi;
	int d_size, ih_size;

	RFALSE(cur_free < 0, "vs-8050: cur_free (%d) < 0", cur_free);

	/* internal level */
	if (h > 0) {
		tb->lnum[h] = cur_free / (DC_SIZE + KEY_SIZE);
		return;
	}

	/* leaf level */

	if (!cur_free || !vn->vn_nr_item) {
		/* no free space or nothing to move */
		tb->lnum[h] = 0;
		tb->lbytes = -1;
		return;
	}

	RFALSE(!PATH_H_PPARENT(tb->tb_path, 0),
	       "vs-8055: parent does not exist or invalid");

	vi = vn->vn_vi;
	if ((unsigned int)cur_free >=
	    (vn->vn_size -
	     ((vi->vi_type & VI_TYPE_LEFT_MERGEABLE) ? IH_SIZE : 0))) {
		/* all contents of S[0] fits into L[0] */

		RFALSE(vn->vn_mode == M_INSERT || vn->vn_mode == M_PASTE,
		       "vs-8055: invalid mode or balance condition failed");

		tb->lnum[0] = vn->vn_nr_item;
		tb->lbytes = -1;
		return;
	}

	d_size = 0, ih_size = IH_SIZE;

	/* first item may be merge with last item in left neighbor */
	if (vi->vi_type & VI_TYPE_LEFT_MERGEABLE)
		d_size = -((int)IH_SIZE), ih_size = 0;

	tb->lnum[0] = 0;
	for (i = 0; i < vn->vn_nr_item;
	     i++, ih_size = IH_SIZE, d_size = 0, vi++) {
		d_size += vi->vi_item_len;
		if (cur_free >= d_size) {
			/* the item can be shifted entirely */
			cur_free -= d_size;
			tb->lnum[0]++;
			continue;
		}

		/* the item cannot be shifted entirely, try to split it */
		/* check whether L[0] can hold ih and at least one byte of the item body */
		if (cur_free <= ih_size) {
			/* cannot shift even a part of the current item */
			tb->lbytes = -1;
			return;
		}
		cur_free -= ih_size;

		tb->lbytes = op_check_left(vi, cur_free, 0, 0);
		if (tb->lbytes != -1)
			/* count partially shifted item */
			tb->lnum[0]++;

		break;
	}

	return;
}

/* using virtual node check, how many items can be shifted to right
   neighbor */
static void check_right(struct tree_balance *tb, int h, int cur_free)
{
	int i;
	struct virtual_node *vn = tb->tb_vn;
	struct virtual_item *vi;
	int d_size, ih_size;

	RFALSE(cur_free < 0, "vs-8070: cur_free < 0");

	/* internal level */
	if (h > 0) {
		tb->rnum[h] = cur_free / (DC_SIZE + KEY_SIZE);
		return;
	}

	/* leaf level */

	if (!cur_free || !vn->vn_nr_item) {
		/* no free space  */
		tb->rnum[h] = 0;
		tb->rbytes = -1;
		return;
	}

	RFALSE(!PATH_H_PPARENT(tb->tb_path, 0),
	       "vs-8075: parent does not exist or invalid");

	vi = vn->vn_vi + vn->vn_nr_item - 1;
	if ((unsigned int)cur_free >=
	    (vn->vn_size -
	     ((vi->vi_type & VI_TYPE_RIGHT_MERGEABLE) ? IH_SIZE : 0))) {
		/* all contents of S[0] fits into R[0] */

		RFALSE(vn->vn_mode == M_INSERT || vn->vn_mode == M_PASTE,
		       "vs-8080: invalid mode or balance condition failed");

		tb->rnum[h] = vn->vn_nr_item;
		tb->rbytes = -1;
		return;
	}

	d_size = 0, ih_size = IH_SIZE;

	/* last item may be merge with first item in right neighbor */
	if (vi->vi_type & VI_TYPE_RIGHT_MERGEABLE)
		d_size = -(int)IH_SIZE, ih_size = 0;

	tb->rnum[0] = 0;
	for (i = vn->vn_nr_item - 1; i >= 0;
	     i--, d_size = 0, ih_size = IH_SIZE, vi--) {
		d_size += vi->vi_item_len;
		if (cur_free >= d_size) {
			/* the item can be shifted entirely */
			cur_free -= d_size;
			tb->rnum[0]++;
			continue;
		}

		/* check whether R[0] can hold ih and at least one byte of the item body */
		if (cur_free <= ih_size) {	/* cannot shift even a part of the current item */
			tb->rbytes = -1;
			return;
		}

		/* R[0] can hold the header of the item and at least one byte of its body */
		cur_free -= ih_size;	/* cur_free is still > 0 */

		tb->rbytes = op_check_right(vi, cur_free);
		if (tb->rbytes != -1)
			/* count partially shifted item */
			tb->rnum[0]++;

		break;
	}

	return;
}

/*
 * from - number of items, which are shifted to left neighbor entirely
 * to - number of item, which are shifted to right neighbor entirely
 * from_bytes - number of bytes of boundary item (or directory entries) which are shifted to left neighbor
 * to_bytes - number of bytes of boundary item (or directory entries) which are shifted to right neighbor */
static int get_num_ver(int mode, struct tree_balance *tb, int h,
		       int from, int from_bytes,
		       int to, int to_bytes, short *snum012, int flow)
{
	int i;
	int cur_free;
	//    int bytes;
	int units;
	struct virtual_node *vn = tb->tb_vn;
	//    struct virtual_item * vi;

	int total_node_size, max_node_size, current_item_size;
	int needed_nodes;
	int start_item,		/* position of item we start filling node from */
	 end_item,		/* position of item we finish filling node by */
	 start_bytes,		/* number of first bytes (entries for directory) of start_item-th item 
				   we do not include into node that is being filled */
	 end_bytes;		/* number of last bytes (entries for directory) of end_item-th item 
				   we do node include into node that is being filled */
	int split_item_positions[2];	/* these are positions in virtual item of
					   items, that are split between S[0] and
					   S1new and S1new and S2new */

	split_item_positions[0] = -1;
	split_item_positions[1] = -1;

	/* We only create additional nodes if we are in insert or paste mode
	   or we are in replace mode at the internal level. If h is 0 and
	   the mode is M_REPLACE then in fix_nodes we change the mode to
	   paste or insert before we get here in the code.  */
	RFALSE(tb->insert_size[h] < 0 || (mode != M_INSERT && mode != M_PASTE),
	       "vs-8100: insert_size < 0 in overflow");

	max_node_size = MAX_CHILD_SIZE(PATH_H_PBUFFER(tb->tb_path, h));

	/* snum012 [0-2] - number of items, that lay
	   to S[0], first new node and second new node */
	snum012[3] = -1;	/* s1bytes */
	snum012[4] = -1;	/* s2bytes */

	/* internal level */
	if (h > 0) {
		i = ((to - from) * (KEY_SIZE + DC_SIZE) + DC_SIZE);
		if (i == max_node_size)
			return 1;
		return (i / max_node_size + 1);
	}

	/* leaf level */
	needed_nodes = 1;
	total_node_size = 0;
	cur_free = max_node_size;

	// start from 'from'-th item
	start_item = from;
	// skip its first 'start_bytes' units
	start_bytes = ((from_bytes != -1) ? from_bytes : 0);

	// last included item is the 'end_item'-th one
	end_item = vn->vn_nr_item - to - 1;
	// do not count last 'end_bytes' units of 'end_item'-th item
	end_bytes = (to_bytes != -1) ? to_bytes : 0;

	/* go through all item beginning from the start_item-th item and ending by
	   the end_item-th item. Do not count first 'start_bytes' units of
	   'start_item'-th item and last 'end_bytes' of 'end_item'-th item */

	for (i = start_item; i <= end_item; i++) {
		struct virtual_item *vi = vn->vn_vi + i;
		int skip_from_end = ((i == end_item) ? end_bytes : 0);

		RFALSE(needed_nodes > 3, "vs-8105: too many nodes are needed");

		/* get size of current item */
		current_item_size = vi->vi_item_len;

		/* do not take in calculation head part (from_bytes) of from-th item */
		current_item_size -=
		    op_part_size(vi, 0 /*from start */ , start_bytes);

		/* do not take in calculation tail part of last item */
		current_item_size -=
		    op_part_size(vi, 1 /*from end */ , skip_from_end);

		/* if item fits into current node entierly */
		if (total_node_size + current_item_size <= max_node_size) {
			snum012[needed_nodes - 1]++;
			total_node_size += current_item_size;
			start_bytes = 0;
			continue;
		}

		if (current_item_size > max_node_size) {
			/* virtual item length is longer, than max size of item in
			   a node. It is impossible for direct item */
			RFALSE(is_direct_le_ih(vi->vi_ih),
			       "vs-8110: "
			       "direct item length is %d. It can not be longer than %d",
			       current_item_size, max_node_size);
			/* we will try to split it */
			flow = 1;
		}

		if (!flow) {
			/* as we do not split items, take new node and continue */
			needed_nodes++;
			i--;
			total_node_size = 0;
			continue;
		}
		// calculate number of item units which fit into node being
		// filled
		{
			int free_space;

			free_space = max_node_size - total_node_size - IH_SIZE;
			units =
			    op_check_left(vi, free_space, start_bytes,
					  skip_from_end);
			if (units == -1) {
				/* nothing fits into current node, take new node and continue */
				needed_nodes++, i--, total_node_size = 0;
				continue;
			}
		}

		/* something fits into the current node */
		//if (snum012[3] != -1 || needed_nodes != 1)
		//  reiserfs_panic (tb->tb_sb, "vs-8115: get_num_ver: too many nodes required");
		//snum012[needed_nodes - 1 + 3] = op_unit_num (vi) - start_bytes - units;
		start_bytes += units;
		snum012[needed_nodes - 1 + 3] = units;

		if (needed_nodes > 2)
			reiserfs_warning(tb->tb_sb, "vs-8111: get_num_ver: "
					 "split_item_position is out of boundary");
		snum012[needed_nodes - 1]++;
		split_item_positions[needed_nodes - 1] = i;
		needed_nodes++;
		/* continue from the same item with start_bytes != -1 */
		start_item = i;
		i--;
		total_node_size = 0;
	}

	// sum012[4] (if it is not -1) contains number of units of which
	// are to be in S1new, snum012[3] - to be in S0. They are supposed
	// to be S1bytes and S2bytes correspondingly, so recalculate
	if (snum012[4] > 0) {
		int split_item_num;
		int bytes_to_r, bytes_to_l;
		int bytes_to_S1new;

		split_item_num = split_item_positions[1];
		bytes_to_l =
		    ((from == split_item_num
		      && from_bytes != -1) ? from_bytes : 0);
		bytes_to_r =
		    ((end_item == split_item_num
		      && end_bytes != -1) ? end_bytes : 0);
		bytes_to_S1new =
		    ((split_item_positions[0] ==
		      split_item_positions[1]) ? snum012[3] : 0);

		// s2bytes
		snum012[4] =
		    op_unit_num(&vn->vn_vi[split_item_num]) - snum012[4] -
		    bytes_to_r - bytes_to_l - bytes_to_S1new;

		if (vn->vn_vi[split_item_num].vi_index != TYPE_DIRENTRY &&
		    vn->vn_vi[split_item_num].vi_index != TYPE_INDIRECT)
			reiserfs_warning(tb->tb_sb, "vs-8115: get_num_ver: not "
					 "directory or indirect item");
	}

	/* now we know S2bytes, calculate S1bytes */
	if (snum012[3] > 0) {
		int split_item_num;
		int bytes_to_r, bytes_to_l;
		int bytes_to_S2new;

		split_item_num = split_item_positions[0];
		bytes_to_l =
		    ((from == split_item_num
		      && from_bytes != -1) ? from_bytes : 0);
		bytes_to_r =
		    ((end_item == split_item_num
		      && end_bytes != -1) ? end_bytes : 0);
		bytes_to_S2new =
		    ((split_item_positions[0] == split_item_positions[1]
		      && snum012[4] != -1) ? snum012[4] : 0);

		// s1bytes
		snum012[3] =
		    op_unit_num(&vn->vn_vi[split_item_num]) - snum012[3] -
		    bytes_to_r - bytes_to_l - bytes_to_S2new;
	}

	return needed_nodes;
}

#ifdef CONFIG_REISERFS_CHECK
extern struct tree_balance *cur_tb;
#endif

/* Set parameters for balancing.
 * Performs write of results of analysis of balancing into structure tb,
 * where it will later be used by the functions that actually do the balancing. 
 * Parameters:
 *	tb	tree_balance structure;
 *	h	current level of the node;
 *	lnum	number of items from S[h] that must be shifted to L[h];
 *	rnum	number of items from S[h] that must be shifted to R[h];
 *	blk_num	number of blocks that S[h] will be splitted into;
 *	s012	number of items that fall into splitted nodes.
 *	lbytes	number of bytes which flow to the left neighbor from the item that is not
 *		not shifted entirely
 *	rbytes	number of bytes which flow to the right neighbor from the item that is not
 *		not shifted entirely
 *	s1bytes	number of bytes which flow to the first  new node when S[0] splits (this number is contained in s012 array)
 */

static void set_parameters(struct tree_balance *tb, int h, int lnum,
			   int rnum, int blk_num, short *s012, int lb, int rb)
{

	tb->lnum[h] = lnum;
	tb->rnum[h] = rnum;
	tb->blknum[h] = blk_num;

	if (h == 0) {		/* only for leaf level */
		if (s012 != NULL) {
			tb->s0num = *s012++,
			    tb->s1num = *s012++, tb->s2num = *s012++;
			tb->s1bytes = *s012++;
			tb->s2bytes = *s012;
		}
		tb->lbytes = lb;
		tb->rbytes = rb;
	}
	PROC_INFO_ADD(tb->tb_sb, lnum[h], lnum);
	PROC_INFO_ADD(tb->tb_sb, rnum[h], rnum);

	PROC_INFO_ADD(tb->tb_sb, lbytes[h], lb);
	PROC_INFO_ADD(tb->tb_sb, rbytes[h], rb);
}

/* check, does node disappear if we shift tb->lnum[0] items to left
   neighbor and tb->rnum[0] to the right one. */
static int is_leaf_removable(struct tree_balance *tb)
{
	struct virtual_node *vn = tb->tb_vn;
	int to_left, to_right;
	int size;
	int remain_items;

	/* number of items, that will be shifted to left (right) neighbor
	   entirely */
	to_left = tb->lnum[0] - ((tb->lbytes != -1) ? 1 : 0);
	to_right = tb->rnum[0] - ((tb->rbytes != -1) ? 1 : 0);
	remain_items = vn->vn_nr_item;

	/* how many items remain in S[0] after shiftings to neighbors */
	remain_items -= (to_left + to_right);

	if (remain_items < 1) {
		/* all content of node can be shifted to neighbors */
		set_parameters(tb, 0, to_left, vn->vn_nr_item - to_left, 0,
			       NULL, -1, -1);
		return 1;
	}

	if (remain_items > 1 || tb->lbytes == -1 || tb->rbytes == -1)
		/* S[0] is not removable */
		return 0;

	/* check, whether we can divide 1 remaining item between neighbors */

	/* get size of remaining item (in item units) */
	size = op_unit_num(&(vn->vn_vi[to_left]));

	if (tb->lbytes + tb->rbytes >= size) {
		set_parameters(tb, 0, to_left + 1, to_right + 1, 0, NULL,
			       tb->lbytes, -1);
		return 1;
	}

	return 0;
}

/* check whether L, S, R can be joined in one node */
static int are_leaves_removable(struct tree_balance *tb, int lfree, int rfree)
{
	struct virtual_node *vn = tb->tb_vn;
	int ih_size;
	struct buffer_head *S0;

	S0 = PATH_H_PBUFFER(tb->tb_path, 0);

	ih_size = 0;
	if (vn->vn_nr_item) {
		if (vn->vn_vi[0].vi_type & VI_TYPE_LEFT_MERGEABLE)
			ih_size += IH_SIZE;

		if (vn->vn_vi[vn->vn_nr_item - 1].
		    vi_type & VI_TYPE_RIGHT_MERGEABLE)
			ih_size += IH_SIZE;
	} else {
		/* there was only one item and it will be deleted */
		struct item_head *ih;

		RFALSE(B_NR_ITEMS(S0) != 1,
		       "vs-8125: item number must be 1: it is %d",
		       B_NR_ITEMS(S0));

		ih = B_N_PITEM_HEAD(S0, 0);
		if (tb->CFR[0]
		    && !comp_short_le_keys(&(ih->ih_key),
					   B_N_PDELIM_KEY(tb->CFR[0],
							  tb->rkey[0])))
			if (is_direntry_le_ih(ih)) {
				/* Directory must be in correct state here: that is
				   somewhere at the left side should exist first directory
				   item. But the item being deleted can not be that first
				   one because its right neighbor is item of the same
				   directory. (But first item always gets deleted in last
				   turn). So, neighbors of deleted item can be merged, so
				   we can save ih_size */
				ih_size = IH_SIZE;

				/* we might check that left neighbor exists and is of the
				   same directory */
				RFALSE(le_ih_k_offset(ih) == DOT_OFFSET,
				       "vs-8130: first directory item can not be removed until directory is not empty");
			}

	}

	if (MAX_CHILD_SIZE(S0) + vn->vn_size <= rfree + lfree + ih_size) {
		set_parameters(tb, 0, -1, -1, -1, NULL, -1, -1);
		PROC_INFO_INC(tb->tb_sb, leaves_removable);
		return 1;
	}
	return 0;

}

/* when we do not split item, lnum and rnum are numbers of entire items */
#define SET_PAR_SHIFT_LEFT \
if (h)\
{\
   int to_l;\
   \
   to_l = (MAX_NR_KEY(Sh)+1 - lpar + vn->vn_nr_item + 1) / 2 -\
	      (MAX_NR_KEY(Sh) + 1 - lpar);\
	      \
	      set_parameters (tb, h, to_l, 0, lnver, NULL, -1, -1);\
}\
else \
{\
   if (lset==LEFT_SHIFT_FLOW)\
     set_parameters (tb, h, lpar, 0, lnver, snum012+lset,\
		     tb->lbytes, -1);\
   else\
     set_parameters (tb, h, lpar - (tb->lbytes!=-1), 0, lnver, snum012+lset,\
		     -1, -1);\
}

#define SET_PAR_SHIFT_RIGHT \
if (h)\
{\
   int to_r;\
   \
   to_r = (MAX_NR_KEY(Sh)+1 - rpar + vn->vn_nr_item + 1) / 2 - (MAX_NR_KEY(Sh) + 1 - rpar);\
   \
   set_parameters (tb, h, 0, to_r, rnver, NULL, -1, -1);\
}\
else \
{\
   if (rset==RIGHT_SHIFT_FLOW)\
     set_parameters (tb, h, 0, rpar, rnver, snum012+rset,\
		  -1, tb->rbytes);\
   else\
     set_parameters (tb, h, 0, rpar - (tb->rbytes!=-1), rnver, snum012+rset,\
		  -1, -1);\
}

static void free_buffers_in_tb(struct tree_balance *p_s_tb)
{
	int n_counter;

	decrement_counters_in_path(p_s_tb->tb_path);

	for (n_counter = 0; n_counter < MAX_HEIGHT; n_counter++) {
		decrement_bcount(p_s_tb->L[n_counter]);
		p_s_tb->L[n_counter] = NULL;
		decrement_bcount(p_s_tb->R[n_counter]);
		p_s_tb->R[n_counter] = NULL;
		decrement_bcount(p_s_tb->FL[n_counter]);
		p_s_tb->FL[n_counter] = NULL;
		decrement_bcount(p_s_tb->FR[n_counter]);
		p_s_tb->FR[n_counter] = NULL;
		decrement_bcount(p_s_tb->CFL[n_counter]);
		p_s_tb->CFL[n_counter] = NULL;
		decrement_bcount(p_s_tb->CFR[n_counter]);
		p_s_tb->CFR[n_counter] = NULL;
	}
}

/* Get new buffers for storing new nodes that are created while balancing.
 * Returns:	SCHEDULE_OCCURRED - schedule occurred while the function worked;
 *	        CARRY_ON - schedule didn't occur while the function worked;
 *	        NO_DISK_SPACE - no disk space.
 */
/* The function is NOT SCHEDULE-SAFE! */
static int get_empty_nodes(struct tree_balance *p_s_tb, int n_h)
{
	struct buffer_head *p_s_new_bh,
	    *p_s_Sh = PATH_H_PBUFFER(p_s_tb->tb_path, n_h);
	b_blocknr_t *p_n_blocknr, a_n_blocknrs[MAX_AMOUNT_NEEDED] = { 0, };
	int n_counter, n_number_of_freeblk, n_amount_needed,	/* number of needed empty blocks */
	 n_retval = CARRY_ON;
	struct super_block *p_s_sb = p_s_tb->tb_sb;

	/* number_of_freeblk is the number of empty blocks which have been
	   acquired for use by the balancing algorithm minus the number of
	   empty blocks used in the previous levels of the analysis,
	   number_of_freeblk = tb->cur_blknum can be non-zero if a schedule occurs
	   after empty blocks are acquired, and the balancing analysis is
	   then restarted, amount_needed is the number needed by this level
	   (n_h) of the balancing analysis.

	   Note that for systems with many processes writing, it would be
	   more layout optimal to calculate the total number needed by all
	   levels and then to run reiserfs_new_blocks to get all of them at once.  */

	/* Initiate number_of_freeblk to the amount acquired prior to the restart of
	   the analysis or 0 if not restarted, then subtract the amount needed
	   by all of the levels of the tree below n_h. */
	/* blknum includes S[n_h], so we subtract 1 in this calculation */
	for (n_counter = 0, n_number_of_freeblk = p_s_tb->cur_blknum;
	     n_counter < n_h; n_counter++)
		n_number_of_freeblk -=
		    (p_s_tb->blknum[n_counter]) ? (p_s_tb->blknum[n_counter] -
						   1) : 0;

	/* Allocate missing empty blocks. */
	/* if p_s_Sh == 0  then we are getting a new root */
	n_amount_needed = (p_s_Sh) ? (p_s_tb->blknum[n_h] - 1) : 1;
	/*  Amount_needed = the amount that we need more than the amount that we have. */
	if (n_amount_needed > n_number_of_freeblk)
		n_amount_needed -= n_number_of_freeblk;
	else			/* If we have enough already then there is nothing to do. */
		return CARRY_ON;

	/* No need to check quota - is not allocated for blocks used for formatted nodes */
	if (reiserfs_new_form_blocknrs(p_s_tb, a_n_blocknrs,
				       n_amount_needed) == NO_DISK_SPACE)
		return NO_DISK_SPACE;

	/* for each blocknumber we just got, get a buffer and stick it on FEB */
	for (p_n_blocknr = a_n_blocknrs, n_counter = 0;
	     n_counter < n_amount_needed; p_n_blocknr++, n_counter++) {

		RFALSE(!*p_n_blocknr,
		       "PAP-8135: reiserfs_new_blocknrs failed when got new blocks");

		p_s_new_bh = sb_getblk(p_s_sb, *p_n_blocknr);
		RFALSE(buffer_dirty(p_s_new_bh) ||
		       buffer_journaled(p_s_new_bh) ||
		       buffer_journal_dirty(p_s_new_bh),
		       "PAP-8140: journlaled or dirty buffer %b for the new block",
		       p_s_new_bh);

		/* Put empty buffers into the array. */
		RFALSE(p_s_tb->FEB[p_s_tb->cur_blknum],
		       "PAP-8141: busy slot for new buffer");

		set_buffer_journal_new(p_s_new_bh);
		p_s_tb->FEB[p_s_tb->cur_blknum++] = p_s_new_bh;
	}

	if (n_retval == CARRY_ON && FILESYSTEM_CHANGED_TB(p_s_tb))
		n_retval = REPEAT_SEARCH;

	return n_retval;
}

/* Get free space of the left neighbor, which is stored in the parent
 * node of the left neighbor.  */
static int get_lfree(struct tree_balance *tb, int h)
{
	struct buffer_head *l, *f;
	int order;

	if ((f = PATH_H_PPARENT(tb->tb_path, h)) == 0 || (l = tb->FL[h]) == 0)
		return 0;

	if (f == l)
		order = PATH_H_B_ITEM_ORDER(tb->tb_path, h) - 1;
	else {
		order = B_NR_ITEMS(l);
		f = l;
	}

	return (MAX_CHILD_SIZE(f) - dc_size(B_N_CHILD(f, order)));
}

/* Get free space of the right neighbor,
 * which is stored in the parent node of the right neighbor.
 */
static int get_rfree(struct tree_balance *tb, int h)
{
	struct buffer_head *r, *f;
	int order;

	if ((f = PATH_H_PPARENT(tb->tb_path, h)) == 0 || (r = tb->FR[h]) == 0)
		return 0;

	if (f == r)
		order = PATH_H_B_ITEM_ORDER(tb->tb_path, h) + 1;
	else {
		order = 0;
		f = r;
	}

	return (MAX_CHILD_SIZE(f) - dc_size(B_N_CHILD(f, order)));

}

/* Check whether left neighbor is in memory. */
static int is_left_neighbor_in_cache(struct tree_balance *p_s_tb, int n_h)
{
	struct buffer_head *p_s_father, *left;
	struct super_block *p_s_sb = p_s_tb->tb_sb;
	b_blocknr_t n_left_neighbor_blocknr;
	int n_left_neighbor_position;

	if (!p_s_tb->FL[n_h])	/* Father of the left neighbor does not exist. */
		return 0;

	/* Calculate father of the node to be balanced. */
	p_s_father = PATH_H_PBUFFER(p_s_tb->tb_path, n_h + 1);

	RFALSE(!p_s_father ||
	       !B_IS_IN_TREE(p_s_father) ||
	       !B_IS_IN_TREE(p_s_tb->FL[n_h]) ||
	       !buffer_uptodate(p_s_father) ||
	       !buffer_uptodate(p_s_tb->FL[n_h]),
	       "vs-8165: F[h] (%b) or FL[h] (%b) is invalid",
	       p_s_father, p_s_tb->FL[n_h]);

	/* Get position of the pointer to the left neighbor into the left father. */
	n_left_neighbor_position = (p_s_father == p_s_tb->FL[n_h]) ?
	    p_s_tb->lkey[n_h] : B_NR_ITEMS(p_s_tb->FL[n_h]);
	/* Get left neighbor block number. */
	n_left_neighbor_blocknr =
	    B_N_CHILD_NUM(p_s_tb->FL[n_h], n_left_neighbor_position);
	/* Look for the left neighbor in the cache. */
	if ((left = sb_find_get_block(p_s_sb, n_left_neighbor_blocknr))) {

		RFALSE(buffer_uptodate(left) && !B_IS_IN_TREE(left),
		       "vs-8170: left neighbor (%b %z) is not in the tree",
		       left, left);
		put_bh(left);
		return 1;
	}

	return 0;
}

#define LEFT_PARENTS  'l'
#define RIGHT_PARENTS 'r'

static void decrement_key(struct cpu_key *p_s_key)
{
	// call item specific function for this key
	item_ops[cpu_key_k_type(p_s_key)]->decrement_key(p_s_key);
}

/* Calculate far left/right parent of the left/right neighbor of the current node, that
 * is calculate the left/right (FL[h]/FR[h]) neighbor of the parent F[h].
 * Calculate left/right common parent of the current node and L[h]/R[h].
 * Calculate left/right delimiting key position.
 * Returns:	PATH_INCORRECT   - path in the tree is not correct;
 		SCHEDULE_OCCURRED - schedule occurred while the function worked;
 *	        CARRY_ON         - schedule didn't occur while the function worked;
 */
static int get_far_parent(struct tree_balance *p_s_tb,
			  int n_h,
			  struct buffer_head **pp_s_father,
			  struct buffer_head **pp_s_com_father, char c_lr_par)
{
	struct buffer_head *p_s_parent;
	INITIALIZE_PATH(s_path_to_neighbor_father);
	struct path *p_s_path = p_s_tb->tb_path;
	struct cpu_key s_lr_father_key;
	int n_counter,
	    n_position = INT_MAX,
	    n_first_last_position = 0,
	    n_path_offset = PATH_H_PATH_OFFSET(p_s_path, n_h);

	/* Starting from F[n_h] go upwards in the tree, and look for the common
	   ancestor of F[n_h], and its neighbor l/r, that should be obtained. */

	n_counter = n_path_offset;

	RFALSE(n_counter < FIRST_PATH_ELEMENT_OFFSET,
	       "PAP-8180: invalid path length");

	for (; n_counter > FIRST_PATH_ELEMENT_OFFSET; n_counter--) {
		/* Check whether parent of the current buffer in the path is really parent in the tree. */
		if (!B_IS_IN_TREE
		    (p_s_parent = PATH_OFFSET_PBUFFER(p_s_path, n_counter - 1)))
			return REPEAT_SEARCH;
		/* Check whether position in the parent is correct. */
		if ((n_position =
		     PATH_OFFSET_POSITION(p_s_path,
					  n_counter - 1)) >
		    B_NR_ITEMS(p_s_parent))
			return REPEAT_SEARCH;
		/* Check whether parent at the path really points to the child. */
		if (B_N_CHILD_NUM(p_s_parent, n_position) !=
		    PATH_OFFSET_PBUFFER(p_s_path, n_counter)->b_blocknr)
			return REPEAT_SEARCH;
		/* Return delimiting key if position in the parent is not equal to first/last one. */
		if (c_lr_par == RIGHT_PARENTS)
			n_first_last_position = B_NR_ITEMS(p_s_parent);
		if (n_position != n_first_last_position) {
			*pp_s_com_father = p_s_parent;
			get_bh(*pp_s_com_father);
			/*(*pp_s_com_father = p_s_parent)->b_count++; */
			break;
		}
	}

	/* if we are in the root of the tree, then there is no common father */
	if (n_counter == FIRST_PATH_ELEMENT_OFFSET) {
		/* Check whether first buffer in the path is the root of the tree. */
		if (PATH_OFFSET_PBUFFER
		    (p_s_tb->tb_path,
		     FIRST_PATH_ELEMENT_OFFSET)->b_blocknr ==
		    SB_ROOT_BLOCK(p_s_tb->tb_sb)) {
			*pp_s_father = *pp_s_com_father = NULL;
			return CARRY_ON;
		}
		return REPEAT_SEARCH;
	}

	RFALSE(B_LEVEL(*pp_s_com_father) <= DISK_LEAF_NODE_LEVEL,
	       "PAP-8185: (%b %z) level too small",
	       *pp_s_com_father, *pp_s_com_father);

	/* Check whether the common parent is locked. */

	if (buffer_locked(*pp_s_com_father)) {
		__wait_on_buffer(*pp_s_com_father);
		if (FILESYSTEM_CHANGED_TB(p_s_tb)) {
			decrement_bcount(*pp_s_com_father);
			return REPEAT_SEARCH;
		}
	}

	/* So, we got common parent of the current node and its left/right neighbor.
	   Now we are geting the parent of the left/right neighbor. */

	/* Form key to get parent of the left/right neighbor. */
	le_key2cpu_key(&s_lr_father_key,
		       B_N_PDELIM_KEY(*pp_s_com_father,
				      (c_lr_par ==
				       LEFT_PARENTS) ? (p_s_tb->lkey[n_h - 1] =
							n_position -
							1) : (p_s_tb->rkey[n_h -
									   1] =
							      n_position)));

	if (c_lr_par == LEFT_PARENTS)
		decrement_key(&s_lr_father_key);

	if (search_by_key
	    (p_s_tb->tb_sb, &s_lr_father_key, &s_path_to_neighbor_father,
	     n_h + 1) == IO_ERROR)
		// path is released
		return IO_ERROR;

	if (FILESYSTEM_CHANGED_TB(p_s_tb)) {
		decrement_counters_in_path(&s_path_to_neighbor_father);
		decrement_bcount(*pp_s_com_father);
		return REPEAT_SEARCH;
	}

	*pp_s_father = PATH_PLAST_BUFFER(&s_path_to_neighbor_father);

	RFALSE(B_LEVEL(*pp_s_father) != n_h + 1,
	       "PAP-8190: (%b %z) level too small", *pp_s_father, *pp_s_father);
	RFALSE(s_path_to_neighbor_father.path_length <
	       FIRST_PATH_ELEMENT_OFFSET, "PAP-8192: path length is too small");

	s_path_to_neighbor_father.path_length--;
	decrement_counters_in_path(&s_path_to_neighbor_father);
	return CARRY_ON;
}

/* Get parents of neighbors of node in the path(S[n_path_offset]) and common parents of
 * S[n_path_offset] and L[n_path_offset]/R[n_path_offset]: F[n_path_offset], FL[n_path_offset],
 * FR[n_path_offset], CFL[n_path_offset], CFR[n_path_offset].
 * Calculate numbers of left and right delimiting keys position: lkey[n_path_offset], rkey[n_path_offset].
 * Returns:	SCHEDULE_OCCURRED - schedule occurred while the function worked;
 *	        CARRY_ON - schedule didn't occur while the function worked;
 */
static int get_parents(struct tree_balance *p_s_tb, int n_h)
{
	struct path *p_s_path = p_s_tb->tb_path;
	int n_position,
	    n_ret_value,
	    n_path_offset = PATH_H_PATH_OFFSET(p_s_tb->tb_path, n_h);
	struct buffer_head *p_s_curf, *p_s_curcf;

	/* Current node is the root of the tree or will be root of the tree */
	if (n_path_offset <= FIRST_PATH_ELEMENT_OFFSET) {
		/* The root can not have parents.
		   Release nodes which previously were obtained as parents of the current node neighbors. */
		decrement_bcount(p_s_tb->FL[n_h]);
		decrement_bcount(p_s_tb->CFL[n_h]);
		decrement_bcount(p_s_tb->FR[n_h]);
		decrement_bcount(p_s_tb->CFR[n_h]);
		p_s_tb->FL[n_h] = p_s_tb->CFL[n_h] = p_s_tb->FR[n_h] =
		    p_s_tb->CFR[n_h] = NULL;
		return CARRY_ON;
	}

	/* Get parent FL[n_path_offset] of L[n_path_offset]. */
	if ((n_position = PATH_OFFSET_POSITION(p_s_path, n_path_offset - 1))) {
		/* Current node is not the first child of its parent. */
		/*(p_s_curf = p_s_curcf = PATH_OFFSET_PBUFFER(p_s_path, n_path_offset - 1))->b_count += 2; */
		p_s_curf = p_s_curcf =
		    PATH_OFFSET_PBUFFER(p_s_path, n_path_offset - 1);
		get_bh(p_s_curf);
		get_bh(p_s_curf);
		p_s_tb->lkey[n_h] = n_position - 1;
	} else {
		/* Calculate current parent of L[n_path_offset], which is the left neighbor of the current node.
		   Calculate current common parent of L[n_path_offset] and the current node. Note that
		   CFL[n_path_offset] not equal FL[n_path_offset] and CFL[n_path_offset] not equal F[n_path_offset].
		   Calculate lkey[n_path_offset]. */
		if ((n_ret_value = get_far_parent(p_s_tb, n_h + 1, &p_s_curf,
						  &p_s_curcf,
						  LEFT_PARENTS)) != CARRY_ON)
			return n_ret_value;
	}

	decrement_bcount(p_s_tb->FL[n_h]);
	p_s_tb->FL[n_h] = p_s_curf;	/* New initialization of FL[n_h]. */
	decrement_bcount(p_s_tb->CFL[n_h]);
	p_s_tb->CFL[n_h] = p_s_curcf;	/* New initialization of CFL[n_h]. */

	RFALSE((p_s_curf && !B_IS_IN_TREE(p_s_curf)) ||
	       (p_s_curcf && !B_IS_IN_TREE(p_s_curcf)),
	       "PAP-8195: FL (%b) or CFL (%b) is invalid", p_s_curf, p_s_curcf);

/* Get parent FR[n_h] of R[n_h]. */

/* Current node is the last child of F[n_h]. FR[n_h] != F[n_h]. */
	if (n_position == B_NR_ITEMS(PATH_H_PBUFFER(p_s_path, n_h + 1))) {
/* Calculate current parent of R[n_h], which is the right neighbor of F[n_h].
   Calculate current common parent of R[n_h] and current node. Note that CFR[n_h]
   not equal FR[n_path_offset] and CFR[n_h] not equal F[n_h]. */
		if ((n_ret_value =
		     get_far_parent(p_s_tb, n_h + 1, &p_s_curf, &p_s_curcf,
				    RIGHT_PARENTS)) != CARRY_ON)
			return n_ret_value;
	} else {
/* Current node is not the last child of its parent F[n_h]. */
		/*(p_s_curf = p_s_curcf = PATH_OFFSET_PBUFFER(p_s_path, n_path_offset - 1))->b_count += 2; */
		p_s_curf = p_s_curcf =
		    PATH_OFFSET_PBUFFER(p_s_path, n_path_offset - 1);
		get_bh(p_s_curf);
		get_bh(p_s_curf);
		p_s_tb->rkey[n_h] = n_position;
	}

	decrement_bcount(p_s_tb->FR[n_h]);
	p_s_tb->FR[n_h] = p_s_curf;	/* New initialization of FR[n_path_offset]. */

	decrement_bcount(p_s_tb->CFR[n_h]);
	p_s_tb->CFR[n_h] = p_s_curcf;	/* New initialization of CFR[n_path_offset]. */

	RFALSE((p_s_curf && !B_IS_IN_TREE(p_s_curf)) ||
	       (p_s_curcf && !B_IS_IN_TREE(p_s_curcf)),
	       "PAP-8205: FR (%b) or CFR (%b) is invalid", p_s_curf, p_s_curcf);

	return CARRY_ON;
}

/* it is possible to remove node as result of shiftings to
   neighbors even when we insert or paste item. */
static inline int can_node_be_removed(int mode, int lfree, int sfree, int rfree,
				      struct tree_balance *tb, int h)
{
	struct buffer_head *Sh = PATH_H_PBUFFER(tb->tb_path, h);
	int levbytes = tb->insert_size[h];
	struct item_head *ih;
	struct reiserfs_key *r_key = NULL;

	ih = B_N_PITEM_HEAD(Sh, 0);
	if (tb->CFR[h])
		r_key = B_N_PDELIM_KEY(tb->CFR[h], tb->rkey[h]);

	if (lfree + rfree + sfree < MAX_CHILD_SIZE(Sh) + levbytes
	    /* shifting may merge items which might save space */
	    -
	    ((!h
	      && op_is_left_mergeable(&(ih->ih_key), Sh->b_size)) ? IH_SIZE : 0)
	    -
	    ((!h && r_key
	      && op_is_left_mergeable(r_key, Sh->b_size)) ? IH_SIZE : 0)
	    + ((h) ? KEY_SIZE : 0)) {
		/* node can not be removed */
		if (sfree >= levbytes) {	/* new item fits into node S[h] without any shifting */
			if (!h)
				tb->s0num =
				    B_NR_ITEMS(Sh) +
				    ((mode == M_INSERT) ? 1 : 0);
			set_parameters(tb, h, 0, 0, 1, NULL, -1, -1);
			return NO_BALANCING_NEEDED;
		}
	}
	PROC_INFO_INC(tb->tb_sb, can_node_be_removed[h]);
	return !NO_BALANCING_NEEDED;
}

/* Check whether current node S[h] is balanced when increasing its size by
 * Inserting or Pasting.
 * Calculate parameters for balancing for current level h.
 * Parameters:
 *	tb	tree_balance structure;
 *	h	current level of the node;
 *	inum	item number in S[h];
 *	mode	i - insert, p - paste;
 * Returns:	1 - schedule occurred; 
 *	        0 - balancing for higher levels needed;
 *	       -1 - no balancing for higher levels needed;
 *	       -2 - no disk space.
 */
/* ip means Inserting or Pasting */
static int ip_check_balance(struct tree_balance *tb, int h)
{
	struct virtual_node *vn = tb->tb_vn;
	int levbytes,		/* Number of bytes that must be inserted into (value
				   is negative if bytes are deleted) buffer which
				   contains node being balanced.  The mnemonic is
				   that the attempted change in node space used level
				   is levbytes bytes. */
	 n_ret_value;

	int lfree, sfree, rfree /* free space in L, S and R */ ;

	/* nver is short for number of vertixes, and lnver is the number if
	   we shift to the left, rnver is the number if we shift to the
	   right, and lrnver is the number if we shift in both directions.
	   The goal is to minimize first the number of vertixes, and second,
	   the number of vertixes whose contents are changed by shifting,
	   and third the number of uncached vertixes whose contents are
	   changed by shifting and must be read from disk.  */
	int nver, lnver, rnver, lrnver;

	/* used at leaf level only, S0 = S[0] is the node being balanced,
	   sInum [ I = 0,1,2 ] is the number of items that will
	   remain in node SI after balancing.  S1 and S2 are new
	   nodes that might be created. */

	/* we perform 8 calls to get_num_ver().  For each call we calculate five parameters.
	   where 4th parameter is s1bytes and 5th - s2bytes
	 */
	short snum012[40] = { 0, };	/* s0num, s1num, s2num for 8 cases 
					   0,1 - do not shift and do not shift but bottle
					   2 - shift only whole item to left
					   3 - shift to left and bottle as much as possible
					   4,5 - shift to right (whole items and as much as possible
					   6,7 - shift to both directions (whole items and as much as possible)
					 */

	/* Sh is the node whose balance is currently being checked */
	struct buffer_head *Sh;

	Sh = PATH_H_PBUFFER(tb->tb_path, h);
	levbytes = tb->insert_size[h];

	/* Calculate balance parameters for creating new root. */
	if (!Sh) {
		if (!h)
			reiserfs_panic(tb->tb_sb,
				       "vs-8210: ip_check_balance: S[0] can not be 0");
		switch (n_ret_value = get_empty_nodes(tb, h)) {
		case CARRY_ON:
			set_parameters(tb, h, 0, 0, 1, NULL, -1, -1);
			return NO_BALANCING_NEEDED;	/* no balancing for higher levels needed */

		case NO_DISK_SPACE:
		case REPEAT_SEARCH:
			return n_ret_value;
		default:
			reiserfs_panic(tb->tb_sb,
				       "vs-8215: ip_check_balance: incorrect return value of get_empty_nodes");
		}
	}

	if ((n_ret_value = get_parents(tb, h)) != CARRY_ON)	/* get parents of S[h] neighbors. */
		return n_ret_value;

	sfree = B_FREE_SPACE(Sh);

	/* get free space of neighbors */
	rfree = get_rfree(tb, h);
	lfree = get_lfree(tb, h);

	if (can_node_be_removed(vn->vn_mode, lfree, sfree, rfree, tb, h) ==
	    NO_BALANCING_NEEDED)
		/* and new item fits into node S[h] without any shifting */
		return NO_BALANCING_NEEDED;

	create_virtual_node(tb, h);

	/*  
	   determine maximal number of items we can shift to the left neighbor (in tb structure)
	   and the maximal number of bytes that can flow to the left neighbor
	   from the left most liquid item that cannot be shifted from S[0] entirely (returned value)
	 */
	check_left(tb, h, lfree);

	/*
	   determine maximal number of items we can shift to the right neighbor (in tb structure)
	   and the maximal number of bytes that can flow to the right neighbor
	   from the right most liquid item that cannot be shifted from S[0] entirely (returned value)
	 */
	check_right(tb, h, rfree);

	/* all contents of internal node S[h] can be moved into its
	   neighbors, S[h] will be removed after balancing */
	if (h && (tb->rnum[h] + tb->lnum[h] >= vn->vn_nr_item + 1)) {
		int to_r;

		/* Since we are working on internal nodes, and our internal
		   nodes have fixed size entries, then we can balance by the
		   number of items rather than the space they consume.  In this
		   routine we set the left node equal to the right node,
		   allowing a difference of less than or equal to 1 child
		   pointer. */
		to_r =
		    ((MAX_NR_KEY(Sh) << 1) + 2 - tb->lnum[h] - tb->rnum[h] +
		     vn->vn_nr_item + 1) / 2 - (MAX_NR_KEY(Sh) + 1 -
						tb->rnum[h]);
		set_parameters(tb, h, vn->vn_nr_item + 1 - to_r, to_r, 0, NULL,
			       -1, -1);
		return CARRY_ON;
	}

	/* this checks balance condition, that any two neighboring nodes can not fit in one node */
	RFALSE(h &&
	       (tb->lnum[h] >= vn->vn_nr_item + 1 ||
		tb->rnum[h] >= vn->vn_nr_item + 1),
	       "vs-8220: tree is not balanced on internal level");
	RFALSE(!h && ((tb->lnum[h] >= vn->vn_nr_item && (tb->lbytes == -1)) ||
		      (tb->rnum[h] >= vn->vn_nr_item && (tb->rbytes == -1))),
	       "vs-8225: tree is not balanced on leaf level");

	/* all contents of S[0] can be moved into its neighbors
	   S[0] will be removed after balancing. */
	if (!h && is_leaf_removable(tb))
		return CARRY_ON;

	/* why do we perform this check here rather than earlier??
	   Answer: we can win 1 node in some cases above. Moreover we
	   checked it above, when we checked, that S[0] is not removable
	   in principle */
	if (sfree >= levbytes) {	/* new item fits into node S[h] without any shifting */
		if (!h)
			tb->s0num = vn->vn_nr_item;
		set_parameters(tb, h, 0, 0, 1, NULL, -1, -1);
		return NO_BALANCING_NEEDED;
	}

	{
		int lpar, rpar, nset, lset, rset, lrset;
		/* 
		 * regular overflowing of the node
		 */

		/* get_num_ver works in 2 modes (FLOW & NO_FLOW) 
		   lpar, rpar - number of items we can shift to left/right neighbor (including splitting item)
		   nset, lset, rset, lrset - shows, whether flowing items give better packing 
		 */
#define FLOW 1
#define NO_FLOW 0		/* do not any splitting */

		/* we choose one the following */
#define NOTHING_SHIFT_NO_FLOW	0
#define NOTHING_SHIFT_FLOW	5
#define LEFT_SHIFT_NO_FLOW	10
#define LEFT_SHIFT_FLOW		15
#define RIGHT_SHIFT_NO_FLOW	20
#define RIGHT_SHIFT_FLOW	25
#define LR_SHIFT_NO_FLOW	30
#define LR_SHIFT_FLOW		35

		lpar = tb->lnum[h];
		rpar = tb->rnum[h];

		/* calculate number of blocks S[h] must be split into when
		   nothing is shifted to the neighbors,
		   as well as number of items in each part of the split node (s012 numbers),
		   and number of bytes (s1bytes) of the shared drop which flow to S1 if any */
		nset = NOTHING_SHIFT_NO_FLOW;
		nver = get_num_ver(vn->vn_mode, tb, h,
				   0, -1, h ? vn->vn_nr_item : 0, -1,
				   snum012, NO_FLOW);

		if (!h) {
			int nver1;

			/* note, that in this case we try to bottle between S[0] and S1 (S1 - the first new node) */
			nver1 = get_num_ver(vn->vn_mode, tb, h,
					    0, -1, 0, -1,
					    snum012 + NOTHING_SHIFT_FLOW, FLOW);
			if (nver > nver1)
				nset = NOTHING_SHIFT_FLOW, nver = nver1;
		}

		/* calculate number of blocks S[h] must be split into when
		   l_shift_num first items and l_shift_bytes of the right most
		   liquid item to be shifted are shifted to the left neighbor,
		   as well as number of items in each part of the splitted node (s012 numbers),
		   and number of bytes (s1bytes) of the shared drop which flow to S1 if any
		 */
		lset = LEFT_SHIFT_NO_FLOW;
		lnver = get_num_ver(vn->vn_mode, tb, h,
				    lpar - ((h || tb->lbytes == -1) ? 0 : 1),
				    -1, h ? vn->vn_nr_item : 0, -1,
				    snum012 + LEFT_SHIFT_NO_FLOW, NO_FLOW);
		if (!h) {
			int lnver1;

			lnver1 = get_num_ver(vn->vn_mode, tb, h,
					     lpar -
					     ((tb->lbytes != -1) ? 1 : 0),
					     tb->lbytes, 0, -1,
					     snum012 + LEFT_SHIFT_FLOW, FLOW);
			if (lnver > lnver1)
				lset = LEFT_SHIFT_FLOW, lnver = lnver1;
		}

		/* calculate number of blocks S[h] must be split into when
		   r_shift_num first items and r_shift_bytes of the left most
		   liquid item to be shifted are shifted to the right neighbor,
		   as well as number of items in each part of the splitted node (s012 numbers),
		   and number of bytes (s1bytes) of the shared drop which flow to S1 if any
		 */
		rset = RIGHT_SHIFT_NO_FLOW;
		rnver = get_num_ver(vn->vn_mode, tb, h,
				    0, -1,
				    h ? (vn->vn_nr_item - rpar) : (rpar -
								   ((tb->
								     rbytes !=
								     -1) ? 1 :
								    0)), -1,
				    snum012 + RIGHT_SHIFT_NO_FLOW, NO_FLOW);
		if (!h) {
			int rnver1;

			rnver1 = get_num_ver(vn->vn_mode, tb, h,
					     0, -1,
					     (rpar -
					      ((tb->rbytes != -1) ? 1 : 0)),
					     tb->rbytes,
					     snum012 + RIGHT_SHIFT_FLOW, FLOW);

			if (rnver > rnver1)
				rset = RIGHT_SHIFT_FLOW, rnver = rnver1;
		}

		/* calculate number of blocks S[h] must be split into when
		   items are shifted in both directions,
		   as well as number of items in each part of the splitted node (s012 numbers),
		   and number of bytes (s1bytes) of the shared drop which flow to S1 if any
		 */
		lrset = LR_SHIFT_NO_FLOW;
		lrnver = get_num_ver(vn->vn_mode, tb, h,
				     lpar - ((h || tb->lbytes == -1) ? 0 : 1),
				     -1,
				     h ? (vn->vn_nr_item - rpar) : (rpar -
								    ((tb->
								      rbytes !=
								      -1) ? 1 :
								     0)), -1,
				     snum012 + LR_SHIFT_NO_FLOW, NO_FLOW);
		if (!h) {
			int lrnver1;

			lrnver1 = get_num_ver(vn->vn_mode, tb, h,
					      lpar -
					      ((tb->lbytes != -1) ? 1 : 0),
					      tb->lbytes,
					      (rpar -
					       ((tb->rbytes != -1) ? 1 : 0)),
					      tb->rbytes,
					      snum012 + LR_SHIFT_FLOW, FLOW);
			if (lrnver > lrnver1)
				lrset = LR_SHIFT_FLOW, lrnver = lrnver1;
		}

		/* Our general shifting strategy is:
		   1) to minimized number of new nodes;
		   2) to minimized number of neighbors involved in shifting;
		   3) to minimized number of disk reads; */

		/* we can win TWO or ONE nodes by shifting in both directions */
		if (lrnver < lnver && lrnver < rnver) {
			RFALSE(h &&
			       (tb->lnum[h] != 1 ||
				tb->rnum[h] != 1 ||
				lrnver != 1 || rnver != 2 || lnver != 2
				|| h != 1), "vs-8230: bad h");
			if (lrset == LR_SHIFT_FLOW)
				set_parameters(tb, h, tb->lnum[h], tb->rnum[h],
					       lrnver, snum012 + lrset,
					       tb->lbytes, tb->rbytes);
			else
				set_parameters(tb, h,
					       tb->lnum[h] -
					       ((tb->lbytes == -1) ? 0 : 1),
					       tb->rnum[h] -
					       ((tb->rbytes == -1) ? 0 : 1),
					       lrnver, snum012 + lrset, -1, -1);

			return CARRY_ON;
		}

		/* if shifting doesn't lead to better packing then don't shift */
		if (nver == lrnver) {
			set_parameters(tb, h, 0, 0, nver, snum012 + nset, -1,
				       -1);
			return CARRY_ON;
		}

		/* now we know that for better packing shifting in only one
		   direction either to the left or to the right is required */

		/*  if shifting to the left is better than shifting to the right */
		if (lnver < rnver) {
			SET_PAR_SHIFT_LEFT;
			return CARRY_ON;
		}

		/* if shifting to the right is better than shifting to the left */
		if (lnver > rnver) {
			SET_PAR_SHIFT_RIGHT;
			return CARRY_ON;
		}

		/* now shifting in either direction gives the same number
		   of nodes and we can make use of the cached neighbors */
		if (is_left_neighbor_in_cache(tb, h)) {
			SET_PAR_SHIFT_LEFT;
			return CARRY_ON;
		}

		/* shift to the right independently on whether the right neighbor in cache or not */
		SET_PAR_SHIFT_RIGHT;
		return CARRY_ON;
	}
}

/* Check whether current node S[h] is balanced when Decreasing its size by
 * Deleting or Cutting for INTERNAL node of S+tree.
 * Calculate parameters for balancing for current level h.
 * Parameters:
 *	tb	tree_balance structure;
 *	h	current level of the node;
 *	inum	item number in S[h];
 *	mode	i - insert, p - paste;
 * Returns:	1 - schedule occurred; 
 *	        0 - balancing for higher levels needed;
 *	       -1 - no balancing for higher levels needed;
 *	       -2 - no disk space.
 *
 * Note: Items of internal nodes have fixed size, so the balance condition for
 * the internal part of S+tree is as for the B-trees.
 */
static int dc_check_balance_internal(struct tree_balance *tb, int h)
{
	struct virtual_node *vn = tb->tb_vn;

	/* Sh is the node whose balance is currently being checked,
	   and Fh is its father.  */
	struct buffer_head *Sh, *Fh;
	int maxsize, n_ret_value;
	int lfree, rfree /* free space in L and R */ ;

	Sh = PATH_H_PBUFFER(tb->tb_path, h);
	Fh = PATH_H_PPARENT(tb->tb_path, h);

	maxsize = MAX_CHILD_SIZE(Sh);

/*   using tb->insert_size[h], which is negative in this case, create_virtual_node calculates: */
/*   new_nr_item = number of items node would have if operation is */
/* 	performed without balancing (new_nr_item); */
	create_virtual_node(tb, h);

	if (!Fh) {		/* S[h] is the root. */
		if (vn->vn_nr_item > 0) {
			set_parameters(tb, h, 0, 0, 1, NULL, -1, -1);
			return NO_BALANCING_NEEDED;	/* no balancing for higher levels needed */
		}
		/* new_nr_item == 0.
		 * Current root will be deleted resulting in
		 * decrementing the tree height. */
		set_parameters(tb, h, 0, 0, 0, NULL, -1, -1);
		return CARRY_ON;
	}

	if ((n_ret_value = get_parents(tb, h)) != CARRY_ON)
		return n_ret_value;

	/* get free space of neighbors */
	rfree = get_rfree(tb, h);
	lfree = get_lfree(tb, h);

	/* determine maximal number of items we can fit into neighbors */
	check_left(tb, h, lfree);
	check_right(tb, h, rfree);

	if (vn->vn_nr_item >= MIN_NR_KEY(Sh)) {	/* Balance condition for the internal node is valid.
						 * In this case we balance only if it leads to better packing. */
		if (vn->vn_nr_item == MIN_NR_KEY(Sh)) {	/* Here we join S[h] with one of its neighbors,
							 * which is impossible with greater values of new_nr_item. */
			if (tb->lnum[h] >= vn->vn_nr_item + 1) {
				/* All contents of S[h] can be moved to L[h]. */
				int n;
				int order_L;

				order_L =
				    ((n =
				      PATH_H_B_ITEM_ORDER(tb->tb_path,
							  h)) ==
				     0) ? B_NR_ITEMS(tb->FL[h]) : n - 1;
				n = dc_size(B_N_CHILD(tb->FL[h], order_L)) /
				    (DC_SIZE + KEY_SIZE);
				set_parameters(tb, h, -n - 1, 0, 0, NULL, -1,
					       -1);
				return CARRY_ON;
			}

			if (tb->rnum[h] >= vn->vn_nr_item + 1) {
				/* All contents of S[h] can be moved to R[h]. */
				int n;
				int order_R;

				order_R =
				    ((n =
				      PATH_H_B_ITEM_ORDER(tb->tb_path,
							  h)) ==
				     B_NR_ITEMS(Fh)) ? 0 : n + 1;
				n = dc_size(B_N_CHILD(tb->FR[h], order_R)) /
				    (DC_SIZE + KEY_SIZE);
				set_parameters(tb, h, 0, -n - 1, 0, NULL, -1,
					       -1);
				return CARRY_ON;
			}
		}

		if (tb->rnum[h] + tb->lnum[h] >= vn->vn_nr_item + 1) {
			/* All contents of S[h] can be moved to the neighbors (L[h] & R[h]). */
			int to_r;

			to_r =
			    ((MAX_NR_KEY(Sh) << 1) + 2 - tb->lnum[h] -
			     tb->rnum[h] + vn->vn_nr_item + 1) / 2 -
			    (MAX_NR_KEY(Sh) + 1 - tb->rnum[h]);
			set_parameters(tb, h, vn->vn_nr_item + 1 - to_r, to_r,
				       0, NULL, -1, -1);
			return CARRY_ON;
		}

		/* Balancing does not lead to better packing. */
		set_parameters(tb, h, 0, 0, 1, NULL, -1, -1);
		return NO_BALANCING_NEEDED;
	}

	/* Current node contain insufficient number of items. Balancing is required. */
	/* Check whether we can merge S[h] with left neighbor. */
	if (tb->lnum[h] >= vn->vn_nr_item + 1)
		if (is_left_neighbor_in_cache(tb, h)
		    || tb->rnum[h] < vn->vn_nr_item + 1 || !tb->FR[h]) {
			int n;
			int order_L;

			order_L =
			    ((n =
			      PATH_H_B_ITEM_ORDER(tb->tb_path,
						  h)) ==
			     0) ? B_NR_ITEMS(tb->FL[h]) : n - 1;
			n = dc_size(B_N_CHILD(tb->FL[h], order_L)) / (DC_SIZE +
								      KEY_SIZE);
			set_parameters(tb, h, -n - 1, 0, 0, NULL, -1, -1);
			return CARRY_ON;
		}

	/* Check whether we can merge S[h] with right neighbor. */
	if (tb->rnum[h] >= vn->vn_nr_item + 1) {
		int n;
		int order_R;

		order_R =
		    ((n =
		      PATH_H_B_ITEM_ORDER(tb->tb_path,
					  h)) == B_NR_ITEMS(Fh)) ? 0 : (n + 1);
		n = dc_size(B_N_CHILD(tb->FR[h], order_R)) / (DC_SIZE +
							      KEY_SIZE);
		set_parameters(tb, h, 0, -n - 1, 0, NULL, -1, -1);
		return CARRY_ON;
	}

	/* All contents of S[h] can be moved to the neighbors (L[h] & R[h]). */
	if (tb->rnum[h] + tb->lnum[h] >= vn->vn_nr_item + 1) {
		int to_r;

		to_r =
		    ((MAX_NR_KEY(Sh) << 1) + 2 - tb->lnum[h] - tb->rnum[h] +
		     vn->vn_nr_item + 1) / 2 - (MAX_NR_KEY(Sh) + 1 -
						tb->rnum[h]);
		set_parameters(tb, h, vn->vn_nr_item + 1 - to_r, to_r, 0, NULL,
			       -1, -1);
		return CARRY_ON;
	}

	/* For internal nodes try to borrow item from a neighbor */
	RFALSE(!tb->FL[h] && !tb->FR[h], "vs-8235: trying to borrow for root");

	/* Borrow one or two items from caching neighbor */
	if (is_left_neighbor_in_cache(tb, h) || !tb->FR[h]) {
		int from_l;

		from_l =
		    (MAX_NR_KEY(Sh) + 1 - tb->lnum[h] + vn->vn_nr_item +
		     1) / 2 - (vn->vn_nr_item + 1);
		set_parameters(tb, h, -from_l, 0, 1, NULL, -1, -1);
		return CARRY_ON;
	}

	set_parameters(tb, h, 0,
		       -((MAX_NR_KEY(Sh) + 1 - tb->rnum[h] + vn->vn_nr_item +
			  1) / 2 - (vn->vn_nr_item + 1)), 1, NULL, -1, -1);
	return CARRY_ON;
}

/* Check whether current node S[h] is balanced when Decreasing its size by
 * Deleting or Truncating for LEAF node of S+tree.
 * Calculate parameters for balancing for current level h.
 * Parameters:
 *	tb	tree_balance structure;
 *	h	current level of the node;
 *	inum	item number in S[h];
 *	mode	i - insert, p - paste;
 * Returns:	1 - schedule occurred; 
 *	        0 - balancing for higher levels needed;
 *	       -1 - no balancing for higher levels needed;
 *	       -2 - no disk space.
 */
static int dc_check_balance_leaf(struct tree_balance *tb, int h)
{
	struct virtual_node *vn = tb->tb_vn;

	/* Number of bytes that must be deleted from
	   (value is negative if bytes are deleted) buffer which
	   contains node being balanced.  The mnemonic is that the
	   attempted change in node space used level is levbytes bytes. */
	int levbytes;
	/* the maximal item size */
	int maxsize, n_ret_value;
	/* S0 is the node whose balance is currently being checked,
	   and F0 is its father.  */
	struct buffer_head *S0, *F0;
	int lfree, rfree /* free space in L and R */ ;

	S0 = PATH_H_PBUFFER(tb->tb_path, 0);
	F0 = PATH_H_PPARENT(tb->tb_path, 0);

	levbytes = tb->insert_size[h];

	maxsize = MAX_CHILD_SIZE(S0);	/* maximal possible size of an item */

	if (!F0) {		/* S[0] is the root now. */

		RFALSE(-levbytes >= maxsize - B_FREE_SPACE(S0),
		       "vs-8240: attempt to create empty buffer tree");

		set_parameters(tb, h, 0, 0, 1, NULL, -1, -1);
		return NO_BALANCING_NEEDED;
	}

	if ((n_ret_value = get_parents(tb, h)) != CARRY_ON)
		return n_ret_value;

	/* get free space of neighbors */
	rfree = get_rfree(tb, h);
	lfree = get_lfree(tb, h);

	create_virtual_node(tb, h);

	/* if 3 leaves can be merge to one, set parameters and return */
	if (are_leaves_removable(tb, lfree, rfree))
		return CARRY_ON;

	/* determine maximal number of items we can shift to the left/right  neighbor
	   and the maximal number of bytes that can flow to the left/right neighbor
	   from the left/right most liquid item that cannot be shifted from S[0] entirely
	 */
	check_left(tb, h, lfree);
	check_right(tb, h, rfree);

	/* check whether we can merge S with left neighbor. */
	if (tb->lnum[0] >= vn->vn_nr_item && tb->lbytes == -1)
		if (is_left_neighbor_in_cache(tb, h) || ((tb->rnum[0] - ((tb->rbytes == -1) ? 0 : 1)) < vn->vn_nr_item) ||	/* S can not be merged with R */
		    !tb->FR[h]) {

			RFALSE(!tb->FL[h],
			       "vs-8245: dc_check_balance_leaf: FL[h] must exist");

			/* set parameter to merge S[0] with its left neighbor */
			set_parameters(tb, h, -1, 0, 0, NULL, -1, -1);
			return CARRY_ON;
		}

	/* check whether we can merge S[0] with right neighbor. */
	if (tb->rnum[0] >= vn->vn_nr_item && tb->rbytes == -1) {
		set_parameters(tb, h, 0, -1, 0, NULL, -1, -1);
		return CARRY_ON;
	}

	/* All contents of S[0] can be moved to the neighbors (L[0] & R[0]). Set parameters and return */
	if (is_leaf_removable(tb))
		return CARRY_ON;

	/* Balancing is not required. */
	tb->s0num = vn->vn_nr_item;
	set_parameters(tb, h, 0, 0, 1, NULL, -1, -1);
	return NO_BALANCING_NEEDED;
}

/* Check whether current node S[h] is balanced when Decreasing its size by
 * Deleting or Cutting.
 * Calculate parameters for balancing for current level h.
 * Parameters:
 *	tb	tree_balance structure;
 *	h	current level of the node;
 *	inum	item number in S[h];
 *	mode	d - delete, c - cut.
 * Returns:	1 - schedule occurred; 
 *	        0 - balancing for higher levels needed;
 *	       -1 - no balancing for higher levels needed;
 *	       -2 - no disk space.
 */
static int dc_check_balance(struct tree_balance *tb, int h)
{
	RFALSE(!(PATH_H_PBUFFER(tb->tb_path, h)),
	       "vs-8250: S is not initialized");

	if (h)
		return dc_check_balance_internal(tb, h);
	else
		return dc_check_balance_leaf(tb, h);
}

/* Check whether current node S[h] is balanced.
 * Calculate parameters for balancing for current level h.
 * Parameters:
 *
 *	tb	tree_balance structure:
 *
 *              tb is a large structure that must be read about in the header file
 *              at the same time as this procedure if the reader is to successfully
 *              understand this procedure
 *
 *	h	current level of the node;
 *	inum	item number in S[h];
 *	mode	i - insert, p - paste, d - delete, c - cut.
 * Returns:	1 - schedule occurred; 
 *	        0 - balancing for higher levels needed;
 *	       -1 - no balancing for higher levels needed;
 *	       -2 - no disk space.
 */
static int check_balance(int mode,
			 struct tree_balance *tb,
			 int h,
			 int inum,
			 int pos_in_item,
			 struct item_head *ins_ih, const void *data)
{
	struct virtual_node *vn;

	vn = tb->tb_vn = (struct virtual_node *)(tb->vn_buf);
	vn->vn_free_ptr = (char *)(tb->tb_vn + 1);
	vn->vn_mode = mode;
	vn->vn_affected_item_num = inum;
	vn->vn_pos_in_item = pos_in_item;
	vn->vn_ins_ih = ins_ih;
	vn->vn_data = data;

	RFALSE(mode == M_INSERT && !vn->vn_ins_ih,
	       "vs-8255: ins_ih can not be 0 in insert mode");

	if (tb->insert_size[h] > 0)
		/* Calculate balance parameters when size of node is increasing. */
		return ip_check_balance(tb, h);

	/* Calculate balance parameters when  size of node is decreasing. */
	return dc_check_balance(tb, h);
}

/* Check whether parent at the path is the really parent of the current node.*/
static int get_direct_parent(struct tree_balance *p_s_tb, int n_h)
{
	struct buffer_head *p_s_bh;
	struct path *p_s_path = p_s_tb->tb_path;
	int n_position,
	    n_path_offset = PATH_H_PATH_OFFSET(p_s_tb->tb_path, n_h);

	/* We are in the root or in the new root. */
	if (n_path_offset <= FIRST_PATH_ELEMENT_OFFSET) {

		RFALSE(n_path_offset < FIRST_PATH_ELEMENT_OFFSET - 1,
		       "PAP-8260: invalid offset in the path");

		if (PATH_OFFSET_PBUFFER(p_s_path, FIRST_PATH_ELEMENT_OFFSET)->
		    b_blocknr == SB_ROOT_BLOCK(p_s_tb->tb_sb)) {
			/* Root is not changed. */
			PATH_OFFSET_PBUFFER(p_s_path, n_path_offset - 1) = NULL;
			PATH_OFFSET_POSITION(p_s_path, n_path_offset - 1) = 0;
			return CARRY_ON;
		}
		return REPEAT_SEARCH;	/* Root is changed and we must recalculate the path. */
	}

	if (!B_IS_IN_TREE
	    (p_s_bh = PATH_OFFSET_PBUFFER(p_s_path, n_path_offset - 1)))
		return REPEAT_SEARCH;	/* Parent in the path is not in the tree. */

	if ((n_position =
	     PATH_OFFSET_POSITION(p_s_path,
				  n_path_offset - 1)) > B_NR_ITEMS(p_s_bh))
		return REPEAT_SEARCH;

	if (B_N_CHILD_NUM(p_s_bh, n_position) !=
	    PATH_OFFSET_PBUFFER(p_s_path, n_path_offset)->b_blocknr)
		/* Parent in the path is not parent of the current node in the tree. */
		return REPEAT_SEARCH;

	if (buffer_locked(p_s_bh)) {
		__wait_on_buffer(p_s_bh);
		if (FILESYSTEM_CHANGED_TB(p_s_tb))
			return REPEAT_SEARCH;
	}

	return CARRY_ON;	/* Parent in the path is unlocked and really parent of the current node.  */
}

/* Using lnum[n_h] and rnum[n_h] we should determine what neighbors
 * of S[n_h] we
 * need in order to balance S[n_h], and get them if necessary.
 * Returns:	SCHEDULE_OCCURRED - schedule occurred while the function worked;
 *	        CARRY_ON - schedule didn't occur while the function worked;
 */
static int get_neighbors(struct tree_balance *p_s_tb, int n_h)
{
	int n_child_position,
	    n_path_offset = PATH_H_PATH_OFFSET(p_s_tb->tb_path, n_h + 1);
	unsigned long n_son_number;
	struct super_block *p_s_sb = p_s_tb->tb_sb;
	struct buffer_head *p_s_bh;

	PROC_INFO_INC(p_s_sb, get_neighbors[n_h]);

	if (p_s_tb->lnum[n_h]) {
		/* We need left neighbor to balance S[n_h]. */
		PROC_INFO_INC(p_s_sb, need_l_neighbor[n_h]);
		p_s_bh = PATH_OFFSET_PBUFFER(p_s_tb->tb_path, n_path_offset);

		RFALSE(p_s_bh == p_s_tb->FL[n_h] &&
		       !PATH_OFFSET_POSITION(p_s_tb->tb_path, n_path_offset),
		       "PAP-8270: invalid position in the parent");

		n_child_position =
		    (p_s_bh ==
		     p_s_tb->FL[n_h]) ? p_s_tb->lkey[n_h] : B_NR_ITEMS(p_s_tb->
								       FL[n_h]);
		n_son_number = B_N_CHILD_NUM(p_s_tb->FL[n_h], n_child_position);
		p_s_bh = sb_bread(p_s_sb, n_son_number);
		if (!p_s_bh)
			return IO_ERROR;
		if (FILESYSTEM_CHANGED_TB(p_s_tb)) {
			decrement_bcount(p_s_bh);
			PROC_INFO_INC(p_s_sb, get_neighbors_restart[n_h]);
			return REPEAT_SEARCH;
		}

		RFALSE(!B_IS_IN_TREE(p_s_tb->FL[n_h]) ||
		       n_child_position > B_NR_ITEMS(p_s_tb->FL[n_h]) ||
		       B_N_CHILD_NUM(p_s_tb->FL[n_h], n_child_position) !=
		       p_s_bh->b_blocknr, "PAP-8275: invalid parent");
		RFALSE(!B_IS_IN_TREE(p_s_bh), "PAP-8280: invalid child");
		RFALSE(!n_h &&
		       B_FREE_SPACE(p_s_bh) !=
		       MAX_CHILD_SIZE(p_s_bh) -
		       dc_size(B_N_CHILD(p_s_tb->FL[0], n_child_position)),
		       "PAP-8290: invalid child size of left neighbor");

		decrement_bcount(p_s_tb->L[n_h]);
		p_s_tb->L[n_h] = p_s_bh;
	}

	if (p_s_tb->rnum[n_h]) {	/* We need right neighbor to balance S[n_path_offset]. */
		PROC_INFO_INC(p_s_sb, need_r_neighbor[n_h]);
		p_s_bh = PATH_OFFSET_PBUFFER(p_s_tb->tb_path, n_path_offset);

		RFALSE(p_s_bh == p_s_tb->FR[n_h] &&
		       PATH_OFFSET_POSITION(p_s_tb->tb_path,
					    n_path_offset) >=
		       B_NR_ITEMS(p_s_bh),
		       "PAP-8295: invalid position in the parent");

		n_child_position =
		    (p_s_bh == p_s_tb->FR[n_h]) ? p_s_tb->rkey[n_h] + 1 : 0;
		n_son_number = B_N_CHILD_NUM(p_s_tb->FR[n_h], n_child_position);
		p_s_bh = sb_bread(p_s_sb, n_son_number);
		if (!p_s_bh)
			return IO_ERROR;
		if (FILESYSTEM_CHANGED_TB(p_s_tb)) {
			decrement_bcount(p_s_bh);
			PROC_INFO_INC(p_s_sb, get_neighbors_restart[n_h]);
			return REPEAT_SEARCH;
		}
		decrement_bcount(p_s_tb->R[n_h]);
		p_s_tb->R[n_h] = p_s_bh;

		RFALSE(!n_h
		       && B_FREE_SPACE(p_s_bh) !=
		       MAX_CHILD_SIZE(p_s_bh) -
		       dc_size(B_N_CHILD(p_s_tb->FR[0], n_child_position)),
		       "PAP-8300: invalid child size of right neighbor (%d != %d - %d)",
		       B_FREE_SPACE(p_s_bh), MAX_CHILD_SIZE(p_s_bh),
		       dc_size(B_N_CHILD(p_s_tb->FR[0], n_child_position)));

	}
	return CARRY_ON;
}

#ifdef CONFIG_REISERFS_CHECK
void *reiserfs_kmalloc(size_t size, int flags, struct super_block *s)
{
	void *vp;
	static size_t malloced;

	vp = kmalloc(size, flags);
	if (vp) {
		REISERFS_SB(s)->s_kmallocs += size;
		if (REISERFS_SB(s)->s_kmallocs > malloced + 200000) {
			reiserfs_warning(s,
					 "vs-8301: reiserfs_kmalloc: allocated memory %d",
					 REISERFS_SB(s)->s_kmallocs);
			malloced = REISERFS_SB(s)->s_kmallocs;
		}
	}
	return vp;
}

void reiserfs_kfree(const void *vp, size_t size, struct super_block *s)
{
	kfree(vp);

	REISERFS_SB(s)->s_kmallocs -= size;
	if (REISERFS_SB(s)->s_kmallocs < 0)
		reiserfs_warning(s,
				 "vs-8302: reiserfs_kfree: allocated memory %d",
				 REISERFS_SB(s)->s_kmallocs);

}
#endif

static int get_virtual_node_size(struct super_block *sb, struct buffer_head *bh)
{
	int max_num_of_items;
	int max_num_of_entries;
	unsigned long blocksize = sb->s_blocksize;

#define MIN_NAME_LEN 1

	max_num_of_items = (blocksize - BLKH_SIZE) / (IH_SIZE + MIN_ITEM_LEN);
	max_num_of_entries = (blocksize - BLKH_SIZE - IH_SIZE) /
	    (DEH_SIZE + MIN_NAME_LEN);

	return sizeof(struct virtual_node) +
	    max(max_num_of_items * sizeof(struct virtual_item),
		sizeof(struct virtual_item) + sizeof(struct direntry_uarea) +
		(max_num_of_entries - 1) * sizeof(__u16));
}

/* maybe we should fail balancing we are going to perform when kmalloc
   fails several times. But now it will loop until kmalloc gets
   required memory */
static int get_mem_for_virtual_node(struct tree_balance *tb)
{
	int check_fs = 0;
	int size;
	char *buf;

	size = get_virtual_node_size(tb->tb_sb, PATH_PLAST_BUFFER(tb->tb_path));

	if (size > tb->vn_buf_size) {
		/* we have to allocate more memory for virtual node */
		if (tb->vn_buf) {
			/* free memory allocated before */
			reiserfs_kfree(tb->vn_buf, tb->vn_buf_size, tb->tb_sb);
			/* this is not needed if kfree is atomic */
			check_fs = 1;
		}

		/* virtual node requires now more memory */
		tb->vn_buf_size = size;

		/* get memory for virtual item */
		buf =
		    reiserfs_kmalloc(size, GFP_ATOMIC | __GFP_NOWARN,
				     tb->tb_sb);
		if (!buf) {
			/* getting memory with GFP_KERNEL priority may involve
			   balancing now (due to indirect_to_direct conversion on
			   dcache shrinking). So, release path and collected
			   resources here */
			free_buffers_in_tb(tb);
			buf = reiserfs_kmalloc(size, GFP_NOFS, tb->tb_sb);
			if (!buf) {
#ifdef CONFIG_REISERFS_CHECK
				reiserfs_warning(tb->tb_sb,
						 "vs-8345: get_mem_for_virtual_node: "
						 "kmalloc failed. reiserfs kmalloced %d bytes",
						 REISERFS_SB(tb->tb_sb)->
						 s_kmallocs);
#endif
				tb->vn_buf_size = 0;
			}
			tb->vn_buf = buf;
			schedule();
			return REPEAT_SEARCH;
		}

		tb->vn_buf = buf;
	}

	if (check_fs && FILESYSTEM_CHANGED_TB(tb))
		return REPEAT_SEARCH;

	return CARRY_ON;
}

#ifdef CONFIG_REISERFS_CHECK
static void tb_buffer_sanity_check(struct super_block *p_s_sb,
				   struct buffer_head *p_s_bh,
				   const char *descr, int level)
{
	if (p_s_bh) {
		if (atomic_read(&(p_s_bh->b_count)) <= 0) {

			reiserfs_panic(p_s_sb,
				       "jmacd-1: tb_buffer_sanity_check(): negative or zero reference counter for buffer %s[%d] (%b)\n",
				       descr, level, p_s_bh);
		}

		if (!buffer_uptodate(p_s_bh)) {
			reiserfs_panic(p_s_sb,
				       "jmacd-2: tb_buffer_sanity_check(): buffer is not up to date %s[%d] (%b)\n",
				       descr, level, p_s_bh);
		}

		if (!B_IS_IN_TREE(p_s_bh)) {
			reiserfs_panic(p_s_sb,
				       "jmacd-3: tb_buffer_sanity_check(): buffer is not in tree %s[%d] (%b)\n",
				       descr, level, p_s_bh);
		}

		if (p_s_bh->b_bdev != p_s_sb->s_bdev) {
			reiserfs_panic(p_s_sb,
				       "jmacd-4: tb_buffer_sanity_check(): buffer has wrong device %s[%d] (%b)\n",
				       descr, level, p_s_bh);
		}

		if (p_s_bh->b_size != p_s_sb->s_blocksize) {
			reiserfs_panic(p_s_sb,
				       "jmacd-5: tb_buffer_sanity_check(): buffer has wrong blocksize %s[%d] (%b)\n",
				       descr, level, p_s_bh);
		}

		if (p_s_bh->b_blocknr > SB_BLOCK_COUNT(p_s_sb)) {
			reiserfs_panic(p_s_sb,
				       "jmacd-6: tb_buffer_sanity_check(): buffer block number too high %s[%d] (%b)\n",
				       descr, level, p_s_bh);
		}
	}
}
#else
static void tb_buffer_sanity_check(struct super_block *p_s_sb,
				   struct buffer_head *p_s_bh,
				   const char *descr, int level)
{;
}
#endif

static int clear_all_dirty_bits(struct super_block *s, struct buffer_head *bh)
{
	return reiserfs_prepare_for_journal(s, bh, 0);
}

static int wait_tb_buffers_until_unlocked(struct tree_balance *p_s_tb)
{
	struct buffer_head *locked;
#ifdef CONFIG_REISERFS_CHECK
	int repeat_counter = 0;
#endif
	int i;

	do {

		locked = NULL;

		for (i = p_s_tb->tb_path->path_length;
		     !locked && i > ILLEGAL_PATH_ELEMENT_OFFSET; i--) {
			if (PATH_OFFSET_PBUFFER(p_s_tb->tb_path, i)) {
				/* if I understand correctly, we can only be sure the last buffer
				 ** in the path is in the tree --clm
				 */
#ifdef CONFIG_REISERFS_CHECK
				if (PATH_PLAST_BUFFER(p_s_tb->tb_path) ==
				    PATH_OFFSET_PBUFFER(p_s_tb->tb_path, i)) {
					tb_buffer_sanity_check(p_s_tb->tb_sb,
							       PATH_OFFSET_PBUFFER
							       (p_s_tb->tb_path,
								i), "S",
							       p_s_tb->tb_path->
							       path_length - i);
				}
#endif
				if (!clear_all_dirty_bits(p_s_tb->tb_sb,
							  PATH_OFFSET_PBUFFER
							  (p_s_tb->tb_path,
							   i))) {
					locked =
					    PATH_OFFSET_PBUFFER(p_s_tb->tb_path,
								i);
				}
			}
		}

		for (i = 0; !locked && i < MAX_HEIGHT && p_s_tb->insert_size[i];
		     i++) {

			if (p_s_tb->lnum[i]) {

				if (p_s_tb->L[i]) {
					tb_buffer_sanity_check(p_s_tb->tb_sb,
							       p_s_tb->L[i],
							       "L", i);
					if (!clear_all_dirty_bits
					    (p_s_tb->tb_sb, p_s_tb->L[i]))
						locked = p_s_tb->L[i];
				}

				if (!locked && p_s_tb->FL[i]) {
					tb_buffer_sanity_check(p_s_tb->tb_sb,
							       p_s_tb->FL[i],
							       "FL", i);
					if (!clear_all_dirty_bits
					    (p_s_tb->tb_sb, p_s_tb->FL[i]))
						locked = p_s_tb->FL[i];
				}

				if (!locked && p_s_tb->CFL[i]) {
					tb_buffer_sanity_check(p_s_tb->tb_sb,
							       p_s_tb->CFL[i],
							       "CFL", i);
					if (!clear_all_dirty_bits
					    (p_s_tb->tb_sb, p_s_tb->CFL[i]))
						locked = p_s_tb->CFL[i];
				}

			}

			if (!locked && (p_s_tb->rnum[i])) {

				if (p_s_tb->R[i]) {
					tb_buffer_sanity_check(p_s_tb->tb_sb,
							       p_s_tb->R[i],
							       "R", i);
					if (!clear_all_dirty_bits
					    (p_s_tb->tb_sb, p_s_tb->R[i]))
						locked = p_s_tb->R[i];
				}

				if (!locked && p_s_tb->FR[i]) {
					tb_buffer_sanity_check(p_s_tb->tb_sb,
							       p_s_tb->FR[i],
							       "FR", i);
					if (!clear_all_dirty_bits
					    (p_s_tb->tb_sb, p_s_tb->FR[i]))
						locked = p_s_tb->FR[i];
				}

				if (!locked && p_s_tb->CFR[i]) {
					tb_buffer_sanity_check(p_s_tb->tb_sb,
							       p_s_tb->CFR[i],
							       "CFR", i);
					if (!clear_all_dirty_bits
					    (p_s_tb->tb_sb, p_s_tb->CFR[i]))
						locked = p_s_tb->CFR[i];
				}
			}
		}
		/* as far as I can tell, this is not required.  The FEB list seems
		 ** to be full of newly allocated nodes, which will never be locked,
		 ** dirty, or anything else.
		 ** To be safe, I'm putting in the checks and waits in.  For the moment,
		 ** they are needed to keep the code in journal.c from complaining
		 ** about the buffer.  That code is inside CONFIG_REISERFS_CHECK as well.
		 ** --clm
		 */
		for (i = 0; !locked && i < MAX_FEB_SIZE; i++) {
			if (p_s_tb->FEB[i]) {
				if (!clear_all_dirty_bits
				    (p_s_tb->tb_sb, p_s_tb->FEB[i]))
					locked = p_s_tb->FEB[i];
			}
		}

		if (locked) {
#ifdef CONFIG_REISERFS_CHECK
			repeat_counter++;
			if ((repeat_counter % 10000) == 0) {
				reiserfs_warning(p_s_tb->tb_sb,
						 "wait_tb_buffers_until_released(): too many "
						 "iterations waiting for buffer to unlock "
						 "(%b)", locked);

				/* Don't loop forever.  Try to recover from possible error. */

				return (FILESYSTEM_CHANGED_TB(p_s_tb)) ?
				    REPEAT_SEARCH : CARRY_ON;
			}
#endif
			__wait_on_buffer(locked);
			if (FILESYSTEM_CHANGED_TB(p_s_tb)) {
				return REPEAT_SEARCH;
			}
		}

	} while (locked);

	return CARRY_ON;
}

/* Prepare for balancing, that is
 *	get all necessary parents, and neighbors;
 *	analyze what and where should be moved;
 *	get sufficient number of new nodes;
 * Balancing will start only after all resources will be collected at a time.
 * 
 * When ported to SMP kernels, only at the last moment after all needed nodes
 * are collected in cache, will the resources be locked using the usual
 * textbook ordered lock acquisition algorithms.  Note that ensuring that
 * this code neither write locks what it does not need to write lock nor locks out of order
 * will be a pain in the butt that could have been avoided.  Grumble grumble. -Hans
 * 
 * fix is meant in the sense of render unchanging
 * 
 * Latency might be improved by first gathering a list of what buffers are needed
 * and then getting as many of them in parallel as possible? -Hans
 *
 * Parameters:
 *	op_mode	i - insert, d - delete, c - cut (truncate), p - paste (append)
 *	tb	tree_balance structure;
 *	inum	item number in S[h];
 *      pos_in_item - comment this if you can
 *      ins_ih & ins_sd are used when inserting
 * Returns:	1 - schedule occurred while the function worked;
 *	        0 - schedule didn't occur while the function worked;
 *             -1 - if no_disk_space 
 */

int fix_nodes(int n_op_mode, struct tree_balance *p_s_tb, struct item_head *p_s_ins_ih,	// item head of item being inserted
	      const void *data	// inserted item or data to be pasted
    )
{
	int n_ret_value, n_h, n_item_num = PATH_LAST_POSITION(p_s_tb->tb_path);
	int n_pos_in_item;

	/* we set wait_tb_buffers_run when we have to restore any dirty bits cleared
	 ** during wait_tb_buffers_run
	 */
	int wait_tb_buffers_run = 0;
	struct buffer_head *p_s_tbS0 = PATH_PLAST_BUFFER(p_s_tb->tb_path);

	++REISERFS_SB(p_s_tb->tb_sb)->s_fix_nodes;

	n_pos_in_item = p_s_tb->tb_path->pos_in_item;

	p_s_tb->fs_gen = get_generation(p_s_tb->tb_sb);

	/* we prepare and log the super here so it will already be in the
	 ** transaction when do_balance needs to change it.
	 ** This way do_balance won't have to schedule when trying to prepare
	 ** the super for logging
	 */
	reiserfs_prepare_for_journal(p_s_tb->tb_sb,
				     SB_BUFFER_WITH_SB(p_s_tb->tb_sb), 1);
	journal_mark_dirty(p_s_tb->transaction_handle, p_s_tb->tb_sb,
			   SB_BUFFER_WITH_SB(p_s_tb->tb_sb));
	if (FILESYSTEM_CHANGED_TB(p_s_tb))
		return REPEAT_SEARCH;

	/* if it possible in indirect_to_direct conversion */
	if (buffer_locked(p_s_tbS0)) {
		__wait_on_buffer(p_s_tbS0);
		if (FILESYSTEM_CHANGED_TB(p_s_tb))
			return REPEAT_SEARCH;
	}
#ifdef CONFIG_REISERFS_CHECK
	if (cur_tb) {
		print_cur_tb("fix_nodes");
		reiserfs_panic(p_s_tb->tb_sb,
			       "PAP-8305: fix_nodes:  there is pending do_balance");
	}

	if (!buffer_uptodate(p_s_tbS0) || !B_IS_IN_TREE(p_s_tbS0)) {
		reiserfs_panic(p_s_tb->tb_sb,
			       "PAP-8320: fix_nodes: S[0] (%b %z) is not uptodate "
			       "at the beginning of fix_nodes or not in tree (mode %c)",
			       p_s_tbS0, p_s_tbS0, n_op_mode);
	}

	/* Check parameters. */
	switch (n_op_mode) {
	case M_INSERT:
		if (n_item_num <= 0 || n_item_num > B_NR_ITEMS(p_s_tbS0))
			reiserfs_panic(p_s_tb->tb_sb,
				       "PAP-8330: fix_nodes: Incorrect item number %d (in S0 - %d) in case of insert",
				       n_item_num, B_NR_ITEMS(p_s_tbS0));
		break;
	case M_PASTE:
	case M_DELETE:
	case M_CUT:
		if (n_item_num < 0 || n_item_num >= B_NR_ITEMS(p_s_tbS0)) {
			print_block(p_s_tbS0, 0, -1, -1);
			reiserfs_panic(p_s_tb->tb_sb,
				       "PAP-8335: fix_nodes: Incorrect item number(%d); mode = %c insert_size = %d\n",
				       n_item_num, n_op_mode,
				       p_s_tb->insert_size[0]);
		}
		break;
	default:
		reiserfs_panic(p_s_tb->tb_sb,
			       "PAP-8340: fix_nodes: Incorrect mode of operation");
	}
#endif

	if (get_mem_for_virtual_node(p_s_tb) == REPEAT_SEARCH)
		// FIXME: maybe -ENOMEM when tb->vn_buf == 0? Now just repeat
		return REPEAT_SEARCH;

	/* Starting from the leaf level; for all levels n_h of the tree. */
	for (n_h = 0; n_h < MAX_HEIGHT && p_s_tb->insert_size[n_h]; n_h++) {
		if ((n_ret_value = get_direct_parent(p_s_tb, n_h)) != CARRY_ON) {
			goto repeat;
		}

		if ((n_ret_value =
		     check_balance(n_op_mode, p_s_tb, n_h, n_item_num,
				   n_pos_in_item, p_s_ins_ih,
				   data)) != CARRY_ON) {
			if (n_ret_value == NO_BALANCING_NEEDED) {
				/* No balancing for higher levels needed. */
				if ((n_ret_value =
				     get_neighbors(p_s_tb, n_h)) != CARRY_ON) {
					goto repeat;
				}
				if (n_h != MAX_HEIGHT - 1)
					p_s_tb->insert_size[n_h + 1] = 0;
				/* ok, analysis and resource gathering are complete */
				break;
			}
			goto repeat;
		}

		if ((n_ret_value = get_neighbors(p_s_tb, n_h)) != CARRY_ON) {
			goto repeat;
		}

		if ((n_ret_value = get_empty_nodes(p_s_tb, n_h)) != CARRY_ON) {
			goto repeat;	/* No disk space, or schedule occurred and
					   analysis may be invalid and needs to be redone. */
		}

		if (!PATH_H_PBUFFER(p_s_tb->tb_path, n_h)) {
			/* We have a positive insert size but no nodes exist on this
			   level, this means that we are creating a new root. */

			RFALSE(p_s_tb->blknum[n_h] != 1,
			       "PAP-8350: creating new empty root");

			if (n_h < MAX_HEIGHT - 1)
				p_s_tb->insert_size[n_h + 1] = 0;
		} else if (!PATH_H_PBUFFER(p_s_tb->tb_path, n_h + 1)) {
			if (p_s_tb->blknum[n_h] > 1) {
				/* The tree needs to be grown, so this node S[n_h]
				   which is the root node is split into two nodes,
				   and a new node (S[n_h+1]) will be created to
				   become the root node.  */

				RFALSE(n_h == MAX_HEIGHT - 1,
				       "PAP-8355: attempt to create too high of a tree");

				p_s_tb->insert_size[n_h + 1] =
				    (DC_SIZE +
				     KEY_SIZE) * (p_s_tb->blknum[n_h] - 1) +
				    DC_SIZE;
			} else if (n_h < MAX_HEIGHT - 1)
				p_s_tb->insert_size[n_h + 1] = 0;
		} else
			p_s_tb->insert_size[n_h + 1] =
			    (DC_SIZE + KEY_SIZE) * (p_s_tb->blknum[n_h] - 1);
	}

	if ((n_ret_value = wait_tb_buffers_until_unlocked(p_s_tb)) == CARRY_ON) {
		if (FILESYSTEM_CHANGED_TB(p_s_tb)) {
			wait_tb_buffers_run = 1;
			n_ret_value = REPEAT_SEARCH;
			goto repeat;
		} else {
			return CARRY_ON;
		}
	} else {
		wait_tb_buffers_run = 1;
		goto repeat;
	}

      repeat:
	// fix_nodes was unable to perform its calculation due to
	// filesystem got changed under us, lack of free disk space or i/o
	// failure. If the first is the case - the search will be
	// repeated. For now - free all resources acquired so far except
	// for the new allocated nodes
	{
		int i;

		/* Release path buffers. */
		if (wait_tb_buffers_run) {
			pathrelse_and_restore(p_s_tb->tb_sb, p_s_tb->tb_path);
		} else {
			pathrelse(p_s_tb->tb_path);
		}
		/* brelse all resources collected for balancing */
		for (i = 0; i < MAX_HEIGHT; i++) {
			if (wait_tb_buffers_run) {
				reiserfs_restore_prepared_buffer(p_s_tb->tb_sb,
								 p_s_tb->L[i]);
				reiserfs_restore_prepared_buffer(p_s_tb->tb_sb,
								 p_s_tb->R[i]);
				reiserfs_restore_prepared_buffer(p_s_tb->tb_sb,
								 p_s_tb->FL[i]);
				reiserfs_restore_prepared_buffer(p_s_tb->tb_sb,
								 p_s_tb->FR[i]);
				reiserfs_restore_prepared_buffer(p_s_tb->tb_sb,
								 p_s_tb->
								 CFL[i]);
				reiserfs_restore_prepared_buffer(p_s_tb->tb_sb,
								 p_s_tb->
								 CFR[i]);
			}

			brelse(p_s_tb->L[i]);
			p_s_tb->L[i] = NULL;
			brelse(p_s_tb->R[i]);
			p_s_tb->R[i] = NULL;
			brelse(p_s_tb->FL[i]);
			p_s_tb->FL[i] = NULL;
			brelse(p_s_tb->FR[i]);
			p_s_tb->FR[i] = NULL;
			brelse(p_s_tb->CFL[i]);
			p_s_tb->CFL[i] = NULL;
			brelse(p_s_tb->CFR[i]);
			p_s_tb->CFR[i] = NULL;
		}

		if (wait_tb_buffers_run) {
			for (i = 0; i < MAX_FEB_SIZE; i++) {
				if (p_s_tb->FEB[i]) {
					reiserfs_restore_prepared_buffer
					    (p_s_tb->tb_sb, p_s_tb->FEB[i]);
				}
			}
		}
		return n_ret_value;
	}

}

/* Anatoly will probably forgive me renaming p_s_tb to tb. I just
   wanted to make lines shorter */
void unfix_nodes(struct tree_balance *tb)
{
	int i;

	/* Release path buffers. */
	pathrelse_and_restore(tb->tb_sb, tb->tb_path);

	/* brelse all resources collected for balancing */
	for (i = 0; i < MAX_HEIGHT; i++) {
		reiserfs_restore_prepared_buffer(tb->tb_sb, tb->L[i]);
		reiserfs_restore_prepared_buffer(tb->tb_sb, tb->R[i]);
		reiserfs_restore_prepared_buffer(tb->tb_sb, tb->FL[i]);
		reiserfs_restore_prepared_buffer(tb->tb_sb, tb->FR[i]);
		reiserfs_restore_prepared_buffer(tb->tb_sb, tb->CFL[i]);
		reiserfs_restore_prepared_buffer(tb->tb_sb, tb->CFR[i]);

		brelse(tb->L[i]);
		brelse(tb->R[i]);
		brelse(tb->FL[i]);
		brelse(tb->FR[i]);
		brelse(tb->CFL[i]);
		brelse(tb->CFR[i]);
	}

	/* deal with list of allocated (used and unused) nodes */
	for (i = 0; i < MAX_FEB_SIZE; i++) {
		if (tb->FEB[i]) {
			b_blocknr_t blocknr = tb->FEB[i]->b_blocknr;
			/* de-allocated block which was not used by balancing and
			   bforget about buffer for it */
			brelse(tb->FEB[i]);
			reiserfs_free_block(tb->transaction_handle, NULL,
					    blocknr, 0);
		}
		if (tb->used[i]) {
			/* release used as new nodes including a new root */
			brelse(tb->used[i]);
		}
	}

	if (tb->vn_buf)
		reiserfs_kfree(tb->vn_buf, tb->vn_buf_size, tb->tb_sb);

}
