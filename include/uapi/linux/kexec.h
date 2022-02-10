/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPILINUX_KEXEC_H
#define _UAPILINUX_KEXEC_H

/* kexec system call -  It loads the new kernel to boot into.
 * kexec does not sync, or unmount filesystems so if you need
 * that to happen you need to do that yourself.
 */

#include <linux/types.h>

/* kexec flags for different usage scenarios */
#define KEXEC_ON_CRASH		0x00000001
#define KEXEC_PRESERVE_CONTEXT	0x00000002
#define KEXEC_ARCH_MASK		0xffff0000

/*
 * Kexec file load interface flags.
 * KEXEC_FILE_UNLOAD : Unload already loaded kexec/kdump image.
 * KEXEC_FILE_ON_CRASH : Load/unload operation belongs to kdump image.
 * KEXEC_FILE_NO_INITRAMFS : No initramfs is being loaded. Ignore the initrd
 *                           fd field.
 */
#define KEXEC_FILE_UNLOAD	0x00000001
#define KEXEC_FILE_ON_CRASH	0x00000002
#define KEXEC_FILE_NO_INITRAMFS	0x00000004

/* These values match the ELF architecture values.
 * Unless there is a good reason that should continue to be the case.
 */
#define KEXEC_ARCH_DEFAULT ( 0 << 16)
#define KEXEC_ARCH_386     ( 3 << 16)
#define KEXEC_ARCH_68K     ( 4 << 16)
#define KEXEC_ARCH_PARISC  (15 << 16)
#define KEXEC_ARCH_X86_64  (62 << 16)
#define KEXEC_ARCH_PPC     (20 << 16)
#define KEXEC_ARCH_PPC64   (21 << 16)
#define KEXEC_ARCH_IA_64   (50 << 16)
#define KEXEC_ARCH_ARM     (40 << 16)
#define KEXEC_ARCH_S390    (22 << 16)
#define KEXEC_ARCH_SH      (42 << 16)
#define KEXEC_ARCH_MIPS_LE (10 << 16)
#define KEXEC_ARCH_MIPS    ( 8 << 16)
#define KEXEC_ARCH_AARCH64 (183 << 16)
#define KEXEC_ARCH_RISCV   (243 << 16)

/* The artificial cap on the number of segments passed to kexec_load. */
#define KEXEC_SEGMENT_MAX 16

#ifndef __KERNEL__
/*
 * This structure is used to hold the arguments that are used when
 * loading  kernel binaries.
 */
struct kexec_segment {
	const void *buf;
	__kernel_size_t bufsz;
	const void *mem;
	__kernel_size_t memsz;
};

#endif /* __KERNEL__ */

#endif /* _UAPILINUX_KEXEC_H */
