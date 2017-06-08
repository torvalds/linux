#ifndef __QCOM_SMEM_H__
#define __QCOM_SMEM_H__

#define QCOM_SMEM_HOST_ANY -1

int qcom_smem_alloc(unsigned host, unsigned item, size_t size);
void *qcom_smem_get(unsigned host, unsigned item, size_t *size);

int qcom_smem_get_free_space(unsigned host);

#endif
