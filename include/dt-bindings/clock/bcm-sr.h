/*
 *  BSD LICENSE
 *
 *  Copyright(c) 2017 Broadcom. All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in
 *      the documentation and/or other materials provided with the
 *      distribution.
 *    * Neither the name of Broadcom Corporation nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _CLOCK_BCM_SR_H
#define _CLOCK_BCM_SR_H

/* GENPLL 0 clock channel ID SCR HSLS FS PCIE */
#define BCM_SR_GENPLL0			0
#define BCM_SR_GENPLL0_SATA_CLK		1
#define BCM_SR_GENPLL0_SCR_CLK		2
#define BCM_SR_GENPLL0_250M_CLK		3
#define BCM_SR_GENPLL0_PCIE_AXI_CLK	4
#define BCM_SR_GENPLL0_PAXC_AXI_X2_CLK	5
#define BCM_SR_GENPLL0_PAXC_AXI_CLK	6

/* GENPLL 1 clock channel ID MHB PCIE NITRO */
#define BCM_SR_GENPLL1			0
#define BCM_SR_GENPLL1_PCIE_TL_CLK	1
#define BCM_SR_GENPLL1_MHB_APB_CLK	2

/* GENPLL 2 clock channel ID NITRO MHB*/
#define BCM_SR_GENPLL2			0
#define BCM_SR_GENPLL2_NIC_CLK		1
#define BCM_SR_GENPLL2_250_NITRO_CLK	2
#define BCM_SR_GENPLL2_125_NITRO_CLK	3
#define BCM_SR_GENPLL2_CHIMP_CLK	4

/* GENPLL 3 HSLS clock channel ID */
#define BCM_SR_GENPLL3			0
#define BCM_SR_GENPLL3_HSLS_CLK		1
#define BCM_SR_GENPLL3_SDIO_CLK		2

/* GENPLL 4 SCR clock channel ID */
#define BCM_SR_GENPLL4			0
#define BCM_SR_GENPLL4_CCN_CLK		1

/* GENPLL 5 FS4 clock channel ID */
#define BCM_SR_GENPLL5			0
#define BCM_SR_GENPLL5_FS_CLK		1
#define BCM_SR_GENPLL5_SPU_CLK		2

/* GENPLL 6 NITRO clock channel ID */
#define BCM_SR_GENPLL6			0
#define BCM_SR_GENPLL6_48_USB_CLK	1

/* LCPLL0  clock channel ID */
#define BCM_SR_LCPLL0			0
#define BCM_SR_LCPLL0_SATA_REF_CLK	1
#define BCM_SR_LCPLL0_USB_REF_CLK	2
#define BCM_SR_LCPLL0_SATA_REFPN_CLK	3

/* LCPLL1  clock channel ID */
#define BCM_SR_LCPLL1			0
#define BCM_SR_LCPLL1_WAN_CLK		1

/* LCPLL PCIE  clock channel ID */
#define BCM_SR_LCPLL_PCIE		0
#define BCM_SR_LCPLL_PCIE_PHY_REF_CLK	1

/* GENPLL EMEM0 clock channel ID */
#define BCM_SR_EMEMPLL0			0
#define BCM_SR_EMEMPLL0_EMEM_CLK	1

/* GENPLL EMEM0 clock channel ID */
#define BCM_SR_EMEMPLL1			0
#define BCM_SR_EMEMPLL1_EMEM_CLK	1

/* GENPLL EMEM0 clock channel ID */
#define BCM_SR_EMEMPLL2			0
#define BCM_SR_EMEMPLL2_EMEM_CLK	1

#endif /* _CLOCK_BCM_SR_H */
