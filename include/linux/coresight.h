/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2012, The Linux Foundation. All rights reserved.
 */

#ifndef _LINUX_CORESIGHT_H
#define _LINUX_CORESIGHT_H

#include <linux/amba/bus.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
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

extern const struct bus_type coresight_bustype;

enum coresight_dev_type {
	CORESIGHT_DEV_TYPE_SINK,
	CORESIGHT_DEV_TYPE_LINK,
	CORESIGHT_DEV_TYPE_LINKSINK,
	CORESIGHT_DEV_TYPE_SOURCE,
	CORESIGHT_DEV_TYPE_HELPER,
	CORESIGHT_DEV_TYPE_MAX
};

enum coresight_dev_subtype_sink {
	CORESIGHT_DEV_SUBTYPE_SINK_DUMMY,
	CORESIGHT_DEV_SUBTYPE_SINK_PORT,
	CORESIGHT_DEV_SUBTYPE_SINK_BUFFER,
	CORESIGHT_DEV_SUBTYPE_SINK_SYSMEM,
	CORESIGHT_DEV_SUBTYPE_SINK_PERCPU_SYSMEM,
};

enum coresight_dev_subtype_link {
	CORESIGHT_DEV_SUBTYPE_LINK_MERG,
	CORESIGHT_DEV_SUBTYPE_LINK_SPLIT,
	CORESIGHT_DEV_SUBTYPE_LINK_FIFO,
};

enum coresight_dev_subtype_source {
	CORESIGHT_DEV_SUBTYPE_SOURCE_PROC,
	CORESIGHT_DEV_SUBTYPE_SOURCE_BUS,
	CORESIGHT_DEV_SUBTYPE_SOURCE_SOFTWARE,
	CORESIGHT_DEV_SUBTYPE_SOURCE_TPDM,
	CORESIGHT_DEV_SUBTYPE_SOURCE_OTHERS,
};

enum coresight_dev_subtype_helper {
	CORESIGHT_DEV_SUBTYPE_HELPER_CATU,
	CORESIGHT_DEV_SUBTYPE_HELPER_ECT_CTI
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
 */
union coresight_dev_subtype {
	/* We have some devices which acts as LINK and SINK */
	struct {
		enum coresight_dev_subtype_sink sink_subtype;
		enum coresight_dev_subtype_link link_subtype;
	};
	enum coresight_dev_subtype_source source_subtype;
	enum coresight_dev_subtype_helper helper_subtype;
};

/**
 * struct coresight_platform_data - data harvested from the firmware
 * specification.
 *
 * @nr_inconns: Number of elements for the input connections.
 * @nr_outconns: Number of elements for the output connections.
 * @out_conns: Array of nr_outconns pointers to connections from this
 *	       component.
 * @in_conns: Sparse array of pointers to input connections. Sparse
 *            because the source device owns the connection so when it's
 *            unloaded the connection leaves an empty slot.
 */
struct coresight_platform_data {
	int nr_inconns;
	int nr_outconns;
	struct coresight_connection **out_conns;
	struct coresight_connection **in_conns;
};

/**
 * struct csdev_access - Abstraction of a CoreSight device access.
 *
 * @io_mem	: True if the device has memory mapped I/O
 * @base	: When io_mem == true, base address of the component
 * @read	: Read from the given "offset" of the given instance.
 * @write	: Write "val" to the given "offset".
 */
struct csdev_access {
	bool io_mem;
	union {
		void __iomem *base;
		struct {
			u64 (*read)(u32 offset, bool relaxed, bool _64bit);
			void (*write)(u64 val, u32 offset, bool relaxed,
				      bool _64bit);
		};
	};
};

#define CSDEV_ACCESS_IOMEM(_addr)		\
	((struct csdev_access)	{		\
		.io_mem		= true,		\
		.base		= (_addr),	\
	})

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
 * @access:	Describe access to the device
 */
struct coresight_desc {
	enum coresight_dev_type type;
	union coresight_dev_subtype subtype;
	const struct coresight_ops *ops;
	struct coresight_platform_data *pdata;
	struct device *dev;
	const struct attribute_group **groups;
	const char *name;
	struct csdev_access access;
};

/**
 * struct coresight_connection - representation of a single connection
 * @src_port:	a connection's output port number.
 * @dest_port:	destination's input port number @src_port is connected to.
 * @dest_fwnode: destination component's fwnode handle.
 * @dest_dev:	a @coresight_device representation of the component
		connected to @src_port. NULL until the device is created
 * @link: Representation of the connection as a sysfs link.
 *
 * The full connection structure looks like this, where in_conns store
 * references to same connection as the source device's out_conns.
 *
 * +-----------------------------+   +-----------------------------+
 * |coresight_device             |   |coresight_connection         |
 * |-----------------------------|   |-----------------------------|
 * |                             |   |                             |
 * |                             |   |                    dest_dev*|<--
 * |pdata->out_conns[nr_outconns]|<->|src_dev*                     |   |
 * |                             |   |                             |   |
 * +-----------------------------+   +-----------------------------+   |
 *                                                                     |
 *                                   +-----------------------------+   |
 *                                   |coresight_device             |   |
 *                                   |------------------------------   |
 *                                   |                             |   |
 *                                   |  pdata->in_conns[nr_inconns]|<--
 *                                   |                             |
 *                                   +-----------------------------+
 */
struct coresight_connection {
	int src_port;
	int dest_port;
	struct fwnode_handle *dest_fwnode;
	struct coresight_device *dest_dev;
	struct coresight_sysfs_link *link;
	struct coresight_device *src_dev;
	atomic_t src_refcnt;
	atomic_t dest_refcnt;
};

/**
 * struct coresight_sysfs_link - representation of a connection in sysfs.
 * @orig:		Originating (master) coresight device for the link.
 * @orig_name:		Name to use for the link orig->target.
 * @target:		Target (slave) coresight device for the link.
 * @target_name:	Name to use for the link target->orig.
 */
struct coresight_sysfs_link {
	struct coresight_device *orig;
	const char *orig_name;
	struct coresight_device *target;
	const char *target_name;
};

/**
 * struct coresight_device - representation of a device as used by the framework
 * @pdata:	Platform data with device connections associated to this device.
 * @type:	as defined by @coresight_dev_type.
 * @subtype:	as defined by @coresight_dev_subtype.
 * @ops:	generic operations for this component, as defined
 *		by @coresight_ops.
 * @access:	Device i/o access abstraction for this device.
 * @dev:	The device entity associated to this component.
 * @mode:	This tracer's mode, i.e sysFS, Perf or disabled. This is
 *		actually an 'enum cs_mode', but is stored in an atomic type.
 *		This is always accessed through local_read() and local_set(),
 *		but wherever it's done from within the Coresight device's lock,
 *		a non-atomic read would also work. This is the main point of
 *		synchronisation between code happening inside the sysfs mode's
 *		coresight_mutex and outside when running in Perf mode. A compare
 *		and exchange swap is done to atomically claim one mode or the
 *		other.
 * @refcnt:	keep track of what is in use. Only access this outside of the
 *		device's spinlock when the coresight_mutex held and mode ==
 *		CS_MODE_SYSFS. Otherwise it must be accessed from inside the
 *		spinlock.
 * @orphan:	true if the component has connections that haven't been linked.
 * @sysfs_sink_activated: 'true' when a sink has been selected for use via sysfs
 *		by writing a 1 to the 'enable_sink' file.  A sink can be
 *		activated but not yet enabled.  Enabling for a _sink_ happens
 *		when a source has been selected and a path is enabled from
 *		source to that sink. A sink can also become enabled but not
 *		activated if it's used via Perf.
 * @ea:		Device attribute for sink representation under PMU directory.
 * @def_sink:	cached reference to default sink found for this device.
 * @nr_links:   number of sysfs links created to other components from this
 *		device. These will appear in the "connections" group.
 * @has_conns_grp: Have added a "connections" group for sysfs links.
 * @feature_csdev_list: List of complex feature programming added to the device.
 * @config_csdev_list:  List of system configurations added to the device.
 * @cscfg_csdev_lock:	Protect the lists of configurations and features.
 * @active_cscfg_ctxt:  Context information for current active system configuration.
 */
struct coresight_device {
	struct coresight_platform_data *pdata;
	enum coresight_dev_type type;
	union coresight_dev_subtype subtype;
	const struct coresight_ops *ops;
	struct csdev_access access;
	struct device dev;
	local_t	mode;
	int refcnt;
	bool orphan;
	/* sink specific fields */
	bool sysfs_sink_activated;
	struct dev_ext_attribute *ea;
	struct coresight_device *def_sink;
	/* sysfs links between components */
	int nr_links;
	bool has_conns_grp;
	/* system configuration and feature lists */
	struct list_head feature_csdev_list;
	struct list_head config_csdev_list;
	spinlock_t cscfg_csdev_lock;
	void *active_cscfg_ctxt;
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

enum cs_mode {
	CS_MODE_DISABLED,
	CS_MODE_SYSFS,
	CS_MODE_PERF,
};

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
	int (*enable)(struct coresight_device *csdev, enum cs_mode mode,
		      void *data);
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
	int (*enable)(struct coresight_device *csdev,
		      struct coresight_connection *in,
		      struct coresight_connection *out);
	void (*disable)(struct coresight_device *csdev,
			struct coresight_connection *in,
			struct coresight_connection *out);
};

/**
 * struct coresight_ops_source - basic operations for a source
 * Operations available for sources.
 * @cpu_id:	returns the value of the CPU number this component
 *		is associated to.
 * @enable:	enables tracing for a source.
 * @disable:	disables tracing for a source.
 */
struct coresight_ops_source {
	int (*cpu_id)(struct coresight_device *csdev);
	int (*enable)(struct coresight_device *csdev, struct perf_event *event,
		      enum cs_mode mode);
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
	int (*enable)(struct coresight_device *csdev, enum cs_mode mode,
		      void *data);
	int (*disable)(struct coresight_device *csdev, void *data);
};

struct coresight_ops {
	const struct coresight_ops_sink *sink_ops;
	const struct coresight_ops_link *link_ops;
	const struct coresight_ops_source *source_ops;
	const struct coresight_ops_helper *helper_ops;
};

static inline u32 csdev_access_relaxed_read32(struct csdev_access *csa,
					      u32 offset)
{
	if (likely(csa->io_mem))
		return readl_relaxed(csa->base + offset);

	return csa->read(offset, true, false);
}

#define CORESIGHT_CIDRn(i)	(0xFF0 + ((i) * 4))

static inline u32 coresight_get_cid(void __iomem *base)
{
	u32 i, cid = 0;

	for (i = 0; i < 4; i++)
		cid |= readl(base + CORESIGHT_CIDRn(i)) << (i * 8);

	return cid;
}

static inline bool is_coresight_device(void __iomem *base)
{
	u32 cid = coresight_get_cid(base);

	return cid == CORESIGHT_CID;
}

/*
 * Attempt to find and enable "APB clock" for the given device
 *
 * Returns:
 *
 * clk   - Clock is found and enabled
 * NULL  - clock is not found
 * ERROR - Clock is found but failed to enable
 */
static inline struct clk *coresight_get_enable_apb_pclk(struct device *dev)
{
	struct clk *pclk;
	int ret;

	pclk = clk_get(dev, "apb_pclk");
	if (IS_ERR(pclk))
		return NULL;

	ret = clk_prepare_enable(pclk);
	if (ret) {
		clk_put(pclk);
		return ERR_PTR(ret);
	}
	return pclk;
}

#define CORESIGHT_PIDRn(i)	(0xFE0 + ((i) * 4))

static inline u32 coresight_get_pid(struct csdev_access *csa)
{
	u32 i, pid = 0;

	for (i = 0; i < 4; i++)
		pid |= csdev_access_relaxed_read32(csa, CORESIGHT_PIDRn(i)) << (i * 8);

	return pid;
}

static inline u64 csdev_access_relaxed_read_pair(struct csdev_access *csa,
						 u32 lo_offset, u32 hi_offset)
{
	if (likely(csa->io_mem)) {
		return readl_relaxed(csa->base + lo_offset) |
			((u64)readl_relaxed(csa->base + hi_offset) << 32);
	}

	return csa->read(lo_offset, true, false) | (csa->read(hi_offset, true, false) << 32);
}

static inline void csdev_access_relaxed_write_pair(struct csdev_access *csa, u64 val,
						   u32 lo_offset, u32 hi_offset)
{
	if (likely(csa->io_mem)) {
		writel_relaxed((u32)val, csa->base + lo_offset);
		writel_relaxed((u32)(val >> 32), csa->base + hi_offset);
	} else {
		csa->write((u32)val, lo_offset, true, false);
		csa->write((u32)(val >> 32), hi_offset, true, false);
	}
}

static inline u32 csdev_access_read32(struct csdev_access *csa, u32 offset)
{
	if (likely(csa->io_mem))
		return readl(csa->base + offset);

	return csa->read(offset, false, false);
}

static inline void csdev_access_relaxed_write32(struct csdev_access *csa,
						u32 val, u32 offset)
{
	if (likely(csa->io_mem))
		writel_relaxed(val, csa->base + offset);
	else
		csa->write(val, offset, true, false);
}

static inline void csdev_access_write32(struct csdev_access *csa, u32 val, u32 offset)
{
	if (likely(csa->io_mem))
		writel(val, csa->base + offset);
	else
		csa->write(val, offset, false, false);
}

#ifdef CONFIG_64BIT

static inline u64 csdev_access_relaxed_read64(struct csdev_access *csa,
					      u32 offset)
{
	if (likely(csa->io_mem))
		return readq_relaxed(csa->base + offset);

	return csa->read(offset, true, true);
}

static inline u64 csdev_access_read64(struct csdev_access *csa, u32 offset)
{
	if (likely(csa->io_mem))
		return readq(csa->base + offset);

	return csa->read(offset, false, true);
}

static inline void csdev_access_relaxed_write64(struct csdev_access *csa,
						u64 val, u32 offset)
{
	if (likely(csa->io_mem))
		writeq_relaxed(val, csa->base + offset);
	else
		csa->write(val, offset, true, true);
}

static inline void csdev_access_write64(struct csdev_access *csa, u64 val, u32 offset)
{
	if (likely(csa->io_mem))
		writeq(val, csa->base + offset);
	else
		csa->write(val, offset, false, true);
}

#else	/* !CONFIG_64BIT */

static inline u64 csdev_access_relaxed_read64(struct csdev_access *csa,
					      u32 offset)
{
	WARN_ON(1);
	return 0;
}

static inline u64 csdev_access_read64(struct csdev_access *csa, u32 offset)
{
	WARN_ON(1);
	return 0;
}

static inline void csdev_access_relaxed_write64(struct csdev_access *csa,
						u64 val, u32 offset)
{
	WARN_ON(1);
}

static inline void csdev_access_write64(struct csdev_access *csa, u64 val, u32 offset)
{
	WARN_ON(1);
}
#endif	/* CONFIG_64BIT */

static inline bool coresight_is_percpu_source(struct coresight_device *csdev)
{
	return csdev && (csdev->type == CORESIGHT_DEV_TYPE_SOURCE) &&
	       (csdev->subtype.source_subtype == CORESIGHT_DEV_SUBTYPE_SOURCE_PROC);
}

static inline bool coresight_is_percpu_sink(struct coresight_device *csdev)
{
	return csdev && (csdev->type == CORESIGHT_DEV_TYPE_SINK) &&
	       (csdev->subtype.sink_subtype == CORESIGHT_DEV_SUBTYPE_SINK_PERCPU_SYSMEM);
}

/*
 * Atomically try to take the device and set a new mode. Returns true on
 * success, false if the device is already taken by someone else.
 */
static inline bool coresight_take_mode(struct coresight_device *csdev,
				       enum cs_mode new_mode)
{
	return local_cmpxchg(&csdev->mode, CS_MODE_DISABLED, new_mode) ==
	       CS_MODE_DISABLED;
}

static inline enum cs_mode coresight_get_mode(struct coresight_device *csdev)
{
	return local_read(&csdev->mode);
}

static inline void coresight_set_mode(struct coresight_device *csdev,
				      enum cs_mode new_mode)
{
	enum cs_mode current_mode = coresight_get_mode(csdev);

	/*
	 * Changing to a new mode must be done from an already disabled state
	 * unless it's synchronized with coresight_take_mode(). Otherwise the
	 * device is already in use and signifies a locking issue.
	 */
	WARN(new_mode != CS_MODE_DISABLED && current_mode != CS_MODE_DISABLED &&
	     current_mode != new_mode, "Device already in use\n");

	local_set(&csdev->mode, new_mode);
}

extern struct coresight_device *
coresight_register(struct coresight_desc *desc);
extern void coresight_unregister(struct coresight_device *csdev);
extern int coresight_enable_sysfs(struct coresight_device *csdev);
extern void coresight_disable_sysfs(struct coresight_device *csdev);
extern int coresight_timeout(struct csdev_access *csa, u32 offset,
			     int position, int value);

extern int coresight_claim_device(struct coresight_device *csdev);
extern int coresight_claim_device_unlocked(struct coresight_device *csdev);

extern void coresight_disclaim_device(struct coresight_device *csdev);
extern void coresight_disclaim_device_unlocked(struct coresight_device *csdev);
extern char *coresight_alloc_device_name(struct coresight_dev_list *devs,
					 struct device *dev);

extern bool coresight_loses_context_with_cpu(struct device *dev);

u32 coresight_relaxed_read32(struct coresight_device *csdev, u32 offset);
u32 coresight_read32(struct coresight_device *csdev, u32 offset);
void coresight_write32(struct coresight_device *csdev, u32 val, u32 offset);
void coresight_relaxed_write32(struct coresight_device *csdev,
			       u32 val, u32 offset);
u64 coresight_relaxed_read64(struct coresight_device *csdev, u32 offset);
u64 coresight_read64(struct coresight_device *csdev, u32 offset);
void coresight_relaxed_write64(struct coresight_device *csdev,
			       u64 val, u32 offset);
void coresight_write64(struct coresight_device *csdev, u64 val, u32 offset);

extern int coresight_get_cpu(struct device *dev);

struct coresight_platform_data *coresight_get_platform_data(struct device *dev);
struct coresight_connection *
coresight_add_out_conn(struct device *dev,
		       struct coresight_platform_data *pdata,
		       const struct coresight_connection *new_conn);
int coresight_add_in_conn(struct coresight_connection *conn);
struct coresight_device *
coresight_find_input_type(struct coresight_platform_data *pdata,
			  enum coresight_dev_type type,
			  union coresight_dev_subtype subtype);
struct coresight_device *
coresight_find_output_type(struct coresight_platform_data *pdata,
			   enum coresight_dev_type type,
			   union coresight_dev_subtype subtype);

#endif		/* _LINUX_COREISGHT_H */
