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
extern void setup_memory_region(void);
extern void contig_e820_setup(void);
extern int e820_any_non_reserved(unsigned long start, unsigned long end);
extern int is_memory_any_valid(unsigned long start, unsigned long end);
extern int e820_all_non_reserved(unsigned long start, unsigned long end);
extern int is_memory_all_valid(unsigned long start, unsigned long end);

#endif/*!__ASSEMBLY__*/

#endif/*__E820_HEADER*/
