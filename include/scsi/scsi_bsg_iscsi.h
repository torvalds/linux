/*
 *  iSCSI Transport BSG Interface
 *
 *  Copyright (C) 2009   James Smart, Emulex Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef SCSI_BSG_ISCSI_H
#define SCSI_BSG_ISCSI_H

/*
 * This file intended to be included by both kernel and user space
 */

#include <scsi/scsi.h>

/*
 * iSCSI Transport SGIO v4 BSG Message Support
 */

/* Default BSG request timeout (in seconds) */
#define ISCSI_DEFAULT_BSG_TIMEOUT      (10 * HZ)


/*
 * Request Message Codes supported by the iSCSI Transport
 */

/* define the class masks for the message codes */
#define ISCSI_BSG_CLS_MASK     0xF0000000      /* find object class */
#define ISCSI_BSG_HST_MASK     0x80000000      /* iscsi host class */

/* iscsi host Message Codes */
#define ISCSI_BSG_HST_VENDOR           (ISCSI_BSG_HST_MASK | 0x000000FF)


/*
 * iSCSI Host Messages
 */

/* ISCSI_BSG_HST_VENDOR : */

/* Request:
 * Note: When specifying vendor_id, be sure to read the Vendor Type and ID
 *   formatting requirements specified in scsi_netlink.h
 */
struct iscsi_bsg_host_vendor {
	/*
	 * Identifies the vendor that the message is formatted for. This
	 * should be the recipient of the message.
	 */
	uint64_t vendor_id;

	/* start of vendor command area */
	uint32_t vendor_cmd[0];
};

/* Response:
 */
struct iscsi_bsg_host_vendor_reply {
	/* start of vendor response area */
	uint32_t vendor_rsp[0];
};


/* request (CDB) structure of the sg_io_v4 */
struct iscsi_bsg_request {
	uint32_t msgcode;
	union {
		struct iscsi_bsg_host_vendor    h_vendor;
	} rqst_data;
} __attribute__((packed));


/* response (request sense data) structure of the sg_io_v4 */
struct iscsi_bsg_reply {
	/*
	 * The completion result. Result exists in two forms:
	 * if negative, it is an -Exxx system errno value. There will
	 * be no further reply information supplied.
	 * else, it's the 4-byte scsi error result, with driver, host,
	 * msg and status fields. The per-msgcode reply structure
	 * will contain valid data.
	 */
	uint32_t result;

	/* If there was reply_payload, how much was recevied ? */
	uint32_t reply_payload_rcv_len;

	union {
		struct iscsi_bsg_host_vendor_reply      vendor_reply;
	} reply_data;
};


#endif /* SCSI_BSG_ISCSI_H */
