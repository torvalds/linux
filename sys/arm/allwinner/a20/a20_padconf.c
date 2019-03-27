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

#ifdef SOC_ALLWINNER_A20

const static struct allwinner_pins a20_pins[] = {
	{"PA0",  0, 0,  {"gpio_in", "gpio_out", "emac", "spi1", "uart2", "gmac", NULL, NULL}},
	{"PA1",  0, 1,  {"gpio_in", "gpio_out", "emac", "spi1", "uart2", "gmac", NULL, NULL}},
	{"PA2",  0, 2,  {"gpio_in", "gpio_out", "emac", "spi1", "uart2", "gmac", NULL, NULL}},
	{"PA3",  0, 3,  {"gpio_in", "gpio_out", "emac", "spi1", "uart2", "gmac", NULL, NULL}},
	{"PA4",  0, 4,  {"gpio_in", "gpio_out", "emac", "spi1", NULL, "gmac", NULL, NULL}},
	{"PA5",  0, 5,  {"gpio_in", "gpio_out", "emac", "spi3", NULL, "gmac", NULL, NULL}},
	{"PA6",  0, 6,  {"gpio_in", "gpio_out", "emac", "spi3", NULL, "gmac", NULL, NULL}},
	{"PA7",  0, 7,  {"gpio_in", "gpio_out", "emac", "spi3", NULL, "gmac", NULL, NULL}},
	{"PA8",  0, 8,  {"gpio_in", "gpio_out", "emac", "spi3", NULL, "gmac", NULL, NULL}},
	{"PA9",  0, 9,  {"gpio_in", "gpio_out", "emac", "spi3", NULL, "gmac", "i2c1", NULL}},
	{"PA10", 0, 10, {"gpio_in", "gpio_out", "emac", NULL, "uart1", "gmac", NULL, NULL}},
	{"PA11", 0, 11, {"gpio_in", "gpio_out", "emac", NULL, "uart1", "gmac", NULL, NULL}},
	{"PA12", 0, 12, {"gpio_in", "gpio_out", "emac", "uart6", "uart1", "gmac", NULL, NULL}},
	{"PA13", 0, 13, {"gpio_in", "gpio_out", "emac", "uart6", "uart1", "gmac", NULL, NULL}},
	{"PA14", 0, 14, {"gpio_in", "gpio_out", "emac", "uart7", "uart1", "gmac", "i2c1", NULL}},
	{"PA15", 0, 15, {"gpio_in", "gpio_out", "emac", "uart7", "uart1", "gmac", "i2c1", NULL}},
	{"PA16", 0, 16, {"gpio_in", "gpio_out", NULL, "can", "uart1", "gmac", "i2c1", NULL}},
	{"PA17", 0, 17, {"gpio_in", "gpio_out", NULL, "can", "uart1", "gmac", "i2c1", NULL}},

	{"PB0",  1, 0,  {"gpio_in", "gpio_out", "i2c0", NULL, NULL, NULL, NULL, NULL}},
	{"PB1",  1, 1,  {"gpio_in", "gpio_out", "i2c0", NULL, NULL, NULL, NULL, NULL}},
	{"PB2",  1, 2,  {"gpio_in", "gpio_out", "pwm", NULL, NULL, NULL, NULL, NULL}},
	{"PB3",  1, 3,  {"gpio_in", "gpio_out", "ir0", NULL, "spdif", NULL, NULL, NULL}},
	{"PB4",  1, 4,  {"gpio_in", "gpio_out", "ir0", NULL, NULL, NULL, NULL, NULL}},
	{"PB5",  1, 5,  {"gpio_in", "gpio_out", "i2s0", "ac97", NULL, NULL, NULL, NULL}},
	{"PB6",  1, 6,  {"gpio_in", "gpio_out", "i2c0", "ac97", NULL, NULL, NULL, NULL}},
	{"PB7",  1, 7,  {"gpio_in", "gpio_out", "i2c0", "ac97", NULL, NULL, NULL, NULL}},
	{"PB8",  1, 8,  {"gpio_in", "gpio_out", "i2c0", "ac97", NULL, NULL, NULL, NULL}},
	{"PB9",  1, 9,  {"gpio_in", "gpio_out", "i2c0", NULL, NULL, NULL, NULL, NULL}},
	{"PB10", 1, 10, {"gpio_in", "gpio_out", "i2c0", NULL, NULL, NULL, NULL, NULL}},
	{"PB11", 1, 11, {"gpio_in", "gpio_out", "i2c0", NULL, NULL, NULL, NULL, NULL}},
	{"PB12", 1, 12, {"gpio_in", "gpio_out", "i2c0", "ac97", "spdif", NULL, NULL, NULL}},
	{"PB13", 1, 13, {"gpio_in", "gpio_out", "spi2", NULL, "spdif", NULL, NULL, NULL}},
	{"PB14", 1, 14, {"gpio_in", "gpio_out", "spi2", "jtag", NULL, NULL, NULL, NULL}},
	{"PB15", 1, 15, {"gpio_in", "gpio_out", "spi2", "jtag", NULL, NULL, NULL, NULL}},
	{"PB16", 1, 16, {"gpio_in", "gpio_out", "spi2", "jtag", NULL, NULL, NULL, NULL}},
	{"PB17", 1, 17, {"gpio_in", "gpio_out", "spi2", "jtag", NULL, NULL, NULL, NULL}},
	{"PB18", 1, 18, {"gpio_in", "gpio_out", "i2c1", NULL, NULL, NULL, NULL, NULL}},
	{"PB19", 1, 19, {"gpio_in", "gpio_out", "i2c1", NULL, NULL, NULL, NULL, NULL}},
	{"PB20", 1, 20, {"gpio_in", "gpio_out", "i2c2", NULL, NULL, NULL, NULL, NULL}},
	{"PB21", 1, 21, {"gpio_in", "gpio_out", "i2c2", NULL, NULL, NULL, NULL, NULL}},
	{"PB22", 1, 22, {"gpio_in", "gpio_out", "uart0", "ir1", NULL, NULL, NULL, NULL}},
	{"PB23", 1, 23, {"gpio_in", "gpio_out", "uart0", "ir1", NULL, NULL, NULL, NULL}},

	{"PC0",  2, 0,  {"gpio_in", "gpio_out", "nand0", "spi0", NULL, NULL, NULL, NULL}},
	{"PC1",  2, 1,  {"gpio_in", "gpio_out", "nand0", "spi0", NULL, NULL, NULL, NULL}},
	{"PC2",  2, 2,  {"gpio_in", "gpio_out", "nand0", "spi0", NULL, NULL, NULL, NULL}},
	{"PC3",  2, 3,  {"gpio_in", "gpio_out", "nand0", NULL, NULL, NULL, NULL, NULL}},
	{"PC4",  2, 4,  {"gpio_in", "gpio_out", "nand0", NULL, NULL, NULL, NULL, NULL}},
	{"PC5",  2, 5,  {"gpio_in", "gpio_out", "nand0", NULL, NULL, NULL, NULL, NULL}},
	{"PC6",  2, 6,  {"gpio_in", "gpio_out", "nand0", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC7",  2, 7,  {"gpio_in", "gpio_out", "nand0", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC8",  2, 8,  {"gpio_in", "gpio_out", "nand0", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC9",  2, 9,  {"gpio_in", "gpio_out", "nand0", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC10", 2, 10, {"gpio_in", "gpio_out", "nand0", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC11", 2, 11, {"gpio_in", "gpio_out", "nand0", "mmc2", NULL, NULL, NULL, NULL}},
	{"PC12", 2, 12, {"gpio_in", "gpio_out", "nand0", NULL, NULL, NULL, NULL, NULL}},
	{"PC13", 2, 13, {"gpio_in", "gpio_out", "nand0", NULL, NULL, NULL, NULL, NULL}},
	{"PC14", 2, 14, {"gpio_in", "gpio_out", "nand0", NULL, NULL, NULL, NULL, NULL}},
	{"PC15", 2, 15, {"gpio_in", "gpio_out", "nand0", NULL, NULL, NULL, NULL, NULL}},
	{"PC16", 2, 16, {"gpio_in", "gpio_out", "nand0", NULL, NULL, NULL, NULL, NULL}},
	{"PC17", 2, 17, {"gpio_in", "gpio_out", "nand0", NULL, NULL, NULL, NULL, NULL}},
	{"PC18", 2, 18, {"gpio_in", "gpio_out", "nand0", NULL, NULL, NULL, NULL, NULL}},
	{"PC19", 2, 19, {"gpio_in", "gpio_out", "nand0", "spi2", NULL, NULL, "eint12", NULL}, 6, 12},
	{"PC20", 2, 20, {"gpio_in", "gpio_out", "nand0", "spi2", NULL, NULL, "eint13", NULL}, 6, 13},
	{"PC21", 2, 21, {"gpio_in", "gpio_out", "nand0", "spi2", NULL, NULL, "eint14", NULL}, 6, 14},
	{"PC22", 2, 22, {"gpio_in", "gpio_out", "nand0", "spi2", NULL, NULL, "eint15", NULL}, 6, 15},
	{"PC23", 2, 23, {"gpio_in", "gpio_out", NULL, "spi0", NULL, NULL, NULL, NULL}},
	{"PC24", 2, 24, {"gpio_in", "gpio_out", "nand0", NULL, NULL, NULL, NULL, NULL}},

	{"PD0",  3, 0,  {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD1",  3, 1,  {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD2",  3, 2,  {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD3",  3, 3,  {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD4",  3, 4,  {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD5",  3, 5,  {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD6",  3, 6,  {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD7",  3, 7,  {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD8",  3, 8,  {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD9",  3, 9,  {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD10", 3, 10, {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD11", 3, 11, {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD12", 3, 12, {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD13", 3, 13, {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD14", 3, 14, {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD15", 3, 15, {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD16", 3, 16, {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD17", 3, 17, {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD18", 3, 18, {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD19", 3, 19, {"gpio_in", "gpio_out", "lcd0", "lvds0", NULL, NULL, NULL, NULL}},
	{"PD20", 3, 20, {"gpio_in", "gpio_out", "lcd0", "csi1", NULL, NULL, NULL, NULL}},
	{"PD21", 3, 21, {"gpio_in", "gpio_out", "lcd0", "sim", NULL, NULL, NULL, NULL}},
	{"PD22", 3, 22, {"gpio_in", "gpio_out", "lcd0", "sim", NULL, NULL, NULL, NULL}},
	{"PD23", 3, 23, {"gpio_in", "gpio_out", "lcd0", "sim", NULL, NULL, NULL, NULL}},
	{"PD24", 3, 24, {"gpio_in", "gpio_out", "lcd0", "sim", NULL, NULL, NULL, NULL}},
	{"PD25", 3, 25, {"gpio_in", "gpio_out", "lcd0", "sim", NULL, NULL, NULL, NULL}},
	{"PD26", 3, 26, {"gpio_in", "gpio_out", "lcd0", "sim", NULL, NULL, NULL, NULL}},
	{"PD27", 3, 27, {"gpio_in", "gpio_out", "lcd0", "sim", NULL, NULL, NULL, NULL}},

	{"PE0",  4, 0,  {"gpio_in", "gpio_out", "ts0", "csi0", NULL, NULL, NULL, NULL}},
	{"PE1",  4, 1,  {"gpio_in", "gpio_out", "ts0", "csi0", NULL, NULL, NULL, NULL}},
	{"PE2",  4, 2,  {"gpio_in", "gpio_out", "ts0", "csi0", NULL, NULL, NULL, NULL}},
	{"PE3",  4, 3,  {"gpio_in", "gpio_out", "ts0", "csi0", NULL, NULL, NULL, NULL}},
	{"PE4",  4, 4,  {"gpio_in", "gpio_out", "ts0", "csi0", NULL, NULL, NULL, NULL}},
	{"PE5",  4, 5,  {"gpio_in", "gpio_out", "ts0", "csi0", NULL, NULL, NULL, NULL}},
	{"PE6",  4, 6,  {"gpio_in", "gpio_out", "ts0", "csi0", NULL, NULL, NULL, NULL}},
	{"PE7",  4, 7,  {"gpio_in", "gpio_out", "ts0", "csi0", NULL, NULL, NULL, NULL}},
	{"PE8",  4, 8,  {"gpio_in", "gpio_out", "ts0", "csi0", NULL, NULL, NULL, NULL}},
	{"PE9",  4, 9,  {"gpio_in", "gpio_out", "ts0", "csi0", NULL, NULL, NULL, NULL}},
	{"PE10", 4, 10, {"gpio_in", "gpio_out", "ts0", "csi0", NULL, NULL, NULL, NULL}},
	{"PE11", 4, 11, {"gpio_in", "gpio_out", "ts0", "csi0", NULL, NULL, NULL, NULL}},

	{"PF0",  5, 0,  {"gpio_in", "gpio_out", "mmc0", NULL, "jtag", NULL, NULL, NULL}},
	{"PF1",  5, 1,  {"gpio_in", "gpio_out", "mmc0", NULL, "jtag", NULL, NULL, NULL}},
	{"PF2",  5, 2,  {"gpio_in", "gpio_out", "mmc0", NULL, "uart0", NULL, NULL, NULL}},
	{"PF3",  5, 3,  {"gpio_in", "gpio_out", "mmc0", NULL, "jtag", NULL, NULL, NULL}},
	{"PF4",  5, 4,  {"gpio_in", "gpio_out", "mmc0", NULL, "uart0", NULL, NULL, NULL}},
	{"PF5",  5, 5,  {"gpio_in", "gpio_out", "mmc0", NULL, "jtag", NULL, NULL, NULL}},

	{"PG0",  6, 0,  {"gpio_in", "gpio_out", "ts1", "csi1", "mmc1", NULL, NULL, NULL}},
	{"PG1",  6, 1,  {"gpio_in", "gpio_out", "ts1", "csi1", "mmc1", NULL, NULL, NULL}},
	{"PG2",  6, 2,  {"gpio_in", "gpio_out", "ts1", "csi1", "mmc1", NULL, NULL, NULL}},
	{"PG3",  6, 3,  {"gpio_in", "gpio_out", "ts1", "csi1", "mmc1", NULL, NULL, NULL}},
	{"PG4",  6, 4,  {"gpio_in", "gpio_out", "ts1", "csi1", "mmc1", "csi0", NULL, NULL}},
	{"PG5",  6, 5,  {"gpio_in", "gpio_out", "ts1", "csi1", "mmc1", "csi0", NULL, NULL}},
	{"PG6",  6, 6,  {"gpio_in", "gpio_out", "ts1", "csi1", "uart3", "csi0", NULL, NULL}},
	{"PG7",  6, 7,  {"gpio_in", "gpio_out", "ts1", "csi1", "uart3", "csi0", NULL, NULL}},
	{"PG8",  6, 8,  {"gpio_in", "gpio_out", "ts1", "csi1", "uart3", "csi0", NULL, NULL}},
	{"PG9",  6, 9,  {"gpio_in", "gpio_out", "ts1", "csi1", "uart3", "csi0", NULL, NULL}},
	{"PG10", 6, 10, {"gpio_in", "gpio_out", "ts1", "csi1", "uart4", "csi0", NULL, NULL}},
	{"PG11", 6, 11, {"gpio_in", "gpio_out", "ts1", "csi1", "uart4", "csi0", NULL, NULL}},

	{"PH0",  7, 0,  {"gpio_in", "gpio_out", "lcd1", NULL, "uart3", NULL, "eint0", "csi1"}, 6, 0},
	{"PH1",  7, 1,  {"gpio_in", "gpio_out", "lcd1", NULL, "uart3", NULL, "eint1", "csi1"}, 6, 1},
	{"PH2",  7, 2,  {"gpio_in", "gpio_out", "lcd1", NULL, "uart3", NULL, "eint2", "csi1"}, 6, 2},
	{"PH3",  7, 3,  {"gpio_in", "gpio_out", "lcd1", NULL, "uart3", NULL, "eint3", "csi1"}, 6, 3},
	{"PH4",  7, 4,  {"gpio_in", "gpio_out", "lcd1", NULL, "uart4", NULL, "eint4", "csi1"}, 6, 4},
	{"PH5",  7, 5,  {"gpio_in", "gpio_out", "lcd1", NULL, "uart4", NULL, "eint5", "csi1"}, 6, 5},
	{"PH6",  7, 6,  {"gpio_in", "gpio_out", "lcd1", NULL, "uart5", "ms", "eint6", "csi1"}, 6, 6},
	{"PH7",  7, 7,  {"gpio_in", "gpio_out", "lcd1", NULL, "uart5", "ms", "eint7", "csi1"}, 6, 7},
	{"PH8",  7, 8,  {"gpio_in", "gpio_out", "lcd1", NULL, "keypad", "ms", "eint8", "csi1"}, 6, 8},
	{"PH9",  7, 9,  {"gpio_in", "gpio_out", "lcd1", NULL, "keypad", "ms", "eint9", "csi1"}, 6, 9},
	{"PH10", 7, 10, {"gpio_in", "gpio_out", "lcd1", NULL, "keypad", "ms", "eint10", "csi1"}, 6, 10},
	{"PH11", 7, 11, {"gpio_in", "gpio_out", "lcd1", NULL, "keypad", "ms", "eint11", "csi1"}, 6, 11},
	{"PH12", 7, 12, {"gpio_in", "gpio_out", "lcd1", NULL, "ps2", NULL, "eint12", "csi1"}, 6, 12},
	{"PH13", 7, 13, {"gpio_in", "gpio_out", "lcd1", NULL, "ps2", "sim", "eint13", "csi1"}, 6, 13},
	{"PH14", 7, 14, {"gpio_in", "gpio_out", "lcd1", NULL, "keypad", "sim", "eint14", "csi1"}, 6, 14},
	{"PH15", 7, 15, {"gpio_in", "gpio_out", "lcd1", NULL, "keypad", "sim", "eint15", "csi1"}, 6, 15},
	{"PH16", 7, 16, {"gpio_in", "gpio_out", "lcd1", NULL, "keypad", "sim", "eint16", "csi1"}, 6, 16},
	{"PH17", 7, 17, {"gpio_in", "gpio_out", "lcd1", NULL, "keypad", "sim", "eint17", "csi1"}, 6, 17},
	{"PH18", 7, 18, {"gpio_in", "gpio_out", "lcd1", NULL, "keypad", "sim", "eint18", "csi1"}, 6, 18},
	{"PH19", 7, 19, {"gpio_in", "gpio_out", "lcd1", NULL, "keypad", "sim", "eint19", "csi1"}, 6, 19},
	{"PH20", 7, 20, {"gpio_in", "gpio_out", "lcd1", NULL, "can", NULL, "eint20", "csi1"}, 6, 20},
	{"PH21", 7, 21, {"gpio_in", "gpio_out", "lcd1", NULL, "can", NULL, "eint21", "csi1"}, 6, 21},
	{"PH22", 7, 22, {"gpio_in", "gpio_out", "lcd1", NULL, "keypad", "mmc1", NULL, "csi1"}},
	{"PH23", 7, 23, {"gpio_in", "gpio_out", "lcd1", NULL, "keypad", "mmc1", NULL, "csi1"}},
	{"PH24", 7, 24, {"gpio_in", "gpio_out", "lcd1", NULL, "keypad", "mmc1", NULL, "csi1"}},
	{"PH25", 7, 25, {"gpio_in", "gpio_out", "lcd1", NULL, "keypad", "mmc1", NULL, "csi1"}},
	{"PH26", 7, 26, {"gpio_in", "gpio_out", "lcd1", NULL, "keypad", "mmc1", NULL, "csi1"}},
	{"PH27", 7, 27, {"gpio_in", "gpio_out", "lcd1", NULL, "keypad", "mmc1", NULL, "csi1"}},

	{"PI0",  8, 0,  {"gpio_in", "gpio_out", NULL, "i2c3", NULL, NULL, NULL, NULL}},
	{"PI1",  8, 1,  {"gpio_in", "gpio_out", NULL, "i2c3", NULL, NULL, NULL, NULL}},
	{"PI2",  8, 2,  {"gpio_in", "gpio_out", NULL, "i2c4", NULL, NULL, NULL, NULL}},
	{"PI3",  8, 3,  {"gpio_in", "gpio_out", "pwm", "i2c4", NULL, NULL, NULL, NULL}},
	{"PI4",  8, 4,  {"gpio_in", "gpio_out", "mmc3", NULL, NULL, NULL, NULL, NULL}},
	{"PI5",  8, 5,  {"gpio_in", "gpio_out", "mmc3", NULL, NULL, NULL, NULL, NULL}},
	{"PI6",  8, 6,  {"gpio_in", "gpio_out", "mmc3", NULL, NULL, NULL, NULL, NULL}},
	{"PI7",  8, 7,  {"gpio_in", "gpio_out", "mmc3", NULL, NULL, NULL, NULL, NULL}},
	{"PI8",  8, 8,  {"gpio_in", "gpio_out", "mmc3", NULL, NULL, NULL, NULL, NULL}},
	{"PI9",  8, 9,  {"gpio_in", "gpio_out", "mmc3", NULL, NULL, NULL, NULL, NULL}},
	{"PI10", 8, 10, {"gpio_in", "gpio_out", "spi0", "uart5", NULL, NULL, "eint", NULL}},
	{"PI11", 8, 11, {"gpio_in", "gpio_out", "spi0", "uart5", NULL, NULL, "eint", NULL}},
	{"PI12", 8, 12, {"gpio_in", "gpio_out", "spi0", "uart6", "clk_out_a", NULL, "eint", NULL}},
	{"PI13", 8, 13, {"gpio_in", "gpio_out", "spi0", "uart6", "clk_out_b", NULL, "eint", NULL}},
	{"PI14", 8, 14, {"gpio_in", "gpio_out", "spi0", "ps2", "timer4", NULL, "eint", NULL}},
	{"PI15", 8, 15, {"gpio_in", "gpio_out", "spi1", "ps2", "timer5", NULL, "eint", NULL}},
	{"PI16", 8, 16, {"gpio_in", "gpio_out", "spi1", "uart2", NULL, NULL, "eint", NULL}},
	{"PI17", 8, 17, {"gpio_in", "gpio_out", "spi1", "uart2", NULL, NULL, "eint", NULL}},
	{"PI18", 8, 18, {"gpio_in", "gpio_out", "spi1", "uart2", NULL, NULL, "eint", NULL}},
	{"PI19", 8, 19, {"gpio_in", "gpio_out", "spi1", "uart2", NULL, NULL, "eint", NULL}},
	{"PI20", 8, 20, {"gpio_in", "gpio_out", "ps2", "uart7", "hdmi", NULL, NULL, NULL}},
	{"PI21", 8, 21, {"gpio_in", "gpio_out", "ps2", "uart7", "hdmi", NULL, NULL, NULL}},
};

const struct allwinner_padconf a20_padconf = {
	.npins = sizeof(a20_pins) / sizeof(struct allwinner_pins),
	.pins = a20_pins,
};

#endif /* SOC_ALLWINNER_A20 */
