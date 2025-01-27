// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/* Copyright (c) 2024, Oracle and/or its affiliates. */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef __KERNEL__
#include <linux/bpf.h>
#include <linux/bsearch.h>
#include <linux/btf.h>
#include <linux/sort.h>
#include <linux/string.h>
#include <linux/bpf_verifier.h>

#define btf_type_by_id				(struct btf_type *)btf_type_by_id
#define btf__type_cnt				btf_nr_types
#define btf__base_btf				btf_base_btf
#define btf__name_by_offset			btf_name_by_offset
#define btf__str_by_offset			btf_str_by_offset
#define btf_kflag				btf_type_kflag

#define calloc(nmemb, sz)			kvcalloc(nmemb, sz, GFP_KERNEL | __GFP_NOWARN)
#define free(ptr)				kvfree(ptr)
#define qsort(base, num, sz, cmp)		sort(base, num, sz, cmp, NULL)

#else

#include "btf.h"
#include "bpf.h"
#include "libbpf.h"
#include "libbpf_internal.h"

#endif /* __KERNEL__ */

struct btf;

struct btf_relocate {
	struct btf *btf;
	const struct btf *base_btf;
	const struct btf *dist_base_btf;
	unsigned int nr_base_types;
	unsigned int nr_split_types;
	unsigned int nr_dist_base_types;
	int dist_str_len;
	int base_str_len;
	__u32 *id_map;
	__u32 *str_map;
};

/* Set temporarily in relocation id_map if distilled base struct/union is
 * embedded in a split BTF struct/union; in such a case, size information must
 * match between distilled base BTF and base BTF representation of type.
 */
#define BTF_IS_EMBEDDED ((__u32)-1)

/* <name, size, id> triple used in sorting/searching distilled base BTF. */
struct btf_name_info {
	const char *name;
	/* set when search requires a size match */
	bool needs_size: 1;
	unsigned int size: 31;
	__u32 id;
};

static int btf_relocate_rewrite_type_id(struct btf_relocate *r, __u32 i)
{
	struct btf_type *t = btf_type_by_id(r->btf, i);
	struct btf_field_iter it;
	__u32 *id;
	int err;

	err = btf_field_iter_init(&it, t, BTF_FIELD_ITER_IDS);
	if (err)
		return err;

	while ((id = btf_field_iter_next(&it)))
		*id = r->id_map[*id];
	return 0;
}

/* Simple string comparison used for sorting within BTF, since all distilled
 * types are named.  If strings match, and size is non-zero for both elements
 * fall back to using size for ordering.
 */
static int cmp_btf_name_size(const void *n1, const void *n2)
{
	const struct btf_name_info *ni1 = n1;
	const struct btf_name_info *ni2 = n2;
	int name_diff = strcmp(ni1->name, ni2->name);

	if (!name_diff && ni1->needs_size && ni2->needs_size)
		return ni2->size - ni1->size;
	return name_diff;
}

/* Binary search with a small twist; find leftmost element that matches
 * so that we can then iterate through all exact matches.  So for example
 * searching { "a", "bb", "bb", "c" }  we would always match on the
 * leftmost "bb".
 */
static struct btf_name_info *search_btf_name_size(struct btf_name_info *key,
						  struct btf_name_info *vals,
						  int nelems)
{
	struct btf_name_info *ret = NULL;
	int high = nelems - 1;
	int low = 0;

	while (low <= high) {
		int mid = (low + high)/2;
		struct btf_name_info *val = &vals[mid];
		int diff = cmp_btf_name_size(key, val);

		if (diff == 0)
			ret = val;
		/* even if found, keep searching for leftmost match */
		if (diff <= 0)
			high = mid - 1;
		else
			low = mid + 1;
	}
	return ret;
}

/* If a member of a split BTF struct/union refers to a base BTF
 * struct/union, mark that struct/union id temporarily in the id_map
 * with BTF_IS_EMBEDDED.  Members can be const/restrict/volatile/typedef
 * reference types, but if a pointer is encountered, the type is no longer
 * considered embedded.
 */
static int btf_mark_embedded_composite_type_ids(struct btf_relocate *r, __u32 i)
{
	struct btf_type *t = btf_type_by_id(r->btf, i);
	struct btf_field_iter it;
	__u32 *id;
	int err;

	if (!btf_is_composite(t))
		return 0;

	err = btf_field_iter_init(&it, t, BTF_FIELD_ITER_IDS);
	if (err)
		return err;

	while ((id = btf_field_iter_next(&it))) {
		__u32 next_id = *id;

		while (next_id) {
			t = btf_type_by_id(r->btf, next_id);
			switch (btf_kind(t)) {
			case BTF_KIND_CONST:
			case BTF_KIND_RESTRICT:
			case BTF_KIND_VOLATILE:
			case BTF_KIND_TYPEDEF:
			case BTF_KIND_TYPE_TAG:
				next_id = t->type;
				break;
			case BTF_KIND_ARRAY: {
				struct btf_array *a = btf_array(t);

				next_id = a->type;
				break;
			}
			case BTF_KIND_STRUCT:
			case BTF_KIND_UNION:
				if (next_id < r->nr_dist_base_types)
					r->id_map[next_id] = BTF_IS_EMBEDDED;
				next_id = 0;
				break;
			default:
				next_id = 0;
				break;
			}
		}
	}

	return 0;
}

/* Build a map from distilled base BTF ids to base BTF ids. To do so, iterate
 * through base BTF looking up distilled type (using binary search) equivalents.
 */
static int btf_relocate_map_distilled_base(struct btf_relocate *r)
{
	struct btf_name_info *info, *info_end;
	struct btf_type *base_t, *dist_t;
	__u8 *base_name_cnt = NULL;
	int err = 0;
	__u32 id;

	/* generate a sort index array of name/type ids sorted by name for
	 * distilled base BTF to speed name-based lookups.
	 */
	info = calloc(r->nr_dist_base_types, sizeof(*info));
	if (!info) {
		err = -ENOMEM;
		goto done;
	}
	info_end = info + r->nr_dist_base_types;
	for (id = 0; id < r->nr_dist_base_types; id++) {
		dist_t = btf_type_by_id(r->dist_base_btf, id);
		info[id].name = btf__name_by_offset(r->dist_base_btf, dist_t->name_off);
		info[id].id = id;
		info[id].size = dist_t->size;
		info[id].needs_size = true;
	}
	qsort(info, r->nr_dist_base_types, sizeof(*info), cmp_btf_name_size);

	/* Mark distilled base struct/union members of split BTF structs/unions
	 * in id_map with BTF_IS_EMBEDDED; this signals that these types
	 * need to match both name and size, otherwise embedding the base
	 * struct/union in the split type is invalid.
	 */
	for (id = r->nr_dist_base_types; id < r->nr_split_types; id++) {
		err = btf_mark_embedded_composite_type_ids(r, id);
		if (err)
			goto done;
	}

	/* Collect name counts for composite types in base BTF.  If multiple
	 * instances of a struct/union of the same name exist, we need to use
	 * size to determine which to map to since name alone is ambiguous.
	 */
	base_name_cnt = calloc(r->base_str_len, sizeof(*base_name_cnt));
	if (!base_name_cnt) {
		err = -ENOMEM;
		goto done;
	}
	for (id = 1; id < r->nr_base_types; id++) {
		base_t = btf_type_by_id(r->base_btf, id);
		if (!btf_is_composite(base_t) || !base_t->name_off)
			continue;
		if (base_name_cnt[base_t->name_off] < 255)
			base_name_cnt[base_t->name_off]++;
	}

	/* Now search base BTF for matching distilled base BTF types. */
	for (id = 1; id < r->nr_base_types; id++) {
		struct btf_name_info *dist_info, base_info = {};
		int dist_kind, base_kind;

		base_t = btf_type_by_id(r->base_btf, id);
		/* distilled base consists of named types only. */
		if (!base_t->name_off)
			continue;
		base_kind = btf_kind(base_t);
		base_info.id = id;
		base_info.name = btf__name_by_offset(r->base_btf, base_t->name_off);
		switch (base_kind) {
		case BTF_KIND_INT:
		case BTF_KIND_FLOAT:
		case BTF_KIND_ENUM:
		case BTF_KIND_ENUM64:
			/* These types should match both name and size */
			base_info.needs_size = true;
			base_info.size = base_t->size;
			break;
		case BTF_KIND_FWD:
			/* No size considerations for fwds. */
			break;
		case BTF_KIND_STRUCT:
		case BTF_KIND_UNION:
			/* Size only needs to be used for struct/union if there
			 * are multiple types in base BTF with the same name.
			 * If there are multiple _distilled_ types with the same
			 * name (a very unlikely scenario), that doesn't matter
			 * unless corresponding _base_ types to match them are
			 * missing.
			 */
			base_info.needs_size = base_name_cnt[base_t->name_off] > 1;
			base_info.size = base_t->size;
			break;
		default:
			continue;
		}
		/* iterate over all matching distilled base types */
		for (dist_info = search_btf_name_size(&base_info, info, r->nr_dist_base_types);
		     dist_info != NULL && dist_info < info_end &&
		     cmp_btf_name_size(&base_info, dist_info) == 0;
		     dist_info++) {
			if (!dist_info->id || dist_info->id >= r->nr_dist_base_types) {
				pr_warn("base BTF id [%d] maps to invalid distilled base BTF id [%d]\n",
					id, dist_info->id);
				err = -EINVAL;
				goto done;
			}
			dist_t = btf_type_by_id(r->dist_base_btf, dist_info->id);
			dist_kind = btf_kind(dist_t);

			/* Validate that the found distilled type is compatible.
			 * Do not error out on mismatch as another match may
			 * occur for an identically-named type.
			 */
			switch (dist_kind) {
			case BTF_KIND_FWD:
				switch (base_kind) {
				case BTF_KIND_FWD:
					if (btf_kflag(dist_t) != btf_kflag(base_t))
						continue;
					break;
				case BTF_KIND_STRUCT:
					if (btf_kflag(base_t))
						continue;
					break;
				case BTF_KIND_UNION:
					if (!btf_kflag(base_t))
						continue;
					break;
				default:
					continue;
				}
				break;
			case BTF_KIND_INT:
				if (dist_kind != base_kind ||
				    btf_int_encoding(base_t) != btf_int_encoding(dist_t))
					continue;
				break;
			case BTF_KIND_FLOAT:
				if (dist_kind != base_kind)
					continue;
				break;
			case BTF_KIND_ENUM:
				/* ENUM and ENUM64 are encoded as sized ENUM in
				 * distilled base BTF.
				 */
				if (base_kind != dist_kind && base_kind != BTF_KIND_ENUM64)
					continue;
				break;
			case BTF_KIND_STRUCT:
			case BTF_KIND_UNION:
				/* size verification is required for embedded
				 * struct/unions.
				 */
				if (r->id_map[dist_info->id] == BTF_IS_EMBEDDED &&
				    base_t->size != dist_t->size)
					continue;
				break;
			default:
				continue;
			}
			if (r->id_map[dist_info->id] &&
			    r->id_map[dist_info->id] != BTF_IS_EMBEDDED) {
				/* we already have a match; this tells us that
				 * multiple base types of the same name
				 * have the same size, since for cases where
				 * multiple types have the same name we match
				 * on name and size.  In this case, we have
				 * no way of determining which to relocate
				 * to in base BTF, so error out.
				 */
				pr_warn("distilled base BTF type '%s' [%u], size %u has multiple candidates of the same size (ids [%u, %u]) in base BTF\n",
					base_info.name, dist_info->id,
					base_t->size, id, r->id_map[dist_info->id]);
				err = -EINVAL;
				goto done;
			}
			/* map id and name */
			r->id_map[dist_info->id] = id;
			r->str_map[dist_t->name_off] = base_t->name_off;
		}
	}
	/* ensure all distilled BTF ids now have a mapping... */
	for (id = 1; id < r->nr_dist_base_types; id++) {
		const char *name;

		if (r->id_map[id] && r->id_map[id] != BTF_IS_EMBEDDED)
			continue;
		dist_t = btf_type_by_id(r->dist_base_btf, id);
		name = btf__name_by_offset(r->dist_base_btf, dist_t->name_off);
		pr_warn("distilled base BTF type '%s' [%d] is not mapped to base BTF id\n",
			name, id);
		err = -EINVAL;
		break;
	}
done:
	free(base_name_cnt);
	free(info);
	return err;
}

/* distilled base should only have named int/float/enum/fwd/struct/union types. */
static int btf_relocate_validate_distilled_base(struct btf_relocate *r)
{
	unsigned int i;

	for (i = 1; i < r->nr_dist_base_types; i++) {
		struct btf_type *t = btf_type_by_id(r->dist_base_btf, i);
		int kind = btf_kind(t);

		switch (kind) {
		case BTF_KIND_INT:
		case BTF_KIND_FLOAT:
		case BTF_KIND_ENUM:
		case BTF_KIND_STRUCT:
		case BTF_KIND_UNION:
		case BTF_KIND_FWD:
			if (t->name_off)
				break;
			pr_warn("type [%d], kind [%d] is invalid for distilled base BTF; it is anonymous\n",
				i, kind);
			return -EINVAL;
		default:
			pr_warn("type [%d] in distilled based BTF has unexpected kind [%d]\n",
				i, kind);
			return -EINVAL;
		}
	}
	return 0;
}

static int btf_relocate_rewrite_strs(struct btf_relocate *r, __u32 i)
{
	struct btf_type *t = btf_type_by_id(r->btf, i);
	struct btf_field_iter it;
	__u32 *str_off;
	int off, err;

	err = btf_field_iter_init(&it, t, BTF_FIELD_ITER_STRS);
	if (err)
		return err;

	while ((str_off = btf_field_iter_next(&it))) {
		if (!*str_off)
			continue;
		if (*str_off >= r->dist_str_len) {
			*str_off += r->base_str_len - r->dist_str_len;
		} else {
			off = r->str_map[*str_off];
			if (!off) {
				pr_warn("string '%s' [offset %u] is not mapped to base BTF\n",
					btf__str_by_offset(r->btf, off), *str_off);
				return -ENOENT;
			}
			*str_off = off;
		}
	}
	return 0;
}

/* If successful, output of relocation is updated BTF with base BTF pointing
 * at base_btf, and type ids, strings adjusted accordingly.
 */
int btf_relocate(struct btf *btf, const struct btf *base_btf, __u32 **id_map)
{
	unsigned int nr_types = btf__type_cnt(btf);
	const struct btf_header *dist_base_hdr;
	const struct btf_header *base_hdr;
	struct btf_relocate r = {};
	int err = 0;
	__u32 id, i;

	r.dist_base_btf = btf__base_btf(btf);
	if (!base_btf || r.dist_base_btf == base_btf)
		return -EINVAL;

	r.nr_dist_base_types = btf__type_cnt(r.dist_base_btf);
	r.nr_base_types = btf__type_cnt(base_btf);
	r.nr_split_types = nr_types - r.nr_dist_base_types;
	r.btf = btf;
	r.base_btf = base_btf;

	r.id_map = calloc(nr_types, sizeof(*r.id_map));
	r.str_map = calloc(btf_header(r.dist_base_btf)->str_len, sizeof(*r.str_map));
	dist_base_hdr = btf_header(r.dist_base_btf);
	base_hdr = btf_header(r.base_btf);
	r.dist_str_len = dist_base_hdr->str_len;
	r.base_str_len = base_hdr->str_len;
	if (!r.id_map || !r.str_map) {
		err = -ENOMEM;
		goto err_out;
	}

	err = btf_relocate_validate_distilled_base(&r);
	if (err)
		goto err_out;

	/* Split BTF ids need to be adjusted as base and distilled base
	 * have different numbers of types, changing the start id of split
	 * BTF.
	 */
	for (id = r.nr_dist_base_types; id < nr_types; id++)
		r.id_map[id] = id + r.nr_base_types - r.nr_dist_base_types;

	/* Build a map from distilled base ids to actual base BTF ids; it is used
	 * to update split BTF id references.  Also build a str_map mapping from
	 * distilled base BTF names to base BTF names.
	 */
	err = btf_relocate_map_distilled_base(&r);
	if (err)
		goto err_out;

	/* Next, rewrite type ids in split BTF, replacing split ids with updated
	 * ids based on number of types in base BTF, and base ids with
	 * relocated ids from base_btf.
	 */
	for (i = 0, id = r.nr_dist_base_types; i < r.nr_split_types; i++, id++) {
		err = btf_relocate_rewrite_type_id(&r, id);
		if (err)
			goto err_out;
	}
	/* String offsets now need to be updated using the str_map. */
	for (i = 0; i < r.nr_split_types; i++) {
		err = btf_relocate_rewrite_strs(&r, i + r.nr_dist_base_types);
		if (err)
			goto err_out;
	}
	/* Finally reset base BTF to be base_btf */
	btf_set_base_btf(btf, base_btf);

	if (id_map) {
		*id_map = r.id_map;
		r.id_map = NULL;
	}
err_out:
	free(r.id_map);
	free(r.str_map);
	return err;
}
