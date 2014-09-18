/*
 * Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README
 */

#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/time.h>
#include "reiserfs.h"
#include <linux/buffer_head.h>

/*
 * copy copy_count entries from source directory item to dest buffer
 * (creating new item if needed)
 */
static void leaf_copy_dir_entries(struct buffer_info *dest_bi,
				  struct buffer_head *source, int last_first,
				  int item_num, int from, int copy_count)
{
	struct buffer_head *dest = dest_bi->bi_bh;
	/*
	 * either the number of target item, or if we must create a
	 * new item, the number of the item we will create it next to
	 */
	int item_num_in_dest;

	struct item_head *ih;
	struct reiserfs_de_head *deh;
	int copy_records_len;	/* length of all records in item to be copied */
	char *records;

	ih = item_head(source, item_num);

	RFALSE(!is_direntry_le_ih(ih), "vs-10000: item must be directory item");

	/*
	 * length of all record to be copied and first byte of
	 * the last of them
	 */
	deh = B_I_DEH(source, ih);
	if (copy_count) {
		copy_records_len = (from ? deh_location(&deh[from - 1]) :
				    ih_item_len(ih)) -
		    deh_location(&deh[from + copy_count - 1]);
		records =
		    source->b_data + ih_location(ih) +
		    deh_location(&deh[from + copy_count - 1]);
	} else {
		copy_records_len = 0;
		records = NULL;
	}

	/* when copy last to first, dest buffer can contain 0 items */
	item_num_in_dest =
	    (last_first ==
	     LAST_TO_FIRST) ? ((B_NR_ITEMS(dest)) ? 0 : -1) : (B_NR_ITEMS(dest)
							       - 1);

	/*
	 * if there are no items in dest or the first/last item in
	 * dest is not item of the same directory
	 */
	if ((item_num_in_dest == -1) ||
	    (last_first == FIRST_TO_LAST && le_ih_k_offset(ih) == DOT_OFFSET) ||
	    (last_first == LAST_TO_FIRST
	     && comp_short_le_keys /*COMP_SHORT_KEYS */ (&ih->ih_key,
							 leaf_key(dest,
								  item_num_in_dest))))
	{
		/* create new item in dest */
		struct item_head new_ih;

		/* form item header */
		memcpy(&new_ih.ih_key, &ih->ih_key, KEY_SIZE);
		put_ih_version(&new_ih, KEY_FORMAT_3_5);
		/* calculate item len */
		put_ih_item_len(&new_ih,
				DEH_SIZE * copy_count + copy_records_len);
		put_ih_entry_count(&new_ih, 0);

		if (last_first == LAST_TO_FIRST) {
			/* form key by the following way */
			if (from < ih_entry_count(ih)) {
				set_le_ih_k_offset(&new_ih,
						   deh_offset(&deh[from]));
			} else {
				/*
				 * no entries will be copied to this
				 * item in this function
				 */
				set_le_ih_k_offset(&new_ih, U32_MAX);
				/*
				 * this item is not yet valid, but we
				 * want I_IS_DIRECTORY_ITEM to return 1
				 * for it, so we -1
				 */
			}
			set_le_key_k_type(KEY_FORMAT_3_5, &new_ih.ih_key,
					  TYPE_DIRENTRY);
		}

		/* insert item into dest buffer */
		leaf_insert_into_buf(dest_bi,
				     (last_first ==
				      LAST_TO_FIRST) ? 0 : B_NR_ITEMS(dest),
				     &new_ih, NULL, 0);
	} else {
		/* prepare space for entries */
		leaf_paste_in_buffer(dest_bi,
				     (last_first ==
				      FIRST_TO_LAST) ? (B_NR_ITEMS(dest) -
							1) : 0, MAX_US_INT,
				     DEH_SIZE * copy_count + copy_records_len,
				     records, 0);
	}

	item_num_in_dest =
	    (last_first == FIRST_TO_LAST) ? (B_NR_ITEMS(dest) - 1) : 0;

	leaf_paste_entries(dest_bi, item_num_in_dest,
			   (last_first ==
			    FIRST_TO_LAST) ? ih_entry_count(item_head(dest,
									  item_num_in_dest))
			   : 0, copy_count, deh + from, records,
			   DEH_SIZE * copy_count + copy_records_len);
}

/*
 * Copy the first (if last_first == FIRST_TO_LAST) or last
 * (last_first == LAST_TO_FIRST) item or part of it or nothing
 * (see the return 0 below) from SOURCE to the end (if last_first)
 * or beginning (!last_first) of the DEST
 */
/* returns 1 if anything was copied, else 0 */
static int leaf_copy_boundary_item(struct buffer_info *dest_bi,
				   struct buffer_head *src, int last_first,
				   int bytes_or_entries)
{
	struct buffer_head *dest = dest_bi->bi_bh;
	/* number of items in the source and destination buffers */
	int dest_nr_item, src_nr_item;
	struct item_head *ih;
	struct item_head *dih;

	dest_nr_item = B_NR_ITEMS(dest);

	/*
	 * if ( DEST is empty or first item of SOURCE and last item of
	 * DEST are the items of different objects or of different types )
	 * then there is no need to treat this item differently from the
	 * other items that we copy, so we return
	 */
	if (last_first == FIRST_TO_LAST) {
		ih = item_head(src, 0);
		dih = item_head(dest, dest_nr_item - 1);

		/* there is nothing to merge */
		if (!dest_nr_item
		    || (!op_is_left_mergeable(&ih->ih_key, src->b_size)))
			return 0;

		RFALSE(!ih_item_len(ih),
		       "vs-10010: item can not have empty length");

		if (is_direntry_le_ih(ih)) {
			if (bytes_or_entries == -1)
				/* copy all entries to dest */
				bytes_or_entries = ih_entry_count(ih);
			leaf_copy_dir_entries(dest_bi, src, FIRST_TO_LAST, 0, 0,
					      bytes_or_entries);
			return 1;
		}

		/*
		 * copy part of the body of the first item of SOURCE
		 * to the end of the body of the last item of the DEST
		 * part defined by 'bytes_or_entries'; if bytes_or_entries
		 * == -1 copy whole body; don't create new item header
		 */
		if (bytes_or_entries == -1)
			bytes_or_entries = ih_item_len(ih);

#ifdef CONFIG_REISERFS_CHECK
		else {
			if (bytes_or_entries == ih_item_len(ih)
			    && is_indirect_le_ih(ih))
				if (get_ih_free_space(ih))
					reiserfs_panic(sb_from_bi(dest_bi),
						       "vs-10020",
						       "last unformatted node "
						       "must be filled "
						       "entirely (%h)", ih);
		}
#endif

		/*
		 * merge first item (or its part) of src buffer with the last
		 * item of dest buffer. Both are of the same file
		 */
		leaf_paste_in_buffer(dest_bi,
				     dest_nr_item - 1, ih_item_len(dih),
				     bytes_or_entries, ih_item_body(src, ih), 0);

		if (is_indirect_le_ih(dih)) {
			RFALSE(get_ih_free_space(dih),
			       "vs-10030: merge to left: last unformatted node of non-last indirect item %h must have zerto free space",
			       ih);
			if (bytes_or_entries == ih_item_len(ih))
				set_ih_free_space(dih, get_ih_free_space(ih));
		}

		return 1;
	}

	/* copy boundary item to right (last_first == LAST_TO_FIRST) */

	/*
	 * (DEST is empty or last item of SOURCE and first item of DEST
	 * are the items of different object or of different types)
	 */
	src_nr_item = B_NR_ITEMS(src);
	ih = item_head(src, src_nr_item - 1);
	dih = item_head(dest, 0);

	if (!dest_nr_item || !op_is_left_mergeable(&dih->ih_key, src->b_size))
		return 0;

	if (is_direntry_le_ih(ih)) {
		/*
		 * bytes_or_entries = entries number in last
		 * item body of SOURCE
		 */
		if (bytes_or_entries == -1)
			bytes_or_entries = ih_entry_count(ih);

		leaf_copy_dir_entries(dest_bi, src, LAST_TO_FIRST,
				      src_nr_item - 1,
				      ih_entry_count(ih) - bytes_or_entries,
				      bytes_or_entries);
		return 1;
	}

	/*
	 * copy part of the body of the last item of SOURCE to the
	 * begin of the body of the first item of the DEST; part defined
	 * by 'bytes_or_entries'; if byte_or_entriess == -1 copy whole body;
	 * change first item key of the DEST; don't create new item header
	 */

	RFALSE(is_indirect_le_ih(ih) && get_ih_free_space(ih),
	       "vs-10040: merge to right: last unformatted node of non-last indirect item must be filled entirely (%h)",
	       ih);

	if (bytes_or_entries == -1) {
		/* bytes_or_entries = length of last item body of SOURCE */
		bytes_or_entries = ih_item_len(ih);

		RFALSE(le_ih_k_offset(dih) !=
		       le_ih_k_offset(ih) + op_bytes_number(ih, src->b_size),
		       "vs-10050: items %h and %h do not match", ih, dih);

		/* change first item key of the DEST */
		set_le_ih_k_offset(dih, le_ih_k_offset(ih));

		/* item becomes non-mergeable */
		/* or mergeable if left item was */
		set_le_ih_k_type(dih, le_ih_k_type(ih));
	} else {
		/* merge to right only part of item */
		RFALSE(ih_item_len(ih) <= bytes_or_entries,
		       "vs-10060: no so much bytes %lu (needed %lu)",
		       (unsigned long)ih_item_len(ih),
		       (unsigned long)bytes_or_entries);

		/* change first item key of the DEST */
		if (is_direct_le_ih(dih)) {
			RFALSE(le_ih_k_offset(dih) <=
			       (unsigned long)bytes_or_entries,
			       "vs-10070: dih %h, bytes_or_entries(%d)", dih,
			       bytes_or_entries);
			set_le_ih_k_offset(dih,
					   le_ih_k_offset(dih) -
					   bytes_or_entries);
		} else {
			RFALSE(le_ih_k_offset(dih) <=
			       (bytes_or_entries / UNFM_P_SIZE) * dest->b_size,
			       "vs-10080: dih %h, bytes_or_entries(%d)",
			       dih,
			       (bytes_or_entries / UNFM_P_SIZE) * dest->b_size);
			set_le_ih_k_offset(dih,
					   le_ih_k_offset(dih) -
					   ((bytes_or_entries / UNFM_P_SIZE) *
					    dest->b_size));
		}
	}

	leaf_paste_in_buffer(dest_bi, 0, 0, bytes_or_entries,
			     ih_item_body(src,
				       ih) + ih_item_len(ih) - bytes_or_entries,
			     0);
	return 1;
}

/*
 * copy cpy_mun items from buffer src to buffer dest
 * last_first == FIRST_TO_LAST means, that we copy cpy_num items beginning
 *                             from first-th item in src to tail of dest
 * last_first == LAST_TO_FIRST means, that we copy cpy_num items beginning
 *                             from first-th item in src to head of dest
 */
static void leaf_copy_items_entirely(struct buffer_info *dest_bi,
				     struct buffer_head *src, int last_first,
				     int first, int cpy_num)
{
	struct buffer_head *dest;
	int nr, free_space;
	int dest_before;
	int last_loc, last_inserted_loc, location;
	int i, j;
	struct block_head *blkh;
	struct item_head *ih;

	RFALSE(last_first != LAST_TO_FIRST && last_first != FIRST_TO_LAST,
	       "vs-10090: bad last_first parameter %d", last_first);
	RFALSE(B_NR_ITEMS(src) - first < cpy_num,
	       "vs-10100: too few items in source %d, required %d from %d",
	       B_NR_ITEMS(src), cpy_num, first);
	RFALSE(cpy_num < 0, "vs-10110: can not copy negative amount of items");
	RFALSE(!dest_bi, "vs-10120: can not copy negative amount of items");

	dest = dest_bi->bi_bh;

	RFALSE(!dest, "vs-10130: can not copy negative amount of items");

	if (cpy_num == 0)
		return;

	blkh = B_BLK_HEAD(dest);
	nr = blkh_nr_item(blkh);
	free_space = blkh_free_space(blkh);

	/*
	 * we will insert items before 0-th or nr-th item in dest buffer.
	 * It depends of last_first parameter
	 */
	dest_before = (last_first == LAST_TO_FIRST) ? 0 : nr;

	/* location of head of first new item */
	ih = item_head(dest, dest_before);

	RFALSE(blkh_free_space(blkh) < cpy_num * IH_SIZE,
	       "vs-10140: not enough free space for headers %d (needed %d)",
	       B_FREE_SPACE(dest), cpy_num * IH_SIZE);

	/* prepare space for headers */
	memmove(ih + cpy_num, ih, (nr - dest_before) * IH_SIZE);

	/* copy item headers */
	memcpy(ih, item_head(src, first), cpy_num * IH_SIZE);

	free_space -= (IH_SIZE * cpy_num);
	set_blkh_free_space(blkh, free_space);

	/* location of unmovable item */
	j = location = (dest_before == 0) ? dest->b_size : ih_location(ih - 1);
	for (i = dest_before; i < nr + cpy_num; i++) {
		location -= ih_item_len(ih + i - dest_before);
		put_ih_location(ih + i - dest_before, location);
	}

	/* prepare space for items */
	last_loc = ih_location(&ih[nr + cpy_num - 1 - dest_before]);
	last_inserted_loc = ih_location(&ih[cpy_num - 1]);

	/* check free space */
	RFALSE(free_space < j - last_inserted_loc,
	       "vs-10150: not enough free space for items %d (needed %d)",
	       free_space, j - last_inserted_loc);

	memmove(dest->b_data + last_loc,
		dest->b_data + last_loc + j - last_inserted_loc,
		last_inserted_loc - last_loc);

	/* copy items */
	memcpy(dest->b_data + last_inserted_loc,
	       item_body(src, (first + cpy_num - 1)),
	       j - last_inserted_loc);

	/* sizes, item number */
	set_blkh_nr_item(blkh, nr + cpy_num);
	set_blkh_free_space(blkh, free_space - (j - last_inserted_loc));

	do_balance_mark_leaf_dirty(dest_bi->tb, dest, 0);

	if (dest_bi->bi_parent) {
		struct disk_child *t_dc;
		t_dc = B_N_CHILD(dest_bi->bi_parent, dest_bi->bi_position);
		RFALSE(dc_block_number(t_dc) != dest->b_blocknr,
		       "vs-10160: block number in bh does not match to field in disk_child structure %lu and %lu",
		       (long unsigned)dest->b_blocknr,
		       (long unsigned)dc_block_number(t_dc));
		put_dc_size(t_dc,
			    dc_size(t_dc) + (j - last_inserted_loc +
					     IH_SIZE * cpy_num));

		do_balance_mark_internal_dirty(dest_bi->tb, dest_bi->bi_parent,
					       0);
	}
}

/*
 * This function splits the (liquid) item into two items (useful when
 * shifting part of an item into another node.)
 */
static void leaf_item_bottle(struct buffer_info *dest_bi,
			     struct buffer_head *src, int last_first,
			     int item_num, int cpy_bytes)
{
	struct buffer_head *dest = dest_bi->bi_bh;
	struct item_head *ih;

	RFALSE(cpy_bytes == -1,
	       "vs-10170: bytes == - 1 means: do not split item");

	if (last_first == FIRST_TO_LAST) {
		/*
		 * if ( if item in position item_num in buffer SOURCE
		 * is directory item )
		 */
		ih = item_head(src, item_num);
		if (is_direntry_le_ih(ih))
			leaf_copy_dir_entries(dest_bi, src, FIRST_TO_LAST,
					      item_num, 0, cpy_bytes);
		else {
			struct item_head n_ih;

			/*
			 * copy part of the body of the item number 'item_num'
			 * of SOURCE to the end of the DEST part defined by
			 * 'cpy_bytes'; create new item header; change old
			 * item_header (????); n_ih = new item_header;
			 */
			memcpy(&n_ih, ih, IH_SIZE);
			put_ih_item_len(&n_ih, cpy_bytes);
			if (is_indirect_le_ih(ih)) {
				RFALSE(cpy_bytes == ih_item_len(ih)
				       && get_ih_free_space(ih),
				       "vs-10180: when whole indirect item is bottle to left neighbor, it must have free_space==0 (not %lu)",
				       (long unsigned)get_ih_free_space(ih));
				set_ih_free_space(&n_ih, 0);
			}

			RFALSE(op_is_left_mergeable(&ih->ih_key, src->b_size),
			       "vs-10190: bad mergeability of item %h", ih);
			n_ih.ih_version = ih->ih_version;	/* JDM Endian safe, both le */
			leaf_insert_into_buf(dest_bi, B_NR_ITEMS(dest), &n_ih,
					     item_body(src, item_num), 0);
		}
	} else {
		/*
		 * if ( if item in position item_num in buffer
		 * SOURCE is directory item )
		 */
		ih = item_head(src, item_num);
		if (is_direntry_le_ih(ih))
			leaf_copy_dir_entries(dest_bi, src, LAST_TO_FIRST,
					      item_num,
					      ih_entry_count(ih) - cpy_bytes,
					      cpy_bytes);
		else {
			struct item_head n_ih;

			/*
			 * copy part of the body of the item number 'item_num'
			 * of SOURCE to the begin of the DEST part defined by
			 * 'cpy_bytes'; create new item header;
			 * n_ih = new item_header;
			 */
			memcpy(&n_ih, ih, SHORT_KEY_SIZE);

			/* Endian safe, both le */
			n_ih.ih_version = ih->ih_version;

			if (is_direct_le_ih(ih)) {
				set_le_ih_k_offset(&n_ih,
						   le_ih_k_offset(ih) +
						   ih_item_len(ih) - cpy_bytes);
				set_le_ih_k_type(&n_ih, TYPE_DIRECT);
				set_ih_free_space(&n_ih, MAX_US_INT);
			} else {
				/* indirect item */
				RFALSE(!cpy_bytes && get_ih_free_space(ih),
				       "vs-10200: ih->ih_free_space must be 0 when indirect item will be appended");
				set_le_ih_k_offset(&n_ih,
						   le_ih_k_offset(ih) +
						   (ih_item_len(ih) -
						    cpy_bytes) / UNFM_P_SIZE *
						   dest->b_size);
				set_le_ih_k_type(&n_ih, TYPE_INDIRECT);
				set_ih_free_space(&n_ih, get_ih_free_space(ih));
			}

			/* set item length */
			put_ih_item_len(&n_ih, cpy_bytes);

			/* Endian safe, both le */
			n_ih.ih_version = ih->ih_version;

			leaf_insert_into_buf(dest_bi, 0, &n_ih,
					     item_body(src, item_num) +
						ih_item_len(ih) - cpy_bytes, 0);
		}
	}
}

/*
 * If cpy_bytes equals minus one than copy cpy_num whole items from SOURCE
 * to DEST.  If cpy_bytes not equal to minus one than copy cpy_num-1 whole
 * items from SOURCE to DEST.  From last item copy cpy_num bytes for regular
 * item and cpy_num directory entries for directory item.
 */
static int leaf_copy_items(struct buffer_info *dest_bi, struct buffer_head *src,
			   int last_first, int cpy_num, int cpy_bytes)
{
	struct buffer_head *dest;
	int pos, i, src_nr_item, bytes;

	dest = dest_bi->bi_bh;
	RFALSE(!dest || !src, "vs-10210: !dest || !src");
	RFALSE(last_first != FIRST_TO_LAST && last_first != LAST_TO_FIRST,
	       "vs-10220:last_first != FIRST_TO_LAST && last_first != LAST_TO_FIRST");
	RFALSE(B_NR_ITEMS(src) < cpy_num,
	       "vs-10230: No enough items: %d, req. %d", B_NR_ITEMS(src),
	       cpy_num);
	RFALSE(cpy_num < 0, "vs-10240: cpy_num < 0 (%d)", cpy_num);

	if (cpy_num == 0)
		return 0;

	if (last_first == FIRST_TO_LAST) {
		/* copy items to left */
		pos = 0;
		if (cpy_num == 1)
			bytes = cpy_bytes;
		else
			bytes = -1;

		/*
		 * copy the first item or it part or nothing to the end of
		 * the DEST (i = leaf_copy_boundary_item(DEST,SOURCE,0,bytes))
		 */
		i = leaf_copy_boundary_item(dest_bi, src, FIRST_TO_LAST, bytes);
		cpy_num -= i;
		if (cpy_num == 0)
			return i;
		pos += i;
		if (cpy_bytes == -1)
			/*
			 * copy first cpy_num items starting from position
			 * 'pos' of SOURCE to end of DEST
			 */
			leaf_copy_items_entirely(dest_bi, src, FIRST_TO_LAST,
						 pos, cpy_num);
		else {
			/*
			 * copy first cpy_num-1 items starting from position
			 * 'pos-1' of the SOURCE to the end of the DEST
			 */
			leaf_copy_items_entirely(dest_bi, src, FIRST_TO_LAST,
						 pos, cpy_num - 1);

			/*
			 * copy part of the item which number is
			 * cpy_num+pos-1 to the end of the DEST
			 */
			leaf_item_bottle(dest_bi, src, FIRST_TO_LAST,
					 cpy_num + pos - 1, cpy_bytes);
		}
	} else {
		/* copy items to right */
		src_nr_item = B_NR_ITEMS(src);
		if (cpy_num == 1)
			bytes = cpy_bytes;
		else
			bytes = -1;

		/*
		 * copy the last item or it part or nothing to the
		 * begin of the DEST
		 * (i = leaf_copy_boundary_item(DEST,SOURCE,1,bytes));
		 */
		i = leaf_copy_boundary_item(dest_bi, src, LAST_TO_FIRST, bytes);

		cpy_num -= i;
		if (cpy_num == 0)
			return i;

		pos = src_nr_item - cpy_num - i;
		if (cpy_bytes == -1) {
			/*
			 * starting from position 'pos' copy last cpy_num
			 * items of SOURCE to begin of DEST
			 */
			leaf_copy_items_entirely(dest_bi, src, LAST_TO_FIRST,
						 pos, cpy_num);
		} else {
			/*
			 * copy last cpy_num-1 items starting from position
			 * 'pos+1' of the SOURCE to the begin of the DEST;
			 */
			leaf_copy_items_entirely(dest_bi, src, LAST_TO_FIRST,
						 pos + 1, cpy_num - 1);

			/*
			 * copy part of the item which number is pos to
			 * the begin of the DEST
			 */
			leaf_item_bottle(dest_bi, src, LAST_TO_FIRST, pos,
					 cpy_bytes);
		}
	}
	return i;
}

/*
 * there are types of coping: from S[0] to L[0], from S[0] to R[0],
 * from R[0] to L[0]. for each of these we have to define parent and
 * positions of destination and source buffers
 */
static void leaf_define_dest_src_infos(int shift_mode, struct tree_balance *tb,
				       struct buffer_info *dest_bi,
				       struct buffer_info *src_bi,
				       int *first_last,
				       struct buffer_head *Snew)
{
	memset(dest_bi, 0, sizeof(struct buffer_info));
	memset(src_bi, 0, sizeof(struct buffer_info));

	/* define dest, src, dest parent, dest position */
	switch (shift_mode) {
	case LEAF_FROM_S_TO_L:	/* it is used in leaf_shift_left */
		src_bi->tb = tb;
		src_bi->bi_bh = PATH_PLAST_BUFFER(tb->tb_path);
		src_bi->bi_parent = PATH_H_PPARENT(tb->tb_path, 0);

		/* src->b_item_order */
		src_bi->bi_position = PATH_H_B_ITEM_ORDER(tb->tb_path, 0);
		dest_bi->tb = tb;
		dest_bi->bi_bh = tb->L[0];
		dest_bi->bi_parent = tb->FL[0];
		dest_bi->bi_position = get_left_neighbor_position(tb, 0);
		*first_last = FIRST_TO_LAST;
		break;

	case LEAF_FROM_S_TO_R:	/* it is used in leaf_shift_right */
		src_bi->tb = tb;
		src_bi->bi_bh = PATH_PLAST_BUFFER(tb->tb_path);
		src_bi->bi_parent = PATH_H_PPARENT(tb->tb_path, 0);
		src_bi->bi_position = PATH_H_B_ITEM_ORDER(tb->tb_path, 0);
		dest_bi->tb = tb;
		dest_bi->bi_bh = tb->R[0];
		dest_bi->bi_parent = tb->FR[0];
		dest_bi->bi_position = get_right_neighbor_position(tb, 0);
		*first_last = LAST_TO_FIRST;
		break;

	case LEAF_FROM_R_TO_L:	/* it is used in balance_leaf_when_delete */
		src_bi->tb = tb;
		src_bi->bi_bh = tb->R[0];
		src_bi->bi_parent = tb->FR[0];
		src_bi->bi_position = get_right_neighbor_position(tb, 0);
		dest_bi->tb = tb;
		dest_bi->bi_bh = tb->L[0];
		dest_bi->bi_parent = tb->FL[0];
		dest_bi->bi_position = get_left_neighbor_position(tb, 0);
		*first_last = FIRST_TO_LAST;
		break;

	case LEAF_FROM_L_TO_R:	/* it is used in balance_leaf_when_delete */
		src_bi->tb = tb;
		src_bi->bi_bh = tb->L[0];
		src_bi->bi_parent = tb->FL[0];
		src_bi->bi_position = get_left_neighbor_position(tb, 0);
		dest_bi->tb = tb;
		dest_bi->bi_bh = tb->R[0];
		dest_bi->bi_parent = tb->FR[0];
		dest_bi->bi_position = get_right_neighbor_position(tb, 0);
		*first_last = LAST_TO_FIRST;
		break;

	case LEAF_FROM_S_TO_SNEW:
		src_bi->tb = tb;
		src_bi->bi_bh = PATH_PLAST_BUFFER(tb->tb_path);
		src_bi->bi_parent = PATH_H_PPARENT(tb->tb_path, 0);
		src_bi->bi_position = PATH_H_B_ITEM_ORDER(tb->tb_path, 0);
		dest_bi->tb = tb;
		dest_bi->bi_bh = Snew;
		dest_bi->bi_parent = NULL;
		dest_bi->bi_position = 0;
		*first_last = LAST_TO_FIRST;
		break;

	default:
		reiserfs_panic(sb_from_bi(src_bi), "vs-10250",
			       "shift type is unknown (%d)", shift_mode);
	}
	RFALSE(!src_bi->bi_bh || !dest_bi->bi_bh,
	       "vs-10260: mode==%d, source (%p) or dest (%p) buffer is initialized incorrectly",
	       shift_mode, src_bi->bi_bh, dest_bi->bi_bh);
}

/*
 * copy mov_num items and mov_bytes of the (mov_num-1)th item to
 * neighbor. Delete them from source
 */
int leaf_move_items(int shift_mode, struct tree_balance *tb, int mov_num,
		    int mov_bytes, struct buffer_head *Snew)
{
	int ret_value;
	struct buffer_info dest_bi, src_bi;
	int first_last;

	leaf_define_dest_src_infos(shift_mode, tb, &dest_bi, &src_bi,
				   &first_last, Snew);

	ret_value =
	    leaf_copy_items(&dest_bi, src_bi.bi_bh, first_last, mov_num,
			    mov_bytes);

	leaf_delete_items(&src_bi, first_last,
			  (first_last ==
			   FIRST_TO_LAST) ? 0 : (B_NR_ITEMS(src_bi.bi_bh) -
						 mov_num), mov_num, mov_bytes);

	return ret_value;
}

/*
 * Shift shift_num items (and shift_bytes of last shifted item if
 * shift_bytes != -1) from S[0] to L[0] and replace the delimiting key
 */
int leaf_shift_left(struct tree_balance *tb, int shift_num, int shift_bytes)
{
	struct buffer_head *S0 = PATH_PLAST_BUFFER(tb->tb_path);
	int i;

	/*
	 * move shift_num (and shift_bytes bytes) items from S[0]
	 * to left neighbor L[0]
	 */
	i = leaf_move_items(LEAF_FROM_S_TO_L, tb, shift_num, shift_bytes, NULL);

	if (shift_num) {
		/* number of items in S[0] == 0 */
		if (B_NR_ITEMS(S0) == 0) {

			RFALSE(shift_bytes != -1,
			       "vs-10270: S0 is empty now, but shift_bytes != -1 (%d)",
			       shift_bytes);
#ifdef CONFIG_REISERFS_CHECK
			if (tb->tb_mode == M_PASTE || tb->tb_mode == M_INSERT) {
				print_cur_tb("vs-10275");
				reiserfs_panic(tb->tb_sb, "vs-10275",
					       "balance condition corrupted "
					       "(%c)", tb->tb_mode);
			}
#endif

			if (PATH_H_POSITION(tb->tb_path, 1) == 0)
				replace_key(tb, tb->CFL[0], tb->lkey[0],
					    PATH_H_PPARENT(tb->tb_path, 0), 0);

		} else {
			/* replace lkey in CFL[0] by 0-th key from S[0]; */
			replace_key(tb, tb->CFL[0], tb->lkey[0], S0, 0);

			RFALSE((shift_bytes != -1 &&
				!(is_direntry_le_ih(item_head(S0, 0))
				  && !ih_entry_count(item_head(S0, 0)))) &&
			       (!op_is_left_mergeable
				(leaf_key(S0, 0), S0->b_size)),
			       "vs-10280: item must be mergeable");
		}
	}

	return i;
}

/* CLEANING STOPPED HERE */

/*
 * Shift shift_num (shift_bytes) items from S[0] to the right neighbor,
 * and replace the delimiting key
 */
int leaf_shift_right(struct tree_balance *tb, int shift_num, int shift_bytes)
{
	int ret_value;

	/*
	 * move shift_num (and shift_bytes) items from S[0] to
	 * right neighbor R[0]
	 */
	ret_value =
	    leaf_move_items(LEAF_FROM_S_TO_R, tb, shift_num, shift_bytes, NULL);

	/* replace rkey in CFR[0] by the 0-th key from R[0] */
	if (shift_num) {
		replace_key(tb, tb->CFR[0], tb->rkey[0], tb->R[0], 0);

	}

	return ret_value;
}

static void leaf_delete_items_entirely(struct buffer_info *bi,
				       int first, int del_num);
/*
 * If del_bytes == -1, starting from position 'first' delete del_num
 * items in whole in buffer CUR.
 *   If not.
 *   If last_first == 0. Starting from position 'first' delete del_num-1
 *   items in whole. Delete part of body of the first item. Part defined by
 *   del_bytes. Don't delete first item header
 *   If last_first == 1. Starting from position 'first+1' delete del_num-1
 *   items in whole. Delete part of body of the last item . Part defined by
 *   del_bytes. Don't delete last item header.
*/
void leaf_delete_items(struct buffer_info *cur_bi, int last_first,
		       int first, int del_num, int del_bytes)
{
	struct buffer_head *bh;
	int item_amount = B_NR_ITEMS(bh = cur_bi->bi_bh);

	RFALSE(!bh, "10155: bh is not defined");
	RFALSE(del_num < 0, "10160: del_num can not be < 0. del_num==%d",
	       del_num);
	RFALSE(first < 0
	       || first + del_num > item_amount,
	       "10165: invalid number of first item to be deleted (%d) or "
	       "no so much items (%d) to delete (only %d)", first,
	       first + del_num, item_amount);

	if (del_num == 0)
		return;

	if (first == 0 && del_num == item_amount && del_bytes == -1) {
		make_empty_node(cur_bi);
		do_balance_mark_leaf_dirty(cur_bi->tb, bh, 0);
		return;
	}

	if (del_bytes == -1)
		/* delete del_num items beginning from item in position first */
		leaf_delete_items_entirely(cur_bi, first, del_num);
	else {
		if (last_first == FIRST_TO_LAST) {
			/*
			 * delete del_num-1 items beginning from
			 * item in position first
			 */
			leaf_delete_items_entirely(cur_bi, first, del_num - 1);

			/*
			 * delete the part of the first item of the bh
			 * do not delete item header
			 */
			leaf_cut_from_buffer(cur_bi, 0, 0, del_bytes);
		} else {
			struct item_head *ih;
			int len;

			/*
			 * delete del_num-1 items beginning from
			 * item in position first+1
			 */
			leaf_delete_items_entirely(cur_bi, first + 1,
						   del_num - 1);

			ih = item_head(bh, B_NR_ITEMS(bh) - 1);
			if (is_direntry_le_ih(ih))
				/* the last item is directory  */
				/*
				 * len = numbers of directory entries
				 * in this item
				 */
				len = ih_entry_count(ih);
			else
				/* len = body len of item */
				len = ih_item_len(ih);

			/*
			 * delete the part of the last item of the bh
			 * do not delete item header
			 */
			leaf_cut_from_buffer(cur_bi, B_NR_ITEMS(bh) - 1,
					     len - del_bytes, del_bytes);
		}
	}
}

/* insert item into the leaf node in position before */
void leaf_insert_into_buf(struct buffer_info *bi, int before,
			  struct item_head * const inserted_item_ih,
			  const char * const inserted_item_body,
			  int zeros_number)
{
	struct buffer_head *bh = bi->bi_bh;
	int nr, free_space;
	struct block_head *blkh;
	struct item_head *ih;
	int i;
	int last_loc, unmoved_loc;
	char *to;

	blkh = B_BLK_HEAD(bh);
	nr = blkh_nr_item(blkh);
	free_space = blkh_free_space(blkh);

	/* check free space */
	RFALSE(free_space < ih_item_len(inserted_item_ih) + IH_SIZE,
	       "vs-10170: not enough free space in block %z, new item %h",
	       bh, inserted_item_ih);
	RFALSE(zeros_number > ih_item_len(inserted_item_ih),
	       "vs-10172: zero number == %d, item length == %d",
	       zeros_number, ih_item_len(inserted_item_ih));

	/* get item new item must be inserted before */
	ih = item_head(bh, before);

	/* prepare space for the body of new item */
	last_loc = nr ? ih_location(&ih[nr - before - 1]) : bh->b_size;
	unmoved_loc = before ? ih_location(ih - 1) : bh->b_size;

	memmove(bh->b_data + last_loc - ih_item_len(inserted_item_ih),
		bh->b_data + last_loc, unmoved_loc - last_loc);

	to = bh->b_data + unmoved_loc - ih_item_len(inserted_item_ih);
	memset(to, 0, zeros_number);
	to += zeros_number;

	/* copy body to prepared space */
	if (inserted_item_body)
		memmove(to, inserted_item_body,
			ih_item_len(inserted_item_ih) - zeros_number);
	else
		memset(to, '\0', ih_item_len(inserted_item_ih) - zeros_number);

	/* insert item header */
	memmove(ih + 1, ih, IH_SIZE * (nr - before));
	memmove(ih, inserted_item_ih, IH_SIZE);

	/* change locations */
	for (i = before; i < nr + 1; i++) {
		unmoved_loc -= ih_item_len(&ih[i - before]);
		put_ih_location(&ih[i - before], unmoved_loc);
	}

	/* sizes, free space, item number */
	set_blkh_nr_item(blkh, blkh_nr_item(blkh) + 1);
	set_blkh_free_space(blkh,
			    free_space - (IH_SIZE +
					  ih_item_len(inserted_item_ih)));
	do_balance_mark_leaf_dirty(bi->tb, bh, 1);

	if (bi->bi_parent) {
		struct disk_child *t_dc;
		t_dc = B_N_CHILD(bi->bi_parent, bi->bi_position);
		put_dc_size(t_dc,
			    dc_size(t_dc) + (IH_SIZE +
					     ih_item_len(inserted_item_ih)));
		do_balance_mark_internal_dirty(bi->tb, bi->bi_parent, 0);
	}
}

/*
 * paste paste_size bytes to affected_item_num-th item.
 * When item is a directory, this only prepare space for new entries
 */
void leaf_paste_in_buffer(struct buffer_info *bi, int affected_item_num,
			  int pos_in_item, int paste_size,
			  const char *body, int zeros_number)
{
	struct buffer_head *bh = bi->bi_bh;
	int nr, free_space;
	struct block_head *blkh;
	struct item_head *ih;
	int i;
	int last_loc, unmoved_loc;

	blkh = B_BLK_HEAD(bh);
	nr = blkh_nr_item(blkh);
	free_space = blkh_free_space(blkh);

	/* check free space */
	RFALSE(free_space < paste_size,
	       "vs-10175: not enough free space: needed %d, available %d",
	       paste_size, free_space);

#ifdef CONFIG_REISERFS_CHECK
	if (zeros_number > paste_size) {
		struct super_block *sb = NULL;
		if (bi && bi->tb)
			sb = bi->tb->tb_sb;
		print_cur_tb("10177");
		reiserfs_panic(sb, "vs-10177",
			       "zeros_number == %d, paste_size == %d",
			       zeros_number, paste_size);
	}
#endif				/* CONFIG_REISERFS_CHECK */

	/* item to be appended */
	ih = item_head(bh, affected_item_num);

	last_loc = ih_location(&ih[nr - affected_item_num - 1]);
	unmoved_loc = affected_item_num ? ih_location(ih - 1) : bh->b_size;

	/* prepare space */
	memmove(bh->b_data + last_loc - paste_size, bh->b_data + last_loc,
		unmoved_loc - last_loc);

	/* change locations */
	for (i = affected_item_num; i < nr; i++)
		put_ih_location(&ih[i - affected_item_num],
				ih_location(&ih[i - affected_item_num]) -
				paste_size);

	if (body) {
		if (!is_direntry_le_ih(ih)) {
			if (!pos_in_item) {
				/* shift data to right */
				memmove(bh->b_data + ih_location(ih) +
					paste_size,
					bh->b_data + ih_location(ih),
					ih_item_len(ih));
				/* paste data in the head of item */
				memset(bh->b_data + ih_location(ih), 0,
				       zeros_number);
				memcpy(bh->b_data + ih_location(ih) +
				       zeros_number, body,
				       paste_size - zeros_number);
			} else {
				memset(bh->b_data + unmoved_loc - paste_size, 0,
				       zeros_number);
				memcpy(bh->b_data + unmoved_loc - paste_size +
				       zeros_number, body,
				       paste_size - zeros_number);
			}
		}
	} else
		memset(bh->b_data + unmoved_loc - paste_size, '\0', paste_size);

	put_ih_item_len(ih, ih_item_len(ih) + paste_size);

	/* change free space */
	set_blkh_free_space(blkh, free_space - paste_size);

	do_balance_mark_leaf_dirty(bi->tb, bh, 0);

	if (bi->bi_parent) {
		struct disk_child *t_dc =
		    B_N_CHILD(bi->bi_parent, bi->bi_position);
		put_dc_size(t_dc, dc_size(t_dc) + paste_size);
		do_balance_mark_internal_dirty(bi->tb, bi->bi_parent, 0);
	}
}

/*
 * cuts DEL_COUNT entries beginning from FROM-th entry. Directory item
 * does not have free space, so it moves DEHs and remaining records as
 * necessary. Return value is size of removed part of directory item
 * in bytes.
 */
static int leaf_cut_entries(struct buffer_head *bh,
			    struct item_head *ih, int from, int del_count)
{
	char *item;
	struct reiserfs_de_head *deh;
	int prev_record_offset;	/* offset of record, that is (from-1)th */
	char *prev_record;	/* */
	int cut_records_len;	/* length of all removed records */
	int i;

	/*
	 * make sure that item is directory and there are enough entries to
	 * remove
	 */
	RFALSE(!is_direntry_le_ih(ih), "10180: item is not directory item");
	RFALSE(ih_entry_count(ih) < from + del_count,
	       "10185: item contains not enough entries: entry_count = %d, from = %d, to delete = %d",
	       ih_entry_count(ih), from, del_count);

	if (del_count == 0)
		return 0;

	/* first byte of item */
	item = bh->b_data + ih_location(ih);

	/* entry head array */
	deh = B_I_DEH(bh, ih);

	/*
	 * first byte of remaining entries, those are BEFORE cut entries
	 * (prev_record) and length of all removed records (cut_records_len)
	 */
	prev_record_offset =
	    (from ? deh_location(&deh[from - 1]) : ih_item_len(ih));
	cut_records_len = prev_record_offset /*from_record */  -
	    deh_location(&deh[from + del_count - 1]);
	prev_record = item + prev_record_offset;

	/* adjust locations of remaining entries */
	for (i = ih_entry_count(ih) - 1; i > from + del_count - 1; i--)
		put_deh_location(&deh[i],
				 deh_location(&deh[i]) -
				 (DEH_SIZE * del_count));

	for (i = 0; i < from; i++)
		put_deh_location(&deh[i],
				 deh_location(&deh[i]) - (DEH_SIZE * del_count +
							  cut_records_len));

	put_ih_entry_count(ih, ih_entry_count(ih) - del_count);

	/* shift entry head array and entries those are AFTER removed entries */
	memmove((char *)(deh + from),
		deh + from + del_count,
		prev_record - cut_records_len - (char *)(deh + from +
							 del_count));

	/* shift records, those are BEFORE removed entries */
	memmove(prev_record - cut_records_len - DEH_SIZE * del_count,
		prev_record, item + ih_item_len(ih) - prev_record);

	return DEH_SIZE * del_count + cut_records_len;
}

/*
 * when cut item is part of regular file
 *      pos_in_item - first byte that must be cut
 *      cut_size - number of bytes to be cut beginning from pos_in_item
 *
 * when cut item is part of directory
 *      pos_in_item - number of first deleted entry
 *      cut_size - count of deleted entries
 */
void leaf_cut_from_buffer(struct buffer_info *bi, int cut_item_num,
			  int pos_in_item, int cut_size)
{
	int nr;
	struct buffer_head *bh = bi->bi_bh;
	struct block_head *blkh;
	struct item_head *ih;
	int last_loc, unmoved_loc;
	int i;

	blkh = B_BLK_HEAD(bh);
	nr = blkh_nr_item(blkh);

	/* item head of truncated item */
	ih = item_head(bh, cut_item_num);

	if (is_direntry_le_ih(ih)) {
		/* first cut entry () */
		cut_size = leaf_cut_entries(bh, ih, pos_in_item, cut_size);
		if (pos_in_item == 0) {
			/* change key */
			RFALSE(cut_item_num,
			       "when 0-th enrty of item is cut, that item must be first in the node, not %d-th",
			       cut_item_num);
			/* change item key by key of first entry in the item */
			set_le_ih_k_offset(ih, deh_offset(B_I_DEH(bh, ih)));
		}
	} else {
		/* item is direct or indirect */
		RFALSE(is_statdata_le_ih(ih), "10195: item is stat data");
		RFALSE(pos_in_item && pos_in_item + cut_size != ih_item_len(ih),
		       "10200: invalid offset (%lu) or trunc_size (%lu) or ih_item_len (%lu)",
		       (long unsigned)pos_in_item, (long unsigned)cut_size,
		       (long unsigned)ih_item_len(ih));

		/* shift item body to left if cut is from the head of item */
		if (pos_in_item == 0) {
			memmove(bh->b_data + ih_location(ih),
				bh->b_data + ih_location(ih) + cut_size,
				ih_item_len(ih) - cut_size);

			/* change key of item */
			if (is_direct_le_ih(ih))
				set_le_ih_k_offset(ih,
						   le_ih_k_offset(ih) +
						   cut_size);
			else {
				set_le_ih_k_offset(ih,
						   le_ih_k_offset(ih) +
						   (cut_size / UNFM_P_SIZE) *
						   bh->b_size);
				RFALSE(ih_item_len(ih) == cut_size
				       && get_ih_free_space(ih),
				       "10205: invalid ih_free_space (%h)", ih);
			}
		}
	}

	/* location of the last item */
	last_loc = ih_location(&ih[nr - cut_item_num - 1]);

	/* location of the item, which is remaining at the same place */
	unmoved_loc = cut_item_num ? ih_location(ih - 1) : bh->b_size;

	/* shift */
	memmove(bh->b_data + last_loc + cut_size, bh->b_data + last_loc,
		unmoved_loc - last_loc - cut_size);

	/* change item length */
	put_ih_item_len(ih, ih_item_len(ih) - cut_size);

	if (is_indirect_le_ih(ih)) {
		if (pos_in_item)
			set_ih_free_space(ih, 0);
	}

	/* change locations */
	for (i = cut_item_num; i < nr; i++)
		put_ih_location(&ih[i - cut_item_num],
				ih_location(&ih[i - cut_item_num]) + cut_size);

	/* size, free space */
	set_blkh_free_space(blkh, blkh_free_space(blkh) + cut_size);

	do_balance_mark_leaf_dirty(bi->tb, bh, 0);

	if (bi->bi_parent) {
		struct disk_child *t_dc;
		t_dc = B_N_CHILD(bi->bi_parent, bi->bi_position);
		put_dc_size(t_dc, dc_size(t_dc) - cut_size);
		do_balance_mark_internal_dirty(bi->tb, bi->bi_parent, 0);
	}
}

/* delete del_num items from buffer starting from the first'th item */
static void leaf_delete_items_entirely(struct buffer_info *bi,
				       int first, int del_num)
{
	struct buffer_head *bh = bi->bi_bh;
	int nr;
	int i, j;
	int last_loc, last_removed_loc;
	struct block_head *blkh;
	struct item_head *ih;

	RFALSE(bh == NULL, "10210: buffer is 0");
	RFALSE(del_num < 0, "10215: del_num less than 0 (%d)", del_num);

	if (del_num == 0)
		return;

	blkh = B_BLK_HEAD(bh);
	nr = blkh_nr_item(blkh);

	RFALSE(first < 0 || first + del_num > nr,
	       "10220: first=%d, number=%d, there is %d items", first, del_num,
	       nr);

	if (first == 0 && del_num == nr) {
		/* this does not work */
		make_empty_node(bi);

		do_balance_mark_leaf_dirty(bi->tb, bh, 0);
		return;
	}

	ih = item_head(bh, first);

	/* location of unmovable item */
	j = (first == 0) ? bh->b_size : ih_location(ih - 1);

	/* delete items */
	last_loc = ih_location(&ih[nr - 1 - first]);
	last_removed_loc = ih_location(&ih[del_num - 1]);

	memmove(bh->b_data + last_loc + j - last_removed_loc,
		bh->b_data + last_loc, last_removed_loc - last_loc);

	/* delete item headers */
	memmove(ih, ih + del_num, (nr - first - del_num) * IH_SIZE);

	/* change item location */
	for (i = first; i < nr - del_num; i++)
		put_ih_location(&ih[i - first],
				ih_location(&ih[i - first]) + (j -
								 last_removed_loc));

	/* sizes, item number */
	set_blkh_nr_item(blkh, blkh_nr_item(blkh) - del_num);
	set_blkh_free_space(blkh,
			    blkh_free_space(blkh) + (j - last_removed_loc +
						     IH_SIZE * del_num));

	do_balance_mark_leaf_dirty(bi->tb, bh, 0);

	if (bi->bi_parent) {
		struct disk_child *t_dc =
		    B_N_CHILD(bi->bi_parent, bi->bi_position);
		put_dc_size(t_dc,
			    dc_size(t_dc) - (j - last_removed_loc +
					     IH_SIZE * del_num));
		do_balance_mark_internal_dirty(bi->tb, bi->bi_parent, 0);
	}
}

/*
 * paste new_entry_count entries (new_dehs, records) into position
 * before to item_num-th item
 */
void leaf_paste_entries(struct buffer_info *bi,
			int item_num,
			int before,
			int new_entry_count,
			struct reiserfs_de_head *new_dehs,
			const char *records, int paste_size)
{
	struct item_head *ih;
	char *item;
	struct reiserfs_de_head *deh;
	char *insert_point;
	int i, old_entry_num;
	struct buffer_head *bh = bi->bi_bh;

	if (new_entry_count == 0)
		return;

	ih = item_head(bh, item_num);

	/*
	 * make sure, that item is directory, and there are enough
	 * records in it
	 */
	RFALSE(!is_direntry_le_ih(ih), "10225: item is not directory item");
	RFALSE(ih_entry_count(ih) < before,
	       "10230: there are no entry we paste entries before. entry_count = %d, before = %d",
	       ih_entry_count(ih), before);

	/* first byte of dest item */
	item = bh->b_data + ih_location(ih);

	/* entry head array */
	deh = B_I_DEH(bh, ih);

	/* new records will be pasted at this point */
	insert_point =
	    item +
	    (before ? deh_location(&deh[before - 1])
	     : (ih_item_len(ih) - paste_size));

	/* adjust locations of records that will be AFTER new records */
	for (i = ih_entry_count(ih) - 1; i >= before; i--)
		put_deh_location(&deh[i],
				 deh_location(&deh[i]) +
				 (DEH_SIZE * new_entry_count));

	/* adjust locations of records that will be BEFORE new records */
	for (i = 0; i < before; i++)
		put_deh_location(&deh[i],
				 deh_location(&deh[i]) + paste_size);

	old_entry_num = ih_entry_count(ih);
	put_ih_entry_count(ih, ih_entry_count(ih) + new_entry_count);

	/* prepare space for pasted records */
	memmove(insert_point + paste_size, insert_point,
		item + (ih_item_len(ih) - paste_size) - insert_point);

	/* copy new records */
	memcpy(insert_point + DEH_SIZE * new_entry_count, records,
	       paste_size - DEH_SIZE * new_entry_count);

	/* prepare space for new entry heads */
	deh += before;
	memmove((char *)(deh + new_entry_count), deh,
		insert_point - (char *)deh);

	/* copy new entry heads */
	deh = (struct reiserfs_de_head *)((char *)deh);
	memcpy(deh, new_dehs, DEH_SIZE * new_entry_count);

	/* set locations of new records */
	for (i = 0; i < new_entry_count; i++) {
		put_deh_location(&deh[i],
				 deh_location(&deh[i]) +
				 (-deh_location
				  (&new_dehs[new_entry_count - 1]) +
				  insert_point + DEH_SIZE * new_entry_count -
				  item));
	}

	/* change item key if necessary (when we paste before 0-th entry */
	if (!before) {
		set_le_ih_k_offset(ih, deh_offset(new_dehs));
	}
#ifdef CONFIG_REISERFS_CHECK
	{
		int prev, next;
		/* check record locations */
		deh = B_I_DEH(bh, ih);
		for (i = 0; i < ih_entry_count(ih); i++) {
			next =
			    (i <
			     ih_entry_count(ih) -
			     1) ? deh_location(&deh[i + 1]) : 0;
			prev = (i != 0) ? deh_location(&deh[i - 1]) : 0;

			if (prev && prev <= deh_location(&deh[i]))
				reiserfs_error(sb_from_bi(bi), "vs-10240",
					       "directory item (%h) "
					       "corrupted (prev %a, "
					       "cur(%d) %a)",
					       ih, deh + i - 1, i, deh + i);
			if (next && next >= deh_location(&deh[i]))
				reiserfs_error(sb_from_bi(bi), "vs-10250",
					       "directory item (%h) "
					       "corrupted (cur(%d) %a, "
					       "next %a)",
					       ih, i, deh + i, deh + i + 1);
		}
	}
#endif

}
