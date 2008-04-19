/*
 * structures and definitions for the int 15, ax=e820 memory map
 * scheme.
 *
 * In a nutshell, setup.S populates a scratch table in the
 * empty_zero_block that contains a list of usable address/size
 * duples.  setup.c, this information is transferred into the e820map,
 * and in init.c/numa.c, that new information is used to mark pages
 * reserved or not.
 */
#ifndef __E820_HEADER
#define __E820_HEADER

#include <linux/ioport.h>

#ifndef __ASSEMBLY__
extern unsigned long find_e820_area(unsigned long start, unsigned long end,
				    unsigned long size, unsigned long align);
extern unsigned long find_e820_area_size(unsigned long start,
					 unsigned long *sizep,
					 unsigned long align);
extern void add_memory_region(unsigned long start, unsigned long size,
			      int type);
extern void update_memory_range(u64 start, u64 size, unsigned old_type,
				unsigned new_type);
extern void setup_memory_region(void);
extern void contig_e820_setup(void);
extern unsigned long e820_end_of_ram(void);
extern void e820_reserve_resources(void);
extern void e820_mark_nosave_regions(void);
extern int e820_any_mapped(unsigned long start, unsigned long end,
			   unsigned type);
extern int e820_all_mapped(unsigned long start, unsigned long end,
			   unsigned type);
extern int e820_any_non_reserved(unsigned long start, unsigned long end);
extern int is_memory_any_valid(unsigned long start, unsigned long end);
extern int e820_all_non_reserved(unsigned long start, unsigned long end);
extern int is_memory_all_valid(unsigned long start, unsigned long end);
extern unsigned long e820_hole_size(unsigned long start, unsigned long end);

extern void e820_setup_gap(void);
extern void e820_register_active_regions(int nid, unsigned long start_pfn,
					 unsigned long end_pfn);

extern void finish_e820_parsing(void);

extern struct e820map e820;
extern void update_e820(void);

extern void reserve_early(unsigned long start, unsigned long end, char *name);
extern void early_res_to_bootmem(void);

#endif/*!__ASSEMBLY__*/

#endif/*__E820_HEADER*/
