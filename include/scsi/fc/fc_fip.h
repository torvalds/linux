/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef _FC_FIP_H_
#define _FC_FIP_H_

/*
 * This version is based on:
 * http://www.t11.org/ftp/t11/pub/fc/bb-5/08-543v1.pdf
 */

#define FIP_DEF_PRI	128	/* default selection priority */
#define FIP_DEF_FC_MAP	0x0efc00 /* default FCoE MAP (MAC OUI) value */
#define FIP_DEF_FKA	8000	/* default FCF keep-alive/advert period (mS) */
#define FIP_VN_KA_PERIOD 90000	/* required VN_port keep-alive period (mS) */
#define FIP_FCF_FUZZ	100	/* random time added by FCF (mS) */

/*
 * Multicast MAC addresses.  T11-adopted.
 */
#define FIP_ALL_FCOE_MACS	((u8[6]) { 1, 0x10, 0x18, 1, 0, 0 })
#define FIP_ALL_ENODE_MACS	((u8[6]) { 1, 0x10, 0x18, 1, 0, 1 })
#define FIP_ALL_FCF_MACS	((u8[6]) { 1, 0x10, 0x18, 1, 0, 2 })

#define FIP_VER		1		/* version for fip_header */

struct fip_header {
	__u8	fip_ver;		/* upper 4 bits are the version */
	__u8	fip_resv1;		/* reserved */
	__be16	fip_op;			/* operation code */
	__u8	fip_resv2;		/* reserved */
	__u8	fip_subcode;		/* lower 4 bits are sub-code */
	__be16	fip_dl_len;		/* length of descriptors in words */
	__be16	fip_flags;		/* header flags */
} __attribute__((packed));

#define FIP_VER_SHIFT	4
#define FIP_VER_ENCAPS(v) ((v) << FIP_VER_SHIFT)
#define FIP_VER_DECAPS(v) ((v) >> FIP_VER_SHIFT)
#define FIP_BPW		4		/* bytes per word for lengths */

/*
 * fip_op.
 */
enum fip_opcode {
	FIP_OP_DISC =	1,		/* discovery, advertisement, etc. */
	FIP_OP_LS =	2,		/* Link Service request or reply */
	FIP_OP_CTRL =	3,		/* Keep Alive / Link Reset */
	FIP_OP_VLAN =	4,		/* VLAN discovery */
	FIP_OP_VENDOR_MIN = 0xfff8,	/* min vendor-specific opcode */
	FIP_OP_VENDOR_MAX = 0xfffe,	/* max vendor-specific opcode */
};

/*
 * Subcodes for FIP_OP_DISC.
 */
enum fip_disc_subcode {
	FIP_SC_SOL =	1,		/* solicitation */
	FIP_SC_ADV =	2,		/* advertisement */
};

/*
 * Subcodes for FIP_OP_LS.
 */
enum fip_trans_subcode {
	FIP_SC_REQ =	1,		/* request */
	FIP_SC_REP =	2,		/* reply */
};

/*
 * Subcodes for FIP_OP_RESET.
 */
enum fip_reset_subcode {
	FIP_SC_KEEP_ALIVE = 1,		/* keep-alive from VN_Port */
	FIP_SC_CLR_VLINK = 2,		/* clear virtual link from VF_Port */
};

/*
 * Subcodes for FIP_OP_VLAN.
 */
enum fip_vlan_subcode {
	FIP_SC_VL_REQ =	1,		/* request */
	FIP_SC_VL_REP =	2,		/* reply */
};

/*
 * flags in header fip_flags.
 */
enum fip_flag {
	FIP_FL_FPMA =	0x8000,		/* supports FPMA fabric-provided MACs */
	FIP_FL_SPMA =	0x4000,		/* supports SPMA server-provided MACs */
	FIP_FL_AVAIL =	0x0004,		/* available for FLOGI/ELP */
	FIP_FL_SOL =	0x0002,		/* this is a solicited message */
	FIP_FL_FPORT =	0x0001,		/* sent from an F port */
};

/*
 * Common descriptor header format.
 */
struct fip_desc {
	__u8	fip_dtype;		/* type - see below */
	__u8	fip_dlen;		/* length - in 32-bit words */
};

enum fip_desc_type {
	FIP_DT_PRI =	1,		/* priority for forwarder selection */
	FIP_DT_MAC =	2,		/* MAC address */
	FIP_DT_MAP_OUI = 3,		/* FC-MAP OUI */
	FIP_DT_NAME =	4,		/* switch name or node name */
	FIP_DT_FAB =	5,		/* fabric descriptor */
	FIP_DT_FCOE_SIZE = 6,		/* max FCoE frame size */
	FIP_DT_FLOGI =	7,		/* FLOGI request or response */
	FIP_DT_FDISC =	8,		/* FDISC request or response */
	FIP_DT_LOGO =	9,		/* LOGO request or response */
	FIP_DT_ELP =	10,		/* ELP request or response */
	FIP_DT_VN_ID =	11,		/* VN_Node Identifier */
	FIP_DT_FKA =	12,		/* advertisement keep-alive period */
	FIP_DT_VENDOR =	13,		/* vendor ID */
	FIP_DT_VLAN =	14,		/* vlan number */
	FIP_DT_LIMIT,			/* max defined desc_type + 1 */
	FIP_DT_VENDOR_BASE = 128,	/* first vendor-specific desc_type */
};

/*
 * FIP_DT_PRI - priority descriptor.
 */
struct fip_pri_desc {
	struct fip_desc fd_desc;
	__u8		fd_resvd;
	__u8		fd_pri;		/* FCF priority:  higher is better */
} __attribute__((packed));

/*
 * FIP_DT_MAC - MAC address descriptor.
 */
struct fip_mac_desc {
	struct fip_desc fd_desc;
	__u8		fd_mac[ETH_ALEN];
} __attribute__((packed));

/*
 * FIP_DT_MAP - descriptor.
 */
struct fip_map_desc {
	struct fip_desc fd_desc;
	__u8		fd_resvd[3];
	__u8		fd_map[3];
} __attribute__((packed));

/*
 * FIP_DT_NAME descriptor.
 */
struct fip_wwn_desc {
	struct fip_desc fd_desc;
	__u8		fd_resvd[2];
	__be64		fd_wwn;		/* 64-bit WWN, unaligned */
} __attribute__((packed));

/*
 * FIP_DT_FAB descriptor.
 */
struct fip_fab_desc {
	struct fip_desc fd_desc;
	__be16		fd_vfid;	/* virtual fabric ID */
	__u8		fd_resvd;
	__u8		fd_map[3];	/* FC-MAP value */
	__be64		fd_wwn;		/* fabric name, unaligned */
} __attribute__((packed));

/*
 * FIP_DT_FCOE_SIZE descriptor.
 */
struct fip_size_desc {
	struct fip_desc fd_desc;
	__be16		fd_size;
} __attribute__((packed));

/*
 * Descriptor that encapsulates an ELS or ILS frame.
 * The encapsulated frame immediately follows this header, without
 * SOF, EOF, or CRC.
 */
struct fip_encaps {
	struct fip_desc fd_desc;
	__u8		fd_resvd[2];
} __attribute__((packed));

/*
 * FIP_DT_VN_ID - VN_Node Identifier descriptor.
 */
struct fip_vn_desc {
	struct fip_desc fd_desc;
	__u8		fd_mac[ETH_ALEN];
	__u8		fd_resvd;
	__u8		fd_fc_id[3];
	__be64		fd_wwpn;	/* port name, unaligned */
} __attribute__((packed));

/*
 * FIP_DT_FKA - Advertisement keep-alive period.
 */
struct fip_fka_desc {
	struct fip_desc fd_desc;
	__u8		fd_resvd[2];
	__be32		fd_fka_period;	/* adv./keep-alive period in mS */
} __attribute__((packed));

/*
 * FIP_DT_VENDOR descriptor.
 */
struct fip_vendor_desc {
	struct fip_desc fd_desc;
	__u8		fd_resvd[2];
	__u8		fd_vendor_id[8];
} __attribute__((packed));

#endif /* _FC_FIP_H_ */
