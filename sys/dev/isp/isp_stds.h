/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 *  Copyright (c) 1997-2009 by Matthew Jacob
 *  All rights reserved.
 * 
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 * 
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 
 *  THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 * 
 */
/*
 * Structures that derive directly from public standards.
 */
#ifndef	_ISP_STDS_H
#define	_ISP_STDS_H
/*
 * FC Frame Header
 *
 * Source: dpANS-X3.xxx-199x, section 18 (AKA FC-PH-2)
 *
 */
typedef struct {
	uint8_t		r_ctl;
	uint8_t		d_id[3];
	uint8_t		cs_ctl;
	uint8_t		s_id[3];
	uint8_t		type;
	uint8_t		f_ctl[3];
	uint8_t		seq_id;
	uint8_t		df_ctl;
	uint16_t	seq_cnt;
	uint16_t	ox_id;
	uint16_t	rx_id;
	uint32_t	parameter;
} fc_hdr_t;

/*
 * FCP_CMND_IU Payload
 *
 * Source: NICTS T10, Project 1144D, Revision 07a, Section 9 (AKA fcp2-r07a)
 *
 * Notes:
 *	When additional cdb length is defined in fcp_cmnd_alen_datadir,
 * 	bits 2..7, the actual cdb length is 16 + ((fcp_cmnd_alen_datadir>>2)*4),
 *	with the datalength following in MSB format just after.
 */
typedef struct {
	uint8_t		fcp_cmnd_lun[8];
	uint8_t		fcp_cmnd_crn;
	uint8_t		fcp_cmnd_task_attribute;
	uint8_t		fcp_cmnd_task_management;
	uint8_t		fcp_cmnd_alen_datadir;
	union {
		struct {
			uint8_t		fcp_cmnd_cdb[16];
			uint32_t	fcp_cmnd_dl;
		} sf;
		struct {
			uint8_t		fcp_cmnd_cdb[1];
		} lf;
	} cdb_dl;
} fcp_cmnd_iu_t;


#define	FCP_CMND_TASK_ATTR_SIMPLE	0x00
#define	FCP_CMND_TASK_ATTR_HEAD		0x01
#define	FCP_CMND_TASK_ATTR_ORDERED	0x02
#define	FCP_CMND_TASK_ATTR_ACA		0x04
#define	FCP_CMND_TASK_ATTR_UNTAGGED	0x05
#define	FCP_CMND_TASK_ATTR_MASK		0x07

#define	FCP_CMND_ADDTL_CDBLEN_SHIFT	2

#define	FCP_CMND_DATA_WRITE		0x01
#define	FCP_CMND_DATA_READ		0x02

#define	FCP_CMND_DATA_DIR_MASK		0x03

#define	FCP_CMND_TMF_CLEAR_ACA		0x40
#define	FCP_CMND_TMF_TGT_RESET		0x20
#define	FCP_CMND_TMF_LUN_RESET		0x10
#define	FCP_CMND_TMF_QUERY_ASYNC_EVENT	0x08
#define	FCP_CMND_TMF_CLEAR_TASK_SET	0x04
#define	FCP_CMND_TMF_ABORT_TASK_SET	0x02
#define	FCP_CMND_TMF_QUERY_TASK_SET	0x01

/*
 * Basic CT IU Header
 *
 * Source: X3.288-199x Generic Services 2 Rev 5.3 (FC-GS-2) Section 4.3.1
 */

typedef struct {
	uint8_t		ct_revision;
	uint8_t		ct_in_id[3];
	uint8_t		ct_fcs_type;
	uint8_t		ct_fcs_subtype;
	uint8_t		ct_options;
	uint8_t		ct_reserved0;
	uint16_t	ct_cmd_resp;
	uint16_t	ct_bcnt_resid;
	uint8_t		ct_reserved1;
	uint8_t		ct_reason;
	uint8_t		ct_explanation;
	uint8_t		ct_vunique;
} ct_hdr_t;
#define	CT_REVISION		1
#define	CT_FC_TYPE_FC		0xFC
#define CT_FC_SUBTYPE_NS	0x02

/*
 * RFT_ID Requet CT_IU
 *
 * Source: NCITS xxx-200x Generic Services- 5 Rev 8.5 Section 5.2.5.30
 */
typedef struct {
	ct_hdr_t	rftid_hdr;
	uint8_t		rftid_reserved;
	uint8_t		rftid_portid[3];
	uint32_t	rftid_fc4types[8];
} rft_id_t;

/*
 * RSPN_ID Requet CT_IU
 *
 * Source: INCITS 463-2010 Generic Services 6 Section 5.2.5.32
 */
typedef struct {
	ct_hdr_t	rspnid_hdr;
	uint8_t		rspnid_reserved;
	uint8_t		rspnid_portid[3];
	uint8_t		rspnid_length;
	uint8_t		rspnid_name[0];
} rspn_id_t;

/*
 * RFF_ID Requet CT_IU
 *
 * Source: INCITS 463-2010 Generic Services 6 Section 5.2.5.34
 */
typedef struct {
	ct_hdr_t	rffid_hdr;
	uint8_t		rffid_reserved;
	uint8_t		rffid_portid[3];
	uint16_t	rffid_reserved2;
	uint8_t		rffid_fc4features;
	uint8_t		rffid_fc4type;
} rff_id_t;

/*
 * RSNN_NN Requet CT_IU
 *
 * Source: INCITS 463-2010 Generic Services 6 Section 5.2.5.35
 */
typedef struct {
	ct_hdr_t	rsnnnn_hdr;
	uint8_t		rsnnnn_nodename[8];
	uint8_t		rsnnnn_length;
	uint8_t		rsnnnn_name[0];
} rsnn_nn_t;

/*
 * FCP Response IU and bits of interest
 * Source: NCITS T10, Project 1828D, Revision 02b (aka FCP4r02b)
 */
typedef struct {
	uint8_t		fcp_rsp_reserved[8];
	uint16_t	fcp_rsp_status_qualifier;	/* SAM-5 Status Qualifier */
	uint8_t		fcp_rsp_bits;
	uint8_t		fcp_rsp_scsi_status;		/* SAM-5 SCSI Status Byte */
	uint32_t	fcp_rsp_resid;
	uint32_t	fcp_rsp_snslen;
	uint32_t	fcp_rsp_rsplen;
	/*
	 * In the bytes that follow, it's going to be
	 * FCP RESPONSE INFO (max 8 bytes, possibly 0)
	 * FCP SENSE INFO (if any)
	 * FCP BIDIRECTIONAL READ RESID (if any)
	 */
	uint8_t		fcp_rsp_extra[0];
} fcp_rsp_iu_t;
#define	MIN_FCP_RESPONSE_SIZE		24

#define	FCP_BIDIR_RSP			0x80	/* Bi-Directional response */
#define	FCP_BIDIR_RESID_UNDERFLOW	0x40
#define	FCP_BIDIR_RESID_OVERFLOW	0x20
#define	FCP_CONF_REQ			0x10
#define	FCP_RESID_UNDERFLOW		0x08
#define	FCP_RESID_OVERFLOW		0x04
#define	FCP_SNSLEN_VALID		0x02
#define	FCP_RSPLEN_VALID		0x01

#define FCP_MAX_RSPLEN			0x08
/*
 * FCP Response Code Definitions
 * Source: NCITS T10, Project 1144D, Revision 08 (aka FCP2r08)
 */
#define	FCP_RSPNS_CODE_OFFSET		3

#define	FCP_RSPNS_TMF_DONE		0
#define	FCP_RSPNS_DLBRSTX		1
#define	FCP_RSPNS_BADCMND		2
#define	FCP_RSPNS_EROFS			3
#define	FCP_RSPNS_TMF_REJECT		4
#define	FCP_RSPNS_TMF_FAILED		5
#define	FCP_RSPNS_TMF_SUCCEEDED		8
#define	FCP_RSPNS_TMF_INCORRECT_LUN	9

/*
 * R_CTL field definitions
 *
 * Bits 31-28 are ROUTING
 * Bits 27-24 are INFORMATION
 *
 * These are nibble values, not bits
 */
#define	R_CTL_ROUTE_DATA	0x00
#define	R_CTL_ROUTE_ELS		0x02
#define	R_CTL_ROUTE_FC4_LINK	0x03
#define	R_CTL_ROUTE_VDATA	0x04
#define	R_CTL_ROUTE_EXENDED	0x05
#define	R_CTL_ROUTE_BASIC	0x08
#define	R_CTL_ROUTE_LINK	0x0c
#define	R_CTL_ROUTE_EXT_ROUTING	0x0f

#define	R_CTL_INFO_UNCATEGORIZED	0x00
#define	R_CTL_INFO_SOLICITED_DATA	0x01
#define	R_CTL_INFO_UNSOLICITED_CONTROL	0x02
#define	R_CTL_INFO_SOLICITED_CONTROL	0x03
#define	R_CTL_INFO_UNSOLICITED_DATA	0x04
#define	R_CTL_INFO_DATA_DESCRIPTOR	0x05
#define	R_CTL_INFO_UNSOLICITED_COMMAND	0x06
#define	R_CTL_INFO_COMMAND_STATUS	0x07

#define	MAKE_RCTL(a, b)	(((a) << 4) | (b))

/* unconverted miscellany */
/*
 * Basic FC Link Service defines
 */
/* #define	ABTS	MAKE_RCTL(R_CTL_ROUTE_BASIC, R_CTL_INFO_SOLICITED_DATA) */
#define	BA_ACC	MAKE_RCTL(R_CTL_ROUTE_BASIC, R_CTL_INFO_UNSOLICITED_DATA)	/* of ABORT */
#define	BA_RJT	MAKE_RCTL(R_CTL_ROUTE_BASIC, R_CTL_INFO_DATA_DESCRIPTOR)	/* of ABORT */

/*
 * Link Service Accept/Reject
 */
#define	LS_ACC			0x8002
#define	LS_RJT			0x8001

/*
 * FC ELS Codes- bits 31-24 of the first payload word of an ELS frame.
 */
#define	PLOGI			0x03
#define	FLOGI			0x04
#define	LOGO			0x05
#define	ABTX			0x06
#define	PRLI			0x20
#define	PRLO			0x21
#define	SCN			0x22
#define	TPRLO			0x24
#define	PDISC			0x50
#define	ADISC			0x52
#define	RNC			0x53

/*
 * PRLI Word 0 definitions
 * FPC4-r02b January, 2011
 */
#define	PRLI_WD0_TYPE_MASK				0xff000000
#define	PRLI_WD0_TC_EXT_MASK				0x00ff0000
#define	PRLI_WD0_EST_IMAGE_PAIR				(1 << 13)

/*
 * PRLI Word 3 definitions
 * FPC4-r02b January, 2011
 */
#define	PRLI_WD3_ENHANCED_DISCOVERY			(1 << 11)
#define	PRLI_WD3_REC_SUPPORT				(1 << 10)
#define	PRLI_WD3_TASK_RETRY_IDENTIFICATION_REQUESTED	(1 << 9)
#define	PRLI_WD3_RETRY					(1 << 8)
#define	PRLI_WD3_CONFIRMED_COMPLETION_ALLOWED		(1 << 7)
#define	PRLI_WD3_DATA_OVERLAY_ALLOWED			(1 << 6)
#define	PRLI_WD3_INITIATOR_FUNCTION			(1 << 5)
#define	PRLI_WD3_TARGET_FUNCTION			(1 << 4)
#define	PRLI_WD3_READ_FCP_XFER_RDY_DISABLED		(1 << 1)	/* definitely supposed to be set */
#define	PRLI_WD3_WRITE_FCP_XFER_RDY_DISABLED		(1 << 0)



/*
 * FC4 defines
 */
#define	FC4_IP		5	/* ISO/EEC 8802-2 LLC/SNAP */
#define	FC4_SCSI	8	/* SCSI-3 via Fibre Channel Protocol (FCP) */
#define	FC4_FC_SVC	0x20	/* Fibre Channel Services */

#ifndef	MSG_ABORT
#define	MSG_ABORT		0x06
#endif
#ifndef	MSG_BUS_DEV_RESET
#define	MSG_BUS_DEV_RESET	0x0c
#endif
#ifndef	MSG_ABORT_TAG
#define	MSG_ABORT_TAG		0x0d
#endif
#ifndef	MSG_CLEAR_QUEUE
#define	MSG_CLEAR_QUEUE		0x0e
#endif
#ifndef	MSG_REL_RECOVERY
#define	MSG_REL_RECOVERY	0x10
#endif
#ifndef	MSG_TERM_IO_PROC
#define	MSG_TERM_IO_PROC	0x11
#endif
#ifndef	MSG_LUN_RESET
#define	MSG_LUN_RESET		0x17
#endif

#endif	/* _ISP_STDS_H */
