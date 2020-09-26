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

#define SMC_TYPE_R		0		/* SMC-R only		      */
#define SMC_TYPE_D		1		/* SMC-D only		      */
#define SMC_TYPE_N		2		/* neither SMC-R nor SMC-D    */
#define SMC_TYPE_B		3		/* SMC-R and SMC-D	      */
#define CLC_WAIT_TIME		(6 * HZ)	/* max. wait time on clcsock  */
#define CLC_WAIT_TIME_SHORT	HZ		/* short wait time on clcsock */
#define SMC_CLC_DECL_MEM	0x01010000  /* insufficient memory resources  */
#define SMC_CLC_DECL_TIMEOUT_CL	0x02010000  /* timeout w4 QP confirm link     */
#define SMC_CLC_DECL_TIMEOUT_AL	0x02020000  /* timeout w4 QP add link	      */
#define SMC_CLC_DECL_CNFERR	0x03000000  /* configuration error            */
#define SMC_CLC_DECL_PEERNOSMC	0x03010000  /* peer did not indicate SMC      */
#define SMC_CLC_DECL_IPSEC	0x03020000  /* IPsec usage		      */
#define SMC_CLC_DECL_NOSMCDEV	0x03030000  /* no SMC device found (R or D)   */
#define SMC_CLC_DECL_NOSMCDDEV	0x03030001  /* no SMC-D device found	      */
#define SMC_CLC_DECL_NOSMCRDEV	0x03030002  /* no SMC-R device found	      */
#define SMC_CLC_DECL_MODEUNSUPP	0x03040000  /* smc modes do not match (R or D)*/
#define SMC_CLC_DECL_RMBE_EC	0x03050000  /* peer has eyecatcher in RMBE    */
#define SMC_CLC_DECL_OPTUNSUPP	0x03060000  /* fastopen sockopt not supported */
#define SMC_CLC_DECL_DIFFPREFIX	0x03070000  /* IP prefix / subnet mismatch    */
#define SMC_CLC_DECL_GETVLANERR	0x03080000  /* err to get vlan id of ip device*/
#define SMC_CLC_DECL_ISMVLANERR	0x03090000  /* err to reg vlan id on ism dev  */
#define SMC_CLC_DECL_NOACTLINK	0x030a0000  /* no active smc-r link in lgr    */
#define SMC_CLC_DECL_NOSRVLINK	0x030b0000  /* SMC-R link from srv not found  */
#define SMC_CLC_DECL_VERSMISMAT	0x030c0000  /* SMC version mismatch	      */
#define SMC_CLC_DECL_MAX_DMB	0x030d0000  /* SMC-D DMB limit exceeded       */
#define SMC_CLC_DECL_SYNCERR	0x04000000  /* synchronization error          */
#define SMC_CLC_DECL_PEERDECL	0x05000000  /* peer declined during handshake */
#define SMC_CLC_DECL_INTERR	0x09990000  /* internal error		      */
#define SMC_CLC_DECL_ERR_RTOK	0x09990001  /*	 rtoken handling failed       */
#define SMC_CLC_DECL_ERR_RDYLNK	0x09990002  /*	 ib ready link failed	      */
#define SMC_CLC_DECL_ERR_REGRMB	0x09990003  /*	 reg rmb failed		      */

#define SMC_FIRST_CONTACT_MASK	0b10	/* first contact bit within typev2 */

struct smc_clc_msg_hdr {	/* header1 of clc messages */
	u8 eyecatcher[4];	/* eye catcher */
	u8 type;		/* proposal / accept / confirm / decline */
	__be16 length;
#if defined(__BIG_ENDIAN_BITFIELD)
	u8 version : 4,
	   typev2  : 2,
	   typev1  : 2;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 typev1  : 2,
	   typev2  : 2,
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

#define SMC_CLC_MAX_V6_PREFIX	8

/* Struct would be 4 byte aligned, but it is used in an array that is sent
 * to peers and must conform to RFC7609, hence we need to use packed here.
 */
struct smc_clc_ipv6_prefix {
	struct in6_addr prefix;
	u8 prefix_len;
} __packed;			/* format defined in RFC7609 */

struct smc_clc_msg_proposal_prefix {	/* prefix part of clc proposal message*/
	__be32 outgoing_subnet;	/* subnet mask */
	u8 prefix_len;		/* number of significant bits in mask */
	u8 reserved[2];
	u8 ipv6_prefixes_cnt;	/* number of IPv6 prefixes in prefix array */
} __aligned(4);

struct smc_clc_msg_smcd {	/* SMC-D GID information */
	u64 gid;		/* ISM GID of requestor */
	u8 res[32];
};

struct smc_clc_msg_proposal {	/* clc proposal message sent by Linux */
	struct smc_clc_msg_hdr hdr;
	struct smc_clc_msg_local lcl;
	__be16 iparea_offset;	/* offset to IP address information area */
} __aligned(4);

struct smc_clc_msg_proposal_area {
	struct smc_clc_msg_proposal		pclc_base;
	struct smc_clc_msg_smcd			pclc_smcd;
	struct smc_clc_msg_proposal_prefix	pclc_prfx;
	struct smc_clc_ipv6_prefix	pclc_prfx_ipv6[SMC_CLC_MAX_V6_PREFIX];
	struct smc_clc_msg_trail		pclc_trl;
};

struct smcr_clc_msg_accept_confirm {	/* SMCR accept/confirm */
	struct smc_clc_msg_local lcl;
	u8 qpn[3];			/* QP number */
	__be32 rmb_rkey;		/* RMB rkey */
	u8 rmbe_idx;			/* Index of RMBE in RMB */
	__be32 rmbe_alert_token;	/* unique connection id */
 #if defined(__BIG_ENDIAN_BITFIELD)
	u8 rmbe_size : 4,		/* buf size (compressed) */
	   qp_mtu   : 4;		/* QP mtu */
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 qp_mtu   : 4,
	   rmbe_size : 4;
#endif
	u8 reserved;
	__be64 rmb_dma_addr;	/* RMB virtual address */
	u8 reserved2;
	u8 psn[3];		/* packet sequence number */
	struct smc_clc_msg_trail smcr_trl;
				/* eye catcher "SMCR" EBCDIC */
} __packed;

struct smcd_clc_msg_accept_confirm {	/* SMCD accept/confirm */
	u64 gid;		/* Sender GID */
	u64 token;		/* DMB token */
	u8 dmbe_idx;		/* DMBE index */
#if defined(__BIG_ENDIAN_BITFIELD)
	u8 dmbe_size : 4,	/* buf size (compressed) */
	   reserved3 : 4;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 reserved3 : 4,
	   dmbe_size : 4;
#endif
	u16 reserved4;
	u32 linkid;		/* Link identifier */
	u32 reserved5[3];
	struct smc_clc_msg_trail smcd_trl;
				/* eye catcher "SMCD" EBCDIC */
} __packed;

struct smc_clc_msg_accept_confirm {	/* clc accept / confirm message */
	struct smc_clc_msg_hdr hdr;
	union {
		struct smcr_clc_msg_accept_confirm r0; /* SMC-R */
		struct smcd_clc_msg_accept_confirm d0; /* SMC-D */
	};
} __packed;			/* format defined in RFC7609 */

struct smc_clc_msg_decline {	/* clc decline message */
	struct smc_clc_msg_hdr hdr;
	u8 id_for_peer[SMC_SYSTEMID_LEN]; /* sender peer_id */
	__be32 peer_diagnosis;	/* diagnosis information */
	u8 reserved2[4];
	struct smc_clc_msg_trail trl; /* eye catcher "SMCR" EBCDIC */
} __aligned(4);

/* determine start of the prefix area within the proposal message */
static inline struct smc_clc_msg_proposal_prefix *
smc_clc_proposal_get_prefix(struct smc_clc_msg_proposal *pclc)
{
	return (struct smc_clc_msg_proposal_prefix *)
	       ((u8 *)pclc + sizeof(*pclc) + ntohs(pclc->iparea_offset));
}

/* get SMC-D info from proposal message */
static inline struct smc_clc_msg_smcd *
smc_get_clc_msg_smcd(struct smc_clc_msg_proposal *prop)
{
	if (ntohs(prop->iparea_offset) != sizeof(struct smc_clc_msg_smcd))
		return NULL;

	return (struct smc_clc_msg_smcd *)(prop + 1);
}

struct smcd_dev;
struct smc_init_info;

int smc_clc_prfx_match(struct socket *clcsock,
		       struct smc_clc_msg_proposal_prefix *prop);
int smc_clc_wait_msg(struct smc_sock *smc, void *buf, int buflen,
		     u8 expected_type, unsigned long timeout);
int smc_clc_send_decline(struct smc_sock *smc, u32 peer_diag_info);
int smc_clc_send_proposal(struct smc_sock *smc, int smc_type,
			  struct smc_init_info *ini);
int smc_clc_send_confirm(struct smc_sock *smc);
int smc_clc_send_accept(struct smc_sock *smc, bool srv_first_contact);

#endif
