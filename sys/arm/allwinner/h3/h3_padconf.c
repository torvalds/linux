/*-
 * Copyright (c) 2016-2017 Emmanuel Vadot <manu@freebsd.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/types.h>

#if defined(__aarch64__)
#include "opt_soc.h"
#endif

#include <arm/allwinner/allwinner_pinctrl.h>

#if defined(SOC_ALLWINNER_H3) || defined(SOC_ALLWINNER_H5)

const static struct allwinner_pins h3_pins[] = {
	{"PA0",  0, 0,  {"gpio_in", "gpio_out", "uart2", "jtag", NULL, NULL, "pa_eint0", NULL}, 6, 0},
	{"PA1",  0, 1,  {"gpio_in", "gpio_out", "uart2", "jtag", NULL, NULL, "pa_eint1", NULL}, 6, 1},
	{"PA2",  0, 2,  {"gpio_in", "gpio_out", "uart2", "jtag", NULL, NULL, "pa_eint2", NULL}, 6, 2},
	{"PA3",  0, 3,  {"gpio_in", "gpio_out", "uart2", "jtag", NULL, NULL, "pa_eint3", NULL}, 6, 3},
	{"PA4",  0, 4,  {"gpio_in", "gpio_out", "uart0", NULL, NULL, NULL, "pa_eint4", NULL}, 6, 4},
	{"PA5",  0, 5,  {"gpio_in", "gpio_out", "uart0", "pwm0", NULL, NULL, "pa_eint5", NULL}, 6, 5},
	{"PA6",  0, 6,  {"gpio_in", "gpio_out", "sim", NULL, NULL, NULL, "pa_eint6", NULL}, 6, 6},
	{"PA7",  0, 7,  {"gpio_in", "gpio_out", "sim", NULL, NULL, NULL, "pa_eint7", NULL}, 6, 7},
	{"PA8",  0, 8,  {"gpio_in", "gpio_out", "sim", NULL, NULL, NULL, "pa_eint8", NULL}, 6, 8},
	{"PA9",  0, 9,  {"gpio_in", "gpio_out", "sim", NULL, NULL, NULL, "pa_eint9", NULL}, 6, 9},
	{"PA10", 0, 10, {"gpio_in", "gpio_out", "sim", NULL, NULL, NULL, "pa_eint10", NULL}, 6, 10},
	{"PA11", 0, 11, {"gpio_in", "gpio_out", "i2c0", "di", NULL, NULL, "pa_eint11", NULL}, 6, 11},
	{"PA12", 0, 12, {"gpio_in", "gpio_out", "i2c0", "di", NULL, NULL, "pa_eint12", NULL}, 6, 12},
	{"PA13", 0, 13, {"gpio_in", "gpio_out", "spi1", "uart3", NULL, NULL, "pa_eint13", NULL}, 6, 13},
	{"PA14", 0, 14, {"gpio_in", "gpio_out", "spi1", "uart3", NULL, NULL, "pa_eint14", NULL}, 6, 14},
	{"PA15", 0, 15, {"gpio_in", "gpio_out", "spi1", "uart3", NULL, NULL, "pa_eint15", NULL}, 6, 15},
	{"PA16", 0, 16, {"gpio_in", "gpio_out", "spi1", "uart3", NULL, NULL, "pa_eint16", NULL}, 6, 16},
	{"PA17", 0, 17, {"gpio_in", "gpio_out", "spdif", NULL, NULL, NULL, "pa_eint17", NULL}, 6, 17},
	{"PA18", 0, 18, {"gpio_in", "gpio_out", "i2s0", "i2c1", NULL, NULL, "pa_eint18", NULL}, 6, 18},
	{"PA19", 0, 19, {"gpio_in", "gpio_out", "i2s0", "i2c1", NULL, NULL, "pa_eint19", NULL}, 6, 19},
	{"PA20", 0, 20, {"gpio_in", "gpio_out", "i2s0", "sim", NULL, NULL, "pa_eint20", NULL}, 6, 20},
	{"PA21", 0, 21, {"gpio_in", "gpio_out", "i2s0", "sim", NULL, NULL, "pa_eint21", NULL}, 6, 21},

	{"PC0",  2, 0,  {"gpio_in", "gpio_out", "nand", "spi0", NULL, NULL, NULL, NULL}},
	{"PC1",  2, 1,  {"gpio_in", "gpio_out", "nand", "spi0", NULL, NULL, NULL, NULL}},
	{"PC2",  2, 2,  {"gpio_in", "gpio_out", "nand", "spi0", NULL, NULL, NULL, NULL}},
	{"PC3",  2, 3,  {"gpio_in", "gpio_out", "nand", "spi0", NULL, NULL, NULL, NULL}},
	{"PC4",  2, 4,  {"gpio_in", "gpio_out", "nand", NULL, NULL, NULL, NULL, NULL}},
	{"PC5",  2, 5,  {"gpio_in", "gpio_out", "nand", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC6",  2, 6,  {"gpio_in", "gpio_out", "nand", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC7",  2, 7,  {"gpio_in", "gpio_out", "nand", NULL, NULL, NULL, NULL, NULL}},
	{"PC8",  2, 8,  {"gpio_in", "gpio_out", "nand", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC9",  2, 9,  {"gpio_in", "gpio_out", "nand", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC10", 2, 10, {"gpio_in", "gpio_out", "nand", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC11", 2, 11, {"gpio_in", "gpio_out", "nand", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC12", 2, 12, {"gpio_in", "gpio_out", "nand", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC13", 2, 13, {"gpio_in", "gpio_out", "nand", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC14", 2, 14, {"gpio_in", "gpio_out", "nand", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC15", 2, 15, {"gpio_in", "gpio_out", "nand", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC16", 2, 16, {"gpio_in", "gpio_out", "nand", "mmc2", NULL, NULL, NULL, NULL}},

	{"PD0",  3, 0,  {"gpio_in", "gpio_out", "emac", NULL, NULL, NULL, NULL, NULL}},
	{"PD1",  3, 1,  {"gpio_in", "gpio_out", "emac", NULL, NULL, NULL, NULL, NULL}},
	{"PD2",  3, 2,  {"gpio_in", "gpio_out", "emac", NULL, NULL, NULL, NULL, NULL}},
	{"PD3",  3, 3,  {"gpio_in", "gpio_out", "emac", NULL, NULL, NULL, NULL, NULL}},
	{"PD4",  3, 4,  {"gpio_in", "gpio_out", "emac", NULL, NULL, NULL, NULL, NULL}},
	{"PD5",  3, 5,  {"gpio_in", "gpio_out", "emac", NULL, NULL, NULL, NULL, NULL}},
	{"PD6",  3, 6,  {"gpio_in", "gpio_out", "emac", NULL, NULL, NULL, NULL, NULL}},
	{"PD7",  3, 7,  {"gpio_in", "gpio_out", "emac", NULL, NULL, NULL, NULL, NULL}},
	{"PD8",  3, 8,  {"gpio_in", "gpio_out", "emac", NULL, NULL, NULL, NULL, NULL}},
	{"PD9",  3, 9,  {"gpio_in", "gpio_out", "emac", NULL, NULL, NULL, NULL, NULL}},
	{"PD10", 3, 10, {"gpio_in", "gpio_out", "emac", NULL, NULL, NULL, NULL, NULL}},
	{"PD11", 3, 11, {"gpio_in", "gpio_out", "emac", NULL, NULL, NULL, NULL, NULL}},
	{"PD12", 3, 12, {"gpio_in", "gpio_out", "emac", NULL, NULL, NULL, NULL, NULL}},
	{"PD13", 3, 13, {"gpio_in", "gpio_out", "emac", NULL, NULL, NULL, NULL, NULL}},
	{"PD14", 3, 14, {"gpio_in", "gpio_out", "emac", NULL, NULL, NULL, NULL, NULL}},
	{"PD15", 3, 15, {"gpio_in", "gpio_out", "emac", NULL, NULL, NULL, NULL, NULL}},
	{"PD16", 3, 16, {"gpio_in", "gpio_out", "emac", NULL, NULL, NULL, NULL, NULL}},
	{"PD17", 3, 17, {"gpio_in", "gpio_out", "emac", NULL, NULL, NULL, NULL, NULL}},

	{"PE0",  4, 0,  {"gpio_in", "gpio_out", "csi", "ts", NULL, NULL, NULL, NULL}},
	{"PE1",  4, 1,  {"gpio_in", "gpio_out", "csi", "ts", NULL, NULL, NULL, NULL}},
	{"PE2",  4, 2,  {"gpio_in", "gpio_out", "csi", "ts", NULL, NULL, NULL, NULL}},
	{"PE3",  4, 3,  {"gpio_in", "gpio_out", "csi", "ts", NULL, NULL, NULL, NULL}},
	{"PE4",  4, 4,  {"gpio_in", "gpio_out", "csi", "ts", NULL, NULL, NULL, NULL}},
	{"PE5",  4, 5,  {"gpio_in", "gpio_out", "csi", "ts", NULL, NULL, NULL, NULL}},
	{"PE6",  4, 6,  {"gpio_in", "gpio_out", "csi", "ts", NULL, NULL, NULL, NULL}},
	{"PE7",  4, 7,  {"gpio_in", "gpio_out", "csi", "ts", NULL, NULL, NULL, NULL}},
	{"PE8",  4, 8,  {"gpio_in", "gpio_out", "csi", "ts", NULL, NULL, NULL, NULL}},
	{"PE9",  4, 9,  {"gpio_in", "gpio_out", "csi", "ts", NULL, NULL, NULL, NULL}},
	{"PE10", 4, 10, {"gpio_in", "gpio_out", "csi", "ts", NULL, NULL, NULL, NULL}},
	{"PE11", 4, 11, {"gpio_in", "gpio_out", "csi", "ts", NULL, NULL, NULL, NULL}},
	{"PE12", 4, 12, {"gpio_in", "gpio_out", "csi", "i2c2", NULL, NULL, NULL, NULL}},
	{"PE13", 4, 13, {"gpio_in", "gpio_out", "csi", "i2c2", NULL, NULL, NULL, NULL}},
	{"PE14", 4, 14, {"gpio_in", "gpio_out", NULL, NULL, NULL, NULL, NULL, NULL}},
	{"PE15", 4, 15, {"gpio_in", "gpio_out", NULL, NULL, NULL, NULL, NULL, NULL}},

	{"PF0",  5, 0,  {"gpio_in", "gpio_out", "mmc0", "jtag", NULL, NULL, NULL, NULL}},
	{"PF1",  5, 1,  {"gpio_in", "gpio_out", "mmc0", "jtag", NULL, NULL, NULL, NULL}},
	{"PF2",  5, 2,  {"gpio_in", "gpio_out", "mmc0", "uart0", NULL, NULL, NULL, NULL}},
	{"PF3",  5, 3,  {"gpio_in", "gpio_out", "mmc0", "jtag", NULL, NULL, NULL, NULL}},
	{"PF4",  5, 4,  {"gpio_in", "gpio_out", "mmc0", "uart0", NULL, NULL, NULL, NULL}},
	{"PF5",  5, 5,  {"gpio_in", "gpio_out", "mmc0", "jtag", NULL, NULL, NULL, NULL}},
	{"PF6",  5, 6,  {"gpio_in", "gpio_out", NULL, NULL, NULL, NULL, NULL, NULL}},

	{"PG0",  6, 0,  {"gpio_in", "gpio_out", "mmc1", NULL, NULL, NULL, "pg_eint0", NULL}, 6, 0},
	{"PG1",  6, 1,  {"gpio_in", "gpio_out", "mmc1", NULL, NULL, NULL, "pg_eint1", NULL}, 6, 1},
	{"PG2",  6, 2,  {"gpio_in", "gpio_out", "mmc1", NULL, NULL, NULL, "pg_eint2", NULL}, 6, 2},
	{"PG3",  6, 3,  {"gpio_in", "gpio_out", "mmc1", NULL, NULL, NULL, "pg_eint3", NULL}, 6, 3},
	{"PG4",  6, 4,  {"gpio_in", "gpio_out", "mmc1", NULL, NULL, NULL, "pg_eint4", NULL}, 6, 4},
	{"PG5",  6, 5,  {"gpio_in", "gpio_out", "mmc1", NULL, NULL, NULL, "pg_eint5", NULL}, 6, 5},
	{"PG6",  6, 6,  {"gpio_in", "gpio_out", "uart1", NULL, NULL, NULL, "pg_eint6", NULL}, 6, 6},
	{"PG7",  6, 7,  {"gpio_in", "gpio_out", "uart1", NULL, NULL, NULL, "pg_eint7", NULL}, 6, 7},
	{"PG8",  6, 8,  {"gpio_in", "gpio_out", "uart1", NULL, NULL, NULL, "pg_eint8", NULL}, 6, 8},
	{"PG9",  6, 9,  {"gpio_in", "gpio_out", "uart1", NULL, NULL, NULL, "pg_eint9", NULL}, 6, 9},
	{"PG10", 6, 10, {"gpio_in", "gpio_out", "i2s1", NULL, NULL, NULL, "pg_eint10", NULL}, 6, 10},
	{"PG11", 6, 11, {"gpio_in", "gpio_out", "i2s1", NULL, NULL, NULL, "pg_eint11", NULL}, 6, 11},
	{"PG12", 6, 12, {"gpio_in", "gpio_out", "i2s1", NULL, NULL, NULL, "pg_eint12", NULL}, 6, 12},
	{"PG13", 6, 13, {"gpio_in", "gpio_out", "i2s1", NULL, NULL, NULL, "pg_eint13", NULL}, 6, 13},
};

const struct allwinner_padconf h3_padconf = {
	.npins = nitems(h3_pins),
	.pins = h3_pins,
};

#endif /* SOC_ALLWINNER_H3 */
