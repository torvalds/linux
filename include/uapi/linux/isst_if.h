/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Intel Speed Select Interface: OS to hardware Interface
 * Copyright (c) 2019, Intel Corporation.
 * All rights reserved.
 *
 * Author: Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>
 */

#ifndef __ISST_IF_H
#define __ISST_IF_H

#include <linux/types.h>

/**
 * struct isst_if_platform_info - Define platform information
 * @api_version:	Version of the firmware document, which this driver
 *			can communicate
 * @driver_version:	Driver version, which will help user to send right
 *			commands. Even if the firmware is capable, driver may
 *			not be ready
 * @max_cmds_per_ioctl:	Returns the maximum number of commands driver will
 *			accept in a single ioctl
 * @mbox_supported:	Support of mail box interface
 * @mmio_supported:	Support of mmio interface for core-power feature
 *
 * Used to return output of IOCTL ISST_IF_GET_PLATFORM_INFO. This
 * information can be used by the user space, to get the driver, firmware
 * support and also number of commands to send in a single IOCTL request.
 */
struct isst_if_platform_info {
	__u16 api_version;
	__u16 driver_version;
	__u16 max_cmds_per_ioctl;
	__u8 mbox_supported;
	__u8 mmio_supported;
};

/**
 * struct isst_if_cpu_map - CPU mapping between logical and physical CPU
 * @logical_cpu:	Linux logical CPU number
 * @physical_cpu:	PUNIT CPU number
 *
 * Used to convert from Linux logical CPU to PUNIT CPU numbering scheme.
 * The PUNIT CPU number is different than APIC ID based CPU numbering.
 */
struct isst_if_cpu_map {
	__u32 logical_cpu;
	__u32 physical_cpu;
};

/**
 * struct isst_if_cpu_maps - structure for CPU map IOCTL
 * @cmd_count:	Number of CPU mapping command in cpu_map[]
 * @cpu_map[]:	Holds one or more CPU map data structure
 *
 * This structure used with ioctl ISST_IF_GET_PHY_ID to send
 * one or more CPU mapping commands. Here IOCTL return value indicates
 * number of commands sent or error number if no commands have been sent.
 */
struct isst_if_cpu_maps {
	__u32 cmd_count;
	struct isst_if_cpu_map cpu_map[1];
};

/**
 * struct isst_if_io_reg - Read write PUNIT IO register
 * @read_write:		Value 0: Read, 1: Write
 * @logical_cpu:	Logical CPU number to get target PCI device.
 * @reg:		PUNIT register offset
 * @value:		For write operation value to write and for
 *			read placeholder read value
 *
 * Structure to specify read/write data to PUNIT registers.
 */
struct isst_if_io_reg {
	__u32 read_write; /* Read:0, Write:1 */
	__u32 logical_cpu;
	__u32 reg;
	__u32 value;
};

/**
 * struct isst_if_io_regs - structure for IO register commands
 * @cmd_count:	Number of io reg commands in io_reg[]
 * @io_reg[]:	Holds one or more io_reg command structure
 *
 * This structure used with ioctl ISST_IF_IO_CMD to send
 * one or more read/write commands to PUNIT. Here IOCTL return value
 * indicates number of requests sent or error number if no requests have
 * been sent.
 */
struct isst_if_io_regs {
	__u32 req_count;
	struct isst_if_io_reg io_reg[1];
};

/**
 * struct isst_if_mbox_cmd - Structure to define mail box command
 * @logical_cpu:	Logical CPU number to get target PCI device
 * @parameter:		Mailbox parameter value
 * @req_data:		Request data for the mailbox
 * @resp_data:		Response data for mailbox command response
 * @command:		Mailbox command value
 * @sub_command:	Mailbox sub command value
 * @reserved:		Unused, set to 0
 *
 * Structure to specify mailbox command to be sent to PUNIT.
 */
struct isst_if_mbox_cmd {
	__u32 logical_cpu;
	__u32 parameter;
	__u32 req_data;
	__u32 resp_data;
	__u16 command;
	__u16 sub_command;
	__u32 reserved;
};

/**
 * struct isst_if_mbox_cmds - structure for mailbox commands
 * @cmd_count:	Number of mailbox commands in mbox_cmd[]
 * @mbox_cmd[]:	Holds one or more mbox commands
 *
 * This structure used with ioctl ISST_IF_MBOX_COMMAND to send
 * one or more mailbox commands to PUNIT. Here IOCTL return value
 * indicates number of commands sent or error number if no commands have
 * been sent.
 */
struct isst_if_mbox_cmds {
	__u32 cmd_count;
	struct isst_if_mbox_cmd mbox_cmd[1];
};

/**
 * struct isst_if_msr_cmd - Structure to define msr command
 * @read_write:		Value 0: Read, 1: Write
 * @logical_cpu:	Logical CPU number
 * @msr:		MSR number
 * @data:		For write operation, data to write, for read
 *			place holder
 *
 * Structure to specify MSR command related to PUNIT.
 */
struct isst_if_msr_cmd {
	__u32 read_write; /* Read:0, Write:1 */
	__u32 logical_cpu;
	__u64 msr;
	__u64 data;
};

/**
 * struct isst_if_msr_cmds - structure for msr commands
 * @cmd_count:	Number of mailbox commands in msr_cmd[]
 * @msr_cmd[]:	Holds one or more msr commands
 *
 * This structure used with ioctl ISST_IF_MSR_COMMAND to send
 * one or more MSR commands. IOCTL return value indicates number of
 * commands sent or error number if no commands have been sent.
 */
struct isst_if_msr_cmds {
	__u32 cmd_count;
	struct isst_if_msr_cmd msr_cmd[1];
};

/**
 * struct isst_core_power - Structure to get/set core_power feature
 * @get_set:	0: Get, 1: Set
 * @socket_id:	Socket/package id
 * @power_domain: Power Domain id
 * @enable:	Feature enable status
 * @priority_type: Priority type for the feature (ordered/proportional)
 *
 * Structure to get/set core_power feature state using IOCTL
 * ISST_IF_CORE_POWER_STATE.
 */
struct isst_core_power {
	__u8 get_set;
	__u8 socket_id;
	__u8 power_domain_id;
	__u8 enable;
	__u8 supported;
	__u8 priority_type;
};

/**
 * struct isst_clos_param - Structure to get/set clos praram
 * @get_set:	0: Get, 1: Set
 * @socket_id:	Socket/package id
 * @power_domain:	Power Domain id
 * clos:	Clos ID for the parameters
 * min_freq_mhz: Minimum frequency in MHz
 * max_freq_mhz: Maximum frequency in MHz
 * prop_prio:	Proportional priority from 0-15
 *
 * Structure to get/set per clos property using IOCTL
 * ISST_IF_CLOS_PARAM.
 */
struct isst_clos_param {
	__u8 get_set;
	__u8 socket_id;
	__u8 power_domain_id;
	__u8 clos;
	__u16 min_freq_mhz;
	__u16 max_freq_mhz;
	__u8 prop_prio;
};

/**
 * struct isst_if_clos_assoc - Structure to assign clos to a CPU
 * @socket_id:	Socket/package id
 * @power_domain:	Power Domain id
 * @logical_cpu: CPU number
 * @clos:	Clos ID to assign to the logical CPU
 *
 * Structure to get/set core_power feature.
 */
struct isst_if_clos_assoc {
	__u8 socket_id;
	__u8 power_domain_id;
	__u16 logical_cpu;
	__u16 clos;
};

/**
 * struct isst_if_clos_assoc_cmds - Structure to assign clos to CPUs
 * @cmd_count:	Number of cmds (cpus) in this request
 * @get_set:	Request is for get or set
 * @punit_cpu_map: Set to 1 if the CPU number is punit numbering not
 *		   Linux CPU number
 *
 * Structure used to get/set associate CPUs to clos using IOCTL
 * ISST_IF_CLOS_ASSOC.
 */
struct isst_if_clos_assoc_cmds {
	__u16 cmd_count;
	__u16 get_set;
	__u16 punit_cpu_map;
	struct isst_if_clos_assoc assoc_info[1];
};

/**
 * struct isst_tpmi_instance_count - Get number of TPMI instances per socket
 * @socket_id:	Socket/package id
 * @count:	Number of instances
 * @valid_mask: Mask of instances as there can be holes
 *
 * Structure used to get TPMI instances information using
 * IOCTL ISST_IF_COUNT_TPMI_INSTANCES.
 */
struct isst_tpmi_instance_count {
	__u8 socket_id;
	__u8 count;
	__u16 valid_mask;
};

/**
 * struct isst_perf_level_info - Structure to get information on SST-PP levels
 * @socket_id:	Socket/package id
 * @power_domain:	Power Domain id
 * @logical_cpu: CPU number
 * @clos:	Clos ID to assign to the logical CPU
 * @max_level: Maximum performance level supported by the platform
 * @feature_rev: The feature revision for SST-PP supported by the platform
 * @level_mask: Mask of supported performance levels
 * @current_level: Current performance level
 * @feature_state: SST-BF and SST-TF (enabled/disabled) status at current level
 * @locked: SST-PP performance level change is locked/unlocked
 * @enabled: SST-PP feature is enabled or not
 * @sst-tf_support: SST-TF support status at this level
 * @sst-bf_support: SST-BF support status at this level
 *
 * Structure to get SST-PP details using IOCTL ISST_IF_PERF_LEVELS.
 */
struct isst_perf_level_info {
	__u8 socket_id;
	__u8 power_domain_id;
	__u8 max_level;
	__u8 feature_rev;
	__u8 level_mask;
	__u8 current_level;
	__u8 feature_state;
	__u8 locked;
	__u8 enabled;
	__u8 sst_tf_support;
	__u8 sst_bf_support;
};

/**
 * struct isst_perf_level_control - Structure to set SST-PP level
 * @socket_id:	Socket/package id
 * @power_domain:	Power Domain id
 * @level:	level to set
 *
 * Structure used change SST-PP level using IOCTL ISST_IF_PERF_SET_LEVEL.
 */
struct isst_perf_level_control {
	__u8 socket_id;
	__u8 power_domain_id;
	__u8 level;
};

/**
 * struct isst_perf_feature_control - Structure to activate SST-BF/SST-TF
 * @socket_id:	Socket/package id
 * @power_domain:	Power Domain id
 * @feature:	bit 0 = SST-BF state, bit 1 = SST-TF state
 *
 * Structure used to enable SST-BF/SST-TF using IOCTL ISST_IF_PERF_SET_FEATURE.
 */
struct isst_perf_feature_control {
	__u8 socket_id;
	__u8 power_domain_id;
	__u8 feature;
};

#define TRL_MAX_BUCKETS	8
#define TRL_MAX_LEVELS		6

/**
 * struct isst_perf_level_data_info - Structure to get SST-PP level details
 * @socket_id:	Socket/package id
 * @power_domain:	Power Domain id
 * @level:	SST-PP level for which caller wants to get information
 * @tdp_ratio: TDP Ratio
 * @base_freq_mhz: Base frequency in MHz
 * @base_freq_avx2_mhz: AVX2 Base frequency in MHz
 * @base_freq_avx512_mhz: AVX512 base frequency in MHz
 * @base_freq_amx_mhz: AMX base frequency in MHz
 * @thermal_design_power_w: Thermal design (TDP) power
 * @tjunction_max_c: Max junction temperature
 * @max_memory_freq_mhz: Max memory frequency in MHz
 * @cooling_type: Type of cooling is used
 * @p0_freq_mhz: core maximum frequency
 * @p1_freq_mhz: Core TDP frequency
 * @pn_freq_mhz: Core maximum efficiency frequency
 * @pm_freq_mhz: Core minimum frequency
 * @p0_fabric_freq_mhz: Fabric (Uncore) maximum frequency
 * @p1_fabric_freq_mhz: Fabric (Uncore) TDP frequency
 * @pn_fabric_freq_mhz: Fabric (Uncore) minimum efficiency frequency
 * @pm_fabric_freq_mhz: Fabric (Uncore) minimum frequency
 * @max_buckets: Maximum trl buckets
 * @max_trl_levels: Maximum trl levels
 * @bucket_core_counts[TRL_MAX_BUCKETS]: Number of cores per bucket
 * @trl_freq_mhz[TRL_MAX_LEVELS][TRL_MAX_BUCKETS]: maximum frequency
 * for a bucket and trl level
 *
 * Structure used to get information on frequencies and TDP for a SST-PP
 * level using ISST_IF_GET_PERF_LEVEL_INFO.
 */
struct isst_perf_level_data_info {
	__u8 socket_id;
	__u8 power_domain_id;
	__u16 level;
	__u16 tdp_ratio;
	__u16 base_freq_mhz;
	__u16 base_freq_avx2_mhz;
	__u16 base_freq_avx512_mhz;
	__u16 base_freq_amx_mhz;
	__u16 thermal_design_power_w;
	__u16 tjunction_max_c;
	__u16 max_memory_freq_mhz;
	__u16 cooling_type;
	__u16 p0_freq_mhz;
	__u16 p1_freq_mhz;
	__u16 pn_freq_mhz;
	__u16 pm_freq_mhz;
	__u16 p0_fabric_freq_mhz;
	__u16 p1_fabric_freq_mhz;
	__u16 pn_fabric_freq_mhz;
	__u16 pm_fabric_freq_mhz;
	__u16 max_buckets;
	__u16 max_trl_levels;
	__u16 bucket_core_counts[TRL_MAX_BUCKETS];
	__u16 trl_freq_mhz[TRL_MAX_LEVELS][TRL_MAX_BUCKETS];
};

/**
 * struct isst_perf_level_cpu_mask - Structure to get SST-PP level CPU mask
 * @socket_id:	Socket/package id
 * @power_domain:	Power Domain id
 * @level:	SST-PP level for which caller wants to get information
 * @punit_cpu_map: Set to 1 if the CPU number is punit numbering not
 *		   Linux CPU number. If 0 CPU buffer is copied to user space
 *		   supplied cpu_buffer of size cpu_buffer_size. Punit
 *		   cpu mask is copied to "mask" field.
 * @mask:	cpu mask for this PP level (punit CPU numbering)
 * @cpu_buffer_size: size of cpu_buffer also used to return the copied CPU
 *		buffer size.
 * @cpu_buffer:	Buffer to copy CPU mask when punit_cpu_map is 0
 *
 * Structure used to get cpumask for a SST-PP level using
 * IOCTL ISST_IF_GET_PERF_LEVEL_CPU_MASK. Also used to get CPU mask for
 * IOCTL ISST_IF_GET_BASE_FREQ_CPU_MASK for SST-BF.
 */
struct isst_perf_level_cpu_mask {
	__u8 socket_id;
	__u8 power_domain_id;
	__u8 level;
	__u8 punit_cpu_map;
	__u64 mask;
	__u16 cpu_buffer_size;
	__s8 cpu_buffer[1];
};

/**
 * struct isst_base_freq_info - Structure to get SST-BF frequencies
 * @socket_id:	Socket/package id
 * @power_domain:	Power Domain id
 * @level:	SST-PP level for which caller wants to get information
 * @high_base_freq_mhz: High priority CPU base frequency
 * @low_base_freq_mhz: Low priority CPU base frequency
 * @tjunction_max_c: Max junction temperature
 * @thermal_design_power_w: Thermal design power in watts
 *
 * Structure used to get SST-BF information using
 * IOCTL ISST_IF_GET_BASE_FREQ_INFO.
 */
struct isst_base_freq_info {
	__u8 socket_id;
	__u8 power_domain_id;
	__u16 level;
	__u16 high_base_freq_mhz;
	__u16 low_base_freq_mhz;
	__u16 tjunction_max_c;
	__u16 thermal_design_power_w;
};

/**
 * struct isst_turbo_freq_info - Structure to get SST-TF frequencies
 * @socket_id:	Socket/package id
 * @power_domain:	Power Domain id
 * @level:	SST-PP level for which caller wants to get information
 * @max_clip_freqs: Maximum number of low priority core clipping frequencies
 * @lp_clip_freq_mhz: Clip frequencies per trl level
 * @bucket_core_counts: Maximum number of cores for a bucket
 * @trl_freq_mhz: Frequencies per trl level for each bucket
 *
 * Structure used to get SST-TF information using
 * IOCTL ISST_IF_GET_TURBO_FREQ_INFO.
 */
struct isst_turbo_freq_info {
	__u8 socket_id;
	__u8 power_domain_id;
	__u16 level;
	__u16 max_clip_freqs;
	__u16 max_buckets;
	__u16 max_trl_levels;
	__u16 lp_clip_freq_mhz[TRL_MAX_LEVELS];
	__u16 bucket_core_counts[TRL_MAX_BUCKETS];
	__u16 trl_freq_mhz[TRL_MAX_LEVELS][TRL_MAX_BUCKETS];
};

#define ISST_IF_MAGIC			0xFE
#define ISST_IF_GET_PLATFORM_INFO	_IOR(ISST_IF_MAGIC, 0, struct isst_if_platform_info *)
#define ISST_IF_GET_PHY_ID		_IOWR(ISST_IF_MAGIC, 1, struct isst_if_cpu_map *)
#define ISST_IF_IO_CMD		_IOW(ISST_IF_MAGIC, 2, struct isst_if_io_regs *)
#define ISST_IF_MBOX_COMMAND	_IOWR(ISST_IF_MAGIC, 3, struct isst_if_mbox_cmds *)
#define ISST_IF_MSR_COMMAND	_IOWR(ISST_IF_MAGIC, 4, struct isst_if_msr_cmds *)

#define ISST_IF_COUNT_TPMI_INSTANCES	_IOR(ISST_IF_MAGIC, 5, struct isst_tpmi_instance_count *)
#define ISST_IF_CORE_POWER_STATE _IOWR(ISST_IF_MAGIC, 6, struct isst_core_power *)
#define ISST_IF_CLOS_PARAM	_IOWR(ISST_IF_MAGIC, 7, struct isst_clos_param *)
#define ISST_IF_CLOS_ASSOC	_IOWR(ISST_IF_MAGIC, 8, struct isst_if_clos_assoc_cmds *)

#define ISST_IF_PERF_LEVELS	_IOWR(ISST_IF_MAGIC, 9, struct isst_perf_level_info *)
#define ISST_IF_PERF_SET_LEVEL	_IOW(ISST_IF_MAGIC, 10, struct isst_perf_level_control *)
#define ISST_IF_PERF_SET_FEATURE _IOW(ISST_IF_MAGIC, 11, struct isst_perf_feature_control *)
#define ISST_IF_GET_PERF_LEVEL_INFO	_IOR(ISST_IF_MAGIC, 12, struct isst_perf_level_data_info *)
#define ISST_IF_GET_PERF_LEVEL_CPU_MASK	_IOR(ISST_IF_MAGIC, 13, struct isst_perf_level_cpu_mask *)
#define ISST_IF_GET_BASE_FREQ_INFO	_IOR(ISST_IF_MAGIC, 14, struct isst_base_freq_info *)
#define ISST_IF_GET_BASE_FREQ_CPU_MASK	_IOR(ISST_IF_MAGIC, 15, struct isst_perf_level_cpu_mask *)
#define ISST_IF_GET_TURBO_FREQ_INFO	_IOR(ISST_IF_MAGIC, 16, struct isst_turbo_freq_info *)

#endif
