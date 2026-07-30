/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_X86_CPU_H
#define LINUX_DEVICE_ID_X86_CPU_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef unsigned long kernel_ulong_t;
#endif

/* Wild cards for x86_cpu_id::vendor, family, model and feature */
#define X86_VENDOR_ANY 0xffff
#define X86_FAMILY_ANY 0
#define X86_MODEL_ANY  0
#define X86_STEPPING_ANY 0
#define X86_STEP_MIN 0
#define X86_STEP_MAX 0xf
#define X86_PLATFORM_ANY 0x0
#define X86_FEATURE_ANY 0	/* Same as FPU, you can't test for that */
#define X86_CPU_TYPE_ANY 0

/*
 * Match x86 CPUs for CPU specific drivers.
 * See documentation of "x86_match_cpu" for details.
 */

/*
 * MODULE_DEVICE_TABLE expects this struct to be called x86cpu_device_id.
 * Although gcc seems to ignore this error, clang fails without this define.
 */
#define x86cpu_device_id x86_cpu_id
struct x86_cpu_id {
	__u16 vendor;
	__u16 family;
	__u16 model;
	__u16 steppings;
	__u16 feature;	/* bit index */
	/* Solely for kernel-internal use: DO NOT EXPORT to userspace! */
	__u16 flags;
	__u8  platform_mask;
	__u8  type;
	kernel_ulong_t driver_data;
};

#endif /* ifndef LINUX_DEVICE_ID_X86_CPU_H */
