/*
 * Copyright (c) 2018-2019 Cavium, Inc.
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
 * File : qlnx_rdma.c
 * Author: David C Somayajulu
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
#ifdef CONFIG_ECORE_SRIOV
#include "ecore_sriov.h"
#include "ecore_vf.h"
#endif
#ifdef CONFIG_ECORE_LL2
#include "ecore_ll2.h"
#endif
#ifdef CONFIG_ECORE_FCOE
#include "ecore_fcoe.h"
#endif
#ifdef CONFIG_ECORE_ISCSI
#include "ecore_iscsi.h"
#endif
#include "ecore_mcp.h"
#include "ecore_hw_defs.h"
#include "mcp_public.h"

#ifdef CONFIG_ECORE_RDMA
#include "ecore_rdma.h"
#endif

#ifdef CONFIG_ECORE_ROCE
#include "ecore_roce.h"
#endif

#ifdef CONFIG_ECORE_IWARP
#include "ecore_iwarp.h"
#endif

#include "ecore_iro.h"
#include "nvm_cfg.h"
#include "ecore_dev_api.h"
#include "ecore_dbg_fw_funcs.h"

#include "qlnx_ioctl.h"
#include "qlnx_def.h"
#include "qlnx_rdma.h"
#include "qlnx_ver.h"
#include <sys/smp.h>

struct mtx qlnx_rdma_dev_lock;
struct qlnx_rdma_if *qlnx_rdma_if = NULL;

qlnx_host_t *qlnx_host_list = NULL;

void
qlnx_rdma_init(void)
{
	if (!mtx_initialized(&qlnx_rdma_dev_lock)) {
		mtx_init(&qlnx_rdma_dev_lock, "qlnx_rdma_dev_lock", NULL, MTX_DEF);
	}
	return;
}

void
qlnx_rdma_deinit(void)
{
	if (mtx_initialized(&qlnx_rdma_dev_lock) && (qlnx_host_list == NULL)) {
		mtx_destroy(&qlnx_rdma_dev_lock);
	}
	return;
}

static void
_qlnx_rdma_dev_add(struct qlnx_host *ha)
{
	QL_DPRINT12(ha, "enter ha = %p qlnx_rdma_if = %p\n", ha, qlnx_rdma_if);

	if (qlnx_rdma_if == NULL) 
		return;

	if (ha->personality != ECORE_PCI_ETH_IWARP && 
		ha->personality != ECORE_PCI_ETH_ROCE)
		return;

	ha->qlnx_rdma = qlnx_rdma_if->add(ha);

	QL_DPRINT12(ha, "exit (ha = %p, qlnx_rdma = %p)\n", ha, ha->qlnx_rdma);
	return;
}

void
qlnx_rdma_dev_add(struct qlnx_host *ha)
{
	QL_DPRINT12(ha, "enter ha = %p\n", ha);

	if (ha->personality != ECORE_PCI_ETH_IWARP &&
			ha->personality != ECORE_PCI_ETH_ROCE)
		return;

	mtx_lock(&qlnx_rdma_dev_lock);

	if (qlnx_host_list == NULL) {
		qlnx_host_list = ha;
		ha->next = NULL;
	} else {
		ha->next = qlnx_host_list;
		qlnx_host_list = ha;
	}

	mtx_unlock(&qlnx_rdma_dev_lock);

	_qlnx_rdma_dev_add(ha);
	
	QL_DPRINT12(ha, "exit (%p)\n", ha);

	return;
}

static int
_qlnx_rdma_dev_remove(struct qlnx_host *ha)
{
	int ret = 0;

	QL_DPRINT12(ha, "enter ha = %p qlnx_rdma_if = %p\n", ha, qlnx_rdma_if);

	if (qlnx_rdma_if == NULL)
		return (ret);

	if (ha->personality != ECORE_PCI_ETH_IWARP &&
		ha->personality != ECORE_PCI_ETH_ROCE)
		return (ret);

	ret = qlnx_rdma_if->remove(ha, ha->qlnx_rdma);

	QL_DPRINT12(ha, "exit ha = %p qlnx_rdma_if = %p\n", ha, qlnx_rdma_if);
	return (ret);
}

int
qlnx_rdma_dev_remove(struct qlnx_host *ha)
{
	int ret = 0;
	qlnx_host_t *ha_prev;
	qlnx_host_t *ha_cur;

	QL_DPRINT12(ha, "enter ha = %p\n", ha);

	if ((qlnx_host_list == NULL) || (ha == NULL))
		return (ret);

	if (ha->personality != ECORE_PCI_ETH_IWARP &&
		ha->personality != ECORE_PCI_ETH_ROCE)
		return (ret);

	ret = _qlnx_rdma_dev_remove(ha);

	if (ret)
		return (ret);

	mtx_lock(&qlnx_rdma_dev_lock);

	if (qlnx_host_list == ha) {
		qlnx_host_list = ha->next;
		ha->next = NULL;
		mtx_unlock(&qlnx_rdma_dev_lock);
		QL_DPRINT12(ha, "exit0 ha = %p\n", ha);
		return (ret);
	}

	ha_prev = ha_cur = qlnx_host_list;

	while ((ha_cur != ha) && (ha_cur != NULL)) {
		ha_prev = ha_cur;
		ha_cur = ha_cur->next;
	}

	if (ha_cur == ha) {
		ha_prev = ha->next;
		ha->next = NULL;
	}

	mtx_unlock(&qlnx_rdma_dev_lock);

	QL_DPRINT12(ha, "exit1 ha = %p\n", ha);
	return (ret);
}

int
qlnx_rdma_register_if(qlnx_rdma_if_t *rdma_if)
{
	qlnx_host_t *ha;

	if (mtx_initialized(&qlnx_rdma_dev_lock)) {

		mtx_lock(&qlnx_rdma_dev_lock);
		qlnx_rdma_if = rdma_if;

		ha = qlnx_host_list;

		while (ha != NULL) {
			_qlnx_rdma_dev_add(ha);
			ha = ha->next;
		}

		mtx_unlock(&qlnx_rdma_dev_lock);

		return (0);
	}

	return (-1);
}

int
qlnx_rdma_deregister_if(qlnx_rdma_if_t *rdma_if)
{
	int ret = 0;
	qlnx_host_t *ha;

	printf("%s: enter rdma_if = %p\n", __func__, rdma_if);

        if (mtx_initialized(&qlnx_rdma_dev_lock)) {

                mtx_lock(&qlnx_rdma_dev_lock);

		ha = qlnx_host_list;

		while (ha != NULL) {

                	mtx_unlock(&qlnx_rdma_dev_lock);

			if (ha->dbg_level & 0xF000)
				ret = EBUSY;
			else
				ret = _qlnx_rdma_dev_remove(ha);

        		device_printf(ha->pci_dev, "%s [%d]: ret = 0x%x\n",
				__func__, __LINE__, ret);
			if (ret)
				return (ret);
			
                	mtx_lock(&qlnx_rdma_dev_lock);

			ha->qlnx_rdma = NULL;

			ha = ha->next;
		}

		if (!ret)
			qlnx_rdma_if = NULL;

                mtx_unlock(&qlnx_rdma_dev_lock);

        }
	printf("%s: exit rdma_if = %p\n", __func__, rdma_if);

        return (ret);
}


void
qlnx_rdma_dev_open(struct qlnx_host *ha)
{
	QL_DPRINT12(ha, "enter ha = %p qlnx_rdma_if = %p\n", ha, qlnx_rdma_if);

	if (qlnx_rdma_if == NULL)
		return;

	if (ha->personality != ECORE_PCI_ETH_IWARP && 
		ha->personality != ECORE_PCI_ETH_ROCE)
		return;

	qlnx_rdma_if->notify(ha, ha->qlnx_rdma, QLNX_ETHDEV_UP);

	QL_DPRINT12(ha, "exit ha = %p qlnx_rdma_if = %p\n", ha, qlnx_rdma_if);
	return;
}


void
qlnx_rdma_dev_close(struct qlnx_host *ha)
{
	QL_DPRINT12(ha, "enter ha = %p qlnx_rdma_if = %p\n", ha, qlnx_rdma_if);

	if (qlnx_rdma_if == NULL)
		return;

	if (ha->personality != ECORE_PCI_ETH_IWARP && 
		ha->personality != ECORE_PCI_ETH_ROCE)
		return;

	qlnx_rdma_if->notify(ha, ha->qlnx_rdma, QLNX_ETHDEV_DOWN);

	QL_DPRINT12(ha, "exit ha = %p qlnx_rdma_if = %p\n", ha, qlnx_rdma_if);
	return;
}

int
qlnx_rdma_get_num_irqs(struct qlnx_host *ha)
{
        return (QLNX_NUM_CNQ + ecore_rdma_get_sb_id(&ha->cdev.hwfns[0], 0) + 2);
}


