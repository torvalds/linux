/*-
 * Data structures and definitions for the CAM system.
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Justin T. Gibbs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 * $FreeBSD$
 */

#ifndef _CAM_CAM_H
#define _CAM_CAM_H 1

#ifdef _KERNEL
#include "opt_cam.h"
#endif

#include <sys/cdefs.h>

typedef u_int path_id_t;
typedef u_int target_id_t;
typedef u_int64_t lun_id_t;

#define	CAM_XPT_PATH_ID	((path_id_t)~0)
#define	CAM_BUS_WILDCARD ((path_id_t)~0)
#define	CAM_TARGET_WILDCARD ((target_id_t)~0)
#define	CAM_LUN_WILDCARD (~(u_int)0)

#define CAM_EXTLUN_BYTE_SWIZZLE(lun) (	\
	((((u_int64_t)lun) & 0xffff000000000000L) >> 48) | \
	((((u_int64_t)lun) & 0x0000ffff00000000L) >> 16) | \
	((((u_int64_t)lun) & 0x00000000ffff0000L) << 16) | \
	((((u_int64_t)lun) & 0x000000000000ffffL) << 48))

/*
 * Maximum length for a CAM CDB.  
 */
#define CAM_MAX_CDBLEN 16

/*
 * Definition of a CAM peripheral driver entry.  Peripheral drivers instantiate
 * one of these for each device they wish to communicate with and pass it into
 * the xpt layer when they wish to schedule work on that device via the
 * xpt_schedule API.
 */
struct cam_periph;

/*
 * Priority information for a CAM structure. 
 */
typedef enum {
    CAM_RL_HOST,
    CAM_RL_BUS,
    CAM_RL_XPT,
    CAM_RL_DEV,
    CAM_RL_NORMAL,
    CAM_RL_VALUES
} cam_rl;
/*
 * The generation number is incremented every time a new entry is entered into
 * the queue giving round robin per priority level scheduling.
 */
typedef struct {
	u_int32_t priority;
#define CAM_PRIORITY_HOST	((CAM_RL_HOST << 8) + 0x80)
#define CAM_PRIORITY_BUS	((CAM_RL_BUS << 8) + 0x80)
#define CAM_PRIORITY_XPT	((CAM_RL_XPT << 8) + 0x80)
#define CAM_PRIORITY_DEV	((CAM_RL_DEV << 8) + 0x80)
#define CAM_PRIORITY_OOB	(CAM_RL_DEV << 8)
#define CAM_PRIORITY_NORMAL	((CAM_RL_NORMAL << 8) + 0x80)
#define CAM_PRIORITY_NONE	(u_int32_t)-1
	u_int32_t generation;
	int       index;
#define CAM_UNQUEUED_INDEX	-1
#define CAM_ACTIVE_INDEX	-2	
#define CAM_DONEQ_INDEX		-3	
#define CAM_EXTRAQ_INDEX	INT_MAX
} cam_pinfo;

/*
 * Macro to compare two generation numbers.  It is used like this:  
 *
 *	if (GENERATIONCMP(a, >=, b))
 *		...;
 *
 * GERERATIONCMP uses modular arithmetic to guard against wraps
 * wraps in the generation number.
 */
#define GENERATIONCMP(x, op, y) ((int32_t)((x) - (y)) op 0)

/* CAM flags XXX Move to cam_periph.h ??? */
typedef enum {
	CAM_FLAG_NONE		= 0x00,
	CAM_EXPECT_INQ_CHANGE	= 0x01,
	CAM_RETRY_SELTO		= 0x02 /* Retry Selection Timeouts */
} cam_flags;

enum {
	SF_RETRY_UA		= 0x01,	/* Retry UNIT ATTENTION conditions. */
	SF_NO_PRINT		= 0x02,	/* Never print error status. */
	SF_QUIET_IR		= 0x04,	/* Be quiet about Illegal Request responses */
	SF_PRINT_ALWAYS		= 0x08,	/* Always print error status. */
	SF_NO_RECOVERY		= 0x10,	/* Don't do active error recovery. */
	SF_NO_RETRY		= 0x20,	/* Don't do any retries. */
	SF_RETRY_BUSY		= 0x40	/* Retry BUSY status. */
};

/* CAM  Status field values */
typedef enum {
	/* CCB request is in progress */
	CAM_REQ_INPROG		= 0x00,

	/* CCB request completed without error */
	CAM_REQ_CMP		= 0x01,

	/* CCB request aborted by the host */
	CAM_REQ_ABORTED		= 0x02,

	/* Unable to abort CCB request */
	CAM_UA_ABORT		= 0x03,

	/* CCB request completed with an error */
	CAM_REQ_CMP_ERR		= 0x04,

	/* CAM subsystem is busy */
	CAM_BUSY		= 0x05,

	/* CCB request was invalid */
	CAM_REQ_INVALID		= 0x06,

	/* Supplied Path ID is invalid */
	CAM_PATH_INVALID	= 0x07,

	/* SCSI Device Not Installed/there */
	CAM_DEV_NOT_THERE	= 0x08,

	/* Unable to terminate I/O CCB request */
	CAM_UA_TERMIO		= 0x09,

	/* Target Selection Timeout */
	CAM_SEL_TIMEOUT		= 0x0a,

	/* Command timeout */
	CAM_CMD_TIMEOUT		= 0x0b,

	/* SCSI error, look at error code in CCB */
	CAM_SCSI_STATUS_ERROR	= 0x0c,

	/* Message Reject Received */
	CAM_MSG_REJECT_REC	= 0x0d,

	/* SCSI Bus Reset Sent/Received */
	CAM_SCSI_BUS_RESET	= 0x0e,

	/* Uncorrectable parity error occurred */
	CAM_UNCOR_PARITY	= 0x0f,

	/* Autosense: request sense cmd fail */
	CAM_AUTOSENSE_FAIL	= 0x10,

	/* No HBA Detected error */
	CAM_NO_HBA		= 0x11,

	/* Data Overrun error */
	CAM_DATA_RUN_ERR	= 0x12,

	/* Unexpected Bus Free */
	CAM_UNEXP_BUSFREE	= 0x13,

	/* Target Bus Phase Sequence Failure */
	CAM_SEQUENCE_FAIL	= 0x14,

	/* CCB length supplied is inadequate */
	CAM_CCB_LEN_ERR		= 0x15,

	/* Unable to provide requested capability*/
	CAM_PROVIDE_FAIL	= 0x16,

	/* A SCSI BDR msg was sent to target */
	CAM_BDR_SENT		= 0x17,

	/* CCB request terminated by the host */
	CAM_REQ_TERMIO		= 0x18,

	/* Unrecoverable Host Bus Adapter Error */
	CAM_UNREC_HBA_ERROR	= 0x19,

	/* Request was too large for this host */
	CAM_REQ_TOO_BIG		= 0x1a,

	/*
	 * This request should be requeued to preserve
	 * transaction ordering.  This typically occurs
	 * when the SIM recognizes an error that should
	 * freeze the queue and must place additional
	 * requests for the target at the sim level
	 * back into the XPT queue.
	 */
	CAM_REQUEUE_REQ		= 0x1b,

	/* ATA error, look at error code in CCB */
	CAM_ATA_STATUS_ERROR	= 0x1c,

	/* Initiator/Target Nexus lost. */
	CAM_SCSI_IT_NEXUS_LOST	= 0x1d,

	/* SMP error, look at error code in CCB */
	CAM_SMP_STATUS_ERROR	= 0x1e,

	/*
	 * Command completed without error but  exceeded the soft
	 * timeout threshold.
	 */
	CAM_REQ_SOFTTIMEOUT	= 0x1f,

	/*
	 * 0x20 - 0x32 are unassigned
	 */

	/* Initiator Detected Error */
	CAM_IDE			= 0x33,

	/* Resource Unavailable */
	CAM_RESRC_UNAVAIL	= 0x34,

	/* Unacknowledged Event by Host */
	CAM_UNACKED_EVENT	= 0x35,

	/* Message Received in Host Target Mode */
	CAM_MESSAGE_RECV	= 0x36,

	/* Invalid CDB received in Host Target Mode */
	CAM_INVALID_CDB		= 0x37,

	/* Lun supplied is invalid */
	CAM_LUN_INVALID		= 0x38,

	/* Target ID supplied is invalid */
	CAM_TID_INVALID		= 0x39,

	/* The requested function is not available */
	CAM_FUNC_NOTAVAIL	= 0x3a,

	/* Nexus is not established */
	CAM_NO_NEXUS		= 0x3b,

	/* The initiator ID is invalid */
	CAM_IID_INVALID		= 0x3c,

	/* The SCSI CDB has been received */
	CAM_CDB_RECVD		= 0x3d,

	/* The LUN is already enabled for target mode */
	CAM_LUN_ALRDY_ENA	= 0x3e,

	/* SCSI Bus Busy */
	CAM_SCSI_BUSY		= 0x3f,


	/*
	 * Flags
	 */

	/* The DEV queue is frozen w/this err */
	CAM_DEV_QFRZN		= 0x40,

	/* Autosense data valid for target */
	CAM_AUTOSNS_VALID	= 0x80,

	/* SIM ready to take more commands */
	CAM_RELEASE_SIMQ	= 0x100,

	/* SIM has this command in its queue */
	CAM_SIM_QUEUED		= 0x200,

	/* Quality of service data is valid */
	CAM_QOS_VALID		= 0x400,

	/* Mask bits for just the status # */
	CAM_STATUS_MASK = 0x3F,

	/*
	 * Target Specific Adjunct Status
	 */
	
	/* sent sense with status */
	CAM_SENT_SENSE		= 0x40000000
} cam_status;

typedef enum {
	CAM_ESF_NONE		= 0x00,
	CAM_ESF_COMMAND		= 0x01,
	CAM_ESF_CAM_STATUS	= 0x02,
	CAM_ESF_PROTO_STATUS	= 0x04,
	CAM_ESF_ALL		= 0xff
} cam_error_string_flags;

typedef enum {
	CAM_EPF_NONE		= 0x00,
	CAM_EPF_MINIMAL		= 0x01,
	CAM_EPF_NORMAL		= 0x02,
	CAM_EPF_ALL		= 0x03,
	CAM_EPF_LEVEL_MASK	= 0x0f
	/* All bits above bit 3 are protocol-specific */
} cam_error_proto_flags;

typedef enum {
	CAM_ESF_PRINT_NONE	= 0x00,
	CAM_ESF_PRINT_STATUS	= 0x10,
	CAM_ESF_PRINT_SENSE	= 0x20
} cam_error_scsi_flags;

typedef enum {
	CAM_ESMF_PRINT_NONE	= 0x00,
	CAM_ESMF_PRINT_STATUS	= 0x10,
	CAM_ESMF_PRINT_FULL_CMD	= 0x20,
} cam_error_smp_flags;

typedef enum {
	CAM_EAF_PRINT_NONE	= 0x00,
	CAM_EAF_PRINT_STATUS	= 0x10,
	CAM_EAF_PRINT_RESULT	= 0x20
} cam_error_ata_flags;

typedef enum {
	CAM_STRVIS_FLAG_NONE		= 0x00,
	CAM_STRVIS_FLAG_NONASCII_MASK	= 0x03,
	CAM_STRVIS_FLAG_NONASCII_TRIM	= 0x00,
	CAM_STRVIS_FLAG_NONASCII_RAW	= 0x01,
	CAM_STRVIS_FLAG_NONASCII_SPC	= 0x02,
	CAM_STRVIS_FLAG_NONASCII_ESC	= 0x03
} cam_strvis_flags;

struct cam_status_entry
{
	cam_status  status_code;
	const char *status_text;
};

extern const struct cam_status_entry cam_status_table[];
extern const int num_cam_status_entries;
#ifdef _KERNEL
extern int cam_sort_io_queues;
#endif
union ccb;
struct sbuf;

#ifdef SYSCTL_DECL	/* from sysctl.h */
SYSCTL_DECL(_kern_cam);
#endif

__BEGIN_DECLS
typedef int (cam_quirkmatch_t)(caddr_t, caddr_t);

caddr_t	cam_quirkmatch(caddr_t target, caddr_t quirk_table, int num_entries,
		       int entry_size, cam_quirkmatch_t *comp_func);

void	cam_strvis(u_int8_t *dst, const u_int8_t *src, int srclen, int dstlen);
void	cam_strvis_sbuf(struct sbuf *sb, const u_int8_t *src, int srclen,
			uint32_t flags);

int	cam_strmatch(const u_int8_t *str, const u_int8_t *pattern, int str_len);
const struct cam_status_entry*
	cam_fetch_status_entry(cam_status status);
#ifdef _KERNEL
char *	cam_error_string(union ccb *ccb, char *str, int str_len,
			 cam_error_string_flags flags,
			 cam_error_proto_flags proto_flags);
void	cam_error_print(union ccb *ccb, cam_error_string_flags flags,
			cam_error_proto_flags proto_flags);
#else /* _KERNEL */
struct cam_device;

char *	cam_error_string(struct cam_device *device, union ccb *ccb, char *str,
			 int str_len, cam_error_string_flags flags,
			 cam_error_proto_flags proto_flags);
void	cam_error_print(struct cam_device *device, union ccb *ccb,
			cam_error_string_flags flags,
			cam_error_proto_flags proto_flags, FILE *ofile);
#endif /* _KERNEL */
__END_DECLS

#ifdef _KERNEL
static __inline void cam_init_pinfo(cam_pinfo *pinfo);

static __inline void cam_init_pinfo(cam_pinfo *pinfo)
{
	pinfo->priority = CAM_PRIORITY_NONE;	
	pinfo->index = CAM_UNQUEUED_INDEX;
}
#endif

#endif /* _CAM_CAM_H */
