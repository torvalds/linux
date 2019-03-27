/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Semihalf.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * From: FreeBSD: src/sys/arm/mv/kirkwood/sheevaplug.c,v 1.2 2010/06/13 13:28:53
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>

#include <machine/armreg.h>
#include <machine/bus.h>
#include <machine/cpu.h>

#include <arm/mv/mvwin.h>
#include <arm/mv/mvreg.h>
#include <arm/mv/mvvar.h>

#include <dev/ofw/openfirm.h>

#include <machine/fdt.h>

#define CPU_FREQ_FIELD(sar)	(((0x01 & (sar >> 52)) << 3) | \
				    (0x07 & (sar >> 21)))
#define FAB_FREQ_FIELD(sar)	(((0x01 & (sar >> 51)) << 4) | \
				    (0x0F & (sar >> 24)))

static uint32_t count_l2clk(void);
void armadaxp_l2_init(void);
void armadaxp_init_coher_fabric(void);
int platform_get_ncpus(void);
static uint64_t get_sar_value_armadaxp(void);

#define ARMADAXP_L2_BASE		(MV_BASE + 0x8000)
#define ARMADAXP_L2_CTRL		0x100
#define L2_ENABLE			(1 << 0)
#define ARMADAXP_L2_AUX_CTRL		0x104
#define L2_WBWT_MODE_MASK		(3 << 0)
#define L2_WBWT_MODE_PAGE		0
#define L2_WBWT_MODE_WB			1
#define L2_WBWT_MODE_WT			2
#define L2_REP_STRAT_MASK		(3 << 27)
#define L2_REP_STRAT_LSFR		(1 << 27)
#define L2_REP_STRAT_SEMIPLRU		(3 << 27)

#define ARMADAXP_L2_CNTR_CTRL		0x200
#define ARMADAXP_L2_CNTR_CONF(x)	(0x204 + (x) * 0xc)
#define ARMADAXP_L2_CNTR2_VAL_LOW	(0x208 + (x) * 0xc)
#define ARMADAXP_L2_CNTR2_VAL_HI	(0x20c + (x) * 0xc)

#define ARMADAXP_L2_INT_CAUSE		0x220

#define ARMADAXP_L2_SYNC_BARRIER	0x700
#define ARMADAXP_L2_INV_WAY		0x778
#define ARMADAXP_L2_CLEAN_WAY		0x7BC
#define ARMADAXP_L2_FLUSH_PHYS		0x7F0
#define ARMADAXP_L2_FLUSH_WAY		0x7FC

#define MV_COHERENCY_FABRIC_BASE	(MV_MBUS_BRIDGE_BASE + 0x200)
#define COHER_FABRIC_CTRL		0x00
#define COHER_FABRIC_CONF		0x04
#define COHER_FABRIC_CFU		0x28
#define COHER_FABRIC_CIB_CTRL		0x80

struct vco_freq_ratio {
	uint8_t	vco_cpu;	/* VCO to CLK0(CPU) clock ratio */
	uint8_t	vco_l2c;	/* VCO to NB(L2 cache) clock ratio */
	uint8_t	vco_hcl;	/* VCO to HCLK(DDR controller) clock ratio */
	uint8_t	vco_ddr;	/* VCO to DR(DDR memory) clock ratio */
};

static struct vco_freq_ratio freq_conf_table[] = {
/*00*/	{ 1, 1,	 4,  2 },
/*01*/	{ 1, 2,	 2,  2 },
/*02*/	{ 2, 2,	 6,  3 },
/*03*/	{ 2, 2,	 3,  3 },
/*04*/	{ 1, 2,	 3,  3 },
/*05*/	{ 1, 2,	 4,  2 },
/*06*/	{ 1, 1,	 2,  2 },
/*07*/	{ 2, 3,	 6,  6 },
/*08*/	{ 2, 3,	 5,  5 },
/*09*/	{ 1, 2,	 6,  3 },
/*10*/	{ 2, 4,	10,  5 },
/*11*/	{ 1, 3,	 6,  6 },
/*12*/	{ 1, 2,	 5,  5 },
/*13*/	{ 1, 3,	 6,  3 },
/*14*/	{ 1, 2,	 5,  5 },
/*15*/	{ 2, 2,	 5,  5 },
/*16*/	{ 1, 1,	 3,  3 },
/*17*/	{ 2, 5,	10, 10 },
/*18*/	{ 1, 3,	 8,  4 },
/*19*/	{ 1, 1,	 2,  1 },
/*20*/	{ 2, 3,	 6,  3 },
/*21*/	{ 1, 2,	 8,  4 },
/*22*/	{ 2, 5,	10,  5 }
};

static uint16_t	cpu_clock_table[] = {
    1000, 1066, 1200, 1333, 1500, 1666, 1800, 2000, 600,  667,  800,  1600,
    2133, 2200, 2400 };

static uint64_t
get_sar_value_armadaxp(void)
{
	uint32_t sar_low, sar_high;

	sar_high = bus_space_read_4(fdtbus_bs_tag, MV_MISC_BASE,
	    SAMPLE_AT_RESET_HI);
	sar_low = bus_space_read_4(fdtbus_bs_tag, MV_MISC_BASE,
	    SAMPLE_AT_RESET_LO);
	return (((uint64_t)sar_high << 32) | sar_low);
}

uint32_t
get_tclk_armadaxp(void)
{
 	uint32_t cputype;

	cputype = cp15_midr_get();
	cputype &= CPU_ID_CPU_MASK;

	if (cputype == CPU_ID_MV88SV584X_V7)
		return (TCLK_250MHZ);
	else
		return (TCLK_200MHZ);
}

uint32_t
get_cpu_freq_armadaxp(void)
{

	return (0);
}

static uint32_t
count_l2clk(void)
{
	uint64_t sar_reg;
	uint32_t freq_vco, freq_l2clk;
	uint8_t  sar_cpu_freq, sar_fab_freq, array_size;

	/* Get value of the SAR register and process it */
	sar_reg = get_sar_value_armadaxp();
	sar_cpu_freq = CPU_FREQ_FIELD(sar_reg);
	sar_fab_freq = FAB_FREQ_FIELD(sar_reg);

	/* Check if CPU frequency field has correct value */
	array_size = nitems(cpu_clock_table);
	if (sar_cpu_freq >= array_size)
		panic("Reserved value in cpu frequency configuration field: "
		    "%d", sar_cpu_freq);

	/* Check if fabric frequency field has correct value */
	array_size = nitems(freq_conf_table);
	if (sar_fab_freq >= array_size)
		panic("Reserved value in fabric frequency configuration field: "
		    "%d", sar_fab_freq);

	/* Get CPU clock frequency */
	freq_vco = cpu_clock_table[sar_cpu_freq] *
	    freq_conf_table[sar_fab_freq].vco_cpu;

	/* Get L2CLK clock frequency */
	freq_l2clk = freq_vco / freq_conf_table[sar_fab_freq].vco_l2c;

	/* Round L2CLK value to integer MHz */
	if (((freq_vco % freq_conf_table[sar_fab_freq].vco_l2c) * 10 /
	    freq_conf_table[sar_fab_freq].vco_l2c) >= 5)
		freq_l2clk++;

	return (freq_l2clk * 1000000);
}

uint32_t
get_l2clk(void)
{
	static uint32_t	l2clk_freq = 0;

	/* If get_l2clk is called first time get L2CLK value from register */
	if (l2clk_freq == 0)
		l2clk_freq = count_l2clk();

	return (l2clk_freq);
}

static uint32_t
read_coher_fabric(uint32_t reg)
{

	return (bus_space_read_4(fdtbus_bs_tag, MV_COHERENCY_FABRIC_BASE, reg));
}

static void
write_coher_fabric(uint32_t reg, uint32_t val)
{

	bus_space_write_4(fdtbus_bs_tag, MV_COHERENCY_FABRIC_BASE, reg, val);
}

int
platform_get_ncpus(void)
{
#if !defined(SMP)
	return (1);
#else
	return ((read_coher_fabric(COHER_FABRIC_CONF) & 0xf) + 1);
#endif
}

void
armadaxp_init_coher_fabric(void)
{
	uint32_t val, cpus, mask;

	cpus = platform_get_ncpus();
	mask = (1 << cpus) - 1;
	val = read_coher_fabric(COHER_FABRIC_CTRL);
	val |= (mask << 24);
	write_coher_fabric(COHER_FABRIC_CTRL, val);

	val = read_coher_fabric(COHER_FABRIC_CONF);
	val |= (mask << 24);
	val |= (1 << 15);
	write_coher_fabric(COHER_FABRIC_CONF, val);
}

#define ALL_WAYS	0xffffffff

/* L2 cache configuration registers */
static uint32_t
read_l2_cache(uint32_t reg)
{

	return (bus_space_read_4(fdtbus_bs_tag, ARMADAXP_L2_BASE, reg));
}

static void
write_l2_cache(uint32_t reg, uint32_t val)
{

	bus_space_write_4(fdtbus_bs_tag, ARMADAXP_L2_BASE, reg, val);
}

static void
armadaxp_l2_idcache_inv_all(void)
{
	write_l2_cache(ARMADAXP_L2_INV_WAY, ALL_WAYS);
}

void
armadaxp_l2_init(void)
{
	u_int32_t reg;

	/* Set L2 policy */
	reg = read_l2_cache(ARMADAXP_L2_AUX_CTRL);
	reg &= ~(L2_WBWT_MODE_MASK);
	reg &= ~(L2_REP_STRAT_MASK);
	reg |= L2_REP_STRAT_SEMIPLRU;
	reg |= L2_WBWT_MODE_WT;
	write_l2_cache(ARMADAXP_L2_AUX_CTRL, reg);

	/* Invalidate l2 cache */
	armadaxp_l2_idcache_inv_all();

	/* Clear pending L2 interrupts */
	write_l2_cache(ARMADAXP_L2_INT_CAUSE, 0x1ff);

	/* Enable l2 cache */
	reg = read_l2_cache(ARMADAXP_L2_CTRL);
	write_l2_cache(ARMADAXP_L2_CTRL, reg | L2_ENABLE);

	/*
	 * For debug purposes
	 * Configure and enable counter
	 */
	write_l2_cache(ARMADAXP_L2_CNTR_CONF(0), 0xf0000 | (4 << 2));
	write_l2_cache(ARMADAXP_L2_CNTR_CONF(1), 0xf0000 | (2 << 2));
	write_l2_cache(ARMADAXP_L2_CNTR_CTRL, 0x303);

	/*
	 * Enable Cache maintenance operation propagation in coherency fabric
	 * Change point of coherency and point of unification to DRAM.
	 */
	reg = read_coher_fabric(COHER_FABRIC_CFU);
	reg |= (1 << 17) | (1 << 18);
	write_coher_fabric(COHER_FABRIC_CFU, reg);

	/* Coherent IO Bridge initialization */
	reg = read_coher_fabric(COHER_FABRIC_CIB_CTRL);
	reg &= ~(7 << 16);
	reg |= (7 << 16);
	write_coher_fabric(COHER_FABRIC_CIB_CTRL, reg);
}

