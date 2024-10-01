// SPDX-License-Identifier: GPL-2.0
#include <linux/union_find.h>

/**
 * uf_find - Find the root of a node and perform path compression
 * @node: the node to find the root of
 *
 * This function returns the root of the node by following the parent
 * pointers. It also performs path compression, making the tree shallower.
 *
 * Returns the root node of the set containing node.
 */
struct uf_node *uf_find(struct uf_node *node)
{
	struct uf_node *parent;

	while (node->parent != node) {
		parent = node->parent;
		node->parent = parent->parent;
		node = parent;
	}
	return node;
}

/**
 * uf_union - Merge two sets, using union by rank
 * @node1: the first node
 * @node2: the second node
 *
 * This function merges the sets containing node1 and node2, by comparing
 * the ranks to keep the tree balanced.
 */
void uf_union(struct uf_node *node1, struct uf_node *node2)
{
	struct uf_node *root1 = uf_find(node1);
	struct uf_node *root2 = uf_find(node2);

	if (root1 == root2)
		return;

	if (root1->rank < root2->rank) {
		root1->parent = root2;
	} else if (root1->rank > root2->rank) {
		root2->parent = root1;
	} else {
		root2->parent = root1;
		root1->rank++;
	}
}
