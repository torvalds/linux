/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2015 Intel Corporation.
 *
 * Authors: David Woodhouse <David.Woodhouse@intel.com>
 */

#ifndef __INTEL_SVM_H__
#define __INTEL_SVM_H__

struct device;

struct svm_dev_ops {
	void (*fault_cb)(struct device *dev, int pasid, u64 address,
			 void *private, int rwxp, int response);
};

/* Values for rxwp in fault_cb callback */
#define SVM_REQ_READ	(1<<3)
#define SVM_REQ_WRITE	(1<<2)
#define SVM_REQ_EXEC	(1<<1)
#define SVM_REQ_PRIV	(1<<0)


/*
 * The SVM_FLAG_PRIVATE_PASID flag requests a PASID which is *not* the "main"
 * PASID for the current process. Even if a PASID already exists, a new one
 * will be allocated. And the PASID allocated with SVM_FLAG_PRIVATE_PASID
 * will not be given to subsequent callers. This facility allows a driver to
 * disambiguate between multiple device contexts which access the same MM,
 * if there is no other way to do so. It should be used sparingly, if at all.
 */
#define SVM_FLAG_PRIVATE_PASID		(1<<0)

/*
 * The SVM_FLAG_SUPERVISOR_MODE flag requests a PASID which can be used only
 * for access to kernel addresses. No IOTLB flushes are automatically done
 * for kernel mappings; it is valid only for access to the kernel's static
 * 1:1 mapping of physical memory — not to vmalloc or even module mappings.
 * A future API addition may permit the use of such ranges, by means of an
 * explicit IOTLB flush call (akin to the DMA API's unmap method).
 *
 * It is unlikely that we will ever hook into flush_tlb_kernel_range() to
 * do such IOTLB flushes automatically.
 */
#define SVM_FLAG_SUPERVISOR_MODE	(1<<1)

#ifdef CONFIG_INTEL_IOMMU_SVM

/**
 * intel_svm_bind_mm() - Bind the current process to a PASID
 * @dev:	Device to be granted access
 * @pasid:	Address for allocated PASID
 * @flags:	Flags. Later for requesting supervisor mode, etc.
 * @ops:	Callbacks to device driver
 *
 * This function attempts to enable PASID support for the given device.
 * If the @pasid argument is non-%NULL, a PASID is allocated for access
 * to the MM of the current process.
 *
 * By using a %NULL value for the @pasid argument, this function can
 * be used to simply validate that PASID support is available for the
 * given device — i.e. that it is behind an IOMMU which has the
 * requisite support, and is enabled.
 *
 * Page faults are handled transparently by the IOMMU code, and there
 * should be no need for the device driver to be involved. If a page
 * fault cannot be handled (i.e. is an invalid address rather than
 * just needs paging in), then the page request will be completed by
 * the core IOMMU code with appropriate status, and the device itself
 * can then report the resulting fault to its driver via whatever
 * mechanism is appropriate.
 *
 * Multiple calls from the same process may result in the same PASID
 * being re-used. A reference count is kept.
 */
extern int intel_svm_bind_mm(struct device *dev, int *pasid, int flags,
			     struct svm_dev_ops *ops);

/**
 * intel_svm_unbind_mm() - Unbind a specified PASID
 * @dev:	Device for which PASID was allocated
 * @pasid:	PASID value to be unbound
 *
 * This function allows a PASID to be retired when the device no
 * longer requires access to the address space of a given process.
 *
 * If the use count for the PASID in question reaches zero, the
 * PASID is revoked and may no longer be used by hardware.
 *
 * Device drivers are required to ensure that no access (including
 * page requests) is currently outstanding for the PASID in question,
 * before calling this function.
 */
extern int intel_svm_unbind_mm(struct device *dev, int pasid);

/**
 * intel_svm_is_pasid_valid() - check if pasid is valid
 * @dev:	Device for which PASID was allocated
 * @pasid:	PASID value to be checked
 *
 * This function checks if the specified pasid is still valid. A
 * valid pasid means the backing mm is still having a valid user.
 * For kernel callers init_mm is always valid. for other mm, if mm->mm_users
 * is non-zero, it is valid.
 *
 * returns -EINVAL if invalid pasid, 0 if pasid ref count is invalid
 * 1 if pasid is valid.
 */
extern int intel_svm_is_pasid_valid(struct device *dev, int pasid);

#else /* CONFIG_INTEL_IOMMU_SVM */

static inline int intel_svm_bind_mm(struct device *dev, int *pasid,
				    int flags, struct svm_dev_ops *ops)
{
	return -ENOSYS;
}

static inline int intel_svm_unbind_mm(struct device *dev, int pasid)
{
	BUG();
}

static int intel_svm_is_pasid_valid(struct device *dev, int pasid)
{
	return -EINVAL;
}
#endif /* CONFIG_INTEL_IOMMU_SVM */

#define intel_svm_available(dev) (!intel_svm_bind_mm((dev), NULL, 0, NULL))

#endif /* __INTEL_SVM_H__ */
