/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MM_PERCPU_INTERNAL_H
#define _MM_PERCPU_INTERNAL_H

#include <linux/types.h>
#include <linux/percpu.h>

/*
 * pcpu_block_md is the metadata block struct.
 * Each chunk's bitmap is split into a number of full blocks.
 * All units are in terms of bits.
 *
 * The scan hint is the largest known contiguous area before the contig hint.
 * It is not necessarily the actual largest contig hint though.  There is an
 * invariant that the scan_hint_start > contig_hint_start iff
 * scan_hint == contig_hint.  This is necessary because when scanning forward,
 * we don't know if a new contig hint would be better than the current one.
 */
struct pcpu_block_md {
	int			scan_hint;	/* scan hint for block */
	int			scan_hint_start; /* block relative starting
						    position of the scan hint */
	int                     contig_hint;    /* contig hint for block */
	int                     contig_hint_start; /* block relative starting
						      position of the contig hint */
	int                     left_free;      /* size of free space along
						   the left side of the block */
	int                     right_free;     /* size of free space along
						   the right side of the block */
	int                     first_free;     /* block position of first free */
	int			nr_bits;	/* total bits responsible for */
};

struct pcpu_chunk {
#ifdef CONFIG_PERCPU_STATS
	int			nr_alloc;	/* # of allocations */
	size_t			max_alloc_size; /* largest allocation size */
#endif

	struct list_head	list;		/* linked to pcpu_slot lists */
	int			free_bytes;	/* free bytes in the chunk */
	struct pcpu_block_md	chunk_md;
	void			*base_addr;	/* base address of this chunk */

	unsigned long		*alloc_map;	/* allocation map */
	unsigned long		*bound_map;	/* boundary map */
	struct pcpu_block_md	*md_blocks;	/* metadata blocks */

	void			*data;		/* chunk data */
	bool			immutable;	/* no [de]population allowed */
	bool			isolated;	/* isolated from active chunk
						   slots */
	int			start_offset;	/* the overlap with the previous
						   region to have a page aligned
						   base_addr */
	int			end_offset;	/* additional area required to
						   have the region end page
						   aligned */
#ifdef CONFIG_MEMCG_KMEM
	struct obj_cgroup	**obj_cgroups;	/* vector of object cgroups */
#endif

	int			nr_pages;	/* # of pages served by this chunk */
	int			nr_populated;	/* # of populated pages */
	int                     nr_empty_pop_pages; /* # of empty populated pages */
	unsigned long		populated[];	/* populated bitmap */
};

extern spinlock_t pcpu_lock;

extern struct list_head *pcpu_chunk_lists;
extern int pcpu_nr_slots;
extern int pcpu_sidelined_slot;
extern int pcpu_to_depopulate_slot;
extern int pcpu_nr_empty_pop_pages;

extern struct pcpu_chunk *pcpu_first_chunk;
extern struct pcpu_chunk *pcpu_reserved_chunk;

/**
 * pcpu_chunk_nr_blocks - converts nr_pages to # of md_blocks
 * @chunk: chunk of interest
 *
 * This conversion is from the number of physical pages that the chunk
 * serves to the number of bitmap blocks used.
 */
static inline int pcpu_chunk_nr_blocks(struct pcpu_chunk *chunk)
{
	return chunk->nr_pages * PAGE_SIZE / PCPU_BITMAP_BLOCK_SIZE;
}

/**
 * pcpu_nr_pages_to_map_bits - converts the pages to size of bitmap
 * @pages: number of physical pages
 *
 * This conversion is from physical pages to the number of bits
 * required in the bitmap.
 */
static inline int pcpu_nr_pages_to_map_bits(int pages)
{
	return pages * PAGE_SIZE / PCPU_MIN_ALLOC_SIZE;
}

/**
 * pcpu_chunk_map_bits - helper to convert nr_pages to size of bitmap
 * @chunk: chunk of interest
 *
 * This conversion is from the number of physical pages that the chunk
 * serves to the number of bits in the bitmap.
 */
static inline int pcpu_chunk_map_bits(struct pcpu_chunk *chunk)
{
	return pcpu_nr_pages_to_map_bits(chunk->nr_pages);
}

#ifdef CONFIG_MEMCG_KMEM
/**
 * pcpu_obj_full_size - helper to calculate size of each accounted object
 * @size: size of area to allocate in bytes
 *
 * For each accounted object there is an extra space which is used to store
 * obj_cgroup membership. Charge it too.
 */
static inline size_t pcpu_obj_full_size(size_t size)
{
	size_t extra_size;

	extra_size = size / PCPU_MIN_ALLOC_SIZE * sizeof(struct obj_cgroup *);

	return size * num_possible_cpus() + extra_size;
}
#endif /* CONFIG_MEMCG_KMEM */

#ifdef CONFIG_PERCPU_STATS

#include <linux/spinlock.h>

struct percpu_stats {
	u64 nr_alloc;		/* lifetime # of allocations */
	u64 nr_dealloc;		/* lifetime # of deallocations */
	u64 nr_cur_alloc;	/* current # of allocations */
	u64 nr_max_alloc;	/* max # of live allocations */
	u32 nr_chunks;		/* current # of live chunks */
	u32 nr_max_chunks;	/* max # of live chunks */
	size_t min_alloc_size;	/* min allocation size */
	size_t max_alloc_size;	/* max allocation size */
};

extern struct percpu_stats pcpu_stats;
extern struct pcpu_alloc_info pcpu_stats_ai;

/*
 * For debug purposes. We don't care about the flexible array.
 */
static inline void pcpu_stats_save_ai(const struct pcpu_alloc_info *ai)
{
	memcpy(&pcpu_stats_ai, ai, sizeof(struct pcpu_alloc_info));

	/* initialize min_alloc_size to unit_size */
	pcpu_stats.min_alloc_size = pcpu_stats_ai.unit_size;
}

/*
 * pcpu_stats_area_alloc - increment area allocation stats
 * @chunk: the location of the area being allocated
 * @size: size of area to allocate in bytes
 *
 * CONTEXT:
 * pcpu_lock.
 */
static inline void pcpu_stats_area_alloc(struct pcpu_chunk *chunk, size_t size)
{
	lockdep_assert_held(&pcpu_lock);

	pcpu_stats.nr_alloc++;
	pcpu_stats.nr_cur_alloc++;
	pcpu_stats.nr_max_alloc =
		max(pcpu_stats.nr_max_alloc, pcpu_stats.nr_cur_alloc);
	pcpu_stats.min_alloc_size =
		min(pcpu_stats.min_alloc_size, size);
	pcpu_stats.max_alloc_size =
		max(pcpu_stats.max_alloc_size, size);

	chunk->nr_alloc++;
	chunk->max_alloc_size = max(chunk->max_alloc_size, size);
}

/*
 * pcpu_stats_area_dealloc - decrement allocation stats
 * @chunk: the location of the area being deallocated
 *
 * CONTEXT:
 * pcpu_lock.
 */
static inline void pcpu_stats_area_dealloc(struct pcpu_chunk *chunk)
{
	lockdep_assert_held(&pcpu_lock);

	pcpu_stats.nr_dealloc++;
	pcpu_stats.nr_cur_alloc--;

	chunk->nr_alloc--;
}

/*
 * pcpu_stats_chunk_alloc - increment chunk stats
 */
static inline void pcpu_stats_chunk_alloc(void)
{
	unsigned long flags;
	spin_lock_irqsave(&pcpu_lock, flags);

	pcpu_stats.nr_chunks++;
	pcpu_stats.nr_max_chunks =
		max(pcpu_stats.nr_max_chunks, pcpu_stats.nr_chunks);

	spin_unlock_irqrestore(&pcpu_lock, flags);
}

/*
 * pcpu_stats_chunk_dealloc - decrement chunk stats
 */
static inline void pcpu_stats_chunk_dealloc(void)
{
	unsigned long flags;
	spin_lock_irqsave(&pcpu_lock, flags);

	pcpu_stats.nr_chunks--;

	spin_unlock_irqrestore(&pcpu_lock, flags);
}

#else

static inline void pcpu_stats_save_ai(const struct pcpu_alloc_info *ai)
{
}

static inline void pcpu_stats_area_alloc(struct pcpu_chunk *chunk, size_t size)
{
}

static inline void pcpu_stats_area_dealloc(struct pcpu_chunk *chunk)
{
}

static inline void pcpu_stats_chunk_alloc(void)
{
}

static inline void pcpu_stats_chunk_dealloc(void)
{
}

#endif /* !CONFIG_PERCPU_STATS */

#endif
