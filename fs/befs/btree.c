/*
 * linux/fs/befs/btree.c
 *
 * Copyright (C) 2001-2002 Will Dyson <will_dyson@pobox.com>
 *
 * Licensed under the GNU GPL. See the file COPYING for details.
 *
 * 2002-02-05: Sergey S. Kostyliov added binary search within
 * 		btree nodes.
 *
 * Many thanks to:
 *
 * Dominic Giampaolo, author of "Practical File System
 * Design with the Be File System", for such a helpful book.
 * 
 * Marcus J. Ranum, author of the b+tree package in 
 * comp.sources.misc volume 10. This code is not copied from that
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
 * all kinds of useful things about the tree, like where the rootnode
 * is located, and the size of the nodes (always 1024 with current version
 * of BeOS).
 *
 * The rest of the tree consists of a series of nodes. Nodes contain a header
 * (struct befs_btree_nodehead), the packed key data, an array of shorts 
 * containing the ending offsets for each of the keys, and an array of
 * befs_off_t values. In interior nodes, the keys are the ending keys for 
 * the childnode they point to, and the values are offsets into the 
 * datastream containing the tree. 
 */

/* Note:
 * 
 * The book states 2 confusing things about befs b+trees. First, 
 * it states that the overflow field of node headers is used by internal nodes
 * to point to another node that "effectively continues this one". Here is what
 * I believe that means. Each key in internal nodes points to another node that
 * contains key values less than itself. Inspection reveals that the last key 
 * in the internal node is not the last key in the index. Keys that are 
 * greater than the last key in the internal node go into the overflow node. 
 * I imagine there is a performance reason for this.
 *
 * Second, it states that the header of a btree node is sufficient to 
 * distinguish internal nodes from leaf nodes. Without saying exactly how. 
 * After figuring out the first, it becomes obvious that internal nodes have
 * overflow nodes and leafnodes do not.
 */

/* 
 * Currently, this code is only good for directory B+trees.
 * In order to be used for other BFS indexes, it needs to be extended to handle
 * duplicate keys and non-string keytypes (int32, int64, float, double).
 */

/*
 * In memory structure of each btree node
 */
struct befs_btree_node {
	befs_host_btree_nodehead head;	/* head of node converted to cpu byteorder */
	struct buffer_head *bh;
	befs_btree_nodehead *od_node;	/* on disk node */
};

/* local constants */
static const befs_off_t befs_bt_inval = 0xffffffffffffffffULL;

/* local functions */
static int befs_btree_seekleaf(struct super_block *sb, const befs_data_stream *ds,
			       befs_btree_super * bt_super,
			       struct befs_btree_node *this_node,
			       befs_off_t * node_off);

static int befs_bt_read_super(struct super_block *sb, const befs_data_stream *ds,
			      befs_btree_super * sup);

static int befs_bt_read_node(struct super_block *sb, const befs_data_stream *ds,
			     struct befs_btree_node *node,
			     befs_off_t node_off);

static int befs_leafnode(struct befs_btree_node *node);

static fs16 *befs_bt_keylen_index(struct befs_btree_node *node);

static fs64 *befs_bt_valarray(struct befs_btree_node *node);

static char *befs_bt_keydata(struct befs_btree_node *node);

static int befs_find_key(struct super_block *sb,
			 struct befs_btree_node *node,
			 const char *findkey, befs_off_t * value);

static char *befs_bt_get_key(struct super_block *sb,
			     struct befs_btree_node *node,
			     int index, u16 * keylen);

static int befs_compare_strings(const void *key1, int keylen1,
				const void *key2, int keylen2);

/**
 * befs_bt_read_super - read in btree superblock convert to cpu byteorder
 * @sb: Filesystem superblock
 * @ds: Datastream to read from
 * @sup: Buffer in which to place the btree superblock
 *
 * Calls befs_read_datastream to read in the btree superblock and
 * makes sure it is in cpu byteorder, byteswapping if necessary.
 *
 * On success, returns BEFS_OK and *@sup contains the btree superblock,
 * in cpu byte order.
 *
 * On failure, BEFS_ERR is returned.
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
	sup->node_size = fs32_to_cpu(sb, od_sup->node_size);
	sup->max_depth = fs32_to_cpu(sb, od_sup->max_depth);
	sup->data_type = fs32_to_cpu(sb, od_sup->data_type);
	sup->root_node_ptr = fs64_to_cpu(sb, od_sup->root_node_ptr);
	sup->free_node_ptr = fs64_to_cpu(sb, od_sup->free_node_ptr);
	sup->max_size = fs64_to_cpu(sb, od_sup->max_size);

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
 * befs_bt_read_node - read in btree node and convert to cpu byteorder
 * @sb: Filesystem superblock
 * @ds: Datastream to read from
 * @node: Buffer in which to place the btree node
 * @node_off: Starting offset (in bytes) of the node in @ds
 *
 * Calls befs_read_datastream to read in the indicated btree node and
 * makes sure its header fields are in cpu byteorder, byteswapping if
 * necessary.
 * Note: node->bh must be NULL when this function called first
 * time. Don't forget brelse(node->bh) after last call.
 *
 * On success, returns BEFS_OK and *@node contains the btree node that
 * starts at @node_off, with the node->head fields in cpu byte order.
 *
 * On failure, BEFS_ERR is returned.
 */

static int
befs_bt_read_node(struct super_block *sb, const befs_data_stream *ds,
		  struct befs_btree_node *node, befs_off_t node_off)
{
	uint off = 0;

	befs_debug(sb, "---> %s", __func__);

	if (node->bh)
		brelse(node->bh);

	node->bh = befs_read_datastream(sb, ds, node_off, &off);
	if (!node->bh) {
		befs_error(sb, "%s failed to read "
			   "node at %llu", __func__, node_off);
		befs_debug(sb, "<--- %s ERROR", __func__);

		return BEFS_ERR;
	}
	node->od_node =
	    (befs_btree_nodehead *) ((void *) node->bh->b_data + off);

	befs_dump_index_node(sb, node->od_node);

	node->head.left = fs64_to_cpu(sb, node->od_node->left);
	node->head.right = fs64_to_cpu(sb, node->od_node->right);
	node->head.overflow = fs64_to_cpu(sb, node->od_node->overflow);
	node->head.all_key_count =
	    fs16_to_cpu(sb, node->od_node->all_key_count);
	node->head.all_key_length =
	    fs16_to_cpu(sb, node->od_node->all_key_length);

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
 * with @key (usually the disk block number of an inode).
 *
 * On failure, returns BEFS_ERR or BEFS_BT_NOT_FOUND.
 * 
 * Algorithm: 
 *   Read the superblock and rootnode of the b+tree.
 *   Drill down through the interior nodes using befs_find_key().
 *   Once at the correct leaf node, use befs_find_key() again to get the
 *   actuall value stored with the key.
 */
int
befs_btree_find(struct super_block *sb, const befs_data_stream *ds,
		const char *key, befs_off_t * value)
{
	struct befs_btree_node *this_node;
	befs_btree_super bt_super;
	befs_off_t node_off;
	int res;

	befs_debug(sb, "---> %s Key: %s", __func__, key);

	if (befs_bt_read_super(sb, ds, &bt_super) != BEFS_OK) {
		befs_error(sb,
			   "befs_btree_find() failed to read index superblock");
		goto error;
	}

	this_node = kmalloc(sizeof(struct befs_btree_node),
						GFP_NOFS);
	if (!this_node) {
		befs_error(sb, "befs_btree_find() failed to allocate %zu "
			   "bytes of memory", sizeof(struct befs_btree_node));
		goto error;
	}

	this_node->bh = NULL;

	/* read in root node */
	node_off = bt_super.root_node_ptr;
	if (befs_bt_read_node(sb, ds, this_node, node_off) != BEFS_OK) {
		befs_error(sb, "befs_btree_find() failed to read "
			   "node at %llu", node_off);
		goto error_alloc;
	}

	while (!befs_leafnode(this_node)) {
		res = befs_find_key(sb, this_node, key, &node_off);
		if (res == BEFS_BT_NOT_FOUND)
			node_off = this_node->head.overflow;
		/* if no match, go to overflow node */
		if (befs_bt_read_node(sb, ds, this_node, node_off) != BEFS_OK) {
			befs_error(sb, "befs_btree_find() failed to read "
				   "node at %llu", node_off);
			goto error_alloc;
		}
	}

	/* at the correct leaf node now */

	res = befs_find_key(sb, this_node, key, value);

	brelse(this_node->bh);
	kfree(this_node);

	if (res != BEFS_BT_MATCH) {
		befs_error(sb, "<--- %s Key %s not found", __func__, key);
		befs_debug(sb, "<--- %s ERROR", __func__);
		*value = 0;
		return BEFS_BT_NOT_FOUND;
	}
	befs_debug(sb, "<--- %s Found key %s, value %llu", __func__,
		   key, *value);
	return BEFS_OK;

      error_alloc:
	kfree(this_node);
      error:
	*value = 0;
	befs_debug(sb, "<--- %s ERROR", __func__);
	return BEFS_ERR;
}

/**
 * befs_find_key - Search for a key within a node
 * @sb: Filesystem superblock
 * @node: Node to find the key within
 * @findkey: Keystring to search for
 * @value: If key is found, the value stored with the key is put here
 *
 * finds exact match if one exists, and returns BEFS_BT_MATCH
 * If no exact match, finds first key in node that is greater
 * (alphabetically) than the search key and returns BEFS_BT_PARMATCH
 * (for partial match, I guess). Can you think of something better to
 * call it?
 *
 * If no key was a match or greater than the search key, return
 * BEFS_BT_NOT_FOUND.
 *
 * Use binary search instead of a linear.
 */
static int
befs_find_key(struct super_block *sb, struct befs_btree_node *node,
	      const char *findkey, befs_off_t * value)
{
	int first, last, mid;
	int eq;
	u16 keylen;
	int findkey_len;
	char *thiskey;
	fs64 *valarray;

	befs_debug(sb, "---> %s %s", __func__, findkey);

	*value = 0;

	findkey_len = strlen(findkey);

	/* if node can not contain key, just skeep this node */
	last = node->head.all_key_count - 1;
	thiskey = befs_bt_get_key(sb, node, last, &keylen);

	eq = befs_compare_strings(thiskey, keylen, findkey, findkey_len);
	if (eq < 0) {
		befs_error(sb, "<--- %s %s not found", __func__, findkey);
		befs_debug(sb, "<--- %s ERROR", __func__);
		return BEFS_BT_NOT_FOUND;
	}

	valarray = befs_bt_valarray(node);

	/* simple binary search */
	first = 0;
	mid = 0;
	while (last >= first) {
		mid = (last + first) / 2;
		befs_debug(sb, "first: %d, last: %d, mid: %d", first, last,
			   mid);
		thiskey = befs_bt_get_key(sb, node, mid, &keylen);
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
	if (eq < 0)
		*value = fs64_to_cpu(sb, valarray[mid + 1]);
	else
		*value = fs64_to_cpu(sb, valarray[mid]);
	befs_debug(sb, "<--- %s found %s at %d", __func__, thiskey, mid);
	return BEFS_BT_PARMATCH;
}

/**
 * befs_btree_read - Traverse leafnodes of a btree
 * @sb: Filesystem superblock
 * @ds: Datastream containing btree
 * @key_no: Key number (alphabetical order) of key to read
 * @bufsize: Size of the buffer to return key in
 * @keybuf: Pointer to a buffer to put the key in
 * @keysize: Length of the returned key
 * @value: Value stored with the returned key
 *
 * Heres how it works: Key_no is the index of the key/value pair to 
 * return in keybuf/value.
 * Bufsize is the size of keybuf (BEFS_NAME_LEN+1 is a good size). Keysize is 
 * the number of characters in the key (just a convenience).
 *
 * Algorithm:
 *   Get the first leafnode of the tree. See if the requested key is in that
 *   node. If not, follow the node->right link to the next leafnode. Repeat 
 *   until the (key_no)th key is found or the tree is out of keys.
 */
int
befs_btree_read(struct super_block *sb, const befs_data_stream *ds,
		loff_t key_no, size_t bufsize, char *keybuf, size_t * keysize,
		befs_off_t * value)
{
	struct befs_btree_node *this_node;
	befs_btree_super bt_super;
	befs_off_t node_off = 0;
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

	this_node = kmalloc(sizeof(struct befs_btree_node), GFP_NOFS);
	if (this_node == NULL) {
		befs_error(sb, "befs_btree_read() failed to allocate %zu "
			   "bytes of memory", sizeof(struct befs_btree_node));
		goto error;
	}

	node_off = bt_super.root_node_ptr;
	this_node->bh = NULL;

	/* seeks down to first leafnode, reads it into this_node */
	res = befs_btree_seekleaf(sb, ds, &bt_super, this_node, &node_off);
	if (res == BEFS_BT_EMPTY) {
		brelse(this_node->bh);
		kfree(this_node);
		*value = 0;
		*keysize = 0;
		befs_debug(sb, "<--- %s Tree is EMPTY", __func__);
		return BEFS_BT_EMPTY;
	} else if (res == BEFS_ERR) {
		goto error_alloc;
	}

	/* find the leaf node containing the key_no key */

	while (key_sum + this_node->head.all_key_count <= key_no) {

		/* no more nodes to look in: key_no is too large */
		if (this_node->head.right == befs_bt_inval) {
			*keysize = 0;
			*value = 0;
			befs_debug(sb,
				   "<--- %s END of keys at %llu", __func__,
				   (unsigned long long)
				   key_sum + this_node->head.all_key_count);
			brelse(this_node->bh);
			kfree(this_node);
			return BEFS_BT_END;
		}

		key_sum += this_node->head.all_key_count;
		node_off = this_node->head.right;

		if (befs_bt_read_node(sb, ds, this_node, node_off) != BEFS_OK) {
			befs_error(sb, "%s failed to read node at %llu",
				  __func__, (unsigned long long)node_off);
			goto error_alloc;
		}
	}

	/* how many keys into this_node is key_no */
	cur_key = key_no - key_sum;

	/* get pointers to datastructures within the node body */
	valarray = befs_bt_valarray(this_node);

	keystart = befs_bt_get_key(sb, this_node, cur_key, &keylen);

	befs_debug(sb, "Read [%llu,%d]: keysize %d",
		   (long long unsigned int)node_off, (int)cur_key,
		   (int)keylen);

	if (bufsize < keylen + 1) {
		befs_error(sb, "%s keybuf too small (%zu) "
			   "for key of size %d", __func__, bufsize, keylen);
		brelse(this_node->bh);
		goto error_alloc;
	}

	strlcpy(keybuf, keystart, keylen + 1);
	*value = fs64_to_cpu(sb, valarray[cur_key]);
	*keysize = keylen;

	befs_debug(sb, "Read [%llu,%d]: Key \"%.*s\", Value %llu", node_off,
		   cur_key, keylen, keybuf, *value);

	brelse(this_node->bh);
	kfree(this_node);

	befs_debug(sb, "<--- %s", __func__);

	return BEFS_OK;

      error_alloc:
	kfree(this_node);

      error:
	*keysize = 0;
	*value = 0;
	befs_debug(sb, "<--- %s ERROR", __func__);
	return BEFS_ERR;
}

/**
 * befs_btree_seekleaf - Find the first leafnode in the btree
 * @sb: Filesystem superblock
 * @ds: Datastream containing btree
 * @bt_super: Pointer to the superblock of the btree
 * @this_node: Buffer to return the leafnode in
 * @node_off: Pointer to offset of current node within datastream. Modified
 * 		by the function.
 *
 *
 * Helper function for btree traverse. Moves the current position to the 
 * start of the first leaf node.
 *
 * Also checks for an empty tree. If there are no keys, returns BEFS_BT_EMPTY.
 */
static int
befs_btree_seekleaf(struct super_block *sb, const befs_data_stream *ds,
		    befs_btree_super *bt_super,
		    struct befs_btree_node *this_node,
		    befs_off_t * node_off)
{

	befs_debug(sb, "---> %s", __func__);

	if (befs_bt_read_node(sb, ds, this_node, *node_off) != BEFS_OK) {
		befs_error(sb, "%s failed to read "
			   "node at %llu", __func__, *node_off);
		goto error;
	}
	befs_debug(sb, "Seekleaf to root node %llu", *node_off);

	if (this_node->head.all_key_count == 0 && befs_leafnode(this_node)) {
		befs_debug(sb, "<--- %s Tree is EMPTY", __func__);
		return BEFS_BT_EMPTY;
	}

	while (!befs_leafnode(this_node)) {

		if (this_node->head.all_key_count == 0) {
			befs_debug(sb, "%s encountered "
				   "an empty interior node: %llu. Using Overflow "
				   "node: %llu", __func__, *node_off,
				   this_node->head.overflow);
			*node_off = this_node->head.overflow;
		} else {
			fs64 *valarray = befs_bt_valarray(this_node);
			*node_off = fs64_to_cpu(sb, valarray[0]);
		}
		if (befs_bt_read_node(sb, ds, this_node, *node_off) != BEFS_OK) {
			befs_error(sb, "%s failed to read "
				   "node at %llu", __func__, *node_off);
			goto error;
		}

		befs_debug(sb, "Seekleaf to child node %llu", *node_off);
	}
	befs_debug(sb, "Node %llu is a leaf node", *node_off);

	return BEFS_OK;

      error:
	befs_debug(sb, "<--- %s ERROR", __func__);
	return BEFS_ERR;
}

/**
 * befs_leafnode - Determine if the btree node is a leaf node or an 
 * interior node
 * @node: Pointer to node structure to test
 * 
 * Return 1 if leaf, 0 if interior
 */
static int
befs_leafnode(struct befs_btree_node *node)
{
	/* all interior nodes (and only interior nodes) have an overflow node */
	if (node->head.overflow == befs_bt_inval)
		return 1;
	else
		return 0;
}

/**
 * befs_bt_keylen_index - Finds start of keylen index in a node
 * @node: Pointer to the node structure to find the keylen index within
 *
 * Returns a pointer to the start of the key length index array
 * of the B+tree node *@node
 *
 * "The length of all the keys in the node is added to the size of the
 * header and then rounded up to a multiple of four to get the beginning
 * of the key length index" (p.88, practical filesystem design).
 *
 * Except that rounding up to 8 works, and rounding up to 4 doesn't.
 */
static fs16 *
befs_bt_keylen_index(struct befs_btree_node *node)
{
	const int keylen_align = 8;
	unsigned long int off =
	    (sizeof (befs_btree_nodehead) + node->head.all_key_length);
	ulong tmp = off % keylen_align;

	if (tmp)
		off += keylen_align - tmp;

	return (fs16 *) ((void *) node->od_node + off);
}

/**
 * befs_bt_valarray - Finds the start of value array in a node
 * @node: Pointer to the node structure to find the value array within
 *
 * Returns a pointer to the start of the value array
 * of the node pointed to by the node header
 */
static fs64 *
befs_bt_valarray(struct befs_btree_node *node)
{
	void *keylen_index_start = (void *) befs_bt_keylen_index(node);
	size_t keylen_index_size = node->head.all_key_count * sizeof (fs16);

	return (fs64 *) (keylen_index_start + keylen_index_size);
}

/**
 * befs_bt_keydata - Finds start of keydata array in a node
 * @node: Pointer to the node structure to find the keydata array within
 *
 * Returns a pointer to the start of the keydata array
 * of the node pointed to by the node header 
 */
static char *
befs_bt_keydata(struct befs_btree_node *node)
{
	return (char *) ((void *) node->od_node + sizeof (befs_btree_nodehead));
}

/**
 * befs_bt_get_key - returns a pointer to the start of a key
 * @sb: filesystem superblock
 * @node: node in which to look for the key
 * @index: the index of the key to get
 * @keylen: modified to be the length of the key at @index
 *
 * Returns a valid pointer into @node on success.
 * Returns NULL on failure (bad input) and sets *@keylen = 0
 */
static char *
befs_bt_get_key(struct super_block *sb, struct befs_btree_node *node,
		int index, u16 * keylen)
{
	int prev_key_end;
	char *keystart;
	fs16 *keylen_index;

	if (index < 0 || index > node->head.all_key_count) {
		*keylen = 0;
		return NULL;
	}

	keystart = befs_bt_keydata(node);
	keylen_index = befs_bt_keylen_index(node);

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
 * Returns <0 if @key2 is greater..
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

/* These will be used for non-string keyed btrees */
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
