/*-
 * Copyright (c) 2017 Broadcom. All rights reserved.
 * The term "Broadcom" refers to Broadcom Limited and/or its subsidiaries.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include "ocs.h"
#include "ocs_utils.h"

#include <sys/conf.h>
#include <sys/sysctl.h>
#include <sys/ioccom.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/linker.h>
#include <sys/firmware.h>

static d_open_t		ocs_open;
static d_close_t	ocs_close;
static d_ioctl_t	ocs_ioctl;

static struct cdevsw ocs_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	ocs_open,
	.d_close =	ocs_close,
	.d_ioctl =	ocs_ioctl,
	.d_name =	"ocs_fc"
};

int
ocs_firmware_write(ocs_t *ocs, const uint8_t *buf, size_t buf_len, uint8_t *change_status);

static int
ocs_open(struct cdev *cdev, int flags, int fmt, struct thread *td)
{
#if 0
	struct ocs_softc *ocs = cdev->si_drv1;

	device_printf(ocs->dev, "%s\n", __func__);
#endif
	return 0;
}

static int
ocs_close(struct cdev *cdev, int flag, int fmt, struct thread *td)
{
#if 0
	struct ocs_softc *ocs = cdev->si_drv1;

	device_printf(ocs->dev, "%s\n", __func__);
#endif
	return 0;
}

static int32_t
__ocs_ioctl_mbox_cb(ocs_hw_t *hw, int32_t status, uint8_t *mqe, void *arg)
{
	struct ocs_softc *ocs = arg;

	/* wait for the ioctl to sleep before calling wakeup */
	mtx_lock(&ocs->dbg_lock);

	mtx_unlock(&ocs->dbg_lock);

	wakeup(arg);

	return 0;
}

static int
ocs_process_sli_config (ocs_t *ocs, ocs_ioctl_elxu_mbox_t *mcmd, ocs_dma_t *dma){

	sli4_cmd_sli_config_t *sli_config = (sli4_cmd_sli_config_t *)mcmd->payload;

	if (sli_config->emb) {
		sli4_req_hdr_t	*req = (sli4_req_hdr_t *)sli_config->payload.embed;

		switch (req->opcode) {
		case SLI4_OPC_COMMON_READ_OBJECT:
			if (mcmd->out_bytes) {
				sli4_req_common_read_object_t *rdobj =
					(sli4_req_common_read_object_t *)sli_config->payload.embed;

				if (ocs_dma_alloc(ocs, dma, mcmd->out_bytes, 4096)) {
					device_printf(ocs->dev, "%s: COMMON_READ_OBJECT - %lld allocation failed\n",
							__func__, (unsigned long long)mcmd->out_bytes);
					return ENXIO;
				}

				memset(dma->virt, 0, mcmd->out_bytes);

				rdobj->host_buffer_descriptor[0].bde_type = SLI4_BDE_TYPE_BDE_64;
				rdobj->host_buffer_descriptor[0].buffer_length = mcmd->out_bytes;
				rdobj->host_buffer_descriptor[0].u.data.buffer_address_low = ocs_addr32_lo(dma->phys);
				rdobj->host_buffer_descriptor[0].u.data.buffer_address_high = ocs_addr32_hi(dma->phys);

			}
			break;
		case SLI4_OPC_COMMON_WRITE_OBJECT:
		{
			sli4_req_common_write_object_t *wrobj =
				(sli4_req_common_write_object_t *)sli_config->payload.embed;

			if (ocs_dma_alloc(ocs, dma, wrobj->desired_write_length, 4096)) {
				device_printf(ocs->dev, "%s: COMMON_WRITE_OBJECT - %d allocation failed\n",
						__func__, wrobj->desired_write_length);
				return ENXIO;
			}
			/* setup the descriptor */
			wrobj->host_buffer_descriptor[0].bde_type = SLI4_BDE_TYPE_BDE_64;
			wrobj->host_buffer_descriptor[0].buffer_length = wrobj->desired_write_length;
			wrobj->host_buffer_descriptor[0].u.data.buffer_address_low = ocs_addr32_lo(dma->phys);
			wrobj->host_buffer_descriptor[0].u.data.buffer_address_high = ocs_addr32_hi(dma->phys);

			/* copy the data into the DMA buffer */
			copyin((void *)(uintptr_t)mcmd->in_addr, dma->virt, mcmd->in_bytes);
		}
			break;
		case SLI4_OPC_COMMON_DELETE_OBJECT:
			break;
		case SLI4_OPC_COMMON_READ_OBJECT_LIST:
			if (mcmd->out_bytes) {
				sli4_req_common_read_object_list_t *rdobj =
					(sli4_req_common_read_object_list_t *)sli_config->payload.embed;

				if (ocs_dma_alloc(ocs, dma, mcmd->out_bytes, 4096)) {
					device_printf(ocs->dev, "%s: COMMON_READ_OBJECT_LIST - %lld allocation failed\n",
							__func__,(unsigned long long) mcmd->out_bytes);
					return ENXIO;
				}

				memset(dma->virt, 0, mcmd->out_bytes);

				rdobj->host_buffer_descriptor[0].bde_type = SLI4_BDE_TYPE_BDE_64;
				rdobj->host_buffer_descriptor[0].buffer_length = mcmd->out_bytes;
				rdobj->host_buffer_descriptor[0].u.data.buffer_address_low = ocs_addr32_lo(dma->phys);
				rdobj->host_buffer_descriptor[0].u.data.buffer_address_high = ocs_addr32_hi(dma->phys);

			}
			break;
		case SLI4_OPC_COMMON_READ_TRANSCEIVER_DATA:
			break;
		default:
			device_printf(ocs->dev, "%s: in=%p (%lld) out=%p (%lld)\n", __func__,
					(void *)(uintptr_t)mcmd->in_addr, (unsigned long long)mcmd->in_bytes,
					(void *)(uintptr_t)mcmd->out_addr, (unsigned long long)mcmd->out_bytes);
			device_printf(ocs->dev, "%s: unknown (opc=%#x)\n", __func__,
					req->opcode);
			hexdump(mcmd, mcmd->size, NULL, 0);
			break;
		}
	} else {
		uint32_t max_bytes = max(mcmd->in_bytes, mcmd->out_bytes);
		if (ocs_dma_alloc(ocs, dma, max_bytes, 4096)) {
			device_printf(ocs->dev, "%s: non-embedded - %u allocation failed\n",
					__func__, max_bytes);
			return ENXIO;
		}

		copyin((void *)(uintptr_t)mcmd->in_addr, dma->virt, mcmd->in_bytes);

		sli_config->payload.mem.address_low  = ocs_addr32_lo(dma->phys);
		sli_config->payload.mem.address_high = ocs_addr32_hi(dma->phys);
		sli_config->payload.mem.length       = max_bytes;
	}

	return 0;
}

static int
ocs_process_mbx_ioctl(ocs_t *ocs, ocs_ioctl_elxu_mbox_t *mcmd)
{
	ocs_dma_t	dma = { 0 };

	if ((ELXU_BSD_MAGIC != mcmd->magic) ||
			(sizeof(ocs_ioctl_elxu_mbox_t) != mcmd->size)) {
		device_printf(ocs->dev, "%s: malformed command m=%08x s=%08x\n",
				__func__, mcmd->magic, mcmd->size);
		return EINVAL;
	}

	switch(((sli4_mbox_command_header_t *)mcmd->payload)->command) {
	case SLI4_MBOX_COMMAND_SLI_CONFIG:
		if (ENXIO == ocs_process_sli_config(ocs, mcmd, &dma))
			return ENXIO;
		break;

	case SLI4_MBOX_COMMAND_READ_REV:
	case SLI4_MBOX_COMMAND_READ_STATUS:
	case SLI4_MBOX_COMMAND_READ_LNK_STAT:
		break;

	default:
		device_printf(ocs->dev, "command %d\n",((sli4_mbox_command_header_t *)mcmd->payload)->command);
		device_printf(ocs->dev, "%s, command not support\n", __func__);
		goto no_support;
		break;

	}

	/*
	 * The dbg_lock usage here insures the command completion code
	 * (__ocs_ioctl_mbox_cb), which calls wakeup(), does not run until
	 * after first calling msleep()
	 *
	 *  1. ioctl grabs dbg_lock
	 *  2. ioctl issues command
	 *       if the command completes before msleep(), the
	 *       command completion code (__ocs_ioctl_mbox_cb) will spin
	 *       on dbg_lock before calling wakeup()
	 *  3. ioctl calls msleep which releases dbg_lock before sleeping
	 *     and reacquires it before waking
	 *  4. command completion handler acquires the dbg_lock, immediately
	 *     releases it, and calls wakeup
	 *  5. msleep returns, re-acquiring the lock
	 *  6. ioctl code releases the lock
	 */
	mtx_lock(&ocs->dbg_lock);
	if (ocs_hw_command(&ocs->hw, mcmd->payload, OCS_CMD_NOWAIT,
			__ocs_ioctl_mbox_cb, ocs)) {

		device_printf(ocs->dev, "%s: command- %x failed\n", __func__,
			((sli4_mbox_command_header_t *)mcmd->payload)->command);
	}
	msleep(ocs, &ocs->dbg_lock, 0, "ocsmbx", 0);
	mtx_unlock(&ocs->dbg_lock);

	if( SLI4_MBOX_COMMAND_SLI_CONFIG == ((sli4_mbox_command_header_t *)mcmd->payload)->command
	  		&& mcmd->out_bytes && dma.virt) {
		copyout(dma.virt, (void *)(uintptr_t)mcmd->out_addr, mcmd->out_bytes);
	}

no_support:
	ocs_dma_free(ocs, &dma);

	return 0;
}

/**
 * @brief perform requested Elx CoreDump helper function
 *
 * The Elx CoreDump facility used for BE3 diagnostics uses the OCS_IOCTL_CMD_ECD_HELPER
 * ioctl function to execute requested "help" functions
 *
 * @param ocs pointer to ocs structure
 * @param req pointer to helper function request
 *
 * @return returns 0 for success, a negative error code value for failure.
 */

static int
ocs_process_ecd_helper (ocs_t *ocs, ocs_ioctl_ecd_helper_t *req)
{
	int32_t rc = 0;
	uint8_t v8;
	uint16_t v16;
	uint32_t v32;


	/* Check the BAR read/write commands for valid bar */
	switch(req->cmd) {
	case OCS_ECD_HELPER_BAR_READ8:
	case OCS_ECD_HELPER_BAR_READ16:
	case OCS_ECD_HELPER_BAR_READ32:
	case OCS_ECD_HELPER_BAR_WRITE8:
	case OCS_ECD_HELPER_BAR_WRITE16:
	case OCS_ECD_HELPER_BAR_WRITE32:
		if (req->bar >= PCI_MAX_BAR) {
			device_printf(ocs->dev, "Error: bar %d out of range\n", req->bar);
			return -EFAULT;
		}
		if (ocs->reg[req->bar].res == NULL) {
			device_printf(ocs->dev, "Error: bar %d not defined\n", req->bar);
			return -EFAULT;
		}
		break;
	default:
		break;
	}

	switch(req->cmd) {
	case OCS_ECD_HELPER_CFG_READ8:
		v8 = ocs_config_read8(ocs, req->offset);
		req->data = v8;
		break;
	case OCS_ECD_HELPER_CFG_READ16:
		v16 = ocs_config_read16(ocs, req->offset);
		req->data = v16;
		break;
	case OCS_ECD_HELPER_CFG_READ32:
		v32 = ocs_config_read32(ocs, req->offset);
		req->data = v32;
		break;
	case OCS_ECD_HELPER_CFG_WRITE8:
		ocs_config_write8(ocs, req->offset, req->data);
		break;
	case OCS_ECD_HELPER_CFG_WRITE16:
		ocs_config_write16(ocs, req->offset, req->data);
		break;
	case OCS_ECD_HELPER_CFG_WRITE32:
		ocs_config_write32(ocs, req->offset, req->data);
		break;
	case OCS_ECD_HELPER_BAR_READ8:
		req->data = ocs_reg_read8(ocs, req->bar, req->offset);
		break;
	case OCS_ECD_HELPER_BAR_READ16:
		req->data = ocs_reg_read16(ocs, req->bar, req->offset);
		break;
	case OCS_ECD_HELPER_BAR_READ32:
		req->data = ocs_reg_read32(ocs, req->bar, req->offset);
		break;
	case OCS_ECD_HELPER_BAR_WRITE8:
		ocs_reg_write8(ocs, req->bar, req->offset, req->data);
		break;
	case OCS_ECD_HELPER_BAR_WRITE16:
		ocs_reg_write16(ocs, req->bar, req->offset, req->data);
		break;
	case OCS_ECD_HELPER_BAR_WRITE32:
		ocs_reg_write32(ocs, req->bar, req->offset, req->data);
		break;
	default:
		device_printf(ocs->dev, "Invalid helper command=%d\n", req->cmd);
		break;
	}

	return rc;
}

static int
ocs_ioctl(struct cdev *cdev, u_long cmd, caddr_t addr, int flag, struct thread *td)
{
	int status = 0;
	struct ocs_softc *ocs = cdev->si_drv1;
	device_t dev = ocs->dev;

	switch (cmd) {
	case OCS_IOCTL_CMD_ELXU_MBOX: {
		/* "copyin" done by kernel; thus, just dereference addr */
		ocs_ioctl_elxu_mbox_t *mcmd = (void *)addr;
		status = ocs_process_mbx_ioctl(ocs, mcmd);
		break;
	}
	case OCS_IOCTL_CMD_ECD_HELPER: {
		/* "copyin" done by kernel; thus, just dereference addr */
		ocs_ioctl_ecd_helper_t *req = (void *)addr;
		status = ocs_process_ecd_helper(ocs, req);
		break;
	}

	case OCS_IOCTL_CMD_VPORT: {
		int32_t rc = 0;
		ocs_ioctl_vport_t *req = (ocs_ioctl_vport_t*) addr;
		ocs_domain_t *domain;

		domain = ocs_domain_get_instance(ocs, req->domain_index);
		if (domain == NULL) {
			device_printf(ocs->dev, "domain [%d] nod found\n",
							req->domain_index);
			return -EFAULT;
		}

		if (req->req_create) {
			rc = ocs_sport_vport_new(domain, req->wwpn, req->wwnn, 
						UINT32_MAX, req->enable_ini,
					req->enable_tgt, NULL, NULL, TRUE);
		} else {
			rc = ocs_sport_vport_del(ocs, domain, req->wwpn, req->wwnn);
		}

		return rc;
	}

	case OCS_IOCTL_CMD_GET_DDUMP: {
		ocs_ioctl_ddump_t *req = (ocs_ioctl_ddump_t*) addr;
		ocs_textbuf_t textbuf;
		int x;

		/* Build a text buffer */
		if (ocs_textbuf_alloc(ocs, &textbuf, req->user_buffer_len)) {
			device_printf(ocs->dev, "Error: ocs_textbuf_alloc failed\n");
			return -EFAULT;
		}

		switch (req->args.action) {
		case OCS_IOCTL_DDUMP_GET:
		case OCS_IOCTL_DDUMP_GET_SAVED: {
			uint32_t remaining;
			uint32_t written;
			uint32_t idx;
			int32_t n;
			ocs_textbuf_t *ptbuf = NULL;
			uint32_t flags = 0;

			if (req->args.action == OCS_IOCTL_DDUMP_GET_SAVED) {
				if (ocs_textbuf_initialized(&ocs->ddump_saved)) {
					ptbuf = &ocs->ddump_saved;
				}
			} else {
				if (ocs_textbuf_alloc(ocs, &textbuf, req->user_buffer_len)) {
					ocs_log_err(ocs, "Error: ocs_textbuf_alloc failed\n");
					return -EFAULT;
				}

				/* translate IOCTL ddump flags to ddump flags */
				if (req->args.flags & OCS_IOCTL_DDUMP_FLAGS_WQES) {
					flags |= OCS_DDUMP_FLAGS_WQES;
				}
				if (req->args.flags & OCS_IOCTL_DDUMP_FLAGS_CQES) {
					flags |= OCS_DDUMP_FLAGS_CQES;
				}
				if (req->args.flags & OCS_IOCTL_DDUMP_FLAGS_MQES) {
					flags |= OCS_DDUMP_FLAGS_MQES;
				}
				if (req->args.flags & OCS_IOCTL_DDUMP_FLAGS_RQES) {
					flags |= OCS_DDUMP_FLAGS_RQES;
				}
				if (req->args.flags & OCS_IOCTL_DDUMP_FLAGS_EQES) {
					flags |= OCS_DDUMP_FLAGS_EQES;
				}

				/* Try 3 times to get the dump */
				for(x=0; x<3; x++) {
					if (ocs_ddump(ocs, &textbuf, flags, req->args.q_entries) != 0) {
						ocs_textbuf_reset(&textbuf);
					} else {
						/* Success */
						x = 0;
						break;
					}
				}
				if (x != 0 ) {
					/* Retries failed */
					ocs_log_test(ocs, "ocs_ddump failed\n");
				} else {
					ptbuf = &textbuf;
				}

			}
			written = 0;
			if (ptbuf != NULL) {
				/* Process each textbuf segment */
				remaining = req->user_buffer_len;
				for (idx = 0; remaining; idx++) {
					n = ocs_textbuf_ext_get_written(ptbuf, idx);
					if (n < 0) {
						break;
					}
					if ((uint32_t)n >= remaining) {
						n = (int32_t)remaining;
					}
					if (ocs_copy_to_user(req->user_buffer + written,
						ocs_textbuf_ext_get_buffer(ptbuf, idx), n)) {
						ocs_log_test(ocs, "Error: (%d) ocs_copy_to_user failed\n", __LINE__);
					}
					written += n;
					remaining -= (uint32_t)n;
				}
			}
			req->bytes_written = written;
			if (ptbuf == &textbuf) {
				ocs_textbuf_free(ocs, &textbuf);
			}

			break;
		}
		case OCS_IOCTL_DDUMP_CLR_SAVED:
			ocs_clear_saved_ddump(ocs);
			break;
		default:
			ocs_log_err(ocs, "Error: ocs_textbuf_alloc failed\n");
			break;
		}
		break;
	}
	case OCS_IOCTL_CMD_DRIVER_INFO: {
		ocs_ioctl_driver_info_t *req = (ocs_ioctl_driver_info_t*)addr;

		ocs_memset(req, 0, sizeof(*req));

		req->pci_vendor = ocs->pci_vendor;
		req->pci_device = ocs->pci_device;
		ocs_strncpy(req->businfo, ocs->businfo, sizeof(req->businfo));

		req->sli_intf = ocs_config_read32(ocs, SLI4_INTF_REG);
		ocs_strncpy(req->desc, device_get_desc(dev), sizeof(req->desc));
		ocs_strncpy(req->fw_rev, ocs->fwrev, sizeof(req->fw_rev));
		if (ocs->domain && ocs->domain->sport) {
			*((uint64_t*)req->hw_addr.fc.wwnn) = ocs_htobe64(ocs->domain->sport->wwnn);
			*((uint64_t*)req->hw_addr.fc.wwpn) = ocs_htobe64(ocs->domain->sport->wwpn);
		}
		ocs_strncpy(req->serialnum, ocs->serialnum, sizeof(req->serialnum));
		break;
	}

	case OCS_IOCTL_CMD_MGMT_LIST: {
		ocs_ioctl_mgmt_buffer_t* req = (ocs_ioctl_mgmt_buffer_t *)addr;
		ocs_textbuf_t textbuf;

		/* Build a text buffer */
		if (ocs_textbuf_alloc(ocs, &textbuf, req->user_buffer_len)) {
			ocs_log_err(ocs, "Error: ocs_textbuf_alloc failed\n");
			return -EFAULT;
		}

		ocs_mgmt_get_list(ocs, &textbuf);

		if (ocs_textbuf_get_written(&textbuf)) {
			if (ocs_copy_to_user(req->user_buffer,
				ocs_textbuf_get_buffer(&textbuf), 
				ocs_textbuf_get_written(&textbuf))) {
				ocs_log_test(ocs, "Error: (%d) ocs_copy_to_user failed\n", __LINE__);
			}
		}
		req->bytes_written = ocs_textbuf_get_written(&textbuf);

		ocs_textbuf_free(ocs, &textbuf);

		break;

	}

	case OCS_IOCTL_CMD_MGMT_GET_ALL: {
		ocs_ioctl_mgmt_buffer_t* req = (ocs_ioctl_mgmt_buffer_t *)addr;
		ocs_textbuf_t textbuf;
		int32_t n;
		uint32_t idx;
		uint32_t copied = 0;

		/* Build a text buffer */
		if (ocs_textbuf_alloc(ocs, &textbuf, req->user_buffer_len)) {
			ocs_log_err(ocs, "Error: ocs_textbuf_alloc failed\n");
			return -EFAULT;
		}

		ocs_mgmt_get_all(ocs, &textbuf);

		for (idx = 0; (n = ocs_textbuf_ext_get_written(&textbuf, idx)) > 0; idx++) {
			if(ocs_copy_to_user(req->user_buffer + copied, 
					ocs_textbuf_ext_get_buffer(&textbuf, idx),
					ocs_textbuf_ext_get_written(&textbuf, idx))) {

					ocs_log_err(ocs, "Error: ocs_textbuf_alloc failed\n");
			}
			copied += n;
		}
		req->bytes_written = copied;

		ocs_textbuf_free(ocs, &textbuf);

		break;
	}

	case OCS_IOCTL_CMD_MGMT_GET: {
		ocs_ioctl_cmd_get_t* req = (ocs_ioctl_cmd_get_t*)addr;
		ocs_textbuf_t textbuf;
		char name[OCS_MGMT_MAX_NAME];

		/* Copy the name value in from user space */
		if (ocs_copy_from_user(name, req->name, OCS_MGMT_MAX_NAME)) {
			ocs_log_test(ocs, "ocs_copy_from_user failed\n");
			ocs_ioctl_free(ocs, req, sizeof(ocs_ioctl_cmd_get_t));
			return -EFAULT;
		}

		/* Build a text buffer */
		if (ocs_textbuf_alloc(ocs, &textbuf, req->value_length)) {
			ocs_log_err(ocs, "Error: ocs_textbuf_alloc failed\n");
			return -EFAULT;
		}

		ocs_mgmt_get(ocs, name, &textbuf);

		if (ocs_textbuf_get_written(&textbuf)) {
			if (ocs_copy_to_user(req->value, 
				ocs_textbuf_get_buffer(&textbuf), 
				ocs_textbuf_get_written(&textbuf))) {
				ocs_log_test(ocs, "Error: (%d) ocs_copy_to_user failed\n", __LINE__);

		}
		}
		req->value_length = ocs_textbuf_get_written(&textbuf);

		ocs_textbuf_free(ocs, &textbuf);

		break;
	}

	case OCS_IOCTL_CMD_MGMT_SET: {
		char name[OCS_MGMT_MAX_NAME];
		char value[OCS_MGMT_MAX_VALUE];
		ocs_ioctl_cmd_set_t* req = (ocs_ioctl_cmd_set_t*)addr;
		
		// Copy the name  in from user space
		if (ocs_copy_from_user(name, req->name, OCS_MGMT_MAX_NAME)) {
			ocs_log_test(ocs, "Error: copy from user failed\n");
			ocs_ioctl_free(ocs, req, sizeof(*req));
			return -EFAULT;
		}

		// Copy the  value in from user space
		if (ocs_copy_from_user(value, req->value, OCS_MGMT_MAX_VALUE)) {
			ocs_log_test(ocs, "Error: copy from user failed\n");
			ocs_ioctl_free(ocs, req, sizeof(*req));
			return -EFAULT;
		}

		req->result = ocs_mgmt_set(ocs, req->name, req->value);

		break;
	}

	case OCS_IOCTL_CMD_MGMT_EXEC: {
		ocs_ioctl_action_t* req = (ocs_ioctl_action_t*) addr;
		char action_name[OCS_MGMT_MAX_NAME];

		if (ocs_copy_from_user(action_name, req->name, sizeof(action_name))) {
			ocs_log_test(ocs, "Error: copy req.name from user failed\n");
			ocs_ioctl_free(ocs, req, sizeof(*req));
			return -EFAULT;
		}

		req->result = ocs_mgmt_exec(ocs, action_name, req->arg_in, req->arg_in_length,
				req->arg_out, req->arg_out_length);

		break;
	}

	default:
		ocs_log_test(ocs, "Error: unknown cmd %#lx\n", cmd);
		status = -ENOTTY;
		break;
	}
	return status;
}

static void
ocs_fw_write_cb(int32_t status, uint32_t actual_write_length, 
					uint32_t change_status, void *arg)
{
        ocs_mgmt_fw_write_result_t *result = arg;

        result->status = status;
        result->actual_xfer = actual_write_length;
        result->change_status = change_status;

        ocs_sem_v(&(result->semaphore));
}

int
ocs_firmware_write(ocs_t *ocs, const uint8_t *buf, size_t buf_len, 
						uint8_t *change_status)
{
        int rc = 0;
        uint32_t bytes_left;
        uint32_t xfer_size;
        uint32_t offset;
        ocs_dma_t dma;
        int last = 0;
        ocs_mgmt_fw_write_result_t result;

        ocs_sem_init(&(result.semaphore), 0, "fw_write");

        bytes_left = buf_len;
        offset = 0;

        if (ocs_dma_alloc(ocs, &dma, FW_WRITE_BUFSIZE, 4096)) {
                ocs_log_err(ocs, "ocs_firmware_write: malloc failed\n");
                return -ENOMEM;
        }

        while (bytes_left > 0) {

                if (bytes_left > FW_WRITE_BUFSIZE) {
                        xfer_size = FW_WRITE_BUFSIZE;
                } else {
                        xfer_size = bytes_left;
                }

                ocs_memcpy(dma.virt, buf + offset, xfer_size);

                if (bytes_left == xfer_size) {
                        last = 1;
                }

                ocs_hw_firmware_write(&ocs->hw, &dma, xfer_size, offset, 
						last, ocs_fw_write_cb, &result);

                if (ocs_sem_p(&(result.semaphore), OCS_SEM_FOREVER) != 0) {
                        rc = -ENXIO;
                        break;
                }

                if (result.actual_xfer == 0 || result.status != 0) {
                        rc = -EFAULT;
                        break;
                }

                if (last) {
                        *change_status = result.change_status;
                }

                bytes_left -= result.actual_xfer;
                offset += result.actual_xfer;
        }

        ocs_dma_free(ocs, &dma);
        return rc;
}

static int
ocs_sys_fwupgrade(SYSCTL_HANDLER_ARGS)
{
	char file_name[256] = {0};
	char fw_change_status;
	uint32_t rc = 1;
        ocs_t *ocs  = (ocs_t *)arg1;
        const struct firmware *fw;
	const struct ocs_hw_grp_hdr *fw_image;

        rc = sysctl_handle_string(oidp, file_name, sizeof(file_name), req);
        if (rc || !req->newptr)
                return rc;

        fw = firmware_get(file_name);
        if (fw == NULL) {
                device_printf(ocs->dev, "Unable to get Firmware. "
                        "Make sure %s is copied to /boot/modules\n", file_name);
                return ENOENT;
        }

	fw_image = (const struct ocs_hw_grp_hdr *)fw->data;

        /* Check if firmware provided is compatible with this particular
         * Adapter of not*/
        if ((ocs_be32toh(fw_image->magic_number) != OCS_HW_OBJECT_G5) &&
                (ocs_be32toh(fw_image->magic_number) != OCS_HW_OBJECT_G6)) {
                device_printf(ocs->dev,
                        "Invalid FW image found Magic: 0x%x Size: %zu \n",
                        ocs_be32toh(fw_image->magic_number), fw->datasize);
                rc = -1;
                goto exit;

        }

        if (!strncmp(ocs->fw_version, fw_image->revision, 
					strnlen(fw_image->revision, 16))) {
                device_printf(ocs->dev, "No update req. "
				"Firmware is already up to date. \n");
                rc = 0;
                goto exit;
        }

	device_printf(ocs->dev, "Upgrading Firmware from %s to %s \n", 
				ocs->fw_version, fw_image->revision);

	rc = ocs_firmware_write(ocs, fw->data, fw->datasize, &fw_change_status);
        if (rc) {
                ocs_log_err(ocs, "Firmware update failed with status = %d\n", rc);
        } else {
                ocs_log_info(ocs, "Firmware updated successfully\n");
                switch (fw_change_status) {
                        case 0x00:
                                device_printf(ocs->dev, 
				"No reset needed, new firmware is active.\n");
                                break;
                        case 0x01:
                                device_printf(ocs->dev,
				"A physical device reset (host reboot) is "
				"needed to activate the new firmware\n");
                                break;
                        case 0x02:
                        case 0x03:
                                device_printf(ocs->dev,
				"firmware is resetting to activate the new "
				"firmware, Host reboot is needed \n");
                                break;
                        default:
                                ocs_log_warn(ocs,
                                        "Unexected value change_status: %d\n",
                                        fw_change_status);
                                break;
                }

        }

exit:
        /* Release Firmware*/
        firmware_put(fw, FIRMWARE_UNLOAD);

        return rc;

}

static int
ocs_sysctl_wwnn(SYSCTL_HANDLER_ARGS)
{
	uint32_t rc = 1;
	ocs_t *ocs = oidp->oid_arg1;
	char old[64];
	char new[64];
	uint64_t *wwnn = NULL;
	ocs_xport_t *xport = ocs->xport;

	if (xport->req_wwnn) {
		wwnn = &xport->req_wwnn;
		memset(old, 0, sizeof(old));
		snprintf(old, sizeof(old), "0x%llx" , (unsigned long long) *wwnn);

	} else {
		wwnn = ocs_hw_get_ptr(&ocs->hw, OCS_HW_WWN_NODE);

		memset(old, 0, sizeof(old));
		snprintf(old, sizeof(old), "0x%llx" , (unsigned long long) ocs_htobe64(*wwnn));
	}

	/*Read wwnn*/
	if (!req->newptr) {

		return (sysctl_handle_string(oidp, old, sizeof(old), req));
	}

	/*Configure port wwn*/
	rc = sysctl_handle_string(oidp, new, sizeof(new), req);
	if (rc)
		return (rc);

	if (strncmp(old, new, strlen(old)) == 0) {
		return 0;
	}

	return (set_req_wwnn(ocs, NULL, new));
}

static int
ocs_sysctl_wwpn(SYSCTL_HANDLER_ARGS)
{
	uint32_t rc = 1;
	ocs_t *ocs = oidp->oid_arg1;
	char old[64];
	char new[64];
	uint64_t *wwpn = NULL;
	ocs_xport_t *xport = ocs->xport;

	if (xport->req_wwpn) {
		wwpn = &xport->req_wwpn;
		memset(old, 0, sizeof(old));
		snprintf(old, sizeof(old), "0x%llx",(unsigned long long) *wwpn);
	} else {
		wwpn = ocs_hw_get_ptr(&ocs->hw, OCS_HW_WWN_PORT);
		memset(old, 0, sizeof(old));
		snprintf(old, sizeof(old), "0x%llx",(unsigned long long) ocs_htobe64(*wwpn));
	}


	/*Read wwpn*/
	if (!req->newptr) {
		return (sysctl_handle_string(oidp, old, sizeof(old), req));
	}

	/*Configure port wwn*/
	rc = sysctl_handle_string(oidp, new, sizeof(new), req);
	if (rc)
		return (rc);

	if (strncmp(old, new, strlen(old)) == 0) {
		return 0;
	}

	return (set_req_wwpn(ocs, NULL, new));
}

static int
ocs_sysctl_current_topology(SYSCTL_HANDLER_ARGS)
{
	ocs_t *ocs = oidp->oid_arg1;
	uint32_t value;

	ocs_hw_get(&ocs->hw, OCS_HW_TOPOLOGY, &value);

	return (sysctl_handle_int(oidp, &value, 0, req));
}

static int
ocs_sysctl_current_speed(SYSCTL_HANDLER_ARGS)
{
	ocs_t *ocs = oidp->oid_arg1;
	uint32_t value;

	ocs_hw_get(&ocs->hw, OCS_HW_LINK_SPEED, &value);

	return (sysctl_handle_int(oidp, &value, 0, req));
}

static int
ocs_sysctl_config_topology(SYSCTL_HANDLER_ARGS)
{
	uint32_t rc = 1;
	ocs_t *ocs = oidp->oid_arg1;
	uint32_t old_value;
	uint32_t new_value;
	char buf[64];

	ocs_hw_get(&ocs->hw, OCS_HW_CONFIG_TOPOLOGY, &old_value);

	/*Read topo*/
	if (!req->newptr) {
		return (sysctl_handle_int(oidp, &old_value, 0, req));
	}

	/*Configure port wwn*/
	rc = sysctl_handle_int(oidp, &new_value, 0, req);
	if (rc)
		return (rc);

	if (new_value == old_value) {
		return 0;
	}

	snprintf(buf, sizeof(buf), "%d",new_value);
	rc = set_configured_topology(ocs, NULL, buf);
	return rc;
}

static int
ocs_sysctl_config_speed(SYSCTL_HANDLER_ARGS)
{
	uint32_t rc = 1;
	ocs_t *ocs = oidp->oid_arg1;
	uint32_t old_value;
	uint32_t new_value;
	char buf[64];

	ocs_hw_get(&ocs->hw, OCS_HW_LINK_CONFIG_SPEED, &old_value);

	/*Read topo*/
	if (!req->newptr) {
		return (sysctl_handle_int(oidp, &old_value, 0, req));
	}

	/*Configure port wwn*/
	rc = sysctl_handle_int(oidp, &new_value, 0, req);
	if (rc)
		return (rc);

	if (new_value == old_value) {
		return 0;
	}

	snprintf(buf, sizeof(buf), "%d",new_value);
	rc = set_configured_speed(ocs, NULL,buf);
	return rc;
}

static int
ocs_sysctl_fcid(SYSCTL_HANDLER_ARGS)
{
	ocs_t *ocs = oidp->oid_arg1;
	char buf[64];

	memset(buf, 0, sizeof(buf));
	if (ocs->domain && ocs->domain->attached) {
		snprintf(buf, sizeof(buf), "0x%06x", 
			ocs->domain->sport->fc_id);
	}

	return (sysctl_handle_string(oidp, buf, sizeof(buf), req));
}


static int
ocs_sysctl_port_state(SYSCTL_HANDLER_ARGS)
{

	char new[256] = {0};
	uint32_t rc = 1;
	ocs_xport_stats_t old;
	ocs_t *ocs  = (ocs_t *)arg1;

	ocs_xport_status(ocs->xport, OCS_XPORT_CONFIG_PORT_STATUS, &old);

	/*Read port state */
	if (!req->newptr) {
		snprintf(new, sizeof(new), "%s",
			(old.value == OCS_XPORT_PORT_OFFLINE) ?
					 "offline" : "online");	
		return (sysctl_handle_string(oidp, new, sizeof(new), req));
        }
	
	/*Configure port state*/
	rc = sysctl_handle_string(oidp, new, sizeof(new), req);
	if (rc)
		return (rc);

	if (ocs_strcasecmp(new, "offline") == 0) {
		if (old.value == OCS_XPORT_PORT_OFFLINE) {
			return (0);
		}
		ocs_log_debug(ocs, "Setting port to %s\n", new);
		rc = ocs_xport_control(ocs->xport, OCS_XPORT_PORT_OFFLINE);
		if (rc != 0) {
			ocs_log_err(ocs, "Setting port to offline failed\n");
		}
	} else if (ocs_strcasecmp(new, "online") == 0) {
		if (old.value == OCS_XPORT_PORT_ONLINE) {
			return (0);
		}
		ocs_log_debug(ocs, "Setting port to %s\n", new);
		rc = ocs_xport_control(ocs->xport, OCS_XPORT_PORT_ONLINE);
		if (rc != 0) {
			ocs_log_err(ocs, "Setting port to online failed\n");
		}
	} else {
		ocs_log_err(ocs, "Unsupported link state %s\n", new);
		rc = 1;
	}

	return (rc);	

}

static int
ocs_sysctl_vport_wwpn(SYSCTL_HANDLER_ARGS)
{
	ocs_fcport *fcp = oidp->oid_arg1;
	char str_wwpn[64];

	memset(str_wwpn, 0, sizeof(str_wwpn));
	snprintf(str_wwpn, sizeof(str_wwpn), "0x%llx", (unsigned long long)fcp->vport->wwpn);
	
	return (sysctl_handle_string(oidp, str_wwpn, sizeof(str_wwpn), req));
}

static int
ocs_sysctl_vport_wwnn(SYSCTL_HANDLER_ARGS)
{
	ocs_fcport *fcp = oidp->oid_arg1;
	char str_wwnn[64];

	memset(str_wwnn, 0, sizeof(str_wwnn));
	snprintf(str_wwnn, sizeof(str_wwnn), "0x%llx", (unsigned long long)fcp->vport->wwnn);

	return (sysctl_handle_string(oidp, str_wwnn, sizeof(str_wwnn), req));
}

/**
 * @brief Initialize sysctl
 *
 * Initialize sysctl so elxsdkutil can query device information.
 *
 * @param ocs pointer to ocs
 * @return void
 */
static void
ocs_sysctl_init(ocs_t *ocs)
{
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(ocs->dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(ocs->dev);
	struct sysctl_oid *vtree; 
	const char *str = NULL;
	char name[16];
	uint32_t rev, if_type, family, i;
	ocs_fcport *fcp = NULL;

	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"devid", CTLFLAG_RD, NULL,
			pci_get_devid(ocs->dev), "Device ID");

	memset(ocs->modeldesc, 0, sizeof(ocs->modeldesc));
	if (0 == pci_get_vpd_ident(ocs->dev, &str)) {
		snprintf(ocs->modeldesc, sizeof(ocs->modeldesc), "%s", str);
	}
	SYSCTL_ADD_STRING(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"modeldesc", CTLFLAG_RD,
			ocs->modeldesc,
			0, "Model Description");

	memset(ocs->serialnum, 0, sizeof(ocs->serialnum));
	if (0 == pci_get_vpd_readonly(ocs->dev, "SN", &str)) {
		snprintf(ocs->serialnum, sizeof(ocs->serialnum), "%s", str);
	}
	SYSCTL_ADD_STRING(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"sn", CTLFLAG_RD,
			ocs->serialnum,
			0, "Serial Number");

	ocs_hw_get(&ocs->hw, OCS_HW_SLI_REV, &rev);
	ocs_hw_get(&ocs->hw, OCS_HW_IF_TYPE, &if_type);
	ocs_hw_get(&ocs->hw, OCS_HW_SLI_FAMILY, &family);

	memset(ocs->fwrev, 0, sizeof(ocs->fwrev));
	snprintf(ocs->fwrev, sizeof(ocs->fwrev), "%s, sli-%d:%d:%x",
			(char *)ocs_hw_get_ptr(&ocs->hw, OCS_HW_FW_REV),
			rev, if_type, family);
	SYSCTL_ADD_STRING(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"fwrev", CTLFLAG_RD,
			ocs->fwrev,
			0, "Firmware Revision");

	memset(ocs->sli_intf, 0, sizeof(ocs->sli_intf));
	snprintf(ocs->sli_intf, sizeof(ocs->sli_intf), "%08x",
		 ocs_config_read32(ocs, SLI4_INTF_REG));
	SYSCTL_ADD_STRING(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			  "sli_intf", CTLFLAG_RD,
			  ocs->sli_intf,
			  0, "SLI Interface");

        SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "fw_upgrade",
                CTLTYPE_STRING | CTLFLAG_RW, (void *)ocs, 0,
                ocs_sys_fwupgrade, "A", "Firmware grp file");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"wwnn", CTLTYPE_STRING | CTLFLAG_RW,
			ocs, 0, ocs_sysctl_wwnn, "A",
			"World Wide Node Name, wwnn should be in the format 0x<XXXXXXXXXXXXXXXX>");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"wwpn", CTLTYPE_STRING | CTLFLAG_RW,
			ocs, 0, ocs_sysctl_wwpn, "A",
			"World Wide Port Name, wwpn should be in the format 0x<XXXXXXXXXXXXXXXX>");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"current_topology", CTLTYPE_UINT | CTLFLAG_RD,
			ocs, 0, ocs_sysctl_current_topology, "IU",
			"Current Topology, 1-NPort; 2-Loop; 3-None");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"current_speed", CTLTYPE_UINT | CTLFLAG_RD,
			ocs, 0, ocs_sysctl_current_speed, "IU",
			"Current Speed");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"configured_topology", CTLTYPE_UINT | CTLFLAG_RW,
			ocs, 0, ocs_sysctl_config_topology, "IU",
			"Configured Topology, 0-Auto; 1-NPort; 2-Loop");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"configured_speed", CTLTYPE_UINT | CTLFLAG_RW,
			ocs, 0, ocs_sysctl_config_speed, "IU",
			"Configured Speed, 0-Auto, 2000, 4000, 8000, 16000, 32000");

	SYSCTL_ADD_STRING(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"businfo", CTLFLAG_RD,
			ocs->businfo,
			0, "Bus Info");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"fcid", CTLTYPE_STRING | CTLFLAG_RD,
			ocs, 0, ocs_sysctl_fcid, "A",
			"Port FC ID");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"port_state", CTLTYPE_STRING | CTLFLAG_RW,
			ocs, 0, ocs_sysctl_port_state, "A",
			"configured port state");

	for (i	= 0; i < ocs->num_vports; i++) {
		fcp = FCPORT(ocs, i+1);

		memset(name, 0, sizeof(name));
		snprintf(name, sizeof(name), "vport%d", i);
		vtree = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(tree),
				OID_AUTO, name, CTLFLAG_RW, 0, "Virtual port");

		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(vtree), OID_AUTO,
			"wwnn", CTLTYPE_STRING | CTLFLAG_RW,
			fcp, 0, ocs_sysctl_vport_wwnn, "A",
			"World Wide Node Name");

		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(vtree), OID_AUTO,
			"wwpn", CTLTYPE_STRING | CTLFLAG_RW,
			fcp, 0, ocs_sysctl_vport_wwpn, "A",
			"World Wide Port Name");

	}

}

/**
 * @brief Initialize the debug module
 *
 * Parse device hints (similar to Linux module parameters) here. To use,
 * run the command
 *    kenv hint.ocs.U.P=V
 * from the command line replacing U with the unit # (0,1,...),
 * P with the parameter name (debug_mask), and V with the value
 */
void
ocs_debug_attach(void *os)
{
	struct ocs_softc *ocs = os;
	int error = 0;
	char *resname = NULL;
	int32_t	unit = INT32_MAX;
	uint32_t ocs_debug_mask = 0;

	resname = "debug_mask";
	if (0 == (error = resource_int_value(device_get_name(ocs->dev), device_get_unit(ocs->dev),
				resname, &ocs_debug_mask))) {
		device_printf(ocs->dev, "setting %s to %010x\n", resname, ocs_debug_mask);
		ocs_debug_enable(ocs_debug_mask);
	}

	unit = device_get_unit(ocs->dev);
	ocs->cdev = make_dev(&ocs_cdevsw, unit, UID_ROOT, GID_OPERATOR, 0640,
			"ocs%d", unit);
	if (ocs->cdev) {
		ocs->cdev->si_drv1 = ocs;
	}

	/* initialize sysctl interface */
	ocs_sysctl_init(ocs);
	mtx_init(&ocs->dbg_lock, "ocs_dbg_lock", NULL, MTX_DEF);
}

/**
 * @brief Free the debug module
 */
void
ocs_debug_detach(void *os)
{
	struct ocs_softc *ocs = os;

	mtx_destroy(&ocs->dbg_lock);

	if (ocs->cdev) {
		ocs->cdev->si_drv1 = NULL;
		destroy_dev(ocs->cdev);
	}
}

