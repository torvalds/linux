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
#define E820_ACPI	3 /* usable as RAM once ACPI tables have been read */
#define E820_NVS	4

#define HIGH_MEMORY	(1024*1024)

#define LOWMEMSIZE()	(0x9f000)

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
extern void e820_print_map(char *who);
extern int e820_mapped(unsigned long start, unsigned long end, unsigned type);

extern void e820_bootmem_free(pg_data_t *pgdat, unsigned long start,unsigned long end);
extern void e820_setup_gap(void);
extern unsigned long e820_hole_size(unsigned long start_pfn,
				    unsigned long end_pfn);

extern void __init parse_memopt(char *p, char **end);
extern void __init parse_memmapopt(char *p, char **end);

extern struct e820map e820;
#endif/*!__ASSEMBLY__*/

#endif/*__E820_HEADER*/
