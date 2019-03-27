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

#ifndef	_ARM_AMLOGIC_AML8726_CCM_H
#define	_ARM_AMLOGIC_AML8726_CCM_H


struct aml8726_ccm_gate {
	uint32_t addr;
	uint32_t bits;
};

struct aml8726_ccm_function {
	const char *name;
	struct aml8726_ccm_gate *gates;
};


/*
 * aml8726-m3
 */

static struct aml8726_ccm_gate aml8726_m3_ethernet[] = {
	{ 4, 0x00000008 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m3_i2c[] = {
	{ 0, 0x00000200 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m3_rng[] = {
	{  0, 0x00001000 },
	{  0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m3_sdio[] = {
	{ 0, 0x00020000 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m3_sdxc[] = {
	{ 0, 0x00004000 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m3_uart_a[] = {
	{ 0, 0x00002000 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m3_uart_b[] = {
	{ 4, 0x00010000 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m3_uart_c[] = {
	{ 8, 0x00008000 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m3_usb_a[] = {
	{ 4, 0x00200000 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m3_usb_b[] = {
	{ 4, 0x00400000 },
	{ 0, 0x00000000 }
};

struct aml8726_ccm_function aml8726_m3_ccm[] = {
	{ "ethernet", aml8726_m3_ethernet },
	{ "i2c", aml8726_m3_i2c },
	{ "rng", aml8726_m3_rng },
	{ "sdio", aml8726_m3_sdio },
	{ "sdxc", aml8726_m3_sdxc },
	{ "uart-a", aml8726_m3_uart_a },
	{ "uart-b", aml8726_m3_uart_b },
	{ "uart-c", aml8726_m3_uart_c },
	{ "usb-a", aml8726_m3_usb_a },
	{ "usb-b", aml8726_m3_usb_b },
	{ NULL }
};


/*
 * aml8726-m6
 */

static struct aml8726_ccm_gate aml8726_m6_ethernet[] = {
	{ 4, 0x00000008 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m6_i2c[] = {
	{ 0, 0x00000200 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m6_rng[] = {
	{  0, 0x00001000 },
	{  0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m6_sdio[] = {
	{ 0, 0x00020000 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m6_sdxc[] = {
	{ 0, 0x00004000 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m6_uart_a[] = {
	{ 0, 0x00002000 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m6_uart_b[] = {
	{ 4, 0x00010000 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m6_uart_c[] = {
	{ 8, 0x00008000 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m6_usb_a[] = {
	{ 4, 0x00200000 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m6_usb_b[] = {
	{ 4, 0x00400000 },
	{ 0, 0x00000000 }
};

struct aml8726_ccm_function aml8726_m6_ccm[] = {
	{ "ethernet", aml8726_m6_ethernet },
	{ "i2c", aml8726_m6_i2c },
	{ "rng", aml8726_m6_rng },
	{ "sdio", aml8726_m6_sdio },
	{ "sdxc", aml8726_m6_sdxc },
	{ "uart-a", aml8726_m6_uart_a },
	{ "uart-b", aml8726_m6_uart_b },
	{ "uart-c", aml8726_m6_uart_c },
	{ "usb-a", aml8726_m6_usb_a },
	{ "usb-b", aml8726_m6_usb_b },
	{ NULL }
};


/*
 * aml8726-m8
 */

static struct aml8726_ccm_gate aml8726_m8_ethernet[] = {
	{ 4, 0x00000008 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m8_i2c[] = {
	{ 0, 0x00000200 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m8_rng[] = {
	{  0, 0x00001000 },
	{ 16, 0x00200000 },
	{  0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m8_sdio[] = {
	{ 0, 0x00020000 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m8_sdxc[] = {
	{ 0, 0x00004000 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m8_uart_a[] = {
	{ 0, 0x00002000 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m8_uart_b[] = {
	{ 4, 0x00010000 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m8_uart_c[] = {
	{ 8, 0x00008000 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m8_usb_a[] = {
	{ 4, 0x00200000 },
	{ 4, 0x04000000 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m8_usb_b[] = {
	{ 4, 0x00400000 },
	{ 4, 0x04000000 },
	{ 0, 0x00000000 }
};

struct aml8726_ccm_function aml8726_m8_ccm[] = {
	{ "ethernet", aml8726_m8_ethernet },
	{ "i2c", aml8726_m8_i2c },
	{ "rng", aml8726_m8_rng },
	{ "sdio", aml8726_m8_sdio },
	{ "sdxc", aml8726_m8_sdxc },
	{ "uart-a", aml8726_m8_uart_a },
	{ "uart-b", aml8726_m8_uart_b },
	{ "uart-c", aml8726_m8_uart_c },
	{ "usb-a", aml8726_m8_usb_a },
	{ "usb-b", aml8726_m8_usb_b },
	{ NULL }
};


/*
 * aml8726-m8b
 */

static struct aml8726_ccm_gate aml8726_m8b_ethernet[] = {
	{ 4, 0x00000008 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m8b_i2c[] = {
	{ 0, 0x00000200 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m8b_rng[] = {
	{  0, 0x00001000 },
	{ 16, 0x00200000 },
	{  0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m8b_sdio[] = {
	{ 0, 0x00020000 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m8b_sdxc[] = {
	{ 0, 0x00004000 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m8b_uart_a[] = {
	{ 0, 0x00002000 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m8b_uart_b[] = {
	{ 4, 0x00010000 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m8b_uart_c[] = {
	{ 8, 0x00008000 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m8b_usb_a[] = {
	{ 4, 0x00200000 },
	{ 4, 0x04000000 },
	{ 0, 0x00000000 }
};

static struct aml8726_ccm_gate aml8726_m8b_usb_b[] = {
	{ 4, 0x00400000 },
	{ 4, 0x04000000 },
	{ 0, 0x00000000 }
};

struct aml8726_ccm_function aml8726_m8b_ccm[] = {
	{ "ethernet", aml8726_m8b_ethernet },
	{ "i2c", aml8726_m8b_i2c },
	{ "rng", aml8726_m8b_rng },
	{ "sdio", aml8726_m8b_sdio },
	{ "sdxc", aml8726_m8b_sdxc },
	{ "uart-a", aml8726_m8b_uart_a },
	{ "uart-b", aml8726_m8b_uart_b },
	{ "uart-c", aml8726_m8b_uart_c },
	{ "usb-a", aml8726_m8b_usb_a },
	{ "usb-b", aml8726_m8b_usb_b },
	{ NULL }
};

#endif /* _ARM_AMLOGIC_AML8726_CCM_H */
