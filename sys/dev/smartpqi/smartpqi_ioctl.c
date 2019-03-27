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

/*
 * Management interface for smartpqi driver
 */

#include "smartpqi_includes.h"

/*
 * Wrapper function to copy to user from kernel
 */
int os_copy_to_user(struct pqisrc_softstate *softs, void *dest_buf,
		void *src_buf, int size, int mode)
{
	return(copyout(src_buf, dest_buf, size));
}

/*
 * Wrapper function to copy from user to kernel
 */
int os_copy_from_user(struct pqisrc_softstate *softs, void *dest_buf,
		void *src_buf, int size, int mode)
{
	return(copyin(src_buf, dest_buf, size));
}

/*
 * Device open function for ioctl entry 
 */
static int smartpqi_open(struct cdev *cdev, int flags, int devtype,
		struct thread *td)
{
	int error = PQI_STATUS_SUCCESS;
	
	return error;
}

/*
 * Device close function for ioctl entry 
 */
static int smartpqi_close(struct cdev *cdev, int flags, int devtype,
		struct thread *td)
{
	int error = PQI_STATUS_SUCCESS;

	return error;
}

/*
 * ioctl for getting driver info
 */
static void smartpqi_get_driver_info_ioctl(caddr_t udata, struct cdev *cdev)
{
	struct pqisrc_softstate *softs = cdev->si_drv1;
	pdriver_info driver_info = (pdriver_info)udata;

	DBG_FUNC("IN udata = %p cdev = %p\n", udata, cdev);

	driver_info->major_version = PQISRC_DRIVER_MAJOR;
	driver_info->minor_version = PQISRC_DRIVER_MINOR;
	driver_info->release_version = PQISRC_DRIVER_RELEASE;
	driver_info->build_revision = PQISRC_DRIVER_REVISION;
	driver_info->max_targets = PQI_MAX_DEVICES - 1;
	driver_info->max_io = softs->max_io_for_scsi_ml;
	driver_info->max_transfer_length = softs->pqi_cap.max_transfer_size;

	DBG_FUNC("OUT\n");
}

/*
 * ioctl for getting controller info
 */
static void smartpqi_get_pci_info_ioctl(caddr_t udata, struct cdev *cdev)
{
	struct pqisrc_softstate *softs = cdev->si_drv1;
	device_t dev = softs->os_specific.pqi_dev;
	pqi_pci_info_t *pci_info = (pqi_pci_info_t *)udata;
	uint32_t sub_vendor = 0;
	uint32_t sub_device = 0;
	uint32_t vendor = 0;
	uint32_t device = 0;

	DBG_FUNC("IN udata = %p cdev = %p\n", udata, cdev);

	pci_info->bus = pci_get_bus(dev);
	pci_info->dev_fn = pci_get_function(dev);
	pci_info->domain = pci_get_domain(dev);
	sub_vendor = pci_read_config(dev, PCIR_SUBVEND_0, 2);
	sub_device = pci_read_config(dev, PCIR_SUBDEV_0, 2);
	pci_info->board_id = ((sub_device << 16) & 0xffff0000) | sub_vendor;
	vendor = pci_get_vendor(dev);
	device =  pci_get_device(dev);
	pci_info->chip_id = ((device << 16) & 0xffff0000) | vendor;
	DBG_FUNC("OUT\n");
}


/*
 * ioctl entry point for user
 */
static int smartpqi_ioctl(struct cdev *cdev, u_long cmd, caddr_t udata,
		int flags, struct thread *td)
{
	int error = PQI_STATUS_SUCCESS;
	struct pqisrc_softstate *softs = cdev->si_drv1;

	DBG_FUNC("IN cmd = 0x%lx udata = %p cdev = %p\n", cmd, udata, cdev);

	if (!udata) {
		DBG_ERR("udata is null !!\n");
	}

	if (pqisrc_ctrl_offline(softs)){
		DBG_ERR("Controller s offline !!\n");
		return ENOTTY;
	}

	switch (cmd) {
		case CCISS_GETDRIVVER:
			smartpqi_get_driver_info_ioctl(udata, cdev);
			break;
		case CCISS_GETPCIINFO:
			smartpqi_get_pci_info_ioctl(udata, cdev);
			break;
		case SMARTPQI_PASS_THRU:
		case CCISS_PASSTHRU:
			error = pqisrc_passthru_ioctl(softs, udata, 0);
			error = PQI_STATUS_SUCCESS;
			break;
		case CCISS_REGNEWD:
			error = pqisrc_scan_devices(softs);
			break;
		default:
			DBG_WARN( "!IOCTL cmd 0x%lx not supported", cmd);
			error = ENOTTY;
			break;
	}

	DBG_FUNC("OUT error = %d\n", error);
	return error;
}

static struct cdevsw smartpqi_cdevsw =
{
	.d_version = D_VERSION,
	.d_open    = smartpqi_open,
	.d_close   = smartpqi_close,
	.d_ioctl   = smartpqi_ioctl,
	.d_name    = "smartpqi",
};

/*
 * Function to create device node for ioctl
 */
int create_char_dev(struct pqisrc_softstate *softs, int card_index)
{
	int error = PQI_STATUS_SUCCESS;

	DBG_FUNC("IN idx = %d\n", card_index);

	softs->os_specific.cdev = make_dev(&smartpqi_cdevsw, card_index,
				UID_ROOT, GID_OPERATOR, 0640,
				"smartpqi%u", card_index);
	if(softs->os_specific.cdev) {
		softs->os_specific.cdev->si_drv1 = softs;
	} else {
		error = PQI_STATUS_FAILURE;
	}

	DBG_FUNC("OUT error = %d\n", error);
	return error;
}

/*
 * Function to destroy device node for ioctl
 */
void destroy_char_dev(struct pqisrc_softstate *softs)
{
	DBG_FUNC("IN\n");
	if (softs->os_specific.cdev) {
		destroy_dev(softs->os_specific.cdev);
		softs->os_specific.cdev = NULL;
	}
	DBG_FUNC("OUT\n");
}

/*
 * Function used to send passthru commands to adapter
 * to support management tools. For eg. ssacli, sscon.
 */
int
pqisrc_passthru_ioctl(struct pqisrc_softstate *softs, void *arg, int mode)
{
	int ret = PQI_STATUS_SUCCESS;
	char *drv_buf = NULL;
	uint32_t tag = 0;
	IOCTL_Command_struct *iocommand = (IOCTL_Command_struct *)arg;
	dma_mem_t ioctl_dma_buf;
	pqisrc_raid_req_t request;
	raid_path_error_info_elem_t error_info;
	ib_queue_t *ib_q = &softs->op_raid_ib_q[PQI_DEFAULT_IB_QUEUE];
	ob_queue_t *ob_q = &softs->op_ob_q[PQI_DEFAULT_IB_QUEUE];
	rcb_t *rcb = NULL;

	memset(&request, 0, sizeof(request));
	memset(&error_info, 0, sizeof(error_info));
		
	DBG_FUNC("IN");

	if (pqisrc_ctrl_offline(softs))
		return PQI_STATUS_FAILURE;
	
	if (!arg)
		return (PQI_STATUS_FAILURE);

	if (iocommand->buf_size < 1 && 
		iocommand->Request.Type.Direction != PQIIOCTL_NONE)
		return PQI_STATUS_FAILURE;
	if (iocommand->Request.CDBLen > sizeof(request.cdb))
		return PQI_STATUS_FAILURE;

	switch (iocommand->Request.Type.Direction) {
		case PQIIOCTL_NONE:
		case PQIIOCTL_WRITE:
		case PQIIOCTL_READ:
		case PQIIOCTL_BIDIRECTIONAL:
			break;
		default:
			return PQI_STATUS_FAILURE;
	}

	if (iocommand->buf_size > 0) {
		memset(&ioctl_dma_buf, 0, sizeof(struct dma_mem));
		ioctl_dma_buf.tag = "Ioctl_PassthruCmd_Buffer";
		ioctl_dma_buf.size = iocommand->buf_size;
		ioctl_dma_buf.align = PQISRC_DEFAULT_DMA_ALIGN;
		/* allocate memory */
		ret = os_dma_mem_alloc(softs, &ioctl_dma_buf);
		if (ret) {
			DBG_ERR("Failed to Allocate dma mem for Ioctl PassthruCmd Buffer : %d\n", ret);
			ret = PQI_STATUS_FAILURE;
			goto out;
		}
		 
		DBG_INFO("ioctl_dma_buf.dma_addr  = %p\n",(void*)ioctl_dma_buf.dma_addr);
		DBG_INFO("ioctl_dma_buf.virt_addr = %p\n",(void*)ioctl_dma_buf.virt_addr);
	
		drv_buf = (char *)ioctl_dma_buf.virt_addr;
		if (iocommand->Request.Type.Direction & PQIIOCTL_WRITE) {
        		if ((ret = os_copy_from_user(softs, (void *)drv_buf, (void *)iocommand->buf, 
						iocommand->buf_size, mode)) != 0) { 
				ret = PQI_STATUS_FAILURE;
				goto free_mem;
			}
		}
	}
	
	request.header.iu_type = PQI_IU_TYPE_RAID_PATH_IO_REQUEST;
	request.header.iu_length = offsetof(pqisrc_raid_req_t, sg_descriptors[1]) - 
									PQI_REQUEST_HEADER_LENGTH;
	memcpy(request.lun_number, iocommand->LUN_info.LunAddrBytes, 
		sizeof(request.lun_number));
	memcpy(request.cdb, iocommand->Request.CDB, iocommand->Request.CDBLen);
	request.additional_cdb_bytes_usage = PQI_ADDITIONAL_CDB_BYTES_0;
	
	switch (iocommand->Request.Type.Direction) {
	case PQIIOCTL_NONE:
		request.data_direction = SOP_DATA_DIR_NONE;
		break;
	case PQIIOCTL_WRITE:
		request.data_direction = SOP_DATA_DIR_FROM_DEVICE;
		break;
	case PQIIOCTL_READ:
		request.data_direction = SOP_DATA_DIR_TO_DEVICE;
		break;
	case PQIIOCTL_BIDIRECTIONAL:
		request.data_direction = SOP_DATA_DIR_BIDIRECTIONAL;
		break;
	}

	request.task_attribute = SOP_TASK_ATTRIBUTE_SIMPLE;
	if (iocommand->buf_size > 0) {
		request.buffer_length = iocommand->buf_size;
		request.sg_descriptors[0].addr = ioctl_dma_buf.dma_addr;
		request.sg_descriptors[0].len = iocommand->buf_size;
		request.sg_descriptors[0].flags =  SG_FLAG_LAST;
	}
	tag = pqisrc_get_tag(&softs->taglist);
	if (INVALID_ELEM == tag) {
		DBG_ERR("Tag not available\n");
		ret = PQI_STATUS_FAILURE;
		goto free_mem;
	}
	request.request_id = tag;
	request.response_queue_id = ob_q->q_id;
	request.error_index = request.request_id;
	rcb = &softs->rcb[tag];

	rcb->success_cmp_callback = pqisrc_process_internal_raid_response_success;
	rcb->error_cmp_callback = pqisrc_process_internal_raid_response_error;
	rcb->tag = tag;
	rcb->req_pending = true;
	/* Submit Command */
	ret = pqisrc_submit_cmnd(softs, ib_q, &request);
	if (ret != PQI_STATUS_SUCCESS) {
		DBG_ERR("Unable to submit command\n");
		goto err_out;
	}

	ret = pqisrc_wait_on_condition(softs, rcb);
	if (ret != PQI_STATUS_SUCCESS) {
		DBG_ERR("Passthru IOCTL cmd timed out !!\n");
		goto err_out;
	}

	memset(&iocommand->error_info, 0, sizeof(iocommand->error_info));


	if (rcb->status) {
		size_t sense_data_length;

		memcpy(&error_info, rcb->error_info, sizeof(error_info));
		iocommand->error_info.ScsiStatus = error_info.status;
		sense_data_length = error_info.sense_data_len;

		if (!sense_data_length)
			sense_data_length = error_info.resp_data_len;

		if (sense_data_length && 
			(sense_data_length > sizeof(error_info.data)))
				sense_data_length = sizeof(error_info.data);

		if (sense_data_length) {
			if (sense_data_length >
				sizeof(iocommand->error_info.SenseInfo))
				sense_data_length =
					sizeof(iocommand->error_info.SenseInfo);
			memcpy (iocommand->error_info.SenseInfo,
					error_info.data, sense_data_length);
			iocommand->error_info.SenseLen = sense_data_length;
		}

		if (error_info.data_out_result == 
				PQI_RAID_DATA_IN_OUT_UNDERFLOW){
			rcb->status = REQUEST_SUCCESS;
		}
	}

	if (rcb->status == REQUEST_SUCCESS && iocommand->buf_size > 0 && 
		(iocommand->Request.Type.Direction & PQIIOCTL_READ)) {

		if ((ret = os_copy_to_user(softs, (void*)iocommand->buf, 
			(void*)drv_buf, iocommand->buf_size, mode)) != 0) {
				DBG_ERR("Failed to copy the response\n");	
				goto err_out;
		}
	}

	os_reset_rcb(rcb); 
	pqisrc_put_tag(&softs->taglist, request.request_id);
	if (iocommand->buf_size > 0)
			os_dma_mem_free(softs,&ioctl_dma_buf);

	DBG_FUNC("OUT\n");
	return ret;
err_out:
	os_reset_rcb(rcb); 
	pqisrc_put_tag(&softs->taglist, request.request_id);

free_mem:
	if (iocommand->buf_size > 0)
		os_dma_mem_free(softs, &ioctl_dma_buf);

out:
	DBG_FUNC("Failed OUT\n");
	return PQI_STATUS_FAILURE;
}
