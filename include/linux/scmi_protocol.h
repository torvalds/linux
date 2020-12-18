/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SCMI Message Protocol driver header
 *
 * Copyright (C) 2018 ARM Ltd.
 */

#ifndef _LINUX_SCMI_PROTOCOL_H
#define _LINUX_SCMI_PROTOCOL_H

#include <linux/device.h>
#include <linux/notifier.h>
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
			u64 rate);
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
 * @transition_latency_get: gets the DVFS transition latency for a given device
 * @device_opps_add: adds all the OPPs for a given device
 * @freq_set: sets the frequency for a given device using sustained frequency
 *	to sustained performance level mapping
 * @freq_get: gets the frequency for a given device using sustained frequency
 *	to sustained performance level mapping
 * @est_power_get: gets the estimated power cost for a given performance domain
 *	at a given frequency
 */
struct scmi_perf_ops {
	int (*limits_set)(const struct scmi_handle *handle, u32 domain,
			  u32 max_perf, u32 min_perf);
	int (*limits_get)(const struct scmi_handle *handle, u32 domain,
			  u32 *max_perf, u32 *min_perf);
	int (*level_set)(const struct scmi_handle *handle, u32 domain,
			 u32 level, bool poll);
	int (*level_get)(const struct scmi_handle *handle, u32 domain,
			 u32 *level, bool poll);
	int (*device_domain_id)(struct device *dev);
	int (*transition_latency_get)(const struct scmi_handle *handle,
				      struct device *dev);
	int (*device_opps_add)(const struct scmi_handle *handle,
			       struct device *dev);
	int (*freq_set)(const struct scmi_handle *handle, u32 domain,
			unsigned long rate, bool poll);
	int (*freq_get)(const struct scmi_handle *handle, u32 domain,
			unsigned long *rate, bool poll);
	int (*est_power_get)(const struct scmi_handle *handle, u32 domain,
			     unsigned long *rate, unsigned long *power);
	bool (*fast_switch_possible)(const struct scmi_handle *handle,
				     struct device *dev);
};

/**
 * struct scmi_power_ops - represents the various operations provided
 *	by SCMI Power Protocol
 *
 * @num_domains_get: get the count of power domains provided by SCMI
 * @name_get: gets the name of a power domain
 * @state_set: sets the power state of a power domain
 * @state_get: gets the power state of a power domain
 */
struct scmi_power_ops {
	int (*num_domains_get)(const struct scmi_handle *handle);
	char *(*name_get)(const struct scmi_handle *handle, u32 domain);
#define SCMI_POWER_STATE_TYPE_SHIFT	30
#define SCMI_POWER_STATE_ID_MASK	(BIT(28) - 1)
#define SCMI_POWER_STATE_PARAM(type, id) \
	((((type) & BIT(0)) << SCMI_POWER_STATE_TYPE_SHIFT) | \
		((id) & SCMI_POWER_STATE_ID_MASK))
#define SCMI_POWER_STATE_GENERIC_ON	SCMI_POWER_STATE_PARAM(0, 0)
#define SCMI_POWER_STATE_GENERIC_OFF	SCMI_POWER_STATE_PARAM(1, 0)
	int (*state_set)(const struct scmi_handle *handle, u32 domain,
			 u32 state);
	int (*state_get)(const struct scmi_handle *handle, u32 domain,
			 u32 *state);
};

struct scmi_sensor_info {
	u32 id;
	u8 type;
	s8 scale;
	u8 num_trip_points;
	bool async;
	char name[SCMI_MAX_STR_SIZE];
};

/*
 * Partial list from Distributed Management Task Force (DMTF) specification:
 * DSP0249 (Platform Level Data Model specification)
 */
enum scmi_sensor_class {
	NONE = 0x0,
	TEMPERATURE_C = 0x2,
	VOLTAGE = 0x5,
	CURRENT = 0x6,
	POWER = 0x7,
	ENERGY = 0x8,
};

/**
 * struct scmi_sensor_ops - represents the various operations provided
 *	by SCMI Sensor Protocol
 *
 * @count_get: get the count of sensors provided by SCMI
 * @info_get: get the information of the specified sensor
 * @trip_point_config: selects and configures a trip-point of interest
 * @reading_get: gets the current value of the sensor
 */
struct scmi_sensor_ops {
	int (*count_get)(const struct scmi_handle *handle);
	const struct scmi_sensor_info *(*info_get)
		(const struct scmi_handle *handle, u32 sensor_id);
	int (*trip_point_config)(const struct scmi_handle *handle,
				 u32 sensor_id, u8 trip_id, u64 trip_value);
	int (*reading_get)(const struct scmi_handle *handle, u32 sensor_id,
			   u64 *value);
};

/**
 * struct scmi_reset_ops - represents the various operations provided
 *	by SCMI Reset Protocol
 *
 * @num_domains_get: get the count of reset domains provided by SCMI
 * @name_get: gets the name of a reset domain
 * @latency_get: gets the reset latency for the specified reset domain
 * @reset: resets the specified reset domain
 * @assert: explicitly assert reset signal of the specified reset domain
 * @deassert: explicitly deassert reset signal of the specified reset domain
 */
struct scmi_reset_ops {
	int (*num_domains_get)(const struct scmi_handle *handle);
	char *(*name_get)(const struct scmi_handle *handle, u32 domain);
	int (*latency_get)(const struct scmi_handle *handle, u32 domain);
	int (*reset)(const struct scmi_handle *handle, u32 domain);
	int (*assert)(const struct scmi_handle *handle, u32 domain);
	int (*deassert)(const struct scmi_handle *handle, u32 domain);
};

/**
 * struct scmi_voltage_info - describe one available SCMI Voltage Domain
 *
 * @id: the domain ID as advertised by the platform
 * @segmented: defines the layout of the entries of array @levels_uv.
 *	       - when True the entries are to be interpreted as triplets,
 *	         each defining a segment representing a range of equally
 *	         space voltages: <lowest_volts>, <highest_volt>, <step_uV>
 *	       - when False the entries simply represent a single discrete
 *	         supported voltage level
 * @negative_volts_allowed: True if any of the entries of @levels_uv represent
 *			    a negative voltage.
 * @attributes: represents Voltage Domain advertised attributes
 * @name: name assigned to the Voltage Domain by platform
 * @num_levels: number of total entries in @levels_uv.
 * @levels_uv: array of entries describing the available voltage levels for
 *	       this domain.
 */
struct scmi_voltage_info {
	unsigned int id;
	bool segmented;
	bool negative_volts_allowed;
	unsigned int attributes;
	char name[SCMI_MAX_STR_SIZE];
	unsigned int num_levels;
#define SCMI_VOLTAGE_SEGMENT_LOW	0
#define SCMI_VOLTAGE_SEGMENT_HIGH	1
#define SCMI_VOLTAGE_SEGMENT_STEP	2
	int *levels_uv;
};

/**
 * struct scmi_voltage_ops - represents the various operations provided
 * by SCMI Voltage Protocol
 *
 * @num_domains_get: get the count of voltage domains provided by SCMI
 * @info_get: get the information of the specified domain
 * @config_set: set the config for the specified domain
 * @config_get: get the config of the specified domain
 * @level_set: set the voltage level for the specified domain
 * @level_get: get the voltage level of the specified domain
 */
struct scmi_voltage_ops {
	int (*num_domains_get)(const struct scmi_handle *handle);
	const struct scmi_voltage_info __must_check *(*info_get)
		(const struct scmi_handle *handle, u32 domain_id);
	int (*config_set)(const struct scmi_handle *handle, u32 domain_id,
			  u32 config);
#define	SCMI_VOLTAGE_ARCH_STATE_OFF		0x0
#define	SCMI_VOLTAGE_ARCH_STATE_ON		0x7
	int (*config_get)(const struct scmi_handle *handle, u32 domain_id,
			  u32 *config);
	int (*level_set)(const struct scmi_handle *handle, u32 domain_id,
			 u32 flags, s32 volt_uV);
	int (*level_get)(const struct scmi_handle *handle, u32 domain_id,
			 s32 *volt_uV);
};

/**
 * struct scmi_notify_ops  - represents notifications' operations provided by
 * SCMI core
 * @register_event_notifier: Register a notifier_block for the requested event
 * @unregister_event_notifier: Unregister a notifier_block for the requested
 *			       event
 *
 * A user can register/unregister its own notifier_block against the wanted
 * platform instance regarding the desired event identified by the
 * tuple: (proto_id, evt_id, src_id) using the provided register/unregister
 * interface where:
 *
 * @handle: The handle identifying the platform instance to use
 * @proto_id: The protocol ID as in SCMI Specification
 * @evt_id: The message ID of the desired event as in SCMI Specification
 * @src_id: A pointer to the desired source ID if different sources are
 *	    possible for the protocol (like domain_id, sensor_id...etc)
 *
 * @src_id can be provided as NULL if it simply does NOT make sense for
 * the protocol at hand, OR if the user is explicitly interested in
 * receiving notifications from ANY existent source associated to the
 * specified proto_id / evt_id.
 *
 * Received notifications are finally delivered to the registered users,
 * invoking the callback provided with the notifier_block *nb as follows:
 *
 *	int user_cb(nb, evt_id, report)
 *
 * with:
 *
 * @nb: The notifier block provided by the user
 * @evt_id: The message ID of the delivered event
 * @report: A custom struct describing the specific event delivered
 */
struct scmi_notify_ops {
	int (*register_event_notifier)(const struct scmi_handle *handle,
				       u8 proto_id, u8 evt_id, u32 *src_id,
				       struct notifier_block *nb);
	int (*unregister_event_notifier)(const struct scmi_handle *handle,
					 u8 proto_id, u8 evt_id, u32 *src_id,
					 struct notifier_block *nb);
};

/**
 * struct scmi_handle - Handle returned to ARM SCMI clients for usage.
 *
 * @dev: pointer to the SCMI device
 * @version: pointer to the structure containing SCMI version information
 * @power_ops: pointer to set of power protocol operations
 * @perf_ops: pointer to set of performance protocol operations
 * @clk_ops: pointer to set of clock protocol operations
 * @sensor_ops: pointer to set of sensor protocol operations
 * @reset_ops: pointer to set of reset protocol operations
 * @voltage_ops: pointer to set of voltage protocol operations
 * @notify_ops: pointer to set of notifications related operations
 * @perf_priv: pointer to private data structure specific to performance
 *	protocol(for internal use only)
 * @clk_priv: pointer to private data structure specific to clock
 *	protocol(for internal use only)
 * @power_priv: pointer to private data structure specific to power
 *	protocol(for internal use only)
 * @sensor_priv: pointer to private data structure specific to sensors
 *	protocol(for internal use only)
 * @reset_priv: pointer to private data structure specific to reset
 *	protocol(for internal use only)
 * @voltage_priv: pointer to private data structure specific to voltage
 *	protocol(for internal use only)
 * @notify_priv: pointer to private data structure specific to notifications
 *	(for internal use only)
 */
struct scmi_handle {
	struct device *dev;
	struct scmi_revision_info *version;
	const struct scmi_perf_ops *perf_ops;
	const struct scmi_clk_ops *clk_ops;
	const struct scmi_power_ops *power_ops;
	const struct scmi_sensor_ops *sensor_ops;
	const struct scmi_reset_ops *reset_ops;
	const struct scmi_voltage_ops *voltage_ops;
	const struct scmi_notify_ops *notify_ops;
	/* for protocol internal use */
	void *perf_priv;
	void *clk_priv;
	void *power_priv;
	void *sensor_priv;
	void *reset_priv;
	void *voltage_priv;
	void *notify_priv;
	void *system_priv;
};

enum scmi_std_protocol {
	SCMI_PROTOCOL_BASE = 0x10,
	SCMI_PROTOCOL_POWER = 0x11,
	SCMI_PROTOCOL_SYSTEM = 0x12,
	SCMI_PROTOCOL_PERF = 0x13,
	SCMI_PROTOCOL_CLOCK = 0x14,
	SCMI_PROTOCOL_SENSOR = 0x15,
	SCMI_PROTOCOL_RESET = 0x16,
	SCMI_PROTOCOL_VOLTAGE = 0x17,
};

enum scmi_system_events {
	SCMI_SYSTEM_SHUTDOWN,
	SCMI_SYSTEM_COLDRESET,
	SCMI_SYSTEM_WARMRESET,
	SCMI_SYSTEM_POWERUP,
	SCMI_SYSTEM_SUSPEND,
	SCMI_SYSTEM_MAX
};

struct scmi_device {
	u32 id;
	u8 protocol_id;
	const char *name;
	struct device dev;
	struct scmi_handle *handle;
};

#define to_scmi_dev(d) container_of(d, struct scmi_device, dev)

struct scmi_device *
scmi_device_create(struct device_node *np, struct device *parent, int protocol,
		   const char *name);
void scmi_device_destroy(struct scmi_device *scmi_dev);

struct scmi_device_id {
	u8 protocol_id;
	const char *name;
};

struct scmi_driver {
	const char *name;
	int (*probe)(struct scmi_device *sdev);
	void (*remove)(struct scmi_device *sdev);
	const struct scmi_device_id *id_table;

	struct device_driver driver;
};

#define to_scmi_driver(d) container_of(d, struct scmi_driver, driver)

#if IS_REACHABLE(CONFIG_ARM_SCMI_PROTOCOL)
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

/* SCMI Notification API - Custom Event Reports */
enum scmi_notification_events {
	SCMI_EVENT_POWER_STATE_CHANGED = 0x0,
	SCMI_EVENT_PERFORMANCE_LIMITS_CHANGED = 0x0,
	SCMI_EVENT_PERFORMANCE_LEVEL_CHANGED = 0x1,
	SCMI_EVENT_SENSOR_TRIP_POINT_EVENT = 0x0,
	SCMI_EVENT_RESET_ISSUED = 0x0,
	SCMI_EVENT_BASE_ERROR_EVENT = 0x0,
	SCMI_EVENT_SYSTEM_POWER_STATE_NOTIFIER = 0x0,
};

struct scmi_power_state_changed_report {
	ktime_t		timestamp;
	unsigned int	agent_id;
	unsigned int	domain_id;
	unsigned int	power_state;
};

struct scmi_system_power_state_notifier_report {
	ktime_t		timestamp;
	unsigned int	agent_id;
	unsigned int	flags;
	unsigned int	system_state;
};

struct scmi_perf_limits_report {
	ktime_t		timestamp;
	unsigned int	agent_id;
	unsigned int	domain_id;
	unsigned int	range_max;
	unsigned int	range_min;
};

struct scmi_perf_level_report {
	ktime_t		timestamp;
	unsigned int	agent_id;
	unsigned int	domain_id;
	unsigned int	performance_level;
};

struct scmi_sensor_trip_point_report {
	ktime_t		timestamp;
	unsigned int	agent_id;
	unsigned int	sensor_id;
	unsigned int	trip_point_desc;
};

struct scmi_reset_issued_report {
	ktime_t		timestamp;
	unsigned int	agent_id;
	unsigned int	domain_id;
	unsigned int	reset_state;
};

struct scmi_base_error_report {
	ktime_t			timestamp;
	unsigned int		agent_id;
	bool			fatal;
	unsigned int		cmd_count;
	unsigned long long	reports[];
};

#endif /* _LINUX_SCMI_PROTOCOL_H */
