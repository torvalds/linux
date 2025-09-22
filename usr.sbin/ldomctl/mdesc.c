/*	$OpenBSD: mdesc.c,v 1.13 2019/11/28 18:40:42 kn Exp $	*/

/*
 * Copyright (c) 2012 Mark Kettenis
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

#include <sys/types.h>
#include <sys/queue.h>
#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mdesc.h"
#include "ldom_util.h"

struct md_name *
md_find_name(struct md *md, const char *str)
{
	struct md_name *name;

	TAILQ_FOREACH(name, &md->name_list, link)
		if (strcmp(name->str, str) == 0)
			return name;
	return NULL;
}

struct md_name *
md_add_name(struct md *md, const char *str)
{
	struct md_name *name;

	name = md_find_name(md, str);
	if (name == NULL) {
		name = xmalloc(sizeof(*name));
		name->str = xstrdup(str);
		TAILQ_INSERT_TAIL(&md->name_list, name, link);
		name->refcnt = 0;
	}
	name->refcnt++;
	return name;
}

void
md_free_name(struct md *md, struct md_name *name)
{
	if (name->refcnt > 1) {
		name->refcnt--;
		return;
	}

	TAILQ_REMOVE(&md->name_list, name, link);
	free(name);
}

struct md_data *
md_find_data(struct md *md, const uint8_t *b, size_t len)
{
	struct md_data *data;

	TAILQ_FOREACH(data, &md->data_list, link)
		if (data->len == len &&
		    memcmp(data->data, b, len) == 0)
			return data;

	return NULL;
}

struct md_data *
md_add_data(struct md *md, const uint8_t *b, size_t len)
{
	struct md_data *data;

	data = md_find_data(md, b, len);
	if (data == NULL) {
		data = xmalloc(sizeof(*data));
		data->data = xmalloc(len);
		memcpy(data->data, b, len);
		data->len = len;
		TAILQ_INSERT_TAIL(&md->data_list, data, link);
		data->refcnt = 0;
	}
	data->refcnt++;
	return data;
}

void
md_free_data(struct md *md, struct md_data *data)
{
	if (data->refcnt > 1) {
		data->refcnt--;
		return;
	}

	TAILQ_REMOVE(&md->data_list, data, link);
	free(data);
}

struct md_node *
md_find_node(struct md *md, const char *name)
{
	struct md_node *node;

	TAILQ_FOREACH(node, &md->node_list, link) {
		if (strcmp(node->name->str, name) == 0)
			return node;
	}

	return NULL;
}

struct md_node *
md_find_subnode(struct md *md, struct md_node *node, const char *name)
{
	struct md_prop *prop;

	TAILQ_FOREACH(prop, &node->prop_list, link) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0 &&
		    strcmp(prop->d.arc.node->name->str, name) == 0)
			return prop->d.arc.node;
	}

	return NULL;
}

struct md_node *
md_add_node(struct md *md, const char *name)
{
	struct md_node *node;

	node = xmalloc(sizeof(*node));
	node->name = md_add_name(md, name);
	TAILQ_INIT(&node->prop_list);
	TAILQ_INSERT_TAIL(&md->node_list, node, link);

	return node;
}

void
md_link_node(struct md *md, struct md_node *node1, struct md_node *node2)
{
	md_add_prop_arc(md, node1, "fwd", node2);
	md_add_prop_arc(md, node2, "back", node1);
}

struct md_prop *
md_find_prop(struct md *md, struct md_node *node, const char *name)
{
	struct md_prop *prop;

	TAILQ_FOREACH(prop, &node->prop_list, link) {
		if (strcmp(prop->name->str, name) == 0)
			return prop;
	}

	return NULL;
}

struct md_prop *
md_add_prop(struct md *md, struct md_node *node, const char *name)
{
	struct md_prop *prop;

	prop = xmalloc(sizeof(*prop));
	prop->name = md_add_name(md, name);
	TAILQ_INSERT_TAIL(&node->prop_list, prop, link);

	return prop;
}

struct md_prop *
md_add_prop_val(struct md *md, struct md_node *node, const char *name,
     uint64_t val)
{
	struct md_prop *prop;

	prop = md_add_prop(md, node, name);
	prop->tag = MD_PROP_VAL;
	prop->d.val = val;

	return prop;
}

struct md_prop *
md_add_prop_str(struct md *md, struct md_node *node, const char *name,
     const char *str)
{
	struct md_prop *prop;

	prop = md_add_prop(md, node, name);
	prop->tag = MD_PROP_STR;
	prop->d.data = md_add_data(md, str, strlen(str) + 1);

	return prop;
}

struct md_prop *
md_add_prop_data(struct md *md, struct md_node *node, const char *name,
     const uint8_t *data, size_t len)
{
	struct md_prop *prop;

	prop = md_add_prop(md, node, name);
	prop->tag = MD_PROP_DATA;
	prop->d.data = md_add_data(md, data, len);

	return prop;
}

struct md_prop *
md_add_prop_arc(struct md *md, struct md_node *node, const char *name,
    struct md_node *target_node)
{
	struct md_prop *prop;

	prop = md_add_prop(md, node, name);
	prop->tag = MD_PROP_ARC;
	prop->d.arc.node = target_node;

	return prop;
}

void
md_delete_prop(struct md *md, struct md_node *node, struct md_prop *prop)
{
	TAILQ_REMOVE(&node->prop_list, prop, link);
	if (prop->tag == MD_PROP_STR || prop->tag == MD_PROP_DATA)
		md_free_data(md, prop->d.data);
	md_free_name(md, prop->name);
	free(prop);
}

bool
md_get_prop_val(struct md *md, struct md_node *node, const char *name,
    uint64_t *val)
{
	struct md_prop *prop;

	prop = md_find_prop(md, node, name);
	if (prop == NULL || prop->tag != MD_PROP_VAL)
		return false;

	*val = prop->d.val;
	return true;
}

bool
md_set_prop_val(struct md *md, struct md_node *node, const char *name,
    uint64_t val)
{
	struct md_prop *prop;

	prop = md_find_prop(md, node, name);
	if (prop == NULL || prop->tag != MD_PROP_VAL)
		return false;

	prop->d.val = val;
	return true;
}

bool
md_get_prop_str(struct md *md, struct md_node *node, const char *name,
    const char **str)
{
	struct md_prop *prop;

	prop = md_find_prop(md, node, name);
	if (prop == NULL || prop->tag != MD_PROP_STR)
		return false;

	*str = prop->d.data->data;
	return true;
}

bool
md_get_prop_data(struct md *md, struct md_node *node, const char *name,
    const void **data, size_t *len)
{
	struct md_prop *prop;

	prop = md_find_prop(md, node, name);
	if (prop == NULL || prop->tag != MD_PROP_DATA)
		return false;

	*data = prop->d.data->data;
	*len = prop->d.data->len;
	return true;
}

bool
md_set_prop_data(struct md *md, struct md_node *node, const char *name,
		 const uint8_t *data, size_t len)
{
	struct md_prop *prop;

	prop = md_find_prop(md, node, name);
	if (prop == NULL || prop->tag != MD_PROP_DATA)
		return false;

	md_free_data(md, prop->d.data);
	prop->d.data = md_add_data(md, data, len);
	return true;
}

void
md_delete_node(struct md *md, struct md_node *node)
{
	struct md_node *node2;
	struct md_prop *prop, *prop2;

	TAILQ_FOREACH(node2, &md->node_list, link) {
		TAILQ_FOREACH_SAFE(prop, &node2->prop_list, link, prop2) {
			if (prop->tag == MD_PROP_ARC &&
			    prop->d.arc.node == node)
				md_delete_prop(md, node2, prop);
		}
	}

	TAILQ_REMOVE(&md->node_list, node, link);
	md_free_name(md, node->name);
	free(node);
}

void
md_find_delete_node(struct md *md, const char *name)
{
	struct md_node *node;

	node = md_find_node(md, name);
	if (node)
		md_delete_node(md, node);
}

struct md *
md_alloc(void)
{
	struct md *md;

	md = xmalloc(sizeof(*md));
	TAILQ_INIT(&md->node_list);
	TAILQ_INIT(&md->name_list);
	TAILQ_INIT(&md->data_list);

	return md;
}

struct md_node *
md_find_index(struct md *md, uint64_t index)
{
	struct md_node *node;

	TAILQ_FOREACH(node, &md->node_list, link) {
		if (node->index == index)
			return node;
	}

	return NULL;
}

void
md_fixup_arcs(struct md *md)
{
	struct md_node *node;
	struct md_prop *prop;

	TAILQ_FOREACH(node, &md->node_list, link) {
		TAILQ_FOREACH(prop, &node->prop_list, link) {
			if (prop->tag == MD_PROP_ARC)
				prop->d.arc.node =
				    md_find_index(md, prop->d.arc.index);
		}
	}
}

void
md_walk_graph(struct md *md, struct md_node *root)
{
	struct md_prop *prop;

	root->index = 1;
	TAILQ_FOREACH(prop, &root->prop_list, link) {
		if (prop->tag == MD_PROP_ARC &&
		    strcmp(prop->name->str, "fwd") == 0)
			md_walk_graph(md, prop->d.arc.node);
	}
}

void
md_collect_garbage(struct md *md)
{
	struct md_node *node, *node2;

	TAILQ_FOREACH(node, &md->node_list, link)
		node->index = 0;

	md_walk_graph(md, md_find_node(md, "root"));

	TAILQ_FOREACH_SAFE(node, &md->node_list, link, node2) {
		if (node->index == 0)
			md_delete_node(md, node);
	}
}

struct md *
md_ingest(void *buf, size_t size)
{
	struct md_header *mdh = buf;
	size_t node_blk_size, name_blk_size, data_blk_size;
	size_t total_size;
	struct md_element *mde;
	struct md_node *node = NULL;
	struct md_prop *prop;
	struct md *md;
	const char *str;
	const uint8_t *data;
	uint8_t *node_blk;
	uint8_t *name_blk;
	uint8_t *data_blk;
	uint64_t index;

	if (size < sizeof(struct md_header))
		errx(1, "too small");

	if (betoh32(mdh->transport_version) != MD_TRANSPORT_VERSION)
		errx(1, "invalid transport version");

	node_blk_size = betoh32(mdh->node_blk_sz);
	name_blk_size = betoh32(mdh->name_blk_sz);
	data_blk_size = betoh32(mdh->data_blk_sz);
	total_size = node_blk_size + name_blk_size + data_blk_size;

	if (size < total_size)
		errx(1, "too small");

	md = md_alloc();

	mde = (void *)&mdh[1];
	node_blk = (void *)mde;
	name_blk = node_blk + node_blk_size;
	data_blk = name_blk + name_blk_size;

	for (index = 0; index < node_blk_size / sizeof(*mde); index++, mde++) {
		switch(mde->tag) {
		case MD_NODE:
			str = name_blk + betoh32(mde->name_offset);
			node = md_add_node(md, str);
			node->index = index;
			break;
		case MD_PROP_VAL:
			if (node == NULL)
				errx(1, "Corrupt MD");
			str = name_blk + betoh32(mde->name_offset);
			md_add_prop_val(md, node, str, betoh64(mde->d.val));
			break;
		case MD_PROP_STR:
			if (node == NULL)
				errx(1, "Corrupt MD");
			str = name_blk + betoh32(mde->name_offset);
			data = data_blk + betoh32(mde->d.y.data_offset);
			md_add_prop_str(md, node, str, data);
			break;
		case MD_PROP_DATA:
			if (node == NULL)
				errx(1, "Corrupt MD");
			str = name_blk + betoh32(mde->name_offset);
			data = data_blk + betoh32(mde->d.y.data_offset);
			md_add_prop_data(md, node, str, data,
			    betoh32(mde->d.y.data_len));
			break;
		case MD_PROP_ARC:
			if (node == NULL)
				errx(1, "Corrupt MD");
			str = name_blk + betoh32(mde->name_offset);
			prop = md_add_prop(md, node, str);
			prop->tag = MD_PROP_ARC;
			prop->d.arc.index = betoh64(mde->d.val);
			prop->d.arc.node = NULL;
			break;
		case MD_NODE_END:
			node = NULL;
			break;
		}
	}

	md_fixup_arcs(md);

	return md;
}

size_t
md_exhume(struct md *md, void **buf)
{
	struct md_node *node;
	struct md_name *name;
	struct md_data *data;
	struct md_prop *prop;
	size_t node_blk_size, name_blk_size, data_blk_size;
	size_t total_size;
	struct md_element *mde;
	struct md_header *mdh;
	uint32_t offset;
	uint64_t index;
	uint8_t *node_blk;
	uint8_t *name_blk;
	uint8_t *data_blk;
	size_t len;

	offset = 0;
	TAILQ_FOREACH(name, &md->name_list, link) {
		name->offset = offset;
		offset += (strlen(name->str) + 1);
	}
	name_blk_size = roundup(offset, MD_ALIGNMENT_SIZE);

	offset = 0;
	TAILQ_FOREACH(data, &md->data_list, link) {
		data->offset = offset;
		offset += data->len;
		offset = roundup(offset, MD_ALIGNMENT_SIZE);
	}
	data_blk_size = roundup(offset, MD_ALIGNMENT_SIZE);

	index = 0;
	TAILQ_FOREACH(node, &md->node_list, link) {
		node->index = index;
		TAILQ_FOREACH(prop, &node->prop_list, link)
			index++;
		index += 2;
	}
	node_blk_size = (index + 1) * sizeof(struct md_element);

	total_size = 16 + node_blk_size + name_blk_size + data_blk_size;
	mdh = xmalloc(total_size);

	mdh->transport_version = htobe32(MD_TRANSPORT_VERSION);
	mdh->node_blk_sz = htobe32(node_blk_size);
	mdh->name_blk_sz = htobe32(name_blk_size);
	mdh->data_blk_sz = htobe32(data_blk_size);

	mde = (void *)&mdh[1];
	node_blk = (void *)mde;
	name_blk = node_blk + node_blk_size;
	data_blk = name_blk + name_blk_size;

	TAILQ_FOREACH(node, &md->node_list, link) {
		memset(mde, 0, sizeof(*mde));
		mde->tag = MD_NODE;
		mde->name_len = strlen(node->name->str);
		mde->name_offset = htobe32(node->name->offset);
		if (TAILQ_NEXT(node, link))
			mde->d.val = htobe64(TAILQ_NEXT(node, link)->index);
		else
			mde->d.val = htobe64(index);
		mde++;
		TAILQ_FOREACH(prop, &node->prop_list, link) {
			memset(mde, 0, sizeof(*mde));
			mde->tag = prop->tag;
			mde->name_len = strlen(prop->name->str);
			mde->name_offset = htobe32(prop->name->offset);
			switch(prop->tag) {
			case MD_PROP_VAL:
				mde->d.val = htobe64(prop->d.val);
				break;
			case MD_PROP_STR:
			case MD_PROP_DATA:
				mde->d.y.data_len =
				    htobe32(prop->d.data->len);
				mde->d.y.data_offset =
				    htobe32(prop->d.data->offset);
				break;
			case MD_PROP_ARC:
				mde->d.val =
				    htobe64(prop->d.arc.node->index);
				break;
			}
			mde++;
		}
		memset(mde, 0, sizeof(*mde));
		mde->tag = MD_NODE_END;
		mde++;
	}
	memset(mde, 0, sizeof(*mde));
	mde->tag = MD_LIST_END;

	TAILQ_FOREACH(name, &md->name_list, link) {
		len = strlen(name->str) + 1;
		memcpy(name_blk, name->str, len);
		name_blk += len;
	}

	TAILQ_FOREACH(data, &md->data_list, link) {
		memcpy(data_blk, data->data, data->len);
		data_blk += roundup(data->len, MD_ALIGNMENT_SIZE);
	}

	*buf = mdh;
	return total_size;
}

struct md *
md_copy(struct md *md)
{
	void *buf;
	size_t size;

	size = md_exhume(md, &buf);
	md = md_ingest(buf, size);
	free(buf);

	return md;
}

struct md *
md_read(const char *path)
{
	FILE *fp;
	size_t size;
	void *buf;

	fp = fopen(path, "r");
	if (fp == NULL)
		return NULL;

	if (fseek(fp, 0, SEEK_END) == -1) {
		fclose(fp);
		return NULL;
	}
	size = ftell(fp);
	if (size == -1) {
		fclose(fp);
		return NULL;
	}
	if (fseek(fp, 0, SEEK_SET) == -1) {
		fclose(fp);
		return NULL;
	}

	buf = xmalloc(size);
	if (fread(buf, size, 1, fp) != 1) {
		fclose(fp);
		free(buf);
		return NULL;
	}

	fclose(fp);

	return md_ingest(buf, size);
}

void
md_write(struct md *md, const char *path)
{
	size_t size;
	void *buf;
	FILE *fp;

	size = md_exhume(md, &buf);

	fp = fopen(path, "w");
	if (fp == NULL)
		err(1, "fopen");

	if (fwrite(buf, size, 1, fp) != 1)
		err(1, "fwrite");

	fclose(fp);
}

uint32_t
md_size(const char *path)
{
	uint32_t size;
	FILE *fp;

	fp = fopen(path, "r");
	if (fp == NULL)
		err(1, "fopen");

	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fclose(fp);

	return size;
}
