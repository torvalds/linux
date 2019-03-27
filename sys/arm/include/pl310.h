/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Olivier Houchard.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/*
 * $FreeBSD$
 */

#ifndef PL310_H_
#define PL310_H_

/**
 *	PL310 - L2 Cache Controller register offsets.
 *
 */
#define PL310_CACHE_ID			0x000
#define 	CACHE_ID_RELEASE_SHIFT		0
#define 	CACHE_ID_RELEASE_MASK		0x3f
#define 	CACHE_ID_RELEASE_r0p0		0x00
#define 	CACHE_ID_RELEASE_r1p0		0x02
#define 	CACHE_ID_RELEASE_r2p0		0x04
#define 	CACHE_ID_RELEASE_r3p0		0x05
#define 	CACHE_ID_RELEASE_r3p1		0x06
#define 	CACHE_ID_RELEASE_r3p2		0x08
#define 	CACHE_ID_RELEASE_r3p3		0x09
#define 	CACHE_ID_PARTNUM_SHIFT		6
#define 	CACHE_ID_PARTNUM_MASK		0xf
#define 	CACHE_ID_PARTNUM_VALUE		0x3
#define PL310_CACHE_TYPE		0x004
#define PL310_CTRL			0x100
#define		CTRL_ENABLED			0x01
#define		CTRL_DISABLED			0x00
#define PL310_AUX_CTRL			0x104
#define 	AUX_CTRL_MASK			0xc0000fff
#define 	AUX_CTRL_ASSOCIATIVITY_SHIFT	16
#define 	AUX_CTRL_WAY_SIZE_SHIFT		17
#define 	AUX_CTRL_WAY_SIZE_MASK		(0x7 << 17)
#define 	AUX_CTRL_SHARE_OVERRIDE		(1 << 22)
#define 	AUX_CTRL_NS_LOCKDOWN		(1 << 26)
#define 	AUX_CTRL_NS_INT_CTRL		(1 << 27)
#define 	AUX_CTRL_DATA_PREFETCH		(1 << 28)
#define 	AUX_CTRL_INSTR_PREFETCH		(1 << 29)
#define 	AUX_CTRL_EARLY_BRESP		(1 << 30)
#define PL310_TAG_RAM_CTRL			0x108
#define PL310_DATA_RAM_CTRL			0x10C
#define		RAM_CTRL_WRITE_SHIFT		8
#define		RAM_CTRL_WRITE_MASK		(0x7 << 8)
#define		RAM_CTRL_READ_SHIFT		4
#define		RAM_CTRL_READ_MASK		(0x7 << 4)
#define		RAM_CTRL_SETUP_SHIFT		0
#define		RAM_CTRL_SETUP_MASK		(0x7 << 0)
#define PL310_EVENT_COUNTER_CTRL	0x200
#define		EVENT_COUNTER_CTRL_ENABLED	(1 << 0)
#define		EVENT_COUNTER_CTRL_C0_RESET	(1 << 1)
#define		EVENT_COUNTER_CTRL_C1_RESET	(1 << 2)
#define PL310_EVENT_COUNTER1_CONF	0x204
#define PL310_EVENT_COUNTER0_CONF	0x208
#define		EVENT_COUNTER_CONF_NOINTR	0
#define		EVENT_COUNTER_CONF_INCR		1
#define		EVENT_COUNTER_CONF_OVFW		2
#define		EVENT_COUNTER_CONF_NOEV		(0 << 2)
#define		EVENT_COUNTER_CONF_CO		(1 << 2)
#define		EVENT_COUNTER_CONF_DRHIT	(2 << 2)
#define		EVENT_COUNTER_CONF_DRREQ	(3 << 2)
#define		EVENT_COUNTER_CONF_DWHIT	(4 << 2)
#define		EVENT_COUNTER_CONF_DWREQ	(5 << 2)
#define		EVENT_COUNTER_CONF_DWTREQ	(6 << 2)
#define		EVENT_COUNTER_CONF_DIRHIT	(7 << 2)
#define		EVENT_COUNTER_CONF_DIRREQ	(8 << 2)
#define		EVENT_COUNTER_CONF_WA		(9 << 2)
#define PL310_EVENT_COUNTER1_VAL	0x20C
#define PL310_EVENT_COUNTER0_VAL	0x210
#define PL310_INTR_MASK			0x214
#define PL310_MASKED_INTR_STAT		0x218
#define PL310_RAW_INTR_STAT		0x21C
#define PL310_INTR_CLEAR		0x220
#define		INTR_MASK_ALL			((1 << 9) - 1)
#define		INTR_MASK_ECNTR			(1 << 0)
#define		INTR_MASK_PARRT			(1 << 1)
#define		INTR_MASK_PARRD			(1 << 2)
#define		INTR_MASK_ERRWT			(1 << 3)
#define		INTR_MASK_ERRWD			(1 << 4)
#define		INTR_MASK_ERRRT			(1 << 5)
#define		INTR_MASK_ERRRD			(1 << 6)
#define		INTR_MASK_SLVERR		(1 << 7)
#define		INTR_MASK_DECERR		(1 << 8)
#define PL310_CACHE_SYNC		0x730
#define PL310_INV_LINE_PA		0x770
#define PL310_INV_WAY			0x77C
#define PL310_CLEAN_LINE_PA		0x7B0
#define PL310_CLEAN_LINE_IDX		0x7B8
#define PL310_CLEAN_WAY			0x7BC
#define PL310_CLEAN_INV_LINE_PA		0x7F0
#define PL310_CLEAN_INV_LINE_IDX	0x7F8
#define PL310_CLEAN_INV_WAY		0x7FC
#define PL310_LOCKDOWN_D_WAY(x)		(0x900 + ((x) * 8))
#define PL310_LOCKDOWN_I_WAY(x)		(0x904 + ((x) * 8))
#define PL310_LOCKDOWN_LINE_ENABLE	0x950
#define PL310_UNLOCK_ALL_LINES_WAY	0x954
#define PL310_ADDR_FILTER_STAR		0xC00
#define PL310_ADDR_FILTER_END		0xC04
#define PL310_DEBUG_CTRL		0xF40
#define		DEBUG_CTRL_DISABLE_LINEFILL	(1 << 0)
#define		DEBUG_CTRL_DISABLE_WRITEBACK	(1 << 1)
#define		DEBUG_CTRL_SPNIDEN		(1 << 2)
#define PL310_PREFETCH_CTRL		0xF60
#define		PREFETCH_CTRL_OFFSET_MASK	(0x1f)
#define		PREFETCH_CTRL_NOTSAMEID		(1 << 21)
#define		PREFETCH_CTRL_INCR_DL		(1 << 23)
#define		PREFETCH_CTRL_PREFETCH_DROP	(1 << 24)
#define		PREFETCH_CTRL_DL_ON_WRAP	(1 << 27)
#define		PREFETCH_CTRL_DATA_PREFETCH	(1 << 28)
#define		PREFETCH_CTRL_INSTR_PREFETCH	(1 << 29)
#define		PREFETCH_CTRL_DL		(1 << 30)
#define PL310_POWER_CTRL		0xF80
#define		POWER_CTRL_ENABLE_GATING	(1 << 1)
#define		POWER_CTRL_ENABLE_STANDBY	(1 << 0)

struct intr_config_hook;

struct pl310_softc {
	device_t	sc_dev;
	struct resource *sc_mem_res;
	struct resource *sc_irq_res;
	void*		sc_irq_h;
	int		sc_enabled;
	struct mtx	sc_mtx;
	u_int		sc_rtl_revision;
	struct intr_config_hook *sc_ich;
	boolean_t	sc_io_coherent;
};

/**
 *	pl310_read4 - read a 32-bit value from the PL310 registers
 *	pl310_write4 - write a 32-bit value from the PL310 registers
 *	@off: byte offset within the register set to read from
 *	@val: the value to write into the register
 *
 *
 *	LOCKING:
 *	None
 *
 *	RETURNS:
 *	nothing in case of write function, if read function returns the value read.
 */
static __inline uint32_t
pl310_read4(struct pl310_softc *sc, bus_size_t off)
{

	return bus_read_4(sc->sc_mem_res, off);
}

static __inline void
pl310_write4(struct pl310_softc *sc, bus_size_t off, uint32_t val)
{

	bus_write_4(sc->sc_mem_res, off, val);
}

void pl310_set_ram_latency(struct pl310_softc *sc, uint32_t which_reg,
    uint32_t read, uint32_t write, uint32_t setup);

#ifndef PLATFORM
void platform_pl310_init(struct pl310_softc *);
void platform_pl310_write_ctrl(struct pl310_softc *, uint32_t);
void platform_pl310_write_debug(struct pl310_softc *, uint32_t);
#endif

#endif /* PL310_H_ */
