/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _SCSI_SCSI_STATUS_H
#define _SCSI_SCSI_STATUS_H

#include <linux/types.h>
#include <scsi/scsi_proto.h>

/* Message codes. */
enum scsi_msg_byte {
	COMMAND_COMPLETE	= 0x00,
	EXTENDED_MESSAGE	= 0x01,
	SAVE_POINTERS		= 0x02,
	RESTORE_POINTERS	= 0x03,
	DISCONNECT		= 0x04,
	INITIATOR_ERROR		= 0x05,
	ABORT_TASK_SET		= 0x06,
	MESSAGE_REJECT		= 0x07,
	NOP			= 0x08,
	MSG_PARITY_ERROR	= 0x09,
	LINKED_CMD_COMPLETE	= 0x0a,
	LINKED_FLG_CMD_COMPLETE	= 0x0b,
	TARGET_RESET		= 0x0c,
	ABORT_TASK		= 0x0d,
	CLEAR_TASK_SET		= 0x0e,
	INITIATE_RECOVERY	= 0x0f,            /* SCSI-II only */
	RELEASE_RECOVERY	= 0x10,            /* SCSI-II only */
	TERMINATE_IO_PROC	= 0x11,            /* SCSI-II only */
	CLEAR_ACA		= 0x16,
	LOGICAL_UNIT_RESET	= 0x17,
	SIMPLE_QUEUE_TAG	= 0x20,
	HEAD_OF_QUEUE_TAG	= 0x21,
	ORDERED_QUEUE_TAG	= 0x22,
	IGNORE_WIDE_RESIDUE	= 0x23,
	ACA			= 0x24,
	QAS_REQUEST		= 0x55,

	/* Old SCSI2 names, don't use in new code */
	BUS_DEVICE_RESET	= TARGET_RESET,
	ABORT			= ABORT_TASK_SET,
};

/* Host byte codes. */
enum scsi_host_status {
	DID_OK		= 0x00,	/* NO error                                */
	DID_NO_CONNECT	= 0x01,	/* Couldn't connect before timeout period  */
	DID_BUS_BUSY	= 0x02,	/* BUS stayed busy through time out period */
	DID_TIME_OUT	= 0x03,	/* TIMED OUT for other reason              */
	DID_BAD_TARGET	= 0x04,	/* BAD target.                             */
	DID_ABORT	= 0x05,	/* Told to abort for some other reason     */
	DID_PARITY	= 0x06,	/* Parity error                            */
	DID_ERROR	= 0x07,	/* Internal error                          */
	DID_RESET	= 0x08,	/* Reset by somebody.                      */
	DID_BAD_INTR	= 0x09,	/* Got an interrupt we weren't expecting.  */
	DID_PASSTHROUGH	= 0x0a,	/* Force command past mid-layer            */
	DID_SOFT_ERROR	= 0x0b,	/* The low level driver just wish a retry  */
	DID_IMM_RETRY	= 0x0c,	/* Retry without decrementing retry count  */
	DID_REQUEUE	= 0x0d,	/* Requeue command (no immediate retry) also
				 * without decrementing the retry count	   */
	DID_TRANSPORT_DISRUPTED = 0x0e, /* Transport error disrupted execution
					 * and the driver blocked the port to
					 * recover the link. Transport class will
					 * retry or fail IO */
	DID_TRANSPORT_FAILFAST = 0x0f, /* Transport class fastfailed the io */
	/*
	 * We used to have DID_TARGET_FAILURE, DID_NEXUS_FAILURE,
	 * DID_ALLOC_FAILURE and DID_MEDIUM_ERROR at 0x10 - 0x13. For compat
	 * with userspace apps that parse the host byte for SG IO, we leave
	 * that block of codes unused and start at 0x14 below.
	 */
	DID_TRANSPORT_MARGINAL = 0x14, /* Transport marginal errors */
};

#endif /* _SCSI_SCSI_STATUS_H */
