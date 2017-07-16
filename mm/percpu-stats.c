/*
 * mm/percpu-debug.c
 *
 * Copyright (C) 2017		Facebook Inc.
 * Copyright (C) 2017		Dennis Zhou <dennisz@fb.com>
 *
 * This file is released under the GPLv2.
 *
 * Prints statistics about the percpu allocator and backing chunks.
 */
#include <linux/debugfs.h>
#include <linux/list.h>
#include <linux/percpu.h>
#include <linux/seq_file.h>
#include <linux/sort.h>
#include <linux/vmalloc.h>

#include "percpu-internal.h"

#define P(X, Y) \
	seq_printf(m, "  %-20s: %12lld\n", X, (long long int)Y)

struct percpu_stats pcpu_stats;
struct pcpu_alloc_info pcpu_stats_ai;

static int cmpint(const void *a, const void *b)
{
	return *(int *)a - *(int *)b;
}

/*
 * Iterates over all chunks to find the max # of map entries used.
 */
static int find_max_map_used(void)
{
	struct pcpu_chunk *chunk;
	int slot, max_map_used;

	max_map_used = 0;
	for (slot = 0; slot < pcpu_nr_slots; slot++)
		list_for_each_entry(chunk, &pcpu_slot[slot], list)
			max_map_used = max(max_map_used, chunk->map_used);

	return max_map_used;
}

/*
 * Prints out chunk state. Fragmentation is considered between
 * the beginning of the chunk to the last allocation.
 */
static void chunk_map_stats(struct seq_file *m, struct pcpu_chunk *chunk,
			    int *buffer)
{
	int i, s_index, last_alloc, alloc_sign, as_len;
	int *alloc_sizes, *p;
	/* statistics */
	int sum_frag = 0, max_frag = 0;
	int cur_min_alloc = 0, cur_med_alloc = 0, cur_max_alloc = 0;

	alloc_sizes = buffer;
	s_index = chunk->has_reserved ? 1 : 0;

	/* find last allocation */
	last_alloc = -1;
	for (i = chunk->map_used - 1; i >= s_index; i--) {
		if (chunk->map[i] & 1) {
			last_alloc = i;
			break;
		}
	}

	/* if the chunk is not empty - ignoring reserve */
	if (last_alloc >= s_index) {
		as_len = last_alloc + 1 - s_index;

		/*
		 * Iterate through chunk map computing size info.
		 * The first bit is overloaded to be a used flag.
		 * negative = free space, positive = allocated
		 */
		for (i = 0, p = chunk->map + s_index; i < as_len; i++, p++) {
			alloc_sign = (*p & 1) ? 1 : -1;
			alloc_sizes[i] = alloc_sign *
				((p[1] & ~1) - (p[0] & ~1));
		}

		sort(alloc_sizes, as_len, sizeof(chunk->map[0]), cmpint, NULL);

		/* Iterate through the unallocated fragements. */
		for (i = 0, p = alloc_sizes; *p < 0 && i < as_len; i++, p++) {
			sum_frag -= *p;
			max_frag = max(max_frag, -1 * (*p));
		}

		cur_min_alloc = alloc_sizes[i];
		cur_med_alloc = alloc_sizes[(i + as_len - 1) / 2];
		cur_max_alloc = alloc_sizes[as_len - 1];
	}

	P("nr_alloc", chunk->nr_alloc);
	P("max_alloc_size", chunk->max_alloc_size);
	P("free_size", chunk->free_size);
	P("contig_hint", chunk->contig_hint);
	P("sum_frag", sum_frag);
	P("max_frag", max_frag);
	P("cur_min_alloc", cur_min_alloc);
	P("cur_med_alloc", cur_med_alloc);
	P("cur_max_alloc", cur_max_alloc);
	seq_putc(m, '\n');
}

static int percpu_stats_show(struct seq_file *m, void *v)
{
	struct pcpu_chunk *chunk;
	int slot, max_map_used;
	int *buffer;

alloc_buffer:
	spin_lock_irq(&pcpu_lock);
	max_map_used = find_max_map_used();
	spin_unlock_irq(&pcpu_lock);

	buffer = vmalloc(max_map_used * sizeof(pcpu_first_chunk->map[0]));
	if (!buffer)
		return -ENOMEM;

	spin_lock_irq(&pcpu_lock);

	/* if the buffer allocated earlier is too small */
	if (max_map_used < find_max_map_used()) {
		spin_unlock_irq(&pcpu_lock);
		vfree(buffer);
		goto alloc_buffer;
	}

#define PL(X) \
	seq_printf(m, "  %-20s: %12lld\n", #X, (long long int)pcpu_stats_ai.X)

	seq_printf(m,
			"Percpu Memory Statistics\n"
			"Allocation Info:\n"
			"----------------------------------------\n");
	PL(unit_size);
	PL(static_size);
	PL(reserved_size);
	PL(dyn_size);
	PL(atom_size);
	PL(alloc_size);
	seq_putc(m, '\n');

#undef PL

#define PU(X) \
	seq_printf(m, "  %-20s: %12llu\n", #X, (unsigned long long)pcpu_stats.X)

	seq_printf(m,
			"Global Stats:\n"
			"----------------------------------------\n");
	PU(nr_alloc);
	PU(nr_dealloc);
	PU(nr_cur_alloc);
	PU(nr_max_alloc);
	PU(nr_chunks);
	PU(nr_max_chunks);
	PU(min_alloc_size);
	PU(max_alloc_size);
	seq_putc(m, '\n');

#undef PU

	seq_printf(m,
			"Per Chunk Stats:\n"
			"----------------------------------------\n");

	if (pcpu_reserved_chunk) {
		seq_puts(m, "Chunk: <- Reserved Chunk\n");
		chunk_map_stats(m, pcpu_reserved_chunk, buffer);
	}

	for (slot = 0; slot < pcpu_nr_slots; slot++) {
		list_for_each_entry(chunk, &pcpu_slot[slot], list) {
			if (chunk == pcpu_first_chunk) {
				seq_puts(m, "Chunk: <- First Chunk\n");
				chunk_map_stats(m, chunk, buffer);


			} else {
				seq_puts(m, "Chunk:\n");
				chunk_map_stats(m, chunk, buffer);
			}

		}
	}

	spin_unlock_irq(&pcpu_lock);

	vfree(buffer);

	return 0;
}

static int percpu_stats_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, percpu_stats_show, NULL);
}

static const struct file_operations percpu_stats_fops = {
	.open		= percpu_stats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init init_percpu_stats_debugfs(void)
{
	debugfs_create_file("percpu_stats", 0444, NULL, NULL,
			&percpu_stats_fops);

	return 0;
}

late_initcall(init_percpu_stats_debugfs);
