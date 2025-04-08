/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Intel Corporation
 * Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES
 */
#ifndef __LINUX_IOMMUFD_H
#define __LINUX_IOMMUFD_H

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/iommu.h>
#include <linux/refcount.h>
#include <linux/types.h>
#include <linux/xarray.h>
#include <uapi/linux/iommufd.h>

struct device;
struct file;
struct iommu_group;
struct iommu_user_data;
struct iommu_user_data_array;
struct iommufd_access;
struct iommufd_ctx;
struct iommufd_device;
struct iommufd_viommu_ops;
struct page;

enum iommufd_object_type {
	IOMMUFD_OBJ_NONE,
	IOMMUFD_OBJ_ANY = IOMMUFD_OBJ_NONE,
	IOMMUFD_OBJ_DEVICE,
	IOMMUFD_OBJ_HWPT_PAGING,
	IOMMUFD_OBJ_HWPT_NESTED,
	IOMMUFD_OBJ_IOAS,
	IOMMUFD_OBJ_ACCESS,
	IOMMUFD_OBJ_FAULT,
	IOMMUFD_OBJ_VIOMMU,
	IOMMUFD_OBJ_VDEVICE,
	IOMMUFD_OBJ_VEVENTQ,
#ifdef CONFIG_IOMMUFD_TEST
	IOMMUFD_OBJ_SELFTEST,
#endif
	IOMMUFD_OBJ_MAX,
};

/* Base struct for all objects with a userspace ID handle. */
struct iommufd_object {
	refcount_t shortterm_users;
	refcount_t users;
	enum iommufd_object_type type;
	unsigned int id;
};

struct iommufd_device *iommufd_device_bind(struct iommufd_ctx *ictx,
					   struct device *dev, u32 *id);
void iommufd_device_unbind(struct iommufd_device *idev);

int iommufd_device_attach(struct iommufd_device *idev, ioasid_t pasid,
			  u32 *pt_id);
int iommufd_device_replace(struct iommufd_device *idev, ioasid_t pasid,
			   u32 *pt_id);
void iommufd_device_detach(struct iommufd_device *idev, ioasid_t pasid);

struct iommufd_ctx *iommufd_device_to_ictx(struct iommufd_device *idev);
u32 iommufd_device_to_id(struct iommufd_device *idev);

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
iommufd_access_create(struct iommufd_ctx *ictx,
		      const struct iommufd_access_ops *ops, void *data, u32 *id);
void iommufd_access_destroy(struct iommufd_access *access);
int iommufd_access_attach(struct iommufd_access *access, u32 ioas_id);
int iommufd_access_replace(struct iommufd_access *access, u32 ioas_id);
void iommufd_access_detach(struct iommufd_access *access);

void iommufd_ctx_get(struct iommufd_ctx *ictx);

struct iommufd_viommu {
	struct iommufd_object obj;
	struct iommufd_ctx *ictx;
	struct iommu_device *iommu_dev;
	struct iommufd_hwpt_paging *hwpt;

	const struct iommufd_viommu_ops *ops;

	struct xarray vdevs;
	struct list_head veventqs;
	struct rw_semaphore veventqs_rwsem;

	unsigned int type;
};

/**
 * struct iommufd_viommu_ops - vIOMMU specific operations
 * @destroy: Clean up all driver-specific parts of an iommufd_viommu. The memory
 *           of the vIOMMU will be free-ed by iommufd core after calling this op
 * @alloc_domain_nested: Allocate a IOMMU_DOMAIN_NESTED on a vIOMMU that holds a
 *                       nesting parent domain (IOMMU_DOMAIN_PAGING). @user_data
 *                       must be defined in include/uapi/linux/iommufd.h.
 *                       It must fully initialize the new iommu_domain before
 *                       returning. Upon failure, ERR_PTR must be returned.
 * @cache_invalidate: Flush hardware cache used by a vIOMMU. It can be used for
 *                    any IOMMU hardware specific cache: TLB and device cache.
 *                    The @array passes in the cache invalidation requests, in
 *                    form of a driver data structure. A driver must update the
 *                    array->entry_num to report the number of handled requests.
 *                    The data structure of the array entry must be defined in
 *                    include/uapi/linux/iommufd.h
 */
struct iommufd_viommu_ops {
	void (*destroy)(struct iommufd_viommu *viommu);
	struct iommu_domain *(*alloc_domain_nested)(
		struct iommufd_viommu *viommu, u32 flags,
		const struct iommu_user_data *user_data);
	int (*cache_invalidate)(struct iommufd_viommu *viommu,
				struct iommu_user_data_array *array);
};

#if IS_ENABLED(CONFIG_IOMMUFD)
struct iommufd_ctx *iommufd_ctx_from_file(struct file *file);
struct iommufd_ctx *iommufd_ctx_from_fd(int fd);
void iommufd_ctx_put(struct iommufd_ctx *ictx);
bool iommufd_ctx_has_group(struct iommufd_ctx *ictx, struct iommu_group *group);

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

#if IS_ENABLED(CONFIG_IOMMUFD_DRIVER_CORE)
struct iommufd_object *_iommufd_object_alloc(struct iommufd_ctx *ictx,
					     size_t size,
					     enum iommufd_object_type type);
struct device *iommufd_viommu_find_dev(struct iommufd_viommu *viommu,
				       unsigned long vdev_id);
int iommufd_viommu_get_vdev_id(struct iommufd_viommu *viommu,
			       struct device *dev, unsigned long *vdev_id);
int iommufd_viommu_report_event(struct iommufd_viommu *viommu,
				enum iommu_veventq_type type, void *event_data,
				size_t data_len);
#else /* !CONFIG_IOMMUFD_DRIVER_CORE */
static inline struct iommufd_object *
_iommufd_object_alloc(struct iommufd_ctx *ictx, size_t size,
		      enum iommufd_object_type type)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline struct device *
iommufd_viommu_find_dev(struct iommufd_viommu *viommu, unsigned long vdev_id)
{
	return NULL;
}

static inline int iommufd_viommu_get_vdev_id(struct iommufd_viommu *viommu,
					     struct device *dev,
					     unsigned long *vdev_id)
{
	return -ENOENT;
}

static inline int iommufd_viommu_report_event(struct iommufd_viommu *viommu,
					      enum iommu_veventq_type type,
					      void *event_data, size_t data_len)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_IOMMUFD_DRIVER_CORE */

/*
 * Helpers for IOMMU driver to allocate driver structures that will be freed by
 * the iommufd core. The free op will be called prior to freeing the memory.
 */
#define iommufd_viommu_alloc(ictx, drv_struct, member, viommu_ops)             \
	({                                                                     \
		drv_struct *ret;                                               \
									       \
		static_assert(__same_type(struct iommufd_viommu,               \
					  ((drv_struct *)NULL)->member));      \
		static_assert(offsetof(drv_struct, member.obj) == 0);          \
		ret = (drv_struct *)_iommufd_object_alloc(                     \
			ictx, sizeof(drv_struct), IOMMUFD_OBJ_VIOMMU);         \
		if (!IS_ERR(ret))                                              \
			ret->member.ops = viommu_ops;                          \
		ret;                                                           \
	})
#endif
