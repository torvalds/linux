/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2003-2011 Netlogic Microsystems (Netlogic). All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY Netlogic Microsystems ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * NETLOGIC_BSD
 * $FreeBSD$
 */

#ifndef __NLM_USB_H__
#define __NLM_USB_H__

#define USB_CTL_0			0x01
#define USB_PHY_0			0x0A
#define USB_PHY_RESET			0x01
#define USB_PHY_PORT_RESET_0		0x10
#define USB_PHY_PORT_RESET_1		0x20
#define USB_CONTROLLER_RESET		0x01
#define USB_INT_STATUS			0x0E
#define USB_INT_EN			0x0F
#define USB_PHY_INTERRUPT_EN		0x01
#define USB_OHCI_INTERRUPT_EN		0x02
#define USB_OHCI_INTERRUPT1_EN		0x04
#define USB_OHCI_INTERRUPT2_EN		0x08
#define USB_CTRL_INTERRUPT_EN		0x10


#if !defined(LOCORE) && !defined(__ASSEMBLY__)

#define nlm_read_usb_reg(b, r)		nlm_read_reg(b,r)
#define nlm_write_usb_reg(b, r, v)	nlm_write_reg(b,r,v)
#define	nlm_get_usb_pcibase(node, inst)	nlm_pcicfg_base(XLP_IO_USB_OFFSET(node, inst))
#define	nlm_get_usb_hcd_base(node, inst) nlm_xkphys_map_pcibar0(nlm_get_usb_pcibase(node, inst))
#define	nlm_get_usb_regbase(node, inst)	(nlm_get_usb_pcibase(node, inst) + XLP_IO_PCI_HDRSZ)

#endif
#endif
