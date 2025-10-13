/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2025 Intel Corporation
 */

#ifndef _INTEL_LB_MEI_INTERFACE_H_
#define _INTEL_LB_MEI_INTERFACE_H_

#include <linux/types.h>

struct device;

/**
 * define INTEL_LB_FLAG_IS_PERSISTENT - Mark the payload as persistent
 *
 * This flag indicates that the late binding payload should be stored
 * persistently in flash across warm resets.
 */
#define INTEL_LB_FLAG_IS_PERSISTENT	BIT(0)

/**
 * enum intel_lb_type - enum to determine late binding payload type
 * @INTEL_LB_TYPE_FAN_CONTROL: Fan controller configuration
 */
enum intel_lb_type {
	INTEL_LB_TYPE_FAN_CONTROL = 1,
};

/**
 * enum intel_lb_status - Status codes returned on late binding transmissions
 * @INTEL_LB_STATUS_SUCCESS: Operation completed successfully
 * @INTEL_LB_STATUS_4ID_MISMATCH: Mismatch in the expected 4ID (firmware identity/token)
 * @INTEL_LB_STATUS_ARB_FAILURE: Arbitration failure (e.g. conflicting access or state)
 * @INTEL_LB_STATUS_GENERAL_ERROR: General firmware error not covered by other codes
 * @INTEL_LB_STATUS_INVALID_PARAMS: One or more input parameters are invalid
 * @INTEL_LB_STATUS_INVALID_SIGNATURE: Payload has an invalid or untrusted signature
 * @INTEL_LB_STATUS_INVALID_PAYLOAD: Payload contents are not accepted by firmware
 * @INTEL_LB_STATUS_TIMEOUT: Operation timed out before completion
 */
enum intel_lb_status {
	INTEL_LB_STATUS_SUCCESS           = 0,
	INTEL_LB_STATUS_4ID_MISMATCH      = 1,
	INTEL_LB_STATUS_ARB_FAILURE       = 2,
	INTEL_LB_STATUS_GENERAL_ERROR     = 3,
	INTEL_LB_STATUS_INVALID_PARAMS    = 4,
	INTEL_LB_STATUS_INVALID_SIGNATURE = 5,
	INTEL_LB_STATUS_INVALID_PAYLOAD   = 6,
	INTEL_LB_STATUS_TIMEOUT           = 7,
};

/**
 * struct intel_lb_component_ops - Ops for late binding services
 */
struct intel_lb_component_ops {
	/**
	 * push_payload - Sends a payload to the authentication firmware
	 * @dev: Device struct corresponding to the mei device
	 * @type: Payload type (see &enum intel_lb_type)
	 * @flags: Payload flags bitmap (e.g. %INTEL_LB_FLAGS_IS_PERSISTENT)
	 * @payload: Pointer to payload buffer
	 * @payload_size: Payload buffer size in bytes
	 *
	 * Return: 0 success, negative errno value on transport failure,
	 *         positive status returned by firmware
	 */
	int (*push_payload)(struct device *dev, u32 type, u32 flags,
			    const void *payload, size_t payload_size);
};

#endif /* _INTEL_LB_MEI_INTERFACE_H_ */
