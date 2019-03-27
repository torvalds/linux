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

static const struct allwinner_pins a83t_r_pins[] = {
	{ "PL0",   0, 0,  { "gpio_in", "gpio_out", "s_rsb", "s_i2c", NULL, NULL, "eint" } },
	{ "PL1",   0, 1,  { "gpio_in", "gpio_out", "s_rsb", "s_i2c", NULL, NULL, "eint" } },
	{ "PL2",   0, 2,  { "gpio_in", "gpio_out", "s_uart", NULL, NULL, NULL, "eint" } },
	{ "PL3",   0, 3,  { "gpio_in", "gpio_out", "s_uart", NULL, NULL, NULL, "eint" } },
	{ "PL4",   0, 4,  { "gpio_in", "gpio_out", "s_jtag", NULL, NULL, NULL, "eint" } },
	{ "PL5",   0, 5,  { "gpio_in", "gpio_out", "s_jtag", NULL, NULL, NULL, "eint" } },
	{ "PL6",   0, 6,  { "gpio_in", "gpio_out", "s_jtag", NULL, NULL, NULL, "eint" } },
	{ "PL7",   0, 7,  { "gpio_in", "gpio_out", "s_jtag", NULL, NULL, NULL, "eint" } },
	{ "PL8",   0, 8,  { "gpio_in", "gpio_out", "s_i2c", NULL, NULL, NULL, "eint" } },
	{ "PL9",   0, 9,  { "gpio_in", "gpio_out", "s_i2c", NULL, NULL, NULL, "eint" } },
	{ "PL10",  0, 10, { "gpio_in", "gpio_out", "s_pwm", NULL, NULL, NULL, "eint" } },
	{ "PL11",  0, 11, { "gpio_in", "gpio_out", NULL, NULL, NULL, "eint" } },
	{ "PL12",  0, 12, { "gpio_in", "gpio_out", "s_cir", NULL, NULL, NULL, "eint" } },
};

const struct allwinner_padconf a83t_r_padconf = {
	.npins = nitems(a83t_r_pins),
	.pins = a83t_r_pins,
};

#endif /* !SOC_ALLWINNER_A83T */
