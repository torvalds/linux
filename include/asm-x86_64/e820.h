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

#include <linux/mmzone.h>

#define E820MAP	0x2d0		/* our map */
#define E820MAX	128		/* number of entries in E820MAP */
#define E820NR	0x1e8		/* # entries in E820MAP */

#define E820_RAM	1
#define E820_RESERVED	2
#define E820_ACPI	3
#define E820_NVS	4

#ifndef __ASSEMBLY__
struct e820entry {
	u64 addr;	/* start of memory segment */
	u64 size;	/* size of memory segment */
	u32 type;	/* type of memory segment */
} __attribute__((packed));

struct e820map {
    int nr_map;
	struct e820entry map[E820MAX];
};

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

extern void e820_setup_gap(void);
extern void e820_register_active_regions(int nid,
				unsigned long start_pfn, unsigned long end_pfn);

extern void finish_e820_parsing(void);

extern struct e820map e820;

extern unsigned ebda_addr, ebda_size;
#endif/*!__ASSEMBLY__*/

#endif/*__E820_HEADER*/
