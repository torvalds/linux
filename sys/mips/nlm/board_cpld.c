/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>

#include <mips/nlm/hal/mips-extns.h>
#include <mips/nlm/hal/haldefs.h>
#include <mips/nlm/hal/iomap.h>
#include <mips/nlm/hal/gbu.h>

#include <mips/nlm/board.h>

#define CPLD_REVISION		0x0
#define CPLD_RESET		0x1
#define CPLD_CTRL		0x2
#define CPLD_RSVD		0x3
#define CPLD_PWR_CTRL		0x4
#define CPLD_MISC		0x5
#define CPLD_CTRL_STATUS	0x6
#define CPLD_PWR_INTR_STATUS	0x7
#define CPLD_DATA		0x8

static __inline
int nlm_cpld_read(uint64_t base, int reg)
{
	uint16_t val;

	val = *(volatile uint16_t *)(long)(base + reg * 2);
	return le16toh(val);
}

static __inline void
nlm_cpld_write(uint64_t base, int reg, uint16_t data)
{
	data = htole16(data);
	*(volatile uint16_t *)(long)(base + reg * 2) = data;
}

int
nlm_board_cpld_majorversion(uint64_t base)
{
	return (nlm_cpld_read(base, CPLD_REVISION) >> 8);
}

int
nlm_board_cpld_minorversion(uint64_t base)
{
	return (nlm_cpld_read(base, CPLD_REVISION) & 0xff);
}

uint64_t nlm_board_cpld_base(int node, int chipselect)
{
	uint64_t gbubase, cpld_phys;

	gbubase = nlm_get_gbu_regbase(node);
	cpld_phys = nlm_read_gbu_reg(gbubase, GBU_CS_BASEADDR(chipselect));
	return (MIPS_PHYS_TO_KSEG1(cpld_phys << 8));
}

void
nlm_board_cpld_reset(uint64_t base)
{

	nlm_cpld_write(base, CPLD_RESET, 1 << 15);
	for(;;)
		__asm __volatile("wait");
}

/* get daughter board type */
int
nlm_board_cpld_dboard_type(uint64_t base, int slot)
{
	uint16_t val;
	int shift = 0;

	switch (slot) {
	case 0: shift = 0; break;
	case 1: shift = 4; break;
	case 2: shift = 2; break;
	case 3: shift = 6; break;
	}
	val = nlm_cpld_read(base, CPLD_CTRL_STATUS) >> shift;
	return (val & 0x3);
}
