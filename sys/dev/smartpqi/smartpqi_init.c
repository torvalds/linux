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
 * Request the adapter to get PQI capabilities supported.
 */
static int pqisrc_report_pqi_capability(pqisrc_softstate_t *softs)
{
	int ret = PQI_STATUS_SUCCESS;
	
	DBG_FUNC("IN\n");

	gen_adm_req_iu_t	admin_req;
	gen_adm_resp_iu_t 	admin_resp;
	dma_mem_t		pqi_cap_dma_buf;
	pqi_dev_cap_t 		*capability = NULL;
	pqi_iu_layer_desc_t	*iu_layer_desc = NULL;

	/* Allocate Non DMA memory */
	capability = os_mem_alloc(softs, sizeof(*capability));
	if (!capability) {
		DBG_ERR("Failed to allocate memory for capability\n");
		ret = PQI_STATUS_FAILURE;
		goto err_out;
	}

	memset(&admin_req, 0, sizeof(admin_req));
	memset(&admin_resp, 0, sizeof(admin_resp));

	memset(&pqi_cap_dma_buf, 0, sizeof(struct dma_mem));
	pqi_cap_dma_buf.tag = "pqi_cap_buf";
	pqi_cap_dma_buf.size = REPORT_PQI_DEV_CAP_DATA_BUF_SIZE;
	pqi_cap_dma_buf.align = PQISRC_DEFAULT_DMA_ALIGN;

	ret = os_dma_mem_alloc(softs, &pqi_cap_dma_buf);
	if (ret) {
		DBG_ERR("Failed to allocate capability DMA buffer : %d\n", ret);
		goto err_dma_alloc;
	}
	
	admin_req.fn_code = PQI_FUNCTION_REPORT_DEV_CAP;
	admin_req.req_type.general_func.buf_size = pqi_cap_dma_buf.size;
	admin_req.req_type.general_func.sg_desc.length = pqi_cap_dma_buf.size;
	admin_req.req_type.general_func.sg_desc.addr = pqi_cap_dma_buf.dma_addr;
	admin_req.req_type.general_func.sg_desc.type =	SGL_DESCRIPTOR_CODE_DATA_BLOCK;

	ret = pqisrc_submit_admin_req(softs, &admin_req, &admin_resp);
	if( PQI_STATUS_SUCCESS == ret) {
                memcpy(capability,
			pqi_cap_dma_buf.virt_addr,
			pqi_cap_dma_buf.size);
	} else {
		DBG_ERR("Failed to send admin req report pqi device capability\n");
		goto err_admin_req;
		
	}

	softs->pqi_dev_cap.max_iqs = capability->max_iqs;
	softs->pqi_dev_cap.max_iq_elements = capability->max_iq_elements;
	softs->pqi_dev_cap.max_iq_elem_len = capability->max_iq_elem_len;
	softs->pqi_dev_cap.min_iq_elem_len = capability->min_iq_elem_len;
	softs->pqi_dev_cap.max_oqs = capability->max_oqs;
	softs->pqi_dev_cap.max_oq_elements = capability->max_oq_elements;
	softs->pqi_dev_cap.max_oq_elem_len = capability->max_oq_elem_len;
	softs->pqi_dev_cap.intr_coales_time_granularity = capability->intr_coales_time_granularity;

	iu_layer_desc = &capability->iu_layer_desc[PQI_PROTOCOL_SOP];
	softs->max_ib_iu_length_per_fw = iu_layer_desc->max_ib_iu_len;
	softs->ib_spanning_supported = iu_layer_desc->ib_spanning_supported;
	softs->ob_spanning_supported = iu_layer_desc->ob_spanning_supported;

	DBG_INIT("softs->pqi_dev_cap.max_iqs: %d\n", softs->pqi_dev_cap.max_iqs);
	DBG_INIT("softs->pqi_dev_cap.max_iq_elements: %d\n", softs->pqi_dev_cap.max_iq_elements);
	DBG_INIT("softs->pqi_dev_cap.max_iq_elem_len: %d\n", softs->pqi_dev_cap.max_iq_elem_len);
	DBG_INIT("softs->pqi_dev_cap.min_iq_elem_len: %d\n", softs->pqi_dev_cap.min_iq_elem_len);
	DBG_INIT("softs->pqi_dev_cap.max_oqs: %d\n", softs->pqi_dev_cap.max_oqs);
	DBG_INIT("softs->pqi_dev_cap.max_oq_elements: %d\n", softs->pqi_dev_cap.max_oq_elements);
	DBG_INIT("softs->pqi_dev_cap.max_oq_elem_len: %d\n", softs->pqi_dev_cap.max_oq_elem_len);
	DBG_INIT("softs->pqi_dev_cap.intr_coales_time_granularity: %d\n", softs->pqi_dev_cap.intr_coales_time_granularity);
	DBG_INIT("softs->max_ib_iu_length_per_fw: %d\n", softs->max_ib_iu_length_per_fw);
	DBG_INIT("softs->ib_spanning_supported: %d\n", softs->ib_spanning_supported);
	DBG_INIT("softs->ob_spanning_supported: %d\n", softs->ob_spanning_supported);
	

	os_mem_free(softs, (void *)capability,
		    REPORT_PQI_DEV_CAP_DATA_BUF_SIZE);
	os_dma_mem_free(softs, &pqi_cap_dma_buf);

	DBG_FUNC("OUT\n");
	return ret;

err_admin_req:
	os_dma_mem_free(softs, &pqi_cap_dma_buf);
err_dma_alloc:
	if (capability)
		os_mem_free(softs, (void *)capability,
			    REPORT_PQI_DEV_CAP_DATA_BUF_SIZE);
err_out:
	DBG_FUNC("failed OUT\n");
	return PQI_STATUS_FAILURE;
}

/*
 * Function used to deallocate the used rcb.
 */
void pqisrc_free_rcb(pqisrc_softstate_t *softs, int req_count)
{
	
	uint32_t num_req;
	size_t size;
	int i;

	DBG_FUNC("IN\n");
	num_req = softs->max_outstanding_io + 1;
	size = num_req * sizeof(rcb_t);
	for (i = 1; i < req_count; i++)
		os_dma_mem_free(softs, &softs->sg_dma_desc[i]);
	os_mem_free(softs, (void *)softs->rcb, size);
	softs->rcb = NULL;
	DBG_FUNC("OUT\n");
}


/*
 * Allocate memory for rcb and SG descriptors.
 */
static int pqisrc_allocate_rcb(pqisrc_softstate_t *softs)
{
	int ret = PQI_STATUS_SUCCESS;
	int i = 0;
	uint32_t num_req = 0;
	uint32_t sg_buf_size = 0;
	uint64_t alloc_size = 0;
	rcb_t *rcb = NULL;
	rcb_t *prcb = NULL;
	DBG_FUNC("IN\n");

	/* Set maximum outstanding requests */
	/* The valid tag values are from 1, 2, ..., softs->max_outstanding_io
	 * The rcb will be accessed by using the tag as index
	 * * As 0 tag index is not used, we need to allocate one extra.
	 */
	softs->max_outstanding_io = softs->pqi_cap.max_outstanding_io;
	num_req = softs->max_outstanding_io + 1;
	DBG_INIT("Max Outstanding IO reset to %d\n", num_req);

	alloc_size = num_req * sizeof(rcb_t);

	/* Allocate Non DMA memory */
	rcb = os_mem_alloc(softs, alloc_size);
	if (!rcb) {
		DBG_ERR("Failed to allocate memory for rcb\n");
		ret = PQI_STATUS_FAILURE;
		goto err_out;
	}
	softs->rcb = rcb;
	
	/* Allocate sg dma memory for sg chain  */
	sg_buf_size = softs->pqi_cap.max_sg_elem *
			sizeof(sgt_t);

	prcb = &softs->rcb[1];
	/* Initialize rcb */
	for(i=1; i < num_req; i++) {
		char tag[15];
		sprintf(tag, "sg_dma_buf%d", i);
		softs->sg_dma_desc[i].tag = tag;
		softs->sg_dma_desc[i].size = sg_buf_size;
		softs->sg_dma_desc[i].align = PQISRC_DEFAULT_DMA_ALIGN;

		ret = os_dma_mem_alloc(softs, &softs->sg_dma_desc[i]);
		if (ret) {
			DBG_ERR("Failed to Allocate sg desc %d\n", ret);
			ret = PQI_STATUS_FAILURE;
			goto error;
		}
		prcb->sg_chain_virt = (sgt_t *)(softs->sg_dma_desc[i].virt_addr);
		prcb->sg_chain_dma = (dma_addr_t)(softs->sg_dma_desc[i].dma_addr);
		prcb ++;
	}

	DBG_FUNC("OUT\n");
	return ret;
error:
	pqisrc_free_rcb(softs, i);
err_out:
	DBG_FUNC("failed OUT\n");
	return ret;
}

/*
 * Function used to decide the operational queue configuration params
 * - no of ibq/obq, shared/non-shared interrupt resource, IU spanning support
 */
void pqisrc_decide_opq_config(pqisrc_softstate_t *softs)
{
	uint16_t total_iq_elements;

	DBG_FUNC("IN\n");

	DBG_INIT("softs->intr_count : %d  softs->num_cpus_online : %d", 
		softs->intr_count, softs->num_cpus_online);
	
	if (softs->intr_count == 1 || softs->num_cpus_online == 1) {
		/* Share the event and Operational queue. */
		softs->num_op_obq = 1;
		softs->share_opq_and_eventq = true;
	}
	else {
		/* Note :  One OBQ (OBQ0) reserved for event queue */
		softs->num_op_obq = MIN(softs->num_cpus_online, 
					softs->intr_count) - 1;
		softs->num_op_obq = softs->intr_count - 1;
		softs->share_opq_and_eventq = false;
	}

	/* 
	 * softs->num_cpus_online is set as number of physical CPUs,
	 * So we can have more queues/interrupts .
	 */
	if (softs->intr_count > 1)	 
		softs->share_opq_and_eventq = false;
	
	DBG_INIT("softs->num_op_obq : %d\n",softs->num_op_obq);

	softs->num_op_raid_ibq = softs->num_op_obq;
	softs->num_op_aio_ibq = softs->num_op_raid_ibq;
	softs->ibq_elem_size =  softs->pqi_dev_cap.max_iq_elem_len * 16;
	softs->obq_elem_size = softs->pqi_dev_cap.max_oq_elem_len * 16;
	if (softs->max_ib_iu_length_per_fw == 256 &&
	    softs->ob_spanning_supported) {
		/* older f/w that doesn't actually support spanning. */
		softs->max_ib_iu_length = softs->ibq_elem_size;
	} else {
		/* max. inbound IU length is an multiple of our inbound element size. */
		softs->max_ib_iu_length =
			(softs->max_ib_iu_length_per_fw / softs->ibq_elem_size) *
			 softs->ibq_elem_size;
					   
	}
	/* If Max. Outstanding IO came with Max. Spanning element count then, 
		needed elements per IO are multiplication of
		Max.Outstanding IO and  Max.Spanning element */
	total_iq_elements = (softs->max_outstanding_io * 
		(softs->max_ib_iu_length / softs->ibq_elem_size));
	
	softs->num_elem_per_op_ibq = total_iq_elements / softs->num_op_raid_ibq;
	softs->num_elem_per_op_ibq = MIN(softs->num_elem_per_op_ibq, 
		softs->pqi_dev_cap.max_iq_elements);
	
	softs->num_elem_per_op_obq = softs->max_outstanding_io / softs->num_op_obq; 
	softs->num_elem_per_op_obq = MIN(softs->num_elem_per_op_obq,
		softs->pqi_dev_cap.max_oq_elements);

	softs->max_sg_per_iu = ((softs->max_ib_iu_length - 
				softs->ibq_elem_size) /
				sizeof(sgt_t)) +
				MAX_EMBEDDED_SG_IN_FIRST_IU;

	DBG_INIT("softs->max_ib_iu_length: %d\n", softs->max_ib_iu_length);
	DBG_INIT("softs->num_elem_per_op_ibq: %d\n", softs->num_elem_per_op_ibq);
	DBG_INIT("softs->num_elem_per_op_obq: %d\n", softs->num_elem_per_op_obq);
	DBG_INIT("softs->max_sg_per_iu: %d\n", softs->max_sg_per_iu);

	DBG_FUNC("OUT\n");
}

/*
 * Configure the operational queue parameters.
 */
int pqisrc_configure_op_queues(pqisrc_softstate_t *softs)
{
	int ret = PQI_STATUS_SUCCESS;
	
	/* Get the PQI capability, 
		REPORT PQI DEVICE CAPABILITY request */
	ret = pqisrc_report_pqi_capability(softs);
	if (ret) {
		DBG_ERR("Failed to send report pqi dev capability request : %d\n",
				ret);
		goto err_out;
	}

	/* Reserve required no of slots for internal requests */
	softs->max_io_for_scsi_ml = softs->max_outstanding_io - PQI_RESERVED_IO_SLOTS_CNT;

	/* Decide the Op queue configuration */
	pqisrc_decide_opq_config(softs);	
	
	DBG_FUNC("OUT\n");
	return ret;
		
err_out:
	DBG_FUNC("OUT failed\n");
	return ret;
}

/*
 * Validate the PQI mode of adapter.
 */
int pqisrc_check_pqimode(pqisrc_softstate_t *softs)
{
	int ret = PQI_STATUS_FAILURE;
	int tmo = 0;
	uint64_t signature = 0;

	DBG_FUNC("IN\n");

	/* Check the PQI device signature */
	tmo = PQISRC_PQIMODE_READY_TIMEOUT;
	do {
		signature = LE_64(PCI_MEM_GET64(softs, &softs->pqi_reg->signature, PQI_SIGNATURE));
        
		if (memcmp(&signature, PQISRC_PQI_DEVICE_SIGNATURE,
				sizeof(uint64_t)) == 0) {
			ret = PQI_STATUS_SUCCESS;
			break;
		}
		OS_SLEEP(PQISRC_MODE_READY_POLL_INTERVAL);
	} while (tmo--);

	PRINT_PQI_SIGNATURE(signature);

	if (tmo <= 0) {
		DBG_ERR("PQI Signature is invalid\n");
		ret = PQI_STATUS_TIMEOUT;
		goto err_out;
	}

	tmo = PQISRC_PQIMODE_READY_TIMEOUT;
	/* Check function and status code for the device */
	COND_WAIT((PCI_MEM_GET64(softs, &softs->pqi_reg->admin_q_config,
		PQI_ADMINQ_CONFIG) == PQI_ADMIN_QUEUE_CONF_FUNC_STATUS_IDLE), tmo);
	if (!tmo) {
		DBG_ERR("PQI device is not in IDLE state\n");
		ret = PQI_STATUS_TIMEOUT;
		goto err_out;
	}


	tmo = PQISRC_PQIMODE_READY_TIMEOUT;
	/* Check the PQI device status register */
	COND_WAIT(LE_32(PCI_MEM_GET32(softs, &softs->pqi_reg->pqi_dev_status, PQI_DEV_STATUS)) &
				PQI_DEV_STATE_AT_INIT, tmo);
	if (!tmo) {
		DBG_ERR("PQI Registers are not ready\n");
		ret = PQI_STATUS_TIMEOUT;
		goto err_out;
	}

	DBG_FUNC("OUT\n");
	return ret;
err_out:
	DBG_FUNC("OUT failed\n");
	return ret;
}

/*
 * Get the PQI configuration table parameters.
 * Currently using for heart-beat counter scratch-pad register.
 */
int pqisrc_process_config_table(pqisrc_softstate_t *softs)
{
	int ret = PQI_STATUS_FAILURE;
	uint32_t config_table_size;
	uint32_t section_off;
	uint8_t *config_table_abs_addr;
	struct pqi_conf_table *conf_table;
	struct pqi_conf_table_section_header *section_hdr;

	config_table_size = softs->pqi_cap.conf_tab_sz;

	if (config_table_size < sizeof(*conf_table) ||
		config_table_size > PQI_CONF_TABLE_MAX_LEN) {
		DBG_ERR("Invalid PQI conf table length of %u\n",
			config_table_size);
		return ret;
	}

	conf_table = os_mem_alloc(softs, config_table_size);
	if (!conf_table) {
		DBG_ERR("Failed to allocate memory for PQI conf table\n");
		return ret;
	}

	config_table_abs_addr = (uint8_t *)(softs->pci_mem_base_vaddr +
					softs->pqi_cap.conf_tab_off);

	PCI_MEM_GET_BUF(softs, config_table_abs_addr,
			softs->pqi_cap.conf_tab_off,
			(uint8_t*)conf_table, config_table_size);


	if (memcmp(conf_table->sign, PQI_CONF_TABLE_SIGNATURE,
			sizeof(conf_table->sign)) != 0) {
		DBG_ERR("Invalid PQI config signature\n");
		goto out;
	}

	section_off = LE_32(conf_table->first_section_off);

	while (section_off) {

		if (section_off+ sizeof(*section_hdr) >= config_table_size) {
			DBG_ERR("PQI config table section offset (%u) beyond \
			end of config table (config table length: %u)\n",
					section_off, config_table_size);
			break;
		}
		
		section_hdr = (struct pqi_conf_table_section_header *)((uint8_t *)conf_table + section_off);
		
		switch (LE_16(section_hdr->section_id)) {
		case PQI_CONF_TABLE_SECTION_GENERAL_INFO:
		case PQI_CONF_TABLE_SECTION_FIRMWARE_FEATURES:
		case PQI_CONF_TABLE_SECTION_FIRMWARE_ERRATA:
		case PQI_CONF_TABLE_SECTION_DEBUG:
		break;
		case PQI_CONF_TABLE_SECTION_HEARTBEAT:
		softs->heartbeat_counter_off = softs->pqi_cap.conf_tab_off +
						section_off +
						offsetof(struct pqi_conf_table_heartbeat,
						heartbeat_counter);
		softs->heartbeat_counter_abs_addr = (uint64_t *)(softs->pci_mem_base_vaddr +
							softs->heartbeat_counter_off);
		ret = PQI_STATUS_SUCCESS;
		break;
		default:
		DBG_ERR("unrecognized PQI config table section ID: 0x%x\n",
					LE_16(section_hdr->section_id));
		break;
		}
		section_off = LE_16(section_hdr->next_section_off);
	}
out:
	os_mem_free(softs, (void *)conf_table,config_table_size);
	return ret;
}

/* Wait for PQI reset completion for the adapter*/
int pqisrc_wait_for_pqi_reset_completion(pqisrc_softstate_t *softs)
{
	int ret = PQI_STATUS_SUCCESS;
	pqi_reset_reg_t reset_reg;
	int pqi_reset_timeout = 0;
	uint64_t val = 0;
	uint32_t max_timeout = 0;

	val = PCI_MEM_GET64(softs, &softs->pqi_reg->pqi_dev_adminq_cap, PQI_ADMINQ_CAP);

	max_timeout = (val & 0xFFFF00000000) >> 32;

	DBG_INIT("max_timeout for PQI reset completion in 100 msec units = %u\n", max_timeout);

	while(1) {
		if (pqi_reset_timeout++ == max_timeout) {
			return PQI_STATUS_TIMEOUT; 
		}
		OS_SLEEP(PQI_RESET_POLL_INTERVAL);/* 100 msec */
		reset_reg.all_bits = PCI_MEM_GET32(softs,
			&softs->pqi_reg->dev_reset, PQI_DEV_RESET);
		if (reset_reg.bits.reset_action == PQI_RESET_ACTION_COMPLETED)
			break;
	}

	return ret;
}

/*
 * Function used to perform PQI hard reset.
 */
int pqi_reset(pqisrc_softstate_t *softs)
{
	int ret = PQI_STATUS_SUCCESS;
	uint32_t val = 0;
	pqi_reset_reg_t pqi_reset_reg;

	DBG_FUNC("IN\n");

	if (true == softs->ctrl_in_pqi_mode) { 
	
		if (softs->pqi_reset_quiesce_allowed) {
			val = PCI_MEM_GET32(softs, &softs->ioa_reg->host_to_ioa_db,
					LEGACY_SIS_IDBR);
			val |= SIS_PQI_RESET_QUIESCE;
			PCI_MEM_PUT32(softs, &softs->ioa_reg->host_to_ioa_db,
					LEGACY_SIS_IDBR, LE_32(val));
			ret = pqisrc_sis_wait_for_db_bit_to_clear(softs, SIS_PQI_RESET_QUIESCE);
			if (ret) {
				DBG_ERR("failed with error %d during quiesce\n", ret);
				return ret;
			}
		}

		pqi_reset_reg.all_bits = 0;
		pqi_reset_reg.bits.reset_type = PQI_RESET_TYPE_HARD_RESET;
		pqi_reset_reg.bits.reset_action = PQI_RESET_ACTION_RESET;

		PCI_MEM_PUT32(softs, &softs->pqi_reg->dev_reset, PQI_DEV_RESET,
			LE_32(pqi_reset_reg.all_bits));

		ret = pqisrc_wait_for_pqi_reset_completion(softs);
		if (ret) {
			DBG_ERR("PQI reset timed out: ret = %d!\n", ret);
			return ret;
		}
	}
	softs->ctrl_in_pqi_mode = false;
	DBG_FUNC("OUT\n");
	return ret;
}

/*
 * Initialize the adapter with supported PQI configuration.
 */
int pqisrc_pqi_init(pqisrc_softstate_t *softs)
{
	int ret = PQI_STATUS_SUCCESS;

	DBG_FUNC("IN\n");

	/* Check the PQI signature */
	ret = pqisrc_check_pqimode(softs);
	if(ret) {
		DBG_ERR("failed to switch to pqi\n");
                goto err_out;
	}

	PQI_SAVE_CTRL_MODE(softs, CTRL_PQI_MODE);
	softs->ctrl_in_pqi_mode = true;
	
	/* Get the No. of Online CPUs,NUMA/Processor config from OS */
	ret = os_get_processor_config(softs);
	if (ret) {
		DBG_ERR("Failed to get processor config from OS %d\n",
			ret);
		goto err_out;
	}
	
	softs->intr_type = INTR_TYPE_NONE;	

	/* Get the interrupt count, type, priority available from OS */
	ret = os_get_intr_config(softs);
	if (ret) {
		DBG_ERR("Failed to get interrupt config from OS %d\n",
			ret);
		goto err_out;
	}

	/*Enable/Set Legacy INTx Interrupt mask clear pqi register,
	 *if allocated interrupt is legacy type.
	 */
	if (INTR_TYPE_FIXED == softs->intr_type) {
		pqisrc_configure_legacy_intx(softs, true);
		sis_enable_intx(softs);
	}

	/* Create Admin Queue pair*/		
	ret = pqisrc_create_admin_queue(softs);
	if(ret) {
                DBG_ERR("Failed to configure admin queue\n");
                goto err_admin_queue;
    	}

	/* For creating event and IO operational queues we have to submit 
	   admin IU requests.So Allocate resources for submitting IUs */  
	     
	/* Allocate the request container block (rcb) */
	ret = pqisrc_allocate_rcb(softs);
	if (ret == PQI_STATUS_FAILURE) {
                DBG_ERR("Failed to allocate rcb \n");
                goto err_rcb;
    	}

	/* Allocate & initialize request id queue */
	ret = pqisrc_init_taglist(softs,&softs->taglist,
				softs->max_outstanding_io);
	if (ret) {
		DBG_ERR("Failed to allocate memory for request id q : %d\n",
			ret);
		goto err_taglist;
	}

	ret = pqisrc_configure_op_queues(softs);
	if (ret) {
			DBG_ERR("Failed to configure op queue\n");
			goto err_config_opq;
	}

	/* Create Operational queues */
	ret = pqisrc_create_op_queues(softs);
	if(ret) {
                DBG_ERR("Failed to create op queue\n");
                ret = PQI_STATUS_FAILURE;
                goto err_create_opq;
        }

	softs->ctrl_online = true;

	DBG_FUNC("OUT\n");
	return ret;

err_create_opq:
err_config_opq:
	pqisrc_destroy_taglist(softs,&softs->taglist);
err_taglist:
	pqisrc_free_rcb(softs, softs->max_outstanding_io + 1);		
err_rcb:
	pqisrc_destroy_admin_queue(softs);
err_admin_queue:
	os_free_intr_config(softs);
err_out:
	DBG_FUNC("OUT failed\n");
	return PQI_STATUS_FAILURE;
}

/* */
int pqisrc_force_sis(pqisrc_softstate_t *softs)
{
	int ret = PQI_STATUS_SUCCESS;

	if (SIS_IS_KERNEL_PANIC(softs)) {
		DBG_INIT("Controller FW is not runnning");
		return PQI_STATUS_FAILURE;
	}

	if (PQI_GET_CTRL_MODE(softs) == CTRL_SIS_MODE) {
		return ret;
	}

	if (SIS_IS_KERNEL_UP(softs)) {
		PQI_SAVE_CTRL_MODE(softs, CTRL_SIS_MODE);
		return ret;
	}
	/* Disable interrupts ? */
	sis_disable_interrupt(softs);

	/* reset pqi, this will delete queues */
	ret = pqi_reset(softs);
	if (ret) {
		return ret;
	}	
	/* Re enable SIS */
	ret = pqisrc_reenable_sis(softs);
	if (ret) {
		return ret;
	}

	PQI_SAVE_CTRL_MODE(softs, CTRL_SIS_MODE);

	return ret;	
}

int pqisrc_wait_for_cmnd_complete(pqisrc_softstate_t *softs)
{
	int ret = PQI_STATUS_SUCCESS;
	int tmo = PQI_CMND_COMPLETE_TMO;
	
	COND_WAIT((softs->taglist.num_elem == softs->max_outstanding_io), tmo);
	if (!tmo) {
		DBG_ERR("Pending commands %x!!!",softs->taglist.num_elem);
		ret = PQI_STATUS_TIMEOUT;
	}
	return ret;
}

void pqisrc_complete_internal_cmds(pqisrc_softstate_t *softs)
{
	int tag = 0;
	rcb_t *rcb;
	
	for (tag = 1; tag <= softs->max_outstanding_io; tag++) {
		rcb = &softs->rcb[tag];
		if(rcb->req_pending && is_internal_req(rcb)) {
			rcb->status = REQUEST_FAILED;
			rcb->req_pending = false;
		}
	}
}

/*
 * Uninitialize the resources used during PQI initialization.
 */
void pqisrc_pqi_uninit(pqisrc_softstate_t *softs)
{
	int i, ret;

	DBG_FUNC("IN\n");
	
	/* Wait for any rescan to finish */
	pqisrc_wait_for_rescan_complete(softs);

	/* Wait for commands to complete */
	ret = pqisrc_wait_for_cmnd_complete(softs);
	
	/* Complete all pending commands. */
	if(ret != PQI_STATUS_SUCCESS) {
		pqisrc_complete_internal_cmds(softs);
		os_complete_outstanding_cmds_nodevice(softs);
	}

    if(softs->devlist_lockcreated==true){    
        os_uninit_spinlock(&softs->devlist_lock);
        softs->devlist_lockcreated = false;
    }
    
	for (i = 0; i <  softs->num_op_raid_ibq; i++) {
        /* OP RAID IB Q */
        if(softs->op_raid_ib_q[i].lockcreated==true){
		OS_UNINIT_PQILOCK(&softs->op_raid_ib_q[i].lock);
		softs->op_raid_ib_q[i].lockcreated = false;
        }
        
        /* OP AIO IB Q */
        if(softs->op_aio_ib_q[i].lockcreated==true){
		OS_UNINIT_PQILOCK(&softs->op_aio_ib_q[i].lock);
		softs->op_aio_ib_q[i].lockcreated = false;
        }
	}

	/* Free Op queues */
	os_dma_mem_free(softs, &softs->op_ibq_dma_mem);
	os_dma_mem_free(softs, &softs->op_obq_dma_mem);
	os_dma_mem_free(softs, &softs->event_q_dma_mem);
	
	/* Free  rcb */
	pqisrc_free_rcb(softs, softs->max_outstanding_io + 1);

	/* Free request id lists */
	pqisrc_destroy_taglist(softs,&softs->taglist);

	if(softs->admin_ib_queue.lockcreated==true){
		OS_UNINIT_PQILOCK(&softs->admin_ib_queue.lock);	
        	softs->admin_ib_queue.lockcreated = false;
	}

	/* Free Admin Queue */
	os_dma_mem_free(softs, &softs->admin_queue_dma_mem);

	/* Switch back to SIS mode */
	if (pqisrc_force_sis(softs)) {
		DBG_ERR("Failed to switch back the adapter to SIS mode!\n");
	}

	DBG_FUNC("OUT\n");
}

/*
 * Function to initialize the adapter settings.
 */
int pqisrc_init(pqisrc_softstate_t *softs)
{
	int ret = 0;
	int i = 0, j = 0;

	DBG_FUNC("IN\n");
    
	check_struct_sizes();
    
	/* Init the Sync interface */
	ret = pqisrc_sis_init(softs);
	if (ret) {
		DBG_ERR("SIS Init failed with error %d\n", ret);
		goto err_out;
	}

	ret = os_create_semaphore("scan_lock", 1, &softs->scan_lock);
	if(ret != PQI_STATUS_SUCCESS){
		DBG_ERR(" Failed to initialize scan lock\n");
		goto err_scan_lock;
	}

	/* Init the PQI interface */
	ret = pqisrc_pqi_init(softs);
	if (ret) {
		DBG_ERR("PQI Init failed with error %d\n", ret);
		goto err_pqi;
	}

	/* Setup interrupt */
	ret = os_setup_intr(softs);
	if (ret) {
		DBG_ERR("Interrupt setup failed with error %d\n", ret);
		goto err_intr;
	}

	/* Report event configuration */
        ret = pqisrc_report_event_config(softs);
        if(ret){
                DBG_ERR(" Failed to configure Report events\n");
		goto err_event;
	}
	 
	/* Set event configuration*/
        ret = pqisrc_set_event_config(softs);
        if(ret){
                DBG_ERR(" Failed to configure Set events\n");
                goto err_event;
        }

	/* Check for For PQI spanning */
	ret = pqisrc_get_ctrl_fw_version(softs);
        if(ret){
                DBG_ERR(" Failed to get ctrl fw version\n");
		goto err_fw_version;
        }

	/* update driver version in to FW */
	ret = pqisrc_write_driver_version_to_host_wellness(softs);
	if (ret) {
		DBG_ERR(" Failed to update driver version in to FW");
		goto err_host_wellness;
	}

    
	os_strlcpy(softs->devlist_lock_name, "devlist_lock", LOCKNAME_SIZE);
	ret = os_init_spinlock(softs, &softs->devlist_lock, softs->devlist_lock_name);
	if(ret){
		DBG_ERR(" Failed to initialize devlist_lock\n");
		softs->devlist_lockcreated=false;
		goto err_lock;
	}
	softs->devlist_lockcreated = true;
	
	OS_ATOMIC64_SET(softs, num_intrs, 0);
	softs->prev_num_intrs = softs->num_intrs;


	/* Get the PQI configuration table to read heart-beat counter*/
	if (PQI_NEW_HEARTBEAT_MECHANISM(softs)) {
		ret = pqisrc_process_config_table(softs);
		if (ret) {
			DBG_ERR("Failed to process PQI configuration table %d\n", ret);
			goto err_config_tab;
		}
	}

	if (PQI_NEW_HEARTBEAT_MECHANISM(softs))
		softs->prev_heartbeat_count = CTRLR_HEARTBEAT_CNT(softs) - OS_FW_HEARTBEAT_TIMER_INTERVAL;
	
	/* Init device list */
	for(i = 0; i < PQI_MAX_DEVICES; i++)
		for(j = 0; j < PQI_MAX_MULTILUN; j++)
			softs->device_list[i][j] = NULL;

	pqisrc_init_targetid_pool(softs);

	DBG_FUNC("OUT\n");
	return ret;

err_config_tab:
	if(softs->devlist_lockcreated==true){    
		os_uninit_spinlock(&softs->devlist_lock);
		softs->devlist_lockcreated = false;
	}	
err_lock:
err_fw_version:
err_event:
err_host_wellness:
	os_destroy_intr(softs);
err_intr:
	pqisrc_pqi_uninit(softs);
err_pqi:
	os_destroy_semaphore(&softs->scan_lock);
err_scan_lock:
	pqisrc_sis_uninit(softs);
err_out:
	DBG_FUNC("OUT failed\n");
	return ret;
}

/*
 * Write all data in the adapter's battery-backed cache to
 * storage.
 */
int pqisrc_flush_cache( pqisrc_softstate_t *softs,
			enum pqisrc_flush_cache_event_type event_type)
{
	int rval = PQI_STATUS_SUCCESS;
	pqisrc_raid_req_t request;
	pqisrc_bmic_flush_cache_t *flush_buff = NULL;

	DBG_FUNC("IN\n");

	if (pqisrc_ctrl_offline(softs))
		return PQI_STATUS_FAILURE;

	flush_buff = os_mem_alloc(softs, sizeof(pqisrc_bmic_flush_cache_t)); 
	if (!flush_buff) {
		DBG_ERR("Failed to allocate memory for flush cache params\n");
		rval = PQI_STATUS_FAILURE;
		return rval;
	}

	flush_buff->halt_event = event_type;

	memset(&request, 0, sizeof(request));

	rval = pqisrc_build_send_raid_request(softs, &request, flush_buff,
			sizeof(*flush_buff), SA_CACHE_FLUSH, 0,
			(uint8_t *)RAID_CTLR_LUNID, NULL);
	if (rval) {
		DBG_ERR("error in build send raid req ret=%d\n", rval);
	}

	if (flush_buff)
		os_mem_free(softs, (void *)flush_buff,
			sizeof(pqisrc_bmic_flush_cache_t));

	DBG_FUNC("OUT\n");

	return rval;
}

/*
 * Uninitialize the adapter.
 */
void pqisrc_uninit(pqisrc_softstate_t *softs)
{
	DBG_FUNC("IN\n");
	
	pqisrc_pqi_uninit(softs);

	pqisrc_sis_uninit(softs);

	os_destroy_semaphore(&softs->scan_lock);
	
	os_destroy_intr(softs);

	pqisrc_cleanup_devices(softs);

	DBG_FUNC("OUT\n");
}
