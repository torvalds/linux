/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 *  FC Transport Netlink Interface
 *
 *  Copyright (C) 2006   James Smart, Emulex Corporation
 */
#ifndef SCSI_NETLINK_FC_H
#define SCSI_NETLINK_FC_H

#include <scsi/scsi_netlink.h>

/*
 * This file intended to be included by both kernel and user space
 */

/*
 * FC Transport Message Types
 */
	/* kernel -> user */
#define FC_NL_ASYNC_EVENT			0x0100
	/* user -> kernel */
/* none */


/*
 * Message Structures :
 */

/* macro to round up message lengths to 8byte boundary */
#define FC_NL_MSGALIGN(len)		(((len) + 7) & ~7)


/*
 * FC Transport Broadcast Event Message :
 *   FC_NL_ASYNC_EVENT
 *
 * Note: if Vendor Unique message, &event_data will be  start of
 * 	 vendor unique payload, and the length of the payload is
 *       per event_datalen
 *
 * Note: When specifying vendor_id, be sure to read the Vendor Type and ID
 *   formatting requirements specified in scsi_netlink.h
 */
struct fc_nl_event {
	struct scsi_nl_hdr snlh;		/* must be 1st element ! */
	uint64_t seconds;
	uint64_t vendor_id;
	uint16_t host_no;
	uint16_t event_datalen;
	uint32_t event_num;
	uint32_t event_code;
	uint32_t event_data;
} __attribute__((aligned(sizeof(uint64_t))));


#endif /* SCSI_NETLINK_FC_H */

