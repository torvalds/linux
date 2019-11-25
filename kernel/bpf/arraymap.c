// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 * Copyright (c) 2016,2017 Facebook
 */
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/filter.h>
#include <linux/perf_event.h>
#include <uapi/linux/btf.h>

#include "map_in_map.h"

#define ARRAY_CREATE_FLAG_MASK \
	(BPF_F_NUMA_NODE | BPF_F_MMAPABLE | BPF_F_ACCESS_MASK)

static void bpf_array_free_percpu(struct bpf_array *array)
{
	int i;

	for (i = 0; i < array->map.max_entries; i++) {
		free_percpu(array->pptrs[i]);
		cond_resched();
	}
}

static int bpf_array_alloc_percpu(struct bpf_array *array)
{
	void __percpu *ptr;
	int i;

	for (i = 0; i < array->map.max_entries; i++) {
		ptr = __alloc_percpu_gfp(array->elem_size, 8,
					 GFP_USER | __GFP_NOWARN);
		if (!ptr) {
			bpf_array_free_percpu(array);
			return -ENOMEM;
		}
		array->pptrs[i] = ptr;
		cond_resched();
	}

	return 0;
}

/* Called from syscall */
int array_map_alloc_check(union bpf_attr *attr)
{
	bool percpu = attr->map_type == BPF_MAP_TYPE_PERCPU_ARRAY;
	int numa_node = bpf_map_attr_numa_node(attr);

	/* check sanity of attributes */
	if (attr->max_entries == 0 || attr->key_size != 4 ||
	    attr->value_size == 0 ||
	    attr->map_flags & ~ARRAY_CREATE_FLAG_MASK ||
	    !bpf_map_flags_access_ok(attr->map_flags) ||
	    (percpu && numa_node != NUMA_NO_NODE))
		return -EINVAL;

	if (attr->map_type != BPF_MAP_TYPE_ARRAY &&
	    attr->map_flags & BPF_F_MMAPABLE)
		return -EINVAL;

	if (attr->value_size > KMALLOC_MAX_SIZE)
		/* if value_size is bigger, the user space won't be able to
		 * access the elements.
		 */
		return -E2BIG;

	return 0;
}

static struct bpf_map *array_map_alloc(union bpf_attr *attr)
{
	bool percpu = attr->map_type == BPF_MAP_TYPE_PERCPU_ARRAY;
	int ret, numa_node = bpf_map_attr_numa_node(attr);
	u32 elem_size, index_mask, max_entries;
	bool unpriv = !capable(CAP_SYS_ADMIN);
	u64 cost, array_size, mask64;
	struct bpf_map_memory mem;
	struct bpf_array *array;

	elem_size = round_up(attr->value_size, 8);

	max_entries = attr->max_entries;

	/* On 32 bit archs roundup_pow_of_two() with max_entries that has
	 * upper most bit set in u32 space is undefined behavior due to
	 * resulting 1U << 32, so do it manually here in u64 space.
	 */
	mask64 = fls_long(max_entries - 1);
	mask64 = 1ULL << mask64;
	mask64 -= 1;

	index_mask = mask64;
	if (unpriv) {
		/* round up array size to nearest power of 2,
		 * since cpu will speculate within index_mask limits
		 */
		max_entries = index_mask + 1;
		/* Check for overflows. */
		if (max_entries < attr->max_entries)
			return ERR_PTR(-E2BIG);
	}

	array_size = sizeof(*array);
	if (percpu) {
		array_size += (u64) max_entries * sizeof(void *);
	} else {
		/* rely on vmalloc() to return page-aligned memory and
		 * ensure array->value is exactly page-aligned
		 */
		if (attr->map_flags & BPF_F_MMAPABLE) {
			array_size = PAGE_ALIGN(array_size);
			array_size += PAGE_ALIGN((u64) max_entries * elem_size);
		} else {
			array_size += (u64) max_entries * elem_size;
		}
	}

	/* make sure there is no u32 overflow later in round_up() */
	cost = array_size;
	if (percpu)
		cost += (u64)attr->max_entries * elem_size * num_possible_cpus();

	ret = bpf_map_charge_init(&mem, cost);
	if (ret < 0)
		return ERR_PTR(ret);

	/* allocate all map elements and zero-initialize them */
	if (attr->map_flags & BPF_F_MMAPABLE) {
		void *data;

		/* kmalloc'ed memory can't be mmap'ed, use explicit vmalloc */
		data = bpf_map_area_mmapable_alloc(array_size, numa_node);
		if (!data) {
			bpf_map_charge_finish(&mem);
			return ERR_PTR(-ENOMEM);
		}
		array = data + PAGE_ALIGN(sizeof(struct bpf_array))
			- offsetof(struct bpf_array, value);
	} else {
		array = bpf_map_area_alloc(array_size, numa_node);
	}
	if (!array) {
		bpf_map_charge_finish(&mem);
		return ERR_PTR(-ENOMEM);
	}
	array->index_mask = index_mask;
	array->map.unpriv_array = unpriv;

	/* copy mandatory map attributes */
	bpf_map_init_from_attr(&array->map, attr);
	bpf_map_charge_move(&array->map.memory, &mem);
	array->elem_size = elem_size;

	if (percpu && bpf_array_alloc_percpu(array)) {
		bpf_map_charge_finish(&array->map.memory);
		bpf_map_area_free(array);
		return ERR_PTR(-ENOMEM);
	}

	return &array->map;
}

/* Called from syscall or from eBPF program */
static void *array_map_lookup_elem(struct bpf_map *map, void *key)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	u32 index = *(u32 *)key;

	if (unlikely(index >= array->map.max_entries))
		return NULL;

	return array->value + array->elem_size * (index & array->index_mask);
}

static int array_map_direct_value_addr(const struct bpf_map *map, u64 *imm,
				       u32 off)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);

	if (map->max_entries != 1)
		return -ENOTSUPP;
	if (off >= map->value_size)
		return -EINVAL;

	*imm = (unsigned long)array->value;
	return 0;
}

static int array_map_direct_value_meta(const struct bpf_map *map, u64 imm,
				       u32 *off)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	u64 base = (unsigned long)array->value;
	u64 range = array->elem_size;

	if (map->max_entries != 1)
		return -ENOTSUPP;
	if (imm < base || imm >= base + range)
		return -ENOENT;

	*off = imm - base;
	return 0;
}

/* emit BPF instructions equivalent to C code of array_map_lookup_elem() */
static u32 array_map_gen_lookup(struct bpf_map *map, struct bpf_insn *insn_buf)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	struct bpf_insn *insn = insn_buf;
	u32 elem_size = round_up(map->value_size, 8);
	const int ret = BPF_REG_0;
	const int map_ptr = BPF_REG_1;
	const int index = BPF_REG_2;

	*insn++ = BPF_ALU64_IMM(BPF_ADD, map_ptr, offsetof(struct bpf_array, value));
	*insn++ = BPF_LDX_MEM(BPF_W, ret, index, 0);
	if (map->unpriv_array) {
		*insn++ = BPF_JMP_IMM(BPF_JGE, ret, map->max_entries, 4);
		*insn++ = BPF_ALU32_IMM(BPF_AND, ret, array->index_mask);
	} else {
		*insn++ = BPF_JMP_IMM(BPF_JGE, ret, map->max_entries, 3);
	}

	if (is_power_of_2(elem_size)) {
		*insn++ = BPF_ALU64_IMM(BPF_LSH, ret, ilog2(elem_size));
	} else {
		*insn++ = BPF_ALU64_IMM(BPF_MUL, ret, elem_size);
	}
	*insn++ = BPF_ALU64_REG(BPF_ADD, ret, map_ptr);
	*insn++ = BPF_JMP_IMM(BPF_JA, 0, 0, 1);
	*insn++ = BPF_MOV64_IMM(ret, 0);
	return insn - insn_buf;
}

/* Called from eBPF program */
static void *percpu_array_map_lookup_elem(struct bpf_map *map, void *key)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	u32 index = *(u32 *)key;

	if (unlikely(index >= array->map.max_entries))
		return NULL;

	return this_cpu_ptr(array->pptrs[index & array->index_mask]);
}

int bpf_percpu_array_copy(struct bpf_map *map, void *key, void *value)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	u32 index = *(u32 *)key;
	void __percpu *pptr;
	int cpu, off = 0;
	u32 size;

	if (unlikely(index >= array->map.max_entries))
		return -ENOENT;

	/* per_cpu areas are zero-filled and bpf programs can only
	 * access 'value_size' of them, so copying rounded areas
	 * will not leak any kernel data
	 */
	size = round_up(map->value_size, 8);
	rcu_read_lock();
	pptr = array->pptrs[index & array->index_mask];
	for_each_possible_cpu(cpu) {
		bpf_long_memcpy(value + off, per_cpu_ptr(pptr, cpu), size);
		off += size;
	}
	rcu_read_unlock();
	return 0;
}

/* Called from syscall */
static int array_map_get_next_key(struct bpf_map *map, void *key, void *next_key)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	u32 index = key ? *(u32 *)key : U32_MAX;
	u32 *next = (u32 *)next_key;

	if (index >= array->map.max_entries) {
		*next = 0;
		return 0;
	}

	if (index == array->map.max_entries - 1)
		return -ENOENT;

	*next = index + 1;
	return 0;
}

/* Called from syscall or from eBPF program */
static int array_map_update_elem(struct bpf_map *map, void *key, void *value,
				 u64 map_flags)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	u32 index = *(u32 *)key;
	char *val;

	if (unlikely((map_flags & ~BPF_F_LOCK) > BPF_EXIST))
		/* unknown flags */
		return -EINVAL;

	if (unlikely(index >= array->map.max_entries))
		/* all elements were pre-allocated, cannot insert a new one */
		return -E2BIG;

	if (unlikely(map_flags & BPF_NOEXIST))
		/* all elements already exist */
		return -EEXIST;

	if (unlikely((map_flags & BPF_F_LOCK) &&
		     !map_value_has_spin_lock(map)))
		return -EINVAL;

	if (array->map.map_type == BPF_MAP_TYPE_PERCPU_ARRAY) {
		memcpy(this_cpu_ptr(array->pptrs[index & array->index_mask]),
		       value, map->value_size);
	} else {
		val = array->value +
			array->elem_size * (index & array->index_mask);
		if (map_flags & BPF_F_LOCK)
			copy_map_value_locked(map, val, value, false);
		else
			copy_map_value(map, val, value);
	}
	return 0;
}

int bpf_percpu_array_update(struct bpf_map *map, void *key, void *value,
			    u64 map_flags)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	u32 index = *(u32 *)key;
	void __percpu *pptr;
	int cpu, off = 0;
	u32 size;

	if (unlikely(map_flags > BPF_EXIST))
		/* unknown flags */
		return -EINVAL;

	if (unlikely(index >= array->map.max_entries))
		/* all elements were pre-allocated, cannot insert a new one */
		return -E2BIG;

	if (unlikely(map_flags == BPF_NOEXIST))
		/* all elements already exist */
		return -EEXIST;

	/* the user space will provide round_up(value_size, 8) bytes that
	 * will be copied into per-cpu area. bpf programs can only access
	 * value_size of it. During lookup the same extra bytes will be
	 * returned or zeros which were zero-filled by percpu_alloc,
	 * so no kernel data leaks possible
	 */
	size = round_up(map->value_size, 8);
	rcu_read_lock();
	pptr = array->pptrs[index & array->index_mask];
	for_each_possible_cpu(cpu) {
		bpf_long_memcpy(per_cpu_ptr(pptr, cpu), value + off, size);
		off += size;
	}
	rcu_read_unlock();
	return 0;
}

/* Called from syscall or from eBPF program */
static int array_map_delete_elem(struct bpf_map *map, void *key)
{
	return -EINVAL;
}

static void *array_map_vmalloc_addr(struct bpf_array *array)
{
	return (void *)round_down((unsigned long)array, PAGE_SIZE);
}

/* Called when map->refcnt goes to zero, either from workqueue or from syscall */
static void array_map_free(struct bpf_map *map)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);

	/* at this point bpf_prog->aux->refcnt == 0 and this map->refcnt == 0,
	 * so the programs (can be more than one that used this map) were
	 * disconnected from events. Wait for outstanding programs to complete
	 * and free the array
	 */
	synchronize_rcu();

	if (array->map.map_type == BPF_MAP_TYPE_PERCPU_ARRAY)
		bpf_array_free_percpu(array);

	if (array->map.map_flags & BPF_F_MMAPABLE)
		bpf_map_area_free(array_map_vmalloc_addr(array));
	else
		bpf_map_area_free(array);
}

static void array_map_seq_show_elem(struct bpf_map *map, void *key,
				    struct seq_file *m)
{
	void *value;

	rcu_read_lock();

	value = array_map_lookup_elem(map, key);
	if (!value) {
		rcu_read_unlock();
		return;
	}

	if (map->btf_key_type_id)
		seq_printf(m, "%u: ", *(u32 *)key);
	btf_type_seq_show(map->btf, map->btf_value_type_id, value, m);
	seq_puts(m, "\n");

	rcu_read_unlock();
}

static void percpu_array_map_seq_show_elem(struct bpf_map *map, void *key,
					   struct seq_file *m)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	u32 index = *(u32 *)key;
	void __percpu *pptr;
	int cpu;

	rcu_read_lock();

	seq_printf(m, "%u: {\n", *(u32 *)key);
	pptr = array->pptrs[index & array->index_mask];
	for_each_possible_cpu(cpu) {
		seq_printf(m, "\tcpu%d: ", cpu);
		btf_type_seq_show(map->btf, map->btf_value_type_id,
				  per_cpu_ptr(pptr, cpu), m);
		seq_puts(m, "\n");
	}
	seq_puts(m, "}\n");

	rcu_read_unlock();
}

static int array_map_check_btf(const struct bpf_map *map,
			       const struct btf *btf,
			       const struct btf_type *key_type,
			       const struct btf_type *value_type)
{
	u32 int_data;

	/* One exception for keyless BTF: .bss/.data/.rodata map */
	if (btf_type_is_void(key_type)) {
		if (map->map_type != BPF_MAP_TYPE_ARRAY ||
		    map->max_entries != 1)
			return -EINVAL;

		if (BTF_INFO_KIND(value_type->info) != BTF_KIND_DATASEC)
			return -EINVAL;

		return 0;
	}

	if (BTF_INFO_KIND(key_type->info) != BTF_KIND_INT)
		return -EINVAL;

	int_data = *(u32 *)(key_type + 1);
	/* bpf array can only take a u32 key. This check makes sure
	 * that the btf matches the attr used during map_create.
	 */
	if (BTF_INT_BITS(int_data) != 32 || BTF_INT_OFFSET(int_data))
		return -EINVAL;

	return 0;
}

static int array_map_mmap(struct bpf_map *map, struct vm_area_struct *vma)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	pgoff_t pgoff = PAGE_ALIGN(sizeof(*array)) >> PAGE_SHIFT;

	if (!(map->map_flags & BPF_F_MMAPABLE))
		return -EINVAL;

	return remap_vmalloc_range(vma, array_map_vmalloc_addr(array), pgoff);
}

const struct bpf_map_ops array_map_ops = {
	.map_alloc_check = array_map_alloc_check,
	.map_alloc = array_map_alloc,
	.map_free = array_map_free,
	.map_get_next_key = array_map_get_next_key,
	.map_lookup_elem = array_map_lookup_elem,
	.map_update_elem = array_map_update_elem,
	.map_delete_elem = array_map_delete_elem,
	.map_gen_lookup = array_map_gen_lookup,
	.map_direct_value_addr = array_map_direct_value_addr,
	.map_direct_value_meta = array_map_direct_value_meta,
	.map_mmap = array_map_mmap,
	.map_seq_show_elem = array_map_seq_show_elem,
	.map_check_btf = array_map_check_btf,
};

const struct bpf_map_ops percpu_array_map_ops = {
	.map_alloc_check = array_map_alloc_check,
	.map_alloc = array_map_alloc,
	.map_free = array_map_free,
	.map_get_next_key = array_map_get_next_key,
	.map_lookup_elem = percpu_array_map_lookup_elem,
	.map_update_elem = array_map_update_elem,
	.map_delete_elem = array_map_delete_elem,
	.map_seq_show_elem = percpu_array_map_seq_show_elem,
	.map_check_btf = array_map_check_btf,
};

static int fd_array_map_alloc_check(union bpf_attr *attr)
{
	/* only file descriptors can be stored in this type of map */
	if (attr->value_size != sizeof(u32))
		return -EINVAL;
	/* Program read-only/write-only not supported for special maps yet. */
	if (attr->map_flags & (BPF_F_RDONLY_PROG | BPF_F_WRONLY_PROG))
		return -EINVAL;
	return array_map_alloc_check(attr);
}

static void fd_array_map_free(struct bpf_map *map)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	int i;

	synchronize_rcu();

	/* make sure it's empty */
	for (i = 0; i < array->map.max_entries; i++)
		BUG_ON(array->ptrs[i] != NULL);

	bpf_map_area_free(array);
}

static void *fd_array_map_lookup_elem(struct bpf_map *map, void *key)
{
	return ERR_PTR(-EOPNOTSUPP);
}

/* only called from syscall */
int bpf_fd_array_map_lookup_elem(struct bpf_map *map, void *key, u32 *value)
{
	void **elem, *ptr;
	int ret =  0;

	if (!map->ops->map_fd_sys_lookup_elem)
		return -ENOTSUPP;

	rcu_read_lock();
	elem = array_map_lookup_elem(map, key);
	if (elem && (ptr = READ_ONCE(*elem)))
		*value = map->ops->map_fd_sys_lookup_elem(ptr);
	else
		ret = -ENOENT;
	rcu_read_unlock();

	return ret;
}

/* only called from syscall */
int bpf_fd_array_map_update_elem(struct bpf_map *map, struct file *map_file,
				 void *key, void *value, u64 map_flags)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	void *new_ptr, *old_ptr;
	u32 index = *(u32 *)key, ufd;

	if (map_flags != BPF_ANY)
		return -EINVAL;

	if (index >= array->map.max_entries)
		return -E2BIG;

	ufd = *(u32 *)value;
	new_ptr = map->ops->map_fd_get_ptr(map, map_file, ufd);
	if (IS_ERR(new_ptr))
		return PTR_ERR(new_ptr);

	if (map->ops->map_poke_run) {
		mutex_lock(&array->aux->poke_mutex);
		old_ptr = xchg(array->ptrs + index, new_ptr);
		map->ops->map_poke_run(map, index, old_ptr, new_ptr);
		mutex_unlock(&array->aux->poke_mutex);
	} else {
		old_ptr = xchg(array->ptrs + index, new_ptr);
	}

	if (old_ptr)
		map->ops->map_fd_put_ptr(old_ptr);
	return 0;
}

static int fd_array_map_delete_elem(struct bpf_map *map, void *key)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	void *old_ptr;
	u32 index = *(u32 *)key;

	if (index >= array->map.max_entries)
		return -E2BIG;

	if (map->ops->map_poke_run) {
		mutex_lock(&array->aux->poke_mutex);
		old_ptr = xchg(array->ptrs + index, NULL);
		map->ops->map_poke_run(map, index, old_ptr, NULL);
		mutex_unlock(&array->aux->poke_mutex);
	} else {
		old_ptr = xchg(array->ptrs + index, NULL);
	}

	if (old_ptr) {
		map->ops->map_fd_put_ptr(old_ptr);
		return 0;
	} else {
		return -ENOENT;
	}
}

static void *prog_fd_array_get_ptr(struct bpf_map *map,
				   struct file *map_file, int fd)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	struct bpf_prog *prog = bpf_prog_get(fd);

	if (IS_ERR(prog))
		return prog;

	if (!bpf_prog_array_compatible(array, prog)) {
		bpf_prog_put(prog);
		return ERR_PTR(-EINVAL);
	}

	return prog;
}

static void prog_fd_array_put_ptr(void *ptr)
{
	bpf_prog_put(ptr);
}

static u32 prog_fd_array_sys_lookup_elem(void *ptr)
{
	return ((struct bpf_prog *)ptr)->aux->id;
}

/* decrement refcnt of all bpf_progs that are stored in this map */
static void bpf_fd_array_map_clear(struct bpf_map *map)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	int i;

	for (i = 0; i < array->map.max_entries; i++)
		fd_array_map_delete_elem(map, &i);
}

static void prog_array_map_seq_show_elem(struct bpf_map *map, void *key,
					 struct seq_file *m)
{
	void **elem, *ptr;
	u32 prog_id;

	rcu_read_lock();

	elem = array_map_lookup_elem(map, key);
	if (elem) {
		ptr = READ_ONCE(*elem);
		if (ptr) {
			seq_printf(m, "%u: ", *(u32 *)key);
			prog_id = prog_fd_array_sys_lookup_elem(ptr);
			btf_type_seq_show(map->btf, map->btf_value_type_id,
					  &prog_id, m);
			seq_puts(m, "\n");
		}
	}

	rcu_read_unlock();
}

struct prog_poke_elem {
	struct list_head list;
	struct bpf_prog_aux *aux;
};

static int prog_array_map_poke_track(struct bpf_map *map,
				     struct bpf_prog_aux *prog_aux)
{
	struct prog_poke_elem *elem;
	struct bpf_array_aux *aux;
	int ret = 0;

	aux = container_of(map, struct bpf_array, map)->aux;
	mutex_lock(&aux->poke_mutex);
	list_for_each_entry(elem, &aux->poke_progs, list) {
		if (elem->aux == prog_aux)
			goto out;
	}

	elem = kmalloc(sizeof(*elem), GFP_KERNEL);
	if (!elem) {
		ret = -ENOMEM;
		goto out;
	}

	INIT_LIST_HEAD(&elem->list);
	/* We must track the program's aux info at this point in time
	 * since the program pointer itself may not be stable yet, see
	 * also comment in prog_array_map_poke_run().
	 */
	elem->aux = prog_aux;

	list_add_tail(&elem->list, &aux->poke_progs);
out:
	mutex_unlock(&aux->poke_mutex);
	return ret;
}

static void prog_array_map_poke_untrack(struct bpf_map *map,
					struct bpf_prog_aux *prog_aux)
{
	struct prog_poke_elem *elem, *tmp;
	struct bpf_array_aux *aux;

	aux = container_of(map, struct bpf_array, map)->aux;
	mutex_lock(&aux->poke_mutex);
	list_for_each_entry_safe(elem, tmp, &aux->poke_progs, list) {
		if (elem->aux == prog_aux) {
			list_del_init(&elem->list);
			kfree(elem);
			break;
		}
	}
	mutex_unlock(&aux->poke_mutex);
}

static void prog_array_map_poke_run(struct bpf_map *map, u32 key,
				    struct bpf_prog *old,
				    struct bpf_prog *new)
{
	struct prog_poke_elem *elem;
	struct bpf_array_aux *aux;

	aux = container_of(map, struct bpf_array, map)->aux;
	WARN_ON_ONCE(!mutex_is_locked(&aux->poke_mutex));

	list_for_each_entry(elem, &aux->poke_progs, list) {
		struct bpf_jit_poke_descriptor *poke;
		int i, ret;

		for (i = 0; i < elem->aux->size_poke_tab; i++) {
			poke = &elem->aux->poke_tab[i];

			/* Few things to be aware of:
			 *
			 * 1) We can only ever access aux in this context, but
			 *    not aux->prog since it might not be stable yet and
			 *    there could be danger of use after free otherwise.
			 * 2) Initially when we start tracking aux, the program
			 *    is not JITed yet and also does not have a kallsyms
			 *    entry. We skip these as poke->ip_stable is not
			 *    active yet. The JIT will do the final fixup before
			 *    setting it stable. The various poke->ip_stable are
			 *    successively activated, so tail call updates can
			 *    arrive from here while JIT is still finishing its
			 *    final fixup for non-activated poke entries.
			 * 3) On program teardown, the program's kallsym entry gets
			 *    removed out of RCU callback, but we can only untrack
			 *    from sleepable context, therefore bpf_arch_text_poke()
			 *    might not see that this is in BPF text section and
			 *    bails out with -EINVAL. As these are unreachable since
			 *    RCU grace period already passed, we simply skip them.
			 * 4) Also programs reaching refcount of zero while patching
			 *    is in progress is okay since we're protected under
			 *    poke_mutex and untrack the programs before the JIT
			 *    buffer is freed. When we're still in the middle of
			 *    patching and suddenly kallsyms entry of the program
			 *    gets evicted, we just skip the rest which is fine due
			 *    to point 3).
			 * 5) Any other error happening below from bpf_arch_text_poke()
			 *    is a unexpected bug.
			 */
			if (!READ_ONCE(poke->ip_stable))
				continue;
			if (poke->reason != BPF_POKE_REASON_TAIL_CALL)
				continue;
			if (poke->tail_call.map != map ||
			    poke->tail_call.key != key)
				continue;

			ret = bpf_arch_text_poke(poke->ip, BPF_MOD_JUMP,
						 old ? (u8 *)old->bpf_func +
						 poke->adj_off : NULL,
						 new ? (u8 *)new->bpf_func +
						 poke->adj_off : NULL);
			BUG_ON(ret < 0 && ret != -EINVAL);
		}
	}
}

static void prog_array_map_clear_deferred(struct work_struct *work)
{
	struct bpf_map *map = container_of(work, struct bpf_array_aux,
					   work)->map;
	bpf_fd_array_map_clear(map);
	bpf_map_put(map);
}

static void prog_array_map_clear(struct bpf_map *map)
{
	struct bpf_array_aux *aux = container_of(map, struct bpf_array,
						 map)->aux;
	bpf_map_inc(map);
	schedule_work(&aux->work);
}

static struct bpf_map *prog_array_map_alloc(union bpf_attr *attr)
{
	struct bpf_array_aux *aux;
	struct bpf_map *map;

	aux = kzalloc(sizeof(*aux), GFP_KERNEL);
	if (!aux)
		return ERR_PTR(-ENOMEM);

	INIT_WORK(&aux->work, prog_array_map_clear_deferred);
	INIT_LIST_HEAD(&aux->poke_progs);
	mutex_init(&aux->poke_mutex);

	map = array_map_alloc(attr);
	if (IS_ERR(map)) {
		kfree(aux);
		return map;
	}

	container_of(map, struct bpf_array, map)->aux = aux;
	aux->map = map;

	return map;
}

static void prog_array_map_free(struct bpf_map *map)
{
	struct prog_poke_elem *elem, *tmp;
	struct bpf_array_aux *aux;

	aux = container_of(map, struct bpf_array, map)->aux;
	list_for_each_entry_safe(elem, tmp, &aux->poke_progs, list) {
		list_del_init(&elem->list);
		kfree(elem);
	}
	kfree(aux);
	fd_array_map_free(map);
}

const struct bpf_map_ops prog_array_map_ops = {
	.map_alloc_check = fd_array_map_alloc_check,
	.map_alloc = prog_array_map_alloc,
	.map_free = prog_array_map_free,
	.map_poke_track = prog_array_map_poke_track,
	.map_poke_untrack = prog_array_map_poke_untrack,
	.map_poke_run = prog_array_map_poke_run,
	.map_get_next_key = array_map_get_next_key,
	.map_lookup_elem = fd_array_map_lookup_elem,
	.map_delete_elem = fd_array_map_delete_elem,
	.map_fd_get_ptr = prog_fd_array_get_ptr,
	.map_fd_put_ptr = prog_fd_array_put_ptr,
	.map_fd_sys_lookup_elem = prog_fd_array_sys_lookup_elem,
	.map_release_uref = prog_array_map_clear,
	.map_seq_show_elem = prog_array_map_seq_show_elem,
};

static struct bpf_event_entry *bpf_event_entry_gen(struct file *perf_file,
						   struct file *map_file)
{
	struct bpf_event_entry *ee;

	ee = kzalloc(sizeof(*ee), GFP_ATOMIC);
	if (ee) {
		ee->event = perf_file->private_data;
		ee->perf_file = perf_file;
		ee->map_file = map_file;
	}

	return ee;
}

static void __bpf_event_entry_free(struct rcu_head *rcu)
{
	struct bpf_event_entry *ee;

	ee = container_of(rcu, struct bpf_event_entry, rcu);
	fput(ee->perf_file);
	kfree(ee);
}

static void bpf_event_entry_free_rcu(struct bpf_event_entry *ee)
{
	call_rcu(&ee->rcu, __bpf_event_entry_free);
}

static void *perf_event_fd_array_get_ptr(struct bpf_map *map,
					 struct file *map_file, int fd)
{
	struct bpf_event_entry *ee;
	struct perf_event *event;
	struct file *perf_file;
	u64 value;

	perf_file = perf_event_get(fd);
	if (IS_ERR(perf_file))
		return perf_file;

	ee = ERR_PTR(-EOPNOTSUPP);
	event = perf_file->private_data;
	if (perf_event_read_local(event, &value, NULL, NULL) == -EOPNOTSUPP)
		goto err_out;

	ee = bpf_event_entry_gen(perf_file, map_file);
	if (ee)
		return ee;
	ee = ERR_PTR(-ENOMEM);
err_out:
	fput(perf_file);
	return ee;
}

static void perf_event_fd_array_put_ptr(void *ptr)
{
	bpf_event_entry_free_rcu(ptr);
}

static void perf_event_fd_array_release(struct bpf_map *map,
					struct file *map_file)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	struct bpf_event_entry *ee;
	int i;

	rcu_read_lock();
	for (i = 0; i < array->map.max_entries; i++) {
		ee = READ_ONCE(array->ptrs[i]);
		if (ee && ee->map_file == map_file)
			fd_array_map_delete_elem(map, &i);
	}
	rcu_read_unlock();
}

const struct bpf_map_ops perf_event_array_map_ops = {
	.map_alloc_check = fd_array_map_alloc_check,
	.map_alloc = array_map_alloc,
	.map_free = fd_array_map_free,
	.map_get_next_key = array_map_get_next_key,
	.map_lookup_elem = fd_array_map_lookup_elem,
	.map_delete_elem = fd_array_map_delete_elem,
	.map_fd_get_ptr = perf_event_fd_array_get_ptr,
	.map_fd_put_ptr = perf_event_fd_array_put_ptr,
	.map_release = perf_event_fd_array_release,
	.map_check_btf = map_check_no_btf,
};

#ifdef CONFIG_CGROUPS
static void *cgroup_fd_array_get_ptr(struct bpf_map *map,
				     struct file *map_file /* not used */,
				     int fd)
{
	return cgroup_get_from_fd(fd);
}

static void cgroup_fd_array_put_ptr(void *ptr)
{
	/* cgroup_put free cgrp after a rcu grace period */
	cgroup_put(ptr);
}

static void cgroup_fd_array_free(struct bpf_map *map)
{
	bpf_fd_array_map_clear(map);
	fd_array_map_free(map);
}

const struct bpf_map_ops cgroup_array_map_ops = {
	.map_alloc_check = fd_array_map_alloc_check,
	.map_alloc = array_map_alloc,
	.map_free = cgroup_fd_array_free,
	.map_get_next_key = array_map_get_next_key,
	.map_lookup_elem = fd_array_map_lookup_elem,
	.map_delete_elem = fd_array_map_delete_elem,
	.map_fd_get_ptr = cgroup_fd_array_get_ptr,
	.map_fd_put_ptr = cgroup_fd_array_put_ptr,
	.map_check_btf = map_check_no_btf,
};
#endif

static struct bpf_map *array_of_map_alloc(union bpf_attr *attr)
{
	struct bpf_map *map, *inner_map_meta;

	inner_map_meta = bpf_map_meta_alloc(attr->inner_map_fd);
	if (IS_ERR(inner_map_meta))
		return inner_map_meta;

	map = array_map_alloc(attr);
	if (IS_ERR(map)) {
		bpf_map_meta_free(inner_map_meta);
		return map;
	}

	map->inner_map_meta = inner_map_meta;

	return map;
}

static void array_of_map_free(struct bpf_map *map)
{
	/* map->inner_map_meta is only accessed by syscall which
	 * is protected by fdget/fdput.
	 */
	bpf_map_meta_free(map->inner_map_meta);
	bpf_fd_array_map_clear(map);
	fd_array_map_free(map);
}

static void *array_of_map_lookup_elem(struct bpf_map *map, void *key)
{
	struct bpf_map **inner_map = array_map_lookup_elem(map, key);

	if (!inner_map)
		return NULL;

	return READ_ONCE(*inner_map);
}

static u32 array_of_map_gen_lookup(struct bpf_map *map,
				   struct bpf_insn *insn_buf)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	u32 elem_size = round_up(map->value_size, 8);
	struct bpf_insn *insn = insn_buf;
	const int ret = BPF_REG_0;
	const int map_ptr = BPF_REG_1;
	const int index = BPF_REG_2;

	*insn++ = BPF_ALU64_IMM(BPF_ADD, map_ptr, offsetof(struct bpf_array, value));
	*insn++ = BPF_LDX_MEM(BPF_W, ret, index, 0);
	if (map->unpriv_array) {
		*insn++ = BPF_JMP_IMM(BPF_JGE, ret, map->max_entries, 6);
		*insn++ = BPF_ALU32_IMM(BPF_AND, ret, array->index_mask);
	} else {
		*insn++ = BPF_JMP_IMM(BPF_JGE, ret, map->max_entries, 5);
	}
	if (is_power_of_2(elem_size))
		*insn++ = BPF_ALU64_IMM(BPF_LSH, ret, ilog2(elem_size));
	else
		*insn++ = BPF_ALU64_IMM(BPF_MUL, ret, elem_size);
	*insn++ = BPF_ALU64_REG(BPF_ADD, ret, map_ptr);
	*insn++ = BPF_LDX_MEM(BPF_DW, ret, ret, 0);
	*insn++ = BPF_JMP_IMM(BPF_JEQ, ret, 0, 1);
	*insn++ = BPF_JMP_IMM(BPF_JA, 0, 0, 1);
	*insn++ = BPF_MOV64_IMM(ret, 0);

	return insn - insn_buf;
}

const struct bpf_map_ops array_of_maps_map_ops = {
	.map_alloc_check = fd_array_map_alloc_check,
	.map_alloc = array_of_map_alloc,
	.map_free = array_of_map_free,
	.map_get_next_key = array_map_get_next_key,
	.map_lookup_elem = array_of_map_lookup_elem,
	.map_delete_elem = fd_array_map_delete_elem,
	.map_fd_get_ptr = bpf_map_fd_get_ptr,
	.map_fd_put_ptr = bpf_map_fd_put_ptr,
	.map_fd_sys_lookup_elem = bpf_map_fd_sys_lookup_elem,
	.map_gen_lookup = array_of_map_gen_lookup,
	.map_check_btf = map_check_no_btf,
};
