/* Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#ifndef _LINUX_BPF_H
#define _LINUX_BPF_H 1

#include <uapi/linux/bpf.h>
#include <linux/workqueue.h>
#include <linux/file.h>

struct bpf_map;

/* map is generic key/value storage optionally accesible by eBPF programs */
struct bpf_map_ops {
	/* funcs callable from userspace (via syscall) */
	struct bpf_map *(*map_alloc)(union bpf_attr *attr);
	void (*map_free)(struct bpf_map *);
	int (*map_get_next_key)(struct bpf_map *map, void *key, void *next_key);

	/* funcs callable from userspace and from eBPF programs */
	void *(*map_lookup_elem)(struct bpf_map *map, void *key);
	int (*map_update_elem)(struct bpf_map *map, void *key, void *value);
	int (*map_delete_elem)(struct bpf_map *map, void *key);
};

struct bpf_map {
	atomic_t refcnt;
	enum bpf_map_type map_type;
	u32 key_size;
	u32 value_size;
	u32 max_entries;
	struct bpf_map_ops *ops;
	struct work_struct work;
};

struct bpf_map_type_list {
	struct list_head list_node;
	struct bpf_map_ops *ops;
	enum bpf_map_type type;
};

void bpf_register_map_type(struct bpf_map_type_list *tl);
void bpf_map_put(struct bpf_map *map);
struct bpf_map *bpf_map_get(struct fd f);

/* eBPF function prototype used by verifier to allow BPF_CALLs from eBPF programs
 * to in-kernel helper functions and for adjusting imm32 field in BPF_CALL
 * instructions after verifying
 */
struct bpf_func_proto {
	u64 (*func)(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5);
	bool gpl_only;
};

struct bpf_verifier_ops {
	/* return eBPF function prototype for verification */
	const struct bpf_func_proto *(*get_func_proto)(enum bpf_func_id func_id);
};

struct bpf_prog_type_list {
	struct list_head list_node;
	struct bpf_verifier_ops *ops;
	enum bpf_prog_type type;
};

void bpf_register_prog_type(struct bpf_prog_type_list *tl);

struct bpf_prog;

struct bpf_prog_aux {
	atomic_t refcnt;
	bool is_gpl_compatible;
	enum bpf_prog_type prog_type;
	struct bpf_verifier_ops *ops;
	struct bpf_map **used_maps;
	u32 used_map_cnt;
	struct bpf_prog *prog;
	struct work_struct work;
};

void bpf_prog_put(struct bpf_prog *prog);
struct bpf_prog *bpf_prog_get(u32 ufd);

#endif /* _LINUX_BPF_H */
