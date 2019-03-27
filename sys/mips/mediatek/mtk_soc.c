/*-
 * Copyright (c) 2016 Stanislav Galabov.
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
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>

#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/fdt/fdt_common.h>
#include <dev/fdt/fdt_clock.h>

#include <mips/mediatek/fdt_reset.h>
#include <mips/mediatek/mtk_sysctl.h>
#include <mips/mediatek/mtk_soc.h>

static uint32_t mtk_soc_socid = MTK_SOC_UNKNOWN;
static uint32_t mtk_soc_uartclk = 0;
static uint32_t mtk_soc_cpuclk = MTK_CPU_CLK_880MHZ;
static uint32_t mtk_soc_timerclk = MTK_CPU_CLK_880MHZ / 2;

static uint32_t mtk_soc_chipid0_3 = MTK_UNKNOWN_CHIPID0_3;
static uint32_t mtk_soc_chipid4_7 = MTK_UNKNOWN_CHIPID4_7;

static const struct ofw_compat_data compat_data[] = {
	{ "ralink,rt2880-soc",		MTK_SOC_RT2880 },
	{ "ralink,rt3050-soc",		MTK_SOC_RT3050 },
	{ "ralink,rt3052-soc",		MTK_SOC_RT3052 },
	{ "ralink,rt3350-soc",		MTK_SOC_RT3350 },
	{ "ralink,rt3352-soc",		MTK_SOC_RT3352 },
	{ "ralink,rt3662-soc",		MTK_SOC_RT3662 },
	{ "ralink,rt3883-soc",		MTK_SOC_RT3883 },
	{ "ralink,rt5350-soc",		MTK_SOC_RT5350 },
	{ "ralink,mtk7620a-soc",	MTK_SOC_MT7620A },
	{ "ralink,mt7620a-soc",		MTK_SOC_MT7620A },
	{ "ralink,mtk7620n-soc",	MTK_SOC_MT7620N },
	{ "ralink,mt7620n-soc",		MTK_SOC_MT7620N },
	{ "mediatek,mtk7621-soc",	MTK_SOC_MT7621 },
	{ "mediatek,mt7621-soc",	MTK_SOC_MT7621 },
	{ "ralink,mt7621-soc",		MTK_SOC_MT7621 },
	{ "ralink,mtk7621-soc",		MTK_SOC_MT7621 },
	{ "ralink,mtk7628an-soc",	MTK_SOC_MT7628 },
	{ "mediatek,mt7628an-soc",	MTK_SOC_MT7628 },
	{ "ralink,mtk7688-soc",		MTK_SOC_MT7688 },

	/* Sentinel */
	{ NULL,				MTK_SOC_UNKNOWN },
};

static uint32_t
mtk_detect_cpuclk_rt2880(bus_space_tag_t bst, bus_space_handle_t bsh)
{
	uint32_t val;

	val = bus_space_read_4(bst, bsh, SYSCTL_SYSCFG);
	val >>= RT2880_CPU_CLKSEL_OFF;
	val &= RT2880_CPU_CLKSEL_MSK;

	switch (val) {
	case 0:
		return (MTK_CPU_CLK_250MHZ);
	case 1:
		return (MTK_CPU_CLK_266MHZ);
	case 2:
		return (MTK_CPU_CLK_280MHZ);
	case 3:
		return (MTK_CPU_CLK_300MHZ);
	}

	/* Never reached */
	return (0);
}

static uint32_t
mtk_detect_cpuclk_rt305x(bus_space_tag_t bst, bus_space_handle_t bsh)
{
	uint32_t val;

	val = bus_space_read_4(bst, bsh, SYSCTL_CHIPID0_3);
	if (val == RT3350_CHIPID0_3)
		return (MTK_CPU_CLK_320MHZ);

	val = bus_space_read_4(bst, bsh, SYSCTL_SYSCFG);
	val >>= RT305X_CPU_CLKSEL_OFF;
	val &= RT305X_CPU_CLKSEL_MSK;

	return ((val == 0) ? MTK_CPU_CLK_320MHZ : MTK_CPU_CLK_384MHZ);
}

static uint32_t
mtk_detect_cpuclk_rt3352(bus_space_tag_t bst, bus_space_handle_t bsh)
{
	uint32_t val;

	val = bus_space_read_4(bst, bsh, SYSCTL_SYSCFG);
	val >>= RT3352_CPU_CLKSEL_OFF;
	val &= RT3352_CPU_CLKSEL_MSK;

	if (val)
		return (MTK_CPU_CLK_400MHZ);

	return (MTK_CPU_CLK_384MHZ);
}

static uint32_t
mtk_detect_cpuclk_rt3883(bus_space_tag_t bst, bus_space_handle_t bsh)
{
	uint32_t val;

	val = bus_space_read_4(bst, bsh, SYSCTL_SYSCFG);
	val >>= RT3883_CPU_CLKSEL_OFF;
	val &= RT3883_CPU_CLKSEL_MSK;

	switch (val) {
	case 0:
		return (MTK_CPU_CLK_250MHZ);
	case 1:
		return (MTK_CPU_CLK_384MHZ);
	case 2:
		return (MTK_CPU_CLK_480MHZ);
	case 3:
		return (MTK_CPU_CLK_500MHZ);
	}

	/* Never reached */
	return (0);
}

static uint32_t
mtk_detect_cpuclk_rt5350(bus_space_tag_t bst, bus_space_handle_t bsh)
{
	uint32_t val1, val2;

	val1 = val2 = bus_space_read_4(bst, bsh, SYSCTL_SYSCFG);

	val1 >>= RT5350_CPU_CLKSEL_OFF1;
	val2 >>= RT5350_CPU_CLKSEL_OFF2;
	val1 &= RT5350_CPU_CLKSEL_MSK;
	val2 &= RT5350_CPU_CLKSEL_MSK;
	val1 |= (val2 << 1);

	switch (val1) {
	case 0:
		return (MTK_CPU_CLK_360MHZ);
	case 1:
		/* Reserved value, but we return UNKNOWN */
		return (MTK_CPU_CLK_UNKNOWN);
	case 2:
		return (MTK_CPU_CLK_320MHZ);
	case 3:
		return (MTK_CPU_CLK_300MHZ);
	}

	/* Never reached */
	return (0);
}

static uint32_t
mtk_detect_cpuclk_mt7620(bus_space_tag_t bst, bus_space_handle_t bsh)
{
	uint32_t val, mul, div, res;

	val = bus_space_read_4(bst, bsh, SYSCTL_MT7620_CPLL_CFG1);
	if (val & MT7620_CPU_CLK_AUX0)
		return (MTK_CPU_CLK_480MHZ);

	val = bus_space_read_4(bst, bsh, SYSCTL_MT7620_CPLL_CFG0);
	if (!(val & MT7620_CPLL_SW_CFG))
		return (MTK_CPU_CLK_600MHZ);

	mul = MT7620_PLL_MULT_RATIO_BASE + ((val >> MT7620_PLL_MULT_RATIO_OFF) &
	    MT7620_PLL_MULT_RATIO_MSK);
	div = (val >> MT7620_PLL_DIV_RATIO_OFF) & MT7620_PLL_DIV_RATIO_MSK;

	if (div != MT7620_PLL_DIV_RATIO_MSK)
		div += MT7620_PLL_DIV_RATIO_BASE;
	else
		div = MT7620_PLL_DIV_RATIO_MAX;

	res = (MT7620_XTAL_40 * mul) / div;

	return (MTK_MHZ(res));
}

static uint32_t
mtk_detect_cpuclk_mt7621(bus_space_tag_t bst, bus_space_handle_t bsh)
{
	uint32_t val, div, res;

	val = bus_space_read_4(bst, bsh, SYSCTL_CLKCFG0);
	if (val & MT7621_USES_MEMDIV) {
		div = bus_space_read_4(bst, bsh, MTK_MT7621_CLKDIV_REG);
		div >>= MT7621_MEMDIV_OFF;
		div &= MT7621_MEMDIV_MSK;
		div += MT7621_MEMDIV_BASE;

		val = bus_space_read_4(bst, bsh, SYSCTL_SYSCFG);
		val >>= MT7621_CLKSEL_OFF;
		val &= MT7621_CLKSEL_MSK;

		if (val >= MT7621_CLKSEL_25MHZ_VAL)
			res = div * MT7621_CLKSEL_25MHZ;
		else if (val >= MT7621_CLKSEL_20MHZ_VAL)
			res = div * MT7621_CLKSEL_20MHZ;
		else
			res = div * 0; /* XXX: not sure about this */
	} else {
		val = bus_space_read_4(bst, bsh, SYSCTL_CUR_CLK_STS);
		div = (val >> MT7621_CLK_STS_DIV_OFF) & MT7621_CLK_STS_MSK;
		val &= MT7621_CLK_STS_MSK;

		res = (MT7621_CLK_STS_BASE * val) / div;
	}

	return (MTK_MHZ(res));
}

static uint32_t
mtk_detect_cpuclk_mt7628(bus_space_tag_t bst, bus_space_handle_t bsh)
{
	uint32_t val;

	val = bus_space_read_4(bst, bsh, SYSCTL_SYSCFG);
	val >>= MT7628_CPU_CLKSEL_OFF;
	val &= MT7628_CPU_CLKSEL_MSK;

	if (val)
		return (MTK_CPU_CLK_580MHZ);

	return (MTK_CPU_CLK_575MHZ);
}

void
mtk_soc_try_early_detect(void)
{
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	uint32_t base;
	phandle_t node;
	int i;

	if ((node = OF_finddevice("/")) == -1)
		return;

	for (i = 0; compat_data[i].ocd_str != NULL; i++) {
		if (ofw_bus_node_is_compatible(node, compat_data[i].ocd_str)) {
			mtk_soc_socid = compat_data[i].ocd_data;
			break;
		}
	}

	if (mtk_soc_socid == MTK_SOC_UNKNOWN) {
		/* We don't know the SoC, so we don't know how to get clocks */
		return;
	}

	bst = fdtbus_bs_tag;
	if (mtk_soc_socid == MTK_SOC_RT2880)
		base = MTK_RT2880_BASE;
	else if (mtk_soc_socid == MTK_SOC_MT7621)
		base = MTK_MT7621_BASE;
	else
		base = MTK_DEFAULT_BASE;

	if (bus_space_map(bst, base, MTK_DEFAULT_SIZE, 0, &bsh))
		return;

	/* Get our CHIP ID */
	mtk_soc_chipid0_3 = bus_space_read_4(bst, bsh, SYSCTL_CHIPID0_3);
	mtk_soc_chipid4_7 = bus_space_read_4(bst, bsh, SYSCTL_CHIPID4_7);

	/* First, figure out the CPU clock */
	switch (mtk_soc_socid) {
	case MTK_SOC_RT2880:
		mtk_soc_cpuclk = mtk_detect_cpuclk_rt2880(bst, bsh);
		break;
	case MTK_SOC_RT3050:  /* fallthrough */
	case MTK_SOC_RT3052:
	case MTK_SOC_RT3350:
		mtk_soc_cpuclk = mtk_detect_cpuclk_rt305x(bst, bsh);
		break;
	case MTK_SOC_RT3352:
		mtk_soc_cpuclk = mtk_detect_cpuclk_rt3352(bst, bsh);
		break;
	case MTK_SOC_RT3662:  /* fallthrough */
	case MTK_SOC_RT3883:
		mtk_soc_cpuclk = mtk_detect_cpuclk_rt3883(bst, bsh);
		break;
	case MTK_SOC_RT5350:
		mtk_soc_cpuclk = mtk_detect_cpuclk_rt5350(bst, bsh);
		break;
	case MTK_SOC_MT7620A: /* fallthrough */
	case MTK_SOC_MT7620N:
		mtk_soc_cpuclk = mtk_detect_cpuclk_mt7620(bst, bsh);
		break;
	case MTK_SOC_MT7621:
		mtk_soc_cpuclk = mtk_detect_cpuclk_mt7621(bst, bsh);
		break;
	case MTK_SOC_MT7628:  /* fallthrough */
	case MTK_SOC_MT7688:
		mtk_soc_cpuclk = mtk_detect_cpuclk_mt7628(bst, bsh);
		break;
	default:
		/* We don't know the SoC, so we can't find the CPU clock */
		break;
	}

	/* Now figure out the timer clock */
	if (mtk_soc_socid == MTK_SOC_MT7621) {
#ifdef notyet
		/* 
		 * We use the GIC timer for timing source and its clock freq is
		 * the same as the CPU's clock freq
		 */
		mtk_soc_timerclk = mtk_soc_cpuclk;
#else
		/*
		 * When GIC timer and MIPS timer are ready to co-exist and
		 * GIC timer is actually implemented, we need to switch to it.
		 * Until then we use a fake GIC timer, which is actually a
		 * normal MIPS ticker, so the timer clock is half the CPU clock
		 */
		mtk_soc_timerclk = mtk_soc_cpuclk / 2;
#endif
	} else {
		/*
		 * We use the MIPS ticker for the rest for now, so
		 * the CPU clock is divided by 2
		 */
		mtk_soc_timerclk = mtk_soc_cpuclk / 2;
	}

	switch (mtk_soc_socid) {
	case MTK_SOC_RT2880:
		mtk_soc_uartclk = mtk_soc_cpuclk / MTK_UARTDIV_2;
		break;
	case MTK_SOC_RT3350:  /* fallthrough */
	case MTK_SOC_RT3050:  /* fallthrough */
	case MTK_SOC_RT3052:
		/* UART clock is CPU clock / 3 */
		mtk_soc_uartclk = mtk_soc_cpuclk / MTK_UARTDIV_3;
		break;
	case MTK_SOC_RT3352:  /* fallthrough */
	case MTK_SOC_RT3662:  /* fallthrough */
	case MTK_SOC_RT3883:  /* fallthrough */
	case MTK_SOC_RT5350:  /* fallthrough */
	case MTK_SOC_MT7620A: /* fallthrough */
	case MTK_SOC_MT7620N: /* fallthrough */
	case MTK_SOC_MT7628:  /* fallthrough */
	case MTK_SOC_MT7688:
		/* UART clock is always 40MHz */
		mtk_soc_uartclk = MTK_UART_CLK_40MHZ;
		break;
	case MTK_SOC_MT7621:
		/* UART clock is always 50MHz */
		mtk_soc_uartclk = MTK_UART_CLK_50MHZ;
		break;
	default:
		/* We don't know the SoC, so we don't know the UART clock */
		break;
	}

	bus_space_unmap(bst, bsh, MTK_DEFAULT_SIZE);
}

extern char cpu_model[];

void
mtk_soc_set_cpu_model(void)
{
	int idx, offset = sizeof(mtk_soc_chipid0_3);
	char *chipid0_3 = (char *)(&mtk_soc_chipid0_3);
	char *chipid4_7 = (char *)(&mtk_soc_chipid4_7);

	/*
	 * CHIPID is always 2x32 bit registers, containing the ASCII
	 * representation of the chip, so use that directly.
	 *
	 * The info is either pre-populated in mtk_soc_try_early_detect() or
	 * it is left at its default value of "unknown " if it could not be
	 * obtained for some reason.
	 */
	for (idx = 0; idx < offset; idx++) {
		cpu_model[idx] = chipid0_3[idx];
		cpu_model[idx + offset] = chipid4_7[idx];
	}

	/* Null-terminate the string */
	cpu_model[2 * offset] = 0;
}

uint32_t
mtk_soc_get_uartclk(void)
{

	return mtk_soc_uartclk;
}

uint32_t
mtk_soc_get_cpuclk(void)
{

	return mtk_soc_cpuclk;
}

uint32_t
mtk_soc_get_timerclk(void)
{

	return mtk_soc_timerclk;
}

uint32_t
mtk_soc_get_socid(void)
{

	return mtk_soc_socid;
}

/*
 * The following are generic reset and clock functions
 */

/* Default reset time is 100ms */
#define DEFAULT_RESET_TIME	100000

int
mtk_soc_reset_device(device_t dev)
{
	int res;

	res = fdt_reset_assert_all(dev);
	if (res == 0) {
		DELAY(DEFAULT_RESET_TIME);
		res = fdt_reset_deassert_all(dev);
		if (res == 0)
			DELAY(DEFAULT_RESET_TIME);
	}

	return (res);
}

int
mtk_soc_stop_clock(device_t dev)
{

	return (fdt_clock_disable_all(dev));
}

int
mtk_soc_start_clock(device_t dev)
{

	return (fdt_clock_enable_all(dev));
}

int
mtk_soc_assert_reset(device_t dev)
{

	return (fdt_reset_assert_all(dev));
}

int
mtk_soc_deassert_reset(device_t dev)
{

	return (fdt_reset_deassert_all(dev));
}

void
mtk_soc_reset(void)
{

	mtk_sysctl_clr_set(SYSCTL_RSTCTRL, 0, 1);
	mtk_sysctl_clr_set(SYSCTL_RSTCTRL, 1, 0);
}
