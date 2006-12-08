/*
 *	Just a place holder. We don't want to have to test x86 before
 *	we include stuff
 */

#ifndef _i386_SETUP_H
#define _i386_SETUP_H

#define COMMAND_LINE_SIZE 256

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
/*
 * This is set up by the setup-routine at boot-time
 */
extern unsigned char boot_params[PARAM_SIZE];

#define PARAM	(boot_params)
#define SCREEN_INFO (*(struct screen_info *) (PARAM+0))
#define EXT_MEM_K (*(unsigned short *) (PARAM+2))
#define ALT_MEM_K (*(unsigned long *) (PARAM+0x1e0))
#define E820_MAP_NR (*(char*) (PARAM+E820NR))
#define E820_MAP    ((struct e820entry *) (PARAM+E820MAP))
#define APM_BIOS_INFO (*(struct apm_bios_info *) (PARAM+0x40))
#define IST_INFO   (*(struct ist_info *) (PARAM+0x60))
#define DRIVE_INFO (*(struct drive_info_struct *) (PARAM+0x80))
#define SYS_DESC_TABLE (*(struct sys_desc_table_struct*)(PARAM+0xa0))
#define EFI_SYSTAB ((efi_system_table_t *) *((unsigned long *)(PARAM+0x1c4)))
#define EFI_MEMDESC_SIZE (*((unsigned long *) (PARAM+0x1c8)))
#define EFI_MEMDESC_VERSION (*((unsigned long *) (PARAM+0x1cc)))
#define EFI_MEMMAP ((void *) *((unsigned long *)(PARAM+0x1d0)))
#define EFI_MEMMAP_SIZE (*((unsigned long *) (PARAM+0x1d4)))
#define MOUNT_ROOT_RDONLY (*(unsigned short *) (PARAM+0x1F2))
#define RAMDISK_FLAGS (*(unsigned short *) (PARAM+0x1F8))
#define VIDEO_MODE (*(unsigned short *) (PARAM+0x1FA))
#define ORIG_ROOT_DEV (*(unsigned short *) (PARAM+0x1FC))
#define AUX_DEVICE_INFO (*(unsigned char *) (PARAM+0x1FF))
#define LOADER_TYPE (*(unsigned char *) (PARAM+0x210))
#define KERNEL_START (*(unsigned long *) (PARAM+0x214))
#define INITRD_START (*(unsigned long *) (PARAM+0x218))
#define INITRD_SIZE (*(unsigned long *) (PARAM+0x21c))
#define EDID_INFO   (*(struct edid_info *) (PARAM+0x140))
#define EDD_NR     (*(unsigned char *) (PARAM+EDDNR))
#define EDD_MBR_SIG_NR (*(unsigned char *) (PARAM+EDD_MBR_SIG_NR_BUF))
#define EDD_MBR_SIGNATURE ((unsigned int *) (PARAM+EDD_MBR_SIG_BUF))
#define EDD_BUF     ((struct edd_info *) (PARAM+EDDBUF))

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

#endif /* __ASSEMBLY__ */

#endif  /*  __KERNEL__  */

#endif /* _i386_SETUP_H */
