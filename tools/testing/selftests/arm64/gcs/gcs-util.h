/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 ARM Limited.
 */

#ifndef GCS_UTIL_H
#define GCS_UTIL_H

#include <stdbool.h>

#ifndef __NR_map_shadow_stack
#define __NR_map_shadow_stack 453
#endif

#ifndef __NR_prctl
#define __NR_prctl 167
#endif

#ifndef NT_ARM_GCS
#define NT_ARM_GCS 0x410

struct user_gcs {
	__u64 features_enabled;
	__u64 features_locked;
	__u64 gcspr_el0;
};
#endif

/* Shadow Stack/Guarded Control Stack interface */
#define PR_GET_SHADOW_STACK_STATUS	74
#define PR_SET_SHADOW_STACK_STATUS      75
#define PR_LOCK_SHADOW_STACK_STATUS     76

# define PR_SHADOW_STACK_ENABLE         (1UL << 0)
# define PR_SHADOW_STACK_WRITE		(1UL << 1)
# define PR_SHADOW_STACK_PUSH		(1UL << 2)

#define PR_SHADOW_STACK_ALL_MODES \
	PR_SHADOW_STACK_ENABLE | PR_SHADOW_STACK_WRITE | PR_SHADOW_STACK_PUSH

#define SHADOW_STACK_SET_TOKEN (1ULL << 0)     /* Set up a restore token in the shadow stack */
#define SHADOW_STACK_SET_MARKER (1ULL << 1)     /* Set up a top of stack merker in the shadow stack */

#define GCS_CAP_ADDR_MASK		(0xfffffffffffff000UL)
#define GCS_CAP_TOKEN_MASK		(0x0000000000000fffUL)
#define GCS_CAP_VALID_TOKEN		1
#define GCS_CAP_IN_PROGRESS_TOKEN	5

#define GCS_CAP(x) (((unsigned long)(x) & GCS_CAP_ADDR_MASK) | \
		    GCS_CAP_VALID_TOKEN)

static inline unsigned long *get_gcspr(void)
{
	unsigned long *gcspr;

	asm volatile(
		"mrs	%0, S3_3_C2_C5_1"
	: "=r" (gcspr)
	:
	: "cc");

	return gcspr;
}

static inline void __attribute__((always_inline)) gcsss1(unsigned long *Xt)
{
	asm volatile (
		"sys #3, C7, C7, #2, %0\n"
		:
		: "rZ" (Xt)
		: "memory");
}

static inline unsigned long __attribute__((always_inline)) *gcsss2(void)
{
	unsigned long *Xt;

	asm volatile(
		"SYSL %0, #3, C7, C7, #3\n"
		: "=r" (Xt)
		:
		: "memory");

	return Xt;
}

static inline bool chkfeat_gcs(void)
{
	register long val __asm__ ("x16") = 1;

	/* CHKFEAT x16 */
	asm volatile(
		"hint #0x28\n"
		: "=r" (val)
		: "r" (val));

	return val != 1;
}

#endif
