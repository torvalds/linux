/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
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
extern int qcom_elf_dump(struct list_head *segs, struct device *dev);
extern int qcom_dump(struct list_head *head, struct device *dev);
extern int qcom_fw_elf_dump(struct firmware *fw, struct device *dev);
extern bool dump_enabled(void);
#else
static inline int qcom_elf_dump(struct list_head *segs, struct device *dev)
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
