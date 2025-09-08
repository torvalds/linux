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
	IOMMUFD_OBJ_HW_QUEUE,
#ifdef CONFIG_IOMMUFD_TEST
	IOMMUFD_OBJ_SELFTEST,
#endif
	IOMMUFD_OBJ_MAX,
};

/* Base struct for all objects with a userspace ID handle. */
struct iommufd_object {
	/*
	 * Destroy will sleep and wait for wait_cnt to go to zero. This allows
	 * concurrent users of the ID to reliably avoid causing a spurious
	 * destroy failure. Incrementing this count should either be short
	 * lived or be revoked and blocked during pre_destroy().
	 */
	refcount_t wait_cnt;
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

	enum iommu_viommu_type type;
};

struct iommufd_vdevice {
	struct iommufd_object obj;
	struct iommufd_viommu *viommu;
	struct iommufd_device *idev;

	/*
	 * Virtual device ID per vIOMMU, e.g. vSID of ARM SMMUv3, vDeviceID of
	 * AMD IOMMU, and vRID of Intel VT-d
	 */
	u64 virt_id;

	/* Clean up all driver-specific parts of an iommufd_vdevice */
	void (*destroy)(struct iommufd_vdevice *vdev);
};

struct iommufd_hw_queue {
	struct iommufd_object obj;
	struct iommufd_viommu *viommu;
	struct iommufd_access *access;

	u64 base_addr; /* in guest physical address space */
	size_t length;

	enum iommu_hw_queue_type type;

	/* Clean up all driver-specific parts of an iommufd_hw_queue */
	void (*destroy)(struct iommufd_hw_queue *hw_queue);
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
 * @vdevice_size: Size of the driver-defined vDEVICE structure per this vIOMMU
 * @vdevice_init: Initialize the driver-level structure of a vDEVICE object, or
 *                related HW procedure. @vdev is already initialized by iommufd
 *                core: vdev->dev and vdev->viommu pointers; vdev->id carries a
 *                per-vIOMMU virtual ID (refer to struct iommu_vdevice_alloc in
 *                include/uapi/linux/iommufd.h)
 *                If driver has a deinit function to revert what vdevice_init op
 *                does, it should set it to the @vdev->destroy function pointer
 * @get_hw_queue_size: Get the size of a driver-defined HW queue structure for a
 *                     given @viommu corresponding to @queue_type. Driver should
 *                     return 0 if HW queue aren't supported accordingly. It is
 *                     required for driver to use the HW_QUEUE_STRUCT_SIZE macro
 *                     to sanitize the driver-level HW queue structure related
 *                     to the core one
 * @hw_queue_init_phys: Initialize the driver-level structure of a HW queue that
 *                      is initialized with its core-level structure that holds
 *                      all the info about a guest queue memory.
 *                      Driver providing this op indicates that HW accesses the
 *                      guest queue memory via physical addresses.
 *                      @index carries the logical HW QUEUE ID per vIOMMU in a
 *                      guest VM, for a multi-queue model. @base_addr_pa carries
 *                      the physical location of the guest queue
 *                      If driver has a deinit function to revert what this op
 *                      does, it should set it to the @hw_queue->destroy pointer
 */
struct iommufd_viommu_ops {
	void (*destroy)(struct iommufd_viommu *viommu);
	struct iommu_domain *(*alloc_domain_nested)(
		struct iommufd_viommu *viommu, u32 flags,
		const struct iommu_user_data *user_data);
	int (*cache_invalidate)(struct iommufd_viommu *viommu,
				struct iommu_user_data_array *array);
	const size_t vdevice_size;
	int (*vdevice_init)(struct iommufd_vdevice *vdev);
	size_t (*get_hw_queue_size)(struct iommufd_viommu *viommu,
				    enum iommu_hw_queue_type queue_type);
	/* AMD's HW will add hw_queue_init simply using @hw_queue->base_addr */
	int (*hw_queue_init_phys)(struct iommufd_hw_queue *hw_queue, u32 index,
				  phys_addr_t base_addr_pa);
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

static inline int iommufd_access_rw(struct iommufd_access *access,
				    unsigned long iova, void *data, size_t len,
				    unsigned int flags)
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
int _iommufd_object_depend(struct iommufd_object *obj_dependent,
			   struct iommufd_object *obj_depended);
void _iommufd_object_undepend(struct iommufd_object *obj_dependent,
			      struct iommufd_object *obj_depended);
int _iommufd_alloc_mmap(struct iommufd_ctx *ictx, struct iommufd_object *owner,
			phys_addr_t mmio_addr, size_t length,
			unsigned long *offset);
void _iommufd_destroy_mmap(struct iommufd_ctx *ictx,
			   struct iommufd_object *owner, unsigned long offset);
struct device *iommufd_vdevice_to_device(struct iommufd_vdevice *vdev);
struct device *iommufd_viommu_find_dev(struct iommufd_viommu *viommu,
				       unsigned long vdev_id);
int iommufd_viommu_get_vdev_id(struct iommufd_viommu *viommu,
			       struct device *dev, unsigned long *vdev_id);
int iommufd_viommu_report_event(struct iommufd_viommu *viommu,
				enum iommu_veventq_type type, void *event_data,
				size_t data_len);
#else /* !CONFIG_IOMMUFD_DRIVER_CORE */
static inline int _iommufd_object_depend(struct iommufd_object *obj_dependent,
					 struct iommufd_object *obj_depended)
{
	return -EOPNOTSUPP;
}

static inline void
_iommufd_object_undepend(struct iommufd_object *obj_dependent,
			 struct iommufd_object *obj_depended)
{
}

static inline int _iommufd_alloc_mmap(struct iommufd_ctx *ictx,
				      struct iommufd_object *owner,
				      phys_addr_t mmio_addr, size_t length,
				      unsigned long *offset)
{
	return -EOPNOTSUPP;
}

static inline void _iommufd_destroy_mmap(struct iommufd_ctx *ictx,
					 struct iommufd_object *owner,
					 unsigned long offset)
{
}

static inline struct device *
iommufd_vdevice_to_device(struct iommufd_vdevice *vdev)
{
	return NULL;
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

#define VIOMMU_STRUCT_SIZE(drv_struct, member)                                 \
	(sizeof(drv_struct) +                                                  \
	 BUILD_BUG_ON_ZERO(offsetof(drv_struct, member)) +                     \
	 BUILD_BUG_ON_ZERO(!__same_type(struct iommufd_viommu,                 \
					((drv_struct *)NULL)->member)))

#define VDEVICE_STRUCT_SIZE(drv_struct, member)                                \
	(sizeof(drv_struct) +                                                  \
	 BUILD_BUG_ON_ZERO(offsetof(drv_struct, member)) +                     \
	 BUILD_BUG_ON_ZERO(!__same_type(struct iommufd_vdevice,                \
					((drv_struct *)NULL)->member)))

#define HW_QUEUE_STRUCT_SIZE(drv_struct, member)                               \
	(sizeof(drv_struct) +                                                  \
	 BUILD_BUG_ON_ZERO(offsetof(drv_struct, member)) +                     \
	 BUILD_BUG_ON_ZERO(!__same_type(struct iommufd_hw_queue,               \
					((drv_struct *)NULL)->member)))

/*
 * Helpers for IOMMU driver to build/destroy a dependency between two sibling
 * structures created by one of the allocators above
 */
#define iommufd_hw_queue_depend(dependent, depended, member)                   \
	({                                                                     \
		int ret = -EINVAL;                                             \
									       \
		static_assert(__same_type(struct iommufd_hw_queue,             \
					  dependent->member));                 \
		static_assert(__same_type(typeof(*dependent), *depended));     \
		if (!WARN_ON_ONCE(dependent->member.viommu !=                  \
				  depended->member.viommu))                    \
			ret = _iommufd_object_depend(&dependent->member.obj,   \
						     &depended->member.obj);   \
		ret;                                                           \
	})

#define iommufd_hw_queue_undepend(dependent, depended, member)                 \
	({                                                                     \
		static_assert(__same_type(struct iommufd_hw_queue,             \
					  dependent->member));                 \
		static_assert(__same_type(typeof(*dependent), *depended));     \
		WARN_ON_ONCE(dependent->member.viommu !=                       \
			     depended->member.viommu);                         \
		_iommufd_object_undepend(&dependent->member.obj,               \
					 &depended->member.obj);               \
	})

/*
 * Helpers for IOMMU driver to alloc/destroy an mmapable area for a structure.
 *
 * To support an mmappable MMIO region, kernel driver must first register it to
 * iommufd core to allocate an @offset, during a driver-structure initialization
 * (e.g. viommu_init op). Then, it should report to user space this @offset and
 * the @length of the MMIO region for mmap syscall.
 */
static inline int iommufd_viommu_alloc_mmap(struct iommufd_viommu *viommu,
					    phys_addr_t mmio_addr,
					    size_t length,
					    unsigned long *offset)
{
	return _iommufd_alloc_mmap(viommu->ictx, &viommu->obj, mmio_addr,
				   length, offset);
}

static inline void iommufd_viommu_destroy_mmap(struct iommufd_viommu *viommu,
					       unsigned long offset)
{
	_iommufd_destroy_mmap(viommu->ictx, &viommu->obj, offset);
}
#endif
