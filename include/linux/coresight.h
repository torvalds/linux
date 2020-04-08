/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2012, The Linux Foundation. All rights reserved.
 */

#ifndef _LINUX_CORESIGHT_H
#define _LINUX_CORESIGHT_H

#include <linux/device.h>
#include <linux/perf_event.h>
#include <linux/sched.h>

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
	CORESIGHT_DEV_TYPE_HELPER,
	CORESIGHT_DEV_TYPE_ECT,
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

enum coresight_dev_subtype_helper {
	CORESIGHT_DEV_SUBTYPE_HELPER_NONE,
	CORESIGHT_DEV_SUBTYPE_HELPER_CATU,
};

/* Embedded Cross Trigger (ECT) sub-types */
enum coresight_dev_subtype_ect {
	CORESIGHT_DEV_SUBTYPE_ECT_NONE,
	CORESIGHT_DEV_SUBTYPE_ECT_CTI,
};

/**
 * union coresight_dev_subtype - further characterisation of a type
 * @sink_subtype:	type of sink this component is, as defined
 *			by @coresight_dev_subtype_sink.
 * @link_subtype:	type of link this component is, as defined
 *			by @coresight_dev_subtype_link.
 * @source_subtype:	type of source this component is, as defined
 *			by @coresight_dev_subtype_source.
 * @helper_subtype:	type of helper this component is, as defined
 *			by @coresight_dev_subtype_helper.
 * @ect_subtype:        type of cross trigger this component is, as
 *			defined by @coresight_dev_subtype_ect
 */
union coresight_dev_subtype {
	/* We have some devices which acts as LINK and SINK */
	struct {
		enum coresight_dev_subtype_sink sink_subtype;
		enum coresight_dev_subtype_link link_subtype;
	};
	enum coresight_dev_subtype_source source_subtype;
	enum coresight_dev_subtype_helper helper_subtype;
	enum coresight_dev_subtype_ect ect_subtype;
};

/**
 * struct coresight_platform_data - data harvested from the DT specification
 * @nr_inport:	number of input ports for this component.
 * @nr_outport:	number of output ports for this component.
 * @conns:	Array of nr_outport connections from this component
 */
struct coresight_platform_data {
	int nr_inport;
	int nr_outport;
	struct coresight_connection *conns;
};

/**
 * struct coresight_desc - description of a component required from drivers
 * @type:	as defined by @coresight_dev_type.
 * @subtype:	as defined by @coresight_dev_subtype.
 * @ops:	generic operations for this component, as defined
 *		by @coresight_ops.
 * @pdata:	platform data collected from DT.
 * @dev:	The device entity associated to this component.
 * @groups:	operations specific to this component. These will end up
 *		in the component's sysfs sub-directory.
 * @name:	name for the coresight device, also shown under sysfs.
 */
struct coresight_desc {
	enum coresight_dev_type type;
	union coresight_dev_subtype subtype;
	const struct coresight_ops *ops;
	struct coresight_platform_data *pdata;
	struct device *dev;
	const struct attribute_group **groups;
	const char *name;
};

/**
 * struct coresight_connection - representation of a single connection
 * @outport:	a connection's output port number.
 * @child_port:	remote component's port number @output is connected to.
 * @chid_fwnode: remote component's fwnode handle.
 * @child_dev:	a @coresight_device representation of the component
		connected to @outport.
 */
struct coresight_connection {
	int outport;
	int child_port;
	struct fwnode_handle *child_fwnode;
	struct coresight_device *child_dev;
};

/**
 * struct coresight_device - representation of a device as used by the framework
 * @pdata:	Platform data with device connections associated to this device.
 * @type:	as defined by @coresight_dev_type.
 * @subtype:	as defined by @coresight_dev_subtype.
 * @ops:	generic operations for this component, as defined
		by @coresight_ops.
 * @dev:	The device entity associated to this component.
 * @refcnt:	keep track of what is in use.
 * @orphan:	true if the component has connections that haven't been linked.
 * @enable:	'true' if component is currently part of an active path.
 * @activated:	'true' only if a _sink_ has been activated.  A sink can be
 *		activated but not yet enabled.  Enabling for a _sink_
 *		appens when a source has been selected for that it.
 * @ea:		Device attribute for sink representation under PMU directory.
 * @ect_dev:	Associated cross trigger device. Not part of the trace data
 *		path or connections.
 */
struct coresight_device {
	struct coresight_platform_data *pdata;
	enum coresight_dev_type type;
	union coresight_dev_subtype subtype;
	const struct coresight_ops *ops;
	struct device dev;
	atomic_t *refcnt;
	bool orphan;
	bool enable;	/* true only if configured as part of a path */
	/* sink specific fields */
	bool activated;	/* true only if a sink is part of a path */
	struct dev_ext_attribute *ea;
	/* cross trigger handling */
	struct coresight_device *ect_dev;
};

/*
 * coresight_dev_list - Mapping for devices to "name" index for device
 * names.
 *
 * @nr_idx:		Number of entries already allocated.
 * @pfx:		Prefix pattern for device name.
 * @fwnode_list:	Array of fwnode_handles associated with each allocated
 *			index, upto nr_idx entries.
 */
struct coresight_dev_list {
	int			nr_idx;
	const char		*pfx;
	struct fwnode_handle	**fwnode_list;
};

#define DEFINE_CORESIGHT_DEVLIST(var, dev_pfx)				\
static struct coresight_dev_list (var) = {				\
						.pfx = dev_pfx,		\
						.nr_idx = 0,		\
						.fwnode_list = NULL,	\
}

#define to_coresight_device(d) container_of(d, struct coresight_device, dev)

#define source_ops(csdev)	csdev->ops->source_ops
#define sink_ops(csdev)		csdev->ops->sink_ops
#define link_ops(csdev)		csdev->ops->link_ops
#define helper_ops(csdev)	csdev->ops->helper_ops
#define ect_ops(csdev)		csdev->ops->ect_ops

/**
 * struct coresight_ops_sink - basic operations for a sink
 * Operations available for sinks
 * @enable:		enables the sink.
 * @disable:		disables the sink.
 * @alloc_buffer:	initialises perf's ring buffer for trace collection.
 * @free_buffer:	release memory allocated in @get_config.
 * @update_buffer:	update buffer pointers after a trace session.
 */
struct coresight_ops_sink {
	int (*enable)(struct coresight_device *csdev, u32 mode, void *data);
	int (*disable)(struct coresight_device *csdev);
	void *(*alloc_buffer)(struct coresight_device *csdev,
			      struct perf_event *event, void **pages,
			      int nr_pages, bool overwrite);
	void (*free_buffer)(void *config);
	unsigned long (*update_buffer)(struct coresight_device *csdev,
			      struct perf_output_handle *handle,
			      void *sink_config);
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
 * @cpu_id:	returns the value of the CPU number this component
 *		is associated to.
 * @trace_id:	returns the value of the component's trace ID as known
 *		to the HW.
 * @enable:	enables tracing for a source.
 * @disable:	disables tracing for a source.
 */
struct coresight_ops_source {
	int (*cpu_id)(struct coresight_device *csdev);
	int (*trace_id)(struct coresight_device *csdev);
	int (*enable)(struct coresight_device *csdev,
		      struct perf_event *event,  u32 mode);
	void (*disable)(struct coresight_device *csdev,
			struct perf_event *event);
};

/**
 * struct coresight_ops_helper - Operations for a helper device.
 *
 * All operations could pass in a device specific data, which could
 * help the helper device to determine what to do.
 *
 * @enable	: Enable the device
 * @disable	: Disable the device
 */
struct coresight_ops_helper {
	int (*enable)(struct coresight_device *csdev, void *data);
	int (*disable)(struct coresight_device *csdev, void *data);
};

/**
 * struct coresight_ops_ect - Ops for an embedded cross trigger device
 *
 * @enable	: Enable the device
 * @disable	: Disable the device
 */
struct coresight_ops_ect {
	int (*enable)(struct coresight_device *csdev);
	int (*disable)(struct coresight_device *csdev);
};

struct coresight_ops {
	const struct coresight_ops_sink *sink_ops;
	const struct coresight_ops_link *link_ops;
	const struct coresight_ops_source *source_ops;
	const struct coresight_ops_helper *helper_ops;
	const struct coresight_ops_ect *ect_ops;
};

#ifdef CONFIG_CORESIGHT
extern struct coresight_device *
coresight_register(struct coresight_desc *desc);
extern void coresight_unregister(struct coresight_device *csdev);
extern int coresight_enable(struct coresight_device *csdev);
extern void coresight_disable(struct coresight_device *csdev);
extern int coresight_timeout(void __iomem *addr, u32 offset,
			     int position, int value);

extern int coresight_claim_device(void __iomem *base);
extern int coresight_claim_device_unlocked(void __iomem *base);

extern void coresight_disclaim_device(void __iomem *base);
extern void coresight_disclaim_device_unlocked(void __iomem *base);
extern char *coresight_alloc_device_name(struct coresight_dev_list *devs,
					 struct device *dev);

extern bool coresight_loses_context_with_cpu(struct device *dev);
#else
static inline struct coresight_device *
coresight_register(struct coresight_desc *desc) { return NULL; }
static inline void coresight_unregister(struct coresight_device *csdev) {}
static inline int
coresight_enable(struct coresight_device *csdev) { return -ENOSYS; }
static inline void coresight_disable(struct coresight_device *csdev) {}
static inline int coresight_timeout(void __iomem *addr, u32 offset,
				     int position, int value) { return 1; }
static inline int coresight_claim_device_unlocked(void __iomem *base)
{
	return -EINVAL;
}

static inline int coresight_claim_device(void __iomem *base)
{
	return -EINVAL;
}

static inline void coresight_disclaim_device(void __iomem *base) {}
static inline void coresight_disclaim_device_unlocked(void __iomem *base) {}

static inline bool coresight_loses_context_with_cpu(struct device *dev)
{
	return false;
}
#endif

extern int coresight_get_cpu(struct device *dev);

struct coresight_platform_data *coresight_get_platform_data(struct device *dev);

#endif
