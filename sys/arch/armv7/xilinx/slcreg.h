/*	$OpenBSD: slcreg.h,v 1.1 2021/04/30 13:20:14 visa Exp $	*/

/*
 * Copyright (c) 2021 Visa Hankala
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

#define SLCR_LOCK			0x0004
#define  SLCR_LOCK_KEY				0x767b
#define SLCR_UNLOCK			0x0008
#define  SLCR_UNLOCK_KEY			0xdf0d
#define SLCR_ARM_PLL_CTRL		0x0100
#define SLCR_DDR_PLL_CTRL		0x0104
#define SLCR_IO_PLL_CTRL		0x0108
#define  SLCR_PLL_CTRL_FDIV_MASK		0x7f
#define  SLCR_PLL_CTRL_FDIV_SHIFT		12
#define SLCR_GEM0_CLK_CTRL		0x0140
#define SLCR_GEM1_CLK_CTRL		0x0144
#define SLCR_SDIO_CLK_CTRL		0x0150
#define SLCR_UART_CLK_CTRL		0x0154
#define  SLCR_CLK_CTRL_DIVISOR1(x)		(((x) >> 20) & 0x3f)
#define  SLCR_CLK_CTRL_DIVISOR1_SHIFT		20
#define  SLCR_CLK_CTRL_DIVISOR(x)		(((x) >> 8) & 0x3f)
#define  SLCR_CLK_CTRL_DIVISOR_SHIFT		8
#define  SLCR_CLK_CTRL_SRCSEL_MASK		(0x7 << 4)
#define  SLCR_CLK_CTRL_SRCSEL_DDR		(0x3 << 4)
#define  SLCR_CLK_CTRL_SRCSEL_ARM		(0x2 << 4)
#define  SLCR_CLK_CTRL_SRCSEL_IO		(0x1 << 4)
#define  SLCR_CLK_CTRL_CLKACT(i)		(0x1 << (i))
#define SLCR_PSS_RST_CTRL		0x0200
#define  SLCR_PSS_RST_CTRL_SOFT_RST		(1 << 0)

#define SLCR_DIV_MASK			0x3f

extern struct mutex zynq_slcr_lock;

uint32_t zynq_slcr_read(struct regmap *, uint32_t);
void	zynq_slcr_write(struct regmap *, uint32_t, uint32_t);
