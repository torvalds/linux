/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Data types and headers for RAPL support
 *
 *  Copyright (C) 2019  Intel Corporation.
 *
 *  Author: Zhang Rui <rui.zhang@intel.com>
 */

#ifndef __INTEL_RAPL_H__
#define __INTEL_RAPL_H__

#include <linux/types.h>
#include <linux/powercap.h>
#include <linux/cpuhotplug.h>

enum rapl_domain_type {
	RAPL_DOMAIN_PACKAGE,	/* entire package/socket */
	RAPL_DOMAIN_PP0,	/* core power plane */
	RAPL_DOMAIN_PP1,	/* graphics uncore */
	RAPL_DOMAIN_DRAM,	/* DRAM control_type */
	RAPL_DOMAIN_PLATFORM,	/* PSys control_type */
	RAPL_DOMAIN_MAX,
};

enum rapl_domain_reg_id {
	RAPL_DOMAIN_REG_LIMIT,
	RAPL_DOMAIN_REG_STATUS,
	RAPL_DOMAIN_REG_PERF,
	RAPL_DOMAIN_REG_POLICY,
	RAPL_DOMAIN_REG_INFO,
	RAPL_DOMAIN_REG_PL4,
	RAPL_DOMAIN_REG_MAX,
};

struct rapl_package;

enum rapl_primitives {
	ENERGY_COUNTER,
	POWER_LIMIT1,
	POWER_LIMIT2,
	POWER_LIMIT4,
	FW_LOCK,

	PL1_ENABLE,		/* power limit 1, aka long term */
	PL1_CLAMP,		/* allow frequency to go below OS request */
	PL2_ENABLE,		/* power limit 2, aka short term, instantaneous */
	PL2_CLAMP,
	PL4_ENABLE,		/* power limit 4, aka max peak power */

	TIME_WINDOW1,		/* long term */
	TIME_WINDOW2,		/* short term */
	THERMAL_SPEC_POWER,
	MAX_POWER,

	MIN_POWER,
	MAX_TIME_WINDOW,
	THROTTLED_TIME,
	PRIORITY_LEVEL,

	/* below are not raw primitive data */
	AVERAGE_POWER,
	NR_RAPL_PRIMITIVES,
};

struct rapl_domain_data {
	u64 primitives[NR_RAPL_PRIMITIVES];
	unsigned long timestamp;
};

#define NR_POWER_LIMITS (3)
struct rapl_power_limit {
	struct powercap_zone_constraint *constraint;
	int prim_id;		/* primitive ID used to enable */
	struct rapl_domain *domain;
	const char *name;
	u64 last_power_limit;
};

struct rapl_package;

#define RAPL_DOMAIN_NAME_LENGTH 16

struct rapl_domain {
	char name[RAPL_DOMAIN_NAME_LENGTH];
	enum rapl_domain_type id;
	u64 regs[RAPL_DOMAIN_REG_MAX];
	struct powercap_zone power_zone;
	struct rapl_domain_data rdd;
	struct rapl_power_limit rpl[NR_POWER_LIMITS];
	u64 attr_map;		/* track capabilities */
	unsigned int state;
	unsigned int domain_energy_unit;
	struct rapl_package *rp;
};

struct reg_action {
	u64 reg;
	u64 mask;
	u64 value;
	int err;
};

/**
 * struct rapl_if_priv: private data for different RAPL interfaces
 * @control_type:		Each RAPL interface must have its own powercap
 *				control type.
 * @platform_rapl_domain:	Optional. Some RAPL interface may have platform
 *				level RAPL control.
 * @pcap_rapl_online:		CPU hotplug state for each RAPL interface.
 * @reg_unit:			Register for getting energy/power/time unit.
 * @regs:			Register sets for different RAPL Domains.
 * @limits:			Number of power limits supported by each domain.
 * @read_raw:			Callback for reading RAPL interface specific
 *				registers.
 * @write_raw:			Callback for writing RAPL interface specific
 *				registers.
 */
struct rapl_if_priv {
	struct powercap_control_type *control_type;
	struct rapl_domain *platform_rapl_domain;
	enum cpuhp_state pcap_rapl_online;
	u64 reg_unit;
	u64 regs[RAPL_DOMAIN_MAX][RAPL_DOMAIN_REG_MAX];
	int limits[RAPL_DOMAIN_MAX];
	int (*read_raw)(int cpu, struct reg_action *ra);
	int (*write_raw)(int cpu, struct reg_action *ra);
};

/* maximum rapl package domain name: package-%d-die-%d */
#define PACKAGE_DOMAIN_NAME_LENGTH 30

struct rapl_package {
	unsigned int id;	/* logical die id, equals physical 1-die systems */
	unsigned int nr_domains;
	unsigned long domain_map;	/* bit map of active domains */
	unsigned int power_unit;
	unsigned int energy_unit;
	unsigned int time_unit;
	struct rapl_domain *domains;	/* array of domains, sized at runtime */
	struct powercap_zone *power_zone;	/* keep track of parent zone */
	unsigned long power_limit_irq;	/* keep track of package power limit
					 * notify interrupt enable status.
					 */
	struct list_head plist;
	int lead_cpu;		/* one active cpu per package for access */
	/* Track active cpus */
	struct cpumask cpumask;
	char name[PACKAGE_DOMAIN_NAME_LENGTH];
	struct rapl_if_priv *priv;
};

struct rapl_package *rapl_find_package_domain(int cpu, struct rapl_if_priv *priv);
struct rapl_package *rapl_add_package(int cpu, struct rapl_if_priv *priv);
void rapl_remove_package(struct rapl_package *rp);

#endif /* __INTEL_RAPL_H__ */
