/*
 *  Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README
 */

/*
 *  Written by Anatoly P. Pinchuk pap@namesys.botik.ru
 *  Programm System Institute
 *  Pereslavl-Zalessky Russia
 */

#include <linux/time.h>
#include <linux/string.h>
#include <linux/pagemap.h>
#include <linux/bio.h>
#include "reiserfs.h"
#include <linux/buffer_head.h>
#include <linux/quotaops.h>

/* Does the buffer contain a disk block which is in the tree. */
inline int B_IS_IN_TREE(const struct buffer_head *bh)
{

	RFALSE(B_LEVEL(bh) > MAX_HEIGHT,
	       "PAP-1010: block (%b) has too big level (%z)", bh, bh);

	return (B_LEVEL(bh) != FREE_LEVEL);
}

/* to get item head in le form */
inline void copy_item_head(struct item_head *to,
			   const struct item_head *from)
{
	memcpy(to, from, IH_SIZE);
}

/*
 * k1 is pointer to on-disk structure which is stored in little-endian
 * form. k2 is pointer to cpu variable. For key of items of the same
 * object this returns 0.
 * Returns: -1 if key1 < key2
 * 0 if key1 == key2
 * 1 if key1 > key2
 */
inline int comp_short_keys(const struct reiserfs_key *le_key,
			   const struct cpu_key *cpu_key)
{
	__u32 n;
	n = le32_to_cpu(le_key->k_dir_id);
	if (n < cpu_key->on_disk_key.k_dir_id)
		return -1;
	if (n > cpu_key->on_disk_key.k_dir_id)
		return 1;
	n = le32_to_cpu(le_key->k_objectid);
	if (n < cpu_key->on_disk_key.k_objectid)
		return -1;
	if (n > cpu_key->on_disk_key.k_objectid)
		return 1;
	return 0;
}

/*
 * k1 is pointer to on-disk structure which is stored in little-endian
 * form. k2 is pointer to cpu variable.
 * Compare keys using all 4 key fields.
 * Returns: -1 if key1 < key2 0
 * if key1 = key2 1 if key1 > key2
 */
static inline int comp_keys(const struct reiserfs_key *le_key,
			    const struct cpu_key *cpu_key)
{
	int retval;

	retval = comp_short_keys(le_key, cpu_key);
	if (retval)
		return retval;
	if (le_key_k_offset(le_key_version(le_key), le_key) <
	    cpu_key_k_offset(cpu_key))
		return -1;
	if (le_key_k_offset(le_key_version(le_key), le_key) >
	    cpu_key_k_offset(cpu_key))
		return 1;

	if (cpu_key->key_length == 3)
		return 0;

	/* this part is needed only when tail conversion is in progress */
	if (le_key_k_type(le_key_version(le_key), le_key) <
	    cpu_key_k_type(cpu_key))
		return -1;

	if (le_key_k_type(le_key_version(le_key), le_key) >
	    cpu_key_k_type(cpu_key))
		return 1;

	return 0;
}

inline int comp_short_le_keys(const struct reiserfs_key *key1,
			      const struct reiserfs_key *key2)
{
	__u32 *k1_u32, *k2_u32;
	int key_length = REISERFS_SHORT_KEY_LEN;

	k1_u32 = (__u32 *) key1;
	k2_u32 = (__u32 *) key2;
	for (; key_length--; ++k1_u32, ++k2_u32) {
		if (le32_to_cpu(*k1_u32) < le32_to_cpu(*k2_u32))
			return -1;
		if (le32_to_cpu(*k1_u32) > le32_to_cpu(*k2_u32))
			return 1;
	}
	return 0;
}

inline void le_key2cpu_key(struct cpu_key *to, const struct reiserfs_key *from)
{
	int version;
	to->on_disk_key.k_dir_id = le32_to_cpu(from->k_dir_id);
	to->on_disk_key.k_objectid = le32_to_cpu(from->k_objectid);

	/* find out version of the key */
	version = le_key_version(from);
	to->version = version;
	to->on_disk_key.k_offset = le_key_k_offset(version, from);
	to->on_disk_key.k_type = le_key_k_type(version, from);
}

/*
 * this does not say which one is bigger, it only returns 1 if keys
 * are not equal, 0 otherwise
 */
inline int comp_le_keys(const struct reiserfs_key *k1,
			const struct reiserfs_key *k2)
{
	return memcmp(k1, k2, sizeof(struct reiserfs_key));
}

/**************************************************************************
 *  Binary search toolkit function                                        *
 *  Search for an item in the array by the item key                       *
 *  Returns:    1 if found,  0 if not found;                              *
 *        *pos = number of the searched element if found, else the        *
 *        number of the first element that is larger than key.            *
 **************************************************************************/
/*
 * For those not familiar with binary search: lbound is the leftmost item
 * that it could be, rbound the rightmost item that it could be.  We examine
 * the item halfway between lbound and rbound, and that tells us either
 * that we can increase lbound, or decrease rbound, or that we have found it,
 * or if lbound <= rbound that there are no possible items, and we have not
 * found it. With each examination we cut the number of possible items it
 * could be by one more than half rounded down, or we find it.
 */
static inline int bin_search(const void *key,	/* Key to search for. */
			     const void *base,	/* First item in the array. */
			     int num,	/* Number of items in the array. */
			     /*
			      * Item size in the array.  searched. Lest the
			      * reader be confused, note that this is crafted
			      * as a general function, and when it is applied
			      * specifically to the array of item headers in a
			      * node, width is actually the item header size
			      * not the item size.
			      */
			     int width,
			     int *pos /* Number of the searched for element. */
    )
{
	int rbound, lbound, j;

	for (j = ((rbound = num - 1) + (lbound = 0)) / 2;
	     lbound <= rbound; j = (rbound + lbound) / 2)
		switch (comp_keys
			((struct reiserfs_key *)((char *)base + j * width),
			 (struct cpu_key *)key)) {
		case -1:
			lbound = j + 1;
			continue;
		case 1:
			rbound = j - 1;
			continue;
		case 0:
			*pos = j;
			return ITEM_FOUND;	/* Key found in the array.  */
		}

	/*
	 * bin_search did not find given key, it returns position of key,
	 * that is minimal and greater than the given one.
	 */
	*pos = lbound;
	return ITEM_NOT_FOUND;
}


/* Minimal possible key. It is never in the tree. */
const struct reiserfs_key MIN_KEY = { 0, 0, {{0, 0},} };

/* Maximal possible key. It is never in the tree. */
static const struct reiserfs_key MAX_KEY = {
	cpu_to_le32(0xffffffff),
	cpu_to_le32(0xffffffff),
	{{cpu_to_le32(0xffffffff),
	  cpu_to_le32(0xffffffff)},}
};

/*
 * Get delimiting key of the buffer by looking for it in the buffers in the
 * path, starting from the bottom of the path, and going upwards.  We must
 * check the path's validity at each step.  If the key is not in the path,
 * there is no delimiting key in the tree (buffer is first or last buffer
 * in tree), and in this case we return a special key, either MIN_KEY or
 * MAX_KEY.
 */
static inline const struct reiserfs_key *get_lkey(const struct treepath *chk_path,
						  const struct super_block *sb)
{
	int position, path_offset = chk_path->path_length;
	struct buffer_head *parent;

	RFALSE(path_offset < FIRST_PATH_ELEMENT_OFFSET,
	       "PAP-5010: invalid offset in the path");

	/* While not higher in path than first element. */
	while (path_offset-- > FIRST_PATH_ELEMENT_OFFSET) {

		RFALSE(!buffer_uptodate
		       (PATH_OFFSET_PBUFFER(chk_path, path_offset)),
		       "PAP-5020: parent is not uptodate");

		/* Parent at the path is not in the tree now. */
		if (!B_IS_IN_TREE
		    (parent =
		     PATH_OFFSET_PBUFFER(chk_path, path_offset)))
			return &MAX_KEY;
		/* Check whether position in the parent is correct. */
		if ((position =
		     PATH_OFFSET_POSITION(chk_path,
					  path_offset)) >
		    B_NR_ITEMS(parent))
			return &MAX_KEY;
		/* Check whether parent at the path really points to the child. */
		if (B_N_CHILD_NUM(parent, position) !=
		    PATH_OFFSET_PBUFFER(chk_path,
					path_offset + 1)->b_blocknr)
			return &MAX_KEY;
		/*
		 * Return delimiting key if position in the parent
		 * is not equal to zero.
		 */
		if (position)
			return internal_key(parent, position - 1);
	}
	/* Return MIN_KEY if we are in the root of the buffer tree. */
	if (PATH_OFFSET_PBUFFER(chk_path, FIRST_PATH_ELEMENT_OFFSET)->
	    b_blocknr == SB_ROOT_BLOCK(sb))
		return &MIN_KEY;
	return &MAX_KEY;
}

/* Get delimiting key of the buffer at the path and its right neighbor. */
inline const struct reiserfs_key *get_rkey(const struct treepath *chk_path,
					   const struct super_block *sb)
{
	int position, path_offset = chk_path->path_length;
	struct buffer_head *parent;

	RFALSE(path_offset < FIRST_PATH_ELEMENT_OFFSET,
	       "PAP-5030: invalid offset in the path");

	while (path_offset-- > FIRST_PATH_ELEMENT_OFFSET) {

		RFALSE(!buffer_uptodate
		       (PATH_OFFSET_PBUFFER(chk_path, path_offset)),
		       "PAP-5040: parent is not uptodate");

		/* Parent at the path is not in the tree now. */
		if (!B_IS_IN_TREE
		    (parent =
		     PATH_OFFSET_PBUFFER(chk_path, path_offset)))
			return &MIN_KEY;
		/* Check whether position in the parent is correct. */
		if ((position =
		     PATH_OFFSET_POSITION(chk_path,
					  path_offset)) >
		    B_NR_ITEMS(parent))
			return &MIN_KEY;
		/*
		 * Check whether parent at the path really points
		 * to the child.
		 */
		if (B_N_CHILD_NUM(parent, position) !=
		    PATH_OFFSET_PBUFFER(chk_path,
					path_offset + 1)->b_blocknr)
			return &MIN_KEY;

		/*
		 * Return delimiting key if position in the parent
		 * is not the last one.
		 */
		if (position != B_NR_ITEMS(parent))
			return internal_key(parent, position);
	}

	/* Return MAX_KEY if we are in the root of the buffer tree. */
	if (PATH_OFFSET_PBUFFER(chk_path, FIRST_PATH_ELEMENT_OFFSET)->
	    b_blocknr == SB_ROOT_BLOCK(sb))
		return &MAX_KEY;
	return &MIN_KEY;
}

/*
 * Check whether a key is contained in the tree rooted from a buffer at a path.
 * This works by looking at the left and right delimiting keys for the buffer
 * in the last path_element in the path.  These delimiting keys are stored
 * at least one level above that buffer in the tree. If the buffer is the
 * first or last node in the tree order then one of the delimiting keys may
 * be absent, and in this case get_lkey and get_rkey return a special key
 * which is MIN_KEY or MAX_KEY.
 */
static inline int key_in_buffer(
				/* Path which should be checked. */
				struct treepath *chk_path,
				/* Key which should be checked. */
				const struct cpu_key *key,
				struct super_block *sb
    )
{

	RFALSE(!key || chk_path->path_length < FIRST_PATH_ELEMENT_OFFSET
	       || chk_path->path_length > MAX_HEIGHT,
	       "PAP-5050: pointer to the key(%p) is NULL or invalid path length(%d)",
	       key, chk_path->path_length);
	RFALSE(!PATH_PLAST_BUFFER(chk_path)->b_bdev,
	       "PAP-5060: device must not be NODEV");

	if (comp_keys(get_lkey(chk_path, sb), key) == 1)
		/* left delimiting key is bigger, that the key we look for */
		return 0;
	/*  if ( comp_keys(key, get_rkey(chk_path, sb)) != -1 ) */
	if (comp_keys(get_rkey(chk_path, sb), key) != 1)
		/* key must be less than right delimitiing key */
		return 0;
	return 1;
}

int reiserfs_check_path(struct treepath *p)
{
	RFALSE(p->path_length != ILLEGAL_PATH_ELEMENT_OFFSET,
	       "path not properly relsed");
	return 0;
}

/*
 * Drop the reference to each buffer in a path and restore
 * dirty bits clean when preparing the buffer for the log.
 * This version should only be called from fix_nodes()
 */
void pathrelse_and_restore(struct super_block *sb,
			   struct treepath *search_path)
{
	int path_offset = search_path->path_length;

	RFALSE(path_offset < ILLEGAL_PATH_ELEMENT_OFFSET,
	       "clm-4000: invalid path offset");

	while (path_offset > ILLEGAL_PATH_ELEMENT_OFFSET) {
		struct buffer_head *bh;
		bh = PATH_OFFSET_PBUFFER(search_path, path_offset--);
		reiserfs_restore_prepared_buffer(sb, bh);
		brelse(bh);
	}
	search_path->path_length = ILLEGAL_PATH_ELEMENT_OFFSET;
}

/* Drop the reference to each buffer in a path */
void pathrelse(struct treepath *search_path)
{
	int path_offset = search_path->path_length;

	RFALSE(path_offset < ILLEGAL_PATH_ELEMENT_OFFSET,
	       "PAP-5090: invalid path offset");

	while (path_offset > ILLEGAL_PATH_ELEMENT_OFFSET)
		brelse(PATH_OFFSET_PBUFFER(search_path, path_offset--));

	search_path->path_length = ILLEGAL_PATH_ELEMENT_OFFSET;
}

static int has_valid_deh_location(struct buffer_head *bh, struct item_head *ih)
{
	struct reiserfs_de_head *deh;
	int i;

	deh = B_I_DEH(bh, ih);
	for (i = 0; i < ih_entry_count(ih); i++) {
		if (deh_location(&deh[i]) > ih_item_len(ih)) {
			reiserfs_warning(NULL, "reiserfs-5094",
					 "directory entry location seems wrong %h",
					 &deh[i]);
			return 0;
		}
	}

	return 1;
}

static int is_leaf(char *buf, int blocksize, struct buffer_head *bh)
{
	struct block_head *blkh;
	struct item_head *ih;
	int used_space;
	int prev_location;
	int i;
	int nr;

	blkh = (struct block_head *)buf;
	if (blkh_level(blkh) != DISK_LEAF_NODE_LEVEL) {
		reiserfs_warning(NULL, "reiserfs-5080",
				 "this should be caught earlier");
		return 0;
	}

	nr = blkh_nr_item(blkh);
	if (nr < 1 || nr > ((blocksize - BLKH_SIZE) / (IH_SIZE + MIN_ITEM_LEN))) {
		/* item number is too big or too small */
		reiserfs_warning(NULL, "reiserfs-5081",
				 "nr_item seems wrong: %z", bh);
		return 0;
	}
	ih = (struct item_head *)(buf + BLKH_SIZE) + nr - 1;
	used_space = BLKH_SIZE + IH_SIZE * nr + (blocksize - ih_location(ih));

	/* free space does not match to calculated amount of use space */
	if (used_space != blocksize - blkh_free_space(blkh)) {
		reiserfs_warning(NULL, "reiserfs-5082",
				 "free space seems wrong: %z", bh);
		return 0;
	}
	/*
	 * FIXME: it is_leaf will hit performance too much - we may have
	 * return 1 here
	 */

	/* check tables of item heads */
	ih = (struct item_head *)(buf + BLKH_SIZE);
	prev_location = blocksize;
	for (i = 0; i < nr; i++, ih++) {
		if (le_ih_k_type(ih) == TYPE_ANY) {
			reiserfs_warning(NULL, "reiserfs-5083",
					 "wrong item type for item %h",
					 ih);
			return 0;
		}
		if (ih_location(ih) >= blocksize
		    || ih_location(ih) < IH_SIZE * nr) {
			reiserfs_warning(NULL, "reiserfs-5084",
					 "item location seems wrong: %h",
					 ih);
			return 0;
		}
		if (ih_item_len(ih) < 1
		    || ih_item_len(ih) > MAX_ITEM_LEN(blocksize)) {
			reiserfs_warning(NULL, "reiserfs-5085",
					 "item length seems wrong: %h",
					 ih);
			return 0;
		}
		if (prev_location - ih_location(ih) != ih_item_len(ih)) {
			reiserfs_warning(NULL, "reiserfs-5086",
					 "item location seems wrong "
					 "(second one): %h", ih);
			return 0;
		}
		if (is_direntry_le_ih(ih)) {
			if (ih_item_len(ih) < (ih_entry_count(ih) * IH_SIZE)) {
				reiserfs_warning(NULL, "reiserfs-5093",
						 "item entry count seems wrong %h",
						 ih);
				return 0;
			}
			return has_valid_deh_location(bh, ih);
		}
		prev_location = ih_location(ih);
	}

	/* one may imagine many more checks */
	return 1;
}

/* returns 1 if buf looks like an internal node, 0 otherwise */
static int is_internal(char *buf, int blocksize, struct buffer_head *bh)
{
	struct block_head *blkh;
	int nr;
	int used_space;

	blkh = (struct block_head *)buf;
	nr = blkh_level(blkh);
	if (nr <= DISK_LEAF_NODE_LEVEL || nr > MAX_HEIGHT) {
		/* this level is not possible for internal nodes */
		reiserfs_warning(NULL, "reiserfs-5087",
				 "this should be caught earlier");
		return 0;
	}

	nr = blkh_nr_item(blkh);
	/* for internal which is not root we might check min number of keys */
	if (nr > (blocksize - BLKH_SIZE - DC_SIZE) / (KEY_SIZE + DC_SIZE)) {
		reiserfs_warning(NULL, "reiserfs-5088",
				 "number of key seems wrong: %z", bh);
		return 0;
	}

	used_space = BLKH_SIZE + KEY_SIZE * nr + DC_SIZE * (nr + 1);
	if (used_space != blocksize - blkh_free_space(blkh)) {
		reiserfs_warning(NULL, "reiserfs-5089",
				 "free space seems wrong: %z", bh);
		return 0;
	}

	/* one may imagine many more checks */
	return 1;
}

/*
 * make sure that bh contains formatted node of reiserfs tree of
 * 'level'-th level
 */
static int is_tree_node(struct buffer_head *bh, int level)
{
	if (B_LEVEL(bh) != level) {
		reiserfs_warning(NULL, "reiserfs-5090", "node level %d does "
				 "not match to the expected one %d",
				 B_LEVEL(bh), level);
		return 0;
	}
	if (level == DISK_LEAF_NODE_LEVEL)
		return is_leaf(bh->b_data, bh->b_size, bh);

	return is_internal(bh->b_data, bh->b_size, bh);
}

#define SEARCH_BY_KEY_READA 16

/*
 * The function is NOT SCHEDULE-SAFE!
 * It might unlock the write lock if we needed to wait for a block
 * to be read. Note that in this case it won't recover the lock to avoid
 * high contention resulting from too much lock requests, especially
 * the caller (search_by_key) will perform other schedule-unsafe
 * operations just after calling this function.
 *
 * @return depth of lock to be restored after read completes
 */
static int search_by_key_reada(struct super_block *s,
				struct buffer_head **bh,
				b_blocknr_t *b, int num)
{
	int i, j;
	int depth = -1;

	for (i = 0; i < num; i++) {
		bh[i] = sb_getblk(s, b[i]);
	}
	/*
	 * We are going to read some blocks on which we
	 * have a reference. It's safe, though we might be
	 * reading blocks concurrently changed if we release
	 * the lock. But it's still fine because we check later
	 * if the tree changed
	 */
	for (j = 0; j < i; j++) {
		/*
		 * note, this needs attention if we are getting rid of the BKL
		 * you have to make sure the prepared bit isn't set on this
		 * buffer
		 */
		if (!buffer_uptodate(bh[j])) {
			if (depth == -1)
				depth = reiserfs_write_unlock_nested(s);
			bh_readahead(bh[j], REQ_RAHEAD);
		}
		brelse(bh[j]);
	}
	return depth;
}

/*
 * This function fills up the path from the root to the leaf as it
 * descends the tree looking for the key.  It uses reiserfs_bread to
 * try to find buffers in the cache given their block number.  If it
 * does not find them in the cache it reads them from disk.  For each
 * node search_by_key finds using reiserfs_bread it then uses
 * bin_search to look through that node.  bin_search will find the
 * position of the block_number of the next node if it is looking
 * through an internal node.  If it is looking through a leaf node
 * bin_search will find the position of the item which has key either
 * equal to given key, or which is the maximal key less than the given
 * key.  search_by_key returns a path that must be checked for the
 * correctness of the top of the path but need not be checked for the
 * correctness of the bottom of the path
 */
/*
 * search_by_key - search for key (and item) in stree
 * @sb: superblock
 * @key: pointer to key to search for
 * @search_path: Allocated and initialized struct treepath; Returned filled
 *		 on success.
 * @stop_level: How far down the tree to search, Use DISK_LEAF_NODE_LEVEL to
 *		stop at leaf level.
 *
 * The function is NOT SCHEDULE-SAFE!
 */
int search_by_key(struct super_block *sb, const struct cpu_key *key,
		  struct treepath *search_path, int stop_level)
{
	b_blocknr_t block_number;
	int expected_level;
	struct buffer_head *bh;
	struct path_element *last_element;
	int node_level, retval;
	int fs_gen;
	struct buffer_head *reada_bh[SEARCH_BY_KEY_READA];
	b_blocknr_t reada_blocks[SEARCH_BY_KEY_READA];
	int reada_count = 0;

#ifdef CONFIG_REISERFS_CHECK
	int repeat_counter = 0;
#endif

	PROC_INFO_INC(sb, search_by_key);

	/*
	 * As we add each node to a path we increase its count.  This means
	 * that we must be careful to release all nodes in a path before we
	 * either discard the path struct or re-use the path struct, as we
	 * do here.
	 */

	pathrelse(search_path);

	/*
	 * With each iteration of this loop we search through the items in the
	 * current node, and calculate the next current node(next path element)
	 * for the next iteration of this loop..
	 */
	block_number = SB_ROOT_BLOCK(sb);
	expected_level = -1;
	while (1) {

#ifdef CONFIG_REISERFS_CHECK
		if (!(++repeat_counter % 50000))
			reiserfs_warning(sb, "PAP-5100",
					 "%s: there were %d iterations of "
					 "while loop looking for key %K",
					 current->comm, repeat_counter,
					 key);
#endif

		/* prep path to have another element added to it. */
		last_element =
		    PATH_OFFSET_PELEMENT(search_path,
					 ++search_path->path_length);
		fs_gen = get_generation(sb);

		/*
		 * Read the next tree node, and set the last element
		 * in the path to have a pointer to it.
		 */
		if ((bh = last_element->pe_buffer =
		     sb_getblk(sb, block_number))) {

			/*
			 * We'll need to drop the lock if we encounter any
			 * buffers that need to be read. If all of them are
			 * already up to date, we don't need to drop the lock.
			 */
			int depth = -1;

			if (!buffer_uptodate(bh) && reada_count > 1)
				depth = search_by_key_reada(sb, reada_bh,
						    reada_blocks, reada_count);

			if (!buffer_uptodate(bh) && depth == -1)
				depth = reiserfs_write_unlock_nested(sb);

			bh_read_nowait(bh, 0);
			wait_on_buffer(bh);

			if (depth != -1)
				reiserfs_write_lock_nested(sb, depth);
			if (!buffer_uptodate(bh))
				goto io_error;
		} else {
io_error:
			search_path->path_length--;
			pathrelse(search_path);
			return IO_ERROR;
		}
		reada_count = 0;
		if (expected_level == -1)
			expected_level = SB_TREE_HEIGHT(sb);
		expected_level--;

		/*
		 * It is possible that schedule occurred. We must check
		 * whether the key to search is still in the tree rooted
		 * from the current buffer. If not then repeat search
		 * from the root.
		 */
		if (fs_changed(fs_gen, sb) &&
		    (!B_IS_IN_TREE(bh) ||
		     B_LEVEL(bh) != expected_level ||
		     !key_in_buffer(search_path, key, sb))) {
			PROC_INFO_INC(sb, search_by_key_fs_changed);
			PROC_INFO_INC(sb, search_by_key_restarted);
			PROC_INFO_INC(sb,
				      sbk_restarted[expected_level - 1]);
			pathrelse(search_path);

			/*
			 * Get the root block number so that we can
			 * repeat the search starting from the root.
			 */
			block_number = SB_ROOT_BLOCK(sb);
			expected_level = -1;

			/* repeat search from the root */
			continue;
		}

		/*
		 * only check that the key is in the buffer if key is not
		 * equal to the MAX_KEY. Latter case is only possible in
		 * "finish_unfinished()" processing during mount.
		 */
		RFALSE(comp_keys(&MAX_KEY, key) &&
		       !key_in_buffer(search_path, key, sb),
		       "PAP-5130: key is not in the buffer");
#ifdef CONFIG_REISERFS_CHECK
		if (REISERFS_SB(sb)->cur_tb) {
			print_cur_tb("5140");
			reiserfs_panic(sb, "PAP-5140",
				       "schedule occurred in do_balance!");
		}
#endif

		/*
		 * make sure, that the node contents look like a node of
		 * certain level
		 */
		if (!is_tree_node(bh, expected_level)) {
			reiserfs_error(sb, "vs-5150",
				       "invalid format found in block %ld. "
				       "Fsck?", bh->b_blocknr);
			pathrelse(search_path);
			return IO_ERROR;
		}

		/* ok, we have acquired next formatted node in the tree */
		node_level = B_LEVEL(bh);

		PROC_INFO_BH_STAT(sb, bh, node_level - 1);

		RFALSE(node_level < stop_level,
		       "vs-5152: tree level (%d) is less than stop level (%d)",
		       node_level, stop_level);

		retval = bin_search(key, item_head(bh, 0),
				      B_NR_ITEMS(bh),
				      (node_level ==
				       DISK_LEAF_NODE_LEVEL) ? IH_SIZE :
				      KEY_SIZE,
				      &last_element->pe_position);
		if (node_level == stop_level) {
			return retval;
		}

		/* we are not in the stop level */
		/*
		 * item has been found, so we choose the pointer which
		 * is to the right of the found one
		 */
		if (retval == ITEM_FOUND)
			last_element->pe_position++;

		/*
		 * if item was not found we choose the position which is to
		 * the left of the found item. This requires no code,
		 * bin_search did it already.
		 */

		/*
		 * So we have chosen a position in the current node which is
		 * an internal node.  Now we calculate child block number by
		 * position in the node.
		 */
		block_number =
		    B_N_CHILD_NUM(bh, last_element->pe_position);

		/*
		 * if we are going to read leaf nodes, try for read
		 * ahead as well
		 */
		if ((search_path->reada & PATH_READA) &&
		    node_level == DISK_LEAF_NODE_LEVEL + 1) {
			int pos = last_element->pe_position;
			int limit = B_NR_ITEMS(bh);
			struct reiserfs_key *le_key;

			if (search_path->reada & PATH_READA_BACK)
				limit = 0;
			while (reada_count < SEARCH_BY_KEY_READA) {
				if (pos == limit)
					break;
				reada_blocks[reada_count++] =
				    B_N_CHILD_NUM(bh, pos);
				if (search_path->reada & PATH_READA_BACK)
					pos--;
				else
					pos++;

				/*
				 * check to make sure we're in the same object
				 */
				le_key = internal_key(bh, pos);
				if (le32_to_cpu(le_key->k_objectid) !=
				    key->on_disk_key.k_objectid) {
					break;
				}
			}
		}
	}
}

/*
 * Form the path to an item and position in this item which contains
 * file byte defined by key. If there is no such item
 * corresponding to the key, we point the path to the item with
 * maximal key less than key, and *pos_in_item is set to one
 * past the last entry/byte in the item.  If searching for entry in a
 * directory item, and it is not found, *pos_in_item is set to one
 * entry more than the entry with maximal key which is less than the
 * sought key.
 *
 * Note that if there is no entry in this same node which is one more,
 * then we point to an imaginary entry.  for direct items, the
 * position is in units of bytes, for indirect items the position is
 * in units of blocknr entries, for directory items the position is in
 * units of directory entries.
 */
/* The function is NOT SCHEDULE-SAFE! */
int search_for_position_by_key(struct super_block *sb,
			       /* Key to search (cpu variable) */
			       const struct cpu_key *p_cpu_key,
			       /* Filled up by this function. */
			       struct treepath *search_path)
{
	struct item_head *p_le_ih;	/* pointer to on-disk structure */
	int blk_size;
	loff_t item_offset, offset;
	struct reiserfs_dir_entry de;
	int retval;

	/* If searching for directory entry. */
	if (is_direntry_cpu_key(p_cpu_key))
		return search_by_entry_key(sb, p_cpu_key, search_path,
					   &de);

	/* If not searching for directory entry. */

	/* If item is found. */
	retval = search_item(sb, p_cpu_key, search_path);
	if (retval == IO_ERROR)
		return retval;
	if (retval == ITEM_FOUND) {

		RFALSE(!ih_item_len
		       (item_head
			(PATH_PLAST_BUFFER(search_path),
			 PATH_LAST_POSITION(search_path))),
		       "PAP-5165: item length equals zero");

		pos_in_item(search_path) = 0;
		return POSITION_FOUND;
	}

	RFALSE(!PATH_LAST_POSITION(search_path),
	       "PAP-5170: position equals zero");

	/* Item is not found. Set path to the previous item. */
	p_le_ih =
	    item_head(PATH_PLAST_BUFFER(search_path),
			   --PATH_LAST_POSITION(search_path));
	blk_size = sb->s_blocksize;

	if (comp_short_keys(&p_le_ih->ih_key, p_cpu_key))
		return FILE_NOT_FOUND;

	/* FIXME: quite ugly this far */

	item_offset = le_ih_k_offset(p_le_ih);
	offset = cpu_key_k_offset(p_cpu_key);

	/* Needed byte is contained in the item pointed to by the path. */
	if (item_offset <= offset &&
	    item_offset + op_bytes_number(p_le_ih, blk_size) > offset) {
		pos_in_item(search_path) = offset - item_offset;
		if (is_indirect_le_ih(p_le_ih)) {
			pos_in_item(search_path) /= blk_size;
		}
		return POSITION_FOUND;
	}

	/*
	 * Needed byte is not contained in the item pointed to by the
	 * path. Set pos_in_item out of the item.
	 */
	if (is_indirect_le_ih(p_le_ih))
		pos_in_item(search_path) =
		    ih_item_len(p_le_ih) / UNFM_P_SIZE;
	else
		pos_in_item(search_path) = ih_item_len(p_le_ih);

	return POSITION_NOT_FOUND;
}

/* Compare given item and item pointed to by the path. */
int comp_items(const struct item_head *stored_ih, const struct treepath *path)
{
	struct buffer_head *bh = PATH_PLAST_BUFFER(path);
	struct item_head *ih;

	/* Last buffer at the path is not in the tree. */
	if (!B_IS_IN_TREE(bh))
		return 1;

	/* Last path position is invalid. */
	if (PATH_LAST_POSITION(path) >= B_NR_ITEMS(bh))
		return 1;

	/* we need only to know, whether it is the same item */
	ih = tp_item_head(path);
	return memcmp(stored_ih, ih, IH_SIZE);
}

/* prepare for delete or cut of direct item */
static inline int prepare_for_direct_item(struct treepath *path,
					  struct item_head *le_ih,
					  struct inode *inode,
					  loff_t new_file_length, int *cut_size)
{
	loff_t round_len;

	if (new_file_length == max_reiserfs_offset(inode)) {
		/* item has to be deleted */
		*cut_size = -(IH_SIZE + ih_item_len(le_ih));
		return M_DELETE;
	}
	/* new file gets truncated */
	if (get_inode_item_key_version(inode) == KEY_FORMAT_3_6) {
		round_len = ROUND_UP(new_file_length);
		/* this was new_file_length < le_ih ... */
		if (round_len < le_ih_k_offset(le_ih)) {
			*cut_size = -(IH_SIZE + ih_item_len(le_ih));
			return M_DELETE;	/* Delete this item. */
		}
		/* Calculate first position and size for cutting from item. */
		pos_in_item(path) = round_len - (le_ih_k_offset(le_ih) - 1);
		*cut_size = -(ih_item_len(le_ih) - pos_in_item(path));

		return M_CUT;	/* Cut from this item. */
	}

	/* old file: items may have any length */

	if (new_file_length < le_ih_k_offset(le_ih)) {
		*cut_size = -(IH_SIZE + ih_item_len(le_ih));
		return M_DELETE;	/* Delete this item. */
	}

	/* Calculate first position and size for cutting from item. */
	*cut_size = -(ih_item_len(le_ih) -
		      (pos_in_item(path) =
		       new_file_length + 1 - le_ih_k_offset(le_ih)));
	return M_CUT;		/* Cut from this item. */
}

static inline int prepare_for_direntry_item(struct treepath *path,
					    struct item_head *le_ih,
					    struct inode *inode,
					    loff_t new_file_length,
					    int *cut_size)
{
	if (le_ih_k_offset(le_ih) == DOT_OFFSET &&
	    new_file_length == max_reiserfs_offset(inode)) {
		RFALSE(ih_entry_count(le_ih) != 2,
		       "PAP-5220: incorrect empty directory item (%h)", le_ih);
		*cut_size = -(IH_SIZE + ih_item_len(le_ih));
		/* Delete the directory item containing "." and ".." entry. */
		return M_DELETE;
	}

	if (ih_entry_count(le_ih) == 1) {
		/*
		 * Delete the directory item such as there is one record only
		 * in this item
		 */
		*cut_size = -(IH_SIZE + ih_item_len(le_ih));
		return M_DELETE;
	}

	/* Cut one record from the directory item. */
	*cut_size =
	    -(DEH_SIZE +
	      entry_length(get_last_bh(path), le_ih, pos_in_item(path)));
	return M_CUT;
}

#define JOURNAL_FOR_FREE_BLOCK_AND_UPDATE_SD (2 * JOURNAL_PER_BALANCE_CNT + 1)

/*
 * If the path points to a directory or direct item, calculate mode
 * and the size cut, for balance.
 * If the path points to an indirect item, remove some number of its
 * unformatted nodes.
 * In case of file truncate calculate whether this item must be
 * deleted/truncated or last unformatted node of this item will be
 * converted to a direct item.
 * This function returns a determination of what balance mode the
 * calling function should employ.
 */
static char prepare_for_delete_or_cut(struct reiserfs_transaction_handle *th,
				      struct inode *inode,
				      struct treepath *path,
				      const struct cpu_key *item_key,
				      /*
				       * Number of unformatted nodes
				       * which were removed from end
				       * of the file.
				       */
				      int *removed,
				      int *cut_size,
				      /* MAX_KEY_OFFSET in case of delete. */
				      unsigned long long new_file_length
    )
{
	struct super_block *sb = inode->i_sb;
	struct item_head *p_le_ih = tp_item_head(path);
	struct buffer_head *bh = PATH_PLAST_BUFFER(path);

	BUG_ON(!th->t_trans_id);

	/* Stat_data item. */
	if (is_statdata_le_ih(p_le_ih)) {

		RFALSE(new_file_length != max_reiserfs_offset(inode),
		       "PAP-5210: mode must be M_DELETE");

		*cut_size = -(IH_SIZE + ih_item_len(p_le_ih));
		return M_DELETE;
	}

	/* Directory item. */
	if (is_direntry_le_ih(p_le_ih))
		return prepare_for_direntry_item(path, p_le_ih, inode,
						 new_file_length,
						 cut_size);

	/* Direct item. */
	if (is_direct_le_ih(p_le_ih))
		return prepare_for_direct_item(path, p_le_ih, inode,
					       new_file_length, cut_size);

	/* Case of an indirect item. */
	{
	    int blk_size = sb->s_blocksize;
	    struct item_head s_ih;
	    int need_re_search;
	    int delete = 0;
	    int result = M_CUT;
	    int pos = 0;

	    if ( new_file_length == max_reiserfs_offset (inode) ) {
		/*
		 * prepare_for_delete_or_cut() is called by
		 * reiserfs_delete_item()
		 */
		new_file_length = 0;
		delete = 1;
	    }

	    do {
		need_re_search = 0;
		*cut_size = 0;
		bh = PATH_PLAST_BUFFER(path);
		copy_item_head(&s_ih, tp_item_head(path));
		pos = I_UNFM_NUM(&s_ih);

		while (le_ih_k_offset (&s_ih) + (pos - 1) * blk_size > new_file_length) {
		    __le32 *unfm;
		    __u32 block;

		    /*
		     * Each unformatted block deletion may involve
		     * one additional bitmap block into the transaction,
		     * thereby the initial journal space reservation
		     * might not be enough.
		     */
		    if (!delete && (*cut_size) != 0 &&
			reiserfs_transaction_free_space(th) < JOURNAL_FOR_FREE_BLOCK_AND_UPDATE_SD)
			break;

		    unfm = (__le32 *)ih_item_body(bh, &s_ih) + pos - 1;
		    block = get_block_num(unfm, 0);

		    if (block != 0) {
			reiserfs_prepare_for_journal(sb, bh, 1);
			put_block_num(unfm, 0, 0);
			journal_mark_dirty(th, bh);
			reiserfs_free_block(th, inode, block, 1);
		    }

		    reiserfs_cond_resched(sb);

		    if (item_moved (&s_ih, path))  {
			need_re_search = 1;
			break;
		    }

		    pos --;
		    (*removed)++;
		    (*cut_size) -= UNFM_P_SIZE;

		    if (pos == 0) {
			(*cut_size) -= IH_SIZE;
			result = M_DELETE;
			break;
		    }
		}
		/*
		 * a trick.  If the buffer has been logged, this will
		 * do nothing.  If we've broken the loop without logging
		 * it, it will restore the buffer
		 */
		reiserfs_restore_prepared_buffer(sb, bh);
	    } while (need_re_search &&
		     search_for_position_by_key(sb, item_key, path) == POSITION_FOUND);
	    pos_in_item(path) = pos * UNFM_P_SIZE;

	    if (*cut_size == 0) {
		/*
		 * Nothing was cut. maybe convert last unformatted node to the
		 * direct item?
		 */
		result = M_CONVERT;
	    }
	    return result;
	}
}

/* Calculate number of bytes which will be deleted or cut during balance */
static int calc_deleted_bytes_number(struct tree_balance *tb, char mode)
{
	int del_size;
	struct item_head *p_le_ih = tp_item_head(tb->tb_path);

	if (is_statdata_le_ih(p_le_ih))
		return 0;

	del_size =
	    (mode ==
	     M_DELETE) ? ih_item_len(p_le_ih) : -tb->insert_size[0];
	if (is_direntry_le_ih(p_le_ih)) {
		/*
		 * return EMPTY_DIR_SIZE; We delete emty directories only.
		 * we can't use EMPTY_DIR_SIZE, as old format dirs have a
		 * different empty size.  ick. FIXME, is this right?
		 */
		return del_size;
	}

	if (is_indirect_le_ih(p_le_ih))
		del_size = (del_size / UNFM_P_SIZE) *
				(PATH_PLAST_BUFFER(tb->tb_path)->b_size);
	return del_size;
}

static void init_tb_struct(struct reiserfs_transaction_handle *th,
			   struct tree_balance *tb,
			   struct super_block *sb,
			   struct treepath *path, int size)
{

	BUG_ON(!th->t_trans_id);

	memset(tb, '\0', sizeof(struct tree_balance));
	tb->transaction_handle = th;
	tb->tb_sb = sb;
	tb->tb_path = path;
	PATH_OFFSET_PBUFFER(path, ILLEGAL_PATH_ELEMENT_OFFSET) = NULL;
	PATH_OFFSET_POSITION(path, ILLEGAL_PATH_ELEMENT_OFFSET) = 0;
	tb->insert_size[0] = size;
}

void padd_item(char *item, int total_length, int length)
{
	int i;

	for (i = total_length; i > length;)
		item[--i] = 0;
}

#ifdef REISERQUOTA_DEBUG
char key2type(struct reiserfs_key *ih)
{
	if (is_direntry_le_key(2, ih))
		return 'd';
	if (is_direct_le_key(2, ih))
		return 'D';
	if (is_indirect_le_key(2, ih))
		return 'i';
	if (is_statdata_le_key(2, ih))
		return 's';
	return 'u';
}

char head2type(struct item_head *ih)
{
	if (is_direntry_le_ih(ih))
		return 'd';
	if (is_direct_le_ih(ih))
		return 'D';
	if (is_indirect_le_ih(ih))
		return 'i';
	if (is_statdata_le_ih(ih))
		return 's';
	return 'u';
}
#endif

/*
 * Delete object item.
 * th       - active transaction handle
 * path     - path to the deleted item
 * item_key - key to search for the deleted item
 * indode   - used for updating i_blocks and quotas
 * un_bh    - NULL or unformatted node pointer
 */
int reiserfs_delete_item(struct reiserfs_transaction_handle *th,
			 struct treepath *path, const struct cpu_key *item_key,
			 struct inode *inode, struct buffer_head *un_bh)
{
	struct super_block *sb = inode->i_sb;
	struct tree_balance s_del_balance;
	struct item_head s_ih;
	struct item_head *q_ih;
	int quota_cut_bytes;
	int ret_value, del_size, removed;
	int depth;

#ifdef CONFIG_REISERFS_CHECK
	char mode;
#endif

	BUG_ON(!th->t_trans_id);

	init_tb_struct(th, &s_del_balance, sb, path,
		       0 /*size is unknown */ );

	while (1) {
		removed = 0;

#ifdef CONFIG_REISERFS_CHECK
		mode =
#endif
		    prepare_for_delete_or_cut(th, inode, path,
					      item_key, &removed,
					      &del_size,
					      max_reiserfs_offset(inode));

		RFALSE(mode != M_DELETE, "PAP-5320: mode must be M_DELETE");

		copy_item_head(&s_ih, tp_item_head(path));
		s_del_balance.insert_size[0] = del_size;

		ret_value = fix_nodes(M_DELETE, &s_del_balance, NULL, NULL);
		if (ret_value != REPEAT_SEARCH)
			break;

		PROC_INFO_INC(sb, delete_item_restarted);

		/* file system changed, repeat search */
		ret_value =
		    search_for_position_by_key(sb, item_key, path);
		if (ret_value == IO_ERROR)
			break;
		if (ret_value == FILE_NOT_FOUND) {
			reiserfs_warning(sb, "vs-5340",
					 "no items of the file %K found",
					 item_key);
			break;
		}
	}			/* while (1) */

	if (ret_value != CARRY_ON) {
		unfix_nodes(&s_del_balance);
		return 0;
	}

	/* reiserfs_delete_item returns item length when success */
	ret_value = calc_deleted_bytes_number(&s_del_balance, M_DELETE);
	q_ih = tp_item_head(path);
	quota_cut_bytes = ih_item_len(q_ih);

	/*
	 * hack so the quota code doesn't have to guess if the file has a
	 * tail.  On tail insert, we allocate quota for 1 unformatted node.
	 * We test the offset because the tail might have been
	 * split into multiple items, and we only want to decrement for
	 * the unfm node once
	 */
	if (!S_ISLNK(inode->i_mode) && is_direct_le_ih(q_ih)) {
		if ((le_ih_k_offset(q_ih) & (sb->s_blocksize - 1)) == 1) {
			quota_cut_bytes = sb->s_blocksize + UNFM_P_SIZE;
		} else {
			quota_cut_bytes = 0;
		}
	}

	if (un_bh) {
		int off;
		char *data;

		/*
		 * We are in direct2indirect conversion, so move tail contents
		 * to the unformatted node
		 */
		/*
		 * note, we do the copy before preparing the buffer because we
		 * don't care about the contents of the unformatted node yet.
		 * the only thing we really care about is the direct item's
		 * data is in the unformatted node.
		 *
		 * Otherwise, we would have to call
		 * reiserfs_prepare_for_journal on the unformatted node,
		 * which might schedule, meaning we'd have to loop all the
		 * way back up to the start of the while loop.
		 *
		 * The unformatted node must be dirtied later on.  We can't be
		 * sure here if the entire tail has been deleted yet.
		 *
		 * un_bh is from the page cache (all unformatted nodes are
		 * from the page cache) and might be a highmem page.  So, we
		 * can't use un_bh->b_data.
		 * -clm
		 */

		data = kmap_atomic(un_bh->b_page);
		off = ((le_ih_k_offset(&s_ih) - 1) & (PAGE_SIZE - 1));
		memcpy(data + off,
		       ih_item_body(PATH_PLAST_BUFFER(path), &s_ih),
		       ret_value);
		kunmap_atomic(data);
	}

	/* Perform balancing after all resources have been collected at once. */
	do_balance(&s_del_balance, NULL, NULL, M_DELETE);

#ifdef REISERQUOTA_DEBUG
	reiserfs_debug(sb, REISERFS_DEBUG_CODE,
		       "reiserquota delete_item(): freeing %u, id=%u type=%c",
		       quota_cut_bytes, inode->i_uid, head2type(&s_ih));
#endif
	depth = reiserfs_write_unlock_nested(inode->i_sb);
	dquot_free_space_nodirty(inode, quota_cut_bytes);
	reiserfs_write_lock_nested(inode->i_sb, depth);

	/* Return deleted body length */
	return ret_value;
}

/*
 * Summary Of Mechanisms For Handling Collisions Between Processes:
 *
 *  deletion of the body of the object is performed by iput(), with the
 *  result that if multiple processes are operating on a file, the
 *  deletion of the body of the file is deferred until the last process
 *  that has an open inode performs its iput().
 *
 *  writes and truncates are protected from collisions by use of
 *  semaphores.
 *
 *  creates, linking, and mknod are protected from collisions with other
 *  processes by making the reiserfs_add_entry() the last step in the
 *  creation, and then rolling back all changes if there was a collision.
 *  - Hans
*/

/* this deletes item which never gets split */
void reiserfs_delete_solid_item(struct reiserfs_transaction_handle *th,
				struct inode *inode, struct reiserfs_key *key)
{
	struct super_block *sb = th->t_super;
	struct tree_balance tb;
	INITIALIZE_PATH(path);
	int item_len = 0;
	int tb_init = 0;
	struct cpu_key cpu_key;
	int retval;
	int quota_cut_bytes = 0;

	BUG_ON(!th->t_trans_id);

	le_key2cpu_key(&cpu_key, key);

	while (1) {
		retval = search_item(th->t_super, &cpu_key, &path);
		if (retval == IO_ERROR) {
			reiserfs_error(th->t_super, "vs-5350",
				       "i/o failure occurred trying "
				       "to delete %K", &cpu_key);
			break;
		}
		if (retval != ITEM_FOUND) {
			pathrelse(&path);
			/*
			 * No need for a warning, if there is just no free
			 * space to insert '..' item into the
			 * newly-created subdir
			 */
			if (!
			    ((unsigned long long)
			     GET_HASH_VALUE(le_key_k_offset
					    (le_key_version(key), key)) == 0
			     && (unsigned long long)
			     GET_GENERATION_NUMBER(le_key_k_offset
						   (le_key_version(key),
						    key)) == 1))
				reiserfs_warning(th->t_super, "vs-5355",
						 "%k not found", key);
			break;
		}
		if (!tb_init) {
			tb_init = 1;
			item_len = ih_item_len(tp_item_head(&path));
			init_tb_struct(th, &tb, th->t_super, &path,
				       -(IH_SIZE + item_len));
		}
		quota_cut_bytes = ih_item_len(tp_item_head(&path));

		retval = fix_nodes(M_DELETE, &tb, NULL, NULL);
		if (retval == REPEAT_SEARCH) {
			PROC_INFO_INC(th->t_super, delete_solid_item_restarted);
			continue;
		}

		if (retval == CARRY_ON) {
			do_balance(&tb, NULL, NULL, M_DELETE);
			/*
			 * Should we count quota for item? (we don't
			 * count quotas for save-links)
			 */
			if (inode) {
				int depth;
#ifdef REISERQUOTA_DEBUG
				reiserfs_debug(th->t_super, REISERFS_DEBUG_CODE,
					       "reiserquota delete_solid_item(): freeing %u id=%u type=%c",
					       quota_cut_bytes, inode->i_uid,
					       key2type(key));
#endif
				depth = reiserfs_write_unlock_nested(sb);
				dquot_free_space_nodirty(inode,
							 quota_cut_bytes);
				reiserfs_write_lock_nested(sb, depth);
			}
			break;
		}

		/* IO_ERROR, NO_DISK_SPACE, etc */
		reiserfs_warning(th->t_super, "vs-5360",
				 "could not delete %K due to fix_nodes failure",
				 &cpu_key);
		unfix_nodes(&tb);
		break;
	}

	reiserfs_check_path(&path);
}

int reiserfs_delete_object(struct reiserfs_transaction_handle *th,
			   struct inode *inode)
{
	int err;
	inode->i_size = 0;
	BUG_ON(!th->t_trans_id);

	/* for directory this deletes item containing "." and ".." */
	err =
	    reiserfs_do_truncate(th, inode, NULL, 0 /*no timestamp updates */ );
	if (err)
		return err;

#if defined( USE_INODE_GENERATION_COUNTER )
	if (!old_format_only(th->t_super)) {
		__le32 *inode_generation;

		inode_generation =
		    &REISERFS_SB(th->t_super)->s_rs->s_inode_generation;
		le32_add_cpu(inode_generation, 1);
	}
/* USE_INODE_GENERATION_COUNTER */
#endif
	reiserfs_delete_solid_item(th, inode, INODE_PKEY(inode));

	return err;
}

static void unmap_buffers(struct page *page, loff_t pos)
{
	struct buffer_head *bh;
	struct buffer_head *head;
	struct buffer_head *next;
	unsigned long tail_index;
	unsigned long cur_index;

	if (page) {
		if (page_has_buffers(page)) {
			tail_index = pos & (PAGE_SIZE - 1);
			cur_index = 0;
			head = page_buffers(page);
			bh = head;
			do {
				next = bh->b_this_page;

				/*
				 * we want to unmap the buffers that contain
				 * the tail, and all the buffers after it
				 * (since the tail must be at the end of the
				 * file).  We don't want to unmap file data
				 * before the tail, since it might be dirty
				 * and waiting to reach disk
				 */
				cur_index += bh->b_size;
				if (cur_index > tail_index) {
					reiserfs_unmap_buffer(bh);
				}
				bh = next;
			} while (bh != head);
		}
	}
}

static int maybe_indirect_to_direct(struct reiserfs_transaction_handle *th,
				    struct inode *inode,
				    struct page *page,
				    struct treepath *path,
				    const struct cpu_key *item_key,
				    loff_t new_file_size, char *mode)
{
	struct super_block *sb = inode->i_sb;
	int block_size = sb->s_blocksize;
	int cut_bytes;
	BUG_ON(!th->t_trans_id);
	BUG_ON(new_file_size != inode->i_size);

	/*
	 * the page being sent in could be NULL if there was an i/o error
	 * reading in the last block.  The user will hit problems trying to
	 * read the file, but for now we just skip the indirect2direct
	 */
	if (atomic_read(&inode->i_count) > 1 ||
	    !tail_has_to_be_packed(inode) ||
	    !page || (REISERFS_I(inode)->i_flags & i_nopack_mask)) {
		/* leave tail in an unformatted node */
		*mode = M_SKIP_BALANCING;
		cut_bytes =
		    block_size - (new_file_size & (block_size - 1));
		pathrelse(path);
		return cut_bytes;
	}

	/* Perform the conversion to a direct_item. */
	return indirect2direct(th, inode, page, path, item_key,
			       new_file_size, mode);
}

/*
 * we did indirect_to_direct conversion. And we have inserted direct
 * item successesfully, but there were no disk space to cut unfm
 * pointer being converted. Therefore we have to delete inserted
 * direct item(s)
 */
static void indirect_to_direct_roll_back(struct reiserfs_transaction_handle *th,
					 struct inode *inode, struct treepath *path)
{
	struct cpu_key tail_key;
	int tail_len;
	int removed;
	BUG_ON(!th->t_trans_id);

	make_cpu_key(&tail_key, inode, inode->i_size + 1, TYPE_DIRECT, 4);
	tail_key.key_length = 4;

	tail_len =
	    (cpu_key_k_offset(&tail_key) & (inode->i_sb->s_blocksize - 1)) - 1;
	while (tail_len) {
		/* look for the last byte of the tail */
		if (search_for_position_by_key(inode->i_sb, &tail_key, path) ==
		    POSITION_NOT_FOUND)
			reiserfs_panic(inode->i_sb, "vs-5615",
				       "found invalid item");
		RFALSE(path->pos_in_item !=
		       ih_item_len(tp_item_head(path)) - 1,
		       "vs-5616: appended bytes found");
		PATH_LAST_POSITION(path)--;

		removed =
		    reiserfs_delete_item(th, path, &tail_key, inode,
					 NULL /*unbh not needed */ );
		RFALSE(removed <= 0
		       || removed > tail_len,
		       "vs-5617: there was tail %d bytes, removed item length %d bytes",
		       tail_len, removed);
		tail_len -= removed;
		set_cpu_key_k_offset(&tail_key,
				     cpu_key_k_offset(&tail_key) - removed);
	}
	reiserfs_warning(inode->i_sb, "reiserfs-5091", "indirect_to_direct "
			 "conversion has been rolled back due to "
			 "lack of disk space");
	mark_inode_dirty(inode);
}

/* (Truncate or cut entry) or delete object item. Returns < 0 on failure */
int reiserfs_cut_from_item(struct reiserfs_transaction_handle *th,
			   struct treepath *path,
			   struct cpu_key *item_key,
			   struct inode *inode,
			   struct page *page, loff_t new_file_size)
{
	struct super_block *sb = inode->i_sb;
	/*
	 * Every function which is going to call do_balance must first
	 * create a tree_balance structure.  Then it must fill up this
	 * structure by using the init_tb_struct and fix_nodes functions.
	 * After that we can make tree balancing.
	 */
	struct tree_balance s_cut_balance;
	struct item_head *p_le_ih;
	int cut_size = 0;	/* Amount to be cut. */
	int ret_value = CARRY_ON;
	int removed = 0;	/* Number of the removed unformatted nodes. */
	int is_inode_locked = 0;
	char mode;		/* Mode of the balance. */
	int retval2 = -1;
	int quota_cut_bytes;
	loff_t tail_pos = 0;
	int depth;

	BUG_ON(!th->t_trans_id);

	init_tb_struct(th, &s_cut_balance, inode->i_sb, path,
		       cut_size);

	/*
	 * Repeat this loop until we either cut the item without needing
	 * to balance, or we fix_nodes without schedule occurring
	 */
	while (1) {
		/*
		 * Determine the balance mode, position of the first byte to
		 * be cut, and size to be cut.  In case of the indirect item
		 * free unformatted nodes which are pointed to by the cut
		 * pointers.
		 */

		mode =
		    prepare_for_delete_or_cut(th, inode, path,
					      item_key, &removed,
					      &cut_size, new_file_size);
		if (mode == M_CONVERT) {
			/*
			 * convert last unformatted node to direct item or
			 * leave tail in the unformatted node
			 */
			RFALSE(ret_value != CARRY_ON,
			       "PAP-5570: can not convert twice");

			ret_value =
			    maybe_indirect_to_direct(th, inode, page,
						     path, item_key,
						     new_file_size, &mode);
			if (mode == M_SKIP_BALANCING)
				/* tail has been left in the unformatted node */
				return ret_value;

			is_inode_locked = 1;

			/*
			 * removing of last unformatted node will
			 * change value we have to return to truncate.
			 * Save it
			 */
			retval2 = ret_value;

			/*
			 * So, we have performed the first part of the
			 * conversion:
			 * inserting the new direct item.  Now we are
			 * removing the last unformatted node pointer.
			 * Set key to search for it.
			 */
			set_cpu_key_k_type(item_key, TYPE_INDIRECT);
			item_key->key_length = 4;
			new_file_size -=
			    (new_file_size & (sb->s_blocksize - 1));
			tail_pos = new_file_size;
			set_cpu_key_k_offset(item_key, new_file_size + 1);
			if (search_for_position_by_key
			    (sb, item_key,
			     path) == POSITION_NOT_FOUND) {
				print_block(PATH_PLAST_BUFFER(path), 3,
					    PATH_LAST_POSITION(path) - 1,
					    PATH_LAST_POSITION(path) + 1);
				reiserfs_panic(sb, "PAP-5580", "item to "
					       "convert does not exist (%K)",
					       item_key);
			}
			continue;
		}
		if (cut_size == 0) {
			pathrelse(path);
			return 0;
		}

		s_cut_balance.insert_size[0] = cut_size;

		ret_value = fix_nodes(mode, &s_cut_balance, NULL, NULL);
		if (ret_value != REPEAT_SEARCH)
			break;

		PROC_INFO_INC(sb, cut_from_item_restarted);

		ret_value =
		    search_for_position_by_key(sb, item_key, path);
		if (ret_value == POSITION_FOUND)
			continue;

		reiserfs_warning(sb, "PAP-5610", "item %K not found",
				 item_key);
		unfix_nodes(&s_cut_balance);
		return (ret_value == IO_ERROR) ? -EIO : -ENOENT;
	}			/* while */

	/* check fix_nodes results (IO_ERROR or NO_DISK_SPACE) */
	if (ret_value != CARRY_ON) {
		if (is_inode_locked) {
			/*
			 * FIXME: this seems to be not needed: we are always
			 * able to cut item
			 */
			indirect_to_direct_roll_back(th, inode, path);
		}
		if (ret_value == NO_DISK_SPACE)
			reiserfs_warning(sb, "reiserfs-5092",
					 "NO_DISK_SPACE");
		unfix_nodes(&s_cut_balance);
		return -EIO;
	}

	/* go ahead and perform balancing */

	RFALSE(mode == M_PASTE || mode == M_INSERT, "invalid mode");

	/* Calculate number of bytes that need to be cut from the item. */
	quota_cut_bytes =
	    (mode ==
	     M_DELETE) ? ih_item_len(tp_item_head(path)) : -s_cut_balance.
	    insert_size[0];
	if (retval2 == -1)
		ret_value = calc_deleted_bytes_number(&s_cut_balance, mode);
	else
		ret_value = retval2;

	/*
	 * For direct items, we only change the quota when deleting the last
	 * item.
	 */
	p_le_ih = tp_item_head(s_cut_balance.tb_path);
	if (!S_ISLNK(inode->i_mode) && is_direct_le_ih(p_le_ih)) {
		if (mode == M_DELETE &&
		    (le_ih_k_offset(p_le_ih) & (sb->s_blocksize - 1)) ==
		    1) {
			/* FIXME: this is to keep 3.5 happy */
			REISERFS_I(inode)->i_first_direct_byte = U32_MAX;
			quota_cut_bytes = sb->s_blocksize + UNFM_P_SIZE;
		} else {
			quota_cut_bytes = 0;
		}
	}
#ifdef CONFIG_REISERFS_CHECK
	if (is_inode_locked) {
		struct item_head *le_ih =
		    tp_item_head(s_cut_balance.tb_path);
		/*
		 * we are going to complete indirect2direct conversion. Make
		 * sure, that we exactly remove last unformatted node pointer
		 * of the item
		 */
		if (!is_indirect_le_ih(le_ih))
			reiserfs_panic(sb, "vs-5652",
				       "item must be indirect %h", le_ih);

		if (mode == M_DELETE && ih_item_len(le_ih) != UNFM_P_SIZE)
			reiserfs_panic(sb, "vs-5653", "completing "
				       "indirect2direct conversion indirect "
				       "item %h being deleted must be of "
				       "4 byte long", le_ih);

		if (mode == M_CUT
		    && s_cut_balance.insert_size[0] != -UNFM_P_SIZE) {
			reiserfs_panic(sb, "vs-5654", "can not complete "
				       "indirect2direct conversion of %h "
				       "(CUT, insert_size==%d)",
				       le_ih, s_cut_balance.insert_size[0]);
		}
		/*
		 * it would be useful to make sure, that right neighboring
		 * item is direct item of this file
		 */
	}
#endif

	do_balance(&s_cut_balance, NULL, NULL, mode);
	if (is_inode_locked) {
		/*
		 * we've done an indirect->direct conversion.  when the
		 * data block was freed, it was removed from the list of
		 * blocks that must be flushed before the transaction
		 * commits, make sure to unmap and invalidate it
		 */
		unmap_buffers(page, tail_pos);
		REISERFS_I(inode)->i_flags &= ~i_pack_on_close_mask;
	}
#ifdef REISERQUOTA_DEBUG
	reiserfs_debug(inode->i_sb, REISERFS_DEBUG_CODE,
		       "reiserquota cut_from_item(): freeing %u id=%u type=%c",
		       quota_cut_bytes, inode->i_uid, '?');
#endif
	depth = reiserfs_write_unlock_nested(sb);
	dquot_free_space_nodirty(inode, quota_cut_bytes);
	reiserfs_write_lock_nested(sb, depth);
	return ret_value;
}

static void truncate_directory(struct reiserfs_transaction_handle *th,
			       struct inode *inode)
{
	BUG_ON(!th->t_trans_id);
	if (inode->i_nlink)
		reiserfs_error(inode->i_sb, "vs-5655", "link count != 0");

	set_le_key_k_offset(KEY_FORMAT_3_5, INODE_PKEY(inode), DOT_OFFSET);
	set_le_key_k_type(KEY_FORMAT_3_5, INODE_PKEY(inode), TYPE_DIRENTRY);
	reiserfs_delete_solid_item(th, inode, INODE_PKEY(inode));
	reiserfs_update_sd(th, inode);
	set_le_key_k_offset(KEY_FORMAT_3_5, INODE_PKEY(inode), SD_OFFSET);
	set_le_key_k_type(KEY_FORMAT_3_5, INODE_PKEY(inode), TYPE_STAT_DATA);
}

/*
 * Truncate file to the new size. Note, this must be called with a
 * transaction already started
 */
int reiserfs_do_truncate(struct reiserfs_transaction_handle *th,
			 struct inode *inode,	/* ->i_size contains new size */
			 struct page *page,	/* up to date for last block */
			 /*
			  * when it is called by file_release to convert
			  * the tail - no timestamps should be updated
			  */
			 int update_timestamps
    )
{
	INITIALIZE_PATH(s_search_path);	/* Path to the current object item. */
	struct item_head *p_le_ih;	/* Pointer to an item header. */

	/* Key to search for a previous file item. */
	struct cpu_key s_item_key;
	loff_t file_size,	/* Old file size. */
	 new_file_size;	/* New file size. */
	int deleted;		/* Number of deleted or truncated bytes. */
	int retval;
	int err = 0;

	BUG_ON(!th->t_trans_id);
	if (!
	    (S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode)
	     || S_ISLNK(inode->i_mode)))
		return 0;

	/* deletion of directory - no need to update timestamps */
	if (S_ISDIR(inode->i_mode)) {
		truncate_directory(th, inode);
		return 0;
	}

	/* Get new file size. */
	new_file_size = inode->i_size;

	/* FIXME: note, that key type is unimportant here */
	make_cpu_key(&s_item_key, inode, max_reiserfs_offset(inode),
		     TYPE_DIRECT, 3);

	retval =
	    search_for_position_by_key(inode->i_sb, &s_item_key,
				       &s_search_path);
	if (retval == IO_ERROR) {
		reiserfs_error(inode->i_sb, "vs-5657",
			       "i/o failure occurred trying to truncate %K",
			       &s_item_key);
		err = -EIO;
		goto out;
	}
	if (retval == POSITION_FOUND || retval == FILE_NOT_FOUND) {
		reiserfs_error(inode->i_sb, "PAP-5660",
			       "wrong result %d of search for %K", retval,
			       &s_item_key);

		err = -EIO;
		goto out;
	}

	s_search_path.pos_in_item--;

	/* Get real file size (total length of all file items) */
	p_le_ih = tp_item_head(&s_search_path);
	if (is_statdata_le_ih(p_le_ih))
		file_size = 0;
	else {
		loff_t offset = le_ih_k_offset(p_le_ih);
		int bytes =
		    op_bytes_number(p_le_ih, inode->i_sb->s_blocksize);

		/*
		 * this may mismatch with real file size: if last direct item
		 * had no padding zeros and last unformatted node had no free
		 * space, this file would have this file size
		 */
		file_size = offset + bytes - 1;
	}
	/*
	 * are we doing a full truncate or delete, if so
	 * kick in the reada code
	 */
	if (new_file_size == 0)
		s_search_path.reada = PATH_READA | PATH_READA_BACK;

	if (file_size == 0 || file_size < new_file_size) {
		goto update_and_out;
	}

	/* Update key to search for the last file item. */
	set_cpu_key_k_offset(&s_item_key, file_size);

	do {
		/* Cut or delete file item. */
		deleted =
		    reiserfs_cut_from_item(th, &s_search_path, &s_item_key,
					   inode, page, new_file_size);
		if (deleted < 0) {
			reiserfs_warning(inode->i_sb, "vs-5665",
					 "reiserfs_cut_from_item failed");
			reiserfs_check_path(&s_search_path);
			return 0;
		}

		RFALSE(deleted > file_size,
		       "PAP-5670: reiserfs_cut_from_item: too many bytes deleted: deleted %d, file_size %lu, item_key %K",
		       deleted, file_size, &s_item_key);

		/* Change key to search the last file item. */
		file_size -= deleted;

		set_cpu_key_k_offset(&s_item_key, file_size);

		/*
		 * While there are bytes to truncate and previous
		 * file item is presented in the tree.
		 */

		/*
		 * This loop could take a really long time, and could log
		 * many more blocks than a transaction can hold.  So, we do
		 * a polite journal end here, and if the transaction needs
		 * ending, we make sure the file is consistent before ending
		 * the current trans and starting a new one
		 */
		if (journal_transaction_should_end(th, 0) ||
		    reiserfs_transaction_free_space(th) <= JOURNAL_FOR_FREE_BLOCK_AND_UPDATE_SD) {
			pathrelse(&s_search_path);

			if (update_timestamps) {
				inode->i_mtime = current_time(inode);
				inode->i_ctime = current_time(inode);
			}
			reiserfs_update_sd(th, inode);

			err = journal_end(th);
			if (err)
				goto out;
			err = journal_begin(th, inode->i_sb,
					    JOURNAL_FOR_FREE_BLOCK_AND_UPDATE_SD + JOURNAL_PER_BALANCE_CNT * 4) ;
			if (err)
				goto out;
			reiserfs_update_inode_transaction(inode);
		}
	} while (file_size > ROUND_UP(new_file_size) &&
		 search_for_position_by_key(inode->i_sb, &s_item_key,
					    &s_search_path) == POSITION_FOUND);

	RFALSE(file_size > ROUND_UP(new_file_size),
	       "PAP-5680: truncate did not finish: new_file_size %lld, current %lld, oid %d",
	       new_file_size, file_size, s_item_key.on_disk_key.k_objectid);

update_and_out:
	if (update_timestamps) {
		/* this is truncate, not file closing */
		inode->i_mtime = current_time(inode);
		inode->i_ctime = current_time(inode);
	}
	reiserfs_update_sd(th, inode);

out:
	pathrelse(&s_search_path);
	return err;
}

#ifdef CONFIG_REISERFS_CHECK
/* this makes sure, that we __append__, not overwrite or add holes */
static void check_research_for_paste(struct treepath *path,
				     const struct cpu_key *key)
{
	struct item_head *found_ih = tp_item_head(path);

	if (is_direct_le_ih(found_ih)) {
		if (le_ih_k_offset(found_ih) +
		    op_bytes_number(found_ih,
				    get_last_bh(path)->b_size) !=
		    cpu_key_k_offset(key)
		    || op_bytes_number(found_ih,
				       get_last_bh(path)->b_size) !=
		    pos_in_item(path))
			reiserfs_panic(NULL, "PAP-5720", "found direct item "
				       "%h or position (%d) does not match "
				       "to key %K", found_ih,
				       pos_in_item(path), key);
	}
	if (is_indirect_le_ih(found_ih)) {
		if (le_ih_k_offset(found_ih) +
		    op_bytes_number(found_ih,
				    get_last_bh(path)->b_size) !=
		    cpu_key_k_offset(key)
		    || I_UNFM_NUM(found_ih) != pos_in_item(path)
		    || get_ih_free_space(found_ih) != 0)
			reiserfs_panic(NULL, "PAP-5730", "found indirect "
				       "item (%h) or position (%d) does not "
				       "match to key (%K)",
				       found_ih, pos_in_item(path), key);
	}
}
#endif				/* config reiserfs check */

/*
 * Paste bytes to the existing item.
 * Returns bytes number pasted into the item.
 */
int reiserfs_paste_into_item(struct reiserfs_transaction_handle *th,
			     /* Path to the pasted item. */
			     struct treepath *search_path,
			     /* Key to search for the needed item. */
			     const struct cpu_key *key,
			     /* Inode item belongs to */
			     struct inode *inode,
			     /* Pointer to the bytes to paste. */
			     const char *body,
			     /* Size of pasted bytes. */
			     int pasted_size)
{
	struct super_block *sb = inode->i_sb;
	struct tree_balance s_paste_balance;
	int retval;
	int fs_gen;
	int depth;

	BUG_ON(!th->t_trans_id);

	fs_gen = get_generation(inode->i_sb);

#ifdef REISERQUOTA_DEBUG
	reiserfs_debug(inode->i_sb, REISERFS_DEBUG_CODE,
		       "reiserquota paste_into_item(): allocating %u id=%u type=%c",
		       pasted_size, inode->i_uid,
		       key2type(&key->on_disk_key));
#endif

	depth = reiserfs_write_unlock_nested(sb);
	retval = dquot_alloc_space_nodirty(inode, pasted_size);
	reiserfs_write_lock_nested(sb, depth);
	if (retval) {
		pathrelse(search_path);
		return retval;
	}
	init_tb_struct(th, &s_paste_balance, th->t_super, search_path,
		       pasted_size);
#ifdef DISPLACE_NEW_PACKING_LOCALITIES
	s_paste_balance.key = key->on_disk_key;
#endif

	/* DQUOT_* can schedule, must check before the fix_nodes */
	if (fs_changed(fs_gen, inode->i_sb)) {
		goto search_again;
	}

	while ((retval =
		fix_nodes(M_PASTE, &s_paste_balance, NULL,
			  body)) == REPEAT_SEARCH) {
search_again:
		/* file system changed while we were in the fix_nodes */
		PROC_INFO_INC(th->t_super, paste_into_item_restarted);
		retval =
		    search_for_position_by_key(th->t_super, key,
					       search_path);
		if (retval == IO_ERROR) {
			retval = -EIO;
			goto error_out;
		}
		if (retval == POSITION_FOUND) {
			reiserfs_warning(inode->i_sb, "PAP-5710",
					 "entry or pasted byte (%K) exists",
					 key);
			retval = -EEXIST;
			goto error_out;
		}
#ifdef CONFIG_REISERFS_CHECK
		check_research_for_paste(search_path, key);
#endif
	}

	/*
	 * Perform balancing after all resources are collected by fix_nodes,
	 * and accessing them will not risk triggering schedule.
	 */
	if (retval == CARRY_ON) {
		do_balance(&s_paste_balance, NULL /*ih */ , body, M_PASTE);
		return 0;
	}
	retval = (retval == NO_DISK_SPACE) ? -ENOSPC : -EIO;
error_out:
	/* this also releases the path */
	unfix_nodes(&s_paste_balance);
#ifdef REISERQUOTA_DEBUG
	reiserfs_debug(inode->i_sb, REISERFS_DEBUG_CODE,
		       "reiserquota paste_into_item(): freeing %u id=%u type=%c",
		       pasted_size, inode->i_uid,
		       key2type(&key->on_disk_key));
#endif
	depth = reiserfs_write_unlock_nested(sb);
	dquot_free_space_nodirty(inode, pasted_size);
	reiserfs_write_lock_nested(sb, depth);
	return retval;
}

/*
 * Insert new item into the buffer at the path.
 * th   - active transaction handle
 * path - path to the inserted item
 * ih   - pointer to the item header to insert
 * body - pointer to the bytes to insert
 */
int reiserfs_insert_item(struct reiserfs_transaction_handle *th,
			 struct treepath *path, const struct cpu_key *key,
			 struct item_head *ih, struct inode *inode,
			 const char *body)
{
	struct tree_balance s_ins_balance;
	int retval;
	int fs_gen = 0;
	int quota_bytes = 0;

	BUG_ON(!th->t_trans_id);

	if (inode) {		/* Do we count quotas for item? */
		int depth;
		fs_gen = get_generation(inode->i_sb);
		quota_bytes = ih_item_len(ih);

		/*
		 * hack so the quota code doesn't have to guess
		 * if the file has a tail, links are always tails,
		 * so there's no guessing needed
		 */
		if (!S_ISLNK(inode->i_mode) && is_direct_le_ih(ih))
			quota_bytes = inode->i_sb->s_blocksize + UNFM_P_SIZE;
#ifdef REISERQUOTA_DEBUG
		reiserfs_debug(inode->i_sb, REISERFS_DEBUG_CODE,
			       "reiserquota insert_item(): allocating %u id=%u type=%c",
			       quota_bytes, inode->i_uid, head2type(ih));
#endif
		/*
		 * We can't dirty inode here. It would be immediately
		 * written but appropriate stat item isn't inserted yet...
		 */
		depth = reiserfs_write_unlock_nested(inode->i_sb);
		retval = dquot_alloc_space_nodirty(inode, quota_bytes);
		reiserfs_write_lock_nested(inode->i_sb, depth);
		if (retval) {
			pathrelse(path);
			return retval;
		}
	}
	init_tb_struct(th, &s_ins_balance, th->t_super, path,
		       IH_SIZE + ih_item_len(ih));
#ifdef DISPLACE_NEW_PACKING_LOCALITIES
	s_ins_balance.key = key->on_disk_key;
#endif
	/*
	 * DQUOT_* can schedule, must check to be sure calling
	 * fix_nodes is safe
	 */
	if (inode && fs_changed(fs_gen, inode->i_sb)) {
		goto search_again;
	}

	while ((retval =
		fix_nodes(M_INSERT, &s_ins_balance, ih,
			  body)) == REPEAT_SEARCH) {
search_again:
		/* file system changed while we were in the fix_nodes */
		PROC_INFO_INC(th->t_super, insert_item_restarted);
		retval = search_item(th->t_super, key, path);
		if (retval == IO_ERROR) {
			retval = -EIO;
			goto error_out;
		}
		if (retval == ITEM_FOUND) {
			reiserfs_warning(th->t_super, "PAP-5760",
					 "key %K already exists in the tree",
					 key);
			retval = -EEXIST;
			goto error_out;
		}
	}

	/* make balancing after all resources will be collected at a time */
	if (retval == CARRY_ON) {
		do_balance(&s_ins_balance, ih, body, M_INSERT);
		return 0;
	}

	retval = (retval == NO_DISK_SPACE) ? -ENOSPC : -EIO;
error_out:
	/* also releases the path */
	unfix_nodes(&s_ins_balance);
#ifdef REISERQUOTA_DEBUG
	if (inode)
		reiserfs_debug(th->t_super, REISERFS_DEBUG_CODE,
		       "reiserquota insert_item(): freeing %u id=%u type=%c",
		       quota_bytes, inode->i_uid, head2type(ih));
#endif
	if (inode) {
		int depth = reiserfs_write_unlock_nested(inode->i_sb);
		dquot_free_space_nodirty(inode, quota_bytes);
		reiserfs_write_lock_nested(inode->i_sb, depth);
	}
	return retval;
}
