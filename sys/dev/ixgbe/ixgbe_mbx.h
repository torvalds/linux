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

#ifndef _IXGBE_MBX_H_
#define _IXGBE_MBX_H_

#include "ixgbe_type.h"

#define IXGBE_VFMAILBOX_SIZE	16 /* 16 32 bit words - 64 bytes */
#define IXGBE_ERR_MBX		-100

#define IXGBE_VFMAILBOX		0x002FC
#define IXGBE_VFMBMEM		0x00200

/* Define mailbox register bits */
#define IXGBE_VFMAILBOX_REQ	0x00000001 /* Request for PF Ready bit */
#define IXGBE_VFMAILBOX_ACK	0x00000002 /* Ack PF message received */
#define IXGBE_VFMAILBOX_VFU	0x00000004 /* VF owns the mailbox buffer */
#define IXGBE_VFMAILBOX_PFU	0x00000008 /* PF owns the mailbox buffer */
#define IXGBE_VFMAILBOX_PFSTS	0x00000010 /* PF wrote a message in the MB */
#define IXGBE_VFMAILBOX_PFACK	0x00000020 /* PF ack the previous VF msg */
#define IXGBE_VFMAILBOX_RSTI	0x00000040 /* PF has reset indication */
#define IXGBE_VFMAILBOX_RSTD	0x00000080 /* PF has indicated reset done */
#define IXGBE_VFMAILBOX_R2C_BITS	0x000000B0 /* All read to clear bits */

#define IXGBE_PFMAILBOX_STS	0x00000001 /* Initiate message send to VF */
#define IXGBE_PFMAILBOX_ACK	0x00000002 /* Ack message recv'd from VF */
#define IXGBE_PFMAILBOX_VFU	0x00000004 /* VF owns the mailbox buffer */
#define IXGBE_PFMAILBOX_PFU	0x00000008 /* PF owns the mailbox buffer */
#define IXGBE_PFMAILBOX_RVFU	0x00000010 /* Reset VFU - used when VF stuck */

#define IXGBE_MBVFICR_VFREQ_MASK	0x0000FFFF /* bits for VF messages */
#define IXGBE_MBVFICR_VFREQ_VF1		0x00000001 /* bit for VF 1 message */
#define IXGBE_MBVFICR_VFACK_MASK	0xFFFF0000 /* bits for VF acks */
#define IXGBE_MBVFICR_VFACK_VF1		0x00010000 /* bit for VF 1 ack */


/* If it's a IXGBE_VF_* msg then it originates in the VF and is sent to the
 * PF.  The reverse is TRUE if it is IXGBE_PF_*.
 * Message ACK's are the value or'd with 0xF0000000
 */
#define IXGBE_VT_MSGTYPE_ACK	0x80000000 /* Messages below or'd with
					    * this are the ACK */
#define IXGBE_VT_MSGTYPE_NACK	0x40000000 /* Messages below or'd with
					    * this are the NACK */
#define IXGBE_VT_MSGTYPE_CTS	0x20000000 /* Indicates that VF is still
					    * clear to send requests */
#define IXGBE_VT_MSGINFO_SHIFT	16
/* bits 23:16 are used for extra info for certain messages */
#define IXGBE_VT_MSGINFO_MASK	(0xFF << IXGBE_VT_MSGINFO_SHIFT)

/* definitions to support mailbox API version negotiation */

/*
 * each element denotes a version of the API; existing numbers may not
 * change; any additions must go at the end
 */
enum ixgbe_pfvf_api_rev {
	ixgbe_mbox_api_10,	/* API version 1.0, linux/freebsd VF driver */
	ixgbe_mbox_api_20,	/* API version 2.0, solaris Phase1 VF driver */
	ixgbe_mbox_api_11,	/* API version 1.1, linux/freebsd VF driver */
	ixgbe_mbox_api_12,	/* API version 1.2, linux/freebsd VF driver */
	ixgbe_mbox_api_13,	/* API version 1.3, linux/freebsd VF driver */
	/* This value should always be last */
	ixgbe_mbox_api_unknown,	/* indicates that API version is not known */
};

/* mailbox API, legacy requests */
#define IXGBE_VF_RESET		0x01 /* VF requests reset */
#define IXGBE_VF_SET_MAC_ADDR	0x02 /* VF requests PF to set MAC addr */
#define IXGBE_VF_SET_MULTICAST	0x03 /* VF requests PF to set MC addr */
#define IXGBE_VF_SET_VLAN	0x04 /* VF requests PF to set VLAN */

/* mailbox API, version 1.0 VF requests */
#define IXGBE_VF_SET_LPE	0x05 /* VF requests PF to set VMOLR.LPE */
#define IXGBE_VF_SET_MACVLAN	0x06 /* VF requests PF for unicast filter */
#define IXGBE_VF_API_NEGOTIATE	0x08 /* negotiate API version */

/* mailbox API, version 1.1 VF requests */
#define IXGBE_VF_GET_QUEUES	0x09 /* get queue configuration */

/* mailbox API, version 1.2 VF requests */
#define IXGBE_VF_GET_RETA      0x0a    /* VF request for RETA */
#define IXGBE_VF_GET_RSS_KEY	0x0b    /* get RSS key */
#define IXGBE_VF_UPDATE_XCAST_MODE	0x0c

/* mode choices for IXGBE_VF_UPDATE_XCAST_MODE */
enum ixgbevf_xcast_modes {
	IXGBEVF_XCAST_MODE_NONE = 0,
	IXGBEVF_XCAST_MODE_MULTI,
	IXGBEVF_XCAST_MODE_ALLMULTI,
	IXGBEVF_XCAST_MODE_PROMISC,
};

/* GET_QUEUES return data indices within the mailbox */
#define IXGBE_VF_TX_QUEUES	1	/* number of Tx queues supported */
#define IXGBE_VF_RX_QUEUES	2	/* number of Rx queues supported */
#define IXGBE_VF_TRANS_VLAN	3	/* Indication of port vlan */
#define IXGBE_VF_DEF_QUEUE	4	/* Default queue offset */

/* length of permanent address message returned from PF */
#define IXGBE_VF_PERMADDR_MSG_LEN	4
/* word in permanent address message with the current multicast type */
#define IXGBE_VF_MC_TYPE_WORD		3

#define IXGBE_PF_CONTROL_MSG		0x0100 /* PF control message */

/* mailbox API, version 2.0 VF requests */
#define IXGBE_VF_API_NEGOTIATE		0x08 /* negotiate API version */
#define IXGBE_VF_GET_QUEUES		0x09 /* get queue configuration */
#define IXGBE_VF_ENABLE_MACADDR		0x0A /* enable MAC address */
#define IXGBE_VF_DISABLE_MACADDR	0x0B /* disable MAC address */
#define IXGBE_VF_GET_MACADDRS		0x0C /* get all configured MAC addrs */
#define IXGBE_VF_SET_MCAST_PROMISC	0x0D /* enable multicast promiscuous */
#define IXGBE_VF_GET_MTU		0x0E /* get bounds on MTU */
#define IXGBE_VF_SET_MTU		0x0F /* set a specific MTU */

/* mailbox API, version 2.0 PF requests */
#define IXGBE_PF_TRANSPARENT_VLAN	0x0101 /* enable transparent vlan */

#define IXGBE_VF_MBX_INIT_TIMEOUT	2000 /* number of retries on mailbox */
#define IXGBE_VF_MBX_INIT_DELAY		500  /* microseconds between retries */

void ixgbe_init_mbx_ops_generic(struct ixgbe_hw *hw);
void ixgbe_init_mbx_params_vf(struct ixgbe_hw *);
void ixgbe_init_mbx_params_pf(struct ixgbe_hw *);

#endif /* _IXGBE_MBX_H_ */
