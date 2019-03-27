/*-
 * Copyright (c) 2016 Rubicon Communications, LLC (Netgate)
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

#ifndef	_TI_SPIREG_H_
#define	_TI_SPIREG_H_

#define	TI_SPI_GCLK			48000000U
#define	TI_SPI_FIFOSZ			32
#define	MCSPI_REVISION			0x0
#define	 MCSPI_REVISION_SCHEME_SHIFT	30
#define	 MCSPI_REVISION_SCHEME_MSK	0x3
#define	 MCSPI_REVISION_FUNC_SHIFT	16
#define	 MCSPI_REVISION_FUNC_MSK	0xfff
#define	 MCSPI_REVISION_RTL_SHIFT	11
#define	 MCSPI_REVISION_RTL_MSK		0x1f
#define	 MCSPI_REVISION_MAJOR_SHIFT	8
#define	 MCSPI_REVISION_MAJOR_MSK	0x7
#define	 MCSPI_REVISION_CUSTOM_SHIFT	6
#define	 MCSPI_REVISION_CUSTOM_MSK	0x3
#define	 MCSPI_REVISION_MINOR_SHIFT	0
#define	 MCSPI_REVISION_MINOR_MSK	0x3f
#define	MCSPI_SYSCONFIG			0x110
#define	 MCSPI_SYSCONFIG_SOFTRESET	(1 << 1)
#define	MCSPI_SYSSTATUS			0x114
#define	 MCSPI_SYSSTATUS_RESETDONE	(1 << 0)
#define	MCSPI_MODULCTRL			0x128
#define	 MCSPI_MODULCTRL_SLAVE		(1 << 2)
#define	 MCSPI_MODULCTRL_SINGLE		(1 << 0)
#define	MCSPI_IRQSTATUS			0x118
#define	MCSPI_IRQENABLE			0x11c
#define	 MCSPI_IRQ_EOW			(1 << 17)
#define	 MCSPI_IRQ_RX0_OVERFLOW		(1 << 3)
#define	 MCSPI_IRQ_RX0_FULL		(1 << 2)
#define	 MCSPI_IRQ_TX0_UNDERFLOW	(1 << 1)
#define	 MCSPI_IRQ_TX0_EMPTY		(1 << 0)
#define	MCSPI_CONF_CH(_c)		(0x12c + 0x14 * (_c))
#define	 MCSPI_CONF_CLKG		(1 << 29)
#define	 MCSPI_CONF_FFER		(1 << 28)
#define	 MCSPI_CONF_FFEW		(1 << 27)
#define	 MCSPI_CONF_SBPOL		(1 << 24)
#define	 MCSPI_CONF_SBE			(1 << 23)
#define	 MCSPI_CONF_FORCE		(1 << 20)
#define	 MCSPI_CONF_TURBO		(1 << 19)
#define	 MCSPI_CONF_IS			(1 << 18)
#define	 MCSPI_CONF_DPE1		(1 << 17)
#define	 MCSPI_CONF_DPE0		(1 << 16)
#define	 MCSPI_CONF_DMAR		(1 << 15)
#define	 MCSPI_CONF_DMAW		(1 << 14)
#define	 MCSPI_CONF_WL_MSK		0x1f
#define	 MCSPI_CONF_WL_SHIFT		7
#define	 MCSPI_CONF_WL8BITS		(7 << MCSPI_CONF_WL_SHIFT)
#define	 MCSPI_CONF_EPOL		(1 << 6)
#define	 MCSPI_CONF_CLK_MSK		0xf
#define	 MCSPI_CONF_CLK_SHIFT		2
#define	 MCSPI_CONF_POL			(1 << 1)
#define	 MCSPI_CONF_PHA			(1 << 0)
#define	MCSPI_STAT_CH(_c)		(0x130 + 0x14 * (_c))
#define	 MCSPI_STAT_TXFFF		(1 << 4)
#define	 MCSPI_STAT_TXS			(1 << 1)
#define	 MCSPI_STAT_RXS			(1 << 0)
#define	MCSPI_CTRL_CH(_c)		(0x134 + 0x14 * (_c))
#define	 MCSPI_EXTCLK_MSK		0xfff
#define	 MCSPI_CTRL_EXTCLK_MSK		0xff
#define	 MCSPI_CTRL_EXTCLK_SHIFT	8
#define	 MCSPI_CTRL_ENABLE		(1 << 0)
#define	MCSPI_TX_CH(_c)			(0x138 + 0x14 * (_c))
#define	MCSPI_RX_CH(_c)			(0x13c + 0x14 * (_c))
#define	MCSPI_XFERLEVEL			0x17c
#define	 MCSPI_XFERLEVEL_AFL(_a)	(((_a) >> 8) & 0xff)
#define	 MCSPI_XFERLEVEL_AEL(_a)	(((_a) >> 0) & 0xff)

#endif	/* _TI_SPIREG_H_ */
