/*
 * radtree -- generic radix tree for binary strings.
 *
 * Copyright (c) 2010, NLnet Labs.  See LICENSE for license.
 */
#include "config.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "radtree.h"
#include "util.h"
#include "region-allocator.h"

#include <stdio.h>
#include <ctype.h>

struct radtree* radix_tree_create(struct region* region)
{
	struct radtree* rt = (struct radtree*)region_alloc(region, sizeof(*rt));
	if(!rt) return NULL;
	rt->region = region;
	radix_tree_init(rt);
	return rt;
}

void radix_tree_init(struct radtree* rt)
{
	rt->root = NULL;
	rt->count = 0;
}

/** delete radnodes in postorder recursion */
static void radnode_del_postorder(struct region* region, struct radnode* n)
{
	unsigned i;
	if(!n) return;
	for(i=0; i<n->len; i++) {
		radnode_del_postorder(region, n->array[i].node);
		region_recycle(region, n->array[i].str, n->array[i].len);
	}
	region_recycle(region, n->array, n->capacity*sizeof(struct radsel));
	region_recycle(region, n, sizeof(*n));
}

void radix_tree_clear(struct radtree* rt)
{
	radnode_del_postorder(rt->region, rt->root);
	rt->root = NULL;
	rt->count = 0;
}

void radix_tree_delete(struct radtree* rt)
{
	if(!rt) return;
	radix_tree_clear(rt);
	region_recycle(rt->region, rt, sizeof(*rt));
}

/** return last elem-containing node in this subtree (excl self) */
static struct radnode*
radnode_last_in_subtree(struct radnode* n)
{
	int idx;
	/* try last entry in array first */
	for(idx=((int)n->len)-1; idx >= 0; idx--) {
		if(n->array[idx].node) {
			/* does it have entries in its subtrees? */
			if(n->array[idx].node->len > 0) {
				struct radnode* s = radnode_last_in_subtree(
					n->array[idx].node);
				if(s) return s;
			}
			/* no, does it have an entry itself? */
			if(n->array[idx].node->elem)
				return n->array[idx].node;
		}
	}
	return NULL;
}

/** last in subtree, incl self */
static struct radnode*
radnode_last_in_subtree_incl_self(struct radnode* n)
{
	struct radnode* s = radnode_last_in_subtree(n);
	if(s) return s;
	if(n->elem) return n;
	return NULL;
}

/** return first elem-containing node in this subtree (excl self) */
static struct radnode*
radnode_first_in_subtree(struct radnode* n)
{
	unsigned idx;
	struct radnode* s;
	/* try every subnode */
	for(idx=0; idx<n->len; idx++) {
		if(n->array[idx].node) {
			/* does it have elem itself? */
			if(n->array[idx].node->elem)
				return n->array[idx].node;
			/* try its subtrees */
			if((s=radnode_first_in_subtree(n->array[idx].node))!=0)
				return s;
		}
	}
	return NULL;
}

/** Find an entry in arrays from idx-1 to 0 */
static struct radnode*
radnode_find_prev_from_idx(struct radnode* n, unsigned from)
{
	unsigned idx = from;
	while(idx > 0) {
		idx --;
		if(n->array[idx].node) {
			struct radnode* s = radnode_last_in_subtree_incl_self(
				n->array[idx].node);
			if(s) return s;
		}
	}
	return NULL;
}

/** 
 * Find a prefix of the key, in whole-nodes.
 * Finds the longest prefix that corresponds to a whole radnode entry.
 * There may be a slightly longer prefix in one of the array elements.
 * @param result: the longest prefix, the entry itself if *respos==len,
 * 	otherwise an array entry, residx.
 * @param respos: pos in string where next unmatched byte is, if == len an
 * 	exact match has been found.  If == 0 then a "" match was found.
 * @return false if no prefix found, not even the root "" prefix.
 */
static int radix_find_prefix_node(struct radtree* rt, uint8_t* k,
	radstrlen_type len, struct radnode** result, radstrlen_type* respos)
{
	struct radnode* n = rt->root;
	radstrlen_type pos = 0;
	uint8_t byte;
	*respos = 0;
	*result = n;
	if(!n) return 0;
	while(n) {
		if(pos == len) {
			return 1;
		}
		byte = k[pos];
		if(byte < n->offset) {
			return 1;
		}
		byte -= n->offset;
		if(byte >= n->len) {
			return 1;
		}
		pos++;
		if(n->array[byte].len != 0) {
			/* must match additional string */
			if(pos+n->array[byte].len > len) {
				return 1;
			}
			if(memcmp(&k[pos], n->array[byte].str,
				n->array[byte].len) != 0) {
				return 1;
			}
			pos += n->array[byte].len;
		}
		n = n->array[byte].node;
		if(!n) return 1;
		*respos = pos;
		*result = n;
	}
	/* cannot reach because of returns when !n above */
	/* ENOTREACH */
	return 1;
}

/** grow array to at least the given size, offset unchanged */
static int
radnode_array_grow(struct region* region, struct radnode* n, unsigned want)
{
	unsigned ns = ((unsigned)n->capacity)*2;
	struct radsel* a;
	assert(want <= 256); /* cannot be more, range of uint8 */
	if(want > ns)
		ns = want;
	if(ns > 256) ns = 256;
	/* we do not use realloc, because we want to keep the old array
	 * in case alloc fails, so that the tree is still usable */
	a = (struct radsel*)region_alloc_array(region, ns, sizeof(struct radsel));
	if(!a) return 0;
	assert(n->len <= n->capacity);
	assert(n->capacity < ns);
	memcpy(&a[0], &n->array[0], n->len*sizeof(struct radsel));
	region_recycle(region, n->array, n->capacity*sizeof(struct radsel));
	n->array = a;
	n->capacity = ns;
	return 1;
}

/** make space in radnode array for another byte */
static int
radnode_array_space(struct region* region, struct radnode* n, uint8_t byte)
{
	/* is there an array? */
	if(!n->array || n->capacity == 0) {
		n->array = (struct radsel*)region_alloc(region,
			sizeof(struct radsel));
		if(!n->array) return 0;
		memset(&n->array[0], 0, sizeof(struct radsel));
		n->len = 1;
		n->capacity = 1;
		n->offset = byte;
	/* is the array unused? */
	} else if(n->len == 0 && n->capacity != 0) {
		n->len = 1;
		n->offset = byte;
		memset(&n->array[0], 0, sizeof(struct radsel));
	/* is it below the offset? */
	} else if(byte < n->offset) {
		/* is capacity enough? */
		unsigned idx;
		unsigned need = n->offset-byte;
		if(n->len+need > n->capacity) {
			/* grow array */
			if(!radnode_array_grow(region, n, n->len+need))
				return 0;
		}
		/* reshuffle items to end */
		memmove(&n->array[need], &n->array[0],
				n->len*sizeof(struct radsel));
		/* fixup pidx */
		for(idx = 0; idx < n->len; idx++) {
			if(n->array[idx+need].node)
				n->array[idx+need].node->pidx = idx+need;
		}
		/* zero the first */
		memset(&n->array[0], 0, need*sizeof(struct radsel));
		n->len += need;
		n->offset = byte;
	/* is it above the max? */
	} else if(byte-n->offset >= n->len) {
		/* is capacity enough? */
		unsigned need = (byte-n->offset) - n->len + 1;
		/* grow array */
		if(n->len + need > n->capacity) {
			if(!radnode_array_grow(region, n, n->len+need))
				return 0;
		}
		/* zero added entries */
		memset(&n->array[n->len], 0, need*sizeof(struct radsel));
		/* grow length */
		n->len += need;
	}
	return 1;
}

/** create a prefix in the array strs */
static int
radsel_str_create(struct region* region, struct radsel* r, uint8_t* k,
	radstrlen_type pos, radstrlen_type len)
{
	r->str = (uint8_t*)region_alloc(region, sizeof(uint8_t)*(len-pos));
	if(!r->str)
		return 0; /* out of memory */
	memmove(r->str, k+pos, len-pos);
	r->len = len-pos;
	return 1;
}

/** see if one byte string p is a prefix of another x (equality is true) */
static int
bstr_is_prefix(uint8_t* p, radstrlen_type plen, uint8_t* x,
	radstrlen_type xlen)
{
	/* if plen is zero, it is an (empty) prefix */
	if(plen == 0)
		return 1;
	/* if so, p must be shorter */
	if(plen > xlen)
		return 0;
	return (memcmp(p, x, plen) == 0);
}

/** number of bytes in common for the two strings */
static radstrlen_type
bstr_common(uint8_t* x, radstrlen_type xlen, uint8_t* y, radstrlen_type ylen)
{
	unsigned i, max = ((xlen<ylen)?xlen:ylen);
	for(i=0; i<max; i++) {
		if(x[i] != y[i])
			return i;
	}
	return max;
}


int
bstr_is_prefix_ext(uint8_t* p, radstrlen_type plen, uint8_t* x,
	radstrlen_type xlen)
{
	return bstr_is_prefix(p, plen, x, xlen);
}

radstrlen_type
bstr_common_ext(uint8_t* x, radstrlen_type xlen, uint8_t* y,
	radstrlen_type ylen)
{
	return bstr_common(x, xlen, y, ylen);
}

/** allocate remainder from prefixes for a split:
 * plen: len prefix, l: longer bstring, llen: length of l. */
static int
radsel_prefix_remainder(struct region* region, radstrlen_type plen,
	uint8_t* l, radstrlen_type llen,
	uint8_t** s, radstrlen_type* slen)
{
	*slen = llen - plen;
	*s = (uint8_t*)region_alloc(region, (*slen)*sizeof(uint8_t));
	if(!*s)
		return 0;
	memmove(*s, l+plen, llen-plen);
	return 1;
}

/** radsel create a split when two nodes have shared prefix.
 * @param r: radsel that gets changed, it contains a node.
 * @param k: key byte string
 * @param pos: position where the string enters the radsel (e.g. r.str)
 * @param len: length of k.
 * @param add: additional node for the string k.
 * 	removed by called on failure.
 * @return false on alloc failure, no changes made.
 */
static int
radsel_split(struct region* region, struct radsel* r, uint8_t* k,
	radstrlen_type pos, radstrlen_type len, struct radnode* add)
{
	uint8_t* addstr = k+pos;
	radstrlen_type addlen = len-pos;
	if(bstr_is_prefix(addstr, addlen, r->str, r->len)) {
		uint8_t* split_str=NULL, *dupstr=NULL;
		radstrlen_type split_len=0;
		/* 'add' is a prefix of r.node */
		/* also for empty addstr */
		/* set it up so that the 'add' node has r.node as child */
		/* so, r.node gets moved below the 'add' node, but we do
		 * this so that the r.node stays the same pointer for its
		 * key name */
		assert(addlen != r->len);
		assert(addlen < r->len);
		if(r->len-addlen > 1) {
			/* shift one because a char is in the lookup array */
			if(!radsel_prefix_remainder(region, addlen+1, r->str,
				r->len, &split_str, &split_len))
				return 0;
		}
		if(addlen != 0) {
			dupstr = (uint8_t*)region_alloc(region,
				addlen*sizeof(uint8_t));
			if(!dupstr) {
				region_recycle(region, split_str, split_len);
				return 0;
			}
			memcpy(dupstr, addstr, addlen);
		}
		if(!radnode_array_space(region, add, r->str[addlen])) {
			region_recycle(region, split_str, split_len);
			region_recycle(region, dupstr, addlen);
			return 0;
		}
		/* alloc succeeded, now link it in */
		add->parent = r->node->parent;
		add->pidx = r->node->pidx;
		add->array[0].node = r->node;
		add->array[0].str = split_str;
		add->array[0].len = split_len;
		r->node->parent = add;
		r->node->pidx = 0;

		r->node = add;
		region_recycle(region, r->str, r->len);
		r->str = dupstr;
		r->len = addlen;
	} else if(bstr_is_prefix(r->str, r->len, addstr, addlen)) {
		uint8_t* split_str = NULL;
		radstrlen_type split_len = 0;
		/* r.node is a prefix of 'add' */
		/* set it up so that the 'r.node' has 'add' as child */
		/* and basically, r.node is already completely fine,
		 * we only need to create a node as its child */
		assert(addlen != r->len);
		assert(r->len < addlen);
		if(addlen-r->len > 1) {
			/* shift one because a character goes into array */
			if(!radsel_prefix_remainder(region, r->len+1, addstr,
				addlen, &split_str, &split_len))
				return 0;
		}
		if(!radnode_array_space(region, r->node, addstr[r->len])) {
			region_recycle(region, split_str, split_len);
			return 0;
		}
		/* alloc succeeded, now link it in */
		add->parent = r->node;
		add->pidx = addstr[r->len] - r->node->offset;
		r->node->array[add->pidx].node = add;
		r->node->array[add->pidx].str = split_str;
		r->node->array[add->pidx].len = split_len;
	} else {
		/* okay we need to create a new node that chooses between 
		 * the nodes 'add' and r.node
		 * We do this so that r.node stays the same pointer for its
		 * key name. */
		struct radnode* com;
		uint8_t* common_str=NULL, *s1_str=NULL, *s2_str=NULL;
		radstrlen_type common_len, s1_len=0, s2_len=0;
		common_len = bstr_common(r->str, r->len, addstr, addlen);
		assert(common_len < r->len);
		assert(common_len < addlen);

		/* create the new node for choice */
		com = (struct radnode*)region_alloc_zero(region, sizeof(*com));
		if(!com) return 0; /* out of memory */

		/* create the two substrings for subchoices */
		if(r->len-common_len > 1) {
			/* shift by one char because it goes in lookup array */
			if(!radsel_prefix_remainder(region, common_len+1,
				r->str, r->len, &s1_str, &s1_len)) {
				region_recycle(region, com, sizeof(*com));
				return 0;
			}
		}
		if(addlen-common_len > 1) {
			if(!radsel_prefix_remainder(region, common_len+1,
				addstr, addlen, &s2_str, &s2_len)) {
				region_recycle(region, com, sizeof(*com));
				region_recycle(region, s1_str, s1_len);
				return 0;
			}
		}

		/* create the shared prefix to go in r */
		if(common_len > 0) {
			common_str = (uint8_t*)region_alloc(region,
				common_len*sizeof(uint8_t));
			if(!common_str) {
				region_recycle(region, com, sizeof(*com));
				region_recycle(region, s1_str, s1_len);
				region_recycle(region, s2_str, s2_len);
				return 0;
			}
			memcpy(common_str, addstr, common_len);
		}

		/* make space in the common node array */
		if(!radnode_array_space(region, com, r->str[common_len]) ||
			!radnode_array_space(region, com, addstr[common_len])) {
			region_recycle(region, com->array, com->capacity*sizeof(struct radsel));
			region_recycle(region, com, sizeof(*com));
			region_recycle(region, common_str, common_len);
			region_recycle(region, s1_str, s1_len);
			region_recycle(region, s2_str, s2_len);
			return 0;
		}

		/* allocs succeeded, proceed to link it all up */
		com->parent = r->node->parent;
		com->pidx = r->node->pidx;
		r->node->parent = com;
		r->node->pidx = r->str[common_len]-com->offset;
		add->parent = com;
		add->pidx = addstr[common_len]-com->offset;
		com->array[r->node->pidx].node = r->node;
		com->array[r->node->pidx].str = s1_str;
		com->array[r->node->pidx].len = s1_len;
		com->array[add->pidx].node = add;
		com->array[add->pidx].str = s2_str;
		com->array[add->pidx].len = s2_len;
		region_recycle(region, r->str, r->len);
		r->str = common_str;
		r->len = common_len;
		r->node = com;
	}
	return 1;
}

struct radnode* radix_insert(struct radtree* rt, uint8_t* k,
	radstrlen_type len, void* elem)
{
	struct radnode* n;
	radstrlen_type pos = 0;
	/* create new element to add */
	struct radnode* add = (struct radnode*)region_alloc_zero(rt->region,
		sizeof(*add));
	if(!add) return NULL; /* out of memory */
	add->elem = elem;

	/* find out where to add it */
	if(!radix_find_prefix_node(rt, k, len, &n, &pos)) {
		/* new root */
		assert(rt->root == NULL);
		if(len == 0) {
			rt->root = add;
		} else {
			/* add a root to point to new node */
			n = (struct radnode*)region_alloc_zero(rt->region,
				sizeof(*n));
			if(!n) {
				region_recycle(rt->region, add, sizeof(*add));
				return NULL;
			}
			if(!radnode_array_space(rt->region, n, k[0])) {
				region_recycle(rt->region, n->array,
					n->capacity*sizeof(struct radsel));
				region_recycle(rt->region, n, sizeof(*n));
				region_recycle(rt->region, add, sizeof(*add));
				return NULL;
			}
			add->parent = n;
			add->pidx = 0;
			n->array[0].node = add;
			if(len > 1) {
				if(!radsel_prefix_remainder(rt->region, 1, k, len,
					&n->array[0].str, &n->array[0].len)) {
					region_recycle(rt->region, n->array,
						n->capacity*sizeof(struct radsel));
					region_recycle(rt->region, n, sizeof(*n));
					region_recycle(rt->region, add, sizeof(*add));
					return NULL;
				}
			}
			rt->root = n;
		}
	} else if(pos == len) {
		/* found an exact match */
		if(n->elem) {
			/* already exists, failure */
			region_recycle(rt->region, add, sizeof(*add));
			return NULL;
		}
		n->elem = elem;
		region_recycle(rt->region, add, sizeof(*add));
		add = n;
	} else {
		/* n is a node which can accomodate */
		uint8_t byte;
		assert(pos < len);
		byte = k[pos];

		/* see if it falls outside of array */
		if(byte < n->offset || byte-n->offset >= n->len) {
			/* make space in the array for it; adjusts offset */
			if(!radnode_array_space(rt->region, n, byte)) {
				region_recycle(rt->region, add, sizeof(*add));
				return NULL;
			}
			assert(byte>=n->offset && byte-n->offset<n->len);
			byte -= n->offset;
			/* see if more prefix needs to be split off */
			if(pos+1 < len) {
				if(!radsel_str_create(rt->region, &n->array[byte],
					k, pos+1, len)) {
					region_recycle(rt->region, add, sizeof(*add));
					return NULL;
				}
			}
			/* insert the new node in the new bucket */
			add->parent = n;
			add->pidx = byte;
			n->array[byte].node = add;
		/* so a bucket exists and byte falls in it */
		} else if(n->array[byte-n->offset].node == NULL) {
			/* use existing bucket */
			byte -= n->offset;
			if(pos+1 < len) {
				/* split off more prefix */
				if(!radsel_str_create(rt->region, &n->array[byte],
					k, pos+1, len)) {
					region_recycle(rt->region, add, sizeof(*add));
					return NULL;
				}
			}
			/* insert the new node in the new bucket */
			add->parent = n;
			add->pidx = byte;
			n->array[byte].node = add;
		} else {
			/* use bucket but it has a shared prefix,
			 * split that out and create a new intermediate
			 * node to split out between the two.
			 * One of the two might exactmatch the new 
			 * intermediate node */
			if(!radsel_split(rt->region, &n->array[byte-n->offset],
				k, pos+1, len, add)) {
				region_recycle(rt->region, add, sizeof(*add));
				return NULL;
			}
		}
	}

	rt->count ++;
	return add;
}

/** Delete a radnode */
static void radnode_delete(struct region* region, struct radnode* n)
{
	unsigned i;
	if(!n) return;
	for(i=0; i<n->len; i++) {
		/* safe to free NULL str */
		region_recycle(region, n->array[i].str, n->array[i].len);
	}
	region_recycle(region, n->array, n->capacity*sizeof(struct radsel));
	region_recycle(region, n, sizeof(*n));
}

/** Cleanup node with one child, it is removed and joined into parent[x] str */
static int
radnode_cleanup_onechild(struct region* region, struct radnode* n,
	struct radnode* par)
{
	uint8_t* join;
	radstrlen_type joinlen;
	uint8_t pidx = n->pidx;
	struct radnode* child = n->array[0].node;
	/* node had one child, merge them into the parent. */
	/* keep the child node, so its pointers stay valid. */

	/* at parent, append child->str to array str */
	assert(pidx < par->len);
	joinlen = par->array[pidx].len + n->array[0].len + 1;
	join = (uint8_t*)region_alloc(region, joinlen*sizeof(uint8_t));
	if(!join) {
		/* cleanup failed due to out of memory */
		/* the tree is inefficient, with node n still existing */
		return 0;
	}
	/* we know that .str and join are malloced, thus aligned */
	if(par->array[pidx].str)
	    memcpy(join, par->array[pidx].str, par->array[pidx].len);
	/* the array lookup is gone, put its character in the lookup string*/
	join[par->array[pidx].len] = child->pidx + n->offset;
	/* but join+len may not be aligned */
	if(n->array[0].str)
	    memmove(join+par->array[pidx].len+1, n->array[0].str, n->array[0].len);
	region_recycle(region, par->array[pidx].str, par->array[pidx].len);
	par->array[pidx].str = join;
	par->array[pidx].len = joinlen;
	/* and set the node to our child. */
	par->array[pidx].node = child;
	child->parent = par;
	child->pidx = pidx;
	/* we are unlinked, delete our node */
	radnode_delete(region, n);
	return 1;
}

/** remove array of nodes */
static void
radnode_array_clean_all(struct region* region, struct radnode* n)
{
	n->offset = 0;
	n->len = 0;
	/* shrink capacity */
	region_recycle(region, n->array, n->capacity*sizeof(struct radsel));
	n->array = NULL;
	n->capacity = 0;
}

/** see if capacity can be reduced for the given node array */
static void
radnode_array_reduce_if_needed(struct region* region, struct radnode* n)
{
	if(n->len <= n->capacity/2 && n->len != n->capacity) {
		struct radsel* a = (struct radsel*)region_alloc_array(region,
			sizeof(*a), n->len);
		if(!a) return;
		memcpy(a, n->array, sizeof(*a)*n->len);
		region_recycle(region, n->array, n->capacity*sizeof(*a));
		n->array = a;
		n->capacity = n->len;
	}
}

/** remove NULL nodes from front of array */
static void
radnode_array_clean_front(struct region* region, struct radnode* n)
{
	/* move them up and adjust offset */
	unsigned idx, shuf = 0;
	/* remove until a nonNULL entry */
	while(shuf < n->len && n->array[shuf].node == NULL)
		shuf++;
	if(shuf == 0)
		return;
	if(shuf == n->len) {
		/* the array is empty, the tree is inefficient */
		radnode_array_clean_all(region, n);
		return;
	}
	assert(shuf < n->len);
	assert((int)shuf <= 255-(int)n->offset);
	memmove(&n->array[0], &n->array[shuf],
		(n->len - shuf)*sizeof(struct radsel));
	n->offset += shuf;
	n->len -= shuf;
	for(idx=0; idx<n->len; idx++)
		if(n->array[idx].node)
			n->array[idx].node->pidx = idx;
	/* see if capacity can be reduced */
	radnode_array_reduce_if_needed(region, n);
}

/** remove NULL nodes from end of array */
static void
radnode_array_clean_end(struct region* region, struct radnode* n)
{
	/* shorten it */
	unsigned shuf = 0;
	/* remove until a nonNULL entry */
	while(shuf < n->len && n->array[n->len-1-shuf].node == NULL)
		shuf++;
	if(shuf == 0)
		return;
	if(shuf == n->len) {
		/* the array is empty, the tree is inefficient */
		radnode_array_clean_all(region, n);
		return;
	}
	assert(shuf < n->len);
	n->len -= shuf;
	/* array elements can stay where they are */
	/* see if capacity can be reduced */
	radnode_array_reduce_if_needed(region, n);
}

/** clean up radnode leaf, where we know it has a parent */
static void
radnode_cleanup_leaf(struct region* region, struct radnode* n,
	struct radnode* par)
{
	uint8_t pidx;
	/* node was a leaf */
	/* delete leaf node, but store parent+idx */
	pidx = n->pidx;
	radnode_delete(region, n);

	/* set parent+idx entry to NULL str and node.*/
	assert(pidx < par->len);
	region_recycle(region, par->array[pidx].str, par->array[pidx].len);
	par->array[pidx].str = NULL;
	par->array[pidx].len = 0;
	par->array[pidx].node = NULL;

	/* see if par offset or len must be adjusted */
	if(par->len == 1) {
		/* removed final element from array */
		radnode_array_clean_all(region, par);
	} else if(pidx == 0) {
		/* removed first element from array */
		radnode_array_clean_front(region, par);
	} else if(pidx == par->len-1) {
		/* removed last element from array */
		radnode_array_clean_end(region, par);
	}
}

/** 
 * Cleanup a radix node that was made smaller, see if it can 
 * be merged with others.
 * @param rt: tree to remove root if needed.
 * @param n: node to cleanup
 * @return false on alloc failure.
 */
static int
radnode_cleanup(struct radtree* rt, struct radnode* n)
{
	while(n) {
		if(n->elem) {
			/* cannot delete node with a data element */
			return 1;
		} else if(n->len == 1 && n->parent) {
			return radnode_cleanup_onechild(rt->region, n, n->parent);
		} else if(n->len == 0) {
			struct radnode* par = n->parent;
			if(!par) {
				/* root deleted */
				radnode_delete(rt->region, n);
				rt->root = NULL;
				return 1;
			}
			/* remove and delete the leaf node */
			radnode_cleanup_leaf(rt->region, n, par);
			/* see if parent can now be cleaned up */
			n = par;
		} else {
			/* node cannot be cleaned up */
			return 1;
		}
	}
	/* ENOTREACH */
	return 1;
}

void radix_delete(struct radtree* rt, struct radnode* n)
{
	if(!n) return;
	n->elem = NULL;
	rt->count --;
	if(!radnode_cleanup(rt, n)) {
		/* out of memory in cleanup.  the elem ptr is NULL, but
		 * the radix tree could be inefficient. */
	}
}

struct radnode* radix_search(struct radtree* rt, uint8_t* k,
	radstrlen_type len)
{
	struct radnode* n = rt->root;
	radstrlen_type pos = 0;
	uint8_t byte;
	while(n) {
		if(pos == len)
			return n->elem?n:NULL;
		byte = k[pos];
		if(byte < n->offset)
			return NULL;
		byte -= n->offset;
		if(byte >= n->len)
			return NULL;
		pos++;
		if(n->array[byte].len != 0) {
			/* must match additional string */
			if(pos+n->array[byte].len > len)
				return NULL; /* no match */
			if(memcmp(&k[pos], n->array[byte].str,
				n->array[byte].len) != 0)
				return NULL; /* no match */
			pos += n->array[byte].len;
		}
		n = n->array[byte].node;
	}
	return NULL;
}

/** return self or a previous element */
static int ret_self_or_prev(struct radnode* n, struct radnode** result)
{
	if(n->elem)
		*result = n;
	else	*result = radix_prev(n);
	return 0;
}

int radix_find_less_equal(struct radtree* rt, uint8_t* k, radstrlen_type len,
        struct radnode** result)
{
	struct radnode* n = rt->root;
	radstrlen_type pos = 0;
	uint8_t byte;
	int r;
	if(!n) {
		/* empty tree */
		*result = NULL;
		return 0;
	}
	while(pos < len) {
		byte = k[pos];
		if(byte < n->offset) {
			/* so the previous is the element itself */
			/* or something before this element */
			return ret_self_or_prev(n, result);
		}
		byte -= n->offset;
		if(byte >= n->len) {
			/* so, the previous is the last of array, or itself */
			/* or something before this element */
			if((*result=radnode_last_in_subtree_incl_self(n))==0)
				*result = radix_prev(n);
			return 0;
		}
		pos++;
		if(!n->array[byte].node) {
			/* no match */
			/* Find an entry in arrays from byte-1 to 0 */
			*result = radnode_find_prev_from_idx(n, byte);
			if(*result)
				return 0;
			/* this entry or something before it */
			return ret_self_or_prev(n, result);
		}
		if(n->array[byte].len != 0) {
			/* must match additional string */
			if(pos+n->array[byte].len > len) {
				/* the additional string is longer than key*/
				if( (memcmp(&k[pos], n->array[byte].str,
					len-pos)) <= 0) {
				  /* and the key is before this node */
				  *result = radix_prev(n->array[byte].node);
				} else {
					/* the key is after the additional
					 * string, thus everything in that
					 * subtree is smaller. */
				  	*result=radnode_last_in_subtree_incl_self(n->array[byte].node);
					/* if somehow that is NULL,
					 * then we have an inefficient tree:
					 * byte+1 is larger than us, so find
					 * something in byte-1 and before */
					if(!*result)
						*result = radix_prev(n->array[byte].node);
				}
				return 0; /* no match */
			}
			if( (r=memcmp(&k[pos], n->array[byte].str,
				n->array[byte].len)) < 0) {
				*result = radix_prev(n->array[byte].node);
				return 0; /* no match */
			} else if(r > 0) {
				/* the key is larger than the additional
				 * string, thus everything in that subtree
				 * is smaller */
				*result=radnode_last_in_subtree_incl_self(n->array[byte].node);
				/* if we have an inefficient tree */
				if(!*result) *result = radix_prev(n->array[byte].node);
				return 0; /* no match */
			}
			pos += n->array[byte].len;
		}
		n = n->array[byte].node;
	}
	if(n->elem) {
		/* exact match */
		*result = n;
		return 1;
	}
	/* there is a node which is an exact match, but it has no element */
	*result = radix_prev(n);
	return 0;
}


struct radnode* radix_first(struct radtree* rt)
{
	struct radnode* n;
	if(!rt || !rt->root) return NULL;
	n = rt->root;
	if(n->elem) return n;
	return radix_next(n);
}

struct radnode* radix_last(struct radtree* rt)
{
	if(!rt || !rt->root) return NULL;
	return radnode_last_in_subtree_incl_self(rt->root);
}

struct radnode* radix_next(struct radnode* n)
{
	if(!n) return NULL;
	if(n->len) {
		/* go down */
		struct radnode* s = radnode_first_in_subtree(n);
		if(s) return s;
	}
	/* go up - the parent->elem is not useful, because it is before us */
	while(n->parent) {
		unsigned idx = n->pidx;
		n = n->parent;
		idx++;
		for(; idx < n->len; idx++) {
			/* go down the next branch */
			if(n->array[idx].node) {
				struct radnode* s;
				/* node itself */
				if(n->array[idx].node->elem)
					return n->array[idx].node;
				/* or subtree */
				s = radnode_first_in_subtree(
					n->array[idx].node);
				if(s) return s;
			}
		}
	}
	return NULL;
}

struct radnode* radix_prev(struct radnode* n)
{
	if(!n) return NULL;
	/* must go up, since all array nodes are after this node */
	while(n->parent) {
		uint8_t idx = n->pidx;
		struct radnode* s;
		n = n->parent;
		assert(n->len > 0); /* since we are a child */
		/* see if there are elements in previous branches there */
		s = radnode_find_prev_from_idx(n, idx);
		if(s) return s;
		/* the current node is before the array */
		if(n->elem)
			return n;
	}
	return NULL;
}

/** convert one character from domain-name to radname */
static uint8_t char_d2r(uint8_t c)
{
	if(c < 'A') return c+1; /* make space for 00 */
	else if(c <= 'Z') return c-'A'+'a'; /* lowercase */
	else return c;
}

/** convert one character from radname to domain-name (still lowercased) */
static uint8_t char_r2d(uint8_t c)
{
	assert(c != 0); /* end of label */
	if(c <= 'A') return c-1;
	else return c;
}

/** copy and convert a range of characters */
static void cpy_d2r(uint8_t* to, const uint8_t* from, int len)
{
	int i;
	for(i=0; i<len; i++)
		to[i] = char_d2r(from[i]);
}

/** copy and convert a range of characters */
static void cpy_r2d(uint8_t* to, uint8_t* from, uint8_t len)
{
	uint8_t i;
	for(i=0; i<len; i++)
		to[i] = char_r2d(from[i]);
}

/* radname code: domain to radix-bstring */
void radname_d2r(uint8_t* k, radstrlen_type* len, const uint8_t* dname,
	size_t dlen)
{
	/* the domain name is converted as follows,
	 * to preserve the normal (NSEC) ordering of domain names.
	 * lowercased, and 'end-of-label' is a '00' byte,
	 * bytes 00-'A' are +1 moved to make space for 00 byte.
	 * final root label is not appended (string ends).
	 * because the only allowed empty label is the final root label,
	 * we can also remove the last 00 label-end.
	 * The total result length is one-or-two less than the dname.
	 * 
	 * examples (numbers are bytes, letters are ascii):
	 * - root: dname: 0, radname: ''
	 * - nl.:  dname: 3nl0, radname: 'nl'
	 * - labs.nl: dname 4labs3nl0, radname: 'nl0labs'
	 * - x.labs.nl: dname 1x4labs3nl0, radname: 'nl0labs0x'
	 */

	/* conversion by putting the label starts on a stack */
	const uint8_t* labstart[130];
	unsigned int lab = 0, kpos, dpos = 0;
	/* sufficient space */
	assert(k && dname);
	assert(dlen <= 256); /* and therefore not more than 128 labels */
	assert(*len >= dlen);
	assert(dlen > 0); /* even root label has dlen=1 */

	/* root */
	if(dlen == 1) {
		assert(dname[0] == 0);
		*len = 0;
		return;
	}
	
	/* walk through domain name and remember label positions */
	do {
		/* compression pointers not allowed */
		if((dname[dpos] & 0xc0)) {
			*len = 0;
			return; /* format error */
		}
		labstart[lab++] = &dname[dpos];
		if(dpos + dname[dpos] + 1 >= dlen) {
			*len = 0;
			return; /* format error */
		}
		/* skip the label contents */
		dpos += dname[dpos];
		dpos ++;
	} while(dname[dpos] != 0);
	/* exit condition makes root label not in labelstart stack */
	/* because the root was handled before, we know there is some text */
	assert(lab > 0);
	lab-=1;
	kpos = *labstart[lab];
	cpy_d2r(k, labstart[lab]+1, kpos);
	/* if there are more labels, copy them over */
	while(lab) {
		/* put 'end-of-label' 00 to end previous label */
		k[kpos++]=0;
		/* append the label */
		lab--;
		cpy_d2r(k+kpos, labstart[lab]+1, *labstart[lab]);
		kpos += *labstart[lab];
	}
	/* done */
	assert(kpos == dlen-2); /* no rootlabel, one less label-marker */
	*len = kpos;
}

/* radname code: radix-bstring to domain */
void radname_r2d(uint8_t* k, radstrlen_type len, uint8_t* dname, size_t* dlen)
{
	/* find labels and push on stack */
	uint8_t* labstart[130];
	uint8_t lablen[130];
	unsigned int lab = 0, dpos, kpos = 0;
	/* sufficient space */
	assert(k && dname);
	assert((size_t)*dlen >= (size_t)len+2);
	assert(len <= 256);
	/* root label */
	if(len == 0) {
		assert(*dlen > 0);
		dname[0]=0;
		*dlen=1;
		return;
	}
	/* find labels */
	while(kpos < len) {
		lablen[lab]=0;
			labstart[lab]=&k[kpos];
		/* skip to next label */
		while(kpos < len && k[kpos] != 0) {
			lablen[lab]++;
			kpos++;
		}
		lab++;
		/* skip 00 byte for label-end */
		if(kpos < len) {
			assert(k[kpos] == 0);
			kpos++;
		}
	}
	/* copy the labels over to the domain name */
	dpos = 0;
	while(lab) {
		lab--;
		/* label length */
		dname[dpos++] = lablen[lab];
		/* label content */
		cpy_r2d(dname+dpos, labstart[lab], lablen[lab]);
		dpos += lablen[lab];
	}
	/* append root label */
	dname[dpos++] = 0;
	/* assert the domain name is wellformed */
	assert((int)dpos == (int)len+2);
	assert(dname[dpos-1] == 0); /* ends with root label */
	*dlen = dpos;
}

/** insert by domain name */
struct radnode*
radname_insert(struct radtree* rt, const uint8_t* d, size_t max, void* elem)
{
	/* convert and insert */
	uint8_t radname[300];
	radstrlen_type len = (radstrlen_type)sizeof(radname);
	if(max > sizeof(radname))
		return NULL; /* too long */
	radname_d2r(radname, &len, d, max);
	return radix_insert(rt, radname, len, elem);
}

/** delete by domain name */
void
radname_delete(struct radtree* rt, const uint8_t* d, size_t max)
{
	/* search and remove */
	struct radnode* n = radname_search(rt, d, max);
	if(n) radix_delete(rt, n);
}

/* search for exact match of domain name, converted to radname in tree */
struct radnode* radname_search(struct radtree* rt, const uint8_t* d,
	size_t max)
{
	/* stack of labels in the domain name */
	const uint8_t* labstart[130];
	unsigned int lab, dpos, lpos;
	struct radnode* n = rt->root;
	uint8_t byte;
	radstrlen_type i;
	uint8_t b;

	/* search for root? it is '' */
	if(max < 1)
		return NULL;
	if(d[0] == 0) {
		if(!n) return NULL;
		return n->elem?n:NULL;
	}

	/* find labels stack in domain name */
	lab = 0;
	dpos = 0;
	/* must have one label, since root is specialcased */
	do {
		if((d[dpos] & 0xc0))
			return NULL; /* compression ptrs not allowed error */
		labstart[lab++] = &d[dpos];
		if(dpos + d[dpos] + 1 >= max)
			return NULL; /* format error: outside of bounds */
		/* skip the label contents */
		dpos += d[dpos];
		dpos ++;
	} while(d[dpos] != 0);
	/* exit condition makes that root label is not in the labstarts */
	/* now: dpos+1 is length of domain name. lab is number of labels-1 */

	/* start processing at the last label */
	lab-=1;
	lpos = 0;
	while(n) {
		/* fetch next byte this label */
		if(lpos < *labstart[lab])
			/* lpos+1 to skip labelstart, lpos++ to move forward */
			byte = char_d2r(labstart[lab][++lpos]);
		else {
			if(lab == 0) /* last label - we're done */
				return n->elem?n:NULL;
			/* next label, search for byte 00 */
			lpos = 0;
			lab--;
			byte = 0;
		}
		/* find that byte in the array */
		if(byte < n->offset)
			return NULL;
		byte -= n->offset;
		if(byte >= n->len)
			return NULL;
		if(n->array[byte].len != 0) {
			/* must match additional string */
			/* see how many bytes we need and start matching them*/
			for(i=0; i<n->array[byte].len; i++) {
				/* next byte to match */
				if(lpos < *labstart[lab])
					b = char_d2r(labstart[lab][++lpos]);
				else {
					/* if last label, no match since
					 * we are in the additional string */
					if(lab == 0)
						return NULL; 
					/* next label, search for byte 00 */
					lpos = 0;
					lab--;
					b = 0;
				}
				if(n->array[byte].str[i] != b)
					return NULL; /* not matched */
			}
		}
		n = n->array[byte].node;
	}
	return NULL;
}

/* find domain name or smaller or equal domain name in radix tree */
int radname_find_less_equal(struct radtree* rt, const uint8_t* d, size_t max,
        struct radnode** result)
{
	/* stack of labels in the domain name */
	const uint8_t* labstart[130];
	unsigned int lab, dpos, lpos;
	struct radnode* n = rt->root;
	uint8_t byte;
	radstrlen_type i;
	uint8_t b;

	/* empty tree */
	if(!n) {
		*result = NULL;
		return 0;
	}

	/* search for root? it is '' */
	if(max < 1) {
		*result = NULL;
		return 0; /* parse error, out of bounds */
	}
	if(d[0] == 0) {
		if(n->elem) {
			*result = n;
			return 1;
		}
		/* no smaller element than the root */
		*result = NULL;
		return 0;
	}

	/* find labels stack in domain name */
	lab = 0;
	dpos = 0;
	/* must have one label, since root is specialcased */
	do {
		if((d[dpos] & 0xc0)) {
			*result = NULL;
			return 0; /* compression ptrs not allowed error */
		}
		labstart[lab++] = &d[dpos];
		if(dpos + d[dpos] + 1 >= max) {
			*result = NULL; /* format error: outside of bounds */
			return 0;
		}
		/* skip the label contents */
		dpos += d[dpos];
		dpos ++;
	} while(d[dpos] != 0);
	/* exit condition makes that root label is not in the labstarts */
	/* now: dpos+1 is length of domain name. lab is number of labels-1 */

	/* start processing at the last label */
	lab-=1;
	lpos = 0;
	while(1) {
		/* fetch next byte this label */
		if(lpos < *labstart[lab])
			/* lpos+1 to skip labelstart, lpos++ to move forward */
			byte = char_d2r(labstart[lab][++lpos]);
		else {
			if(lab == 0) {
				/* last label - we're done */
				/* exact match */
				if(n->elem) {
					*result = n;
					return 1;
				}
				/* there is a node which is an exact match,
				 * but there no element in it */
				*result = radix_prev(n);
				return 0;
			}
			/* next label, search for byte 0 the label separator */
			lpos = 0;
			lab--;
			byte = 0;
		}
		/* find that byte in the array */
		if(byte < n->offset)
			/* so the previous is the element itself */
			/* or something before this element */
			return ret_self_or_prev(n, result);
		byte -= n->offset;
		if(byte >= n->len) {
			/* so, the previous is the last of array, or itself */
			/* or something before this element */
			*result = radnode_last_in_subtree_incl_self(n);
			if(!*result)
				*result = radix_prev(n);
			return 0;
		}
		if(!n->array[byte].node) {
			/* no match */
			/* Find an entry in arrays from byte-1 to 0 */
			*result = radnode_find_prev_from_idx(n, byte);
			if(*result)
				return 0;
			/* this entry or something before it */
			return ret_self_or_prev(n, result);
		}
		if(n->array[byte].len != 0) {
			/* must match additional string */
			/* see how many bytes we need and start matching them*/
			for(i=0; i<n->array[byte].len; i++) {
				/* next byte to match */
				if(lpos < *labstart[lab])
					b = char_d2r(labstart[lab][++lpos]);
				else {
					/* if last label, no match since
					 * we are in the additional string */
					if(lab == 0) {
						/* dname ended, thus before
						 * this array element */
						*result =radix_prev(
							n->array[byte].node);
						return 0; 
					}
					/* next label, search for byte 00 */
					lpos = 0;
					lab--;
					b = 0;
				}
				if(b < n->array[byte].str[i]) {
					*result =radix_prev(
						n->array[byte].node);
					return 0; 
				} else if(b > n->array[byte].str[i]) {
					/* the key is after the additional,
					 * so everything in its subtree is
					 * smaller */
					*result = radnode_last_in_subtree_incl_self(n->array[byte].node);
					/* if that is NULL, we have an
					 * inefficient tree, find in byte-1*/
					if(!*result)
						*result = radix_prev(n->array[byte].node);
					return 0;
				}
			}
		}
		n = n->array[byte].node;
	}
	/* ENOTREACH */
	return 0;
}

