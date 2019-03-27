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
 * Function used to validate the adapter health.
 */
boolean_t pqisrc_ctrl_offline(pqisrc_softstate_t *softs)
{
	DBG_FUNC("IN\n");

	DBG_FUNC("OUT\n");

	return !softs->ctrl_online;
}

/* Function used set/clear legacy INTx bit in Legacy Interrupt INTx
 * mask clear pqi register
 */
void pqisrc_configure_legacy_intx(pqisrc_softstate_t *softs, boolean_t enable_intx)
{
	uint32_t intx_mask;
	uint32_t *reg_addr = NULL;
	
	DBG_FUNC("IN\n");
	
	if (enable_intx)
		reg_addr = &softs->pqi_reg->legacy_intr_mask_clr;
	else
		reg_addr = &softs->pqi_reg->legacy_intr_mask_set;
	
	intx_mask = PCI_MEM_GET32(softs, reg_addr, PQI_LEGACY_INTR_MASK_CLR);
	intx_mask |= PQISRC_LEGACY_INTX_MASK;
	PCI_MEM_PUT32(softs, reg_addr, PQI_LEGACY_INTR_MASK_CLR ,intx_mask);
	
	DBG_FUNC("OUT\n");
}

/*
 * Function used to take exposed devices to OS as offline.
 */
void pqisrc_take_devices_offline(pqisrc_softstate_t *softs)
{
	pqi_scsi_dev_t *device = NULL;
	int i,j;

	DBG_FUNC("IN\n");
	for(i = 0; i < PQI_MAX_DEVICES; i++) {
		for(j = 0; j < PQI_MAX_MULTILUN; j++) {
			if(softs->device_list[i][j] == NULL)
				continue;
			device = softs->device_list[i][j];
			pqisrc_remove_device(softs, device);
		}
	}

	DBG_FUNC("OUT\n");
}

/*
 * Function used to take adapter offline.
 */
void pqisrc_take_ctrl_offline(pqisrc_softstate_t *softs)
{

	DBG_FUNC("IN\n");

	softs->ctrl_online = false;
	pqisrc_trigger_nmi_sis(softs);
	os_complete_outstanding_cmds_nodevice(softs);
	pqisrc_take_devices_offline(softs);

	DBG_FUNC("OUT\n");
}

/*
 * Timer handler for the adapter heart-beat.
 */
void pqisrc_heartbeat_timer_handler(pqisrc_softstate_t *softs)
{
	uint64_t num_intrs;
	uint8_t take_offline = false;

	DBG_FUNC("IN\n");

	num_intrs = OS_ATOMIC64_READ(softs, num_intrs);

	if (PQI_NEW_HEARTBEAT_MECHANISM(softs)) {
		if (CTRLR_HEARTBEAT_CNT(softs) == softs->prev_heartbeat_count) {
			take_offline = true;
			goto take_ctrl_offline;
		}
		softs->prev_heartbeat_count = CTRLR_HEARTBEAT_CNT(softs);
		DBG_INFO("CTRLR_HEARTBEAT_CNT(softs)  = %lx \
		softs->prev_heartbeat_count = %lx\n",
		CTRLR_HEARTBEAT_CNT(softs), softs->prev_heartbeat_count);
	} else {
		if (num_intrs == softs->prev_num_intrs) {
			softs->num_heartbeats_requested++;
			if (softs->num_heartbeats_requested > PQI_MAX_HEARTBEAT_REQUESTS) {
				take_offline = true;
				goto take_ctrl_offline;
			}
			softs->pending_events[PQI_EVENT_HEARTBEAT].pending = true;

			pqisrc_ack_all_events((void*)softs);

		} else {
			softs->num_heartbeats_requested = 0;
		}
		softs->prev_num_intrs = num_intrs;
	}

take_ctrl_offline:
	if (take_offline){
		DBG_ERR("controller is offline\n");
		pqisrc_take_ctrl_offline(softs);
		os_stop_heartbeat_timer(softs);
	}
	DBG_FUNC("OUT\n");
}

/*
 * Conditional variable management routine for internal commands.
 */
int pqisrc_wait_on_condition(pqisrc_softstate_t *softs, rcb_t *rcb){

	DBG_FUNC("IN\n");

	int ret = PQI_STATUS_SUCCESS;
	uint32_t loop_cnt = 0;
	
	while (rcb->req_pending == true) {
		OS_SLEEP(500); /* Micro sec */

		/*Polling needed for FreeBSD : since ithread routine is not scheduled
                during bootup, we could use polling until interrupts are
                enabled (using 'if (cold)'to check for the boot time before
                interrupts are enabled). */
		IS_POLLING_REQUIRED(softs);

		if (loop_cnt++ == PQISRC_CMD_TIMEOUT_CNT) {
			DBG_ERR("ERR: Requested cmd timed out !!!\n");
			ret = PQI_STATUS_TIMEOUT;
			break;
		}
	
		if (pqisrc_ctrl_offline(softs)) {
			DBG_ERR("Controller is Offline");
			ret = PQI_STATUS_FAILURE;
			break;
		}

	}
	rcb->req_pending = true;

	DBG_FUNC("OUT\n");

	return ret;
}

/* Function used to validate the device wwid. */
boolean_t pqisrc_device_equal(pqi_scsi_dev_t *dev1,
	pqi_scsi_dev_t *dev2)
{
	return dev1->wwid == dev2->wwid;
}

/* Function used to validate the device scsi3addr. */
boolean_t pqisrc_scsi3addr_equal(uint8_t *scsi3addr1, uint8_t *scsi3addr2)
{
	return memcmp(scsi3addr1, scsi3addr2, 8) == 0;
}

/* Function used to validate hba_lunid */
boolean_t pqisrc_is_hba_lunid(uint8_t *scsi3addr)
{
	return pqisrc_scsi3addr_equal(scsi3addr, (uint8_t*)RAID_CTLR_LUNID);
}

/* Function used to validate type of device */
boolean_t pqisrc_is_logical_device(pqi_scsi_dev_t *device)
{
	return !device->is_physical_device;
}

/* Function used to sanitize inquiry string */
void pqisrc_sanitize_inquiry_string(unsigned char *s, int len)
{
	boolean_t terminated = false;

	DBG_FUNC("IN\n");

	for (; len > 0; (--len, ++s)) {
		if (*s == 0)
			terminated = true;
		if (terminated || *s < 0x20 || *s > 0x7e)
			*s = ' ';
	}

	DBG_FUNC("OUT\n");
}

static char *raid_levels[] = {
	"RAID 0",
	"RAID 4",
	"RAID 1(1+0)",
	"RAID 5",
	"RAID 5+1",
	"RAID ADG",
	"RAID 1(ADM)",
};

/* Get the RAID level from the index */
char *pqisrc_raidlevel_to_string(uint8_t raid_level)
{
	DBG_FUNC("IN\n");
	if (raid_level < ARRAY_SIZE(raid_levels))
		return raid_levels[raid_level];
	DBG_FUNC("OUT\n");

	return " ";
}

/* Debug routine for displaying device info */
void pqisrc_display_device_info(pqisrc_softstate_t *softs,
	char *action, pqi_scsi_dev_t *device)
{
	DBG_INFO( "%s scsi BTL %d:%d:%d:  %.8s %.16s %-12s SSDSmartPathCap%c En%c Exp%c qd=%d\n",
		action,
		device->bus,
		device->target,
		device->lun,
		device->vendor,
		device->model,
		pqisrc_raidlevel_to_string(device->raid_level),
		device->offload_config ? '+' : '-',
		device->offload_enabled_pending ? '+' : '-',
		device->expose_device ? '+' : '-',
		device->queue_depth);
	pqisrc_raidlevel_to_string(device->raid_level); /* To use this function */
}

/* validate the structure sizes */
void check_struct_sizes()
{   
    
    ASSERT(sizeof(SCSI3Addr_struct)== 2);
    ASSERT(sizeof(PhysDevAddr_struct) == 8);
    ASSERT(sizeof(LogDevAddr_struct)== 8);
    ASSERT(sizeof(LUNAddr_struct)==8);
    ASSERT(sizeof(RequestBlock_struct) == 20);
    ASSERT(sizeof(MoreErrInfo_struct)== 8);
    ASSERT(sizeof(ErrorInfo_struct)== 48);
    ASSERT(sizeof(IOCTL_Command_struct)== 86);
    ASSERT(sizeof(struct bmic_host_wellness_driver_version)== 42);
    ASSERT(sizeof(struct bmic_host_wellness_time)== 20);
    ASSERT(sizeof(struct pqi_dev_adminq_cap)== 8);
    ASSERT(sizeof(struct admin_q_param)== 4);
    ASSERT(sizeof(struct pqi_registers)== 256);
    ASSERT(sizeof(struct ioa_registers)== 4128);
    ASSERT(sizeof(struct pqi_pref_settings)==4);
    ASSERT(sizeof(struct pqi_cap)== 20);
    ASSERT(sizeof(iu_header_t)== 4);
    ASSERT(sizeof(gen_adm_req_iu_t)== 64);
    ASSERT(sizeof(gen_adm_resp_iu_t)== 64);
    ASSERT(sizeof(op_q_params) == 9);
    ASSERT(sizeof(raid_path_error_info_elem_t)== 276);
    ASSERT(sizeof(aio_path_error_info_elem_t)== 276);
    ASSERT(sizeof(struct init_base_struct)== 24);
    ASSERT(sizeof(pqi_iu_layer_desc_t)== 16);
    ASSERT(sizeof(pqi_dev_cap_t)== 576);
    ASSERT(sizeof(pqi_aio_req_t)== 128);
    ASSERT(sizeof(pqisrc_raid_req_t)== 128);
    ASSERT(sizeof(pqi_tmf_req_t)== 32);
    ASSERT(sizeof(struct pqi_io_response)== 16);
    ASSERT(sizeof(struct sense_header_scsi)== 8);
    ASSERT(sizeof(reportlun_header_t)==8);
    ASSERT(sizeof(reportlun_ext_entry_t)== 24);
    ASSERT(sizeof(reportlun_data_ext_t)== 32);
    ASSERT(sizeof(raidmap_data_t)==8);
    ASSERT(sizeof(pqisrc_raid_map_t)== 8256);
    ASSERT(sizeof(bmic_ident_ctrl_t)== 325);
    ASSERT(sizeof(bmic_ident_physdev_t)==2048);
  
}
