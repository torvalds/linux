/******************************************************************************
  SPDX-License-Identifier: BSD-3-Clause

  Copyright (c) 2001-2015, Intel Corporation 
  All rights reserved.
  
  Redistribution and use in source and binary forms, with or without 
  modification, are permitted provided that the following conditions are met:
  
   1. Redistributions of source code must retain the above copyright notice, 
      this list of conditions and the following disclaimer.
  
   2. Redistributions in binary form must reproduce the above copyright 
      notice, this list of conditions and the following disclaimer in the 
      documentation and/or other materials provided with the distribution.
  
   3. Neither the name of the Intel Corporation nor the names of its 
      contributors may be used to endorse or promote products derived from 
      this software without specific prior written permission.
  
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD$*/

#ifndef _E1000_80003ES2LAN_H_
#define _E1000_80003ES2LAN_H_

#define E1000_KMRNCTRLSTA_OFFSET_FIFO_CTRL	0x00
#define E1000_KMRNCTRLSTA_OFFSET_INB_CTRL	0x02
#define E1000_KMRNCTRLSTA_OFFSET_HD_CTRL	0x10
#define E1000_KMRNCTRLSTA_OFFSET_MAC2PHY_OPMODE	0x1F

#define E1000_KMRNCTRLSTA_FIFO_CTRL_RX_BYPASS	0x0008
#define E1000_KMRNCTRLSTA_FIFO_CTRL_TX_BYPASS	0x0800
#define E1000_KMRNCTRLSTA_INB_CTRL_DIS_PADDING	0x0010

#define E1000_KMRNCTRLSTA_HD_CTRL_10_100_DEFAULT 0x0004
#define E1000_KMRNCTRLSTA_HD_CTRL_1000_DEFAULT	0x0000
#define E1000_KMRNCTRLSTA_OPMODE_E_IDLE		0x2000

#define E1000_KMRNCTRLSTA_OPMODE_MASK		0x000C
#define E1000_KMRNCTRLSTA_OPMODE_INBAND_MDIO	0x0004

#define E1000_TCTL_EXT_GCEX_MASK 0x000FFC00 /* Gig Carry Extend Padding */
#define DEFAULT_TCTL_EXT_GCEX_80003ES2LAN	0x00010000

#define DEFAULT_TIPG_IPGT_1000_80003ES2LAN	0x8
#define DEFAULT_TIPG_IPGT_10_100_80003ES2LAN	0x9

/* GG82563 PHY Specific Status Register (Page 0, Register 16 */
#define GG82563_PSCR_POLARITY_REVERSAL_DISABLE	0x0002 /* 1=Reversal Dis */
#define GG82563_PSCR_CROSSOVER_MODE_MASK	0x0060
#define GG82563_PSCR_CROSSOVER_MODE_MDI		0x0000 /* 00=Manual MDI */
#define GG82563_PSCR_CROSSOVER_MODE_MDIX	0x0020 /* 01=Manual MDIX */
#define GG82563_PSCR_CROSSOVER_MODE_AUTO	0x0060 /* 11=Auto crossover */

/* PHY Specific Control Register 2 (Page 0, Register 26) */
#define GG82563_PSCR2_REVERSE_AUTO_NEG		0x2000 /* 1=Reverse Auto-Neg */

/* MAC Specific Control Register (Page 2, Register 21) */
/* Tx clock speed for Link Down and 1000BASE-T for the following speeds */
#define GG82563_MSCR_TX_CLK_MASK		0x0007
#define GG82563_MSCR_TX_CLK_10MBPS_2_5		0x0004
#define GG82563_MSCR_TX_CLK_100MBPS_25		0x0005
#define GG82563_MSCR_TX_CLK_1000MBPS_25		0x0007

#define GG82563_MSCR_ASSERT_CRS_ON_TX		0x0010 /* 1=Assert */

/* DSP Distance Register (Page 5, Register 26)
 * 0 = <50M
 * 1 = 50-80M
 * 2 = 80-100M
 * 3 = 110-140M
 * 4 = >140M
 */
#define GG82563_DSPD_CABLE_LENGTH		0x0007

/* Kumeran Mode Control Register (Page 193, Register 16) */
#define GG82563_KMCR_PASS_FALSE_CARRIER		0x0800

/* Max number of times Kumeran read/write should be validated */
#define GG82563_MAX_KMRN_RETRY			0x5

/* Power Management Control Register (Page 193, Register 20) */
/* 1=Enable SERDES Electrical Idle */
#define GG82563_PMCR_ENABLE_ELECTRICAL_IDLE	0x0001

/* In-Band Control Register (Page 194, Register 18) */
#define GG82563_ICR_DIS_PADDING			0x0010 /* Disable Padding */

#endif
