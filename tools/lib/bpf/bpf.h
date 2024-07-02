/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */

/*
 * Common BPF ELF operations.
 *
 * Copyright (C) 2013-2015 Alexei Starovoitov <ast@kernel.org>
 * Copyright (C) 2015 Wang Nan <wangnan0@huawei.com>
 * Copyright (C) 2015 Huawei Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License (not later!)
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not,  see <http://www.gnu.org/licenses>
 */
#ifndef __LIBBPF_BPF_H
#define __LIBBPF_BPF_H

#include <linux/bpf.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "libbpf_common.h"
#include "libbpf_legacy.h"

#ifdef __cplusplus
extern "C" {
#endif

LIBBPF_API int libbpf_set_memlock_rlim(size_t memlock_bytes);

struct bpf_map_create_opts {
	size_t sz; /* size of this struct for forward/backward compatibility */

	__u32 btf_fd;
	__u32 btf_key_type_id;
	__u32 btf_value_type_id;
	__u32 btf_vmlinux_value_type_id;

	__u32 inner_map_fd;
	__u32 map_flags;
	__u64 map_extra;

	__u32 numa_node;
	__u32 map_ifindex;
	__s32 value_type_btf_obj_fd;

	__u32 token_fd;
	size_t :0;
};
#define bpf_map_create_opts__last_field token_fd

LIBBPF_API int bpf_map_create(enum bpf_map_type map_type,
			      const char *map_name,
			      __u32 key_size,
			      __u32 value_size,
			      __u32 max_entries,
			      const struct bpf_map_create_opts *opts);

struct bpf_prog_load_opts {
	size_t sz; /* size of this struct for forward/backward compatibility */

	/* libbpf can retry BPF_PROG_LOAD command if bpf() syscall returns
	 * -EAGAIN. This field determines how many attempts libbpf has to
	 *  make. If not specified, libbpf will use default value of 5.
	 */
	int attempts;

	enum bpf_attach_type expected_attach_type;
	__u32 prog_btf_fd;
	__u32 prog_flags;
	__u32 prog_ifindex;
	__u32 kern_version;

	__u32 attach_btf_id;
	__u32 attach_prog_fd;
	__u32 attach_btf_obj_fd;

	const int *fd_array;

	/* .BTF.ext func info data */
	const void *func_info;
	__u32 func_info_cnt;
	__u32 func_info_rec_size;

	/* .BTF.ext line info data */
	const void *line_info;
	__u32 line_info_cnt;
	__u32 line_info_rec_size;

	/* verifier log options */
	__u32 log_level;
	__u32 log_size;
	char *log_buf;
	/* output: actual total log contents size (including termintaing zero).
	 * It could be both larger than original log_size (if log was
	 * truncated), or smaller (if log buffer wasn't filled completely).
	 * If kernel doesn't support this feature, log_size is left unchanged.
	 */
	__u32 log_true_size;
	__u32 token_fd;
	size_t :0;
};
#define bpf_prog_load_opts__last_field token_fd

LIBBPF_API int bpf_prog_load(enum bpf_prog_type prog_type,
			     const char *prog_name, const char *license,
			     const struct bpf_insn *insns, size_t insn_cnt,
			     struct bpf_prog_load_opts *opts);

/* Flags to direct loading requirements */
#define MAPS_RELAX_COMPAT	0x01

/* Recommended log buffer size */
#define BPF_LOG_BUF_SIZE (UINT32_MAX >> 8) /* verifier maximum in kernels <= 5.1 */

struct bpf_btf_load_opts {
	size_t sz; /* size of this struct for forward/backward compatibility */

	/* kernel log options */
	char *log_buf;
	__u32 log_level;
	__u32 log_size;
	/* output: actual total log contents size (including termintaing zero).
	 * It could be both larger than original log_size (if log was
	 * truncated), or smaller (if log buffer wasn't filled completely).
	 * If kernel doesn't support this feature, log_size is left unchanged.
	 */
	__u32 log_true_size;

	__u32 btf_flags;
	__u32 token_fd;
	size_t :0;
};
#define bpf_btf_load_opts__last_field token_fd

LIBBPF_API int bpf_btf_load(const void *btf_data, size_t btf_size,
			    struct bpf_btf_load_opts *opts);

LIBBPF_API int bpf_map_update_elem(int fd, const void *key, const void *value,
				   __u64 flags);

LIBBPF_API int bpf_map_lookup_elem(int fd, const void *key, void *value);
LIBBPF_API int bpf_map_lookup_elem_flags(int fd, const void *key, void *value,
					 __u64 flags);
LIBBPF_API int bpf_map_lookup_and_delete_elem(int fd, const void *key,
					      void *value);
LIBBPF_API int bpf_map_lookup_and_delete_elem_flags(int fd, const void *key,
						    void *value, __u64 flags);
LIBBPF_API int bpf_map_delete_elem(int fd, const void *key);
LIBBPF_API int bpf_map_delete_elem_flags(int fd, const void *key, __u64 flags);
LIBBPF_API int bpf_map_get_next_key(int fd, const void *key, void *next_key);
LIBBPF_API int bpf_map_freeze(int fd);

struct bpf_map_batch_opts {
	size_t sz; /* size of this struct for forward/backward compatibility */
	__u64 elem_flags;
	__u64 flags;
};
#define bpf_map_batch_opts__last_field flags


/**
 * @brief **bpf_map_delete_batch()** allows for batch deletion of multiple
 * elements in a BPF map.
 *
 * @param fd BPF map file descriptor
 * @param keys pointer to an array of *count* keys
 * @param count input and output parameter; on input **count** represents the
 * number of  elements in the map to delete in batch;
 * on output if a non-EFAULT error is returned, **count** represents the number of deleted
 * elements if the output **count** value is not equal to the input **count** value
 * If EFAULT is returned, **count** should not be trusted to be correct.
 * @param opts options for configuring the way the batch deletion works
 * @return 0, on success; negative error code, otherwise (errno is also set to
 * the error code)
 */
LIBBPF_API int bpf_map_delete_batch(int fd, const void *keys,
				    __u32 *count,
				    const struct bpf_map_batch_opts *opts);

/**
 * @brief **bpf_map_lookup_batch()** allows for batch lookup of BPF map elements.
 *
 * The parameter *in_batch* is the address of the first element in the batch to
 * read. *out_batch* is an output parameter that should be passed as *in_batch*
 * to subsequent calls to **bpf_map_lookup_batch()**. NULL can be passed for
 * *in_batch* to indicate that the batched lookup starts from the beginning of
 * the map. Both *in_batch* and *out_batch* must point to memory large enough to
 * hold a single key, except for maps of type **BPF_MAP_TYPE_{HASH, PERCPU_HASH,
 * LRU_HASH, LRU_PERCPU_HASH}**, for which the memory size must be at
 * least 4 bytes wide regardless of key size.
 *
 * The *keys* and *values* are output parameters which must point to memory large enough to
 * hold *count* items based on the key and value size of the map *map_fd*. The *keys*
 * buffer must be of *key_size* * *count*. The *values* buffer must be of
 * *value_size* * *count*.
 *
 * @param fd BPF map file descriptor
 * @param in_batch address of the first element in batch to read, can pass NULL to
 * indicate that the batched lookup starts from the beginning of the map.
 * @param out_batch output parameter that should be passed to next call as *in_batch*
 * @param keys pointer to an array large enough for *count* keys
 * @param values pointer to an array large enough for *count* values
 * @param count input and output parameter; on input it's the number of elements
 * in the map to read in batch; on output it's the number of elements that were
 * successfully read.
 * If a non-EFAULT error is returned, count will be set as the number of elements
 * that were read before the error occurred.
 * If EFAULT is returned, **count** should not be trusted to be correct.
 * @param opts options for configuring the way the batch lookup works
 * @return 0, on success; negative error code, otherwise (errno is also set to
 * the error code)
 */
LIBBPF_API int bpf_map_lookup_batch(int fd, void *in_batch, void *out_batch,
				    void *keys, void *values, __u32 *count,
				    const struct bpf_map_batch_opts *opts);

/**
 * @brief **bpf_map_lookup_and_delete_batch()** allows for batch lookup and deletion
 * of BPF map elements where each element is deleted after being retrieved.
 *
 * @param fd BPF map file descriptor
 * @param in_batch address of the first element in batch to read, can pass NULL to
 * get address of the first element in *out_batch*. If not NULL, must be large
 * enough to hold a key. For **BPF_MAP_TYPE_{HASH, PERCPU_HASH, LRU_HASH,
 * LRU_PERCPU_HASH}**, the memory size must be at least 4 bytes wide regardless
 * of key size.
 * @param out_batch output parameter that should be passed to next call as *in_batch*
 * @param keys pointer to an array of *count* keys
 * @param values pointer to an array large enough for *count* values
 * @param count input and output parameter; on input it's the number of elements
 * in the map to read and delete in batch; on output it represents the number of
 * elements that were successfully read and deleted
 * If a non-**EFAULT** error code is returned and if the output **count** value
 * is not equal to the input **count** value, up to **count** elements may
 * have been deleted.
 * if **EFAULT** is returned up to *count* elements may have been deleted without
 * being returned via the *keys* and *values* output parameters.
 * @param opts options for configuring the way the batch lookup and delete works
 * @return 0, on success; negative error code, otherwise (errno is also set to
 * the error code)
 */
LIBBPF_API int bpf_map_lookup_and_delete_batch(int fd, void *in_batch,
					void *out_batch, void *keys,
					void *values, __u32 *count,
					const struct bpf_map_batch_opts *opts);

/**
 * @brief **bpf_map_update_batch()** updates multiple elements in a map
 * by specifying keys and their corresponding values.
 *
 * The *keys* and *values* parameters must point to memory large enough
 * to hold *count* items based on the key and value size of the map.
 *
 * The *opts* parameter can be used to control how *bpf_map_update_batch()*
 * should handle keys that either do or do not already exist in the map.
 * In particular the *flags* parameter of *bpf_map_batch_opts* can be
 * one of the following:
 *
 * Note that *count* is an input and output parameter, where on output it
 * represents how many elements were successfully updated. Also note that if
 * **EFAULT** then *count* should not be trusted to be correct.
 *
 * **BPF_ANY**
 *    Create new elements or update existing.
 *
 * **BPF_NOEXIST**
 *    Create new elements only if they do not exist.
 *
 * **BPF_EXIST**
 *    Update existing elements.
 *
 * **BPF_F_LOCK**
 *    Update spin_lock-ed map elements. This must be
 *    specified if the map value contains a spinlock.
 *
 * @param fd BPF map file descriptor
 * @param keys pointer to an array of *count* keys
 * @param values pointer to an array of *count* values
 * @param count input and output parameter; on input it's the number of elements
 * in the map to update in batch; on output if a non-EFAULT error is returned,
 * **count** represents the number of updated elements if the output **count**
 * value is not equal to the input **count** value.
 * If EFAULT is returned, **count** should not be trusted to be correct.
 * @param opts options for configuring the way the batch update works
 * @return 0, on success; negative error code, otherwise (errno is also set to
 * the error code)
 */
LIBBPF_API int bpf_map_update_batch(int fd, const void *keys, const void *values,
				    __u32 *count,
				    const struct bpf_map_batch_opts *opts);

struct bpf_obj_pin_opts {
	size_t sz; /* size of this struct for forward/backward compatibility */

	__u32 file_flags;
	int path_fd;

	size_t :0;
};
#define bpf_obj_pin_opts__last_field path_fd

LIBBPF_API int bpf_obj_pin(int fd, const char *pathname);
LIBBPF_API int bpf_obj_pin_opts(int fd, const char *pathname,
				const struct bpf_obj_pin_opts *opts);

struct bpf_obj_get_opts {
	size_t sz; /* size of this struct for forward/backward compatibility */

	__u32 file_flags;
	int path_fd;

	size_t :0;
};
#define bpf_obj_get_opts__last_field path_fd

LIBBPF_API int bpf_obj_get(const char *pathname);
LIBBPF_API int bpf_obj_get_opts(const char *pathname,
				const struct bpf_obj_get_opts *opts);

LIBBPF_API int bpf_prog_attach(int prog_fd, int attachable_fd,
			       enum bpf_attach_type type, unsigned int flags);
LIBBPF_API int bpf_prog_detach(int attachable_fd, enum bpf_attach_type type);
LIBBPF_API int bpf_prog_detach2(int prog_fd, int attachable_fd,
				enum bpf_attach_type type);

struct bpf_prog_attach_opts {
	size_t sz; /* size of this struct for forward/backward compatibility */
	__u32 flags;
	union {
		int replace_prog_fd;
		int replace_fd;
	};
	int relative_fd;
	__u32 relative_id;
	__u64 expected_revision;
	size_t :0;
};
#define bpf_prog_attach_opts__last_field expected_revision

struct bpf_prog_detach_opts {
	size_t sz; /* size of this struct for forward/backward compatibility */
	__u32 flags;
	int relative_fd;
	__u32 relative_id;
	__u64 expected_revision;
	size_t :0;
};
#define bpf_prog_detach_opts__last_field expected_revision

/**
 * @brief **bpf_prog_attach_opts()** attaches the BPF program corresponding to
 * *prog_fd* to a *target* which can represent a file descriptor or netdevice
 * ifindex.
 *
 * @param prog_fd BPF program file descriptor
 * @param target attach location file descriptor or ifindex
 * @param type attach type for the BPF program
 * @param opts options for configuring the attachment
 * @return 0, on success; negative error code, otherwise (errno is also set to
 * the error code)
 */
LIBBPF_API int bpf_prog_attach_opts(int prog_fd, int target,
				    enum bpf_attach_type type,
				    const struct bpf_prog_attach_opts *opts);

/**
 * @brief **bpf_prog_detach_opts()** detaches the BPF program corresponding to
 * *prog_fd* from a *target* which can represent a file descriptor or netdevice
 * ifindex.
 *
 * @param prog_fd BPF program file descriptor
 * @param target detach location file descriptor or ifindex
 * @param type detach type for the BPF program
 * @param opts options for configuring the detachment
 * @return 0, on success; negative error code, otherwise (errno is also set to
 * the error code)
 */
LIBBPF_API int bpf_prog_detach_opts(int prog_fd, int target,
				    enum bpf_attach_type type,
				    const struct bpf_prog_detach_opts *opts);

union bpf_iter_link_info; /* defined in up-to-date linux/bpf.h */
struct bpf_link_create_opts {
	size_t sz; /* size of this struct for forward/backward compatibility */
	__u32 flags;
	union bpf_iter_link_info *iter_info;
	__u32 iter_info_len;
	__u32 target_btf_id;
	union {
		struct {
			__u64 bpf_cookie;
		} perf_event;
		struct {
			__u32 flags;
			__u32 cnt;
			const char **syms;
			const unsigned long *addrs;
			const __u64 *cookies;
		} kprobe_multi;
		struct {
			__u32 flags;
			__u32 cnt;
			const char *path;
			const unsigned long *offsets;
			const unsigned long *ref_ctr_offsets;
			const __u64 *cookies;
			__u32 pid;
		} uprobe_multi;
		struct {
			__u64 cookie;
		} tracing;
		struct {
			__u32 pf;
			__u32 hooknum;
			__s32 priority;
			__u32 flags;
		} netfilter;
		struct {
			__u32 relative_fd;
			__u32 relative_id;
			__u64 expected_revision;
		} tcx;
		struct {
			__u32 relative_fd;
			__u32 relative_id;
			__u64 expected_revision;
		} netkit;
	};
	size_t :0;
};
#define bpf_link_create_opts__last_field uprobe_multi.pid

LIBBPF_API int bpf_link_create(int prog_fd, int target_fd,
			       enum bpf_attach_type attach_type,
			       const struct bpf_link_create_opts *opts);

LIBBPF_API int bpf_link_detach(int link_fd);

struct bpf_link_update_opts {
	size_t sz; /* size of this struct for forward/backward compatibility */
	__u32 flags;	   /* extra flags */
	__u32 old_prog_fd; /* expected old program FD */
	__u32 old_map_fd;  /* expected old map FD */
};
#define bpf_link_update_opts__last_field old_map_fd

LIBBPF_API int bpf_link_update(int link_fd, int new_prog_fd,
			       const struct bpf_link_update_opts *opts);

LIBBPF_API int bpf_iter_create(int link_fd);

struct bpf_prog_test_run_attr {
	int prog_fd;
	int repeat;
	const void *data_in;
	__u32 data_size_in;
	void *data_out;      /* optional */
	__u32 data_size_out; /* in: max length of data_out
			      * out: length of data_out */
	__u32 retval;        /* out: return code of the BPF program */
	__u32 duration;      /* out: average per repetition in ns */
	const void *ctx_in; /* optional */
	__u32 ctx_size_in;
	void *ctx_out;      /* optional */
	__u32 ctx_size_out; /* in: max length of ctx_out
			     * out: length of cxt_out */
};

LIBBPF_API int bpf_prog_get_next_id(__u32 start_id, __u32 *next_id);
LIBBPF_API int bpf_map_get_next_id(__u32 start_id, __u32 *next_id);
LIBBPF_API int bpf_btf_get_next_id(__u32 start_id, __u32 *next_id);
LIBBPF_API int bpf_link_get_next_id(__u32 start_id, __u32 *next_id);

struct bpf_get_fd_by_id_opts {
	size_t sz; /* size of this struct for forward/backward compatibility */
	__u32 open_flags; /* permissions requested for the operation on fd */
	size_t :0;
};
#define bpf_get_fd_by_id_opts__last_field open_flags

LIBBPF_API int bpf_prog_get_fd_by_id(__u32 id);
LIBBPF_API int bpf_prog_get_fd_by_id_opts(__u32 id,
				const struct bpf_get_fd_by_id_opts *opts);
LIBBPF_API int bpf_map_get_fd_by_id(__u32 id);
LIBBPF_API int bpf_map_get_fd_by_id_opts(__u32 id,
				const struct bpf_get_fd_by_id_opts *opts);
LIBBPF_API int bpf_btf_get_fd_by_id(__u32 id);
LIBBPF_API int bpf_btf_get_fd_by_id_opts(__u32 id,
				const struct bpf_get_fd_by_id_opts *opts);
LIBBPF_API int bpf_link_get_fd_by_id(__u32 id);
LIBBPF_API int bpf_link_get_fd_by_id_opts(__u32 id,
				const struct bpf_get_fd_by_id_opts *opts);
LIBBPF_API int bpf_obj_get_info_by_fd(int bpf_fd, void *info, __u32 *info_len);

/**
 * @brief **bpf_prog_get_info_by_fd()** obtains information about the BPF
 * program corresponding to *prog_fd*.
 *
 * Populates up to *info_len* bytes of *info* and updates *info_len* with the
 * actual number of bytes written to *info*. Note that *info* should be
 * zero-initialized or initialized as expected by the requested *info*
 * type. Failing to (zero-)initialize *info* under certain circumstances can
 * result in this helper returning an error.
 *
 * @param prog_fd BPF program file descriptor
 * @param info pointer to **struct bpf_prog_info** that will be populated with
 * BPF program information
 * @param info_len pointer to the size of *info*; on success updated with the
 * number of bytes written to *info*
 * @return 0, on success; negative error code, otherwise (errno is also set to
 * the error code)
 */
LIBBPF_API int bpf_prog_get_info_by_fd(int prog_fd, struct bpf_prog_info *info, __u32 *info_len);

/**
 * @brief **bpf_map_get_info_by_fd()** obtains information about the BPF
 * map corresponding to *map_fd*.
 *
 * Populates up to *info_len* bytes of *info* and updates *info_len* with the
 * actual number of bytes written to *info*. Note that *info* should be
 * zero-initialized or initialized as expected by the requested *info*
 * type. Failing to (zero-)initialize *info* under certain circumstances can
 * result in this helper returning an error.
 *
 * @param map_fd BPF map file descriptor
 * @param info pointer to **struct bpf_map_info** that will be populated with
 * BPF map information
 * @param info_len pointer to the size of *info*; on success updated with the
 * number of bytes written to *info*
 * @return 0, on success; negative error code, otherwise (errno is also set to
 * the error code)
 */
LIBBPF_API int bpf_map_get_info_by_fd(int map_fd, struct bpf_map_info *info, __u32 *info_len);

/**
 * @brief **bpf_btf_get_info_by_fd()** obtains information about the
 * BTF object corresponding to *btf_fd*.
 *
 * Populates up to *info_len* bytes of *info* and updates *info_len* with the
 * actual number of bytes written to *info*. Note that *info* should be
 * zero-initialized or initialized as expected by the requested *info*
 * type. Failing to (zero-)initialize *info* under certain circumstances can
 * result in this helper returning an error.
 *
 * @param btf_fd BTF object file descriptor
 * @param info pointer to **struct bpf_btf_info** that will be populated with
 * BTF object information
 * @param info_len pointer to the size of *info*; on success updated with the
 * number of bytes written to *info*
 * @return 0, on success; negative error code, otherwise (errno is also set to
 * the error code)
 */
LIBBPF_API int bpf_btf_get_info_by_fd(int btf_fd, struct bpf_btf_info *info, __u32 *info_len);

/**
 * @brief **bpf_btf_get_info_by_fd()** obtains information about the BPF
 * link corresponding to *link_fd*.
 *
 * Populates up to *info_len* bytes of *info* and updates *info_len* with the
 * actual number of bytes written to *info*. Note that *info* should be
 * zero-initialized or initialized as expected by the requested *info*
 * type. Failing to (zero-)initialize *info* under certain circumstances can
 * result in this helper returning an error.
 *
 * @param link_fd BPF link file descriptor
 * @param info pointer to **struct bpf_link_info** that will be populated with
 * BPF link information
 * @param info_len pointer to the size of *info*; on success updated with the
 * number of bytes written to *info*
 * @return 0, on success; negative error code, otherwise (errno is also set to
 * the error code)
 */
LIBBPF_API int bpf_link_get_info_by_fd(int link_fd, struct bpf_link_info *info, __u32 *info_len);

struct bpf_prog_query_opts {
	size_t sz; /* size of this struct for forward/backward compatibility */
	__u32 query_flags;
	__u32 attach_flags; /* output argument */
	__u32 *prog_ids;
	union {
		/* input+output argument */
		__u32 prog_cnt;
		__u32 count;
	};
	__u32 *prog_attach_flags;
	__u32 *link_ids;
	__u32 *link_attach_flags;
	__u64 revision;
	size_t :0;
};
#define bpf_prog_query_opts__last_field revision

/**
 * @brief **bpf_prog_query_opts()** queries the BPF programs and BPF links
 * which are attached to *target* which can represent a file descriptor or
 * netdevice ifindex.
 *
 * @param target query location file descriptor or ifindex
 * @param type attach type for the BPF program
 * @param opts options for configuring the query
 * @return 0, on success; negative error code, otherwise (errno is also set to
 * the error code)
 */
LIBBPF_API int bpf_prog_query_opts(int target, enum bpf_attach_type type,
				   struct bpf_prog_query_opts *opts);
LIBBPF_API int bpf_prog_query(int target_fd, enum bpf_attach_type type,
			      __u32 query_flags, __u32 *attach_flags,
			      __u32 *prog_ids, __u32 *prog_cnt);

struct bpf_raw_tp_opts {
	size_t sz; /* size of this struct for forward/backward compatibility */
	const char *tp_name;
	__u64 cookie;
	size_t :0;
};
#define bpf_raw_tp_opts__last_field cookie

LIBBPF_API int bpf_raw_tracepoint_open_opts(int prog_fd, struct bpf_raw_tp_opts *opts);
LIBBPF_API int bpf_raw_tracepoint_open(const char *name, int prog_fd);
LIBBPF_API int bpf_task_fd_query(int pid, int fd, __u32 flags, char *buf,
				 __u32 *buf_len, __u32 *prog_id, __u32 *fd_type,
				 __u64 *probe_offset, __u64 *probe_addr);

#ifdef __cplusplus
/* forward-declaring enums in C++ isn't compatible with pure C enums, so
 * instead define bpf_enable_stats() as accepting int as an input
 */
LIBBPF_API int bpf_enable_stats(int type);
#else
enum bpf_stats_type; /* defined in up-to-date linux/bpf.h */
LIBBPF_API int bpf_enable_stats(enum bpf_stats_type type);
#endif

struct bpf_prog_bind_opts {
	size_t sz; /* size of this struct for forward/backward compatibility */
	__u32 flags;
};
#define bpf_prog_bind_opts__last_field flags

LIBBPF_API int bpf_prog_bind_map(int prog_fd, int map_fd,
				 const struct bpf_prog_bind_opts *opts);

struct bpf_test_run_opts {
	size_t sz; /* size of this struct for forward/backward compatibility */
	const void *data_in; /* optional */
	void *data_out;      /* optional */
	__u32 data_size_in;
	__u32 data_size_out; /* in: max length of data_out
			      * out: length of data_out
			      */
	const void *ctx_in; /* optional */
	void *ctx_out;      /* optional */
	__u32 ctx_size_in;
	__u32 ctx_size_out; /* in: max length of ctx_out
			     * out: length of cxt_out
			     */
	__u32 retval;        /* out: return code of the BPF program */
	int repeat;
	__u32 duration;      /* out: average per repetition in ns */
	__u32 flags;
	__u32 cpu;
	__u32 batch_size;
};
#define bpf_test_run_opts__last_field batch_size

LIBBPF_API int bpf_prog_test_run_opts(int prog_fd,
				      struct bpf_test_run_opts *opts);

struct bpf_token_create_opts {
	size_t sz; /* size of this struct for forward/backward compatibility */
	__u32 flags;
	size_t :0;
};
#define bpf_token_create_opts__last_field flags

/**
 * @brief **bpf_token_create()** creates a new instance of BPF token derived
 * from specified BPF FS mount point.
 *
 * BPF token created with this API can be passed to bpf() syscall for
 * commands like BPF_PROG_LOAD, BPF_MAP_CREATE, etc.
 *
 * @param bpffs_fd FD for BPF FS instance from which to derive a BPF token
 * instance.
 * @param opts optional BPF token creation options, can be NULL
 *
 * @return BPF token FD > 0, on success; negative error code, otherwise (errno
 * is also set to the error code)
 */
LIBBPF_API int bpf_token_create(int bpffs_fd,
				struct bpf_token_create_opts *opts);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* __LIBBPF_BPF_H */
