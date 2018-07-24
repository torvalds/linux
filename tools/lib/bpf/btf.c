/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018 Facebook */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <linux/err.h>
#include <linux/btf.h>
#include "btf.h"
#include "bpf.h"

#define elog(fmt, ...) { if (err_log) err_log(fmt, ##__VA_ARGS__); }
#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

#define BTF_MAX_NR_TYPES 65535

static struct btf_type btf_void;

struct btf {
	union {
		struct btf_header *hdr;
		void *data;
	};
	struct btf_type **types;
	const char *strings;
	void *nohdr_data;
	__u32 nr_types;
	__u32 types_size;
	__u32 data_size;
	int fd;
};

static const char *btf_name_by_offset(const struct btf *btf, __u32 offset)
{
	if (offset < btf->hdr->str_len)
		return &btf->strings[offset];
	else
		return NULL;
}

static int btf_add_type(struct btf *btf, struct btf_type *t)
{
	if (btf->types_size - btf->nr_types < 2) {
		struct btf_type **new_types;
		__u32 expand_by, new_size;

		if (btf->types_size == BTF_MAX_NR_TYPES)
			return -E2BIG;

		expand_by = max(btf->types_size >> 2, 16);
		new_size = min(BTF_MAX_NR_TYPES, btf->types_size + expand_by);

		new_types = realloc(btf->types, sizeof(*new_types) * new_size);
		if (!new_types)
			return -ENOMEM;

		if (btf->nr_types == 0)
			new_types[0] = &btf_void;

		btf->types = new_types;
		btf->types_size = new_size;
	}

	btf->types[++(btf->nr_types)] = t;

	return 0;
}

static int btf_parse_hdr(struct btf *btf, btf_print_fn_t err_log)
{
	const struct btf_header *hdr = btf->hdr;
	__u32 meta_left;

	if (btf->data_size < sizeof(struct btf_header)) {
		elog("BTF header not found\n");
		return -EINVAL;
	}

	if (hdr->magic != BTF_MAGIC) {
		elog("Invalid BTF magic:%x\n", hdr->magic);
		return -EINVAL;
	}

	if (hdr->version != BTF_VERSION) {
		elog("Unsupported BTF version:%u\n", hdr->version);
		return -ENOTSUP;
	}

	if (hdr->flags) {
		elog("Unsupported BTF flags:%x\n", hdr->flags);
		return -ENOTSUP;
	}

	meta_left = btf->data_size - sizeof(*hdr);
	if (!meta_left) {
		elog("BTF has no data\n");
		return -EINVAL;
	}

	if (meta_left < hdr->type_off) {
		elog("Invalid BTF type section offset:%u\n", hdr->type_off);
		return -EINVAL;
	}

	if (meta_left < hdr->str_off) {
		elog("Invalid BTF string section offset:%u\n", hdr->str_off);
		return -EINVAL;
	}

	if (hdr->type_off >= hdr->str_off) {
		elog("BTF type section offset >= string section offset. No type?\n");
		return -EINVAL;
	}

	if (hdr->type_off & 0x02) {
		elog("BTF type section is not aligned to 4 bytes\n");
		return -EINVAL;
	}

	btf->nohdr_data = btf->hdr + 1;

	return 0;
}

static int btf_parse_str_sec(struct btf *btf, btf_print_fn_t err_log)
{
	const struct btf_header *hdr = btf->hdr;
	const char *start = btf->nohdr_data + hdr->str_off;
	const char *end = start + btf->hdr->str_len;

	if (!hdr->str_len || hdr->str_len - 1 > BTF_MAX_NAME_OFFSET ||
	    start[0] || end[-1]) {
		elog("Invalid BTF string section\n");
		return -EINVAL;
	}

	btf->strings = start;

	return 0;
}

static int btf_parse_type_sec(struct btf *btf, btf_print_fn_t err_log)
{
	struct btf_header *hdr = btf->hdr;
	void *nohdr_data = btf->nohdr_data;
	void *next_type = nohdr_data + hdr->type_off;
	void *end_type = nohdr_data + hdr->str_off;

	while (next_type < end_type) {
		struct btf_type *t = next_type;
		__u16 vlen = BTF_INFO_VLEN(t->info);
		int err;

		next_type += sizeof(*t);
		switch (BTF_INFO_KIND(t->info)) {
		case BTF_KIND_INT:
			next_type += sizeof(int);
			break;
		case BTF_KIND_ARRAY:
			next_type += sizeof(struct btf_array);
			break;
		case BTF_KIND_STRUCT:
		case BTF_KIND_UNION:
			next_type += vlen * sizeof(struct btf_member);
			break;
		case BTF_KIND_ENUM:
			next_type += vlen * sizeof(struct btf_enum);
			break;
		case BTF_KIND_TYPEDEF:
		case BTF_KIND_PTR:
		case BTF_KIND_FWD:
		case BTF_KIND_VOLATILE:
		case BTF_KIND_CONST:
		case BTF_KIND_RESTRICT:
			break;
		default:
			elog("Unsupported BTF_KIND:%u\n",
			     BTF_INFO_KIND(t->info));
			return -EINVAL;
		}

		err = btf_add_type(btf, t);
		if (err)
			return err;
	}

	return 0;
}

static const struct btf_type *btf_type_by_id(const struct btf *btf,
					     __u32 type_id)
{
	if (type_id > btf->nr_types)
		return NULL;

	return btf->types[type_id];
}

static bool btf_type_is_void(const struct btf_type *t)
{
	return t == &btf_void || BTF_INFO_KIND(t->info) == BTF_KIND_FWD;
}

static bool btf_type_is_void_or_null(const struct btf_type *t)
{
	return !t || btf_type_is_void(t);
}

static __s64 btf_type_size(const struct btf_type *t)
{
	switch (BTF_INFO_KIND(t->info)) {
	case BTF_KIND_INT:
	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION:
	case BTF_KIND_ENUM:
		return t->size;
	case BTF_KIND_PTR:
		return sizeof(void *);
	default:
		return -EINVAL;
	}
}

#define MAX_RESOLVE_DEPTH 32

__s64 btf__resolve_size(const struct btf *btf, __u32 type_id)
{
	const struct btf_array *array;
	const struct btf_type *t;
	__u32 nelems = 1;
	__s64 size = -1;
	int i;

	t = btf_type_by_id(btf, type_id);
	for (i = 0; i < MAX_RESOLVE_DEPTH && !btf_type_is_void_or_null(t);
	     i++) {
		size = btf_type_size(t);
		if (size >= 0)
			break;

		switch (BTF_INFO_KIND(t->info)) {
		case BTF_KIND_TYPEDEF:
		case BTF_KIND_VOLATILE:
		case BTF_KIND_CONST:
		case BTF_KIND_RESTRICT:
			type_id = t->type;
			break;
		case BTF_KIND_ARRAY:
			array = (const struct btf_array *)(t + 1);
			if (nelems && array->nelems > UINT32_MAX / nelems)
				return -E2BIG;
			nelems *= array->nelems;
			type_id = array->type;
			break;
		default:
			return -EINVAL;
		}

		t = btf_type_by_id(btf, type_id);
	}

	if (size < 0)
		return -EINVAL;

	if (nelems && size > UINT32_MAX / nelems)
		return -E2BIG;

	return nelems * size;
}

__s32 btf__find_by_name(const struct btf *btf, const char *type_name)
{
	__u32 i;

	if (!strcmp(type_name, "void"))
		return 0;

	for (i = 1; i <= btf->nr_types; i++) {
		const struct btf_type *t = btf->types[i];
		const char *name = btf_name_by_offset(btf, t->name_off);

		if (name && !strcmp(type_name, name))
			return i;
	}

	return -ENOENT;
}

void btf__free(struct btf *btf)
{
	if (!btf)
		return;

	if (btf->fd != -1)
		close(btf->fd);

	free(btf->data);
	free(btf->types);
	free(btf);
}

struct btf *btf__new(__u8 *data, __u32 size, btf_print_fn_t err_log)
{
	__u32 log_buf_size = 0;
	char *log_buf = NULL;
	struct btf *btf;
	int err;

	btf = calloc(1, sizeof(struct btf));
	if (!btf)
		return ERR_PTR(-ENOMEM);

	btf->fd = -1;

	if (err_log) {
		log_buf = malloc(BPF_LOG_BUF_SIZE);
		if (!log_buf) {
			err = -ENOMEM;
			goto done;
		}
		*log_buf = 0;
		log_buf_size = BPF_LOG_BUF_SIZE;
	}

	btf->data = malloc(size);
	if (!btf->data) {
		err = -ENOMEM;
		goto done;
	}

	memcpy(btf->data, data, size);
	btf->data_size = size;

	btf->fd = bpf_load_btf(btf->data, btf->data_size,
			       log_buf, log_buf_size, false);

	if (btf->fd == -1) {
		err = -errno;
		elog("Error loading BTF: %s(%d)\n", strerror(errno), errno);
		if (log_buf && *log_buf)
			elog("%s\n", log_buf);
		goto done;
	}

	err = btf_parse_hdr(btf, err_log);
	if (err)
		goto done;

	err = btf_parse_str_sec(btf, err_log);
	if (err)
		goto done;

	err = btf_parse_type_sec(btf, err_log);

done:
	free(log_buf);

	if (err) {
		btf__free(btf);
		return ERR_PTR(err);
	}

	return btf;
}

int btf__fd(const struct btf *btf)
{
	return btf->fd;
}
