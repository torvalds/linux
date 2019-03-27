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



#ifndef TW_CL_EXTERNS_H

#define TW_CL_EXTERNS_H


/*
 * Data structures and functions global to the Common Layer.
 */


extern TW_INT8			tw_cli_fw_img[];
extern TW_INT32			tw_cli_fw_img_size;
extern TW_INT8			*tw_cli_severity_string_table[];


/* Do controller initialization. */
extern TW_INT32	tw_cli_start_ctlr(struct tw_cli_ctlr_context *ctlr);

/* Establish a logical connection with the firmware on the controller. */
extern TW_INT32	tw_cli_init_connection(struct tw_cli_ctlr_context *ctlr,
	TW_UINT16 message_credits, TW_UINT32 set_features,
	TW_UINT16 current_fw_srl, TW_UINT16 current_fw_arch_id,
	TW_UINT16 current_fw_branch, TW_UINT16 current_fw_build,
	TW_UINT16 *fw_on_ctlr_srl, TW_UINT16 *fw_on_ctlr_arch_id,
	TW_UINT16 *fw_on_ctlr_branch, TW_UINT16 *fw_on_ctlr_build,
	TW_UINT32 *init_connect_result);



/* Functions in tw_cl_io.c */

/* Submit a command packet to the firmware on the controller. */
extern TW_INT32	tw_cli_submit_cmd(struct tw_cli_req_context *req);

/* Get a firmware parameter. */
extern TW_INT32	tw_cli_get_param(struct tw_cli_ctlr_context *ctlr,
	TW_INT32 table_id, TW_INT32 parameter_id, TW_VOID *param_data,
	TW_INT32 size, TW_VOID (* callback)(struct tw_cli_req_context *req));

/* Set a firmware parameter. */
extern TW_INT32	tw_cli_set_param(struct tw_cli_ctlr_context *ctlr,
	TW_INT32 table_id, TW_INT32 param_id, TW_INT32 param_size,
	TW_VOID *data, TW_VOID (* callback)(struct tw_cli_req_context *req));

/* Submit a command to the firmware and poll for completion. */
extern TW_INT32	tw_cli_submit_and_poll_request(struct tw_cli_req_context *req,
	TW_UINT32 timeout);

/* Soft reset the controller. */
extern TW_INT32	tw_cli_soft_reset(struct tw_cli_ctlr_context *ctlr);
extern int twa_setup_intr(struct twa_softc *sc);
extern int twa_teardown_intr(struct twa_softc *sc);

/* Send down a SCSI command to the firmware (usually, an internal Req Sense. */
extern TW_INT32	tw_cli_send_scsi_cmd(struct tw_cli_req_context *req,
	TW_INT32 cmd);

/* Get an AEN from the firmware (by sending down a Req Sense). */
extern TW_INT32	tw_cli_get_aen(struct tw_cli_ctlr_context *ctlr);

/* Fill in the scatter/gather list. */
extern TW_VOID tw_cli_fill_sg_list(struct tw_cli_ctlr_context *ctlr,
	TW_VOID *sgl_src, TW_VOID *sgl_dest, TW_INT32 num_sgl_entries);



/* Functions in tw_cl_intr.c */

/* Process a host interrupt. */
extern TW_VOID	tw_cli_process_host_intr(struct tw_cli_ctlr_context *ctlr);

/* Process an attention interrupt. */
extern TW_VOID	tw_cli_process_attn_intr(struct tw_cli_ctlr_context *ctlr);

/* Process a command interrupt. */
extern TW_VOID	tw_cli_process_cmd_intr(struct tw_cli_ctlr_context *ctlr);

/* Process a response interrupt from the controller. */
extern TW_INT32	tw_cli_process_resp_intr(struct tw_cli_ctlr_context *ctlr);

/* Submit any requests in the pending queue to the firmware. */
extern TW_INT32	tw_cli_submit_pending_queue(struct tw_cli_ctlr_context *ctlr);

/* Process all requests in the complete queue. */
extern TW_VOID	tw_cli_process_complete_queue(struct tw_cli_ctlr_context *ctlr);

/* CL internal callback for SCSI/fw passthru requests. */
extern TW_VOID	tw_cli_complete_io(struct tw_cli_req_context *req);

/* Completion routine for SCSI requests. */
extern TW_VOID	tw_cli_scsi_complete(struct tw_cli_req_context *req);

/* Callback for get/set param requests. */
extern TW_VOID	tw_cli_param_callback(struct tw_cli_req_context *req);

/* Callback for Req Sense commands to get AEN's. */
extern TW_VOID	tw_cli_aen_callback(struct tw_cli_req_context *req);

/* Decide what to do with a retrieved AEN. */
extern TW_UINT16	tw_cli_manage_aen(struct tw_cli_ctlr_context *ctlr,
	struct tw_cli_req_context *req);

/* Enable controller interrupts. */
extern TW_VOID
	tw_cli_enable_interrupts(struct tw_cli_ctlr_context *ctlr_handle);

/* Disable controller interrupts. */
extern TW_VOID
	tw_cli_disable_interrupts(struct tw_cli_ctlr_context *ctlr_handle);



/* Functions in tw_cl_misc.c */

/* Print if dbg_level is appropriate (by calling OS Layer). */
extern TW_VOID	tw_cli_dbg_printf(TW_UINT8 dbg_level,
	struct tw_cl_ctlr_handle *ctlr_handle, const TW_INT8 *cur_func,
	TW_INT8 *fmt, ...);

/* Describe meaning of each set bit in the given register. */
extern TW_INT8	*tw_cli_describe_bits(TW_UINT32 reg, TW_INT8 *str);

/* Complete all requests in the complete queue with a RESET status. */
extern TW_VOID	tw_cli_drain_complete_queue(struct tw_cli_ctlr_context *ctlr);

/* Complete all requests in the busy queue with a RESET status. */
extern TW_VOID	tw_cli_drain_busy_queue(struct tw_cli_ctlr_context *ctlr);

/* Complete all requests in the pending queue with a RESET status. */
extern TW_VOID	tw_cli_drain_pending_queue(struct tw_cli_ctlr_context *ctlr);

/* Drain the controller response queue. */
extern TW_INT32	tw_cli_drain_response_queue(struct tw_cli_ctlr_context *ctlr);

/* Find a particular response in the controller response queue. */
extern TW_INT32	tw_cli_find_response(struct tw_cli_ctlr_context *ctlr,
	TW_INT32 req_id);

/* Drain the controller AEN queue. */
extern TW_INT32	tw_cli_drain_aen_queue(struct tw_cli_ctlr_context *ctlr);

/* Determine if a given AEN has been posted by the firmware. */
extern TW_INT32	tw_cli_find_aen(struct tw_cli_ctlr_context *ctlr,
	TW_UINT16 aen_code);

/* Poll for a given status to show up in the firmware status register. */
extern TW_INT32	tw_cli_poll_status(struct tw_cli_ctlr_context *ctlr,
	TW_UINT32 status, TW_UINT32 timeout);

/* Get a free CL internal request context packet. */
extern struct tw_cli_req_context *
	tw_cli_get_request(struct tw_cli_ctlr_context *ctlr
	);

/* Notify OSL of controller info (fw/BIOS versions, etc.). */
extern TW_VOID	tw_cli_notify_ctlr_info(struct tw_cli_ctlr_context *ctlr);

/* Make sure that the firmware status register reports a proper status. */
extern TW_INT32	tw_cli_check_ctlr_state(struct tw_cli_ctlr_context *ctlr,
	TW_UINT32 status_reg);



#endif /* TW_CL_EXTERNS_H */
