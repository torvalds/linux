/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef _I915_PXP_TEE_INTERFACE_H_
#define _I915_PXP_TEE_INTERFACE_H_

#include <linux/mutex.h>
#include <linux/device.h>
struct scatterlist;

/**
 * struct i915_pxp_component_ops - ops for PXP services.
 */
struct i915_pxp_component_ops {
	/**
	 * @owner: Module providing the ops.
	 */
	struct module *owner;

	/**
	 * @send: Send a PXP message.
	 */
	int (*send)(struct device *dev, const void *message, size_t size,
		    unsigned long timeout_ms);
	/**
	 * @recv: Receive a PXP message.
	 */
	int (*recv)(struct device *dev, void *buffer, size_t size,
		    unsigned long timeout_ms);
	/**
	 * @gsc_command: Send a GSC command.
	 */
	ssize_t (*gsc_command)(struct device *dev, u8 client_id, u32 fence_id,
			       struct scatterlist *sg_in, size_t total_in_len,
			       struct scatterlist *sg_out);

};

/**
 * struct i915_pxp_component - Used for communication between i915 and TEE
 * drivers for the PXP services
 */
struct i915_pxp_component {
	/**
	 * @tee_dev: device that provide the PXP service from TEE Bus.
	 */
	struct device *tee_dev;

	/**
	 * @ops: Ops implemented by TEE driver, used by i915 driver.
	 */
	const struct i915_pxp_component_ops *ops;

	/**
	 * @mutex: To protect the above members.
	 */
	struct mutex mutex;
};

#endif /* _I915_TEE_PXP_INTERFACE_H_ */
