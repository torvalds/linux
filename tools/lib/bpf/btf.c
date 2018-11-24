// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
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

struct btf_ext {
	void *func_info;
	__u32 func_info_rec_size;
	__u32 func_info_len;
};

/* The minimum bpf_func_info checked by the loader */
struct bpf_func_info_min {
	__u32   insn_offset;
	__u32   type_id;
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
		case BTF_KIND_FUNC_PROTO:
			next_type += vlen * sizeof(struct btf_param);
			break;
		case BTF_KIND_FUNC:
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

	t = btf__type_by_id(btf, type_id);
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

		t = btf__type_by_id(btf, type_id);
	}

	if (size < 0)
		return -EINVAL;

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

	bzero(ptr, last_size);
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
		bzero(ptr, last_size);
		btf_info.btf = ptr_to_u64(ptr);
		err = bpf_obj_get_info_by_fd(btf_fd, &btf_info, &len);
	}

	if (err || btf_info.btf_size > last_size) {
		err = errno;
		goto exit_free;
	}

	*btf = btf__new((__u8 *)btf_info.btf, btf_info.btf_size, NULL);
	if (IS_ERR(*btf)) {
		err = PTR_ERR(*btf);
		*btf = NULL;
	}

exit_free:
	close(btf_fd);
	free(ptr);

	return err;
}

static int btf_ext_validate_func_info(const void *finfo, __u32 size,
				      btf_print_fn_t err_log)
{
	int sec_hdrlen = sizeof(struct btf_sec_func_info);
	__u32 size_left, num_records, record_size;
	const struct btf_sec_func_info *sinfo;
	__u64 total_record_size;

	/* At least a func_info record size */
	if (size < sizeof(__u32)) {
		elog("BTF.ext func_info record size not found");
		return -EINVAL;
	}

	/* The record size needs to meet below minimum standard */
	record_size = *(__u32 *)finfo;
	if (record_size < sizeof(struct bpf_func_info_min) ||
	    record_size % sizeof(__u32)) {
		elog("BTF.ext func_info invalid record size");
		return -EINVAL;
	}

	sinfo = finfo + sizeof(__u32);
	size_left = size - sizeof(__u32);

	/* If no func_info records, return failure now so .BTF.ext
	 * won't be used.
	 */
	if (!size_left) {
		elog("BTF.ext no func info records");
		return -EINVAL;
	}

	while (size_left) {
		if (size_left < sec_hdrlen) {
			elog("BTF.ext func_info header not found");
			return -EINVAL;
		}

		num_records = sinfo->num_func_info;
		if (num_records == 0) {
			elog("incorrect BTF.ext num_func_info");
			return -EINVAL;
		}

		total_record_size = sec_hdrlen +
				    (__u64)num_records * record_size;
		if (size_left < total_record_size) {
			elog("incorrect BTF.ext num_func_info");
			return -EINVAL;
		}

		size_left -= total_record_size;
		sinfo = (void *)sinfo + total_record_size;
	}

	return 0;
}

static int btf_ext_parse_hdr(__u8 *data, __u32 data_size,
			     btf_print_fn_t err_log)
{
	const struct btf_ext_header *hdr = (struct btf_ext_header *)data;
	__u32 meta_left, last_func_info_pos;
	void *finfo;

	if (data_size < offsetof(struct btf_ext_header, func_info_off) ||
	    data_size < hdr->hdr_len) {
		elog("BTF.ext header not found");
		return -EINVAL;
	}

	if (hdr->magic != BTF_MAGIC) {
		elog("Invalid BTF.ext magic:%x\n", hdr->magic);
		return -EINVAL;
	}

	if (hdr->version != BTF_VERSION) {
		elog("Unsupported BTF.ext version:%u\n", hdr->version);
		return -ENOTSUP;
	}

	if (hdr->flags) {
		elog("Unsupported BTF.ext flags:%x\n", hdr->flags);
		return -ENOTSUP;
	}

	meta_left = data_size - hdr->hdr_len;
	if (!meta_left) {
		elog("BTF.ext has no data\n");
		return -EINVAL;
	}

	if (meta_left < hdr->func_info_off) {
		elog("Invalid BTF.ext func_info section offset:%u\n",
		     hdr->func_info_off);
		return -EINVAL;
	}

	if (hdr->func_info_off & 0x03) {
		elog("BTF.ext func_info section is not aligned to 4 bytes\n");
		return -EINVAL;
	}

	last_func_info_pos = hdr->hdr_len + hdr->func_info_off +
			     hdr->func_info_len;
	if (last_func_info_pos > data_size) {
		elog("Invalid BTF.ext func_info section size:%u\n",
		     hdr->func_info_len);
		return -EINVAL;
	}

	finfo = data + hdr->hdr_len + hdr->func_info_off;
	return btf_ext_validate_func_info(finfo, hdr->func_info_len,
					  err_log);
}

void btf_ext__free(struct btf_ext *btf_ext)
{
	if (!btf_ext)
		return;

	free(btf_ext->func_info);
	free(btf_ext);
}

struct btf_ext *btf_ext__new(__u8 *data, __u32 size, btf_print_fn_t err_log)
{
	const struct btf_ext_header *hdr;
	struct btf_ext *btf_ext;
	void *org_fdata, *fdata;
	__u32 hdrlen, size_u32;
	int err;

	err = btf_ext_parse_hdr(data, size, err_log);
	if (err)
		return ERR_PTR(err);

	btf_ext = calloc(1, sizeof(struct btf_ext));
	if (!btf_ext)
		return ERR_PTR(-ENOMEM);

	hdr = (const struct btf_ext_header *)data;
	hdrlen = hdr->hdr_len;
	size_u32 = sizeof(__u32);
	fdata = malloc(hdr->func_info_len - size_u32);
	if (!fdata) {
		free(btf_ext);
		return ERR_PTR(-ENOMEM);
	}

	/* remember record size and copy rest of func_info data */
	org_fdata = data + hdrlen + hdr->func_info_off;
	btf_ext->func_info_rec_size = *(__u32 *)org_fdata;
	memcpy(fdata, org_fdata + size_u32, hdr->func_info_len - size_u32);
	btf_ext->func_info = fdata;
	btf_ext->func_info_len = hdr->func_info_len - size_u32;

	return btf_ext;
}

int btf_ext__reloc_init(struct btf *btf, struct btf_ext *btf_ext,
			const char *sec_name, void **func_info,
			__u32 *func_info_rec_size, __u32 *func_info_len)
{
	__u32 sec_hdrlen = sizeof(struct btf_sec_func_info);
	__u32 i, record_size, records_len;
	struct btf_sec_func_info *sinfo;
	const char *info_sec_name;
	__s64 remain_len;
	void *data;

	record_size = btf_ext->func_info_rec_size;
	sinfo = btf_ext->func_info;
	remain_len = btf_ext->func_info_len;

	while (remain_len > 0) {
		records_len = sinfo->num_func_info * record_size;
		info_sec_name = btf__name_by_offset(btf, sinfo->sec_name_off);
		if (strcmp(info_sec_name, sec_name)) {
			remain_len -= sec_hdrlen + records_len;
			sinfo = (void *)sinfo + sec_hdrlen + records_len;
			continue;
		}

		data = malloc(records_len);
		if (!data)
			return -ENOMEM;

		memcpy(data, sinfo->data, records_len);

		/* adjust the insn_offset, the data in .BTF.ext is
		 * the actual byte offset, and the kernel expects
		 * the offset in term of bpf_insn.
		 *
		 * adjust the insn offset only, the rest data will
		 * be passed to kernel.
		 */
		for (i = 0; i < sinfo->num_func_info; i++) {
			struct bpf_func_info_min *record;

			record = data + i * record_size;
			record->insn_offset /= sizeof(struct bpf_insn);
		}

		*func_info = data;
		*func_info_len = records_len;
		*func_info_rec_size = record_size;
		return 0;
	}

	return -EINVAL;
}

int btf_ext__reloc(struct btf *btf, struct btf_ext *btf_ext,
		   const char *sec_name, __u32 insns_cnt,
		   void **func_info, __u32 *func_info_len)
{
	__u32 sec_hdrlen = sizeof(struct btf_sec_func_info);
	__u32 i, record_size, existing_flen, records_len;
	struct btf_sec_func_info *sinfo;
	const char *info_sec_name;
	__u64 remain_len;
	void *data;

	record_size = btf_ext->func_info_rec_size;
	sinfo = btf_ext->func_info;
	remain_len = btf_ext->func_info_len;
	while (remain_len > 0) {
		records_len = sinfo->num_func_info * record_size;
		info_sec_name = btf__name_by_offset(btf, sinfo->sec_name_off);
		if (strcmp(info_sec_name, sec_name)) {
			remain_len -= sec_hdrlen + records_len;
			sinfo = (void *)sinfo + sec_hdrlen + records_len;
			continue;
		}

		existing_flen = *func_info_len;
		data = realloc(*func_info, existing_flen + records_len);
		if (!data)
			return -ENOMEM;

		memcpy(data + existing_flen, sinfo->data, records_len);
		/* adjust insn_offset only, the rest data will be passed
		 * to the kernel.
		 */
		for (i = 0; i < sinfo->num_func_info; i++) {
			struct bpf_func_info_min *record;

			record = data + existing_flen + i * record_size;
			record->insn_offset =
				record->insn_offset / sizeof(struct bpf_insn) +
				insns_cnt;
		}
		*func_info = data;
		*func_info_len = existing_flen + records_len;
		return 0;
	}

	return -EINVAL;
}
