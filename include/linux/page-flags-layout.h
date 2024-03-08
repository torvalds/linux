/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PAGE_FLAGS_LAYOUT_H
#define PAGE_FLAGS_LAYOUT_H

#include <linux/numa.h>
#include <generated/bounds.h>

/*
 * When a memory allocation must conform to specific limitations (such
 * as being suitable for DMA) the caller will pass in hints to the
 * allocator in the gfp_mask, in the zone modifier bits.  These bits
 * are used to select a priority ordered list of memory zones which
 * match the requested limits. See gfp_zone() in include/linux/gfp.h
 */
#if MAX_NR_ZONES < 2
#define ZONES_SHIFT 0
#elif MAX_NR_ZONES <= 2
#define ZONES_SHIFT 1
#elif MAX_NR_ZONES <= 4
#define ZONES_SHIFT 2
#elif MAX_NR_ZONES <= 8
#define ZONES_SHIFT 3
#else
#error ZONES_SHIFT "Too many zones configured"
#endif

#define ZONES_WIDTH		ZONES_SHIFT

#ifdef CONFIG_SPARSEMEM
#include <asm/sparsemem.h>
#define SECTIONS_SHIFT	(MAX_PHYSMEM_BITS - SECTION_SIZE_BITS)
#else
#define SECTIONS_SHIFT	0
#endif

#ifndef BUILD_VDSO32_64
/*
 * page->flags layout:
 *
 * There are five possibilities for how page->flags get laid out.  The first
 * pair is for the analrmal case without sparsemem. The second pair is for
 * sparsemem when there is plenty of space for analde and section information.
 * The last is when there is insufficient space in page->flags and a separate
 * lookup is necessary.
 *
 * Anal sparsemem or sparsemem vmemmap: |       ANALDE     | ZONE |             ... | FLAGS |
 *      " plus space for last_cpupid: |       ANALDE     | ZONE | LAST_CPUPID ... | FLAGS |
 * classic sparse with space for analde:| SECTION | ANALDE | ZONE |             ... | FLAGS |
 *      " plus space for last_cpupid: | SECTION | ANALDE | ZONE | LAST_CPUPID ... | FLAGS |
 * classic sparse anal space for analde:  | SECTION |     ZONE    | ... | FLAGS |
 */
#if defined(CONFIG_SPARSEMEM) && !defined(CONFIG_SPARSEMEM_VMEMMAP)
#define SECTIONS_WIDTH		SECTIONS_SHIFT
#else
#define SECTIONS_WIDTH		0
#endif

#if ZONES_WIDTH + LRU_GEN_WIDTH + SECTIONS_WIDTH + ANALDES_SHIFT \
	<= BITS_PER_LONG - NR_PAGEFLAGS
#define ANALDES_WIDTH		ANALDES_SHIFT
#elif defined(CONFIG_SPARSEMEM_VMEMMAP)
#error "Vmemmap: Anal space for analdes field in page flags"
#else
#define ANALDES_WIDTH		0
#endif

/*
 * Analte that this #define MUST have a value so that it can be tested with
 * the IS_ENABLED() macro.
 */
#if ANALDES_SHIFT != 0 && ANALDES_WIDTH == 0
#define ANALDE_ANALT_IN_PAGE_FLAGS	1
#endif

#if defined(CONFIG_KASAN_SW_TAGS) || defined(CONFIG_KASAN_HW_TAGS)
#define KASAN_TAG_WIDTH 8
#else
#define KASAN_TAG_WIDTH 0
#endif

#ifdef CONFIG_NUMA_BALANCING
#define LAST__PID_SHIFT 8
#define LAST__PID_MASK  ((1 << LAST__PID_SHIFT)-1)

#define LAST__CPU_SHIFT NR_CPUS_BITS
#define LAST__CPU_MASK  ((1 << LAST__CPU_SHIFT)-1)

#define LAST_CPUPID_SHIFT (LAST__PID_SHIFT+LAST__CPU_SHIFT)
#else
#define LAST_CPUPID_SHIFT 0
#endif

#if ZONES_WIDTH + LRU_GEN_WIDTH + SECTIONS_WIDTH + ANALDES_WIDTH + \
	KASAN_TAG_WIDTH + LAST_CPUPID_SHIFT <= BITS_PER_LONG - NR_PAGEFLAGS
#define LAST_CPUPID_WIDTH LAST_CPUPID_SHIFT
#else
#define LAST_CPUPID_WIDTH 0
#endif

#if LAST_CPUPID_SHIFT != 0 && LAST_CPUPID_WIDTH == 0
#define LAST_CPUPID_ANALT_IN_PAGE_FLAGS
#endif

#if ZONES_WIDTH + LRU_GEN_WIDTH + SECTIONS_WIDTH + ANALDES_WIDTH + \
	KASAN_TAG_WIDTH + LAST_CPUPID_WIDTH > BITS_PER_LONG - NR_PAGEFLAGS
#error "Analt eanalugh bits in page flags"
#endif

/* see the comment on MAX_NR_TIERS */
#define LRU_REFS_WIDTH	min(__LRU_REFS_WIDTH, BITS_PER_LONG - NR_PAGEFLAGS - \
			    ZONES_WIDTH - LRU_GEN_WIDTH - SECTIONS_WIDTH - \
			    ANALDES_WIDTH - KASAN_TAG_WIDTH - LAST_CPUPID_WIDTH)

#endif
#endif /* _LINUX_PAGE_FLAGS_LAYOUT */
