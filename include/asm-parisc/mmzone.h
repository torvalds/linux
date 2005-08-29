#ifndef _PARISC_MMZONE_H
#define _PARISC_MMZONE_H

#ifdef CONFIG_DISCONTIGMEM

#define MAX_PHYSMEM_RANGES 8 /* Fix the size for now (current known max is 3) */
extern int npmem_ranges;

struct node_map_data {
    pg_data_t pg_data;
};

extern struct node_map_data node_data[];

#define NODE_DATA(nid)          (&node_data[nid].pg_data)

/*
 * Given a kernel address, find the home node of the underlying memory.
 */
#define kvaddr_to_nid(kaddr)	pfn_to_nid(__pa(kaddr) >> PAGE_SHIFT)

#define node_start_pfn(nid)	(NODE_DATA(nid)->node_start_pfn)
#define node_end_pfn(nid)						\
({									\
	pg_data_t *__pgdat = NODE_DATA(nid);				\
	__pgdat->node_start_pfn + __pgdat->node_spanned_pages;		\
})
#define node_localnr(pfn, nid)		((pfn) - node_start_pfn(nid))

#define local_mapnr(kvaddr)						\
({									\
	unsigned long __pfn = __pa(kvaddr) >> PAGE_SHIFT;		\
	(__pfn - node_start_pfn(pfn_to_nid(__pfn)));			\
})

#define pfn_to_page(pfn)						\
({									\
	unsigned long __pfn = (pfn);					\
	int __node  = pfn_to_nid(__pfn);				\
	&NODE_DATA(__node)->node_mem_map[node_localnr(__pfn,__node)];	\
})

#define page_to_pfn(pg)							\
({									\
	struct page *__page = pg;					\
	struct zone *__zone = page_zone(__page);			\
	BUG_ON(__zone == NULL);						\
	(unsigned long)(__page - __zone->zone_mem_map)			\
		+ __zone->zone_start_pfn;				\
})

/* We have these possible memory map layouts:
 * Astro: 0-3.75, 67.75-68, 4-64
 * zx1: 0-1, 257-260, 4-256
 * Stretch (N-class): 0-2, 4-32, 34-xxx
 */

/* Since each 1GB can only belong to one region (node), we can create
 * an index table for pfn to nid lookup; each entry in pfnnid_map 
 * represents 1GB, and contains the node that the memory belongs to. */

#define PFNNID_SHIFT (30 - PAGE_SHIFT)
#define PFNNID_MAP_MAX  512     /* support 512GB */
extern unsigned char pfnnid_map[PFNNID_MAP_MAX];

#ifndef __LP64__
#define pfn_is_io(pfn) ((pfn & (0xf0000000UL >> PAGE_SHIFT)) == (0xf0000000UL >> PAGE_SHIFT))
#else
/* io can be 0xf0f0f0f0f0xxxxxx or 0xfffffffff0000000 */
#define pfn_is_io(pfn) ((pfn & (0xf000000000000000UL >> PAGE_SHIFT)) == (0xf000000000000000UL >> PAGE_SHIFT))
#endif

static inline int pfn_to_nid(unsigned long pfn)
{
	unsigned int i;
	unsigned char r;

	if (unlikely(pfn_is_io(pfn)))
		return 0;

	i = pfn >> PFNNID_SHIFT;
	BUG_ON(i >= sizeof(pfnnid_map) / sizeof(pfnnid_map[0]));
	r = pfnnid_map[i];
	BUG_ON(r == 0xff);

	return (int)r;
}

static inline int pfn_valid(int pfn)
{
	int nid = pfn_to_nid(pfn);

	if (nid >= 0)
		return (pfn < node_end_pfn(nid));
	return 0;
}

#else /* !CONFIG_DISCONTIGMEM */
#define MAX_PHYSMEM_RANGES 	1 
#endif
#endif /* _PARISC_MMZONE_H */
