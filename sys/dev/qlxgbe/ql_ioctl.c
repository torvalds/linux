/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013-2016 Qlogic Corporation
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * File: ql_ioctl.c
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");


#include "ql_os.h"
#include "ql_hw.h"
#include "ql_def.h"
#include "ql_inline.h"
#include "ql_glbl.h"
#include "ql_ioctl.h"
#include "ql_ver.h"
#include "ql_dbg.h"

static int ql_slowpath_log(qla_host_t *ha, qla_sp_log_t *log);
static int ql_drvr_state(qla_host_t *ha, qla_driver_state_t *drvr_state);
static uint32_t ql_drvr_state_size(qla_host_t *ha);
static int ql_eioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
		struct thread *td);

static struct cdevsw qla_cdevsw = {
	.d_version = D_VERSION,
	.d_ioctl = ql_eioctl,
	.d_name = "qlcnic",
};

int
ql_make_cdev(qla_host_t *ha)
{
        ha->ioctl_dev = make_dev(&qla_cdevsw,
				ha->ifp->if_dunit,
                                UID_ROOT,
                                GID_WHEEL,
                                0600,
                                "%s",
                                if_name(ha->ifp));

	if (ha->ioctl_dev == NULL)
		return (-1);

        ha->ioctl_dev->si_drv1 = ha;

	return (0);
}

void
ql_del_cdev(qla_host_t *ha)
{
	if (ha->ioctl_dev != NULL)
		destroy_dev(ha->ioctl_dev);
	return;
}

static int
ql_eioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
	struct thread *td)
{
        qla_host_t *ha;
        int rval = 0;
	device_t pci_dev;
	struct ifnet *ifp;
	int count;

	q80_offchip_mem_val_t val;
	qla_rd_pci_ids_t *pci_ids;
	qla_rd_fw_dump_t *fw_dump;
        union {
		qla_reg_val_t *rv;
	        qla_rd_flash_t *rdf;
		qla_wr_flash_t *wrf;
		qla_erase_flash_t *erf;
		qla_offchip_mem_val_t *mem;
	} u;


        if ((ha = (qla_host_t *)dev->si_drv1) == NULL)
                return ENXIO;

	pci_dev= ha->pci_dev;

        switch(cmd) {

        case QLA_RDWR_REG:

                u.rv = (qla_reg_val_t *)data;

                if (u.rv->direct) {
                        if (u.rv->rd) {
                                u.rv->val = READ_REG32(ha, u.rv->reg);
                        } else {
                                WRITE_REG32(ha, u.rv->reg, u.rv->val);
                        }
                } else {
                        if ((rval = ql_rdwr_indreg32(ha, u.rv->reg, &u.rv->val,
                                u.rv->rd)))
                                rval = ENXIO;
                }
                break;

        case QLA_RD_FLASH:

		if (!ha->hw.flags.fdt_valid) {
			rval = EIO;
			break;
		}	

                u.rdf = (qla_rd_flash_t *)data;
                if ((rval = ql_rd_flash32(ha, u.rdf->off, &u.rdf->data)))
                        rval = ENXIO;
                break;

	case QLA_WR_FLASH:

		ifp = ha->ifp;

		if (ifp == NULL) {
			rval = ENXIO;
			break;
		}

		if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
			rval = ENXIO;
			break;
		}

		if (!ha->hw.flags.fdt_valid) {
			rval = EIO;
			break;
		}	

		u.wrf = (qla_wr_flash_t *)data;
		if ((rval = ql_wr_flash_buffer(ha, u.wrf->off, u.wrf->size,
			u.wrf->buffer))) {
			printf("flash write failed[%d]\n", rval);
			rval = ENXIO;
		}
		break;

	case QLA_ERASE_FLASH:

		ifp = ha->ifp;

		if (ifp == NULL) {
			rval = ENXIO;
			break;
		}

		if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
			rval = ENXIO;
			break;
		}

		if (!ha->hw.flags.fdt_valid) {
			rval = EIO;
			break;
		}	
		
		u.erf = (qla_erase_flash_t *)data;
		if ((rval = ql_erase_flash(ha, u.erf->off, 
			u.erf->size))) {
			printf("flash erase failed[%d]\n", rval);
			rval = ENXIO;
		}
		break;

	case QLA_RDWR_MS_MEM:
		u.mem = (qla_offchip_mem_val_t *)data;

		if ((rval = ql_rdwr_offchip_mem(ha, u.mem->off, &val, 
			u.mem->rd)))
			rval = ENXIO;
		else {
			u.mem->data_lo = val.data_lo;
			u.mem->data_hi = val.data_hi;
			u.mem->data_ulo = val.data_ulo;
			u.mem->data_uhi = val.data_uhi;
		}

		break;

	case QLA_RD_FW_DUMP_SIZE:

		if (ha->hw.mdump_init == 0) {
			rval = EINVAL;
			break;
		}
		
		fw_dump = (qla_rd_fw_dump_t *)data;
		fw_dump->minidump_size = ha->hw.mdump_buffer_size + 
						ha->hw.mdump_template_size;
		fw_dump->pci_func = ha->pci_func;

		break;

	case QLA_RD_FW_DUMP:

		if (ha->hw.mdump_init == 0) {
			device_printf(pci_dev, "%s: minidump not initialized\n", __func__);
			rval = EINVAL;
			break;
		}
		
		fw_dump = (qla_rd_fw_dump_t *)data;

		if ((fw_dump->minidump == NULL) ||
			(fw_dump->minidump_size != (ha->hw.mdump_buffer_size +
				ha->hw.mdump_template_size))) {
			device_printf(pci_dev,
				"%s: minidump buffer [%p] size = [%d, %d] invalid\n", __func__,
				fw_dump->minidump, fw_dump->minidump_size,
				(ha->hw.mdump_buffer_size + ha->hw.mdump_template_size));
			rval = EINVAL;
			break;
		}

		if ((ha->pci_func & 0x1)) {
			device_printf(pci_dev, "%s: mindump allowed only on Port0\n", __func__);
			rval = ENXIO;
			break;
		}

		fw_dump->saved = 1;

		if (ha->offline) {

			if (ha->enable_minidump)
				ql_minidump(ha);

			fw_dump->saved = 0;
			fw_dump->usec_ts = ha->hw.mdump_usec_ts;

			if (!ha->hw.mdump_done) {
				device_printf(pci_dev,
					"%s: port offline minidump failed\n", __func__);
				rval = ENXIO;
				break;
			}
		} else {

#define QLA_LOCK_MDUMP_MS_TIMEOUT (QLA_LOCK_DEFAULT_MS_TIMEOUT * 5)
			if (QLA_LOCK(ha, __func__, QLA_LOCK_MDUMP_MS_TIMEOUT, 0) == 0) {
				if (!ha->hw.mdump_done) {
					fw_dump->saved = 0;
					QL_INITIATE_RECOVERY(ha);
					device_printf(pci_dev, "%s: recovery initiated "
						" to trigger minidump\n",
						__func__);
				}
				QLA_UNLOCK(ha, __func__);
			} else {
				device_printf(pci_dev, "%s: QLA_LOCK() failed0\n", __func__);
				rval = ENXIO;
				break;
			}
	
#define QLNX_DUMP_WAIT_SECS	30

			count = QLNX_DUMP_WAIT_SECS * 1000;

			while (count) {
				if (ha->hw.mdump_done)
					break;
				qla_mdelay(__func__, 100);
				count -= 100;
			}

			if (!ha->hw.mdump_done) {
				device_printf(pci_dev,
					"%s: port not offline minidump failed\n", __func__);
				rval = ENXIO;
				break;
			}
			fw_dump->usec_ts = ha->hw.mdump_usec_ts;
			
			if (QLA_LOCK(ha, __func__, QLA_LOCK_MDUMP_MS_TIMEOUT, 0) == 0) {
				ha->hw.mdump_done = 0;
				QLA_UNLOCK(ha, __func__);
			} else {
				device_printf(pci_dev, "%s: QLA_LOCK() failed1\n", __func__);
				rval = ENXIO;
				break;
			}
		}

		if ((rval = copyout(ha->hw.mdump_template,
			fw_dump->minidump, ha->hw.mdump_template_size))) {
			device_printf(pci_dev, "%s: template copyout failed\n", __func__);
			rval = ENXIO;
			break;
		}

		if ((rval = copyout(ha->hw.mdump_buffer,
				((uint8_t *)fw_dump->minidump +
					ha->hw.mdump_template_size),
				ha->hw.mdump_buffer_size))) {
			device_printf(pci_dev, "%s: minidump copyout failed\n", __func__);
			rval = ENXIO;
		}
		break;

	case QLA_RD_DRVR_STATE:
		rval = ql_drvr_state(ha, (qla_driver_state_t *)data);
		break;

	case QLA_RD_SLOWPATH_LOG:
		rval = ql_slowpath_log(ha, (qla_sp_log_t *)data);
		break;

	case QLA_RD_PCI_IDS:
		pci_ids = (qla_rd_pci_ids_t *)data;
		pci_ids->ven_id = pci_get_vendor(pci_dev);
		pci_ids->dev_id = pci_get_device(pci_dev);
		pci_ids->subsys_ven_id = pci_get_subvendor(pci_dev);
		pci_ids->subsys_dev_id = pci_get_subdevice(pci_dev);
		pci_ids->rev_id = pci_read_config(pci_dev, PCIR_REVID, 1);
		break;

        default:
                break;
        }

        return rval;
}



static int
ql_drvr_state(qla_host_t *ha, qla_driver_state_t *state)
{
	int rval = 0;
	uint32_t drvr_state_size;

	drvr_state_size = ql_drvr_state_size(ha);

	if (state->buffer == NULL) {
		state->size = drvr_state_size;
		return (0);
	}
		
	if (state->size < drvr_state_size)
		return (ENXIO);

	if (ha->hw.drvr_state == NULL)
		return (ENOMEM);

	ql_capture_drvr_state(ha);

	rval = copyout(ha->hw.drvr_state, state->buffer, drvr_state_size);

	bzero(ha->hw.drvr_state, drvr_state_size);

	return (rval);
}

static uint32_t
ql_drvr_state_size(qla_host_t *ha)
{
	uint32_t drvr_state_size;
	uint32_t size;

	size = sizeof (qla_drvr_state_hdr_t);
	drvr_state_size = QL_ALIGN(size, 64);

	size =  ha->hw.num_tx_rings * (sizeof (qla_drvr_state_tx_t));
	drvr_state_size += QL_ALIGN(size, 64);

	size =  ha->hw.num_rds_rings * (sizeof (qla_drvr_state_rx_t));
	drvr_state_size += QL_ALIGN(size, 64);

	size =  ha->hw.num_sds_rings * (sizeof (qla_drvr_state_sds_t));
	drvr_state_size += QL_ALIGN(size, 64);

	size = sizeof(q80_tx_cmd_t) * NUM_TX_DESCRIPTORS * ha->hw.num_tx_rings;
	drvr_state_size += QL_ALIGN(size, 64);

	size = sizeof(q80_recv_desc_t) * NUM_RX_DESCRIPTORS * ha->hw.num_rds_rings;
	drvr_state_size += QL_ALIGN(size, 64);

	size = sizeof(q80_stat_desc_t) * NUM_STATUS_DESCRIPTORS *
			ha->hw.num_sds_rings;
	drvr_state_size += QL_ALIGN(size, 64);

	return (drvr_state_size);
}

static void
ql_get_tx_state(qla_host_t *ha, qla_drvr_state_tx_t *tx_state)
{
	int i;

	for (i = 0; i < ha->hw.num_tx_rings; i++) {
		tx_state->base_p_addr = ha->hw.tx_cntxt[i].tx_ring_paddr;
		tx_state->cons_p_addr = ha->hw.tx_cntxt[i].tx_cons_paddr;
		tx_state->tx_prod_reg = ha->hw.tx_cntxt[i].tx_prod_reg;
		tx_state->tx_cntxt_id = ha->hw.tx_cntxt[i].tx_cntxt_id;
		tx_state->txr_free = ha->hw.tx_cntxt[i].txr_free;
		tx_state->txr_next = ha->hw.tx_cntxt[i].txr_next;
		tx_state->txr_comp = ha->hw.tx_cntxt[i].txr_comp;
		tx_state++;
	}
	return;
}

static void
ql_get_rx_state(qla_host_t *ha, qla_drvr_state_rx_t *rx_state)
{
	int i;

	for (i = 0; i < ha->hw.num_rds_rings; i++) {
		rx_state->prod_std = ha->hw.rds[i].prod_std;
		rx_state->rx_next = ha->hw.rds[i].rx_next;
		rx_state++;
	}
	return;
}

static void
ql_get_sds_state(qla_host_t *ha, qla_drvr_state_sds_t *sds_state)
{
	int i;

	for (i = 0; i < ha->hw.num_sds_rings; i++) {
		sds_state->sdsr_next = ha->hw.sds[i].sdsr_next;
		sds_state->sds_consumer = ha->hw.sds[i].sds_consumer;
		sds_state++;
	}
	return;
}

void
ql_capture_drvr_state(qla_host_t *ha)
{
	uint8_t *state_buffer;
	uint8_t *ptr;
	qla_drvr_state_hdr_t *hdr;
	uint32_t size;
	int i;

	state_buffer =  ha->hw.drvr_state;

	if (state_buffer == NULL)
		return;

	hdr = (qla_drvr_state_hdr_t *)state_buffer;
	
	hdr->saved = 0;

	if (hdr->drvr_version_major) {
		hdr->saved = 1;
		return;
	}

	hdr->usec_ts = qla_get_usec_timestamp();

	hdr->drvr_version_major = QLA_VERSION_MAJOR;
	hdr->drvr_version_minor = QLA_VERSION_MINOR;
	hdr->drvr_version_build = QLA_VERSION_BUILD;

	bcopy(ha->hw.mac_addr, hdr->mac_addr, ETHER_ADDR_LEN);

	hdr->link_speed = ha->hw.link_speed;
	hdr->cable_length = ha->hw.cable_length;
	hdr->cable_oui = ha->hw.cable_oui;
	hdr->link_up = ha->hw.link_up;
	hdr->module_type = ha->hw.module_type;
	hdr->link_faults = ha->hw.link_faults;
	hdr->rcv_intr_coalesce = ha->hw.rcv_intr_coalesce;
	hdr->xmt_intr_coalesce = ha->hw.xmt_intr_coalesce;

	size = sizeof (qla_drvr_state_hdr_t);
	hdr->tx_state_offset = QL_ALIGN(size, 64);

	ptr = state_buffer + hdr->tx_state_offset;

	ql_get_tx_state(ha, (qla_drvr_state_tx_t *)ptr);

	size =  ha->hw.num_tx_rings * (sizeof (qla_drvr_state_tx_t));
	hdr->rx_state_offset = hdr->tx_state_offset + QL_ALIGN(size, 64);
	ptr = state_buffer + hdr->rx_state_offset;

	ql_get_rx_state(ha, (qla_drvr_state_rx_t *)ptr);

	size =  ha->hw.num_rds_rings * (sizeof (qla_drvr_state_rx_t));
	hdr->sds_state_offset = hdr->rx_state_offset + QL_ALIGN(size, 64);
	ptr = state_buffer + hdr->sds_state_offset;

	ql_get_sds_state(ha, (qla_drvr_state_sds_t *)ptr);

	size =  ha->hw.num_sds_rings * (sizeof (qla_drvr_state_sds_t));
	hdr->txr_offset = hdr->sds_state_offset + QL_ALIGN(size, 64);
	ptr = state_buffer + hdr->txr_offset;

	hdr->num_tx_rings = ha->hw.num_tx_rings;
	hdr->txr_size = sizeof(q80_tx_cmd_t) * NUM_TX_DESCRIPTORS;
	hdr->txr_entries = NUM_TX_DESCRIPTORS;

	size = hdr->num_tx_rings * hdr->txr_size;
	bcopy(ha->hw.dma_buf.tx_ring.dma_b, ptr, size);

	hdr->rxr_offset = hdr->txr_offset + QL_ALIGN(size, 64);
	ptr = state_buffer + hdr->rxr_offset;

	hdr->rxr_size = sizeof(q80_recv_desc_t) * NUM_RX_DESCRIPTORS;
	hdr->rxr_entries = NUM_RX_DESCRIPTORS;
	hdr->num_rx_rings = ha->hw.num_rds_rings;

	for (i = 0; i < ha->hw.num_rds_rings; i++) {
		bcopy(ha->hw.dma_buf.rds_ring[i].dma_b, ptr, hdr->rxr_size);
		ptr += hdr->rxr_size;
	}

	size = hdr->rxr_size * hdr->num_rx_rings;
	hdr->sds_offset = hdr->rxr_offset + QL_ALIGN(size, 64);
	hdr->sds_ring_size = sizeof(q80_stat_desc_t) * NUM_STATUS_DESCRIPTORS;
	hdr->sds_entries = NUM_STATUS_DESCRIPTORS;
	hdr->num_sds_rings = ha->hw.num_sds_rings;

	ptr = state_buffer + hdr->sds_offset;
	for (i = 0; i < ha->hw.num_sds_rings; i++) {
		bcopy(ha->hw.dma_buf.sds_ring[i].dma_b, ptr, hdr->sds_ring_size);
		ptr += hdr->sds_ring_size;
	}
	return;
}

void
ql_alloc_drvr_state_buffer(qla_host_t *ha)
{
	uint32_t drvr_state_size;

	drvr_state_size = ql_drvr_state_size(ha);

	ha->hw.drvr_state =  malloc(drvr_state_size, M_QLA83XXBUF, M_NOWAIT);	

	if (ha->hw.drvr_state != NULL)
		bzero(ha->hw.drvr_state, drvr_state_size);

	return;
}

void
ql_free_drvr_state_buffer(qla_host_t *ha)
{
	if (ha->hw.drvr_state != NULL)
		free(ha->hw.drvr_state, M_QLA83XXBUF);
	return;
}

void
ql_sp_log(qla_host_t *ha, uint16_t fmtstr_idx, uint16_t num_params,
	uint32_t param0, uint32_t param1, uint32_t param2, uint32_t param3,
	uint32_t param4)
{
	qla_sp_log_entry_t *sp_e, *sp_log;

	if (((sp_log = ha->hw.sp_log) == NULL) || ha->hw.sp_log_stop)
		return;

	mtx_lock(&ha->sp_log_lock);

	sp_e = &sp_log[ha->hw.sp_log_index];

	bzero(sp_e, sizeof (qla_sp_log_entry_t));

	sp_e->fmtstr_idx = fmtstr_idx;
	sp_e->num_params = num_params;

	sp_e->usec_ts = qla_get_usec_timestamp();

	sp_e->params[0] = param0;
	sp_e->params[1] = param1;
	sp_e->params[2] = param2;
	sp_e->params[3] = param3;
	sp_e->params[4] = param4;

	ha->hw.sp_log_index = (ha->hw.sp_log_index + 1) & (NUM_LOG_ENTRIES - 1);

	if (ha->hw.sp_log_num_entries < NUM_LOG_ENTRIES)
		ha->hw.sp_log_num_entries++;

	mtx_unlock(&ha->sp_log_lock);

	return;
}

void
ql_alloc_sp_log_buffer(qla_host_t *ha)
{
	uint32_t size;

	size = (sizeof(qla_sp_log_entry_t)) * NUM_LOG_ENTRIES;

	ha->hw.sp_log =  malloc(size, M_QLA83XXBUF, M_NOWAIT);	

	if (ha->hw.sp_log != NULL)
		bzero(ha->hw.sp_log, size);

	ha->hw.sp_log_index = 0;
	ha->hw.sp_log_num_entries = 0;

	return;
}

void
ql_free_sp_log_buffer(qla_host_t *ha)
{
	if (ha->hw.sp_log != NULL)
		free(ha->hw.sp_log, M_QLA83XXBUF);
	return;
}

static int
ql_slowpath_log(qla_host_t *ha, qla_sp_log_t *log)
{
	int rval = 0;
	uint32_t size;

	if ((ha->hw.sp_log == NULL) || (log->buffer == NULL))
		return (EINVAL);

	size = (sizeof(qla_sp_log_entry_t) * NUM_LOG_ENTRIES);

	mtx_lock(&ha->sp_log_lock);

	rval = copyout(ha->hw.sp_log, log->buffer, size);

	if (!rval) {
		log->next_idx = ha->hw.sp_log_index;
		log->num_entries = ha->hw.sp_log_num_entries;
	}
	device_printf(ha->pci_dev,
		"%s: exit [rval = %d][%p, next_idx = %d, %d entries, %d bytes]\n",
		__func__, rval, log->buffer, log->next_idx, log->num_entries, size);
	mtx_unlock(&ha->sp_log_lock);

	return (rval);
}

