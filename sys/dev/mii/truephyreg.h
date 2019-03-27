/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $DragonFly: src/sys/dev/netif/mii_layer/truephyreg.h,v 1.2 2007/10/23 14:28:42 sephe Exp $
 * $FreeBSD$
 */

#ifndef _MII_TRUEPHYREG_H
#define _MII_TRUEPHYREG_H

#define TRUEPHY_INDEX		0x10	/* XXX reserved in DS */
#define TRUEPHY_INDEX_MAGIC	0x402
#define TRUEPHY_DATA		0x11	/* XXX reserved in DS */

#define TRUEPHY_CTRL		0x12
#define TRUEPHY_CTRL_DIAG	0x0004
#define TRUEPHY_CTRL_RSV1	0x0002	/* XXX reserved */
#define TRUEPHY_CTRL_RSV0	0x0001	/* XXX reserved */

#define TRUEPHY_CONF		0x16
#define TRUEPHY_CONF_TXFIFO_MASK 0x3000
#define TRUEPHY_CONF_TXFIFO_8	0x0000
#define TRUEPHY_CONF_TXFIFO_16	0x1000
#define TRUEPHY_CONF_TXFIFO_24	0x2000
#define TRUEPHY_CONF_TXFIFO_32	0x3000

#define TRUEPHY_SR		0x1a
#define TRUEPHY_SR_SPD_MASK	0x0300
#define TRUEPHY_SR_SPD_1000T	0x0200
#define TRUEPHY_SR_SPD_100TX	0x0100
#define TRUEPHY_SR_SPD_10T	0x0000
#define TRUEPHY_SR_FDX		0x0080

#endif	/* !_MII_TRUEPHYREG_H */
