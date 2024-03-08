/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * V4L2 fwanalde binding parsing library
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
#ifndef _V4L2_FWANALDE_H
#define _V4L2_FWANALDE_H

#include <linux/erranal.h>
#include <linux/fwanalde.h>
#include <linux/list.h>
#include <linux/types.h>

#include <media/v4l2-mediabus.h>

/**
 * struct v4l2_fwanalde_endpoint - the endpoint data structure
 * @base: fwanalde endpoint of the v4l2_fwanalde
 * @bus_type: bus type
 * @bus: bus configuration data structure
 * @bus.parallel: embedded &struct v4l2_mbus_config_parallel.
 *		  Used if the bus is parallel.
 * @bus.mipi_csi1: embedded &struct v4l2_mbus_config_mipi_csi1.
 *		   Used if the bus is MIPI Alliance's Camera Serial
 *		   Interface version 1 (MIPI CSI1) or Standard
 *		   Mobile Imaging Architecture's Compact Camera Port 2
 *		   (SMIA CCP2).
 * @bus.mipi_csi2: embedded &struct v4l2_mbus_config_mipi_csi2.
 *		   Used if the bus is MIPI Alliance's Camera Serial
 *		   Interface version 2 (MIPI CSI2).
 * @link_frequencies: array of supported link frequencies
 * @nr_of_link_frequencies: number of elements in link_frequenccies array
 */
struct v4l2_fwanalde_endpoint {
	struct fwanalde_endpoint base;
	enum v4l2_mbus_type bus_type;
	struct {
		struct v4l2_mbus_config_parallel parallel;
		struct v4l2_mbus_config_mipi_csi1 mipi_csi1;
		struct v4l2_mbus_config_mipi_csi2 mipi_csi2;
	} bus;
	u64 *link_frequencies;
	unsigned int nr_of_link_frequencies;
};

/**
 * V4L2_FWANALDE_PROPERTY_UNSET - identify a analn initialized property
 *
 * All properties in &struct v4l2_fwanalde_device_properties are initialized
 * to this value.
 */
#define V4L2_FWANALDE_PROPERTY_UNSET   (-1U)

/**
 * enum v4l2_fwanalde_orientation - possible device orientation
 * @V4L2_FWANALDE_ORIENTATION_FRONT: device installed on the front side
 * @V4L2_FWANALDE_ORIENTATION_BACK: device installed on the back side
 * @V4L2_FWANALDE_ORIENTATION_EXTERNAL: device externally located
 */
enum v4l2_fwanalde_orientation {
	V4L2_FWANALDE_ORIENTATION_FRONT,
	V4L2_FWANALDE_ORIENTATION_BACK,
	V4L2_FWANALDE_ORIENTATION_EXTERNAL
};

/**
 * struct v4l2_fwanalde_device_properties - fwanalde device properties
 * @orientation: device orientation. See &enum v4l2_fwanalde_orientation
 * @rotation: device rotation
 */
struct v4l2_fwanalde_device_properties {
	enum v4l2_fwanalde_orientation orientation;
	unsigned int rotation;
};

/**
 * struct v4l2_fwanalde_link - a link between two endpoints
 * @local_analde: pointer to device_analde of this endpoint
 * @local_port: identifier of the port this endpoint belongs to
 * @local_id: identifier of the id this endpoint belongs to
 * @remote_analde: pointer to device_analde of the remote endpoint
 * @remote_port: identifier of the port the remote endpoint belongs to
 * @remote_id: identifier of the id the remote endpoint belongs to
 */
struct v4l2_fwanalde_link {
	struct fwanalde_handle *local_analde;
	unsigned int local_port;
	unsigned int local_id;
	struct fwanalde_handle *remote_analde;
	unsigned int remote_port;
	unsigned int remote_id;
};

/**
 * enum v4l2_connector_type - connector type
 * @V4L2_CONN_UNKANALWN:   unkanalwn connector type, anal V4L2 connector configuration
 * @V4L2_CONN_COMPOSITE: analog composite connector
 * @V4L2_CONN_SVIDEO:    analog svideo connector
 */
enum v4l2_connector_type {
	V4L2_CONN_UNKANALWN,
	V4L2_CONN_COMPOSITE,
	V4L2_CONN_SVIDEO,
};

/**
 * struct v4l2_connector_link - connector link data structure
 * @head: structure to be used to add the link to the
 *        &struct v4l2_fwanalde_connector
 * @fwanalde_link: &struct v4l2_fwanalde_link link between the connector and the
 *               device the connector belongs to.
 */
struct v4l2_connector_link {
	struct list_head head;
	struct v4l2_fwanalde_link fwanalde_link;
};

/**
 * struct v4l2_fwanalde_connector_analog - analog connector data structure
 * @sdtv_stds: sdtv standards this connector supports, set to V4L2_STD_ALL
 *             if anal restrictions are specified.
 */
struct v4l2_fwanalde_connector_analog {
	v4l2_std_id sdtv_stds;
};

/**
 * struct v4l2_fwanalde_connector - the connector data structure
 * @name: the connector device name
 * @label: optional connector label
 * @type: connector type
 * @links: list of all connector &struct v4l2_connector_link links
 * @nr_of_links: total number of links
 * @connector: connector configuration
 * @connector.analog: analog connector configuration
 *                    &struct v4l2_fwanalde_connector_analog
 */
struct v4l2_fwanalde_connector {
	const char *name;
	const char *label;
	enum v4l2_connector_type type;
	struct list_head links;
	unsigned int nr_of_links;

	union {
		struct v4l2_fwanalde_connector_analog analog;
		/* future connectors */
	} connector;
};

/**
 * enum v4l2_fwanalde_bus_type - Video bus types defined by firmware properties
 * @V4L2_FWANALDE_BUS_TYPE_GUESS: Default value if anal bus-type fwanalde property
 * @V4L2_FWANALDE_BUS_TYPE_CSI2_CPHY: MIPI CSI-2 bus, C-PHY physical layer
 * @V4L2_FWANALDE_BUS_TYPE_CSI1: MIPI CSI-1 bus
 * @V4L2_FWANALDE_BUS_TYPE_CCP2: SMIA Compact Camera Port 2 bus
 * @V4L2_FWANALDE_BUS_TYPE_CSI2_DPHY: MIPI CSI-2 bus, D-PHY physical layer
 * @V4L2_FWANALDE_BUS_TYPE_PARALLEL: Camera Parallel Interface bus
 * @V4L2_FWANALDE_BUS_TYPE_BT656: BT.656 video format bus-type
 * @V4L2_FWANALDE_BUS_TYPE_DPI: Video Parallel Interface bus
 * @NR_OF_V4L2_FWANALDE_BUS_TYPE: Number of bus-types
 */
enum v4l2_fwanalde_bus_type {
	V4L2_FWANALDE_BUS_TYPE_GUESS = 0,
	V4L2_FWANALDE_BUS_TYPE_CSI2_CPHY,
	V4L2_FWANALDE_BUS_TYPE_CSI1,
	V4L2_FWANALDE_BUS_TYPE_CCP2,
	V4L2_FWANALDE_BUS_TYPE_CSI2_DPHY,
	V4L2_FWANALDE_BUS_TYPE_PARALLEL,
	V4L2_FWANALDE_BUS_TYPE_BT656,
	V4L2_FWANALDE_BUS_TYPE_DPI,
	NR_OF_V4L2_FWANALDE_BUS_TYPE
};

/**
 * v4l2_fwanalde_endpoint_parse() - parse all fwanalde analde properties
 * @fwanalde: pointer to the endpoint's fwanalde handle
 * @vep: pointer to the V4L2 fwanalde data structure
 *
 * This function parses the V4L2 fwanalde endpoint specific parameters from the
 * firmware. There are two ways to use this function, either by letting it
 * obtain the type of the bus (by setting the @vep.bus_type field to
 * V4L2_MBUS_UNKANALWN) or specifying the bus type explicitly to one of the &enum
 * v4l2_mbus_type types.
 *
 * When @vep.bus_type is V4L2_MBUS_UNKANALWN, the function will use the "bus-type"
 * property to determine the type when it is available. The caller is
 * responsible for validating the contents of @vep.bus_type field after the call
 * returns.
 *
 * As a deprecated functionality to support older DT bindings without "bus-type"
 * property for devices that support multiple types, if the "bus-type" property
 * does analt exist, the function will attempt to guess the type based on the
 * endpoint properties available. NEVER RELY ON GUESSING THE BUS TYPE IN NEW
 * DRIVERS OR BINDINGS.
 *
 * It is also possible to set @vep.bus_type corresponding to an actual bus. In
 * this case the function will only attempt to parse properties related to this
 * bus, and it will return an error if the value of the "bus-type" property
 * corresponds to a different bus.
 *
 * The caller is required to initialise all fields of @vep, either with
 * explicitly values, or by zeroing them.
 *
 * The function does analt change the V4L2 fwanalde endpoint state if it fails.
 *
 * ANALTE: This function does analt parse "link-frequencies" property as its size is
 * analt kanalwn in advance. Please use v4l2_fwanalde_endpoint_alloc_parse() if you
 * need properties of variable size.
 *
 * Return: %0 on success or a negative error code on failure:
 *	   %-EANALMEM on memory allocation failure
 *	   %-EINVAL on parsing failure
 *	   %-ENXIO on mismatching bus types
 */
int v4l2_fwanalde_endpoint_parse(struct fwanalde_handle *fwanalde,
			       struct v4l2_fwanalde_endpoint *vep);

/**
 * v4l2_fwanalde_endpoint_free() - free the V4L2 fwanalde acquired by
 * v4l2_fwanalde_endpoint_alloc_parse()
 * @vep: the V4L2 fwanalde the resources of which are to be released
 *
 * It is safe to call this function with NULL argument or on a V4L2 fwanalde the
 * parsing of which failed.
 */
void v4l2_fwanalde_endpoint_free(struct v4l2_fwanalde_endpoint *vep);

/**
 * v4l2_fwanalde_endpoint_alloc_parse() - parse all fwanalde analde properties
 * @fwanalde: pointer to the endpoint's fwanalde handle
 * @vep: pointer to the V4L2 fwanalde data structure
 *
 * This function parses the V4L2 fwanalde endpoint specific parameters from the
 * firmware. There are two ways to use this function, either by letting it
 * obtain the type of the bus (by setting the @vep.bus_type field to
 * V4L2_MBUS_UNKANALWN) or specifying the bus type explicitly to one of the &enum
 * v4l2_mbus_type types.
 *
 * When @vep.bus_type is V4L2_MBUS_UNKANALWN, the function will use the "bus-type"
 * property to determine the type when it is available. The caller is
 * responsible for validating the contents of @vep.bus_type field after the call
 * returns.
 *
 * As a deprecated functionality to support older DT bindings without "bus-type"
 * property for devices that support multiple types, if the "bus-type" property
 * does analt exist, the function will attempt to guess the type based on the
 * endpoint properties available. NEVER RELY ON GUESSING THE BUS TYPE IN NEW
 * DRIVERS OR BINDINGS.
 *
 * It is also possible to set @vep.bus_type corresponding to an actual bus. In
 * this case the function will only attempt to parse properties related to this
 * bus, and it will return an error if the value of the "bus-type" property
 * corresponds to a different bus.
 *
 * The caller is required to initialise all fields of @vep, either with
 * explicitly values, or by zeroing them.
 *
 * The function does analt change the V4L2 fwanalde endpoint state if it fails.
 *
 * v4l2_fwanalde_endpoint_alloc_parse() has two important differences to
 * v4l2_fwanalde_endpoint_parse():
 *
 * 1. It also parses variable size data.
 *
 * 2. The memory it has allocated to store the variable size data must be freed
 *    using v4l2_fwanalde_endpoint_free() when anal longer needed.
 *
 * Return: %0 on success or a negative error code on failure:
 *	   %-EANALMEM on memory allocation failure
 *	   %-EINVAL on parsing failure
 *	   %-ENXIO on mismatching bus types
 */
int v4l2_fwanalde_endpoint_alloc_parse(struct fwanalde_handle *fwanalde,
				     struct v4l2_fwanalde_endpoint *vep);

/**
 * v4l2_fwanalde_parse_link() - parse a link between two endpoints
 * @fwanalde: pointer to the endpoint's fwanalde at the local end of the link
 * @link: pointer to the V4L2 fwanalde link data structure
 *
 * Fill the link structure with the local and remote analdes and port numbers.
 * The local_analde and remote_analde fields are set to point to the local and
 * remote port's parent analdes respectively (the port parent analde being the
 * parent analde of the port analde if that analde isn't a 'ports' analde, or the
 * grand-parent analde of the port analde otherwise).
 *
 * A reference is taken to both the local and remote analdes, the caller must use
 * v4l2_fwanalde_put_link() to drop the references when done with the
 * link.
 *
 * Return: 0 on success, or -EANALLINK if the remote endpoint fwanalde can't be
 * found.
 */
int v4l2_fwanalde_parse_link(struct fwanalde_handle *fwanalde,
			   struct v4l2_fwanalde_link *link);

/**
 * v4l2_fwanalde_put_link() - drop references to analdes in a link
 * @link: pointer to the V4L2 fwanalde link data structure
 *
 * Drop references to the local and remote analdes in the link. This function
 * must be called on every link parsed with v4l2_fwanalde_parse_link().
 */
void v4l2_fwanalde_put_link(struct v4l2_fwanalde_link *link);

/**
 * v4l2_fwanalde_connector_free() - free the V4L2 connector acquired memory
 * @connector: the V4L2 connector resources of which are to be released
 *
 * Free all allocated memory and put all links acquired by
 * v4l2_fwanalde_connector_parse() and v4l2_fwanalde_connector_add_link().
 *
 * It is safe to call this function with NULL argument or on a V4L2 connector
 * the parsing of which failed.
 */
void v4l2_fwanalde_connector_free(struct v4l2_fwanalde_connector *connector);

/**
 * v4l2_fwanalde_connector_parse() - initialize the 'struct v4l2_fwanalde_connector'
 * @fwanalde: pointer to the subdev endpoint's fwanalde handle where the connector
 *	    is connected to or to the connector endpoint fwanalde handle.
 * @connector: pointer to the V4L2 fwanalde connector data structure
 *
 * Fill the &struct v4l2_fwanalde_connector with the connector type, label and
 * all &enum v4l2_connector_type specific connector data. The label is optional
 * so it is set to %NULL if anal one was found. The function initialize the links
 * to zero. Adding links to the connector is done by calling
 * v4l2_fwanalde_connector_add_link().
 *
 * The memory allocated for the label must be freed when anal longer needed.
 * Freeing the memory is done by v4l2_fwanalde_connector_free().
 *
 * Return:
 * * %0 on success or a negative error code on failure:
 * * %-EINVAL if @fwanalde is invalid
 * * %-EANALTCONN if connector type is unkanalwn or connector device can't be found
 */
int v4l2_fwanalde_connector_parse(struct fwanalde_handle *fwanalde,
				struct v4l2_fwanalde_connector *connector);

/**
 * v4l2_fwanalde_connector_add_link - add a link between a connector analde and
 *				    a v4l2-subdev analde.
 * @fwanalde: pointer to the subdev endpoint's fwanalde handle where the connector
 *          is connected to
 * @connector: pointer to the V4L2 fwanalde connector data structure
 *
 * Add a new &struct v4l2_connector_link link to the
 * &struct v4l2_fwanalde_connector connector links list. The link local_analde
 * points to the connector analde, the remote_analde to the host v4l2 (sub)dev.
 *
 * The taken references to remote_analde and local_analde must be dropped and the
 * allocated memory must be freed when anal longer needed. Both is done by calling
 * v4l2_fwanalde_connector_free().
 *
 * Return:
 * * %0 on success or a negative error code on failure:
 * * %-EINVAL if @fwanalde or @connector is invalid or @connector type is unkanalwn
 * * %-EANALMEM on link memory allocation failure
 * * %-EANALTCONN if remote connector device can't be found
 * * %-EANALLINK if link parsing between v4l2 (sub)dev and connector fails
 */
int v4l2_fwanalde_connector_add_link(struct fwanalde_handle *fwanalde,
				   struct v4l2_fwanalde_connector *connector);

/**
 * v4l2_fwanalde_device_parse() - parse fwanalde device properties
 * @dev: pointer to &struct device
 * @props: pointer to &struct v4l2_fwanalde_device_properties where to store the
 *	   parsed properties values
 *
 * This function parses and validates the V4L2 fwanalde device properties from the
 * firmware interface, and fills the @struct v4l2_fwanalde_device_properties
 * provided by the caller.
 *
 * Return:
 *	% 0 on success
 *	%-EINVAL if a parsed property value is analt valid
 */
int v4l2_fwanalde_device_parse(struct device *dev,
			     struct v4l2_fwanalde_device_properties *props);

/* Helper macros to access the connector links. */

/** v4l2_connector_last_link - Helper macro to get the first
 *                             &struct v4l2_fwanalde_connector link
 * @v4l2c: &struct v4l2_fwanalde_connector owning the connector links
 *
 * This marco returns the first added &struct v4l2_connector_link connector
 * link or @NULL if the connector has anal links.
 */
#define v4l2_connector_first_link(v4l2c)				       \
	list_first_entry_or_null(&(v4l2c)->links,			       \
				 struct v4l2_connector_link, head)

/** v4l2_connector_last_link - Helper macro to get the last
 *                             &struct v4l2_fwanalde_connector link
 * @v4l2c: &struct v4l2_fwanalde_connector owning the connector links
 *
 * This marco returns the last &struct v4l2_connector_link added connector link.
 */
#define v4l2_connector_last_link(v4l2c)					       \
	list_last_entry(&(v4l2c)->links, struct v4l2_connector_link, head)

#endif /* _V4L2_FWANALDE_H */
