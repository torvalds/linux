/*	$OpenBSD: getsecs.c,v 1.5 2023/02/23 13:28:38 aoyama Exp $	*/
/*	$NetBSD: getsecs.c,v 1.1 2013/01/13 14:10:55 tsutsui Exp $	*/

/*-
 * Copyright (c) 2004 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by UCHIYAMA Yasushi.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <luna88k/stand/boot/samachdep.h>
#include <machine/board.h>
#include <luna88k/dev/timekeeper.h>

#define _DS_GET(off, data) \
	do { *chiptime = (off); (data) = (*chipdata); } while (0)
#define _DS_SET(off, data) \
	do { *chiptime = (off); *chipdata = (uint8_t)(data); } while (0)

/*
 * Convert a single byte between (unsigned) packed bcd and binary.
 * Public domain.
 */
unsigned int
bcdtobin(unsigned int bcd)
{

        return (((bcd >> 4) & 0x0f) * 10 + (bcd & 0x0f));
}

typedef struct {
	uint	Year;
	uint	Month;
	uint	Day;
	uint	Hour;
	uint	Minute;
	uint	Second;
} rtc_time;

#define MK_YEAR0	1970	/* year offset of MK */
#define DS_YEAR0	1990	/* year offset of DS */

static void
mk_gettime(rtc_time *t) {
	volatile uint32_t *mclock =
	    (volatile uint32_t *)(NVRAM_ADDR + MK_NVRAM_SPACE);
	mclock[MK_CSR] |= MK_CSR_READ << 24;
	t->Second = bcdtobin(mclock[MK_SEC]   >> 24);
	t->Minute = bcdtobin(mclock[MK_MIN]   >> 24);
	t->Hour   = bcdtobin(mclock[MK_HOUR]  >> 24);
	t->Day    = bcdtobin(mclock[MK_DOM]   >> 24);
	t->Month  = bcdtobin(mclock[MK_MONTH] >> 24);
	t->Year   = bcdtobin(mclock[MK_YEAR]  >> 24);
	mclock[MK_CSR] &= ~(MK_CSR_READ << 24);

	/* UniOS-Mach doesn't set the correct BCD year after Y2K */
	if (t->Year > 100) t->Year -= (MK_YEAR0 % 100);

	t->Year += MK_YEAR0;

	return;
}

static void
ds_gettime(rtc_time *t) {
	volatile uint8_t *chiptime = (volatile uint8_t *)NVRAM_ADDR;
	volatile uint8_t *chipdata = chiptime + 1;

	uint8_t c;

	/* specify 24hr and BCD mode */
	_DS_GET(DS_REGB, c);
	c |= DS_REGB_24HR;
	c &= ~DS_REGB_BINARY;
	_DS_SET(DS_REGB, c);

	/* update in progress; spin loop */
	for (;;) {
		*chiptime = DS_REGA;
		if ((*chipdata & DS_REGA_UIP) == 0)
			break;
	}

	*chiptime = DS_SEC;
	t->Second = bcdtobin(*chipdata);
	*chiptime = DS_MIN;
	t->Minute = bcdtobin(*chipdata);
	*chiptime = DS_HOUR;
	t->Hour   = bcdtobin(*chipdata);
	*chiptime = DS_DOM;
	t->Day    = bcdtobin(*chipdata);
	*chiptime = DS_MONTH;
	t->Month  = bcdtobin(*chipdata);
	*chiptime = DS_YEAR;
	t->Year   = bcdtobin(*chipdata);

	/* UniOS-Mach doesn't set the correct BCD year after Y2K */
	if (t->Year > 100) t->Year -= (DS_YEAR0 % 100);

	t->Year += DS_YEAR0;

	return;
}

time_t
getsecs(void)
{
	rtc_time t;
	time_t r = 0;
	int y = 0;
	const int daytab[][14] = {
	    { 0, -1, 30, 58, 89, 119, 150, 180, 211, 242, 272, 303, 333, 364 },
	    { 0, -1, 30, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
	};
#define isleap(_y) (((_y) % 4) == 0 && (((_y) % 100) != 0 || ((_y) % 400) == 0))

	if (machtype == LUNA_88K) {
		mk_gettime(&t);
	} else {
		ds_gettime(&t);
	}

	/* Calc days from UNIX epoch */
	r = (t.Year - 1970) * 365;
	for (y = 1970; y < t.Year; y++) {
		if (isleap(y))
		r++;
	}
	r += daytab[isleap(t.Year)? 1 : 0][t.Month] + t.Day;

	/* Calc secs */
	r *= 60 * 60 * 24;
	r += ((t.Hour * 60) + t.Minute) * 60 + t.Second;

	return (r);
}
