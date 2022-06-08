/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __LINUX_INITRD_H
#define __LINUX_INITRD_H

#define INITRD_MINOR 250 /* shouldn't collide with /dev/ram* too soon ... */

/* starting block # of image */
extern int rd_image_start;

/* size of a single RAM disk */
extern unsigned long rd_size;

/* 1 if it is not an error if initrd_start < memory_start */
extern int initrd_below_start_ok;

/* free_initrd_mem always gets called with the next two as arguments.. */
extern unsigned long initrd_start, initrd_end;
extern void free_initrd_mem(unsigned long, unsigned long);

#ifdef CONFIG_BLK_DEV_INITRD
extern void __init reserve_initrd_mem(void);
extern void wait_for_initramfs(void);
#else
static inline void __init reserve_initrd_mem(void) {}
static inline void wait_for_initramfs(void) {}
#endif

extern phys_addr_t phys_initrd_start;
extern unsigned long phys_initrd_size;

extern char __initramfs_start[];
extern unsigned long __initramfs_size;

void console_on_rootfs(void);

#endif /* __LINUX_INITRD_H */
