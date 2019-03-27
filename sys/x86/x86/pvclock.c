/*-
 * Copyright (c) 2009 Adrian Chadd
 * Copyright (c) 2012 Spectra Logic Corporation
 * Copyright (c) 2014 Bryan Venteicher
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>

#include <machine/cpufunc.h>
#include <machine/cpu.h>
#include <machine/atomic.h>
#include <machine/pvclock.h>

/*
 * Last time; this guarantees a monotonically increasing clock for when
 * a stable TSC is not provided.
 */
static volatile uint64_t pvclock_last_cycles;

void
pvclock_resume(void)
{

	atomic_store_rel_64(&pvclock_last_cycles, 0);
}

uint64_t
pvclock_get_last_cycles(void)
{

	return (atomic_load_acq_64(&pvclock_last_cycles));
}

uint64_t
pvclock_tsc_freq(struct pvclock_vcpu_time_info *ti)
{
	uint64_t freq;

	freq = (1000000000ULL << 32) / ti->tsc_to_system_mul;

	if (ti->tsc_shift < 0)
		freq <<= -ti->tsc_shift;
	else
		freq >>= ti->tsc_shift;

	return (freq);
}

/*
 * Scale a 64-bit delta by scaling and multiplying by a 32-bit fraction,
 * yielding a 64-bit result.
 */
static inline uint64_t
pvclock_scale_delta(uint64_t delta, uint32_t mul_frac, int shift)
{
	uint64_t product;

	if (shift < 0)
		delta >>= -shift;
	else
		delta <<= shift;

#if defined(__i386__)
	{
		uint32_t tmp1, tmp2;

		/**
		 * For i386, the formula looks like:
		 *
		 *   lower = (mul_frac * (delta & UINT_MAX)) >> 32
		 *   upper = mul_frac * (delta >> 32)
		 *   product = lower + upper
		 */
		__asm__ (
			"mul  %5       ; "
			"mov  %4,%%eax ; "
			"mov  %%edx,%4 ; "
			"mul  %5       ; "
			"xor  %5,%5    ; "
			"add  %4,%%eax ; "
			"adc  %5,%%edx ; "
			: "=A" (product), "=r" (tmp1), "=r" (tmp2)
			: "a" ((uint32_t)delta), "1" ((uint32_t)(delta >> 32)),
			  "2" (mul_frac) );
	}
#elif defined(__amd64__)
	{
		unsigned long tmp;

		__asm__ (
			"mulq %[mul_frac] ; shrd $32, %[hi], %[lo]"
			: [lo]"=a" (product), [hi]"=d" (tmp)
			: "0" (delta), [mul_frac]"rm"((uint64_t)mul_frac));
	}
#else
#error "pvclock: unsupported x86 architecture?"
#endif

	return (product);
}

static uint64_t
pvclock_get_nsec_offset(struct pvclock_vcpu_time_info *ti)
{
	uint64_t delta;

	delta = rdtsc() - ti->tsc_timestamp;

	return (pvclock_scale_delta(delta, ti->tsc_to_system_mul,
	    ti->tsc_shift));
}

static void
pvclock_read_time_info(struct pvclock_vcpu_time_info *ti,
    uint64_t *cycles, uint8_t *flags)
{
	uint32_t version;

	do {
		version = ti->version;
		rmb();
		*cycles = ti->system_time + pvclock_get_nsec_offset(ti);
		*flags = ti->flags;
		rmb();
	} while ((ti->version & 1) != 0 || ti->version != version);
}

static void
pvclock_read_wall_clock(struct pvclock_wall_clock *wc, uint32_t *sec,
    uint32_t *nsec)
{
	uint32_t version;

	do {
		version = wc->version;
		rmb();
		*sec = wc->sec;
		*nsec = wc->nsec;
		rmb();
	} while ((wc->version & 1) != 0 || wc->version != version);
}

uint64_t
pvclock_get_timecount(struct pvclock_vcpu_time_info *ti)
{
	uint64_t now, last;
	uint8_t flags;

	pvclock_read_time_info(ti, &now, &flags);

	if (flags & PVCLOCK_FLAG_TSC_STABLE)
		return (now);

	/*
	 * Enforce a monotonically increasing clock time across all VCPUs.
	 * If our time is too old, use the last time and return. Otherwise,
	 * try to update the last time.
	 */
	do {
		last = atomic_load_acq_64(&pvclock_last_cycles);
		if (last > now)
			return (last);
	} while (!atomic_cmpset_64(&pvclock_last_cycles, last, now));

	return (now);
}

void
pvclock_get_wallclock(struct pvclock_wall_clock *wc, struct timespec *ts)
{
	uint32_t sec, nsec;

	pvclock_read_wall_clock(wc, &sec, &nsec);
	ts->tv_sec = sec;
	ts->tv_nsec = nsec;
}
