/* SPDX-License-Identifier: LGPL-2.1 */

/*
 * Common eBPF ELF object loading operations.
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
#ifndef __BPF_LIBBPF_H
#define __BPF_LIBBPF_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>  // for size_t
#include <linux/bpf.h>

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
	__LIBBPF_ERRNO__END,
};

int libbpf_strerror(int err, char *buf, size_t size);

/*
 * __printf is defined in include/linux/compiler-gcc.h. However,
 * it would be better if libbpf.h didn't depend on Linux header files.
 * So instead of __printf, here we use gcc attribute directly.
 */
typedef int (*libbpf_print_fn_t)(const char *, ...)
	__attribute__((format(printf, 1, 2)));

void libbpf_set_print(libbpf_print_fn_t warn,
		      libbpf_print_fn_t info,
		      libbpf_print_fn_t debug);

/* Hide internal to user */
struct bpf_object;

struct bpf_object *bpf_object__open(const char *path);
struct bpf_object *bpf_object__open_buffer(void *obj_buf,
					   size_t obj_buf_sz,
					   const char *name);
int bpf_object__pin(struct bpf_object *object, const char *path);
void bpf_object__close(struct bpf_object *object);

/* Load/unload object into/from kernel */
int bpf_object__load(struct bpf_object *obj);
int bpf_object__unload(struct bpf_object *obj);
const char *bpf_object__name(struct bpf_object *obj);
unsigned int bpf_object__kversion(struct bpf_object *obj);
int bpf_object__btf_fd(const struct bpf_object *obj);

struct bpf_object *bpf_object__next(struct bpf_object *prev);
#define bpf_object__for_each_safe(pos, tmp)			\
	for ((pos) = bpf_object__next(NULL),		\
		(tmp) = bpf_object__next(pos);		\
	     (pos) != NULL;				\
	     (pos) = (tmp), (tmp) = bpf_object__next(tmp))

typedef void (*bpf_object_clear_priv_t)(struct bpf_object *, void *);
int bpf_object__set_priv(struct bpf_object *obj, void *priv,
			 bpf_object_clear_priv_t clear_priv);
void *bpf_object__priv(struct bpf_object *prog);

/* Accessors of bpf_program */
struct bpf_program;
struct bpf_program *bpf_program__next(struct bpf_program *prog,
				      struct bpf_object *obj);

#define bpf_object__for_each_program(pos, obj)		\
	for ((pos) = bpf_program__next(NULL, (obj));	\
	     (pos) != NULL;				\
	     (pos) = bpf_program__next((pos), (obj)))

typedef void (*bpf_program_clear_priv_t)(struct bpf_program *,
					 void *);

int bpf_program__set_priv(struct bpf_program *prog, void *priv,
			  bpf_program_clear_priv_t clear_priv);

void *bpf_program__priv(struct bpf_program *prog);

const char *bpf_program__title(struct bpf_program *prog, bool needs_copy);

int bpf_program__fd(struct bpf_program *prog);
int bpf_program__pin_instance(struct bpf_program *prog, const char *path,
			      int instance);
int bpf_program__pin(struct bpf_program *prog, const char *path);

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

int bpf_program__set_prep(struct bpf_program *prog, int nr_instance,
			  bpf_program_prep_t prep);

int bpf_program__nth_fd(struct bpf_program *prog, int n);

/*
 * Adjust type of BPF program. Default is kprobe.
 */
int bpf_program__set_socket_filter(struct bpf_program *prog);
int bpf_program__set_tracepoint(struct bpf_program *prog);
int bpf_program__set_raw_tracepoint(struct bpf_program *prog);
int bpf_program__set_kprobe(struct bpf_program *prog);
int bpf_program__set_sched_cls(struct bpf_program *prog);
int bpf_program__set_sched_act(struct bpf_program *prog);
int bpf_program__set_xdp(struct bpf_program *prog);
int bpf_program__set_perf_event(struct bpf_program *prog);
void bpf_program__set_type(struct bpf_program *prog, enum bpf_prog_type type);
void bpf_program__set_expected_attach_type(struct bpf_program *prog,
					   enum bpf_attach_type type);

bool bpf_program__is_socket_filter(struct bpf_program *prog);
bool bpf_program__is_tracepoint(struct bpf_program *prog);
bool bpf_program__is_raw_tracepoint(struct bpf_program *prog);
bool bpf_program__is_kprobe(struct bpf_program *prog);
bool bpf_program__is_sched_cls(struct bpf_program *prog);
bool bpf_program__is_sched_act(struct bpf_program *prog);
bool bpf_program__is_xdp(struct bpf_program *prog);
bool bpf_program__is_perf_event(struct bpf_program *prog);

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
struct bpf_map;
struct bpf_map *
bpf_object__find_map_by_name(struct bpf_object *obj, const char *name);

/*
 * Get bpf_map through the offset of corresponding struct bpf_map_def
 * in the BPF object file.
 */
struct bpf_map *
bpf_object__find_map_by_offset(struct bpf_object *obj, size_t offset);

struct bpf_map *
bpf_map__next(struct bpf_map *map, struct bpf_object *obj);
#define bpf_map__for_each(pos, obj)		\
	for ((pos) = bpf_map__next(NULL, (obj));	\
	     (pos) != NULL;				\
	     (pos) = bpf_map__next((pos), (obj)))

int bpf_map__fd(struct bpf_map *map);
const struct bpf_map_def *bpf_map__def(struct bpf_map *map);
const char *bpf_map__name(struct bpf_map *map);
uint32_t bpf_map__btf_key_type_id(const struct bpf_map *map);
uint32_t bpf_map__btf_value_type_id(const struct bpf_map *map);

typedef void (*bpf_map_clear_priv_t)(struct bpf_map *, void *);
int bpf_map__set_priv(struct bpf_map *map, void *priv,
		      bpf_map_clear_priv_t clear_priv);
void *bpf_map__priv(struct bpf_map *map);
int bpf_map__pin(struct bpf_map *map, const char *path);

long libbpf_get_error(const void *ptr);

struct bpf_prog_load_attr {
	const char *file;
	enum bpf_prog_type prog_type;
	enum bpf_attach_type expected_attach_type;
	int ifindex;
};

int bpf_prog_load_xattr(const struct bpf_prog_load_attr *attr,
			struct bpf_object **pobj, int *prog_fd);
int bpf_prog_load(const char *file, enum bpf_prog_type type,
		  struct bpf_object **pobj, int *prog_fd);

int bpf_set_link_xdp_fd(int ifindex, int fd, __u32 flags);

enum bpf_perf_event_ret {
	LIBBPF_PERF_EVENT_DONE	= 0,
	LIBBPF_PERF_EVENT_ERROR	= -1,
	LIBBPF_PERF_EVENT_CONT	= -2,
};

typedef enum bpf_perf_event_ret (*bpf_perf_event_print_t)(void *event,
							  void *priv);
int bpf_perf_event_read_simple(void *mem, unsigned long size,
			       unsigned long page_size,
			       void **buf, size_t *buf_len,
			       bpf_perf_event_print_t fn, void *priv);
#endif
