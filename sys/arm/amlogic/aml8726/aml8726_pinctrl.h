/*-
 * Copyright 2015 John Wehle <john@feith.com>
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
 *
 * $FreeBSD$
 */

/*
 * In addition to supplying entries for pins which need to be configured
 * by the operating system it's also necessary to supply entries for pins
 * which may have been configured by the firmware for a different purpose.
 */

#ifndef	_ARM_AMLOGIC_AML8726_PINCTRL_H
#define	_ARM_AMLOGIC_AML8726_PINCTRL_H

enum aml8726_pinctrl_pull_mode {
	aml8726_unknown_pm,
	aml8726_disable_pm,
	aml8726_enable_pm,
	aml8726_enable_down_pm,
	aml8726_enable_up_pm
};

struct aml8726_pinctrl_pkg_pin {
	const char *pkg_name;
	boolean_t aobus;
	uint32_t pull_addr;
	uint32_t pull_bits;
};

struct aml8726_pinctrl_pin {
	const char *name;
	const char *pkg_name;
	uint32_t mux_addr;
	uint32_t mux_bits;
};

struct aml8726_pinctrl_function {
	const char *name;
	struct aml8726_pinctrl_pin *pins;
};

/*
 * aml8726-m3
 *
 *                 start     size
 * cbus mux        0x202c    36
 * cbus pu_pd      0x203a    24
 * cbus pull_en    0x203a    24
 * aobus mux       0x0005    4
 * aobus pu_pd     0x000b    4
 * aobus pull_en   0x000b    4
 */

static struct aml8726_pinctrl_pkg_pin aml8726_m3_pkg_pin[] = {
	{ "card_0", false, 0, 0x00000000 },
	{ "card_1", false, 0, 0x00000000 },
	{ "card_2", false, 0, 0x00000000 },
	{ "card_3", false, 0, 0x00000000 },
	{ "card_4", false, 0, 0x00000000 },
	{ "card_5", false, 0, 0x00000000 },
	{ "card_6", false, 0, 0x00000000 },

	{ "gpioc_10", false, 0, 0x00000000 },
	{ "gpioc_11", false, 0, 0x00000000 },
	{ "gpioc_12", false, 0, 0x00000000 },
	{ "gpioc_13", false, 0, 0x00000000 },

	{ "gpiox_13", false, 0, 0x00000000 },
	{ "gpiox_14", false, 0, 0x00000000 },
	{ "gpiox_15", false, 0, 0x00000000 },
	{ "gpiox_16", false, 0, 0x00000000 },
	{ "gpiox_17", false, 0, 0x00000000 },
	{ "gpiox_18", false, 0, 0x00000000 },
	{ "gpiox_19", false, 0, 0x00000000 },
	{ "gpiox_20", false, 0, 0x00000000 },
	{ "gpiox_21", false, 0, 0x00000000 },
	{ "gpiox_22", false, 0, 0x00000000 },
	{ "gpiox_23", false, 0, 0x00000000 },
	{ "gpiox_24", false, 0, 0x00000000 },
	{ "gpiox_25", false, 0, 0x00000000 },
	{ "gpiox_26", false, 0, 0x00000000 },
	{ "gpiox_27", false, 0, 0x00000000 },
	{ "gpiox_28", false, 0, 0x00000000 },

	{ "gpioy_0", false, 0, 0x00000000 },
	{ "gpioy_1", false, 0, 0x00000000 },
	{ "gpioy_2", false, 0, 0x00000000 },
	{ "gpioy_3", false, 0, 0x00000000 },
	{ "gpioy_4", false, 0, 0x00000000 },
	{ "gpioy_5", false, 0, 0x00000000 },
	{ "gpioy_6", false, 0, 0x00000000 },
	{ "gpioy_7", false, 0, 0x00000000 },
	{ "gpioy_8", false, 0, 0x00000000 },
	{ "gpioy_9", false, 0, 0x00000000 },

	{ "gpioao_0", true, 0, 0x00000000 },
	{ "gpioao_1", true, 0, 0x00000000 },
	{ "gpioao_2", true, 0, 0x00000000 },
	{ "gpioao_3", true, 0, 0x00000000 },
	{ "gpioao_4", true, 0, 0x00000000 },
	{ "gpioao_5", true, 0, 0x00000000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m3_gpio[] = {
	{ "card_0", "card_0", 0, 0x00000000 },
	{ "card_1", "card_1", 0, 0x00000000 },
	{ "card_2", "card_2", 0, 0x00000000 },
	{ "card_3", "card_3", 0, 0x00000000 },
	{ "card_4", "card_4", 0, 0x00000000 },
	{ "card_5", "card_5", 0, 0x00000000 },
	{ "card_6", "card_6", 0, 0x00000000 },

	{ "gpioc_10", "gpioc_10", 0, 0x00000000 },
	{ "gpioc_11", "gpioc_11", 0, 0x00000000 },
	{ "gpioc_12", "gpioc_12", 0, 0x00000000 },
	{ "gpioc_13", "gpioc_13", 0, 0x00000000 },

	{ "gpiox_13", "gpiox_13", 0, 0x00000000 },
	{ "gpiox_14", "gpiox_14", 0, 0x00000000 },
	{ "gpiox_15", "gpiox_15", 0, 0x00000000 },
	{ "gpiox_16", "gpiox_16", 0, 0x00000000 },
	{ "gpiox_17", "gpiox_17", 0, 0x00000000 },
	{ "gpiox_18", "gpiox_18", 0, 0x00000000 },
	{ "gpiox_19", "gpiox_19", 0, 0x00000000 },
	{ "gpiox_20", "gpiox_20", 0, 0x00000000 },
	{ "gpiox_21", "gpiox_21", 0, 0x00000000 },
	{ "gpiox_22", "gpiox_22", 0, 0x00000000 },
	{ "gpiox_23", "gpiox_23", 0, 0x00000000 },
	{ "gpiox_24", "gpiox_24", 0, 0x00000000 },
	{ "gpiox_25", "gpiox_25", 0, 0x00000000 },
	{ "gpiox_26", "gpiox_26", 0, 0x00000000 },
	{ "gpiox_27", "gpiox_27", 0, 0x00000000 },
	{ "gpiox_28", "gpiox_28", 0, 0x00000000 },

	{ "gpioy_0", "gpioy_0", 0, 0x00000000 },
	{ "gpioy_1", "gpioy_1", 0, 0x00000000 },
	{ "gpioy_2", "gpioy_2", 0, 0x00000000 },
	{ "gpioy_3", "gpioy_3", 0, 0x00000000 },
	{ "gpioy_4", "gpioy_4", 0, 0x00000000 },
	{ "gpioy_5", "gpioy_5", 0, 0x00000000 },
	{ "gpioy_6", "gpioy_6", 0, 0x00000000 },
	{ "gpioy_7", "gpioy_7", 0, 0x00000000 },
	{ "gpioy_8", "gpioy_8", 0, 0x00000000 },
	{ "gpioy_9", "gpioy_9", 0, 0x00000000 },

	{ "gpioao_0", "gpioao_0", 0, 0x00000000 },
	{ "gpioao_1", "gpioao_1", 0, 0x00000000 },
	{ "gpioao_2", "gpioao_2", 0, 0x00000000 },
	{ "gpioao_3", "gpioao_3", 0, 0x00000000 },
	{ "gpioao_4", "gpioao_4", 0, 0x00000000 },
	{ "gpioao_5", "gpioao_5", 0, 0x00000000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m3_ethernet[] = {
	{ "clk50_in",  "gpioy_0", 24, 0x00040000 },
	{ "clk_out",   "gpioy_0", 24, 0x00020000 },
	{ "tx_en",     "gpioy_5", 24, 0x00001000 },
	{ "tx_d0",     "gpioy_7", 24, 0x00000400 },
	{ "tx_d1",     "gpioy_6", 24, 0x00000800 },
	{ "crs_dv",    "gpioy_2", 24, 0x00008000 },
	{ "rx_err",    "gpioy_1", 24, 0x00010000 },
	{ "rx_d0",     "gpioy_4", 24, 0x00002000 },
	{ "rx_d1",     "gpioy_3", 24, 0x00004000 },
	{ "mdc",       "gpioy_8", 24, 0x00000200 },
	{ "mdio",      "gpioy_9", 24, 0x00000100 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m3_hdmi[] = {
	{ "cec",     "gpioc_13", 4, 0x02000000 },
	{ "hpd",     "gpioc_10", 4, 0x00400000 },
	{ "scl",     "gpioc_12", 4, 0x01000000 },
	{ "sda",     "gpioc_11", 4, 0x00800000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m3_i2c_a[] = {
	{ "scl",     "gpiox_26", 20, 0x04000000 },
	{ "sda",     "gpiox_25", 20, 0x08000000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m3_i2c_b[] = {
	{ "scl",     "gpiox_28", 20, 0x40000000 },
	{ "sda",     "gpiox_27", 20, 0x80000000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m3_sdio_b[] = {
	{ "clk",     "card_4", 8, 0x00000800 },
	{ "cmd",     "card_5", 8, 0x00000400 },
	{ "d0",      "card_0", 8, 0x00008000 },
	{ "d1",      "card_1", 8, 0x00004000 },
	{ "d2",      "card_2", 8, 0x00002000 },
	{ "d3",      "card_3", 8, 0x00001000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m3_sdxc_b[] = {
	{ "clk",     "card_4", 8, 0x00000020 },
	{ "cmd",     "card_5", 8, 0x00000010 },
	{ "d0",      "card_0", 8, 0x00000080 },
	{ "d1",      "card_1", 8, 0x00000040 },
	{ "d2",      "card_2", 8, 0x00000040 },
	{ "d3",      "card_3", 8, 0x00000040 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m3_uart_a[] = {
	{ "tx",      "gpiox_13", 16, 0x00002000 },
	{ "rx",      "gpiox_14", 16, 0x00001000 },
	{ "cts",     "gpiox_15", 16, 0x00000800 },
	{ "rts",     "gpiox_16", 16, 0x00000400 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m3_uart_b[] = {
	{ "tx",      "gpiox_17", 16, 0x00000200 },
	{ "rx",      "gpiox_18", 16, 0x00000100 },
	{ "cts",     "gpiox_19", 16, 0x00000080 },
	{ "rts",     "gpiox_20", 16, 0x00000040 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m3_uart_c[] = {
	{ "tx",      "gpiox_21", 16, 0x00000008 },
	{ "rx",      "gpiox_22", 16, 0x00000004 },
	{ "cts",     "gpiox_23", 16, 0x00000002 },
	{ "rts",     "gpiox_24", 16, 0x00000001 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m3_i2c_ao[] = {
	{ "scl",     "gpioao_4", 0, 0x00000400 },
	{ "sda",     "gpioao_5", 0, 0x00000200 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m3_uart_ao[] = {
	{ "tx",      "gpioao_0", 0, 0x00001000 },
	{ "rx",      "gpioao_1", 0, 0x00000800 },
	{ "cts",     "gpioao_2", 0, 0x00000400 },
	{ "rts",     "gpioao_3", 0, 0x00000200 },
	{ NULL }
};

struct aml8726_pinctrl_function aml8726_m3_pinctrl[] = {
	{ "gpio", aml8726_m3_gpio },
	{ "ethernet", aml8726_m3_ethernet },
	{ "hdmi", aml8726_m3_hdmi },
	{ "i2c-a", aml8726_m3_i2c_a },
	{ "i2c-b", aml8726_m3_i2c_b },
	{ "sdio-b", aml8726_m3_sdio_b },
	{ "sdxc-b", aml8726_m3_sdxc_b },
	{ "uart-a", aml8726_m3_uart_a },
	{ "uart-b", aml8726_m3_uart_b },
	{ "uart-c", aml8726_m3_uart_c },
	{ "i2c-ao", aml8726_m3_i2c_ao },
	{ "uart-ao", aml8726_m3_uart_ao },
	{ NULL }
};

/*
 * aml8726-m6
 *
 *                 start     size
 * cbus mux        0x202c    40
 * cbus pu_pd      0x203a    24
 * cbus pull_en    0x203a    24
 * aobus mux       0x0005    4
 * aobus pu_pd     0x000b    4
 * aobus pull_en   0x000b    4
 *
 * For simplicity we don't support setting pull for gpioe and gpioz.
 */

static struct aml8726_pinctrl_pkg_pin aml8726_m6_pkg_pin[] = {
	{ "card_0", false, 12, 0x00100000 },
	{ "card_1", false, 12, 0x00200000 },
	{ "card_2", false, 12, 0x00400000 },
	{ "card_3", false, 12, 0x00800000 },
	{ "card_4", false, 12, 0x01000000 },
	{ "card_5", false, 12, 0x02000000 },
	{ "card_6", false, 12, 0x04000000 },

	{ "gpioc_10", false, 8, 0x00000400 },
	{ "gpioc_11", false, 8, 0x00000800 },
	{ "gpioc_12", false, 8, 0x00001000 },
	{ "gpioc_13", false, 8, 0x00002000 },

	{ "gpiox_13", false, 16, 0x00002000 },
	{ "gpiox_14", false, 16, 0x00004000 },
	{ "gpiox_15", false, 16, 0x00008000 },
	{ "gpiox_16", false, 16, 0x00010000 },
	{ "gpiox_17", false, 16, 0x00020000 },
	{ "gpiox_18", false, 16, 0x00040000 },
	{ "gpiox_19", false, 16, 0x00080000 },
	{ "gpiox_20", false, 16, 0x00100000 },
	{ "gpiox_21", false, 16, 0x00200000 },
	{ "gpiox_22", false, 16, 0x00400000 },
	{ "gpiox_23", false, 16, 0x00800000 },
	{ "gpiox_24", false, 16, 0x01000000 },
	{ "gpiox_25", false, 16, 0x02000000 },
	{ "gpiox_26", false, 16, 0x04000000 },
	{ "gpiox_27", false, 16, 0x08000000 },
	{ "gpiox_28", false, 16, 0x10000000 },

	{ "gpioy_0",  false, 20, 0x00000010 },
	{ "gpioy_1",  false, 20, 0x00000020 },
	{ "gpioy_2",  false, 20, 0x00000040 },
	{ "gpioy_3",  false, 20, 0x00000080 },
	{ "gpioy_4",  false, 20, 0x00000100 },
	{ "gpioy_5",  false, 20, 0x00000200 },
	{ "gpioy_6",  false, 20, 0x00000400 },
	{ "gpioy_7",  false, 20, 0x00000800 },
	{ "gpioy_8",  false, 20, 0x00001000 },
	{ "gpioy_9",  false, 20, 0x00002000 },
	{ "gpioy_10", false, 20, 0x00004000 },
	{ "gpioy_11", false, 20, 0x00008000 },
	{ "gpioy_12", false, 20, 0x00010000 },
	{ "gpioy_13", false, 20, 0x00020000 },
	{ "gpioy_14", false, 20, 0x00040000 },

	{ "gpioao_0", true, 0, 0x00000001 },
	{ "gpioao_1", true, 0, 0x00000002 },
	{ "gpioao_2", true, 0, 0x00000004 },
	{ "gpioao_3", true, 0, 0x00000008 },
	{ "gpioao_4", true, 0, 0x00000010 },
	{ "gpioao_5", true, 0, 0x00000020 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m6_gpio[] = {
	{ "card_0", "card_0", 0, 0x00000000 },
	{ "card_1", "card_1", 0, 0x00000000 },
	{ "card_2", "card_2", 0, 0x00000000 },
	{ "card_3", "card_3", 0, 0x00000000 },
	{ "card_4", "card_4", 0, 0x00000000 },
	{ "card_5", "card_5", 0, 0x00000000 },
	{ "card_6", "card_6", 0, 0x00000000 },

	{ "gpioc_10", "gpioc_10", 0, 0x00000000 },
	{ "gpioc_11", "gpioc_11", 0, 0x00000000 },
	{ "gpioc_12", "gpioc_12", 0, 0x00000000 },
	{ "gpioc_13", "gpioc_13", 0, 0x00000000 },

	{ "gpiox_13", "gpiox_13", 0, 0x00000000 },
	{ "gpiox_14", "gpiox_14", 0, 0x00000000 },
	{ "gpiox_15", "gpiox_15", 0, 0x00000000 },
	{ "gpiox_16", "gpiox_16", 0, 0x00000000 },
	{ "gpiox_17", "gpiox_17", 0, 0x00000000 },
	{ "gpiox_18", "gpiox_18", 0, 0x00000000 },
	{ "gpiox_19", "gpiox_19", 0, 0x00000000 },
	{ "gpiox_20", "gpiox_20", 0, 0x00000000 },
	{ "gpiox_21", "gpiox_21", 0, 0x00000000 },
	{ "gpiox_22", "gpiox_22", 0, 0x00000000 },
	{ "gpiox_23", "gpiox_23", 0, 0x00000000 },
	{ "gpiox_24", "gpiox_24", 0, 0x00000000 },
	{ "gpiox_25", "gpiox_25", 0, 0x00000000 },
	{ "gpiox_26", "gpiox_26", 0, 0x00000000 },
	{ "gpiox_27", "gpiox_27", 0, 0x00000000 },
	{ "gpiox_28", "gpiox_28", 0, 0x00000000 },

	{ "gpioy_0", "gpioy_0", 0, 0x00000000 },
	{ "gpioy_1", "gpioy_1", 0, 0x00000000 },
	{ "gpioy_2", "gpioy_2", 0, 0x00000000 },
	{ "gpioy_3", "gpioy_3", 0, 0x00000000 },
	{ "gpioy_4", "gpioy_4", 0, 0x00000000 },
	{ "gpioy_5", "gpioy_5", 0, 0x00000000 },
	{ "gpioy_6", "gpioy_6", 0, 0x00000000 },
	{ "gpioy_7", "gpioy_7", 0, 0x00000000 },
	{ "gpioy_8", "gpioy_8", 0, 0x00000000 },
	{ "gpioy_9", "gpioy_9", 0, 0x00000000 },
	{ "gpioy_10", "gpioy_10", 0, 0x00000000 },
	{ "gpioy_11", "gpioy_11", 0, 0x00000000 },
	{ "gpioy_12", "gpioy_12", 0, 0x00000000 },
	{ "gpioy_13", "gpioy_13", 0, 0x00000000 },
	{ "gpioy_14", "gpioy_14", 0, 0x00000000 },

	{ "gpioao_0", "gpioao_0", 0, 0x00000000 },
	{ "gpioao_1", "gpioao_1", 0, 0x00000000 },
	{ "gpioao_2", "gpioao_2", 0, 0x00000000 },
	{ "gpioao_3", "gpioao_3", 0, 0x00000000 },
	{ "gpioao_4", "gpioao_4", 0, 0x00000000 },
	{ "gpioao_5", "gpioao_5", 0, 0x00000000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m6_ethernet[] = {
	{ "ref_clk_in",  "gpioy_0",  24, 0x80000000 },
	{ "ref_clk_out", "gpioy_0",  24, 0x40000000 },
	{ "tx_clk",      "gpioy_1",  24, 0x00040000 },
	{ "tx_en",       "gpioy_2",  24, 0x00020000 },
	{ "tx_d0",       "gpioy_6",  24, 0x00002000 },
	{ "tx_d1",       "gpioy_5",  24, 0x00004000 },
	{ "tx_d2",       "gpioy_4",  24, 0x00008000 },
	{ "tx_d3",       "gpioy_3",  24, 0x00010000 },
	{ "rx_clk",      "gpioy_7",  24, 0x00001000 },
	{ "rx_dv",       "gpioy_8",  24, 0x00000800 },
	{ "rx_d0",       "gpioy_12", 24, 0x00000080 },
	{ "rx_d1",       "gpioy_11", 24, 0x00000100 },
	{ "rx_d2",       "gpioy_10", 24, 0x00000200 },
	{ "rx_d3",       "gpioy_9",  24, 0x00000400 },
	{ "mdc",         "gpioy_14", 24, 0x00000020 },
	{ "mdio",        "gpioy_13", 24, 0x00000040 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m6_hdmi[] = {
	{ "cec",     "gpioc_13", 4, 0x02000000 },
	{ "hpd",     "gpioc_10", 4, 0x00400000 },
	{ "scl",     "gpioc_12", 4, 0x01000000 },
	{ "sda",     "gpioc_11", 4, 0x00800000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m6_i2c_a[] = {
	{ "scl",     "gpiox_26", 20, 0x04000000 },
	{ "sda",     "gpiox_25", 20, 0x08000000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m6_i2c_b[] = {
	{ "scl",     "gpiox_28", 20, 0x40000000 },
	{ "sda",     "gpiox_27", 20, 0x80000000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m6_sdio_b[] = {
	{ "clk",     "card_4", 8, 0x00000800 },
	{ "cmd",     "card_5", 8, 0x00000400 },
	{ "d0",      "card_0", 8, 0x00008000 },
	{ "d1",      "card_1", 8, 0x00004000 },
	{ "d2",      "card_2", 8, 0x00002000 },
	{ "d3",      "card_3", 8, 0x00001000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m6_sdxc_b[] = {
	{ "clk",     "card_4", 8, 0x00000020 },
	{ "cmd",     "card_5", 8, 0x00000010 },
	{ "d0",      "card_0", 8, 0x00000080 },
	{ "d1",      "card_1", 8, 0x00000040 },
	{ "d2",      "card_2", 8, 0x00000040 },
	{ "d3",      "card_3", 8, 0x00000040 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m6_uart_a[] = {
	{ "tx",      "gpiox_13", 16, 0x00002000 },
	{ "rx",      "gpiox_14", 16, 0x00001000 },
	{ "cts",     "gpiox_15", 16, 0x00000800 },
	{ "rts",     "gpiox_16", 16, 0x00000400 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m6_uart_b[] = {
	{ "tx",      "gpiox_17", 16, 0x00000200 },
	{ "rx",      "gpiox_18", 16, 0x00000100 },
	{ "cts",     "gpiox_19", 16, 0x00000080 },
	{ "rts",     "gpiox_20", 16, 0x00000040 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m6_uart_c[] = {
	{ "tx",      "gpiox_21", 16, 0x00000008 },
	{ "rx",      "gpiox_22", 16, 0x00000004 },
	{ "cts",     "gpiox_23", 16, 0x00000002 },
	{ "rts",     "gpiox_24", 16, 0x00000001 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m6_i2c_ao[] = {
	{ "scl",     "gpioao_4", 0, 0x00000400 },
	{ "sda",     "gpioao_5", 0, 0x00000200 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m6_uart_ao[] = {
	{ "tx",      "gpioao_0", 0, 0x00001000 },
	{ "rx",      "gpioao_1", 0, 0x00000800 },
	{ "cts",     "gpioao_2", 0, 0x00000400 },
	{ "rts",     "gpioao_3", 0, 0x00000200 },
	{ NULL }
};

struct aml8726_pinctrl_function aml8726_m6_pinctrl[] = {
	{ "gpio", aml8726_m6_gpio },
	{ "ethernet", aml8726_m6_ethernet },
	{ "hdmi", aml8726_m6_hdmi },
	{ "i2c-a", aml8726_m6_i2c_a },
	{ "i2c-b", aml8726_m6_i2c_b },
	{ "sdio-b", aml8726_m6_sdio_b },
	{ "sdxc-b", aml8726_m6_sdxc_b },
	{ "uart-a", aml8726_m6_uart_a },
	{ "uart-b", aml8726_m6_uart_b },
	{ "uart-c", aml8726_m6_uart_c },
	{ "i2c-ao", aml8726_m6_i2c_ao },
	{ "uart-ao", aml8726_m6_uart_ao },
	{ NULL }
};


/*
 * aml8726-m8
 *
 *                 start     size
 * cbus mux        0x202c    40
 * cbus pu_pd      0x203a    20
 * cbus pull_en    0x2048    20
 * aobus mux       0x0005    4
 * aobus pu_pd     0x000b    4
 * aobus pull_en   0x000b    4
 */

static struct aml8726_pinctrl_pkg_pin aml8726_m8_pkg_pin[] = {
	{ "boot_0",  false, 8, 0x00000001 },
	{ "boot_1",  false, 8, 0x00000002 },
	{ "boot_2",  false, 8, 0x00000004 },
	{ "boot_3",  false, 8, 0x00000008 },
	{ "boot_4",  false, 8, 0x00000010 },
	{ "boot_5",  false, 8, 0x00000020 },
	{ "boot_6",  false, 8, 0x00000040 },
	{ "boot_7",  false, 8, 0x00000080 },

	{ "boot_16", false, 8, 0x00010000 },
	{ "boot_17", false, 8, 0x00020000 },

	{ "card_0", false, 8, 0x00100000 },
	{ "card_1", false, 8, 0x00200000 },
	{ "card_2", false, 8, 0x00400000 },
	{ "card_3", false, 8, 0x00800000 },
	{ "card_4", false, 8, 0x01000000 },
	{ "card_5", false, 8, 0x02000000 },
	{ "card_6", false, 8, 0x04000000 },

	{ "gpioh_0", false, 4, 0x00001000 },
	{ "gpioh_1", false, 4, 0x00002000 },
	{ "gpioh_2", false, 4, 0x00004000 },
	{ "gpioh_3", false, 4, 0x00008000 },

	{ "gpiox_12", false, 16, 0x00001000 },
	{ "gpiox_13", false, 16, 0x00002000 },
	{ "gpiox_14", false, 16, 0x00004000 },
	{ "gpiox_15", false, 16, 0x00008000 },
	{ "gpiox_16", false, 16, 0x00010000 },
	{ "gpiox_17", false, 16, 0x00020000 },
	{ "gpiox_18", false, 16, 0x00040000 },
	{ "gpiox_19", false, 16, 0x00080000 },

	{ "gpioy_0", false, 12, 0x00000001 },
	{ "gpioy_1", false, 12, 0x00000002 },
	{ "gpioy_2", false, 12, 0x00000004 },
	{ "gpioy_3", false, 12, 0x00000008 },

	{ "gpioz_2", false, 4,  0x00000004 },
	{ "gpioz_3", false, 4,  0x00000008 },
	{ "gpioz_4", false, 4,  0x00000010 },
	{ "gpioz_5", false, 4,  0x00000020 },
	{ "gpioz_6", false, 4,  0x00000040 },
	{ "gpioz_7", false, 4,  0x00000080 },
	{ "gpioz_8", false, 4,  0x00000100 },
	{ "gpioz_9", false, 4,  0x00000200 },
	{ "gpioz_10", false, 4, 0x00000400 },
	{ "gpioz_11", false, 4, 0x00000800 },
	{ "gpioz_12", false, 4, 0x00001000 },
	{ "gpioz_13", false, 4, 0x00002000 },

	{ "gpioao_0", true, 0, 0x00000001 },
	{ "gpioao_1", true, 0, 0x00000002 },
	{ "gpioao_2", true, 0, 0x00000004 },
	{ "gpioao_3", true, 0, 0x00000008 },
	{ "gpioao_4", true, 0, 0x00000010 },
	{ "gpioao_5", true, 0, 0x00000020 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8_gpio[] = {
	{ "boot_0",  "boot_0", 0, 0x00000000 },
	{ "boot_1",  "boot_1", 0, 0x00000000 },
	{ "boot_2",  "boot_2", 0, 0x00000000 },
	{ "boot_3",  "boot_3", 0, 0x00000000 },
	{ "boot_4",  "boot_4", 0, 0x00000000 },
	{ "boot_5",  "boot_5", 0, 0x00000000 },
	{ "boot_6",  "boot_6", 0, 0x00000000 },
	{ "boot_7",  "boot_7", 0, 0x00000000 },

	{ "boot_16", "boot_16", 0, 0x00000000 },
	{ "boot_17", "boot_17", 0, 0x00000000 },

	{ "card_0", "card_0", 0, 0x00000000 },
	{ "card_1", "card_1", 0, 0x00000000 },
	{ "card_2", "card_2", 0, 0x00000000 },
	{ "card_3", "card_3", 0, 0x00000000 },
	{ "card_4", "card_4", 0, 0x00000000 },
	{ "card_5", "card_5", 0, 0x00000000 },
	{ "card_6", "card_6", 0, 0x00000000 },

	{ "gpioh_0", "gpioh_0", 0, 0x00000000 },
	{ "gpioh_1", "gpioh_1", 0, 0x00000000 },
	{ "gpioh_2", "gpioh_2", 0, 0x00000000 },
	{ "gpioh_3", "gpioh_3", 0, 0x00000000 },

	{ "gpiox_12", "gpiox_12", 0, 0x00000000 },
	{ "gpiox_13", "gpiox_13", 0, 0x00000000 },
	{ "gpiox_14", "gpiox_14", 0, 0x00000000 },
	{ "gpiox_15", "gpiox_15", 0, 0x00000000 },
	{ "gpiox_16", "gpiox_16", 0, 0x00000000 },
	{ "gpiox_17", "gpiox_17", 0, 0x00000000 },
	{ "gpiox_18", "gpiox_18", 0, 0x00000000 },
	{ "gpiox_19", "gpiox_19", 0, 0x00000000 },

	{ "gpioy_0", "gpioy_0", 0, 0x00000000 },
	{ "gpioy_1", "gpioy_1", 0, 0x00000000 },
	{ "gpioy_2", "gpioy_2", 0, 0x00000000 },
	{ "gpioy_3", "gpioy_3", 0, 0x00000000 },

	{ "gpioz_2", "gpioz_2", 0, 0x00000000 },
	{ "gpioz_3", "gpioz_3", 0, 0x00000000 },
	{ "gpioz_4", "gpioz_4", 0, 0x00000000 },
	{ "gpioz_5", "gpioz_5", 0, 0x00000000 },
	{ "gpioz_6", "gpioz_6", 0, 0x00000000 },
	{ "gpioz_7", "gpioz_7", 0, 0x00000000 },
	{ "gpioz_8", "gpioz_8", 0, 0x00000000 },
	{ "gpioz_9", "gpioz_9", 0, 0x00000000 },
	{ "gpioz_10", "gpioz_10", 0, 0x00000000 },
	{ "gpioz_11", "gpioz_11", 0, 0x00000000 },
	{ "gpioz_12", "gpioz_12", 0, 0x00000000 },
	{ "gpioz_13", "gpioz_13", 0, 0x00000000 },

	{ "gpioao_0", "gpioao_0", 0, 0x00000000 },
	{ "gpioao_1", "gpioao_1", 0, 0x00000000 },
	{ "gpioao_2", "gpioao_2", 0, 0x00000000 },
	{ "gpioao_3", "gpioao_3", 0, 0x00000000 },
	{ "gpioao_4", "gpioao_4", 0, 0x00000000 },
	{ "gpioao_5", "gpioao_5", 0, 0x00000000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8_ethernet[] = {
	{ "tx_clk",     "gpioz_4",  24, 0x00008000 },
	{ "tx_en",      "gpioz_5",  24, 0x00004000 },
	{ "tx_d0",      "gpioz_7",  24, 0x00001000 },
	{ "tx_d1",      "gpioz_6",  24, 0x00002000 },
	{ "rx_clk_in",  "gpioz_8",  24, 0x00000400 },
	{ "rx_clk_out", "gpioz_8",  24, 0x00000200 },
	{ "rx_dv",      "gpioz_9",  24, 0x00000800 },
	{ "rx_d0",      "gpioz_11", 24, 0x00000080 },
	{ "rx_d1",      "gpioz_10", 24, 0x00000100 },
	{ "mdc",        "gpioz_13", 24, 0x00000020 },
	{ "mdio",       "gpioz_12", 24, 0x00000040 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8_hdmi[] = {
	{ "cec",     "gpioh_3", 4, 0x00800000 },
	{ "hpd",     "gpioh_0", 4, 0x04000000 },
	{ "scl",     "gpioh_2", 4, 0x01000000 },
	{ "sda",     "gpioh_1", 4, 0x02000000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8_i2c_a[] = {
	{ "scl",     "gpioz_12", 20, 0x00000040 },
	{ "sda",     "gpioz_11", 20, 0x00000080 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8_i2c_b[] = {
	{ "scl",     "gpioz_3", 20, 0x04000000 },
	{ "sda",     "gpioz_2", 20, 0x08000000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8_sdio_b[] = {
	{ "clk",     "card_2", 8, 0x00000800 },
	{ "cmd",     "card_3", 8, 0x00000400 },
	{ "d0",      "card_1", 8, 0x00008000 },
	{ "d1",      "card_0", 8, 0x00004000 },
	{ "d2",      "card_5", 8, 0x00002000 },
	{ "d3",      "card_4", 8, 0x00001000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8_sdxc_b[] = {
	{ "clk",     "card_2", 8, 0x00000020 },
	{ "cmd",     "card_3", 8, 0x00000010 },
	{ "d0",      "card_1", 8, 0x00000080 },
	{ "d1",      "card_0", 8, 0x00000040 },
	{ "d2",      "card_5", 8, 0x00000040 },
	{ "d3",      "card_4", 8, 0x00000040 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8_sdio_c[] = {
	{ "clk",     "boot_17", 24, 0x01000000 },
	{ "cmd",     "boot_16", 24, 0x02000000 },
	{ "d0",      "boot_0",  24, 0x20000000 },
	{ "d1",      "boot_1",  24, 0x10000000 },
	{ "d2",      "boot_2",  24, 0x08000000 },
	{ "d3",      "boot_3",  24, 0x04000000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8_sdxc_c[] = {
	{ "clk",     "boot_17", 16, 0x04000000 },
	{ "cmd",     "boot_16", 16, 0x08000000 },
	{ "d0",      "boot_0",  16, 0x40000000 },
	{ "d1",      "boot_1",  16, 0x20000000 },
	{ "d2",      "boot_2",  16, 0x20000000 },
	{ "d3",      "boot_3",  16, 0x20000000 },
	{ "d4",      "boot_4",  16, 0x10000000 },
	{ "d5",      "boot_5",  16, 0x10000000 },
	{ "d6",      "boot_6",  16, 0x10000000 },
	{ "d7",      "boot_7",  16, 0x10000000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8_uart_a[] = {
	{ "tx",      "gpiox_4", 16, 0x00020000 },
	{ "rx",      "gpiox_5", 16, 0x00010000 },
	{ "cts",     "gpiox_6", 16, 0x00008000 },
	{ "rts",     "gpiox_7", 16, 0x00004000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8_uart_b[] = {
	{ "tx",      "gpiox_16", 16, 0x00000200 },
	{ "rx",      "gpiox_17", 16, 0x00000100 },
	{ "cts",     "gpiox_18", 16, 0x00000080 },
	{ "rts",     "gpiox_19", 16, 0x00000040 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8_uart_c[] = {
	{ "tx",      "gpioy_0", 4, 0x00080000 },
	{ "rx",      "gpioy_1", 4, 0x00040000 },
	{ "cts",     "gpioy_2", 4, 0x00020000 },
	{ "rts",     "gpioy_3", 4, 0x00010000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8_i2c_ao[] = {
	{ "scl",     "gpioao_4", 0, 0x00000400 },
	{ "sda",     "gpioao_5", 0, 0x00000200 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8_uart_ao[] = {
	{ "tx",      "gpioao_0", 0, 0x00001000 },
	{ "rx",      "gpioao_1", 0, 0x00000800 },
	{ "cts",     "gpioao_2", 0, 0x00000400 },
	{ "rts",     "gpioao_3", 0, 0x00000200 },
	{ NULL }
};

struct aml8726_pinctrl_function aml8726_m8_pinctrl[] = {
	{ "gpio", aml8726_m8_gpio },
	{ "ethernet", aml8726_m8_ethernet },
	{ "hdmi", aml8726_m8_hdmi },
	{ "i2c-a", aml8726_m8_i2c_a },
	{ "i2c-b", aml8726_m8_i2c_b },
	{ "sdio-b", aml8726_m8_sdio_b },
	{ "sdxc-b", aml8726_m8_sdxc_b },
	{ "sdio-c", aml8726_m8_sdio_c },
	{ "sdxc-c", aml8726_m8_sdxc_c },
	{ "uart-a", aml8726_m8_uart_a },
	{ "uart-b", aml8726_m8_uart_b },
	{ "uart-c", aml8726_m8_uart_c },
	{ "i2c-ao", aml8726_m8_i2c_ao },
	{ "uart-ao", aml8726_m8_uart_ao },
	{ NULL }
};


/*
 * aml8726-m8b
 *
 *                 start     size
 * cbus mux        0x202c    40
 * cbus pu_pd      0x203a    24
 * cbus pull_en    0x2048    24
 * aobus mux       0x0005    4
 * aobus pu_pd     0x000b    4
 * aobus pull_en   0x000b    4
 */

static struct aml8726_pinctrl_pkg_pin aml8726_m8b_pkg_pin[] = {
	{ "boot_0",  false, 8, 0x00000001 },
	{ "boot_1",  false, 8, 0x00000002 },
	{ "boot_2",  false, 8, 0x00000004 },
	{ "boot_3",  false, 8, 0x00000008 },
	{ "boot_4",  false, 8, 0x00000010 },
	{ "boot_5",  false, 8, 0x00000020 },
	{ "boot_6",  false, 8, 0x00000040 },
	{ "boot_7",  false, 8, 0x00000080 },
	{ "boot_8",  false, 8, 0x00000100 },
	{ "boot_9",  false, 8, 0x00000200 },
	{ "boot_10", false, 8, 0x00000400 },

	{ "card_0", false, 8, 0x00100000 },
	{ "card_1", false, 8, 0x00200000 },
	{ "card_2", false, 8, 0x00400000 },
	{ "card_3", false, 8, 0x00800000 },
	{ "card_4", false, 8, 0x01000000 },
	{ "card_5", false, 8, 0x02000000 },
	{ "card_6", false, 8, 0x04000000 },

	{ "dif_0p", false, 20, 0x00000100 },
	{ "dif_0n", false, 20, 0x00000200 },
	{ "dif_1p", false, 20, 0x00000400 },
	{ "dif_1n", false, 20, 0x00000800 },
	{ "dif_2p", false, 20, 0x00001000 },
	{ "dif_2n", false, 20, 0x00002000 },
	{ "dif_3p", false, 20, 0x00004000 },
	{ "dif_3n", false, 20, 0x00008000 },
	{ "dif_4p", false, 20, 0x00010000 },
	{ "dif_4n", false, 20, 0x00020000 },

	{ "gpiodv_24", false, 0, 0x01000000 },
	{ "gpiodv_25", false, 0, 0x02000000 },
	{ "gpiodv_26", false, 0, 0x04000000 },
	{ "gpiodv_27", false, 0, 0x08000000 },

	{ "gpioh_0", false, 4, 0x00010000 },
	{ "gpioh_1", false, 4, 0x00020000 },
	{ "gpioh_2", false, 4, 0x00040000 },
	{ "gpioh_3", false, 4, 0x00080000 },
	{ "gpioh_4", false, 4, 0x00100000 },
	{ "gpioh_5", false, 4, 0x00200000 },
	{ "gpioh_6", false, 4, 0x00400000 },
	{ "gpioh_7", false, 4, 0x00800000 },
	{ "gpioh_8", false, 4, 0x01000000 },
	{ "gpioh_9", false, 4, 0x02000000 },

	{ "gpiox_4", false, 16,  0x00000010 },
	{ "gpiox_5", false, 16,  0x00000020 },
	{ "gpiox_6", false, 16,  0x00000040 },
	{ "gpiox_7", false, 16,  0x00000080 },
	{ "gpiox_16", false, 16, 0x00010000 },
	{ "gpiox_17", false, 16, 0x00020000 },
	{ "gpiox_18", false, 16, 0x00040000 },
	{ "gpiox_19", false, 16, 0x00080000 },

	{ "gpioao_0", true, 0, 0x00000001 },
	{ "gpioao_1", true, 0, 0x00000002 },
	{ "gpioao_2", true, 0, 0x00000004 },
	{ "gpioao_3", true, 0, 0x00000008 },
	{ "gpioao_4", true, 0, 0x00000010 },
	{ "gpioao_5", true, 0, 0x00000020 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8b_gpio[] = {
	{ "boot_0",  "boot_0",  0, 0x00000000 },
	{ "boot_1",  "boot_1",  0, 0x00000000 },
	{ "boot_2",  "boot_2",  0, 0x00000000 },
	{ "boot_3",  "boot_3",  0, 0x00000000 },
	{ "boot_4",  "boot_4",  0, 0x00000000 },
	{ "boot_5",  "boot_5",  0, 0x00000000 },
	{ "boot_6",  "boot_6",  0, 0x00000000 },
	{ "boot_7",  "boot_7",  0, 0x00000000 },
	{ "boot_8",  "boot_8",  0, 0x00000000 },
	{ "boot_9",  "boot_9",  0, 0x00000000 },
	{ "boot_10", "boot_10", 0, 0x00000000 },

	{ "card_0", "card_0", 0, 0x00000000 },
	{ "card_1", "card_1", 0, 0x00000000 },
	{ "card_2", "card_2", 0, 0x00000000 },
	{ "card_3", "card_3", 0, 0x00000000 },
	{ "card_4", "card_4", 0, 0x00000000 },
	{ "card_5", "card_5", 0, 0x00000000 },
	{ "card_6", "card_6", 0, 0x00000000 },

	{ "dif_0p", "dif_0p", 0, 0x00000000 },
	{ "dif_0n", "dif_0n", 0, 0x00000000 },
	{ "dif_1p", "dif_1p", 0, 0x00000000 },
	{ "dif_1n", "dif_1n", 0, 0x00000000 },
	{ "dif_2p", "dif_2p", 0, 0x00000000 },
	{ "dif_2n", "dif_2n", 0, 0x00000000 },
	{ "dif_3p", "dif_3p", 0, 0x00000000 },
	{ "dif_3n", "dif_3n", 0, 0x00000000 },
	{ "dif_4p", "dif_4p", 0, 0x00000000 },
	{ "dif_4n", "dif_4n", 0, 0x00000000 },

	{ "gpiodv_24", "gpiodv_24", 0, 0x00000000 },
	{ "gpiodv_25", "gpiodv_25", 0, 0x00000000 },
	{ "gpiodv_26", "gpiodv_26", 0, 0x00000000 },
	{ "gpiodv_27", "gpiodv_27", 0, 0x00000000 },

	{ "gpioh_0", "gpioh_0", 0, 0x00000000 },
	{ "gpioh_1", "gpioh_1", 0, 0x00000000 },
	{ "gpioh_2", "gpioh_2", 0, 0x00000000 },
	{ "gpioh_3", "gpioh_3", 0, 0x00000000 },
	{ "gpioh_4", "gpioh_4", 0, 0x00000000 },
	{ "gpioh_5", "gpioh_5", 0, 0x00000000 },
	{ "gpioh_6", "gpioh_6", 0, 0x00000000 },
	{ "gpioh_7", "gpioh_7", 0, 0x00000000 },
	{ "gpioh_8", "gpioh_8", 0, 0x00000000 },
	{ "gpioh_9", "gpioh_9", 0, 0x00000000 },

	{ "gpiox_4", "gpiox_4", 0, 0x00000000 },
	{ "gpiox_5", "gpiox_5", 0, 0x00000000 },
	{ "gpiox_6", "gpiox_6", 0, 0x00000000 },
	{ "gpiox_7", "gpiox_7", 0, 0x00000000 },
	{ "gpiox_16", "gpiox_16", 0, 0x00000000 },
	{ "gpiox_17", "gpiox_17", 0, 0x00000000 },
	{ "gpiox_18", "gpiox_18", 0, 0x00000000 },
	{ "gpiox_19", "gpiox_19", 0, 0x00000000 },

	{ "gpioao_0", "gpioao_0", 0, 0x00000000 },
	{ "gpioao_1", "gpioao_1", 0, 0x00000000 },
	{ "gpioao_2", "gpioao_2", 0, 0x00000000 },
	{ "gpioao_3", "gpioao_3", 0, 0x00000000 },
	{ "gpioao_4", "gpioao_4", 0, 0x00000000 },
	{ "gpioao_5", "gpioao_5", 0, 0x00000000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8b_ethernet[] = {
	{ "ref_clk", "dif_3n",   24, 0x00000100 },
	{ "tx_clk",  "gpioh_9",  24, 0x00000800 },
	{ "tx_en",   "dif_3p",   24, 0x00000040 },
	{ "tx_d0",   "gpioh_6",  28, 0x00100000 },
	{ "tx_d1",   "gpioh_5",  28, 0x00200000 },
	{ "tx_d2",   "gpioh_8",  24, 0x00001000 },
	{ "tx_d3",   "gpioh_7",  24, 0x00002000 },
	{ "rx_clk",  "dif_1n",   24, 0x00000008 },
	{ "rx_dv",   "dif_1p",   24, 0x00000004 },
	{ "rx_d0",   "dif_0n",   24, 0x00000002 },
	{ "rx_d1",   "dif_0p",   24, 0x00000001 },
	{ "rx_d2",   "dif_2n",   28, 0x00800000 },
	{ "rx_d3",   "dif_2p",   28, 0x00400000 },
	{ "mdc",     "dif_4p",   24, 0x00000200 },
	{ "mdio",    "dif_4n",   24, 0x00000400 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8b_hdmi[] = {
	{ "cec",     "gpioh_3", 4, 0x00800000 },
	{ "hpd",     "gpioh_0", 4, 0x04000000 },
	{ "scl",     "gpioh_2", 4, 0x01000000 },
	{ "sda",     "gpioh_1", 4, 0x02000000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8b_i2c_a[] = {
	{ "scl",     "gpiodv_25", 36, 0x40000000 },
	{ "sda",     "gpiodv_24", 36, 0x80000000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8b_i2c_b[] = {
	{ "scl",     "gpiodv_27", 36, 0x10000000 },
	{ "sda",     "gpiodv_26", 36, 0x20000000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8b_sdio_b[] = {
	{ "clk",     "card_2", 8, 0x00000800 },
	{ "cmd",     "card_3", 8, 0x00000400 },
	{ "d0",      "card_1", 8, 0x00008000 },
	{ "d1",      "card_0", 8, 0x00004000 },
	{ "d2",      "card_5", 8, 0x00002000 },
	{ "d3",      "card_4", 8, 0x00001000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8b_sdxc_b[] = {
	{ "clk",     "card_2", 8, 0x00000020 },
	{ "cmd",     "card_3", 8, 0x00000010 },
	{ "d0",      "card_1", 8, 0x00000080 },
	{ "d1",      "card_0", 8, 0x00000040 },
	{ "d2",      "card_5", 8, 0x00000040 },
	{ "d3",      "card_4", 8, 0x00000040 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8b_sdio_c[] = {
	{ "clk",     "boot_8",  24, 0x80000000 },
	{ "cmd",     "boot_10", 24, 0x40000000 },
	{ "d0",      "boot_0",  24, 0x20000000 },
	{ "d1",      "boot_1",  24, 0x10000000 },
	{ "d2",      "boot_2",  24, 0x08000000 },
	{ "d3",      "boot_3",  24, 0x04000000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8b_sdxc_c[] = {
	{ "clk",     "boot_8",  28, 0x00080000 },
	{ "cmd",     "boot_10", 28, 0x00040000 },
	{ "d0",      "boot_0",  16, 0x40000000 },
	{ "d1",      "boot_1",  16, 0x20000000 },
	{ "d2",      "boot_2",  16, 0x20000000 },
	{ "d3",      "boot_3",  16, 0x20000000 },
	{ "d4",      "boot_4",  16, 0x10000000 },
	{ "d5",      "boot_5",  16, 0x10000000 },
	{ "d6",      "boot_6",  16, 0x10000000 },
	{ "d7",      "boot_7",  16, 0x10000000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8b_uart_a[] = {
	{ "tx",      "gpiox_4", 16, 0x00020000 },
	{ "rx",      "gpiox_5", 16, 0x00010000 },
	{ "cts",     "gpiox_6", 16, 0x00008000 },
	{ "rts",     "gpiox_7", 16, 0x00004000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8b_uart_b[] = {
	{ "tx",      "gpiox_16", 16, 0x00000200 },
	{ "rx",      "gpiox_17", 16, 0x00000100 },
	{ "cts",     "gpiox_18", 16, 0x00000080 },
	{ "rts",     "gpiox_19", 16, 0x00000040 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8b_uart_c[] = {
	{ "tx",      "gpiodv_24", 24, 0x00800000 },
	{ "rx",      "gpiodv_25", 24, 0x00400000 },
	{ "cts",     "gpiodv_26", 24, 0x00200000 },
	{ "rts",     "gpiodv_27", 24, 0x00100000 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8b_i2c_ao[] = {
	{ "scl",     "gpioao_4", 0, 0x00000400 },
	{ "sda",     "gpioao_5", 0, 0x00000200 },
	{ NULL }
};

static struct aml8726_pinctrl_pin aml8726_m8b_uart_ao[] = {
	{ "tx",      "gpioao_0", 0, 0x00001000 },
	{ "rx",      "gpioao_1", 0, 0x00000800 },
	{ "cts",     "gpioao_2", 0, 0x00000400 },
	{ "rts",     "gpioao_3", 0, 0x00000200 },
	{ NULL }
};

struct aml8726_pinctrl_function aml8726_m8b_pinctrl[] = {
	{ "gpio", aml8726_m8b_gpio },
	{ "ethernet", aml8726_m8b_ethernet },
	{ "hdmi", aml8726_m8b_hdmi },
	{ "i2c-a", aml8726_m8b_i2c_a },
	{ "i2c-b", aml8726_m8b_i2c_b },
	{ "sdio-b", aml8726_m8b_sdio_b },
	{ "sdxc-b", aml8726_m8b_sdxc_b },
	{ "sdio-c", aml8726_m8b_sdio_c },
	{ "sdxc-c", aml8726_m8b_sdxc_c },
	{ "uart-a", aml8726_m8b_uart_a },
	{ "uart-b", aml8726_m8b_uart_b },
	{ "uart-c", aml8726_m8b_uart_c },
	{ "i2c-ao", aml8726_m8b_i2c_ao },
	{ "uart-ao", aml8726_m8b_uart_ao },
	{ NULL }
};

#endif /* _ARM_AMLOGIC_AML8726_PINCTRL_H */
