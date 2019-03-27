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

#include"smartpqi_includes.h"

/*
 * Function to rescan the devices connected to adapter.
 */
int
pqisrc_rescan_devices(pqisrc_softstate_t *softs)
{
	int ret;
	
	DBG_FUNC("IN\n");
	
	os_sema_lock(&softs->scan_lock);
	
	ret = pqisrc_scan_devices(softs);

	os_sema_unlock(&softs->scan_lock);
	
	DBG_FUNC("OUT\n");

	return ret;
}

void pqisrc_wait_for_rescan_complete(pqisrc_softstate_t *softs)
{
	os_sema_lock(&softs->scan_lock);
	os_sema_unlock(&softs->scan_lock);
}

/*
 * Subroutine to acknowledge the events processed by the driver to the adapter.
 */
static void 
pqisrc_acknowledge_event(pqisrc_softstate_t *softs, 
	struct pqi_event *event)
{
 
	pqi_event_acknowledge_request_t request;
	ib_queue_t *ib_q = &softs->op_raid_ib_q[0];
	int tmo = PQISRC_EVENT_ACK_RESP_TIMEOUT;
	memset(&request,0,sizeof(request));

	DBG_FUNC("IN\n");

	request.header.iu_type = PQI_REQUEST_IU_ACKNOWLEDGE_VENDOR_EVENT;
	request.header.iu_length = (sizeof(pqi_event_acknowledge_request_t) - 
								PQI_REQUEST_HEADER_LENGTH);
	request.event_type = event->event_type;
	request.event_id   = event->event_id;
	request.additional_event_id = event->additional_event_id;

	/* Submit Event Acknowledge */

	pqisrc_submit_cmnd(softs, ib_q, &request);

	/*
	 * We have to special-case this type of request because the firmware
	 * does not generate an interrupt when this type of request completes.
	 * Therefore, we have to poll until we see that the firmware has
	 * consumed the request before we move on.
	 */

	COND_WAIT(((ib_q->pi_local) == *(ib_q->ci_virt_addr)), tmo);
		if (tmo <= 0) {
			DBG_ERR("wait for event acknowledge timed out\n");
			DBG_ERR("tmo : %d\n",tmo);	
 		}		
	
	DBG_FUNC(" OUT\n");
}

/*
 * Acknowledge processed events to the adapter.
 */
void
pqisrc_ack_all_events(void *arg1)
{
	int i;
	struct pqi_event *pending_event;
	pqisrc_softstate_t *softs = (pqisrc_softstate_t*)arg1;
		
	DBG_FUNC(" IN\n");


	pending_event = &softs->pending_events[0];
	for (i=0; i < PQI_NUM_SUPPORTED_EVENTS; i++) {
		if (pending_event->pending == true) {
			pending_event->pending = false;
			pqisrc_acknowledge_event(softs, pending_event);
		}
		pending_event++;
	}
	
	/* Rescan devices except for heartbeat event */
	if ((pqisrc_rescan_devices(softs)) != PQI_STATUS_SUCCESS) {
			DBG_ERR(" Failed to Re-Scan devices\n ");
	}
	DBG_FUNC(" OUT\n");
	
}

/*
 * Get event index from event type to validate the type of event.
 */
static int 
pqisrc_event_type_to_event_index(unsigned event_type)
{
	int index;

	switch (event_type) {
	case PQI_EVENT_TYPE_HOTPLUG:
		index = PQI_EVENT_HOTPLUG;
		break;
	case PQI_EVENT_TYPE_HARDWARE:
		index = PQI_EVENT_HARDWARE;
		break;
	case PQI_EVENT_TYPE_PHYSICAL_DEVICE:
		index = PQI_EVENT_PHYSICAL_DEVICE;
		break;
	case PQI_EVENT_TYPE_LOGICAL_DEVICE:
		index = PQI_EVENT_LOGICAL_DEVICE;
		break;
	case PQI_EVENT_TYPE_AIO_STATE_CHANGE:
		index = PQI_EVENT_AIO_STATE_CHANGE;
		break;
	case PQI_EVENT_TYPE_AIO_CONFIG_CHANGE:
		index = PQI_EVENT_AIO_CONFIG_CHANGE;
		break;
	default:
		index = -1;
		break;
	}

	return index;
}

/*
 * Function used to process the events supported by the adapter.
 */
int 
pqisrc_process_event_intr_src(pqisrc_softstate_t *softs,int obq_id)
{
	uint32_t obq_pi,obq_ci;
	pqi_event_response_t response;
	ob_queue_t *event_q;
	struct pqi_event *pending_event;
	boolean_t  need_delayed_work = false;
	
	DBG_FUNC(" IN\n");
	
	OS_ATOMIC64_INC(softs, num_intrs);
	
	event_q = &softs->event_q;
	obq_ci = event_q->ci_local;
	obq_pi = *(event_q->pi_virt_addr);
	DBG_INFO("Initial Event_q ci : %d Event_q pi : %d\n", obq_ci, obq_pi);
	
	while(1) {
		int event_index;
		DBG_INFO("queue_id : %d ci : %d pi : %d\n",obq_id, obq_ci, obq_pi);
		if (obq_pi == obq_ci)
			break;

		need_delayed_work = true;

		/* Copy the response */
		memcpy(&response, event_q->array_virt_addr + (obq_ci * event_q->elem_size),
					sizeof(pqi_event_response_t));
		DBG_INFO("response.header.iu_type : 0x%x \n", response.header.iu_type);
		DBG_INFO("response.event_type : 0x%x \n", response.event_type);

		event_index = pqisrc_event_type_to_event_index(response.event_type);

		if (event_index >= 0) {
			if(response.request_acknowledge) {
				pending_event = &softs->pending_events[event_index];
				pending_event->pending = true;
				pending_event->event_type = response.event_type;
				pending_event->event_id = response.event_id;
				pending_event->additional_event_id = response.additional_event_id;
			}
		}
     	
	obq_ci = (obq_ci + 1) % event_q->num_elem;
	}
	/* Update CI */
	event_q->ci_local = obq_ci;
	PCI_MEM_PUT32(softs, event_q->ci_register_abs,
        event_q->ci_register_offset, event_q->ci_local);

	/*Adding events to the task queue for acknowledging*/
	if (need_delayed_work == true) {
		os_eventtaskqueue_enqueue(softs);
	}

	DBG_FUNC("OUT");
	return PQI_STATUS_SUCCESS;
	

}

/*
 * Function used to send a general management request to adapter.
 */
int pqisrc_submit_management_req(pqisrc_softstate_t *softs, 
                                      pqi_event_config_request_t *request)
{  
	int ret = PQI_STATUS_SUCCESS;
	ib_queue_t *op_ib_q = &softs->op_raid_ib_q[0];
	rcb_t *rcb = NULL;
	
	DBG_FUNC(" IN\n");

	/* Get the tag */
	request->request_id = pqisrc_get_tag(&softs->taglist);
	if (INVALID_ELEM == request->request_id) {
		DBG_ERR("Tag not available\n");
		ret = PQI_STATUS_FAILURE;
		goto err_out; 
	}

	rcb = &softs->rcb[request->request_id];
	rcb->req_pending = true;
	rcb->tag = request->request_id;
	/* Submit command on operational raid ib queue */
	ret = pqisrc_submit_cmnd(softs, op_ib_q, request);
	if (ret != PQI_STATUS_SUCCESS) {
		DBG_ERR("  Unable to submit command\n");
		goto err_cmd;
	}

	ret = pqisrc_wait_on_condition(softs, rcb);
	if (ret != PQI_STATUS_SUCCESS) {
		DBG_ERR("Management request timed out !!\n");
		goto err_cmd;
	}

	os_reset_rcb(rcb);    
 	pqisrc_put_tag(&softs->taglist,request->request_id);
	DBG_FUNC("OUT\n");
	return ret;
	
err_cmd:
	os_reset_rcb(rcb); 
	pqisrc_put_tag(&softs->taglist,request->request_id);
err_out:
	DBG_FUNC(" failed OUT : %d\n", ret);
	return ret;
}

/*
 * Build and send the general management request.
 */
static int 
pqi_event_configure(pqisrc_softstate_t *softs , 
                              pqi_event_config_request_t *request, 
                              dma_mem_t *buff)
{
        int ret = PQI_STATUS_SUCCESS;
	
	DBG_FUNC(" IN\n");
	
	request->header.comp_feature = 0x00;
	request->header.iu_length = sizeof(pqi_event_config_request_t) - 
		    PQI_REQUEST_HEADER_LENGTH; /* excluding IU header length */
				    
	/*Op OQ id where response to be delivered */
	request->response_queue_id = softs->op_ob_q[0].q_id;
	request->buffer_length 	   = buff->size;
	request->sg_desc.addr 	   = buff->dma_addr;
	request->sg_desc.length    = buff->size;
	request->sg_desc.zero 	   = 0;
	request->sg_desc.type 	   = SGL_DESCRIPTOR_CODE_LAST_ALTERNATIVE_SGL_SEGMENT;
        
	/* submit management req IU*/
	ret = pqisrc_submit_management_req(softs,request);
	if(ret)
		goto err_out;
	

	DBG_FUNC(" OUT\n");
	return ret;
  
err_out:
	DBG_FUNC("Failed OUT\n");
	return ret;
}

/*
 * Prepare REPORT EVENT CONFIGURATION IU to request that
 * event configuration information be reported.
 */
int pqisrc_report_event_config(pqisrc_softstate_t *softs)
{

	int ret,i ;
	pqi_event_config_request_t request; 
	pqi_event_config_t  *event_config_p ;
	dma_mem_t  buf_report_event ;
	/*bytes to be allocaed for report event config data-in buffer */
	uint32_t alloc_size = sizeof(pqi_event_config_t) ;
	memset(&request, 0 , sizeof(request));
 
	DBG_FUNC(" IN\n");
	
	memset(&buf_report_event, 0, sizeof(struct dma_mem)); 
	buf_report_event.tag 	= "pqi_report_event_buf" ;
	buf_report_event.size 	= alloc_size;
	buf_report_event.align 	= PQISRC_DEFAULT_DMA_ALIGN;
 
	/* allocate memory */
	ret = os_dma_mem_alloc(softs, &buf_report_event);
	if (ret) {
		DBG_ERR("Failed to Allocate report event config buffer : %d\n", ret);
		goto err_out;
	}
	DBG_INFO("buf_report_event.dma_addr	= %p \n",(void*)buf_report_event.dma_addr);
	DBG_INFO("buf_report_event.virt_addr 	= %p \n",(void*)buf_report_event.virt_addr);
  
	request.header.iu_type = PQI_REQUEST_IU_REPORT_VENDOR_EVENT_CONFIG;
	
	/* Event configuration */
	ret=pqi_event_configure(softs,&request,&buf_report_event);
	if(ret)
		goto free_mem;
    
	
	event_config_p = (pqi_event_config_t*)buf_report_event.virt_addr;
	softs->event_config.num_event_descriptors = MIN(event_config_p->num_event_descriptors,
		                                            PQI_MAX_EVENT_DESCRIPTORS) ;
							    
        for (i=0; i < softs->event_config.num_event_descriptors ;i++){
		softs->event_config.descriptors[i].event_type = 
					event_config_p->descriptors[i].event_type;
	}
        /* free the allocated memory*/
	os_dma_mem_free(softs, &buf_report_event);
	   
	DBG_FUNC(" OUT\n");
	return ret;

free_mem:
	os_dma_mem_free(softs, &buf_report_event);
err_out:
	DBG_FUNC("Failed OUT\n");
	return PQI_STATUS_FAILURE;
}

/*
 * Prepare SET EVENT CONFIGURATION IU to request that
 * event configuration parameters be set.
 */
int pqisrc_set_event_config(pqisrc_softstate_t *softs)
{

	int ret,i;
	pqi_event_config_request_t request;
	pqi_event_config_t *event_config_p;
	dma_mem_t buf_set_event;
	/*bytes to be allocaed for set event config data-out buffer */
	uint32_t alloc_size = sizeof(pqi_event_config_t);
	memset(&request, 0 , sizeof(request));

	DBG_FUNC(" IN\n");

 	memset(&buf_set_event, 0, sizeof(struct dma_mem));
	buf_set_event.tag 	= "pqi_set_event_buf";
	buf_set_event.size 	= alloc_size;
	buf_set_event.align 	= PQISRC_DEFAULT_DMA_ALIGN;
	  
	/* allocate memory */
	ret = os_dma_mem_alloc(softs, &buf_set_event);
	if (ret) {
		DBG_ERR("Failed to Allocate set event config buffer : %d\n", ret);
		goto err_out;
	}
		 
	DBG_INFO("buf_set_event.dma_addr  	= %p\n",(void*)buf_set_event.dma_addr);
	DBG_INFO("buf_set_event.virt_addr 	= %p\n",(void*)buf_set_event.virt_addr);

	request.header.iu_type = PQI_REQUEST_IU_SET_EVENT_CONFIG;
	request.iu_specific.global_event_oq_id = softs->event_q.q_id; 

	/*pointer to data-out buffer*/

	event_config_p = (pqi_event_config_t *)buf_set_event.virt_addr;

	event_config_p->num_event_descriptors = softs->event_config.num_event_descriptors;

	
	for (i=0; i < softs->event_config.num_event_descriptors ; i++){
		event_config_p->descriptors[i].event_type = 
					softs->event_config.descriptors[i].event_type;
		if( pqisrc_event_type_to_event_index(event_config_p->descriptors[i].event_type) != -1)
			event_config_p->descriptors[i].oq_id = softs->event_q.q_id;
		else
			event_config_p->descriptors[i].oq_id = 0; /* Not supported this event. */
			

	}
        /* Event configuration */
	ret = pqi_event_configure(softs,&request,&buf_set_event);
		if(ret)
			goto free_mem;
    
	os_dma_mem_free(softs, &buf_set_event);
	   
	DBG_FUNC(" OUT\n");
	return ret;
	
free_mem:
	os_dma_mem_free(softs, &buf_set_event);
err_out:
	DBG_FUNC("Failed OUT\n");
	return PQI_STATUS_FAILURE;

}
