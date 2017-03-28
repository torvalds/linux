// SPDX-License-Identifier: GPL-2.0
/*
 * SCMI Message Protocol driver header
 *
 * Copyright (C) 2018 ARM Ltd.
 */
#include <linux/types.h>

/**
 * struct scmi_handle - Handle returned to ARM SCMI clients for usage.
 *
 * @dev: pointer to the SCMI device
 */
struct scmi_handle {
	struct device *dev;
};
