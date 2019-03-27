/*
 * Copyright (c) 2017-2018 Cavium, Inc. 
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
 * File: qlnx_ioctl.c
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "qlnx_os.h"
#include "bcm_osal.h"

#include "reg_addr.h"
#include "ecore_gtt_reg_addr.h"
#include "ecore.h"
#include "ecore_chain.h"
#include "ecore_status.h"
#include "ecore_hw.h"
#include "ecore_rt_defs.h"
#include "ecore_init_ops.h"
#include "ecore_int.h"
#include "ecore_cxt.h"
#include "ecore_spq.h"
#include "ecore_init_fw_funcs.h"
#include "ecore_sp_commands.h"
#include "ecore_dev_api.h"
#include "ecore_l2_api.h"
#include "ecore_mcp.h"
#include "ecore_hw_defs.h"
#include "mcp_public.h"
#include "ecore_iro.h"
#include "nvm_cfg.h"
#include "ecore_dev_api.h"
#include "ecore_dbg_fw_funcs.h"
#include "ecore_dcbx_api.h"

#include "qlnx_ioctl.h"
#include "qlnx_def.h"
#include "qlnx_ver.h"
#include <sys/smp.h>


static int qlnx_eioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
                struct thread *td);

static struct cdevsw qlnx_cdevsw = {
        .d_version = D_VERSION,
        .d_ioctl = qlnx_eioctl,
        .d_name = "qlnxioctl",
};

int
qlnx_make_cdev(qlnx_host_t *ha)
{
	ha->ioctl_dev = make_dev(&qlnx_cdevsw,
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
qlnx_del_cdev(qlnx_host_t *ha)
{
	if (ha->ioctl_dev != NULL)
		destroy_dev(ha->ioctl_dev);
	return;
}

int
qlnx_grc_dump(qlnx_host_t *ha, uint32_t *num_dumped_dwords, int hwfn_index)
{
	int rval = EINVAL;
	struct ecore_hwfn *p_hwfn;
	struct ecore_ptt *p_ptt;

	if (ha->grcdump_dwords[hwfn_index]) {
		/* the grcdump is already available */
		*num_dumped_dwords = ha->grcdump_dwords[hwfn_index];
		return (0);
	}

	ecore_dbg_set_app_ver(ecore_dbg_get_fw_func_ver());

	p_hwfn = &ha->cdev.hwfns[hwfn_index];
	p_ptt = ecore_ptt_acquire(p_hwfn);

	if (!p_ptt) {
		QL_DPRINT1(ha,"ecore_ptt_acquire failed\n");
		return (rval);
	}

	if ((rval = ecore_dbg_grc_dump(p_hwfn, p_ptt,
			ha->grcdump[hwfn_index],
			(ha->grcdump_size[hwfn_index] >> 2),
			num_dumped_dwords)) == DBG_STATUS_OK) {
	 	rval = 0;	
		ha->grcdump_taken = 1;
	} else
		QL_DPRINT1(ha,"ecore_dbg_grc_dump failed [%d, 0x%x]\n",
			   hwfn_index, rval);

	ecore_ptt_release(p_hwfn, p_ptt);

	return (rval);
}

static void
qlnx_get_grc_dump_size(qlnx_host_t *ha, qlnx_grcdump_t *grcdump)
{
	int i;

	grcdump->pci_func = ha->pci_func;

	for (i = 0; i < ha->cdev.num_hwfns; i++)
		grcdump->grcdump_size[i] = ha->grcdump_size[i];

	return;
}

static int
qlnx_get_grc_dump(qlnx_host_t *ha, qlnx_grcdump_t *grcdump)
{
	int		i;
	int		rval = 0;
	uint32_t	dwords = 0;

	grcdump->pci_func = ha->pci_func;

	for (i = 0; i < ha->cdev.num_hwfns; i++) {

		if ((ha->grcdump[i] == NULL) || (grcdump->grcdump[i] == NULL) ||
			(grcdump->grcdump_size[i] < ha->grcdump_size[i]))
			return (EINVAL);

		rval = qlnx_grc_dump(ha, &dwords, i);

		if (rval)
			break;

		grcdump->grcdump_dwords[i] = dwords;

		QL_DPRINT1(ha,"grcdump_dwords[%d] = 0x%x\n", i, dwords);

		rval = copyout(ha->grcdump[i], grcdump->grcdump[i],
				ha->grcdump_size[i]);

		if (rval)
			break;

		ha->grcdump_dwords[i] = 0;
	}

	ha->grcdump_taken = 0;

	return (rval);
}

int
qlnx_idle_chk(qlnx_host_t *ha, uint32_t *num_dumped_dwords, int hwfn_index)
{
	int rval = EINVAL;
	struct ecore_hwfn *p_hwfn;
	struct ecore_ptt *p_ptt;

	if (ha->idle_chk_dwords[hwfn_index]) {
		/* the idle check is already available */
		*num_dumped_dwords = ha->idle_chk_dwords[hwfn_index];
		return (0);
	}

	ecore_dbg_set_app_ver(ecore_dbg_get_fw_func_ver());

	p_hwfn = &ha->cdev.hwfns[hwfn_index];
	p_ptt = ecore_ptt_acquire(p_hwfn);

	if (!p_ptt) {
		QL_DPRINT1(ha,"ecore_ptt_acquire failed\n");
		return (rval);
	}

	if ((rval = ecore_dbg_idle_chk_dump(p_hwfn, p_ptt,
			ha->idle_chk[hwfn_index],
			(ha->idle_chk_size[hwfn_index] >> 2),
			num_dumped_dwords)) == DBG_STATUS_OK) {
	 	rval = 0;	
		ha->idle_chk_taken = 1;
	} else
		QL_DPRINT1(ha,"ecore_dbg_idle_chk_dump failed [%d, 0x%x]\n",
			   hwfn_index, rval);

	ecore_ptt_release(p_hwfn, p_ptt);

	return (rval);
}

static void
qlnx_get_idle_chk_size(qlnx_host_t *ha, qlnx_idle_chk_t *idle_chk)
{
	int i;

	idle_chk->pci_func = ha->pci_func;

	for (i = 0; i < ha->cdev.num_hwfns; i++)
		idle_chk->idle_chk_size[i] = ha->idle_chk_size[i];

	return;
}

static int
qlnx_get_idle_chk(qlnx_host_t *ha, qlnx_idle_chk_t *idle_chk)
{
	int		i;
	int		rval = 0;
	uint32_t	dwords = 0;

	idle_chk->pci_func = ha->pci_func;

	for (i = 0; i < ha->cdev.num_hwfns; i++) {

		if ((ha->idle_chk[i] == NULL) ||
				(idle_chk->idle_chk[i] == NULL) ||
				(idle_chk->idle_chk_size[i] <
					ha->idle_chk_size[i]))
			return (EINVAL);

		rval = qlnx_idle_chk(ha, &dwords, i);

		if (rval)
			break;
		
		idle_chk->idle_chk_dwords[i] = dwords;

		QL_DPRINT1(ha,"idle_chk_dwords[%d] = 0x%x\n", i, dwords);

               	rval = copyout(ha->idle_chk[i], idle_chk->idle_chk[i],
				ha->idle_chk_size[i]);

		if (rval)
			break;

		ha->idle_chk_dwords[i] = 0;
	}
	ha->idle_chk_taken = 0;

	return (rval);
}

static uint32_t
qlnx_get_trace_cmd_size(qlnx_host_t *ha, int hwfn_index, uint16_t cmd)
{
        int rval = -1;
        struct ecore_hwfn *p_hwfn;
        struct ecore_ptt *p_ptt;
	uint32_t num_dwords = 0;

        p_hwfn = &ha->cdev.hwfns[hwfn_index];
        p_ptt = ecore_ptt_acquire(p_hwfn);

        if (!p_ptt) {
                QL_DPRINT1(ha, "ecore_ptt_acquire [%d, 0x%x]failed\n",
                           hwfn_index, cmd);
                return (0);
        }

	switch (cmd) {

	case QLNX_MCP_TRACE:
        	rval = ecore_dbg_mcp_trace_get_dump_buf_size(p_hwfn,
				p_ptt, &num_dwords);
		break;

	case QLNX_REG_FIFO:
        	rval = ecore_dbg_reg_fifo_get_dump_buf_size(p_hwfn,
				p_ptt, &num_dwords);
		break;

	case QLNX_IGU_FIFO:
        	rval = ecore_dbg_igu_fifo_get_dump_buf_size(p_hwfn,
				p_ptt, &num_dwords);
		break;

	case QLNX_PROTECTION_OVERRIDE:
        	rval = ecore_dbg_protection_override_get_dump_buf_size(p_hwfn,
				p_ptt, &num_dwords);
		break;

	case QLNX_FW_ASSERTS:
        	rval = ecore_dbg_fw_asserts_get_dump_buf_size(p_hwfn,
				p_ptt, &num_dwords);
		break;
	}

        if (rval != DBG_STATUS_OK) {
                QL_DPRINT1(ha,"cmd = 0x%x failed [0x%x]\n", cmd, rval);
		num_dwords = 0;
        }

        ecore_ptt_release(p_hwfn, p_ptt);

        return ((num_dwords * sizeof (uint32_t)));
}

static void
qlnx_get_trace_size(qlnx_host_t *ha, qlnx_trace_t *trace)
{
	int i;

	trace->pci_func = ha->pci_func;

	for (i = 0; i < ha->cdev.num_hwfns; i++) {
		trace->size[i] = qlnx_get_trace_cmd_size(ha, i, trace->cmd);
	}

	return;
}

static int
qlnx_get_trace(qlnx_host_t *ha, int hwfn_index, qlnx_trace_t *trace)
{
        int rval = -1;
        struct ecore_hwfn *p_hwfn;
        struct ecore_ptt *p_ptt;
	uint32_t num_dwords = 0;
	void *buffer;

	buffer = qlnx_zalloc(trace->size[hwfn_index]);
	if (buffer == NULL) { 
                QL_DPRINT1(ha,"qlnx_zalloc [%d, 0x%x]failed\n",
                           hwfn_index, trace->cmd);
                return (ENXIO);
	}
	ecore_dbg_set_app_ver(ecore_dbg_get_fw_func_ver());

        p_hwfn = &ha->cdev.hwfns[hwfn_index];
        p_ptt = ecore_ptt_acquire(p_hwfn);

        if (!p_ptt) {
                QL_DPRINT1(ha, "ecore_ptt_acquire [%d, 0x%x]failed\n",
                           hwfn_index, trace->cmd);
                return (ENXIO);
        }

	switch (trace->cmd) {

	case QLNX_MCP_TRACE:
        	rval = ecore_dbg_mcp_trace_dump(p_hwfn, p_ptt,
				buffer, (trace->size[hwfn_index] >> 2),
				&num_dwords);
		break;

	case QLNX_REG_FIFO:
        	rval = ecore_dbg_reg_fifo_dump(p_hwfn, p_ptt,
				buffer, (trace->size[hwfn_index] >> 2),
				&num_dwords);
		break;

	case QLNX_IGU_FIFO:
        	rval = ecore_dbg_igu_fifo_dump(p_hwfn, p_ptt,
				buffer, (trace->size[hwfn_index] >> 2),
				&num_dwords);
		break;

	case QLNX_PROTECTION_OVERRIDE:
        	rval = ecore_dbg_protection_override_dump(p_hwfn, p_ptt,
				buffer, (trace->size[hwfn_index] >> 2),
				&num_dwords);
		break;

	case QLNX_FW_ASSERTS:
        	rval = ecore_dbg_fw_asserts_dump(p_hwfn, p_ptt,
				buffer, (trace->size[hwfn_index] >> 2),
				&num_dwords);
		break;
	}

        if (rval != DBG_STATUS_OK) {
                QL_DPRINT1(ha,"cmd = 0x%x failed [0x%x]\n", trace->cmd, rval);
		num_dwords = 0;
        }

        ecore_ptt_release(p_hwfn, p_ptt);

	trace->dwords[hwfn_index] = num_dwords;

	if (num_dwords) {
               	rval = copyout(buffer, trace->buffer[hwfn_index],
				(num_dwords << 2));
	}

        return (rval);
}

static int
qlnx_reg_rd_wr(qlnx_host_t *ha, qlnx_reg_rd_wr_t *reg_rd_wr)
{
	int			rval = 0;
	struct ecore_hwfn	*p_hwfn;

	if (reg_rd_wr->hwfn_index >= QLNX_MAX_HW_FUNCS) {
		return (EINVAL);
	}

	p_hwfn = &ha->cdev.hwfns[reg_rd_wr->hwfn_index];

	switch (reg_rd_wr->cmd) {

		case QLNX_REG_READ_CMD:
			if (reg_rd_wr->access_type == QLNX_REG_ACCESS_DIRECT) {
				reg_rd_wr->val = qlnx_reg_rd32(p_hwfn,
							reg_rd_wr->addr);
			}
			break;

		case QLNX_REG_WRITE_CMD:
			if (reg_rd_wr->access_type == QLNX_REG_ACCESS_DIRECT) {
				qlnx_reg_wr32(p_hwfn, reg_rd_wr->addr,
					reg_rd_wr->val);
			}
			break;

		default:
			rval = EINVAL;
			break;
	} 

	return (rval);
}

static int
qlnx_rd_wr_pci_config(qlnx_host_t *ha, qlnx_pcicfg_rd_wr_t *pci_cfg_rd_wr)
{
	int rval = 0;

	switch (pci_cfg_rd_wr->cmd) {

		case QLNX_PCICFG_READ:
			pci_cfg_rd_wr->val = pci_read_config(ha->pci_dev,
						pci_cfg_rd_wr->reg,
						pci_cfg_rd_wr->width);
			break;

		case QLNX_PCICFG_WRITE:
			pci_write_config(ha->pci_dev, pci_cfg_rd_wr->reg,
				pci_cfg_rd_wr->val, pci_cfg_rd_wr->width);
			break;

		default:
			rval = EINVAL;
			break;
	} 

	return (rval);
}

static void
qlnx_mac_addr(qlnx_host_t *ha, qlnx_perm_mac_addr_t *mac_addr)
{
	bzero(mac_addr->addr, sizeof(mac_addr->addr));
	snprintf(mac_addr->addr, sizeof(mac_addr->addr),
		"%02x:%02x:%02x:%02x:%02x:%02x",
		ha->primary_mac[0], ha->primary_mac[1], ha->primary_mac[2],
		ha->primary_mac[3], ha->primary_mac[4], ha->primary_mac[5]);

	return;
}

static int
qlnx_get_regs(qlnx_host_t *ha, qlnx_get_regs_t *regs)
{
	int		i;
	int		rval = 0;
	uint32_t	dwords = 0;
	uint8_t		*outb;

	regs->reg_buf_len = 0;
	outb = regs->reg_buf;

	for (i = 0; i < ha->cdev.num_hwfns; i++) {

		rval = qlnx_grc_dump(ha, &dwords, i);

		if (rval)
			break;

		regs->reg_buf_len += (dwords << 2);

		rval = copyout(ha->grcdump[i], outb, ha->grcdump_size[i]);

		if (rval)
			break;

		ha->grcdump_dwords[i] = 0;
		outb += regs->reg_buf_len;
	}

	ha->grcdump_taken = 0;

	return (rval);
}

extern char qlnx_name_str[];
extern char qlnx_ver_str[];

static int
qlnx_drv_info(qlnx_host_t *ha, qlnx_drvinfo_t *drv_info)
{
	int i;

	bzero(drv_info, sizeof(qlnx_drvinfo_t));

	snprintf(drv_info->drv_name, sizeof(drv_info->drv_name), "%s",
		qlnx_name_str);
	snprintf(drv_info->drv_version, sizeof(drv_info->drv_version), "%s",
		qlnx_ver_str);
	snprintf(drv_info->mfw_version, sizeof(drv_info->mfw_version), "%s",
		ha->mfw_ver);
	snprintf(drv_info->stormfw_version, sizeof(drv_info->stormfw_version),
		"%s", ha->stormfw_ver);

	drv_info->eeprom_dump_len = ha->flash_size;

	for (i = 0; i < ha->cdev.num_hwfns; i++) {
		drv_info->reg_dump_len += ha->grcdump_size[i]; 
	}

	snprintf(drv_info->bus_info, sizeof(drv_info->bus_info),
		"%d:%d:%d", pci_get_bus(ha->pci_dev),
		pci_get_slot(ha->pci_dev), ha->pci_func);

	return (0);
}

static int
qlnx_dev_settings(qlnx_host_t *ha, qlnx_dev_setting_t *dev_info)
{
	struct ecore_hwfn *p_hwfn;
	struct qlnx_link_output if_link;

	p_hwfn = &ha->cdev.hwfns[0];

	qlnx_fill_link(ha, p_hwfn, &if_link);

	dev_info->supported = if_link.supported_caps;
	dev_info->advertising = if_link.advertised_caps;
	dev_info->speed = if_link.speed;
	dev_info->duplex = if_link.duplex;
	dev_info->port = ha->pci_func & 0x1;
	dev_info->autoneg = if_link.autoneg;

	return (0);
}

static int
qlnx_write_nvram(qlnx_host_t *ha, qlnx_nvram_t *nvram, uint32_t cmd)
{
	uint8_t *buf;
	int ret = 0;

	if ((nvram->data == NULL) || (nvram->data_len == 0))
		return (EINVAL);

	buf = qlnx_zalloc(nvram->data_len);

	ret = copyin(nvram->data, buf, nvram->data_len);

	QL_DPRINT9(ha, "issue cmd = 0x%x data = %p \
		 data_len = 0x%x ret = 0x%x exit\n",
		cmd, nvram->data, nvram->data_len, ret);

	if (ret == 0) {
		ret = ecore_mcp_nvm_write(&ha->cdev, cmd,
			nvram->offset, buf, nvram->data_len);
	}

	QL_DPRINT9(ha, "cmd = 0x%x data = %p \
		 data_len = 0x%x resp = 0x%x ret = 0x%x exit\n",
		cmd, nvram->data, nvram->data_len, ha->cdev.mcp_nvm_resp, ret);

	free(buf, M_QLNXBUF);

	return (ret);
}

static int
qlnx_read_nvram(qlnx_host_t *ha, qlnx_nvram_t *nvram)
{
	uint8_t *buf;
	int ret = 0;

	if ((nvram->data == NULL) || (nvram->data_len == 0))
		return (EINVAL);

	buf = qlnx_zalloc(nvram->data_len);

	ret = ecore_mcp_nvm_read(&ha->cdev, nvram->offset, buf,
		nvram->data_len);

	QL_DPRINT9(ha, " data = %p data_len = 0x%x \
		 resp = 0x%x ret = 0x%x exit\n",
		nvram->data, nvram->data_len, ha->cdev.mcp_nvm_resp, ret);

	if (ret == 0) {
		ret = copyout(buf, nvram->data, nvram->data_len);
	}

	free(buf, M_QLNXBUF);

	return (ret);
}

static int
qlnx_get_nvram_resp(qlnx_host_t *ha, qlnx_nvram_t *nvram)
{
	uint8_t *buf;
	int ret = 0;

	if ((nvram->data == NULL) || (nvram->data_len == 0))
		return (EINVAL);

	buf = qlnx_zalloc(nvram->data_len);


	ret = ecore_mcp_nvm_resp(&ha->cdev, buf);

	QL_DPRINT9(ha, "data = %p data_len = 0x%x \
		 resp = 0x%x ret = 0x%x exit\n",
		nvram->data, nvram->data_len, ha->cdev.mcp_nvm_resp, ret);

	if (ret == 0) {
		ret = copyout(buf, nvram->data, nvram->data_len);
	}

	free(buf, M_QLNXBUF);

	return (ret);
}

static int
qlnx_nvram(qlnx_host_t *ha, qlnx_nvram_t *nvram)
{
	int ret = 0;

	switch (nvram->cmd) {

	case QLNX_NVRAM_CMD_WRITE_NVRAM:
		ret = qlnx_write_nvram(ha, nvram, ECORE_NVM_WRITE_NVRAM);
		break;

	case QLNX_NVRAM_CMD_PUT_FILE_DATA:
		ret = qlnx_write_nvram(ha, nvram, ECORE_PUT_FILE_DATA);
		break;

	case QLNX_NVRAM_CMD_READ_NVRAM:
		ret = qlnx_read_nvram(ha, nvram);
		break;

	case QLNX_NVRAM_CMD_SET_SECURE_MODE:
		ret = ecore_mcp_nvm_set_secure_mode(&ha->cdev, nvram->offset);

		QL_DPRINT9(ha, "QLNX_NVRAM_CMD_SET_SECURE_MODE \
			 resp = 0x%x ret = 0x%x exit\n",
			 ha->cdev.mcp_nvm_resp, ret);
		break;

	case QLNX_NVRAM_CMD_DEL_FILE:
		ret = ecore_mcp_nvm_del_file(&ha->cdev, nvram->offset);

		QL_DPRINT9(ha, "QLNX_NVRAM_CMD_DEL_FILE \
			 resp = 0x%x ret = 0x%x exit\n",
			ha->cdev.mcp_nvm_resp, ret);
		break;

	case QLNX_NVRAM_CMD_PUT_FILE_BEGIN:
		ret = ecore_mcp_nvm_put_file_begin(&ha->cdev, nvram->offset);

		QL_DPRINT9(ha, "QLNX_NVRAM_CMD_PUT_FILE_BEGIN \
			 resp = 0x%x ret = 0x%x exit\n",
			ha->cdev.mcp_nvm_resp, ret);
		break;

	case QLNX_NVRAM_CMD_GET_NVRAM_RESP:
		ret = qlnx_get_nvram_resp(ha, nvram);
		break;

	default:
		ret = EINVAL;
		break;
	}

	return (ret);
}

static void
qlnx_storm_stats(qlnx_host_t *ha, qlnx_storm_stats_dump_t *s_stats)
{
	int i;
	int index;
	int ret;
	int stats_copied = 0;

	s_stats->num_hwfns = ha->cdev.num_hwfns;

//	if (ha->storm_stats_index < QLNX_STORM_STATS_SAMPLES_PER_HWFN)
//		return;

	s_stats->num_samples = ha->storm_stats_index;

	for (i = 0; i < ha->cdev.num_hwfns; i++) {

		index = (QLNX_STORM_STATS_SAMPLES_PER_HWFN * i);

		if (s_stats->buffer[i]) {

			ret = copyout(&ha->storm_stats[index],
					s_stats->buffer[i],
					QLNX_STORM_STATS_BYTES_PER_HWFN);
			if (ret) {
				printf("%s [%d]: failed\n", __func__, i);
			}

			if (s_stats->num_samples ==
				QLNX_STORM_STATS_SAMPLES_PER_HWFN) {

				bzero((void *)&ha->storm_stats[i],
					QLNX_STORM_STATS_BYTES_PER_HWFN);

				stats_copied = 1;
			}
		}
	}

	if (stats_copied)
		ha->storm_stats_index = 0;

	return;
}

#ifdef QLNX_USER_LLDP

static int
qlnx_lldp_configure(qlnx_host_t *ha, struct ecore_hwfn *p_hwfn,
	struct ecore_ptt *p_ptt, uint32_t enable)
{
	int ret = 0;
	uint8_t lldp_mac[6] = {0};
	struct ecore_lldp_config_params lldp_params;
	struct ecore_lldp_sys_tlvs tlv_params;

	ret = ecore_mcp_get_lldp_mac(p_hwfn, p_ptt, lldp_mac);

	if (ret != ECORE_SUCCESS) {
                device_printf(ha->pci_dev,
			"%s: ecore_mcp_get_lldp_mac failed\n", __func__);
                return (-1);
	}

	bzero(&lldp_params, sizeof(struct ecore_lldp_config_params));
	bzero(&tlv_params, sizeof(struct ecore_lldp_sys_tlvs));

	lldp_params.agent = ECORE_LLDP_NEAREST_BRIDGE;
	lldp_params.tx_interval = 30; //Default value used as suggested by MFW
	lldp_params.tx_hold = 4; //Default value used as suggested by MFW
	lldp_params.tx_credit = 5; //Default value used as suggested by MFW
	lldp_params.rx_enable = enable ? 1 : 0;
	lldp_params.tx_enable = enable ? 1 : 0;

	lldp_params.chassis_id_tlv[0] = 0;
	lldp_params.chassis_id_tlv[0] |= (QLNX_LLDP_TYPE_CHASSIS_ID << 1);
	lldp_params.chassis_id_tlv[0] |=
		((QLNX_LLDP_CHASSIS_ID_SUBTYPE_OCTETS +
			QLNX_LLDP_CHASSIS_ID_MAC_ADDR_LEN) << 8);
	lldp_params.chassis_id_tlv[0] |= (QLNX_LLDP_CHASSIS_ID_SUBTYPE_MAC << 16);
	lldp_params.chassis_id_tlv[0] |= lldp_mac[0] << 24;
	lldp_params.chassis_id_tlv[1] = lldp_mac[1] | (lldp_mac[2] << 8) |
		 (lldp_mac[3] << 16) | (lldp_mac[4] << 24);
	lldp_params.chassis_id_tlv[2] = lldp_mac[5];


	lldp_params.port_id_tlv[0] = 0;
	lldp_params.port_id_tlv[0] |= (QLNX_LLDP_TYPE_PORT_ID << 1);
	lldp_params.port_id_tlv[0] |=
		((QLNX_LLDP_PORT_ID_SUBTYPE_OCTETS +
			QLNX_LLDP_PORT_ID_MAC_ADDR_LEN) << 8);
	lldp_params.port_id_tlv[0] |= (QLNX_LLDP_PORT_ID_SUBTYPE_MAC << 16);
	lldp_params.port_id_tlv[0] |= lldp_mac[0] << 24;
	lldp_params.port_id_tlv[1] = lldp_mac[1] | (lldp_mac[2] << 8) |
		 (lldp_mac[3] << 16) | (lldp_mac[4] << 24);
	lldp_params.port_id_tlv[2] = lldp_mac[5];

	ret = ecore_lldp_set_params(p_hwfn, p_ptt, &lldp_params);

	if (ret != ECORE_SUCCESS) {
                device_printf(ha->pci_dev,
			"%s: ecore_lldp_set_params failed\n", __func__);
                return (-1);
	}

	//If LLDP is disable then disable discard_mandatory_tlv flag
	if (!enable) {
		tlv_params.discard_mandatory_tlv = false;
		tlv_params.buf_size = 0;
		ret = ecore_lldp_set_system_tlvs(p_hwfn, p_ptt, &tlv_params);
    	}

	if (ret != ECORE_SUCCESS) {
                device_printf(ha->pci_dev,
			"%s: ecore_lldp_set_system_tlvs failed\n", __func__);
	}

	return (ret);
}

static int
qlnx_register_default_lldp_tlvs(qlnx_host_t *ha, struct ecore_hwfn *p_hwfn,
	struct ecore_ptt *p_ptt)
{
	int ret = 0;

	ret = ecore_lldp_register_tlv(p_hwfn, p_ptt,
			ECORE_LLDP_NEAREST_BRIDGE, QLNX_LLDP_TYPE_CHASSIS_ID);
	if (ret != ECORE_SUCCESS) {
                device_printf(ha->pci_dev,
			"%s: QLNX_LLDP_TYPE_CHASSIS_ID failed\n", __func__);
		goto qlnx_register_default_lldp_tlvs_exit;
	}

	//register Port ID TLV
	ret = ecore_lldp_register_tlv(p_hwfn, p_ptt,
			ECORE_LLDP_NEAREST_BRIDGE, QLNX_LLDP_TYPE_PORT_ID);
	if (ret != ECORE_SUCCESS) {
                device_printf(ha->pci_dev,
			"%s: QLNX_LLDP_TYPE_PORT_ID failed\n", __func__);
		goto qlnx_register_default_lldp_tlvs_exit;
	}

	//register TTL TLV
	ret = ecore_lldp_register_tlv(p_hwfn, p_ptt,
			ECORE_LLDP_NEAREST_BRIDGE, QLNX_LLDP_TYPE_TTL);
	if (ret != ECORE_SUCCESS) {
                device_printf(ha->pci_dev,
			"%s: QLNX_LLDP_TYPE_TTL failed\n", __func__);
		goto qlnx_register_default_lldp_tlvs_exit;
	}

	//register Port Description TLV
	ret = ecore_lldp_register_tlv(p_hwfn, p_ptt,
			ECORE_LLDP_NEAREST_BRIDGE, QLNX_LLDP_TYPE_PORT_DESC);
	if (ret != ECORE_SUCCESS) {
                device_printf(ha->pci_dev,
			"%s: QLNX_LLDP_TYPE_PORT_DESC failed\n", __func__);
		goto qlnx_register_default_lldp_tlvs_exit;
	}

	//register System Name TLV
	ret = ecore_lldp_register_tlv(p_hwfn, p_ptt,
			ECORE_LLDP_NEAREST_BRIDGE, QLNX_LLDP_TYPE_SYS_NAME);
	if (ret != ECORE_SUCCESS) {
                device_printf(ha->pci_dev,
			"%s: QLNX_LLDP_TYPE_SYS_NAME failed\n", __func__);
		goto qlnx_register_default_lldp_tlvs_exit;
	}

	//register System Description TLV
	ret = ecore_lldp_register_tlv(p_hwfn, p_ptt,
			ECORE_LLDP_NEAREST_BRIDGE, QLNX_LLDP_TYPE_SYS_DESC);
	if (ret != ECORE_SUCCESS) {
                device_printf(ha->pci_dev,
			"%s: QLNX_LLDP_TYPE_SYS_DESC failed\n", __func__);
		goto qlnx_register_default_lldp_tlvs_exit;
	}

	//register System Capabilities TLV
	ret = ecore_lldp_register_tlv(p_hwfn, p_ptt,
			ECORE_LLDP_NEAREST_BRIDGE, QLNX_LLDP_TYPE_SYS_CAPS);
	if (ret != ECORE_SUCCESS) {
                device_printf(ha->pci_dev,
			"%s: QLNX_LLDP_TYPE_SYS_CAPS failed\n", __func__);
		goto qlnx_register_default_lldp_tlvs_exit;
	}

	//register Management Address TLV
	ret = ecore_lldp_register_tlv(p_hwfn, p_ptt,
			ECORE_LLDP_NEAREST_BRIDGE, QLNX_LLDP_TYPE_MGMT_ADDR);
	if (ret != ECORE_SUCCESS) {
                device_printf(ha->pci_dev,
			"%s: QLNX_LLDP_TYPE_MGMT_ADDR failed\n", __func__);
		goto qlnx_register_default_lldp_tlvs_exit;
	}

	//register Organizationally Specific TLVs
	ret = ecore_lldp_register_tlv(p_hwfn, p_ptt,
			ECORE_LLDP_NEAREST_BRIDGE, QLNX_LLDP_TYPE_ORG_SPECIFIC);
	if (ret != ECORE_SUCCESS) {
                device_printf(ha->pci_dev,
			"%s: QLNX_LLDP_TYPE_ORG_SPECIFIC failed\n", __func__);
	}

qlnx_register_default_lldp_tlvs_exit:
	return (ret);
}

int
qlnx_set_lldp_tlvx(qlnx_host_t *ha, qlnx_lldp_sys_tlvs_t *lldp_tlvs)
{
	int ret = 0;
	struct ecore_hwfn *p_hwfn;
	struct ecore_ptt *p_ptt;
	struct ecore_lldp_sys_tlvs tlv_params;

	p_hwfn = &ha->cdev.hwfns[0];
	p_ptt = ecore_ptt_acquire(p_hwfn);

        if (!p_ptt) {
                device_printf(ha->pci_dev,
			"%s: ecore_ptt_acquire failed\n", __func__);
                return (ENXIO);
        }

	ret = qlnx_lldp_configure(ha, p_hwfn, p_ptt, 0);

	if (ret) {
                device_printf(ha->pci_dev,
			"%s: qlnx_lldp_configure disable failed\n", __func__);
		goto qlnx_set_lldp_tlvx_exit;
	}

	ret = qlnx_register_default_lldp_tlvs(ha, p_hwfn, p_ptt);

	if (ret) {
                device_printf(ha->pci_dev,
			"%s: qlnx_register_default_lldp_tlvs failed\n",
			__func__);
		goto qlnx_set_lldp_tlvx_exit;
	}

	ret = qlnx_lldp_configure(ha, p_hwfn, p_ptt, 1);

	if (ret) {
                device_printf(ha->pci_dev,
			"%s: qlnx_lldp_configure enable failed\n", __func__);
		goto qlnx_set_lldp_tlvx_exit;
	}

	if (lldp_tlvs != NULL) {
		bzero(&tlv_params, sizeof(struct ecore_lldp_sys_tlvs));

		tlv_params.discard_mandatory_tlv =
			(lldp_tlvs->discard_mandatory_tlv ? true: false);
		tlv_params.buf_size = lldp_tlvs->buf_size;
		memcpy(tlv_params.buf, lldp_tlvs->buf, lldp_tlvs->buf_size);

		ret = ecore_lldp_set_system_tlvs(p_hwfn, p_ptt, &tlv_params);
	
		if (ret) {
			device_printf(ha->pci_dev,
				"%s: ecore_lldp_set_system_tlvs failed\n",
				__func__);
		}
	}
qlnx_set_lldp_tlvx_exit:

	ecore_ptt_release(p_hwfn, p_ptt);
	return (ret);
}

#endif /* #ifdef QLNX_USER_LLDP */

static int
qlnx_eioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
	struct thread *td)
{
	qlnx_host_t	*ha;
	int		rval = 0;
	struct ifnet	*ifp;
	qlnx_trace_t	*trace;
	int		i;

	if ((ha = (qlnx_host_t *)dev->si_drv1) == NULL)
		return ENXIO;

	ifp = ha->ifp;

	switch (cmd) {

	case QLNX_GRC_DUMP_SIZE:
		qlnx_get_grc_dump_size(ha, (qlnx_grcdump_t *)data);
		break;

	case QLNX_GRC_DUMP:
		rval = qlnx_get_grc_dump(ha, (qlnx_grcdump_t *)data);
		break;

	case QLNX_IDLE_CHK_SIZE:
		qlnx_get_idle_chk_size(ha, (qlnx_idle_chk_t *)data);
		break;

	case QLNX_IDLE_CHK:
		rval = qlnx_get_idle_chk(ha, (qlnx_idle_chk_t *)data);
		break;

	case QLNX_DRV_INFO:
		rval = qlnx_drv_info(ha, (qlnx_drvinfo_t *)data);
		break;

	case QLNX_DEV_SETTING:
		rval = qlnx_dev_settings(ha, (qlnx_dev_setting_t *)data);
		break;

	case QLNX_GET_REGS:
		rval = qlnx_get_regs(ha, (qlnx_get_regs_t *)data);
		break;

	case QLNX_NVRAM:
		rval = qlnx_nvram(ha, (qlnx_nvram_t *)data);
		break;

	case QLNX_RD_WR_REG:
		rval = qlnx_reg_rd_wr(ha, (qlnx_reg_rd_wr_t *)data);
		break;

	case QLNX_RD_WR_PCICFG:
		rval = qlnx_rd_wr_pci_config(ha, (qlnx_pcicfg_rd_wr_t *)data);
		break;

	case QLNX_MAC_ADDR:
		qlnx_mac_addr(ha, (qlnx_perm_mac_addr_t *)data);
		break;

	case QLNX_STORM_STATS:
		qlnx_storm_stats(ha, (qlnx_storm_stats_dump_t *)data);
		break;

	case QLNX_TRACE_SIZE:
		qlnx_get_trace_size(ha, (qlnx_trace_t *)data);
		break;

	case QLNX_TRACE:
		trace = (qlnx_trace_t *)data;

		for (i = 0; i < ha->cdev.num_hwfns; i++) {

			if (trace->size[i] && trace->cmd && trace->buffer[i])
				rval = qlnx_get_trace(ha, i, trace);

			if (rval)
				break;
		}
		break;

#ifdef QLNX_USER_LLDP
	case QLNX_SET_LLDP_TLVS:
		rval = qlnx_set_lldp_tlvx(ha, (qlnx_lldp_sys_tlvs_t *)data);
		break;
#endif /* #ifdef QLNX_USER_LLDP */

	default:
		rval = EINVAL;
		break;
	}

	return (rval);
}

