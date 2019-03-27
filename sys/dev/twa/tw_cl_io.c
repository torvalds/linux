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
 * Modifications by: Manjunath Ranganathaiah
 */


/*
 * Common Layer I/O functions.
 */


#include "tw_osl_share.h"
#include "tw_cl_share.h"
#include "tw_cl_fwif.h"
#include "tw_cl_ioctl.h"
#include "tw_cl.h"
#include "tw_cl_externs.h"
#include "tw_osl_ioctl.h"

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_xpt_sim.h>



/*
 * Function name:	tw_cl_start_io
 * Description:		Interface to OS Layer for accepting SCSI requests.
 *
 * Input:		ctlr_handle	-- controller handle
 *			req_pkt		-- OSL built request packet
 *			req_handle	-- request handle
 * Output:		None
 * Return value:	0	-- success
 *			non-zero-- failure
 */
TW_INT32
tw_cl_start_io(struct tw_cl_ctlr_handle *ctlr_handle,
	struct tw_cl_req_packet *req_pkt, struct tw_cl_req_handle *req_handle)
{
	struct tw_cli_ctlr_context		*ctlr;
	struct tw_cli_req_context		*req;
	struct tw_cl_command_9k			*cmd;
	struct tw_cl_scsi_req_packet		*scsi_req;
	TW_INT32				error = TW_CL_ERR_REQ_SUCCESS;

	tw_cli_dbg_printf(10, ctlr_handle, tw_osl_cur_func(), "entered");

	ctlr = (struct tw_cli_ctlr_context *)(ctlr_handle->cl_ctlr_ctxt);

	/*
	 * If working with a firmware version that does not support multiple
	 * luns, and this request is directed at a non-zero lun, error it
	 * back right away.
	 */
	if ((req_pkt->gen_req_pkt.scsi_req.lun) &&
		(ctlr->working_srl < TWA_MULTI_LUN_FW_SRL)) {
		req_pkt->status |= (TW_CL_ERR_REQ_INVALID_LUN |
			TW_CL_ERR_REQ_SCSI_ERROR);
		req_pkt->tw_osl_callback(req_handle);
		return(TW_CL_ERR_REQ_SUCCESS);
	}

	if ((req = tw_cli_get_request(ctlr
		)) == TW_CL_NULL) {
		tw_cli_dbg_printf(2, ctlr_handle, tw_osl_cur_func(),
			"Out of request context packets: returning busy");
		return(TW_OSL_EBUSY);
	}

	req_handle->cl_req_ctxt = req;
	req->req_handle = req_handle;
	req->orig_req = req_pkt;
	req->tw_cli_callback = tw_cli_complete_io;

	req->flags |= TW_CLI_REQ_FLAGS_EXTERNAL;
	req->flags |= TW_CLI_REQ_FLAGS_9K;

	scsi_req = &(req_pkt->gen_req_pkt.scsi_req);

	/* Build the cmd pkt. */
	cmd = &(req->cmd_pkt->command.cmd_pkt_9k);

	req->cmd_pkt->cmd_hdr.header_desc.size_header = 128;

	cmd->res__opcode = BUILD_RES__OPCODE(0, TWA_FW_CMD_EXECUTE_SCSI);
	cmd->unit = (TW_UINT8)(scsi_req->unit);
	cmd->lun_l4__req_id = TW_CL_SWAP16(
		BUILD_LUN_L4__REQ_ID(scsi_req->lun, req->request_id));
	cmd->status = 0;
	cmd->sgl_offset = 16; /* offset from end of hdr = max cdb len */
	tw_osl_memcpy(cmd->cdb, scsi_req->cdb, scsi_req->cdb_len);

	if (req_pkt->flags & TW_CL_REQ_CALLBACK_FOR_SGLIST) {
		TW_UINT32	num_sgl_entries;

		req_pkt->tw_osl_sgl_callback(req_handle, cmd->sg_list,
			&num_sgl_entries);
		cmd->lun_h4__sgl_entries =
			TW_CL_SWAP16(BUILD_LUN_H4__SGL_ENTRIES(scsi_req->lun,
				num_sgl_entries));
	} else {
		cmd->lun_h4__sgl_entries =
			TW_CL_SWAP16(BUILD_LUN_H4__SGL_ENTRIES(scsi_req->lun,
				scsi_req->sgl_entries));
		tw_cli_fill_sg_list(ctlr, scsi_req->sg_list,
			cmd->sg_list, scsi_req->sgl_entries);
	}

	if (((TW_CL_Q_FIRST_ITEM(&(ctlr->req_q_head[TW_CLI_PENDING_Q]))) != TW_CL_NULL) ||
		(ctlr->reset_in_progress)) {
		tw_cli_req_q_insert_tail(req, TW_CLI_PENDING_Q);
		TW_CLI_WRITE_CONTROL_REGISTER(ctlr_handle,
			TWA_CONTROL_UNMASK_COMMAND_INTERRUPT);
	} else if ((error = tw_cli_submit_cmd(req))) {
		tw_cli_dbg_printf(2, ctlr_handle, tw_osl_cur_func(),
			"Could not start request. request = %p, error = %d",
			req, error);
		tw_cli_req_q_insert_tail(req, TW_CLI_FREE_Q);
	}
	return(error);
}



/*
 * Function name:	tw_cli_submit_cmd
 * Description:		Submits a cmd to firmware.
 *
 * Input:		req	-- ptr to CL internal request context
 * Output:		None
 * Return value:	0	-- success
 *			non-zero-- failure
 */
TW_INT32
tw_cli_submit_cmd(struct tw_cli_req_context *req)
{
	struct tw_cli_ctlr_context	*ctlr = req->ctlr;
	struct tw_cl_ctlr_handle	*ctlr_handle = ctlr->ctlr_handle;
	TW_UINT32			status_reg;
	TW_INT32			error = 0;

	tw_cli_dbg_printf(10, ctlr_handle, tw_osl_cur_func(), "entered");

	/* Serialize access to the controller cmd queue. */
	tw_osl_get_lock(ctlr_handle, ctlr->io_lock);

	/* For 9650SE first write low 4 bytes */
	if ((ctlr->device_id == TW_CL_DEVICE_ID_9K_E) ||
	    (ctlr->device_id == TW_CL_DEVICE_ID_9K_SA))
		tw_osl_write_reg(ctlr_handle,
				 TWA_COMMAND_QUEUE_OFFSET_LOW,
				 (TW_UINT32)(req->cmd_pkt_phys + sizeof(struct tw_cl_command_header)), 4);

	status_reg = TW_CLI_READ_STATUS_REGISTER(ctlr_handle);
	if (status_reg & TWA_STATUS_COMMAND_QUEUE_FULL) {
		struct tw_cl_req_packet	*req_pkt =
			(struct tw_cl_req_packet *)(req->orig_req);

		tw_cli_dbg_printf(7, ctlr_handle, tw_osl_cur_func(),
			"Cmd queue full");

		if ((req->flags & TW_CLI_REQ_FLAGS_INTERNAL)
			|| ((req_pkt) &&
			(req_pkt->flags & TW_CL_REQ_RETRY_ON_BUSY))
			) {
			if (req->state != TW_CLI_REQ_STATE_PENDING) {
				tw_cli_dbg_printf(2, ctlr_handle,
					tw_osl_cur_func(),
					"pending internal/ioctl request");
				req->state = TW_CLI_REQ_STATE_PENDING;
				tw_cli_req_q_insert_tail(req, TW_CLI_PENDING_Q);
				/* Unmask command interrupt. */
				TW_CLI_WRITE_CONTROL_REGISTER(ctlr_handle,
					TWA_CONTROL_UNMASK_COMMAND_INTERRUPT);
			} else
				error = TW_OSL_EBUSY;
		} else {
			error = TW_OSL_EBUSY;
		}
	} else {
		tw_cli_dbg_printf(10, ctlr_handle, tw_osl_cur_func(),
			"Submitting command");

		/* Insert command into busy queue */
		req->state = TW_CLI_REQ_STATE_BUSY;
		tw_cli_req_q_insert_tail(req, TW_CLI_BUSY_Q);

		if ((ctlr->device_id == TW_CL_DEVICE_ID_9K_E) ||
		    (ctlr->device_id == TW_CL_DEVICE_ID_9K_SA)) {
			/* Now write the high 4 bytes */
			tw_osl_write_reg(ctlr_handle, 
					 TWA_COMMAND_QUEUE_OFFSET_HIGH,
					 (TW_UINT32)(((TW_UINT64)(req->cmd_pkt_phys + sizeof(struct tw_cl_command_header)))>>32), 4);
		} else {
			if (ctlr->flags & TW_CL_64BIT_ADDRESSES) {
				/* First write the low 4 bytes, then the high 4. */
				tw_osl_write_reg(ctlr_handle,
						 TWA_COMMAND_QUEUE_OFFSET_LOW,
						 (TW_UINT32)(req->cmd_pkt_phys + sizeof(struct tw_cl_command_header)), 4);
				tw_osl_write_reg(ctlr_handle, 
						 TWA_COMMAND_QUEUE_OFFSET_HIGH,
						 (TW_UINT32)(((TW_UINT64)(req->cmd_pkt_phys + sizeof(struct tw_cl_command_header)))>>32), 4);
			} else
				tw_osl_write_reg(ctlr_handle, 
						 TWA_COMMAND_QUEUE_OFFSET,
						 (TW_UINT32)(req->cmd_pkt_phys + sizeof(struct tw_cl_command_header)), 4);
		}
	}

	tw_osl_free_lock(ctlr_handle, ctlr->io_lock);

	return(error);
}



/*
 * Function name:	tw_cl_fw_passthru
 * Description:		Interface to OS Layer for accepting firmware
 *			passthru requests.
 * Input:		ctlr_handle	-- controller handle
 *			req_pkt		-- OSL built request packet
 *			req_handle	-- request handle
 * Output:		None
 * Return value:	0	-- success
 *			non-zero-- failure
 */
TW_INT32
tw_cl_fw_passthru(struct tw_cl_ctlr_handle *ctlr_handle,
	struct tw_cl_req_packet *req_pkt, struct tw_cl_req_handle *req_handle)
{
	struct tw_cli_ctlr_context		*ctlr;
	struct tw_cli_req_context		*req;
	union tw_cl_command_7k			*cmd_7k;
	struct tw_cl_command_9k			*cmd_9k;
	struct tw_cl_passthru_req_packet	*pt_req;
	TW_UINT8				opcode;
	TW_UINT8				sgl_offset;
	TW_VOID					*sgl = TW_CL_NULL;
	TW_INT32				error = TW_CL_ERR_REQ_SUCCESS;

	tw_cli_dbg_printf(5, ctlr_handle, tw_osl_cur_func(), "entered");

	ctlr = (struct tw_cli_ctlr_context *)(ctlr_handle->cl_ctlr_ctxt);

	if ((req = tw_cli_get_request(ctlr
		)) == TW_CL_NULL) {
		tw_cli_dbg_printf(2, ctlr_handle, tw_osl_cur_func(),
			"Out of request context packets: returning busy");
		return(TW_OSL_EBUSY);
	}

	req_handle->cl_req_ctxt = req;
	req->req_handle = req_handle;
	req->orig_req = req_pkt;
	req->tw_cli_callback = tw_cli_complete_io;

	req->flags |= TW_CLI_REQ_FLAGS_PASSTHRU;

	pt_req = &(req_pkt->gen_req_pkt.pt_req);

	tw_osl_memcpy(req->cmd_pkt, pt_req->cmd_pkt,
		pt_req->cmd_pkt_length);
	/* Build the cmd pkt. */
	if ((opcode = GET_OPCODE(((TW_UINT8 *)
		(pt_req->cmd_pkt))[sizeof(struct tw_cl_command_header)]))
			== TWA_FW_CMD_EXECUTE_SCSI) {
		TW_UINT16	lun_l4, lun_h4;

		tw_cli_dbg_printf(5, ctlr_handle, tw_osl_cur_func(),
			"passthru: 9k cmd pkt");
		req->flags |= TW_CLI_REQ_FLAGS_9K;
		cmd_9k = &(req->cmd_pkt->command.cmd_pkt_9k);
		lun_l4 = GET_LUN_L4(cmd_9k->lun_l4__req_id);
		lun_h4 = GET_LUN_H4(cmd_9k->lun_h4__sgl_entries);
		cmd_9k->lun_l4__req_id = TW_CL_SWAP16(
			BUILD_LUN_L4__REQ_ID(lun_l4, req->request_id));
		if (pt_req->sgl_entries) {
			cmd_9k->lun_h4__sgl_entries =
				TW_CL_SWAP16(BUILD_LUN_H4__SGL_ENTRIES(lun_h4,
					pt_req->sgl_entries));
			sgl = (TW_VOID *)(cmd_9k->sg_list);
		}
	} else {
		tw_cli_dbg_printf(5, ctlr_handle, tw_osl_cur_func(),
			"passthru: 7k cmd pkt");
		cmd_7k = &(req->cmd_pkt->command.cmd_pkt_7k);
		cmd_7k->generic.request_id =
			(TW_UINT8)(TW_CL_SWAP16(req->request_id));
		if ((sgl_offset =
			GET_SGL_OFF(cmd_7k->generic.sgl_off__opcode))) {
			if (ctlr->device_id == TW_CL_DEVICE_ID_9K_SA)
				sgl = (((TW_UINT32 *)cmd_7k) + cmd_7k->generic.size);
			else
				sgl = (((TW_UINT32 *)cmd_7k) + sgl_offset);
			cmd_7k->generic.size += pt_req->sgl_entries *
				((ctlr->flags & TW_CL_64BIT_ADDRESSES) ? 3 : 2);
		}
	}

	if (sgl)
		tw_cli_fill_sg_list(ctlr, pt_req->sg_list,
			sgl, pt_req->sgl_entries);

	if (((TW_CL_Q_FIRST_ITEM(&(ctlr->req_q_head[TW_CLI_PENDING_Q]))) != TW_CL_NULL) ||
		(ctlr->reset_in_progress)) {
		tw_cli_req_q_insert_tail(req, TW_CLI_PENDING_Q);
		TW_CLI_WRITE_CONTROL_REGISTER(ctlr_handle,
			TWA_CONTROL_UNMASK_COMMAND_INTERRUPT);
	} else if ((error = tw_cli_submit_cmd(req))) {
		tw_cl_create_event(ctlr_handle, TW_CL_FALSE,
			TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
			0x1100, 0x1, TW_CL_SEVERITY_ERROR_STRING,
			"Failed to start passthru command",
			"error = %d", error);
		tw_cli_req_q_insert_tail(req, TW_CLI_FREE_Q);
	}
	return(error);
}



/*
 * Function name:	tw_cl_ioctl
 * Description:		Handler of CL supported ioctl cmds.
 *
 * Input:		ctlr	-- ptr to per ctlr structure
 *			cmd	-- ioctl cmd
 *			buf	-- ptr to buffer in kernel memory, which is
 *				   a copy of the input buffer in user-space
 * Output:		buf	-- ptr to buffer in kernel memory, which will
 *				   need to be copied to the output buffer in
 *				   user-space
 * Return value:	0	-- success
 *			non-zero-- failure
 */
TW_INT32
tw_cl_ioctl(struct tw_cl_ctlr_handle *ctlr_handle, u_long cmd, TW_VOID *buf)
{
	struct tw_cli_ctlr_context	*ctlr =
		(struct tw_cli_ctlr_context *)(ctlr_handle->cl_ctlr_ctxt);
	struct tw_cl_ioctl_packet	*user_buf =
		(struct tw_cl_ioctl_packet *)buf;
	struct tw_cl_event_packet	event_buf;
	TW_INT32			event_index;
	TW_INT32			start_index;
	TW_INT32			error = TW_OSL_ESUCCESS;

	tw_cli_dbg_printf(5, ctlr_handle, tw_osl_cur_func(), "entered");

	/* Serialize access to the AEN queue and the ioctl lock. */
	tw_osl_get_lock(ctlr_handle, ctlr->gen_lock);

	switch (cmd) {
	case TW_CL_IOCTL_GET_FIRST_EVENT:
		tw_cli_dbg_printf(3, ctlr_handle, tw_osl_cur_func(),
			"Get First Event");

		if (ctlr->aen_q_wrapped) {
			if (ctlr->aen_q_overflow) {
				/*
				 * The aen queue has wrapped, even before some
				 * events have been retrieved.  Let the caller
				 * know that he missed out on some AEN's.
				 */
				user_buf->driver_pkt.status =
					TW_CL_ERROR_AEN_OVERFLOW;
				ctlr->aen_q_overflow = TW_CL_FALSE;
			} else
				user_buf->driver_pkt.status = 0;
			event_index = ctlr->aen_head;
		} else {
			if (ctlr->aen_head == ctlr->aen_tail) {
				user_buf->driver_pkt.status =
					TW_CL_ERROR_AEN_NO_EVENTS;
				break;
			}
			user_buf->driver_pkt.status = 0;
			event_index = ctlr->aen_tail;	/* = 0 */
		}
		tw_osl_memcpy(user_buf->data_buf,
			&(ctlr->aen_queue[event_index]),
			sizeof(struct tw_cl_event_packet));

		ctlr->aen_queue[event_index].retrieved = TW_CL_AEN_RETRIEVED;

		break;


	case TW_CL_IOCTL_GET_LAST_EVENT:
		tw_cli_dbg_printf(3, ctlr_handle, tw_osl_cur_func(),
			"Get Last Event");

		if (ctlr->aen_q_wrapped) {
			if (ctlr->aen_q_overflow) {
				/*
				 * The aen queue has wrapped, even before some
				 * events have been retrieved.  Let the caller
				 * know that he missed out on some AEN's.
				 */
				user_buf->driver_pkt.status =
					TW_CL_ERROR_AEN_OVERFLOW;
				ctlr->aen_q_overflow = TW_CL_FALSE;
			} else
				user_buf->driver_pkt.status = 0;
		} else {
			if (ctlr->aen_head == ctlr->aen_tail) {
				user_buf->driver_pkt.status =
					TW_CL_ERROR_AEN_NO_EVENTS;
				break;
			}
			user_buf->driver_pkt.status = 0;
		}
		event_index = (ctlr->aen_head - 1 + ctlr->max_aens_supported) %
			ctlr->max_aens_supported;

		tw_osl_memcpy(user_buf->data_buf,
			&(ctlr->aen_queue[event_index]),
			sizeof(struct tw_cl_event_packet));

		ctlr->aen_queue[event_index].retrieved = TW_CL_AEN_RETRIEVED;
		
		break;


	case TW_CL_IOCTL_GET_NEXT_EVENT:
		tw_cli_dbg_printf(3, ctlr_handle, tw_osl_cur_func(),
			"Get Next Event");

		user_buf->driver_pkt.status = 0;
		if (ctlr->aen_q_wrapped) {
			tw_cli_dbg_printf(3, ctlr_handle, tw_osl_cur_func(),
				"Get Next Event: wrapped");
			if (ctlr->aen_q_overflow) {
				/*
				 * The aen queue has wrapped, even before some
				 * events have been retrieved.  Let the caller
				 * know that he missed out on some AEN's.
				 */
				tw_cli_dbg_printf(2, ctlr_handle,
					tw_osl_cur_func(),
					"Get Next Event: overflow");
				user_buf->driver_pkt.status =
					TW_CL_ERROR_AEN_OVERFLOW;
				ctlr->aen_q_overflow = TW_CL_FALSE;
			}
			start_index = ctlr->aen_head;
		} else {
			if (ctlr->aen_head == ctlr->aen_tail) {
				tw_cli_dbg_printf(3, ctlr_handle,
					tw_osl_cur_func(),
					"Get Next Event: empty queue");
				user_buf->driver_pkt.status =
					TW_CL_ERROR_AEN_NO_EVENTS;
				break;
			}
			start_index = ctlr->aen_tail;	/* = 0 */
		}
		tw_osl_memcpy(&event_buf, user_buf->data_buf,
			sizeof(struct tw_cl_event_packet));

		event_index = (start_index + event_buf.sequence_id -
			ctlr->aen_queue[start_index].sequence_id + 1) %
			ctlr->max_aens_supported;

		tw_cli_dbg_printf(3, ctlr_handle, tw_osl_cur_func(),
			"Get Next Event: si = %x, ei = %x, ebsi = %x, "
			"sisi = %x, eisi = %x",
			start_index, event_index, event_buf.sequence_id,
			ctlr->aen_queue[start_index].sequence_id,
			ctlr->aen_queue[event_index].sequence_id);

		if (! (ctlr->aen_queue[event_index].sequence_id >
			event_buf.sequence_id)) {
			/*
			 * We don't have any event matching the criterion.  So,
			 * we have to report TW_CL_ERROR_NO_EVENTS.  If we also
			 * encountered an overflow condition above, we cannot
			 * report both conditions during this call.  We choose
			 * to report NO_EVENTS this time, and an overflow the
			 * next time we are called.
			 */
			if (user_buf->driver_pkt.status ==
				TW_CL_ERROR_AEN_OVERFLOW) {
				/*
				 * Make a note so we report the overflow
				 * next time.
				 */
				ctlr->aen_q_overflow = TW_CL_TRUE;
			}
			user_buf->driver_pkt.status = TW_CL_ERROR_AEN_NO_EVENTS;
			break;
		}
		/* Copy the event -- even if there has been an overflow. */
		tw_osl_memcpy(user_buf->data_buf,
			&(ctlr->aen_queue[event_index]),
			sizeof(struct tw_cl_event_packet));

		ctlr->aen_queue[event_index].retrieved = TW_CL_AEN_RETRIEVED;

		break;


	case TW_CL_IOCTL_GET_PREVIOUS_EVENT:
		tw_cli_dbg_printf(3, ctlr_handle, tw_osl_cur_func(),
			"Get Previous Event");

		user_buf->driver_pkt.status = 0;
		if (ctlr->aen_q_wrapped) {
			if (ctlr->aen_q_overflow) {
				/*
				 * The aen queue has wrapped, even before some
				 * events have been retrieved.  Let the caller
				 * know that he missed out on some AEN's.
				 */
				user_buf->driver_pkt.status =
					TW_CL_ERROR_AEN_OVERFLOW;
				ctlr->aen_q_overflow = TW_CL_FALSE;
			}
			start_index = ctlr->aen_head;
		} else {
			if (ctlr->aen_head == ctlr->aen_tail) {
				user_buf->driver_pkt.status =
					TW_CL_ERROR_AEN_NO_EVENTS;
				break;
			}
			start_index = ctlr->aen_tail;	/* = 0 */
		}
		tw_osl_memcpy(&event_buf, user_buf->data_buf,
			sizeof(struct tw_cl_event_packet));

		event_index = (start_index + event_buf.sequence_id -
			ctlr->aen_queue[start_index].sequence_id - 1) %
			ctlr->max_aens_supported;

		if (! (ctlr->aen_queue[event_index].sequence_id <
			event_buf.sequence_id)) {
			/*
			 * We don't have any event matching the criterion.  So,
			 * we have to report TW_CL_ERROR_NO_EVENTS.  If we also
			 * encountered an overflow condition above, we cannot
			 * report both conditions during this call.  We choose
			 * to report NO_EVENTS this time, and an overflow the
			 * next time we are called.
			 */
			if (user_buf->driver_pkt.status ==
				TW_CL_ERROR_AEN_OVERFLOW) {
				/*
				 * Make a note so we report the overflow
				 * next time.
				 */
				ctlr->aen_q_overflow = TW_CL_TRUE;
			}
			user_buf->driver_pkt.status = TW_CL_ERROR_AEN_NO_EVENTS;
			break;
		}
		/* Copy the event -- even if there has been an overflow. */
		tw_osl_memcpy(user_buf->data_buf,
			&(ctlr->aen_queue[event_index]),
			sizeof(struct tw_cl_event_packet));

		ctlr->aen_queue[event_index].retrieved = TW_CL_AEN_RETRIEVED;

		break;


	case TW_CL_IOCTL_GET_LOCK:
	{
		struct tw_cl_lock_packet	lock_pkt;
		TW_TIME				cur_time;

		tw_cli_dbg_printf(3, ctlr_handle, tw_osl_cur_func(),
			"Get ioctl lock");

		cur_time = tw_osl_get_local_time();
		tw_osl_memcpy(&lock_pkt, user_buf->data_buf,
			sizeof(struct tw_cl_lock_packet));

		if ((ctlr->ioctl_lock.lock == TW_CLI_LOCK_FREE) ||
			(lock_pkt.force_flag) ||
			(cur_time >= ctlr->ioctl_lock.timeout)) {
			tw_cli_dbg_printf(3, ctlr_handle, tw_osl_cur_func(),
				"GET_LOCK: Getting lock!");
			ctlr->ioctl_lock.lock = TW_CLI_LOCK_HELD;
			ctlr->ioctl_lock.timeout =
				cur_time + (lock_pkt.timeout_msec / 1000);
			lock_pkt.time_remaining_msec = lock_pkt.timeout_msec;
			user_buf->driver_pkt.status = 0;
		} else {
			tw_cli_dbg_printf(2, ctlr_handle, tw_osl_cur_func(),
				"GET_LOCK: Lock already held!");
			lock_pkt.time_remaining_msec = (TW_UINT32)(
				(ctlr->ioctl_lock.timeout - cur_time) * 1000);
			user_buf->driver_pkt.status =
				TW_CL_ERROR_IOCTL_LOCK_ALREADY_HELD;
		}
		tw_osl_memcpy(user_buf->data_buf, &lock_pkt,
			sizeof(struct tw_cl_lock_packet));
		break;
	}


	case TW_CL_IOCTL_RELEASE_LOCK:
		tw_cli_dbg_printf(3, ctlr_handle, tw_osl_cur_func(),
			"Release ioctl lock");

		if (ctlr->ioctl_lock.lock == TW_CLI_LOCK_FREE) {
			tw_cli_dbg_printf(2, ctlr_handle, tw_osl_cur_func(),
				"twa_ioctl: RELEASE_LOCK: Lock not held!");
			user_buf->driver_pkt.status =
				TW_CL_ERROR_IOCTL_LOCK_NOT_HELD;
		} else {
			tw_cli_dbg_printf(3, ctlr_handle, tw_osl_cur_func(),
				"RELEASE_LOCK: Releasing lock!");
			ctlr->ioctl_lock.lock = TW_CLI_LOCK_FREE;
			user_buf->driver_pkt.status = 0;
		}
		break;


	case TW_CL_IOCTL_GET_COMPATIBILITY_INFO:
	{
		struct tw_cl_compatibility_packet	comp_pkt;

		tw_cli_dbg_printf(3, ctlr_handle, tw_osl_cur_func(),
			"Get compatibility info");

		tw_osl_memcpy(comp_pkt.driver_version,
			TW_OSL_DRIVER_VERSION_STRING,
			sizeof(TW_OSL_DRIVER_VERSION_STRING));
		comp_pkt.working_srl = ctlr->working_srl;
		comp_pkt.working_branch = ctlr->working_branch;
		comp_pkt.working_build = ctlr->working_build;
		comp_pkt.driver_srl_high = TWA_CURRENT_FW_SRL;
		comp_pkt.driver_branch_high =
			TWA_CURRENT_FW_BRANCH(ctlr->arch_id);
		comp_pkt.driver_build_high =
			TWA_CURRENT_FW_BUILD(ctlr->arch_id);
		comp_pkt.driver_srl_low = TWA_BASE_FW_SRL;
		comp_pkt.driver_branch_low = TWA_BASE_FW_BRANCH;
		comp_pkt.driver_build_low = TWA_BASE_FW_BUILD;
		comp_pkt.fw_on_ctlr_srl = ctlr->fw_on_ctlr_srl;
		comp_pkt.fw_on_ctlr_branch = ctlr->fw_on_ctlr_branch;
		comp_pkt.fw_on_ctlr_build = ctlr->fw_on_ctlr_build;
		user_buf->driver_pkt.status = 0;

		/* Copy compatibility information to user space. */
		tw_osl_memcpy(user_buf->data_buf, &comp_pkt,
			(sizeof(struct tw_cl_compatibility_packet) <
			user_buf->driver_pkt.buffer_length) ?
			sizeof(struct tw_cl_compatibility_packet) :
			user_buf->driver_pkt.buffer_length);
		break;
	}

	default:	
		/* Unknown opcode. */
		tw_cli_dbg_printf(3, ctlr_handle, tw_osl_cur_func(),
			"Unknown ioctl cmd 0x%x", cmd);
		error = TW_OSL_ENOTTY;
	}

	tw_osl_free_lock(ctlr_handle, ctlr->gen_lock);
	return(error);
}



/*
 * Function name:	tw_cli_get_param
 * Description:		Get a firmware parameter.
 *
 * Input:		ctlr		-- ptr to per ctlr structure
 *			table_id	-- parameter table #
 *			param_id	-- index of the parameter in the table
 *			param_size	-- size of the parameter in bytes
 *			callback	-- ptr to function, if any, to be called
 *					back on completion; TW_CL_NULL if no callback.
 * Output:		param_data	-- param value
 * Return value:	0	-- success
 *			non-zero-- failure
 */
TW_INT32
tw_cli_get_param(struct tw_cli_ctlr_context *ctlr, TW_INT32 table_id,
	TW_INT32 param_id, TW_VOID *param_data, TW_INT32 param_size,
	TW_VOID (* callback)(struct tw_cli_req_context *req))
{
	struct tw_cli_req_context	*req;
	union tw_cl_command_7k		*cmd;
	struct tw_cl_param_9k		*param = TW_CL_NULL;
	TW_INT32			error = TW_OSL_EBUSY;

	tw_cli_dbg_printf(4, ctlr->ctlr_handle, tw_osl_cur_func(), "entered");

	/* Get a request packet. */
	if ((req = tw_cli_get_request(ctlr
		)) == TW_CL_NULL)
		goto out;

	/* Make sure this is the only CL internal request at this time. */
	if (ctlr->internal_req_busy) {
		error = TW_OSL_EBUSY;
		goto out;
	}
	ctlr->internal_req_busy = TW_CL_TRUE;
	req->data = ctlr->internal_req_data;
	req->data_phys = ctlr->internal_req_data_phys;
	req->length = TW_CLI_SECTOR_SIZE;
	req->flags |= TW_CLI_REQ_FLAGS_INTERNAL;

	/* Initialize memory to read data into. */
	param = (struct tw_cl_param_9k *)(req->data);
	tw_osl_memzero(param, sizeof(struct tw_cl_param_9k) - 1 + param_size);

	/* Build the cmd pkt. */
	cmd = &(req->cmd_pkt->command.cmd_pkt_7k);

	req->cmd_pkt->cmd_hdr.header_desc.size_header = 128;

	cmd->param.sgl_off__opcode =
		BUILD_SGL_OFF__OPCODE(2, TWA_FW_CMD_GET_PARAM);
	cmd->param.request_id = (TW_UINT8)(TW_CL_SWAP16(req->request_id));
	cmd->param.host_id__unit = BUILD_HOST_ID__UNIT(0, 0);
	cmd->param.param_count = TW_CL_SWAP16(1);

	if (ctlr->flags & TW_CL_64BIT_ADDRESSES) {
		((struct tw_cl_sg_desc64 *)(cmd->param.sgl))[0].address =
			TW_CL_SWAP64(req->data_phys);
		((struct tw_cl_sg_desc64 *)(cmd->param.sgl))[0].length =
			TW_CL_SWAP32(req->length);
		cmd->param.size = 2 + 3;
	} else {
		((struct tw_cl_sg_desc32 *)(cmd->param.sgl))[0].address =
			TW_CL_SWAP32(req->data_phys);
		((struct tw_cl_sg_desc32 *)(cmd->param.sgl))[0].length =
			TW_CL_SWAP32(req->length);
		cmd->param.size = 2 + 2;
	}

	/* Specify which parameter we need. */
	param->table_id = TW_CL_SWAP16(table_id | TWA_9K_PARAM_DESCRIPTOR);
	param->parameter_id = (TW_UINT8)(param_id);
	param->parameter_size_bytes = TW_CL_SWAP16(param_size);

	/* Submit the command. */
	if (callback == TW_CL_NULL) {
		/* There's no call back; wait till the command completes. */
		error = tw_cli_submit_and_poll_request(req,
				TW_CLI_REQUEST_TIMEOUT_PERIOD);
		if (error)
			goto out;
		if ((error = cmd->param.status)) {
#if       0
			tw_cli_create_ctlr_event(ctlr,
				TW_CL_MESSAGE_SOURCE_CONTROLLER_ERROR,
				&(req->cmd_pkt->cmd_hdr));
#endif // 0
			goto out;
		}
		tw_osl_memcpy(param_data, param->data, param_size);
		ctlr->internal_req_busy = TW_CL_FALSE;
		tw_cli_req_q_insert_tail(req, TW_CLI_FREE_Q);
	} else {
		/* There's a call back.  Simply submit the command. */
		req->tw_cli_callback = callback;
		if ((error = tw_cli_submit_cmd(req)))
			goto out;
	}
	return(0);

out:
	tw_cl_create_event(ctlr->ctlr_handle, TW_CL_FALSE,
		TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
		0x1101, 0x1, TW_CL_SEVERITY_ERROR_STRING,
		"get_param failed",
		"error = %d", error);
	if (param)
		ctlr->internal_req_busy = TW_CL_FALSE;
	if (req)
		tw_cli_req_q_insert_tail(req, TW_CLI_FREE_Q);
	return(1);
}



/*
 * Function name:	tw_cli_set_param
 * Description:		Set a firmware parameter.
 *
 * Input:		ctlr		-- ptr to per ctlr structure
 *			table_id	-- parameter table #
 *			param_id	-- index of the parameter in the table
 *			param_size	-- size of the parameter in bytes
 *			callback	-- ptr to function, if any, to be called
 *					back on completion; TW_CL_NULL if no callback.
 * Output:		None
 * Return value:	0	-- success
 *			non-zero-- failure
 */
TW_INT32
tw_cli_set_param(struct tw_cli_ctlr_context *ctlr, TW_INT32 table_id,
	TW_INT32 param_id, TW_INT32 param_size, TW_VOID *data,
	TW_VOID (* callback)(struct tw_cli_req_context *req))
{
	struct tw_cli_req_context	*req;
	union tw_cl_command_7k		*cmd;
	struct tw_cl_param_9k		*param = TW_CL_NULL;
	TW_INT32			error = TW_OSL_EBUSY;

	tw_cli_dbg_printf(4, ctlr->ctlr_handle, tw_osl_cur_func(), "entered");

	/* Get a request packet. */
	if ((req = tw_cli_get_request(ctlr
		)) == TW_CL_NULL)
		goto out;

	/* Make sure this is the only CL internal request at this time. */
	if (ctlr->internal_req_busy) {
		error = TW_OSL_EBUSY;
		goto out;
	}
	ctlr->internal_req_busy = TW_CL_TRUE;
	req->data = ctlr->internal_req_data;
	req->data_phys = ctlr->internal_req_data_phys;
	req->length = TW_CLI_SECTOR_SIZE;
	req->flags |= TW_CLI_REQ_FLAGS_INTERNAL;

	/* Initialize memory to send data using. */
	param = (struct tw_cl_param_9k *)(req->data);
	tw_osl_memzero(param, sizeof(struct tw_cl_param_9k) - 1 + param_size);

	/* Build the cmd pkt. */
	cmd = &(req->cmd_pkt->command.cmd_pkt_7k);

	req->cmd_pkt->cmd_hdr.header_desc.size_header = 128;

	cmd->param.sgl_off__opcode =
		BUILD_SGL_OFF__OPCODE(2, TWA_FW_CMD_SET_PARAM);
	cmd->param.request_id = (TW_UINT8)(TW_CL_SWAP16(req->request_id));
	cmd->param.host_id__unit = BUILD_HOST_ID__UNIT(0, 0);
	cmd->param.param_count = TW_CL_SWAP16(1);

	if (ctlr->flags & TW_CL_64BIT_ADDRESSES) {
		((struct tw_cl_sg_desc64 *)(cmd->param.sgl))[0].address =
			TW_CL_SWAP64(req->data_phys);
		((struct tw_cl_sg_desc64 *)(cmd->param.sgl))[0].length =
			TW_CL_SWAP32(req->length);
		cmd->param.size = 2 + 3;
	} else {
		((struct tw_cl_sg_desc32 *)(cmd->param.sgl))[0].address =
			TW_CL_SWAP32(req->data_phys);
		((struct tw_cl_sg_desc32 *)(cmd->param.sgl))[0].length =
			TW_CL_SWAP32(req->length);
		cmd->param.size = 2 + 2;
	}

	/* Specify which parameter we want to set. */
	param->table_id = TW_CL_SWAP16(table_id | TWA_9K_PARAM_DESCRIPTOR);
	param->parameter_id = (TW_UINT8)(param_id);
	param->parameter_size_bytes = TW_CL_SWAP16(param_size);
	tw_osl_memcpy(param->data, data, param_size);

	/* Submit the command. */
	if (callback == TW_CL_NULL) {
		/* There's no call back; wait till the command completes. */
		error = tw_cli_submit_and_poll_request(req,
				TW_CLI_REQUEST_TIMEOUT_PERIOD);
		if (error)
			goto out;
		if ((error = cmd->param.status)) {
#if       0
			tw_cli_create_ctlr_event(ctlr,
				TW_CL_MESSAGE_SOURCE_CONTROLLER_ERROR,
				&(req->cmd_pkt->cmd_hdr));
#endif // 0
			goto out;
		}
		ctlr->internal_req_busy = TW_CL_FALSE;
		tw_cli_req_q_insert_tail(req, TW_CLI_FREE_Q);
	} else {
		/* There's a call back.  Simply submit the command. */
		req->tw_cli_callback = callback;
		if ((error = tw_cli_submit_cmd(req)))
			goto out;
	}
	return(error);

out:
	tw_cl_create_event(ctlr->ctlr_handle, TW_CL_FALSE,
		TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
		0x1102, 0x1, TW_CL_SEVERITY_ERROR_STRING,
		"set_param failed",
		"error = %d", error);
	if (param)
		ctlr->internal_req_busy = TW_CL_FALSE;
	if (req)
		tw_cli_req_q_insert_tail(req, TW_CLI_FREE_Q);
	return(error);
}



/*
 * Function name:	tw_cli_submit_and_poll_request
 * Description:		Sends down a firmware cmd, and waits for the completion
 *			in a tight loop.
 *
 * Input:		req	-- ptr to request pkt
 *			timeout -- max # of seconds to wait before giving up
 * Output:		None
 * Return value:	0	-- success
 *			non-zero-- failure
 */
TW_INT32
tw_cli_submit_and_poll_request(struct tw_cli_req_context *req,
	TW_UINT32 timeout)
{
	struct tw_cli_ctlr_context	*ctlr = req->ctlr;
	TW_TIME				end_time;
	TW_INT32			error;

	tw_cli_dbg_printf(4, ctlr->ctlr_handle, tw_osl_cur_func(), "entered");

	/*
	 * If the cmd queue is full, tw_cli_submit_cmd will queue this
	 * request in the pending queue, since this is an internal request.
	 */
	if ((error = tw_cli_submit_cmd(req))) {
		tw_cl_create_event(ctlr->ctlr_handle, TW_CL_FALSE,
			TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
			0x1103, 0x1, TW_CL_SEVERITY_ERROR_STRING,
			"Failed to start internal request",
			"error = %d", error);
		return(error);
	}

	/*
	 * Poll for the response until the command gets completed, or there's
	 * a timeout.
	 */
	end_time = tw_osl_get_local_time() + timeout;
	do {
		if ((error = req->error_code))
			/*
			 * This will take care of completion due to a reset,
			 * or a failure in tw_cli_submit_pending_queue.
			 * The caller should do the clean-up.
			 */
			return(error);

		/* See if the command completed. */
		tw_cli_process_resp_intr(ctlr);

		if ((req->state != TW_CLI_REQ_STATE_BUSY) &&
			(req->state != TW_CLI_REQ_STATE_PENDING))
			return(req->state != TW_CLI_REQ_STATE_COMPLETE);
	} while (tw_osl_get_local_time() <= end_time);

	/* Time out! */
	tw_cl_create_event(ctlr->ctlr_handle, TW_CL_FALSE,
		TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
		0x1104, 0x1, TW_CL_SEVERITY_ERROR_STRING,
		"Internal request timed out",
		"request = %p", req);

	/*
	 * We will reset the controller only if the request has already been
	 * submitted, so as to not lose the request packet.  If a busy request
	 * timed out, the reset will take care of freeing resources.  If a
	 * pending request timed out, we will free resources for that request,
	 * right here, thereby avoiding a reset.  So, the caller is expected
	 * to NOT cleanup when TW_OSL_ETIMEDOUT is returned.
	 */

	/*
	 * We have to make sure that this timed out request, if it were in the
	 * pending queue, doesn't get submitted while we are here, from
	 * tw_cli_submit_pending_queue.  There could be a race in that case.
	 * Need to revisit.
	 */
	if (req->state == TW_CLI_REQ_STATE_PENDING) {
		tw_cli_dbg_printf(3, ctlr->ctlr_handle, tw_osl_cur_func(),
			"Removing request from pending queue");
		/*
		 * Request was never submitted.  Clean up.  Note that we did
		 * not do a reset.  So, we have to remove the request ourselves
		 * from the pending queue (as against tw_cli_drain_pendinq_queue
		 * taking care of it).
		 */
		tw_cli_req_q_remove_item(req, TW_CLI_PENDING_Q);
		if ((TW_CL_Q_FIRST_ITEM(&(ctlr->req_q_head[TW_CLI_PENDING_Q]))) == TW_CL_NULL)
			TW_CLI_WRITE_CONTROL_REGISTER(ctlr->ctlr_handle,
				TWA_CONTROL_MASK_COMMAND_INTERRUPT);
		if (req->data)
			ctlr->internal_req_busy = TW_CL_FALSE;
		tw_cli_req_q_insert_tail(req, TW_CLI_FREE_Q);
	}

	return(TW_OSL_ETIMEDOUT);
}



/*
 * Function name:	tw_cl_reset_ctlr
 * Description:		Soft resets and then initializes the controller;
 *			drains any incomplete requests.
 *
 * Input:		ctlr	-- ptr to per ctlr structure
 * 			req_handle	-- ptr to request handle
 * Output:		None
 * Return value:	0	-- success
 *			non-zero-- failure
 */
TW_INT32
tw_cl_reset_ctlr(struct tw_cl_ctlr_handle *ctlr_handle)
{
	struct tw_cli_ctlr_context	*ctlr =
		(struct tw_cli_ctlr_context *)(ctlr_handle->cl_ctlr_ctxt);
	struct twa_softc		*sc = ctlr_handle->osl_ctlr_ctxt;
	struct tw_cli_req_context	*req;
	TW_INT32			reset_attempt = 1;
	TW_INT32			error = 0;

	tw_cli_dbg_printf(2, ctlr_handle, tw_osl_cur_func(), "entered");

	ctlr->reset_in_progress = TW_CL_TRUE;
	twa_teardown_intr(sc);


	/*
	 * Error back all requests in the complete, busy, and pending queues.
	 * If any request is already on its way to getting submitted, it's in
	 * none of these queues and so, will not be completed.  That request
	 * will continue its course and get submitted to the controller after
	 * the reset is done (and io_lock is released).
	 */
	tw_cli_drain_complete_queue(ctlr);
	tw_cli_drain_busy_queue(ctlr);
	tw_cli_drain_pending_queue(ctlr);
	ctlr->internal_req_busy = TW_CL_FALSE;
	ctlr->get_more_aens     = TW_CL_FALSE;

	/* Soft reset the controller. */
	while (reset_attempt <= TW_CLI_MAX_RESET_ATTEMPTS) {
		if ((error = tw_cli_soft_reset(ctlr))) {
			tw_cl_create_event(ctlr_handle, TW_CL_FALSE,
				TW_CL_MESSAGE_SOURCE_COMMON_LAYER_EVENT,
				0x1105, 0x1, TW_CL_SEVERITY_ERROR_STRING,
				"Controller reset failed",
				"error = %d; attempt %d", error, reset_attempt++);
			reset_attempt++;
			continue;
		}

		/* Re-establish logical connection with the controller. */
		if ((error = tw_cli_init_connection(ctlr,
				(TW_UINT16)(ctlr->max_simult_reqs),
				0, 0, 0, 0, 0, TW_CL_NULL, TW_CL_NULL, TW_CL_NULL,
				TW_CL_NULL, TW_CL_NULL))) {
			tw_cl_create_event(ctlr_handle, TW_CL_FALSE,
				TW_CL_MESSAGE_SOURCE_COMMON_LAYER_EVENT,
				0x1106, 0x1, TW_CL_SEVERITY_ERROR_STRING,
				"Can't initialize connection after reset",
				"error = %d", error);
			reset_attempt++;
			continue;
		}

#ifdef    TW_OSL_DEBUG
		tw_cl_create_event(ctlr_handle, TW_CL_FALSE,
			TW_CL_MESSAGE_SOURCE_COMMON_LAYER_EVENT,
			0x1107, 0x3, TW_CL_SEVERITY_INFO_STRING,
			"Controller reset done!", " ");
#endif /* TW_OSL_DEBUG */
		break;
	} /* End of while */

	/* Move commands from the reset queue to the pending queue. */
	while ((req = tw_cli_req_q_remove_head(ctlr, TW_CLI_RESET_Q)) != TW_CL_NULL) {
		tw_osl_timeout(req->req_handle);
		tw_cli_req_q_insert_tail(req, TW_CLI_PENDING_Q);
	}

	twa_setup_intr(sc);
	tw_cli_enable_interrupts(ctlr);
	if ((TW_CL_Q_FIRST_ITEM(&(ctlr->req_q_head[TW_CLI_PENDING_Q]))) != TW_CL_NULL)
		TW_CLI_WRITE_CONTROL_REGISTER(ctlr_handle,
			TWA_CONTROL_UNMASK_COMMAND_INTERRUPT);
	ctlr->reset_in_progress = TW_CL_FALSE;
	ctlr->reset_needed = TW_CL_FALSE;

	/* Request for a bus re-scan. */
	tw_osl_scan_bus(ctlr_handle);

	return(error);
}

TW_VOID
tw_cl_set_reset_needed(struct tw_cl_ctlr_handle *ctlr_handle)
{
	struct tw_cli_ctlr_context	*ctlr =
		(struct tw_cli_ctlr_context *)(ctlr_handle->cl_ctlr_ctxt);

	ctlr->reset_needed = TW_CL_TRUE;
}

TW_INT32
tw_cl_is_reset_needed(struct tw_cl_ctlr_handle *ctlr_handle)
{
	struct tw_cli_ctlr_context	*ctlr =
		(struct tw_cli_ctlr_context *)(ctlr_handle->cl_ctlr_ctxt);

	return(ctlr->reset_needed);
}

TW_INT32
tw_cl_is_active(struct tw_cl_ctlr_handle *ctlr_handle)
{
	struct tw_cli_ctlr_context	*ctlr =
		(struct tw_cli_ctlr_context *)
		(ctlr_handle->cl_ctlr_ctxt);

		return(ctlr->active);
}



/*
 * Function name:	tw_cli_soft_reset
 * Description:		Does the actual soft reset.
 *
 * Input:		ctlr	-- ptr to per ctlr structure
 * Output:		None
 * Return value:	0	-- success
 *			non-zero-- failure
 */
TW_INT32
tw_cli_soft_reset(struct tw_cli_ctlr_context *ctlr)
{
	struct tw_cl_ctlr_handle	*ctlr_handle = ctlr->ctlr_handle;
	int				found;
	int				loop_count;
	TW_UINT32			error;

	tw_cli_dbg_printf(1, ctlr_handle, tw_osl_cur_func(), "entered");

	tw_cl_create_event(ctlr_handle, TW_CL_FALSE,
		TW_CL_MESSAGE_SOURCE_COMMON_LAYER_EVENT,
		0x1108, 0x3, TW_CL_SEVERITY_INFO_STRING,
		"Resetting controller...",
		" ");

	/* Don't let any new commands get submitted to the controller. */
	tw_osl_get_lock(ctlr_handle, ctlr->io_lock);

	TW_CLI_SOFT_RESET(ctlr_handle);

	if ((ctlr->device_id == TW_CL_DEVICE_ID_9K_X) ||
	    (ctlr->device_id == TW_CL_DEVICE_ID_9K_E) ||
	    (ctlr->device_id == TW_CL_DEVICE_ID_9K_SA)) {
		/*
		 * There's a hardware bug in the G133 ASIC, which can lead to
		 * PCI parity errors and hangs, if the host accesses any
		 * registers when the firmware is resetting the hardware, as
		 * part of a hard/soft reset.  The window of time when the
		 * problem can occur is about 10 ms.  Here, we will handshake
		 * with the firmware to find out when the firmware is pulling
		 * down the hardware reset pin, and wait for about 500 ms to
		 * make sure we don't access any hardware registers (for
		 * polling) during that window.
		 */
		ctlr->reset_phase1_in_progress = TW_CL_TRUE;
		loop_count = 0;
		do {
			found = (tw_cli_find_response(ctlr, TWA_RESET_PHASE1_NOTIFICATION_RESPONSE) == TW_OSL_ESUCCESS);
			tw_osl_delay(10);
			loop_count++;
			error = 0x7888;
		} while (!found && (loop_count < 6000000)); /* Loop for no more than 60 seconds */

		if (!found) {
			tw_cl_create_event(ctlr_handle, TW_CL_FALSE,
				TW_CL_MESSAGE_SOURCE_COMMON_LAYER_EVENT,
				0x1109, 0x1, TW_CL_SEVERITY_ERROR_STRING,
				"Missed firmware handshake after soft-reset",
				"error = %d", error);
			tw_osl_free_lock(ctlr_handle, ctlr->io_lock);
			return(error);
		}

		tw_osl_delay(TWA_RESET_PHASE1_WAIT_TIME_MS * 1000);
		ctlr->reset_phase1_in_progress = TW_CL_FALSE;
	}

	if ((error = tw_cli_poll_status(ctlr,
			TWA_STATUS_MICROCONTROLLER_READY |
			TWA_STATUS_ATTENTION_INTERRUPT,
			TW_CLI_RESET_TIMEOUT_PERIOD))) {
		tw_cl_create_event(ctlr_handle, TW_CL_FALSE,
			TW_CL_MESSAGE_SOURCE_COMMON_LAYER_EVENT,
			0x1109, 0x1, TW_CL_SEVERITY_ERROR_STRING,
			"Micro-ctlr not ready/No attn intr after reset",
			"error = %d", error);
		tw_osl_free_lock(ctlr_handle, ctlr->io_lock);
		return(error);
	}

	TW_CLI_WRITE_CONTROL_REGISTER(ctlr_handle,
		TWA_CONTROL_CLEAR_ATTENTION_INTERRUPT);

	if ((error = tw_cli_drain_response_queue(ctlr))) {
		tw_cl_create_event(ctlr_handle, TW_CL_FALSE,
			TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
			0x110A, 0x1, TW_CL_SEVERITY_ERROR_STRING,
			"Can't drain response queue after reset",
			"error = %d", error);
		tw_osl_free_lock(ctlr_handle, ctlr->io_lock);
		return(error);
	}
	
	tw_osl_free_lock(ctlr_handle, ctlr->io_lock);

	if ((error = tw_cli_drain_aen_queue(ctlr))) {
		tw_cl_create_event(ctlr_handle, TW_CL_FALSE,
			TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
			0x110B, 0x1, TW_CL_SEVERITY_ERROR_STRING,
			"Can't drain AEN queue after reset",
			"error = %d", error);
		return(error);
	}
	
	if ((error = tw_cli_find_aen(ctlr, TWA_AEN_SOFT_RESET))) {
		tw_cl_create_event(ctlr_handle, TW_CL_FALSE,
			TW_CL_MESSAGE_SOURCE_COMMON_LAYER_EVENT,
			0x110C, 0x1, TW_CL_SEVERITY_ERROR_STRING,
			"Reset not reported by controller",
			"error = %d", error);
		return(error);
	}

	return(TW_OSL_ESUCCESS);
}



/*
 * Function name:	tw_cli_send_scsi_cmd
 * Description:		Sends down a scsi cmd to fw.
 *
 * Input:		req	-- ptr to request pkt
 *			cmd	-- opcode of scsi cmd to send
 * Output:		None
 * Return value:	0	-- success
 *			non-zero-- failure
 */
TW_INT32
tw_cli_send_scsi_cmd(struct tw_cli_req_context *req, TW_INT32 cmd)
{
	struct tw_cl_command_packet	*cmdpkt;
	struct tw_cl_command_9k		*cmd9k;
	struct tw_cli_ctlr_context	*ctlr;
	TW_INT32			error;

	ctlr = req->ctlr;
	tw_cli_dbg_printf(4, ctlr->ctlr_handle, tw_osl_cur_func(), "entered");

	/* Make sure this is the only CL internal request at this time. */
	if (ctlr->internal_req_busy)
		return(TW_OSL_EBUSY);
	ctlr->internal_req_busy = TW_CL_TRUE;
	req->data = ctlr->internal_req_data;
	req->data_phys = ctlr->internal_req_data_phys;
	tw_osl_memzero(req->data, TW_CLI_SECTOR_SIZE);
	req->length = TW_CLI_SECTOR_SIZE;

	/* Build the cmd pkt. */
	cmdpkt = req->cmd_pkt;

	cmdpkt->cmd_hdr.header_desc.size_header = 128;
		
	cmd9k = &(cmdpkt->command.cmd_pkt_9k);

	cmd9k->res__opcode =
		BUILD_RES__OPCODE(0, TWA_FW_CMD_EXECUTE_SCSI);
	cmd9k->unit = 0;
	cmd9k->lun_l4__req_id = TW_CL_SWAP16(req->request_id);
	cmd9k->status = 0;
	cmd9k->sgl_offset = 16; /* offset from end of hdr = max cdb len */
	cmd9k->lun_h4__sgl_entries = TW_CL_SWAP16(1);

	if (req->ctlr->flags & TW_CL_64BIT_ADDRESSES) {
		((struct tw_cl_sg_desc64 *)(cmd9k->sg_list))[0].address =
			TW_CL_SWAP64(req->data_phys);
		((struct tw_cl_sg_desc64 *)(cmd9k->sg_list))[0].length =
			TW_CL_SWAP32(req->length);
	} else {
		((struct tw_cl_sg_desc32 *)(cmd9k->sg_list))[0].address =
			TW_CL_SWAP32(req->data_phys);
		((struct tw_cl_sg_desc32 *)(cmd9k->sg_list))[0].length =
			TW_CL_SWAP32(req->length);
	}

	cmd9k->cdb[0] = (TW_UINT8)cmd;
	cmd9k->cdb[4] = 128;

	if ((error = tw_cli_submit_cmd(req)))
		if (error != TW_OSL_EBUSY) {
			tw_cli_dbg_printf(1, ctlr->ctlr_handle,
				tw_osl_cur_func(),
				"Failed to start SCSI command",
				"request = %p, error = %d", req, error);
			return(TW_OSL_EIO);
		}
	return(TW_OSL_ESUCCESS);
}



/*
 * Function name:	tw_cli_get_aen
 * Description:		Sends down a Request Sense cmd to fw to fetch an AEN.
 *
 * Input:		ctlr	-- ptr to per ctlr structure
 * Output:		None
 * Return value:	0	-- success
 *			non-zero-- failure
 */
TW_INT32
tw_cli_get_aen(struct tw_cli_ctlr_context *ctlr)
{
	struct tw_cli_req_context	*req;
	TW_INT32			error;

	tw_cli_dbg_printf(4, ctlr->ctlr_handle, tw_osl_cur_func(), "entered");

	if ((req = tw_cli_get_request(ctlr
		)) == TW_CL_NULL)
		return(TW_OSL_EBUSY);

	req->flags |= TW_CLI_REQ_FLAGS_INTERNAL;
	req->flags |= TW_CLI_REQ_FLAGS_9K;
	req->tw_cli_callback = tw_cli_aen_callback;
	if ((error = tw_cli_send_scsi_cmd(req, 0x03 /* REQUEST_SENSE */))) {
		tw_cli_dbg_printf(1, ctlr->ctlr_handle, tw_osl_cur_func(),
			"Could not send SCSI command",
			"request = %p, error = %d", req, error);
		if (req->data)
			ctlr->internal_req_busy = TW_CL_FALSE;
		tw_cli_req_q_insert_tail(req, TW_CLI_FREE_Q);
	}
	return(error);
}



/*
 * Function name:	tw_cli_fill_sg_list
 * Description:		Fills in the scatter/gather list.
 *
 * Input:		ctlr	-- ptr to per ctlr structure
 *			sgl_src	-- ptr to fill the sg list from
 *			sgl_dest-- ptr to sg list
 *			nsegments--# of segments
 * Output:		None
 * Return value:	None
 */
TW_VOID
tw_cli_fill_sg_list(struct tw_cli_ctlr_context *ctlr, TW_VOID *sgl_src,
	TW_VOID *sgl_dest, TW_INT32 num_sgl_entries)
{
	TW_INT32	i;

	tw_cli_dbg_printf(10, ctlr->ctlr_handle, tw_osl_cur_func(), "entered");

	if (ctlr->flags & TW_CL_64BIT_ADDRESSES) {
		struct tw_cl_sg_desc64 *sgl_s =
			(struct tw_cl_sg_desc64 *)sgl_src;
		struct tw_cl_sg_desc64 *sgl_d =
			(struct tw_cl_sg_desc64 *)sgl_dest;

		tw_cli_dbg_printf(10, ctlr->ctlr_handle, tw_osl_cur_func(),
			"64 bit addresses");
		for (i = 0; i < num_sgl_entries; i++) {
			sgl_d[i].address = TW_CL_SWAP64(sgl_s->address);
			sgl_d[i].length = TW_CL_SWAP32(sgl_s->length);
			sgl_s++;
			if (ctlr->flags & TW_CL_64BIT_SG_LENGTH)
				sgl_s = (struct tw_cl_sg_desc64 *)
					(((TW_INT8 *)(sgl_s)) + 4);
		}
	} else {
		struct tw_cl_sg_desc32 *sgl_s =
			(struct tw_cl_sg_desc32 *)sgl_src;
		struct tw_cl_sg_desc32 *sgl_d =
			(struct tw_cl_sg_desc32 *)sgl_dest;

		tw_cli_dbg_printf(10, ctlr->ctlr_handle, tw_osl_cur_func(),
			"32 bit addresses");
		for (i = 0; i < num_sgl_entries; i++) {
			sgl_d[i].address = TW_CL_SWAP32(sgl_s[i].address);
			sgl_d[i].length = TW_CL_SWAP32(sgl_s[i].length);
		}
	}
}

