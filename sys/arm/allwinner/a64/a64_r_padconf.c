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

#include "opt_soc.h"

#ifdef SOC_ALLWINNER_A64

static const struct allwinner_pins a64_r_pins[] = {
	{ "PL0",   0, 0,  { "gpio_in", "gpio_out", "s_rsb", "s_i2c", NULL, NULL, "pl_eint0" }, 6, 0},
	{ "PL1",   0, 1,  { "gpio_in", "gpio_out", "s_rsb", "s_i2c", NULL, NULL, "pl_eint1" }, 6, 1},
	{ "PL2",   0, 2,  { "gpio_in", "gpio_out", "s_uart", NULL, NULL, NULL, "pl_eint2" }, 6, 2},
	{ "PL3",   0, 3,  { "gpio_in", "gpio_out", "s_uart", NULL, NULL, NULL, "pl_eint3" }, 6, 3},
	{ "PL4",   0, 4,  { "gpio_in", "gpio_out", "s_jtag", NULL, NULL, NULL, "pl_eint4" }, 6, 4},
	{ "PL5",   0, 5,  { "gpio_in", "gpio_out", "s_jtag", NULL, NULL, NULL, "pl_eint5" }, 6, 5},
	{ "PL6",   0, 6,  { "gpio_in", "gpio_out", "s_jtag", NULL, NULL, NULL, "pl_eint6" }, 6, 6},
	{ "PL7",   0, 7,  { "gpio_in", "gpio_out", "s_jtag", NULL, NULL, NULL, "pl_eint7" }, 6, 7},
	{ "PL8",   0, 8,  { "gpio_in", "gpio_out", "s_i2c", NULL, NULL, NULL, "pl_eint8" }, 6, 8},
	{ "PL9",   0, 9,  { "gpio_in", "gpio_out", "s_i2c", NULL, NULL, NULL, "pl_eint9" }, 6, 9},
	{ "PL10",  0, 10, { "gpio_in", "gpio_out", "s_pwm", NULL, NULL, NULL, "pl_eint10" }, 6, 10},
	{ "PL11",  0, 11, { "gpio_in", "gpio_out", "s_cir", NULL, NULL, NULL, "pl_eint11" }, 6, 11},
	{ "PL12",  0, 12, { "gpio_in", "gpio_out", NULL, NULL, NULL, NULL, "pl_eint12" }, 6, 12},
};

const struct allwinner_padconf a64_r_padconf = {
	.npins = nitems(a64_r_pins),
	.pins = a64_r_pins,
};

#endif /* !SOC_ALLWINNER_A64 */
