/*-
 * Copyright (c) 2016 Stanislav Galabov.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 *
 * $FreeBSD$
 */

#ifndef _MTK_PINCTRL_H_
#define _MTK_PINCTRL_H_

struct mtk_pin_function {
	const char		*name;
	uint32_t		value;
};

struct mtk_pin_group {
	const char		*name;
	uint32_t		sysc_reg;
	uint32_t		offset;
	uint32_t		mask;
	struct mtk_pin_function	*functions;
	uint32_t		funcnum;
};

#define FUNC(_name, _value)			\
    { .name = (_name), .value = (_value) }

#define GROUP(_name, _reg, _off, _mask, _funcs)	\
    { .name = (_name), .sysc_reg = (_reg), .offset = (_off),		\
    .mask = (_mask), .functions = (_funcs), .funcnum = nitems(_funcs) }

#define GROUP_END	{ NULL, 0, 0, 0, NULL, 0 }

#define DECL_FUNC(_name)	\
    static struct mtk_pin_function _name[]
#define DECL_TABLE(_name)	\
    static struct mtk_pin_group _name[]

/* Pin function declarations */
DECL_FUNC(i2c_func) = {
	FUNC("i2c", 0), FUNC("gpio", 1)
};

DECL_FUNC(spi_func) = {
	FUNC("spi", 0), FUNC("gpio", 1)
};

DECL_FUNC(uartf_func) = {
	FUNC("uartf", 0), FUNC("pcm uartf", 1), FUNC("pcm i2s", 2),
	FUNC("i2s uartf", 3), FUNC("pcm gpio", 4), FUNC("gpio uartf", 5),
	FUNC("gpio i2s", 6), FUNC("gpio", 7)
};

DECL_FUNC(wdt_func) = {
	FUNC("wdt rst", 0), FUNC("wdt", 0), FUNC("wdt refclk", 1),
	FUNC("gpio", 2)
};

DECL_FUNC(uartlite_func) = {
	FUNC("uartlite", 0), FUNC("gpio", 1)
};

DECL_FUNC(jtag_func) = {
	FUNC("jtag", 0), FUNC("gpio", 1)
};

DECL_FUNC(mdio_func) = {
	FUNC("mdio", 0), FUNC("gpio", 1)
};

DECL_FUNC(led_func) = {
	FUNC("led", 0), FUNC("gpio", 1), FUNC("bt", 2)
};

DECL_FUNC(cs1_func) = {
	FUNC("spi_cs1", 0), FUNC("wdt_cs1", 1), FUNC("gpio", 2)
};

DECL_FUNC(sdram_func) = {
	FUNC("sdram", 0), FUNC("gpio", 1)
};

DECL_FUNC(rgmii_func) = {
	FUNC("rgmii", 0), FUNC("rgmii1", 0), FUNC("rgmii2", 0), FUNC("gpio", 1)
};

DECL_FUNC(lna_func) = {
	FUNC("lna", 0), FUNC("gpio", 1)
};

DECL_FUNC(pa_func) = {
	FUNC("pa", 0), FUNC("gpio", 1)
};

DECL_FUNC(gex_func) = {
	FUNC("ge1", 0), FUNC("ge2", 0), FUNC("gpio", 1)
};

DECL_FUNC(rt2880_uartf_func) = {
	FUNC("uartf", 0), FUNC("gpio", 1)
};

DECL_FUNC(rt2880_pci_func) = {
	FUNC("pci", 0), FUNC("gpio", 1)
};

DECL_FUNC(rt3883_pci_func) = {
	FUNC("pci-dev", 0), FUNC("pci-host2", 1), FUNC("pci-host1", 2),
	FUNC("pci-fnc", 3), FUNC("gpio", 7)
};

DECL_FUNC(mt7620_pcie_func) = {
	FUNC("pcie rst", 0), FUNC("pcie refclk", 1), FUNC("gpio", 2)
};

DECL_FUNC(lna_a_func) = {
	FUNC("lna a", 0), FUNC("lna g", 0), FUNC("codec", 2), FUNC("gpio", 3)
};

DECL_FUNC(nd_sd_func) = {
	FUNC("nand", 0), FUNC("sd", 1), FUNC("gpio", 2)
};

DECL_FUNC(mt7620_mdio_func) = {
	FUNC("mdio", 0), FUNC("mdio refclk", 1), FUNC("gpio", 2)
};

DECL_FUNC(spi_refclk_func) = {
	FUNC("spi refclk", 0), FUNC("gpio", 1)
};

DECL_FUNC(wled_func) = {
	FUNC("wled", 0), FUNC("gpio", 1)
};

DECL_FUNC(ephy_func) = {
	FUNC("ephy", 0), FUNC("gpio", 1)
};

DECL_FUNC(mt7628_gpio_func) = {
	FUNC("gpio", 0), FUNC("gpio", 1), FUNC("refclk", 2), FUNC("pcie", 3)
};

DECL_FUNC(mt7628_spis_func) = {
	FUNC("spis", 0), FUNC("gpio", 1), FUNC("utif", 2), FUNC("pwm", 3)
};

DECL_FUNC(mt7628_spi_cs1_func) = {
	FUNC("spi", 0), FUNC("gpio", 1), FUNC("refclk", 2), FUNC("-", 3)
};

DECL_FUNC(mt7628_i2s_func) = {
	FUNC("i2s", 0), FUNC("gpio", 1), FUNC("pcm", 2), FUNC("anttenna", 3)
};

DECL_FUNC(mt7628_uart0_func) = {
	FUNC("uart0", 0), FUNC("gpio", 1), FUNC("-", 2), FUNC("-", 3)
};

DECL_FUNC(mt7628_sd_func) = {
	FUNC("sdxc", 0), FUNC("gpio", 1), FUNC("utif", 2), FUNC("jtag", 3)
};

DECL_FUNC(mt7628_perst_func) = {
	FUNC("perst", 0), FUNC("gpio", 1)
};

DECL_FUNC(mt7628_refclk_func) = {
	FUNC("refclk", 0), FUNC("gpio", 1)
};

DECL_FUNC(mt7628_i2c_func) = {
	FUNC("i2c", 0), FUNC("gpio", 1), FUNC("debug", 2), FUNC("-", 3)
};

DECL_FUNC(mt7628_uart1_func) = {
	FUNC("uart1", 0), FUNC("gpio", 1), FUNC("pwm", 2), FUNC("sw r", 3)
};

DECL_FUNC(mt7628_uart2_func) = {
	FUNC("uart2", 0), FUNC("gpio", 1), FUNC("pwm", 2), FUNC("sdxc", 3)
};

DECL_FUNC(mt7628_pwm0_func) = {
	FUNC("pwm", 0), FUNC("gpio", 1), FUNC("utif", 2), FUNC("sdxc", 3)
};

DECL_FUNC(mt7621_uart1_func) = {
	FUNC("uart1", 0), FUNC("gpio", 1)
};

DECL_FUNC(mt7621_i2c_func) = {
	FUNC("i2c", 0), FUNC("gpio", 1)
};

DECL_FUNC(mt7621_uart3_func) = {
	FUNC("uart3", 0), FUNC("gpio", 1), FUNC("i2s", 2), FUNC("spdif3", 3)
};

DECL_FUNC(mt7621_uart2_func) = {
	FUNC("uart2", 0), FUNC("gpio", 1), FUNC("pcm", 2), FUNC("spdif2", 3)
};

DECL_FUNC(mt7621_jtag_func) = {
	FUNC("jtag", 0), FUNC("gpio", 1)
};

DECL_FUNC(mt7621_wdt_func) = {
	FUNC("wdt rst", 0), FUNC("gpio", 1), FUNC("wdt refclk", 2), FUNC("-", 3)
};

DECL_FUNC(mt7621_pcie_func) = {
	FUNC("pcie rst", 0), FUNC("gpio", 1), FUNC("pcie refclk", 2),
	FUNC("-", 3)
};

DECL_FUNC(mt7621_mdio_func) = {
	FUNC("mdio", 0), FUNC("gpio", 1), FUNC("-", 2), FUNC("-", 3)
};

DECL_FUNC(mt7621_rgmii_func) = {
	FUNC("rgmii1", 0), FUNC("rgmii2", 0), FUNC("gpio", 1)
};

DECL_FUNC(mt7621_spi_func) = {
	FUNC("spi", 0), FUNC("gpio", 1), FUNC("nand1", 2), FUNC("-", 3)
};

DECL_FUNC(mt7621_sdhci_func) = {
	FUNC("sdhci", 0), FUNC("gpio", 1), FUNC("nand1", 2), FUNC("-", 3)
};

/* Pin groups declarations */
DECL_TABLE(mt7628_pintable) = {
	GROUP("gpio", SYSCTL_GPIOMODE, 0, 3, mt7628_gpio_func),
	GROUP("spis", SYSCTL_GPIOMODE, 2, 3, mt7628_spis_func),
	GROUP("spi cs1", SYSCTL_GPIOMODE, 4, 3, mt7628_spi_cs1_func),
	GROUP("i2s", SYSCTL_GPIOMODE, 6, 3, mt7628_i2s_func),
	GROUP("uart0", SYSCTL_GPIOMODE, 8, 3, mt7628_uart0_func),
	GROUP("sdmode", SYSCTL_GPIOMODE, 10, 3, mt7628_sd_func),
	GROUP("spi", SYSCTL_GPIOMODE, 12, 1, spi_func),
	GROUP("wdt", SYSCTL_GPIOMODE, 14, 1, wdt_func),
	GROUP("perst", SYSCTL_GPIOMODE, 16, 1, mt7628_perst_func),
	GROUP("refclk", SYSCTL_GPIOMODE, 18, 1, mt7628_refclk_func),
	GROUP("i2c", SYSCTL_GPIOMODE, 20, 3, mt7628_i2c_func),
	GROUP("uart1", SYSCTL_GPIOMODE, 24, 3, mt7628_uart1_func),
	GROUP("uart2", SYSCTL_GPIOMODE, 26, 3, mt7628_uart2_func),
	GROUP("pwm0", SYSCTL_GPIOMODE, 28, 3, mt7628_pwm0_func),
	GROUP("pwm1", SYSCTL_GPIOMODE, 30, 3, mt7628_pwm0_func),
	GROUP_END
};

DECL_TABLE(mt7621_pintable) = {
	GROUP("uart1", SYSCTL_GPIOMODE, 1, 1, mt7621_uart1_func),
	GROUP("i2c", SYSCTL_GPIOMODE, 2, 1, mt7621_i2c_func),
	GROUP("uart3", SYSCTL_GPIOMODE, 3, 3, mt7621_uart3_func),
	GROUP("uart2", SYSCTL_GPIOMODE, 5, 3, mt7621_uart2_func),
	GROUP("jtag", SYSCTL_GPIOMODE, 7, 1, mt7621_jtag_func),
	GROUP("wdt", SYSCTL_GPIOMODE, 8, 3, mt7621_wdt_func),
	GROUP("pcie", SYSCTL_GPIOMODE, 10, 3, mt7621_pcie_func),
	GROUP("mdio", SYSCTL_GPIOMODE, 12, 3, mt7621_mdio_func),
	GROUP("rgmii2", SYSCTL_GPIOMODE, 15, 1, mt7621_rgmii_func),
	GROUP("spi", SYSCTL_GPIOMODE, 16, 3, mt7621_spi_func),
	GROUP("sdhci", SYSCTL_GPIOMODE, 18, 3, mt7621_sdhci_func),
	GROUP("rgmii1", SYSCTL_GPIOMODE, 14, 1, mt7621_rgmii_func),
	GROUP_END
};

DECL_TABLE(mt7620_pintable) = {
	GROUP("i2c", SYSCTL_GPIOMODE, 0, 1, i2c_func),
	GROUP("uartf", SYSCTL_GPIOMODE, 2, 7, uartf_func),
	GROUP("uartlite", SYSCTL_GPIOMODE, 5, 1, uartlite_func),
	GROUP("mdio", SYSCTL_GPIOMODE, 7, 3, mt7620_mdio_func),
	GROUP("rgmii1", SYSCTL_GPIOMODE, 9, 1, rgmii_func),
	GROUP("rgmii2", SYSCTL_GPIOMODE, 10, 1, rgmii_func),
	GROUP("spi", SYSCTL_GPIOMODE, 11, 1, spi_func),
	GROUP("spi refclk", SYSCTL_GPIOMODE, 12, 1, spi_refclk_func),
	GROUP("wled", SYSCTL_GPIOMODE, 13, 1, wled_func),
	GROUP("ephy", SYSCTL_GPIOMODE, 15, 1, ephy_func),
	GROUP("pcie", SYSCTL_GPIOMODE, 16, 3, mt7620_pcie_func),
	GROUP("nd_sd", SYSCTL_GPIOMODE, 18, 3, nd_sd_func),
	GROUP("pa", SYSCTL_GPIOMODE, 20, 1, pa_func),
	GROUP("wdt", SYSCTL_GPIOMODE, 21, 3, wdt_func),
	GROUP_END
};

DECL_TABLE(rt2880_pintable) = {
	GROUP("i2c", SYSCTL_GPIOMODE, 0, 1, i2c_func),
	GROUP("uartf", SYSCTL_GPIOMODE, 1, 1, rt2880_uartf_func),
	GROUP("spi", SYSCTL_GPIOMODE, 2, 1, spi_func),
	GROUP("uartlite", SYSCTL_GPIOMODE, 3, 1, uartlite_func),
	GROUP("jtag", SYSCTL_GPIOMODE, 4, 1, jtag_func),
	GROUP("mdio", SYSCTL_GPIOMODE, 5, 1, mdio_func),
	GROUP("sdram", SYSCTL_GPIOMODE, 6, 1, sdram_func),
	GROUP("pci", SYSCTL_GPIOMODE, 7, 1, rt2880_pci_func),
	GROUP_END
};

DECL_TABLE(rt3050_pintable) = {
	GROUP("i2c", SYSCTL_GPIOMODE, 0, 1, i2c_func),
	GROUP("spi", SYSCTL_GPIOMODE, 1, 1, spi_func),
	GROUP("uartf", SYSCTL_GPIOMODE, 2, 7, uartf_func),
	GROUP("uartlite", SYSCTL_GPIOMODE, 5, 1, uartlite_func),
	GROUP("jtag", SYSCTL_GPIOMODE, 6, 1, jtag_func),
	GROUP("mdio", SYSCTL_GPIOMODE, 7, 1, mdio_func),
	GROUP("sdram", SYSCTL_GPIOMODE, 8, 1, sdram_func),
	GROUP("rgmii", SYSCTL_GPIOMODE, 9, 1, rgmii_func),
	GROUP_END
};

DECL_TABLE(rt3352_pintable) = {
	GROUP("i2c", SYSCTL_GPIOMODE, 0, 1, i2c_func),
	GROUP("spi", SYSCTL_GPIOMODE, 1, 1, i2c_func),
	GROUP("uartf", SYSCTL_GPIOMODE, 2, 7, uartf_func),
	GROUP("uartlite", SYSCTL_GPIOMODE, 5, 1, uartlite_func),
	GROUP("jtag", SYSCTL_GPIOMODE, 6, 1, jtag_func),
	GROUP("mdio", SYSCTL_GPIOMODE, 7, 1, mdio_func),
	GROUP("rgmii", SYSCTL_GPIOMODE, 9, 1, rgmii_func),
	GROUP("led", SYSCTL_GPIOMODE, 14, 3, led_func),
	GROUP("lna", SYSCTL_GPIOMODE, 18, 1, lna_func),
	GROUP("pa", SYSCTL_GPIOMODE, 20, 1, pa_func),
	GROUP("spi_cs1", SYSCTL_GPIOMODE, 21, 3, cs1_func),
	GROUP_END
};

DECL_TABLE(rt3883_pintable) = {
	GROUP("i2c", SYSCTL_GPIOMODE, 0, 1, i2c_func),
	GROUP("spi", SYSCTL_GPIOMODE, 1, 1, spi_func),
	GROUP("uartf", SYSCTL_GPIOMODE, 2, 7, uartf_func),
	GROUP("uartlite", SYSCTL_GPIOMODE, 5, 1, uartlite_func),
	GROUP("jtag", SYSCTL_GPIOMODE, 6, 1, jtag_func),
	GROUP("mdio", SYSCTL_GPIOMODE, 7, 1, mdio_func),
	GROUP("lna a", SYSCTL_GPIOMODE, 16, 3, lna_a_func),
	GROUP("lna g", SYSCTL_GPIOMODE, 18, 3, lna_a_func),
	GROUP("pci", SYSCTL_GPIOMODE, 11, 7, rt3883_pci_func),
	GROUP("ge1", SYSCTL_GPIOMODE, 9, 1, gex_func),
	GROUP("ge2", SYSCTL_GPIOMODE, 10, 1, gex_func),
	GROUP_END
};

DECL_TABLE(rt5350_pintable) = {
	GROUP("i2c", SYSCTL_GPIOMODE, 0, 1, i2c_func),
	GROUP("spi", SYSCTL_GPIOMODE, 1, 1, spi_func),
	GROUP("uartf", SYSCTL_GPIOMODE, 2, 7, uartf_func),
	GROUP("uartlite", SYSCTL_GPIOMODE, 5, 1, uartlite_func),
	GROUP("jtag", SYSCTL_GPIOMODE, 6, 1, jtag_func),
	GROUP("led", SYSCTL_GPIOMODE, 14, 3, led_func),
	GROUP("spi_cs1", SYSCTL_GPIOMODE, 21, 3, cs1_func),
	GROUP_END
};

#endif /* _MTK_PINCTRL_H_ */
