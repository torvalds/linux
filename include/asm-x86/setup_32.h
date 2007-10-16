/*
 *	Just a place holder. We don't want to have to test x86 before
 *	we include stuff
 */

#ifndef _i386_SETUP_H
#define _i386_SETUP_H

#define COMMAND_LINE_SIZE 2048

#ifdef __KERNEL__
#include <linux/pfn.h>

/*
 * Reserved space for vmalloc and iomap - defined in asm/page.h
 */
#define MAXMEM_PFN	PFN_DOWN(MAXMEM)
#define MAX_NONPAE_PFN	(1 << 20)

#define PARAM_SIZE 4096

#define OLD_CL_MAGIC_ADDR	0x90020
#define OLD_CL_MAGIC		0xA33F
#define OLD_CL_BASE_ADDR	0x90000
#define OLD_CL_OFFSET		0x90022
#define NEW_CL_POINTER		0x228	/* Relative to real mode data */

#ifndef __ASSEMBLY__

#include <asm/bootparam.h>

/*
 * This is set up by the setup-routine at boot-time
 */
extern struct boot_params boot_params;

/*
 * Do NOT EVER look at the BIOS memory size location.
 * It does not work on many machines.
 */
#define LOWMEMSIZE()	(0x9f000)

struct e820entry;

char * __init machine_specific_memory_setup(void);
char *memory_setup(void);

int __init copy_e820_map(struct e820entry * biosmap, int nr_map);
int __init sanitize_e820_map(struct e820entry * biosmap, char * pnr_map);
void __init add_memory_region(unsigned long long start,
			      unsigned long long size, int type);

extern unsigned long init_pg_tables_end;

#ifndef CONFIG_PARAVIRT
#define paravirt_post_allocator_init()	do {} while (0)
#endif

#endif /* __ASSEMBLY__ */

#endif  /*  __KERNEL__  */

#endif /* _i386_SETUP_H */
