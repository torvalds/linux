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
#include <mips/atheros/qca953xreg.h>

#include <mips/atheros/ar71xx_cpudef.h>
#include <mips/atheros/ar71xx_setup.h>

#include <mips/atheros/ar71xx_chip.h>

#include <mips/atheros/qca953x_chip.h>

static void
qca953x_chip_detect_mem_size(void)
{
}

static void
qca953x_chip_detect_sys_frequency(void)
{
	unsigned long ref_rate;
	unsigned long cpu_rate;
	unsigned long ddr_rate;
	unsigned long ahb_rate;
	uint32_t pll, out_div, ref_div, nint, frac, clk_ctrl, postdiv;
	uint32_t cpu_pll, ddr_pll;
	uint32_t bootstrap;

	bootstrap = ATH_READ_REG(QCA953X_RESET_REG_BOOTSTRAP);
	if (bootstrap &	QCA953X_BOOTSTRAP_REF_CLK_40)
		ref_rate = 40 * 1000 * 1000;
	else
		ref_rate = 25 * 1000 * 1000;

	pll = ATH_READ_REG(QCA953X_PLL_CPU_CONFIG_REG);
	out_div = (pll >> QCA953X_PLL_CPU_CONFIG_OUTDIV_SHIFT) &
		  QCA953X_PLL_CPU_CONFIG_OUTDIV_MASK;
	ref_div = (pll >> QCA953X_PLL_CPU_CONFIG_REFDIV_SHIFT) &
		  QCA953X_PLL_CPU_CONFIG_REFDIV_MASK;
	nint = (pll >> QCA953X_PLL_CPU_CONFIG_NINT_SHIFT) &
	       QCA953X_PLL_CPU_CONFIG_NINT_MASK;
	frac = (pll >> QCA953X_PLL_CPU_CONFIG_NFRAC_SHIFT) &
	       QCA953X_PLL_CPU_CONFIG_NFRAC_MASK;

	cpu_pll = nint * ref_rate / ref_div;
	cpu_pll += frac * (ref_rate >> 6) / ref_div;
	cpu_pll /= (1 << out_div);

	pll = ATH_READ_REG(QCA953X_PLL_DDR_CONFIG_REG);
	out_div = (pll >> QCA953X_PLL_DDR_CONFIG_OUTDIV_SHIFT) &
		  QCA953X_PLL_DDR_CONFIG_OUTDIV_MASK;
	ref_div = (pll >> QCA953X_PLL_DDR_CONFIG_REFDIV_SHIFT) &
		  QCA953X_PLL_DDR_CONFIG_REFDIV_MASK;
	nint = (pll >> QCA953X_PLL_DDR_CONFIG_NINT_SHIFT) &
	       QCA953X_PLL_DDR_CONFIG_NINT_MASK;
	frac = (pll >> QCA953X_PLL_DDR_CONFIG_NFRAC_SHIFT) &
	       QCA953X_PLL_DDR_CONFIG_NFRAC_MASK;

	ddr_pll = nint * ref_rate / ref_div;
	ddr_pll += frac * (ref_rate >> 6) / (ref_div << 4);
	ddr_pll /= (1 << out_div);

	clk_ctrl = ATH_READ_REG(QCA953X_PLL_CLK_CTRL_REG);

	postdiv = (clk_ctrl >> QCA953X_PLL_CLK_CTRL_CPU_POST_DIV_SHIFT) &
		  QCA953X_PLL_CLK_CTRL_CPU_POST_DIV_MASK;

	if (clk_ctrl & QCA953X_PLL_CLK_CTRL_CPU_PLL_BYPASS)
		cpu_rate = ref_rate;
	else if (clk_ctrl & QCA953X_PLL_CLK_CTRL_CPUCLK_FROM_CPUPLL)
		cpu_rate = cpu_pll / (postdiv + 1);
	else
		cpu_rate = ddr_pll / (postdiv + 1);

	postdiv = (clk_ctrl >> QCA953X_PLL_CLK_CTRL_DDR_POST_DIV_SHIFT) &
		  QCA953X_PLL_CLK_CTRL_DDR_POST_DIV_MASK;

	if (clk_ctrl & QCA953X_PLL_CLK_CTRL_DDR_PLL_BYPASS)
		ddr_rate = ref_rate;
	else if (clk_ctrl & QCA953X_PLL_CLK_CTRL_DDRCLK_FROM_DDRPLL)
		ddr_rate = ddr_pll / (postdiv + 1);
	else
		ddr_rate = cpu_pll / (postdiv + 1);

	postdiv = (clk_ctrl >> QCA953X_PLL_CLK_CTRL_AHB_POST_DIV_SHIFT) &
		  QCA953X_PLL_CLK_CTRL_AHB_POST_DIV_MASK;

	if (clk_ctrl & QCA953X_PLL_CLK_CTRL_AHB_PLL_BYPASS)
		ahb_rate = ref_rate;
	else if (clk_ctrl & QCA953X_PLL_CLK_CTRL_AHBCLK_FROM_DDRPLL)
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
qca953x_chip_device_stop(uint32_t mask)
{
	uint32_t reg;

	reg = ATH_READ_REG(QCA953X_RESET_REG_RESET_MODULE);
	ATH_WRITE_REG(QCA953X_RESET_REG_RESET_MODULE, reg | mask);
}

static void
qca953x_chip_device_start(uint32_t mask)
{
	uint32_t reg;

	reg = ATH_READ_REG(QCA953X_RESET_REG_RESET_MODULE);
	ATH_WRITE_REG(QCA953X_RESET_REG_RESET_MODULE, reg & ~mask);
}

static int
qca953x_chip_device_stopped(uint32_t mask)
{
	uint32_t reg;

	reg = ATH_READ_REG(QCA953X_RESET_REG_RESET_MODULE);
	return ((reg & mask) == mask);
}

static void
qca953x_chip_set_mii_speed(uint32_t unit, uint32_t speed)
{

	/* XXX TODO */
	return;
}

static void
qca953x_chip_set_pll_ge(int unit, int speed, uint32_t pll)
{
	switch (unit) {
	case 0:
		ATH_WRITE_REG(QCA953X_PLL_ETH_XMII_CONTROL_REG, pll);
		break;
	case 1:
		/* nothing */
		break;
	default:
		printf("%s: invalid PLL set for arge unit: %d\n",
		    __func__, unit);
		return;
	}
}

static void
qca953x_chip_ddr_flush(ar71xx_flush_ddr_id_t id)
{

	switch (id) {
	case AR71XX_CPU_DDR_FLUSH_GE0:
		ar71xx_ddr_flush(QCA953X_DDR_REG_FLUSH_GE0);
		break;
	case AR71XX_CPU_DDR_FLUSH_GE1:
		ar71xx_ddr_flush(QCA953X_DDR_REG_FLUSH_GE1);
		break;
	case AR71XX_CPU_DDR_FLUSH_USB:
		ar71xx_ddr_flush(QCA953X_DDR_REG_FLUSH_USB);
		break;
	case AR71XX_CPU_DDR_FLUSH_PCIE:
		ar71xx_ddr_flush(QCA953X_DDR_REG_FLUSH_PCIE);
		break;
	case AR71XX_CPU_DDR_FLUSH_WMAC:
		ar71xx_ddr_flush(QCA953X_DDR_REG_FLUSH_WMAC);
		break;
	default:
		printf("%s: invalid flush (%d)\n", __func__, id);
	}
}

static uint32_t
qca953x_chip_get_eth_pll(unsigned int mac, int speed)
{
	uint32_t pll;

	switch (speed) {
	case 10:
		pll = QCA953X_PLL_VAL_10;
		break;
	case 100:
		pll = QCA953X_PLL_VAL_100;
		break;
	case 1000:
		pll = QCA953X_PLL_VAL_1000;
		break;
	default:
		printf("%s%d: invalid speed %d\n", __func__, mac, speed);
		pll = 0;
	}
	return (pll);
}

static void
qca953x_chip_reset_ethernet_switch(void)
{
}

static void
qca953x_configure_gmac(uint32_t gmac_cfg)
{
	uint32_t reg;

	reg = ATH_READ_REG(QCA953X_GMAC_REG_ETH_CFG);
	printf("%s: ETH_CFG=0x%08x\n", __func__, reg);
	reg &= ~(QCA953X_ETH_CFG_SW_ONLY_MODE |
	    QCA953X_ETH_CFG_SW_PHY_SWAP |
	    QCA953X_ETH_CFG_SW_APB_ACCESS |
	    QCA953X_ETH_CFG_SW_ACC_MSB_FIRST);

	reg |= gmac_cfg;
	ATH_WRITE_REG(QCA953X_GMAC_REG_ETH_CFG, reg);
}

static void
qca953x_chip_init_usb_peripheral(void)
{
	uint32_t bootstrap;

	bootstrap = ATH_READ_REG(QCA953X_RESET_REG_BOOTSTRAP);

	ar71xx_device_stop(QCA953X_RESET_USBSUS_OVERRIDE);
	DELAY(1000);

	ar71xx_device_start(QCA953X_RESET_USB_PHY);
	DELAY(1000);

	ar71xx_device_start(QCA953X_RESET_USB_PHY_ANALOG);
	DELAY(1000);

	ar71xx_device_start(QCA953X_RESET_USB_HOST);
	DELAY(1000);
}

static void
qca953x_chip_set_mii_if(uint32_t unit, uint32_t mii_mode)
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
qca953x_chip_reset_wmac(void)
{

	/* XXX TODO */
}

static void
qca953x_chip_init_gmac(void)
{
	long gmac_cfg;

	if (resource_long_value("qca953x_gmac", 0, "gmac_cfg",
	    &gmac_cfg) == 0) {
		printf("%s: gmac_cfg=0x%08lx\n",
		    __func__,
		    (long) gmac_cfg);
		qca953x_configure_gmac((uint32_t) gmac_cfg);
	}
}

/*
 * Reset the NAND Flash Controller.
 *
 * + active=1 means "make it active".
 * + active=0 means "make it inactive".
 */
static void
qca953x_chip_reset_nfc(int active)
{
}

/*
 * Configure the GPIO output mux setup.
 *
 * The QCA953x has an output mux which allowed
 * certain functions to be configured on any pin.
 * Specifically, the switch PHY link LEDs and
 * WMAC external RX LNA switches are not limited to
 * a specific GPIO pin.
 */
static void
qca953x_chip_gpio_output_configure(int gpio, uint8_t func)
{
	uint32_t reg, s;
	uint32_t t;

	if (gpio > QCA953X_GPIO_COUNT)
		return;

	reg = QCA953X_GPIO_REG_OUT_FUNC0 + rounddown(gpio, 4);
	s = 8 * (gpio % 4);

	/* read-modify-write */
	t = ATH_READ_REG(AR71XX_GPIO_BASE + reg);
	t &= ~(0xff << s);
	t |= func << s;
	ATH_WRITE_REG(AR71XX_GPIO_BASE + reg, t);

	/* flush write */
	ATH_READ_REG(AR71XX_GPIO_BASE + reg);
}

struct ar71xx_cpu_def qca953x_chip_def = {
	&qca953x_chip_detect_mem_size,
	&qca953x_chip_detect_sys_frequency,
	&qca953x_chip_device_stop,
	&qca953x_chip_device_start,
	&qca953x_chip_device_stopped,
	&qca953x_chip_set_pll_ge,
	&qca953x_chip_set_mii_speed,
	&qca953x_chip_set_mii_if,
	&qca953x_chip_get_eth_pll,
	&qca953x_chip_ddr_flush,
	&qca953x_chip_init_usb_peripheral,
	&qca953x_chip_reset_ethernet_switch,
	&qca953x_chip_reset_wmac,
	&qca953x_chip_init_gmac,
	&qca953x_chip_reset_nfc,
	&qca953x_chip_gpio_output_configure,
};
