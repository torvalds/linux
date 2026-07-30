/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Dreamchip RPP-X1 ISP Driver - Userspace API
 *
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#ifndef __UAPI_RPP_X1_CONFIG_H
#define __UAPI_RPP_X1_CONFIG_H

#include <linux/media/v4l2-isp.h>

/**
 * struct rppx1_window - Measurement window
 *
 * RPP-X1 measurement window. Different blocks use a window or multiple
 * windows for measurement purposes. This defines a common type for all of
 * them. The number of relevant bits depends on the block where the window is
 * used and is specified in the per-block description
 *
 * @h_offs: horizontal offset from the left of the frame in pixels
 * @v_offs: vertical offset from the top of the frame in pixels
 * @h_size: horizontal size of the window in pixels
 * @v_size: vertical size of the window in pixels
 */
struct rppx1_window {
	__u16 h_offs;
	__u16 v_offs;
	__u16 h_size;
	__u16 v_size;
};

/* ---------------------------------------------------------------------------
 * Parameter Structures
 *
 * The same ISP block might be instantiated in multiple pipeliness and operate
 * on a different bitdepth/precision. For fields of varying length among
 * different instances of the same block, use a data type that can accommodate
 * the larger bitdepth/precision.
 */

/**
 * RPPX1_PARAMS_MAX_SIZE - Maximum size of all RPP-X1 parameter blocks
 *
 * Some types are reported twice as the same block might be instantiated in
 * multiple pipes.
 */
#define RPPX1_PARAMS_MAX_SIZE 0

/* ---------------------------------------------------------------------------
 * Statistics Structures
 *
 * The same ISP block might be instantiated in multiple pipeliness and operate
 * on a different bitdepth/precision. For fields of varying length among
 * different instances of the same block, use a data type that can accommodate
 * the larger bitdepth/precision.
 */

/**
 * RPPX1_STATS_MAX_SIZE - Maximum size of all RPP-X1 statistics
 *
 * Some types are reported twice as the same block might be instantiated in
 * multiple pipes.
 */
#define RPPX1_STATS_MAX_SIZE 0

#endif /* __UAPI_RPP_X1_CONFIG_H */
