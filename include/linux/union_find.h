/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_UNION_FIND_H
#define __LINUX_UNION_FIND_H
/**
 * union_find.h - union-find data structure implementation
 *
 * This header provides functions and structures to implement the union-find
 * data structure. The union-find data structure is used to manage disjoint
 * sets and supports efficient union and find operations.
 *
 * See Documentation/core-api/union_find.rst for documentation and samples.
 */

struct uf_node {
	struct uf_node *parent;
	unsigned int rank;
};

/* This macro is used for static initialization of a union-find node. */
#define UF_INIT_NODE(node)	{.parent = &node, .rank = 0}

/**
 * uf_node_init - Initialize a union-find node
 * @node: pointer to the union-find node to be initialized
 *
 * This function sets the parent of the node to itself and
 * initializes its rank to 0.
 */
static inline void uf_node_init(struct uf_node *node)
{
	node->parent = node;
	node->rank = 0;
}

/* find the root of a node */
struct uf_node *uf_find(struct uf_node *node);

/* Merge two intersecting nodes */
void uf_union(struct uf_node *node1, struct uf_node *node2);

#endif /* __LINUX_UNION_FIND_H */
