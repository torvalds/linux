/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019, 2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QTEE_SHMBRIDGE_H__
#define __QTEE_SHMBRIDGE_H__

/* VMID and permission definitions */
#include <soc/qcom/secure_buffer.h>

/**
 * struct qtee_shm - info of shared memory allocated from the default bridge
 * @ paddr: physical address of the shm allocated from the default bridge
 * @ vaddr: virtual address of the shm
 * @ size: size of the shm
 */
struct qtee_shm {
	phys_addr_t paddr;
	void *vaddr;
	size_t size;
};

/**
 * Check whether shmbridge mechanism is enabled in HYP or not
 *
 * return true when enabled, false when not enabled
 */
bool qtee_shmbridge_is_enabled(void);

/**
 * Check whether a bridge starting from paddr exists
 *
 * @ [IN] paddr: physical addr of the buffer
 *
 * return 0 or -EEXIST
 */
int32_t qtee_shmbridge_query(phys_addr_t paddr);

/**
 * Register paddr & size as a bridge, get bridge handle
 *
 * @ [IN] paddr: physical addr of the buffer to be turned into bridge
 * @ [IN] size: size of the bridge
 * @ [IN] ns_vmid_list: non-secure vmids array
 * @ [IN] ns_vm_perm_list: NS VM permission array
 * @ [IN] ns_vmid_num: number of NS VMIDs (at most 4)
 * @ [IN] tz_perm: TZ permission
 * @ [OUT] *handle: output shmbridge handle
 *
 * return success or error
 */
int32_t qtee_shmbridge_register(
		phys_addr_t paddr,
		size_t size,
		uint32_t *ns_vmid_list,
		uint32_t *ns_vm_perm_list,
		uint32_t ns_vmid_num,
		uint32_t tz_perm,
		uint64_t *handle);

/**
 * Deregister bridge
 *
 * @ [IN] handle: shmbridge handle
 *
 * return success or error
 */
int32_t qtee_shmbridge_deregister(uint64_t handle);

/**
 * Sub-allocate from default kernel bridge created by shmb driver
 *
 * @ [IN] size: size of the buffer to be sub-allocated from the bridge
 * @ [OUT] *shm: output qtee_shm structure with buffer paddr, vaddr and
 *         size; returns ERR_PTR or NULL otherwise
 *
 * return success or error
 *
 * Note: This will allocate a cached buffer, so after a client allocates
 *       a bridge buffer, it need to first flush cache with
 *       "qtee_shmbridge_flush_shm_buf" before invoke scm_call to TZ,
 *       and then invalidate cache with "qtee_shmbridge_inv_shm_buf"
 *       after scm_call return.
 */
int32_t qtee_shmbridge_allocate_shm(size_t size, struct qtee_shm *shm);

/*
 * Free buffer that is sub-allocated from default kernel bridge
 *
 * @ [IN] shm: qtee_shm structure to be freed
 *
 */
void qtee_shmbridge_free_shm(struct qtee_shm *shm);

/*
 * cache clean operation for buffer sub-allocated from default bridge
 *
 * @ [IN] shm: qtee_shm, its cache to be cleaned
 *
 */
void qtee_shmbridge_flush_shm_buf(struct qtee_shm *shm);

/*
 * cache invalidation operation for buffer sub-allocated from default bridge
 *
 * @ [IN] shm: qtee_shm, its cache to be invalidated
 *
 */
void qtee_shmbridge_inv_shm_buf(struct qtee_shm *shm);

#endif /*__QTEE_SHMBRIDGE_H__*/
