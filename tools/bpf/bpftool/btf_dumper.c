// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (c) 2018 Facebook */

#include <ctype.h>
#include <stdio.h> /* for (FILE *) used by json_writer */
#include <string.h>
#include <unistd.h>
#include <asm/byteorder.h>
#include <linux/bitops.h>
#include <linux/btf.h>
#include <linux/err.h>
#include <bpf/btf.h>
#include <bpf/bpf.h>

#include "json_writer.h"
#include "main.h"

#define BITS_PER_BYTE_MASK (BITS_PER_BYTE - 1)
#define BITS_PER_BYTE_MASKED(bits) ((bits) & BITS_PER_BYTE_MASK)
#define BITS_ROUNDDOWN_BYTES(bits) ((bits) >> 3)
#define BITS_ROUNDUP_BYTES(bits) \
	(BITS_ROUNDDOWN_BYTES(bits) + !!BITS_PER_BYTE_MASKED(bits))

static int btf_dumper_do_type(const struct btf_dumper *d, __u32 type_id,
			      __u8 bit_offset, const void *data);

static int btf_dump_func(const struct btf *btf, char *func_sig,
			 const struct btf_type *func_proto,
			 const struct btf_type *func, int pos, int size);

static int dump_prog_id_as_func_ptr(const struct btf_dumper *d,
				    const struct btf_type *func_proto,
				    __u32 prog_id)
{
	struct bpf_prog_info_linear *prog_info = NULL;
	const struct btf_type *func_type;
	const char *prog_name = NULL;
	struct bpf_func_info *finfo;
	struct btf *prog_btf = NULL;
	struct bpf_prog_info *info;
	int prog_fd, func_sig_len;
	char prog_str[1024];

	/* Get the ptr's func_proto */
	func_sig_len = btf_dump_func(d->btf, prog_str, func_proto, NULL, 0,
				     sizeof(prog_str));
	if (func_sig_len == -1)
		return -1;

	if (!prog_id)
		goto print;

	/* Get the bpf_prog's name.  Obtain from func_info. */
	prog_fd = bpf_prog_get_fd_by_id(prog_id);
	if (prog_fd == -1)
		goto print;

	prog_info = bpf_program__get_prog_info_linear(prog_fd,
						1UL << BPF_PROG_INFO_FUNC_INFO);
	close(prog_fd);
	if (IS_ERR(prog_info)) {
		prog_info = NULL;
		goto print;
	}
	info = &prog_info->info;

	if (!info->btf_id || !info->nr_func_info)
		goto print;
	prog_btf = btf__load_from_kernel_by_id(info->btf_id);
	if (libbpf_get_error(prog_btf))
		goto print;
	finfo = u64_to_ptr(info->func_info);
	func_type = btf__type_by_id(prog_btf, finfo->type_id);
	if (!func_type || !btf_is_func(func_type))
		goto print;

	prog_name = btf__name_by_offset(prog_btf, func_type->name_off);

print:
	if (!prog_id)
		snprintf(&prog_str[func_sig_len],
			 sizeof(prog_str) - func_sig_len, " 0");
	else if (prog_name)
		snprintf(&prog_str[func_sig_len],
			 sizeof(prog_str) - func_sig_len,
			 " %s/prog_id:%u", prog_name, prog_id);
	else
		snprintf(&prog_str[func_sig_len],
			 sizeof(prog_str) - func_sig_len,
			 " <unknown_prog_name>/prog_id:%u", prog_id);

	prog_str[sizeof(prog_str) - 1] = '\0';
	jsonw_string(d->jw, prog_str);
	btf__free(prog_btf);
	free(prog_info);
	return 0;
}

static void btf_dumper_ptr(const struct btf_dumper *d,
			   const struct btf_type *t,
			   const void *data)
{
	unsigned long value = *(unsigned long *)data;
	const struct btf_type *ptr_type;
	__s32 ptr_type_id;

	if (!d->prog_id_as_func_ptr || value > UINT32_MAX)
		goto print_ptr_value;

	ptr_type_id = btf__resolve_type(d->btf, t->type);
	if (ptr_type_id < 0)
		goto print_ptr_value;
	ptr_type = btf__type_by_id(d->btf, ptr_type_id);
	if (!ptr_type || !btf_is_func_proto(ptr_type))
		goto print_ptr_value;

	if (!dump_prog_id_as_func_ptr(d, ptr_type, value))
		return;

print_ptr_value:
	if (d->is_plain_text)
		jsonw_printf(d->jw, "%p", (void *)value);
	else
		jsonw_printf(d->jw, "%lu", value);
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

static int btf_dumper_enum(const struct btf_dumper *d,
			    const struct btf_type *t,
			    const void *data)
{
	const struct btf_enum *enums = btf_enum(t);
	__s64 value;
	__u16 i;

	switch (t->size) {
	case 8:
		value = *(__s64 *)data;
		break;
	case 4:
		value = *(__s32 *)data;
		break;
	case 2:
		value = *(__s16 *)data;
		break;
	case 1:
		value = *(__s8 *)data;
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < btf_vlen(t); i++) {
		if (value == enums[i].val) {
			jsonw_string(d->jw,
				     btf__name_by_offset(d->btf,
							 enums[i].name_off));
			return 0;
		}
	}

	jsonw_int(d->jw, value);
	return 0;
}

static bool is_str_array(const struct btf *btf, const struct btf_array *arr,
			 const char *s)
{
	const struct btf_type *elem_type;
	const char *end_s;

	if (!arr->nelems)
		return false;

	elem_type = btf__type_by_id(btf, arr->type);
	/* Not skipping typedef.  typedef to char does not count as
	 * a string now.
	 */
	while (elem_type && btf_is_mod(elem_type))
		elem_type = btf__type_by_id(btf, elem_type->type);

	if (!elem_type || !btf_is_int(elem_type) || elem_type->size != 1)
		return false;

	if (btf_int_encoding(elem_type) != BTF_INT_CHAR &&
	    strcmp("char", btf__name_by_offset(btf, elem_type->name_off)))
		return false;

	end_s = s + arr->nelems;
	while (s < end_s) {
		if (!*s)
			return true;
		if (*s <= 0x1f || *s >= 0x7f)
			return false;
		s++;
	}

	/* '\0' is not found */
	return false;
}

static int btf_dumper_array(const struct btf_dumper *d, __u32 type_id,
			    const void *data)
{
	const struct btf_type *t = btf__type_by_id(d->btf, type_id);
	struct btf_array *arr = (struct btf_array *)(t + 1);
	long long elem_size;
	int ret = 0;
	__u32 i;

	if (is_str_array(d->btf, arr, data)) {
		jsonw_string(d->jw, data);
		return 0;
	}

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

static void btf_int128_print(json_writer_t *jw, const void *data,
			     bool is_plain_text)
{
	/* data points to a __int128 number.
	 * Suppose
	 *     int128_num = *(__int128 *)data;
	 * The below formulas shows what upper_num and lower_num represents:
	 *     upper_num = int128_num >> 64;
	 *     lower_num = int128_num & 0xffffffffFFFFFFFFULL;
	 */
	__u64 upper_num, lower_num;

#ifdef __BIG_ENDIAN_BITFIELD
	upper_num = *(__u64 *)data;
	lower_num = *(__u64 *)(data + 8);
#else
	upper_num = *(__u64 *)(data + 8);
	lower_num = *(__u64 *)data;
#endif

	if (is_plain_text) {
		if (upper_num == 0)
			jsonw_printf(jw, "0x%llx", lower_num);
		else
			jsonw_printf(jw, "0x%llx%016llx", upper_num, lower_num);
	} else {
		if (upper_num == 0)
			jsonw_printf(jw, "\"0x%llx\"", lower_num);
		else
			jsonw_printf(jw, "\"0x%llx%016llx\"", upper_num, lower_num);
	}
}

static void btf_int128_shift(__u64 *print_num, __u16 left_shift_bits,
			     __u16 right_shift_bits)
{
	__u64 upper_num, lower_num;

#ifdef __BIG_ENDIAN_BITFIELD
	upper_num = print_num[0];
	lower_num = print_num[1];
#else
	upper_num = print_num[1];
	lower_num = print_num[0];
#endif

	/* shake out un-needed bits by shift/or operations */
	if (left_shift_bits >= 64) {
		upper_num = lower_num << (left_shift_bits - 64);
		lower_num = 0;
	} else {
		upper_num = (upper_num << left_shift_bits) |
			    (lower_num >> (64 - left_shift_bits));
		lower_num = lower_num << left_shift_bits;
	}

	if (right_shift_bits >= 64) {
		lower_num = upper_num >> (right_shift_bits - 64);
		upper_num = 0;
	} else {
		lower_num = (lower_num >> right_shift_bits) |
			    (upper_num << (64 - right_shift_bits));
		upper_num = upper_num >> right_shift_bits;
	}

#ifdef __BIG_ENDIAN_BITFIELD
	print_num[0] = upper_num;
	print_num[1] = lower_num;
#else
	print_num[0] = lower_num;
	print_num[1] = upper_num;
#endif
}

static void btf_dumper_bitfield(__u32 nr_bits, __u8 bit_offset,
				const void *data, json_writer_t *jw,
				bool is_plain_text)
{
	int left_shift_bits, right_shift_bits;
	__u64 print_num[2] = {};
	int bytes_to_copy;
	int bits_to_copy;

	bits_to_copy = bit_offset + nr_bits;
	bytes_to_copy = BITS_ROUNDUP_BYTES(bits_to_copy);

	memcpy(print_num, data, bytes_to_copy);
#if defined(__BIG_ENDIAN_BITFIELD)
	left_shift_bits = bit_offset;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	left_shift_bits = 128 - bits_to_copy;
#else
#error neither big nor little endian
#endif
	right_shift_bits = 128 - nr_bits;

	btf_int128_shift(print_num, left_shift_bits, right_shift_bits);
	btf_int128_print(jw, print_num, is_plain_text);
}


static void btf_dumper_int_bits(__u32 int_type, __u8 bit_offset,
				const void *data, json_writer_t *jw,
				bool is_plain_text)
{
	int nr_bits = BTF_INT_BITS(int_type);
	int total_bits_offset;

	/* bits_offset is at most 7.
	 * BTF_INT_OFFSET() cannot exceed 128 bits.
	 */
	total_bits_offset = bit_offset + BTF_INT_OFFSET(int_type);
	data += BITS_ROUNDDOWN_BYTES(total_bits_offset);
	bit_offset = BITS_PER_BYTE_MASKED(total_bits_offset);
	btf_dumper_bitfield(nr_bits, bit_offset, data, jw,
			    is_plain_text);
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

	if (nr_bits == 128) {
		btf_int128_print(jw, data, is_plain_text);
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
	int kind_flag;
	int ret = 0;
	int i, vlen;

	t = btf__type_by_id(d->btf, type_id);
	if (!t)
		return -EINVAL;

	kind_flag = BTF_INFO_KFLAG(t->info);
	vlen = BTF_INFO_VLEN(t->info);
	jsonw_start_object(d->jw);
	m = (struct btf_member *)(t + 1);

	for (i = 0; i < vlen; i++) {
		__u32 bit_offset = m[i].offset;
		__u32 bitfield_size = 0;

		if (kind_flag) {
			bitfield_size = BTF_MEMBER_BITFIELD_SIZE(bit_offset);
			bit_offset = BTF_MEMBER_BIT_OFFSET(bit_offset);
		}

		jsonw_name(d->jw, btf__name_by_offset(d->btf, m[i].name_off));
		data_off = data + BITS_ROUNDDOWN_BYTES(bit_offset);
		if (bitfield_size) {
			btf_dumper_bitfield(bitfield_size,
					    BITS_PER_BYTE_MASKED(bit_offset),
					    data_off, d->jw, d->is_plain_text);
		} else {
			ret = btf_dumper_do_type(d, m[i].type,
						 BITS_PER_BYTE_MASKED(bit_offset),
						 data_off);
			if (ret)
				break;
		}
	}

	jsonw_end_object(d->jw);

	return ret;
}

static int btf_dumper_var(const struct btf_dumper *d, __u32 type_id,
			  __u8 bit_offset, const void *data)
{
	const struct btf_type *t = btf__type_by_id(d->btf, type_id);
	int ret;

	jsonw_start_object(d->jw);
	jsonw_name(d->jw, btf__name_by_offset(d->btf, t->name_off));
	ret = btf_dumper_do_type(d, t->type, bit_offset, data);
	jsonw_end_object(d->jw);

	return ret;
}

static int btf_dumper_datasec(const struct btf_dumper *d, __u32 type_id,
			      const void *data)
{
	struct btf_var_secinfo *vsi;
	const struct btf_type *t;
	int ret = 0, i, vlen;

	t = btf__type_by_id(d->btf, type_id);
	if (!t)
		return -EINVAL;

	vlen = BTF_INFO_VLEN(t->info);
	vsi = (struct btf_var_secinfo *)(t + 1);

	jsonw_start_object(d->jw);
	jsonw_name(d->jw, btf__name_by_offset(d->btf, t->name_off));
	jsonw_start_array(d->jw);
	for (i = 0; i < vlen; i++) {
		ret = btf_dumper_do_type(d, vsi[i].type, 0, data + vsi[i].offset);
		if (ret)
			break;
	}
	jsonw_end_array(d->jw);
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
		return btf_dumper_enum(d, t, data);
	case BTF_KIND_PTR:
		btf_dumper_ptr(d, t, data);
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
	case BTF_KIND_VAR:
		return btf_dumper_var(d, type_id, bit_offset, data);
	case BTF_KIND_DATASEC:
		return btf_dumper_datasec(d, type_id, data);
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

#define BTF_PRINT_ARG(...)						\
	do {								\
		pos += snprintf(func_sig + pos, size - pos,		\
				__VA_ARGS__);				\
		if (pos >= size)					\
			return -1;					\
	} while (0)
#define BTF_PRINT_TYPE(type)					\
	do {								\
		pos = __btf_dumper_type_only(btf, type, func_sig,	\
					     pos, size);		\
		if (pos == -1)						\
			return -1;					\
	} while (0)

static int __btf_dumper_type_only(const struct btf *btf, __u32 type_id,
				  char *func_sig, int pos, int size)
{
	const struct btf_type *proto_type;
	const struct btf_array *array;
	const struct btf_var *var;
	const struct btf_type *t;

	if (!type_id) {
		BTF_PRINT_ARG("void ");
		return pos;
	}

	t = btf__type_by_id(btf, type_id);

	switch (BTF_INFO_KIND(t->info)) {
	case BTF_KIND_INT:
	case BTF_KIND_TYPEDEF:
	case BTF_KIND_FLOAT:
		BTF_PRINT_ARG("%s ", btf__name_by_offset(btf, t->name_off));
		break;
	case BTF_KIND_STRUCT:
		BTF_PRINT_ARG("struct %s ",
			      btf__name_by_offset(btf, t->name_off));
		break;
	case BTF_KIND_UNION:
		BTF_PRINT_ARG("union %s ",
			      btf__name_by_offset(btf, t->name_off));
		break;
	case BTF_KIND_ENUM:
		BTF_PRINT_ARG("enum %s ",
			      btf__name_by_offset(btf, t->name_off));
		break;
	case BTF_KIND_ARRAY:
		array = (struct btf_array *)(t + 1);
		BTF_PRINT_TYPE(array->type);
		BTF_PRINT_ARG("[%d]", array->nelems);
		break;
	case BTF_KIND_PTR:
		BTF_PRINT_TYPE(t->type);
		BTF_PRINT_ARG("* ");
		break;
	case BTF_KIND_FWD:
		BTF_PRINT_ARG("%s %s ",
			      BTF_INFO_KFLAG(t->info) ? "union" : "struct",
			      btf__name_by_offset(btf, t->name_off));
		break;
	case BTF_KIND_VOLATILE:
		BTF_PRINT_ARG("volatile ");
		BTF_PRINT_TYPE(t->type);
		break;
	case BTF_KIND_CONST:
		BTF_PRINT_ARG("const ");
		BTF_PRINT_TYPE(t->type);
		break;
	case BTF_KIND_RESTRICT:
		BTF_PRINT_ARG("restrict ");
		BTF_PRINT_TYPE(t->type);
		break;
	case BTF_KIND_FUNC_PROTO:
		pos = btf_dump_func(btf, func_sig, t, NULL, pos, size);
		if (pos == -1)
			return -1;
		break;
	case BTF_KIND_FUNC:
		proto_type = btf__type_by_id(btf, t->type);
		pos = btf_dump_func(btf, func_sig, proto_type, t, pos, size);
		if (pos == -1)
			return -1;
		break;
	case BTF_KIND_VAR:
		var = (struct btf_var *)(t + 1);
		if (var->linkage == BTF_VAR_STATIC)
			BTF_PRINT_ARG("static ");
		BTF_PRINT_TYPE(t->type);
		BTF_PRINT_ARG(" %s",
			      btf__name_by_offset(btf, t->name_off));
		break;
	case BTF_KIND_DATASEC:
		BTF_PRINT_ARG("section (\"%s\") ",
			      btf__name_by_offset(btf, t->name_off));
		break;
	case BTF_KIND_UNKN:
	default:
		return -1;
	}

	return pos;
}

static int btf_dump_func(const struct btf *btf, char *func_sig,
			 const struct btf_type *func_proto,
			 const struct btf_type *func, int pos, int size)
{
	int i, vlen;

	BTF_PRINT_TYPE(func_proto->type);
	if (func)
		BTF_PRINT_ARG("%s(", btf__name_by_offset(btf, func->name_off));
	else
		BTF_PRINT_ARG("(");
	vlen = BTF_INFO_VLEN(func_proto->info);
	for (i = 0; i < vlen; i++) {
		struct btf_param *arg = &((struct btf_param *)(func_proto + 1))[i];

		if (i)
			BTF_PRINT_ARG(", ");
		if (arg->type) {
			BTF_PRINT_TYPE(arg->type);
			if (arg->name_off)
				BTF_PRINT_ARG("%s",
					      btf__name_by_offset(btf, arg->name_off));
			else if (pos && func_sig[pos - 1] == ' ')
				/* Remove unnecessary space for
				 * FUNC_PROTO that does not have
				 * arg->name_off
				 */
				func_sig[--pos] = '\0';
		} else {
			BTF_PRINT_ARG("...");
		}
	}
	BTF_PRINT_ARG(")");

	return pos;
}

void btf_dumper_type_only(const struct btf *btf, __u32 type_id, char *func_sig,
			  int size)
{
	int err;

	func_sig[0] = '\0';
	if (!btf)
		return;

	err = __btf_dumper_type_only(btf, type_id, func_sig, 0, size);
	if (err < 0)
		func_sig[0] = '\0';
}

static const char *ltrim(const char *s)
{
	while (isspace(*s))
		s++;

	return s;
}

void btf_dump_linfo_plain(const struct btf *btf,
			  const struct bpf_line_info *linfo,
			  const char *prefix, bool linum)
{
	const char *line = btf__name_by_offset(btf, linfo->line_off);

	if (!line)
		return;
	line = ltrim(line);

	if (!prefix)
		prefix = "";

	if (linum) {
		const char *file = btf__name_by_offset(btf, linfo->file_name_off);

		/* More forgiving on file because linum option is
		 * expected to provide more info than the already
		 * available src line.
		 */
		if (!file)
			file = "";

		printf("%s%s [file:%s line_num:%u line_col:%u]\n",
		       prefix, line, file,
		       BPF_LINE_INFO_LINE_NUM(linfo->line_col),
		       BPF_LINE_INFO_LINE_COL(linfo->line_col));
	} else {
		printf("%s%s\n", prefix, line);
	}
}

void btf_dump_linfo_json(const struct btf *btf,
			 const struct bpf_line_info *linfo, bool linum)
{
	const char *line = btf__name_by_offset(btf, linfo->line_off);

	if (line)
		jsonw_string_field(json_wtr, "src", ltrim(line));

	if (linum) {
		const char *file = btf__name_by_offset(btf, linfo->file_name_off);

		if (file)
			jsonw_string_field(json_wtr, "file", file);

		if (BPF_LINE_INFO_LINE_NUM(linfo->line_col))
			jsonw_int_field(json_wtr, "line_num",
					BPF_LINE_INFO_LINE_NUM(linfo->line_col));

		if (BPF_LINE_INFO_LINE_COL(linfo->line_col))
			jsonw_int_field(json_wtr, "line_col",
					BPF_LINE_INFO_LINE_COL(linfo->line_col));
	}
}
