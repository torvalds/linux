/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */

#ifndef __PERF_BPF_UTILS_H
#define __PERF_BPF_UTILS_H

#define ptr_to_u64(ptr)    ((__u64)(unsigned long)(ptr))

#ifdef HAVE_LIBBPF_SUPPORT

#include <bpf/libbpf.h>
#include <bpf/libbpf_version.h>

#define LIBBPF_CURRENT_VERSION_GEQ(major, minor)                       \
       (LIBBPF_MAJOR_VERSION > (major) ||                              \
        (LIBBPF_MAJOR_VERSION == (major) && LIBBPF_MINOR_VERSION >= (minor)))

#if LIBBPF_CURRENT_VERSION_GEQ(1, 7)
// libbpf 1.7+ support the btf_dump_type_data_opts.emit_strings option.
#define HAVE_LIBBPF_STRINGS_SUPPORT 1
#endif

/*
 * Get bpf_prog_info in continuous memory
 *
 * struct bpf_prog_info has multiple arrays. The user has option to choose
 * arrays to fetch from kernel. The following APIs provide an uniform way to
 * fetch these data. All arrays in bpf_prog_info are stored in a single
 * continuous memory region. This makes it easy to store the info in a
 * file.
 *
 * Before writing perf_bpil to files, it is necessary to
 * translate pointers in bpf_prog_info to offsets. Helper functions
 * bpil_addr_to_offs() and bpil_offs_to_addr()
 * are introduced to switch between pointers and offsets.
 *
 * Examples:
 *   # To fetch map_ids and prog_tags:
 *   __u64 arrays = (1UL << PERF_BPIL_MAP_IDS) |
 *           (1UL << PERF_BPIL_PROG_TAGS);
 *   struct perf_bpil *info_linear =
 *           get_bpf_prog_info_linear(fd, arrays);
 *
 *   # To save data in file
 *   bpil_addr_to_offs(info_linear);
 *   write(f, info_linear, sizeof(*info_linear) + info_linear->data_len);
 *
 *   # To read data from file
 *   read(f, info_linear, <proper_size>);
 *   bpil_offs_to_addr(info_linear);
 */
enum perf_bpil_array_types {
	PERF_BPIL_FIRST_ARRAY = 0,
	PERF_BPIL_JITED_INSNS = 0,
	PERF_BPIL_XLATED_INSNS,
	PERF_BPIL_MAP_IDS,
	PERF_BPIL_JITED_KSYMS,
	PERF_BPIL_JITED_FUNC_LENS,
	PERF_BPIL_FUNC_INFO,
	PERF_BPIL_LINE_INFO,
	PERF_BPIL_JITED_LINE_INFO,
	PERF_BPIL_PROG_TAGS,
	PERF_BPIL_LAST_ARRAY,
};

struct perf_bpil {
	/* size of struct bpf_prog_info, when the tool is compiled */
	__u32			info_len;
	/* total bytes allocated for data, round up to 8 bytes */
	__u32			data_len;
	/* which arrays are included in data */
	__u64			arrays;
	struct bpf_prog_info	info;
	__u8			data[];
};

struct perf_bpil *
get_bpf_prog_info_linear(int fd, __u64 arrays);

void
bpil_addr_to_offs(struct perf_bpil *info_linear);

void
bpil_offs_to_addr(struct perf_bpil *info_linear);

#endif /* HAVE_LIBBPF_SUPPORT */
#endif /* __PERF_BPF_UTILS_H */
