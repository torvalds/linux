/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_XBC_H
#define _LINUX_XBC_H
/*
 * Extra Boot Config
 * Copyright (C) 2019 Linaro Ltd.
 * Author: Masami Hiramatsu <mhiramat@kernel.org>
 */

#include <linux/kernel.h>
#include <linux/types.h>

#define BOOTCONFIG_MAGIC	"#BOOTCONFIG\n"
#define BOOTCONFIG_MAGIC_LEN	12
#define BOOTCONFIG_ALIGN_SHIFT	2
#define BOOTCONFIG_ALIGN	(1 << BOOTCONFIG_ALIGN_SHIFT)
#define BOOTCONFIG_ALIGN_MASK	(BOOTCONFIG_ALIGN - 1)

/* XBC tree node */
struct xbc_node {
	u16 next;
	u16 child;
	u16 parent;
	u16 data;
} __attribute__ ((__packed__));

#define XBC_KEY		0
#define XBC_VALUE	(1 << 15)
/* Maximum size of boot config is 32KB - 1 */
#define XBC_DATA_MAX	(XBC_VALUE - 1)

#define XBC_NODE_MAX	1024
#define XBC_KEYLEN_MAX	256
#define XBC_DEPTH_MAX	16

/* Node tree access raw APIs */
struct xbc_node * __init xbc_root_node(void);
int __init xbc_node_index(struct xbc_node *node);
struct xbc_node * __init xbc_node_get_parent(struct xbc_node *node);
struct xbc_node * __init xbc_node_get_child(struct xbc_node *node);
struct xbc_node * __init xbc_node_get_next(struct xbc_node *node);
const char * __init xbc_node_get_data(struct xbc_node *node);

/**
 * xbc_node_is_value() - Test the node is a value node
 * @node: An XBC node.
 *
 * Test the @node is a value node and return true if a value node, false if not.
 */
static inline __init bool xbc_node_is_value(struct xbc_node *node)
{
	return node->data & XBC_VALUE;
}

/**
 * xbc_node_is_key() - Test the node is a key node
 * @node: An XBC node.
 *
 * Test the @node is a key node and return true if a key node, false if not.
 */
static inline __init bool xbc_node_is_key(struct xbc_node *node)
{
	return !xbc_node_is_value(node);
}

/**
 * xbc_node_is_array() - Test the node is an arraied value node
 * @node: An XBC node.
 *
 * Test the @node is an arraied value node.
 */
static inline __init bool xbc_node_is_array(struct xbc_node *node)
{
	return xbc_node_is_value(node) && node->next != 0;
}

/**
 * xbc_node_is_leaf() - Test the node is a leaf key node
 * @node: An XBC node.
 *
 * Test the @node is a leaf key node which is a key node and has a value node
 * or no child. Returns true if it is a leaf node, or false if not.
 */
static inline __init bool xbc_node_is_leaf(struct xbc_node *node)
{
	return xbc_node_is_key(node) &&
		(!node->child || xbc_node_is_value(xbc_node_get_child(node)));
}

/* Tree-based key-value access APIs */
struct xbc_node * __init xbc_node_find_child(struct xbc_node *parent,
					     const char *key);

const char * __init xbc_node_find_value(struct xbc_node *parent,
					const char *key,
					struct xbc_node **vnode);

struct xbc_node * __init xbc_node_find_next_leaf(struct xbc_node *root,
						 struct xbc_node *leaf);

const char * __init xbc_node_find_next_key_value(struct xbc_node *root,
						 struct xbc_node **leaf);

/**
 * xbc_find_value() - Find a value which matches the key
 * @key: Search key
 * @vnode: A container pointer of XBC value node.
 *
 * Search a value whose key matches @key from whole of XBC tree and return
 * the value if found. Found value node is stored in *@vnode.
 * Note that this can return 0-length string and store NULL in *@vnode for
 * key-only (non-value) entry.
 */
static inline const char * __init
xbc_find_value(const char *key, struct xbc_node **vnode)
{
	return xbc_node_find_value(NULL, key, vnode);
}

/**
 * xbc_find_node() - Find a node which matches the key
 * @key: Search key
 *
 * Search a (key) node whose key matches @key from whole of XBC tree and
 * return the node if found. If not found, returns NULL.
 */
static inline struct xbc_node * __init xbc_find_node(const char *key)
{
	return xbc_node_find_child(NULL, key);
}

/**
 * xbc_array_for_each_value() - Iterate value nodes on an array
 * @anode: An XBC arraied value node
 * @value: A value
 *
 * Iterate array value nodes and values starts from @anode. This is expected to
 * be used with xbc_find_value() and xbc_node_find_value(), so that user can
 * process each array entry node.
 */
#define xbc_array_for_each_value(anode, value)				\
	for (value = xbc_node_get_data(anode); anode != NULL ;		\
	     anode = xbc_node_get_next(anode),				\
	     value = anode ? xbc_node_get_data(anode) : NULL)

/**
 * xbc_node_for_each_child() - Iterate child nodes
 * @parent: An XBC node.
 * @child: Iterated XBC node.
 *
 * Iterate child nodes of @parent. Each child nodes are stored to @child.
 */
#define xbc_node_for_each_child(parent, child)				\
	for (child = xbc_node_get_child(parent); child != NULL ;	\
	     child = xbc_node_get_next(child))

/**
 * xbc_node_for_each_array_value() - Iterate array entries of geven key
 * @node: An XBC node.
 * @key: A key string searched under @node
 * @anode: Iterated XBC node of array entry.
 * @value: Iterated value of array entry.
 *
 * Iterate array entries of given @key under @node. Each array entry node
 * is stroed to @anode and @value. If the @node doesn't have @key node,
 * it does nothing.
 * Note that even if the found key node has only one value (not array)
 * this executes block once. Hoever, if the found key node has no value
 * (key-only node), this does nothing. So don't use this for testing the
 * key-value pair existence.
 */
#define xbc_node_for_each_array_value(node, key, anode, value)		\
	for (value = xbc_node_find_value(node, key, &anode); value != NULL; \
	     anode = xbc_node_get_next(anode),				\
	     value = anode ? xbc_node_get_data(anode) : NULL)

/**
 * xbc_node_for_each_key_value() - Iterate key-value pairs under a node
 * @node: An XBC node.
 * @knode: Iterated key node
 * @value: Iterated value string
 *
 * Iterate key-value pairs under @node. Each key node and value string are
 * stored in @knode and @value respectively.
 */
#define xbc_node_for_each_key_value(node, knode, value)			\
	for (knode = NULL, value = xbc_node_find_next_key_value(node, &knode);\
	     knode != NULL; value = xbc_node_find_next_key_value(node, &knode))

/**
 * xbc_for_each_key_value() - Iterate key-value pairs
 * @knode: Iterated key node
 * @value: Iterated value string
 *
 * Iterate key-value pairs in whole XBC tree. Each key node and value string
 * are stored in @knode and @value respectively.
 */
#define xbc_for_each_key_value(knode, value)				\
	xbc_node_for_each_key_value(NULL, knode, value)

/* Compose partial key */
int __init xbc_node_compose_key_after(struct xbc_node *root,
			struct xbc_node *node, char *buf, size_t size);

/**
 * xbc_node_compose_key() - Compose full key string of the XBC node
 * @node: An XBC node.
 * @buf: A buffer to store the key.
 * @size: The size of the @buf.
 *
 * Compose the full-length key of the @node into @buf. Returns the total
 * length of the key stored in @buf. Or returns -EINVAL if @node is NULL,
 * and -ERANGE if the key depth is deeper than max depth.
 */
static inline int __init xbc_node_compose_key(struct xbc_node *node,
					      char *buf, size_t size)
{
	return xbc_node_compose_key_after(NULL, node, buf, size);
}

/* XBC node initializer */
int __init xbc_init(char *buf, const char **emsg, int *epos);


/* XBC cleanup data structures */
void __init xbc_destroy_all(void);

/* Debug dump functions */
void __init xbc_debug_dump(void);

#endif
