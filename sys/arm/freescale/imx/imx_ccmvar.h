/*-
 * Copyright (c) 2014 Ian Lepore <ian@freebsd.org>
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

#ifndef	IMX_CCMVAR_H
#define	IMX_CCMVAR_H

/*
 * We need a clock management system that works across unrelated SoCs and
 * devices.  For now, to keep imx development moving, define some barebones
 * functionality that can be shared within the imx family by having each SoC
 * implement functions with a common name.
 *
 * The usb enable functions are best-effort.  They turn on the usb otg, host,
 * and phy clocks in a SoC-specific manner, but it may take a lot more than that
 * to make usb work on a given board.  In particular, it can require specific
 * pinmux setup of gpio pins connected to external phy parts, voltage regulators
 * and overcurrent detectors, and so on.  On such boards, u-boot or other early
 * board setup code has to handle those things.
 */

uint32_t imx_ccm_ecspi_hz(void);
uint32_t imx_ccm_ipg_hz(void);
uint32_t imx_ccm_perclk_hz(void);
uint32_t imx_ccm_sdhci_hz(void);
uint32_t imx_ccm_uart_hz(void);
uint32_t imx_ccm_ahb_hz(void);

void imx_ccm_usb_enable(device_t _usbdev);
void imx_ccm_usbphy_enable(device_t _phydev);
void imx_ccm_ssi_configure(device_t _ssidev);
void imx_ccm_hdmi_enable(void);
void imx_ccm_ipu_enable(int ipu);
int  imx6_ccm_sata_enable(void);

/* Routines to get and set the arm clock root divisor register. */
uint32_t imx_ccm_get_cacrr(void);
void     imx_ccm_set_cacrr(uint32_t _divisor);

#endif
