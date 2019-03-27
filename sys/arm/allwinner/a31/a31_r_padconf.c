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

#if defined(SOC_ALLWINNER_A31) || defined(SOC_ALLWINNER_A31S)

const static struct allwinner_pins a31_r_pins[] = {
	{"PL0",  0, 0,  {"gpio_in", "gpio_out", "s_twi", "s_p2wi", NULL, NULL, NULL, NULL}},
	{"PL1",  0, 1,  {"gpio_in", "gpio_out", "s_twi", "s_p2wi", NULL, NULL, NULL, NULL}},
	{"PL2",  0, 2,  {"gpio_in", "gpio_out", "s_uart", NULL, NULL, NULL, NULL, NULL}},
	{"PL3",  0, 3,  {"gpio_in", "gpio_out", "s_uart", NULL, NULL, NULL, NULL, NULL}},
	{"PL4",  0, 4,  {"gpio_in", "gpio_out", "s_ir", NULL, NULL, NULL, NULL, NULL}},
	{"PL5",  0, 5,  {"gpio_in", "gpio_out", "pl_eint0", "s_jtag", NULL, NULL, NULL, NULL}, 2, 0},
	{"PL6",  0, 6,  {"gpio_in", "gpio_out", "pl_eint1", "s_jtag", NULL, NULL, NULL, NULL}, 2, 1},
	{"PL7",  0, 7,  {"gpio_in", "gpio_out", "pl_eint2", "s_jtag", NULL, NULL, NULL, NULL}, 2, 2},
	{"PL8",  0, 8,  {"gpio_in", "gpio_out", "pl_eint3", "s_jtag", NULL, NULL, NULL, NULL}, 2, 3},

	{"PM0",  1, 0,  {"gpio_in", "gpio_out", "pm_eint0", NULL, NULL, NULL, NULL, NULL}, 2, 0},
	{"PM1",  1, 1,  {"gpio_in", "gpio_out", "pm_eint1", NULL, NULL, NULL, NULL, NULL}, 2, 1},
	{"PM2",  1, 2,  {"gpio_in", "gpio_out", "pm_eint2", "1wire", NULL, NULL, NULL, NULL}, 2, 2},
	{"PM3",  1, 3,  {"gpio_in", "gpio_out", "pm_eint3", NULL, NULL, NULL, NULL, NULL}, 2, 3},
	{"PM4",  1, 4,  {"gpio_in", "gpio_out", "pm_eint4", NULL, NULL, NULL, NULL, NULL}, 2, 4},
	{"PM5",  1, 5,  {"gpio_in", "gpio_out", "pm_eint5", NULL, NULL, NULL, NULL, NULL}, 2, 5},
	{"PM6",  1, 6,  {"gpio_in", "gpio_out", "pm_eint6", NULL, NULL, NULL, NULL, NULL}, 2, 6},
	{"PM7",  1, 7,  {"gpio_in", "gpio_out", "pm_eint7", "rtc", NULL, NULL, NULL, NULL}, 2, 7},
};

const struct allwinner_padconf a31_r_padconf = {
	.npins = nitems(a31_r_pins),
	.pins = a31_r_pins,
};

#endif
