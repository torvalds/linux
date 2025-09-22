/*	$OpenBSD: aic7xxx_cam.h,v 1.10 2022/01/09 05:42:38 jsg Exp $	*/
/*	$NetBSD: aic7xxx_cam.h,v 1.3 2003/04/20 11:17:20 fvdl Exp $	*/

/*
 * Data structures and definitions for the CAM system.
 *
 * Copyright (c) 1997 Justin T. Gibbs.
 * Copyright (c) 2000 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
/*
 * Ported from FreeBSD by Pascal Renauld, Network Storage Solutions, Inc. - April 2003
 */

#ifndef _AIC7XXX_CAM_H
#define _AIC7XXX_CAM_H

#define	CAM_BUS_WILDCARD ((u_int)~0)
#define	CAM_TARGET_WILDCARD ((u_int)~0)
#define	CAM_LUN_WILDCARD -1

/*
 * Translate FreeBSD names for SCSI status byte values to OpenBSD names.
 */
#define SCSI_STATUS_OK                  SCSI_OK
#define SCSI_STATUS_CHECK_COND          SCSI_CHECK
#define SCSI_STATUS_COND_MET            SCSI_COND_MET
#define SCSI_STATUS_BUSY                SCSI_BUSY
#define SCSI_STATUS_INTERMED            SCSI_INTERM
#define SCSI_STATUS_INTERMED_COND_MET   SCSI_INTERMED_COND_MET
#define SCSI_STATUS_RESERV_CONFLICT     SCSI_RESERV_CONFLICT
#define SCSI_STATUS_CMD_TERMINATED      SCSI_TERMINATED
#define SCSI_STATUS_QUEUE_FULL          SCSI_QUEUE_FULL

#define XS_CTL_DATA_IN		SCSI_DATA_IN
#define XS_CTL_POLL		SCSI_POLL
#define XS_CTL_RESET		SCSI_RESET

#define MSG_EXT_PPR_QAS_REQ	MSG_EXT_PPR_PROT_QAS
#define MSG_EXT_PPR_IU_REQ	MSG_EXT_PPR_PROT_IUS
#define MSG_EXT_PPR_DT_REQ	MSG_EXT_PPR_PROT_DT
#define MSG_ORDERED_TASK	MSG_ORDERED_Q_TAG
#define MSG_SIMPLE_TASK		MSG_SIMPLE_Q_TAG
#define MSG_ABORT_TASK		MSG_ABORT_TAG

#define	callout_reset(timer, timeout, func, arg) do {	\
	if (!timeout_initialized((timer)))		\
		timeout_set((timer), (func), (arg));	\
	timeout_add((timer), (timeout));		\
} while (0)

#define aic_delay	DELAY

#define aic_htobe16(x) htobe16(x)
#define aic_htobe32(x) htobe32(x)
#define aic_htobe64(x) htobe64(x)
#define aic_htole16(x) htole16(x)
#define aic_htole32(x) htole32(x)
#define aic_htole64(x) htole64(x)

#define aic_be16toh(x) be16toh(x)
#define aic_be32toh(x) be32toh(x)
#define aic_be64toh(x) be64toh(x)
#define aic_le16toh(x) letoh16(x)
#define aic_le32toh(x) letoh32(x)
#define aic_le64toh(x) letoh64(x)

#define xs_control	flags
#define xs_callout	stimeout
#define xs_status	status

/* CAM Status field values */
typedef enum {
	CAM_REQ_INPROG,		/* CCB request is in progress */
	CAM_REQ_CMP,		/* CCB request completed without error */
	CAM_REQ_ABORTED,	/* CCB request aborted by the host */
	CAM_UA_ABORT,		/* Unable to abort CCB request */
	CAM_REQ_CMP_ERR,	/* CCB request completed with an error */
	CAM_BUSY,		/* CAM subsystem is busy */
	CAM_REQ_INVALID,	/* CCB request was invalid */
	CAM_PATH_INVALID,	/* Supplied Path ID is invalid */
	CAM_SEL_TIMEOUT,	/* Target Selection Timeout */
	CAM_CMD_TIMEOUT,	/* Command timeout */
	CAM_SCSI_STATUS_ERROR,	/* SCSI error, look at error code in CCB */
	CAM_SCSI_BUS_RESET,	/* SCSI Bus Reset Sent/Received */
	CAM_UNCOR_PARITY,	/* Uncorrectable parity error occurred */
	CAM_AUTOSENSE_FAIL,	/* Autosense: request sense cmd fail */
	CAM_NO_HBA,		/* No HBA Detected Error */
	CAM_DATA_RUN_ERR,	/* Data Overrun error */
	CAM_UNEXP_BUSFREE,	/* Unexpected Bus Free */
	CAM_SEQUENCE_FAIL,	/* Protocol Violation */
	CAM_CCB_LEN_ERR,	/* CCB length supplied is inadequate */
	CAM_PROVIDE_FAIL,	/* Unable to provide requested capability */
	CAM_BDR_SENT,		/* A SCSI BDR msg was sent to target */
	CAM_REQ_TERMIO,		/* CCB request terminated by the host */
	CAM_UNREC_HBA_ERROR,	/* Unrecoverable Host Bus Adapter Error */
	CAM_REQ_TOO_BIG,	/* The request was too large for this host */
	CAM_UA_TERMIO,		/* Unable to terminate I/O CCB request */
	CAM_MSG_REJECT_REC,	/* Message Reject Received */
	CAM_DEV_NOT_THERE,	/* SCSI Device Not Installed/there */
	CAM_RESRC_UNAVAIL,	/* Resource Unavailable */
	/*
	 * This request should be requeued to preserve
	 * transaction ordering.  This typically occurs
	 * when the SIM recognizes an error that should
	 * freeze the queue and must place additional
	 * requests for the target at the sim level
	 * back into the XPT queue.
	 */
	CAM_REQUEUE_REQ,
	CAM_DEV_QFRZN		= 0x40,

	CAM_STATUS_MASK		= 0x3F
} cam_status;

typedef enum {
	CAM_DIR_IN		= SCSI_DATA_IN,
	CAM_DIR_OUT		= SCSI_DATA_OUT
} ccb_flags;

typedef enum {
	AC_BUS_RESET            =       0x001,
	AC_UNSOL_RESEL          =       0x002,
	AC_SCSI_AEN             =       0x008,
	AC_SENT_BDR             =       0x010,
	AC_PATH_REGISTERED      =       0x020,
	AC_PATH_DEREGISTERED    =       0x040,
	AC_FOUND_DEVICE         =       0x080,
	AC_LOST_DEVICE          =       0x100,
	AC_TRANSFER_NEG         =       0x200,
	AC_INQ_CHANGED          =       0x400,
	AC_GETDEV_CHANGED       =       0x800
} ac_code;

#endif /* _AIC7XXX_CAM_H */
