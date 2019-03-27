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

/* Validate the scsi sense response code */
static inline boolean_t pqisrc_scsi_sense_valid(const struct sense_header_scsi *sshdr)
{
	DBG_FUNC("IN\n");

	if (!sshdr)
		return false;

	DBG_FUNC("OUT\n");

	return (sshdr->response_code & 0x70) == 0x70;
}

/* Initialize target ID pool for HBA/PDs */
void  pqisrc_init_targetid_pool(pqisrc_softstate_t *softs)
{
	int i, tid = PQI_MAX_PHYSICALS + PQI_MAX_LOGICALS - 1;

	for(i = 0; i < PQI_MAX_PHYSICALS; i++) {
		softs->tid_pool.tid[i] = tid--;
	}
	softs->tid_pool.index = i - 1;
}

int pqisrc_alloc_tid(pqisrc_softstate_t *softs)
{
	if(softs->tid_pool.index <= -1) {
		DBG_ERR("Target ID exhausted\n");
		return INVALID_ELEM;
	}
	
	return  softs->tid_pool.tid[softs->tid_pool.index--];
}

void pqisrc_free_tid(pqisrc_softstate_t *softs, int tid)
{
	if(softs->tid_pool.index >= PQI_MAX_PHYSICALS) {
		DBG_ERR("Target ID queue is full\n");
		return;
	}
	
	softs->tid_pool.index++;
	softs->tid_pool.tid[softs->tid_pool.index] = tid;
}

/* Update scsi sense info to a local buffer*/
boolean_t pqisrc_update_scsi_sense(const uint8_t *buff, int len,
			      struct sense_header_scsi *header)
{

	DBG_FUNC("IN\n");

	if (!buff || !len)
		return false;

	memset(header, 0, sizeof(struct sense_header_scsi));

	header->response_code = (buff[0] & 0x7f);

	if (!pqisrc_scsi_sense_valid(header))
		return false;

	if (header->response_code >= 0x72) {
		/* descriptor format */
		if (len > 1)
			header->sense_key = (buff[1] & 0xf);
		if (len > 2)
			header->asc = buff[2];
		if (len > 3)
			header->ascq = buff[3];
		if (len > 7)
			header->additional_length = buff[7];
	} else {
		 /* fixed format */
		if (len > 2)
			header->sense_key = (buff[2] & 0xf);
		if (len > 7) {
			len = (len < (buff[7] + 8)) ?
					len : (buff[7] + 8);
			if (len > 12)
				header->asc = buff[12];
			if (len > 13)
				header->ascq = buff[13];
		}
	}

	DBG_FUNC("OUT\n");

	return true;
}

/*
 * Function used to build the internal raid request and analyze the response
 */
int pqisrc_build_send_raid_request(pqisrc_softstate_t *softs,  pqisrc_raid_req_t *request,
			    void *buff, size_t datasize, uint8_t cmd, uint16_t vpd_page, uint8_t *scsi3addr,
			    raid_path_error_info_elem_t *error_info)
{
	
	uint8_t *cdb;
	int ret = PQI_STATUS_SUCCESS;
	uint32_t tag = 0;
	struct dma_mem device_mem;
	sgt_t *sgd;

	ib_queue_t *ib_q = &softs->op_raid_ib_q[PQI_DEFAULT_IB_QUEUE];
	ob_queue_t *ob_q = &softs->op_ob_q[PQI_DEFAULT_IB_QUEUE];

	rcb_t *rcb = NULL;
	
	DBG_FUNC("IN\n");

	memset(&device_mem, 0, sizeof(struct dma_mem));

	/* for TUR datasize: 0 buff: NULL */
	if (datasize) {
		device_mem.tag = "device_mem";
		device_mem.size = datasize;
		device_mem.align = PQISRC_DEFAULT_DMA_ALIGN;

		ret = os_dma_mem_alloc(softs, &device_mem);
	
		if (ret) {
			DBG_ERR("failed to allocate dma memory for device_mem return code %d\n", ret);
			return ret;
		}

		sgd = (sgt_t *)&request->sg_descriptors[0];

		sgd->addr = device_mem.dma_addr;
		sgd->len = datasize;
		sgd->flags = SG_FLAG_LAST;

	}

	/* Build raid path request */
	request->header.iu_type = PQI_IU_TYPE_RAID_PATH_IO_REQUEST;

	request->header.iu_length = LE_16(offsetof(pqisrc_raid_req_t,
							sg_descriptors[1]) - PQI_REQUEST_HEADER_LENGTH);
	request->buffer_length = LE_32(datasize);
	memcpy(request->lun_number, scsi3addr, sizeof(request->lun_number));
	request->task_attribute = SOP_TASK_ATTRIBUTE_SIMPLE;
	request->additional_cdb_bytes_usage = PQI_ADDITIONAL_CDB_BYTES_0;

	cdb = request->cdb;

	switch (cmd) {
	case SA_INQUIRY:
		request->data_direction = SOP_DATA_DIR_TO_DEVICE;
		cdb[0] = SA_INQUIRY;
		if (vpd_page & VPD_PAGE) {
			cdb[1] = 0x1;
			cdb[2] = (uint8_t)vpd_page;
		}
		cdb[4] = (uint8_t)datasize;
		break;
	case SA_REPORT_LOG:
	case SA_REPORT_PHYS:
		request->data_direction = SOP_DATA_DIR_TO_DEVICE;
		cdb[0] = cmd;
		if (cmd == SA_REPORT_PHYS)
			cdb[1] = SA_REPORT_PHYS_EXTENDED;
		else
		cdb[1] = SA_REPORT_LOG_EXTENDED;
		cdb[8] = (uint8_t)((datasize) >> 8);
		cdb[9] = (uint8_t)datasize;
		break;
	case TEST_UNIT_READY:
		request->data_direction = SOP_DATA_DIR_NONE;
		break;
	case SA_GET_RAID_MAP:
		request->data_direction = SOP_DATA_DIR_TO_DEVICE;
		cdb[0] = SA_CISS_READ;
		cdb[1] = cmd;
		cdb[8] = (uint8_t)((datasize) >> 8);
		cdb[9] = (uint8_t)datasize;
		break;
	case SA_CACHE_FLUSH:
		request->data_direction = SOP_DATA_DIR_FROM_DEVICE;
		memcpy(device_mem.virt_addr, buff, datasize);
		cdb[0] = BMIC_WRITE;
		cdb[6] = BMIC_CACHE_FLUSH;
		cdb[7] = (uint8_t)((datasize)  << 8);
		cdb[8] = (uint8_t)((datasize)  >> 8);
		break;
	case BMIC_IDENTIFY_CONTROLLER:
	case BMIC_IDENTIFY_PHYSICAL_DEVICE:
		request->data_direction = SOP_DATA_DIR_TO_DEVICE;
		cdb[0] = BMIC_READ;
		cdb[6] = cmd;
		cdb[7] = (uint8_t)((datasize)  << 8);
		cdb[8] = (uint8_t)((datasize)  >> 8);
		break;
	case BMIC_WRITE_HOST_WELLNESS:
		request->data_direction = SOP_DATA_DIR_FROM_DEVICE;
		memcpy(device_mem.virt_addr, buff, datasize);
		cdb[0] = BMIC_WRITE;
		cdb[6] = cmd;
		cdb[7] = (uint8_t)((datasize)  << 8);
		cdb[8] = (uint8_t)((datasize)  >> 8);
		break;
	case BMIC_SENSE_SUBSYSTEM_INFORMATION:
		request->data_direction = SOP_DATA_DIR_TO_DEVICE;
		cdb[0] = BMIC_READ;
		cdb[6] = cmd;
		cdb[7] = (uint8_t)((datasize)  << 8);
		cdb[8] = (uint8_t)((datasize)  >> 8);
		break;	
	default:
		DBG_ERR("unknown command 0x%x", cmd);
		break;
	}

	tag = pqisrc_get_tag(&softs->taglist);
	if (INVALID_ELEM == tag) {
		DBG_ERR("Tag not available\n");
		ret = PQI_STATUS_FAILURE;
		goto err_notag;
	}

	((pqisrc_raid_req_t *)request)->request_id = tag;
	((pqisrc_raid_req_t *)request)->error_index = ((pqisrc_raid_req_t *)request)->request_id;
	((pqisrc_raid_req_t *)request)->response_queue_id = ob_q->q_id;
	rcb = &softs->rcb[tag];
	rcb->success_cmp_callback = pqisrc_process_internal_raid_response_success;
	rcb->error_cmp_callback = pqisrc_process_internal_raid_response_error;

	rcb->req_pending = true;
	rcb->tag = tag;
	/* Submit Command */
	ret = pqisrc_submit_cmnd(softs, ib_q, request);

	if (ret != PQI_STATUS_SUCCESS) {
		DBG_ERR("Unable to submit command\n");
		goto err_out;
	}

	ret = pqisrc_wait_on_condition(softs, rcb);
	if (ret != PQI_STATUS_SUCCESS) {
		DBG_ERR("Internal RAID request timed out: cmd : 0x%c\n", cmd);
		goto err_out;
	}

	if (datasize) {
		if (buff) {
			memcpy(buff, device_mem.virt_addr, datasize);
		}
		os_dma_mem_free(softs, &device_mem);
	}
	
	ret = rcb->status;
	if (ret) {
		if(error_info) {
			memcpy(error_info, 
			       rcb->error_info,
			       sizeof(*error_info));

			if (error_info->data_out_result ==
			    PQI_RAID_DATA_IN_OUT_UNDERFLOW) {
				ret = PQI_STATUS_SUCCESS;
			}
			else{
				DBG_DISC("Error!! Bus=%u Target=%u, Cmd=0x%x," 
					"Ret=%d\n", BMIC_GET_LEVEL_2_BUS(scsi3addr), 
					BMIC_GET_LEVEL_TWO_TARGET(scsi3addr), 
					cmd, ret);
				ret = PQI_STATUS_FAILURE;
			}
		}
	} else {
		if(error_info) {
			ret = PQI_STATUS_SUCCESS;
			memset(error_info, 0, sizeof(*error_info));
		}
	}

	os_reset_rcb(rcb);
	pqisrc_put_tag(&softs->taglist, ((pqisrc_raid_req_t *)request)->request_id);
	DBG_FUNC("OUT\n");
	return ret;

err_out:
	DBG_ERR("Error!! Bus=%u Target=%u, Cmd=0x%x, Ret=%d\n", 
		BMIC_GET_LEVEL_2_BUS(scsi3addr), BMIC_GET_LEVEL_TWO_TARGET(scsi3addr), 
		cmd, ret);
	os_reset_rcb(rcb);
	pqisrc_put_tag(&softs->taglist, ((pqisrc_raid_req_t *)request)->request_id);
err_notag:
	if (datasize)
		os_dma_mem_free(softs, &device_mem);
	DBG_FUNC("FAILED \n");
	return ret;
}

/* common function used to send report physical and logical luns cmnds*/
static int pqisrc_report_luns(pqisrc_softstate_t *softs, uint8_t cmd,
	void *buff, size_t buf_len)
{
	int ret;
	pqisrc_raid_req_t request;

	DBG_FUNC("IN\n");

	memset(&request, 0, sizeof(request));
	ret =  pqisrc_build_send_raid_request(softs, &request, buff, 
				buf_len, cmd, 0, (uint8_t *)RAID_CTLR_LUNID, NULL);

	DBG_FUNC("OUT\n");

	return ret;
}

/* subroutine used to get physical and logical luns of the device */
static int pqisrc_get_physical_logical_luns(pqisrc_softstate_t *softs, uint8_t cmd,
		reportlun_data_ext_t **buff, size_t *data_length)
{
	int ret;
	size_t list_len;
	size_t data_len;
	size_t new_lun_list_length;
	reportlun_data_ext_t *lun_data;
	reportlun_header_t report_lun_header;

	DBG_FUNC("IN\n");

	ret = pqisrc_report_luns(softs, cmd, &report_lun_header,
		sizeof(report_lun_header));

	if (ret) {
		DBG_ERR("failed return code: %d\n", ret);
		return ret;
	}
	list_len = BE_32(report_lun_header.list_length);

retry:
	data_len = sizeof(reportlun_header_t) + list_len;
	*data_length = data_len;

	lun_data = os_mem_alloc(softs, data_len);

	if (!lun_data) {
		DBG_ERR("failed to allocate memory for lun_data\n");
		return PQI_STATUS_FAILURE;
	}
		
	if (list_len == 0) {
		DBG_DISC("list_len is 0\n");
		memcpy(lun_data, &report_lun_header, sizeof(report_lun_header));
		goto out;
	}

	ret = pqisrc_report_luns(softs, cmd, lun_data, data_len);

	if (ret) {
		DBG_ERR("error\n");
		goto error;
	}

	new_lun_list_length = BE_32(lun_data->header.list_length);

	if (new_lun_list_length > list_len) {
		list_len = new_lun_list_length;
		os_mem_free(softs, (void *)lun_data, data_len);
		goto retry;
	}

out:
	*buff = lun_data;
	DBG_FUNC("OUT\n");
	return 0;

error:
	os_mem_free(softs, (void *)lun_data, data_len);
	DBG_ERR("FAILED\n");
	return ret;
}

/*
 * Function used to get physical and logical device list
 */
static int pqisrc_get_phys_log_device_list(pqisrc_softstate_t *softs,
	reportlun_data_ext_t **physical_dev_list,
	reportlun_data_ext_t **logical_dev_list, 
	size_t *phys_data_length,
	size_t *log_data_length)
{
	int ret = PQI_STATUS_SUCCESS;
	size_t logical_list_length;
	size_t logdev_data_length;
	size_t data_length;
	reportlun_data_ext_t *local_logdev_list;
	reportlun_data_ext_t *logdev_data;
	reportlun_header_t report_lun_header;
	

	DBG_FUNC("IN\n");

	ret = pqisrc_get_physical_logical_luns(softs, SA_REPORT_PHYS, physical_dev_list, phys_data_length);
	if (ret) {
		DBG_ERR("report physical LUNs failed");
		return ret;
	}

	ret = pqisrc_get_physical_logical_luns(softs, SA_REPORT_LOG, logical_dev_list, log_data_length);
	if (ret) {
		DBG_ERR("report logical LUNs failed");
		return ret;
	}


	logdev_data = *logical_dev_list;

	if (logdev_data) {
		logical_list_length =
			BE_32(logdev_data->header.list_length);
	} else {
		memset(&report_lun_header, 0, sizeof(report_lun_header));
		logdev_data =
			(reportlun_data_ext_t *)&report_lun_header;
		logical_list_length = 0;
	}

	logdev_data_length = sizeof(reportlun_header_t) +
		logical_list_length;

	/* Adding LOGICAL device entry for controller */
	local_logdev_list = os_mem_alloc(softs,
					    logdev_data_length + sizeof(reportlun_ext_entry_t));
	if (!local_logdev_list) {
		data_length = *log_data_length;
		os_mem_free(softs, (char *)*logical_dev_list, data_length);
		*logical_dev_list = NULL;
		return PQI_STATUS_FAILURE;
	}

	memcpy(local_logdev_list, logdev_data, logdev_data_length);
	memset((uint8_t *)local_logdev_list + logdev_data_length, 0,
		sizeof(reportlun_ext_entry_t));
	local_logdev_list->header.list_length = BE_32(logical_list_length +
							sizeof(reportlun_ext_entry_t));
	data_length = *log_data_length;
	os_mem_free(softs, (char *)*logical_dev_list, data_length);
	*log_data_length = logdev_data_length + sizeof(reportlun_ext_entry_t);
	*logical_dev_list = local_logdev_list;

	DBG_FUNC("OUT\n");

	return ret;
}

/* Subroutine used to set Bus-Target-Lun for the requested device */
static inline void pqisrc_set_btl(pqi_scsi_dev_t *device,
	int bus, int target, int lun)
{
	DBG_FUNC("IN\n");

	device->bus = bus;
	device->target = target;
	device->lun = lun;

	DBG_FUNC("OUT\n");
}

inline boolean_t pqisrc_is_external_raid_device(pqi_scsi_dev_t *device)
{
	return device->is_external_raid_device;
}

static inline boolean_t pqisrc_is_external_raid_addr(uint8_t *scsi3addr)
{
	return scsi3addr[2] != 0;
}

/* Function used to assign Bus-Target-Lun for the requested device */
static void pqisrc_assign_btl(pqi_scsi_dev_t *device)
{
	uint8_t *scsi3addr;
	uint32_t lunid;
	uint32_t bus;
	uint32_t target;
	uint32_t lun;
	DBG_FUNC("IN\n");

	scsi3addr = device->scsi3addr;
	lunid = GET_LE32(scsi3addr);

	if (pqisrc_is_hba_lunid(scsi3addr)) {
		/* The specified device is the controller. */
		pqisrc_set_btl(device, PQI_HBA_BUS, PQI_CTLR_INDEX, lunid & 0x3fff);
		device->target_lun_valid = true;
		return;
	}

	if (pqisrc_is_logical_device(device)) {
		if (pqisrc_is_external_raid_device(device)) {
			DBG_DISC("External Raid Device!!!");
			bus = PQI_EXTERNAL_RAID_VOLUME_BUS;
			target = (lunid >> 16) & 0x3fff;
			lun = lunid & 0xff;
		} else {
			bus = PQI_RAID_VOLUME_BUS;
			lun = 0;
			target = lunid & 0x3fff;
		}
		pqisrc_set_btl(device, bus, target, lun);
		device->target_lun_valid = true;
		return;
	}

	DBG_FUNC("OUT\n");
}

/* Build and send the internal INQUIRY command to particular device */
static int pqisrc_send_scsi_inquiry(pqisrc_softstate_t *softs,
	uint8_t *scsi3addr, uint16_t vpd_page, uint8_t *buff, int buf_len)
{
	int ret = PQI_STATUS_SUCCESS;
	pqisrc_raid_req_t request;
	raid_path_error_info_elem_t error_info;

	DBG_FUNC("IN\n");

	memset(&request, 0, sizeof(request));
	ret =  pqisrc_build_send_raid_request(softs, &request, buff, buf_len, 
								SA_INQUIRY, vpd_page, scsi3addr, &error_info);

	DBG_FUNC("OUT\n");
	return ret;
}

/* Function used to parse the sense information from response */
static void pqisrc_fetch_sense_info(const uint8_t *sense_data,
	unsigned sense_data_length, uint8_t *sense_key, uint8_t *asc, uint8_t *ascq)
{
	struct sense_header_scsi header;

	DBG_FUNC("IN\n");

	*sense_key = 0;
	*ascq = 0;
	*asc = 0;

	if (pqisrc_update_scsi_sense(sense_data, sense_data_length, &header)) {
		*sense_key = header.sense_key;
		*asc = header.asc;
		*ascq = header.ascq;
	}

	DBG_DISC("sense_key: %x asc: %x ascq: %x\n", *sense_key, *asc, *ascq);

	DBG_FUNC("OUT\n");
}

/* Function used to validate volume offline status */
static uint8_t pqisrc_get_volume_offline_status(pqisrc_softstate_t *softs,
	uint8_t *scsi3addr)
{
	int ret = PQI_STATUS_SUCCESS;
	uint8_t status = SA_LV_STATUS_VPD_UNSUPPORTED;
	uint8_t size;
	uint8_t *buff = NULL;

	DBG_FUNC("IN\n");
	
	buff = os_mem_alloc(softs, 64);
	if (!buff)
		return PQI_STATUS_FAILURE;

	/* Get the size of the VPD return buff. */
	ret = pqisrc_send_scsi_inquiry(softs, scsi3addr, VPD_PAGE | SA_VPD_LV_STATUS,
		buff, SCSI_VPD_HEADER_LENGTH);

	if (ret)
		goto out;

	size = buff[3];

	/* Now get the whole VPD buff. */
	ret = pqisrc_send_scsi_inquiry(softs, scsi3addr, VPD_PAGE | SA_VPD_LV_STATUS,
		buff, size + SCSI_VPD_HEADER_LENGTH);
	if (ret)
		goto out;

	status = buff[4];

out:
	os_mem_free(softs, (char *)buff, 64);
	DBG_FUNC("OUT\n");

	return status;
}


/* Determine offline status of a volume.  Returns appropriate SA_LV_* status.*/
static uint8_t pqisrc_get_dev_vol_status(pqisrc_softstate_t *softs,
	uint8_t *scsi3addr)
{
	int ret = PQI_STATUS_SUCCESS;
	uint8_t *sense_data;
	unsigned sense_data_len;
	uint8_t sense_key;
	uint8_t asc;
	uint8_t ascq;
	uint8_t off_status;
	uint8_t scsi_status;
	pqisrc_raid_req_t request;
	raid_path_error_info_elem_t error_info;

	DBG_FUNC("IN\n");

	memset(&request, 0, sizeof(request));	
	ret =  pqisrc_build_send_raid_request(softs, &request, NULL, 0, 
				TEST_UNIT_READY, 0, scsi3addr, &error_info);
	
	if (ret)
		goto error;
	sense_data = error_info.data;
	sense_data_len = LE_16(error_info.sense_data_len);

	if (sense_data_len > sizeof(error_info.data))
		sense_data_len = sizeof(error_info.data);

	pqisrc_fetch_sense_info(sense_data, sense_data_len, &sense_key, &asc,
		&ascq);

	scsi_status = error_info.status;

	/* scsi status: "CHECK CONDN" /  SK: "not ready" ? */
	if (scsi_status != 2 ||
	    sense_key != 2 ||
	    asc != ASC_LUN_NOT_READY) {
		return SA_LV_OK;
	}

	/* Determine the reason for not ready state. */
	off_status = pqisrc_get_volume_offline_status(softs, scsi3addr);

	DBG_DISC("offline_status 0x%x\n", off_status);

	/* Keep volume offline in certain cases. */
	switch (off_status) {
	case SA_LV_UNDERGOING_ERASE:
	case SA_LV_NOT_AVAILABLE:
	case SA_LV_UNDERGOING_RPI:
	case SA_LV_PENDING_RPI:
	case SA_LV_ENCRYPTED_NO_KEY:
	case SA_LV_PLAINTEXT_IN_ENCRYPT_ONLY_CONTROLLER:
	case SA_LV_UNDERGOING_ENCRYPTION:
	case SA_LV_UNDERGOING_ENCRYPTION_REKEYING:
	case SA_LV_ENCRYPTED_IN_NON_ENCRYPTED_CONTROLLER:
		return off_status;
	case SA_LV_STATUS_VPD_UNSUPPORTED:
		/*
		 * If the VPD status page isn't available,
		 * use ASC/ASCQ to determine state.
		 */
		if (ascq == ASCQ_LUN_NOT_READY_FORMAT_IN_PROGRESS ||
		    ascq == ASCQ_LUN_NOT_READY_INITIALIZING_CMD_REQ)
			return off_status;
		break;
	}

	DBG_FUNC("OUT\n");

	return SA_LV_OK;

error:
	return SA_LV_STATUS_VPD_UNSUPPORTED;
}

/* Validate the RAID map parameters */
static int pqisrc_raid_map_validation(pqisrc_softstate_t *softs,
	pqi_scsi_dev_t *device, pqisrc_raid_map_t *raid_map)
{
	char *error_msg;
	uint32_t raidmap_size;
	uint32_t r5or6_blocks_per_row;
	unsigned phys_dev_num;
	unsigned num_raidmap_entries;

	DBG_FUNC("IN\n");

	raidmap_size = LE_32(raid_map->structure_size);
	if (raidmap_size < offsetof(pqisrc_raid_map_t, dev_data)) {
		error_msg = "RAID map too small\n";
		goto error;
	}

	if (raidmap_size > sizeof(*raid_map)) {
		error_msg = "RAID map too large\n";
		goto error;
	}

	phys_dev_num = LE_16(raid_map->layout_map_count) *
		(LE_16(raid_map->data_disks_per_row) +
		LE_16(raid_map->metadata_disks_per_row));
	num_raidmap_entries = phys_dev_num *
		LE_16(raid_map->row_cnt);

	if (num_raidmap_entries > RAID_MAP_MAX_ENTRIES) {
		error_msg = "invalid number of map entries in RAID map\n";
		goto error;
	}

	if (device->raid_level == SA_RAID_1) {
		if (LE_16(raid_map->layout_map_count) != 2) {
			error_msg = "invalid RAID-1 map\n";
			goto error;
		}
	} else if (device->raid_level == SA_RAID_ADM) {
		if (LE_16(raid_map->layout_map_count) != 3) {
			error_msg = "invalid RAID-1(ADM) map\n";
			goto error;
		}
	} else if ((device->raid_level == SA_RAID_5 ||
		device->raid_level == SA_RAID_6) &&
		LE_16(raid_map->layout_map_count) > 1) {
		/* RAID 50/60 */
		r5or6_blocks_per_row =
			LE_16(raid_map->strip_size) *
			LE_16(raid_map->data_disks_per_row);
		if (r5or6_blocks_per_row == 0) {
			error_msg = "invalid RAID-5 or RAID-6 map\n";
			goto error;
		}
	}

	DBG_FUNC("OUT\n");

	return 0;

error:
	DBG_ERR("%s\n", error_msg);
	return PQI_STATUS_FAILURE;
}

/* Get device raidmap for the requested device */
static int pqisrc_get_device_raidmap(pqisrc_softstate_t *softs,
	pqi_scsi_dev_t *device)
{
	int ret = PQI_STATUS_SUCCESS;
	pqisrc_raid_req_t request;
	pqisrc_raid_map_t *raid_map;

	DBG_FUNC("IN\n");

	raid_map = os_mem_alloc(softs, sizeof(*raid_map));
	if (!raid_map)
		return PQI_STATUS_FAILURE;

	memset(&request, 0, sizeof(request));
	ret =  pqisrc_build_send_raid_request(softs, &request, raid_map, sizeof(*raid_map), 
			 		SA_GET_RAID_MAP, 0, device->scsi3addr, NULL);

	if (ret) {
		DBG_ERR("error in build send raid req ret=%d\n", ret);
		goto err_out;
	}

	ret = pqisrc_raid_map_validation(softs, device, raid_map);
	if (ret) {
		DBG_ERR("error in raid map validation ret=%d\n", ret);
		goto err_out;
	}

	device->raid_map = raid_map;
	DBG_FUNC("OUT\n");
	return 0;

err_out:
	os_mem_free(softs, (char*)raid_map, sizeof(*raid_map));
	DBG_FUNC("FAILED \n");
	return ret;
}

/* Get device ioaccel_status to validate the type of device */
static void pqisrc_get_dev_ioaccel_status(pqisrc_softstate_t *softs,
	pqi_scsi_dev_t *device)
{
	int ret = PQI_STATUS_SUCCESS;
	uint8_t *buff;
	uint8_t ioaccel_status;

	DBG_FUNC("IN\n");

	buff = os_mem_alloc(softs, 64);
	if (!buff)
		return;

	ret = pqisrc_send_scsi_inquiry(softs, device->scsi3addr,
					VPD_PAGE | SA_VPD_LV_IOACCEL_STATUS, buff, 64);
	if (ret) {
		DBG_ERR("error in send scsi inquiry ret=%d\n", ret);
		goto err_out;
	}
	
	ioaccel_status = buff[IOACCEL_STATUS_BYTE];
	device->offload_config =
		!!(ioaccel_status & OFFLOAD_CONFIGURED_BIT);

	if (device->offload_config) {
		device->offload_enabled_pending =
			!!(ioaccel_status & OFFLOAD_ENABLED_BIT);
		if (pqisrc_get_device_raidmap(softs, device))
			device->offload_enabled_pending = false;
	}
	
	DBG_DISC("offload_config: 0x%x offload_enabled_pending: 0x%x \n", 
			device->offload_config, device->offload_enabled_pending);

err_out:
	os_mem_free(softs, (char*)buff, 64);
	DBG_FUNC("OUT\n");
}

/* Get RAID level of requested device */
static void pqisrc_get_dev_raid_level(pqisrc_softstate_t *softs,
	pqi_scsi_dev_t *device)
{
	uint8_t raid_level;
	uint8_t *buff;

	DBG_FUNC("IN\n");

	raid_level = SA_RAID_UNKNOWN;

	buff = os_mem_alloc(softs, 64);
	if (buff) {
		int ret;
		ret = pqisrc_send_scsi_inquiry(softs, device->scsi3addr,
			VPD_PAGE | SA_VPD_LV_DEVICE_GEOMETRY, buff, 64);
		if (ret == 0) {
			raid_level = buff[8];
			if (raid_level > SA_RAID_MAX)
				raid_level = SA_RAID_UNKNOWN;
		}
		os_mem_free(softs, (char*)buff, 64);
	}

	device->raid_level = raid_level;
	DBG_DISC("RAID LEVEL: %x \n",  raid_level);
	DBG_FUNC("OUT\n");
}

/* Parse the inquiry response and determine the type of device */
static int pqisrc_get_dev_data(pqisrc_softstate_t *softs,
	pqi_scsi_dev_t *device)
{
	int ret = PQI_STATUS_SUCCESS;
	uint8_t *inq_buff;

	DBG_FUNC("IN\n");

	inq_buff = os_mem_alloc(softs, OBDR_TAPE_INQ_SIZE);
	if (!inq_buff)
		return PQI_STATUS_FAILURE;

	/* Send an inquiry to the device to see what it is. */
	ret = pqisrc_send_scsi_inquiry(softs, device->scsi3addr, 0, inq_buff,
		OBDR_TAPE_INQ_SIZE);
	if (ret)
		goto err_out;
	pqisrc_sanitize_inquiry_string(&inq_buff[8], 8);
	pqisrc_sanitize_inquiry_string(&inq_buff[16], 16);

	device->devtype = inq_buff[0] & 0x1f;
	memcpy(device->vendor, &inq_buff[8],
		sizeof(device->vendor));
	memcpy(device->model, &inq_buff[16],
		sizeof(device->model));
	DBG_DISC("DEV_TYPE: %x VENDOR: %s MODEL: %s\n",  device->devtype, device->vendor, device->model);

	if (pqisrc_is_logical_device(device) && device->devtype == DISK_DEVICE) {
		if (pqisrc_is_external_raid_device(device)) {
			device->raid_level = SA_RAID_UNKNOWN;
			device->volume_status = SA_LV_OK;
			device->volume_offline = false;
		} 
		else {
			pqisrc_get_dev_raid_level(softs, device);
			pqisrc_get_dev_ioaccel_status(softs, device);
			device->volume_status = pqisrc_get_dev_vol_status(softs,
						device->scsi3addr);
			device->volume_offline = device->volume_status != SA_LV_OK;
		}
	}

	/*
	 * Check if this is a One-Button-Disaster-Recovery device
	 * by looking for "$DR-10" at offset 43 in the inquiry data.
	 */
	device->is_obdr_device = (device->devtype == ROM_DEVICE &&
		memcmp(&inq_buff[OBDR_SIG_OFFSET], OBDR_TAPE_SIG,
			OBDR_SIG_LEN) == 0);
err_out:
	os_mem_free(softs, (char*)inq_buff, OBDR_TAPE_INQ_SIZE);

	DBG_FUNC("OUT\n");
	return ret;
}

/*
 * BMIC (Basic Management And Interface Commands) command
 * to get the controller identify params
 */
static int pqisrc_identify_ctrl(pqisrc_softstate_t *softs,
	bmic_ident_ctrl_t *buff)
{
	int ret = PQI_STATUS_SUCCESS;
	pqisrc_raid_req_t request;

	DBG_FUNC("IN\n");

	memset(&request, 0, sizeof(request));	
	ret =  pqisrc_build_send_raid_request(softs, &request, buff, sizeof(*buff), 
				BMIC_IDENTIFY_CONTROLLER, 0, (uint8_t *)RAID_CTLR_LUNID, NULL);
	DBG_FUNC("OUT\n");

	return ret;
}

/* Get the adapter FW version using BMIC_IDENTIFY_CONTROLLER */
int pqisrc_get_ctrl_fw_version(pqisrc_softstate_t *softs)
{
	int ret = PQI_STATUS_SUCCESS;
	bmic_ident_ctrl_t *identify_ctrl;

	DBG_FUNC("IN\n");

	identify_ctrl = os_mem_alloc(softs, sizeof(*identify_ctrl));
	if (!identify_ctrl) {
		DBG_ERR("failed to allocate memory for identify_ctrl\n");
		return PQI_STATUS_FAILURE;
	}

	memset(identify_ctrl, 0, sizeof(*identify_ctrl));

	ret = pqisrc_identify_ctrl(softs, identify_ctrl);
	if (ret)
		goto out;
     
	softs->fw_build_number = identify_ctrl->fw_build_number;
	memcpy(softs->fw_version, identify_ctrl->fw_version,
		sizeof(identify_ctrl->fw_version));
	softs->fw_version[sizeof(identify_ctrl->fw_version)] = '\0';
	snprintf(softs->fw_version +
		strlen(softs->fw_version),
		sizeof(softs->fw_version),
		"-%u", identify_ctrl->fw_build_number);
out:
	os_mem_free(softs, (char *)identify_ctrl, sizeof(*identify_ctrl));
	DBG_INIT("Firmware version: %s Firmware build number: %d\n", softs->fw_version, softs->fw_build_number);
	DBG_FUNC("OUT\n");
	return ret;
}

/* BMIC command to determine scsi device identify params */
static int pqisrc_identify_physical_disk(pqisrc_softstate_t *softs,
	pqi_scsi_dev_t *device,
	bmic_ident_physdev_t *buff,
	int buf_len)
{
	int ret = PQI_STATUS_SUCCESS;
	uint16_t bmic_device_index;
	pqisrc_raid_req_t request;


	DBG_FUNC("IN\n");

	memset(&request, 0, sizeof(request));	
	bmic_device_index = BMIC_GET_DRIVE_NUMBER(device->scsi3addr);
	request.cdb[2] = (uint8_t)bmic_device_index;
	request.cdb[9] = (uint8_t)(bmic_device_index >> 8);

	ret =  pqisrc_build_send_raid_request(softs, &request, buff, buf_len, 
				BMIC_IDENTIFY_PHYSICAL_DEVICE, 0, (uint8_t *)RAID_CTLR_LUNID, NULL);
	DBG_FUNC("OUT\n");
	return ret;
}

/*
 * Function used to get the scsi device information using one of BMIC
 * BMIC_IDENTIFY_PHYSICAL_DEVICE
 */
static void pqisrc_get_physical_device_info(pqisrc_softstate_t *softs,
	pqi_scsi_dev_t *device,
	bmic_ident_physdev_t *id_phys)
{
	int ret = PQI_STATUS_SUCCESS;

	DBG_FUNC("IN\n");
	memset(id_phys, 0, sizeof(*id_phys));

	ret= pqisrc_identify_physical_disk(softs, device,
		id_phys, sizeof(*id_phys));
	if (ret) {
		device->queue_depth = PQI_PHYSICAL_DISK_DEFAULT_MAX_QUEUE_DEPTH;
		return;
	}

	device->queue_depth =
		LE_16(id_phys->current_queue_depth_limit);
	device->device_type = id_phys->device_type;
	device->active_path_index = id_phys->active_path_number;
	device->path_map = id_phys->redundant_path_present_map;
	memcpy(&device->box,
		&id_phys->alternate_paths_phys_box_on_port,
		sizeof(device->box));
	memcpy(&device->phys_connector,
		&id_phys->alternate_paths_phys_connector,
		sizeof(device->phys_connector));
	device->bay = id_phys->phys_bay_in_box;

	DBG_DISC("BMIC DEV_TYPE: %x QUEUE DEPTH: 0x%x \n",  device->device_type, device->queue_depth);
	DBG_FUNC("OUT\n");
}


/* Function used to find the entry of the device in a list */
static device_status_t pqisrc_scsi_find_entry(pqisrc_softstate_t *softs,
	pqi_scsi_dev_t *device_to_find,
	pqi_scsi_dev_t **same_device)
{
	pqi_scsi_dev_t *device;
	int i,j;
	DBG_FUNC("IN\n");
	for(i = 0; i < PQI_MAX_DEVICES; i++) {
		for(j = 0; j < PQI_MAX_MULTILUN; j++) {
			if(softs->device_list[i][j] == NULL)
				continue;
			device = softs->device_list[i][j];
			if (pqisrc_scsi3addr_equal(device_to_find->scsi3addr,
				device->scsi3addr)) {
				*same_device = device;
				if (pqisrc_device_equal(device_to_find, device)) {
					if (device_to_find->volume_offline)
						return DEVICE_CHANGED;
					return DEVICE_UNCHANGED;
				}
				return DEVICE_CHANGED;
			}
		}
	}
	DBG_FUNC("OUT\n");

	return DEVICE_NOT_FOUND;
}


/* Update the newly added devices as existed device */
static void pqisrc_exist_device_update(pqisrc_softstate_t *softs,
	pqi_scsi_dev_t *device_exist,
	pqi_scsi_dev_t *new_device)
{
	DBG_FUNC("IN\n");
	device_exist->expose_device = new_device->expose_device;
	memcpy(device_exist->vendor, new_device->vendor,
		sizeof(device_exist->vendor));
	memcpy(device_exist->model, new_device->model,
		sizeof(device_exist->model));
	device_exist->is_physical_device = new_device->is_physical_device;
	device_exist->is_external_raid_device =
		new_device->is_external_raid_device;
	device_exist->sas_address = new_device->sas_address;
	device_exist->raid_level = new_device->raid_level;
	device_exist->queue_depth = new_device->queue_depth;
	device_exist->ioaccel_handle = new_device->ioaccel_handle;
	device_exist->volume_status = new_device->volume_status;
	device_exist->active_path_index = new_device->active_path_index;
	device_exist->path_map = new_device->path_map;
	device_exist->bay = new_device->bay;
	memcpy(device_exist->box, new_device->box,
		sizeof(device_exist->box));
	memcpy(device_exist->phys_connector, new_device->phys_connector,
		sizeof(device_exist->phys_connector));
	device_exist->offload_config = new_device->offload_config;
	device_exist->offload_enabled = false;
	device_exist->offload_enabled_pending =
		new_device->offload_enabled_pending;
	device_exist->offload_to_mirror = 0;
	if (device_exist->raid_map)
		os_mem_free(softs,
			    (char *)device_exist->raid_map,
			    sizeof(*device_exist->raid_map));
	device_exist->raid_map = new_device->raid_map;
	/* To prevent this from being freed later. */
	new_device->raid_map = NULL;
	DBG_FUNC("OUT\n");
}

/* Validate the ioaccel_handle for a newly added device */
static pqi_scsi_dev_t *pqisrc_identify_device_via_ioaccel(
	pqisrc_softstate_t *softs, uint32_t ioaccel_handle)
{
	pqi_scsi_dev_t *device;
	int i,j;
	DBG_FUNC("IN\n");	
	for(i = 0; i < PQI_MAX_DEVICES; i++) {
		for(j = 0; j < PQI_MAX_MULTILUN; j++) {
			if(softs->device_list[i][j] == NULL)
				continue;
			device = softs->device_list[i][j];
			if (device->devtype != DISK_DEVICE)
				continue;
			if (pqisrc_is_logical_device(device))
				continue;
			if (device->ioaccel_handle == ioaccel_handle)
				return device;
		}
	}
	DBG_FUNC("OUT\n");

	return NULL;
}

/* Get the scsi device queue depth */
static void pqisrc_update_log_dev_qdepth(pqisrc_softstate_t *softs)
{
	unsigned i;
	unsigned phys_dev_num;
	unsigned num_raidmap_entries;
	unsigned queue_depth;
	pqisrc_raid_map_t *raid_map;
	pqi_scsi_dev_t *device;
	raidmap_data_t *dev_data;
	pqi_scsi_dev_t *phys_disk;
	unsigned j;
	unsigned k;

	DBG_FUNC("IN\n");

	for(i = 0; i < PQI_MAX_DEVICES; i++) {
		for(j = 0; j < PQI_MAX_MULTILUN; j++) {
			if(softs->device_list[i][j] == NULL)
				continue;
			device = softs->device_list[i][j];
			if (device->devtype != DISK_DEVICE)
				continue;
			if (!pqisrc_is_logical_device(device))
				continue;
			if (pqisrc_is_external_raid_device(device))
				continue;
			device->queue_depth = PQI_LOGICAL_DISK_DEFAULT_MAX_QUEUE_DEPTH;
			raid_map = device->raid_map;
			if (!raid_map)
				return;
			dev_data = raid_map->dev_data;
			phys_dev_num = LE_16(raid_map->layout_map_count) *
					(LE_16(raid_map->data_disks_per_row) +
					LE_16(raid_map->metadata_disks_per_row));
			num_raidmap_entries = phys_dev_num *
						LE_16(raid_map->row_cnt);

			queue_depth = 0;
			for (k = 0; k < num_raidmap_entries; k++) {
				phys_disk = pqisrc_identify_device_via_ioaccel(softs,
						dev_data[k].ioaccel_handle);

				if (!phys_disk) {
					DBG_WARN(
					"Failed to find physical disk handle for logical drive %016llx\n",
						(unsigned long long)BE_64(device->scsi3addr[0]));
					device->offload_enabled = false;
					device->offload_enabled_pending = false;
					if (raid_map)
						os_mem_free(softs, (char *)raid_map, sizeof(*raid_map));
					device->raid_map = NULL;
					return;
				}

				queue_depth += phys_disk->queue_depth;
			}

			device->queue_depth = queue_depth;
		} /* end inner loop */
	}/* end outer loop */
	DBG_FUNC("OUT\n");
}

/* Function used to add a scsi device to OS scsi subsystem */
static int pqisrc_add_device(pqisrc_softstate_t *softs,
	pqi_scsi_dev_t *device)
{
	DBG_FUNC("IN\n");
	DBG_WARN("vendor: %s model: %s bus:%d target:%d lun:%d is_physical_device:0x%x expose_device:0x%x volume_offline 0x%x volume_status 0x%x \n",
		device->vendor, device->model, device->bus, device->target, device->lun, device->is_physical_device, device->expose_device, device->volume_offline, device->volume_status);

	device->invalid = false;

	if(device->expose_device) {
		/* TBD: Call OS upper layer function to add the device entry */
		os_add_device(softs,device);
	}
	DBG_FUNC("OUT\n");
	return PQI_STATUS_SUCCESS;

}

/* Function used to remove a scsi device from OS scsi subsystem */
void pqisrc_remove_device(pqisrc_softstate_t *softs,
	pqi_scsi_dev_t *device)
{
	DBG_FUNC("IN\n");
	DBG_DISC("vendor: %s model: %s bus:%d target:%d lun:%d is_physical_device:0x%x expose_device:0x%x volume_offline 0x%x volume_status 0x%x \n",
		device->vendor, device->model, device->bus, device->target, device->lun, device->is_physical_device, device->expose_device, device->volume_offline, device->volume_status);

	/* TBD: Call OS upper layer function to remove the device entry */
	device->invalid = true;
	os_remove_device(softs,device);
	DBG_FUNC("OUT\n");
}


/*
 * When exposing new device to OS fails then adjst list according to the
 * mid scsi list
 */
static void pqisrc_adjust_list(pqisrc_softstate_t *softs,
	pqi_scsi_dev_t *device)
{
	DBG_FUNC("IN\n");

	if (!device) {
		DBG_ERR("softs = %p: device is NULL !!!\n", softs);
		return;
	}

	OS_ACQUIRE_SPINLOCK(&softs->devlist_lock);
	softs->device_list[device->target][device->lun] = NULL;
	OS_RELEASE_SPINLOCK(&softs->devlist_lock);
	pqisrc_device_mem_free(softs, device);

	DBG_FUNC("OUT\n");
}

/* Debug routine used to display the RAID volume status of the device */
static void pqisrc_display_volume_status(pqisrc_softstate_t *softs,
	pqi_scsi_dev_t *device)
{
	char *status;

	DBG_FUNC("IN\n");
	switch (device->volume_status) {
	case SA_LV_OK:
		status = "Volume is online.";
		break;
	case SA_LV_UNDERGOING_ERASE:
		status = "Volume is undergoing background erase process.";
		break;
	case SA_LV_NOT_AVAILABLE:
		status = "Volume is waiting for transforming volume.";
		break;
	case SA_LV_UNDERGOING_RPI:
		status = "Volume is undergoing rapid parity initialization process.";
		break;
	case SA_LV_PENDING_RPI:
		status = "Volume is queued for rapid parity initialization process.";
		break;
	case SA_LV_ENCRYPTED_NO_KEY:
		status = "Volume is encrypted and cannot be accessed because key is not present.";
		break;
	case SA_LV_PLAINTEXT_IN_ENCRYPT_ONLY_CONTROLLER:
		status = "Volume is not encrypted and cannot be accessed because controller is in encryption-only mode.";
		break;
	case SA_LV_UNDERGOING_ENCRYPTION:
		status = "Volume is undergoing encryption process.";
		break;
	case SA_LV_UNDERGOING_ENCRYPTION_REKEYING:
		status = "Volume is undergoing encryption re-keying process.";
		break;
	case SA_LV_ENCRYPTED_IN_NON_ENCRYPTED_CONTROLLER:
		status = "Volume is encrypted and cannot be accessed because controller does not have encryption enabled.";
		break;
	case SA_LV_PENDING_ENCRYPTION:
		status = "Volume is pending migration to encrypted state, but process has not started.";
		break;
	case SA_LV_PENDING_ENCRYPTION_REKEYING:
		status = "Volume is encrypted and is pending encryption rekeying.";
		break;
	case SA_LV_STATUS_VPD_UNSUPPORTED:
		status = "Volume status is not available through vital product data pages.";
		break;
	default:
		status = "Volume is in an unknown state.";
		break;
	}

	DBG_DISC("scsi BTL %d:%d:%d %s\n",
		device->bus, device->target, device->lun, status);
	DBG_FUNC("OUT\n");
}

void pqisrc_device_mem_free(pqisrc_softstate_t *softs, pqi_scsi_dev_t *device)
{
	DBG_FUNC("IN\n");
	if (!device)
		return;
	if (device->raid_map) {
			os_mem_free(softs, (char *)device->raid_map, sizeof(pqisrc_raid_map_t));
	}
	os_mem_free(softs, (char *)device,sizeof(*device));
	DBG_FUNC("OUT\n");
	
}

/* OS should call this function to free the scsi device */
void pqisrc_free_device(pqisrc_softstate_t * softs,pqi_scsi_dev_t *device)
{

		OS_ACQUIRE_SPINLOCK(&softs->devlist_lock);
		if (!pqisrc_is_logical_device(device)) {
			pqisrc_free_tid(softs,device->target);
		}
		pqisrc_device_mem_free(softs, device);
		OS_RELEASE_SPINLOCK(&softs->devlist_lock);

}


/* Update the newly added devices to the device list */
static void pqisrc_update_device_list(pqisrc_softstate_t *softs,
	pqi_scsi_dev_t *new_device_list[], int num_new_devices)
{
	int ret;
	int i;
	device_status_t dev_status;
	pqi_scsi_dev_t *device;
	pqi_scsi_dev_t *same_device;
	pqi_scsi_dev_t **added = NULL;
	pqi_scsi_dev_t **removed = NULL;
	int nadded = 0, nremoved = 0;
	int j;
	int tid = 0;

	DBG_FUNC("IN\n");

	added = os_mem_alloc(softs, sizeof(*added) * PQI_MAX_DEVICES);
	removed = os_mem_alloc(softs, sizeof(*removed) * PQI_MAX_DEVICES);

	if (!added || !removed) {
		DBG_WARN("Out of memory \n");
		goto free_and_out;
	}
	
	OS_ACQUIRE_SPINLOCK(&softs->devlist_lock);
	
	for(i = 0; i < PQI_MAX_DEVICES; i++) {
		for(j = 0; j < PQI_MAX_MULTILUN; j++) {
			if(softs->device_list[i][j] == NULL)
				continue;
			device = softs->device_list[i][j];
			device->device_gone = true;
		}
	}
	DBG_IO("Device list used an array\n");
	for (i = 0; i < num_new_devices; i++) {
		device = new_device_list[i];

		dev_status = pqisrc_scsi_find_entry(softs, device,
			&same_device);

		switch (dev_status) {
		case DEVICE_UNCHANGED:
			/* New Device present in existing device list  */
			device->new_device = false;
			same_device->device_gone = false;
			pqisrc_exist_device_update(softs, same_device, device);
			break;
		case DEVICE_NOT_FOUND:
			/* Device not found in existing list */
			device->new_device = true;
			break;
		case DEVICE_CHANGED:
			/* Actual device gone need to add device to list*/
			device->new_device = true;
			break;
		default:
			break;
		}
	}
	/* Process all devices that have gone away. */
	for(i = 0, nremoved = 0; i < PQI_MAX_DEVICES; i++) {
		for(j = 0; j < PQI_MAX_MULTILUN; j++) {
			if(softs->device_list[i][j] == NULL)
				continue;
			device = softs->device_list[i][j];
			if (device->device_gone) {
				softs->device_list[device->target][device->lun] = NULL;
				removed[nremoved] = device;
				nremoved++;
			}
		}
	}

	/* Process all new devices. */
	for (i = 0, nadded = 0; i < num_new_devices; i++) {
		device = new_device_list[i];
		if (!device->new_device)
			continue;
		if (device->volume_offline)
			continue;
		
		/* physical device */
		if (!pqisrc_is_logical_device(device)) {
			tid = pqisrc_alloc_tid(softs);
			if(INVALID_ELEM != tid)
				pqisrc_set_btl(device, PQI_PHYSICAL_DEVICE_BUS, tid, 0);
		}

 		softs->device_list[device->target][device->lun] = device;
		DBG_DISC("Added device %p at B : %d T : %d L : %d\n",device,
			device->bus,device->target,device->lun);
		/* To prevent this entry from being freed later. */
		new_device_list[i] = NULL;
		added[nadded] = device;
		nadded++;
	}

	pqisrc_update_log_dev_qdepth(softs);
	
	for(i = 0; i < PQI_MAX_DEVICES; i++) {
		for(j = 0; j < PQI_MAX_MULTILUN; j++) {
			if(softs->device_list[i][j] == NULL)
				continue;
			device = softs->device_list[i][j];
			device->offload_enabled = device->offload_enabled_pending;
		}
	}

	OS_RELEASE_SPINLOCK(&softs->devlist_lock);

	for(i = 0; i < nremoved; i++) {
		device = removed[i];
		if (device == NULL)
			continue;
		pqisrc_remove_device(softs, device);
		pqisrc_display_device_info(softs, "removed", device);
		
	}

	for(i = 0; i < PQI_MAX_DEVICES; i++) {
		for(j = 0; j < PQI_MAX_MULTILUN; j++) {
			if(softs->device_list[i][j] == NULL)
				continue;
			device = softs->device_list[i][j];
			/*
			* Notify the OS upper layer if the queue depth of any existing device has
			* changed.
			*/
			if (device->queue_depth !=
				device->advertised_queue_depth) {
				device->advertised_queue_depth = device->queue_depth;
				/* TBD: Call OS upper layer function to change device Q depth */
			}
		}
	}
	for(i = 0; i < nadded; i++) {
		device = added[i];
		if (device->expose_device) {
			ret = pqisrc_add_device(softs, device);
			if (ret) {
				DBG_WARN("scsi %d:%d:%d addition failed, device not added\n",
					device->bus, device->target,
					device->lun);
				pqisrc_adjust_list(softs, device);
				continue;
			}
		}

		pqisrc_display_device_info(softs, "added", device);
	}

	/* Process all volumes that are offline. */
	for (i = 0; i < num_new_devices; i++) {
		device = new_device_list[i];
		if (!device)
			continue;
		if (!device->new_device)
			continue;
		if (device->volume_offline) {
			pqisrc_display_volume_status(softs, device);
			pqisrc_display_device_info(softs, "offline", device);
		}
	}

free_and_out:
	if (added)
		os_mem_free(softs, (char *)added,
			    sizeof(*added) * PQI_MAX_DEVICES); 
	if (removed)
		os_mem_free(softs, (char *)removed,
			    sizeof(*removed) * PQI_MAX_DEVICES); 

	DBG_FUNC("OUT\n");
}

/*
 * Let the Adapter know about driver version using one of BMIC
 * BMIC_WRITE_HOST_WELLNESS
 */
int pqisrc_write_driver_version_to_host_wellness(pqisrc_softstate_t *softs)
{
	int rval = PQI_STATUS_SUCCESS;
	struct bmic_host_wellness_driver_version *host_wellness_driver_ver;
	size_t data_length;
	pqisrc_raid_req_t request;

	DBG_FUNC("IN\n");

	memset(&request, 0, sizeof(request));	
	data_length = sizeof(*host_wellness_driver_ver);

	host_wellness_driver_ver = os_mem_alloc(softs, data_length);
	if (!host_wellness_driver_ver) {
		DBG_ERR("failed to allocate memory for host wellness driver_version\n");
		return PQI_STATUS_FAILURE;
	}

	host_wellness_driver_ver->start_tag[0] = '<';
	host_wellness_driver_ver->start_tag[1] = 'H';
	host_wellness_driver_ver->start_tag[2] = 'W';
	host_wellness_driver_ver->start_tag[3] = '>';
	host_wellness_driver_ver->driver_version_tag[0] = 'D';
	host_wellness_driver_ver->driver_version_tag[1] = 'V';
	host_wellness_driver_ver->driver_version_length = LE_16(sizeof(host_wellness_driver_ver->driver_version));
	strncpy(host_wellness_driver_ver->driver_version, softs->os_name,
        sizeof(host_wellness_driver_ver->driver_version));
    if (strlen(softs->os_name) < sizeof(host_wellness_driver_ver->driver_version) ) {
        strncpy(host_wellness_driver_ver->driver_version + strlen(softs->os_name), PQISRC_DRIVER_VERSION,
			sizeof(host_wellness_driver_ver->driver_version) -  strlen(softs->os_name));
    } else {
        DBG_DISC("OS name length(%lu) is longer than buffer of driver_version\n",
            strlen(softs->os_name));
    }
	host_wellness_driver_ver->driver_version[sizeof(host_wellness_driver_ver->driver_version) - 1] = '\0';
	host_wellness_driver_ver->end_tag[0] = 'Z';
	host_wellness_driver_ver->end_tag[1] = 'Z';

	rval = pqisrc_build_send_raid_request(softs, &request, host_wellness_driver_ver,data_length,
					BMIC_WRITE_HOST_WELLNESS, 0, (uint8_t *)RAID_CTLR_LUNID, NULL);

	os_mem_free(softs, (char *)host_wellness_driver_ver, data_length);
	
	DBG_FUNC("OUT");
	return rval;
}

/* 
 * Write current RTC time from host to the adapter using
 * BMIC_WRITE_HOST_WELLNESS
 */
int pqisrc_write_current_time_to_host_wellness(pqisrc_softstate_t *softs)
{
	int rval = PQI_STATUS_SUCCESS;
	struct bmic_host_wellness_time *host_wellness_time;
	size_t data_length;
	pqisrc_raid_req_t request;

	DBG_FUNC("IN\n");

	memset(&request, 0, sizeof(request));	
	data_length = sizeof(*host_wellness_time);

	host_wellness_time = os_mem_alloc(softs, data_length);
	if (!host_wellness_time) {
		DBG_ERR("failed to allocate memory for host wellness time structure\n");
		return PQI_STATUS_FAILURE;
	}

	host_wellness_time->start_tag[0] = '<';
	host_wellness_time->start_tag[1] = 'H';
	host_wellness_time->start_tag[2] = 'W';
	host_wellness_time->start_tag[3] = '>';
	host_wellness_time->time_tag[0] = 'T';
	host_wellness_time->time_tag[1] = 'D';
	host_wellness_time->time_length = LE_16(offsetof(struct bmic_host_wellness_time, time_length) - 
											offsetof(struct bmic_host_wellness_time, century));

	os_get_time(host_wellness_time);

	host_wellness_time->dont_write_tag[0] = 'D';
	host_wellness_time->dont_write_tag[1] = 'W';
	host_wellness_time->end_tag[0] = 'Z';
	host_wellness_time->end_tag[1] = 'Z';
	
	rval = pqisrc_build_send_raid_request(softs, &request, host_wellness_time,data_length,
					BMIC_WRITE_HOST_WELLNESS, 0, (uint8_t *)RAID_CTLR_LUNID, NULL);
	
	os_mem_free(softs, (char *)host_wellness_time, data_length);

	DBG_FUNC("OUT");
	return rval;
}

/*
 * Function used to perform a rescan of scsi devices
 * for any config change events
 */
int pqisrc_scan_devices(pqisrc_softstate_t *softs)
{
	boolean_t is_physical_device;
	int ret = PQI_STATUS_FAILURE;
	int i;
	int new_dev_cnt;
	int phy_log_dev_cnt;
	uint8_t *scsi3addr;
	uint32_t physical_cnt;
	uint32_t logical_cnt;
	uint32_t ndev_allocated = 0;
	size_t phys_data_length, log_data_length;
	reportlun_data_ext_t *physical_dev_list = NULL;
	reportlun_data_ext_t *logical_dev_list = NULL;
	reportlun_ext_entry_t *lun_ext_entry = NULL;
	bmic_ident_physdev_t *bmic_phy_info = NULL;
	pqi_scsi_dev_t **new_device_list = NULL;
	pqi_scsi_dev_t *device = NULL;
	

	DBG_FUNC("IN\n");

	ret = pqisrc_get_phys_log_device_list(softs, &physical_dev_list, &logical_dev_list,
				      &phys_data_length, &log_data_length);

	if (ret)
		goto err_out;

	physical_cnt = BE_32(physical_dev_list->header.list_length) 
		/ sizeof(physical_dev_list->lun_entries[0]);
	
	logical_cnt = BE_32(logical_dev_list->header.list_length)
		/ sizeof(logical_dev_list->lun_entries[0]);

	DBG_DISC("physical_cnt %d logical_cnt %d\n", physical_cnt, logical_cnt);

	if (physical_cnt) {
		bmic_phy_info = os_mem_alloc(softs, sizeof(*bmic_phy_info));
		if (bmic_phy_info == NULL) {
			ret = PQI_STATUS_FAILURE;
			DBG_ERR("failed to allocate memory for BMIC ID PHYS Device : %d\n", ret);
			goto err_out;
		}
	}
	phy_log_dev_cnt = physical_cnt + logical_cnt;
	new_device_list = os_mem_alloc(softs,
				sizeof(*new_device_list) * phy_log_dev_cnt);

	if (new_device_list == NULL) {
		ret = PQI_STATUS_FAILURE;
		DBG_ERR("failed to allocate memory for device list : %d\n", ret);
		goto err_out;
	}

	for (i = 0; i < phy_log_dev_cnt; i++) {
		new_device_list[i] = os_mem_alloc(softs,
						sizeof(*new_device_list[i]));
		if (new_device_list[i] == NULL) {
			ret = PQI_STATUS_FAILURE;
			DBG_ERR("failed to allocate memory for device list : %d\n", ret);
			ndev_allocated = i;
			goto err_out;
		}
	}

	ndev_allocated = phy_log_dev_cnt;
	new_dev_cnt = 0;
	for (i = 0; i < phy_log_dev_cnt; i++) {

		if (i < physical_cnt) {
			is_physical_device = true;
			lun_ext_entry = &physical_dev_list->lun_entries[i];
		} else {
			is_physical_device = false;
			lun_ext_entry =
				&logical_dev_list->lun_entries[i - physical_cnt];
		}

		scsi3addr = lun_ext_entry->lunid;
		/* Save the target sas adderess for external raid device */
		if(lun_ext_entry->device_type == CONTROLLER_DEVICE) {
			int target = lun_ext_entry->lunid[3] & 0x3f;
			softs->target_sas_addr[target] = BE_64(lun_ext_entry->wwid);
		}

		/* Skip masked physical non-disk devices. */
		if (MASKED_DEVICE(scsi3addr) && is_physical_device
				&& (lun_ext_entry->ioaccel_handle == 0))
			continue;

		device = new_device_list[new_dev_cnt];
		memset(device, 0, sizeof(*device));
		memcpy(device->scsi3addr, scsi3addr, sizeof(device->scsi3addr));
		device->wwid = lun_ext_entry->wwid;
		device->is_physical_device = is_physical_device;
		if (!is_physical_device)
			device->is_external_raid_device =
				pqisrc_is_external_raid_addr(scsi3addr);
		

		/* Get device type, vendor, model, device ID. */
		ret = pqisrc_get_dev_data(softs, device);
		if (ret) {
			DBG_WARN("Inquiry failed, skipping device %016llx\n",
				 (unsigned long long)BE_64(device->scsi3addr[0]));
			DBG_DISC("INQUIRY FAILED \n");
			continue;
		}
		pqisrc_assign_btl(device);

		/*
		 * Expose all devices except for physical devices that
		 * are masked.
		 */
		if (device->is_physical_device &&
			MASKED_DEVICE(scsi3addr))
			device->expose_device = false;
		else
			device->expose_device = true;

		if (device->is_physical_device &&
		    (lun_ext_entry->device_flags &
		     REPORT_LUN_DEV_FLAG_AIO_ENABLED) &&
		     lun_ext_entry->ioaccel_handle) {
			device->aio_enabled = true;
		}
		switch (device->devtype) {
		case ROM_DEVICE:
			/*
			 * We don't *really* support actual CD-ROM devices,
			 * but we do support the HP "One Button Disaster
			 * Recovery" tape drive which temporarily pretends to
			 * be a CD-ROM drive.
			 */
			if (device->is_obdr_device)
				new_dev_cnt++;
			break;
		case DISK_DEVICE:
		case ZBC_DEVICE:
			if (device->is_physical_device) {
				device->ioaccel_handle =
					lun_ext_entry->ioaccel_handle;
				device->sas_address = BE_64(lun_ext_entry->wwid);
				pqisrc_get_physical_device_info(softs, device,
					bmic_phy_info);
			}
			new_dev_cnt++;
			break;
		case ENCLOSURE_DEVICE:
			if (device->is_physical_device) {
				device->sas_address = BE_64(lun_ext_entry->wwid);
			}
			new_dev_cnt++;
			break;	
		case TAPE_DEVICE:
		case MEDIUM_CHANGER_DEVICE:
			new_dev_cnt++;
			break;
		case RAID_DEVICE:
			/*
			 * Only present the HBA controller itself as a RAID
			 * controller.  If it's a RAID controller other than
			 * the HBA itself (an external RAID controller, MSA500
			 * or similar), don't present it.
			 */
			if (pqisrc_is_hba_lunid(scsi3addr))
				new_dev_cnt++;
			break;
		case SES_DEVICE:
		case CONTROLLER_DEVICE:
			break;
		}
	}
	DBG_DISC("new_dev_cnt %d\n", new_dev_cnt);

	pqisrc_update_device_list(softs, new_device_list, new_dev_cnt);
	
err_out:
	if (new_device_list) {
		for (i = 0; i < ndev_allocated; i++) {
			if (new_device_list[i]) {
				if(new_device_list[i]->raid_map)
					os_mem_free(softs, (char *)new_device_list[i]->raid_map,
					    					sizeof(pqisrc_raid_map_t));
				os_mem_free(softs, (char*)new_device_list[i],
					    			sizeof(*new_device_list[i]));
			}
		}
		os_mem_free(softs, (char *)new_device_list,
			    		sizeof(*new_device_list) * ndev_allocated); 
	}
	if(physical_dev_list)
		os_mem_free(softs, (char *)physical_dev_list, phys_data_length);
    	if(logical_dev_list)
		os_mem_free(softs, (char *)logical_dev_list, log_data_length);
	if (bmic_phy_info)
		os_mem_free(softs, (char *)bmic_phy_info, sizeof(*bmic_phy_info));
	
	DBG_FUNC("OUT \n");

	return ret;
}

/*
 * Clean up memory allocated for devices.
 */
void pqisrc_cleanup_devices(pqisrc_softstate_t *softs)
{

	int i = 0,j = 0;
	pqi_scsi_dev_t *dvp = NULL;
	DBG_FUNC("IN\n");
	
 	for(i = 0; i < PQI_MAX_DEVICES; i++) {
		for(j = 0; j < PQI_MAX_MULTILUN; j++) {
			if (softs->device_list[i][j] == NULL) 
				continue;
			dvp = softs->device_list[i][j];
			pqisrc_device_mem_free(softs, dvp);
		}
	}
	DBG_FUNC("OUT\n");
}

