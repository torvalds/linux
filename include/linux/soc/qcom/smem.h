/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __QCOM_SMEM_H__
#define __QCOM_SMEM_H__

#define QCOM_SMEM_HOST_ANY -1

int qcom_smem_alloc(unsigned host, unsigned item, size_t size);
#if IS_ENABLED(CONFIG_QCOM_SMEM)
void *qcom_smem_get(unsigned int host, unsigned int item, size_t *size);
#else
static inline void *qcom_smem_get(unsigned int host, unsigned int item, size_t *size)
{
	return ERR_PTR(ENODEV);
}
#endif
int qcom_smem_get_free_space(unsigned host);

phys_addr_t qcom_smem_virt_to_phys(void *p);

int qcom_smem_bust_hwspin_lock_by_host(unsigned int host);

#endif
