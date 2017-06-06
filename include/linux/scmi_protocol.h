// SPDX-License-Identifier: GPL-2.0
/*
 * SCMI Message Protocol driver header
 *
 * Copyright (C) 2018 ARM Ltd.
 */
#include <linux/device.h>
#include <linux/types.h>

#define SCMI_MAX_STR_SIZE	16
#define SCMI_MAX_NUM_RATES	16

/**
 * struct scmi_revision_info - version information structure
 *
 * @major_ver: Major ABI version. Change here implies risk of backward
 *	compatibility break.
 * @minor_ver: Minor ABI version. Change here implies new feature addition,
 *	or compatible change in ABI.
 * @num_protocols: Number of protocols that are implemented, excluding the
 *	base protocol.
 * @num_agents: Number of agents in the system.
 * @impl_ver: A vendor-specific implementation version.
 * @vendor_id: A vendor identifier(Null terminated ASCII string)
 * @sub_vendor_id: A sub-vendor identifier(Null terminated ASCII string)
 */
struct scmi_revision_info {
	u16 major_ver;
	u16 minor_ver;
	u8 num_protocols;
	u8 num_agents;
	u32 impl_ver;
	char vendor_id[SCMI_MAX_STR_SIZE];
	char sub_vendor_id[SCMI_MAX_STR_SIZE];
};

struct scmi_clock_info {
	char name[SCMI_MAX_STR_SIZE];
	bool rate_discrete;
	union {
		struct {
			int num_rates;
			u64 rates[SCMI_MAX_NUM_RATES];
		} list;
		struct {
			u64 min_rate;
			u64 max_rate;
			u64 step_size;
		} range;
	};
};

struct scmi_handle;

/**
 * struct scmi_clk_ops - represents the various operations provided
 *	by SCMI Clock Protocol
 *
 * @count_get: get the count of clocks provided by SCMI
 * @info_get: get the information of the specified clock
 * @rate_get: request the current clock rate of a clock
 * @rate_set: set the clock rate of a clock
 * @enable: enables the specified clock
 * @disable: disables the specified clock
 */
struct scmi_clk_ops {
	int (*count_get)(const struct scmi_handle *handle);

	const struct scmi_clock_info *(*info_get)
		(const struct scmi_handle *handle, u32 clk_id);
	int (*rate_get)(const struct scmi_handle *handle, u32 clk_id,
			u64 *rate);
	int (*rate_set)(const struct scmi_handle *handle, u32 clk_id,
			u32 config, u64 rate);
	int (*enable)(const struct scmi_handle *handle, u32 clk_id);
	int (*disable)(const struct scmi_handle *handle, u32 clk_id);
};

/**
 * struct scmi_perf_ops - represents the various operations provided
 *	by SCMI Performance Protocol
 *
 * @limits_set: sets limits on the performance level of a domain
 * @limits_get: gets limits on the performance level of a domain
 * @level_set: sets the performance level of a domain
 * @level_get: gets the performance level of a domain
 * @device_domain_id: gets the scmi domain id for a given device
 * @get_transition_latency: gets the DVFS transition latency for a given device
 * @add_opps_to_device: adds all the OPPs for a given device
 * @freq_set: sets the frequency for a given device using sustained frequency
 *	to sustained performance level mapping
 * @freq_get: gets the frequency for a given device using sustained frequency
 *	to sustained performance level mapping
 */
struct scmi_perf_ops {
	int (*limits_set)(const struct scmi_handle *handle, u32 domain,
			  u32 max_perf, u32 min_perf);
	int (*limits_get)(const struct scmi_handle *handle, u32 domain,
			  u32 *max_perf, u32 *min_perf);
	int (*level_set)(const struct scmi_handle *handle, u32 domain,
			 u32 level);
	int (*level_get)(const struct scmi_handle *handle, u32 domain,
			 u32 *level);
	int (*device_domain_id)(struct device *dev);
	int (*get_transition_latency)(const struct scmi_handle *handle,
				      struct device *dev);
	int (*add_opps_to_device)(const struct scmi_handle *handle,
				  struct device *dev);
	int (*freq_set)(const struct scmi_handle *handle, u32 domain,
			unsigned long rate);
	int (*freq_get)(const struct scmi_handle *handle, u32 domain,
			unsigned long *rate);
};

/**
 * struct scmi_handle - Handle returned to ARM SCMI clients for usage.
 *
 * @dev: pointer to the SCMI device
 * @version: pointer to the structure containing SCMI version information
 * @perf_ops: pointer to set of performance protocol operations
 * @clk_ops: pointer to set of clock protocol operations
 */
struct scmi_handle {
	struct device *dev;
	struct scmi_revision_info *version;
	struct scmi_perf_ops *perf_ops;
	struct scmi_clk_ops *clk_ops;
	/* for protocol internal use */
	void *perf_priv;
	void *clk_priv;
};

enum scmi_std_protocol {
	SCMI_PROTOCOL_BASE = 0x10,
	SCMI_PROTOCOL_POWER = 0x11,
	SCMI_PROTOCOL_SYSTEM = 0x12,
	SCMI_PROTOCOL_PERF = 0x13,
	SCMI_PROTOCOL_CLOCK = 0x14,
	SCMI_PROTOCOL_SENSOR = 0x15,
};

struct scmi_device {
	u32 id;
	u8 protocol_id;
	struct device dev;
	struct scmi_handle *handle;
};

#define to_scmi_dev(d) container_of(d, struct scmi_device, dev)

struct scmi_device *
scmi_device_create(struct device_node *np, struct device *parent, int protocol);
void scmi_device_destroy(struct scmi_device *scmi_dev);

struct scmi_device_id {
	u8 protocol_id;
};

struct scmi_driver {
	const char *name;
	int (*probe)(struct scmi_device *sdev);
	void (*remove)(struct scmi_device *sdev);
	const struct scmi_device_id *id_table;

	struct device_driver driver;
};

#define to_scmi_driver(d) container_of(d, struct scmi_driver, driver)

#ifdef CONFIG_ARM_SCMI_PROTOCOL
int scmi_driver_register(struct scmi_driver *driver,
			 struct module *owner, const char *mod_name);
void scmi_driver_unregister(struct scmi_driver *driver);
#else
static inline int
scmi_driver_register(struct scmi_driver *driver, struct module *owner,
		     const char *mod_name)
{
	return -EINVAL;
}

static inline void scmi_driver_unregister(struct scmi_driver *driver) {}
#endif /* CONFIG_ARM_SCMI_PROTOCOL */

#define scmi_register(driver) \
	scmi_driver_register(driver, THIS_MODULE, KBUILD_MODNAME)
#define scmi_unregister(driver) \
	scmi_driver_unregister(driver)

/**
 * module_scmi_driver() - Helper macro for registering a scmi driver
 * @__scmi_driver: scmi_driver structure
 *
 * Helper macro for scmi drivers to set up proper module init / exit
 * functions.  Replaces module_init() and module_exit() and keeps people from
 * printing pointless things to the kernel log when their driver is loaded.
 */
#define module_scmi_driver(__scmi_driver)	\
	module_driver(__scmi_driver, scmi_register, scmi_unregister)

typedef int (*scmi_prot_init_fn_t)(struct scmi_handle *);
int scmi_protocol_register(int protocol_id, scmi_prot_init_fn_t fn);
void scmi_protocol_unregister(int protocol_id);
