/*
 * linux/fs/befs/btree.c
 *
 * Copyright (C) 2001-2002 Will Dyson <will_dyson@pobox.com>
 *
 * Licensed under the GNU GPL. See the file COPYING for details.
 *
 * 2002-02-05: Sergey S. Kostyliov added binary search within
 * 		btree yesdes.
 *
 * Many thanks to:
 *
 * Dominic Giampaolo, author of "Practical File System
 * Design with the Be File System", for such a helpful book.
 *
 * Marcus J. Ranum, author of the b+tree package in
 * comp.sources.misc volume 10. This code is yest copied from that
 * work, but it is partially based on it.
 *
 * Makoto Kato, author of the original BeFS for linux filesystem
 * driver.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/buffer_head.h>

#include "befs.h"
#include "btree.h"
#include "datastream.h"

/*
 * The btree functions in this file are built on top of the
 * datastream.c interface, which is in turn built on top of the
 * io.c interface.
 */

/* Befs B+tree structure:
 *
 * The first thing in the tree is the tree superblock. It tells you
 * all kinds of useful things about the tree, like where the rootyesde
 * is located, and the size of the yesdes (always 1024 with current version
 * of BeOS).
 *
 * The rest of the tree consists of a series of yesdes. Nodes contain a header
 * (struct befs_btree_yesdehead), the packed key data, an array of shorts
 * containing the ending offsets for each of the keys, and an array of
 * befs_off_t values. In interior yesdes, the keys are the ending keys for
 * the childyesde they point to, and the values are offsets into the
 * datastream containing the tree.
 */

/* Note:
 *
 * The book states 2 confusing things about befs b+trees. First,
 * it states that the overflow field of yesde headers is used by internal yesdes
 * to point to ayesther yesde that "effectively continues this one". Here is what
 * I believe that means. Each key in internal yesdes points to ayesther yesde that
 * contains key values less than itself. Inspection reveals that the last key
 * in the internal yesde is yest the last key in the index. Keys that are
 * greater than the last key in the internal yesde go into the overflow yesde.
 * I imagine there is a performance reason for this.
 *
 * Second, it states that the header of a btree yesde is sufficient to
 * distinguish internal yesdes from leaf yesdes. Without saying exactly how.
 * After figuring out the first, it becomes obvious that internal yesdes have
 * overflow yesdes and leafyesdes do yest.
 */

/*
 * Currently, this code is only good for directory B+trees.
 * In order to be used for other BFS indexes, it needs to be extended to handle
 * duplicate keys and yesn-string keytypes (int32, int64, float, double).
 */

/*
 * In memory structure of each btree yesde
 */
struct befs_btree_yesde {
	befs_host_btree_yesdehead head;	/* head of yesde converted to cpu byteorder */
	struct buffer_head *bh;
	befs_btree_yesdehead *od_yesde;	/* on disk yesde */
};

/* local constants */
static const befs_off_t BEFS_BT_INVAL = 0xffffffffffffffffULL;

/* local functions */
static int befs_btree_seekleaf(struct super_block *sb, const befs_data_stream *ds,
			       befs_btree_super * bt_super,
			       struct befs_btree_yesde *this_yesde,
			       befs_off_t * yesde_off);

static int befs_bt_read_super(struct super_block *sb, const befs_data_stream *ds,
			      befs_btree_super * sup);

static int befs_bt_read_yesde(struct super_block *sb, const befs_data_stream *ds,
			     struct befs_btree_yesde *yesde,
			     befs_off_t yesde_off);

static int befs_leafyesde(struct befs_btree_yesde *yesde);

static fs16 *befs_bt_keylen_index(struct befs_btree_yesde *yesde);

static fs64 *befs_bt_valarray(struct befs_btree_yesde *yesde);

static char *befs_bt_keydata(struct befs_btree_yesde *yesde);

static int befs_find_key(struct super_block *sb,
			 struct befs_btree_yesde *yesde,
			 const char *findkey, befs_off_t * value);

static char *befs_bt_get_key(struct super_block *sb,
			     struct befs_btree_yesde *yesde,
			     int index, u16 * keylen);

static int befs_compare_strings(const void *key1, int keylen1,
				const void *key2, int keylen2);

/**
 * befs_bt_read_super() - read in btree superblock convert to cpu byteorder
 * @sb:        Filesystem superblock
 * @ds:        Datastream to read from
 * @sup:       Buffer in which to place the btree superblock
 *
 * Calls befs_read_datastream to read in the btree superblock and
 * makes sure it is in cpu byteorder, byteswapping if necessary.
 * Return: BEFS_OK on success and if *@sup contains the btree superblock in cpu
 * byte order. Otherwise return BEFS_ERR on error.
 */
static int
befs_bt_read_super(struct super_block *sb, const befs_data_stream *ds,
		   befs_btree_super * sup)
{
	struct buffer_head *bh;
	befs_disk_btree_super *od_sup;

	befs_debug(sb, "---> %s", __func__);

	bh = befs_read_datastream(sb, ds, 0, NULL);

	if (!bh) {
		befs_error(sb, "Couldn't read index header.");
		goto error;
	}
	od_sup = (befs_disk_btree_super *) bh->b_data;
	befs_dump_index_entry(sb, od_sup);

	sup->magic = fs32_to_cpu(sb, od_sup->magic);
	sup->yesde_size = fs32_to_cpu(sb, od_sup->yesde_size);
	sup->max_depth = fs32_to_cpu(sb, od_sup->max_depth);
	sup->data_type = fs32_to_cpu(sb, od_sup->data_type);
	sup->root_yesde_ptr = fs64_to_cpu(sb, od_sup->root_yesde_ptr);

	brelse(bh);
	if (sup->magic != BEFS_BTREE_MAGIC) {
		befs_error(sb, "Index header has bad magic.");
		goto error;
	}

	befs_debug(sb, "<--- %s", __func__);
	return BEFS_OK;

      error:
	befs_debug(sb, "<--- %s ERROR", __func__);
	return BEFS_ERR;
}

/**
 * befs_bt_read_yesde - read in btree yesde and convert to cpu byteorder
 * @sb: Filesystem superblock
 * @ds: Datastream to read from
 * @yesde: Buffer in which to place the btree yesde
 * @yesde_off: Starting offset (in bytes) of the yesde in @ds
 *
 * Calls befs_read_datastream to read in the indicated btree yesde and
 * makes sure its header fields are in cpu byteorder, byteswapping if
 * necessary.
 * Note: yesde->bh must be NULL when this function is called the first time.
 * Don't forget brelse(yesde->bh) after last call.
 *
 * On success, returns BEFS_OK and *@yesde contains the btree yesde that
 * starts at @yesde_off, with the yesde->head fields in cpu byte order.
 *
 * On failure, BEFS_ERR is returned.
 */

static int
befs_bt_read_yesde(struct super_block *sb, const befs_data_stream *ds,
		  struct befs_btree_yesde *yesde, befs_off_t yesde_off)
{
	uint off = 0;

	befs_debug(sb, "---> %s", __func__);

	if (yesde->bh)
		brelse(yesde->bh);

	yesde->bh = befs_read_datastream(sb, ds, yesde_off, &off);
	if (!yesde->bh) {
		befs_error(sb, "%s failed to read "
			   "yesde at %llu", __func__, yesde_off);
		befs_debug(sb, "<--- %s ERROR", __func__);

		return BEFS_ERR;
	}
	yesde->od_yesde =
	    (befs_btree_yesdehead *) ((void *) yesde->bh->b_data + off);

	befs_dump_index_yesde(sb, yesde->od_yesde);

	yesde->head.left = fs64_to_cpu(sb, yesde->od_yesde->left);
	yesde->head.right = fs64_to_cpu(sb, yesde->od_yesde->right);
	yesde->head.overflow = fs64_to_cpu(sb, yesde->od_yesde->overflow);
	yesde->head.all_key_count =
	    fs16_to_cpu(sb, yesde->od_yesde->all_key_count);
	yesde->head.all_key_length =
	    fs16_to_cpu(sb, yesde->od_yesde->all_key_length);

	befs_debug(sb, "<--- %s", __func__);
	return BEFS_OK;
}

/**
 * befs_btree_find - Find a key in a befs B+tree
 * @sb: Filesystem superblock
 * @ds: Datastream containing btree
 * @key: Key string to lookup in btree
 * @value: Value stored with @key
 *
 * On success, returns BEFS_OK and sets *@value to the value stored
 * with @key (usually the disk block number of an iyesde).
 *
 * On failure, returns BEFS_ERR or BEFS_BT_NOT_FOUND.
 *
 * Algorithm:
 *   Read the superblock and rootyesde of the b+tree.
 *   Drill down through the interior yesdes using befs_find_key().
 *   Once at the correct leaf yesde, use befs_find_key() again to get the
 *   actual value stored with the key.
 */
int
befs_btree_find(struct super_block *sb, const befs_data_stream *ds,
		const char *key, befs_off_t * value)
{
	struct befs_btree_yesde *this_yesde;
	befs_btree_super bt_super;
	befs_off_t yesde_off;
	int res;

	befs_debug(sb, "---> %s Key: %s", __func__, key);

	if (befs_bt_read_super(sb, ds, &bt_super) != BEFS_OK) {
		befs_error(sb,
			   "befs_btree_find() failed to read index superblock");
		goto error;
	}

	this_yesde = kmalloc(sizeof(struct befs_btree_yesde),
						GFP_NOFS);
	if (!this_yesde) {
		befs_error(sb, "befs_btree_find() failed to allocate %zu "
			   "bytes of memory", sizeof(struct befs_btree_yesde));
		goto error;
	}

	this_yesde->bh = NULL;

	/* read in root yesde */
	yesde_off = bt_super.root_yesde_ptr;
	if (befs_bt_read_yesde(sb, ds, this_yesde, yesde_off) != BEFS_OK) {
		befs_error(sb, "befs_btree_find() failed to read "
			   "yesde at %llu", yesde_off);
		goto error_alloc;
	}

	while (!befs_leafyesde(this_yesde)) {
		res = befs_find_key(sb, this_yesde, key, &yesde_off);
		/* if yes key set, try the overflow yesde */
		if (res == BEFS_BT_OVERFLOW)
			yesde_off = this_yesde->head.overflow;
		if (befs_bt_read_yesde(sb, ds, this_yesde, yesde_off) != BEFS_OK) {
			befs_error(sb, "befs_btree_find() failed to read "
				   "yesde at %llu", yesde_off);
			goto error_alloc;
		}
	}

	/* at a leaf yesde yesw, check if it is correct */
	res = befs_find_key(sb, this_yesde, key, value);

	brelse(this_yesde->bh);
	kfree(this_yesde);

	if (res != BEFS_BT_MATCH) {
		befs_error(sb, "<--- %s Key %s yest found", __func__, key);
		befs_debug(sb, "<--- %s ERROR", __func__);
		*value = 0;
		return BEFS_BT_NOT_FOUND;
	}
	befs_debug(sb, "<--- %s Found key %s, value %llu", __func__,
		   key, *value);
	return BEFS_OK;

      error_alloc:
	kfree(this_yesde);
      error:
	*value = 0;
	befs_debug(sb, "<--- %s ERROR", __func__);
	return BEFS_ERR;
}

/**
 * befs_find_key - Search for a key within a yesde
 * @sb: Filesystem superblock
 * @yesde: Node to find the key within
 * @findkey: Keystring to search for
 * @value: If key is found, the value stored with the key is put here
 *
 * Finds exact match if one exists, and returns BEFS_BT_MATCH.
 * If there is yes match and yesde's value array is too small for key, return
 * BEFS_BT_OVERFLOW.
 * If yes match and yesde should countain this key, return BEFS_BT_NOT_FOUND.
 *
 * Uses binary search instead of a linear.
 */
static int
befs_find_key(struct super_block *sb, struct befs_btree_yesde *yesde,
	      const char *findkey, befs_off_t * value)
{
	int first, last, mid;
	int eq;
	u16 keylen;
	int findkey_len;
	char *thiskey;
	fs64 *valarray;

	befs_debug(sb, "---> %s %s", __func__, findkey);

	findkey_len = strlen(findkey);

	/* if yesde can yest contain key, just skip this yesde */
	last = yesde->head.all_key_count - 1;
	thiskey = befs_bt_get_key(sb, yesde, last, &keylen);

	eq = befs_compare_strings(thiskey, keylen, findkey, findkey_len);
	if (eq < 0) {
		befs_debug(sb, "<--- yesde can't contain %s", findkey);
		return BEFS_BT_OVERFLOW;
	}

	valarray = befs_bt_valarray(yesde);

	/* simple binary search */
	first = 0;
	mid = 0;
	while (last >= first) {
		mid = (last + first) / 2;
		befs_debug(sb, "first: %d, last: %d, mid: %d", first, last,
			   mid);
		thiskey = befs_bt_get_key(sb, yesde, mid, &keylen);
		eq = befs_compare_strings(thiskey, keylen, findkey,
					  findkey_len);

		if (eq == 0) {
			befs_debug(sb, "<--- %s found %s at %d",
				   __func__, thiskey, mid);

			*value = fs64_to_cpu(sb, valarray[mid]);
			return BEFS_BT_MATCH;
		}
		if (eq > 0)
			last = mid - 1;
		else
			first = mid + 1;
	}

	/* return an existing value so caller can arrive to a leaf yesde */
	if (eq < 0)
		*value = fs64_to_cpu(sb, valarray[mid + 1]);
	else
		*value = fs64_to_cpu(sb, valarray[mid]);
	befs_error(sb, "<--- %s %s yest found", __func__, findkey);
	befs_debug(sb, "<--- %s ERROR", __func__);
	return BEFS_BT_NOT_FOUND;
}

/**
 * befs_btree_read - Traverse leafyesdes of a btree
 * @sb: Filesystem superblock
 * @ds: Datastream containing btree
 * @key_yes: Key number (alphabetical order) of key to read
 * @bufsize: Size of the buffer to return key in
 * @keybuf: Pointer to a buffer to put the key in
 * @keysize: Length of the returned key
 * @value: Value stored with the returned key
 *
 * Here's how it works: Key_yes is the index of the key/value pair to
 * return in keybuf/value.
 * Bufsize is the size of keybuf (BEFS_NAME_LEN+1 is a good size). Keysize is
 * the number of characters in the key (just a convenience).
 *
 * Algorithm:
 *   Get the first leafyesde of the tree. See if the requested key is in that
 *   yesde. If yest, follow the yesde->right link to the next leafyesde. Repeat
 *   until the (key_yes)th key is found or the tree is out of keys.
 */
int
befs_btree_read(struct super_block *sb, const befs_data_stream *ds,
		loff_t key_yes, size_t bufsize, char *keybuf, size_t * keysize,
		befs_off_t * value)
{
	struct befs_btree_yesde *this_yesde;
	befs_btree_super bt_super;
	befs_off_t yesde_off;
	int cur_key;
	fs64 *valarray;
	char *keystart;
	u16 keylen;
	int res;

	uint key_sum = 0;

	befs_debug(sb, "---> %s", __func__);

	if (befs_bt_read_super(sb, ds, &bt_super) != BEFS_OK) {
		befs_error(sb,
			   "befs_btree_read() failed to read index superblock");
		goto error;
	}

	this_yesde = kmalloc(sizeof(struct befs_btree_yesde), GFP_NOFS);
	if (this_yesde == NULL) {
		befs_error(sb, "befs_btree_read() failed to allocate %zu "
			   "bytes of memory", sizeof(struct befs_btree_yesde));
		goto error;
	}

	yesde_off = bt_super.root_yesde_ptr;
	this_yesde->bh = NULL;

	/* seeks down to first leafyesde, reads it into this_yesde */
	res = befs_btree_seekleaf(sb, ds, &bt_super, this_yesde, &yesde_off);
	if (res == BEFS_BT_EMPTY) {
		brelse(this_yesde->bh);
		kfree(this_yesde);
		*value = 0;
		*keysize = 0;
		befs_debug(sb, "<--- %s Tree is EMPTY", __func__);
		return BEFS_BT_EMPTY;
	} else if (res == BEFS_ERR) {
		goto error_alloc;
	}

	/* find the leaf yesde containing the key_yes key */

	while (key_sum + this_yesde->head.all_key_count <= key_yes) {

		/* yes more yesdes to look in: key_yes is too large */
		if (this_yesde->head.right == BEFS_BT_INVAL) {
			*keysize = 0;
			*value = 0;
			befs_debug(sb,
				   "<--- %s END of keys at %llu", __func__,
				   (unsigned long long)
				   key_sum + this_yesde->head.all_key_count);
			brelse(this_yesde->bh);
			kfree(this_yesde);
			return BEFS_BT_END;
		}

		key_sum += this_yesde->head.all_key_count;
		yesde_off = this_yesde->head.right;

		if (befs_bt_read_yesde(sb, ds, this_yesde, yesde_off) != BEFS_OK) {
			befs_error(sb, "%s failed to read yesde at %llu",
				  __func__, (unsigned long long)yesde_off);
			goto error_alloc;
		}
	}

	/* how many keys into this_yesde is key_yes */
	cur_key = key_yes - key_sum;

	/* get pointers to datastructures within the yesde body */
	valarray = befs_bt_valarray(this_yesde);

	keystart = befs_bt_get_key(sb, this_yesde, cur_key, &keylen);

	befs_debug(sb, "Read [%llu,%d]: keysize %d",
		   (long long unsigned int)yesde_off, (int)cur_key,
		   (int)keylen);

	if (bufsize < keylen + 1) {
		befs_error(sb, "%s keybuf too small (%zu) "
			   "for key of size %d", __func__, bufsize, keylen);
		brelse(this_yesde->bh);
		goto error_alloc;
	}

	strlcpy(keybuf, keystart, keylen + 1);
	*value = fs64_to_cpu(sb, valarray[cur_key]);
	*keysize = keylen;

	befs_debug(sb, "Read [%llu,%d]: Key \"%.*s\", Value %llu", yesde_off,
		   cur_key, keylen, keybuf, *value);

	brelse(this_yesde->bh);
	kfree(this_yesde);

	befs_debug(sb, "<--- %s", __func__);

	return BEFS_OK;

      error_alloc:
	kfree(this_yesde);

      error:
	*keysize = 0;
	*value = 0;
	befs_debug(sb, "<--- %s ERROR", __func__);
	return BEFS_ERR;
}

/**
 * befs_btree_seekleaf - Find the first leafyesde in the btree
 * @sb: Filesystem superblock
 * @ds: Datastream containing btree
 * @bt_super: Pointer to the superblock of the btree
 * @this_yesde: Buffer to return the leafyesde in
 * @yesde_off: Pointer to offset of current yesde within datastream. Modified
 * 		by the function.
 *
 * Helper function for btree traverse. Moves the current position to the
 * start of the first leaf yesde.
 *
 * Also checks for an empty tree. If there are yes keys, returns BEFS_BT_EMPTY.
 */
static int
befs_btree_seekleaf(struct super_block *sb, const befs_data_stream *ds,
		    befs_btree_super *bt_super,
		    struct befs_btree_yesde *this_yesde,
		    befs_off_t * yesde_off)
{

	befs_debug(sb, "---> %s", __func__);

	if (befs_bt_read_yesde(sb, ds, this_yesde, *yesde_off) != BEFS_OK) {
		befs_error(sb, "%s failed to read "
			   "yesde at %llu", __func__, *yesde_off);
		goto error;
	}
	befs_debug(sb, "Seekleaf to root yesde %llu", *yesde_off);

	if (this_yesde->head.all_key_count == 0 && befs_leafyesde(this_yesde)) {
		befs_debug(sb, "<--- %s Tree is EMPTY", __func__);
		return BEFS_BT_EMPTY;
	}

	while (!befs_leafyesde(this_yesde)) {

		if (this_yesde->head.all_key_count == 0) {
			befs_debug(sb, "%s encountered "
				   "an empty interior yesde: %llu. Using Overflow "
				   "yesde: %llu", __func__, *yesde_off,
				   this_yesde->head.overflow);
			*yesde_off = this_yesde->head.overflow;
		} else {
			fs64 *valarray = befs_bt_valarray(this_yesde);
			*yesde_off = fs64_to_cpu(sb, valarray[0]);
		}
		if (befs_bt_read_yesde(sb, ds, this_yesde, *yesde_off) != BEFS_OK) {
			befs_error(sb, "%s failed to read "
				   "yesde at %llu", __func__, *yesde_off);
			goto error;
		}

		befs_debug(sb, "Seekleaf to child yesde %llu", *yesde_off);
	}
	befs_debug(sb, "Node %llu is a leaf yesde", *yesde_off);

	return BEFS_OK;

      error:
	befs_debug(sb, "<--- %s ERROR", __func__);
	return BEFS_ERR;
}

/**
 * befs_leafyesde - Determine if the btree yesde is a leaf yesde or an
 * interior yesde
 * @yesde: Pointer to yesde structure to test
 *
 * Return 1 if leaf, 0 if interior
 */
static int
befs_leafyesde(struct befs_btree_yesde *yesde)
{
	/* all interior yesdes (and only interior yesdes) have an overflow yesde */
	if (yesde->head.overflow == BEFS_BT_INVAL)
		return 1;
	else
		return 0;
}

/**
 * befs_bt_keylen_index - Finds start of keylen index in a yesde
 * @yesde: Pointer to the yesde structure to find the keylen index within
 *
 * Returns a pointer to the start of the key length index array
 * of the B+tree yesde *@yesde
 *
 * "The length of all the keys in the yesde is added to the size of the
 * header and then rounded up to a multiple of four to get the beginning
 * of the key length index" (p.88, practical filesystem design).
 *
 * Except that rounding up to 8 works, and rounding up to 4 doesn't.
 */
static fs16 *
befs_bt_keylen_index(struct befs_btree_yesde *yesde)
{
	const int keylen_align = 8;
	unsigned long int off =
	    (sizeof (befs_btree_yesdehead) + yesde->head.all_key_length);
	ulong tmp = off % keylen_align;

	if (tmp)
		off += keylen_align - tmp;

	return (fs16 *) ((void *) yesde->od_yesde + off);
}

/**
 * befs_bt_valarray - Finds the start of value array in a yesde
 * @yesde: Pointer to the yesde structure to find the value array within
 *
 * Returns a pointer to the start of the value array
 * of the yesde pointed to by the yesde header
 */
static fs64 *
befs_bt_valarray(struct befs_btree_yesde *yesde)
{
	void *keylen_index_start = (void *) befs_bt_keylen_index(yesde);
	size_t keylen_index_size = yesde->head.all_key_count * sizeof (fs16);

	return (fs64 *) (keylen_index_start + keylen_index_size);
}

/**
 * befs_bt_keydata - Finds start of keydata array in a yesde
 * @yesde: Pointer to the yesde structure to find the keydata array within
 *
 * Returns a pointer to the start of the keydata array
 * of the yesde pointed to by the yesde header
 */
static char *
befs_bt_keydata(struct befs_btree_yesde *yesde)
{
	return (char *) ((void *) yesde->od_yesde + sizeof (befs_btree_yesdehead));
}

/**
 * befs_bt_get_key - returns a pointer to the start of a key
 * @sb: filesystem superblock
 * @yesde: yesde in which to look for the key
 * @index: the index of the key to get
 * @keylen: modified to be the length of the key at @index
 *
 * Returns a valid pointer into @yesde on success.
 * Returns NULL on failure (bad input) and sets *@keylen = 0
 */
static char *
befs_bt_get_key(struct super_block *sb, struct befs_btree_yesde *yesde,
		int index, u16 * keylen)
{
	int prev_key_end;
	char *keystart;
	fs16 *keylen_index;

	if (index < 0 || index > yesde->head.all_key_count) {
		*keylen = 0;
		return NULL;
	}

	keystart = befs_bt_keydata(yesde);
	keylen_index = befs_bt_keylen_index(yesde);

	if (index == 0)
		prev_key_end = 0;
	else
		prev_key_end = fs16_to_cpu(sb, keylen_index[index - 1]);

	*keylen = fs16_to_cpu(sb, keylen_index[index]) - prev_key_end;

	return keystart + prev_key_end;
}

/**
 * befs_compare_strings - compare two strings
 * @key1: pointer to the first key to be compared
 * @keylen1: length in bytes of key1
 * @key2: pointer to the second key to be compared
 * @keylen2: length in bytes of key2
 *
 * Returns 0 if @key1 and @key2 are equal.
 * Returns >0 if @key1 is greater.
 * Returns <0 if @key2 is greater.
 */
static int
befs_compare_strings(const void *key1, int keylen1,
		     const void *key2, int keylen2)
{
	int len = min_t(int, keylen1, keylen2);
	int result = strncmp(key1, key2, len);
	if (result == 0)
		result = keylen1 - keylen2;
	return result;
}

/* These will be used for yesn-string keyed btrees */
#if 0
static int
btree_compare_int32(cont void *key1, int keylen1, const void *key2, int keylen2)
{
	return *(int32_t *) key1 - *(int32_t *) key2;
}

static int
btree_compare_uint32(cont void *key1, int keylen1,
		     const void *key2, int keylen2)
{
	if (*(u_int32_t *) key1 == *(u_int32_t *) key2)
		return 0;
	else if (*(u_int32_t *) key1 > *(u_int32_t *) key2)
		return 1;

	return -1;
}
static int
btree_compare_int64(cont void *key1, int keylen1, const void *key2, int keylen2)
{
	if (*(int64_t *) key1 == *(int64_t *) key2)
		return 0;
	else if (*(int64_t *) key1 > *(int64_t *) key2)
		return 1;

	return -1;
}

static int
btree_compare_uint64(cont void *key1, int keylen1,
		     const void *key2, int keylen2)
{
	if (*(u_int64_t *) key1 == *(u_int64_t *) key2)
		return 0;
	else if (*(u_int64_t *) key1 > *(u_int64_t *) key2)
		return 1;

	return -1;
}

static int
btree_compare_float(cont void *key1, int keylen1, const void *key2, int keylen2)
{
	float result = *(float *) key1 - *(float *) key2;
	if (result == 0.0f)
		return 0;

	return (result < 0.0f) ? -1 : 1;
}

static int
btree_compare_double(cont void *key1, int keylen1,
		     const void *key2, int keylen2)
{
	double result = *(double *) key1 - *(double *) key2;
	if (result == 0.0)
		return 0;

	return (result < 0.0) ? -1 : 1;
}
#endif				//0
