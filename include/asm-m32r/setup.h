/*
 * This is set up by the setup-routine at boot-time
 */
#define PARAM			((unsigned char *)empty_zero_page)

#define MOUNT_ROOT_RDONLY	(*(unsigned long *) (PARAM+0x000))
#define RAMDISK_FLAGS		(*(unsigned long *) (PARAM+0x004))
#define ORIG_ROOT_DEV		(*(unsigned long *) (PARAM+0x008))
#define LOADER_TYPE		(*(unsigned long *) (PARAM+0x00c))
#define INITRD_START		(*(unsigned long *) (PARAM+0x010))
#define INITRD_SIZE		(*(unsigned long *) (PARAM+0x014))

#define M32R_CPUCLK		(*(unsigned long *) (PARAM+0x018))
#define M32R_BUSCLK		(*(unsigned long *) (PARAM+0x01c))
#define M32R_TIMER_DIVIDE	(*(unsigned long *) (PARAM+0x020))

#define COMMAND_LINE		((char *) (PARAM+0x100))

#define SCREEN_INFO		(*(struct screen_info *) (PARAM+0x200))

#define COMMAND_LINE_SIZE	(512)

#define RAMDISK_IMAGE_START_MASK	(0x07FF)
#define RAMDISK_PROMPT_FLAG		(0x8000)
#define RAMDISK_LOAD_FLAG		(0x4000)

#define PFN_UP(x)	(((x) + PAGE_SIZE-1) >> PAGE_SHIFT)
#define PFN_DOWN(x)	((x) >> PAGE_SHIFT)
#define PFN_PHYS(x)	((x) << PAGE_SHIFT)

extern unsigned long memory_start;
extern unsigned long memory_end;

