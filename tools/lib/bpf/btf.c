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
#include "strset.h"

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
	 * and cached again if user calls btf__raw_data(), at which point
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
	 * strset__data(strs_set)-----+
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

	/* only one of strs_data or strs_set can be non-NULL, depending on
	 * whether BTF is in a modifiable state (strs_set is used) or not
	 * (strs_data points inside raw_data)
	 */
	void *strs_data;
	/* a set of unique strings */
	struct strset *strs_set;
	/* whether strings are already deduplicated */
	bool strs_deduped;

	/* whether base_btf should be freed in btf_free for this instance */
	bool owns_base;

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
 * memory to accommodate *add_cnt* new elements, assuming *cur_cnt* elements
 * are already used. At most *max_cnt* elements can be ever allocated.
 * If necessary, memory is reallocated and all existing data is copied over,
 * new pointer to the memory region is stored at *data, new memory region
 * capacity (in number of elements) is stored in *cap.
 * On success, memory pointer to the beginning of unused memory is returned.
 * On error, NULL is returned.
 */
void *libbpf_add_mem(void **data, size_t *cap_cnt, size_t elem_sz,
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
int libbpf_ensure_mem(void **data, size_t *cap_cnt, size_t elem_sz, size_t need_cnt)
{
	void *p;

	if (need_cnt <= *cap_cnt)
		return 0;

	p = libbpf_add_mem(data, cap_cnt, elem_sz, *cap_cnt, SIZE_MAX, need_cnt - *cap_cnt);
	if (!p)
		return -ENOMEM;

	return 0;
}

static void *btf_add_type_offs_mem(struct btf *btf, size_t add_cnt)
{
	return libbpf_add_mem((void **)&btf->type_offs, &btf->type_offs_cap, sizeof(__u32),
			      btf->nr_types, BTF_MAX_NR_TYPES, add_cnt);
}

static int btf_add_type_idx_entry(struct btf *btf, __u32 type_off)
{
	__u32 *p;

	p = btf_add_type_offs_mem(btf, 1);
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
		pr_debug("Invalid BTF magic: %x\n", hdr->magic);
		return -EINVAL;
	}

	if (btf->raw_size < hdr->hdr_len) {
		pr_debug("BTF header len %u larger than data size %u\n",
			 hdr->hdr_len, btf->raw_size);
		return -EINVAL;
	}

	meta_left = btf->raw_size - hdr->hdr_len;
	if (meta_left < (long long)hdr->str_off + hdr->str_len) {
		pr_debug("Invalid BTF total size: %u\n", btf->raw_size);
		return -EINVAL;
	}

	if ((long long)hdr->type_off + hdr->type_len > hdr->str_off) {
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
	case BTF_KIND_FLOAT:
	case BTF_KIND_TYPE_TAG:
		return base_size;
	case BTF_KIND_INT:
		return base_size + sizeof(__u32);
	case BTF_KIND_ENUM:
		return base_size + vlen * sizeof(struct btf_enum);
	case BTF_KIND_ENUM64:
		return base_size + vlen * sizeof(struct btf_enum64);
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
	case BTF_KIND_DECL_TAG:
		return base_size + sizeof(struct btf_decl_tag);
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
	struct btf_enum64 *e64;
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
	case BTF_KIND_FLOAT:
	case BTF_KIND_TYPE_TAG:
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
	case BTF_KIND_ENUM64:
		for (i = 0, e64 = btf_enum64(t); i < vlen; i++, e64++) {
			e64->name_off = bswap_32(e64->name_off);
			e64->val_lo32 = bswap_32(e64->val_lo32);
			e64->val_hi32 = bswap_32(e64->val_hi32);
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
	case BTF_KIND_DECL_TAG:
		btf_decl_tag(t)->component_idx = bswap_32(btf_decl_tag(t)->component_idx);
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

static int btf_validate_str(const struct btf *btf, __u32 str_off, const char *what, __u32 type_id)
{
	const char *s;

	s = btf__str_by_offset(btf, str_off);
	if (!s) {
		pr_warn("btf: type [%u]: invalid %s (string offset %u)\n", type_id, what, str_off);
		return -EINVAL;
	}

	return 0;
}

static int btf_validate_id(const struct btf *btf, __u32 id, __u32 ctx_id)
{
	const struct btf_type *t;

	t = btf__type_by_id(btf, id);
	if (!t) {
		pr_warn("btf: type [%u]: invalid referenced type ID %u\n", ctx_id, id);
		return -EINVAL;
	}

	return 0;
}

static int btf_validate_type(const struct btf *btf, const struct btf_type *t, __u32 id)
{
	__u32 kind = btf_kind(t);
	int err, i, n;

	err = btf_validate_str(btf, t->name_off, "type name", id);
	if (err)
		return err;

	switch (kind) {
	case BTF_KIND_UNKN:
	case BTF_KIND_INT:
	case BTF_KIND_FWD:
	case BTF_KIND_FLOAT:
		break;
	case BTF_KIND_PTR:
	case BTF_KIND_TYPEDEF:
	case BTF_KIND_VOLATILE:
	case BTF_KIND_CONST:
	case BTF_KIND_RESTRICT:
	case BTF_KIND_VAR:
	case BTF_KIND_DECL_TAG:
	case BTF_KIND_TYPE_TAG:
		err = btf_validate_id(btf, t->type, id);
		if (err)
			return err;
		break;
	case BTF_KIND_ARRAY: {
		const struct btf_array *a = btf_array(t);

		err = btf_validate_id(btf, a->type, id);
		err = err ?: btf_validate_id(btf, a->index_type, id);
		if (err)
			return err;
		break;
	}
	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION: {
		const struct btf_member *m = btf_members(t);

		n = btf_vlen(t);
		for (i = 0; i < n; i++, m++) {
			err = btf_validate_str(btf, m->name_off, "field name", id);
			err = err ?: btf_validate_id(btf, m->type, id);
			if (err)
				return err;
		}
		break;
	}
	case BTF_KIND_ENUM: {
		const struct btf_enum *m = btf_enum(t);

		n = btf_vlen(t);
		for (i = 0; i < n; i++, m++) {
			err = btf_validate_str(btf, m->name_off, "enum name", id);
			if (err)
				return err;
		}
		break;
	}
	case BTF_KIND_ENUM64: {
		const struct btf_enum64 *m = btf_enum64(t);

		n = btf_vlen(t);
		for (i = 0; i < n; i++, m++) {
			err = btf_validate_str(btf, m->name_off, "enum name", id);
			if (err)
				return err;
		}
		break;
	}
	case BTF_KIND_FUNC: {
		const struct btf_type *ft;

		err = btf_validate_id(btf, t->type, id);
		if (err)
			return err;
		ft = btf__type_by_id(btf, t->type);
		if (btf_kind(ft) != BTF_KIND_FUNC_PROTO) {
			pr_warn("btf: type [%u]: referenced type [%u] is not FUNC_PROTO\n", id, t->type);
			return -EINVAL;
		}
		break;
	}
	case BTF_KIND_FUNC_PROTO: {
		const struct btf_param *m = btf_params(t);

		n = btf_vlen(t);
		for (i = 0; i < n; i++, m++) {
			err = btf_validate_str(btf, m->name_off, "param name", id);
			err = err ?: btf_validate_id(btf, m->type, id);
			if (err)
				return err;
		}
		break;
	}
	case BTF_KIND_DATASEC: {
		const struct btf_var_secinfo *m = btf_var_secinfos(t);

		n = btf_vlen(t);
		for (i = 0; i < n; i++, m++) {
			err = btf_validate_id(btf, m->type, id);
			if (err)
				return err;
		}
		break;
	}
	default:
		pr_warn("btf: type [%u]: unrecognized kind %u\n", id, kind);
		return -EINVAL;
	}
	return 0;
}

/* Validate basic sanity of BTF. It's intentionally less thorough than
 * kernel's validation and validates only properties of BTF that libbpf relies
 * on to be correct (e.g., valid type IDs, valid string offsets, etc)
 */
static int btf_sanity_check(const struct btf *btf)
{
	const struct btf_type *t;
	__u32 i, n = btf__type_cnt(btf);
	int err;

	for (i = btf->start_id; i < n; i++) {
		t = btf_type_by_id(btf, i);
		err = btf_validate_type(btf, t, i);
		if (err)
			return err;
	}
	return 0;
}

__u32 btf__type_cnt(const struct btf *btf)
{
	return btf->start_id + btf->nr_types;
}

const struct btf *btf__base_btf(const struct btf *btf)
{
	return btf->base_btf;
}

/* internal helper returning non-const pointer to a type */
struct btf_type *btf_type_by_id(const struct btf *btf, __u32 type_id)
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
		return errno = EINVAL, NULL;
	return btf_type_by_id((struct btf *)btf, type_id);
}

static int determine_ptr_size(const struct btf *btf)
{
	static const char * const long_aliases[] = {
		"long",
		"long int",
		"int long",
		"unsigned long",
		"long unsigned",
		"unsigned long int",
		"unsigned int long",
		"long unsigned int",
		"long int unsigned",
		"int unsigned long",
		"int long unsigned",
	};
	const struct btf_type *t;
	const char *name;
	int i, j, n;

	if (btf->base_btf && btf->base_btf->ptr_sz > 0)
		return btf->base_btf->ptr_sz;

	n = btf__type_cnt(btf);
	for (i = 1; i < n; i++) {
		t = btf__type_by_id(btf, i);
		if (!btf_is_int(t))
			continue;

		if (t->size != 4 && t->size != 8)
			continue;

		name = btf__name_by_offset(btf, t->name_off);
		if (!name)
			continue;

		for (j = 0; j < ARRAY_SIZE(long_aliases); j++) {
			if (strcmp(name, long_aliases[j]) == 0)
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
		return libbpf_err(-EINVAL);
	btf->ptr_sz = ptr_sz;
	return 0;
}

static bool is_host_big_endian(void)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return false;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
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
		return libbpf_err(-EINVAL);

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
	for (i = 0; i < MAX_RESOLVE_DEPTH && !btf_type_is_void_or_null(t); i++) {
		switch (btf_kind(t)) {
		case BTF_KIND_INT:
		case BTF_KIND_STRUCT:
		case BTF_KIND_UNION:
		case BTF_KIND_ENUM:
		case BTF_KIND_ENUM64:
		case BTF_KIND_DATASEC:
		case BTF_KIND_FLOAT:
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
		case BTF_KIND_DECL_TAG:
		case BTF_KIND_TYPE_TAG:
			type_id = t->type;
			break;
		case BTF_KIND_ARRAY:
			array = btf_array(t);
			if (nelems && array->nelems > UINT32_MAX / nelems)
				return libbpf_err(-E2BIG);
			nelems *= array->nelems;
			type_id = array->type;
			break;
		default:
			return libbpf_err(-EINVAL);
		}

		t = btf__type_by_id(btf, type_id);
	}

done:
	if (size < 0)
		return libbpf_err(-EINVAL);
	if (nelems && size > UINT32_MAX / nelems)
		return libbpf_err(-E2BIG);

	return nelems * size;
}

int btf__align_of(const struct btf *btf, __u32 id)
{
	const struct btf_type *t = btf__type_by_id(btf, id);
	__u16 kind = btf_kind(t);

	switch (kind) {
	case BTF_KIND_INT:
	case BTF_KIND_ENUM:
	case BTF_KIND_ENUM64:
	case BTF_KIND_FLOAT:
		return min(btf_ptr_sz(btf), (size_t)t->size);
	case BTF_KIND_PTR:
		return btf_ptr_sz(btf);
	case BTF_KIND_TYPEDEF:
	case BTF_KIND_VOLATILE:
	case BTF_KIND_CONST:
	case BTF_KIND_RESTRICT:
	case BTF_KIND_TYPE_TAG:
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
				return libbpf_err(align);
			max_align = max(max_align, align);

			/* if field offset isn't aligned according to field
			 * type's alignment, then struct must be packed
			 */
			if (btf_member_bitfield_size(t, i) == 0 &&
			    (m->offset % (8 * align)) != 0)
				return 1;
		}

		/* if struct/union size isn't a multiple of its alignment,
		 * then struct must be packed
		 */
		if ((t->size % max_align) != 0)
			return 1;

		return max_align;
	}
	default:
		pr_warn("unsupported BTF_KIND:%u\n", btf_kind(t));
		return errno = EINVAL, 0;
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
		return libbpf_err(-EINVAL);

	return type_id;
}

__s32 btf__find_by_name(const struct btf *btf, const char *type_name)
{
	__u32 i, nr_types = btf__type_cnt(btf);

	if (!strcmp(type_name, "void"))
		return 0;

	for (i = 1; i < nr_types; i++) {
		const struct btf_type *t = btf__type_by_id(btf, i);
		const char *name = btf__name_by_offset(btf, t->name_off);

		if (name && !strcmp(type_name, name))
			return i;
	}

	return libbpf_err(-ENOENT);
}

static __s32 btf_find_by_name_kind(const struct btf *btf, int start_id,
				   const char *type_name, __u32 kind)
{
	__u32 i, nr_types = btf__type_cnt(btf);

	if (kind == BTF_KIND_UNKN || !strcmp(type_name, "void"))
		return 0;

	for (i = start_id; i < nr_types; i++) {
		const struct btf_type *t = btf__type_by_id(btf, i);
		const char *name;

		if (btf_kind(t) != kind)
			continue;
		name = btf__name_by_offset(btf, t->name_off);
		if (name && !strcmp(type_name, name))
			return i;
	}

	return libbpf_err(-ENOENT);
}

__s32 btf__find_by_name_kind_own(const struct btf *btf, const char *type_name,
				 __u32 kind)
{
	return btf_find_by_name_kind(btf, btf->start_id, type_name, kind);
}

__s32 btf__find_by_name_kind(const struct btf *btf, const char *type_name,
			     __u32 kind)
{
	return btf_find_by_name_kind(btf, 1, type_name, kind);
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
		strset__free(btf->strs_set);
	}
	free(btf->raw_data);
	free(btf->raw_data_swapped);
	free(btf->type_offs);
	if (btf->owns_base)
		btf__free(btf->base_btf);
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
		btf->start_id = btf__type_cnt(base_btf);
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
	return libbpf_ptr(btf_new_empty(NULL));
}

struct btf *btf__new_empty_split(struct btf *base_btf)
{
	return libbpf_ptr(btf_new_empty(base_btf));
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
	btf->fd = -1;

	if (base_btf) {
		btf->base_btf = base_btf;
		btf->start_id = btf__type_cnt(base_btf);
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
	err = err ?: btf_sanity_check(btf);
	if (err)
		goto done;

done:
	if (err) {
		btf__free(btf);
		return ERR_PTR(err);
	}

	return btf;
}

struct btf *btf__new(const void *data, __u32 size)
{
	return libbpf_ptr(btf_new(data, size, NULL));
}

struct btf *btf__new_split(const void *data, __u32 size, struct btf *base_btf)
{
	return libbpf_ptr(btf_new(data, size, base_btf));
}

struct btf_elf_secs {
	Elf_Data *btf_data;
	Elf_Data *btf_ext_data;
	Elf_Data *btf_base_data;
};

static int btf_find_elf_sections(Elf *elf, const char *path, struct btf_elf_secs *secs)
{
	Elf_Scn *scn = NULL;
	Elf_Data *data;
	GElf_Ehdr ehdr;
	size_t shstrndx;
	int idx = 0;

	if (!gelf_getehdr(elf, &ehdr)) {
		pr_warn("failed to get EHDR from %s\n", path);
		goto err;
	}

	if (elf_getshdrstrndx(elf, &shstrndx)) {
		pr_warn("failed to get section names section index for %s\n",
			path);
		goto err;
	}

	if (!elf_rawdata(elf_getscn(elf, shstrndx), NULL)) {
		pr_warn("failed to get e_shstrndx from %s\n", path);
		goto err;
	}

	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		Elf_Data **field;
		GElf_Shdr sh;
		char *name;

		idx++;
		if (gelf_getshdr(scn, &sh) != &sh) {
			pr_warn("failed to get section(%d) header from %s\n",
				idx, path);
			goto err;
		}
		name = elf_strptr(elf, shstrndx, sh.sh_name);
		if (!name) {
			pr_warn("failed to get section(%d) name from %s\n",
				idx, path);
			goto err;
		}

		if (strcmp(name, BTF_ELF_SEC) == 0)
			field = &secs->btf_data;
		else if (strcmp(name, BTF_EXT_ELF_SEC) == 0)
			field = &secs->btf_ext_data;
		else if (strcmp(name, BTF_BASE_ELF_SEC) == 0)
			field = &secs->btf_base_data;
		else
			continue;

		data = elf_getdata(scn, 0);
		if (!data) {
			pr_warn("failed to get section(%d, %s) data from %s\n",
				idx, name, path);
			goto err;
		}
		*field = data;
	}

	return 0;

err:
	return -LIBBPF_ERRNO__FORMAT;
}

static struct btf *btf_parse_elf(const char *path, struct btf *base_btf,
				 struct btf_ext **btf_ext)
{
	struct btf_elf_secs secs = {};
	struct btf *dist_base_btf = NULL;
	struct btf *btf = NULL;
	int err = 0, fd = -1;
	Elf *elf = NULL;

	if (elf_version(EV_CURRENT) == EV_NONE) {
		pr_warn("failed to init libelf for %s\n", path);
		return ERR_PTR(-LIBBPF_ERRNO__LIBELF);
	}

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		err = -errno;
		pr_warn("failed to open %s: %s\n", path, strerror(errno));
		return ERR_PTR(err);
	}

	elf = elf_begin(fd, ELF_C_READ, NULL);
	if (!elf) {
		pr_warn("failed to open %s as ELF file\n", path);
		goto done;
	}

	err = btf_find_elf_sections(elf, path, &secs);
	if (err)
		goto done;

	if (!secs.btf_data) {
		pr_warn("failed to find '%s' ELF section in %s\n", BTF_ELF_SEC, path);
		err = -ENODATA;
		goto done;
	}

	if (secs.btf_base_data) {
		dist_base_btf = btf_new(secs.btf_base_data->d_buf, secs.btf_base_data->d_size,
					NULL);
		if (IS_ERR(dist_base_btf)) {
			err = PTR_ERR(dist_base_btf);
			dist_base_btf = NULL;
			goto done;
		}
	}

	btf = btf_new(secs.btf_data->d_buf, secs.btf_data->d_size,
		      dist_base_btf ?: base_btf);
	if (IS_ERR(btf)) {
		err = PTR_ERR(btf);
		goto done;
	}
	if (dist_base_btf && base_btf) {
		err = btf__relocate(btf, base_btf);
		if (err)
			goto done;
		btf__free(dist_base_btf);
		dist_base_btf = NULL;
	}

	if (dist_base_btf)
		btf->owns_base = true;

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

	if (btf_ext && secs.btf_ext_data) {
		*btf_ext = btf_ext__new(secs.btf_ext_data->d_buf, secs.btf_ext_data->d_size);
		if (IS_ERR(*btf_ext)) {
			err = PTR_ERR(*btf_ext);
			goto done;
		}
	} else if (btf_ext) {
		*btf_ext = NULL;
	}
done:
	if (elf)
		elf_end(elf);
	close(fd);

	if (!err)
		return btf;

	if (btf_ext)
		btf_ext__free(*btf_ext);
	btf__free(dist_base_btf);
	btf__free(btf);

	return ERR_PTR(err);
}

struct btf *btf__parse_elf(const char *path, struct btf_ext **btf_ext)
{
	return libbpf_ptr(btf_parse_elf(path, NULL, btf_ext));
}

struct btf *btf__parse_elf_split(const char *path, struct btf *base_btf)
{
	return libbpf_ptr(btf_parse_elf(path, base_btf, NULL));
}

static struct btf *btf_parse_raw(const char *path, struct btf *base_btf)
{
	struct btf *btf = NULL;
	void *data = NULL;
	FILE *f = NULL;
	__u16 magic;
	int err = 0;
	long sz;

	f = fopen(path, "rbe");
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
	return libbpf_ptr(btf_parse_raw(path, NULL));
}

struct btf *btf__parse_raw_split(const char *path, struct btf *base_btf)
{
	return libbpf_ptr(btf_parse_raw(path, base_btf));
}

static struct btf *btf_parse(const char *path, struct btf *base_btf, struct btf_ext **btf_ext)
{
	struct btf *btf;
	int err;

	if (btf_ext)
		*btf_ext = NULL;

	btf = btf_parse_raw(path, base_btf);
	err = libbpf_get_error(btf);
	if (!err)
		return btf;
	if (err != -EPROTO)
		return ERR_PTR(err);
	return btf_parse_elf(path, base_btf, btf_ext);
}

struct btf *btf__parse(const char *path, struct btf_ext **btf_ext)
{
	return libbpf_ptr(btf_parse(path, NULL, btf_ext));
}

struct btf *btf__parse_split(const char *path, struct btf *base_btf)
{
	return libbpf_ptr(btf_parse(path, base_btf, NULL));
}

static void *btf_get_raw_data(const struct btf *btf, __u32 *size, bool swap_endian);

int btf_load_into_kernel(struct btf *btf,
			 char *log_buf, size_t log_sz, __u32 log_level,
			 int token_fd)
{
	LIBBPF_OPTS(bpf_btf_load_opts, opts);
	__u32 buf_sz = 0, raw_size;
	char *buf = NULL, *tmp;
	void *raw_data;
	int err = 0;

	if (btf->fd >= 0)
		return libbpf_err(-EEXIST);
	if (log_sz && !log_buf)
		return libbpf_err(-EINVAL);

	/* cache native raw data representation */
	raw_data = btf_get_raw_data(btf, &raw_size, false);
	if (!raw_data) {
		err = -ENOMEM;
		goto done;
	}
	btf->raw_size = raw_size;
	btf->raw_data = raw_data;

retry_load:
	/* if log_level is 0, we won't provide log_buf/log_size to the kernel,
	 * initially. Only if BTF loading fails, we bump log_level to 1 and
	 * retry, using either auto-allocated or custom log_buf. This way
	 * non-NULL custom log_buf provides a buffer just in case, but hopes
	 * for successful load and no need for log_buf.
	 */
	if (log_level) {
		/* if caller didn't provide custom log_buf, we'll keep
		 * allocating our own progressively bigger buffers for BTF
		 * verification log
		 */
		if (!log_buf) {
			buf_sz = max((__u32)BPF_LOG_BUF_SIZE, buf_sz * 2);
			tmp = realloc(buf, buf_sz);
			if (!tmp) {
				err = -ENOMEM;
				goto done;
			}
			buf = tmp;
			buf[0] = '\0';
		}

		opts.log_buf = log_buf ? log_buf : buf;
		opts.log_size = log_buf ? log_sz : buf_sz;
		opts.log_level = log_level;
	}

	opts.token_fd = token_fd;
	if (token_fd)
		opts.btf_flags |= BPF_F_TOKEN_FD;

	btf->fd = bpf_btf_load(raw_data, raw_size, &opts);
	if (btf->fd < 0) {
		/* time to turn on verbose mode and try again */
		if (log_level == 0) {
			log_level = 1;
			goto retry_load;
		}
		/* only retry if caller didn't provide custom log_buf, but
		 * make sure we can never overflow buf_sz
		 */
		if (!log_buf && errno == ENOSPC && buf_sz <= UINT_MAX / 2)
			goto retry_load;

		err = -errno;
		pr_warn("BTF loading error: %d\n", err);
		/* don't print out contents of custom log_buf */
		if (!log_buf && buf[0])
			pr_warn("-- BEGIN BTF LOAD LOG ---\n%s\n-- END BTF LOAD LOG --\n", buf);
	}

done:
	free(buf);
	return libbpf_err(err);
}

int btf__load_into_kernel(struct btf *btf)
{
	return btf_load_into_kernel(btf, NULL, 0, 0, 0);
}

int btf__fd(const struct btf *btf)
{
	return btf->fd;
}

void btf__set_fd(struct btf *btf, int fd)
{
	btf->fd = fd;
}

static const void *btf_strs_data(const struct btf *btf)
{
	return btf->strs_data ? btf->strs_data : strset__data(btf->strs_set);
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

	memcpy(p, btf_strs_data(btf), hdr->str_len);
	p += hdr->str_len;

	*size = data_sz;
	return data;
err_out:
	free(data);
	return NULL;
}

const void *btf__raw_data(const struct btf *btf_ro, __u32 *size)
{
	struct btf *btf = (struct btf *)btf_ro;
	__u32 data_sz;
	void *data;

	data = btf_get_raw_data(btf, &data_sz, btf->swapped_endian);
	if (!data)
		return errno = ENOMEM, NULL;

	btf->raw_size = data_sz;
	if (btf->swapped_endian)
		btf->raw_data_swapped = data;
	else
		btf->raw_data = data;
	*size = data_sz;
	return data;
}

__attribute__((alias("btf__raw_data")))
const void *btf__get_raw_data(const struct btf *btf, __u32 *size);

const char *btf__str_by_offset(const struct btf *btf, __u32 offset)
{
	if (offset < btf->start_str_off)
		return btf__str_by_offset(btf->base_btf, offset);
	else if (offset - btf->start_str_off < btf->hdr->str_len)
		return btf_strs_data(btf) + (offset - btf->start_str_off);
	else
		return errno = EINVAL, NULL;
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

	/* we won't know btf_size until we call bpf_btf_get_info_by_fd(). so
	 * let's start with a sane default - 4KiB here - and resize it only if
	 * bpf_btf_get_info_by_fd() needs a bigger buffer.
	 */
	last_size = 4096;
	ptr = malloc(last_size);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	memset(&btf_info, 0, sizeof(btf_info));
	btf_info.btf = ptr_to_u64(ptr);
	btf_info.btf_size = last_size;
	err = bpf_btf_get_info_by_fd(btf_fd, &btf_info, &len);

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

		err = bpf_btf_get_info_by_fd(btf_fd, &btf_info, &len);
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

struct btf *btf__load_from_kernel_by_id_split(__u32 id, struct btf *base_btf)
{
	struct btf *btf;
	int btf_fd;

	btf_fd = bpf_btf_get_fd_by_id(id);
	if (btf_fd < 0)
		return libbpf_err_ptr(-errno);

	btf = btf_get_from_fd(btf_fd, base_btf);
	close(btf_fd);

	return libbpf_ptr(btf);
}

struct btf *btf__load_from_kernel_by_id(__u32 id)
{
	return btf__load_from_kernel_by_id_split(id, NULL);
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
	void *hdr, *types;
	struct strset *set = NULL;
	int err = -ENOMEM;

	if (btf_is_modifiable(btf)) {
		/* any BTF modification invalidates raw_data */
		btf_invalidate_raw_data(btf);
		return 0;
	}

	/* split raw data into three memory regions */
	hdr = malloc(btf->hdr->hdr_len);
	types = malloc(btf->hdr->type_len);
	if (!hdr || !types)
		goto err_out;

	memcpy(hdr, btf->hdr, btf->hdr->hdr_len);
	memcpy(types, btf->types_data, btf->hdr->type_len);

	/* build lookup index for all strings */
	set = strset__new(BTF_MAX_STR_OFFSET, btf->strs_data, btf->hdr->str_len);
	if (IS_ERR(set)) {
		err = PTR_ERR(set);
		goto err_out;
	}

	/* only when everything was successful, update internal state */
	btf->hdr = hdr;
	btf->types_data = types;
	btf->types_data_cap = btf->hdr->type_len;
	btf->strs_data = NULL;
	btf->strs_set = set;
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
	strset__free(set);
	free(hdr);
	free(types);
	return err;
}

/* Find an offset in BTF string section that corresponds to a given string *s*.
 * Returns:
 *   - >0 offset into string section, if string is found;
 *   - -ENOENT, if string is not in the string section;
 *   - <0, on any other error.
 */
int btf__find_str(struct btf *btf, const char *s)
{
	int off;

	if (btf->base_btf) {
		off = btf__find_str(btf->base_btf, s);
		if (off != -ENOENT)
			return off;
	}

	/* BTF needs to be in a modifiable state to build string lookup index */
	if (btf_ensure_modifiable(btf))
		return libbpf_err(-ENOMEM);

	off = strset__find_str(btf->strs_set, s);
	if (off < 0)
		return libbpf_err(off);

	return btf->start_str_off + off;
}

/* Add a string s to the BTF string section.
 * Returns:
 *   - > 0 offset into string section, on success;
 *   - < 0, on error.
 */
int btf__add_str(struct btf *btf, const char *s)
{
	int off;

	if (btf->base_btf) {
		off = btf__find_str(btf->base_btf, s);
		if (off != -ENOENT)
			return off;
	}

	if (btf_ensure_modifiable(btf))
		return libbpf_err(-ENOMEM);

	off = strset__add_str(btf->strs_set, s);
	if (off < 0)
		return libbpf_err(off);

	btf->hdr->str_len = strset__data_size(btf->strs_set);

	return btf->start_str_off + off;
}

static void *btf_add_type_mem(struct btf *btf, size_t add_sz)
{
	return libbpf_add_mem(&btf->types_data, &btf->types_data_cap, 1,
			      btf->hdr->type_len, UINT_MAX, add_sz);
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
		return libbpf_err(err);

	btf->hdr->type_len += data_sz;
	btf->hdr->str_off += data_sz;
	btf->nr_types++;
	return btf->start_id + btf->nr_types - 1;
}

struct btf_pipe {
	const struct btf *src;
	struct btf *dst;
	struct hashmap *str_off_map; /* map string offsets from src to dst */
};

static int btf_rewrite_str(struct btf_pipe *p, __u32 *str_off)
{
	long mapped_off;
	int off, err;

	if (!*str_off) /* nothing to do for empty strings */
		return 0;

	if (p->str_off_map &&
	    hashmap__find(p->str_off_map, *str_off, &mapped_off)) {
		*str_off = mapped_off;
		return 0;
	}

	off = btf__add_str(p->dst, btf__str_by_offset(p->src, *str_off));
	if (off < 0)
		return off;

	/* Remember string mapping from src to dst.  It avoids
	 * performing expensive string comparisons.
	 */
	if (p->str_off_map) {
		err = hashmap__append(p->str_off_map, *str_off, off);
		if (err)
			return err;
	}

	*str_off = off;
	return 0;
}

static int btf_add_type(struct btf_pipe *p, const struct btf_type *src_type)
{
	struct btf_field_iter it;
	struct btf_type *t;
	__u32 *str_off;
	int sz, err;

	sz = btf_type_size(src_type);
	if (sz < 0)
		return libbpf_err(sz);

	/* deconstruct BTF, if necessary, and invalidate raw_data */
	if (btf_ensure_modifiable(p->dst))
		return libbpf_err(-ENOMEM);

	t = btf_add_type_mem(p->dst, sz);
	if (!t)
		return libbpf_err(-ENOMEM);

	memcpy(t, src_type, sz);

	err = btf_field_iter_init(&it, t, BTF_FIELD_ITER_STRS);
	if (err)
		return libbpf_err(err);

	while ((str_off = btf_field_iter_next(&it))) {
		err = btf_rewrite_str(p, str_off);
		if (err)
			return libbpf_err(err);
	}

	return btf_commit_type(p->dst, sz);
}

int btf__add_type(struct btf *btf, const struct btf *src_btf, const struct btf_type *src_type)
{
	struct btf_pipe p = { .src = src_btf, .dst = btf };

	return btf_add_type(&p, src_type);
}

static size_t btf_dedup_identity_hash_fn(long key, void *ctx);
static bool btf_dedup_equal_fn(long k1, long k2, void *ctx);

int btf__add_btf(struct btf *btf, const struct btf *src_btf)
{
	struct btf_pipe p = { .src = src_btf, .dst = btf };
	int data_sz, sz, cnt, i, err, old_strs_len;
	__u32 *off;
	void *t;

	/* appending split BTF isn't supported yet */
	if (src_btf->base_btf)
		return libbpf_err(-ENOTSUP);

	/* deconstruct BTF, if necessary, and invalidate raw_data */
	if (btf_ensure_modifiable(btf))
		return libbpf_err(-ENOMEM);

	/* remember original strings section size if we have to roll back
	 * partial strings section changes
	 */
	old_strs_len = btf->hdr->str_len;

	data_sz = src_btf->hdr->type_len;
	cnt = btf__type_cnt(src_btf) - 1;

	/* pre-allocate enough memory for new types */
	t = btf_add_type_mem(btf, data_sz);
	if (!t)
		return libbpf_err(-ENOMEM);

	/* pre-allocate enough memory for type offset index for new types */
	off = btf_add_type_offs_mem(btf, cnt);
	if (!off)
		return libbpf_err(-ENOMEM);

	/* Map the string offsets from src_btf to the offsets from btf to improve performance */
	p.str_off_map = hashmap__new(btf_dedup_identity_hash_fn, btf_dedup_equal_fn, NULL);
	if (IS_ERR(p.str_off_map))
		return libbpf_err(-ENOMEM);

	/* bulk copy types data for all types from src_btf */
	memcpy(t, src_btf->types_data, data_sz);

	for (i = 0; i < cnt; i++) {
		struct btf_field_iter it;
		__u32 *type_id, *str_off;

		sz = btf_type_size(t);
		if (sz < 0) {
			/* unlikely, has to be corrupted src_btf */
			err = sz;
			goto err_out;
		}

		/* fill out type ID to type offset mapping for lookups by type ID */
		*off = t - btf->types_data;

		/* add, dedup, and remap strings referenced by this BTF type */
		err = btf_field_iter_init(&it, t, BTF_FIELD_ITER_STRS);
		if (err)
			goto err_out;
		while ((str_off = btf_field_iter_next(&it))) {
			err = btf_rewrite_str(&p, str_off);
			if (err)
				goto err_out;
		}

		/* remap all type IDs referenced from this BTF type */
		err = btf_field_iter_init(&it, t, BTF_FIELD_ITER_IDS);
		if (err)
			goto err_out;

		while ((type_id = btf_field_iter_next(&it))) {
			if (!*type_id) /* nothing to do for VOID references */
				continue;

			/* we haven't updated btf's type count yet, so
			 * btf->start_id + btf->nr_types - 1 is the type ID offset we should
			 * add to all newly added BTF types
			 */
			*type_id += btf->start_id + btf->nr_types - 1;
		}

		/* go to next type data and type offset index entry */
		t += sz;
		off++;
	}

	/* Up until now any of the copied type data was effectively invisible,
	 * so if we exited early before this point due to error, BTF would be
	 * effectively unmodified. There would be extra internal memory
	 * pre-allocated, but it would not be available for querying.  But now
	 * that we've copied and rewritten all the data successfully, we can
	 * update type count and various internal offsets and sizes to
	 * "commit" the changes and made them visible to the outside world.
	 */
	btf->hdr->type_len += data_sz;
	btf->hdr->str_off += data_sz;
	btf->nr_types += cnt;

	hashmap__free(p.str_off_map);

	/* return type ID of the first added BTF type */
	return btf->start_id + btf->nr_types - cnt;
err_out:
	/* zero out preallocated memory as if it was just allocated with
	 * libbpf_add_mem()
	 */
	memset(btf->types_data + btf->hdr->type_len, 0, data_sz);
	memset(btf->strs_data + old_strs_len, 0, btf->hdr->str_len - old_strs_len);

	/* and now restore original strings section size; types data size
	 * wasn't modified, so doesn't need restoring, see big comment above
	 */
	btf->hdr->str_len = old_strs_len;

	hashmap__free(p.str_off_map);

	return libbpf_err(err);
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
		return libbpf_err(-EINVAL);
	/* byte_sz must be power of 2 */
	if (!byte_sz || (byte_sz & (byte_sz - 1)) || byte_sz > 16)
		return libbpf_err(-EINVAL);
	if (encoding & ~(BTF_INT_SIGNED | BTF_INT_CHAR | BTF_INT_BOOL))
		return libbpf_err(-EINVAL);

	/* deconstruct BTF, if necessary, and invalidate raw_data */
	if (btf_ensure_modifiable(btf))
		return libbpf_err(-ENOMEM);

	sz = sizeof(struct btf_type) + sizeof(int);
	t = btf_add_type_mem(btf, sz);
	if (!t)
		return libbpf_err(-ENOMEM);

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

/*
 * Append new BTF_KIND_FLOAT type with:
 *   - *name* - non-empty, non-NULL type name;
 *   - *sz* - size of the type, in bytes;
 * Returns:
 *   - >0, type ID of newly added BTF type;
 *   - <0, on error.
 */
int btf__add_float(struct btf *btf, const char *name, size_t byte_sz)
{
	struct btf_type *t;
	int sz, name_off;

	/* non-empty name */
	if (!name || !name[0])
		return libbpf_err(-EINVAL);

	/* byte_sz must be one of the explicitly allowed values */
	if (byte_sz != 2 && byte_sz != 4 && byte_sz != 8 && byte_sz != 12 &&
	    byte_sz != 16)
		return libbpf_err(-EINVAL);

	if (btf_ensure_modifiable(btf))
		return libbpf_err(-ENOMEM);

	sz = sizeof(struct btf_type);
	t = btf_add_type_mem(btf, sz);
	if (!t)
		return libbpf_err(-ENOMEM);

	name_off = btf__add_str(btf, name);
	if (name_off < 0)
		return name_off;

	t->name_off = name_off;
	t->info = btf_type_info(BTF_KIND_FLOAT, 0, 0);
	t->size = byte_sz;

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
		return libbpf_err(-EINVAL);

	if (btf_ensure_modifiable(btf))
		return libbpf_err(-ENOMEM);

	sz = sizeof(struct btf_type);
	t = btf_add_type_mem(btf, sz);
	if (!t)
		return libbpf_err(-ENOMEM);

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
		return libbpf_err(-EINVAL);

	if (btf_ensure_modifiable(btf))
		return libbpf_err(-ENOMEM);

	sz = sizeof(struct btf_type) + sizeof(struct btf_array);
	t = btf_add_type_mem(btf, sz);
	if (!t)
		return libbpf_err(-ENOMEM);

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
		return libbpf_err(-ENOMEM);

	sz = sizeof(struct btf_type);
	t = btf_add_type_mem(btf, sz);
	if (!t)
		return libbpf_err(-ENOMEM);

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
	return btf_type_by_id(btf, btf__type_cnt(btf) - 1);
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
		return libbpf_err(-EINVAL);
	t = btf_last_type(btf);
	if (!btf_is_composite(t))
		return libbpf_err(-EINVAL);

	if (validate_type_id(type_id))
		return libbpf_err(-EINVAL);
	/* best-effort bit field offset/size enforcement */
	is_bitfield = bit_size || (bit_offset % 8 != 0);
	if (is_bitfield && (bit_size == 0 || bit_size > 255 || bit_offset > 0xffffff))
		return libbpf_err(-EINVAL);

	/* only offset 0 is allowed for unions */
	if (btf_is_union(t) && bit_offset)
		return libbpf_err(-EINVAL);

	/* decompose and invalidate raw data */
	if (btf_ensure_modifiable(btf))
		return libbpf_err(-ENOMEM);

	sz = sizeof(struct btf_member);
	m = btf_add_type_mem(btf, sz);
	if (!m)
		return libbpf_err(-ENOMEM);

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

static int btf_add_enum_common(struct btf *btf, const char *name, __u32 byte_sz,
			       bool is_signed, __u8 kind)
{
	struct btf_type *t;
	int sz, name_off = 0;

	/* byte_sz must be power of 2 */
	if (!byte_sz || (byte_sz & (byte_sz - 1)) || byte_sz > 8)
		return libbpf_err(-EINVAL);

	if (btf_ensure_modifiable(btf))
		return libbpf_err(-ENOMEM);

	sz = sizeof(struct btf_type);
	t = btf_add_type_mem(btf, sz);
	if (!t)
		return libbpf_err(-ENOMEM);

	if (name && name[0]) {
		name_off = btf__add_str(btf, name);
		if (name_off < 0)
			return name_off;
	}

	/* start out with vlen=0; it will be adjusted when adding enum values */
	t->name_off = name_off;
	t->info = btf_type_info(kind, 0, is_signed);
	t->size = byte_sz;

	return btf_commit_type(btf, sz);
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
	/*
	 * set the signedness to be unsigned, it will change to signed
	 * if any later enumerator is negative.
	 */
	return btf_add_enum_common(btf, name, byte_sz, false, BTF_KIND_ENUM);
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
		return libbpf_err(-EINVAL);
	t = btf_last_type(btf);
	if (!btf_is_enum(t))
		return libbpf_err(-EINVAL);

	/* non-empty name */
	if (!name || !name[0])
		return libbpf_err(-EINVAL);
	if (value < INT_MIN || value > UINT_MAX)
		return libbpf_err(-E2BIG);

	/* decompose and invalidate raw data */
	if (btf_ensure_modifiable(btf))
		return libbpf_err(-ENOMEM);

	sz = sizeof(struct btf_enum);
	v = btf_add_type_mem(btf, sz);
	if (!v)
		return libbpf_err(-ENOMEM);

	name_off = btf__add_str(btf, name);
	if (name_off < 0)
		return name_off;

	v->name_off = name_off;
	v->val = value;

	/* update parent type's vlen */
	t = btf_last_type(btf);
	btf_type_inc_vlen(t);

	/* if negative value, set signedness to signed */
	if (value < 0)
		t->info = btf_type_info(btf_kind(t), btf_vlen(t), true);

	btf->hdr->type_len += sz;
	btf->hdr->str_off += sz;
	return 0;
}

/*
 * Append new BTF_KIND_ENUM64 type with:
 *   - *name* - name of the enum, can be NULL or empty for anonymous enums;
 *   - *byte_sz* - size of the enum, in bytes.
 *   - *is_signed* - whether the enum values are signed or not;
 *
 * Enum initially has no enum values in it (and corresponds to enum forward
 * declaration). Enumerator values can be added by btf__add_enum64_value()
 * immediately after btf__add_enum64() succeeds.
 *
 * Returns:
 *   - >0, type ID of newly added BTF type;
 *   - <0, on error.
 */
int btf__add_enum64(struct btf *btf, const char *name, __u32 byte_sz,
		    bool is_signed)
{
	return btf_add_enum_common(btf, name, byte_sz, is_signed,
				   BTF_KIND_ENUM64);
}

/*
 * Append new enum value for the current ENUM64 type with:
 *   - *name* - name of the enumerator value, can't be NULL or empty;
 *   - *value* - integer value corresponding to enum value *name*;
 * Returns:
 *   -  0, on success;
 *   - <0, on error.
 */
int btf__add_enum64_value(struct btf *btf, const char *name, __u64 value)
{
	struct btf_enum64 *v;
	struct btf_type *t;
	int sz, name_off;

	/* last type should be BTF_KIND_ENUM64 */
	if (btf->nr_types == 0)
		return libbpf_err(-EINVAL);
	t = btf_last_type(btf);
	if (!btf_is_enum64(t))
		return libbpf_err(-EINVAL);

	/* non-empty name */
	if (!name || !name[0])
		return libbpf_err(-EINVAL);

	/* decompose and invalidate raw data */
	if (btf_ensure_modifiable(btf))
		return libbpf_err(-ENOMEM);

	sz = sizeof(struct btf_enum64);
	v = btf_add_type_mem(btf, sz);
	if (!v)
		return libbpf_err(-ENOMEM);

	name_off = btf__add_str(btf, name);
	if (name_off < 0)
		return name_off;

	v->name_off = name_off;
	v->val_lo32 = (__u32)value;
	v->val_hi32 = value >> 32;

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
		return libbpf_err(-EINVAL);

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
		return libbpf_err(-EINVAL);
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
		return libbpf_err(-EINVAL);

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
 * Append new BTF_KIND_TYPE_TAG type with:
 *   - *value*, non-empty/non-NULL tag value;
 *   - *ref_type_id* - referenced type ID, it might not exist yet;
 * Returns:
 *   - >0, type ID of newly added BTF type;
 *   - <0, on error.
 */
int btf__add_type_tag(struct btf *btf, const char *value, int ref_type_id)
{
	if (!value || !value[0])
		return libbpf_err(-EINVAL);

	return btf_add_ref_kind(btf, BTF_KIND_TYPE_TAG, value, ref_type_id);
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
		return libbpf_err(-EINVAL);
	if (linkage != BTF_FUNC_STATIC && linkage != BTF_FUNC_GLOBAL &&
	    linkage != BTF_FUNC_EXTERN)
		return libbpf_err(-EINVAL);

	id = btf_add_ref_kind(btf, BTF_KIND_FUNC, name, proto_type_id);
	if (id > 0) {
		struct btf_type *t = btf_type_by_id(btf, id);

		t->info = btf_type_info(BTF_KIND_FUNC, linkage, 0);
	}
	return libbpf_err(id);
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
		return libbpf_err(-EINVAL);

	if (btf_ensure_modifiable(btf))
		return libbpf_err(-ENOMEM);

	sz = sizeof(struct btf_type);
	t = btf_add_type_mem(btf, sz);
	if (!t)
		return libbpf_err(-ENOMEM);

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
		return libbpf_err(-EINVAL);

	/* last type should be BTF_KIND_FUNC_PROTO */
	if (btf->nr_types == 0)
		return libbpf_err(-EINVAL);
	t = btf_last_type(btf);
	if (!btf_is_func_proto(t))
		return libbpf_err(-EINVAL);

	/* decompose and invalidate raw data */
	if (btf_ensure_modifiable(btf))
		return libbpf_err(-ENOMEM);

	sz = sizeof(struct btf_param);
	p = btf_add_type_mem(btf, sz);
	if (!p)
		return libbpf_err(-ENOMEM);

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
		return libbpf_err(-EINVAL);
	if (linkage != BTF_VAR_STATIC && linkage != BTF_VAR_GLOBAL_ALLOCATED &&
	    linkage != BTF_VAR_GLOBAL_EXTERN)
		return libbpf_err(-EINVAL);
	if (validate_type_id(type_id))
		return libbpf_err(-EINVAL);

	/* deconstruct BTF, if necessary, and invalidate raw_data */
	if (btf_ensure_modifiable(btf))
		return libbpf_err(-ENOMEM);

	sz = sizeof(struct btf_type) + sizeof(struct btf_var);
	t = btf_add_type_mem(btf, sz);
	if (!t)
		return libbpf_err(-ENOMEM);

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
		return libbpf_err(-EINVAL);

	if (btf_ensure_modifiable(btf))
		return libbpf_err(-ENOMEM);

	sz = sizeof(struct btf_type);
	t = btf_add_type_mem(btf, sz);
	if (!t)
		return libbpf_err(-ENOMEM);

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
		return libbpf_err(-EINVAL);
	t = btf_last_type(btf);
	if (!btf_is_datasec(t))
		return libbpf_err(-EINVAL);

	if (validate_type_id(var_type_id))
		return libbpf_err(-EINVAL);

	/* decompose and invalidate raw data */
	if (btf_ensure_modifiable(btf))
		return libbpf_err(-ENOMEM);

	sz = sizeof(struct btf_var_secinfo);
	v = btf_add_type_mem(btf, sz);
	if (!v)
		return libbpf_err(-ENOMEM);

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

/*
 * Append new BTF_KIND_DECL_TAG type with:
 *   - *value* - non-empty/non-NULL string;
 *   - *ref_type_id* - referenced type ID, it might not exist yet;
 *   - *component_idx* - -1 for tagging reference type, otherwise struct/union
 *     member or function argument index;
 * Returns:
 *   - >0, type ID of newly added BTF type;
 *   - <0, on error.
 */
int btf__add_decl_tag(struct btf *btf, const char *value, int ref_type_id,
		 int component_idx)
{
	struct btf_type *t;
	int sz, value_off;

	if (!value || !value[0] || component_idx < -1)
		return libbpf_err(-EINVAL);

	if (validate_type_id(ref_type_id))
		return libbpf_err(-EINVAL);

	if (btf_ensure_modifiable(btf))
		return libbpf_err(-ENOMEM);

	sz = sizeof(struct btf_type) + sizeof(struct btf_decl_tag);
	t = btf_add_type_mem(btf, sz);
	if (!t)
		return libbpf_err(-ENOMEM);

	value_off = btf__add_str(btf, value);
	if (value_off < 0)
		return value_off;

	t->name_off = value_off;
	t->info = btf_type_info(BTF_KIND_DECL_TAG, 0, false);
	t->type = ref_type_id;
	btf_decl_tag(t)->component_idx = component_idx;

	return btf_commit_type(btf, sz);
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
	size_t sec_cnt = 0;
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

		total_record_size = sec_hdrlen + (__u64)num_records * record_size;
		if (info_left < total_record_size) {
			pr_debug("%s section has incorrect num_records in .BTF.ext\n",
			     ext_sec->desc);
			return -EINVAL;
		}

		info_left -= total_record_size;
		sinfo = (void *)sinfo + total_record_size;
		sec_cnt++;
	}

	ext_info = ext_sec->ext_info;
	ext_info->len = ext_sec->len - sizeof(__u32);
	ext_info->rec_size = record_size;
	ext_info->info = info + sizeof(__u32);
	ext_info->sec_cnt = sec_cnt;

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
	free(btf_ext->func_info.sec_idxs);
	free(btf_ext->line_info.sec_idxs);
	free(btf_ext->core_relo_info.sec_idxs);
	free(btf_ext->data);
	free(btf_ext);
}

struct btf_ext *btf_ext__new(const __u8 *data, __u32 size)
{
	struct btf_ext *btf_ext;
	int err;

	btf_ext = calloc(1, sizeof(struct btf_ext));
	if (!btf_ext)
		return libbpf_err_ptr(-ENOMEM);

	btf_ext->data_size = size;
	btf_ext->data = malloc(size);
	if (!btf_ext->data) {
		err = -ENOMEM;
		goto done;
	}
	memcpy(btf_ext->data, data, size);

	err = btf_ext_parse_hdr(btf_ext->data, size);
	if (err)
		goto done;

	if (btf_ext->hdr->hdr_len < offsetofend(struct btf_ext_header, line_info_len)) {
		err = -EINVAL;
		goto done;
	}

	err = btf_ext_setup_func_info(btf_ext);
	if (err)
		goto done;

	err = btf_ext_setup_line_info(btf_ext);
	if (err)
		goto done;

	if (btf_ext->hdr->hdr_len < offsetofend(struct btf_ext_header, core_relo_len))
		goto done; /* skip core relos parsing */

	err = btf_ext_setup_core_relos(btf_ext);
	if (err)
		goto done;

done:
	if (err) {
		btf_ext__free(btf_ext);
		return libbpf_err_ptr(err);
	}

	return btf_ext;
}

const void *btf_ext__raw_data(const struct btf_ext *btf_ext, __u32 *size)
{
	*size = btf_ext->data_size;
	return btf_ext->data;
}

__attribute__((alias("btf_ext__raw_data")))
const void *btf_ext__get_raw_data(const struct btf_ext *btf_ext, __u32 *size);


struct btf_dedup;

static struct btf_dedup *btf_dedup_new(struct btf *btf, const struct btf_dedup_opts *opts);
static void btf_dedup_free(struct btf_dedup *d);
static int btf_dedup_prep(struct btf_dedup *d);
static int btf_dedup_strings(struct btf_dedup *d);
static int btf_dedup_prim_types(struct btf_dedup *d);
static int btf_dedup_struct_types(struct btf_dedup *d);
static int btf_dedup_ref_types(struct btf_dedup *d);
static int btf_dedup_resolve_fwds(struct btf_dedup *d);
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
 * Algorithm completes its work in 7 separate passes:
 *
 * 1. Strings deduplication.
 * 2. Primitive types deduplication (int, enum, fwd).
 * 3. Struct/union types deduplication.
 * 4. Resolve unambiguous forward declarations.
 * 5. Reference types deduplication (pointers, typedefs, arrays, funcs, func
 *    protos, and const/volatile/restrict modifiers).
 * 6. Types compaction.
 * 7. Types remapping.
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
int btf__dedup(struct btf *btf, const struct btf_dedup_opts *opts)
{
	struct btf_dedup *d;
	int err;

	if (!OPTS_VALID(opts, btf_dedup_opts))
		return libbpf_err(-EINVAL);

	d = btf_dedup_new(btf, opts);
	if (IS_ERR(d)) {
		pr_debug("btf_dedup_new failed: %ld", PTR_ERR(d));
		return libbpf_err(-EINVAL);
	}

	if (btf_ensure_modifiable(btf)) {
		err = -ENOMEM;
		goto done;
	}

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
	err = btf_dedup_resolve_fwds(d);
	if (err < 0) {
		pr_debug("btf_dedup_resolve_fwds failed:%d\n", err);
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
	return libbpf_err(err);
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
	struct strset *strs_set;
};

static long hash_combine(long h, long value)
{
	return h * 31 + value;
}

#define for_each_dedup_cand(d, node, hash) \
	hashmap__for_each_key_entry(d->dedup_table, node, hash)

static int btf_dedup_table_add(struct btf_dedup *d, long hash, __u32 type_id)
{
	return hashmap__append(d->dedup_table, hash, type_id);
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

static size_t btf_dedup_identity_hash_fn(long key, void *ctx)
{
	return key;
}

static size_t btf_dedup_collision_hash_fn(long key, void *ctx)
{
	return 0;
}

static bool btf_dedup_equal_fn(long k1, long k2, void *ctx)
{
	return k1 == k2;
}

static struct btf_dedup *btf_dedup_new(struct btf *btf, const struct btf_dedup_opts *opts)
{
	struct btf_dedup *d = calloc(1, sizeof(struct btf_dedup));
	hashmap_hash_fn hash_fn = btf_dedup_identity_hash_fn;
	int i, err = 0, type_cnt;

	if (!d)
		return ERR_PTR(-ENOMEM);

	if (OPTS_GET(opts, force_collisions, false))
		hash_fn = btf_dedup_collision_hash_fn;

	d->btf = btf;
	d->btf_ext = OPTS_GET(opts, btf_ext, NULL);

	d->dedup_table = hashmap__new(hash_fn, btf_dedup_equal_fn, NULL);
	if (IS_ERR(d->dedup_table)) {
		err = PTR_ERR(d->dedup_table);
		d->dedup_table = NULL;
		goto done;
	}

	type_cnt = btf__type_cnt(btf);
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

/*
 * Iterate over all possible places in .BTF and .BTF.ext that can reference
 * string and pass pointer to it to a provided callback `fn`.
 */
static int btf_for_each_str_off(struct btf_dedup *d, str_off_visit_fn fn, void *ctx)
{
	int i, r;

	for (i = 0; i < d->btf->nr_types; i++) {
		struct btf_field_iter it;
		struct btf_type *t = btf_type_by_id(d->btf, d->btf->start_id + i);
		__u32 *str_off;

		r = btf_field_iter_init(&it, t, BTF_FIELD_ITER_STRS);
		if (r)
			return r;

		while ((str_off = btf_field_iter_next(&it))) {
			r = fn(str_off, ctx);
			if (r)
				return r;
		}
	}

	if (!d->btf_ext)
		return 0;

	r = btf_ext_visit_str_offs(d->btf_ext, fn, ctx);
	if (r)
		return r;

	return 0;
}

static int strs_dedup_remap_str_off(__u32 *str_off_ptr, void *ctx)
{
	struct btf_dedup *d = ctx;
	__u32 str_off = *str_off_ptr;
	const char *s;
	int off, err;

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

	off = strset__add_str(d->strs_set, s);
	if (off < 0)
		return off;

	*str_off_ptr = d->btf->start_str_off + off;
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
	int err;

	if (d->btf->strs_deduped)
		return 0;

	d->strs_set = strset__new(BTF_MAX_STR_OFFSET, NULL, 0);
	if (IS_ERR(d->strs_set)) {
		err = PTR_ERR(d->strs_set);
		goto err_out;
	}

	if (!d->btf->base_btf) {
		/* insert empty string; we won't be looking it up during strings
		 * dedup, but it's good to have it for generic BTF string lookups
		 */
		err = strset__add_str(d->strs_set, "");
		if (err < 0)
			goto err_out;
	}

	/* remap string offsets */
	err = btf_for_each_str_off(d, strs_dedup_remap_str_off, d);
	if (err)
		goto err_out;

	/* replace BTF string data and hash with deduped ones */
	strset__free(d->btf->strs_set);
	d->btf->hdr->str_len = strset__data_size(d->strs_set);
	d->btf->strs_set = d->strs_set;
	d->strs_set = NULL;
	d->btf->strs_deduped = true;
	return 0;

err_out:
	strset__free(d->strs_set);
	d->strs_set = NULL;

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

/* Calculate type signature hash of INT or TAG. */
static long btf_hash_int_decl_tag(struct btf_type *t)
{
	__u32 info = *(__u32 *)(t + 1);
	long h;

	h = btf_hash_common(t);
	h = hash_combine(h, info);
	return h;
}

/* Check structural equality of two INTs or TAGs. */
static bool btf_equal_int_tag(struct btf_type *t1, struct btf_type *t2)
{
	__u32 info1, info2;

	if (!btf_equal_common(t1, t2))
		return false;
	info1 = *(__u32 *)(t1 + 1);
	info2 = *(__u32 *)(t2 + 1);
	return info1 == info2;
}

/* Calculate type signature hash of ENUM/ENUM64. */
static long btf_hash_enum(struct btf_type *t)
{
	long h;

	/* don't hash vlen, enum members and size to support enum fwd resolving */
	h = hash_combine(0, t->name_off);
	return h;
}

static bool btf_equal_enum_members(struct btf_type *t1, struct btf_type *t2)
{
	const struct btf_enum *m1, *m2;
	__u16 vlen;
	int i;

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

static bool btf_equal_enum64_members(struct btf_type *t1, struct btf_type *t2)
{
	const struct btf_enum64 *m1, *m2;
	__u16 vlen;
	int i;

	vlen = btf_vlen(t1);
	m1 = btf_enum64(t1);
	m2 = btf_enum64(t2);
	for (i = 0; i < vlen; i++) {
		if (m1->name_off != m2->name_off || m1->val_lo32 != m2->val_lo32 ||
		    m1->val_hi32 != m2->val_hi32)
			return false;
		m1++;
		m2++;
	}
	return true;
}

/* Check structural equality of two ENUMs or ENUM64s. */
static bool btf_equal_enum(struct btf_type *t1, struct btf_type *t2)
{
	if (!btf_equal_common(t1, t2))
		return false;

	/* t1 & t2 kinds are identical because of btf_equal_common */
	if (btf_kind(t1) == BTF_KIND_ENUM)
		return btf_equal_enum_members(t1, t2);
	else
		return btf_equal_enum64_members(t1, t2);
}

static inline bool btf_is_enum_fwd(struct btf_type *t)
{
	return btf_is_any_enum(t) && btf_vlen(t) == 0;
}

static bool btf_compat_enum(struct btf_type *t1, struct btf_type *t2)
{
	if (!btf_is_enum_fwd(t1) && !btf_is_enum_fwd(t2))
		return btf_equal_enum(t1, t2);
	/* At this point either t1 or t2 or both are forward declarations, thus:
	 * - skip comparing vlen because it is zero for forward declarations;
	 * - skip comparing size to allow enum forward declarations
	 *   to be compatible with enum64 full declarations;
	 * - skip comparing kind for the same reason.
	 */
	return t1->name_off == t2->name_off &&
	       btf_is_any_enum(t1) && btf_is_any_enum(t2);
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
 * Check structural compatibility of two STRUCTs/UNIONs, ignoring referenced
 * type IDs. This check is performed during type graph equivalence check and
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
		case BTF_KIND_FLOAT:
		case BTF_KIND_TYPE_TAG:
			h = btf_hash_common(t);
			break;
		case BTF_KIND_INT:
		case BTF_KIND_DECL_TAG:
			h = btf_hash_int_decl_tag(t);
			break;
		case BTF_KIND_ENUM:
		case BTF_KIND_ENUM64:
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
	case BTF_KIND_DECL_TAG:
	case BTF_KIND_TYPE_TAG:
		return 0;

	case BTF_KIND_INT:
		h = btf_hash_int_decl_tag(t);
		for_each_dedup_cand(d, hash_entry, h) {
			cand_id = hash_entry->value;
			cand = btf_type_by_id(d->btf, cand_id);
			if (btf_equal_int_tag(t, cand)) {
				new_id = cand_id;
				break;
			}
		}
		break;

	case BTF_KIND_ENUM:
	case BTF_KIND_ENUM64:
		h = btf_hash_enum(t);
		for_each_dedup_cand(d, hash_entry, h) {
			cand_id = hash_entry->value;
			cand = btf_type_by_id(d->btf, cand_id);
			if (btf_equal_enum(t, cand)) {
				new_id = cand_id;
				break;
			}
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
	case BTF_KIND_FLOAT:
		h = btf_hash_common(t);
		for_each_dedup_cand(d, hash_entry, h) {
			cand_id = hash_entry->value;
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
static bool btf_dedup_identical_arrays(struct btf_dedup *d, __u32 id1, __u32 id2)
{
	struct btf_type *t1, *t2;

	t1 = btf_type_by_id(d->btf, id1);
	t2 = btf_type_by_id(d->btf, id2);
	if (!btf_is_array(t1) || !btf_is_array(t2))
		return false;

	return btf_equal_array(t1, t2);
}

/* Check if given two types are identical STRUCT/UNION definitions */
static bool btf_dedup_identical_structs(struct btf_dedup *d, __u32 id1, __u32 id2)
{
	const struct btf_member *m1, *m2;
	struct btf_type *t1, *t2;
	int n, i;

	t1 = btf_type_by_id(d->btf, id1);
	t2 = btf_type_by_id(d->btf, id2);

	if (!btf_is_composite(t1) || btf_kind(t1) != btf_kind(t2))
		return false;

	if (!btf_shallow_equal_struct(t1, t2))
		return false;

	m1 = btf_members(t1);
	m2 = btf_members(t2);
	for (i = 0, n = btf_vlen(t1); i < n; i++, m1++, m2++) {
		if (m1->type != m2->type &&
		    !btf_dedup_identical_arrays(d, m1->type, m2->type) &&
		    !btf_dedup_identical_structs(d, m1->type, m2->type))
			return false;
	}
	return true;
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
		if (hypot_type_id == cand_id)
			return 1;
		/* In some cases compiler will generate different DWARF types
		 * for *identical* array type definitions and use them for
		 * different fields within the *same* struct. This breaks type
		 * equivalence check, which makes an assumption that candidate
		 * types sub-graph has a consistent and deduped-by-compiler
		 * types within a single CU. So work around that by explicitly
		 * allowing identical array types here.
		 */
		if (btf_dedup_identical_arrays(d, hypot_type_id, cand_id))
			return 1;
		/* It turns out that similar situation can happen with
		 * struct/union sometimes, sigh... Handle the case where
		 * structs/unions are exactly the same, down to the referenced
		 * type IDs. Anything more complicated (e.g., if referenced
		 * types are different, but equivalent) is *way more*
		 * complicated and requires a many-to-many equivalence mapping.
		 */
		if (btf_dedup_identical_structs(d, hypot_type_id, cand_id))
			return 1;
		return 0;
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
	if ((cand_kind == BTF_KIND_FWD || canon_kind == BTF_KIND_FWD)
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
		return btf_equal_int_tag(cand_type, canon_type);

	case BTF_KIND_ENUM:
	case BTF_KIND_ENUM64:
		return btf_compat_enum(cand_type, canon_type);

	case BTF_KIND_FWD:
	case BTF_KIND_FLOAT:
		return btf_equal_common(cand_type, canon_type);

	case BTF_KIND_CONST:
	case BTF_KIND_VOLATILE:
	case BTF_KIND_RESTRICT:
	case BTF_KIND_PTR:
	case BTF_KIND_TYPEDEF:
	case BTF_KIND_FUNC:
	case BTF_KIND_TYPE_TAG:
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
		__u32 cand_id = hash_entry->value;
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
	case BTF_KIND_TYPE_TAG:
		ref_type_id = btf_dedup_ref_type(d, t->type);
		if (ref_type_id < 0)
			return ref_type_id;
		t->type = ref_type_id;

		h = btf_hash_common(t);
		for_each_dedup_cand(d, hash_entry, h) {
			cand_id = hash_entry->value;
			cand = btf_type_by_id(d->btf, cand_id);
			if (btf_equal_common(t, cand)) {
				new_id = cand_id;
				break;
			}
		}
		break;

	case BTF_KIND_DECL_TAG:
		ref_type_id = btf_dedup_ref_type(d, t->type);
		if (ref_type_id < 0)
			return ref_type_id;
		t->type = ref_type_id;

		h = btf_hash_int_decl_tag(t);
		for_each_dedup_cand(d, hash_entry, h) {
			cand_id = hash_entry->value;
			cand = btf_type_by_id(d->btf, cand_id);
			if (btf_equal_int_tag(t, cand)) {
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
			cand_id = hash_entry->value;
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
			cand_id = hash_entry->value;
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
 * Collect a map from type names to type ids for all canonical structs
 * and unions. If the same name is shared by several canonical types
 * use a special value 0 to indicate this fact.
 */
static int btf_dedup_fill_unique_names_map(struct btf_dedup *d, struct hashmap *names_map)
{
	__u32 nr_types = btf__type_cnt(d->btf);
	struct btf_type *t;
	__u32 type_id;
	__u16 kind;
	int err;

	/*
	 * Iterate over base and split module ids in order to get all
	 * available structs in the map.
	 */
	for (type_id = 1; type_id < nr_types; ++type_id) {
		t = btf_type_by_id(d->btf, type_id);
		kind = btf_kind(t);

		if (kind != BTF_KIND_STRUCT && kind != BTF_KIND_UNION)
			continue;

		/* Skip non-canonical types */
		if (type_id != d->map[type_id])
			continue;

		err = hashmap__add(names_map, t->name_off, type_id);
		if (err == -EEXIST)
			err = hashmap__set(names_map, t->name_off, 0, NULL, NULL);

		if (err)
			return err;
	}

	return 0;
}

static int btf_dedup_resolve_fwd(struct btf_dedup *d, struct hashmap *names_map, __u32 type_id)
{
	struct btf_type *t = btf_type_by_id(d->btf, type_id);
	enum btf_fwd_kind fwd_kind = btf_kflag(t);
	__u16 cand_kind, kind = btf_kind(t);
	struct btf_type *cand_t;
	uintptr_t cand_id;

	if (kind != BTF_KIND_FWD)
		return 0;

	/* Skip if this FWD already has a mapping */
	if (type_id != d->map[type_id])
		return 0;

	if (!hashmap__find(names_map, t->name_off, &cand_id))
		return 0;

	/* Zero is a special value indicating that name is not unique */
	if (!cand_id)
		return 0;

	cand_t = btf_type_by_id(d->btf, cand_id);
	cand_kind = btf_kind(cand_t);
	if ((cand_kind == BTF_KIND_STRUCT && fwd_kind != BTF_FWD_STRUCT) ||
	    (cand_kind == BTF_KIND_UNION && fwd_kind != BTF_FWD_UNION))
		return 0;

	d->map[type_id] = cand_id;

	return 0;
}

/*
 * Resolve unambiguous forward declarations.
 *
 * The lion's share of all FWD declarations is resolved during
 * `btf_dedup_struct_types` phase when different type graphs are
 * compared against each other. However, if in some compilation unit a
 * FWD declaration is not a part of a type graph compared against
 * another type graph that declaration's canonical type would not be
 * changed. Example:
 *
 * CU #1:
 *
 * struct foo;
 * struct foo *some_global;
 *
 * CU #2:
 *
 * struct foo { int u; };
 * struct foo *another_global;
 *
 * After `btf_dedup_struct_types` the BTF looks as follows:
 *
 * [1] STRUCT 'foo' size=4 vlen=1 ...
 * [2] INT 'int' size=4 ...
 * [3] PTR '(anon)' type_id=1
 * [4] FWD 'foo' fwd_kind=struct
 * [5] PTR '(anon)' type_id=4
 *
 * This pass assumes that such FWD declarations should be mapped to
 * structs or unions with identical name in case if the name is not
 * ambiguous.
 */
static int btf_dedup_resolve_fwds(struct btf_dedup *d)
{
	int i, err;
	struct hashmap *names_map;

	names_map = hashmap__new(btf_dedup_identity_hash_fn, btf_dedup_equal_fn, NULL);
	if (IS_ERR(names_map))
		return PTR_ERR(names_map);

	err = btf_dedup_fill_unique_names_map(d, names_map);
	if (err < 0)
		goto exit;

	for (i = 0; i < d->btf->nr_types; i++) {
		err = btf_dedup_resolve_fwd(d, names_map, d->btf->start_id + i);
		if (err < 0)
			break;
	}

exit:
	hashmap__free(names_map);
	return err;
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
static int btf_dedup_remap_type_id(__u32 *type_id, void *ctx)
{
	struct btf_dedup *d = ctx;
	__u32 resolved_type_id, new_type_id;

	resolved_type_id = resolve_type_id(d, *type_id);
	new_type_id = d->hypot_map[resolved_type_id];
	if (new_type_id > BTF_MAX_NR_TYPES)
		return -EINVAL;

	*type_id = new_type_id;
	return 0;
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
static int btf_dedup_remap_types(struct btf_dedup *d)
{
	int i, r;

	for (i = 0; i < d->btf->nr_types; i++) {
		struct btf_type *t = btf_type_by_id(d->btf, d->btf->start_id + i);
		struct btf_field_iter it;
		__u32 *type_id;

		r = btf_field_iter_init(&it, t, BTF_FIELD_ITER_IDS);
		if (r)
			return r;

		while ((type_id = btf_field_iter_next(&it))) {
			__u32 resolved_id, new_id;

			resolved_id = resolve_type_id(d, *type_id);
			new_id = d->hypot_map[resolved_id];
			if (new_id > BTF_MAX_NR_TYPES)
				return -EINVAL;

			*type_id = new_id;
		}
	}

	if (!d->btf_ext)
		return 0;

	r = btf_ext_visit_type_ids(d->btf_ext, btf_dedup_remap_type_id, d);
	if (r)
		return r;

	return 0;
}

/*
 * Probe few well-known locations for vmlinux kernel image and try to load BTF
 * data out of it to use for target BTF.
 */
struct btf *btf__load_vmlinux_btf(void)
{
	const char *sysfs_btf_path = "/sys/kernel/btf/vmlinux";
	/* fall back locations, trying to find vmlinux on disk */
	const char *locations[] = {
		"/boot/vmlinux-%1$s",
		"/lib/modules/%1$s/vmlinux-%1$s",
		"/lib/modules/%1$s/build/vmlinux",
		"/usr/lib/modules/%1$s/kernel/vmlinux",
		"/usr/lib/debug/boot/vmlinux-%1$s",
		"/usr/lib/debug/boot/vmlinux-%1$s.debug",
		"/usr/lib/debug/lib/modules/%1$s/vmlinux",
	};
	char path[PATH_MAX + 1];
	struct utsname buf;
	struct btf *btf;
	int i, err;

	/* is canonical sysfs location accessible? */
	if (faccessat(AT_FDCWD, sysfs_btf_path, F_OK, AT_EACCESS) < 0) {
		pr_warn("kernel BTF is missing at '%s', was CONFIG_DEBUG_INFO_BTF enabled?\n",
			sysfs_btf_path);
	} else {
		btf = btf__parse(sysfs_btf_path, NULL);
		if (!btf) {
			err = -errno;
			pr_warn("failed to read kernel BTF from '%s': %d\n", sysfs_btf_path, err);
			return libbpf_err_ptr(err);
		}
		pr_debug("loaded kernel BTF from '%s'\n", sysfs_btf_path);
		return btf;
	}

	/* try fallback locations */
	uname(&buf);
	for (i = 0; i < ARRAY_SIZE(locations); i++) {
		snprintf(path, PATH_MAX, locations[i], buf.release);

		if (faccessat(AT_FDCWD, path, R_OK, AT_EACCESS))
			continue;

		btf = btf__parse(path, NULL);
		err = libbpf_get_error(btf);
		pr_debug("loading kernel BTF '%s': %d\n", path, err);
		if (err)
			continue;

		return btf;
	}

	pr_warn("failed to find valid kernel BTF\n");
	return libbpf_err_ptr(-ESRCH);
}

struct btf *libbpf_find_kernel_btf(void) __attribute__((alias("btf__load_vmlinux_btf")));

struct btf *btf__load_module_btf(const char *module_name, struct btf *vmlinux_btf)
{
	char path[80];

	snprintf(path, sizeof(path), "/sys/kernel/btf/%s", module_name);
	return btf__parse_split(path, vmlinux_btf);
}

int btf_ext_visit_type_ids(struct btf_ext *btf_ext, type_id_visit_fn visit, void *ctx)
{
	const struct btf_ext_info *seg;
	struct btf_ext_info_sec *sec;
	int i, err;

	seg = &btf_ext->func_info;
	for_each_btf_ext_sec(seg, sec) {
		struct bpf_func_info_min *rec;

		for_each_btf_ext_rec(seg, sec, i, rec) {
			err = visit(&rec->type_id, ctx);
			if (err < 0)
				return err;
		}
	}

	seg = &btf_ext->core_relo_info;
	for_each_btf_ext_sec(seg, sec) {
		struct bpf_core_relo *rec;

		for_each_btf_ext_rec(seg, sec, i, rec) {
			err = visit(&rec->type_id, ctx);
			if (err < 0)
				return err;
		}
	}

	return 0;
}

int btf_ext_visit_str_offs(struct btf_ext *btf_ext, str_off_visit_fn visit, void *ctx)
{
	const struct btf_ext_info *seg;
	struct btf_ext_info_sec *sec;
	int i, err;

	seg = &btf_ext->func_info;
	for_each_btf_ext_sec(seg, sec) {
		err = visit(&sec->sec_name_off, ctx);
		if (err)
			return err;
	}

	seg = &btf_ext->line_info;
	for_each_btf_ext_sec(seg, sec) {
		struct bpf_line_info_min *rec;

		err = visit(&sec->sec_name_off, ctx);
		if (err)
			return err;

		for_each_btf_ext_rec(seg, sec, i, rec) {
			err = visit(&rec->file_name_off, ctx);
			if (err)
				return err;
			err = visit(&rec->line_off, ctx);
			if (err)
				return err;
		}
	}

	seg = &btf_ext->core_relo_info;
	for_each_btf_ext_sec(seg, sec) {
		struct bpf_core_relo *rec;

		err = visit(&sec->sec_name_off, ctx);
		if (err)
			return err;

		for_each_btf_ext_rec(seg, sec, i, rec) {
			err = visit(&rec->access_str_off, ctx);
			if (err)
				return err;
		}
	}

	return 0;
}

struct btf_distill {
	struct btf_pipe pipe;
	int *id_map;
	unsigned int split_start_id;
	unsigned int split_start_str;
	int diff_id;
};

static int btf_add_distilled_type_ids(struct btf_distill *dist, __u32 i)
{
	struct btf_type *split_t = btf_type_by_id(dist->pipe.src, i);
	struct btf_field_iter it;
	__u32 *id;
	int err;

	err = btf_field_iter_init(&it, split_t, BTF_FIELD_ITER_IDS);
	if (err)
		return err;
	while ((id = btf_field_iter_next(&it))) {
		struct btf_type *base_t;

		if (!*id)
			continue;
		/* split BTF id, not needed */
		if (*id >= dist->split_start_id)
			continue;
		/* already added ? */
		if (dist->id_map[*id] > 0)
			continue;

		/* only a subset of base BTF types should be referenced from
		 * split BTF; ensure nothing unexpected is referenced.
		 */
		base_t = btf_type_by_id(dist->pipe.src, *id);
		switch (btf_kind(base_t)) {
		case BTF_KIND_INT:
		case BTF_KIND_FLOAT:
		case BTF_KIND_FWD:
		case BTF_KIND_ARRAY:
		case BTF_KIND_STRUCT:
		case BTF_KIND_UNION:
		case BTF_KIND_TYPEDEF:
		case BTF_KIND_ENUM:
		case BTF_KIND_ENUM64:
		case BTF_KIND_PTR:
		case BTF_KIND_CONST:
		case BTF_KIND_RESTRICT:
		case BTF_KIND_VOLATILE:
		case BTF_KIND_FUNC_PROTO:
		case BTF_KIND_TYPE_TAG:
			dist->id_map[*id] = *id;
			break;
		default:
			pr_warn("unexpected reference to base type[%u] of kind [%u] when creating distilled base BTF.\n",
				*id, btf_kind(base_t));
			return -EINVAL;
		}
		/* If a base type is used, ensure types it refers to are
		 * marked as used also; so for example if we find a PTR to INT
		 * we need both the PTR and INT.
		 *
		 * The only exception is named struct/unions, since distilled
		 * base BTF composite types have no members.
		 */
		if (btf_is_composite(base_t) && base_t->name_off)
			continue;
		err = btf_add_distilled_type_ids(dist, *id);
		if (err)
			return err;
	}
	return 0;
}

static int btf_add_distilled_types(struct btf_distill *dist)
{
	bool adding_to_base = dist->pipe.dst->start_id == 1;
	int id = btf__type_cnt(dist->pipe.dst);
	struct btf_type *t;
	int i, err = 0;


	/* Add types for each of the required references to either distilled
	 * base or split BTF, depending on type characteristics.
	 */
	for (i = 1; i < dist->split_start_id; i++) {
		const char *name;
		int kind;

		if (!dist->id_map[i])
			continue;
		t = btf_type_by_id(dist->pipe.src, i);
		kind = btf_kind(t);
		name = btf__name_by_offset(dist->pipe.src, t->name_off);

		switch (kind) {
		case BTF_KIND_INT:
		case BTF_KIND_FLOAT:
		case BTF_KIND_FWD:
			/* Named int, float, fwd are added to base. */
			if (!adding_to_base)
				continue;
			err = btf_add_type(&dist->pipe, t);
			break;
		case BTF_KIND_STRUCT:
		case BTF_KIND_UNION:
			/* Named struct/union are added to base as 0-vlen
			 * struct/union of same size.  Anonymous struct/unions
			 * are added to split BTF as-is.
			 */
			if (adding_to_base) {
				if (!t->name_off)
					continue;
				err = btf_add_composite(dist->pipe.dst, kind, name, t->size);
			} else {
				if (t->name_off)
					continue;
				err = btf_add_type(&dist->pipe, t);
			}
			break;
		case BTF_KIND_ENUM:
		case BTF_KIND_ENUM64:
			/* Named enum[64]s are added to base as a sized
			 * enum; relocation will match with appropriately-named
			 * and sized enum or enum64.
			 *
			 * Anonymous enums are added to split BTF as-is.
			 */
			if (adding_to_base) {
				if (!t->name_off)
					continue;
				err = btf__add_enum(dist->pipe.dst, name, t->size);
			} else {
				if (t->name_off)
					continue;
				err = btf_add_type(&dist->pipe, t);
			}
			break;
		case BTF_KIND_ARRAY:
		case BTF_KIND_TYPEDEF:
		case BTF_KIND_PTR:
		case BTF_KIND_CONST:
		case BTF_KIND_RESTRICT:
		case BTF_KIND_VOLATILE:
		case BTF_KIND_FUNC_PROTO:
		case BTF_KIND_TYPE_TAG:
			/* All other types are added to split BTF. */
			if (adding_to_base)
				continue;
			err = btf_add_type(&dist->pipe, t);
			break;
		default:
			pr_warn("unexpected kind when adding base type '%s'[%u] of kind [%u] to distilled base BTF.\n",
				name, i, kind);
			return -EINVAL;

		}
		if (err < 0)
			break;
		dist->id_map[i] = id++;
	}
	return err;
}

/* Split BTF ids without a mapping will be shifted downwards since distilled
 * base BTF is smaller than the original base BTF.  For those that have a
 * mapping (either to base or updated split BTF), update the id based on
 * that mapping.
 */
static int btf_update_distilled_type_ids(struct btf_distill *dist, __u32 i)
{
	struct btf_type *t = btf_type_by_id(dist->pipe.dst, i);
	struct btf_field_iter it;
	__u32 *id;
	int err;

	err = btf_field_iter_init(&it, t, BTF_FIELD_ITER_IDS);
	if (err)
		return err;
	while ((id = btf_field_iter_next(&it))) {
		if (dist->id_map[*id])
			*id = dist->id_map[*id];
		else if (*id >= dist->split_start_id)
			*id -= dist->diff_id;
	}
	return 0;
}

/* Create updated split BTF with distilled base BTF; distilled base BTF
 * consists of BTF information required to clarify the types that split
 * BTF refers to, omitting unneeded details.  Specifically it will contain
 * base types and memberless definitions of named structs, unions and enumerated
 * types. Associated reference types like pointers, arrays and anonymous
 * structs, unions and enumerated types will be added to split BTF.
 * Size is recorded for named struct/unions to help guide matching to the
 * target base BTF during later relocation.
 *
 * The only case where structs, unions or enumerated types are fully represented
 * is when they are anonymous; in such cases, the anonymous type is added to
 * split BTF in full.
 *
 * We return newly-created split BTF where the split BTF refers to a newly-created
 * distilled base BTF. Both must be freed separately by the caller.
 */
int btf__distill_base(const struct btf *src_btf, struct btf **new_base_btf,
		      struct btf **new_split_btf)
{
	struct btf *new_base = NULL, *new_split = NULL;
	const struct btf *old_base;
	unsigned int n = btf__type_cnt(src_btf);
	struct btf_distill dist = {};
	struct btf_type *t;
	int i, err = 0;

	/* src BTF must be split BTF. */
	old_base = btf__base_btf(src_btf);
	if (!new_base_btf || !new_split_btf || !old_base)
		return libbpf_err(-EINVAL);

	new_base = btf__new_empty();
	if (!new_base)
		return libbpf_err(-ENOMEM);
	dist.id_map = calloc(n, sizeof(*dist.id_map));
	if (!dist.id_map) {
		err = -ENOMEM;
		goto done;
	}
	dist.pipe.src = src_btf;
	dist.pipe.dst = new_base;
	dist.pipe.str_off_map = hashmap__new(btf_dedup_identity_hash_fn, btf_dedup_equal_fn, NULL);
	if (IS_ERR(dist.pipe.str_off_map)) {
		err = -ENOMEM;
		goto done;
	}
	dist.split_start_id = btf__type_cnt(old_base);
	dist.split_start_str = old_base->hdr->str_len;

	/* Pass over src split BTF; generate the list of base BTF type ids it
	 * references; these will constitute our distilled BTF set to be
	 * distributed over base and split BTF as appropriate.
	 */
	for (i = src_btf->start_id; i < n; i++) {
		err = btf_add_distilled_type_ids(&dist, i);
		if (err < 0)
			goto done;
	}
	/* Next add types for each of the required references to base BTF and split BTF
	 * in turn.
	 */
	err = btf_add_distilled_types(&dist);
	if (err < 0)
		goto done;

	/* Create new split BTF with distilled base BTF as its base; the final
	 * state is split BTF with distilled base BTF that represents enough
	 * about its base references to allow it to be relocated with the base
	 * BTF available.
	 */
	new_split = btf__new_empty_split(new_base);
	if (!new_split) {
		err = -errno;
		goto done;
	}
	dist.pipe.dst = new_split;
	/* First add all split types */
	for (i = src_btf->start_id; i < n; i++) {
		t = btf_type_by_id(src_btf, i);
		err = btf_add_type(&dist.pipe, t);
		if (err < 0)
			goto done;
	}
	/* Now add distilled types to split BTF that are not added to base. */
	err = btf_add_distilled_types(&dist);
	if (err < 0)
		goto done;

	/* All split BTF ids will be shifted downwards since there are less base
	 * BTF ids in distilled base BTF.
	 */
	dist.diff_id = dist.split_start_id - btf__type_cnt(new_base);

	n = btf__type_cnt(new_split);
	/* Now update base/split BTF ids. */
	for (i = 1; i < n; i++) {
		err = btf_update_distilled_type_ids(&dist, i);
		if (err < 0)
			break;
	}
done:
	free(dist.id_map);
	hashmap__free(dist.pipe.str_off_map);
	if (err) {
		btf__free(new_split);
		btf__free(new_base);
		return libbpf_err(err);
	}
	*new_base_btf = new_base;
	*new_split_btf = new_split;

	return 0;
}

const struct btf_header *btf_header(const struct btf *btf)
{
	return btf->hdr;
}

void btf_set_base_btf(struct btf *btf, const struct btf *base_btf)
{
	btf->base_btf = (struct btf *)base_btf;
	btf->start_id = btf__type_cnt(base_btf);
	btf->start_str_off = base_btf->hdr->str_len;
}

int btf__relocate(struct btf *btf, const struct btf *base_btf)
{
	int err = btf_relocate(btf, base_btf, NULL);

	if (!err)
		btf->owns_base = false;
	return libbpf_err(err);
}
