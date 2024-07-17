/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __SELFTESTS_GIC_V3_ITS_H__
#define __SELFTESTS_GIC_V3_ITS_H__

#include <linux/sizes.h>

void its_init(vm_paddr_t coll_tbl, size_t coll_tbl_sz,
	      vm_paddr_t device_tbl, size_t device_tbl_sz,
	      vm_paddr_t cmdq, size_t cmdq_size);

void its_send_mapd_cmd(void *cmdq_base, u32 device_id, vm_paddr_t itt_base,
		       size_t itt_size, bool valid);
void its_send_mapc_cmd(void *cmdq_base, u32 vcpu_id, u32 collection_id, bool valid);
void its_send_mapti_cmd(void *cmdq_base, u32 device_id, u32 event_id,
			u32 collection_id, u32 intid);
void its_send_invall_cmd(void *cmdq_base, u32 collection_id);

#endif // __SELFTESTS_GIC_V3_ITS_H__
