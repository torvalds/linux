/*	$OpenBSD: cacheinfo.c,v 1.15 2024/06/13 02:19:20 guenther Exp $	*/

/*
 * Copyright (c) 2022 Jonathan Gray <jsg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/cpu.h>
#include <machine/specialreg.h>

#define MAX_CACHE_LEAF	10

#ifdef MULTIPROCESSOR
uint32_t prev_cache[MAX_CACHE_LEAF][3];
# define prev_e5_ecx	prev_cache[0][0]
# define prev_e5_edx	prev_cache[0][1]
# define prev_e6_ecx	prev_cache[0][2]
# define PREV_SET(x,y)	(x) = (y)
# define PREV_SAME(x,y)	((x) == (y))
#else
# define PREV_SET(x,y)	(void)0
# define PREV_SAME(x,y)	0
#endif

void
amd64_print_l1_cacheinfo(struct cpu_info *ci)
{
	u_int ways, linesize, totalsize;
	u_int dummy, ecx, edx;

	if (ci->ci_pnfeatset < 0x80000005)
		return;

	CPUID(0x80000005, dummy, dummy, ecx, edx);

	if (!CPU_IS_PRIMARY(ci) && PREV_SAME(ecx, prev_e5_ecx) &&
	    PREV_SAME(edx, prev_e5_edx))
		return;
	PREV_SET(prev_e5_ecx, ecx);
	PREV_SET(prev_e5_edx, edx);
	if (ecx == 0)
		return;

	/* L1D */
	linesize = ecx & 0xff;
	ways = (ecx >> 16) & 0xff;
	totalsize = (ecx >> 24) & 0xff; /* KB */

	printf("%s: ", ci->ci_dev->dv_xname);

	printf("%dKB ", totalsize);
	printf("%db/line ", linesize);

	switch (ways) {
	case 0x00:
		/* reserved */
		break;
	case 0x01:
		printf("direct-mapped");
		break;
	case 0xff:
		printf("fully associative");
		break;
	default:
		printf("%d-way", ways);
		break;
	}
	printf(" D-cache, ");

	/* L1C */
	linesize = edx & 0xff;
	ways = (edx >> 16) & 0xff;
	totalsize = (edx >> 24) & 0xff; /* KB */

	printf("%dKB ", totalsize);
	printf("%db/line ", linesize);

	switch (ways) {
	case 0x00:
		/* reserved */
		break;
	case 0x01:
		printf("direct-mapped");
		break;
	case 0xff:
		printf("fully associative");
		break;
	default:
		printf("%d-way", ways);
		break;
	}
	printf(" I-cache\n");
}

void
amd64_print_l2_cacheinfo(struct cpu_info *ci)
{
	u_int ways, linesize, totalsize;
	u_int dummy, ecx;

	if (ci->ci_pnfeatset < 0x80000006)
		return;

	CPUID(0x80000006, dummy, dummy, ecx, dummy);

	if (!CPU_IS_PRIMARY(ci) && PREV_SAME(ecx, prev_e6_ecx))
		return;
	PREV_SET(prev_e6_ecx, ecx);
	if (ecx == 0)
		return;

	printf("%s: ", ci->ci_dev->dv_xname);

	linesize = ecx & 0xff;
	ways = (ecx >> 12) & 0x0f;
	totalsize = ((ecx >> 16) & 0xffff); /* KB */

	if (totalsize < 1024)
		printf("%dKB ", totalsize);
	else
		printf("%dMB ", totalsize >> 10);
	printf("%db/line ", linesize);

	switch (ways) {
	case 0x00:
		printf("disabled");
		break;
	case 0x01:
		printf("direct-mapped");
		break;
	case 0x02:
	case 0x04:
		printf("%d-way", ways);
		break;
	case 0x06:
		printf("8-way");
		break;
	case 0x03:	
	case 0x05:
	case 0x09:
		/* reserved */
		break;
	case 0x07:
		/* see cpuid 4 sub-leaf 2 */
		break;
	case 0x08:
		printf("16-way");
		break;
	case 0x0a:
		printf("32-way");
		break;
	case 0x0c:
		printf("64-way");
		break;
	case 0x0d:
		printf("96-way");
		break;
	case 0x0e:
		printf("128-way");
		break;
	case 0x0f:
		printf("fully associative");
	}

	printf(" L2 cache\n");
}

static inline int
intel_print_one_cache(struct cpu_info *ci, int leaf, u_int eax, u_int ebx,
    u_int ecx)
{
	u_int ways, partitions, linesize, sets, totalsize;
	int type, level;

	type = eax & 0x1f;
	if (type == 0)
		return 1;
	level = (eax >> 5) & 7;

	ways = (ebx >> 22) + 1;
	linesize = (ebx & 0xfff) + 1;
	partitions =  ((ebx >> 12) & 0x3ff) + 1;
	sets = ecx + 1;

	totalsize = ways * linesize * partitions * sets;

	if (leaf == 0)
		printf("%s: ", ci->ci_dev->dv_xname);
	else
		printf(", ");

	if (totalsize < 1024*1024)
		printf("%dKB ", totalsize >> 10);
	else
		printf("%dMB ", totalsize >> 20);
	printf("%db/line %d-way ", linesize, ways);

	if (level == 1) {
		if (type == 1)
			printf("D");
		else if (type == 2)
			printf("I");
		else if (type == 3)
			printf("U");
		printf("-cache");
	} else {
		printf("L%d cache", level);
	}
	return 0;
}

void
intel_print_cacheinfo(struct cpu_info *ci, u_int fn)
{
	int leaf;
	u_int eax, ebx, ecx, dummy;

	leaf = 0;
#ifdef MULTIPROCESSOR
	if (! CPU_IS_PRIMARY(ci)) {
		int i;
		/* find the first level that differs, if any */
		for (; leaf < MAX_CACHE_LEAF; leaf++) {
			CPUID_LEAF(fn, leaf, eax, ebx, ecx, dummy);
			if (PREV_SAME(prev_cache[leaf][0], eax) &&
			    PREV_SAME(prev_cache[leaf][1], ebx) &&
			    PREV_SAME(prev_cache[leaf][2], ecx)) {
				/* last level? */
				if ((eax & 0x1f) == 0)
					break;
				continue;
			}
			/* print lower levels that were the same */
			for (i = 0; i < leaf; i++)
				intel_print_one_cache(ci, i, prev_cache[i][0],
				    prev_cache[i][1], prev_cache[i][2]);
			/* print this (differing) level and higher levels */
			goto printit;
		}
		/* same as previous */
		return;
	}
#endif

	for (; leaf < MAX_CACHE_LEAF; leaf++) {
		CPUID_LEAF(fn, leaf, eax, ebx, ecx, dummy);
#ifdef MULTIPROCESSOR
printit:
#endif
		PREV_SET(prev_cache[leaf][0], eax);
		PREV_SET(prev_cache[leaf][1], ebx);
		PREV_SET(prev_cache[leaf][2], ecx);
		if (intel_print_one_cache(ci, leaf, eax, ebx, ecx))
			break;
	}
	printf("\n");
}

void
x86_print_cacheinfo(struct cpu_info *ci)
{
	uint64_t msr;

	if (ci->ci_vendor == CPUV_INTEL &&
	    rdmsr_safe(MSR_MISC_ENABLE, &msr) == 0 &&
	    (msr & MISC_ENABLE_LIMIT_CPUID_MAXVAL) == 0) {
		intel_print_cacheinfo(ci, 4);
		return;
	}

	if (ci->ci_vendor == CPUV_AMD &&
	    (ci->ci_efeature_ecx & CPUIDECX_TOPEXT)) {
		intel_print_cacheinfo(ci, 0x8000001d);
		return;
	}

	/* 0x80000005 / 0x80000006 */
	amd64_print_l1_cacheinfo(ci);
	amd64_print_l2_cacheinfo(ci);
}
