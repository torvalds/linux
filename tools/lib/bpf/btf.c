// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/* Copyright (c) 2018 Facebook */

#include <byteswap.h>
#include <endian.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/utsname.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/btf.h>
#include <gelf.h>
#include "btf.h"
#include "bpf.h"
#include "libbpf.h"
#include "libbpf_internal.h"
#include "hashmap.h"

#define BTF_MAX_NR_TYPES 0x7fffffffU
#define BTF_MAX_STR_OFFSET 0x7fffffffU

static struct btf_type btf_void;

struct btf {
	/* raw BTF data in native endianness */
	void *raw_data;
	/* raw BTF data in non-native endianness */
	void *raw_data_swapped;
	__u32 raw_size;
	/* whether target endianness differs from the native one */
	bool swapped_endian;

	/*
	 * When BTF is loaded from an ELF or raw memory it is stored
	 * in a contiguous memory block. The hdr, type_data, and, strs_data
	 * point inside that memory region to their respective parts of BTF
	 * representation:
	 *
	 * +--------------------------------+
	 * |  Header  |  Types  |  Strings  |
	 * +--------------------------------+
	 * ^          ^         ^
	 * |          |         |
	 * hdr        |         |
	 * types_data-+         |
	 * strs_data------------+
	 *
	 * If BTF data is later modified, e.g., due to types added or
	 * removed, BTF deduplication performed, etc, this contiguous
	 * representation is broken up into three independently allocated
	 * memory regions to be able to modify them independently.
	 * raw_data is nulled out at that point, but can be later allocated
	 * and cached again if user calls btf__get_raw_data(), at which point
	 * raw_data will contain a contiguous copy of header, types, and
	 * strings:
	 *
	 * +----------+  +---------+  +-----------+
	 * |  Header  |  |  Types  |  |  Strings  |
	 * +----------+  +---------+  +-----------+
	 * ^             ^            ^
	 * |             |            |
	 * hdr           |            |
	 * types_data----+            |
	 * strs_data------------------+
	 *
	 *               +----------+---------+-----------+
	 *               |  Header  |  Types  |  Strings  |
	 * raw_data----->+----------+---------+-----------+
	 */
	struct btf_header *hdr;

	void *types_data;
	size_t types_data_cap; /* used size stored in hdr->type_len */

	/* type ID to `struct btf_type *` lookup index
	 * type_offs[0] corresponds to the first non-VOID type:
	 *   - for base BTF it's type [1];
	 *   - for split BTF it's the first non-base BTF type.
	 */
	__u32 *type_offs;
	size_t type_offs_cap;
	/* number of types in this BTF instance:
	 *   - doesn't include special [0] void type;
	 *   - for split BTF counts number of types added on top of base BTF.
	 */
	__u32 nr_types;
	/* if not NULL, points to the base BTF on top of which the current
	 * split BTF is based
	 */
	struct btf *base_btf;
	/* BTF type ID of the first type in this BTF instance:
	 *   - for base BTF it's equal to 1;
	 *   - for split BTF it's equal to biggest type ID of base BTF plus 1.
	 */
	int start_id;
	/* logical string offset of this BTF instance:
	 *   - for base BTF it's equal to 0;
	 *   - for split BTF it's equal to total size of base BTF's string section size.
	 */
	int start_str_off;

	void *strs_data;
	size_t strs_data_cap; /* used size stored in hdr->str_len */

	/* lookup index for each unique string in strings section */
	struct hashmap *strs_hash;
	/* whether strings are already deduplicated */
	bool strs_deduped;
	/* extra indirection layer to make strings hashmap work with stable
	 * string offsets and ability to transparently choose between
	 * btf->strs_data or btf_dedup->strs_data as a source of strings.
	 * This is used for BTF strings dedup to transfer deduplicated strings
	 * data back to struct btf without re-building strings index.
	 */
	void **strs_data_ptr;

	/* BTF object FD, if loaded into kernel */
	int fd;

	/* Pointer size (in bytes) for a target architecture of this BTF */
	int ptr_sz;
};

static inline __u64 ptr_to_u64(const void *ptr)
{
	return (__u64) (unsigned long) ptr;
}

/* Ensure given dynamically allocated memory region pointed to by *data* with
 * capacity of *cap_cnt* elements each taking *elem_sz* bytes has enough
 * memory to accomodate *add_cnt* new elements, assuming *cur_cnt* elements
 * are already used. At most *max_cnt* elements can be ever allocated.
 * If necessary, memory is reallocated and all existing data is copied over,
 * new pointer to the memory region is stored at *data, new memory region
 * capacity (in number of elements) is stored in *cap.
 * On success, memory pointer to the beginning of unused memory is returned.
 * On error, NULL is returned.
 */
void *btf_add_mem(void **data, size_t *cap_cnt, size_t elem_sz,
		  size_t cur_cnt, size_t max_cnt, size_t add_cnt)
{
	size_t new_cnt;
	void *new_data;

	if (cur_cnt + add_cnt <= *cap_cnt)
		return *data + cur_cnt * elem_sz;

	/* requested more than the set limit */
	if (cur_cnt + add_cnt > max_cnt)
		return NULL;

	new_cnt = *cap_cnt;
	new_cnt += new_cnt / 4;		  /* expand by 25% */
	if (new_cnt < 16)		  /* but at least 16 elements */
		new_cnt = 16;
	if (new_cnt > max_cnt)		  /* but not exceeding a set limit */
		new_cnt = max_cnt;
	if (new_cnt < cur_cnt + add_cnt)  /* also ensure we have enough memory */
		new_cnt = cur_cnt + add_cnt;

	new_data = libbpf_reallocarray(*data, new_cnt, elem_sz);
	if (!new_data)
		return NULL;

	/* zero out newly allocated portion of memory */
	memset(new_data + (*cap_cnt) * elem_sz, 0, (new_cnt - *cap_cnt) * elem_sz);

	*data = new_data;
	*cap_cnt = new_cnt;
	return new_data + cur_cnt * elem_sz;
}

/* Ensure given dynamically allocated memory region has enough allocated space
 * to accommodate *need_cnt* elements of size *elem_sz* bytes each
 */
int btf_ensure_mem(void **data, size_t *cap_cnt, size_t elem_sz, size_t need_cnt)
{
	void *p;

	if (need_cnt <= *cap_cnt)
		return 0;

	p = btf_add_mem(data, cap_cnt, elem_sz, *cap_cnt, SIZE_MAX, need_cnt - *cap_cnt);
	if (!p)
		return -ENOMEM;

	return 0;
}

static int btf_add_type_idx_entry(struct btf *btf, __u32 type_off)
{
	__u32 *p;

	p = btf_add_mem((void **)&btf->type_offs, &btf->type_offs_cap, sizeof(__u32),
			btf->nr_types, BTF_MAX_NR_TYPES, 1);
	if (!p)
		return -ENOMEM;

	*p = type_off;
	return 0;
}

static void btf_bswap_hdr(struct btf_header *h)
{
	h->magic = bswap_16(h->magic);
	h->hdr_len = bswap_32(h->hdr_len);
	h->type_off = bswap_32(h->type_off);
	h->type_len = bswap_32(h->type_len);
	h->str_off = bswap_32(h->str_off);
	h->str_len = bswap_32(h->str_len);
}

static int btf_parse_hdr(struct btf *btf)
{
	struct btf_header *hdr = btf->hdr;
	__u32 meta_left;

	if (btf->raw_size < sizeof(struct btf_header)) {
		pr_debug("BTF header not found\n");
		return -EINVAL;
	}

	if (hdr->magic == bswap_16(BTF_MAGIC)) {
		btf->swapped_endian = true;
		if (bswap_32(hdr->hdr_len) != sizeof(struct btf_header)) {
			pr_warn("Can't load BTF with non-native endianness due to unsupported header length %u\n",
				bswap_32(hdr->hdr_len));
			return -ENOTSUP;
		}
		btf_bswap_hdr(hdr);
	} else if (hdr->magic != BTF_MAGIC) {
		pr_debug("Invalid BTF magic:%x\n", hdr->magic);
		return -EINVAL;
	}

	meta_left = btf->raw_size - sizeof(*hdr);
	if (meta_left < hdr->str_off + hdr->str_len) {
		pr_debug("Invalid BTF total size:%u\n", btf->raw_size);
		return -EINVAL;
	}

	if (hdr->type_off + hdr->type_len > hdr->str_off) {
		pr_debug("Invalid BTF data sections layout: type data at %u + %u, strings data at %u + %u\n",
			 hdr->type_off, hdr->type_len, hdr->str_off, hdr->str_len);
		return -EINVAL;
	}

	if (hdr->type_off % 4) {
		pr_debug("BTF type section is not aligned to 4 bytes\n");
		return -EINVAL;
	}

	return 0;
}

static int btf_parse_str_sec(struct btf *btf)
{
	const struct btf_header *hdr = btf->hdr;
	const char *start = btf->strs_data;
	const char *end = start + btf->hdr->str_len;

	if (btf->base_btf && hdr->str_len == 0)
		return 0;
	if (!hdr->str_len || hdr->str_len - 1 > BTF_MAX_STR_OFFSET || end[-1]) {
		pr_debug("Invalid BTF string section\n");
		return -EINVAL;
	}
	if (!btf->base_btf && start[0]) {
		pr_debug("Invalid BTF string section\n");
		return -EINVAL;
	}
	return 0;
}

static int btf_type_size(const struct btf_type *t)
{
	const int base_size = sizeof(struct btf_type);
	__u16 vlen = btf_vlen(t);

	switch (btf_kind(t)) {
	case BTF_KIND_FWD:
	case BTF_KIND_CONST:
	case BTF_KIND_VOLATILE:
	case BTF_KIND_RESTRICT:
	case BTF_KIND_PTR:
	case BTF_KIND_TYPEDEF:
	case BTF_KIND_FUNC:
		return base_size;
	case BTF_KIND_INT:
		return base_size + sizeof(__u32);
	case BTF_KIND_ENUM:
		return base_size + vlen * sizeof(struct btf_enum);
	case BTF_KIND_ARRAY:
		return base_size + sizeof(struct btf_array);
	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION:
		return base_size + vlen * sizeof(struct btf_member);
	case BTF_KIND_FUNC_PROTO:
		return base_size + vlen * sizeof(struct btf_param);
	case BTF_KIND_VAR:
		return base_size + sizeof(struct btf_var);
	case BTF_KIND_DATASEC:
		return base_size + vlen * sizeof(struct btf_var_secinfo);
	default:
		pr_debug("Unsupported BTF_KIND:%u\n", btf_kind(t));
		return -EINVAL;
	}
}

static void btf_bswap_type_base(struct btf_type *t)
{
	t->name_off = bswap_32(t->name_off);
	t->info = bswap_32(t->info);
	t->type = bswap_32(t->type);
}

static int btf_bswap_type_rest(struct btf_type *t)
{
	struct btf_var_secinfo *v;
	struct btf_member *m;
	struct btf_array *a;
	struct btf_param *p;
	struct btf_enum *e;
	__u16 vlen = btf_vlen(t);
	int i;

	switch (btf_kind(t)) {
	case BTF_KIND_FWD:
	case BTF_KIND_CONST:
	case BTF_KIND_VOLATILE:
	case BTF_KIND_RESTRICT:
	case BTF_KIND_PTR:
	case BTF_KIND_TYPEDEF:
	case BTF_KIND_FUNC:
		return 0;
	case BTF_KIND_INT:
		*(__u32 *)(t + 1) = bswap_32(*(__u32 *)(t + 1));
		return 0;
	case BTF_KIND_ENUM:
		for (i = 0, e = btf_enum(t); i < vlen; i++, e++) {
			e->name_off = bswap_32(e->name_off);
			e->val = bswap_32(e->val);
		}
		return 0;
	case BTF_KIND_ARRAY:
		a = btf_array(t);
		a->type = bswap_32(a->type);
		a->index_type = bswap_32(a->index_type);
		a->nelems = bswap_32(a->nelems);
		return 0;
	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION:
		for (i = 0, m = btf_members(t); i < vlen; i++, m++) {
			m->name_off = bswap_32(m->name_off);
			m->type = bswap_32(m->type);
			m->offset = bswap_32(m->offset);
		}
		return 0;
	case BTF_KIND_FUNC_PROTO:
		for (i = 0, p = btf_params(t); i < vlen; i++, p++) {
			p->name_off = bswap_32(p->name_off);
			p->type = bswap_32(p->type);
		}
		return 0;
	case BTF_KIND_VAR:
		btf_var(t)->linkage = bswap_32(btf_var(t)->linkage);
		return 0;
	case BTF_KIND_DATASEC:
		for (i = 0, v = btf_var_secinfos(t); i < vlen; i++, v++) {
			v->type = bswap_32(v->type);
			v->offset = bswap_32(v->offset);
			v->size = bswap_32(v->size);
		}
		return 0;
	default:
		pr_debug("Unsupported BTF_KIND:%u\n", btf_kind(t));
		return -EINVAL;
	}
}

static int btf_parse_type_sec(struct btf *btf)
{
	struct btf_header *hdr = btf->hdr;
	void *next_type = btf->types_data;
	void *end_type = next_type + hdr->type_len;
	int err, type_size;

	while (next_type + sizeof(struct btf_type) <= end_type) {
		if (btf->swapped_endian)
			btf_bswap_type_base(next_type);

		type_size = btf_type_size(next_type);
		if (type_size < 0)
			return type_size;
		if (next_type + type_size > end_type) {
			pr_warn("BTF type [%d] is malformed\n", btf->start_id + btf->nr_types);
			return -EINVAL;
		}

		if (btf->swapped_endian && btf_bswap_type_rest(next_type))
			return -EINVAL;

		err = btf_add_type_idx_entry(btf, next_type - btf->types_data);
		if (err)
			return err;

		next_type += type_size;
		btf->nr_types++;
	}

	if (next_type != end_type) {
		pr_warn("BTF types data is malformed\n");
		return -EINVAL;
	}

	return 0;
}

__u32 btf__get_nr_types(const struct btf *btf)
{
	return btf->start_id + btf->nr_types - 1;
}

const struct btf *btf__base_btf(const struct btf *btf)
{
	return btf->base_btf;
}

/* internal helper returning non-const pointer to a type */
static struct btf_type *btf_type_by_id(struct btf *btf, __u32 type_id)
{
	if (type_id == 0)
		return &btf_void;
	if (type_id < btf->start_id)
		return btf_type_by_id(btf->base_btf, type_id);
	return btf->types_data + btf->type_offs[type_id - btf->start_id];
}

const struct btf_type *btf__type_by_id(const struct btf *btf, __u32 type_id)
{
	if (type_id >= btf->start_id + btf->nr_types)
		return NULL;
	return btf_type_by_id((struct btf *)btf, type_id);
}

static int determine_ptr_size(const struct btf *btf)
{
	const struct btf_type *t;
	const char *name;
	int i, n;

	if (btf->base_btf && btf->base_btf->ptr_sz > 0)
		return btf->base_btf->ptr_sz;

	n = btf__get_nr_types(btf);
	for (i = 1; i <= n; i++) {
		t = btf__type_by_id(btf, i);
		if (!btf_is_int(t))
			continue;

		name = btf__name_by_offset(btf, t->name_off);
		if (!name)
			continue;

		if (strcmp(name, "long int") == 0 ||
		    strcmp(name, "long unsigned int") == 0) {
			if (t->size != 4 && t->size != 8)
				continue;
			return t->size;
		}
	}

	return -1;
}

static size_t btf_ptr_sz(const struct btf *btf)
{
	if (!btf->ptr_sz)
		((struct btf *)btf)->ptr_sz = determine_ptr_size(btf);
	return btf->ptr_sz < 0 ? sizeof(void *) : btf->ptr_sz;
}

/* Return pointer size this BTF instance assumes. The size is heuristically
 * determined by looking for 'long' or 'unsigned long' integer type and
 * recording its size in bytes. If BTF type information doesn't have any such
 * type, this function returns 0. In the latter case, native architecture's
 * pointer size is assumed, so will be either 4 or 8, depending on
 * architecture that libbpf was compiled for. It's possible to override
 * guessed value by using btf__set_pointer_size() API.
 */
size_t btf__pointer_size(const struct btf *btf)
{
	if (!btf->ptr_sz)
		((struct btf *)btf)->ptr_sz = determine_ptr_size(btf);

	if (btf->ptr_sz < 0)
		/* not enough BTF type info to guess */
		return 0;

	return btf->ptr_sz;
}

/* Override or set pointer size in bytes. Only values of 4 and 8 are
 * supported.
 */
int btf__set_pointer_size(struct btf *btf, size_t ptr_sz)
{
	if (ptr_sz != 4 && ptr_sz != 8)
		return -EINVAL;
	btf->ptr_sz = ptr_sz;
	return 0;
}

static bool is_host_big_endian(void)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
	return false;
#elif __BYTE_ORDER == __BIG_ENDIAN
	return true;
#else
# error "Unrecognized __BYTE_ORDER__"
#endif
}

enum btf_endianness btf__endianness(const struct btf *btf)
{
	if (is_host_big_endian())
		return btf->swapped_endian ? BTF_LITTLE_ENDIAN : BTF_BIG_ENDIAN;
	else
		return btf->swapped_endian ? BTF_BIG_ENDIAN : BTF_LITTLE_ENDIAN;
}

int btf__set_endianness(struct btf *btf, enum btf_endianness endian)
{
	if (endian != BTF_LITTLE_ENDIAN && endian != BTF_BIG_ENDIAN)
		return -EINVAL;

	btf->swapped_endian = is_host_big_endian() != (endian == BTF_BIG_ENDIAN);
	if (!btf->swapped_endian) {
		free(btf->raw_data_swapped);
		btf->raw_data_swapped = NULL;
	}
	return 0;
}

static bool btf_type_is_void(const struct btf_type *t)
{
	return t == &btf_void || btf_is_fwd(t);
}

static bool btf_type_is_void_or_null(const struct btf_type *t)
{
	return !t || btf_type_is_void(t);
}

#define MAX_RESOLVE_DEPTH 32

__s64 btf__resolve_size(const struct btf *btf, __u32 type_id)
{
	const struct btf_array *array;
	const struct btf_type *t;
	__u32 nelems = 1;
	__s64 size = -1;
	int i;

	t = btf__type_by_id(btf, type_id);
	for (i = 0; i < MAX_RESOLVE_DEPTH && !btf_type_is_void_or_null(t);
	     i++) {
		switch (btf_kind(t)) {
		case BTF_KIND_INT:
		case BTF_KIND_STRUCT:
		case BTF_KIND_UNION:
		case BTF_KIND_ENUM:
		case BTF_KIND_DATASEC:
			size = t->size;
			goto done;
		case BTF_KIND_PTR:
			size = btf_ptr_sz(btf);
			goto done;
		case BTF_KIND_TYPEDEF:
		case BTF_KIND_VOLATILE:
		case BTF_KIND_CONST:
		case BTF_KIND_RESTRICT:
		case BTF_KIND_VAR:
			type_id = t->type;
			break;
		case BTF_KIND_ARRAY:
			array = btf_array(t);
			if (nelems && array->nelems > UINT32_MAX / nelems)
				return -E2BIG;
			nelems *= array->nelems;
			type_id = array->type;
			break;
		default:
			return -EINVAL;
		}

		t = btf__type_by_id(btf, type_id);
	}

done:
	if (size < 0)
		return -EINVAL;
	if (nelems && size > UINT32_MAX / nelems)
		return -E2BIG;

	return nelems * size;
}

int btf__align_of(const struct btf *btf, __u32 id)
{
	const struct btf_type *t = btf__type_by_id(btf, id);
	__u16 kind = btf_kind(t);

	switch (kind) {
	case BTF_KIND_INT:
	case BTF_KIND_ENUM:
		return min(btf_ptr_sz(btf), (size_t)t->size);
	case BTF_KIND_PTR:
		return btf_ptr_sz(btf);
	case BTF_KIND_TYPEDEF:
	case BTF_KIND_VOLATILE:
	case BTF_KIND_CONST:
	case BTF_KIND_RESTRICT:
		return btf__align_of(btf, t->type);
	case BTF_KIND_ARRAY:
		return btf__align_of(btf, btf_array(t)->type);
	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION: {
		const struct btf_member *m = btf_members(t);
		__u16 vlen = btf_vlen(t);
		int i, max_align = 1, align;

		for (i = 0; i < vlen; i++, m++) {
			align = btf__align_of(btf, m->type);
			if (align <= 0)
				return align;
			max_align = max(max_align, align);
		}

		return max_align;
	}
	default:
		pr_warn("unsupported BTF_KIND:%u\n", btf_kind(t));
		return 0;
	}
}

int btf__resolve_type(const struct btf *btf, __u32 type_id)
{
	const struct btf_type *t;
	int depth = 0;

	t = btf__type_by_id(btf, type_id);
	while (depth < MAX_RESOLVE_DEPTH &&
	       !btf_type_is_void_or_null(t) &&
	       (btf_is_mod(t) || btf_is_typedef(t) || btf_is_var(t))) {
		type_id = t->type;
		t = btf__type_by_id(btf, type_id);
		depth++;
	}

	if (depth == MAX_RESOLVE_DEPTH || btf_type_is_void_or_null(t))
		return -EINVAL;

	return type_id;
}

__s32 btf__find_by_name(const struct btf *btf, const char *type_name)
{
	__u32 i, nr_types = btf__get_nr_types(btf);

	if (!strcmp(type_name, "void"))
		return 0;

	for (i = 1; i <= nr_types; i++) {
		const struct btf_type *t = btf__type_by_id(btf, i);
		const char *name = btf__name_by_offset(btf, t->name_off);

		if (name && !strcmp(type_name, name))
			return i;
	}

	return -ENOENT;
}

__s32 btf__find_by_name_kind(const struct btf *btf, const char *type_name,
			     __u32 kind)
{
	__u32 i, nr_types = btf__get_nr_types(btf);

	if (kind == BTF_KIND_UNKN || !strcmp(type_name, "void"))
		return 0;

	for (i = 1; i <= nr_types; i++) {
		const struct btf_type *t = btf__type_by_id(btf, i);
		const char *name;

		if (btf_kind(t) != kind)
			continue;
		name = btf__name_by_offset(btf, t->name_off);
		if (name && !strcmp(type_name, name))
			return i;
	}

	return -ENOENT;
}

static bool btf_is_modifiable(const struct btf *btf)
{
	return (void *)btf->hdr != btf->raw_data;
}

void btf__free(struct btf *btf)
{
	if (IS_ERR_OR_NULL(btf))
		return;

	if (btf->fd >= 0)
		close(btf->fd);

	if (btf_is_modifiable(btf)) {
		/* if BTF was modified after loading, it will have a split
		 * in-memory representation for header, types, and strings
		 * sections, so we need to free all of them individually. It
		 * might still have a cached contiguous raw data present,
		 * which will be unconditionally freed below.
		 */
		free(btf->hdr);
		free(btf->types_data);
		free(btf->strs_data);
	}
	free(btf->raw_data);
	free(btf->raw_data_swapped);
	free(btf->type_offs);
	free(btf);
}

static struct btf *btf_new_empty(struct btf *base_btf)
{
	struct btf *btf;

	btf = calloc(1, sizeof(*btf));
	if (!btf)
		return ERR_PTR(-ENOMEM);

	btf->nr_types = 0;
	btf->start_id = 1;
	btf->start_str_off = 0;
	btf->fd = -1;
	btf->ptr_sz = sizeof(void *);
	btf->swapped_endian = false;

	if (base_btf) {
		btf->base_btf = base_btf;
		btf->start_id = btf__get_nr_types(base_btf) + 1;
		btf->start_str_off = base_btf->hdr->str_len;
	}

	/* +1 for empty string at offset 0 */
	btf->raw_size = sizeof(struct btf_header) + (base_btf ? 0 : 1);
	btf->raw_data = calloc(1, btf->raw_size);
	if (!btf->raw_data) {
		free(btf);
		return ERR_PTR(-ENOMEM);
	}

	btf->hdr = btf->raw_data;
	btf->hdr->hdr_len = sizeof(struct btf_header);
	btf->hdr->magic = BTF_MAGIC;
	btf->hdr->version = BTF_VERSION;

	btf->types_data = btf->raw_data + btf->hdr->hdr_len;
	btf->strs_data = btf->raw_data + btf->hdr->hdr_len;
	btf->hdr->str_len = base_btf ? 0 : 1; /* empty string at offset 0 */

	return btf;
}

struct btf *btf__new_empty(void)
{
	return btf_new_empty(NULL);
}

struct btf *btf__new_empty_split(struct btf *base_btf)
{
	return btf_new_empty(base_btf);
}

static struct btf *btf_new(const void *data, __u32 size, struct btf *base_btf)
{
	struct btf *btf;
	int err;

	btf = calloc(1, sizeof(struct btf));
	if (!btf)
		return ERR_PTR(-ENOMEM);

	btf->nr_types = 0;
	btf->start_id = 1;
	btf->start_str_off = 0;

	if (base_btf) {
		btf->base_btf = base_btf;
		btf->start_id = btf__get_nr_types(base_btf) + 1;
		btf->start_str_off = base_btf->hdr->str_len;
	}

	btf->raw_data = malloc(size);
	if (!btf->raw_data) {
		err = -ENOMEM;
		goto done;
	}
	memcpy(btf->raw_data, data, size);
	btf->raw_size = size;

	btf->hdr = btf->raw_data;
	err = btf_parse_hdr(btf);
	if (err)
		goto done;

	btf->strs_data = btf->raw_data + btf->hdr->hdr_len + btf->hdr->str_off;
	btf->types_data = btf->raw_data + btf->hdr->hdr_len + btf->hdr->type_off;

	err = btf_parse_str_sec(btf);
	err = err ?: btf_parse_type_sec(btf);
	if (err)
		goto done;

	btf->fd = -1;

done:
	if (err) {
		btf__free(btf);
		return ERR_PTR(err);
	}

	return btf;
}

struct btf *btf__new(const void *data, __u32 size)
{
	return btf_new(data, size, NULL);
}

static struct btf *btf_parse_elf(const char *path, struct btf *base_btf,
				 struct btf_ext **btf_ext)
{
	Elf_Data *btf_data = NULL, *btf_ext_data = NULL;
	int err = 0, fd = -1, idx = 0;
	struct btf *btf = NULL;
	Elf_Scn *scn = NULL;
	Elf *elf = NULL;
	GElf_Ehdr ehdr;
	size_t shstrndx;

	if (elf_version(EV_CURRENT) == EV_NONE) {
		pr_warn("failed to init libelf for %s\n", path);
		return ERR_PTR(-LIBBPF_ERRNO__LIBELF);
	}

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		err = -errno;
		pr_warn("failed to open %s: %s\n", path, strerror(errno));
		return ERR_PTR(err);
	}

	err = -LIBBPF_ERRNO__FORMAT;

	elf = elf_begin(fd, ELF_C_READ, NULL);
	if (!elf) {
		pr_warn("failed to open %s as ELF file\n", path);
		goto done;
	}
	if (!gelf_getehdr(elf, &ehdr)) {
		pr_warn("failed to get EHDR from %s\n", path);
		goto done;
	}

	if (elf_getshdrstrndx(elf, &shstrndx)) {
		pr_warn("failed to get section names section index for %s\n",
			path);
		goto done;
	}

	if (!elf_rawdata(elf_getscn(elf, shstrndx), NULL)) {
		pr_warn("failed to get e_shstrndx from %s\n", path);
		goto done;
	}

	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		GElf_Shdr sh;
		char *name;

		idx++;
		if (gelf_getshdr(scn, &sh) != &sh) {
			pr_warn("failed to get section(%d) header from %s\n",
				idx, path);
			goto done;
		}
		name = elf_strptr(elf, shstrndx, sh.sh_name);
		if (!name) {
			pr_warn("failed to get section(%d) name from %s\n",
				idx, path);
			goto done;
		}
		if (strcmp(name, BTF_ELF_SEC) == 0) {
			btf_data = elf_getdata(scn, 0);
			if (!btf_data) {
				pr_warn("failed to get section(%d, %s) data from %s\n",
					idx, name, path);
				goto done;
			}
			continue;
		} else if (btf_ext && strcmp(name, BTF_EXT_ELF_SEC) == 0) {
			btf_ext_data = elf_getdata(scn, 0);
			if (!btf_ext_data) {
				pr_warn("failed to get section(%d, %s) data from %s\n",
					idx, name, path);
				goto done;
			}
			continue;
		}
	}

	err = 0;

	if (!btf_data) {
		err = -ENOENT;
		goto done;
	}
	btf = btf_new(btf_data->d_buf, btf_data->d_size, base_btf);
	if (IS_ERR(btf))
		goto done;

	switch (gelf_getclass(elf)) {
	case ELFCLASS32:
		btf__set_pointer_size(btf, 4);
		break;
	case ELFCLASS64:
		btf__set_pointer_size(btf, 8);
		break;
	default:
		pr_warn("failed to get ELF class (bitness) for %s\n", path);
		break;
	}

	if (btf_ext && btf_ext_data) {
		*btf_ext = btf_ext__new(btf_ext_data->d_buf,
					btf_ext_data->d_size);
		if (IS_ERR(*btf_ext))
			goto done;
	} else if (btf_ext) {
		*btf_ext = NULL;
	}
done:
	if (elf)
		elf_end(elf);
	close(fd);

	if (err)
		return ERR_PTR(err);
	/*
	 * btf is always parsed before btf_ext, so no need to clean up
	 * btf_ext, if btf loading failed
	 */
	if (IS_ERR(btf))
		return btf;
	if (btf_ext && IS_ERR(*btf_ext)) {
		btf__free(btf);
		err = PTR_ERR(*btf_ext);
		return ERR_PTR(err);
	}
	return btf;
}

struct btf *btf__parse_elf(const char *path, struct btf_ext **btf_ext)
{
	return btf_parse_elf(path, NULL, btf_ext);
}

struct btf *btf__parse_elf_split(const char *path, struct btf *base_btf)
{
	return btf_parse_elf(path, base_btf, NULL);
}

static struct btf *btf_parse_raw(const char *path, struct btf *base_btf)
{
	struct btf *btf = NULL;
	void *data = NULL;
	FILE *f = NULL;
	__u16 magic;
	int err = 0;
	long sz;

	f = fopen(path, "rb");
	if (!f) {
		err = -errno;
		goto err_out;
	}

	/* check BTF magic */
	if (fread(&magic, 1, sizeof(magic), f) < sizeof(magic)) {
		err = -EIO;
		goto err_out;
	}
	if (magic != BTF_MAGIC && magic != bswap_16(BTF_MAGIC)) {
		/* definitely not a raw BTF */
		err = -EPROTO;
		goto err_out;
	}

	/* get file size */
	if (fseek(f, 0, SEEK_END)) {
		err = -errno;
		goto err_out;
	}
	sz = ftell(f);
	if (sz < 0) {
		err = -errno;
		goto err_out;
	}
	/* rewind to the start */
	if (fseek(f, 0, SEEK_SET)) {
		err = -errno;
		goto err_out;
	}

	/* pre-alloc memory and read all of BTF data */
	data = malloc(sz);
	if (!data) {
		err = -ENOMEM;
		goto err_out;
	}
	if (fread(data, 1, sz, f) < sz) {
		err = -EIO;
		goto err_out;
	}

	/* finally parse BTF data */
	btf = btf_new(data, sz, base_btf);

err_out:
	free(data);
	if (f)
		fclose(f);
	return err ? ERR_PTR(err) : btf;
}

struct btf *btf__parse_raw(const char *path)
{
	return btf_parse_raw(path, NULL);
}

struct btf *btf__parse_raw_split(const char *path, struct btf *base_btf)
{
	return btf_parse_raw(path, base_btf);
}

static struct btf *btf_parse(const char *path, struct btf *base_btf, struct btf_ext **btf_ext)
{
	struct btf *btf;

	if (btf_ext)
		*btf_ext = NULL;

	btf = btf_parse_raw(path, base_btf);
	if (!IS_ERR(btf) || PTR_ERR(btf) != -EPROTO)
		return btf;

	return btf_parse_elf(path, base_btf, btf_ext);
}

struct btf *btf__parse(const char *path, struct btf_ext **btf_ext)
{
	return btf_parse(path, NULL, btf_ext);
}

struct btf *btf__parse_split(const char *path, struct btf *base_btf)
{
	return btf_parse(path, base_btf, NULL);
}

static int compare_vsi_off(const void *_a, const void *_b)
{
	const struct btf_var_secinfo *a = _a;
	const struct btf_var_secinfo *b = _b;

	return a->offset - b->offset;
}

static int btf_fixup_datasec(struct bpf_object *obj, struct btf *btf,
			     struct btf_type *t)
{
	__u32 size = 0, off = 0, i, vars = btf_vlen(t);
	const char *name = btf__name_by_offset(btf, t->name_off);
	const struct btf_type *t_var;
	struct btf_var_secinfo *vsi;
	const struct btf_var *var;
	int ret;

	if (!name) {
		pr_debug("No name found in string section for DATASEC kind.\n");
		return -ENOENT;
	}

	/* .extern datasec size and var offsets were set correctly during
	 * extern collection step, so just skip straight to sorting variables
	 */
	if (t->size)
		goto sort_vars;

	ret = bpf_object__section_size(obj, name, &size);
	if (ret || !size || (t->size && t->size != size)) {
		pr_debug("Invalid size for section %s: %u bytes\n", name, size);
		return -ENOENT;
	}

	t->size = size;

	for (i = 0, vsi = btf_var_secinfos(t); i < vars; i++, vsi++) {
		t_var = btf__type_by_id(btf, vsi->type);
		var = btf_var(t_var);

		if (!btf_is_var(t_var)) {
			pr_debug("Non-VAR type seen in section %s\n", name);
			return -EINVAL;
		}

		if (var->linkage == BTF_VAR_STATIC)
			continue;

		name = btf__name_by_offset(btf, t_var->name_off);
		if (!name) {
			pr_debug("No name found in string section for VAR kind\n");
			return -ENOENT;
		}

		ret = bpf_object__variable_offset(obj, name, &off);
		if (ret) {
			pr_debug("No offset found in symbol table for VAR %s\n",
				 name);
			return -ENOENT;
		}

		vsi->offset = off;
	}

sort_vars:
	qsort(btf_var_secinfos(t), vars, sizeof(*vsi), compare_vsi_off);
	return 0;
}

int btf__finalize_data(struct bpf_object *obj, struct btf *btf)
{
	int err = 0;
	__u32 i;

	for (i = 1; i <= btf->nr_types; i++) {
		struct btf_type *t = btf_type_by_id(btf, i);

		/* Loader needs to fix up some of the things compiler
		 * couldn't get its hands on while emitting BTF. This
		 * is section size and global variable offset. We use
		 * the info from the ELF itself for this purpose.
		 */
		if (btf_is_datasec(t)) {
			err = btf_fixup_datasec(obj, btf, t);
			if (err)
				break;
		}
	}

	return err;
}

static void *btf_get_raw_data(const struct btf *btf, __u32 *size, bool swap_endian);

int btf__load(struct btf *btf)
{
	__u32 log_buf_size = 0, raw_size;
	char *log_buf = NULL;
	void *raw_data;
	int err = 0;

	if (btf->fd >= 0)
		return -EEXIST;

retry_load:
	if (log_buf_size) {
		log_buf = malloc(log_buf_size);
		if (!log_buf)
			return -ENOMEM;

		*log_buf = 0;
	}

	raw_data = btf_get_raw_data(btf, &raw_size, false);
	if (!raw_data) {
		err = -ENOMEM;
		goto done;
	}
	/* cache native raw data representation */
	btf->raw_size = raw_size;
	btf->raw_data = raw_data;

	btf->fd = bpf_load_btf(raw_data, raw_size, log_buf, log_buf_size, false);
	if (btf->fd < 0) {
		if (!log_buf || errno == ENOSPC) {
			log_buf_size = max((__u32)BPF_LOG_BUF_SIZE,
					   log_buf_size << 1);
			free(log_buf);
			goto retry_load;
		}

		err = -errno;
		pr_warn("Error loading BTF: %s(%d)\n", strerror(errno), errno);
		if (*log_buf)
			pr_warn("%s\n", log_buf);
		goto done;
	}

done:
	free(log_buf);
	return err;
}

int btf__fd(const struct btf *btf)
{
	return btf->fd;
}

void btf__set_fd(struct btf *btf, int fd)
{
	btf->fd = fd;
}

static void *btf_get_raw_data(const struct btf *btf, __u32 *size, bool swap_endian)
{
	struct btf_header *hdr = btf->hdr;
	struct btf_type *t;
	void *data, *p;
	__u32 data_sz;
	int i;

	data = swap_endian ? btf->raw_data_swapped : btf->raw_data;
	if (data) {
		*size = btf->raw_size;
		return data;
	}

	data_sz = hdr->hdr_len + hdr->type_len + hdr->str_len;
	data = calloc(1, data_sz);
	if (!data)
		return NULL;
	p = data;

	memcpy(p, hdr, hdr->hdr_len);
	if (swap_endian)
		btf_bswap_hdr(p);
	p += hdr->hdr_len;

	memcpy(p, btf->types_data, hdr->type_len);
	if (swap_endian) {
		for (i = 0; i < btf->nr_types; i++) {
			t = p + btf->type_offs[i];
			/* btf_bswap_type_rest() relies on native t->info, so
			 * we swap base type info after we swapped all the
			 * additional information
			 */
			if (btf_bswap_type_rest(t))
				goto err_out;
			btf_bswap_type_base(t);
		}
	}
	p += hdr->type_len;

	memcpy(p, btf->strs_data, hdr->str_len);
	p += hdr->str_len;

	*size = data_sz;
	return data;
err_out:
	free(data);
	return NULL;
}

const void *btf__get_raw_data(const struct btf *btf_ro, __u32 *size)
{
	struct btf *btf = (struct btf *)btf_ro;
	__u32 data_sz;
	void *data;

	data = btf_get_raw_data(btf, &data_sz, btf->swapped_endian);
	if (!data)
		return NULL;

	btf->raw_size = data_sz;
	if (btf->swapped_endian)
		btf->raw_data_swapped = data;
	else
		btf->raw_data = data;
	*size = data_sz;
	return data;
}

const char *btf__str_by_offset(const struct btf *btf, __u32 offset)
{
	if (offset < btf->start_str_off)
		return btf__str_by_offset(btf->base_btf, offset);
	else if (offset - btf->start_str_off < btf->hdr->str_len)
		return btf->strs_data + (offset - btf->start_str_off);
	else
		return NULL;
}

const char *btf__name_by_offset(const struct btf *btf, __u32 offset)
{
	return btf__str_by_offset(btf, offset);
}

struct btf *btf_get_from_fd(int btf_fd, struct btf *base_btf)
{
	struct bpf_btf_info btf_info;
	__u32 len = sizeof(btf_info);
	__u32 last_size;
	struct btf *btf;
	void *ptr;
	int err;

	/* we won't know btf_size until we call bpf_obj_get_info_by_fd(). so
	 * let's start with a sane default - 4KiB here - and resize it only if
	 * bpf_obj_get_info_by_fd() needs a bigger buffer.
	 */
	last_size = 4096;
	ptr = malloc(last_size);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	memset(&btf_info, 0, sizeof(btf_info));
	btf_info.btf = ptr_to_u64(ptr);
	btf_info.btf_size = last_size;
	err = bpf_obj_get_info_by_fd(btf_fd, &btf_info, &len);

	if (!err && btf_info.btf_size > last_size) {
		void *temp_ptr;

		last_size = btf_info.btf_size;
		temp_ptr = realloc(ptr, last_size);
		if (!temp_ptr) {
			btf = ERR_PTR(-ENOMEM);
			goto exit_free;
		}
		ptr = temp_ptr;

		len = sizeof(btf_info);
		memset(&btf_info, 0, sizeof(btf_info));
		btf_info.btf = ptr_to_u64(ptr);
		btf_info.btf_size = last_size;

		err = bpf_obj_get_info_by_fd(btf_fd, &btf_info, &len);
	}

	if (err || btf_info.btf_size > last_size) {
		btf = err ? ERR_PTR(-errno) : ERR_PTR(-E2BIG);
		goto exit_free;
	}

	btf = btf_new(ptr, btf_info.btf_size, base_btf);

exit_free:
	free(ptr);
	return btf;
}

int btf__get_from_id(__u32 id, struct btf **btf)
{
	struct btf *res;
	int btf_fd;

	*btf = NULL;
	btf_fd = bpf_btf_get_fd_by_id(id);
	if (btf_fd < 0)
		return -errno;

	res = btf_get_from_fd(btf_fd, NULL);
	close(btf_fd);
	if (IS_ERR(res))
		return PTR_ERR(res);

	*btf = res;
	return 0;
}

int btf__get_map_kv_tids(const struct btf *btf, const char *map_name,
			 __u32 expected_key_size, __u32 expected_value_size,
			 __u32 *key_type_id, __u32 *value_type_id)
{
	const struct btf_type *container_type;
	const struct btf_member *key, *value;
	const size_t max_name = 256;
	char container_name[max_name];
	__s64 key_size, value_size;
	__s32 container_id;

	if (snprintf(container_name, max_name, "____btf_map_%s", map_name) ==
	    max_name) {
		pr_warn("map:%s length of '____btf_map_%s' is too long\n",
			map_name, map_name);
		return -EINVAL;
	}

	container_id = btf__find_by_name(btf, container_name);
	if (container_id < 0) {
		pr_debug("map:%s container_name:%s cannot be found in BTF. Missing BPF_ANNOTATE_KV_PAIR?\n",
			 map_name, container_name);
		return container_id;
	}

	container_type = btf__type_by_id(btf, container_id);
	if (!container_type) {
		pr_warn("map:%s cannot find BTF type for container_id:%u\n",
			map_name, container_id);
		return -EINVAL;
	}

	if (!btf_is_struct(container_type) || btf_vlen(container_type) < 2) {
		pr_warn("map:%s container_name:%s is an invalid container struct\n",
			map_name, container_name);
		return -EINVAL;
	}

	key = btf_members(container_type);
	value = key + 1;

	key_size = btf__resolve_size(btf, key->type);
	if (key_size < 0) {
		pr_warn("map:%s invalid BTF key_type_size\n", map_name);
		return key_size;
	}

	if (expected_key_size != key_size) {
		pr_warn("map:%s btf_key_type_size:%u != map_def_key_size:%u\n",
			map_name, (__u32)key_size, expected_key_size);
		return -EINVAL;
	}

	value_size = btf__resolve_size(btf, value->type);
	if (value_size < 0) {
		pr_warn("map:%s invalid BTF value_type_size\n", map_name);
		return value_size;
	}

	if (expected_value_size != value_size) {
		pr_warn("map:%s btf_value_type_size:%u != map_def_value_size:%u\n",
			map_name, (__u32)value_size, expected_value_size);
		return -EINVAL;
	}

	*key_type_id = key->type;
	*value_type_id = value->type;

	return 0;
}

static size_t strs_hash_fn(const void *key, void *ctx)
{
	const struct btf *btf = ctx;
	const char *strs = *btf->strs_data_ptr;
	const char *str = strs + (long)key;

	return str_hash(str);
}

static bool strs_hash_equal_fn(const void *key1, const void *key2, void *ctx)
{
	const struct btf *btf = ctx;
	const char *strs = *btf->strs_data_ptr;
	const char *str1 = strs + (long)key1;
	const char *str2 = strs + (long)key2;

	return strcmp(str1, str2) == 0;
}

static void btf_invalidate_raw_data(struct btf *btf)
{
	if (btf->raw_data) {
		free(btf->raw_data);
		btf->raw_data = NULL;
	}
	if (btf->raw_data_swapped) {
		free(btf->raw_data_swapped);
		btf->raw_data_swapped = NULL;
	}
}

/* Ensure BTF is ready to be modified (by splitting into a three memory
 * regions for header, types, and strings). Also invalidate cached
 * raw_data, if any.
 */
static int btf_ensure_modifiable(struct btf *btf)
{
	void *hdr, *types, *strs, *strs_end, *s;
	struct hashmap *hash = NULL;
	long off;
	int err;

	if (btf_is_modifiable(btf)) {
		/* any BTF modification invalidates raw_data */
		btf_invalidate_raw_data(btf);
		return 0;
	}

	/* split raw data into three memory regions */
	hdr = malloc(btf->hdr->hdr_len);
	types = malloc(btf->hdr->type_len);
	strs = malloc(btf->hdr->str_len);
	if (!hdr || !types || !strs)
		goto err_out;

	memcpy(hdr, btf->hdr, btf->hdr->hdr_len);
	memcpy(types, btf->types_data, btf->hdr->type_len);
	memcpy(strs, btf->strs_data, btf->hdr->str_len);

	/* make hashmap below use btf->strs_data as a source of strings */
	btf->strs_data_ptr = &btf->strs_data;

	/* build lookup index for all strings */
	hash = hashmap__new(strs_hash_fn, strs_hash_equal_fn, btf);
	if (IS_ERR(hash)) {
		err = PTR_ERR(hash);
		hash = NULL;
		goto err_out;
	}

	strs_end = strs + btf->hdr->str_len;
	for (off = 0, s = strs; s < strs_end; off += strlen(s) + 1, s = strs + off) {
		/* hashmap__add() returns EEXIST if string with the same
		 * content already is in the hash map
		 */
		err = hashmap__add(hash, (void *)off, (void *)off);
		if (err == -EEXIST)
			continue; /* duplicate */
		if (err)
			goto err_out;
	}

	/* only when everything was successful, update internal state */
	btf->hdr = hdr;
	btf->types_data = types;
	btf->types_data_cap = btf->hdr->type_len;
	btf->strs_data = strs;
	btf->strs_data_cap = btf->hdr->str_len;
	btf->strs_hash = hash;
	/* if BTF was created from scratch, all strings are guaranteed to be
	 * unique and deduplicated
	 */
	if (btf->hdr->str_len == 0)
		btf->strs_deduped = true;
	if (!btf->base_btf && btf->hdr->str_len == 1)
		btf->strs_deduped = true;

	/* invalidate raw_data representation */
	btf_invalidate_raw_data(btf);

	return 0;

err_out:
	hashmap__free(hash);
	free(hdr);
	free(types);
	free(strs);
	return -ENOMEM;
}

static void *btf_add_str_mem(struct btf *btf, size_t add_sz)
{
	return btf_add_mem(&btf->strs_data, &btf->strs_data_cap, 1,
			   btf->hdr->str_len, BTF_MAX_STR_OFFSET, add_sz);
}

/* Find an offset in BTF string section that corresponds to a given string *s*.
 * Returns:
 *   - >0 offset into string section, if string is found;
 *   - -ENOENT, if string is not in the string section;
 *   - <0, on any other error.
 */
int btf__find_str(struct btf *btf, const char *s)
{
	long old_off, new_off, len;
	void *p;

	if (btf->base_btf) {
		int ret;

		ret = btf__find_str(btf->base_btf, s);
		if (ret != -ENOENT)
			return ret;
	}

	/* BTF needs to be in a modifiable state to build string lookup index */
	if (btf_ensure_modifiable(btf))
		return -ENOMEM;

	/* see btf__add_str() for why we do this */
	len = strlen(s) + 1;
	p = btf_add_str_mem(btf, len);
	if (!p)
		return -ENOMEM;

	new_off = btf->hdr->str_len;
	memcpy(p, s, len);

	if (hashmap__find(btf->strs_hash, (void *)new_off, (void **)&old_off))
		return btf->start_str_off + old_off;

	return -ENOENT;
}

/* Add a string s to the BTF string section.
 * Returns:
 *   - > 0 offset into string section, on success;
 *   - < 0, on error.
 */
int btf__add_str(struct btf *btf, const char *s)
{
	long old_off, new_off, len;
	void *p;
	int err;

	if (btf->base_btf) {
		int ret;

		ret = btf__find_str(btf->base_btf, s);
		if (ret != -ENOENT)
			return ret;
	}

	if (btf_ensure_modifiable(btf))
		return -ENOMEM;

	/* Hashmap keys are always offsets within btf->strs_data, so to even
	 * look up some string from the "outside", we need to first append it
	 * at the end, so that it can be addressed with an offset. Luckily,
	 * until btf->hdr->str_len is incremented, that string is just a piece
	 * of garbage for the rest of BTF code, so no harm, no foul. On the
	 * other hand, if the string is unique, it's already appended and
	 * ready to be used, only a simple btf->hdr->str_len increment away.
	 */
	len = strlen(s) + 1;
	p = btf_add_str_mem(btf, len);
	if (!p)
		return -ENOMEM;

	new_off = btf->hdr->str_len;
	memcpy(p, s, len);

	/* Now attempt to add the string, but only if the string with the same
	 * contents doesn't exist already (HASHMAP_ADD strategy). If such
	 * string exists, we'll get its offset in old_off (that's old_key).
	 */
	err = hashmap__insert(btf->strs_hash, (void *)new_off, (void *)new_off,
			      HASHMAP_ADD, (const void **)&old_off, NULL);
	if (err == -EEXIST)
		return btf->start_str_off + old_off; /* duplicated string, return existing offset */
	if (err)
		return err;

	btf->hdr->str_len += len; /* new unique string, adjust data length */
	return btf->start_str_off + new_off;
}

static void *btf_add_type_mem(struct btf *btf, size_t add_sz)
{
	return btf_add_mem(&btf->types_data, &btf->types_data_cap, 1,
			   btf->hdr->type_len, UINT_MAX, add_sz);
}

static __u32 btf_type_info(int kind, int vlen, int kflag)
{
	return (kflag << 31) | (kind << 24) | vlen;
}

static void btf_type_inc_vlen(struct btf_type *t)
{
	t->info = btf_type_info(btf_kind(t), btf_vlen(t) + 1, btf_kflag(t));
}

static int btf_commit_type(struct btf *btf, int data_sz)
{
	int err;

	err = btf_add_type_idx_entry(btf, btf->hdr->type_len);
	if (err)
		return err;

	btf->hdr->type_len += data_sz;
	btf->hdr->str_off += data_sz;
	btf->nr_types++;
	return btf->start_id + btf->nr_types - 1;
}

/*
 * Append new BTF_KIND_INT type with:
 *   - *name* - non-empty, non-NULL type name;
 *   - *sz* - power-of-2 (1, 2, 4, ..) size of the type, in bytes;
 *   - encoding is a combination of BTF_INT_SIGNED, BTF_INT_CHAR, BTF_INT_BOOL.
 * Returns:
 *   - >0, type ID of newly added BTF type;
 *   - <0, on error.
 */
int btf__add_int(struct btf *btf, const char *name, size_t byte_sz, int encoding)
{
	struct btf_type *t;
	int sz, name_off;

	/* non-empty name */
	if (!name || !name[0])
		return -EINVAL;
	/* byte_sz must be power of 2 */
	if (!byte_sz || (byte_sz & (byte_sz - 1)) || byte_sz > 16)
		return -EINVAL;
	if (encoding & ~(BTF_INT_SIGNED | BTF_INT_CHAR | BTF_INT_BOOL))
		return -EINVAL;

	/* deconstruct BTF, if necessary, and invalidate raw_data */
	if (btf_ensure_modifiable(btf))
		return -ENOMEM;

	sz = sizeof(struct btf_type) + sizeof(int);
	t = btf_add_type_mem(btf, sz);
	if (!t)
		return -ENOMEM;

	/* if something goes wrong later, we might end up with an extra string,
	 * but that shouldn't be a problem, because BTF can't be constructed
	 * completely anyway and will most probably be just discarded
	 */
	name_off = btf__add_str(btf, name);
	if (name_off < 0)
		return name_off;

	t->name_off = name_off;
	t->info = btf_type_info(BTF_KIND_INT, 0, 0);
	t->size = byte_sz;
	/* set INT info, we don't allow setting legacy bit offset/size */
	*(__u32 *)(t + 1) = (encoding << 24) | (byte_sz * 8);

	return btf_commit_type(btf, sz);
}

/* it's completely legal to append BTF types with type IDs pointing forward to
 * types that haven't been appended yet, so we only make sure that id looks
 * sane, we can't guarantee that ID will always be valid
 */
static int validate_type_id(int id)
{
	if (id < 0 || id > BTF_MAX_NR_TYPES)
		return -EINVAL;
	return 0;
}

/* generic append function for PTR, TYPEDEF, CONST/VOLATILE/RESTRICT */
static int btf_add_ref_kind(struct btf *btf, int kind, const char *name, int ref_type_id)
{
	struct btf_type *t;
	int sz, name_off = 0;

	if (validate_type_id(ref_type_id))
		return -EINVAL;

	if (btf_ensure_modifiable(btf))
		return -ENOMEM;

	sz = sizeof(struct btf_type);
	t = btf_add_type_mem(btf, sz);
	if (!t)
		return -ENOMEM;

	if (name && name[0]) {
		name_off = btf__add_str(btf, name);
		if (name_off < 0)
			return name_off;
	}

	t->name_off = name_off;
	t->info = btf_type_info(kind, 0, 0);
	t->type = ref_type_id;

	return btf_commit_type(btf, sz);
}

/*
 * Append new BTF_KIND_PTR type with:
 *   - *ref_type_id* - referenced type ID, it might not exist yet;
 * Returns:
 *   - >0, type ID of newly added BTF type;
 *   - <0, on error.
 */
int btf__add_ptr(struct btf *btf, int ref_type_id)
{
	return btf_add_ref_kind(btf, BTF_KIND_PTR, NULL, ref_type_id);
}

/*
 * Append new BTF_KIND_ARRAY type with:
 *   - *index_type_id* - type ID of the type describing array index;
 *   - *elem_type_id* - type ID of the type describing array element;
 *   - *nr_elems* - the size of the array;
 * Returns:
 *   - >0, type ID of newly added BTF type;
 *   - <0, on error.
 */
int btf__add_array(struct btf *btf, int index_type_id, int elem_type_id, __u32 nr_elems)
{
	struct btf_type *t;
	struct btf_array *a;
	int sz;

	if (validate_type_id(index_type_id) || validate_type_id(elem_type_id))
		return -EINVAL;

	if (btf_ensure_modifiable(btf))
		return -ENOMEM;

	sz = sizeof(struct btf_type) + sizeof(struct btf_array);
	t = btf_add_type_mem(btf, sz);
	if (!t)
		return -ENOMEM;

	t->name_off = 0;
	t->info = btf_type_info(BTF_KIND_ARRAY, 0, 0);
	t->size = 0;

	a = btf_array(t);
	a->type = elem_type_id;
	a->index_type = index_type_id;
	a->nelems = nr_elems;

	return btf_commit_type(btf, sz);
}

/* generic STRUCT/UNION append function */
static int btf_add_composite(struct btf *btf, int kind, const char *name, __u32 bytes_sz)
{
	struct btf_type *t;
	int sz, name_off = 0;

	if (btf_ensure_modifiable(btf))
		return -ENOMEM;

	sz = sizeof(struct btf_type);
	t = btf_add_type_mem(btf, sz);
	if (!t)
		return -ENOMEM;

	if (name && name[0]) {
		name_off = btf__add_str(btf, name);
		if (name_off < 0)
			return name_off;
	}

	/* start out with vlen=0 and no kflag; this will be adjusted when
	 * adding each member
	 */
	t->name_off = name_off;
	t->info = btf_type_info(kind, 0, 0);
	t->size = bytes_sz;

	return btf_commit_type(btf, sz);
}

/*
 * Append new BTF_KIND_STRUCT type with:
 *   - *name* - name of the struct, can be NULL or empty for anonymous structs;
 *   - *byte_sz* - size of the struct, in bytes;
 *
 * Struct initially has no fields in it. Fields can be added by
 * btf__add_field() right after btf__add_struct() succeeds. 
 *
 * Returns:
 *   - >0, type ID of newly added BTF type;
 *   - <0, on error.
 */
int btf__add_struct(struct btf *btf, const char *name, __u32 byte_sz)
{
	return btf_add_composite(btf, BTF_KIND_STRUCT, name, byte_sz);
}

/*
 * Append new BTF_KIND_UNION type with:
 *   - *name* - name of the union, can be NULL or empty for anonymous union;
 *   - *byte_sz* - size of the union, in bytes;
 *
 * Union initially has no fields in it. Fields can be added by
 * btf__add_field() right after btf__add_union() succeeds. All fields
 * should have *bit_offset* of 0.
 *
 * Returns:
 *   - >0, type ID of newly added BTF type;
 *   - <0, on error.
 */
int btf__add_union(struct btf *btf, const char *name, __u32 byte_sz)
{
	return btf_add_composite(btf, BTF_KIND_UNION, name, byte_sz);
}

static struct btf_type *btf_last_type(struct btf *btf)
{
	return btf_type_by_id(btf, btf__get_nr_types(btf));
}

/*
 * Append new field for the current STRUCT/UNION type with:
 *   - *name* - name of the field, can be NULL or empty for anonymous field;
 *   - *type_id* - type ID for the type describing field type;
 *   - *bit_offset* - bit offset of the start of the field within struct/union;
 *   - *bit_size* - bit size of a bitfield, 0 for non-bitfield fields;
 * Returns:
 *   -  0, on success;
 *   - <0, on error.
 */
int btf__add_field(struct btf *btf, const char *name, int type_id,
		   __u32 bit_offset, __u32 bit_size)
{
	struct btf_type *t;
	struct btf_member *m;
	bool is_bitfield;
	int sz, name_off = 0;

	/* last type should be union/struct */
	if (btf->nr_types == 0)
		return -EINVAL;
	t = btf_last_type(btf);
	if (!btf_is_composite(t))
		return -EINVAL;

	if (validate_type_id(type_id))
		return -EINVAL;
	/* best-effort bit field offset/size enforcement */
	is_bitfield = bit_size || (bit_offset % 8 != 0);
	if (is_bitfield && (bit_size == 0 || bit_size > 255 || bit_offset > 0xffffff))
		return -EINVAL;

	/* only offset 0 is allowed for unions */
	if (btf_is_union(t) && bit_offset)
		return -EINVAL;

	/* decompose and invalidate raw data */
	if (btf_ensure_modifiable(btf))
		return -ENOMEM;

	sz = sizeof(struct btf_member);
	m = btf_add_type_mem(btf, sz);
	if (!m)
		return -ENOMEM;

	if (name && name[0]) {
		name_off = btf__add_str(btf, name);
		if (name_off < 0)
			return name_off;
	}

	m->name_off = name_off;
	m->type = type_id;
	m->offset = bit_offset | (bit_size << 24);

	/* btf_add_type_mem can invalidate t pointer */
	t = btf_last_type(btf);
	/* update parent type's vlen and kflag */
	t->info = btf_type_info(btf_kind(t), btf_vlen(t) + 1, is_bitfield || btf_kflag(t));

	btf->hdr->type_len += sz;
	btf->hdr->str_off += sz;
	return 0;
}

/*
 * Append new BTF_KIND_ENUM type with:
 *   - *name* - name of the enum, can be NULL or empty for anonymous enums;
 *   - *byte_sz* - size of the enum, in bytes.
 *
 * Enum initially has no enum values in it (and corresponds to enum forward
 * declaration). Enumerator values can be added by btf__add_enum_value()
 * immediately after btf__add_enum() succeeds.
 *
 * Returns:
 *   - >0, type ID of newly added BTF type;
 *   - <0, on error.
 */
int btf__add_enum(struct btf *btf, const char *name, __u32 byte_sz)
{
	struct btf_type *t;
	int sz, name_off = 0;

	/* byte_sz must be power of 2 */
	if (!byte_sz || (byte_sz & (byte_sz - 1)) || byte_sz > 8)
		return -EINVAL;

	if (btf_ensure_modifiable(btf))
		return -ENOMEM;

	sz = sizeof(struct btf_type);
	t = btf_add_type_mem(btf, sz);
	if (!t)
		return -ENOMEM;

	if (name && name[0]) {
		name_off = btf__add_str(btf, name);
		if (name_off < 0)
			return name_off;
	}

	/* start out with vlen=0; it will be adjusted when adding enum values */
	t->name_off = name_off;
	t->info = btf_type_info(BTF_KIND_ENUM, 0, 0);
	t->size = byte_sz;

	return btf_commit_type(btf, sz);
}

/*
 * Append new enum value for the current ENUM type with:
 *   - *name* - name of the enumerator value, can't be NULL or empty;
 *   - *value* - integer value corresponding to enum value *name*;
 * Returns:
 *   -  0, on success;
 *   - <0, on error.
 */
int btf__add_enum_value(struct btf *btf, const char *name, __s64 value)
{
	struct btf_type *t;
	struct btf_enum *v;
	int sz, name_off;

	/* last type should be BTF_KIND_ENUM */
	if (btf->nr_types == 0)
		return -EINVAL;
	t = btf_last_type(btf);
	if (!btf_is_enum(t))
		return -EINVAL;

	/* non-empty name */
	if (!name || !name[0])
		return -EINVAL;
	if (value < INT_MIN || value > UINT_MAX)
		return -E2BIG;

	/* decompose and invalidate raw data */
	if (btf_ensure_modifiable(btf))
		return -ENOMEM;

	sz = sizeof(struct btf_enum);
	v = btf_add_type_mem(btf, sz);
	if (!v)
		return -ENOMEM;

	name_off = btf__add_str(btf, name);
	if (name_off < 0)
		return name_off;

	v->name_off = name_off;
	v->val = value;

	/* update parent type's vlen */
	t = btf_last_type(btf);
	btf_type_inc_vlen(t);

	btf->hdr->type_len += sz;
	btf->hdr->str_off += sz;
	return 0;
}

/*
 * Append new BTF_KIND_FWD type with:
 *   - *name*, non-empty/non-NULL name;
 *   - *fwd_kind*, kind of forward declaration, one of BTF_FWD_STRUCT,
 *     BTF_FWD_UNION, or BTF_FWD_ENUM;
 * Returns:
 *   - >0, type ID of newly added BTF type;
 *   - <0, on error.
 */
int btf__add_fwd(struct btf *btf, const char *name, enum btf_fwd_kind fwd_kind)
{
	if (!name || !name[0])
		return -EINVAL;

	switch (fwd_kind) {
	case BTF_FWD_STRUCT:
	case BTF_FWD_UNION: {
		struct btf_type *t;
		int id;

		id = btf_add_ref_kind(btf, BTF_KIND_FWD, name, 0);
		if (id <= 0)
			return id;
		t = btf_type_by_id(btf, id);
		t->info = btf_type_info(BTF_KIND_FWD, 0, fwd_kind == BTF_FWD_UNION);
		return id;
	}
	case BTF_FWD_ENUM:
		/* enum forward in BTF currently is just an enum with no enum
		 * values; we also assume a standard 4-byte size for it
		 */
		return btf__add_enum(btf, name, sizeof(int));
	default:
		return -EINVAL;
	}
}

/*
 * Append new BTF_KING_TYPEDEF type with:
 *   - *name*, non-empty/non-NULL name;
 *   - *ref_type_id* - referenced type ID, it might not exist yet;
 * Returns:
 *   - >0, type ID of newly added BTF type;
 *   - <0, on error.
 */
int btf__add_typedef(struct btf *btf, const char *name, int ref_type_id)
{
	if (!name || !name[0])
		return -EINVAL;

	return btf_add_ref_kind(btf, BTF_KIND_TYPEDEF, name, ref_type_id);
}

/*
 * Append new BTF_KIND_VOLATILE type with:
 *   - *ref_type_id* - referenced type ID, it might not exist yet;
 * Returns:
 *   - >0, type ID of newly added BTF type;
 *   - <0, on error.
 */
int btf__add_volatile(struct btf *btf, int ref_type_id)
{
	return btf_add_ref_kind(btf, BTF_KIND_VOLATILE, NULL, ref_type_id);
}

/*
 * Append new BTF_KIND_CONST type with:
 *   - *ref_type_id* - referenced type ID, it might not exist yet;
 * Returns:
 *   - >0, type ID of newly added BTF type;
 *   - <0, on error.
 */
int btf__add_const(struct btf *btf, int ref_type_id)
{
	return btf_add_ref_kind(btf, BTF_KIND_CONST, NULL, ref_type_id);
}

/*
 * Append new BTF_KIND_RESTRICT type with:
 *   - *ref_type_id* - referenced type ID, it might not exist yet;
 * Returns:
 *   - >0, type ID of newly added BTF type;
 *   - <0, on error.
 */
int btf__add_restrict(struct btf *btf, int ref_type_id)
{
	return btf_add_ref_kind(btf, BTF_KIND_RESTRICT, NULL, ref_type_id);
}

/*
 * Append new BTF_KIND_FUNC type with:
 *   - *name*, non-empty/non-NULL name;
 *   - *proto_type_id* - FUNC_PROTO's type ID, it might not exist yet;
 * Returns:
 *   - >0, type ID of newly added BTF type;
 *   - <0, on error.
 */
int btf__add_func(struct btf *btf, const char *name,
		  enum btf_func_linkage linkage, int proto_type_id)
{
	int id;

	if (!name || !name[0])
		return -EINVAL;
	if (linkage != BTF_FUNC_STATIC && linkage != BTF_FUNC_GLOBAL &&
	    linkage != BTF_FUNC_EXTERN)
		return -EINVAL;

	id = btf_add_ref_kind(btf, BTF_KIND_FUNC, name, proto_type_id);
	if (id > 0) {
		struct btf_type *t = btf_type_by_id(btf, id);

		t->info = btf_type_info(BTF_KIND_FUNC, linkage, 0);
	}
	return id;
}

/*
 * Append new BTF_KIND_FUNC_PROTO with:
 *   - *ret_type_id* - type ID for return result of a function.
 *
 * Function prototype initially has no arguments, but they can be added by
 * btf__add_func_param() one by one, immediately after
 * btf__add_func_proto() succeeded.
 *
 * Returns:
 *   - >0, type ID of newly added BTF type;
 *   - <0, on error.
 */
int btf__add_func_proto(struct btf *btf, int ret_type_id)
{
	struct btf_type *t;
	int sz;

	if (validate_type_id(ret_type_id))
		return -EINVAL;

	if (btf_ensure_modifiable(btf))
		return -ENOMEM;

	sz = sizeof(struct btf_type);
	t = btf_add_type_mem(btf, sz);
	if (!t)
		return -ENOMEM;

	/* start out with vlen=0; this will be adjusted when adding enum
	 * values, if necessary
	 */
	t->name_off = 0;
	t->info = btf_type_info(BTF_KIND_FUNC_PROTO, 0, 0);
	t->type = ret_type_id;

	return btf_commit_type(btf, sz);
}

/*
 * Append new function parameter for current FUNC_PROTO type with:
 *   - *name* - parameter name, can be NULL or empty;
 *   - *type_id* - type ID describing the type of the parameter.
 * Returns:
 *   -  0, on success;
 *   - <0, on error.
 */
int btf__add_func_param(struct btf *btf, const char *name, int type_id)
{
	struct btf_type *t;
	struct btf_param *p;
	int sz, name_off = 0;

	if (validate_type_id(type_id))
		return -EINVAL;

	/* last type should be BTF_KIND_FUNC_PROTO */
	if (btf->nr_types == 0)
		return -EINVAL;
	t = btf_last_type(btf);
	if (!btf_is_func_proto(t))
		return -EINVAL;

	/* decompose and invalidate raw data */
	if (btf_ensure_modifiable(btf))
		return -ENOMEM;

	sz = sizeof(struct btf_param);
	p = btf_add_type_mem(btf, sz);
	if (!p)
		return -ENOMEM;

	if (name && name[0]) {
		name_off = btf__add_str(btf, name);
		if (name_off < 0)
			return name_off;
	}

	p->name_off = name_off;
	p->type = type_id;

	/* update parent type's vlen */
	t = btf_last_type(btf);
	btf_type_inc_vlen(t);

	btf->hdr->type_len += sz;
	btf->hdr->str_off += sz;
	return 0;
}

/*
 * Append new BTF_KIND_VAR type with:
 *   - *name* - non-empty/non-NULL name;
 *   - *linkage* - variable linkage, one of BTF_VAR_STATIC,
 *     BTF_VAR_GLOBAL_ALLOCATED, or BTF_VAR_GLOBAL_EXTERN;
 *   - *type_id* - type ID of the type describing the type of the variable.
 * Returns:
 *   - >0, type ID of newly added BTF type;
 *   - <0, on error.
 */
int btf__add_var(struct btf *btf, const char *name, int linkage, int type_id)
{
	struct btf_type *t;
	struct btf_var *v;
	int sz, name_off;

	/* non-empty name */
	if (!name || !name[0])
		return -EINVAL;
	if (linkage != BTF_VAR_STATIC && linkage != BTF_VAR_GLOBAL_ALLOCATED &&
	    linkage != BTF_VAR_GLOBAL_EXTERN)
		return -EINVAL;
	if (validate_type_id(type_id))
		return -EINVAL;

	/* deconstruct BTF, if necessary, and invalidate raw_data */
	if (btf_ensure_modifiable(btf))
		return -ENOMEM;

	sz = sizeof(struct btf_type) + sizeof(struct btf_var);
	t = btf_add_type_mem(btf, sz);
	if (!t)
		return -ENOMEM;

	name_off = btf__add_str(btf, name);
	if (name_off < 0)
		return name_off;

	t->name_off = name_off;
	t->info = btf_type_info(BTF_KIND_VAR, 0, 0);
	t->type = type_id;

	v = btf_var(t);
	v->linkage = linkage;

	return btf_commit_type(btf, sz);
}

/*
 * Append new BTF_KIND_DATASEC type with:
 *   - *name* - non-empty/non-NULL name;
 *   - *byte_sz* - data section size, in bytes.
 *
 * Data section is initially empty. Variables info can be added with
 * btf__add_datasec_var_info() calls, after btf__add_datasec() succeeds.
 *
 * Returns:
 *   - >0, type ID of newly added BTF type;
 *   - <0, on error.
 */
int btf__add_datasec(struct btf *btf, const char *name, __u32 byte_sz)
{
	struct btf_type *t;
	int sz, name_off;

	/* non-empty name */
	if (!name || !name[0])
		return -EINVAL;

	if (btf_ensure_modifiable(btf))
		return -ENOMEM;

	sz = sizeof(struct btf_type);
	t = btf_add_type_mem(btf, sz);
	if (!t)
		return -ENOMEM;

	name_off = btf__add_str(btf, name);
	if (name_off < 0)
		return name_off;

	/* start with vlen=0, which will be update as var_secinfos are added */
	t->name_off = name_off;
	t->info = btf_type_info(BTF_KIND_DATASEC, 0, 0);
	t->size = byte_sz;

	return btf_commit_type(btf, sz);
}

/*
 * Append new data section variable information entry for current DATASEC type:
 *   - *var_type_id* - type ID, describing type of the variable;
 *   - *offset* - variable offset within data section, in bytes;
 *   - *byte_sz* - variable size, in bytes.
 *
 * Returns:
 *   -  0, on success;
 *   - <0, on error.
 */
int btf__add_datasec_var_info(struct btf *btf, int var_type_id, __u32 offset, __u32 byte_sz)
{
	struct btf_type *t;
	struct btf_var_secinfo *v;
	int sz;

	/* last type should be BTF_KIND_DATASEC */
	if (btf->nr_types == 0)
		return -EINVAL;
	t = btf_last_type(btf);
	if (!btf_is_datasec(t))
		return -EINVAL;

	if (validate_type_id(var_type_id))
		return -EINVAL;

	/* decompose and invalidate raw data */
	if (btf_ensure_modifiable(btf))
		return -ENOMEM;

	sz = sizeof(struct btf_var_secinfo);
	v = btf_add_type_mem(btf, sz);
	if (!v)
		return -ENOMEM;

	v->type = var_type_id;
	v->offset = offset;
	v->size = byte_sz;

	/* update parent type's vlen */
	t = btf_last_type(btf);
	btf_type_inc_vlen(t);

	btf->hdr->type_len += sz;
	btf->hdr->str_off += sz;
	return 0;
}

struct btf_ext_sec_setup_param {
	__u32 off;
	__u32 len;
	__u32 min_rec_size;
	struct btf_ext_info *ext_info;
	const char *desc;
};

static int btf_ext_setup_info(struct btf_ext *btf_ext,
			      struct btf_ext_sec_setup_param *ext_sec)
{
	const struct btf_ext_info_sec *sinfo;
	struct btf_ext_info *ext_info;
	__u32 info_left, record_size;
	/* The start of the info sec (including the __u32 record_size). */
	void *info;

	if (ext_sec->len == 0)
		return 0;

	if (ext_sec->off & 0x03) {
		pr_debug(".BTF.ext %s section is not aligned to 4 bytes\n",
		     ext_sec->desc);
		return -EINVAL;
	}

	info = btf_ext->data + btf_ext->hdr->hdr_len + ext_sec->off;
	info_left = ext_sec->len;

	if (btf_ext->data + btf_ext->data_size < info + ext_sec->len) {
		pr_debug("%s section (off:%u len:%u) is beyond the end of the ELF section .BTF.ext\n",
			 ext_sec->desc, ext_sec->off, ext_sec->len);
		return -EINVAL;
	}

	/* At least a record size */
	if (info_left < sizeof(__u32)) {
		pr_debug(".BTF.ext %s record size not found\n", ext_sec->desc);
		return -EINVAL;
	}

	/* The record size needs to meet the minimum standard */
	record_size = *(__u32 *)info;
	if (record_size < ext_sec->min_rec_size ||
	    record_size & 0x03) {
		pr_debug("%s section in .BTF.ext has invalid record size %u\n",
			 ext_sec->desc, record_size);
		return -EINVAL;
	}

	sinfo = info + sizeof(__u32);
	info_left -= sizeof(__u32);

	/* If no records, return failure now so .BTF.ext won't be used. */
	if (!info_left) {
		pr_debug("%s section in .BTF.ext has no records", ext_sec->desc);
		return -EINVAL;
	}

	while (info_left) {
		unsigned int sec_hdrlen = sizeof(struct btf_ext_info_sec);
		__u64 total_record_size;
		__u32 num_records;

		if (info_left < sec_hdrlen) {
			pr_debug("%s section header is not found in .BTF.ext\n",
			     ext_sec->desc);
			return -EINVAL;
		}

		num_records = sinfo->num_info;
		if (num_records == 0) {
			pr_debug("%s section has incorrect num_records in .BTF.ext\n",
			     ext_sec->desc);
			return -EINVAL;
		}

		total_record_size = sec_hdrlen +
				    (__u64)num_records * record_size;
		if (info_left < total_record_size) {
			pr_debug("%s section has incorrect num_records in .BTF.ext\n",
			     ext_sec->desc);
			return -EINVAL;
		}

		info_left -= total_record_size;
		sinfo = (void *)sinfo + total_record_size;
	}

	ext_info = ext_sec->ext_info;
	ext_info->len = ext_sec->len - sizeof(__u32);
	ext_info->rec_size = record_size;
	ext_info->info = info + sizeof(__u32);

	return 0;
}

static int btf_ext_setup_func_info(struct btf_ext *btf_ext)
{
	struct btf_ext_sec_setup_param param = {
		.off = btf_ext->hdr->func_info_off,
		.len = btf_ext->hdr->func_info_len,
		.min_rec_size = sizeof(struct bpf_func_info_min),
		.ext_info = &btf_ext->func_info,
		.desc = "func_info"
	};

	return btf_ext_setup_info(btf_ext, &param);
}

static int btf_ext_setup_line_info(struct btf_ext *btf_ext)
{
	struct btf_ext_sec_setup_param param = {
		.off = btf_ext->hdr->line_info_off,
		.len = btf_ext->hdr->line_info_len,
		.min_rec_size = sizeof(struct bpf_line_info_min),
		.ext_info = &btf_ext->line_info,
		.desc = "line_info",
	};

	return btf_ext_setup_info(btf_ext, &param);
}

static int btf_ext_setup_core_relos(struct btf_ext *btf_ext)
{
	struct btf_ext_sec_setup_param param = {
		.off = btf_ext->hdr->core_relo_off,
		.len = btf_ext->hdr->core_relo_len,
		.min_rec_size = sizeof(struct bpf_core_relo),
		.ext_info = &btf_ext->core_relo_info,
		.desc = "core_relo",
	};

	return btf_ext_setup_info(btf_ext, &param);
}

static int btf_ext_parse_hdr(__u8 *data, __u32 data_size)
{
	const struct btf_ext_header *hdr = (struct btf_ext_header *)data;

	if (data_size < offsetofend(struct btf_ext_header, hdr_len) ||
	    data_size < hdr->hdr_len) {
		pr_debug("BTF.ext header not found");
		return -EINVAL;
	}

	if (hdr->magic == bswap_16(BTF_MAGIC)) {
		pr_warn("BTF.ext in non-native endianness is not supported\n");
		return -ENOTSUP;
	} else if (hdr->magic != BTF_MAGIC) {
		pr_debug("Invalid BTF.ext magic:%x\n", hdr->magic);
		return -EINVAL;
	}

	if (hdr->version != BTF_VERSION) {
		pr_debug("Unsupported BTF.ext version:%u\n", hdr->version);
		return -ENOTSUP;
	}

	if (hdr->flags) {
		pr_debug("Unsupported BTF.ext flags:%x\n", hdr->flags);
		return -ENOTSUP;
	}

	if (data_size == hdr->hdr_len) {
		pr_debug("BTF.ext has no data\n");
		return -EINVAL;
	}

	return 0;
}

void btf_ext__free(struct btf_ext *btf_ext)
{
	if (IS_ERR_OR_NULL(btf_ext))
		return;
	free(btf_ext->data);
	free(btf_ext);
}

struct btf_ext *btf_ext__new(__u8 *data, __u32 size)
{
	struct btf_ext *btf_ext;
	int err;

	err = btf_ext_parse_hdr(data, size);
	if (err)
		return ERR_PTR(err);

	btf_ext = calloc(1, sizeof(struct btf_ext));
	if (!btf_ext)
		return ERR_PTR(-ENOMEM);

	btf_ext->data_size = size;
	btf_ext->data = malloc(size);
	if (!btf_ext->data) {
		err = -ENOMEM;
		goto done;
	}
	memcpy(btf_ext->data, data, size);

	if (btf_ext->hdr->hdr_len <
	    offsetofend(struct btf_ext_header, line_info_len))
		goto done;
	err = btf_ext_setup_func_info(btf_ext);
	if (err)
		goto done;

	err = btf_ext_setup_line_info(btf_ext);
	if (err)
		goto done;

	if (btf_ext->hdr->hdr_len < offsetofend(struct btf_ext_header, core_relo_len))
		goto done;
	err = btf_ext_setup_core_relos(btf_ext);
	if (err)
		goto done;

done:
	if (err) {
		btf_ext__free(btf_ext);
		return ERR_PTR(err);
	}

	return btf_ext;
}

const void *btf_ext__get_raw_data(const struct btf_ext *btf_ext, __u32 *size)
{
	*size = btf_ext->data_size;
	return btf_ext->data;
}

static int btf_ext_reloc_info(const struct btf *btf,
			      const struct btf_ext_info *ext_info,
			      const char *sec_name, __u32 insns_cnt,
			      void **info, __u32 *cnt)
{
	__u32 sec_hdrlen = sizeof(struct btf_ext_info_sec);
	__u32 i, record_size, existing_len, records_len;
	struct btf_ext_info_sec *sinfo;
	const char *info_sec_name;
	__u64 remain_len;
	void *data;

	record_size = ext_info->rec_size;
	sinfo = ext_info->info;
	remain_len = ext_info->len;
	while (remain_len > 0) {
		records_len = sinfo->num_info * record_size;
		info_sec_name = btf__name_by_offset(btf, sinfo->sec_name_off);
		if (strcmp(info_sec_name, sec_name)) {
			remain_len -= sec_hdrlen + records_len;
			sinfo = (void *)sinfo + sec_hdrlen + records_len;
			continue;
		}

		existing_len = (*cnt) * record_size;
		data = realloc(*info, existing_len + records_len);
		if (!data)
			return -ENOMEM;

		memcpy(data + existing_len, sinfo->data, records_len);
		/* adjust insn_off only, the rest data will be passed
		 * to the kernel.
		 */
		for (i = 0; i < sinfo->num_info; i++) {
			__u32 *insn_off;

			insn_off = data + existing_len + (i * record_size);
			*insn_off = *insn_off / sizeof(struct bpf_insn) +
				insns_cnt;
		}
		*info = data;
		*cnt += sinfo->num_info;
		return 0;
	}

	return -ENOENT;
}

int btf_ext__reloc_func_info(const struct btf *btf,
			     const struct btf_ext *btf_ext,
			     const char *sec_name, __u32 insns_cnt,
			     void **func_info, __u32 *cnt)
{
	return btf_ext_reloc_info(btf, &btf_ext->func_info, sec_name,
				  insns_cnt, func_info, cnt);
}

int btf_ext__reloc_line_info(const struct btf *btf,
			     const struct btf_ext *btf_ext,
			     const char *sec_name, __u32 insns_cnt,
			     void **line_info, __u32 *cnt)
{
	return btf_ext_reloc_info(btf, &btf_ext->line_info, sec_name,
				  insns_cnt, line_info, cnt);
}

__u32 btf_ext__func_info_rec_size(const struct btf_ext *btf_ext)
{
	return btf_ext->func_info.rec_size;
}

__u32 btf_ext__line_info_rec_size(const struct btf_ext *btf_ext)
{
	return btf_ext->line_info.rec_size;
}

struct btf_dedup;

static struct btf_dedup *btf_dedup_new(struct btf *btf, struct btf_ext *btf_ext,
				       const struct btf_dedup_opts *opts);
static void btf_dedup_free(struct btf_dedup *d);
static int btf_dedup_prep(struct btf_dedup *d);
static int btf_dedup_strings(struct btf_dedup *d);
static int btf_dedup_prim_types(struct btf_dedup *d);
static int btf_dedup_struct_types(struct btf_dedup *d);
static int btf_dedup_ref_types(struct btf_dedup *d);
static int btf_dedup_compact_types(struct btf_dedup *d);
static int btf_dedup_remap_types(struct btf_dedup *d);

/*
 * Deduplicate BTF types and strings.
 *
 * BTF dedup algorithm takes as an input `struct btf` representing `.BTF` ELF
 * section with all BTF type descriptors and string data. It overwrites that
 * memory in-place with deduplicated types and strings without any loss of
 * information. If optional `struct btf_ext` representing '.BTF.ext' ELF section
 * is provided, all the strings referenced from .BTF.ext section are honored
 * and updated to point to the right offsets after deduplication.
 *
 * If function returns with error, type/string data might be garbled and should
 * be discarded.
 *
 * More verbose and detailed description of both problem btf_dedup is solving,
 * as well as solution could be found at:
 * https://facebookmicrosites.github.io/bpf/blog/2018/11/14/btf-enhancement.html
 *
 * Problem description and justification
 * =====================================
 *
 * BTF type information is typically emitted either as a result of conversion
 * from DWARF to BTF or directly by compiler. In both cases, each compilation
 * unit contains information about a subset of all the types that are used
 * in an application. These subsets are frequently overlapping and contain a lot
 * of duplicated information when later concatenated together into a single
 * binary. This algorithm ensures that each unique type is represented by single
 * BTF type descriptor, greatly reducing resulting size of BTF data.
 *
 * Compilation unit isolation and subsequent duplication of data is not the only
 * problem. The same type hierarchy (e.g., struct and all the type that struct
 * references) in different compilation units can be represented in BTF to
 * various degrees of completeness (or, rather, incompleteness) due to
 * struct/union forward declarations.
 *
 * Let's take a look at an example, that we'll use to better understand the
 * problem (and solution). Suppose we have two compilation units, each using
 * same `struct S`, but each of them having incomplete type information about
 * struct's fields:
 *
 * // CU #1:
 * struct S;
 * struct A {
 *	int a;
 *	struct A* self;
 *	struct S* parent;
 * };
 * struct B;
 * struct S {
 *	struct A* a_ptr;
 *	struct B* b_ptr;
 * };
 *
 * // CU #2:
 * struct S;
 * struct A;
 * struct B {
 *	int b;
 *	struct B* self;
 *	struct S* parent;
 * };
 * struct S {
 *	struct A* a_ptr;
 *	struct B* b_ptr;
 * };
 *
 * In case of CU #1, BTF data will know only that `struct B` exist (but no
 * more), but will know the complete type information about `struct A`. While
 * for CU #2, it will know full type information about `struct B`, but will
 * only know about forward declaration of `struct A` (in BTF terms, it will
 * have `BTF_KIND_FWD` type descriptor with name `B`).
 *
 * This compilation unit isolation means that it's possible that there is no
 * single CU with complete type information describing structs `S`, `A`, and
 * `B`. Also, we might get tons of duplicated and redundant type information.
 *
 * Additional complication we need to keep in mind comes from the fact that
 * types, in general, can form graphs containing cycles, not just DAGs.
 *
 * While algorithm does deduplication, it also merges and resolves type
 * information (unless disabled throught `struct btf_opts`), whenever possible.
 * E.g., in the example above with two compilation units having partial type
 * information for structs `A` and `B`, the output of algorithm will emit
 * a single copy of each BTF type that describes structs `A`, `B`, and `S`
 * (as well as type information for `int` and pointers), as if they were defined
 * in a single compilation unit as:
 *
 * struct A {
 *	int a;
 *	struct A* self;
 *	struct S* parent;
 * };
 * struct B {
 *	int b;
 *	struct B* self;
 *	struct S* parent;
 * };
 * struct S {
 *	struct A* a_ptr;
 *	struct B* b_ptr;
 * };
 *
 * Algorithm summary
 * =================
 *
 * Algorithm completes its work in 6 separate passes:
 *
 * 1. Strings deduplication.
 * 2. Primitive types deduplication (int, enum, fwd).
 * 3. Struct/union types deduplication.
 * 4. Reference types deduplication (pointers, typedefs, arrays, funcs, func
 *    protos, and const/volatile/restrict modifiers).
 * 5. Types compaction.
 * 6. Types remapping.
 *
 * Algorithm determines canonical type descriptor, which is a single
 * representative type for each truly unique type. This canonical type is the
 * one that will go into final deduplicated BTF type information. For
 * struct/unions, it is also the type that algorithm will merge additional type
 * information into (while resolving FWDs), as it discovers it from data in
 * other CUs. Each input BTF type eventually gets either mapped to itself, if
 * that type is canonical, or to some other type, if that type is equivalent
 * and was chosen as canonical representative. This mapping is stored in
 * `btf_dedup->map` array. This map is also used to record STRUCT/UNION that
 * FWD type got resolved to.
 *
 * To facilitate fast discovery of canonical types, we also maintain canonical
 * index (`btf_dedup->dedup_table`), which maps type descriptor's signature hash
 * (i.e., hashed kind, name, size, fields, etc) into a list of canonical types
 * that match that signature. With sufficiently good choice of type signature
 * hashing function, we can limit number of canonical types for each unique type
 * signature to a very small number, allowing to find canonical type for any
 * duplicated type very quickly.
 *
 * Struct/union deduplication is the most critical part and algorithm for
 * deduplicating structs/unions is described in greater details in comments for
 * `btf_dedup_is_equiv` function.
 */
int btf__dedup(struct btf *btf, struct btf_ext *btf_ext,
	       const struct btf_dedup_opts *opts)
{
	struct btf_dedup *d = btf_dedup_new(btf, btf_ext, opts);
	int err;

	if (IS_ERR(d)) {
		pr_debug("btf_dedup_new failed: %ld", PTR_ERR(d));
		return -EINVAL;
	}

	if (btf_ensure_modifiable(btf))
		return -ENOMEM;

	err = btf_dedup_prep(d);
	if (err) {
		pr_debug("btf_dedup_prep failed:%d\n", err);
		goto done;
	}
	err = btf_dedup_strings(d);
	if (err < 0) {
		pr_debug("btf_dedup_strings failed:%d\n", err);
		goto done;
	}
	err = btf_dedup_prim_types(d);
	if (err < 0) {
		pr_debug("btf_dedup_prim_types failed:%d\n", err);
		goto done;
	}
	err = btf_dedup_struct_types(d);
	if (err < 0) {
		pr_debug("btf_dedup_struct_types failed:%d\n", err);
		goto done;
	}
	err = btf_dedup_ref_types(d);
	if (err < 0) {
		pr_debug("btf_dedup_ref_types failed:%d\n", err);
		goto done;
	}
	err = btf_dedup_compact_types(d);
	if (err < 0) {
		pr_debug("btf_dedup_compact_types failed:%d\n", err);
		goto done;
	}
	err = btf_dedup_remap_types(d);
	if (err < 0) {
		pr_debug("btf_dedup_remap_types failed:%d\n", err);
		goto done;
	}

done:
	btf_dedup_free(d);
	return err;
}

#define BTF_UNPROCESSED_ID ((__u32)-1)
#define BTF_IN_PROGRESS_ID ((__u32)-2)

struct btf_dedup {
	/* .BTF section to be deduped in-place */
	struct btf *btf;
	/*
	 * Optional .BTF.ext section. When provided, any strings referenced
	 * from it will be taken into account when deduping strings
	 */
	struct btf_ext *btf_ext;
	/*
	 * This is a map from any type's signature hash to a list of possible
	 * canonical representative type candidates. Hash collisions are
	 * ignored, so even types of various kinds can share same list of
	 * candidates, which is fine because we rely on subsequent
	 * btf_xxx_equal() checks to authoritatively verify type equality.
	 */
	struct hashmap *dedup_table;
	/* Canonical types map */
	__u32 *map;
	/* Hypothetical mapping, used during type graph equivalence checks */
	__u32 *hypot_map;
	__u32 *hypot_list;
	size_t hypot_cnt;
	size_t hypot_cap;
	/* Whether hypothetical mapping, if successful, would need to adjust
	 * already canonicalized types (due to a new forward declaration to
	 * concrete type resolution). In such case, during split BTF dedup
	 * candidate type would still be considered as different, because base
	 * BTF is considered to be immutable.
	 */
	bool hypot_adjust_canon;
	/* Various option modifying behavior of algorithm */
	struct btf_dedup_opts opts;
	/* temporary strings deduplication state */
	void *strs_data;
	size_t strs_cap;
	size_t strs_len;
	struct hashmap* strs_hash;
};

static long hash_combine(long h, long value)
{
	return h * 31 + value;
}

#define for_each_dedup_cand(d, node, hash) \
	hashmap__for_each_key_entry(d->dedup_table, node, (void *)hash)

static int btf_dedup_table_add(struct btf_dedup *d, long hash, __u32 type_id)
{
	return hashmap__append(d->dedup_table,
			       (void *)hash, (void *)(long)type_id);
}

static int btf_dedup_hypot_map_add(struct btf_dedup *d,
				   __u32 from_id, __u32 to_id)
{
	if (d->hypot_cnt == d->hypot_cap) {
		__u32 *new_list;

		d->hypot_cap += max((size_t)16, d->hypot_cap / 2);
		new_list = libbpf_reallocarray(d->hypot_list, d->hypot_cap, sizeof(__u32));
		if (!new_list)
			return -ENOMEM;
		d->hypot_list = new_list;
	}
	d->hypot_list[d->hypot_cnt++] = from_id;
	d->hypot_map[from_id] = to_id;
	return 0;
}

static void btf_dedup_clear_hypot_map(struct btf_dedup *d)
{
	int i;

	for (i = 0; i < d->hypot_cnt; i++)
		d->hypot_map[d->hypot_list[i]] = BTF_UNPROCESSED_ID;
	d->hypot_cnt = 0;
	d->hypot_adjust_canon = false;
}

static void btf_dedup_free(struct btf_dedup *d)
{
	hashmap__free(d->dedup_table);
	d->dedup_table = NULL;

	free(d->map);
	d->map = NULL;

	free(d->hypot_map);
	d->hypot_map = NULL;

	free(d->hypot_list);
	d->hypot_list = NULL;

	free(d);
}

static size_t btf_dedup_identity_hash_fn(const void *key, void *ctx)
{
	return (size_t)key;
}

static size_t btf_dedup_collision_hash_fn(const void *key, void *ctx)
{
	return 0;
}

static bool btf_dedup_equal_fn(const void *k1, const void *k2, void *ctx)
{
	return k1 == k2;
}

static struct btf_dedup *btf_dedup_new(struct btf *btf, struct btf_ext *btf_ext,
				       const struct btf_dedup_opts *opts)
{
	struct btf_dedup *d = calloc(1, sizeof(struct btf_dedup));
	hashmap_hash_fn hash_fn = btf_dedup_identity_hash_fn;
	int i, err = 0, type_cnt;

	if (!d)
		return ERR_PTR(-ENOMEM);

	d->opts.dont_resolve_fwds = opts && opts->dont_resolve_fwds;
	/* dedup_table_size is now used only to force collisions in tests */
	if (opts && opts->dedup_table_size == 1)
		hash_fn = btf_dedup_collision_hash_fn;

	d->btf = btf;
	d->btf_ext = btf_ext;

	d->dedup_table = hashmap__new(hash_fn, btf_dedup_equal_fn, NULL);
	if (IS_ERR(d->dedup_table)) {
		err = PTR_ERR(d->dedup_table);
		d->dedup_table = NULL;
		goto done;
	}

	type_cnt = btf__get_nr_types(btf) + 1;
	d->map = malloc(sizeof(__u32) * type_cnt);
	if (!d->map) {
		err = -ENOMEM;
		goto done;
	}
	/* special BTF "void" type is made canonical immediately */
	d->map[0] = 0;
	for (i = 1; i < type_cnt; i++) {
		struct btf_type *t = btf_type_by_id(d->btf, i);

		/* VAR and DATASEC are never deduped and are self-canonical */
		if (btf_is_var(t) || btf_is_datasec(t))
			d->map[i] = i;
		else
			d->map[i] = BTF_UNPROCESSED_ID;
	}

	d->hypot_map = malloc(sizeof(__u32) * type_cnt);
	if (!d->hypot_map) {
		err = -ENOMEM;
		goto done;
	}
	for (i = 0; i < type_cnt; i++)
		d->hypot_map[i] = BTF_UNPROCESSED_ID;

done:
	if (err) {
		btf_dedup_free(d);
		return ERR_PTR(err);
	}

	return d;
}

typedef int (*str_off_fn_t)(__u32 *str_off_ptr, void *ctx);

/*
 * Iterate over all possible places in .BTF and .BTF.ext that can reference
 * string and pass pointer to it to a provided callback `fn`.
 */
static int btf_for_each_str_off(struct btf_dedup *d, str_off_fn_t fn, void *ctx)
{
	void *line_data_cur, *line_data_end;
	int i, j, r, rec_size;
	struct btf_type *t;

	for (i = 0; i < d->btf->nr_types; i++) {
		t = btf_type_by_id(d->btf, d->btf->start_id + i);
		r = fn(&t->name_off, ctx);
		if (r)
			return r;

		switch (btf_kind(t)) {
		case BTF_KIND_STRUCT:
		case BTF_KIND_UNION: {
			struct btf_member *m = btf_members(t);
			__u16 vlen = btf_vlen(t);

			for (j = 0; j < vlen; j++) {
				r = fn(&m->name_off, ctx);
				if (r)
					return r;
				m++;
			}
			break;
		}
		case BTF_KIND_ENUM: {
			struct btf_enum *m = btf_enum(t);
			__u16 vlen = btf_vlen(t);

			for (j = 0; j < vlen; j++) {
				r = fn(&m->name_off, ctx);
				if (r)
					return r;
				m++;
			}
			break;
		}
		case BTF_KIND_FUNC_PROTO: {
			struct btf_param *m = btf_params(t);
			__u16 vlen = btf_vlen(t);

			for (j = 0; j < vlen; j++) {
				r = fn(&m->name_off, ctx);
				if (r)
					return r;
				m++;
			}
			break;
		}
		default:
			break;
		}
	}

	if (!d->btf_ext)
		return 0;

	line_data_cur = d->btf_ext->line_info.info;
	line_data_end = d->btf_ext->line_info.info + d->btf_ext->line_info.len;
	rec_size = d->btf_ext->line_info.rec_size;

	while (line_data_cur < line_data_end) {
		struct btf_ext_info_sec *sec = line_data_cur;
		struct bpf_line_info_min *line_info;
		__u32 num_info = sec->num_info;

		r = fn(&sec->sec_name_off, ctx);
		if (r)
			return r;

		line_data_cur += sizeof(struct btf_ext_info_sec);
		for (i = 0; i < num_info; i++) {
			line_info = line_data_cur;
			r = fn(&line_info->file_name_off, ctx);
			if (r)
				return r;
			r = fn(&line_info->line_off, ctx);
			if (r)
				return r;
			line_data_cur += rec_size;
		}
	}

	return 0;
}

static int strs_dedup_remap_str_off(__u32 *str_off_ptr, void *ctx)
{
	struct btf_dedup *d = ctx;
	__u32 str_off = *str_off_ptr;
	long old_off, new_off, len;
	const char *s;
	void *p;
	int err;

	/* don't touch empty string or string in main BTF */
	if (str_off == 0 || str_off < d->btf->start_str_off)
		return 0;

	s = btf__str_by_offset(d->btf, str_off);
	if (d->btf->base_btf) {
		err = btf__find_str(d->btf->base_btf, s);
		if (err >= 0) {
			*str_off_ptr = err;
			return 0;
		}
		if (err != -ENOENT)
			return err;
	}

	len = strlen(s) + 1;

	new_off = d->strs_len;
	p = btf_add_mem(&d->strs_data, &d->strs_cap, 1, new_off, BTF_MAX_STR_OFFSET, len);
	if (!p)
		return -ENOMEM;

	memcpy(p, s, len);

	/* Now attempt to add the string, but only if the string with the same
	 * contents doesn't exist already (HASHMAP_ADD strategy). If such
	 * string exists, we'll get its offset in old_off (that's old_key).
	 */
	err = hashmap__insert(d->strs_hash, (void *)new_off, (void *)new_off,
			      HASHMAP_ADD, (const void **)&old_off, NULL);
	if (err == -EEXIST) {
		*str_off_ptr = d->btf->start_str_off + old_off;
	} else if (err) {
		return err;
	} else {
		*str_off_ptr = d->btf->start_str_off + new_off;
		d->strs_len += len;
	}
	return 0;
}

/*
 * Dedup string and filter out those that are not referenced from either .BTF
 * or .BTF.ext (if provided) sections.
 *
 * This is done by building index of all strings in BTF's string section,
 * then iterating over all entities that can reference strings (e.g., type
 * names, struct field names, .BTF.ext line info, etc) and marking corresponding
 * strings as used. After that all used strings are deduped and compacted into
 * sequential blob of memory and new offsets are calculated. Then all the string
 * references are iterated again and rewritten using new offsets.
 */
static int btf_dedup_strings(struct btf_dedup *d)
{
	char *s;
	int err;

	if (d->btf->strs_deduped)
		return 0;

	/* temporarily switch to use btf_dedup's strs_data for strings for hash
	 * functions; later we'll just transfer hashmap to struct btf as is,
	 * along the strs_data
	 */
	d->btf->strs_data_ptr = &d->strs_data;

	d->strs_hash = hashmap__new(strs_hash_fn, strs_hash_equal_fn, d->btf);
	if (IS_ERR(d->strs_hash)) {
		err = PTR_ERR(d->strs_hash);
		d->strs_hash = NULL;
		goto err_out;
	}

	if (!d->btf->base_btf) {
		s = btf_add_mem(&d->strs_data, &d->strs_cap, 1, d->strs_len, BTF_MAX_STR_OFFSET, 1);
		if (!s)
			return -ENOMEM;
		/* initial empty string */
		s[0] = 0;
		d->strs_len = 1;

		/* insert empty string; we won't be looking it up during strings
		 * dedup, but it's good to have it for generic BTF string lookups
		 */
		err = hashmap__insert(d->strs_hash, (void *)0, (void *)0,
				      HASHMAP_ADD, NULL, NULL);
		if (err)
			goto err_out;
	}

	/* remap string offsets */
	err = btf_for_each_str_off(d, strs_dedup_remap_str_off, d);
	if (err)
		goto err_out;

	/* replace BTF string data and hash with deduped ones */
	free(d->btf->strs_data);
	hashmap__free(d->btf->strs_hash);
	d->btf->strs_data = d->strs_data;
	d->btf->strs_data_cap = d->strs_cap;
	d->btf->hdr->str_len = d->strs_len;
	d->btf->strs_hash = d->strs_hash;
	/* now point strs_data_ptr back to btf->strs_data */
	d->btf->strs_data_ptr = &d->btf->strs_data;

	d->strs_data = d->strs_hash = NULL;
	d->strs_len = d->strs_cap = 0;
	d->btf->strs_deduped = true;
	return 0;

err_out:
	free(d->strs_data);
	hashmap__free(d->strs_hash);
	d->strs_data = d->strs_hash = NULL;
	d->strs_len = d->strs_cap = 0;

	/* restore strings pointer for existing d->btf->strs_hash back */
	d->btf->strs_data_ptr = &d->strs_data;

	return err;
}

static long btf_hash_common(struct btf_type *t)
{
	long h;

	h = hash_combine(0, t->name_off);
	h = hash_combine(h, t->info);
	h = hash_combine(h, t->size);
	return h;
}

static bool btf_equal_common(struct btf_type *t1, struct btf_type *t2)
{
	return t1->name_off == t2->name_off &&
	       t1->info == t2->info &&
	       t1->size == t2->size;
}

/* Calculate type signature hash of INT. */
static long btf_hash_int(struct btf_type *t)
{
	__u32 info = *(__u32 *)(t + 1);
	long h;

	h = btf_hash_common(t);
	h = hash_combine(h, info);
	return h;
}

/* Check structural equality of two INTs. */
static bool btf_equal_int(struct btf_type *t1, struct btf_type *t2)
{
	__u32 info1, info2;

	if (!btf_equal_common(t1, t2))
		return false;
	info1 = *(__u32 *)(t1 + 1);
	info2 = *(__u32 *)(t2 + 1);
	return info1 == info2;
}

/* Calculate type signature hash of ENUM. */
static long btf_hash_enum(struct btf_type *t)
{
	long h;

	/* don't hash vlen and enum members to support enum fwd resolving */
	h = hash_combine(0, t->name_off);
	h = hash_combine(h, t->info & ~0xffff);
	h = hash_combine(h, t->size);
	return h;
}

/* Check structural equality of two ENUMs. */
static bool btf_equal_enum(struct btf_type *t1, struct btf_type *t2)
{
	const struct btf_enum *m1, *m2;
	__u16 vlen;
	int i;

	if (!btf_equal_common(t1, t2))
		return false;

	vlen = btf_vlen(t1);
	m1 = btf_enum(t1);
	m2 = btf_enum(t2);
	for (i = 0; i < vlen; i++) {
		if (m1->name_off != m2->name_off || m1->val != m2->val)
			return false;
		m1++;
		m2++;
	}
	return true;
}

static inline bool btf_is_enum_fwd(struct btf_type *t)
{
	return btf_is_enum(t) && btf_vlen(t) == 0;
}

static bool btf_compat_enum(struct btf_type *t1, struct btf_type *t2)
{
	if (!btf_is_enum_fwd(t1) && !btf_is_enum_fwd(t2))
		return btf_equal_enum(t1, t2);
	/* ignore vlen when comparing */
	return t1->name_off == t2->name_off &&
	       (t1->info & ~0xffff) == (t2->info & ~0xffff) &&
	       t1->size == t2->size;
}

/*
 * Calculate type signature hash of STRUCT/UNION, ignoring referenced type IDs,
 * as referenced type IDs equivalence is established separately during type
 * graph equivalence check algorithm.
 */
static long btf_hash_struct(struct btf_type *t)
{
	const struct btf_member *member = btf_members(t);
	__u32 vlen = btf_vlen(t);
	long h = btf_hash_common(t);
	int i;

	for (i = 0; i < vlen; i++) {
		h = hash_combine(h, member->name_off);
		h = hash_combine(h, member->offset);
		/* no hashing of referenced type ID, it can be unresolved yet */
		member++;
	}
	return h;
}

/*
 * Check structural compatibility of two FUNC_PROTOs, ignoring referenced type
 * IDs. This check is performed during type graph equivalence check and
 * referenced types equivalence is checked separately.
 */
static bool btf_shallow_equal_struct(struct btf_type *t1, struct btf_type *t2)
{
	const struct btf_member *m1, *m2;
	__u16 vlen;
	int i;

	if (!btf_equal_common(t1, t2))
		return false;

	vlen = btf_vlen(t1);
	m1 = btf_members(t1);
	m2 = btf_members(t2);
	for (i = 0; i < vlen; i++) {
		if (m1->name_off != m2->name_off || m1->offset != m2->offset)
			return false;
		m1++;
		m2++;
	}
	return true;
}

/*
 * Calculate type signature hash of ARRAY, including referenced type IDs,
 * under assumption that they were already resolved to canonical type IDs and
 * are not going to change.
 */
static long btf_hash_array(struct btf_type *t)
{
	const struct btf_array *info = btf_array(t);
	long h = btf_hash_common(t);

	h = hash_combine(h, info->type);
	h = hash_combine(h, info->index_type);
	h = hash_combine(h, info->nelems);
	return h;
}

/*
 * Check exact equality of two ARRAYs, taking into account referenced
 * type IDs, under assumption that they were already resolved to canonical
 * type IDs and are not going to change.
 * This function is called during reference types deduplication to compare
 * ARRAY to potential canonical representative.
 */
static bool btf_equal_array(struct btf_type *t1, struct btf_type *t2)
{
	const struct btf_array *info1, *info2;

	if (!btf_equal_common(t1, t2))
		return false;

	info1 = btf_array(t1);
	info2 = btf_array(t2);
	return info1->type == info2->type &&
	       info1->index_type == info2->index_type &&
	       info1->nelems == info2->nelems;
}

/*
 * Check structural compatibility of two ARRAYs, ignoring referenced type
 * IDs. This check is performed during type graph equivalence check and
 * referenced types equivalence is checked separately.
 */
static bool btf_compat_array(struct btf_type *t1, struct btf_type *t2)
{
	if (!btf_equal_common(t1, t2))
		return false;

	return btf_array(t1)->nelems == btf_array(t2)->nelems;
}

/*
 * Calculate type signature hash of FUNC_PROTO, including referenced type IDs,
 * under assumption that they were already resolved to canonical type IDs and
 * are not going to change.
 */
static long btf_hash_fnproto(struct btf_type *t)
{
	const struct btf_param *member = btf_params(t);
	__u16 vlen = btf_vlen(t);
	long h = btf_hash_common(t);
	int i;

	for (i = 0; i < vlen; i++) {
		h = hash_combine(h, member->name_off);
		h = hash_combine(h, member->type);
		member++;
	}
	return h;
}

/*
 * Check exact equality of two FUNC_PROTOs, taking into account referenced
 * type IDs, under assumption that they were already resolved to canonical
 * type IDs and are not going to change.
 * This function is called during reference types deduplication to compare
 * FUNC_PROTO to potential canonical representative.
 */
static bool btf_equal_fnproto(struct btf_type *t1, struct btf_type *t2)
{
	const struct btf_param *m1, *m2;
	__u16 vlen;
	int i;

	if (!btf_equal_common(t1, t2))
		return false;

	vlen = btf_vlen(t1);
	m1 = btf_params(t1);
	m2 = btf_params(t2);
	for (i = 0; i < vlen; i++) {
		if (m1->name_off != m2->name_off || m1->type != m2->type)
			return false;
		m1++;
		m2++;
	}
	return true;
}

/*
 * Check structural compatibility of two FUNC_PROTOs, ignoring referenced type
 * IDs. This check is performed during type graph equivalence check and
 * referenced types equivalence is checked separately.
 */
static bool btf_compat_fnproto(struct btf_type *t1, struct btf_type *t2)
{
	const struct btf_param *m1, *m2;
	__u16 vlen;
	int i;

	/* skip return type ID */
	if (t1->name_off != t2->name_off || t1->info != t2->info)
		return false;

	vlen = btf_vlen(t1);
	m1 = btf_params(t1);
	m2 = btf_params(t2);
	for (i = 0; i < vlen; i++) {
		if (m1->name_off != m2->name_off)
			return false;
		m1++;
		m2++;
	}
	return true;
}

/* Prepare split BTF for deduplication by calculating hashes of base BTF's
 * types and initializing the rest of the state (canonical type mapping) for
 * the fixed base BTF part.
 */
static int btf_dedup_prep(struct btf_dedup *d)
{
	struct btf_type *t;
	int type_id;
	long h;

	if (!d->btf->base_btf)
		return 0;

	for (type_id = 1; type_id < d->btf->start_id; type_id++) {
		t = btf_type_by_id(d->btf, type_id);

		/* all base BTF types are self-canonical by definition */
		d->map[type_id] = type_id;

		switch (btf_kind(t)) {
		case BTF_KIND_VAR:
		case BTF_KIND_DATASEC:
			/* VAR and DATASEC are never hash/deduplicated */
			continue;
		case BTF_KIND_CONST:
		case BTF_KIND_VOLATILE:
		case BTF_KIND_RESTRICT:
		case BTF_KIND_PTR:
		case BTF_KIND_FWD:
		case BTF_KIND_TYPEDEF:
		case BTF_KIND_FUNC:
			h = btf_hash_common(t);
			break;
		case BTF_KIND_INT:
			h = btf_hash_int(t);
			break;
		case BTF_KIND_ENUM:
			h = btf_hash_enum(t);
			break;
		case BTF_KIND_STRUCT:
		case BTF_KIND_UNION:
			h = btf_hash_struct(t);
			break;
		case BTF_KIND_ARRAY:
			h = btf_hash_array(t);
			break;
		case BTF_KIND_FUNC_PROTO:
			h = btf_hash_fnproto(t);
			break;
		default:
			pr_debug("unknown kind %d for type [%d]\n", btf_kind(t), type_id);
			return -EINVAL;
		}
		if (btf_dedup_table_add(d, h, type_id))
			return -ENOMEM;
	}

	return 0;
}

/*
 * Deduplicate primitive types, that can't reference other types, by calculating
 * their type signature hash and comparing them with any possible canonical
 * candidate. If no canonical candidate matches, type itself is marked as
 * canonical and is added into `btf_dedup->dedup_table` as another candidate.
 */
static int btf_dedup_prim_type(struct btf_dedup *d, __u32 type_id)
{
	struct btf_type *t = btf_type_by_id(d->btf, type_id);
	struct hashmap_entry *hash_entry;
	struct btf_type *cand;
	/* if we don't find equivalent type, then we are canonical */
	__u32 new_id = type_id;
	__u32 cand_id;
	long h;

	switch (btf_kind(t)) {
	case BTF_KIND_CONST:
	case BTF_KIND_VOLATILE:
	case BTF_KIND_RESTRICT:
	case BTF_KIND_PTR:
	case BTF_KIND_TYPEDEF:
	case BTF_KIND_ARRAY:
	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION:
	case BTF_KIND_FUNC:
	case BTF_KIND_FUNC_PROTO:
	case BTF_KIND_VAR:
	case BTF_KIND_DATASEC:
		return 0;

	case BTF_KIND_INT:
		h = btf_hash_int(t);
		for_each_dedup_cand(d, hash_entry, h) {
			cand_id = (__u32)(long)hash_entry->value;
			cand = btf_type_by_id(d->btf, cand_id);
			if (btf_equal_int(t, cand)) {
				new_id = cand_id;
				break;
			}
		}
		break;

	case BTF_KIND_ENUM:
		h = btf_hash_enum(t);
		for_each_dedup_cand(d, hash_entry, h) {
			cand_id = (__u32)(long)hash_entry->value;
			cand = btf_type_by_id(d->btf, cand_id);
			if (btf_equal_enum(t, cand)) {
				new_id = cand_id;
				break;
			}
			if (d->opts.dont_resolve_fwds)
				continue;
			if (btf_compat_enum(t, cand)) {
				if (btf_is_enum_fwd(t)) {
					/* resolve fwd to full enum */
					new_id = cand_id;
					break;
				}
				/* resolve canonical enum fwd to full enum */
				d->map[cand_id] = type_id;
			}
		}
		break;

	case BTF_KIND_FWD:
		h = btf_hash_common(t);
		for_each_dedup_cand(d, hash_entry, h) {
			cand_id = (__u32)(long)hash_entry->value;
			cand = btf_type_by_id(d->btf, cand_id);
			if (btf_equal_common(t, cand)) {
				new_id = cand_id;
				break;
			}
		}
		break;

	default:
		return -EINVAL;
	}

	d->map[type_id] = new_id;
	if (type_id == new_id && btf_dedup_table_add(d, h, type_id))
		return -ENOMEM;

	return 0;
}

static int btf_dedup_prim_types(struct btf_dedup *d)
{
	int i, err;

	for (i = 0; i < d->btf->nr_types; i++) {
		err = btf_dedup_prim_type(d, d->btf->start_id + i);
		if (err)
			return err;
	}
	return 0;
}

/*
 * Check whether type is already mapped into canonical one (could be to itself).
 */
static inline bool is_type_mapped(struct btf_dedup *d, uint32_t type_id)
{
	return d->map[type_id] <= BTF_MAX_NR_TYPES;
}

/*
 * Resolve type ID into its canonical type ID, if any; otherwise return original
 * type ID. If type is FWD and is resolved into STRUCT/UNION already, follow
 * STRUCT/UNION link and resolve it into canonical type ID as well.
 */
static inline __u32 resolve_type_id(struct btf_dedup *d, __u32 type_id)
{
	while (is_type_mapped(d, type_id) && d->map[type_id] != type_id)
		type_id = d->map[type_id];
	return type_id;
}

/*
 * Resolve FWD to underlying STRUCT/UNION, if any; otherwise return original
 * type ID.
 */
static uint32_t resolve_fwd_id(struct btf_dedup *d, uint32_t type_id)
{
	__u32 orig_type_id = type_id;

	if (!btf_is_fwd(btf__type_by_id(d->btf, type_id)))
		return type_id;

	while (is_type_mapped(d, type_id) && d->map[type_id] != type_id)
		type_id = d->map[type_id];

	if (!btf_is_fwd(btf__type_by_id(d->btf, type_id)))
		return type_id;

	return orig_type_id;
}


static inline __u16 btf_fwd_kind(struct btf_type *t)
{
	return btf_kflag(t) ? BTF_KIND_UNION : BTF_KIND_STRUCT;
}

/* Check if given two types are identical ARRAY definitions */
static int btf_dedup_identical_arrays(struct btf_dedup *d, __u32 id1, __u32 id2)
{
	struct btf_type *t1, *t2;

	t1 = btf_type_by_id(d->btf, id1);
	t2 = btf_type_by_id(d->btf, id2);
	if (!btf_is_array(t1) || !btf_is_array(t2))
		return 0;

	return btf_equal_array(t1, t2);
}

/*
 * Check equivalence of BTF type graph formed by candidate struct/union (we'll
 * call it "candidate graph" in this description for brevity) to a type graph
 * formed by (potential) canonical struct/union ("canonical graph" for brevity
 * here, though keep in mind that not all types in canonical graph are
 * necessarily canonical representatives themselves, some of them might be
 * duplicates or its uniqueness might not have been established yet).
 * Returns:
 *  - >0, if type graphs are equivalent;
 *  -  0, if not equivalent;
 *  - <0, on error.
 *
 * Algorithm performs side-by-side DFS traversal of both type graphs and checks
 * equivalence of BTF types at each step. If at any point BTF types in candidate
 * and canonical graphs are not compatible structurally, whole graphs are
 * incompatible. If types are structurally equivalent (i.e., all information
 * except referenced type IDs is exactly the same), a mapping from `canon_id` to
 * a `cand_id` is recored in hypothetical mapping (`btf_dedup->hypot_map`).
 * If a type references other types, then those referenced types are checked
 * for equivalence recursively.
 *
 * During DFS traversal, if we find that for current `canon_id` type we
 * already have some mapping in hypothetical map, we check for two possible
 * situations:
 *   - `canon_id` is mapped to exactly the same type as `cand_id`. This will
 *     happen when type graphs have cycles. In this case we assume those two
 *     types are equivalent.
 *   - `canon_id` is mapped to different type. This is contradiction in our
 *     hypothetical mapping, because same graph in canonical graph corresponds
 *     to two different types in candidate graph, which for equivalent type
 *     graphs shouldn't happen. This condition terminates equivalence check
 *     with negative result.
 *
 * If type graphs traversal exhausts types to check and find no contradiction,
 * then type graphs are equivalent.
 *
 * When checking types for equivalence, there is one special case: FWD types.
 * If FWD type resolution is allowed and one of the types (either from canonical
 * or candidate graph) is FWD and other is STRUCT/UNION (depending on FWD's kind
 * flag) and their names match, hypothetical mapping is updated to point from
 * FWD to STRUCT/UNION. If graphs will be determined as equivalent successfully,
 * this mapping will be used to record FWD -> STRUCT/UNION mapping permanently.
 *
 * Technically, this could lead to incorrect FWD to STRUCT/UNION resolution,
 * if there are two exactly named (or anonymous) structs/unions that are
 * compatible structurally, one of which has FWD field, while other is concrete
 * STRUCT/UNION, but according to C sources they are different structs/unions
 * that are referencing different types with the same name. This is extremely
 * unlikely to happen, but btf_dedup API allows to disable FWD resolution if
 * this logic is causing problems.
 *
 * Doing FWD resolution means that both candidate and/or canonical graphs can
 * consists of portions of the graph that come from multiple compilation units.
 * This is due to the fact that types within single compilation unit are always
 * deduplicated and FWDs are already resolved, if referenced struct/union
 * definiton is available. So, if we had unresolved FWD and found corresponding
 * STRUCT/UNION, they will be from different compilation units. This
 * consequently means that when we "link" FWD to corresponding STRUCT/UNION,
 * type graph will likely have at least two different BTF types that describe
 * same type (e.g., most probably there will be two different BTF types for the
 * same 'int' primitive type) and could even have "overlapping" parts of type
 * graph that describe same subset of types.
 *
 * This in turn means that our assumption that each type in canonical graph
 * must correspond to exactly one type in candidate graph might not hold
 * anymore and will make it harder to detect contradictions using hypothetical
 * map. To handle this problem, we allow to follow FWD -> STRUCT/UNION
 * resolution only in canonical graph. FWDs in candidate graphs are never
 * resolved. To see why it's OK, let's check all possible situations w.r.t. FWDs
 * that can occur:
 *   - Both types in canonical and candidate graphs are FWDs. If they are
 *     structurally equivalent, then they can either be both resolved to the
 *     same STRUCT/UNION or not resolved at all. In both cases they are
 *     equivalent and there is no need to resolve FWD on candidate side.
 *   - Both types in canonical and candidate graphs are concrete STRUCT/UNION,
 *     so nothing to resolve as well, algorithm will check equivalence anyway.
 *   - Type in canonical graph is FWD, while type in candidate is concrete
 *     STRUCT/UNION. In this case candidate graph comes from single compilation
 *     unit, so there is exactly one BTF type for each unique C type. After
 *     resolving FWD into STRUCT/UNION, there might be more than one BTF type
 *     in canonical graph mapping to single BTF type in candidate graph, but
 *     because hypothetical mapping maps from canonical to candidate types, it's
 *     alright, and we still maintain the property of having single `canon_id`
 *     mapping to single `cand_id` (there could be two different `canon_id`
 *     mapped to the same `cand_id`, but it's not contradictory).
 *   - Type in canonical graph is concrete STRUCT/UNION, while type in candidate
 *     graph is FWD. In this case we are just going to check compatibility of
 *     STRUCT/UNION and corresponding FWD, and if they are compatible, we'll
 *     assume that whatever STRUCT/UNION FWD resolves to must be equivalent to
 *     a concrete STRUCT/UNION from canonical graph. If the rest of type graphs
 *     turn out equivalent, we'll re-resolve FWD to concrete STRUCT/UNION from
 *     canonical graph.
 */
static int btf_dedup_is_equiv(struct btf_dedup *d, __u32 cand_id,
			      __u32 canon_id)
{
	struct btf_type *cand_type;
	struct btf_type *canon_type;
	__u32 hypot_type_id;
	__u16 cand_kind;
	__u16 canon_kind;
	int i, eq;

	/* if both resolve to the same canonical, they must be equivalent */
	if (resolve_type_id(d, cand_id) == resolve_type_id(d, canon_id))
		return 1;

	canon_id = resolve_fwd_id(d, canon_id);

	hypot_type_id = d->hypot_map[canon_id];
	if (hypot_type_id <= BTF_MAX_NR_TYPES) {
		/* In some cases compiler will generate different DWARF types
		 * for *identical* array type definitions and use them for
		 * different fields within the *same* struct. This breaks type
		 * equivalence check, which makes an assumption that candidate
		 * types sub-graph has a consistent and deduped-by-compiler
		 * types within a single CU. So work around that by explicitly
		 * allowing identical array types here.
		 */
		return hypot_type_id == cand_id ||
		       btf_dedup_identical_arrays(d, hypot_type_id, cand_id);
	}

	if (btf_dedup_hypot_map_add(d, canon_id, cand_id))
		return -ENOMEM;

	cand_type = btf_type_by_id(d->btf, cand_id);
	canon_type = btf_type_by_id(d->btf, canon_id);
	cand_kind = btf_kind(cand_type);
	canon_kind = btf_kind(canon_type);

	if (cand_type->name_off != canon_type->name_off)
		return 0;

	/* FWD <--> STRUCT/UNION equivalence check, if enabled */
	if (!d->opts.dont_resolve_fwds
	    && (cand_kind == BTF_KIND_FWD || canon_kind == BTF_KIND_FWD)
	    && cand_kind != canon_kind) {
		__u16 real_kind;
		__u16 fwd_kind;

		if (cand_kind == BTF_KIND_FWD) {
			real_kind = canon_kind;
			fwd_kind = btf_fwd_kind(cand_type);
		} else {
			real_kind = cand_kind;
			fwd_kind = btf_fwd_kind(canon_type);
			/* we'd need to resolve base FWD to STRUCT/UNION */
			if (fwd_kind == real_kind && canon_id < d->btf->start_id)
				d->hypot_adjust_canon = true;
		}
		return fwd_kind == real_kind;
	}

	if (cand_kind != canon_kind)
		return 0;

	switch (cand_kind) {
	case BTF_KIND_INT:
		return btf_equal_int(cand_type, canon_type);

	case BTF_KIND_ENUM:
		if (d->opts.dont_resolve_fwds)
			return btf_equal_enum(cand_type, canon_type);
		else
			return btf_compat_enum(cand_type, canon_type);

	case BTF_KIND_FWD:
		return btf_equal_common(cand_type, canon_type);

	case BTF_KIND_CONST:
	case BTF_KIND_VOLATILE:
	case BTF_KIND_RESTRICT:
	case BTF_KIND_PTR:
	case BTF_KIND_TYPEDEF:
	case BTF_KIND_FUNC:
		if (cand_type->info != canon_type->info)
			return 0;
		return btf_dedup_is_equiv(d, cand_type->type, canon_type->type);

	case BTF_KIND_ARRAY: {
		const struct btf_array *cand_arr, *canon_arr;

		if (!btf_compat_array(cand_type, canon_type))
			return 0;
		cand_arr = btf_array(cand_type);
		canon_arr = btf_array(canon_type);
		eq = btf_dedup_is_equiv(d, cand_arr->index_type, canon_arr->index_type);
		if (eq <= 0)
			return eq;
		return btf_dedup_is_equiv(d, cand_arr->type, canon_arr->type);
	}

	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION: {
		const struct btf_member *cand_m, *canon_m;
		__u16 vlen;

		if (!btf_shallow_equal_struct(cand_type, canon_type))
			return 0;
		vlen = btf_vlen(cand_type);
		cand_m = btf_members(cand_type);
		canon_m = btf_members(canon_type);
		for (i = 0; i < vlen; i++) {
			eq = btf_dedup_is_equiv(d, cand_m->type, canon_m->type);
			if (eq <= 0)
				return eq;
			cand_m++;
			canon_m++;
		}

		return 1;
	}

	case BTF_KIND_FUNC_PROTO: {
		const struct btf_param *cand_p, *canon_p;
		__u16 vlen;

		if (!btf_compat_fnproto(cand_type, canon_type))
			return 0;
		eq = btf_dedup_is_equiv(d, cand_type->type, canon_type->type);
		if (eq <= 0)
			return eq;
		vlen = btf_vlen(cand_type);
		cand_p = btf_params(cand_type);
		canon_p = btf_params(canon_type);
		for (i = 0; i < vlen; i++) {
			eq = btf_dedup_is_equiv(d, cand_p->type, canon_p->type);
			if (eq <= 0)
				return eq;
			cand_p++;
			canon_p++;
		}
		return 1;
	}

	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * Use hypothetical mapping, produced by successful type graph equivalence
 * check, to augment existing struct/union canonical mapping, where possible.
 *
 * If BTF_KIND_FWD resolution is allowed, this mapping is also used to record
 * FWD -> STRUCT/UNION correspondence as well. FWD resolution is bidirectional:
 * it doesn't matter if FWD type was part of canonical graph or candidate one,
 * we are recording the mapping anyway. As opposed to carefulness required
 * for struct/union correspondence mapping (described below), for FWD resolution
 * it's not important, as by the time that FWD type (reference type) will be
 * deduplicated all structs/unions will be deduped already anyway.
 *
 * Recording STRUCT/UNION mapping is purely a performance optimization and is
 * not required for correctness. It needs to be done carefully to ensure that
 * struct/union from candidate's type graph is not mapped into corresponding
 * struct/union from canonical type graph that itself hasn't been resolved into
 * canonical representative. The only guarantee we have is that canonical
 * struct/union was determined as canonical and that won't change. But any
 * types referenced through that struct/union fields could have been not yet
 * resolved, so in case like that it's too early to establish any kind of
 * correspondence between structs/unions.
 *
 * No canonical correspondence is derived for primitive types (they are already
 * deduplicated completely already anyway) or reference types (they rely on
 * stability of struct/union canonical relationship for equivalence checks).
 */
static void btf_dedup_merge_hypot_map(struct btf_dedup *d)
{
	__u32 canon_type_id, targ_type_id;
	__u16 t_kind, c_kind;
	__u32 t_id, c_id;
	int i;

	for (i = 0; i < d->hypot_cnt; i++) {
		canon_type_id = d->hypot_list[i];
		targ_type_id = d->hypot_map[canon_type_id];
		t_id = resolve_type_id(d, targ_type_id);
		c_id = resolve_type_id(d, canon_type_id);
		t_kind = btf_kind(btf__type_by_id(d->btf, t_id));
		c_kind = btf_kind(btf__type_by_id(d->btf, c_id));
		/*
		 * Resolve FWD into STRUCT/UNION.
		 * It's ok to resolve FWD into STRUCT/UNION that's not yet
		 * mapped to canonical representative (as opposed to
		 * STRUCT/UNION <--> STRUCT/UNION mapping logic below), because
		 * eventually that struct is going to be mapped and all resolved
		 * FWDs will automatically resolve to correct canonical
		 * representative. This will happen before ref type deduping,
		 * which critically depends on stability of these mapping. This
		 * stability is not a requirement for STRUCT/UNION equivalence
		 * checks, though.
		 */

		/* if it's the split BTF case, we still need to point base FWD
		 * to STRUCT/UNION in a split BTF, because FWDs from split BTF
		 * will be resolved against base FWD. If we don't point base
		 * canonical FWD to the resolved STRUCT/UNION, then all the
		 * FWDs in split BTF won't be correctly resolved to a proper
		 * STRUCT/UNION.
		 */
		if (t_kind != BTF_KIND_FWD && c_kind == BTF_KIND_FWD)
			d->map[c_id] = t_id;

		/* if graph equivalence determined that we'd need to adjust
		 * base canonical types, then we need to only point base FWDs
		 * to STRUCTs/UNIONs and do no more modifications. For all
		 * other purposes the type graphs were not equivalent.
		 */
		if (d->hypot_adjust_canon)
			continue;
		
		if (t_kind == BTF_KIND_FWD && c_kind != BTF_KIND_FWD)
			d->map[t_id] = c_id;

		if ((t_kind == BTF_KIND_STRUCT || t_kind == BTF_KIND_UNION) &&
		    c_kind != BTF_KIND_FWD &&
		    is_type_mapped(d, c_id) &&
		    !is_type_mapped(d, t_id)) {
			/*
			 * as a perf optimization, we can map struct/union
			 * that's part of type graph we just verified for
			 * equivalence. We can do that for struct/union that has
			 * canonical representative only, though.
			 */
			d->map[t_id] = c_id;
		}
	}
}

/*
 * Deduplicate struct/union types.
 *
 * For each struct/union type its type signature hash is calculated, taking
 * into account type's name, size, number, order and names of fields, but
 * ignoring type ID's referenced from fields, because they might not be deduped
 * completely until after reference types deduplication phase. This type hash
 * is used to iterate over all potential canonical types, sharing same hash.
 * For each canonical candidate we check whether type graphs that they form
 * (through referenced types in fields and so on) are equivalent using algorithm
 * implemented in `btf_dedup_is_equiv`. If such equivalence is found and
 * BTF_KIND_FWD resolution is allowed, then hypothetical mapping
 * (btf_dedup->hypot_map) produced by aforementioned type graph equivalence
 * algorithm is used to record FWD -> STRUCT/UNION mapping. It's also used to
 * potentially map other structs/unions to their canonical representatives,
 * if such relationship hasn't yet been established. This speeds up algorithm
 * by eliminating some of the duplicate work.
 *
 * If no matching canonical representative was found, struct/union is marked
 * as canonical for itself and is added into btf_dedup->dedup_table hash map
 * for further look ups.
 */
static int btf_dedup_struct_type(struct btf_dedup *d, __u32 type_id)
{
	struct btf_type *cand_type, *t;
	struct hashmap_entry *hash_entry;
	/* if we don't find equivalent type, then we are canonical */
	__u32 new_id = type_id;
	__u16 kind;
	long h;

	/* already deduped or is in process of deduping (loop detected) */
	if (d->map[type_id] <= BTF_MAX_NR_TYPES)
		return 0;

	t = btf_type_by_id(d->btf, type_id);
	kind = btf_kind(t);

	if (kind != BTF_KIND_STRUCT && kind != BTF_KIND_UNION)
		return 0;

	h = btf_hash_struct(t);
	for_each_dedup_cand(d, hash_entry, h) {
		__u32 cand_id = (__u32)(long)hash_entry->value;
		int eq;

		/*
		 * Even though btf_dedup_is_equiv() checks for
		 * btf_shallow_equal_struct() internally when checking two
		 * structs (unions) for equivalence, we need to guard here
		 * from picking matching FWD type as a dedup candidate.
		 * This can happen due to hash collision. In such case just
		 * relying on btf_dedup_is_equiv() would lead to potentially
		 * creating a loop (FWD -> STRUCT and STRUCT -> FWD), because
		 * FWD and compatible STRUCT/UNION are considered equivalent.
		 */
		cand_type = btf_type_by_id(d->btf, cand_id);
		if (!btf_shallow_equal_struct(t, cand_type))
			continue;

		btf_dedup_clear_hypot_map(d);
		eq = btf_dedup_is_equiv(d, type_id, cand_id);
		if (eq < 0)
			return eq;
		if (!eq)
			continue;
		btf_dedup_merge_hypot_map(d);
		if (d->hypot_adjust_canon) /* not really equivalent */
			continue;
		new_id = cand_id;
		break;
	}

	d->map[type_id] = new_id;
	if (type_id == new_id && btf_dedup_table_add(d, h, type_id))
		return -ENOMEM;

	return 0;
}

static int btf_dedup_struct_types(struct btf_dedup *d)
{
	int i, err;

	for (i = 0; i < d->btf->nr_types; i++) {
		err = btf_dedup_struct_type(d, d->btf->start_id + i);
		if (err)
			return err;
	}
	return 0;
}

/*
 * Deduplicate reference type.
 *
 * Once all primitive and struct/union types got deduplicated, we can easily
 * deduplicate all other (reference) BTF types. This is done in two steps:
 *
 * 1. Resolve all referenced type IDs into their canonical type IDs. This
 * resolution can be done either immediately for primitive or struct/union types
 * (because they were deduped in previous two phases) or recursively for
 * reference types. Recursion will always terminate at either primitive or
 * struct/union type, at which point we can "unwind" chain of reference types
 * one by one. There is no danger of encountering cycles because in C type
 * system the only way to form type cycle is through struct/union, so any chain
 * of reference types, even those taking part in a type cycle, will inevitably
 * reach struct/union at some point.
 *
 * 2. Once all referenced type IDs are resolved into canonical ones, BTF type
 * becomes "stable", in the sense that no further deduplication will cause
 * any changes to it. With that, it's now possible to calculate type's signature
 * hash (this time taking into account referenced type IDs) and loop over all
 * potential canonical representatives. If no match was found, current type
 * will become canonical representative of itself and will be added into
 * btf_dedup->dedup_table as another possible canonical representative.
 */
static int btf_dedup_ref_type(struct btf_dedup *d, __u32 type_id)
{
	struct hashmap_entry *hash_entry;
	__u32 new_id = type_id, cand_id;
	struct btf_type *t, *cand;
	/* if we don't find equivalent type, then we are representative type */
	int ref_type_id;
	long h;

	if (d->map[type_id] == BTF_IN_PROGRESS_ID)
		return -ELOOP;
	if (d->map[type_id] <= BTF_MAX_NR_TYPES)
		return resolve_type_id(d, type_id);

	t = btf_type_by_id(d->btf, type_id);
	d->map[type_id] = BTF_IN_PROGRESS_ID;

	switch (btf_kind(t)) {
	case BTF_KIND_CONST:
	case BTF_KIND_VOLATILE:
	case BTF_KIND_RESTRICT:
	case BTF_KIND_PTR:
	case BTF_KIND_TYPEDEF:
	case BTF_KIND_FUNC:
		ref_type_id = btf_dedup_ref_type(d, t->type);
		if (ref_type_id < 0)
			return ref_type_id;
		t->type = ref_type_id;

		h = btf_hash_common(t);
		for_each_dedup_cand(d, hash_entry, h) {
			cand_id = (__u32)(long)hash_entry->value;
			cand = btf_type_by_id(d->btf, cand_id);
			if (btf_equal_common(t, cand)) {
				new_id = cand_id;
				break;
			}
		}
		break;

	case BTF_KIND_ARRAY: {
		struct btf_array *info = btf_array(t);

		ref_type_id = btf_dedup_ref_type(d, info->type);
		if (ref_type_id < 0)
			return ref_type_id;
		info->type = ref_type_id;

		ref_type_id = btf_dedup_ref_type(d, info->index_type);
		if (ref_type_id < 0)
			return ref_type_id;
		info->index_type = ref_type_id;

		h = btf_hash_array(t);
		for_each_dedup_cand(d, hash_entry, h) {
			cand_id = (__u32)(long)hash_entry->value;
			cand = btf_type_by_id(d->btf, cand_id);
			if (btf_equal_array(t, cand)) {
				new_id = cand_id;
				break;
			}
		}
		break;
	}

	case BTF_KIND_FUNC_PROTO: {
		struct btf_param *param;
		__u16 vlen;
		int i;

		ref_type_id = btf_dedup_ref_type(d, t->type);
		if (ref_type_id < 0)
			return ref_type_id;
		t->type = ref_type_id;

		vlen = btf_vlen(t);
		param = btf_params(t);
		for (i = 0; i < vlen; i++) {
			ref_type_id = btf_dedup_ref_type(d, param->type);
			if (ref_type_id < 0)
				return ref_type_id;
			param->type = ref_type_id;
			param++;
		}

		h = btf_hash_fnproto(t);
		for_each_dedup_cand(d, hash_entry, h) {
			cand_id = (__u32)(long)hash_entry->value;
			cand = btf_type_by_id(d->btf, cand_id);
			if (btf_equal_fnproto(t, cand)) {
				new_id = cand_id;
				break;
			}
		}
		break;
	}

	default:
		return -EINVAL;
	}

	d->map[type_id] = new_id;
	if (type_id == new_id && btf_dedup_table_add(d, h, type_id))
		return -ENOMEM;

	return new_id;
}

static int btf_dedup_ref_types(struct btf_dedup *d)
{
	int i, err;

	for (i = 0; i < d->btf->nr_types; i++) {
		err = btf_dedup_ref_type(d, d->btf->start_id + i);
		if (err < 0)
			return err;
	}
	/* we won't need d->dedup_table anymore */
	hashmap__free(d->dedup_table);
	d->dedup_table = NULL;
	return 0;
}

/*
 * Compact types.
 *
 * After we established for each type its corresponding canonical representative
 * type, we now can eliminate types that are not canonical and leave only
 * canonical ones layed out sequentially in memory by copying them over
 * duplicates. During compaction btf_dedup->hypot_map array is reused to store
 * a map from original type ID to a new compacted type ID, which will be used
 * during next phase to "fix up" type IDs, referenced from struct/union and
 * reference types.
 */
static int btf_dedup_compact_types(struct btf_dedup *d)
{
	__u32 *new_offs;
	__u32 next_type_id = d->btf->start_id;
	const struct btf_type *t;
	void *p;
	int i, id, len;

	/* we are going to reuse hypot_map to store compaction remapping */
	d->hypot_map[0] = 0;
	/* base BTF types are not renumbered */
	for (id = 1; id < d->btf->start_id; id++)
		d->hypot_map[id] = id;
	for (i = 0, id = d->btf->start_id; i < d->btf->nr_types; i++, id++)
		d->hypot_map[id] = BTF_UNPROCESSED_ID;

	p = d->btf->types_data;

	for (i = 0, id = d->btf->start_id; i < d->btf->nr_types; i++, id++) {
		if (d->map[id] != id)
			continue;

		t = btf__type_by_id(d->btf, id);
		len = btf_type_size(t);
		if (len < 0)
			return len;

		memmove(p, t, len);
		d->hypot_map[id] = next_type_id;
		d->btf->type_offs[next_type_id - d->btf->start_id] = p - d->btf->types_data;
		p += len;
		next_type_id++;
	}

	/* shrink struct btf's internal types index and update btf_header */
	d->btf->nr_types = next_type_id - d->btf->start_id;
	d->btf->type_offs_cap = d->btf->nr_types;
	d->btf->hdr->type_len = p - d->btf->types_data;
	new_offs = libbpf_reallocarray(d->btf->type_offs, d->btf->type_offs_cap,
				       sizeof(*new_offs));
	if (d->btf->type_offs_cap && !new_offs)
		return -ENOMEM;
	d->btf->type_offs = new_offs;
	d->btf->hdr->str_off = d->btf->hdr->type_len;
	d->btf->raw_size = d->btf->hdr->hdr_len + d->btf->hdr->type_len + d->btf->hdr->str_len;
	return 0;
}

/*
 * Figure out final (deduplicated and compacted) type ID for provided original
 * `type_id` by first resolving it into corresponding canonical type ID and
 * then mapping it to a deduplicated type ID, stored in btf_dedup->hypot_map,
 * which is populated during compaction phase.
 */
static int btf_dedup_remap_type_id(struct btf_dedup *d, __u32 type_id)
{
	__u32 resolved_type_id, new_type_id;

	resolved_type_id = resolve_type_id(d, type_id);
	new_type_id = d->hypot_map[resolved_type_id];
	if (new_type_id > BTF_MAX_NR_TYPES)
		return -EINVAL;
	return new_type_id;
}

/*
 * Remap referenced type IDs into deduped type IDs.
 *
 * After BTF types are deduplicated and compacted, their final type IDs may
 * differ from original ones. The map from original to a corresponding
 * deduped type ID is stored in btf_dedup->hypot_map and is populated during
 * compaction phase. During remapping phase we are rewriting all type IDs
 * referenced from any BTF type (e.g., struct fields, func proto args, etc) to
 * their final deduped type IDs.
 */
static int btf_dedup_remap_type(struct btf_dedup *d, __u32 type_id)
{
	struct btf_type *t = btf_type_by_id(d->btf, type_id);
	int i, r;

	switch (btf_kind(t)) {
	case BTF_KIND_INT:
	case BTF_KIND_ENUM:
		break;

	case BTF_KIND_FWD:
	case BTF_KIND_CONST:
	case BTF_KIND_VOLATILE:
	case BTF_KIND_RESTRICT:
	case BTF_KIND_PTR:
	case BTF_KIND_TYPEDEF:
	case BTF_KIND_FUNC:
	case BTF_KIND_VAR:
		r = btf_dedup_remap_type_id(d, t->type);
		if (r < 0)
			return r;
		t->type = r;
		break;

	case BTF_KIND_ARRAY: {
		struct btf_array *arr_info = btf_array(t);

		r = btf_dedup_remap_type_id(d, arr_info->type);
		if (r < 0)
			return r;
		arr_info->type = r;
		r = btf_dedup_remap_type_id(d, arr_info->index_type);
		if (r < 0)
			return r;
		arr_info->index_type = r;
		break;
	}

	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION: {
		struct btf_member *member = btf_members(t);
		__u16 vlen = btf_vlen(t);

		for (i = 0; i < vlen; i++) {
			r = btf_dedup_remap_type_id(d, member->type);
			if (r < 0)
				return r;
			member->type = r;
			member++;
		}
		break;
	}

	case BTF_KIND_FUNC_PROTO: {
		struct btf_param *param = btf_params(t);
		__u16 vlen = btf_vlen(t);

		r = btf_dedup_remap_type_id(d, t->type);
		if (r < 0)
			return r;
		t->type = r;

		for (i = 0; i < vlen; i++) {
			r = btf_dedup_remap_type_id(d, param->type);
			if (r < 0)
				return r;
			param->type = r;
			param++;
		}
		break;
	}

	case BTF_KIND_DATASEC: {
		struct btf_var_secinfo *var = btf_var_secinfos(t);
		__u16 vlen = btf_vlen(t);

		for (i = 0; i < vlen; i++) {
			r = btf_dedup_remap_type_id(d, var->type);
			if (r < 0)
				return r;
			var->type = r;
			var++;
		}
		break;
	}

	default:
		return -EINVAL;
	}

	return 0;
}

static int btf_dedup_remap_types(struct btf_dedup *d)
{
	int i, r;

	for (i = 0; i < d->btf->nr_types; i++) {
		r = btf_dedup_remap_type(d, d->btf->start_id + i);
		if (r < 0)
			return r;
	}
	return 0;
}

/*
 * Probe few well-known locations for vmlinux kernel image and try to load BTF
 * data out of it to use for target BTF.
 */
struct btf *libbpf_find_kernel_btf(void)
{
	struct {
		const char *path_fmt;
		bool raw_btf;
	} locations[] = {
		/* try canonical vmlinux BTF through sysfs first */
		{ "/sys/kernel/btf/vmlinux", true /* raw BTF */ },
		/* fall back to trying to find vmlinux ELF on disk otherwise */
		{ "/boot/vmlinux-%1$s" },
		{ "/lib/modules/%1$s/vmlinux-%1$s" },
		{ "/lib/modules/%1$s/build/vmlinux" },
		{ "/usr/lib/modules/%1$s/kernel/vmlinux" },
		{ "/usr/lib/debug/boot/vmlinux-%1$s" },
		{ "/usr/lib/debug/boot/vmlinux-%1$s.debug" },
		{ "/usr/lib/debug/lib/modules/%1$s/vmlinux" },
	};
	char path[PATH_MAX + 1];
	struct utsname buf;
	struct btf *btf;
	int i;

	uname(&buf);

	for (i = 0; i < ARRAY_SIZE(locations); i++) {
		snprintf(path, PATH_MAX, locations[i].path_fmt, buf.release);

		if (access(path, R_OK))
			continue;

		if (locations[i].raw_btf)
			btf = btf__parse_raw(path);
		else
			btf = btf__parse_elf(path, NULL);

		pr_debug("loading kernel BTF '%s': %ld\n",
			 path, IS_ERR(btf) ? PTR_ERR(btf) : 0);
		if (IS_ERR(btf))
			continue;

		return btf;
	}

	pr_warn("failed to find valid kernel BTF\n");
	return ERR_PTR(-ESRCH);
}
