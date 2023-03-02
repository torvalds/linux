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

struct device;
struct iommufd_device;
struct page;
struct iommufd_ctx;
struct iommufd_access;
struct file;

struct iommufd_device *iommufd_device_bind(struct iommufd_ctx *ictx,
					   struct device *dev, u32 *id);
void iommufd_device_unbind(struct iommufd_device *idev);

int iommufd_device_attach(struct iommufd_device *idev, u32 *pt_id);
void iommufd_device_detach(struct iommufd_device *idev);

struct iommufd_access_ops {
	u8 needs_pin_pages : 1;
	void (*unmap)(void *data, unsigned long iova, unsigned long length);
};

enum {
	IOMMUFD_ACCESS_RW_READ = 0,
	IOMMUFD_ACCESS_RW_WRITE = 1 << 0,
	/* Set if the caller is in a kthread then rw will use kthread_use_mm() */
	IOMMUFD_ACCESS_RW_KTHREAD = 1 << 1,

	/* Only for use by selftest */
	__IOMMUFD_ACCESS_RW_SLOW_PATH = 1 << 2,
};

struct iommufd_access *
iommufd_access_create(struct iommufd_ctx *ictx, u32 ioas_id,
		      const struct iommufd_access_ops *ops, void *data);
void iommufd_access_destroy(struct iommufd_access *access);

void iommufd_ctx_get(struct iommufd_ctx *ictx);

#if IS_ENABLED(CONFIG_IOMMUFD)
struct iommufd_ctx *iommufd_ctx_from_file(struct file *file);
void iommufd_ctx_put(struct iommufd_ctx *ictx);

int iommufd_access_pin_pages(struct iommufd_access *access, unsigned long iova,
			     unsigned long length, struct page **out_pages,
			     unsigned int flags);
void iommufd_access_unpin_pages(struct iommufd_access *access,
				unsigned long iova, unsigned long length);
int iommufd_access_rw(struct iommufd_access *access, unsigned long iova,
		      void *data, size_t len, unsigned int flags);
int iommufd_vfio_compat_ioas_get_id(struct iommufd_ctx *ictx, u32 *out_ioas_id);
int iommufd_vfio_compat_ioas_create(struct iommufd_ctx *ictx);
int iommufd_vfio_compat_set_no_iommu(struct iommufd_ctx *ictx);
#else /* !CONFIG_IOMMUFD */
static inline struct iommufd_ctx *iommufd_ctx_from_file(struct file *file)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void iommufd_ctx_put(struct iommufd_ctx *ictx)
{
}

static inline int iommufd_access_pin_pages(struct iommufd_access *access,
					   unsigned long iova,
					   unsigned long length,
					   struct page **out_pages,
					   unsigned int flags)
{
	return -EOPNOTSUPP;
}

static inline void iommufd_access_unpin_pages(struct iommufd_access *access,
					      unsigned long iova,
					      unsigned long length)
{
}

static inline int iommufd_access_rw(struct iommufd_access *access, unsigned long iova,
		      void *data, size_t len, unsigned int flags)
{
	return -EOPNOTSUPP;
}

static inline int iommufd_vfio_compat_ioas_create(struct iommufd_ctx *ictx)
{
	return -EOPNOTSUPP;
}

static inline int iommufd_vfio_compat_set_no_iommu(struct iommufd_ctx *ictx)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_IOMMUFD */
#endif
