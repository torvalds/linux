#ifndef _ASM_X86_SETUP_H
#define _ASM_X86_SETUP_H

#define COMMAND_LINE_SIZE 2048

#ifndef __ASSEMBLY__

/* Interrupt control for vSMPowered x86_64 systems */
void vsmp_init(void);

#ifdef CONFIG_X86_VISWS
extern void visws_early_detect(void);
#else
static inline void visws_early_detect(void) { }
#endif

/*
 * Any setup quirks to be performed?
 */
extern int (*arch_time_init_quirk)(void);
extern int (*arch_pre_intr_init_quirk)(void);
extern int (*arch_intr_init_quirk)(void);
extern int (*arch_trap_init_quirk)(void);
extern char * (*arch_memory_setup_quirk)(void);
extern int (*mach_get_smp_config_quirk)(unsigned int early);
extern int (*mach_find_smp_config_quirk)(unsigned int reserve);

#ifndef CONFIG_PARAVIRT
#define paravirt_post_allocator_init()	do {} while (0)
#endif
#endif /* __ASSEMBLY__ */

#ifdef __KERNEL__

#ifdef __i386__

#include <linux/pfn.h>
/*
 * Reserved space for vmalloc and iomap - defined in asm/page.h
 */
#define MAXMEM_PFN	PFN_DOWN(MAXMEM)
#define MAX_NONPAE_PFN	(1 << 20)

#endif /* __i386__ */

#define PARAM_SIZE 4096		/* sizeof(struct boot_params) */

#define OLD_CL_MAGIC		0xA33F
#define OLD_CL_ADDRESS		0x020	/* Relative to real mode data */
#define NEW_CL_POINTER		0x228	/* Relative to real mode data */

#ifndef __ASSEMBLY__
#include <asm/bootparam.h>

#ifndef _SETUP

/*
 * This is set up by the setup-routine at boot-time
 */
extern struct boot_params boot_params;

/*
 * Do NOT EVER look at the BIOS memory size location.
 * It does not work on many machines.
 */
#define LOWMEMSIZE()	(0x9f000)

#ifdef __i386__

void __init i386_start_kernel(void);
extern void probe_roms(void);

extern unsigned long init_pg_tables_start;
extern unsigned long init_pg_tables_end;

#else
void __init x86_64_start_kernel(char *real_mode);
void __init x86_64_start_reservations(char *real_mode_data);

#endif /* __i386__ */
#endif /* _SETUP */
#endif /* __ASSEMBLY__ */
#endif  /*  __KERNEL__  */

#endif /* _ASM_X86_SETUP_H */
