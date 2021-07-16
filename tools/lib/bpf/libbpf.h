/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */

/*
 * Common eBPF ELF object loading operations.
 *
 * Copyright (C) 2013-2015 Alexei Starovoitov <ast@kernel.org>
 * Copyright (C) 2015 Wang Nan <wangnan0@huawei.com>
 * Copyright (C) 2015 Huawei Inc.
 */
#ifndef __LIBBPF_LIBBPF_H
#define __LIBBPF_LIBBPF_H

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>  // for size_t
#include <linux/bpf.h>

#include "libbpf_common.h"
#include "libbpf_legacy.h"

#ifdef __cplusplus
extern "C" {
#endif

enum libbpf_errno {
	__LIBBPF_ERRNO__START = 4000,

	/* Something wrong in libelf */
	LIBBPF_ERRNO__LIBELF = __LIBBPF_ERRNO__START,
	LIBBPF_ERRNO__FORMAT,	/* BPF object format invalid */
	LIBBPF_ERRNO__KVERSION,	/* Incorrect or no 'version' section */
	LIBBPF_ERRNO__ENDIAN,	/* Endian mismatch */
	LIBBPF_ERRNO__INTERNAL,	/* Internal error in libbpf */
	LIBBPF_ERRNO__RELOC,	/* Relocation failed */
	LIBBPF_ERRNO__LOAD,	/* Load program failure for unknown reason */
	LIBBPF_ERRNO__VERIFY,	/* Kernel verifier blocks program loading */
	LIBBPF_ERRNO__PROG2BIG,	/* Program too big */
	LIBBPF_ERRNO__KVER,	/* Incorrect kernel version */
	LIBBPF_ERRNO__PROGTYPE,	/* Kernel doesn't support this program type */
	LIBBPF_ERRNO__WRNGPID,	/* Wrong pid in netlink message */
	LIBBPF_ERRNO__INVSEQ,	/* Invalid netlink sequence */
	LIBBPF_ERRNO__NLPARSE,	/* netlink parsing error */
	__LIBBPF_ERRNO__END,
};

LIBBPF_API int libbpf_strerror(int err, char *buf, size_t size);

enum libbpf_print_level {
        LIBBPF_WARN,
        LIBBPF_INFO,
        LIBBPF_DEBUG,
};

typedef int (*libbpf_print_fn_t)(enum libbpf_print_level level,
				 const char *, va_list ap);

LIBBPF_API libbpf_print_fn_t libbpf_set_print(libbpf_print_fn_t fn);

/* Hide internal to user */
struct bpf_object;

struct bpf_object_open_attr {
	const char *file;
	enum bpf_prog_type prog_type;
};

struct bpf_object_open_opts {
	/* size of this struct, for forward/backward compatiblity */
	size_t sz;
	/* object name override, if provided:
	 * - for object open from file, this will override setting object
	 *   name from file path's base name;
	 * - for object open from memory buffer, this will specify an object
	 *   name and will override default "<addr>-<buf-size>" name;
	 */
	const char *object_name;
	/* parse map definitions non-strictly, allowing extra attributes/data */
	bool relaxed_maps;
	/* DEPRECATED: handle CO-RE relocations non-strictly, allowing failures.
	 * Value is ignored. Relocations always are processed non-strictly.
	 * Non-relocatable instructions are replaced with invalid ones to
	 * prevent accidental errors.
	 * */
	bool relaxed_core_relocs;
	/* maps that set the 'pinning' attribute in their definition will have
	 * their pin_path attribute set to a file in this directory, and be
	 * auto-pinned to that path on load; defaults to "/sys/fs/bpf".
	 */
	const char *pin_root_path;
	__u32 attach_prog_fd;
	/* Additional kernel config content that augments and overrides
	 * system Kconfig for CONFIG_xxx externs.
	 */
	const char *kconfig;
	/* Path to the custom BTF to be used for BPF CO-RE relocations.
	 * This custom BTF completely replaces the use of vmlinux BTF
	 * for the purpose of CO-RE relocations.
	 * NOTE: any other BPF feature (e.g., fentry/fexit programs,
	 * struct_ops, etc) will need actual kernel BTF at /sys/kernel/btf/vmlinux.
	 */
	const char *btf_custom_path;
};
#define bpf_object_open_opts__last_field btf_custom_path

LIBBPF_API struct bpf_object *bpf_object__open(const char *path);
LIBBPF_API struct bpf_object *
bpf_object__open_file(const char *path, const struct bpf_object_open_opts *opts);
LIBBPF_API struct bpf_object *
bpf_object__open_mem(const void *obj_buf, size_t obj_buf_sz,
		     const struct bpf_object_open_opts *opts);

/* deprecated bpf_object__open variants */
LIBBPF_API struct bpf_object *
bpf_object__open_buffer(const void *obj_buf, size_t obj_buf_sz,
			const char *name);
LIBBPF_API struct bpf_object *
bpf_object__open_xattr(struct bpf_object_open_attr *attr);

enum libbpf_pin_type {
	LIBBPF_PIN_NONE,
	/* PIN_BY_NAME: pin maps by name (in /sys/fs/bpf by default) */
	LIBBPF_PIN_BY_NAME,
};

/* pin_maps and unpin_maps can both be called with a NULL path, in which case
 * they will use the pin_path attribute of each map (and ignore all maps that
 * don't have a pin_path set).
 */
LIBBPF_API int bpf_object__pin_maps(struct bpf_object *obj, const char *path);
LIBBPF_API int bpf_object__unpin_maps(struct bpf_object *obj,
				      const char *path);
LIBBPF_API int bpf_object__pin_programs(struct bpf_object *obj,
					const char *path);
LIBBPF_API int bpf_object__unpin_programs(struct bpf_object *obj,
					  const char *path);
LIBBPF_API int bpf_object__pin(struct bpf_object *object, const char *path);
LIBBPF_API void bpf_object__close(struct bpf_object *object);

struct bpf_object_load_attr {
	struct bpf_object *obj;
	int log_level;
	const char *target_btf_path;
};

/* Load/unload object into/from kernel */
LIBBPF_API int bpf_object__load(struct bpf_object *obj);
LIBBPF_API int bpf_object__load_xattr(struct bpf_object_load_attr *attr);
LIBBPF_API int bpf_object__unload(struct bpf_object *obj);

LIBBPF_API const char *bpf_object__name(const struct bpf_object *obj);
LIBBPF_API unsigned int bpf_object__kversion(const struct bpf_object *obj);
LIBBPF_API int bpf_object__set_kversion(struct bpf_object *obj, __u32 kern_version);

struct btf;
LIBBPF_API struct btf *bpf_object__btf(const struct bpf_object *obj);
LIBBPF_API int bpf_object__btf_fd(const struct bpf_object *obj);

LIBBPF_API struct bpf_program *
bpf_object__find_program_by_title(const struct bpf_object *obj,
				  const char *title);
LIBBPF_API struct bpf_program *
bpf_object__find_program_by_name(const struct bpf_object *obj,
				 const char *name);

LIBBPF_API struct bpf_object *bpf_object__next(struct bpf_object *prev);
#define bpf_object__for_each_safe(pos, tmp)			\
	for ((pos) = bpf_object__next(NULL),		\
		(tmp) = bpf_object__next(pos);		\
	     (pos) != NULL;				\
	     (pos) = (tmp), (tmp) = bpf_object__next(tmp))

typedef void (*bpf_object_clear_priv_t)(struct bpf_object *, void *);
LIBBPF_API int bpf_object__set_priv(struct bpf_object *obj, void *priv,
				    bpf_object_clear_priv_t clear_priv);
LIBBPF_API void *bpf_object__priv(const struct bpf_object *prog);

LIBBPF_API int
libbpf_prog_type_by_name(const char *name, enum bpf_prog_type *prog_type,
			 enum bpf_attach_type *expected_attach_type);
LIBBPF_API int libbpf_attach_type_by_name(const char *name,
					  enum bpf_attach_type *attach_type);
LIBBPF_API int libbpf_find_vmlinux_btf_id(const char *name,
					  enum bpf_attach_type attach_type);

/* Accessors of bpf_program */
struct bpf_program;
LIBBPF_API struct bpf_program *bpf_program__next(struct bpf_program *prog,
						 const struct bpf_object *obj);

#define bpf_object__for_each_program(pos, obj)		\
	for ((pos) = bpf_program__next(NULL, (obj));	\
	     (pos) != NULL;				\
	     (pos) = bpf_program__next((pos), (obj)))

LIBBPF_API struct bpf_program *bpf_program__prev(struct bpf_program *prog,
						 const struct bpf_object *obj);

typedef void (*bpf_program_clear_priv_t)(struct bpf_program *, void *);

LIBBPF_API int bpf_program__set_priv(struct bpf_program *prog, void *priv,
				     bpf_program_clear_priv_t clear_priv);

LIBBPF_API void *bpf_program__priv(const struct bpf_program *prog);
LIBBPF_API void bpf_program__set_ifindex(struct bpf_program *prog,
					 __u32 ifindex);

LIBBPF_API const char *bpf_program__name(const struct bpf_program *prog);
LIBBPF_API const char *bpf_program__section_name(const struct bpf_program *prog);
LIBBPF_API LIBBPF_DEPRECATED("BPF program title is confusing term; please use bpf_program__section_name() instead")
const char *bpf_program__title(const struct bpf_program *prog, bool needs_copy);
LIBBPF_API bool bpf_program__autoload(const struct bpf_program *prog);
LIBBPF_API int bpf_program__set_autoload(struct bpf_program *prog, bool autoload);

/* returns program size in bytes */
LIBBPF_API size_t bpf_program__size(const struct bpf_program *prog);

LIBBPF_API int bpf_program__load(struct bpf_program *prog, char *license,
				 __u32 kern_version);
LIBBPF_API int bpf_program__fd(const struct bpf_program *prog);
LIBBPF_API int bpf_program__pin_instance(struct bpf_program *prog,
					 const char *path,
					 int instance);
LIBBPF_API int bpf_program__unpin_instance(struct bpf_program *prog,
					   const char *path,
					   int instance);
LIBBPF_API int bpf_program__pin(struct bpf_program *prog, const char *path);
LIBBPF_API int bpf_program__unpin(struct bpf_program *prog, const char *path);
LIBBPF_API void bpf_program__unload(struct bpf_program *prog);

struct bpf_link;

LIBBPF_API struct bpf_link *bpf_link__open(const char *path);
LIBBPF_API int bpf_link__fd(const struct bpf_link *link);
LIBBPF_API const char *bpf_link__pin_path(const struct bpf_link *link);
LIBBPF_API int bpf_link__pin(struct bpf_link *link, const char *path);
LIBBPF_API int bpf_link__unpin(struct bpf_link *link);
LIBBPF_API int bpf_link__update_program(struct bpf_link *link,
					struct bpf_program *prog);
LIBBPF_API void bpf_link__disconnect(struct bpf_link *link);
LIBBPF_API int bpf_link__detach(struct bpf_link *link);
LIBBPF_API int bpf_link__destroy(struct bpf_link *link);

LIBBPF_API struct bpf_link *
bpf_program__attach(struct bpf_program *prog);
LIBBPF_API struct bpf_link *
bpf_program__attach_perf_event(struct bpf_program *prog, int pfd);
LIBBPF_API struct bpf_link *
bpf_program__attach_kprobe(struct bpf_program *prog, bool retprobe,
			   const char *func_name);
LIBBPF_API struct bpf_link *
bpf_program__attach_uprobe(struct bpf_program *prog, bool retprobe,
			   pid_t pid, const char *binary_path,
			   size_t func_offset);
LIBBPF_API struct bpf_link *
bpf_program__attach_tracepoint(struct bpf_program *prog,
			       const char *tp_category,
			       const char *tp_name);
LIBBPF_API struct bpf_link *
bpf_program__attach_raw_tracepoint(struct bpf_program *prog,
				   const char *tp_name);
LIBBPF_API struct bpf_link *
bpf_program__attach_trace(struct bpf_program *prog);
LIBBPF_API struct bpf_link *
bpf_program__attach_lsm(struct bpf_program *prog);
LIBBPF_API struct bpf_link *
bpf_program__attach_cgroup(struct bpf_program *prog, int cgroup_fd);
LIBBPF_API struct bpf_link *
bpf_program__attach_netns(struct bpf_program *prog, int netns_fd);
LIBBPF_API struct bpf_link *
bpf_program__attach_xdp(struct bpf_program *prog, int ifindex);
LIBBPF_API struct bpf_link *
bpf_program__attach_freplace(struct bpf_program *prog,
			     int target_fd, const char *attach_func_name);

struct bpf_map;

LIBBPF_API struct bpf_link *bpf_map__attach_struct_ops(struct bpf_map *map);

struct bpf_iter_attach_opts {
	size_t sz; /* size of this struct for forward/backward compatibility */
	union bpf_iter_link_info *link_info;
	__u32 link_info_len;
};
#define bpf_iter_attach_opts__last_field link_info_len

LIBBPF_API struct bpf_link *
bpf_program__attach_iter(struct bpf_program *prog,
			 const struct bpf_iter_attach_opts *opts);

struct bpf_insn;

/*
 * Libbpf allows callers to adjust BPF programs before being loaded
 * into kernel. One program in an object file can be transformed into
 * multiple variants to be attached to different hooks.
 *
 * bpf_program_prep_t, bpf_program__set_prep and bpf_program__nth_fd
 * form an API for this purpose.
 *
 * - bpf_program_prep_t:
 *   Defines a 'preprocessor', which is a caller defined function
 *   passed to libbpf through bpf_program__set_prep(), and will be
 *   called before program is loaded. The processor should adjust
 *   the program one time for each instance according to the instance id
 *   passed to it.
 *
 * - bpf_program__set_prep:
 *   Attaches a preprocessor to a BPF program. The number of instances
 *   that should be created is also passed through this function.
 *
 * - bpf_program__nth_fd:
 *   After the program is loaded, get resulting FD of a given instance
 *   of the BPF program.
 *
 * If bpf_program__set_prep() is not used, the program would be loaded
 * without adjustment during bpf_object__load(). The program has only
 * one instance. In this case bpf_program__fd(prog) is equal to
 * bpf_program__nth_fd(prog, 0).
 */

struct bpf_prog_prep_result {
	/*
	 * If not NULL, load new instruction array.
	 * If set to NULL, don't load this instance.
	 */
	struct bpf_insn *new_insn_ptr;
	int new_insn_cnt;

	/* If not NULL, result FD is written to it. */
	int *pfd;
};

/*
 * Parameters of bpf_program_prep_t:
 *  - prog:	The bpf_program being loaded.
 *  - n:	Index of instance being generated.
 *  - insns:	BPF instructions array.
 *  - insns_cnt:Number of instructions in insns.
 *  - res:	Output parameter, result of transformation.
 *
 * Return value:
 *  - Zero:	pre-processing success.
 *  - Non-zero:	pre-processing error, stop loading.
 */
typedef int (*bpf_program_prep_t)(struct bpf_program *prog, int n,
				  struct bpf_insn *insns, int insns_cnt,
				  struct bpf_prog_prep_result *res);

LIBBPF_API int bpf_program__set_prep(struct bpf_program *prog, int nr_instance,
				     bpf_program_prep_t prep);

LIBBPF_API int bpf_program__nth_fd(const struct bpf_program *prog, int n);

/*
 * Adjust type of BPF program. Default is kprobe.
 */
LIBBPF_API int bpf_program__set_socket_filter(struct bpf_program *prog);
LIBBPF_API int bpf_program__set_tracepoint(struct bpf_program *prog);
LIBBPF_API int bpf_program__set_raw_tracepoint(struct bpf_program *prog);
LIBBPF_API int bpf_program__set_kprobe(struct bpf_program *prog);
LIBBPF_API int bpf_program__set_lsm(struct bpf_program *prog);
LIBBPF_API int bpf_program__set_sched_cls(struct bpf_program *prog);
LIBBPF_API int bpf_program__set_sched_act(struct bpf_program *prog);
LIBBPF_API int bpf_program__set_xdp(struct bpf_program *prog);
LIBBPF_API int bpf_program__set_perf_event(struct bpf_program *prog);
LIBBPF_API int bpf_program__set_tracing(struct bpf_program *prog);
LIBBPF_API int bpf_program__set_struct_ops(struct bpf_program *prog);
LIBBPF_API int bpf_program__set_extension(struct bpf_program *prog);
LIBBPF_API int bpf_program__set_sk_lookup(struct bpf_program *prog);

LIBBPF_API enum bpf_prog_type bpf_program__get_type(const struct bpf_program *prog);
LIBBPF_API void bpf_program__set_type(struct bpf_program *prog,
				      enum bpf_prog_type type);

LIBBPF_API enum bpf_attach_type
bpf_program__get_expected_attach_type(const struct bpf_program *prog);
LIBBPF_API void
bpf_program__set_expected_attach_type(struct bpf_program *prog,
				      enum bpf_attach_type type);

LIBBPF_API int
bpf_program__set_attach_target(struct bpf_program *prog, int attach_prog_fd,
			       const char *attach_func_name);

LIBBPF_API bool bpf_program__is_socket_filter(const struct bpf_program *prog);
LIBBPF_API bool bpf_program__is_tracepoint(const struct bpf_program *prog);
LIBBPF_API bool bpf_program__is_raw_tracepoint(const struct bpf_program *prog);
LIBBPF_API bool bpf_program__is_kprobe(const struct bpf_program *prog);
LIBBPF_API bool bpf_program__is_lsm(const struct bpf_program *prog);
LIBBPF_API bool bpf_program__is_sched_cls(const struct bpf_program *prog);
LIBBPF_API bool bpf_program__is_sched_act(const struct bpf_program *prog);
LIBBPF_API bool bpf_program__is_xdp(const struct bpf_program *prog);
LIBBPF_API bool bpf_program__is_perf_event(const struct bpf_program *prog);
LIBBPF_API bool bpf_program__is_tracing(const struct bpf_program *prog);
LIBBPF_API bool bpf_program__is_struct_ops(const struct bpf_program *prog);
LIBBPF_API bool bpf_program__is_extension(const struct bpf_program *prog);
LIBBPF_API bool bpf_program__is_sk_lookup(const struct bpf_program *prog);

/*
 * No need for __attribute__((packed)), all members of 'bpf_map_def'
 * are all aligned.  In addition, using __attribute__((packed))
 * would trigger a -Wpacked warning message, and lead to an error
 * if -Werror is set.
 */
struct bpf_map_def {
	unsigned int type;
	unsigned int key_size;
	unsigned int value_size;
	unsigned int max_entries;
	unsigned int map_flags;
};

/*
 * The 'struct bpf_map' in include/linux/bpf.h is internal to the kernel,
 * so no need to worry about a name clash.
 */
LIBBPF_API struct bpf_map *
bpf_object__find_map_by_name(const struct bpf_object *obj, const char *name);

LIBBPF_API int
bpf_object__find_map_fd_by_name(const struct bpf_object *obj, const char *name);

/*
 * Get bpf_map through the offset of corresponding struct bpf_map_def
 * in the BPF object file.
 */
LIBBPF_API struct bpf_map *
bpf_object__find_map_by_offset(struct bpf_object *obj, size_t offset);

LIBBPF_API struct bpf_map *
bpf_map__next(const struct bpf_map *map, const struct bpf_object *obj);
#define bpf_object__for_each_map(pos, obj)		\
	for ((pos) = bpf_map__next(NULL, (obj));	\
	     (pos) != NULL;				\
	     (pos) = bpf_map__next((pos), (obj)))
#define bpf_map__for_each bpf_object__for_each_map

LIBBPF_API struct bpf_map *
bpf_map__prev(const struct bpf_map *map, const struct bpf_object *obj);

/* get/set map FD */
LIBBPF_API int bpf_map__fd(const struct bpf_map *map);
LIBBPF_API int bpf_map__reuse_fd(struct bpf_map *map, int fd);
/* get map definition */
LIBBPF_API const struct bpf_map_def *bpf_map__def(const struct bpf_map *map);
/* get map name */
LIBBPF_API const char *bpf_map__name(const struct bpf_map *map);
/* get/set map type */
LIBBPF_API enum bpf_map_type bpf_map__type(const struct bpf_map *map);
LIBBPF_API int bpf_map__set_type(struct bpf_map *map, enum bpf_map_type type);
/* get/set map size (max_entries) */
LIBBPF_API __u32 bpf_map__max_entries(const struct bpf_map *map);
LIBBPF_API int bpf_map__set_max_entries(struct bpf_map *map, __u32 max_entries);
LIBBPF_API int bpf_map__resize(struct bpf_map *map, __u32 max_entries);
/* get/set map flags */
LIBBPF_API __u32 bpf_map__map_flags(const struct bpf_map *map);
LIBBPF_API int bpf_map__set_map_flags(struct bpf_map *map, __u32 flags);
/* get/set map NUMA node */
LIBBPF_API __u32 bpf_map__numa_node(const struct bpf_map *map);
LIBBPF_API int bpf_map__set_numa_node(struct bpf_map *map, __u32 numa_node);
/* get/set map key size */
LIBBPF_API __u32 bpf_map__key_size(const struct bpf_map *map);
LIBBPF_API int bpf_map__set_key_size(struct bpf_map *map, __u32 size);
/* get/set map value size */
LIBBPF_API __u32 bpf_map__value_size(const struct bpf_map *map);
LIBBPF_API int bpf_map__set_value_size(struct bpf_map *map, __u32 size);
/* get map key/value BTF type IDs */
LIBBPF_API __u32 bpf_map__btf_key_type_id(const struct bpf_map *map);
LIBBPF_API __u32 bpf_map__btf_value_type_id(const struct bpf_map *map);
/* get/set map if_index */
LIBBPF_API __u32 bpf_map__ifindex(const struct bpf_map *map);
LIBBPF_API int bpf_map__set_ifindex(struct bpf_map *map, __u32 ifindex);

typedef void (*bpf_map_clear_priv_t)(struct bpf_map *, void *);
LIBBPF_API int bpf_map__set_priv(struct bpf_map *map, void *priv,
				 bpf_map_clear_priv_t clear_priv);
LIBBPF_API void *bpf_map__priv(const struct bpf_map *map);
LIBBPF_API int bpf_map__set_initial_value(struct bpf_map *map,
					  const void *data, size_t size);
LIBBPF_API const void *bpf_map__initial_value(struct bpf_map *map, size_t *psize);
LIBBPF_API bool bpf_map__is_offload_neutral(const struct bpf_map *map);
LIBBPF_API bool bpf_map__is_internal(const struct bpf_map *map);
LIBBPF_API int bpf_map__set_pin_path(struct bpf_map *map, const char *path);
LIBBPF_API const char *bpf_map__get_pin_path(const struct bpf_map *map);
LIBBPF_API bool bpf_map__is_pinned(const struct bpf_map *map);
LIBBPF_API int bpf_map__pin(struct bpf_map *map, const char *path);
LIBBPF_API int bpf_map__unpin(struct bpf_map *map, const char *path);

LIBBPF_API int bpf_map__set_inner_map_fd(struct bpf_map *map, int fd);
LIBBPF_API struct bpf_map *bpf_map__inner_map(struct bpf_map *map);

LIBBPF_API long libbpf_get_error(const void *ptr);

struct bpf_prog_load_attr {
	const char *file;
	enum bpf_prog_type prog_type;
	enum bpf_attach_type expected_attach_type;
	int ifindex;
	int log_level;
	int prog_flags;
};

LIBBPF_API int bpf_prog_load_xattr(const struct bpf_prog_load_attr *attr,
				   struct bpf_object **pobj, int *prog_fd);
LIBBPF_API int bpf_prog_load(const char *file, enum bpf_prog_type type,
			     struct bpf_object **pobj, int *prog_fd);

/* XDP related API */
struct xdp_link_info {
	__u32 prog_id;
	__u32 drv_prog_id;
	__u32 hw_prog_id;
	__u32 skb_prog_id;
	__u8 attach_mode;
};

struct bpf_xdp_set_link_opts {
	size_t sz;
	int old_fd;
	size_t :0;
};
#define bpf_xdp_set_link_opts__last_field old_fd

LIBBPF_API int bpf_set_link_xdp_fd(int ifindex, int fd, __u32 flags);
LIBBPF_API int bpf_set_link_xdp_fd_opts(int ifindex, int fd, __u32 flags,
					const struct bpf_xdp_set_link_opts *opts);
LIBBPF_API int bpf_get_link_xdp_id(int ifindex, __u32 *prog_id, __u32 flags);
LIBBPF_API int bpf_get_link_xdp_info(int ifindex, struct xdp_link_info *info,
				     size_t info_size, __u32 flags);

/* TC related API */
enum bpf_tc_attach_point {
	BPF_TC_INGRESS = 1 << 0,
	BPF_TC_EGRESS  = 1 << 1,
	BPF_TC_CUSTOM  = 1 << 2,
};

#define BPF_TC_PARENT(a, b) 	\
	((((a) << 16) & 0xFFFF0000U) | ((b) & 0x0000FFFFU))

enum bpf_tc_flags {
	BPF_TC_F_REPLACE = 1 << 0,
};

struct bpf_tc_hook {
	size_t sz;
	int ifindex;
	enum bpf_tc_attach_point attach_point;
	__u32 parent;
	size_t :0;
};
#define bpf_tc_hook__last_field parent

struct bpf_tc_opts {
	size_t sz;
	int prog_fd;
	__u32 flags;
	__u32 prog_id;
	__u32 handle;
	__u32 priority;
	size_t :0;
};
#define bpf_tc_opts__last_field priority

LIBBPF_API int bpf_tc_hook_create(struct bpf_tc_hook *hook);
LIBBPF_API int bpf_tc_hook_destroy(struct bpf_tc_hook *hook);
LIBBPF_API int bpf_tc_attach(const struct bpf_tc_hook *hook,
			     struct bpf_tc_opts *opts);
LIBBPF_API int bpf_tc_detach(const struct bpf_tc_hook *hook,
			     const struct bpf_tc_opts *opts);
LIBBPF_API int bpf_tc_query(const struct bpf_tc_hook *hook,
			    struct bpf_tc_opts *opts);

/* Ring buffer APIs */
struct ring_buffer;

typedef int (*ring_buffer_sample_fn)(void *ctx, void *data, size_t size);

struct ring_buffer_opts {
	size_t sz; /* size of this struct, for forward/backward compatiblity */
};

#define ring_buffer_opts__last_field sz

LIBBPF_API struct ring_buffer *
ring_buffer__new(int map_fd, ring_buffer_sample_fn sample_cb, void *ctx,
		 const struct ring_buffer_opts *opts);
LIBBPF_API void ring_buffer__free(struct ring_buffer *rb);
LIBBPF_API int ring_buffer__add(struct ring_buffer *rb, int map_fd,
				ring_buffer_sample_fn sample_cb, void *ctx);
LIBBPF_API int ring_buffer__poll(struct ring_buffer *rb, int timeout_ms);
LIBBPF_API int ring_buffer__consume(struct ring_buffer *rb);
LIBBPF_API int ring_buffer__epoll_fd(const struct ring_buffer *rb);

/* Perf buffer APIs */
struct perf_buffer;

typedef void (*perf_buffer_sample_fn)(void *ctx, int cpu,
				      void *data, __u32 size);
typedef void (*perf_buffer_lost_fn)(void *ctx, int cpu, __u64 cnt);

/* common use perf buffer options */
struct perf_buffer_opts {
	/* if specified, sample_cb is called for each sample */
	perf_buffer_sample_fn sample_cb;
	/* if specified, lost_cb is called for each batch of lost samples */
	perf_buffer_lost_fn lost_cb;
	/* ctx is provided to sample_cb and lost_cb */
	void *ctx;
};

LIBBPF_API struct perf_buffer *
perf_buffer__new(int map_fd, size_t page_cnt,
		 const struct perf_buffer_opts *opts);

enum bpf_perf_event_ret {
	LIBBPF_PERF_EVENT_DONE	= 0,
	LIBBPF_PERF_EVENT_ERROR	= -1,
	LIBBPF_PERF_EVENT_CONT	= -2,
};

struct perf_event_header;

typedef enum bpf_perf_event_ret
(*perf_buffer_event_fn)(void *ctx, int cpu, struct perf_event_header *event);

/* raw perf buffer options, giving most power and control */
struct perf_buffer_raw_opts {
	/* perf event attrs passed directly into perf_event_open() */
	struct perf_event_attr *attr;
	/* raw event callback */
	perf_buffer_event_fn event_cb;
	/* ctx is provided to event_cb */
	void *ctx;
	/* if cpu_cnt == 0, open all on all possible CPUs (up to the number of
	 * max_entries of given PERF_EVENT_ARRAY map)
	 */
	int cpu_cnt;
	/* if cpu_cnt > 0, cpus is an array of CPUs to open ring buffers on */
	int *cpus;
	/* if cpu_cnt > 0, map_keys specify map keys to set per-CPU FDs for */
	int *map_keys;
};

LIBBPF_API struct perf_buffer *
perf_buffer__new_raw(int map_fd, size_t page_cnt,
		     const struct perf_buffer_raw_opts *opts);

LIBBPF_API void perf_buffer__free(struct perf_buffer *pb);
LIBBPF_API int perf_buffer__epoll_fd(const struct perf_buffer *pb);
LIBBPF_API int perf_buffer__poll(struct perf_buffer *pb, int timeout_ms);
LIBBPF_API int perf_buffer__consume(struct perf_buffer *pb);
LIBBPF_API int perf_buffer__consume_buffer(struct perf_buffer *pb, size_t buf_idx);
LIBBPF_API size_t perf_buffer__buffer_cnt(const struct perf_buffer *pb);
LIBBPF_API int perf_buffer__buffer_fd(const struct perf_buffer *pb, size_t buf_idx);

typedef enum bpf_perf_event_ret
	(*bpf_perf_event_print_t)(struct perf_event_header *hdr,
				  void *private_data);
LIBBPF_API enum bpf_perf_event_ret
bpf_perf_event_read_simple(void *mmap_mem, size_t mmap_size, size_t page_size,
			   void **copy_mem, size_t *copy_size,
			   bpf_perf_event_print_t fn, void *private_data);

struct bpf_prog_linfo;
struct bpf_prog_info;

LIBBPF_API void bpf_prog_linfo__free(struct bpf_prog_linfo *prog_linfo);
LIBBPF_API struct bpf_prog_linfo *
bpf_prog_linfo__new(const struct bpf_prog_info *info);
LIBBPF_API const struct bpf_line_info *
bpf_prog_linfo__lfind_addr_func(const struct bpf_prog_linfo *prog_linfo,
				__u64 addr, __u32 func_idx, __u32 nr_skip);
LIBBPF_API const struct bpf_line_info *
bpf_prog_linfo__lfind(const struct bpf_prog_linfo *prog_linfo,
		      __u32 insn_off, __u32 nr_skip);

/*
 * Probe for supported system features
 *
 * Note that running many of these probes in a short amount of time can cause
 * the kernel to reach the maximal size of lockable memory allowed for the
 * user, causing subsequent probes to fail. In this case, the caller may want
 * to adjust that limit with setrlimit().
 */
LIBBPF_API bool bpf_probe_prog_type(enum bpf_prog_type prog_type,
				    __u32 ifindex);
LIBBPF_API bool bpf_probe_map_type(enum bpf_map_type map_type, __u32 ifindex);
LIBBPF_API bool bpf_probe_helper(enum bpf_func_id id,
				 enum bpf_prog_type prog_type, __u32 ifindex);
LIBBPF_API bool bpf_probe_large_insn_limit(__u32 ifindex);

/*
 * Get bpf_prog_info in continuous memory
 *
 * struct bpf_prog_info has multiple arrays. The user has option to choose
 * arrays to fetch from kernel. The following APIs provide an uniform way to
 * fetch these data. All arrays in bpf_prog_info are stored in a single
 * continuous memory region. This makes it easy to store the info in a
 * file.
 *
 * Before writing bpf_prog_info_linear to files, it is necessary to
 * translate pointers in bpf_prog_info to offsets. Helper functions
 * bpf_program__bpil_addr_to_offs() and bpf_program__bpil_offs_to_addr()
 * are introduced to switch between pointers and offsets.
 *
 * Examples:
 *   # To fetch map_ids and prog_tags:
 *   __u64 arrays = (1UL << BPF_PROG_INFO_MAP_IDS) |
 *           (1UL << BPF_PROG_INFO_PROG_TAGS);
 *   struct bpf_prog_info_linear *info_linear =
 *           bpf_program__get_prog_info_linear(fd, arrays);
 *
 *   # To save data in file
 *   bpf_program__bpil_addr_to_offs(info_linear);
 *   write(f, info_linear, sizeof(*info_linear) + info_linear->data_len);
 *
 *   # To read data from file
 *   read(f, info_linear, <proper_size>);
 *   bpf_program__bpil_offs_to_addr(info_linear);
 */
enum bpf_prog_info_array {
	BPF_PROG_INFO_FIRST_ARRAY = 0,
	BPF_PROG_INFO_JITED_INSNS = 0,
	BPF_PROG_INFO_XLATED_INSNS,
	BPF_PROG_INFO_MAP_IDS,
	BPF_PROG_INFO_JITED_KSYMS,
	BPF_PROG_INFO_JITED_FUNC_LENS,
	BPF_PROG_INFO_FUNC_INFO,
	BPF_PROG_INFO_LINE_INFO,
	BPF_PROG_INFO_JITED_LINE_INFO,
	BPF_PROG_INFO_PROG_TAGS,
	BPF_PROG_INFO_LAST_ARRAY,
};

struct bpf_prog_info_linear {
	/* size of struct bpf_prog_info, when the tool is compiled */
	__u32			info_len;
	/* total bytes allocated for data, round up to 8 bytes */
	__u32			data_len;
	/* which arrays are included in data */
	__u64			arrays;
	struct bpf_prog_info	info;
	__u8			data[];
};

LIBBPF_API struct bpf_prog_info_linear *
bpf_program__get_prog_info_linear(int fd, __u64 arrays);

LIBBPF_API void
bpf_program__bpil_addr_to_offs(struct bpf_prog_info_linear *info_linear);

LIBBPF_API void
bpf_program__bpil_offs_to_addr(struct bpf_prog_info_linear *info_linear);

/*
 * A helper function to get the number of possible CPUs before looking up
 * per-CPU maps. Negative errno is returned on failure.
 *
 * Example usage:
 *
 *     int ncpus = libbpf_num_possible_cpus();
 *     if (ncpus < 0) {
 *          // error handling
 *     }
 *     long values[ncpus];
 *     bpf_map_lookup_elem(per_cpu_map_fd, key, values);
 *
 */
LIBBPF_API int libbpf_num_possible_cpus(void);

struct bpf_map_skeleton {
	const char *name;
	struct bpf_map **map;
	void **mmaped;
};

struct bpf_prog_skeleton {
	const char *name;
	struct bpf_program **prog;
	struct bpf_link **link;
};

struct bpf_object_skeleton {
	size_t sz; /* size of this struct, for forward/backward compatibility */

	const char *name;
	void *data;
	size_t data_sz;

	struct bpf_object **obj;

	int map_cnt;
	int map_skel_sz; /* sizeof(struct bpf_skeleton_map) */
	struct bpf_map_skeleton *maps;

	int prog_cnt;
	int prog_skel_sz; /* sizeof(struct bpf_skeleton_prog) */
	struct bpf_prog_skeleton *progs;
};

LIBBPF_API int
bpf_object__open_skeleton(struct bpf_object_skeleton *s,
			  const struct bpf_object_open_opts *opts);
LIBBPF_API int bpf_object__load_skeleton(struct bpf_object_skeleton *s);
LIBBPF_API int bpf_object__attach_skeleton(struct bpf_object_skeleton *s);
LIBBPF_API void bpf_object__detach_skeleton(struct bpf_object_skeleton *s);
LIBBPF_API void bpf_object__destroy_skeleton(struct bpf_object_skeleton *s);

struct gen_loader_opts {
	size_t sz; /* size of this struct, for forward/backward compatiblity */
	const char *data;
	const char *insns;
	__u32 data_sz;
	__u32 insns_sz;
};

#define gen_loader_opts__last_field insns_sz
LIBBPF_API int bpf_object__gen_loader(struct bpf_object *obj,
				      struct gen_loader_opts *opts);

enum libbpf_tristate {
	TRI_NO = 0,
	TRI_YES = 1,
	TRI_MODULE = 2,
};

struct bpf_linker_opts {
	/* size of this struct, for forward/backward compatiblity */
	size_t sz;
};
#define bpf_linker_opts__last_field sz

struct bpf_linker_file_opts {
	/* size of this struct, for forward/backward compatiblity */
	size_t sz;
};
#define bpf_linker_file_opts__last_field sz

struct bpf_linker;

LIBBPF_API struct bpf_linker *bpf_linker__new(const char *filename, struct bpf_linker_opts *opts);
LIBBPF_API int bpf_linker__add_file(struct bpf_linker *linker,
				    const char *filename,
				    const struct bpf_linker_file_opts *opts);
LIBBPF_API int bpf_linker__finalize(struct bpf_linker *linker);
LIBBPF_API void bpf_linker__free(struct bpf_linker *linker);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* __LIBBPF_LIBBPF_H */
