/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-07 Applied Micro Circuits Corporation.
 * Copyright (c) 2004-05 Vinod Kashyap
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD$
 */

/*
 * AMCC'S 3ware driver for 9000 series storage controllers.
 *
 * Author: Vinod Kashyap
 * Modifications by: Adam Radford
 */



#ifndef TW_CL_H

#define TW_CL_H


/*
 * Common Layer internal macros, structures and functions.
 */


#define TW_CLI_SECTOR_SIZE		0x200
#define TW_CLI_REQUEST_TIMEOUT_PERIOD	60 /* seconds */
#define TW_CLI_RESET_TIMEOUT_PERIOD	60 /* seconds */
#define TW_CLI_MAX_RESET_ATTEMPTS	2

/* Possible values of ctlr->ioctl_lock.lock. */
#define TW_CLI_LOCK_FREE		0x0	/* lock is free */
#define TW_CLI_LOCK_HELD		0x1	/* lock is held */

/* Possible values of req->state. */
#define TW_CLI_REQ_STATE_INIT		0x0	/* being initialized */
#define TW_CLI_REQ_STATE_BUSY		0x1	/* submitted to controller */
#define TW_CLI_REQ_STATE_PENDING	0x2	/* in pending queue */
#define TW_CLI_REQ_STATE_COMPLETE	0x3	/* completed by controller */

/* Possible values of req->flags. */
#define TW_CLI_REQ_FLAGS_7K		(1<<0)	/* 7000 cmd pkt */
#define TW_CLI_REQ_FLAGS_9K		(1<<1)	/* 9000 cmd pkt */
#define TW_CLI_REQ_FLAGS_INTERNAL	(1<<2)	/* internal request */
#define TW_CLI_REQ_FLAGS_PASSTHRU	(1<<3)	/* passthru request */
#define TW_CLI_REQ_FLAGS_EXTERNAL	(1<<4)	/* external request */

#ifdef TW_OSL_PCI_CONFIG_ACCESSIBLE
/* Register offsets in PCI config space. */
#define TW_CLI_PCI_CONFIG_COMMAND_OFFSET	0x4 /* cmd register offset */
#define TW_CLI_PCI_CONFIG_STATUS_OFFSET		0x6 /* status register offset */
#endif /* TW_OSL_PCI_CONFIG_ACCESSIBLE */


#ifdef TW_OSL_DEBUG
struct tw_cli_q_stats {
	TW_UINT32	cur_len;/* current # of entries in q */
	TW_UINT32	max_len;	 /* max # of entries in q, ever reached */
};
#endif /* TW_OSL_DEBUG */


/* Queues of CL internal request context packets. */
#define TW_CLI_FREE_Q		0	/* free q */
#define TW_CLI_BUSY_Q		1	/* q of reqs submitted to fw */
#define TW_CLI_PENDING_Q	2	/* q of reqs deferred due to 'q full' */
#define TW_CLI_COMPLETE_Q	3	/* q of reqs completed by fw */
#define TW_CLI_RESET_Q		4	/* q of reqs reset by timeout */
#define TW_CLI_Q_COUNT		5	/* total number of queues */


/* CL's internal request context. */
struct tw_cli_req_context {
	struct tw_cl_req_handle	*req_handle;/* handle to track requests between
						OSL & CL */
	struct tw_cli_ctlr_context  *ctlr; /* ptr to CL's controller context */
	struct tw_cl_command_packet *cmd_pkt;/* ptr to ctlr cmd pkt */
	TW_UINT64	cmd_pkt_phys;	/* cmd pkt physical address */
	TW_VOID		*data;		/* ptr to data being passed to fw */
	TW_UINT32	length;		/* length of data being passed to fw */
	TW_UINT64	data_phys;	/* physical address of data */

	TW_UINT32	state;		/* request state */
	TW_UINT32	flags;		/* request flags */

	TW_UINT32	error_code;	/* error encountered before submission
					of request to fw, if any */

	TW_VOID		*orig_req;	/* ptr to original request for use
					during callback */
	TW_VOID		(*tw_cli_callback)(struct tw_cli_req_context *req);
					/* CL internal callback */
	TW_UINT32	request_id;	/* request id for tracking with fw */
	struct tw_cl_link link;		/* to link this request in a list */
};


/* CL's internal controller context. */
struct tw_cli_ctlr_context {
	struct tw_cl_ctlr_handle *ctlr_handle;	/* handle to track ctlr between
							OSL & CL. */
	struct tw_cli_req_context *req_ctxt_buf;/* pointer to the array of CL's
						internal request context pkts */
	struct tw_cl_command_packet *cmd_pkt_buf;/* ptr to array of cmd pkts */

	TW_UINT64		cmd_pkt_phys;	/* phys addr of cmd_pkt_buf */

	TW_UINT32		device_id;	/* controller device id */
	TW_UINT32		arch_id;	/* controller architecture id */
	TW_UINT8 		active;			  /* Initialization done, and controller is active. */
	TW_UINT8 		interrupts_enabled;	  /* Interrupts on controller enabled. */
	TW_UINT8 		internal_req_busy;	  /* Data buffer for internal requests in use. */
	TW_UINT8 		get_more_aens;		  /* More AEN's need to be retrieved. */
	TW_UINT8 		reset_needed;		  /* Controller needs a soft reset. */
	TW_UINT8 		reset_in_progress;	  /* Controller is being reset. */
	TW_UINT8 		reset_phase1_in_progress; /* In 'phase 1' of reset. */
	TW_UINT32		flags;		/* controller settings */
	TW_UINT32		sg_size_factor;	/* SG element size should be a
							multiple of this */

	/* Request queues and arrays. */
	struct tw_cl_link	req_q_head[TW_CLI_Q_COUNT];

	TW_UINT8		*internal_req_data;/* internal req data buf */
	TW_UINT64		internal_req_data_phys;/* phys addr of internal
							req data buf */
	TW_UINT32		max_simult_reqs; /* max simultaneous requests
							supported */
	TW_UINT32		max_aens_supported;/* max AEN's supported */
	/* AEN handler fields. */
	struct tw_cl_event_packet *aen_queue;	/* circular queue of AENs from
							firmware/CL/OSL */
	TW_UINT32		aen_head;	/* AEN queue head */
	TW_UINT32		aen_tail;	/* AEN queue tail */
	TW_UINT32		aen_cur_seq_id;	/* index of the last event+1 */
	TW_UINT32		aen_q_overflow;	/* indicates if unretrieved
						events were overwritten */
	TW_UINT32		aen_q_wrapped;	/* indicates if AEN queue ever
							wrapped */

	TW_UINT16		working_srl;	/* driver & firmware negotiated
							srl */
	TW_UINT16		working_branch;	/* branch # of the firmware
					that the driver is compatible with */
	TW_UINT16		working_build;	/* build # of the firmware
					that the driver is compatible with */
	TW_UINT16		fw_on_ctlr_srl;	/* srl of running firmware */
	TW_UINT16		fw_on_ctlr_branch;/* branch # of running
							firmware */
	TW_UINT16		fw_on_ctlr_build;/* build # of running
							firmware */
	TW_UINT32		operating_mode; /* base mode/current mode */

	TW_INT32		host_intr_pending;/* host intr processing
							needed */
	TW_INT32		attn_intr_pending;/* attn intr processing
							needed */
	TW_INT32		cmd_intr_pending;/* cmd intr processing
							needed */
	TW_INT32		resp_intr_pending;/* resp intr processing
							needed */

	TW_LOCK_HANDLE		gen_lock_handle;/* general purpose lock */
	TW_LOCK_HANDLE		*gen_lock;/* ptr to general purpose lock */
	TW_LOCK_HANDLE		io_lock_handle;	/* lock held during cmd
						submission */
	TW_LOCK_HANDLE		*io_lock;/* ptr to lock held during cmd
						submission */

#ifdef TW_OSL_CAN_SLEEP
	TW_SLEEP_HANDLE		sleep_handle;	/* handle to co-ordinate sleeps
						& wakeups */
#endif /* TW_OSL_CAN_SLEEP */

	struct {
		TW_UINT32	lock;		/* lock state */
		TW_TIME		timeout;	/* time at which the lock will
						become available, even if not
						explicitly released */
	} ioctl_lock;		/* lock for use by user applications, for
				synchronization between ioctl calls */
#ifdef TW_OSL_DEBUG
	struct tw_cli_q_stats	q_stats[TW_CLI_Q_COUNT];/* queue statistics */
#endif /* TW_OSL_DEBUG */
};



/*
 * Queue primitives
 */

#ifdef TW_OSL_DEBUG

#define TW_CLI_Q_INIT(ctlr, q_type)	do {				\
	(ctlr)->q_stats[q_type].cur_len = 0;				\
	(ctlr)->q_stats[q_type].max_len = 0;				\
} while (0)


#define TW_CLI_Q_INSERT(ctlr, q_type)	do {				\
	struct tw_cli_q_stats *q_stats = &((ctlr)->q_stats[q_type]);	\
									\
	if (++(q_stats->cur_len) > q_stats->max_len)			\
		q_stats->max_len = q_stats->cur_len;			\
} while (0)


#define TW_CLI_Q_REMOVE(ctlr, q_type)					\
	(ctlr)->q_stats[q_type].cur_len--

#else /* TW_OSL_DEBUG */

#define TW_CLI_Q_INIT(ctlr, q_index)
#define TW_CLI_Q_INSERT(ctlr, q_index)
#define TW_CLI_Q_REMOVE(ctlr, q_index)

#endif /* TW_OSL_DEBUG */


/* Initialize a queue of requests. */
static __inline TW_VOID
tw_cli_req_q_init(struct tw_cli_ctlr_context *ctlr, TW_UINT8 q_type)
{
	TW_CL_Q_INIT(&(ctlr->req_q_head[q_type]));
	TW_CLI_Q_INIT(ctlr, q_type);
}



/* Insert the given request at the head of the given queue (q_type). */
static __inline TW_VOID
tw_cli_req_q_insert_head(struct tw_cli_req_context *req, TW_UINT8 q_type)
{
	struct tw_cli_ctlr_context	*ctlr = req->ctlr;

	tw_osl_get_lock(ctlr->ctlr_handle, ctlr->gen_lock);
	TW_CL_Q_INSERT_HEAD(&(ctlr->req_q_head[q_type]), &(req->link));
	TW_CLI_Q_INSERT(ctlr, q_type);
	tw_osl_free_lock(ctlr->ctlr_handle, ctlr->gen_lock);
}



/* Insert the given request at the tail of the given queue (q_type). */
static __inline TW_VOID
tw_cli_req_q_insert_tail(struct tw_cli_req_context *req, TW_UINT8 q_type)
{
	struct tw_cli_ctlr_context	*ctlr = req->ctlr;

	tw_osl_get_lock(ctlr->ctlr_handle, ctlr->gen_lock);
	TW_CL_Q_INSERT_TAIL(&(ctlr->req_q_head[q_type]), &(req->link));
	TW_CLI_Q_INSERT(ctlr, q_type);
	tw_osl_free_lock(ctlr->ctlr_handle, ctlr->gen_lock);
}



/* Remove and return the request at the head of the given queue (q_type). */
static __inline struct tw_cli_req_context *
tw_cli_req_q_remove_head(struct tw_cli_ctlr_context *ctlr, TW_UINT8 q_type)
{
	struct tw_cli_req_context	*req = TW_CL_NULL;
	struct tw_cl_link		*link;

	tw_osl_get_lock(ctlr->ctlr_handle, ctlr->gen_lock);
	if ((link = TW_CL_Q_FIRST_ITEM(&(ctlr->req_q_head[q_type]))) !=
		TW_CL_NULL) {
		req = TW_CL_STRUCT_HEAD(link,
			struct tw_cli_req_context, link);
		TW_CL_Q_REMOVE_ITEM(&(ctlr->req_q_head[q_type]), &(req->link));
		TW_CLI_Q_REMOVE(ctlr, q_type);
	}
	tw_osl_free_lock(ctlr->ctlr_handle, ctlr->gen_lock);
	return(req);
}



/* Remove the given request from the given queue (q_type). */
static __inline TW_VOID
tw_cli_req_q_remove_item(struct tw_cli_req_context *req, TW_UINT8 q_type)
{
	struct tw_cli_ctlr_context	*ctlr = req->ctlr;

	tw_osl_get_lock(ctlr->ctlr_handle, ctlr->gen_lock);
	TW_CL_Q_REMOVE_ITEM(&(ctlr->req_q_head[q_type]), &(req->link));
	TW_CLI_Q_REMOVE(ctlr, q_type);
	tw_osl_free_lock(ctlr->ctlr_handle, ctlr->gen_lock);
}



/* Create an event packet for an event/error posted by the controller. */
#define tw_cli_create_ctlr_event(ctlr, event_src, cmd_hdr)	do {	\
	TW_UINT8 severity =						\
		GET_SEVERITY((cmd_hdr)->status_block.res__severity);	\
									\
	tw_cl_create_event(ctlr->ctlr_handle, TW_CL_TRUE, event_src,	\
		(cmd_hdr)->status_block.error,				\
		severity,						\
		tw_cli_severity_string_table[severity],			\
		(cmd_hdr)->err_specific_desc +				\
		tw_osl_strlen((cmd_hdr)->err_specific_desc) + 1,	\
		(cmd_hdr)->err_specific_desc);				\
	/* Print 18 bytes of sense information. */			\
	tw_cli_dbg_printf(2, ctlr->ctlr_handle,				\
		tw_osl_cur_func(),					\
		"sense info: %x %x %x %x %x %x %x %x %x "		\
		"%x %x %x %x %x %x %x %x %x",				\
		(cmd_hdr)->sense_data[0], (cmd_hdr)->sense_data[1],	\
		(cmd_hdr)->sense_data[2], (cmd_hdr)->sense_data[3],	\
		(cmd_hdr)->sense_data[4], (cmd_hdr)->sense_data[5],	\
		(cmd_hdr)->sense_data[6], (cmd_hdr)->sense_data[7],	\
		(cmd_hdr)->sense_data[8], (cmd_hdr)->sense_data[9],	\
		(cmd_hdr)->sense_data[10], (cmd_hdr)->sense_data[11],	\
		(cmd_hdr)->sense_data[12], (cmd_hdr)->sense_data[13],	\
		(cmd_hdr)->sense_data[14], (cmd_hdr)->sense_data[15],	\
		(cmd_hdr)->sense_data[16], (cmd_hdr)->sense_data[17]);	\
} while (0)



#endif /* TW_CL_H */
