/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __VDSO_DATAPAGE_H
#define __VDSO_DATAPAGE_H

#ifndef __ASSEMBLY__

#include <linux/compiler.h>
#include <uapi/linux/time.h>
#include <uapi/linux/types.h>
#include <uapi/asm-generic/errno-base.h>

#include <vdso/align.h>
#include <vdso/bits.h>
#include <vdso/clocksource.h>
#include <vdso/ktime.h>
#include <vdso/limits.h>
#include <vdso/math64.h>
#include <vdso/page.h>
#include <vdso/processor.h>
#include <vdso/time.h>
#include <vdso/time32.h>
#include <vdso/time64.h>

#ifdef CONFIG_ARCH_HAS_VDSO_TIME_DATA
#include <asm/vdso/time_data.h>
#else
struct arch_vdso_time_data {};
#endif

#if defined(CONFIG_ARCH_HAS_VDSO_ARCH_DATA)
#include <asm/vdso/arch_data.h>
#elif defined(CONFIG_GENERIC_VDSO_DATA_STORE)
struct vdso_arch_data {
	/* Needed for the generic code, never actually used at runtime */
	char __unused;
};
#endif

#define VDSO_BASES	(CLOCK_TAI + 1)
#define VDSO_HRES	(BIT(CLOCK_REALTIME)		| \
			 BIT(CLOCK_MONOTONIC)		| \
			 BIT(CLOCK_BOOTTIME)		| \
			 BIT(CLOCK_TAI))
#define VDSO_COARSE	(BIT(CLOCK_REALTIME_COARSE)	| \
			 BIT(CLOCK_MONOTONIC_COARSE))
#define VDSO_RAW	(BIT(CLOCK_MONOTONIC_RAW))

#define CS_HRES_COARSE	0
#define CS_RAW		1
#define CS_BASES	(CS_RAW + 1)

/**
 * struct vdso_timestamp - basetime per clock_id
 * @sec:	seconds
 * @nsec:	nanoseconds
 *
 * There is one vdso_timestamp object in vvar for each vDSO-accelerated
 * clock_id. For high-resolution clocks, this encodes the time
 * corresponding to vdso_time_data.cycle_last. For coarse clocks this encodes
 * the actual time.
 *
 * To be noticed that for highres clocks nsec is left-shifted by
 * vdso_time_data[x].shift.
 */
struct vdso_timestamp {
	u64	sec;
	u64	nsec;
};

/**
 * struct vdso_time_data - vdso datapage representation
 * @seq:		timebase sequence counter
 * @clock_mode:		clock mode
 * @cycle_last:		timebase at clocksource init
 * @max_cycles:		maximum cycles which won't overflow 64bit multiplication
 * @mask:		clocksource mask
 * @mult:		clocksource multiplier
 * @shift:		clocksource shift
 * @basetime[clock_id]:	basetime per clock_id
 * @offset[clock_id]:	time namespace offset per clock_id
 * @tz_minuteswest:	minutes west of Greenwich
 * @tz_dsttime:		type of DST correction
 * @hrtimer_res:	hrtimer resolution
 * @__unused:		unused
 * @arch_data:		architecture specific data (optional, defaults
 *			to an empty struct)
 *
 * vdso_time_data will be accessed by 64 bit and compat code at the same time
 * so we should be careful before modifying this structure.
 *
 * The ordering of the struct members is optimized to have fast access to the
 * often required struct members which are related to CLOCK_REALTIME and
 * CLOCK_MONOTONIC. This information is stored in the first cache lines.
 *
 * @basetime is used to store the base time for the system wide time getter
 * VVAR page.
 *
 * @offset is used by the special time namespace VVAR pages which are
 * installed instead of the real VVAR page. These namespace pages must set
 * @seq to 1 and @clock_mode to VDSO_CLOCKMODE_TIMENS to force the code into
 * the time namespace slow path. The namespace aware functions retrieve the
 * real system wide VVAR page, read host time and add the per clock offset.
 * For clocks which are not affected by time namespace adjustment the
 * offset must be zero.
 */
struct vdso_time_data {
	u32			seq;

	s32			clock_mode;
	u64			cycle_last;
#ifdef CONFIG_GENERIC_VDSO_OVERFLOW_PROTECT
	u64			max_cycles;
#endif
	u64			mask;
	u32			mult;
	u32			shift;

	union {
		struct vdso_timestamp	basetime[VDSO_BASES];
		struct timens_offset	offset[VDSO_BASES];
	};

	s32			tz_minuteswest;
	s32			tz_dsttime;
	u32			hrtimer_res;
	u32			__unused;

	struct arch_vdso_time_data arch_data;
};

#define vdso_data vdso_time_data

/**
 * struct vdso_rng_data - vdso RNG state information
 * @generation:	counter representing the number of RNG reseeds
 * @is_ready:	boolean signaling whether the RNG is initialized
 */
struct vdso_rng_data {
	u64	generation;
	u8	is_ready;
};

/*
 * We use the hidden visibility to prevent the compiler from generating a GOT
 * relocation. Not only is going through a GOT useless (the entry couldn't and
 * must not be overridden by another library), it does not even work: the linker
 * cannot generate an absolute address to the data page.
 *
 * With the hidden visibility, the compiler simply generates a PC-relative
 * relocation, and this is what we need.
 */
#ifndef CONFIG_GENERIC_VDSO_DATA_STORE
extern struct vdso_time_data _vdso_data[CS_BASES] __attribute__((visibility("hidden")));
extern struct vdso_time_data _timens_data[CS_BASES] __attribute__((visibility("hidden")));
extern struct vdso_rng_data _vdso_rng_data __attribute__((visibility("hidden")));
#else
extern struct vdso_time_data vdso_u_time_data[CS_BASES] __attribute__((visibility("hidden")));
extern struct vdso_rng_data vdso_u_rng_data __attribute__((visibility("hidden")));
extern struct vdso_arch_data vdso_u_arch_data __attribute__((visibility("hidden")));

extern struct vdso_time_data *vdso_k_time_data;
extern struct vdso_rng_data *vdso_k_rng_data;
extern struct vdso_arch_data *vdso_k_arch_data;
#endif

/**
 * union vdso_data_store - Generic vDSO data page
 */
union vdso_data_store {
	struct vdso_time_data	data[CS_BASES];
	u8			page[1U << CONFIG_PAGE_SHIFT];
};

#ifdef CONFIG_GENERIC_VDSO_DATA_STORE

#define VDSO_ARCH_DATA_SIZE ALIGN(sizeof(struct vdso_arch_data), PAGE_SIZE)
#define VDSO_ARCH_DATA_PAGES (VDSO_ARCH_DATA_SIZE >> PAGE_SHIFT)

enum vdso_pages {
	VDSO_TIME_PAGE_OFFSET,
	VDSO_TIMENS_PAGE_OFFSET,
	VDSO_RNG_PAGE_OFFSET,
	VDSO_ARCH_PAGES_START,
	VDSO_ARCH_PAGES_END = VDSO_ARCH_PAGES_START + VDSO_ARCH_DATA_PAGES - 1,
	VDSO_NR_PAGES
};

#endif /* CONFIG_GENERIC_VDSO_DATA_STORE */

/*
 * The generic vDSO implementation requires that gettimeofday.h
 * provides:
 * - __arch_get_vdso_data(): to get the vdso datapage.
 * - __arch_get_hw_counter(): to get the hw counter based on the
 *   clock_mode.
 * - gettimeofday_fallback(): fallback for gettimeofday.
 * - clock_gettime_fallback(): fallback for clock_gettime.
 * - clock_getres_fallback(): fallback for clock_getres.
 */
#ifdef ENABLE_COMPAT_VDSO
#include <asm/vdso/compat_gettimeofday.h>
#else
#include <asm/vdso/gettimeofday.h>
#endif /* ENABLE_COMPAT_VDSO */

#else /* !__ASSEMBLY__ */

#ifdef CONFIG_VDSO_GETRANDOM
#define __vdso_u_rng_data	PROVIDE(vdso_u_rng_data = vdso_u_data + 2 * PAGE_SIZE);
#else
#define __vdso_u_rng_data
#endif

#ifdef CONFIG_ARCH_HAS_VDSO_ARCH_DATA
#define __vdso_u_arch_data	PROVIDE(vdso_u_arch_data = vdso_u_data + 3 * PAGE_SIZE);
#else
#define __vdso_u_arch_data
#endif

#define VDSO_VVAR_SYMS						\
	PROVIDE(vdso_u_data = . - __VDSO_PAGES * PAGE_SIZE);	\
	PROVIDE(vdso_u_time_data = vdso_u_data);		\
	__vdso_u_rng_data					\
	__vdso_u_arch_data					\


#endif /* !__ASSEMBLY__ */

#endif /* __VDSO_DATAPAGE_H */
