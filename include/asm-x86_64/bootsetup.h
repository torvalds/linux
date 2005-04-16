
#ifndef _X86_64_BOOTSETUP_H
#define _X86_64_BOOTSETUP_H 1

extern char x86_boot_params[2048];

/*
 * This is set up by the setup-routine at boot-time
 */
#define PARAM	((unsigned char *)x86_boot_params)
#define SCREEN_INFO (*(struct screen_info *) (PARAM+0))
#define EXT_MEM_K (*(unsigned short *) (PARAM+2))
#define ALT_MEM_K (*(unsigned int *) (PARAM+0x1e0))
#define E820_MAP_NR (*(char*) (PARAM+E820NR))
#define E820_MAP    ((struct e820entry *) (PARAM+E820MAP))
#define APM_BIOS_INFO (*(struct apm_bios_info *) (PARAM+0x40))
#define DRIVE_INFO (*(struct drive_info_struct *) (PARAM+0x80))
#define SYS_DESC_TABLE (*(struct sys_desc_table_struct*)(PARAM+0xa0))
#define MOUNT_ROOT_RDONLY (*(unsigned short *) (PARAM+0x1F2))
#define RAMDISK_FLAGS (*(unsigned short *) (PARAM+0x1F8))
#define SAVED_VIDEO_MODE (*(unsigned short *) (PARAM+0x1FA))
#define ORIG_ROOT_DEV (*(unsigned short *) (PARAM+0x1FC))
#define AUX_DEVICE_INFO (*(unsigned char *) (PARAM+0x1FF))
#define LOADER_TYPE (*(unsigned char *) (PARAM+0x210))
#define KERNEL_START (*(unsigned int *) (PARAM+0x214))
#define INITRD_START (*(unsigned int *) (PARAM+0x218))
#define INITRD_SIZE (*(unsigned int *) (PARAM+0x21c))
#define EDID_INFO (*(struct edid_info *) (PARAM+0x140))
#define EDD_NR     (*(unsigned char *) (PARAM+EDDNR))
#define EDD_MBR_SIG_NR (*(unsigned char *) (PARAM+EDD_MBR_SIG_NR_BUF))
#define EDD_MBR_SIGNATURE ((unsigned int *) (PARAM+EDD_MBR_SIG_BUF))
#define EDD_BUF     ((struct edd_info *) (PARAM+EDDBUF))
#define COMMAND_LINE saved_command_line

#define RAMDISK_IMAGE_START_MASK  	0x07FF
#define RAMDISK_PROMPT_FLAG		0x8000
#define RAMDISK_LOAD_FLAG		0x4000	

#endif
