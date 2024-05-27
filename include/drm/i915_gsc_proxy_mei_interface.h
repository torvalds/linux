/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2022-2023 Intel Corporation
 */

#ifndef _I915_GSC_PROXY_MEI_INTERFACE_H_
#define _I915_GSC_PROXY_MEI_INTERFACE_H_

#include <linux/types.h>

struct device;
struct module;

/**
 * struct i915_gsc_proxy_component_ops - ops for GSC Proxy services.
 * @owner: Module providing the ops
 * @send: sends a proxy message from GSC FW to ME FW
 * @recv: receives a proxy message for GSC FW from ME FW
 */
struct i915_gsc_proxy_component_ops {
	struct module *owner;

	/**
	 * @send: Sends a proxy message to ME FW.
	 * @dev: device struct corresponding to the mei device
	 * @buf: message buffer to send
	 * @size: size of the message
	 * Return: bytes sent on success, negative errno value on failure
	 */
	int (*send)(struct device *dev, const void *buf, size_t size);

	/**
	 * @recv: Receives a proxy message from ME FW.
	 * @dev: device struct corresponding to the mei device
	 * @buf: message buffer to contain the received message
	 * @size: size of the buffer
	 * Return: bytes received on success, negative errno value on failure
	 */
	int (*recv)(struct device *dev, void *buf, size_t size);
};

/**
 * struct i915_gsc_proxy_component - Used for communication between i915 and
 * MEI drivers for GSC proxy services
 * @mei_dev: device that provide the GSC proxy service.
 * @ops: Ops implemented by GSC proxy driver, used by i915 driver.
 */
struct i915_gsc_proxy_component {
	struct device *mei_dev;
	const struct i915_gsc_proxy_component_ops *ops;
};

#endif /* _I915_GSC_PROXY_MEI_INTERFACE_H_ */
