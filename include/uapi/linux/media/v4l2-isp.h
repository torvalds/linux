/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Video4Linux2 generic ISP parameters and statistics support
 *
 * Copyright (C) 2025 Ideas On Board Oy
 * Author: Jacopo Mondi <jacopo.mondi@ideasonboard.com>
 */

#ifndef _UAPI_V4L2_ISP_H_
#define _UAPI_V4L2_ISP_H_

#include <linux/stddef.h>
#include <linux/types.h>

/**
 * enum v4l2_isp_params_version - V4L2 ISP parameters versioning
 *
 * @V4L2_ISP_PARAMS_VERSION_V0: First version of the V4L2 ISP parameters format
 *				(for compatibility)
 * @V4L2_ISP_PARAMS_VERSION_V1: First version of the V4L2 ISP parameters format
 *
 * V0 and V1 are identical in order to support drivers compatible with the V4L2
 * ISP parameters format already upstreamed which use either 0 or 1 as their
 * versioning identifier. Both V0 and V1 refers to the first version of the
 * V4L2 ISP parameters format.
 *
 * Future revisions of the V4L2 ISP parameters format should start from the
 * value of 2.
 */
enum v4l2_isp_params_version {
	V4L2_ISP_PARAMS_VERSION_V0 = 0,
	V4L2_ISP_PARAMS_VERSION_V1
};

#define V4L2_ISP_PARAMS_FL_BLOCK_DISABLE	(1U << 0)
#define V4L2_ISP_PARAMS_FL_BLOCK_ENABLE		(1U << 1)

/*
 * Reserve the first 8 bits for V4L2_ISP_PARAMS_FL_* flag.
 *
 * Driver-specific flags should be defined as:
 * #define DRIVER_SPECIFIC_FLAG0     ((1U << V4L2_ISP_PARAMS_FL_DRIVER_FLAGS(0))
 * #define DRIVER_SPECIFIC_FLAG1     ((1U << V4L2_ISP_PARAMS_FL_DRIVER_FLAGS(1))
 */
#define V4L2_ISP_PARAMS_FL_DRIVER_FLAGS(n)       ((n) + 8)

/**
 * struct v4l2_isp_params_block_header - V4L2 extensible parameters block header
 * @type: The parameters block type (driver-specific)
 * @flags: A bitmask of block flags (driver-specific)
 * @size: Size (in bytes) of the parameters block, including this header
 *
 * This structure represents the common part of all the ISP configuration
 * blocks. Each parameters block shall embed an instance of this structure type
 * as its first member, followed by the block-specific configuration data.
 *
 * The @type field is an ISP driver-specific value that identifies the block
 * type. The @size field specifies the size of the parameters block.
 *
 * The @flags field is a bitmask of per-block flags V4L2_PARAMS_ISP_FL_* and
 * driver-specific flags specified by the driver header.
 */
struct v4l2_isp_params_block_header {
	__u16 type;
	__u16 flags;
	__u32 size;
} __attribute__((aligned(8)));

/**
 * struct v4l2_isp_params_buffer - V4L2 extensible parameters configuration
 * @version: The parameters buffer version (driver-specific)
 * @data_size: The configuration data effective size, excluding this header
 * @data: The configuration data
 *
 * This structure contains the configuration parameters of the ISP algorithms,
 * serialized by userspace into a data buffer. Each configuration parameter
 * block is represented by a block-specific structure which contains a
 * :c:type:`v4l2_isp_params_block_header` entry as first member. Userspace
 * populates the @data buffer with configuration parameters for the blocks that
 * it intends to configure. As a consequence, the data buffer effective size
 * changes according to the number of ISP blocks that userspace intends to
 * configure and is set by userspace in the @data_size field.
 *
 * The parameters buffer is versioned by the @version field to allow modifying
 * and extending its definition. Userspace shall populate the @version field to
 * inform the driver about the version it intends to use. The driver will parse
 * and handle the @data buffer according to the data layout specific to the
 * indicated version and return an error if the desired version is not
 * supported.
 *
 * For each ISP block that userspace wants to configure, a block-specific
 * structure is appended to the @data buffer, one after the other without gaps
 * in between. Userspace shall populate the @data_size field with the effective
 * size, in bytes, of the @data buffer.
 */
struct v4l2_isp_params_buffer {
	__u32 version;
	__u32 data_size;
	__u8 data[] __counted_by(data_size);
};

#endif /* _UAPI_V4L2_ISP_H_ */
