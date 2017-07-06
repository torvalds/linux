/*
 * Copyright (c) 2016 Avago Technologies.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful.
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND WARRANTIES,
 * INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY, FITNESS FOR A
 * PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE DISCLAIMED, EXCEPT TO
 * THE EXTENT THAT SUCH DISCLAIMERS ARE HELD TO BE LEGALLY INVALID.
 * See the GNU General Public License for more details, a copy of which
 * can be found in the file COPYING included with this package
 *
 */

/*
 * This file contains definitions relative to FC-NVME r1.14 (16-020vB).
 */

#ifndef _NVME_FC_H
#define _NVME_FC_H 1


#define NVME_CMD_SCSI_ID		0xFD
#define NVME_CMD_FC_ID			FC_TYPE_NVME

/* FC-NVME Cmd IU Flags */
#define FCNVME_CMD_FLAGS_DIRMASK	0x03
#define FCNVME_CMD_FLAGS_WRITE		0x01
#define FCNVME_CMD_FLAGS_READ		0x02

struct nvme_fc_cmd_iu {
	__u8			scsi_id;
	__u8			fc_id;
	__be16			iu_len;
	__u8			rsvd4[3];
	__u8			flags;
	__be64			connection_id;
	__be32			csn;
	__be32			data_len;
	struct nvme_command	sqe;
	__be32			rsvd88[2];
};

#define NVME_FC_SIZEOF_ZEROS_RSP	12

enum {
	FCNVME_SC_SUCCESS		= 0,
	FCNVME_SC_INVALID_FIELD		= 1,
	FCNVME_SC_INVALID_CONNID	= 2,
};

struct nvme_fc_ersp_iu {
	__u8			status_code;
	__u8			rsvd1;
	__be16			iu_len;
	__be32			rsn;
	__be32			xfrd_len;
	__be32			rsvd12;
	struct nvme_completion	cqe;
	/* for now - no additional payload */
};


/* FC-NVME Link Services */
enum {
	FCNVME_LS_RSVD			= 0,
	FCNVME_LS_RJT			= 1,
	FCNVME_LS_ACC			= 2,
	FCNVME_LS_CREATE_ASSOCIATION	= 3,
	FCNVME_LS_CREATE_CONNECTION	= 4,
	FCNVME_LS_DISCONNECT		= 5,
};

/* FC-NVME Link Service Descriptors */
enum {
	FCNVME_LSDESC_RSVD		= 0x0,
	FCNVME_LSDESC_RQST		= 0x1,
	FCNVME_LSDESC_RJT		= 0x2,
	FCNVME_LSDESC_CREATE_ASSOC_CMD	= 0x3,
	FCNVME_LSDESC_CREATE_CONN_CMD	= 0x4,
	FCNVME_LSDESC_DISCONN_CMD	= 0x5,
	FCNVME_LSDESC_CONN_ID		= 0x6,
	FCNVME_LSDESC_ASSOC_ID		= 0x7,
};


/* ********** start of Link Service Descriptors ********** */


/*
 * fills in length of a descriptor. Struture minus descriptor header
 */
static inline __be32 fcnvme_lsdesc_len(size_t sz)
{
	return cpu_to_be32(sz - (2 * sizeof(u32)));
}

struct fcnvme_ls_rqst_w0 {
	u8	ls_cmd;			/* FCNVME_LS_xxx */
	u8	zeros[3];
};

/* FCNVME_LSDESC_RQST */
struct fcnvme_lsdesc_rqst {
	__be32	desc_tag;		/* FCNVME_LSDESC_xxx */
	__be32	desc_len;
	struct fcnvme_ls_rqst_w0	w0;
	__be32	rsvd12;
};

/* FC-NVME LS RJT reason_code values */
enum fcnvme_ls_rjt_reason {
	FCNVME_RJT_RC_NONE		= 0,
	/* no reason - not to be sent */

	FCNVME_RJT_RC_INVAL		= 0x01,
	/* invalid NVMe_LS command code */

	FCNVME_RJT_RC_LOGIC		= 0x03,
	/* logical error */

	FCNVME_RJT_RC_UNAB		= 0x09,
	/* unable to perform command request */

	FCNVME_RJT_RC_UNSUP		= 0x0b,
	/* command not supported */

	FCNVME_RJT_RC_INPROG		= 0x0e,
	/* command already in progress */

	FCNVME_RJT_RC_INV_ASSOC		= 0x40,
	/* Invalid Association ID*/

	FCNVME_RJT_RC_INV_CONN		= 0x41,
	/* Invalid Connection ID*/

	FCNVME_RJT_RC_VENDOR		= 0xff,
	/* vendor specific error */
};

/* FC-NVME LS RJT reason_explanation values */
enum fcnvme_ls_rjt_explan {
	FCNVME_RJT_EXP_NONE		= 0x00,
	/* No additional explanation */

	FCNVME_RJT_EXP_OXID_RXID	= 0x17,
	/* invalid OX_ID-RX_ID combination */

	FCNVME_RJT_EXP_INSUF_RES	= 0x29,
	/* insufficient resources */

	FCNVME_RJT_EXP_UNAB_DATA	= 0x2a,
	/* unable to supply requested data */

	FCNVME_RJT_EXP_INV_LEN		= 0x2d,
	/* Invalid payload length */
};

/* FCNVME_LSDESC_RJT */
struct fcnvme_lsdesc_rjt {
	__be32	desc_tag;		/* FCNVME_LSDESC_xxx */
	__be32	desc_len;
	u8	rsvd8;

	/*
	 * Reject reason and explanaction codes are generic
	 * to ELs's from LS-3.
	 */
	u8	reason_code;		/* fcnvme_ls_rjt_reason */
	u8	reason_explanation;	/* fcnvme_ls_rjt_explan */

	u8	vendor;
	__be32	rsvd12;
};


#define FCNVME_ASSOC_HOSTNQN_LEN	256
#define FCNVME_ASSOC_SUBNQN_LEN		256

/* FCNVME_LSDESC_CREATE_ASSOC_CMD */
struct fcnvme_lsdesc_cr_assoc_cmd {
	__be32	desc_tag;		/* FCNVME_LSDESC_xxx */
	__be32	desc_len;
	__be16	ersp_ratio;
	__be16	rsvd10;
	__be32	rsvd12[9];
	__be16	cntlid;
	__be16	sqsize;
	__be32	rsvd52;
	uuid_t	hostid;
	u8	hostnqn[FCNVME_ASSOC_HOSTNQN_LEN];
	u8	subnqn[FCNVME_ASSOC_SUBNQN_LEN];
	u8	rsvd632[384];
};

/* FCNVME_LSDESC_CREATE_CONN_CMD */
struct fcnvme_lsdesc_cr_conn_cmd {
	__be32	desc_tag;		/* FCNVME_LSDESC_xxx */
	__be32	desc_len;
	__be16	ersp_ratio;
	__be16	rsvd10;
	__be32	rsvd12[9];
	__be16	qid;
	__be16	sqsize;
	__be32  rsvd52;
};

/* Disconnect Scope Values */
enum {
	FCNVME_DISCONN_ASSOCIATION	= 0,
	FCNVME_DISCONN_CONNECTION	= 1,
};

/* FCNVME_LSDESC_DISCONN_CMD */
struct fcnvme_lsdesc_disconn_cmd {
	__be32	desc_tag;		/* FCNVME_LSDESC_xxx */
	__be32	desc_len;
	u8	rsvd8[3];
	/* note: scope is really a 1 bit field */
	u8	scope;			/* FCNVME_DISCONN_xxx */
	__be32	rsvd12;
	__be64	id;
};

/* FCNVME_LSDESC_CONN_ID */
struct fcnvme_lsdesc_conn_id {
	__be32	desc_tag;		/* FCNVME_LSDESC_xxx */
	__be32	desc_len;
	__be64	connection_id;
};

/* FCNVME_LSDESC_ASSOC_ID */
struct fcnvme_lsdesc_assoc_id {
	__be32	desc_tag;		/* FCNVME_LSDESC_xxx */
	__be32	desc_len;
	__be64	association_id;
};

/* r_ctl values */
enum {
	FCNVME_RS_RCTL_DATA		= 1,
	FCNVME_RS_RCTL_XFER_RDY		= 5,
	FCNVME_RS_RCTL_RSP		= 8,
};


/* ********** start of Link Services ********** */


/* FCNVME_LS_RJT */
struct fcnvme_ls_rjt {
	struct fcnvme_ls_rqst_w0		w0;
	__be32					desc_list_len;
	struct fcnvme_lsdesc_rqst		rqst;
	struct fcnvme_lsdesc_rjt		rjt;
};

/* FCNVME_LS_ACC */
struct fcnvme_ls_acc_hdr {
	struct fcnvme_ls_rqst_w0		w0;
	__be32					desc_list_len;
	struct fcnvme_lsdesc_rqst		rqst;
	/* Followed by cmd-specific ACC descriptors, see next definitions */
};

/* FCNVME_LS_CREATE_ASSOCIATION */
struct fcnvme_ls_cr_assoc_rqst {
	struct fcnvme_ls_rqst_w0		w0;
	__be32					desc_list_len;
	struct fcnvme_lsdesc_cr_assoc_cmd	assoc_cmd;
};

struct fcnvme_ls_cr_assoc_acc {
	struct fcnvme_ls_acc_hdr		hdr;
	struct fcnvme_lsdesc_assoc_id		associd;
	struct fcnvme_lsdesc_conn_id		connectid;
};


/* FCNVME_LS_CREATE_CONNECTION */
struct fcnvme_ls_cr_conn_rqst {
	struct fcnvme_ls_rqst_w0		w0;
	__be32					desc_list_len;
	struct fcnvme_lsdesc_assoc_id		associd;
	struct fcnvme_lsdesc_cr_conn_cmd	connect_cmd;
};

struct fcnvme_ls_cr_conn_acc {
	struct fcnvme_ls_acc_hdr		hdr;
	struct fcnvme_lsdesc_conn_id		connectid;
};

/* FCNVME_LS_DISCONNECT */
struct fcnvme_ls_disconnect_rqst {
	struct fcnvme_ls_rqst_w0		w0;
	__be32					desc_list_len;
	struct fcnvme_lsdesc_assoc_id		associd;
	struct fcnvme_lsdesc_disconn_cmd	discon_cmd;
};

struct fcnvme_ls_disconnect_acc {
	struct fcnvme_ls_acc_hdr		hdr;
};


/*
 * Yet to be defined in FC-NVME:
 */
#define NVME_FC_CONNECT_TIMEOUT_SEC	2		/* 2 seconds */
#define NVME_FC_LS_TIMEOUT_SEC		2		/* 2 seconds */
#define NVME_FC_TGTOP_TIMEOUT_SEC	2		/* 2 seconds */


#endif /* _NVME_FC_H */
