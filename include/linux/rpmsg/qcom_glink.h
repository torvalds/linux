/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _LINUX_RPMSG_QCOM_GLINK_H
#define _LINUX_RPMSG_QCOM_GLINK_H

#include <linux/device.h>

struct qcom_glink;
struct qcom_glink_mem_entry;

#if IS_ENABLED(CONFIG_RPMSG_QCOM_GLINK)
void qcom_glink_ssr_notify(const char *ssr_name);
struct qcom_glink_mem_entry *
qcom_glink_mem_entry_init(struct device *dev, void *va, dma_addr_t dma, size_t len, u32 da);
void qcom_glink_mem_entry_free(struct qcom_glink_mem_entry *mem);
#else
static inline void qcom_glink_ssr_notify(const char *ssr_name) {}
static inline struct qcom_glink_mem_entry *
qcom_glink_mem_entry_init(struct device *dev, void *va, dma_addr_t dma, size_t len, u32 da)
{
	return NULL;
}
static inline void qcom_glink_mem_entry_free(struct qcom_glink_mem_entry *mem) {}
#endif

#if IS_ENABLED(CONFIG_RPMSG_QCOM_GLINK_SMEM)

struct qcom_glink *qcom_glink_smem_register(struct device *parent,
					    struct device_node *node);
void qcom_glink_smem_unregister(struct qcom_glink *glink);
int qcom_glink_smem_start(struct qcom_glink *glink);
bool qcom_glink_is_wakeup(bool reset);
void qcom_glink_early_ssr_notify(void *data);

#else

static inline struct qcom_glink *
qcom_glink_smem_register(struct device *parent,
			 struct device_node *node)
{
	return NULL;
}

static inline void qcom_glink_smem_unregister(struct qcom_glink *glink) {}
static inline void qcom_glink_early_ssr_notify(void *data) {}

static inline int qcom_glink_smem_start(struct qcom_glink *glink)
{
	return -ENXIO;
}

static inline bool qcom_glink_is_wakeup(bool reset)
{
	return false;
}
#endif

#if IS_ENABLED(CONFIG_RPMSG_QCOM_GLINK_SPSS)

struct qcom_glink *qcom_glink_spss_register(struct device *parent,
					    struct device_node *node);
void qcom_glink_spss_unregister(struct qcom_glink *glink);

#else

static inline struct qcom_glink *
qcom_glink_spss_register(struct device *parent,
			 struct device_node *node)
{
	return NULL;
}

static inline void qcom_glink_spss_unregister(struct qcom_glink *glink) {}

#endif

#endif
