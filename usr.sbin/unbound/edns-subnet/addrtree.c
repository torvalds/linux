/*
 * edns-subnet/addrtree.c -- radix tree for edns subnet cache.
 *
 * Copyright (c) 2013, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/** \file 
 * addrtree -- radix tree for edns subnet cache.
 */

#include "config.h"
#include "util/log.h"
#include "util/data/msgreply.h"
#include "util/module.h"
#include "addrtree.h"

/** 
 * Create a new edge
 * @param node: Child node this edge will connect to.
 * @param addr: full key to this edge.
 * @param addrlen: length of relevant part of key for this node
 * @param parent_node: Parent node for node
 * @param parent_index: Index of child node at parent node
 * @return new addredge or NULL on failure
 */
static struct addredge * 
edge_create(struct addrnode *node, const addrkey_t *addr, 
	addrlen_t addrlen, struct addrnode *parent_node, int parent_index)
{
	size_t n;
	struct addredge *edge = (struct addredge *)malloc( sizeof (*edge) );
	if (!edge)
		return NULL;
	edge->node = node;
	edge->len = addrlen;
	edge->parent_index = parent_index;
	edge->parent_node = parent_node;
	/* ceil() */
	n = (size_t)((addrlen / KEYWIDTH) + ((addrlen % KEYWIDTH != 0)?1:0));
	edge->str = (addrkey_t *)calloc(n, sizeof (addrkey_t));
	if (!edge->str) {
		free(edge);
		return NULL;
	}
	memcpy(edge->str, addr, n * sizeof (addrkey_t));
	/* Only manipulate other objects after successful alloc */
	node->parent_edge = edge;
	log_assert(parent_node->edge[parent_index] == NULL);
	parent_node->edge[parent_index] = edge;
	return edge;
}

/** 
 * Create a new node
 * @param tree: Tree the node lives in.
 * @param elem: Element to store at this node
 * @param scope: Scopemask from server reply
 * @param ttl: Element is valid up to this time. Absolute, seconds
 * @return new addrnode or NULL on failure
 */
static struct addrnode * 
node_create(struct addrtree *tree, void *elem, addrlen_t scope, 
	time_t ttl)
{
	struct addrnode* node = (struct addrnode *)malloc( sizeof (*node) );
	if (!node)
		return NULL;
	node->elem = elem;
	tree->node_count++;
	node->scope = scope;
	node->ttl = ttl;
	node->only_match_scope_zero = 0;
	node->edge[0] = NULL;
	node->edge[1] = NULL;
	node->parent_edge = NULL;
	node->next = NULL;
	node->prev = NULL;
	return node;
}

/** Size in bytes of node and parent edge
 * @param tree: tree the node lives in
 * @param n: node which size must be calculated 
 * @return size in bytes.
 **/
static inline size_t 
node_size(const struct addrtree *tree, const struct addrnode *n)
{
	return sizeof *n + sizeof *n->parent_edge + n->parent_edge->len + 
		(n->elem?tree->sizefunc(n->elem):0);
}

struct addrtree * 
addrtree_create(addrlen_t max_depth, void (*delfunc)(void *, void *), 
	size_t (*sizefunc)(void *), void *env, uint32_t max_node_count)
{
	struct addrtree *tree;
	log_assert(delfunc != NULL);
	log_assert(sizefunc != NULL);
	tree = (struct addrtree *)calloc(1, sizeof(*tree));
	if (!tree)
		return NULL;
	tree->root = node_create(tree, NULL, 0, 0);
	if (!tree->root) {
		free(tree);
		return NULL;
	}
	tree->size_bytes = sizeof *tree + sizeof *tree->root;
	tree->first = NULL;
	tree->last = NULL;
	tree->max_depth = max_depth;
	tree->delfunc = delfunc;
	tree->sizefunc = sizefunc;
	tree->env = env;
	tree->node_count = 0;
	tree->max_node_count = max_node_count;
	return tree;
}

/** 
 * Scrub a node clean of elem
 * @param tree: tree the node lives in.
 * @param node: node to be cleaned.
 */
static void
clean_node(struct addrtree *tree, struct addrnode *node)
{
	if (!node->elem) return;
	tree->size_bytes -= tree->sizefunc(node->elem);
	tree->delfunc(tree->env, node->elem);
	node->only_match_scope_zero = 0;
	node->elem = NULL;
}

/** Remove specified node from LRU list */
static void
lru_pop(struct addrtree *tree, struct addrnode *node)
{
	if (node == tree->first) {
		if (!node->next) { /* it is the last as well */
			tree->first = NULL;
			tree->last = NULL;
		} else {
			tree->first = node->next;
			tree->first->prev = NULL;
		}
	} else if (node == tree->last) { /* but not the first */
		tree->last = node->prev;
		tree->last->next = NULL;
	} else {
		node->prev->next = node->next;
		node->next->prev = node->prev;
	}
}

/** Add node to LRU list as most recently used. */
static void
lru_push(struct addrtree *tree, struct addrnode *node)
{
	if (!tree->first) {
		tree->first = node;
		node->prev = NULL;
	} else {
		tree->last->next = node;
		node->prev = tree->last;
	}
	tree->last = node;
	node->next = NULL;
}

/** Move node to the end of LRU list */
static void
lru_update(struct addrtree *tree, struct addrnode *node)
{
	if (tree->root == node) return;
	lru_pop(tree, node);
	lru_push(tree, node);
}

/** 
 * Purge a node from the tree. Node and parentedge are cleaned and 
 * free'd.
 * @param tree: Tree the node lives in.
 * @param node: Node to be freed
 */
static void
purge_node(struct addrtree *tree, struct addrnode *node)
{
	struct addredge *parent_edge, *child_edge = NULL;
	int index;
	int keep = node->edge[0] && node->edge[1];
	
	clean_node(tree, node);
	parent_edge = node->parent_edge;
	if (keep || !parent_edge) return;
	tree->node_count--;
	index = parent_edge->parent_index;
	child_edge = node->edge[!node->edge[0]];
	if (child_edge) {
		child_edge->parent_node  = parent_edge->parent_node;
		child_edge->parent_index = index;
	}
	parent_edge->parent_node->edge[index] = child_edge;
	tree->size_bytes -= node_size(tree, node);
	free(parent_edge->str);
	free(parent_edge);
	lru_pop(tree, node);
	free(node);
}

/**
 * If a limit is set remove old nodes while above that limit.
 * @param tree: Tree to be cleaned up.
 */
static void
lru_cleanup(struct addrtree *tree)
{
	struct addrnode *n, *p;
	int children;
	if (tree->max_node_count == 0) return;
	while (tree->node_count > tree->max_node_count) {
		n = tree->first;
		if (!n) break;
		children = (n->edge[0] != NULL) + (n->edge[1] != NULL);
		/** Don't remove this node, it is either the root or we can't
		 * do without it because it has 2 children */
		if (children == 2 || !n->parent_edge) {
			lru_update(tree, n);
			continue;
		}
		p = n->parent_edge->parent_node;
		purge_node(tree, n);
		/** Since we removed n, n's parent p is eligible for deletion
		 * if it is not the root node, caries no data and has only 1
		 * child */
		children = (p->edge[0] != NULL) + (p->edge[1] != NULL);
		if (!p->elem && children == 1 && p->parent_edge) {
			purge_node(tree, p);
		}
	}
}

inline size_t
addrtree_size(const struct addrtree *tree)
{
	return tree?tree->size_bytes:0;
}

void addrtree_delete(struct addrtree *tree)
{
	struct addrnode *n;
	if (!tree) return;
	clean_node(tree, tree->root);
	free(tree->root);
	tree->size_bytes -= sizeof(struct addrnode);
	while ((n = tree->first)) {
		tree->first = n->next;
		clean_node(tree, n);
		tree->size_bytes -= node_size(tree, n);
		free(n->parent_edge->str);
		free(n->parent_edge);
		free(n);
	}
	log_assert(sizeof *tree == addrtree_size(tree));
	free(tree);
}

/**
 * Get N'th bit from address 
 * @param addr: address to inspect
 * @param addrlen: length of addr in bits
 * @param n: index of bit to test. Must be in range [0, addrlen)
 * @return 0 or 1
 */
static int 
getbit(const addrkey_t *addr, addrlen_t addrlen, addrlen_t n)
{
	log_assert(addrlen > n);
	(void)addrlen;
	return (int)(addr[n/KEYWIDTH]>>((KEYWIDTH-1)-(n%KEYWIDTH))) & 1;
}

/**
 * Test for equality on N'th bit.
 * @return 0 for equal, 1 otherwise 
 */
static inline int 
cmpbit(const addrkey_t *key1, const addrkey_t *key2, addrlen_t n)
{
	addrkey_t c = key1[n/KEYWIDTH] ^ key2[n/KEYWIDTH];
	return (int)(c >> ((KEYWIDTH-1)-(n%KEYWIDTH))) & 1;
}

/**
 * Common number of bits in prefix.
 * @param s1: first prefix.
 * @param l1: length of s1 in bits.
 * @param s2: second prefix.
 * @param l2: length of s2 in bits.
 * @param skip: nr of bits already checked.
 * @return common number of bits.
 */
static addrlen_t 
bits_common(const addrkey_t *s1, addrlen_t l1, 
	const addrkey_t *s2, addrlen_t l2, addrlen_t skip)
{
	addrlen_t len, i;
	len = (l1 > l2) ? l2 : l1;
	log_assert(skip < len);
	for (i = skip; i < len; i++) {
		if (cmpbit(s1, s2, i)) return i;
	}
	return len;
} 

/**
 * Tests if s1 is a substring of s2
 * @param s1: first prefix.
 * @param l1: length of s1 in bits.
 * @param s2: second prefix.
 * @param l2: length of s2 in bits.
 * @param skip: nr of bits already checked.
 * @return 1 for substring, 0 otherwise 
 */
static int 
issub(const addrkey_t *s1, addrlen_t l1, 
	const addrkey_t *s2, addrlen_t l2,  addrlen_t skip)
{
	return bits_common(s1, l1, s2, l2, skip) == l1;
}

void
addrtree_insert(struct addrtree *tree, const addrkey_t *addr, 
	addrlen_t sourcemask, addrlen_t scope, void *elem, time_t ttl, 
	time_t now, int only_match_scope_zero)
{
	struct addrnode *newnode, *node;
	struct addredge *edge;
	int index;
	addrlen_t common, depth;

	node = tree->root;
	log_assert(node != NULL);

	/* Protect our cache against too much fine-grained data */
	if (tree->max_depth < scope) scope = tree->max_depth;
	/* Server answer was less specific than question */
	if (scope < sourcemask) sourcemask = scope;

	depth = 0;
	while (1) {
		log_assert(depth <= sourcemask);
		/* Case 1: update existing node */
		if (depth == sourcemask) {
			/* update this node's scope and data */
			clean_node(tree, node);
			node->ttl = ttl;
			node->only_match_scope_zero = only_match_scope_zero;
			node->elem = elem;
			node->scope = scope;
			tree->size_bytes += tree->sizefunc(elem);
			return;
		}
		index = getbit(addr, sourcemask, depth);
		/* Get an edge to an unexpired node */
		edge = node->edge[index];
		while (edge) {
			/* Purge all expired nodes on path */
			if (!edge->node->elem || edge->node->ttl >= now)
				break;
			purge_node(tree, edge->node);
			edge = node->edge[index];
		}
		/* Case 2: New leafnode */
		if (!edge) {
			newnode = node_create(tree, elem, scope, ttl);
			if (!newnode) return;
			if (!edge_create(newnode, addr, sourcemask, node,
				index)) {
				clean_node(tree, newnode);
				tree->node_count--;
				free(newnode);
				return;
			}
			tree->size_bytes += node_size(tree, newnode);
			lru_push(tree, newnode);
			lru_cleanup(tree);
			return;
		}
		/* Case 3: Traverse edge */
		common = bits_common(edge->str, edge->len, addr, sourcemask,
			depth);
		if (common == edge->len) {
			/* We update the scope of intermediate nodes. Apparently
			 * the * authority changed its mind. If we would not do
			 * this we might not be able to reach our new node. */
			node->scope = scope;
			depth = edge->len;
			node = edge->node;
			continue;
		}
		/* Case 4: split. */
		if (!(newnode = node_create(tree, NULL, 0, 0)))
			return;
		node->edge[index] = NULL;
		if (!edge_create(newnode, addr, common, node, index)) {
			node->edge[index] = edge;
			clean_node(tree, newnode);
			tree->node_count--;
			free(newnode);
			return;
		}
		lru_push(tree, newnode);
		/* connect existing child to our new node */
		index = getbit(edge->str, edge->len, common);
		newnode->edge[index] = edge;
		edge->parent_node = newnode;
		edge->parent_index = (int)index;
		
		if (common == sourcemask) {
			/* Data is stored in the node */
			newnode->elem = elem;
			newnode->scope = scope;
			newnode->ttl = ttl;
			newnode->only_match_scope_zero = only_match_scope_zero;
		} 
		
		tree->size_bytes += node_size(tree, newnode);

		if (common != sourcemask) {
			/* Data is stored in other leafnode */
			node = newnode;
			newnode = node_create(tree, elem, scope, ttl);
			if (!edge_create(newnode, addr, sourcemask, node,
				index^1)) {
				clean_node(tree, newnode);
				tree->node_count--;
				free(newnode);
				return;
			}
			tree->size_bytes += node_size(tree, newnode);
			lru_push(tree, newnode);
		}
		lru_cleanup(tree);
		return;
	}
}

struct addrnode *
addrtree_find(struct addrtree *tree, const addrkey_t *addr, 
	addrlen_t sourcemask, time_t now)
{
	struct addrnode *node = tree->root;
	struct addredge *edge = NULL;
	addrlen_t depth = 0;

	log_assert(node != NULL);
	while (1) {
		/* Current node more specific then question. */
		log_assert(depth <= sourcemask);
		/* does this node have data? if yes, see if we have a match */
		if (node->elem && node->ttl >= now &&
			!(sourcemask != 0 && node->only_match_scope_zero)) {
			/* saved at wrong depth */;
			log_assert(node->scope >= depth);
			if (depth == node->scope ||
				(node->scope > sourcemask &&
				 depth == sourcemask)) {
				/* Authority indicates it does not have a more
				 * precise answer or we cannot ask a more
				 * specific question. */
				lru_update(tree, node);
				return node;
			}
		}
		/* This is our final depth, but we haven't found an answer. */
		if (depth == sourcemask)
			return NULL;
		/* Find an edge to traverse */
		edge = node->edge[getbit(addr, sourcemask, depth)];
		if (!edge || !edge->node)
			return NULL;
		if (edge->len > sourcemask )
			return NULL;
		if (!issub(edge->str, edge->len, addr, sourcemask, depth))
			return NULL;
		log_assert(depth < edge->len);
		depth = edge->len;
		node = edge->node;
	}
}

/** Wrappers for static functions to unit test */
int unittest_wrapper_addrtree_cmpbit(const addrkey_t *key1, 
	const addrkey_t *key2, addrlen_t n) {
	return cmpbit(key1, key2, n);
}
addrlen_t unittest_wrapper_addrtree_bits_common(const addrkey_t *s1, 
	addrlen_t l1, const addrkey_t *s2, addrlen_t l2, addrlen_t skip) {
	return bits_common(s1, l1, s2, l2, skip);
}
int unittest_wrapper_addrtree_getbit(const addrkey_t *addr, 
	addrlen_t addrlen, addrlen_t n) {
	return getbit(addr, addrlen, n);
}
int unittest_wrapper_addrtree_issub(const addrkey_t *s1, addrlen_t l1, 
	const addrkey_t *s2, addrlen_t l2,  addrlen_t skip) {
	return issub(s1, l1, s2, l2, skip);
}
