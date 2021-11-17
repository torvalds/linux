/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SCPI Message Protocol driver header
 *
 * Copyright (C) 2014 ARM Ltd.
 */

#ifndef _LINUX_SCPI_PROTOCOL_H
#define _LINUX_SCPI_PROTOCOL_H

#include <linux/types.h>

struct scpi_opp {
	u32 freq;
	u32 m_volt;
} __packed;

struct scpi_dvfs_info {
	unsigned int count;
	unsigned int latency; /* in nanoseconds */
	struct scpi_opp *opps;
};

enum scpi_sensor_class {
	TEMPERATURE,
	VOLTAGE,
	CURRENT,
	POWER,
	ENERGY,
};

struct scpi_sensor_info {
	u16 sensor_id;
	u8 class;
	u8 trigger_type;
	char name[20];
} __packed;

/**
 * struct scpi_ops - represents the various operations provided
 *	by SCP through SCPI message protocol
 * @get_version: returns the major and minor revision on the SCPI
 *	message protocol
 * @clk_get_range: gets clock range limit(min - max in Hz)
 * @clk_get_val: gets clock value(in Hz)
 * @clk_set_val: sets the clock value, setting to 0 will disable the
 *	clock (if supported)
 * @dvfs_get_idx: gets the Operating Point of the given power domain.
 *	OPP is an index to the list return by @dvfs_get_info
 * @dvfs_set_idx: sets the Operating Point of the given power domain.
 *	OPP is an index to the list return by @dvfs_get_info
 * @dvfs_get_info: returns the DVFS capabilities of the given power
 *	domain. It includes the OPP list and the latency information
 * @device_domain_id: gets the scpi domain id for a given device
 * @get_transition_latency: gets the DVFS transition latency for a given device
 * @add_opps_to_device: adds all the OPPs for a given device
 * @sensor_get_capability: get the list of capabilities for the sensors
 * @sensor_get_info: get the information of the specified sensor
 * @sensor_get_value: gets the current value of the sensor
 * @device_get_power_state: gets the power state of a power domain
 * @device_set_power_state: sets the power state of a power domain
 */
struct scpi_ops {
	u32 (*get_version)(void);
	int (*clk_get_range)(u16, unsigned long *, unsigned long *);
	unsigned long (*clk_get_val)(u16);
	int (*clk_set_val)(u16, unsigned long);
	int (*dvfs_get_idx)(u8);
	int (*dvfs_set_idx)(u8, u8);
	struct scpi_dvfs_info *(*dvfs_get_info)(u8);
	int (*device_domain_id)(struct device *);
	int (*get_transition_latency)(struct device *);
	int (*add_opps_to_device)(struct device *);
	int (*sensor_get_capability)(u16 *sensors);
	int (*sensor_get_info)(u16 sensor_id, struct scpi_sensor_info *);
	int (*sensor_get_value)(u16, u64 *);
	int (*device_get_power_state)(u16);
	int (*device_set_power_state)(u16, u8);
};

#if IS_REACHABLE(CONFIG_ARM_SCPI_PROTOCOL)
struct scpi_ops *get_scpi_ops(void);
#else
static inline struct scpi_ops *get_scpi_ops(void) { return NULL; }
#endif

#endif /* _LINUX_SCPI_PROTOCOL_H */
