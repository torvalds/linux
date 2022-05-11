/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _QCOM_RAMDUMP_HEADER
#define _QCOM_RAMDUMP_HEADER
#include <linux/kernel.h>
#include <linux/firmware.h>

struct device;

struct qcom_dump_segment {
	struct list_head node;
	dma_addr_t da;
	void *va;
	size_t size;
};

#if IS_ENABLED(CONFIG_QCOM_RAMDUMP)
extern void *qcom_create_ramdump_device(const char *dev_name, struct device *parent);
extern void qcom_destroy_ramdump_device(void *dev);
extern int qcom_elf_dump(struct list_head *segs, struct device *dev, unsigned char class);
extern int qcom_dump(struct list_head *head, struct device *dev);
extern int qcom_fw_elf_dump(struct firmware *fw, struct device *dev);
extern bool dump_enabled(void);
#else
static inline void *qcom_create_ramdump_device(const char *dev_name,
		struct device *parent)
{
	return NULL;
}
static inline void qcom_destroy_ramdump_device(void *dev)
{
}
static inline int qcom_elf_dump(struct list_head *segs, struct device *dev, unsigned char class)
{
	return -ENODEV;
}
static inline int qcom_dump(struct list_head *head, struct device *dev)
{
	return -ENODEV;
}
static inline int qcom_fw_elf_dump(struct firmware *fw, struct device *dev)
{
	return -ENODEV;
}
static inline bool dump_enabled(void)
{
	return false;
}
#endif /* CONFIG_QCOM_RAMDUMP */

#endif
