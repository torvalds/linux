/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * SCSI Target Emulator
 *
 * Copyright (c) 2002 Nate Lawson.
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

#ifndef _SCSI_TARGET_H
#define _SCSI_TARGET_H

/*
 * Maximum number of parallel commands to accept,
 * 1024 for Fibre Channel (SPI is 16).
 */
#define MAX_INITIATORS		8
#define	SECTOR_SIZE		512
#define MAX_EVENTS		(MAX_INITIATORS + 5)
				/* kqueue for AIO, signals */

/* Additional SCSI 3 defines for inquiry response */
#define SID_Addr16	0x0100

TAILQ_HEAD(io_queue, ccb_hdr);

/* Offset into the private CCB area for storing our descriptor */
#define targ_descr	periph_priv.entries[1].ptr

/* Descriptor attached to each ATIO */
struct atio_descr {
	off_t	  base_off;	/* Base offset for ATIO */
	uint	  total_len;	/* Total xfer len for this ATIO */
	uint	  init_req;	/* Transfer count requested to/from init */
	uint	  init_ack;	/* Data transferred ok to/from init */
	uint	  targ_req;	/* Transfer count requested to/from target */
	uint	  targ_ack;	/* Data transferred ok to/from target */
	int	  flags;	/* Flags for CTIOs */
	u_int8_t *cdb;		/* Pointer to received CDB */
		  		/* List of completed AIO/CTIOs */
	struct	  io_queue cmplt_io;
};

typedef enum {
	ATIO_WORK,
	AIO_DONE,
	CTIO_DONE
} io_ops;

/* Descriptor attached to each CTIO */
struct ctio_descr {
	void	*buf;		/* Backing store */
	off_t	 offset;	/* Position in transfer (for file, */
				/* doesn't start at 0) */
	struct	 aiocb aiocb;	/* AIO descriptor for this CTIO */
	struct	 ccb_accept_tio *atio;
				/* ATIO we are satisfying */
	io_ops	 event;		/* Event that queued this CTIO */
};

typedef enum {
        UA_NONE         = 0x00,
        UA_POWER_ON     = 0x01,
        UA_BUS_RESET    = 0x02,
        UA_BDR          = 0x04
} ua_types;

typedef enum {
        CA_NONE         = 0x00,
        CA_UNIT_ATTN    = 0x01,
        CA_CMD_SENSE    = 0x02
} ca_types;

struct initiator_state {
        ua_types   orig_ua;
        ca_types   orig_ca;
        ua_types   pending_ua;
        ca_types   pending_ca;
        struct     scsi_sense_data sense_data;
};

/* Global functions */
extern cam_status	tcmd_init(u_int16_t req_inq_flags,
				  u_int16_t sim_inq_flags);
extern int		tcmd_handle(struct ccb_accept_tio *atio,
				    struct ccb_scsiio *ctio, io_ops event);
extern void		tcmd_sense(u_int init_id, struct ccb_scsiio *ctio,
				   u_int8_t flags,
				   u_int8_t asc, u_int8_t ascq);
extern void		tcmd_ua(u_int init_id, ua_types new_ua);
extern int		work_atio(struct ccb_accept_tio *atio);
extern void		send_ccb(union ccb *ccb, int priority);
extern void		free_ccb(union ccb *ccb);
static __inline u_int	min(u_int a, u_int b) { return (a < b ? a : b); }

/* Global Data */
extern int notaio;

/*
 * Compat Defines
 */
#if __FreeBSD_version >= 500000
#define	OFF_FMT	"%ju"
#else
#define	OFF_FMT "%llu"
#endif

#endif /* _SCSI_TARGET_H */
