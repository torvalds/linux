/*-
 * Copyright (c) 2015 Adrian Chadd <adrian@FreeBSD.org>
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

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/cons.h>
#include <sys/kdb.h>
#include <sys/reboot.h>

#include <vm/vm.h>
#include <vm/vm_page.h>

#include <net/ethernet.h>

#include <machine/clock.h>
#include <machine/cpu.h>
#include <machine/cpuregs.h>
#include <machine/hwfunc.h>
#include <machine/md_var.h>
#include <machine/trap.h>
#include <machine/vmparam.h>

#include <mips/atheros/ar71xxreg.h>
//#include <mips/atheros/ar934xreg.h>
#include <mips/atheros/qca955xreg.h>

#include <mips/atheros/ar71xx_cpudef.h>
#include <mips/atheros/ar71xx_setup.h>

#include <mips/atheros/ar71xx_chip.h>

#include <mips/atheros/qca955x_chip.h>

static void
qca955x_chip_detect_mem_size(void)
{
}

static void
qca955x_chip_detect_sys_frequency(void)
{
	unsigned long ref_rate;
	unsigned long cpu_rate;
	unsigned long ddr_rate;
	unsigned long ahb_rate;
	uint32_t pll, out_div, ref_div, nint, frac, clk_ctrl, postdiv;
	uint32_t cpu_pll, ddr_pll;
	uint32_t bootstrap;

	bootstrap = ATH_READ_REG(QCA955X_RESET_REG_BOOTSTRAP);
	if (bootstrap &	QCA955X_BOOTSTRAP_REF_CLK_40)
		ref_rate = 40 * 1000 * 1000;
	else
		ref_rate = 25 * 1000 * 1000;

	pll = ATH_READ_REG(QCA955X_PLL_CPU_CONFIG_REG);
	out_div = (pll >> QCA955X_PLL_CPU_CONFIG_OUTDIV_SHIFT) &
		  QCA955X_PLL_CPU_CONFIG_OUTDIV_MASK;
	ref_div = (pll >> QCA955X_PLL_CPU_CONFIG_REFDIV_SHIFT) &
		  QCA955X_PLL_CPU_CONFIG_REFDIV_MASK;
	nint = (pll >> QCA955X_PLL_CPU_CONFIG_NINT_SHIFT) &
	       QCA955X_PLL_CPU_CONFIG_NINT_MASK;
	frac = (pll >> QCA955X_PLL_CPU_CONFIG_NFRAC_SHIFT) &
	       QCA955X_PLL_CPU_CONFIG_NFRAC_MASK;

	cpu_pll = nint * ref_rate / ref_div;
	cpu_pll += frac * ref_rate / (ref_div * (1 << 6));
	cpu_pll /= (1 << out_div);

	pll = ATH_READ_REG(QCA955X_PLL_DDR_CONFIG_REG);
	out_div = (pll >> QCA955X_PLL_DDR_CONFIG_OUTDIV_SHIFT) &
		  QCA955X_PLL_DDR_CONFIG_OUTDIV_MASK;
	ref_div = (pll >> QCA955X_PLL_DDR_CONFIG_REFDIV_SHIFT) &
		  QCA955X_PLL_DDR_CONFIG_REFDIV_MASK;
	nint = (pll >> QCA955X_PLL_DDR_CONFIG_NINT_SHIFT) &
	       QCA955X_PLL_DDR_CONFIG_NINT_MASK;
	frac = (pll >> QCA955X_PLL_DDR_CONFIG_NFRAC_SHIFT) &
	       QCA955X_PLL_DDR_CONFIG_NFRAC_MASK;

	ddr_pll = nint * ref_rate / ref_div;
	ddr_pll += frac * ref_rate / (ref_div * (1 << 10));
	ddr_pll /= (1 << out_div);

	clk_ctrl = ATH_READ_REG(QCA955X_PLL_CLK_CTRL_REG);

	postdiv = (clk_ctrl >> QCA955X_PLL_CLK_CTRL_CPU_POST_DIV_SHIFT) &
		  QCA955X_PLL_CLK_CTRL_CPU_POST_DIV_MASK;

	if (clk_ctrl & QCA955X_PLL_CLK_CTRL_CPU_PLL_BYPASS)
		cpu_rate = ref_rate;
	else if (clk_ctrl & QCA955X_PLL_CLK_CTRL_CPUCLK_FROM_CPUPLL)
		cpu_rate = ddr_pll / (postdiv + 1);
	else
		cpu_rate = cpu_pll / (postdiv + 1);

	postdiv = (clk_ctrl >> QCA955X_PLL_CLK_CTRL_DDR_POST_DIV_SHIFT) &
		  QCA955X_PLL_CLK_CTRL_DDR_POST_DIV_MASK;

	if (clk_ctrl & QCA955X_PLL_CLK_CTRL_DDR_PLL_BYPASS)
		ddr_rate = ref_rate;
	else if (clk_ctrl & QCA955X_PLL_CLK_CTRL_DDRCLK_FROM_DDRPLL)
		ddr_rate = cpu_pll / (postdiv + 1);
	else
		ddr_rate = ddr_pll / (postdiv + 1);

	postdiv = (clk_ctrl >> QCA955X_PLL_CLK_CTRL_AHB_POST_DIV_SHIFT) &
		  QCA955X_PLL_CLK_CTRL_AHB_POST_DIV_MASK;

	if (clk_ctrl & QCA955X_PLL_CLK_CTRL_AHB_PLL_BYPASS)
		ahb_rate = ref_rate;
	else if (clk_ctrl & QCA955X_PLL_CLK_CTRL_AHBCLK_FROM_DDRPLL)
		ahb_rate = ddr_pll / (postdiv + 1);
	else
		ahb_rate = cpu_pll / (postdiv + 1);

	u_ar71xx_ddr_freq = ddr_rate;
	u_ar71xx_cpu_freq = cpu_rate;
	u_ar71xx_ahb_freq = ahb_rate;

	u_ar71xx_wdt_freq = ref_rate;
	u_ar71xx_uart_freq = ref_rate;
	u_ar71xx_mdio_freq = ref_rate;
	u_ar71xx_refclk = ref_rate;
}

static void
qca955x_chip_device_stop(uint32_t mask)
{
	uint32_t reg;

	reg = ATH_READ_REG(QCA955X_RESET_REG_RESET_MODULE);
	ATH_WRITE_REG(QCA955X_RESET_REG_RESET_MODULE, reg | mask);
}

static void
qca955x_chip_device_start(uint32_t mask)
{
	uint32_t reg;

	reg = ATH_READ_REG(QCA955X_RESET_REG_RESET_MODULE);
	ATH_WRITE_REG(QCA955X_RESET_REG_RESET_MODULE, reg & ~mask);
}

static int
qca955x_chip_device_stopped(uint32_t mask)
{
	uint32_t reg;

	reg = ATH_READ_REG(QCA955X_RESET_REG_RESET_MODULE);
	return ((reg & mask) == mask);
}

static void
qca955x_chip_set_mii_speed(uint32_t unit, uint32_t speed)
{

	/* XXX TODO */
	return;
}

static void
qca955x_chip_set_pll_ge(int unit, int speed, uint32_t pll)
{
	switch (unit) {
	case 0:
		ATH_WRITE_REG(QCA955X_PLL_ETH_XMII_CONTROL_REG, pll);
		break;
	case 1:
		ATH_WRITE_REG(QCA955X_PLL_ETH_SGMII_CONTROL_REG, pll);
		break;
	default:
		printf("%s: invalid PLL set for arge unit: %d\n",
		    __func__, unit);
		return;
	}
}

static void
qca955x_chip_ddr_flush(ar71xx_flush_ddr_id_t id)
{

	switch (id) {
	case AR71XX_CPU_DDR_FLUSH_GE0:
		ar71xx_ddr_flush(QCA955X_DDR_REG_FLUSH_GE0);
		break;
	case AR71XX_CPU_DDR_FLUSH_GE1:
		ar71xx_ddr_flush(QCA955X_DDR_REG_FLUSH_GE1);
		break;
	case AR71XX_CPU_DDR_FLUSH_USB:
		ar71xx_ddr_flush(QCA955X_DDR_REG_FLUSH_USB);
		break;
	case AR71XX_CPU_DDR_FLUSH_PCIE:
		ar71xx_ddr_flush(QCA955X_DDR_REG_FLUSH_PCIE);
		break;
	case AR71XX_CPU_DDR_FLUSH_WMAC:
		ar71xx_ddr_flush(QCA955X_DDR_REG_FLUSH_WMAC);
		break;
	case AR71XX_CPU_DDR_FLUSH_PCIE_EP:
		ar71xx_ddr_flush(QCA955X_DDR_REG_FLUSH_SRC1);
		break;
	case AR71XX_CPU_DDR_FLUSH_CHECKSUM:
		ar71xx_ddr_flush(QCA955X_DDR_REG_FLUSH_SRC2);
		break;
	default:
		printf("%s: invalid flush (%d)\n", __func__, id);
	}
}

static uint32_t
qca955x_chip_get_eth_pll(unsigned int mac, int speed)
{
	uint32_t pll;

	switch (speed) {
	case 10:
		pll = QCA955X_PLL_VAL_10;
		break;
	case 100:
		pll = QCA955X_PLL_VAL_100;
		break;
	case 1000:
		pll = QCA955X_PLL_VAL_1000;
		break;
	default:
		printf("%s%d: invalid speed %d\n", __func__, mac, speed);
		pll = 0;
	}
	return (pll);
}

static void
qca955x_chip_reset_ethernet_switch(void)
{
#if 0
	ar71xx_device_stop(AR934X_RESET_ETH_SWITCH);
	DELAY(100);
	ar71xx_device_start(AR934X_RESET_ETH_SWITCH);
	DELAY(100);
#endif
}

static void
qca955x_configure_gmac(uint32_t gmac_cfg)
{
	uint32_t reg;

	reg = ATH_READ_REG(QCA955X_GMAC_REG_ETH_CFG);
	printf("%s: ETH_CFG=0x%08x\n", __func__, reg);
	reg &= ~(QCA955X_ETH_CFG_RGMII_EN | QCA955X_ETH_CFG_GE0_SGMII);
	reg |= gmac_cfg;
	ATH_WRITE_REG(QCA955X_GMAC_REG_ETH_CFG, reg);
}

static void
qca955x_chip_init_usb_peripheral(void)
{
}

static void
qca955x_chip_set_mii_if(uint32_t unit, uint32_t mii_mode)
{

	/*
	 * XXX !
	 *
	 * Nothing to see here; although gmac0 can have its
	 * MII configuration changed, the register values
	 * are slightly different.
	 */
}

/*
 * XXX TODO: fetch default MII divider configuration
 */

static void
qca955x_chip_reset_wmac(void)
{

	/* XXX TODO */
}

static void
qca955x_chip_init_gmac(void)
{
	long gmac_cfg;

	if (resource_long_value("qca955x_gmac", 0, "gmac_cfg",
	    &gmac_cfg) == 0) {
		printf("%s: gmac_cfg=0x%08lx\n",
		    __func__,
		    (long) gmac_cfg);
		qca955x_configure_gmac((uint32_t) gmac_cfg);
	}
}

/*
 * Reset the NAND Flash Controller.
 *
 * + active=1 means "make it active".
 * + active=0 means "make it inactive".
 */
static void
qca955x_chip_reset_nfc(int active)
{
#if 0
	if (active) {
		ar71xx_device_start(AR934X_RESET_NANDF);
		DELAY(100);

		ar71xx_device_start(AR934X_RESET_ETH_SWITCH_ANALOG);
		DELAY(250);
	} else {
		ar71xx_device_stop(AR934X_RESET_ETH_SWITCH_ANALOG);
		DELAY(250);

		ar71xx_device_stop(AR934X_RESET_NANDF);
		DELAY(100);
	}
#endif
}

/*
 * Configure the GPIO output mux setup.
 *
 * The QCA955x has an output mux which allowed
 * certain functions to be configured on any pin.
 * Specifically, the switch PHY link LEDs and
 * WMAC external RX LNA switches are not limited to
 * a specific GPIO pin.
 */
static void
qca955x_chip_gpio_output_configure(int gpio, uint8_t func)
{
	uint32_t reg, s;
	uint32_t t;

	if (gpio > QCA955X_GPIO_COUNT)
		return;

	reg = QCA955X_GPIO_REG_OUT_FUNC0 + rounddown(gpio, 4);
	s = 8 * (gpio % 4);

	/* read-modify-write */
	t = ATH_READ_REG(AR71XX_GPIO_BASE + reg);
	t &= ~(0xff << s);
	t |= func << s;
	ATH_WRITE_REG(AR71XX_GPIO_BASE + reg, t);

	/* flush write */
	ATH_READ_REG(AR71XX_GPIO_BASE + reg);
}

struct ar71xx_cpu_def qca955x_chip_def = {
	&qca955x_chip_detect_mem_size,
	&qca955x_chip_detect_sys_frequency,
	&qca955x_chip_device_stop,
	&qca955x_chip_device_start,
	&qca955x_chip_device_stopped,
	&qca955x_chip_set_pll_ge,
	&qca955x_chip_set_mii_speed,
	&qca955x_chip_set_mii_if,
	&qca955x_chip_get_eth_pll,
	&qca955x_chip_ddr_flush,
	&qca955x_chip_init_usb_peripheral,
	&qca955x_chip_reset_ethernet_switch,
	&qca955x_chip_reset_wmac,
	&qca955x_chip_init_gmac,
	&qca955x_chip_reset_nfc,
	&qca955x_chip_gpio_output_configure,
};
