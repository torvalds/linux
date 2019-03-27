/*-
 * Copyright (c) 2016 Emmanuel Vadot <manu@freebsd.org>
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

#include <arm/allwinner/allwinner_pinctrl.h>

#ifdef SOC_ALLWINNER_A13

const static struct allwinner_pins a13_pins[] = {
	{"PB0",  1, 0,  {"gpio_in", "gpio_out", "i2c0", NULL, NULL, NULL, NULL, NULL}},
	{"PB1",  1, 1,  {"gpio_in", "gpio_out", "i2c0", NULL, NULL, NULL, NULL, NULL}},
	{"PB2",  1, 2,  {"gpio_in", "gpio_out", "pwm", NULL, NULL, NULL, "eint16", NULL}, 6, 16},
	{"PB3",  1, 3,  {"gpio_in", "gpio_out", "ir0", NULL, NULL, NULL, "eint17", NULL}, 6, 17},
	{"PB4",  1, 4,  {"gpio_in", "gpio_out", "ir0", NULL, NULL, NULL, "eint18", NULL}, 6, 18},
	{"PB10", 1, 10, {"gpio_in", "gpio_out", "spi2", NULL, NULL, NULL, "eint24", NULL}, 6, 24},
	{"PB15", 1, 15, {"gpio_in", "gpio_out", "i2c1", NULL, NULL, NULL, NULL, NULL}},
	{"PB16", 1, 16, {"gpio_in", "gpio_out", "i2c1", NULL, NULL, NULL, NULL, NULL}},
	{"PB17", 1, 17, {"gpio_in", "gpio_out", "i2c2", NULL, NULL, NULL, NULL, NULL}},
	{"PB18", 1, 18, {"gpio_in", "gpio_out", "i2c2", NULL, NULL, NULL, NULL, NULL}},

	{"PC0",  2,  0, {"gpio_in", "gpio_out", "nand", "spi0", NULL, NULL, NULL, NULL}},
	{"PC1",  2,  1, {"gpio_in", "gpio_out", "nand", "spi0", NULL, NULL, NULL, NULL}},
	{"PC2",  2,  2, {"gpio_in", "gpio_out", "nand", "spi0", NULL, NULL, NULL, NULL}},
	{"PC3",  2,  3, {"gpio_in", "gpio_out", "nand", "spi0", NULL, NULL, NULL, NULL}},
	{"PC4",  2,  4, {"gpio_in", "gpio_out", "nand", NULL, NULL, NULL, NULL, NULL}},
	{"PC5",  2,  5, {"gpio_in", "gpio_out", "nand", NULL, NULL, NULL, NULL, NULL}},
	{"PC6",  2,  6, {"gpio_in", "gpio_out", "nand", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC7",  2,  7, {"gpio_in", "gpio_out", "nand", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC8",  2,  8, {"gpio_in", "gpio_out", "nand", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC9",  2,  9, {"gpio_in", "gpio_out", "nand", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC10", 2, 10, {"gpio_in", "gpio_out", "nand", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC11", 2, 11, {"gpio_in", "gpio_out", "nand", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC12", 2, 12, {"gpio_in", "gpio_out", "nand", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC13", 2, 13, {"gpio_in", "gpio_out", "nand", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC14", 2, 14, {"gpio_in", "gpio_out", "nand", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC15", 2, 15, {"gpio_in", "gpio_out", "nand", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC19", 2, 19, {"gpio_in", "gpio_out", "nand", NULL, "uart3", NULL, NULL, NULL}},

	{"PD2",  3,  2, {"gpio_in", "gpio_out", "lcd0", NULL, NULL, NULL, NULL, NULL}},
	{"PD3",  3,  3, {"gpio_in", "gpio_out", "lcd0", NULL, NULL, NULL, NULL, NULL}},
	{"PD4",  3,  4, {"gpio_in", "gpio_out", "lcd0", NULL, NULL, NULL, NULL, NULL}},
	{"PD5",  3,  5, {"gpio_in", "gpio_out", "lcd0", NULL, NULL, NULL, NULL, NULL}},
	{"PD6",  3,  6, {"gpio_in", "gpio_out", "lcd0", NULL, NULL, NULL, NULL, NULL}},
	{"PD7",  3,  7, {"gpio_in", "gpio_out", "lcd0", NULL, NULL, NULL, NULL, NULL}},
	{"PD10", 3, 10, {"gpio_in", "gpio_out", "lcd0", NULL, NULL, NULL, NULL, NULL}},
	{"PD11", 3, 11, {"gpio_in", "gpio_out", "lcd0", NULL, NULL, NULL, NULL, NULL}},
	{"PD12", 3, 12, {"gpio_in", "gpio_out", "lcd0", NULL, NULL, NULL, NULL, NULL}},
	{"PD13", 3, 13, {"gpio_in", "gpio_out", "lcd0", NULL, NULL, NULL, NULL, NULL}},
	{"PD14", 3, 14, {"gpio_in", "gpio_out", "lcd0", NULL, NULL, NULL, NULL, NULL}},
	{"PD15", 3, 15, {"gpio_in", "gpio_out", "lcd0", NULL, NULL, NULL, NULL, NULL}},
	{"PD18", 3, 18, {"gpio_in", "gpio_out", "lcd0", NULL, NULL, NULL, NULL, NULL}},
	{"PD19", 3, 19, {"gpio_in", "gpio_out", "lcd0", NULL, NULL, NULL, NULL, NULL}},
	{"PD20", 3, 20, {"gpio_in", "gpio_out", "lcd0", NULL, NULL, NULL, NULL, NULL}},
	{"PD21", 3, 21, {"gpio_in", "gpio_out", "lcd0", NULL, NULL, NULL, NULL, NULL}},
	{"PD22", 3, 22, {"gpio_in", "gpio_out", "lcd0", NULL, NULL, NULL, NULL, NULL}},
	{"PD23", 3, 23, {"gpio_in", "gpio_out", "lcd0", NULL, NULL, NULL, NULL, NULL}},
	{"PD24", 3, 24, {"gpio_in", "gpio_out", "lcd0", NULL, NULL, NULL, NULL, NULL}},
	{"PD25", 3, 25, {"gpio_in", "gpio_out", "lcd0", NULL, NULL, NULL, NULL, NULL}},
	{"PD26", 3, 26, {"gpio_in", "gpio_out", "lcd0", NULL, NULL, NULL, NULL, NULL}},
	{"PD27", 3, 27, {"gpio_in", "gpio_out", "lcd0", NULL, NULL, NULL, NULL, NULL}},

	{"PE0",  4,  0, {"gpio_in", NULL, NULL, "csi0", "spi2", NULL, "eint14", NULL}, 6, 14},
	{"PE1",  4,  1, {"gpio_in", NULL, NULL, "csi0", "spi2", NULL, "eint15", NULL}, 6, 15},
	{"PE2",  4,  2, {"gpio_in", NULL, NULL, "csi0", "spi2", NULL, NULL, NULL}},
	{"PE3",  4,  3, {"gpio_in", "gpio_out", NULL, "csi0", "spi2", NULL, NULL, NULL}},
	{"PE4",  4,  4, {"gpio_in", "gpio_out", NULL, "csi0", "mmc2", NULL, NULL, NULL}},
	{"PE5",  4,  5, {"gpio_in", "gpio_out", NULL, "csi0", "mmc2", NULL, NULL, NULL}},
	{"PE6",  4,  6, {"gpio_in", "gpio_out", NULL, "csi0", "mmc2", NULL, NULL, NULL}},
	{"PE7",  4,  7, {"gpio_in", "gpio_out", NULL, "csi0", "mmc2", NULL, NULL, NULL}},
	{"PE8",  4,  8, {"gpio_in", "gpio_out", NULL, "csi0", "mmc2", NULL, NULL, NULL}},
	{"PE9",  4,  9, {"gpio_in", "gpio_out", NULL, "csi0", "mmc2", NULL, NULL, NULL}},
	{"PE10", 4, 10, {"gpio_in", "gpio_out", NULL, "csi0", "uart1", NULL, NULL, NULL}},
	{"PE11", 4, 11, {"gpio_in", "gpio_out", NULL, "csi0", "uart1", NULL, NULL, NULL}},

	{"PF0",  5,  0, {"gpio_in", "gpio_out", "mmc0", NULL, NULL, NULL, NULL, NULL}},
	{"PF1",  5,  1, {"gpio_in", "gpio_out", "mmc0", NULL, NULL, NULL, NULL, NULL}},
	{"PF2",  5,  2, {"gpio_in", "gpio_out", "mmc0", NULL, NULL, NULL, NULL, NULL}},
	{"PF3",  5,  3, {"gpio_in", "gpio_out", "mmc0", NULL, NULL, NULL, NULL, NULL}},
	{"PF4",  5,  4, {"gpio_in", "gpio_out", "mmc0", NULL, NULL, NULL, NULL, NULL}},
	{"PF5",  5,  5, {"gpio_in", "gpio_out", "mmc0", NULL, NULL, NULL, NULL, NULL}},

	{"PG0",  6,  0, {"gpio_in", NULL, NULL, NULL, NULL, NULL, "eint0", NULL}, 6, 0},
	{"PG1",  6,  1, {"gpio_in", NULL, NULL, NULL, NULL, NULL, "eint1", NULL}, 6, 1},
	{"PG2",  6,  2, {"gpio_in", NULL, NULL, NULL, NULL, NULL, "eint2", NULL}, 6, 2},
	{"PG3",  6,  3, {"gpio_in", "gpio_out", "mmc1", NULL, "uart1", NULL, "eint3", NULL}, 6, 3},
	{"PG4",  6,  4, {"gpio_in", "gpio_out", "mmc1", NULL, "uart1", NULL, "eint4", NULL}, 6, 4},
	{"PG9",  6,  9, {"gpio_in", "gpio_out", "spi1", "uart3", NULL, NULL, "eint9", NULL}, 6, 9},
	{"PG10", 6, 10, {"gpio_in", "gpio_out", "spi1", "uart3", NULL, NULL, "eint10", NULL}, 6, 10},
	{"PG11", 6, 11, {"gpio_in", "gpio_out", "spi1", "uart3", NULL, NULL, "eint11", NULL}, 6, 11},
	{"PG12", 6, 12, {"gpio_in", "gpio_out", "spi1", "uart3", NULL, NULL, "eint12", NULL}, 6, 12},
};

const struct allwinner_padconf a13_padconf = {
	.npins = sizeof(a13_pins) / sizeof(struct allwinner_pins),
	.pins = a13_pins,
};

#endif /* SOC_ALLWINNER_A13 */
