/*
 * Copyright © 2015 Intel Corporation.
 *
 * Authors: David Woodhouse <David.Woodhouse@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __INTEL_SVM_H__
#define __INTEL_SVM_H__

#ifdef CONFIG_INTEL_IOMMU_SVM

struct device;

/**
 * intel_svm_bind_mm() - Bind the current process to a PASID
 * @dev:	Device to be granted acccess
 * @pasid:	Address for allocated PASID
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
extern int intel_svm_bind_mm(struct device *dev, int *pasid);

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

#else /* CONFIG_INTEL_IOMMU_SVM */

static inline int intel_svm_bind_mm(struct device *dev, int *pasid)
{
	return -ENOSYS;
}

static inline int intel_svm_unbind_mm(struct device *dev, int pasid)
{
	BUG();
}
#endif /* CONFIG_INTEL_IOMMU_SVM */

#define intel_svm_available(dev) (!intel_svm_bind_mm((dev), NULL))

#endif /* __INTEL_SVM_H__ */
