/******************************************************************************
  SPDX-License-Identifier: BSD-3-Clause

  Copyright (c) 2001-2017, Intel Corporation
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

#ifndef _IXGBE_DCB_82598_H_
#define _IXGBE_DCB_82598_H_

/* DCB register definitions */

#define IXGBE_DPMCS_MTSOS_SHIFT	16
#define IXGBE_DPMCS_TDPAC	0x00000001 /* 0 Round Robin,
					    * 1 DFP - Deficit Fixed Priority */
#define IXGBE_DPMCS_TRM		0x00000010 /* Transmit Recycle Mode */
#define IXGBE_DPMCS_ARBDIS	0x00000040 /* DCB arbiter disable */
#define IXGBE_DPMCS_TSOEF	0x00080000 /* TSO Expand Factor: 0=x4, 1=x2 */

#define IXGBE_RUPPBMR_MQA	0x80000000 /* Enable UP to queue mapping */

#define IXGBE_RT2CR_MCL_SHIFT	12 /* Offset to Max Credit Limit setting */
#define IXGBE_RT2CR_LSP		0x80000000 /* LSP enable bit */

#define IXGBE_RDRXCTL_MPBEN	0x00000010 /* DMA config for multiple packet
					    * buffers enable */
#define IXGBE_RDRXCTL_MCEN	0x00000040 /* DMA config for multiple cores
					    * (RSS) enable */

#define IXGBE_TDTQ2TCCR_MCL_SHIFT	12
#define IXGBE_TDTQ2TCCR_BWG_SHIFT	9
#define IXGBE_TDTQ2TCCR_GSP	0x40000000
#define IXGBE_TDTQ2TCCR_LSP	0x80000000

#define IXGBE_TDPT2TCCR_MCL_SHIFT	12
#define IXGBE_TDPT2TCCR_BWG_SHIFT	9
#define IXGBE_TDPT2TCCR_GSP	0x40000000
#define IXGBE_TDPT2TCCR_LSP	0x80000000

#define IXGBE_PDPMCS_TPPAC	0x00000020 /* 0 Round Robin,
					    * 1 DFP - Deficit Fixed Priority */
#define IXGBE_PDPMCS_ARBDIS	0x00000040 /* Arbiter disable */
#define IXGBE_PDPMCS_TRM	0x00000100 /* Transmit Recycle Mode enable */

#define IXGBE_DTXCTL_ENDBUBD	0x00000004 /* Enable DBU buffer division */

#define IXGBE_TXPBSIZE_40KB	0x0000A000 /* 40KB Packet Buffer */
#define IXGBE_RXPBSIZE_48KB	0x0000C000 /* 48KB Packet Buffer */
#define IXGBE_RXPBSIZE_64KB	0x00010000 /* 64KB Packet Buffer */
#define IXGBE_RXPBSIZE_80KB	0x00014000 /* 80KB Packet Buffer */

/* DCB driver APIs */

/* DCB PFC */
s32 ixgbe_dcb_config_pfc_82598(struct ixgbe_hw *, u8);

/* DCB stats */
s32 ixgbe_dcb_config_tc_stats_82598(struct ixgbe_hw *);
s32 ixgbe_dcb_get_tc_stats_82598(struct ixgbe_hw *,
				 struct ixgbe_hw_stats *, u8);
s32 ixgbe_dcb_get_pfc_stats_82598(struct ixgbe_hw *,
				  struct ixgbe_hw_stats *, u8);

/* DCB config arbiters */
s32 ixgbe_dcb_config_tx_desc_arbiter_82598(struct ixgbe_hw *, u16 *, u16 *,
					   u8 *, u8 *);
s32 ixgbe_dcb_config_tx_data_arbiter_82598(struct ixgbe_hw *, u16 *, u16 *,
					   u8 *, u8 *);
s32 ixgbe_dcb_config_rx_arbiter_82598(struct ixgbe_hw *, u16 *, u16 *, u8 *);

/* DCB initialization */
s32 ixgbe_dcb_hw_config_82598(struct ixgbe_hw *, int, u16 *, u16 *, u8 *, u8 *);
#endif /* _IXGBE_DCB_82958_H_ */
