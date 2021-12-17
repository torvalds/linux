/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef _I915_PXP_TEE_INTERFACE_H_
#define _I915_PXP_TEE_INTERFACE_H_

#include <linux/mutex.h>
#include <linux/device.h>

/**
 * struct i915_pxp_component_ops - ops for PXP services.
 * @owner: Module providing the ops
 * @send: sends data to PXP
 * @receive: receives data from PXP
 */
struct i915_pxp_component_ops {
	/**
	 * @owner: owner of the module provding the ops
	 */
	struct module *owner;

	int (*send)(struct device *dev, const void *message, size_t size);
	int (*recv)(struct device *dev, void *buffer, size_t size);
};

/**
 * struct i915_pxp_component - Used for communication between i915 and TEE
 * drivers for the PXP services
 * @tee_dev: device that provide the PXP service from TEE Bus.
 * @pxp_ops: Ops implemented by TEE driver, used by i915 driver.
 */
struct i915_pxp_component {
	struct device *tee_dev;
	const struct i915_pxp_component_ops *ops;

	/* To protect the above members. */
	struct mutex mutex;
};

#endif /* _I915_TEE_PXP_INTERFACE_H_ */
