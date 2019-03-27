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

#ifndef _E1000_MBX_H_
#define _E1000_MBX_H_

#include "e1000_api.h"

/* Define mailbox register bits */
#define E1000_V2PMAILBOX_REQ	0x00000001 /* Request for PF Ready bit */
#define E1000_V2PMAILBOX_ACK	0x00000002 /* Ack PF message received */
#define E1000_V2PMAILBOX_VFU	0x00000004 /* VF owns the mailbox buffer */
#define E1000_V2PMAILBOX_PFU	0x00000008 /* PF owns the mailbox buffer */
#define E1000_V2PMAILBOX_PFSTS	0x00000010 /* PF wrote a message in the MB */
#define E1000_V2PMAILBOX_PFACK	0x00000020 /* PF ack the previous VF msg */
#define E1000_V2PMAILBOX_RSTI	0x00000040 /* PF has reset indication */
#define E1000_V2PMAILBOX_RSTD	0x00000080 /* PF has indicated reset done */
#define E1000_V2PMAILBOX_R2C_BITS 0x000000B0 /* All read to clear bits */

#define E1000_P2VMAILBOX_STS	0x00000001 /* Initiate message send to VF */
#define E1000_P2VMAILBOX_ACK	0x00000002 /* Ack message recv'd from VF */
#define E1000_P2VMAILBOX_VFU	0x00000004 /* VF owns the mailbox buffer */
#define E1000_P2VMAILBOX_PFU	0x00000008 /* PF owns the mailbox buffer */
#define E1000_P2VMAILBOX_RVFU	0x00000010 /* Reset VFU - used when VF stuck */

#define E1000_MBVFICR_VFREQ_MASK 0x000000FF /* bits for VF messages */
#define E1000_MBVFICR_VFREQ_VF1	0x00000001 /* bit for VF 1 message */
#define E1000_MBVFICR_VFACK_MASK 0x00FF0000 /* bits for VF acks */
#define E1000_MBVFICR_VFACK_VF1	0x00010000 /* bit for VF 1 ack */

#define E1000_VFMAILBOX_SIZE	16 /* 16 32 bit words - 64 bytes */

/* If it's a E1000_VF_* msg then it originates in the VF and is sent to the
 * PF.  The reverse is TRUE if it is E1000_PF_*.
 * Message ACK's are the value or'd with 0xF0000000
 */
/* Msgs below or'd with this are the ACK */
#define E1000_VT_MSGTYPE_ACK	0x80000000
/* Msgs below or'd with this are the NACK */
#define E1000_VT_MSGTYPE_NACK	0x40000000
/* Indicates that VF is still clear to send requests */
#define E1000_VT_MSGTYPE_CTS	0x20000000
#define E1000_VT_MSGINFO_SHIFT	16
/* bits 23:16 are used for extra info for certain messages */
#define E1000_VT_MSGINFO_MASK	(0xFF << E1000_VT_MSGINFO_SHIFT)

#define E1000_VF_RESET			0x01 /* VF requests reset */
#define E1000_VF_SET_MAC_ADDR		0x02 /* VF requests to set MAC addr */
#define E1000_VF_SET_MULTICAST		0x03 /* VF requests to set MC addr */
#define E1000_VF_SET_MULTICAST_COUNT_MASK (0x1F << E1000_VT_MSGINFO_SHIFT)
#define E1000_VF_SET_MULTICAST_OVERFLOW	(0x80 << E1000_VT_MSGINFO_SHIFT)
#define E1000_VF_SET_VLAN		0x04 /* VF requests to set VLAN */
#define E1000_VF_SET_VLAN_ADD		(0x01 << E1000_VT_MSGINFO_SHIFT)
#define E1000_VF_SET_LPE		0x05 /* reqs to set VMOLR.LPE */
#define E1000_VF_SET_PROMISC		0x06 /* reqs to clear VMOLR.ROPE/MPME*/
#define E1000_VF_SET_PROMISC_UNICAST	(0x01 << E1000_VT_MSGINFO_SHIFT)
#define E1000_VF_SET_PROMISC_MULTICAST	(0x02 << E1000_VT_MSGINFO_SHIFT)

#define E1000_PF_CONTROL_MSG		0x0100 /* PF control message */

#define E1000_VF_MBX_INIT_TIMEOUT	2000 /* number of retries on mailbox */
#define E1000_VF_MBX_INIT_DELAY		500  /* microseconds between retries */

s32 e1000_read_mbx(struct e1000_hw *, u32 *, u16, u16);
s32 e1000_write_mbx(struct e1000_hw *, u32 *, u16, u16);
s32 e1000_read_posted_mbx(struct e1000_hw *, u32 *, u16, u16);
s32 e1000_write_posted_mbx(struct e1000_hw *, u32 *, u16, u16);
s32 e1000_check_for_msg(struct e1000_hw *, u16);
s32 e1000_check_for_ack(struct e1000_hw *, u16);
s32 e1000_check_for_rst(struct e1000_hw *, u16);
void e1000_init_mbx_ops_generic(struct e1000_hw *hw);
s32 e1000_init_mbx_params_vf(struct e1000_hw *);
s32 e1000_init_mbx_params_pf(struct e1000_hw *);

#endif /* _E1000_MBX_H_ */
