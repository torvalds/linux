/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Intel Corporation
 * Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES
 */
#ifndef __LINUX_IOMMUFD_H
#define __LINUX_IOMMUFD_H

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/device.h>

struct iommufd_device;
struct iommufd_ctx;
struct file;

struct iommufd_device *iommufd_device_bind(struct iommufd_ctx *ictx,
					   struct device *dev, u32 *id);
void iommufd_device_unbind(struct iommufd_device *idev);

int iommufd_device_attach(struct iommufd_device *idev, u32 *pt_id);
void iommufd_device_detach(struct iommufd_device *idev);

enum {
	IOMMUFD_ACCESS_RW_READ = 0,
	IOMMUFD_ACCESS_RW_WRITE = 1 << 0,
	/* Set if the caller is in a kthread then rw will use kthread_use_mm() */
	IOMMUFD_ACCESS_RW_KTHREAD = 1 << 1,
};

void iommufd_ctx_get(struct iommufd_ctx *ictx);

#if IS_ENABLED(CONFIG_IOMMUFD)
struct iommufd_ctx *iommufd_ctx_from_file(struct file *file);
void iommufd_ctx_put(struct iommufd_ctx *ictx);
#else /* !CONFIG_IOMMUFD */
static inline struct iommufd_ctx *iommufd_ctx_from_file(struct file *file)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void iommufd_ctx_put(struct iommufd_ctx *ictx)
{
}
#endif /* CONFIG_IOMMUFD */
#endif
