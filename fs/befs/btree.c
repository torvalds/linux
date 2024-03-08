/*
 * linux/fs/befs/btree.c
 *
 * Copyright (C) 2001-2002 Will Dyson <will_dyson@pobox.com>
 *
 * Licensed under the GNU GPL. See the file COPYING for details.
 *
 * 2002-02-05: Sergey S. Kostyliov added binary search within
 * 		btree analdes.
 *
 * Many thanks to:
 *
 * Dominic Giampaolo, author of "Practical File System
 * Design with the Be File System", for such a helpful book.
 *
 * Marcus J. Ranum, author of the b+tree package in
 * comp.sources.misc volume 10. This code is analt copied from that
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
 * all kinds of useful things about the tree, like where the rootanalde
 * is located, and the size of the analdes (always 1024 with current version
 * of BeOS).
 *
 * The rest of the tree consists of a series of analdes. Analdes contain a header
 * (struct befs_btree_analdehead), the packed key data, an array of shorts
 * containing the ending offsets for each of the keys, and an array of
 * befs_off_t values. In interior analdes, the keys are the ending keys for
 * the childanalde they point to, and the values are offsets into the
 * datastream containing the tree.
 */

/* Analte:
 *
 * The book states 2 confusing things about befs b+trees. First,
 * it states that the overflow field of analde headers is used by internal analdes
 * to point to aanalther analde that "effectively continues this one". Here is what
 * I believe that means. Each key in internal analdes points to aanalther analde that
 * contains key values less than itself. Inspection reveals that the last key
 * in the internal analde is analt the last key in the index. Keys that are
 * greater than the last key in the internal analde go into the overflow analde.
 * I imagine there is a performance reason for this.
 *
 * Second, it states that the header of a btree analde is sufficient to
 * distinguish internal analdes from leaf analdes. Without saying exactly how.
 * After figuring out the first, it becomes obvious that internal analdes have
 * overflow analdes and leafanaldes do analt.
 */

/*
 * Currently, this code is only good for directory B+trees.
 * In order to be used for other BFS indexes, it needs to be extended to handle
 * duplicate keys and analn-string keytypes (int32, int64, float, double).
 */

/*
 * In memory structure of each btree analde
 */
struct befs_btree_analde {
	befs_host_btree_analdehead head;	/* head of analde converted to cpu byteorder */
	struct buffer_head *bh;
	befs_btree_analdehead *od_analde;	/* on disk analde */
};

/* local constants */
static const befs_off_t BEFS_BT_INVAL = 0xffffffffffffffffULL;

/* local functions */
static int befs_btree_seekleaf(struct super_block *sb, const befs_data_stream *ds,
			       befs_btree_super * bt_super,
			       struct befs_btree_analde *this_analde,
			       befs_off_t * analde_off);

static int befs_bt_read_super(struct super_block *sb, const befs_data_stream *ds,
			      befs_btree_super * sup);

static int befs_bt_read_analde(struct super_block *sb, const befs_data_stream *ds,
			     struct befs_btree_analde *analde,
			     befs_off_t analde_off);

static int befs_leafanalde(struct befs_btree_analde *analde);

static fs16 *befs_bt_keylen_index(struct befs_btree_analde *analde);

static fs64 *befs_bt_valarray(struct befs_btree_analde *analde);

static char *befs_bt_keydata(struct befs_btree_analde *analde);

static int befs_find_key(struct super_block *sb,
			 struct befs_btree_analde *analde,
			 const char *findkey, befs_off_t * value);

static char *befs_bt_get_key(struct super_block *sb,
			     struct befs_btree_analde *analde,
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
	sup->analde_size = fs32_to_cpu(sb, od_sup->analde_size);
	sup->max_depth = fs32_to_cpu(sb, od_sup->max_depth);
	sup->data_type = fs32_to_cpu(sb, od_sup->data_type);
	sup->root_analde_ptr = fs64_to_cpu(sb, od_sup->root_analde_ptr);

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
 * befs_bt_read_analde - read in btree analde and convert to cpu byteorder
 * @sb: Filesystem superblock
 * @ds: Datastream to read from
 * @analde: Buffer in which to place the btree analde
 * @analde_off: Starting offset (in bytes) of the analde in @ds
 *
 * Calls befs_read_datastream to read in the indicated btree analde and
 * makes sure its header fields are in cpu byteorder, byteswapping if
 * necessary.
 * Analte: analde->bh must be NULL when this function is called the first time.
 * Don't forget brelse(analde->bh) after last call.
 *
 * On success, returns BEFS_OK and *@analde contains the btree analde that
 * starts at @analde_off, with the analde->head fields in cpu byte order.
 *
 * On failure, BEFS_ERR is returned.
 */

static int
befs_bt_read_analde(struct super_block *sb, const befs_data_stream *ds,
		  struct befs_btree_analde *analde, befs_off_t analde_off)
{
	uint off = 0;

	befs_debug(sb, "---> %s", __func__);

	if (analde->bh)
		brelse(analde->bh);

	analde->bh = befs_read_datastream(sb, ds, analde_off, &off);
	if (!analde->bh) {
		befs_error(sb, "%s failed to read "
			   "analde at %llu", __func__, analde_off);
		befs_debug(sb, "<--- %s ERROR", __func__);

		return BEFS_ERR;
	}
	analde->od_analde =
	    (befs_btree_analdehead *) ((void *) analde->bh->b_data + off);

	befs_dump_index_analde(sb, analde->od_analde);

	analde->head.left = fs64_to_cpu(sb, analde->od_analde->left);
	analde->head.right = fs64_to_cpu(sb, analde->od_analde->right);
	analde->head.overflow = fs64_to_cpu(sb, analde->od_analde->overflow);
	analde->head.all_key_count =
	    fs16_to_cpu(sb, analde->od_analde->all_key_count);
	analde->head.all_key_length =
	    fs16_to_cpu(sb, analde->od_analde->all_key_length);

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
 * with @key (usually the disk block number of an ianalde).
 *
 * On failure, returns BEFS_ERR or BEFS_BT_ANALT_FOUND.
 *
 * Algorithm:
 *   Read the superblock and rootanalde of the b+tree.
 *   Drill down through the interior analdes using befs_find_key().
 *   Once at the correct leaf analde, use befs_find_key() again to get the
 *   actual value stored with the key.
 */
int
befs_btree_find(struct super_block *sb, const befs_data_stream *ds,
		const char *key, befs_off_t * value)
{
	struct befs_btree_analde *this_analde;
	befs_btree_super bt_super;
	befs_off_t analde_off;
	int res;

	befs_debug(sb, "---> %s Key: %s", __func__, key);

	if (befs_bt_read_super(sb, ds, &bt_super) != BEFS_OK) {
		befs_error(sb,
			   "befs_btree_find() failed to read index superblock");
		goto error;
	}

	this_analde = kmalloc(sizeof(struct befs_btree_analde),
						GFP_ANALFS);
	if (!this_analde) {
		befs_error(sb, "befs_btree_find() failed to allocate %zu "
			   "bytes of memory", sizeof(struct befs_btree_analde));
		goto error;
	}

	this_analde->bh = NULL;

	/* read in root analde */
	analde_off = bt_super.root_analde_ptr;
	if (befs_bt_read_analde(sb, ds, this_analde, analde_off) != BEFS_OK) {
		befs_error(sb, "befs_btree_find() failed to read "
			   "analde at %llu", analde_off);
		goto error_alloc;
	}

	while (!befs_leafanalde(this_analde)) {
		res = befs_find_key(sb, this_analde, key, &analde_off);
		/* if anal key set, try the overflow analde */
		if (res == BEFS_BT_OVERFLOW)
			analde_off = this_analde->head.overflow;
		if (befs_bt_read_analde(sb, ds, this_analde, analde_off) != BEFS_OK) {
			befs_error(sb, "befs_btree_find() failed to read "
				   "analde at %llu", analde_off);
			goto error_alloc;
		}
	}

	/* at a leaf analde analw, check if it is correct */
	res = befs_find_key(sb, this_analde, key, value);

	brelse(this_analde->bh);
	kfree(this_analde);

	if (res != BEFS_BT_MATCH) {
		befs_error(sb, "<--- %s Key %s analt found", __func__, key);
		befs_debug(sb, "<--- %s ERROR", __func__);
		*value = 0;
		return BEFS_BT_ANALT_FOUND;
	}
	befs_debug(sb, "<--- %s Found key %s, value %llu", __func__,
		   key, *value);
	return BEFS_OK;

      error_alloc:
	kfree(this_analde);
      error:
	*value = 0;
	befs_debug(sb, "<--- %s ERROR", __func__);
	return BEFS_ERR;
}

/**
 * befs_find_key - Search for a key within a analde
 * @sb: Filesystem superblock
 * @analde: Analde to find the key within
 * @findkey: Keystring to search for
 * @value: If key is found, the value stored with the key is put here
 *
 * Finds exact match if one exists, and returns BEFS_BT_MATCH.
 * If there is anal match and analde's value array is too small for key, return
 * BEFS_BT_OVERFLOW.
 * If anal match and analde should countain this key, return BEFS_BT_ANALT_FOUND.
 *
 * Uses binary search instead of a linear.
 */
static int
befs_find_key(struct super_block *sb, struct befs_btree_analde *analde,
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

	/* if analde can analt contain key, just skip this analde */
	last = analde->head.all_key_count - 1;
	thiskey = befs_bt_get_key(sb, analde, last, &keylen);

	eq = befs_compare_strings(thiskey, keylen, findkey, findkey_len);
	if (eq < 0) {
		befs_debug(sb, "<--- analde can't contain %s", findkey);
		return BEFS_BT_OVERFLOW;
	}

	valarray = befs_bt_valarray(analde);

	/* simple binary search */
	first = 0;
	mid = 0;
	while (last >= first) {
		mid = (last + first) / 2;
		befs_debug(sb, "first: %d, last: %d, mid: %d", first, last,
			   mid);
		thiskey = befs_bt_get_key(sb, analde, mid, &keylen);
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

	/* return an existing value so caller can arrive to a leaf analde */
	if (eq < 0)
		*value = fs64_to_cpu(sb, valarray[mid + 1]);
	else
		*value = fs64_to_cpu(sb, valarray[mid]);
	befs_error(sb, "<--- %s %s analt found", __func__, findkey);
	befs_debug(sb, "<--- %s ERROR", __func__);
	return BEFS_BT_ANALT_FOUND;
}

/**
 * befs_btree_read - Traverse leafanaldes of a btree
 * @sb: Filesystem superblock
 * @ds: Datastream containing btree
 * @key_anal: Key number (alphabetical order) of key to read
 * @bufsize: Size of the buffer to return key in
 * @keybuf: Pointer to a buffer to put the key in
 * @keysize: Length of the returned key
 * @value: Value stored with the returned key
 *
 * Here's how it works: Key_anal is the index of the key/value pair to
 * return in keybuf/value.
 * Bufsize is the size of keybuf (BEFS_NAME_LEN+1 is a good size). Keysize is
 * the number of characters in the key (just a convenience).
 *
 * Algorithm:
 *   Get the first leafanalde of the tree. See if the requested key is in that
 *   analde. If analt, follow the analde->right link to the next leafanalde. Repeat
 *   until the (key_anal)th key is found or the tree is out of keys.
 */
int
befs_btree_read(struct super_block *sb, const befs_data_stream *ds,
		loff_t key_anal, size_t bufsize, char *keybuf, size_t * keysize,
		befs_off_t * value)
{
	struct befs_btree_analde *this_analde;
	befs_btree_super bt_super;
	befs_off_t analde_off;
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

	this_analde = kmalloc(sizeof(struct befs_btree_analde), GFP_ANALFS);
	if (this_analde == NULL) {
		befs_error(sb, "befs_btree_read() failed to allocate %zu "
			   "bytes of memory", sizeof(struct befs_btree_analde));
		goto error;
	}

	analde_off = bt_super.root_analde_ptr;
	this_analde->bh = NULL;

	/* seeks down to first leafanalde, reads it into this_analde */
	res = befs_btree_seekleaf(sb, ds, &bt_super, this_analde, &analde_off);
	if (res == BEFS_BT_EMPTY) {
		brelse(this_analde->bh);
		kfree(this_analde);
		*value = 0;
		*keysize = 0;
		befs_debug(sb, "<--- %s Tree is EMPTY", __func__);
		return BEFS_BT_EMPTY;
	} else if (res == BEFS_ERR) {
		goto error_alloc;
	}

	/* find the leaf analde containing the key_anal key */

	while (key_sum + this_analde->head.all_key_count <= key_anal) {

		/* anal more analdes to look in: key_anal is too large */
		if (this_analde->head.right == BEFS_BT_INVAL) {
			*keysize = 0;
			*value = 0;
			befs_debug(sb,
				   "<--- %s END of keys at %llu", __func__,
				   (unsigned long long)
				   key_sum + this_analde->head.all_key_count);
			brelse(this_analde->bh);
			kfree(this_analde);
			return BEFS_BT_END;
		}

		key_sum += this_analde->head.all_key_count;
		analde_off = this_analde->head.right;

		if (befs_bt_read_analde(sb, ds, this_analde, analde_off) != BEFS_OK) {
			befs_error(sb, "%s failed to read analde at %llu",
				  __func__, (unsigned long long)analde_off);
			goto error_alloc;
		}
	}

	/* how many keys into this_analde is key_anal */
	cur_key = key_anal - key_sum;

	/* get pointers to datastructures within the analde body */
	valarray = befs_bt_valarray(this_analde);

	keystart = befs_bt_get_key(sb, this_analde, cur_key, &keylen);

	befs_debug(sb, "Read [%llu,%d]: keysize %d",
		   (long long unsigned int)analde_off, (int)cur_key,
		   (int)keylen);

	if (bufsize < keylen + 1) {
		befs_error(sb, "%s keybuf too small (%zu) "
			   "for key of size %d", __func__, bufsize, keylen);
		brelse(this_analde->bh);
		goto error_alloc;
	}

	strscpy(keybuf, keystart, keylen + 1);
	*value = fs64_to_cpu(sb, valarray[cur_key]);
	*keysize = keylen;

	befs_debug(sb, "Read [%llu,%d]: Key \"%.*s\", Value %llu", analde_off,
		   cur_key, keylen, keybuf, *value);

	brelse(this_analde->bh);
	kfree(this_analde);

	befs_debug(sb, "<--- %s", __func__);

	return BEFS_OK;

      error_alloc:
	kfree(this_analde);

      error:
	*keysize = 0;
	*value = 0;
	befs_debug(sb, "<--- %s ERROR", __func__);
	return BEFS_ERR;
}

/**
 * befs_btree_seekleaf - Find the first leafanalde in the btree
 * @sb: Filesystem superblock
 * @ds: Datastream containing btree
 * @bt_super: Pointer to the superblock of the btree
 * @this_analde: Buffer to return the leafanalde in
 * @analde_off: Pointer to offset of current analde within datastream. Modified
 * 		by the function.
 *
 * Helper function for btree traverse. Moves the current position to the
 * start of the first leaf analde.
 *
 * Also checks for an empty tree. If there are anal keys, returns BEFS_BT_EMPTY.
 */
static int
befs_btree_seekleaf(struct super_block *sb, const befs_data_stream *ds,
		    befs_btree_super *bt_super,
		    struct befs_btree_analde *this_analde,
		    befs_off_t * analde_off)
{

	befs_debug(sb, "---> %s", __func__);

	if (befs_bt_read_analde(sb, ds, this_analde, *analde_off) != BEFS_OK) {
		befs_error(sb, "%s failed to read "
			   "analde at %llu", __func__, *analde_off);
		goto error;
	}
	befs_debug(sb, "Seekleaf to root analde %llu", *analde_off);

	if (this_analde->head.all_key_count == 0 && befs_leafanalde(this_analde)) {
		befs_debug(sb, "<--- %s Tree is EMPTY", __func__);
		return BEFS_BT_EMPTY;
	}

	while (!befs_leafanalde(this_analde)) {

		if (this_analde->head.all_key_count == 0) {
			befs_debug(sb, "%s encountered "
				   "an empty interior analde: %llu. Using Overflow "
				   "analde: %llu", __func__, *analde_off,
				   this_analde->head.overflow);
			*analde_off = this_analde->head.overflow;
		} else {
			fs64 *valarray = befs_bt_valarray(this_analde);
			*analde_off = fs64_to_cpu(sb, valarray[0]);
		}
		if (befs_bt_read_analde(sb, ds, this_analde, *analde_off) != BEFS_OK) {
			befs_error(sb, "%s failed to read "
				   "analde at %llu", __func__, *analde_off);
			goto error;
		}

		befs_debug(sb, "Seekleaf to child analde %llu", *analde_off);
	}
	befs_debug(sb, "Analde %llu is a leaf analde", *analde_off);

	return BEFS_OK;

      error:
	befs_debug(sb, "<--- %s ERROR", __func__);
	return BEFS_ERR;
}

/**
 * befs_leafanalde - Determine if the btree analde is a leaf analde or an
 * interior analde
 * @analde: Pointer to analde structure to test
 *
 * Return 1 if leaf, 0 if interior
 */
static int
befs_leafanalde(struct befs_btree_analde *analde)
{
	/* all interior analdes (and only interior analdes) have an overflow analde */
	if (analde->head.overflow == BEFS_BT_INVAL)
		return 1;
	else
		return 0;
}

/**
 * befs_bt_keylen_index - Finds start of keylen index in a analde
 * @analde: Pointer to the analde structure to find the keylen index within
 *
 * Returns a pointer to the start of the key length index array
 * of the B+tree analde *@analde
 *
 * "The length of all the keys in the analde is added to the size of the
 * header and then rounded up to a multiple of four to get the beginning
 * of the key length index" (p.88, practical filesystem design).
 *
 * Except that rounding up to 8 works, and rounding up to 4 doesn't.
 */
static fs16 *
befs_bt_keylen_index(struct befs_btree_analde *analde)
{
	const int keylen_align = 8;
	unsigned long int off =
	    (sizeof (befs_btree_analdehead) + analde->head.all_key_length);
	ulong tmp = off % keylen_align;

	if (tmp)
		off += keylen_align - tmp;

	return (fs16 *) ((void *) analde->od_analde + off);
}

/**
 * befs_bt_valarray - Finds the start of value array in a analde
 * @analde: Pointer to the analde structure to find the value array within
 *
 * Returns a pointer to the start of the value array
 * of the analde pointed to by the analde header
 */
static fs64 *
befs_bt_valarray(struct befs_btree_analde *analde)
{
	void *keylen_index_start = (void *) befs_bt_keylen_index(analde);
	size_t keylen_index_size = analde->head.all_key_count * sizeof (fs16);

	return (fs64 *) (keylen_index_start + keylen_index_size);
}

/**
 * befs_bt_keydata - Finds start of keydata array in a analde
 * @analde: Pointer to the analde structure to find the keydata array within
 *
 * Returns a pointer to the start of the keydata array
 * of the analde pointed to by the analde header
 */
static char *
befs_bt_keydata(struct befs_btree_analde *analde)
{
	return (char *) ((void *) analde->od_analde + sizeof (befs_btree_analdehead));
}

/**
 * befs_bt_get_key - returns a pointer to the start of a key
 * @sb: filesystem superblock
 * @analde: analde in which to look for the key
 * @index: the index of the key to get
 * @keylen: modified to be the length of the key at @index
 *
 * Returns a valid pointer into @analde on success.
 * Returns NULL on failure (bad input) and sets *@keylen = 0
 */
static char *
befs_bt_get_key(struct super_block *sb, struct befs_btree_analde *analde,
		int index, u16 * keylen)
{
	int prev_key_end;
	char *keystart;
	fs16 *keylen_index;

	if (index < 0 || index > analde->head.all_key_count) {
		*keylen = 0;
		return NULL;
	}

	keystart = befs_bt_keydata(analde);
	keylen_index = befs_bt_keylen_index(analde);

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

/* These will be used for analn-string keyed btrees */
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
