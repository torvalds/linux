// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/* Copyright (c) 2018 Facebook */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <linux/err.h>
#include <linux/btf.h>
#include "btf.h"
#include "bpf.h"
#include "libbpf.h"
#include "libbpf_util.h"

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

#define BTF_MAX_NR_TYPES 0x7fffffff
#define BTF_MAX_STR_OFFSET 0x7fffffff

#define IS_MODIFIER(k) (((k) == BTF_KIND_TYPEDEF) || \
		((k) == BTF_KIND_VOLATILE) || \
		((k) == BTF_KIND_CONST) || \
		((k) == BTF_KIND_RESTRICT))

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

struct btf_ext_info {
	/*
	 * info points to the individual info section (e.g. func_info and
	 * line_info) from the .BTF.ext. It does not include the __u32 rec_size.
	 */
	void *info;
	__u32 rec_size;
	__u32 len;
};

struct btf_ext {
	union {
		struct btf_ext_header *hdr;
		void *data;
	};
	struct btf_ext_info func_info;
	struct btf_ext_info line_info;
	__u32 data_size;
};

struct btf_ext_info_sec {
	__u32	sec_name_off;
	__u32	num_info;
	/* Followed by num_info * record_size number of bytes */
	__u8	data[0];
};

/* The minimum bpf_func_info checked by the loader */
struct bpf_func_info_min {
	__u32   insn_off;
	__u32   type_id;
};

/* The minimum bpf_line_info checked by the loader */
struct bpf_line_info_min {
	__u32	insn_off;
	__u32	file_name_off;
	__u32	line_off;
	__u32	line_col;
};

static inline __u64 ptr_to_u64(const void *ptr)
{
	return (__u64) (unsigned long) ptr;
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

static int btf_parse_hdr(struct btf *btf)
{
	const struct btf_header *hdr = btf->hdr;
	__u32 meta_left;

	if (btf->data_size < sizeof(struct btf_header)) {
		pr_debug("BTF header not found\n");
		return -EINVAL;
	}

	if (hdr->magic != BTF_MAGIC) {
		pr_debug("Invalid BTF magic:%x\n", hdr->magic);
		return -EINVAL;
	}

	if (hdr->version != BTF_VERSION) {
		pr_debug("Unsupported BTF version:%u\n", hdr->version);
		return -ENOTSUP;
	}

	if (hdr->flags) {
		pr_debug("Unsupported BTF flags:%x\n", hdr->flags);
		return -ENOTSUP;
	}

	meta_left = btf->data_size - sizeof(*hdr);
	if (!meta_left) {
		pr_debug("BTF has no data\n");
		return -EINVAL;
	}

	if (meta_left < hdr->type_off) {
		pr_debug("Invalid BTF type section offset:%u\n", hdr->type_off);
		return -EINVAL;
	}

	if (meta_left < hdr->str_off) {
		pr_debug("Invalid BTF string section offset:%u\n", hdr->str_off);
		return -EINVAL;
	}

	if (hdr->type_off >= hdr->str_off) {
		pr_debug("BTF type section offset >= string section offset. No type?\n");
		return -EINVAL;
	}

	if (hdr->type_off & 0x02) {
		pr_debug("BTF type section is not aligned to 4 bytes\n");
		return -EINVAL;
	}

	btf->nohdr_data = btf->hdr + 1;

	return 0;
}

static int btf_parse_str_sec(struct btf *btf)
{
	const struct btf_header *hdr = btf->hdr;
	const char *start = btf->nohdr_data + hdr->str_off;
	const char *end = start + btf->hdr->str_len;

	if (!hdr->str_len || hdr->str_len - 1 > BTF_MAX_STR_OFFSET ||
	    start[0] || end[-1]) {
		pr_debug("Invalid BTF string section\n");
		return -EINVAL;
	}

	btf->strings = start;

	return 0;
}

static int btf_type_size(struct btf_type *t)
{
	int base_size = sizeof(struct btf_type);
	__u16 vlen = BTF_INFO_VLEN(t->info);

	switch (BTF_INFO_KIND(t->info)) {
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
	default:
		pr_debug("Unsupported BTF_KIND:%u\n", BTF_INFO_KIND(t->info));
		return -EINVAL;
	}
}

static int btf_parse_type_sec(struct btf *btf)
{
	struct btf_header *hdr = btf->hdr;
	void *nohdr_data = btf->nohdr_data;
	void *next_type = nohdr_data + hdr->type_off;
	void *end_type = nohdr_data + hdr->str_off;

	while (next_type < end_type) {
		struct btf_type *t = next_type;
		int type_size;
		int err;

		type_size = btf_type_size(t);
		if (type_size < 0)
			return type_size;
		next_type += type_size;
		err = btf_add_type(btf, t);
		if (err)
			return err;
	}

	return 0;
}

__u32 btf__get_nr_types(const struct btf *btf)
{
	return btf->nr_types;
}

const struct btf_type *btf__type_by_id(const struct btf *btf, __u32 type_id)
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
		switch (BTF_INFO_KIND(t->info)) {
		case BTF_KIND_INT:
		case BTF_KIND_STRUCT:
		case BTF_KIND_UNION:
		case BTF_KIND_ENUM:
			size = t->size;
			goto done;
		case BTF_KIND_PTR:
			size = sizeof(void *);
			goto done;
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

		t = btf__type_by_id(btf, type_id);
	}

	if (size < 0)
		return -EINVAL;

done:
	if (nelems && size > UINT32_MAX / nelems)
		return -E2BIG;

	return nelems * size;
}

int btf__resolve_type(const struct btf *btf, __u32 type_id)
{
	const struct btf_type *t;
	int depth = 0;

	t = btf__type_by_id(btf, type_id);
	while (depth < MAX_RESOLVE_DEPTH &&
	       !btf_type_is_void_or_null(t) &&
	       IS_MODIFIER(BTF_INFO_KIND(t->info))) {
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
	__u32 i;

	if (!strcmp(type_name, "void"))
		return 0;

	for (i = 1; i <= btf->nr_types; i++) {
		const struct btf_type *t = btf->types[i];
		const char *name = btf__name_by_offset(btf, t->name_off);

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

struct btf *btf__new(__u8 *data, __u32 size)
{
	struct btf *btf;
	int err;

	btf = calloc(1, sizeof(struct btf));
	if (!btf)
		return ERR_PTR(-ENOMEM);

	btf->fd = -1;

	btf->data = malloc(size);
	if (!btf->data) {
		err = -ENOMEM;
		goto done;
	}

	memcpy(btf->data, data, size);
	btf->data_size = size;

	err = btf_parse_hdr(btf);
	if (err)
		goto done;

	err = btf_parse_str_sec(btf);
	if (err)
		goto done;

	err = btf_parse_type_sec(btf);

done:
	if (err) {
		btf__free(btf);
		return ERR_PTR(err);
	}

	return btf;
}

int btf__load(struct btf *btf)
{
	__u32 log_buf_size = BPF_LOG_BUF_SIZE;
	char *log_buf = NULL;
	int err = 0;

	if (btf->fd >= 0)
		return -EEXIST;

	log_buf = malloc(log_buf_size);
	if (!log_buf)
		return -ENOMEM;

	*log_buf = 0;

	btf->fd = bpf_load_btf(btf->data, btf->data_size,
			       log_buf, log_buf_size, false);
	if (btf->fd < 0) {
		err = -errno;
		pr_warning("Error loading BTF: %s(%d)\n", strerror(errno), errno);
		if (*log_buf)
			pr_warning("%s\n", log_buf);
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

const void *btf__get_raw_data(const struct btf *btf, __u32 *size)
{
	*size = btf->data_size;
	return btf->data;
}

const char *btf__name_by_offset(const struct btf *btf, __u32 offset)
{
	if (offset < btf->hdr->str_len)
		return &btf->strings[offset];
	else
		return NULL;
}

int btf__get_from_id(__u32 id, struct btf **btf)
{
	struct bpf_btf_info btf_info = { 0 };
	__u32 len = sizeof(btf_info);
	__u32 last_size;
	int btf_fd;
	void *ptr;
	int err;

	err = 0;
	*btf = NULL;
	btf_fd = bpf_btf_get_fd_by_id(id);
	if (btf_fd < 0)
		return 0;

	/* we won't know btf_size until we call bpf_obj_get_info_by_fd(). so
	 * let's start with a sane default - 4KiB here - and resize it only if
	 * bpf_obj_get_info_by_fd() needs a bigger buffer.
	 */
	btf_info.btf_size = 4096;
	last_size = btf_info.btf_size;
	ptr = malloc(last_size);
	if (!ptr) {
		err = -ENOMEM;
		goto exit_free;
	}

	memset(ptr, 0, last_size);
	btf_info.btf = ptr_to_u64(ptr);
	err = bpf_obj_get_info_by_fd(btf_fd, &btf_info, &len);

	if (!err && btf_info.btf_size > last_size) {
		void *temp_ptr;

		last_size = btf_info.btf_size;
		temp_ptr = realloc(ptr, last_size);
		if (!temp_ptr) {
			err = -ENOMEM;
			goto exit_free;
		}
		ptr = temp_ptr;
		memset(ptr, 0, last_size);
		btf_info.btf = ptr_to_u64(ptr);
		err = bpf_obj_get_info_by_fd(btf_fd, &btf_info, &len);
	}

	if (err || btf_info.btf_size > last_size) {
		err = errno;
		goto exit_free;
	}

	*btf = btf__new((__u8 *)(long)btf_info.btf, btf_info.btf_size);
	if (IS_ERR(*btf)) {
		err = PTR_ERR(*btf);
		*btf = NULL;
	}

exit_free:
	close(btf_fd);
	free(ptr);

	return err;
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
		pr_warning("map:%s length of '____btf_map_%s' is too long\n",
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
		pr_warning("map:%s cannot find BTF type for container_id:%u\n",
			   map_name, container_id);
		return -EINVAL;
	}

	if (BTF_INFO_KIND(container_type->info) != BTF_KIND_STRUCT ||
	    BTF_INFO_VLEN(container_type->info) < 2) {
		pr_warning("map:%s container_name:%s is an invalid container struct\n",
			   map_name, container_name);
		return -EINVAL;
	}

	key = (struct btf_member *)(container_type + 1);
	value = key + 1;

	key_size = btf__resolve_size(btf, key->type);
	if (key_size < 0) {
		pr_warning("map:%s invalid BTF key_type_size\n", map_name);
		return key_size;
	}

	if (expected_key_size != key_size) {
		pr_warning("map:%s btf_key_type_size:%u != map_def_key_size:%u\n",
			   map_name, (__u32)key_size, expected_key_size);
		return -EINVAL;
	}

	value_size = btf__resolve_size(btf, value->type);
	if (value_size < 0) {
		pr_warning("map:%s invalid BTF value_type_size\n", map_name);
		return value_size;
	}

	if (expected_value_size != value_size) {
		pr_warning("map:%s btf_value_type_size:%u != map_def_value_size:%u\n",
			   map_name, (__u32)value_size, expected_value_size);
		return -EINVAL;
	}

	*key_type_id = key->type;
	*value_type_id = value->type;

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

static int btf_ext_parse_hdr(__u8 *data, __u32 data_size)
{
	const struct btf_ext_header *hdr = (struct btf_ext_header *)data;

	if (data_size < offsetof(struct btf_ext_header, func_info_off) ||
	    data_size < hdr->hdr_len) {
		pr_debug("BTF.ext header not found");
		return -EINVAL;
	}

	if (hdr->magic != BTF_MAGIC) {
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
	if (!btf_ext)
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

	err = btf_ext_setup_func_info(btf_ext);
	if (err)
		goto done;

	err = btf_ext_setup_line_info(btf_ext);
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

#define BTF_DEDUP_TABLE_DEFAULT_SIZE (1 << 14)
#define BTF_DEDUP_TABLE_MAX_SIZE_LOG 31
#define BTF_UNPROCESSED_ID ((__u32)-1)
#define BTF_IN_PROGRESS_ID ((__u32)-2)

struct btf_dedup_node {
	struct btf_dedup_node *next;
	__u32 type_id;
};

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
	struct btf_dedup_node **dedup_table;
	/* Canonical types map */
	__u32 *map;
	/* Hypothetical mapping, used during type graph equivalence checks */
	__u32 *hypot_map;
	__u32 *hypot_list;
	size_t hypot_cnt;
	size_t hypot_cap;
	/* Various option modifying behavior of algorithm */
	struct btf_dedup_opts opts;
};

struct btf_str_ptr {
	const char *str;
	__u32 new_off;
	bool used;
};

struct btf_str_ptrs {
	struct btf_str_ptr *ptrs;
	const char *data;
	__u32 cnt;
	__u32 cap;
};

static inline __u32 hash_combine(__u32 h, __u32 value)
{
/* 2^31 + 2^29 - 2^25 + 2^22 - 2^19 - 2^16 + 1 */
#define GOLDEN_RATIO_PRIME 0x9e370001UL
	return h * 37 + value * GOLDEN_RATIO_PRIME;
#undef GOLDEN_RATIO_PRIME
}

#define for_each_dedup_cand(d, hash, node) \
	for (node = d->dedup_table[hash & (d->opts.dedup_table_size - 1)]; \
	     node;							   \
	     node = node->next)

static int btf_dedup_table_add(struct btf_dedup *d, __u32 hash, __u32 type_id)
{
	struct btf_dedup_node *node = malloc(sizeof(struct btf_dedup_node));
	int bucket = hash & (d->opts.dedup_table_size - 1);

	if (!node)
		return -ENOMEM;
	node->type_id = type_id;
	node->next = d->dedup_table[bucket];
	d->dedup_table[bucket] = node;
	return 0;
}

static int btf_dedup_hypot_map_add(struct btf_dedup *d,
				   __u32 from_id, __u32 to_id)
{
	if (d->hypot_cnt == d->hypot_cap) {
		__u32 *new_list;

		d->hypot_cap += max(16, d->hypot_cap / 2);
		new_list = realloc(d->hypot_list, sizeof(__u32) * d->hypot_cap);
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
}

static void btf_dedup_table_free(struct btf_dedup *d)
{
	struct btf_dedup_node *head, *tmp;
	int i;

	if (!d->dedup_table)
		return;

	for (i = 0; i < d->opts.dedup_table_size; i++) {
		while (d->dedup_table[i]) {
			tmp = d->dedup_table[i];
			d->dedup_table[i] = tmp->next;
			free(tmp);
		}

		head = d->dedup_table[i];
		while (head) {
			tmp = head;
			head = head->next;
			free(tmp);
		}
	}

	free(d->dedup_table);
	d->dedup_table = NULL;
}

static void btf_dedup_free(struct btf_dedup *d)
{
	btf_dedup_table_free(d);

	free(d->map);
	d->map = NULL;

	free(d->hypot_map);
	d->hypot_map = NULL;

	free(d->hypot_list);
	d->hypot_list = NULL;

	free(d);
}

/* Find closest power of two >= to size, capped at 2^max_size_log */
static __u32 roundup_pow2_max(__u32 size, int max_size_log)
{
	int i;

	for (i = 0; i < max_size_log  && (1U << i) < size;  i++)
		;
	return 1U << i;
}


static struct btf_dedup *btf_dedup_new(struct btf *btf, struct btf_ext *btf_ext,
				       const struct btf_dedup_opts *opts)
{
	struct btf_dedup *d = calloc(1, sizeof(struct btf_dedup));
	int i, err = 0;
	__u32 sz;

	if (!d)
		return ERR_PTR(-ENOMEM);

	d->opts.dont_resolve_fwds = opts && opts->dont_resolve_fwds;
	sz = opts && opts->dedup_table_size ? opts->dedup_table_size
					    : BTF_DEDUP_TABLE_DEFAULT_SIZE;
	sz = roundup_pow2_max(sz, BTF_DEDUP_TABLE_MAX_SIZE_LOG);
	d->opts.dedup_table_size = sz;

	d->btf = btf;
	d->btf_ext = btf_ext;

	d->dedup_table = calloc(d->opts.dedup_table_size,
				sizeof(struct btf_dedup_node *));
	if (!d->dedup_table) {
		err = -ENOMEM;
		goto done;
	}

	d->map = malloc(sizeof(__u32) * (1 + btf->nr_types));
	if (!d->map) {
		err = -ENOMEM;
		goto done;
	}
	/* special BTF "void" type is made canonical immediately */
	d->map[0] = 0;
	for (i = 1; i <= btf->nr_types; i++)
		d->map[i] = BTF_UNPROCESSED_ID;

	d->hypot_map = malloc(sizeof(__u32) * (1 + btf->nr_types));
	if (!d->hypot_map) {
		err = -ENOMEM;
		goto done;
	}
	for (i = 0; i <= btf->nr_types; i++)
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

	for (i = 1; i <= d->btf->nr_types; i++) {
		t = d->btf->types[i];
		r = fn(&t->name_off, ctx);
		if (r)
			return r;

		switch (BTF_INFO_KIND(t->info)) {
		case BTF_KIND_STRUCT:
		case BTF_KIND_UNION: {
			struct btf_member *m = (struct btf_member *)(t + 1);
			__u16 vlen = BTF_INFO_VLEN(t->info);

			for (j = 0; j < vlen; j++) {
				r = fn(&m->name_off, ctx);
				if (r)
					return r;
				m++;
			}
			break;
		}
		case BTF_KIND_ENUM: {
			struct btf_enum *m = (struct btf_enum *)(t + 1);
			__u16 vlen = BTF_INFO_VLEN(t->info);

			for (j = 0; j < vlen; j++) {
				r = fn(&m->name_off, ctx);
				if (r)
					return r;
				m++;
			}
			break;
		}
		case BTF_KIND_FUNC_PROTO: {
			struct btf_param *m = (struct btf_param *)(t + 1);
			__u16 vlen = BTF_INFO_VLEN(t->info);

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

static int str_sort_by_content(const void *a1, const void *a2)
{
	const struct btf_str_ptr *p1 = a1;
	const struct btf_str_ptr *p2 = a2;

	return strcmp(p1->str, p2->str);
}

static int str_sort_by_offset(const void *a1, const void *a2)
{
	const struct btf_str_ptr *p1 = a1;
	const struct btf_str_ptr *p2 = a2;

	if (p1->str != p2->str)
		return p1->str < p2->str ? -1 : 1;
	return 0;
}

static int btf_dedup_str_ptr_cmp(const void *str_ptr, const void *pelem)
{
	const struct btf_str_ptr *p = pelem;

	if (str_ptr != p->str)
		return (const char *)str_ptr < p->str ? -1 : 1;
	return 0;
}

static int btf_str_mark_as_used(__u32 *str_off_ptr, void *ctx)
{
	struct btf_str_ptrs *strs;
	struct btf_str_ptr *s;

	if (*str_off_ptr == 0)
		return 0;

	strs = ctx;
	s = bsearch(strs->data + *str_off_ptr, strs->ptrs, strs->cnt,
		    sizeof(struct btf_str_ptr), btf_dedup_str_ptr_cmp);
	if (!s)
		return -EINVAL;
	s->used = true;
	return 0;
}

static int btf_str_remap_offset(__u32 *str_off_ptr, void *ctx)
{
	struct btf_str_ptrs *strs;
	struct btf_str_ptr *s;

	if (*str_off_ptr == 0)
		return 0;

	strs = ctx;
	s = bsearch(strs->data + *str_off_ptr, strs->ptrs, strs->cnt,
		    sizeof(struct btf_str_ptr), btf_dedup_str_ptr_cmp);
	if (!s)
		return -EINVAL;
	*str_off_ptr = s->new_off;
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
	const struct btf_header *hdr = d->btf->hdr;
	char *start = (char *)d->btf->nohdr_data + hdr->str_off;
	char *end = start + d->btf->hdr->str_len;
	char *p = start, *tmp_strs = NULL;
	struct btf_str_ptrs strs = {
		.cnt = 0,
		.cap = 0,
		.ptrs = NULL,
		.data = start,
	};
	int i, j, err = 0, grp_idx;
	bool grp_used;

	/* build index of all strings */
	while (p < end) {
		if (strs.cnt + 1 > strs.cap) {
			struct btf_str_ptr *new_ptrs;

			strs.cap += max(strs.cnt / 2, 16);
			new_ptrs = realloc(strs.ptrs,
					   sizeof(strs.ptrs[0]) * strs.cap);
			if (!new_ptrs) {
				err = -ENOMEM;
				goto done;
			}
			strs.ptrs = new_ptrs;
		}

		strs.ptrs[strs.cnt].str = p;
		strs.ptrs[strs.cnt].used = false;

		p += strlen(p) + 1;
		strs.cnt++;
	}

	/* temporary storage for deduplicated strings */
	tmp_strs = malloc(d->btf->hdr->str_len);
	if (!tmp_strs) {
		err = -ENOMEM;
		goto done;
	}

	/* mark all used strings */
	strs.ptrs[0].used = true;
	err = btf_for_each_str_off(d, btf_str_mark_as_used, &strs);
	if (err)
		goto done;

	/* sort strings by context, so that we can identify duplicates */
	qsort(strs.ptrs, strs.cnt, sizeof(strs.ptrs[0]), str_sort_by_content);

	/*
	 * iterate groups of equal strings and if any instance in a group was
	 * referenced, emit single instance and remember new offset
	 */
	p = tmp_strs;
	grp_idx = 0;
	grp_used = strs.ptrs[0].used;
	/* iterate past end to avoid code duplication after loop */
	for (i = 1; i <= strs.cnt; i++) {
		/*
		 * when i == strs.cnt, we want to skip string comparison and go
		 * straight to handling last group of strings (otherwise we'd
		 * need to handle last group after the loop w/ duplicated code)
		 */
		if (i < strs.cnt &&
		    !strcmp(strs.ptrs[i].str, strs.ptrs[grp_idx].str)) {
			grp_used = grp_used || strs.ptrs[i].used;
			continue;
		}

		/*
		 * this check would have been required after the loop to handle
		 * last group of strings, but due to <= condition in a loop
		 * we avoid that duplication
		 */
		if (grp_used) {
			int new_off = p - tmp_strs;
			__u32 len = strlen(strs.ptrs[grp_idx].str);

			memmove(p, strs.ptrs[grp_idx].str, len + 1);
			for (j = grp_idx; j < i; j++)
				strs.ptrs[j].new_off = new_off;
			p += len + 1;
		}

		if (i < strs.cnt) {
			grp_idx = i;
			grp_used = strs.ptrs[i].used;
		}
	}

	/* replace original strings with deduped ones */
	d->btf->hdr->str_len = p - tmp_strs;
	memmove(start, tmp_strs, d->btf->hdr->str_len);
	end = start + d->btf->hdr->str_len;

	/* restore original order for further binary search lookups */
	qsort(strs.ptrs, strs.cnt, sizeof(strs.ptrs[0]), str_sort_by_offset);

	/* remap string offsets */
	err = btf_for_each_str_off(d, btf_str_remap_offset, &strs);
	if (err)
		goto done;

	d->btf->hdr->str_len = end - start;

done:
	free(tmp_strs);
	free(strs.ptrs);
	return err;
}

static __u32 btf_hash_common(struct btf_type *t)
{
	__u32 h;

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
static __u32 btf_hash_int(struct btf_type *t)
{
	__u32 info = *(__u32 *)(t + 1);
	__u32 h;

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
static __u32 btf_hash_enum(struct btf_type *t)
{
	__u32 h;

	/* don't hash vlen and enum members to support enum fwd resolving */
	h = hash_combine(0, t->name_off);
	h = hash_combine(h, t->info & ~0xffff);
	h = hash_combine(h, t->size);
	return h;
}

/* Check structural equality of two ENUMs. */
static bool btf_equal_enum(struct btf_type *t1, struct btf_type *t2)
{
	struct btf_enum *m1, *m2;
	__u16 vlen;
	int i;

	if (!btf_equal_common(t1, t2))
		return false;

	vlen = BTF_INFO_VLEN(t1->info);
	m1 = (struct btf_enum *)(t1 + 1);
	m2 = (struct btf_enum *)(t2 + 1);
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
	return BTF_INFO_KIND(t->info) == BTF_KIND_ENUM &&
	       BTF_INFO_VLEN(t->info) == 0;
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
static __u32 btf_hash_struct(struct btf_type *t)
{
	struct btf_member *member = (struct btf_member *)(t + 1);
	__u32 vlen = BTF_INFO_VLEN(t->info);
	__u32 h = btf_hash_common(t);
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
	struct btf_member *m1, *m2;
	__u16 vlen;
	int i;

	if (!btf_equal_common(t1, t2))
		return false;

	vlen = BTF_INFO_VLEN(t1->info);
	m1 = (struct btf_member *)(t1 + 1);
	m2 = (struct btf_member *)(t2 + 1);
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
static __u32 btf_hash_array(struct btf_type *t)
{
	struct btf_array *info = (struct btf_array *)(t + 1);
	__u32 h = btf_hash_common(t);

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
	struct btf_array *info1, *info2;

	if (!btf_equal_common(t1, t2))
		return false;

	info1 = (struct btf_array *)(t1 + 1);
	info2 = (struct btf_array *)(t2 + 1);
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
	struct btf_array *info1, *info2;

	if (!btf_equal_common(t1, t2))
		return false;

	info1 = (struct btf_array *)(t1 + 1);
	info2 = (struct btf_array *)(t2 + 1);
	return info1->nelems == info2->nelems;
}

/*
 * Calculate type signature hash of FUNC_PROTO, including referenced type IDs,
 * under assumption that they were already resolved to canonical type IDs and
 * are not going to change.
 */
static inline __u32 btf_hash_fnproto(struct btf_type *t)
{
	struct btf_param *member = (struct btf_param *)(t + 1);
	__u16 vlen = BTF_INFO_VLEN(t->info);
	__u32 h = btf_hash_common(t);
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
static inline bool btf_equal_fnproto(struct btf_type *t1, struct btf_type *t2)
{
	struct btf_param *m1, *m2;
	__u16 vlen;
	int i;

	if (!btf_equal_common(t1, t2))
		return false;

	vlen = BTF_INFO_VLEN(t1->info);
	m1 = (struct btf_param *)(t1 + 1);
	m2 = (struct btf_param *)(t2 + 1);
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
static inline bool btf_compat_fnproto(struct btf_type *t1, struct btf_type *t2)
{
	struct btf_param *m1, *m2;
	__u16 vlen;
	int i;

	/* skip return type ID */
	if (t1->name_off != t2->name_off || t1->info != t2->info)
		return false;

	vlen = BTF_INFO_VLEN(t1->info);
	m1 = (struct btf_param *)(t1 + 1);
	m2 = (struct btf_param *)(t2 + 1);
	for (i = 0; i < vlen; i++) {
		if (m1->name_off != m2->name_off)
			return false;
		m1++;
		m2++;
	}
	return true;
}

/*
 * Deduplicate primitive types, that can't reference other types, by calculating
 * their type signature hash and comparing them with any possible canonical
 * candidate. If no canonical candidate matches, type itself is marked as
 * canonical and is added into `btf_dedup->dedup_table` as another candidate.
 */
static int btf_dedup_prim_type(struct btf_dedup *d, __u32 type_id)
{
	struct btf_type *t = d->btf->types[type_id];
	struct btf_type *cand;
	struct btf_dedup_node *cand_node;
	/* if we don't find equivalent type, then we are canonical */
	__u32 new_id = type_id;
	__u32 h;

	switch (BTF_INFO_KIND(t->info)) {
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
		return 0;

	case BTF_KIND_INT:
		h = btf_hash_int(t);
		for_each_dedup_cand(d, h, cand_node) {
			cand = d->btf->types[cand_node->type_id];
			if (btf_equal_int(t, cand)) {
				new_id = cand_node->type_id;
				break;
			}
		}
		break;

	case BTF_KIND_ENUM:
		h = btf_hash_enum(t);
		for_each_dedup_cand(d, h, cand_node) {
			cand = d->btf->types[cand_node->type_id];
			if (btf_equal_enum(t, cand)) {
				new_id = cand_node->type_id;
				break;
			}
			if (d->opts.dont_resolve_fwds)
				continue;
			if (btf_compat_enum(t, cand)) {
				if (btf_is_enum_fwd(t)) {
					/* resolve fwd to full enum */
					new_id = cand_node->type_id;
					break;
				}
				/* resolve canonical enum fwd to full enum */
				d->map[cand_node->type_id] = type_id;
			}
		}
		break;

	case BTF_KIND_FWD:
		h = btf_hash_common(t);
		for_each_dedup_cand(d, h, cand_node) {
			cand = d->btf->types[cand_node->type_id];
			if (btf_equal_common(t, cand)) {
				new_id = cand_node->type_id;
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

	for (i = 1; i <= d->btf->nr_types; i++) {
		err = btf_dedup_prim_type(d, i);
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

	if (BTF_INFO_KIND(d->btf->types[type_id]->info) != BTF_KIND_FWD)
		return type_id;

	while (is_type_mapped(d, type_id) && d->map[type_id] != type_id)
		type_id = d->map[type_id];

	if (BTF_INFO_KIND(d->btf->types[type_id]->info) != BTF_KIND_FWD)
		return type_id;

	return orig_type_id;
}


static inline __u16 btf_fwd_kind(struct btf_type *t)
{
	return BTF_INFO_KFLAG(t->info) ? BTF_KIND_UNION : BTF_KIND_STRUCT;
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
	if (hypot_type_id <= BTF_MAX_NR_TYPES)
		return hypot_type_id == cand_id;

	if (btf_dedup_hypot_map_add(d, canon_id, cand_id))
		return -ENOMEM;

	cand_type = d->btf->types[cand_id];
	canon_type = d->btf->types[canon_id];
	cand_kind = BTF_INFO_KIND(cand_type->info);
	canon_kind = BTF_INFO_KIND(canon_type->info);

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
		struct btf_array *cand_arr, *canon_arr;

		if (!btf_compat_array(cand_type, canon_type))
			return 0;
		cand_arr = (struct btf_array *)(cand_type + 1);
		canon_arr = (struct btf_array *)(canon_type + 1);
		eq = btf_dedup_is_equiv(d,
			cand_arr->index_type, canon_arr->index_type);
		if (eq <= 0)
			return eq;
		return btf_dedup_is_equiv(d, cand_arr->type, canon_arr->type);
	}

	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION: {
		struct btf_member *cand_m, *canon_m;
		__u16 vlen;

		if (!btf_shallow_equal_struct(cand_type, canon_type))
			return 0;
		vlen = BTF_INFO_VLEN(cand_type->info);
		cand_m = (struct btf_member *)(cand_type + 1);
		canon_m = (struct btf_member *)(canon_type + 1);
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
		struct btf_param *cand_p, *canon_p;
		__u16 vlen;

		if (!btf_compat_fnproto(cand_type, canon_type))
			return 0;
		eq = btf_dedup_is_equiv(d, cand_type->type, canon_type->type);
		if (eq <= 0)
			return eq;
		vlen = BTF_INFO_VLEN(cand_type->info);
		cand_p = (struct btf_param *)(cand_type + 1);
		canon_p = (struct btf_param *)(canon_type + 1);
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
	__u32 cand_type_id, targ_type_id;
	__u16 t_kind, c_kind;
	__u32 t_id, c_id;
	int i;

	for (i = 0; i < d->hypot_cnt; i++) {
		cand_type_id = d->hypot_list[i];
		targ_type_id = d->hypot_map[cand_type_id];
		t_id = resolve_type_id(d, targ_type_id);
		c_id = resolve_type_id(d, cand_type_id);
		t_kind = BTF_INFO_KIND(d->btf->types[t_id]->info);
		c_kind = BTF_INFO_KIND(d->btf->types[c_id]->info);
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
		if (t_kind != BTF_KIND_FWD && c_kind == BTF_KIND_FWD)
			d->map[c_id] = t_id;
		else if (t_kind == BTF_KIND_FWD && c_kind != BTF_KIND_FWD)
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
	struct btf_dedup_node *cand_node;
	struct btf_type *cand_type, *t;
	/* if we don't find equivalent type, then we are canonical */
	__u32 new_id = type_id;
	__u16 kind;
	__u32 h;

	/* already deduped or is in process of deduping (loop detected) */
	if (d->map[type_id] <= BTF_MAX_NR_TYPES)
		return 0;

	t = d->btf->types[type_id];
	kind = BTF_INFO_KIND(t->info);

	if (kind != BTF_KIND_STRUCT && kind != BTF_KIND_UNION)
		return 0;

	h = btf_hash_struct(t);
	for_each_dedup_cand(d, h, cand_node) {
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
		cand_type = d->btf->types[cand_node->type_id];
		if (!btf_shallow_equal_struct(t, cand_type))
			continue;

		btf_dedup_clear_hypot_map(d);
		eq = btf_dedup_is_equiv(d, type_id, cand_node->type_id);
		if (eq < 0)
			return eq;
		if (!eq)
			continue;
		new_id = cand_node->type_id;
		btf_dedup_merge_hypot_map(d);
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

	for (i = 1; i <= d->btf->nr_types; i++) {
		err = btf_dedup_struct_type(d, i);
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
	struct btf_dedup_node *cand_node;
	struct btf_type *t, *cand;
	/* if we don't find equivalent type, then we are representative type */
	__u32 new_id = type_id;
	int ref_type_id;
	__u32 h;

	if (d->map[type_id] == BTF_IN_PROGRESS_ID)
		return -ELOOP;
	if (d->map[type_id] <= BTF_MAX_NR_TYPES)
		return resolve_type_id(d, type_id);

	t = d->btf->types[type_id];
	d->map[type_id] = BTF_IN_PROGRESS_ID;

	switch (BTF_INFO_KIND(t->info)) {
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
		for_each_dedup_cand(d, h, cand_node) {
			cand = d->btf->types[cand_node->type_id];
			if (btf_equal_common(t, cand)) {
				new_id = cand_node->type_id;
				break;
			}
		}
		break;

	case BTF_KIND_ARRAY: {
		struct btf_array *info = (struct btf_array *)(t + 1);

		ref_type_id = btf_dedup_ref_type(d, info->type);
		if (ref_type_id < 0)
			return ref_type_id;
		info->type = ref_type_id;

		ref_type_id = btf_dedup_ref_type(d, info->index_type);
		if (ref_type_id < 0)
			return ref_type_id;
		info->index_type = ref_type_id;

		h = btf_hash_array(t);
		for_each_dedup_cand(d, h, cand_node) {
			cand = d->btf->types[cand_node->type_id];
			if (btf_equal_array(t, cand)) {
				new_id = cand_node->type_id;
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

		vlen = BTF_INFO_VLEN(t->info);
		param = (struct btf_param *)(t + 1);
		for (i = 0; i < vlen; i++) {
			ref_type_id = btf_dedup_ref_type(d, param->type);
			if (ref_type_id < 0)
				return ref_type_id;
			param->type = ref_type_id;
			param++;
		}

		h = btf_hash_fnproto(t);
		for_each_dedup_cand(d, h, cand_node) {
			cand = d->btf->types[cand_node->type_id];
			if (btf_equal_fnproto(t, cand)) {
				new_id = cand_node->type_id;
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

	for (i = 1; i <= d->btf->nr_types; i++) {
		err = btf_dedup_ref_type(d, i);
		if (err < 0)
			return err;
	}
	btf_dedup_table_free(d);
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
	struct btf_type **new_types;
	__u32 next_type_id = 1;
	char *types_start, *p;
	int i, len;

	/* we are going to reuse hypot_map to store compaction remapping */
	d->hypot_map[0] = 0;
	for (i = 1; i <= d->btf->nr_types; i++)
		d->hypot_map[i] = BTF_UNPROCESSED_ID;

	types_start = d->btf->nohdr_data + d->btf->hdr->type_off;
	p = types_start;

	for (i = 1; i <= d->btf->nr_types; i++) {
		if (d->map[i] != i)
			continue;

		len = btf_type_size(d->btf->types[i]);
		if (len < 0)
			return len;

		memmove(p, d->btf->types[i], len);
		d->hypot_map[i] = next_type_id;
		d->btf->types[next_type_id] = (struct btf_type *)p;
		p += len;
		next_type_id++;
	}

	/* shrink struct btf's internal types index and update btf_header */
	d->btf->nr_types = next_type_id - 1;
	d->btf->types_size = d->btf->nr_types;
	d->btf->hdr->type_len = p - types_start;
	new_types = realloc(d->btf->types,
			    (1 + d->btf->nr_types) * sizeof(struct btf_type *));
	if (!new_types)
		return -ENOMEM;
	d->btf->types = new_types;

	/* make sure string section follows type information without gaps */
	d->btf->hdr->str_off = p - (char *)d->btf->nohdr_data;
	memmove(p, d->btf->strings, d->btf->hdr->str_len);
	d->btf->strings = p;
	p += d->btf->hdr->str_len;

	d->btf->data_size = p - (char *)d->btf->data;
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
	struct btf_type *t = d->btf->types[type_id];
	int i, r;

	switch (BTF_INFO_KIND(t->info)) {
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
		r = btf_dedup_remap_type_id(d, t->type);
		if (r < 0)
			return r;
		t->type = r;
		break;

	case BTF_KIND_ARRAY: {
		struct btf_array *arr_info = (struct btf_array *)(t + 1);

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
		struct btf_member *member = (struct btf_member *)(t + 1);
		__u16 vlen = BTF_INFO_VLEN(t->info);

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
		struct btf_param *param = (struct btf_param *)(t + 1);
		__u16 vlen = BTF_INFO_VLEN(t->info);

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

	default:
		return -EINVAL;
	}

	return 0;
}

static int btf_dedup_remap_types(struct btf_dedup *d)
{
	int i, r;

	for (i = 1; i <= d->btf->nr_types; i++) {
		r = btf_dedup_remap_type(d, i);
		if (r < 0)
			return r;
	}
	return 0;
}
