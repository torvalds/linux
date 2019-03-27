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
 * Common Layer interrupt handling functions.
 */


#include "tw_osl_share.h"
#include "tw_cl_share.h"
#include "tw_cl_fwif.h"
#include "tw_cl_ioctl.h"
#include "tw_cl.h"
#include "tw_cl_externs.h"
#include "tw_osl_ioctl.h"



/*
 * Function name:	twa_interrupt
 * Description:		Interrupt handler.  Determines the kind of interrupt,
 *			and returns TW_CL_TRUE if it recognizes the interrupt.
 *
 * Input:		ctlr_handle	-- controller handle
 * Output:		None
 * Return value:	TW_CL_TRUE -- interrupt recognized
 *			TW_CL_FALSE-- interrupt not recognized
 */
TW_INT32
tw_cl_interrupt(struct tw_cl_ctlr_handle *ctlr_handle)
{
	struct tw_cli_ctlr_context	*ctlr =
		(struct tw_cli_ctlr_context *)(ctlr_handle->cl_ctlr_ctxt);
	TW_UINT32			status_reg;
	TW_INT32			rc = TW_CL_FALSE;

	tw_cli_dbg_printf(10, ctlr_handle, tw_osl_cur_func(), "entered");

	/* If we don't have controller context, bail */
	if (ctlr == NULL)
		goto out;

	/*
	 * Bail If we get an interrupt while resetting, or shutting down.
	 */
	if (ctlr->reset_in_progress || !(ctlr->active))
		goto out;

	/* Read the status register to determine the type of interrupt. */
	status_reg = TW_CLI_READ_STATUS_REGISTER(ctlr_handle);
	if (tw_cli_check_ctlr_state(ctlr, status_reg))
		goto out;

	/* Clear the interrupt. */
	if (status_reg & TWA_STATUS_HOST_INTERRUPT) {
		tw_cli_dbg_printf(6, ctlr_handle, tw_osl_cur_func(),
			"Host interrupt");
		TW_CLI_WRITE_CONTROL_REGISTER(ctlr_handle,
			TWA_CONTROL_CLEAR_HOST_INTERRUPT);
	}
	if (status_reg & TWA_STATUS_ATTENTION_INTERRUPT) {
		tw_cli_dbg_printf(6, ctlr_handle, tw_osl_cur_func(),
			"Attention interrupt");
		rc |= TW_CL_TRUE; /* request for a deferred isr call */
		tw_cli_process_attn_intr(ctlr);
		TW_CLI_WRITE_CONTROL_REGISTER(ctlr_handle,
			TWA_CONTROL_CLEAR_ATTENTION_INTERRUPT);
	}
	if (status_reg & TWA_STATUS_COMMAND_INTERRUPT) {
		tw_cli_dbg_printf(6, ctlr_handle, tw_osl_cur_func(),
			"Command interrupt");
		rc |= TW_CL_TRUE; /* request for a deferred isr call */
		tw_cli_process_cmd_intr(ctlr);
		if ((TW_CL_Q_FIRST_ITEM(&(ctlr->req_q_head[TW_CLI_PENDING_Q]))) == TW_CL_NULL)
			TW_CLI_WRITE_CONTROL_REGISTER(ctlr_handle,
				TWA_CONTROL_MASK_COMMAND_INTERRUPT);
	}
	if (status_reg & TWA_STATUS_RESPONSE_INTERRUPT) {
		tw_cli_dbg_printf(10, ctlr_handle, tw_osl_cur_func(),
			"Response interrupt");
		rc |= TW_CL_TRUE; /* request for a deferred isr call */
		tw_cli_process_resp_intr(ctlr);
	}
out:
	return(rc);
}



/*
 * Function name:	tw_cli_process_host_intr
 * Description:		This function gets called if we triggered an interrupt.
 *			We don't use it as of now.
 *
 * Input:		ctlr	-- ptr to CL internal ctlr context
 * Output:		None
 * Return value:	None
 */
TW_VOID
tw_cli_process_host_intr(struct tw_cli_ctlr_context *ctlr)
{
	tw_cli_dbg_printf(6, ctlr->ctlr_handle, tw_osl_cur_func(), "entered");
}



/*
 * Function name:	tw_cli_process_attn_intr
 * Description:		This function gets called if the fw posted an AEN
 *			(Asynchronous Event Notification).  It fetches
 *			all the AEN's that the fw might have posted.
 *
 * Input:		ctlr	-- ptr to CL internal ctlr context
 * Output:		None
 * Return value:	None
 */
TW_VOID
tw_cli_process_attn_intr(struct tw_cli_ctlr_context *ctlr)
{
	TW_INT32	error;

	tw_cli_dbg_printf(6, ctlr->ctlr_handle, tw_osl_cur_func(), "entered");

	if ((error = tw_cli_get_aen(ctlr))) {
		/*
		 * If the driver is already in the process of retrieveing AEN's,
		 * we will be returned TW_OSL_EBUSY.  In this case,
		 * tw_cli_param_callback or tw_cli_aen_callback will eventually
		 * retrieve the AEN this attention interrupt is for.  So, we
		 * don't need to print the failure.
		 */ 
		if (error != TW_OSL_EBUSY)
			tw_cl_create_event(ctlr->ctlr_handle, TW_CL_FALSE,
				TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
				0x1200, 0x1, TW_CL_SEVERITY_ERROR_STRING,
				"Failed to fetch AEN",
				"error = %d", error);
	}
}



/*
 * Function name:	tw_cli_process_cmd_intr
 * Description:		This function gets called if we hit a queue full
 *			condition earlier, and the fw is now ready for
 *			new cmds.  Submits any pending requests.
 *
 * Input:		ctlr	-- ptr to CL internal ctlr context
 * Output:		None
 * Return value:	None
 */
TW_VOID
tw_cli_process_cmd_intr(struct tw_cli_ctlr_context *ctlr)
{
	tw_cli_dbg_printf(6, ctlr->ctlr_handle, tw_osl_cur_func(), "entered");

	/* Start any requests that might be in the pending queue. */
	tw_cli_submit_pending_queue(ctlr);

	/*
	 * If tw_cli_submit_pending_queue was unsuccessful due to a "cmd queue
	 * full" condition, cmd_intr will already have been unmasked by
	 * tw_cli_submit_cmd.  We don't need to do it again... simply return.
	 */
}



/*
 * Function name:	tw_cli_process_resp_intr
 * Description:		Looks for cmd completions from fw; queues cmds completed
 *			by fw into complete queue.
 *
 * Input:		ctlr	-- ptr to CL internal ctlr context
 * Output:		None
 * Return value:	0	-- no ctlr error
 *			non-zero-- ctlr error
 */
TW_INT32
tw_cli_process_resp_intr(struct tw_cli_ctlr_context *ctlr)
{
	TW_UINT32			resp;
	struct tw_cli_req_context	*req;
	TW_INT32			error;
	TW_UINT32			status_reg;
    
	tw_cli_dbg_printf(10, ctlr->ctlr_handle, tw_osl_cur_func(), "entered");

	for (;;) {
		status_reg = TW_CLI_READ_STATUS_REGISTER(ctlr->ctlr_handle);
		if ((error = tw_cli_check_ctlr_state(ctlr, status_reg)))
			break;
		if (status_reg & TWA_STATUS_RESPONSE_QUEUE_EMPTY) {
			tw_cli_dbg_printf(7, ctlr->ctlr_handle,
				tw_osl_cur_func(), "Response queue empty");
			break;
		}

		/* Response queue is not empty. */
		resp = TW_CLI_READ_RESPONSE_QUEUE(ctlr->ctlr_handle);
		{
			req = &(ctlr->req_ctxt_buf[GET_RESP_ID(resp)]);
		}

		if (req->state != TW_CLI_REQ_STATE_BUSY) {
			tw_cl_create_event(ctlr->ctlr_handle, TW_CL_FALSE,
				TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
				0x1201, 0x1, TW_CL_SEVERITY_ERROR_STRING,
				"Unposted command completed!!",
				"request = %p, status = %d",
				req, req->state);
#ifdef TW_OSL_DEBUG
			tw_cl_print_ctlr_stats(ctlr->ctlr_handle);
#endif /* TW_OSL_DEBUG */
			continue;
		}

		/*
		 * Remove the request from the busy queue, mark it as complete,
		 * and enqueue it in the complete queue.
		 */
		tw_cli_req_q_remove_item(req, TW_CLI_BUSY_Q);
		req->state = TW_CLI_REQ_STATE_COMPLETE;
		tw_cli_req_q_insert_tail(req, TW_CLI_COMPLETE_Q);

	}

	/* Complete this, and other requests in the complete queue. */
	tw_cli_process_complete_queue(ctlr);
	
	return(error);
}



/*
 * Function name:	tw_cli_submit_pending_queue
 * Description:		Kick starts any requests in the pending queue.
 *
 * Input:		ctlr	-- ptr to CL internal ctlr context
 * Output:		None
 * Return value:	0	-- all pending requests submitted successfully
 *			non-zero-- otherwise
 */
TW_INT32
tw_cli_submit_pending_queue(struct tw_cli_ctlr_context *ctlr)
{
	struct tw_cli_req_context	*req;
	TW_INT32			error = TW_OSL_ESUCCESS;
    
	tw_cli_dbg_printf(3, ctlr->ctlr_handle, tw_osl_cur_func(), "entered");
	
	/*
	 * Pull requests off the pending queue, and submit them.
	 */
	while ((req = tw_cli_req_q_remove_head(ctlr, TW_CLI_PENDING_Q)) !=
		TW_CL_NULL) {
		if ((error = tw_cli_submit_cmd(req))) {
			if (error == TW_OSL_EBUSY) {
				tw_cli_dbg_printf(2, ctlr->ctlr_handle,
					tw_osl_cur_func(),
					"Requeueing pending request");
				req->state = TW_CLI_REQ_STATE_PENDING;
				/*
				 * Queue the request at the head of the pending
				 * queue, and break away, so we don't try to
				 * submit any more requests.
				 */
				tw_cli_req_q_insert_head(req, TW_CLI_PENDING_Q);
				break;
			} else {
				tw_cl_create_event(ctlr->ctlr_handle,
					TW_CL_FALSE,
					TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
					0x1202, 0x1,
					TW_CL_SEVERITY_ERROR_STRING,
					"Could not start request "
					"in pending queue",
					"request = %p, opcode = 0x%x, "
					"error = %d", req,
					GET_OPCODE(req->cmd_pkt->
						command.cmd_pkt_9k.res__opcode),
					error);
				/*
				 * Set the appropriate error and call the CL
				 * internal callback if there's one.  If the
				 * request originator is polling for completion,
				 * he should be checking req->error to
				 * determine that the request did not go
				 * through.  The request originators are
				 * responsible for the clean-up.
				 */
				req->error_code = error;
				req->state = TW_CLI_REQ_STATE_COMPLETE;
				if (req->tw_cli_callback)
					req->tw_cli_callback(req);
				error = TW_OSL_ESUCCESS;
			}
		}
	}
	return(error);
}



/*
 * Function name:	tw_cli_process_complete_queue
 * Description:		Calls the CL internal callback routine, if any, for
 *			each request in the complete queue.
 *
 * Input:		ctlr	-- ptr to CL internal ctlr context
 * Output:		None
 * Return value:	None
 */
TW_VOID
tw_cli_process_complete_queue(struct tw_cli_ctlr_context *ctlr)
{
	struct tw_cli_req_context	*req;
    
	tw_cli_dbg_printf(10, ctlr->ctlr_handle, tw_osl_cur_func(), "entered");

	/*
	 * Pull commands off the completed list, dispatch them appropriately.
	 */
	while ((req = tw_cli_req_q_remove_head(ctlr, TW_CLI_COMPLETE_Q)) !=
		TW_CL_NULL) {
		/* Call the CL internal callback, if there's one. */
		if (req->tw_cli_callback)
			req->tw_cli_callback(req);
	}
}



/*
 * Function name:	tw_cli_complete_io
 * Description:		CL internal callback for SCSI/fw passthru requests.
 *
 * Input:		req	-- ptr to CL internal request context
 * Output:		None
 * Return value:	None
 */
TW_VOID
tw_cli_complete_io(struct tw_cli_req_context *req)
{
	struct tw_cli_ctlr_context	*ctlr = req->ctlr;
	struct tw_cl_req_packet		*req_pkt =
		(struct tw_cl_req_packet *)(req->orig_req);

	tw_cli_dbg_printf(8, ctlr->ctlr_handle, tw_osl_cur_func(), "entered");

	req_pkt->status = TW_CL_ERR_REQ_SUCCESS;
	if (req->error_code) {
		req_pkt->status = TW_CL_ERR_REQ_UNABLE_TO_SUBMIT_COMMAND;
		goto out;
	}

	if (req->state != TW_CLI_REQ_STATE_COMPLETE) {
		tw_cl_create_event(ctlr->ctlr_handle, TW_CL_FALSE,
			TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
			0x1203, 0x1, TW_CL_SEVERITY_ERROR_STRING,
			"I/O completion on incomplete command!!",
			"request = %p, status = %d",
			req, req->state);
#ifdef TW_OSL_DEBUG
		tw_cl_print_ctlr_stats(ctlr->ctlr_handle);
#endif /* TW_OSL_DEBUG */
		return;
	}

	if (req->flags & TW_CLI_REQ_FLAGS_PASSTHRU) {
		/* Copy the command packet back into OSL's space. */
		tw_osl_memcpy(req_pkt->gen_req_pkt.pt_req.cmd_pkt, req->cmd_pkt,
			sizeof(struct tw_cl_command_packet));
	} else
		tw_cli_scsi_complete(req);

out:
	req_pkt->tw_osl_callback(req->req_handle);
	tw_cli_req_q_insert_tail(req, TW_CLI_FREE_Q);
}



/*
 * Function name:	tw_cli_scsi_complete
 * Description:		Completion routine for SCSI requests.
 *
 * Input:		req	-- ptr to CL internal request context
 * Output:		None
 * Return value:	None
 */
TW_VOID
tw_cli_scsi_complete(struct tw_cli_req_context *req)
{
	struct tw_cl_req_packet		*req_pkt =
		(struct tw_cl_req_packet *)(req->orig_req);
	struct tw_cl_scsi_req_packet	*scsi_req =
		&(req_pkt->gen_req_pkt.scsi_req);
	struct tw_cl_command_9k		*cmd =
		&(req->cmd_pkt->command.cmd_pkt_9k);
	struct tw_cl_command_header	*cmd_hdr;
	TW_UINT16			error;
	TW_UINT8			*cdb;

	tw_cli_dbg_printf(8, req->ctlr->ctlr_handle, tw_osl_cur_func(),
		"entered");

	scsi_req->scsi_status = cmd->status;
	if (! cmd->status)
		return;

	tw_cli_dbg_printf(1, req->ctlr->ctlr_handle, tw_osl_cur_func(),
		"req_id = 0x%x, status = 0x%x",
		GET_REQ_ID(cmd->lun_l4__req_id), cmd->status);

	cmd_hdr = &(req->cmd_pkt->cmd_hdr);
	error = cmd_hdr->status_block.error;
	if ((error == TWA_ERROR_LOGICAL_UNIT_NOT_SUPPORTED) ||
			(error == TWA_ERROR_UNIT_OFFLINE)) {
		if (GET_LUN_L4(cmd->lun_l4__req_id))
			req_pkt->status |= TW_CL_ERR_REQ_INVALID_LUN;
		else
			req_pkt->status |= TW_CL_ERR_REQ_INVALID_TARGET;
	} else {
		tw_cli_dbg_printf(2, req->ctlr->ctlr_handle,
			tw_osl_cur_func(),
			"cmd = %x %x %x %x %x %x %x",
			GET_OPCODE(cmd->res__opcode),
			GET_SGL_OFF(cmd->res__opcode),
			cmd->unit,
			cmd->lun_l4__req_id,
			cmd->status,
			cmd->sgl_offset,
			cmd->lun_h4__sgl_entries);

		cdb = (TW_UINT8 *)(cmd->cdb);
		tw_cli_dbg_printf(2, req->ctlr->ctlr_handle,
			tw_osl_cur_func(),
			"cdb = %x %x %x %x %x %x %x %x "
			"%x %x %x %x %x %x %x %x",
			cdb[0], cdb[1], cdb[2], cdb[3],
			cdb[4], cdb[5], cdb[6], cdb[7],
			cdb[8], cdb[9], cdb[10], cdb[11],
			cdb[12], cdb[13], cdb[14], cdb[15]);

#if       0
		/* 
		 * Print the error. Firmware doesn't yet support
		 * the 'Mode Sense' cmd.  Don't print if the cmd
		 * is 'Mode Sense', and the error is 'Invalid field
		 * in CDB'.
		 */
		if (! ((cdb[0] == 0x1A) && (error == 0x10D)))
			tw_cli_create_ctlr_event(req->ctlr,
				TW_CL_MESSAGE_SOURCE_CONTROLLER_ERROR,
				cmd_hdr);
#endif // 0
	}

	if (scsi_req->sense_data) {
		tw_osl_memcpy(scsi_req->sense_data, cmd_hdr->sense_data,
			TWA_SENSE_DATA_LENGTH);
		scsi_req->sense_len = TWA_SENSE_DATA_LENGTH;
		req_pkt->status |= TW_CL_ERR_REQ_AUTO_SENSE_VALID;
	}
	req_pkt->status |= TW_CL_ERR_REQ_SCSI_ERROR;
}



/*
 * Function name:	tw_cli_param_callback
 * Description:		Callback for get/set_param requests.
 *
 * Input:		req	-- ptr to completed request pkt
 * Output:		None
 * Return value:	None
 */
TW_VOID
tw_cli_param_callback(struct tw_cli_req_context *req)
{
	struct tw_cli_ctlr_context	*ctlr = req->ctlr;
	union tw_cl_command_7k		*cmd =
		&(req->cmd_pkt->command.cmd_pkt_7k);
	TW_INT32			error;

	tw_cli_dbg_printf(4, ctlr->ctlr_handle, tw_osl_cur_func(), "entered");

	/*
	 * If the request was never submitted to the controller, the function
	 * that sets req->error is responsible for calling tw_cl_create_event.
	 */
	if (! req->error_code)
		if (cmd->param.status) {
#if       0
			tw_cli_create_ctlr_event(ctlr,
				TW_CL_MESSAGE_SOURCE_CONTROLLER_ERROR,
				&(req->cmd_pkt->cmd_hdr));
#endif // 0
			tw_cl_create_event(ctlr->ctlr_handle, TW_CL_FALSE,
				TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
				0x1204, 0x1, TW_CL_SEVERITY_ERROR_STRING,
				"get/set_param failed",
				"status = %d", cmd->param.status);
		}

	ctlr->internal_req_busy = TW_CL_FALSE;
	tw_cli_req_q_insert_tail(req, TW_CLI_FREE_Q);

	if ((ctlr->get_more_aens) && (!(ctlr->reset_in_progress))) {
		ctlr->get_more_aens = TW_CL_FALSE;
		tw_cli_dbg_printf(4, ctlr->ctlr_handle, tw_osl_cur_func(),
			"Fetching more AEN's");
		if ((error = tw_cli_get_aen(ctlr)))
			tw_cl_create_event(ctlr->ctlr_handle, TW_CL_FALSE,
				TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
				0x1205, 0x1, TW_CL_SEVERITY_ERROR_STRING,
				"Failed to fetch all AEN's from param_callback",
				"error = %d", error);
	}
}



/*
 * Function name:	tw_cli_aen_callback
 * Description:		Callback for requests to fetch AEN's.
 *
 * Input:		req	-- ptr to completed request pkt
 * Output:		None
 * Return value:	None
 */
TW_VOID
tw_cli_aen_callback(struct tw_cli_req_context *req)
{
	struct tw_cli_ctlr_context	*ctlr = req->ctlr;
	struct tw_cl_command_header	*cmd_hdr;
	struct tw_cl_command_9k		*cmd =
		&(req->cmd_pkt->command.cmd_pkt_9k);
	TW_UINT16			aen_code = TWA_AEN_QUEUE_EMPTY;
	TW_INT32			error;

	tw_cli_dbg_printf(4, ctlr->ctlr_handle, tw_osl_cur_func(), "entered");

	tw_cli_dbg_printf(4, ctlr->ctlr_handle, tw_osl_cur_func(),
		"req_id = 0x%x, req error = %d, status = 0x%x",
		GET_REQ_ID(cmd->lun_l4__req_id), req->error_code, cmd->status);

	/*
	 * If the request was never submitted to the controller, the function
	 * that sets error is responsible for calling tw_cl_create_event.
	 */
	if (!(error = req->error_code))
		if ((error = cmd->status)) {
			cmd_hdr = (struct tw_cl_command_header *)
				(&(req->cmd_pkt->cmd_hdr));
#if       0
			tw_cli_create_ctlr_event(ctlr,
				TW_CL_MESSAGE_SOURCE_CONTROLLER_ERROR,
				cmd_hdr);
#endif // 0
			tw_cl_create_event(ctlr->ctlr_handle, TW_CL_FALSE,
				TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
				0x1206, 0x1, TW_CL_SEVERITY_ERROR_STRING,
				"Request Sense failed",
				"opcode = 0x%x, status = %d",
				GET_OPCODE(cmd->res__opcode), cmd->status);
		}

	if (error) {
		ctlr->internal_req_busy = TW_CL_FALSE;
		tw_cli_req_q_insert_tail(req, TW_CLI_FREE_Q);
		return;
	}

	tw_cli_dbg_printf(4, ctlr->ctlr_handle, tw_osl_cur_func(),
		"Request Sense command succeeded");

	aen_code = tw_cli_manage_aen(ctlr, req);

	if (aen_code != TWA_AEN_SYNC_TIME_WITH_HOST) {
		ctlr->internal_req_busy = TW_CL_FALSE;
		tw_cli_req_q_insert_tail(req, TW_CLI_FREE_Q);
		if (aen_code != TWA_AEN_QUEUE_EMPTY)
			if ((error = tw_cli_get_aen(ctlr)))
				tw_cl_create_event(ctlr->ctlr_handle,
					TW_CL_FALSE,
					TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
					0x1207, 0x1,
					TW_CL_SEVERITY_ERROR_STRING,
					"Failed to fetch all AEN's",
					"error = %d", error);
	}
}



/*
 * Function name:	tw_cli_manage_aen
 * Description:		Handles AEN's.
 *
 * Input:		ctlr	-- ptr to CL internal ctlr context
 *			req	-- ptr to CL internal request context
 * Output:		None
 * Return value:	None
 */
TW_UINT16
tw_cli_manage_aen(struct tw_cli_ctlr_context *ctlr,
	struct tw_cli_req_context *req)
{
	struct tw_cl_command_header	*cmd_hdr;
	TW_UINT16			aen_code;
	TW_TIME				local_time;
	TW_TIME				sync_time;
	TW_UINT32			error;

	tw_cli_dbg_printf(4, ctlr->ctlr_handle, tw_osl_cur_func(), "entered");

	cmd_hdr = (struct tw_cl_command_header *)(req->data);
	aen_code = cmd_hdr->status_block.error;

	switch (aen_code) {
	case TWA_AEN_SYNC_TIME_WITH_HOST:
		tw_cli_dbg_printf(4, ctlr->ctlr_handle, tw_osl_cur_func(),
			"Received AEN_SYNC_TIME");
		/*
		 * Free the internal req pkt right here, since
		 * tw_cli_set_param will need it.
		 */
		ctlr->internal_req_busy = TW_CL_FALSE;
		tw_cli_req_q_insert_tail(req, TW_CLI_FREE_Q);

		/*
		 * We will use a callback in tw_cli_set_param only when
		 * interrupts are enabled and we can expect our callback
		 * to get called.  Setting the get_more_aens
		 * flag will make the callback continue to try to retrieve
		 * more AEN's.
		 */
		if (ctlr->interrupts_enabled)
			ctlr->get_more_aens = TW_CL_TRUE;
		/* Calculate time (in seconds) since last Sunday 12.00 AM. */
		local_time = tw_osl_get_local_time();
		sync_time = (local_time - (3 * 86400)) % 604800;
		if ((error = tw_cli_set_param(ctlr, TWA_PARAM_TIME_TABLE,
				TWA_PARAM_TIME_SCHED_TIME, 4,
				&sync_time,
				(ctlr->interrupts_enabled)
				? tw_cli_param_callback : TW_CL_NULL)))
			tw_cl_create_event(ctlr->ctlr_handle, TW_CL_FALSE,
				TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
				0x1208, 0x1, TW_CL_SEVERITY_ERROR_STRING,
				"Unable to sync time with ctlr",
				"error = %d", error);

		break;


	case TWA_AEN_QUEUE_EMPTY:
		tw_cli_dbg_printf(4, ctlr->ctlr_handle, tw_osl_cur_func(),
			"AEN queue empty");
		break;


	default:
		/* Queue the event. */

		tw_cli_dbg_printf(4, ctlr->ctlr_handle, tw_osl_cur_func(),
			"Queueing AEN");
		tw_cli_create_ctlr_event(ctlr,
			TW_CL_MESSAGE_SOURCE_CONTROLLER_EVENT,
			cmd_hdr);
		break;
	} /* switch */
	return(aen_code);
}



/*
 * Function name:	tw_cli_enable_interrupts
 * Description:		Enables interrupts on the controller
 *
 * Input:		ctlr	-- ptr to CL internal ctlr context
 * Output:		None
 * Return value:	None
 */
TW_VOID
tw_cli_enable_interrupts(struct tw_cli_ctlr_context *ctlr)
{
	tw_cli_dbg_printf(3, ctlr->ctlr_handle, tw_osl_cur_func(), "entered");

	ctlr->interrupts_enabled = TW_CL_TRUE;
	TW_CLI_WRITE_CONTROL_REGISTER(ctlr->ctlr_handle,
		TWA_CONTROL_CLEAR_ATTENTION_INTERRUPT |
		TWA_CONTROL_UNMASK_RESPONSE_INTERRUPT |
		TWA_CONTROL_ENABLE_INTERRUPTS);
}



/*
 * Function name:	twa_setup
 * Description:		Disables interrupts on the controller
 *
 * Input:		ctlr	-- ptr to CL internal ctlr context
 * Output:		None
 * Return value:	None
 */
TW_VOID
tw_cli_disable_interrupts(struct tw_cli_ctlr_context *ctlr)
{
	tw_cli_dbg_printf(3, ctlr->ctlr_handle, tw_osl_cur_func(), "entered");

	TW_CLI_WRITE_CONTROL_REGISTER(ctlr->ctlr_handle,
		TWA_CONTROL_DISABLE_INTERRUPTS);
	ctlr->interrupts_enabled = TW_CL_FALSE;
}

