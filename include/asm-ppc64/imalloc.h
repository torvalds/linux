#ifndef _PPC64_IMALLOC_H
#define _PPC64_IMALLOC_H

/*
 * Define the address range of the imalloc VM area.
 */
#define PHBS_IO_BASE  	  VMALLOC_END
#define IMALLOC_BASE      (PHBS_IO_BASE + 0x80000000ul)	/* Reserve 2 gigs for PHBs */
#define IMALLOC_END       (VMALLOC_START + PGTABLE_RANGE)


/* imalloc region types */
#define IM_REGION_UNUSED	0x1
#define IM_REGION_SUBSET	0x2
#define IM_REGION_EXISTS	0x4
#define IM_REGION_OVERLAP	0x8
#define IM_REGION_SUPERSET	0x10

extern struct vm_struct * im_get_free_area(unsigned long size);
extern struct vm_struct * im_get_area(unsigned long v_addr, unsigned long size,
				      int region_type);
extern void im_free(void *addr);

extern unsigned long ioremap_bot;

#endif /* _PPC64_IMALLOC_H */
