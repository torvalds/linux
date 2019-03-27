/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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
 *
 * $OpenBSD: if_urtwnreg.h,v 1.3 2010/11/16 18:02:59 damien Exp $
 * $FreeBSD$
 */

#ifndef R92CU_REG_H
#define R92CU_REG_H

#include <dev/rtwn/rtl8192c/r92c_reg.h>


/*
 * MAC registers.
 */
/* System Configuration. */
#define R92C_USB_SIE_INTF		0x0e0


/*
 * USB registers.
 */
#define R92C_USB_SUSPEND		0xfe10
#define R92C_USB_INFO			0xfe17
#define R92C_USB_SPECIAL_OPTION		0xfe55
#define R92C_USB_HCPWM			0xfe57
#define R92C_USB_HRPWM			0xfe58
#define R92C_USB_DMA_AGG_TO		0xfe5b
#define R92C_USB_AGG_TO			0xfe5c
#define R92C_USB_AGG_TH			0xfe5d
#define R92C_USB_VID			0xfe60
#define R92C_USB_PID			0xfe62
#define R92C_USB_OPTIONAL		0xfe64
#define R92C_USB_EP			0xfe65
#define R92C_USB_PHY			0xfe68
#define R92C_USB_MAC_ADDR		0xfe70
#define R92C_USB_STRING			0xfe80

/* Bits for R92C_USB_SPECIAL_OPTION. */
#define R92C_USB_SPECIAL_OPTION_AGG_EN		0x08
#define R92C_USB_SPECIAL_OPTION_INT_BULK_SEL	0x10

/* Bits for R92C_USB_EP. */
#define R92C_USB_EP_HQ_M	0x000f
#define R92C_USB_EP_HQ_S	0
#define R92C_USB_EP_NQ_M	0x00f0
#define R92C_USB_EP_NQ_S	4
#define R92C_USB_EP_LQ_M	0x0f00
#define R92C_USB_EP_LQ_S	8

#endif	/* R92CU_REG_H */
