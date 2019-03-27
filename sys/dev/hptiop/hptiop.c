/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * HighPoint RR3xxx/4xxx RAID Driver for FreeBSD
 * Copyright (C) 2007-2012 HighPoint Technologies, Inc. All Rights Reserved.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/cons.h>
#include <sys/time.h>
#include <sys/systm.h>

#include <sys/stat.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/libkern.h>
#include <sys/kernel.h>

#include <sys/kthread.h>
#include <sys/mutex.h>
#include <sys/module.h>

#include <sys/eventhandler.h>
#include <sys/bus.h>
#include <sys/taskqueue.h>
#include <sys/ioccom.h>

#include <machine/resource.h>
#include <machine/bus.h>
#include <machine/stdarg.h>
#include <sys/rman.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>


#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_debug.h>
#include <cam/cam_periph.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>


#include <dev/hptiop/hptiop.h>

static const char driver_name[] = "hptiop";
static const char driver_version[] = "v1.9";

static devclass_t hptiop_devclass;

static int hptiop_send_sync_msg(struct hpt_iop_hba *hba,
				u_int32_t msg, u_int32_t millisec);
static void hptiop_request_callback_itl(struct hpt_iop_hba *hba,
							u_int32_t req);
static void hptiop_request_callback_mv(struct hpt_iop_hba *hba, u_int64_t req);
static void hptiop_request_callback_mvfrey(struct hpt_iop_hba *hba,
							u_int32_t req);
static void hptiop_os_message_callback(struct hpt_iop_hba *hba, u_int32_t msg);
static int  hptiop_do_ioctl_itl(struct hpt_iop_hba *hba,
				struct hpt_iop_ioctl_param *pParams);
static int  hptiop_do_ioctl_mv(struct hpt_iop_hba *hba,
				struct hpt_iop_ioctl_param *pParams);
static int  hptiop_do_ioctl_mvfrey(struct hpt_iop_hba *hba,
				struct hpt_iop_ioctl_param *pParams);
static int  hptiop_rescan_bus(struct hpt_iop_hba *hba);
static int hptiop_alloc_pci_res_itl(struct hpt_iop_hba *hba);
static int hptiop_alloc_pci_res_mv(struct hpt_iop_hba *hba);
static int hptiop_alloc_pci_res_mvfrey(struct hpt_iop_hba *hba);
static int hptiop_get_config_itl(struct hpt_iop_hba *hba,
				struct hpt_iop_request_get_config *config);
static int hptiop_get_config_mv(struct hpt_iop_hba *hba,
				struct hpt_iop_request_get_config *config);
static int hptiop_get_config_mvfrey(struct hpt_iop_hba *hba,
				struct hpt_iop_request_get_config *config);
static int hptiop_set_config_itl(struct hpt_iop_hba *hba,
				struct hpt_iop_request_set_config *config);
static int hptiop_set_config_mv(struct hpt_iop_hba *hba,
				struct hpt_iop_request_set_config *config);
static int hptiop_set_config_mvfrey(struct hpt_iop_hba *hba,
				struct hpt_iop_request_set_config *config);
static int hptiop_internal_memalloc_mv(struct hpt_iop_hba *hba);
static int hptiop_internal_memalloc_mvfrey(struct hpt_iop_hba *hba);
static int hptiop_internal_memfree_itl(struct hpt_iop_hba *hba);
static int hptiop_internal_memfree_mv(struct hpt_iop_hba *hba);
static int hptiop_internal_memfree_mvfrey(struct hpt_iop_hba *hba);
static int  hptiop_post_ioctl_command_itl(struct hpt_iop_hba *hba,
			u_int32_t req32, struct hpt_iop_ioctl_param *pParams);
static int  hptiop_post_ioctl_command_mv(struct hpt_iop_hba *hba,
				struct hpt_iop_request_ioctl_command *req,
				struct hpt_iop_ioctl_param *pParams);
static int  hptiop_post_ioctl_command_mvfrey(struct hpt_iop_hba *hba,
				struct hpt_iop_request_ioctl_command *req,
				struct hpt_iop_ioctl_param *pParams);
static void hptiop_post_req_itl(struct hpt_iop_hba *hba,
				struct hpt_iop_srb *srb,
				bus_dma_segment_t *segs, int nsegs);
static void hptiop_post_req_mv(struct hpt_iop_hba *hba,
				struct hpt_iop_srb *srb,
				bus_dma_segment_t *segs, int nsegs);
static void hptiop_post_req_mvfrey(struct hpt_iop_hba *hba,
				struct hpt_iop_srb *srb,
				bus_dma_segment_t *segs, int nsegs);
static void hptiop_post_msg_itl(struct hpt_iop_hba *hba, u_int32_t msg);
static void hptiop_post_msg_mv(struct hpt_iop_hba *hba, u_int32_t msg);
static void hptiop_post_msg_mvfrey(struct hpt_iop_hba *hba, u_int32_t msg);
static void hptiop_enable_intr_itl(struct hpt_iop_hba *hba);
static void hptiop_enable_intr_mv(struct hpt_iop_hba *hba);
static void hptiop_enable_intr_mvfrey(struct hpt_iop_hba *hba);
static void hptiop_disable_intr_itl(struct hpt_iop_hba *hba);
static void hptiop_disable_intr_mv(struct hpt_iop_hba *hba);
static void hptiop_disable_intr_mvfrey(struct hpt_iop_hba *hba);
static void hptiop_free_srb(struct hpt_iop_hba *hba, struct hpt_iop_srb *srb);
static int  hptiop_os_query_remove_device(struct hpt_iop_hba *hba, int tid);
static int  hptiop_probe(device_t dev);
static int  hptiop_attach(device_t dev);
static int  hptiop_detach(device_t dev);
static int  hptiop_shutdown(device_t dev);
static void hptiop_action(struct cam_sim *sim, union ccb *ccb);
static void hptiop_poll(struct cam_sim *sim);
static void hptiop_async(void *callback_arg, u_int32_t code,
					struct cam_path *path, void *arg);
static void hptiop_pci_intr(void *arg);
static void hptiop_release_resource(struct hpt_iop_hba *hba);
static void hptiop_reset_adapter(void *argv);
static d_open_t hptiop_open;
static d_close_t hptiop_close;
static d_ioctl_t hptiop_ioctl;

static struct cdevsw hptiop_cdevsw = {
	.d_open = hptiop_open,
	.d_close = hptiop_close,
	.d_ioctl = hptiop_ioctl,
	.d_name = driver_name,
	.d_version = D_VERSION,
};

#define hba_from_dev(dev) \
	((struct hpt_iop_hba *)devclass_get_softc(hptiop_devclass, dev2unit(dev)))

#define BUS_SPACE_WRT4_ITL(offset, value) bus_space_write_4(hba->bar0t,\
		hba->bar0h, offsetof(struct hpt_iopmu_itl, offset), (value))
#define BUS_SPACE_RD4_ITL(offset) bus_space_read_4(hba->bar0t,\
		hba->bar0h, offsetof(struct hpt_iopmu_itl, offset))

#define BUS_SPACE_WRT4_MV0(offset, value) bus_space_write_4(hba->bar0t,\
		hba->bar0h, offsetof(struct hpt_iopmv_regs, offset), value)
#define BUS_SPACE_RD4_MV0(offset) bus_space_read_4(hba->bar0t,\
		hba->bar0h, offsetof(struct hpt_iopmv_regs, offset))
#define BUS_SPACE_WRT4_MV2(offset, value) bus_space_write_4(hba->bar2t,\
		hba->bar2h, offsetof(struct hpt_iopmu_mv, offset), value)
#define BUS_SPACE_RD4_MV2(offset) bus_space_read_4(hba->bar2t,\
		hba->bar2h, offsetof(struct hpt_iopmu_mv, offset))

#define BUS_SPACE_WRT4_MVFREY2(offset, value) bus_space_write_4(hba->bar2t,\
		hba->bar2h, offsetof(struct hpt_iopmu_mvfrey, offset), value)
#define BUS_SPACE_RD4_MVFREY2(offset) bus_space_read_4(hba->bar2t,\
		hba->bar2h, offsetof(struct hpt_iopmu_mvfrey, offset))

static int hptiop_open(ioctl_dev_t dev, int flags,
					int devtype, ioctl_thread_t proc)
{
	struct hpt_iop_hba *hba = hba_from_dev(dev);

	if (hba==NULL)
		return ENXIO;
	if (hba->flag & HPT_IOCTL_FLAG_OPEN)
		return EBUSY;
	hba->flag |= HPT_IOCTL_FLAG_OPEN;
	return 0;
}

static int hptiop_close(ioctl_dev_t dev, int flags,
					int devtype, ioctl_thread_t proc)
{
	struct hpt_iop_hba *hba = hba_from_dev(dev);
	hba->flag &= ~(u_int32_t)HPT_IOCTL_FLAG_OPEN;
	return 0;
}

static int hptiop_ioctl(ioctl_dev_t dev, u_long cmd, caddr_t data,
					int flags, ioctl_thread_t proc)
{
	int ret = EFAULT;
	struct hpt_iop_hba *hba = hba_from_dev(dev);

	mtx_lock(&Giant);

	switch (cmd) {
	case HPT_DO_IOCONTROL:
		ret = hba->ops->do_ioctl(hba,
				(struct hpt_iop_ioctl_param *)data);
		break;
	case HPT_SCAN_BUS:
		ret = hptiop_rescan_bus(hba);
		break;
	}

	mtx_unlock(&Giant);

	return ret;
}

static u_int64_t hptiop_mv_outbound_read(struct hpt_iop_hba *hba)
{
	u_int64_t p;
	u_int32_t outbound_tail = BUS_SPACE_RD4_MV2(outbound_tail);
	u_int32_t outbound_head = BUS_SPACE_RD4_MV2(outbound_head);

	if (outbound_tail != outbound_head) {
		bus_space_read_region_4(hba->bar2t, hba->bar2h,
			offsetof(struct hpt_iopmu_mv,
				outbound_q[outbound_tail]),
			(u_int32_t *)&p, 2);

		outbound_tail++;

		if (outbound_tail == MVIOP_QUEUE_LEN)
			outbound_tail = 0;

		BUS_SPACE_WRT4_MV2(outbound_tail, outbound_tail);
		return p;
	} else
		return 0;
}

static void hptiop_mv_inbound_write(u_int64_t p, struct hpt_iop_hba *hba)
{
	u_int32_t inbound_head = BUS_SPACE_RD4_MV2(inbound_head);
	u_int32_t head = inbound_head + 1;

	if (head == MVIOP_QUEUE_LEN)
		head = 0;

	bus_space_write_region_4(hba->bar2t, hba->bar2h,
			offsetof(struct hpt_iopmu_mv, inbound_q[inbound_head]),
			(u_int32_t *)&p, 2);
	BUS_SPACE_WRT4_MV2(inbound_head, head);
	BUS_SPACE_WRT4_MV0(inbound_doorbell, MVIOP_MU_INBOUND_INT_POSTQUEUE);
}

static void hptiop_post_msg_itl(struct hpt_iop_hba *hba, u_int32_t msg)
{
	BUS_SPACE_WRT4_ITL(inbound_msgaddr0, msg);
	BUS_SPACE_RD4_ITL(outbound_intstatus);
}

static void hptiop_post_msg_mv(struct hpt_iop_hba *hba, u_int32_t msg)
{

	BUS_SPACE_WRT4_MV2(inbound_msg, msg);
	BUS_SPACE_WRT4_MV0(inbound_doorbell, MVIOP_MU_INBOUND_INT_MSG);

	BUS_SPACE_RD4_MV0(outbound_intmask);
}

static void hptiop_post_msg_mvfrey(struct hpt_iop_hba *hba, u_int32_t msg)
{
	BUS_SPACE_WRT4_MVFREY2(f0_to_cpu_msg_a, msg);
	BUS_SPACE_RD4_MVFREY2(f0_to_cpu_msg_a);
}

static int hptiop_wait_ready_itl(struct hpt_iop_hba * hba, u_int32_t millisec)
{
	u_int32_t req=0;
	int i;

	for (i = 0; i < millisec; i++) {
		req = BUS_SPACE_RD4_ITL(inbound_queue);
		if (req != IOPMU_QUEUE_EMPTY)
			break;
		DELAY(1000);
	}

	if (req!=IOPMU_QUEUE_EMPTY) {
		BUS_SPACE_WRT4_ITL(outbound_queue, req);
		BUS_SPACE_RD4_ITL(outbound_intstatus);
		return 0;
	}

	return -1;
}

static int hptiop_wait_ready_mv(struct hpt_iop_hba * hba, u_int32_t millisec)
{
	if (hptiop_send_sync_msg(hba, IOPMU_INBOUND_MSG0_NOP, millisec))
		return -1;

	return 0;
}

static int hptiop_wait_ready_mvfrey(struct hpt_iop_hba * hba,
							u_int32_t millisec)
{
	if (hptiop_send_sync_msg(hba, IOPMU_INBOUND_MSG0_NOP, millisec))
		return -1;

	return 0;
}

static void hptiop_request_callback_itl(struct hpt_iop_hba * hba,
							u_int32_t index)
{
	struct hpt_iop_srb *srb;
	struct hpt_iop_request_scsi_command *req=NULL;
	union ccb *ccb;
	u_int8_t *cdb;
	u_int32_t result, temp, dxfer;
	u_int64_t temp64;

	if (index & IOPMU_QUEUE_MASK_HOST_BITS) { /*host req*/
		if (hba->firmware_version > 0x01020000 ||
			hba->interface_version > 0x01020000) {
			srb = hba->srb[index & ~(u_int32_t)
				(IOPMU_QUEUE_ADDR_HOST_BIT
				| IOPMU_QUEUE_REQUEST_RESULT_BIT)];
			req = (struct hpt_iop_request_scsi_command *)srb;
			if (index & IOPMU_QUEUE_REQUEST_RESULT_BIT)
				result = IOP_RESULT_SUCCESS;
			else
				result = req->header.result;
		} else {
			srb = hba->srb[index &
				~(u_int32_t)IOPMU_QUEUE_ADDR_HOST_BIT];
			req = (struct hpt_iop_request_scsi_command *)srb;
			result = req->header.result;
		}
		dxfer = req->dataxfer_length;
		goto srb_complete;
	}

	/*iop req*/
	temp = bus_space_read_4(hba->bar0t, hba->bar0h, index +
		offsetof(struct hpt_iop_request_header, type));
	result = bus_space_read_4(hba->bar0t, hba->bar0h, index +
		offsetof(struct hpt_iop_request_header, result));
	switch(temp) {
	case IOP_REQUEST_TYPE_IOCTL_COMMAND:
	{
		temp64 = 0;
		bus_space_write_region_4(hba->bar0t, hba->bar0h, index +
			offsetof(struct hpt_iop_request_header, context),
			(u_int32_t *)&temp64, 2);
		wakeup((void *)((unsigned long)hba->u.itl.mu + index));
		break;
	}

	case IOP_REQUEST_TYPE_SCSI_COMMAND:
		bus_space_read_region_4(hba->bar0t, hba->bar0h, index +
			offsetof(struct hpt_iop_request_header, context),
			(u_int32_t *)&temp64, 2);
		srb = (struct hpt_iop_srb *)(unsigned long)temp64;
		dxfer = bus_space_read_4(hba->bar0t, hba->bar0h, 
				index + offsetof(struct hpt_iop_request_scsi_command,
				dataxfer_length));	
srb_complete:
		ccb = (union ccb *)srb->ccb;
		if (ccb->ccb_h.flags & CAM_CDB_POINTER)
			cdb = ccb->csio.cdb_io.cdb_ptr;
		else
			cdb = ccb->csio.cdb_io.cdb_bytes;

		if (cdb[0] == SYNCHRONIZE_CACHE) { /* ??? */
			ccb->ccb_h.status = CAM_REQ_CMP;
			goto scsi_done;
		}

		switch (result) {
		case IOP_RESULT_SUCCESS:
			switch (ccb->ccb_h.flags & CAM_DIR_MASK) {
			case CAM_DIR_IN:
				bus_dmamap_sync(hba->io_dmat,
					srb->dma_map, BUS_DMASYNC_POSTREAD);
				bus_dmamap_unload(hba->io_dmat, srb->dma_map);
				break;
			case CAM_DIR_OUT:
				bus_dmamap_sync(hba->io_dmat,
					srb->dma_map, BUS_DMASYNC_POSTWRITE);
				bus_dmamap_unload(hba->io_dmat, srb->dma_map);
				break;
			}

			ccb->ccb_h.status = CAM_REQ_CMP;
			break;

		case IOP_RESULT_BAD_TARGET:
			ccb->ccb_h.status = CAM_DEV_NOT_THERE;
			break;
		case IOP_RESULT_BUSY:
			ccb->ccb_h.status = CAM_BUSY;
			break;
		case IOP_RESULT_INVALID_REQUEST:
			ccb->ccb_h.status = CAM_REQ_INVALID;
			break;
		case IOP_RESULT_FAIL:
			ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR;
			break;
		case IOP_RESULT_RESET:
			ccb->ccb_h.status = CAM_BUSY;
			break;
		case IOP_RESULT_CHECK_CONDITION:
			memset(&ccb->csio.sense_data, 0,
			    sizeof(ccb->csio.sense_data));
			if (dxfer < ccb->csio.sense_len)
				ccb->csio.sense_resid = ccb->csio.sense_len -
				    dxfer;
			else
				ccb->csio.sense_resid = 0;
			if (srb->srb_flag & HPT_SRB_FLAG_HIGH_MEM_ACESS) {/*iop*/
				bus_space_read_region_1(hba->bar0t, hba->bar0h,
					index + offsetof(struct hpt_iop_request_scsi_command,
					sg_list), (u_int8_t *)&ccb->csio.sense_data, 
					MIN(dxfer, sizeof(ccb->csio.sense_data)));
			} else {
				memcpy(&ccb->csio.sense_data, &req->sg_list, 
					MIN(dxfer, sizeof(ccb->csio.sense_data)));
			}
			ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR;
			ccb->ccb_h.status |= CAM_AUTOSNS_VALID;
			ccb->csio.scsi_status = SCSI_STATUS_CHECK_COND;
			break;
		default:
			ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR;
			break;
		}
scsi_done:
		if (srb->srb_flag & HPT_SRB_FLAG_HIGH_MEM_ACESS)
			BUS_SPACE_WRT4_ITL(outbound_queue, index);

		ccb->csio.resid = ccb->csio.dxfer_len - dxfer;

		hptiop_free_srb(hba, srb);
		xpt_done(ccb);
		break;
	}
}

static void hptiop_drain_outbound_queue_itl(struct hpt_iop_hba *hba)
{
	u_int32_t req, temp;

	while ((req = BUS_SPACE_RD4_ITL(outbound_queue)) !=IOPMU_QUEUE_EMPTY) {
		if (req & IOPMU_QUEUE_MASK_HOST_BITS)
			hptiop_request_callback_itl(hba, req);
		else {
			struct hpt_iop_request_header *p;

			p = (struct hpt_iop_request_header *)
				((char *)hba->u.itl.mu + req);
			temp = bus_space_read_4(hba->bar0t,
					hba->bar0h,req +
					offsetof(struct hpt_iop_request_header,
						flags));
			if (temp & IOP_REQUEST_FLAG_SYNC_REQUEST) {
				u_int64_t temp64;
				bus_space_read_region_4(hba->bar0t,
					hba->bar0h,req +
					offsetof(struct hpt_iop_request_header,
						context),
					(u_int32_t *)&temp64, 2);
				if (temp64) {
					hptiop_request_callback_itl(hba, req);
				} else {
					temp64 = 1;
					bus_space_write_region_4(hba->bar0t,
						hba->bar0h,req +
						offsetof(struct hpt_iop_request_header,
							context),
						(u_int32_t *)&temp64, 2);
				}
			} else
				hptiop_request_callback_itl(hba, req);
		}
	}
}

static int hptiop_intr_itl(struct hpt_iop_hba * hba)
{
	u_int32_t status;
	int ret = 0;

	status = BUS_SPACE_RD4_ITL(outbound_intstatus);

	if (status & IOPMU_OUTBOUND_INT_MSG0) {
		u_int32_t msg = BUS_SPACE_RD4_ITL(outbound_msgaddr0);
		KdPrint(("hptiop: received outbound msg %x\n", msg));
		BUS_SPACE_WRT4_ITL(outbound_intstatus, IOPMU_OUTBOUND_INT_MSG0);
		hptiop_os_message_callback(hba, msg);
		ret = 1;
	}

	if (status & IOPMU_OUTBOUND_INT_POSTQUEUE) {
		hptiop_drain_outbound_queue_itl(hba);
		ret = 1;
	}

	return ret;
}

static void hptiop_request_callback_mv(struct hpt_iop_hba * hba,
							u_int64_t _tag)
{
	u_int32_t context = (u_int32_t)_tag;

	if (context & MVIOP_CMD_TYPE_SCSI) {
		struct hpt_iop_srb *srb;
		struct hpt_iop_request_scsi_command *req;
		union ccb *ccb;
		u_int8_t *cdb;

		srb = hba->srb[context >> MVIOP_REQUEST_NUMBER_START_BIT];
		req = (struct hpt_iop_request_scsi_command *)srb;
		ccb = (union ccb *)srb->ccb;
		if (ccb->ccb_h.flags & CAM_CDB_POINTER)
			cdb = ccb->csio.cdb_io.cdb_ptr;
		else
			cdb = ccb->csio.cdb_io.cdb_bytes;

		if (cdb[0] == SYNCHRONIZE_CACHE) { /* ??? */
			ccb->ccb_h.status = CAM_REQ_CMP;
			goto scsi_done;
		}
		if (context & MVIOP_MU_QUEUE_REQUEST_RESULT_BIT)
			req->header.result = IOP_RESULT_SUCCESS;

		switch (req->header.result) {
		case IOP_RESULT_SUCCESS:
			switch (ccb->ccb_h.flags & CAM_DIR_MASK) {
			case CAM_DIR_IN:
				bus_dmamap_sync(hba->io_dmat,
					srb->dma_map, BUS_DMASYNC_POSTREAD);
				bus_dmamap_unload(hba->io_dmat, srb->dma_map);
				break;
			case CAM_DIR_OUT:
				bus_dmamap_sync(hba->io_dmat,
					srb->dma_map, BUS_DMASYNC_POSTWRITE);
				bus_dmamap_unload(hba->io_dmat, srb->dma_map);
				break;
			}
			ccb->ccb_h.status = CAM_REQ_CMP;
			break;
		case IOP_RESULT_BAD_TARGET:
			ccb->ccb_h.status = CAM_DEV_NOT_THERE;
			break;
		case IOP_RESULT_BUSY:
			ccb->ccb_h.status = CAM_BUSY;
			break;
		case IOP_RESULT_INVALID_REQUEST:
			ccb->ccb_h.status = CAM_REQ_INVALID;
			break;
		case IOP_RESULT_FAIL:
			ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR;
			break;
		case IOP_RESULT_RESET:
			ccb->ccb_h.status = CAM_BUSY;
			break;
		case IOP_RESULT_CHECK_CONDITION:
			memset(&ccb->csio.sense_data, 0,
			    sizeof(ccb->csio.sense_data));
			if (req->dataxfer_length < ccb->csio.sense_len)
				ccb->csio.sense_resid = ccb->csio.sense_len -
				    req->dataxfer_length;
			else
				ccb->csio.sense_resid = 0;
			memcpy(&ccb->csio.sense_data, &req->sg_list, 
				MIN(req->dataxfer_length, sizeof(ccb->csio.sense_data)));
			ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR;
			ccb->ccb_h.status |= CAM_AUTOSNS_VALID;
			ccb->csio.scsi_status = SCSI_STATUS_CHECK_COND;
			break;
		default:
			ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR;
			break;
		}
scsi_done:
		ccb->csio.resid = ccb->csio.dxfer_len - req->dataxfer_length;
		
		hptiop_free_srb(hba, srb);
		xpt_done(ccb);
	} else if (context & MVIOP_CMD_TYPE_IOCTL) {
		struct hpt_iop_request_ioctl_command *req = hba->ctlcfg_ptr;
		if (context & MVIOP_MU_QUEUE_REQUEST_RESULT_BIT)
			hba->config_done = 1;
		else
			hba->config_done = -1;
		wakeup(req);
	} else if (context &
			(MVIOP_CMD_TYPE_SET_CONFIG |
				MVIOP_CMD_TYPE_GET_CONFIG))
		hba->config_done = 1;
	else {
		device_printf(hba->pcidev, "wrong callback type\n");
	}
}

static void hptiop_request_callback_mvfrey(struct hpt_iop_hba * hba,
				u_int32_t _tag)
{
	u_int32_t req_type = _tag & 0xf;

	struct hpt_iop_srb *srb;
	struct hpt_iop_request_scsi_command *req;
	union ccb *ccb;
	u_int8_t *cdb;

	switch (req_type) {
	case IOP_REQUEST_TYPE_GET_CONFIG:
	case IOP_REQUEST_TYPE_SET_CONFIG:
		hba->config_done = 1;
		break;

	case IOP_REQUEST_TYPE_SCSI_COMMAND:
		srb = hba->srb[(_tag >> 4) & 0xff];
		req = (struct hpt_iop_request_scsi_command *)srb;

		ccb = (union ccb *)srb->ccb;

		callout_stop(&srb->timeout);

		if (ccb->ccb_h.flags & CAM_CDB_POINTER)
			cdb = ccb->csio.cdb_io.cdb_ptr;
		else
			cdb = ccb->csio.cdb_io.cdb_bytes;

		if (cdb[0] == SYNCHRONIZE_CACHE) { /* ??? */
			ccb->ccb_h.status = CAM_REQ_CMP;
			goto scsi_done;
		}

		if (_tag & MVFREYIOPMU_QUEUE_REQUEST_RESULT_BIT)
			req->header.result = IOP_RESULT_SUCCESS;

		switch (req->header.result) {
		case IOP_RESULT_SUCCESS:
			switch (ccb->ccb_h.flags & CAM_DIR_MASK) {
			case CAM_DIR_IN:
				bus_dmamap_sync(hba->io_dmat,
						srb->dma_map, BUS_DMASYNC_POSTREAD);
				bus_dmamap_unload(hba->io_dmat, srb->dma_map);
				break;
			case CAM_DIR_OUT:
				bus_dmamap_sync(hba->io_dmat,
						srb->dma_map, BUS_DMASYNC_POSTWRITE);
				bus_dmamap_unload(hba->io_dmat, srb->dma_map);
				break;
			}
			ccb->ccb_h.status = CAM_REQ_CMP;
			break;
		case IOP_RESULT_BAD_TARGET:
			ccb->ccb_h.status = CAM_DEV_NOT_THERE;
			break;
		case IOP_RESULT_BUSY:
			ccb->ccb_h.status = CAM_BUSY;
			break;
		case IOP_RESULT_INVALID_REQUEST:
			ccb->ccb_h.status = CAM_REQ_INVALID;
			break;
		case IOP_RESULT_FAIL:
			ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR;
			break;
		case IOP_RESULT_RESET:
			ccb->ccb_h.status = CAM_BUSY;
			break;
		case IOP_RESULT_CHECK_CONDITION:
			memset(&ccb->csio.sense_data, 0,
			       sizeof(ccb->csio.sense_data));
			if (req->dataxfer_length < ccb->csio.sense_len)
				ccb->csio.sense_resid = ccb->csio.sense_len -
				req->dataxfer_length;
			else
				ccb->csio.sense_resid = 0;
			memcpy(&ccb->csio.sense_data, &req->sg_list, 
			       MIN(req->dataxfer_length, sizeof(ccb->csio.sense_data)));
			ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR;
			ccb->ccb_h.status |= CAM_AUTOSNS_VALID;
			ccb->csio.scsi_status = SCSI_STATUS_CHECK_COND;
			break;
		default:
			ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR;
			break;
		}
scsi_done:
		ccb->csio.resid = ccb->csio.dxfer_len - req->dataxfer_length;
		
		hptiop_free_srb(hba, srb);
		xpt_done(ccb);
		break;
	case IOP_REQUEST_TYPE_IOCTL_COMMAND:
		if (_tag & MVFREYIOPMU_QUEUE_REQUEST_RESULT_BIT)
			hba->config_done = 1;
		else
			hba->config_done = -1;
		wakeup((struct hpt_iop_request_ioctl_command *)hba->ctlcfg_ptr);
		break;
	default:
		device_printf(hba->pcidev, "wrong callback type\n");
		break;
	}
}

static void hptiop_drain_outbound_queue_mv(struct hpt_iop_hba * hba)
{
	u_int64_t req;

	while ((req = hptiop_mv_outbound_read(hba))) {
		if (req & MVIOP_MU_QUEUE_ADDR_HOST_BIT) {
			if (req & MVIOP_MU_QUEUE_REQUEST_RETURN_CONTEXT) {
				hptiop_request_callback_mv(hba, req);
			}
	    	}
	}
}

static int hptiop_intr_mv(struct hpt_iop_hba * hba)
{
	u_int32_t status;
	int ret = 0;

	status = BUS_SPACE_RD4_MV0(outbound_doorbell);

	if (status)
		BUS_SPACE_WRT4_MV0(outbound_doorbell, ~status);

	if (status & MVIOP_MU_OUTBOUND_INT_MSG) {
		u_int32_t msg = BUS_SPACE_RD4_MV2(outbound_msg);
		KdPrint(("hptiop: received outbound msg %x\n", msg));
		hptiop_os_message_callback(hba, msg);
		ret = 1;
	}

	if (status & MVIOP_MU_OUTBOUND_INT_POSTQUEUE) {
		hptiop_drain_outbound_queue_mv(hba);
		ret = 1;
	}

	return ret;
}

static int hptiop_intr_mvfrey(struct hpt_iop_hba * hba)
{
	u_int32_t status, _tag, cptr;
	int ret = 0;

	if (hba->initialized) {
		BUS_SPACE_WRT4_MVFREY2(pcie_f0_int_enable, 0);
	}

	status = BUS_SPACE_RD4_MVFREY2(f0_doorbell);
	if (status) {
		BUS_SPACE_WRT4_MVFREY2(f0_doorbell, status);
		if (status & CPU_TO_F0_DRBL_MSG_A_BIT) {
			u_int32_t msg = BUS_SPACE_RD4_MVFREY2(cpu_to_f0_msg_a);
			hptiop_os_message_callback(hba, msg);
		}
		ret = 1;
	}

	status = BUS_SPACE_RD4_MVFREY2(isr_cause);
	if (status) {
		BUS_SPACE_WRT4_MVFREY2(isr_cause, status);
		do {
			cptr = *hba->u.mvfrey.outlist_cptr & 0xff;
			while (hba->u.mvfrey.outlist_rptr != cptr) {
				hba->u.mvfrey.outlist_rptr++;
				if (hba->u.mvfrey.outlist_rptr == hba->u.mvfrey.list_count) {
					hba->u.mvfrey.outlist_rptr = 0;
				}
	
				_tag = hba->u.mvfrey.outlist[hba->u.mvfrey.outlist_rptr].val;
				hptiop_request_callback_mvfrey(hba, _tag);
				ret = 2;
			}
		} while (cptr != (*hba->u.mvfrey.outlist_cptr & 0xff));
	}

	if (hba->initialized) {
		BUS_SPACE_WRT4_MVFREY2(pcie_f0_int_enable, 0x1010);
	}

	return ret;
}

static int hptiop_send_sync_request_itl(struct hpt_iop_hba * hba,
					u_int32_t req32, u_int32_t millisec)
{
	u_int32_t i;
	u_int64_t temp64;

	BUS_SPACE_WRT4_ITL(inbound_queue, req32);
	BUS_SPACE_RD4_ITL(outbound_intstatus);

	for (i = 0; i < millisec; i++) {
		hptiop_intr_itl(hba);
		bus_space_read_region_4(hba->bar0t, hba->bar0h, req32 +
			offsetof(struct hpt_iop_request_header, context),
			(u_int32_t *)&temp64, 2);
		if (temp64)
			return 0;
		DELAY(1000);
	}

	return -1;
}

static int hptiop_send_sync_request_mv(struct hpt_iop_hba *hba,
					void *req, u_int32_t millisec)
{
	u_int32_t i;
	u_int64_t phy_addr;
	hba->config_done = 0;

	phy_addr = hba->ctlcfgcmd_phy |
			(u_int64_t)MVIOP_MU_QUEUE_ADDR_HOST_BIT;
	((struct hpt_iop_request_get_config *)req)->header.flags |=
		IOP_REQUEST_FLAG_SYNC_REQUEST |
		IOP_REQUEST_FLAG_OUTPUT_CONTEXT;
	hptiop_mv_inbound_write(phy_addr, hba);
	BUS_SPACE_RD4_MV0(outbound_intmask);

	for (i = 0; i < millisec; i++) {
		hptiop_intr_mv(hba);
		if (hba->config_done)
			return 0;
		DELAY(1000);
	}
	return -1;
}

static int hptiop_send_sync_request_mvfrey(struct hpt_iop_hba *hba,
					void *req, u_int32_t millisec)
{
	u_int32_t i, index;
	u_int64_t phy_addr;
	struct hpt_iop_request_header *reqhdr =
										(struct hpt_iop_request_header *)req;
	
	hba->config_done = 0;

	phy_addr = hba->ctlcfgcmd_phy;
	reqhdr->flags = IOP_REQUEST_FLAG_SYNC_REQUEST
					| IOP_REQUEST_FLAG_OUTPUT_CONTEXT
					| IOP_REQUEST_FLAG_ADDR_BITS
					| ((phy_addr >> 16) & 0xffff0000);
	reqhdr->context = ((phy_addr & 0xffffffff) << 32 )
					| IOPMU_QUEUE_ADDR_HOST_BIT | reqhdr->type;

	hba->u.mvfrey.inlist_wptr++;
	index = hba->u.mvfrey.inlist_wptr & 0x3fff;

	if (index == hba->u.mvfrey.list_count) {
		index = 0;
		hba->u.mvfrey.inlist_wptr &= ~0x3fff;
		hba->u.mvfrey.inlist_wptr ^= CL_POINTER_TOGGLE;
	}

	hba->u.mvfrey.inlist[index].addr = phy_addr;
	hba->u.mvfrey.inlist[index].intrfc_len = (reqhdr->size + 3) / 4;

	BUS_SPACE_WRT4_MVFREY2(inbound_write_ptr, hba->u.mvfrey.inlist_wptr);
	BUS_SPACE_RD4_MVFREY2(inbound_write_ptr);

	for (i = 0; i < millisec; i++) {
		hptiop_intr_mvfrey(hba);
		if (hba->config_done)
			return 0;
		DELAY(1000);
	}
	return -1;
}

static int hptiop_send_sync_msg(struct hpt_iop_hba *hba,
					u_int32_t msg, u_int32_t millisec)
{
	u_int32_t i;

	hba->msg_done = 0;
	hba->ops->post_msg(hba, msg);

	for (i=0; i<millisec; i++) {
		hba->ops->iop_intr(hba);
		if (hba->msg_done)
			break;
		DELAY(1000);
	}

	return hba->msg_done? 0 : -1;
}

static int hptiop_get_config_itl(struct hpt_iop_hba * hba,
				struct hpt_iop_request_get_config * config)
{
	u_int32_t req32;

	config->header.size = sizeof(struct hpt_iop_request_get_config);
	config->header.type = IOP_REQUEST_TYPE_GET_CONFIG;
	config->header.flags = IOP_REQUEST_FLAG_SYNC_REQUEST;
	config->header.result = IOP_RESULT_PENDING;
	config->header.context = 0;

	req32 = BUS_SPACE_RD4_ITL(inbound_queue);
	if (req32 == IOPMU_QUEUE_EMPTY)
		return -1;

	bus_space_write_region_4(hba->bar0t, hba->bar0h,
			req32, (u_int32_t *)config,
			sizeof(struct hpt_iop_request_header) >> 2);

	if (hptiop_send_sync_request_itl(hba, req32, 20000)) {
		KdPrint(("hptiop: get config send cmd failed"));
		return -1;
	}

	bus_space_read_region_4(hba->bar0t, hba->bar0h,
			req32, (u_int32_t *)config,
			sizeof(struct hpt_iop_request_get_config) >> 2);

	BUS_SPACE_WRT4_ITL(outbound_queue, req32);

	return 0;
}

static int hptiop_get_config_mv(struct hpt_iop_hba * hba,
				struct hpt_iop_request_get_config * config)
{
	struct hpt_iop_request_get_config *req;

	if (!(req = hba->ctlcfg_ptr))
		return -1;

	req->header.flags = 0;
	req->header.type = IOP_REQUEST_TYPE_GET_CONFIG;
	req->header.size = sizeof(struct hpt_iop_request_get_config);
	req->header.result = IOP_RESULT_PENDING;
	req->header.context = MVIOP_CMD_TYPE_GET_CONFIG;

	if (hptiop_send_sync_request_mv(hba, req, 20000)) {
		KdPrint(("hptiop: get config send cmd failed"));
		return -1;
	}

	*config = *req;
	return 0;
}

static int hptiop_get_config_mvfrey(struct hpt_iop_hba * hba,
				struct hpt_iop_request_get_config * config)
{
	struct hpt_iop_request_get_config *info = hba->u.mvfrey.config;

	if (info->header.size != sizeof(struct hpt_iop_request_get_config) ||
	    info->header.type != IOP_REQUEST_TYPE_GET_CONFIG) {
		KdPrint(("hptiop: header size %x/%x type %x/%x",
			 info->header.size, (int)sizeof(struct hpt_iop_request_get_config),
			 info->header.type, IOP_REQUEST_TYPE_GET_CONFIG));
		return -1;
	}

	config->interface_version = info->interface_version;
	config->firmware_version = info->firmware_version;
	config->max_requests = info->max_requests;
	config->request_size = info->request_size;
	config->max_sg_count = info->max_sg_count;
	config->data_transfer_length = info->data_transfer_length;
	config->alignment_mask = info->alignment_mask;
	config->max_devices = info->max_devices;
	config->sdram_size = info->sdram_size;

	KdPrint(("hptiop: maxreq %x reqsz %x datalen %x maxdev %x sdram %x",
		 config->max_requests, config->request_size,
		 config->data_transfer_length, config->max_devices,
		 config->sdram_size));

	return 0;
}

static int hptiop_set_config_itl(struct hpt_iop_hba *hba,
				struct hpt_iop_request_set_config *config)
{
	u_int32_t req32;

	req32 = BUS_SPACE_RD4_ITL(inbound_queue);

	if (req32 == IOPMU_QUEUE_EMPTY)
		return -1;

	config->header.size = sizeof(struct hpt_iop_request_set_config);
	config->header.type = IOP_REQUEST_TYPE_SET_CONFIG;
	config->header.flags = IOP_REQUEST_FLAG_SYNC_REQUEST;
	config->header.result = IOP_RESULT_PENDING;
	config->header.context = 0;

	bus_space_write_region_4(hba->bar0t, hba->bar0h, req32, 
		(u_int32_t *)config, 
		sizeof(struct hpt_iop_request_set_config) >> 2);

	if (hptiop_send_sync_request_itl(hba, req32, 20000)) {
		KdPrint(("hptiop: set config send cmd failed"));
		return -1;
	}

	BUS_SPACE_WRT4_ITL(outbound_queue, req32);

	return 0;
}

static int hptiop_set_config_mv(struct hpt_iop_hba *hba,
				struct hpt_iop_request_set_config *config)
{
	struct hpt_iop_request_set_config *req;

	if (!(req = hba->ctlcfg_ptr))
		return -1;

	memcpy((u_int8_t *)req + sizeof(struct hpt_iop_request_header),
		(u_int8_t *)config + sizeof(struct hpt_iop_request_header),
		sizeof(struct hpt_iop_request_set_config) -
			sizeof(struct hpt_iop_request_header));

	req->header.flags = 0;
	req->header.type = IOP_REQUEST_TYPE_SET_CONFIG;
	req->header.size = sizeof(struct hpt_iop_request_set_config);
	req->header.result = IOP_RESULT_PENDING;
	req->header.context = MVIOP_CMD_TYPE_SET_CONFIG;

	if (hptiop_send_sync_request_mv(hba, req, 20000)) {
		KdPrint(("hptiop: set config send cmd failed"));
		return -1;
	}

	return 0;
}

static int hptiop_set_config_mvfrey(struct hpt_iop_hba *hba,
				struct hpt_iop_request_set_config *config)
{
	struct hpt_iop_request_set_config *req;

	if (!(req = hba->ctlcfg_ptr))
		return -1;

	memcpy((u_int8_t *)req + sizeof(struct hpt_iop_request_header),
		(u_int8_t *)config + sizeof(struct hpt_iop_request_header),
		sizeof(struct hpt_iop_request_set_config) -
			sizeof(struct hpt_iop_request_header));

	req->header.type = IOP_REQUEST_TYPE_SET_CONFIG;
	req->header.size = sizeof(struct hpt_iop_request_set_config);
	req->header.result = IOP_RESULT_PENDING;

	if (hptiop_send_sync_request_mvfrey(hba, req, 20000)) {
		KdPrint(("hptiop: set config send cmd failed"));
		return -1;
	}

	return 0;
}

static int hptiop_post_ioctl_command_itl(struct hpt_iop_hba *hba,
				u_int32_t req32,
				struct hpt_iop_ioctl_param *pParams)
{
	u_int64_t temp64;
	struct hpt_iop_request_ioctl_command req;

	if ((((pParams->nInBufferSize + 3) & ~3) + pParams->nOutBufferSize) >
			(hba->max_request_size -
			offsetof(struct hpt_iop_request_ioctl_command, buf))) {
		device_printf(hba->pcidev, "request size beyond max value");
		return -1;
	}

	req.header.size = offsetof(struct hpt_iop_request_ioctl_command, buf)
		+ pParams->nInBufferSize;
	req.header.type = IOP_REQUEST_TYPE_IOCTL_COMMAND;
	req.header.flags = IOP_REQUEST_FLAG_SYNC_REQUEST;
	req.header.result = IOP_RESULT_PENDING;
	req.header.context = req32 + (u_int64_t)(unsigned long)hba->u.itl.mu;
	req.ioctl_code = HPT_CTL_CODE_BSD_TO_IOP(pParams->dwIoControlCode);
	req.inbuf_size = pParams->nInBufferSize;
	req.outbuf_size = pParams->nOutBufferSize;
	req.bytes_returned = 0;

	bus_space_write_region_4(hba->bar0t, hba->bar0h, req32, (u_int32_t *)&req, 
		offsetof(struct hpt_iop_request_ioctl_command, buf)>>2);
	
	hptiop_lock_adapter(hba);

	BUS_SPACE_WRT4_ITL(inbound_queue, req32);
	BUS_SPACE_RD4_ITL(outbound_intstatus);

	bus_space_read_region_4(hba->bar0t, hba->bar0h, req32 +
		offsetof(struct hpt_iop_request_ioctl_command, header.context),
		(u_int32_t *)&temp64, 2);
	while (temp64) {
		if (hptiop_sleep(hba, (void *)((unsigned long)hba->u.itl.mu + req32),
				PPAUSE, "hptctl", HPT_OSM_TIMEOUT)==0)
			break;
		hptiop_send_sync_msg(hba, IOPMU_INBOUND_MSG0_RESET, 60000);
		bus_space_read_region_4(hba->bar0t, hba->bar0h,req32 +
			offsetof(struct hpt_iop_request_ioctl_command,
				header.context),
			(u_int32_t *)&temp64, 2);
	}

	hptiop_unlock_adapter(hba);
	return 0;
}

static int hptiop_bus_space_copyin(struct hpt_iop_hba *hba, u_int32_t bus,
									void *user, int size)
{
	unsigned char byte;
	int i;

	for (i=0; i<size; i++) {
		if (copyin((u_int8_t *)user + i, &byte, 1))
			return -1;
		bus_space_write_1(hba->bar0t, hba->bar0h, bus + i, byte);
	}

	return 0;
}

static int hptiop_bus_space_copyout(struct hpt_iop_hba *hba, u_int32_t bus,
									void *user, int size)
{
	unsigned char byte;
	int i;

	for (i=0; i<size; i++) {
		byte = bus_space_read_1(hba->bar0t, hba->bar0h, bus + i);
		if (copyout(&byte, (u_int8_t *)user + i, 1))
			return -1;
	}

	return 0;
}

static int hptiop_do_ioctl_itl(struct hpt_iop_hba *hba,
				struct hpt_iop_ioctl_param * pParams)
{
	u_int32_t req32;
	u_int32_t result;

	if ((pParams->Magic != HPT_IOCTL_MAGIC) &&
		(pParams->Magic != HPT_IOCTL_MAGIC32))
		return EFAULT;
	
	req32 = BUS_SPACE_RD4_ITL(inbound_queue);
	if (req32 == IOPMU_QUEUE_EMPTY)
		return EFAULT;

	if (pParams->nInBufferSize)
		if (hptiop_bus_space_copyin(hba, req32 +
			offsetof(struct hpt_iop_request_ioctl_command, buf),
			(void *)pParams->lpInBuffer, pParams->nInBufferSize))
			goto invalid;

	if (hptiop_post_ioctl_command_itl(hba, req32, pParams))
		goto invalid;

	result = bus_space_read_4(hba->bar0t, hba->bar0h, req32 +
			offsetof(struct hpt_iop_request_ioctl_command,
				header.result));

	if (result == IOP_RESULT_SUCCESS) {
		if (pParams->nOutBufferSize)
			if (hptiop_bus_space_copyout(hba, req32 +
				offsetof(struct hpt_iop_request_ioctl_command, buf) + 
					((pParams->nInBufferSize + 3) & ~3),
				(void *)pParams->lpOutBuffer, pParams->nOutBufferSize))
				goto invalid;

		if (pParams->lpBytesReturned) {
			if (hptiop_bus_space_copyout(hba, req32 + 
				offsetof(struct hpt_iop_request_ioctl_command, bytes_returned),
				(void *)pParams->lpBytesReturned, sizeof(unsigned  long)))
				goto invalid;
		}

		BUS_SPACE_WRT4_ITL(outbound_queue, req32);

		return 0;
	} else{
invalid:
		BUS_SPACE_WRT4_ITL(outbound_queue, req32);

		return EFAULT;
	}
}

static int hptiop_post_ioctl_command_mv(struct hpt_iop_hba *hba,
				struct hpt_iop_request_ioctl_command *req,
				struct hpt_iop_ioctl_param *pParams)
{
	u_int64_t req_phy;
	int size = 0;

	if ((((pParams->nInBufferSize + 3) & ~3) + pParams->nOutBufferSize) >
			(hba->max_request_size -
			offsetof(struct hpt_iop_request_ioctl_command, buf))) {
		device_printf(hba->pcidev, "request size beyond max value");
		return -1;
	}

	req->ioctl_code = HPT_CTL_CODE_BSD_TO_IOP(pParams->dwIoControlCode);
	req->inbuf_size = pParams->nInBufferSize;
	req->outbuf_size = pParams->nOutBufferSize;
	req->header.size = offsetof(struct hpt_iop_request_ioctl_command, buf)
					+ pParams->nInBufferSize;
	req->header.context = (u_int64_t)MVIOP_CMD_TYPE_IOCTL;
	req->header.type = IOP_REQUEST_TYPE_IOCTL_COMMAND;
	req->header.result = IOP_RESULT_PENDING;
	req->header.flags = IOP_REQUEST_FLAG_OUTPUT_CONTEXT;
	size = req->header.size >> 8;
	size = imin(3, size);
	req_phy = hba->ctlcfgcmd_phy | MVIOP_MU_QUEUE_ADDR_HOST_BIT | size;
	hptiop_mv_inbound_write(req_phy, hba);

	BUS_SPACE_RD4_MV0(outbound_intmask);

	while (hba->config_done == 0) {
		if (hptiop_sleep(hba, req, PPAUSE,
			"hptctl", HPT_OSM_TIMEOUT)==0)
			continue;
		hptiop_send_sync_msg(hba, IOPMU_INBOUND_MSG0_RESET, 60000);
	}
	return 0;
}

static int hptiop_do_ioctl_mv(struct hpt_iop_hba *hba,
				struct hpt_iop_ioctl_param *pParams)
{
	struct hpt_iop_request_ioctl_command *req;

	if ((pParams->Magic != HPT_IOCTL_MAGIC) &&
		(pParams->Magic != HPT_IOCTL_MAGIC32))
		return EFAULT;

	req = (struct hpt_iop_request_ioctl_command *)(hba->ctlcfg_ptr);
	hba->config_done = 0;
	hptiop_lock_adapter(hba);
	if (pParams->nInBufferSize)
		if (copyin((void *)pParams->lpInBuffer,
				req->buf, pParams->nInBufferSize))
			goto invalid;
	if (hptiop_post_ioctl_command_mv(hba, req, pParams))
		goto invalid;

	if (hba->config_done == 1) {
		if (pParams->nOutBufferSize)
			if (copyout(req->buf +
				((pParams->nInBufferSize + 3) & ~3),
				(void *)pParams->lpOutBuffer,
				pParams->nOutBufferSize))
				goto invalid;

		if (pParams->lpBytesReturned)
			if (copyout(&req->bytes_returned,
				(void*)pParams->lpBytesReturned,
				sizeof(u_int32_t)))
				goto invalid;
		hptiop_unlock_adapter(hba);
		return 0;
	} else{
invalid:
		hptiop_unlock_adapter(hba);
		return EFAULT;
	}
}

static int hptiop_post_ioctl_command_mvfrey(struct hpt_iop_hba *hba,
				struct hpt_iop_request_ioctl_command *req,
				struct hpt_iop_ioctl_param *pParams)
{
	u_int64_t phy_addr;
	u_int32_t index;

	phy_addr = hba->ctlcfgcmd_phy;

	if ((((pParams->nInBufferSize + 3) & ~3) + pParams->nOutBufferSize) >
			(hba->max_request_size -
			offsetof(struct hpt_iop_request_ioctl_command, buf))) {
		device_printf(hba->pcidev, "request size beyond max value");
		return -1;
	}

	req->ioctl_code = HPT_CTL_CODE_BSD_TO_IOP(pParams->dwIoControlCode);
	req->inbuf_size = pParams->nInBufferSize;
	req->outbuf_size = pParams->nOutBufferSize;
	req->header.size = offsetof(struct hpt_iop_request_ioctl_command, buf)
					+ pParams->nInBufferSize;

	req->header.type = IOP_REQUEST_TYPE_IOCTL_COMMAND;
	req->header.result = IOP_RESULT_PENDING;

	req->header.flags = IOP_REQUEST_FLAG_SYNC_REQUEST
						| IOP_REQUEST_FLAG_OUTPUT_CONTEXT
						| IOP_REQUEST_FLAG_ADDR_BITS
						| ((phy_addr >> 16) & 0xffff0000);
	req->header.context = ((phy_addr & 0xffffffff) << 32 )
						| IOPMU_QUEUE_ADDR_HOST_BIT | req->header.type;

	hba->u.mvfrey.inlist_wptr++;
	index = hba->u.mvfrey.inlist_wptr & 0x3fff;

	if (index == hba->u.mvfrey.list_count) {
		index = 0;
		hba->u.mvfrey.inlist_wptr &= ~0x3fff;
		hba->u.mvfrey.inlist_wptr ^= CL_POINTER_TOGGLE;
	}

	hba->u.mvfrey.inlist[index].addr = phy_addr;
	hba->u.mvfrey.inlist[index].intrfc_len = (req->header.size + 3) / 4;

	BUS_SPACE_WRT4_MVFREY2(inbound_write_ptr, hba->u.mvfrey.inlist_wptr);
	BUS_SPACE_RD4_MVFREY2(inbound_write_ptr);

	while (hba->config_done == 0) {
		if (hptiop_sleep(hba, req, PPAUSE,
			"hptctl", HPT_OSM_TIMEOUT)==0)
			continue;
		hptiop_send_sync_msg(hba, IOPMU_INBOUND_MSG0_RESET, 60000);
	}
	return 0;
}

static int hptiop_do_ioctl_mvfrey(struct hpt_iop_hba *hba,
				struct hpt_iop_ioctl_param *pParams)
{
	struct hpt_iop_request_ioctl_command *req;

	if ((pParams->Magic != HPT_IOCTL_MAGIC) &&
		(pParams->Magic != HPT_IOCTL_MAGIC32))
		return EFAULT;

	req = (struct hpt_iop_request_ioctl_command *)(hba->ctlcfg_ptr);
	hba->config_done = 0;
	hptiop_lock_adapter(hba);
	if (pParams->nInBufferSize)
		if (copyin((void *)pParams->lpInBuffer,
				req->buf, pParams->nInBufferSize))
			goto invalid;
	if (hptiop_post_ioctl_command_mvfrey(hba, req, pParams))
		goto invalid;

	if (hba->config_done == 1) {
		if (pParams->nOutBufferSize)
			if (copyout(req->buf +
				((pParams->nInBufferSize + 3) & ~3),
				(void *)pParams->lpOutBuffer,
				pParams->nOutBufferSize))
				goto invalid;

		if (pParams->lpBytesReturned)
			if (copyout(&req->bytes_returned,
				(void*)pParams->lpBytesReturned,
				sizeof(u_int32_t)))
				goto invalid;
		hptiop_unlock_adapter(hba);
		return 0;
	} else{
invalid:
		hptiop_unlock_adapter(hba);
		return EFAULT;
	}
}

static int  hptiop_rescan_bus(struct hpt_iop_hba * hba)
{
	union ccb           *ccb;

	if ((ccb = xpt_alloc_ccb()) == NULL)
		return(ENOMEM);
	if (xpt_create_path(&ccb->ccb_h.path, NULL, cam_sim_path(hba->sim),
		CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		xpt_free_ccb(ccb);
		return(EIO);
	}
	xpt_rescan(ccb);
	return(0);
}

static  bus_dmamap_callback_t   hptiop_map_srb;
static  bus_dmamap_callback_t   hptiop_post_scsi_command;
static  bus_dmamap_callback_t   hptiop_mv_map_ctlcfg;
static	bus_dmamap_callback_t	hptiop_mvfrey_map_ctlcfg;

static int hptiop_alloc_pci_res_itl(struct hpt_iop_hba *hba)
{
	hba->bar0_rid = 0x10;
	hba->bar0_res = bus_alloc_resource_any(hba->pcidev,
			SYS_RES_MEMORY, &hba->bar0_rid, RF_ACTIVE);

	if (hba->bar0_res == NULL) {
		device_printf(hba->pcidev,
			"failed to get iop base adrress.\n");
		return -1;
	}
	hba->bar0t = rman_get_bustag(hba->bar0_res);
	hba->bar0h = rman_get_bushandle(hba->bar0_res);
	hba->u.itl.mu = (struct hpt_iopmu_itl *)
				rman_get_virtual(hba->bar0_res);

	if (!hba->u.itl.mu) {
		bus_release_resource(hba->pcidev, SYS_RES_MEMORY,
					hba->bar0_rid, hba->bar0_res);
		device_printf(hba->pcidev, "alloc mem res failed\n");
		return -1;
	}

	return 0;
}

static int hptiop_alloc_pci_res_mv(struct hpt_iop_hba *hba)
{
	hba->bar0_rid = 0x10;
	hba->bar0_res = bus_alloc_resource_any(hba->pcidev,
			SYS_RES_MEMORY, &hba->bar0_rid, RF_ACTIVE);

	if (hba->bar0_res == NULL) {
		device_printf(hba->pcidev, "failed to get iop bar0.\n");
		return -1;
	}
	hba->bar0t = rman_get_bustag(hba->bar0_res);
	hba->bar0h = rman_get_bushandle(hba->bar0_res);
	hba->u.mv.regs = (struct hpt_iopmv_regs *)
				rman_get_virtual(hba->bar0_res);

	if (!hba->u.mv.regs) {
		bus_release_resource(hba->pcidev, SYS_RES_MEMORY,
					hba->bar0_rid, hba->bar0_res);
		device_printf(hba->pcidev, "alloc bar0 mem res failed\n");
		return -1;
	}

	hba->bar2_rid = 0x18;
	hba->bar2_res = bus_alloc_resource_any(hba->pcidev,
			SYS_RES_MEMORY, &hba->bar2_rid, RF_ACTIVE);

	if (hba->bar2_res == NULL) {
		bus_release_resource(hba->pcidev, SYS_RES_MEMORY,
					hba->bar0_rid, hba->bar0_res);
		device_printf(hba->pcidev, "failed to get iop bar2.\n");
		return -1;
	}

	hba->bar2t = rman_get_bustag(hba->bar2_res);
	hba->bar2h = rman_get_bushandle(hba->bar2_res);
	hba->u.mv.mu = (struct hpt_iopmu_mv *)rman_get_virtual(hba->bar2_res);

	if (!hba->u.mv.mu) {
		bus_release_resource(hba->pcidev, SYS_RES_MEMORY,
					hba->bar0_rid, hba->bar0_res);
		bus_release_resource(hba->pcidev, SYS_RES_MEMORY,
					hba->bar2_rid, hba->bar2_res);
		device_printf(hba->pcidev, "alloc mem bar2 res failed\n");
		return -1;
	}

	return 0;
}

static int hptiop_alloc_pci_res_mvfrey(struct hpt_iop_hba *hba)
{
	hba->bar0_rid = 0x10;
	hba->bar0_res = bus_alloc_resource_any(hba->pcidev,
			SYS_RES_MEMORY, &hba->bar0_rid, RF_ACTIVE);

	if (hba->bar0_res == NULL) {
		device_printf(hba->pcidev, "failed to get iop bar0.\n");
		return -1;
	}
	hba->bar0t = rman_get_bustag(hba->bar0_res);
	hba->bar0h = rman_get_bushandle(hba->bar0_res);
	hba->u.mvfrey.config = (struct hpt_iop_request_get_config *)
				rman_get_virtual(hba->bar0_res);

	if (!hba->u.mvfrey.config) {
		bus_release_resource(hba->pcidev, SYS_RES_MEMORY,
					hba->bar0_rid, hba->bar0_res);
		device_printf(hba->pcidev, "alloc bar0 mem res failed\n");
		return -1;
	}

	hba->bar2_rid = 0x18;
	hba->bar2_res = bus_alloc_resource_any(hba->pcidev,
			SYS_RES_MEMORY, &hba->bar2_rid, RF_ACTIVE);

	if (hba->bar2_res == NULL) {
		bus_release_resource(hba->pcidev, SYS_RES_MEMORY,
					hba->bar0_rid, hba->bar0_res);
		device_printf(hba->pcidev, "failed to get iop bar2.\n");
		return -1;
	}

	hba->bar2t = rman_get_bustag(hba->bar2_res);
	hba->bar2h = rman_get_bushandle(hba->bar2_res);
	hba->u.mvfrey.mu =
					(struct hpt_iopmu_mvfrey *)rman_get_virtual(hba->bar2_res);

	if (!hba->u.mvfrey.mu) {
		bus_release_resource(hba->pcidev, SYS_RES_MEMORY,
					hba->bar0_rid, hba->bar0_res);
		bus_release_resource(hba->pcidev, SYS_RES_MEMORY,
					hba->bar2_rid, hba->bar2_res);
		device_printf(hba->pcidev, "alloc mem bar2 res failed\n");
		return -1;
	}

	return 0;
}

static void hptiop_release_pci_res_itl(struct hpt_iop_hba *hba)
{
	if (hba->bar0_res)
		bus_release_resource(hba->pcidev, SYS_RES_MEMORY,
			hba->bar0_rid, hba->bar0_res);
}

static void hptiop_release_pci_res_mv(struct hpt_iop_hba *hba)
{
	if (hba->bar0_res)
		bus_release_resource(hba->pcidev, SYS_RES_MEMORY,
			hba->bar0_rid, hba->bar0_res);
	if (hba->bar2_res)
		bus_release_resource(hba->pcidev, SYS_RES_MEMORY,
			hba->bar2_rid, hba->bar2_res);
}

static void hptiop_release_pci_res_mvfrey(struct hpt_iop_hba *hba)
{
	if (hba->bar0_res)
		bus_release_resource(hba->pcidev, SYS_RES_MEMORY,
			hba->bar0_rid, hba->bar0_res);
	if (hba->bar2_res)
		bus_release_resource(hba->pcidev, SYS_RES_MEMORY,
			hba->bar2_rid, hba->bar2_res);
}

static int hptiop_internal_memalloc_mv(struct hpt_iop_hba *hba)
{
	if (bus_dma_tag_create(hba->parent_dmat,
				1,
				0,
				BUS_SPACE_MAXADDR_32BIT,
				BUS_SPACE_MAXADDR,
				NULL, NULL,
				0x800 - 0x8,
				1,
				BUS_SPACE_MAXSIZE_32BIT,
				BUS_DMA_ALLOCNOW,
				NULL,
				NULL,
				&hba->ctlcfg_dmat)) {
		device_printf(hba->pcidev, "alloc ctlcfg_dmat failed\n");
		return -1;
	}

	if (bus_dmamem_alloc(hba->ctlcfg_dmat, (void **)&hba->ctlcfg_ptr,
		BUS_DMA_WAITOK | BUS_DMA_COHERENT,
		&hba->ctlcfg_dmamap) != 0) {
			device_printf(hba->pcidev,
					"bus_dmamem_alloc failed!\n");
			bus_dma_tag_destroy(hba->ctlcfg_dmat);
			return -1;
	}

	if (bus_dmamap_load(hba->ctlcfg_dmat,
			hba->ctlcfg_dmamap, hba->ctlcfg_ptr,
			MVIOP_IOCTLCFG_SIZE,
			hptiop_mv_map_ctlcfg, hba, 0)) {
		device_printf(hba->pcidev, "bus_dmamap_load failed!\n");
		if (hba->ctlcfg_dmat) {
			bus_dmamem_free(hba->ctlcfg_dmat,
				hba->ctlcfg_ptr, hba->ctlcfg_dmamap);
			bus_dma_tag_destroy(hba->ctlcfg_dmat);
		}
		return -1;
	}

	return 0;
}

static int hptiop_internal_memalloc_mvfrey(struct hpt_iop_hba *hba)
{
	u_int32_t list_count = BUS_SPACE_RD4_MVFREY2(inbound_conf_ctl);

	list_count >>= 16;

	if (list_count == 0) {
		return -1;
	}

	hba->u.mvfrey.list_count = list_count;
	hba->u.mvfrey.internal_mem_size = 0x800
							+ list_count * sizeof(struct mvfrey_inlist_entry)
							+ list_count * sizeof(struct mvfrey_outlist_entry)
							+ sizeof(int);
	if (bus_dma_tag_create(hba->parent_dmat,
				1,
				0,
				BUS_SPACE_MAXADDR_32BIT,
				BUS_SPACE_MAXADDR,
				NULL, NULL,
				hba->u.mvfrey.internal_mem_size,
				1,
				BUS_SPACE_MAXSIZE_32BIT,
				BUS_DMA_ALLOCNOW,
				NULL,
				NULL,
				&hba->ctlcfg_dmat)) {
		device_printf(hba->pcidev, "alloc ctlcfg_dmat failed\n");
		return -1;
	}

	if (bus_dmamem_alloc(hba->ctlcfg_dmat, (void **)&hba->ctlcfg_ptr,
		BUS_DMA_WAITOK | BUS_DMA_COHERENT,
		&hba->ctlcfg_dmamap) != 0) {
			device_printf(hba->pcidev,
					"bus_dmamem_alloc failed!\n");
			bus_dma_tag_destroy(hba->ctlcfg_dmat);
			return -1;
	}

	if (bus_dmamap_load(hba->ctlcfg_dmat,
			hba->ctlcfg_dmamap, hba->ctlcfg_ptr,
			hba->u.mvfrey.internal_mem_size,
			hptiop_mvfrey_map_ctlcfg, hba, 0)) {
		device_printf(hba->pcidev, "bus_dmamap_load failed!\n");
		if (hba->ctlcfg_dmat) {
			bus_dmamem_free(hba->ctlcfg_dmat,
				hba->ctlcfg_ptr, hba->ctlcfg_dmamap);
			bus_dma_tag_destroy(hba->ctlcfg_dmat);
		}
		return -1;
	}

	return 0;
}

static int hptiop_internal_memfree_itl(struct hpt_iop_hba *hba) {
	return 0;
}

static int hptiop_internal_memfree_mv(struct hpt_iop_hba *hba)
{
	if (hba->ctlcfg_dmat) {
		bus_dmamap_unload(hba->ctlcfg_dmat, hba->ctlcfg_dmamap);
		bus_dmamem_free(hba->ctlcfg_dmat,
					hba->ctlcfg_ptr, hba->ctlcfg_dmamap);
		bus_dma_tag_destroy(hba->ctlcfg_dmat);
	}

	return 0;
}

static int hptiop_internal_memfree_mvfrey(struct hpt_iop_hba *hba)
{
	if (hba->ctlcfg_dmat) {
		bus_dmamap_unload(hba->ctlcfg_dmat, hba->ctlcfg_dmamap);
		bus_dmamem_free(hba->ctlcfg_dmat,
					hba->ctlcfg_ptr, hba->ctlcfg_dmamap);
		bus_dma_tag_destroy(hba->ctlcfg_dmat);
	}

	return 0;
}

static int hptiop_reset_comm_mvfrey(struct hpt_iop_hba *hba)
{
	u_int32_t i = 100;

	if (hptiop_send_sync_msg(hba, IOPMU_INBOUND_MSG0_RESET_COMM, 3000))
		return -1;

	/* wait 100ms for MCU ready */
	while(i--) {
		DELAY(1000);
	}

	BUS_SPACE_WRT4_MVFREY2(inbound_base,
							hba->u.mvfrey.inlist_phy & 0xffffffff);
	BUS_SPACE_WRT4_MVFREY2(inbound_base_high,
							(hba->u.mvfrey.inlist_phy >> 16) >> 16);

	BUS_SPACE_WRT4_MVFREY2(outbound_base,
							hba->u.mvfrey.outlist_phy & 0xffffffff);
	BUS_SPACE_WRT4_MVFREY2(outbound_base_high,
							(hba->u.mvfrey.outlist_phy >> 16) >> 16);

	BUS_SPACE_WRT4_MVFREY2(outbound_shadow_base,
							hba->u.mvfrey.outlist_cptr_phy & 0xffffffff);
	BUS_SPACE_WRT4_MVFREY2(outbound_shadow_base_high,
							(hba->u.mvfrey.outlist_cptr_phy >> 16) >> 16);

	hba->u.mvfrey.inlist_wptr = (hba->u.mvfrey.list_count - 1)
								| CL_POINTER_TOGGLE;
	*hba->u.mvfrey.outlist_cptr = (hba->u.mvfrey.list_count - 1)
								| CL_POINTER_TOGGLE;
	hba->u.mvfrey.outlist_rptr = hba->u.mvfrey.list_count - 1;
	
	return 0;
}

/*
 * CAM driver interface
 */
static device_method_t driver_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,     hptiop_probe),
	DEVMETHOD(device_attach,    hptiop_attach),
	DEVMETHOD(device_detach,    hptiop_detach),
	DEVMETHOD(device_shutdown,  hptiop_shutdown),
	{ 0, 0 }
};

static struct hptiop_adapter_ops hptiop_itl_ops = {
	.family	           = INTEL_BASED_IOP,
	.iop_wait_ready    = hptiop_wait_ready_itl,
	.internal_memalloc = 0,
	.internal_memfree  = hptiop_internal_memfree_itl,
	.alloc_pci_res     = hptiop_alloc_pci_res_itl,
	.release_pci_res   = hptiop_release_pci_res_itl,
	.enable_intr       = hptiop_enable_intr_itl,
	.disable_intr      = hptiop_disable_intr_itl,
	.get_config        = hptiop_get_config_itl,
	.set_config        = hptiop_set_config_itl,
	.iop_intr          = hptiop_intr_itl,
	.post_msg          = hptiop_post_msg_itl,
	.post_req          = hptiop_post_req_itl,
	.do_ioctl          = hptiop_do_ioctl_itl,
	.reset_comm        = 0,
};

static struct hptiop_adapter_ops hptiop_mv_ops = {
	.family	           = MV_BASED_IOP,
	.iop_wait_ready    = hptiop_wait_ready_mv,
	.internal_memalloc = hptiop_internal_memalloc_mv,
	.internal_memfree  = hptiop_internal_memfree_mv,
	.alloc_pci_res     = hptiop_alloc_pci_res_mv,
	.release_pci_res   = hptiop_release_pci_res_mv,
	.enable_intr       = hptiop_enable_intr_mv,
	.disable_intr      = hptiop_disable_intr_mv,
	.get_config        = hptiop_get_config_mv,
	.set_config        = hptiop_set_config_mv,
	.iop_intr          = hptiop_intr_mv,
	.post_msg          = hptiop_post_msg_mv,
	.post_req          = hptiop_post_req_mv,
	.do_ioctl          = hptiop_do_ioctl_mv,
	.reset_comm        = 0,
};

static struct hptiop_adapter_ops hptiop_mvfrey_ops = {
	.family	           = MVFREY_BASED_IOP,
	.iop_wait_ready    = hptiop_wait_ready_mvfrey,
	.internal_memalloc = hptiop_internal_memalloc_mvfrey,
	.internal_memfree  = hptiop_internal_memfree_mvfrey,
	.alloc_pci_res     = hptiop_alloc_pci_res_mvfrey,
	.release_pci_res   = hptiop_release_pci_res_mvfrey,
	.enable_intr       = hptiop_enable_intr_mvfrey,
	.disable_intr      = hptiop_disable_intr_mvfrey,
	.get_config        = hptiop_get_config_mvfrey,
	.set_config        = hptiop_set_config_mvfrey,
	.iop_intr          = hptiop_intr_mvfrey,
	.post_msg          = hptiop_post_msg_mvfrey,
	.post_req          = hptiop_post_req_mvfrey,
	.do_ioctl          = hptiop_do_ioctl_mvfrey,
	.reset_comm        = hptiop_reset_comm_mvfrey,
};

static driver_t hptiop_pci_driver = {
	driver_name,
	driver_methods,
	sizeof(struct hpt_iop_hba)
};

DRIVER_MODULE(hptiop, pci, hptiop_pci_driver, hptiop_devclass, 0, 0);
MODULE_DEPEND(hptiop, cam, 1, 1, 1);

static int hptiop_probe(device_t dev)
{
	struct hpt_iop_hba *hba;
	u_int32_t id;
	static char buf[256];
	int sas = 0;
	struct hptiop_adapter_ops *ops;

	if (pci_get_vendor(dev) != 0x1103)
		return (ENXIO);

	id = pci_get_device(dev);

	switch (id) {
		case 0x4520:
		case 0x4521:
		case 0x4522:
			sas = 1;
		case 0x3620:
		case 0x3622:
		case 0x3640:
			ops = &hptiop_mvfrey_ops;
			break;
		case 0x4210:
		case 0x4211:
		case 0x4310:
		case 0x4311:
		case 0x4320:
		case 0x4321:
 		case 0x4322:
			sas = 1;
		case 0x3220:
		case 0x3320:
		case 0x3410:
		case 0x3520:
		case 0x3510:
		case 0x3511:
		case 0x3521:
		case 0x3522:
		case 0x3530:
		case 0x3540:
		case 0x3560:
			ops = &hptiop_itl_ops;
			break;
		case 0x3020:
		case 0x3120:
		case 0x3122:
			ops = &hptiop_mv_ops;
			break;
		default:
			return (ENXIO);
	}

	device_printf(dev, "adapter at PCI %d:%d:%d, IRQ %d\n",
		pci_get_bus(dev), pci_get_slot(dev),
		pci_get_function(dev), pci_get_irq(dev));

	sprintf(buf, "RocketRAID %x %s Controller\n",
				id, sas ? "SAS" : "SATA");
	device_set_desc_copy(dev, buf);

	hba = (struct hpt_iop_hba *)device_get_softc(dev);
	bzero(hba, sizeof(struct hpt_iop_hba));
	hba->ops = ops;

	KdPrint(("hba->ops=%p\n", hba->ops));
	return 0;
}

static int hptiop_attach(device_t dev)
{
	struct hpt_iop_hba *hba = (struct hpt_iop_hba *)device_get_softc(dev);
	struct hpt_iop_request_get_config  iop_config;
	struct hpt_iop_request_set_config  set_config;
	int rid = 0;
	struct cam_devq *devq;
	struct ccb_setasync ccb;
	u_int32_t unit = device_get_unit(dev);

	device_printf(dev, "%d RocketRAID 3xxx/4xxx controller driver %s\n",
			unit, driver_version);

	KdPrint(("hptiop: attach(%d, %d/%d/%d) ops=%p\n", unit,
		pci_get_bus(dev), pci_get_slot(dev),
		pci_get_function(dev), hba->ops));

	pci_enable_busmaster(dev);
	hba->pcidev = dev;
	hba->pciunit = unit;

	if (hba->ops->alloc_pci_res(hba))
		return ENXIO;

	if (hba->ops->iop_wait_ready(hba, 2000)) {
		device_printf(dev, "adapter is not ready\n");
		goto release_pci_res;
	}

	mtx_init(&hba->lock, "hptioplock", NULL, MTX_DEF);

	if (bus_dma_tag_create(bus_get_dma_tag(dev),/* PCI parent */
			1,  /* alignment */
			0, /* boundary */
			BUS_SPACE_MAXADDR,  /* lowaddr */
			BUS_SPACE_MAXADDR,  /* highaddr */
			NULL, NULL,         /* filter, filterarg */
			BUS_SPACE_MAXSIZE_32BIT,    /* maxsize */
			BUS_SPACE_UNRESTRICTED, /* nsegments */
			BUS_SPACE_MAXSIZE_32BIT,    /* maxsegsize */
			0,      /* flags */
			NULL,   /* lockfunc */
			NULL,       /* lockfuncarg */
			&hba->parent_dmat   /* tag */))
	{
		device_printf(dev, "alloc parent_dmat failed\n");
		goto release_pci_res;
	}

	if (hba->ops->family == MV_BASED_IOP) {
		if (hba->ops->internal_memalloc(hba)) {
			device_printf(dev, "alloc srb_dmat failed\n");
			goto destroy_parent_tag;
		}
	}
	
	if (hba->ops->get_config(hba, &iop_config)) {
		device_printf(dev, "get iop config failed.\n");
		goto get_config_failed;
	}

	hba->firmware_version = iop_config.firmware_version;
	hba->interface_version = iop_config.interface_version;
	hba->max_requests = iop_config.max_requests;
	hba->max_devices = iop_config.max_devices;
	hba->max_request_size = iop_config.request_size;
	hba->max_sg_count = iop_config.max_sg_count;

	if (hba->ops->family == MVFREY_BASED_IOP) {
		if (hba->ops->internal_memalloc(hba)) {
			device_printf(dev, "alloc srb_dmat failed\n");
			goto destroy_parent_tag;
		}
		if (hba->ops->reset_comm(hba)) {
			device_printf(dev, "reset comm failed\n");
			goto get_config_failed;
		}
	}

	if (bus_dma_tag_create(hba->parent_dmat,/* parent */
			4,  /* alignment */
			BUS_SPACE_MAXADDR_32BIT+1, /* boundary */
			BUS_SPACE_MAXADDR,  /* lowaddr */
			BUS_SPACE_MAXADDR,  /* highaddr */
			NULL, NULL,         /* filter, filterarg */
			PAGE_SIZE * (hba->max_sg_count-1),  /* maxsize */
			hba->max_sg_count,  /* nsegments */
			0x20000,    /* maxsegsize */
			BUS_DMA_ALLOCNOW,       /* flags */
			busdma_lock_mutex,  /* lockfunc */
			&hba->lock,     /* lockfuncarg */
			&hba->io_dmat   /* tag */))
	{
		device_printf(dev, "alloc io_dmat failed\n");
		goto get_config_failed;
	}

	if (bus_dma_tag_create(hba->parent_dmat,/* parent */
			1,  /* alignment */
			0, /* boundary */
			BUS_SPACE_MAXADDR_32BIT,    /* lowaddr */
			BUS_SPACE_MAXADDR,  /* highaddr */
			NULL, NULL,         /* filter, filterarg */
			HPT_SRB_MAX_SIZE * HPT_SRB_MAX_QUEUE_SIZE + 0x20,
			1,  /* nsegments */
			BUS_SPACE_MAXSIZE_32BIT,    /* maxsegsize */
			0,      /* flags */
			NULL,   /* lockfunc */
			NULL,       /* lockfuncarg */
			&hba->srb_dmat  /* tag */))
	{
		device_printf(dev, "alloc srb_dmat failed\n");
		goto destroy_io_dmat;
	}

	if (bus_dmamem_alloc(hba->srb_dmat, (void **)&hba->uncached_ptr,
			BUS_DMA_WAITOK | BUS_DMA_COHERENT,
			&hba->srb_dmamap) != 0)
	{
		device_printf(dev, "srb bus_dmamem_alloc failed!\n");
		goto destroy_srb_dmat;
	}

	if (bus_dmamap_load(hba->srb_dmat,
			hba->srb_dmamap, hba->uncached_ptr,
			(HPT_SRB_MAX_SIZE * HPT_SRB_MAX_QUEUE_SIZE) + 0x20,
			hptiop_map_srb, hba, 0))
	{
		device_printf(dev, "bus_dmamap_load failed!\n");
		goto srb_dmamem_free;
	}

	if ((devq = cam_simq_alloc(hba->max_requests - 1 )) == NULL) {
		device_printf(dev, "cam_simq_alloc failed\n");
		goto srb_dmamap_unload;
	}

	hba->sim = cam_sim_alloc(hptiop_action, hptiop_poll, driver_name,
			hba, unit, &hba->lock, hba->max_requests - 1, 1, devq);
	if (!hba->sim) {
		device_printf(dev, "cam_sim_alloc failed\n");
		cam_simq_free(devq);
		goto srb_dmamap_unload;
	}
	hptiop_lock_adapter(hba);
	if (xpt_bus_register(hba->sim, dev, 0) != CAM_SUCCESS)
	{
		device_printf(dev, "xpt_bus_register failed\n");
		goto free_cam_sim;
	}

	if (xpt_create_path(&hba->path, /*periph */ NULL,
			cam_sim_path(hba->sim), CAM_TARGET_WILDCARD,
			CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		device_printf(dev, "xpt_create_path failed\n");
		goto deregister_xpt_bus;
	}
	hptiop_unlock_adapter(hba);

	bzero(&set_config, sizeof(set_config));
	set_config.iop_id = unit;
	set_config.vbus_id = cam_sim_path(hba->sim);
	set_config.max_host_request_size = HPT_SRB_MAX_REQ_SIZE;

	if (hba->ops->set_config(hba, &set_config)) {
		device_printf(dev, "set iop config failed.\n");
		goto free_hba_path;
	}

	xpt_setup_ccb(&ccb.ccb_h, hba->path, /*priority*/5);
	ccb.ccb_h.func_code = XPT_SASYNC_CB;
	ccb.event_enable = (AC_FOUND_DEVICE | AC_LOST_DEVICE);
	ccb.callback = hptiop_async;
	ccb.callback_arg = hba->sim;
	xpt_action((union ccb *)&ccb);

	rid = 0;
	if ((hba->irq_res = bus_alloc_resource_any(hba->pcidev, SYS_RES_IRQ,
			&rid, RF_SHAREABLE | RF_ACTIVE)) == NULL) {
		device_printf(dev, "allocate irq failed!\n");
		goto free_hba_path;
	}

	if (bus_setup_intr(hba->pcidev, hba->irq_res, INTR_TYPE_CAM | INTR_MPSAFE,
				NULL, hptiop_pci_intr, hba, &hba->irq_handle))
	{
		device_printf(dev, "allocate intr function failed!\n");
		goto free_irq_resource;
	}

	if (hptiop_send_sync_msg(hba,
			IOPMU_INBOUND_MSG0_START_BACKGROUND_TASK, 5000)) {
		device_printf(dev, "fail to start background task\n");
		goto teartown_irq_resource;
	}

	hba->ops->enable_intr(hba);
	hba->initialized = 1;

	hba->ioctl_dev = make_dev(&hptiop_cdevsw, unit,
				UID_ROOT, GID_WHEEL /*GID_OPERATOR*/,
				S_IRUSR | S_IWUSR, "%s%d", driver_name, unit);


	return 0;


teartown_irq_resource:
	bus_teardown_intr(dev, hba->irq_res, hba->irq_handle);

free_irq_resource:
	bus_release_resource(dev, SYS_RES_IRQ, 0, hba->irq_res);

	hptiop_lock_adapter(hba);
free_hba_path:
	xpt_free_path(hba->path);

deregister_xpt_bus:
	xpt_bus_deregister(cam_sim_path(hba->sim));

free_cam_sim:
	cam_sim_free(hba->sim, /*free devq*/ TRUE);
	hptiop_unlock_adapter(hba);

srb_dmamap_unload:
	if (hba->uncached_ptr)
		bus_dmamap_unload(hba->srb_dmat, hba->srb_dmamap);

srb_dmamem_free:
	if (hba->uncached_ptr)
		bus_dmamem_free(hba->srb_dmat,
			hba->uncached_ptr, hba->srb_dmamap);

destroy_srb_dmat:
	if (hba->srb_dmat)
		bus_dma_tag_destroy(hba->srb_dmat);

destroy_io_dmat:
	if (hba->io_dmat)
		bus_dma_tag_destroy(hba->io_dmat);

get_config_failed:
	hba->ops->internal_memfree(hba);

destroy_parent_tag:
	if (hba->parent_dmat)
		bus_dma_tag_destroy(hba->parent_dmat);

release_pci_res:
	if (hba->ops->release_pci_res)
		hba->ops->release_pci_res(hba);

	return ENXIO;
}

static int hptiop_detach(device_t dev)
{
	struct hpt_iop_hba * hba = (struct hpt_iop_hba *)device_get_softc(dev);
	int i;
	int error = EBUSY;

	hptiop_lock_adapter(hba);
	for (i = 0; i < hba->max_devices; i++)
		if (hptiop_os_query_remove_device(hba, i)) {
			device_printf(dev, "%d file system is busy. id=%d",
						hba->pciunit, i);
			goto out;
		}

	if ((error = hptiop_shutdown(dev)) != 0)
		goto out;
	if (hptiop_send_sync_msg(hba,
		IOPMU_INBOUND_MSG0_STOP_BACKGROUND_TASK, 60000))
		goto out;
	hptiop_unlock_adapter(hba);

	hptiop_release_resource(hba);
	return (0);
out:
	hptiop_unlock_adapter(hba);
	return error;
}

static int hptiop_shutdown(device_t dev)
{
	struct hpt_iop_hba * hba = (struct hpt_iop_hba *)device_get_softc(dev);

	int error = 0;

	if (hba->flag & HPT_IOCTL_FLAG_OPEN) {
		device_printf(dev, "%d device is busy", hba->pciunit);
		return EBUSY;
	}

	hba->ops->disable_intr(hba);

	if (hptiop_send_sync_msg(hba, IOPMU_INBOUND_MSG0_SHUTDOWN, 60000))
		error = EBUSY;

	return error;
}

static void hptiop_pci_intr(void *arg)
{
	struct hpt_iop_hba * hba = (struct hpt_iop_hba *)arg;
	hptiop_lock_adapter(hba);
	hba->ops->iop_intr(hba);
	hptiop_unlock_adapter(hba);
}

static void hptiop_poll(struct cam_sim *sim)
{
	struct hpt_iop_hba *hba;

	hba = cam_sim_softc(sim);
	hba->ops->iop_intr(hba);
}

static void hptiop_async(void * callback_arg, u_int32_t code,
					struct cam_path * path, void * arg)
{
}

static void hptiop_enable_intr_itl(struct hpt_iop_hba *hba)
{
	BUS_SPACE_WRT4_ITL(outbound_intmask,
		~(IOPMU_OUTBOUND_INT_POSTQUEUE | IOPMU_OUTBOUND_INT_MSG0));
}

static void hptiop_enable_intr_mv(struct hpt_iop_hba *hba)
{
	u_int32_t int_mask;

	int_mask = BUS_SPACE_RD4_MV0(outbound_intmask);
			
	int_mask |= MVIOP_MU_OUTBOUND_INT_POSTQUEUE
			| MVIOP_MU_OUTBOUND_INT_MSG;
    	BUS_SPACE_WRT4_MV0(outbound_intmask,int_mask);
}

static void hptiop_enable_intr_mvfrey(struct hpt_iop_hba *hba)
{
	BUS_SPACE_WRT4_MVFREY2(f0_doorbell_enable, CPU_TO_F0_DRBL_MSG_A_BIT);
	BUS_SPACE_RD4_MVFREY2(f0_doorbell_enable);

	BUS_SPACE_WRT4_MVFREY2(isr_enable, 0x1);
	BUS_SPACE_RD4_MVFREY2(isr_enable);

	BUS_SPACE_WRT4_MVFREY2(pcie_f0_int_enable, 0x1010);
	BUS_SPACE_RD4_MVFREY2(pcie_f0_int_enable);
}

static void hptiop_disable_intr_itl(struct hpt_iop_hba *hba)
{
	u_int32_t int_mask;

	int_mask = BUS_SPACE_RD4_ITL(outbound_intmask);

	int_mask |= IOPMU_OUTBOUND_INT_POSTQUEUE | IOPMU_OUTBOUND_INT_MSG0;
	BUS_SPACE_WRT4_ITL(outbound_intmask, int_mask);
	BUS_SPACE_RD4_ITL(outbound_intstatus);
}

static void hptiop_disable_intr_mv(struct hpt_iop_hba *hba)
{
	u_int32_t int_mask;
	int_mask = BUS_SPACE_RD4_MV0(outbound_intmask);
	
	int_mask &= ~(MVIOP_MU_OUTBOUND_INT_MSG
			| MVIOP_MU_OUTBOUND_INT_POSTQUEUE);
	BUS_SPACE_WRT4_MV0(outbound_intmask,int_mask);
	BUS_SPACE_RD4_MV0(outbound_intmask);
}

static void hptiop_disable_intr_mvfrey(struct hpt_iop_hba *hba)
{
	BUS_SPACE_WRT4_MVFREY2(f0_doorbell_enable, 0);
	BUS_SPACE_RD4_MVFREY2(f0_doorbell_enable);

	BUS_SPACE_WRT4_MVFREY2(isr_enable, 0);
	BUS_SPACE_RD4_MVFREY2(isr_enable);

	BUS_SPACE_WRT4_MVFREY2(pcie_f0_int_enable, 0);
	BUS_SPACE_RD4_MVFREY2(pcie_f0_int_enable);
}

static void hptiop_reset_adapter(void *argv)
{
	struct hpt_iop_hba * hba = (struct hpt_iop_hba *)argv;
	if (hptiop_send_sync_msg(hba, IOPMU_INBOUND_MSG0_RESET, 60000))
		return;
	hptiop_send_sync_msg(hba, IOPMU_INBOUND_MSG0_START_BACKGROUND_TASK, 5000);
}

static void *hptiop_get_srb(struct hpt_iop_hba * hba)
{
	struct hpt_iop_srb * srb;

	if (hba->srb_list) {
		srb = hba->srb_list;
		hba->srb_list = srb->next;
		return srb;
	}

	return NULL;
}

static void hptiop_free_srb(struct hpt_iop_hba *hba, struct hpt_iop_srb *srb)
{
	srb->next = hba->srb_list;
	hba->srb_list = srb;
}

static void hptiop_action(struct cam_sim *sim, union ccb *ccb)
{
	struct hpt_iop_hba * hba = (struct hpt_iop_hba *)cam_sim_softc(sim);
	struct hpt_iop_srb * srb;
	int error;

	switch (ccb->ccb_h.func_code) {

	case XPT_SCSI_IO:
		if (ccb->ccb_h.target_lun != 0 ||
			ccb->ccb_h.target_id >= hba->max_devices ||
			(ccb->ccb_h.flags & CAM_CDB_PHYS))
		{
			ccb->ccb_h.status = CAM_TID_INVALID;
			xpt_done(ccb);
			return;
		}

		if ((srb = hptiop_get_srb(hba)) == NULL) {
			device_printf(hba->pcidev, "srb allocated failed");
			ccb->ccb_h.status = CAM_REQ_CMP_ERR;
			xpt_done(ccb);
			return;
		}

		srb->ccb = ccb;
		error = bus_dmamap_load_ccb(hba->io_dmat,
					    srb->dma_map,
					    ccb,
					    hptiop_post_scsi_command,
					    srb,
					    0);

		if (error && error != EINPROGRESS) {
			device_printf(hba->pcidev,
				"%d bus_dmamap_load error %d",
				hba->pciunit, error);
			xpt_freeze_simq(hba->sim, 1);
			ccb->ccb_h.status = CAM_REQ_CMP_ERR;
			hptiop_free_srb(hba, srb);
			xpt_done(ccb);
			return;
		}

		return;

	case XPT_RESET_BUS:
		device_printf(hba->pcidev, "reset adapter");
		hba->msg_done = 0;
		hptiop_reset_adapter(hba);
		break;

	case XPT_GET_TRAN_SETTINGS:
	case XPT_SET_TRAN_SETTINGS:
		ccb->ccb_h.status = CAM_FUNC_NOTAVAIL;
		break;

	case XPT_CALC_GEOMETRY:
		cam_calc_geometry(&ccb->ccg, 1);
		break;

	case XPT_PATH_INQ:
	{
		struct ccb_pathinq *cpi = &ccb->cpi;

		cpi->version_num = 1;
		cpi->hba_inquiry = PI_SDTR_ABLE;
		cpi->target_sprt = 0;
		cpi->hba_misc = PIM_NOBUSRESET;
		cpi->hba_eng_cnt = 0;
		cpi->max_target = hba->max_devices;
		cpi->max_lun = 0;
		cpi->unit_number = cam_sim_unit(sim);
		cpi->bus_id = cam_sim_bus(sim);
		cpi->initiator_id = hba->max_devices;
		cpi->base_transfer_speed = 3300;

		strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strlcpy(cpi->hba_vid, "HPT   ", HBA_IDLEN);
		strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->transport = XPORT_SPI;
		cpi->transport_version = 2;
		cpi->protocol = PROTO_SCSI;
		cpi->protocol_version = SCSI_REV_2;
		cpi->ccb_h.status = CAM_REQ_CMP;
		break;
	}

	default:
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	}

	xpt_done(ccb);
	return;
}

static void hptiop_post_req_itl(struct hpt_iop_hba *hba,
				struct hpt_iop_srb *srb,
				bus_dma_segment_t *segs, int nsegs)
{
	int idx;
	union ccb *ccb = srb->ccb;
	u_int8_t *cdb;

	if (ccb->ccb_h.flags & CAM_CDB_POINTER)
		cdb = ccb->csio.cdb_io.cdb_ptr;
	else
		cdb = ccb->csio.cdb_io.cdb_bytes;

	KdPrint(("ccb=%p %x-%x-%x\n",
		ccb, *(u_int32_t *)cdb, *((u_int32_t *)cdb+1), *((u_int32_t *)cdb+2)));

	if (srb->srb_flag & HPT_SRB_FLAG_HIGH_MEM_ACESS) {
		u_int32_t iop_req32;
		struct hpt_iop_request_scsi_command req;

		iop_req32 = BUS_SPACE_RD4_ITL(inbound_queue);

		if (iop_req32 == IOPMU_QUEUE_EMPTY) {
			device_printf(hba->pcidev, "invalid req offset\n");
			ccb->ccb_h.status = CAM_BUSY;
			bus_dmamap_unload(hba->io_dmat, srb->dma_map);
			hptiop_free_srb(hba, srb);
			xpt_done(ccb);
			return;
		}

		if (ccb->csio.dxfer_len && nsegs > 0) {
			struct hpt_iopsg *psg = req.sg_list;
			for (idx = 0; idx < nsegs; idx++, psg++) {
				psg->pci_address = (u_int64_t)segs[idx].ds_addr;
				psg->size = segs[idx].ds_len;
				psg->eot = 0;
			}
			psg[-1].eot = 1;
		}

		bcopy(cdb, req.cdb, ccb->csio.cdb_len);

		req.header.size =
				offsetof(struct hpt_iop_request_scsi_command, sg_list)
				+ nsegs*sizeof(struct hpt_iopsg);
		req.header.type = IOP_REQUEST_TYPE_SCSI_COMMAND;
		req.header.flags = 0;
		req.header.result = IOP_RESULT_PENDING;
		req.header.context = (u_int64_t)(unsigned long)srb;
		req.dataxfer_length = ccb->csio.dxfer_len;
		req.channel =  0;
		req.target =  ccb->ccb_h.target_id;
		req.lun =  ccb->ccb_h.target_lun;

		bus_space_write_region_1(hba->bar0t, hba->bar0h, iop_req32,
			(u_int8_t *)&req, req.header.size);

		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN) {
			bus_dmamap_sync(hba->io_dmat,
				srb->dma_map, BUS_DMASYNC_PREREAD);
		}
		else if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT)
			bus_dmamap_sync(hba->io_dmat,
				srb->dma_map, BUS_DMASYNC_PREWRITE);

		BUS_SPACE_WRT4_ITL(inbound_queue,iop_req32);
	} else {
		struct hpt_iop_request_scsi_command *req;

		req = (struct hpt_iop_request_scsi_command *)srb;
		if (ccb->csio.dxfer_len && nsegs > 0) {
			struct hpt_iopsg *psg = req->sg_list;
			for (idx = 0; idx < nsegs; idx++, psg++) {
				psg->pci_address = 
					(u_int64_t)segs[idx].ds_addr;
				psg->size = segs[idx].ds_len;
				psg->eot = 0;
			}
			psg[-1].eot = 1;
		}

		bcopy(cdb, req->cdb, ccb->csio.cdb_len);

		req->header.type = IOP_REQUEST_TYPE_SCSI_COMMAND;
		req->header.result = IOP_RESULT_PENDING;
		req->dataxfer_length = ccb->csio.dxfer_len;
		req->channel =  0;
		req->target =  ccb->ccb_h.target_id;
		req->lun =  ccb->ccb_h.target_lun;
		req->header.size =
			offsetof(struct hpt_iop_request_scsi_command, sg_list)
			+ nsegs*sizeof(struct hpt_iopsg);
		req->header.context = (u_int64_t)srb->index |
						IOPMU_QUEUE_ADDR_HOST_BIT;
		req->header.flags = IOP_REQUEST_FLAG_OUTPUT_CONTEXT;

		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN) {
			bus_dmamap_sync(hba->io_dmat,
				srb->dma_map, BUS_DMASYNC_PREREAD);
		}else if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT) {
			bus_dmamap_sync(hba->io_dmat,
				srb->dma_map, BUS_DMASYNC_PREWRITE);
		}

		if (hba->firmware_version > 0x01020000
			|| hba->interface_version > 0x01020000) {
			u_int32_t size_bits;

			if (req->header.size < 256)
				size_bits = IOPMU_QUEUE_REQUEST_SIZE_BIT;
			else if (req->header.size < 512)
				size_bits = IOPMU_QUEUE_ADDR_HOST_BIT;
			else
				size_bits = IOPMU_QUEUE_REQUEST_SIZE_BIT
						| IOPMU_QUEUE_ADDR_HOST_BIT;

			BUS_SPACE_WRT4_ITL(inbound_queue,
				(u_int32_t)srb->phy_addr | size_bits);
		} else
			BUS_SPACE_WRT4_ITL(inbound_queue, (u_int32_t)srb->phy_addr
				|IOPMU_QUEUE_ADDR_HOST_BIT);
	}
}

static void hptiop_post_req_mv(struct hpt_iop_hba *hba,
				struct hpt_iop_srb *srb,
				bus_dma_segment_t *segs, int nsegs)
{
	int idx, size;
	union ccb *ccb = srb->ccb;
	u_int8_t *cdb;
	struct hpt_iop_request_scsi_command *req;
	u_int64_t req_phy;

    	req = (struct hpt_iop_request_scsi_command *)srb;
	req_phy = srb->phy_addr;

	if (ccb->csio.dxfer_len && nsegs > 0) {
		struct hpt_iopsg *psg = req->sg_list;
		for (idx = 0; idx < nsegs; idx++, psg++) {
			psg->pci_address = (u_int64_t)segs[idx].ds_addr;
			psg->size = segs[idx].ds_len;
			psg->eot = 0;
		}
		psg[-1].eot = 1;
	}
	if (ccb->ccb_h.flags & CAM_CDB_POINTER)
		cdb = ccb->csio.cdb_io.cdb_ptr;
	else
		cdb = ccb->csio.cdb_io.cdb_bytes;

	bcopy(cdb, req->cdb, ccb->csio.cdb_len);
	req->header.type = IOP_REQUEST_TYPE_SCSI_COMMAND;
	req->header.result = IOP_RESULT_PENDING;
	req->dataxfer_length = ccb->csio.dxfer_len;
	req->channel = 0;
	req->target =  ccb->ccb_h.target_id;
	req->lun =  ccb->ccb_h.target_lun;
	req->header.size = sizeof(struct hpt_iop_request_scsi_command)
				- sizeof(struct hpt_iopsg)
				+ nsegs * sizeof(struct hpt_iopsg);
	if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN) {
		bus_dmamap_sync(hba->io_dmat,
			srb->dma_map, BUS_DMASYNC_PREREAD);
	}
	else if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT)
		bus_dmamap_sync(hba->io_dmat,
			srb->dma_map, BUS_DMASYNC_PREWRITE);
	req->header.context = (u_int64_t)srb->index
					<< MVIOP_REQUEST_NUMBER_START_BIT
					| MVIOP_CMD_TYPE_SCSI;
	req->header.flags = IOP_REQUEST_FLAG_OUTPUT_CONTEXT;
	size = req->header.size >> 8;
	hptiop_mv_inbound_write(req_phy
			| MVIOP_MU_QUEUE_ADDR_HOST_BIT
			| imin(3, size), hba);
}

static void hptiop_post_req_mvfrey(struct hpt_iop_hba *hba,
				struct hpt_iop_srb *srb,
				bus_dma_segment_t *segs, int nsegs)
{
	int idx, index;
	union ccb *ccb = srb->ccb;
	u_int8_t *cdb;
	struct hpt_iop_request_scsi_command *req;
	u_int64_t req_phy;

	req = (struct hpt_iop_request_scsi_command *)srb;
	req_phy = srb->phy_addr;

	if (ccb->csio.dxfer_len && nsegs > 0) {
		struct hpt_iopsg *psg = req->sg_list;
		for (idx = 0; idx < nsegs; idx++, psg++) {
			psg->pci_address = (u_int64_t)segs[idx].ds_addr | 1;
			psg->size = segs[idx].ds_len;
			psg->eot = 0;
		}
		psg[-1].eot = 1;
	}
	if (ccb->ccb_h.flags & CAM_CDB_POINTER)
		cdb = ccb->csio.cdb_io.cdb_ptr;
	else
		cdb = ccb->csio.cdb_io.cdb_bytes;

	bcopy(cdb, req->cdb, ccb->csio.cdb_len);
	req->header.type = IOP_REQUEST_TYPE_SCSI_COMMAND;
	req->header.result = IOP_RESULT_PENDING;
	req->dataxfer_length = ccb->csio.dxfer_len;
	req->channel = 0;
	req->target = ccb->ccb_h.target_id;
	req->lun = ccb->ccb_h.target_lun;
	req->header.size = sizeof(struct hpt_iop_request_scsi_command)
				- sizeof(struct hpt_iopsg)
				+ nsegs * sizeof(struct hpt_iopsg);
	if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN) {
		bus_dmamap_sync(hba->io_dmat,
			srb->dma_map, BUS_DMASYNC_PREREAD);
	}
	else if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT)
		bus_dmamap_sync(hba->io_dmat,
			srb->dma_map, BUS_DMASYNC_PREWRITE);

	req->header.flags = IOP_REQUEST_FLAG_OUTPUT_CONTEXT
						| IOP_REQUEST_FLAG_ADDR_BITS
						| ((req_phy >> 16) & 0xffff0000);
	req->header.context = ((req_phy & 0xffffffff) << 32 )
						| srb->index << 4
						| IOPMU_QUEUE_ADDR_HOST_BIT | req->header.type;

	hba->u.mvfrey.inlist_wptr++;
	index = hba->u.mvfrey.inlist_wptr & 0x3fff;

	if (index == hba->u.mvfrey.list_count) {
		index = 0;
		hba->u.mvfrey.inlist_wptr &= ~0x3fff;
		hba->u.mvfrey.inlist_wptr ^= CL_POINTER_TOGGLE;
	}

	hba->u.mvfrey.inlist[index].addr = req_phy;
	hba->u.mvfrey.inlist[index].intrfc_len = (req->header.size + 3) / 4;

	BUS_SPACE_WRT4_MVFREY2(inbound_write_ptr, hba->u.mvfrey.inlist_wptr);
	BUS_SPACE_RD4_MVFREY2(inbound_write_ptr);

	if (req->header.type == IOP_REQUEST_TYPE_SCSI_COMMAND) {
		callout_reset(&srb->timeout, 20 * hz, hptiop_reset_adapter, hba);
	}
}

static void hptiop_post_scsi_command(void *arg, bus_dma_segment_t *segs,
					int nsegs, int error)
{
	struct hpt_iop_srb *srb = (struct hpt_iop_srb *)arg;
	union ccb *ccb = srb->ccb;
	struct hpt_iop_hba *hba = srb->hba;

	if (error || nsegs > hba->max_sg_count) {
		KdPrint(("hptiop: func_code=%x tid=%x lun=%jx nsegs=%d\n",
			ccb->ccb_h.func_code,
			ccb->ccb_h.target_id,
			(uintmax_t)ccb->ccb_h.target_lun, nsegs));
		ccb->ccb_h.status = CAM_BUSY;
		bus_dmamap_unload(hba->io_dmat, srb->dma_map);
		hptiop_free_srb(hba, srb);
		xpt_done(ccb);
		return;
	}

	hba->ops->post_req(hba, srb, segs, nsegs);
}

static void hptiop_mv_map_ctlcfg(void *arg, bus_dma_segment_t *segs,
				int nsegs, int error)
{
	struct hpt_iop_hba *hba = (struct hpt_iop_hba *)arg;
	hba->ctlcfgcmd_phy = ((u_int64_t)segs->ds_addr + 0x1F) 
				& ~(u_int64_t)0x1F;
	hba->ctlcfg_ptr = (u_int8_t *)(((unsigned long)hba->ctlcfg_ptr + 0x1F)
				& ~0x1F);
}

static void hptiop_mvfrey_map_ctlcfg(void *arg, bus_dma_segment_t *segs,
				int nsegs, int error)
{
	struct hpt_iop_hba *hba = (struct hpt_iop_hba *)arg;
	char *p;
	u_int64_t phy;
	u_int32_t list_count = hba->u.mvfrey.list_count;

	phy = ((u_int64_t)segs->ds_addr + 0x1F) 
				& ~(u_int64_t)0x1F;
	p = (u_int8_t *)(((unsigned long)hba->ctlcfg_ptr + 0x1F)
				& ~0x1F);
	
	hba->ctlcfgcmd_phy = phy;
	hba->ctlcfg_ptr = p;

	p += 0x800;
	phy += 0x800;

	hba->u.mvfrey.inlist = (struct mvfrey_inlist_entry *)p;
	hba->u.mvfrey.inlist_phy = phy;

	p += list_count * sizeof(struct mvfrey_inlist_entry);
	phy += list_count * sizeof(struct mvfrey_inlist_entry);

	hba->u.mvfrey.outlist = (struct mvfrey_outlist_entry *)p;
	hba->u.mvfrey.outlist_phy = phy;

	p += list_count * sizeof(struct mvfrey_outlist_entry);
	phy += list_count * sizeof(struct mvfrey_outlist_entry);

	hba->u.mvfrey.outlist_cptr = (u_int32_t *)p;
	hba->u.mvfrey.outlist_cptr_phy = phy;
}

static void hptiop_map_srb(void *arg, bus_dma_segment_t *segs,
				int nsegs, int error)
{
	struct hpt_iop_hba * hba = (struct hpt_iop_hba *)arg;
	bus_addr_t phy_addr = (segs->ds_addr + 0x1F) & ~(bus_addr_t)0x1F;
	struct hpt_iop_srb *srb, *tmp_srb;
	int i;

	if (error || nsegs == 0) {
		device_printf(hba->pcidev, "hptiop_map_srb error");
		return;
	}

	/* map srb */
	srb = (struct hpt_iop_srb *)
		(((unsigned long)hba->uncached_ptr + 0x1F)
		& ~(unsigned long)0x1F);

	for (i = 0; i < HPT_SRB_MAX_QUEUE_SIZE; i++) {
		tmp_srb = (struct hpt_iop_srb *)
					((char *)srb + i * HPT_SRB_MAX_SIZE);
		if (((unsigned long)tmp_srb & 0x1F) == 0) {
			if (bus_dmamap_create(hba->io_dmat,
						0, &tmp_srb->dma_map)) {
				device_printf(hba->pcidev, "dmamap create failed");
				return;
			}

			bzero(tmp_srb, sizeof(struct hpt_iop_srb));
			tmp_srb->hba = hba;
			tmp_srb->index = i;
			if (hba->ctlcfg_ptr == 0) {/*itl iop*/
				tmp_srb->phy_addr = (u_int64_t)(u_int32_t)
							(phy_addr >> 5);
				if (phy_addr & IOPMU_MAX_MEM_SUPPORT_MASK_32G)
					tmp_srb->srb_flag =
						HPT_SRB_FLAG_HIGH_MEM_ACESS;
			} else {
				tmp_srb->phy_addr = phy_addr;
			}

			callout_init_mtx(&tmp_srb->timeout, &hba->lock, 0);
			hptiop_free_srb(hba, tmp_srb);
			hba->srb[i] = tmp_srb;
			phy_addr += HPT_SRB_MAX_SIZE;
		}
		else {
			device_printf(hba->pcidev, "invalid alignment");
			return;
		}
	}
}

static void hptiop_os_message_callback(struct hpt_iop_hba * hba, u_int32_t msg)
{
	hba->msg_done = 1;
}

static  int hptiop_os_query_remove_device(struct hpt_iop_hba * hba,
						int target_id)
{
	struct cam_periph       *periph = NULL;
	struct cam_path         *path;
	int                     status, retval = 0;

	status = xpt_create_path(&path, NULL, hba->sim->path_id, target_id, 0);

	if (status == CAM_REQ_CMP) {
		if ((periph = cam_periph_find(path, "da")) != NULL) {
			if (periph->refcount >= 1) {
				device_printf(hba->pcidev, "%d ,"
					"target_id=0x%x,"
					"refcount=%d",
				    hba->pciunit, target_id, periph->refcount);
				retval = -1;
			}
		}
		xpt_free_path(path);
	}
	return retval;
}

static void hptiop_release_resource(struct hpt_iop_hba *hba)
{
	int i;

	if (hba->ioctl_dev)
		destroy_dev(hba->ioctl_dev);

	if (hba->path) {
		struct ccb_setasync ccb;

		xpt_setup_ccb(&ccb.ccb_h, hba->path, /*priority*/5);
		ccb.ccb_h.func_code = XPT_SASYNC_CB;
		ccb.event_enable = 0;
		ccb.callback = hptiop_async;
		ccb.callback_arg = hba->sim;
		xpt_action((union ccb *)&ccb);
		xpt_free_path(hba->path);
	}

	if (hba->irq_handle)
		bus_teardown_intr(hba->pcidev, hba->irq_res, hba->irq_handle);

	if (hba->sim) {
		hptiop_lock_adapter(hba);
		xpt_bus_deregister(cam_sim_path(hba->sim));
		cam_sim_free(hba->sim, TRUE);
		hptiop_unlock_adapter(hba);
	}

	if (hba->ctlcfg_dmat) {
		bus_dmamap_unload(hba->ctlcfg_dmat, hba->ctlcfg_dmamap);
		bus_dmamem_free(hba->ctlcfg_dmat,
					hba->ctlcfg_ptr, hba->ctlcfg_dmamap);
		bus_dma_tag_destroy(hba->ctlcfg_dmat);
	}

	for (i = 0; i < HPT_SRB_MAX_QUEUE_SIZE; i++) {
		struct hpt_iop_srb *srb = hba->srb[i];
		if (srb->dma_map)
			bus_dmamap_destroy(hba->io_dmat, srb->dma_map);
		callout_drain(&srb->timeout);
	}

	if (hba->srb_dmat) {
		bus_dmamap_unload(hba->srb_dmat, hba->srb_dmamap);
		bus_dmamap_destroy(hba->srb_dmat, hba->srb_dmamap);
		bus_dma_tag_destroy(hba->srb_dmat);
	}

	if (hba->io_dmat)
		bus_dma_tag_destroy(hba->io_dmat);

	if (hba->parent_dmat)
		bus_dma_tag_destroy(hba->parent_dmat);

	if (hba->irq_res)
		bus_release_resource(hba->pcidev, SYS_RES_IRQ,
					0, hba->irq_res);

	if (hba->bar0_res)
		bus_release_resource(hba->pcidev, SYS_RES_MEMORY,
					hba->bar0_rid, hba->bar0_res);
	if (hba->bar2_res)
		bus_release_resource(hba->pcidev, SYS_RES_MEMORY,
					hba->bar2_rid, hba->bar2_res);
	mtx_destroy(&hba->lock);
}
