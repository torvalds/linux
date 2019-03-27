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

/* */
void sis_disable_msix(pqisrc_softstate_t *softs)
{
	uint32_t db_reg;

	DBG_FUNC("IN\n");

	db_reg = PCI_MEM_GET32(softs, &softs->ioa_reg->host_to_ioa_db,
			LEGACY_SIS_IDBR);
	db_reg &= ~SIS_ENABLE_MSIX;
	PCI_MEM_PUT32(softs, &softs->ioa_reg->host_to_ioa_db,
			LEGACY_SIS_IDBR, db_reg);

	DBG_FUNC("OUT\n");
}

void sis_enable_intx(pqisrc_softstate_t *softs)
{
	uint32_t db_reg;

	DBG_FUNC("IN\n");

	db_reg = PCI_MEM_GET32(softs, &softs->ioa_reg->host_to_ioa_db,
			LEGACY_SIS_IDBR);
	db_reg |= SIS_ENABLE_INTX;
	PCI_MEM_PUT32(softs, &softs->ioa_reg->host_to_ioa_db,
			LEGACY_SIS_IDBR, db_reg);
	if (pqisrc_sis_wait_for_db_bit_to_clear(softs,SIS_ENABLE_INTX) 
			!= PQI_STATUS_SUCCESS) {
		DBG_ERR("Failed to wait for enable intx db bit to clear\n");
	}
	DBG_FUNC("OUT\n");
}

void sis_disable_intx(pqisrc_softstate_t *softs)
{
	uint32_t db_reg;

	DBG_FUNC("IN\n");

	db_reg = PCI_MEM_GET32(softs, &softs->ioa_reg->host_to_ioa_db,
			LEGACY_SIS_IDBR);
	db_reg &= ~SIS_ENABLE_INTX;
	PCI_MEM_PUT32(softs, &softs->ioa_reg->host_to_ioa_db,
			LEGACY_SIS_IDBR, db_reg);

	DBG_FUNC("OUT\n");
}

void sis_disable_interrupt(pqisrc_softstate_t *softs)
{
	DBG_FUNC("IN");
	
	switch(softs->intr_type) {
		case INTR_TYPE_FIXED:
			pqisrc_configure_legacy_intx(softs,false);
			sis_disable_intx(softs);
			break;
		case INTR_TYPE_MSI:
		case INTR_TYPE_MSIX:
			sis_disable_msix(softs);
			break;
		default:
			DBG_ERR("Inerrupt mode none!\n");
			break;
	}
	
	DBG_FUNC("OUT");
}

/* Trigger a NMI as part of taking controller offline procedure */
void pqisrc_trigger_nmi_sis(pqisrc_softstate_t *softs)
{

	DBG_FUNC("IN\n");

	PCI_MEM_PUT32(softs,  &softs->ioa_reg->host_to_ioa_db, 
			LEGACY_SIS_IDBR, LE_32(TRIGGER_NMI_SIS));
	DBG_FUNC("OUT\n");
}

/* Switch the adapter back to SIS mode during uninitialization */
int pqisrc_reenable_sis(pqisrc_softstate_t *softs)
{
	int ret = PQI_STATUS_SUCCESS;
	uint32_t timeout = SIS_ENABLE_TIMEOUT;

	DBG_FUNC("IN\n");

	PCI_MEM_PUT32(softs, &softs->ioa_reg->host_to_ioa_db, 
        LEGACY_SIS_IDBR, LE_32(REENABLE_SIS));

	COND_WAIT(((PCI_MEM_GET32(softs, &softs->ioa_reg->ioa_to_host_db, LEGACY_SIS_ODBR_R) &
				REENABLE_SIS) == 0), timeout)
	if (!timeout) {
		DBG_WARN(" [ %s ] failed to re enable sis\n",__func__);
		ret = PQI_STATUS_TIMEOUT;
	}
		
	DBG_FUNC("OUT\n");
	return ret;
}

/* Validate the FW status PQI_CTRL_KERNEL_UP_AND_RUNNING */
int pqisrc_check_fw_status(pqisrc_softstate_t *softs)
{
	int ret = PQI_STATUS_SUCCESS;
	uint32_t timeout = SIS_STATUS_OK_TIMEOUT;

	DBG_FUNC("IN\n");

	OS_SLEEP(1000000);
	COND_WAIT((GET_FW_STATUS(softs) &
		PQI_CTRL_KERNEL_UP_AND_RUNNING), timeout);
	if (!timeout) {
		DBG_ERR("FW check status timedout\n");
		ret = PQI_STATUS_TIMEOUT;
	}

	DBG_FUNC("OUT\n");
	return ret;
}

/* Function used to submit a SIS command to the adapter */
static int pqisrc_send_sis_cmd(pqisrc_softstate_t *softs,
					uint32_t *mb)
{
	int ret = PQI_STATUS_SUCCESS;
	int i = 0;
	uint32_t timeout = SIS_CMD_COMPLETE_TIMEOUT;

	int val;

	DBG_FUNC("IN\n");


	/* Copy Command to mailbox */
	for (i = 0; i < 6; i++)
		PCI_MEM_PUT32(softs, &softs->ioa_reg->mb[i], 
            LEGACY_SIS_SRCV_MAILBOX+i*4, LE_32(mb[i]));
    
	PCI_MEM_PUT32(softs, &softs->ioa_reg->ioa_to_host_db_clr, 
		LEGACY_SIS_ODBR_R, LE_32(0x1000));

	/* Submit the command */
	PCI_MEM_PUT32(softs, &softs->ioa_reg->host_to_ioa_db, 
		LEGACY_SIS_IDBR, LE_32(SIS_CMD_SUBMIT));

#ifdef SIS_POLL_WAIT
	/* Wait for 20  milli sec to poll */
	OS_BUSYWAIT(SIS_POLL_START_WAIT_TIME);
#endif

	val = PCI_MEM_GET32(softs, &softs->ioa_reg->ioa_to_host_db, LEGACY_SIS_ODBR_R);

	DBG_FUNC("val : %x\n",val);
	/* Spin waiting for the command to complete */
	COND_WAIT((PCI_MEM_GET32(softs, &softs->ioa_reg->ioa_to_host_db, LEGACY_SIS_ODBR_R) &
		SIS_CMD_COMPLETE), timeout);
	if (!timeout) {
		DBG_ERR("Sync command %x, timedout\n", mb[0]);
		ret = PQI_STATUS_TIMEOUT;
		goto err_out;
	}
	/* Check command status */
	mb[0] = LE_32(PCI_MEM_GET32(softs, &softs->ioa_reg->mb[0], LEGACY_SIS_SRCV_MAILBOX));

	if (mb[0] != SIS_CMD_STATUS_SUCCESS) {
		DBG_ERR("SIS cmd failed with status = 0x%x\n",
			mb[0]);
		ret = PQI_STATUS_FAILURE;
		goto err_out;
	}

	/* Copy the mailbox back  */
	for (i = 1; i < 6; i++)
		mb[i] =	LE_32(PCI_MEM_GET32(softs, &softs->ioa_reg->mb[i], LEGACY_SIS_SRCV_MAILBOX+i*4));

	DBG_FUNC("OUT\n");
	return ret;

err_out:
	DBG_FUNC("OUT failed\n");
	return ret;
}

/* First SIS command for the adapter to check PQI support */
int pqisrc_get_adapter_properties(pqisrc_softstate_t *softs,
				uint32_t *prop, uint32_t *ext_prop)
{
	int ret = PQI_STATUS_SUCCESS;
	uint32_t mb[6] = {0};

	DBG_FUNC("IN\n");

	mb[0] = SIS_CMD_GET_ADAPTER_PROPERTIES;
	ret = pqisrc_send_sis_cmd(softs, mb);
	if (!ret) {
		DBG_INIT("GET_PROPERTIES prop = %x, ext_prop = %x\n",
					mb[1], mb[4]);
		*prop = mb[1];
		*ext_prop = mb[4];
	}

	DBG_FUNC("OUT\n");
	return ret;
}

/* Second SIS command to the adapter GET_COMM_PREFERRED_SETTINGS */
int pqisrc_get_preferred_settings(pqisrc_softstate_t *softs)
{
	int ret = PQI_STATUS_SUCCESS;
	uint32_t mb[6] = {0};

	DBG_FUNC("IN\n");

	mb[0] = SIS_CMD_GET_COMM_PREFERRED_SETTINGS;
	ret = pqisrc_send_sis_cmd(softs, mb);
	if (!ret) {
		/* 31:16 maximum command size in KB */
		softs->pref_settings.max_cmd_size = mb[1] >> 16;
		/* 15:00: Maximum FIB size in bytes */
		softs->pref_settings.max_fib_size = mb[1] & 0x0000FFFF;
		DBG_INIT("cmd size = %x, fib size = %x\n",
			softs->pref_settings.max_cmd_size,
			softs->pref_settings.max_fib_size);
	}

	DBG_FUNC("OUT\n");
	return ret;
}

/* Get supported PQI capabilities from the adapter */
int pqisrc_get_sis_pqi_cap(pqisrc_softstate_t *softs)
{
	int ret = PQI_STATUS_SUCCESS;
	uint32_t mb[6] = {0};

	DBG_FUNC("IN\n");

	mb[0] = SIS_CMD_GET_PQI_CAPABILITIES;
	ret = pqisrc_send_sis_cmd(softs,  mb);
	if (!ret) {
		softs->pqi_cap.max_sg_elem = mb[1];
		softs->pqi_cap.max_transfer_size = mb[2];
		softs->pqi_cap.max_outstanding_io = mb[3];
		softs->pqi_cap.conf_tab_off = mb[4];
		softs->pqi_cap.conf_tab_sz =  mb[5];

		DBG_INIT("max_sg_elem = %x\n",
					softs->pqi_cap.max_sg_elem);
		DBG_INIT("max_transfer_size = %x\n",
					softs->pqi_cap.max_transfer_size);
		DBG_INIT("max_outstanding_io = %x\n",
					softs->pqi_cap.max_outstanding_io);
	}

	DBG_FUNC("OUT\n");
	return ret;
}

/* Send INIT STRUCT BASE ADDR - one of the SIS command */
int pqisrc_init_struct_base(pqisrc_softstate_t *softs)
{
	int ret = PQI_STATUS_SUCCESS;
	uint32_t elem_size = 0;
	uint32_t num_elem = 0;
	struct dma_mem init_struct_mem = {0};
	struct init_base_struct *init_struct = NULL;
	uint32_t mb[6] = {0};

	DBG_FUNC("IN\n");

	/* Allocate init struct */
	memset(&init_struct_mem, 0, sizeof(struct dma_mem));
	init_struct_mem.size = sizeof(struct init_base_struct);
	init_struct_mem.align = PQISRC_INIT_STRUCT_DMA_ALIGN;
	init_struct_mem.tag = "init_struct";
	ret = os_dma_mem_alloc(softs, &init_struct_mem);
	if (ret) {
		DBG_ERR("Failed to Allocate error buffer ret : %d\n",
			ret);
		goto err_out;
	}

	/* Calculate error buffer size */
	/* The valid tag values are from 1, 2, ..., softs->max_outstanding_io
	 * The rcb and error buffer will be accessed by using the tag as index
	 * As 0 tag  index is not used, we need to allocate one extra.
	 */
	num_elem = softs->pqi_cap.max_outstanding_io + 1;
	elem_size = PQISRC_ERR_BUF_ELEM_SIZE;
	softs->err_buf_dma_mem.size = num_elem * elem_size;

	/* Allocate error buffer */
	softs->err_buf_dma_mem.align = PQISRC_ERR_BUF_DMA_ALIGN;
	softs->err_buf_dma_mem.tag = "error_buffer";
	ret = os_dma_mem_alloc(softs, &softs->err_buf_dma_mem);
	if (ret) {
		DBG_ERR("Failed to Allocate error buffer ret : %d\n",
			ret);
		goto err_error_buf_alloc;
	}

	/* Fill init struct */
	init_struct = (struct init_base_struct *)DMA_TO_VIRT(&init_struct_mem);
	init_struct->revision = PQISRC_INIT_STRUCT_REVISION;
	init_struct->flags    = 0;
	init_struct->err_buf_paddr_l = DMA_PHYS_LOW(&softs->err_buf_dma_mem);
	init_struct->err_buf_paddr_h = DMA_PHYS_HIGH(&softs->err_buf_dma_mem);
	init_struct->err_buf_elem_len = elem_size;
	init_struct->err_buf_num_elem = num_elem;

	mb[0] = SIS_CMD_INIT_BASE_STRUCT_ADDRESS;
	mb[1] = DMA_PHYS_LOW(&init_struct_mem);
	mb[2] = DMA_PHYS_HIGH(&init_struct_mem);
	mb[3] = init_struct_mem.size;

	ret = pqisrc_send_sis_cmd(softs, mb);
	if (ret)
		goto err_sis_cmd;

	DBG_FUNC("OUT\n");
	os_dma_mem_free(softs, &init_struct_mem);
	return ret;

err_sis_cmd:
	os_dma_mem_free(softs, &softs->err_buf_dma_mem);
err_error_buf_alloc:
	os_dma_mem_free(softs, &init_struct_mem);
err_out:
	DBG_FUNC("OUT failed %d\n", ret);
	return PQI_STATUS_FAILURE;
}

/*
 * SIS initialization of the adapter in a sequence of
 * - GET_ADAPTER_PROPERTIES
 * - GET_COMM_PREFERRED_SETTINGS
 * - GET_PQI_CAPABILITIES
 * - INIT_STRUCT_BASE ADDR
 */
int pqisrc_sis_init(pqisrc_softstate_t *softs)
{
	int ret = PQI_STATUS_SUCCESS;
	uint32_t prop = 0;
	uint32_t ext_prop = 0;

	DBG_FUNC("IN\n");

	ret = pqisrc_force_sis(softs);
	if (ret) {
		DBG_ERR("Failed to switch back the adapter to SIS mode!\n");
		goto err_out;
	}

	/* Check FW status ready	*/
	ret = pqisrc_check_fw_status(softs);
	if (ret) {
		DBG_ERR("PQI Controller is not ready !!!\n");
		goto err_out;
	}

	/* Check For PQI support(19h) */
	ret = pqisrc_get_adapter_properties(softs, &prop, &ext_prop);
	if (ret) {
		DBG_ERR("Failed to get adapter properties\n");
		goto err_out;
	}
	if (!((prop & SIS_SUPPORT_EXT_OPT) &&
		(ext_prop & SIS_SUPPORT_PQI))) {
		DBG_ERR("PQI Mode Not Supported\n");
		ret = PQI_STATUS_FAILURE;
		goto err_out;
	}

	softs->pqi_reset_quiesce_allowed = false;
	if (ext_prop & SIS_SUPPORT_PQI_RESET_QUIESCE)
		softs->pqi_reset_quiesce_allowed = true;

	/* Send GET_COMM_PREFERRED_SETTINGS (26h)  */
	ret = pqisrc_get_preferred_settings(softs);
	if (ret) {
		DBG_ERR("Failed to get adapter pref settings\n");
		goto err_out;
	}

	/* Get PQI settings , 3000h*/
	ret = pqisrc_get_sis_pqi_cap(softs);
	if (ret) {
		DBG_ERR("Failed to get PQI Capabilities\n");
		goto err_out;
	}

	/* Init struct base addr */
	ret = pqisrc_init_struct_base(softs);
	if (ret) {
		DBG_ERR("Failed to set init struct base addr\n");
		goto err_out;
	}

	DBG_FUNC("OUT\n");
	return ret;

err_out:
	DBG_FUNC("OUT failed\n");
	return ret;
}

/* Deallocate the resources used during SIS initialization */
void pqisrc_sis_uninit(pqisrc_softstate_t *softs)
{
	DBG_FUNC("IN\n");

	os_dma_mem_free(softs, &softs->err_buf_dma_mem);
	os_resource_free(softs);
	pqi_reset(softs);

	DBG_FUNC("OUT\n");
}

int pqisrc_sis_wait_for_db_bit_to_clear(pqisrc_softstate_t *softs, uint32_t bit)
{
	int rcode = PQI_STATUS_SUCCESS;
	uint32_t db_reg;
	uint32_t loop_cnt = 0;

	DBG_FUNC("IN\n");

	while (1) {
		db_reg = PCI_MEM_GET32(softs, &softs->ioa_reg->host_to_ioa_db,
				LEGACY_SIS_IDBR);
		if ((db_reg & bit) == 0)
			break;
		if (GET_FW_STATUS(softs) & PQI_CTRL_KERNEL_PANIC) {
			DBG_ERR("controller kernel panic\n");
			rcode = PQI_STATUS_FAILURE;
			break;
		}
		if (loop_cnt++ == SIS_DB_BIT_CLEAR_TIMEOUT_CNT) {
			DBG_ERR("door-bell reg bit 0x%x not cleared\n", bit);
			rcode = PQI_STATUS_TIMEOUT;
			break;
		}
		OS_SLEEP(500);
	}

	DBG_FUNC("OUT\n");

	return rcode;
}
