/*-
 * Copyright (c) 2016 Landon Fuller <landonf@FreeBSD.org>
 * Copyright (c) 2010, Broadcom Corporation.
 * All rights reserved.
 * 
 * This file is derived from the siutils.c source distributed with the
 * Asus RT-N16 firmware source code release.
 * 
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $Id: siutils.c,v 1.821.2.48 2011-02-11 20:59:28 Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/systm.h>

#include <dev/bhnd/bhnd.h>
#include <dev/bhnd/bhndb/bhndb_pcireg.h>

#include <dev/bhnd/cores/chipc/chipc.h>
#include <dev/bhnd/cores/chipc/chipcreg.h>

#include <dev/bhnd/cores/pmu/bhnd_pmuvar.h>
#include <dev/bhnd/cores/pmu/bhnd_pmureg.h>

#include "bhnd_chipc_if.h"

#include "bhnd_pwrctl_private.h"

static uint32_t	bhnd_pwrctl_factor6(uint32_t x);

/**
 * Return the factor value corresponding to a given N3M clock control magic
 * field value (CHIPC_F6_*).
 */
static uint32_t
bhnd_pwrctl_factor6(uint32_t x)
{
	switch (x) {
	case CHIPC_F6_2:	
		return (2);
	case CHIPC_F6_3:	
		return (3);
	case CHIPC_F6_4:	
		return (4);
	case CHIPC_F6_5:	
		return (5);
	case CHIPC_F6_6:
		return (6);
	case CHIPC_F6_7:
		return (7);
	default:
		return (0);
	}
}

/**
 * Return the backplane clock's chipc 'M' register offset for a given PLL type,
 * or 0 if a fixed clock speed should be used.
 *
 * @param cid Chip identification.
 * @param pll_type PLL type (CHIPC_PLL_TYPE*)
 * @param[out] fixed_hz If 0 is returned, will be set to the fixed clock
 * speed for this device.
 */
bus_size_t
bhnd_pwrctl_si_clkreg_m(const struct bhnd_chipid *cid,
    uint8_t pll_type, uint32_t *fixed_hz)
{
	switch (pll_type) {
	case CHIPC_PLL_TYPE6:
		return (CHIPC_CLKC_M3);
	case CHIPC_PLL_TYPE3:
		return (CHIPC_CLKC_M2);
	default:
		return (CHIPC_CLKC_SB);
	}
}

/**
 * Calculate the backplane clock speed (in Hz) for a given a set of clock
 * control values.
 * 
 * @param cid Chip identification.
 * @param pll_type PLL type (CHIPC_PLL_TYPE*)
 * @param n clock control N register value.
 * @param m clock control M register value.
 */
uint32_t
bhnd_pwrctl_si_clock_rate(const struct bhnd_chipid *cid,
    uint32_t pll_type, uint32_t n, uint32_t m)
{
	uint32_t rate;

	KASSERT(bhnd_pwrctl_si_clkreg_m(cid, pll_type, NULL) != 0,
	    ("can't compute clock rate on fixed clock"));

	rate = bhnd_pwrctl_clock_rate(pll_type, n, m);
	if (pll_type == CHIPC_PLL_TYPE3)
		rate /= 2;

	return (rate);
}

/**
 * Return the CPU clock's chipc 'M' register offset for a given PLL type,
 * or 0 if a fixed clock speed should be used.
 * 
 * @param cid Chip identification.
 * @param pll_type PLL type (CHIPC_PLL_TYPE*)
 * @param[out] fixed_hz If 0 is returned, will be set to the fixed clock
 * speed for this device.
 */
bus_size_t
bhnd_pwrctl_cpu_clkreg_m(const struct bhnd_chipid *cid,
    uint8_t pll_type, uint32_t *fixed_hz)
{
	switch (pll_type) {
	case CHIPC_PLL_TYPE2:
	case CHIPC_PLL_TYPE4:
	case CHIPC_PLL_TYPE6:
	case CHIPC_PLL_TYPE7:
		return (CHIPC_CLKC_M3);

	case CHIPC_PLL_TYPE5:
		/* fixed 200MHz */
		if (fixed_hz != NULL)
			*fixed_hz = 200 * 1000 * 1000;
		return (0);

	case CHIPC_PLL_TYPE3:
		if (cid->chip_id == BHND_CHIPID_BCM5365) {
			/* fixed 200MHz */
			if (fixed_hz != NULL)
				*fixed_hz = 200 * 1000 * 1000;
			return (0);
		}

		return (CHIPC_CLKC_M2);

	default:
		return (CHIPC_CLKC_SB);
	}
}

/**
 * Calculate the CPU clock speed (in Hz) for a given a set of clock control
 * values.
 * 
 * @param cid Chip identification.
 * @param pll_type PLL type (CHIPC_PLL_TYPE*)
 * @param n clock control N register value.
 * @param m clock control M register value.
 */
uint32_t
bhnd_pwrctl_cpu_clock_rate(const struct bhnd_chipid *cid,
    uint32_t pll_type, uint32_t n, uint32_t m)
{
	KASSERT(bhnd_pwrctl_cpu_clkreg_m(cid, pll_type, NULL) != 0,
	    ("can't compute clock rate on fixed clock"));

	return (bhnd_pwrctl_clock_rate(pll_type, n, m));
}

/**
 * Calculate the clock speed (in Hz) for a given a set of clockcontrol
 * values.
 * 
 * @param pll_type PLL type (CHIPC_PLL_TYPE*)
 * @param n clock control N register value.
 * @param m clock control M register value.
 */
uint32_t
bhnd_pwrctl_clock_rate(uint32_t pll_type, uint32_t n, uint32_t m)
{
	uint32_t clk_base;
	uint32_t n1, n2, clock, m1, m2, m3, mc;

	n1 = CHIPC_GET_BITS(n, CHIPC_CN_N1);
	n2 = CHIPC_GET_BITS(n, CHIPC_CN_N2);

	switch (pll_type) {
	case CHIPC_PLL_TYPE1:
	case CHIPC_PLL_TYPE3:
	case CHIPC_PLL_TYPE4:
	case CHIPC_PLL_TYPE7:
		n1 = bhnd_pwrctl_factor6(n1);
		n2 += CHIPC_F5_BIAS;
		break;

	case CHIPC_PLL_TYPE2:
		n1 += CHIPC_T2_BIAS;
		n2 += CHIPC_T2_BIAS;
		KASSERT(n1 >= 2 && n1 <= 7, ("invalid n1 value"));
		KASSERT(n2 >= 5 && n2 <= 23, ("invalid n2 value"));
		break;
		
	case CHIPC_PLL_TYPE5:
		return (100000000);

	case CHIPC_PLL_TYPE6:
		if (m & CHIPC_T6_MMASK)
			return (CHIPC_T6_M1);
		else
			return (CHIPC_T6_M0);

	default:
		printf("unsupported PLL type %u\n", pll_type);
		return (0);
	}

	/* PLL types 3 and 7 use BASE2 (25Mhz) */
	if (pll_type == CHIPC_PLL_TYPE3 || pll_type == CHIPC_PLL_TYPE7) {
		clk_base = CHIPC_CLOCK_BASE2;
	} else {
		clk_base = CHIPC_CLOCK_BASE1;
	}

	clock = clk_base * n1 * n2;

	if (clock == 0)
		return (0);

	m1 = CHIPC_GET_BITS(m, CHIPC_M1);
	m2 = CHIPC_GET_BITS(m, CHIPC_M2);
	m3 = CHIPC_GET_BITS(m, CHIPC_M3);
	mc = CHIPC_GET_BITS(m, CHIPC_MC);

	switch (pll_type) {
	case CHIPC_PLL_TYPE1:
	case CHIPC_PLL_TYPE3:
	case CHIPC_PLL_TYPE4:
	case CHIPC_PLL_TYPE7:
		m1 = bhnd_pwrctl_factor6(m1);
		if (pll_type == CHIPC_PLL_TYPE1 || pll_type == CHIPC_PLL_TYPE3)
			m2 += CHIPC_F5_BIAS;
		else
			m2 = bhnd_pwrctl_factor6(m2);

		m3 = bhnd_pwrctl_factor6(m3);

		switch (mc) {
		case CHIPC_MC_BYPASS:	
			return (clock);
		case CHIPC_MC_M1:	
			return (clock / m1);
		case CHIPC_MC_M1M2:	
			return (clock / (m1 * m2));
		case CHIPC_MC_M1M2M3:
			return (clock / (m1 * m2 * m3));
		case CHIPC_MC_M1M3:
			return (clock / (m1 * m3));
		default:
			printf("unsupported pwrctl mc %#x\n", mc);
			return (0);
		}
	case CHIPC_PLL_TYPE2:
		m1 += CHIPC_T2_BIAS;
		m2 += CHIPC_T2M2_BIAS;
		m3 += CHIPC_T2_BIAS;
		KASSERT(m1 >= 2 && m1 <= 7, ("invalid m1 value"));
		KASSERT(m2 >= 3 && m2 <= 10, ("invalid m2 value"));
		KASSERT(m3 >= 2 && m3 <= 7, ("invalid m3 value"));

		if ((mc & CHIPC_T2MC_M1BYP) == 0)
			clock /= m1;
		if ((mc & CHIPC_T2MC_M2BYP) == 0)
			clock /= m2;
		if ((mc & CHIPC_T2MC_M3BYP) == 0)
			clock /= m3;

		return (clock);
	default:
		panic("unhandled PLL type %u\n", pll_type);
	}
}

/**
 * Return the backplane clock speed in Hz.
 * 
 * @param sc driver instance state.
 */
uint32_t
bhnd_pwrctl_getclk_speed(struct bhnd_pwrctl_softc *sc)
{
	const struct bhnd_chipid	*cid;
	struct chipc_caps		*ccaps;
	bus_size_t			 creg;
	uint32_t 			 n, m;
	uint32_t 			 rate;

	PWRCTL_LOCK_ASSERT(sc, MA_OWNED);

	cid = bhnd_get_chipid(sc->chipc_dev);
	ccaps = BHND_CHIPC_GET_CAPS(sc->chipc_dev);

	n = bhnd_bus_read_4(sc->res, CHIPC_CLKC_N);

	/* Get M register offset */
	creg = bhnd_pwrctl_si_clkreg_m(cid, ccaps->pll_type, &rate);
	if (creg == 0) /* fixed rate */
		return (rate);

	/* calculate rate */
	m = bhnd_bus_read_4(sc->res, creg);
	return (bhnd_pwrctl_si_clock_rate(cid, ccaps->pll_type, n, m));
}

/* return the slow clock source */
static bhnd_clksrc
bhnd_pwrctl_slowclk_src(struct bhnd_pwrctl_softc *sc)
{
	uint32_t clkreg;
	uint32_t clksrc;

	/* Fetch clock source */
	if (PWRCTL_QUIRK(sc, PCICLK_CTL)) {
		return (bhnd_pwrctl_hostb_get_clksrc(sc->chipc_dev,
		    BHND_CLOCK_ILP));
	} else if (PWRCTL_QUIRK(sc, SLOWCLK_CTL)) {
		clkreg = bhnd_bus_read_4(sc->res, CHIPC_PLL_SLOWCLK_CTL);
		clksrc = clkreg & CHIPC_SCC_SS_MASK;
	} else {
		/* Instaclock */
		clksrc = CHIPC_SCC_SS_XTAL;
	}

	/* Map to bhnd_clksrc */
	switch (clksrc) {
	case CHIPC_SCC_SS_PCI:
		return (BHND_CLKSRC_PCI);
	case CHIPC_SCC_SS_LPO:
		return (BHND_CLKSRC_LPO);
	case CHIPC_SCC_SS_XTAL:
		return (BHND_CLKSRC_XTAL);
	default:
		return (BHND_CLKSRC_UNKNOWN);
	}
}

/* return the ILP (slowclock) min or max frequency */
static uint32_t
bhnd_pwrctl_slowclk_freq(struct bhnd_pwrctl_softc *sc, bool max_freq)
{
	bhnd_clksrc	slowclk;
	uint32_t	div;
	uint32_t	hz;

	slowclk = bhnd_pwrctl_slowclk_src(sc);

	/* Determine clock divisor */
	if (PWRCTL_QUIRK(sc, PCICLK_CTL)) {
		if (slowclk == BHND_CLKSRC_PCI)
			div = 64;
		else
			div = 32;
	} else if (PWRCTL_QUIRK(sc, SLOWCLK_CTL)) {
		div = bhnd_bus_read_4(sc->res, CHIPC_PLL_SLOWCLK_CTL);
		div = CHIPC_GET_BITS(div, CHIPC_SCC_CD);
		div = 4 * (div + 1);
	} else if (PWRCTL_QUIRK(sc, INSTACLK_CTL)) {
		if (max_freq) {
			div = 1;
		} else {
			div = bhnd_bus_read_4(sc->res, CHIPC_SYS_CLK_CTL);
			div = CHIPC_GET_BITS(div, CHIPC_SYCC_CD);
			div = 4 * (div + 1);
		}
	} else {
		device_printf(sc->dev, "unknown device type\n");
		return (0);
	}

	/* Determine clock frequency */
	switch (slowclk) {
	case BHND_CLKSRC_LPO:
		hz = max_freq ? CHIPC_LPOMAXFREQ : CHIPC_LPOMINFREQ;
		break;
	case BHND_CLKSRC_XTAL:
		hz = max_freq ? CHIPC_XTALMAXFREQ : CHIPC_XTALMINFREQ;
		break;
	case BHND_CLKSRC_PCI:
		hz = max_freq ? CHIPC_PCIMAXFREQ : CHIPC_PCIMINFREQ;
		break;
	default:
		device_printf(sc->dev, "unknown slowclk source %#x\n", slowclk);
		return (0);
	}

	return (hz / div);
}

/**
 * Initialize power control registers.
 */
int
bhnd_pwrctl_init(struct bhnd_pwrctl_softc *sc)
{
	uint32_t	clkctl;
	uint32_t	pll_delay, slowclk, slowmaxfreq;
	uint32_t 	pll_on_delay, fref_sel_delay;
	int		error;

	pll_delay = CHIPC_PLL_DELAY;

	/* set all Instaclk chip ILP to 1 MHz */
	if (PWRCTL_QUIRK(sc, INSTACLK_CTL)) {
		clkctl = (CHIPC_ILP_DIV_1MHZ << CHIPC_SYCC_CD_SHIFT);
		clkctl &= CHIPC_SYCC_CD_MASK;
		bhnd_bus_write_4(sc->res, CHIPC_SYS_CLK_CTL, clkctl);
	}

	/* 
	 * Initialize PLL/FREF delays.
	 * 
	 * If the slow clock is not sourced by the xtal, include the
	 * delay required to bring it up.
	 */
	slowclk = bhnd_pwrctl_slowclk_src(sc);
	if (slowclk != CHIPC_SCC_SS_XTAL)
		pll_delay += CHIPC_XTAL_ON_DELAY;

	/* Starting with 4318 it is ILP that is used for the delays */
	if (PWRCTL_QUIRK(sc, INSTACLK_CTL))
		slowmaxfreq = bhnd_pwrctl_slowclk_freq(sc, false);
	else
		slowmaxfreq = bhnd_pwrctl_slowclk_freq(sc, true);

	pll_on_delay = ((slowmaxfreq * pll_delay) + 999999) / 1000000;
	fref_sel_delay = ((slowmaxfreq * CHIPC_FREF_DELAY) + 999999) / 1000000;

	bhnd_bus_write_4(sc->res, CHIPC_PLL_ON_DELAY, pll_on_delay);
	bhnd_bus_write_4(sc->res, CHIPC_PLL_FREFSEL_DELAY, fref_sel_delay);

	/* If required, force HT */
	if (PWRCTL_QUIRK(sc, FORCE_HT)) {
		if ((error = bhnd_pwrctl_setclk(sc, BHND_CLOCK_HT)))
			return (error);
	}

	return (0);
}

/* return the value suitable for writing to the dot11 core
 * FAST_PWRUP_DELAY register */
u_int
bhnd_pwrctl_fast_pwrup_delay(struct bhnd_pwrctl_softc *sc)
{
	u_int pll_on_delay, slowminfreq;
	u_int fpdelay;

	fpdelay = 0;

	slowminfreq = bhnd_pwrctl_slowclk_freq(sc, false);

	pll_on_delay = bhnd_bus_read_4(sc->res, CHIPC_PLL_ON_DELAY) + 2;
	pll_on_delay *= 1000000;
	pll_on_delay += (slowminfreq - 1);
	fpdelay = pll_on_delay / slowminfreq;

	return (fpdelay);
}

/**
 * Distribute @p clock on backplane.
 * 
 * @param sc Driver instance state.
 * @param clock Clock to enable.
 * 
 * @retval 0 success
 * @retval ENODEV If @p clock is unsupported, or if the device does not
 * 		  support dynamic clock control.
 */
int
bhnd_pwrctl_setclk(struct bhnd_pwrctl_softc *sc, bhnd_clock clock)
{
	uint32_t	scc;

	PWRCTL_LOCK_ASSERT(sc, MA_OWNED);

	/* Is dynamic clock control supported? */
	if (PWRCTL_QUIRK(sc, FIXED_CLK))
		return (ENODEV);

	/* Chips with ccrev 10 are EOL and they don't have SYCC_HR used below */
	if (bhnd_get_hwrev(sc->chipc_dev) == 10)
		return (ENODEV);

	if (PWRCTL_QUIRK(sc, SLOWCLK_CTL))
		scc = bhnd_bus_read_4(sc->res, CHIPC_PLL_SLOWCLK_CTL);
	else
		scc = bhnd_bus_read_4(sc->res, CHIPC_SYS_CLK_CTL);

	switch (clock) {
	case BHND_CLOCK_HT:
		/* fast (pll) clock */
		if (PWRCTL_QUIRK(sc, SLOWCLK_CTL)) {
			scc &= ~(CHIPC_SCC_XC | CHIPC_SCC_FS | CHIPC_SCC_IP);
			scc |= CHIPC_SCC_IP;

			/* force xtal back on before clearing SCC_DYN_XTAL.. */
			bhnd_pwrctl_hostb_ungate_clock(sc->chipc_dev,
			    BHND_CLOCK_HT);
		} else if (PWRCTL_QUIRK(sc, INSTACLK_CTL)) {
			scc |= CHIPC_SYCC_HR;
		} else {
			return (ENODEV);
		}

		if (PWRCTL_QUIRK(sc, SLOWCLK_CTL))
			bhnd_bus_write_4(sc->res, CHIPC_PLL_SLOWCLK_CTL, scc);
		else
			bhnd_bus_write_4(sc->res, CHIPC_SYS_CLK_CTL, scc);
		DELAY(CHIPC_PLL_DELAY);

		break;		

	case BHND_CLOCK_DYN:
		/* enable dynamic clock control */
		if (PWRCTL_QUIRK(sc, SLOWCLK_CTL)) {
			scc &= ~(CHIPC_SCC_FS | CHIPC_SCC_IP | CHIPC_SCC_XC);
			if ((scc & CHIPC_SCC_SS_MASK) != CHIPC_SCC_SS_XTAL)
				scc |= CHIPC_SCC_XC;
	
			bhnd_bus_write_4(sc->res, CHIPC_PLL_SLOWCLK_CTL, scc);

			/* for dynamic control, we have to release our xtal_pu
			 * "force on" */
			if (scc & CHIPC_SCC_XC) {
				bhnd_pwrctl_hostb_gate_clock(sc->chipc_dev,
				    BHND_CLOCK_HT);
			}
		} else if (PWRCTL_QUIRK(sc, INSTACLK_CTL)) {
			/* Instaclock */
			scc &= ~CHIPC_SYCC_HR;
			bhnd_bus_write_4(sc->res, CHIPC_SYS_CLK_CTL, scc);
		} else {
			return (ENODEV);
		}

		break;

	default:
		return (ENODEV);
	}

	return (0);
}
