/*
 * V4L2 fwnode binding parsing library
 *
 * Copyright (c) 2016 Intel Corporation.
 * Author: Sakari Ailus <sakari.ailus@linux.intel.com>
 *
 * Copyright (C) 2012 - 2013 Samsung Electronics Co., Ltd.
 * Author: Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * Copyright (C) 2012 Renesas Electronics Corp.
 * Author: Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 */
#ifndef _V4L2_FWNODE_H
#define _V4L2_FWNODE_H

#include <linux/errno.h>
#include <linux/fwnode.h>
#include <linux/list.h>
#include <linux/types.h>

#include <media/v4l2-mediabus.h>

struct fwnode_handle;

#define V4L2_FWNODE_CSI2_MAX_DATA_LANES	4

/**
 * struct v4l2_fwnode_bus_mipi_csi2 - MIPI CSI-2 bus data structure
 * @flags: media bus (V4L2_MBUS_*) flags
 * @data_lanes: an array of physical data lane indexes
 * @clock_lane: physical lane index of the clock lane
 * @num_data_lanes: number of data lanes
 * @lane_polarities: polarity of the lanes. The order is the same of
 *		   the physical lanes.
 */
struct v4l2_fwnode_bus_mipi_csi2 {
	unsigned int flags;
	unsigned char data_lanes[V4L2_FWNODE_CSI2_MAX_DATA_LANES];
	unsigned char clock_lane;
	unsigned short num_data_lanes;
	bool lane_polarities[1 + V4L2_FWNODE_CSI2_MAX_DATA_LANES];
};

/**
 * struct v4l2_fwnode_bus_parallel - parallel data bus data structure
 * @flags: media bus (V4L2_MBUS_*) flags
 * @bus_width: bus width in bits
 * @data_shift: data shift in bits
 */
struct v4l2_fwnode_bus_parallel {
	unsigned int flags;
	unsigned char bus_width;
	unsigned char data_shift;
};

/**
 * struct v4l2_fwnode_bus_mipi_csi1 - CSI-1/CCP2 data bus structure
 * @clock_inv: polarity of clock/strobe signal
 *	       false - not inverted, true - inverted
 * @strobe: false - data/clock, true - data/strobe
 * @lane_polarity: the polarities of the clock (index 0) and data lanes
 *		   index (1)
 * @data_lane: the number of the data lane
 * @clock_lane: the number of the clock lane
 */
struct v4l2_fwnode_bus_mipi_csi1 {
	bool clock_inv;
	bool strobe;
	bool lane_polarity[2];
	unsigned char data_lane;
	unsigned char clock_lane;
};

/**
 * struct v4l2_fwnode_endpoint - the endpoint data structure
 * @base: fwnode endpoint of the v4l2_fwnode
 * @bus_type: bus type
 * @bus: bus configuration data structure
 * @link_frequencies: array of supported link frequencies
 * @nr_of_link_frequencies: number of elements in link_frequenccies array
 */
struct v4l2_fwnode_endpoint {
	struct fwnode_endpoint base;
	/*
	 * Fields below this line will be zeroed by
	 * v4l2_fwnode_parse_endpoint()
	 */
	enum v4l2_mbus_type bus_type;
	union {
		struct v4l2_fwnode_bus_parallel parallel;
		struct v4l2_fwnode_bus_mipi_csi1 mipi_csi1;
		struct v4l2_fwnode_bus_mipi_csi2 mipi_csi2;
	} bus;
	u64 *link_frequencies;
	unsigned int nr_of_link_frequencies;
};

/**
 * struct v4l2_fwnode_link - a link between two endpoints
 * @local_node: pointer to device_node of this endpoint
 * @local_port: identifier of the port this endpoint belongs to
 * @remote_node: pointer to device_node of the remote endpoint
 * @remote_port: identifier of the port the remote endpoint belongs to
 */
struct v4l2_fwnode_link {
	struct fwnode_handle *local_node;
	unsigned int local_port;
	struct fwnode_handle *remote_node;
	unsigned int remote_port;
};

int v4l2_fwnode_endpoint_parse(struct fwnode_handle *fwnode,
			       struct v4l2_fwnode_endpoint *vep);
struct v4l2_fwnode_endpoint *v4l2_fwnode_endpoint_alloc_parse(
	struct fwnode_handle *fwnode);
void v4l2_fwnode_endpoint_free(struct v4l2_fwnode_endpoint *vep);
int v4l2_fwnode_parse_link(struct fwnode_handle *fwnode,
			   struct v4l2_fwnode_link *link);
void v4l2_fwnode_put_link(struct v4l2_fwnode_link *link);

#endif /* _V4L2_FWNODE_H */
