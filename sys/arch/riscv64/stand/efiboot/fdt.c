/*	$OpenBSD: fdt.c,v 1.4 2023/02/13 16:16:03 kettenis Exp $	*/

/*
 * Copyright (c) 2009 Dariusz Swiderski <sfires@sfires.net>
 * Copyright (c) 2009, 2016 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <lib/libkern/libkern.h>

#include "fdt.h"

unsigned int fdt_check_head(void *);
char	*fdt_get_str(uint32_t);
void	*skip_property(uint32_t *);
void	*skip_props(uint32_t *);
void	*skip_node_name(uint32_t *);
void	*skip_node(void *);
void	*skip_nops(uint32_t *);
void	*fdt_parent_node_recurse(void *, void *);

static int tree_inited = 0;
static struct fdt tree;

unsigned int
fdt_check_head(void *fdt)
{
	struct fdt_head *fh;
	uint32_t *ptr, *tok;

	fh = fdt;
	ptr = (uint32_t *)fdt;

	if (betoh32(fh->fh_magic) != FDT_MAGIC)
		return 0;

	if (betoh32(fh->fh_version) > FDT_CODE_VERSION)
		return 0;

	tok = skip_nops(ptr + (betoh32(fh->fh_struct_off) / 4));
	if (betoh32(*tok) != FDT_NODE_BEGIN)
		return 0;

	/* check for end signature on version 17 blob */
	if ((betoh32(fh->fh_version) >= 17) &&
	    (betoh32(*(ptr + (betoh32(fh->fh_struct_off) / 4) +
	    (betoh32(fh->fh_struct_size) / 4) - 1)) != FDT_END))
		return 0;

	return betoh32(fh->fh_version);
}

/*
 * Initializes internal structures of module.
 * Has to be called once.
 */
int
fdt_init(void *fdt)
{
	int version;

	memset(&tree, 0, sizeof(struct fdt));
	tree_inited = 0;

	if (!fdt)
		return 0;

	if (!(version = fdt_check_head(fdt)))
		return 0;

	tree.header = (struct fdt_head *)fdt;
	tree.tree = (char *)fdt + betoh32(tree.header->fh_struct_off);
	tree.strings = (char *)fdt + betoh32(tree.header->fh_strings_off);
	tree.memory = (char *)fdt + betoh32(tree.header->fh_reserve_off);
	tree.end = (char *)fdt + betoh32(tree.header->fh_size);
	tree.version = version;
	tree.strings_size = betoh32(tree.header->fh_strings_size);
	if (tree.version >= 17)
		tree.struct_size = betoh32(tree.header->fh_struct_size);
	tree_inited = 1;

	return version;
}

void
fdt_finalize(void)
{
	char *start = (char *)tree.header;

	tree.header->fh_size = htobe32(tree.end - start);
	tree.header->fh_struct_off = htobe32(tree.tree - start);
	tree.header->fh_strings_off = htobe32(tree.strings - start);
	tree.header->fh_reserve_off = htobe32(tree.memory - start);
	tree.header->fh_strings_size = htobe32(tree.strings_size);
	if (tree.version >= 17)
		tree.header->fh_struct_size = htobe32(tree.struct_size);
}

/*
 * Return the size of the FDT.
 */
size_t
fdt_get_size(void *fdt)
{
	if (!fdt)
		return 0;

	if (!fdt_check_head(fdt))
		return 0;

	return betoh32(((struct fdt_head *)fdt)->fh_size);
}

/*
 * Retrieve string pointer from strings table.
 */
char *
fdt_get_str(uint32_t num)
{
	if (num > tree.strings_size)
		return NULL;
	return (tree.strings) ? (tree.strings + num) : NULL;
}

int
fdt_add_str(char *name)
{
	size_t len = roundup(strlen(name) + 1, sizeof(uint32_t));
	char *end = tree.strings + tree.strings_size;

	if (end + len > tree.end)
		panic("FDT overflow");

	tree.strings_size += len;
	memset(end, 0, len);
	memcpy(end, name, strlen(name));

	return (end - tree.strings);
}

/*
 * Utility functions for skipping parts of tree.
 */

void *
skip_nops(uint32_t *ptr)
{
	while (betoh32(*ptr) == FDT_NOP)
		ptr++;

	return ptr;
}

void *
skip_property(uint32_t *ptr)
{
	uint32_t size;

	size = betoh32(*(ptr + 1));
	/* move forward by magic + size + nameid + rounded up property size */
	ptr += 3 + roundup(size, sizeof(uint32_t)) / sizeof(uint32_t);

	return skip_nops(ptr);
}

void *
skip_props(uint32_t *ptr)
{
	while (betoh32(*ptr) == FDT_PROPERTY) {
		ptr = skip_property(ptr);
	}
	return ptr;
}

void *
skip_node_name(uint32_t *ptr)
{
	/* skip name, aligned to 4 bytes, this is NULL term., so must add 1 */
	ptr += roundup(strlen((char *)ptr) + 1,
	    sizeof(uint32_t)) / sizeof(uint32_t);

	return skip_nops(ptr);
}

/*
 * Retrieves node property, the returned pointer is inside the fdt tree,
 * so we should not modify content pointed by it directly.
 */
int
fdt_node_property(void *node, char *name, char **out)
{
	uint32_t *ptr;
	uint32_t nameid;
	char *tmp;
	
	if (!tree_inited)
		return 0;

	ptr = (uint32_t *)node;

	if (betoh32(*ptr) != FDT_NODE_BEGIN)
		return 0;

	ptr = skip_node_name(ptr + 1);

	while (betoh32(*ptr) == FDT_PROPERTY) {
		nameid = betoh32(*(ptr + 2)); /* id of name in strings table */
		tmp = fdt_get_str(nameid);
		if (!strcmp(name, tmp)) {
			*out = (char *)(ptr + 3); /* beginning of the value */
			return betoh32(*(ptr + 1)); /* size of value */
		}
		ptr = skip_property(ptr);
	}
	return 0;
}

int
fdt_node_set_property(void *node, char *name, void *data, int len)
{
	char *end = tree.strings + tree.strings_size;
	uint32_t *ptr, *next;
	uint32_t nameid;
	uint32_t curlen;
	size_t delta;
	char *tmp;

	if (!tree_inited)
		return 0;

	ptr = (uint32_t *)node;

	if (betoh32(*ptr) != FDT_NODE_BEGIN)
		return 0;

	ptr = skip_node_name(ptr + 1);

	while (betoh32(*ptr) == FDT_PROPERTY) {
		nameid = betoh32(*(ptr + 2)); /* id of name in strings table */
		tmp = fdt_get_str(nameid);
		next = skip_property(ptr);
		if (!strcmp(name, tmp)) {
			curlen = betoh32(*(ptr + 1));
			delta = roundup(len, sizeof(uint32_t)) -
			    roundup(curlen, sizeof(uint32_t));
			if (end + delta > tree.end)
				panic("FDT overflow");

			memmove((char *)next + delta, next,
			    end - (char *)next);
			tree.struct_size += delta;
			tree.strings += delta;
			*(ptr + 1) = htobe32(len);
			memcpy(ptr + 3, data, len);
			return 1;
		}
		ptr = next;
	}
	return 0;
}

int
fdt_node_add_property(void *node, char *name, void *data, int len)
{
	char *end = tree.strings + tree.strings_size;
	char *dummy;

	if (!tree_inited)
		return 0;

	if (!fdt_node_property(node, name, &dummy)) {
		uint32_t *ptr = (uint32_t *)node;

		if (betoh32(*ptr) != FDT_NODE_BEGIN)
			return 0;

		if (end + 3 * sizeof(uint32_t) > tree.end)
			panic("FDT overflow");

		ptr = skip_node_name(ptr + 1);

		memmove(ptr + 3, ptr, end - (char *)ptr);
		tree.struct_size += 3 * sizeof(uint32_t);
		tree.strings += 3 * sizeof(uint32_t);
		*ptr++ = htobe32(FDT_PROPERTY);
		*ptr++ = htobe32(0);
		*ptr++ = htobe32(fdt_add_str(name));
	}

	return fdt_node_set_property(node, name, data, len);
}

int
fdt_node_add_node(void *node, char *name, void **child)
{
	size_t len = roundup(strlen(name) + 1, sizeof(uint32_t)) + 8;
	char *end = tree.strings + tree.strings_size;
	uint32_t *ptr = (uint32_t *)node;

	if (!tree_inited)
		return 0;

	if (betoh32(*ptr) != FDT_NODE_BEGIN)
		return 0;

	if (end + len > tree.end)
		panic("FDT overflow");

	ptr = skip_node_name(ptr + 1);
	ptr = skip_props(ptr);

	/* skip children */
	while (betoh32(*ptr) == FDT_NODE_BEGIN)
		ptr = skip_node(ptr);

	memmove((char *)ptr + len, ptr, end - (char *)ptr);
	tree.struct_size += len;
	tree.strings += len;

	*child = ptr;
	*ptr++ = htobe32(FDT_NODE_BEGIN);
	memset(ptr, 0, len - 8);
	memcpy(ptr, name, strlen(name));
	ptr += (len - 8) / sizeof(uint32_t);
	*ptr++ = htobe32(FDT_NODE_END);

	return 1;
}

/*
 * Retrieves next node, skipping all the children nodes of the pointed node,
 * returns pointer to next node, no matter if it exists or not.
 */
void *
skip_node(void *node)
{
	uint32_t *ptr = node;

	ptr++;

	ptr = skip_node_name(ptr);
	ptr = skip_props(ptr);

	/* skip children */
	while (betoh32(*ptr) == FDT_NODE_BEGIN)
		ptr = skip_node(ptr);

	return skip_nops(ptr + 1);
}

/*
 * Retrieves next node, skipping all the children nodes of the pointed node,
 * returns pointer to next node if exists, otherwise returns NULL.
 * If passed 0 will return first node of the tree (root).
 */
void *
fdt_next_node(void *node)
{
	uint32_t *ptr;

	if (!tree_inited)
		return NULL;

	ptr = node;

	if (node == NULL) {
		ptr = skip_nops((uint32_t *)tree.tree);
		return (betoh32(*ptr) == FDT_NODE_BEGIN) ? ptr : NULL;
	}

	if (betoh32(*ptr) != FDT_NODE_BEGIN)
		return NULL;

	ptr++;

	ptr = skip_node_name(ptr);
	ptr = skip_props(ptr);

	/* skip children */
	while (betoh32(*ptr) == FDT_NODE_BEGIN)
		ptr = skip_node(ptr);

	if (betoh32(*ptr) != FDT_NODE_END)
		return NULL;

	ptr = skip_nops(ptr + 1);

	if (betoh32(*ptr) != FDT_NODE_BEGIN)
		return NULL;

	return ptr;
}

/*
 * Retrieves node property as integers and puts them in the given
 * integer array.
 */
int
fdt_node_property_ints(void *node, char *name, int *out, int outlen)
{
	int *data;
	int i, inlen;

	inlen = fdt_node_property(node, name, (char **)&data) / sizeof(int);
	if (inlen <= 0)
		return -1;

	for (i = 0; i < inlen && i < outlen; i++)
		out[i] = betoh32(data[i]);

	return i;
}

/*
 * Retrieves node property as an integer.
 */
int
fdt_node_property_int(void *node, char *name, int *out)
{
	return fdt_node_property_ints(node, name, out, 1);
}

/*
 * Retrieves next node, skipping all the children nodes of the pointed node
 */
void *
fdt_child_node(void *node)
{
	uint32_t *ptr;

	if (!tree_inited)
		return NULL;

	ptr = node;

	if (betoh32(*ptr) != FDT_NODE_BEGIN)
		return NULL;

	ptr++;

	ptr = skip_node_name(ptr);
	ptr = skip_props(ptr);
	/* check if there is a child node */
	return (betoh32(*ptr) == FDT_NODE_BEGIN) ? (ptr) : NULL;
}

/*
 * Retrieves node name.
 */
char *
fdt_node_name(void *node)
{
	uint32_t *ptr;

	if (!tree_inited)
		return NULL;

	ptr = node;

	if (betoh32(*ptr) != FDT_NODE_BEGIN)
		return NULL;

	return (char *)(ptr + 1);
}

void *
fdt_find_node(char *name)
{
	void *node = fdt_next_node(0);
	const char *p = name;

	if (!tree_inited)
		return NULL;

	if (*p != '/')
		return NULL;

	while (*p) {
		void *child;
		const char *q;

		while (*p == '/')
			p++;
		if (*p == 0)
			return node;
		q = strchr(p, '/');
		if (q == NULL)
			q = p + strlen(p);

		for (child = fdt_child_node(node); child;
		     child = fdt_next_node(child)) {
			if (strncmp(p, fdt_node_name(child), q - p) == 0) {
				node = child;
				break;
			}
		}

		if (child == NULL)
			return NULL; /* No match found. */

		p = q;
	}

	return node;
}

void *
fdt_parent_node_recurse(void *pnode, void *child)
{
	void *node = fdt_child_node(pnode);
	void *tmp;

	while (node && (node != child)) {
		if ((tmp = fdt_parent_node_recurse(node, child)))
			return tmp;
		node = fdt_next_node(node);
	}
	return (node) ? pnode : NULL;
}

void *
fdt_parent_node(void *node)
{
	void *pnode = fdt_next_node(0);

	if (!tree_inited)
		return NULL;

	if (node == pnode)
		return NULL;

	return fdt_parent_node_recurse(pnode, node);
}

int
fdt_node_is_compatible(void *node, const char *name)
{
	char *data;
	int len;

	len = fdt_node_property(node, "compatible", &data);
	while (len > 0) {
		if (strcmp(data, name) == 0)
			return 1;
		len -= strlen(data) + 1;
		data += strlen(data) + 1;
	}

	return 0;
}

#ifdef DEBUG
/*
 * Debug methods for printing whole tree, particular nodes and properties
 */
void *
fdt_print_property(void *node, int level)
{
	uint32_t *ptr;
	char *tmp, *value;
	int cnt;
	uint32_t nameid, size;

	ptr = (uint32_t *)node;

	if (!tree_inited)
		return NULL;

	if (betoh32(*ptr) != FDT_PROPERTY)
		return ptr; /* should never happen */

	/* extract property name_id and size */
	size = betoh32(*++ptr);
	nameid = betoh32(*++ptr);

	for (cnt = 0; cnt < level; cnt++)
		printf("\t");

	tmp = fdt_get_str(nameid);
	printf("\t%s : ", tmp ? tmp : "NO_NAME");

	ptr++;
	value = (char *)ptr;

	if (!strcmp(tmp, "device_type") || !strcmp(tmp, "compatible") ||
	    !strcmp(tmp, "model") || !strcmp(tmp, "bootargs") ||
	    !strcmp(tmp, "linux,stdout-path")) {
		printf("%s", value);
	} else if (!strcmp(tmp, "clock-frequency") ||
	    !strcmp(tmp, "timebase-frequency")) {
		printf("%d", betoh32(*((unsigned int *)value)));
	} else {
		for (cnt = 0; cnt < size; cnt++) {
			if ((cnt % sizeof(uint32_t)) == 0)
				printf(" ");
			printf("%x%x", value[cnt] >> 4, value[cnt] & 0xf);
		}
	}
	ptr += roundup(size, sizeof(uint32_t)) / sizeof(uint32_t);
	printf("\n");

	return ptr;
}

void
fdt_print_node(void *node, int level)
{
	uint32_t *ptr;
	int cnt;
	
	ptr = (uint32_t *)node;

	if (betoh32(*ptr) != FDT_NODE_BEGIN)
		return;

	ptr++;

	for (cnt = 0; cnt < level; cnt++)
		printf("\t");
	printf("%s :\n", fdt_node_name(node));
	ptr = skip_node_name(ptr);

	while (betoh32(*ptr) == FDT_PROPERTY)
		ptr = fdt_print_property(ptr, level);
}

void
fdt_print_node_recurse(void *node, int level)
{
	void *child;

	fdt_print_node(node, level);
	for (child = fdt_child_node(node); child; child = fdt_next_node(child))
		fdt_print_node_recurse(child, level + 1);
}

void
fdt_print_tree(void)
{
	fdt_print_node_recurse(fdt_next_node(0), 0);
}
#endif
