/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010-2015, 2018-2019 The Linux Foundation. All rights reserved.
 * Copyright (C) 2015 Linaro Ltd.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __QCOM_PAS_H
#define __QCOM_PAS_H

#include <linux/err.h>
#include <linux/types.h>

struct qcom_pas_context {
	struct device *dev;
	u32 pas_id;
	phys_addr_t mem_phys;
	size_t mem_size;
	void *ptr;
	dma_addr_t phys;
	ssize_t size;
	bool use_tzmem;
};

bool qcom_pas_is_available(void);
struct qcom_pas_context *devm_qcom_pas_context_alloc(struct device *dev,
						     u32 pas_id,
						     phys_addr_t mem_phys,
						     size_t mem_size);
int qcom_pas_init_image(u32 pas_id, const void *metadata, size_t size,
			struct qcom_pas_context *ctx);
struct resource_table *qcom_pas_get_rsc_table(struct qcom_pas_context *ctx,
					      void *input_rt, size_t input_rt_size,
					      size_t *output_rt_size);
int qcom_pas_mem_setup(u32 pas_id, phys_addr_t addr, phys_addr_t size);
int qcom_pas_auth_and_reset(u32 pas_id);
int qcom_pas_prepare_and_auth_reset(struct qcom_pas_context *ctx);
int qcom_pas_set_remote_state(u32 state, u32 pas_id);
int qcom_pas_shutdown(u32 pas_id);
bool qcom_pas_supported(u32 pas_id);
void qcom_pas_metadata_release(struct qcom_pas_context *ctx);

#endif /* __QCOM_PAS_H */
