/* SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright 2003 H. Peter Anvin - All Rights Reserved
 *
 * ----------------------------------------------------------------------- */

#ifndef LINUX_RAID_RAID6_H
#define LINUX_RAID_RAID6_H

#ifdef __KERNEL__

/* Set to 1 to use kernel-wide empty_zero_page */
#define RAID6_USE_EMPTY_ZERO_PAGE 0
#include <linux/blkdev.h>

/* We need a pre-zeroed page... if we don't want to use the kernel-provided
   one define it here */
#if RAID6_USE_EMPTY_ZERO_PAGE
# define raid6_empty_zero_page empty_zero_page
#else
extern const char raid6_empty_zero_page[PAGE_SIZE];
#endif

#else /* ! __KERNEL__ */
/* Used for testing in user space */

#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>

/* Not standard, but glibc defines it */
#define BITS_PER_LONG __WORDSIZE

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#ifndef PAGE_SIZE
# define PAGE_SIZE 4096
#endif
extern const char raid6_empty_zero_page[PAGE_SIZE];

#define __init
#define __exit
#ifndef __attribute_const__
# define __attribute_const__ __attribute__((const))
#endif
#define noinline __attribute__((noinline))

#define preempt_enable()
#define preempt_disable()
#define cpu_has_feature(x) 1
#define enable_kernel_altivec()
#define disable_kernel_altivec()

#undef	EXPORT_SYMBOL
#define EXPORT_SYMBOL(sym)
#undef	EXPORT_SYMBOL_GPL
#define EXPORT_SYMBOL_GPL(sym)
#define MODULE_LICENSE(licence)
#define MODULE_DESCRIPTION(desc)
#define subsys_initcall(x)
#define module_exit(x)

#define IS_ENABLED(x) (x)
#define CONFIG_RAID6_PQ_BENCHMARK 1
#endif /* __KERNEL__ */

/* Routine choices */
struct raid6_calls {
	void (*gen_syndrome)(int, size_t, void **);
	void (*xor_syndrome)(int, int, int, size_t, void **);
	int  (*valid)(void);	/* Returns 1 if this routine set is usable */
	const char *name;	/* Name of this routine set */
	int prefer;		/* Has special performance attribute */
};

/* Selected algorithm */
extern struct raid6_calls raid6_call;

/* Various routine sets */
extern const struct raid6_calls raid6_intx1;
extern const struct raid6_calls raid6_intx2;
extern const struct raid6_calls raid6_intx4;
extern const struct raid6_calls raid6_intx8;
extern const struct raid6_calls raid6_intx16;
extern const struct raid6_calls raid6_intx32;
extern const struct raid6_calls raid6_mmxx1;
extern const struct raid6_calls raid6_mmxx2;
extern const struct raid6_calls raid6_sse1x1;
extern const struct raid6_calls raid6_sse1x2;
extern const struct raid6_calls raid6_sse2x1;
extern const struct raid6_calls raid6_sse2x2;
extern const struct raid6_calls raid6_sse2x4;
extern const struct raid6_calls raid6_altivec1;
extern const struct raid6_calls raid6_altivec2;
extern const struct raid6_calls raid6_altivec4;
extern const struct raid6_calls raid6_altivec8;
extern const struct raid6_calls raid6_avx2x1;
extern const struct raid6_calls raid6_avx2x2;
extern const struct raid6_calls raid6_avx2x4;
extern const struct raid6_calls raid6_avx512x1;
extern const struct raid6_calls raid6_avx512x2;
extern const struct raid6_calls raid6_avx512x4;
extern const struct raid6_calls raid6_s390vx8;
extern const struct raid6_calls raid6_vpermxor1;
extern const struct raid6_calls raid6_vpermxor2;
extern const struct raid6_calls raid6_vpermxor4;
extern const struct raid6_calls raid6_vpermxor8;

struct raid6_recov_calls {
	void (*data2)(int, size_t, int, int, void **);
	void (*datap)(int, size_t, int, void **);
	int  (*valid)(void);
	const char *name;
	int priority;
};

extern const struct raid6_recov_calls raid6_recov_intx1;
extern const struct raid6_recov_calls raid6_recov_ssse3;
extern const struct raid6_recov_calls raid6_recov_avx2;
extern const struct raid6_recov_calls raid6_recov_avx512;
extern const struct raid6_recov_calls raid6_recov_s390xc;
extern const struct raid6_recov_calls raid6_recov_neon;

extern const struct raid6_calls raid6_neonx1;
extern const struct raid6_calls raid6_neonx2;
extern const struct raid6_calls raid6_neonx4;
extern const struct raid6_calls raid6_neonx8;

/* Algorithm list */
extern const struct raid6_calls * const raid6_algos[];
extern const struct raid6_recov_calls *const raid6_recov_algos[];
int raid6_select_algo(void);

/* Return values from chk_syndrome */
#define RAID6_OK	0
#define RAID6_P_BAD	1
#define RAID6_Q_BAD	2
#define RAID6_PQ_BAD	3

/* Galois field tables */
extern const u8 raid6_gfmul[256][256] __attribute__((aligned(256)));
extern const u8 raid6_vgfmul[256][32] __attribute__((aligned(256)));
extern const u8 raid6_gfexp[256]      __attribute__((aligned(256)));
extern const u8 raid6_gflog[256]      __attribute__((aligned(256)));
extern const u8 raid6_gfinv[256]      __attribute__((aligned(256)));
extern const u8 raid6_gfexi[256]      __attribute__((aligned(256)));

/* Recovery routines */
extern void (*raid6_2data_recov)(int disks, size_t bytes, int faila, int failb,
		       void **ptrs);
extern void (*raid6_datap_recov)(int disks, size_t bytes, int faila,
			void **ptrs);
void raid6_dual_recov(int disks, size_t bytes, int faila, int failb,
		      void **ptrs);

/* Some definitions to allow code to be compiled for testing in userspace */
#ifndef __KERNEL__

# define jiffies	raid6_jiffies()
# define printk 	printf
# define pr_err(format, ...) fprintf(stderr, format, ## __VA_ARGS__)
# define pr_info(format, ...) fprintf(stdout, format, ## __VA_ARGS__)
# define GFP_KERNEL	0
# define __get_free_pages(x, y)	((unsigned long)mmap(NULL, PAGE_SIZE << (y), \
						     PROT_READ|PROT_WRITE,   \
						     MAP_PRIVATE|MAP_ANONYMOUS,\
						     0, 0))
# define free_pages(x, y)	munmap((void *)(x), PAGE_SIZE << (y))

static inline void cpu_relax(void)
{
	/* Nothing */
}

#undef  HZ
#define HZ 1000
static inline uint32_t raid6_jiffies(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec*1000 + tv.tv_usec/1000;
}

#endif /* ! __KERNEL__ */

#endif /* LINUX_RAID_RAID6_H */
