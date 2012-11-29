/******************************************************************************
 *
 * Name: actbl3.h - ACPI Table Definitions
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2012, Intel Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

#ifndef __ACTBL3_H__
#define __ACTBL3_H__

/*******************************************************************************
 *
 * Additional ACPI Tables (3)
 *
 * These tables are not consumed directly by the ACPICA subsystem, but are
 * included here to support device drivers and the AML disassembler.
 *
 * The tables in this file are fully defined within the ACPI specification.
 *
 ******************************************************************************/

/*
 * Values for description table header signatures for tables defined in this
 * file. Useful because they make it more difficult to inadvertently type in
 * the wrong signature.
 */
#define ACPI_SIG_BGRT           "BGRT"	/* Boot Graphics Resource Table */
#define ACPI_SIG_DRTM           "DRTM"	/* Dynamic Root of Trust for Measurement table */
#define ACPI_SIG_FPDT           "FPDT"	/* Firmware Performance Data Table */
#define ACPI_SIG_GTDT           "GTDT"	/* Generic Timer Description Table */
#define ACPI_SIG_MPST           "MPST"	/* Memory Power State Table */
#define ACPI_SIG_PCCT           "PCCT"	/* Platform Communications Channel Table */
#define ACPI_SIG_PMTT           "PMTT"	/* Platform Memory Topology Table */
#define ACPI_SIG_RASF           "RASF"	/* RAS Feature table */

#define ACPI_SIG_S3PT           "S3PT"	/* S3 Performance (sub)Table */
#define ACPI_SIG_PCCS           "PCC"	/* PCC Shared Memory Region */

/* Reserved table signatures */

#define ACPI_SIG_CSRT           "CSRT"	/* Core System Resources Table */
#define ACPI_SIG_MATR           "MATR"	/* Memory Address Translation Table */
#define ACPI_SIG_MSDM           "MSDM"	/* Microsoft Data Management Table */
#define ACPI_SIG_WPBT           "WPBT"	/* Windows Platform Binary Table */

/*
 * All tables must be byte-packed to match the ACPI specification, since
 * the tables are provided by the system BIOS.
 */
#pragma pack(1)

/*
 * Note: C bitfields are not used for this reason:
 *
 * "Bitfields are great and easy to read, but unfortunately the C language
 * does not specify the layout of bitfields in memory, which means they are
 * essentially useless for dealing with packed data in on-disk formats or
 * binary wire protocols." (Or ACPI tables and buffers.) "If you ask me,
 * this decision was a design error in C. Ritchie could have picked an order
 * and stuck with it." Norman Ramsey.
 * See http://stackoverflow.com/a/1053662/41661
 */

/*******************************************************************************
 *
 * BGRT - Boot Graphics Resource Table (ACPI 5.0)
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_bgrt {
	struct acpi_table_header header;	/* Common ACPI table header */
	u16 version;
	u8 status;
	u8 image_type;
	u64 image_address;
	u32 image_offset_x;
	u32 image_offset_y;
};

/*******************************************************************************
 *
 * DRTM - Dynamic Root of Trust for Measurement table
 *
 ******************************************************************************/

struct acpi_table_drtm {
	struct acpi_table_header header;	/* Common ACPI table header */
	u64 entry_base_address;
	u64 entry_length;
	u32 entry_address32;
	u64 entry_address64;
	u64 exit_address;
	u64 log_area_address;
	u32 log_area_length;
	u64 arch_dependent_address;
	u32 flags;
};

/* 1) Validated Tables List */

struct acpi_drtm_vtl_list {
	u32 validated_table_list_count;
};

/* 2) Resources List */

struct acpi_drtm_resource_list {
	u32 resource_list_count;
};

/* 3) Platform-specific Identifiers List */

struct acpi_drtm_id_list {
	u32 id_list_count;
};

/*******************************************************************************
 *
 * FPDT - Firmware Performance Data Table (ACPI 5.0)
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_fpdt {
	struct acpi_table_header header;	/* Common ACPI table header */
};

/* FPDT subtable header */

struct acpi_fpdt_header {
	u16 type;
	u8 length;
	u8 revision;
};

/* Values for Type field above */

enum acpi_fpdt_type {
	ACPI_FPDT_TYPE_BOOT = 0,
	ACPI_FPDT_TYPE_S3PERF = 1,
};

/*
 * FPDT subtables
 */

/* 0: Firmware Basic Boot Performance Record */

struct acpi_fpdt_boot {
	struct acpi_fpdt_header header;
	u8 reserved[4];
	u64 reset_end;
	u64 load_start;
	u64 startup_start;
	u64 exit_services_entry;
	u64 exit_services_exit;
};

/* 1: S3 Performance Table Pointer Record */

struct acpi_fpdt_s3pt_ptr {
	struct acpi_fpdt_header header;
	u8 reserved[4];
	u64 address;
};

/*
 * S3PT - S3 Performance Table. This table is pointed to by the
 * FPDT S3 Pointer Record above.
 */
struct acpi_table_s3pt {
	u8 signature[4];	/* "S3PT" */
	u32 length;
};

/*
 * S3PT Subtables
 */
struct acpi_s3pt_header {
	u16 type;
	u8 length;
	u8 revision;
};

/* Values for Type field above */

enum acpi_s3pt_type {
	ACPI_S3PT_TYPE_RESUME = 0,
	ACPI_S3PT_TYPE_SUSPEND = 1,
};

struct acpi_s3pt_resume {
	struct acpi_s3pt_header header;
	u32 resume_count;
	u64 full_resume;
	u64 average_resume;
};

struct acpi_s3pt_suspend {
	struct acpi_s3pt_header header;
	u64 suspend_start;
	u64 suspend_end;
};

/*******************************************************************************
 *
 * GTDT - Generic Timer Description Table (ACPI 5.0)
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_gtdt {
	struct acpi_table_header header;	/* Common ACPI table header */
	u64 address;
	u32 flags;
	u32 secure_pl1_interrupt;
	u32 secure_pl1_flags;
	u32 non_secure_pl1_interrupt;
	u32 non_secure_pl1_flags;
	u32 virtual_timer_interrupt;
	u32 virtual_timer_flags;
	u32 non_secure_pl2_interrupt;
	u32 non_secure_pl2_flags;
};

/* Values for Flags field above */

#define ACPI_GTDT_MAPPED_BLOCK_PRESENT      1

/* Values for all "TimerFlags" fields above */

#define ACPI_GTDT_INTERRUPT_MODE            1
#define ACPI_GTDT_INTERRUPT_POLARITY        2

/*******************************************************************************
 *
 * MPST - Memory Power State Table (ACPI 5.0)
 *        Version 1
 *
 ******************************************************************************/

#define ACPI_MPST_CHANNEL_INFO \
	u8                              channel_id; \
	u8                              reserved1[3]; \
	u16                             power_node_count; \
	u16                             reserved2;

/* Main table */

struct acpi_table_mpst {
	struct acpi_table_header header;	/* Common ACPI table header */
	 ACPI_MPST_CHANNEL_INFO	/* Platform Communication Channel */
};

/* Memory Platform Communication Channel Info */

struct acpi_mpst_channel {
	ACPI_MPST_CHANNEL_INFO	/* Platform Communication Channel */
};

/* Memory Power Node Structure */

struct acpi_mpst_power_node {
	u8 flags;
	u8 reserved1;
	u16 node_id;
	u32 length;
	u64 range_address;
	u64 range_length;
	u32 num_power_states;
	u32 num_physical_components;
};

/* Values for Flags field above */

#define ACPI_MPST_ENABLED               1
#define ACPI_MPST_POWER_MANAGED         2
#define ACPI_MPST_HOT_PLUG_CAPABLE      4

/* Memory Power State Structure (follows POWER_NODE above) */

struct acpi_mpst_power_state {
	u8 power_state;
	u8 info_index;
};

/* Physical Component ID Structure (follows POWER_STATE above) */

struct acpi_mpst_component {
	u16 component_id;
};

/* Memory Power State Characteristics Structure (follows all POWER_NODEs) */

struct acpi_mpst_data_hdr {
	u16 characteristics_count;
	u16 reserved;
};

struct acpi_mpst_power_data {
	u8 structure_id;
	u8 flags;
	u16 reserved1;
	u32 average_power;
	u32 power_saving;
	u64 exit_latency;
	u64 reserved2;
};

/* Values for Flags field above */

#define ACPI_MPST_PRESERVE              1
#define ACPI_MPST_AUTOENTRY             2
#define ACPI_MPST_AUTOEXIT              4

/* Shared Memory Region (not part of an ACPI table) */

struct acpi_mpst_shared {
	u32 signature;
	u16 pcc_command;
	u16 pcc_status;
	u32 command_register;
	u32 status_register;
	u32 power_state_id;
	u32 power_node_id;
	u64 energy_consumed;
	u64 average_power;
};

/*******************************************************************************
 *
 * PCCT - Platform Communications Channel Table (ACPI 5.0)
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_pcct {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 flags;
	u32 latency;
	u32 reserved;
};

/* Values for Flags field above */

#define ACPI_PCCT_DOORBELL              1

/*
 * PCCT subtables
 */

/* 0: Generic Communications Subspace */

struct acpi_pcct_subspace {
	struct acpi_subtable_header header;
	u8 reserved[6];
	u64 base_address;
	u64 length;
	struct acpi_generic_address doorbell_register;
	u64 preserve_mask;
	u64 write_mask;
};

/*
 * PCC memory structures (not part of the ACPI table)
 */

/* Shared Memory Region */

struct acpi_pcct_shared_memory {
	u32 signature;
	u16 command;
	u16 status;
};

/*******************************************************************************
 *
 * PMTT - Platform Memory Topology Table (ACPI 5.0)
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_pmtt {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 reserved;
};

/* Common header for PMTT subtables that follow main table */

struct acpi_pmtt_header {
	u8 type;
	u8 reserved1;
	u16 length;
	u16 flags;
	u16 reserved2;
};

/* Values for Type field above */

#define ACPI_PMTT_TYPE_SOCKET           0
#define ACPI_PMTT_TYPE_CONTROLLER       1
#define ACPI_PMTT_TYPE_DIMM             2
#define ACPI_PMTT_TYPE_RESERVED         3	/* 0x03-0xFF are reserved */

/* Values for Flags field above */

#define ACPI_PMTT_TOP_LEVEL             0x0001
#define ACPI_PMTT_PHYSICAL              0x0002
#define ACPI_PMTT_MEMORY_TYPE           0x000C

/*
 * PMTT subtables, correspond to Type in struct acpi_pmtt_header
 */

/* 0: Socket Structure */

struct acpi_pmtt_socket {
	struct acpi_pmtt_header header;
	u16 socket_id;
	u16 reserved;
};

/* 1: Memory Controller subtable */

struct acpi_pmtt_controller {
	struct acpi_pmtt_header header;
	u32 read_latency;
	u32 write_latency;
	u32 read_bandwidth;
	u32 write_bandwidth;
	u16 access_width;
	u16 alignment;
	u16 reserved;
	u16 domain_count;
};

/* 1a: Proximity Domain substructure */

struct acpi_pmtt_domain {
	u32 proximity_domain;
};

/* 2: Physical Component Identifier (DIMM) */

struct acpi_pmtt_physical_component {
	struct acpi_pmtt_header header;
	u16 component_id;
	u16 reserved;
	u32 memory_size;
	u32 bios_handle;
};

/*******************************************************************************
 *
 * RASF - RAS Feature Table (ACPI 5.0)
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_rasf {
	struct acpi_table_header header;	/* Common ACPI table header */
	u8 channel_id[12];
};

/* RASF Platform Communication Channel Shared Memory Region */

struct acpi_rasf_shared_memory {
	u32 signature;
	u16 command;
	u16 status;
	u64 requested_address;
	u64 requested_length;
	u64 actual_address;
	u64 actual_length;
	u16 flags;
	u8 speed;
};

/* Masks for Flags and Speed fields above */

#define ACPI_RASF_SCRUBBER_RUNNING      1
#define ACPI_RASF_SPEED                 (7<<1)

/* Channel Commands */

enum acpi_rasf_commands {
	ACPI_RASF_GET_RAS_CAPABILITIES = 1,
	ACPI_RASF_GET_PATROL_PARAMETERS = 2,
	ACPI_RASF_START_PATROL_SCRUBBER = 3,
	ACPI_RASF_STOP_PATROL_SCRUBBER = 4
};

/* Channel Command flags */

#define ACPI_RASF_GENERATE_SCI          (1<<15)

/* Status values */

enum acpi_rasf_status {
	ACPI_RASF_SUCCESS = 0,
	ACPI_RASF_NOT_VALID = 1,
	ACPI_RASF_NOT_SUPPORTED = 2,
	ACPI_RASF_BUSY = 3,
	ACPI_RASF_FAILED = 4,
	ACPI_RASF_ABORTED = 5,
	ACPI_RASF_INVALID_DATA = 6
};

/* Status flags */

#define ACPI_RASF_COMMAND_COMPLETE      (1)
#define ACPI_RASF_SCI_DOORBELL          (1<<1)
#define ACPI_RASF_ERROR                 (1<<2)
#define ACPI_RASF_STATUS                (0x1F<<3)

/* Reset to default packing */

#pragma pack()

#endif				/* __ACTBL3_H__ */
