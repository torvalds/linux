/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __QCOM_MDT_LOADER_H__
#define __QCOM_MDT_LOADER_H__

#include <linux/types.h>

#define QCOM_MDT_TYPE_MASK	(7 << 24)
#define QCOM_MDT_TYPE_HASH	(2 << 24)
#define QCOM_MDT_RELOCATABLE	BIT(27)

struct device;
struct firmware;

struct qcom_mdt_metadata {
	void *buf;
	dma_addr_t buf_phys;
	size_t size;
};

#if IS_ENABLED(CONFIG_QCOM_MDT_LOADER)

ssize_t qcom_mdt_get_size(const struct firmware *fw);
int qcom_mdt_load(struct device *dev, const struct firmware *fw,
		  const char *fw_name, int pas_id, void *mem_region,
		  phys_addr_t mem_phys, size_t mem_size,
		  phys_addr_t *reloc_base);

int qcom_mdt_load_no_init(struct device *dev, const struct firmware *fw,
			  const char *fw_name, int pas_id, void *mem_region,
			  phys_addr_t mem_phys, size_t mem_size,
			  phys_addr_t *reloc_base);
void *qcom_mdt_read_metadata(struct device *dev, const struct firmware *fw,
		const char *firmware, size_t *data_len, bool dma_phys_below_32b,
		dma_addr_t *metadata_phys);
int qcom_mdt_load_no_free(struct device *dev, const struct firmware *fw, const char *firmware,
		  int pas_id, void *mem_region, phys_addr_t mem_phys, size_t mem_size,
		  phys_addr_t *reloc_base, bool dma_phys_below_32b,
		  struct qcom_mdt_metadata *metadata);
void qcom_mdt_free_metadata(struct device *dev, int pas_id, struct qcom_mdt_metadata *mdata,
			    bool dma_phys_below_32b, int err);

#else /* !IS_ENABLED(CONFIG_QCOM_MDT_LOADER) */

static inline ssize_t qcom_mdt_get_size(const struct firmware *fw)
{
	return -ENODEV;
}

static inline int qcom_mdt_load(struct device *dev, const struct firmware *fw,
				const char *fw_name, int pas_id,
				void *mem_region, phys_addr_t mem_phys,
				size_t mem_size, phys_addr_t *reloc_base)
{
	return -ENODEV;
}

static inline int qcom_mdt_load_no_init(struct device *dev,
					const struct firmware *fw,
					const char *fw_name, int pas_id,
					void *mem_region, phys_addr_t mem_phys,
					size_t mem_size,
					phys_addr_t *reloc_base)
{
	return -ENODEV;
}

static inline void *qcom_mdt_read_metadata(struct device *dev,
					   const struct firmware *fw,
					   const char *firmware,
					   size_t *data_len,
					   bool dma_phys_below_32b,
					   dma_addr_t *metadata_phys)
{
	return NULL;
}

static inline int qcom_mdt_load_no_free(struct device *dev,
					const struct firmware *fw,
					const char *firmware,
					int pas_id, void *mem_region,
					phys_addr_t mem_phys, size_t mem_size,
					phys_addr_t *reloc_base,
					bool dma_phys_below_32b,
					struct qcom_mdt_metadata *metadata)
{
	return -ENODEV;
}

static inline void qcom_mdt_free_metadata(struct device *dev,
					  int pas_id,
					  struct qcom_mdt_metadata *mdata,
					  bool dma_phys_below_32b, int err)
{
	return;
}

#endif /* !IS_ENABLED(CONFIG_QCOM_MDT_LOADER) */

#endif
