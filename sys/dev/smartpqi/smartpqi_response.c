/*-
 * Copyright (c) 2018 Microsemi Corporation.
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
 */

/* $FreeBSD$ */

#include "smartpqi_includes.h"

/*
 * Process internal RAID response in the case of success.
 */
void pqisrc_process_internal_raid_response_success(pqisrc_softstate_t *softs,
					  rcb_t *rcb) 
{
	DBG_FUNC("IN");

	rcb->status = REQUEST_SUCCESS;
	rcb->req_pending = false;

	DBG_FUNC("OUT");
}

/*
 * Process internal RAID response in the case of failure.
 */
void  pqisrc_process_internal_raid_response_error(pqisrc_softstate_t *softs,
				       rcb_t *rcb, uint16_t err_idx)
{
	raid_path_error_info_elem_t error_info;

	DBG_FUNC("IN");

	rcb->error_info = (char *) (softs->err_buf_dma_mem.virt_addr) +
			  (err_idx * PQI_ERROR_BUFFER_ELEMENT_LENGTH);
	rcb->status = REQUEST_SUCCESS;
	memcpy(&error_info, rcb->error_info, sizeof(error_info));

	DBG_INFO("error_status 0x%x data_in_result 0x%x data_out_result 0x%x\n",
		error_info.status, error_info.data_in_result, error_info.data_out_result);

	if (error_info.status != 0)
		rcb->status = REQUEST_FAILED;
	if (error_info.data_in_result != PQI_RAID_DATA_IN_OUT_GOOD)
		rcb->status = REQUEST_FAILED;
	if (error_info.data_out_result != PQI_RAID_DATA_IN_OUT_GOOD)
		rcb->status = REQUEST_FAILED;

	rcb->req_pending = false;

	DBG_FUNC("OUT");
}

/*
 * Process the AIO/RAID IO in the case of success.
 */
void pqisrc_process_io_response_success(pqisrc_softstate_t *softs,
		rcb_t *rcb)
{
	DBG_FUNC("IN");

	os_io_response_success(rcb);

	DBG_FUNC("OUT");
}

/*
 * Process the error info for AIO in the case of failure.
 */
void pqisrc_process_aio_response_error(pqisrc_softstate_t *softs,
		rcb_t *rcb, uint16_t err_idx)
{
	aio_path_error_info_elem_t *err_info = NULL;
	
	DBG_FUNC("IN");
	
	err_info = (aio_path_error_info_elem_t*)
			softs->err_buf_dma_mem.virt_addr + 
			err_idx;

	if(err_info == NULL) {
		DBG_ERR("err_info structure is NULL  err_idx :%x", err_idx);
		return;
	}
	
	os_aio_response_error(rcb, err_info);

	DBG_FUNC("OUT");
}

/*
 * Process the error info for RAID IO in the case of failure.
 */
void pqisrc_process_raid_response_error(pqisrc_softstate_t *softs,
		rcb_t *rcb, uint16_t err_idx)
{
	raid_path_error_info_elem_t *err_info = NULL;
	
	DBG_FUNC("IN");
	
	err_info = (raid_path_error_info_elem_t*)
			softs->err_buf_dma_mem.virt_addr + 
			err_idx;
	
	if(err_info == NULL) {
		DBG_ERR("err_info structure is NULL  err_idx :%x", err_idx);
		return;
	}
	
	os_raid_response_error(rcb, err_info);

	DBG_FUNC("OUT");
}

/*
 * Process the Task Management function response.
 */
int pqisrc_process_task_management_response(pqisrc_softstate_t *softs,
			pqi_tmf_resp_t *tmf_resp)
{
	int ret = REQUEST_SUCCESS;
	uint32_t tag = (uint32_t)tmf_resp->req_id;
	rcb_t *rcb = &softs->rcb[tag];

	ASSERT(rcb->tag == tag);

	DBG_FUNC("IN\n");

	switch (tmf_resp->resp_code) {
	case SOP_TASK_MANAGEMENT_FUNCTION_COMPLETE:
	case SOP_TASK_MANAGEMENT_FUNCTION_SUCCEEDED:
		ret = REQUEST_SUCCESS;
		break;
	default:
		DBG_ERR("TMF Failed, Response code : 0x%x\n", tmf_resp->resp_code);
		ret = REQUEST_FAILED;
		break;
	}

	rcb->status = ret;
	rcb->req_pending = false;

	DBG_FUNC("OUT");
	return ret;
}

/*
 * Function used to process the response from the adapter
 * which is invoked by IRQ handler.
 */
void 
pqisrc_process_response_queue(pqisrc_softstate_t *softs, int oq_id)
{
	ob_queue_t *ob_q;
	struct pqi_io_response *response;
	uint32_t oq_pi, oq_ci;

	DBG_FUNC("IN");

	OS_ATOMIC64_INC(softs, num_intrs);
	
	ob_q = &softs->op_ob_q[oq_id - 1]; /* zero for event Q */
	oq_ci = ob_q->ci_local;
	oq_pi = *(ob_q->pi_virt_addr);

	DBG_INFO("ci : %d pi : %d qid : %d\n", oq_ci, oq_pi, ob_q->q_id);

	while (1) {
		rcb_t *rcb = NULL;
		uint32_t tag = 0;
		uint32_t offset;

		if (oq_pi == oq_ci)
			break;
		/* Get the response */
		offset = oq_ci * ob_q->elem_size;
		response = (struct pqi_io_response *)(ob_q->array_virt_addr + 
							offset);
		tag = response->request_id;
		rcb = &softs->rcb[tag];
		/* Make sure we are processing a valid response. */ 
		ASSERT(rcb->tag == tag && rcb->req_pending);
		rcb->req_pending = false;

		DBG_INFO("response.header.iu_type : %x \n", response->header.iu_type);

		switch (response->header.iu_type) {
		case PQI_RESPONSE_IU_RAID_PATH_IO_SUCCESS:
		case PQI_RESPONSE_IU_AIO_PATH_IO_SUCCESS:
			rcb->success_cmp_callback(softs, rcb);
			break;
		case PQI_RESPONSE_IU_RAID_PATH_IO_ERROR:
		case PQI_RESPONSE_IU_AIO_PATH_IO_ERROR:
			rcb->error_cmp_callback(softs, rcb, LE_16(response->error_index));
			break;
		case PQI_RESPONSE_IU_GENERAL_MANAGEMENT:
			rcb->req_pending = false;
			break;
		case PQI_RESPONSE_IU_TASK_MANAGEMENT:
			rcb->status = pqisrc_process_task_management_response(softs, (void *)response);
			break;

		default:
			DBG_ERR("Invalid Response IU 0x%x\n",response->header.iu_type);
			break;
		}

		oq_ci = (oq_ci + 1) % ob_q->num_elem;
	}

	ob_q->ci_local = oq_ci;
	PCI_MEM_PUT32(softs, ob_q->ci_register_abs, 
        ob_q->ci_register_offset, ob_q->ci_local );
	DBG_FUNC("OUT");
}
