/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 *  SCSI Transport Netlink Interface
 *    Used for the posting of outbound SCSI transport events
 *
 *  Copyright (C) 2006   James Smart, Emulex Corporation
 */
#ifndef SCSI_NETLINK_H
#define SCSI_NETLINK_H

#include <linux/netlink.h>
#include <linux/types.h>

/*
 * This file intended to be included by both kernel and user space
 */

/* Single Netlink Message type to send all SCSI Transport messages */
#define SCSI_TRANSPORT_MSG		NLMSG_MIN_TYPE + 1

/* SCSI Transport Broadcast Groups */
	/* leaving groups 0 and 1 unassigned */
#define SCSI_NL_GRP_FC_EVENTS		(1<<2)		/* Group 2 */
#define SCSI_NL_GRP_CNT			3


/* SCSI_TRANSPORT_MSG event message header */
struct scsi_nl_hdr {
	__u8 version;
	__u8 transport;
	__u16 magic;
	__u16 msgtype;
	__u16 msglen;
} __attribute__((aligned(sizeof(__u64))));

/* scsi_nl_hdr->version value */
#define SCSI_NL_VERSION				1

/* scsi_nl_hdr->magic value */
#define SCSI_NL_MAGIC				0xA1B2

/* scsi_nl_hdr->transport value */
#define SCSI_NL_TRANSPORT			0
#define SCSI_NL_TRANSPORT_FC			1
#define SCSI_NL_MAX_TRANSPORTS			2

/* Transport-based scsi_nl_hdr->msgtype values are defined in each transport */

/*
 * GENERIC SCSI scsi_nl_hdr->msgtype Values
 */
	/* kernel -> user */
#define SCSI_NL_SHOST_VENDOR			0x0001
	/* user -> kernel */
/* SCSI_NL_SHOST_VENDOR msgtype is kernel->user and user->kernel */


/*
 * Message Structures :
 */

/* macro to round up message lengths to 8byte boundary */
#define SCSI_NL_MSGALIGN(len)		(((len) + 7) & ~7)


/*
 * SCSI HOST Vendor Unique messages :
 *   SCSI_NL_SHOST_VENDOR
 *
 * Note: The Vendor Unique message payload will begin directly after
 * 	 this structure, with the length of the payload per vmsg_datalen.
 *
 * Note: When specifying vendor_id, be sure to read the Vendor Type and ID
 *   formatting requirements specified below
 */
struct scsi_nl_host_vendor_msg {
	struct scsi_nl_hdr snlh;		/* must be 1st element ! */
	__u64 vendor_id;
	__u16 host_no;
	__u16 vmsg_datalen;
} __attribute__((aligned(sizeof(__u64))));


/*
 * Vendor ID:
 *   If transports post vendor-unique events, they must pass a well-known
 *   32-bit vendor identifier. This identifier consists of 8 bits indicating
 *   the "type" of identifier contained, and 24 bits of id data.
 *
 *   Identifiers for each type:
 *    PCI :  ID data is the 16 bit PCI Registered Vendor ID
 */
#define SCSI_NL_VID_TYPE_SHIFT		56
#define SCSI_NL_VID_TYPE_MASK		((__u64)0xFF << SCSI_NL_VID_TYPE_SHIFT)
#define SCSI_NL_VID_TYPE_PCI		((__u64)0x01 << SCSI_NL_VID_TYPE_SHIFT)
#define SCSI_NL_VID_ID_MASK		(~ SCSI_NL_VID_TYPE_MASK)


#define INIT_SCSI_NL_HDR(hdr, t, mtype, mlen)			\
	{							\
	(hdr)->version = SCSI_NL_VERSION;			\
	(hdr)->transport = t;					\
	(hdr)->magic = SCSI_NL_MAGIC;				\
	(hdr)->msgtype = mtype;					\
	(hdr)->msglen = mlen;					\
	}

#endif /* SCSI_NETLINK_H */

