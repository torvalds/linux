/*-
 * Copyright (c) 2012-2017 Chelsio Communications, Inc.
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

#ifndef _T4FW_INTERFACE_H_
#define _T4FW_INTERFACE_H_

/******************************************************************************
 *   R E T U R N   V A L U E S
 ********************************/

enum fw_retval {
	FW_SUCCESS		= 0,	/* completed successfully */
	FW_EPERM		= 1,	/* operation not permitted */
	FW_ENOENT		= 2,	/* no such file or directory */
	FW_EIO			= 5,	/* input/output error; hw bad */
	FW_ENOEXEC		= 8,	/* exec format error; inv microcode */
	FW_EAGAIN		= 11,	/* try again */
	FW_ENOMEM		= 12,	/* out of memory */
	FW_EFAULT		= 14,	/* bad address; fw bad */
	FW_EBUSY		= 16,	/* resource busy */
	FW_EEXIST		= 17,	/* file exists */
	FW_ENODEV		= 19,	/* no such device */
	FW_EINVAL		= 22,	/* invalid argument */
	FW_ENOSPC		= 28,	/* no space left on device */
	FW_ENOSYS		= 38,	/* functionality not implemented */
	FW_ENODATA		= 61,	/* no data available */
	FW_EPROTO		= 71,	/* protocol error */
	FW_EADDRINUSE		= 98,	/* address already in use */
	FW_EADDRNOTAVAIL	= 99,	/* cannot assigned requested address */
	FW_ENETDOWN		= 100,	/* network is down */
	FW_ENETUNREACH		= 101,	/* network is unreachable */
	FW_ENOBUFS		= 105,	/* no buffer space available */
	FW_ETIMEDOUT		= 110,	/* timeout */
	FW_EINPROGRESS		= 115,	/* fw internal */
	FW_SCSI_ABORT_REQUESTED	= 128,	/* */
	FW_SCSI_ABORT_TIMEDOUT	= 129,	/* */
	FW_SCSI_ABORTED		= 130,	/* */
	FW_SCSI_CLOSE_REQUESTED	= 131,	/* */
	FW_ERR_LINK_DOWN	= 132,	/* */
	FW_RDEV_NOT_READY	= 133,	/* */
	FW_ERR_RDEV_LOST	= 134,	/* */
	FW_ERR_RDEV_LOGO	= 135,	/* */
	FW_FCOE_NO_XCHG		= 136,	/* */
	FW_SCSI_RSP_ERR		= 137,	/* */
	FW_ERR_RDEV_IMPL_LOGO	= 138,	/* */
	FW_SCSI_UNDER_FLOW_ERR  = 139,	/* */
	FW_SCSI_OVER_FLOW_ERR   = 140,	/* */
	FW_SCSI_DDP_ERR		= 141,	/* DDP error*/
	FW_SCSI_TASK_ERR	= 142,	/* No SCSI tasks available */
	FW_SCSI_IO_BLOCK	= 143,	/* IO is going to be blocked due to resource failure */
};

/******************************************************************************
 *   M E M O R Y   T Y P E s
 ******************************/

enum fw_memtype {
	FW_MEMTYPE_EDC0		= 0x0,
	FW_MEMTYPE_EDC1		= 0x1,
	FW_MEMTYPE_EXTMEM	= 0x2,
	FW_MEMTYPE_FLASH	= 0x4,
	FW_MEMTYPE_INTERNAL	= 0x5,
	FW_MEMTYPE_EXTMEM1	= 0x6,
	FW_MEMTYPE_HMA          = 0x7,
};

/******************************************************************************
 *   W O R K   R E Q U E S T s
 ********************************/

enum fw_wr_opcodes {
	FW_FRAG_WR		= 0x1d,
	FW_FILTER_WR		= 0x02,
	FW_ULPTX_WR		= 0x04,
	FW_TP_WR		= 0x05,
	FW_ETH_TX_PKT_WR	= 0x08,
	FW_ETH_TX_PKT2_WR	= 0x44,
	FW_ETH_TX_PKTS_WR	= 0x09,
	FW_ETH_TX_PKTS2_WR	= 0x78,
	FW_ETH_TX_EO_WR		= 0x1c,
	FW_EQ_FLUSH_WR		= 0x1b,
	FW_OFLD_CONNECTION_WR	= 0x2f,
	FW_FLOWC_WR		= 0x0a,
	FW_OFLD_TX_DATA_WR	= 0x0b,
	FW_CMD_WR		= 0x10,
	FW_ETH_TX_PKT_VM_WR	= 0x11,
	FW_ETH_TX_PKTS_VM_WR	= 0x12,
	FW_RI_RES_WR		= 0x0c,
	FW_RI_RDMA_WRITE_WR	= 0x14,
	FW_RI_SEND_WR		= 0x15,
	FW_RI_RDMA_READ_WR	= 0x16,
	FW_RI_RECV_WR		= 0x17,
	FW_RI_BIND_MW_WR	= 0x18,
	FW_RI_FR_NSMR_WR	= 0x19,
	FW_RI_FR_NSMR_TPTE_WR	= 0x20,
	FW_RI_RDMA_WRITE_CMPL_WR =  0x21,
	FW_RI_INV_LSTAG_WR	= 0x1a,
	FW_RI_SEND_IMMEDIATE_WR	= 0x15,
	FW_RI_ATOMIC_WR		= 0x16,
	FW_RI_WR		= 0x0d,
	FW_CHNET_IFCONF_WR	= 0x6b,
	FW_RDEV_WR		= 0x38,
	FW_FOISCSI_NODE_WR	= 0x60,
	FW_FOISCSI_CTRL_WR	= 0x6a,
	FW_FOISCSI_CHAP_WR	= 0x6c,
	FW_FCOE_ELS_CT_WR	= 0x30,
	FW_SCSI_WRITE_WR	= 0x31,
	FW_SCSI_READ_WR		= 0x32,
	FW_SCSI_CMD_WR		= 0x33,
	FW_SCSI_ABRT_CLS_WR	= 0x34,
	FW_SCSI_TGT_ACC_WR	= 0x35,
	FW_SCSI_TGT_XMIT_WR	= 0x36,
	FW_SCSI_TGT_RSP_WR	= 0x37,
	FW_POFCOE_TCB_WR	= 0x42,
	FW_POFCOE_ULPTX_WR	= 0x43,
	FW_ISCSI_TX_DATA_WR	= 0x45,
	FW_PTP_TX_PKT_WR        = 0x46,
	FW_TLSTX_DATA_WR	= 0x68,
	FW_CRYPTO_LOOKASIDE_WR	= 0x6d,
	FW_COISCSI_TGT_WR	= 0x70,
	FW_COISCSI_TGT_CONN_WR	= 0x71,
	FW_COISCSI_TGT_XMIT_WR	= 0x72,
	FW_COISCSI_STATS_WR	 = 0x73,
	FW_ISNS_WR		= 0x75,
	FW_ISNS_XMIT_WR		= 0x76,
	FW_FILTER2_WR		= 0x77,
	FW_LASTC2E_WR		= 0x80
};

/*
 * Generic work request header flit0
 */
struct fw_wr_hdr {
	__be32 hi;
	__be32 lo;
};

/*	work request opcode (hi)
 */
#define S_FW_WR_OP		24
#define M_FW_WR_OP		0xff
#define V_FW_WR_OP(x)		((x) << S_FW_WR_OP)
#define G_FW_WR_OP(x)		(((x) >> S_FW_WR_OP) & M_FW_WR_OP)

/*	atomic flag (hi) - firmware encapsulates CPLs in CPL_BARRIER
 */
#define S_FW_WR_ATOMIC		23
#define M_FW_WR_ATOMIC		0x1
#define V_FW_WR_ATOMIC(x)	((x) << S_FW_WR_ATOMIC)
#define G_FW_WR_ATOMIC(x)	\
    (((x) >> S_FW_WR_ATOMIC) & M_FW_WR_ATOMIC)
#define F_FW_WR_ATOMIC		V_FW_WR_ATOMIC(1U)

/*	flush flag (hi) - firmware flushes flushable work request buffered
 *			      in the flow context.
 */
#define S_FW_WR_FLUSH     22
#define M_FW_WR_FLUSH     0x1
#define V_FW_WR_FLUSH(x)  ((x) << S_FW_WR_FLUSH)
#define G_FW_WR_FLUSH(x)  \
    (((x) >> S_FW_WR_FLUSH) & M_FW_WR_FLUSH)
#define F_FW_WR_FLUSH     V_FW_WR_FLUSH(1U)

/*	completion flag (hi) - firmware generates a cpl_fw6_ack
 */
#define S_FW_WR_COMPL     21
#define M_FW_WR_COMPL     0x1
#define V_FW_WR_COMPL(x)  ((x) << S_FW_WR_COMPL)
#define G_FW_WR_COMPL(x)  \
    (((x) >> S_FW_WR_COMPL) & M_FW_WR_COMPL)
#define F_FW_WR_COMPL     V_FW_WR_COMPL(1U)


/*	work request immediate data lengh (hi)
 */
#define S_FW_WR_IMMDLEN	0
#define M_FW_WR_IMMDLEN	0xff
#define V_FW_WR_IMMDLEN(x)	((x) << S_FW_WR_IMMDLEN)
#define G_FW_WR_IMMDLEN(x)	\
    (((x) >> S_FW_WR_IMMDLEN) & M_FW_WR_IMMDLEN)

/*	egress queue status update to associated ingress queue entry (lo)
 */
#define S_FW_WR_EQUIQ		31
#define M_FW_WR_EQUIQ		0x1
#define V_FW_WR_EQUIQ(x)	((x) << S_FW_WR_EQUIQ)
#define G_FW_WR_EQUIQ(x)	(((x) >> S_FW_WR_EQUIQ) & M_FW_WR_EQUIQ)
#define F_FW_WR_EQUIQ		V_FW_WR_EQUIQ(1U)

/*	egress queue status update to egress queue status entry (lo)
 */
#define S_FW_WR_EQUEQ		30
#define M_FW_WR_EQUEQ		0x1
#define V_FW_WR_EQUEQ(x)	((x) << S_FW_WR_EQUEQ)
#define G_FW_WR_EQUEQ(x)	(((x) >> S_FW_WR_EQUEQ) & M_FW_WR_EQUEQ)
#define F_FW_WR_EQUEQ		V_FW_WR_EQUEQ(1U)

/*	flow context identifier (lo)
 */
#define S_FW_WR_FLOWID		8
#define M_FW_WR_FLOWID		0xfffff
#define V_FW_WR_FLOWID(x)	((x) << S_FW_WR_FLOWID)
#define G_FW_WR_FLOWID(x)	(((x) >> S_FW_WR_FLOWID) & M_FW_WR_FLOWID)

/*	length in units of 16-bytes (lo)
 */
#define S_FW_WR_LEN16		0
#define M_FW_WR_LEN16		0xff
#define V_FW_WR_LEN16(x)	((x) << S_FW_WR_LEN16)
#define G_FW_WR_LEN16(x)	(((x) >> S_FW_WR_LEN16) & M_FW_WR_LEN16)

struct fw_frag_wr {
	__be32 op_to_fragoff16;
	__be32 flowid_len16;
	__be64 r4;
};

#define S_FW_FRAG_WR_EOF	15
#define M_FW_FRAG_WR_EOF	0x1
#define V_FW_FRAG_WR_EOF(x)	((x) << S_FW_FRAG_WR_EOF)
#define G_FW_FRAG_WR_EOF(x)	(((x) >> S_FW_FRAG_WR_EOF) & M_FW_FRAG_WR_EOF)
#define F_FW_FRAG_WR_EOF	V_FW_FRAG_WR_EOF(1U)

#define S_FW_FRAG_WR_FRAGOFF16		8
#define M_FW_FRAG_WR_FRAGOFF16		0x7f
#define V_FW_FRAG_WR_FRAGOFF16(x)	((x) << S_FW_FRAG_WR_FRAGOFF16)
#define G_FW_FRAG_WR_FRAGOFF16(x)	\
    (((x) >> S_FW_FRAG_WR_FRAGOFF16) & M_FW_FRAG_WR_FRAGOFF16)

/* valid filter configurations for compressed tuple
 * Encodings: TPL - Compressed TUPLE for filter in addition to 4-tuple
 * FR - FRAGMENT, FC - FCoE, MT - MPS MATCH TYPE, M - MPS MATCH,
 * E - Ethertype, P - Port, PR - Protocol, T - TOS, IV - Inner VLAN,
 * OV - Outer VLAN/VNIC_ID,
*/
#define HW_TPL_FR_MT_M_E_P_FC		0x3C3
#define HW_TPL_FR_MT_M_PR_T_FC		0x3B3
#define HW_TPL_FR_MT_M_IV_P_FC		0x38B
#define HW_TPL_FR_MT_M_OV_P_FC		0x387
#define HW_TPL_FR_MT_E_PR_T		0x370
#define HW_TPL_FR_MT_E_PR_P_FC		0X363
#define HW_TPL_FR_MT_E_T_P_FC		0X353
#define HW_TPL_FR_MT_PR_IV_P_FC		0X32B
#define HW_TPL_FR_MT_PR_OV_P_FC		0X327
#define HW_TPL_FR_MT_T_IV_P_FC		0X31B
#define HW_TPL_FR_MT_T_OV_P_FC		0X317
#define HW_TPL_FR_M_E_PR_FC		0X2E1
#define HW_TPL_FR_M_E_T_FC		0X2D1
#define HW_TPL_FR_M_PR_IV_FC		0X2A9
#define HW_TPL_FR_M_PR_OV_FC		0X2A5
#define HW_TPL_FR_M_T_IV_FC		0X299
#define HW_TPL_FR_M_T_OV_FC		0X295
#define HW_TPL_FR_E_PR_T_P		0X272
#define HW_TPL_FR_E_PR_T_FC		0X271
#define HW_TPL_FR_E_IV_FC		0X249
#define HW_TPL_FR_E_OV_FC		0X245
#define HW_TPL_FR_PR_T_IV_FC		0X239
#define HW_TPL_FR_PR_T_OV_FC		0X235
#define HW_TPL_FR_IV_OV_FC		0X20D
#define HW_TPL_MT_M_E_PR		0X1E0
#define HW_TPL_MT_M_E_T			0X1D0
#define HW_TPL_MT_E_PR_T_FC		0X171
#define HW_TPL_MT_E_IV			0X148
#define HW_TPL_MT_E_OV			0X144
#define HW_TPL_MT_PR_T_IV		0X138
#define HW_TPL_MT_PR_T_OV		0X134
#define HW_TPL_M_E_PR_P			0X0E2
#define HW_TPL_M_E_T_P			0X0D2
#define HW_TPL_E_PR_T_P_FC		0X073
#define HW_TPL_E_IV_P			0X04A
#define HW_TPL_E_OV_P			0X046
#define HW_TPL_PR_T_IV_P		0X03A
#define HW_TPL_PR_T_OV_P		0X036

/* filter wr reply code in cookie in CPL_SET_TCB_RPL */
enum fw_filter_wr_cookie {
	FW_FILTER_WR_SUCCESS,
	FW_FILTER_WR_FLT_ADDED,
	FW_FILTER_WR_FLT_DELETED,
	FW_FILTER_WR_SMT_TBL_FULL,
	FW_FILTER_WR_EINVAL,
};

enum fw_filter_wr_nat_mode {
	FW_FILTER_WR_NATMODE_NONE = 0,
	FW_FILTER_WR_NATMODE_DIP ,
	FW_FILTER_WR_NATMODE_DIPDP,
	FW_FILTER_WR_NATMODE_DIPDPSIP,
	FW_FILTER_WR_NATMODE_DIPDPSP,
	FW_FILTER_WR_NATMODE_SIPSP,
	FW_FILTER_WR_NATMODE_DIPSIPSP,
	FW_FILTER_WR_NATMODE_FOURTUPLE,
};

struct fw_filter_wr {
	__be32 op_pkd;
	__be32 len16_pkd;
	__be64 r3;
	__be32 tid_to_iq;
	__be32 del_filter_to_l2tix;
	__be16 ethtype;
	__be16 ethtypem;
	__u8   frag_to_ovlan_vldm;
	__u8   smac_sel;
	__be16 rx_chan_rx_rpl_iq;
	__be32 maci_to_matchtypem;
	__u8   ptcl;
	__u8   ptclm;
	__u8   ttyp;
	__u8   ttypm;
	__be16 ivlan;
	__be16 ivlanm;
	__be16 ovlan;
	__be16 ovlanm;
	__u8   lip[16];
	__u8   lipm[16];
	__u8   fip[16];
	__u8   fipm[16];
	__be16 lp;
	__be16 lpm;
	__be16 fp;
	__be16 fpm;
	__be16 r7;
	__u8   sma[6];
};

struct fw_filter2_wr {
	__be32 op_pkd;
	__be32 len16_pkd;
	__be64 r3;
	__be32 tid_to_iq;
	__be32 del_filter_to_l2tix;
	__be16 ethtype;
	__be16 ethtypem;
	__u8   frag_to_ovlan_vldm;
	__u8   smac_sel;
	__be16 rx_chan_rx_rpl_iq;
	__be32 maci_to_matchtypem;
	__u8   ptcl;
	__u8   ptclm;
	__u8   ttyp;
	__u8   ttypm;
	__be16 ivlan;
	__be16 ivlanm;
	__be16 ovlan;
	__be16 ovlanm;
	__u8   lip[16];
	__u8   lipm[16];
	__u8   fip[16];
	__u8   fipm[16];
	__be16 lp;
	__be16 lpm;
	__be16 fp;
	__be16 fpm;
	__be16 r7;
	__u8   sma[6];
	__be16 r8;
	__u8   filter_type_swapmac;
	__u8   natmode_to_ulp_type;
	__be16 newlport;
	__be16 newfport;
	__u8   newlip[16];
	__u8   newfip[16];
	__be32 natseqcheck;
	__be32 r9;
	__be64 r10;
	__be64 r11;
	__be64 r12;
	__be64 r13;
};

#define S_FW_FILTER_WR_TID	12
#define M_FW_FILTER_WR_TID	0xfffff
#define V_FW_FILTER_WR_TID(x)	((x) << S_FW_FILTER_WR_TID)
#define G_FW_FILTER_WR_TID(x)	\
    (((x) >> S_FW_FILTER_WR_TID) & M_FW_FILTER_WR_TID)

#define S_FW_FILTER_WR_RQTYPE		11
#define M_FW_FILTER_WR_RQTYPE		0x1
#define V_FW_FILTER_WR_RQTYPE(x)	((x) << S_FW_FILTER_WR_RQTYPE)
#define G_FW_FILTER_WR_RQTYPE(x)	\
    (((x) >> S_FW_FILTER_WR_RQTYPE) & M_FW_FILTER_WR_RQTYPE)
#define F_FW_FILTER_WR_RQTYPE	V_FW_FILTER_WR_RQTYPE(1U)

#define S_FW_FILTER_WR_NOREPLY		10
#define M_FW_FILTER_WR_NOREPLY		0x1
#define V_FW_FILTER_WR_NOREPLY(x)	((x) << S_FW_FILTER_WR_NOREPLY)
#define G_FW_FILTER_WR_NOREPLY(x)	\
    (((x) >> S_FW_FILTER_WR_NOREPLY) & M_FW_FILTER_WR_NOREPLY)
#define F_FW_FILTER_WR_NOREPLY	V_FW_FILTER_WR_NOREPLY(1U)

#define S_FW_FILTER_WR_IQ	0
#define M_FW_FILTER_WR_IQ	0x3ff
#define V_FW_FILTER_WR_IQ(x)	((x) << S_FW_FILTER_WR_IQ)
#define G_FW_FILTER_WR_IQ(x)	\
    (((x) >> S_FW_FILTER_WR_IQ) & M_FW_FILTER_WR_IQ)

#define S_FW_FILTER_WR_DEL_FILTER	31
#define M_FW_FILTER_WR_DEL_FILTER	0x1
#define V_FW_FILTER_WR_DEL_FILTER(x)	((x) << S_FW_FILTER_WR_DEL_FILTER)
#define G_FW_FILTER_WR_DEL_FILTER(x)	\
    (((x) >> S_FW_FILTER_WR_DEL_FILTER) & M_FW_FILTER_WR_DEL_FILTER)
#define F_FW_FILTER_WR_DEL_FILTER	V_FW_FILTER_WR_DEL_FILTER(1U)

#define S_FW_FILTER2_WR_DROP_ENCAP	30
#define M_FW_FILTER2_WR_DROP_ENCAP	0x1
#define V_FW_FILTER2_WR_DROP_ENCAP(x)	((x) << S_FW_FILTER2_WR_DROP_ENCAP)
#define G_FW_FILTER2_WR_DROP_ENCAP(x)	\
    (((x) >> S_FW_FILTER2_WR_DROP_ENCAP) & M_FW_FILTER2_WR_DROP_ENCAP)
#define F_FW_FILTER2_WR_DROP_ENCAP	V_FW_FILTER2_WR_DROP_ENCAP(1U)

#define S_FW_FILTER2_WR_TX_LOOP         29
#define M_FW_FILTER2_WR_TX_LOOP         0x1
#define V_FW_FILTER2_WR_TX_LOOP(x)      ((x) << S_FW_FILTER2_WR_TX_LOOP)
#define G_FW_FILTER2_WR_TX_LOOP(x)      \
	    (((x) >> S_FW_FILTER2_WR_TX_LOOP) & M_FW_FILTER2_WR_TX_LOOP)
#define F_FW_FILTER2_WR_TX_LOOP         V_FW_FILTER2_WR_TX_LOOP(1U)

#define S_FW_FILTER_WR_RPTTID		25
#define M_FW_FILTER_WR_RPTTID		0x1
#define V_FW_FILTER_WR_RPTTID(x)	((x) << S_FW_FILTER_WR_RPTTID)
#define G_FW_FILTER_WR_RPTTID(x)	\
    (((x) >> S_FW_FILTER_WR_RPTTID) & M_FW_FILTER_WR_RPTTID)
#define F_FW_FILTER_WR_RPTTID	V_FW_FILTER_WR_RPTTID(1U)

#define S_FW_FILTER_WR_DROP	24
#define M_FW_FILTER_WR_DROP	0x1
#define V_FW_FILTER_WR_DROP(x)	((x) << S_FW_FILTER_WR_DROP)
#define G_FW_FILTER_WR_DROP(x)	\
    (((x) >> S_FW_FILTER_WR_DROP) & M_FW_FILTER_WR_DROP)
#define F_FW_FILTER_WR_DROP	V_FW_FILTER_WR_DROP(1U)

#define S_FW_FILTER_WR_DIRSTEER		23
#define M_FW_FILTER_WR_DIRSTEER		0x1
#define V_FW_FILTER_WR_DIRSTEER(x)	((x) << S_FW_FILTER_WR_DIRSTEER)
#define G_FW_FILTER_WR_DIRSTEER(x)	\
    (((x) >> S_FW_FILTER_WR_DIRSTEER) & M_FW_FILTER_WR_DIRSTEER)
#define F_FW_FILTER_WR_DIRSTEER	V_FW_FILTER_WR_DIRSTEER(1U)

#define S_FW_FILTER_WR_MASKHASH		22
#define M_FW_FILTER_WR_MASKHASH		0x1
#define V_FW_FILTER_WR_MASKHASH(x)	((x) << S_FW_FILTER_WR_MASKHASH)
#define G_FW_FILTER_WR_MASKHASH(x)	\
    (((x) >> S_FW_FILTER_WR_MASKHASH) & M_FW_FILTER_WR_MASKHASH)
#define F_FW_FILTER_WR_MASKHASH	V_FW_FILTER_WR_MASKHASH(1U)

#define S_FW_FILTER_WR_DIRSTEERHASH	21
#define M_FW_FILTER_WR_DIRSTEERHASH	0x1
#define V_FW_FILTER_WR_DIRSTEERHASH(x)	((x) << S_FW_FILTER_WR_DIRSTEERHASH)
#define G_FW_FILTER_WR_DIRSTEERHASH(x)	\
    (((x) >> S_FW_FILTER_WR_DIRSTEERHASH) & M_FW_FILTER_WR_DIRSTEERHASH)
#define F_FW_FILTER_WR_DIRSTEERHASH	V_FW_FILTER_WR_DIRSTEERHASH(1U)

#define S_FW_FILTER_WR_LPBK	20
#define M_FW_FILTER_WR_LPBK	0x1
#define V_FW_FILTER_WR_LPBK(x)	((x) << S_FW_FILTER_WR_LPBK)
#define G_FW_FILTER_WR_LPBK(x)	\
    (((x) >> S_FW_FILTER_WR_LPBK) & M_FW_FILTER_WR_LPBK)
#define F_FW_FILTER_WR_LPBK	V_FW_FILTER_WR_LPBK(1U)

#define S_FW_FILTER_WR_DMAC	19
#define M_FW_FILTER_WR_DMAC	0x1
#define V_FW_FILTER_WR_DMAC(x)	((x) << S_FW_FILTER_WR_DMAC)
#define G_FW_FILTER_WR_DMAC(x)	\
    (((x) >> S_FW_FILTER_WR_DMAC) & M_FW_FILTER_WR_DMAC)
#define F_FW_FILTER_WR_DMAC	V_FW_FILTER_WR_DMAC(1U)

#define S_FW_FILTER_WR_SMAC	18
#define M_FW_FILTER_WR_SMAC	0x1
#define V_FW_FILTER_WR_SMAC(x)	((x) << S_FW_FILTER_WR_SMAC)
#define G_FW_FILTER_WR_SMAC(x)	\
    (((x) >> S_FW_FILTER_WR_SMAC) & M_FW_FILTER_WR_SMAC)
#define F_FW_FILTER_WR_SMAC	V_FW_FILTER_WR_SMAC(1U)

#define S_FW_FILTER_WR_INSVLAN		17
#define M_FW_FILTER_WR_INSVLAN		0x1
#define V_FW_FILTER_WR_INSVLAN(x)	((x) << S_FW_FILTER_WR_INSVLAN)
#define G_FW_FILTER_WR_INSVLAN(x)	\
    (((x) >> S_FW_FILTER_WR_INSVLAN) & M_FW_FILTER_WR_INSVLAN)
#define F_FW_FILTER_WR_INSVLAN	V_FW_FILTER_WR_INSVLAN(1U)

#define S_FW_FILTER_WR_RMVLAN		16
#define M_FW_FILTER_WR_RMVLAN		0x1
#define V_FW_FILTER_WR_RMVLAN(x)	((x) << S_FW_FILTER_WR_RMVLAN)
#define G_FW_FILTER_WR_RMVLAN(x)	\
    (((x) >> S_FW_FILTER_WR_RMVLAN) & M_FW_FILTER_WR_RMVLAN)
#define F_FW_FILTER_WR_RMVLAN	V_FW_FILTER_WR_RMVLAN(1U)

#define S_FW_FILTER_WR_HITCNTS		15
#define M_FW_FILTER_WR_HITCNTS		0x1
#define V_FW_FILTER_WR_HITCNTS(x)	((x) << S_FW_FILTER_WR_HITCNTS)
#define G_FW_FILTER_WR_HITCNTS(x)	\
    (((x) >> S_FW_FILTER_WR_HITCNTS) & M_FW_FILTER_WR_HITCNTS)
#define F_FW_FILTER_WR_HITCNTS	V_FW_FILTER_WR_HITCNTS(1U)

#define S_FW_FILTER_WR_TXCHAN		13
#define M_FW_FILTER_WR_TXCHAN		0x3
#define V_FW_FILTER_WR_TXCHAN(x)	((x) << S_FW_FILTER_WR_TXCHAN)
#define G_FW_FILTER_WR_TXCHAN(x)	\
    (((x) >> S_FW_FILTER_WR_TXCHAN) & M_FW_FILTER_WR_TXCHAN)

#define S_FW_FILTER_WR_PRIO	12
#define M_FW_FILTER_WR_PRIO	0x1
#define V_FW_FILTER_WR_PRIO(x)	((x) << S_FW_FILTER_WR_PRIO)
#define G_FW_FILTER_WR_PRIO(x)	\
    (((x) >> S_FW_FILTER_WR_PRIO) & M_FW_FILTER_WR_PRIO)
#define F_FW_FILTER_WR_PRIO	V_FW_FILTER_WR_PRIO(1U)

#define S_FW_FILTER_WR_L2TIX	0
#define M_FW_FILTER_WR_L2TIX	0xfff
#define V_FW_FILTER_WR_L2TIX(x)	((x) << S_FW_FILTER_WR_L2TIX)
#define G_FW_FILTER_WR_L2TIX(x)	\
    (((x) >> S_FW_FILTER_WR_L2TIX) & M_FW_FILTER_WR_L2TIX)

#define S_FW_FILTER_WR_FRAG	7
#define M_FW_FILTER_WR_FRAG	0x1
#define V_FW_FILTER_WR_FRAG(x)	((x) << S_FW_FILTER_WR_FRAG)
#define G_FW_FILTER_WR_FRAG(x)	\
    (((x) >> S_FW_FILTER_WR_FRAG) & M_FW_FILTER_WR_FRAG)
#define F_FW_FILTER_WR_FRAG	V_FW_FILTER_WR_FRAG(1U)

#define S_FW_FILTER_WR_FRAGM	6
#define M_FW_FILTER_WR_FRAGM	0x1
#define V_FW_FILTER_WR_FRAGM(x)	((x) << S_FW_FILTER_WR_FRAGM)
#define G_FW_FILTER_WR_FRAGM(x)	\
    (((x) >> S_FW_FILTER_WR_FRAGM) & M_FW_FILTER_WR_FRAGM)
#define F_FW_FILTER_WR_FRAGM	V_FW_FILTER_WR_FRAGM(1U)

#define S_FW_FILTER_WR_IVLAN_VLD	5
#define M_FW_FILTER_WR_IVLAN_VLD	0x1
#define V_FW_FILTER_WR_IVLAN_VLD(x)	((x) << S_FW_FILTER_WR_IVLAN_VLD)
#define G_FW_FILTER_WR_IVLAN_VLD(x)	\
    (((x) >> S_FW_FILTER_WR_IVLAN_VLD) & M_FW_FILTER_WR_IVLAN_VLD)
#define F_FW_FILTER_WR_IVLAN_VLD	V_FW_FILTER_WR_IVLAN_VLD(1U)

#define S_FW_FILTER_WR_OVLAN_VLD	4
#define M_FW_FILTER_WR_OVLAN_VLD	0x1
#define V_FW_FILTER_WR_OVLAN_VLD(x)	((x) << S_FW_FILTER_WR_OVLAN_VLD)
#define G_FW_FILTER_WR_OVLAN_VLD(x)	\
    (((x) >> S_FW_FILTER_WR_OVLAN_VLD) & M_FW_FILTER_WR_OVLAN_VLD)
#define F_FW_FILTER_WR_OVLAN_VLD	V_FW_FILTER_WR_OVLAN_VLD(1U)

#define S_FW_FILTER_WR_IVLAN_VLDM	3
#define M_FW_FILTER_WR_IVLAN_VLDM	0x1
#define V_FW_FILTER_WR_IVLAN_VLDM(x)	((x) << S_FW_FILTER_WR_IVLAN_VLDM)
#define G_FW_FILTER_WR_IVLAN_VLDM(x)	\
    (((x) >> S_FW_FILTER_WR_IVLAN_VLDM) & M_FW_FILTER_WR_IVLAN_VLDM)
#define F_FW_FILTER_WR_IVLAN_VLDM	V_FW_FILTER_WR_IVLAN_VLDM(1U)

#define S_FW_FILTER_WR_OVLAN_VLDM	2
#define M_FW_FILTER_WR_OVLAN_VLDM	0x1
#define V_FW_FILTER_WR_OVLAN_VLDM(x)	((x) << S_FW_FILTER_WR_OVLAN_VLDM)
#define G_FW_FILTER_WR_OVLAN_VLDM(x)	\
    (((x) >> S_FW_FILTER_WR_OVLAN_VLDM) & M_FW_FILTER_WR_OVLAN_VLDM)
#define F_FW_FILTER_WR_OVLAN_VLDM	V_FW_FILTER_WR_OVLAN_VLDM(1U)

#define S_FW_FILTER_WR_RX_CHAN		15
#define M_FW_FILTER_WR_RX_CHAN		0x1
#define V_FW_FILTER_WR_RX_CHAN(x)	((x) << S_FW_FILTER_WR_RX_CHAN)
#define G_FW_FILTER_WR_RX_CHAN(x)	\
    (((x) >> S_FW_FILTER_WR_RX_CHAN) & M_FW_FILTER_WR_RX_CHAN)
#define F_FW_FILTER_WR_RX_CHAN	V_FW_FILTER_WR_RX_CHAN(1U)

#define S_FW_FILTER_WR_RX_RPL_IQ	0
#define M_FW_FILTER_WR_RX_RPL_IQ	0x3ff
#define V_FW_FILTER_WR_RX_RPL_IQ(x)	((x) << S_FW_FILTER_WR_RX_RPL_IQ)
#define G_FW_FILTER_WR_RX_RPL_IQ(x)	\
    (((x) >> S_FW_FILTER_WR_RX_RPL_IQ) & M_FW_FILTER_WR_RX_RPL_IQ)

#define S_FW_FILTER2_WR_FILTER_TYPE	1
#define M_FW_FILTER2_WR_FILTER_TYPE	0x1
#define V_FW_FILTER2_WR_FILTER_TYPE(x)	((x) << S_FW_FILTER2_WR_FILTER_TYPE)
#define G_FW_FILTER2_WR_FILTER_TYPE(x)	\
    (((x) >> S_FW_FILTER2_WR_FILTER_TYPE) & M_FW_FILTER2_WR_FILTER_TYPE)
#define F_FW_FILTER2_WR_FILTER_TYPE	V_FW_FILTER2_WR_FILTER_TYPE(1U)

#define S_FW_FILTER2_WR_SWAPMAC		0
#define M_FW_FILTER2_WR_SWAPMAC		0x1
#define V_FW_FILTER2_WR_SWAPMAC(x)	((x) << S_FW_FILTER2_WR_SWAPMAC)
#define G_FW_FILTER2_WR_SWAPMAC(x)	\
    (((x) >> S_FW_FILTER2_WR_SWAPMAC) & M_FW_FILTER2_WR_SWAPMAC)
#define F_FW_FILTER2_WR_SWAPMAC		V_FW_FILTER2_WR_SWAPMAC(1U)

#define S_FW_FILTER2_WR_NATMODE		5
#define M_FW_FILTER2_WR_NATMODE		0x7
#define V_FW_FILTER2_WR_NATMODE(x)	((x) << S_FW_FILTER2_WR_NATMODE)
#define G_FW_FILTER2_WR_NATMODE(x)	\
    (((x) >> S_FW_FILTER2_WR_NATMODE) & M_FW_FILTER2_WR_NATMODE)

#define S_FW_FILTER2_WR_NATFLAGCHECK	4
#define M_FW_FILTER2_WR_NATFLAGCHECK	0x1
#define V_FW_FILTER2_WR_NATFLAGCHECK(x)	((x) << S_FW_FILTER2_WR_NATFLAGCHECK)
#define G_FW_FILTER2_WR_NATFLAGCHECK(x)	\
    (((x) >> S_FW_FILTER2_WR_NATFLAGCHECK) & M_FW_FILTER2_WR_NATFLAGCHECK)
#define F_FW_FILTER2_WR_NATFLAGCHECK	V_FW_FILTER2_WR_NATFLAGCHECK(1U)

#define S_FW_FILTER2_WR_ULP_TYPE	0
#define M_FW_FILTER2_WR_ULP_TYPE	0xf
#define V_FW_FILTER2_WR_ULP_TYPE(x)	((x) << S_FW_FILTER2_WR_ULP_TYPE)
#define G_FW_FILTER2_WR_ULP_TYPE(x)	\
    (((x) >> S_FW_FILTER2_WR_ULP_TYPE) & M_FW_FILTER2_WR_ULP_TYPE)

#define S_FW_FILTER_WR_MACI	23
#define M_FW_FILTER_WR_MACI	0x1ff
#define V_FW_FILTER_WR_MACI(x)	((x) << S_FW_FILTER_WR_MACI)
#define G_FW_FILTER_WR_MACI(x)	\
    (((x) >> S_FW_FILTER_WR_MACI) & M_FW_FILTER_WR_MACI)

#define S_FW_FILTER_WR_MACIM	14
#define M_FW_FILTER_WR_MACIM	0x1ff
#define V_FW_FILTER_WR_MACIM(x)	((x) << S_FW_FILTER_WR_MACIM)
#define G_FW_FILTER_WR_MACIM(x)	\
    (((x) >> S_FW_FILTER_WR_MACIM) & M_FW_FILTER_WR_MACIM)

#define S_FW_FILTER_WR_FCOE	13
#define M_FW_FILTER_WR_FCOE	0x1
#define V_FW_FILTER_WR_FCOE(x)	((x) << S_FW_FILTER_WR_FCOE)
#define G_FW_FILTER_WR_FCOE(x)	\
    (((x) >> S_FW_FILTER_WR_FCOE) & M_FW_FILTER_WR_FCOE)
#define F_FW_FILTER_WR_FCOE	V_FW_FILTER_WR_FCOE(1U)

#define S_FW_FILTER_WR_FCOEM	12
#define M_FW_FILTER_WR_FCOEM	0x1
#define V_FW_FILTER_WR_FCOEM(x)	((x) << S_FW_FILTER_WR_FCOEM)
#define G_FW_FILTER_WR_FCOEM(x)	\
    (((x) >> S_FW_FILTER_WR_FCOEM) & M_FW_FILTER_WR_FCOEM)
#define F_FW_FILTER_WR_FCOEM	V_FW_FILTER_WR_FCOEM(1U)

#define S_FW_FILTER_WR_PORT	9
#define M_FW_FILTER_WR_PORT	0x7
#define V_FW_FILTER_WR_PORT(x)	((x) << S_FW_FILTER_WR_PORT)
#define G_FW_FILTER_WR_PORT(x)	\
    (((x) >> S_FW_FILTER_WR_PORT) & M_FW_FILTER_WR_PORT)

#define S_FW_FILTER_WR_PORTM	6
#define M_FW_FILTER_WR_PORTM	0x7
#define V_FW_FILTER_WR_PORTM(x)	((x) << S_FW_FILTER_WR_PORTM)
#define G_FW_FILTER_WR_PORTM(x)	\
    (((x) >> S_FW_FILTER_WR_PORTM) & M_FW_FILTER_WR_PORTM)

#define S_FW_FILTER_WR_MATCHTYPE	3
#define M_FW_FILTER_WR_MATCHTYPE	0x7
#define V_FW_FILTER_WR_MATCHTYPE(x)	((x) << S_FW_FILTER_WR_MATCHTYPE)
#define G_FW_FILTER_WR_MATCHTYPE(x)	\
    (((x) >> S_FW_FILTER_WR_MATCHTYPE) & M_FW_FILTER_WR_MATCHTYPE)

#define S_FW_FILTER_WR_MATCHTYPEM	0
#define M_FW_FILTER_WR_MATCHTYPEM	0x7
#define V_FW_FILTER_WR_MATCHTYPEM(x)	((x) << S_FW_FILTER_WR_MATCHTYPEM)
#define G_FW_FILTER_WR_MATCHTYPEM(x)	\
    (((x) >> S_FW_FILTER_WR_MATCHTYPEM) & M_FW_FILTER_WR_MATCHTYPEM)

struct fw_ulptx_wr {
	__be32 op_to_compl;
	__be32 flowid_len16;
	__u64  cookie;
};

/*	flag for packet type - control packet (0), data packet (1)
 */
#define S_FW_ULPTX_WR_DATA	28
#define M_FW_ULPTX_WR_DATA	0x1
#define V_FW_ULPTX_WR_DATA(x)	((x) << S_FW_ULPTX_WR_DATA)
#define G_FW_ULPTX_WR_DATA(x)	\
    (((x) >> S_FW_ULPTX_WR_DATA) & M_FW_ULPTX_WR_DATA)
#define F_FW_ULPTX_WR_DATA	V_FW_ULPTX_WR_DATA(1U)

struct fw_tp_wr {
	__be32 op_to_immdlen;
	__be32 flowid_len16;
	__u64  cookie;
};

struct fw_eth_tx_pkt_wr {
	__be32 op_immdlen;
	__be32 equiq_to_len16;
	__be64 r3;
};

#define S_FW_ETH_TX_PKT_WR_IMMDLEN	0
#define M_FW_ETH_TX_PKT_WR_IMMDLEN	0x1ff
#define V_FW_ETH_TX_PKT_WR_IMMDLEN(x)	((x) << S_FW_ETH_TX_PKT_WR_IMMDLEN)
#define G_FW_ETH_TX_PKT_WR_IMMDLEN(x)	\
    (((x) >> S_FW_ETH_TX_PKT_WR_IMMDLEN) & M_FW_ETH_TX_PKT_WR_IMMDLEN)

struct fw_eth_tx_pkt2_wr {
	__be32 op_immdlen;
	__be32 equiq_to_len16;
	__be32 r3;
	__be32 L4ChkDisable_to_IpHdrLen;
};

#define S_FW_ETH_TX_PKT2_WR_IMMDLEN	0
#define M_FW_ETH_TX_PKT2_WR_IMMDLEN	0x1ff
#define V_FW_ETH_TX_PKT2_WR_IMMDLEN(x)	((x) << S_FW_ETH_TX_PKT2_WR_IMMDLEN)
#define G_FW_ETH_TX_PKT2_WR_IMMDLEN(x)	\
    (((x) >> S_FW_ETH_TX_PKT2_WR_IMMDLEN) & M_FW_ETH_TX_PKT2_WR_IMMDLEN)

#define S_FW_ETH_TX_PKT2_WR_L4CHKDISABLE	31
#define M_FW_ETH_TX_PKT2_WR_L4CHKDISABLE	0x1
#define V_FW_ETH_TX_PKT2_WR_L4CHKDISABLE(x)	\
    ((x) << S_FW_ETH_TX_PKT2_WR_L4CHKDISABLE)
#define G_FW_ETH_TX_PKT2_WR_L4CHKDISABLE(x)	\
    (((x) >> S_FW_ETH_TX_PKT2_WR_L4CHKDISABLE) & \
     M_FW_ETH_TX_PKT2_WR_L4CHKDISABLE)
#define F_FW_ETH_TX_PKT2_WR_L4CHKDISABLE	\
    V_FW_ETH_TX_PKT2_WR_L4CHKDISABLE(1U)

#define S_FW_ETH_TX_PKT2_WR_L3CHKDISABLE	30
#define M_FW_ETH_TX_PKT2_WR_L3CHKDISABLE	0x1
#define V_FW_ETH_TX_PKT2_WR_L3CHKDISABLE(x)	\
    ((x) << S_FW_ETH_TX_PKT2_WR_L3CHKDISABLE)
#define G_FW_ETH_TX_PKT2_WR_L3CHKDISABLE(x)	\
    (((x) >> S_FW_ETH_TX_PKT2_WR_L3CHKDISABLE) & \
     M_FW_ETH_TX_PKT2_WR_L3CHKDISABLE)
#define F_FW_ETH_TX_PKT2_WR_L3CHKDISABLE	\
    V_FW_ETH_TX_PKT2_WR_L3CHKDISABLE(1U)

#define S_FW_ETH_TX_PKT2_WR_IVLAN	28
#define M_FW_ETH_TX_PKT2_WR_IVLAN	0x1
#define V_FW_ETH_TX_PKT2_WR_IVLAN(x)	((x) << S_FW_ETH_TX_PKT2_WR_IVLAN)
#define G_FW_ETH_TX_PKT2_WR_IVLAN(x)	\
    (((x) >> S_FW_ETH_TX_PKT2_WR_IVLAN) & M_FW_ETH_TX_PKT2_WR_IVLAN)
#define F_FW_ETH_TX_PKT2_WR_IVLAN	V_FW_ETH_TX_PKT2_WR_IVLAN(1U)

#define S_FW_ETH_TX_PKT2_WR_IVLANTAG	12
#define M_FW_ETH_TX_PKT2_WR_IVLANTAG	0xffff
#define V_FW_ETH_TX_PKT2_WR_IVLANTAG(x)	((x) << S_FW_ETH_TX_PKT2_WR_IVLANTAG)
#define G_FW_ETH_TX_PKT2_WR_IVLANTAG(x)	\
    (((x) >> S_FW_ETH_TX_PKT2_WR_IVLANTAG) & M_FW_ETH_TX_PKT2_WR_IVLANTAG)

#define S_FW_ETH_TX_PKT2_WR_CHKTYPE	8
#define M_FW_ETH_TX_PKT2_WR_CHKTYPE	0xf
#define V_FW_ETH_TX_PKT2_WR_CHKTYPE(x)	((x) << S_FW_ETH_TX_PKT2_WR_CHKTYPE)
#define G_FW_ETH_TX_PKT2_WR_CHKTYPE(x)	\
    (((x) >> S_FW_ETH_TX_PKT2_WR_CHKTYPE) & M_FW_ETH_TX_PKT2_WR_CHKTYPE)

#define S_FW_ETH_TX_PKT2_WR_IPHDRLEN	0
#define M_FW_ETH_TX_PKT2_WR_IPHDRLEN	0xff
#define V_FW_ETH_TX_PKT2_WR_IPHDRLEN(x)	((x) << S_FW_ETH_TX_PKT2_WR_IPHDRLEN)
#define G_FW_ETH_TX_PKT2_WR_IPHDRLEN(x)	\
    (((x) >> S_FW_ETH_TX_PKT2_WR_IPHDRLEN) & M_FW_ETH_TX_PKT2_WR_IPHDRLEN)

struct fw_eth_tx_pkts_wr {
	__be32 op_pkd;
	__be32 equiq_to_len16;
	__be32 r3;
	__be16 plen;
	__u8   npkt;
	__u8   type;
};

#define S_FW_PTP_TX_PKT_WR_IMMDLEN      0
#define M_FW_PTP_TX_PKT_WR_IMMDLEN      0x1ff
#define V_FW_PTP_TX_PKT_WR_IMMDLEN(x)   ((x) << S_FW_PTP_TX_PKT_WR_IMMDLEN)
#define G_FW_PTP_TX_PKT_WR_IMMDLEN(x)   \
    (((x) >> S_FW_PTP_TX_PKT_WR_IMMDLEN) & M_FW_PTP_TX_PKT_WR_IMMDLEN)

struct fw_eth_tx_pkt_ptp_wr {
	__be32 op_immdlen;
	__be32 equiq_to_len16;
	__be64 r3;
};

enum fw_eth_tx_eo_type {
	FW_ETH_TX_EO_TYPE_UDPSEG,
	FW_ETH_TX_EO_TYPE_TCPSEG,
	FW_ETH_TX_EO_TYPE_NVGRESEG,
	FW_ETH_TX_EO_TYPE_VXLANSEG,
	FW_ETH_TX_EO_TYPE_GENEVESEG,
};

struct fw_eth_tx_eo_wr {
	__be32 op_immdlen;
	__be32 equiq_to_len16;
	__be64 r3;
	union fw_eth_tx_eo {
		struct fw_eth_tx_eo_udpseg {
			__u8   type;
			__u8   ethlen;
			__be16 iplen;
			__u8   udplen;
			__u8   rtplen;
			__be16 r4;
			__be16 mss;
			__be16 schedpktsize;
			__be32 plen;
		} udpseg;
		struct fw_eth_tx_eo_tcpseg {
			__u8   type;
			__u8   ethlen;
			__be16 iplen;
			__u8   tcplen;
			__u8   tsclk_tsoff;
			__be16 r4;
			__be16 mss;
			__be16 r5;
			__be32 plen;
		} tcpseg;
		struct fw_eth_tx_eo_nvgreseg {
			__u8   type;
			__u8   iphdroffout;
			__be16 grehdroff;
			__be16 iphdroffin;
			__be16 tcphdroffin;
			__be16 mss;
			__be16 r4;
			__be32 plen;
		} nvgreseg;
		struct fw_eth_tx_eo_vxlanseg {
			__u8   type;
			__u8   iphdroffout;
			__be16 vxlanhdroff;
			__be16 iphdroffin;
			__be16 tcphdroffin;
			__be16 mss;
			__be16 r4;
			__be32 plen;

		} vxlanseg;
		struct fw_eth_tx_eo_geneveseg {
			__u8   type;
			__u8   iphdroffout;
			__be16 genevehdroff;
			__be16 iphdroffin;
			__be16 tcphdroffin;
			__be16 mss;
			__be16 r4;
			__be32 plen;
		} geneveseg;
	} u;
};

#define S_FW_ETH_TX_EO_WR_IMMDLEN	0
#define M_FW_ETH_TX_EO_WR_IMMDLEN	0x1ff
#define V_FW_ETH_TX_EO_WR_IMMDLEN(x)	((x) << S_FW_ETH_TX_EO_WR_IMMDLEN)
#define G_FW_ETH_TX_EO_WR_IMMDLEN(x)	\
    (((x) >> S_FW_ETH_TX_EO_WR_IMMDLEN) & M_FW_ETH_TX_EO_WR_IMMDLEN)

#define S_FW_ETH_TX_EO_WR_TSCLK		6
#define M_FW_ETH_TX_EO_WR_TSCLK		0x3
#define V_FW_ETH_TX_EO_WR_TSCLK(x)	((x) << S_FW_ETH_TX_EO_WR_TSCLK)
#define G_FW_ETH_TX_EO_WR_TSCLK(x)	\
    (((x) >> S_FW_ETH_TX_EO_WR_TSCLK) & M_FW_ETH_TX_EO_WR_TSCLK)

#define S_FW_ETH_TX_EO_WR_TSOFF		0
#define M_FW_ETH_TX_EO_WR_TSOFF		0x3f
#define V_FW_ETH_TX_EO_WR_TSOFF(x)	((x) << S_FW_ETH_TX_EO_WR_TSOFF)
#define G_FW_ETH_TX_EO_WR_TSOFF(x)	\
    (((x) >> S_FW_ETH_TX_EO_WR_TSOFF) & M_FW_ETH_TX_EO_WR_TSOFF)

struct fw_eq_flush_wr {
	__u8   opcode;
	__u8   r1[3];
	__be32 equiq_to_len16;
	__be64 r3;
};

struct fw_ofld_connection_wr {
	__be32 op_compl;
	__be32 len16_pkd;
	__u64  cookie;
	__be64 r2;
	__be64 r3;
	struct fw_ofld_connection_le {
		__be32 version_cpl;
		__be32 filter;
		__be32 r1;
		__be16 lport;
		__be16 pport;
		union fw_ofld_connection_leip {
			struct fw_ofld_connection_le_ipv4 {
				__be32 pip;
				__be32 lip;
				__be64 r0;
				__be64 r1;
				__be64 r2;
			} ipv4;
			struct fw_ofld_connection_le_ipv6 {
				__be64 pip_hi;
				__be64 pip_lo;
				__be64 lip_hi;
				__be64 lip_lo;
			} ipv6;
		} u;
	} le;
	struct fw_ofld_connection_tcb {
		__be32 t_state_to_astid;
		__be16 cplrxdataack_cplpassacceptrpl;
		__be16 rcv_adv;
		__be32 rcv_nxt;
		__be32 tx_max;
		__be64 opt0;
		__be32 opt2;
		__be32 r1;
		__be64 r2;
		__be64 r3;
	} tcb;
};

#define S_FW_OFLD_CONNECTION_WR_VERSION		31
#define M_FW_OFLD_CONNECTION_WR_VERSION		0x1
#define V_FW_OFLD_CONNECTION_WR_VERSION(x)	\
    ((x) << S_FW_OFLD_CONNECTION_WR_VERSION)
#define G_FW_OFLD_CONNECTION_WR_VERSION(x)	\
    (((x) >> S_FW_OFLD_CONNECTION_WR_VERSION) & \
     M_FW_OFLD_CONNECTION_WR_VERSION)
#define F_FW_OFLD_CONNECTION_WR_VERSION	V_FW_OFLD_CONNECTION_WR_VERSION(1U)

#define S_FW_OFLD_CONNECTION_WR_CPL	30
#define M_FW_OFLD_CONNECTION_WR_CPL	0x1
#define V_FW_OFLD_CONNECTION_WR_CPL(x)	((x) << S_FW_OFLD_CONNECTION_WR_CPL)
#define G_FW_OFLD_CONNECTION_WR_CPL(x)	\
    (((x) >> S_FW_OFLD_CONNECTION_WR_CPL) & M_FW_OFLD_CONNECTION_WR_CPL)
#define F_FW_OFLD_CONNECTION_WR_CPL	V_FW_OFLD_CONNECTION_WR_CPL(1U)

#define S_FW_OFLD_CONNECTION_WR_T_STATE		28
#define M_FW_OFLD_CONNECTION_WR_T_STATE		0xf
#define V_FW_OFLD_CONNECTION_WR_T_STATE(x)	\
    ((x) << S_FW_OFLD_CONNECTION_WR_T_STATE)
#define G_FW_OFLD_CONNECTION_WR_T_STATE(x)	\
    (((x) >> S_FW_OFLD_CONNECTION_WR_T_STATE) & \
     M_FW_OFLD_CONNECTION_WR_T_STATE)

#define S_FW_OFLD_CONNECTION_WR_RCV_SCALE	24
#define M_FW_OFLD_CONNECTION_WR_RCV_SCALE	0xf
#define V_FW_OFLD_CONNECTION_WR_RCV_SCALE(x)	\
    ((x) << S_FW_OFLD_CONNECTION_WR_RCV_SCALE)
#define G_FW_OFLD_CONNECTION_WR_RCV_SCALE(x)	\
    (((x) >> S_FW_OFLD_CONNECTION_WR_RCV_SCALE) & \
     M_FW_OFLD_CONNECTION_WR_RCV_SCALE)

#define S_FW_OFLD_CONNECTION_WR_ASTID		0
#define M_FW_OFLD_CONNECTION_WR_ASTID		0xffffff
#define V_FW_OFLD_CONNECTION_WR_ASTID(x)	\
    ((x) << S_FW_OFLD_CONNECTION_WR_ASTID)
#define G_FW_OFLD_CONNECTION_WR_ASTID(x)	\
    (((x) >> S_FW_OFLD_CONNECTION_WR_ASTID) & M_FW_OFLD_CONNECTION_WR_ASTID)

#define S_FW_OFLD_CONNECTION_WR_CPLRXDATAACK	15
#define M_FW_OFLD_CONNECTION_WR_CPLRXDATAACK	0x1
#define V_FW_OFLD_CONNECTION_WR_CPLRXDATAACK(x)	\
    ((x) << S_FW_OFLD_CONNECTION_WR_CPLRXDATAACK)
#define G_FW_OFLD_CONNECTION_WR_CPLRXDATAACK(x)	\
    (((x) >> S_FW_OFLD_CONNECTION_WR_CPLRXDATAACK) & \
     M_FW_OFLD_CONNECTION_WR_CPLRXDATAACK)
#define F_FW_OFLD_CONNECTION_WR_CPLRXDATAACK	\
    V_FW_OFLD_CONNECTION_WR_CPLRXDATAACK(1U)

#define S_FW_OFLD_CONNECTION_WR_CPLPASSACCEPTRPL	14
#define M_FW_OFLD_CONNECTION_WR_CPLPASSACCEPTRPL	0x1
#define V_FW_OFLD_CONNECTION_WR_CPLPASSACCEPTRPL(x)	\
    ((x) << S_FW_OFLD_CONNECTION_WR_CPLPASSACCEPTRPL)
#define G_FW_OFLD_CONNECTION_WR_CPLPASSACCEPTRPL(x)	\
    (((x) >> S_FW_OFLD_CONNECTION_WR_CPLPASSACCEPTRPL) & \
     M_FW_OFLD_CONNECTION_WR_CPLPASSACCEPTRPL)
#define F_FW_OFLD_CONNECTION_WR_CPLPASSACCEPTRPL	\
    V_FW_OFLD_CONNECTION_WR_CPLPASSACCEPTRPL(1U)

enum fw_flowc_mnem_tcpstate {
	FW_FLOWC_MNEM_TCPSTATE_CLOSED	= 0, /* illegal */
	FW_FLOWC_MNEM_TCPSTATE_LISTEN	= 1, /* illegal */
	FW_FLOWC_MNEM_TCPSTATE_SYNSENT	= 2, /* illegal */
	FW_FLOWC_MNEM_TCPSTATE_SYNRECEIVED = 3, /* illegal */
	FW_FLOWC_MNEM_TCPSTATE_ESTABLISHED = 4, /* default */
	FW_FLOWC_MNEM_TCPSTATE_CLOSEWAIT = 5, /* got peer close already */
	FW_FLOWC_MNEM_TCPSTATE_FINWAIT1	= 6, /* haven't gotten ACK for FIN and
					      * will resend FIN - equiv ESTAB
					      */
	FW_FLOWC_MNEM_TCPSTATE_CLOSING	= 7, /* haven't gotten ACK for FIN and
					      * will resend FIN but have
					      * received FIN
					      */
	FW_FLOWC_MNEM_TCPSTATE_LASTACK	= 8, /* haven't gotten ACK for FIN and
					      * will resend FIN but have
					      * received FIN
					      */
	FW_FLOWC_MNEM_TCPSTATE_FINWAIT2	= 9, /* sent FIN and got FIN + ACK,
					      * waiting for FIN
					      */
	FW_FLOWC_MNEM_TCPSTATE_TIMEWAIT	= 10, /* not expected */
};

enum fw_flowc_mnem_eostate {
	FW_FLOWC_MNEM_EOSTATE_CLOSED	= 0, /* illegal */
	FW_FLOWC_MNEM_EOSTATE_ESTABLISHED = 1, /* default */
	FW_FLOWC_MNEM_EOSTATE_CLOSING	= 2, /* graceful close, after sending
					      * outstanding payload
					      */
	FW_FLOWC_MNEM_EOSTATE_ABORTING	= 3, /* immediate close, after
					      * discarding outstanding payload
					      */
};

enum fw_flowc_mnem {
	FW_FLOWC_MNEM_PFNVFN		= 0, /* PFN [15:8] VFN [7:0] */
	FW_FLOWC_MNEM_CH		= 1,
	FW_FLOWC_MNEM_PORT		= 2,
	FW_FLOWC_MNEM_IQID		= 3,
	FW_FLOWC_MNEM_SNDNXT		= 4,
	FW_FLOWC_MNEM_RCVNXT		= 5,
	FW_FLOWC_MNEM_SNDBUF		= 6,
	FW_FLOWC_MNEM_MSS		= 7,
	FW_FLOWC_MNEM_TXDATAPLEN_MAX	= 8,
	FW_FLOWC_MNEM_TCPSTATE		= 9,
	FW_FLOWC_MNEM_EOSTATE		= 10,
	FW_FLOWC_MNEM_SCHEDCLASS	= 11,
	FW_FLOWC_MNEM_DCBPRIO		= 12,
	FW_FLOWC_MNEM_SND_SCALE		= 13,
	FW_FLOWC_MNEM_RCV_SCALE		= 14,
	FW_FLOWC_MNEM_ULP_MODE		= 15,
	FW_FLOWC_MNEM_MAX		= 16,
};

struct fw_flowc_mnemval {
	__u8   mnemonic;
	__u8   r4[3];
	__be32 val;
};

struct fw_flowc_wr {
	__be32 op_to_nparams;
	__be32 flowid_len16;
#ifndef C99_NOT_SUPPORTED
	struct fw_flowc_mnemval mnemval[0];
#endif
};

#define S_FW_FLOWC_WR_NPARAMS		0
#define M_FW_FLOWC_WR_NPARAMS		0xff
#define V_FW_FLOWC_WR_NPARAMS(x)	((x) << S_FW_FLOWC_WR_NPARAMS)
#define G_FW_FLOWC_WR_NPARAMS(x)	\
    (((x) >> S_FW_FLOWC_WR_NPARAMS) & M_FW_FLOWC_WR_NPARAMS)

struct fw_ofld_tx_data_wr {
	__be32 op_to_immdlen;
	__be32 flowid_len16;
	__be32 plen;
	__be32 lsodisable_to_flags;
};

#define S_FW_OFLD_TX_DATA_WR_LSODISABLE		31
#define M_FW_OFLD_TX_DATA_WR_LSODISABLE		0x1
#define V_FW_OFLD_TX_DATA_WR_LSODISABLE(x)	\
    ((x) << S_FW_OFLD_TX_DATA_WR_LSODISABLE)
#define G_FW_OFLD_TX_DATA_WR_LSODISABLE(x)	\
    (((x) >> S_FW_OFLD_TX_DATA_WR_LSODISABLE) & \
     M_FW_OFLD_TX_DATA_WR_LSODISABLE)
#define F_FW_OFLD_TX_DATA_WR_LSODISABLE	V_FW_OFLD_TX_DATA_WR_LSODISABLE(1U)

#define S_FW_OFLD_TX_DATA_WR_ALIGNPLD		30
#define M_FW_OFLD_TX_DATA_WR_ALIGNPLD		0x1
#define V_FW_OFLD_TX_DATA_WR_ALIGNPLD(x)	\
    ((x) << S_FW_OFLD_TX_DATA_WR_ALIGNPLD)
#define G_FW_OFLD_TX_DATA_WR_ALIGNPLD(x)	\
    (((x) >> S_FW_OFLD_TX_DATA_WR_ALIGNPLD) & M_FW_OFLD_TX_DATA_WR_ALIGNPLD)
#define F_FW_OFLD_TX_DATA_WR_ALIGNPLD	V_FW_OFLD_TX_DATA_WR_ALIGNPLD(1U)

#define S_FW_OFLD_TX_DATA_WR_ALIGNPLDSHOVE	29
#define M_FW_OFLD_TX_DATA_WR_ALIGNPLDSHOVE	0x1
#define V_FW_OFLD_TX_DATA_WR_ALIGNPLDSHOVE(x)	\
    ((x) << S_FW_OFLD_TX_DATA_WR_ALIGNPLDSHOVE)
#define G_FW_OFLD_TX_DATA_WR_ALIGNPLDSHOVE(x)	\
    (((x) >> S_FW_OFLD_TX_DATA_WR_ALIGNPLDSHOVE) & \
     M_FW_OFLD_TX_DATA_WR_ALIGNPLDSHOVE)
#define F_FW_OFLD_TX_DATA_WR_ALIGNPLDSHOVE	\
    V_FW_OFLD_TX_DATA_WR_ALIGNPLDSHOVE(1U)

#define S_FW_OFLD_TX_DATA_WR_FLAGS	0
#define M_FW_OFLD_TX_DATA_WR_FLAGS	0xfffffff
#define V_FW_OFLD_TX_DATA_WR_FLAGS(x)	((x) << S_FW_OFLD_TX_DATA_WR_FLAGS)
#define G_FW_OFLD_TX_DATA_WR_FLAGS(x)	\
    (((x) >> S_FW_OFLD_TX_DATA_WR_FLAGS) & M_FW_OFLD_TX_DATA_WR_FLAGS)


/* Use fw_ofld_tx_data_wr structure */
#define S_FW_ISCSI_TX_DATA_WR_FLAGS_HI		10
#define M_FW_ISCSI_TX_DATA_WR_FLAGS_HI		0x3fffff
#define V_FW_ISCSI_TX_DATA_WR_FLAGS_HI(x)	\
    ((x) << S_FW_ISCSI_TX_DATA_WR_FLAGS_HI)
#define G_FW_ISCSI_TX_DATA_WR_FLAGS_HI(x)	\
    (((x) >> S_FW_ISCSI_TX_DATA_WR_FLAGS_HI) & M_FW_ISCSI_TX_DATA_WR_FLAGS_HI)

#define S_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_ISO	9
#define M_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_ISO	0x1
#define V_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_ISO(x)	\
    ((x) << S_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_ISO)
#define G_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_ISO(x)	\
    (((x) >> S_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_ISO) & \
     M_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_ISO)
#define F_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_ISO	\
    V_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_ISO(1U)

#define S_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_PI	8
#define M_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_PI	0x1
#define V_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_PI(x)	\
    ((x) << S_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_PI)
#define G_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_PI(x)	\
    (((x) >> S_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_PI) & \
     M_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_PI)
#define F_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_PI	\
    V_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_PI(1U)

#define S_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_DCRC		7
#define M_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_DCRC		0x1
#define V_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_DCRC(x)	\
    ((x) << S_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_DCRC)
#define G_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_DCRC(x)	\
    (((x) >> S_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_DCRC) & \
     M_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_DCRC)
#define F_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_DCRC	\
    V_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_DCRC(1U)

#define S_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_HCRC		6
#define M_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_HCRC		0x1
#define V_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_HCRC(x)	\
    ((x) << S_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_HCRC)
#define G_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_HCRC(x)	\
    (((x) >> S_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_HCRC) & \
     M_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_HCRC)
#define F_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_HCRC	\
    V_FW_ISCSI_TX_DATA_WR_ULPSUBMODE_HCRC(1U)

#define S_FW_ISCSI_TX_DATA_WR_FLAGS_LO		0
#define M_FW_ISCSI_TX_DATA_WR_FLAGS_LO		0x3f
#define V_FW_ISCSI_TX_DATA_WR_FLAGS_LO(x)	\
    ((x) << S_FW_ISCSI_TX_DATA_WR_FLAGS_LO)
#define G_FW_ISCSI_TX_DATA_WR_FLAGS_LO(x)	\
    (((x) >> S_FW_ISCSI_TX_DATA_WR_FLAGS_LO) & M_FW_ISCSI_TX_DATA_WR_FLAGS_LO)

struct fw_cmd_wr {
	__be32 op_dma;
	__be32 len16_pkd;
	__be64 cookie_daddr;
};

#define S_FW_CMD_WR_DMA		17
#define M_FW_CMD_WR_DMA		0x1
#define V_FW_CMD_WR_DMA(x)	((x) << S_FW_CMD_WR_DMA)
#define G_FW_CMD_WR_DMA(x)	(((x) >> S_FW_CMD_WR_DMA) & M_FW_CMD_WR_DMA)
#define F_FW_CMD_WR_DMA	V_FW_CMD_WR_DMA(1U)

struct fw_eth_tx_pkt_vm_wr {
	__be32 op_immdlen;
	__be32 equiq_to_len16;
	__be32 r3[2];
	__u8   ethmacdst[6];
	__u8   ethmacsrc[6];
	__be16 ethtype;
	__be16 vlantci;
};

struct fw_eth_tx_pkts_vm_wr {
	__be32 op_pkd;
	__be32 equiq_to_len16;
	__be32 r3;
	__be16 plen;
	__u8   npkt;
	__u8   r4;
	__u8   ethmacdst[6];
	__u8   ethmacsrc[6];
	__be16 ethtype;
	__be16 vlantci;
};

/******************************************************************************
 *   R I   W O R K   R E Q U E S T s
 **************************************/

enum fw_ri_wr_opcode {
	FW_RI_RDMA_WRITE		= 0x0,	/* IETF RDMAP v1.0 ... */
	FW_RI_READ_REQ			= 0x1,
	FW_RI_READ_RESP			= 0x2,
	FW_RI_SEND			= 0x3,
	FW_RI_SEND_WITH_INV		= 0x4,
	FW_RI_SEND_WITH_SE		= 0x5,
	FW_RI_SEND_WITH_SE_INV		= 0x6,
	FW_RI_TERMINATE			= 0x7,
	FW_RI_RDMA_INIT			= 0x8,	/* CHELSIO RI specific ... */
	FW_RI_BIND_MW			= 0x9,
	FW_RI_FAST_REGISTER		= 0xa,
	FW_RI_LOCAL_INV			= 0xb,
	FW_RI_QP_MODIFY			= 0xc,
	FW_RI_BYPASS			= 0xd,
	FW_RI_RECEIVE			= 0xe,
#if 0
	FW_RI_SEND_IMMEDIATE		= 0x8,
	FW_RI_SEND_IMMEDIATE_WITH_SE	= 0x9,
	FW_RI_ATOMIC_REQUEST		= 0xa,
	FW_RI_ATOMIC_RESPONSE		= 0xb,

	FW_RI_BIND_MW			= 0xc, /* CHELSIO RI specific ... */
	FW_RI_FAST_REGISTER		= 0xd,
	FW_RI_LOCAL_INV			= 0xe,
#endif
	FW_RI_SGE_EC_CR_RETURN		= 0xf,
	FW_RI_WRITE_IMMEDIATE	= FW_RI_RDMA_INIT,
};

enum fw_ri_wr_flags {
	FW_RI_COMPLETION_FLAG		= 0x01,
	FW_RI_NOTIFICATION_FLAG		= 0x02,
	FW_RI_SOLICITED_EVENT_FLAG	= 0x04,
	FW_RI_READ_FENCE_FLAG		= 0x08,
	FW_RI_LOCAL_FENCE_FLAG		= 0x10,
	FW_RI_RDMA_READ_INVALIDATE	= 0x20,
	FW_RI_RDMA_WRITE_WITH_IMMEDIATE	= 0x40
};

enum fw_ri_mpa_attrs {
	FW_RI_MPA_RX_MARKER_ENABLE	= 0x01,
	FW_RI_MPA_TX_MARKER_ENABLE	= 0x02,
	FW_RI_MPA_CRC_ENABLE		= 0x04,
	FW_RI_MPA_IETF_ENABLE		= 0x08
};

enum fw_ri_qp_caps {
	FW_RI_QP_RDMA_READ_ENABLE	= 0x01,
	FW_RI_QP_RDMA_WRITE_ENABLE	= 0x02,
	FW_RI_QP_BIND_ENABLE		= 0x04,
	FW_RI_QP_FAST_REGISTER_ENABLE	= 0x08,
	FW_RI_QP_STAG0_ENABLE		= 0x10,
	FW_RI_QP_RDMA_READ_REQ_0B_ENABLE= 0x80,
};

enum fw_ri_addr_type {
	FW_RI_ZERO_BASED_TO		= 0x00,
	FW_RI_VA_BASED_TO		= 0x01
};

enum fw_ri_mem_perms {
	FW_RI_MEM_ACCESS_REM_WRITE	= 0x01,
	FW_RI_MEM_ACCESS_REM_READ	= 0x02,
	FW_RI_MEM_ACCESS_REM		= 0x03,
	FW_RI_MEM_ACCESS_LOCAL_WRITE	= 0x04,
	FW_RI_MEM_ACCESS_LOCAL_READ	= 0x08,
	FW_RI_MEM_ACCESS_LOCAL		= 0x0C
};

enum fw_ri_stag_type {
	FW_RI_STAG_NSMR			= 0x00,
	FW_RI_STAG_SMR			= 0x01,
	FW_RI_STAG_MW			= 0x02,
	FW_RI_STAG_MW_RELAXED		= 0x03
};

enum fw_ri_data_op {
	FW_RI_DATA_IMMD			= 0x81,
	FW_RI_DATA_DSGL			= 0x82,
	FW_RI_DATA_ISGL			= 0x83
};

enum fw_ri_sgl_depth {
	FW_RI_SGL_DEPTH_MAX_SQ		= 16,
	FW_RI_SGL_DEPTH_MAX_RQ		= 4
};

enum fw_ri_cqe_err {
	FW_RI_CQE_ERR_SUCCESS		= 0x00,	/* success, no error detected */
	FW_RI_CQE_ERR_STAG		= 0x01, /* STAG invalid */
	FW_RI_CQE_ERR_PDID		= 0x02, /* PDID mismatch */
	FW_RI_CQE_ERR_QPID		= 0x03, /* QPID mismatch */
	FW_RI_CQE_ERR_ACCESS		= 0x04, /* Invalid access right */
	FW_RI_CQE_ERR_WRAP		= 0x05, /* Wrap error */
	FW_RI_CQE_ERR_BOUND		= 0x06, /* base and bounds violation */
	FW_RI_CQE_ERR_INVALIDATE_SHARED_MR = 0x07, /* attempt to invalidate a SMR */
	FW_RI_CQE_ERR_INVALIDATE_MR_WITH_MW_BOUND = 0x08, /* attempt to invalidate a MR w MW */
	FW_RI_CQE_ERR_ECC		= 0x09,	/* ECC error detected */
	FW_RI_CQE_ERR_ECC_PSTAG		= 0x0A, /* ECC error detected when reading the PSTAG for a MW Invalidate */
	FW_RI_CQE_ERR_PBL_ADDR_BOUND	= 0x0B, /* pbl address out of bound : software error */
	FW_RI_CQE_ERR_CRC		= 0x10,	/* CRC error */
	FW_RI_CQE_ERR_MARKER		= 0x11,	/* Marker error */
	FW_RI_CQE_ERR_PDU_LEN_ERR	= 0x12,	/* invalid PDU length */
	FW_RI_CQE_ERR_OUT_OF_RQE	= 0x13,	/* out of RQE */
	FW_RI_CQE_ERR_DDP_VERSION	= 0x14,	/* wrong DDP version */
	FW_RI_CQE_ERR_RDMA_VERSION	= 0x15,	/* wrong RDMA version */
	FW_RI_CQE_ERR_OPCODE		= 0x16,	/* invalid rdma opcode */
	FW_RI_CQE_ERR_DDP_QUEUE_NUM	= 0x17,	/* invalid ddp queue number */
	FW_RI_CQE_ERR_MSN		= 0x18, /* MSN error */
	FW_RI_CQE_ERR_TBIT		= 0x19, /* tag bit not set correctly */
	FW_RI_CQE_ERR_MO		= 0x1A, /* MO not zero for TERMINATE or READ_REQ */
	FW_RI_CQE_ERR_MSN_GAP		= 0x1B, /* */
	FW_RI_CQE_ERR_MSN_RANGE		= 0x1C, /* */
	FW_RI_CQE_ERR_IRD_OVERFLOW	= 0x1D, /* */
	FW_RI_CQE_ERR_RQE_ADDR_BOUND	= 0x1E, /*  RQE address out of bound : software error */
	FW_RI_CQE_ERR_INTERNAL_ERR	= 0x1F  /* internel error (opcode mismatch) */

};

struct fw_ri_dsge_pair {
	__be32	len[2];
	__be64	addr[2];
};

struct fw_ri_dsgl {
	__u8	op;
	__u8	r1;
	__be16	nsge;
	__be32	len0;
	__be64	addr0;
#ifndef C99_NOT_SUPPORTED
	struct fw_ri_dsge_pair sge[0];
#endif
};

struct fw_ri_sge {
	__be32 stag;
	__be32 len;
	__be64 to;
};

struct fw_ri_isgl {
	__u8	op;
	__u8	r1;
	__be16	nsge;
	__be32	r2;
#ifndef C99_NOT_SUPPORTED
	struct fw_ri_sge sge[0];
#endif
};

struct fw_ri_immd {
	__u8	op;
	__u8	r1;
	__be16	r2;
	__be32	immdlen;
#ifndef C99_NOT_SUPPORTED
	__u8	data[0];
#endif
};

struct fw_ri_tpte {
	__be32 valid_to_pdid;
	__be32 locread_to_qpid;
	__be32 nosnoop_pbladdr;
	__be32 len_lo;
	__be32 va_hi;
	__be32 va_lo_fbo;
	__be32 dca_mwbcnt_pstag;
	__be32 len_hi;
};

#define S_FW_RI_TPTE_VALID		31
#define M_FW_RI_TPTE_VALID		0x1
#define V_FW_RI_TPTE_VALID(x)		((x) << S_FW_RI_TPTE_VALID)
#define G_FW_RI_TPTE_VALID(x)		\
    (((x) >> S_FW_RI_TPTE_VALID) & M_FW_RI_TPTE_VALID)
#define F_FW_RI_TPTE_VALID		V_FW_RI_TPTE_VALID(1U)

#define S_FW_RI_TPTE_STAGKEY		23
#define M_FW_RI_TPTE_STAGKEY		0xff
#define V_FW_RI_TPTE_STAGKEY(x)		((x) << S_FW_RI_TPTE_STAGKEY)
#define G_FW_RI_TPTE_STAGKEY(x)		\
    (((x) >> S_FW_RI_TPTE_STAGKEY) & M_FW_RI_TPTE_STAGKEY)

#define S_FW_RI_TPTE_STAGSTATE		22
#define M_FW_RI_TPTE_STAGSTATE		0x1
#define V_FW_RI_TPTE_STAGSTATE(x)	((x) << S_FW_RI_TPTE_STAGSTATE)
#define G_FW_RI_TPTE_STAGSTATE(x)	\
    (((x) >> S_FW_RI_TPTE_STAGSTATE) & M_FW_RI_TPTE_STAGSTATE)
#define F_FW_RI_TPTE_STAGSTATE		V_FW_RI_TPTE_STAGSTATE(1U)

#define S_FW_RI_TPTE_STAGTYPE		20
#define M_FW_RI_TPTE_STAGTYPE		0x3
#define V_FW_RI_TPTE_STAGTYPE(x)	((x) << S_FW_RI_TPTE_STAGTYPE)
#define G_FW_RI_TPTE_STAGTYPE(x)	\
    (((x) >> S_FW_RI_TPTE_STAGTYPE) & M_FW_RI_TPTE_STAGTYPE)

#define S_FW_RI_TPTE_PDID		0
#define M_FW_RI_TPTE_PDID		0xfffff
#define V_FW_RI_TPTE_PDID(x)		((x) << S_FW_RI_TPTE_PDID)
#define G_FW_RI_TPTE_PDID(x)		\
    (((x) >> S_FW_RI_TPTE_PDID) & M_FW_RI_TPTE_PDID)

#define S_FW_RI_TPTE_PERM		28
#define M_FW_RI_TPTE_PERM		0xf
#define V_FW_RI_TPTE_PERM(x)		((x) << S_FW_RI_TPTE_PERM)
#define G_FW_RI_TPTE_PERM(x)		\
    (((x) >> S_FW_RI_TPTE_PERM) & M_FW_RI_TPTE_PERM)

#define S_FW_RI_TPTE_REMINVDIS		27
#define M_FW_RI_TPTE_REMINVDIS		0x1
#define V_FW_RI_TPTE_REMINVDIS(x)	((x) << S_FW_RI_TPTE_REMINVDIS)
#define G_FW_RI_TPTE_REMINVDIS(x)	\
    (((x) >> S_FW_RI_TPTE_REMINVDIS) & M_FW_RI_TPTE_REMINVDIS)
#define F_FW_RI_TPTE_REMINVDIS		V_FW_RI_TPTE_REMINVDIS(1U)

#define S_FW_RI_TPTE_ADDRTYPE		26
#define M_FW_RI_TPTE_ADDRTYPE		1
#define V_FW_RI_TPTE_ADDRTYPE(x)	((x) << S_FW_RI_TPTE_ADDRTYPE)
#define G_FW_RI_TPTE_ADDRTYPE(x)	\
    (((x) >> S_FW_RI_TPTE_ADDRTYPE) & M_FW_RI_TPTE_ADDRTYPE)
#define F_FW_RI_TPTE_ADDRTYPE		V_FW_RI_TPTE_ADDRTYPE(1U)

#define S_FW_RI_TPTE_MWBINDEN		25
#define M_FW_RI_TPTE_MWBINDEN		0x1
#define V_FW_RI_TPTE_MWBINDEN(x)	((x) << S_FW_RI_TPTE_MWBINDEN)
#define G_FW_RI_TPTE_MWBINDEN(x)	\
    (((x) >> S_FW_RI_TPTE_MWBINDEN) & M_FW_RI_TPTE_MWBINDEN)
#define F_FW_RI_TPTE_MWBINDEN		V_FW_RI_TPTE_MWBINDEN(1U)

#define S_FW_RI_TPTE_PS			20
#define M_FW_RI_TPTE_PS			0x1f
#define V_FW_RI_TPTE_PS(x)		((x) << S_FW_RI_TPTE_PS)
#define G_FW_RI_TPTE_PS(x)		\
    (((x) >> S_FW_RI_TPTE_PS) & M_FW_RI_TPTE_PS)

#define S_FW_RI_TPTE_QPID		0
#define M_FW_RI_TPTE_QPID		0xfffff
#define V_FW_RI_TPTE_QPID(x)		((x) << S_FW_RI_TPTE_QPID)
#define G_FW_RI_TPTE_QPID(x)		\
    (((x) >> S_FW_RI_TPTE_QPID) & M_FW_RI_TPTE_QPID)

#define S_FW_RI_TPTE_NOSNOOP		31
#define M_FW_RI_TPTE_NOSNOOP		0x1
#define V_FW_RI_TPTE_NOSNOOP(x)		((x) << S_FW_RI_TPTE_NOSNOOP)
#define G_FW_RI_TPTE_NOSNOOP(x)		\
    (((x) >> S_FW_RI_TPTE_NOSNOOP) & M_FW_RI_TPTE_NOSNOOP)
#define F_FW_RI_TPTE_NOSNOOP		V_FW_RI_TPTE_NOSNOOP(1U)

#define S_FW_RI_TPTE_PBLADDR		0
#define M_FW_RI_TPTE_PBLADDR		0x1fffffff
#define V_FW_RI_TPTE_PBLADDR(x)		((x) << S_FW_RI_TPTE_PBLADDR)
#define G_FW_RI_TPTE_PBLADDR(x)		\
    (((x) >> S_FW_RI_TPTE_PBLADDR) & M_FW_RI_TPTE_PBLADDR)

#define S_FW_RI_TPTE_DCA		24
#define M_FW_RI_TPTE_DCA		0x1f
#define V_FW_RI_TPTE_DCA(x)		((x) << S_FW_RI_TPTE_DCA)
#define G_FW_RI_TPTE_DCA(x)		\
    (((x) >> S_FW_RI_TPTE_DCA) & M_FW_RI_TPTE_DCA)

#define S_FW_RI_TPTE_MWBCNT_PSTAG	0
#define M_FW_RI_TPTE_MWBCNT_PSTAG	0xffffff
#define V_FW_RI_TPTE_MWBCNT_PSTAT(x)	\
    ((x) << S_FW_RI_TPTE_MWBCNT_PSTAG)
#define G_FW_RI_TPTE_MWBCNT_PSTAG(x)	\
    (((x) >> S_FW_RI_TPTE_MWBCNT_PSTAG) & M_FW_RI_TPTE_MWBCNT_PSTAG)

enum fw_ri_cqe_rxtx {
	FW_RI_CQE_RXTX_RX = 0x0,
	FW_RI_CQE_RXTX_TX = 0x1,
};

struct fw_ri_cqe {
	union fw_ri_rxtx {
		struct fw_ri_scqe {
		__be32	qpid_n_stat_rxtx_type;
		__be32	plen;
		__be32	stag;
		__be32	wrid;
		} scqe;
		struct fw_ri_rcqe {
		__be32	qpid_n_stat_rxtx_type;
		__be32	plen;
		__be32	stag;
		__be32	msn;
		} rcqe;
		struct fw_ri_rcqe_imm {
		__be32	qpid_n_stat_rxtx_type;
		__be32	plen;
		__be32	mo;
		__be32	msn;
		__u64	imm_data;
		} imm_data_rcqe;
	} u;
};

#define S_FW_RI_CQE_QPID      12
#define M_FW_RI_CQE_QPID      0xfffff
#define V_FW_RI_CQE_QPID(x)   ((x) << S_FW_RI_CQE_QPID)
#define G_FW_RI_CQE_QPID(x)   \
    (((x) >> S_FW_RI_CQE_QPID) &  M_FW_RI_CQE_QPID)

#define S_FW_RI_CQE_NOTIFY    10
#define M_FW_RI_CQE_NOTIFY    0x1
#define V_FW_RI_CQE_NOTIFY(x) ((x) << S_FW_RI_CQE_NOTIFY)
#define G_FW_RI_CQE_NOTIFY(x) \
    (((x) >> S_FW_RI_CQE_NOTIFY) &  M_FW_RI_CQE_NOTIFY)

#define S_FW_RI_CQE_STATUS    5
#define M_FW_RI_CQE_STATUS    0x1f
#define V_FW_RI_CQE_STATUS(x) ((x) << S_FW_RI_CQE_STATUS)
#define G_FW_RI_CQE_STATUS(x) \
    (((x) >> S_FW_RI_CQE_STATUS) &  M_FW_RI_CQE_STATUS)


#define S_FW_RI_CQE_RXTX      4
#define M_FW_RI_CQE_RXTX      0x1
#define V_FW_RI_CQE_RXTX(x)   ((x) << S_FW_RI_CQE_RXTX)
#define G_FW_RI_CQE_RXTX(x)   \
    (((x) >> S_FW_RI_CQE_RXTX) &  M_FW_RI_CQE_RXTX)

#define S_FW_RI_CQE_TYPE      0
#define M_FW_RI_CQE_TYPE      0xf
#define V_FW_RI_CQE_TYPE(x)   ((x) << S_FW_RI_CQE_TYPE)
#define G_FW_RI_CQE_TYPE(x)   \
    (((x) >> S_FW_RI_CQE_TYPE) &  M_FW_RI_CQE_TYPE)

enum fw_ri_res_type {
	FW_RI_RES_TYPE_SQ,
	FW_RI_RES_TYPE_RQ,
	FW_RI_RES_TYPE_CQ,
	FW_RI_RES_TYPE_SRQ,
};

enum fw_ri_res_op {
	FW_RI_RES_OP_WRITE,
	FW_RI_RES_OP_RESET,
};

struct fw_ri_res {
	union fw_ri_restype {
		struct fw_ri_res_sqrq {
			__u8   restype;
			__u8   op;
			__be16 r3;
			__be32 eqid;
			__be32 r4[2];
			__be32 fetchszm_to_iqid;
			__be32 dcaen_to_eqsize;
			__be64 eqaddr;
		} sqrq;
		struct fw_ri_res_cq {
			__u8   restype;
			__u8   op;
			__be16 r3;
			__be32 iqid;
			__be32 r4[2];
			__be32 iqandst_to_iqandstindex;
			__be16 iqdroprss_to_iqesize;
			__be16 iqsize;
			__be64 iqaddr;
			__be32 iqns_iqro;
			__be32 r6_lo;
			__be64 r7;
		} cq;
		struct fw_ri_res_srq {
			__u8   restype;
			__u8   op;
			__be16 r3;
			__be32 eqid;
			__be32 r4[2];
			__be32 fetchszm_to_iqid;
			__be32 dcaen_to_eqsize;
			__be64 eqaddr;
			__be32 srqid;
			__be32 pdid;
			__be32 hwsrqsize;
			__be32 hwsrqaddr;
		} srq;
	} u;
};

struct fw_ri_res_wr {
	__be32 op_nres;
	__be32 len16_pkd;
	__u64  cookie;
#ifndef C99_NOT_SUPPORTED
	struct fw_ri_res res[0];
#endif
};

#define S_FW_RI_RES_WR_VFN		8
#define M_FW_RI_RES_WR_VFN		0xff
#define V_FW_RI_RES_WR_VFN(x)		((x) << S_FW_RI_RES_WR_VFN)
#define G_FW_RI_RES_WR_VFN(x)		\
    (((x) >> S_FW_RI_RES_WR_VFN) & M_FW_RI_RES_WR_VFN)

#define S_FW_RI_RES_WR_NRES	0
#define M_FW_RI_RES_WR_NRES	0xff
#define V_FW_RI_RES_WR_NRES(x)	((x) << S_FW_RI_RES_WR_NRES)
#define G_FW_RI_RES_WR_NRES(x)	\
    (((x) >> S_FW_RI_RES_WR_NRES) & M_FW_RI_RES_WR_NRES)

#define S_FW_RI_RES_WR_FETCHSZM		26
#define M_FW_RI_RES_WR_FETCHSZM		0x1
#define V_FW_RI_RES_WR_FETCHSZM(x)	((x) << S_FW_RI_RES_WR_FETCHSZM)
#define G_FW_RI_RES_WR_FETCHSZM(x)	\
    (((x) >> S_FW_RI_RES_WR_FETCHSZM) & M_FW_RI_RES_WR_FETCHSZM)
#define F_FW_RI_RES_WR_FETCHSZM	V_FW_RI_RES_WR_FETCHSZM(1U)

#define S_FW_RI_RES_WR_STATUSPGNS	25
#define M_FW_RI_RES_WR_STATUSPGNS	0x1
#define V_FW_RI_RES_WR_STATUSPGNS(x)	((x) << S_FW_RI_RES_WR_STATUSPGNS)
#define G_FW_RI_RES_WR_STATUSPGNS(x)	\
    (((x) >> S_FW_RI_RES_WR_STATUSPGNS) & M_FW_RI_RES_WR_STATUSPGNS)
#define F_FW_RI_RES_WR_STATUSPGNS	V_FW_RI_RES_WR_STATUSPGNS(1U)

#define S_FW_RI_RES_WR_STATUSPGRO	24
#define M_FW_RI_RES_WR_STATUSPGRO	0x1
#define V_FW_RI_RES_WR_STATUSPGRO(x)	((x) << S_FW_RI_RES_WR_STATUSPGRO)
#define G_FW_RI_RES_WR_STATUSPGRO(x)	\
    (((x) >> S_FW_RI_RES_WR_STATUSPGRO) & M_FW_RI_RES_WR_STATUSPGRO)
#define F_FW_RI_RES_WR_STATUSPGRO	V_FW_RI_RES_WR_STATUSPGRO(1U)

#define S_FW_RI_RES_WR_FETCHNS		23
#define M_FW_RI_RES_WR_FETCHNS		0x1
#define V_FW_RI_RES_WR_FETCHNS(x)	((x) << S_FW_RI_RES_WR_FETCHNS)
#define G_FW_RI_RES_WR_FETCHNS(x)	\
    (((x) >> S_FW_RI_RES_WR_FETCHNS) & M_FW_RI_RES_WR_FETCHNS)
#define F_FW_RI_RES_WR_FETCHNS	V_FW_RI_RES_WR_FETCHNS(1U)

#define S_FW_RI_RES_WR_FETCHRO		22
#define M_FW_RI_RES_WR_FETCHRO		0x1
#define V_FW_RI_RES_WR_FETCHRO(x)	((x) << S_FW_RI_RES_WR_FETCHRO)
#define G_FW_RI_RES_WR_FETCHRO(x)	\
    (((x) >> S_FW_RI_RES_WR_FETCHRO) & M_FW_RI_RES_WR_FETCHRO)
#define F_FW_RI_RES_WR_FETCHRO	V_FW_RI_RES_WR_FETCHRO(1U)

#define S_FW_RI_RES_WR_HOSTFCMODE	20
#define M_FW_RI_RES_WR_HOSTFCMODE	0x3
#define V_FW_RI_RES_WR_HOSTFCMODE(x)	((x) << S_FW_RI_RES_WR_HOSTFCMODE)
#define G_FW_RI_RES_WR_HOSTFCMODE(x)	\
    (((x) >> S_FW_RI_RES_WR_HOSTFCMODE) & M_FW_RI_RES_WR_HOSTFCMODE)

#define S_FW_RI_RES_WR_CPRIO	19
#define M_FW_RI_RES_WR_CPRIO	0x1
#define V_FW_RI_RES_WR_CPRIO(x)	((x) << S_FW_RI_RES_WR_CPRIO)
#define G_FW_RI_RES_WR_CPRIO(x)	\
    (((x) >> S_FW_RI_RES_WR_CPRIO) & M_FW_RI_RES_WR_CPRIO)
#define F_FW_RI_RES_WR_CPRIO	V_FW_RI_RES_WR_CPRIO(1U)

#define S_FW_RI_RES_WR_ONCHIP		18
#define M_FW_RI_RES_WR_ONCHIP		0x1
#define V_FW_RI_RES_WR_ONCHIP(x)	((x) << S_FW_RI_RES_WR_ONCHIP)
#define G_FW_RI_RES_WR_ONCHIP(x)	\
    (((x) >> S_FW_RI_RES_WR_ONCHIP) & M_FW_RI_RES_WR_ONCHIP)
#define F_FW_RI_RES_WR_ONCHIP	V_FW_RI_RES_WR_ONCHIP(1U)

#define S_FW_RI_RES_WR_PCIECHN		16
#define M_FW_RI_RES_WR_PCIECHN		0x3
#define V_FW_RI_RES_WR_PCIECHN(x)	((x) << S_FW_RI_RES_WR_PCIECHN)
#define G_FW_RI_RES_WR_PCIECHN(x)	\
    (((x) >> S_FW_RI_RES_WR_PCIECHN) & M_FW_RI_RES_WR_PCIECHN)

#define S_FW_RI_RES_WR_IQID	0
#define M_FW_RI_RES_WR_IQID	0xffff
#define V_FW_RI_RES_WR_IQID(x)	((x) << S_FW_RI_RES_WR_IQID)
#define G_FW_RI_RES_WR_IQID(x)	\
    (((x) >> S_FW_RI_RES_WR_IQID) & M_FW_RI_RES_WR_IQID)

#define S_FW_RI_RES_WR_DCAEN	31
#define M_FW_RI_RES_WR_DCAEN	0x1
#define V_FW_RI_RES_WR_DCAEN(x)	((x) << S_FW_RI_RES_WR_DCAEN)
#define G_FW_RI_RES_WR_DCAEN(x)	\
    (((x) >> S_FW_RI_RES_WR_DCAEN) & M_FW_RI_RES_WR_DCAEN)
#define F_FW_RI_RES_WR_DCAEN	V_FW_RI_RES_WR_DCAEN(1U)

#define S_FW_RI_RES_WR_DCACPU		26
#define M_FW_RI_RES_WR_DCACPU		0x1f
#define V_FW_RI_RES_WR_DCACPU(x)	((x) << S_FW_RI_RES_WR_DCACPU)
#define G_FW_RI_RES_WR_DCACPU(x)	\
    (((x) >> S_FW_RI_RES_WR_DCACPU) & M_FW_RI_RES_WR_DCACPU)

#define S_FW_RI_RES_WR_FBMIN	23
#define M_FW_RI_RES_WR_FBMIN	0x7
#define V_FW_RI_RES_WR_FBMIN(x)	((x) << S_FW_RI_RES_WR_FBMIN)
#define G_FW_RI_RES_WR_FBMIN(x)	\
    (((x) >> S_FW_RI_RES_WR_FBMIN) & M_FW_RI_RES_WR_FBMIN)

#define S_FW_RI_RES_WR_FBMAX	20
#define M_FW_RI_RES_WR_FBMAX	0x7
#define V_FW_RI_RES_WR_FBMAX(x)	((x) << S_FW_RI_RES_WR_FBMAX)
#define G_FW_RI_RES_WR_FBMAX(x)	\
    (((x) >> S_FW_RI_RES_WR_FBMAX) & M_FW_RI_RES_WR_FBMAX)

#define S_FW_RI_RES_WR_CIDXFTHRESHO	19
#define M_FW_RI_RES_WR_CIDXFTHRESHO	0x1
#define V_FW_RI_RES_WR_CIDXFTHRESHO(x)	((x) << S_FW_RI_RES_WR_CIDXFTHRESHO)
#define G_FW_RI_RES_WR_CIDXFTHRESHO(x)	\
    (((x) >> S_FW_RI_RES_WR_CIDXFTHRESHO) & M_FW_RI_RES_WR_CIDXFTHRESHO)
#define F_FW_RI_RES_WR_CIDXFTHRESHO	V_FW_RI_RES_WR_CIDXFTHRESHO(1U)

#define S_FW_RI_RES_WR_CIDXFTHRESH	16
#define M_FW_RI_RES_WR_CIDXFTHRESH	0x7
#define V_FW_RI_RES_WR_CIDXFTHRESH(x)	((x) << S_FW_RI_RES_WR_CIDXFTHRESH)
#define G_FW_RI_RES_WR_CIDXFTHRESH(x)	\
    (((x) >> S_FW_RI_RES_WR_CIDXFTHRESH) & M_FW_RI_RES_WR_CIDXFTHRESH)

#define S_FW_RI_RES_WR_EQSIZE		0
#define M_FW_RI_RES_WR_EQSIZE		0xffff
#define V_FW_RI_RES_WR_EQSIZE(x)	((x) << S_FW_RI_RES_WR_EQSIZE)
#define G_FW_RI_RES_WR_EQSIZE(x)	\
    (((x) >> S_FW_RI_RES_WR_EQSIZE) & M_FW_RI_RES_WR_EQSIZE)

#define S_FW_RI_RES_WR_IQANDST		15
#define M_FW_RI_RES_WR_IQANDST		0x1
#define V_FW_RI_RES_WR_IQANDST(x)	((x) << S_FW_RI_RES_WR_IQANDST)
#define G_FW_RI_RES_WR_IQANDST(x)	\
    (((x) >> S_FW_RI_RES_WR_IQANDST) & M_FW_RI_RES_WR_IQANDST)
#define F_FW_RI_RES_WR_IQANDST	V_FW_RI_RES_WR_IQANDST(1U)

#define S_FW_RI_RES_WR_IQANUS		14
#define M_FW_RI_RES_WR_IQANUS		0x1
#define V_FW_RI_RES_WR_IQANUS(x)	((x) << S_FW_RI_RES_WR_IQANUS)
#define G_FW_RI_RES_WR_IQANUS(x)	\
    (((x) >> S_FW_RI_RES_WR_IQANUS) & M_FW_RI_RES_WR_IQANUS)
#define F_FW_RI_RES_WR_IQANUS	V_FW_RI_RES_WR_IQANUS(1U)

#define S_FW_RI_RES_WR_IQANUD		12
#define M_FW_RI_RES_WR_IQANUD		0x3
#define V_FW_RI_RES_WR_IQANUD(x)	((x) << S_FW_RI_RES_WR_IQANUD)
#define G_FW_RI_RES_WR_IQANUD(x)	\
    (((x) >> S_FW_RI_RES_WR_IQANUD) & M_FW_RI_RES_WR_IQANUD)

#define S_FW_RI_RES_WR_IQANDSTINDEX	0
#define M_FW_RI_RES_WR_IQANDSTINDEX	0xfff
#define V_FW_RI_RES_WR_IQANDSTINDEX(x)	((x) << S_FW_RI_RES_WR_IQANDSTINDEX)
#define G_FW_RI_RES_WR_IQANDSTINDEX(x)	\
    (((x) >> S_FW_RI_RES_WR_IQANDSTINDEX) & M_FW_RI_RES_WR_IQANDSTINDEX)

#define S_FW_RI_RES_WR_IQDROPRSS	15
#define M_FW_RI_RES_WR_IQDROPRSS	0x1
#define V_FW_RI_RES_WR_IQDROPRSS(x)	((x) << S_FW_RI_RES_WR_IQDROPRSS)
#define G_FW_RI_RES_WR_IQDROPRSS(x)	\
    (((x) >> S_FW_RI_RES_WR_IQDROPRSS) & M_FW_RI_RES_WR_IQDROPRSS)
#define F_FW_RI_RES_WR_IQDROPRSS	V_FW_RI_RES_WR_IQDROPRSS(1U)

#define S_FW_RI_RES_WR_IQGTSMODE	14
#define M_FW_RI_RES_WR_IQGTSMODE	0x1
#define V_FW_RI_RES_WR_IQGTSMODE(x)	((x) << S_FW_RI_RES_WR_IQGTSMODE)
#define G_FW_RI_RES_WR_IQGTSMODE(x)	\
    (((x) >> S_FW_RI_RES_WR_IQGTSMODE) & M_FW_RI_RES_WR_IQGTSMODE)
#define F_FW_RI_RES_WR_IQGTSMODE	V_FW_RI_RES_WR_IQGTSMODE(1U)

#define S_FW_RI_RES_WR_IQPCIECH		12
#define M_FW_RI_RES_WR_IQPCIECH		0x3
#define V_FW_RI_RES_WR_IQPCIECH(x)	((x) << S_FW_RI_RES_WR_IQPCIECH)
#define G_FW_RI_RES_WR_IQPCIECH(x)	\
    (((x) >> S_FW_RI_RES_WR_IQPCIECH) & M_FW_RI_RES_WR_IQPCIECH)

#define S_FW_RI_RES_WR_IQDCAEN		11
#define M_FW_RI_RES_WR_IQDCAEN		0x1
#define V_FW_RI_RES_WR_IQDCAEN(x)	((x) << S_FW_RI_RES_WR_IQDCAEN)
#define G_FW_RI_RES_WR_IQDCAEN(x)	\
    (((x) >> S_FW_RI_RES_WR_IQDCAEN) & M_FW_RI_RES_WR_IQDCAEN)
#define F_FW_RI_RES_WR_IQDCAEN	V_FW_RI_RES_WR_IQDCAEN(1U)

#define S_FW_RI_RES_WR_IQDCACPU		6
#define M_FW_RI_RES_WR_IQDCACPU		0x1f
#define V_FW_RI_RES_WR_IQDCACPU(x)	((x) << S_FW_RI_RES_WR_IQDCACPU)
#define G_FW_RI_RES_WR_IQDCACPU(x)	\
    (((x) >> S_FW_RI_RES_WR_IQDCACPU) & M_FW_RI_RES_WR_IQDCACPU)

#define S_FW_RI_RES_WR_IQINTCNTTHRESH		4
#define M_FW_RI_RES_WR_IQINTCNTTHRESH		0x3
#define V_FW_RI_RES_WR_IQINTCNTTHRESH(x)	\
    ((x) << S_FW_RI_RES_WR_IQINTCNTTHRESH)
#define G_FW_RI_RES_WR_IQINTCNTTHRESH(x)	\
    (((x) >> S_FW_RI_RES_WR_IQINTCNTTHRESH) & M_FW_RI_RES_WR_IQINTCNTTHRESH)

#define S_FW_RI_RES_WR_IQO	3
#define M_FW_RI_RES_WR_IQO	0x1
#define V_FW_RI_RES_WR_IQO(x)	((x) << S_FW_RI_RES_WR_IQO)
#define G_FW_RI_RES_WR_IQO(x)	\
    (((x) >> S_FW_RI_RES_WR_IQO) & M_FW_RI_RES_WR_IQO)
#define F_FW_RI_RES_WR_IQO	V_FW_RI_RES_WR_IQO(1U)

#define S_FW_RI_RES_WR_IQCPRIO		2
#define M_FW_RI_RES_WR_IQCPRIO		0x1
#define V_FW_RI_RES_WR_IQCPRIO(x)	((x) << S_FW_RI_RES_WR_IQCPRIO)
#define G_FW_RI_RES_WR_IQCPRIO(x)	\
    (((x) >> S_FW_RI_RES_WR_IQCPRIO) & M_FW_RI_RES_WR_IQCPRIO)
#define F_FW_RI_RES_WR_IQCPRIO	V_FW_RI_RES_WR_IQCPRIO(1U)

#define S_FW_RI_RES_WR_IQESIZE		0
#define M_FW_RI_RES_WR_IQESIZE		0x3
#define V_FW_RI_RES_WR_IQESIZE(x)	((x) << S_FW_RI_RES_WR_IQESIZE)
#define G_FW_RI_RES_WR_IQESIZE(x)	\
    (((x) >> S_FW_RI_RES_WR_IQESIZE) & M_FW_RI_RES_WR_IQESIZE)

#define S_FW_RI_RES_WR_IQNS	31
#define M_FW_RI_RES_WR_IQNS	0x1
#define V_FW_RI_RES_WR_IQNS(x)	((x) << S_FW_RI_RES_WR_IQNS)
#define G_FW_RI_RES_WR_IQNS(x)	\
    (((x) >> S_FW_RI_RES_WR_IQNS) & M_FW_RI_RES_WR_IQNS)
#define F_FW_RI_RES_WR_IQNS	V_FW_RI_RES_WR_IQNS(1U)

#define S_FW_RI_RES_WR_IQRO	30
#define M_FW_RI_RES_WR_IQRO	0x1
#define V_FW_RI_RES_WR_IQRO(x)	((x) << S_FW_RI_RES_WR_IQRO)
#define G_FW_RI_RES_WR_IQRO(x)	\
    (((x) >> S_FW_RI_RES_WR_IQRO) & M_FW_RI_RES_WR_IQRO)
#define F_FW_RI_RES_WR_IQRO	V_FW_RI_RES_WR_IQRO(1U)

struct fw_ri_rdma_write_wr {
	__u8   opcode;
	__u8   flags;
	__u16  wrid;
	__u8   r1[3];
	__u8   len16;
	__u64  immd_data;
	__be32 plen;
	__be32 stag_sink;
	__be64 to_sink;
#ifndef C99_NOT_SUPPORTED
	union {
		struct fw_ri_immd immd_src[0];
		struct fw_ri_isgl isgl_src[0];
	} u;
#endif
};

struct fw_ri_send_wr {
	__u8   opcode;
	__u8   flags;
	__u16  wrid;
	__u8   r1[3];
	__u8   len16;
	__be32 sendop_pkd;
	__be32 stag_inv;
	__be32 plen;
	__be32 r3;
	__be64 r4;
#ifndef C99_NOT_SUPPORTED
	union {
		struct fw_ri_immd immd_src[0];
		struct fw_ri_isgl isgl_src[0];
	} u;
#endif
};

#define S_FW_RI_SEND_WR_SENDOP		0
#define M_FW_RI_SEND_WR_SENDOP		0xf
#define V_FW_RI_SEND_WR_SENDOP(x)	((x) << S_FW_RI_SEND_WR_SENDOP)
#define G_FW_RI_SEND_WR_SENDOP(x)	\
    (((x) >> S_FW_RI_SEND_WR_SENDOP) & M_FW_RI_SEND_WR_SENDOP)

struct fw_ri_rdma_write_cmpl_wr {
	__u8   opcode;
	__u8   flags;
	__u16  wrid;
	__u8   r1[3];
	__u8   len16;
	__u8   r2;
	__u8   flags_send;
	__u16  wrid_send;
	__be32 stag_inv;
	__be32 plen;
	__be32 stag_sink;
	__be64 to_sink;
	union fw_ri_cmpl {
		struct fw_ri_immd_cmpl {
			__u8   op;
			__u8   r1[6];
			__u8   immdlen;
			__u8   data[16];
		} immd_src;
		struct fw_ri_isgl isgl_src;
	} u_cmpl;
	__be64 r3;
#ifndef C99_NOT_SUPPORTED
	union fw_ri_write {
		struct fw_ri_immd immd_src[0];
		struct fw_ri_isgl isgl_src[0];
	} u;
#endif
};

struct fw_ri_rdma_read_wr {
	__u8   opcode;
	__u8   flags;
	__u16  wrid;
	__u8   r1[3];
	__u8   len16;
	__be64 r2;
	__be32 stag_sink;
	__be32 to_sink_hi;
	__be32 to_sink_lo;
	__be32 plen;
	__be32 stag_src;
	__be32 to_src_hi;
	__be32 to_src_lo;
	__be32 r5;
};

struct fw_ri_recv_wr {
	__u8   opcode;
	__u8   r1;
	__u16  wrid;
	__u8   r2[3];
	__u8   len16;
	struct fw_ri_isgl isgl;
};

struct fw_ri_bind_mw_wr {
	__u8   opcode;
	__u8   flags;
	__u16  wrid;
	__u8   r1[3];
	__u8   len16;
	__u8   qpbinde_to_dcacpu;
	__u8   pgsz_shift;
	__u8   addr_type;
	__u8   mem_perms;
	__be32 stag_mr;
	__be32 stag_mw;
	__be32 r3;
	__be64 len_mw;
	__be64 va_fbo;
	__be64 r4;
};

#define S_FW_RI_BIND_MW_WR_QPBINDE	6
#define M_FW_RI_BIND_MW_WR_QPBINDE	0x1
#define V_FW_RI_BIND_MW_WR_QPBINDE(x)	((x) << S_FW_RI_BIND_MW_WR_QPBINDE)
#define G_FW_RI_BIND_MW_WR_QPBINDE(x)	\
    (((x) >> S_FW_RI_BIND_MW_WR_QPBINDE) & M_FW_RI_BIND_MW_WR_QPBINDE)
#define F_FW_RI_BIND_MW_WR_QPBINDE	V_FW_RI_BIND_MW_WR_QPBINDE(1U)

#define S_FW_RI_BIND_MW_WR_NS		5
#define M_FW_RI_BIND_MW_WR_NS		0x1
#define V_FW_RI_BIND_MW_WR_NS(x)	((x) << S_FW_RI_BIND_MW_WR_NS)
#define G_FW_RI_BIND_MW_WR_NS(x)	\
    (((x) >> S_FW_RI_BIND_MW_WR_NS) & M_FW_RI_BIND_MW_WR_NS)
#define F_FW_RI_BIND_MW_WR_NS	V_FW_RI_BIND_MW_WR_NS(1U)

#define S_FW_RI_BIND_MW_WR_DCACPU	0
#define M_FW_RI_BIND_MW_WR_DCACPU	0x1f
#define V_FW_RI_BIND_MW_WR_DCACPU(x)	((x) << S_FW_RI_BIND_MW_WR_DCACPU)
#define G_FW_RI_BIND_MW_WR_DCACPU(x)	\
    (((x) >> S_FW_RI_BIND_MW_WR_DCACPU) & M_FW_RI_BIND_MW_WR_DCACPU)

struct fw_ri_fr_nsmr_wr {
	__u8   opcode;
	__u8   flags;
	__u16  wrid;
	__u8   r1[3];
	__u8   len16;
	__u8   qpbinde_to_dcacpu;
	__u8   pgsz_shift;
	__u8   addr_type;
	__u8   mem_perms;
	__be32 stag;
	__be32 len_hi;
	__be32 len_lo;
	__be32 va_hi;
	__be32 va_lo_fbo;
};

#define S_FW_RI_FR_NSMR_WR_QPBINDE	6
#define M_FW_RI_FR_NSMR_WR_QPBINDE	0x1
#define V_FW_RI_FR_NSMR_WR_QPBINDE(x)	((x) << S_FW_RI_FR_NSMR_WR_QPBINDE)
#define G_FW_RI_FR_NSMR_WR_QPBINDE(x)	\
    (((x) >> S_FW_RI_FR_NSMR_WR_QPBINDE) & M_FW_RI_FR_NSMR_WR_QPBINDE)
#define F_FW_RI_FR_NSMR_WR_QPBINDE	V_FW_RI_FR_NSMR_WR_QPBINDE(1U)

#define S_FW_RI_FR_NSMR_WR_NS		5
#define M_FW_RI_FR_NSMR_WR_NS		0x1
#define V_FW_RI_FR_NSMR_WR_NS(x)	((x) << S_FW_RI_FR_NSMR_WR_NS)
#define G_FW_RI_FR_NSMR_WR_NS(x)	\
    (((x) >> S_FW_RI_FR_NSMR_WR_NS) & M_FW_RI_FR_NSMR_WR_NS)
#define F_FW_RI_FR_NSMR_WR_NS	V_FW_RI_FR_NSMR_WR_NS(1U)

#define S_FW_RI_FR_NSMR_WR_DCACPU	0
#define M_FW_RI_FR_NSMR_WR_DCACPU	0x1f
#define V_FW_RI_FR_NSMR_WR_DCACPU(x)	((x) << S_FW_RI_FR_NSMR_WR_DCACPU)
#define G_FW_RI_FR_NSMR_WR_DCACPU(x)	\
    (((x) >> S_FW_RI_FR_NSMR_WR_DCACPU) & M_FW_RI_FR_NSMR_WR_DCACPU)

struct fw_ri_fr_nsmr_tpte_wr {
	__u8   opcode;
	__u8   flags;
	__u16  wrid;
	__u8   r1[3];
	__u8   len16;
	__be32 r2;
	__be32 stag;
	struct fw_ri_tpte tpte;
	__be64 pbl[2];
};

struct fw_ri_inv_lstag_wr {
	__u8   opcode;
	__u8   flags;
	__u16  wrid;
	__u8   r1[3];
	__u8   len16;
	__be32 r2;
	__be32 stag_inv;
};

struct fw_ri_send_immediate_wr {
	__u8   opcode;
	__u8   flags;
	__u16  wrid;
	__u8   r1[3];
	__u8   len16;
	__be32 sendimmop_pkd;
	__be32 r3;
	__be32 plen;
	__be32 r4;
	__be64 r5;
#ifndef C99_NOT_SUPPORTED
	struct fw_ri_immd immd_src[0];
#endif
};

#define S_FW_RI_SEND_IMMEDIATE_WR_SENDIMMOP	0
#define M_FW_RI_SEND_IMMEDIATE_WR_SENDIMMOP	0xf
#define V_FW_RI_SEND_IMMEDIATE_WR_SENDIMMOP(x)	\
    ((x) << S_FW_RI_SEND_IMMEDIATE_WR_SENDIMMOP)
#define G_FW_RI_SEND_IMMEDIATE_WR_SENDIMMOP(x)	\
    (((x) >> S_FW_RI_SEND_IMMEDIATE_WR_SENDIMMOP) & \
     M_FW_RI_SEND_IMMEDIATE_WR_SENDIMMOP)

enum fw_ri_atomic_op {
	FW_RI_ATOMIC_OP_FETCHADD,
	FW_RI_ATOMIC_OP_SWAP,
	FW_RI_ATOMIC_OP_CMDSWAP,
};

struct fw_ri_atomic_wr {
	__u8   opcode;
	__u8   flags;
	__u16  wrid;
	__u8   r1[3];
	__u8   len16;
	__be32 atomicop_pkd;
	__be64 r3;
	__be32 aopcode_pkd;
	__be32 reqid;
	__be32 stag;
	__be32 to_hi;
	__be32 to_lo;
	__be32 addswap_data_hi;
	__be32 addswap_data_lo;
	__be32 addswap_mask_hi;
	__be32 addswap_mask_lo;
	__be32 compare_data_hi;
	__be32 compare_data_lo;
	__be32 compare_mask_hi;
	__be32 compare_mask_lo;
	__be32 r5;
};

#define S_FW_RI_ATOMIC_WR_ATOMICOP	0
#define M_FW_RI_ATOMIC_WR_ATOMICOP	0xf
#define V_FW_RI_ATOMIC_WR_ATOMICOP(x)	((x) << S_FW_RI_ATOMIC_WR_ATOMICOP)
#define G_FW_RI_ATOMIC_WR_ATOMICOP(x)	\
    (((x) >> S_FW_RI_ATOMIC_WR_ATOMICOP) & M_FW_RI_ATOMIC_WR_ATOMICOP)

#define S_FW_RI_ATOMIC_WR_AOPCODE	0
#define M_FW_RI_ATOMIC_WR_AOPCODE	0xf
#define V_FW_RI_ATOMIC_WR_AOPCODE(x)	((x) << S_FW_RI_ATOMIC_WR_AOPCODE)
#define G_FW_RI_ATOMIC_WR_AOPCODE(x)	\
    (((x) >> S_FW_RI_ATOMIC_WR_AOPCODE) & M_FW_RI_ATOMIC_WR_AOPCODE)

enum fw_ri_type {
	FW_RI_TYPE_INIT,
	FW_RI_TYPE_FINI,
	FW_RI_TYPE_TERMINATE
};

enum fw_ri_init_p2ptype {
	FW_RI_INIT_P2PTYPE_RDMA_WRITE		= FW_RI_RDMA_WRITE,
	FW_RI_INIT_P2PTYPE_READ_REQ		= FW_RI_READ_REQ,
	FW_RI_INIT_P2PTYPE_SEND			= FW_RI_SEND,
	FW_RI_INIT_P2PTYPE_SEND_WITH_INV	= FW_RI_SEND_WITH_INV,
	FW_RI_INIT_P2PTYPE_SEND_WITH_SE		= FW_RI_SEND_WITH_SE,
	FW_RI_INIT_P2PTYPE_SEND_WITH_SE_INV	= FW_RI_SEND_WITH_SE_INV,
	FW_RI_INIT_P2PTYPE_DISABLED		= 0xf,
};

enum fw_ri_init_rqeqid_srq {
	FW_RI_INIT_RQEQID_SRQ			= 1 << 31,
};

struct fw_ri_wr {
	__be32 op_compl;
	__be32 flowid_len16;
	__u64  cookie;
	union fw_ri {
		struct fw_ri_init {
			__u8   type;
			__u8   mpareqbit_p2ptype;
			__u8   r4[2];
			__u8   mpa_attrs;
			__u8   qp_caps;
			__be16 nrqe;
			__be32 pdid;
			__be32 qpid;
			__be32 sq_eqid;
			__be32 rq_eqid;
			__be32 scqid;
			__be32 rcqid;
			__be32 ord_max;
			__be32 ird_max;
			__be32 iss;
			__be32 irs;
			__be32 hwrqsize;
			__be32 hwrqaddr;
			__be64 r5;
			union fw_ri_init_p2p {
				struct fw_ri_rdma_write_wr write;
				struct fw_ri_rdma_read_wr read;
				struct fw_ri_send_wr send;
			} u;
		} init;
		struct fw_ri_fini {
			__u8   type;
			__u8   r3[7];
			__be64 r4;
		} fini;
		struct fw_ri_terminate {
			__u8   type;
			__u8   r3[3];
			__be32 immdlen;
			__u8   termmsg[40];
		} terminate;
	} u;
};

#define S_FW_RI_WR_MPAREQBIT	7
#define M_FW_RI_WR_MPAREQBIT	0x1
#define V_FW_RI_WR_MPAREQBIT(x)	((x) << S_FW_RI_WR_MPAREQBIT)
#define G_FW_RI_WR_MPAREQBIT(x)	\
    (((x) >> S_FW_RI_WR_MPAREQBIT) & M_FW_RI_WR_MPAREQBIT)
#define F_FW_RI_WR_MPAREQBIT	V_FW_RI_WR_MPAREQBIT(1U)

#define S_FW_RI_WR_0BRRBIT	6
#define M_FW_RI_WR_0BRRBIT	0x1
#define V_FW_RI_WR_0BRRBIT(x)	((x) << S_FW_RI_WR_0BRRBIT)
#define G_FW_RI_WR_0BRRBIT(x)	\
    (((x) >> S_FW_RI_WR_0BRRBIT) & M_FW_RI_WR_0BRRBIT)
#define F_FW_RI_WR_0BRRBIT	V_FW_RI_WR_0BRRBIT(1U)

#define S_FW_RI_WR_P2PTYPE	0
#define M_FW_RI_WR_P2PTYPE	0xf
#define V_FW_RI_WR_P2PTYPE(x)	((x) << S_FW_RI_WR_P2PTYPE)
#define G_FW_RI_WR_P2PTYPE(x)	\
    (((x) >> S_FW_RI_WR_P2PTYPE) & M_FW_RI_WR_P2PTYPE)

/******************************************************************************
 *  F O i S C S I   W O R K R E Q U E S T s
 *********************************************/

#define	FW_FOISCSI_NAME_MAX_LEN		224
#define	FW_FOISCSI_ALIAS_MAX_LEN	224
#define	FW_FOISCSI_KEY_MAX_LEN	64
#define	FW_FOISCSI_VAL_MAX_LEN	256
#define FW_FOISCSI_CHAP_SEC_MAX_LEN	128
#define	FW_FOISCSI_INIT_NODE_MAX	8

enum fw_chnet_ifconf_wr_subop {
	FW_CHNET_IFCONF_WR_SUBOP_NONE = 0,

	FW_CHNET_IFCONF_WR_SUBOP_IPV4_SET,
	FW_CHNET_IFCONF_WR_SUBOP_IPV4_GET,

	FW_CHNET_IFCONF_WR_SUBOP_VLAN_IPV4_SET,
	FW_CHNET_IFCONF_WR_SUBOP_VLAN_IPV4_GET,

	FW_CHNET_IFCONF_WR_SUBOP_IPV6_SET,
	FW_CHNET_IFCONF_WR_SUBOP_IPV6_GET,

	FW_CHNET_IFCONF_WR_SUBOP_VLAN_SET,
	FW_CHNET_IFCONF_WR_SUBOP_VLAN_GET,

	FW_CHNET_IFCONF_WR_SUBOP_MTU_SET,
	FW_CHNET_IFCONF_WR_SUBOP_MTU_GET,

	FW_CHNET_IFCONF_WR_SUBOP_DHCP_SET,
	FW_CHNET_IFCONF_WR_SUBOP_DHCP_GET,

	FW_CHNET_IFCONF_WR_SUBOP_DHCPV6_SET,
	FW_CHNET_IFCONF_WR_SUBOP_DHCPV6_GET,

	FW_CHNET_IFCONF_WR_SUBOP_LINKLOCAL_ADDR_SET,
	FW_CHNET_IFCONF_WR_SUBOP_RA_BASED_ADDR_SET,
	FW_CHNET_IFCONF_WR_SUBOP_ADDR_EXPIRED,

	FW_CHNET_IFCONF_WR_SUBOP_ICMP_PING4,
	FW_CHNET_IFCONF_WR_SUBOP_ICMP_PING6,

	FW_CHNET_IFCONF_WR_SUBOP_MAX,
};

struct fw_chnet_ifconf_wr {
	__be32 op_compl;
	__be32 flowid_len16;
	__u64  cookie;
	__be32 if_flowid;
	__u8   idx;
	__u8   subop;
	__u8   retval;
	__u8   r2;
	union {
		__be64 r3;
		struct fw_chnet_ifconf_ping {
			__be16 ping_time;
			__u8   ping_rsptype;
			__u8   ping_param_rspcode_to_fin_bit;
			__u8   ping_pktsize;
			__u8   ping_ttl;
			__be16 ping_seq;
		} ping;
		struct fw_chnet_ifconf_mac {
			__u8   peer_mac[6];
			__u8   smac_idx;
		} mac;
	} u;
	struct fw_chnet_ifconf_params {
		__be32 r0;
		__be16 vlanid;
		__be16 mtu;
		union fw_chnet_ifconf_addr_type {
			struct fw_chnet_ifconf_ipv4 {
				__be32 addr;
				__be32 mask;
				__be32 router;
				__be32 r0;
				__be64 r1;
			} ipv4;
			struct fw_chnet_ifconf_ipv6 {
				__u8   prefix_len;
				__u8   r0;
				__be16 r1;
				__be32 r2;
				__be64 addr_hi;
				__be64 addr_lo;
				__be64 router_hi;
				__be64 router_lo;
			} ipv6;
		} in_attr;
	} param;
};

#define S_FW_CHNET_IFCONF_WR_PING_MACBIT	1
#define M_FW_CHNET_IFCONF_WR_PING_MACBIT	0x1
#define V_FW_CHNET_IFCONF_WR_PING_MACBIT(x)	\
    ((x) << S_FW_CHNET_IFCONF_WR_PING_MACBIT)
#define G_FW_CHNET_IFCONF_WR_PING_MACBIT(x)	\
    (((x) >> S_FW_CHNET_IFCONF_WR_PING_MACBIT) & \
     M_FW_CHNET_IFCONF_WR_PING_MACBIT)
#define F_FW_CHNET_IFCONF_WR_PING_MACBIT	\
    V_FW_CHNET_IFCONF_WR_PING_MACBIT(1U)

#define S_FW_CHNET_IFCONF_WR_FIN_BIT	0
#define M_FW_CHNET_IFCONF_WR_FIN_BIT	0x1
#define V_FW_CHNET_IFCONF_WR_FIN_BIT(x)	((x) << S_FW_CHNET_IFCONF_WR_FIN_BIT)
#define G_FW_CHNET_IFCONF_WR_FIN_BIT(x)	\
    (((x) >> S_FW_CHNET_IFCONF_WR_FIN_BIT) & M_FW_CHNET_IFCONF_WR_FIN_BIT)
#define F_FW_CHNET_IFCONF_WR_FIN_BIT	V_FW_CHNET_IFCONF_WR_FIN_BIT(1U)

enum fw_foiscsi_node_type {
	FW_FOISCSI_NODE_TYPE_INITIATOR = 0,
	FW_FOISCSI_NODE_TYPE_TARGET,
};

enum fw_foiscsi_session_type {
	FW_FOISCSI_SESSION_TYPE_DISCOVERY = 0,
	FW_FOISCSI_SESSION_TYPE_NORMAL,
};

enum fw_foiscsi_auth_policy {
	FW_FOISCSI_AUTH_POLICY_ONEWAY = 0,
	FW_FOISCSI_AUTH_POLICY_MUTUAL,
};

enum fw_foiscsi_auth_method {
	FW_FOISCSI_AUTH_METHOD_NONE = 0,
	FW_FOISCSI_AUTH_METHOD_CHAP,
	FW_FOISCSI_AUTH_METHOD_CHAP_FST,
	FW_FOISCSI_AUTH_METHOD_CHAP_SEC,
};

enum fw_foiscsi_digest_type {
	FW_FOISCSI_DIGEST_TYPE_NONE = 0,
	FW_FOISCSI_DIGEST_TYPE_CRC32,
	FW_FOISCSI_DIGEST_TYPE_CRC32_FST,
	FW_FOISCSI_DIGEST_TYPE_CRC32_SEC,
};

enum fw_foiscsi_wr_subop {
	FW_FOISCSI_WR_SUBOP_ADD = 1,
	FW_FOISCSI_WR_SUBOP_DEL = 2,
	FW_FOISCSI_WR_SUBOP_MOD = 4,
};

enum fw_coiscsi_stats_wr_subop {
	FW_COISCSI_WR_SUBOP_TOT = 1,
	FW_COISCSI_WR_SUBOP_MAX = 2,
	FW_COISCSI_WR_SUBOP_CUR = 3,
	FW_COISCSI_WR_SUBOP_CLR = 4,
};

enum fw_foiscsi_ctrl_state {
	FW_FOISCSI_CTRL_STATE_FREE = 0,
	FW_FOISCSI_CTRL_STATE_ONLINE = 1,
	FW_FOISCSI_CTRL_STATE_FAILED,
	FW_FOISCSI_CTRL_STATE_IN_RECOVERY,
	FW_FOISCSI_CTRL_STATE_REDIRECT,
};

struct fw_rdev_wr {
	__be32 op_to_immdlen;
	__be32 alloc_to_len16;
	__be64 cookie;
	__u8   protocol;
	__u8   event_cause;
	__u8   cur_state;
	__u8   prev_state;
	__be32 flags_to_assoc_flowid;
	union rdev_entry {
		struct fcoe_rdev_entry {
			__be32 flowid;
			__u8   protocol;
			__u8   event_cause;
			__u8   flags;
			__u8   rjt_reason;
			__u8   cur_login_st;
			__u8   prev_login_st;
			__be16 rcv_fr_sz;
			__u8   rd_xfer_rdy_to_rport_type;
			__u8   vft_to_qos;
			__u8   org_proc_assoc_to_acc_rsp_code;
			__u8   enh_disc_to_tgt;
			__u8   wwnn[8];
			__u8   wwpn[8];
			__be16 iqid;
			__u8   fc_oui[3];
			__u8   r_id[3];
		} fcoe_rdev;
		struct iscsi_rdev_entry {
			__be32 flowid;
			__u8   protocol;
			__u8   event_cause;
			__u8   flags;
			__u8   r3;
			__be16 iscsi_opts;
			__be16 tcp_opts;
			__be16 ip_opts;
			__be16 max_rcv_len;
			__be16 max_snd_len;
			__be16 first_brst_len;
			__be16 max_brst_len;
			__be16 r4;
			__be16 def_time2wait;
			__be16 def_time2ret;
			__be16 nop_out_intrvl;
			__be16 non_scsi_to;
			__be16 isid;
			__be16 tsid;
			__be16 port;
			__be16 tpgt;
			__u8   r5[6];
			__be16 iqid;
		} iscsi_rdev;
	} u;
};

#define S_FW_RDEV_WR_IMMDLEN	0
#define M_FW_RDEV_WR_IMMDLEN	0xff
#define V_FW_RDEV_WR_IMMDLEN(x)	((x) << S_FW_RDEV_WR_IMMDLEN)
#define G_FW_RDEV_WR_IMMDLEN(x)	\
    (((x) >> S_FW_RDEV_WR_IMMDLEN) & M_FW_RDEV_WR_IMMDLEN)

#define S_FW_RDEV_WR_ALLOC	31
#define M_FW_RDEV_WR_ALLOC	0x1
#define V_FW_RDEV_WR_ALLOC(x)	((x) << S_FW_RDEV_WR_ALLOC)
#define G_FW_RDEV_WR_ALLOC(x)	\
    (((x) >> S_FW_RDEV_WR_ALLOC) & M_FW_RDEV_WR_ALLOC)
#define F_FW_RDEV_WR_ALLOC	V_FW_RDEV_WR_ALLOC(1U)

#define S_FW_RDEV_WR_FREE	30
#define M_FW_RDEV_WR_FREE	0x1
#define V_FW_RDEV_WR_FREE(x)	((x) << S_FW_RDEV_WR_FREE)
#define G_FW_RDEV_WR_FREE(x)	\
    (((x) >> S_FW_RDEV_WR_FREE) & M_FW_RDEV_WR_FREE)
#define F_FW_RDEV_WR_FREE	V_FW_RDEV_WR_FREE(1U)

#define S_FW_RDEV_WR_MODIFY	29
#define M_FW_RDEV_WR_MODIFY	0x1
#define V_FW_RDEV_WR_MODIFY(x)	((x) << S_FW_RDEV_WR_MODIFY)
#define G_FW_RDEV_WR_MODIFY(x)	\
    (((x) >> S_FW_RDEV_WR_MODIFY) & M_FW_RDEV_WR_MODIFY)
#define F_FW_RDEV_WR_MODIFY	V_FW_RDEV_WR_MODIFY(1U)

#define S_FW_RDEV_WR_FLOWID	8
#define M_FW_RDEV_WR_FLOWID	0xfffff
#define V_FW_RDEV_WR_FLOWID(x)	((x) << S_FW_RDEV_WR_FLOWID)
#define G_FW_RDEV_WR_FLOWID(x)	\
    (((x) >> S_FW_RDEV_WR_FLOWID) & M_FW_RDEV_WR_FLOWID)

#define S_FW_RDEV_WR_LEN16	0
#define M_FW_RDEV_WR_LEN16	0xff
#define V_FW_RDEV_WR_LEN16(x)	((x) << S_FW_RDEV_WR_LEN16)
#define G_FW_RDEV_WR_LEN16(x)	\
    (((x) >> S_FW_RDEV_WR_LEN16) & M_FW_RDEV_WR_LEN16)

#define S_FW_RDEV_WR_FLAGS	24
#define M_FW_RDEV_WR_FLAGS	0xff
#define V_FW_RDEV_WR_FLAGS(x)	((x) << S_FW_RDEV_WR_FLAGS)
#define G_FW_RDEV_WR_FLAGS(x)	\
    (((x) >> S_FW_RDEV_WR_FLAGS) & M_FW_RDEV_WR_FLAGS)

#define S_FW_RDEV_WR_GET_NEXT		20
#define M_FW_RDEV_WR_GET_NEXT		0xf
#define V_FW_RDEV_WR_GET_NEXT(x)	((x) << S_FW_RDEV_WR_GET_NEXT)
#define G_FW_RDEV_WR_GET_NEXT(x)	\
    (((x) >> S_FW_RDEV_WR_GET_NEXT) & M_FW_RDEV_WR_GET_NEXT)

#define S_FW_RDEV_WR_ASSOC_FLOWID	0
#define M_FW_RDEV_WR_ASSOC_FLOWID	0xfffff
#define V_FW_RDEV_WR_ASSOC_FLOWID(x)	((x) << S_FW_RDEV_WR_ASSOC_FLOWID)
#define G_FW_RDEV_WR_ASSOC_FLOWID(x)	\
    (((x) >> S_FW_RDEV_WR_ASSOC_FLOWID) & M_FW_RDEV_WR_ASSOC_FLOWID)

#define S_FW_RDEV_WR_RJT	7
#define M_FW_RDEV_WR_RJT	0x1
#define V_FW_RDEV_WR_RJT(x)	((x) << S_FW_RDEV_WR_RJT)
#define G_FW_RDEV_WR_RJT(x)	(((x) >> S_FW_RDEV_WR_RJT) & M_FW_RDEV_WR_RJT)
#define F_FW_RDEV_WR_RJT	V_FW_RDEV_WR_RJT(1U)

#define S_FW_RDEV_WR_REASON	0
#define M_FW_RDEV_WR_REASON	0x7f
#define V_FW_RDEV_WR_REASON(x)	((x) << S_FW_RDEV_WR_REASON)
#define G_FW_RDEV_WR_REASON(x)	\
    (((x) >> S_FW_RDEV_WR_REASON) & M_FW_RDEV_WR_REASON)

#define S_FW_RDEV_WR_RD_XFER_RDY	7
#define M_FW_RDEV_WR_RD_XFER_RDY	0x1
#define V_FW_RDEV_WR_RD_XFER_RDY(x)	((x) << S_FW_RDEV_WR_RD_XFER_RDY)
#define G_FW_RDEV_WR_RD_XFER_RDY(x)	\
    (((x) >> S_FW_RDEV_WR_RD_XFER_RDY) & M_FW_RDEV_WR_RD_XFER_RDY)
#define F_FW_RDEV_WR_RD_XFER_RDY	V_FW_RDEV_WR_RD_XFER_RDY(1U)

#define S_FW_RDEV_WR_WR_XFER_RDY	6
#define M_FW_RDEV_WR_WR_XFER_RDY	0x1
#define V_FW_RDEV_WR_WR_XFER_RDY(x)	((x) << S_FW_RDEV_WR_WR_XFER_RDY)
#define G_FW_RDEV_WR_WR_XFER_RDY(x)	\
    (((x) >> S_FW_RDEV_WR_WR_XFER_RDY) & M_FW_RDEV_WR_WR_XFER_RDY)
#define F_FW_RDEV_WR_WR_XFER_RDY	V_FW_RDEV_WR_WR_XFER_RDY(1U)

#define S_FW_RDEV_WR_FC_SP	5
#define M_FW_RDEV_WR_FC_SP	0x1
#define V_FW_RDEV_WR_FC_SP(x)	((x) << S_FW_RDEV_WR_FC_SP)
#define G_FW_RDEV_WR_FC_SP(x)	\
    (((x) >> S_FW_RDEV_WR_FC_SP) & M_FW_RDEV_WR_FC_SP)
#define F_FW_RDEV_WR_FC_SP	V_FW_RDEV_WR_FC_SP(1U)

#define S_FW_RDEV_WR_RPORT_TYPE		0
#define M_FW_RDEV_WR_RPORT_TYPE		0x1f
#define V_FW_RDEV_WR_RPORT_TYPE(x)	((x) << S_FW_RDEV_WR_RPORT_TYPE)
#define G_FW_RDEV_WR_RPORT_TYPE(x)	\
    (((x) >> S_FW_RDEV_WR_RPORT_TYPE) & M_FW_RDEV_WR_RPORT_TYPE)

#define S_FW_RDEV_WR_VFT	7
#define M_FW_RDEV_WR_VFT	0x1
#define V_FW_RDEV_WR_VFT(x)	((x) << S_FW_RDEV_WR_VFT)
#define G_FW_RDEV_WR_VFT(x)	(((x) >> S_FW_RDEV_WR_VFT) & M_FW_RDEV_WR_VFT)
#define F_FW_RDEV_WR_VFT	V_FW_RDEV_WR_VFT(1U)

#define S_FW_RDEV_WR_NPIV	6
#define M_FW_RDEV_WR_NPIV	0x1
#define V_FW_RDEV_WR_NPIV(x)	((x) << S_FW_RDEV_WR_NPIV)
#define G_FW_RDEV_WR_NPIV(x)	\
    (((x) >> S_FW_RDEV_WR_NPIV) & M_FW_RDEV_WR_NPIV)
#define F_FW_RDEV_WR_NPIV	V_FW_RDEV_WR_NPIV(1U)

#define S_FW_RDEV_WR_CLASS	4
#define M_FW_RDEV_WR_CLASS	0x3
#define V_FW_RDEV_WR_CLASS(x)	((x) << S_FW_RDEV_WR_CLASS)
#define G_FW_RDEV_WR_CLASS(x)	\
    (((x) >> S_FW_RDEV_WR_CLASS) & M_FW_RDEV_WR_CLASS)

#define S_FW_RDEV_WR_SEQ_DEL	3
#define M_FW_RDEV_WR_SEQ_DEL	0x1
#define V_FW_RDEV_WR_SEQ_DEL(x)	((x) << S_FW_RDEV_WR_SEQ_DEL)
#define G_FW_RDEV_WR_SEQ_DEL(x)	\
    (((x) >> S_FW_RDEV_WR_SEQ_DEL) & M_FW_RDEV_WR_SEQ_DEL)
#define F_FW_RDEV_WR_SEQ_DEL	V_FW_RDEV_WR_SEQ_DEL(1U)

#define S_FW_RDEV_WR_PRIO_PREEMP	2
#define M_FW_RDEV_WR_PRIO_PREEMP	0x1
#define V_FW_RDEV_WR_PRIO_PREEMP(x)	((x) << S_FW_RDEV_WR_PRIO_PREEMP)
#define G_FW_RDEV_WR_PRIO_PREEMP(x)	\
    (((x) >> S_FW_RDEV_WR_PRIO_PREEMP) & M_FW_RDEV_WR_PRIO_PREEMP)
#define F_FW_RDEV_WR_PRIO_PREEMP	V_FW_RDEV_WR_PRIO_PREEMP(1U)

#define S_FW_RDEV_WR_PREF	1
#define M_FW_RDEV_WR_PREF	0x1
#define V_FW_RDEV_WR_PREF(x)	((x) << S_FW_RDEV_WR_PREF)
#define G_FW_RDEV_WR_PREF(x)	\
    (((x) >> S_FW_RDEV_WR_PREF) & M_FW_RDEV_WR_PREF)
#define F_FW_RDEV_WR_PREF	V_FW_RDEV_WR_PREF(1U)

#define S_FW_RDEV_WR_QOS	0
#define M_FW_RDEV_WR_QOS	0x1
#define V_FW_RDEV_WR_QOS(x)	((x) << S_FW_RDEV_WR_QOS)
#define G_FW_RDEV_WR_QOS(x)	(((x) >> S_FW_RDEV_WR_QOS) & M_FW_RDEV_WR_QOS)
#define F_FW_RDEV_WR_QOS	V_FW_RDEV_WR_QOS(1U)

#define S_FW_RDEV_WR_ORG_PROC_ASSOC	7
#define M_FW_RDEV_WR_ORG_PROC_ASSOC	0x1
#define V_FW_RDEV_WR_ORG_PROC_ASSOC(x)	((x) << S_FW_RDEV_WR_ORG_PROC_ASSOC)
#define G_FW_RDEV_WR_ORG_PROC_ASSOC(x)	\
    (((x) >> S_FW_RDEV_WR_ORG_PROC_ASSOC) & M_FW_RDEV_WR_ORG_PROC_ASSOC)
#define F_FW_RDEV_WR_ORG_PROC_ASSOC	V_FW_RDEV_WR_ORG_PROC_ASSOC(1U)

#define S_FW_RDEV_WR_RSP_PROC_ASSOC	6
#define M_FW_RDEV_WR_RSP_PROC_ASSOC	0x1
#define V_FW_RDEV_WR_RSP_PROC_ASSOC(x)	((x) << S_FW_RDEV_WR_RSP_PROC_ASSOC)
#define G_FW_RDEV_WR_RSP_PROC_ASSOC(x)	\
    (((x) >> S_FW_RDEV_WR_RSP_PROC_ASSOC) & M_FW_RDEV_WR_RSP_PROC_ASSOC)
#define F_FW_RDEV_WR_RSP_PROC_ASSOC	V_FW_RDEV_WR_RSP_PROC_ASSOC(1U)

#define S_FW_RDEV_WR_IMAGE_PAIR		5
#define M_FW_RDEV_WR_IMAGE_PAIR		0x1
#define V_FW_RDEV_WR_IMAGE_PAIR(x)	((x) << S_FW_RDEV_WR_IMAGE_PAIR)
#define G_FW_RDEV_WR_IMAGE_PAIR(x)	\
    (((x) >> S_FW_RDEV_WR_IMAGE_PAIR) & M_FW_RDEV_WR_IMAGE_PAIR)
#define F_FW_RDEV_WR_IMAGE_PAIR	V_FW_RDEV_WR_IMAGE_PAIR(1U)

#define S_FW_RDEV_WR_ACC_RSP_CODE	0
#define M_FW_RDEV_WR_ACC_RSP_CODE	0x1f
#define V_FW_RDEV_WR_ACC_RSP_CODE(x)	((x) << S_FW_RDEV_WR_ACC_RSP_CODE)
#define G_FW_RDEV_WR_ACC_RSP_CODE(x)	\
    (((x) >> S_FW_RDEV_WR_ACC_RSP_CODE) & M_FW_RDEV_WR_ACC_RSP_CODE)

#define S_FW_RDEV_WR_ENH_DISC		7
#define M_FW_RDEV_WR_ENH_DISC		0x1
#define V_FW_RDEV_WR_ENH_DISC(x)	((x) << S_FW_RDEV_WR_ENH_DISC)
#define G_FW_RDEV_WR_ENH_DISC(x)	\
    (((x) >> S_FW_RDEV_WR_ENH_DISC) & M_FW_RDEV_WR_ENH_DISC)
#define F_FW_RDEV_WR_ENH_DISC	V_FW_RDEV_WR_ENH_DISC(1U)

#define S_FW_RDEV_WR_REC	6
#define M_FW_RDEV_WR_REC	0x1
#define V_FW_RDEV_WR_REC(x)	((x) << S_FW_RDEV_WR_REC)
#define G_FW_RDEV_WR_REC(x)	(((x) >> S_FW_RDEV_WR_REC) & M_FW_RDEV_WR_REC)
#define F_FW_RDEV_WR_REC	V_FW_RDEV_WR_REC(1U)

#define S_FW_RDEV_WR_TASK_RETRY_ID	5
#define M_FW_RDEV_WR_TASK_RETRY_ID	0x1
#define V_FW_RDEV_WR_TASK_RETRY_ID(x)	((x) << S_FW_RDEV_WR_TASK_RETRY_ID)
#define G_FW_RDEV_WR_TASK_RETRY_ID(x)	\
    (((x) >> S_FW_RDEV_WR_TASK_RETRY_ID) & M_FW_RDEV_WR_TASK_RETRY_ID)
#define F_FW_RDEV_WR_TASK_RETRY_ID	V_FW_RDEV_WR_TASK_RETRY_ID(1U)

#define S_FW_RDEV_WR_RETRY	4
#define M_FW_RDEV_WR_RETRY	0x1
#define V_FW_RDEV_WR_RETRY(x)	((x) << S_FW_RDEV_WR_RETRY)
#define G_FW_RDEV_WR_RETRY(x)	\
    (((x) >> S_FW_RDEV_WR_RETRY) & M_FW_RDEV_WR_RETRY)
#define F_FW_RDEV_WR_RETRY	V_FW_RDEV_WR_RETRY(1U)

#define S_FW_RDEV_WR_CONF_CMPL		3
#define M_FW_RDEV_WR_CONF_CMPL		0x1
#define V_FW_RDEV_WR_CONF_CMPL(x)	((x) << S_FW_RDEV_WR_CONF_CMPL)
#define G_FW_RDEV_WR_CONF_CMPL(x)	\
    (((x) >> S_FW_RDEV_WR_CONF_CMPL) & M_FW_RDEV_WR_CONF_CMPL)
#define F_FW_RDEV_WR_CONF_CMPL	V_FW_RDEV_WR_CONF_CMPL(1U)

#define S_FW_RDEV_WR_DATA_OVLY		2
#define M_FW_RDEV_WR_DATA_OVLY		0x1
#define V_FW_RDEV_WR_DATA_OVLY(x)	((x) << S_FW_RDEV_WR_DATA_OVLY)
#define G_FW_RDEV_WR_DATA_OVLY(x)	\
    (((x) >> S_FW_RDEV_WR_DATA_OVLY) & M_FW_RDEV_WR_DATA_OVLY)
#define F_FW_RDEV_WR_DATA_OVLY	V_FW_RDEV_WR_DATA_OVLY(1U)

#define S_FW_RDEV_WR_INI	1
#define M_FW_RDEV_WR_INI	0x1
#define V_FW_RDEV_WR_INI(x)	((x) << S_FW_RDEV_WR_INI)
#define G_FW_RDEV_WR_INI(x)	(((x) >> S_FW_RDEV_WR_INI) & M_FW_RDEV_WR_INI)
#define F_FW_RDEV_WR_INI	V_FW_RDEV_WR_INI(1U)

#define S_FW_RDEV_WR_TGT	0
#define M_FW_RDEV_WR_TGT	0x1
#define V_FW_RDEV_WR_TGT(x)	((x) << S_FW_RDEV_WR_TGT)
#define G_FW_RDEV_WR_TGT(x)	(((x) >> S_FW_RDEV_WR_TGT) & M_FW_RDEV_WR_TGT)
#define F_FW_RDEV_WR_TGT	V_FW_RDEV_WR_TGT(1U)

struct fw_foiscsi_node_wr {
	__be32 op_to_immdlen;
	__be32 no_sess_recv_to_len16;
	__u64  cookie;
	__u8   subop;
	__u8   status;
	__u8   alias_len;
	__u8   iqn_len;
	__be32 node_flowid;
	__be16 nodeid;
	__be16 login_retry;
	__be16 retry_timeout;
	__be16 r3;
	__u8   iqn[224];
	__u8   alias[224];
	__be32 isid_tval_to_isid_cval;
};

#define S_FW_FOISCSI_NODE_WR_IMMDLEN	0
#define M_FW_FOISCSI_NODE_WR_IMMDLEN	0xffff
#define V_FW_FOISCSI_NODE_WR_IMMDLEN(x)	((x) << S_FW_FOISCSI_NODE_WR_IMMDLEN)
#define G_FW_FOISCSI_NODE_WR_IMMDLEN(x)	\
    (((x) >> S_FW_FOISCSI_NODE_WR_IMMDLEN) & M_FW_FOISCSI_NODE_WR_IMMDLEN)

#define S_FW_FOISCSI_NODE_WR_NO_SESS_RECV	28
#define M_FW_FOISCSI_NODE_WR_NO_SESS_RECV	0x1
#define V_FW_FOISCSI_NODE_WR_NO_SESS_RECV(x)	\
    ((x) << S_FW_FOISCSI_NODE_WR_NO_SESS_RECV)
#define G_FW_FOISCSI_NODE_WR_NO_SESS_RECV(x)	\
    (((x) >> S_FW_FOISCSI_NODE_WR_NO_SESS_RECV) & \
     M_FW_FOISCSI_NODE_WR_NO_SESS_RECV)
#define F_FW_FOISCSI_NODE_WR_NO_SESS_RECV	\
    V_FW_FOISCSI_NODE_WR_NO_SESS_RECV(1U)

#define S_FW_FOISCSI_NODE_WR_ISID_TVAL		30
#define M_FW_FOISCSI_NODE_WR_ISID_TVAL		0x3
#define V_FW_FOISCSI_NODE_WR_ISID_TVAL(x)	\
    ((x) << S_FW_FOISCSI_NODE_WR_ISID_TVAL)
#define G_FW_FOISCSI_NODE_WR_ISID_TVAL(x)	\
    (((x) >> S_FW_FOISCSI_NODE_WR_ISID_TVAL) & M_FW_FOISCSI_NODE_WR_ISID_TVAL)

#define S_FW_FOISCSI_NODE_WR_ISID_AVAL		24
#define M_FW_FOISCSI_NODE_WR_ISID_AVAL		0x3f
#define V_FW_FOISCSI_NODE_WR_ISID_AVAL(x)	\
    ((x) << S_FW_FOISCSI_NODE_WR_ISID_AVAL)
#define G_FW_FOISCSI_NODE_WR_ISID_AVAL(x)	\
    (((x) >> S_FW_FOISCSI_NODE_WR_ISID_AVAL) & M_FW_FOISCSI_NODE_WR_ISID_AVAL)

#define S_FW_FOISCSI_NODE_WR_ISID_BVAL		8
#define M_FW_FOISCSI_NODE_WR_ISID_BVAL		0xffff
#define V_FW_FOISCSI_NODE_WR_ISID_BVAL(x)	\
    ((x) << S_FW_FOISCSI_NODE_WR_ISID_BVAL)
#define G_FW_FOISCSI_NODE_WR_ISID_BVAL(x)	\
    (((x) >> S_FW_FOISCSI_NODE_WR_ISID_BVAL) & M_FW_FOISCSI_NODE_WR_ISID_BVAL)

#define S_FW_FOISCSI_NODE_WR_ISID_CVAL		0
#define M_FW_FOISCSI_NODE_WR_ISID_CVAL		0xff
#define V_FW_FOISCSI_NODE_WR_ISID_CVAL(x)	\
    ((x) << S_FW_FOISCSI_NODE_WR_ISID_CVAL)
#define G_FW_FOISCSI_NODE_WR_ISID_CVAL(x)	\
    (((x) >> S_FW_FOISCSI_NODE_WR_ISID_CVAL) & M_FW_FOISCSI_NODE_WR_ISID_CVAL)

struct fw_foiscsi_ctrl_wr {
	__be32 op_to_no_fin;
	__be32 flowid_len16;
	__u64  cookie;
	__u8   subop;
	__u8   status;
	__u8   ctrl_state;
	__u8   io_state;
	__be32 node_id;
	__be32 ctrl_id;
	__be32 io_id;
	struct fw_foiscsi_sess_attr {
		__be32 sess_type_to_erl;
		__be16 max_conn;
		__be16 max_r2t;
		__be16 time2wait;
		__be16 time2retain;
		__be32 max_burst;
		__be32 first_burst;
		__be32 r1;
	} sess_attr;
	struct fw_foiscsi_conn_attr {
		__be32 hdigest_to_tcp_ws_en;
		__be32 max_rcv_dsl;
		__be32 ping_tmo;
		__be16 dst_port;
		__be16 src_port;
		union fw_foiscsi_conn_attr_addr {
			struct fw_foiscsi_conn_attr_ipv6 {
				__be64 dst_addr[2];
				__be64 src_addr[2];
			} ipv6_addr;
			struct fw_foiscsi_conn_attr_ipv4 {
				__be32 dst_addr;
				__be32 src_addr;
			} ipv4_addr;
		} u;
	} conn_attr;
	__u8   tgt_name_len;
	__u8   r3[7];
	__u8   tgt_name[FW_FOISCSI_NAME_MAX_LEN];
};

#define S_FW_FOISCSI_CTRL_WR_PORTID	1
#define M_FW_FOISCSI_CTRL_WR_PORTID	0x7
#define V_FW_FOISCSI_CTRL_WR_PORTID(x)	((x) << S_FW_FOISCSI_CTRL_WR_PORTID)
#define G_FW_FOISCSI_CTRL_WR_PORTID(x)	\
    (((x) >> S_FW_FOISCSI_CTRL_WR_PORTID) & M_FW_FOISCSI_CTRL_WR_PORTID)

#define S_FW_FOISCSI_CTRL_WR_NO_FIN	0
#define M_FW_FOISCSI_CTRL_WR_NO_FIN	0x1
#define V_FW_FOISCSI_CTRL_WR_NO_FIN(x)	((x) << S_FW_FOISCSI_CTRL_WR_NO_FIN)
#define G_FW_FOISCSI_CTRL_WR_NO_FIN(x)	\
    (((x) >> S_FW_FOISCSI_CTRL_WR_NO_FIN) & M_FW_FOISCSI_CTRL_WR_NO_FIN)
#define F_FW_FOISCSI_CTRL_WR_NO_FIN	V_FW_FOISCSI_CTRL_WR_NO_FIN(1U)

#define S_FW_FOISCSI_CTRL_WR_SESS_TYPE		30
#define M_FW_FOISCSI_CTRL_WR_SESS_TYPE		0x3
#define V_FW_FOISCSI_CTRL_WR_SESS_TYPE(x)	\
    ((x) << S_FW_FOISCSI_CTRL_WR_SESS_TYPE)
#define G_FW_FOISCSI_CTRL_WR_SESS_TYPE(x)	\
    (((x) >> S_FW_FOISCSI_CTRL_WR_SESS_TYPE) & M_FW_FOISCSI_CTRL_WR_SESS_TYPE)

#define S_FW_FOISCSI_CTRL_WR_SEQ_INORDER	29
#define M_FW_FOISCSI_CTRL_WR_SEQ_INORDER	0x1
#define V_FW_FOISCSI_CTRL_WR_SEQ_INORDER(x)	\
    ((x) << S_FW_FOISCSI_CTRL_WR_SEQ_INORDER)
#define G_FW_FOISCSI_CTRL_WR_SEQ_INORDER(x)	\
    (((x) >> S_FW_FOISCSI_CTRL_WR_SEQ_INORDER) & \
     M_FW_FOISCSI_CTRL_WR_SEQ_INORDER)
#define F_FW_FOISCSI_CTRL_WR_SEQ_INORDER	\
    V_FW_FOISCSI_CTRL_WR_SEQ_INORDER(1U)

#define S_FW_FOISCSI_CTRL_WR_PDU_INORDER	28
#define M_FW_FOISCSI_CTRL_WR_PDU_INORDER	0x1
#define V_FW_FOISCSI_CTRL_WR_PDU_INORDER(x)	\
    ((x) << S_FW_FOISCSI_CTRL_WR_PDU_INORDER)
#define G_FW_FOISCSI_CTRL_WR_PDU_INORDER(x)	\
    (((x) >> S_FW_FOISCSI_CTRL_WR_PDU_INORDER) & \
     M_FW_FOISCSI_CTRL_WR_PDU_INORDER)
#define F_FW_FOISCSI_CTRL_WR_PDU_INORDER	\
    V_FW_FOISCSI_CTRL_WR_PDU_INORDER(1U)

#define S_FW_FOISCSI_CTRL_WR_IMMD_DATA_EN	27
#define M_FW_FOISCSI_CTRL_WR_IMMD_DATA_EN	0x1
#define V_FW_FOISCSI_CTRL_WR_IMMD_DATA_EN(x)	\
    ((x) << S_FW_FOISCSI_CTRL_WR_IMMD_DATA_EN)
#define G_FW_FOISCSI_CTRL_WR_IMMD_DATA_EN(x)	\
    (((x) >> S_FW_FOISCSI_CTRL_WR_IMMD_DATA_EN) & \
     M_FW_FOISCSI_CTRL_WR_IMMD_DATA_EN)
#define F_FW_FOISCSI_CTRL_WR_IMMD_DATA_EN	\
    V_FW_FOISCSI_CTRL_WR_IMMD_DATA_EN(1U)

#define S_FW_FOISCSI_CTRL_WR_INIT_R2T_EN	26
#define M_FW_FOISCSI_CTRL_WR_INIT_R2T_EN	0x1
#define V_FW_FOISCSI_CTRL_WR_INIT_R2T_EN(x)	\
    ((x) << S_FW_FOISCSI_CTRL_WR_INIT_R2T_EN)
#define G_FW_FOISCSI_CTRL_WR_INIT_R2T_EN(x)	\
    (((x) >> S_FW_FOISCSI_CTRL_WR_INIT_R2T_EN) & \
     M_FW_FOISCSI_CTRL_WR_INIT_R2T_EN)
#define F_FW_FOISCSI_CTRL_WR_INIT_R2T_EN	\
    V_FW_FOISCSI_CTRL_WR_INIT_R2T_EN(1U)

#define S_FW_FOISCSI_CTRL_WR_ERL	24
#define M_FW_FOISCSI_CTRL_WR_ERL	0x3
#define V_FW_FOISCSI_CTRL_WR_ERL(x)	((x) << S_FW_FOISCSI_CTRL_WR_ERL)
#define G_FW_FOISCSI_CTRL_WR_ERL(x)	\
    (((x) >> S_FW_FOISCSI_CTRL_WR_ERL) & M_FW_FOISCSI_CTRL_WR_ERL)

#define S_FW_FOISCSI_CTRL_WR_HDIGEST	30
#define M_FW_FOISCSI_CTRL_WR_HDIGEST	0x3
#define V_FW_FOISCSI_CTRL_WR_HDIGEST(x)	((x) << S_FW_FOISCSI_CTRL_WR_HDIGEST)
#define G_FW_FOISCSI_CTRL_WR_HDIGEST(x)	\
    (((x) >> S_FW_FOISCSI_CTRL_WR_HDIGEST) & M_FW_FOISCSI_CTRL_WR_HDIGEST)

#define S_FW_FOISCSI_CTRL_WR_DDIGEST	28
#define M_FW_FOISCSI_CTRL_WR_DDIGEST	0x3
#define V_FW_FOISCSI_CTRL_WR_DDIGEST(x)	((x) << S_FW_FOISCSI_CTRL_WR_DDIGEST)
#define G_FW_FOISCSI_CTRL_WR_DDIGEST(x)	\
    (((x) >> S_FW_FOISCSI_CTRL_WR_DDIGEST) & M_FW_FOISCSI_CTRL_WR_DDIGEST)

#define S_FW_FOISCSI_CTRL_WR_AUTH_METHOD	25
#define M_FW_FOISCSI_CTRL_WR_AUTH_METHOD	0x7
#define V_FW_FOISCSI_CTRL_WR_AUTH_METHOD(x)	\
    ((x) << S_FW_FOISCSI_CTRL_WR_AUTH_METHOD)
#define G_FW_FOISCSI_CTRL_WR_AUTH_METHOD(x)	\
    (((x) >> S_FW_FOISCSI_CTRL_WR_AUTH_METHOD) & \
     M_FW_FOISCSI_CTRL_WR_AUTH_METHOD)

#define S_FW_FOISCSI_CTRL_WR_AUTH_POLICY	23
#define M_FW_FOISCSI_CTRL_WR_AUTH_POLICY	0x3
#define V_FW_FOISCSI_CTRL_WR_AUTH_POLICY(x)	\
    ((x) << S_FW_FOISCSI_CTRL_WR_AUTH_POLICY)
#define G_FW_FOISCSI_CTRL_WR_AUTH_POLICY(x)	\
    (((x) >> S_FW_FOISCSI_CTRL_WR_AUTH_POLICY) & \
     M_FW_FOISCSI_CTRL_WR_AUTH_POLICY)

#define S_FW_FOISCSI_CTRL_WR_DDP_PGSZ		21
#define M_FW_FOISCSI_CTRL_WR_DDP_PGSZ		0x3
#define V_FW_FOISCSI_CTRL_WR_DDP_PGSZ(x)	\
    ((x) << S_FW_FOISCSI_CTRL_WR_DDP_PGSZ)
#define G_FW_FOISCSI_CTRL_WR_DDP_PGSZ(x)	\
    (((x) >> S_FW_FOISCSI_CTRL_WR_DDP_PGSZ) & M_FW_FOISCSI_CTRL_WR_DDP_PGSZ)

#define S_FW_FOISCSI_CTRL_WR_IPV6	20
#define M_FW_FOISCSI_CTRL_WR_IPV6	0x1
#define V_FW_FOISCSI_CTRL_WR_IPV6(x)	((x) << S_FW_FOISCSI_CTRL_WR_IPV6)
#define G_FW_FOISCSI_CTRL_WR_IPV6(x)	\
    (((x) >> S_FW_FOISCSI_CTRL_WR_IPV6) & M_FW_FOISCSI_CTRL_WR_IPV6)
#define F_FW_FOISCSI_CTRL_WR_IPV6	V_FW_FOISCSI_CTRL_WR_IPV6(1U)

#define S_FW_FOISCSI_CTRL_WR_DDP_PGIDX		16
#define M_FW_FOISCSI_CTRL_WR_DDP_PGIDX		0xf
#define V_FW_FOISCSI_CTRL_WR_DDP_PGIDX(x)	\
    ((x) << S_FW_FOISCSI_CTRL_WR_DDP_PGIDX)
#define G_FW_FOISCSI_CTRL_WR_DDP_PGIDX(x)	\
    (((x) >> S_FW_FOISCSI_CTRL_WR_DDP_PGIDX) & M_FW_FOISCSI_CTRL_WR_DDP_PGIDX)

#define S_FW_FOISCSI_CTRL_WR_TCP_WS	12
#define M_FW_FOISCSI_CTRL_WR_TCP_WS	0xf
#define V_FW_FOISCSI_CTRL_WR_TCP_WS(x)	((x) << S_FW_FOISCSI_CTRL_WR_TCP_WS)
#define G_FW_FOISCSI_CTRL_WR_TCP_WS(x)	\
    (((x) >> S_FW_FOISCSI_CTRL_WR_TCP_WS) & M_FW_FOISCSI_CTRL_WR_TCP_WS)

#define S_FW_FOISCSI_CTRL_WR_TCP_WS_EN		11
#define M_FW_FOISCSI_CTRL_WR_TCP_WS_EN		0x1
#define V_FW_FOISCSI_CTRL_WR_TCP_WS_EN(x)	\
    ((x) << S_FW_FOISCSI_CTRL_WR_TCP_WS_EN)
#define G_FW_FOISCSI_CTRL_WR_TCP_WS_EN(x)	\
    (((x) >> S_FW_FOISCSI_CTRL_WR_TCP_WS_EN) & M_FW_FOISCSI_CTRL_WR_TCP_WS_EN)
#define F_FW_FOISCSI_CTRL_WR_TCP_WS_EN	V_FW_FOISCSI_CTRL_WR_TCP_WS_EN(1U)

struct fw_foiscsi_chap_wr {
	__be32 op_to_kv_flag;
	__be32 flowid_len16;
	__u64  cookie;
	__u8   status;
	union fw_foiscsi_len {
		struct fw_foiscsi_chap_lens {
			__u8   id_len;
			__u8   sec_len;
		} chapl;
		struct fw_foiscsi_vend_kv_lens {
			__u8   key_len;
			__u8   val_len;
		} vend_kvl;
	} lenu;
	__u8   node_type;
	__be16 node_id;
	__u8   r3[2];
	union fw_foiscsi_chap_vend {
		struct fw_foiscsi_chap {
			__u8   chap_id[224];
			__u8   chap_sec[128];
		} chap;
		struct fw_foiscsi_vend_kv {
			__u8   vend_key[64];
			__u8   vend_val[256];
		} vend_kv;
	} u;
};

#define S_FW_FOISCSI_CHAP_WR_KV_FLAG	20
#define M_FW_FOISCSI_CHAP_WR_KV_FLAG	0x1
#define V_FW_FOISCSI_CHAP_WR_KV_FLAG(x)	((x) << S_FW_FOISCSI_CHAP_WR_KV_FLAG)
#define G_FW_FOISCSI_CHAP_WR_KV_FLAG(x)	\
    (((x) >> S_FW_FOISCSI_CHAP_WR_KV_FLAG) & M_FW_FOISCSI_CHAP_WR_KV_FLAG)
#define F_FW_FOISCSI_CHAP_WR_KV_FLAG	V_FW_FOISCSI_CHAP_WR_KV_FLAG(1U)

/******************************************************************************
 *  C O i S C S I  W O R K R E Q U E S T S
 ********************************************/

enum fw_chnet_addr_type {
	FW_CHNET_ADDD_TYPE_NONE = 0,
	FW_CHNET_ADDR_TYPE_IPV4,
	FW_CHNET_ADDR_TYPE_IPV6,
};

enum fw_msg_wr_type {
	FW_MSG_WR_TYPE_RPL = 0,
	FW_MSG_WR_TYPE_ERR,
	FW_MSG_WR_TYPE_PLD,
};

struct fw_coiscsi_tgt_wr {
	__be32 op_compl;
	__be32 flowid_len16;
	__u64  cookie;
	__u8   subop;
	__u8   status;
	__be16 r4;
	__be32 flags;
	struct fw_coiscsi_tgt_conn_attr {
		__be32 in_tid;
		__be16 in_port;
		__u8   in_type;
		__u8   r6;
		union fw_coiscsi_tgt_conn_attr_addr {
			struct fw_coiscsi_tgt_conn_attr_in_addr {
				__be32 addr;
				__be32 r7;
				__be32 r8[2];
			} in_addr;
			struct fw_coiscsi_tgt_conn_attr_in_addr6 {
				__be64 addr[2];
			} in_addr6;
		} u;
	} conn_attr;
};

#define S_FW_COISCSI_TGT_WR_PORTID	0
#define M_FW_COISCSI_TGT_WR_PORTID	0x7
#define V_FW_COISCSI_TGT_WR_PORTID(x)	((x) << S_FW_COISCSI_TGT_WR_PORTID)
#define G_FW_COISCSI_TGT_WR_PORTID(x)	\
    (((x) >> S_FW_COISCSI_TGT_WR_PORTID) & M_FW_COISCSI_TGT_WR_PORTID)

struct fw_coiscsi_tgt_conn_wr {
	__be32 op_compl;
	__be32 flowid_len16;
	__u64  cookie;
	__u8   subop;
	__u8   status;
	__be16 iq_id;
	__be32 in_stid;
	__be32 io_id;
	__be32 flags_fin;
	union {
		struct fw_coiscsi_tgt_conn_tcp {
			__be16 in_sport;
			__be16 in_dport;
			__u8   wscale_wsen;
			__u8   r4[3];
			union fw_coiscsi_tgt_conn_tcp_addr {
				struct fw_coiscsi_tgt_conn_tcp_in_addr {
					__be32 saddr;
					__be32 daddr;
				} in_addr;
				struct fw_coiscsi_tgt_conn_tcp_in_addr6 {
					__be64 saddr[2];
					__be64 daddr[2];
				} in_addr6;
			} u;
		} conn_tcp;
		struct fw_coiscsi_tgt_conn_stats {
			__be32 ddp_reqs;
			__be32 ddp_cmpls;
			__be16 ddp_aborts;
			__be16 ddp_bps;
		} stats;
	} u;
	struct fw_coiscsi_tgt_conn_iscsi {
		__be32 hdigest_to_ddp_pgsz;
		__be32 tgt_id;
		__be16 max_r2t;
		__be16 r5;
		__be32 max_burst;
		__be32 max_rdsl;
		__be32 max_tdsl;
		__be32 cur_sn;
		__be32 r6;
	} conn_iscsi;
};

#define S_FW_COISCSI_TGT_CONN_WR_PORTID		0
#define M_FW_COISCSI_TGT_CONN_WR_PORTID		0x7
#define V_FW_COISCSI_TGT_CONN_WR_PORTID(x)	\
    ((x) << S_FW_COISCSI_TGT_CONN_WR_PORTID)
#define G_FW_COISCSI_TGT_CONN_WR_PORTID(x)	\
    (((x) >> S_FW_COISCSI_TGT_CONN_WR_PORTID) & \
     M_FW_COISCSI_TGT_CONN_WR_PORTID)

#define S_FW_COISCSI_TGT_CONN_WR_FIN	0
#define M_FW_COISCSI_TGT_CONN_WR_FIN	0x1
#define V_FW_COISCSI_TGT_CONN_WR_FIN(x)	((x) << S_FW_COISCSI_TGT_CONN_WR_FIN)
#define G_FW_COISCSI_TGT_CONN_WR_FIN(x)	\
    (((x) >> S_FW_COISCSI_TGT_CONN_WR_FIN) & M_FW_COISCSI_TGT_CONN_WR_FIN)
#define F_FW_COISCSI_TGT_CONN_WR_FIN	V_FW_COISCSI_TGT_CONN_WR_FIN(1U)

#define S_FW_COISCSI_TGT_CONN_WR_WSCALE		1
#define M_FW_COISCSI_TGT_CONN_WR_WSCALE		0xf
#define V_FW_COISCSI_TGT_CONN_WR_WSCALE(x)	\
    ((x) << S_FW_COISCSI_TGT_CONN_WR_WSCALE)
#define G_FW_COISCSI_TGT_CONN_WR_WSCALE(x)	\
    (((x) >> S_FW_COISCSI_TGT_CONN_WR_WSCALE) & \
     M_FW_COISCSI_TGT_CONN_WR_WSCALE)

#define S_FW_COISCSI_TGT_CONN_WR_WSEN		0
#define M_FW_COISCSI_TGT_CONN_WR_WSEN		0x1
#define V_FW_COISCSI_TGT_CONN_WR_WSEN(x)	\
    ((x) << S_FW_COISCSI_TGT_CONN_WR_WSEN)
#define G_FW_COISCSI_TGT_CONN_WR_WSEN(x)	\
    (((x) >> S_FW_COISCSI_TGT_CONN_WR_WSEN) & M_FW_COISCSI_TGT_CONN_WR_WSEN)
#define F_FW_COISCSI_TGT_CONN_WR_WSEN	V_FW_COISCSI_TGT_CONN_WR_WSEN(1U)

struct fw_coiscsi_tgt_xmit_wr {
	__be32 op_to_immdlen;
	union {
		struct cmpl_stat {
			__be32 cmpl_status_pkd;
		} cs;
		struct flowid_len {
			__be32 flowid_len16;
		} fllen;
	} u;
	__u64  cookie;
	__be16 iq_id;
	__be16 r3;
	__be32 pz_off;
	__be32 t_xfer_len;
	union {
		__be32 tag;
		__be32 datasn;
		__be32 ddp_status;
	} cu;
};

#define S_FW_COISCSI_TGT_XMIT_WR_DDGST		23
#define M_FW_COISCSI_TGT_XMIT_WR_DDGST		0x1
#define V_FW_COISCSI_TGT_XMIT_WR_DDGST(x)	\
    ((x) << S_FW_COISCSI_TGT_XMIT_WR_DDGST)
#define G_FW_COISCSI_TGT_XMIT_WR_DDGST(x)	\
    (((x) >> S_FW_COISCSI_TGT_XMIT_WR_DDGST) & M_FW_COISCSI_TGT_XMIT_WR_DDGST)
#define F_FW_COISCSI_TGT_XMIT_WR_DDGST	V_FW_COISCSI_TGT_XMIT_WR_DDGST(1U)

#define S_FW_COISCSI_TGT_XMIT_WR_HDGST		22
#define M_FW_COISCSI_TGT_XMIT_WR_HDGST		0x1
#define V_FW_COISCSI_TGT_XMIT_WR_HDGST(x)	\
    ((x) << S_FW_COISCSI_TGT_XMIT_WR_HDGST)
#define G_FW_COISCSI_TGT_XMIT_WR_HDGST(x)	\
    (((x) >> S_FW_COISCSI_TGT_XMIT_WR_HDGST) & M_FW_COISCSI_TGT_XMIT_WR_HDGST)
#define F_FW_COISCSI_TGT_XMIT_WR_HDGST	V_FW_COISCSI_TGT_XMIT_WR_HDGST(1U)

#define S_FW_COISCSI_TGT_XMIT_WR_DDP	20
#define M_FW_COISCSI_TGT_XMIT_WR_DDP	0x1
#define V_FW_COISCSI_TGT_XMIT_WR_DDP(x)	((x) << S_FW_COISCSI_TGT_XMIT_WR_DDP)
#define G_FW_COISCSI_TGT_XMIT_WR_DDP(x)	\
    (((x) >> S_FW_COISCSI_TGT_XMIT_WR_DDP) & M_FW_COISCSI_TGT_XMIT_WR_DDP)
#define F_FW_COISCSI_TGT_XMIT_WR_DDP	V_FW_COISCSI_TGT_XMIT_WR_DDP(1U)

#define S_FW_COISCSI_TGT_XMIT_WR_ABORT		19
#define M_FW_COISCSI_TGT_XMIT_WR_ABORT		0x1
#define V_FW_COISCSI_TGT_XMIT_WR_ABORT(x)	\
    ((x) << S_FW_COISCSI_TGT_XMIT_WR_ABORT)
#define G_FW_COISCSI_TGT_XMIT_WR_ABORT(x)	\
    (((x) >> S_FW_COISCSI_TGT_XMIT_WR_ABORT) & M_FW_COISCSI_TGT_XMIT_WR_ABORT)
#define F_FW_COISCSI_TGT_XMIT_WR_ABORT	V_FW_COISCSI_TGT_XMIT_WR_ABORT(1U)

#define S_FW_COISCSI_TGT_XMIT_WR_FINAL		18
#define M_FW_COISCSI_TGT_XMIT_WR_FINAL		0x1
#define V_FW_COISCSI_TGT_XMIT_WR_FINAL(x)	\
    ((x) << S_FW_COISCSI_TGT_XMIT_WR_FINAL)
#define G_FW_COISCSI_TGT_XMIT_WR_FINAL(x)	\
    (((x) >> S_FW_COISCSI_TGT_XMIT_WR_FINAL) & M_FW_COISCSI_TGT_XMIT_WR_FINAL)
#define F_FW_COISCSI_TGT_XMIT_WR_FINAL	V_FW_COISCSI_TGT_XMIT_WR_FINAL(1U)

#define S_FW_COISCSI_TGT_XMIT_WR_PADLEN		16
#define M_FW_COISCSI_TGT_XMIT_WR_PADLEN		0x3
#define V_FW_COISCSI_TGT_XMIT_WR_PADLEN(x)	\
    ((x) << S_FW_COISCSI_TGT_XMIT_WR_PADLEN)
#define G_FW_COISCSI_TGT_XMIT_WR_PADLEN(x)	\
    (((x) >> S_FW_COISCSI_TGT_XMIT_WR_PADLEN) & \
     M_FW_COISCSI_TGT_XMIT_WR_PADLEN)

#define S_FW_COISCSI_TGT_XMIT_WR_INCSTATSN	15
#define M_FW_COISCSI_TGT_XMIT_WR_INCSTATSN	0x1
#define V_FW_COISCSI_TGT_XMIT_WR_INCSTATSN(x)	\
    ((x) << S_FW_COISCSI_TGT_XMIT_WR_INCSTATSN)
#define G_FW_COISCSI_TGT_XMIT_WR_INCSTATSN(x)	\
    (((x) >> S_FW_COISCSI_TGT_XMIT_WR_INCSTATSN) & \
     M_FW_COISCSI_TGT_XMIT_WR_INCSTATSN)
#define F_FW_COISCSI_TGT_XMIT_WR_INCSTATSN	\
    V_FW_COISCSI_TGT_XMIT_WR_INCSTATSN(1U)

#define S_FW_COISCSI_TGT_XMIT_WR_IMMDLEN	0
#define M_FW_COISCSI_TGT_XMIT_WR_IMMDLEN	0xff
#define V_FW_COISCSI_TGT_XMIT_WR_IMMDLEN(x)	\
    ((x) << S_FW_COISCSI_TGT_XMIT_WR_IMMDLEN)
#define G_FW_COISCSI_TGT_XMIT_WR_IMMDLEN(x)	\
    (((x) >> S_FW_COISCSI_TGT_XMIT_WR_IMMDLEN) & \
     M_FW_COISCSI_TGT_XMIT_WR_IMMDLEN)

#define S_FW_COISCSI_TGT_XMIT_WR_CMPL_STATUS	8
#define M_FW_COISCSI_TGT_XMIT_WR_CMPL_STATUS	0xff
#define V_FW_COISCSI_TGT_XMIT_WR_CMPL_STATUS(x)	\
    ((x) << S_FW_COISCSI_TGT_XMIT_WR_CMPL_STATUS)
#define G_FW_COISCSI_TGT_XMIT_WR_CMPL_STATUS(x)	\
    (((x) >> S_FW_COISCSI_TGT_XMIT_WR_CMPL_STATUS) & \
     M_FW_COISCSI_TGT_XMIT_WR_CMPL_STATUS)

struct fw_coiscsi_stats_wr {
	__be32 op_compl;
	__be32 flowid_len16;
	__u64  cookie;
	__u8   subop;
	__u8   status;
	union fw_coiscsi_stats {
		struct fw_coiscsi_resource {
			__u8   num_ipv4_tgt;
			__u8   num_ipv6_tgt;
			__be16 num_l2t_entries;
			__be16 num_csocks;
			__be16 num_tasks;
			__be16 num_ppods_zone[11];
			__be32 num_bufll64;
			__u8   r2[12];
		} rsrc;
	} u;
};

#define S_FW_COISCSI_STATS_WR_PORTID	0
#define M_FW_COISCSI_STATS_WR_PORTID	0x7
#define V_FW_COISCSI_STATS_WR_PORTID(x)	((x) << S_FW_COISCSI_STATS_WR_PORTID)
#define G_FW_COISCSI_STATS_WR_PORTID(x)	\
    (((x) >> S_FW_COISCSI_STATS_WR_PORTID) & M_FW_COISCSI_STATS_WR_PORTID)

struct fw_isns_wr {
	__be32 op_compl;
	__be32 flowid_len16;
	__u64  cookie;
	__u8   subop;
	__u8   status;
	__be16 iq_id;
	__be16 vlanid;
	__be16 r4;
	struct fw_tcp_conn_attr {
		__be32 in_tid;
		__be16 in_port;
		__u8   in_type;
		__u8   r6;
		union fw_tcp_conn_attr_addr {
			struct fw_tcp_conn_attr_in_addr {
				__be32 addr;
				__be32 r7;
				__be32 r8[2];
			} in_addr;
			struct fw_tcp_conn_attr_in_addr6 {
				__be64 addr[2];
			} in_addr6;
		} u;
	} conn_attr;
};

#define S_FW_ISNS_WR_PORTID	0
#define M_FW_ISNS_WR_PORTID	0x7
#define V_FW_ISNS_WR_PORTID(x)	((x) << S_FW_ISNS_WR_PORTID)
#define G_FW_ISNS_WR_PORTID(x)	\
    (((x) >> S_FW_ISNS_WR_PORTID) & M_FW_ISNS_WR_PORTID)

struct fw_isns_xmit_wr {
	__be32 op_to_immdlen;
	__be32 flowid_len16;
	__u64  cookie;
	__be16 iq_id;
	__be16 r4;
	__be32 xfer_len;
	__be64 r5;
};

#define S_FW_ISNS_XMIT_WR_IMMDLEN	0
#define M_FW_ISNS_XMIT_WR_IMMDLEN	0xff
#define V_FW_ISNS_XMIT_WR_IMMDLEN(x)	((x) << S_FW_ISNS_XMIT_WR_IMMDLEN)
#define G_FW_ISNS_XMIT_WR_IMMDLEN(x)	\
    (((x) >> S_FW_ISNS_XMIT_WR_IMMDLEN) & M_FW_ISNS_XMIT_WR_IMMDLEN)

/******************************************************************************
 *  F O F C O E   W O R K R E Q U E S T s
 *******************************************/

struct fw_fcoe_els_ct_wr {
	__be32 op_immdlen;
	__be32 flowid_len16;
	__be64 cookie;
	__be16 iqid;
	__u8   tmo_val;
	__u8   els_ct_type;
	__u8   ctl_pri;
	__u8   cp_en_class;
	__be16 xfer_cnt;
	__u8   fl_to_sp;
	__u8   l_id[3];
	__u8   r5;
	__u8   r_id[3];
	__be64 rsp_dmaaddr;
	__be32 rsp_dmalen;
	__be32 r6;
};

#define S_FW_FCOE_ELS_CT_WR_OPCODE	24
#define M_FW_FCOE_ELS_CT_WR_OPCODE	0xff
#define V_FW_FCOE_ELS_CT_WR_OPCODE(x)	((x) << S_FW_FCOE_ELS_CT_WR_OPCODE)
#define G_FW_FCOE_ELS_CT_WR_OPCODE(x)	\
    (((x) >> S_FW_FCOE_ELS_CT_WR_OPCODE) & M_FW_FCOE_ELS_CT_WR_OPCODE)

#define S_FW_FCOE_ELS_CT_WR_IMMDLEN	0
#define M_FW_FCOE_ELS_CT_WR_IMMDLEN	0xff
#define V_FW_FCOE_ELS_CT_WR_IMMDLEN(x)	((x) << S_FW_FCOE_ELS_CT_WR_IMMDLEN)
#define G_FW_FCOE_ELS_CT_WR_IMMDLEN(x)	\
    (((x) >> S_FW_FCOE_ELS_CT_WR_IMMDLEN) & M_FW_FCOE_ELS_CT_WR_IMMDLEN)

#define S_FW_FCOE_ELS_CT_WR_FLOWID	8
#define M_FW_FCOE_ELS_CT_WR_FLOWID	0xfffff
#define V_FW_FCOE_ELS_CT_WR_FLOWID(x)	((x) << S_FW_FCOE_ELS_CT_WR_FLOWID)
#define G_FW_FCOE_ELS_CT_WR_FLOWID(x)	\
    (((x) >> S_FW_FCOE_ELS_CT_WR_FLOWID) & M_FW_FCOE_ELS_CT_WR_FLOWID)

#define S_FW_FCOE_ELS_CT_WR_LEN16	0
#define M_FW_FCOE_ELS_CT_WR_LEN16	0xff
#define V_FW_FCOE_ELS_CT_WR_LEN16(x)	((x) << S_FW_FCOE_ELS_CT_WR_LEN16)
#define G_FW_FCOE_ELS_CT_WR_LEN16(x)	\
    (((x) >> S_FW_FCOE_ELS_CT_WR_LEN16) & M_FW_FCOE_ELS_CT_WR_LEN16)

#define S_FW_FCOE_ELS_CT_WR_CP_EN	6
#define M_FW_FCOE_ELS_CT_WR_CP_EN	0x3
#define V_FW_FCOE_ELS_CT_WR_CP_EN(x)	((x) << S_FW_FCOE_ELS_CT_WR_CP_EN)
#define G_FW_FCOE_ELS_CT_WR_CP_EN(x)	\
    (((x) >> S_FW_FCOE_ELS_CT_WR_CP_EN) & M_FW_FCOE_ELS_CT_WR_CP_EN)

#define S_FW_FCOE_ELS_CT_WR_CLASS	4
#define M_FW_FCOE_ELS_CT_WR_CLASS	0x3
#define V_FW_FCOE_ELS_CT_WR_CLASS(x)	((x) << S_FW_FCOE_ELS_CT_WR_CLASS)
#define G_FW_FCOE_ELS_CT_WR_CLASS(x)	\
    (((x) >> S_FW_FCOE_ELS_CT_WR_CLASS) & M_FW_FCOE_ELS_CT_WR_CLASS)

#define S_FW_FCOE_ELS_CT_WR_FL		2
#define M_FW_FCOE_ELS_CT_WR_FL		0x1
#define V_FW_FCOE_ELS_CT_WR_FL(x)	((x) << S_FW_FCOE_ELS_CT_WR_FL)
#define G_FW_FCOE_ELS_CT_WR_FL(x)	\
    (((x) >> S_FW_FCOE_ELS_CT_WR_FL) & M_FW_FCOE_ELS_CT_WR_FL)
#define F_FW_FCOE_ELS_CT_WR_FL	V_FW_FCOE_ELS_CT_WR_FL(1U)

#define S_FW_FCOE_ELS_CT_WR_NPIV	1
#define M_FW_FCOE_ELS_CT_WR_NPIV	0x1
#define V_FW_FCOE_ELS_CT_WR_NPIV(x)	((x) << S_FW_FCOE_ELS_CT_WR_NPIV)
#define G_FW_FCOE_ELS_CT_WR_NPIV(x)	\
    (((x) >> S_FW_FCOE_ELS_CT_WR_NPIV) & M_FW_FCOE_ELS_CT_WR_NPIV)
#define F_FW_FCOE_ELS_CT_WR_NPIV	V_FW_FCOE_ELS_CT_WR_NPIV(1U)

#define S_FW_FCOE_ELS_CT_WR_SP		0
#define M_FW_FCOE_ELS_CT_WR_SP		0x1
#define V_FW_FCOE_ELS_CT_WR_SP(x)	((x) << S_FW_FCOE_ELS_CT_WR_SP)
#define G_FW_FCOE_ELS_CT_WR_SP(x)	\
    (((x) >> S_FW_FCOE_ELS_CT_WR_SP) & M_FW_FCOE_ELS_CT_WR_SP)
#define F_FW_FCOE_ELS_CT_WR_SP	V_FW_FCOE_ELS_CT_WR_SP(1U)

/******************************************************************************
 *  S C S I   W O R K R E Q U E S T s   (FOiSCSI and FCOE unified data path)
 *****************************************************************************/

struct fw_scsi_write_wr {
	__be32 op_immdlen;
	__be32 flowid_len16;
	__be64 cookie;
	__be16 iqid;
	__u8   tmo_val;
	__u8   use_xfer_cnt;
	union fw_scsi_write_priv {
		struct fcoe_write_priv {
			__u8   ctl_pri;
			__u8   cp_en_class;
			__u8   r3_lo[2];
		} fcoe;
		struct iscsi_write_priv {
			__u8   r3[4];
		} iscsi;
	} u;
	__be32 xfer_cnt;
	__be32 ini_xfer_cnt;
	__be64 rsp_dmaaddr;
	__be32 rsp_dmalen;
	__be32 r4;
};

#define S_FW_SCSI_WRITE_WR_OPCODE	24
#define M_FW_SCSI_WRITE_WR_OPCODE	0xff
#define V_FW_SCSI_WRITE_WR_OPCODE(x)	((x) << S_FW_SCSI_WRITE_WR_OPCODE)
#define G_FW_SCSI_WRITE_WR_OPCODE(x)	\
    (((x) >> S_FW_SCSI_WRITE_WR_OPCODE) & M_FW_SCSI_WRITE_WR_OPCODE)

#define S_FW_SCSI_WRITE_WR_IMMDLEN	0
#define M_FW_SCSI_WRITE_WR_IMMDLEN	0xff
#define V_FW_SCSI_WRITE_WR_IMMDLEN(x)	((x) << S_FW_SCSI_WRITE_WR_IMMDLEN)
#define G_FW_SCSI_WRITE_WR_IMMDLEN(x)	\
    (((x) >> S_FW_SCSI_WRITE_WR_IMMDLEN) & M_FW_SCSI_WRITE_WR_IMMDLEN)

#define S_FW_SCSI_WRITE_WR_FLOWID	8
#define M_FW_SCSI_WRITE_WR_FLOWID	0xfffff
#define V_FW_SCSI_WRITE_WR_FLOWID(x)	((x) << S_FW_SCSI_WRITE_WR_FLOWID)
#define G_FW_SCSI_WRITE_WR_FLOWID(x)	\
    (((x) >> S_FW_SCSI_WRITE_WR_FLOWID) & M_FW_SCSI_WRITE_WR_FLOWID)

#define S_FW_SCSI_WRITE_WR_LEN16	0
#define M_FW_SCSI_WRITE_WR_LEN16	0xff
#define V_FW_SCSI_WRITE_WR_LEN16(x)	((x) << S_FW_SCSI_WRITE_WR_LEN16)
#define G_FW_SCSI_WRITE_WR_LEN16(x)	\
    (((x) >> S_FW_SCSI_WRITE_WR_LEN16) & M_FW_SCSI_WRITE_WR_LEN16)

#define S_FW_SCSI_WRITE_WR_CP_EN	6
#define M_FW_SCSI_WRITE_WR_CP_EN	0x3
#define V_FW_SCSI_WRITE_WR_CP_EN(x)	((x) << S_FW_SCSI_WRITE_WR_CP_EN)
#define G_FW_SCSI_WRITE_WR_CP_EN(x)	\
    (((x) >> S_FW_SCSI_WRITE_WR_CP_EN) & M_FW_SCSI_WRITE_WR_CP_EN)

#define S_FW_SCSI_WRITE_WR_CLASS	4
#define M_FW_SCSI_WRITE_WR_CLASS	0x3
#define V_FW_SCSI_WRITE_WR_CLASS(x)	((x) << S_FW_SCSI_WRITE_WR_CLASS)
#define G_FW_SCSI_WRITE_WR_CLASS(x)	\
    (((x) >> S_FW_SCSI_WRITE_WR_CLASS) & M_FW_SCSI_WRITE_WR_CLASS)

struct fw_scsi_read_wr {
	__be32 op_immdlen;
	__be32 flowid_len16;
	__be64 cookie;
	__be16 iqid;
	__u8   tmo_val;
	__u8   use_xfer_cnt;
	union fw_scsi_read_priv {
		struct fcoe_read_priv {
			__u8   ctl_pri;
			__u8   cp_en_class;
			__u8   r3_lo[2];
		} fcoe;
		struct iscsi_read_priv {
			__u8   r3[4];
		} iscsi;
	} u;
	__be32 xfer_cnt;
	__be32 ini_xfer_cnt;
	__be64 rsp_dmaaddr;
	__be32 rsp_dmalen;
	__be32 r4;
};

#define S_FW_SCSI_READ_WR_OPCODE	24
#define M_FW_SCSI_READ_WR_OPCODE	0xff
#define V_FW_SCSI_READ_WR_OPCODE(x)	((x) << S_FW_SCSI_READ_WR_OPCODE)
#define G_FW_SCSI_READ_WR_OPCODE(x)	\
    (((x) >> S_FW_SCSI_READ_WR_OPCODE) & M_FW_SCSI_READ_WR_OPCODE)

#define S_FW_SCSI_READ_WR_IMMDLEN	0
#define M_FW_SCSI_READ_WR_IMMDLEN	0xff
#define V_FW_SCSI_READ_WR_IMMDLEN(x)	((x) << S_FW_SCSI_READ_WR_IMMDLEN)
#define G_FW_SCSI_READ_WR_IMMDLEN(x)	\
    (((x) >> S_FW_SCSI_READ_WR_IMMDLEN) & M_FW_SCSI_READ_WR_IMMDLEN)

#define S_FW_SCSI_READ_WR_FLOWID	8
#define M_FW_SCSI_READ_WR_FLOWID	0xfffff
#define V_FW_SCSI_READ_WR_FLOWID(x)	((x) << S_FW_SCSI_READ_WR_FLOWID)
#define G_FW_SCSI_READ_WR_FLOWID(x)	\
    (((x) >> S_FW_SCSI_READ_WR_FLOWID) & M_FW_SCSI_READ_WR_FLOWID)

#define S_FW_SCSI_READ_WR_LEN16		0
#define M_FW_SCSI_READ_WR_LEN16		0xff
#define V_FW_SCSI_READ_WR_LEN16(x)	((x) << S_FW_SCSI_READ_WR_LEN16)
#define G_FW_SCSI_READ_WR_LEN16(x)	\
    (((x) >> S_FW_SCSI_READ_WR_LEN16) & M_FW_SCSI_READ_WR_LEN16)

#define S_FW_SCSI_READ_WR_CP_EN		6
#define M_FW_SCSI_READ_WR_CP_EN		0x3
#define V_FW_SCSI_READ_WR_CP_EN(x)	((x) << S_FW_SCSI_READ_WR_CP_EN)
#define G_FW_SCSI_READ_WR_CP_EN(x)	\
    (((x) >> S_FW_SCSI_READ_WR_CP_EN) & M_FW_SCSI_READ_WR_CP_EN)

#define S_FW_SCSI_READ_WR_CLASS		4
#define M_FW_SCSI_READ_WR_CLASS		0x3
#define V_FW_SCSI_READ_WR_CLASS(x)	((x) << S_FW_SCSI_READ_WR_CLASS)
#define G_FW_SCSI_READ_WR_CLASS(x)	\
    (((x) >> S_FW_SCSI_READ_WR_CLASS) & M_FW_SCSI_READ_WR_CLASS)

struct fw_scsi_cmd_wr {
	__be32 op_immdlen;
	__be32 flowid_len16;
	__be64 cookie;
	__be16 iqid;
	__u8   tmo_val;
	__u8   r3;
	union fw_scsi_cmd_priv {
		struct fcoe_cmd_priv {
			__u8   ctl_pri;
			__u8   cp_en_class;
			__u8   r4_lo[2];
		} fcoe;
		struct iscsi_cmd_priv {
			__u8   r4[4];
		} iscsi;
	} u;
	__u8   r5[8];
	__be64 rsp_dmaaddr;
	__be32 rsp_dmalen;
	__be32 r6;
};

#define S_FW_SCSI_CMD_WR_OPCODE		24
#define M_FW_SCSI_CMD_WR_OPCODE		0xff
#define V_FW_SCSI_CMD_WR_OPCODE(x)	((x) << S_FW_SCSI_CMD_WR_OPCODE)
#define G_FW_SCSI_CMD_WR_OPCODE(x)	\
    (((x) >> S_FW_SCSI_CMD_WR_OPCODE) & M_FW_SCSI_CMD_WR_OPCODE)

#define S_FW_SCSI_CMD_WR_IMMDLEN	0
#define M_FW_SCSI_CMD_WR_IMMDLEN	0xff
#define V_FW_SCSI_CMD_WR_IMMDLEN(x)	((x) << S_FW_SCSI_CMD_WR_IMMDLEN)
#define G_FW_SCSI_CMD_WR_IMMDLEN(x)	\
    (((x) >> S_FW_SCSI_CMD_WR_IMMDLEN) & M_FW_SCSI_CMD_WR_IMMDLEN)

#define S_FW_SCSI_CMD_WR_FLOWID		8
#define M_FW_SCSI_CMD_WR_FLOWID		0xfffff
#define V_FW_SCSI_CMD_WR_FLOWID(x)	((x) << S_FW_SCSI_CMD_WR_FLOWID)
#define G_FW_SCSI_CMD_WR_FLOWID(x)	\
    (((x) >> S_FW_SCSI_CMD_WR_FLOWID) & M_FW_SCSI_CMD_WR_FLOWID)

#define S_FW_SCSI_CMD_WR_LEN16		0
#define M_FW_SCSI_CMD_WR_LEN16		0xff
#define V_FW_SCSI_CMD_WR_LEN16(x)	((x) << S_FW_SCSI_CMD_WR_LEN16)
#define G_FW_SCSI_CMD_WR_LEN16(x)	\
    (((x) >> S_FW_SCSI_CMD_WR_LEN16) & M_FW_SCSI_CMD_WR_LEN16)

#define S_FW_SCSI_CMD_WR_CP_EN		6
#define M_FW_SCSI_CMD_WR_CP_EN		0x3
#define V_FW_SCSI_CMD_WR_CP_EN(x)	((x) << S_FW_SCSI_CMD_WR_CP_EN)
#define G_FW_SCSI_CMD_WR_CP_EN(x)	\
    (((x) >> S_FW_SCSI_CMD_WR_CP_EN) & M_FW_SCSI_CMD_WR_CP_EN)

#define S_FW_SCSI_CMD_WR_CLASS		4
#define M_FW_SCSI_CMD_WR_CLASS		0x3
#define V_FW_SCSI_CMD_WR_CLASS(x)	((x) << S_FW_SCSI_CMD_WR_CLASS)
#define G_FW_SCSI_CMD_WR_CLASS(x)	\
    (((x) >> S_FW_SCSI_CMD_WR_CLASS) & M_FW_SCSI_CMD_WR_CLASS)

struct fw_scsi_abrt_cls_wr {
	__be32 op_immdlen;
	__be32 flowid_len16;
	__be64 cookie;
	__be16 iqid;
	__u8   tmo_val;
	__u8   sub_opcode_to_chk_all_io;
	__u8   r3[4];
	__be64 t_cookie;
};

#define S_FW_SCSI_ABRT_CLS_WR_OPCODE	24
#define M_FW_SCSI_ABRT_CLS_WR_OPCODE	0xff
#define V_FW_SCSI_ABRT_CLS_WR_OPCODE(x)	((x) << S_FW_SCSI_ABRT_CLS_WR_OPCODE)
#define G_FW_SCSI_ABRT_CLS_WR_OPCODE(x)	\
    (((x) >> S_FW_SCSI_ABRT_CLS_WR_OPCODE) & M_FW_SCSI_ABRT_CLS_WR_OPCODE)

#define S_FW_SCSI_ABRT_CLS_WR_IMMDLEN		0
#define M_FW_SCSI_ABRT_CLS_WR_IMMDLEN		0xff
#define V_FW_SCSI_ABRT_CLS_WR_IMMDLEN(x)	\
    ((x) << S_FW_SCSI_ABRT_CLS_WR_IMMDLEN)
#define G_FW_SCSI_ABRT_CLS_WR_IMMDLEN(x)	\
    (((x) >> S_FW_SCSI_ABRT_CLS_WR_IMMDLEN) & M_FW_SCSI_ABRT_CLS_WR_IMMDLEN)

#define S_FW_SCSI_ABRT_CLS_WR_FLOWID	8
#define M_FW_SCSI_ABRT_CLS_WR_FLOWID	0xfffff
#define V_FW_SCSI_ABRT_CLS_WR_FLOWID(x)	((x) << S_FW_SCSI_ABRT_CLS_WR_FLOWID)
#define G_FW_SCSI_ABRT_CLS_WR_FLOWID(x)	\
    (((x) >> S_FW_SCSI_ABRT_CLS_WR_FLOWID) & M_FW_SCSI_ABRT_CLS_WR_FLOWID)

#define S_FW_SCSI_ABRT_CLS_WR_LEN16	0
#define M_FW_SCSI_ABRT_CLS_WR_LEN16	0xff
#define V_FW_SCSI_ABRT_CLS_WR_LEN16(x)	((x) << S_FW_SCSI_ABRT_CLS_WR_LEN16)
#define G_FW_SCSI_ABRT_CLS_WR_LEN16(x)	\
    (((x) >> S_FW_SCSI_ABRT_CLS_WR_LEN16) & M_FW_SCSI_ABRT_CLS_WR_LEN16)

#define S_FW_SCSI_ABRT_CLS_WR_SUB_OPCODE	2
#define M_FW_SCSI_ABRT_CLS_WR_SUB_OPCODE	0x3f
#define V_FW_SCSI_ABRT_CLS_WR_SUB_OPCODE(x)	\
    ((x) << S_FW_SCSI_ABRT_CLS_WR_SUB_OPCODE)
#define G_FW_SCSI_ABRT_CLS_WR_SUB_OPCODE(x)	\
    (((x) >> S_FW_SCSI_ABRT_CLS_WR_SUB_OPCODE) & \
     M_FW_SCSI_ABRT_CLS_WR_SUB_OPCODE)

#define S_FW_SCSI_ABRT_CLS_WR_UNSOL	1
#define M_FW_SCSI_ABRT_CLS_WR_UNSOL	0x1
#define V_FW_SCSI_ABRT_CLS_WR_UNSOL(x)	((x) << S_FW_SCSI_ABRT_CLS_WR_UNSOL)
#define G_FW_SCSI_ABRT_CLS_WR_UNSOL(x)	\
    (((x) >> S_FW_SCSI_ABRT_CLS_WR_UNSOL) & M_FW_SCSI_ABRT_CLS_WR_UNSOL)
#define F_FW_SCSI_ABRT_CLS_WR_UNSOL	V_FW_SCSI_ABRT_CLS_WR_UNSOL(1U)

#define S_FW_SCSI_ABRT_CLS_WR_CHK_ALL_IO	0
#define M_FW_SCSI_ABRT_CLS_WR_CHK_ALL_IO	0x1
#define V_FW_SCSI_ABRT_CLS_WR_CHK_ALL_IO(x)	\
    ((x) << S_FW_SCSI_ABRT_CLS_WR_CHK_ALL_IO)
#define G_FW_SCSI_ABRT_CLS_WR_CHK_ALL_IO(x)	\
    (((x) >> S_FW_SCSI_ABRT_CLS_WR_CHK_ALL_IO) & \
     M_FW_SCSI_ABRT_CLS_WR_CHK_ALL_IO)
#define F_FW_SCSI_ABRT_CLS_WR_CHK_ALL_IO	\
    V_FW_SCSI_ABRT_CLS_WR_CHK_ALL_IO(1U)

struct fw_scsi_tgt_acc_wr {
	__be32 op_immdlen;
	__be32 flowid_len16;
	__be64 cookie;
	__be16 iqid;
	__u8   r3;
	__u8   use_burst_len;
	union fw_scsi_tgt_acc_priv {
		struct fcoe_tgt_acc_priv {
			__u8   ctl_pri;
			__u8   cp_en_class;
			__u8   r4_lo[2];
		} fcoe;
		struct iscsi_tgt_acc_priv {
			__u8   r4[4];
		} iscsi;
	} u;
	__be32 burst_len;
	__be32 rel_off;
	__be64 r5;
	__be32 r6;
	__be32 tot_xfer_len;
};

#define S_FW_SCSI_TGT_ACC_WR_OPCODE	24
#define M_FW_SCSI_TGT_ACC_WR_OPCODE	0xff
#define V_FW_SCSI_TGT_ACC_WR_OPCODE(x)	((x) << S_FW_SCSI_TGT_ACC_WR_OPCODE)
#define G_FW_SCSI_TGT_ACC_WR_OPCODE(x)	\
    (((x) >> S_FW_SCSI_TGT_ACC_WR_OPCODE) & M_FW_SCSI_TGT_ACC_WR_OPCODE)

#define S_FW_SCSI_TGT_ACC_WR_IMMDLEN	0
#define M_FW_SCSI_TGT_ACC_WR_IMMDLEN	0xff
#define V_FW_SCSI_TGT_ACC_WR_IMMDLEN(x)	((x) << S_FW_SCSI_TGT_ACC_WR_IMMDLEN)
#define G_FW_SCSI_TGT_ACC_WR_IMMDLEN(x)	\
    (((x) >> S_FW_SCSI_TGT_ACC_WR_IMMDLEN) & M_FW_SCSI_TGT_ACC_WR_IMMDLEN)

#define S_FW_SCSI_TGT_ACC_WR_FLOWID	8
#define M_FW_SCSI_TGT_ACC_WR_FLOWID	0xfffff
#define V_FW_SCSI_TGT_ACC_WR_FLOWID(x)	((x) << S_FW_SCSI_TGT_ACC_WR_FLOWID)
#define G_FW_SCSI_TGT_ACC_WR_FLOWID(x)	\
    (((x) >> S_FW_SCSI_TGT_ACC_WR_FLOWID) & M_FW_SCSI_TGT_ACC_WR_FLOWID)

#define S_FW_SCSI_TGT_ACC_WR_LEN16	0
#define M_FW_SCSI_TGT_ACC_WR_LEN16	0xff
#define V_FW_SCSI_TGT_ACC_WR_LEN16(x)	((x) << S_FW_SCSI_TGT_ACC_WR_LEN16)
#define G_FW_SCSI_TGT_ACC_WR_LEN16(x)	\
    (((x) >> S_FW_SCSI_TGT_ACC_WR_LEN16) & M_FW_SCSI_TGT_ACC_WR_LEN16)

#define S_FW_SCSI_TGT_ACC_WR_CP_EN	6
#define M_FW_SCSI_TGT_ACC_WR_CP_EN	0x3
#define V_FW_SCSI_TGT_ACC_WR_CP_EN(x)	((x) << S_FW_SCSI_TGT_ACC_WR_CP_EN)
#define G_FW_SCSI_TGT_ACC_WR_CP_EN(x)	\
    (((x) >> S_FW_SCSI_TGT_ACC_WR_CP_EN) & M_FW_SCSI_TGT_ACC_WR_CP_EN)

#define S_FW_SCSI_TGT_ACC_WR_CLASS	4
#define M_FW_SCSI_TGT_ACC_WR_CLASS	0x3
#define V_FW_SCSI_TGT_ACC_WR_CLASS(x)	((x) << S_FW_SCSI_TGT_ACC_WR_CLASS)
#define G_FW_SCSI_TGT_ACC_WR_CLASS(x)	\
    (((x) >> S_FW_SCSI_TGT_ACC_WR_CLASS) & M_FW_SCSI_TGT_ACC_WR_CLASS)

struct fw_scsi_tgt_xmit_wr {
	__be32 op_immdlen;
	__be32 flowid_len16;
	__be64 cookie;
	__be16 iqid;
	__u8   auto_rsp;
	__u8   use_xfer_cnt;
	union fw_scsi_tgt_xmit_priv {
		struct fcoe_tgt_xmit_priv {
			__u8   ctl_pri;
			__u8   cp_en_class;
			__u8   r3_lo[2];
		} fcoe;
		struct iscsi_tgt_xmit_priv {
			__u8   r3[4];
		} iscsi;
	} u;
	__be32 xfer_cnt;
	__be32 r4;
	__be64 r5;
	__be32 r6;
	__be32 tot_xfer_len;
};

#define S_FW_SCSI_TGT_XMIT_WR_OPCODE	24
#define M_FW_SCSI_TGT_XMIT_WR_OPCODE	0xff
#define V_FW_SCSI_TGT_XMIT_WR_OPCODE(x)	((x) << S_FW_SCSI_TGT_XMIT_WR_OPCODE)
#define G_FW_SCSI_TGT_XMIT_WR_OPCODE(x)	\
    (((x) >> S_FW_SCSI_TGT_XMIT_WR_OPCODE) & M_FW_SCSI_TGT_XMIT_WR_OPCODE)

#define S_FW_SCSI_TGT_XMIT_WR_IMMDLEN		0
#define M_FW_SCSI_TGT_XMIT_WR_IMMDLEN		0xff
#define V_FW_SCSI_TGT_XMIT_WR_IMMDLEN(x)	\
    ((x) << S_FW_SCSI_TGT_XMIT_WR_IMMDLEN)
#define G_FW_SCSI_TGT_XMIT_WR_IMMDLEN(x)	\
    (((x) >> S_FW_SCSI_TGT_XMIT_WR_IMMDLEN) & M_FW_SCSI_TGT_XMIT_WR_IMMDLEN)

#define S_FW_SCSI_TGT_XMIT_WR_FLOWID	8
#define M_FW_SCSI_TGT_XMIT_WR_FLOWID	0xfffff
#define V_FW_SCSI_TGT_XMIT_WR_FLOWID(x)	((x) << S_FW_SCSI_TGT_XMIT_WR_FLOWID)
#define G_FW_SCSI_TGT_XMIT_WR_FLOWID(x)	\
    (((x) >> S_FW_SCSI_TGT_XMIT_WR_FLOWID) & M_FW_SCSI_TGT_XMIT_WR_FLOWID)

#define S_FW_SCSI_TGT_XMIT_WR_LEN16	0
#define M_FW_SCSI_TGT_XMIT_WR_LEN16	0xff
#define V_FW_SCSI_TGT_XMIT_WR_LEN16(x)	((x) << S_FW_SCSI_TGT_XMIT_WR_LEN16)
#define G_FW_SCSI_TGT_XMIT_WR_LEN16(x)	\
    (((x) >> S_FW_SCSI_TGT_XMIT_WR_LEN16) & M_FW_SCSI_TGT_XMIT_WR_LEN16)

#define S_FW_SCSI_TGT_XMIT_WR_CP_EN	6
#define M_FW_SCSI_TGT_XMIT_WR_CP_EN	0x3
#define V_FW_SCSI_TGT_XMIT_WR_CP_EN(x)	((x) << S_FW_SCSI_TGT_XMIT_WR_CP_EN)
#define G_FW_SCSI_TGT_XMIT_WR_CP_EN(x)	\
    (((x) >> S_FW_SCSI_TGT_XMIT_WR_CP_EN) & M_FW_SCSI_TGT_XMIT_WR_CP_EN)

#define S_FW_SCSI_TGT_XMIT_WR_CLASS	4
#define M_FW_SCSI_TGT_XMIT_WR_CLASS	0x3
#define V_FW_SCSI_TGT_XMIT_WR_CLASS(x)	((x) << S_FW_SCSI_TGT_XMIT_WR_CLASS)
#define G_FW_SCSI_TGT_XMIT_WR_CLASS(x)	\
    (((x) >> S_FW_SCSI_TGT_XMIT_WR_CLASS) & M_FW_SCSI_TGT_XMIT_WR_CLASS)

struct fw_scsi_tgt_rsp_wr {
	__be32 op_immdlen;
	__be32 flowid_len16;
	__be64 cookie;
	__be16 iqid;
	__u8   r3[2];
	union fw_scsi_tgt_rsp_priv {
		struct fcoe_tgt_rsp_priv {
			__u8   ctl_pri;
			__u8   cp_en_class;
			__u8   r4_lo[2];
		} fcoe;
		struct iscsi_tgt_rsp_priv {
			__u8   r4[4];
		} iscsi;
	} u;
	__u8   r5[8];
};

#define S_FW_SCSI_TGT_RSP_WR_OPCODE	24
#define M_FW_SCSI_TGT_RSP_WR_OPCODE	0xff
#define V_FW_SCSI_TGT_RSP_WR_OPCODE(x)	((x) << S_FW_SCSI_TGT_RSP_WR_OPCODE)
#define G_FW_SCSI_TGT_RSP_WR_OPCODE(x)	\
    (((x) >> S_FW_SCSI_TGT_RSP_WR_OPCODE) & M_FW_SCSI_TGT_RSP_WR_OPCODE)

#define S_FW_SCSI_TGT_RSP_WR_IMMDLEN	0
#define M_FW_SCSI_TGT_RSP_WR_IMMDLEN	0xff
#define V_FW_SCSI_TGT_RSP_WR_IMMDLEN(x)	((x) << S_FW_SCSI_TGT_RSP_WR_IMMDLEN)
#define G_FW_SCSI_TGT_RSP_WR_IMMDLEN(x)	\
    (((x) >> S_FW_SCSI_TGT_RSP_WR_IMMDLEN) & M_FW_SCSI_TGT_RSP_WR_IMMDLEN)

#define S_FW_SCSI_TGT_RSP_WR_FLOWID	8
#define M_FW_SCSI_TGT_RSP_WR_FLOWID	0xfffff
#define V_FW_SCSI_TGT_RSP_WR_FLOWID(x)	((x) << S_FW_SCSI_TGT_RSP_WR_FLOWID)
#define G_FW_SCSI_TGT_RSP_WR_FLOWID(x)	\
    (((x) >> S_FW_SCSI_TGT_RSP_WR_FLOWID) & M_FW_SCSI_TGT_RSP_WR_FLOWID)

#define S_FW_SCSI_TGT_RSP_WR_LEN16	0
#define M_FW_SCSI_TGT_RSP_WR_LEN16	0xff
#define V_FW_SCSI_TGT_RSP_WR_LEN16(x)	((x) << S_FW_SCSI_TGT_RSP_WR_LEN16)
#define G_FW_SCSI_TGT_RSP_WR_LEN16(x)	\
    (((x) >> S_FW_SCSI_TGT_RSP_WR_LEN16) & M_FW_SCSI_TGT_RSP_WR_LEN16)

#define S_FW_SCSI_TGT_RSP_WR_CP_EN	6
#define M_FW_SCSI_TGT_RSP_WR_CP_EN	0x3
#define V_FW_SCSI_TGT_RSP_WR_CP_EN(x)	((x) << S_FW_SCSI_TGT_RSP_WR_CP_EN)
#define G_FW_SCSI_TGT_RSP_WR_CP_EN(x)	\
    (((x) >> S_FW_SCSI_TGT_RSP_WR_CP_EN) & M_FW_SCSI_TGT_RSP_WR_CP_EN)

#define S_FW_SCSI_TGT_RSP_WR_CLASS	4
#define M_FW_SCSI_TGT_RSP_WR_CLASS	0x3
#define V_FW_SCSI_TGT_RSP_WR_CLASS(x)	((x) << S_FW_SCSI_TGT_RSP_WR_CLASS)
#define G_FW_SCSI_TGT_RSP_WR_CLASS(x)	\
    (((x) >> S_FW_SCSI_TGT_RSP_WR_CLASS) & M_FW_SCSI_TGT_RSP_WR_CLASS)

struct fw_pofcoe_tcb_wr {
	__be32 op_compl;
	__be32 equiq_to_len16;
	__be32 r4;
	__be32 xfer_len;
	__be32 tid_to_port;
	__be16 x_id;
	__be16 vlan_id;
	__be64 cookie;
	__be32 s_id;
	__be32 d_id;
	__be32 tag;
	__be16 r6;
	__be16 iqid;
};

#define S_FW_POFCOE_TCB_WR_TID		12
#define M_FW_POFCOE_TCB_WR_TID		0xfffff
#define V_FW_POFCOE_TCB_WR_TID(x)	((x) << S_FW_POFCOE_TCB_WR_TID)
#define G_FW_POFCOE_TCB_WR_TID(x)	\
    (((x) >> S_FW_POFCOE_TCB_WR_TID) & M_FW_POFCOE_TCB_WR_TID)

#define S_FW_POFCOE_TCB_WR_ALLOC	4
#define M_FW_POFCOE_TCB_WR_ALLOC	0x1
#define V_FW_POFCOE_TCB_WR_ALLOC(x)	((x) << S_FW_POFCOE_TCB_WR_ALLOC)
#define G_FW_POFCOE_TCB_WR_ALLOC(x)	\
    (((x) >> S_FW_POFCOE_TCB_WR_ALLOC) & M_FW_POFCOE_TCB_WR_ALLOC)
#define F_FW_POFCOE_TCB_WR_ALLOC	V_FW_POFCOE_TCB_WR_ALLOC(1U)

#define S_FW_POFCOE_TCB_WR_FREE		3
#define M_FW_POFCOE_TCB_WR_FREE		0x1
#define V_FW_POFCOE_TCB_WR_FREE(x)	((x) << S_FW_POFCOE_TCB_WR_FREE)
#define G_FW_POFCOE_TCB_WR_FREE(x)	\
    (((x) >> S_FW_POFCOE_TCB_WR_FREE) & M_FW_POFCOE_TCB_WR_FREE)
#define F_FW_POFCOE_TCB_WR_FREE	V_FW_POFCOE_TCB_WR_FREE(1U)

#define S_FW_POFCOE_TCB_WR_PORT		0
#define M_FW_POFCOE_TCB_WR_PORT		0x7
#define V_FW_POFCOE_TCB_WR_PORT(x)	((x) << S_FW_POFCOE_TCB_WR_PORT)
#define G_FW_POFCOE_TCB_WR_PORT(x)	\
    (((x) >> S_FW_POFCOE_TCB_WR_PORT) & M_FW_POFCOE_TCB_WR_PORT)

struct fw_pofcoe_ulptx_wr {
	__be32 op_pkd;
	__be32 equiq_to_len16;
	__u64  cookie;
};

/*******************************************************************
 *  T10 DIF related definition
 *******************************************************************/
struct fw_tx_pi_header {
	__be16 op_to_inline;
	__u8   pi_interval_tag_type;
	__u8   num_pi;
	__be32 pi_start4_pi_end4;
	__u8   tag_gen_enabled_pkd;
	__u8   num_pi_dsg;
	__be16 app_tag;
	__be32 ref_tag;
};

#define S_FW_TX_PI_HEADER_OP	8
#define M_FW_TX_PI_HEADER_OP	0xff
#define V_FW_TX_PI_HEADER_OP(x)	((x) << S_FW_TX_PI_HEADER_OP)
#define G_FW_TX_PI_HEADER_OP(x)	\
    (((x) >> S_FW_TX_PI_HEADER_OP) & M_FW_TX_PI_HEADER_OP)

#define S_FW_TX_PI_HEADER_ULPTXMORE	7
#define M_FW_TX_PI_HEADER_ULPTXMORE	0x1
#define V_FW_TX_PI_HEADER_ULPTXMORE(x)	((x) << S_FW_TX_PI_HEADER_ULPTXMORE)
#define G_FW_TX_PI_HEADER_ULPTXMORE(x)	\
    (((x) >> S_FW_TX_PI_HEADER_ULPTXMORE) & M_FW_TX_PI_HEADER_ULPTXMORE)
#define F_FW_TX_PI_HEADER_ULPTXMORE	V_FW_TX_PI_HEADER_ULPTXMORE(1U)

#define S_FW_TX_PI_HEADER_PI_CONTROL	4
#define M_FW_TX_PI_HEADER_PI_CONTROL	0x7
#define V_FW_TX_PI_HEADER_PI_CONTROL(x)	((x) << S_FW_TX_PI_HEADER_PI_CONTROL)
#define G_FW_TX_PI_HEADER_PI_CONTROL(x)	\
    (((x) >> S_FW_TX_PI_HEADER_PI_CONTROL) & M_FW_TX_PI_HEADER_PI_CONTROL)

#define S_FW_TX_PI_HEADER_GUARD_TYPE	2
#define M_FW_TX_PI_HEADER_GUARD_TYPE	0x1
#define V_FW_TX_PI_HEADER_GUARD_TYPE(x)	((x) << S_FW_TX_PI_HEADER_GUARD_TYPE)
#define G_FW_TX_PI_HEADER_GUARD_TYPE(x)	\
    (((x) >> S_FW_TX_PI_HEADER_GUARD_TYPE) & M_FW_TX_PI_HEADER_GUARD_TYPE)
#define F_FW_TX_PI_HEADER_GUARD_TYPE	V_FW_TX_PI_HEADER_GUARD_TYPE(1U)

#define S_FW_TX_PI_HEADER_VALIDATE	1
#define M_FW_TX_PI_HEADER_VALIDATE	0x1
#define V_FW_TX_PI_HEADER_VALIDATE(x)	((x) << S_FW_TX_PI_HEADER_VALIDATE)
#define G_FW_TX_PI_HEADER_VALIDATE(x)	\
    (((x) >> S_FW_TX_PI_HEADER_VALIDATE) & M_FW_TX_PI_HEADER_VALIDATE)
#define F_FW_TX_PI_HEADER_VALIDATE	V_FW_TX_PI_HEADER_VALIDATE(1U)

#define S_FW_TX_PI_HEADER_INLINE	0
#define M_FW_TX_PI_HEADER_INLINE	0x1
#define V_FW_TX_PI_HEADER_INLINE(x)	((x) << S_FW_TX_PI_HEADER_INLINE)
#define G_FW_TX_PI_HEADER_INLINE(x)	\
    (((x) >> S_FW_TX_PI_HEADER_INLINE) & M_FW_TX_PI_HEADER_INLINE)
#define F_FW_TX_PI_HEADER_INLINE	V_FW_TX_PI_HEADER_INLINE(1U)

#define S_FW_TX_PI_HEADER_PI_INTERVAL		7
#define M_FW_TX_PI_HEADER_PI_INTERVAL		0x1
#define V_FW_TX_PI_HEADER_PI_INTERVAL(x)	\
    ((x) << S_FW_TX_PI_HEADER_PI_INTERVAL)
#define G_FW_TX_PI_HEADER_PI_INTERVAL(x)	\
    (((x) >> S_FW_TX_PI_HEADER_PI_INTERVAL) & M_FW_TX_PI_HEADER_PI_INTERVAL)
#define F_FW_TX_PI_HEADER_PI_INTERVAL	V_FW_TX_PI_HEADER_PI_INTERVAL(1U)

#define S_FW_TX_PI_HEADER_TAG_TYPE	5
#define M_FW_TX_PI_HEADER_TAG_TYPE	0x3
#define V_FW_TX_PI_HEADER_TAG_TYPE(x)	((x) << S_FW_TX_PI_HEADER_TAG_TYPE)
#define G_FW_TX_PI_HEADER_TAG_TYPE(x)	\
    (((x) >> S_FW_TX_PI_HEADER_TAG_TYPE) & M_FW_TX_PI_HEADER_TAG_TYPE)

#define S_FW_TX_PI_HEADER_PI_START4	22
#define M_FW_TX_PI_HEADER_PI_START4	0x3ff
#define V_FW_TX_PI_HEADER_PI_START4(x)	((x) << S_FW_TX_PI_HEADER_PI_START4)
#define G_FW_TX_PI_HEADER_PI_START4(x)	\
    (((x) >> S_FW_TX_PI_HEADER_PI_START4) & M_FW_TX_PI_HEADER_PI_START4)

#define S_FW_TX_PI_HEADER_PI_END4	0
#define M_FW_TX_PI_HEADER_PI_END4	0x3fffff
#define V_FW_TX_PI_HEADER_PI_END4(x)	((x) << S_FW_TX_PI_HEADER_PI_END4)
#define G_FW_TX_PI_HEADER_PI_END4(x)	\
    (((x) >> S_FW_TX_PI_HEADER_PI_END4) & M_FW_TX_PI_HEADER_PI_END4)

#define S_FW_TX_PI_HEADER_TAG_GEN_ENABLED	6
#define M_FW_TX_PI_HEADER_TAG_GEN_ENABLED	0x3
#define V_FW_TX_PI_HEADER_TAG_GEN_ENABLED(x)	\
    ((x) << S_FW_TX_PI_HEADER_TAG_GEN_ENABLED)
#define G_FW_TX_PI_HEADER_TAG_GEN_ENABLED(x)	\
    (((x) >> S_FW_TX_PI_HEADER_TAG_GEN_ENABLED) & \
     M_FW_TX_PI_HEADER_TAG_GEN_ENABLED)

enum fw_pi_error_type {
	FW_PI_ERROR_GUARD_CHECK_FAILED = 0,
};

struct fw_pi_error {
	__be32 err_type_pkd;
	__be32 flowid_len16;
	__be16 r2;
	__be16 app_tag;
	__be32 ref_tag;
	__be32  pisc[4];
};

#define S_FW_PI_ERROR_ERR_TYPE		24
#define M_FW_PI_ERROR_ERR_TYPE		0xff
#define V_FW_PI_ERROR_ERR_TYPE(x)	((x) << S_FW_PI_ERROR_ERR_TYPE)
#define G_FW_PI_ERROR_ERR_TYPE(x)	\
    (((x) >> S_FW_PI_ERROR_ERR_TYPE) & M_FW_PI_ERROR_ERR_TYPE)

struct fw_tlstx_data_wr {
        __be32 op_to_immdlen;
        __be32 flowid_len16;
        __be32 plen;
        __be32 lsodisable_to_flags;
        __be32 r5;
        __be32 ctxloc_to_exp;
        __be16 mfs;
        __be16 adjustedplen_pkd;
        __be16 expinplenmax_pkd;
        __u8   pdusinplenmax_pkd;
        __u8   r10;
};

#define S_FW_TLSTX_DATA_WR_OPCODE       24
#define M_FW_TLSTX_DATA_WR_OPCODE       0xff
#define V_FW_TLSTX_DATA_WR_OPCODE(x)    ((x) << S_FW_TLSTX_DATA_WR_OPCODE)
#define G_FW_TLSTX_DATA_WR_OPCODE(x)    \
    (((x) >> S_FW_TLSTX_DATA_WR_OPCODE) & M_FW_TLSTX_DATA_WR_OPCODE)

#define S_FW_TLSTX_DATA_WR_COMPL        21
#define M_FW_TLSTX_DATA_WR_COMPL        0x1
#define V_FW_TLSTX_DATA_WR_COMPL(x)     ((x) << S_FW_TLSTX_DATA_WR_COMPL)
#define G_FW_TLSTX_DATA_WR_COMPL(x)     \
    (((x) >> S_FW_TLSTX_DATA_WR_COMPL) & M_FW_TLSTX_DATA_WR_COMPL)
#define F_FW_TLSTX_DATA_WR_COMPL        V_FW_TLSTX_DATA_WR_COMPL(1U)

#define S_FW_TLSTX_DATA_WR_IMMDLEN      0
#define M_FW_TLSTX_DATA_WR_IMMDLEN      0xff
#define V_FW_TLSTX_DATA_WR_IMMDLEN(x)   ((x) << S_FW_TLSTX_DATA_WR_IMMDLEN)
#define G_FW_TLSTX_DATA_WR_IMMDLEN(x)   \
    (((x) >> S_FW_TLSTX_DATA_WR_IMMDLEN) & M_FW_TLSTX_DATA_WR_IMMDLEN)

#define S_FW_TLSTX_DATA_WR_FLOWID       8
#define M_FW_TLSTX_DATA_WR_FLOWID       0xfffff
#define V_FW_TLSTX_DATA_WR_FLOWID(x)    ((x) << S_FW_TLSTX_DATA_WR_FLOWID)
#define G_FW_TLSTX_DATA_WR_FLOWID(x)    \
    (((x) >> S_FW_TLSTX_DATA_WR_FLOWID) & M_FW_TLSTX_DATA_WR_FLOWID)

#define S_FW_TLSTX_DATA_WR_LEN16        0
#define M_FW_TLSTX_DATA_WR_LEN16        0xff
#define V_FW_TLSTX_DATA_WR_LEN16(x)     ((x) << S_FW_TLSTX_DATA_WR_LEN16)
#define G_FW_TLSTX_DATA_WR_LEN16(x)     \
    (((x) >> S_FW_TLSTX_DATA_WR_LEN16) & M_FW_TLSTX_DATA_WR_LEN16)

#define S_FW_TLSTX_DATA_WR_LSODISABLE   31
#define M_FW_TLSTX_DATA_WR_LSODISABLE   0x1
#define V_FW_TLSTX_DATA_WR_LSODISABLE(x) \
    ((x) << S_FW_TLSTX_DATA_WR_LSODISABLE)
#define G_FW_TLSTX_DATA_WR_LSODISABLE(x) \
    (((x) >> S_FW_TLSTX_DATA_WR_LSODISABLE) & M_FW_TLSTX_DATA_WR_LSODISABLE)
#define F_FW_TLSTX_DATA_WR_LSODISABLE   V_FW_TLSTX_DATA_WR_LSODISABLE(1U)

#define S_FW_TLSTX_DATA_WR_ALIGNPLD     30
#define M_FW_TLSTX_DATA_WR_ALIGNPLD     0x1
#define V_FW_TLSTX_DATA_WR_ALIGNPLD(x)  ((x) << S_FW_TLSTX_DATA_WR_ALIGNPLD)
#define G_FW_TLSTX_DATA_WR_ALIGNPLD(x)  \
    (((x) >> S_FW_TLSTX_DATA_WR_ALIGNPLD) & M_FW_TLSTX_DATA_WR_ALIGNPLD)
#define F_FW_TLSTX_DATA_WR_ALIGNPLD     V_FW_TLSTX_DATA_WR_ALIGNPLD(1U)

#define S_FW_TLSTX_DATA_WR_ALIGNPLDSHOVE 29
#define M_FW_TLSTX_DATA_WR_ALIGNPLDSHOVE 0x1
#define V_FW_TLSTX_DATA_WR_ALIGNPLDSHOVE(x) \
    ((x) << S_FW_TLSTX_DATA_WR_ALIGNPLDSHOVE)
#define G_FW_TLSTX_DATA_WR_ALIGNPLDSHOVE(x) \
    (((x) >> S_FW_TLSTX_DATA_WR_ALIGNPLDSHOVE) & \
     M_FW_TLSTX_DATA_WR_ALIGNPLDSHOVE)
#define F_FW_TLSTX_DATA_WR_ALIGNPLDSHOVE V_FW_TLSTX_DATA_WR_ALIGNPLDSHOVE(1U)

#define S_FW_TLSTX_DATA_WR_FLAGS        0
#define M_FW_TLSTX_DATA_WR_FLAGS        0xfffffff
#define V_FW_TLSTX_DATA_WR_FLAGS(x)     ((x) << S_FW_TLSTX_DATA_WR_FLAGS)
#define G_FW_TLSTX_DATA_WR_FLAGS(x)     \
    (((x) >> S_FW_TLSTX_DATA_WR_FLAGS) & M_FW_TLSTX_DATA_WR_FLAGS)

#define S_FW_TLSTX_DATA_WR_CTXLOC       30
#define M_FW_TLSTX_DATA_WR_CTXLOC       0x3
#define V_FW_TLSTX_DATA_WR_CTXLOC(x)    ((x) << S_FW_TLSTX_DATA_WR_CTXLOC)
#define G_FW_TLSTX_DATA_WR_CTXLOC(x)    \
    (((x) >> S_FW_TLSTX_DATA_WR_CTXLOC) & M_FW_TLSTX_DATA_WR_CTXLOC)

#define S_FW_TLSTX_DATA_WR_IVDSGL       29
#define M_FW_TLSTX_DATA_WR_IVDSGL       0x1
#define V_FW_TLSTX_DATA_WR_IVDSGL(x)    ((x) << S_FW_TLSTX_DATA_WR_IVDSGL)
#define G_FW_TLSTX_DATA_WR_IVDSGL(x)    \
    (((x) >> S_FW_TLSTX_DATA_WR_IVDSGL) & M_FW_TLSTX_DATA_WR_IVDSGL)
#define F_FW_TLSTX_DATA_WR_IVDSGL       V_FW_TLSTX_DATA_WR_IVDSGL(1U)

#define S_FW_TLSTX_DATA_WR_KEYSIZE      24
#define M_FW_TLSTX_DATA_WR_KEYSIZE      0x1f
#define V_FW_TLSTX_DATA_WR_KEYSIZE(x)   ((x) << S_FW_TLSTX_DATA_WR_KEYSIZE)
#define G_FW_TLSTX_DATA_WR_KEYSIZE(x)   \
    (((x) >> S_FW_TLSTX_DATA_WR_KEYSIZE) & M_FW_TLSTX_DATA_WR_KEYSIZE)

#define S_FW_TLSTX_DATA_WR_NUMIVS       14
#define M_FW_TLSTX_DATA_WR_NUMIVS       0xff
#define V_FW_TLSTX_DATA_WR_NUMIVS(x)    ((x) << S_FW_TLSTX_DATA_WR_NUMIVS)
#define G_FW_TLSTX_DATA_WR_NUMIVS(x)    \
    (((x) >> S_FW_TLSTX_DATA_WR_NUMIVS) & M_FW_TLSTX_DATA_WR_NUMIVS)

#define S_FW_TLSTX_DATA_WR_EXP          0
#define M_FW_TLSTX_DATA_WR_EXP          0x3fff
#define V_FW_TLSTX_DATA_WR_EXP(x)       ((x) << S_FW_TLSTX_DATA_WR_EXP)
#define G_FW_TLSTX_DATA_WR_EXP(x)       \
    (((x) >> S_FW_TLSTX_DATA_WR_EXP) & M_FW_TLSTX_DATA_WR_EXP)

#define S_FW_TLSTX_DATA_WR_ADJUSTEDPLEN 1
#define M_FW_TLSTX_DATA_WR_ADJUSTEDPLEN 0x7fff
#define V_FW_TLSTX_DATA_WR_ADJUSTEDPLEN(x) \
    ((x) << S_FW_TLSTX_DATA_WR_ADJUSTEDPLEN)
#define G_FW_TLSTX_DATA_WR_ADJUSTEDPLEN(x) \
    (((x) >> S_FW_TLSTX_DATA_WR_ADJUSTEDPLEN) & \
     M_FW_TLSTX_DATA_WR_ADJUSTEDPLEN)

#define S_FW_TLSTX_DATA_WR_EXPINPLENMAX 4
#define M_FW_TLSTX_DATA_WR_EXPINPLENMAX 0xfff
#define V_FW_TLSTX_DATA_WR_EXPINPLENMAX(x) \
    ((x) << S_FW_TLSTX_DATA_WR_EXPINPLENMAX)
#define G_FW_TLSTX_DATA_WR_EXPINPLENMAX(x) \
    (((x) >> S_FW_TLSTX_DATA_WR_EXPINPLENMAX) & \
     M_FW_TLSTX_DATA_WR_EXPINPLENMAX)

#define S_FW_TLSTX_DATA_WR_PDUSINPLENMAX 2
#define M_FW_TLSTX_DATA_WR_PDUSINPLENMAX 0x3f
#define V_FW_TLSTX_DATA_WR_PDUSINPLENMAX(x) \
    ((x) << S_FW_TLSTX_DATA_WR_PDUSINPLENMAX)
#define G_FW_TLSTX_DATA_WR_PDUSINPLENMAX(x) \
    (((x) >> S_FW_TLSTX_DATA_WR_PDUSINPLENMAX) & \
     M_FW_TLSTX_DATA_WR_PDUSINPLENMAX)

struct fw_crypto_lookaside_wr {
        __be32 op_to_cctx_size;
        __be32 len16_pkd;
        __be32 session_id;
        __be32 rx_chid_to_rx_q_id;
        __be32 key_addr;
        __be32 pld_size_hash_size;
        __be64 cookie;
};

#define S_FW_CRYPTO_LOOKASIDE_WR_OPCODE 24
#define M_FW_CRYPTO_LOOKASIDE_WR_OPCODE 0xff
#define V_FW_CRYPTO_LOOKASIDE_WR_OPCODE(x) \
    ((x) << S_FW_CRYPTO_LOOKASIDE_WR_OPCODE)
#define G_FW_CRYPTO_LOOKASIDE_WR_OPCODE(x) \
    (((x) >> S_FW_CRYPTO_LOOKASIDE_WR_OPCODE) & \
     M_FW_CRYPTO_LOOKASIDE_WR_OPCODE)

#define S_FW_CRYPTO_LOOKASIDE_WR_COMPL 23
#define M_FW_CRYPTO_LOOKASIDE_WR_COMPL 0x1
#define V_FW_CRYPTO_LOOKASIDE_WR_COMPL(x) \
    ((x) << S_FW_CRYPTO_LOOKASIDE_WR_COMPL)
#define G_FW_CRYPTO_LOOKASIDE_WR_COMPL(x) \
    (((x) >> S_FW_CRYPTO_LOOKASIDE_WR_COMPL) & \
     M_FW_CRYPTO_LOOKASIDE_WR_COMPL)
#define F_FW_CRYPTO_LOOKASIDE_WR_COMPL V_FW_CRYPTO_LOOKASIDE_WR_COMPL(1U)

#define S_FW_CRYPTO_LOOKASIDE_WR_IMM_LEN 15
#define M_FW_CRYPTO_LOOKASIDE_WR_IMM_LEN 0xff
#define V_FW_CRYPTO_LOOKASIDE_WR_IMM_LEN(x) \
    ((x) << S_FW_CRYPTO_LOOKASIDE_WR_IMM_LEN)
#define G_FW_CRYPTO_LOOKASIDE_WR_IMM_LEN(x) \
    (((x) >> S_FW_CRYPTO_LOOKASIDE_WR_IMM_LEN) & \
     M_FW_CRYPTO_LOOKASIDE_WR_IMM_LEN)

#define S_FW_CRYPTO_LOOKASIDE_WR_CCTX_LOC 5
#define M_FW_CRYPTO_LOOKASIDE_WR_CCTX_LOC 0x3
#define V_FW_CRYPTO_LOOKASIDE_WR_CCTX_LOC(x) \
    ((x) << S_FW_CRYPTO_LOOKASIDE_WR_CCTX_LOC)
#define G_FW_CRYPTO_LOOKASIDE_WR_CCTX_LOC(x) \
    (((x) >> S_FW_CRYPTO_LOOKASIDE_WR_CCTX_LOC) & \
     M_FW_CRYPTO_LOOKASIDE_WR_CCTX_LOC)

#define S_FW_CRYPTO_LOOKASIDE_WR_CCTX_SIZE 0
#define M_FW_CRYPTO_LOOKASIDE_WR_CCTX_SIZE 0x1f
#define V_FW_CRYPTO_LOOKASIDE_WR_CCTX_SIZE(x) \
    ((x) << S_FW_CRYPTO_LOOKASIDE_WR_CCTX_SIZE)
#define G_FW_CRYPTO_LOOKASIDE_WR_CCTX_SIZE(x) \
    (((x) >> S_FW_CRYPTO_LOOKASIDE_WR_CCTX_SIZE) & \
     M_FW_CRYPTO_LOOKASIDE_WR_CCTX_SIZE)

#define S_FW_CRYPTO_LOOKASIDE_WR_LEN16 0
#define M_FW_CRYPTO_LOOKASIDE_WR_LEN16 0xff
#define V_FW_CRYPTO_LOOKASIDE_WR_LEN16(x) \
    ((x) << S_FW_CRYPTO_LOOKASIDE_WR_LEN16)
#define G_FW_CRYPTO_LOOKASIDE_WR_LEN16(x) \
    (((x) >> S_FW_CRYPTO_LOOKASIDE_WR_LEN16) & \
     M_FW_CRYPTO_LOOKASIDE_WR_LEN16)

#define S_FW_CRYPTO_LOOKASIDE_WR_RX_CHID 29
#define M_FW_CRYPTO_LOOKASIDE_WR_RX_CHID 0x3
#define V_FW_CRYPTO_LOOKASIDE_WR_RX_CHID(x) \
    ((x) << S_FW_CRYPTO_LOOKASIDE_WR_RX_CHID)
#define G_FW_CRYPTO_LOOKASIDE_WR_RX_CHID(x) \
    (((x) >> S_FW_CRYPTO_LOOKASIDE_WR_RX_CHID) & \
     M_FW_CRYPTO_LOOKASIDE_WR_RX_CHID)

#define S_FW_CRYPTO_LOOKASIDE_WR_LCB  27
#define M_FW_CRYPTO_LOOKASIDE_WR_LCB  0x3
#define V_FW_CRYPTO_LOOKASIDE_WR_LCB(x) \
    ((x) << S_FW_CRYPTO_LOOKASIDE_WR_LCB)
#define G_FW_CRYPTO_LOOKASIDE_WR_LCB(x) \
    (((x) >> S_FW_CRYPTO_LOOKASIDE_WR_LCB) & M_FW_CRYPTO_LOOKASIDE_WR_LCB)

#define S_FW_CRYPTO_LOOKASIDE_WR_PHASH 25
#define M_FW_CRYPTO_LOOKASIDE_WR_PHASH 0x3
#define V_FW_CRYPTO_LOOKASIDE_WR_PHASH(x) \
    ((x) << S_FW_CRYPTO_LOOKASIDE_WR_PHASH)
#define G_FW_CRYPTO_LOOKASIDE_WR_PHASH(x) \
    (((x) >> S_FW_CRYPTO_LOOKASIDE_WR_PHASH) & \
     M_FW_CRYPTO_LOOKASIDE_WR_PHASH)

#define S_FW_CRYPTO_LOOKASIDE_WR_IV   23
#define M_FW_CRYPTO_LOOKASIDE_WR_IV   0x3
#define V_FW_CRYPTO_LOOKASIDE_WR_IV(x) \
    ((x) << S_FW_CRYPTO_LOOKASIDE_WR_IV)
#define G_FW_CRYPTO_LOOKASIDE_WR_IV(x) \
    (((x) >> S_FW_CRYPTO_LOOKASIDE_WR_IV) & M_FW_CRYPTO_LOOKASIDE_WR_IV)

#define S_FW_CRYPTO_LOOKASIDE_WR_FQIDX  15
#define M_FW_CRYPTO_LOOKASIDE_WR_FQIDX  0xff
#define V_FW_CRYPTO_LOOKASIDE_WR_FQIDX(x) \
	((x) << S_FW_CRYPTO_LOOKASIDE_WR_FQIDX)
#define G_FW_CRYPTO_LOOKASIDE_WR_FQIDX(x) \
	(((x) >> S_FW_CRYPTO_LOOKASIDE_WR_FQIDX) &\
	  M_FW_CRYPTO_LOOKASIDE_WR_FQIDX)

#define S_FW_CRYPTO_LOOKASIDE_WR_TX_CH 10
#define M_FW_CRYPTO_LOOKASIDE_WR_TX_CH 0x3
#define V_FW_CRYPTO_LOOKASIDE_WR_TX_CH(x) \
    ((x) << S_FW_CRYPTO_LOOKASIDE_WR_TX_CH)
#define G_FW_CRYPTO_LOOKASIDE_WR_TX_CH(x) \
    (((x) >> S_FW_CRYPTO_LOOKASIDE_WR_TX_CH) & \
     M_FW_CRYPTO_LOOKASIDE_WR_TX_CH)

#define S_FW_CRYPTO_LOOKASIDE_WR_RX_Q_ID 0
#define M_FW_CRYPTO_LOOKASIDE_WR_RX_Q_ID 0x3ff
#define V_FW_CRYPTO_LOOKASIDE_WR_RX_Q_ID(x) \
    ((x) << S_FW_CRYPTO_LOOKASIDE_WR_RX_Q_ID)
#define G_FW_CRYPTO_LOOKASIDE_WR_RX_Q_ID(x) \
    (((x) >> S_FW_CRYPTO_LOOKASIDE_WR_RX_Q_ID) & \
     M_FW_CRYPTO_LOOKASIDE_WR_RX_Q_ID)

#define S_FW_CRYPTO_LOOKASIDE_WR_PLD_SIZE 24
#define M_FW_CRYPTO_LOOKASIDE_WR_PLD_SIZE 0xff
#define V_FW_CRYPTO_LOOKASIDE_WR_PLD_SIZE(x) \
    ((x) << S_FW_CRYPTO_LOOKASIDE_WR_PLD_SIZE)
#define G_FW_CRYPTO_LOOKASIDE_WR_PLD_SIZE(x) \
    (((x) >> S_FW_CRYPTO_LOOKASIDE_WR_PLD_SIZE) & \
     M_FW_CRYPTO_LOOKASIDE_WR_PLD_SIZE)

#define S_FW_CRYPTO_LOOKASIDE_WR_HASH_SIZE 17
#define M_FW_CRYPTO_LOOKASIDE_WR_HASH_SIZE 0x7f
#define V_FW_CRYPTO_LOOKASIDE_WR_HASH_SIZE(x) \
    ((x) << S_FW_CRYPTO_LOOKASIDE_WR_HASH_SIZE)
#define G_FW_CRYPTO_LOOKASIDE_WR_HASH_SIZE(x) \
    (((x) >> S_FW_CRYPTO_LOOKASIDE_WR_HASH_SIZE) & \
     M_FW_CRYPTO_LOOKASIDE_WR_HASH_SIZE)

/******************************************************************************
 *  C O M M A N D s
 *********************/

/*
 * The maximum length of time, in miliseconds, that we expect any firmware
 * command to take to execute and return a reply to the host.  The RESET
 * and INITIALIZE commands can take a fair amount of time to execute but
 * most execute in far less time than this maximum.  This constant is used
 * by host software to determine how long to wait for a firmware command
 * reply before declaring the firmware as dead/unreachable ...
 */
#define FW_CMD_MAX_TIMEOUT	10000

/*
 * If a host driver does a HELLO and discovers that there's already a MASTER
 * selected, we may have to wait for that MASTER to finish issuing RESET,
 * configuration and INITIALIZE commands.  Also, there's a possibility that
 * our own HELLO may get lost if it happens right as the MASTER is issuign a
 * RESET command, so we need to be willing to make a few retries of our HELLO.
 */
#define FW_CMD_HELLO_TIMEOUT	(3 * FW_CMD_MAX_TIMEOUT)
#define FW_CMD_HELLO_RETRIES	3

enum fw_cmd_opcodes {
	FW_LDST_CMD                    = 0x01,
	FW_RESET_CMD                   = 0x03,
	FW_HELLO_CMD                   = 0x04,
	FW_BYE_CMD                     = 0x05,
	FW_INITIALIZE_CMD              = 0x06,
	FW_CAPS_CONFIG_CMD             = 0x07,
	FW_PARAMS_CMD                  = 0x08,
	FW_PFVF_CMD                    = 0x09,
	FW_IQ_CMD                      = 0x10,
	FW_EQ_MNGT_CMD                 = 0x11,
	FW_EQ_ETH_CMD                  = 0x12,
	FW_EQ_CTRL_CMD                 = 0x13,
	FW_EQ_OFLD_CMD                 = 0x21,
	FW_VI_CMD                      = 0x14,
	FW_VI_MAC_CMD                  = 0x15,
	FW_VI_RXMODE_CMD               = 0x16,
	FW_VI_ENABLE_CMD               = 0x17,
	FW_VI_STATS_CMD                = 0x1a,
	FW_ACL_MAC_CMD                 = 0x18,
	FW_ACL_VLAN_CMD                = 0x19,
	FW_PORT_CMD                    = 0x1b,
	FW_PORT_STATS_CMD              = 0x1c,
	FW_PORT_LB_STATS_CMD           = 0x1d,
	FW_PORT_TRACE_CMD              = 0x1e,
	FW_PORT_TRACE_MMAP_CMD         = 0x1f,
	FW_RSS_IND_TBL_CMD             = 0x20,
	FW_RSS_GLB_CONFIG_CMD          = 0x22,
	FW_RSS_VI_CONFIG_CMD           = 0x23,
	FW_SCHED_CMD                   = 0x24,
	FW_DEVLOG_CMD                  = 0x25,
	FW_WATCHDOG_CMD                = 0x27,
	FW_CLIP_CMD                    = 0x28,
	FW_CHNET_IFACE_CMD             = 0x26,
	FW_FCOE_RES_INFO_CMD           = 0x31,
	FW_FCOE_LINK_CMD               = 0x32,
	FW_FCOE_VNP_CMD                = 0x33,
	FW_FCOE_SPARAMS_CMD            = 0x35,
	FW_FCOE_STATS_CMD              = 0x37,
	FW_FCOE_FCF_CMD                = 0x38,
	FW_DCB_IEEE_CMD		       = 0x3a,
	FW_DIAG_CMD		       = 0x3d,
	FW_PTP_CMD                     = 0x3e,
	FW_HMA_CMD                     = 0x3f,
	FW_LASTC2E_CMD                 = 0x40,
	FW_ERROR_CMD                   = 0x80,
	FW_DEBUG_CMD                   = 0x81,
};

enum fw_cmd_cap {
	FW_CMD_CAP_PF                  = 0x01,
	FW_CMD_CAP_DMAQ                = 0x02,
	FW_CMD_CAP_PORT                = 0x04,
	FW_CMD_CAP_PORTPROMISC         = 0x08,
	FW_CMD_CAP_PORTSTATS           = 0x10,
	FW_CMD_CAP_VF                  = 0x80,
};

/*
 * Generic command header flit0
 */
struct fw_cmd_hdr {
	__be32 hi;
	__be32 lo;
};

#define S_FW_CMD_OP		24
#define M_FW_CMD_OP		0xff
#define V_FW_CMD_OP(x)		((x) << S_FW_CMD_OP)
#define G_FW_CMD_OP(x)		(((x) >> S_FW_CMD_OP) & M_FW_CMD_OP)

#define S_FW_CMD_REQUEST	23
#define M_FW_CMD_REQUEST	0x1
#define V_FW_CMD_REQUEST(x)	((x) << S_FW_CMD_REQUEST)
#define G_FW_CMD_REQUEST(x)	(((x) >> S_FW_CMD_REQUEST) & M_FW_CMD_REQUEST)
#define F_FW_CMD_REQUEST	V_FW_CMD_REQUEST(1U)

#define S_FW_CMD_READ		22
#define M_FW_CMD_READ		0x1
#define V_FW_CMD_READ(x)	((x) << S_FW_CMD_READ)
#define G_FW_CMD_READ(x)	(((x) >> S_FW_CMD_READ) & M_FW_CMD_READ)
#define F_FW_CMD_READ		V_FW_CMD_READ(1U)

#define S_FW_CMD_WRITE		21
#define M_FW_CMD_WRITE		0x1
#define V_FW_CMD_WRITE(x)	((x) << S_FW_CMD_WRITE)
#define G_FW_CMD_WRITE(x)	(((x) >> S_FW_CMD_WRITE) & M_FW_CMD_WRITE)
#define F_FW_CMD_WRITE		V_FW_CMD_WRITE(1U)

#define S_FW_CMD_EXEC		20
#define M_FW_CMD_EXEC		0x1
#define V_FW_CMD_EXEC(x)	((x) << S_FW_CMD_EXEC)
#define G_FW_CMD_EXEC(x)	(((x) >> S_FW_CMD_EXEC) & M_FW_CMD_EXEC)
#define F_FW_CMD_EXEC		V_FW_CMD_EXEC(1U)

#define S_FW_CMD_RAMASK		20
#define M_FW_CMD_RAMASK		0xf
#define V_FW_CMD_RAMASK(x)	((x) << S_FW_CMD_RAMASK)
#define G_FW_CMD_RAMASK(x)	(((x) >> S_FW_CMD_RAMASK) & M_FW_CMD_RAMASK)

#define S_FW_CMD_RETVAL		8
#define M_FW_CMD_RETVAL		0xff
#define V_FW_CMD_RETVAL(x)	((x) << S_FW_CMD_RETVAL)
#define G_FW_CMD_RETVAL(x)	(((x) >> S_FW_CMD_RETVAL) & M_FW_CMD_RETVAL)

#define S_FW_CMD_LEN16		0
#define M_FW_CMD_LEN16		0xff
#define V_FW_CMD_LEN16(x)	((x) << S_FW_CMD_LEN16)
#define G_FW_CMD_LEN16(x)	(((x) >> S_FW_CMD_LEN16) & M_FW_CMD_LEN16)

#define FW_LEN16(fw_struct) V_FW_CMD_LEN16(sizeof(fw_struct) / 16)

/*
 *	address spaces
 */
enum fw_ldst_addrspc {
	FW_LDST_ADDRSPC_FIRMWARE  = 0x0001,
	FW_LDST_ADDRSPC_SGE_EGRC  = 0x0008,
	FW_LDST_ADDRSPC_SGE_INGC  = 0x0009,
	FW_LDST_ADDRSPC_SGE_FLMC  = 0x000a,
	FW_LDST_ADDRSPC_SGE_CONMC = 0x000b,
	FW_LDST_ADDRSPC_TP_PIO    = 0x0010,
	FW_LDST_ADDRSPC_TP_TM_PIO = 0x0011,
	FW_LDST_ADDRSPC_TP_MIB    = 0x0012,
	FW_LDST_ADDRSPC_MDIO      = 0x0018,
	FW_LDST_ADDRSPC_MPS       = 0x0020,
	FW_LDST_ADDRSPC_FUNC      = 0x0028,
	FW_LDST_ADDRSPC_FUNC_PCIE = 0x0029,
	FW_LDST_ADDRSPC_FUNC_I2C  = 0x002A, /* legacy */
	FW_LDST_ADDRSPC_LE	  = 0x0030,
	FW_LDST_ADDRSPC_I2C       = 0x0038,
	FW_LDST_ADDRSPC_PCIE_CFGS = 0x0040,
	FW_LDST_ADDRSPC_PCIE_DBG  = 0x0041,
	FW_LDST_ADDRSPC_PCIE_PHY  = 0x0042,
	FW_LDST_ADDRSPC_CIM_Q	  = 0x0048,
};

/*
 *	MDIO VSC8634 register access control field
 */
enum fw_ldst_mdio_vsc8634_aid {
	FW_LDST_MDIO_VS_STANDARD,
	FW_LDST_MDIO_VS_EXTENDED,
	FW_LDST_MDIO_VS_GPIO
};

enum fw_ldst_mps_fid {
	FW_LDST_MPS_ATRB,
	FW_LDST_MPS_RPLC
};

enum fw_ldst_func_access_ctl {
	FW_LDST_FUNC_ACC_CTL_VIID,
	FW_LDST_FUNC_ACC_CTL_FID
};

enum fw_ldst_func_mod_index {
	FW_LDST_FUNC_MPS
};

struct fw_ldst_cmd {
	__be32 op_to_addrspace;
	__be32 cycles_to_len16;
	union fw_ldst {
		struct fw_ldst_addrval {
			__be32 addr;
			__be32 val;
		} addrval;
		struct fw_ldst_idctxt {
			__be32 physid;
			__be32 msg_ctxtflush;
			__be32 ctxt_data7;
			__be32 ctxt_data6;
			__be32 ctxt_data5;
			__be32 ctxt_data4;
			__be32 ctxt_data3;
			__be32 ctxt_data2;
			__be32 ctxt_data1;
			__be32 ctxt_data0;
		} idctxt;
		struct fw_ldst_mdio {
			__be16 paddr_mmd;
			__be16 raddr;
			__be16 vctl;
			__be16 rval;
		} mdio;
		struct fw_ldst_cim_rq {
			__u8   req_first64[8];
			__u8   req_second64[8];
			__u8   resp_first64[8];
			__u8   resp_second64[8];
			__be32 r3[2];
		} cim_rq;
		union fw_ldst_mps {
			struct fw_ldst_mps_rplc {
				__be16 fid_idx;
				__be16 rplcpf_pkd;
				__be32 rplc255_224;
				__be32 rplc223_192;
				__be32 rplc191_160;
				__be32 rplc159_128;
				__be32 rplc127_96;
				__be32 rplc95_64;
				__be32 rplc63_32;
				__be32 rplc31_0;
			} rplc;
			struct fw_ldst_mps_atrb {
				__be16 fid_mpsid;
				__be16 r2[3];
				__be32 r3[2];
				__be32 r4;
				__be32 atrb;
				__be16 vlan[16];
			} atrb;
		} mps;
		struct fw_ldst_func {
			__u8   access_ctl;
			__u8   mod_index;
			__be16 ctl_id;
			__be32 offset;
			__be64 data0;
			__be64 data1;
		} func;
		struct fw_ldst_pcie {
			__u8   ctrl_to_fn;
			__u8   bnum;
			__u8   r;
			__u8   ext_r;
			__u8   select_naccess;
			__u8   pcie_fn;
			__be16 nset_pkd;
			__be32 data[12];
		} pcie;
		struct fw_ldst_i2c_deprecated {
			__u8   pid_pkd;
			__u8   base;
			__u8   boffset;
			__u8   data;
			__be32 r9;
		} i2c_deprecated;
		struct fw_ldst_i2c {
			__u8   pid;
			__u8   did;
			__u8   boffset;
			__u8   blen;
			__be32 r9;
			__u8   data[48];
		} i2c;
		struct fw_ldst_le {
			__be32 index;
			__be32 r9;
			__u8   val[33];
			__u8   r11[7];
		} le;
	} u;
};

#define S_FW_LDST_CMD_ADDRSPACE		0
#define M_FW_LDST_CMD_ADDRSPACE		0xff
#define V_FW_LDST_CMD_ADDRSPACE(x)	((x) << S_FW_LDST_CMD_ADDRSPACE)
#define G_FW_LDST_CMD_ADDRSPACE(x)	\
    (((x) >> S_FW_LDST_CMD_ADDRSPACE) & M_FW_LDST_CMD_ADDRSPACE)

#define S_FW_LDST_CMD_CYCLES		16
#define M_FW_LDST_CMD_CYCLES		0xffff
#define V_FW_LDST_CMD_CYCLES(x)		((x) << S_FW_LDST_CMD_CYCLES)
#define G_FW_LDST_CMD_CYCLES(x)		\
    (((x) >> S_FW_LDST_CMD_CYCLES) & M_FW_LDST_CMD_CYCLES)

#define S_FW_LDST_CMD_MSG		31
#define M_FW_LDST_CMD_MSG		0x1
#define V_FW_LDST_CMD_MSG(x)		((x) << S_FW_LDST_CMD_MSG)
#define G_FW_LDST_CMD_MSG(x)		\
    (((x) >> S_FW_LDST_CMD_MSG) & M_FW_LDST_CMD_MSG)
#define F_FW_LDST_CMD_MSG		V_FW_LDST_CMD_MSG(1U)

#define S_FW_LDST_CMD_CTXTFLUSH		30
#define M_FW_LDST_CMD_CTXTFLUSH		0x1
#define V_FW_LDST_CMD_CTXTFLUSH(x)	((x) << S_FW_LDST_CMD_CTXTFLUSH)
#define G_FW_LDST_CMD_CTXTFLUSH(x)	\
    (((x) >> S_FW_LDST_CMD_CTXTFLUSH) & M_FW_LDST_CMD_CTXTFLUSH)
#define F_FW_LDST_CMD_CTXTFLUSH		V_FW_LDST_CMD_CTXTFLUSH(1U)

#define S_FW_LDST_CMD_PADDR		8
#define M_FW_LDST_CMD_PADDR		0x1f
#define V_FW_LDST_CMD_PADDR(x)		((x) << S_FW_LDST_CMD_PADDR)
#define G_FW_LDST_CMD_PADDR(x)		\
    (((x) >> S_FW_LDST_CMD_PADDR) & M_FW_LDST_CMD_PADDR)

#define S_FW_LDST_CMD_MMD		0
#define M_FW_LDST_CMD_MMD		0x1f
#define V_FW_LDST_CMD_MMD(x)		((x) << S_FW_LDST_CMD_MMD)
#define G_FW_LDST_CMD_MMD(x)		\
    (((x) >> S_FW_LDST_CMD_MMD) & M_FW_LDST_CMD_MMD)

#define S_FW_LDST_CMD_FID		15
#define M_FW_LDST_CMD_FID		0x1
#define V_FW_LDST_CMD_FID(x)		((x) << S_FW_LDST_CMD_FID)
#define G_FW_LDST_CMD_FID(x)		\
    (((x) >> S_FW_LDST_CMD_FID) & M_FW_LDST_CMD_FID)
#define F_FW_LDST_CMD_FID		V_FW_LDST_CMD_FID(1U)

#define S_FW_LDST_CMD_IDX		0
#define M_FW_LDST_CMD_IDX		0x7fff
#define V_FW_LDST_CMD_IDX(x)		((x) << S_FW_LDST_CMD_IDX)
#define G_FW_LDST_CMD_IDX(x)		\
    (((x) >> S_FW_LDST_CMD_IDX) & M_FW_LDST_CMD_IDX)

#define S_FW_LDST_CMD_RPLCPF		0
#define M_FW_LDST_CMD_RPLCPF		0xff
#define V_FW_LDST_CMD_RPLCPF(x)		((x) << S_FW_LDST_CMD_RPLCPF)
#define G_FW_LDST_CMD_RPLCPF(x)		\
    (((x) >> S_FW_LDST_CMD_RPLCPF) & M_FW_LDST_CMD_RPLCPF)

#define S_FW_LDST_CMD_MPSID		0
#define M_FW_LDST_CMD_MPSID		0x7fff
#define V_FW_LDST_CMD_MPSID(x)		((x) << S_FW_LDST_CMD_MPSID)
#define G_FW_LDST_CMD_MPSID(x)		\
    (((x) >> S_FW_LDST_CMD_MPSID) & M_FW_LDST_CMD_MPSID)

#define S_FW_LDST_CMD_CTRL		7
#define M_FW_LDST_CMD_CTRL		0x1
#define V_FW_LDST_CMD_CTRL(x)		((x) << S_FW_LDST_CMD_CTRL)
#define G_FW_LDST_CMD_CTRL(x)		\
    (((x) >> S_FW_LDST_CMD_CTRL) & M_FW_LDST_CMD_CTRL)
#define F_FW_LDST_CMD_CTRL		V_FW_LDST_CMD_CTRL(1U)

#define S_FW_LDST_CMD_LC		4
#define M_FW_LDST_CMD_LC		0x1
#define V_FW_LDST_CMD_LC(x)		((x) << S_FW_LDST_CMD_LC)
#define G_FW_LDST_CMD_LC(x)		\
    (((x) >> S_FW_LDST_CMD_LC) & M_FW_LDST_CMD_LC)
#define F_FW_LDST_CMD_LC		V_FW_LDST_CMD_LC(1U)

#define S_FW_LDST_CMD_AI		3
#define M_FW_LDST_CMD_AI		0x1
#define V_FW_LDST_CMD_AI(x)		((x) << S_FW_LDST_CMD_AI)
#define G_FW_LDST_CMD_AI(x)		\
    (((x) >> S_FW_LDST_CMD_AI) & M_FW_LDST_CMD_AI)
#define F_FW_LDST_CMD_AI		V_FW_LDST_CMD_AI(1U)

#define S_FW_LDST_CMD_FN		0
#define M_FW_LDST_CMD_FN		0x7
#define V_FW_LDST_CMD_FN(x)		((x) << S_FW_LDST_CMD_FN)
#define G_FW_LDST_CMD_FN(x)		\
    (((x) >> S_FW_LDST_CMD_FN) & M_FW_LDST_CMD_FN)

#define S_FW_LDST_CMD_SELECT		4
#define M_FW_LDST_CMD_SELECT		0xf
#define V_FW_LDST_CMD_SELECT(x)		((x) << S_FW_LDST_CMD_SELECT)
#define G_FW_LDST_CMD_SELECT(x)		\
    (((x) >> S_FW_LDST_CMD_SELECT) & M_FW_LDST_CMD_SELECT)

#define S_FW_LDST_CMD_NACCESS		0
#define M_FW_LDST_CMD_NACCESS		0xf
#define V_FW_LDST_CMD_NACCESS(x)	((x) << S_FW_LDST_CMD_NACCESS)
#define G_FW_LDST_CMD_NACCESS(x)	\
    (((x) >> S_FW_LDST_CMD_NACCESS) & M_FW_LDST_CMD_NACCESS)

#define S_FW_LDST_CMD_NSET		14
#define M_FW_LDST_CMD_NSET		0x3
#define V_FW_LDST_CMD_NSET(x)		((x) << S_FW_LDST_CMD_NSET)
#define G_FW_LDST_CMD_NSET(x)		\
    (((x) >> S_FW_LDST_CMD_NSET) & M_FW_LDST_CMD_NSET)

#define S_FW_LDST_CMD_PID		6
#define M_FW_LDST_CMD_PID		0x3
#define V_FW_LDST_CMD_PID(x)		((x) << S_FW_LDST_CMD_PID)
#define G_FW_LDST_CMD_PID(x)		\
    (((x) >> S_FW_LDST_CMD_PID) & M_FW_LDST_CMD_PID)

struct fw_reset_cmd {
	__be32 op_to_write;
	__be32 retval_len16;
	__be32 val;
	__be32 halt_pkd;
};

#define S_FW_RESET_CMD_HALT		31
#define M_FW_RESET_CMD_HALT		0x1
#define V_FW_RESET_CMD_HALT(x)		((x) << S_FW_RESET_CMD_HALT)
#define G_FW_RESET_CMD_HALT(x)		\
    (((x) >> S_FW_RESET_CMD_HALT) & M_FW_RESET_CMD_HALT)
#define F_FW_RESET_CMD_HALT		V_FW_RESET_CMD_HALT(1U)

enum {
	FW_HELLO_CMD_STAGE_OS		= 0,
	FW_HELLO_CMD_STAGE_PREOS0	= 1,
	FW_HELLO_CMD_STAGE_PREOS1	= 2,
	FW_HELLO_CMD_STAGE_POSTOS	= 3,
};

struct fw_hello_cmd {
	__be32 op_to_write;
	__be32 retval_len16;
	__be32 err_to_clearinit;
	__be32 fwrev;
};

#define S_FW_HELLO_CMD_ERR		31
#define M_FW_HELLO_CMD_ERR		0x1
#define V_FW_HELLO_CMD_ERR(x)		((x) << S_FW_HELLO_CMD_ERR)
#define G_FW_HELLO_CMD_ERR(x)		\
    (((x) >> S_FW_HELLO_CMD_ERR) & M_FW_HELLO_CMD_ERR)
#define F_FW_HELLO_CMD_ERR		V_FW_HELLO_CMD_ERR(1U)

#define S_FW_HELLO_CMD_INIT		30
#define M_FW_HELLO_CMD_INIT		0x1
#define V_FW_HELLO_CMD_INIT(x)		((x) << S_FW_HELLO_CMD_INIT)
#define G_FW_HELLO_CMD_INIT(x)		\
    (((x) >> S_FW_HELLO_CMD_INIT) & M_FW_HELLO_CMD_INIT)
#define F_FW_HELLO_CMD_INIT		V_FW_HELLO_CMD_INIT(1U)

#define S_FW_HELLO_CMD_MASTERDIS	29
#define M_FW_HELLO_CMD_MASTERDIS	0x1
#define V_FW_HELLO_CMD_MASTERDIS(x)	((x) << S_FW_HELLO_CMD_MASTERDIS)
#define G_FW_HELLO_CMD_MASTERDIS(x)	\
    (((x) >> S_FW_HELLO_CMD_MASTERDIS) & M_FW_HELLO_CMD_MASTERDIS)
#define F_FW_HELLO_CMD_MASTERDIS	V_FW_HELLO_CMD_MASTERDIS(1U)

#define S_FW_HELLO_CMD_MASTERFORCE	28
#define M_FW_HELLO_CMD_MASTERFORCE	0x1
#define V_FW_HELLO_CMD_MASTERFORCE(x)	((x) << S_FW_HELLO_CMD_MASTERFORCE)
#define G_FW_HELLO_CMD_MASTERFORCE(x)	\
    (((x) >> S_FW_HELLO_CMD_MASTERFORCE) & M_FW_HELLO_CMD_MASTERFORCE)
#define F_FW_HELLO_CMD_MASTERFORCE	V_FW_HELLO_CMD_MASTERFORCE(1U)

#define S_FW_HELLO_CMD_MBMASTER		24
#define M_FW_HELLO_CMD_MBMASTER		0xf
#define V_FW_HELLO_CMD_MBMASTER(x)	((x) << S_FW_HELLO_CMD_MBMASTER)
#define G_FW_HELLO_CMD_MBMASTER(x)	\
    (((x) >> S_FW_HELLO_CMD_MBMASTER) & M_FW_HELLO_CMD_MBMASTER)

#define S_FW_HELLO_CMD_MBASYNCNOTINT	23
#define M_FW_HELLO_CMD_MBASYNCNOTINT	0x1
#define V_FW_HELLO_CMD_MBASYNCNOTINT(x)	((x) << S_FW_HELLO_CMD_MBASYNCNOTINT)
#define G_FW_HELLO_CMD_MBASYNCNOTINT(x)	\
    (((x) >> S_FW_HELLO_CMD_MBASYNCNOTINT) & M_FW_HELLO_CMD_MBASYNCNOTINT)
#define F_FW_HELLO_CMD_MBASYNCNOTINT	V_FW_HELLO_CMD_MBASYNCNOTINT(1U)

#define S_FW_HELLO_CMD_MBASYNCNOT	20
#define M_FW_HELLO_CMD_MBASYNCNOT	0x7
#define V_FW_HELLO_CMD_MBASYNCNOT(x)	((x) << S_FW_HELLO_CMD_MBASYNCNOT)
#define G_FW_HELLO_CMD_MBASYNCNOT(x)	\
    (((x) >> S_FW_HELLO_CMD_MBASYNCNOT) & M_FW_HELLO_CMD_MBASYNCNOT)

#define S_FW_HELLO_CMD_STAGE		17
#define M_FW_HELLO_CMD_STAGE		0x7
#define V_FW_HELLO_CMD_STAGE(x)		((x) << S_FW_HELLO_CMD_STAGE)
#define G_FW_HELLO_CMD_STAGE(x)		\
    (((x) >> S_FW_HELLO_CMD_STAGE) & M_FW_HELLO_CMD_STAGE)

#define S_FW_HELLO_CMD_CLEARINIT	16
#define M_FW_HELLO_CMD_CLEARINIT	0x1
#define V_FW_HELLO_CMD_CLEARINIT(x)	((x) << S_FW_HELLO_CMD_CLEARINIT)
#define G_FW_HELLO_CMD_CLEARINIT(x)	\
    (((x) >> S_FW_HELLO_CMD_CLEARINIT) & M_FW_HELLO_CMD_CLEARINIT)
#define F_FW_HELLO_CMD_CLEARINIT	V_FW_HELLO_CMD_CLEARINIT(1U)

struct fw_bye_cmd {
	__be32 op_to_write;
	__be32 retval_len16;
	__be64 r3;
};

struct fw_initialize_cmd {
	__be32 op_to_write;
	__be32 retval_len16;
	__be64 r3;
};

enum fw_caps_config_hm {
	FW_CAPS_CONFIG_HM_PCIE		= 0x00000001,
	FW_CAPS_CONFIG_HM_PL		= 0x00000002,
	FW_CAPS_CONFIG_HM_SGE		= 0x00000004,
	FW_CAPS_CONFIG_HM_CIM		= 0x00000008,
	FW_CAPS_CONFIG_HM_ULPTX		= 0x00000010,
	FW_CAPS_CONFIG_HM_TP		= 0x00000020,
	FW_CAPS_CONFIG_HM_ULPRX		= 0x00000040,
	FW_CAPS_CONFIG_HM_PMRX		= 0x00000080,
	FW_CAPS_CONFIG_HM_PMTX		= 0x00000100,
	FW_CAPS_CONFIG_HM_MC		= 0x00000200,
	FW_CAPS_CONFIG_HM_LE		= 0x00000400,
	FW_CAPS_CONFIG_HM_MPS		= 0x00000800,
	FW_CAPS_CONFIG_HM_XGMAC		= 0x00001000,
	FW_CAPS_CONFIG_HM_CPLSWITCH	= 0x00002000,
	FW_CAPS_CONFIG_HM_T4DBG		= 0x00004000,
	FW_CAPS_CONFIG_HM_MI		= 0x00008000,
	FW_CAPS_CONFIG_HM_I2CM		= 0x00010000,
	FW_CAPS_CONFIG_HM_NCSI		= 0x00020000,
	FW_CAPS_CONFIG_HM_SMB		= 0x00040000,
	FW_CAPS_CONFIG_HM_MA		= 0x00080000,
	FW_CAPS_CONFIG_HM_EDRAM		= 0x00100000,
	FW_CAPS_CONFIG_HM_PMU		= 0x00200000,
	FW_CAPS_CONFIG_HM_UART		= 0x00400000,
	FW_CAPS_CONFIG_HM_SF		= 0x00800000,
};

/*
 * The VF Register Map.
 *
 * The Scatter Gather Engine (SGE), Multiport Support module (MPS), PIO Local
 * bus module (PL) and CPU Interface Module (CIM) components are mapped via
 * the Slice to Module Map Table (see below) in the Physical Function Register
 * Map.  The Mail Box Data (MBDATA) range is mapped via the PCI-E Mailbox Base
 * and Offset registers in the PF Register Map.  The MBDATA base address is
 * quite constrained as it determines the Mailbox Data addresses for both PFs
 * and VFs, and therefore must fit in both the VF and PF Register Maps without
 * overlapping other registers.
 */
#define FW_T4VF_SGE_BASE_ADDR      0x0000
#define FW_T4VF_MPS_BASE_ADDR      0x0100
#define FW_T4VF_PL_BASE_ADDR       0x0200
#define FW_T4VF_MBDATA_BASE_ADDR   0x0240
#define FW_T6VF_MBDATA_BASE_ADDR   0x0280 /* aligned to mbox size 128B */
#define FW_T4VF_CIM_BASE_ADDR      0x0300

#define FW_T4VF_REGMAP_START       0x0000
#define FW_T4VF_REGMAP_SIZE        0x0400

enum fw_caps_config_nbm {
	FW_CAPS_CONFIG_NBM_IPMI		= 0x00000001,
	FW_CAPS_CONFIG_NBM_NCSI		= 0x00000002,
};

enum fw_caps_config_link {
	FW_CAPS_CONFIG_LINK_PPP		= 0x00000001,
	FW_CAPS_CONFIG_LINK_QFC		= 0x00000002,
	FW_CAPS_CONFIG_LINK_DCBX	= 0x00000004,
};

enum fw_caps_config_switch {
	FW_CAPS_CONFIG_SWITCH_INGRESS	= 0x00000001,
	FW_CAPS_CONFIG_SWITCH_EGRESS	= 0x00000002,
};

enum fw_caps_config_nic {
	FW_CAPS_CONFIG_NIC		= 0x00000001,
	FW_CAPS_CONFIG_NIC_VM		= 0x00000002,
	FW_CAPS_CONFIG_NIC_IDS		= 0x00000004,
	FW_CAPS_CONFIG_NIC_UM		= 0x00000008,
	FW_CAPS_CONFIG_NIC_UM_ISGL	= 0x00000010,
	FW_CAPS_CONFIG_NIC_HASHFILTER	= 0x00000020,
	FW_CAPS_CONFIG_NIC_ETHOFLD	= 0x00000040,
};

enum fw_caps_config_toe {
	FW_CAPS_CONFIG_TOE		= 0x00000001,
};

enum fw_caps_config_rdma {
	FW_CAPS_CONFIG_RDMA_RDDP	= 0x00000001,
	FW_CAPS_CONFIG_RDMA_RDMAC	= 0x00000002,
};

enum fw_caps_config_iscsi {
	FW_CAPS_CONFIG_ISCSI_INITIATOR_PDU = 0x00000001,
	FW_CAPS_CONFIG_ISCSI_TARGET_PDU = 0x00000002,
	FW_CAPS_CONFIG_ISCSI_INITIATOR_CNXOFLD = 0x00000004,
	FW_CAPS_CONFIG_ISCSI_TARGET_CNXOFLD = 0x00000008,
	FW_CAPS_CONFIG_ISCSI_INITIATOR_SSNOFLD = 0x00000010,
	FW_CAPS_CONFIG_ISCSI_TARGET_SSNOFLD = 0x00000020,
	FW_CAPS_CONFIG_ISCSI_T10DIF = 0x00000040,
	FW_CAPS_CONFIG_ISCSI_INITIATOR_CMDOFLD = 0x00000080,
	FW_CAPS_CONFIG_ISCSI_TARGET_CMDOFLD = 0x00000100,
};

enum fw_caps_config_crypto {
	FW_CAPS_CONFIG_CRYPTO_LOOKASIDE = 0x00000001,
	FW_CAPS_CONFIG_TLSKEYS = 0x00000002,
	FW_CAPS_CONFIG_IPSEC_INLINE = 0x00000004,
};

enum fw_caps_config_fcoe {
	FW_CAPS_CONFIG_FCOE_INITIATOR	= 0x00000001,
	FW_CAPS_CONFIG_FCOE_TARGET	= 0x00000002,
	FW_CAPS_CONFIG_FCOE_CTRL_OFLD   = 0x00000004,
	FW_CAPS_CONFIG_POFCOE_INITIATOR = 0x00000008,
	FW_CAPS_CONFIG_POFCOE_TARGET    = 0x00000010,
};

enum fw_memtype_cf {
	FW_MEMTYPE_CF_EDC0		= FW_MEMTYPE_EDC0,
	FW_MEMTYPE_CF_EDC1		= FW_MEMTYPE_EDC1,
	FW_MEMTYPE_CF_EXTMEM		= FW_MEMTYPE_EXTMEM,
	FW_MEMTYPE_CF_FLASH		= FW_MEMTYPE_FLASH,
	FW_MEMTYPE_CF_INTERNAL		= FW_MEMTYPE_INTERNAL,
	FW_MEMTYPE_CF_EXTMEM1		= FW_MEMTYPE_EXTMEM1,
};

struct fw_caps_config_cmd {
	__be32 op_to_write;
	__be32 cfvalid_to_len16;
	__be32 r2;
	__be32 hwmbitmap;
	__be16 nbmcaps;
	__be16 linkcaps;
	__be16 switchcaps;
	__be16 r3;
	__be16 niccaps;
	__be16 toecaps;
	__be16 rdmacaps;
	__be16 cryptocaps;
	__be16 iscsicaps;
	__be16 fcoecaps;
	__be32 cfcsum;
	__be32 finiver;
	__be32 finicsum;
};

#define S_FW_CAPS_CONFIG_CMD_CFVALID	27
#define M_FW_CAPS_CONFIG_CMD_CFVALID	0x1
#define V_FW_CAPS_CONFIG_CMD_CFVALID(x)	((x) << S_FW_CAPS_CONFIG_CMD_CFVALID)
#define G_FW_CAPS_CONFIG_CMD_CFVALID(x)	\
    (((x) >> S_FW_CAPS_CONFIG_CMD_CFVALID) & M_FW_CAPS_CONFIG_CMD_CFVALID)
#define F_FW_CAPS_CONFIG_CMD_CFVALID	V_FW_CAPS_CONFIG_CMD_CFVALID(1U)

#define S_FW_CAPS_CONFIG_CMD_MEMTYPE_CF	24
#define M_FW_CAPS_CONFIG_CMD_MEMTYPE_CF	0x7
#define V_FW_CAPS_CONFIG_CMD_MEMTYPE_CF(x) \
    ((x) << S_FW_CAPS_CONFIG_CMD_MEMTYPE_CF)
#define G_FW_CAPS_CONFIG_CMD_MEMTYPE_CF(x) \
    (((x) >> S_FW_CAPS_CONFIG_CMD_MEMTYPE_CF) & \
     M_FW_CAPS_CONFIG_CMD_MEMTYPE_CF)

#define S_FW_CAPS_CONFIG_CMD_MEMADDR64K_CF 16
#define M_FW_CAPS_CONFIG_CMD_MEMADDR64K_CF 0xff
#define V_FW_CAPS_CONFIG_CMD_MEMADDR64K_CF(x) \
    ((x) << S_FW_CAPS_CONFIG_CMD_MEMADDR64K_CF)
#define G_FW_CAPS_CONFIG_CMD_MEMADDR64K_CF(x) \
    (((x) >> S_FW_CAPS_CONFIG_CMD_MEMADDR64K_CF) & \
     M_FW_CAPS_CONFIG_CMD_MEMADDR64K_CF)

/*
 * params command mnemonics
 */
enum fw_params_mnem {
	FW_PARAMS_MNEM_DEV		= 1,	/* device params */
	FW_PARAMS_MNEM_PFVF		= 2,	/* function params */
	FW_PARAMS_MNEM_REG		= 3,	/* limited register access */
	FW_PARAMS_MNEM_DMAQ		= 4,	/* dma queue params */
	FW_PARAMS_MNEM_CHNET		= 5,	/* chnet params */
	FW_PARAMS_MNEM_LAST
};

/*
 * device parameters
 */
enum fw_params_param_dev {
	FW_PARAMS_PARAM_DEV_CCLK	= 0x00, /* chip core clock in khz */
	FW_PARAMS_PARAM_DEV_PORTVEC	= 0x01, /* the port vector */
	FW_PARAMS_PARAM_DEV_NTID	= 0x02, /* reads the number of TIDs
						 * allocated by the device's
						 * Lookup Engine
						 */
	FW_PARAMS_PARAM_DEV_FLOWC_BUFFIFO_SZ = 0x03,
	FW_PARAMS_PARAM_DEV_INTFVER_NIC	= 0x04,
	FW_PARAMS_PARAM_DEV_INTFVER_VNIC = 0x05,
	FW_PARAMS_PARAM_DEV_INTFVER_OFLD = 0x06,
	FW_PARAMS_PARAM_DEV_INTFVER_RI	= 0x07,
	FW_PARAMS_PARAM_DEV_INTFVER_ISCSIPDU = 0x08,
	FW_PARAMS_PARAM_DEV_INTFVER_ISCSI = 0x09,
	FW_PARAMS_PARAM_DEV_INTFVER_FCOE = 0x0A,
	FW_PARAMS_PARAM_DEV_FWREV	= 0x0B,
	FW_PARAMS_PARAM_DEV_TPREV	= 0x0C,
	FW_PARAMS_PARAM_DEV_CF		= 0x0D,
	FW_PARAMS_PARAM_DEV_BYPASS	= 0x0E,
	FW_PARAMS_PARAM_DEV_PHYFW	= 0x0F,
	FW_PARAMS_PARAM_DEV_LOAD	= 0x10,
	FW_PARAMS_PARAM_DEV_DIAG	= 0x11,
	FW_PARAMS_PARAM_DEV_UCLK	= 0x12, /* uP clock in khz */
	FW_PARAMS_PARAM_DEV_MAXORDIRD_QP = 0x13, /* max supported QP IRD/ORD
						 */
	FW_PARAMS_PARAM_DEV_MAXIRD_ADAPTER= 0x14,/* max supported ADAPTER IRD
						 */
	FW_PARAMS_PARAM_DEV_INTFVER_FCOEPDU = 0x15,
	FW_PARAMS_PARAM_DEV_MCINIT	= 0x16,
	FW_PARAMS_PARAM_DEV_ULPTX_MEMWRITE_DSGL = 0x17,
	FW_PARAMS_PARAM_DEV_FWCACHE	= 0x18,
	FW_PARAMS_PARAM_DEV_RSSINFO	= 0x19,
	FW_PARAMS_PARAM_DEV_SCFGREV	= 0x1A,
	FW_PARAMS_PARAM_DEV_VPDREV	= 0x1B,
	FW_PARAMS_PARAM_DEV_RI_FR_NSMR_TPTE_WR	= 0x1C,
	FW_PARAMS_PARAM_DEV_FILTER2_WR	= 0x1D,

	FW_PARAMS_PARAM_DEV_MPSBGMAP	= 0x1E,
	FW_PARAMS_PARAM_DEV_TPCHMAP	= 0x1F,
	FW_PARAMS_PARAM_DEV_HMA_SIZE	= 0x20,
	FW_PARAMS_PARAM_DEV_RDMA_WRITE_WITH_IMM	= 0x21,
	FW_PARAMS_PARAM_DEV_RING_BACKBONE	= 0x22,
	FW_PARAMS_PARAM_DEV_PPOD_EDRAM	= 0x23,
	FW_PARAMS_PARAM_DEV_RI_WRITE_CMPL_WR	= 0x24,
	FW_PARAMS_PARAM_DEV_ADD_SMAC = 0x25,
	FW_PARAMS_PARAM_DEV_HPFILTER_REGION_SUPPORT = 0x26,
	FW_PARAMS_PARAM_DEV_OPAQUE_VIID_SMT_EXTN = 0x27,
};

/*
 * dev bypass parameters; actions and modes
 */
enum fw_params_param_dev_bypass {

	/* actions
	 */
	FW_PARAMS_PARAM_DEV_BYPASS_PFAIL = 0x00,
	FW_PARAMS_PARAM_DEV_BYPASS_CURRENT = 0x01,

	/* modes
	 */
	FW_PARAMS_PARAM_DEV_BYPASS_NORMAL = 0x00,
	FW_PARAMS_PARAM_DEV_BYPASS_DROP	= 0x1,
	FW_PARAMS_PARAM_DEV_BYPASS_BYPASS = 0x2,
};

enum fw_params_param_dev_phyfw {
	FW_PARAMS_PARAM_DEV_PHYFW_DOWNLOAD = 0x00,
	FW_PARAMS_PARAM_DEV_PHYFW_VERSION = 0x01,
};

enum fw_params_param_dev_diag {
	FW_PARAM_DEV_DIAG_TMP		= 0x00,
	FW_PARAM_DEV_DIAG_VDD		= 0x01,
	FW_PARAM_DEV_DIAG_MAXTMPTHRESH	= 0x02,
};

enum fw_params_param_dev_fwcache {
	FW_PARAM_DEV_FWCACHE_FLUSH	= 0x00,
	FW_PARAM_DEV_FWCACHE_FLUSHINV	= 0x01,
};

/*
 * physical and virtual function parameters
 */
enum fw_params_param_pfvf {
	FW_PARAMS_PARAM_PFVF_RWXCAPS	= 0x00,
	FW_PARAMS_PARAM_PFVF_ROUTE_START = 0x01,
	FW_PARAMS_PARAM_PFVF_ROUTE_END = 0x02,
	FW_PARAMS_PARAM_PFVF_CLIP_START = 0x03,
	FW_PARAMS_PARAM_PFVF_CLIP_END = 0x04,
	FW_PARAMS_PARAM_PFVF_FILTER_START = 0x05,
	FW_PARAMS_PARAM_PFVF_FILTER_END = 0x06,
	FW_PARAMS_PARAM_PFVF_SERVER_START = 0x07,
	FW_PARAMS_PARAM_PFVF_SERVER_END = 0x08,
	FW_PARAMS_PARAM_PFVF_TDDP_START = 0x09,
	FW_PARAMS_PARAM_PFVF_TDDP_END = 0x0A,
	FW_PARAMS_PARAM_PFVF_ISCSI_START = 0x0B,
	FW_PARAMS_PARAM_PFVF_ISCSI_END = 0x0C,
	FW_PARAMS_PARAM_PFVF_STAG_START = 0x0D,
	FW_PARAMS_PARAM_PFVF_STAG_END = 0x0E,
	FW_PARAMS_PARAM_PFVF_RQ_START = 0x1F,
	FW_PARAMS_PARAM_PFVF_RQ_END	= 0x10,
	FW_PARAMS_PARAM_PFVF_PBL_START = 0x11,
	FW_PARAMS_PARAM_PFVF_PBL_END	= 0x12,
	FW_PARAMS_PARAM_PFVF_L2T_START = 0x13,
	FW_PARAMS_PARAM_PFVF_L2T_END = 0x14,
	FW_PARAMS_PARAM_PFVF_SQRQ_START = 0x15,
	FW_PARAMS_PARAM_PFVF_SQRQ_END	= 0x16,
	FW_PARAMS_PARAM_PFVF_CQ_START	= 0x17,
	FW_PARAMS_PARAM_PFVF_CQ_END	= 0x18,
	FW_PARAMS_PARAM_PFVF_SRQ_START	= 0x19,
	FW_PARAMS_PARAM_PFVF_SRQ_END	= 0x1A,
	FW_PARAMS_PARAM_PFVF_SCHEDCLASS_ETH = 0x20,
	FW_PARAMS_PARAM_PFVF_VIID	= 0x24,
	FW_PARAMS_PARAM_PFVF_CPMASK	= 0x25,
	FW_PARAMS_PARAM_PFVF_OCQ_START	= 0x26,
	FW_PARAMS_PARAM_PFVF_OCQ_END	= 0x27,
	FW_PARAMS_PARAM_PFVF_CONM_MAP   = 0x28,
	FW_PARAMS_PARAM_PFVF_IQFLINT_START = 0x29,
	FW_PARAMS_PARAM_PFVF_IQFLINT_END = 0x2A,
	FW_PARAMS_PARAM_PFVF_EQ_START	= 0x2B,
	FW_PARAMS_PARAM_PFVF_EQ_END	= 0x2C,
	FW_PARAMS_PARAM_PFVF_ACTIVE_FILTER_START = 0x2D,
	FW_PARAMS_PARAM_PFVF_ACTIVE_FILTER_END = 0x2E,
	FW_PARAMS_PARAM_PFVF_ETHOFLD_START = 0x2F,
	FW_PARAMS_PARAM_PFVF_ETHOFLD_END = 0x30,
	FW_PARAMS_PARAM_PFVF_CPLFW4MSG_ENCAP = 0x31,
	FW_PARAMS_PARAM_PFVF_HPFILTER_START = 0x32,
	FW_PARAMS_PARAM_PFVF_HPFILTER_END = 0x33,
	FW_PARAMS_PARAM_PFVF_TLS_START = 0x34,
        FW_PARAMS_PARAM_PFVF_TLS_END = 0x35,
	FW_PARAMS_PARAM_PFVF_RAWF_START	= 0x36,
	FW_PARAMS_PARAM_PFVF_RAWF_END	= 0x37,
	FW_PARAMS_PARAM_PFVF_RSSKEYINFO	= 0x38,
	FW_PARAMS_PARAM_PFVF_NCRYPTO_LOOKASIDE = 0x39,
	FW_PARAMS_PARAM_PFVF_PORT_CAPS32 = 0x3A,
	FW_PARAMS_PARAM_PFVF_PPOD_EDRAM_START = 0x3B,
	FW_PARAMS_PARAM_PFVF_PPOD_EDRAM_END = 0x3C,
	FW_PARAMS_PARAM_PFVF_MAX_PKTS_PER_ETH_TX_PKTS_WR = 0x3D,
};

/*
 * dma queue parameters
 */
enum fw_params_param_dmaq {
	FW_PARAMS_PARAM_DMAQ_IQ_DCAEN_DCACPU = 0x00,
	FW_PARAMS_PARAM_DMAQ_IQ_INTCNTTHRESH = 0x01,
	FW_PARAMS_PARAM_DMAQ_IQ_INTIDX	= 0x02,
	FW_PARAMS_PARAM_DMAQ_IQ_DCA	= 0x03,
	FW_PARAMS_PARAM_DMAQ_EQ_CMPLIQID_MNGT = 0x10,
	FW_PARAMS_PARAM_DMAQ_EQ_CMPLIQID_CTRL = 0x11,
	FW_PARAMS_PARAM_DMAQ_EQ_SCHEDCLASS_ETH = 0x12,
	FW_PARAMS_PARAM_DMAQ_EQ_DCBPRIO_ETH = 0x13,
	FW_PARAMS_PARAM_DMAQ_EQ_DCA	= 0x14,
	FW_PARAMS_PARAM_DMAQ_CONM_CTXT	= 0x20,
	FW_PARAMS_PARAM_DMAQ_FLM_DCA	= 0x30
};

/*
 * chnet parameters
 */
enum fw_params_param_chnet {
	FW_PARAMS_PARAM_CHNET_FLAGS		= 0x00,
};

enum fw_params_param_chnet_flags {
	FW_PARAMS_PARAM_CHNET_FLAGS_ENABLE_IPV6	= 0x1,
	FW_PARAMS_PARAM_CHNET_FLAGS_ENABLE_DAD	= 0x2,
	FW_PARAMS_PARAM_CHNET_FLAGS_ENABLE_MLDV2= 0x4,
};

#define S_FW_PARAMS_MNEM	24
#define M_FW_PARAMS_MNEM	0xff
#define V_FW_PARAMS_MNEM(x)	((x) << S_FW_PARAMS_MNEM)
#define G_FW_PARAMS_MNEM(x)	\
    (((x) >> S_FW_PARAMS_MNEM) & M_FW_PARAMS_MNEM)

#define S_FW_PARAMS_PARAM_X	16
#define M_FW_PARAMS_PARAM_X	0xff
#define V_FW_PARAMS_PARAM_X(x) ((x) << S_FW_PARAMS_PARAM_X)
#define G_FW_PARAMS_PARAM_X(x) \
    (((x) >> S_FW_PARAMS_PARAM_X) & M_FW_PARAMS_PARAM_X)

#define S_FW_PARAMS_PARAM_Y	8
#define M_FW_PARAMS_PARAM_Y	0xff
#define V_FW_PARAMS_PARAM_Y(x) ((x) << S_FW_PARAMS_PARAM_Y)
#define G_FW_PARAMS_PARAM_Y(x) \
    (((x) >> S_FW_PARAMS_PARAM_Y) & M_FW_PARAMS_PARAM_Y)

#define S_FW_PARAMS_PARAM_Z	0
#define M_FW_PARAMS_PARAM_Z	0xff
#define V_FW_PARAMS_PARAM_Z(x) ((x) << S_FW_PARAMS_PARAM_Z)
#define G_FW_PARAMS_PARAM_Z(x) \
    (((x) >> S_FW_PARAMS_PARAM_Z) & M_FW_PARAMS_PARAM_Z)

#define S_FW_PARAMS_PARAM_XYZ	0
#define M_FW_PARAMS_PARAM_XYZ	0xffffff
#define V_FW_PARAMS_PARAM_XYZ(x) ((x) << S_FW_PARAMS_PARAM_XYZ)
#define G_FW_PARAMS_PARAM_XYZ(x) \
    (((x) >> S_FW_PARAMS_PARAM_XYZ) & M_FW_PARAMS_PARAM_XYZ)

#define S_FW_PARAMS_PARAM_YZ	0
#define M_FW_PARAMS_PARAM_YZ	0xffff
#define V_FW_PARAMS_PARAM_YZ(x) ((x) << S_FW_PARAMS_PARAM_YZ)
#define G_FW_PARAMS_PARAM_YZ(x) \
    (((x) >> S_FW_PARAMS_PARAM_YZ) & M_FW_PARAMS_PARAM_YZ)

#define S_FW_PARAMS_PARAM_DMAQ_DCA_TPHINTEN 31
#define M_FW_PARAMS_PARAM_DMAQ_DCA_TPHINTEN 0x1
#define V_FW_PARAMS_PARAM_DMAQ_DCA_TPHINTEN(x) \
    ((x) << S_FW_PARAMS_PARAM_DMAQ_DCA_TPHINTEN)
#define G_FW_PARAMS_PARAM_DMAQ_DCA_TPHINTEN(x) \
    (((x) >> S_FW_PARAMS_PARAM_DMAQ_DCA_TPHINTEN) & \
	M_FW_PARAMS_PARAM_DMAQ_DCA_TPHINTEN)

#define S_FW_PARAMS_PARAM_DMAQ_DCA_TPHINT 24
#define M_FW_PARAMS_PARAM_DMAQ_DCA_TPHINT 0x3
#define V_FW_PARAMS_PARAM_DMAQ_DCA_TPHINT(x) \
    ((x) << S_FW_PARAMS_PARAM_DMAQ_DCA_TPHINT)
#define G_FW_PARAMS_PARAM_DMAQ_DCA_TPHINT(x) \
    (((x) >> S_FW_PARAMS_PARAM_DMAQ_DCA_TPHINT) & \
	M_FW_PARAMS_PARAM_DMAQ_DCA_TPHINT)

#define S_FW_PARAMS_PARAM_DMAQ_DCA_ST	0
#define M_FW_PARAMS_PARAM_DMAQ_DCA_ST	0x7ff
#define V_FW_PARAMS_PARAM_DMAQ_DCA_ST(x) \
    ((x) << S_FW_PARAMS_PARAM_DMAQ_DCA_ST)
#define G_FW_PARAMS_PARAM_DMAQ_DCA_ST(x) \
    (((x) >> S_FW_PARAMS_PARAM_DMAQ_DCA_ST) & M_FW_PARAMS_PARAM_DMAQ_DCA_ST)

#define S_FW_PARAMS_PARAM_DMAQ_INTIDX_QTYPE	29
#define M_FW_PARAMS_PARAM_DMAQ_INTIDX_QTYPE	0x7
#define V_FW_PARAMS_PARAM_DMAQ_INTIDX_QTYPE(x)	\
    ((x) << S_FW_PARAMS_PARAM_DMAQ_INTIDX_QTYPE)
#define G_FW_PARAMS_PARAM_DMAQ_INTIDX_QTYPE(x)	\
    (((x) >> S_FW_PARAMS_PARAM_DMAQ_INTIDX_QTYPE) & \
     M_FW_PARAMS_PARAM_DMAQ_INTIDX_QTYPE)

#define S_FW_PARAMS_PARAM_DMAQ_INTIDX_INTIDX	0
#define M_FW_PARAMS_PARAM_DMAQ_INTIDX_INTIDX	0x3ff
#define V_FW_PARAMS_PARAM_DMAQ_INTIDX_INTIDX(x)	\
    ((x) << S_FW_PARAMS_PARAM_DMAQ_INTIDX_INTIDX)
#define G_FW_PARAMS_PARAM_DMAQ_INTIDX_INTIDX(x)	\
    (((x) >> S_FW_PARAMS_PARAM_DMAQ_INTIDX_INTIDX) & \
     M_FW_PARAMS_PARAM_DMAQ_INTIDX_INTIDX)

struct fw_params_cmd {
	__be32 op_to_vfn;
	__be32 retval_len16;
	struct fw_params_param {
		__be32 mnem;
		__be32 val;
	} param[7];
};

#define S_FW_PARAMS_CMD_PFN		8
#define M_FW_PARAMS_CMD_PFN		0x7
#define V_FW_PARAMS_CMD_PFN(x)		((x) << S_FW_PARAMS_CMD_PFN)
#define G_FW_PARAMS_CMD_PFN(x)		\
    (((x) >> S_FW_PARAMS_CMD_PFN) & M_FW_PARAMS_CMD_PFN)

#define S_FW_PARAMS_CMD_VFN		0
#define M_FW_PARAMS_CMD_VFN		0xff
#define V_FW_PARAMS_CMD_VFN(x)		((x) << S_FW_PARAMS_CMD_VFN)
#define G_FW_PARAMS_CMD_VFN(x)		\
    (((x) >> S_FW_PARAMS_CMD_VFN) & M_FW_PARAMS_CMD_VFN)

struct fw_pfvf_cmd {
	__be32 op_to_vfn;
	__be32 retval_len16;
	__be32 niqflint_niq;
	__be32 type_to_neq;
	__be32 tc_to_nexactf;
	__be32 r_caps_to_nethctrl;
	__be16 nricq;
	__be16 nriqp;
	__be32 r4;
};

#define S_FW_PFVF_CMD_PFN		8
#define M_FW_PFVF_CMD_PFN		0x7
#define V_FW_PFVF_CMD_PFN(x)		((x) << S_FW_PFVF_CMD_PFN)
#define G_FW_PFVF_CMD_PFN(x)		\
    (((x) >> S_FW_PFVF_CMD_PFN) & M_FW_PFVF_CMD_PFN)

#define S_FW_PFVF_CMD_VFN		0
#define M_FW_PFVF_CMD_VFN		0xff
#define V_FW_PFVF_CMD_VFN(x)		((x) << S_FW_PFVF_CMD_VFN)
#define G_FW_PFVF_CMD_VFN(x)		\
    (((x) >> S_FW_PFVF_CMD_VFN) & M_FW_PFVF_CMD_VFN)

#define S_FW_PFVF_CMD_NIQFLINT		20
#define M_FW_PFVF_CMD_NIQFLINT		0xfff
#define V_FW_PFVF_CMD_NIQFLINT(x)	((x) << S_FW_PFVF_CMD_NIQFLINT)
#define G_FW_PFVF_CMD_NIQFLINT(x)	\
    (((x) >> S_FW_PFVF_CMD_NIQFLINT) & M_FW_PFVF_CMD_NIQFLINT)

#define S_FW_PFVF_CMD_NIQ		0
#define M_FW_PFVF_CMD_NIQ		0xfffff
#define V_FW_PFVF_CMD_NIQ(x)		((x) << S_FW_PFVF_CMD_NIQ)
#define G_FW_PFVF_CMD_NIQ(x)		\
    (((x) >> S_FW_PFVF_CMD_NIQ) & M_FW_PFVF_CMD_NIQ)

#define S_FW_PFVF_CMD_TYPE		31
#define M_FW_PFVF_CMD_TYPE		0x1
#define V_FW_PFVF_CMD_TYPE(x)		((x) << S_FW_PFVF_CMD_TYPE)
#define G_FW_PFVF_CMD_TYPE(x)		\
    (((x) >> S_FW_PFVF_CMD_TYPE) & M_FW_PFVF_CMD_TYPE)
#define F_FW_PFVF_CMD_TYPE		V_FW_PFVF_CMD_TYPE(1U)

#define S_FW_PFVF_CMD_CMASK		24
#define M_FW_PFVF_CMD_CMASK		0xf
#define V_FW_PFVF_CMD_CMASK(x)		((x) << S_FW_PFVF_CMD_CMASK)
#define G_FW_PFVF_CMD_CMASK(x)		\
    (((x) >> S_FW_PFVF_CMD_CMASK) & M_FW_PFVF_CMD_CMASK)

#define S_FW_PFVF_CMD_PMASK		20
#define M_FW_PFVF_CMD_PMASK		0xf
#define V_FW_PFVF_CMD_PMASK(x)		((x) << S_FW_PFVF_CMD_PMASK)
#define G_FW_PFVF_CMD_PMASK(x)		\
    (((x) >> S_FW_PFVF_CMD_PMASK) & M_FW_PFVF_CMD_PMASK)

#define S_FW_PFVF_CMD_NEQ		0
#define M_FW_PFVF_CMD_NEQ		0xfffff
#define V_FW_PFVF_CMD_NEQ(x)		((x) << S_FW_PFVF_CMD_NEQ)
#define G_FW_PFVF_CMD_NEQ(x)		\
    (((x) >> S_FW_PFVF_CMD_NEQ) & M_FW_PFVF_CMD_NEQ)

#define S_FW_PFVF_CMD_TC		24
#define M_FW_PFVF_CMD_TC		0xff
#define V_FW_PFVF_CMD_TC(x)		((x) << S_FW_PFVF_CMD_TC)
#define G_FW_PFVF_CMD_TC(x)		\
    (((x) >> S_FW_PFVF_CMD_TC) & M_FW_PFVF_CMD_TC)

#define S_FW_PFVF_CMD_NVI		16
#define M_FW_PFVF_CMD_NVI		0xff
#define V_FW_PFVF_CMD_NVI(x)		((x) << S_FW_PFVF_CMD_NVI)
#define G_FW_PFVF_CMD_NVI(x)		\
    (((x) >> S_FW_PFVF_CMD_NVI) & M_FW_PFVF_CMD_NVI)

#define S_FW_PFVF_CMD_NEXACTF		0
#define M_FW_PFVF_CMD_NEXACTF		0xffff
#define V_FW_PFVF_CMD_NEXACTF(x)	((x) << S_FW_PFVF_CMD_NEXACTF)
#define G_FW_PFVF_CMD_NEXACTF(x)	\
    (((x) >> S_FW_PFVF_CMD_NEXACTF) & M_FW_PFVF_CMD_NEXACTF)

#define S_FW_PFVF_CMD_R_CAPS		24
#define M_FW_PFVF_CMD_R_CAPS		0xff
#define V_FW_PFVF_CMD_R_CAPS(x)		((x) << S_FW_PFVF_CMD_R_CAPS)
#define G_FW_PFVF_CMD_R_CAPS(x)		\
    (((x) >> S_FW_PFVF_CMD_R_CAPS) & M_FW_PFVF_CMD_R_CAPS)

#define S_FW_PFVF_CMD_WX_CAPS		16
#define M_FW_PFVF_CMD_WX_CAPS		0xff
#define V_FW_PFVF_CMD_WX_CAPS(x)	((x) << S_FW_PFVF_CMD_WX_CAPS)
#define G_FW_PFVF_CMD_WX_CAPS(x)	\
    (((x) >> S_FW_PFVF_CMD_WX_CAPS) & M_FW_PFVF_CMD_WX_CAPS)

#define S_FW_PFVF_CMD_NETHCTRL		0
#define M_FW_PFVF_CMD_NETHCTRL		0xffff
#define V_FW_PFVF_CMD_NETHCTRL(x)	((x) << S_FW_PFVF_CMD_NETHCTRL)
#define G_FW_PFVF_CMD_NETHCTRL(x)	\
    (((x) >> S_FW_PFVF_CMD_NETHCTRL) & M_FW_PFVF_CMD_NETHCTRL)

/*
 *	ingress queue type; the first 1K ingress queues can have associated 0,
 *	1 or 2 free lists and an interrupt, all other ingress queues lack these
 *	capabilities
 */
enum fw_iq_type {
	FW_IQ_TYPE_FL_INT_CAP,
	FW_IQ_TYPE_NO_FL_INT_CAP,
	FW_IQ_TYPE_VF_CQ
};

enum fw_iq_iqtype {
	FW_IQ_IQTYPE_OTHER,
	FW_IQ_IQTYPE_NIC,
	FW_IQ_IQTYPE_OFLD,
};

struct fw_iq_cmd {
	__be32 op_to_vfn;
	__be32 alloc_to_len16;
	__be16 physiqid;
	__be16 iqid;
	__be16 fl0id;
	__be16 fl1id;
	__be32 type_to_iqandstindex;
	__be16 iqdroprss_to_iqesize;
	__be16 iqsize;
	__be64 iqaddr;
	__be32 iqns_to_fl0congen;
	__be16 fl0dcaen_to_fl0cidxfthresh;
	__be16 fl0size;
	__be64 fl0addr;
	__be32 fl1cngchmap_to_fl1congen;
	__be16 fl1dcaen_to_fl1cidxfthresh;
	__be16 fl1size;
	__be64 fl1addr;
};

#define S_FW_IQ_CMD_PFN			8
#define M_FW_IQ_CMD_PFN			0x7
#define V_FW_IQ_CMD_PFN(x)		((x) << S_FW_IQ_CMD_PFN)
#define G_FW_IQ_CMD_PFN(x)		\
    (((x) >> S_FW_IQ_CMD_PFN) & M_FW_IQ_CMD_PFN)

#define S_FW_IQ_CMD_VFN			0
#define M_FW_IQ_CMD_VFN			0xff
#define V_FW_IQ_CMD_VFN(x)		((x) << S_FW_IQ_CMD_VFN)
#define G_FW_IQ_CMD_VFN(x)		\
    (((x) >> S_FW_IQ_CMD_VFN) & M_FW_IQ_CMD_VFN)

#define S_FW_IQ_CMD_ALLOC		31
#define M_FW_IQ_CMD_ALLOC		0x1
#define V_FW_IQ_CMD_ALLOC(x)		((x) << S_FW_IQ_CMD_ALLOC)
#define G_FW_IQ_CMD_ALLOC(x)		\
    (((x) >> S_FW_IQ_CMD_ALLOC) & M_FW_IQ_CMD_ALLOC)
#define F_FW_IQ_CMD_ALLOC		V_FW_IQ_CMD_ALLOC(1U)

#define S_FW_IQ_CMD_FREE		30
#define M_FW_IQ_CMD_FREE		0x1
#define V_FW_IQ_CMD_FREE(x)		((x) << S_FW_IQ_CMD_FREE)
#define G_FW_IQ_CMD_FREE(x)		\
    (((x) >> S_FW_IQ_CMD_FREE) & M_FW_IQ_CMD_FREE)
#define F_FW_IQ_CMD_FREE		V_FW_IQ_CMD_FREE(1U)

#define S_FW_IQ_CMD_MODIFY		29
#define M_FW_IQ_CMD_MODIFY		0x1
#define V_FW_IQ_CMD_MODIFY(x)		((x) << S_FW_IQ_CMD_MODIFY)
#define G_FW_IQ_CMD_MODIFY(x)		\
    (((x) >> S_FW_IQ_CMD_MODIFY) & M_FW_IQ_CMD_MODIFY)
#define F_FW_IQ_CMD_MODIFY		V_FW_IQ_CMD_MODIFY(1U)

#define S_FW_IQ_CMD_IQSTART		28
#define M_FW_IQ_CMD_IQSTART		0x1
#define V_FW_IQ_CMD_IQSTART(x)		((x) << S_FW_IQ_CMD_IQSTART)
#define G_FW_IQ_CMD_IQSTART(x)		\
    (((x) >> S_FW_IQ_CMD_IQSTART) & M_FW_IQ_CMD_IQSTART)
#define F_FW_IQ_CMD_IQSTART		V_FW_IQ_CMD_IQSTART(1U)

#define S_FW_IQ_CMD_IQSTOP		27
#define M_FW_IQ_CMD_IQSTOP		0x1
#define V_FW_IQ_CMD_IQSTOP(x)		((x) << S_FW_IQ_CMD_IQSTOP)
#define G_FW_IQ_CMD_IQSTOP(x)		\
    (((x) >> S_FW_IQ_CMD_IQSTOP) & M_FW_IQ_CMD_IQSTOP)
#define F_FW_IQ_CMD_IQSTOP		V_FW_IQ_CMD_IQSTOP(1U)

#define S_FW_IQ_CMD_TYPE		29
#define M_FW_IQ_CMD_TYPE		0x7
#define V_FW_IQ_CMD_TYPE(x)		((x) << S_FW_IQ_CMD_TYPE)
#define G_FW_IQ_CMD_TYPE(x)		\
    (((x) >> S_FW_IQ_CMD_TYPE) & M_FW_IQ_CMD_TYPE)

#define S_FW_IQ_CMD_IQASYNCH		28
#define M_FW_IQ_CMD_IQASYNCH		0x1
#define V_FW_IQ_CMD_IQASYNCH(x)		((x) << S_FW_IQ_CMD_IQASYNCH)
#define G_FW_IQ_CMD_IQASYNCH(x)		\
    (((x) >> S_FW_IQ_CMD_IQASYNCH) & M_FW_IQ_CMD_IQASYNCH)
#define F_FW_IQ_CMD_IQASYNCH		V_FW_IQ_CMD_IQASYNCH(1U)

#define S_FW_IQ_CMD_VIID		16
#define M_FW_IQ_CMD_VIID		0xfff
#define V_FW_IQ_CMD_VIID(x)		((x) << S_FW_IQ_CMD_VIID)
#define G_FW_IQ_CMD_VIID(x)		\
    (((x) >> S_FW_IQ_CMD_VIID) & M_FW_IQ_CMD_VIID)

#define S_FW_IQ_CMD_IQANDST		15
#define M_FW_IQ_CMD_IQANDST		0x1
#define V_FW_IQ_CMD_IQANDST(x)		((x) << S_FW_IQ_CMD_IQANDST)
#define G_FW_IQ_CMD_IQANDST(x)		\
    (((x) >> S_FW_IQ_CMD_IQANDST) & M_FW_IQ_CMD_IQANDST)
#define F_FW_IQ_CMD_IQANDST		V_FW_IQ_CMD_IQANDST(1U)

#define S_FW_IQ_CMD_IQANUS		14
#define M_FW_IQ_CMD_IQANUS		0x1
#define V_FW_IQ_CMD_IQANUS(x)		((x) << S_FW_IQ_CMD_IQANUS)
#define G_FW_IQ_CMD_IQANUS(x)		\
    (((x) >> S_FW_IQ_CMD_IQANUS) & M_FW_IQ_CMD_IQANUS)
#define F_FW_IQ_CMD_IQANUS		V_FW_IQ_CMD_IQANUS(1U)

#define S_FW_IQ_CMD_IQANUD		12
#define M_FW_IQ_CMD_IQANUD		0x3
#define V_FW_IQ_CMD_IQANUD(x)		((x) << S_FW_IQ_CMD_IQANUD)
#define G_FW_IQ_CMD_IQANUD(x)		\
    (((x) >> S_FW_IQ_CMD_IQANUD) & M_FW_IQ_CMD_IQANUD)

#define S_FW_IQ_CMD_IQANDSTINDEX	0
#define M_FW_IQ_CMD_IQANDSTINDEX	0xfff
#define V_FW_IQ_CMD_IQANDSTINDEX(x)	((x) << S_FW_IQ_CMD_IQANDSTINDEX)
#define G_FW_IQ_CMD_IQANDSTINDEX(x)	\
    (((x) >> S_FW_IQ_CMD_IQANDSTINDEX) & M_FW_IQ_CMD_IQANDSTINDEX)

#define S_FW_IQ_CMD_IQDROPRSS		15
#define M_FW_IQ_CMD_IQDROPRSS		0x1
#define V_FW_IQ_CMD_IQDROPRSS(x)	((x) << S_FW_IQ_CMD_IQDROPRSS)
#define G_FW_IQ_CMD_IQDROPRSS(x)	\
    (((x) >> S_FW_IQ_CMD_IQDROPRSS) & M_FW_IQ_CMD_IQDROPRSS)
#define F_FW_IQ_CMD_IQDROPRSS		V_FW_IQ_CMD_IQDROPRSS(1U)

#define S_FW_IQ_CMD_IQGTSMODE		14
#define M_FW_IQ_CMD_IQGTSMODE		0x1
#define V_FW_IQ_CMD_IQGTSMODE(x)	((x) << S_FW_IQ_CMD_IQGTSMODE)
#define G_FW_IQ_CMD_IQGTSMODE(x)	\
    (((x) >> S_FW_IQ_CMD_IQGTSMODE) & M_FW_IQ_CMD_IQGTSMODE)
#define F_FW_IQ_CMD_IQGTSMODE		V_FW_IQ_CMD_IQGTSMODE(1U)

#define S_FW_IQ_CMD_IQPCIECH		12
#define M_FW_IQ_CMD_IQPCIECH		0x3
#define V_FW_IQ_CMD_IQPCIECH(x)		((x) << S_FW_IQ_CMD_IQPCIECH)
#define G_FW_IQ_CMD_IQPCIECH(x)		\
    (((x) >> S_FW_IQ_CMD_IQPCIECH) & M_FW_IQ_CMD_IQPCIECH)

#define S_FW_IQ_CMD_IQDCAEN		11
#define M_FW_IQ_CMD_IQDCAEN		0x1
#define V_FW_IQ_CMD_IQDCAEN(x)		((x) << S_FW_IQ_CMD_IQDCAEN)
#define G_FW_IQ_CMD_IQDCAEN(x)		\
    (((x) >> S_FW_IQ_CMD_IQDCAEN) & M_FW_IQ_CMD_IQDCAEN)
#define F_FW_IQ_CMD_IQDCAEN		V_FW_IQ_CMD_IQDCAEN(1U)

#define S_FW_IQ_CMD_IQDCACPU		6
#define M_FW_IQ_CMD_IQDCACPU		0x1f
#define V_FW_IQ_CMD_IQDCACPU(x)		((x) << S_FW_IQ_CMD_IQDCACPU)
#define G_FW_IQ_CMD_IQDCACPU(x)		\
    (((x) >> S_FW_IQ_CMD_IQDCACPU) & M_FW_IQ_CMD_IQDCACPU)

#define S_FW_IQ_CMD_IQINTCNTTHRESH	4
#define M_FW_IQ_CMD_IQINTCNTTHRESH	0x3
#define V_FW_IQ_CMD_IQINTCNTTHRESH(x)	((x) << S_FW_IQ_CMD_IQINTCNTTHRESH)
#define G_FW_IQ_CMD_IQINTCNTTHRESH(x)	\
    (((x) >> S_FW_IQ_CMD_IQINTCNTTHRESH) & M_FW_IQ_CMD_IQINTCNTTHRESH)

#define S_FW_IQ_CMD_IQO			3
#define M_FW_IQ_CMD_IQO			0x1
#define V_FW_IQ_CMD_IQO(x)		((x) << S_FW_IQ_CMD_IQO)
#define G_FW_IQ_CMD_IQO(x)		\
    (((x) >> S_FW_IQ_CMD_IQO) & M_FW_IQ_CMD_IQO)
#define F_FW_IQ_CMD_IQO			V_FW_IQ_CMD_IQO(1U)

#define S_FW_IQ_CMD_IQCPRIO		2
#define M_FW_IQ_CMD_IQCPRIO		0x1
#define V_FW_IQ_CMD_IQCPRIO(x)		((x) << S_FW_IQ_CMD_IQCPRIO)
#define G_FW_IQ_CMD_IQCPRIO(x)		\
    (((x) >> S_FW_IQ_CMD_IQCPRIO) & M_FW_IQ_CMD_IQCPRIO)
#define F_FW_IQ_CMD_IQCPRIO		V_FW_IQ_CMD_IQCPRIO(1U)

#define S_FW_IQ_CMD_IQESIZE		0
#define M_FW_IQ_CMD_IQESIZE		0x3
#define V_FW_IQ_CMD_IQESIZE(x)		((x) << S_FW_IQ_CMD_IQESIZE)
#define G_FW_IQ_CMD_IQESIZE(x)		\
    (((x) >> S_FW_IQ_CMD_IQESIZE) & M_FW_IQ_CMD_IQESIZE)

#define S_FW_IQ_CMD_IQNS		31
#define M_FW_IQ_CMD_IQNS		0x1
#define V_FW_IQ_CMD_IQNS(x)		((x) << S_FW_IQ_CMD_IQNS)
#define G_FW_IQ_CMD_IQNS(x)		\
    (((x) >> S_FW_IQ_CMD_IQNS) & M_FW_IQ_CMD_IQNS)
#define F_FW_IQ_CMD_IQNS		V_FW_IQ_CMD_IQNS(1U)

#define S_FW_IQ_CMD_IQRO		30
#define M_FW_IQ_CMD_IQRO		0x1
#define V_FW_IQ_CMD_IQRO(x)		((x) << S_FW_IQ_CMD_IQRO)
#define G_FW_IQ_CMD_IQRO(x)		\
    (((x) >> S_FW_IQ_CMD_IQRO) & M_FW_IQ_CMD_IQRO)
#define F_FW_IQ_CMD_IQRO		V_FW_IQ_CMD_IQRO(1U)

#define S_FW_IQ_CMD_IQFLINTIQHSEN	28
#define M_FW_IQ_CMD_IQFLINTIQHSEN	0x3
#define V_FW_IQ_CMD_IQFLINTIQHSEN(x)	((x) << S_FW_IQ_CMD_IQFLINTIQHSEN)
#define G_FW_IQ_CMD_IQFLINTIQHSEN(x)	\
    (((x) >> S_FW_IQ_CMD_IQFLINTIQHSEN) & M_FW_IQ_CMD_IQFLINTIQHSEN)

#define S_FW_IQ_CMD_IQFLINTCONGEN	27
#define M_FW_IQ_CMD_IQFLINTCONGEN	0x1
#define V_FW_IQ_CMD_IQFLINTCONGEN(x)	((x) << S_FW_IQ_CMD_IQFLINTCONGEN)
#define G_FW_IQ_CMD_IQFLINTCONGEN(x)	\
    (((x) >> S_FW_IQ_CMD_IQFLINTCONGEN) & M_FW_IQ_CMD_IQFLINTCONGEN)
#define F_FW_IQ_CMD_IQFLINTCONGEN	V_FW_IQ_CMD_IQFLINTCONGEN(1U)

#define S_FW_IQ_CMD_IQFLINTISCSIC	26
#define M_FW_IQ_CMD_IQFLINTISCSIC	0x1
#define V_FW_IQ_CMD_IQFLINTISCSIC(x)	((x) << S_FW_IQ_CMD_IQFLINTISCSIC)
#define G_FW_IQ_CMD_IQFLINTISCSIC(x)	\
    (((x) >> S_FW_IQ_CMD_IQFLINTISCSIC) & M_FW_IQ_CMD_IQFLINTISCSIC)
#define F_FW_IQ_CMD_IQFLINTISCSIC	V_FW_IQ_CMD_IQFLINTISCSIC(1U)

#define S_FW_IQ_CMD_IQTYPE	24
#define M_FW_IQ_CMD_IQTYPE	0x3
#define V_FW_IQ_CMD_IQTYPE(x)	((x) << S_FW_IQ_CMD_IQTYPE)
#define G_FW_IQ_CMD_IQTYPE(x)	\
    (((x) >> S_FW_IQ_CMD_IQTYPE) & M_FW_IQ_CMD_IQTYPE)

#define S_FW_IQ_CMD_FL0CNGCHMAP		20
#define M_FW_IQ_CMD_FL0CNGCHMAP		0xf
#define V_FW_IQ_CMD_FL0CNGCHMAP(x)	((x) << S_FW_IQ_CMD_FL0CNGCHMAP)
#define G_FW_IQ_CMD_FL0CNGCHMAP(x)	\
    (((x) >> S_FW_IQ_CMD_FL0CNGCHMAP) & M_FW_IQ_CMD_FL0CNGCHMAP)

#define S_FW_IQ_CMD_FL0CONGDROP		16
#define M_FW_IQ_CMD_FL0CONGDROP		0x1
#define V_FW_IQ_CMD_FL0CONGDROP(x)	((x) << S_FW_IQ_CMD_FL0CONGDROP)
#define G_FW_IQ_CMD_FL0CONGDROP(x)	\
    (((x) >> S_FW_IQ_CMD_FL0CONGDROP) & M_FW_IQ_CMD_FL0CONGDROP)
#define F_FW_IQ_CMD_FL0CONGDROP		V_FW_IQ_CMD_FL0CONGDROP(1U)

#define S_FW_IQ_CMD_FL0CACHELOCK	15
#define M_FW_IQ_CMD_FL0CACHELOCK	0x1
#define V_FW_IQ_CMD_FL0CACHELOCK(x)	((x) << S_FW_IQ_CMD_FL0CACHELOCK)
#define G_FW_IQ_CMD_FL0CACHELOCK(x)	\
    (((x) >> S_FW_IQ_CMD_FL0CACHELOCK) & M_FW_IQ_CMD_FL0CACHELOCK)
#define F_FW_IQ_CMD_FL0CACHELOCK	V_FW_IQ_CMD_FL0CACHELOCK(1U)

#define S_FW_IQ_CMD_FL0DBP		14
#define M_FW_IQ_CMD_FL0DBP		0x1
#define V_FW_IQ_CMD_FL0DBP(x)		((x) << S_FW_IQ_CMD_FL0DBP)
#define G_FW_IQ_CMD_FL0DBP(x)		\
    (((x) >> S_FW_IQ_CMD_FL0DBP) & M_FW_IQ_CMD_FL0DBP)
#define F_FW_IQ_CMD_FL0DBP		V_FW_IQ_CMD_FL0DBP(1U)

#define S_FW_IQ_CMD_FL0DATANS		13
#define M_FW_IQ_CMD_FL0DATANS		0x1
#define V_FW_IQ_CMD_FL0DATANS(x)	((x) << S_FW_IQ_CMD_FL0DATANS)
#define G_FW_IQ_CMD_FL0DATANS(x)	\
    (((x) >> S_FW_IQ_CMD_FL0DATANS) & M_FW_IQ_CMD_FL0DATANS)
#define F_FW_IQ_CMD_FL0DATANS		V_FW_IQ_CMD_FL0DATANS(1U)

#define S_FW_IQ_CMD_FL0DATARO		12
#define M_FW_IQ_CMD_FL0DATARO		0x1
#define V_FW_IQ_CMD_FL0DATARO(x)	((x) << S_FW_IQ_CMD_FL0DATARO)
#define G_FW_IQ_CMD_FL0DATARO(x)	\
    (((x) >> S_FW_IQ_CMD_FL0DATARO) & M_FW_IQ_CMD_FL0DATARO)
#define F_FW_IQ_CMD_FL0DATARO		V_FW_IQ_CMD_FL0DATARO(1U)

#define S_FW_IQ_CMD_FL0CONGCIF		11
#define M_FW_IQ_CMD_FL0CONGCIF		0x1
#define V_FW_IQ_CMD_FL0CONGCIF(x)	((x) << S_FW_IQ_CMD_FL0CONGCIF)
#define G_FW_IQ_CMD_FL0CONGCIF(x)	\
    (((x) >> S_FW_IQ_CMD_FL0CONGCIF) & M_FW_IQ_CMD_FL0CONGCIF)
#define F_FW_IQ_CMD_FL0CONGCIF		V_FW_IQ_CMD_FL0CONGCIF(1U)

#define S_FW_IQ_CMD_FL0ONCHIP		10
#define M_FW_IQ_CMD_FL0ONCHIP		0x1
#define V_FW_IQ_CMD_FL0ONCHIP(x)	((x) << S_FW_IQ_CMD_FL0ONCHIP)
#define G_FW_IQ_CMD_FL0ONCHIP(x)	\
    (((x) >> S_FW_IQ_CMD_FL0ONCHIP) & M_FW_IQ_CMD_FL0ONCHIP)
#define F_FW_IQ_CMD_FL0ONCHIP		V_FW_IQ_CMD_FL0ONCHIP(1U)

#define S_FW_IQ_CMD_FL0STATUSPGNS	9
#define M_FW_IQ_CMD_FL0STATUSPGNS	0x1
#define V_FW_IQ_CMD_FL0STATUSPGNS(x)	((x) << S_FW_IQ_CMD_FL0STATUSPGNS)
#define G_FW_IQ_CMD_FL0STATUSPGNS(x)	\
    (((x) >> S_FW_IQ_CMD_FL0STATUSPGNS) & M_FW_IQ_CMD_FL0STATUSPGNS)
#define F_FW_IQ_CMD_FL0STATUSPGNS	V_FW_IQ_CMD_FL0STATUSPGNS(1U)

#define S_FW_IQ_CMD_FL0STATUSPGRO	8
#define M_FW_IQ_CMD_FL0STATUSPGRO	0x1
#define V_FW_IQ_CMD_FL0STATUSPGRO(x)	((x) << S_FW_IQ_CMD_FL0STATUSPGRO)
#define G_FW_IQ_CMD_FL0STATUSPGRO(x)	\
    (((x) >> S_FW_IQ_CMD_FL0STATUSPGRO) & M_FW_IQ_CMD_FL0STATUSPGRO)
#define F_FW_IQ_CMD_FL0STATUSPGRO	V_FW_IQ_CMD_FL0STATUSPGRO(1U)

#define S_FW_IQ_CMD_FL0FETCHNS		7
#define M_FW_IQ_CMD_FL0FETCHNS		0x1
#define V_FW_IQ_CMD_FL0FETCHNS(x)	((x) << S_FW_IQ_CMD_FL0FETCHNS)
#define G_FW_IQ_CMD_FL0FETCHNS(x)	\
    (((x) >> S_FW_IQ_CMD_FL0FETCHNS) & M_FW_IQ_CMD_FL0FETCHNS)
#define F_FW_IQ_CMD_FL0FETCHNS		V_FW_IQ_CMD_FL0FETCHNS(1U)

#define S_FW_IQ_CMD_FL0FETCHRO		6
#define M_FW_IQ_CMD_FL0FETCHRO		0x1
#define V_FW_IQ_CMD_FL0FETCHRO(x)	((x) << S_FW_IQ_CMD_FL0FETCHRO)
#define G_FW_IQ_CMD_FL0FETCHRO(x)	\
    (((x) >> S_FW_IQ_CMD_FL0FETCHRO) & M_FW_IQ_CMD_FL0FETCHRO)
#define F_FW_IQ_CMD_FL0FETCHRO		V_FW_IQ_CMD_FL0FETCHRO(1U)

#define S_FW_IQ_CMD_FL0HOSTFCMODE	4
#define M_FW_IQ_CMD_FL0HOSTFCMODE	0x3
#define V_FW_IQ_CMD_FL0HOSTFCMODE(x)	((x) << S_FW_IQ_CMD_FL0HOSTFCMODE)
#define G_FW_IQ_CMD_FL0HOSTFCMODE(x)	\
    (((x) >> S_FW_IQ_CMD_FL0HOSTFCMODE) & M_FW_IQ_CMD_FL0HOSTFCMODE)

#define S_FW_IQ_CMD_FL0CPRIO		3
#define M_FW_IQ_CMD_FL0CPRIO		0x1
#define V_FW_IQ_CMD_FL0CPRIO(x)		((x) << S_FW_IQ_CMD_FL0CPRIO)
#define G_FW_IQ_CMD_FL0CPRIO(x)		\
    (((x) >> S_FW_IQ_CMD_FL0CPRIO) & M_FW_IQ_CMD_FL0CPRIO)
#define F_FW_IQ_CMD_FL0CPRIO		V_FW_IQ_CMD_FL0CPRIO(1U)

#define S_FW_IQ_CMD_FL0PADEN		2
#define M_FW_IQ_CMD_FL0PADEN		0x1
#define V_FW_IQ_CMD_FL0PADEN(x)		((x) << S_FW_IQ_CMD_FL0PADEN)
#define G_FW_IQ_CMD_FL0PADEN(x)		\
    (((x) >> S_FW_IQ_CMD_FL0PADEN) & M_FW_IQ_CMD_FL0PADEN)
#define F_FW_IQ_CMD_FL0PADEN		V_FW_IQ_CMD_FL0PADEN(1U)

#define S_FW_IQ_CMD_FL0PACKEN		1
#define M_FW_IQ_CMD_FL0PACKEN		0x1
#define V_FW_IQ_CMD_FL0PACKEN(x)	((x) << S_FW_IQ_CMD_FL0PACKEN)
#define G_FW_IQ_CMD_FL0PACKEN(x)	\
    (((x) >> S_FW_IQ_CMD_FL0PACKEN) & M_FW_IQ_CMD_FL0PACKEN)
#define F_FW_IQ_CMD_FL0PACKEN		V_FW_IQ_CMD_FL0PACKEN(1U)

#define S_FW_IQ_CMD_FL0CONGEN		0
#define M_FW_IQ_CMD_FL0CONGEN		0x1
#define V_FW_IQ_CMD_FL0CONGEN(x)	((x) << S_FW_IQ_CMD_FL0CONGEN)
#define G_FW_IQ_CMD_FL0CONGEN(x)	\
    (((x) >> S_FW_IQ_CMD_FL0CONGEN) & M_FW_IQ_CMD_FL0CONGEN)
#define F_FW_IQ_CMD_FL0CONGEN		V_FW_IQ_CMD_FL0CONGEN(1U)

#define S_FW_IQ_CMD_FL0DCAEN		15
#define M_FW_IQ_CMD_FL0DCAEN		0x1
#define V_FW_IQ_CMD_FL0DCAEN(x)		((x) << S_FW_IQ_CMD_FL0DCAEN)
#define G_FW_IQ_CMD_FL0DCAEN(x)		\
    (((x) >> S_FW_IQ_CMD_FL0DCAEN) & M_FW_IQ_CMD_FL0DCAEN)
#define F_FW_IQ_CMD_FL0DCAEN		V_FW_IQ_CMD_FL0DCAEN(1U)

#define S_FW_IQ_CMD_FL0DCACPU		10
#define M_FW_IQ_CMD_FL0DCACPU		0x1f
#define V_FW_IQ_CMD_FL0DCACPU(x)	((x) << S_FW_IQ_CMD_FL0DCACPU)
#define G_FW_IQ_CMD_FL0DCACPU(x)	\
    (((x) >> S_FW_IQ_CMD_FL0DCACPU) & M_FW_IQ_CMD_FL0DCACPU)

#define S_FW_IQ_CMD_FL0FBMIN		7
#define M_FW_IQ_CMD_FL0FBMIN		0x7
#define V_FW_IQ_CMD_FL0FBMIN(x)		((x) << S_FW_IQ_CMD_FL0FBMIN)
#define G_FW_IQ_CMD_FL0FBMIN(x)		\
    (((x) >> S_FW_IQ_CMD_FL0FBMIN) & M_FW_IQ_CMD_FL0FBMIN)

#define S_FW_IQ_CMD_FL0FBMAX		4
#define M_FW_IQ_CMD_FL0FBMAX		0x7
#define V_FW_IQ_CMD_FL0FBMAX(x)		((x) << S_FW_IQ_CMD_FL0FBMAX)
#define G_FW_IQ_CMD_FL0FBMAX(x)		\
    (((x) >> S_FW_IQ_CMD_FL0FBMAX) & M_FW_IQ_CMD_FL0FBMAX)

#define S_FW_IQ_CMD_FL0CIDXFTHRESHO	3
#define M_FW_IQ_CMD_FL0CIDXFTHRESHO	0x1
#define V_FW_IQ_CMD_FL0CIDXFTHRESHO(x)	((x) << S_FW_IQ_CMD_FL0CIDXFTHRESHO)
#define G_FW_IQ_CMD_FL0CIDXFTHRESHO(x)	\
    (((x) >> S_FW_IQ_CMD_FL0CIDXFTHRESHO) & M_FW_IQ_CMD_FL0CIDXFTHRESHO)
#define F_FW_IQ_CMD_FL0CIDXFTHRESHO	V_FW_IQ_CMD_FL0CIDXFTHRESHO(1U)

#define S_FW_IQ_CMD_FL0CIDXFTHRESH	0
#define M_FW_IQ_CMD_FL0CIDXFTHRESH	0x7
#define V_FW_IQ_CMD_FL0CIDXFTHRESH(x)	((x) << S_FW_IQ_CMD_FL0CIDXFTHRESH)
#define G_FW_IQ_CMD_FL0CIDXFTHRESH(x)	\
    (((x) >> S_FW_IQ_CMD_FL0CIDXFTHRESH) & M_FW_IQ_CMD_FL0CIDXFTHRESH)

#define S_FW_IQ_CMD_FL1CNGCHMAP		20
#define M_FW_IQ_CMD_FL1CNGCHMAP		0xf
#define V_FW_IQ_CMD_FL1CNGCHMAP(x)	((x) << S_FW_IQ_CMD_FL1CNGCHMAP)
#define G_FW_IQ_CMD_FL1CNGCHMAP(x)	\
    (((x) >> S_FW_IQ_CMD_FL1CNGCHMAP) & M_FW_IQ_CMD_FL1CNGCHMAP)

#define S_FW_IQ_CMD_FL1CONGDROP		16
#define M_FW_IQ_CMD_FL1CONGDROP		0x1
#define V_FW_IQ_CMD_FL1CONGDROP(x)	((x) << S_FW_IQ_CMD_FL1CONGDROP)
#define G_FW_IQ_CMD_FL1CONGDROP(x)	\
    (((x) >> S_FW_IQ_CMD_FL1CONGDROP) & M_FW_IQ_CMD_FL1CONGDROP)
#define F_FW_IQ_CMD_FL1CONGDROP		V_FW_IQ_CMD_FL1CONGDROP(1U)

#define S_FW_IQ_CMD_FL1CACHELOCK	15
#define M_FW_IQ_CMD_FL1CACHELOCK	0x1
#define V_FW_IQ_CMD_FL1CACHELOCK(x)	((x) << S_FW_IQ_CMD_FL1CACHELOCK)
#define G_FW_IQ_CMD_FL1CACHELOCK(x)	\
    (((x) >> S_FW_IQ_CMD_FL1CACHELOCK) & M_FW_IQ_CMD_FL1CACHELOCK)
#define F_FW_IQ_CMD_FL1CACHELOCK	V_FW_IQ_CMD_FL1CACHELOCK(1U)

#define S_FW_IQ_CMD_FL1DBP		14
#define M_FW_IQ_CMD_FL1DBP		0x1
#define V_FW_IQ_CMD_FL1DBP(x)		((x) << S_FW_IQ_CMD_FL1DBP)
#define G_FW_IQ_CMD_FL1DBP(x)		\
    (((x) >> S_FW_IQ_CMD_FL1DBP) & M_FW_IQ_CMD_FL1DBP)
#define F_FW_IQ_CMD_FL1DBP		V_FW_IQ_CMD_FL1DBP(1U)

#define S_FW_IQ_CMD_FL1DATANS		13
#define M_FW_IQ_CMD_FL1DATANS		0x1
#define V_FW_IQ_CMD_FL1DATANS(x)	((x) << S_FW_IQ_CMD_FL1DATANS)
#define G_FW_IQ_CMD_FL1DATANS(x)	\
    (((x) >> S_FW_IQ_CMD_FL1DATANS) & M_FW_IQ_CMD_FL1DATANS)
#define F_FW_IQ_CMD_FL1DATANS		V_FW_IQ_CMD_FL1DATANS(1U)

#define S_FW_IQ_CMD_FL1DATARO		12
#define M_FW_IQ_CMD_FL1DATARO		0x1
#define V_FW_IQ_CMD_FL1DATARO(x)	((x) << S_FW_IQ_CMD_FL1DATARO)
#define G_FW_IQ_CMD_FL1DATARO(x)	\
    (((x) >> S_FW_IQ_CMD_FL1DATARO) & M_FW_IQ_CMD_FL1DATARO)
#define F_FW_IQ_CMD_FL1DATARO		V_FW_IQ_CMD_FL1DATARO(1U)

#define S_FW_IQ_CMD_FL1CONGCIF		11
#define M_FW_IQ_CMD_FL1CONGCIF		0x1
#define V_FW_IQ_CMD_FL1CONGCIF(x)	((x) << S_FW_IQ_CMD_FL1CONGCIF)
#define G_FW_IQ_CMD_FL1CONGCIF(x)	\
    (((x) >> S_FW_IQ_CMD_FL1CONGCIF) & M_FW_IQ_CMD_FL1CONGCIF)
#define F_FW_IQ_CMD_FL1CONGCIF		V_FW_IQ_CMD_FL1CONGCIF(1U)

#define S_FW_IQ_CMD_FL1ONCHIP		10
#define M_FW_IQ_CMD_FL1ONCHIP		0x1
#define V_FW_IQ_CMD_FL1ONCHIP(x)	((x) << S_FW_IQ_CMD_FL1ONCHIP)
#define G_FW_IQ_CMD_FL1ONCHIP(x)	\
    (((x) >> S_FW_IQ_CMD_FL1ONCHIP) & M_FW_IQ_CMD_FL1ONCHIP)
#define F_FW_IQ_CMD_FL1ONCHIP		V_FW_IQ_CMD_FL1ONCHIP(1U)

#define S_FW_IQ_CMD_FL1STATUSPGNS	9
#define M_FW_IQ_CMD_FL1STATUSPGNS	0x1
#define V_FW_IQ_CMD_FL1STATUSPGNS(x)	((x) << S_FW_IQ_CMD_FL1STATUSPGNS)
#define G_FW_IQ_CMD_FL1STATUSPGNS(x)	\
    (((x) >> S_FW_IQ_CMD_FL1STATUSPGNS) & M_FW_IQ_CMD_FL1STATUSPGNS)
#define F_FW_IQ_CMD_FL1STATUSPGNS	V_FW_IQ_CMD_FL1STATUSPGNS(1U)

#define S_FW_IQ_CMD_FL1STATUSPGRO	8
#define M_FW_IQ_CMD_FL1STATUSPGRO	0x1
#define V_FW_IQ_CMD_FL1STATUSPGRO(x)	((x) << S_FW_IQ_CMD_FL1STATUSPGRO)
#define G_FW_IQ_CMD_FL1STATUSPGRO(x)	\
    (((x) >> S_FW_IQ_CMD_FL1STATUSPGRO) & M_FW_IQ_CMD_FL1STATUSPGRO)
#define F_FW_IQ_CMD_FL1STATUSPGRO	V_FW_IQ_CMD_FL1STATUSPGRO(1U)

#define S_FW_IQ_CMD_FL1FETCHNS		7
#define M_FW_IQ_CMD_FL1FETCHNS		0x1
#define V_FW_IQ_CMD_FL1FETCHNS(x)	((x) << S_FW_IQ_CMD_FL1FETCHNS)
#define G_FW_IQ_CMD_FL1FETCHNS(x)	\
    (((x) >> S_FW_IQ_CMD_FL1FETCHNS) & M_FW_IQ_CMD_FL1FETCHNS)
#define F_FW_IQ_CMD_FL1FETCHNS		V_FW_IQ_CMD_FL1FETCHNS(1U)

#define S_FW_IQ_CMD_FL1FETCHRO		6
#define M_FW_IQ_CMD_FL1FETCHRO		0x1
#define V_FW_IQ_CMD_FL1FETCHRO(x)	((x) << S_FW_IQ_CMD_FL1FETCHRO)
#define G_FW_IQ_CMD_FL1FETCHRO(x)	\
    (((x) >> S_FW_IQ_CMD_FL1FETCHRO) & M_FW_IQ_CMD_FL1FETCHRO)
#define F_FW_IQ_CMD_FL1FETCHRO		V_FW_IQ_CMD_FL1FETCHRO(1U)

#define S_FW_IQ_CMD_FL1HOSTFCMODE	4
#define M_FW_IQ_CMD_FL1HOSTFCMODE	0x3
#define V_FW_IQ_CMD_FL1HOSTFCMODE(x)	((x) << S_FW_IQ_CMD_FL1HOSTFCMODE)
#define G_FW_IQ_CMD_FL1HOSTFCMODE(x)	\
    (((x) >> S_FW_IQ_CMD_FL1HOSTFCMODE) & M_FW_IQ_CMD_FL1HOSTFCMODE)

#define S_FW_IQ_CMD_FL1CPRIO		3
#define M_FW_IQ_CMD_FL1CPRIO		0x1
#define V_FW_IQ_CMD_FL1CPRIO(x)		((x) << S_FW_IQ_CMD_FL1CPRIO)
#define G_FW_IQ_CMD_FL1CPRIO(x)		\
    (((x) >> S_FW_IQ_CMD_FL1CPRIO) & M_FW_IQ_CMD_FL1CPRIO)
#define F_FW_IQ_CMD_FL1CPRIO		V_FW_IQ_CMD_FL1CPRIO(1U)

#define S_FW_IQ_CMD_FL1PADEN		2
#define M_FW_IQ_CMD_FL1PADEN		0x1
#define V_FW_IQ_CMD_FL1PADEN(x)		((x) << S_FW_IQ_CMD_FL1PADEN)
#define G_FW_IQ_CMD_FL1PADEN(x)		\
    (((x) >> S_FW_IQ_CMD_FL1PADEN) & M_FW_IQ_CMD_FL1PADEN)
#define F_FW_IQ_CMD_FL1PADEN		V_FW_IQ_CMD_FL1PADEN(1U)

#define S_FW_IQ_CMD_FL1PACKEN		1
#define M_FW_IQ_CMD_FL1PACKEN		0x1
#define V_FW_IQ_CMD_FL1PACKEN(x)	((x) << S_FW_IQ_CMD_FL1PACKEN)
#define G_FW_IQ_CMD_FL1PACKEN(x)	\
    (((x) >> S_FW_IQ_CMD_FL1PACKEN) & M_FW_IQ_CMD_FL1PACKEN)
#define F_FW_IQ_CMD_FL1PACKEN		V_FW_IQ_CMD_FL1PACKEN(1U)

#define S_FW_IQ_CMD_FL1CONGEN		0
#define M_FW_IQ_CMD_FL1CONGEN		0x1
#define V_FW_IQ_CMD_FL1CONGEN(x)	((x) << S_FW_IQ_CMD_FL1CONGEN)
#define G_FW_IQ_CMD_FL1CONGEN(x)	\
    (((x) >> S_FW_IQ_CMD_FL1CONGEN) & M_FW_IQ_CMD_FL1CONGEN)
#define F_FW_IQ_CMD_FL1CONGEN		V_FW_IQ_CMD_FL1CONGEN(1U)

#define S_FW_IQ_CMD_FL1DCAEN		15
#define M_FW_IQ_CMD_FL1DCAEN		0x1
#define V_FW_IQ_CMD_FL1DCAEN(x)		((x) << S_FW_IQ_CMD_FL1DCAEN)
#define G_FW_IQ_CMD_FL1DCAEN(x)		\
    (((x) >> S_FW_IQ_CMD_FL1DCAEN) & M_FW_IQ_CMD_FL1DCAEN)
#define F_FW_IQ_CMD_FL1DCAEN		V_FW_IQ_CMD_FL1DCAEN(1U)

#define S_FW_IQ_CMD_FL1DCACPU		10
#define M_FW_IQ_CMD_FL1DCACPU		0x1f
#define V_FW_IQ_CMD_FL1DCACPU(x)	((x) << S_FW_IQ_CMD_FL1DCACPU)
#define G_FW_IQ_CMD_FL1DCACPU(x)	\
    (((x) >> S_FW_IQ_CMD_FL1DCACPU) & M_FW_IQ_CMD_FL1DCACPU)

#define S_FW_IQ_CMD_FL1FBMIN		7
#define M_FW_IQ_CMD_FL1FBMIN		0x7
#define V_FW_IQ_CMD_FL1FBMIN(x)		((x) << S_FW_IQ_CMD_FL1FBMIN)
#define G_FW_IQ_CMD_FL1FBMIN(x)		\
    (((x) >> S_FW_IQ_CMD_FL1FBMIN) & M_FW_IQ_CMD_FL1FBMIN)

#define S_FW_IQ_CMD_FL1FBMAX		4
#define M_FW_IQ_CMD_FL1FBMAX		0x7
#define V_FW_IQ_CMD_FL1FBMAX(x)		((x) << S_FW_IQ_CMD_FL1FBMAX)
#define G_FW_IQ_CMD_FL1FBMAX(x)		\
    (((x) >> S_FW_IQ_CMD_FL1FBMAX) & M_FW_IQ_CMD_FL1FBMAX)

#define S_FW_IQ_CMD_FL1CIDXFTHRESHO	3
#define M_FW_IQ_CMD_FL1CIDXFTHRESHO	0x1
#define V_FW_IQ_CMD_FL1CIDXFTHRESHO(x)	((x) << S_FW_IQ_CMD_FL1CIDXFTHRESHO)
#define G_FW_IQ_CMD_FL1CIDXFTHRESHO(x)	\
    (((x) >> S_FW_IQ_CMD_FL1CIDXFTHRESHO) & M_FW_IQ_CMD_FL1CIDXFTHRESHO)
#define F_FW_IQ_CMD_FL1CIDXFTHRESHO	V_FW_IQ_CMD_FL1CIDXFTHRESHO(1U)

#define S_FW_IQ_CMD_FL1CIDXFTHRESH	0
#define M_FW_IQ_CMD_FL1CIDXFTHRESH	0x7
#define V_FW_IQ_CMD_FL1CIDXFTHRESH(x)	((x) << S_FW_IQ_CMD_FL1CIDXFTHRESH)
#define G_FW_IQ_CMD_FL1CIDXFTHRESH(x)	\
    (((x) >> S_FW_IQ_CMD_FL1CIDXFTHRESH) & M_FW_IQ_CMD_FL1CIDXFTHRESH)

struct fw_eq_mngt_cmd {
	__be32 op_to_vfn;
	__be32 alloc_to_len16;
	__be32 cmpliqid_eqid;
	__be32 physeqid_pkd;
	__be32 fetchszm_to_iqid;
	__be32 dcaen_to_eqsize;
	__be64 eqaddr;
};

#define S_FW_EQ_MNGT_CMD_PFN		8
#define M_FW_EQ_MNGT_CMD_PFN		0x7
#define V_FW_EQ_MNGT_CMD_PFN(x)		((x) << S_FW_EQ_MNGT_CMD_PFN)
#define G_FW_EQ_MNGT_CMD_PFN(x)		\
    (((x) >> S_FW_EQ_MNGT_CMD_PFN) & M_FW_EQ_MNGT_CMD_PFN)

#define S_FW_EQ_MNGT_CMD_VFN		0
#define M_FW_EQ_MNGT_CMD_VFN		0xff
#define V_FW_EQ_MNGT_CMD_VFN(x)		((x) << S_FW_EQ_MNGT_CMD_VFN)
#define G_FW_EQ_MNGT_CMD_VFN(x)		\
    (((x) >> S_FW_EQ_MNGT_CMD_VFN) & M_FW_EQ_MNGT_CMD_VFN)

#define S_FW_EQ_MNGT_CMD_ALLOC		31
#define M_FW_EQ_MNGT_CMD_ALLOC		0x1
#define V_FW_EQ_MNGT_CMD_ALLOC(x)	((x) << S_FW_EQ_MNGT_CMD_ALLOC)
#define G_FW_EQ_MNGT_CMD_ALLOC(x)	\
    (((x) >> S_FW_EQ_MNGT_CMD_ALLOC) & M_FW_EQ_MNGT_CMD_ALLOC)
#define F_FW_EQ_MNGT_CMD_ALLOC		V_FW_EQ_MNGT_CMD_ALLOC(1U)

#define S_FW_EQ_MNGT_CMD_FREE		30
#define M_FW_EQ_MNGT_CMD_FREE		0x1
#define V_FW_EQ_MNGT_CMD_FREE(x)	((x) << S_FW_EQ_MNGT_CMD_FREE)
#define G_FW_EQ_MNGT_CMD_FREE(x)	\
    (((x) >> S_FW_EQ_MNGT_CMD_FREE) & M_FW_EQ_MNGT_CMD_FREE)
#define F_FW_EQ_MNGT_CMD_FREE		V_FW_EQ_MNGT_CMD_FREE(1U)

#define S_FW_EQ_MNGT_CMD_MODIFY		29
#define M_FW_EQ_MNGT_CMD_MODIFY		0x1
#define V_FW_EQ_MNGT_CMD_MODIFY(x)	((x) << S_FW_EQ_MNGT_CMD_MODIFY)
#define G_FW_EQ_MNGT_CMD_MODIFY(x)	\
    (((x) >> S_FW_EQ_MNGT_CMD_MODIFY) & M_FW_EQ_MNGT_CMD_MODIFY)
#define F_FW_EQ_MNGT_CMD_MODIFY		V_FW_EQ_MNGT_CMD_MODIFY(1U)

#define S_FW_EQ_MNGT_CMD_EQSTART	28
#define M_FW_EQ_MNGT_CMD_EQSTART	0x1
#define V_FW_EQ_MNGT_CMD_EQSTART(x)	((x) << S_FW_EQ_MNGT_CMD_EQSTART)
#define G_FW_EQ_MNGT_CMD_EQSTART(x)	\
    (((x) >> S_FW_EQ_MNGT_CMD_EQSTART) & M_FW_EQ_MNGT_CMD_EQSTART)
#define F_FW_EQ_MNGT_CMD_EQSTART	V_FW_EQ_MNGT_CMD_EQSTART(1U)

#define S_FW_EQ_MNGT_CMD_EQSTOP		27
#define M_FW_EQ_MNGT_CMD_EQSTOP		0x1
#define V_FW_EQ_MNGT_CMD_EQSTOP(x)	((x) << S_FW_EQ_MNGT_CMD_EQSTOP)
#define G_FW_EQ_MNGT_CMD_EQSTOP(x)	\
    (((x) >> S_FW_EQ_MNGT_CMD_EQSTOP) & M_FW_EQ_MNGT_CMD_EQSTOP)
#define F_FW_EQ_MNGT_CMD_EQSTOP		V_FW_EQ_MNGT_CMD_EQSTOP(1U)

#define S_FW_EQ_MNGT_CMD_CMPLIQID	20
#define M_FW_EQ_MNGT_CMD_CMPLIQID	0xfff
#define V_FW_EQ_MNGT_CMD_CMPLIQID(x)	((x) << S_FW_EQ_MNGT_CMD_CMPLIQID)
#define G_FW_EQ_MNGT_CMD_CMPLIQID(x)	\
    (((x) >> S_FW_EQ_MNGT_CMD_CMPLIQID) & M_FW_EQ_MNGT_CMD_CMPLIQID)

#define S_FW_EQ_MNGT_CMD_EQID		0
#define M_FW_EQ_MNGT_CMD_EQID		0xfffff
#define V_FW_EQ_MNGT_CMD_EQID(x)	((x) << S_FW_EQ_MNGT_CMD_EQID)
#define G_FW_EQ_MNGT_CMD_EQID(x)	\
    (((x) >> S_FW_EQ_MNGT_CMD_EQID) & M_FW_EQ_MNGT_CMD_EQID)

#define S_FW_EQ_MNGT_CMD_PHYSEQID	0
#define M_FW_EQ_MNGT_CMD_PHYSEQID	0xfffff
#define V_FW_EQ_MNGT_CMD_PHYSEQID(x)	((x) << S_FW_EQ_MNGT_CMD_PHYSEQID)
#define G_FW_EQ_MNGT_CMD_PHYSEQID(x)	\
    (((x) >> S_FW_EQ_MNGT_CMD_PHYSEQID) & M_FW_EQ_MNGT_CMD_PHYSEQID)

#define S_FW_EQ_MNGT_CMD_FETCHSZM	26
#define M_FW_EQ_MNGT_CMD_FETCHSZM	0x1
#define V_FW_EQ_MNGT_CMD_FETCHSZM(x)	((x) << S_FW_EQ_MNGT_CMD_FETCHSZM)
#define G_FW_EQ_MNGT_CMD_FETCHSZM(x)	\
    (((x) >> S_FW_EQ_MNGT_CMD_FETCHSZM) & M_FW_EQ_MNGT_CMD_FETCHSZM)
#define F_FW_EQ_MNGT_CMD_FETCHSZM	V_FW_EQ_MNGT_CMD_FETCHSZM(1U)

#define S_FW_EQ_MNGT_CMD_STATUSPGNS	25
#define M_FW_EQ_MNGT_CMD_STATUSPGNS	0x1
#define V_FW_EQ_MNGT_CMD_STATUSPGNS(x)	((x) << S_FW_EQ_MNGT_CMD_STATUSPGNS)
#define G_FW_EQ_MNGT_CMD_STATUSPGNS(x)	\
    (((x) >> S_FW_EQ_MNGT_CMD_STATUSPGNS) & M_FW_EQ_MNGT_CMD_STATUSPGNS)
#define F_FW_EQ_MNGT_CMD_STATUSPGNS	V_FW_EQ_MNGT_CMD_STATUSPGNS(1U)

#define S_FW_EQ_MNGT_CMD_STATUSPGRO	24
#define M_FW_EQ_MNGT_CMD_STATUSPGRO	0x1
#define V_FW_EQ_MNGT_CMD_STATUSPGRO(x)	((x) << S_FW_EQ_MNGT_CMD_STATUSPGRO)
#define G_FW_EQ_MNGT_CMD_STATUSPGRO(x)	\
    (((x) >> S_FW_EQ_MNGT_CMD_STATUSPGRO) & M_FW_EQ_MNGT_CMD_STATUSPGRO)
#define F_FW_EQ_MNGT_CMD_STATUSPGRO	V_FW_EQ_MNGT_CMD_STATUSPGRO(1U)

#define S_FW_EQ_MNGT_CMD_FETCHNS	23
#define M_FW_EQ_MNGT_CMD_FETCHNS	0x1
#define V_FW_EQ_MNGT_CMD_FETCHNS(x)	((x) << S_FW_EQ_MNGT_CMD_FETCHNS)
#define G_FW_EQ_MNGT_CMD_FETCHNS(x)	\
    (((x) >> S_FW_EQ_MNGT_CMD_FETCHNS) & M_FW_EQ_MNGT_CMD_FETCHNS)
#define F_FW_EQ_MNGT_CMD_FETCHNS	V_FW_EQ_MNGT_CMD_FETCHNS(1U)

#define S_FW_EQ_MNGT_CMD_FETCHRO	22
#define M_FW_EQ_MNGT_CMD_FETCHRO	0x1
#define V_FW_EQ_MNGT_CMD_FETCHRO(x)	((x) << S_FW_EQ_MNGT_CMD_FETCHRO)
#define G_FW_EQ_MNGT_CMD_FETCHRO(x)	\
    (((x) >> S_FW_EQ_MNGT_CMD_FETCHRO) & M_FW_EQ_MNGT_CMD_FETCHRO)
#define F_FW_EQ_MNGT_CMD_FETCHRO	V_FW_EQ_MNGT_CMD_FETCHRO(1U)

#define S_FW_EQ_MNGT_CMD_HOSTFCMODE	20
#define M_FW_EQ_MNGT_CMD_HOSTFCMODE	0x3
#define V_FW_EQ_MNGT_CMD_HOSTFCMODE(x)	((x) << S_FW_EQ_MNGT_CMD_HOSTFCMODE)
#define G_FW_EQ_MNGT_CMD_HOSTFCMODE(x)	\
    (((x) >> S_FW_EQ_MNGT_CMD_HOSTFCMODE) & M_FW_EQ_MNGT_CMD_HOSTFCMODE)

#define S_FW_EQ_MNGT_CMD_CPRIO		19
#define M_FW_EQ_MNGT_CMD_CPRIO		0x1
#define V_FW_EQ_MNGT_CMD_CPRIO(x)	((x) << S_FW_EQ_MNGT_CMD_CPRIO)
#define G_FW_EQ_MNGT_CMD_CPRIO(x)	\
    (((x) >> S_FW_EQ_MNGT_CMD_CPRIO) & M_FW_EQ_MNGT_CMD_CPRIO)
#define F_FW_EQ_MNGT_CMD_CPRIO		V_FW_EQ_MNGT_CMD_CPRIO(1U)

#define S_FW_EQ_MNGT_CMD_ONCHIP		18
#define M_FW_EQ_MNGT_CMD_ONCHIP		0x1
#define V_FW_EQ_MNGT_CMD_ONCHIP(x)	((x) << S_FW_EQ_MNGT_CMD_ONCHIP)
#define G_FW_EQ_MNGT_CMD_ONCHIP(x)	\
    (((x) >> S_FW_EQ_MNGT_CMD_ONCHIP) & M_FW_EQ_MNGT_CMD_ONCHIP)
#define F_FW_EQ_MNGT_CMD_ONCHIP		V_FW_EQ_MNGT_CMD_ONCHIP(1U)

#define S_FW_EQ_MNGT_CMD_PCIECHN	16
#define M_FW_EQ_MNGT_CMD_PCIECHN	0x3
#define V_FW_EQ_MNGT_CMD_PCIECHN(x)	((x) << S_FW_EQ_MNGT_CMD_PCIECHN)
#define G_FW_EQ_MNGT_CMD_PCIECHN(x)	\
    (((x) >> S_FW_EQ_MNGT_CMD_PCIECHN) & M_FW_EQ_MNGT_CMD_PCIECHN)

#define S_FW_EQ_MNGT_CMD_IQID		0
#define M_FW_EQ_MNGT_CMD_IQID		0xffff
#define V_FW_EQ_MNGT_CMD_IQID(x)	((x) << S_FW_EQ_MNGT_CMD_IQID)
#define G_FW_EQ_MNGT_CMD_IQID(x)	\
    (((x) >> S_FW_EQ_MNGT_CMD_IQID) & M_FW_EQ_MNGT_CMD_IQID)

#define S_FW_EQ_MNGT_CMD_DCAEN		31
#define M_FW_EQ_MNGT_CMD_DCAEN		0x1
#define V_FW_EQ_MNGT_CMD_DCAEN(x)	((x) << S_FW_EQ_MNGT_CMD_DCAEN)
#define G_FW_EQ_MNGT_CMD_DCAEN(x)	\
    (((x) >> S_FW_EQ_MNGT_CMD_DCAEN) & M_FW_EQ_MNGT_CMD_DCAEN)
#define F_FW_EQ_MNGT_CMD_DCAEN		V_FW_EQ_MNGT_CMD_DCAEN(1U)

#define S_FW_EQ_MNGT_CMD_DCACPU		26
#define M_FW_EQ_MNGT_CMD_DCACPU		0x1f
#define V_FW_EQ_MNGT_CMD_DCACPU(x)	((x) << S_FW_EQ_MNGT_CMD_DCACPU)
#define G_FW_EQ_MNGT_CMD_DCACPU(x)	\
    (((x) >> S_FW_EQ_MNGT_CMD_DCACPU) & M_FW_EQ_MNGT_CMD_DCACPU)

#define S_FW_EQ_MNGT_CMD_FBMIN		23
#define M_FW_EQ_MNGT_CMD_FBMIN		0x7
#define V_FW_EQ_MNGT_CMD_FBMIN(x)	((x) << S_FW_EQ_MNGT_CMD_FBMIN)
#define G_FW_EQ_MNGT_CMD_FBMIN(x)	\
    (((x) >> S_FW_EQ_MNGT_CMD_FBMIN) & M_FW_EQ_MNGT_CMD_FBMIN)

#define S_FW_EQ_MNGT_CMD_FBMAX		20
#define M_FW_EQ_MNGT_CMD_FBMAX		0x7
#define V_FW_EQ_MNGT_CMD_FBMAX(x)	((x) << S_FW_EQ_MNGT_CMD_FBMAX)
#define G_FW_EQ_MNGT_CMD_FBMAX(x)	\
    (((x) >> S_FW_EQ_MNGT_CMD_FBMAX) & M_FW_EQ_MNGT_CMD_FBMAX)

#define S_FW_EQ_MNGT_CMD_CIDXFTHRESHO	19
#define M_FW_EQ_MNGT_CMD_CIDXFTHRESHO	0x1
#define V_FW_EQ_MNGT_CMD_CIDXFTHRESHO(x) \
    ((x) << S_FW_EQ_MNGT_CMD_CIDXFTHRESHO)
#define G_FW_EQ_MNGT_CMD_CIDXFTHRESHO(x) \
    (((x) >> S_FW_EQ_MNGT_CMD_CIDXFTHRESHO) & M_FW_EQ_MNGT_CMD_CIDXFTHRESHO)
#define F_FW_EQ_MNGT_CMD_CIDXFTHRESHO	V_FW_EQ_MNGT_CMD_CIDXFTHRESHO(1U)

#define S_FW_EQ_MNGT_CMD_CIDXFTHRESH	16
#define M_FW_EQ_MNGT_CMD_CIDXFTHRESH	0x7
#define V_FW_EQ_MNGT_CMD_CIDXFTHRESH(x)	((x) << S_FW_EQ_MNGT_CMD_CIDXFTHRESH)
#define G_FW_EQ_MNGT_CMD_CIDXFTHRESH(x)	\
    (((x) >> S_FW_EQ_MNGT_CMD_CIDXFTHRESH) & M_FW_EQ_MNGT_CMD_CIDXFTHRESH)

#define S_FW_EQ_MNGT_CMD_EQSIZE		0
#define M_FW_EQ_MNGT_CMD_EQSIZE		0xffff
#define V_FW_EQ_MNGT_CMD_EQSIZE(x)	((x) << S_FW_EQ_MNGT_CMD_EQSIZE)
#define G_FW_EQ_MNGT_CMD_EQSIZE(x)	\
    (((x) >> S_FW_EQ_MNGT_CMD_EQSIZE) & M_FW_EQ_MNGT_CMD_EQSIZE)

struct fw_eq_eth_cmd {
	__be32 op_to_vfn;
	__be32 alloc_to_len16;
	__be32 eqid_pkd;
	__be32 physeqid_pkd;
	__be32 fetchszm_to_iqid;
	__be32 dcaen_to_eqsize;
	__be64 eqaddr;
	__be32 autoequiqe_to_viid;
	__be32 r8_lo;
	__be64 r9;
};

#define S_FW_EQ_ETH_CMD_PFN		8
#define M_FW_EQ_ETH_CMD_PFN		0x7
#define V_FW_EQ_ETH_CMD_PFN(x)		((x) << S_FW_EQ_ETH_CMD_PFN)
#define G_FW_EQ_ETH_CMD_PFN(x)		\
    (((x) >> S_FW_EQ_ETH_CMD_PFN) & M_FW_EQ_ETH_CMD_PFN)

#define S_FW_EQ_ETH_CMD_VFN		0
#define M_FW_EQ_ETH_CMD_VFN		0xff
#define V_FW_EQ_ETH_CMD_VFN(x)		((x) << S_FW_EQ_ETH_CMD_VFN)
#define G_FW_EQ_ETH_CMD_VFN(x)		\
    (((x) >> S_FW_EQ_ETH_CMD_VFN) & M_FW_EQ_ETH_CMD_VFN)

#define S_FW_EQ_ETH_CMD_ALLOC		31
#define M_FW_EQ_ETH_CMD_ALLOC		0x1
#define V_FW_EQ_ETH_CMD_ALLOC(x)	((x) << S_FW_EQ_ETH_CMD_ALLOC)
#define G_FW_EQ_ETH_CMD_ALLOC(x)	\
    (((x) >> S_FW_EQ_ETH_CMD_ALLOC) & M_FW_EQ_ETH_CMD_ALLOC)
#define F_FW_EQ_ETH_CMD_ALLOC		V_FW_EQ_ETH_CMD_ALLOC(1U)

#define S_FW_EQ_ETH_CMD_FREE		30
#define M_FW_EQ_ETH_CMD_FREE		0x1
#define V_FW_EQ_ETH_CMD_FREE(x)		((x) << S_FW_EQ_ETH_CMD_FREE)
#define G_FW_EQ_ETH_CMD_FREE(x)		\
    (((x) >> S_FW_EQ_ETH_CMD_FREE) & M_FW_EQ_ETH_CMD_FREE)
#define F_FW_EQ_ETH_CMD_FREE		V_FW_EQ_ETH_CMD_FREE(1U)

#define S_FW_EQ_ETH_CMD_MODIFY		29
#define M_FW_EQ_ETH_CMD_MODIFY		0x1
#define V_FW_EQ_ETH_CMD_MODIFY(x)	((x) << S_FW_EQ_ETH_CMD_MODIFY)
#define G_FW_EQ_ETH_CMD_MODIFY(x)	\
    (((x) >> S_FW_EQ_ETH_CMD_MODIFY) & M_FW_EQ_ETH_CMD_MODIFY)
#define F_FW_EQ_ETH_CMD_MODIFY		V_FW_EQ_ETH_CMD_MODIFY(1U)

#define S_FW_EQ_ETH_CMD_EQSTART		28
#define M_FW_EQ_ETH_CMD_EQSTART		0x1
#define V_FW_EQ_ETH_CMD_EQSTART(x)	((x) << S_FW_EQ_ETH_CMD_EQSTART)
#define G_FW_EQ_ETH_CMD_EQSTART(x)	\
    (((x) >> S_FW_EQ_ETH_CMD_EQSTART) & M_FW_EQ_ETH_CMD_EQSTART)
#define F_FW_EQ_ETH_CMD_EQSTART		V_FW_EQ_ETH_CMD_EQSTART(1U)

#define S_FW_EQ_ETH_CMD_EQSTOP		27
#define M_FW_EQ_ETH_CMD_EQSTOP		0x1
#define V_FW_EQ_ETH_CMD_EQSTOP(x)	((x) << S_FW_EQ_ETH_CMD_EQSTOP)
#define G_FW_EQ_ETH_CMD_EQSTOP(x)	\
    (((x) >> S_FW_EQ_ETH_CMD_EQSTOP) & M_FW_EQ_ETH_CMD_EQSTOP)
#define F_FW_EQ_ETH_CMD_EQSTOP		V_FW_EQ_ETH_CMD_EQSTOP(1U)

#define S_FW_EQ_ETH_CMD_EQID		0
#define M_FW_EQ_ETH_CMD_EQID		0xfffff
#define V_FW_EQ_ETH_CMD_EQID(x)		((x) << S_FW_EQ_ETH_CMD_EQID)
#define G_FW_EQ_ETH_CMD_EQID(x)		\
    (((x) >> S_FW_EQ_ETH_CMD_EQID) & M_FW_EQ_ETH_CMD_EQID)

#define S_FW_EQ_ETH_CMD_PHYSEQID	0
#define M_FW_EQ_ETH_CMD_PHYSEQID	0xfffff
#define V_FW_EQ_ETH_CMD_PHYSEQID(x)	((x) << S_FW_EQ_ETH_CMD_PHYSEQID)
#define G_FW_EQ_ETH_CMD_PHYSEQID(x)	\
    (((x) >> S_FW_EQ_ETH_CMD_PHYSEQID) & M_FW_EQ_ETH_CMD_PHYSEQID)

#define S_FW_EQ_ETH_CMD_FETCHSZM	26
#define M_FW_EQ_ETH_CMD_FETCHSZM	0x1
#define V_FW_EQ_ETH_CMD_FETCHSZM(x)	((x) << S_FW_EQ_ETH_CMD_FETCHSZM)
#define G_FW_EQ_ETH_CMD_FETCHSZM(x)	\
    (((x) >> S_FW_EQ_ETH_CMD_FETCHSZM) & M_FW_EQ_ETH_CMD_FETCHSZM)
#define F_FW_EQ_ETH_CMD_FETCHSZM	V_FW_EQ_ETH_CMD_FETCHSZM(1U)

#define S_FW_EQ_ETH_CMD_STATUSPGNS	25
#define M_FW_EQ_ETH_CMD_STATUSPGNS	0x1
#define V_FW_EQ_ETH_CMD_STATUSPGNS(x)	((x) << S_FW_EQ_ETH_CMD_STATUSPGNS)
#define G_FW_EQ_ETH_CMD_STATUSPGNS(x)	\
    (((x) >> S_FW_EQ_ETH_CMD_STATUSPGNS) & M_FW_EQ_ETH_CMD_STATUSPGNS)
#define F_FW_EQ_ETH_CMD_STATUSPGNS	V_FW_EQ_ETH_CMD_STATUSPGNS(1U)

#define S_FW_EQ_ETH_CMD_STATUSPGRO	24
#define M_FW_EQ_ETH_CMD_STATUSPGRO	0x1
#define V_FW_EQ_ETH_CMD_STATUSPGRO(x)	((x) << S_FW_EQ_ETH_CMD_STATUSPGRO)
#define G_FW_EQ_ETH_CMD_STATUSPGRO(x)	\
    (((x) >> S_FW_EQ_ETH_CMD_STATUSPGRO) & M_FW_EQ_ETH_CMD_STATUSPGRO)
#define F_FW_EQ_ETH_CMD_STATUSPGRO	V_FW_EQ_ETH_CMD_STATUSPGRO(1U)

#define S_FW_EQ_ETH_CMD_FETCHNS		23
#define M_FW_EQ_ETH_CMD_FETCHNS		0x1
#define V_FW_EQ_ETH_CMD_FETCHNS(x)	((x) << S_FW_EQ_ETH_CMD_FETCHNS)
#define G_FW_EQ_ETH_CMD_FETCHNS(x)	\
    (((x) >> S_FW_EQ_ETH_CMD_FETCHNS) & M_FW_EQ_ETH_CMD_FETCHNS)
#define F_FW_EQ_ETH_CMD_FETCHNS		V_FW_EQ_ETH_CMD_FETCHNS(1U)

#define S_FW_EQ_ETH_CMD_FETCHRO		22
#define M_FW_EQ_ETH_CMD_FETCHRO		0x1
#define V_FW_EQ_ETH_CMD_FETCHRO(x)	((x) << S_FW_EQ_ETH_CMD_FETCHRO)
#define G_FW_EQ_ETH_CMD_FETCHRO(x)	\
    (((x) >> S_FW_EQ_ETH_CMD_FETCHRO) & M_FW_EQ_ETH_CMD_FETCHRO)
#define F_FW_EQ_ETH_CMD_FETCHRO		V_FW_EQ_ETH_CMD_FETCHRO(1U)

#define S_FW_EQ_ETH_CMD_HOSTFCMODE	20
#define M_FW_EQ_ETH_CMD_HOSTFCMODE	0x3
#define V_FW_EQ_ETH_CMD_HOSTFCMODE(x)	((x) << S_FW_EQ_ETH_CMD_HOSTFCMODE)
#define G_FW_EQ_ETH_CMD_HOSTFCMODE(x)	\
    (((x) >> S_FW_EQ_ETH_CMD_HOSTFCMODE) & M_FW_EQ_ETH_CMD_HOSTFCMODE)

#define S_FW_EQ_ETH_CMD_CPRIO		19
#define M_FW_EQ_ETH_CMD_CPRIO		0x1
#define V_FW_EQ_ETH_CMD_CPRIO(x)	((x) << S_FW_EQ_ETH_CMD_CPRIO)
#define G_FW_EQ_ETH_CMD_CPRIO(x)	\
    (((x) >> S_FW_EQ_ETH_CMD_CPRIO) & M_FW_EQ_ETH_CMD_CPRIO)
#define F_FW_EQ_ETH_CMD_CPRIO		V_FW_EQ_ETH_CMD_CPRIO(1U)

#define S_FW_EQ_ETH_CMD_ONCHIP		18
#define M_FW_EQ_ETH_CMD_ONCHIP		0x1
#define V_FW_EQ_ETH_CMD_ONCHIP(x)	((x) << S_FW_EQ_ETH_CMD_ONCHIP)
#define G_FW_EQ_ETH_CMD_ONCHIP(x)	\
    (((x) >> S_FW_EQ_ETH_CMD_ONCHIP) & M_FW_EQ_ETH_CMD_ONCHIP)
#define F_FW_EQ_ETH_CMD_ONCHIP		V_FW_EQ_ETH_CMD_ONCHIP(1U)

#define S_FW_EQ_ETH_CMD_PCIECHN		16
#define M_FW_EQ_ETH_CMD_PCIECHN		0x3
#define V_FW_EQ_ETH_CMD_PCIECHN(x)	((x) << S_FW_EQ_ETH_CMD_PCIECHN)
#define G_FW_EQ_ETH_CMD_PCIECHN(x)	\
    (((x) >> S_FW_EQ_ETH_CMD_PCIECHN) & M_FW_EQ_ETH_CMD_PCIECHN)

#define S_FW_EQ_ETH_CMD_IQID		0
#define M_FW_EQ_ETH_CMD_IQID		0xffff
#define V_FW_EQ_ETH_CMD_IQID(x)		((x) << S_FW_EQ_ETH_CMD_IQID)
#define G_FW_EQ_ETH_CMD_IQID(x)		\
    (((x) >> S_FW_EQ_ETH_CMD_IQID) & M_FW_EQ_ETH_CMD_IQID)

#define S_FW_EQ_ETH_CMD_DCAEN		31
#define M_FW_EQ_ETH_CMD_DCAEN		0x1
#define V_FW_EQ_ETH_CMD_DCAEN(x)	((x) << S_FW_EQ_ETH_CMD_DCAEN)
#define G_FW_EQ_ETH_CMD_DCAEN(x)	\
    (((x) >> S_FW_EQ_ETH_CMD_DCAEN) & M_FW_EQ_ETH_CMD_DCAEN)
#define F_FW_EQ_ETH_CMD_DCAEN		V_FW_EQ_ETH_CMD_DCAEN(1U)

#define S_FW_EQ_ETH_CMD_DCACPU		26
#define M_FW_EQ_ETH_CMD_DCACPU		0x1f
#define V_FW_EQ_ETH_CMD_DCACPU(x)	((x) << S_FW_EQ_ETH_CMD_DCACPU)
#define G_FW_EQ_ETH_CMD_DCACPU(x)	\
    (((x) >> S_FW_EQ_ETH_CMD_DCACPU) & M_FW_EQ_ETH_CMD_DCACPU)

#define S_FW_EQ_ETH_CMD_FBMIN		23
#define M_FW_EQ_ETH_CMD_FBMIN		0x7
#define V_FW_EQ_ETH_CMD_FBMIN(x)	((x) << S_FW_EQ_ETH_CMD_FBMIN)
#define G_FW_EQ_ETH_CMD_FBMIN(x)	\
    (((x) >> S_FW_EQ_ETH_CMD_FBMIN) & M_FW_EQ_ETH_CMD_FBMIN)

#define S_FW_EQ_ETH_CMD_FBMAX		20
#define M_FW_EQ_ETH_CMD_FBMAX		0x7
#define V_FW_EQ_ETH_CMD_FBMAX(x)	((x) << S_FW_EQ_ETH_CMD_FBMAX)
#define G_FW_EQ_ETH_CMD_FBMAX(x)	\
    (((x) >> S_FW_EQ_ETH_CMD_FBMAX) & M_FW_EQ_ETH_CMD_FBMAX)

#define S_FW_EQ_ETH_CMD_CIDXFTHRESHO	19
#define M_FW_EQ_ETH_CMD_CIDXFTHRESHO	0x1
#define V_FW_EQ_ETH_CMD_CIDXFTHRESHO(x)	((x) << S_FW_EQ_ETH_CMD_CIDXFTHRESHO)
#define G_FW_EQ_ETH_CMD_CIDXFTHRESHO(x)	\
    (((x) >> S_FW_EQ_ETH_CMD_CIDXFTHRESHO) & M_FW_EQ_ETH_CMD_CIDXFTHRESHO)
#define F_FW_EQ_ETH_CMD_CIDXFTHRESHO	V_FW_EQ_ETH_CMD_CIDXFTHRESHO(1U)

#define S_FW_EQ_ETH_CMD_CIDXFTHRESH	16
#define M_FW_EQ_ETH_CMD_CIDXFTHRESH	0x7
#define V_FW_EQ_ETH_CMD_CIDXFTHRESH(x)	((x) << S_FW_EQ_ETH_CMD_CIDXFTHRESH)
#define G_FW_EQ_ETH_CMD_CIDXFTHRESH(x)	\
    (((x) >> S_FW_EQ_ETH_CMD_CIDXFTHRESH) & M_FW_EQ_ETH_CMD_CIDXFTHRESH)

#define S_FW_EQ_ETH_CMD_EQSIZE		0
#define M_FW_EQ_ETH_CMD_EQSIZE		0xffff
#define V_FW_EQ_ETH_CMD_EQSIZE(x)	((x) << S_FW_EQ_ETH_CMD_EQSIZE)
#define G_FW_EQ_ETH_CMD_EQSIZE(x)	\
    (((x) >> S_FW_EQ_ETH_CMD_EQSIZE) & M_FW_EQ_ETH_CMD_EQSIZE)

#define S_FW_EQ_ETH_CMD_AUTOEQUIQE	31
#define M_FW_EQ_ETH_CMD_AUTOEQUIQE	0x1
#define V_FW_EQ_ETH_CMD_AUTOEQUIQE(x)	((x) << S_FW_EQ_ETH_CMD_AUTOEQUIQE)
#define G_FW_EQ_ETH_CMD_AUTOEQUIQE(x)	\
    (((x) >> S_FW_EQ_ETH_CMD_AUTOEQUIQE) & M_FW_EQ_ETH_CMD_AUTOEQUIQE)
#define F_FW_EQ_ETH_CMD_AUTOEQUIQE	V_FW_EQ_ETH_CMD_AUTOEQUIQE(1U)

#define S_FW_EQ_ETH_CMD_AUTOEQUEQE	30
#define M_FW_EQ_ETH_CMD_AUTOEQUEQE	0x1
#define V_FW_EQ_ETH_CMD_AUTOEQUEQE(x)	((x) << S_FW_EQ_ETH_CMD_AUTOEQUEQE)
#define G_FW_EQ_ETH_CMD_AUTOEQUEQE(x)	\
    (((x) >> S_FW_EQ_ETH_CMD_AUTOEQUEQE) & M_FW_EQ_ETH_CMD_AUTOEQUEQE)
#define F_FW_EQ_ETH_CMD_AUTOEQUEQE	V_FW_EQ_ETH_CMD_AUTOEQUEQE(1U)

#define S_FW_EQ_ETH_CMD_VIID		16
#define M_FW_EQ_ETH_CMD_VIID		0xfff
#define V_FW_EQ_ETH_CMD_VIID(x)		((x) << S_FW_EQ_ETH_CMD_VIID)
#define G_FW_EQ_ETH_CMD_VIID(x)		\
    (((x) >> S_FW_EQ_ETH_CMD_VIID) & M_FW_EQ_ETH_CMD_VIID)

struct fw_eq_ctrl_cmd {
	__be32 op_to_vfn;
	__be32 alloc_to_len16;
	__be32 cmpliqid_eqid;
	__be32 physeqid_pkd;
	__be32 fetchszm_to_iqid;
	__be32 dcaen_to_eqsize;
	__be64 eqaddr;
};

#define S_FW_EQ_CTRL_CMD_PFN		8
#define M_FW_EQ_CTRL_CMD_PFN		0x7
#define V_FW_EQ_CTRL_CMD_PFN(x)		((x) << S_FW_EQ_CTRL_CMD_PFN)
#define G_FW_EQ_CTRL_CMD_PFN(x)		\
    (((x) >> S_FW_EQ_CTRL_CMD_PFN) & M_FW_EQ_CTRL_CMD_PFN)

#define S_FW_EQ_CTRL_CMD_VFN		0
#define M_FW_EQ_CTRL_CMD_VFN		0xff
#define V_FW_EQ_CTRL_CMD_VFN(x)		((x) << S_FW_EQ_CTRL_CMD_VFN)
#define G_FW_EQ_CTRL_CMD_VFN(x)		\
    (((x) >> S_FW_EQ_CTRL_CMD_VFN) & M_FW_EQ_CTRL_CMD_VFN)

#define S_FW_EQ_CTRL_CMD_ALLOC		31
#define M_FW_EQ_CTRL_CMD_ALLOC		0x1
#define V_FW_EQ_CTRL_CMD_ALLOC(x)	((x) << S_FW_EQ_CTRL_CMD_ALLOC)
#define G_FW_EQ_CTRL_CMD_ALLOC(x)	\
    (((x) >> S_FW_EQ_CTRL_CMD_ALLOC) & M_FW_EQ_CTRL_CMD_ALLOC)
#define F_FW_EQ_CTRL_CMD_ALLOC		V_FW_EQ_CTRL_CMD_ALLOC(1U)

#define S_FW_EQ_CTRL_CMD_FREE		30
#define M_FW_EQ_CTRL_CMD_FREE		0x1
#define V_FW_EQ_CTRL_CMD_FREE(x)	((x) << S_FW_EQ_CTRL_CMD_FREE)
#define G_FW_EQ_CTRL_CMD_FREE(x)	\
    (((x) >> S_FW_EQ_CTRL_CMD_FREE) & M_FW_EQ_CTRL_CMD_FREE)
#define F_FW_EQ_CTRL_CMD_FREE		V_FW_EQ_CTRL_CMD_FREE(1U)

#define S_FW_EQ_CTRL_CMD_MODIFY		29
#define M_FW_EQ_CTRL_CMD_MODIFY		0x1
#define V_FW_EQ_CTRL_CMD_MODIFY(x)	((x) << S_FW_EQ_CTRL_CMD_MODIFY)
#define G_FW_EQ_CTRL_CMD_MODIFY(x)	\
    (((x) >> S_FW_EQ_CTRL_CMD_MODIFY) & M_FW_EQ_CTRL_CMD_MODIFY)
#define F_FW_EQ_CTRL_CMD_MODIFY		V_FW_EQ_CTRL_CMD_MODIFY(1U)

#define S_FW_EQ_CTRL_CMD_EQSTART	28
#define M_FW_EQ_CTRL_CMD_EQSTART	0x1
#define V_FW_EQ_CTRL_CMD_EQSTART(x)	((x) << S_FW_EQ_CTRL_CMD_EQSTART)
#define G_FW_EQ_CTRL_CMD_EQSTART(x)	\
    (((x) >> S_FW_EQ_CTRL_CMD_EQSTART) & M_FW_EQ_CTRL_CMD_EQSTART)
#define F_FW_EQ_CTRL_CMD_EQSTART	V_FW_EQ_CTRL_CMD_EQSTART(1U)

#define S_FW_EQ_CTRL_CMD_EQSTOP		27
#define M_FW_EQ_CTRL_CMD_EQSTOP		0x1
#define V_FW_EQ_CTRL_CMD_EQSTOP(x)	((x) << S_FW_EQ_CTRL_CMD_EQSTOP)
#define G_FW_EQ_CTRL_CMD_EQSTOP(x)	\
    (((x) >> S_FW_EQ_CTRL_CMD_EQSTOP) & M_FW_EQ_CTRL_CMD_EQSTOP)
#define F_FW_EQ_CTRL_CMD_EQSTOP		V_FW_EQ_CTRL_CMD_EQSTOP(1U)

#define S_FW_EQ_CTRL_CMD_CMPLIQID	20
#define M_FW_EQ_CTRL_CMD_CMPLIQID	0xfff
#define V_FW_EQ_CTRL_CMD_CMPLIQID(x)	((x) << S_FW_EQ_CTRL_CMD_CMPLIQID)
#define G_FW_EQ_CTRL_CMD_CMPLIQID(x)	\
    (((x) >> S_FW_EQ_CTRL_CMD_CMPLIQID) & M_FW_EQ_CTRL_CMD_CMPLIQID)

#define S_FW_EQ_CTRL_CMD_EQID		0
#define M_FW_EQ_CTRL_CMD_EQID		0xfffff
#define V_FW_EQ_CTRL_CMD_EQID(x)	((x) << S_FW_EQ_CTRL_CMD_EQID)
#define G_FW_EQ_CTRL_CMD_EQID(x)	\
    (((x) >> S_FW_EQ_CTRL_CMD_EQID) & M_FW_EQ_CTRL_CMD_EQID)

#define S_FW_EQ_CTRL_CMD_PHYSEQID	0
#define M_FW_EQ_CTRL_CMD_PHYSEQID	0xfffff
#define V_FW_EQ_CTRL_CMD_PHYSEQID(x)	((x) << S_FW_EQ_CTRL_CMD_PHYSEQID)
#define G_FW_EQ_CTRL_CMD_PHYSEQID(x)	\
    (((x) >> S_FW_EQ_CTRL_CMD_PHYSEQID) & M_FW_EQ_CTRL_CMD_PHYSEQID)

#define S_FW_EQ_CTRL_CMD_FETCHSZM	26
#define M_FW_EQ_CTRL_CMD_FETCHSZM	0x1
#define V_FW_EQ_CTRL_CMD_FETCHSZM(x)	((x) << S_FW_EQ_CTRL_CMD_FETCHSZM)
#define G_FW_EQ_CTRL_CMD_FETCHSZM(x)	\
    (((x) >> S_FW_EQ_CTRL_CMD_FETCHSZM) & M_FW_EQ_CTRL_CMD_FETCHSZM)
#define F_FW_EQ_CTRL_CMD_FETCHSZM	V_FW_EQ_CTRL_CMD_FETCHSZM(1U)

#define S_FW_EQ_CTRL_CMD_STATUSPGNS	25
#define M_FW_EQ_CTRL_CMD_STATUSPGNS	0x1
#define V_FW_EQ_CTRL_CMD_STATUSPGNS(x)	((x) << S_FW_EQ_CTRL_CMD_STATUSPGNS)
#define G_FW_EQ_CTRL_CMD_STATUSPGNS(x)	\
    (((x) >> S_FW_EQ_CTRL_CMD_STATUSPGNS) & M_FW_EQ_CTRL_CMD_STATUSPGNS)
#define F_FW_EQ_CTRL_CMD_STATUSPGNS	V_FW_EQ_CTRL_CMD_STATUSPGNS(1U)

#define S_FW_EQ_CTRL_CMD_STATUSPGRO	24
#define M_FW_EQ_CTRL_CMD_STATUSPGRO	0x1
#define V_FW_EQ_CTRL_CMD_STATUSPGRO(x)	((x) << S_FW_EQ_CTRL_CMD_STATUSPGRO)
#define G_FW_EQ_CTRL_CMD_STATUSPGRO(x)	\
    (((x) >> S_FW_EQ_CTRL_CMD_STATUSPGRO) & M_FW_EQ_CTRL_CMD_STATUSPGRO)
#define F_FW_EQ_CTRL_CMD_STATUSPGRO	V_FW_EQ_CTRL_CMD_STATUSPGRO(1U)

#define S_FW_EQ_CTRL_CMD_FETCHNS	23
#define M_FW_EQ_CTRL_CMD_FETCHNS	0x1
#define V_FW_EQ_CTRL_CMD_FETCHNS(x)	((x) << S_FW_EQ_CTRL_CMD_FETCHNS)
#define G_FW_EQ_CTRL_CMD_FETCHNS(x)	\
    (((x) >> S_FW_EQ_CTRL_CMD_FETCHNS) & M_FW_EQ_CTRL_CMD_FETCHNS)
#define F_FW_EQ_CTRL_CMD_FETCHNS	V_FW_EQ_CTRL_CMD_FETCHNS(1U)

#define S_FW_EQ_CTRL_CMD_FETCHRO	22
#define M_FW_EQ_CTRL_CMD_FETCHRO	0x1
#define V_FW_EQ_CTRL_CMD_FETCHRO(x)	((x) << S_FW_EQ_CTRL_CMD_FETCHRO)
#define G_FW_EQ_CTRL_CMD_FETCHRO(x)	\
    (((x) >> S_FW_EQ_CTRL_CMD_FETCHRO) & M_FW_EQ_CTRL_CMD_FETCHRO)
#define F_FW_EQ_CTRL_CMD_FETCHRO	V_FW_EQ_CTRL_CMD_FETCHRO(1U)

#define S_FW_EQ_CTRL_CMD_HOSTFCMODE	20
#define M_FW_EQ_CTRL_CMD_HOSTFCMODE	0x3
#define V_FW_EQ_CTRL_CMD_HOSTFCMODE(x)	((x) << S_FW_EQ_CTRL_CMD_HOSTFCMODE)
#define G_FW_EQ_CTRL_CMD_HOSTFCMODE(x)	\
    (((x) >> S_FW_EQ_CTRL_CMD_HOSTFCMODE) & M_FW_EQ_CTRL_CMD_HOSTFCMODE)

#define S_FW_EQ_CTRL_CMD_CPRIO		19
#define M_FW_EQ_CTRL_CMD_CPRIO		0x1
#define V_FW_EQ_CTRL_CMD_CPRIO(x)	((x) << S_FW_EQ_CTRL_CMD_CPRIO)
#define G_FW_EQ_CTRL_CMD_CPRIO(x)	\
    (((x) >> S_FW_EQ_CTRL_CMD_CPRIO) & M_FW_EQ_CTRL_CMD_CPRIO)
#define F_FW_EQ_CTRL_CMD_CPRIO		V_FW_EQ_CTRL_CMD_CPRIO(1U)

#define S_FW_EQ_CTRL_CMD_ONCHIP		18
#define M_FW_EQ_CTRL_CMD_ONCHIP		0x1
#define V_FW_EQ_CTRL_CMD_ONCHIP(x)	((x) << S_FW_EQ_CTRL_CMD_ONCHIP)
#define G_FW_EQ_CTRL_CMD_ONCHIP(x)	\
    (((x) >> S_FW_EQ_CTRL_CMD_ONCHIP) & M_FW_EQ_CTRL_CMD_ONCHIP)
#define F_FW_EQ_CTRL_CMD_ONCHIP		V_FW_EQ_CTRL_CMD_ONCHIP(1U)

#define S_FW_EQ_CTRL_CMD_PCIECHN	16
#define M_FW_EQ_CTRL_CMD_PCIECHN	0x3
#define V_FW_EQ_CTRL_CMD_PCIECHN(x)	((x) << S_FW_EQ_CTRL_CMD_PCIECHN)
#define G_FW_EQ_CTRL_CMD_PCIECHN(x)	\
    (((x) >> S_FW_EQ_CTRL_CMD_PCIECHN) & M_FW_EQ_CTRL_CMD_PCIECHN)

#define S_FW_EQ_CTRL_CMD_IQID		0
#define M_FW_EQ_CTRL_CMD_IQID		0xffff
#define V_FW_EQ_CTRL_CMD_IQID(x)	((x) << S_FW_EQ_CTRL_CMD_IQID)
#define G_FW_EQ_CTRL_CMD_IQID(x)	\
    (((x) >> S_FW_EQ_CTRL_CMD_IQID) & M_FW_EQ_CTRL_CMD_IQID)

#define S_FW_EQ_CTRL_CMD_DCAEN		31
#define M_FW_EQ_CTRL_CMD_DCAEN		0x1
#define V_FW_EQ_CTRL_CMD_DCAEN(x)	((x) << S_FW_EQ_CTRL_CMD_DCAEN)
#define G_FW_EQ_CTRL_CMD_DCAEN(x)	\
    (((x) >> S_FW_EQ_CTRL_CMD_DCAEN) & M_FW_EQ_CTRL_CMD_DCAEN)
#define F_FW_EQ_CTRL_CMD_DCAEN		V_FW_EQ_CTRL_CMD_DCAEN(1U)

#define S_FW_EQ_CTRL_CMD_DCACPU		26
#define M_FW_EQ_CTRL_CMD_DCACPU		0x1f
#define V_FW_EQ_CTRL_CMD_DCACPU(x)	((x) << S_FW_EQ_CTRL_CMD_DCACPU)
#define G_FW_EQ_CTRL_CMD_DCACPU(x)	\
    (((x) >> S_FW_EQ_CTRL_CMD_DCACPU) & M_FW_EQ_CTRL_CMD_DCACPU)

#define S_FW_EQ_CTRL_CMD_FBMIN		23
#define M_FW_EQ_CTRL_CMD_FBMIN		0x7
#define V_FW_EQ_CTRL_CMD_FBMIN(x)	((x) << S_FW_EQ_CTRL_CMD_FBMIN)
#define G_FW_EQ_CTRL_CMD_FBMIN(x)	\
    (((x) >> S_FW_EQ_CTRL_CMD_FBMIN) & M_FW_EQ_CTRL_CMD_FBMIN)

#define S_FW_EQ_CTRL_CMD_FBMAX		20
#define M_FW_EQ_CTRL_CMD_FBMAX		0x7
#define V_FW_EQ_CTRL_CMD_FBMAX(x)	((x) << S_FW_EQ_CTRL_CMD_FBMAX)
#define G_FW_EQ_CTRL_CMD_FBMAX(x)	\
    (((x) >> S_FW_EQ_CTRL_CMD_FBMAX) & M_FW_EQ_CTRL_CMD_FBMAX)

#define S_FW_EQ_CTRL_CMD_CIDXFTHRESHO	19
#define M_FW_EQ_CTRL_CMD_CIDXFTHRESHO	0x1
#define V_FW_EQ_CTRL_CMD_CIDXFTHRESHO(x) \
    ((x) << S_FW_EQ_CTRL_CMD_CIDXFTHRESHO)
#define G_FW_EQ_CTRL_CMD_CIDXFTHRESHO(x) \
    (((x) >> S_FW_EQ_CTRL_CMD_CIDXFTHRESHO) & M_FW_EQ_CTRL_CMD_CIDXFTHRESHO)
#define F_FW_EQ_CTRL_CMD_CIDXFTHRESHO	V_FW_EQ_CTRL_CMD_CIDXFTHRESHO(1U)

#define S_FW_EQ_CTRL_CMD_CIDXFTHRESH	16
#define M_FW_EQ_CTRL_CMD_CIDXFTHRESH	0x7
#define V_FW_EQ_CTRL_CMD_CIDXFTHRESH(x)	((x) << S_FW_EQ_CTRL_CMD_CIDXFTHRESH)
#define G_FW_EQ_CTRL_CMD_CIDXFTHRESH(x)	\
    (((x) >> S_FW_EQ_CTRL_CMD_CIDXFTHRESH) & M_FW_EQ_CTRL_CMD_CIDXFTHRESH)

#define S_FW_EQ_CTRL_CMD_EQSIZE		0
#define M_FW_EQ_CTRL_CMD_EQSIZE		0xffff
#define V_FW_EQ_CTRL_CMD_EQSIZE(x)	((x) << S_FW_EQ_CTRL_CMD_EQSIZE)
#define G_FW_EQ_CTRL_CMD_EQSIZE(x)	\
    (((x) >> S_FW_EQ_CTRL_CMD_EQSIZE) & M_FW_EQ_CTRL_CMD_EQSIZE)

struct fw_eq_ofld_cmd {
	__be32 op_to_vfn;
	__be32 alloc_to_len16;
	__be32 eqid_pkd;
	__be32 physeqid_pkd;
	__be32 fetchszm_to_iqid;
	__be32 dcaen_to_eqsize;
	__be64 eqaddr;
};

#define S_FW_EQ_OFLD_CMD_PFN		8
#define M_FW_EQ_OFLD_CMD_PFN		0x7
#define V_FW_EQ_OFLD_CMD_PFN(x)		((x) << S_FW_EQ_OFLD_CMD_PFN)
#define G_FW_EQ_OFLD_CMD_PFN(x)		\
    (((x) >> S_FW_EQ_OFLD_CMD_PFN) & M_FW_EQ_OFLD_CMD_PFN)

#define S_FW_EQ_OFLD_CMD_VFN		0
#define M_FW_EQ_OFLD_CMD_VFN		0xff
#define V_FW_EQ_OFLD_CMD_VFN(x)		((x) << S_FW_EQ_OFLD_CMD_VFN)
#define G_FW_EQ_OFLD_CMD_VFN(x)		\
    (((x) >> S_FW_EQ_OFLD_CMD_VFN) & M_FW_EQ_OFLD_CMD_VFN)

#define S_FW_EQ_OFLD_CMD_ALLOC		31
#define M_FW_EQ_OFLD_CMD_ALLOC		0x1
#define V_FW_EQ_OFLD_CMD_ALLOC(x)	((x) << S_FW_EQ_OFLD_CMD_ALLOC)
#define G_FW_EQ_OFLD_CMD_ALLOC(x)	\
    (((x) >> S_FW_EQ_OFLD_CMD_ALLOC) & M_FW_EQ_OFLD_CMD_ALLOC)
#define F_FW_EQ_OFLD_CMD_ALLOC		V_FW_EQ_OFLD_CMD_ALLOC(1U)

#define S_FW_EQ_OFLD_CMD_FREE		30
#define M_FW_EQ_OFLD_CMD_FREE		0x1
#define V_FW_EQ_OFLD_CMD_FREE(x)	((x) << S_FW_EQ_OFLD_CMD_FREE)
#define G_FW_EQ_OFLD_CMD_FREE(x)	\
    (((x) >> S_FW_EQ_OFLD_CMD_FREE) & M_FW_EQ_OFLD_CMD_FREE)
#define F_FW_EQ_OFLD_CMD_FREE		V_FW_EQ_OFLD_CMD_FREE(1U)

#define S_FW_EQ_OFLD_CMD_MODIFY		29
#define M_FW_EQ_OFLD_CMD_MODIFY		0x1
#define V_FW_EQ_OFLD_CMD_MODIFY(x)	((x) << S_FW_EQ_OFLD_CMD_MODIFY)
#define G_FW_EQ_OFLD_CMD_MODIFY(x)	\
    (((x) >> S_FW_EQ_OFLD_CMD_MODIFY) & M_FW_EQ_OFLD_CMD_MODIFY)
#define F_FW_EQ_OFLD_CMD_MODIFY		V_FW_EQ_OFLD_CMD_MODIFY(1U)

#define S_FW_EQ_OFLD_CMD_EQSTART	28
#define M_FW_EQ_OFLD_CMD_EQSTART	0x1
#define V_FW_EQ_OFLD_CMD_EQSTART(x)	((x) << S_FW_EQ_OFLD_CMD_EQSTART)
#define G_FW_EQ_OFLD_CMD_EQSTART(x)	\
    (((x) >> S_FW_EQ_OFLD_CMD_EQSTART) & M_FW_EQ_OFLD_CMD_EQSTART)
#define F_FW_EQ_OFLD_CMD_EQSTART	V_FW_EQ_OFLD_CMD_EQSTART(1U)

#define S_FW_EQ_OFLD_CMD_EQSTOP		27
#define M_FW_EQ_OFLD_CMD_EQSTOP		0x1
#define V_FW_EQ_OFLD_CMD_EQSTOP(x)	((x) << S_FW_EQ_OFLD_CMD_EQSTOP)
#define G_FW_EQ_OFLD_CMD_EQSTOP(x)	\
    (((x) >> S_FW_EQ_OFLD_CMD_EQSTOP) & M_FW_EQ_OFLD_CMD_EQSTOP)
#define F_FW_EQ_OFLD_CMD_EQSTOP		V_FW_EQ_OFLD_CMD_EQSTOP(1U)

#define S_FW_EQ_OFLD_CMD_EQID		0
#define M_FW_EQ_OFLD_CMD_EQID		0xfffff
#define V_FW_EQ_OFLD_CMD_EQID(x)	((x) << S_FW_EQ_OFLD_CMD_EQID)
#define G_FW_EQ_OFLD_CMD_EQID(x)	\
    (((x) >> S_FW_EQ_OFLD_CMD_EQID) & M_FW_EQ_OFLD_CMD_EQID)

#define S_FW_EQ_OFLD_CMD_PHYSEQID	0
#define M_FW_EQ_OFLD_CMD_PHYSEQID	0xfffff
#define V_FW_EQ_OFLD_CMD_PHYSEQID(x)	((x) << S_FW_EQ_OFLD_CMD_PHYSEQID)
#define G_FW_EQ_OFLD_CMD_PHYSEQID(x)	\
    (((x) >> S_FW_EQ_OFLD_CMD_PHYSEQID) & M_FW_EQ_OFLD_CMD_PHYSEQID)

#define S_FW_EQ_OFLD_CMD_FETCHSZM	26
#define M_FW_EQ_OFLD_CMD_FETCHSZM	0x1
#define V_FW_EQ_OFLD_CMD_FETCHSZM(x)	((x) << S_FW_EQ_OFLD_CMD_FETCHSZM)
#define G_FW_EQ_OFLD_CMD_FETCHSZM(x)	\
    (((x) >> S_FW_EQ_OFLD_CMD_FETCHSZM) & M_FW_EQ_OFLD_CMD_FETCHSZM)
#define F_FW_EQ_OFLD_CMD_FETCHSZM	V_FW_EQ_OFLD_CMD_FETCHSZM(1U)

#define S_FW_EQ_OFLD_CMD_STATUSPGNS	25
#define M_FW_EQ_OFLD_CMD_STATUSPGNS	0x1
#define V_FW_EQ_OFLD_CMD_STATUSPGNS(x)	((x) << S_FW_EQ_OFLD_CMD_STATUSPGNS)
#define G_FW_EQ_OFLD_CMD_STATUSPGNS(x)	\
    (((x) >> S_FW_EQ_OFLD_CMD_STATUSPGNS) & M_FW_EQ_OFLD_CMD_STATUSPGNS)
#define F_FW_EQ_OFLD_CMD_STATUSPGNS	V_FW_EQ_OFLD_CMD_STATUSPGNS(1U)

#define S_FW_EQ_OFLD_CMD_STATUSPGRO	24
#define M_FW_EQ_OFLD_CMD_STATUSPGRO	0x1
#define V_FW_EQ_OFLD_CMD_STATUSPGRO(x)	((x) << S_FW_EQ_OFLD_CMD_STATUSPGRO)
#define G_FW_EQ_OFLD_CMD_STATUSPGRO(x)	\
    (((x) >> S_FW_EQ_OFLD_CMD_STATUSPGRO) & M_FW_EQ_OFLD_CMD_STATUSPGRO)
#define F_FW_EQ_OFLD_CMD_STATUSPGRO	V_FW_EQ_OFLD_CMD_STATUSPGRO(1U)

#define S_FW_EQ_OFLD_CMD_FETCHNS	23
#define M_FW_EQ_OFLD_CMD_FETCHNS	0x1
#define V_FW_EQ_OFLD_CMD_FETCHNS(x)	((x) << S_FW_EQ_OFLD_CMD_FETCHNS)
#define G_FW_EQ_OFLD_CMD_FETCHNS(x)	\
    (((x) >> S_FW_EQ_OFLD_CMD_FETCHNS) & M_FW_EQ_OFLD_CMD_FETCHNS)
#define F_FW_EQ_OFLD_CMD_FETCHNS	V_FW_EQ_OFLD_CMD_FETCHNS(1U)

#define S_FW_EQ_OFLD_CMD_FETCHRO	22
#define M_FW_EQ_OFLD_CMD_FETCHRO	0x1
#define V_FW_EQ_OFLD_CMD_FETCHRO(x)	((x) << S_FW_EQ_OFLD_CMD_FETCHRO)
#define G_FW_EQ_OFLD_CMD_FETCHRO(x)	\
    (((x) >> S_FW_EQ_OFLD_CMD_FETCHRO) & M_FW_EQ_OFLD_CMD_FETCHRO)
#define F_FW_EQ_OFLD_CMD_FETCHRO	V_FW_EQ_OFLD_CMD_FETCHRO(1U)

#define S_FW_EQ_OFLD_CMD_HOSTFCMODE	20
#define M_FW_EQ_OFLD_CMD_HOSTFCMODE	0x3
#define V_FW_EQ_OFLD_CMD_HOSTFCMODE(x)	((x) << S_FW_EQ_OFLD_CMD_HOSTFCMODE)
#define G_FW_EQ_OFLD_CMD_HOSTFCMODE(x)	\
    (((x) >> S_FW_EQ_OFLD_CMD_HOSTFCMODE) & M_FW_EQ_OFLD_CMD_HOSTFCMODE)

#define S_FW_EQ_OFLD_CMD_CPRIO		19
#define M_FW_EQ_OFLD_CMD_CPRIO		0x1
#define V_FW_EQ_OFLD_CMD_CPRIO(x)	((x) << S_FW_EQ_OFLD_CMD_CPRIO)
#define G_FW_EQ_OFLD_CMD_CPRIO(x)	\
    (((x) >> S_FW_EQ_OFLD_CMD_CPRIO) & M_FW_EQ_OFLD_CMD_CPRIO)
#define F_FW_EQ_OFLD_CMD_CPRIO		V_FW_EQ_OFLD_CMD_CPRIO(1U)

#define S_FW_EQ_OFLD_CMD_ONCHIP		18
#define M_FW_EQ_OFLD_CMD_ONCHIP		0x1
#define V_FW_EQ_OFLD_CMD_ONCHIP(x)	((x) << S_FW_EQ_OFLD_CMD_ONCHIP)
#define G_FW_EQ_OFLD_CMD_ONCHIP(x)	\
    (((x) >> S_FW_EQ_OFLD_CMD_ONCHIP) & M_FW_EQ_OFLD_CMD_ONCHIP)
#define F_FW_EQ_OFLD_CMD_ONCHIP		V_FW_EQ_OFLD_CMD_ONCHIP(1U)

#define S_FW_EQ_OFLD_CMD_PCIECHN	16
#define M_FW_EQ_OFLD_CMD_PCIECHN	0x3
#define V_FW_EQ_OFLD_CMD_PCIECHN(x)	((x) << S_FW_EQ_OFLD_CMD_PCIECHN)
#define G_FW_EQ_OFLD_CMD_PCIECHN(x)	\
    (((x) >> S_FW_EQ_OFLD_CMD_PCIECHN) & M_FW_EQ_OFLD_CMD_PCIECHN)

#define S_FW_EQ_OFLD_CMD_IQID		0
#define M_FW_EQ_OFLD_CMD_IQID		0xffff
#define V_FW_EQ_OFLD_CMD_IQID(x)	((x) << S_FW_EQ_OFLD_CMD_IQID)
#define G_FW_EQ_OFLD_CMD_IQID(x)	\
    (((x) >> S_FW_EQ_OFLD_CMD_IQID) & M_FW_EQ_OFLD_CMD_IQID)

#define S_FW_EQ_OFLD_CMD_DCAEN		31
#define M_FW_EQ_OFLD_CMD_DCAEN		0x1
#define V_FW_EQ_OFLD_CMD_DCAEN(x)	((x) << S_FW_EQ_OFLD_CMD_DCAEN)
#define G_FW_EQ_OFLD_CMD_DCAEN(x)	\
    (((x) >> S_FW_EQ_OFLD_CMD_DCAEN) & M_FW_EQ_OFLD_CMD_DCAEN)
#define F_FW_EQ_OFLD_CMD_DCAEN		V_FW_EQ_OFLD_CMD_DCAEN(1U)

#define S_FW_EQ_OFLD_CMD_DCACPU		26
#define M_FW_EQ_OFLD_CMD_DCACPU		0x1f
#define V_FW_EQ_OFLD_CMD_DCACPU(x)	((x) << S_FW_EQ_OFLD_CMD_DCACPU)
#define G_FW_EQ_OFLD_CMD_DCACPU(x)	\
    (((x) >> S_FW_EQ_OFLD_CMD_DCACPU) & M_FW_EQ_OFLD_CMD_DCACPU)

#define S_FW_EQ_OFLD_CMD_FBMIN		23
#define M_FW_EQ_OFLD_CMD_FBMIN		0x7
#define V_FW_EQ_OFLD_CMD_FBMIN(x)	((x) << S_FW_EQ_OFLD_CMD_FBMIN)
#define G_FW_EQ_OFLD_CMD_FBMIN(x)	\
    (((x) >> S_FW_EQ_OFLD_CMD_FBMIN) & M_FW_EQ_OFLD_CMD_FBMIN)

#define S_FW_EQ_OFLD_CMD_FBMAX		20
#define M_FW_EQ_OFLD_CMD_FBMAX		0x7
#define V_FW_EQ_OFLD_CMD_FBMAX(x)	((x) << S_FW_EQ_OFLD_CMD_FBMAX)
#define G_FW_EQ_OFLD_CMD_FBMAX(x)	\
    (((x) >> S_FW_EQ_OFLD_CMD_FBMAX) & M_FW_EQ_OFLD_CMD_FBMAX)

#define S_FW_EQ_OFLD_CMD_CIDXFTHRESHO	19
#define M_FW_EQ_OFLD_CMD_CIDXFTHRESHO	0x1
#define V_FW_EQ_OFLD_CMD_CIDXFTHRESHO(x) \
    ((x) << S_FW_EQ_OFLD_CMD_CIDXFTHRESHO)
#define G_FW_EQ_OFLD_CMD_CIDXFTHRESHO(x) \
    (((x) >> S_FW_EQ_OFLD_CMD_CIDXFTHRESHO) & M_FW_EQ_OFLD_CMD_CIDXFTHRESHO)
#define F_FW_EQ_OFLD_CMD_CIDXFTHRESHO	V_FW_EQ_OFLD_CMD_CIDXFTHRESHO(1U)

#define S_FW_EQ_OFLD_CMD_CIDXFTHRESH	16
#define M_FW_EQ_OFLD_CMD_CIDXFTHRESH	0x7
#define V_FW_EQ_OFLD_CMD_CIDXFTHRESH(x)	((x) << S_FW_EQ_OFLD_CMD_CIDXFTHRESH)
#define G_FW_EQ_OFLD_CMD_CIDXFTHRESH(x)	\
    (((x) >> S_FW_EQ_OFLD_CMD_CIDXFTHRESH) & M_FW_EQ_OFLD_CMD_CIDXFTHRESH)

#define S_FW_EQ_OFLD_CMD_EQSIZE		0
#define M_FW_EQ_OFLD_CMD_EQSIZE		0xffff
#define V_FW_EQ_OFLD_CMD_EQSIZE(x)	((x) << S_FW_EQ_OFLD_CMD_EQSIZE)
#define G_FW_EQ_OFLD_CMD_EQSIZE(x)	\
    (((x) >> S_FW_EQ_OFLD_CMD_EQSIZE) & M_FW_EQ_OFLD_CMD_EQSIZE)

/* Macros for VIID parsing:
   VIID - [10:8] PFN, [7] VI Valid, [6:0] VI number */
#define S_FW_VIID_PFN		8
#define M_FW_VIID_PFN		0x7
#define V_FW_VIID_PFN(x)	((x) << S_FW_VIID_PFN)
#define G_FW_VIID_PFN(x)	(((x) >> S_FW_VIID_PFN) & M_FW_VIID_PFN)

#define S_FW_VIID_VIVLD		7
#define M_FW_VIID_VIVLD		0x1
#define V_FW_VIID_VIVLD(x)	((x) << S_FW_VIID_VIVLD)
#define G_FW_VIID_VIVLD(x)	(((x) >> S_FW_VIID_VIVLD) & M_FW_VIID_VIVLD)

#define S_FW_VIID_VIN		0
#define M_FW_VIID_VIN		0x7F
#define V_FW_VIID_VIN(x)	((x) << S_FW_VIID_VIN)
#define G_FW_VIID_VIN(x)	(((x) >> S_FW_VIID_VIN) & M_FW_VIID_VIN)

/* Macros for VIID parsing:
   VIID - [11:9] PFN, [8] VI Valid, [7:0] VI number */
#define S_FW_256VIID_PFN		9
#define M_FW_256VIID_PFN		0x7
#define V_FW_256VIID_PFN(x)		((x) << S_FW_256VIID_PFN)
#define G_FW_256VIID_PFN(x)		(((x) >> S_FW_256VIID_PFN) & M_FW_256VIID_PFN)

#define S_FW_256VIID_VIVLD		8
#define M_FW_256VIID_VIVLD		0x1
#define V_FW_256VIID_VIVLD(x)		((x) << S_FW_256VIID_VIVLD)
#define G_FW_256VIID_VIVLD(x)		(((x) >> S_FW_256VIID_VIVLD) & M_FW_256VIID_VIVLD)

#define S_FW_256VIID_VIN		0
#define M_FW_256VIID_VIN		0xFF
#define V_FW_256VIID_VIN(x)		((x) << S_FW_256VIID_VIN)
#define G_FW_256VIID_VIN(x)		(((x) >> S_FW_256VIID_VIN) & M_FW_256VIID_VIN)

enum fw_vi_func {
	FW_VI_FUNC_ETH,
	FW_VI_FUNC_OFLD,
	FW_VI_FUNC_IWARP,
	FW_VI_FUNC_OPENISCSI,
	FW_VI_FUNC_OPENFCOE,
	FW_VI_FUNC_FOISCSI,
	FW_VI_FUNC_FOFCOE,
	FW_VI_FUNC_FW,
};

struct fw_vi_cmd {
	__be32 op_to_vfn;
	__be32 alloc_to_len16;
	__be16 type_to_viid;
	__u8   mac[6];
	__u8   portid_pkd;
	__u8   nmac;
	__u8   nmac0[6];
	__be16 norss_rsssize;
	__u8   nmac1[6];
	__be16 idsiiq_pkd;
	__u8   nmac2[6];
	__be16 idseiq_pkd;
	__u8   nmac3[6];
	__be64 r9;
	__be64 r10;
};

#define S_FW_VI_CMD_PFN			8
#define M_FW_VI_CMD_PFN			0x7
#define V_FW_VI_CMD_PFN(x)		((x) << S_FW_VI_CMD_PFN)
#define G_FW_VI_CMD_PFN(x)		\
    (((x) >> S_FW_VI_CMD_PFN) & M_FW_VI_CMD_PFN)

#define S_FW_VI_CMD_VFN			0
#define M_FW_VI_CMD_VFN			0xff
#define V_FW_VI_CMD_VFN(x)		((x) << S_FW_VI_CMD_VFN)
#define G_FW_VI_CMD_VFN(x)		\
    (((x) >> S_FW_VI_CMD_VFN) & M_FW_VI_CMD_VFN)

#define S_FW_VI_CMD_ALLOC		31
#define M_FW_VI_CMD_ALLOC		0x1
#define V_FW_VI_CMD_ALLOC(x)		((x) << S_FW_VI_CMD_ALLOC)
#define G_FW_VI_CMD_ALLOC(x)		\
    (((x) >> S_FW_VI_CMD_ALLOC) & M_FW_VI_CMD_ALLOC)
#define F_FW_VI_CMD_ALLOC		V_FW_VI_CMD_ALLOC(1U)

#define S_FW_VI_CMD_FREE		30
#define M_FW_VI_CMD_FREE		0x1
#define V_FW_VI_CMD_FREE(x)		((x) << S_FW_VI_CMD_FREE)
#define G_FW_VI_CMD_FREE(x)		\
    (((x) >> S_FW_VI_CMD_FREE) & M_FW_VI_CMD_FREE)
#define F_FW_VI_CMD_FREE		V_FW_VI_CMD_FREE(1U)

#define S_FW_VI_CMD_VFVLD		24
#define M_FW_VI_CMD_VFVLD		0x1
#define V_FW_VI_CMD_VFVLD(x)		((x) << S_FW_VI_CMD_VFVLD)
#define G_FW_VI_CMD_VFVLD(x)		\
    (((x) >> S_FW_VI_CMD_VFVLD) & M_FW_VI_CMD_VFVLD)
#define F_FW_VI_CMD_VFVLD		V_FW_VI_CMD_VFVLD(1U)

#define S_FW_VI_CMD_VIN			16
#define M_FW_VI_CMD_VIN			0xff
#define V_FW_VI_CMD_VIN(x)		((x) << S_FW_VI_CMD_VIN)
#define G_FW_VI_CMD_VIN(x)		\
    (((x) >> S_FW_VI_CMD_VIN) & M_FW_VI_CMD_VIN)

#define S_FW_VI_CMD_TYPE		15
#define M_FW_VI_CMD_TYPE		0x1
#define V_FW_VI_CMD_TYPE(x)		((x) << S_FW_VI_CMD_TYPE)
#define G_FW_VI_CMD_TYPE(x)		\
    (((x) >> S_FW_VI_CMD_TYPE) & M_FW_VI_CMD_TYPE)
#define F_FW_VI_CMD_TYPE		V_FW_VI_CMD_TYPE(1U)

#define S_FW_VI_CMD_FUNC		12
#define M_FW_VI_CMD_FUNC		0x7
#define V_FW_VI_CMD_FUNC(x)		((x) << S_FW_VI_CMD_FUNC)
#define G_FW_VI_CMD_FUNC(x)		\
    (((x) >> S_FW_VI_CMD_FUNC) & M_FW_VI_CMD_FUNC)

#define S_FW_VI_CMD_VIID		0
#define M_FW_VI_CMD_VIID		0xfff
#define V_FW_VI_CMD_VIID(x)		((x) << S_FW_VI_CMD_VIID)
#define G_FW_VI_CMD_VIID(x)		\
    (((x) >> S_FW_VI_CMD_VIID) & M_FW_VI_CMD_VIID)

#define S_FW_VI_CMD_PORTID		4
#define M_FW_VI_CMD_PORTID		0xf
#define V_FW_VI_CMD_PORTID(x)		((x) << S_FW_VI_CMD_PORTID)
#define G_FW_VI_CMD_PORTID(x)		\
    (((x) >> S_FW_VI_CMD_PORTID) & M_FW_VI_CMD_PORTID)

#define S_FW_VI_CMD_NORSS		11
#define M_FW_VI_CMD_NORSS		0x1
#define V_FW_VI_CMD_NORSS(x)		((x) << S_FW_VI_CMD_NORSS)
#define G_FW_VI_CMD_NORSS(x)		\
    (((x) >> S_FW_VI_CMD_NORSS) & M_FW_VI_CMD_NORSS)
#define F_FW_VI_CMD_NORSS		V_FW_VI_CMD_NORSS(1U)

#define S_FW_VI_CMD_RSSSIZE		0
#define M_FW_VI_CMD_RSSSIZE		0x7ff
#define V_FW_VI_CMD_RSSSIZE(x)		((x) << S_FW_VI_CMD_RSSSIZE)
#define G_FW_VI_CMD_RSSSIZE(x)		\
    (((x) >> S_FW_VI_CMD_RSSSIZE) & M_FW_VI_CMD_RSSSIZE)

#define S_FW_VI_CMD_IDSIIQ		0
#define M_FW_VI_CMD_IDSIIQ		0x3ff
#define V_FW_VI_CMD_IDSIIQ(x)		((x) << S_FW_VI_CMD_IDSIIQ)
#define G_FW_VI_CMD_IDSIIQ(x)		\
    (((x) >> S_FW_VI_CMD_IDSIIQ) & M_FW_VI_CMD_IDSIIQ)

#define S_FW_VI_CMD_IDSEIQ		0
#define M_FW_VI_CMD_IDSEIQ		0x3ff
#define V_FW_VI_CMD_IDSEIQ(x)		((x) << S_FW_VI_CMD_IDSEIQ)
#define G_FW_VI_CMD_IDSEIQ(x)		\
    (((x) >> S_FW_VI_CMD_IDSEIQ) & M_FW_VI_CMD_IDSEIQ)

/* Special VI_MAC command index ids */
#define FW_VI_MAC_ADD_MAC		0x3FF
#define FW_VI_MAC_ADD_PERSIST_MAC	0x3FE
#define FW_VI_MAC_MAC_BASED_FREE	0x3FD
#define FW_VI_MAC_ID_BASED_FREE		0x3FC

enum fw_vi_mac_smac {
	FW_VI_MAC_MPS_TCAM_ENTRY,
	FW_VI_MAC_MPS_TCAM_ONLY,
	FW_VI_MAC_SMT_ONLY,
	FW_VI_MAC_SMT_AND_MPSTCAM
};

enum fw_vi_mac_result {
	FW_VI_MAC_R_SUCCESS,
	FW_VI_MAC_R_F_NONEXISTENT_NOMEM,
	FW_VI_MAC_R_SMAC_FAIL,
	FW_VI_MAC_R_F_ACL_CHECK
};

enum fw_vi_mac_entry_types {
	FW_VI_MAC_TYPE_EXACTMAC,
	FW_VI_MAC_TYPE_HASHVEC,
	FW_VI_MAC_TYPE_RAW,
	FW_VI_MAC_TYPE_EXACTMAC_VNI,
};

struct fw_vi_mac_cmd {
	__be32 op_to_viid;
	__be32 freemacs_to_len16;
	union fw_vi_mac {
		struct fw_vi_mac_exact {
			__be16 valid_to_idx;
			__u8   macaddr[6];
		} exact[7];
		struct fw_vi_mac_hash {
			__be64 hashvec;
		} hash;
		struct fw_vi_mac_raw {
			__be32 raw_idx_pkd;
			__be32 data0_pkd;
			__be32 data1[2];
			__be64 data0m_pkd;
			__be32 data1m[2];
		} raw;
		struct fw_vi_mac_vni {
			__be16 valid_to_idx;
			__u8   macaddr[6];
			__be16 r7;
			__u8   macaddr_mask[6];
			__be32 lookup_type_to_vni;
			__be32 vni_mask_pkd;
		} exact_vni[2];
	} u;
};

#define S_FW_VI_MAC_CMD_SMTID		12
#define M_FW_VI_MAC_CMD_SMTID		0xff
#define V_FW_VI_MAC_CMD_SMTID(x)	((x) << S_FW_VI_MAC_CMD_SMTID)
#define G_FW_VI_MAC_CMD_SMTID(x)	\
    (((x) >> S_FW_VI_MAC_CMD_SMTID) & M_FW_VI_MAC_CMD_SMTID)

#define S_FW_VI_MAC_CMD_VIID		0
#define M_FW_VI_MAC_CMD_VIID		0xfff
#define V_FW_VI_MAC_CMD_VIID(x)		((x) << S_FW_VI_MAC_CMD_VIID)
#define G_FW_VI_MAC_CMD_VIID(x)		\
    (((x) >> S_FW_VI_MAC_CMD_VIID) & M_FW_VI_MAC_CMD_VIID)

#define S_FW_VI_MAC_CMD_FREEMACS	31
#define M_FW_VI_MAC_CMD_FREEMACS	0x1
#define V_FW_VI_MAC_CMD_FREEMACS(x)	((x) << S_FW_VI_MAC_CMD_FREEMACS)
#define G_FW_VI_MAC_CMD_FREEMACS(x)	\
    (((x) >> S_FW_VI_MAC_CMD_FREEMACS) & M_FW_VI_MAC_CMD_FREEMACS)
#define F_FW_VI_MAC_CMD_FREEMACS	V_FW_VI_MAC_CMD_FREEMACS(1U)

#define S_FW_VI_MAC_CMD_IS_SMAC		30
#define M_FW_VI_MAC_CMD_IS_SMAC		0x1
#define V_FW_VI_MAC_CMD_IS_SMAC(x)	((x) << S_FW_VI_MAC_CMD_IS_SMAC)
#define G_FW_VI_MAC_CMD_IS_SMAC(x)	\
    (((x) >> S_FW_VI_MAC_CMD_IS_SMAC) & M_FW_VI_MAC_CMD_IS_SMAC)
#define F_FW_VI_MAC_CMD_IS_SMAC	V_FW_VI_MAC_CMD_IS_SMAC(1U)

#define S_FW_VI_MAC_CMD_ENTRY_TYPE	23
#define M_FW_VI_MAC_CMD_ENTRY_TYPE	0x7
#define V_FW_VI_MAC_CMD_ENTRY_TYPE(x)	((x) << S_FW_VI_MAC_CMD_ENTRY_TYPE)
#define G_FW_VI_MAC_CMD_ENTRY_TYPE(x)	\
    (((x) >> S_FW_VI_MAC_CMD_ENTRY_TYPE) & M_FW_VI_MAC_CMD_ENTRY_TYPE)

#define S_FW_VI_MAC_CMD_HASHUNIEN	22
#define M_FW_VI_MAC_CMD_HASHUNIEN	0x1
#define V_FW_VI_MAC_CMD_HASHUNIEN(x)	((x) << S_FW_VI_MAC_CMD_HASHUNIEN)
#define G_FW_VI_MAC_CMD_HASHUNIEN(x)	\
    (((x) >> S_FW_VI_MAC_CMD_HASHUNIEN) & M_FW_VI_MAC_CMD_HASHUNIEN)
#define F_FW_VI_MAC_CMD_HASHUNIEN	V_FW_VI_MAC_CMD_HASHUNIEN(1U)

#define S_FW_VI_MAC_CMD_VALID		15
#define M_FW_VI_MAC_CMD_VALID		0x1
#define V_FW_VI_MAC_CMD_VALID(x)	((x) << S_FW_VI_MAC_CMD_VALID)
#define G_FW_VI_MAC_CMD_VALID(x)	\
    (((x) >> S_FW_VI_MAC_CMD_VALID) & M_FW_VI_MAC_CMD_VALID)
#define F_FW_VI_MAC_CMD_VALID		V_FW_VI_MAC_CMD_VALID(1U)

#define S_FW_VI_MAC_CMD_PRIO		12
#define M_FW_VI_MAC_CMD_PRIO		0x7
#define V_FW_VI_MAC_CMD_PRIO(x)		((x) << S_FW_VI_MAC_CMD_PRIO)
#define G_FW_VI_MAC_CMD_PRIO(x)		\
    (((x) >> S_FW_VI_MAC_CMD_PRIO) & M_FW_VI_MAC_CMD_PRIO)

#define S_FW_VI_MAC_CMD_SMAC_RESULT	10
#define M_FW_VI_MAC_CMD_SMAC_RESULT	0x3
#define V_FW_VI_MAC_CMD_SMAC_RESULT(x)	((x) << S_FW_VI_MAC_CMD_SMAC_RESULT)
#define G_FW_VI_MAC_CMD_SMAC_RESULT(x)	\
    (((x) >> S_FW_VI_MAC_CMD_SMAC_RESULT) & M_FW_VI_MAC_CMD_SMAC_RESULT)

#define S_FW_VI_MAC_CMD_IDX		0
#define M_FW_VI_MAC_CMD_IDX		0x3ff
#define V_FW_VI_MAC_CMD_IDX(x)		((x) << S_FW_VI_MAC_CMD_IDX)
#define G_FW_VI_MAC_CMD_IDX(x)		\
    (((x) >> S_FW_VI_MAC_CMD_IDX) & M_FW_VI_MAC_CMD_IDX)

#define S_FW_VI_MAC_CMD_RAW_IDX		16
#define M_FW_VI_MAC_CMD_RAW_IDX		0xffff
#define V_FW_VI_MAC_CMD_RAW_IDX(x)	((x) << S_FW_VI_MAC_CMD_RAW_IDX)
#define G_FW_VI_MAC_CMD_RAW_IDX(x)	\
    (((x) >> S_FW_VI_MAC_CMD_RAW_IDX) & M_FW_VI_MAC_CMD_RAW_IDX)

#define S_FW_VI_MAC_CMD_DATA0		0
#define M_FW_VI_MAC_CMD_DATA0		0xffff
#define V_FW_VI_MAC_CMD_DATA0(x)	((x) << S_FW_VI_MAC_CMD_DATA0)
#define G_FW_VI_MAC_CMD_DATA0(x)	\
    (((x) >> S_FW_VI_MAC_CMD_DATA0) & M_FW_VI_MAC_CMD_DATA0)

#define S_FW_VI_MAC_CMD_LOOKUP_TYPE	31
#define M_FW_VI_MAC_CMD_LOOKUP_TYPE	0x1
#define V_FW_VI_MAC_CMD_LOOKUP_TYPE(x)	((x) << S_FW_VI_MAC_CMD_LOOKUP_TYPE)
#define G_FW_VI_MAC_CMD_LOOKUP_TYPE(x)	\
    (((x) >> S_FW_VI_MAC_CMD_LOOKUP_TYPE) & M_FW_VI_MAC_CMD_LOOKUP_TYPE)
#define F_FW_VI_MAC_CMD_LOOKUP_TYPE	V_FW_VI_MAC_CMD_LOOKUP_TYPE(1U)

#define S_FW_VI_MAC_CMD_DIP_HIT		30
#define M_FW_VI_MAC_CMD_DIP_HIT		0x1
#define V_FW_VI_MAC_CMD_DIP_HIT(x)	((x) << S_FW_VI_MAC_CMD_DIP_HIT)
#define G_FW_VI_MAC_CMD_DIP_HIT(x)	\
    (((x) >> S_FW_VI_MAC_CMD_DIP_HIT) & M_FW_VI_MAC_CMD_DIP_HIT)
#define F_FW_VI_MAC_CMD_DIP_HIT	V_FW_VI_MAC_CMD_DIP_HIT(1U)

#define S_FW_VI_MAC_CMD_VNI	0
#define M_FW_VI_MAC_CMD_VNI	0xffffff
#define V_FW_VI_MAC_CMD_VNI(x)	((x) << S_FW_VI_MAC_CMD_VNI)
#define G_FW_VI_MAC_CMD_VNI(x)	\
    (((x) >> S_FW_VI_MAC_CMD_VNI) & M_FW_VI_MAC_CMD_VNI)

/* Extracting loopback port number passed from driver.
 * as a part of fw_vi_mac_vni For non loopback entries
 * ignore the field and update port number from flowc.
 * Fw will ignore if physical port number received.
 * expected range (4-7).
 */

#define S_FW_VI_MAC_CMD_PORT            24
#define M_FW_VI_MAC_CMD_PORT            0x7
#define V_FW_VI_MAC_CMD_PORT(x)         ((x) << S_FW_VI_MAC_CMD_PORT)
#define G_FW_VI_MAC_CMD_PORT(x)         \
    (((x) >> S_FW_VI_MAC_CMD_PORT) & M_FW_VI_MAC_CMD_PORT)

#define S_FW_VI_MAC_CMD_VNI_MASK	0
#define M_FW_VI_MAC_CMD_VNI_MASK	0xffffff
#define V_FW_VI_MAC_CMD_VNI_MASK(x)	((x) << S_FW_VI_MAC_CMD_VNI_MASK)
#define G_FW_VI_MAC_CMD_VNI_MASK(x)	\
    (((x) >> S_FW_VI_MAC_CMD_VNI_MASK) & M_FW_VI_MAC_CMD_VNI_MASK)

/* T4 max MTU supported */
#define T4_MAX_MTU_SUPPORTED	9600
#define FW_RXMODE_MTU_NO_CHG	65535

struct fw_vi_rxmode_cmd {
	__be32 op_to_viid;
	__be32 retval_len16;
	__be32 mtu_to_vlanexen;
	__be32 r4_lo;
};

#define S_FW_VI_RXMODE_CMD_VIID		0
#define M_FW_VI_RXMODE_CMD_VIID		0xfff
#define V_FW_VI_RXMODE_CMD_VIID(x)	((x) << S_FW_VI_RXMODE_CMD_VIID)
#define G_FW_VI_RXMODE_CMD_VIID(x)	\
    (((x) >> S_FW_VI_RXMODE_CMD_VIID) & M_FW_VI_RXMODE_CMD_VIID)

#define S_FW_VI_RXMODE_CMD_MTU		16
#define M_FW_VI_RXMODE_CMD_MTU		0xffff
#define V_FW_VI_RXMODE_CMD_MTU(x)	((x) << S_FW_VI_RXMODE_CMD_MTU)
#define G_FW_VI_RXMODE_CMD_MTU(x)	\
    (((x) >> S_FW_VI_RXMODE_CMD_MTU) & M_FW_VI_RXMODE_CMD_MTU)

#define S_FW_VI_RXMODE_CMD_PROMISCEN	14
#define M_FW_VI_RXMODE_CMD_PROMISCEN	0x3
#define V_FW_VI_RXMODE_CMD_PROMISCEN(x)	((x) << S_FW_VI_RXMODE_CMD_PROMISCEN)
#define G_FW_VI_RXMODE_CMD_PROMISCEN(x)	\
    (((x) >> S_FW_VI_RXMODE_CMD_PROMISCEN) & M_FW_VI_RXMODE_CMD_PROMISCEN)

#define S_FW_VI_RXMODE_CMD_ALLMULTIEN	12
#define M_FW_VI_RXMODE_CMD_ALLMULTIEN	0x3
#define V_FW_VI_RXMODE_CMD_ALLMULTIEN(x) \
    ((x) << S_FW_VI_RXMODE_CMD_ALLMULTIEN)
#define G_FW_VI_RXMODE_CMD_ALLMULTIEN(x) \
    (((x) >> S_FW_VI_RXMODE_CMD_ALLMULTIEN) & M_FW_VI_RXMODE_CMD_ALLMULTIEN)

#define S_FW_VI_RXMODE_CMD_BROADCASTEN	10
#define M_FW_VI_RXMODE_CMD_BROADCASTEN	0x3
#define V_FW_VI_RXMODE_CMD_BROADCASTEN(x) \
    ((x) << S_FW_VI_RXMODE_CMD_BROADCASTEN)
#define G_FW_VI_RXMODE_CMD_BROADCASTEN(x) \
    (((x) >> S_FW_VI_RXMODE_CMD_BROADCASTEN) & M_FW_VI_RXMODE_CMD_BROADCASTEN)

#define S_FW_VI_RXMODE_CMD_VLANEXEN	8
#define M_FW_VI_RXMODE_CMD_VLANEXEN	0x3
#define V_FW_VI_RXMODE_CMD_VLANEXEN(x)	((x) << S_FW_VI_RXMODE_CMD_VLANEXEN)
#define G_FW_VI_RXMODE_CMD_VLANEXEN(x)	\
    (((x) >> S_FW_VI_RXMODE_CMD_VLANEXEN) & M_FW_VI_RXMODE_CMD_VLANEXEN)

struct fw_vi_enable_cmd {
	__be32 op_to_viid;
	__be32 ien_to_len16;
	__be16 blinkdur;
	__be16 r3;
	__be32 r4;
};

#define S_FW_VI_ENABLE_CMD_VIID		0
#define M_FW_VI_ENABLE_CMD_VIID		0xfff
#define V_FW_VI_ENABLE_CMD_VIID(x)	((x) << S_FW_VI_ENABLE_CMD_VIID)
#define G_FW_VI_ENABLE_CMD_VIID(x)	\
    (((x) >> S_FW_VI_ENABLE_CMD_VIID) & M_FW_VI_ENABLE_CMD_VIID)

#define S_FW_VI_ENABLE_CMD_IEN		31
#define M_FW_VI_ENABLE_CMD_IEN		0x1
#define V_FW_VI_ENABLE_CMD_IEN(x)	((x) << S_FW_VI_ENABLE_CMD_IEN)
#define G_FW_VI_ENABLE_CMD_IEN(x)	\
    (((x) >> S_FW_VI_ENABLE_CMD_IEN) & M_FW_VI_ENABLE_CMD_IEN)
#define F_FW_VI_ENABLE_CMD_IEN		V_FW_VI_ENABLE_CMD_IEN(1U)

#define S_FW_VI_ENABLE_CMD_EEN		30
#define M_FW_VI_ENABLE_CMD_EEN		0x1
#define V_FW_VI_ENABLE_CMD_EEN(x)	((x) << S_FW_VI_ENABLE_CMD_EEN)
#define G_FW_VI_ENABLE_CMD_EEN(x)	\
    (((x) >> S_FW_VI_ENABLE_CMD_EEN) & M_FW_VI_ENABLE_CMD_EEN)
#define F_FW_VI_ENABLE_CMD_EEN		V_FW_VI_ENABLE_CMD_EEN(1U)

#define S_FW_VI_ENABLE_CMD_LED		29
#define M_FW_VI_ENABLE_CMD_LED		0x1
#define V_FW_VI_ENABLE_CMD_LED(x)	((x) << S_FW_VI_ENABLE_CMD_LED)
#define G_FW_VI_ENABLE_CMD_LED(x)	\
    (((x) >> S_FW_VI_ENABLE_CMD_LED) & M_FW_VI_ENABLE_CMD_LED)
#define F_FW_VI_ENABLE_CMD_LED		V_FW_VI_ENABLE_CMD_LED(1U)

#define S_FW_VI_ENABLE_CMD_DCB_INFO	28
#define M_FW_VI_ENABLE_CMD_DCB_INFO	0x1
#define V_FW_VI_ENABLE_CMD_DCB_INFO(x)	((x) << S_FW_VI_ENABLE_CMD_DCB_INFO)
#define G_FW_VI_ENABLE_CMD_DCB_INFO(x)	\
    (((x) >> S_FW_VI_ENABLE_CMD_DCB_INFO) & M_FW_VI_ENABLE_CMD_DCB_INFO)
#define F_FW_VI_ENABLE_CMD_DCB_INFO	V_FW_VI_ENABLE_CMD_DCB_INFO(1U)

/* VI VF stats offset definitions */
#define VI_VF_NUM_STATS	16
enum fw_vi_stats_vf_index {
	FW_VI_VF_STAT_TX_BCAST_BYTES_IX,
	FW_VI_VF_STAT_TX_BCAST_FRAMES_IX,
	FW_VI_VF_STAT_TX_MCAST_BYTES_IX,
	FW_VI_VF_STAT_TX_MCAST_FRAMES_IX,
	FW_VI_VF_STAT_TX_UCAST_BYTES_IX,
	FW_VI_VF_STAT_TX_UCAST_FRAMES_IX,
	FW_VI_VF_STAT_TX_DROP_FRAMES_IX,
	FW_VI_VF_STAT_TX_OFLD_BYTES_IX,
	FW_VI_VF_STAT_TX_OFLD_FRAMES_IX,
	FW_VI_VF_STAT_RX_BCAST_BYTES_IX,
	FW_VI_VF_STAT_RX_BCAST_FRAMES_IX,
	FW_VI_VF_STAT_RX_MCAST_BYTES_IX,
	FW_VI_VF_STAT_RX_MCAST_FRAMES_IX,
	FW_VI_VF_STAT_RX_UCAST_BYTES_IX,
	FW_VI_VF_STAT_RX_UCAST_FRAMES_IX,
	FW_VI_VF_STAT_RX_ERR_FRAMES_IX
};

/* VI PF stats offset definitions */
#define VI_PF_NUM_STATS	17
enum fw_vi_stats_pf_index {
	FW_VI_PF_STAT_TX_BCAST_BYTES_IX,
	FW_VI_PF_STAT_TX_BCAST_FRAMES_IX,
	FW_VI_PF_STAT_TX_MCAST_BYTES_IX,
	FW_VI_PF_STAT_TX_MCAST_FRAMES_IX,
	FW_VI_PF_STAT_TX_UCAST_BYTES_IX,
	FW_VI_PF_STAT_TX_UCAST_FRAMES_IX,
	FW_VI_PF_STAT_TX_OFLD_BYTES_IX,
	FW_VI_PF_STAT_TX_OFLD_FRAMES_IX,
	FW_VI_PF_STAT_RX_BYTES_IX,
	FW_VI_PF_STAT_RX_FRAMES_IX,
	FW_VI_PF_STAT_RX_BCAST_BYTES_IX,
	FW_VI_PF_STAT_RX_BCAST_FRAMES_IX,
	FW_VI_PF_STAT_RX_MCAST_BYTES_IX,
	FW_VI_PF_STAT_RX_MCAST_FRAMES_IX,
	FW_VI_PF_STAT_RX_UCAST_BYTES_IX,
	FW_VI_PF_STAT_RX_UCAST_FRAMES_IX,
	FW_VI_PF_STAT_RX_ERR_FRAMES_IX
};

struct fw_vi_stats_cmd {
	__be32 op_to_viid;
	__be32 retval_len16;
	union fw_vi_stats {
		struct fw_vi_stats_ctl {
			__be16 nstats_ix;
			__be16 r6;
			__be32 r7;
			__be64 stat0;
			__be64 stat1;
			__be64 stat2;
			__be64 stat3;
			__be64 stat4;
			__be64 stat5;
		} ctl;
		struct fw_vi_stats_pf {
			__be64 tx_bcast_bytes;
			__be64 tx_bcast_frames;
			__be64 tx_mcast_bytes;
			__be64 tx_mcast_frames;
			__be64 tx_ucast_bytes;
			__be64 tx_ucast_frames;
			__be64 tx_offload_bytes;
			__be64 tx_offload_frames;
			__be64 rx_pf_bytes;
			__be64 rx_pf_frames;
			__be64 rx_bcast_bytes;
			__be64 rx_bcast_frames;
			__be64 rx_mcast_bytes;
			__be64 rx_mcast_frames;
			__be64 rx_ucast_bytes;
			__be64 rx_ucast_frames;
			__be64 rx_err_frames;
		} pf;
		struct fw_vi_stats_vf {
			__be64 tx_bcast_bytes;
			__be64 tx_bcast_frames;
			__be64 tx_mcast_bytes;
			__be64 tx_mcast_frames;
			__be64 tx_ucast_bytes;
			__be64 tx_ucast_frames;
			__be64 tx_drop_frames;
			__be64 tx_offload_bytes;
			__be64 tx_offload_frames;
			__be64 rx_bcast_bytes;
			__be64 rx_bcast_frames;
			__be64 rx_mcast_bytes;
			__be64 rx_mcast_frames;
			__be64 rx_ucast_bytes;
			__be64 rx_ucast_frames;
			__be64 rx_err_frames;
		} vf;
	} u;
};

#define S_FW_VI_STATS_CMD_VIID		0
#define M_FW_VI_STATS_CMD_VIID		0xfff
#define V_FW_VI_STATS_CMD_VIID(x)	((x) << S_FW_VI_STATS_CMD_VIID)
#define G_FW_VI_STATS_CMD_VIID(x)	\
    (((x) >> S_FW_VI_STATS_CMD_VIID) & M_FW_VI_STATS_CMD_VIID)

#define S_FW_VI_STATS_CMD_NSTATS	12
#define M_FW_VI_STATS_CMD_NSTATS	0x7
#define V_FW_VI_STATS_CMD_NSTATS(x)	((x) << S_FW_VI_STATS_CMD_NSTATS)
#define G_FW_VI_STATS_CMD_NSTATS(x)	\
    (((x) >> S_FW_VI_STATS_CMD_NSTATS) & M_FW_VI_STATS_CMD_NSTATS)

#define S_FW_VI_STATS_CMD_IX		0
#define M_FW_VI_STATS_CMD_IX		0x1f
#define V_FW_VI_STATS_CMD_IX(x)		((x) << S_FW_VI_STATS_CMD_IX)
#define G_FW_VI_STATS_CMD_IX(x)		\
    (((x) >> S_FW_VI_STATS_CMD_IX) & M_FW_VI_STATS_CMD_IX)

struct fw_acl_mac_cmd {
	__be32 op_to_vfn;
	__be32 en_to_len16;
	__u8   nmac;
	__u8   r3[7];
	__be16 r4;
	__u8   macaddr0[6];
	__be16 r5;
	__u8   macaddr1[6];
	__be16 r6;
	__u8   macaddr2[6];
	__be16 r7;
	__u8   macaddr3[6];
};

#define S_FW_ACL_MAC_CMD_PFN		8
#define M_FW_ACL_MAC_CMD_PFN		0x7
#define V_FW_ACL_MAC_CMD_PFN(x)		((x) << S_FW_ACL_MAC_CMD_PFN)
#define G_FW_ACL_MAC_CMD_PFN(x)		\
    (((x) >> S_FW_ACL_MAC_CMD_PFN) & M_FW_ACL_MAC_CMD_PFN)

#define S_FW_ACL_MAC_CMD_VFN		0
#define M_FW_ACL_MAC_CMD_VFN		0xff
#define V_FW_ACL_MAC_CMD_VFN(x)		((x) << S_FW_ACL_MAC_CMD_VFN)
#define G_FW_ACL_MAC_CMD_VFN(x)		\
    (((x) >> S_FW_ACL_MAC_CMD_VFN) & M_FW_ACL_MAC_CMD_VFN)

#define S_FW_ACL_MAC_CMD_EN		31
#define M_FW_ACL_MAC_CMD_EN		0x1
#define V_FW_ACL_MAC_CMD_EN(x)		((x) << S_FW_ACL_MAC_CMD_EN)
#define G_FW_ACL_MAC_CMD_EN(x)		\
    (((x) >> S_FW_ACL_MAC_CMD_EN) & M_FW_ACL_MAC_CMD_EN)
#define F_FW_ACL_MAC_CMD_EN		V_FW_ACL_MAC_CMD_EN(1U)

struct fw_acl_vlan_cmd {
	__be32 op_to_vfn;
	__be32 en_to_len16;
	__u8   nvlan;
	__u8   dropnovlan_fm;
	__u8   r3_lo[6];
	__be16 vlanid[16];
};

#define S_FW_ACL_VLAN_CMD_PFN		8
#define M_FW_ACL_VLAN_CMD_PFN		0x7
#define V_FW_ACL_VLAN_CMD_PFN(x)	((x) << S_FW_ACL_VLAN_CMD_PFN)
#define G_FW_ACL_VLAN_CMD_PFN(x)	\
    (((x) >> S_FW_ACL_VLAN_CMD_PFN) & M_FW_ACL_VLAN_CMD_PFN)

#define S_FW_ACL_VLAN_CMD_VFN		0
#define M_FW_ACL_VLAN_CMD_VFN		0xff
#define V_FW_ACL_VLAN_CMD_VFN(x)	((x) << S_FW_ACL_VLAN_CMD_VFN)
#define G_FW_ACL_VLAN_CMD_VFN(x)	\
    (((x) >> S_FW_ACL_VLAN_CMD_VFN) & M_FW_ACL_VLAN_CMD_VFN)

#define S_FW_ACL_VLAN_CMD_EN		31
#define M_FW_ACL_VLAN_CMD_EN		0x1
#define V_FW_ACL_VLAN_CMD_EN(x)		((x) << S_FW_ACL_VLAN_CMD_EN)
#define G_FW_ACL_VLAN_CMD_EN(x)		\
    (((x) >> S_FW_ACL_VLAN_CMD_EN) & M_FW_ACL_VLAN_CMD_EN)
#define F_FW_ACL_VLAN_CMD_EN		V_FW_ACL_VLAN_CMD_EN(1U)

#define S_FW_ACL_VLAN_CMD_TRANSPARENT	30
#define M_FW_ACL_VLAN_CMD_TRANSPARENT	0x1
#define V_FW_ACL_VLAN_CMD_TRANSPARENT(x) \
    ((x) << S_FW_ACL_VLAN_CMD_TRANSPARENT)
#define G_FW_ACL_VLAN_CMD_TRANSPARENT(x) \
    (((x) >> S_FW_ACL_VLAN_CMD_TRANSPARENT) & M_FW_ACL_VLAN_CMD_TRANSPARENT)
#define F_FW_ACL_VLAN_CMD_TRANSPARENT	V_FW_ACL_VLAN_CMD_TRANSPARENT(1U)

#define S_FW_ACL_VLAN_CMD_PMASK		16
#define M_FW_ACL_VLAN_CMD_PMASK		0xf
#define V_FW_ACL_VLAN_CMD_PMASK(x)	((x) << S_FW_ACL_VLAN_CMD_PMASK)
#define G_FW_ACL_VLAN_CMD_PMASK(x)	\
    (((x) >> S_FW_ACL_VLAN_CMD_PMASK) & M_FW_ACL_VLAN_CMD_PMASK)

#define S_FW_ACL_VLAN_CMD_DROPNOVLAN	7
#define M_FW_ACL_VLAN_CMD_DROPNOVLAN	0x1
#define V_FW_ACL_VLAN_CMD_DROPNOVLAN(x)	((x) << S_FW_ACL_VLAN_CMD_DROPNOVLAN)
#define G_FW_ACL_VLAN_CMD_DROPNOVLAN(x)	\
    (((x) >> S_FW_ACL_VLAN_CMD_DROPNOVLAN) & M_FW_ACL_VLAN_CMD_DROPNOVLAN)
#define F_FW_ACL_VLAN_CMD_DROPNOVLAN	V_FW_ACL_VLAN_CMD_DROPNOVLAN(1U)

#define S_FW_ACL_VLAN_CMD_FM		6
#define M_FW_ACL_VLAN_CMD_FM		0x1
#define V_FW_ACL_VLAN_CMD_FM(x)		((x) << S_FW_ACL_VLAN_CMD_FM)
#define G_FW_ACL_VLAN_CMD_FM(x)		\
    (((x) >> S_FW_ACL_VLAN_CMD_FM) & M_FW_ACL_VLAN_CMD_FM)
#define F_FW_ACL_VLAN_CMD_FM		V_FW_ACL_VLAN_CMD_FM(1U)

/* old 16-bit port capabilities bitmap (fw_port_cap16_t) */
enum fw_port_cap {
	FW_PORT_CAP_SPEED_100M		= 0x0001,
	FW_PORT_CAP_SPEED_1G		= 0x0002,
	FW_PORT_CAP_SPEED_25G		= 0x0004,
	FW_PORT_CAP_SPEED_10G		= 0x0008,
	FW_PORT_CAP_SPEED_40G		= 0x0010,
	FW_PORT_CAP_SPEED_100G		= 0x0020,
	FW_PORT_CAP_FC_RX		= 0x0040,
	FW_PORT_CAP_FC_TX		= 0x0080,
	FW_PORT_CAP_ANEG		= 0x0100,
	FW_PORT_CAP_MDIAUTO		= 0x0200,
	FW_PORT_CAP_MDISTRAIGHT		= 0x0400,
	FW_PORT_CAP_FEC_RS		= 0x0800,
	FW_PORT_CAP_FEC_BASER_RS	= 0x1000,
	FW_PORT_CAP_FORCE_PAUSE		= 0x2000,
	FW_PORT_CAP_802_3_PAUSE		= 0x4000,
	FW_PORT_CAP_802_3_ASM_DIR	= 0x8000,
};

#define S_FW_PORT_CAP_SPEED	0
#define M_FW_PORT_CAP_SPEED	0x3f
#define V_FW_PORT_CAP_SPEED(x)	((x) << S_FW_PORT_CAP_SPEED)
#define G_FW_PORT_CAP_SPEED(x) \
    (((x) >> S_FW_PORT_CAP_SPEED) & M_FW_PORT_CAP_SPEED)

#define S_FW_PORT_CAP_FC	6
#define M_FW_PORT_CAP_FC	0x3
#define V_FW_PORT_CAP_FC(x)	((x) << S_FW_PORT_CAP_FC)
#define G_FW_PORT_CAP_FC(x) \
    (((x) >> S_FW_PORT_CAP_FC) & M_FW_PORT_CAP_FC)

#define S_FW_PORT_CAP_ANEG	8
#define M_FW_PORT_CAP_ANEG	0x1
#define V_FW_PORT_CAP_ANEG(x)	((x) << S_FW_PORT_CAP_ANEG)
#define G_FW_PORT_CAP_ANEG(x) \
    (((x) >> S_FW_PORT_CAP_ANEG) & M_FW_PORT_CAP_ANEG)

#define S_FW_PORT_CAP_FEC	11
#define M_FW_PORT_CAP_FEC	0x3
#define V_FW_PORT_CAP_FEC(x)	((x) << S_FW_PORT_CAP_FEC)
#define G_FW_PORT_CAP_FEC(x) \
    (((x) >> S_FW_PORT_CAP_FEC) & M_FW_PORT_CAP_FEC)

#define S_FW_PORT_CAP_FORCE_PAUSE	13
#define M_FW_PORT_CAP_FORCE_PAUSE	0x1
#define V_FW_PORT_CAP_FORCE_PAUSE(x)	((x) << S_FW_PORT_CAP_FORCE_PAUSE)
#define G_FW_PORT_CAP_FORCE_PAUSE(x) \
    (((x) >> S_FW_PORT_CAP_FORCE_PAUSE) & M_FW_PORT_CAP_FORCE_PAUSE)

#define S_FW_PORT_CAP_802_3	14
#define M_FW_PORT_CAP_802_3	0x3
#define V_FW_PORT_CAP_802_3(x)	((x) << S_FW_PORT_CAP_802_3)
#define G_FW_PORT_CAP_802_3(x) \
    (((x) >> S_FW_PORT_CAP_802_3) & M_FW_PORT_CAP_802_3)

enum fw_port_mdi {
	FW_PORT_CAP_MDI_UNCHANGED,
	FW_PORT_CAP_MDI_AUTO,
	FW_PORT_CAP_MDI_F_STRAIGHT,
	FW_PORT_CAP_MDI_F_CROSSOVER
};

#define S_FW_PORT_CAP_MDI 9
#define M_FW_PORT_CAP_MDI 3
#define V_FW_PORT_CAP_MDI(x) ((x) << S_FW_PORT_CAP_MDI)
#define G_FW_PORT_CAP_MDI(x) (((x) >> S_FW_PORT_CAP_MDI) & M_FW_PORT_CAP_MDI)

/* new 32-bit port capabilities bitmap (fw_port_cap32_t) */
#define	FW_PORT_CAP32_SPEED_100M	0x00000001UL
#define	FW_PORT_CAP32_SPEED_1G		0x00000002UL
#define	FW_PORT_CAP32_SPEED_10G		0x00000004UL
#define	FW_PORT_CAP32_SPEED_25G		0x00000008UL
#define	FW_PORT_CAP32_SPEED_40G		0x00000010UL
#define	FW_PORT_CAP32_SPEED_50G		0x00000020UL
#define	FW_PORT_CAP32_SPEED_100G	0x00000040UL
#define	FW_PORT_CAP32_SPEED_200G	0x00000080UL
#define	FW_PORT_CAP32_SPEED_400G	0x00000100UL
#define	FW_PORT_CAP32_SPEED_RESERVED1	0x00000200UL
#define	FW_PORT_CAP32_SPEED_RESERVED2	0x00000400UL
#define	FW_PORT_CAP32_SPEED_RESERVED3	0x00000800UL
#define	FW_PORT_CAP32_RESERVED1		0x0000f000UL
#define	FW_PORT_CAP32_FC_RX		0x00010000UL
#define	FW_PORT_CAP32_FC_TX		0x00020000UL
#define	FW_PORT_CAP32_802_3_PAUSE	0x00040000UL
#define	FW_PORT_CAP32_802_3_ASM_DIR	0x00080000UL
#define	FW_PORT_CAP32_ANEG		0x00100000UL
#define	FW_PORT_CAP32_MDIAUTO		0x00200000UL
#define	FW_PORT_CAP32_MDISTRAIGHT	0x00400000UL
#define	FW_PORT_CAP32_FEC_RS		0x00800000UL
#define	FW_PORT_CAP32_FEC_BASER_RS	0x01000000UL
#define	FW_PORT_CAP32_FEC_RESERVED1	0x02000000UL
#define	FW_PORT_CAP32_FEC_RESERVED2	0x04000000UL
#define	FW_PORT_CAP32_FEC_RESERVED3	0x08000000UL
#define	FW_PORT_CAP32_FORCE_PAUSE	0x10000000UL
#define	FW_PORT_CAP32_RESERVED2		0xe0000000UL

#define S_FW_PORT_CAP32_SPEED	0
#define M_FW_PORT_CAP32_SPEED	0xfff
#define V_FW_PORT_CAP32_SPEED(x)	((x) << S_FW_PORT_CAP32_SPEED)
#define G_FW_PORT_CAP32_SPEED(x) \
    (((x) >> S_FW_PORT_CAP32_SPEED) & M_FW_PORT_CAP32_SPEED)

#define S_FW_PORT_CAP32_FC	16
#define M_FW_PORT_CAP32_FC	0x3
#define V_FW_PORT_CAP32_FC(x)	((x) << S_FW_PORT_CAP32_FC)
#define G_FW_PORT_CAP32_FC(x) \
    (((x) >> S_FW_PORT_CAP32_FC) & M_FW_PORT_CAP32_FC)

#define S_FW_PORT_CAP32_802_3	18
#define M_FW_PORT_CAP32_802_3	0x3
#define V_FW_PORT_CAP32_802_3(x)	((x) << S_FW_PORT_CAP32_802_3)
#define G_FW_PORT_CAP32_802_3(x) \
    (((x) >> S_FW_PORT_CAP32_802_3) & M_FW_PORT_CAP32_802_3)

#define S_FW_PORT_CAP32_ANEG	20
#define M_FW_PORT_CAP32_ANEG	0x1
#define V_FW_PORT_CAP32_ANEG(x)	((x) << S_FW_PORT_CAP32_ANEG)
#define G_FW_PORT_CAP32_ANEG(x) \
    (((x) >> S_FW_PORT_CAP32_ANEG) & M_FW_PORT_CAP32_ANEG)

#define S_FW_PORT_CAP32_FORCE_PAUSE	28
#define M_FW_PORT_CAP32_FORCE_PAUSE	0x1
#define V_FW_PORT_CAP32_FORCE_PAUSE(x)	((x) << S_FW_PORT_CAP32_FORCE_PAUSE)
#define G_FW_PORT_CAP32_FORCE_PAUSE(x) \
    (((x) >> S_FW_PORT_CAP32_FORCE_PAUSE) & M_FW_PORT_CAP32_FORCE_PAUSE)

enum fw_port_mdi32 {
	FW_PORT_CAP32_MDI_UNCHANGED,
	FW_PORT_CAP32_MDI_AUTO,
	FW_PORT_CAP32_MDI_F_STRAIGHT,
	FW_PORT_CAP32_MDI_F_CROSSOVER
};

#define S_FW_PORT_CAP32_MDI 21
#define M_FW_PORT_CAP32_MDI 3
#define V_FW_PORT_CAP32_MDI(x) ((x) << S_FW_PORT_CAP32_MDI)
#define G_FW_PORT_CAP32_MDI(x) \
    (((x) >> S_FW_PORT_CAP32_MDI) & M_FW_PORT_CAP32_MDI)

#define S_FW_PORT_CAP32_FEC	23
#define M_FW_PORT_CAP32_FEC	0x1f
#define V_FW_PORT_CAP32_FEC(x)	((x) << S_FW_PORT_CAP32_FEC)
#define G_FW_PORT_CAP32_FEC(x) \
    (((x) >> S_FW_PORT_CAP32_FEC) & M_FW_PORT_CAP32_FEC)

/* macros to isolate various 32-bit Port Capabilities sub-fields */
#define CAP32_SPEED(__cap32) \
	(V_FW_PORT_CAP32_SPEED(M_FW_PORT_CAP32_SPEED) & __cap32)

#define CAP32_FEC(__cap32) \
	(V_FW_PORT_CAP32_FEC(M_FW_PORT_CAP32_FEC) & __cap32)

#define CAP32_FC(__cap32) \
	(V_FW_PORT_CAP32_FC(M_FW_PORT_CAP32_FC) & __cap32)

enum fw_port_action {
	FW_PORT_ACTION_L1_CFG		= 0x0001,
	FW_PORT_ACTION_L2_CFG		= 0x0002,
	FW_PORT_ACTION_GET_PORT_INFO	= 0x0003,
	FW_PORT_ACTION_L2_PPP_CFG	= 0x0004,
	FW_PORT_ACTION_L2_DCB_CFG	= 0x0005,
	FW_PORT_ACTION_DCB_READ_TRANS	= 0x0006,
	FW_PORT_ACTION_DCB_READ_RECV	= 0x0007,
	FW_PORT_ACTION_DCB_READ_DET	= 0x0008,
	FW_PORT_ACTION_L1_CFG32		= 0x0009,
	FW_PORT_ACTION_GET_PORT_INFO32	= 0x000a,
	FW_PORT_ACTION_LOW_PWR_TO_NORMAL = 0x0010,
	FW_PORT_ACTION_L1_LOW_PWR_EN	= 0x0011,
	FW_PORT_ACTION_L2_WOL_MODE_EN	= 0x0012,
	FW_PORT_ACTION_LPBK_TO_NORMAL	= 0x0020,
	FW_PORT_ACTION_LPBK_SS_ASIC	= 0x0022,
	FW_PORT_ACTION_LPBK_WS_ASIC	= 0x0023,
	FW_PORT_ACTION_LPBK_WS_EXT_PHY	= 0x0025,
	FW_PORT_ACTION_LPBK_SS_EXT	= 0x0026,
	FW_PORT_ACTION_DIAGNOSTICS	= 0x0027,
	FW_PORT_ACTION_LPBK_SS_EXT_PHY	= 0x0028,
	FW_PORT_ACTION_PHY_RESET	= 0x0040,
	FW_PORT_ACTION_PMA_RESET	= 0x0041,
	FW_PORT_ACTION_PCS_RESET	= 0x0042,
	FW_PORT_ACTION_PHYXS_RESET	= 0x0043,
	FW_PORT_ACTION_DTEXS_REEST	= 0x0044,
	FW_PORT_ACTION_AN_RESET		= 0x0045,
};

enum fw_port_l2cfg_ctlbf {
	FW_PORT_L2_CTLBF_OVLAN0	= 0x01,
	FW_PORT_L2_CTLBF_OVLAN1	= 0x02,
	FW_PORT_L2_CTLBF_OVLAN2	= 0x04,
	FW_PORT_L2_CTLBF_OVLAN3	= 0x08,
	FW_PORT_L2_CTLBF_IVLAN	= 0x10,
	FW_PORT_L2_CTLBF_TXIPG	= 0x20,
	FW_PORT_L2_CTLBF_MTU	= 0x40
};

enum fw_dcb_app_tlv_sf {
	FW_DCB_APP_SF_ETHERTYPE,
	FW_DCB_APP_SF_SOCKET_TCP,
	FW_DCB_APP_SF_SOCKET_UDP,
	FW_DCB_APP_SF_SOCKET_ALL,
};

enum fw_port_dcb_versions {
	FW_PORT_DCB_VER_UNKNOWN,
	FW_PORT_DCB_VER_CEE1D0,
	FW_PORT_DCB_VER_CEE1D01,
	FW_PORT_DCB_VER_IEEE,
	FW_PORT_DCB_VER_AUTO=7
};

enum fw_port_dcb_cfg {
	FW_PORT_DCB_CFG_PG	= 0x01,
	FW_PORT_DCB_CFG_PFC	= 0x02,
	FW_PORT_DCB_CFG_APPL	= 0x04
};

enum fw_port_dcb_cfg_rc {
	FW_PORT_DCB_CFG_SUCCESS	= 0x0,
	FW_PORT_DCB_CFG_ERROR	= 0x1
};

enum fw_port_dcb_type {
	FW_PORT_DCB_TYPE_PGID		= 0x00,
	FW_PORT_DCB_TYPE_PGRATE		= 0x01,
	FW_PORT_DCB_TYPE_PRIORATE	= 0x02,
	FW_PORT_DCB_TYPE_PFC		= 0x03,
	FW_PORT_DCB_TYPE_APP_ID		= 0x04,
	FW_PORT_DCB_TYPE_CONTROL	= 0x05,
};

enum fw_port_dcb_feature_state {
	FW_PORT_DCB_FEATURE_STATE_PENDING = 0x0,
	FW_PORT_DCB_FEATURE_STATE_SUCCESS = 0x1,
	FW_PORT_DCB_FEATURE_STATE_ERROR	= 0x2,
	FW_PORT_DCB_FEATURE_STATE_TIMEOUT = 0x3,
};

enum fw_port_diag_ops {
	FW_PORT_DIAGS_TEMP		= 0x00,
	FW_PORT_DIAGS_TX_POWER		= 0x01,
	FW_PORT_DIAGS_RX_POWER		= 0x02,
	FW_PORT_DIAGS_TX_DIS		= 0x03,
};

struct fw_port_cmd {
	__be32 op_to_portid;
	__be32 action_to_len16;
	union fw_port {
		struct fw_port_l1cfg {
			__be32 rcap;
			__be32 r;
		} l1cfg;
		struct fw_port_l2cfg {
			__u8   ctlbf;
			__u8   ovlan3_to_ivlan0;
			__be16 ivlantype;
			__be16 txipg_force_pinfo;
			__be16 mtu;
			__be16 ovlan0mask;
			__be16 ovlan0type;
			__be16 ovlan1mask;
			__be16 ovlan1type;
			__be16 ovlan2mask;
			__be16 ovlan2type;
			__be16 ovlan3mask;
			__be16 ovlan3type;
		} l2cfg;
		struct fw_port_info {
			__be32 lstatus_to_modtype;
			__be16 pcap;
			__be16 acap;
			__be16 mtu;
			__u8   cbllen;
			__u8   auxlinfo;
			__u8   dcbxdis_pkd;
			__u8   r8_lo;
			__be16 lpacap;
			__be64 r9;
		} info;
		struct fw_port_diags {
			__u8   diagop;
			__u8   r[3];
			__be32 diagval;
		} diags;
		union fw_port_dcb {
			struct fw_port_dcb_pgid {
				__u8   type;
				__u8   apply_pkd;
				__u8   r10_lo[2];
				__be32 pgid;
				__be64 r11;
			} pgid;
			struct fw_port_dcb_pgrate {
				__u8   type;
				__u8   apply_pkd;
				__u8   r10_lo[5];
				__u8   num_tcs_supported;
				__u8   pgrate[8];
				__u8   tsa[8];
			} pgrate;
			struct fw_port_dcb_priorate {
				__u8   type;
				__u8   apply_pkd;
				__u8   r10_lo[6];
				__u8   strict_priorate[8];
			} priorate;
			struct fw_port_dcb_pfc {
				__u8   type;
				__u8   pfcen;
				__u8   apply_pkd;
				__u8   r10_lo[4];
				__u8   max_pfc_tcs;
				__be64 r11;
			} pfc;
			struct fw_port_app_priority {
				__u8   type;
				__u8   apply_pkd;
				__u8   r10_lo;
				__u8   idx;
				__u8   user_prio_map;
				__u8   sel_field;
				__be16 protocolid;
				__be64 r12;
			} app_priority;
			struct fw_port_dcb_control {
				__u8   type;
				__u8   all_syncd_pkd;
				__be16 dcb_version_to_app_state;
				__be32 r11;
				__be64 r12;
			} control;
		} dcb;
		struct fw_port_l1cfg32 {
			__be32 rcap32;
			__be32 r;
		} l1cfg32;
		struct fw_port_info32 {
			__be32 lstatus32_to_cbllen32;
			__be32 auxlinfo32_mtu32;
			__be32 linkattr32;
			__be32 pcaps32;
			__be32 acaps32;
			__be32 lpacaps32;
		} info32;
	} u;
};

#define S_FW_PORT_CMD_READ		22
#define M_FW_PORT_CMD_READ		0x1
#define V_FW_PORT_CMD_READ(x)		((x) << S_FW_PORT_CMD_READ)
#define G_FW_PORT_CMD_READ(x)		\
    (((x) >> S_FW_PORT_CMD_READ) & M_FW_PORT_CMD_READ)
#define F_FW_PORT_CMD_READ		V_FW_PORT_CMD_READ(1U)

#define S_FW_PORT_CMD_PORTID		0
#define M_FW_PORT_CMD_PORTID		0xf
#define V_FW_PORT_CMD_PORTID(x)		((x) << S_FW_PORT_CMD_PORTID)
#define G_FW_PORT_CMD_PORTID(x)		\
    (((x) >> S_FW_PORT_CMD_PORTID) & M_FW_PORT_CMD_PORTID)

#define S_FW_PORT_CMD_ACTION		16
#define M_FW_PORT_CMD_ACTION		0xffff
#define V_FW_PORT_CMD_ACTION(x)		((x) << S_FW_PORT_CMD_ACTION)
#define G_FW_PORT_CMD_ACTION(x)		\
    (((x) >> S_FW_PORT_CMD_ACTION) & M_FW_PORT_CMD_ACTION)

#define S_FW_PORT_CMD_OVLAN3		7
#define M_FW_PORT_CMD_OVLAN3		0x1
#define V_FW_PORT_CMD_OVLAN3(x)		((x) << S_FW_PORT_CMD_OVLAN3)
#define G_FW_PORT_CMD_OVLAN3(x)		\
    (((x) >> S_FW_PORT_CMD_OVLAN3) & M_FW_PORT_CMD_OVLAN3)
#define F_FW_PORT_CMD_OVLAN3		V_FW_PORT_CMD_OVLAN3(1U)

#define S_FW_PORT_CMD_OVLAN2		6
#define M_FW_PORT_CMD_OVLAN2		0x1
#define V_FW_PORT_CMD_OVLAN2(x)		((x) << S_FW_PORT_CMD_OVLAN2)
#define G_FW_PORT_CMD_OVLAN2(x)		\
    (((x) >> S_FW_PORT_CMD_OVLAN2) & M_FW_PORT_CMD_OVLAN2)
#define F_FW_PORT_CMD_OVLAN2		V_FW_PORT_CMD_OVLAN2(1U)

#define S_FW_PORT_CMD_OVLAN1		5
#define M_FW_PORT_CMD_OVLAN1		0x1
#define V_FW_PORT_CMD_OVLAN1(x)		((x) << S_FW_PORT_CMD_OVLAN1)
#define G_FW_PORT_CMD_OVLAN1(x)		\
    (((x) >> S_FW_PORT_CMD_OVLAN1) & M_FW_PORT_CMD_OVLAN1)
#define F_FW_PORT_CMD_OVLAN1		V_FW_PORT_CMD_OVLAN1(1U)

#define S_FW_PORT_CMD_OVLAN0		4
#define M_FW_PORT_CMD_OVLAN0		0x1
#define V_FW_PORT_CMD_OVLAN0(x)		((x) << S_FW_PORT_CMD_OVLAN0)
#define G_FW_PORT_CMD_OVLAN0(x)		\
    (((x) >> S_FW_PORT_CMD_OVLAN0) & M_FW_PORT_CMD_OVLAN0)
#define F_FW_PORT_CMD_OVLAN0		V_FW_PORT_CMD_OVLAN0(1U)

#define S_FW_PORT_CMD_IVLAN0		3
#define M_FW_PORT_CMD_IVLAN0		0x1
#define V_FW_PORT_CMD_IVLAN0(x)		((x) << S_FW_PORT_CMD_IVLAN0)
#define G_FW_PORT_CMD_IVLAN0(x)		\
    (((x) >> S_FW_PORT_CMD_IVLAN0) & M_FW_PORT_CMD_IVLAN0)
#define F_FW_PORT_CMD_IVLAN0		V_FW_PORT_CMD_IVLAN0(1U)

#define S_FW_PORT_CMD_TXIPG		3
#define M_FW_PORT_CMD_TXIPG		0x1fff
#define V_FW_PORT_CMD_TXIPG(x)		((x) << S_FW_PORT_CMD_TXIPG)
#define G_FW_PORT_CMD_TXIPG(x)		\
    (((x) >> S_FW_PORT_CMD_TXIPG) & M_FW_PORT_CMD_TXIPG)

#define S_FW_PORT_CMD_FORCE_PINFO	0
#define M_FW_PORT_CMD_FORCE_PINFO	0x1
#define V_FW_PORT_CMD_FORCE_PINFO(x)	((x) << S_FW_PORT_CMD_FORCE_PINFO)
#define G_FW_PORT_CMD_FORCE_PINFO(x)	\
    (((x) >> S_FW_PORT_CMD_FORCE_PINFO) & M_FW_PORT_CMD_FORCE_PINFO)
#define F_FW_PORT_CMD_FORCE_PINFO	V_FW_PORT_CMD_FORCE_PINFO(1U)

#define S_FW_PORT_CMD_LSTATUS		31
#define M_FW_PORT_CMD_LSTATUS		0x1
#define V_FW_PORT_CMD_LSTATUS(x)	((x) << S_FW_PORT_CMD_LSTATUS)
#define G_FW_PORT_CMD_LSTATUS(x)	\
    (((x) >> S_FW_PORT_CMD_LSTATUS) & M_FW_PORT_CMD_LSTATUS)
#define F_FW_PORT_CMD_LSTATUS		V_FW_PORT_CMD_LSTATUS(1U)

#define S_FW_PORT_CMD_LSPEED		24
#define M_FW_PORT_CMD_LSPEED		0x3f
#define V_FW_PORT_CMD_LSPEED(x)		((x) << S_FW_PORT_CMD_LSPEED)
#define G_FW_PORT_CMD_LSPEED(x)		\
    (((x) >> S_FW_PORT_CMD_LSPEED) & M_FW_PORT_CMD_LSPEED)

#define S_FW_PORT_CMD_TXPAUSE		23
#define M_FW_PORT_CMD_TXPAUSE		0x1
#define V_FW_PORT_CMD_TXPAUSE(x)	((x) << S_FW_PORT_CMD_TXPAUSE)
#define G_FW_PORT_CMD_TXPAUSE(x)	\
    (((x) >> S_FW_PORT_CMD_TXPAUSE) & M_FW_PORT_CMD_TXPAUSE)
#define F_FW_PORT_CMD_TXPAUSE		V_FW_PORT_CMD_TXPAUSE(1U)

#define S_FW_PORT_CMD_RXPAUSE		22
#define M_FW_PORT_CMD_RXPAUSE		0x1
#define V_FW_PORT_CMD_RXPAUSE(x)	((x) << S_FW_PORT_CMD_RXPAUSE)
#define G_FW_PORT_CMD_RXPAUSE(x)	\
    (((x) >> S_FW_PORT_CMD_RXPAUSE) & M_FW_PORT_CMD_RXPAUSE)
#define F_FW_PORT_CMD_RXPAUSE		V_FW_PORT_CMD_RXPAUSE(1U)

#define S_FW_PORT_CMD_MDIOCAP		21
#define M_FW_PORT_CMD_MDIOCAP		0x1
#define V_FW_PORT_CMD_MDIOCAP(x)	((x) << S_FW_PORT_CMD_MDIOCAP)
#define G_FW_PORT_CMD_MDIOCAP(x)	\
    (((x) >> S_FW_PORT_CMD_MDIOCAP) & M_FW_PORT_CMD_MDIOCAP)
#define F_FW_PORT_CMD_MDIOCAP		V_FW_PORT_CMD_MDIOCAP(1U)

#define S_FW_PORT_CMD_MDIOADDR		16
#define M_FW_PORT_CMD_MDIOADDR		0x1f
#define V_FW_PORT_CMD_MDIOADDR(x)	((x) << S_FW_PORT_CMD_MDIOADDR)
#define G_FW_PORT_CMD_MDIOADDR(x)	\
    (((x) >> S_FW_PORT_CMD_MDIOADDR) & M_FW_PORT_CMD_MDIOADDR)

#define S_FW_PORT_CMD_LPTXPAUSE		15
#define M_FW_PORT_CMD_LPTXPAUSE		0x1
#define V_FW_PORT_CMD_LPTXPAUSE(x)	((x) << S_FW_PORT_CMD_LPTXPAUSE)
#define G_FW_PORT_CMD_LPTXPAUSE(x)	\
    (((x) >> S_FW_PORT_CMD_LPTXPAUSE) & M_FW_PORT_CMD_LPTXPAUSE)
#define F_FW_PORT_CMD_LPTXPAUSE		V_FW_PORT_CMD_LPTXPAUSE(1U)

#define S_FW_PORT_CMD_LPRXPAUSE		14
#define M_FW_PORT_CMD_LPRXPAUSE		0x1
#define V_FW_PORT_CMD_LPRXPAUSE(x)	((x) << S_FW_PORT_CMD_LPRXPAUSE)
#define G_FW_PORT_CMD_LPRXPAUSE(x)	\
    (((x) >> S_FW_PORT_CMD_LPRXPAUSE) & M_FW_PORT_CMD_LPRXPAUSE)
#define F_FW_PORT_CMD_LPRXPAUSE		V_FW_PORT_CMD_LPRXPAUSE(1U)

#define S_FW_PORT_CMD_PTYPE		8
#define M_FW_PORT_CMD_PTYPE		0x1f
#define V_FW_PORT_CMD_PTYPE(x)		((x) << S_FW_PORT_CMD_PTYPE)
#define G_FW_PORT_CMD_PTYPE(x)		\
    (((x) >> S_FW_PORT_CMD_PTYPE) & M_FW_PORT_CMD_PTYPE)

#define S_FW_PORT_CMD_LINKDNRC		5
#define M_FW_PORT_CMD_LINKDNRC		0x7
#define V_FW_PORT_CMD_LINKDNRC(x)	((x) << S_FW_PORT_CMD_LINKDNRC)
#define G_FW_PORT_CMD_LINKDNRC(x)	\
    (((x) >> S_FW_PORT_CMD_LINKDNRC) & M_FW_PORT_CMD_LINKDNRC)

#define S_FW_PORT_CMD_MODTYPE		0
#define M_FW_PORT_CMD_MODTYPE		0x1f
#define V_FW_PORT_CMD_MODTYPE(x)	((x) << S_FW_PORT_CMD_MODTYPE)
#define G_FW_PORT_CMD_MODTYPE(x)	\
    (((x) >> S_FW_PORT_CMD_MODTYPE) & M_FW_PORT_CMD_MODTYPE)

#define S_FW_PORT_AUXLINFO_KX4	2
#define M_FW_PORT_AUXLINFO_KX4	0x1
#define V_FW_PORT_AUXLINFO_KX4(x) \
    ((x) << S_FW_PORT_AUXLINFO_KX4)
#define G_FW_PORT_AUXLINFO_KX4(x) \
    (((x) >> S_FW_PORT_AUXLINFO_KX4) & M_FW_PORT_AUXLINFO_KX4)
#define F_FW_PORT_AUXLINFO_KX4	V_FW_PORT_AUXLINFO_KX4(1U)

#define S_FW_PORT_AUXLINFO_KR	1
#define M_FW_PORT_AUXLINFO_KR	0x1
#define V_FW_PORT_AUXLINFO_KR(x) \
    ((x) << S_FW_PORT_AUXLINFO_KR)
#define G_FW_PORT_AUXLINFO_KR(x) \
    (((x) >> S_FW_PORT_AUXLINFO_KR) & M_FW_PORT_AUXLINFO_KR)
#define F_FW_PORT_AUXLINFO_KR	V_FW_PORT_AUXLINFO_KR(1U)

#define S_FW_PORT_CMD_DCBXDIS		7
#define M_FW_PORT_CMD_DCBXDIS		0x1
#define V_FW_PORT_CMD_DCBXDIS(x)	((x) << S_FW_PORT_CMD_DCBXDIS)
#define G_FW_PORT_CMD_DCBXDIS(x)	\
    (((x) >> S_FW_PORT_CMD_DCBXDIS) & M_FW_PORT_CMD_DCBXDIS)
#define F_FW_PORT_CMD_DCBXDIS		V_FW_PORT_CMD_DCBXDIS(1U)

#define S_FW_PORT_CMD_APPLY		7
#define M_FW_PORT_CMD_APPLY		0x1
#define V_FW_PORT_CMD_APPLY(x)		((x) << S_FW_PORT_CMD_APPLY)
#define G_FW_PORT_CMD_APPLY(x)		\
    (((x) >> S_FW_PORT_CMD_APPLY) & M_FW_PORT_CMD_APPLY)
#define F_FW_PORT_CMD_APPLY		V_FW_PORT_CMD_APPLY(1U)

#define S_FW_PORT_CMD_ALL_SYNCD		7
#define M_FW_PORT_CMD_ALL_SYNCD		0x1
#define V_FW_PORT_CMD_ALL_SYNCD(x)	((x) << S_FW_PORT_CMD_ALL_SYNCD)
#define G_FW_PORT_CMD_ALL_SYNCD(x)	\
    (((x) >> S_FW_PORT_CMD_ALL_SYNCD) & M_FW_PORT_CMD_ALL_SYNCD)
#define F_FW_PORT_CMD_ALL_SYNCD		V_FW_PORT_CMD_ALL_SYNCD(1U)

#define S_FW_PORT_CMD_DCB_VERSION	12
#define M_FW_PORT_CMD_DCB_VERSION	0x7
#define V_FW_PORT_CMD_DCB_VERSION(x)	((x) << S_FW_PORT_CMD_DCB_VERSION)
#define G_FW_PORT_CMD_DCB_VERSION(x)	\
    (((x) >> S_FW_PORT_CMD_DCB_VERSION) & M_FW_PORT_CMD_DCB_VERSION)

#define S_FW_PORT_CMD_PFC_STATE		8
#define M_FW_PORT_CMD_PFC_STATE		0xf
#define V_FW_PORT_CMD_PFC_STATE(x)	((x) << S_FW_PORT_CMD_PFC_STATE)
#define G_FW_PORT_CMD_PFC_STATE(x)	\
    (((x) >> S_FW_PORT_CMD_PFC_STATE) & M_FW_PORT_CMD_PFC_STATE)

#define S_FW_PORT_CMD_ETS_STATE		4
#define M_FW_PORT_CMD_ETS_STATE		0xf
#define V_FW_PORT_CMD_ETS_STATE(x)	((x) << S_FW_PORT_CMD_ETS_STATE)
#define G_FW_PORT_CMD_ETS_STATE(x)	\
    (((x) >> S_FW_PORT_CMD_ETS_STATE) & M_FW_PORT_CMD_ETS_STATE)

#define S_FW_PORT_CMD_APP_STATE		0
#define M_FW_PORT_CMD_APP_STATE		0xf
#define V_FW_PORT_CMD_APP_STATE(x)	((x) << S_FW_PORT_CMD_APP_STATE)
#define G_FW_PORT_CMD_APP_STATE(x)	\
    (((x) >> S_FW_PORT_CMD_APP_STATE) & M_FW_PORT_CMD_APP_STATE)

#define S_FW_PORT_CMD_LSTATUS32		31
#define M_FW_PORT_CMD_LSTATUS32		0x1
#define V_FW_PORT_CMD_LSTATUS32(x)	((x) << S_FW_PORT_CMD_LSTATUS32)
#define G_FW_PORT_CMD_LSTATUS32(x)	\
    (((x) >> S_FW_PORT_CMD_LSTATUS32) & M_FW_PORT_CMD_LSTATUS32)
#define F_FW_PORT_CMD_LSTATUS32	V_FW_PORT_CMD_LSTATUS32(1U)

#define S_FW_PORT_CMD_LINKDNRC32	28
#define M_FW_PORT_CMD_LINKDNRC32	0x7
#define V_FW_PORT_CMD_LINKDNRC32(x)	((x) << S_FW_PORT_CMD_LINKDNRC32)
#define G_FW_PORT_CMD_LINKDNRC32(x)	\
    (((x) >> S_FW_PORT_CMD_LINKDNRC32) & M_FW_PORT_CMD_LINKDNRC32)

#define S_FW_PORT_CMD_DCBXDIS32		27
#define M_FW_PORT_CMD_DCBXDIS32		0x1
#define V_FW_PORT_CMD_DCBXDIS32(x)	((x) << S_FW_PORT_CMD_DCBXDIS32)
#define G_FW_PORT_CMD_DCBXDIS32(x)	\
    (((x) >> S_FW_PORT_CMD_DCBXDIS32) & M_FW_PORT_CMD_DCBXDIS32)
#define F_FW_PORT_CMD_DCBXDIS32	V_FW_PORT_CMD_DCBXDIS32(1U)

#define S_FW_PORT_CMD_MDIOCAP32		26
#define M_FW_PORT_CMD_MDIOCAP32		0x1
#define V_FW_PORT_CMD_MDIOCAP32(x)	((x) << S_FW_PORT_CMD_MDIOCAP32)
#define G_FW_PORT_CMD_MDIOCAP32(x)	\
    (((x) >> S_FW_PORT_CMD_MDIOCAP32) & M_FW_PORT_CMD_MDIOCAP32)
#define F_FW_PORT_CMD_MDIOCAP32	V_FW_PORT_CMD_MDIOCAP32(1U)

#define S_FW_PORT_CMD_MDIOADDR32	21
#define M_FW_PORT_CMD_MDIOADDR32	0x1f
#define V_FW_PORT_CMD_MDIOADDR32(x)	((x) << S_FW_PORT_CMD_MDIOADDR32)
#define G_FW_PORT_CMD_MDIOADDR32(x)	\
    (((x) >> S_FW_PORT_CMD_MDIOADDR32) & M_FW_PORT_CMD_MDIOADDR32)

#define S_FW_PORT_CMD_PORTTYPE32	13
#define M_FW_PORT_CMD_PORTTYPE32	0xff
#define V_FW_PORT_CMD_PORTTYPE32(x)	((x) << S_FW_PORT_CMD_PORTTYPE32)
#define G_FW_PORT_CMD_PORTTYPE32(x)	\
    (((x) >> S_FW_PORT_CMD_PORTTYPE32) & M_FW_PORT_CMD_PORTTYPE32)

#define S_FW_PORT_CMD_MODTYPE32		8
#define M_FW_PORT_CMD_MODTYPE32		0x1f
#define V_FW_PORT_CMD_MODTYPE32(x)	((x) << S_FW_PORT_CMD_MODTYPE32)
#define G_FW_PORT_CMD_MODTYPE32(x)	\
    (((x) >> S_FW_PORT_CMD_MODTYPE32) & M_FW_PORT_CMD_MODTYPE32)

#define S_FW_PORT_CMD_CBLLEN32		0
#define M_FW_PORT_CMD_CBLLEN32		0xff
#define V_FW_PORT_CMD_CBLLEN32(x)	((x) << S_FW_PORT_CMD_CBLLEN32)
#define G_FW_PORT_CMD_CBLLEN32(x)	\
    (((x) >> S_FW_PORT_CMD_CBLLEN32) & M_FW_PORT_CMD_CBLLEN32)

#define S_FW_PORT_CMD_AUXLINFO32	24
#define M_FW_PORT_CMD_AUXLINFO32	0xff
#define V_FW_PORT_CMD_AUXLINFO32(x)	((x) << S_FW_PORT_CMD_AUXLINFO32)
#define G_FW_PORT_CMD_AUXLINFO32(x)	\
    (((x) >> S_FW_PORT_CMD_AUXLINFO32) & M_FW_PORT_CMD_AUXLINFO32)

#define S_FW_PORT_AUXLINFO32_KX4	2
#define M_FW_PORT_AUXLINFO32_KX4	0x1
#define V_FW_PORT_AUXLINFO32_KX4(x) \
    ((x) << S_FW_PORT_AUXLINFO32_KX4)
#define G_FW_PORT_AUXLINFO32_KX4(x) \
    (((x) >> S_FW_PORT_AUXLINFO32_KX4) & M_FW_PORT_AUXLINFO32_KX4)
#define F_FW_PORT_AUXLINFO32_KX4	V_FW_PORT_AUXLINFO32_KX4(1U)

#define S_FW_PORT_AUXLINFO32_KR	1
#define M_FW_PORT_AUXLINFO32_KR	0x1
#define V_FW_PORT_AUXLINFO32_KR(x) \
    ((x) << S_FW_PORT_AUXLINFO32_KR)
#define G_FW_PORT_AUXLINFO32_KR(x) \
    (((x) >> S_FW_PORT_AUXLINFO32_KR) & M_FW_PORT_AUXLINFO32_KR)
#define F_FW_PORT_AUXLINFO32_KR	V_FW_PORT_AUXLINFO32_KR(1U)

#define S_FW_PORT_CMD_MTU32	0
#define M_FW_PORT_CMD_MTU32	0xffff
#define V_FW_PORT_CMD_MTU32(x)	((x) << S_FW_PORT_CMD_MTU32)
#define G_FW_PORT_CMD_MTU32(x)	\
    (((x) >> S_FW_PORT_CMD_MTU32) & M_FW_PORT_CMD_MTU32)

/*
 *	These are configured into the VPD and hence tools that generate
 *	VPD may use this enumeration.
 *	extPHY	#lanes	T4_I2C	extI2C	BP_Eq	BP_ANEG	Speed
 *
 *	REMEMBER:
 *	    Update the Common Code t4_hw.c:t4_get_port_type_description()
 *	    with any new Firmware Port Technology Types!
 */
enum fw_port_type {
	FW_PORT_TYPE_FIBER_XFI	=  0,	/* Y, 1, N, Y, N, N, 10G */
	FW_PORT_TYPE_FIBER_XAUI	=  1,	/* Y, 4, N, Y, N, N, 10G */
	FW_PORT_TYPE_BT_SGMII	=  2,	/* Y, 1, No, No, No, No, 1G/100M */
	FW_PORT_TYPE_BT_XFI	=  3,	/* Y, 1, No, No, No, No, 10G/1G/100M */
	FW_PORT_TYPE_BT_XAUI	=  4,	/* Y, 4, No, No, No, No, 10G/1G/100M */
	FW_PORT_TYPE_KX4	=  5,	/* No, 4, No, No, Yes, Yes, 10G */
	FW_PORT_TYPE_CX4	=  6,	/* No, 4, No, No, No, No, 10G */
	FW_PORT_TYPE_KX		=  7,	/* No, 1, No, No, Yes, No, 1G */
	FW_PORT_TYPE_KR		=  8,	/* No, 1, No, No, Yes, Yes, 10G */
	FW_PORT_TYPE_SFP	=  9,	/* No, 1, Yes, No, No, No, 10G */
	FW_PORT_TYPE_BP_AP	= 10,	/* No, 1, No, No, Yes, Yes, 10G, BP ANGE */
	FW_PORT_TYPE_BP4_AP	= 11,	/* No, 4, No, No, Yes, Yes, 10G, BP ANGE */
	FW_PORT_TYPE_QSFP_10G	= 12,	/* No, 1, Yes, No, No, No, 10G */
	FW_PORT_TYPE_QSA	= 13,	/* No, 1, Yes, No, No, No, 10G */
	FW_PORT_TYPE_QSFP	= 14,	/* No, 4, Yes, No, No, No, 40G */
	FW_PORT_TYPE_BP40_BA	= 15,	/* No, 4, No, No, Yes, Yes, 40G/10G/1G, BP ANGE */
	FW_PORT_TYPE_KR4_100G	= 16,	/* No, 4, 100G/40G/25G, Backplane */
	FW_PORT_TYPE_CR4_QSFP	= 17,	/* No, 4, 100G/40G/25G */
	FW_PORT_TYPE_CR_QSFP	= 18,	/* No, 1, 25G Spider cable */
	FW_PORT_TYPE_CR2_QSFP	= 19,	/* No, 2, 50G */
	FW_PORT_TYPE_SFP28	= 20,	/* No, 1, 25G/10G/1G */
	FW_PORT_TYPE_KR_SFP28	= 21,	/* No, 1, 25G/10G/1G using Backplane */
	FW_PORT_TYPE_KR_XLAUI	= 22,	/* No, 4, 40G/10G/1G, No AN*/
	FW_PORT_TYPE_NONE = M_FW_PORT_CMD_PTYPE
};

/* These are read from module's EEPROM and determined once the
   module is inserted. */
enum fw_port_module_type {
	FW_PORT_MOD_TYPE_NA		= 0x0,
	FW_PORT_MOD_TYPE_LR		= 0x1,
	FW_PORT_MOD_TYPE_SR		= 0x2,
	FW_PORT_MOD_TYPE_ER		= 0x3,
	FW_PORT_MOD_TYPE_TWINAX_PASSIVE	= 0x4,
	FW_PORT_MOD_TYPE_TWINAX_ACTIVE	= 0x5,
	FW_PORT_MOD_TYPE_LRM		= 0x6,
	FW_PORT_MOD_TYPE_ERROR		= M_FW_PORT_CMD_MODTYPE - 3,
	FW_PORT_MOD_TYPE_UNKNOWN	= M_FW_PORT_CMD_MODTYPE - 2,
	FW_PORT_MOD_TYPE_NOTSUPPORTED	= M_FW_PORT_CMD_MODTYPE - 1,
	FW_PORT_MOD_TYPE_NONE		= M_FW_PORT_CMD_MODTYPE
};

/* used by FW and tools may use this to generate VPD */
enum fw_port_mod_sub_type {
	FW_PORT_MOD_SUB_TYPE_NA,
	FW_PORT_MOD_SUB_TYPE_MV88E114X=0x1,
	FW_PORT_MOD_SUB_TYPE_TN8022=0x2,
	FW_PORT_MOD_SUB_TYPE_AQ1202=0x3,
	FW_PORT_MOD_SUB_TYPE_88x3120=0x4,
	FW_PORT_MOD_SUB_TYPE_BCM84834=0x5,
	FW_PORT_MOD_SUB_TYPE_BCM5482=0x6,
	FW_PORT_MOD_SUB_TYPE_BCM84856=0x7,
	FW_PORT_MOD_SUB_TYPE_BT_VSC8634=0x8,

	/*
	 * The following will never been in the VPD.  They are TWINAX cable
	 * lengths decoded from SFP+ module i2c PROMs.  These should almost
	 * certainly go somewhere else ...
	 */
	FW_PORT_MOD_SUB_TYPE_TWINAX_1=0x9,
	FW_PORT_MOD_SUB_TYPE_TWINAX_3=0xA,
	FW_PORT_MOD_SUB_TYPE_TWINAX_5=0xB,
	FW_PORT_MOD_SUB_TYPE_TWINAX_7=0xC,
};

/* link down reason codes (3b) */
enum fw_port_link_dn_rc {
	FW_PORT_LINK_DN_RC_NONE,
	FW_PORT_LINK_DN_RC_REMFLT,	/* Remote fault detected */
	FW_PORT_LINK_DN_ANEG_F,		/* Auto-negotiation fault */
	FW_PORT_LINK_DN_RESERVED3,
	FW_PORT_LINK_DN_OVERHEAT,	/* Port overheated */
	FW_PORT_LINK_DN_UNKNOWN,	/* Unable to determine reason */
	FW_PORT_LINK_DN_RX_LOS,		/* No RX signal detected */
	FW_PORT_LINK_DN_RESERVED7
};
enum fw_port_stats_tx_index {
	FW_STAT_TX_PORT_BYTES_IX = 0,
	FW_STAT_TX_PORT_FRAMES_IX,
	FW_STAT_TX_PORT_BCAST_IX,
	FW_STAT_TX_PORT_MCAST_IX,
	FW_STAT_TX_PORT_UCAST_IX,
	FW_STAT_TX_PORT_ERROR_IX,
	FW_STAT_TX_PORT_64B_IX,
	FW_STAT_TX_PORT_65B_127B_IX,
	FW_STAT_TX_PORT_128B_255B_IX,
	FW_STAT_TX_PORT_256B_511B_IX,
	FW_STAT_TX_PORT_512B_1023B_IX,
	FW_STAT_TX_PORT_1024B_1518B_IX,
	FW_STAT_TX_PORT_1519B_MAX_IX,
	FW_STAT_TX_PORT_DROP_IX,
	FW_STAT_TX_PORT_PAUSE_IX,
	FW_STAT_TX_PORT_PPP0_IX,
	FW_STAT_TX_PORT_PPP1_IX,
	FW_STAT_TX_PORT_PPP2_IX,
	FW_STAT_TX_PORT_PPP3_IX,
	FW_STAT_TX_PORT_PPP4_IX,
	FW_STAT_TX_PORT_PPP5_IX,
	FW_STAT_TX_PORT_PPP6_IX,
	FW_STAT_TX_PORT_PPP7_IX,
	FW_NUM_PORT_TX_STATS
};

enum fw_port_stat_rx_index {
	FW_STAT_RX_PORT_BYTES_IX = 0,
	FW_STAT_RX_PORT_FRAMES_IX,
	FW_STAT_RX_PORT_BCAST_IX,
	FW_STAT_RX_PORT_MCAST_IX,
	FW_STAT_RX_PORT_UCAST_IX,
	FW_STAT_RX_PORT_MTU_ERROR_IX,
	FW_STAT_RX_PORT_MTU_CRC_ERROR_IX,
	FW_STAT_RX_PORT_CRC_ERROR_IX,
	FW_STAT_RX_PORT_LEN_ERROR_IX,
	FW_STAT_RX_PORT_SYM_ERROR_IX,
	FW_STAT_RX_PORT_64B_IX,
	FW_STAT_RX_PORT_65B_127B_IX,
	FW_STAT_RX_PORT_128B_255B_IX,
	FW_STAT_RX_PORT_256B_511B_IX,
	FW_STAT_RX_PORT_512B_1023B_IX,
	FW_STAT_RX_PORT_1024B_1518B_IX,
	FW_STAT_RX_PORT_1519B_MAX_IX,
	FW_STAT_RX_PORT_PAUSE_IX,
	FW_STAT_RX_PORT_PPP0_IX,
	FW_STAT_RX_PORT_PPP1_IX,
	FW_STAT_RX_PORT_PPP2_IX,
	FW_STAT_RX_PORT_PPP3_IX,
	FW_STAT_RX_PORT_PPP4_IX,
	FW_STAT_RX_PORT_PPP5_IX,
	FW_STAT_RX_PORT_PPP6_IX,
	FW_STAT_RX_PORT_PPP7_IX,
	FW_STAT_RX_PORT_LESS_64B_IX,
        FW_STAT_RX_PORT_MAC_ERROR_IX,
        FW_NUM_PORT_RX_STATS
};
/* port stats */
#define FW_NUM_PORT_STATS (FW_NUM_PORT_TX_STATS + \
                                 FW_NUM_PORT_RX_STATS)


struct fw_port_stats_cmd {
	__be32 op_to_portid;
	__be32 retval_len16;
	union fw_port_stats {
		struct fw_port_stats_ctl {
			__u8   nstats_bg_bm;
			__u8   tx_ix;
			__be16 r6;
			__be32 r7;
			__be64 stat0;
			__be64 stat1;
			__be64 stat2;
			__be64 stat3;
			__be64 stat4;
			__be64 stat5;
		} ctl;
		struct fw_port_stats_all {
			__be64 tx_bytes;
			__be64 tx_frames;
			__be64 tx_bcast;
			__be64 tx_mcast;
			__be64 tx_ucast;
			__be64 tx_error;
			__be64 tx_64b;
			__be64 tx_65b_127b;
			__be64 tx_128b_255b;
			__be64 tx_256b_511b;
			__be64 tx_512b_1023b;
			__be64 tx_1024b_1518b;
			__be64 tx_1519b_max;
			__be64 tx_drop;
			__be64 tx_pause;
			__be64 tx_ppp0;
			__be64 tx_ppp1;
			__be64 tx_ppp2;
			__be64 tx_ppp3;
			__be64 tx_ppp4;
			__be64 tx_ppp5;
			__be64 tx_ppp6;
			__be64 tx_ppp7;
			__be64 rx_bytes;
			__be64 rx_frames;
			__be64 rx_bcast;
			__be64 rx_mcast;
			__be64 rx_ucast;
			__be64 rx_mtu_error;
			__be64 rx_mtu_crc_error;
			__be64 rx_crc_error;
			__be64 rx_len_error;
			__be64 rx_sym_error;
			__be64 rx_64b;
			__be64 rx_65b_127b;
			__be64 rx_128b_255b;
			__be64 rx_256b_511b;
			__be64 rx_512b_1023b;
			__be64 rx_1024b_1518b;
			__be64 rx_1519b_max;
			__be64 rx_pause;
			__be64 rx_ppp0;
			__be64 rx_ppp1;
			__be64 rx_ppp2;
			__be64 rx_ppp3;
			__be64 rx_ppp4;
			__be64 rx_ppp5;
			__be64 rx_ppp6;
			__be64 rx_ppp7;
			__be64 rx_less_64b;
			__be64 rx_bg_drop;
			__be64 rx_bg_trunc;
		} all;
	} u;
};

#define S_FW_PORT_STATS_CMD_NSTATS	4
#define M_FW_PORT_STATS_CMD_NSTATS	0x7
#define V_FW_PORT_STATS_CMD_NSTATS(x)	((x) << S_FW_PORT_STATS_CMD_NSTATS)
#define G_FW_PORT_STATS_CMD_NSTATS(x)	\
    (((x) >> S_FW_PORT_STATS_CMD_NSTATS) & M_FW_PORT_STATS_CMD_NSTATS)

#define S_FW_PORT_STATS_CMD_BG_BM	0
#define M_FW_PORT_STATS_CMD_BG_BM	0x3
#define V_FW_PORT_STATS_CMD_BG_BM(x)	((x) << S_FW_PORT_STATS_CMD_BG_BM)
#define G_FW_PORT_STATS_CMD_BG_BM(x)	\
    (((x) >> S_FW_PORT_STATS_CMD_BG_BM) & M_FW_PORT_STATS_CMD_BG_BM)

#define S_FW_PORT_STATS_CMD_TX		7
#define M_FW_PORT_STATS_CMD_TX		0x1
#define V_FW_PORT_STATS_CMD_TX(x)	((x) << S_FW_PORT_STATS_CMD_TX)
#define G_FW_PORT_STATS_CMD_TX(x)	\
    (((x) >> S_FW_PORT_STATS_CMD_TX) & M_FW_PORT_STATS_CMD_TX)
#define F_FW_PORT_STATS_CMD_TX		V_FW_PORT_STATS_CMD_TX(1U)

#define S_FW_PORT_STATS_CMD_IX		0
#define M_FW_PORT_STATS_CMD_IX		0x3f
#define V_FW_PORT_STATS_CMD_IX(x)	((x) << S_FW_PORT_STATS_CMD_IX)
#define G_FW_PORT_STATS_CMD_IX(x)	\
    (((x) >> S_FW_PORT_STATS_CMD_IX) & M_FW_PORT_STATS_CMD_IX)

/* port loopback stats */
#define FW_NUM_LB_STATS 14
enum fw_port_lb_stats_index {
	FW_STAT_LB_PORT_BYTES_IX,
	FW_STAT_LB_PORT_FRAMES_IX,
	FW_STAT_LB_PORT_BCAST_IX,
	FW_STAT_LB_PORT_MCAST_IX,
	FW_STAT_LB_PORT_UCAST_IX,
	FW_STAT_LB_PORT_ERROR_IX,
	FW_STAT_LB_PORT_64B_IX,
	FW_STAT_LB_PORT_65B_127B_IX,
	FW_STAT_LB_PORT_128B_255B_IX,
	FW_STAT_LB_PORT_256B_511B_IX,
	FW_STAT_LB_PORT_512B_1023B_IX,
	FW_STAT_LB_PORT_1024B_1518B_IX,
	FW_STAT_LB_PORT_1519B_MAX_IX,
	FW_STAT_LB_PORT_DROP_FRAMES_IX
};

struct fw_port_lb_stats_cmd {
	__be32 op_to_lbport;
	__be32 retval_len16;
	union fw_port_lb_stats {
		struct fw_port_lb_stats_ctl {
			__u8   nstats_bg_bm;
			__u8   ix_pkd;
			__be16 r6;
			__be32 r7;
			__be64 stat0;
			__be64 stat1;
			__be64 stat2;
			__be64 stat3;
			__be64 stat4;
			__be64 stat5;
		} ctl;
		struct fw_port_lb_stats_all {
			__be64 tx_bytes;
			__be64 tx_frames;
			__be64 tx_bcast;
			__be64 tx_mcast;
			__be64 tx_ucast;
			__be64 tx_error;
			__be64 tx_64b;
			__be64 tx_65b_127b;
			__be64 tx_128b_255b;
			__be64 tx_256b_511b;
			__be64 tx_512b_1023b;
			__be64 tx_1024b_1518b;
			__be64 tx_1519b_max;
			__be64 rx_lb_drop;
			__be64 rx_lb_trunc;
		} all;
	} u;
};

#define S_FW_PORT_LB_STATS_CMD_LBPORT	0
#define M_FW_PORT_LB_STATS_CMD_LBPORT	0xf
#define V_FW_PORT_LB_STATS_CMD_LBPORT(x) \
    ((x) << S_FW_PORT_LB_STATS_CMD_LBPORT)
#define G_FW_PORT_LB_STATS_CMD_LBPORT(x) \
    (((x) >> S_FW_PORT_LB_STATS_CMD_LBPORT) & M_FW_PORT_LB_STATS_CMD_LBPORT)

#define S_FW_PORT_LB_STATS_CMD_NSTATS	4
#define M_FW_PORT_LB_STATS_CMD_NSTATS	0x7
#define V_FW_PORT_LB_STATS_CMD_NSTATS(x) \
    ((x) << S_FW_PORT_LB_STATS_CMD_NSTATS)
#define G_FW_PORT_LB_STATS_CMD_NSTATS(x) \
    (((x) >> S_FW_PORT_LB_STATS_CMD_NSTATS) & M_FW_PORT_LB_STATS_CMD_NSTATS)

#define S_FW_PORT_LB_STATS_CMD_BG_BM	0
#define M_FW_PORT_LB_STATS_CMD_BG_BM	0x3
#define V_FW_PORT_LB_STATS_CMD_BG_BM(x)	((x) << S_FW_PORT_LB_STATS_CMD_BG_BM)
#define G_FW_PORT_LB_STATS_CMD_BG_BM(x)	\
    (((x) >> S_FW_PORT_LB_STATS_CMD_BG_BM) & M_FW_PORT_LB_STATS_CMD_BG_BM)

#define S_FW_PORT_LB_STATS_CMD_IX	0
#define M_FW_PORT_LB_STATS_CMD_IX	0xf
#define V_FW_PORT_LB_STATS_CMD_IX(x)	((x) << S_FW_PORT_LB_STATS_CMD_IX)
#define G_FW_PORT_LB_STATS_CMD_IX(x)	\
    (((x) >> S_FW_PORT_LB_STATS_CMD_IX) & M_FW_PORT_LB_STATS_CMD_IX)

/* Trace related defines */
#define FW_TRACE_CAPTURE_MAX_SINGLE_FLT_MODE 10240
#define FW_TRACE_CAPTURE_MAX_MULTI_FLT_MODE  2560

struct fw_port_trace_cmd {
	__be32 op_to_portid;
	__be32 retval_len16;
	__be16 traceen_to_pciech;
	__be16 qnum;
	__be32 r5;
};

#define S_FW_PORT_TRACE_CMD_PORTID	0
#define M_FW_PORT_TRACE_CMD_PORTID	0xf
#define V_FW_PORT_TRACE_CMD_PORTID(x)	((x) << S_FW_PORT_TRACE_CMD_PORTID)
#define G_FW_PORT_TRACE_CMD_PORTID(x)	\
    (((x) >> S_FW_PORT_TRACE_CMD_PORTID) & M_FW_PORT_TRACE_CMD_PORTID)

#define S_FW_PORT_TRACE_CMD_TRACEEN	15
#define M_FW_PORT_TRACE_CMD_TRACEEN	0x1
#define V_FW_PORT_TRACE_CMD_TRACEEN(x)	((x) << S_FW_PORT_TRACE_CMD_TRACEEN)
#define G_FW_PORT_TRACE_CMD_TRACEEN(x)	\
    (((x) >> S_FW_PORT_TRACE_CMD_TRACEEN) & M_FW_PORT_TRACE_CMD_TRACEEN)
#define F_FW_PORT_TRACE_CMD_TRACEEN	V_FW_PORT_TRACE_CMD_TRACEEN(1U)

#define S_FW_PORT_TRACE_CMD_FLTMODE	14
#define M_FW_PORT_TRACE_CMD_FLTMODE	0x1
#define V_FW_PORT_TRACE_CMD_FLTMODE(x)	((x) << S_FW_PORT_TRACE_CMD_FLTMODE)
#define G_FW_PORT_TRACE_CMD_FLTMODE(x)	\
    (((x) >> S_FW_PORT_TRACE_CMD_FLTMODE) & M_FW_PORT_TRACE_CMD_FLTMODE)
#define F_FW_PORT_TRACE_CMD_FLTMODE	V_FW_PORT_TRACE_CMD_FLTMODE(1U)

#define S_FW_PORT_TRACE_CMD_DUPLEN	13
#define M_FW_PORT_TRACE_CMD_DUPLEN	0x1
#define V_FW_PORT_TRACE_CMD_DUPLEN(x)	((x) << S_FW_PORT_TRACE_CMD_DUPLEN)
#define G_FW_PORT_TRACE_CMD_DUPLEN(x)	\
    (((x) >> S_FW_PORT_TRACE_CMD_DUPLEN) & M_FW_PORT_TRACE_CMD_DUPLEN)
#define F_FW_PORT_TRACE_CMD_DUPLEN	V_FW_PORT_TRACE_CMD_DUPLEN(1U)

#define S_FW_PORT_TRACE_CMD_RUNTFLTSIZE	8
#define M_FW_PORT_TRACE_CMD_RUNTFLTSIZE	0x1f
#define V_FW_PORT_TRACE_CMD_RUNTFLTSIZE(x) \
    ((x) << S_FW_PORT_TRACE_CMD_RUNTFLTSIZE)
#define G_FW_PORT_TRACE_CMD_RUNTFLTSIZE(x) \
    (((x) >> S_FW_PORT_TRACE_CMD_RUNTFLTSIZE) & \
     M_FW_PORT_TRACE_CMD_RUNTFLTSIZE)

#define S_FW_PORT_TRACE_CMD_PCIECH	6
#define M_FW_PORT_TRACE_CMD_PCIECH	0x3
#define V_FW_PORT_TRACE_CMD_PCIECH(x)	((x) << S_FW_PORT_TRACE_CMD_PCIECH)
#define G_FW_PORT_TRACE_CMD_PCIECH(x)	\
    (((x) >> S_FW_PORT_TRACE_CMD_PCIECH) & M_FW_PORT_TRACE_CMD_PCIECH)

struct fw_port_trace_mmap_cmd {
	__be32 op_to_portid;
	__be32 retval_len16;
	__be32 fid_to_skipoffset;
	__be32 minpktsize_capturemax;
	__u8   map[224];
};

#define S_FW_PORT_TRACE_MMAP_CMD_PORTID	0
#define M_FW_PORT_TRACE_MMAP_CMD_PORTID	0xf
#define V_FW_PORT_TRACE_MMAP_CMD_PORTID(x) \
    ((x) << S_FW_PORT_TRACE_MMAP_CMD_PORTID)
#define G_FW_PORT_TRACE_MMAP_CMD_PORTID(x) \
    (((x) >> S_FW_PORT_TRACE_MMAP_CMD_PORTID) & \
     M_FW_PORT_TRACE_MMAP_CMD_PORTID)

#define S_FW_PORT_TRACE_MMAP_CMD_FID	30
#define M_FW_PORT_TRACE_MMAP_CMD_FID	0x3
#define V_FW_PORT_TRACE_MMAP_CMD_FID(x)	((x) << S_FW_PORT_TRACE_MMAP_CMD_FID)
#define G_FW_PORT_TRACE_MMAP_CMD_FID(x)	\
    (((x) >> S_FW_PORT_TRACE_MMAP_CMD_FID) & M_FW_PORT_TRACE_MMAP_CMD_FID)

#define S_FW_PORT_TRACE_MMAP_CMD_MMAPEN	29
#define M_FW_PORT_TRACE_MMAP_CMD_MMAPEN	0x1
#define V_FW_PORT_TRACE_MMAP_CMD_MMAPEN(x) \
    ((x) << S_FW_PORT_TRACE_MMAP_CMD_MMAPEN)
#define G_FW_PORT_TRACE_MMAP_CMD_MMAPEN(x) \
    (((x) >> S_FW_PORT_TRACE_MMAP_CMD_MMAPEN) & \
     M_FW_PORT_TRACE_MMAP_CMD_MMAPEN)
#define F_FW_PORT_TRACE_MMAP_CMD_MMAPEN	V_FW_PORT_TRACE_MMAP_CMD_MMAPEN(1U)

#define S_FW_PORT_TRACE_MMAP_CMD_DCMAPEN 28
#define M_FW_PORT_TRACE_MMAP_CMD_DCMAPEN 0x1
#define V_FW_PORT_TRACE_MMAP_CMD_DCMAPEN(x) \
    ((x) << S_FW_PORT_TRACE_MMAP_CMD_DCMAPEN)
#define G_FW_PORT_TRACE_MMAP_CMD_DCMAPEN(x) \
    (((x) >> S_FW_PORT_TRACE_MMAP_CMD_DCMAPEN) & \
     M_FW_PORT_TRACE_MMAP_CMD_DCMAPEN)
#define F_FW_PORT_TRACE_MMAP_CMD_DCMAPEN V_FW_PORT_TRACE_MMAP_CMD_DCMAPEN(1U)

#define S_FW_PORT_TRACE_MMAP_CMD_SKIPLENGTH 8
#define M_FW_PORT_TRACE_MMAP_CMD_SKIPLENGTH 0x1f
#define V_FW_PORT_TRACE_MMAP_CMD_SKIPLENGTH(x) \
    ((x) << S_FW_PORT_TRACE_MMAP_CMD_SKIPLENGTH)
#define G_FW_PORT_TRACE_MMAP_CMD_SKIPLENGTH(x) \
    (((x) >> S_FW_PORT_TRACE_MMAP_CMD_SKIPLENGTH) & \
     M_FW_PORT_TRACE_MMAP_CMD_SKIPLENGTH)

#define S_FW_PORT_TRACE_MMAP_CMD_SKIPOFFSET 0
#define M_FW_PORT_TRACE_MMAP_CMD_SKIPOFFSET 0x1f
#define V_FW_PORT_TRACE_MMAP_CMD_SKIPOFFSET(x) \
    ((x) << S_FW_PORT_TRACE_MMAP_CMD_SKIPOFFSET)
#define G_FW_PORT_TRACE_MMAP_CMD_SKIPOFFSET(x) \
    (((x) >> S_FW_PORT_TRACE_MMAP_CMD_SKIPOFFSET) & \
     M_FW_PORT_TRACE_MMAP_CMD_SKIPOFFSET)

#define S_FW_PORT_TRACE_MMAP_CMD_MINPKTSIZE 18
#define M_FW_PORT_TRACE_MMAP_CMD_MINPKTSIZE 0x3fff
#define V_FW_PORT_TRACE_MMAP_CMD_MINPKTSIZE(x) \
    ((x) << S_FW_PORT_TRACE_MMAP_CMD_MINPKTSIZE)
#define G_FW_PORT_TRACE_MMAP_CMD_MINPKTSIZE(x) \
    (((x) >> S_FW_PORT_TRACE_MMAP_CMD_MINPKTSIZE) & \
     M_FW_PORT_TRACE_MMAP_CMD_MINPKTSIZE)

#define S_FW_PORT_TRACE_MMAP_CMD_CAPTUREMAX 0
#define M_FW_PORT_TRACE_MMAP_CMD_CAPTUREMAX 0x3fff
#define V_FW_PORT_TRACE_MMAP_CMD_CAPTUREMAX(x) \
    ((x) << S_FW_PORT_TRACE_MMAP_CMD_CAPTUREMAX)
#define G_FW_PORT_TRACE_MMAP_CMD_CAPTUREMAX(x) \
    (((x) >> S_FW_PORT_TRACE_MMAP_CMD_CAPTUREMAX) & \
     M_FW_PORT_TRACE_MMAP_CMD_CAPTUREMAX)

enum fw_ptp_subop {

	/* none */
	FW_PTP_SC_INIT_TIMER		= 0x00,
	FW_PTP_SC_TX_TYPE		= 0x01,

	/* init */
	FW_PTP_SC_RXTIME_STAMP		= 0x08,
	FW_PTP_SC_RDRX_TYPE		= 0x09,

	/* ts */
	FW_PTP_SC_ADJ_FREQ		= 0x10,
	FW_PTP_SC_ADJ_TIME		= 0x11,
	FW_PTP_SC_ADJ_FTIME		= 0x12,
	FW_PTP_SC_WALL_CLOCK		= 0x13,
	FW_PTP_SC_GET_TIME		= 0x14,
	FW_PTP_SC_SET_TIME		= 0x15,
};

struct fw_ptp_cmd {
	__be32 op_to_portid;
	__be32 retval_len16;
	union fw_ptp {
		struct fw_ptp_sc {
			__u8   sc;
			__u8   r3[7];
		} scmd;
		struct fw_ptp_init {
			__u8   sc;
			__u8   txchan;
			__be16 absid;
			__be16 mode;
			__be16 r3;
		} init;
		struct fw_ptp_ts {
			__u8   sc;
			__u8   sign;
			__be16 r3;
			__be32 ppb;
			__be64 tm;
		} ts;
	} u;
	__be64 r3;
};

#define S_FW_PTP_CMD_PORTID		0
#define M_FW_PTP_CMD_PORTID		0xf
#define V_FW_PTP_CMD_PORTID(x)		((x) << S_FW_PTP_CMD_PORTID)
#define G_FW_PTP_CMD_PORTID(x)		\
    (((x) >> S_FW_PTP_CMD_PORTID) & M_FW_PTP_CMD_PORTID)

struct fw_rss_ind_tbl_cmd {
	__be32 op_to_viid;
	__be32 retval_len16;
	__be16 niqid;
	__be16 startidx;
	__be32 r3;
	__be32 iq0_to_iq2;
	__be32 iq3_to_iq5;
	__be32 iq6_to_iq8;
	__be32 iq9_to_iq11;
	__be32 iq12_to_iq14;
	__be32 iq15_to_iq17;
	__be32 iq18_to_iq20;
	__be32 iq21_to_iq23;
	__be32 iq24_to_iq26;
	__be32 iq27_to_iq29;
	__be32 iq30_iq31;
	__be32 r15_lo;
};

#define S_FW_RSS_IND_TBL_CMD_VIID	0
#define M_FW_RSS_IND_TBL_CMD_VIID	0xfff
#define V_FW_RSS_IND_TBL_CMD_VIID(x)	((x) << S_FW_RSS_IND_TBL_CMD_VIID)
#define G_FW_RSS_IND_TBL_CMD_VIID(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_VIID) & M_FW_RSS_IND_TBL_CMD_VIID)

#define S_FW_RSS_IND_TBL_CMD_IQ0	20
#define M_FW_RSS_IND_TBL_CMD_IQ0	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ0(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ0)
#define G_FW_RSS_IND_TBL_CMD_IQ0(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ0) & M_FW_RSS_IND_TBL_CMD_IQ0)

#define S_FW_RSS_IND_TBL_CMD_IQ1	10
#define M_FW_RSS_IND_TBL_CMD_IQ1	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ1(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ1)
#define G_FW_RSS_IND_TBL_CMD_IQ1(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ1) & M_FW_RSS_IND_TBL_CMD_IQ1)

#define S_FW_RSS_IND_TBL_CMD_IQ2	0
#define M_FW_RSS_IND_TBL_CMD_IQ2	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ2(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ2)
#define G_FW_RSS_IND_TBL_CMD_IQ2(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ2) & M_FW_RSS_IND_TBL_CMD_IQ2)

#define S_FW_RSS_IND_TBL_CMD_IQ3	20
#define M_FW_RSS_IND_TBL_CMD_IQ3	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ3(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ3)
#define G_FW_RSS_IND_TBL_CMD_IQ3(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ3) & M_FW_RSS_IND_TBL_CMD_IQ3)

#define S_FW_RSS_IND_TBL_CMD_IQ4	10
#define M_FW_RSS_IND_TBL_CMD_IQ4	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ4(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ4)
#define G_FW_RSS_IND_TBL_CMD_IQ4(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ4) & M_FW_RSS_IND_TBL_CMD_IQ4)

#define S_FW_RSS_IND_TBL_CMD_IQ5	0
#define M_FW_RSS_IND_TBL_CMD_IQ5	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ5(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ5)
#define G_FW_RSS_IND_TBL_CMD_IQ5(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ5) & M_FW_RSS_IND_TBL_CMD_IQ5)

#define S_FW_RSS_IND_TBL_CMD_IQ6	20
#define M_FW_RSS_IND_TBL_CMD_IQ6	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ6(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ6)
#define G_FW_RSS_IND_TBL_CMD_IQ6(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ6) & M_FW_RSS_IND_TBL_CMD_IQ6)

#define S_FW_RSS_IND_TBL_CMD_IQ7	10
#define M_FW_RSS_IND_TBL_CMD_IQ7	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ7(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ7)
#define G_FW_RSS_IND_TBL_CMD_IQ7(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ7) & M_FW_RSS_IND_TBL_CMD_IQ7)

#define S_FW_RSS_IND_TBL_CMD_IQ8	0
#define M_FW_RSS_IND_TBL_CMD_IQ8	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ8(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ8)
#define G_FW_RSS_IND_TBL_CMD_IQ8(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ8) & M_FW_RSS_IND_TBL_CMD_IQ8)

#define S_FW_RSS_IND_TBL_CMD_IQ9	20
#define M_FW_RSS_IND_TBL_CMD_IQ9	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ9(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ9)
#define G_FW_RSS_IND_TBL_CMD_IQ9(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ9) & M_FW_RSS_IND_TBL_CMD_IQ9)

#define S_FW_RSS_IND_TBL_CMD_IQ10	10
#define M_FW_RSS_IND_TBL_CMD_IQ10	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ10(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ10)
#define G_FW_RSS_IND_TBL_CMD_IQ10(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ10) & M_FW_RSS_IND_TBL_CMD_IQ10)

#define S_FW_RSS_IND_TBL_CMD_IQ11	0
#define M_FW_RSS_IND_TBL_CMD_IQ11	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ11(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ11)
#define G_FW_RSS_IND_TBL_CMD_IQ11(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ11) & M_FW_RSS_IND_TBL_CMD_IQ11)

#define S_FW_RSS_IND_TBL_CMD_IQ12	20
#define M_FW_RSS_IND_TBL_CMD_IQ12	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ12(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ12)
#define G_FW_RSS_IND_TBL_CMD_IQ12(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ12) & M_FW_RSS_IND_TBL_CMD_IQ12)

#define S_FW_RSS_IND_TBL_CMD_IQ13	10
#define M_FW_RSS_IND_TBL_CMD_IQ13	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ13(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ13)
#define G_FW_RSS_IND_TBL_CMD_IQ13(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ13) & M_FW_RSS_IND_TBL_CMD_IQ13)

#define S_FW_RSS_IND_TBL_CMD_IQ14	0
#define M_FW_RSS_IND_TBL_CMD_IQ14	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ14(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ14)
#define G_FW_RSS_IND_TBL_CMD_IQ14(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ14) & M_FW_RSS_IND_TBL_CMD_IQ14)

#define S_FW_RSS_IND_TBL_CMD_IQ15	20
#define M_FW_RSS_IND_TBL_CMD_IQ15	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ15(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ15)
#define G_FW_RSS_IND_TBL_CMD_IQ15(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ15) & M_FW_RSS_IND_TBL_CMD_IQ15)

#define S_FW_RSS_IND_TBL_CMD_IQ16	10
#define M_FW_RSS_IND_TBL_CMD_IQ16	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ16(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ16)
#define G_FW_RSS_IND_TBL_CMD_IQ16(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ16) & M_FW_RSS_IND_TBL_CMD_IQ16)

#define S_FW_RSS_IND_TBL_CMD_IQ17	0
#define M_FW_RSS_IND_TBL_CMD_IQ17	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ17(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ17)
#define G_FW_RSS_IND_TBL_CMD_IQ17(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ17) & M_FW_RSS_IND_TBL_CMD_IQ17)

#define S_FW_RSS_IND_TBL_CMD_IQ18	20
#define M_FW_RSS_IND_TBL_CMD_IQ18	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ18(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ18)
#define G_FW_RSS_IND_TBL_CMD_IQ18(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ18) & M_FW_RSS_IND_TBL_CMD_IQ18)

#define S_FW_RSS_IND_TBL_CMD_IQ19	10
#define M_FW_RSS_IND_TBL_CMD_IQ19	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ19(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ19)
#define G_FW_RSS_IND_TBL_CMD_IQ19(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ19) & M_FW_RSS_IND_TBL_CMD_IQ19)

#define S_FW_RSS_IND_TBL_CMD_IQ20	0
#define M_FW_RSS_IND_TBL_CMD_IQ20	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ20(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ20)
#define G_FW_RSS_IND_TBL_CMD_IQ20(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ20) & M_FW_RSS_IND_TBL_CMD_IQ20)

#define S_FW_RSS_IND_TBL_CMD_IQ21	20
#define M_FW_RSS_IND_TBL_CMD_IQ21	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ21(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ21)
#define G_FW_RSS_IND_TBL_CMD_IQ21(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ21) & M_FW_RSS_IND_TBL_CMD_IQ21)

#define S_FW_RSS_IND_TBL_CMD_IQ22	10
#define M_FW_RSS_IND_TBL_CMD_IQ22	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ22(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ22)
#define G_FW_RSS_IND_TBL_CMD_IQ22(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ22) & M_FW_RSS_IND_TBL_CMD_IQ22)

#define S_FW_RSS_IND_TBL_CMD_IQ23	0
#define M_FW_RSS_IND_TBL_CMD_IQ23	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ23(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ23)
#define G_FW_RSS_IND_TBL_CMD_IQ23(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ23) & M_FW_RSS_IND_TBL_CMD_IQ23)

#define S_FW_RSS_IND_TBL_CMD_IQ24	20
#define M_FW_RSS_IND_TBL_CMD_IQ24	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ24(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ24)
#define G_FW_RSS_IND_TBL_CMD_IQ24(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ24) & M_FW_RSS_IND_TBL_CMD_IQ24)

#define S_FW_RSS_IND_TBL_CMD_IQ25	10
#define M_FW_RSS_IND_TBL_CMD_IQ25	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ25(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ25)
#define G_FW_RSS_IND_TBL_CMD_IQ25(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ25) & M_FW_RSS_IND_TBL_CMD_IQ25)

#define S_FW_RSS_IND_TBL_CMD_IQ26	0
#define M_FW_RSS_IND_TBL_CMD_IQ26	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ26(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ26)
#define G_FW_RSS_IND_TBL_CMD_IQ26(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ26) & M_FW_RSS_IND_TBL_CMD_IQ26)

#define S_FW_RSS_IND_TBL_CMD_IQ27	20
#define M_FW_RSS_IND_TBL_CMD_IQ27	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ27(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ27)
#define G_FW_RSS_IND_TBL_CMD_IQ27(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ27) & M_FW_RSS_IND_TBL_CMD_IQ27)

#define S_FW_RSS_IND_TBL_CMD_IQ28	10
#define M_FW_RSS_IND_TBL_CMD_IQ28	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ28(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ28)
#define G_FW_RSS_IND_TBL_CMD_IQ28(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ28) & M_FW_RSS_IND_TBL_CMD_IQ28)

#define S_FW_RSS_IND_TBL_CMD_IQ29	0
#define M_FW_RSS_IND_TBL_CMD_IQ29	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ29(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ29)
#define G_FW_RSS_IND_TBL_CMD_IQ29(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ29) & M_FW_RSS_IND_TBL_CMD_IQ29)

#define S_FW_RSS_IND_TBL_CMD_IQ30	20
#define M_FW_RSS_IND_TBL_CMD_IQ30	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ30(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ30)
#define G_FW_RSS_IND_TBL_CMD_IQ30(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ30) & M_FW_RSS_IND_TBL_CMD_IQ30)

#define S_FW_RSS_IND_TBL_CMD_IQ31	10
#define M_FW_RSS_IND_TBL_CMD_IQ31	0x3ff
#define V_FW_RSS_IND_TBL_CMD_IQ31(x)	((x) << S_FW_RSS_IND_TBL_CMD_IQ31)
#define G_FW_RSS_IND_TBL_CMD_IQ31(x)	\
    (((x) >> S_FW_RSS_IND_TBL_CMD_IQ31) & M_FW_RSS_IND_TBL_CMD_IQ31)

struct fw_rss_glb_config_cmd {
	__be32 op_to_write;
	__be32 retval_len16;
	union fw_rss_glb_config {
		struct fw_rss_glb_config_manual {
			__be32 mode_pkd;
			__be32 r3;
			__be64 r4;
			__be64 r5;
		} manual;
		struct fw_rss_glb_config_basicvirtual {
			__be32 mode_keymode;
			__be32 synmapen_to_hashtoeplitz;
			__be64 r8;
			__be64 r9;
		} basicvirtual;
	} u;
};

#define S_FW_RSS_GLB_CONFIG_CMD_MODE	28
#define M_FW_RSS_GLB_CONFIG_CMD_MODE	0xf
#define V_FW_RSS_GLB_CONFIG_CMD_MODE(x)	((x) << S_FW_RSS_GLB_CONFIG_CMD_MODE)
#define G_FW_RSS_GLB_CONFIG_CMD_MODE(x)	\
    (((x) >> S_FW_RSS_GLB_CONFIG_CMD_MODE) & M_FW_RSS_GLB_CONFIG_CMD_MODE)

#define FW_RSS_GLB_CONFIG_CMD_MODE_MANUAL	0
#define FW_RSS_GLB_CONFIG_CMD_MODE_BASICVIRTUAL	1
#define FW_RSS_GLB_CONFIG_CMD_MODE_MAX		1

#define S_FW_RSS_GLB_CONFIG_CMD_KEYMODE	26
#define M_FW_RSS_GLB_CONFIG_CMD_KEYMODE	0x3
#define V_FW_RSS_GLB_CONFIG_CMD_KEYMODE(x) \
    ((x) << S_FW_RSS_GLB_CONFIG_CMD_KEYMODE)
#define G_FW_RSS_GLB_CONFIG_CMD_KEYMODE(x) \
    (((x) >> S_FW_RSS_GLB_CONFIG_CMD_KEYMODE) & \
     M_FW_RSS_GLB_CONFIG_CMD_KEYMODE)

#define FW_RSS_GLB_CONFIG_CMD_KEYMODE_GLBKEY	0
#define FW_RSS_GLB_CONFIG_CMD_KEYMODE_GLBVF_KEY	1
#define FW_RSS_GLB_CONFIG_CMD_KEYMODE_PFVF_KEY	2
#define FW_RSS_GLB_CONFIG_CMD_KEYMODE_IDXVF_KEY	3

#define S_FW_RSS_GLB_CONFIG_CMD_SYNMAPEN 8
#define M_FW_RSS_GLB_CONFIG_CMD_SYNMAPEN 0x1
#define V_FW_RSS_GLB_CONFIG_CMD_SYNMAPEN(x) \
    ((x) << S_FW_RSS_GLB_CONFIG_CMD_SYNMAPEN)
#define G_FW_RSS_GLB_CONFIG_CMD_SYNMAPEN(x) \
    (((x) >> S_FW_RSS_GLB_CONFIG_CMD_SYNMAPEN) & \
     M_FW_RSS_GLB_CONFIG_CMD_SYNMAPEN)
#define F_FW_RSS_GLB_CONFIG_CMD_SYNMAPEN V_FW_RSS_GLB_CONFIG_CMD_SYNMAPEN(1U)

#define S_FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV6 7
#define M_FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV6 0x1
#define V_FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV6(x) \
    ((x) << S_FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV6)
#define G_FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV6(x) \
    (((x) >> S_FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV6) & \
     M_FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV6)
#define F_FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV6 \
    V_FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV6(1U)

#define S_FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV6 6
#define M_FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV6 0x1
#define V_FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV6(x) \
    ((x) << S_FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV6)
#define G_FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV6(x) \
    (((x) >> S_FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV6) & \
     M_FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV6)
#define F_FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV6 \
    V_FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV6(1U)

#define S_FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV4 5
#define M_FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV4 0x1
#define V_FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV4(x) \
    ((x) << S_FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV4)
#define G_FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV4(x) \
    (((x) >> S_FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV4) & \
     M_FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV4)
#define F_FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV4 \
    V_FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV4(1U)

#define S_FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV4 4
#define M_FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV4 0x1
#define V_FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV4(x) \
    ((x) << S_FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV4)
#define G_FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV4(x) \
    (((x) >> S_FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV4) & \
     M_FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV4)
#define F_FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV4 \
    V_FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV4(1U)

#define S_FW_RSS_GLB_CONFIG_CMD_OFDMAPEN 3
#define M_FW_RSS_GLB_CONFIG_CMD_OFDMAPEN 0x1
#define V_FW_RSS_GLB_CONFIG_CMD_OFDMAPEN(x) \
    ((x) << S_FW_RSS_GLB_CONFIG_CMD_OFDMAPEN)
#define G_FW_RSS_GLB_CONFIG_CMD_OFDMAPEN(x) \
    (((x) >> S_FW_RSS_GLB_CONFIG_CMD_OFDMAPEN) & \
     M_FW_RSS_GLB_CONFIG_CMD_OFDMAPEN)
#define F_FW_RSS_GLB_CONFIG_CMD_OFDMAPEN V_FW_RSS_GLB_CONFIG_CMD_OFDMAPEN(1U)

#define S_FW_RSS_GLB_CONFIG_CMD_TNLMAPEN 2
#define M_FW_RSS_GLB_CONFIG_CMD_TNLMAPEN 0x1
#define V_FW_RSS_GLB_CONFIG_CMD_TNLMAPEN(x) \
    ((x) << S_FW_RSS_GLB_CONFIG_CMD_TNLMAPEN)
#define G_FW_RSS_GLB_CONFIG_CMD_TNLMAPEN(x) \
    (((x) >> S_FW_RSS_GLB_CONFIG_CMD_TNLMAPEN) & \
     M_FW_RSS_GLB_CONFIG_CMD_TNLMAPEN)
#define F_FW_RSS_GLB_CONFIG_CMD_TNLMAPEN V_FW_RSS_GLB_CONFIG_CMD_TNLMAPEN(1U)

#define S_FW_RSS_GLB_CONFIG_CMD_TNLALLLKP 1
#define M_FW_RSS_GLB_CONFIG_CMD_TNLALLLKP 0x1
#define V_FW_RSS_GLB_CONFIG_CMD_TNLALLLKP(x) \
    ((x) << S_FW_RSS_GLB_CONFIG_CMD_TNLALLLKP)
#define G_FW_RSS_GLB_CONFIG_CMD_TNLALLLKP(x) \
    (((x) >> S_FW_RSS_GLB_CONFIG_CMD_TNLALLLKP) & \
     M_FW_RSS_GLB_CONFIG_CMD_TNLALLLKP)
#define F_FW_RSS_GLB_CONFIG_CMD_TNLALLLKP \
    V_FW_RSS_GLB_CONFIG_CMD_TNLALLLKP(1U)

#define S_FW_RSS_GLB_CONFIG_CMD_HASHTOEPLITZ 0
#define M_FW_RSS_GLB_CONFIG_CMD_HASHTOEPLITZ 0x1
#define V_FW_RSS_GLB_CONFIG_CMD_HASHTOEPLITZ(x) \
    ((x) << S_FW_RSS_GLB_CONFIG_CMD_HASHTOEPLITZ)
#define G_FW_RSS_GLB_CONFIG_CMD_HASHTOEPLITZ(x) \
    (((x) >> S_FW_RSS_GLB_CONFIG_CMD_HASHTOEPLITZ) & \
     M_FW_RSS_GLB_CONFIG_CMD_HASHTOEPLITZ)
#define F_FW_RSS_GLB_CONFIG_CMD_HASHTOEPLITZ \
    V_FW_RSS_GLB_CONFIG_CMD_HASHTOEPLITZ(1U)

struct fw_rss_vi_config_cmd {
	__be32 op_to_viid;
	__be32 retval_len16;
	union fw_rss_vi_config {
		struct fw_rss_vi_config_manual {
			__be64 r3;
			__be64 r4;
			__be64 r5;
		} manual;
		struct fw_rss_vi_config_basicvirtual {
			__be32 r6;
			__be32 defaultq_to_udpen;
			__be32 secretkeyidx_pkd;
			__be32 secretkeyxor;
			__be64 r10;
		} basicvirtual;
	} u;
};

#define S_FW_RSS_VI_CONFIG_CMD_VIID	0
#define M_FW_RSS_VI_CONFIG_CMD_VIID	0xfff
#define V_FW_RSS_VI_CONFIG_CMD_VIID(x)	((x) << S_FW_RSS_VI_CONFIG_CMD_VIID)
#define G_FW_RSS_VI_CONFIG_CMD_VIID(x)	\
    (((x) >> S_FW_RSS_VI_CONFIG_CMD_VIID) & M_FW_RSS_VI_CONFIG_CMD_VIID)

#define S_FW_RSS_VI_CONFIG_CMD_DEFAULTQ	16
#define M_FW_RSS_VI_CONFIG_CMD_DEFAULTQ	0x3ff
#define V_FW_RSS_VI_CONFIG_CMD_DEFAULTQ(x) \
    ((x) << S_FW_RSS_VI_CONFIG_CMD_DEFAULTQ)
#define G_FW_RSS_VI_CONFIG_CMD_DEFAULTQ(x) \
    (((x) >> S_FW_RSS_VI_CONFIG_CMD_DEFAULTQ) & \
     M_FW_RSS_VI_CONFIG_CMD_DEFAULTQ)

#define S_FW_RSS_VI_CONFIG_CMD_IP6FOURTUPEN 4
#define M_FW_RSS_VI_CONFIG_CMD_IP6FOURTUPEN 0x1
#define V_FW_RSS_VI_CONFIG_CMD_IP6FOURTUPEN(x) \
    ((x) << S_FW_RSS_VI_CONFIG_CMD_IP6FOURTUPEN)
#define G_FW_RSS_VI_CONFIG_CMD_IP6FOURTUPEN(x) \
    (((x) >> S_FW_RSS_VI_CONFIG_CMD_IP6FOURTUPEN) & \
     M_FW_RSS_VI_CONFIG_CMD_IP6FOURTUPEN)
#define F_FW_RSS_VI_CONFIG_CMD_IP6FOURTUPEN \
    V_FW_RSS_VI_CONFIG_CMD_IP6FOURTUPEN(1U)

#define S_FW_RSS_VI_CONFIG_CMD_IP6TWOTUPEN 3
#define M_FW_RSS_VI_CONFIG_CMD_IP6TWOTUPEN 0x1
#define V_FW_RSS_VI_CONFIG_CMD_IP6TWOTUPEN(x) \
    ((x) << S_FW_RSS_VI_CONFIG_CMD_IP6TWOTUPEN)
#define G_FW_RSS_VI_CONFIG_CMD_IP6TWOTUPEN(x) \
    (((x) >> S_FW_RSS_VI_CONFIG_CMD_IP6TWOTUPEN) & \
     M_FW_RSS_VI_CONFIG_CMD_IP6TWOTUPEN)
#define F_FW_RSS_VI_CONFIG_CMD_IP6TWOTUPEN \
    V_FW_RSS_VI_CONFIG_CMD_IP6TWOTUPEN(1U)

#define S_FW_RSS_VI_CONFIG_CMD_IP4FOURTUPEN 2
#define M_FW_RSS_VI_CONFIG_CMD_IP4FOURTUPEN 0x1
#define V_FW_RSS_VI_CONFIG_CMD_IP4FOURTUPEN(x) \
    ((x) << S_FW_RSS_VI_CONFIG_CMD_IP4FOURTUPEN)
#define G_FW_RSS_VI_CONFIG_CMD_IP4FOURTUPEN(x) \
    (((x) >> S_FW_RSS_VI_CONFIG_CMD_IP4FOURTUPEN) & \
     M_FW_RSS_VI_CONFIG_CMD_IP4FOURTUPEN)
#define F_FW_RSS_VI_CONFIG_CMD_IP4FOURTUPEN \
    V_FW_RSS_VI_CONFIG_CMD_IP4FOURTUPEN(1U)

#define S_FW_RSS_VI_CONFIG_CMD_IP4TWOTUPEN 1
#define M_FW_RSS_VI_CONFIG_CMD_IP4TWOTUPEN 0x1
#define V_FW_RSS_VI_CONFIG_CMD_IP4TWOTUPEN(x) \
    ((x) << S_FW_RSS_VI_CONFIG_CMD_IP4TWOTUPEN)
#define G_FW_RSS_VI_CONFIG_CMD_IP4TWOTUPEN(x) \
    (((x) >> S_FW_RSS_VI_CONFIG_CMD_IP4TWOTUPEN) & \
     M_FW_RSS_VI_CONFIG_CMD_IP4TWOTUPEN)
#define F_FW_RSS_VI_CONFIG_CMD_IP4TWOTUPEN \
    V_FW_RSS_VI_CONFIG_CMD_IP4TWOTUPEN(1U)

#define S_FW_RSS_VI_CONFIG_CMD_UDPEN	0
#define M_FW_RSS_VI_CONFIG_CMD_UDPEN	0x1
#define V_FW_RSS_VI_CONFIG_CMD_UDPEN(x)	((x) << S_FW_RSS_VI_CONFIG_CMD_UDPEN)
#define G_FW_RSS_VI_CONFIG_CMD_UDPEN(x)	\
    (((x) >> S_FW_RSS_VI_CONFIG_CMD_UDPEN) & M_FW_RSS_VI_CONFIG_CMD_UDPEN)
#define F_FW_RSS_VI_CONFIG_CMD_UDPEN	V_FW_RSS_VI_CONFIG_CMD_UDPEN(1U)

#define S_FW_RSS_VI_CONFIG_CMD_SECRETKEYIDX 0
#define M_FW_RSS_VI_CONFIG_CMD_SECRETKEYIDX 0xf
#define V_FW_RSS_VI_CONFIG_CMD_SECRETKEYIDX(x) \
    ((x) << S_FW_RSS_VI_CONFIG_CMD_SECRETKEYIDX)
#define G_FW_RSS_VI_CONFIG_CMD_SECRETKEYIDX(x) \
    (((x) >> S_FW_RSS_VI_CONFIG_CMD_SECRETKEYIDX) & \
     M_FW_RSS_VI_CONFIG_CMD_SECRETKEYIDX)

enum fw_sched_sc {
	FW_SCHED_SC_CONFIG		= 0,
	FW_SCHED_SC_PARAMS		= 1,
};

enum fw_sched_type {
	FW_SCHED_TYPE_PKTSCHED	        = 0,
	FW_SCHED_TYPE_STREAMSCHED       = 1,
};

enum fw_sched_params_level {
	FW_SCHED_PARAMS_LEVEL_CL_RL	= 0,
	FW_SCHED_PARAMS_LEVEL_CL_WRR	= 1,
	FW_SCHED_PARAMS_LEVEL_CH_RL	= 2,
};

enum fw_sched_params_mode {
	FW_SCHED_PARAMS_MODE_CLASS	= 0,
	FW_SCHED_PARAMS_MODE_FLOW	= 1,
};

enum fw_sched_params_unit {
	FW_SCHED_PARAMS_UNIT_BITRATE	= 0,
	FW_SCHED_PARAMS_UNIT_PKTRATE	= 1,
};

enum fw_sched_params_rate {
	FW_SCHED_PARAMS_RATE_REL	= 0,
	FW_SCHED_PARAMS_RATE_ABS	= 1,
};

struct fw_sched_cmd {
	__be32 op_to_write;
	__be32 retval_len16;
	union fw_sched {
		struct fw_sched_config {
			__u8   sc;
			__u8   type;
			__u8   minmaxen;
			__u8   r3[5];
			__u8   nclasses[4];
			__be32 r4;
		} config;
		struct fw_sched_params {
			__u8   sc;
			__u8   type;
			__u8   level;
			__u8   mode;
			__u8   unit;
			__u8   rate;
			__u8   ch;
			__u8   cl;
			__be32 min;
			__be32 max;
			__be16 weight;
			__be16 pktsize;
			__be16 burstsize;
			__be16 r4;
		} params;
	} u;
};

/*
 *	length of the formatting string
 */
#define FW_DEVLOG_FMT_LEN	192

/*
 *	maximum number of the formatting string parameters
 */
#define FW_DEVLOG_FMT_PARAMS_NUM 8

/*
 *	priority levels
 */
enum fw_devlog_level {
	FW_DEVLOG_LEVEL_EMERG	= 0x0,
	FW_DEVLOG_LEVEL_CRIT	= 0x1,
	FW_DEVLOG_LEVEL_ERR	= 0x2,
	FW_DEVLOG_LEVEL_NOTICE	= 0x3,
	FW_DEVLOG_LEVEL_INFO	= 0x4,
	FW_DEVLOG_LEVEL_DEBUG	= 0x5,
	FW_DEVLOG_LEVEL_MAX	= 0x5,
};

/*
 *	facilities that may send a log message
 */
enum fw_devlog_facility {
	FW_DEVLOG_FACILITY_CORE		= 0x00,
	FW_DEVLOG_FACILITY_CF		= 0x01,
	FW_DEVLOG_FACILITY_SCHED	= 0x02,
	FW_DEVLOG_FACILITY_TIMER	= 0x04,
	FW_DEVLOG_FACILITY_RES		= 0x06,
	FW_DEVLOG_FACILITY_HW		= 0x08,
	FW_DEVLOG_FACILITY_FLR		= 0x10,
	FW_DEVLOG_FACILITY_DMAQ		= 0x12,
	FW_DEVLOG_FACILITY_PHY		= 0x14,
	FW_DEVLOG_FACILITY_MAC		= 0x16,
	FW_DEVLOG_FACILITY_PORT		= 0x18,
	FW_DEVLOG_FACILITY_VI		= 0x1A,
	FW_DEVLOG_FACILITY_FILTER	= 0x1C,
	FW_DEVLOG_FACILITY_ACL		= 0x1E,
	FW_DEVLOG_FACILITY_TM		= 0x20,
	FW_DEVLOG_FACILITY_QFC		= 0x22,
	FW_DEVLOG_FACILITY_DCB		= 0x24,
	FW_DEVLOG_FACILITY_ETH		= 0x26,
	FW_DEVLOG_FACILITY_OFLD		= 0x28,
	FW_DEVLOG_FACILITY_RI		= 0x2A,
	FW_DEVLOG_FACILITY_ISCSI	= 0x2C,
	FW_DEVLOG_FACILITY_FCOE		= 0x2E,
	FW_DEVLOG_FACILITY_FOISCSI	= 0x30,
	FW_DEVLOG_FACILITY_FOFCOE	= 0x32,
	FW_DEVLOG_FACILITY_CHNET	= 0x34,
	FW_DEVLOG_FACILITY_COISCSI	= 0x36,
	FW_DEVLOG_FACILITY_MAX		= 0x38,
};

/*
 *	log message format
 */
struct fw_devlog_e {
	__be64	timestamp;
	__be32	seqno;
	__be16	reserved1;
	__u8	level;
	__u8	facility;
	__u8	fmt[FW_DEVLOG_FMT_LEN];
	__be32	params[FW_DEVLOG_FMT_PARAMS_NUM];
	__be32	reserved3[4];
};

struct fw_devlog_cmd {
	__be32 op_to_write;
	__be32 retval_len16;
	__u8   level;
	__u8   r2[7];
	__be32 memtype_devlog_memaddr16_devlog;
	__be32 memsize_devlog;
	__be32 r3[2];
};

#define S_FW_DEVLOG_CMD_MEMTYPE_DEVLOG	28
#define M_FW_DEVLOG_CMD_MEMTYPE_DEVLOG	0xf
#define V_FW_DEVLOG_CMD_MEMTYPE_DEVLOG(x) \
    ((x) << S_FW_DEVLOG_CMD_MEMTYPE_DEVLOG)
#define G_FW_DEVLOG_CMD_MEMTYPE_DEVLOG(x) \
    (((x) >> S_FW_DEVLOG_CMD_MEMTYPE_DEVLOG) & M_FW_DEVLOG_CMD_MEMTYPE_DEVLOG)

#define S_FW_DEVLOG_CMD_MEMADDR16_DEVLOG 0
#define M_FW_DEVLOG_CMD_MEMADDR16_DEVLOG 0xfffffff
#define V_FW_DEVLOG_CMD_MEMADDR16_DEVLOG(x) \
    ((x) << S_FW_DEVLOG_CMD_MEMADDR16_DEVLOG)
#define G_FW_DEVLOG_CMD_MEMADDR16_DEVLOG(x) \
    (((x) >> S_FW_DEVLOG_CMD_MEMADDR16_DEVLOG) & \
     M_FW_DEVLOG_CMD_MEMADDR16_DEVLOG)

enum fw_watchdog_actions {
	FW_WATCHDOG_ACTION_SHUTDOWN = 0,
	FW_WATCHDOG_ACTION_FLR = 1,
	FW_WATCHDOG_ACTION_BYPASS = 2,
	FW_WATCHDOG_ACTION_TMPCHK = 3,
	FW_WATCHDOG_ACTION_PAUSEOFF = 4,

	FW_WATCHDOG_ACTION_MAX = 5,
};

#define FW_WATCHDOG_MAX_TIMEOUT_SECS	60

struct fw_watchdog_cmd {
	__be32 op_to_vfn;
	__be32 retval_len16;
	__be32 timeout;
	__be32 action;
};

#define S_FW_WATCHDOG_CMD_PFN		8
#define M_FW_WATCHDOG_CMD_PFN		0x7
#define V_FW_WATCHDOG_CMD_PFN(x)	((x) << S_FW_WATCHDOG_CMD_PFN)
#define G_FW_WATCHDOG_CMD_PFN(x)	\
    (((x) >> S_FW_WATCHDOG_CMD_PFN) & M_FW_WATCHDOG_CMD_PFN)

#define S_FW_WATCHDOG_CMD_VFN		0
#define M_FW_WATCHDOG_CMD_VFN		0xff
#define V_FW_WATCHDOG_CMD_VFN(x)	((x) << S_FW_WATCHDOG_CMD_VFN)
#define G_FW_WATCHDOG_CMD_VFN(x)	\
    (((x) >> S_FW_WATCHDOG_CMD_VFN) & M_FW_WATCHDOG_CMD_VFN)

struct fw_clip_cmd {
	__be32 op_to_write;
	__be32 alloc_to_len16;
	__be64 ip_hi;
	__be64 ip_lo;
	__be32 r4[2];
};

#define S_FW_CLIP_CMD_ALLOC		31
#define M_FW_CLIP_CMD_ALLOC		0x1
#define V_FW_CLIP_CMD_ALLOC(x)		((x) << S_FW_CLIP_CMD_ALLOC)
#define G_FW_CLIP_CMD_ALLOC(x)		\
    (((x) >> S_FW_CLIP_CMD_ALLOC) & M_FW_CLIP_CMD_ALLOC)
#define F_FW_CLIP_CMD_ALLOC		V_FW_CLIP_CMD_ALLOC(1U)

#define S_FW_CLIP_CMD_FREE		30
#define M_FW_CLIP_CMD_FREE		0x1
#define V_FW_CLIP_CMD_FREE(x)		((x) << S_FW_CLIP_CMD_FREE)
#define G_FW_CLIP_CMD_FREE(x)		\
    (((x) >> S_FW_CLIP_CMD_FREE) & M_FW_CLIP_CMD_FREE)
#define F_FW_CLIP_CMD_FREE		V_FW_CLIP_CMD_FREE(1U)

#define S_FW_CLIP_CMD_INDEX	16
#define M_FW_CLIP_CMD_INDEX	0x1fff
#define V_FW_CLIP_CMD_INDEX(x)	((x) << S_FW_CLIP_CMD_INDEX)
#define G_FW_CLIP_CMD_INDEX(x)	\
    (((x) >> S_FW_CLIP_CMD_INDEX) & M_FW_CLIP_CMD_INDEX)

/******************************************************************************
 *   F O i S C S I   C O M M A N D s
 **************************************/

#define	FW_CHNET_IFACE_ADDR_MAX	3

enum fw_chnet_iface_cmd_subop {
	FW_CHNET_IFACE_CMD_SUBOP_NOOP = 0,

	FW_CHNET_IFACE_CMD_SUBOP_LINK_UP,
	FW_CHNET_IFACE_CMD_SUBOP_LINK_DOWN,

	FW_CHNET_IFACE_CMD_SUBOP_MTU_SET,
	FW_CHNET_IFACE_CMD_SUBOP_MTU_GET,

	FW_CHNET_IFACE_CMD_SUBOP_MAX,
};

struct fw_chnet_iface_cmd {
	__be32 op_to_portid;
	__be32 retval_len16;
	__u8   subop;
	__u8   r2[2];
	__u8   flags;
	__be32 ifid_ifstate;
	__be16 mtu;
	__be16 vlanid;
	__be32 r3;
	__be16 r4;
	__u8   mac[6];
};

#define S_FW_CHNET_IFACE_CMD_PORTID	0
#define M_FW_CHNET_IFACE_CMD_PORTID	0xf
#define V_FW_CHNET_IFACE_CMD_PORTID(x)	((x) << S_FW_CHNET_IFACE_CMD_PORTID)
#define G_FW_CHNET_IFACE_CMD_PORTID(x)	\
    (((x) >> S_FW_CHNET_IFACE_CMD_PORTID) & M_FW_CHNET_IFACE_CMD_PORTID)

#define S_FW_CHNET_IFACE_CMD_RSS_IQID		16
#define M_FW_CHNET_IFACE_CMD_RSS_IQID		0xffff
#define V_FW_CHNET_IFACE_CMD_RSS_IQID(x)	\
    ((x) << S_FW_CHNET_IFACE_CMD_RSS_IQID)
#define G_FW_CHNET_IFACE_CMD_RSS_IQID(x)	\
    (((x) >> S_FW_CHNET_IFACE_CMD_RSS_IQID) & M_FW_CHNET_IFACE_CMD_RSS_IQID)

#define S_FW_CHNET_IFACE_CMD_RSS_IQID_F		0
#define M_FW_CHNET_IFACE_CMD_RSS_IQID_F		0x1
#define V_FW_CHNET_IFACE_CMD_RSS_IQID_F(x)	\
    ((x) << S_FW_CHNET_IFACE_CMD_RSS_IQID_F)
#define G_FW_CHNET_IFACE_CMD_RSS_IQID_F(x)	\
    (((x) >> S_FW_CHNET_IFACE_CMD_RSS_IQID_F) &	\
    M_FW_CHNET_IFACE_CMD_RSS_IQID_F)
#define F_FW_CHNET_IFACE_CMD_RSS_IQID_F V_FW_CHNET_IFACE_CMD_RSS_IQID_F(1U)

#define S_FW_CHNET_IFACE_CMD_IFID	8
#define M_FW_CHNET_IFACE_CMD_IFID	0xffffff
#define V_FW_CHNET_IFACE_CMD_IFID(x)	((x) << S_FW_CHNET_IFACE_CMD_IFID)
#define G_FW_CHNET_IFACE_CMD_IFID(x)	\
    (((x) >> S_FW_CHNET_IFACE_CMD_IFID) & M_FW_CHNET_IFACE_CMD_IFID)

#define S_FW_CHNET_IFACE_CMD_IFSTATE	0
#define M_FW_CHNET_IFACE_CMD_IFSTATE	0xff
#define V_FW_CHNET_IFACE_CMD_IFSTATE(x)	((x) << S_FW_CHNET_IFACE_CMD_IFSTATE)
#define G_FW_CHNET_IFACE_CMD_IFSTATE(x)	\
    (((x) >> S_FW_CHNET_IFACE_CMD_IFSTATE) & M_FW_CHNET_IFACE_CMD_IFSTATE)

struct fw_fcoe_res_info_cmd {
	__be32 op_to_read;
	__be32 retval_len16;
	__be16 e_d_tov;
	__be16 r_a_tov_seq;
	__be16 r_a_tov_els;
	__be16 r_r_tov;
	__be32 max_xchgs;
	__be32 max_ssns;
	__be32 used_xchgs;
	__be32 used_ssns;
	__be32 max_fcfs;
	__be32 max_vnps;
	__be32 used_fcfs;
	__be32 used_vnps;
};

struct fw_fcoe_link_cmd {
	__be32 op_to_portid;
	__be32 retval_len16;
	__be32 sub_opcode_fcfi;
	__u8   r3;
	__u8   lstatus;
	__be16 flags;
	__u8   r4;
	__u8   set_vlan;
	__be16 vlan_id;
	__be32 vnpi_pkd;
	__be16 r6;
	__u8   phy_mac[6];
	__u8   vnport_wwnn[8];
	__u8   vnport_wwpn[8];
};

#define S_FW_FCOE_LINK_CMD_PORTID	0
#define M_FW_FCOE_LINK_CMD_PORTID	0xf
#define V_FW_FCOE_LINK_CMD_PORTID(x)	((x) << S_FW_FCOE_LINK_CMD_PORTID)
#define G_FW_FCOE_LINK_CMD_PORTID(x)	\
    (((x) >> S_FW_FCOE_LINK_CMD_PORTID) & M_FW_FCOE_LINK_CMD_PORTID)

#define S_FW_FCOE_LINK_CMD_SUB_OPCODE	24
#define M_FW_FCOE_LINK_CMD_SUB_OPCODE	0xff
#define V_FW_FCOE_LINK_CMD_SUB_OPCODE(x) \
    ((x) << S_FW_FCOE_LINK_CMD_SUB_OPCODE)
#define G_FW_FCOE_LINK_CMD_SUB_OPCODE(x) \
    (((x) >> S_FW_FCOE_LINK_CMD_SUB_OPCODE) & M_FW_FCOE_LINK_CMD_SUB_OPCODE)

#define S_FW_FCOE_LINK_CMD_FCFI		0
#define M_FW_FCOE_LINK_CMD_FCFI		0xffffff
#define V_FW_FCOE_LINK_CMD_FCFI(x)	((x) << S_FW_FCOE_LINK_CMD_FCFI)
#define G_FW_FCOE_LINK_CMD_FCFI(x)	\
    (((x) >> S_FW_FCOE_LINK_CMD_FCFI) & M_FW_FCOE_LINK_CMD_FCFI)

#define S_FW_FCOE_LINK_CMD_VNPI		0
#define M_FW_FCOE_LINK_CMD_VNPI		0xfffff
#define V_FW_FCOE_LINK_CMD_VNPI(x)	((x) << S_FW_FCOE_LINK_CMD_VNPI)
#define G_FW_FCOE_LINK_CMD_VNPI(x)	\
    (((x) >> S_FW_FCOE_LINK_CMD_VNPI) & M_FW_FCOE_LINK_CMD_VNPI)

struct fw_fcoe_vnp_cmd {
	__be32 op_to_fcfi;
	__be32 alloc_to_len16;
	__be32 gen_wwn_to_vnpi;
	__be32 vf_id;
	__be16 iqid;
	__u8   vnport_mac[6];
	__u8   vnport_wwnn[8];
	__u8   vnport_wwpn[8];
	__u8   cmn_srv_parms[16];
	__u8   clsp_word_0_1[8];
};

#define S_FW_FCOE_VNP_CMD_FCFI		0
#define M_FW_FCOE_VNP_CMD_FCFI		0xfffff
#define V_FW_FCOE_VNP_CMD_FCFI(x)	((x) << S_FW_FCOE_VNP_CMD_FCFI)
#define G_FW_FCOE_VNP_CMD_FCFI(x)	\
    (((x) >> S_FW_FCOE_VNP_CMD_FCFI) & M_FW_FCOE_VNP_CMD_FCFI)

#define S_FW_FCOE_VNP_CMD_ALLOC		31
#define M_FW_FCOE_VNP_CMD_ALLOC		0x1
#define V_FW_FCOE_VNP_CMD_ALLOC(x)	((x) << S_FW_FCOE_VNP_CMD_ALLOC)
#define G_FW_FCOE_VNP_CMD_ALLOC(x)	\
    (((x) >> S_FW_FCOE_VNP_CMD_ALLOC) & M_FW_FCOE_VNP_CMD_ALLOC)
#define F_FW_FCOE_VNP_CMD_ALLOC		V_FW_FCOE_VNP_CMD_ALLOC(1U)

#define S_FW_FCOE_VNP_CMD_FREE		30
#define M_FW_FCOE_VNP_CMD_FREE		0x1
#define V_FW_FCOE_VNP_CMD_FREE(x)	((x) << S_FW_FCOE_VNP_CMD_FREE)
#define G_FW_FCOE_VNP_CMD_FREE(x)	\
    (((x) >> S_FW_FCOE_VNP_CMD_FREE) & M_FW_FCOE_VNP_CMD_FREE)
#define F_FW_FCOE_VNP_CMD_FREE		V_FW_FCOE_VNP_CMD_FREE(1U)

#define S_FW_FCOE_VNP_CMD_MODIFY	29
#define M_FW_FCOE_VNP_CMD_MODIFY	0x1
#define V_FW_FCOE_VNP_CMD_MODIFY(x)	((x) << S_FW_FCOE_VNP_CMD_MODIFY)
#define G_FW_FCOE_VNP_CMD_MODIFY(x)	\
    (((x) >> S_FW_FCOE_VNP_CMD_MODIFY) & M_FW_FCOE_VNP_CMD_MODIFY)
#define F_FW_FCOE_VNP_CMD_MODIFY	V_FW_FCOE_VNP_CMD_MODIFY(1U)

#define S_FW_FCOE_VNP_CMD_GEN_WWN	22
#define M_FW_FCOE_VNP_CMD_GEN_WWN	0x1
#define V_FW_FCOE_VNP_CMD_GEN_WWN(x)	((x) << S_FW_FCOE_VNP_CMD_GEN_WWN)
#define G_FW_FCOE_VNP_CMD_GEN_WWN(x)	\
    (((x) >> S_FW_FCOE_VNP_CMD_GEN_WWN) & M_FW_FCOE_VNP_CMD_GEN_WWN)
#define F_FW_FCOE_VNP_CMD_GEN_WWN	V_FW_FCOE_VNP_CMD_GEN_WWN(1U)

#define S_FW_FCOE_VNP_CMD_PERSIST	21
#define M_FW_FCOE_VNP_CMD_PERSIST	0x1
#define V_FW_FCOE_VNP_CMD_PERSIST(x)	((x) << S_FW_FCOE_VNP_CMD_PERSIST)
#define G_FW_FCOE_VNP_CMD_PERSIST(x)	\
    (((x) >> S_FW_FCOE_VNP_CMD_PERSIST) & M_FW_FCOE_VNP_CMD_PERSIST)
#define F_FW_FCOE_VNP_CMD_PERSIST	V_FW_FCOE_VNP_CMD_PERSIST(1U)

#define S_FW_FCOE_VNP_CMD_VFID_EN	20
#define M_FW_FCOE_VNP_CMD_VFID_EN	0x1
#define V_FW_FCOE_VNP_CMD_VFID_EN(x)	((x) << S_FW_FCOE_VNP_CMD_VFID_EN)
#define G_FW_FCOE_VNP_CMD_VFID_EN(x)	\
    (((x) >> S_FW_FCOE_VNP_CMD_VFID_EN) & M_FW_FCOE_VNP_CMD_VFID_EN)
#define F_FW_FCOE_VNP_CMD_VFID_EN	V_FW_FCOE_VNP_CMD_VFID_EN(1U)

#define S_FW_FCOE_VNP_CMD_VNPI		0
#define M_FW_FCOE_VNP_CMD_VNPI		0xfffff
#define V_FW_FCOE_VNP_CMD_VNPI(x)	((x) << S_FW_FCOE_VNP_CMD_VNPI)
#define G_FW_FCOE_VNP_CMD_VNPI(x)	\
    (((x) >> S_FW_FCOE_VNP_CMD_VNPI) & M_FW_FCOE_VNP_CMD_VNPI)

struct fw_fcoe_sparams_cmd {
	__be32 op_to_portid;
	__be32 retval_len16;
	__u8   r3[7];
	__u8   cos;
	__u8   lport_wwnn[8];
	__u8   lport_wwpn[8];
	__u8   cmn_srv_parms[16];
	__u8   cls_srv_parms[16];
};

#define S_FW_FCOE_SPARAMS_CMD_PORTID	0
#define M_FW_FCOE_SPARAMS_CMD_PORTID	0xf
#define V_FW_FCOE_SPARAMS_CMD_PORTID(x)	((x) << S_FW_FCOE_SPARAMS_CMD_PORTID)
#define G_FW_FCOE_SPARAMS_CMD_PORTID(x)	\
    (((x) >> S_FW_FCOE_SPARAMS_CMD_PORTID) & M_FW_FCOE_SPARAMS_CMD_PORTID)

struct fw_fcoe_stats_cmd {
	__be32 op_to_flowid;
	__be32 free_to_len16;
	union fw_fcoe_stats {
		struct fw_fcoe_stats_ctl {
			__u8   nstats_port;
			__u8   port_valid_ix;
			__be16 r6;
			__be32 r7;
			__be64 stat0;
			__be64 stat1;
			__be64 stat2;
			__be64 stat3;
			__be64 stat4;
			__be64 stat5;
		} ctl;
		struct fw_fcoe_port_stats {
			__be64 tx_bcast_bytes;
			__be64 tx_bcast_frames;
			__be64 tx_mcast_bytes;
			__be64 tx_mcast_frames;
			__be64 tx_ucast_bytes;
			__be64 tx_ucast_frames;
			__be64 tx_drop_frames;
			__be64 tx_offload_bytes;
			__be64 tx_offload_frames;
			__be64 rx_bcast_bytes;
			__be64 rx_bcast_frames;
			__be64 rx_mcast_bytes;
			__be64 rx_mcast_frames;
			__be64 rx_ucast_bytes;
			__be64 rx_ucast_frames;
			__be64 rx_err_frames;
		} port_stats;
		struct fw_fcoe_fcf_stats {
			__be32 fip_tx_bytes;
			__be32 fip_tx_fr;
			__be64 fcf_ka;
			__be64 mcast_adv_rcvd;
			__be16 ucast_adv_rcvd;
			__be16 sol_sent;
			__be16 vlan_req;
			__be16 vlan_rpl;
			__be16 clr_vlink;
			__be16 link_down;
			__be16 link_up;
			__be16 logo;
			__be16 flogi_req;
			__be16 flogi_rpl;
			__be16 fdisc_req;
			__be16 fdisc_rpl;
			__be16 fka_prd_chg;
			__be16 fc_map_chg;
			__be16 vfid_chg;
			__u8   no_fka_req;
			__u8   no_vnp;
		} fcf_stats;
		struct fw_fcoe_pcb_stats {
			__be64 tx_bytes;
			__be64 tx_frames;
			__be64 rx_bytes;
			__be64 rx_frames;
			__be32 vnp_ka;
			__be32 unsol_els_rcvd;
			__be64 unsol_cmd_rcvd;
			__be16 implicit_logo;
			__be16 flogi_inv_sparm;
			__be16 fdisc_inv_sparm;
			__be16 flogi_rjt;
			__be16 fdisc_rjt;
			__be16 no_ssn;
			__be16 mac_flt_fail;
			__be16 inv_fr_rcvd;
		} pcb_stats;
		struct fw_fcoe_scb_stats {
			__be64 tx_bytes;
			__be64 tx_frames;
			__be64 rx_bytes;
			__be64 rx_frames;
			__be32 host_abrt_req;
			__be32 adap_auto_abrt;
			__be32 adap_abrt_rsp;
			__be32 host_ios_req;
			__be16 ssn_offl_ios;
			__be16 ssn_not_rdy_ios;
			__u8   rx_data_ddp_err;
			__u8   ddp_flt_set_err;
			__be16 rx_data_fr_err;
			__u8   bad_st_abrt_req;
			__u8   no_io_abrt_req;
			__u8   abort_tmo;
			__u8   abort_tmo_2;
			__be32 abort_req;
			__u8   no_ppod_res_tmo;
			__u8   bp_tmo;
			__u8   adap_auto_cls;
			__u8   no_io_cls_req;
			__be32 host_cls_req;
			__be64 unsol_cmd_rcvd;
			__be32 plogi_req_rcvd;
			__be32 prli_req_rcvd;
			__be16 logo_req_rcvd;
			__be16 prlo_req_rcvd;
			__be16 plogi_rjt_rcvd;
			__be16 prli_rjt_rcvd;
			__be32 adisc_req_rcvd;
			__be32 rscn_rcvd;
			__be32 rrq_req_rcvd;
			__be32 unsol_els_rcvd;
			__u8   adisc_rjt_rcvd;
			__u8   scr_rjt;
			__u8   ct_rjt;
			__u8   inval_bls_rcvd;
			__be32 ba_rjt_rcvd;
		} scb_stats;
	} u;
};

#define S_FW_FCOE_STATS_CMD_FLOWID	0
#define M_FW_FCOE_STATS_CMD_FLOWID	0xfffff
#define V_FW_FCOE_STATS_CMD_FLOWID(x)	((x) << S_FW_FCOE_STATS_CMD_FLOWID)
#define G_FW_FCOE_STATS_CMD_FLOWID(x)	\
    (((x) >> S_FW_FCOE_STATS_CMD_FLOWID) & M_FW_FCOE_STATS_CMD_FLOWID)

#define S_FW_FCOE_STATS_CMD_FREE	30
#define M_FW_FCOE_STATS_CMD_FREE	0x1
#define V_FW_FCOE_STATS_CMD_FREE(x)	((x) << S_FW_FCOE_STATS_CMD_FREE)
#define G_FW_FCOE_STATS_CMD_FREE(x)	\
    (((x) >> S_FW_FCOE_STATS_CMD_FREE) & M_FW_FCOE_STATS_CMD_FREE)
#define F_FW_FCOE_STATS_CMD_FREE	V_FW_FCOE_STATS_CMD_FREE(1U)

#define S_FW_FCOE_STATS_CMD_NSTATS	4
#define M_FW_FCOE_STATS_CMD_NSTATS	0x7
#define V_FW_FCOE_STATS_CMD_NSTATS(x)	((x) << S_FW_FCOE_STATS_CMD_NSTATS)
#define G_FW_FCOE_STATS_CMD_NSTATS(x)	\
    (((x) >> S_FW_FCOE_STATS_CMD_NSTATS) & M_FW_FCOE_STATS_CMD_NSTATS)

#define S_FW_FCOE_STATS_CMD_PORT	0
#define M_FW_FCOE_STATS_CMD_PORT	0x3
#define V_FW_FCOE_STATS_CMD_PORT(x)	((x) << S_FW_FCOE_STATS_CMD_PORT)
#define G_FW_FCOE_STATS_CMD_PORT(x)	\
    (((x) >> S_FW_FCOE_STATS_CMD_PORT) & M_FW_FCOE_STATS_CMD_PORT)

#define S_FW_FCOE_STATS_CMD_PORT_VALID	7
#define M_FW_FCOE_STATS_CMD_PORT_VALID	0x1
#define V_FW_FCOE_STATS_CMD_PORT_VALID(x) \
    ((x) << S_FW_FCOE_STATS_CMD_PORT_VALID)
#define G_FW_FCOE_STATS_CMD_PORT_VALID(x) \
    (((x) >> S_FW_FCOE_STATS_CMD_PORT_VALID) & M_FW_FCOE_STATS_CMD_PORT_VALID)
#define F_FW_FCOE_STATS_CMD_PORT_VALID	V_FW_FCOE_STATS_CMD_PORT_VALID(1U)

#define S_FW_FCOE_STATS_CMD_IX		0
#define M_FW_FCOE_STATS_CMD_IX		0x3f
#define V_FW_FCOE_STATS_CMD_IX(x)	((x) << S_FW_FCOE_STATS_CMD_IX)
#define G_FW_FCOE_STATS_CMD_IX(x)	\
    (((x) >> S_FW_FCOE_STATS_CMD_IX) & M_FW_FCOE_STATS_CMD_IX)

struct fw_fcoe_fcf_cmd {
	__be32 op_to_fcfi;
	__be32 retval_len16;
	__be16 priority_pkd;
	__u8   mac[6];
	__u8   name_id[8];
	__u8   fabric[8];
	__be16 vf_id;
	__be16 max_fcoe_size;
	__u8   vlan_id;
	__u8   fc_map[3];
	__be32 fka_adv;
	__be32 r6;
	__u8   r7_hi;
	__u8   fpma_to_portid;
	__u8   spma_mac[6];
	__be64 r8;
};

#define S_FW_FCOE_FCF_CMD_FCFI		0
#define M_FW_FCOE_FCF_CMD_FCFI		0xfffff
#define V_FW_FCOE_FCF_CMD_FCFI(x)	((x) << S_FW_FCOE_FCF_CMD_FCFI)
#define G_FW_FCOE_FCF_CMD_FCFI(x)	\
    (((x) >> S_FW_FCOE_FCF_CMD_FCFI) & M_FW_FCOE_FCF_CMD_FCFI)

#define S_FW_FCOE_FCF_CMD_PRIORITY	0
#define M_FW_FCOE_FCF_CMD_PRIORITY	0xff
#define V_FW_FCOE_FCF_CMD_PRIORITY(x)	((x) << S_FW_FCOE_FCF_CMD_PRIORITY)
#define G_FW_FCOE_FCF_CMD_PRIORITY(x)	\
    (((x) >> S_FW_FCOE_FCF_CMD_PRIORITY) & M_FW_FCOE_FCF_CMD_PRIORITY)

#define S_FW_FCOE_FCF_CMD_FPMA		6
#define M_FW_FCOE_FCF_CMD_FPMA		0x1
#define V_FW_FCOE_FCF_CMD_FPMA(x)	((x) << S_FW_FCOE_FCF_CMD_FPMA)
#define G_FW_FCOE_FCF_CMD_FPMA(x)	\
    (((x) >> S_FW_FCOE_FCF_CMD_FPMA) & M_FW_FCOE_FCF_CMD_FPMA)
#define F_FW_FCOE_FCF_CMD_FPMA		V_FW_FCOE_FCF_CMD_FPMA(1U)

#define S_FW_FCOE_FCF_CMD_SPMA		5
#define M_FW_FCOE_FCF_CMD_SPMA		0x1
#define V_FW_FCOE_FCF_CMD_SPMA(x)	((x) << S_FW_FCOE_FCF_CMD_SPMA)
#define G_FW_FCOE_FCF_CMD_SPMA(x)	\
    (((x) >> S_FW_FCOE_FCF_CMD_SPMA) & M_FW_FCOE_FCF_CMD_SPMA)
#define F_FW_FCOE_FCF_CMD_SPMA		V_FW_FCOE_FCF_CMD_SPMA(1U)

#define S_FW_FCOE_FCF_CMD_LOGIN		4
#define M_FW_FCOE_FCF_CMD_LOGIN		0x1
#define V_FW_FCOE_FCF_CMD_LOGIN(x)	((x) << S_FW_FCOE_FCF_CMD_LOGIN)
#define G_FW_FCOE_FCF_CMD_LOGIN(x)	\
    (((x) >> S_FW_FCOE_FCF_CMD_LOGIN) & M_FW_FCOE_FCF_CMD_LOGIN)
#define F_FW_FCOE_FCF_CMD_LOGIN		V_FW_FCOE_FCF_CMD_LOGIN(1U)

#define S_FW_FCOE_FCF_CMD_PORTID	0
#define M_FW_FCOE_FCF_CMD_PORTID	0xf
#define V_FW_FCOE_FCF_CMD_PORTID(x)	((x) << S_FW_FCOE_FCF_CMD_PORTID)
#define G_FW_FCOE_FCF_CMD_PORTID(x)	\
    (((x) >> S_FW_FCOE_FCF_CMD_PORTID) & M_FW_FCOE_FCF_CMD_PORTID)

/******************************************************************************
 *   E R R O R   a n d   D E B U G   C O M M A N D s
 ******************************************************/

enum fw_error_type {
	FW_ERROR_TYPE_EXCEPTION		= 0x0,
	FW_ERROR_TYPE_HWMODULE		= 0x1,
	FW_ERROR_TYPE_WR		= 0x2,
	FW_ERROR_TYPE_ACL		= 0x3,
};

enum fw_dcb_ieee_locations {
	FW_IEEE_LOC_LOCAL,
	FW_IEEE_LOC_PEER,
	FW_IEEE_LOC_OPERATIONAL,
};

struct fw_dcb_ieee_cmd {
	__be32 op_to_location;
	__be32 changed_to_len16;
	union fw_dcbx_stats {
		struct fw_dcbx_pfc_stats_ieee {
			__be32 pfc_mbc_pkd;
			__be32 pfc_willing_to_pfc_en;
		} dcbx_pfc_stats;
		struct fw_dcbx_ets_stats_ieee {
			__be32 cbs_to_ets_max_tc;
			__be32 pg_table;
			__u8   pg_percent[8];
			__u8   tsa[8];
		} dcbx_ets_stats;
		struct fw_dcbx_app_stats_ieee {
			__be32 num_apps_pkd;
			__be32 r6;
			__be32 app[4];
		} dcbx_app_stats;
		struct fw_dcbx_control {
			__be32 multi_peer_invalidated;
			__u8 version;
			__u8 r6[3];
		} dcbx_control;
	} u;
};

#define S_FW_DCB_IEEE_CMD_PORT		8
#define M_FW_DCB_IEEE_CMD_PORT		0x7
#define V_FW_DCB_IEEE_CMD_PORT(x)	((x) << S_FW_DCB_IEEE_CMD_PORT)
#define G_FW_DCB_IEEE_CMD_PORT(x)	\
    (((x) >> S_FW_DCB_IEEE_CMD_PORT) & M_FW_DCB_IEEE_CMD_PORT)

#define S_FW_DCB_IEEE_CMD_FEATURE	2
#define M_FW_DCB_IEEE_CMD_FEATURE	0x7
#define V_FW_DCB_IEEE_CMD_FEATURE(x)	((x) << S_FW_DCB_IEEE_CMD_FEATURE)
#define G_FW_DCB_IEEE_CMD_FEATURE(x)	\
    (((x) >> S_FW_DCB_IEEE_CMD_FEATURE) & M_FW_DCB_IEEE_CMD_FEATURE)

#define S_FW_DCB_IEEE_CMD_LOCATION	0
#define M_FW_DCB_IEEE_CMD_LOCATION	0x3
#define V_FW_DCB_IEEE_CMD_LOCATION(x)	((x) << S_FW_DCB_IEEE_CMD_LOCATION)
#define G_FW_DCB_IEEE_CMD_LOCATION(x)	\
    (((x) >> S_FW_DCB_IEEE_CMD_LOCATION) & M_FW_DCB_IEEE_CMD_LOCATION)

#define S_FW_DCB_IEEE_CMD_CHANGED	20
#define M_FW_DCB_IEEE_CMD_CHANGED	0x1
#define V_FW_DCB_IEEE_CMD_CHANGED(x)	((x) << S_FW_DCB_IEEE_CMD_CHANGED)
#define G_FW_DCB_IEEE_CMD_CHANGED(x)	\
    (((x) >> S_FW_DCB_IEEE_CMD_CHANGED) & M_FW_DCB_IEEE_CMD_CHANGED)
#define F_FW_DCB_IEEE_CMD_CHANGED	V_FW_DCB_IEEE_CMD_CHANGED(1U)

#define S_FW_DCB_IEEE_CMD_RECEIVED	19
#define M_FW_DCB_IEEE_CMD_RECEIVED	0x1
#define V_FW_DCB_IEEE_CMD_RECEIVED(x)	((x) << S_FW_DCB_IEEE_CMD_RECEIVED)
#define G_FW_DCB_IEEE_CMD_RECEIVED(x)	\
    (((x) >> S_FW_DCB_IEEE_CMD_RECEIVED) & M_FW_DCB_IEEE_CMD_RECEIVED)
#define F_FW_DCB_IEEE_CMD_RECEIVED	V_FW_DCB_IEEE_CMD_RECEIVED(1U)

#define S_FW_DCB_IEEE_CMD_APPLY		18
#define M_FW_DCB_IEEE_CMD_APPLY		0x1
#define V_FW_DCB_IEEE_CMD_APPLY(x)	((x) << S_FW_DCB_IEEE_CMD_APPLY)
#define G_FW_DCB_IEEE_CMD_APPLY(x)	\
    (((x) >> S_FW_DCB_IEEE_CMD_APPLY) & M_FW_DCB_IEEE_CMD_APPLY)
#define F_FW_DCB_IEEE_CMD_APPLY	V_FW_DCB_IEEE_CMD_APPLY(1U)

#define S_FW_DCB_IEEE_CMD_DISABLED	17
#define M_FW_DCB_IEEE_CMD_DISABLED	0x1
#define V_FW_DCB_IEEE_CMD_DISABLED(x)	((x) << S_FW_DCB_IEEE_CMD_DISABLED)
#define G_FW_DCB_IEEE_CMD_DISABLED(x)	\
    (((x) >> S_FW_DCB_IEEE_CMD_DISABLED) & M_FW_DCB_IEEE_CMD_DISABLED)
#define F_FW_DCB_IEEE_CMD_DISABLED	V_FW_DCB_IEEE_CMD_DISABLED(1U)

#define S_FW_DCB_IEEE_CMD_MORE		16
#define M_FW_DCB_IEEE_CMD_MORE		0x1
#define V_FW_DCB_IEEE_CMD_MORE(x)	((x) << S_FW_DCB_IEEE_CMD_MORE)
#define G_FW_DCB_IEEE_CMD_MORE(x)	\
    (((x) >> S_FW_DCB_IEEE_CMD_MORE) & M_FW_DCB_IEEE_CMD_MORE)
#define F_FW_DCB_IEEE_CMD_MORE	V_FW_DCB_IEEE_CMD_MORE(1U)

#define S_FW_DCB_IEEE_CMD_PFC_MBC	0
#define M_FW_DCB_IEEE_CMD_PFC_MBC	0x1
#define V_FW_DCB_IEEE_CMD_PFC_MBC(x)	((x) << S_FW_DCB_IEEE_CMD_PFC_MBC)
#define G_FW_DCB_IEEE_CMD_PFC_MBC(x)	\
    (((x) >> S_FW_DCB_IEEE_CMD_PFC_MBC) & M_FW_DCB_IEEE_CMD_PFC_MBC)
#define F_FW_DCB_IEEE_CMD_PFC_MBC	V_FW_DCB_IEEE_CMD_PFC_MBC(1U)

#define S_FW_DCB_IEEE_CMD_PFC_WILLING		16
#define M_FW_DCB_IEEE_CMD_PFC_WILLING		0x1
#define V_FW_DCB_IEEE_CMD_PFC_WILLING(x)	\
    ((x) << S_FW_DCB_IEEE_CMD_PFC_WILLING)
#define G_FW_DCB_IEEE_CMD_PFC_WILLING(x)	\
    (((x) >> S_FW_DCB_IEEE_CMD_PFC_WILLING) & M_FW_DCB_IEEE_CMD_PFC_WILLING)
#define F_FW_DCB_IEEE_CMD_PFC_WILLING	V_FW_DCB_IEEE_CMD_PFC_WILLING(1U)

#define S_FW_DCB_IEEE_CMD_PFC_MAX_TC	8
#define M_FW_DCB_IEEE_CMD_PFC_MAX_TC	0xff
#define V_FW_DCB_IEEE_CMD_PFC_MAX_TC(x)	((x) << S_FW_DCB_IEEE_CMD_PFC_MAX_TC)
#define G_FW_DCB_IEEE_CMD_PFC_MAX_TC(x)	\
    (((x) >> S_FW_DCB_IEEE_CMD_PFC_MAX_TC) & M_FW_DCB_IEEE_CMD_PFC_MAX_TC)

#define S_FW_DCB_IEEE_CMD_PFC_EN	0
#define M_FW_DCB_IEEE_CMD_PFC_EN	0xff
#define V_FW_DCB_IEEE_CMD_PFC_EN(x)	((x) << S_FW_DCB_IEEE_CMD_PFC_EN)
#define G_FW_DCB_IEEE_CMD_PFC_EN(x)	\
    (((x) >> S_FW_DCB_IEEE_CMD_PFC_EN) & M_FW_DCB_IEEE_CMD_PFC_EN)

#define S_FW_DCB_IEEE_CMD_CBS		16
#define M_FW_DCB_IEEE_CMD_CBS		0x1
#define V_FW_DCB_IEEE_CMD_CBS(x)	((x) << S_FW_DCB_IEEE_CMD_CBS)
#define G_FW_DCB_IEEE_CMD_CBS(x)	\
    (((x) >> S_FW_DCB_IEEE_CMD_CBS) & M_FW_DCB_IEEE_CMD_CBS)
#define F_FW_DCB_IEEE_CMD_CBS	V_FW_DCB_IEEE_CMD_CBS(1U)

#define S_FW_DCB_IEEE_CMD_ETS_WILLING		8
#define M_FW_DCB_IEEE_CMD_ETS_WILLING		0x1
#define V_FW_DCB_IEEE_CMD_ETS_WILLING(x)	\
    ((x) << S_FW_DCB_IEEE_CMD_ETS_WILLING)
#define G_FW_DCB_IEEE_CMD_ETS_WILLING(x)	\
    (((x) >> S_FW_DCB_IEEE_CMD_ETS_WILLING) & M_FW_DCB_IEEE_CMD_ETS_WILLING)
#define F_FW_DCB_IEEE_CMD_ETS_WILLING	V_FW_DCB_IEEE_CMD_ETS_WILLING(1U)

#define S_FW_DCB_IEEE_CMD_ETS_MAX_TC	0
#define M_FW_DCB_IEEE_CMD_ETS_MAX_TC	0xff
#define V_FW_DCB_IEEE_CMD_ETS_MAX_TC(x)	((x) << S_FW_DCB_IEEE_CMD_ETS_MAX_TC)
#define G_FW_DCB_IEEE_CMD_ETS_MAX_TC(x)	\
    (((x) >> S_FW_DCB_IEEE_CMD_ETS_MAX_TC) & M_FW_DCB_IEEE_CMD_ETS_MAX_TC)

#define S_FW_DCB_IEEE_CMD_NUM_APPS	0
#define M_FW_DCB_IEEE_CMD_NUM_APPS	0x7
#define V_FW_DCB_IEEE_CMD_NUM_APPS(x)	((x) << S_FW_DCB_IEEE_CMD_NUM_APPS)
#define G_FW_DCB_IEEE_CMD_NUM_APPS(x)	\
    (((x) >> S_FW_DCB_IEEE_CMD_NUM_APPS) & M_FW_DCB_IEEE_CMD_NUM_APPS)

#define S_FW_DCB_IEEE_CMD_MULTI_PEER	31
#define M_FW_DCB_IEEE_CMD_MULTI_PEER	0x1
#define V_FW_DCB_IEEE_CMD_MULTI_PEER(x)	((x) << S_FW_DCB_IEEE_CMD_MULTI_PEER)
#define G_FW_DCB_IEEE_CMD_MULTI_PEER(x)	\
    (((x) >> S_FW_DCB_IEEE_CMD_MULTI_PEER) & M_FW_DCB_IEEE_CMD_MULTI_PEER)
#define F_FW_DCB_IEEE_CMD_MULTI_PEER	V_FW_DCB_IEEE_CMD_MULTI_PEER(1U)

#define S_FW_DCB_IEEE_CMD_INVALIDATED		30
#define M_FW_DCB_IEEE_CMD_INVALIDATED		0x1
#define V_FW_DCB_IEEE_CMD_INVALIDATED(x)	\
    ((x) << S_FW_DCB_IEEE_CMD_INVALIDATED)
#define G_FW_DCB_IEEE_CMD_INVALIDATED(x)	\
    (((x) >> S_FW_DCB_IEEE_CMD_INVALIDATED) & M_FW_DCB_IEEE_CMD_INVALIDATED)
#define F_FW_DCB_IEEE_CMD_INVALIDATED	V_FW_DCB_IEEE_CMD_INVALIDATED(1U)

/* Hand-written */
#define S_FW_DCB_IEEE_CMD_APP_PROTOCOL	16
#define M_FW_DCB_IEEE_CMD_APP_PROTOCOL	0xffff
#define V_FW_DCB_IEEE_CMD_APP_PROTOCOL(x)	((x) << S_FW_DCB_IEEE_CMD_APP_PROTOCOL)
#define G_FW_DCB_IEEE_CMD_APP_PROTOCOL(x)	\
    (((x) >> S_FW_DCB_IEEE_CMD_APP_PROTOCOL) & M_FW_DCB_IEEE_CMD_APP_PROTOCOL)

#define S_FW_DCB_IEEE_CMD_APP_SELECT	3
#define M_FW_DCB_IEEE_CMD_APP_SELECT	0x7
#define V_FW_DCB_IEEE_CMD_APP_SELECT(x)	((x) << S_FW_DCB_IEEE_CMD_APP_SELECT)
#define G_FW_DCB_IEEE_CMD_APP_SELECT(x)	\
    (((x) >> S_FW_DCB_IEEE_CMD_APP_SELECT) & M_FW_DCB_IEEE_CMD_APP_SELECT)

#define S_FW_DCB_IEEE_CMD_APP_PRIORITY	0
#define M_FW_DCB_IEEE_CMD_APP_PRIORITY	0x7
#define V_FW_DCB_IEEE_CMD_APP_PRIORITY(x)	((x) << S_FW_DCB_IEEE_CMD_APP_PRIORITY)
#define G_FW_DCB_IEEE_CMD_APP_PRIORITY(x)	\
    (((x) >> S_FW_DCB_IEEE_CMD_APP_PRIORITY) & M_FW_DCB_IEEE_CMD_APP_PRIORITY)


struct fw_error_cmd {
	__be32 op_to_type;
	__be32 len16_pkd;
	union fw_error {
		struct fw_error_exception {
			__be32 info[6];
		} exception;
		struct fw_error_hwmodule {
			__be32 regaddr;
			__be32 regval;
		} hwmodule;
		struct fw_error_wr {
			__be16 cidx;
			__be16 pfn_vfn;
			__be32 eqid;
			__u8   wrhdr[16];
		} wr;
		struct fw_error_acl {
			__be16 cidx;
			__be16 pfn_vfn;
			__be32 eqid;
			__be16 mv_pkd;
			__u8   val[6];
			__be64 r4;
		} acl;
	} u;
};

#define S_FW_ERROR_CMD_FATAL		4
#define M_FW_ERROR_CMD_FATAL		0x1
#define V_FW_ERROR_CMD_FATAL(x)		((x) << S_FW_ERROR_CMD_FATAL)
#define G_FW_ERROR_CMD_FATAL(x)		\
    (((x) >> S_FW_ERROR_CMD_FATAL) & M_FW_ERROR_CMD_FATAL)
#define F_FW_ERROR_CMD_FATAL		V_FW_ERROR_CMD_FATAL(1U)

#define S_FW_ERROR_CMD_TYPE		0
#define M_FW_ERROR_CMD_TYPE		0xf
#define V_FW_ERROR_CMD_TYPE(x)		((x) << S_FW_ERROR_CMD_TYPE)
#define G_FW_ERROR_CMD_TYPE(x)		\
    (((x) >> S_FW_ERROR_CMD_TYPE) & M_FW_ERROR_CMD_TYPE)

#define S_FW_ERROR_CMD_PFN		8
#define M_FW_ERROR_CMD_PFN		0x7
#define V_FW_ERROR_CMD_PFN(x)		((x) << S_FW_ERROR_CMD_PFN)
#define G_FW_ERROR_CMD_PFN(x)		\
    (((x) >> S_FW_ERROR_CMD_PFN) & M_FW_ERROR_CMD_PFN)

#define S_FW_ERROR_CMD_VFN		0
#define M_FW_ERROR_CMD_VFN		0xff
#define V_FW_ERROR_CMD_VFN(x)		((x) << S_FW_ERROR_CMD_VFN)
#define G_FW_ERROR_CMD_VFN(x)		\
    (((x) >> S_FW_ERROR_CMD_VFN) & M_FW_ERROR_CMD_VFN)

#define S_FW_ERROR_CMD_PFN		8
#define M_FW_ERROR_CMD_PFN		0x7
#define V_FW_ERROR_CMD_PFN(x)		((x) << S_FW_ERROR_CMD_PFN)
#define G_FW_ERROR_CMD_PFN(x)		\
    (((x) >> S_FW_ERROR_CMD_PFN) & M_FW_ERROR_CMD_PFN)

#define S_FW_ERROR_CMD_VFN		0
#define M_FW_ERROR_CMD_VFN		0xff
#define V_FW_ERROR_CMD_VFN(x)		((x) << S_FW_ERROR_CMD_VFN)
#define G_FW_ERROR_CMD_VFN(x)		\
    (((x) >> S_FW_ERROR_CMD_VFN) & M_FW_ERROR_CMD_VFN)

#define S_FW_ERROR_CMD_MV		15
#define M_FW_ERROR_CMD_MV		0x1
#define V_FW_ERROR_CMD_MV(x)		((x) << S_FW_ERROR_CMD_MV)
#define G_FW_ERROR_CMD_MV(x)		\
    (((x) >> S_FW_ERROR_CMD_MV) & M_FW_ERROR_CMD_MV)
#define F_FW_ERROR_CMD_MV		V_FW_ERROR_CMD_MV(1U)

struct fw_debug_cmd {
	__be32 op_type;
	__be32 len16_pkd;
	union fw_debug {
		struct fw_debug_assert {
			__be32 fcid;
			__be32 line;
			__be32 x;
			__be32 y;
			__u8   filename_0_7[8];
			__u8   filename_8_15[8];
			__be64 r3;
		} assert;
		struct fw_debug_prt {
			__be16 dprtstridx;
			__be16 r3[3];
			__be32 dprtstrparam0;
			__be32 dprtstrparam1;
			__be32 dprtstrparam2;
			__be32 dprtstrparam3;
		} prt;
	} u;
};

#define S_FW_DEBUG_CMD_TYPE		0
#define M_FW_DEBUG_CMD_TYPE		0xff
#define V_FW_DEBUG_CMD_TYPE(x)		((x) << S_FW_DEBUG_CMD_TYPE)
#define G_FW_DEBUG_CMD_TYPE(x)		\
    (((x) >> S_FW_DEBUG_CMD_TYPE) & M_FW_DEBUG_CMD_TYPE)

enum fw_diag_cmd_type {
	FW_DIAG_CMD_TYPE_OFLDIAG = 0,
};

enum fw_diag_cmd_ofldiag_op {
	FW_DIAG_CMD_OFLDIAG_TEST_NONE = 0,
	FW_DIAG_CMD_OFLDIAG_TEST_START,
	FW_DIAG_CMD_OFLDIAG_TEST_STOP,
	FW_DIAG_CMD_OFLDIAG_TEST_STATUS,
};

enum fw_diag_cmd_ofldiag_status {
	FW_DIAG_CMD_OFLDIAG_STATUS_IDLE = 0,
	FW_DIAG_CMD_OFLDIAG_STATUS_RUNNING,
	FW_DIAG_CMD_OFLDIAG_STATUS_FAILED,
	FW_DIAG_CMD_OFLDIAG_STATUS_PASSED,
};

struct fw_diag_cmd {
	__be32 op_type;
	__be32 len16_pkd;
	union fw_diag_test {
		struct fw_diag_test_ofldiag {
			__u8   test_op;
			__u8   r3;
			__be16 test_status;
			__be32 duration;
		} ofldiag;
	} u;
};

#define S_FW_DIAG_CMD_TYPE		0
#define M_FW_DIAG_CMD_TYPE		0xff
#define V_FW_DIAG_CMD_TYPE(x)		((x) << S_FW_DIAG_CMD_TYPE)
#define G_FW_DIAG_CMD_TYPE(x)		\
    (((x) >> S_FW_DIAG_CMD_TYPE) & M_FW_DIAG_CMD_TYPE)

struct fw_hma_cmd {
	__be32 op_pkd;
	__be32 retval_len16;
	__be32 mode_to_pcie_params;
	__be32 naddr_size;
	__be32 addr_size_pkd;
	__be32 r6;
	__be64 phy_address[5];
};

#define S_FW_HMA_CMD_MODE	31
#define M_FW_HMA_CMD_MODE	0x1
#define V_FW_HMA_CMD_MODE(x)	((x) << S_FW_HMA_CMD_MODE)
#define G_FW_HMA_CMD_MODE(x)	\
    (((x) >> S_FW_HMA_CMD_MODE) & M_FW_HMA_CMD_MODE)
#define F_FW_HMA_CMD_MODE	V_FW_HMA_CMD_MODE(1U)

#define S_FW_HMA_CMD_SOC	30
#define M_FW_HMA_CMD_SOC	0x1
#define V_FW_HMA_CMD_SOC(x)	((x) << S_FW_HMA_CMD_SOC)
#define G_FW_HMA_CMD_SOC(x)	(((x) >> S_FW_HMA_CMD_SOC) & M_FW_HMA_CMD_SOC)
#define F_FW_HMA_CMD_SOC	V_FW_HMA_CMD_SOC(1U)

#define S_FW_HMA_CMD_EOC	29
#define M_FW_HMA_CMD_EOC	0x1
#define V_FW_HMA_CMD_EOC(x)	((x) << S_FW_HMA_CMD_EOC)
#define G_FW_HMA_CMD_EOC(x)	(((x) >> S_FW_HMA_CMD_EOC) & M_FW_HMA_CMD_EOC)
#define F_FW_HMA_CMD_EOC	V_FW_HMA_CMD_EOC(1U)

#define S_FW_HMA_CMD_PCIE_PARAMS	0
#define M_FW_HMA_CMD_PCIE_PARAMS	0x7ffffff
#define V_FW_HMA_CMD_PCIE_PARAMS(x)	((x) << S_FW_HMA_CMD_PCIE_PARAMS)
#define G_FW_HMA_CMD_PCIE_PARAMS(x)	\
    (((x) >> S_FW_HMA_CMD_PCIE_PARAMS) & M_FW_HMA_CMD_PCIE_PARAMS)

#define S_FW_HMA_CMD_NADDR	12
#define M_FW_HMA_CMD_NADDR	0x3f
#define V_FW_HMA_CMD_NADDR(x)	((x) << S_FW_HMA_CMD_NADDR)
#define G_FW_HMA_CMD_NADDR(x)	\
    (((x) >> S_FW_HMA_CMD_NADDR) & M_FW_HMA_CMD_NADDR)

#define S_FW_HMA_CMD_SIZE	0
#define M_FW_HMA_CMD_SIZE	0xfff
#define V_FW_HMA_CMD_SIZE(x)	((x) << S_FW_HMA_CMD_SIZE)
#define G_FW_HMA_CMD_SIZE(x)	\
    (((x) >> S_FW_HMA_CMD_SIZE) & M_FW_HMA_CMD_SIZE)

#define S_FW_HMA_CMD_ADDR_SIZE		11
#define M_FW_HMA_CMD_ADDR_SIZE		0x1fffff
#define V_FW_HMA_CMD_ADDR_SIZE(x)	((x) << S_FW_HMA_CMD_ADDR_SIZE)
#define G_FW_HMA_CMD_ADDR_SIZE(x)	\
    (((x) >> S_FW_HMA_CMD_ADDR_SIZE) & M_FW_HMA_CMD_ADDR_SIZE)

/******************************************************************************
 *   P C I E   F W   R E G I S T E R
 **************************************/

enum pcie_fw_eval {
	PCIE_FW_EVAL_CRASH		= 0,
	PCIE_FW_EVAL_PREP		= 1,
	PCIE_FW_EVAL_CONF		= 2,
	PCIE_FW_EVAL_INIT		= 3,
	PCIE_FW_EVAL_UNEXPECTEDEVENT	= 4,
	PCIE_FW_EVAL_OVERHEAT		= 5,
	PCIE_FW_EVAL_DEVICESHUTDOWN	= 6,
};

/**
 *	Register definitions for the PCIE_FW register which the firmware uses
 *	to retain status across RESETs.  This register should be considered
 *	as a READ-ONLY register for Host Software and only to be used to
 *	track firmware initialization/error state, etc.
 */
#define S_PCIE_FW_ERR		31
#define M_PCIE_FW_ERR		0x1
#define V_PCIE_FW_ERR(x)	((x) << S_PCIE_FW_ERR)
#define G_PCIE_FW_ERR(x)	(((x) >> S_PCIE_FW_ERR) & M_PCIE_FW_ERR)
#define F_PCIE_FW_ERR		V_PCIE_FW_ERR(1U)

#define S_PCIE_FW_INIT		30
#define M_PCIE_FW_INIT		0x1
#define V_PCIE_FW_INIT(x)	((x) << S_PCIE_FW_INIT)
#define G_PCIE_FW_INIT(x)	(((x) >> S_PCIE_FW_INIT) & M_PCIE_FW_INIT)
#define F_PCIE_FW_INIT		V_PCIE_FW_INIT(1U)

#define S_PCIE_FW_HALT          29
#define M_PCIE_FW_HALT          0x1
#define V_PCIE_FW_HALT(x)       ((x) << S_PCIE_FW_HALT)
#define G_PCIE_FW_HALT(x)       (((x) >> S_PCIE_FW_HALT) & M_PCIE_FW_HALT)
#define F_PCIE_FW_HALT          V_PCIE_FW_HALT(1U)

#define S_PCIE_FW_EVAL		24
#define M_PCIE_FW_EVAL		0x7
#define V_PCIE_FW_EVAL(x)	((x) << S_PCIE_FW_EVAL)
#define G_PCIE_FW_EVAL(x)	(((x) >> S_PCIE_FW_EVAL) & M_PCIE_FW_EVAL)

#define S_PCIE_FW_STAGE		21
#define M_PCIE_FW_STAGE		0x7
#define V_PCIE_FW_STAGE(x)	((x) << S_PCIE_FW_STAGE)
#define G_PCIE_FW_STAGE(x)	(((x) >> S_PCIE_FW_STAGE) & M_PCIE_FW_STAGE)

#define S_PCIE_FW_ASYNCNOT_VLD	20
#define M_PCIE_FW_ASYNCNOT_VLD	0x1
#define V_PCIE_FW_ASYNCNOT_VLD(x) \
    ((x) << S_PCIE_FW_ASYNCNOT_VLD)
#define G_PCIE_FW_ASYNCNOT_VLD(x) \
    (((x) >> S_PCIE_FW_ASYNCNOT_VLD) & M_PCIE_FW_ASYNCNOT_VLD)
#define F_PCIE_FW_ASYNCNOT_VLD	V_PCIE_FW_ASYNCNOT_VLD(1U)

#define S_PCIE_FW_ASYNCNOTINT	19
#define M_PCIE_FW_ASYNCNOTINT	0x1
#define V_PCIE_FW_ASYNCNOTINT(x) \
    ((x) << S_PCIE_FW_ASYNCNOTINT)
#define G_PCIE_FW_ASYNCNOTINT(x) \
    (((x) >> S_PCIE_FW_ASYNCNOTINT) & M_PCIE_FW_ASYNCNOTINT)
#define F_PCIE_FW_ASYNCNOTINT	V_PCIE_FW_ASYNCNOTINT(1U)

#define S_PCIE_FW_ASYNCNOT	16
#define M_PCIE_FW_ASYNCNOT	0x7
#define V_PCIE_FW_ASYNCNOT(x)	((x) << S_PCIE_FW_ASYNCNOT)
#define G_PCIE_FW_ASYNCNOT(x)	\
    (((x) >> S_PCIE_FW_ASYNCNOT) & M_PCIE_FW_ASYNCNOT)

#define S_PCIE_FW_MASTER_VLD	15
#define M_PCIE_FW_MASTER_VLD	0x1
#define V_PCIE_FW_MASTER_VLD(x)	((x) << S_PCIE_FW_MASTER_VLD)
#define G_PCIE_FW_MASTER_VLD(x)	\
    (((x) >> S_PCIE_FW_MASTER_VLD) & M_PCIE_FW_MASTER_VLD)
#define F_PCIE_FW_MASTER_VLD	V_PCIE_FW_MASTER_VLD(1U)

#define S_PCIE_FW_MASTER	12
#define M_PCIE_FW_MASTER	0x7
#define V_PCIE_FW_MASTER(x)	((x) << S_PCIE_FW_MASTER)
#define G_PCIE_FW_MASTER(x)	(((x) >> S_PCIE_FW_MASTER) & M_PCIE_FW_MASTER)

#define S_PCIE_FW_RESET_VLD		11
#define M_PCIE_FW_RESET_VLD		0x1
#define V_PCIE_FW_RESET_VLD(x)	((x) << S_PCIE_FW_RESET_VLD)
#define G_PCIE_FW_RESET_VLD(x)	\
    (((x) >> S_PCIE_FW_RESET_VLD) & M_PCIE_FW_RESET_VLD)
#define F_PCIE_FW_RESET_VLD	V_PCIE_FW_RESET_VLD(1U)

#define S_PCIE_FW_RESET		8
#define M_PCIE_FW_RESET		0x7
#define V_PCIE_FW_RESET(x)	((x) << S_PCIE_FW_RESET)
#define G_PCIE_FW_RESET(x)	\
    (((x) >> S_PCIE_FW_RESET) & M_PCIE_FW_RESET)

#define S_PCIE_FW_REGISTERED	0
#define M_PCIE_FW_REGISTERED	0xff
#define V_PCIE_FW_REGISTERED(x)	((x) << S_PCIE_FW_REGISTERED)
#define G_PCIE_FW_REGISTERED(x)	\
    (((x) >> S_PCIE_FW_REGISTERED) & M_PCIE_FW_REGISTERED)


/******************************************************************************
 *   P C I E   F W   P F 0   R E G I S T E R
 **********************************************/

/*
 *	this register is available as 32-bit of persistent storage (across
 *	PL_RST based chip-reset) for boot drivers (i.e. firmware and driver
 *	will not write it)
 */


/******************************************************************************
 *   P C I E   F W   P F 7   R E G I S T E R
 **********************************************/

/*
 * PF7 stores the Firmware Device Log parameters which allows Host Drivers to
 * access the "devlog" which needing to contact firmware.  The encoding is
 * mostly the same as that returned by the DEVLOG command except for the size
 * which is encoded as the number of entries in multiples-1 of 128 here rather
 * than the memory size as is done in the DEVLOG command.  Thus, 0 means 128
 * and 15 means 2048.  This of course in turn constrains the allowed values
 * for the devlog size ...
 */
#define PCIE_FW_PF_DEVLOG		7

#define S_PCIE_FW_PF_DEVLOG_NENTRIES128	28
#define M_PCIE_FW_PF_DEVLOG_NENTRIES128	0xf
#define V_PCIE_FW_PF_DEVLOG_NENTRIES128(x) \
	((x) << S_PCIE_FW_PF_DEVLOG_NENTRIES128)
#define G_PCIE_FW_PF_DEVLOG_NENTRIES128(x) \
	(((x) >> S_PCIE_FW_PF_DEVLOG_NENTRIES128) & \
	 M_PCIE_FW_PF_DEVLOG_NENTRIES128)

#define S_PCIE_FW_PF_DEVLOG_ADDR16	4
#define M_PCIE_FW_PF_DEVLOG_ADDR16	0xffffff
#define V_PCIE_FW_PF_DEVLOG_ADDR16(x)	((x) << S_PCIE_FW_PF_DEVLOG_ADDR16)
#define G_PCIE_FW_PF_DEVLOG_ADDR16(x) \
	(((x) >> S_PCIE_FW_PF_DEVLOG_ADDR16) & M_PCIE_FW_PF_DEVLOG_ADDR16)

#define S_PCIE_FW_PF_DEVLOG_MEMTYPE	0
#define M_PCIE_FW_PF_DEVLOG_MEMTYPE	0xf
#define V_PCIE_FW_PF_DEVLOG_MEMTYPE(x)	((x) << S_PCIE_FW_PF_DEVLOG_MEMTYPE)
#define G_PCIE_FW_PF_DEVLOG_MEMTYPE(x) \
	(((x) >> S_PCIE_FW_PF_DEVLOG_MEMTYPE) & M_PCIE_FW_PF_DEVLOG_MEMTYPE)


/******************************************************************************
 *   B I N A R Y   H E A D E R   F O R M A T
 **********************************************/

/*
 *	firmware binary header format
 */
struct fw_hdr {
	__u8	ver;
	__u8	chip;			/* terminator chip family */
	__be16	len512;			/* bin length in units of 512-bytes */
	__be32	fw_ver;			/* firmware version */
	__be32	tp_microcode_ver;	/* tcp processor microcode version */
	__u8	intfver_nic;
	__u8	intfver_vnic;
	__u8	intfver_ofld;
	__u8	intfver_ri;
	__u8	intfver_iscsipdu;
	__u8	intfver_iscsi;
	__u8	intfver_fcoepdu;
	__u8	intfver_fcoe;
	__u32	reserved2;
	__u32	reserved3;
	__be32	magic;			/* runtime or bootstrap fw */
	__be32	flags;
	__be32	reserved6[23];
};

enum fw_hdr_chip {
	FW_HDR_CHIP_T4,
	FW_HDR_CHIP_T5,
	FW_HDR_CHIP_T6
};

#define S_FW_HDR_FW_VER_MAJOR	24
#define M_FW_HDR_FW_VER_MAJOR	0xff
#define V_FW_HDR_FW_VER_MAJOR(x) \
    ((x) << S_FW_HDR_FW_VER_MAJOR)
#define G_FW_HDR_FW_VER_MAJOR(x) \
    (((x) >> S_FW_HDR_FW_VER_MAJOR) & M_FW_HDR_FW_VER_MAJOR)

#define S_FW_HDR_FW_VER_MINOR	16
#define M_FW_HDR_FW_VER_MINOR	0xff
#define V_FW_HDR_FW_VER_MINOR(x) \
    ((x) << S_FW_HDR_FW_VER_MINOR)
#define G_FW_HDR_FW_VER_MINOR(x) \
    (((x) >> S_FW_HDR_FW_VER_MINOR) & M_FW_HDR_FW_VER_MINOR)

#define S_FW_HDR_FW_VER_MICRO	8
#define M_FW_HDR_FW_VER_MICRO	0xff
#define V_FW_HDR_FW_VER_MICRO(x) \
    ((x) << S_FW_HDR_FW_VER_MICRO)
#define G_FW_HDR_FW_VER_MICRO(x) \
    (((x) >> S_FW_HDR_FW_VER_MICRO) & M_FW_HDR_FW_VER_MICRO)

#define S_FW_HDR_FW_VER_BUILD	0
#define M_FW_HDR_FW_VER_BUILD	0xff
#define V_FW_HDR_FW_VER_BUILD(x) \
    ((x) << S_FW_HDR_FW_VER_BUILD)
#define G_FW_HDR_FW_VER_BUILD(x) \
    (((x) >> S_FW_HDR_FW_VER_BUILD) & M_FW_HDR_FW_VER_BUILD)

enum {
	T4FW_VERSION_MAJOR	= 0x01,
	T4FW_VERSION_MINOR	= 0x17,
	T4FW_VERSION_MICRO	= 0x00,
	T4FW_VERSION_BUILD	= 0x00,

	T5FW_VERSION_MAJOR	= 0x01,
	T5FW_VERSION_MINOR	= 0x17,
	T5FW_VERSION_MICRO	= 0x00,
	T5FW_VERSION_BUILD	= 0x00,

	T6FW_VERSION_MAJOR	= 0x01,
	T6FW_VERSION_MINOR	= 0x17,
	T6FW_VERSION_MICRO	= 0x00,
	T6FW_VERSION_BUILD	= 0x00,
};

enum {
	/* T4
	 */
	T4FW_HDR_INTFVER_NIC	= 0x00,
	T4FW_HDR_INTFVER_VNIC	= 0x00,
	T4FW_HDR_INTFVER_OFLD	= 0x00,
	T4FW_HDR_INTFVER_RI	= 0x00,
	T4FW_HDR_INTFVER_ISCSIPDU= 0x00,
	T4FW_HDR_INTFVER_ISCSI	= 0x00,
	T4FW_HDR_INTFVER_FCOEPDU  = 0x00,
	T4FW_HDR_INTFVER_FCOE	= 0x00,

	/* T5
	 */
	T5FW_HDR_INTFVER_NIC	= 0x00,
	T5FW_HDR_INTFVER_VNIC	= 0x00,
	T5FW_HDR_INTFVER_OFLD	= 0x00,
	T5FW_HDR_INTFVER_RI	= 0x00,
	T5FW_HDR_INTFVER_ISCSIPDU= 0x00,
	T5FW_HDR_INTFVER_ISCSI	= 0x00,
	T5FW_HDR_INTFVER_FCOEPDU= 0x00,
	T5FW_HDR_INTFVER_FCOE	= 0x00,

	/* T6
	 */
	T6FW_HDR_INTFVER_NIC	= 0x00,
	T6FW_HDR_INTFVER_VNIC	= 0x00,
	T6FW_HDR_INTFVER_OFLD	= 0x00,
	T6FW_HDR_INTFVER_RI	= 0x00,
	T6FW_HDR_INTFVER_ISCSIPDU= 0x00,
	T6FW_HDR_INTFVER_ISCSI	= 0x00,
	T6FW_HDR_INTFVER_FCOEPDU= 0x00,
	T6FW_HDR_INTFVER_FCOE	= 0x00,
};

enum {
	FW_HDR_MAGIC_RUNTIME	= 0x00000000,
	FW_HDR_MAGIC_BOOTSTRAP	= 0x626f6f74,
};

enum fw_hdr_flags {
	FW_HDR_FLAGS_RESET_HALT	= 0x00000001,
};

/*
 *	External PHY firmware binary header format
 */
struct fw_ephy_hdr {
	__u8	ver;
	__u8	reserved;
	__be16	len512;			/* bin length in units of 512-bytes */
	__be32	magic;

	__be16	vendor_id;
	__be16	device_id;
	__be32	version;

	__be32	reserved1[4];
};

enum {
	FW_EPHY_HDR_MAGIC	= 0x65706879,
};
	
struct fw_ifconf_dhcp_info {
	__be32		addr;
	__be32		mask;
	__be16		vlanid;
	__be16		mtu;
	__be32		gw;
	__u8		op;
	__u8		len;
	__u8		data[270];
};

#endif /* _T4FW_INTERFACE_H_ */
