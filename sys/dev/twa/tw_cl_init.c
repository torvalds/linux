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
 * Common Layer initialization functions.
 */


#include "tw_osl_share.h"
#include "tw_cl_share.h"
#include "tw_cl_fwif.h"
#include "tw_cl_ioctl.h"
#include "tw_cl.h"
#include "tw_cl_externs.h"
#include "tw_osl_ioctl.h"


/*
 * Function name:	tw_cl_ctlr_supported
 * Description:		Determines if a controller is supported.
 *
 * Input:		vendor_id -- vendor id of the controller
 *			device_id -- device id of the controller
 * Output:		None
 * Return value:	TW_CL_TRUE-- controller supported
 *			TW_CL_FALSE-- controller not supported
 */
TW_INT32
tw_cl_ctlr_supported(TW_INT32 vendor_id, TW_INT32 device_id)
{
	if ((vendor_id == TW_CL_VENDOR_ID) &&
		((device_id == TW_CL_DEVICE_ID_9K) ||
		 (device_id == TW_CL_DEVICE_ID_9K_X) ||
		 (device_id == TW_CL_DEVICE_ID_9K_E) ||
		 (device_id == TW_CL_DEVICE_ID_9K_SA)))
		return(TW_CL_TRUE);
	return(TW_CL_FALSE);
}



/*
 * Function name:	tw_cl_get_pci_bar_info
 * Description:		Returns PCI BAR info.
 *
 * Input:		device_id -- device id of the controller
 *			bar_type -- type of PCI BAR in question
 * Output:		bar_num -- PCI BAR number corresponding to bar_type
 *			bar0_offset -- byte offset from BAR 0 (0x10 in
 *					PCI config space)
 *			bar_size -- size, in bytes, of the BAR in question
 * Return value:	0 -- success
 *			non-zero -- failure
 */
TW_INT32
tw_cl_get_pci_bar_info(TW_INT32 device_id, TW_INT32 bar_type,
	TW_INT32 *bar_num, TW_INT32 *bar0_offset, TW_INT32 *bar_size)
{
	TW_INT32	error = TW_OSL_ESUCCESS;

	switch(device_id) {
	case TW_CL_DEVICE_ID_9K:
		switch(bar_type) {
		case TW_CL_BAR_TYPE_IO:
			*bar_num = 0;
			*bar0_offset = 0;
			*bar_size = 4;
			break;

		case TW_CL_BAR_TYPE_MEM:
			*bar_num = 1;
			*bar0_offset = 0x4;
			*bar_size = 8;
			break;

		case TW_CL_BAR_TYPE_SBUF:
			*bar_num = 2;
			*bar0_offset = 0xC;
			*bar_size = 8;
			break;
		}
		break;

	case TW_CL_DEVICE_ID_9K_X:
	case TW_CL_DEVICE_ID_9K_E:
	case TW_CL_DEVICE_ID_9K_SA:
		switch(bar_type) {
		case TW_CL_BAR_TYPE_IO:
			*bar_num = 2;
			*bar0_offset = 0x10;
			*bar_size = 4;
			break;

		case TW_CL_BAR_TYPE_MEM:
			*bar_num = 1;
			*bar0_offset = 0x8;
			*bar_size = 8;
			break;

		case TW_CL_BAR_TYPE_SBUF:
			*bar_num = 0;
			*bar0_offset = 0;
			*bar_size = 8;
			break;
		}
		break;

	default:
		error = TW_OSL_ENOTTY;
		break;
	}

	return(error);
}



/*
 * Function name:	tw_cl_get_mem_requirements
 * Description:		Provides info about Common Layer requirements for a
 *			controller, given the controller type (in 'flags').
 * Input:		ctlr_handle -- controller handle
 *			flags -- more info passed by the OS Layer
 *			device_id -- device id of the controller
 *			max_simult_reqs -- maximum # of simultaneous
 *					requests that the OS Layer expects
 *					the Common Layer to support
 *			max_aens -- maximun # of AEN's needed to be supported
 * Output:		alignment -- alignment needed for all DMA'able
 *					buffers
 *			sg_size_factor -- every SG element should have a size
 *					that's a multiple of this number
 *			non_dma_mem_size -- # of bytes of memory needed for
 *					non-DMA purposes
 *			dma_mem_size -- # of bytes of DMA'able memory needed
 *			per_req_dma_mem_size -- # of bytes of DMA'able memory
 *					needed per request, if applicable
 *			per_req_non_dma_mem_size -- # of bytes of memory needed
 *					per request for non-DMA purposes,
 *					if applicable
 * Output:		None
 * Return value:	0	-- success
 *			non-zero-- failure
 */
TW_INT32
tw_cl_get_mem_requirements(struct tw_cl_ctlr_handle *ctlr_handle,
	TW_UINT32 flags, TW_INT32 device_id, TW_INT32 max_simult_reqs,
	TW_INT32 max_aens, TW_UINT32 *alignment, TW_UINT32 *sg_size_factor,
	TW_UINT32 *non_dma_mem_size, TW_UINT32 *dma_mem_size
	)
{
	if (device_id == 0)
		device_id = TW_CL_DEVICE_ID_9K;

	if (max_simult_reqs > TW_CL_MAX_SIMULTANEOUS_REQUESTS) {
		tw_cl_create_event(ctlr_handle, TW_CL_FALSE,
			TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
			0x1000, 0x1, TW_CL_SEVERITY_ERROR_STRING,
			"Too many simultaneous requests to support!",
			"requested = %d, supported = %d, error = %d\n",
			max_simult_reqs, TW_CL_MAX_SIMULTANEOUS_REQUESTS,
			TW_OSL_EBIG);
		return(TW_OSL_EBIG);
	}

	*alignment = TWA_ALIGNMENT(device_id);
	*sg_size_factor = TWA_SG_ELEMENT_SIZE_FACTOR(device_id);

	/*
	 * Total non-DMA memory needed is the sum total of memory needed for
	 * the controller context, request packets (including the 1 needed for
	 * CL internal requests), and event packets.
	 */

	*non_dma_mem_size = sizeof(struct tw_cli_ctlr_context) +
		(sizeof(struct tw_cli_req_context) * max_simult_reqs) +
		(sizeof(struct tw_cl_event_packet) * max_aens);


	/*
	 * Total DMA'able memory needed is the sum total of memory needed for
	 * all command packets (including the 1 needed for CL internal
	 * requests), and memory needed to hold the payload for internal
	 * requests.
	 */

	*dma_mem_size = (sizeof(struct tw_cl_command_packet) *
		(max_simult_reqs)) + (TW_CLI_SECTOR_SIZE);

	return(0);
}



/*
 * Function name:	tw_cl_init_ctlr
 * Description:		Initializes driver data structures for the controller.
 *
 * Input:		ctlr_handle -- controller handle
 *			flags -- more info passed by the OS Layer
 *			device_id -- device id of the controller
 *			max_simult_reqs -- maximum # of simultaneous requests
 *					that the OS Layer expects the Common
 *					Layer to support
 *			max_aens -- maximun # of AEN's needed to be supported
 *			non_dma_mem -- ptr to allocated non-DMA memory
 *			dma_mem -- ptr to allocated DMA'able memory
 *			dma_mem_phys -- physical address of dma_mem
 * Output:		None
 * Return value:	0	-- success
 *			non-zero-- failure
 */
TW_INT32
tw_cl_init_ctlr(struct tw_cl_ctlr_handle *ctlr_handle, TW_UINT32 flags,
	TW_INT32 device_id, TW_INT32 max_simult_reqs, TW_INT32 max_aens,
	TW_VOID *non_dma_mem, TW_VOID *dma_mem, TW_UINT64 dma_mem_phys
	)
{
	struct tw_cli_ctlr_context	*ctlr;
	struct tw_cli_req_context	*req;
	TW_UINT8			*free_non_dma_mem;
	TW_INT32			error = TW_OSL_ESUCCESS;
	TW_INT32			i;

	tw_cli_dbg_printf(3, ctlr_handle, tw_osl_cur_func(), "entered");

	if (flags & TW_CL_START_CTLR_ONLY) {
		ctlr = (struct tw_cli_ctlr_context *)
			(ctlr_handle->cl_ctlr_ctxt);
		goto start_ctlr;
	}

	if (max_simult_reqs > TW_CL_MAX_SIMULTANEOUS_REQUESTS) {
		tw_cl_create_event(ctlr_handle, TW_CL_FALSE,
			TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
			0x1000, 0x1, TW_CL_SEVERITY_ERROR_STRING,
			"Too many simultaneous requests to support!",
			"requested = %d, supported = %d, error = %d\n",
			max_simult_reqs, TW_CL_MAX_SIMULTANEOUS_REQUESTS,
			TW_OSL_EBIG);
		return(TW_OSL_EBIG);
	}

	if ((non_dma_mem == TW_CL_NULL) || (dma_mem == TW_CL_NULL)
		) {
		tw_cl_create_event(ctlr_handle, TW_CL_FALSE,
			TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
			0x1001, 0x1, TW_CL_SEVERITY_ERROR_STRING,
			"Insufficient memory for Common Layer's internal usage",
			"error = %d\n", TW_OSL_ENOMEM);
		return(TW_OSL_ENOMEM);
	}

	tw_osl_memzero(non_dma_mem, sizeof(struct tw_cli_ctlr_context) +
		(sizeof(struct tw_cli_req_context) * max_simult_reqs) +
		(sizeof(struct tw_cl_event_packet) * max_aens));

	tw_osl_memzero(dma_mem,
		(sizeof(struct tw_cl_command_packet) *
		max_simult_reqs) +
		TW_CLI_SECTOR_SIZE);

	free_non_dma_mem = (TW_UINT8 *)non_dma_mem;

	ctlr = (struct tw_cli_ctlr_context *)free_non_dma_mem;
	free_non_dma_mem += sizeof(struct tw_cli_ctlr_context);

	ctlr_handle->cl_ctlr_ctxt = ctlr;
	ctlr->ctlr_handle = ctlr_handle;

	ctlr->device_id = (TW_UINT32)device_id;
	ctlr->arch_id = TWA_ARCH_ID(device_id);
	ctlr->flags = flags;
	ctlr->sg_size_factor = TWA_SG_ELEMENT_SIZE_FACTOR(device_id);
	ctlr->max_simult_reqs = max_simult_reqs;
	ctlr->max_aens_supported = max_aens;

	/* Initialize queues of CL internal request context packets. */
	tw_cli_req_q_init(ctlr, TW_CLI_FREE_Q);
	tw_cli_req_q_init(ctlr, TW_CLI_BUSY_Q);
	tw_cli_req_q_init(ctlr, TW_CLI_PENDING_Q);
	tw_cli_req_q_init(ctlr, TW_CLI_COMPLETE_Q);
	tw_cli_req_q_init(ctlr, TW_CLI_RESET_Q);

	/* Initialize all locks used by CL. */
	ctlr->gen_lock = &(ctlr->gen_lock_handle);
	tw_osl_init_lock(ctlr_handle, "tw_cl_gen_lock", ctlr->gen_lock);
	ctlr->io_lock = &(ctlr->io_lock_handle);
	tw_osl_init_lock(ctlr_handle, "tw_cl_io_lock", ctlr->io_lock);

	/* Initialize CL internal request context packets. */
	ctlr->req_ctxt_buf = (struct tw_cli_req_context *)free_non_dma_mem;
	free_non_dma_mem += (sizeof(struct tw_cli_req_context) *
		max_simult_reqs);

	ctlr->cmd_pkt_buf = (struct tw_cl_command_packet *)dma_mem;
	ctlr->cmd_pkt_phys = dma_mem_phys;

	ctlr->internal_req_data = (TW_UINT8 *)
		(ctlr->cmd_pkt_buf +
		max_simult_reqs);
	ctlr->internal_req_data_phys = ctlr->cmd_pkt_phys +
		(sizeof(struct tw_cl_command_packet) *
		max_simult_reqs);

	for (i = 0; i < max_simult_reqs; i++) {
		req = &(ctlr->req_ctxt_buf[i]);

		req->cmd_pkt = &(ctlr->cmd_pkt_buf[i]);
		req->cmd_pkt_phys = ctlr->cmd_pkt_phys +
			(i * sizeof(struct tw_cl_command_packet));

		req->request_id = i;
		req->ctlr = ctlr;

		/* Insert request into the free queue. */
		tw_cli_req_q_insert_tail(req, TW_CLI_FREE_Q);
	}

	/* Initialize the AEN queue. */
	ctlr->aen_queue = (struct tw_cl_event_packet *)free_non_dma_mem;


start_ctlr:
	/*
	 * Disable interrupts.  Interrupts will be enabled in tw_cli_start_ctlr
	 * (only) if initialization succeeded.
	 */
	tw_cli_disable_interrupts(ctlr);

	/* Initialize the controller. */
	if ((error = tw_cli_start_ctlr(ctlr))) {
		/* Soft reset the controller, and try one more time. */
		tw_cl_create_event(ctlr_handle, TW_CL_FALSE,
			TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
			0x1002, 0x1, TW_CL_SEVERITY_ERROR_STRING,
			"Controller initialization failed. Retrying...",
			"error = %d\n", error);
		if ((error = tw_cli_soft_reset(ctlr))) {
			tw_cl_create_event(ctlr_handle, TW_CL_FALSE,
				TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
				0x1003, 0x1, TW_CL_SEVERITY_ERROR_STRING,
				"Controller soft reset failed",
				"error = %d\n", error);
			return(error);
		} else if ((error = tw_cli_start_ctlr(ctlr))) {
			tw_cl_create_event(ctlr_handle, TW_CL_FALSE,
				TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
				0x1004, 0x1, TW_CL_SEVERITY_ERROR_STRING,
				"Controller initialization retry failed",
				"error = %d\n", error);
			return(error);
		}
	}
	/* Notify some info about the controller to the OSL. */
	tw_cli_notify_ctlr_info(ctlr);

	/* Mark the controller active. */
	ctlr->active = TW_CL_TRUE;
	return(error);
}

/*
 * Function name:	tw_cli_start_ctlr
 * Description:		Establishes a logical connection with the controller.
 *			Determines whether or not the driver is compatible 
 *                      with the firmware on the controller, before proceeding
 *                      to work with it.
 *
 * Input:		ctlr	-- ptr to per ctlr structure
 * Output:		None
 * Return value:	0	-- success
 *			non-zero-- failure
 */
TW_INT32
tw_cli_start_ctlr(struct tw_cli_ctlr_context *ctlr)
{
	TW_UINT16	fw_on_ctlr_srl = 0;
	TW_UINT16	fw_on_ctlr_arch_id = 0;
	TW_UINT16	fw_on_ctlr_branch = 0;
	TW_UINT16	fw_on_ctlr_build = 0;
	TW_UINT32	init_connect_result = 0;
	TW_INT32	error = TW_OSL_ESUCCESS;

	tw_cli_dbg_printf(3, ctlr->ctlr_handle, tw_osl_cur_func(), "entered");

	/* Wait for the controller to become ready. */
	if ((error = tw_cli_poll_status(ctlr,
			TWA_STATUS_MICROCONTROLLER_READY,
			TW_CLI_REQUEST_TIMEOUT_PERIOD))) {
		tw_cl_create_event(ctlr->ctlr_handle, TW_CL_FALSE,
			TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
			0x1009, 0x1, TW_CL_SEVERITY_ERROR_STRING,
			"Microcontroller not ready",
			"error = %d", error);
		return(error);
	}
	/* Drain the response queue. */
	if ((error = tw_cli_drain_response_queue(ctlr))) {
		tw_cl_create_event(ctlr->ctlr_handle, TW_CL_FALSE,
			TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
			0x100A, 0x1, TW_CL_SEVERITY_ERROR_STRING,
			"Can't drain response queue",
			"error = %d", error);
		return(error);
	}
	/* Establish a logical connection with the controller. */
	if ((error = tw_cli_init_connection(ctlr,
			(TW_UINT16)(ctlr->max_simult_reqs),
			TWA_EXTENDED_INIT_CONNECT, TWA_CURRENT_FW_SRL,
			(TW_UINT16)(ctlr->arch_id),
			TWA_CURRENT_FW_BRANCH(ctlr->arch_id),
			TWA_CURRENT_FW_BUILD(ctlr->arch_id),
			&fw_on_ctlr_srl, &fw_on_ctlr_arch_id,
			&fw_on_ctlr_branch, &fw_on_ctlr_build,
			&init_connect_result))) {
		tw_cl_create_event(ctlr->ctlr_handle, TW_CL_FALSE,
			TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
			0x100B, 0x2, TW_CL_SEVERITY_WARNING_STRING,
			"Can't initialize connection in current mode",
			"error = %d", error);
		return(error);
	}
	{
		 /* See if we can at least work with the firmware on the
                 * controller in the current mode.
		 */
		if (init_connect_result & TWA_CTLR_FW_COMPATIBLE) {
			/* Yes, we can.  Make note of the operating mode. */
			if (init_connect_result & TWA_CTLR_FW_SAME_OR_NEWER) {
				ctlr->working_srl = TWA_CURRENT_FW_SRL;
				ctlr->working_branch =
					TWA_CURRENT_FW_BRANCH(ctlr->arch_id);
				ctlr->working_build =
					TWA_CURRENT_FW_BUILD(ctlr->arch_id);
			} else {
				ctlr->working_srl = fw_on_ctlr_srl;
				ctlr->working_branch = fw_on_ctlr_branch;
				ctlr->working_build = fw_on_ctlr_build;
			}
		} else {
			/*
			 * No, we can't.  See if we can at least work with
			 * it in the base mode.
			 */
			tw_cl_create_event(ctlr->ctlr_handle, TW_CL_FALSE,
				TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
				0x1010, 0x2, TW_CL_SEVERITY_WARNING_STRING,
				"Driver/Firmware mismatch. "
				"Negotiating for base level...",
				" ");
			if ((error = tw_cli_init_connection(ctlr,
					(TW_UINT16)(ctlr->max_simult_reqs),
					TWA_EXTENDED_INIT_CONNECT,
					TWA_BASE_FW_SRL,
					(TW_UINT16)(ctlr->arch_id),
					TWA_BASE_FW_BRANCH, TWA_BASE_FW_BUILD,
					&fw_on_ctlr_srl, &fw_on_ctlr_arch_id,
					&fw_on_ctlr_branch, &fw_on_ctlr_build,
					&init_connect_result))) {
				tw_cl_create_event(ctlr->ctlr_handle,
					TW_CL_FALSE,
					TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
					0x1011, 0x1,
					TW_CL_SEVERITY_ERROR_STRING,
					"Can't initialize connection in "
					"base mode",
					" ");
				return(error);
			}
			if (!(init_connect_result & TWA_CTLR_FW_COMPATIBLE)) {
				/*
				 * The firmware on the controller is not even
				 * compatible with our base mode.  We cannot
				 * work with it.  Bail...
				 */
				return(1);
			}
			/*
			 * We can work with this firmware, but only in
			 * base mode.
			 */
			ctlr->working_srl = TWA_BASE_FW_SRL;
			ctlr->working_branch = TWA_BASE_FW_BRANCH;
			ctlr->working_build = TWA_BASE_FW_BUILD;
			ctlr->operating_mode = TWA_BASE_MODE;
		}
		ctlr->fw_on_ctlr_srl = fw_on_ctlr_srl;
		ctlr->fw_on_ctlr_branch = fw_on_ctlr_branch;
		ctlr->fw_on_ctlr_build = fw_on_ctlr_build;
	}

	/* Drain the AEN queue */
	if ((error = tw_cli_drain_aen_queue(ctlr)))
		/* 
		 * We will just print that we couldn't drain the AEN queue.
		 * There's no need to bail out.
		 */
		tw_cl_create_event(ctlr->ctlr_handle, TW_CL_FALSE,
			TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
			0x1014, 0x2, TW_CL_SEVERITY_WARNING_STRING,
			"Can't drain AEN queue",
			"error = %d", error);

	/* Enable interrupts. */
	tw_cli_enable_interrupts(ctlr);

	return(TW_OSL_ESUCCESS);
}


/*
 * Function name:	tw_cl_shutdown_ctlr
 * Description:		Closes logical connection with the controller.
 *
 * Input:		ctlr	-- ptr to per ctlr structure
 *			flags	-- more info passed by the OS Layer
 * Output:		None
 * Return value:	0	-- success
 *			non-zero-- failure
 */
TW_INT32
tw_cl_shutdown_ctlr(struct tw_cl_ctlr_handle *ctlr_handle, TW_UINT32 flags)
{
	struct tw_cli_ctlr_context	*ctlr =
		(struct tw_cli_ctlr_context *)(ctlr_handle->cl_ctlr_ctxt);
	TW_INT32			error;

	tw_cli_dbg_printf(3, ctlr_handle, tw_osl_cur_func(), "entered");
	/*
	 * Mark the controller as inactive, disable any further interrupts,
	 * and notify the controller that we are going down.
	 */
	ctlr->active = TW_CL_FALSE;

	tw_cli_disable_interrupts(ctlr);

	/* Let the controller know that we are going down. */
	if ((error = tw_cli_init_connection(ctlr, TWA_SHUTDOWN_MESSAGE_CREDITS,
			0, 0, 0, 0, 0, TW_CL_NULL, TW_CL_NULL, TW_CL_NULL,
			TW_CL_NULL, TW_CL_NULL)))
		tw_cl_create_event(ctlr_handle, TW_CL_FALSE,
			TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
			0x1015, 0x1, TW_CL_SEVERITY_ERROR_STRING,
			"Can't close connection with controller",
			"error = %d", error);

	if (flags & TW_CL_STOP_CTLR_ONLY)
		goto ret;

	/* Destroy all locks used by CL. */
	tw_osl_destroy_lock(ctlr_handle, ctlr->gen_lock);
	tw_osl_destroy_lock(ctlr_handle, ctlr->io_lock);

ret:
	return(error);
}



/*
 * Function name:	tw_cli_init_connection
 * Description:		Sends init_connection cmd to firmware
 *
 * Input:		ctlr		-- ptr to per ctlr structure
 *			message_credits	-- max # of requests that we might send
 *					 down simultaneously.  This will be
 *					 typically set to 256 at init-time or
 *					after a reset, and to 1 at shutdown-time
 *			set_features	-- indicates if we intend to use 64-bit
 *					sg, also indicates if we want to do a
 *					basic or an extended init_connection;
 *
 * Note: The following input/output parameters are valid, only in case of an
 *		extended init_connection:
 *
 *			current_fw_srl		-- srl of fw we are bundled
 *						with, if any; 0 otherwise
 *			current_fw_arch_id	-- arch_id of fw we are bundled
 *						with, if any; 0 otherwise
 *			current_fw_branch	-- branch # of fw we are bundled
 *						with, if any; 0 otherwise
 *			current_fw_build	-- build # of fw we are bundled
 *						with, if any; 0 otherwise
 * Output:		fw_on_ctlr_srl		-- srl of fw on ctlr
 *			fw_on_ctlr_arch_id	-- arch_id of fw on ctlr
 *			fw_on_ctlr_branch	-- branch # of fw on ctlr
 *			fw_on_ctlr_build	-- build # of fw on ctlr
 *			init_connect_result	-- result bitmap of fw response
 * Return value:	0	-- success
 *			non-zero-- failure
 */
TW_INT32
tw_cli_init_connection(struct tw_cli_ctlr_context *ctlr,
	TW_UINT16 message_credits, TW_UINT32 set_features,
	TW_UINT16 current_fw_srl, TW_UINT16 current_fw_arch_id,
	TW_UINT16 current_fw_branch, TW_UINT16 current_fw_build,
	TW_UINT16 *fw_on_ctlr_srl, TW_UINT16 *fw_on_ctlr_arch_id,
	TW_UINT16 *fw_on_ctlr_branch, TW_UINT16 *fw_on_ctlr_build,
	TW_UINT32 *init_connect_result)
{
	struct tw_cli_req_context		*req;
	struct tw_cl_command_init_connect	*init_connect;
	TW_INT32				error = TW_OSL_EBUSY;
    
	tw_cli_dbg_printf(3, ctlr->ctlr_handle, tw_osl_cur_func(), "entered");

	/* Get a request packet. */
	if ((req = tw_cli_get_request(ctlr
		)) == TW_CL_NULL)
		goto out;

	req->flags |= TW_CLI_REQ_FLAGS_INTERNAL;

	/* Build the cmd pkt. */
	init_connect = &(req->cmd_pkt->command.cmd_pkt_7k.init_connect);

	req->cmd_pkt->cmd_hdr.header_desc.size_header = 128;

	init_connect->res1__opcode =
		BUILD_RES__OPCODE(0, TWA_FW_CMD_INIT_CONNECTION);
   	init_connect->request_id =
		(TW_UINT8)(TW_CL_SWAP16(req->request_id));
	init_connect->message_credits = TW_CL_SWAP16(message_credits);
	init_connect->features = TW_CL_SWAP32(set_features);
	if (ctlr->flags & TW_CL_64BIT_ADDRESSES)
		init_connect->features |= TW_CL_SWAP32(TWA_64BIT_SG_ADDRESSES);
	if (set_features & TWA_EXTENDED_INIT_CONNECT) {
		/*
		 * Fill in the extra fields needed for an extended
		 * init_connect.
		 */
		init_connect->size = 6;
		init_connect->fw_srl = TW_CL_SWAP16(current_fw_srl);
		init_connect->fw_arch_id = TW_CL_SWAP16(current_fw_arch_id);
		init_connect->fw_branch = TW_CL_SWAP16(current_fw_branch);
		init_connect->fw_build = TW_CL_SWAP16(current_fw_build);
	} else
		init_connect->size = 3;

	/* Submit the command, and wait for it to complete. */
	error = tw_cli_submit_and_poll_request(req,
		TW_CLI_REQUEST_TIMEOUT_PERIOD);
	if (error)
		goto out;
	if ((error = init_connect->status)) {
#if       0
		tw_cli_create_ctlr_event(ctlr,
			TW_CL_MESSAGE_SOURCE_CONTROLLER_ERROR,
			&(req->cmd_pkt->cmd_hdr));
#endif // 0
		goto out;
	}
	if (set_features & TWA_EXTENDED_INIT_CONNECT) {
		*fw_on_ctlr_srl = TW_CL_SWAP16(init_connect->fw_srl);
		*fw_on_ctlr_arch_id = TW_CL_SWAP16(init_connect->fw_arch_id);
		*fw_on_ctlr_branch = TW_CL_SWAP16(init_connect->fw_branch);
		*fw_on_ctlr_build = TW_CL_SWAP16(init_connect->fw_build);
		*init_connect_result = TW_CL_SWAP32(init_connect->result);
	}
	tw_cli_req_q_insert_tail(req, TW_CLI_FREE_Q);
	return(error);

out:
	tw_cl_create_event(ctlr->ctlr_handle, TW_CL_FALSE,
		TW_CL_MESSAGE_SOURCE_COMMON_LAYER_ERROR,
		0x1016, 0x1, TW_CL_SEVERITY_ERROR_STRING,
		"init_connection failed",
		"error = %d", error);
	if (req)
		tw_cli_req_q_insert_tail(req, TW_CLI_FREE_Q);
	return(error);
}


