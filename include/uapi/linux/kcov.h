/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_KCOV_IOCTLS_H
#define _LINUX_KCOV_IOCTLS_H

#include <linux/types.h>

/*
 * Argument for KCOV_REMOTE_ENABLE ioctl, see Documentation/dev-tools/kcov.rst
 * and the comment before kcov_remote_start() for usage details.
 */
struct kcov_remote_arg {
	unsigned int	trace_mode;	/* KCOV_TRACE_PC or KCOV_TRACE_CMP */
	unsigned int	area_size;	/* Length of coverage buffer in words */
	unsigned int	num_handles;	/* Size of handles array */
	__u64		common_handle;
	__u64		handles[0];
};

#define KCOV_REMOTE_MAX_HANDLES		0x100

#define KCOV_INIT_TRACE			_IOR('c', 1, unsigned long)
#define KCOV_ENABLE			_IO('c', 100)
#define KCOV_DISABLE			_IO('c', 101)
#define KCOV_REMOTE_ENABLE		_IOW('c', 102, struct kcov_remote_arg)

enum {
	/*
	 * Tracing coverage collection mode.
	 * Covered PCs are collected in a per-task buffer.
	 * In new KCOV version the mode is chosen by calling
	 * ioctl(fd, KCOV_ENABLE, mode). In older versions the mode argument
	 * was supposed to be 0 in such a call. So, for reasons of backward
	 * compatibility, we have chosen the value KCOV_TRACE_PC to be 0.
	 */
	KCOV_TRACE_PC = 0,
	/* Collecting comparison operands mode. */
	KCOV_TRACE_CMP = 1,
};

/*
 * The format for the types of collected comparisons.
 *
 * Bit 0 shows whether one of the arguments is a compile-time constant.
 * Bits 1 & 2 contain log2 of the argument size, up to 8 bytes.
 */
#define KCOV_CMP_CONST          (1 << 0)
#define KCOV_CMP_SIZE(n)        ((n) << 1)
#define KCOV_CMP_MASK           KCOV_CMP_SIZE(3)

#define KCOV_SUBSYSTEM_COMMON	(0x00ull << 56)
#define KCOV_SUBSYSTEM_USB	(0x01ull << 56)

#define KCOV_SUBSYSTEM_MASK	(0xffull << 56)
#define KCOV_INSTANCE_MASK	(0xffffffffull)

static inline __u64 kcov_remote_handle(__u64 subsys, __u64 inst)
{
	if (subsys & ~KCOV_SUBSYSTEM_MASK || inst & ~KCOV_INSTANCE_MASK)
		return 0;
	return subsys | inst;
}

#endif /* _LINUX_KCOV_IOCTLS_H */
