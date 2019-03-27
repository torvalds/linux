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

#ifdef SOC_ALLWINNER_A33

const static struct allwinner_pins a33_pins[] = {
	{"PB0",  1, 0,  {"gpio_in", "gpio_out", "uart2", "uart0", "pb_eint0", NULL}, 4, 0},
	{"PB1",  1, 1,  {"gpio_in", "gpio_out", "uart2", "uart0", "pb_eint1", NULL}, 4, 1},
	{"PB2",  1, 2,  {"gpio_in", "gpio_out", "uart2", NULL, "pb_eint2", NULL}, 4, 2},
	{"PB3",  1, 3,  {"gpio_in", "gpio_out", "uart2", NULL, "pb_eint3", NULL}, 4, 3},
	{"PB4",  1, 4,  {"gpio_in", "gpio_out", "i2s0", "aif2", "pb_eint4", NULL}, 4, 4},
	{"PB5",  1, 5,  {"gpio_in", "gpio_out", "i2s0", "aif2", "pb_eint5", NULL}, 4, 5},
	{"PB6",  1, 6,  {"gpio_in", "gpio_out", "i2s0", "aif2", "pb_eint6", NULL}, 4, 6},
	{"PB7",  1, 7,  {"gpio_in", "gpio_out", "i2s0", "aif2", "pb_eint7", NULL}, 4, 7},

	{"PC0",  2, 0,  {"gpio_in", "gpio_out", "nand0", "spi0", NULL, NULL, NULL, NULL}},
	{"PC1",  2, 1,  {"gpio_in", "gpio_out", "nand0", "spi0", NULL, NULL, NULL, NULL}},
	{"PC2",  2, 2,  {"gpio_in", "gpio_out", "nand0", "spi0", NULL, NULL, NULL, NULL}},
	{"PC3",  2, 3,  {"gpio_in", "gpio_out", "nand0", "spi0", NULL, NULL, NULL, NULL}},
	{"PC4",  2, 4,  {"gpio_in", "gpio_out", "nand0", NULL, NULL, NULL, NULL, NULL}},
	{"PC5",  2, 5,  {"gpio_in", "gpio_out", "nand0", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC6",  2, 6,  {"gpio_in", "gpio_out", "nand0", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC7",  2, 7,  {"gpio_in", "gpio_out", "nand0", NULL, NULL, NULL, NULL, NULL}},
	{"PC8",  2, 8,  {"gpio_in", "gpio_out", "nand0", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC9",  2, 9,  {"gpio_in", "gpio_out", "nand0", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC10", 2, 10, {"gpio_in", "gpio_out", "nand0", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC11", 2, 11, {"gpio_in", "gpio_out", "nand0", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC12", 2, 12, {"gpio_in", "gpio_out", "nand0", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC13", 2, 13, {"gpio_in", "gpio_out", "nand0", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC14", 2, 14, {"gpio_in", "gpio_out", "nand0", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC15", 2, 15, {"gpio_in", "gpio_out", "nand0", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC16", 2, 16, {"gpio_in", "gpio_out", "nand0", "mmc2", NULL, NULL, NULL, NULL}},

	{"PD2",  3, 2,  {"gpio_in", "gpio_out", "lcd0", "mmc1", NULL, NULL, NULL, NULL}},
	{"PD3",  3, 3,  {"gpio_in", "gpio_out", "lcd0", "mmc1", NULL, NULL, NULL, NULL}},
	{"PD4",  3, 4,  {"gpio_in", "gpio_out", "lcd0", "mmc1", NULL, NULL, NULL, NULL}},
	{"PD5",  3, 5,  {"gpio_in", "gpio_out", "lcd0", "mmc1", NULL, NULL, NULL, NULL}},
	{"PD6",  3, 6,  {"gpio_in", "gpio_out", "lcd0", "mmc1", NULL, NULL, NULL, NULL}},
	{"PD7",  3, 7,  {"gpio_in", "gpio_out", "lcd0", "mmc1", NULL, NULL, NULL, NULL}},
	{"PD10", 3, 10, {"gpio_in", "gpio_out", "lcd0", "uart1", NULL, NULL, NULL, NULL}},
	{"PD11", 3, 11, {"gpio_in", "gpio_out", "lcd0", "uart1", NULL, NULL, NULL, NULL}},
	{"PD12", 3, 12, {"gpio_in", "gpio_out", "lcd0", "uart1", NULL, NULL, NULL, NULL}},
	{"PD13", 3, 13, {"gpio_in", "gpio_out", "lcd0", "uart1", NULL, NULL, NULL, NULL}},
	{"PD14", 3, 14, {"gpio_in", "gpio_out", "lcd0", NULL, NULL, NULL, NULL, NULL}},
	{"PD15", 3, 15, {"gpio_in", "gpio_out", "lcd0", NULL, NULL, NULL, NULL, NULL}},
	{"PD18", 3, 18, {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD19", 3, 19, {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD20", 3, 20, {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD21", 3, 21, {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD22", 3, 22, {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD23", 3, 23, {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD24", 3, 24, {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD25", 3, 25, {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD26", 3, 26, {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD27", 3, 27, {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},

	{"PE0",  4, 0,  {"gpio_in", "gpio_out", "csi", NULL, NULL, NULL, NULL, NULL}, 0, 0},
	{"PE1",  4, 1,  {"gpio_in", "gpio_out", "csi", NULL, NULL, NULL, NULL, NULL}, 0, 0},
	{"PE2",  4, 2,  {"gpio_in", "gpio_out", "csi", NULL, NULL, NULL, NULL, NULL}, 0, 0},
	{"PE3",  4, 3,  {"gpio_in", "gpio_out", "csi", NULL, NULL, NULL, NULL, NULL}, 0, 0},
	{"PE4",  4, 4,  {"gpio_in", "gpio_out", "csi", NULL, NULL, NULL, NULL, NULL}, 0, 0},
	{"PE5",  4, 5,  {"gpio_in", "gpio_out", "csi", NULL, NULL, NULL, NULL, NULL}, 0, 0},
	{"PE6",  4, 6,  {"gpio_in", "gpio_out", "csi", NULL, NULL, NULL, NULL, NULL}, 0, 0},
	{"PE7",  4, 7,  {"gpio_in", "gpio_out", "csi", NULL, NULL, NULL, NULL, NULL}, 0, 0},
	{"PE8",  4, 8,  {"gpio_in", "gpio_out", "csi", NULL, NULL, NULL, NULL, NULL}, 0, 0},
	{"PE9",  4, 9,  {"gpio_in", "gpio_out", "csi", NULL, NULL, NULL, NULL, NULL}, 0, 0},
	{"PE10", 4, 10, {"gpio_in", "gpio_out", "csi", NULL, NULL, NULL, NULL, NULL}, 0, 0},
	{"PE11", 4, 11, {"gpio_in", "gpio_out", "csi", NULL, NULL, NULL, NULL, NULL}, 0, 0},
	{"PE12", 4, 12, {"gpio_in", "gpio_out", "csi", "i2c2", NULL, NULL, NULL, NULL}, 0, 0},
	{"PE13", 4, 13, {"gpio_in", "gpio_out", "csi", "i2c2", NULL, NULL, NULL, NULL}, 0, 0},
	{"PE14", 4, 14, {"gpio_in", "gpio_out", NULL, NULL, NULL, NULL, NULL, NULL}, 0, 0},
	{"PE15", 4, 15, {"gpio_in", "gpio_out", NULL, NULL, NULL, NULL, NULL, NULL}, 0, 0},
	{"PE16", 4, 16, {"gpio_in", "gpio_out", NULL, NULL, NULL, NULL, NULL, NULL}, 0, 0},
	{"PE17", 4, 16, {"gpio_in", "gpio_out", NULL, NULL, NULL, NULL, NULL, NULL}, 0, 0},

	{"PF0",  5, 0,  {"gpio_in", "gpio_out", "mmc0", "jtag", NULL, NULL, NULL}},
	{"PF1",  5, 1,  {"gpio_in", "gpio_out", "mmc0", "jtag", NULL, NULL, NULL}},
	{"PF2",  5, 2,  {"gpio_in", "gpio_out", "mmc0", "uart0", NULL, NULL, NULL}},
	{"PF3",  5, 3,  {"gpio_in", "gpio_out", "mmc0", "jtag", NULL, NULL, NULL}},
	{"PF4",  5, 4,  {"gpio_in", "gpio_out", "mmc0", "uart0", NULL, NULL, NULL}},
	{"PF5",  5, 5,  {"gpio_in", "gpio_out", "mmc0", "jtag", NULL, NULL, NULL}},

	{"PG0",  6, 0,  {"gpio_in", "gpio_out", "mmc1", NULL, "pg_eint0", NULL}, 4, 0},
	{"PG1",  6, 1,  {"gpio_in", "gpio_out", "mmc1", NULL, "pg_eint1", NULL}, 4, 1},
	{"PG2",  6, 2,  {"gpio_in", "gpio_out", "mmc1", NULL, "pg_eint2", NULL}, 4, 2},
	{"PG3",  6, 3,  {"gpio_in", "gpio_out", "mmc1", NULL, "pg_eint3", NULL}, 4, 3},
	{"PG4",  6, 4,  {"gpio_in", "gpio_out", "mmc1", NULL, "pg_eint4", NULL}, 4, 4},
	{"PG5",  6, 5,  {"gpio_in", "gpio_out", "mmc1", NULL, "pg_eint5", NULL}, 4, 5},
	{"PG6",  6, 6,  {"gpio_in", "gpio_out", "uart1", NULL, "pg_eint6", NULL}, 4, 6},
	{"PG7",  6, 7,  {"gpio_in", "gpio_out", "uart1", NULL, "pg_eint7", NULL}, 4, 7},
	{"PG8",  6, 8,  {"gpio_in", "gpio_out", "uart1", NULL, "pg_eint8", NULL}, 4, 8},
	{"PG9",  6, 9,  {"gpio_in", "gpio_out", "uart1", NULL, "pg_eint9", NULL}, 4, 9},
	{"PG10", 6, 10, {"gpio_in", "gpio_out", "i2s1", "aif3", "pg_eint10", NULL}, 4, 10},
	{"PG11", 6, 11, {"gpio_in", "gpio_out", "i2s1", "aif3", "pg_eint11", NULL}, 4, 11},
	{"PG12", 6, 12, {"gpio_in", "gpio_out", "i2s1", "aif3", "pg_eint12", NULL}, 4, 12},
	{"PG13", 6, 13, {"gpio_in", "gpio_out", "i2s1", "aif3", "pg_eint13", NULL}, 4, 13},

	{"PH0",  7, 0,  {"gpio_in", "gpio_out", "pwm0", NULL, NULL, NULL, NULL, NULL}},
	{"PH1",  7, 1,  {"gpio_in", "gpio_out", "pwm1", NULL, NULL, NULL, NULL, NULL}},
	{"PH2",  7, 2,  {"gpio_in", "gpio_out", "i2c0", NULL, NULL, NULL, NULL, NULL}},
	{"PH3",  7, 3,  {"gpio_in", "gpio_out", "i2c0", NULL, NULL, NULL, NULL, NULL}},
	{"PH4",  7, 4,  {"gpio_in", "gpio_out", "i2c1", NULL, NULL, NULL, NULL, NULL}},
	{"PH5",  7, 5,  {"gpio_in", "gpio_out", "i2c1", NULL, NULL, NULL, NULL, NULL}},
	{"PH6",  7, 6,  {"gpio_in", "gpio_out", "spi0", "uart3", NULL, NULL, NULL, NULL}},
	{"PH7",  7, 7,  {"gpio_in", "gpio_out", "spi0", "uart3", NULL, NULL, NULL, NULL}},
	{"PH8",  7, 8,  {"gpio_in", "gpio_out", "spi0", "uart3", NULL, NULL, NULL, NULL}},
	{"PH9",  7, 9,  {"gpio_in", "gpio_out", "spi0", "uart3", NULL, NULL, NULL, NULL}},
};

const struct allwinner_padconf a33_padconf = {
	.npins = nitems(a33_pins),
	.pins = a33_pins,
};

#endif /* SOC_ALLWINNER_A33 */
