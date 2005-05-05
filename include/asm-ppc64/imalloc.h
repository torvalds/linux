#ifndef _PPC64_IMALLOC_H
#define _PPC64_IMALLOC_H

/*
 * Define the address range of the imalloc VM area.
 */
#define PHBS_IO_BASE  	  IOREGIONBASE
#define IMALLOC_BASE      (IOREGIONBASE + 0x80000000ul)	/* Reserve 2 gigs for PHBs */
#define IMALLOC_END       (IOREGIONBASE + EADDR_MASK)


/* imalloc region types */
#define IM_REGION_UNUSED	0x1
#define IM_REGION_SUBSET	0x2
#define IM_REGION_EXISTS	0x4
#define IM_REGION_OVERLAP	0x8
#define IM_REGION_SUPERSET	0x10

extern struct vm_struct * im_get_free_area(unsigned long size);
extern struct vm_struct * im_get_area(unsigned long v_addr, unsigned long size,
			int region_type);
unsigned long im_free(void *addr);

#endif /* _PPC64_IMALLOC_H */
