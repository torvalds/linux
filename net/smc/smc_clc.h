/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  CLC (connection layer control) handshake over initial TCP socket to
 *  prepare for RDMA traffic
 *
 *  Copyright IBM Corp. 2016
 *
 *  Author(s):  Ursula Braun <ubraun@linux.vnet.ibm.com>
 */

#ifndef _SMC_CLC_H
#define _SMC_CLC_H

#include <rdma/ib_verbs.h>

#include "smc.h"

#define SMC_CLC_PROPOSAL	0x01
#define SMC_CLC_ACCEPT		0x02
#define SMC_CLC_CONFIRM		0x03
#define SMC_CLC_DECLINE		0x04

/* eye catcher "SMCR" EBCDIC for CLC messages */
static const char SMC_EYECATCHER[4] = {'\xe2', '\xd4', '\xc3', '\xd9'};

#define SMC_CLC_V1		0x1		/* SMC version                */
#define CLC_WAIT_TIME		(6 * HZ)	/* max. wait time on clcsock  */
#define SMC_CLC_DECL_MEM	0x01010000  /* insufficient memory resources  */
#define SMC_CLC_DECL_TIMEOUT	0x02000000  /* timeout                        */
#define SMC_CLC_DECL_CNFERR	0x03000000  /* configuration error            */
#define SMC_CLC_DECL_IPSEC	0x03030000  /* IPsec usage                    */
#define SMC_CLC_DECL_SYNCERR	0x04000000  /* synchronization error          */
#define SMC_CLC_DECL_REPLY	0x06000000  /* reply to a received decline    */
#define SMC_CLC_DECL_INTERR	0x99990000  /* internal error                 */
#define SMC_CLC_DECL_TCL	0x02040000  /* timeout w4 QP confirm          */
#define SMC_CLC_DECL_SEND	0x07000000  /* sending problem                */

struct smc_clc_msg_hdr {	/* header1 of clc messages */
	u8 eyecatcher[4];	/* eye catcher */
	u8 type;		/* proposal / accept / confirm / decline */
	__be16 length;
#if defined(__BIG_ENDIAN_BITFIELD)
	u8 version : 4,
	   flag    : 1,
	   rsvd	   : 3;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 rsvd    : 3,
	   flag    : 1,
	   version : 4;
#endif
} __packed;			/* format defined in RFC7609 */

struct smc_clc_msg_trail {	/* trailer of clc messages */
	u8 eyecatcher[4];
};

struct smc_clc_msg_local {	/* header2 of clc messages */
	u8 id_for_peer[SMC_SYSTEMID_LEN]; /* unique system id */
	u8 gid[16];		/* gid of ib_device port */
	u8 mac[6];		/* mac of ib_device port */
};

struct smc_clc_msg_proposal {	/* clc proposal message */
	struct smc_clc_msg_hdr hdr;
	struct smc_clc_msg_local lcl;
	__be16 iparea_offset;	/* offset to IP address information area */
	__be32 outgoing_subnet;	/* subnet mask */
	u8 prefix_len;		/* number of significant bits in mask */
	u8 reserved[2];
	u8 ipv6_prefixes_cnt;	/* number of IPv6 prefixes in prefix array */
	struct smc_clc_msg_trail trl; /* eye catcher "SMCR" EBCDIC */
} __aligned(4);

struct smc_clc_msg_accept_confirm {	/* clc accept / confirm message */
	struct smc_clc_msg_hdr hdr;
	struct smc_clc_msg_local lcl;
	u8 qpn[3];		/* QP number */
	__be32 rmb_rkey;	/* RMB rkey */
	u8 conn_idx;		/* Connection index, which RMBE in RMB */
	__be32 rmbe_alert_token;/* unique connection id */
#if defined(__BIG_ENDIAN_BITFIELD)
	u8 rmbe_size : 4,	/* RMBE buf size (compressed notation) */
	   qp_mtu   : 4;	/* QP mtu */
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 qp_mtu   : 4,
	   rmbe_size : 4;
#endif
	u8 reserved;
	__be64 rmb_dma_addr;	/* RMB virtual address */
	u8 reserved2;
	u8 psn[3];		/* initial packet sequence number */
	struct smc_clc_msg_trail trl; /* eye catcher "SMCR" EBCDIC */
} __packed;			/* format defined in RFC7609 */

struct smc_clc_msg_decline {	/* clc decline message */
	struct smc_clc_msg_hdr hdr;
	u8 id_for_peer[SMC_SYSTEMID_LEN]; /* sender peer_id */
	__be32 peer_diagnosis;	/* diagnosis information */
	u8 reserved2[4];
	struct smc_clc_msg_trail trl; /* eye catcher "SMCR" EBCDIC */
} __aligned(4);

struct smc_sock;
struct smc_ib_device;

int smc_clc_wait_msg(struct smc_sock *smc, void *buf, int buflen,
		     u8 expected_type);
int smc_clc_send_decline(struct smc_sock *smc, u32 peer_diag_info);
int smc_clc_send_proposal(struct smc_sock *smc, struct smc_ib_device *smcibdev,
			  u8 ibport);
int smc_clc_send_confirm(struct smc_sock *smc);
int smc_clc_send_accept(struct smc_sock *smc, int srv_first_contact);

#endif
