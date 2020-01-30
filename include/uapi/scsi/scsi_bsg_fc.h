/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 *  FC Transport BSG Interface
 *
 *  Copyright (C) 2008   James Smart, Emulex Corporation
 */

#ifndef SCSI_BSG_FC_H
#define SCSI_BSG_FC_H

#include <linux/types.h>

/*
 * This file intended to be included by both kernel and user space
 */

/*
 * FC Transport SGIO v4 BSG Message Support
 */

/* Default BSG request timeout (in seconds) */
#define FC_DEFAULT_BSG_TIMEOUT		(10 * HZ)


/*
 * Request Message Codes supported by the FC Transport
 */

/* define the class masks for the message codes */
#define FC_BSG_CLS_MASK		0xF0000000	/* find object class */
#define FC_BSG_HST_MASK		0x80000000	/* fc host class */
#define FC_BSG_RPT_MASK		0x40000000	/* fc rport class */

	/* fc_host Message Codes */
#define FC_BSG_HST_ADD_RPORT		(FC_BSG_HST_MASK | 0x00000001)
#define FC_BSG_HST_DEL_RPORT		(FC_BSG_HST_MASK | 0x00000002)
#define FC_BSG_HST_ELS_NOLOGIN		(FC_BSG_HST_MASK | 0x00000003)
#define FC_BSG_HST_CT			(FC_BSG_HST_MASK | 0x00000004)
#define FC_BSG_HST_VENDOR		(FC_BSG_HST_MASK | 0x000000FF)

	/* fc_rport Message Codes */
#define FC_BSG_RPT_ELS			(FC_BSG_RPT_MASK | 0x00000001)
#define FC_BSG_RPT_CT			(FC_BSG_RPT_MASK | 0x00000002)



/*
 * FC Address Identifiers in Message Structures :
 *
 *   Whenever a command payload contains a FC Address Identifier
 *   (aka port_id), the value is effectively in big-endian
 *   order, thus the array elements are decoded as follows:
 *     element [0] is bits 23:16 of the FC Address Identifier
 *     element [1] is bits 15:8 of the FC Address Identifier
 *     element [2] is bits 7:0 of the FC Address Identifier
 */


/*
 * FC Host Messages
 */

/* FC_BSG_HST_ADDR_PORT : */

/* Request:
 * This message requests the FC host to login to the remote port
 * at the specified N_Port_Id.  The remote port is to be enumerated
 * with the transport upon completion of the login.
 */
struct fc_bsg_host_add_rport {
	__u8	reserved;

	/* FC Address Identier of the remote port to login to */
	__u8	port_id[3];
};

/* Response:
 * There is no additional response data - fc_bsg_reply->result is sufficient
 */


/* FC_BSG_HST_DEL_RPORT : */

/* Request:
 * This message requests the FC host to remove an enumerated
 * remote port and to terminate the login to it.
 *
 * Note: The driver is free to reject this request if it desires to
 * remain logged in with the remote port.
 */
struct fc_bsg_host_del_rport {
	__u8	reserved;

	/* FC Address Identier of the remote port to logout of */
	__u8	port_id[3];
};

/* Response:
 * There is no additional response data - fc_bsg_reply->result is sufficient
 */


/* FC_BSG_HST_ELS_NOLOGIN : */

/* Request:
 * This message requests the FC_Host to send an ELS to a specific
 * N_Port_ID. The host does not need to log into the remote port,
 * nor does it need to enumerate the rport for further traffic
 * (although, the FC host is free to do so if it desires).
 */
struct fc_bsg_host_els {
	/*
	 * ELS Command Code being sent (must be the same as byte 0
	 * of the payload)
	 */
	__u8	command_code;

	/* FC Address Identier of the remote port to send the ELS to */
	__u8	port_id[3];
};

/* Response:
 */
/* fc_bsg_ctels_reply->status values */
#define FC_CTELS_STATUS_OK	0x00000000
#define FC_CTELS_STATUS_REJECT	0x00000001
#define FC_CTELS_STATUS_P_RJT	0x00000002
#define FC_CTELS_STATUS_F_RJT	0x00000003
#define FC_CTELS_STATUS_P_BSY	0x00000004
#define FC_CTELS_STATUS_F_BSY	0x00000006
struct fc_bsg_ctels_reply {
	/*
	 * Note: An ELS LS_RJT may be reported in 2 ways:
	 *  a) A status of FC_CTELS_STATUS_OK is returned. The caller
	 *     is to look into the ELS receive payload to determine
	 *     LS_ACC or LS_RJT (by contents of word 0). The reject
	 *     data will be in word 1.
	 *  b) A status of FC_CTELS_STATUS_REJECT is returned, The
	 *     rjt_data field will contain valid data.
	 *
	 * Note: ELS LS_ACC is determined by an FC_CTELS_STATUS_OK, and
	 *   the receive payload word 0 indicates LS_ACC
	 *   (e.g. value is 0x02xxxxxx).
	 *
	 * Note: Similarly, a CT Reject may be reported in 2 ways:
	 *  a) A status of FC_CTELS_STATUS_OK is returned. The caller
	 *     is to look into the CT receive payload to determine
	 *     Accept or Reject (by contents of word 2). The reject
	 *     data will be in word 3.
	 *  b) A status of FC_CTELS_STATUS_REJECT is returned, The
	 *     rjt_data field will contain valid data.
	 *
	 * Note: x_RJT/BSY status will indicae that the rjt_data field
	 *   is valid and contains the reason/explanation values.
	 */
	__u32	status;		/* See FC_CTELS_STATUS_xxx */

	/* valid if status is not FC_CTELS_STATUS_OK */
	struct	{
		__u8	action;		/* fragment_id for CT REJECT */
		__u8	reason_code;
		__u8	reason_explanation;
		__u8	vendor_unique;
	} rjt_data;
};


/* FC_BSG_HST_CT : */

/* Request:
 * This message requests that a CT Request be performed with the
 * indicated N_Port_ID. The driver is responsible for logging in with
 * the fabric and/or N_Port_ID, etc as per FC rules. This request does
 * not mandate that the driver must enumerate the destination in the
 * transport. The driver is allowed to decide whether to enumerate it,
 * and whether to tear it down after the request.
 */
struct fc_bsg_host_ct {
	__u8	reserved;

	/* FC Address Identier of the remote port to send the ELS to */
	__u8	port_id[3];

	/*
	 * We need words 0-2 of the generic preamble for the LLD's
	 */
	__u32	preamble_word0;	/* revision & IN_ID */
	__u32	preamble_word1;	/* GS_Type, GS_SubType, Options, Rsvd */
	__u32	preamble_word2;	/* Cmd Code, Max Size */

};
/* Response:
 *
 * The reply structure is an fc_bsg_ctels_reply structure
 */


/* FC_BSG_HST_VENDOR : */

/* Request:
 * Note: When specifying vendor_id, be sure to read the Vendor Type and ID
 *   formatting requirements specified in scsi_netlink.h
 */
struct fc_bsg_host_vendor {
	/*
	 * Identifies the vendor that the message is formatted for. This
	 * should be the recipient of the message.
	 */
	__u64 vendor_id;

	/* start of vendor command area */
	__u32 vendor_cmd[0];
};

/* Response:
 */
struct fc_bsg_host_vendor_reply {
	/* start of vendor response area */
	__u32 vendor_rsp[0];
};



/*
 * FC Remote Port Messages
 */

/* FC_BSG_RPT_ELS : */

/* Request:
 * This message requests that an ELS be performed with the rport.
 */
struct fc_bsg_rport_els {
	/*
	 * ELS Command Code being sent (must be the same as
	 * byte 0 of the payload)
	 */
	__u8 els_code;
};

/* Response:
 *
 * The reply structure is an fc_bsg_ctels_reply structure
 */


/* FC_BSG_RPT_CT : */

/* Request:
 * This message requests that a CT Request be performed with the rport.
 */
struct fc_bsg_rport_ct {
	/*
	 * We need words 0-2 of the generic preamble for the LLD's
	 */
	__u32	preamble_word0;	/* revision & IN_ID */
	__u32	preamble_word1;	/* GS_Type, GS_SubType, Options, Rsvd */
	__u32	preamble_word2;	/* Cmd Code, Max Size */
};
/* Response:
 *
 * The reply structure is an fc_bsg_ctels_reply structure
 */




/* request (CDB) structure of the sg_io_v4 */
struct fc_bsg_request {
	__u32 msgcode;
	union {
		struct fc_bsg_host_add_rport	h_addrport;
		struct fc_bsg_host_del_rport	h_delrport;
		struct fc_bsg_host_els		h_els;
		struct fc_bsg_host_ct		h_ct;
		struct fc_bsg_host_vendor	h_vendor;

		struct fc_bsg_rport_els		r_els;
		struct fc_bsg_rport_ct		r_ct;
	} rqst_data;
} __attribute__((packed));


/* response (request sense data) structure of the sg_io_v4 */
struct fc_bsg_reply {
	/*
	 * The completion result. Result exists in two forms:
	 *  if negative, it is an -Exxx system errno value. There will
	 *    be no further reply information supplied.
	 *  else, it's the 4-byte scsi error result, with driver, host,
	 *    msg and status fields. The per-msgcode reply structure
	 *    will contain valid data.
	 */
	__u32 result;

	/* If there was reply_payload, how much was recevied ? */
	__u32 reply_payload_rcv_len;

	union {
		struct fc_bsg_host_vendor_reply		vendor_reply;

		struct fc_bsg_ctels_reply		ctels_reply;
	} reply_data;
};


#endif /* SCSI_BSG_FC_H */

