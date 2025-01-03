// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2019 Facebook */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <linux/err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/btf.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <bpf/bpf.h>
#include <bpf/btf.h>
#include <bpf/hashmap.h>
#include <bpf/libbpf.h>

#include "json_writer.h"
#include "main.h"

#define KFUNC_DECL_TAG		"bpf_kfunc"
#define FASTCALL_DECL_TAG	"bpf_fastcall"

static const char * const btf_kind_str[NR_BTF_KINDS] = {
	[BTF_KIND_UNKN]		= "UNKNOWN",
	[BTF_KIND_INT]		= "INT",
	[BTF_KIND_PTR]		= "PTR",
	[BTF_KIND_ARRAY]	= "ARRAY",
	[BTF_KIND_STRUCT]	= "STRUCT",
	[BTF_KIND_UNION]	= "UNION",
	[BTF_KIND_ENUM]		= "ENUM",
	[BTF_KIND_FWD]		= "FWD",
	[BTF_KIND_TYPEDEF]	= "TYPEDEF",
	[BTF_KIND_VOLATILE]	= "VOLATILE",
	[BTF_KIND_CONST]	= "CONST",
	[BTF_KIND_RESTRICT]	= "RESTRICT",
	[BTF_KIND_FUNC]		= "FUNC",
	[BTF_KIND_FUNC_PROTO]	= "FUNC_PROTO",
	[BTF_KIND_VAR]		= "VAR",
	[BTF_KIND_DATASEC]	= "DATASEC",
	[BTF_KIND_FLOAT]	= "FLOAT",
	[BTF_KIND_DECL_TAG]	= "DECL_TAG",
	[BTF_KIND_TYPE_TAG]	= "TYPE_TAG",
	[BTF_KIND_ENUM64]	= "ENUM64",
};

struct sort_datum {
	int index;
	int type_rank;
	const char *sort_name;
	const char *own_name;
	__u64 disambig_hash;
};

static const char *btf_int_enc_str(__u8 encoding)
{
	switch (encoding) {
	case 0:
		return "(none)";
	case BTF_INT_SIGNED:
		return "SIGNED";
	case BTF_INT_CHAR:
		return "CHAR";
	case BTF_INT_BOOL:
		return "BOOL";
	default:
		return "UNKN";
	}
}

static const char *btf_var_linkage_str(__u32 linkage)
{
	switch (linkage) {
	case BTF_VAR_STATIC:
		return "static";
	case BTF_VAR_GLOBAL_ALLOCATED:
		return "global";
	case BTF_VAR_GLOBAL_EXTERN:
		return "extern";
	default:
		return "(unknown)";
	}
}

static const char *btf_func_linkage_str(const struct btf_type *t)
{
	switch (btf_vlen(t)) {
	case BTF_FUNC_STATIC:
		return "static";
	case BTF_FUNC_GLOBAL:
		return "global";
	case BTF_FUNC_EXTERN:
		return "extern";
	default:
		return "(unknown)";
	}
}

static const char *btf_str(const struct btf *btf, __u32 off)
{
	if (!off)
		return "(anon)";
	return btf__name_by_offset(btf, off) ? : "(invalid)";
}

static int btf_kind_safe(int kind)
{
	return kind <= BTF_KIND_MAX ? kind : BTF_KIND_UNKN;
}

static int dump_btf_type(const struct btf *btf, __u32 id,
			 const struct btf_type *t)
{
	json_writer_t *w = json_wtr;
	int kind = btf_kind(t);

	if (json_output) {
		jsonw_start_object(w);
		jsonw_uint_field(w, "id", id);
		jsonw_string_field(w, "kind", btf_kind_str[btf_kind_safe(kind)]);
		jsonw_string_field(w, "name", btf_str(btf, t->name_off));
	} else {
		printf("[%u] %s '%s'", id, btf_kind_str[btf_kind_safe(kind)],
		       btf_str(btf, t->name_off));
	}

	switch (kind) {
	case BTF_KIND_INT: {
		__u32 v = *(__u32 *)(t + 1);
		const char *enc;

		enc = btf_int_enc_str(BTF_INT_ENCODING(v));

		if (json_output) {
			jsonw_uint_field(w, "size", t->size);
			jsonw_uint_field(w, "bits_offset", BTF_INT_OFFSET(v));
			jsonw_uint_field(w, "nr_bits", BTF_INT_BITS(v));
			jsonw_string_field(w, "encoding", enc);
		} else {
			printf(" size=%u bits_offset=%u nr_bits=%u encoding=%s",
			       t->size, BTF_INT_OFFSET(v), BTF_INT_BITS(v),
			       enc);
		}
		break;
	}
	case BTF_KIND_PTR:
	case BTF_KIND_CONST:
	case BTF_KIND_VOLATILE:
	case BTF_KIND_RESTRICT:
	case BTF_KIND_TYPEDEF:
	case BTF_KIND_TYPE_TAG:
		if (json_output)
			jsonw_uint_field(w, "type_id", t->type);
		else
			printf(" type_id=%u", t->type);
		break;
	case BTF_KIND_ARRAY: {
		const struct btf_array *arr = (const void *)(t + 1);

		if (json_output) {
			jsonw_uint_field(w, "type_id", arr->type);
			jsonw_uint_field(w, "index_type_id", arr->index_type);
			jsonw_uint_field(w, "nr_elems", arr->nelems);
		} else {
			printf(" type_id=%u index_type_id=%u nr_elems=%u",
			       arr->type, arr->index_type, arr->nelems);
		}
		break;
	}
	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION: {
		const struct btf_member *m = (const void *)(t + 1);
		__u16 vlen = BTF_INFO_VLEN(t->info);
		int i;

		if (json_output) {
			jsonw_uint_field(w, "size", t->size);
			jsonw_uint_field(w, "vlen", vlen);
			jsonw_name(w, "members");
			jsonw_start_array(w);
		} else {
			printf(" size=%u vlen=%u", t->size, vlen);
		}
		for (i = 0; i < vlen; i++, m++) {
			const char *name = btf_str(btf, m->name_off);
			__u32 bit_off, bit_sz;

			if (BTF_INFO_KFLAG(t->info)) {
				bit_off = BTF_MEMBER_BIT_OFFSET(m->offset);
				bit_sz = BTF_MEMBER_BITFIELD_SIZE(m->offset);
			} else {
				bit_off = m->offset;
				bit_sz = 0;
			}

			if (json_output) {
				jsonw_start_object(w);
				jsonw_string_field(w, "name", name);
				jsonw_uint_field(w, "type_id", m->type);
				jsonw_uint_field(w, "bits_offset", bit_off);
				if (bit_sz) {
					jsonw_uint_field(w, "bitfield_size",
							 bit_sz);
				}
				jsonw_end_object(w);
			} else {
				printf("\n\t'%s' type_id=%u bits_offset=%u",
				       name, m->type, bit_off);
				if (bit_sz)
					printf(" bitfield_size=%u", bit_sz);
			}
		}
		if (json_output)
			jsonw_end_array(w);
		break;
	}
	case BTF_KIND_ENUM: {
		const struct btf_enum *v = (const void *)(t + 1);
		__u16 vlen = BTF_INFO_VLEN(t->info);
		const char *encoding;
		int i;

		encoding = btf_kflag(t) ? "SIGNED" : "UNSIGNED";
		if (json_output) {
			jsonw_string_field(w, "encoding", encoding);
			jsonw_uint_field(w, "size", t->size);
			jsonw_uint_field(w, "vlen", vlen);
			jsonw_name(w, "values");
			jsonw_start_array(w);
		} else {
			printf(" encoding=%s size=%u vlen=%u", encoding, t->size, vlen);
		}
		for (i = 0; i < vlen; i++, v++) {
			const char *name = btf_str(btf, v->name_off);

			if (json_output) {
				jsonw_start_object(w);
				jsonw_string_field(w, "name", name);
				if (btf_kflag(t))
					jsonw_int_field(w, "val", v->val);
				else
					jsonw_uint_field(w, "val", v->val);
				jsonw_end_object(w);
			} else {
				if (btf_kflag(t))
					printf("\n\t'%s' val=%d", name, v->val);
				else
					printf("\n\t'%s' val=%u", name, v->val);
			}
		}
		if (json_output)
			jsonw_end_array(w);
		break;
	}
	case BTF_KIND_ENUM64: {
		const struct btf_enum64 *v = btf_enum64(t);
		__u16 vlen = btf_vlen(t);
		const char *encoding;
		int i;

		encoding = btf_kflag(t) ? "SIGNED" : "UNSIGNED";
		if (json_output) {
			jsonw_string_field(w, "encoding", encoding);
			jsonw_uint_field(w, "size", t->size);
			jsonw_uint_field(w, "vlen", vlen);
			jsonw_name(w, "values");
			jsonw_start_array(w);
		} else {
			printf(" encoding=%s size=%u vlen=%u", encoding, t->size, vlen);
		}
		for (i = 0; i < vlen; i++, v++) {
			const char *name = btf_str(btf, v->name_off);
			__u64 val = ((__u64)v->val_hi32 << 32) | v->val_lo32;

			if (json_output) {
				jsonw_start_object(w);
				jsonw_string_field(w, "name", name);
				if (btf_kflag(t))
					jsonw_int_field(w, "val", val);
				else
					jsonw_uint_field(w, "val", val);
				jsonw_end_object(w);
			} else {
				if (btf_kflag(t))
					printf("\n\t'%s' val=%lldLL", name,
					       (long long)val);
				else
					printf("\n\t'%s' val=%lluULL", name,
					       (unsigned long long)val);
			}
		}
		if (json_output)
			jsonw_end_array(w);
		break;
	}
	case BTF_KIND_FWD: {
		const char *fwd_kind = BTF_INFO_KFLAG(t->info) ? "union"
							       : "struct";

		if (json_output)
			jsonw_string_field(w, "fwd_kind", fwd_kind);
		else
			printf(" fwd_kind=%s", fwd_kind);
		break;
	}
	case BTF_KIND_FUNC: {
		const char *linkage = btf_func_linkage_str(t);

		if (json_output) {
			jsonw_uint_field(w, "type_id", t->type);
			jsonw_string_field(w, "linkage", linkage);
		} else {
			printf(" type_id=%u linkage=%s", t->type, linkage);
		}
		break;
	}
	case BTF_KIND_FUNC_PROTO: {
		const struct btf_param *p = (const void *)(t + 1);
		__u16 vlen = BTF_INFO_VLEN(t->info);
		int i;

		if (json_output) {
			jsonw_uint_field(w, "ret_type_id", t->type);
			jsonw_uint_field(w, "vlen", vlen);
			jsonw_name(w, "params");
			jsonw_start_array(w);
		} else {
			printf(" ret_type_id=%u vlen=%u", t->type, vlen);
		}
		for (i = 0; i < vlen; i++, p++) {
			const char *name = btf_str(btf, p->name_off);

			if (json_output) {
				jsonw_start_object(w);
				jsonw_string_field(w, "name", name);
				jsonw_uint_field(w, "type_id", p->type);
				jsonw_end_object(w);
			} else {
				printf("\n\t'%s' type_id=%u", name, p->type);
			}
		}
		if (json_output)
			jsonw_end_array(w);
		break;
	}
	case BTF_KIND_VAR: {
		const struct btf_var *v = (const void *)(t + 1);
		const char *linkage;

		linkage = btf_var_linkage_str(v->linkage);

		if (json_output) {
			jsonw_uint_field(w, "type_id", t->type);
			jsonw_string_field(w, "linkage", linkage);
		} else {
			printf(" type_id=%u, linkage=%s", t->type, linkage);
		}
		break;
	}
	case BTF_KIND_DATASEC: {
		const struct btf_var_secinfo *v = (const void *)(t + 1);
		const struct btf_type *vt;
		__u16 vlen = BTF_INFO_VLEN(t->info);
		int i;

		if (json_output) {
			jsonw_uint_field(w, "size", t->size);
			jsonw_uint_field(w, "vlen", vlen);
			jsonw_name(w, "vars");
			jsonw_start_array(w);
		} else {
			printf(" size=%u vlen=%u", t->size, vlen);
		}
		for (i = 0; i < vlen; i++, v++) {
			if (json_output) {
				jsonw_start_object(w);
				jsonw_uint_field(w, "type_id", v->type);
				jsonw_uint_field(w, "offset", v->offset);
				jsonw_uint_field(w, "size", v->size);
				jsonw_end_object(w);
			} else {
				printf("\n\ttype_id=%u offset=%u size=%u",
				       v->type, v->offset, v->size);

				if (v->type < btf__type_cnt(btf)) {
					vt = btf__type_by_id(btf, v->type);
					printf(" (%s '%s')",
					       btf_kind_str[btf_kind_safe(btf_kind(vt))],
					       btf_str(btf, vt->name_off));
				}
			}
		}
		if (json_output)
			jsonw_end_array(w);
		break;
	}
	case BTF_KIND_FLOAT: {
		if (json_output)
			jsonw_uint_field(w, "size", t->size);
		else
			printf(" size=%u", t->size);
		break;
	}
	case BTF_KIND_DECL_TAG: {
		const struct btf_decl_tag *tag = (const void *)(t + 1);

		if (json_output) {
			jsonw_uint_field(w, "type_id", t->type);
			jsonw_int_field(w, "component_idx", tag->component_idx);
		} else {
			printf(" type_id=%u component_idx=%d", t->type, tag->component_idx);
		}
		break;
	}
	default:
		break;
	}

	if (json_output)
		jsonw_end_object(json_wtr);
	else
		printf("\n");

	return 0;
}

static int dump_btf_raw(const struct btf *btf,
			__u32 *root_type_ids, int root_type_cnt)
{
	const struct btf_type *t;
	int i;

	if (json_output) {
		jsonw_start_object(json_wtr);
		jsonw_name(json_wtr, "types");
		jsonw_start_array(json_wtr);
	}

	if (root_type_cnt) {
		for (i = 0; i < root_type_cnt; i++) {
			t = btf__type_by_id(btf, root_type_ids[i]);
			dump_btf_type(btf, root_type_ids[i], t);
		}
	} else {
		const struct btf *base;
		int cnt = btf__type_cnt(btf);
		int start_id = 1;

		base = btf__base_btf(btf);
		if (base)
			start_id = btf__type_cnt(base);

		for (i = start_id; i < cnt; i++) {
			t = btf__type_by_id(btf, i);
			dump_btf_type(btf, i, t);
		}
	}

	if (json_output) {
		jsonw_end_array(json_wtr);
		jsonw_end_object(json_wtr);
	}
	return 0;
}

struct ptr_array {
	__u32 cnt;
	__u32 cap;
	const void **elems;
};

static int ptr_array_push(const void *ptr, struct ptr_array *arr)
{
	__u32 new_cap;
	void *tmp;

	if (arr->cnt == arr->cap) {
		new_cap = (arr->cap ?: 16) * 2;
		tmp = realloc(arr->elems, sizeof(*arr->elems) * new_cap);
		if (!tmp)
			return -ENOMEM;
		arr->elems = tmp;
		arr->cap = new_cap;
	}
	arr->elems[arr->cnt++] = ptr;
	return 0;
}

static void ptr_array_free(struct ptr_array *arr)
{
	free(arr->elems);
}

static int cmp_kfuncs(const void *pa, const void *pb, void *ctx)
{
	struct btf *btf = ctx;
	const struct btf_type *a = *(void **)pa;
	const struct btf_type *b = *(void **)pb;

	return strcmp(btf__str_by_offset(btf, a->name_off),
		      btf__str_by_offset(btf, b->name_off));
}

static int dump_btf_kfuncs(struct btf_dump *d, const struct btf *btf)
{
	LIBBPF_OPTS(btf_dump_emit_type_decl_opts, opts);
	__u32 cnt = btf__type_cnt(btf), i, j;
	struct ptr_array fastcalls = {};
	struct ptr_array kfuncs = {};
	int err = 0;

	printf("\n/* BPF kfuncs */\n");
	printf("#ifndef BPF_NO_KFUNC_PROTOTYPES\n");

	for (i = 1; i < cnt; i++) {
		const struct btf_type *t = btf__type_by_id(btf, i);
		const struct btf_type *ft;
		const char *name;

		if (!btf_is_decl_tag(t))
			continue;

		if (btf_decl_tag(t)->component_idx != -1)
			continue;

		ft = btf__type_by_id(btf, t->type);
		if (!btf_is_func(ft))
			continue;

		name = btf__name_by_offset(btf, t->name_off);
		if (strncmp(name, KFUNC_DECL_TAG, sizeof(KFUNC_DECL_TAG)) == 0) {
			err = ptr_array_push(ft, &kfuncs);
			if (err)
				goto out;
		}

		if (strncmp(name, FASTCALL_DECL_TAG, sizeof(FASTCALL_DECL_TAG)) == 0) {
			err = ptr_array_push(ft, &fastcalls);
			if (err)
				goto out;
		}
	}

	/* Sort kfuncs by name for improved vmlinux.h stability  */
	qsort_r(kfuncs.elems, kfuncs.cnt, sizeof(*kfuncs.elems), cmp_kfuncs, (void *)btf);
	for (i = 0; i < kfuncs.cnt; i++) {
		const struct btf_type *t = kfuncs.elems[i];

		printf("extern ");

		/* Assume small amount of fastcall kfuncs */
		for (j = 0; j < fastcalls.cnt; j++) {
			if (fastcalls.elems[j] == t) {
				printf("__bpf_fastcall ");
				break;
			}
		}

		opts.field_name = btf__name_by_offset(btf, t->name_off);
		err = btf_dump__emit_type_decl(d, t->type, &opts);
		if (err)
			goto out;

		printf(" __weak __ksym;\n");
	}

	printf("#endif\n\n");

out:
	ptr_array_free(&fastcalls);
	ptr_array_free(&kfuncs);
	return err;
}

static void __printf(2, 0) btf_dump_printf(void *ctx,
					   const char *fmt, va_list args)
{
	vfprintf(stdout, fmt, args);
}

static int btf_type_rank(const struct btf *btf, __u32 index, bool has_name)
{
	const struct btf_type *t = btf__type_by_id(btf, index);
	const int kind = btf_kind(t);
	const int max_rank = 10;

	if (t->name_off)
		has_name = true;

	switch (kind) {
	case BTF_KIND_ENUM:
	case BTF_KIND_ENUM64:
		return has_name ? 1 : 0;
	case BTF_KIND_INT:
	case BTF_KIND_FLOAT:
		return 2;
	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION:
		return has_name ? 3 : max_rank;
	case BTF_KIND_FUNC_PROTO:
		return has_name ? 4 : max_rank;
	case BTF_KIND_ARRAY:
		if (has_name)
			return btf_type_rank(btf, btf_array(t)->type, has_name);
		return max_rank;
	case BTF_KIND_TYPE_TAG:
	case BTF_KIND_CONST:
	case BTF_KIND_PTR:
	case BTF_KIND_VOLATILE:
	case BTF_KIND_RESTRICT:
	case BTF_KIND_TYPEDEF:
	case BTF_KIND_DECL_TAG:
		if (has_name)
			return btf_type_rank(btf, t->type, has_name);
		return max_rank;
	default:
		return max_rank;
	}
}

static const char *btf_type_sort_name(const struct btf *btf, __u32 index, bool from_ref)
{
	const struct btf_type *t = btf__type_by_id(btf, index);

	switch (btf_kind(t)) {
	case BTF_KIND_ENUM:
	case BTF_KIND_ENUM64: {
		int name_off = t->name_off;

		if (!from_ref && !name_off && btf_vlen(t))
			name_off = btf_kind(t) == BTF_KIND_ENUM64 ?
				btf_enum64(t)->name_off :
				btf_enum(t)->name_off;

		return btf__name_by_offset(btf, name_off);
	}
	case BTF_KIND_ARRAY:
		return btf_type_sort_name(btf, btf_array(t)->type, true);
	case BTF_KIND_TYPE_TAG:
	case BTF_KIND_CONST:
	case BTF_KIND_PTR:
	case BTF_KIND_VOLATILE:
	case BTF_KIND_RESTRICT:
	case BTF_KIND_TYPEDEF:
	case BTF_KIND_DECL_TAG:
		return btf_type_sort_name(btf, t->type, true);
	default:
		return btf__name_by_offset(btf, t->name_off);
	}
	return NULL;
}

static __u64 hasher(__u64 hash, __u64 val)
{
	return hash * 31 + val;
}

static __u64 btf_name_hasher(__u64 hash, const struct btf *btf, __u32 name_off)
{
	if (!name_off)
		return hash;

	return hasher(hash, str_hash(btf__name_by_offset(btf, name_off)));
}

static __u64 btf_type_disambig_hash(const struct btf *btf, __u32 id, bool include_members)
{
	const struct btf_type *t = btf__type_by_id(btf, id);
	int i;
	size_t hash = 0;

	hash = btf_name_hasher(hash, btf, t->name_off);

	switch (btf_kind(t)) {
	case BTF_KIND_ENUM:
	case BTF_KIND_ENUM64:
		for (i = 0; i < btf_vlen(t); i++) {
			__u32 name_off = btf_is_enum(t) ?
				btf_enum(t)[i].name_off :
				btf_enum64(t)[i].name_off;

			hash = btf_name_hasher(hash, btf, name_off);
		}
		break;
	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION:
		if (!include_members)
			break;
		for (i = 0; i < btf_vlen(t); i++) {
			const struct btf_member *m = btf_members(t) + i;

			hash = btf_name_hasher(hash, btf, m->name_off);
			/* resolve field type's name and hash it as well */
			hash = hasher(hash, btf_type_disambig_hash(btf, m->type, false));
		}
		break;
	case BTF_KIND_TYPE_TAG:
	case BTF_KIND_CONST:
	case BTF_KIND_PTR:
	case BTF_KIND_VOLATILE:
	case BTF_KIND_RESTRICT:
	case BTF_KIND_TYPEDEF:
	case BTF_KIND_DECL_TAG:
		hash = hasher(hash, btf_type_disambig_hash(btf, t->type, include_members));
		break;
	case BTF_KIND_ARRAY: {
		struct btf_array *arr = btf_array(t);

		hash = hasher(hash, arr->nelems);
		hash = hasher(hash, btf_type_disambig_hash(btf, arr->type, include_members));
		break;
	}
	default:
		break;
	}
	return hash;
}

static int btf_type_compare(const void *left, const void *right)
{
	const struct sort_datum *d1 = (const struct sort_datum *)left;
	const struct sort_datum *d2 = (const struct sort_datum *)right;
	int r;

	r = d1->type_rank - d2->type_rank;
	r = r ?: strcmp(d1->sort_name, d2->sort_name);
	r = r ?: strcmp(d1->own_name, d2->own_name);
	if (r)
		return r;

	if (d1->disambig_hash != d2->disambig_hash)
		return d1->disambig_hash < d2->disambig_hash ? -1 : 1;

	return d1->index - d2->index;
}

static struct sort_datum *sort_btf_c(const struct btf *btf)
{
	struct sort_datum *datums;
	int n;

	n = btf__type_cnt(btf);
	datums = malloc(sizeof(struct sort_datum) * n);
	if (!datums)
		return NULL;

	for (int i = 0; i < n; ++i) {
		struct sort_datum *d = datums + i;
		const struct btf_type *t = btf__type_by_id(btf, i);

		d->index = i;
		d->type_rank = btf_type_rank(btf, i, false);
		d->sort_name = btf_type_sort_name(btf, i, false);
		d->own_name = btf__name_by_offset(btf, t->name_off);
		d->disambig_hash = btf_type_disambig_hash(btf, i, true);
	}

	qsort(datums, n, sizeof(struct sort_datum), btf_type_compare);

	return datums;
}

static int dump_btf_c(const struct btf *btf,
		      __u32 *root_type_ids, int root_type_cnt, bool sort_dump)
{
	struct sort_datum *datums = NULL;
	struct btf_dump *d;
	int err = 0, i;

	d = btf_dump__new(btf, btf_dump_printf, NULL, NULL);
	if (!d)
		return -errno;

	printf("#ifndef __VMLINUX_H__\n");
	printf("#define __VMLINUX_H__\n");
	printf("\n");
	printf("#ifndef BPF_NO_PRESERVE_ACCESS_INDEX\n");
	printf("#pragma clang attribute push (__attribute__((preserve_access_index)), apply_to = record)\n");
	printf("#endif\n\n");
	printf("#ifndef __ksym\n");
	printf("#define __ksym __attribute__((section(\".ksyms\")))\n");
	printf("#endif\n\n");
	printf("#ifndef __weak\n");
	printf("#define __weak __attribute__((weak))\n");
	printf("#endif\n\n");
	printf("#ifndef __bpf_fastcall\n");
	printf("#if __has_attribute(bpf_fastcall)\n");
	printf("#define __bpf_fastcall __attribute__((bpf_fastcall))\n");
	printf("#else\n");
	printf("#define __bpf_fastcall\n");
	printf("#endif\n");
	printf("#endif\n\n");

	if (root_type_cnt) {
		for (i = 0; i < root_type_cnt; i++) {
			err = btf_dump__dump_type(d, root_type_ids[i]);
			if (err)
				goto done;
		}
	} else {
		int cnt = btf__type_cnt(btf);

		if (sort_dump)
			datums = sort_btf_c(btf);
		for (i = 1; i < cnt; i++) {
			int idx = datums ? datums[i].index : i;

			err = btf_dump__dump_type(d, idx);
			if (err)
				goto done;
		}

		err = dump_btf_kfuncs(d, btf);
		if (err)
			goto done;
	}

	printf("#ifndef BPF_NO_PRESERVE_ACCESS_INDEX\n");
	printf("#pragma clang attribute pop\n");
	printf("#endif\n");
	printf("\n");
	printf("#endif /* __VMLINUX_H__ */\n");

done:
	free(datums);
	btf_dump__free(d);
	return err;
}

static const char sysfs_vmlinux[] = "/sys/kernel/btf/vmlinux";

static struct btf *get_vmlinux_btf_from_sysfs(void)
{
	struct btf *base;

	base = btf__parse(sysfs_vmlinux, NULL);
	if (!base)
		p_err("failed to parse vmlinux BTF at '%s': %d\n",
		      sysfs_vmlinux, -errno);

	return base;
}

#define BTF_NAME_BUFF_LEN 64

static bool btf_is_kernel_module(__u32 btf_id)
{
	struct bpf_btf_info btf_info = {};
	char btf_name[BTF_NAME_BUFF_LEN];
	int btf_fd;
	__u32 len;
	int err;

	btf_fd = bpf_btf_get_fd_by_id(btf_id);
	if (btf_fd < 0) {
		p_err("can't get BTF object by id (%u): %s", btf_id, strerror(errno));
		return false;
	}

	len = sizeof(btf_info);
	btf_info.name = ptr_to_u64(btf_name);
	btf_info.name_len = sizeof(btf_name);
	err = bpf_btf_get_info_by_fd(btf_fd, &btf_info, &len);
	close(btf_fd);
	if (err) {
		p_err("can't get BTF (ID %u) object info: %s", btf_id, strerror(errno));
		return false;
	}

	return btf_info.kernel_btf && strncmp(btf_name, "vmlinux", sizeof(btf_name)) != 0;
}

static int do_dump(int argc, char **argv)
{
	bool dump_c = false, sort_dump_c = true;
	struct btf *btf = NULL, *base = NULL;
	__u32 root_type_ids[2];
	int root_type_cnt = 0;
	__u32 btf_id = -1;
	const char *src;
	int fd = -1;
	int err = 0;

	if (!REQ_ARGS(2)) {
		usage();
		return -1;
	}
	src = GET_ARG();
	if (is_prefix(src, "map")) {
		struct bpf_map_info info = {};
		__u32 len = sizeof(info);

		if (!REQ_ARGS(2)) {
			usage();
			return -1;
		}

		fd = map_parse_fd_and_info(&argc, &argv, &info, &len);
		if (fd < 0)
			return -1;

		btf_id = info.btf_id;
		if (argc && is_prefix(*argv, "key")) {
			root_type_ids[root_type_cnt++] = info.btf_key_type_id;
			NEXT_ARG();
		} else if (argc && is_prefix(*argv, "value")) {
			root_type_ids[root_type_cnt++] = info.btf_value_type_id;
			NEXT_ARG();
		} else if (argc && is_prefix(*argv, "all")) {
			NEXT_ARG();
		} else if (argc && is_prefix(*argv, "kv")) {
			root_type_ids[root_type_cnt++] = info.btf_key_type_id;
			root_type_ids[root_type_cnt++] = info.btf_value_type_id;
			NEXT_ARG();
		} else {
			root_type_ids[root_type_cnt++] = info.btf_key_type_id;
			root_type_ids[root_type_cnt++] = info.btf_value_type_id;
		}
	} else if (is_prefix(src, "prog")) {
		struct bpf_prog_info info = {};
		__u32 len = sizeof(info);

		if (!REQ_ARGS(2)) {
			usage();
			return -1;
		}

		fd = prog_parse_fd(&argc, &argv);
		if (fd < 0)
			return -1;

		err = bpf_prog_get_info_by_fd(fd, &info, &len);
		if (err) {
			p_err("can't get prog info: %s", strerror(errno));
			goto done;
		}

		btf_id = info.btf_id;
	} else if (is_prefix(src, "id")) {
		char *endptr;

		btf_id = strtoul(*argv, &endptr, 0);
		if (*endptr) {
			p_err("can't parse %s as ID", *argv);
			return -1;
		}
		NEXT_ARG();
	} else if (is_prefix(src, "file")) {
		const char sysfs_prefix[] = "/sys/kernel/btf/";

		if (!base_btf &&
		    strncmp(*argv, sysfs_prefix, sizeof(sysfs_prefix) - 1) == 0 &&
		    strcmp(*argv, sysfs_vmlinux) != 0)
			base = get_vmlinux_btf_from_sysfs();

		btf = btf__parse_split(*argv, base ?: base_btf);
		if (!btf) {
			err = -errno;
			p_err("failed to load BTF from %s: %s",
			      *argv, strerror(errno));
			goto done;
		}
		NEXT_ARG();
	} else {
		err = -1;
		p_err("unrecognized BTF source specifier: '%s'", src);
		goto done;
	}

	while (argc) {
		if (is_prefix(*argv, "format")) {
			NEXT_ARG();
			if (argc < 1) {
				p_err("expecting value for 'format' option\n");
				err = -EINVAL;
				goto done;
			}
			if (strcmp(*argv, "c") == 0) {
				dump_c = true;
			} else if (strcmp(*argv, "raw") == 0) {
				dump_c = false;
			} else {
				p_err("unrecognized format specifier: '%s', possible values: raw, c",
				      *argv);
				err = -EINVAL;
				goto done;
			}
			NEXT_ARG();
		} else if (is_prefix(*argv, "unsorted")) {
			sort_dump_c = false;
			NEXT_ARG();
		} else {
			p_err("unrecognized option: '%s'", *argv);
			err = -EINVAL;
			goto done;
		}
	}

	if (!btf) {
		if (!base_btf && btf_is_kernel_module(btf_id)) {
			p_info("Warning: valid base BTF was not specified with -B option, falling back to standard base BTF (%s)",
			       sysfs_vmlinux);
			base_btf = get_vmlinux_btf_from_sysfs();
		}

		btf = btf__load_from_kernel_by_id_split(btf_id, base_btf);
		if (!btf) {
			err = -errno;
			p_err("get btf by id (%u): %s", btf_id, strerror(errno));
			goto done;
		}
	}

	if (dump_c) {
		if (json_output) {
			p_err("JSON output for C-syntax dump is not supported");
			err = -ENOTSUP;
			goto done;
		}
		err = dump_btf_c(btf, root_type_ids, root_type_cnt, sort_dump_c);
	} else {
		err = dump_btf_raw(btf, root_type_ids, root_type_cnt);
	}

done:
	close(fd);
	btf__free(btf);
	btf__free(base);
	return err;
}

static int btf_parse_fd(int *argc, char ***argv)
{
	unsigned int id;
	char *endptr;
	int fd;

	if (!is_prefix(*argv[0], "id")) {
		p_err("expected 'id', got: '%s'?", **argv);
		return -1;
	}
	NEXT_ARGP();

	id = strtoul(**argv, &endptr, 0);
	if (*endptr) {
		p_err("can't parse %s as ID", **argv);
		return -1;
	}
	NEXT_ARGP();

	fd = bpf_btf_get_fd_by_id(id);
	if (fd < 0)
		p_err("can't get BTF object by id (%u): %s",
		      id, strerror(errno));

	return fd;
}

static int
build_btf_type_table(struct hashmap *tab, enum bpf_obj_type type,
		     void *info, __u32 *len)
{
	static const char * const names[] = {
		[BPF_OBJ_UNKNOWN]	= "unknown",
		[BPF_OBJ_PROG]		= "prog",
		[BPF_OBJ_MAP]		= "map",
	};
	__u32 btf_id, id = 0;
	int err;
	int fd;

	while (true) {
		switch (type) {
		case BPF_OBJ_PROG:
			err = bpf_prog_get_next_id(id, &id);
			break;
		case BPF_OBJ_MAP:
			err = bpf_map_get_next_id(id, &id);
			break;
		default:
			err = -1;
			p_err("unexpected object type: %d", type);
			goto err_free;
		}
		if (err) {
			if (errno == ENOENT) {
				err = 0;
				break;
			}
			p_err("can't get next %s: %s%s", names[type],
			      strerror(errno),
			      errno == EINVAL ? " -- kernel too old?" : "");
			goto err_free;
		}

		switch (type) {
		case BPF_OBJ_PROG:
			fd = bpf_prog_get_fd_by_id(id);
			break;
		case BPF_OBJ_MAP:
			fd = bpf_map_get_fd_by_id(id);
			break;
		default:
			err = -1;
			p_err("unexpected object type: %d", type);
			goto err_free;
		}
		if (fd < 0) {
			if (errno == ENOENT)
				continue;
			p_err("can't get %s by id (%u): %s", names[type], id,
			      strerror(errno));
			err = -1;
			goto err_free;
		}

		memset(info, 0, *len);
		if (type == BPF_OBJ_PROG)
			err = bpf_prog_get_info_by_fd(fd, info, len);
		else
			err = bpf_map_get_info_by_fd(fd, info, len);
		close(fd);
		if (err) {
			p_err("can't get %s info: %s", names[type],
			      strerror(errno));
			goto err_free;
		}

		switch (type) {
		case BPF_OBJ_PROG:
			btf_id = ((struct bpf_prog_info *)info)->btf_id;
			break;
		case BPF_OBJ_MAP:
			btf_id = ((struct bpf_map_info *)info)->btf_id;
			break;
		default:
			err = -1;
			p_err("unexpected object type: %d", type);
			goto err_free;
		}
		if (!btf_id)
			continue;

		err = hashmap__append(tab, btf_id, id);
		if (err) {
			p_err("failed to append entry to hashmap for BTF ID %u, object ID %u: %s",
			      btf_id, id, strerror(-err));
			goto err_free;
		}
	}

	return 0;

err_free:
	hashmap__free(tab);
	return err;
}

static int
build_btf_tables(struct hashmap *btf_prog_table,
		 struct hashmap *btf_map_table)
{
	struct bpf_prog_info prog_info;
	__u32 prog_len = sizeof(prog_info);
	struct bpf_map_info map_info;
	__u32 map_len = sizeof(map_info);
	int err = 0;

	err = build_btf_type_table(btf_prog_table, BPF_OBJ_PROG, &prog_info,
				   &prog_len);
	if (err)
		return err;

	err = build_btf_type_table(btf_map_table, BPF_OBJ_MAP, &map_info,
				   &map_len);
	if (err) {
		hashmap__free(btf_prog_table);
		return err;
	}

	return 0;
}

static void
show_btf_plain(struct bpf_btf_info *info, int fd,
	       struct hashmap *btf_prog_table,
	       struct hashmap *btf_map_table)
{
	struct hashmap_entry *entry;
	const char *name = u64_to_ptr(info->name);
	int n;

	printf("%u: ", info->id);
	if (info->kernel_btf)
		printf("name [%s]  ", name);
	else if (name && name[0])
		printf("name %s  ", name);
	else
		printf("name <anon>  ");
	printf("size %uB", info->btf_size);

	n = 0;
	hashmap__for_each_key_entry(btf_prog_table, entry, info->id) {
		printf("%s%lu", n++ == 0 ? "  prog_ids " : ",", entry->value);
	}

	n = 0;
	hashmap__for_each_key_entry(btf_map_table, entry, info->id) {
		printf("%s%lu", n++ == 0 ? "  map_ids " : ",", entry->value);
	}

	emit_obj_refs_plain(refs_table, info->id, "\n\tpids ");

	printf("\n");
}

static void
show_btf_json(struct bpf_btf_info *info, int fd,
	      struct hashmap *btf_prog_table,
	      struct hashmap *btf_map_table)
{
	struct hashmap_entry *entry;
	const char *name = u64_to_ptr(info->name);

	jsonw_start_object(json_wtr);	/* btf object */
	jsonw_uint_field(json_wtr, "id", info->id);
	jsonw_uint_field(json_wtr, "size", info->btf_size);

	jsonw_name(json_wtr, "prog_ids");
	jsonw_start_array(json_wtr);	/* prog_ids */
	hashmap__for_each_key_entry(btf_prog_table, entry, info->id) {
		jsonw_uint(json_wtr, entry->value);
	}
	jsonw_end_array(json_wtr);	/* prog_ids */

	jsonw_name(json_wtr, "map_ids");
	jsonw_start_array(json_wtr);	/* map_ids */
	hashmap__for_each_key_entry(btf_map_table, entry, info->id) {
		jsonw_uint(json_wtr, entry->value);
	}
	jsonw_end_array(json_wtr);	/* map_ids */

	emit_obj_refs_json(refs_table, info->id, json_wtr); /* pids */

	jsonw_bool_field(json_wtr, "kernel", info->kernel_btf);

	if (name && name[0])
		jsonw_string_field(json_wtr, "name", name);

	jsonw_end_object(json_wtr);	/* btf object */
}

static int
show_btf(int fd, struct hashmap *btf_prog_table,
	 struct hashmap *btf_map_table)
{
	struct bpf_btf_info info;
	__u32 len = sizeof(info);
	char name[64];
	int err;

	memset(&info, 0, sizeof(info));
	err = bpf_btf_get_info_by_fd(fd, &info, &len);
	if (err) {
		p_err("can't get BTF object info: %s", strerror(errno));
		return -1;
	}
	/* if kernel support emitting BTF object name, pass name pointer */
	if (info.name_len) {
		memset(&info, 0, sizeof(info));
		info.name_len = sizeof(name);
		info.name = ptr_to_u64(name);
		len = sizeof(info);

		err = bpf_btf_get_info_by_fd(fd, &info, &len);
		if (err) {
			p_err("can't get BTF object info: %s", strerror(errno));
			return -1;
		}
	}

	if (json_output)
		show_btf_json(&info, fd, btf_prog_table, btf_map_table);
	else
		show_btf_plain(&info, fd, btf_prog_table, btf_map_table);

	return 0;
}

static int do_show(int argc, char **argv)
{
	struct hashmap *btf_prog_table;
	struct hashmap *btf_map_table;
	int err, fd = -1;
	__u32 id = 0;

	if (argc == 2) {
		fd = btf_parse_fd(&argc, &argv);
		if (fd < 0)
			return -1;
	}

	if (argc) {
		if (fd >= 0)
			close(fd);
		return BAD_ARG();
	}

	btf_prog_table = hashmap__new(hash_fn_for_key_as_id,
				      equal_fn_for_key_as_id, NULL);
	btf_map_table = hashmap__new(hash_fn_for_key_as_id,
				     equal_fn_for_key_as_id, NULL);
	if (IS_ERR(btf_prog_table) || IS_ERR(btf_map_table)) {
		hashmap__free(btf_prog_table);
		hashmap__free(btf_map_table);
		if (fd >= 0)
			close(fd);
		p_err("failed to create hashmap for object references");
		return -1;
	}
	err = build_btf_tables(btf_prog_table, btf_map_table);
	if (err) {
		if (fd >= 0)
			close(fd);
		return err;
	}
	build_obj_refs_table(&refs_table, BPF_OBJ_BTF);

	if (fd >= 0) {
		err = show_btf(fd, btf_prog_table, btf_map_table);
		close(fd);
		goto exit_free;
	}

	if (json_output)
		jsonw_start_array(json_wtr);	/* root array */

	while (true) {
		err = bpf_btf_get_next_id(id, &id);
		if (err) {
			if (errno == ENOENT) {
				err = 0;
				break;
			}
			p_err("can't get next BTF object: %s%s",
			      strerror(errno),
			      errno == EINVAL ? " -- kernel too old?" : "");
			err = -1;
			break;
		}

		fd = bpf_btf_get_fd_by_id(id);
		if (fd < 0) {
			if (errno == ENOENT)
				continue;
			p_err("can't get BTF object by id (%u): %s",
			      id, strerror(errno));
			err = -1;
			break;
		}

		err = show_btf(fd, btf_prog_table, btf_map_table);
		close(fd);
		if (err)
			break;
	}

	if (json_output)
		jsonw_end_array(json_wtr);	/* root array */

exit_free:
	hashmap__free(btf_prog_table);
	hashmap__free(btf_map_table);
	delete_obj_refs_table(refs_table);

	return err;
}

static int do_help(int argc, char **argv)
{
	if (json_output) {
		jsonw_null(json_wtr);
		return 0;
	}

	fprintf(stderr,
		"Usage: %1$s %2$s { show | list } [id BTF_ID]\n"
		"       %1$s %2$s dump BTF_SRC [format FORMAT]\n"
		"       %1$s %2$s help\n"
		"\n"
		"       BTF_SRC := { id BTF_ID | prog PROG | map MAP [{key | value | kv | all}] | file FILE }\n"
		"       FORMAT  := { raw | c [unsorted] }\n"
		"       " HELP_SPEC_MAP "\n"
		"       " HELP_SPEC_PROGRAM "\n"
		"       " HELP_SPEC_OPTIONS " |\n"
		"                    {-B|--base-btf} }\n"
		"",
		bin_name, "btf");

	return 0;
}

static const struct cmd cmds[] = {
	{ "show",	do_show },
	{ "list",	do_show },
	{ "help",	do_help },
	{ "dump",	do_dump },
	{ 0 }
};

int do_btf(int argc, char **argv)
{
	return cmd_select(cmds, argc, argv, do_help);
}
