/*-
 * Copyright (c) 2016 Landon Fuller <landonf@FreeBSD.org>
 *
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

#include <dev/bhnd/bhnd.h>

#include <dev/bhnd/cores/chipc/chipcreg.h>

#include <dev/bhnd/cores/chipc/pwrctl/bhnd_pwrctlvar.h>

#include <dev/bhnd/cores/pmu/bhnd_pmureg.h>
#include <dev/bhnd/cores/pmu/bhnd_pmuvar.h>

#include "bcm_machdep.h"

static struct bhnd_pmu_query	*bcm_get_pmu(struct bcm_platform *bp);
static bool			 bcm_has_pmu(struct bcm_platform *bp);

static uint32_t			 bcm_pmu_read4(bus_size_t reg, void *ctx);
static void			 bcm_pmu_write4(bus_size_t reg, uint32_t val,
				     void *ctx);
static uint32_t			 bcm_pmu_read_chipst(void *ctx);

const struct bhnd_pmu_io bcm_pmu_soc_io = {
	.rd4		= bcm_pmu_read4,
	.wr4		= bcm_pmu_write4,
	.rd_chipst	= bcm_pmu_read_chipst
};

/**
 * Supported UART clock sources.
 */
typedef enum {
	BCM_UART_RCLK_PLL_T1	= 0,	/**< UART uses PLL m2 (mii/uart/mipsref) with no divisor */
	BCM_UART_RCLK_ALP	= 1,	/**< UART uses ALP rclk with no divisor */
	BCM_UART_RCLK_EXT	= 2,	/**< UART uses 1.8423 MHz external clock */
	BCM_UART_RCLK_SI	= 3,	/**< UART uses backplane clock with divisor of two */
	BCM_UART_RCLK_FIXED	= 4,	/**< UART uses fixed 88Mhz backplane clock with a divisor of 48 */
} bcm_uart_clksrc;

/**
 * UART clock configuration.
 */
struct bcm_uart_clkcfg {
	bcm_uart_clksrc		src;	/**< clock source */
	uint32_t		div;	/**< clock divisor */
	uint32_t		freq;	/**< clock frequency (Hz) */
};

#define	BCM_UART_RCLK_PLL_T1_DIV	1
#define	BCM_UART_RCLK_ALP_DIV		1
#define	BCM_UART_RCLK_EXT_HZ		1842300		/* 1.8423MHz */
#define	BCM_UART_RCLK_EXT_DIV		1
#define	BCM_UART_RCLK_FIXED_HZ		88000000	/* 88MHz */
#define	BCM_UART_RCLK_FIXED_DIV		48

/* Fetch PLL type from ChipCommon capability flags */
#define	BCM_PMU_PLL_TYPE(_bp)	\
	CHIPC_GET_BITS(_bp->cc_caps, CHIPC_CAP_PLL)

/**
 * Return the PMU instance, or NULL if no PMU.
 */
static struct bhnd_pmu_query *
bcm_get_pmu(struct bcm_platform	*bp)
{
	if (!bcm_has_pmu(bp))
		return (NULL);
	return (&bp->pmu);
}

/**
 * Return true if a PMU is available, false otherwise.
 */
static bool
bcm_has_pmu(struct bcm_platform *bp)
{
	return (bp->pmu_addr != 0);
}

/**
 * Determine the UART clock source for @p bp and return the
 * corresponding clock configuration, if any.
 */
static struct bcm_uart_clkcfg
bcm_get_uart_clkcfg(struct bcm_platform *bp)
{
	struct bcm_uart_clkcfg	 cfg;
	struct bhnd_core_info	*cc_id;

	cc_id = &bp->cc_id;

	/* These tests are ordered by precedence. */

	/* PLL M2 clock source? */
	if (!bcm_has_pmu(bp) && BCM_PMU_PLL_TYPE(bp) == CHIPC_PLL_TYPE1) {
		uint32_t n, m;
	
		n = BCM_CHIPC_READ_4(bp, CHIPC_CLKC_N);
		m = BCM_CHIPC_READ_4(bp, CHIPC_CLKC_M2);

		cfg = (struct bcm_uart_clkcfg) {
			BCM_UART_RCLK_PLL_T1,
			BCM_UART_RCLK_PLL_T1_DIV,
			bhnd_pwrctl_clock_rate(BCM_PMU_PLL_TYPE(bp), n, m)
		};
	
		return (cfg);
	}

	/* ALP clock source? */
	if (cc_id->hwrev != 15 && cc_id->hwrev >= 11) {
		cfg = (struct bcm_uart_clkcfg) {
			BCM_UART_RCLK_ALP,
			BCM_UART_RCLK_ALP_DIV,
			bcm_get_alpfreq(bp)
		};
		return (cfg);
	}

	/* External clock? */
	if (CHIPC_HWREV_HAS_CORECTRL(cc_id->hwrev)) {
		uint32_t	corectrl, uclksel;
		bool		uintclk0;

		/* Fetch UART clock support flag */ 
		uclksel = CHIPC_GET_BITS(bp->cc_caps, CHIPC_CAP_UCLKSEL);

		/* Is UART using internal clock? */
		corectrl = BCM_CHIPC_READ_4(bp, CHIPC_CORECTRL);
		uintclk0 = CHIPC_GET_FLAG(corectrl, CHIPC_UARTCLKO);

		if (uintclk0 && uclksel == CHIPC_CAP_UCLKSEL_UINTCLK) {
			cfg = (struct bcm_uart_clkcfg) {
				BCM_UART_RCLK_EXT,
				BCM_UART_RCLK_EXT_DIV,
				BCM_UART_RCLK_EXT_HZ
			};
			return (cfg);
		}
	}

	/* UART uses backplane clock? */
	if (cc_id->hwrev == 15 || (cc_id->hwrev >= 3 && cc_id->hwrev <= 10)) {
		cfg = (struct bcm_uart_clkcfg) {
			BCM_UART_RCLK_SI,
			BCM_CHIPC_READ_4(bp, CHIPC_CLKDIV) & CHIPC_CLKD_UART,
			bcm_get_sifreq(bp)
		};

		return (cfg);
	}

	/* UART uses fixed clock? */
	if (cc_id->hwrev <= 2) {
		cfg = (struct bcm_uart_clkcfg) {
			BCM_UART_RCLK_FIXED,
			BCM_UART_RCLK_FIXED_DIV,
			BCM_UART_RCLK_FIXED_HZ
		};

		return (cfg);
	}

	/* All cases must be accounted for above */
	panic("unreachable - no clock config");
}

/**
 * Return the UART reference clock frequency (in Hz).
 */
u_int
bcm_get_uart_rclk(struct bcm_platform *bp)
{
	struct bcm_uart_clkcfg cfg;

	cfg = bcm_get_uart_clkcfg(bp);
	return (cfg.freq / cfg.div);
}

/** ALP clock frequency (in Hz) */
uint64_t
bcm_get_alpfreq(struct bcm_platform *bp) {
	if (!bcm_has_pmu(bp))
		return (BHND_PMU_ALP_CLOCK);

	return (bhnd_pmu_alp_clock(bcm_get_pmu(bp)));
}

/** ILP clock frequency (in Hz) */
uint64_t
bcm_get_ilpfreq(struct bcm_platform *bp) {
	if (!bcm_has_pmu(bp))
		return (BHND_PMU_ILP_CLOCK);

	return (bhnd_pmu_ilp_clock(bcm_get_pmu(bp)));
}

/** CPU clock frequency (in Hz) */
uint64_t
bcm_get_cpufreq(struct bcm_platform *bp)
{
	uint32_t		 fixed_hz;
	uint32_t		 n, m;
	bus_size_t		 mreg;
	uint8_t			 pll_type;

	/* PMU support */
	if (bcm_has_pmu(bp))
		return (bhnd_pmu_cpu_clock(bcm_get_pmu(bp)));

	/*
	 * PWRCTL support
	 */
	pll_type = CHIPC_GET_BITS(bp->cc_caps, CHIPC_CAP_PLL);
	mreg = bhnd_pwrctl_cpu_clkreg_m(&bp->cid, pll_type, &fixed_hz);
	if (mreg == 0)
		return (fixed_hz);

	n = BCM_CHIPC_READ_4(bp, CHIPC_CLKC_N);
	m = BCM_CHIPC_READ_4(bp, mreg);

	return (bhnd_pwrctl_cpu_clock_rate(&bp->cid, pll_type, n, m));
	
}

/** Backplane clock frequency (in Hz) */
uint64_t
bcm_get_sifreq(struct bcm_platform *bp)
{
	uint32_t		 fixed_hz;
	uint32_t		 n, m;
	bus_size_t		 mreg;
	uint8_t			 pll_type;

	/* PMU support */
	if (bcm_has_pmu(bp))
		return (bhnd_pmu_si_clock(bcm_get_pmu(bp)));

	/*
	 * PWRCTL support
	 */
	pll_type = CHIPC_GET_BITS(bp->cc_caps, CHIPC_CAP_PLL);
	mreg = bhnd_pwrctl_si_clkreg_m(&bp->cid, pll_type, &fixed_hz);
	if (mreg == 0)
		return (fixed_hz);

	n = BCM_CHIPC_READ_4(bp, CHIPC_CLKC_N);
	m = BCM_CHIPC_READ_4(bp, mreg);

	return (bhnd_pwrctl_si_clock_rate(&bp->cid, pll_type, n, m));
}


static uint32_t
bcm_pmu_read4(bus_size_t reg, void *ctx) {
	struct bcm_platform *bp = ctx;
	return (readl(BCM_SOC_ADDR(bp->pmu_addr, reg)));
}

static void
bcm_pmu_write4(bus_size_t reg, uint32_t val, void *ctx) {
	struct bcm_platform *bp = ctx;
	writel(BCM_SOC_ADDR(bp->pmu_addr, reg), val);
}

static uint32_t
bcm_pmu_read_chipst(void *ctx)
{
	struct bcm_platform *bp = ctx;
	return (readl(BCM_SOC_ADDR(bp->cc_addr, CHIPC_CHIPST)));
}
