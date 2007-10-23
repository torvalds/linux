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

#ifndef __ASSEMBLY__
extern unsigned long find_e820_area(unsigned long start, unsigned long end, 
				    unsigned size);
extern void add_memory_region(unsigned long start, unsigned long size, 
			      int type);
extern void setup_memory_region(void);
extern void contig_e820_setup(void); 
extern unsigned long e820_end_of_ram(void);
extern void e820_reserve_resources(void);
extern void e820_mark_nosave_regions(void);
extern void e820_print_map(char *who);
extern int e820_any_mapped(unsigned long start, unsigned long end, unsigned type);
extern int e820_all_mapped(unsigned long start, unsigned long end, unsigned type);
extern unsigned long e820_hole_size(unsigned long start, unsigned long end);

extern void e820_setup_gap(void);
extern void e820_register_active_regions(int nid,
				unsigned long start_pfn, unsigned long end_pfn);

extern void finish_e820_parsing(void);

extern struct e820map e820;

extern unsigned ebda_addr, ebda_size;
extern unsigned long nodemap_addr, nodemap_size;
#endif/*!__ASSEMBLY__*/

#endif/*__E820_HEADER*/
