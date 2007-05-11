/*
 * structures and definitions for the int 15, ax=e820 memory map
 * scheme.
 *
 * In a nutshell, arch/i386/boot/setup.S populates a scratch table
 * in the empty_zero_block that contains a list of usable address/size
 * duples.   In arch/i386/kernel/setup.c, this information is
 * transferred into the e820map, and in arch/i386/mm/init.c, that
 * new information is used to mark pages reserved or not.
 *
 */
#ifndef __E820_HEADER
#define __E820_HEADER

#define E820MAP	0x2d0		/* our map */
#define E820MAX	128		/* number of entries in E820MAP */
#define E820NR	0x1e8		/* # entries in E820MAP */

#define E820_RAM	1
#define E820_RESERVED	2
#define E820_ACPI	3
#define E820_NVS	4

#define HIGH_MEMORY	(1024*1024)

#ifndef __ASSEMBLY__

struct e820map {
    int nr_map;
    struct e820entry {
	unsigned long long addr;	/* start of memory segment */
	unsigned long long size;	/* size of memory segment */
	unsigned long type;		/* type of memory segment */
    } map[E820MAX];
};

extern struct e820map e820;

extern int e820_all_mapped(unsigned long start, unsigned long end,
			   unsigned type);
extern int e820_any_mapped(u64 start, u64 end, unsigned type);
extern void find_max_pfn(void);
extern void register_bootmem_low_pages(unsigned long max_low_pfn);
extern void e820_register_memory(void);
extern void limit_regions(unsigned long long size);
extern void print_memory_map(char *who);

#endif/*!__ASSEMBLY__*/

#endif/*__E820_HEADER*/
