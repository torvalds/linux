/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Video4Linux2 generic ISP parameters and statistics support
 *
 * Copyright (C) 2025 Ideas On Board Oy
 * Author: Jacopo Mondi <jacopo.mondi@ideasonboard.com>
 */

#ifndef _V4L2_ISP_H_
#define _V4L2_ISP_H_

#include <linux/media/v4l2-isp.h>

struct device;
struct vb2_buffer;

/**
 * v4l2_isp_buffer_size - Calculate size of v4l2_isp_buffer
 * @max_size: The total size of the ISP configuration or statistics blocks
 *
 * Users of v4l2-isp will have differing sized data arrays for parameters and
 * statistics, depending on their specific blocks. Drivers need to be able to
 * calculate the appropriate size of the buffer to accommodate all ISP blocks
 * supported by the platform. This macro provides a convenient tool for the
 * calculation.
 *
 * The intended users of this function are drivers initializing the size
 * of their metadata (parameters and statistics) buffers.
 *
 */
#define v4l2_isp_buffer_size(max_size)			\
	(offsetof(struct v4l2_isp_buffer, data) + (max_size))

/**
 * v4l2_isp_params_validate_buffer_size - Validate a V4L2 ISP buffer sizes
 * @dev: the driver's device pointer
 * @vb: the videobuf2 buffer
 * @max_size: the maximum allowed buffer size
 *
 * This function performs validation of the size of a V4L2 ISP parameters buffer
 * before the driver can access the actual data buffer content.
 *
 * After the sizes validation, drivers should copy the buffer content to a
 * kernel-only memory area to prevent userspace from modifying it,
 * before completing validation using v4l2_isp_params_validate_buffer().
 *
 * The @vb buffer as received from the vb2 .buf_prepare() operation is checked
 * against @max_size and it's validated to be large enough to accommodate at
 * least one ISP configuration block.
 */
int v4l2_isp_params_validate_buffer_size(struct device *dev,
					 struct vb2_buffer *vb,
					 size_t max_size);

/**
 * struct v4l2_isp_params_block_type_info - V4L2 ISP per-block-type info
 * @size: the block type expected size
 * @block_validate: driver's callback to implement per-block validation
 *
 * The v4l2_isp_params_block_type_info collects information of the ISP
 * configuration block types for validation purposes. It contains the expected
 * block type size and a function pointer where drivers can register a callback
 * for additional per-block validation purposes. The validation function is
 * expected to return 0 on success or a negative error number for errors.
 *
 * Drivers shall prepare a list of block type info, indexed by block type, one
 * for each supported ISP block type and correctly populate them with the
 * expected block type size and the optional callback.
 */
struct v4l2_isp_params_block_type_info {
	size_t size;
	int (*block_validate)(struct device *dev,
			      const struct v4l2_isp_block_header *block);
};

/**
 * v4l2_isp_params_validate_buffer - Validate a V4L2 ISP parameters buffer
 * @dev: the driver's device pointer
 * @vb: the videobuf2 buffer
 * @buffer: the V4L2 ISP parameters buffer
 * @type_info: the array of per-block-type validation info
 * @num_block_types: the number of block types in the type_info array
 *
 * This function completes the validation of a V4L2 ISP parameters buffer,
 * verifying each configuration block correctness before the driver can use
 * them to program the hardware.
 *
 * Drivers should use this function after having validated the correctness of
 * the vb2 buffer sizes by using the v4l2_isp_params_validate_buffer_size()
 * helper first. Once the buffer size has been validated, drivers should
 * perform a copy of the user provided buffer into a kernel-only memory buffer
 * to prevent userspace from modifying its content after it has been submitted
 * to the driver, and then call this function to complete validation.
 */
int v4l2_isp_params_validate_buffer(struct device *dev, struct vb2_buffer *vb,
				    const struct v4l2_isp_params_buffer *buffer,
				    const struct v4l2_isp_params_block_type_info *type_info,
				    size_t num_block_types);

#endif /* _V4L2_ISP_H_ */
