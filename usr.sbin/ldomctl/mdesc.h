/*	$OpenBSD: mdesc.h,v 1.8 2019/07/14 14:40:55 kettenis Exp $	*/

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
#include <stdbool.h>

struct md_header {
	uint32_t	transport_version;
	uint32_t	node_blk_sz;
	uint32_t	name_blk_sz;
	uint32_t	data_blk_sz;
};

#define MD_TRANSPORT_VERSION    0x10000

#define MD_ALIGNMENT_SIZE	0x10

#define MD_LIST_END	0
#define MD_NODE		'N'
#define MD_NODE_END	'E'
#define MD_NOOP		' '
#define MD_PROP_ARC	'a'
#define MD_PROP_VAL	'v'
#define MD_PROP_STR	's'
#define MD_PROP_DATA	'd'

struct md_element {
	uint8_t		tag;
	uint8_t		name_len;
	uint16_t	_reserved_field;
	uint32_t	name_offset;
	union {
		struct {
			uint32_t	data_len;
			uint32_t	data_offset;
		} y;
		uint64_t	val;
	} d;
};

struct md {
	TAILQ_HEAD(md_node_head, md_node) node_list;
	TAILQ_HEAD(md_name_head, md_name) name_list;
	TAILQ_HEAD(md_data_head, md_data) data_list;
};

struct md_node {
	struct md_name *name;
	TAILQ_HEAD(md_prop_head, md_prop) prop_list;

	TAILQ_ENTRY(md_node) link;
	uint64_t index;
};

struct md_prop {
	struct md_name *name;
	uint8_t tag;
	union {
		uint64_t val;
		struct {
			uint64_t index;
			struct md_node *node;
		} arc;
		struct md_data *data;
	} d;

	TAILQ_ENTRY(md_prop) link;
};

struct md_name {
	const char *str;

	TAILQ_ENTRY(md_name) link;
	uint32_t offset;
	int refcnt;
};

struct md_data {
	void *data;
	size_t len;

	TAILQ_ENTRY(md_data) link;
	uint32_t offset;
	int refcnt;
};

struct md_prop *md_add_prop(struct md *, struct md_node *, const char *);
struct md_prop *md_add_prop_val(struct md *, struct md_node *,
		    const char *, uint64_t);
struct md_prop *md_add_prop_str(struct md *, struct md_node *,
		    const char *, const char *);
struct md_prop *md_add_prop_data(struct md *, struct md_node *,
		    const char *, const uint8_t *, size_t);
struct md_prop *md_add_prop_arc(struct md *, struct md_node *,
		    const char *,struct md_node *);
void md_delete_prop(struct md *, struct md_node *, struct md_prop *);

struct md_node *md_find_node(struct md *, const char *);
struct md_node *md_find_subnode(struct md *, struct md_node *, const char *);
struct md_node *md_add_node(struct md *, const char *);
void md_link_node(struct md *, struct md_node *, struct md_node *);
struct md_prop *md_find_prop(struct md *, struct md_node *, const char *);

bool md_get_prop_val(struct md *, struct md_node *, const char *, uint64_t *);
bool md_set_prop_val(struct md *, struct md_node *, const char *, uint64_t);
bool md_get_prop_str(struct md *, struct md_node *, const char *,
    const char **);
bool md_set_prop_data(struct md *, struct md_node *, const char *,
    const uint8_t *, size_t);
bool md_get_prop_data(struct md *, struct md_node *, const char *,
    const void **, size_t *);

void md_delete_node(struct md *, struct md_node *);
void md_find_delete_node(struct md *, const char *);

void md_collect_garbage(struct md *);

struct md *md_alloc(void);
struct md *md_ingest(void *, size_t);
size_t md_exhume(struct md *md, void **);
struct md *md_copy(struct md *);

struct md *md_read(const char *);
void md_write(struct md *, const char *);
uint32_t md_size(const char *);
