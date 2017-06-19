#ifndef _MM_PERCPU_INTERNAL_H
#define _MM_PERCPU_INTERNAL_H

#include <linux/types.h>
#include <linux/percpu.h>

struct pcpu_chunk {
	struct list_head	list;		/* linked to pcpu_slot lists */
	int			free_size;	/* free bytes in the chunk */
	int			contig_hint;	/* max contiguous size hint */
	void			*base_addr;	/* base address of this chunk */

	int			map_used;	/* # of map entries used before the sentry */
	int			map_alloc;	/* # of map entries allocated */
	int			*map;		/* allocation map */
	struct list_head	map_extend_list;/* on pcpu_map_extend_chunks */

	void			*data;		/* chunk data */
	int			first_free;	/* no free below this */
	bool			immutable;	/* no [de]population allowed */
	int			nr_populated;	/* # of populated pages */
	unsigned long		populated[];	/* populated bitmap */
};

extern spinlock_t pcpu_lock;

extern struct list_head *pcpu_slot;
extern int pcpu_nr_slots;

extern struct pcpu_chunk *pcpu_first_chunk;
extern struct pcpu_chunk *pcpu_reserved_chunk;

#endif
