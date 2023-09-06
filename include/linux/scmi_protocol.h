/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SCMI Message Protocol driver header
 *
 * Copyright (C) 2018-2021 ARM Ltd.
 */

#ifndef _LINUX_SCMI_PROTOCOL_H
#define _LINUX_SCMI_PROTOCOL_H

#include <linux/bitfield.h>
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
struct scmi_device;
struct scmi_protocol_handle;

/**
 * struct scmi_clk_proto_ops - represents the various operations provided
 *	by SCMI Clock Protocol
 *
 * @count_get: get the count of clocks provided by SCMI
 * @info_get: get the information of the specified clock
 * @rate_get: request the current clock rate of a clock
 * @rate_set: set the clock rate of a clock
 * @enable: enables the specified clock
 * @disable: disables the specified clock
 */
struct scmi_clk_proto_ops {
	int (*count_get)(const struct scmi_protocol_handle *ph);

	const struct scmi_clock_info __must_check *(*info_get)
		(const struct scmi_protocol_handle *ph, u32 clk_id);
	int (*rate_get)(const struct scmi_protocol_handle *ph, u32 clk_id,
			u64 *rate);
	int (*rate_set)(const struct scmi_protocol_handle *ph, u32 clk_id,
			u64 rate);
	int (*enable)(const struct scmi_protocol_handle *ph, u32 clk_id);
	int (*disable)(const struct scmi_protocol_handle *ph, u32 clk_id);
};

/**
 * struct scmi_perf_proto_ops - represents the various operations provided
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
 * @fast_switch_possible: indicates if fast DVFS switching is possible or not
 *	for a given device
 * @power_scale_mw_get: indicates if the power values provided are in milliWatts
 *	or in some other (abstract) scale
 */
struct scmi_perf_proto_ops {
	int (*limits_set)(const struct scmi_protocol_handle *ph, u32 domain,
			  u32 max_perf, u32 min_perf);
	int (*limits_get)(const struct scmi_protocol_handle *ph, u32 domain,
			  u32 *max_perf, u32 *min_perf);
	int (*level_set)(const struct scmi_protocol_handle *ph, u32 domain,
			 u32 level, bool poll);
	int (*level_get)(const struct scmi_protocol_handle *ph, u32 domain,
			 u32 *level, bool poll);
	int (*device_domain_id)(struct device *dev);
	int (*transition_latency_get)(const struct scmi_protocol_handle *ph,
				      struct device *dev);
	int (*device_opps_add)(const struct scmi_protocol_handle *ph,
			       struct device *dev);
	int (*freq_set)(const struct scmi_protocol_handle *ph, u32 domain,
			unsigned long rate, bool poll);
	int (*freq_get)(const struct scmi_protocol_handle *ph, u32 domain,
			unsigned long *rate, bool poll);
	int (*est_power_get)(const struct scmi_protocol_handle *ph, u32 domain,
			     unsigned long *rate, unsigned long *power);
	bool (*fast_switch_possible)(const struct scmi_protocol_handle *ph,
				     struct device *dev);
	bool (*power_scale_mw_get)(const struct scmi_protocol_handle *ph);
};

/**
 * struct scmi_power_proto_ops - represents the various operations provided
 *	by SCMI Power Protocol
 *
 * @num_domains_get: get the count of power domains provided by SCMI
 * @name_get: gets the name of a power domain
 * @state_set: sets the power state of a power domain
 * @state_get: gets the power state of a power domain
 */
struct scmi_power_proto_ops {
	int (*num_domains_get)(const struct scmi_protocol_handle *ph);
	char *(*name_get)(const struct scmi_protocol_handle *ph, u32 domain);
#define SCMI_POWER_STATE_TYPE_SHIFT	30
#define SCMI_POWER_STATE_ID_MASK	(BIT(28) - 1)
#define SCMI_POWER_STATE_PARAM(type, id) \
	((((type) & BIT(0)) << SCMI_POWER_STATE_TYPE_SHIFT) | \
		((id) & SCMI_POWER_STATE_ID_MASK))
#define SCMI_POWER_STATE_GENERIC_ON	SCMI_POWER_STATE_PARAM(0, 0)
#define SCMI_POWER_STATE_GENERIC_OFF	SCMI_POWER_STATE_PARAM(1, 0)
	int (*state_set)(const struct scmi_protocol_handle *ph, u32 domain,
			 u32 state);
	int (*state_get)(const struct scmi_protocol_handle *ph, u32 domain,
			 u32 *state);
};

/**
 * struct scmi_sensor_reading  - represent a timestamped read
 *
 * Used by @reading_get_timestamped method.
 *
 * @value: The signed value sensor read.
 * @timestamp: An unsigned timestamp for the sensor read, as provided by
 *	       SCMI platform. Set to zero when not available.
 */
struct scmi_sensor_reading {
	long long value;
	unsigned long long timestamp;
};

/**
 * struct scmi_range_attrs  - specifies a sensor or axis values' range
 * @min_range: The minimum value which can be represented by the sensor/axis.
 * @max_range: The maximum value which can be represented by the sensor/axis.
 */
struct scmi_range_attrs {
	long long min_range;
	long long max_range;
};

/**
 * struct scmi_sensor_axis_info  - describes one sensor axes
 * @id: The axes ID.
 * @type: Axes type. Chosen amongst one of @enum scmi_sensor_class.
 * @scale: Power-of-10 multiplier applied to the axis unit.
 * @name: NULL-terminated string representing axes name as advertised by
 *	  SCMI platform.
 * @extended_attrs: Flag to indicate the presence of additional extended
 *		    attributes for this axes.
 * @resolution: Extended attribute representing the resolution of the axes.
 *		Set to 0 if not reported by this axes.
 * @exponent: Extended attribute representing the power-of-10 multiplier that
 *	      is applied to the resolution field. Set to 0 if not reported by
 *	      this axes.
 * @attrs: Extended attributes representing minimum and maximum values
 *	   measurable by this axes. Set to 0 if not reported by this sensor.
 */
struct scmi_sensor_axis_info {
	unsigned int id;
	unsigned int type;
	int scale;
	char name[SCMI_MAX_STR_SIZE];
	bool extended_attrs;
	unsigned int resolution;
	int exponent;
	struct scmi_range_attrs attrs;
};

/**
 * struct scmi_sensor_intervals_info  - describes number and type of available
 *	update intervals
 * @segmented: Flag for segmented intervals' representation. When True there
 *	       will be exactly 3 intervals in @desc, with each entry
 *	       representing a member of a segment in this order:
 *	       {lowest update interval, highest update interval, step size}
 * @count: Number of intervals described in @desc.
 * @desc: Array of @count interval descriptor bitmask represented as detailed in
 *	  the SCMI specification: it can be accessed using the accompanying
 *	  macros.
 * @prealloc_pool: A minimal preallocated pool of desc entries used to avoid
 *		   lesser-than-64-bytes dynamic allocation for small @count
 *		   values.
 */
struct scmi_sensor_intervals_info {
	bool segmented;
	unsigned int count;
#define SCMI_SENS_INTVL_SEGMENT_LOW	0
#define SCMI_SENS_INTVL_SEGMENT_HIGH	1
#define SCMI_SENS_INTVL_SEGMENT_STEP	2
	unsigned int *desc;
#define SCMI_SENS_INTVL_GET_SECS(x)		FIELD_GET(GENMASK(20, 5), (x))
#define SCMI_SENS_INTVL_GET_EXP(x)					\
	({								\
		int __signed_exp = FIELD_GET(GENMASK(4, 0), (x));	\
									\
		if (__signed_exp & BIT(4))				\
			__signed_exp |= GENMASK(31, 5);			\
		__signed_exp;						\
	})
#define SCMI_MAX_PREALLOC_POOL			16
	unsigned int prealloc_pool[SCMI_MAX_PREALLOC_POOL];
};

/**
 * struct scmi_sensor_info - represents information related to one of the
 * available sensors.
 * @id: Sensor ID.
 * @type: Sensor type. Chosen amongst one of @enum scmi_sensor_class.
 * @scale: Power-of-10 multiplier applied to the sensor unit.
 * @num_trip_points: Number of maximum configurable trip points.
 * @async: Flag for asynchronous read support.
 * @update: Flag for continuouos update notification support.
 * @timestamped: Flag for timestamped read support.
 * @tstamp_scale: Power-of-10 multiplier applied to the sensor timestamps to
 *		  represent it in seconds.
 * @num_axis: Number of supported axis if any. Reported as 0 for scalar sensors.
 * @axis: Pointer to an array of @num_axis descriptors.
 * @intervals: Descriptor of available update intervals.
 * @sensor_config: A bitmask reporting the current sensor configuration as
 *		   detailed in the SCMI specification: it can accessed and
 *		   modified through the accompanying macros.
 * @name: NULL-terminated string representing sensor name as advertised by
 *	  SCMI platform.
 * @extended_scalar_attrs: Flag to indicate the presence of additional extended
 *			   attributes for this sensor.
 * @sensor_power: Extended attribute representing the average power
 *		  consumed by the sensor in microwatts (uW) when it is active.
 *		  Reported here only for scalar sensors.
 *		  Set to 0 if not reported by this sensor.
 * @resolution: Extended attribute representing the resolution of the sensor.
 *		Reported here only for scalar sensors.
 *		Set to 0 if not reported by this sensor.
 * @exponent: Extended attribute representing the power-of-10 multiplier that is
 *	      applied to the resolution field.
 *	      Reported here only for scalar sensors.
 *	      Set to 0 if not reported by this sensor.
 * @scalar_attrs: Extended attributes representing minimum and maximum
 *		  measurable values by this sensor.
 *		  Reported here only for scalar sensors.
 *		  Set to 0 if not reported by this sensor.
 */
struct scmi_sensor_info {
	unsigned int id;
	unsigned int type;
	int scale;
	unsigned int num_trip_points;
	bool async;
	bool update;
	bool timestamped;
	int tstamp_scale;
	unsigned int num_axis;
	struct scmi_sensor_axis_info *axis;
	struct scmi_sensor_intervals_info intervals;
	unsigned int sensor_config;
#define SCMI_SENS_CFG_UPDATE_SECS_MASK		GENMASK(31, 16)
#define SCMI_SENS_CFG_GET_UPDATE_SECS(x)				\
	FIELD_GET(SCMI_SENS_CFG_UPDATE_SECS_MASK, (x))

#define SCMI_SENS_CFG_UPDATE_EXP_MASK		GENMASK(15, 11)
#define SCMI_SENS_CFG_GET_UPDATE_EXP(x)					\
	({								\
		int __signed_exp =					\
			FIELD_GET(SCMI_SENS_CFG_UPDATE_EXP_MASK, (x));	\
									\
		if (__signed_exp & BIT(4))				\
			__signed_exp |= GENMASK(31, 5);			\
		__signed_exp;						\
	})

#define SCMI_SENS_CFG_ROUND_MASK		GENMASK(10, 9)
#define SCMI_SENS_CFG_ROUND_AUTO		2
#define SCMI_SENS_CFG_ROUND_UP			1
#define SCMI_SENS_CFG_ROUND_DOWN		0

#define SCMI_SENS_CFG_TSTAMP_ENABLED_MASK	BIT(1)
#define SCMI_SENS_CFG_TSTAMP_ENABLE		1
#define SCMI_SENS_CFG_TSTAMP_DISABLE		0
#define SCMI_SENS_CFG_IS_TSTAMP_ENABLED(x)				\
	FIELD_GET(SCMI_SENS_CFG_TSTAMP_ENABLED_MASK, (x))

#define SCMI_SENS_CFG_SENSOR_ENABLED_MASK	BIT(0)
#define SCMI_SENS_CFG_SENSOR_ENABLE		1
#define SCMI_SENS_CFG_SENSOR_DISABLE		0
	char name[SCMI_MAX_STR_SIZE];
#define SCMI_SENS_CFG_IS_ENABLED(x)		FIELD_GET(BIT(0), (x))
	bool extended_scalar_attrs;
	unsigned int sensor_power;
	unsigned int resolution;
	int exponent;
	struct scmi_range_attrs scalar_attrs;
};

/*
 * Partial list from Distributed Management Task Force (DMTF) specification:
 * DSP0249 (Platform Level Data Model specification)
 */
enum scmi_sensor_class {
	NONE = 0x0,
	UNSPEC = 0x1,
	TEMPERATURE_C = 0x2,
	TEMPERATURE_F = 0x3,
	TEMPERATURE_K = 0x4,
	VOLTAGE = 0x5,
	CURRENT = 0x6,
	POWER = 0x7,
	ENERGY = 0x8,
	CHARGE = 0x9,
	VOLTAMPERE = 0xA,
	NITS = 0xB,
	LUMENS = 0xC,
	LUX = 0xD,
	CANDELAS = 0xE,
	KPA = 0xF,
	PSI = 0x10,
	NEWTON = 0x11,
	CFM = 0x12,
	RPM = 0x13,
	HERTZ = 0x14,
	SECS = 0x15,
	MINS = 0x16,
	HOURS = 0x17,
	DAYS = 0x18,
	WEEKS = 0x19,
	MILS = 0x1A,
	INCHES = 0x1B,
	FEET = 0x1C,
	CUBIC_INCHES = 0x1D,
	CUBIC_FEET = 0x1E,
	METERS = 0x1F,
	CUBIC_CM = 0x20,
	CUBIC_METERS = 0x21,
	LITERS = 0x22,
	FLUID_OUNCES = 0x23,
	RADIANS = 0x24,
	STERADIANS = 0x25,
	REVOLUTIONS = 0x26,
	CYCLES = 0x27,
	GRAVITIES = 0x28,
	OUNCES = 0x29,
	POUNDS = 0x2A,
	FOOT_POUNDS = 0x2B,
	OUNCE_INCHES = 0x2C,
	GAUSS = 0x2D,
	GILBERTS = 0x2E,
	HENRIES = 0x2F,
	FARADS = 0x30,
	OHMS = 0x31,
	SIEMENS = 0x32,
	MOLES = 0x33,
	BECQUERELS = 0x34,
	PPM = 0x35,
	DECIBELS = 0x36,
	DBA = 0x37,
	DBC = 0x38,
	GRAYS = 0x39,
	SIEVERTS = 0x3A,
	COLOR_TEMP_K = 0x3B,
	BITS = 0x3C,
	BYTES = 0x3D,
	WORDS = 0x3E,
	DWORDS = 0x3F,
	QWORDS = 0x40,
	PERCENTAGE = 0x41,
	PASCALS = 0x42,
	COUNTS = 0x43,
	GRAMS = 0x44,
	NEWTON_METERS = 0x45,
	HITS = 0x46,
	MISSES = 0x47,
	RETRIES = 0x48,
	OVERRUNS = 0x49,
	UNDERRUNS = 0x4A,
	COLLISIONS = 0x4B,
	PACKETS = 0x4C,
	MESSAGES = 0x4D,
	CHARS = 0x4E,
	ERRORS = 0x4F,
	CORRECTED_ERRS = 0x50,
	UNCORRECTABLE_ERRS = 0x51,
	SQ_MILS = 0x52,
	SQ_INCHES = 0x53,
	SQ_FEET = 0x54,
	SQ_CM = 0x55,
	SQ_METERS = 0x56,
	RADIANS_SEC = 0x57,
	BPM = 0x58,
	METERS_SEC_SQUARED = 0x59,
	METERS_SEC = 0x5A,
	CUBIC_METERS_SEC = 0x5B,
	MM_MERCURY = 0x5C,
	RADIANS_SEC_SQUARED = 0x5D,
	OEM_UNIT = 0xFF
};

/**
 * struct scmi_sensor_proto_ops - represents the various operations provided
 *	by SCMI Sensor Protocol
 *
 * @count_get: get the count of sensors provided by SCMI
 * @info_get: get the information of the specified sensor
 * @trip_point_config: selects and configures a trip-point of interest
 * @reading_get: gets the current value of the sensor
 * @reading_get_timestamped: gets the current value and timestamp, when
 *			     available, of the sensor. (as of v3.0 spec)
 *			     Supports multi-axis sensors for sensors which
 *			     supports it and if the @reading array size of
 *			     @count entry equals the sensor num_axis
 * @config_get: Get sensor current configuration
 * @config_set: Set sensor current configuration
 */
struct scmi_sensor_proto_ops {
	int (*count_get)(const struct scmi_protocol_handle *ph);
	const struct scmi_sensor_info __must_check *(*info_get)
		(const struct scmi_protocol_handle *ph, u32 sensor_id);
	int (*trip_point_config)(const struct scmi_protocol_handle *ph,
				 u32 sensor_id, u8 trip_id, u64 trip_value);
	int (*reading_get)(const struct scmi_protocol_handle *ph, u32 sensor_id,
			   u64 *value);
	int (*reading_get_timestamped)(const struct scmi_protocol_handle *ph,
				       u32 sensor_id, u8 count,
				       struct scmi_sensor_reading *readings);
	int (*config_get)(const struct scmi_protocol_handle *ph,
			  u32 sensor_id, u32 *sensor_config);
	int (*config_set)(const struct scmi_protocol_handle *ph,
			  u32 sensor_id, u32 sensor_config);
};

/**
 * struct scmi_reset_proto_ops - represents the various operations provided
 *	by SCMI Reset Protocol
 *
 * @num_domains_get: get the count of reset domains provided by SCMI
 * @name_get: gets the name of a reset domain
 * @latency_get: gets the reset latency for the specified reset domain
 * @reset: resets the specified reset domain
 * @assert: explicitly assert reset signal of the specified reset domain
 * @deassert: explicitly deassert reset signal of the specified reset domain
 */
struct scmi_reset_proto_ops {
	int (*num_domains_get)(const struct scmi_protocol_handle *ph);
	char *(*name_get)(const struct scmi_protocol_handle *ph, u32 domain);
	int (*latency_get)(const struct scmi_protocol_handle *ph, u32 domain);
	int (*reset)(const struct scmi_protocol_handle *ph, u32 domain);
	int (*assert)(const struct scmi_protocol_handle *ph, u32 domain);
	int (*deassert)(const struct scmi_protocol_handle *ph, u32 domain);
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
 * struct scmi_voltage_proto_ops - represents the various operations provided
 * by SCMI Voltage Protocol
 *
 * @num_domains_get: get the count of voltage domains provided by SCMI
 * @info_get: get the information of the specified domain
 * @config_set: set the config for the specified domain
 * @config_get: get the config of the specified domain
 * @level_set: set the voltage level for the specified domain
 * @level_get: get the voltage level of the specified domain
 */
struct scmi_voltage_proto_ops {
	int (*num_domains_get)(const struct scmi_protocol_handle *ph);
	const struct scmi_voltage_info __must_check *(*info_get)
		(const struct scmi_protocol_handle *ph, u32 domain_id);
	int (*config_set)(const struct scmi_protocol_handle *ph, u32 domain_id,
			  u32 config);
#define	SCMI_VOLTAGE_ARCH_STATE_OFF		0x0
#define	SCMI_VOLTAGE_ARCH_STATE_ON		0x7
	int (*config_get)(const struct scmi_protocol_handle *ph, u32 domain_id,
			  u32 *config);
	int (*level_set)(const struct scmi_protocol_handle *ph, u32 domain_id,
			 u32 flags, s32 volt_uV);
	int (*level_get)(const struct scmi_protocol_handle *ph, u32 domain_id,
			 s32 *volt_uV);
};

/**
 * struct scmi_notify_ops  - represents notifications' operations provided by
 * SCMI core
 * @devm_event_notifier_register: Managed registration of a notifier_block for
 *				  the requested event
 * @devm_event_notifier_unregister: Managed unregistration of a notifier_block
 *				    for the requested event
 * @event_notifier_register: Register a notifier_block for the requested event
 * @event_notifier_unregister: Unregister a notifier_block for the requested
 *			       event
 *
 * A user can register/unregister its own notifier_block against the wanted
 * platform instance regarding the desired event identified by the
 * tuple: (proto_id, evt_id, src_id) using the provided register/unregister
 * interface where:
 *
 * @sdev: The scmi_device to use when calling the devres managed ops devm_
 * @handle: The handle identifying the platform instance to use, when not
 *	    calling the managed ops devm_
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
	int (*devm_event_notifier_register)(struct scmi_device *sdev,
					    u8 proto_id, u8 evt_id,
					    const u32 *src_id,
					    struct notifier_block *nb);
	int (*devm_event_notifier_unregister)(struct scmi_device *sdev,
					      u8 proto_id, u8 evt_id,
					      const u32 *src_id,
					      struct notifier_block *nb);
	int (*event_notifier_register)(const struct scmi_handle *handle,
				       u8 proto_id, u8 evt_id,
				       const u32 *src_id,
				       struct notifier_block *nb);
	int (*event_notifier_unregister)(const struct scmi_handle *handle,
					 u8 proto_id, u8 evt_id,
					 const u32 *src_id,
					 struct notifier_block *nb);
};

/**
 * struct scmi_handle - Handle returned to ARM SCMI clients for usage.
 *
 * @dev: pointer to the SCMI device
 * @version: pointer to the structure containing SCMI version information
 * @devm_protocol_get: devres managed method to acquire a protocol and get specific
 *		       operations and a dedicated protocol handler
 * @devm_protocol_put: devres managed method to release a protocol
 * @notify_ops: pointer to set of notifications related operations
 */
struct scmi_handle {
	struct device *dev;
	struct scmi_revision_info *version;

	const void __must_check *
		(*devm_protocol_get)(struct scmi_device *sdev, u8 proto,
				     struct scmi_protocol_handle **ph);
	void (*devm_protocol_put)(struct scmi_device *sdev, u8 proto);

	const struct scmi_notify_ops *notify_ops;
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

/**
 * module_scmi_protocol() - Helper macro for registering a scmi protocol
 * @__scmi_protocol: scmi_protocol structure
 *
 * Helper macro for scmi drivers to set up proper module init / exit
 * functions.  Replaces module_init() and module_exit() and keeps people from
 * printing pointless things to the kernel log when their driver is loaded.
 */
#define module_scmi_protocol(__scmi_protocol)	\
	module_driver(__scmi_protocol,		\
		      scmi_protocol_register, scmi_protocol_unregister)

struct scmi_protocol;
int scmi_protocol_register(const struct scmi_protocol *proto);
void scmi_protocol_unregister(const struct scmi_protocol *proto);

/* SCMI Notification API - Custom Event Reports */
enum scmi_notification_events {
	SCMI_EVENT_POWER_STATE_CHANGED = 0x0,
	SCMI_EVENT_PERFORMANCE_LIMITS_CHANGED = 0x0,
	SCMI_EVENT_PERFORMANCE_LEVEL_CHANGED = 0x1,
	SCMI_EVENT_SENSOR_TRIP_POINT_EVENT = 0x0,
	SCMI_EVENT_SENSOR_UPDATE = 0x1,
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

struct scmi_sensor_update_report {
	ktime_t				timestamp;
	unsigned int			agent_id;
	unsigned int			sensor_id;
	unsigned int			readings_count;
	struct scmi_sensor_reading	readings[];
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
