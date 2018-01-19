/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __ASM_GENERIC_IPCBUF_H
#define __ASM_GENERIC_IPCBUF_H

/*
 * The generic ipc64_perm structure:
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * ipc64_perm was originally meant to be architecture specific, but
 * everyone just ended up making identical copies without specific
 * optimizations, so we may just as well all use the same one.
 *
 * Pad space is left for:
 * - 32-bit mode_t on architectures that only had 16 bit
 * - 32-bit seq
 * - 2 miscellaneous 32-bit values
 */

struct ipc64_perm {
	__kernel_key_t		key;
	__kernel_uid32_t	uid;
	__kernel_gid32_t	gid;
	__kernel_uid32_t	cuid;
	__kernel_gid32_t	cgid;
	__kernel_mode_t		mode;
				/* pad if mode_t is u16: */
	unsigned char		__pad1[4 - sizeof(__kernel_mode_t)];
	unsigned short		seq;
	unsigned short		__pad2;
	__kernel_ulong_t	__unused1;
	__kernel_ulong_t	__unused2;
};

#endif /* __ASM_GENERIC_IPCBUF_H */
