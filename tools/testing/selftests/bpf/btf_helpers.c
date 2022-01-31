// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include <stdio.h>
#include <errno.h>
#include <bpf/btf.h>
#include <bpf/libbpf.h>
#include "test_progs.h"

static const char * const btf_kind_str_mapping[] = {
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
};

static const char *btf_kind_str(__u16 kind)
{
	if (kind > BTF_KIND_DATASEC)
		return "UNKNOWN";
	return btf_kind_str_mapping[kind];
}

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
		return "global-alloc";
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
	return btf__str_by_offset(btf, off) ?: "(invalid)";
}

int fprintf_btf_type_raw(FILE *out, const struct btf *btf, __u32 id)
{
	const struct btf_type *t;
	int kind, i;
	__u32 vlen;

	t = btf__type_by_id(btf, id);
	if (!t)
		return -EINVAL;

	vlen = btf_vlen(t);
	kind = btf_kind(t);

	fprintf(out, "[%u] %s '%s'", id, btf_kind_str(kind), btf_str(btf, t->name_off));

	switch (kind) {
	case BTF_KIND_INT:
		fprintf(out, " size=%u bits_offset=%u nr_bits=%u encoding=%s",
			t->size, btf_int_offset(t), btf_int_bits(t),
			btf_int_enc_str(btf_int_encoding(t)));
		break;
	case BTF_KIND_PTR:
	case BTF_KIND_CONST:
	case BTF_KIND_VOLATILE:
	case BTF_KIND_RESTRICT:
	case BTF_KIND_TYPEDEF:
		fprintf(out, " type_id=%u", t->type);
		break;
	case BTF_KIND_ARRAY: {
		const struct btf_array *arr = btf_array(t);

		fprintf(out, " type_id=%u index_type_id=%u nr_elems=%u",
			arr->type, arr->index_type, arr->nelems);
		break;
	}
	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION: {
		const struct btf_member *m = btf_members(t);

		fprintf(out, " size=%u vlen=%u", t->size, vlen);
		for (i = 0; i < vlen; i++, m++) {
			__u32 bit_off, bit_sz;

			bit_off = btf_member_bit_offset(t, i);
			bit_sz = btf_member_bitfield_size(t, i);
			fprintf(out, "\n\t'%s' type_id=%u bits_offset=%u",
				btf_str(btf, m->name_off), m->type, bit_off);
			if (bit_sz)
				fprintf(out, " bitfield_size=%u", bit_sz);
		}
		break;
	}
	case BTF_KIND_ENUM: {
		const struct btf_enum *v = btf_enum(t);

		fprintf(out, " size=%u vlen=%u", t->size, vlen);
		for (i = 0; i < vlen; i++, v++) {
			fprintf(out, "\n\t'%s' val=%u",
				btf_str(btf, v->name_off), v->val);
		}
		break;
	}
	case BTF_KIND_FWD:
		fprintf(out, " fwd_kind=%s", btf_kflag(t) ? "union" : "struct");
		break;
	case BTF_KIND_FUNC:
		fprintf(out, " type_id=%u linkage=%s", t->type, btf_func_linkage_str(t));
		break;
	case BTF_KIND_FUNC_PROTO: {
		const struct btf_param *p = btf_params(t);

		fprintf(out, " ret_type_id=%u vlen=%u", t->type, vlen);
		for (i = 0; i < vlen; i++, p++) {
			fprintf(out, "\n\t'%s' type_id=%u",
				btf_str(btf, p->name_off), p->type);
		}
		break;
	}
	case BTF_KIND_VAR:
		fprintf(out, " type_id=%u, linkage=%s",
			t->type, btf_var_linkage_str(btf_var(t)->linkage));
		break;
	case BTF_KIND_DATASEC: {
		const struct btf_var_secinfo *v = btf_var_secinfos(t);

		fprintf(out, " size=%u vlen=%u", t->size, vlen);
		for (i = 0; i < vlen; i++, v++) {
			fprintf(out, "\n\ttype_id=%u offset=%u size=%u",
				v->type, v->offset, v->size);
		}
		break;
	}
	case BTF_KIND_FLOAT:
		fprintf(out, " size=%u", t->size);
		break;
	default:
		break;
	}

	return 0;
}

/* Print raw BTF type dump into a local buffer and return string pointer back.
 * Buffer *will* be overwritten by subsequent btf_type_raw_dump() calls
 */
const char *btf_type_raw_dump(const struct btf *btf, int type_id)
{
	static char buf[16 * 1024];
	FILE *buf_file;

	buf_file = fmemopen(buf, sizeof(buf) - 1, "w");
	if (!buf_file) {
		fprintf(stderr, "Failed to open memstream: %d\n", errno);
		return NULL;
	}

	fprintf_btf_type_raw(buf_file, btf, type_id);
	fflush(buf_file);
	fclose(buf_file);

	return buf;
}

int btf_validate_raw(struct btf *btf, int nr_types, const char *exp_types[])
{
	int i;
	bool ok = true;

	ASSERT_EQ(btf__get_nr_types(btf), nr_types, "btf_nr_types");

	for (i = 1; i <= nr_types; i++) {
		if (!ASSERT_STREQ(btf_type_raw_dump(btf, i), exp_types[i - 1], "raw_dump"))
			ok = false;
	}

	return ok;
}

static void btf_dump_printf(void *ctx, const char *fmt, va_list args)
{
	vfprintf(ctx, fmt, args);
}

/* Print BTF-to-C dump into a local buffer and return string pointer back.
 * Buffer *will* be overwritten by subsequent btf_type_raw_dump() calls
 */
const char *btf_type_c_dump(const struct btf *btf)
{
	static char buf[16 * 1024];
	FILE *buf_file;
	struct btf_dump *d = NULL;
	struct btf_dump_opts opts = {};
	int err, i;

	buf_file = fmemopen(buf, sizeof(buf) - 1, "w");
	if (!buf_file) {
		fprintf(stderr, "Failed to open memstream: %d\n", errno);
		return NULL;
	}

	opts.ctx = buf_file;
	d = btf_dump__new(btf, NULL, &opts, btf_dump_printf);
	if (libbpf_get_error(d)) {
		fprintf(stderr, "Failed to create btf_dump instance: %ld\n", libbpf_get_error(d));
		goto err_out;
	}

	for (i = 1; i <= btf__get_nr_types(btf); i++) {
		err = btf_dump__dump_type(d, i);
		if (err) {
			fprintf(stderr, "Failed to dump type [%d]: %d\n", i, err);
			goto err_out;
		}
	}

	btf_dump__free(d);
	fflush(buf_file);
	fclose(buf_file);
	return buf;
err_out:
	btf_dump__free(d);
	fclose(buf_file);
	return NULL;
}
