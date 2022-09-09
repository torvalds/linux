// SPDX-License-Identifier: GPL-2.0
/*
 * queue_stack_maps.c: BPF queue and stack maps
 *
 * Copyright (c) 2018 Politecnico di Torino
 */
#include <linux/bpf.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/capability.h>
#include <linux/btf_ids.h>
#include "percpu_freelist.h"

#define QUEUE_STACK_CREATE_FLAG_MASK \
	(BPF_F_NUMA_NODE | BPF_F_ACCESS_MASK)

struct bpf_queue_stack {
	struct bpf_map map;
	raw_spinlock_t lock;
	u32 head, tail;
	u32 size; /* max_entries + 1 */

	char elements[] __aligned(8);
};

static struct bpf_queue_stack *bpf_queue_stack(struct bpf_map *map)
{
	return container_of(map, struct bpf_queue_stack, map);
}

static bool queue_stack_map_is_empty(struct bpf_queue_stack *qs)
{
	return qs->head == qs->tail;
}

static bool queue_stack_map_is_full(struct bpf_queue_stack *qs)
{
	u32 head = qs->head + 1;

	if (unlikely(head >= qs->size))
		head = 0;

	return head == qs->tail;
}

/* Called from syscall */
static int queue_stack_map_alloc_check(union bpf_attr *attr)
{
	if (!bpf_capable())
		return -EPERM;

	/* check sanity of attributes */
	if (attr->max_entries == 0 || attr->key_size != 0 ||
	    attr->value_size == 0 ||
	    attr->map_flags & ~QUEUE_STACK_CREATE_FLAG_MASK ||
	    !bpf_map_flags_access_ok(attr->map_flags))
		return -EINVAL;

	if (attr->value_size > KMALLOC_MAX_SIZE)
		/* if value_size is bigger, the user space won't be able to
		 * access the elements.
		 */
		return -E2BIG;

	return 0;
}

static struct bpf_map *queue_stack_map_alloc(union bpf_attr *attr)
{
	int numa_node = bpf_map_attr_numa_node(attr);
	struct bpf_queue_stack *qs;
	u64 size, queue_size;

	size = (u64) attr->max_entries + 1;
	queue_size = sizeof(*qs) + size * attr->value_size;

	qs = bpf_map_area_alloc(queue_size, numa_node);
	if (!qs)
		return ERR_PTR(-ENOMEM);

	memset(qs, 0, sizeof(*qs));

	bpf_map_init_from_attr(&qs->map, attr);

	qs->size = size;

	raw_spin_lock_init(&qs->lock);

	return &qs->map;
}

/* Called when map->refcnt goes to zero, either from workqueue or from syscall */
static void queue_stack_map_free(struct bpf_map *map)
{
	struct bpf_queue_stack *qs = bpf_queue_stack(map);

	bpf_map_area_free(qs);
}

static int __queue_map_get(struct bpf_map *map, void *value, bool delete)
{
	struct bpf_queue_stack *qs = bpf_queue_stack(map);
	unsigned long flags;
	int err = 0;
	void *ptr;

	raw_spin_lock_irqsave(&qs->lock, flags);

	if (queue_stack_map_is_empty(qs)) {
		memset(value, 0, qs->map.value_size);
		err = -ENOENT;
		goto out;
	}

	ptr = &qs->elements[qs->tail * qs->map.value_size];
	memcpy(value, ptr, qs->map.value_size);

	if (delete) {
		if (unlikely(++qs->tail >= qs->size))
			qs->tail = 0;
	}

out:
	raw_spin_unlock_irqrestore(&qs->lock, flags);
	return err;
}


static int __stack_map_get(struct bpf_map *map, void *value, bool delete)
{
	struct bpf_queue_stack *qs = bpf_queue_stack(map);
	unsigned long flags;
	int err = 0;
	void *ptr;
	u32 index;

	raw_spin_lock_irqsave(&qs->lock, flags);

	if (queue_stack_map_is_empty(qs)) {
		memset(value, 0, qs->map.value_size);
		err = -ENOENT;
		goto out;
	}

	index = qs->head - 1;
	if (unlikely(index >= qs->size))
		index = qs->size - 1;

	ptr = &qs->elements[index * qs->map.value_size];
	memcpy(value, ptr, qs->map.value_size);

	if (delete)
		qs->head = index;

out:
	raw_spin_unlock_irqrestore(&qs->lock, flags);
	return err;
}

/* Called from syscall or from eBPF program */
static int queue_map_peek_elem(struct bpf_map *map, void *value)
{
	return __queue_map_get(map, value, false);
}

/* Called from syscall or from eBPF program */
static int stack_map_peek_elem(struct bpf_map *map, void *value)
{
	return __stack_map_get(map, value, false);
}

/* Called from syscall or from eBPF program */
static int queue_map_pop_elem(struct bpf_map *map, void *value)
{
	return __queue_map_get(map, value, true);
}

/* Called from syscall or from eBPF program */
static int stack_map_pop_elem(struct bpf_map *map, void *value)
{
	return __stack_map_get(map, value, true);
}

/* Called from syscall or from eBPF program */
static int queue_stack_map_push_elem(struct bpf_map *map, void *value,
				     u64 flags)
{
	struct bpf_queue_stack *qs = bpf_queue_stack(map);
	unsigned long irq_flags;
	int err = 0;
	void *dst;

	/* BPF_EXIST is used to force making room for a new element in case the
	 * map is full
	 */
	bool replace = (flags & BPF_EXIST);

	/* Check supported flags for queue and stack maps */
	if (flags & BPF_NOEXIST || flags > BPF_EXIST)
		return -EINVAL;

	raw_spin_lock_irqsave(&qs->lock, irq_flags);

	if (queue_stack_map_is_full(qs)) {
		if (!replace) {
			err = -E2BIG;
			goto out;
		}
		/* advance tail pointer to overwrite oldest element */
		if (unlikely(++qs->tail >= qs->size))
			qs->tail = 0;
	}

	dst = &qs->elements[qs->head * qs->map.value_size];
	memcpy(dst, value, qs->map.value_size);

	if (unlikely(++qs->head >= qs->size))
		qs->head = 0;

out:
	raw_spin_unlock_irqrestore(&qs->lock, irq_flags);
	return err;
}

/* Called from syscall or from eBPF program */
static void *queue_stack_map_lookup_elem(struct bpf_map *map, void *key)
{
	return NULL;
}

/* Called from syscall or from eBPF program */
static int queue_stack_map_update_elem(struct bpf_map *map, void *key,
				       void *value, u64 flags)
{
	return -EINVAL;
}

/* Called from syscall or from eBPF program */
static int queue_stack_map_delete_elem(struct bpf_map *map, void *key)
{
	return -EINVAL;
}

/* Called from syscall */
static int queue_stack_map_get_next_key(struct bpf_map *map, void *key,
					void *next_key)
{
	return -EINVAL;
}

BTF_ID_LIST_SINGLE(queue_map_btf_ids, struct, bpf_queue_stack)
const struct bpf_map_ops queue_map_ops = {
	.map_meta_equal = bpf_map_meta_equal,
	.map_alloc_check = queue_stack_map_alloc_check,
	.map_alloc = queue_stack_map_alloc,
	.map_free = queue_stack_map_free,
	.map_lookup_elem = queue_stack_map_lookup_elem,
	.map_update_elem = queue_stack_map_update_elem,
	.map_delete_elem = queue_stack_map_delete_elem,
	.map_push_elem = queue_stack_map_push_elem,
	.map_pop_elem = queue_map_pop_elem,
	.map_peek_elem = queue_map_peek_elem,
	.map_get_next_key = queue_stack_map_get_next_key,
	.map_btf_id = &queue_map_btf_ids[0],
};

const struct bpf_map_ops stack_map_ops = {
	.map_meta_equal = bpf_map_meta_equal,
	.map_alloc_check = queue_stack_map_alloc_check,
	.map_alloc = queue_stack_map_alloc,
	.map_free = queue_stack_map_free,
	.map_lookup_elem = queue_stack_map_lookup_elem,
	.map_update_elem = queue_stack_map_update_elem,
	.map_delete_elem = queue_stack_map_delete_elem,
	.map_push_elem = queue_stack_map_push_elem,
	.map_pop_elem = stack_map_pop_elem,
	.map_peek_elem = stack_map_peek_elem,
	.map_get_next_key = queue_stack_map_get_next_key,
	.map_btf_id = &queue_map_btf_ids[0],
};
