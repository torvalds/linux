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

#ifndef _E1000_VF_H_
#define _E1000_VF_H_

#include "e1000_osdep.h"
#include "e1000_regs.h"
#include "e1000_defines.h"

struct e1000_hw;

#define E1000_DEV_ID_82576_VF		0x10CA
#define E1000_DEV_ID_I350_VF		0x1520

#define E1000_VF_INIT_TIMEOUT		200 /* Num of retries to clear RSTI */

/* Additional Descriptor Control definitions */
#define E1000_TXDCTL_QUEUE_ENABLE	0x02000000 /* Ena specific Tx Queue */
#define E1000_RXDCTL_QUEUE_ENABLE	0x02000000 /* Ena specific Rx Queue */

/* SRRCTL bit definitions */
#define E1000_SRRCTL(_n)	((_n) < 4 ? (0x0280C + ((_n) * 0x100)) : \
				 (0x0C00C + ((_n) * 0x40)))
#define E1000_SRRCTL_BSIZEPKT_SHIFT		10 /* Shift _right_ */
#define E1000_SRRCTL_BSIZEHDRSIZE_MASK		0x00000F00
#define E1000_SRRCTL_BSIZEHDRSIZE_SHIFT		2  /* Shift _left_ */
#define E1000_SRRCTL_DESCTYPE_LEGACY		0x00000000
#define E1000_SRRCTL_DESCTYPE_ADV_ONEBUF	0x02000000
#define E1000_SRRCTL_DESCTYPE_HDR_SPLIT		0x04000000
#define E1000_SRRCTL_DESCTYPE_HDR_SPLIT_ALWAYS	0x0A000000
#define E1000_SRRCTL_DESCTYPE_HDR_REPLICATION	0x06000000
#define E1000_SRRCTL_DESCTYPE_HDR_REPLICATION_LARGE_PKT 0x08000000
#define E1000_SRRCTL_DESCTYPE_MASK		0x0E000000
#define E1000_SRRCTL_DROP_EN			0x80000000

#define E1000_SRRCTL_BSIZEPKT_MASK	0x0000007F
#define E1000_SRRCTL_BSIZEHDR_MASK	0x00003F00

/* Interrupt Defines */
#define E1000_EICR		0x01580 /* Ext. Interrupt Cause Read - R/clr */
#define E1000_EITR(_n)		(0x01680 + ((_n) << 2))
#define E1000_EICS		0x01520 /* Ext. Intr Cause Set -W0 */
#define E1000_EIMS		0x01524 /* Ext. Intr Mask Set/Read -RW */
#define E1000_EIMC		0x01528 /* Ext. Intr Mask Clear -WO */
#define E1000_EIAC		0x0152C /* Ext. Intr Auto Clear -RW */
#define E1000_EIAM		0x01530 /* Ext. Intr Ack Auto Clear Mask -RW */
#define E1000_IVAR0		0x01700 /* Intr Vector Alloc (array) -RW */
#define E1000_IVAR_MISC		0x01740 /* IVAR for "other" causes -RW */
#define E1000_IVAR_VALID	0x80

/* Receive Descriptor - Advanced */
union e1000_adv_rx_desc {
	struct {
		u64 pkt_addr; /* Packet buffer address */
		u64 hdr_addr; /* Header buffer address */
	} read;
	struct {
		struct {
			union {
				u32 data;
				struct {
					/* RSS type, Packet type */
					u16 pkt_info;
					/* Split Header, header buffer len */
					u16 hdr_info;
				} hs_rss;
			} lo_dword;
			union {
				u32 rss; /* RSS Hash */
				struct {
					u16 ip_id; /* IP id */
					u16 csum; /* Packet Checksum */
				} csum_ip;
			} hi_dword;
		} lower;
		struct {
			u32 status_error; /* ext status/error */
			u16 length; /* Packet length */
			u16 vlan; /* VLAN tag */
		} upper;
	} wb;  /* writeback */
};

#define E1000_RXDADV_HDRBUFLEN_MASK	0x7FE0
#define E1000_RXDADV_HDRBUFLEN_SHIFT	5

/* Transmit Descriptor - Advanced */
union e1000_adv_tx_desc {
	struct {
		u64 buffer_addr;    /* Address of descriptor's data buf */
		u32 cmd_type_len;
		u32 olinfo_status;
	} read;
	struct {
		u64 rsvd;       /* Reserved */
		u32 nxtseq_seed;
		u32 status;
	} wb;
};

/* Adv Transmit Descriptor Config Masks */
#define E1000_ADVTXD_DTYP_CTXT	0x00200000 /* Advanced Context Descriptor */
#define E1000_ADVTXD_DTYP_DATA	0x00300000 /* Advanced Data Descriptor */
#define E1000_ADVTXD_DCMD_EOP	0x01000000 /* End of Packet */
#define E1000_ADVTXD_DCMD_IFCS	0x02000000 /* Insert FCS (Ethernet CRC) */
#define E1000_ADVTXD_DCMD_RS	0x08000000 /* Report Status */
#define E1000_ADVTXD_DCMD_DEXT	0x20000000 /* Descriptor extension (1=Adv) */
#define E1000_ADVTXD_DCMD_VLE	0x40000000 /* VLAN pkt enable */
#define E1000_ADVTXD_DCMD_TSE	0x80000000 /* TCP Seg enable */
#define E1000_ADVTXD_PAYLEN_SHIFT	14 /* Adv desc PAYLEN shift */

/* Context descriptors */
struct e1000_adv_tx_context_desc {
	u32 vlan_macip_lens;
	u32 seqnum_seed;
	u32 type_tucmd_mlhl;
	u32 mss_l4len_idx;
};

#define E1000_ADVTXD_MACLEN_SHIFT	9  /* Adv ctxt desc mac len shift */
#define E1000_ADVTXD_TUCMD_IPV4		0x00000400  /* IP Packet Type: 1=IPv4 */
#define E1000_ADVTXD_TUCMD_L4T_TCP	0x00000800  /* L4 Packet TYPE of TCP */
#define E1000_ADVTXD_L4LEN_SHIFT	8  /* Adv ctxt L4LEN shift */
#define E1000_ADVTXD_MSS_SHIFT		16  /* Adv ctxt MSS shift */

enum e1000_mac_type {
	e1000_undefined = 0,
	e1000_vfadapt,
	e1000_vfadapt_i350,
	e1000_num_macs  /* List is 1-based, so subtract 1 for TRUE count. */
};

struct e1000_vf_stats {
	u64 base_gprc;
	u64 base_gptc;
	u64 base_gorc;
	u64 base_gotc;
	u64 base_mprc;
	u64 base_gotlbc;
	u64 base_gptlbc;
	u64 base_gorlbc;
	u64 base_gprlbc;

	u32 last_gprc;
	u32 last_gptc;
	u32 last_gorc;
	u32 last_gotc;
	u32 last_mprc;
	u32 last_gotlbc;
	u32 last_gptlbc;
	u32 last_gorlbc;
	u32 last_gprlbc;

	u64 gprc;
	u64 gptc;
	u64 gorc;
	u64 gotc;
	u64 mprc;
	u64 gotlbc;
	u64 gptlbc;
	u64 gorlbc;
	u64 gprlbc;
};

#include "e1000_mbx.h"

struct e1000_mac_operations {
	/* Function pointers for the MAC. */
	s32  (*init_params)(struct e1000_hw *);
	s32  (*check_for_link)(struct e1000_hw *);
	void (*clear_vfta)(struct e1000_hw *);
	s32  (*get_bus_info)(struct e1000_hw *);
	s32  (*get_link_up_info)(struct e1000_hw *, u16 *, u16 *);
	void (*update_mc_addr_list)(struct e1000_hw *, u8 *, u32);
	s32  (*reset_hw)(struct e1000_hw *);
	s32  (*init_hw)(struct e1000_hw *);
	s32  (*setup_link)(struct e1000_hw *);
	void (*write_vfta)(struct e1000_hw *, u32, u32);
	int  (*rar_set)(struct e1000_hw *, u8*, u32);
	s32  (*read_mac_addr)(struct e1000_hw *);
};

struct e1000_mac_info {
	struct e1000_mac_operations ops;
	u8 addr[6];
	u8 perm_addr[6];

	enum e1000_mac_type type;

	u16 mta_reg_count;
	u16 rar_entry_count;

	bool get_link_status;
};

struct e1000_mbx_operations {
	s32 (*init_params)(struct e1000_hw *hw);
	s32 (*read)(struct e1000_hw *, u32 *, u16,  u16);
	s32 (*write)(struct e1000_hw *, u32 *, u16, u16);
	s32 (*read_posted)(struct e1000_hw *, u32 *, u16,  u16);
	s32 (*write_posted)(struct e1000_hw *, u32 *, u16, u16);
	s32 (*check_for_msg)(struct e1000_hw *, u16);
	s32 (*check_for_ack)(struct e1000_hw *, u16);
	s32 (*check_for_rst)(struct e1000_hw *, u16);
};

struct e1000_mbx_stats {
	u32 msgs_tx;
	u32 msgs_rx;

	u32 acks;
	u32 reqs;
	u32 rsts;
};

struct e1000_mbx_info {
	struct e1000_mbx_operations ops;
	struct e1000_mbx_stats stats;
	u32 timeout;
	u32 usec_delay;
	u16 size;
};

struct e1000_dev_spec_vf {
	u32 vf_number;
	u32 v2p_mailbox;
};

struct e1000_hw {
	void *back;

	u8 *hw_addr;
	u8 *flash_address;
	unsigned long io_base;

	struct e1000_mac_info  mac;
	struct e1000_mbx_info mbx;

	union {
		struct e1000_dev_spec_vf vf;
	} dev_spec;

	u16 device_id;
	u16 subsystem_vendor_id;
	u16 subsystem_device_id;
	u16 vendor_id;

	u8  revision_id;
};

enum e1000_promisc_type {
	e1000_promisc_disabled = 0,   /* all promisc modes disabled */
	e1000_promisc_unicast = 1,    /* unicast promiscuous enabled */
	e1000_promisc_multicast = 2,  /* multicast promiscuous enabled */
	e1000_promisc_enabled = 3,    /* both uni and multicast promisc */
	e1000_num_promisc_types
};

/* These functions must be implemented by drivers */
s32  e1000_read_pcie_cap_reg(struct e1000_hw *hw, u32 reg, u16 *value);
void e1000_vfta_set_vf(struct e1000_hw *, u16, bool);
void e1000_rlpml_set_vf(struct e1000_hw *, u16);
s32 e1000_promisc_set_vf(struct e1000_hw *, enum e1000_promisc_type);
#endif /* _E1000_VF_H_ */
