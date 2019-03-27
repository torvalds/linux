/*-
 * Copyright (c) 2016 Jared McNeill <jmcneill@invisible.ca>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/types.h>

#include <arm/allwinner/allwinner_pinctrl.h>

#ifdef SOC_ALLWINNER_A83T

static const struct allwinner_pins a83t_pins[] = {
	{ "PB0",  1, 0,   { "gpio_in", "gpio_out", "uart2", "jtag", NULL, NULL, "eint" } },
	{ "PB1",  1, 1,   { "gpio_in", "gpio_out", "uart2", "jtag", NULL, NULL, "eint" } },
	{ "PB2",  1, 2,   { "gpio_in", "gpio_out", "uart2", "jtag", NULL, NULL, "eint" } },
	{ "PB3",  1, 3,   { "gpio_in", "gpio_out", "uart2", "jtag", NULL, NULL, "eint" } },
	{ "PB4",  1, 4,   { "gpio_in", "gpio_out", "i2s0", "tdm", NULL, NULL, "eint" } },
	{ "PB5",  1, 5,   { "gpio_in", "gpio_out", "i2s0", "tdm", NULL, NULL, "eint" } },
	{ "PB6",  1, 6,   { "gpio_in", "gpio_out", "i2s0", "tdm", NULL, NULL, "eint" } },
	{ "PB7",  1, 7,   { "gpio_in", "gpio_out", "i2s0", "tdm", NULL, NULL, "eint" } },
	{ "PB8",  1, 8,   { "gpio_in", "gpio_out", "i2s0", "tdm", NULL, NULL, "eint" } },
	{ "PB9",  1, 9,   { "gpio_in", "gpio_out", "uart0", NULL, NULL, NULL, "eint" } },
	{ "PB10", 1, 10,  { "gpio_in", "gpio_out", "uart0", NULL, NULL, NULL, "eint" } },

	{ "PC0",  2, 0,   { "gpio_in", "gpio_out", "nand", "spi0" } },
	{ "PC1",  2, 1,   { "gpio_in", "gpio_out", "nand", "spi0" } },
	{ "PC2",  2, 2,   { "gpio_in", "gpio_out", "nand", "spi0" } },
	{ "PC3",  2, 3,   { "gpio_in", "gpio_out", "nand", "spi0" } },
	{ "PC4",  2, 4,   { "gpio_in", "gpio_out", "nand" } },
	{ "PC5",  2, 5,   { "gpio_in", "gpio_out", "nand", "mmc2" } },
	{ "PC6",  2, 6,   { "gpio_in", "gpio_out", "nand", "mmc2" } },
	{ "PC7",  2, 7,   { "gpio_in", "gpio_out", "nand" } },
	{ "PC8",  2, 8,   { "gpio_in", "gpio_out", "nand", "mmc2" } },
	{ "PC9",  2, 9,   { "gpio_in", "gpio_out", "nand", "mmc2" } },
	{ "PC10", 2, 10,  { "gpio_in", "gpio_out", "nand", "mmc2" } },
	{ "PC11", 2, 11,  { "gpio_in", "gpio_out", "nand", "mmc2" } },
	{ "PC12", 2, 12,  { "gpio_in", "gpio_out", "nand", "mmc2" } },
	{ "PC13", 2, 13,  { "gpio_in", "gpio_out", "nand", "mmc2" } },
	{ "PC14", 2, 14,  { "gpio_in", "gpio_out", "nand", "mmc2" } },
	{ "PC15", 2, 15,  { "gpio_in", "gpio_out", "nand", "mmc2" } },
	{ "PC16", 2, 16,  { "gpio_in", "gpio_out", "nand", "mmc2" } },
	{ "PC17", 2, 17,  { "gpio_in", "gpio_out", "nand" } },
	{ "PC18", 2, 18,  { "gpio_in", "gpio_out", "nand" } },

	{ "PD2",  3, 2,   { "gpio_in", "gpio_out", "lcd", NULL, "gmac" } },
	{ "PD3",  3, 3,   { "gpio_in", "gpio_out", "lcd", NULL, "gmac" } },
	{ "PD4",  3, 4,   { "gpio_in", "gpio_out", "lcd", NULL, "gmac" } },
	{ "PD5",  3, 5,   { "gpio_in", "gpio_out", "lcd", NULL, "gmac" } },
	{ "PD6",  3, 6,   { "gpio_in", "gpio_out", "lcd", NULL, "gmac" } },
	{ "PD7",  3, 7,   { "gpio_in", "gpio_out", "lcd", NULL, "gmac" } },
	{ "PD10", 3, 10,  { "gpio_in", "gpio_out", "lcd", NULL, "gmac" } },
	{ "PD11", 3, 11,  { "gpio_in", "gpio_out", "lcd", NULL, "gmac" } },
	{ "PD12", 3, 12,  { "gpio_in", "gpio_out", "lcd", NULL, "gmac" } },
	{ "PD13", 3, 13,  { "gpio_in", "gpio_out", "lcd", NULL, "gmac" } },
	{ "PD14", 3, 14,  { "gpio_in", "gpio_out", "lcd", NULL, "gmac" } },
	{ "PD15", 3, 15,  { "gpio_in", "gpio_out", "lcd", NULL, "gmac" } },
	{ "PD18", 3, 18,  { "gpio_in", "gpio_out", "lcd", "lvds", "gmac" } },
	{ "PD19", 3, 19,  { "gpio_in", "gpio_out", "lcd", "lvds", "gmac" } },
	{ "PD20", 3, 20,  { "gpio_in", "gpio_out", "lcd", "lvds", "gmac" } },
	{ "PD21", 3, 21,  { "gpio_in", "gpio_out", "lcd", "lvds", "gmac" } },
	{ "PD22", 3, 22,  { "gpio_in", "gpio_out", "lcd", "lvds", "gmac" } },
	{ "PD23", 3, 23,  { "gpio_in", "gpio_out", "lcd", "lvds", "gmac" } },
	{ "PD24", 3, 24,  { "gpio_in", "gpio_out", "lcd", "lvds" } },
	{ "PD25", 3, 25,  { "gpio_in", "gpio_out", "lcd", "lvds" } },
	{ "PD26", 3, 26,  { "gpio_in", "gpio_out", "lcd", "lvds" } },
	{ "PD27", 3, 27,  { "gpio_in", "gpio_out", "lcd", "lvds" } },
	{ "PD28", 3, 28,  { "gpio_in", "gpio_out", "pwm" } },
	{ "PD29", 3, 29,  { "gpio_in", "gpio_out" } },

	{ "PE0",  4, 0,   { "gpio_in", "gpio_out", "csi", NULL, "ccir" } },
	{ "PE1",  4, 1,   { "gpio_in", "gpio_out", "csi", NULL, "ccir" } },
	{ "PE2",  4, 2,   { "gpio_in", "gpio_out", "csi", NULL, "ccir" } },
	{ "PE3",  4, 3,   { "gpio_in", "gpio_out", "csi", NULL, "ccir" } },
	{ "PE4",  4, 4,   { "gpio_in", "gpio_out", "csi" } },
	{ "PE5",  4, 5,   { "gpio_in", "gpio_out", "csi" } },
	{ "PE6",  4, 6,   { "gpio_in", "gpio_out", "csi", NULL, "ccir" } },
	{ "PE7",  4, 7,   { "gpio_in", "gpio_out", "csi", NULL, "ccir" } },
	{ "PE8",  4, 8,   { "gpio_in", "gpio_out", "csi", NULL, "ccir" } },
	{ "PE9",  4, 9,   { "gpio_in", "gpio_out", "csi", NULL, "ccir" } },
	{ "PE10", 4, 10,  { "gpio_in", "gpio_out", "csi", "uart4", "ccir" } },
	{ "PE11", 4, 11,  { "gpio_in", "gpio_out", "csi", "uart4", "ccir" } },
	{ "PE12", 4, 12,  { "gpio_in", "gpio_out", "csi", "uart4", "ccir" } },
	{ "PE13", 4, 13,  { "gpio_in", "gpio_out", "csi", "uart4", "ccir" } },
	{ "PE14", 4, 14,  { "gpio_in", "gpio_out", "csi", "twi2" } },
	{ "PE15", 4, 15,  { "gpio_in", "gpio_out", "csi", "twi2" } },
	{ "PE16", 4, 16,  { "gpio_in", "gpio_out" } },
	{ "PE17", 4, 17,  { "gpio_in", "gpio_out" } },
	{ "PE18", 4, 18,  { "gpio_in", "gpio_out", NULL, "owa" } },
	{ "PE19", 4, 19,  { "gpio_in", "gpio_out" } },

	{ "PF0",  5, 0,   { "gpio_in", "gpio_out", "mmc0", "jtag" } },
	{ "PF1",  5, 1,   { "gpio_in", "gpio_out", "mmc0", "jtag" } },
	{ "PF2",  5, 2,   { "gpio_in", "gpio_out", "mmc0", "uart0" } },
	{ "PF3",  5, 3,   { "gpio_in", "gpio_out", "mmc0", "jtag" } },
	{ "PF4",  5, 4,   { "gpio_in", "gpio_out", "mmc0", "uart0" } },
	{ "PF5",  5, 5,   { "gpio_in", "gpio_out", "mmc0", "jtag" } },
	{ "PF6",  5, 6,   { "gpio_in", "gpio_out" } },

	{ "PG0",  6, 0,   { "gpio_in", "gpio_out", "mmc1", NULL, NULL, NULL, "eint" } },
	{ "PG1",  6, 1,   { "gpio_in", "gpio_out", "mmc1", NULL, NULL, NULL, "eint" } },
	{ "PG2",  6, 2,   { "gpio_in", "gpio_out", "mmc1", NULL, NULL, NULL, "eint" } },
	{ "PG3",  6, 3,   { "gpio_in", "gpio_out", "mmc1", NULL, NULL, NULL, "eint" } },
	{ "PG4",  6, 4,   { "gpio_in", "gpio_out", "mmc1", NULL, NULL, NULL, "eint" } },
	{ "PG5",  6, 5,   { "gpio_in", "gpio_out", "mmc1", NULL, NULL, NULL, "eint" } },
	{ "PG6",  6, 6,   { "gpio_in", "gpio_out", "uart1", "spi1", NULL, NULL, "eint" } },
	{ "PG7",  6, 7,   { "gpio_in", "gpio_out", "uart1", "spi1", NULL, NULL, "eint" } },
	{ "PG8",  6, 8,   { "gpio_in", "gpio_out", "uart1", "spi1", NULL, NULL, "eint" } },
	{ "PG9",  6, 9,   { "gpio_in", "gpio_out", "uart1", "spi1", NULL, NULL, "eint" } },
	{ "PG10", 6, 10,  { "gpio_in", "gpio_out", "i2s1", "uart3", NULL, NULL, "eint" } },
	{ "PG11", 6, 11,  { "gpio_in", "gpio_out", "i2s1", "uart3", NULL, NULL, "eint" } },
	{ "PG12", 6, 12,  { "gpio_in", "gpio_out", "i2s1", "uart3", NULL, NULL, "eint" } },
	{ "PG13", 6, 13,  { "gpio_in", "gpio_out", "i2s1", "uart3", NULL, NULL, "eint" } },

	{ "PH0",  7, 0,   { "gpio_in", "gpio_out", "i2c0", NULL, NULL, NULL, "eint" } },
	{ "PH1",  7, 1,   { "gpio_in", "gpio_out", "i2c0", NULL, NULL, NULL, "eint" } },
	{ "PH2",  7, 2,   { "gpio_in", "gpio_out", "i2c1", NULL, NULL, NULL, "eint" } },
	{ "PH3",  7, 3,   { "gpio_in", "gpio_out", "i2c1", NULL, NULL, NULL, "eint" } },
	{ "PH4",  7, 4,   { "gpio_in", "gpio_out", "i2c2", NULL, NULL, NULL, "eint" } },
	{ "PH5",  7, 5,   { "gpio_in", "gpio_out", "i2c2", NULL, NULL, NULL, "eint" } },
	{ "PH6",  7, 6,   { "gpio_in", "gpio_out", "hdmiddc", NULL, NULL, NULL, "eint" } },
	{ "PH7",  7, 7,   { "gpio_in", "gpio_out", "hdmiddc", NULL, NULL, NULL, "eint" } },
	{ "PH8",  7, 8,   { "gpio_in", "gpio_out", "hdmiddc", NULL, NULL, NULL, "eint" } },
	{ "PH9",  7, 9,   { "gpio_in", "gpio_out", NULL, NULL, NULL, NULL, "eint" } },
	{ "PH10", 7, 10,  { "gpio_in", "gpio_out", NULL, NULL, NULL, NULL, "eint" } },
	{ "PH11", 7, 11,  { "gpio_in", "gpio_out", NULL, NULL, NULL, NULL, "eint" } },
};

const struct allwinner_padconf a83t_padconf = {
	.npins = nitems(a83t_pins),
	.pins = a83t_pins,
};

#endif /* !SOC_ALLWINNER_A83T */
