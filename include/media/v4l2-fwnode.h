/* SPDX-License-Identifier: GPL-2.0-only */
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
 */
#ifndef _V4L2_FWNODE_H
#define _V4L2_FWNODE_H

#include <linux/errno.h>
#include <linux/fwnode.h>
#include <linux/list.h>
#include <linux/types.h>

#include <media/v4l2-mediabus.h>

struct fwnode_handle;
struct v4l2_async_notifier;
struct v4l2_async_subdev;

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
	unsigned char num_data_lanes;
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
	unsigned char clock_inv:1;
	unsigned char strobe:1;
	bool lane_polarity[2];
	unsigned char data_lane;
	unsigned char clock_lane;
};

/**
 * struct v4l2_fwnode_endpoint - the endpoint data structure
 * @base: fwnode endpoint of the v4l2_fwnode
 * @bus_type: bus type
 * @bus: bus configuration data structure
 * @bus.parallel: embedded &struct v4l2_fwnode_bus_parallel.
 *		  Used if the bus is parallel.
 * @bus.mipi_csi1: embedded &struct v4l2_fwnode_bus_mipi_csi1.
 *		   Used if the bus is MIPI Alliance's Camera Serial
 *		   Interface version 1 (MIPI CSI1) or Standard
 *		   Mobile Imaging Architecture's Compact Camera Port 2
 *		   (SMIA CCP2).
 * @bus.mipi_csi2: embedded &struct v4l2_fwnode_bus_mipi_csi2.
 *		   Used if the bus is MIPI Alliance's Camera Serial
 *		   Interface version 2 (MIPI CSI2).
 * @link_frequencies: array of supported link frequencies
 * @nr_of_link_frequencies: number of elements in link_frequenccies array
 */
struct v4l2_fwnode_endpoint {
	struct fwnode_endpoint base;
	/*
	 * Fields below this line will be zeroed by
	 * v4l2_fwnode_endpoint_parse()
	 */
	enum v4l2_mbus_type bus_type;
	struct {
		struct v4l2_fwnode_bus_parallel parallel;
		struct v4l2_fwnode_bus_mipi_csi1 mipi_csi1;
		struct v4l2_fwnode_bus_mipi_csi2 mipi_csi2;
	} bus;
	u64 *link_frequencies;
	unsigned int nr_of_link_frequencies;
};

/**
 * V4L2_FWNODE_PROPERTY_UNSET - identify a non initialized property
 *
 * All properties in &struct v4l2_fwnode_device_properties are initialized
 * to this value.
 */
#define V4L2_FWNODE_PROPERTY_UNSET   (-1U)

/**
 * enum v4l2_fwnode_orientation - possible device orientation
 * @V4L2_FWNODE_ORIENTATION_FRONT: device installed on the front side
 * @V4L2_FWNODE_ORIENTATION_BACK: device installed on the back side
 * @V4L2_FWNODE_ORIENTATION_EXTERNAL: device externally located
 */
enum v4l2_fwnode_orientation {
	V4L2_FWNODE_ORIENTATION_FRONT,
	V4L2_FWNODE_ORIENTATION_BACK,
	V4L2_FWNODE_ORIENTATION_EXTERNAL
};

/**
 * struct v4l2_fwnode_device_properties - fwnode device properties
 * @orientation: device orientation. See &enum v4l2_fwnode_orientation
 * @rotation: device rotation
 */
struct v4l2_fwnode_device_properties {
	enum v4l2_fwnode_orientation orientation;
	unsigned int rotation;
};

/**
 * struct v4l2_fwnode_link - a link between two endpoints
 * @local_node: pointer to device_node of this endpoint
 * @local_port: identifier of the port this endpoint belongs to
 * @local_id: identifier of the id this endpoint belongs to
 * @remote_node: pointer to device_node of the remote endpoint
 * @remote_port: identifier of the port the remote endpoint belongs to
 * @remote_id: identifier of the id the remote endpoint belongs to
 */
struct v4l2_fwnode_link {
	struct fwnode_handle *local_node;
	unsigned int local_port;
	unsigned int local_id;
	struct fwnode_handle *remote_node;
	unsigned int remote_port;
	unsigned int remote_id;
};

/**
 * enum v4l2_connector_type - connector type
 * @V4L2_CONN_UNKNOWN:   unknown connector type, no V4L2 connector configuration
 * @V4L2_CONN_COMPOSITE: analog composite connector
 * @V4L2_CONN_SVIDEO:    analog svideo connector
 */
enum v4l2_connector_type {
	V4L2_CONN_UNKNOWN,
	V4L2_CONN_COMPOSITE,
	V4L2_CONN_SVIDEO,
};

/**
 * struct v4l2_connector_link - connector link data structure
 * @head: structure to be used to add the link to the
 *        &struct v4l2_fwnode_connector
 * @fwnode_link: &struct v4l2_fwnode_link link between the connector and the
 *               device the connector belongs to.
 */
struct v4l2_connector_link {
	struct list_head head;
	struct v4l2_fwnode_link fwnode_link;
};

/**
 * struct v4l2_fwnode_connector_analog - analog connector data structure
 * @sdtv_stds: sdtv standards this connector supports, set to V4L2_STD_ALL
 *             if no restrictions are specified.
 */
struct v4l2_fwnode_connector_analog {
	v4l2_std_id sdtv_stds;
};

/**
 * struct v4l2_fwnode_connector - the connector data structure
 * @name: the connector device name
 * @label: optional connector label
 * @type: connector type
 * @links: list of all connector &struct v4l2_connector_link links
 * @nr_of_links: total number of links
 * @connector: connector configuration
 * @connector.analog: analog connector configuration
 *                    &struct v4l2_fwnode_connector_analog
 */
struct v4l2_fwnode_connector {
	const char *name;
	const char *label;
	enum v4l2_connector_type type;
	struct list_head links;
	unsigned int nr_of_links;

	union {
		struct v4l2_fwnode_connector_analog analog;
		/* future connectors */
	} connector;
};

/**
 * v4l2_fwnode_endpoint_parse() - parse all fwnode node properties
 * @fwnode: pointer to the endpoint's fwnode handle
 * @vep: pointer to the V4L2 fwnode data structure
 *
 * This function parses the V4L2 fwnode endpoint specific parameters from the
 * firmware. The caller is responsible for assigning @vep.bus_type to a valid
 * media bus type. The caller may also set the default configuration for the
 * endpoint --- a configuration that shall be in line with the DT binding
 * documentation. Should a device support multiple bus types, the caller may
 * call this function once the correct type is found --- with a default
 * configuration valid for that type.
 *
 * It is also allowed to set @vep.bus_type to V4L2_MBUS_UNKNOWN. USING THIS
 * FEATURE REQUIRES "bus-type" PROPERTY IN DT BINDINGS. For old drivers,
 * guessing @vep.bus_type between CSI-2 D-PHY, parallel and BT.656 busses is
 * supported. NEVER RELY ON GUESSING @vep.bus_type IN NEW DRIVERS!
 *
 * The caller is required to initialise all fields of @vep, either with
 * explicitly values, or by zeroing them.
 *
 * The function does not change the V4L2 fwnode endpoint state if it fails.
 *
 * NOTE: This function does not parse properties the size of which is variable
 * without a low fixed limit. Please use v4l2_fwnode_endpoint_alloc_parse() in
 * new drivers instead.
 *
 * Return: %0 on success or a negative error code on failure:
 *	   %-ENOMEM on memory allocation failure
 *	   %-EINVAL on parsing failure
 *	   %-ENXIO on mismatching bus types
 */
int v4l2_fwnode_endpoint_parse(struct fwnode_handle *fwnode,
			       struct v4l2_fwnode_endpoint *vep);

/**
 * v4l2_fwnode_endpoint_free() - free the V4L2 fwnode acquired by
 * v4l2_fwnode_endpoint_alloc_parse()
 * @vep: the V4L2 fwnode the resources of which are to be released
 *
 * It is safe to call this function with NULL argument or on a V4L2 fwnode the
 * parsing of which failed.
 */
void v4l2_fwnode_endpoint_free(struct v4l2_fwnode_endpoint *vep);

/**
 * v4l2_fwnode_endpoint_alloc_parse() - parse all fwnode node properties
 * @fwnode: pointer to the endpoint's fwnode handle
 * @vep: pointer to the V4L2 fwnode data structure
 *
 * This function parses the V4L2 fwnode endpoint specific parameters from the
 * firmware. The caller is responsible for assigning @vep.bus_type to a valid
 * media bus type. The caller may also set the default configuration for the
 * endpoint --- a configuration that shall be in line with the DT binding
 * documentation. Should a device support multiple bus types, the caller may
 * call this function once the correct type is found --- with a default
 * configuration valid for that type.
 *
 * It is also allowed to set @vep.bus_type to V4L2_MBUS_UNKNOWN. USING THIS
 * FEATURE REQUIRES "bus-type" PROPERTY IN DT BINDINGS. For old drivers,
 * guessing @vep.bus_type between CSI-2 D-PHY, parallel and BT.656 busses is
 * supported. NEVER RELY ON GUESSING @vep.bus_type IN NEW DRIVERS!
 *
 * The caller is required to initialise all fields of @vep, either with
 * explicitly values, or by zeroing them.
 *
 * The function does not change the V4L2 fwnode endpoint state if it fails.
 *
 * v4l2_fwnode_endpoint_alloc_parse() has two important differences to
 * v4l2_fwnode_endpoint_parse():
 *
 * 1. It also parses variable size data.
 *
 * 2. The memory it has allocated to store the variable size data must be freed
 *    using v4l2_fwnode_endpoint_free() when no longer needed.
 *
 * Return: %0 on success or a negative error code on failure:
 *	   %-ENOMEM on memory allocation failure
 *	   %-EINVAL on parsing failure
 *	   %-ENXIO on mismatching bus types
 */
int v4l2_fwnode_endpoint_alloc_parse(struct fwnode_handle *fwnode,
				     struct v4l2_fwnode_endpoint *vep);

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

/**
 * v4l2_fwnode_connector_free() - free the V4L2 connector acquired memory
 * @connector: the V4L2 connector resources of which are to be released
 *
 * Free all allocated memory and put all links acquired by
 * v4l2_fwnode_connector_parse() and v4l2_fwnode_connector_add_link().
 *
 * It is safe to call this function with NULL argument or on a V4L2 connector
 * the parsing of which failed.
 */
void v4l2_fwnode_connector_free(struct v4l2_fwnode_connector *connector);

/**
 * v4l2_fwnode_connector_parse() - initialize the 'struct v4l2_fwnode_connector'
 * @fwnode: pointer to the subdev endpoint's fwnode handle where the connector
 *	    is connected to or to the connector endpoint fwnode handle.
 * @connector: pointer to the V4L2 fwnode connector data structure
 *
 * Fill the &struct v4l2_fwnode_connector with the connector type, label and
 * all &enum v4l2_connector_type specific connector data. The label is optional
 * so it is set to %NULL if no one was found. The function initialize the links
 * to zero. Adding links to the connector is done by calling
 * v4l2_fwnode_connector_add_link().
 *
 * The memory allocated for the label must be freed when no longer needed.
 * Freeing the memory is done by v4l2_fwnode_connector_free().
 *
 * Return:
 * * %0 on success or a negative error code on failure:
 * * %-EINVAL if @fwnode is invalid
 * * %-ENOTCONN if connector type is unknown or connector device can't be found
 */
int v4l2_fwnode_connector_parse(struct fwnode_handle *fwnode,
				struct v4l2_fwnode_connector *connector);

/**
 * v4l2_fwnode_connector_add_link - add a link between a connector node and
 *				    a v4l2-subdev node.
 * @fwnode: pointer to the subdev endpoint's fwnode handle where the connector
 *          is connected to
 * @connector: pointer to the V4L2 fwnode connector data structure
 *
 * Add a new &struct v4l2_connector_link link to the
 * &struct v4l2_fwnode_connector connector links list. The link local_node
 * points to the connector node, the remote_node to the host v4l2 (sub)dev.
 *
 * The taken references to remote_node and local_node must be dropped and the
 * allocated memory must be freed when no longer needed. Both is done by calling
 * v4l2_fwnode_connector_free().
 *
 * Return:
 * * %0 on success or a negative error code on failure:
 * * %-EINVAL if @fwnode or @connector is invalid or @connector type is unknown
 * * %-ENOMEM on link memory allocation failure
 * * %-ENOTCONN if remote connector device can't be found
 * * %-ENOLINK if link parsing between v4l2 (sub)dev and connector fails
 */
int v4l2_fwnode_connector_add_link(struct fwnode_handle *fwnode,
				   struct v4l2_fwnode_connector *connector);

/**
 * v4l2_fwnode_device_parse() - parse fwnode device properties
 * @dev: pointer to &struct device
 * @props: pointer to &struct v4l2_fwnode_device_properties where to store the
 *	   parsed properties values
 *
 * This function parses and validates the V4L2 fwnode device properties from the
 * firmware interface, and fills the @struct v4l2_fwnode_device_properties
 * provided by the caller.
 *
 * Return:
 *	% 0 on success
 *	%-EINVAL if a parsed property value is not valid
 */
int v4l2_fwnode_device_parse(struct device *dev,
			     struct v4l2_fwnode_device_properties *props);

/**
 * typedef parse_endpoint_func - Driver's callback function to be called on
 *	each V4L2 fwnode endpoint.
 *
 * @dev: pointer to &struct device
 * @vep: pointer to &struct v4l2_fwnode_endpoint
 * @asd: pointer to &struct v4l2_async_subdev
 *
 * Return:
 * * %0 on success
 * * %-ENOTCONN if the endpoint is to be skipped but this
 *   should not be considered as an error
 * * %-EINVAL if the endpoint configuration is invalid
 */
typedef int (*parse_endpoint_func)(struct device *dev,
				  struct v4l2_fwnode_endpoint *vep,
				  struct v4l2_async_subdev *asd);

/**
 * v4l2_async_notifier_parse_fwnode_endpoints - Parse V4L2 fwnode endpoints in a
 *						device node
 * @dev: the device the endpoints of which are to be parsed
 * @notifier: notifier for @dev
 * @asd_struct_size: size of the driver's async sub-device struct, including
 *		     sizeof(struct v4l2_async_subdev). The &struct
 *		     v4l2_async_subdev shall be the first member of
 *		     the driver's async sub-device struct, i.e. both
 *		     begin at the same memory address.
 * @parse_endpoint: Driver's callback function called on each V4L2 fwnode
 *		    endpoint. Optional.
 *
 * Parse the fwnode endpoints of the @dev device and populate the async sub-
 * devices list in the notifier. The @parse_endpoint callback function is
 * called for each endpoint with the corresponding async sub-device pointer to
 * let the caller initialize the driver-specific part of the async sub-device
 * structure.
 *
 * The notifier memory shall be zeroed before this function is called on the
 * notifier.
 *
 * This function may not be called on a registered notifier and may be called on
 * a notifier only once.
 *
 * The &struct v4l2_fwnode_endpoint passed to the callback function
 * @parse_endpoint is released once the function is finished. If there is a need
 * to retain that configuration, the user needs to allocate memory for it.
 *
 * Any notifier populated using this function must be released with a call to
 * v4l2_async_notifier_cleanup() after it has been unregistered and the async
 * sub-devices are no longer in use, even if the function returned an error.
 *
 * Return: %0 on success, including when no async sub-devices are found
 *	   %-ENOMEM if memory allocation failed
 *	   %-EINVAL if graph or endpoint parsing failed
 *	   Other error codes as returned by @parse_endpoint
 */
int
v4l2_async_notifier_parse_fwnode_endpoints(struct device *dev,
					   struct v4l2_async_notifier *notifier,
					   size_t asd_struct_size,
					   parse_endpoint_func parse_endpoint);

/**
 * v4l2_async_notifier_parse_fwnode_endpoints_by_port - Parse V4L2 fwnode
 *							endpoints of a port in a
 *							device node
 * @dev: the device the endpoints of which are to be parsed
 * @notifier: notifier for @dev
 * @asd_struct_size: size of the driver's async sub-device struct, including
 *		     sizeof(struct v4l2_async_subdev). The &struct
 *		     v4l2_async_subdev shall be the first member of
 *		     the driver's async sub-device struct, i.e. both
 *		     begin at the same memory address.
 * @port: port number where endpoints are to be parsed
 * @parse_endpoint: Driver's callback function called on each V4L2 fwnode
 *		    endpoint. Optional.
 *
 * This function is just like v4l2_async_notifier_parse_fwnode_endpoints() with
 * the exception that it only parses endpoints in a given port. This is useful
 * on devices that have both sinks and sources: the async sub-devices connected
 * to sources have already been configured by another driver (on capture
 * devices). In this case the driver must know which ports to parse.
 *
 * Parse the fwnode endpoints of the @dev device on a given @port and populate
 * the async sub-devices list of the notifier. The @parse_endpoint callback
 * function is called for each endpoint with the corresponding async sub-device
 * pointer to let the caller initialize the driver-specific part of the async
 * sub-device structure.
 *
 * The notifier memory shall be zeroed before this function is called on the
 * notifier the first time.
 *
 * This function may not be called on a registered notifier and may be called on
 * a notifier only once per port.
 *
 * The &struct v4l2_fwnode_endpoint passed to the callback function
 * @parse_endpoint is released once the function is finished. If there is a need
 * to retain that configuration, the user needs to allocate memory for it.
 *
 * Any notifier populated using this function must be released with a call to
 * v4l2_async_notifier_cleanup() after it has been unregistered and the async
 * sub-devices are no longer in use, even if the function returned an error.
 *
 * Return: %0 on success, including when no async sub-devices are found
 *	   %-ENOMEM if memory allocation failed
 *	   %-EINVAL if graph or endpoint parsing failed
 *	   Other error codes as returned by @parse_endpoint
 */
int
v4l2_async_notifier_parse_fwnode_endpoints_by_port(struct device *dev,
						   struct v4l2_async_notifier *notifier,
						   size_t asd_struct_size,
						   unsigned int port,
						   parse_endpoint_func parse_endpoint);

/**
 * v4l2_fwnode_reference_parse_sensor_common - parse common references on
 *					       sensors for async sub-devices
 * @dev: the device node the properties of which are parsed for references
 * @notifier: the async notifier where the async subdevs will be added
 *
 * Parse common sensor properties for remote devices related to the
 * sensor and set up async sub-devices for them.
 *
 * Any notifier populated using this function must be released with a call to
 * v4l2_async_notifier_release() after it has been unregistered and the async
 * sub-devices are no longer in use, even in the case the function returned an
 * error.
 *
 * Return: 0 on success
 *	   -ENOMEM if memory allocation failed
 *	   -EINVAL if property parsing failed
 */
int v4l2_async_notifier_parse_fwnode_sensor_common(struct device *dev,
						   struct v4l2_async_notifier *notifier);

/* Helper macros to access the connector links. */

/** v4l2_connector_last_link - Helper macro to get the first
 *                             &struct v4l2_fwnode_connector link
 * @v4l2c: &struct v4l2_fwnode_connector owning the connector links
 *
 * This marco returns the first added &struct v4l2_connector_link connector
 * link or @NULL if the connector has no links.
 */
#define v4l2_connector_first_link(v4l2c)				       \
	list_first_entry_or_null(&(v4l2c)->links,			       \
				 struct v4l2_connector_link, head)

/** v4l2_connector_last_link - Helper macro to get the last
 *                             &struct v4l2_fwnode_connector link
 * @v4l2c: &struct v4l2_fwnode_connector owning the connector links
 *
 * This marco returns the last &struct v4l2_connector_link added connector link.
 */
#define v4l2_connector_last_link(v4l2c)					       \
	list_last_entry(&(v4l2c)->links, struct v4l2_connector_link, head)

#endif /* _V4L2_FWNODE_H */
