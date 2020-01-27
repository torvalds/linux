// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018 Facebook */

#include <ctype.h>
#include <stdio.h> /* for (FILE *) used by json_writer */
#include <string.h>
#include <asm/byteorder.h>
#include <linux/bitops.h>
#include <linux/btf.h>
#include <linux/err.h>

#include "btf.h"
#include "json_writer.h"
#include "main.h"

#define BITS_PER_BYTE_MASK (BITS_PER_BYTE - 1)
#define BITS_PER_BYTE_MASKED(bits) ((bits) & BITS_PER_BYTE_MASK)
#define BITS_ROUNDDOWN_BYTES(bits) ((bits) >> 3)
#define BITS_ROUNDUP_BYTES(bits) \
	(BITS_ROUNDDOWN_BYTES(bits) + !!BITS_PER_BYTE_MASKED(bits))

static int btf_dumper_do_type(const struct btf_dumper *d, __u32 type_id,
			      __u8 bit_offset, const void *data);

static void btf_dumper_ptr(const void *data, json_writer_t *jw,
			   bool is_plain_text)
{
	if (is_plain_text)
		jsonw_printf(jw, "%p", data);
	else
		jsonw_printf(jw, "%lu", *(unsigned long *)data);
}

static int btf_dumper_modifier(const struct btf_dumper *d, __u32 type_id,
			       __u8 bit_offset, const void *data)
{
	int actual_type_id;

	actual_type_id = btf__resolve_type(d->btf, type_id);
	if (actual_type_id < 0)
		return actual_type_id;

	return btf_dumper_do_type(d, actual_type_id, bit_offset, data);
}

static void btf_dumper_enum(const void *data, json_writer_t *jw)
{
	jsonw_printf(jw, "%d", *(int *)data);
}

static int btf_dumper_array(const struct btf_dumper *d, __u32 type_id,
			    const void *data)
{
	const struct btf_type *t = btf__type_by_id(d->btf, type_id);
	struct btf_array *arr = (struct btf_array *)(t + 1);
	long long elem_size;
	int ret = 0;
	__u32 i;

	elem_size = btf__resolve_size(d->btf, arr->type);
	if (elem_size < 0)
		return elem_size;

	jsonw_start_array(d->jw);
	for (i = 0; i < arr->nelems; i++) {
		ret = btf_dumper_do_type(d, arr->type, 0,
					 data + i * elem_size);
		if (ret)
			break;
	}

	jsonw_end_array(d->jw);
	return ret;
}

static void btf_dumper_int_bits(__u32 int_type, __u8 bit_offset,
				const void *data, json_writer_t *jw,
				bool is_plain_text)
{
	int left_shift_bits, right_shift_bits;
	int nr_bits = BTF_INT_BITS(int_type);
	int total_bits_offset;
	int bytes_to_copy;
	int bits_to_copy;
	__u64 print_num;

	total_bits_offset = bit_offset + BTF_INT_OFFSET(int_type);
	data += BITS_ROUNDDOWN_BYTES(total_bits_offset);
	bit_offset = BITS_PER_BYTE_MASKED(total_bits_offset);
	bits_to_copy = bit_offset + nr_bits;
	bytes_to_copy = BITS_ROUNDUP_BYTES(bits_to_copy);

	print_num = 0;
	memcpy(&print_num, data, bytes_to_copy);
#if defined(__BIG_ENDIAN_BITFIELD)
	left_shift_bits = bit_offset;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	left_shift_bits = 64 - bits_to_copy;
#else
#error neither big nor little endian
#endif
	right_shift_bits = 64 - nr_bits;

	print_num <<= left_shift_bits;
	print_num >>= right_shift_bits;
	if (is_plain_text)
		jsonw_printf(jw, "0x%llx", print_num);
	else
		jsonw_printf(jw, "%llu", print_num);
}

static int btf_dumper_int(const struct btf_type *t, __u8 bit_offset,
			  const void *data, json_writer_t *jw,
			  bool is_plain_text)
{
	__u32 *int_type;
	__u32 nr_bits;

	int_type = (__u32 *)(t + 1);
	nr_bits = BTF_INT_BITS(*int_type);
	/* if this is bit field */
	if (bit_offset || BTF_INT_OFFSET(*int_type) ||
	    BITS_PER_BYTE_MASKED(nr_bits)) {
		btf_dumper_int_bits(*int_type, bit_offset, data, jw,
				    is_plain_text);
		return 0;
	}

	switch (BTF_INT_ENCODING(*int_type)) {
	case 0:
		if (BTF_INT_BITS(*int_type) == 64)
			jsonw_printf(jw, "%llu", *(__u64 *)data);
		else if (BTF_INT_BITS(*int_type) == 32)
			jsonw_printf(jw, "%u", *(__u32 *)data);
		else if (BTF_INT_BITS(*int_type) == 16)
			jsonw_printf(jw, "%hu", *(__u16 *)data);
		else if (BTF_INT_BITS(*int_type) == 8)
			jsonw_printf(jw, "%hhu", *(__u8 *)data);
		else
			btf_dumper_int_bits(*int_type, bit_offset, data, jw,
					    is_plain_text);
		break;
	case BTF_INT_SIGNED:
		if (BTF_INT_BITS(*int_type) == 64)
			jsonw_printf(jw, "%lld", *(long long *)data);
		else if (BTF_INT_BITS(*int_type) == 32)
			jsonw_printf(jw, "%d", *(int *)data);
		else if (BTF_INT_BITS(*int_type) == 16)
			jsonw_printf(jw, "%hd", *(short *)data);
		else if (BTF_INT_BITS(*int_type) == 8)
			jsonw_printf(jw, "%hhd", *(char *)data);
		else
			btf_dumper_int_bits(*int_type, bit_offset, data, jw,
					    is_plain_text);
		break;
	case BTF_INT_CHAR:
		if (isprint(*(char *)data))
			jsonw_printf(jw, "\"%c\"", *(char *)data);
		else
			if (is_plain_text)
				jsonw_printf(jw, "0x%hhx", *(char *)data);
			else
				jsonw_printf(jw, "\"\\u00%02hhx\"",
					     *(char *)data);
		break;
	case BTF_INT_BOOL:
		jsonw_bool(jw, *(int *)data);
		break;
	default:
		/* shouldn't happen */
		return -EINVAL;
	}

	return 0;
}

static int btf_dumper_struct(const struct btf_dumper *d, __u32 type_id,
			     const void *data)
{
	const struct btf_type *t;
	struct btf_member *m;
	const void *data_off;
	int ret = 0;
	int i, vlen;

	t = btf__type_by_id(d->btf, type_id);
	if (!t)
		return -EINVAL;

	vlen = BTF_INFO_VLEN(t->info);
	jsonw_start_object(d->jw);
	m = (struct btf_member *)(t + 1);

	for (i = 0; i < vlen; i++) {
		data_off = data + BITS_ROUNDDOWN_BYTES(m[i].offset);
		jsonw_name(d->jw, btf__name_by_offset(d->btf, m[i].name_off));
		ret = btf_dumper_do_type(d, m[i].type,
					 BITS_PER_BYTE_MASKED(m[i].offset),
					 data_off);
		if (ret)
			break;
	}

	jsonw_end_object(d->jw);

	return ret;
}

static int btf_dumper_do_type(const struct btf_dumper *d, __u32 type_id,
			      __u8 bit_offset, const void *data)
{
	const struct btf_type *t = btf__type_by_id(d->btf, type_id);

	switch (BTF_INFO_KIND(t->info)) {
	case BTF_KIND_INT:
		return btf_dumper_int(t, bit_offset, data, d->jw,
				     d->is_plain_text);
	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION:
		return btf_dumper_struct(d, type_id, data);
	case BTF_KIND_ARRAY:
		return btf_dumper_array(d, type_id, data);
	case BTF_KIND_ENUM:
		btf_dumper_enum(data, d->jw);
		return 0;
	case BTF_KIND_PTR:
		btf_dumper_ptr(data, d->jw, d->is_plain_text);
		return 0;
	case BTF_KIND_UNKN:
		jsonw_printf(d->jw, "(unknown)");
		return 0;
	case BTF_KIND_FWD:
		/* map key or value can't be forward */
		jsonw_printf(d->jw, "(fwd-kind-invalid)");
		return -EINVAL;
	case BTF_KIND_TYPEDEF:
	case BTF_KIND_VOLATILE:
	case BTF_KIND_CONST:
	case BTF_KIND_RESTRICT:
		return btf_dumper_modifier(d, type_id, bit_offset, data);
	default:
		jsonw_printf(d->jw, "(unsupported-kind");
		return -EINVAL;
	}
}

int btf_dumper_type(const struct btf_dumper *d, __u32 type_id,
		    const void *data)
{
	return btf_dumper_do_type(d, type_id, 0, data);
}
