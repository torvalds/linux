/* Copyright (c) 2012, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _LINUX_CORESIGHT_H
#define _LINUX_CORESIGHT_H

#include <linux/device.h>

/* Peripheral id registers (0xFD0-0xFEC) */
#define CORESIGHT_PERIPHIDR4	0xfd0
#define CORESIGHT_PERIPHIDR5	0xfd4
#define CORESIGHT_PERIPHIDR6	0xfd8
#define CORESIGHT_PERIPHIDR7	0xfdC
#define CORESIGHT_PERIPHIDR0	0xfe0
#define CORESIGHT_PERIPHIDR1	0xfe4
#define CORESIGHT_PERIPHIDR2	0xfe8
#define CORESIGHT_PERIPHIDR3	0xfeC
/* Component id registers (0xFF0-0xFFC) */
#define CORESIGHT_COMPIDR0	0xff0
#define CORESIGHT_COMPIDR1	0xff4
#define CORESIGHT_COMPIDR2	0xff8
#define CORESIGHT_COMPIDR3	0xffC

#define ETM_ARCH_V3_3		0x23
#define ETM_ARCH_V3_5		0x25
#define PFT_ARCH_V1_0		0x30
#define PFT_ARCH_V1_1		0x31

#define CORESIGHT_UNLOCK	0xc5acce55

extern struct bus_type coresight_bustype;

enum coresight_dev_type {
	CORESIGHT_DEV_TYPE_NONE,
	CORESIGHT_DEV_TYPE_SINK,
	CORESIGHT_DEV_TYPE_LINK,
	CORESIGHT_DEV_TYPE_LINKSINK,
	CORESIGHT_DEV_TYPE_SOURCE,
};

enum coresight_dev_subtype_sink {
	CORESIGHT_DEV_SUBTYPE_SINK_NONE,
	CORESIGHT_DEV_SUBTYPE_SINK_PORT,
	CORESIGHT_DEV_SUBTYPE_SINK_BUFFER,
};

enum coresight_dev_subtype_link {
	CORESIGHT_DEV_SUBTYPE_LINK_NONE,
	CORESIGHT_DEV_SUBTYPE_LINK_MERG,
	CORESIGHT_DEV_SUBTYPE_LINK_SPLIT,
	CORESIGHT_DEV_SUBTYPE_LINK_FIFO,
};

enum coresight_dev_subtype_source {
	CORESIGHT_DEV_SUBTYPE_SOURCE_NONE,
	CORESIGHT_DEV_SUBTYPE_SOURCE_PROC,
	CORESIGHT_DEV_SUBTYPE_SOURCE_BUS,
	CORESIGHT_DEV_SUBTYPE_SOURCE_SOFTWARE,
};

/**
 * struct coresight_dev_subtype - further characterisation of a type
 * @sink_subtype:	type of sink this component is, as defined
			by @coresight_dev_subtype_sink.
 * @link_subtype:	type of link this component is, as defined
			by @coresight_dev_subtype_link.
 * @source_subtype:	type of source this component is, as defined
			by @coresight_dev_subtype_source.
 */
struct coresight_dev_subtype {
	enum coresight_dev_subtype_sink sink_subtype;
	enum coresight_dev_subtype_link link_subtype;
	enum coresight_dev_subtype_source source_subtype;
};

/**
 * struct coresight_platform_data - data harvested from the DT specification
 * @cpu:	the CPU a source belongs to. Only applicable for ETM/PTMs.
 * @name:	name of the component as shown under sysfs.
 * @nr_inport:	number of input ports for this component.
 * @outports:	list of remote enpoint port number.
 * @child_names:name of all child components connected to this device.
 * @child_ports:child component port number the current component is
		connected  to.
 * @nr_outport:	number of output ports for this component.
 * @clk:	The clock this component is associated to.
 */
struct coresight_platform_data {
	int cpu;
	const char *name;
	int nr_inport;
	int *outports;
	const char **child_names;
	int *child_ports;
	int nr_outport;
	struct clk *clk;
};

/**
 * struct coresight_desc - description of a component required from drivers
 * @type:	as defined by @coresight_dev_type.
 * @subtype:	as defined by @coresight_dev_subtype.
 * @ops:	generic operations for this component, as defined
		by @coresight_ops.
 * @pdata:	platform data collected from DT.
 * @dev:	The device entity associated to this component.
 * @groups	:operations specific to this component. These will end up
		in the component's sysfs sub-directory.
 */
struct coresight_desc {
	enum coresight_dev_type type;
	struct coresight_dev_subtype subtype;
	const struct coresight_ops *ops;
	struct coresight_platform_data *pdata;
	struct device *dev;
	const struct attribute_group **groups;
};

/**
 * struct coresight_connection - representation of a single connection
 * @ref_count:	keeping count a port' references.
 * @outport:	a connection's output port number.
 * @chid_name:	remote component's name.
 * @child_port:	remote component's port number @output is connected to.
 * @child_dev:	a @coresight_device representation of the component
		connected to @outport.
 */
struct coresight_connection {
	int outport;
	const char *child_name;
	int child_port;
	struct coresight_device *child_dev;
};

/**
 * struct coresight_device - representation of a device as used by the framework
 * @nr_inport:	number of input port associated to this component.
 * @nr_outport:	number of output port associated to this component.
 * @type:	as defined by @coresight_dev_type.
 * @subtype:	as defined by @coresight_dev_subtype.
 * @ops:	generic operations for this component, as defined
		by @coresight_ops.
 * @dev:	The device entity associated to this component.
 * @refcnt:	keep track of what is in use.
 * @path_link:	link of current component into the path being enabled.
 * @orphan:	true if the component has connections that haven't been linked.
 * @enable:	'true' if component is currently part of an active path.
 * @activated:	'true' only if a _sink_ has been activated.  A sink can be
		activated but not yet enabled.  Enabling for a _sink_
		happens when a source has been selected for that it.
 */
struct coresight_device {
	struct coresight_connection *conns;
	int nr_inport;
	int nr_outport;
	enum coresight_dev_type type;
	struct coresight_dev_subtype subtype;
	const struct coresight_ops *ops;
	struct device dev;
	atomic_t *refcnt;
	struct list_head path_link;
	bool orphan;
	bool enable;	/* true only if configured as part of a path */
	bool activated;	/* true only if a sink is part of a path */
};

#define to_coresight_device(d) container_of(d, struct coresight_device, dev)

#define source_ops(csdev)	csdev->ops->source_ops
#define sink_ops(csdev)		csdev->ops->sink_ops
#define link_ops(csdev)		csdev->ops->link_ops

/**
 * struct coresight_ops_sink - basic operations for a sink
 * Operations available for sinks
 * @enable:	enables the sink.
 * @disable:	disables the sink.
 */
struct coresight_ops_sink {
	int (*enable)(struct coresight_device *csdev);
	void (*disable)(struct coresight_device *csdev);
};

/**
 * struct coresight_ops_link - basic operations for a link
 * Operations available for links.
 * @enable:	enables flow between iport and oport.
 * @disable:	disables flow between iport and oport.
 */
struct coresight_ops_link {
	int (*enable)(struct coresight_device *csdev, int iport, int oport);
	void (*disable)(struct coresight_device *csdev, int iport, int oport);
};

/**
 * struct coresight_ops_source - basic operations for a source
 * Operations available for sources.
 * @trace_id:	returns the value of the component's trace ID as known
		to the HW.
 * @enable:	enables tracing from a source.
 * @disable:	disables tracing for a source.
 */
struct coresight_ops_source {
	int (*trace_id)(struct coresight_device *csdev);
	int (*enable)(struct coresight_device *csdev);
	void (*disable)(struct coresight_device *csdev);
};

struct coresight_ops {
	const struct coresight_ops_sink *sink_ops;
	const struct coresight_ops_link *link_ops;
	const struct coresight_ops_source *source_ops;
};

#ifdef CONFIG_CORESIGHT
extern struct coresight_device *
coresight_register(struct coresight_desc *desc);
extern void coresight_unregister(struct coresight_device *csdev);
extern int coresight_enable(struct coresight_device *csdev);
extern void coresight_disable(struct coresight_device *csdev);
extern int coresight_timeout(void __iomem *addr, u32 offset,
			     int position, int value);
#else
static inline struct coresight_device *
coresight_register(struct coresight_desc *desc) { return NULL; }
static inline void coresight_unregister(struct coresight_device *csdev) {}
static inline int
coresight_enable(struct coresight_device *csdev) { return -ENOSYS; }
static inline void coresight_disable(struct coresight_device *csdev) {}
static inline int coresight_timeout(void __iomem *addr, u32 offset,
				     int position, int value) { return 1; }
#endif

#ifdef CONFIG_OF
extern struct coresight_platform_data *of_get_coresight_platform_data(
				struct device *dev, struct device_node *node);
#else
static inline struct coresight_platform_data *of_get_coresight_platform_data(
	struct device *dev, struct device_node *node) { return NULL; }
#endif

#endif
