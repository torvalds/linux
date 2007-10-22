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

#define HIGH_MEMORY	(1024*1024)

#ifndef __ASSEMBLY__

extern struct e820map e820;

extern int e820_all_mapped(unsigned long start, unsigned long end,
			   unsigned type);
extern int e820_any_mapped(u64 start, u64 end, unsigned type);
extern void find_max_pfn(void);
extern void register_bootmem_low_pages(unsigned long max_low_pfn);
extern void e820_register_memory(void);
extern void limit_regions(unsigned long long size);
extern void print_memory_map(char *who);

#if defined(CONFIG_PM) && defined(CONFIG_HIBERNATION)
extern void e820_mark_nosave_regions(void);
#else
static inline void e820_mark_nosave_regions(void)
{
}
#endif

#endif/*!__ASSEMBLY__*/
#endif/*__E820_HEADER*/
