// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <stdlib.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <bpf/bpf.h>
#include "bpf-utils.h"
#include "debug.h"

struct bpil_array_desc {
	int	array_offset;	/* e.g. offset of jited_prog_insns */
	int	count_offset;	/* e.g. offset of jited_prog_len */
	int	size_offset;	/* > 0: offset of rec size,
				 * < 0: fix size of -size_offset
				 */
};

static struct bpil_array_desc bpil_array_desc[] = {
	[PERF_BPIL_JITED_INSNS] = {
		offsetof(struct bpf_prog_info, jited_prog_insns),
		offsetof(struct bpf_prog_info, jited_prog_len),
		-1,
	},
	[PERF_BPIL_XLATED_INSNS] = {
		offsetof(struct bpf_prog_info, xlated_prog_insns),
		offsetof(struct bpf_prog_info, xlated_prog_len),
		-1,
	},
	[PERF_BPIL_MAP_IDS] = {
		offsetof(struct bpf_prog_info, map_ids),
		offsetof(struct bpf_prog_info, nr_map_ids),
		-(int)sizeof(__u32),
	},
	[PERF_BPIL_JITED_KSYMS] = {
		offsetof(struct bpf_prog_info, jited_ksyms),
		offsetof(struct bpf_prog_info, nr_jited_ksyms),
		-(int)sizeof(__u64),
	},
	[PERF_BPIL_JITED_FUNC_LENS] = {
		offsetof(struct bpf_prog_info, jited_func_lens),
		offsetof(struct bpf_prog_info, nr_jited_func_lens),
		-(int)sizeof(__u32),
	},
	[PERF_BPIL_FUNC_INFO] = {
		offsetof(struct bpf_prog_info, func_info),
		offsetof(struct bpf_prog_info, nr_func_info),
		offsetof(struct bpf_prog_info, func_info_rec_size),
	},
	[PERF_BPIL_LINE_INFO] = {
		offsetof(struct bpf_prog_info, line_info),
		offsetof(struct bpf_prog_info, nr_line_info),
		offsetof(struct bpf_prog_info, line_info_rec_size),
	},
	[PERF_BPIL_JITED_LINE_INFO] = {
		offsetof(struct bpf_prog_info, jited_line_info),
		offsetof(struct bpf_prog_info, nr_jited_line_info),
		offsetof(struct bpf_prog_info, jited_line_info_rec_size),
	},
	[PERF_BPIL_PROG_TAGS] = {
		offsetof(struct bpf_prog_info, prog_tags),
		offsetof(struct bpf_prog_info, nr_prog_tags),
		-(int)sizeof(__u8) * BPF_TAG_SIZE,
	},

};

static __u32 bpf_prog_info_read_offset_u32(struct bpf_prog_info *info,
					   int offset)
{
	__u32 *array = (__u32 *)info;

	if (offset >= 0)
		return array[offset / sizeof(__u32)];
	return -(int)offset;
}

static __u64 bpf_prog_info_read_offset_u64(struct bpf_prog_info *info,
					   int offset)
{
	__u64 *array = (__u64 *)info;

	if (offset >= 0)
		return array[offset / sizeof(__u64)];
	return -(int)offset;
}

static void bpf_prog_info_set_offset_u32(struct bpf_prog_info *info, int offset,
					 __u32 val)
{
	__u32 *array = (__u32 *)info;

	if (offset >= 0)
		array[offset / sizeof(__u32)] = val;
}

static void bpf_prog_info_set_offset_u64(struct bpf_prog_info *info, int offset,
					 __u64 val)
{
	__u64 *array = (__u64 *)info;

	if (offset >= 0)
		array[offset / sizeof(__u64)] = val;
}

struct perf_bpil *
get_bpf_prog_info_linear(int fd, __u64 arrays)
{
	struct bpf_prog_info info = {};
	struct perf_bpil *info_linear;
	__u32 info_len = sizeof(info);
	__u32 data_len = 0;
	int i, err;
	void *ptr;

	if (arrays >> PERF_BPIL_LAST_ARRAY)
		return ERR_PTR(-EINVAL);

	/* step 1: get array dimensions */
	err = bpf_obj_get_info_by_fd(fd, &info, &info_len);
	if (err) {
		pr_debug("can't get prog info: %s", strerror(errno));
		return ERR_PTR(-EFAULT);
	}

	/* step 2: calculate total size of all arrays */
	for (i = PERF_BPIL_FIRST_ARRAY; i < PERF_BPIL_LAST_ARRAY; ++i) {
		bool include_array = (arrays & (1UL << i)) > 0;
		struct bpil_array_desc *desc;
		__u32 count, size;

		desc = bpil_array_desc + i;

		/* kernel is too old to support this field */
		if (info_len < desc->array_offset + sizeof(__u32) ||
		    info_len < desc->count_offset + sizeof(__u32) ||
		    (desc->size_offset > 0 && info_len < (__u32)desc->size_offset))
			include_array = false;

		if (!include_array) {
			arrays &= ~(1UL << i);	/* clear the bit */
			continue;
		}

		count = bpf_prog_info_read_offset_u32(&info, desc->count_offset);
		size  = bpf_prog_info_read_offset_u32(&info, desc->size_offset);

		data_len += roundup(count * size, sizeof(__u64));
	}

	/* step 3: allocate continuous memory */
	info_linear = malloc(sizeof(struct perf_bpil) + data_len);
	if (!info_linear)
		return ERR_PTR(-ENOMEM);

	/* step 4: fill data to info_linear->info */
	info_linear->arrays = arrays;
	memset(&info_linear->info, 0, sizeof(info));
	ptr = info_linear->data;

	for (i = PERF_BPIL_FIRST_ARRAY; i < PERF_BPIL_LAST_ARRAY; ++i) {
		struct bpil_array_desc *desc;
		__u32 count, size;

		if ((arrays & (1UL << i)) == 0)
			continue;

		desc  = bpil_array_desc + i;
		count = bpf_prog_info_read_offset_u32(&info, desc->count_offset);
		size  = bpf_prog_info_read_offset_u32(&info, desc->size_offset);
		bpf_prog_info_set_offset_u32(&info_linear->info,
					     desc->count_offset, count);
		bpf_prog_info_set_offset_u32(&info_linear->info,
					     desc->size_offset, size);
		bpf_prog_info_set_offset_u64(&info_linear->info,
					     desc->array_offset,
					     ptr_to_u64(ptr));
		ptr += roundup(count * size, sizeof(__u64));
	}

	/* step 5: call syscall again to get required arrays */
	err = bpf_obj_get_info_by_fd(fd, &info_linear->info, &info_len);
	if (err) {
		pr_debug("can't get prog info: %s", strerror(errno));
		free(info_linear);
		return ERR_PTR(-EFAULT);
	}

	/* step 6: verify the data */
	for (i = PERF_BPIL_FIRST_ARRAY; i < PERF_BPIL_LAST_ARRAY; ++i) {
		struct bpil_array_desc *desc;
		__u32 v1, v2;

		if ((arrays & (1UL << i)) == 0)
			continue;

		desc = bpil_array_desc + i;
		v1 = bpf_prog_info_read_offset_u32(&info, desc->count_offset);
		v2 = bpf_prog_info_read_offset_u32(&info_linear->info,
						   desc->count_offset);
		if (v1 != v2)
			pr_warning("%s: mismatch in element count\n", __func__);

		v1 = bpf_prog_info_read_offset_u32(&info, desc->size_offset);
		v2 = bpf_prog_info_read_offset_u32(&info_linear->info,
						   desc->size_offset);
		if (v1 != v2)
			pr_warning("%s: mismatch in rec size\n", __func__);
	}

	/* step 7: update info_len and data_len */
	info_linear->info_len = sizeof(struct bpf_prog_info);
	info_linear->data_len = data_len;

	return info_linear;
}

void bpil_addr_to_offs(struct perf_bpil *info_linear)
{
	int i;

	for (i = PERF_BPIL_FIRST_ARRAY; i < PERF_BPIL_LAST_ARRAY; ++i) {
		struct bpil_array_desc *desc;
		__u64 addr, offs;

		if ((info_linear->arrays & (1UL << i)) == 0)
			continue;

		desc = bpil_array_desc + i;
		addr = bpf_prog_info_read_offset_u64(&info_linear->info,
						     desc->array_offset);
		offs = addr - ptr_to_u64(info_linear->data);
		bpf_prog_info_set_offset_u64(&info_linear->info,
					     desc->array_offset, offs);
	}
}

void bpil_offs_to_addr(struct perf_bpil *info_linear)
{
	int i;

	for (i = PERF_BPIL_FIRST_ARRAY; i < PERF_BPIL_LAST_ARRAY; ++i) {
		struct bpil_array_desc *desc;
		__u64 addr, offs;

		if ((info_linear->arrays & (1UL << i)) == 0)
			continue;

		desc = bpil_array_desc + i;
		offs = bpf_prog_info_read_offset_u64(&info_linear->info,
						     desc->array_offset);
		addr = offs + ptr_to_u64(info_linear->data);
		bpf_prog_info_set_offset_u64(&info_linear->info,
					     desc->array_offset, addr);
	}
}
