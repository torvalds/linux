// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2016 Facebook
 */
#include <linux/bpf.h>
#include <linux/jhash.h>
#include <linux/filter.h>
#include <linux/stacktrace.h>
#include <linux/perf_event.h>
#include <linux/elf.h>
#include <linux/pagemap.h>
#include <linux/irq_work.h>
#include "percpu_freelist.h"

#define STACK_CREATE_FLAG_MASK					\
	(BPF_F_NUMA_NODE | BPF_F_RDONLY | BPF_F_WRONLY |	\
	 BPF_F_STACK_BUILD_ID)

struct stack_map_bucket {
	struct pcpu_freelist_node fnode;
	u32 hash;
	u32 nr;
	u64 data[];
};

struct bpf_stack_map {
	struct bpf_map map;
	void *elems;
	struct pcpu_freelist freelist;
	u32 n_buckets;
	struct stack_map_bucket *buckets[];
};

/* irq_work to run up_read() for build_id lookup in nmi context */
struct stack_map_irq_work {
	struct irq_work irq_work;
	struct rw_semaphore *sem;
};

static void do_up_read(struct irq_work *entry)
{
	struct stack_map_irq_work *work;

	work = container_of(entry, struct stack_map_irq_work, irq_work);
	up_read_non_owner(work->sem);
	work->sem = NULL;
}

static DEFINE_PER_CPU(struct stack_map_irq_work, up_read_work);

static inline bool stack_map_use_build_id(struct bpf_map *map)
{
	return (map->map_flags & BPF_F_STACK_BUILD_ID);
}

static inline int stack_map_data_size(struct bpf_map *map)
{
	return stack_map_use_build_id(map) ?
		sizeof(struct bpf_stack_build_id) : sizeof(u64);
}

static int prealloc_elems_and_freelist(struct bpf_stack_map *smap)
{
	u32 elem_size = sizeof(struct stack_map_bucket) + smap->map.value_size;
	int err;

	smap->elems = bpf_map_area_alloc(elem_size * smap->map.max_entries,
					 smap->map.numa_node);
	if (!smap->elems)
		return -ENOMEM;

	err = pcpu_freelist_init(&smap->freelist);
	if (err)
		goto free_elems;

	pcpu_freelist_populate(&smap->freelist, smap->elems, elem_size,
			       smap->map.max_entries);
	return 0;

free_elems:
	bpf_map_area_free(smap->elems);
	return err;
}

/* Called from syscall */
static struct bpf_map *stack_map_alloc(union bpf_attr *attr)
{
	u32 value_size = attr->value_size;
	struct bpf_stack_map *smap;
	struct bpf_map_memory mem;
	u64 cost, n_buckets;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return ERR_PTR(-EPERM);

	if (attr->map_flags & ~STACK_CREATE_FLAG_MASK)
		return ERR_PTR(-EINVAL);

	/* check sanity of attributes */
	if (attr->max_entries == 0 || attr->key_size != 4 ||
	    value_size < 8 || value_size % 8)
		return ERR_PTR(-EINVAL);

	BUILD_BUG_ON(sizeof(struct bpf_stack_build_id) % sizeof(u64));
	if (attr->map_flags & BPF_F_STACK_BUILD_ID) {
		if (value_size % sizeof(struct bpf_stack_build_id) ||
		    value_size / sizeof(struct bpf_stack_build_id)
		    > sysctl_perf_event_max_stack)
			return ERR_PTR(-EINVAL);
	} else if (value_size / 8 > sysctl_perf_event_max_stack)
		return ERR_PTR(-EINVAL);

	/* hash table size must be power of 2 */
	n_buckets = roundup_pow_of_two(attr->max_entries);

	cost = n_buckets * sizeof(struct stack_map_bucket *) + sizeof(*smap);
	cost += n_buckets * (value_size + sizeof(struct stack_map_bucket));
	err = bpf_map_charge_init(&mem, cost);
	if (err)
		return ERR_PTR(err);

	smap = bpf_map_area_alloc(cost, bpf_map_attr_numa_node(attr));
	if (!smap) {
		bpf_map_charge_finish(&mem);
		return ERR_PTR(-ENOMEM);
	}

	bpf_map_init_from_attr(&smap->map, attr);
	smap->map.value_size = value_size;
	smap->n_buckets = n_buckets;

	err = get_callchain_buffers(sysctl_perf_event_max_stack);
	if (err)
		goto free_charge;

	err = prealloc_elems_and_freelist(smap);
	if (err)
		goto put_buffers;

	bpf_map_charge_move(&smap->map.memory, &mem);

	return &smap->map;

put_buffers:
	put_callchain_buffers();
free_charge:
	bpf_map_charge_finish(&mem);
	bpf_map_area_free(smap);
	return ERR_PTR(err);
}

#define BPF_BUILD_ID 3
/*
 * Parse build id from the note segment. This logic can be shared between
 * 32-bit and 64-bit system, because Elf32_Nhdr and Elf64_Nhdr are
 * identical.
 */
static inline int stack_map_parse_build_id(void *page_addr,
					   unsigned char *build_id,
					   void *note_start,
					   Elf32_Word note_size)
{
	Elf32_Word note_offs = 0, new_offs;

	/* check for overflow */
	if (note_start < page_addr || note_start + note_size < note_start)
		return -EINVAL;

	/* only supports note that fits in the first page */
	if (note_start + note_size > page_addr + PAGE_SIZE)
		return -EINVAL;

	while (note_offs + sizeof(Elf32_Nhdr) < note_size) {
		Elf32_Nhdr *nhdr = (Elf32_Nhdr *)(note_start + note_offs);

		if (nhdr->n_type == BPF_BUILD_ID &&
		    nhdr->n_namesz == sizeof("GNU") &&
		    nhdr->n_descsz > 0 &&
		    nhdr->n_descsz <= BPF_BUILD_ID_SIZE) {
			memcpy(build_id,
			       note_start + note_offs +
			       ALIGN(sizeof("GNU"), 4) + sizeof(Elf32_Nhdr),
			       nhdr->n_descsz);
			memset(build_id + nhdr->n_descsz, 0,
			       BPF_BUILD_ID_SIZE - nhdr->n_descsz);
			return 0;
		}
		new_offs = note_offs + sizeof(Elf32_Nhdr) +
			ALIGN(nhdr->n_namesz, 4) + ALIGN(nhdr->n_descsz, 4);
		if (new_offs <= note_offs)  /* overflow */
			break;
		note_offs = new_offs;
	}
	return -EINVAL;
}

/* Parse build ID from 32-bit ELF */
static int stack_map_get_build_id_32(void *page_addr,
				     unsigned char *build_id)
{
	Elf32_Ehdr *ehdr = (Elf32_Ehdr *)page_addr;
	Elf32_Phdr *phdr;
	int i;

	/* only supports phdr that fits in one page */
	if (ehdr->e_phnum >
	    (PAGE_SIZE - sizeof(Elf32_Ehdr)) / sizeof(Elf32_Phdr))
		return -EINVAL;

	phdr = (Elf32_Phdr *)(page_addr + sizeof(Elf32_Ehdr));

	for (i = 0; i < ehdr->e_phnum; ++i)
		if (phdr[i].p_type == PT_NOTE)
			return stack_map_parse_build_id(page_addr, build_id,
					page_addr + phdr[i].p_offset,
					phdr[i].p_filesz);
	return -EINVAL;
}

/* Parse build ID from 64-bit ELF */
static int stack_map_get_build_id_64(void *page_addr,
				     unsigned char *build_id)
{
	Elf64_Ehdr *ehdr = (Elf64_Ehdr *)page_addr;
	Elf64_Phdr *phdr;
	int i;

	/* only supports phdr that fits in one page */
	if (ehdr->e_phnum >
	    (PAGE_SIZE - sizeof(Elf64_Ehdr)) / sizeof(Elf64_Phdr))
		return -EINVAL;

	phdr = (Elf64_Phdr *)(page_addr + sizeof(Elf64_Ehdr));

	for (i = 0; i < ehdr->e_phnum; ++i)
		if (phdr[i].p_type == PT_NOTE)
			return stack_map_parse_build_id(page_addr, build_id,
					page_addr + phdr[i].p_offset,
					phdr[i].p_filesz);
	return -EINVAL;
}

/* Parse build ID of ELF file mapped to vma */
static int stack_map_get_build_id(struct vm_area_struct *vma,
				  unsigned char *build_id)
{
	Elf32_Ehdr *ehdr;
	struct page *page;
	void *page_addr;
	int ret;

	/* only works for page backed storage  */
	if (!vma->vm_file)
		return -EINVAL;

	page = find_get_page(vma->vm_file->f_mapping, 0);
	if (!page)
		return -EFAULT;	/* page not mapped */

	ret = -EINVAL;
	page_addr = kmap_atomic(page);
	ehdr = (Elf32_Ehdr *)page_addr;

	/* compare magic x7f "ELF" */
	if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0)
		goto out;

	/* only support executable file and shared object file */
	if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN)
		goto out;

	if (ehdr->e_ident[EI_CLASS] == ELFCLASS32)
		ret = stack_map_get_build_id_32(page_addr, build_id);
	else if (ehdr->e_ident[EI_CLASS] == ELFCLASS64)
		ret = stack_map_get_build_id_64(page_addr, build_id);
out:
	kunmap_atomic(page_addr);
	put_page(page);
	return ret;
}

static void stack_map_get_build_id_offset(struct bpf_stack_build_id *id_offs,
					  u64 *ips, u32 trace_nr, bool user)
{
	int i;
	struct vm_area_struct *vma;
	bool irq_work_busy = false;
	struct stack_map_irq_work *work = NULL;

	if (irqs_disabled()) {
		work = this_cpu_ptr(&up_read_work);
		if (work->irq_work.flags & IRQ_WORK_BUSY)
			/* cannot queue more up_read, fallback */
			irq_work_busy = true;
	}

	/*
	 * We cannot do up_read() when the irq is disabled, because of
	 * risk to deadlock with rq_lock. To do build_id lookup when the
	 * irqs are disabled, we need to run up_read() in irq_work. We use
	 * a percpu variable to do the irq_work. If the irq_work is
	 * already used by another lookup, we fall back to report ips.
	 *
	 * Same fallback is used for kernel stack (!user) on a stackmap
	 * with build_id.
	 */
	if (!user || !current || !current->mm || irq_work_busy ||
	    down_read_trylock(&current->mm->mmap_sem) == 0) {
		/* cannot access current->mm, fall back to ips */
		for (i = 0; i < trace_nr; i++) {
			id_offs[i].status = BPF_STACK_BUILD_ID_IP;
			id_offs[i].ip = ips[i];
			memset(id_offs[i].build_id, 0, BPF_BUILD_ID_SIZE);
		}
		return;
	}

	for (i = 0; i < trace_nr; i++) {
		vma = find_vma(current->mm, ips[i]);
		if (!vma || stack_map_get_build_id(vma, id_offs[i].build_id)) {
			/* per entry fall back to ips */
			id_offs[i].status = BPF_STACK_BUILD_ID_IP;
			id_offs[i].ip = ips[i];
			memset(id_offs[i].build_id, 0, BPF_BUILD_ID_SIZE);
			continue;
		}
		id_offs[i].offset = (vma->vm_pgoff << PAGE_SHIFT) + ips[i]
			- vma->vm_start;
		id_offs[i].status = BPF_STACK_BUILD_ID_VALID;
	}

	if (!work) {
		up_read(&current->mm->mmap_sem);
	} else {
		work->sem = &current->mm->mmap_sem;
		irq_work_queue(&work->irq_work);
		/*
		 * The irq_work will release the mmap_sem with
		 * up_read_non_owner(). The rwsem_release() is called
		 * here to release the lock from lockdep's perspective.
		 */
		rwsem_release(&current->mm->mmap_sem.dep_map, _RET_IP_);
	}
}

BPF_CALL_3(bpf_get_stackid, struct pt_regs *, regs, struct bpf_map *, map,
	   u64, flags)
{
	struct bpf_stack_map *smap = container_of(map, struct bpf_stack_map, map);
	struct perf_callchain_entry *trace;
	struct stack_map_bucket *bucket, *new_bucket, *old_bucket;
	u32 max_depth = map->value_size / stack_map_data_size(map);
	/* stack_map_alloc() checks that max_depth <= sysctl_perf_event_max_stack */
	u32 init_nr = sysctl_perf_event_max_stack - max_depth;
	u32 skip = flags & BPF_F_SKIP_FIELD_MASK;
	u32 hash, id, trace_nr, trace_len;
	bool user = flags & BPF_F_USER_STACK;
	bool kernel = !user;
	u64 *ips;
	bool hash_matches;

	if (unlikely(flags & ~(BPF_F_SKIP_FIELD_MASK | BPF_F_USER_STACK |
			       BPF_F_FAST_STACK_CMP | BPF_F_REUSE_STACKID)))
		return -EINVAL;

	trace = get_perf_callchain(regs, init_nr, kernel, user,
				   sysctl_perf_event_max_stack, false, false);

	if (unlikely(!trace))
		/* couldn't fetch the stack trace */
		return -EFAULT;

	/* get_perf_callchain() guarantees that trace->nr >= init_nr
	 * and trace-nr <= sysctl_perf_event_max_stack, so trace_nr <= max_depth
	 */
	trace_nr = trace->nr - init_nr;

	if (trace_nr <= skip)
		/* skipping more than usable stack trace */
		return -EFAULT;

	trace_nr -= skip;
	trace_len = trace_nr * sizeof(u64);
	ips = trace->ip + skip + init_nr;
	hash = jhash2((u32 *)ips, trace_len / sizeof(u32), 0);
	id = hash & (smap->n_buckets - 1);
	bucket = READ_ONCE(smap->buckets[id]);

	hash_matches = bucket && bucket->hash == hash;
	/* fast cmp */
	if (hash_matches && flags & BPF_F_FAST_STACK_CMP)
		return id;

	if (stack_map_use_build_id(map)) {
		/* for build_id+offset, pop a bucket before slow cmp */
		new_bucket = (struct stack_map_bucket *)
			pcpu_freelist_pop(&smap->freelist);
		if (unlikely(!new_bucket))
			return -ENOMEM;
		new_bucket->nr = trace_nr;
		stack_map_get_build_id_offset(
			(struct bpf_stack_build_id *)new_bucket->data,
			ips, trace_nr, user);
		trace_len = trace_nr * sizeof(struct bpf_stack_build_id);
		if (hash_matches && bucket->nr == trace_nr &&
		    memcmp(bucket->data, new_bucket->data, trace_len) == 0) {
			pcpu_freelist_push(&smap->freelist, &new_bucket->fnode);
			return id;
		}
		if (bucket && !(flags & BPF_F_REUSE_STACKID)) {
			pcpu_freelist_push(&smap->freelist, &new_bucket->fnode);
			return -EEXIST;
		}
	} else {
		if (hash_matches && bucket->nr == trace_nr &&
		    memcmp(bucket->data, ips, trace_len) == 0)
			return id;
		if (bucket && !(flags & BPF_F_REUSE_STACKID))
			return -EEXIST;

		new_bucket = (struct stack_map_bucket *)
			pcpu_freelist_pop(&smap->freelist);
		if (unlikely(!new_bucket))
			return -ENOMEM;
		memcpy(new_bucket->data, ips, trace_len);
	}

	new_bucket->hash = hash;
	new_bucket->nr = trace_nr;

	old_bucket = xchg(&smap->buckets[id], new_bucket);
	if (old_bucket)
		pcpu_freelist_push(&smap->freelist, &old_bucket->fnode);
	return id;
}

const struct bpf_func_proto bpf_get_stackid_proto = {
	.func		= bpf_get_stackid,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
};

BPF_CALL_4(bpf_get_stack, struct pt_regs *, regs, void *, buf, u32, size,
	   u64, flags)
{
	u32 init_nr, trace_nr, copy_len, elem_size, num_elem;
	bool user_build_id = flags & BPF_F_USER_BUILD_ID;
	u32 skip = flags & BPF_F_SKIP_FIELD_MASK;
	bool user = flags & BPF_F_USER_STACK;
	struct perf_callchain_entry *trace;
	bool kernel = !user;
	int err = -EINVAL;
	u64 *ips;

	if (unlikely(flags & ~(BPF_F_SKIP_FIELD_MASK | BPF_F_USER_STACK |
			       BPF_F_USER_BUILD_ID)))
		goto clear;
	if (kernel && user_build_id)
		goto clear;

	elem_size = (user && user_build_id) ? sizeof(struct bpf_stack_build_id)
					    : sizeof(u64);
	if (unlikely(size % elem_size))
		goto clear;

	num_elem = size / elem_size;
	if (sysctl_perf_event_max_stack < num_elem)
		init_nr = 0;
	else
		init_nr = sysctl_perf_event_max_stack - num_elem;
	trace = get_perf_callchain(regs, init_nr, kernel, user,
				   sysctl_perf_event_max_stack, false, false);
	if (unlikely(!trace))
		goto err_fault;

	trace_nr = trace->nr - init_nr;
	if (trace_nr < skip)
		goto err_fault;

	trace_nr -= skip;
	trace_nr = (trace_nr <= num_elem) ? trace_nr : num_elem;
	copy_len = trace_nr * elem_size;
	ips = trace->ip + skip + init_nr;
	if (user && user_build_id)
		stack_map_get_build_id_offset(buf, ips, trace_nr, user);
	else
		memcpy(buf, ips, copy_len);

	if (size > copy_len)
		memset(buf + copy_len, 0, size - copy_len);
	return copy_len;

err_fault:
	err = -EFAULT;
clear:
	memset(buf, 0, size);
	return err;
}

const struct bpf_func_proto bpf_get_stack_proto = {
	.func		= bpf_get_stack,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg3_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg4_type	= ARG_ANYTHING,
};

/* Called from eBPF program */
static void *stack_map_lookup_elem(struct bpf_map *map, void *key)
{
	return ERR_PTR(-EOPNOTSUPP);
}

/* Called from syscall */
int bpf_stackmap_copy(struct bpf_map *map, void *key, void *value)
{
	struct bpf_stack_map *smap = container_of(map, struct bpf_stack_map, map);
	struct stack_map_bucket *bucket, *old_bucket;
	u32 id = *(u32 *)key, trace_len;

	if (unlikely(id >= smap->n_buckets))
		return -ENOENT;

	bucket = xchg(&smap->buckets[id], NULL);
	if (!bucket)
		return -ENOENT;

	trace_len = bucket->nr * stack_map_data_size(map);
	memcpy(value, bucket->data, trace_len);
	memset(value + trace_len, 0, map->value_size - trace_len);

	old_bucket = xchg(&smap->buckets[id], bucket);
	if (old_bucket)
		pcpu_freelist_push(&smap->freelist, &old_bucket->fnode);
	return 0;
}

static int stack_map_get_next_key(struct bpf_map *map, void *key,
				  void *next_key)
{
	struct bpf_stack_map *smap = container_of(map,
						  struct bpf_stack_map, map);
	u32 id;

	WARN_ON_ONCE(!rcu_read_lock_held());

	if (!key) {
		id = 0;
	} else {
		id = *(u32 *)key;
		if (id >= smap->n_buckets || !smap->buckets[id])
			id = 0;
		else
			id++;
	}

	while (id < smap->n_buckets && !smap->buckets[id])
		id++;

	if (id >= smap->n_buckets)
		return -ENOENT;

	*(u32 *)next_key = id;
	return 0;
}

static int stack_map_update_elem(struct bpf_map *map, void *key, void *value,
				 u64 map_flags)
{
	return -EINVAL;
}

/* Called from syscall or from eBPF program */
static int stack_map_delete_elem(struct bpf_map *map, void *key)
{
	struct bpf_stack_map *smap = container_of(map, struct bpf_stack_map, map);
	struct stack_map_bucket *old_bucket;
	u32 id = *(u32 *)key;

	if (unlikely(id >= smap->n_buckets))
		return -E2BIG;

	old_bucket = xchg(&smap->buckets[id], NULL);
	if (old_bucket) {
		pcpu_freelist_push(&smap->freelist, &old_bucket->fnode);
		return 0;
	} else {
		return -ENOENT;
	}
}

/* Called when map->refcnt goes to zero, either from workqueue or from syscall */
static void stack_map_free(struct bpf_map *map)
{
	struct bpf_stack_map *smap = container_of(map, struct bpf_stack_map, map);

	/* wait for bpf programs to complete before freeing stack map */
	synchronize_rcu();

	bpf_map_area_free(smap->elems);
	pcpu_freelist_destroy(&smap->freelist);
	bpf_map_area_free(smap);
	put_callchain_buffers();
}

const struct bpf_map_ops stack_trace_map_ops = {
	.map_alloc = stack_map_alloc,
	.map_free = stack_map_free,
	.map_get_next_key = stack_map_get_next_key,
	.map_lookup_elem = stack_map_lookup_elem,
	.map_update_elem = stack_map_update_elem,
	.map_delete_elem = stack_map_delete_elem,
	.map_check_btf = map_check_no_btf,
};

static int __init stack_map_init(void)
{
	int cpu;
	struct stack_map_irq_work *work;

	for_each_possible_cpu(cpu) {
		work = per_cpu_ptr(&up_read_work, cpu);
		init_irq_work(&work->irq_work, do_up_read);
	}
	return 0;
}
subsys_initcall(stack_map_init);
