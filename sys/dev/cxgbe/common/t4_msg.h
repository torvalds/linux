/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011, 2016 Chelsio Communications, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef T4_MSG_H
#define T4_MSG_H

enum {
	CPL_PASS_OPEN_REQ     = 0x1,
	CPL_PASS_ACCEPT_RPL   = 0x2,
	CPL_ACT_OPEN_REQ      = 0x3,
	CPL_SET_TCB           = 0x4,
	CPL_SET_TCB_FIELD     = 0x5,
	CPL_GET_TCB           = 0x6,
	CPL_CLOSE_CON_REQ     = 0x8,
	CPL_CLOSE_LISTSRV_REQ = 0x9,
	CPL_ABORT_REQ         = 0xA,
	CPL_ABORT_RPL         = 0xB,
	CPL_TX_DATA           = 0xC,
	CPL_RX_DATA_ACK       = 0xD,
	CPL_TX_PKT            = 0xE,
	CPL_RTE_DELETE_REQ    = 0xF,
	CPL_RTE_WRITE_REQ     = 0x10,
	CPL_RTE_READ_REQ      = 0x11,
	CPL_L2T_WRITE_REQ     = 0x12,
	CPL_L2T_READ_REQ      = 0x13,
	CPL_SMT_WRITE_REQ     = 0x14,
	CPL_SMT_READ_REQ      = 0x15,
	CPL_TAG_WRITE_REQ     = 0x16,
	CPL_BARRIER           = 0x18,
	CPL_TID_RELEASE       = 0x1A,
	CPL_TAG_READ_REQ      = 0x1B,
	CPL_SRQ_TABLE_REQ     = 0x1C,
	CPL_TX_PKT_FSO        = 0x1E,
	CPL_TX_DATA_ISO       = 0x1F,

	CPL_CLOSE_LISTSRV_RPL = 0x20,
	CPL_ERROR             = 0x21,
	CPL_GET_TCB_RPL       = 0x22,
	CPL_L2T_WRITE_RPL     = 0x23,
	CPL_PASS_OPEN_RPL     = 0x24,
	CPL_ACT_OPEN_RPL      = 0x25,
	CPL_PEER_CLOSE        = 0x26,
	CPL_RTE_DELETE_RPL    = 0x27,
	CPL_RTE_WRITE_RPL     = 0x28,
	CPL_RX_URG_PKT        = 0x29,
	CPL_TAG_WRITE_RPL     = 0x2A,
	CPL_ABORT_REQ_RSS     = 0x2B,
	CPL_RX_URG_NOTIFY     = 0x2C,
	CPL_ABORT_RPL_RSS     = 0x2D,
	CPL_SMT_WRITE_RPL     = 0x2E,
	CPL_TX_DATA_ACK       = 0x2F,

	CPL_RX_PHYS_ADDR      = 0x30,
	CPL_PCMD_READ_RPL     = 0x31,
	CPL_CLOSE_CON_RPL     = 0x32,
	CPL_ISCSI_HDR         = 0x33,
	CPL_L2T_READ_RPL      = 0x34,
	CPL_RDMA_CQE          = 0x35,
	CPL_RDMA_CQE_READ_RSP = 0x36,
	CPL_RDMA_CQE_ERR      = 0x37,
	CPL_RTE_READ_RPL      = 0x38,
	CPL_RX_DATA           = 0x39,
	CPL_SET_TCB_RPL       = 0x3A,
	CPL_RX_PKT            = 0x3B,
	CPL_TAG_READ_RPL      = 0x3C,
	CPL_HIT_NOTIFY        = 0x3D,
	CPL_PKT_NOTIFY        = 0x3E,
	CPL_RX_DDP_COMPLETE   = 0x3F,

	CPL_ACT_ESTABLISH     = 0x40,
	CPL_PASS_ESTABLISH    = 0x41,
	CPL_RX_DATA_DDP       = 0x42,
	CPL_SMT_READ_RPL      = 0x43,
	CPL_PASS_ACCEPT_REQ   = 0x44,
	CPL_RX_ISCSI_CMP      = 0x45,
	CPL_RX_FCOE_DDP       = 0x46,
	CPL_FCOE_HDR          = 0x47,
	CPL_T5_TRACE_PKT      = 0x48,
	CPL_RX_ISCSI_DDP      = 0x49,
	CPL_RX_FCOE_DIF       = 0x4A,
	CPL_RX_DATA_DIF       = 0x4B,
	CPL_ERR_NOTIFY	      = 0x4D,
	CPL_RX_TLS_CMP        = 0x4E,

	CPL_RDMA_READ_REQ     = 0x60,
	CPL_RX_ISCSI_DIF      = 0x60,

	CPL_SET_LE_REQ        = 0x80,
	CPL_PASS_OPEN_REQ6    = 0x81,
	CPL_ACT_OPEN_REQ6     = 0x83,
	CPL_TX_TLS_PDU        = 0x88,
	CPL_TX_TLS_SFO        = 0x89,

	CPL_TX_SEC_PDU        = 0x8A,
	CPL_TX_TLS_ACK        = 0x8B,

	CPL_RDMA_TERMINATE    = 0xA2,
	CPL_RDMA_WRITE        = 0xA4,
	CPL_SGE_EGR_UPDATE    = 0xA5,
	CPL_SET_LE_RPL        = 0xA6,
	CPL_FW2_MSG           = 0xA7,
	CPL_FW2_PLD           = 0xA8,
	CPL_T5_RDMA_READ_REQ  = 0xA9,
	CPL_RDMA_ATOMIC_REQ   = 0xAA,
	CPL_RDMA_ATOMIC_RPL   = 0xAB,
	CPL_RDMA_IMM_DATA     = 0xAC,
	CPL_RDMA_IMM_DATA_SE  = 0xAD,
	CPL_RX_MPS_PKT        = 0xAF,

	CPL_TRACE_PKT         = 0xB0,
	CPL_RX2TX_DATA        = 0xB1,
	CPL_TLS_DATA          = 0xB1,
	CPL_ISCSI_DATA        = 0xB2,
	CPL_FCOE_DATA         = 0xB3,

	CPL_FW4_MSG           = 0xC0,
	CPL_FW4_PLD           = 0xC1,
	CPL_FW4_ACK           = 0xC3,
	CPL_SRQ_TABLE_RPL     = 0xCC,
	CPL_RX_PHYS_DSGL      = 0xD0,

	CPL_FW6_MSG           = 0xE0,
	CPL_FW6_PLD           = 0xE1,
	CPL_TX_TNL_LSO        = 0xEC,
	CPL_TX_PKT_LSO        = 0xED,
	CPL_TX_PKT_XT         = 0xEE,

	NUM_CPL_CMDS    /* must be last and previous entries must be sorted */
};

enum CPL_error {
	CPL_ERR_NONE               = 0,
	CPL_ERR_TCAM_PARITY        = 1,
	CPL_ERR_TCAM_MISS          = 2,
	CPL_ERR_TCAM_FULL          = 3,
	CPL_ERR_BAD_LENGTH         = 15,
	CPL_ERR_BAD_ROUTE          = 18,
	CPL_ERR_CONN_RESET         = 20,
	CPL_ERR_CONN_EXIST_SYNRECV = 21,
	CPL_ERR_CONN_EXIST         = 22,
	CPL_ERR_ARP_MISS           = 23,
	CPL_ERR_BAD_SYN            = 24,
	CPL_ERR_CONN_TIMEDOUT      = 30,
	CPL_ERR_XMIT_TIMEDOUT      = 31,
	CPL_ERR_PERSIST_TIMEDOUT   = 32,
	CPL_ERR_FINWAIT2_TIMEDOUT  = 33,
	CPL_ERR_KEEPALIVE_TIMEDOUT = 34,
	CPL_ERR_RTX_NEG_ADVICE     = 35,
	CPL_ERR_PERSIST_NEG_ADVICE = 36,
	CPL_ERR_KEEPALV_NEG_ADVICE = 37,
	CPL_ERR_WAIT_ARP_RPL       = 41,
	CPL_ERR_ABORT_FAILED       = 42,
	CPL_ERR_IWARP_FLM          = 50,
	CPL_CONTAINS_READ_RPL      = 60,
	CPL_CONTAINS_WRITE_RPL     = 61,
};

/*
 * Some of the error codes above implicitly indicate that there is no TID
 * allocated with the result of an ACT_OPEN.  We use this predicate to make
 * that explicit.
 */
static inline int act_open_has_tid(int status)
{
	return (status != CPL_ERR_TCAM_PARITY &&
		status != CPL_ERR_TCAM_MISS &&
		status != CPL_ERR_TCAM_FULL &&
		status != CPL_ERR_CONN_EXIST_SYNRECV &&
		status != CPL_ERR_CONN_EXIST);
}

/*
 * Convert an ACT_OPEN_RPL status to an errno.
 */
static inline int
act_open_rpl_status_to_errno(int status)
{

	switch (status) {
	case CPL_ERR_CONN_RESET:
		return (ECONNREFUSED);
	case CPL_ERR_ARP_MISS:
		return (EHOSTUNREACH);
	case CPL_ERR_CONN_TIMEDOUT:
		return (ETIMEDOUT);
	case CPL_ERR_TCAM_FULL:
		return (EAGAIN);
	case CPL_ERR_CONN_EXIST:
		return (EAGAIN);
	default:
		return (EIO);
	}
}


enum {
	CPL_CONN_POLICY_AUTO = 0,
	CPL_CONN_POLICY_ASK  = 1,
	CPL_CONN_POLICY_FILTER = 2,
	CPL_CONN_POLICY_DENY = 3
};

enum {
	ULP_MODE_NONE          = 0,
	ULP_MODE_ISCSI         = 2,
	ULP_MODE_RDMA          = 4,
	ULP_MODE_TCPDDP        = 5,
	ULP_MODE_FCOE          = 6,
	ULP_MODE_TLS           = 8,
};

enum {
	ULP_CRC_HEADER = 1 << 0,
	ULP_CRC_DATA   = 1 << 1
};

enum {
	CPL_PASS_OPEN_ACCEPT,
	CPL_PASS_OPEN_REJECT,
	CPL_PASS_OPEN_ACCEPT_TNL
};

enum {
	CPL_ABORT_SEND_RST = 0,
	CPL_ABORT_NO_RST,
};

enum {                     /* TX_PKT_XT checksum types */
	TX_CSUM_TCP    = 0,
	TX_CSUM_UDP    = 1,
	TX_CSUM_CRC16  = 4,
	TX_CSUM_CRC32  = 5,
	TX_CSUM_CRC32C = 6,
	TX_CSUM_FCOE   = 7,
	TX_CSUM_TCPIP  = 8,
	TX_CSUM_UDPIP  = 9,
	TX_CSUM_TCPIP6 = 10,
	TX_CSUM_UDPIP6 = 11,
	TX_CSUM_IP     = 12,
};

enum {                     /* packet type in CPL_RX_PKT */
	PKTYPE_XACT_UCAST = 0,
	PKTYPE_HASH_UCAST = 1,
	PKTYPE_XACT_MCAST = 2,
	PKTYPE_HASH_MCAST = 3,
	PKTYPE_PROMISC    = 4,
	PKTYPE_HPROMISC   = 5,
	PKTYPE_BCAST      = 6
};

enum {                     /* DMAC type in CPL_RX_PKT */
	DATYPE_UCAST,
	DATYPE_MCAST,
	DATYPE_BCAST
};

enum {                     /* TCP congestion control algorithms */
	CONG_ALG_RENO,
	CONG_ALG_TAHOE,
	CONG_ALG_NEWRENO,
	CONG_ALG_HIGHSPEED
};

enum {                     /* RSS hash type */
	RSS_HASH_NONE = 0, /* no hash computed */
	RSS_HASH_IP   = 1, /* IP or IPv6 2-tuple hash */
	RSS_HASH_TCP  = 2, /* TCP 4-tuple hash */
	RSS_HASH_UDP  = 3  /* UDP 4-tuple hash */
};

enum {                     /* LE commands */
	LE_CMD_READ  = 0x4,
	LE_CMD_WRITE = 0xb
};

enum {                     /* LE request size */
	LE_SZ_NONE = 0,
	LE_SZ_33   = 1,
	LE_SZ_66   = 2,
	LE_SZ_132  = 3,
	LE_SZ_264  = 4,
	LE_SZ_528  = 5
};

union opcode_tid {
	__be32 opcode_tid;
	__u8 opcode;
};

#define S_CPL_OPCODE    24
#define V_CPL_OPCODE(x) ((x) << S_CPL_OPCODE)
#define G_CPL_OPCODE(x) (((x) >> S_CPL_OPCODE) & 0xFF)
#define G_TID(x)    ((x) & 0xFFFFFF)

/* tid is assumed to be 24-bits */
#define MK_OPCODE_TID(opcode, tid) (V_CPL_OPCODE(opcode) | (tid))

#define OPCODE_TID(cmd) ((cmd)->ot.opcode_tid)

/* extract the TID from a CPL command */
#define GET_TID(cmd) (G_TID(ntohl(OPCODE_TID(cmd))))
#define GET_OPCODE(cmd) ((cmd)->ot.opcode)

/* partitioning of TID fields that also carry a queue id */
#define S_TID_TID    0
#define M_TID_TID    0x7ff
#define V_TID_TID(x) ((x) << S_TID_TID)
#define G_TID_TID(x) (((x) >> S_TID_TID) & M_TID_TID)

#define S_TID_COOKIE    11
#define M_TID_COOKIE    0x7
#define V_TID_COOKIE(x) ((x) << S_TID_COOKIE)
#define G_TID_COOKIE(x) (((x) >> S_TID_COOKIE) & M_TID_COOKIE)

#define S_TID_QID    14
#define M_TID_QID    0x3ff
#define V_TID_QID(x) ((x) << S_TID_QID)
#define G_TID_QID(x) (((x) >> S_TID_QID) & M_TID_QID)

union opcode_info {
	__be64 opcode_info;
	__u8 opcode;
};

struct tcp_options {
	__be16 mss;
	__u8 wsf;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 :4;
	__u8 unknown:1;
	__u8 ecn:1;
	__u8 sack:1;
	__u8 tstamp:1;
#else
	__u8 tstamp:1;
	__u8 sack:1;
	__u8 ecn:1;
	__u8 unknown:1;
	__u8 :4;
#endif
};

struct rss_header {
	__u8 opcode;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 channel:2;
	__u8 filter_hit:1;
	__u8 filter_tid:1;
	__u8 hash_type:2;
	__u8 ipv6:1;
	__u8 send2fw:1;
#else
	__u8 send2fw:1;
	__u8 ipv6:1;
	__u8 hash_type:2;
	__u8 filter_tid:1;
	__u8 filter_hit:1;
	__u8 channel:2;
#endif
	__be16 qid;
	__be32 hash_val;
};

#define S_HASHTYPE 20
#define M_HASHTYPE 0x3
#define G_HASHTYPE(x) (((x) >> S_HASHTYPE) & M_HASHTYPE)

#define S_QNUM 0
#define M_QNUM 0xFFFF
#define G_QNUM(x) (((x) >> S_QNUM) & M_QNUM)

#if defined(RSS_HDR_VLD) || defined(CHELSIO_FW)
# define RSS_HDR struct rss_header rss_hdr;
#else
# define RSS_HDR
#endif

#ifndef CHELSIO_FW
struct work_request_hdr {
	__be32 wr_hi;
	__be32 wr_mid;
	__be64 wr_lo;
};

/* wr_mid fields */
#define S_WR_LEN16    0
#define M_WR_LEN16    0xFF
#define V_WR_LEN16(x) ((x) << S_WR_LEN16)
#define G_WR_LEN16(x) (((x) >> S_WR_LEN16) & M_WR_LEN16)

/* wr_hi fields */
#define S_WR_OP    24
#define M_WR_OP    0xFF
#define V_WR_OP(x) ((__u64)(x) << S_WR_OP)
#define G_WR_OP(x) (((x) >> S_WR_OP) & M_WR_OP)

# define WR_HDR struct work_request_hdr wr
# define WR_HDR_SIZE sizeof(struct work_request_hdr)
#else
# define WR_HDR
# define WR_HDR_SIZE 0
#endif

/* option 0 fields */
#define S_ACCEPT_MODE    0
#define M_ACCEPT_MODE    0x3
#define V_ACCEPT_MODE(x) ((x) << S_ACCEPT_MODE)
#define G_ACCEPT_MODE(x) (((x) >> S_ACCEPT_MODE) & M_ACCEPT_MODE)

#define S_TX_CHAN    2
#define M_TX_CHAN    0x3
#define V_TX_CHAN(x) ((x) << S_TX_CHAN)
#define G_TX_CHAN(x) (((x) >> S_TX_CHAN) & M_TX_CHAN)

#define S_NO_CONG    4
#define V_NO_CONG(x) ((x) << S_NO_CONG)
#define F_NO_CONG    V_NO_CONG(1U)

#define S_DELACK    5
#define V_DELACK(x) ((x) << S_DELACK)
#define F_DELACK    V_DELACK(1U)

#define S_INJECT_TIMER    6
#define V_INJECT_TIMER(x) ((x) << S_INJECT_TIMER)
#define F_INJECT_TIMER    V_INJECT_TIMER(1U)

#define S_NON_OFFLOAD    7
#define V_NON_OFFLOAD(x) ((x) << S_NON_OFFLOAD)
#define F_NON_OFFLOAD    V_NON_OFFLOAD(1U)

#define S_ULP_MODE    8
#define M_ULP_MODE    0xF
#define V_ULP_MODE(x) ((x) << S_ULP_MODE)
#define G_ULP_MODE(x) (((x) >> S_ULP_MODE) & M_ULP_MODE)

#define S_RCV_BUFSIZ    12
#define M_RCV_BUFSIZ    0x3FFU
#define V_RCV_BUFSIZ(x) ((x) << S_RCV_BUFSIZ)
#define G_RCV_BUFSIZ(x) (((x) >> S_RCV_BUFSIZ) & M_RCV_BUFSIZ)

#define S_DSCP    22
#define M_DSCP    0x3F
#define V_DSCP(x) ((x) << S_DSCP)
#define G_DSCP(x) (((x) >> S_DSCP) & M_DSCP)

#define S_SMAC_SEL    28
#define M_SMAC_SEL    0xFF
#define V_SMAC_SEL(x) ((__u64)(x) << S_SMAC_SEL)
#define G_SMAC_SEL(x) (((x) >> S_SMAC_SEL) & M_SMAC_SEL)

#define S_L2T_IDX    36
#define M_L2T_IDX    0xFFF
#define V_L2T_IDX(x) ((__u64)(x) << S_L2T_IDX)
#define G_L2T_IDX(x) (((x) >> S_L2T_IDX) & M_L2T_IDX)

#define S_TCAM_BYPASS    48
#define V_TCAM_BYPASS(x) ((__u64)(x) << S_TCAM_BYPASS)
#define F_TCAM_BYPASS    V_TCAM_BYPASS(1ULL)

#define S_NAGLE    49
#define V_NAGLE(x) ((__u64)(x) << S_NAGLE)
#define F_NAGLE    V_NAGLE(1ULL)

#define S_WND_SCALE    50
#define M_WND_SCALE    0xF
#define V_WND_SCALE(x) ((__u64)(x) << S_WND_SCALE)
#define G_WND_SCALE(x) (((x) >> S_WND_SCALE) & M_WND_SCALE)

#define S_KEEP_ALIVE    54
#define V_KEEP_ALIVE(x) ((__u64)(x) << S_KEEP_ALIVE)
#define F_KEEP_ALIVE    V_KEEP_ALIVE(1ULL)

#define S_MAX_RT    55
#define M_MAX_RT    0xF
#define V_MAX_RT(x) ((__u64)(x) << S_MAX_RT)
#define G_MAX_RT(x) (((x) >> S_MAX_RT) & M_MAX_RT)

#define S_MAX_RT_OVERRIDE    59
#define V_MAX_RT_OVERRIDE(x) ((__u64)(x) << S_MAX_RT_OVERRIDE)
#define F_MAX_RT_OVERRIDE    V_MAX_RT_OVERRIDE(1ULL)

#define S_MSS_IDX    60
#define M_MSS_IDX    0xF
#define V_MSS_IDX(x) ((__u64)(x) << S_MSS_IDX)
#define G_MSS_IDX(x) (((x) >> S_MSS_IDX) & M_MSS_IDX)

/* option 1 fields */
#define S_SYN_RSS_ENABLE    0
#define V_SYN_RSS_ENABLE(x) ((x) << S_SYN_RSS_ENABLE)
#define F_SYN_RSS_ENABLE    V_SYN_RSS_ENABLE(1U)

#define S_SYN_RSS_USE_HASH    1
#define V_SYN_RSS_USE_HASH(x) ((x) << S_SYN_RSS_USE_HASH)
#define F_SYN_RSS_USE_HASH    V_SYN_RSS_USE_HASH(1U)

#define S_SYN_RSS_QUEUE    2
#define M_SYN_RSS_QUEUE    0x3FF
#define V_SYN_RSS_QUEUE(x) ((x) << S_SYN_RSS_QUEUE)
#define G_SYN_RSS_QUEUE(x) (((x) >> S_SYN_RSS_QUEUE) & M_SYN_RSS_QUEUE)

#define S_LISTEN_INTF    12
#define M_LISTEN_INTF    0xFF
#define V_LISTEN_INTF(x) ((x) << S_LISTEN_INTF)
#define G_LISTEN_INTF(x) (((x) >> S_LISTEN_INTF) & M_LISTEN_INTF)

#define S_LISTEN_FILTER    20
#define V_LISTEN_FILTER(x) ((x) << S_LISTEN_FILTER)
#define F_LISTEN_FILTER    V_LISTEN_FILTER(1U)

#define S_SYN_DEFENSE    21
#define V_SYN_DEFENSE(x) ((x) << S_SYN_DEFENSE)
#define F_SYN_DEFENSE    V_SYN_DEFENSE(1U)

#define S_CONN_POLICY    22
#define M_CONN_POLICY    0x3
#define V_CONN_POLICY(x) ((x) << S_CONN_POLICY)
#define G_CONN_POLICY(x) (((x) >> S_CONN_POLICY) & M_CONN_POLICY)

#define S_T5_FILT_INFO    24
#define M_T5_FILT_INFO    0xffffffffffULL
#define V_T5_FILT_INFO(x) ((x) << S_T5_FILT_INFO)
#define G_T5_FILT_INFO(x) (((x) >> S_T5_FILT_INFO) & M_T5_FILT_INFO)

#define S_FILT_INFO    28
#define M_FILT_INFO    0xfffffffffULL
#define V_FILT_INFO(x) ((x) << S_FILT_INFO)
#define G_FILT_INFO(x) (((x) >> S_FILT_INFO) & M_FILT_INFO)

/* option 2 fields */
#define S_RSS_QUEUE    0
#define M_RSS_QUEUE    0x3FF
#define V_RSS_QUEUE(x) ((x) << S_RSS_QUEUE)
#define G_RSS_QUEUE(x) (((x) >> S_RSS_QUEUE) & M_RSS_QUEUE)

#define S_RSS_QUEUE_VALID    10
#define V_RSS_QUEUE_VALID(x) ((x) << S_RSS_QUEUE_VALID)
#define F_RSS_QUEUE_VALID    V_RSS_QUEUE_VALID(1U)

#define S_RX_COALESCE_VALID    11
#define V_RX_COALESCE_VALID(x) ((x) << S_RX_COALESCE_VALID)
#define F_RX_COALESCE_VALID    V_RX_COALESCE_VALID(1U)

#define S_RX_COALESCE    12
#define M_RX_COALESCE    0x3
#define V_RX_COALESCE(x) ((x) << S_RX_COALESCE)
#define G_RX_COALESCE(x) (((x) >> S_RX_COALESCE) & M_RX_COALESCE)

#define S_CONG_CNTRL    14
#define M_CONG_CNTRL    0x3
#define V_CONG_CNTRL(x) ((x) << S_CONG_CNTRL)
#define G_CONG_CNTRL(x) (((x) >> S_CONG_CNTRL) & M_CONG_CNTRL)

#define S_PACE    16
#define M_PACE    0x3
#define V_PACE(x) ((x) << S_PACE)
#define G_PACE(x) (((x) >> S_PACE) & M_PACE)

#define S_CONG_CNTRL_VALID    18
#define V_CONG_CNTRL_VALID(x) ((x) << S_CONG_CNTRL_VALID)
#define F_CONG_CNTRL_VALID    V_CONG_CNTRL_VALID(1U)

#define S_T5_ISS    18
#define V_T5_ISS(x) ((x) << S_T5_ISS)
#define F_T5_ISS    V_T5_ISS(1U)

#define S_PACE_VALID    19
#define V_PACE_VALID(x) ((x) << S_PACE_VALID)
#define F_PACE_VALID    V_PACE_VALID(1U)

#define S_RX_FC_DISABLE    20
#define V_RX_FC_DISABLE(x) ((x) << S_RX_FC_DISABLE)
#define F_RX_FC_DISABLE    V_RX_FC_DISABLE(1U)

#define S_RX_FC_DDP    21
#define V_RX_FC_DDP(x) ((x) << S_RX_FC_DDP)
#define F_RX_FC_DDP    V_RX_FC_DDP(1U)

#define S_RX_FC_VALID    22
#define V_RX_FC_VALID(x) ((x) << S_RX_FC_VALID)
#define F_RX_FC_VALID    V_RX_FC_VALID(1U)

#define S_TX_QUEUE    23
#define M_TX_QUEUE    0x7
#define V_TX_QUEUE(x) ((x) << S_TX_QUEUE)
#define G_TX_QUEUE(x) (((x) >> S_TX_QUEUE) & M_TX_QUEUE)

#define S_RX_CHANNEL    26
#define V_RX_CHANNEL(x) ((x) << S_RX_CHANNEL)
#define F_RX_CHANNEL    V_RX_CHANNEL(1U)

#define S_CCTRL_ECN    27
#define V_CCTRL_ECN(x) ((x) << S_CCTRL_ECN)
#define F_CCTRL_ECN    V_CCTRL_ECN(1U)

#define S_WND_SCALE_EN    28
#define V_WND_SCALE_EN(x) ((x) << S_WND_SCALE_EN)
#define F_WND_SCALE_EN    V_WND_SCALE_EN(1U)

#define S_TSTAMPS_EN    29
#define V_TSTAMPS_EN(x) ((x) << S_TSTAMPS_EN)
#define F_TSTAMPS_EN    V_TSTAMPS_EN(1U)

#define S_SACK_EN    30
#define V_SACK_EN(x) ((x) << S_SACK_EN)
#define F_SACK_EN    V_SACK_EN(1U)

#define S_T5_OPT_2_VALID    31
#define V_T5_OPT_2_VALID(x) ((x) << S_T5_OPT_2_VALID)
#define F_T5_OPT_2_VALID    V_T5_OPT_2_VALID(1U)

struct cpl_pass_open_req {
	WR_HDR;
	union opcode_tid ot;
	__be16 local_port;
	__be16 peer_port;
	__be32 local_ip;
	__be32 peer_ip;
	__be64 opt0;
	__be64 opt1;
};

struct cpl_pass_open_req6 {
	WR_HDR;
	union opcode_tid ot;
	__be16 local_port;
	__be16 peer_port;
	__be64 local_ip_hi;
	__be64 local_ip_lo;
	__be64 peer_ip_hi;
	__be64 peer_ip_lo;
	__be64 opt0;
	__be64 opt1;
};

struct cpl_pass_open_rpl {
	RSS_HDR
	union opcode_tid ot;
	__u8 rsvd[3];
	__u8 status;
};

struct cpl_pass_establish {
	RSS_HDR
	union opcode_tid ot;
	__be32 rsvd;
	__be32 tos_stid;
	__be16 mac_idx;
	__be16 tcp_opt;
	__be32 snd_isn;
	__be32 rcv_isn;
};

/* cpl_pass_establish.tos_stid fields */
#define S_PASS_OPEN_TID    0
#define M_PASS_OPEN_TID    0xFFFFFF
#define V_PASS_OPEN_TID(x) ((x) << S_PASS_OPEN_TID)
#define G_PASS_OPEN_TID(x) (((x) >> S_PASS_OPEN_TID) & M_PASS_OPEN_TID)

#define S_PASS_OPEN_TOS    24
#define M_PASS_OPEN_TOS    0xFF
#define V_PASS_OPEN_TOS(x) ((x) << S_PASS_OPEN_TOS)
#define G_PASS_OPEN_TOS(x) (((x) >> S_PASS_OPEN_TOS) & M_PASS_OPEN_TOS)

/* cpl_pass_establish.tcp_opt fields (also applies to act_open_establish) */
#define S_TCPOPT_WSCALE_OK	5
#define M_TCPOPT_WSCALE_OK  	0x1
#define V_TCPOPT_WSCALE_OK(x)	((x) << S_TCPOPT_WSCALE_OK)
#define G_TCPOPT_WSCALE_OK(x)	(((x) >> S_TCPOPT_WSCALE_OK) & M_TCPOPT_WSCALE_OK)

#define S_TCPOPT_SACK		6
#define M_TCPOPT_SACK		0x1
#define V_TCPOPT_SACK(x)	((x) << S_TCPOPT_SACK)
#define G_TCPOPT_SACK(x)	(((x) >> S_TCPOPT_SACK) & M_TCPOPT_SACK)

#define S_TCPOPT_TSTAMP		7
#define M_TCPOPT_TSTAMP		0x1
#define V_TCPOPT_TSTAMP(x)	((x) << S_TCPOPT_TSTAMP)
#define G_TCPOPT_TSTAMP(x)	(((x) >> S_TCPOPT_TSTAMP) & M_TCPOPT_TSTAMP)

#define S_TCPOPT_SND_WSCALE	8
#define M_TCPOPT_SND_WSCALE	0xF
#define V_TCPOPT_SND_WSCALE(x)	((x) << S_TCPOPT_SND_WSCALE)
#define G_TCPOPT_SND_WSCALE(x)	(((x) >> S_TCPOPT_SND_WSCALE) & M_TCPOPT_SND_WSCALE)

#define S_TCPOPT_MSS	12
#define M_TCPOPT_MSS	0xF
#define V_TCPOPT_MSS(x)	((x) << S_TCPOPT_MSS)
#define G_TCPOPT_MSS(x)	(((x) >> S_TCPOPT_MSS) & M_TCPOPT_MSS)

struct cpl_pass_accept_req {
	RSS_HDR
	union opcode_tid ot;
	__be16 rsvd;
	__be16 len;
	__be32 hdr_len;
	__be16 vlan;
	__be16 l2info;
	__be32 tos_stid;
	struct tcp_options tcpopt;
};

/* cpl_pass_accept_req.hdr_len fields */
#define S_SYN_RX_CHAN    0
#define M_SYN_RX_CHAN    0xF
#define V_SYN_RX_CHAN(x) ((x) << S_SYN_RX_CHAN)
#define G_SYN_RX_CHAN(x) (((x) >> S_SYN_RX_CHAN) & M_SYN_RX_CHAN)

#define S_TCP_HDR_LEN    10
#define M_TCP_HDR_LEN    0x3F
#define V_TCP_HDR_LEN(x) ((x) << S_TCP_HDR_LEN)
#define G_TCP_HDR_LEN(x) (((x) >> S_TCP_HDR_LEN) & M_TCP_HDR_LEN)

#define S_T6_TCP_HDR_LEN   8
#define V_T6_TCP_HDR_LEN(x) ((x) << S_T6_TCP_HDR_LEN)
#define G_T6_TCP_HDR_LEN(x) (((x) >> S_T6_TCP_HDR_LEN) & M_TCP_HDR_LEN)

#define S_IP_HDR_LEN    16
#define M_IP_HDR_LEN    0x3FF
#define V_IP_HDR_LEN(x) ((x) << S_IP_HDR_LEN)
#define G_IP_HDR_LEN(x) (((x) >> S_IP_HDR_LEN) & M_IP_HDR_LEN)

#define S_T6_IP_HDR_LEN    14
#define V_T6_IP_HDR_LEN(x) ((x) << S_T6_IP_HDR_LEN)
#define G_T6_IP_HDR_LEN(x) (((x) >> S_T6_IP_HDR_LEN) & M_IP_HDR_LEN)

#define S_ETH_HDR_LEN    26
#define M_ETH_HDR_LEN    0x3F
#define V_ETH_HDR_LEN(x) ((x) << S_ETH_HDR_LEN)
#define G_ETH_HDR_LEN(x) (((x) >> S_ETH_HDR_LEN) & M_ETH_HDR_LEN)

#define S_T6_ETH_HDR_LEN    24
#define M_T6_ETH_HDR_LEN    0xFF
#define V_T6_ETH_HDR_LEN(x) ((x) << S_T6_ETH_HDR_LEN)
#define G_T6_ETH_HDR_LEN(x) (((x) >> S_T6_ETH_HDR_LEN) & M_T6_ETH_HDR_LEN)

/* cpl_pass_accept_req.l2info fields */
#define S_SYN_MAC_IDX    0
#define M_SYN_MAC_IDX    0x1FF
#define V_SYN_MAC_IDX(x) ((x) << S_SYN_MAC_IDX)
#define G_SYN_MAC_IDX(x) (((x) >> S_SYN_MAC_IDX) & M_SYN_MAC_IDX)

#define S_SYN_XACT_MATCH    9
#define V_SYN_XACT_MATCH(x) ((x) << S_SYN_XACT_MATCH)
#define F_SYN_XACT_MATCH    V_SYN_XACT_MATCH(1U)

#define S_SYN_INTF    12
#define M_SYN_INTF    0xF
#define V_SYN_INTF(x) ((x) << S_SYN_INTF)
#define G_SYN_INTF(x) (((x) >> S_SYN_INTF) & M_SYN_INTF)

struct cpl_pass_accept_rpl {
	WR_HDR;
	union opcode_tid ot;
	__be32 opt2;
	__be64 opt0;
};

struct cpl_t5_pass_accept_rpl {
	WR_HDR;
	union opcode_tid ot;
	__be32 opt2;
	__be64 opt0;
	__be32 iss;
	union {
		__be32 rsvd; /* T5 */
		__be32 opt3; /* T6 */
	} u;
};

struct cpl_act_open_req {
	WR_HDR;
	union opcode_tid ot;
	__be16 local_port;
	__be16 peer_port;
	__be32 local_ip;
	__be32 peer_ip;
	__be64 opt0;
	__be32 params;
	__be32 opt2;
};

#define S_FILTER_TUPLE	24
#define M_FILTER_TUPLE	0xFFFFFFFFFF
#define V_FILTER_TUPLE(x) ((x) << S_FILTER_TUPLE)
#define G_FILTER_TUPLE(x) (((x) >> S_FILTER_TUPLE) & M_FILTER_TUPLE)
struct cpl_t5_act_open_req {
	WR_HDR;
	union opcode_tid ot;
	__be16 local_port;
	__be16 peer_port;
	__be32 local_ip;
	__be32 peer_ip;
	__be64 opt0;
	__be32 iss;
	__be32 opt2;
	__be64 params;
};

struct cpl_t6_act_open_req {
	WR_HDR;
	union opcode_tid ot;
	__be16 local_port;
	__be16 peer_port;
	__be32 local_ip;
	__be32 peer_ip;
	__be64 opt0;
	__be32 iss;
	__be32 opt2;
	__be64 params;
	__be32 rsvd2;
	__be32 opt3;
};

/* cpl_{t5,t6}_act_open_req.params field */
#define S_AOPEN_FCOEMASK	0
#define V_AOPEN_FCOEMASK(x)	((x) << S_AOPEN_FCOEMASK)
#define F_AOPEN_FCOEMASK	V_AOPEN_FCOEMASK(1U)

struct cpl_act_open_req6 {
	WR_HDR;
	union opcode_tid ot;
	__be16 local_port;
	__be16 peer_port;
	__be64 local_ip_hi;
	__be64 local_ip_lo;
	__be64 peer_ip_hi;
	__be64 peer_ip_lo;
	__be64 opt0;
	__be32 params;
	__be32 opt2;
};

struct cpl_t5_act_open_req6 {
	WR_HDR;
	union opcode_tid ot;
	__be16 local_port;
	__be16 peer_port;
	__be64 local_ip_hi;
	__be64 local_ip_lo;
	__be64 peer_ip_hi;
	__be64 peer_ip_lo;
	__be64 opt0;
	__be32 iss;
	__be32 opt2;
	__be64 params;
};

struct cpl_t6_act_open_req6 {
	WR_HDR;
	union opcode_tid ot;
	__be16 local_port;
	__be16 peer_port;
	__be64 local_ip_hi;
	__be64 local_ip_lo;
	__be64 peer_ip_hi;
	__be64 peer_ip_lo;
	__be64 opt0;
	__be32 iss;
	__be32 opt2;
	__be64 params;
	__be32 rsvd2;
	__be32 opt3;
};

struct cpl_act_open_rpl {
	RSS_HDR
	union opcode_tid ot;
	__be32 atid_status;
};

/* cpl_act_open_rpl.atid_status fields */
#define S_AOPEN_STATUS    0
#define M_AOPEN_STATUS    0xFF
#define V_AOPEN_STATUS(x) ((x) << S_AOPEN_STATUS)
#define G_AOPEN_STATUS(x) (((x) >> S_AOPEN_STATUS) & M_AOPEN_STATUS)

#define S_AOPEN_ATID    8
#define M_AOPEN_ATID    0xFFFFFF
#define V_AOPEN_ATID(x) ((x) << S_AOPEN_ATID)
#define G_AOPEN_ATID(x) (((x) >> S_AOPEN_ATID) & M_AOPEN_ATID)

struct cpl_act_establish {
	RSS_HDR
	union opcode_tid ot;
	__be32 rsvd;
	__be32 tos_atid;
	__be16 mac_idx;
	__be16 tcp_opt;
	__be32 snd_isn;
	__be32 rcv_isn;
};

struct cpl_get_tcb {
	WR_HDR;
	union opcode_tid ot;
	__be16 reply_ctrl;
	__be16 cookie;
};

/* cpl_get_tcb.reply_ctrl fields */
#define S_QUEUENO    0
#define M_QUEUENO    0x3FF
#define V_QUEUENO(x) ((x) << S_QUEUENO)
#define G_QUEUENO(x) (((x) >> S_QUEUENO) & M_QUEUENO)

#define S_REPLY_CHAN    14
#define V_REPLY_CHAN(x) ((x) << S_REPLY_CHAN)
#define F_REPLY_CHAN    V_REPLY_CHAN(1U)

#define S_NO_REPLY    15
#define V_NO_REPLY(x) ((x) << S_NO_REPLY)
#define F_NO_REPLY    V_NO_REPLY(1U)

struct cpl_get_tcb_rpl {
	RSS_HDR
	union opcode_tid ot;
	__u8 cookie;
	__u8 status;
	__be16 len;
};

struct cpl_set_tcb {
	WR_HDR;
	union opcode_tid ot;
	__be16 reply_ctrl;
	__be16 cookie;
};

struct cpl_set_tcb_field {
	WR_HDR;
	union opcode_tid ot;
	__be16 reply_ctrl;
	__be16 word_cookie;
	__be64 mask;
	__be64 val;
};

struct cpl_set_tcb_field_core {
	union opcode_tid ot;
	__be16 reply_ctrl;
	__be16 word_cookie;
	__be64 mask;
	__be64 val;
};

/* cpl_set_tcb_field.word_cookie fields */
#define S_WORD    0
#define M_WORD    0x1F
#define V_WORD(x) ((x) << S_WORD)
#define G_WORD(x) (((x) >> S_WORD) & M_WORD)

#define S_COOKIE    5
#define M_COOKIE    0x7
#define V_COOKIE(x) ((x) << S_COOKIE)
#define G_COOKIE(x) (((x) >> S_COOKIE) & M_COOKIE)

struct cpl_set_tcb_rpl {
	RSS_HDR
	union opcode_tid ot;
	__be16 rsvd;
	__u8   cookie;
	__u8   status;
	__be64 oldval;
};

struct cpl_close_con_req {
	WR_HDR;
	union opcode_tid ot;
	__be32 rsvd;
};

struct cpl_close_con_rpl {
	RSS_HDR
	union opcode_tid ot;
	__u8  rsvd[3];
	__u8  status;
	__be32 snd_nxt;
	__be32 rcv_nxt;
};

struct cpl_close_listsvr_req {
	WR_HDR;
	union opcode_tid ot;
	__be16 reply_ctrl;
	__be16 rsvd;
};

/* additional cpl_close_listsvr_req.reply_ctrl field */
#define S_LISTSVR_IPV6    14
#define V_LISTSVR_IPV6(x) ((x) << S_LISTSVR_IPV6)
#define F_LISTSVR_IPV6    V_LISTSVR_IPV6(1U)

struct cpl_close_listsvr_rpl {
	RSS_HDR
	union opcode_tid ot;
	__u8 rsvd[3];
	__u8 status;
};

struct cpl_abort_req_rss {
	RSS_HDR
	union opcode_tid ot;
	__u8  rsvd[3];
	__u8  status;
};

struct cpl_abort_req_rss6 {
	RSS_HDR
	union opcode_tid ot;
	__u32 srqidx_status;
};

#define S_ABORT_RSS_STATUS    0
#define M_ABORT_RSS_STATUS    0xff
#define V_ABORT_RSS_STATUS(x) ((x) << S_ABORT_RSS_STATUS)
#define G_ABORT_RSS_STATUS(x) (((x) >> S_ABORT_RSS_STATUS) & M_ABORT_RSS_STATUS)

#define S_ABORT_RSS_SRQIDX    8
#define M_ABORT_RSS_SRQIDX    0xffffff
#define V_ABORT_RSS_SRQIDX(x) ((x) << S_ABORT_RSS_SRQIDX)
#define G_ABORT_RSS_SRQIDX(x) (((x) >> S_ABORT_RSS_SRQIDX) & M_ABORT_RSS_SRQIDX)


/* cpl_abort_req status command code in case of T6,
 * bit[0] specifies whether to send RST (0) to remote peer or suppress it (1)
 * bit[1] indicates ABORT_REQ was sent after a CLOSE_CON_REQ
 * bit[2] specifies whether to disable the mmgr (1) or not (0)
 */
struct cpl_abort_req {
	WR_HDR;
	union opcode_tid ot;
	__be32 rsvd0;
	__u8  rsvd1;
	__u8  cmd;
	__u8  rsvd2[6];
};

struct cpl_abort_req_core {
	union opcode_tid ot;
	__be32 rsvd0;
	__u8  rsvd1;
	__u8  cmd;
	__u8  rsvd2[6];
};

struct cpl_abort_rpl_rss {
	RSS_HDR
	union opcode_tid ot;
	__u8  rsvd[3];
	__u8  status;
};

struct cpl_abort_rpl_rss6 {
	RSS_HDR
	union opcode_tid ot;
	__u32 srqidx_status;
};

struct cpl_abort_rpl {
	WR_HDR;
	union opcode_tid ot;
	__be32 rsvd0;
	__u8  rsvd1;
	__u8  cmd;
	__u8  rsvd2[6];
};

struct cpl_abort_rpl_core {
	union opcode_tid ot;
	__be32 rsvd0;
	__u8  rsvd1;
	__u8  cmd;
	__u8  rsvd2[6];
};

struct cpl_peer_close {
	RSS_HDR
	union opcode_tid ot;
	__be32 rcv_nxt;
};

struct cpl_tid_release {
	WR_HDR;
	union opcode_tid ot;
	__be32 rsvd;
};

struct tx_data_wr {
	__be32 wr_hi;
	__be32 wr_lo;
	__be32 len;
	__be32 flags;
	__be32 sndseq;
	__be32 param;
};

/* tx_data_wr.flags fields */
#define S_TX_ACK_PAGES    21
#define M_TX_ACK_PAGES    0x7
#define V_TX_ACK_PAGES(x) ((x) << S_TX_ACK_PAGES)
#define G_TX_ACK_PAGES(x) (((x) >> S_TX_ACK_PAGES) & M_TX_ACK_PAGES)

/* tx_data_wr.param fields */
#define S_TX_PORT    0
#define M_TX_PORT    0x7
#define V_TX_PORT(x) ((x) << S_TX_PORT)
#define G_TX_PORT(x) (((x) >> S_TX_PORT) & M_TX_PORT)

#define S_TX_MSS    4
#define M_TX_MSS    0xF
#define V_TX_MSS(x) ((x) << S_TX_MSS)
#define G_TX_MSS(x) (((x) >> S_TX_MSS) & M_TX_MSS)

#define S_TX_QOS    8
#define M_TX_QOS    0xFF
#define V_TX_QOS(x) ((x) << S_TX_QOS)
#define G_TX_QOS(x) (((x) >> S_TX_QOS) & M_TX_QOS)

#define S_TX_SNDBUF 16
#define M_TX_SNDBUF 0xFFFF
#define V_TX_SNDBUF(x) ((x) << S_TX_SNDBUF)
#define G_TX_SNDBUF(x) (((x) >> S_TX_SNDBUF) & M_TX_SNDBUF)

struct cpl_tx_data {
	union opcode_tid ot;
	__be32 len;
	__be32 rsvd;
	__be32 flags;
};

/* cpl_tx_data.flags fields */
#define S_TX_PROXY    5
#define V_TX_PROXY(x) ((x) << S_TX_PROXY)
#define F_TX_PROXY    V_TX_PROXY(1U)

#define S_TX_ULP_SUBMODE    6
#define M_TX_ULP_SUBMODE    0xF
#define V_TX_ULP_SUBMODE(x) ((x) << S_TX_ULP_SUBMODE)
#define G_TX_ULP_SUBMODE(x) (((x) >> S_TX_ULP_SUBMODE) & M_TX_ULP_SUBMODE)

#define S_TX_ULP_MODE    10
#define M_TX_ULP_MODE    0x7
#define V_TX_ULP_MODE(x) ((x) << S_TX_ULP_MODE)
#define G_TX_ULP_MODE(x) (((x) >> S_TX_ULP_MODE) & M_TX_ULP_MODE)

#define S_TX_FORCE    13
#define V_TX_FORCE(x) ((x) << S_TX_FORCE)
#define F_TX_FORCE    V_TX_FORCE(1U)

#define S_TX_SHOVE    14
#define V_TX_SHOVE(x) ((x) << S_TX_SHOVE)
#define F_TX_SHOVE    V_TX_SHOVE(1U)

#define S_TX_MORE    15
#define V_TX_MORE(x) ((x) << S_TX_MORE)
#define F_TX_MORE    V_TX_MORE(1U)

#define S_TX_URG    16
#define V_TX_URG(x) ((x) << S_TX_URG)
#define F_TX_URG    V_TX_URG(1U)

#define S_TX_FLUSH    17
#define V_TX_FLUSH(x) ((x) << S_TX_FLUSH)
#define F_TX_FLUSH    V_TX_FLUSH(1U)

#define S_TX_SAVE    18
#define V_TX_SAVE(x) ((x) << S_TX_SAVE)
#define F_TX_SAVE    V_TX_SAVE(1U)

#define S_TX_TNL    19
#define V_TX_TNL(x) ((x) << S_TX_TNL)
#define F_TX_TNL    V_TX_TNL(1U)

#define S_T6_TX_FORCE    20
#define V_T6_TX_FORCE(x) ((x) << S_T6_TX_FORCE)
#define F_T6_TX_FORCE    V_T6_TX_FORCE(1U)

/* additional tx_data_wr.flags fields */
#define S_TX_CPU_IDX    0
#define M_TX_CPU_IDX    0x3F
#define V_TX_CPU_IDX(x) ((x) << S_TX_CPU_IDX)
#define G_TX_CPU_IDX(x) (((x) >> S_TX_CPU_IDX) & M_TX_CPU_IDX)

#define S_TX_CLOSE    17
#define V_TX_CLOSE(x) ((x) << S_TX_CLOSE)
#define F_TX_CLOSE    V_TX_CLOSE(1U)

#define S_TX_INIT    18
#define V_TX_INIT(x) ((x) << S_TX_INIT)
#define F_TX_INIT    V_TX_INIT(1U)

#define S_TX_IMM_ACK    19
#define V_TX_IMM_ACK(x) ((x) << S_TX_IMM_ACK)
#define F_TX_IMM_ACK    V_TX_IMM_ACK(1U)

#define S_TX_IMM_DMA    20
#define V_TX_IMM_DMA(x) ((x) << S_TX_IMM_DMA)
#define F_TX_IMM_DMA    V_TX_IMM_DMA(1U)

struct cpl_tx_data_ack {
	RSS_HDR
	union opcode_tid ot;
	__be32 snd_una;
};

struct cpl_wr_ack {  /* XXX */
	RSS_HDR
	union opcode_tid ot;
	__be16 credits;
	__be16 rsvd;
	__be32 snd_nxt;
	__be32 snd_una;
};

struct cpl_tx_pkt_core {
	__be32 ctrl0;
	__be16 pack;
	__be16 len;
	__be64 ctrl1;
};

struct cpl_tx_pkt {
	WR_HDR;
	struct cpl_tx_pkt_core c;
};

#define cpl_tx_pkt_xt cpl_tx_pkt

/* cpl_tx_pkt_core.ctrl0 fields */
#define S_TXPKT_VF    0
#define M_TXPKT_VF    0xFF
#define V_TXPKT_VF(x) ((x) << S_TXPKT_VF)
#define G_TXPKT_VF(x) (((x) >> S_TXPKT_VF) & M_TXPKT_VF)

#define S_TXPKT_PF    8
#define M_TXPKT_PF    0x7
#define V_TXPKT_PF(x) ((x) << S_TXPKT_PF)
#define G_TXPKT_PF(x) (((x) >> S_TXPKT_PF) & M_TXPKT_PF)

#define S_TXPKT_VF_VLD    11
#define V_TXPKT_VF_VLD(x) ((x) << S_TXPKT_VF_VLD)
#define F_TXPKT_VF_VLD    V_TXPKT_VF_VLD(1U)

#define S_TXPKT_OVLAN_IDX    12
#define M_TXPKT_OVLAN_IDX    0xF
#define V_TXPKT_OVLAN_IDX(x) ((x) << S_TXPKT_OVLAN_IDX)
#define G_TXPKT_OVLAN_IDX(x) (((x) >> S_TXPKT_OVLAN_IDX) & M_TXPKT_OVLAN_IDX)

#define S_TXPKT_T5_OVLAN_IDX    12
#define M_TXPKT_T5_OVLAN_IDX    0x7
#define V_TXPKT_T5_OVLAN_IDX(x) ((x) << S_TXPKT_T5_OVLAN_IDX)
#define G_TXPKT_T5_OVLAN_IDX(x) (((x) >> S_TXPKT_T5_OVLAN_IDX) & \
				M_TXPKT_T5_OVLAN_IDX)

#define S_TXPKT_INTF    16
#define M_TXPKT_INTF    0xF
#define V_TXPKT_INTF(x) ((x) << S_TXPKT_INTF)
#define G_TXPKT_INTF(x) (((x) >> S_TXPKT_INTF) & M_TXPKT_INTF)

#define S_TXPKT_SPECIAL_STAT    20
#define V_TXPKT_SPECIAL_STAT(x) ((x) << S_TXPKT_SPECIAL_STAT)
#define F_TXPKT_SPECIAL_STAT    V_TXPKT_SPECIAL_STAT(1U)

#define S_TXPKT_T5_FCS_DIS    21
#define V_TXPKT_T5_FCS_DIS(x) ((x) << S_TXPKT_T5_FCS_DIS)
#define F_TXPKT_T5_FCS_DIS    V_TXPKT_T5_FCS_DIS(1U)

#define S_TXPKT_INS_OVLAN    21
#define V_TXPKT_INS_OVLAN(x) ((x) << S_TXPKT_INS_OVLAN)
#define F_TXPKT_INS_OVLAN    V_TXPKT_INS_OVLAN(1U)

#define S_TXPKT_T5_INS_OVLAN    15
#define V_TXPKT_T5_INS_OVLAN(x) ((x) << S_TXPKT_T5_INS_OVLAN)
#define F_TXPKT_T5_INS_OVLAN    V_TXPKT_T5_INS_OVLAN(1U)

#define S_TXPKT_STAT_DIS    22
#define V_TXPKT_STAT_DIS(x) ((x) << S_TXPKT_STAT_DIS)
#define F_TXPKT_STAT_DIS    V_TXPKT_STAT_DIS(1U)

#define S_TXPKT_LOOPBACK    23
#define V_TXPKT_LOOPBACK(x) ((x) << S_TXPKT_LOOPBACK)
#define F_TXPKT_LOOPBACK    V_TXPKT_LOOPBACK(1U)

#define S_TXPKT_TSTAMP    23
#define V_TXPKT_TSTAMP(x) ((x) << S_TXPKT_TSTAMP)
#define F_TXPKT_TSTAMP    V_TXPKT_TSTAMP(1U)

#define S_TXPKT_OPCODE    24
#define M_TXPKT_OPCODE    0xFF
#define V_TXPKT_OPCODE(x) ((x) << S_TXPKT_OPCODE)
#define G_TXPKT_OPCODE(x) (((x) >> S_TXPKT_OPCODE) & M_TXPKT_OPCODE)

/* cpl_tx_pkt_core.ctrl1 fields */
#define S_TXPKT_SA_IDX    0
#define M_TXPKT_SA_IDX    0xFFF
#define V_TXPKT_SA_IDX(x) ((x) << S_TXPKT_SA_IDX)
#define G_TXPKT_SA_IDX(x) (((x) >> S_TXPKT_SA_IDX) & M_TXPKT_SA_IDX)

#define S_TXPKT_CSUM_END    12
#define M_TXPKT_CSUM_END    0xFF
#define V_TXPKT_CSUM_END(x) ((x) << S_TXPKT_CSUM_END)
#define G_TXPKT_CSUM_END(x) (((x) >> S_TXPKT_CSUM_END) & M_TXPKT_CSUM_END)

#define S_TXPKT_CSUM_START    20
#define M_TXPKT_CSUM_START    0x3FF
#define V_TXPKT_CSUM_START(x) ((x) << S_TXPKT_CSUM_START)
#define G_TXPKT_CSUM_START(x) (((x) >> S_TXPKT_CSUM_START) & M_TXPKT_CSUM_START)

#define S_TXPKT_IPHDR_LEN    20
#define M_TXPKT_IPHDR_LEN    0x3FFF
#define V_TXPKT_IPHDR_LEN(x) ((__u64)(x) << S_TXPKT_IPHDR_LEN)
#define G_TXPKT_IPHDR_LEN(x) (((x) >> S_TXPKT_IPHDR_LEN) & M_TXPKT_IPHDR_LEN)

#define M_T6_TXPKT_IPHDR_LEN    0xFFF
#define G_T6_TXPKT_IPHDR_LEN(x) \
	(((x) >> S_TXPKT_IPHDR_LEN) & M_T6_TXPKT_IPHDR_LEN)

#define S_TXPKT_CSUM_LOC    30
#define M_TXPKT_CSUM_LOC    0x3FF
#define V_TXPKT_CSUM_LOC(x) ((__u64)(x) << S_TXPKT_CSUM_LOC)
#define G_TXPKT_CSUM_LOC(x) (((x) >> S_TXPKT_CSUM_LOC) & M_TXPKT_CSUM_LOC)

#define S_TXPKT_ETHHDR_LEN    34
#define M_TXPKT_ETHHDR_LEN    0x3F
#define V_TXPKT_ETHHDR_LEN(x) ((__u64)(x) << S_TXPKT_ETHHDR_LEN)
#define G_TXPKT_ETHHDR_LEN(x) (((x) >> S_TXPKT_ETHHDR_LEN) & M_TXPKT_ETHHDR_LEN)

#define S_T6_TXPKT_ETHHDR_LEN    32
#define M_T6_TXPKT_ETHHDR_LEN    0xFF
#define V_T6_TXPKT_ETHHDR_LEN(x) ((__u64)(x) << S_T6_TXPKT_ETHHDR_LEN)
#define G_T6_TXPKT_ETHHDR_LEN(x) \
	(((x) >> S_T6_TXPKT_ETHHDR_LEN) & M_T6_TXPKT_ETHHDR_LEN)

#define S_TXPKT_CSUM_TYPE    40
#define M_TXPKT_CSUM_TYPE    0xF
#define V_TXPKT_CSUM_TYPE(x) ((__u64)(x) << S_TXPKT_CSUM_TYPE)
#define G_TXPKT_CSUM_TYPE(x) (((x) >> S_TXPKT_CSUM_TYPE) & M_TXPKT_CSUM_TYPE)

#define S_TXPKT_VLAN    44
#define M_TXPKT_VLAN    0xFFFF
#define V_TXPKT_VLAN(x) ((__u64)(x) << S_TXPKT_VLAN)
#define G_TXPKT_VLAN(x) (((x) >> S_TXPKT_VLAN) & M_TXPKT_VLAN)

#define S_TXPKT_VLAN_VLD    60
#define V_TXPKT_VLAN_VLD(x) ((__u64)(x) << S_TXPKT_VLAN_VLD)
#define F_TXPKT_VLAN_VLD    V_TXPKT_VLAN_VLD(1ULL)

#define S_TXPKT_IPSEC    61
#define V_TXPKT_IPSEC(x) ((__u64)(x) << S_TXPKT_IPSEC)
#define F_TXPKT_IPSEC    V_TXPKT_IPSEC(1ULL)

#define S_TXPKT_IPCSUM_DIS    62
#define V_TXPKT_IPCSUM_DIS(x) ((__u64)(x) << S_TXPKT_IPCSUM_DIS)
#define F_TXPKT_IPCSUM_DIS    V_TXPKT_IPCSUM_DIS(1ULL)

#define S_TXPKT_L4CSUM_DIS    63
#define V_TXPKT_L4CSUM_DIS(x) ((__u64)(x) << S_TXPKT_L4CSUM_DIS)
#define F_TXPKT_L4CSUM_DIS    V_TXPKT_L4CSUM_DIS(1ULL)

struct cpl_tx_pkt_lso_core {
	__be32 lso_ctrl;
	__be16 ipid_ofst;
	__be16 mss;
	__be32 seqno_offset;
	__be32 len;
	/* encapsulated CPL (TX_PKT, TX_PKT_XT or TX_DATA) follows here */
};

struct cpl_tx_pkt_lso {
	WR_HDR;
	struct cpl_tx_pkt_lso_core c;
	/* encapsulated CPL (TX_PKT, TX_PKT_XT or TX_DATA) follows here */
};

struct cpl_tx_pkt_ufo_core {
	__be16 ethlen;
	__be16 iplen;
	__be16 udplen;
	__be16 mss;
	__be32 len;
	__be32 r1;
	/* encapsulated CPL (TX_PKT, TX_PKT_XT or TX_DATA) follows here */
};

struct cpl_tx_pkt_ufo {
	WR_HDR;
	struct cpl_tx_pkt_ufo_core c;
	/* encapsulated CPL (TX_PKT, TX_PKT_XT or TX_DATA) follows here */
};

/* cpl_tx_pkt_lso_core.lso_ctrl fields */
#define S_LSO_TCPHDR_LEN    0
#define M_LSO_TCPHDR_LEN    0xF
#define V_LSO_TCPHDR_LEN(x) ((x) << S_LSO_TCPHDR_LEN)
#define G_LSO_TCPHDR_LEN(x) (((x) >> S_LSO_TCPHDR_LEN) & M_LSO_TCPHDR_LEN)

#define S_LSO_IPHDR_LEN    4
#define M_LSO_IPHDR_LEN    0xFFF
#define V_LSO_IPHDR_LEN(x) ((x) << S_LSO_IPHDR_LEN)
#define G_LSO_IPHDR_LEN(x) (((x) >> S_LSO_IPHDR_LEN) & M_LSO_IPHDR_LEN)

#define S_LSO_ETHHDR_LEN    16
#define M_LSO_ETHHDR_LEN    0xF
#define V_LSO_ETHHDR_LEN(x) ((x) << S_LSO_ETHHDR_LEN)
#define G_LSO_ETHHDR_LEN(x) (((x) >> S_LSO_ETHHDR_LEN) & M_LSO_ETHHDR_LEN)

#define S_LSO_IPV6    20
#define V_LSO_IPV6(x) ((x) << S_LSO_IPV6)
#define F_LSO_IPV6    V_LSO_IPV6(1U)

#define S_LSO_OFLD_ENCAP    21
#define V_LSO_OFLD_ENCAP(x) ((x) << S_LSO_OFLD_ENCAP)
#define F_LSO_OFLD_ENCAP    V_LSO_OFLD_ENCAP(1U)

#define S_LSO_LAST_SLICE    22
#define V_LSO_LAST_SLICE(x) ((x) << S_LSO_LAST_SLICE)
#define F_LSO_LAST_SLICE    V_LSO_LAST_SLICE(1U)

#define S_LSO_FIRST_SLICE    23
#define V_LSO_FIRST_SLICE(x) ((x) << S_LSO_FIRST_SLICE)
#define F_LSO_FIRST_SLICE    V_LSO_FIRST_SLICE(1U)

#define S_LSO_OPCODE    24
#define M_LSO_OPCODE    0xFF
#define V_LSO_OPCODE(x) ((x) << S_LSO_OPCODE)
#define G_LSO_OPCODE(x) (((x) >> S_LSO_OPCODE) & M_LSO_OPCODE)

#define S_LSO_T5_XFER_SIZE	   0
#define M_LSO_T5_XFER_SIZE    0xFFFFFFF
#define V_LSO_T5_XFER_SIZE(x) ((x) << S_LSO_T5_XFER_SIZE)
#define G_LSO_T5_XFER_SIZE(x) (((x) >> S_LSO_T5_XFER_SIZE) & M_LSO_T5_XFER_SIZE)

/* cpl_tx_pkt_lso_core.mss fields */
#define S_LSO_MSS    0
#define M_LSO_MSS    0x3FFF
#define V_LSO_MSS(x) ((x) << S_LSO_MSS)
#define G_LSO_MSS(x) (((x) >> S_LSO_MSS) & M_LSO_MSS)

#define S_LSO_IPID_SPLIT    15
#define V_LSO_IPID_SPLIT(x) ((x) << S_LSO_IPID_SPLIT)
#define F_LSO_IPID_SPLIT    V_LSO_IPID_SPLIT(1U)

struct cpl_tx_pkt_fso {
	WR_HDR;
	__be32 fso_ctrl;
	__be16 seqcnt_ofst;
	__be16 mtu;
	__be32 param_offset;
	__be32 len;
	/* encapsulated CPL (TX_PKT or TX_PKT_XT) follows here */
};

/* cpl_tx_pkt_fso.fso_ctrl fields different from cpl_tx_pkt_lso.lso_ctrl */
#define S_FSO_XCHG_CLASS    21
#define V_FSO_XCHG_CLASS(x) ((x) << S_FSO_XCHG_CLASS)
#define F_FSO_XCHG_CLASS    V_FSO_XCHG_CLASS(1U)

#define S_FSO_INITIATOR    20
#define V_FSO_INITIATOR(x) ((x) << S_FSO_INITIATOR)
#define F_FSO_INITIATOR    V_FSO_INITIATOR(1U)

#define S_FSO_FCHDR_LEN    12
#define M_FSO_FCHDR_LEN    0xF
#define V_FSO_FCHDR_LEN(x) ((x) << S_FSO_FCHDR_LEN)
#define G_FSO_FCHDR_LEN(x) (((x) >> S_FSO_FCHDR_LEN) & M_FSO_FCHDR_LEN)

struct cpl_iscsi_hdr_no_rss {
	union opcode_tid ot;
	__be16 pdu_len_ddp;
	__be16 len;
	__be32 seq;
	__be16 urg;
	__u8 rsvd;
	__u8 status;
};

struct cpl_tx_data_iso {
	__be32 op_to_scsi;
	__u8   reserved1;
	__u8   ahs_len;
	__be16 mpdu;
	__be32 burst_size;
	__be32 len;
	__be32 reserved2_seglen_offset;
	__be32 datasn_offset;
	__be32 buffer_offset;
	__be32 reserved3;

	/* encapsulated CPL_TX_DATA follows here */
};

/* cpl_tx_data_iso.op_to_scsi fields */
#define S_CPL_TX_DATA_ISO_OP	24
#define M_CPL_TX_DATA_ISO_OP	0xff
#define V_CPL_TX_DATA_ISO_OP(x)	((x) << S_CPL_TX_DATA_ISO_OP)
#define G_CPL_TX_DATA_ISO_OP(x)	\
    (((x) >> S_CPL_TX_DATA_ISO_OP) & M_CPL_TX_DATA_ISO_OP)

#define S_CPL_TX_DATA_ISO_FIRST		23
#define M_CPL_TX_DATA_ISO_FIRST		0x1
#define V_CPL_TX_DATA_ISO_FIRST(x)	((x) << S_CPL_TX_DATA_ISO_FIRST)
#define G_CPL_TX_DATA_ISO_FIRST(x)	\
    (((x) >> S_CPL_TX_DATA_ISO_FIRST) & M_CPL_TX_DATA_ISO_FIRST)
#define F_CPL_TX_DATA_ISO_FIRST	V_CPL_TX_DATA_ISO_FIRST(1U)

#define S_CPL_TX_DATA_ISO_LAST		22
#define M_CPL_TX_DATA_ISO_LAST		0x1
#define V_CPL_TX_DATA_ISO_LAST(x)	((x) << S_CPL_TX_DATA_ISO_LAST)
#define G_CPL_TX_DATA_ISO_LAST(x)	\
    (((x) >> S_CPL_TX_DATA_ISO_LAST) & M_CPL_TX_DATA_ISO_LAST)
#define F_CPL_TX_DATA_ISO_LAST	V_CPL_TX_DATA_ISO_LAST(1U)

#define S_CPL_TX_DATA_ISO_CPLHDRLEN	21
#define M_CPL_TX_DATA_ISO_CPLHDRLEN	0x1
#define V_CPL_TX_DATA_ISO_CPLHDRLEN(x)	((x) << S_CPL_TX_DATA_ISO_CPLHDRLEN)
#define G_CPL_TX_DATA_ISO_CPLHDRLEN(x)	\
    (((x) >> S_CPL_TX_DATA_ISO_CPLHDRLEN) & M_CPL_TX_DATA_ISO_CPLHDRLEN)
#define F_CPL_TX_DATA_ISO_CPLHDRLEN	V_CPL_TX_DATA_ISO_CPLHDRLEN(1U)

#define S_CPL_TX_DATA_ISO_HDRCRC	20
#define M_CPL_TX_DATA_ISO_HDRCRC	0x1
#define V_CPL_TX_DATA_ISO_HDRCRC(x)	((x) << S_CPL_TX_DATA_ISO_HDRCRC)
#define G_CPL_TX_DATA_ISO_HDRCRC(x)	\
    (((x) >> S_CPL_TX_DATA_ISO_HDRCRC) & M_CPL_TX_DATA_ISO_HDRCRC)
#define F_CPL_TX_DATA_ISO_HDRCRC	V_CPL_TX_DATA_ISO_HDRCRC(1U)

#define S_CPL_TX_DATA_ISO_PLDCRC	19
#define M_CPL_TX_DATA_ISO_PLDCRC	0x1
#define V_CPL_TX_DATA_ISO_PLDCRC(x)	((x) << S_CPL_TX_DATA_ISO_PLDCRC)
#define G_CPL_TX_DATA_ISO_PLDCRC(x)	\
    (((x) >> S_CPL_TX_DATA_ISO_PLDCRC) & M_CPL_TX_DATA_ISO_PLDCRC)
#define F_CPL_TX_DATA_ISO_PLDCRC	V_CPL_TX_DATA_ISO_PLDCRC(1U)

#define S_CPL_TX_DATA_ISO_IMMEDIATE	18
#define M_CPL_TX_DATA_ISO_IMMEDIATE	0x1
#define V_CPL_TX_DATA_ISO_IMMEDIATE(x)	((x) << S_CPL_TX_DATA_ISO_IMMEDIATE)
#define G_CPL_TX_DATA_ISO_IMMEDIATE(x)	\
    (((x) >> S_CPL_TX_DATA_ISO_IMMEDIATE) & M_CPL_TX_DATA_ISO_IMMEDIATE)
#define F_CPL_TX_DATA_ISO_IMMEDIATE	V_CPL_TX_DATA_ISO_IMMEDIATE(1U)

#define S_CPL_TX_DATA_ISO_SCSI		16
#define M_CPL_TX_DATA_ISO_SCSI		0x3
#define V_CPL_TX_DATA_ISO_SCSI(x)	((x) << S_CPL_TX_DATA_ISO_SCSI)
#define G_CPL_TX_DATA_ISO_SCSI(x)	\
    (((x) >> S_CPL_TX_DATA_ISO_SCSI) & M_CPL_TX_DATA_ISO_SCSI)

/* cpl_tx_data_iso.reserved2_seglen_offset fields */
#define S_CPL_TX_DATA_ISO_SEGLEN_OFFSET		0
#define M_CPL_TX_DATA_ISO_SEGLEN_OFFSET		0xffffff
#define V_CPL_TX_DATA_ISO_SEGLEN_OFFSET(x)	\
    ((x) << S_CPL_TX_DATA_ISO_SEGLEN_OFFSET)
#define G_CPL_TX_DATA_ISO_SEGLEN_OFFSET(x)	\
    (((x) >> S_CPL_TX_DATA_ISO_SEGLEN_OFFSET) & \
     M_CPL_TX_DATA_ISO_SEGLEN_OFFSET)

struct cpl_iscsi_hdr {
	RSS_HDR
	union opcode_tid ot;
	__be16 pdu_len_ddp;
	__be16 len;
	__be32 seq;
	__be16 urg;
	__u8 rsvd;
	__u8 status;
};

/* cpl_iscsi_hdr.pdu_len_ddp fields */
#define S_ISCSI_PDU_LEN    0
#define M_ISCSI_PDU_LEN    0x7FFF
#define V_ISCSI_PDU_LEN(x) ((x) << S_ISCSI_PDU_LEN)
#define G_ISCSI_PDU_LEN(x) (((x) >> S_ISCSI_PDU_LEN) & M_ISCSI_PDU_LEN)

#define S_ISCSI_DDP    15
#define V_ISCSI_DDP(x) ((x) << S_ISCSI_DDP)
#define F_ISCSI_DDP    V_ISCSI_DDP(1U)

struct cpl_iscsi_data {
	RSS_HDR
	union opcode_tid ot;
	__u8 rsvd0[2];
	__be16 len;
	__be32 seq;
	__be16 urg;
	__u8 rsvd1;
	__u8 status;
};

struct cpl_rx_data {
	RSS_HDR
	union opcode_tid ot;
	__be16 rsvd;
	__be16 len;
	__be32 seq;
	__be16 urg;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 dack_mode:2;
	__u8 psh:1;
	__u8 heartbeat:1;
	__u8 ddp_off:1;
	__u8 :3;
#else
	__u8 :3;
	__u8 ddp_off:1;
	__u8 heartbeat:1;
	__u8 psh:1;
	__u8 dack_mode:2;
#endif
	__u8 status;
};

struct cpl_fcoe_hdr {
	RSS_HDR
	union opcode_tid ot;
	__be16 oxid;
	__be16 len;
	__be32 rctl_fctl;
	__u8 cs_ctl;
	__u8 df_ctl;
	__u8 sof;
	__u8 eof;
	__be16 seq_cnt;
	__u8 seq_id;
	__u8 type;
	__be32 param;
};

/* cpl_fcoe_hdr.rctl_fctl fields */
#define S_FCOE_FCHDR_RCTL	24
#define M_FCOE_FCHDR_RCTL	0xff
#define V_FCOE_FCHDR_RCTL(x)	((x) << S_FCOE_FCHDR_RCTL)
#define G_FCOE_FCHDR_RCTL(x)	\
	(((x) >> S_FCOE_FCHDR_RCTL) & M_FCOE_FCHDR_RCTL)

#define S_FCOE_FCHDR_FCTL	0
#define M_FCOE_FCHDR_FCTL	0xffffff
#define V_FCOE_FCHDR_FCTL(x)	((x) << S_FCOE_FCHDR_FCTL)
#define G_FCOE_FCHDR_FCTL(x)	\
	(((x) >> S_FCOE_FCHDR_FCTL) & M_FCOE_FCHDR_FCTL)

struct cpl_fcoe_data {
	RSS_HDR
	union opcode_tid ot;
	__u8 rsvd0[2];
	__be16 len;
	__be32 seq;
	__u8 rsvd1[3];
	__u8 status;
};

struct cpl_rx_urg_notify {
	RSS_HDR
	union opcode_tid ot;
	__be32 seq;
};

struct cpl_rx_urg_pkt {
	RSS_HDR
	union opcode_tid ot;
	__be16 rsvd;
	__be16 len;
};

struct cpl_rx_data_ack {
	WR_HDR;
	union opcode_tid ot;
	__be32 credit_dack;
};

struct cpl_rx_data_ack_core {
	union opcode_tid ot;
	__be32 credit_dack;
};

/* cpl_rx_data_ack.ack_seq fields */
#define S_RX_CREDITS    0
#define M_RX_CREDITS    0x3FFFFFF
#define V_RX_CREDITS(x) ((x) << S_RX_CREDITS)
#define G_RX_CREDITS(x) (((x) >> S_RX_CREDITS) & M_RX_CREDITS)

#define S_RX_MODULATE_TX    26
#define V_RX_MODULATE_TX(x) ((x) << S_RX_MODULATE_TX)
#define F_RX_MODULATE_TX    V_RX_MODULATE_TX(1U)

#define S_RX_MODULATE_RX    27
#define V_RX_MODULATE_RX(x) ((x) << S_RX_MODULATE_RX)
#define F_RX_MODULATE_RX    V_RX_MODULATE_RX(1U)

#define S_RX_FORCE_ACK    28
#define V_RX_FORCE_ACK(x) ((x) << S_RX_FORCE_ACK)
#define F_RX_FORCE_ACK    V_RX_FORCE_ACK(1U)

#define S_RX_DACK_MODE    29
#define M_RX_DACK_MODE    0x3
#define V_RX_DACK_MODE(x) ((x) << S_RX_DACK_MODE)
#define G_RX_DACK_MODE(x) (((x) >> S_RX_DACK_MODE) & M_RX_DACK_MODE)

#define S_RX_DACK_CHANGE    31
#define V_RX_DACK_CHANGE(x) ((x) << S_RX_DACK_CHANGE)
#define F_RX_DACK_CHANGE    V_RX_DACK_CHANGE(1U)

struct cpl_rx_ddp_complete {
	RSS_HDR
	union opcode_tid ot;
	__be32 ddp_report;
	__be32 rcv_nxt;
	__be32 rsvd;
};

struct cpl_rx_data_ddp {
	RSS_HDR
	union opcode_tid ot;
	__be16 urg;
	__be16 len;
	__be32 seq;
	union {
		__be32 nxt_seq;
		__be32 ddp_report;
	} u;
	__be32 ulp_crc;
	__be32 ddpvld;
};

#define cpl_rx_iscsi_ddp cpl_rx_data_ddp

struct cpl_rx_fcoe_ddp {
	RSS_HDR
	union opcode_tid ot;
	__be16 rsvd;
	__be16 len;
	__be32 seq;
	__be32 ddp_report;
	__be32 ulp_crc;
	__be32 ddpvld;
};

struct cpl_rx_data_dif {
	RSS_HDR
	union opcode_tid ot;
	__be16 ddp_len;
	__be16 msg_len;
	__be32 seq;
	union {
		__be32 nxt_seq;
		__be32 ddp_report;
	} u;
	__be32 err_vec;
	__be32 ddpvld;
};

struct cpl_rx_iscsi_dif {
	RSS_HDR
	union opcode_tid ot;
	__be16 ddp_len;
	__be16 msg_len;
	__be32 seq;
	union {
		__be32 nxt_seq;
		__be32 ddp_report;
	} u;
	__be32 ulp_crc;
	__be32 ddpvld;
	__u8 rsvd0[8];
	__be32 err_vec;
	__u8 rsvd1[4];
};

struct cpl_rx_iscsi_cmp {
	RSS_HDR
	union opcode_tid ot;
	__be16 pdu_len_ddp;
	__be16 len;
	__be32 seq;
	__be16 urg;
	__u8 rsvd;
	__u8 status;
	__be32 ulp_crc;
	__be32 ddpvld;
};

struct cpl_rx_fcoe_dif {
	RSS_HDR
	union opcode_tid ot;
	__be16 ddp_len;
	__be16 msg_len;
	__be32 seq;
	__be32 ddp_report;
	__be32 err_vec;
	__be32 ddpvld;
};

/* cpl_rx_{data,iscsi,fcoe}_{ddp,dif}.ddpvld fields */
#define S_DDP_VALID    15
#define M_DDP_VALID    0x1FFFF
#define V_DDP_VALID(x) ((x) << S_DDP_VALID)
#define G_DDP_VALID(x) (((x) >> S_DDP_VALID) & M_DDP_VALID)

#define S_DDP_PPOD_MISMATCH    15
#define V_DDP_PPOD_MISMATCH(x) ((x) << S_DDP_PPOD_MISMATCH)
#define F_DDP_PPOD_MISMATCH    V_DDP_PPOD_MISMATCH(1U)

#define S_DDP_PDU    16
#define V_DDP_PDU(x) ((x) << S_DDP_PDU)
#define F_DDP_PDU    V_DDP_PDU(1U)

#define S_DDP_LLIMIT_ERR    17
#define V_DDP_LLIMIT_ERR(x) ((x) << S_DDP_LLIMIT_ERR)
#define F_DDP_LLIMIT_ERR    V_DDP_LLIMIT_ERR(1U)

#define S_DDP_PPOD_PARITY_ERR    18
#define V_DDP_PPOD_PARITY_ERR(x) ((x) << S_DDP_PPOD_PARITY_ERR)
#define F_DDP_PPOD_PARITY_ERR    V_DDP_PPOD_PARITY_ERR(1U)

#define S_DDP_PADDING_ERR    19
#define V_DDP_PADDING_ERR(x) ((x) << S_DDP_PADDING_ERR)
#define F_DDP_PADDING_ERR    V_DDP_PADDING_ERR(1U)

#define S_DDP_HDRCRC_ERR    20
#define V_DDP_HDRCRC_ERR(x) ((x) << S_DDP_HDRCRC_ERR)
#define F_DDP_HDRCRC_ERR    V_DDP_HDRCRC_ERR(1U)

#define S_DDP_DATACRC_ERR    21
#define V_DDP_DATACRC_ERR(x) ((x) << S_DDP_DATACRC_ERR)
#define F_DDP_DATACRC_ERR    V_DDP_DATACRC_ERR(1U)

#define S_DDP_INVALID_TAG    22
#define V_DDP_INVALID_TAG(x) ((x) << S_DDP_INVALID_TAG)
#define F_DDP_INVALID_TAG    V_DDP_INVALID_TAG(1U)

#define S_DDP_ULIMIT_ERR    23
#define V_DDP_ULIMIT_ERR(x) ((x) << S_DDP_ULIMIT_ERR)
#define F_DDP_ULIMIT_ERR    V_DDP_ULIMIT_ERR(1U)

#define S_DDP_OFFSET_ERR    24
#define V_DDP_OFFSET_ERR(x) ((x) << S_DDP_OFFSET_ERR)
#define F_DDP_OFFSET_ERR    V_DDP_OFFSET_ERR(1U)

#define S_DDP_COLOR_ERR    25
#define V_DDP_COLOR_ERR(x) ((x) << S_DDP_COLOR_ERR)
#define F_DDP_COLOR_ERR    V_DDP_COLOR_ERR(1U)

#define S_DDP_TID_MISMATCH    26
#define V_DDP_TID_MISMATCH(x) ((x) << S_DDP_TID_MISMATCH)
#define F_DDP_TID_MISMATCH    V_DDP_TID_MISMATCH(1U)

#define S_DDP_INVALID_PPOD    27
#define V_DDP_INVALID_PPOD(x) ((x) << S_DDP_INVALID_PPOD)
#define F_DDP_INVALID_PPOD    V_DDP_INVALID_PPOD(1U)

#define S_DDP_ULP_MODE    28
#define M_DDP_ULP_MODE    0xF
#define V_DDP_ULP_MODE(x) ((x) << S_DDP_ULP_MODE)
#define G_DDP_ULP_MODE(x) (((x) >> S_DDP_ULP_MODE) & M_DDP_ULP_MODE)

/* cpl_rx_{data,iscsi,fcoe}_{ddp,dif}.ddp_report fields */
#define S_DDP_OFFSET    0
#define M_DDP_OFFSET    0xFFFFFF
#define V_DDP_OFFSET(x) ((x) << S_DDP_OFFSET)
#define G_DDP_OFFSET(x) (((x) >> S_DDP_OFFSET) & M_DDP_OFFSET)

#define S_DDP_DACK_MODE    24
#define M_DDP_DACK_MODE    0x3
#define V_DDP_DACK_MODE(x) ((x) << S_DDP_DACK_MODE)
#define G_DDP_DACK_MODE(x) (((x) >> S_DDP_DACK_MODE) & M_DDP_DACK_MODE)

#define S_DDP_BUF_IDX    26
#define V_DDP_BUF_IDX(x) ((x) << S_DDP_BUF_IDX)
#define F_DDP_BUF_IDX    V_DDP_BUF_IDX(1U)

#define S_DDP_URG    27
#define V_DDP_URG(x) ((x) << S_DDP_URG)
#define F_DDP_URG    V_DDP_URG(1U)

#define S_DDP_PSH    28
#define V_DDP_PSH(x) ((x) << S_DDP_PSH)
#define F_DDP_PSH    V_DDP_PSH(1U)

#define S_DDP_BUF_COMPLETE    29
#define V_DDP_BUF_COMPLETE(x) ((x) << S_DDP_BUF_COMPLETE)
#define F_DDP_BUF_COMPLETE    V_DDP_BUF_COMPLETE(1U)

#define S_DDP_BUF_TIMED_OUT    30
#define V_DDP_BUF_TIMED_OUT(x) ((x) << S_DDP_BUF_TIMED_OUT)
#define F_DDP_BUF_TIMED_OUT    V_DDP_BUF_TIMED_OUT(1U)

#define S_DDP_INV    31
#define V_DDP_INV(x) ((x) << S_DDP_INV)
#define F_DDP_INV    V_DDP_INV(1U)

struct cpl_rx_pkt {
	RSS_HDR
	__u8 opcode;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 iff:4;
	__u8 csum_calc:1;
	__u8 ipmi_pkt:1;
	__u8 vlan_ex:1;
	__u8 ip_frag:1;
#else
	__u8 ip_frag:1;
	__u8 vlan_ex:1;
	__u8 ipmi_pkt:1;
	__u8 csum_calc:1;
	__u8 iff:4;
#endif
	__be16 csum;
	__be16 vlan;
	__be16 len;
	__be32 l2info;
	__be16 hdr_len;
	__be16 err_vec;
};

/* rx_pkt.l2info fields */
#define S_RX_ETHHDR_LEN    0
#define M_RX_ETHHDR_LEN    0x1F
#define V_RX_ETHHDR_LEN(x) ((x) << S_RX_ETHHDR_LEN)
#define G_RX_ETHHDR_LEN(x) (((x) >> S_RX_ETHHDR_LEN) & M_RX_ETHHDR_LEN)

#define S_RX_T5_ETHHDR_LEN    0
#define M_RX_T5_ETHHDR_LEN    0x3F
#define V_RX_T5_ETHHDR_LEN(x) ((x) << S_RX_T5_ETHHDR_LEN)
#define G_RX_T5_ETHHDR_LEN(x) (((x) >> S_RX_T5_ETHHDR_LEN) & M_RX_T5_ETHHDR_LEN)

#define M_RX_T6_ETHHDR_LEN    0xFF
#define G_RX_T6_ETHHDR_LEN(x) (((x) >> S_RX_ETHHDR_LEN) & M_RX_T6_ETHHDR_LEN)

#define S_RX_PKTYPE    5
#define M_RX_PKTYPE    0x7
#define V_RX_PKTYPE(x) ((x) << S_RX_PKTYPE)
#define G_RX_PKTYPE(x) (((x) >> S_RX_PKTYPE) & M_RX_PKTYPE)

#define S_RX_T5_DATYPE    6
#define M_RX_T5_DATYPE    0x3
#define V_RX_T5_DATYPE(x) ((x) << S_RX_T5_DATYPE)
#define G_RX_T5_DATYPE(x) (((x) >> S_RX_T5_DATYPE) & M_RX_T5_DATYPE)

#define S_RX_MACIDX    8
#define M_RX_MACIDX    0x1FF
#define V_RX_MACIDX(x) ((x) << S_RX_MACIDX)
#define G_RX_MACIDX(x) (((x) >> S_RX_MACIDX) & M_RX_MACIDX)

#define S_RX_T5_PKTYPE    17
#define M_RX_T5_PKTYPE    0x7
#define V_RX_T5_PKTYPE(x) ((x) << S_RX_T5_PKTYPE)
#define G_RX_T5_PKTYPE(x) (((x) >> S_RX_T5_PKTYPE) & M_RX_T5_PKTYPE)

#define S_RX_DATYPE    18
#define M_RX_DATYPE    0x3
#define V_RX_DATYPE(x) ((x) << S_RX_DATYPE)
#define G_RX_DATYPE(x) (((x) >> S_RX_DATYPE) & M_RX_DATYPE)

#define S_RXF_PSH    20
#define V_RXF_PSH(x) ((x) << S_RXF_PSH)
#define F_RXF_PSH    V_RXF_PSH(1U)

#define S_RXF_SYN    21
#define V_RXF_SYN(x) ((x) << S_RXF_SYN)
#define F_RXF_SYN    V_RXF_SYN(1U)

#define S_RXF_UDP    22
#define V_RXF_UDP(x) ((x) << S_RXF_UDP)
#define F_RXF_UDP    V_RXF_UDP(1U)

#define S_RXF_TCP    23
#define V_RXF_TCP(x) ((x) << S_RXF_TCP)
#define F_RXF_TCP    V_RXF_TCP(1U)

#define S_RXF_IP    24
#define V_RXF_IP(x) ((x) << S_RXF_IP)
#define F_RXF_IP    V_RXF_IP(1U)

#define S_RXF_IP6    25
#define V_RXF_IP6(x) ((x) << S_RXF_IP6)
#define F_RXF_IP6    V_RXF_IP6(1U)

#define S_RXF_SYN_COOKIE    26
#define V_RXF_SYN_COOKIE(x) ((x) << S_RXF_SYN_COOKIE)
#define F_RXF_SYN_COOKIE    V_RXF_SYN_COOKIE(1U)

#define S_RXF_FCOE    26
#define V_RXF_FCOE(x) ((x) << S_RXF_FCOE)
#define F_RXF_FCOE    V_RXF_FCOE(1U)

#define S_RXF_LRO    27
#define V_RXF_LRO(x) ((x) << S_RXF_LRO)
#define F_RXF_LRO    V_RXF_LRO(1U)

#define S_RX_CHAN    28
#define M_RX_CHAN    0xF
#define V_RX_CHAN(x) ((x) << S_RX_CHAN)
#define G_RX_CHAN(x) (((x) >> S_RX_CHAN) & M_RX_CHAN)

/* rx_pkt.hdr_len fields */
#define S_RX_TCPHDR_LEN    0
#define M_RX_TCPHDR_LEN    0x3F
#define V_RX_TCPHDR_LEN(x) ((x) << S_RX_TCPHDR_LEN)
#define G_RX_TCPHDR_LEN(x) (((x) >> S_RX_TCPHDR_LEN) & M_RX_TCPHDR_LEN)

#define S_RX_IPHDR_LEN    6
#define M_RX_IPHDR_LEN    0x3FF
#define V_RX_IPHDR_LEN(x) ((x) << S_RX_IPHDR_LEN)
#define G_RX_IPHDR_LEN(x) (((x) >> S_RX_IPHDR_LEN) & M_RX_IPHDR_LEN)

/* rx_pkt.err_vec fields */
#define S_RXERR_OR    0
#define V_RXERR_OR(x) ((x) << S_RXERR_OR)
#define F_RXERR_OR    V_RXERR_OR(1U)

#define S_RXERR_MAC    1
#define V_RXERR_MAC(x) ((x) << S_RXERR_MAC)
#define F_RXERR_MAC    V_RXERR_MAC(1U)

#define S_RXERR_IPVERS    2
#define V_RXERR_IPVERS(x) ((x) << S_RXERR_IPVERS)
#define F_RXERR_IPVERS    V_RXERR_IPVERS(1U)

#define S_RXERR_FRAG    3
#define V_RXERR_FRAG(x) ((x) << S_RXERR_FRAG)
#define F_RXERR_FRAG    V_RXERR_FRAG(1U)

#define S_RXERR_ATTACK    4
#define V_RXERR_ATTACK(x) ((x) << S_RXERR_ATTACK)
#define F_RXERR_ATTACK    V_RXERR_ATTACK(1U)

#define S_RXERR_ETHHDR_LEN    5
#define V_RXERR_ETHHDR_LEN(x) ((x) << S_RXERR_ETHHDR_LEN)
#define F_RXERR_ETHHDR_LEN    V_RXERR_ETHHDR_LEN(1U)

#define S_RXERR_IPHDR_LEN    6
#define V_RXERR_IPHDR_LEN(x) ((x) << S_RXERR_IPHDR_LEN)
#define F_RXERR_IPHDR_LEN    V_RXERR_IPHDR_LEN(1U)

#define S_RXERR_TCPHDR_LEN    7
#define V_RXERR_TCPHDR_LEN(x) ((x) << S_RXERR_TCPHDR_LEN)
#define F_RXERR_TCPHDR_LEN    V_RXERR_TCPHDR_LEN(1U)

#define S_RXERR_PKT_LEN    8
#define V_RXERR_PKT_LEN(x) ((x) << S_RXERR_PKT_LEN)
#define F_RXERR_PKT_LEN    V_RXERR_PKT_LEN(1U)

#define S_RXERR_TCP_OPT    9
#define V_RXERR_TCP_OPT(x) ((x) << S_RXERR_TCP_OPT)
#define F_RXERR_TCP_OPT    V_RXERR_TCP_OPT(1U)

#define S_RXERR_IPCSUM    12
#define V_RXERR_IPCSUM(x) ((x) << S_RXERR_IPCSUM)
#define F_RXERR_IPCSUM    V_RXERR_IPCSUM(1U)

#define S_RXERR_CSUM    13
#define V_RXERR_CSUM(x) ((x) << S_RXERR_CSUM)
#define F_RXERR_CSUM    V_RXERR_CSUM(1U)

#define S_RXERR_PING    14
#define V_RXERR_PING(x) ((x) << S_RXERR_PING)
#define F_RXERR_PING    V_RXERR_PING(1U)

/* In T6, rx_pkt.err_vec indicates
 * RxError Error vector (16b) or
 * Encapsulating header length (8b),
 * Outer encapsulation type (2b) and
 * compressed error vector (6b) if CRxPktEnc is
 * enabled in TP_OUT_CONFIG
 */

#define S_T6_COMPR_RXERR_VEC    0
#define M_T6_COMPR_RXERR_VEC    0x3F
#define V_T6_COMPR_RXERR_VEC(x) ((x) << S_T6_COMPR_RXERR_VEC)
#define G_T6_COMPR_RXERR_VEC(x) \
		(((x) >> S_T6_COMPR_RXERR_VEC) & M_T6_COMPR_RXERR_VEC)

#define S_T6_COMPR_RXERR_MAC    0
#define V_T6_COMPR_RXERR_MAC(x) ((x) << S_T6_COMPR_RXERR_MAC)
#define F_T6_COMPR_RXERR_MAC    V_T6_COMPR_RXERR_MAC(1U)

/* Logical OR of RX_ERROR_PKT_LEN, RX_ERROR_TCP_HDR_LEN
 * RX_ERROR_IP_HDR_LEN, RX_ERROR_ETH_HDR_LEN
 */
#define S_T6_COMPR_RXERR_LEN    1
#define V_T6_COMPR_RXERR_LEN(x) ((x) << S_T6_COMPR_RXERR_LEN)
#define F_T6_COMPR_RXERR_LEN    V_COMPR_T6_RXERR_LEN(1U)

#define S_T6_COMPR_RXERR_TCP_OPT    2
#define V_T6_COMPR_RXERR_TCP_OPT(x) ((x) << S_T6_COMPR_RXERR_TCP_OPT)
#define F_T6_COMPR_RXERR_TCP_OPT    V_T6_COMPR_RXERR_TCP_OPT(1U)

#define S_T6_COMPR_RXERR_IPV6_EXT    3
#define V_T6_COMPR_RXERR_IPV6_EXT(x) ((x) << S_T6_COMPR_RXERR_IPV6_EXT)
#define F_T6_COMPR_RXERR_IPV6_EXT    V_T6_COMPR_RXERR_IPV6_EXT(1U)

/* Logical OR of RX_ERROR_CSUM, RX_ERROR_CSIP */
#define S_T6_COMPR_RXERR_SUM   4
#define V_T6_COMPR_RXERR_SUM(x) ((x) << S_T6_COMPR_RXERR_SUM)
#define F_T6_COMPR_RXERR_SUM    V_T6_COMPR_RXERR_SUM(1U)

/* Logical OR of RX_ERROR_FPMA, RX_ERROR_PING_DROP,
 * RX_ERROR_ATTACK, RX_ERROR_FRAG,RX_ERROR_IPVERSION
 */
#define S_T6_COMPR_RXERR_MISC   5
#define V_T6_COMPR_RXERR_MISC(x) ((x) << S_T6_COMPR_RXERR_MISC)
#define F_T6_COMPR_RXERR_MISC    V_T6_COMPR_RXERR_MISC(1U)

#define S_T6_RX_TNL_TYPE    6
#define M_T6_RX_TNL_TYPE    0x3
#define V_T6_RX_TNL_TYPE(x) ((x) << S_T6_RX_TNL_TYPE)
#define G_T6_RX_TNL_TYPE(x) (((x) >> S_T6_RX_TNL_TYPE) & M_T6_RX_TNL_TYPE)

#define RX_PKT_TNL_TYPE_NVGRE	1
#define RX_PKT_TNL_TYPE_VXLAN	2
#define RX_PKT_TNL_TYPE_GENEVE	3

#define S_T6_RX_TNLHDR_LEN    8
#define M_T6_RX_TNLHDR_LEN    0xFF
#define V_T6_RX_TNLHDR_LEN(x) ((x) << S_T6_RX_TNLHDR_LEN)
#define G_T6_RX_TNLHDR_LEN(x) (((x) >> S_T6_RX_TNLHDR_LEN) & M_T6_RX_TNLHDR_LEN)

struct cpl_trace_pkt {
	RSS_HDR
	__u8 opcode;
	__u8 intf;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 runt:4;
	__u8 filter_hit:4;
	__u8 :6;
	__u8 err:1;
	__u8 trunc:1;
#else
	__u8 filter_hit:4;
	__u8 runt:4;
	__u8 trunc:1;
	__u8 err:1;
	__u8 :6;
#endif
	__be16 rsvd;
	__be16 len;
	__be64 tstamp;
};

struct cpl_t5_trace_pkt {
	RSS_HDR
	__u8 opcode;
	__u8 intf;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 runt:4;
	__u8 filter_hit:4;
	__u8 :6;
	__u8 err:1;
	__u8 trunc:1;
#else
	__u8 filter_hit:4;
	__u8 runt:4;
	__u8 trunc:1;
	__u8 err:1;
	__u8 :6;
#endif
	__be16 rsvd;
	__be16 len;
	__be64 tstamp;
	__be64 rsvd1;
};

struct cpl_rte_delete_req {
	WR_HDR;
	union opcode_tid ot;
	__be32 params;
};

/* {cpl_rte_delete_req, cpl_rte_read_req}.params fields */
#define S_RTE_REQ_LUT_IX    8
#define M_RTE_REQ_LUT_IX    0x7FF
#define V_RTE_REQ_LUT_IX(x) ((x) << S_RTE_REQ_LUT_IX)
#define G_RTE_REQ_LUT_IX(x) (((x) >> S_RTE_REQ_LUT_IX) & M_RTE_REQ_LUT_IX)

#define S_RTE_REQ_LUT_BASE    19
#define M_RTE_REQ_LUT_BASE    0x7FF
#define V_RTE_REQ_LUT_BASE(x) ((x) << S_RTE_REQ_LUT_BASE)
#define G_RTE_REQ_LUT_BASE(x) (((x) >> S_RTE_REQ_LUT_BASE) & M_RTE_REQ_LUT_BASE)

#define S_RTE_READ_REQ_SELECT    31
#define V_RTE_READ_REQ_SELECT(x) ((x) << S_RTE_READ_REQ_SELECT)
#define F_RTE_READ_REQ_SELECT    V_RTE_READ_REQ_SELECT(1U)

struct cpl_rte_delete_rpl {
	RSS_HDR
	union opcode_tid ot;
	__u8 status;
	__u8 rsvd[3];
};

struct cpl_rte_write_req {
	WR_HDR;
	union opcode_tid ot;
	__u32 write_sel;
	__be32 lut_params;
	__be32 l2t_idx;
	__be32 netmask;
	__be32 faddr;
};

/* cpl_rte_write_req.write_sel fields */
#define S_RTE_WR_L2TIDX    31
#define V_RTE_WR_L2TIDX(x) ((x) << S_RTE_WR_L2TIDX)
#define F_RTE_WR_L2TIDX    V_RTE_WR_L2TIDX(1U)

#define S_RTE_WR_FADDR    30
#define V_RTE_WR_FADDR(x) ((x) << S_RTE_WR_FADDR)
#define F_RTE_WR_FADDR    V_RTE_WR_FADDR(1U)

/* cpl_rte_write_req.lut_params fields */
#define S_RTE_WR_LUT_IX    10
#define M_RTE_WR_LUT_IX    0x7FF
#define V_RTE_WR_LUT_IX(x) ((x) << S_RTE_WR_LUT_IX)
#define G_RTE_WR_LUT_IX(x) (((x) >> S_RTE_WR_LUT_IX) & M_RTE_WR_LUT_IX)

#define S_RTE_WR_LUT_BASE    21
#define M_RTE_WR_LUT_BASE    0x7FF
#define V_RTE_WR_LUT_BASE(x) ((x) << S_RTE_WR_LUT_BASE)
#define G_RTE_WR_LUT_BASE(x) (((x) >> S_RTE_WR_LUT_BASE) & M_RTE_WR_LUT_BASE)

struct cpl_rte_write_rpl {
	RSS_HDR
	union opcode_tid ot;
	__u8 status;
	__u8 rsvd[3];
};

struct cpl_rte_read_req {
	WR_HDR;
	union opcode_tid ot;
	__be32 params;
};

struct cpl_rte_read_rpl {
	RSS_HDR
	union opcode_tid ot;
	__u8 status;
	__u8 rsvd;
	__be16 l2t_idx;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u32 :30;
	__u32 select:1;
#else
	__u32 select:1;
	__u32 :30;
#endif
	__be32 addr;
};

struct cpl_l2t_write_req {
	WR_HDR;
	union opcode_tid ot;
	__be16 params;
	__be16 l2t_idx;
	__be16 vlan;
	__u8   dst_mac[6];
};

/* cpl_l2t_write_req.params fields */
#define S_L2T_W_INFO    2
#define M_L2T_W_INFO    0x3F
#define V_L2T_W_INFO(x) ((x) << S_L2T_W_INFO)
#define G_L2T_W_INFO(x) (((x) >> S_L2T_W_INFO) & M_L2T_W_INFO)

#define S_L2T_W_PORT    8
#define M_L2T_W_PORT    0x3
#define V_L2T_W_PORT(x) ((x) << S_L2T_W_PORT)
#define G_L2T_W_PORT(x) (((x) >> S_L2T_W_PORT) & M_L2T_W_PORT)

#define S_L2T_W_LPBK    10
#define V_L2T_W_LPBK(x) ((x) << S_L2T_W_LPBK)
#define F_L2T_W_PKBK    V_L2T_W_LPBK(1U)

#define S_L2T_W_ARPMISS         11
#define V_L2T_W_ARPMISS(x)      ((x) << S_L2T_W_ARPMISS)
#define F_L2T_W_ARPMISS         V_L2T_W_ARPMISS(1U)

#define S_L2T_W_NOREPLY    15
#define V_L2T_W_NOREPLY(x) ((x) << S_L2T_W_NOREPLY)
#define F_L2T_W_NOREPLY    V_L2T_W_NOREPLY(1U)

#define CPL_L2T_VLAN_NONE 0xfff

struct cpl_l2t_write_rpl {
	RSS_HDR
	union opcode_tid ot;
	__u8 status;
	__u8 rsvd[3];
};

struct cpl_l2t_read_req {
	WR_HDR;
	union opcode_tid ot;
	__be32 l2t_idx;
};

struct cpl_l2t_read_rpl {
	RSS_HDR
	union opcode_tid ot;
	__u8 status;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 :4;
	__u8 iff:4;
#else
	__u8 iff:4;
	__u8 :4;
#endif
	__be16 vlan;
	__be16 info;
	__u8 dst_mac[6];
};

struct cpl_srq_table_req {
	WR_HDR;
	union opcode_tid ot;
	__u8 status;
	__u8 rsvd[2];
	__u8 idx;
	__be64 rsvd_pdid;
	__be32 qlen_qbase;
	__be16 cur_msn;
	__be16 max_msn;
};

struct cpl_srq_table_rpl {
	RSS_HDR
	union opcode_tid ot;
	__u8 status;
	__u8 rsvd[2];
	__u8 idx;
	__be64 rsvd_pdid;
	__be32 qlen_qbase;
	__be16 cur_msn;
	__be16 max_msn;
};

/* cpl_srq_table_{req,rpl}.params fields */
#define S_SRQT_QLEN   28
#define M_SRQT_QLEN   0xF
#define V_SRQT_QLEN(x) ((x) << S_SRQT_QLEN)
#define G_SRQT_QLEN(x) (((x) >> S_SRQT_QLEN) & M_SRQT_QLEN)

#define S_SRQT_QBASE    0
#define M_SRQT_QBASE   0x3FFFFFF
#define V_SRQT_QBASE(x) ((x) << S_SRQT_QBASE)
#define G_SRQT_QBASE(x) (((x) >> S_SRQT_QBASE) & M_SRQT_QBASE)

#define S_SRQT_PDID    0
#define M_SRQT_PDID   0xFF
#define V_SRQT_PDID(x) ((x) << S_SRQT_PDID)
#define G_SRQT_PDID(x) (((x) >> S_SRQT_PDID) & M_SRQT_PDID)

#define S_SRQT_IDX    0
#define M_SRQT_IDX    0xF
#define V_SRQT_IDX(x) ((x) << S_SRQT_IDX)
#define G_SRQT_IDX(x) (((x) >> S_SRQT_IDX) & M_SRQT_IDX)

struct cpl_smt_write_req {
	WR_HDR;
	union opcode_tid ot;
	__be32 params;
	__be16 pfvf1;
	__u8   src_mac1[6];
	__be16 pfvf0;
	__u8   src_mac0[6];
};

struct cpl_t6_smt_write_req {
	WR_HDR;
	union opcode_tid ot;
	__be32 params;
	__be64 tag;
	__be16 pfvf0;
	__u8   src_mac0[6];
	__be32 local_ip;
	__be32 rsvd;
};

struct cpl_smt_write_rpl {
	RSS_HDR
	union opcode_tid ot;
	__u8 status;
	__u8 rsvd[3];
};

struct cpl_smt_read_req {
	WR_HDR;
	union opcode_tid ot;
	__be32 params;
};

struct cpl_smt_read_rpl {
	RSS_HDR
	union opcode_tid ot;
	__u8   status;
	__u8   ovlan_idx;
	__be16 rsvd;
	__be16 pfvf1;
	__u8   src_mac1[6];
	__be16 pfvf0;
	__u8   src_mac0[6];
};

/* cpl_smt_{read,write}_req.params fields */
#define S_SMTW_OVLAN_IDX    16
#define M_SMTW_OVLAN_IDX    0xF
#define V_SMTW_OVLAN_IDX(x) ((x) << S_SMTW_OVLAN_IDX)
#define G_SMTW_OVLAN_IDX(x) (((x) >> S_SMTW_OVLAN_IDX) & M_SMTW_OVLAN_IDX)

#define S_SMTW_IDX    20
#define M_SMTW_IDX    0x7F
#define V_SMTW_IDX(x) ((x) << S_SMTW_IDX)
#define G_SMTW_IDX(x) (((x) >> S_SMTW_IDX) & M_SMTW_IDX)

#define M_T6_SMTW_IDX    0xFF
#define G_T6_SMTW_IDX(x) (((x) >> S_SMTW_IDX) & M_T6_SMTW_IDX)

#define S_SMTW_NORPL    31
#define V_SMTW_NORPL(x) ((x) << S_SMTW_NORPL)
#define F_SMTW_NORPL    V_SMTW_NORPL(1U)

/* cpl_smt_{read,write}_req.pfvf? fields */
#define S_SMTW_VF    0
#define M_SMTW_VF    0xFF
#define V_SMTW_VF(x) ((x) << S_SMTW_VF)
#define G_SMTW_VF(x) (((x) >> S_SMTW_VF) & M_SMTW_VF)

#define S_SMTW_PF    8
#define M_SMTW_PF    0x7
#define V_SMTW_PF(x) ((x) << S_SMTW_PF)
#define G_SMTW_PF(x) (((x) >> S_SMTW_PF) & M_SMTW_PF)

#define S_SMTW_VF_VLD    11
#define V_SMTW_VF_VLD(x) ((x) << S_SMTW_VF_VLD)
#define F_SMTW_VF_VLD    V_SMTW_VF_VLD(1U)

struct cpl_tag_write_req {
	WR_HDR;
	union opcode_tid ot;
	__be32 params;
	__be64 tag_val;
};

struct cpl_tag_write_rpl {
	RSS_HDR
	union opcode_tid ot;
	__u8 status;
	__u8 rsvd[2];
	__u8 idx;
};

struct cpl_tag_read_req {
	WR_HDR;
	union opcode_tid ot;
	__be32 params;
};

struct cpl_tag_read_rpl {
	RSS_HDR
	union opcode_tid ot;
	__u8   status;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 :4;
	__u8 tag_len:1;
	__u8 :2;
	__u8 ins_enable:1;
#else
	__u8 ins_enable:1;
	__u8 :2;
	__u8 tag_len:1;
	__u8 :4;
#endif
	__u8   rsvd;
	__u8   tag_idx;
	__be64 tag_val;
};

/* cpl_tag{read,write}_req.params fields */
#define S_TAGW_IDX    0
#define M_TAGW_IDX    0x7F
#define V_TAGW_IDX(x) ((x) << S_TAGW_IDX)
#define G_TAGW_IDX(x) (((x) >> S_TAGW_IDX) & M_TAGW_IDX)

#define S_TAGW_LEN    20
#define V_TAGW_LEN(x) ((x) << S_TAGW_LEN)
#define F_TAGW_LEN    V_TAGW_LEN(1U)

#define S_TAGW_INS_ENABLE    23
#define V_TAGW_INS_ENABLE(x) ((x) << S_TAGW_INS_ENABLE)
#define F_TAGW_INS_ENABLE    V_TAGW_INS_ENABLE(1U)

#define S_TAGW_NORPL    31
#define V_TAGW_NORPL(x) ((x) << S_TAGW_NORPL)
#define F_TAGW_NORPL    V_TAGW_NORPL(1U)

struct cpl_barrier {
	WR_HDR;
	__u8 opcode;
	__u8 chan_map;
	__be16 rsvd0;
	__be32 rsvd1;
};

/* cpl_barrier.chan_map fields */
#define S_CHAN_MAP    4
#define M_CHAN_MAP    0xF
#define V_CHAN_MAP(x) ((x) << S_CHAN_MAP)
#define G_CHAN_MAP(x) (((x) >> S_CHAN_MAP) & M_CHAN_MAP)

struct cpl_error {
	RSS_HDR
	union opcode_tid ot;
	__be32 error;
};

struct cpl_hit_notify {
	RSS_HDR
	union opcode_tid ot;
	__be32 rsvd;
	__be32 info;
	__be32 reason;
};

struct cpl_pkt_notify {
	RSS_HDR
	union opcode_tid ot;
	__be16 rsvd;
	__be16 len;
	__be32 info;
	__be32 reason;
};

/* cpl_{hit,pkt}_notify.info fields */
#define S_NTFY_MAC_IDX    0
#define M_NTFY_MAC_IDX    0x1FF
#define V_NTFY_MAC_IDX(x) ((x) << S_NTFY_MAC_IDX)
#define G_NTFY_MAC_IDX(x) (((x) >> S_NTFY_MAC_IDX) & M_NTFY_MAC_IDX)

#define S_NTFY_INTF    10
#define M_NTFY_INTF    0xF
#define V_NTFY_INTF(x) ((x) << S_NTFY_INTF)
#define G_NTFY_INTF(x) (((x) >> S_NTFY_INTF) & M_NTFY_INTF)

#define S_NTFY_TCPHDR_LEN    14
#define M_NTFY_TCPHDR_LEN    0xF
#define V_NTFY_TCPHDR_LEN(x) ((x) << S_NTFY_TCPHDR_LEN)
#define G_NTFY_TCPHDR_LEN(x) (((x) >> S_NTFY_TCPHDR_LEN) & M_NTFY_TCPHDR_LEN)

#define S_NTFY_IPHDR_LEN    18
#define M_NTFY_IPHDR_LEN    0x1FF
#define V_NTFY_IPHDR_LEN(x) ((x) << S_NTFY_IPHDR_LEN)
#define G_NTFY_IPHDR_LEN(x) (((x) >> S_NTFY_IPHDR_LEN) & M_NTFY_IPHDR_LEN)

#define S_NTFY_ETHHDR_LEN    27
#define M_NTFY_ETHHDR_LEN    0x1F
#define V_NTFY_ETHHDR_LEN(x) ((x) << S_NTFY_ETHHDR_LEN)
#define G_NTFY_ETHHDR_LEN(x) (((x) >> S_NTFY_ETHHDR_LEN) & M_NTFY_ETHHDR_LEN)

#define S_NTFY_T5_IPHDR_LEN    18
#define M_NTFY_T5_IPHDR_LEN    0xFF
#define V_NTFY_T5_IPHDR_LEN(x) ((x) << S_NTFY_T5_IPHDR_LEN)
#define G_NTFY_T5_IPHDR_LEN(x) (((x) >> S_NTFY_T5_IPHDR_LEN) & M_NTFY_T5_IPHDR_LEN)

#define S_NTFY_T5_ETHHDR_LEN    26
#define M_NTFY_T5_ETHHDR_LEN    0x3F
#define V_NTFY_T5_ETHHDR_LEN(x) ((x) << S_NTFY_T5_ETHHDR_LEN)
#define G_NTFY_T5_ETHHDR_LEN(x) (((x) >> S_NTFY_T5_ETHHDR_LEN) & M_NTFY_T5_ETHHDR_LEN)

struct cpl_rdma_terminate {
	RSS_HDR
	union opcode_tid ot;
	__be16 rsvd;
	__be16 len;
};

struct cpl_set_le_req {
	WR_HDR;
	union opcode_tid ot;
	__be16 reply_ctrl;
	__be16 params;
	__be64 mask_hi;
	__be64 mask_lo;
	__be64 val_hi;
	__be64 val_lo;
};

/* cpl_set_le_req.reply_ctrl additional fields */
#define S_LE_REQ_IP6    13
#define V_LE_REQ_IP6(x) ((x) << S_LE_REQ_IP6)
#define F_LE_REQ_IP6    V_LE_REQ_IP6(1U)

/* cpl_set_le_req.params fields */
#define S_LE_CHAN    0
#define M_LE_CHAN    0x3
#define V_LE_CHAN(x) ((x) << S_LE_CHAN)
#define G_LE_CHAN(x) (((x) >> S_LE_CHAN) & M_LE_CHAN)

#define S_LE_OFFSET    5
#define M_LE_OFFSET    0x7
#define V_LE_OFFSET(x) ((x) << S_LE_OFFSET)
#define G_LE_OFFSET(x) (((x) >> S_LE_OFFSET) & M_LE_OFFSET)

#define S_LE_MORE    8
#define V_LE_MORE(x) ((x) << S_LE_MORE)
#define F_LE_MORE    V_LE_MORE(1U)

#define S_LE_REQSIZE    9
#define M_LE_REQSIZE    0x7
#define V_LE_REQSIZE(x) ((x) << S_LE_REQSIZE)
#define G_LE_REQSIZE(x) (((x) >> S_LE_REQSIZE) & M_LE_REQSIZE)

#define S_LE_REQCMD    12
#define M_LE_REQCMD    0xF
#define V_LE_REQCMD(x) ((x) << S_LE_REQCMD)
#define G_LE_REQCMD(x) (((x) >> S_LE_REQCMD) & M_LE_REQCMD)

struct cpl_set_le_rpl {
	RSS_HDR
	union opcode_tid ot;
	__u8 chan;
	__u8 info;
	__be16 len;
};

/* cpl_set_le_rpl.info fields */
#define S_LE_RSPCMD    0
#define M_LE_RSPCMD    0xF
#define V_LE_RSPCMD(x) ((x) << S_LE_RSPCMD)
#define G_LE_RSPCMD(x) (((x) >> S_LE_RSPCMD) & M_LE_RSPCMD)

#define S_LE_RSPSIZE    4
#define M_LE_RSPSIZE    0x7
#define V_LE_RSPSIZE(x) ((x) << S_LE_RSPSIZE)
#define G_LE_RSPSIZE(x) (((x) >> S_LE_RSPSIZE) & M_LE_RSPSIZE)

#define S_LE_RSPTYPE    7
#define V_LE_RSPTYPE(x) ((x) << S_LE_RSPTYPE)
#define F_LE_RSPTYPE    V_LE_RSPTYPE(1U)

struct cpl_sge_egr_update {
	RSS_HDR
	__be32 opcode_qid;
	__be16 cidx;
	__be16 pidx;
};

/* cpl_sge_egr_update.ot fields */
#define S_AUTOEQU	22
#define M_AUTOEQU	0x1
#define V_AUTOEQU(x)	((x) << S_AUTOEQU)
#define G_AUTOEQU(x)	(((x) >> S_AUTOEQU) & M_AUTOEQU)

#define S_EGR_QID    0
#define M_EGR_QID    0x1FFFF
#define V_EGR_QID(x) ((x) << S_EGR_QID)
#define G_EGR_QID(x) (((x) >> S_EGR_QID) & M_EGR_QID)

/* cpl_fw*.type values */
enum {
	FW_TYPE_CMD_RPL = 0,
	FW_TYPE_WR_RPL = 1,
	FW_TYPE_CQE = 2,
	FW_TYPE_OFLD_CONNECTION_WR_RPL = 3,
	FW_TYPE_RSSCPL = 4,
	FW_TYPE_WRERR_RPL = 5,
	FW_TYPE_PI_ERR = 6,
	FW_TYPE_TLS_KEY = 7,
};

struct cpl_fw2_pld {
	RSS_HDR
	u8 opcode;
	u8 rsvd[5];
	__be16 len;
};

struct cpl_fw4_pld {
	RSS_HDR
	u8 opcode;
	u8 rsvd0[3];
	u8 type;
	u8 rsvd1;
	__be16 len;
	__be64 data;
	__be64 rsvd2;
};

struct cpl_fw6_pld {
	RSS_HDR
	u8 opcode;
	u8 rsvd[5];
	__be16 len;
	__be64 data[4];
};

struct cpl_fw2_msg {
	RSS_HDR
	union opcode_info oi;
};

struct cpl_fw4_msg {
	RSS_HDR
	u8 opcode;
	u8 type;
	__be16 rsvd0;
	__be32 rsvd1;
	__be64 data[2];
};

struct cpl_fw4_ack {
	RSS_HDR
	union opcode_tid ot;
	u8 credits;
	u8 rsvd0[2];
	u8 flags;
	__be32 snd_nxt;
	__be32 snd_una;
	__be64 rsvd1;
};

enum {
	CPL_FW4_ACK_FLAGS_SEQVAL	= 0x1,	/* seqn valid */
	CPL_FW4_ACK_FLAGS_CH		= 0x2,	/* channel change complete */
	CPL_FW4_ACK_FLAGS_FLOWC		= 0x4,	/* fw_flowc_wr complete */
};

#define S_CPL_FW4_ACK_OPCODE    24
#define M_CPL_FW4_ACK_OPCODE    0xff
#define V_CPL_FW4_ACK_OPCODE(x) ((x) << S_CPL_FW4_ACK_OPCODE)
#define G_CPL_FW4_ACK_OPCODE(x) \
    (((x) >> S_CPL_FW4_ACK_OPCODE) & M_CPL_FW4_ACK_OPCODE)

#define S_CPL_FW4_ACK_FLOWID    0
#define M_CPL_FW4_ACK_FLOWID    0xffffff
#define V_CPL_FW4_ACK_FLOWID(x) ((x) << S_CPL_FW4_ACK_FLOWID)
#define G_CPL_FW4_ACK_FLOWID(x) \
    (((x) >> S_CPL_FW4_ACK_FLOWID) & M_CPL_FW4_ACK_FLOWID)

#define S_CPL_FW4_ACK_CR        24
#define M_CPL_FW4_ACK_CR        0xff
#define V_CPL_FW4_ACK_CR(x)     ((x) << S_CPL_FW4_ACK_CR)
#define G_CPL_FW4_ACK_CR(x)     (((x) >> S_CPL_FW4_ACK_CR) & M_CPL_FW4_ACK_CR)

#define S_CPL_FW4_ACK_SEQVAL    0
#define M_CPL_FW4_ACK_SEQVAL    0x1
#define V_CPL_FW4_ACK_SEQVAL(x) ((x) << S_CPL_FW4_ACK_SEQVAL)
#define G_CPL_FW4_ACK_SEQVAL(x) \
    (((x) >> S_CPL_FW4_ACK_SEQVAL) & M_CPL_FW4_ACK_SEQVAL)
#define F_CPL_FW4_ACK_SEQVAL    V_CPL_FW4_ACK_SEQVAL(1U)

struct cpl_fw6_msg {
	RSS_HDR
	u8 opcode;
	u8 type;
	__be16 rsvd0;
	__be32 rsvd1;
	__be64 data[4];
};

/* cpl_fw6_msg.type values */
enum {
	FW6_TYPE_CMD_RPL	= FW_TYPE_CMD_RPL,
	FW6_TYPE_WR_RPL		= FW_TYPE_WR_RPL,
	FW6_TYPE_CQE		= FW_TYPE_CQE,
	FW6_TYPE_OFLD_CONNECTION_WR_RPL = FW_TYPE_OFLD_CONNECTION_WR_RPL,
	FW6_TYPE_RSSCPL		= FW_TYPE_RSSCPL,
	FW6_TYPE_WRERR_RPL	= FW_TYPE_WRERR_RPL,
	FW6_TYPE_PI_ERR		= FW_TYPE_PI_ERR,
	NUM_FW6_TYPES
};

struct cpl_fw6_msg_ofld_connection_wr_rpl {
	__u64	cookie;
	__be32	tid;	/* or atid in case of active failure */
	__u8	t_state;
	__u8	retval;
	__u8	rsvd[2];
};

/* ULP_TX opcodes */
enum {
	ULP_TX_MEM_READ = 2,
	ULP_TX_MEM_WRITE = 3,
	ULP_TX_PKT = 4
};

enum {
	ULP_TX_SC_NOOP = 0x80,
	ULP_TX_SC_IMM  = 0x81,
	ULP_TX_SC_DSGL = 0x82,
	ULP_TX_SC_ISGL = 0x83,
	ULP_TX_SC_PICTRL = 0x84,
	ULP_TX_SC_MEMRD = 0x86
};

#define S_ULPTX_CMD    24
#define M_ULPTX_CMD    0xFF
#define V_ULPTX_CMD(x) ((x) << S_ULPTX_CMD)

#define S_ULPTX_LEN16    0
#define M_ULPTX_LEN16    0xFF
#define V_ULPTX_LEN16(x) ((x) << S_ULPTX_LEN16)

#define S_ULP_TX_SC_MORE 23
#define V_ULP_TX_SC_MORE(x) ((x) << S_ULP_TX_SC_MORE)
#define F_ULP_TX_SC_MORE  V_ULP_TX_SC_MORE(1U)

struct ulptx_sge_pair {
	__be32 len[2];
	__be64 addr[2];
};

struct ulptx_sgl {
	__be32 cmd_nsge;
	__be32 len0;
	__be64 addr0;
#if !(defined C99_NOT_SUPPORTED)
	struct ulptx_sge_pair sge[0];
#endif
};

struct ulptx_isge {
	__be32 stag;
	__be32 len;
	__be64 target_ofst;
};

struct ulptx_isgl {
	__be32 cmd_nisge;
	__be32 rsvd;
#if !(defined C99_NOT_SUPPORTED)
	struct ulptx_isge sge[0];
#endif
};

struct ulptx_idata {
	__be32 cmd_more;
	__be32 len;
};

#define S_ULPTX_NSGE    0
#define M_ULPTX_NSGE    0xFFFF
#define V_ULPTX_NSGE(x) ((x) << S_ULPTX_NSGE)
#define G_ULPTX_NSGE(x) (((x) >> S_ULPTX_NSGE) & M_ULPTX_NSGE)

struct ulptx_sc_memrd {
	__be32 cmd_to_len;
	__be32 addr;
};

struct ulp_mem_io {
	WR_HDR;
	__be32 cmd;
	__be32 len16;             /* command length */
	__be32 dlen;              /* data length in 32-byte units */
	__be32 lock_addr;
};

/* additional ulp_mem_io.cmd fields */
#define S_ULP_MEMIO_ORDER    23
#define V_ULP_MEMIO_ORDER(x) ((x) << S_ULP_MEMIO_ORDER)
#define F_ULP_MEMIO_ORDER    V_ULP_MEMIO_ORDER(1U)

#define S_T5_ULP_MEMIO_IMM    23
#define V_T5_ULP_MEMIO_IMM(x) ((x) << S_T5_ULP_MEMIO_IMM)
#define F_T5_ULP_MEMIO_IMM    V_T5_ULP_MEMIO_IMM(1U)

#define S_T5_ULP_MEMIO_ORDER    22
#define V_T5_ULP_MEMIO_ORDER(x) ((x) << S_T5_ULP_MEMIO_ORDER)
#define F_T5_ULP_MEMIO_ORDER    V_T5_ULP_MEMIO_ORDER(1U)

#define S_T5_ULP_MEMIO_FID	4
#define M_T5_ULP_MEMIO_FID	0x7ff
#define V_T5_ULP_MEMIO_FID(x)	((x) << S_T5_ULP_MEMIO_FID)

/* ulp_mem_io.lock_addr fields */
#define S_ULP_MEMIO_ADDR    0
#define M_ULP_MEMIO_ADDR    0x7FFFFFF
#define V_ULP_MEMIO_ADDR(x) ((x) << S_ULP_MEMIO_ADDR)

#define S_ULP_MEMIO_LOCK    31
#define V_ULP_MEMIO_LOCK(x) ((x) << S_ULP_MEMIO_LOCK)
#define F_ULP_MEMIO_LOCK    V_ULP_MEMIO_LOCK(1U)

/* ulp_mem_io.dlen fields */
#define S_ULP_MEMIO_DATA_LEN    0
#define M_ULP_MEMIO_DATA_LEN    0x1F
#define V_ULP_MEMIO_DATA_LEN(x) ((x) << S_ULP_MEMIO_DATA_LEN)

/* ULP_TXPKT field values */
enum {
	ULP_TXPKT_DEST_TP = 0,
	ULP_TXPKT_DEST_SGE,
	ULP_TXPKT_DEST_UP,
	ULP_TXPKT_DEST_DEVNULL,
};

struct ulp_txpkt {
	__be32 cmd_dest;
	__be32 len;
};

/* ulp_txpkt.cmd_dest fields */
#define S_ULP_TXPKT_DATAMODIFY       23
#define M_ULP_TXPKT_DATAMODIFY       0x1
#define V_ULP_TXPKT_DATAMODIFY(x)    ((x) << S_ULP_TXPKT_DATAMODIFY)
#define G_ULP_TXPKT_DATAMODIFY(x)    \
	(((x) >> S_ULP_TXPKT_DATAMODIFY) & M_ULP_TXPKT_DATAMODIFY_)
#define F_ULP_TXPKT_DATAMODIFY       V_ULP_TXPKT_DATAMODIFY(1U)

#define S_ULP_TXPKT_CHANNELID        22
#define M_ULP_TXPKT_CHANNELID        0x1
#define V_ULP_TXPKT_CHANNELID(x)     ((x) << S_ULP_TXPKT_CHANNELID)
#define G_ULP_TXPKT_CHANNELID(x)     \
	(((x) >> S_ULP_TXPKT_CHANNELID) & M_ULP_TXPKT_CHANNELID)
#define F_ULP_TXPKT_CHANNELID        V_ULP_TXPKT_CHANNELID(1U)

/* ulp_txpkt.cmd_dest fields */
#define S_ULP_TXPKT_DEST    16
#define M_ULP_TXPKT_DEST    0x3
#define V_ULP_TXPKT_DEST(x) ((x) << S_ULP_TXPKT_DEST)

#define S_ULP_TXPKT_FID	    4
#define M_ULP_TXPKT_FID     0x7ff
#define V_ULP_TXPKT_FID(x)  ((x) << S_ULP_TXPKT_FID)

#define S_ULP_TXPKT_RO      3
#define V_ULP_TXPKT_RO(x) ((x) << S_ULP_TXPKT_RO)
#define F_ULP_TXPKT_RO V_ULP_TXPKT_RO(1U)

enum cpl_tx_tnl_lso_type {
	TX_TNL_TYPE_OPAQUE,
	TX_TNL_TYPE_NVGRE,
	TX_TNL_TYPE_VXLAN,
	TX_TNL_TYPE_GENEVE,
};

struct cpl_tx_tnl_lso {
	__be32 op_to_IpIdSplitOut;
	__be16 IpIdOffsetOut;
	__be16 UdpLenSetOut_to_TnlHdrLen;
	__be64 r1;
	__be32 Flow_to_TcpHdrLen;
	__be16 IpIdOffset;
	__be16 IpIdSplit_to_Mss;
	__be32 TCPSeqOffset;
	__be32 EthLenOffset_Size;
	/* encapsulated CPL (TX_PKT_XT) follows here */
};

#define S_CPL_TX_TNL_LSO_OPCODE		24
#define M_CPL_TX_TNL_LSO_OPCODE		0xff
#define V_CPL_TX_TNL_LSO_OPCODE(x)	((x) << S_CPL_TX_TNL_LSO_OPCODE)
#define G_CPL_TX_TNL_LSO_OPCODE(x)	\
    (((x) >> S_CPL_TX_TNL_LSO_OPCODE) & M_CPL_TX_TNL_LSO_OPCODE)

#define S_CPL_TX_TNL_LSO_FIRST		23
#define M_CPL_TX_TNL_LSO_FIRST		0x1
#define V_CPL_TX_TNL_LSO_FIRST(x)	((x) << S_CPL_TX_TNL_LSO_FIRST)
#define G_CPL_TX_TNL_LSO_FIRST(x)	\
    (((x) >> S_CPL_TX_TNL_LSO_FIRST) & M_CPL_TX_TNL_LSO_FIRST)
#define F_CPL_TX_TNL_LSO_FIRST		V_CPL_TX_TNL_LSO_FIRST(1U)

#define S_CPL_TX_TNL_LSO_LAST		22
#define M_CPL_TX_TNL_LSO_LAST		0x1
#define V_CPL_TX_TNL_LSO_LAST(x)	((x) << S_CPL_TX_TNL_LSO_LAST)
#define G_CPL_TX_TNL_LSO_LAST(x)	\
    (((x) >> S_CPL_TX_TNL_LSO_LAST) & M_CPL_TX_TNL_LSO_LAST)
#define F_CPL_TX_TNL_LSO_LAST		V_CPL_TX_TNL_LSO_LAST(1U)

#define S_CPL_TX_TNL_LSO_ETHHDRLENXOUT	21
#define M_CPL_TX_TNL_LSO_ETHHDRLENXOUT	0x1
#define V_CPL_TX_TNL_LSO_ETHHDRLENXOUT(x) \
    ((x) << S_CPL_TX_TNL_LSO_ETHHDRLENXOUT)
#define G_CPL_TX_TNL_LSO_ETHHDRLENXOUT(x) \
    (((x) >> S_CPL_TX_TNL_LSO_ETHHDRLENXOUT) & M_CPL_TX_TNL_LSO_ETHHDRLENXOUT)
#define F_CPL_TX_TNL_LSO_ETHHDRLENXOUT	V_CPL_TX_TNL_LSO_ETHHDRLENXOUT(1U)

#define S_CPL_TX_TNL_LSO_IPV6OUT	20
#define M_CPL_TX_TNL_LSO_IPV6OUT	0x1
#define V_CPL_TX_TNL_LSO_IPV6OUT(x)	((x) << S_CPL_TX_TNL_LSO_IPV6OUT)
#define G_CPL_TX_TNL_LSO_IPV6OUT(x)	\
    (((x) >> S_CPL_TX_TNL_LSO_IPV6OUT) & M_CPL_TX_TNL_LSO_IPV6OUT)
#define F_CPL_TX_TNL_LSO_IPV6OUT	V_CPL_TX_TNL_LSO_IPV6OUT(1U)

#define S_CPL_TX_TNL_LSO_ETHHDRLENOUT	16
#define M_CPL_TX_TNL_LSO_ETHHDRLENOUT	0xf
#define V_CPL_TX_TNL_LSO_ETHHDRLENOUT(x) \
    ((x) << S_CPL_TX_TNL_LSO_ETHHDRLENOUT)
#define G_CPL_TX_TNL_LSO_ETHHDRLENOUT(x) \
    (((x) >> S_CPL_TX_TNL_LSO_ETHHDRLENOUT) & M_CPL_TX_TNL_LSO_ETHHDRLENOUT)

#define S_CPL_TX_TNL_LSO_IPHDRLENOUT	4
#define M_CPL_TX_TNL_LSO_IPHDRLENOUT	0xfff
#define V_CPL_TX_TNL_LSO_IPHDRLENOUT(x)	((x) << S_CPL_TX_TNL_LSO_IPHDRLENOUT)
#define G_CPL_TX_TNL_LSO_IPHDRLENOUT(x)	\
    (((x) >> S_CPL_TX_TNL_LSO_IPHDRLENOUT) & M_CPL_TX_TNL_LSO_IPHDRLENOUT)

#define S_CPL_TX_TNL_LSO_IPHDRCHKOUT	3
#define M_CPL_TX_TNL_LSO_IPHDRCHKOUT	0x1
#define V_CPL_TX_TNL_LSO_IPHDRCHKOUT(x)	((x) << S_CPL_TX_TNL_LSO_IPHDRCHKOUT)
#define G_CPL_TX_TNL_LSO_IPHDRCHKOUT(x)	\
    (((x) >> S_CPL_TX_TNL_LSO_IPHDRCHKOUT) & M_CPL_TX_TNL_LSO_IPHDRCHKOUT)
#define F_CPL_TX_TNL_LSO_IPHDRCHKOUT	V_CPL_TX_TNL_LSO_IPHDRCHKOUT(1U)

#define S_CPL_TX_TNL_LSO_IPLENSETOUT	2
#define M_CPL_TX_TNL_LSO_IPLENSETOUT	0x1
#define V_CPL_TX_TNL_LSO_IPLENSETOUT(x)	((x) << S_CPL_TX_TNL_LSO_IPLENSETOUT)
#define G_CPL_TX_TNL_LSO_IPLENSETOUT(x)	\
    (((x) >> S_CPL_TX_TNL_LSO_IPLENSETOUT) & M_CPL_TX_TNL_LSO_IPLENSETOUT)
#define F_CPL_TX_TNL_LSO_IPLENSETOUT	V_CPL_TX_TNL_LSO_IPLENSETOUT(1U)

#define S_CPL_TX_TNL_LSO_IPIDINCOUT	1
#define M_CPL_TX_TNL_LSO_IPIDINCOUT	0x1
#define V_CPL_TX_TNL_LSO_IPIDINCOUT(x)	((x) << S_CPL_TX_TNL_LSO_IPIDINCOUT)
#define G_CPL_TX_TNL_LSO_IPIDINCOUT(x)	\
    (((x) >> S_CPL_TX_TNL_LSO_IPIDINCOUT) & M_CPL_TX_TNL_LSO_IPIDINCOUT)
#define F_CPL_TX_TNL_LSO_IPIDINCOUT	V_CPL_TX_TNL_LSO_IPIDINCOUT(1U)

#define S_CPL_TX_TNL_LSO_IPIDSPLITOUT	0
#define M_CPL_TX_TNL_LSO_IPIDSPLITOUT	0x1
#define V_CPL_TX_TNL_LSO_IPIDSPLITOUT(x) \
    ((x) << S_CPL_TX_TNL_LSO_IPIDSPLITOUT)
#define G_CPL_TX_TNL_LSO_IPIDSPLITOUT(x) \
    (((x) >> S_CPL_TX_TNL_LSO_IPIDSPLITOUT) & M_CPL_TX_TNL_LSO_IPIDSPLITOUT)
#define F_CPL_TX_TNL_LSO_IPIDSPLITOUT	V_CPL_TX_TNL_LSO_IPIDSPLITOUT(1U)

#define S_CPL_TX_TNL_LSO_UDPLENSETOUT	15
#define M_CPL_TX_TNL_LSO_UDPLENSETOUT	0x1
#define V_CPL_TX_TNL_LSO_UDPLENSETOUT(x) \
    ((x) << S_CPL_TX_TNL_LSO_UDPLENSETOUT)
#define G_CPL_TX_TNL_LSO_UDPLENSETOUT(x) \
    (((x) >> S_CPL_TX_TNL_LSO_UDPLENSETOUT) & M_CPL_TX_TNL_LSO_UDPLENSETOUT)
#define F_CPL_TX_TNL_LSO_UDPLENSETOUT	V_CPL_TX_TNL_LSO_UDPLENSETOUT(1U)

#define S_CPL_TX_TNL_LSO_UDPCHKCLROUT	14
#define M_CPL_TX_TNL_LSO_UDPCHKCLROUT	0x1
#define V_CPL_TX_TNL_LSO_UDPCHKCLROUT(x) \
    ((x) << S_CPL_TX_TNL_LSO_UDPCHKCLROUT)
#define G_CPL_TX_TNL_LSO_UDPCHKCLROUT(x) \
    (((x) >> S_CPL_TX_TNL_LSO_UDPCHKCLROUT) & M_CPL_TX_TNL_LSO_UDPCHKCLROUT)
#define F_CPL_TX_TNL_LSO_UDPCHKCLROUT	V_CPL_TX_TNL_LSO_UDPCHKCLROUT(1U)

#define S_CPL_TX_TNL_LSO_TNLTYPE	12
#define M_CPL_TX_TNL_LSO_TNLTYPE	0x3
#define V_CPL_TX_TNL_LSO_TNLTYPE(x)	((x) << S_CPL_TX_TNL_LSO_TNLTYPE)
#define G_CPL_TX_TNL_LSO_TNLTYPE(x)	\
    (((x) >> S_CPL_TX_TNL_LSO_TNLTYPE) & M_CPL_TX_TNL_LSO_TNLTYPE)

#define S_CPL_TX_TNL_LSO_TNLHDRLEN	0
#define M_CPL_TX_TNL_LSO_TNLHDRLEN	0xfff
#define V_CPL_TX_TNL_LSO_TNLHDRLEN(x)	((x) << S_CPL_TX_TNL_LSO_TNLHDRLEN)
#define G_CPL_TX_TNL_LSO_TNLHDRLEN(x)	\
    (((x) >> S_CPL_TX_TNL_LSO_TNLHDRLEN) & M_CPL_TX_TNL_LSO_TNLHDRLEN)

#define S_CPL_TX_TNL_LSO_FLOW		21
#define M_CPL_TX_TNL_LSO_FLOW		0x1
#define V_CPL_TX_TNL_LSO_FLOW(x)	((x) << S_CPL_TX_TNL_LSO_FLOW)
#define G_CPL_TX_TNL_LSO_FLOW(x)	\
    (((x) >> S_CPL_TX_TNL_LSO_FLOW) & M_CPL_TX_TNL_LSO_FLOW)
#define F_CPL_TX_TNL_LSO_FLOW		V_CPL_TX_TNL_LSO_FLOW(1U)

#define S_CPL_TX_TNL_LSO_IPV6		20
#define M_CPL_TX_TNL_LSO_IPV6		0x1
#define V_CPL_TX_TNL_LSO_IPV6(x)	((x) << S_CPL_TX_TNL_LSO_IPV6)
#define G_CPL_TX_TNL_LSO_IPV6(x)	\
    (((x) >> S_CPL_TX_TNL_LSO_IPV6) & M_CPL_TX_TNL_LSO_IPV6)
#define F_CPL_TX_TNL_LSO_IPV6		V_CPL_TX_TNL_LSO_IPV6(1U)

#define S_CPL_TX_TNL_LSO_ETHHDRLEN	16
#define M_CPL_TX_TNL_LSO_ETHHDRLEN	0xf
#define V_CPL_TX_TNL_LSO_ETHHDRLEN(x)	((x) << S_CPL_TX_TNL_LSO_ETHHDRLEN)
#define G_CPL_TX_TNL_LSO_ETHHDRLEN(x)	\
    (((x) >> S_CPL_TX_TNL_LSO_ETHHDRLEN) & M_CPL_TX_TNL_LSO_ETHHDRLEN)

#define S_CPL_TX_TNL_LSO_IPHDRLEN	4
#define M_CPL_TX_TNL_LSO_IPHDRLEN	0xfff
#define V_CPL_TX_TNL_LSO_IPHDRLEN(x)	((x) << S_CPL_TX_TNL_LSO_IPHDRLEN)
#define G_CPL_TX_TNL_LSO_IPHDRLEN(x)	\
    (((x) >> S_CPL_TX_TNL_LSO_IPHDRLEN) & M_CPL_TX_TNL_LSO_IPHDRLEN)

#define S_CPL_TX_TNL_LSO_TCPHDRLEN	0
#define M_CPL_TX_TNL_LSO_TCPHDRLEN	0xf
#define V_CPL_TX_TNL_LSO_TCPHDRLEN(x)	((x) << S_CPL_TX_TNL_LSO_TCPHDRLEN)
#define G_CPL_TX_TNL_LSO_TCPHDRLEN(x)	\
    (((x) >> S_CPL_TX_TNL_LSO_TCPHDRLEN) & M_CPL_TX_TNL_LSO_TCPHDRLEN)

#define S_CPL_TX_TNL_LSO_IPIDSPLIT	15
#define M_CPL_TX_TNL_LSO_IPIDSPLIT	0x1
#define V_CPL_TX_TNL_LSO_IPIDSPLIT(x)	((x) << S_CPL_TX_TNL_LSO_IPIDSPLIT)
#define G_CPL_TX_TNL_LSO_IPIDSPLIT(x)	\
    (((x) >> S_CPL_TX_TNL_LSO_IPIDSPLIT) & M_CPL_TX_TNL_LSO_IPIDSPLIT)
#define F_CPL_TX_TNL_LSO_IPIDSPLIT	V_CPL_TX_TNL_LSO_IPIDSPLIT(1U)

#define S_CPL_TX_TNL_LSO_ETHHDRLENX	14
#define M_CPL_TX_TNL_LSO_ETHHDRLENX	0x1
#define V_CPL_TX_TNL_LSO_ETHHDRLENX(x)	((x) << S_CPL_TX_TNL_LSO_ETHHDRLENX)
#define G_CPL_TX_TNL_LSO_ETHHDRLENX(x)	\
    (((x) >> S_CPL_TX_TNL_LSO_ETHHDRLENX) & M_CPL_TX_TNL_LSO_ETHHDRLENX)
#define F_CPL_TX_TNL_LSO_ETHHDRLENX	V_CPL_TX_TNL_LSO_ETHHDRLENX(1U)

#define S_CPL_TX_TNL_LSO_MSS		0
#define M_CPL_TX_TNL_LSO_MSS		0x3fff
#define V_CPL_TX_TNL_LSO_MSS(x)		((x) << S_CPL_TX_TNL_LSO_MSS)
#define G_CPL_TX_TNL_LSO_MSS(x)		\
    (((x) >> S_CPL_TX_TNL_LSO_MSS) & M_CPL_TX_TNL_LSO_MSS)

#define S_CPL_TX_TNL_LSO_ETHLENOFFSET	28
#define M_CPL_TX_TNL_LSO_ETHLENOFFSET	0xf
#define V_CPL_TX_TNL_LSO_ETHLENOFFSET(x) \
    ((x) << S_CPL_TX_TNL_LSO_ETHLENOFFSET)
#define G_CPL_TX_TNL_LSO_ETHLENOFFSET(x) \
    (((x) >> S_CPL_TX_TNL_LSO_ETHLENOFFSET) & M_CPL_TX_TNL_LSO_ETHLENOFFSET)

#define S_CPL_TX_TNL_LSO_SIZE		0
#define M_CPL_TX_TNL_LSO_SIZE		0xfffffff
#define V_CPL_TX_TNL_LSO_SIZE(x)	((x) << S_CPL_TX_TNL_LSO_SIZE)
#define G_CPL_TX_TNL_LSO_SIZE(x)	\
    (((x) >> S_CPL_TX_TNL_LSO_SIZE) & M_CPL_TX_TNL_LSO_SIZE)

struct cpl_rx_mps_pkt {
	__be32 op_to_r1_hi;
	__be32 r1_lo_length;
};

#define S_CPL_RX_MPS_PKT_OP     24
#define M_CPL_RX_MPS_PKT_OP     0xff
#define V_CPL_RX_MPS_PKT_OP(x)  ((x) << S_CPL_RX_MPS_PKT_OP)
#define G_CPL_RX_MPS_PKT_OP(x)  \
	(((x) >> S_CPL_RX_MPS_PKT_OP) & M_CPL_RX_MPS_PKT_OP)

#define S_CPL_RX_MPS_PKT_TYPE           20
#define M_CPL_RX_MPS_PKT_TYPE           0xf
#define V_CPL_RX_MPS_PKT_TYPE(x)        ((x) << S_CPL_RX_MPS_PKT_TYPE)
#define G_CPL_RX_MPS_PKT_TYPE(x)        \
	(((x) >> S_CPL_RX_MPS_PKT_TYPE) & M_CPL_RX_MPS_PKT_TYPE)

/*
 * Values for CPL_RX_MPS_PKT_TYPE, a bit-wise orthogonal field.
 */
#define X_CPL_RX_MPS_PKT_TYPE_PAUSE	(1 << 0)
#define X_CPL_RX_MPS_PKT_TYPE_PPP	(1 << 1)
#define X_CPL_RX_MPS_PKT_TYPE_QFC	(1 << 2)
#define X_CPL_RX_MPS_PKT_TYPE_PTP	(1 << 3)

struct cpl_tx_tls_sfo {
	__be32 op_to_seg_len;
	__be32 pld_len;
	__be32 type_protover;
	__be32 r1_lo;
	__be32 seqno_numivs;
	__be32 ivgen_hdrlen;
	__be64 scmd1;
};

/* cpl_tx_tls_sfo macros */
#define S_CPL_TX_TLS_SFO_OPCODE         24
#define M_CPL_TX_TLS_SFO_OPCODE         0xff
#define V_CPL_TX_TLS_SFO_OPCODE(x)      ((x) << S_CPL_TX_TLS_SFO_OPCODE)
#define G_CPL_TX_TLS_SFO_OPCODE(x)      \
	(((x) >> S_CPL_TX_TLS_SFO_OPCODE) & M_CPL_TX_TLS_SFO_OPCODE)

#define S_CPL_TX_TLS_SFO_DATA_TYPE      20
#define M_CPL_TX_TLS_SFO_DATA_TYPE      0xf
#define V_CPL_TX_TLS_SFO_DATA_TYPE(x)   ((x) << S_CPL_TX_TLS_SFO_DATA_TYPE)
#define G_CPL_TX_TLS_SFO_DATA_TYPE(x)   \
	(((x) >> S_CPL_TX_TLS_SFO_DATA_TYPE) & M_CPL_TX_TLS_SFO_DATA_TYPE)

#define S_CPL_TX_TLS_SFO_CPL_LEN        16
#define M_CPL_TX_TLS_SFO_CPL_LEN        0xf
#define V_CPL_TX_TLS_SFO_CPL_LEN(x)     ((x) << S_CPL_TX_TLS_SFO_CPL_LEN)
#define G_CPL_TX_TLS_SFO_CPL_LEN(x)     \
	(((x) >> S_CPL_TX_TLS_SFO_CPL_LEN) & M_CPL_TX_TLS_SFO_CPL_LEN)

#define S_CPL_TX_TLS_SFO_SEG_LEN        0
#define M_CPL_TX_TLS_SFO_SEG_LEN        0xffff
#define V_CPL_TX_TLS_SFO_SEG_LEN(x)     ((x) << S_CPL_TX_TLS_SFO_SEG_LEN)
#define G_CPL_TX_TLS_SFO_SEG_LEN(x)     \
	(((x) >> S_CPL_TX_TLS_SFO_SEG_LEN) & M_CPL_TX_TLS_SFO_SEG_LEN)

#define S_CPL_TX_TLS_SFO_TYPE           24
#define M_CPL_TX_TLS_SFO_TYPE           0xff
#define V_CPL_TX_TLS_SFO_TYPE(x)        ((x) << S_CPL_TX_TLS_SFO_TYPE)
#define G_CPL_TX_TLS_SFO_TYPE(x)        \
    (((x) >> S_CPL_TX_TLS_SFO_TYPE) & M_CPL_TX_TLS_SFO_TYPE)

#define S_CPL_TX_TLS_SFO_PROTOVER       8
#define M_CPL_TX_TLS_SFO_PROTOVER       0xffff
#define V_CPL_TX_TLS_SFO_PROTOVER(x)    ((x) << S_CPL_TX_TLS_SFO_PROTOVER)
#define G_CPL_TX_TLS_SFO_PROTOVER(x)    \
    (((x) >> S_CPL_TX_TLS_SFO_PROTOVER) & M_CPL_TX_TLS_SFO_PROTOVER)

struct cpl_tls_data {
	RSS_HDR
	union opcode_tid ot;
	__be32 length_pkd;
	__be32 seq;
	__be32 r1;
};

#define S_CPL_TLS_DATA_OPCODE           24
#define M_CPL_TLS_DATA_OPCODE           0xff
#define V_CPL_TLS_DATA_OPCODE(x)        ((x) << S_CPL_TLS_DATA_OPCODE)
#define G_CPL_TLS_DATA_OPCODE(x)        \
	(((x) >> S_CPL_TLS_DATA_OPCODE) & M_CPL_TLS_DATA_OPCODE)

#define S_CPL_TLS_DATA_TID              0
#define M_CPL_TLS_DATA_TID              0xffffff
#define V_CPL_TLS_DATA_TID(x)           ((x) << S_CPL_TLS_DATA_TID)
#define G_CPL_TLS_DATA_TID(x)           \
	(((x) >> S_CPL_TLS_DATA_TID) & M_CPL_TLS_DATA_TID)

#define S_CPL_TLS_DATA_LENGTH           0
#define M_CPL_TLS_DATA_LENGTH           0xffff
#define V_CPL_TLS_DATA_LENGTH(x)        ((x) << S_CPL_TLS_DATA_LENGTH)
#define G_CPL_TLS_DATA_LENGTH(x)        \
	(((x) >> S_CPL_TLS_DATA_LENGTH) & M_CPL_TLS_DATA_LENGTH)

struct cpl_rx_tls_cmp {
	RSS_HDR
	union opcode_tid ot;
	__be32 pdulength_length;
	__be32 seq;
	__be32 ddp_report;
	__be32 r;
	__be32 ddp_valid;
};

#define S_CPL_RX_TLS_CMP_OPCODE         24
#define M_CPL_RX_TLS_CMP_OPCODE         0xff
#define V_CPL_RX_TLS_CMP_OPCODE(x)      ((x) << S_CPL_RX_TLS_CMP_OPCODE)
#define G_CPL_RX_TLS_CMP_OPCODE(x)      \
	(((x) >> S_CPL_RX_TLS_CMP_OPCODE) & M_CPL_RX_TLS_CMP_OPCODE)

#define S_CPL_RX_TLS_CMP_TID            0
#define M_CPL_RX_TLS_CMP_TID            0xffffff
#define V_CPL_RX_TLS_CMP_TID(x)         ((x) << S_CPL_RX_TLS_CMP_TID)
#define G_CPL_RX_TLS_CMP_TID(x)         \
	(((x) >> S_CPL_RX_TLS_CMP_TID) & M_CPL_RX_TLS_CMP_TID)

#define S_CPL_RX_TLS_CMP_PDULENGTH      16
#define M_CPL_RX_TLS_CMP_PDULENGTH      0xffff
#define V_CPL_RX_TLS_CMP_PDULENGTH(x)   ((x) << S_CPL_RX_TLS_CMP_PDULENGTH)
#define G_CPL_RX_TLS_CMP_PDULENGTH(x)   \
	(((x) >> S_CPL_RX_TLS_CMP_PDULENGTH) & M_CPL_RX_TLS_CMP_PDULENGTH)

#define S_CPL_RX_TLS_CMP_LENGTH         0
#define M_CPL_RX_TLS_CMP_LENGTH         0xffff
#define V_CPL_RX_TLS_CMP_LENGTH(x)      ((x) << S_CPL_RX_TLS_CMP_LENGTH)
#define G_CPL_RX_TLS_CMP_LENGTH(x)      \
	(((x) >> S_CPL_RX_TLS_CMP_LENGTH) & M_CPL_RX_TLS_CMP_LENGTH)

#define S_SCMD_SEQ_NO_CTRL      29
#define M_SCMD_SEQ_NO_CTRL      0x3
#define V_SCMD_SEQ_NO_CTRL(x)   ((x) << S_SCMD_SEQ_NO_CTRL)
#define G_SCMD_SEQ_NO_CTRL(x)   \
	(((x) >> S_SCMD_SEQ_NO_CTRL) & M_SCMD_SEQ_NO_CTRL)

/* StsFieldPrsnt- Status field at the end of the TLS PDU */
#define S_SCMD_STATUS_PRESENT   28
#define M_SCMD_STATUS_PRESENT   0x1
#define V_SCMD_STATUS_PRESENT(x)    ((x) << S_SCMD_STATUS_PRESENT)
#define G_SCMD_STATUS_PRESENT(x)    \
	(((x) >> S_SCMD_STATUS_PRESENT) & M_SCMD_STATUS_PRESENT)
#define F_SCMD_STATUS_PRESENT   V_SCMD_STATUS_PRESENT(1U)

/* ProtoVersion - Protocol Version 0: 1.2, 1:1.1, 2:DTLS, 3:Generic,
 * 3-15: Reserved. */
#define S_SCMD_PROTO_VERSION    24
#define M_SCMD_PROTO_VERSION    0xf
#define V_SCMD_PROTO_VERSION(x) ((x) << S_SCMD_PROTO_VERSION)
#define G_SCMD_PROTO_VERSION(x) \
	(((x) >> S_SCMD_PROTO_VERSION) & M_SCMD_PROTO_VERSION)

/* EncDecCtrl - Encryption/Decryption Control. 0: Encrypt, 1: Decrypt */
#define S_SCMD_ENC_DEC_CTRL     23
#define M_SCMD_ENC_DEC_CTRL     0x1
#define V_SCMD_ENC_DEC_CTRL(x)  ((x) << S_SCMD_ENC_DEC_CTRL)
#define G_SCMD_ENC_DEC_CTRL(x)  \
	(((x) >> S_SCMD_ENC_DEC_CTRL) & M_SCMD_ENC_DEC_CTRL)
#define F_SCMD_ENC_DEC_CTRL V_SCMD_ENC_DEC_CTRL(1U)

/* CipherAuthSeqCtrl - Cipher Authentication Sequence Control. */
#define S_SCMD_CIPH_AUTH_SEQ_CTRL       22
#define M_SCMD_CIPH_AUTH_SEQ_CTRL       0x1
#define V_SCMD_CIPH_AUTH_SEQ_CTRL(x)    \
	((x) << S_SCMD_CIPH_AUTH_SEQ_CTRL)
#define G_SCMD_CIPH_AUTH_SEQ_CTRL(x)    \
	(((x) >> S_SCMD_CIPH_AUTH_SEQ_CTRL) & M_SCMD_CIPH_AUTH_SEQ_CTRL)
#define F_SCMD_CIPH_AUTH_SEQ_CTRL   V_SCMD_CIPH_AUTH_SEQ_CTRL(1U)

/* CiphMode -  Cipher Mode. 0: NOP, 1:AES-CBC, 2:AES-GCM, 3:AES-CTR,
 * 4:Generic-AES, 5-15: Reserved. */
#define S_SCMD_CIPH_MODE    18
#define M_SCMD_CIPH_MODE    0xf
#define V_SCMD_CIPH_MODE(x) ((x) << S_SCMD_CIPH_MODE)
#define G_SCMD_CIPH_MODE(x) \
	(((x) >> S_SCMD_CIPH_MODE) & M_SCMD_CIPH_MODE)

/* AuthMode - Auth Mode. 0: NOP, 1:SHA1, 2:SHA2-224, 3:SHA2-256
 * 4-15: Reserved */
#define S_SCMD_AUTH_MODE    14
#define M_SCMD_AUTH_MODE    0xf
#define V_SCMD_AUTH_MODE(x) ((x) << S_SCMD_AUTH_MODE)
#define G_SCMD_AUTH_MODE(x) \
	(((x) >> S_SCMD_AUTH_MODE) & M_SCMD_AUTH_MODE)

/* HmacCtrl - HMAC Control. 0:NOP, 1:No truncation, 2:Support HMAC Truncation
 * per RFC 4366, 3:IPSec 96 bits, 4-7:Reserved
 */
#define S_SCMD_HMAC_CTRL    11
#define M_SCMD_HMAC_CTRL    0x7
#define V_SCMD_HMAC_CTRL(x) ((x) << S_SCMD_HMAC_CTRL)
#define G_SCMD_HMAC_CTRL(x) \
	(((x) >> S_SCMD_HMAC_CTRL) & M_SCMD_HMAC_CTRL)

/* IvSize - IV size in units of 2 bytes */
#define S_SCMD_IV_SIZE  7
#define M_SCMD_IV_SIZE  0xf
#define V_SCMD_IV_SIZE(x)   ((x) << S_SCMD_IV_SIZE)
#define G_SCMD_IV_SIZE(x)   \
	(((x) >> S_SCMD_IV_SIZE) & M_SCMD_IV_SIZE)

/* NumIVs - Number of IVs */
#define S_SCMD_NUM_IVS  0
#define M_SCMD_NUM_IVS  0x7f
#define V_SCMD_NUM_IVS(x)   ((x) << S_SCMD_NUM_IVS)
#define G_SCMD_NUM_IVS(x)   \
	(((x) >> S_SCMD_NUM_IVS) & M_SCMD_NUM_IVS)

/* EnbDbgId - If this is enabled upper 20 (63:44) bits if SeqNumber
 * (below) are used as Cid (connection id for debug status), these
 * bits are padded to zero for forming the 64 bit
 * sequence number for TLS
 */
#define S_SCMD_ENB_DBGID  31
#define M_SCMD_ENB_DBGID  0x1
#define V_SCMD_ENB_DBGID(x)   ((x) << S_SCMD_ENB_DBGID)
#define G_SCMD_ENB_DBGID(x)   \
	(((x) >> S_SCMD_ENB_DBGID) & M_SCMD_ENB_DBGID)

/* IV generation in SW. */
#define S_SCMD_IV_GEN_CTRL      30
#define M_SCMD_IV_GEN_CTRL      0x1
#define V_SCMD_IV_GEN_CTRL(x)   ((x) << S_SCMD_IV_GEN_CTRL)
#define G_SCMD_IV_GEN_CTRL(x)   \
	(((x) >> S_SCMD_IV_GEN_CTRL) & M_SCMD_IV_GEN_CTRL)
#define F_SCMD_IV_GEN_CTRL  V_SCMD_IV_GEN_CTRL(1U)

/* More frags */
#define S_SCMD_MORE_FRAGS   20
#define M_SCMD_MORE_FRAGS   0x1
#define V_SCMD_MORE_FRAGS(x)    ((x) << S_SCMD_MORE_FRAGS)
#define G_SCMD_MORE_FRAGS(x)    (((x) >> S_SCMD_MORE_FRAGS) & M_SCMD_MORE_FRAGS)

/*last frag */
#define S_SCMD_LAST_FRAG    19
#define M_SCMD_LAST_FRAG    0x1
#define V_SCMD_LAST_FRAG(x) ((x) << S_SCMD_LAST_FRAG)
#define G_SCMD_LAST_FRAG(x) (((x) >> S_SCMD_LAST_FRAG) & M_SCMD_LAST_FRAG)

/* TlsCompPdu */
#define S_SCMD_TLS_COMPPDU    18
#define M_SCMD_TLS_COMPPDU    0x1
#define V_SCMD_TLS_COMPPDU(x) ((x) << S_SCMD_TLS_COMPPDU)
#define G_SCMD_TLS_COMPPDU(x) (((x) >> S_SCMD_TLS_COMPPDU) & M_SCMD_TLS_COMPPDU)

/* KeyCntxtInline - Key context inline after the scmd  OR PayloadOnly*/
#define S_SCMD_KEY_CTX_INLINE   17
#define M_SCMD_KEY_CTX_INLINE   0x1
#define V_SCMD_KEY_CTX_INLINE(x)    ((x) << S_SCMD_KEY_CTX_INLINE)
#define G_SCMD_KEY_CTX_INLINE(x)    \
	(((x) >> S_SCMD_KEY_CTX_INLINE) & M_SCMD_KEY_CTX_INLINE)
#define F_SCMD_KEY_CTX_INLINE   V_SCMD_KEY_CTX_INLINE(1U)

/* TLSFragEnable - 0: Host created TLS PDUs, 1: TLS Framgmentation in ASIC */
#define S_SCMD_TLS_FRAG_ENABLE  16
#define M_SCMD_TLS_FRAG_ENABLE  0x1
#define V_SCMD_TLS_FRAG_ENABLE(x)   ((x) << S_SCMD_TLS_FRAG_ENABLE)
#define G_SCMD_TLS_FRAG_ENABLE(x)   \
	(((x) >> S_SCMD_TLS_FRAG_ENABLE) & M_SCMD_TLS_FRAG_ENABLE)
#define F_SCMD_TLS_FRAG_ENABLE  V_SCMD_TLS_FRAG_ENABLE(1U)

/* MacOnly - Only send the MAC and discard PDU. This is valid for hash only
 * modes, in this case TLS_TX  will drop the PDU and only
 * send back the MAC bytes. */
#define S_SCMD_MAC_ONLY 15
#define M_SCMD_MAC_ONLY 0x1
#define V_SCMD_MAC_ONLY(x)  ((x) << S_SCMD_MAC_ONLY)
#define G_SCMD_MAC_ONLY(x)  \
	(((x) >> S_SCMD_MAC_ONLY) & M_SCMD_MAC_ONLY)
#define F_SCMD_MAC_ONLY V_SCMD_MAC_ONLY(1U)

/* AadIVDrop - Drop the AAD and IV fields. Useful in protocols
 * which have complex AAD and IV formations Eg:AES-CCM
 */
#define S_SCMD_AADIVDROP 14
#define M_SCMD_AADIVDROP 0x1
#define V_SCMD_AADIVDROP(x)  ((x) << S_SCMD_AADIVDROP)
#define G_SCMD_AADIVDROP(x)  \
	(((x) >> S_SCMD_AADIVDROP) & M_SCMD_AADIVDROP)
#define F_SCMD_AADIVDROP V_SCMD_AADIVDROP(1U)

/* HdrLength - Length of all headers excluding TLS header
 * present before start of crypto PDU/payload. */
#define S_SCMD_HDR_LEN  0
#define M_SCMD_HDR_LEN  0x3fff
#define V_SCMD_HDR_LEN(x)   ((x) << S_SCMD_HDR_LEN)
#define G_SCMD_HDR_LEN(x)   \
	(((x) >> S_SCMD_HDR_LEN) & M_SCMD_HDR_LEN)

struct cpl_tx_sec_pdu {
	__be32 op_ivinsrtofst;
	__be32 pldlen;
	__be32 aadstart_cipherstop_hi;
	__be32 cipherstop_lo_authinsert;
	__be32 seqno_numivs;
	__be32 ivgen_hdrlen;
	__be64 scmd1;
};

#define S_CPL_TX_SEC_PDU_OPCODE     24
#define M_CPL_TX_SEC_PDU_OPCODE     0xff
#define V_CPL_TX_SEC_PDU_OPCODE(x)  ((x) << S_CPL_TX_SEC_PDU_OPCODE)
#define G_CPL_TX_SEC_PDU_OPCODE(x)  \
	(((x) >> S_CPL_TX_SEC_PDU_OPCODE) & M_CPL_TX_SEC_PDU_OPCODE)

/* RX Channel Id */
#define S_CPL_TX_SEC_PDU_RXCHID  22
#define M_CPL_TX_SEC_PDU_RXCHID  0x1
#define V_CPL_TX_SEC_PDU_RXCHID(x)   ((x) << S_CPL_TX_SEC_PDU_RXCHID)
#define G_CPL_TX_SEC_PDU_RXCHID(x)   \
(((x) >> S_CPL_TX_SEC_PDU_RXCHID) & M_CPL_TX_SEC_PDU_RXCHID)
#define F_CPL_TX_SEC_PDU_RXCHID  V_CPL_TX_SEC_PDU_RXCHID(1U)

/* Ack Follows */
#define S_CPL_TX_SEC_PDU_ACKFOLLOWS  21
#define M_CPL_TX_SEC_PDU_ACKFOLLOWS  0x1
#define V_CPL_TX_SEC_PDU_ACKFOLLOWS(x)   ((x) << S_CPL_TX_SEC_PDU_ACKFOLLOWS)
#define G_CPL_TX_SEC_PDU_ACKFOLLOWS(x)   \
(((x) >> S_CPL_TX_SEC_PDU_ACKFOLLOWS) & M_CPL_TX_SEC_PDU_ACKFOLLOWS)
#define F_CPL_TX_SEC_PDU_ACKFOLLOWS  V_CPL_TX_SEC_PDU_ACKFOLLOWS(1U)

/* Loopback bit in cpl_tx_sec_pdu */
#define S_CPL_TX_SEC_PDU_ULPTXLPBK  20
#define M_CPL_TX_SEC_PDU_ULPTXLPBK  0x1
#define V_CPL_TX_SEC_PDU_ULPTXLPBK(x)   ((x) << S_CPL_TX_SEC_PDU_ULPTXLPBK)
#define G_CPL_TX_SEC_PDU_ULPTXLPBK(x)   \
(((x) >> S_CPL_TX_SEC_PDU_ULPTXLPBK) & M_CPL_TX_SEC_PDU_ULPTXLPBK)
#define F_CPL_TX_SEC_PDU_ULPTXLPBK  V_CPL_TX_SEC_PDU_ULPTXLPBK(1U)

/* Length of cpl header encapsulated */
#define S_CPL_TX_SEC_PDU_CPLLEN     16
#define M_CPL_TX_SEC_PDU_CPLLEN     0xf
#define V_CPL_TX_SEC_PDU_CPLLEN(x)  ((x) << S_CPL_TX_SEC_PDU_CPLLEN)
#define G_CPL_TX_SEC_PDU_CPLLEN(x)  \
	(((x) >> S_CPL_TX_SEC_PDU_CPLLEN) & M_CPL_TX_SEC_PDU_CPLLEN)

/* PlaceHolder */
#define S_CPL_TX_SEC_PDU_PLACEHOLDER    10
#define M_CPL_TX_SEC_PDU_PLACEHOLDER    0x1
#define V_CPL_TX_SEC_PDU_PLACEHOLDER(x) ((x) << S_CPL_TX_SEC_PDU_PLACEHOLDER)
#define G_CPL_TX_SEC_PDU_PLACEHOLDER(x) \
	(((x) >> S_CPL_TX_SEC_PDU_PLACEHOLDER) & \
	 M_CPL_TX_SEC_PDU_PLACEHOLDER)

/* IvInsrtOffset: Insertion location for IV */
#define S_CPL_TX_SEC_PDU_IVINSRTOFST    0
#define M_CPL_TX_SEC_PDU_IVINSRTOFST    0x3ff
#define V_CPL_TX_SEC_PDU_IVINSRTOFST(x) ((x) << S_CPL_TX_SEC_PDU_IVINSRTOFST)
#define G_CPL_TX_SEC_PDU_IVINSRTOFST(x) \
	(((x) >> S_CPL_TX_SEC_PDU_IVINSRTOFST) & \
	 M_CPL_TX_SEC_PDU_IVINSRTOFST)

/* AadStartOffset: Offset in bytes for AAD start from
 * the first byte following
 * the pkt headers (0-255
 *  bytes) */
#define S_CPL_TX_SEC_PDU_AADSTART   24
#define M_CPL_TX_SEC_PDU_AADSTART   0xff
#define V_CPL_TX_SEC_PDU_AADSTART(x)    ((x) << S_CPL_TX_SEC_PDU_AADSTART)
#define G_CPL_TX_SEC_PDU_AADSTART(x)    \
	(((x) >> S_CPL_TX_SEC_PDU_AADSTART) & \
	 M_CPL_TX_SEC_PDU_AADSTART)

/* AadStopOffset: offset in bytes for AAD stop/end from the first byte following
 * the pkt headers (0-511 bytes) */
#define S_CPL_TX_SEC_PDU_AADSTOP    15
#define M_CPL_TX_SEC_PDU_AADSTOP    0x1ff
#define V_CPL_TX_SEC_PDU_AADSTOP(x) ((x) << S_CPL_TX_SEC_PDU_AADSTOP)
#define G_CPL_TX_SEC_PDU_AADSTOP(x) \
	(((x) >> S_CPL_TX_SEC_PDU_AADSTOP) & M_CPL_TX_SEC_PDU_AADSTOP)

/* CipherStartOffset: offset in bytes for encryption/decryption start from the
 * first byte following the pkt headers (0-1023
 *  bytes) */
#define S_CPL_TX_SEC_PDU_CIPHERSTART    5
#define M_CPL_TX_SEC_PDU_CIPHERSTART    0x3ff
#define V_CPL_TX_SEC_PDU_CIPHERSTART(x) ((x) << S_CPL_TX_SEC_PDU_CIPHERSTART)
#define G_CPL_TX_SEC_PDU_CIPHERSTART(x) \
	(((x) >> S_CPL_TX_SEC_PDU_CIPHERSTART) & \
	 M_CPL_TX_SEC_PDU_CIPHERSTART)

/* CipherStopOffset: offset in bytes for encryption/decryption end
 * from end of the payload of this command (0-511 bytes) */
#define S_CPL_TX_SEC_PDU_CIPHERSTOP_HI      0
#define M_CPL_TX_SEC_PDU_CIPHERSTOP_HI      0x1f
#define V_CPL_TX_SEC_PDU_CIPHERSTOP_HI(x)   \
	((x) << S_CPL_TX_SEC_PDU_CIPHERSTOP_HI)
#define G_CPL_TX_SEC_PDU_CIPHERSTOP_HI(x)   \
	(((x) >> S_CPL_TX_SEC_PDU_CIPHERSTOP_HI) & \
	 M_CPL_TX_SEC_PDU_CIPHERSTOP_HI)

#define S_CPL_TX_SEC_PDU_CIPHERSTOP_LO      28
#define M_CPL_TX_SEC_PDU_CIPHERSTOP_LO      0xf
#define V_CPL_TX_SEC_PDU_CIPHERSTOP_LO(x)   \
	((x) << S_CPL_TX_SEC_PDU_CIPHERSTOP_LO)
#define G_CPL_TX_SEC_PDU_CIPHERSTOP_LO(x)   \
	(((x) >> S_CPL_TX_SEC_PDU_CIPHERSTOP_LO) & \
	 M_CPL_TX_SEC_PDU_CIPHERSTOP_LO)

/* AuthStartOffset: offset in bytes for authentication start from
 * the first byte following the pkt headers (0-1023)
 *  */
#define S_CPL_TX_SEC_PDU_AUTHSTART  18
#define M_CPL_TX_SEC_PDU_AUTHSTART  0x3ff
#define V_CPL_TX_SEC_PDU_AUTHSTART(x)   ((x) << S_CPL_TX_SEC_PDU_AUTHSTART)
#define G_CPL_TX_SEC_PDU_AUTHSTART(x)   \
	(((x) >> S_CPL_TX_SEC_PDU_AUTHSTART) & \
	 M_CPL_TX_SEC_PDU_AUTHSTART)

/* AuthStopOffset: offset in bytes for authentication
 * end from end of the payload of this command (0-511 Bytes) */
#define S_CPL_TX_SEC_PDU_AUTHSTOP   9
#define M_CPL_TX_SEC_PDU_AUTHSTOP   0x1ff
#define V_CPL_TX_SEC_PDU_AUTHSTOP(x)    ((x) << S_CPL_TX_SEC_PDU_AUTHSTOP)
#define G_CPL_TX_SEC_PDU_AUTHSTOP(x)    \
	(((x) >> S_CPL_TX_SEC_PDU_AUTHSTOP) & \
	 M_CPL_TX_SEC_PDU_AUTHSTOP)

/* AuthInsrtOffset: offset in bytes for authentication insertion
 * from end of the payload of this command (0-511 bytes) */
#define S_CPL_TX_SEC_PDU_AUTHINSERT 0
#define M_CPL_TX_SEC_PDU_AUTHINSERT 0x1ff
#define V_CPL_TX_SEC_PDU_AUTHINSERT(x)  ((x) << S_CPL_TX_SEC_PDU_AUTHINSERT)
#define G_CPL_TX_SEC_PDU_AUTHINSERT(x)  \
	(((x) >> S_CPL_TX_SEC_PDU_AUTHINSERT) & \
	 M_CPL_TX_SEC_PDU_AUTHINSERT)

struct cpl_rx_phys_dsgl {
	__be32 op_to_tid;
	__be32 pcirlxorder_to_noofsgentr;
	struct rss_header rss_hdr_int;
};

#define S_CPL_RX_PHYS_DSGL_OPCODE       24
#define M_CPL_RX_PHYS_DSGL_OPCODE       0xff
#define V_CPL_RX_PHYS_DSGL_OPCODE(x)    ((x) << S_CPL_RX_PHYS_DSGL_OPCODE)
#define G_CPL_RX_PHYS_DSGL_OPCODE(x)    \
	    (((x) >> S_CPL_RX_PHYS_DSGL_OPCODE) & M_CPL_RX_PHYS_DSGL_OPCODE)

#define S_CPL_RX_PHYS_DSGL_ISRDMA       23
#define M_CPL_RX_PHYS_DSGL_ISRDMA       0x1
#define V_CPL_RX_PHYS_DSGL_ISRDMA(x)    ((x) << S_CPL_RX_PHYS_DSGL_ISRDMA)
#define G_CPL_RX_PHYS_DSGL_ISRDMA(x)    \
	    (((x) >> S_CPL_RX_PHYS_DSGL_ISRDMA) & M_CPL_RX_PHYS_DSGL_ISRDMA)
#define F_CPL_RX_PHYS_DSGL_ISRDMA       V_CPL_RX_PHYS_DSGL_ISRDMA(1U)

#define S_CPL_RX_PHYS_DSGL_RSVD1        20
#define M_CPL_RX_PHYS_DSGL_RSVD1        0x7
#define V_CPL_RX_PHYS_DSGL_RSVD1(x)     ((x) << S_CPL_RX_PHYS_DSGL_RSVD1)
#define G_CPL_RX_PHYS_DSGL_RSVD1(x)     \
	    (((x) >> S_CPL_RX_PHYS_DSGL_RSVD1) & M_CPL_RX_PHYS_DSGL_RSVD1)

#define S_CPL_RX_PHYS_DSGL_PCIRLXORDER          31
#define M_CPL_RX_PHYS_DSGL_PCIRLXORDER          0x1
#define V_CPL_RX_PHYS_DSGL_PCIRLXORDER(x)       \
	((x) << S_CPL_RX_PHYS_DSGL_PCIRLXORDER)
#define G_CPL_RX_PHYS_DSGL_PCIRLXORDER(x)       \
	(((x) >> S_CPL_RX_PHYS_DSGL_PCIRLXORDER) & \
	 M_CPL_RX_PHYS_DSGL_PCIRLXORDER)
#define F_CPL_RX_PHYS_DSGL_PCIRLXORDER  V_CPL_RX_PHYS_DSGL_PCIRLXORDER(1U)

#define S_CPL_RX_PHYS_DSGL_PCINOSNOOP           30
#define M_CPL_RX_PHYS_DSGL_PCINOSNOOP           0x1
#define V_CPL_RX_PHYS_DSGL_PCINOSNOOP(x)        \
	((x) << S_CPL_RX_PHYS_DSGL_PCINOSNOOP)
#define G_CPL_RX_PHYS_DSGL_PCINOSNOOP(x)        \
	(((x) >> S_CPL_RX_PHYS_DSGL_PCINOSNOOP) & \
	 M_CPL_RX_PHYS_DSGL_PCINOSNOOP)
#define F_CPL_RX_PHYS_DSGL_PCINOSNOOP   V_CPL_RX_PHYS_DSGL_PCINOSNOOP(1U)

#define S_CPL_RX_PHYS_DSGL_PCITPHNTENB          29
#define M_CPL_RX_PHYS_DSGL_PCITPHNTENB          0x1
#define V_CPL_RX_PHYS_DSGL_PCITPHNTENB(x)       \
	((x) << S_CPL_RX_PHYS_DSGL_PCITPHNTENB)
#define G_CPL_RX_PHYS_DSGL_PCITPHNTENB(x)       \
	(((x) >> S_CPL_RX_PHYS_DSGL_PCITPHNTENB) & \
	 M_CPL_RX_PHYS_DSGL_PCITPHNTENB)
#define F_CPL_RX_PHYS_DSGL_PCITPHNTENB  V_CPL_RX_PHYS_DSGL_PCITPHNTENB(1U)

#define S_CPL_RX_PHYS_DSGL_PCITPHNT     27
#define M_CPL_RX_PHYS_DSGL_PCITPHNT     0x3
#define V_CPL_RX_PHYS_DSGL_PCITPHNT(x)  ((x) << S_CPL_RX_PHYS_DSGL_PCITPHNT)
#define G_CPL_RX_PHYS_DSGL_PCITPHNT(x)  \
	(((x) >> S_CPL_RX_PHYS_DSGL_PCITPHNT) & \
	M_CPL_RX_PHYS_DSGL_PCITPHNT)

#define S_CPL_RX_PHYS_DSGL_DCAID        16
#define M_CPL_RX_PHYS_DSGL_DCAID        0x7ff
#define V_CPL_RX_PHYS_DSGL_DCAID(x)     ((x) << S_CPL_RX_PHYS_DSGL_DCAID)
#define G_CPL_RX_PHYS_DSGL_DCAID(x)     \
	(((x) >> S_CPL_RX_PHYS_DSGL_DCAID) & \
	 M_CPL_RX_PHYS_DSGL_DCAID)

#define S_CPL_RX_PHYS_DSGL_NOOFSGENTR           0
#define M_CPL_RX_PHYS_DSGL_NOOFSGENTR           0xffff
#define V_CPL_RX_PHYS_DSGL_NOOFSGENTR(x)        \
	((x) << S_CPL_RX_PHYS_DSGL_NOOFSGENTR)
#define G_CPL_RX_PHYS_DSGL_NOOFSGENTR(x)        \
	(((x) >> S_CPL_RX_PHYS_DSGL_NOOFSGENTR) & \
	 M_CPL_RX_PHYS_DSGL_NOOFSGENTR)

/* CPL_TX_TLS_ACK */
struct cpl_tx_tls_ack {
        __be32 op_to_Rsvd2;
        __be32 PldLen;
        __be64 Rsvd3;
};

#define S_CPL_TX_TLS_ACK_OPCODE         24
#define M_CPL_TX_TLS_ACK_OPCODE         0xff
#define V_CPL_TX_TLS_ACK_OPCODE(x)      ((x) << S_CPL_TX_TLS_ACK_OPCODE)
#define G_CPL_TX_TLS_ACK_OPCODE(x)      \
    (((x) >> S_CPL_TX_TLS_ACK_OPCODE) & M_CPL_TX_TLS_ACK_OPCODE)

#define S_CPL_TX_TLS_ACK_RSVD1          23
#define M_CPL_TX_TLS_ACK_RSVD1          0x1
#define V_CPL_TX_TLS_ACK_RSVD1(x)       ((x) << S_CPL_TX_TLS_ACK_RSVD1)
#define G_CPL_TX_TLS_ACK_RSVD1(x)       \
    (((x) >> S_CPL_TX_TLS_ACK_RSVD1) & M_CPL_TX_TLS_ACK_RSVD1)
#define F_CPL_TX_TLS_ACK_RSVD1  V_CPL_TX_TLS_ACK_RSVD1(1U)

#define S_CPL_TX_TLS_ACK_RXCHID         22
#define M_CPL_TX_TLS_ACK_RXCHID         0x1
#define V_CPL_TX_TLS_ACK_RXCHID(x)      ((x) << S_CPL_TX_TLS_ACK_RXCHID)
#define G_CPL_TX_TLS_ACK_RXCHID(x)      \
    (((x) >> S_CPL_TX_TLS_ACK_RXCHID) & M_CPL_TX_TLS_ACK_RXCHID)
#define F_CPL_TX_TLS_ACK_RXCHID V_CPL_TX_TLS_ACK_RXCHID(1U)

#define S_CPL_TX_TLS_ACK_FWMSG          21
#define M_CPL_TX_TLS_ACK_FWMSG          0x1
#define V_CPL_TX_TLS_ACK_FWMSG(x)       ((x) << S_CPL_TX_TLS_ACK_FWMSG)
#define G_CPL_TX_TLS_ACK_FWMSG(x)       \
    (((x) >> S_CPL_TX_TLS_ACK_FWMSG) & M_CPL_TX_TLS_ACK_FWMSG)
#define F_CPL_TX_TLS_ACK_FWMSG  V_CPL_TX_TLS_ACK_FWMSG(1U)

#define S_CPL_TX_TLS_ACK_ULPTXLPBK      20
#define M_CPL_TX_TLS_ACK_ULPTXLPBK      0x1
#define V_CPL_TX_TLS_ACK_ULPTXLPBK(x)   ((x) << S_CPL_TX_TLS_ACK_ULPTXLPBK)
#define G_CPL_TX_TLS_ACK_ULPTXLPBK(x)   \
    (((x) >> S_CPL_TX_TLS_ACK_ULPTXLPBK) & M_CPL_TX_TLS_ACK_ULPTXLPBK)
#define F_CPL_TX_TLS_ACK_ULPTXLPBK      V_CPL_TX_TLS_ACK_ULPTXLPBK(1U)

#define S_CPL_TX_TLS_ACK_CPLLEN         16
#define M_CPL_TX_TLS_ACK_CPLLEN         0xf
#define V_CPL_TX_TLS_ACK_CPLLEN(x)      ((x) << S_CPL_TX_TLS_ACK_CPLLEN)
#define G_CPL_TX_TLS_ACK_CPLLEN(x)      \
    (((x) >> S_CPL_TX_TLS_ACK_CPLLEN) & M_CPL_TX_TLS_ACK_CPLLEN)

#define S_CPL_TX_TLS_ACK_COMPLONERR     15
#define M_CPL_TX_TLS_ACK_COMPLONERR     0x1
#define V_CPL_TX_TLS_ACK_COMPLONERR(x)  ((x) << S_CPL_TX_TLS_ACK_COMPLONERR)
#define G_CPL_TX_TLS_ACK_COMPLONERR(x)  \
    (((x) >> S_CPL_TX_TLS_ACK_COMPLONERR) & M_CPL_TX_TLS_ACK_COMPLONERR)
#define F_CPL_TX_TLS_ACK_COMPLONERR     V_CPL_TX_TLS_ACK_COMPLONERR(1U)

#define S_CPL_TX_TLS_ACK_LCB    14
#define M_CPL_TX_TLS_ACK_LCB    0x1
#define V_CPL_TX_TLS_ACK_LCB(x) ((x) << S_CPL_TX_TLS_ACK_LCB)
#define G_CPL_TX_TLS_ACK_LCB(x) \
    (((x) >> S_CPL_TX_TLS_ACK_LCB) & M_CPL_TX_TLS_ACK_LCB)
#define F_CPL_TX_TLS_ACK_LCB    V_CPL_TX_TLS_ACK_LCB(1U)

#define S_CPL_TX_TLS_ACK_PHASH          13
#define M_CPL_TX_TLS_ACK_PHASH          0x1
#define V_CPL_TX_TLS_ACK_PHASH(x)       ((x) << S_CPL_TX_TLS_ACK_PHASH)
#define G_CPL_TX_TLS_ACK_PHASH(x)       \
    (((x) >> S_CPL_TX_TLS_ACK_PHASH) & M_CPL_TX_TLS_ACK_PHASH)
#define F_CPL_TX_TLS_ACK_PHASH  V_CPL_TX_TLS_ACK_PHASH(1U)

#define S_CPL_TX_TLS_ACK_RSVD2          0
#define M_CPL_TX_TLS_ACK_RSVD2          0x1fff
#define V_CPL_TX_TLS_ACK_RSVD2(x)       ((x) << S_CPL_TX_TLS_ACK_RSVD2)
#define G_CPL_TX_TLS_ACK_RSVD2(x)       \
    (((x) >> S_CPL_TX_TLS_ACK_RSVD2) & M_CPL_TX_TLS_ACK_RSVD2)

#endif  /* T4_MSG_H */
