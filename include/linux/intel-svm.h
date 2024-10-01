/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2015 Intel Corporation.
 *
 * Authors: David Woodhouse <David.Woodhouse@intel.com>
 */

#ifndef __INTEL_SVM_H__
#define __INTEL_SVM_H__

/* Page Request Queue depth */
#define PRQ_ORDER	4
#define PRQ_RING_MASK	((0x1000 << PRQ_ORDER) - 0x20)
#define PRQ_DEPTH	((0x1000 << PRQ_ORDER) >> 5)

#endif /* __INTEL_SVM_H__ */
