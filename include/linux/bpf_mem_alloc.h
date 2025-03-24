/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */
#ifndef _BPF_MEM_ALLOC_H
#define _BPF_MEM_ALLOC_H
#include <linux/compiler_types.h>
#include <linux/workqueue.h>

struct bpf_mem_cache;
struct bpf_mem_caches;

struct bpf_mem_alloc {
	struct bpf_mem_caches __percpu *caches;
	struct bpf_mem_cache __percpu *cache;
	struct obj_cgroup *objcg;
	bool percpu;
	struct work_struct work;
};

/* 'size != 0' is for bpf_mem_alloc which manages fixed-size objects.
 * Alloc and free are done with bpf_mem_cache_{alloc,free}().
 *
 * 'size = 0' is for bpf_mem_alloc which manages many fixed-size objects.
 * Alloc and free are done with bpf_mem_{alloc,free}() and the size of
 * the returned object is given by the size argument of bpf_mem_alloc().
 * If percpu equals true, error will be returned in order to avoid
 * large memory consumption and the below bpf_mem_alloc_percpu_unit_init()
 * should be used to do on-demand per-cpu allocation for each size.
 */
int bpf_mem_alloc_init(struct bpf_mem_alloc *ma, int size, bool percpu);
/* Initialize a non-fix-size percpu memory allocator */
int bpf_mem_alloc_percpu_init(struct bpf_mem_alloc *ma, struct obj_cgroup *objcg);
/* The percpu allocation with a specific unit size. */
int bpf_mem_alloc_percpu_unit_init(struct bpf_mem_alloc *ma, int size);
void bpf_mem_alloc_destroy(struct bpf_mem_alloc *ma);

/* Check the allocation size for kmalloc equivalent allocator */
int bpf_mem_alloc_check_size(bool percpu, size_t size);

/* kmalloc/kfree equivalent: */
void *bpf_mem_alloc(struct bpf_mem_alloc *ma, size_t size);
void bpf_mem_free(struct bpf_mem_alloc *ma, void *ptr);
void bpf_mem_free_rcu(struct bpf_mem_alloc *ma, void *ptr);

/* kmem_cache_alloc/free equivalent: */
void *bpf_mem_cache_alloc(struct bpf_mem_alloc *ma);
void bpf_mem_cache_free(struct bpf_mem_alloc *ma, void *ptr);
void bpf_mem_cache_free_rcu(struct bpf_mem_alloc *ma, void *ptr);
void bpf_mem_cache_raw_free(void *ptr);
void *bpf_mem_cache_alloc_flags(struct bpf_mem_alloc *ma, gfp_t flags);

#endif /* _BPF_MEM_ALLOC_H */
