/*	$OpenBSD: powernow.c,v 1.6 2014/09/14 14:17:23 jsg Exp $	*/
/*
 * Copyright (c) 2004 Ted Unangst
 * All rights reserved.
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

/*
 * AMD K6 PowerNow driver, see AMD tech doc #23525
 * Numerous hints from Linux cpufreq driver
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/specialreg.h>
#include <machine/pio.h>

/* table 27 in amd 23535.  entries after 45 taken from linux cpufreq */
struct {
	int mult;	/* not actually used, maybe for cpuspeed */
	int magic;
} k6_multipliers[] = {
	{ 20, 4 },
	{ 30, 5 },
	{ 35, 7 },
	{ 40, 2 },
	{ 45, 0 },
	{ 55, 3 },
	{ 50, 1 },
	{ 60, 6 },
};
#define NUM_K6_ENTRIES (sizeof k6_multipliers / sizeof k6_multipliers[0])

int	k6_maxindex;	/* no setting higher than this */

/* linux says: "it doesn't matter where, as long as it is unused */
#define K6PORT 0xfff0

void
k6_powernow_init(void)
{
	uint64_t msrval;
	uint32_t portval;
	int i;

	/* on */
	msrval = K6PORT | 0x1;
	wrmsr(MSR_K6_EPMR, msrval);
	/* read */
	portval = inl(K6PORT + 8);
	/* off */
	wrmsr(MSR_K6_EPMR, 0LL);

	portval = (portval >> 5) & 7;

	for (i = 0; i < NUM_K6_ENTRIES; i++)
		if (portval == k6_multipliers[i].magic) {
			k6_maxindex = i;
			break;
		}
	if (i == NUM_K6_ENTRIES) {
		printf("bad value for current multiplier\n");
		return;
	}

	cpu_setperf = k6_powernow_setperf;
}

void
k6_powernow_setperf(int level)
{
	uint64_t msrval;
	uint32_t portval;
	int index;

	index = level * k6_maxindex / 100;

	/* on */
	msrval = K6PORT | 0x1;
	wrmsr(MSR_K6_EPMR, msrval);
	/* read and update */
	portval = inl(K6PORT + 8);
	portval &= 0xf;
	portval |= 1 << 12 | 1 << 10 | 1 << 9 |
	    k6_multipliers[index].magic << 5;
	outl(K6PORT + 8, portval);
	/* off */
	wrmsr(MSR_K6_EPMR, 0LL);
}
