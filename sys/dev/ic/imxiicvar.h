/* $OpenBSD: imxiicvar.h,v 1.2 2022/06/28 23:43:13 naddy Exp $ */
/*
 * Copyright (c) 2013 Patrick Wildt <patrick@blueri.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef IMXIICVAR_H
#define IMXIICVAR_H

#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>
#include <sys/rwlock.h>

#include <dev/i2c/i2cvar.h>

#define I2C_TYPE_IMX21	0
#define I2C_TYPE_VF610	1

struct imxiic_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_size_t		sc_ios;
	int			sc_reg_shift;
	int			sc_type;
	int			sc_bitrate;
	uint32_t		sc_clkrate;

	const struct imxiic_clk_pair *sc_clk_div;
	int			sc_clk_ndiv;

	struct rwlock		sc_buslock;
	struct i2c_controller	i2c_tag;

	uint16_t		frequency;
	uint16_t		stopped;
};

void imxiic_enable(struct imxiic_softc *, int);
void imxiic_setspeed(struct imxiic_softc *, u_int);

int imxiic_i2c_acquire_bus(void *, int);
void imxiic_i2c_release_bus(void *, int);
int imxiic_i2c_exec(void *, i2c_op_t, i2c_addr_t, const void *, size_t,
    void *, size_t, int);

struct imxiic_clk_pair {
	uint16_t div;
	uint16_t val;
};

static const struct imxiic_clk_pair imxiic_imx21_clk_div[50] = {
	{ 22,	0x20 }, { 24,	0x21 }, { 26,	0x22 }, { 28,	0x23 },
	{ 30,	0x00 }, { 32,	0x24 }, { 36,	0x25 }, { 40,	0x26 },
	{ 42,	0x03 }, { 44,	0x27 }, { 48,	0x28 }, { 52,	0x05 },
	{ 56,	0x29 }, { 60,	0x06 }, { 64,	0x2A }, { 72,	0x2B },
	{ 80,	0x2C }, { 88,	0x09 }, { 96,	0x2D }, { 104,	0x0A },
	{ 112,	0x2E }, { 128,	0x2F }, { 144,	0x0C }, { 160,	0x30 },
	{ 192,	0x31 }, { 224,	0x32 }, { 240,	0x0F }, { 256,	0x33 },
	{ 288,	0x10 }, { 320,	0x34 }, { 384,	0x35 }, { 448,	0x36 },
	{ 480,	0x13 }, { 512,	0x37 }, { 576,	0x14 }, { 640,	0x38 },
	{ 768,	0x39 }, { 896,	0x3A }, { 960,	0x17 }, { 1024,	0x3B },
	{ 1152,	0x18 }, { 1280,	0x3C }, { 1536,	0x3D }, { 1792,	0x3E },
	{ 1920,	0x1B }, { 2048,	0x3F }, { 2304,	0x1C }, { 2560,	0x1D },
	{ 3072,	0x1E }, { 3840,	0x1F }
};

static const struct imxiic_clk_pair imxiic_vf610_clk_div[60] = {
	{ 20,	0x00 }, { 22,	0x01 }, { 24,	0x02 }, { 26,	0x03 },
	{ 28,	0x04 }, { 30,	0x05 }, { 32,	0x09 }, { 34,	0x06 },
	{ 36,	0x0A }, { 40,	0x07 }, { 44,	0x0C }, { 48,	0x0D },
	{ 52,	0x43 }, { 56,	0x0E }, { 60,	0x45 }, { 64,	0x12 },
	{ 68,	0x0F }, { 72,	0x13 }, { 80,	0x14 }, { 88,	0x15 },
	{ 96,	0x19 }, { 104,	0x16 }, { 112,	0x1A }, { 128,	0x17 },
	{ 136,	0x4F }, { 144,	0x1C }, { 160,	0x1D }, { 176,	0x55 },
	{ 192,	0x1E }, { 208,	0x56 }, { 224,	0x22 }, { 228,	0x24 },
	{ 240,	0x1F }, { 256,	0x23 }, { 288,	0x5C }, { 320,	0x25 },
	{ 384,	0x26 }, { 448,	0x2A }, { 480,	0x27 }, { 512,	0x2B },
	{ 576,	0x2C }, { 640,	0x2D }, { 768,	0x31 }, { 896,	0x32 },
	{ 960,	0x2F }, { 1024,	0x33 }, { 1152,	0x34 }, { 1280,	0x35 },
	{ 1536,	0x36 }, { 1792,	0x3A }, { 1920,	0x37 }, { 2048,	0x3B },
	{ 2304,	0x3C }, { 2560,	0x3D }, { 3072,	0x3E }, { 3584,	0x7A },
	{ 3840,	0x3F }, { 4096,	0x7B }, { 5120,	0x7D }, { 6144,	0x7E },
};

#endif
