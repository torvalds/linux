#ifndef _ASM_BOOT_H
#define _ASM_BOOT_H

/* Don't touch these, unless you really know what you're doing. */
#define DEF_SYSSEG	0x1000
#define DEF_SYSSIZE	0x7F00

/* Internal svga startup constants */
#define NORMAL_VGA	0xffff		/* 80x25 mode */
#define EXTENDED_VGA	0xfffe		/* 80x50 mode */
#define ASK_VGA		0xfffd		/* ask for it at bootup */

/* Physical address where kernel should be loaded. */
#define LOAD_PHYSICAL_ADDR ((CONFIG_PHYSICAL_START \
				+ (CONFIG_PHYSICAL_ALIGN - 1)) \
				& ~(CONFIG_PHYSICAL_ALIGN - 1))

#ifdef CONFIG_X86_64
#define BOOT_HEAP_SIZE	0x7000
#define BOOT_STACK_SIZE	0x4000
#else
#define BOOT_HEAP_SIZE	0x4000
#define BOOT_STACK_SIZE	0x1000
#endif

#endif /* _ASM_BOOT_H */
