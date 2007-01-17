#ifndef __ASM_SH64_SETUP_H
#define __ASM_SH64_SETUP_H

#define COMMAND_LINE_SIZE 256

#ifdef __KERNEL__

#define PARAM ((unsigned char *)empty_zero_page)
#define MOUNT_ROOT_RDONLY (*(unsigned long *) (PARAM+0x000))
#define RAMDISK_FLAGS (*(unsigned long *) (PARAM+0x004))
#define ORIG_ROOT_DEV (*(unsigned long *) (PARAM+0x008))
#define LOADER_TYPE (*(unsigned long *) (PARAM+0x00c))
#define INITRD_START (*(unsigned long *) (PARAM+0x010))
#define INITRD_SIZE (*(unsigned long *) (PARAM+0x014))

#define COMMAND_LINE ((char *) (PARAM+256))
#define COMMAND_LINE_SIZE 256

#endif  /*  __KERNEL__  */

#endif /* __ASM_SH64_SETUP_H */

