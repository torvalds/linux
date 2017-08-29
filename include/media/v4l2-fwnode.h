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
	unsigned char data_lanes[4];
	unsigned char clock_lane;
	unsigned short num_data_lanes;
	bool lane_polarities[5];
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

/**
 * v4l2_fwnode_endpoint_parse() - parse all fwnode node properties
 * @fwnode: pointer to the endpoint's fwnode handle
 * @vep: pointer to the V4L2 fwnode data structure
 *
 * All properties are optional. If none are found, we don't set any flags. This
 * means the port has a static configuration and no properties have to be
 * specified explicitly. If any properties that identify the bus as parallel
 * are found and slave-mode isn't set, we set V4L2_MBUS_MASTER. Similarly, if
 * we recognise the bus as serial CSI-2 and clock-noncontinuous isn't set, we
 * set the V4L2_MBUS_CSI2_CONTINUOUS_CLOCK flag. The caller should hold a
 * reference to @fwnode.
 *
 * NOTE: This function does not parse properties the size of which is variable
 * without a low fixed limit. Please use v4l2_fwnode_endpoint_alloc_parse() in
 * new drivers instead.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int v4l2_fwnode_endpoint_parse(struct fwnode_handle *fwnode,
			       struct v4l2_fwnode_endpoint *vep);

/*
 * v4l2_fwnode_endpoint_free() - free the V4L2 fwnode acquired by
 * v4l2_fwnode_endpoint_alloc_parse()
 * @vep - the V4L2 fwnode the resources of which are to be released
 *
 * It is safe to call this function with NULL argument or on a V4L2 fwnode the
 * parsing of which failed.
 */
void v4l2_fwnode_endpoint_free(struct v4l2_fwnode_endpoint *vep);

/**
 * v4l2_fwnode_endpoint_alloc_parse() - parse all fwnode node properties
 * @fwnode: pointer to the endpoint's fwnode handle
 *
 * All properties are optional. If none are found, we don't set any flags. This
 * means the port has a static configuration and no properties have to be
 * specified explicitly. If any properties that identify the bus as parallel
 * are found and slave-mode isn't set, we set V4L2_MBUS_MASTER. Similarly, if
 * we recognise the bus as serial CSI-2 and clock-noncontinuous isn't set, we
 * set the V4L2_MBUS_CSI2_CONTINUOUS_CLOCK flag. The caller should hold a
 * reference to @fwnode.
 *
 * v4l2_fwnode_endpoint_alloc_parse() has two important differences to
 * v4l2_fwnode_endpoint_parse():
 *
 * 1. It also parses variable size data.
 *
 * 2. The memory it has allocated to store the variable size data must be freed
 *    using v4l2_fwnode_endpoint_free() when no longer needed.
 *
 * Return: Pointer to v4l2_fwnode_endpoint if successful, on an error pointer
 * on error.
 */
struct v4l2_fwnode_endpoint *v4l2_fwnode_endpoint_alloc_parse(
	struct fwnode_handle *fwnode);

/**
 * v4l2_fwnode_parse_link() - parse a link between two endpoints
 * @fwnode: pointer to the endpoint's fwnode at the local end of the link
 * @link: pointer to the V4L2 fwnode link data structure
 *
 * Fill the link structure with the local and remote nodes and port numbers.
 * The local_node and remote_node fields are set to point to the local and
 * remote port's parent nodes respectively (the port parent node being the
 * parent node of the port node if that node isn't a 'ports' node, or the
 * grand-parent node of the port node otherwise).
 *
 * A reference is taken to both the local and remote nodes, the caller must use
 * v4l2_fwnode_put_link() to drop the references when done with the
 * link.
 *
 * Return: 0 on success, or -ENOLINK if the remote endpoint fwnode can't be
 * found.
 */
int v4l2_fwnode_parse_link(struct fwnode_handle *fwnode,
			   struct v4l2_fwnode_link *link);

/**
 * v4l2_fwnode_put_link() - drop references to nodes in a link
 * @link: pointer to the V4L2 fwnode link data structure
 *
 * Drop references to the local and remote nodes in the link. This function
 * must be called on every link parsed with v4l2_fwnode_parse_link().
 */
void v4l2_fwnode_put_link(struct v4l2_fwnode_link *link);

#endif /* _V4L2_FWNODE_H */
