/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
 *
 * Copyright 2016-2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef HABANALABS_H_
#define HABANALABS_H_

#include <linux/types.h>
#include <linux/ioctl.h>

/*
 * Defines that are asic-specific but constitutes as ABI between kernel driver
 * and userspace
 */
#define GOYA_KMD_SRAM_RESERVED_SIZE_FROM_START		0x8000	/* 32KB */
#define GAUDI_DRIVER_SRAM_RESERVED_SIZE_FROM_START	0x80	/* 128 bytes */

/*
 * 128 SOBs reserved for collective wait
 * 16 SOBs reserved for sync stream
 */
#define GAUDI_FIRST_AVAILABLE_W_S_SYNC_OBJECT		144

/*
 * 64 monitors reserved for collective wait
 * 8 monitors reserved for sync stream
 */
#define GAUDI_FIRST_AVAILABLE_W_S_MONITOR		72

/*
 * Goya queue Numbering
 *
 * The external queues (PCI DMA channels) MUST be before the internal queues
 * and each group (PCI DMA channels and internal) must be contiguous inside
 * itself but there can be a gap between the two groups (although not
 * recommended)
 */

enum goya_queue_id {
	GOYA_QUEUE_ID_DMA_0 = 0,
	GOYA_QUEUE_ID_DMA_1 = 1,
	GOYA_QUEUE_ID_DMA_2 = 2,
	GOYA_QUEUE_ID_DMA_3 = 3,
	GOYA_QUEUE_ID_DMA_4 = 4,
	GOYA_QUEUE_ID_CPU_PQ = 5,
	GOYA_QUEUE_ID_MME = 6,	/* Internal queues start here */
	GOYA_QUEUE_ID_TPC0 = 7,
	GOYA_QUEUE_ID_TPC1 = 8,
	GOYA_QUEUE_ID_TPC2 = 9,
	GOYA_QUEUE_ID_TPC3 = 10,
	GOYA_QUEUE_ID_TPC4 = 11,
	GOYA_QUEUE_ID_TPC5 = 12,
	GOYA_QUEUE_ID_TPC6 = 13,
	GOYA_QUEUE_ID_TPC7 = 14,
	GOYA_QUEUE_ID_SIZE
};

/*
 * Gaudi queue Numbering
 * External queues (PCI DMA channels) are DMA_0_*, DMA_1_* and DMA_5_*.
 * Except one CPU queue, all the rest are internal queues.
 */

enum gaudi_queue_id {
	GAUDI_QUEUE_ID_DMA_0_0 = 0,	/* external */
	GAUDI_QUEUE_ID_DMA_0_1 = 1,	/* external */
	GAUDI_QUEUE_ID_DMA_0_2 = 2,	/* external */
	GAUDI_QUEUE_ID_DMA_0_3 = 3,	/* external */
	GAUDI_QUEUE_ID_DMA_1_0 = 4,	/* external */
	GAUDI_QUEUE_ID_DMA_1_1 = 5,	/* external */
	GAUDI_QUEUE_ID_DMA_1_2 = 6,	/* external */
	GAUDI_QUEUE_ID_DMA_1_3 = 7,	/* external */
	GAUDI_QUEUE_ID_CPU_PQ = 8,	/* CPU */
	GAUDI_QUEUE_ID_DMA_2_0 = 9,	/* internal */
	GAUDI_QUEUE_ID_DMA_2_1 = 10,	/* internal */
	GAUDI_QUEUE_ID_DMA_2_2 = 11,	/* internal */
	GAUDI_QUEUE_ID_DMA_2_3 = 12,	/* internal */
	GAUDI_QUEUE_ID_DMA_3_0 = 13,	/* internal */
	GAUDI_QUEUE_ID_DMA_3_1 = 14,	/* internal */
	GAUDI_QUEUE_ID_DMA_3_2 = 15,	/* internal */
	GAUDI_QUEUE_ID_DMA_3_3 = 16,	/* internal */
	GAUDI_QUEUE_ID_DMA_4_0 = 17,	/* internal */
	GAUDI_QUEUE_ID_DMA_4_1 = 18,	/* internal */
	GAUDI_QUEUE_ID_DMA_4_2 = 19,	/* internal */
	GAUDI_QUEUE_ID_DMA_4_3 = 20,	/* internal */
	GAUDI_QUEUE_ID_DMA_5_0 = 21,	/* internal */
	GAUDI_QUEUE_ID_DMA_5_1 = 22,	/* internal */
	GAUDI_QUEUE_ID_DMA_5_2 = 23,	/* internal */
	GAUDI_QUEUE_ID_DMA_5_3 = 24,	/* internal */
	GAUDI_QUEUE_ID_DMA_6_0 = 25,	/* internal */
	GAUDI_QUEUE_ID_DMA_6_1 = 26,	/* internal */
	GAUDI_QUEUE_ID_DMA_6_2 = 27,	/* internal */
	GAUDI_QUEUE_ID_DMA_6_3 = 28,	/* internal */
	GAUDI_QUEUE_ID_DMA_7_0 = 29,	/* internal */
	GAUDI_QUEUE_ID_DMA_7_1 = 30,	/* internal */
	GAUDI_QUEUE_ID_DMA_7_2 = 31,	/* internal */
	GAUDI_QUEUE_ID_DMA_7_3 = 32,	/* internal */
	GAUDI_QUEUE_ID_MME_0_0 = 33,	/* internal */
	GAUDI_QUEUE_ID_MME_0_1 = 34,	/* internal */
	GAUDI_QUEUE_ID_MME_0_2 = 35,	/* internal */
	GAUDI_QUEUE_ID_MME_0_3 = 36,	/* internal */
	GAUDI_QUEUE_ID_MME_1_0 = 37,	/* internal */
	GAUDI_QUEUE_ID_MME_1_1 = 38,	/* internal */
	GAUDI_QUEUE_ID_MME_1_2 = 39,	/* internal */
	GAUDI_QUEUE_ID_MME_1_3 = 40,	/* internal */
	GAUDI_QUEUE_ID_TPC_0_0 = 41,	/* internal */
	GAUDI_QUEUE_ID_TPC_0_1 = 42,	/* internal */
	GAUDI_QUEUE_ID_TPC_0_2 = 43,	/* internal */
	GAUDI_QUEUE_ID_TPC_0_3 = 44,	/* internal */
	GAUDI_QUEUE_ID_TPC_1_0 = 45,	/* internal */
	GAUDI_QUEUE_ID_TPC_1_1 = 46,	/* internal */
	GAUDI_QUEUE_ID_TPC_1_2 = 47,	/* internal */
	GAUDI_QUEUE_ID_TPC_1_3 = 48,	/* internal */
	GAUDI_QUEUE_ID_TPC_2_0 = 49,	/* internal */
	GAUDI_QUEUE_ID_TPC_2_1 = 50,	/* internal */
	GAUDI_QUEUE_ID_TPC_2_2 = 51,	/* internal */
	GAUDI_QUEUE_ID_TPC_2_3 = 52,	/* internal */
	GAUDI_QUEUE_ID_TPC_3_0 = 53,	/* internal */
	GAUDI_QUEUE_ID_TPC_3_1 = 54,	/* internal */
	GAUDI_QUEUE_ID_TPC_3_2 = 55,	/* internal */
	GAUDI_QUEUE_ID_TPC_3_3 = 56,	/* internal */
	GAUDI_QUEUE_ID_TPC_4_0 = 57,	/* internal */
	GAUDI_QUEUE_ID_TPC_4_1 = 58,	/* internal */
	GAUDI_QUEUE_ID_TPC_4_2 = 59,	/* internal */
	GAUDI_QUEUE_ID_TPC_4_3 = 60,	/* internal */
	GAUDI_QUEUE_ID_TPC_5_0 = 61,	/* internal */
	GAUDI_QUEUE_ID_TPC_5_1 = 62,	/* internal */
	GAUDI_QUEUE_ID_TPC_5_2 = 63,	/* internal */
	GAUDI_QUEUE_ID_TPC_5_3 = 64,	/* internal */
	GAUDI_QUEUE_ID_TPC_6_0 = 65,	/* internal */
	GAUDI_QUEUE_ID_TPC_6_1 = 66,	/* internal */
	GAUDI_QUEUE_ID_TPC_6_2 = 67,	/* internal */
	GAUDI_QUEUE_ID_TPC_6_3 = 68,	/* internal */
	GAUDI_QUEUE_ID_TPC_7_0 = 69,	/* internal */
	GAUDI_QUEUE_ID_TPC_7_1 = 70,	/* internal */
	GAUDI_QUEUE_ID_TPC_7_2 = 71,	/* internal */
	GAUDI_QUEUE_ID_TPC_7_3 = 72,	/* internal */
	GAUDI_QUEUE_ID_NIC_0_0 = 73,	/* internal */
	GAUDI_QUEUE_ID_NIC_0_1 = 74,	/* internal */
	GAUDI_QUEUE_ID_NIC_0_2 = 75,	/* internal */
	GAUDI_QUEUE_ID_NIC_0_3 = 76,	/* internal */
	GAUDI_QUEUE_ID_NIC_1_0 = 77,	/* internal */
	GAUDI_QUEUE_ID_NIC_1_1 = 78,	/* internal */
	GAUDI_QUEUE_ID_NIC_1_2 = 79,	/* internal */
	GAUDI_QUEUE_ID_NIC_1_3 = 80,	/* internal */
	GAUDI_QUEUE_ID_NIC_2_0 = 81,	/* internal */
	GAUDI_QUEUE_ID_NIC_2_1 = 82,	/* internal */
	GAUDI_QUEUE_ID_NIC_2_2 = 83,	/* internal */
	GAUDI_QUEUE_ID_NIC_2_3 = 84,	/* internal */
	GAUDI_QUEUE_ID_NIC_3_0 = 85,	/* internal */
	GAUDI_QUEUE_ID_NIC_3_1 = 86,	/* internal */
	GAUDI_QUEUE_ID_NIC_3_2 = 87,	/* internal */
	GAUDI_QUEUE_ID_NIC_3_3 = 88,	/* internal */
	GAUDI_QUEUE_ID_NIC_4_0 = 89,	/* internal */
	GAUDI_QUEUE_ID_NIC_4_1 = 90,	/* internal */
	GAUDI_QUEUE_ID_NIC_4_2 = 91,	/* internal */
	GAUDI_QUEUE_ID_NIC_4_3 = 92,	/* internal */
	GAUDI_QUEUE_ID_NIC_5_0 = 93,	/* internal */
	GAUDI_QUEUE_ID_NIC_5_1 = 94,	/* internal */
	GAUDI_QUEUE_ID_NIC_5_2 = 95,	/* internal */
	GAUDI_QUEUE_ID_NIC_5_3 = 96,	/* internal */
	GAUDI_QUEUE_ID_NIC_6_0 = 97,	/* internal */
	GAUDI_QUEUE_ID_NIC_6_1 = 98,	/* internal */
	GAUDI_QUEUE_ID_NIC_6_2 = 99,	/* internal */
	GAUDI_QUEUE_ID_NIC_6_3 = 100,	/* internal */
	GAUDI_QUEUE_ID_NIC_7_0 = 101,	/* internal */
	GAUDI_QUEUE_ID_NIC_7_1 = 102,	/* internal */
	GAUDI_QUEUE_ID_NIC_7_2 = 103,	/* internal */
	GAUDI_QUEUE_ID_NIC_7_3 = 104,	/* internal */
	GAUDI_QUEUE_ID_NIC_8_0 = 105,	/* internal */
	GAUDI_QUEUE_ID_NIC_8_1 = 106,	/* internal */
	GAUDI_QUEUE_ID_NIC_8_2 = 107,	/* internal */
	GAUDI_QUEUE_ID_NIC_8_3 = 108,	/* internal */
	GAUDI_QUEUE_ID_NIC_9_0 = 109,	/* internal */
	GAUDI_QUEUE_ID_NIC_9_1 = 110,	/* internal */
	GAUDI_QUEUE_ID_NIC_9_2 = 111,	/* internal */
	GAUDI_QUEUE_ID_NIC_9_3 = 112,	/* internal */
	GAUDI_QUEUE_ID_SIZE
};

/*
 * Engine Numbering
 *
 * Used in the "busy_engines_mask" field in `struct hl_info_hw_idle'
 */

enum goya_engine_id {
	GOYA_ENGINE_ID_DMA_0 = 0,
	GOYA_ENGINE_ID_DMA_1,
	GOYA_ENGINE_ID_DMA_2,
	GOYA_ENGINE_ID_DMA_3,
	GOYA_ENGINE_ID_DMA_4,
	GOYA_ENGINE_ID_MME_0,
	GOYA_ENGINE_ID_TPC_0,
	GOYA_ENGINE_ID_TPC_1,
	GOYA_ENGINE_ID_TPC_2,
	GOYA_ENGINE_ID_TPC_3,
	GOYA_ENGINE_ID_TPC_4,
	GOYA_ENGINE_ID_TPC_5,
	GOYA_ENGINE_ID_TPC_6,
	GOYA_ENGINE_ID_TPC_7,
	GOYA_ENGINE_ID_SIZE
};

enum gaudi_engine_id {
	GAUDI_ENGINE_ID_DMA_0 = 0,
	GAUDI_ENGINE_ID_DMA_1,
	GAUDI_ENGINE_ID_DMA_2,
	GAUDI_ENGINE_ID_DMA_3,
	GAUDI_ENGINE_ID_DMA_4,
	GAUDI_ENGINE_ID_DMA_5,
	GAUDI_ENGINE_ID_DMA_6,
	GAUDI_ENGINE_ID_DMA_7,
	GAUDI_ENGINE_ID_MME_0,
	GAUDI_ENGINE_ID_MME_1,
	GAUDI_ENGINE_ID_MME_2,
	GAUDI_ENGINE_ID_MME_3,
	GAUDI_ENGINE_ID_TPC_0,
	GAUDI_ENGINE_ID_TPC_1,
	GAUDI_ENGINE_ID_TPC_2,
	GAUDI_ENGINE_ID_TPC_3,
	GAUDI_ENGINE_ID_TPC_4,
	GAUDI_ENGINE_ID_TPC_5,
	GAUDI_ENGINE_ID_TPC_6,
	GAUDI_ENGINE_ID_TPC_7,
	GAUDI_ENGINE_ID_NIC_0,
	GAUDI_ENGINE_ID_NIC_1,
	GAUDI_ENGINE_ID_NIC_2,
	GAUDI_ENGINE_ID_NIC_3,
	GAUDI_ENGINE_ID_NIC_4,
	GAUDI_ENGINE_ID_NIC_5,
	GAUDI_ENGINE_ID_NIC_6,
	GAUDI_ENGINE_ID_NIC_7,
	GAUDI_ENGINE_ID_NIC_8,
	GAUDI_ENGINE_ID_NIC_9,
	GAUDI_ENGINE_ID_SIZE
};

/*
 * ASIC specific PLL index
 *
 * Used to retrieve in frequency info of different IPs via
 * HL_INFO_PLL_FREQUENCY under HL_IOCTL_INFO IOCTL. The enums need to be
 * used as an index in struct hl_pll_frequency_info
 */

enum hl_goya_pll_index {
	HL_GOYA_CPU_PLL = 0,
	HL_GOYA_IC_PLL,
	HL_GOYA_MC_PLL,
	HL_GOYA_MME_PLL,
	HL_GOYA_PCI_PLL,
	HL_GOYA_EMMC_PLL,
	HL_GOYA_TPC_PLL,
	HL_GOYA_PLL_MAX
};

enum hl_gaudi_pll_index {
	HL_GAUDI_CPU_PLL = 0,
	HL_GAUDI_PCI_PLL,
	HL_GAUDI_SRAM_PLL,
	HL_GAUDI_HBM_PLL,
	HL_GAUDI_NIC_PLL,
	HL_GAUDI_DMA_PLL,
	HL_GAUDI_MESH_PLL,
	HL_GAUDI_MME_PLL,
	HL_GAUDI_TPC_PLL,
	HL_GAUDI_IF_PLL,
	HL_GAUDI_PLL_MAX
};

enum hl_device_status {
	HL_DEVICE_STATUS_OPERATIONAL,
	HL_DEVICE_STATUS_IN_RESET,
	HL_DEVICE_STATUS_MALFUNCTION,
	HL_DEVICE_STATUS_NEEDS_RESET
};

/* Opcode for management ioctl
 *
 * HW_IP_INFO            - Receive information about different IP blocks in the
 *                         device.
 * HL_INFO_HW_EVENTS     - Receive an array describing how many times each event
 *                         occurred since the last hard reset.
 * HL_INFO_DRAM_USAGE    - Retrieve the dram usage inside the device and of the
 *                         specific context. This is relevant only for devices
 *                         where the dram is managed by the kernel driver
 * HL_INFO_HW_IDLE       - Retrieve information about the idle status of each
 *                         internal engine.
 * HL_INFO_DEVICE_STATUS - Retrieve the device's status. This opcode doesn't
 *                         require an open context.
 * HL_INFO_DEVICE_UTILIZATION  - Retrieve the total utilization of the device
 *                               over the last period specified by the user.
 *                               The period can be between 100ms to 1s, in
 *                               resolution of 100ms. The return value is a
 *                               percentage of the utilization rate.
 * HL_INFO_HW_EVENTS_AGGREGATE - Receive an array describing how many times each
 *                               event occurred since the driver was loaded.
 * HL_INFO_CLK_RATE            - Retrieve the current and maximum clock rate
 *                               of the device in MHz. The maximum clock rate is
 *                               configurable via sysfs parameter
 * HL_INFO_RESET_COUNT   - Retrieve the counts of the soft and hard reset
 *                         operations performed on the device since the last
 *                         time the driver was loaded.
 * HL_INFO_TIME_SYNC     - Retrieve the device's time alongside the host's time
 *                         for synchronization.
 * HL_INFO_CS_COUNTERS   - Retrieve command submission counters
 * HL_INFO_PCI_COUNTERS  - Retrieve PCI counters
 * HL_INFO_CLK_THROTTLE_REASON - Retrieve clock throttling reason
 * HL_INFO_SYNC_MANAGER  - Retrieve sync manager info per dcore
 * HL_INFO_TOTAL_ENERGY  - Retrieve total energy consumption
 * HL_INFO_PLL_FREQUENCY - Retrieve PLL frequency
 * HL_INFO_OPEN_STATS    - Retrieve info regarding recent device open calls
 */
#define HL_INFO_HW_IP_INFO		0
#define HL_INFO_HW_EVENTS		1
#define HL_INFO_DRAM_USAGE		2
#define HL_INFO_HW_IDLE			3
#define HL_INFO_DEVICE_STATUS		4
#define HL_INFO_DEVICE_UTILIZATION	6
#define HL_INFO_HW_EVENTS_AGGREGATE	7
#define HL_INFO_CLK_RATE		8
#define HL_INFO_RESET_COUNT		9
#define HL_INFO_TIME_SYNC		10
#define HL_INFO_CS_COUNTERS		11
#define HL_INFO_PCI_COUNTERS		12
#define HL_INFO_CLK_THROTTLE_REASON	13
#define HL_INFO_SYNC_MANAGER		14
#define HL_INFO_TOTAL_ENERGY		15
#define HL_INFO_PLL_FREQUENCY		16
#define HL_INFO_POWER			17
#define HL_INFO_OPEN_STATS		18

#define HL_INFO_VERSION_MAX_LEN	128
#define HL_INFO_CARD_NAME_MAX_LEN	16

struct hl_info_hw_ip_info {
	__u64 sram_base_address;
	__u64 dram_base_address;
	__u64 dram_size;
	__u32 sram_size;
	__u32 num_of_events;
	__u32 device_id; /* PCI Device ID */
	__u32 module_id; /* For mezzanine cards in servers (From OCP spec.) */
	__u32 reserved;
	__u16 first_available_interrupt_id;
	__u16 reserved2;
	__u32 cpld_version;
	__u32 psoc_pci_pll_nr;
	__u32 psoc_pci_pll_nf;
	__u32 psoc_pci_pll_od;
	__u32 psoc_pci_pll_div_factor;
	__u8 tpc_enabled_mask;
	__u8 dram_enabled;
	__u8 pad[2];
	__u8 cpucp_version[HL_INFO_VERSION_MAX_LEN];
	__u8 card_name[HL_INFO_CARD_NAME_MAX_LEN];
	__u64 reserved3;
	__u64 dram_page_size;
};

struct hl_info_dram_usage {
	__u64 dram_free_mem;
	__u64 ctx_dram_mem;
};

#define HL_BUSY_ENGINES_MASK_EXT_SIZE	2

struct hl_info_hw_idle {
	__u32 is_idle;
	/*
	 * Bitmask of busy engines.
	 * Bits definition is according to `enum <chip>_enging_id'.
	 */
	__u32 busy_engines_mask;

	/*
	 * Extended Bitmask of busy engines.
	 * Bits definition is according to `enum <chip>_enging_id'.
	 */
	__u64 busy_engines_mask_ext[HL_BUSY_ENGINES_MASK_EXT_SIZE];
};

struct hl_info_device_status {
	__u32 status;
	__u32 pad;
};

struct hl_info_device_utilization {
	__u32 utilization;
	__u32 pad;
};

struct hl_info_clk_rate {
	__u32 cur_clk_rate_mhz;
	__u32 max_clk_rate_mhz;
};

struct hl_info_reset_count {
	__u32 hard_reset_cnt;
	__u32 soft_reset_cnt;
};

struct hl_info_time_sync {
	__u64 device_time;
	__u64 host_time;
};

/**
 * struct hl_info_pci_counters - pci counters
 * @rx_throughput: PCI rx throughput KBps
 * @tx_throughput: PCI tx throughput KBps
 * @replay_cnt: PCI replay counter
 */
struct hl_info_pci_counters {
	__u64 rx_throughput;
	__u64 tx_throughput;
	__u64 replay_cnt;
};

#define HL_CLK_THROTTLE_POWER	0x1
#define HL_CLK_THROTTLE_THERMAL	0x2

/**
 * struct hl_info_clk_throttle - clock throttling reason
 * @clk_throttling_reason: each bit represents a clk throttling reason
 */
struct hl_info_clk_throttle {
	__u32 clk_throttling_reason;
};

/**
 * struct hl_info_energy - device energy information
 * @total_energy_consumption: total device energy consumption
 */
struct hl_info_energy {
	__u64 total_energy_consumption;
};

#define HL_PLL_NUM_OUTPUTS 4

struct hl_pll_frequency_info {
	__u16 output[HL_PLL_NUM_OUTPUTS];
};

/**
 * struct hl_open_stats_info - device open statistics information
 * @open_counter: ever growing counter, increased on each successful dev open
 * @last_open_period_ms: duration (ms) device was open last time
 */
struct hl_open_stats_info {
	__u64 open_counter;
	__u64 last_open_period_ms;
};

/**
 * struct hl_power_info - power information
 * @power: power consumption
 */
struct hl_power_info {
	__u64 power;
};

/**
 * struct hl_info_sync_manager - sync manager information
 * @first_available_sync_object: first available sob
 * @first_available_monitor: first available monitor
 * @first_available_cq: first available cq
 */
struct hl_info_sync_manager {
	__u32 first_available_sync_object;
	__u32 first_available_monitor;
	__u32 first_available_cq;
	__u32 reserved;
};

/**
 * struct hl_info_cs_counters - command submission counters
 * @total_out_of_mem_drop_cnt: total dropped due to memory allocation issue
 * @ctx_out_of_mem_drop_cnt: context dropped due to memory allocation issue
 * @total_parsing_drop_cnt: total dropped due to error in packet parsing
 * @ctx_parsing_drop_cnt: context dropped due to error in packet parsing
 * @total_queue_full_drop_cnt: total dropped due to queue full
 * @ctx_queue_full_drop_cnt: context dropped due to queue full
 * @total_device_in_reset_drop_cnt: total dropped due to device in reset
 * @ctx_device_in_reset_drop_cnt: context dropped due to device in reset
 * @total_max_cs_in_flight_drop_cnt: total dropped due to maximum CS in-flight
 * @ctx_max_cs_in_flight_drop_cnt: context dropped due to maximum CS in-flight
 * @total_validation_drop_cnt: total dropped due to validation error
 * @ctx_validation_drop_cnt: context dropped due to validation error
 */
struct hl_info_cs_counters {
	__u64 total_out_of_mem_drop_cnt;
	__u64 ctx_out_of_mem_drop_cnt;
	__u64 total_parsing_drop_cnt;
	__u64 ctx_parsing_drop_cnt;
	__u64 total_queue_full_drop_cnt;
	__u64 ctx_queue_full_drop_cnt;
	__u64 total_device_in_reset_drop_cnt;
	__u64 ctx_device_in_reset_drop_cnt;
	__u64 total_max_cs_in_flight_drop_cnt;
	__u64 ctx_max_cs_in_flight_drop_cnt;
	__u64 total_validation_drop_cnt;
	__u64 ctx_validation_drop_cnt;
};

enum gaudi_dcores {
	HL_GAUDI_WS_DCORE,
	HL_GAUDI_WN_DCORE,
	HL_GAUDI_EN_DCORE,
	HL_GAUDI_ES_DCORE
};

struct hl_info_args {
	/* Location of relevant struct in userspace */
	__u64 return_pointer;
	/*
	 * The size of the return value. Just like "size" in "snprintf",
	 * it limits how many bytes the kernel can write
	 *
	 * For hw_events array, the size should be
	 * hl_info_hw_ip_info.num_of_events * sizeof(__u32)
	 */
	__u32 return_size;

	/* HL_INFO_* */
	__u32 op;

	union {
		/* Dcore id for which the information is relevant.
		 * For Gaudi refer to 'enum gaudi_dcores'
		 */
		__u32 dcore_id;
		/* Context ID - Currently not in use */
		__u32 ctx_id;
		/* Period value for utilization rate (100ms - 1000ms, in 100ms
		 * resolution.
		 */
		__u32 period_ms;
		/* PLL frequency retrieval */
		__u32 pll_index;
	};

	__u32 pad;
};

/* Opcode to create a new command buffer */
#define HL_CB_OP_CREATE		0
/* Opcode to destroy previously created command buffer */
#define HL_CB_OP_DESTROY	1
/* Opcode to retrieve information about a command buffer */
#define HL_CB_OP_INFO		2

/* 2MB minus 32 bytes for 2xMSG_PROT */
#define HL_MAX_CB_SIZE		(0x200000 - 32)

/* Indicates whether the command buffer should be mapped to the device's MMU */
#define HL_CB_FLAGS_MAP		0x1

struct hl_cb_in {
	/* Handle of CB or 0 if we want to create one */
	__u64 cb_handle;
	/* HL_CB_OP_* */
	__u32 op;
	/* Size of CB. Maximum size is HL_MAX_CB_SIZE. The minimum size that
	 * will be allocated, regardless of this parameter's value, is PAGE_SIZE
	 */
	__u32 cb_size;
	/* Context ID - Currently not in use */
	__u32 ctx_id;
	/* HL_CB_FLAGS_* */
	__u32 flags;
};

struct hl_cb_out {
	union {
		/* Handle of CB */
		__u64 cb_handle;

		/* Information about CB */
		struct {
			/* Usage count of CB */
			__u32 usage_cnt;
			__u32 pad;
		};
	};
};

union hl_cb_args {
	struct hl_cb_in in;
	struct hl_cb_out out;
};

/* HL_CS_CHUNK_FLAGS_ values
 *
 * HL_CS_CHUNK_FLAGS_USER_ALLOC_CB:
 *      Indicates if the CB was allocated and mapped by userspace.
 *      User allocated CB is a command buffer allocated by the user, via malloc
 *      (or similar). After allocating the CB, the user invokes “memory ioctl”
 *      to map the user memory into a device virtual address. The user provides
 *      this address via the cb_handle field. The interface provides the
 *      ability to create a large CBs, Which aren’t limited to
 *      “HL_MAX_CB_SIZE”. Therefore, it increases the PCI-DMA queues
 *      throughput. This CB allocation method also reduces the use of Linux
 *      DMA-able memory pool. Which are limited and used by other Linux
 *      sub-systems.
 */
#define HL_CS_CHUNK_FLAGS_USER_ALLOC_CB 0x1

/*
 * This structure size must always be fixed to 64-bytes for backward
 * compatibility
 */
struct hl_cs_chunk {
	union {
		/* For external queue, this represents a Handle of CB on the
		 * Host.
		 * For internal queue in Goya, this represents an SRAM or
		 * a DRAM address of the internal CB. In Gaudi, this might also
		 * represent a mapped host address of the CB.
		 *
		 * A mapped host address is in the device address space, after
		 * a host address was mapped by the device MMU.
		 */
		__u64 cb_handle;

		/* Relevant only when HL_CS_FLAGS_WAIT or
		 * HL_CS_FLAGS_COLLECTIVE_WAIT is set.
		 * This holds address of array of u64 values that contain
		 * signal CS sequence numbers. The wait described by this job
		 * will listen on all those signals (wait event per signal)
		 */
		__u64 signal_seq_arr;
	};

	/* Index of queue to put the CB on */
	__u32 queue_index;

	union {
		/*
		 * Size of command buffer with valid packets
		 * Can be smaller then actual CB size
		 */
		__u32 cb_size;

		/* Relevant only when HL_CS_FLAGS_WAIT or
		 * HL_CS_FLAGS_COLLECTIVE_WAIT is set.
		 * Number of entries in signal_seq_arr
		 */
		__u32 num_signal_seq_arr;
	};

	/* HL_CS_CHUNK_FLAGS_* */
	__u32 cs_chunk_flags;

	/* Relevant only when HL_CS_FLAGS_COLLECTIVE_WAIT is set.
	 * This holds the collective engine ID. The wait described by this job
	 * will sync with this engine and with all NICs before completion.
	 */
	__u32 collective_engine_id;

	/* Align structure to 64 bytes */
	__u32 pad[10];
};

/* SIGNAL and WAIT/COLLECTIVE_WAIT flags are mutually exclusive */
#define HL_CS_FLAGS_FORCE_RESTORE		0x1
#define HL_CS_FLAGS_SIGNAL			0x2
#define HL_CS_FLAGS_WAIT			0x4
#define HL_CS_FLAGS_COLLECTIVE_WAIT		0x8
#define HL_CS_FLAGS_TIMESTAMP			0x20
#define HL_CS_FLAGS_STAGED_SUBMISSION		0x40
#define HL_CS_FLAGS_STAGED_SUBMISSION_FIRST	0x80
#define HL_CS_FLAGS_STAGED_SUBMISSION_LAST	0x100
#define HL_CS_FLAGS_CUSTOM_TIMEOUT		0x200
#define HL_CS_FLAGS_SKIP_RESET_ON_TIMEOUT	0x400

#define HL_CS_STATUS_SUCCESS		0

#define HL_MAX_JOBS_PER_CS		512

struct hl_cs_in {

	/* this holds address of array of hl_cs_chunk for restore phase */
	__u64 chunks_restore;

	/* holds address of array of hl_cs_chunk for execution phase */
	__u64 chunks_execute;

	/* Sequence number of a staged submission CS
	 * valid only if HL_CS_FLAGS_STAGED_SUBMISSION is set
	 */
	__u64 seq;

	/* Number of chunks in restore phase array. Maximum number is
	 * HL_MAX_JOBS_PER_CS
	 */
	__u32 num_chunks_restore;

	/* Number of chunks in execution array. Maximum number is
	 * HL_MAX_JOBS_PER_CS
	 */
	__u32 num_chunks_execute;

	/* timeout in seconds - valid only if HL_CS_FLAGS_CUSTOM_TIMEOUT
	 * is set
	 */
	__u32 timeout;

	/* HL_CS_FLAGS_* */
	__u32 cs_flags;

	/* Context ID - Currently not in use */
	__u32 ctx_id;
};

struct hl_cs_out {
	/*
	 * seq holds the sequence number of the CS to pass to wait ioctl. All
	 * values are valid except for 0 and ULLONG_MAX
	 */
	__u64 seq;
	/* HL_CS_STATUS_* */
	__u32 status;
	__u32 pad;
};

union hl_cs_args {
	struct hl_cs_in in;
	struct hl_cs_out out;
};

#define HL_WAIT_CS_FLAGS_INTERRUPT	0x2
#define HL_WAIT_CS_FLAGS_INTERRUPT_MASK 0xFFF00000

struct hl_wait_cs_in {
	union {
		struct {
			/* Command submission sequence number */
			__u64 seq;
			/* Absolute timeout to wait for command submission
			 * in microseconds
			 */
			__u64 timeout_us;
		};

		struct {
			/* User address for completion comparison.
			 * upon interrupt, driver will compare the value pointed
			 * by this address with the supplied target value.
			 * in order not to perform any comparison, set address
			 * to all 1s.
			 * Relevant only when HL_WAIT_CS_FLAGS_INTERRUPT is set
			 */
			__u64 addr;
			/* Target value for completion comparison */
			__u32 target;
			/* Absolute timeout to wait for interrupt
			 * in microseconds
			 */
			__u32 interrupt_timeout_us;
		};
	};

	/* Context ID - Currently not in use */
	__u32 ctx_id;
	/* HL_WAIT_CS_FLAGS_*
	 * If HL_WAIT_CS_FLAGS_INTERRUPT is set, this field should include
	 * interrupt id according to HL_WAIT_CS_FLAGS_INTERRUPT_MASK, in order
	 * not to specify an interrupt id ,set mask to all 1s.
	 */
	__u32 flags;
};

#define HL_WAIT_CS_STATUS_COMPLETED	0
#define HL_WAIT_CS_STATUS_BUSY		1
#define HL_WAIT_CS_STATUS_TIMEDOUT	2
#define HL_WAIT_CS_STATUS_ABORTED	3
#define HL_WAIT_CS_STATUS_INTERRUPTED	4

#define HL_WAIT_CS_STATUS_FLAG_GONE		0x1
#define HL_WAIT_CS_STATUS_FLAG_TIMESTAMP_VLD	0x2

struct hl_wait_cs_out {
	/* HL_WAIT_CS_STATUS_* */
	__u32 status;
	/* HL_WAIT_CS_STATUS_FLAG* */
	__u32 flags;
	/* valid only if HL_WAIT_CS_STATUS_FLAG_TIMESTAMP_VLD is set */
	__s64 timestamp_nsec;
};

union hl_wait_cs_args {
	struct hl_wait_cs_in in;
	struct hl_wait_cs_out out;
};

/* Opcode to allocate device memory */
#define HL_MEM_OP_ALLOC			0
/* Opcode to free previously allocated device memory */
#define HL_MEM_OP_FREE			1
/* Opcode to map host and device memory */
#define HL_MEM_OP_MAP			2
/* Opcode to unmap previously mapped host and device memory */
#define HL_MEM_OP_UNMAP			3
/* Opcode to map a hw block */
#define HL_MEM_OP_MAP_BLOCK		4

/* Memory flags */
#define HL_MEM_CONTIGUOUS	0x1
#define HL_MEM_SHARED		0x2
#define HL_MEM_USERPTR		0x4

struct hl_mem_in {
	union {
		/* HL_MEM_OP_ALLOC- allocate device memory */
		struct {
			/* Size to alloc */
			__u64 mem_size;
		} alloc;

		/* HL_MEM_OP_FREE - free device memory */
		struct {
			/* Handle returned from HL_MEM_OP_ALLOC */
			__u64 handle;
		} free;

		/* HL_MEM_OP_MAP - map device memory */
		struct {
			/*
			 * Requested virtual address of mapped memory.
			 * The driver will try to map the requested region to
			 * this hint address, as long as the address is valid
			 * and not already mapped. The user should check the
			 * returned address of the IOCTL to make sure he got
			 * the hint address. Passing 0 here means that the
			 * driver will choose the address itself.
			 */
			__u64 hint_addr;
			/* Handle returned from HL_MEM_OP_ALLOC */
			__u64 handle;
		} map_device;

		/* HL_MEM_OP_MAP - map host memory */
		struct {
			/* Address of allocated host memory */
			__u64 host_virt_addr;
			/*
			 * Requested virtual address of mapped memory.
			 * The driver will try to map the requested region to
			 * this hint address, as long as the address is valid
			 * and not already mapped. The user should check the
			 * returned address of the IOCTL to make sure he got
			 * the hint address. Passing 0 here means that the
			 * driver will choose the address itself.
			 */
			__u64 hint_addr;
			/* Size of allocated host memory */
			__u64 mem_size;
		} map_host;

		/* HL_MEM_OP_MAP_BLOCK - map a hw block */
		struct {
			/*
			 * HW block address to map, a handle and size will be
			 * returned to the user and will be used to mmap the
			 * relevant block. Only addresses from configuration
			 * space are allowed.
			 */
			__u64 block_addr;
		} map_block;

		/* HL_MEM_OP_UNMAP - unmap host memory */
		struct {
			/* Virtual address returned from HL_MEM_OP_MAP */
			__u64 device_virt_addr;
		} unmap;
	};

	/* HL_MEM_OP_* */
	__u32 op;
	/* HL_MEM_* flags */
	__u32 flags;
	/* Context ID - Currently not in use */
	__u32 ctx_id;
	__u32 pad;
};

struct hl_mem_out {
	union {
		/*
		 * Used for HL_MEM_OP_MAP as the virtual address that was
		 * assigned in the device VA space.
		 * A value of 0 means the requested operation failed.
		 */
		__u64 device_virt_addr;

		/*
		 * Used in HL_MEM_OP_ALLOC
		 * This is the assigned handle for the allocated memory
		 */
		__u64 handle;

		struct {
			/*
			 * Used in HL_MEM_OP_MAP_BLOCK.
			 * This is the assigned handle for the mapped block
			 */
			__u64 block_handle;

			/*
			 * Used in HL_MEM_OP_MAP_BLOCK
			 * This is the size of the mapped block
			 */
			__u32 block_size;

			__u32 pad;
		};
	};
};

union hl_mem_args {
	struct hl_mem_in in;
	struct hl_mem_out out;
};

#define HL_DEBUG_MAX_AUX_VALUES		10

struct hl_debug_params_etr {
	/* Address in memory to allocate buffer */
	__u64 buffer_address;

	/* Size of buffer to allocate */
	__u64 buffer_size;

	/* Sink operation mode: SW fifo, HW fifo, Circular buffer */
	__u32 sink_mode;
	__u32 pad;
};

struct hl_debug_params_etf {
	/* Address in memory to allocate buffer */
	__u64 buffer_address;

	/* Size of buffer to allocate */
	__u64 buffer_size;

	/* Sink operation mode: SW fifo, HW fifo, Circular buffer */
	__u32 sink_mode;
	__u32 pad;
};

struct hl_debug_params_stm {
	/* Two bit masks for HW event and Stimulus Port */
	__u64 he_mask;
	__u64 sp_mask;

	/* Trace source ID */
	__u32 id;

	/* Frequency for the timestamp register */
	__u32 frequency;
};

struct hl_debug_params_bmon {
	/* Two address ranges that the user can request to filter */
	__u64 start_addr0;
	__u64 addr_mask0;

	__u64 start_addr1;
	__u64 addr_mask1;

	/* Capture window configuration */
	__u32 bw_win;
	__u32 win_capture;

	/* Trace source ID */
	__u32 id;
	__u32 pad;
};

struct hl_debug_params_spmu {
	/* Event types selection */
	__u64 event_types[HL_DEBUG_MAX_AUX_VALUES];

	/* Number of event types selection */
	__u32 event_types_num;
	__u32 pad;
};

/* Opcode for ETR component */
#define HL_DEBUG_OP_ETR		0
/* Opcode for ETF component */
#define HL_DEBUG_OP_ETF		1
/* Opcode for STM component */
#define HL_DEBUG_OP_STM		2
/* Opcode for FUNNEL component */
#define HL_DEBUG_OP_FUNNEL	3
/* Opcode for BMON component */
#define HL_DEBUG_OP_BMON	4
/* Opcode for SPMU component */
#define HL_DEBUG_OP_SPMU	5
/* Opcode for timestamp (deprecated) */
#define HL_DEBUG_OP_TIMESTAMP	6
/* Opcode for setting the device into or out of debug mode. The enable
 * variable should be 1 for enabling debug mode and 0 for disabling it
 */
#define HL_DEBUG_OP_SET_MODE	7

struct hl_debug_args {
	/*
	 * Pointer to user input structure.
	 * This field is relevant to specific opcodes.
	 */
	__u64 input_ptr;
	/* Pointer to user output structure */
	__u64 output_ptr;
	/* Size of user input structure */
	__u32 input_size;
	/* Size of user output structure */
	__u32 output_size;
	/* HL_DEBUG_OP_* */
	__u32 op;
	/*
	 * Register index in the component, taken from the debug_regs_index enum
	 * in the various ASIC header files
	 */
	__u32 reg_idx;
	/* Enable/disable */
	__u32 enable;
	/* Context ID - Currently not in use */
	__u32 ctx_id;
};

/*
 * Various information operations such as:
 * - H/W IP information
 * - Current dram usage
 *
 * The user calls this IOCTL with an opcode that describes the required
 * information. The user should supply a pointer to a user-allocated memory
 * chunk, which will be filled by the driver with the requested information.
 *
 * The user supplies the maximum amount of size to copy into the user's memory,
 * in order to prevent data corruption in case of differences between the
 * definitions of structures in kernel and userspace, e.g. in case of old
 * userspace and new kernel driver
 */
#define HL_IOCTL_INFO	\
		_IOWR('H', 0x01, struct hl_info_args)

/*
 * Command Buffer
 * - Request a Command Buffer
 * - Destroy a Command Buffer
 *
 * The command buffers are memory blocks that reside in DMA-able address
 * space and are physically contiguous so they can be accessed by the device
 * directly. They are allocated using the coherent DMA API.
 *
 * When creating a new CB, the IOCTL returns a handle of it, and the user-space
 * process needs to use that handle to mmap the buffer so it can access them.
 *
 * In some instances, the device must access the command buffer through the
 * device's MMU, and thus its memory should be mapped. In these cases, user can
 * indicate the driver that such a mapping is required.
 * The resulting device virtual address will be used internally by the driver,
 * and won't be returned to user.
 *
 */
#define HL_IOCTL_CB		\
		_IOWR('H', 0x02, union hl_cb_args)

/*
 * Command Submission
 *
 * To submit work to the device, the user need to call this IOCTL with a set
 * of JOBS. That set of JOBS constitutes a CS object.
 * Each JOB will be enqueued on a specific queue, according to the user's input.
 * There can be more then one JOB per queue.
 *
 * The CS IOCTL will receive two sets of JOBS. One set is for "restore" phase
 * and a second set is for "execution" phase.
 * The JOBS on the "restore" phase are enqueued only after context-switch
 * (or if its the first CS for this context). The user can also order the
 * driver to run the "restore" phase explicitly
 *
 * There are two types of queues - external and internal. External queues
 * are DMA queues which transfer data from/to the Host. All other queues are
 * internal. The driver will get completion notifications from the device only
 * on JOBS which are enqueued in the external queues.
 *
 * For jobs on external queues, the user needs to create command buffers
 * through the CB ioctl and give the CB's handle to the CS ioctl. For jobs on
 * internal queues, the user needs to prepare a "command buffer" with packets
 * on either the device SRAM/DRAM or the host, and give the device address of
 * that buffer to the CS ioctl.
 *
 * This IOCTL is asynchronous in regard to the actual execution of the CS. This
 * means it returns immediately after ALL the JOBS were enqueued on their
 * relevant queues. Therefore, the user mustn't assume the CS has been completed
 * or has even started to execute.
 *
 * Upon successful enqueue, the IOCTL returns a sequence number which the user
 * can use with the "Wait for CS" IOCTL to check whether the handle's CS
 * external JOBS have been completed. Note that if the CS has internal JOBS
 * which can execute AFTER the external JOBS have finished, the driver might
 * report that the CS has finished executing BEFORE the internal JOBS have
 * actually finished executing.
 *
 * Even though the sequence number increments per CS, the user can NOT
 * automatically assume that if CS with sequence number N finished, then CS
 * with sequence number N-1 also finished. The user can make this assumption if
 * and only if CS N and CS N-1 are exactly the same (same CBs for the same
 * queues).
 */
#define HL_IOCTL_CS			\
		_IOWR('H', 0x03, union hl_cs_args)

/*
 * Wait for Command Submission
 *
 * The user can call this IOCTL with a handle it received from the CS IOCTL
 * to wait until the handle's CS has finished executing. The user will wait
 * inside the kernel until the CS has finished or until the user-requested
 * timeout has expired.
 *
 * If the timeout value is 0, the driver won't sleep at all. It will check
 * the status of the CS and return immediately
 *
 * The return value of the IOCTL is a standard Linux error code. The possible
 * values are:
 *
 * EINTR     - Kernel waiting has been interrupted, e.g. due to OS signal
 *             that the user process received
 * ETIMEDOUT - The CS has caused a timeout on the device
 * EIO       - The CS was aborted (usually because the device was reset)
 * ENODEV    - The device wants to do hard-reset (so user need to close FD)
 *
 * The driver also returns a custom define inside the IOCTL which can be:
 *
 * HL_WAIT_CS_STATUS_COMPLETED   - The CS has been completed successfully (0)
 * HL_WAIT_CS_STATUS_BUSY        - The CS is still executing (0)
 * HL_WAIT_CS_STATUS_TIMEDOUT    - The CS has caused a timeout on the device
 *                                 (ETIMEDOUT)
 * HL_WAIT_CS_STATUS_ABORTED     - The CS was aborted, usually because the
 *                                 device was reset (EIO)
 * HL_WAIT_CS_STATUS_INTERRUPTED - Waiting for the CS was interrupted (EINTR)
 *
 */

#define HL_IOCTL_WAIT_CS			\
		_IOWR('H', 0x04, union hl_wait_cs_args)

/*
 * Memory
 * - Map host memory to device MMU
 * - Unmap host memory from device MMU
 *
 * This IOCTL allows the user to map host memory to the device MMU
 *
 * For host memory, the IOCTL doesn't allocate memory. The user is supposed
 * to allocate the memory in user-space (malloc/new). The driver pins the
 * physical pages (up to the allowed limit by the OS), assigns a virtual
 * address in the device VA space and initializes the device MMU.
 *
 * There is an option for the user to specify the requested virtual address.
 *
 */
#define HL_IOCTL_MEMORY		\
		_IOWR('H', 0x05, union hl_mem_args)

/*
 * Debug
 * - Enable/disable the ETR/ETF/FUNNEL/STM/BMON/SPMU debug traces
 *
 * This IOCTL allows the user to get debug traces from the chip.
 *
 * Before the user can send configuration requests of the various
 * debug/profile engines, it needs to set the device into debug mode.
 * This is because the debug/profile infrastructure is shared component in the
 * device and we can't allow multiple users to access it at the same time.
 *
 * Once a user set the device into debug mode, the driver won't allow other
 * users to "work" with the device, i.e. open a FD. If there are multiple users
 * opened on the device, the driver won't allow any user to debug the device.
 *
 * For each configuration request, the user needs to provide the register index
 * and essential data such as buffer address and size.
 *
 * Once the user has finished using the debug/profile engines, he should
 * set the device into non-debug mode, i.e. disable debug mode.
 *
 * The driver can decide to "kick out" the user if he abuses this interface.
 *
 */
#define HL_IOCTL_DEBUG		\
		_IOWR('H', 0x06, struct hl_debug_args)

#define HL_COMMAND_START	0x01
#define HL_COMMAND_END		0x07

#endif /* HABANALABS_H_ */
