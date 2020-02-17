/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/******************************************************************************
 *
 * Name: actbl1.h - Additional ACPI table definitions
 *
 * Copyright (C) 2000 - 2020, Intel Corp.
 *
 *****************************************************************************/

#ifndef __ACTBL1_H__
#define __ACTBL1_H__

/*******************************************************************************
 *
 * Additional ACPI Tables
 *
 * These tables are not consumed directly by the ACPICA subsystem, but are
 * included here to support device drivers and the AML disassembler.
 *
 ******************************************************************************/

/*
 * Values for description table header signatures for tables defined in this
 * file. Useful because they make it more difficult to inadvertently type in
 * the wrong signature.
 */
#define ACPI_SIG_ASF            "ASF!"	/* Alert Standard Format table */
#define ACPI_SIG_BERT           "BERT"	/* Boot Error Record Table */
#define ACPI_SIG_BGRT           "BGRT"	/* Boot Graphics Resource Table */
#define ACPI_SIG_BOOT           "BOOT"	/* Simple Boot Flag Table */
#define ACPI_SIG_CPEP           "CPEP"	/* Corrected Platform Error Polling table */
#define ACPI_SIG_CSRT           "CSRT"	/* Core System Resource Table */
#define ACPI_SIG_DBG2           "DBG2"	/* Debug Port table type 2 */
#define ACPI_SIG_DBGP           "DBGP"	/* Debug Port table */
#define ACPI_SIG_DMAR           "DMAR"	/* DMA Remapping table */
#define ACPI_SIG_DRTM           "DRTM"	/* Dynamic Root of Trust for Measurement table */
#define ACPI_SIG_ECDT           "ECDT"	/* Embedded Controller Boot Resources Table */
#define ACPI_SIG_EINJ           "EINJ"	/* Error Injection table */
#define ACPI_SIG_ERST           "ERST"	/* Error Record Serialization Table */
#define ACPI_SIG_FPDT           "FPDT"	/* Firmware Performance Data Table */
#define ACPI_SIG_GTDT           "GTDT"	/* Generic Timer Description Table */
#define ACPI_SIG_HEST           "HEST"	/* Hardware Error Source Table */
#define ACPI_SIG_HMAT           "HMAT"	/* Heterogeneous Memory Attributes Table */
#define ACPI_SIG_HPET           "HPET"	/* High Precision Event Timer table */
#define ACPI_SIG_IBFT           "IBFT"	/* iSCSI Boot Firmware Table */

#define ACPI_SIG_S3PT           "S3PT"	/* S3 Performance (sub)Table */
#define ACPI_SIG_PCCS           "PCC"	/* PCC Shared Memory Region */

/* Reserved table signatures */

#define ACPI_SIG_MATR           "MATR"	/* Memory Address Translation Table */
#define ACPI_SIG_MSDM           "MSDM"	/* Microsoft Data Management Table */

/*
 * These tables have been seen in the field, but no definition has been found
 */
#ifdef ACPI_UNDEFINED_TABLES
#define ACPI_SIG_ATKG           "ATKG"
#define ACPI_SIG_GSCI           "GSCI"	/* GMCH SCI table */
#define ACPI_SIG_IEIT           "IEIT"
#endif

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
 * Common subtable headers
 *
 ******************************************************************************/

/* Generic subtable header (used in MADT, SRAT, etc.) */

struct acpi_subtable_header {
	u8 type;
	u8 length;
};

/* Subtable header for WHEA tables (EINJ, ERST, WDAT) */

struct acpi_whea_header {
	u8 action;
	u8 instruction;
	u8 flags;
	u8 reserved;
	struct acpi_generic_address register_region;
	u64 value;		/* Value used with Read/Write register */
	u64 mask;		/* Bitmask required for this register instruction */
};

/*******************************************************************************
 *
 * ASF - Alert Standard Format table (Signature "ASF!")
 *       Revision 0x10
 *
 * Conforms to the Alert Standard Format Specification V2.0, 23 April 2003
 *
 ******************************************************************************/

struct acpi_table_asf {
	struct acpi_table_header header;	/* Common ACPI table header */
};

/* ASF subtable header */

struct acpi_asf_header {
	u8 type;
	u8 reserved;
	u16 length;
};

/* Values for Type field above */

enum acpi_asf_type {
	ACPI_ASF_TYPE_INFO = 0,
	ACPI_ASF_TYPE_ALERT = 1,
	ACPI_ASF_TYPE_CONTROL = 2,
	ACPI_ASF_TYPE_BOOT = 3,
	ACPI_ASF_TYPE_ADDRESS = 4,
	ACPI_ASF_TYPE_RESERVED = 5
};

/*
 * ASF subtables
 */

/* 0: ASF Information */

struct acpi_asf_info {
	struct acpi_asf_header header;
	u8 min_reset_value;
	u8 min_poll_interval;
	u16 system_id;
	u32 mfg_id;
	u8 flags;
	u8 reserved2[3];
};

/* Masks for Flags field above */

#define ACPI_ASF_SMBUS_PROTOCOLS    (1)

/* 1: ASF Alerts */

struct acpi_asf_alert {
	struct acpi_asf_header header;
	u8 assert_mask;
	u8 deassert_mask;
	u8 alerts;
	u8 data_length;
};

struct acpi_asf_alert_data {
	u8 address;
	u8 command;
	u8 mask;
	u8 value;
	u8 sensor_type;
	u8 type;
	u8 offset;
	u8 source_type;
	u8 severity;
	u8 sensor_number;
	u8 entity;
	u8 instance;
};

/* 2: ASF Remote Control */

struct acpi_asf_remote {
	struct acpi_asf_header header;
	u8 controls;
	u8 data_length;
	u16 reserved2;
};

struct acpi_asf_control_data {
	u8 function;
	u8 address;
	u8 command;
	u8 value;
};

/* 3: ASF RMCP Boot Options */

struct acpi_asf_rmcp {
	struct acpi_asf_header header;
	u8 capabilities[7];
	u8 completion_code;
	u32 enterprise_id;
	u8 command;
	u16 parameter;
	u16 boot_options;
	u16 oem_parameters;
};

/* 4: ASF Address */

struct acpi_asf_address {
	struct acpi_asf_header header;
	u8 eprom_address;
	u8 devices;
};

/*******************************************************************************
 *
 * BERT - Boot Error Record Table (ACPI 4.0)
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_bert {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 region_length;	/* Length of the boot error region */
	u64 address;		/* Physical address of the error region */
};

/* Boot Error Region (not a subtable, pointed to by Address field above) */

struct acpi_bert_region {
	u32 block_status;	/* Type of error information */
	u32 raw_data_offset;	/* Offset to raw error data */
	u32 raw_data_length;	/* Length of raw error data */
	u32 data_length;	/* Length of generic error data */
	u32 error_severity;	/* Severity code */
};

/* Values for block_status flags above */

#define ACPI_BERT_UNCORRECTABLE             (1)
#define ACPI_BERT_CORRECTABLE               (1<<1)
#define ACPI_BERT_MULTIPLE_UNCORRECTABLE    (1<<2)
#define ACPI_BERT_MULTIPLE_CORRECTABLE      (1<<3)
#define ACPI_BERT_ERROR_ENTRY_COUNT         (0xFF<<4)	/* 8 bits, error count */

/* Values for error_severity above */

enum acpi_bert_error_severity {
	ACPI_BERT_ERROR_CORRECTABLE = 0,
	ACPI_BERT_ERROR_FATAL = 1,
	ACPI_BERT_ERROR_CORRECTED = 2,
	ACPI_BERT_ERROR_NONE = 3,
	ACPI_BERT_ERROR_RESERVED = 4	/* 4 and greater are reserved */
};

/*
 * Note: The generic error data that follows the error_severity field above
 * uses the struct acpi_hest_generic_data defined under the HEST table below
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

/* Flags for Status field above */

#define ACPI_BGRT_DISPLAYED                 (1)
#define ACPI_BGRT_ORIENTATION_OFFSET        (3 << 1)

/*******************************************************************************
 *
 * BOOT - Simple Boot Flag Table
 *        Version 1
 *
 * Conforms to the "Simple Boot Flag Specification", Version 2.1
 *
 ******************************************************************************/

struct acpi_table_boot {
	struct acpi_table_header header;	/* Common ACPI table header */
	u8 cmos_index;		/* Index in CMOS RAM for the boot register */
	u8 reserved[3];
};

/*******************************************************************************
 *
 * CPEP - Corrected Platform Error Polling table (ACPI 4.0)
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_cpep {
	struct acpi_table_header header;	/* Common ACPI table header */
	u64 reserved;
};

/* Subtable */

struct acpi_cpep_polling {
	struct acpi_subtable_header header;
	u8 id;			/* Processor ID */
	u8 eid;			/* Processor EID */
	u32 interval;		/* Polling interval (msec) */
};

/*******************************************************************************
 *
 * CSRT - Core System Resource Table
 *        Version 0
 *
 * Conforms to the "Core System Resource Table (CSRT)", November 14, 2011
 *
 ******************************************************************************/

struct acpi_table_csrt {
	struct acpi_table_header header;	/* Common ACPI table header */
};

/* Resource Group subtable */

struct acpi_csrt_group {
	u32 length;
	u32 vendor_id;
	u32 subvendor_id;
	u16 device_id;
	u16 subdevice_id;
	u16 revision;
	u16 reserved;
	u32 shared_info_length;

	/* Shared data immediately follows (Length = shared_info_length) */
};

/* Shared Info subtable */

struct acpi_csrt_shared_info {
	u16 major_version;
	u16 minor_version;
	u32 mmio_base_low;
	u32 mmio_base_high;
	u32 gsi_interrupt;
	u8 interrupt_polarity;
	u8 interrupt_mode;
	u8 num_channels;
	u8 dma_address_width;
	u16 base_request_line;
	u16 num_handshake_signals;
	u32 max_block_size;

	/* Resource descriptors immediately follow (Length = Group length - shared_info_length) */
};

/* Resource Descriptor subtable */

struct acpi_csrt_descriptor {
	u32 length;
	u16 type;
	u16 subtype;
	u32 uid;

	/* Resource-specific information immediately follows */
};

/* Resource Types */

#define ACPI_CSRT_TYPE_INTERRUPT    0x0001
#define ACPI_CSRT_TYPE_TIMER        0x0002
#define ACPI_CSRT_TYPE_DMA          0x0003

/* Resource Subtypes */

#define ACPI_CSRT_XRUPT_LINE        0x0000
#define ACPI_CSRT_XRUPT_CONTROLLER  0x0001
#define ACPI_CSRT_TIMER             0x0000
#define ACPI_CSRT_DMA_CHANNEL       0x0000
#define ACPI_CSRT_DMA_CONTROLLER    0x0001

/*******************************************************************************
 *
 * DBG2 - Debug Port Table 2
 *        Version 0 (Both main table and subtables)
 *
 * Conforms to "Microsoft Debug Port Table 2 (DBG2)", December 10, 2015
 *
 ******************************************************************************/

struct acpi_table_dbg2 {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 info_offset;
	u32 info_count;
};

struct acpi_dbg2_header {
	u32 info_offset;
	u32 info_count;
};

/* Debug Device Information Subtable */

struct acpi_dbg2_device {
	u8 revision;
	u16 length;
	u8 register_count;	/* Number of base_address registers */
	u16 namepath_length;
	u16 namepath_offset;
	u16 oem_data_length;
	u16 oem_data_offset;
	u16 port_type;
	u16 port_subtype;
	u16 reserved;
	u16 base_address_offset;
	u16 address_size_offset;
	/*
	 * Data that follows:
	 *    base_address (required) - Each in 12-byte Generic Address Structure format.
	 *    address_size (required) - Array of u32 sizes corresponding to each base_address register.
	 *    Namepath    (required) - Null terminated string. Single dot if not supported.
	 *    oem_data    (optional) - Length is oem_data_length.
	 */
};

/* Types for port_type field above */

#define ACPI_DBG2_SERIAL_PORT       0x8000
#define ACPI_DBG2_1394_PORT         0x8001
#define ACPI_DBG2_USB_PORT          0x8002
#define ACPI_DBG2_NET_PORT          0x8003

/* Subtypes for port_subtype field above */

#define ACPI_DBG2_16550_COMPATIBLE  0x0000
#define ACPI_DBG2_16550_SUBSET      0x0001
#define ACPI_DBG2_ARM_PL011         0x0003
#define ACPI_DBG2_ARM_SBSA_32BIT    0x000D
#define ACPI_DBG2_ARM_SBSA_GENERIC  0x000E
#define ACPI_DBG2_ARM_DCC           0x000F
#define ACPI_DBG2_BCM2835           0x0010

#define ACPI_DBG2_1394_STANDARD     0x0000

#define ACPI_DBG2_USB_XHCI          0x0000
#define ACPI_DBG2_USB_EHCI          0x0001

/*******************************************************************************
 *
 * DBGP - Debug Port table
 *        Version 1
 *
 * Conforms to the "Debug Port Specification", Version 1.00, 2/9/2000
 *
 ******************************************************************************/

struct acpi_table_dbgp {
	struct acpi_table_header header;	/* Common ACPI table header */
	u8 type;		/* 0=full 16550, 1=subset of 16550 */
	u8 reserved[3];
	struct acpi_generic_address debug_port;
};

/*******************************************************************************
 *
 * DMAR - DMA Remapping table
 *        Version 1
 *
 * Conforms to "Intel Virtualization Technology for Directed I/O",
 * Version 2.3, October 2014
 *
 ******************************************************************************/

struct acpi_table_dmar {
	struct acpi_table_header header;	/* Common ACPI table header */
	u8 width;		/* Host Address Width */
	u8 flags;
	u8 reserved[10];
};

/* Masks for Flags field above */

#define ACPI_DMAR_INTR_REMAP        (1)
#define ACPI_DMAR_X2APIC_OPT_OUT    (1<<1)
#define ACPI_DMAR_X2APIC_MODE       (1<<2)

/* DMAR subtable header */

struct acpi_dmar_header {
	u16 type;
	u16 length;
};

/* Values for subtable type in struct acpi_dmar_header */

enum acpi_dmar_type {
	ACPI_DMAR_TYPE_HARDWARE_UNIT = 0,
	ACPI_DMAR_TYPE_RESERVED_MEMORY = 1,
	ACPI_DMAR_TYPE_ROOT_ATS = 2,
	ACPI_DMAR_TYPE_HARDWARE_AFFINITY = 3,
	ACPI_DMAR_TYPE_NAMESPACE = 4,
	ACPI_DMAR_TYPE_RESERVED = 5	/* 5 and greater are reserved */
};

/* DMAR Device Scope structure */

struct acpi_dmar_device_scope {
	u8 entry_type;
	u8 length;
	u16 reserved;
	u8 enumeration_id;
	u8 bus;
};

/* Values for entry_type in struct acpi_dmar_device_scope - device types */

enum acpi_dmar_scope_type {
	ACPI_DMAR_SCOPE_TYPE_NOT_USED = 0,
	ACPI_DMAR_SCOPE_TYPE_ENDPOINT = 1,
	ACPI_DMAR_SCOPE_TYPE_BRIDGE = 2,
	ACPI_DMAR_SCOPE_TYPE_IOAPIC = 3,
	ACPI_DMAR_SCOPE_TYPE_HPET = 4,
	ACPI_DMAR_SCOPE_TYPE_NAMESPACE = 5,
	ACPI_DMAR_SCOPE_TYPE_RESERVED = 6	/* 6 and greater are reserved */
};

struct acpi_dmar_pci_path {
	u8 device;
	u8 function;
};

/*
 * DMAR Subtables, correspond to Type in struct acpi_dmar_header
 */

/* 0: Hardware Unit Definition */

struct acpi_dmar_hardware_unit {
	struct acpi_dmar_header header;
	u8 flags;
	u8 reserved;
	u16 segment;
	u64 address;		/* Register Base Address */
};

/* Masks for Flags field above */

#define ACPI_DMAR_INCLUDE_ALL       (1)

/* 1: Reserved Memory Definition */

struct acpi_dmar_reserved_memory {
	struct acpi_dmar_header header;
	u16 reserved;
	u16 segment;
	u64 base_address;	/* 4K aligned base address */
	u64 end_address;	/* 4K aligned limit address */
};

/* Masks for Flags field above */

#define ACPI_DMAR_ALLOW_ALL         (1)

/* 2: Root Port ATS Capability Reporting Structure */

struct acpi_dmar_atsr {
	struct acpi_dmar_header header;
	u8 flags;
	u8 reserved;
	u16 segment;
};

/* Masks for Flags field above */

#define ACPI_DMAR_ALL_PORTS         (1)

/* 3: Remapping Hardware Static Affinity Structure */

struct acpi_dmar_rhsa {
	struct acpi_dmar_header header;
	u32 reserved;
	u64 base_address;
	u32 proximity_domain;
};

/* 4: ACPI Namespace Device Declaration Structure */

struct acpi_dmar_andd {
	struct acpi_dmar_header header;
	u8 reserved[3];
	u8 device_number;
	char device_name[1];
};

/*******************************************************************************
 *
 * DRTM - Dynamic Root of Trust for Measurement table
 * Conforms to "TCG D-RTM Architecture" June 17 2013, Version 1.0.0
 * Table version 1
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

/* Flag Definitions for above */

#define ACPI_DRTM_ACCESS_ALLOWED            (1)
#define ACPI_DRTM_ENABLE_GAP_CODE           (1<<1)
#define ACPI_DRTM_INCOMPLETE_MEASUREMENTS   (1<<2)
#define ACPI_DRTM_AUTHORITY_ORDER           (1<<3)

/* 1) Validated Tables List (64-bit addresses) */

struct acpi_drtm_vtable_list {
	u32 validated_table_count;
	u64 validated_tables[1];
};

/* 2) Resources List (of Resource Descriptors) */

/* Resource Descriptor */

struct acpi_drtm_resource {
	u8 size[7];
	u8 type;
	u64 address;
};

struct acpi_drtm_resource_list {
	u32 resource_count;
	struct acpi_drtm_resource resources[1];
};

/* 3) Platform-specific Identifiers List */

struct acpi_drtm_dps_id {
	u32 dps_id_length;
	u8 dps_id[16];
};

/*******************************************************************************
 *
 * ECDT - Embedded Controller Boot Resources Table
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_ecdt {
	struct acpi_table_header header;	/* Common ACPI table header */
	struct acpi_generic_address control;	/* Address of EC command/status register */
	struct acpi_generic_address data;	/* Address of EC data register */
	u32 uid;		/* Unique ID - must be same as the EC _UID method */
	u8 gpe;			/* The GPE for the EC */
	u8 id[1];		/* Full namepath of the EC in the ACPI namespace */
};

/*******************************************************************************
 *
 * EINJ - Error Injection Table (ACPI 4.0)
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_einj {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 header_length;
	u8 flags;
	u8 reserved[3];
	u32 entries;
};

/* EINJ Injection Instruction Entries (actions) */

struct acpi_einj_entry {
	struct acpi_whea_header whea_header;	/* Common header for WHEA tables */
};

/* Masks for Flags field above */

#define ACPI_EINJ_PRESERVE          (1)

/* Values for Action field above */

enum acpi_einj_actions {
	ACPI_EINJ_BEGIN_OPERATION = 0,
	ACPI_EINJ_GET_TRIGGER_TABLE = 1,
	ACPI_EINJ_SET_ERROR_TYPE = 2,
	ACPI_EINJ_GET_ERROR_TYPE = 3,
	ACPI_EINJ_END_OPERATION = 4,
	ACPI_EINJ_EXECUTE_OPERATION = 5,
	ACPI_EINJ_CHECK_BUSY_STATUS = 6,
	ACPI_EINJ_GET_COMMAND_STATUS = 7,
	ACPI_EINJ_SET_ERROR_TYPE_WITH_ADDRESS = 8,
	ACPI_EINJ_GET_EXECUTE_TIMINGS = 9,
	ACPI_EINJ_ACTION_RESERVED = 10,	/* 10 and greater are reserved */
	ACPI_EINJ_TRIGGER_ERROR = 0xFF	/* Except for this value */
};

/* Values for Instruction field above */

enum acpi_einj_instructions {
	ACPI_EINJ_READ_REGISTER = 0,
	ACPI_EINJ_READ_REGISTER_VALUE = 1,
	ACPI_EINJ_WRITE_REGISTER = 2,
	ACPI_EINJ_WRITE_REGISTER_VALUE = 3,
	ACPI_EINJ_NOOP = 4,
	ACPI_EINJ_FLUSH_CACHELINE = 5,
	ACPI_EINJ_INSTRUCTION_RESERVED = 6	/* 6 and greater are reserved */
};

struct acpi_einj_error_type_with_addr {
	u32 error_type;
	u32 vendor_struct_offset;
	u32 flags;
	u32 apic_id;
	u64 address;
	u64 range;
	u32 pcie_id;
};

struct acpi_einj_vendor {
	u32 length;
	u32 pcie_id;
	u16 vendor_id;
	u16 device_id;
	u8 revision_id;
	u8 reserved[3];
};

/* EINJ Trigger Error Action Table */

struct acpi_einj_trigger {
	u32 header_size;
	u32 revision;
	u32 table_size;
	u32 entry_count;
};

/* Command status return values */

enum acpi_einj_command_status {
	ACPI_EINJ_SUCCESS = 0,
	ACPI_EINJ_FAILURE = 1,
	ACPI_EINJ_INVALID_ACCESS = 2,
	ACPI_EINJ_STATUS_RESERVED = 3	/* 3 and greater are reserved */
};

/* Error types returned from ACPI_EINJ_GET_ERROR_TYPE (bitfield) */

#define ACPI_EINJ_PROCESSOR_CORRECTABLE     (1)
#define ACPI_EINJ_PROCESSOR_UNCORRECTABLE   (1<<1)
#define ACPI_EINJ_PROCESSOR_FATAL           (1<<2)
#define ACPI_EINJ_MEMORY_CORRECTABLE        (1<<3)
#define ACPI_EINJ_MEMORY_UNCORRECTABLE      (1<<4)
#define ACPI_EINJ_MEMORY_FATAL              (1<<5)
#define ACPI_EINJ_PCIX_CORRECTABLE          (1<<6)
#define ACPI_EINJ_PCIX_UNCORRECTABLE        (1<<7)
#define ACPI_EINJ_PCIX_FATAL                (1<<8)
#define ACPI_EINJ_PLATFORM_CORRECTABLE      (1<<9)
#define ACPI_EINJ_PLATFORM_UNCORRECTABLE    (1<<10)
#define ACPI_EINJ_PLATFORM_FATAL            (1<<11)
#define ACPI_EINJ_VENDOR_DEFINED            (1<<31)

/*******************************************************************************
 *
 * ERST - Error Record Serialization Table (ACPI 4.0)
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_erst {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 header_length;
	u32 reserved;
	u32 entries;
};

/* ERST Serialization Entries (actions) */

struct acpi_erst_entry {
	struct acpi_whea_header whea_header;	/* Common header for WHEA tables */
};

/* Masks for Flags field above */

#define ACPI_ERST_PRESERVE          (1)

/* Values for Action field above */

enum acpi_erst_actions {
	ACPI_ERST_BEGIN_WRITE = 0,
	ACPI_ERST_BEGIN_READ = 1,
	ACPI_ERST_BEGIN_CLEAR = 2,
	ACPI_ERST_END = 3,
	ACPI_ERST_SET_RECORD_OFFSET = 4,
	ACPI_ERST_EXECUTE_OPERATION = 5,
	ACPI_ERST_CHECK_BUSY_STATUS = 6,
	ACPI_ERST_GET_COMMAND_STATUS = 7,
	ACPI_ERST_GET_RECORD_ID = 8,
	ACPI_ERST_SET_RECORD_ID = 9,
	ACPI_ERST_GET_RECORD_COUNT = 10,
	ACPI_ERST_BEGIN_DUMMY_WRIITE = 11,
	ACPI_ERST_NOT_USED = 12,
	ACPI_ERST_GET_ERROR_RANGE = 13,
	ACPI_ERST_GET_ERROR_LENGTH = 14,
	ACPI_ERST_GET_ERROR_ATTRIBUTES = 15,
	ACPI_ERST_EXECUTE_TIMINGS = 16,
	ACPI_ERST_ACTION_RESERVED = 17	/* 17 and greater are reserved */
};

/* Values for Instruction field above */

enum acpi_erst_instructions {
	ACPI_ERST_READ_REGISTER = 0,
	ACPI_ERST_READ_REGISTER_VALUE = 1,
	ACPI_ERST_WRITE_REGISTER = 2,
	ACPI_ERST_WRITE_REGISTER_VALUE = 3,
	ACPI_ERST_NOOP = 4,
	ACPI_ERST_LOAD_VAR1 = 5,
	ACPI_ERST_LOAD_VAR2 = 6,
	ACPI_ERST_STORE_VAR1 = 7,
	ACPI_ERST_ADD = 8,
	ACPI_ERST_SUBTRACT = 9,
	ACPI_ERST_ADD_VALUE = 10,
	ACPI_ERST_SUBTRACT_VALUE = 11,
	ACPI_ERST_STALL = 12,
	ACPI_ERST_STALL_WHILE_TRUE = 13,
	ACPI_ERST_SKIP_NEXT_IF_TRUE = 14,
	ACPI_ERST_GOTO = 15,
	ACPI_ERST_SET_SRC_ADDRESS_BASE = 16,
	ACPI_ERST_SET_DST_ADDRESS_BASE = 17,
	ACPI_ERST_MOVE_DATA = 18,
	ACPI_ERST_INSTRUCTION_RESERVED = 19	/* 19 and greater are reserved */
};

/* Command status return values */

enum acpi_erst_command_status {
	ACPI_ERST_SUCESS = 0,
	ACPI_ERST_NO_SPACE = 1,
	ACPI_ERST_NOT_AVAILABLE = 2,
	ACPI_ERST_FAILURE = 3,
	ACPI_ERST_RECORD_EMPTY = 4,
	ACPI_ERST_NOT_FOUND = 5,
	ACPI_ERST_STATUS_RESERVED = 6	/* 6 and greater are reserved */
};

/* Error Record Serialization Information */

struct acpi_erst_info {
	u16 signature;		/* Should be "ER" */
	u8 data[48];
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

/* FPDT subtable header (Performance Record Structure) */

struct acpi_fpdt_header {
	u16 type;
	u8 length;
	u8 revision;
};

/* Values for Type field above */

enum acpi_fpdt_type {
	ACPI_FPDT_TYPE_BOOT = 0,
	ACPI_FPDT_TYPE_S3PERF = 1
};

/*
 * FPDT subtables
 */

/* 0: Firmware Basic Boot Performance Record */

struct acpi_fpdt_boot_pointer {
	struct acpi_fpdt_header header;
	u8 reserved[4];
	u64 address;
};

/* 1: S3 Performance Table Pointer Record */

struct acpi_fpdt_s3pt_pointer {
	struct acpi_fpdt_header header;
	u8 reserved[4];
	u64 address;
};

/*
 * S3PT - S3 Performance Table. This table is pointed to by the
 * S3 Pointer Record above.
 */
struct acpi_table_s3pt {
	u8 signature[4];	/* "S3PT" */
	u32 length;
};

/*
 * S3PT Subtables (Not part of the actual FPDT)
 */

/* Values for Type field in S3PT header */

enum acpi_s3pt_type {
	ACPI_S3PT_TYPE_RESUME = 0,
	ACPI_S3PT_TYPE_SUSPEND = 1,
	ACPI_FPDT_BOOT_PERFORMANCE = 2
};

struct acpi_s3pt_resume {
	struct acpi_fpdt_header header;
	u32 resume_count;
	u64 full_resume;
	u64 average_resume;
};

struct acpi_s3pt_suspend {
	struct acpi_fpdt_header header;
	u64 suspend_start;
	u64 suspend_end;
};

/*
 * FPDT Boot Performance Record (Not part of the actual FPDT)
 */
struct acpi_fpdt_boot {
	struct acpi_fpdt_header header;
	u8 reserved[4];
	u64 reset_end;
	u64 load_start;
	u64 startup_start;
	u64 exit_services_entry;
	u64 exit_services_exit;
};

/*******************************************************************************
 *
 * GTDT - Generic Timer Description Table (ACPI 5.1)
 *        Version 2
 *
 ******************************************************************************/

struct acpi_table_gtdt {
	struct acpi_table_header header;	/* Common ACPI table header */
	u64 counter_block_addresss;
	u32 reserved;
	u32 secure_el1_interrupt;
	u32 secure_el1_flags;
	u32 non_secure_el1_interrupt;
	u32 non_secure_el1_flags;
	u32 virtual_timer_interrupt;
	u32 virtual_timer_flags;
	u32 non_secure_el2_interrupt;
	u32 non_secure_el2_flags;
	u64 counter_read_block_address;
	u32 platform_timer_count;
	u32 platform_timer_offset;
};

/* Flag Definitions: Timer Block Physical Timers and Virtual timers */

#define ACPI_GTDT_INTERRUPT_MODE        (1)
#define ACPI_GTDT_INTERRUPT_POLARITY    (1<<1)
#define ACPI_GTDT_ALWAYS_ON             (1<<2)

struct acpi_gtdt_el2 {
	u32 virtual_el2_timer_gsiv;
	u32 virtual_el2_timer_flags;
};

/* Common GTDT subtable header */

struct acpi_gtdt_header {
	u8 type;
	u16 length;
};

/* Values for GTDT subtable type above */

enum acpi_gtdt_type {
	ACPI_GTDT_TYPE_TIMER_BLOCK = 0,
	ACPI_GTDT_TYPE_WATCHDOG = 1,
	ACPI_GTDT_TYPE_RESERVED = 2	/* 2 and greater are reserved */
};

/* GTDT Subtables, correspond to Type in struct acpi_gtdt_header */

/* 0: Generic Timer Block */

struct acpi_gtdt_timer_block {
	struct acpi_gtdt_header header;
	u8 reserved;
	u64 block_address;
	u32 timer_count;
	u32 timer_offset;
};

/* Timer Sub-Structure, one per timer */

struct acpi_gtdt_timer_entry {
	u8 frame_number;
	u8 reserved[3];
	u64 base_address;
	u64 el0_base_address;
	u32 timer_interrupt;
	u32 timer_flags;
	u32 virtual_timer_interrupt;
	u32 virtual_timer_flags;
	u32 common_flags;
};

/* Flag Definitions: timer_flags and virtual_timer_flags above */

#define ACPI_GTDT_GT_IRQ_MODE               (1)
#define ACPI_GTDT_GT_IRQ_POLARITY           (1<<1)

/* Flag Definitions: common_flags above */

#define ACPI_GTDT_GT_IS_SECURE_TIMER        (1)
#define ACPI_GTDT_GT_ALWAYS_ON              (1<<1)

/* 1: SBSA Generic Watchdog Structure */

struct acpi_gtdt_watchdog {
	struct acpi_gtdt_header header;
	u8 reserved;
	u64 refresh_frame_address;
	u64 control_frame_address;
	u32 timer_interrupt;
	u32 timer_flags;
};

/* Flag Definitions: timer_flags above */

#define ACPI_GTDT_WATCHDOG_IRQ_MODE         (1)
#define ACPI_GTDT_WATCHDOG_IRQ_POLARITY     (1<<1)
#define ACPI_GTDT_WATCHDOG_SECURE           (1<<2)

/*******************************************************************************
 *
 * HEST - Hardware Error Source Table (ACPI 4.0)
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_hest {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 error_source_count;
};

/* HEST subtable header */

struct acpi_hest_header {
	u16 type;
	u16 source_id;
};

/* Values for Type field above for subtables */

enum acpi_hest_types {
	ACPI_HEST_TYPE_IA32_CHECK = 0,
	ACPI_HEST_TYPE_IA32_CORRECTED_CHECK = 1,
	ACPI_HEST_TYPE_IA32_NMI = 2,
	ACPI_HEST_TYPE_NOT_USED3 = 3,
	ACPI_HEST_TYPE_NOT_USED4 = 4,
	ACPI_HEST_TYPE_NOT_USED5 = 5,
	ACPI_HEST_TYPE_AER_ROOT_PORT = 6,
	ACPI_HEST_TYPE_AER_ENDPOINT = 7,
	ACPI_HEST_TYPE_AER_BRIDGE = 8,
	ACPI_HEST_TYPE_GENERIC_ERROR = 9,
	ACPI_HEST_TYPE_GENERIC_ERROR_V2 = 10,
	ACPI_HEST_TYPE_IA32_DEFERRED_CHECK = 11,
	ACPI_HEST_TYPE_RESERVED = 12	/* 12 and greater are reserved */
};

/*
 * HEST substructures contained in subtables
 */

/*
 * IA32 Error Bank(s) - Follows the struct acpi_hest_ia_machine_check and
 * struct acpi_hest_ia_corrected structures.
 */
struct acpi_hest_ia_error_bank {
	u8 bank_number;
	u8 clear_status_on_init;
	u8 status_format;
	u8 reserved;
	u32 control_register;
	u64 control_data;
	u32 status_register;
	u32 address_register;
	u32 misc_register;
};

/* Common HEST sub-structure for PCI/AER structures below (6,7,8) */

struct acpi_hest_aer_common {
	u16 reserved1;
	u8 flags;
	u8 enabled;
	u32 records_to_preallocate;
	u32 max_sections_per_record;
	u32 bus;		/* Bus and Segment numbers */
	u16 device;
	u16 function;
	u16 device_control;
	u16 reserved2;
	u32 uncorrectable_mask;
	u32 uncorrectable_severity;
	u32 correctable_mask;
	u32 advanced_capabilities;
};

/* Masks for HEST Flags fields */

#define ACPI_HEST_FIRMWARE_FIRST        (1)
#define ACPI_HEST_GLOBAL                (1<<1)
#define ACPI_HEST_GHES_ASSIST           (1<<2)

/*
 * Macros to access the bus/segment numbers in Bus field above:
 *  Bus number is encoded in bits 7:0
 *  Segment number is encoded in bits 23:8
 */
#define ACPI_HEST_BUS(bus)              ((bus) & 0xFF)
#define ACPI_HEST_SEGMENT(bus)          (((bus) >> 8) & 0xFFFF)

/* Hardware Error Notification */

struct acpi_hest_notify {
	u8 type;
	u8 length;
	u16 config_write_enable;
	u32 poll_interval;
	u32 vector;
	u32 polling_threshold_value;
	u32 polling_threshold_window;
	u32 error_threshold_value;
	u32 error_threshold_window;
};

/* Values for Notify Type field above */

enum acpi_hest_notify_types {
	ACPI_HEST_NOTIFY_POLLED = 0,
	ACPI_HEST_NOTIFY_EXTERNAL = 1,
	ACPI_HEST_NOTIFY_LOCAL = 2,
	ACPI_HEST_NOTIFY_SCI = 3,
	ACPI_HEST_NOTIFY_NMI = 4,
	ACPI_HEST_NOTIFY_CMCI = 5,	/* ACPI 5.0 */
	ACPI_HEST_NOTIFY_MCE = 6,	/* ACPI 5.0 */
	ACPI_HEST_NOTIFY_GPIO = 7,	/* ACPI 6.0 */
	ACPI_HEST_NOTIFY_SEA = 8,	/* ACPI 6.1 */
	ACPI_HEST_NOTIFY_SEI = 9,	/* ACPI 6.1 */
	ACPI_HEST_NOTIFY_GSIV = 10,	/* ACPI 6.1 */
	ACPI_HEST_NOTIFY_SOFTWARE_DELEGATED = 11,	/* ACPI 6.2 */
	ACPI_HEST_NOTIFY_RESERVED = 12	/* 12 and greater are reserved */
};

/* Values for config_write_enable bitfield above */

#define ACPI_HEST_TYPE                  (1)
#define ACPI_HEST_POLL_INTERVAL         (1<<1)
#define ACPI_HEST_POLL_THRESHOLD_VALUE  (1<<2)
#define ACPI_HEST_POLL_THRESHOLD_WINDOW (1<<3)
#define ACPI_HEST_ERR_THRESHOLD_VALUE   (1<<4)
#define ACPI_HEST_ERR_THRESHOLD_WINDOW  (1<<5)

/*
 * HEST subtables
 */

/* 0: IA32 Machine Check Exception */

struct acpi_hest_ia_machine_check {
	struct acpi_hest_header header;
	u16 reserved1;
	u8 flags;		/* See flags ACPI_HEST_GLOBAL, etc. above */
	u8 enabled;
	u32 records_to_preallocate;
	u32 max_sections_per_record;
	u64 global_capability_data;
	u64 global_control_data;
	u8 num_hardware_banks;
	u8 reserved3[7];
};

/* 1: IA32 Corrected Machine Check */

struct acpi_hest_ia_corrected {
	struct acpi_hest_header header;
	u16 reserved1;
	u8 flags;		/* See flags ACPI_HEST_GLOBAL, etc. above */
	u8 enabled;
	u32 records_to_preallocate;
	u32 max_sections_per_record;
	struct acpi_hest_notify notify;
	u8 num_hardware_banks;
	u8 reserved2[3];
};

/* 2: IA32 Non-Maskable Interrupt */

struct acpi_hest_ia_nmi {
	struct acpi_hest_header header;
	u32 reserved;
	u32 records_to_preallocate;
	u32 max_sections_per_record;
	u32 max_raw_data_length;
};

/* 3,4,5: Not used */

/* 6: PCI Express Root Port AER */

struct acpi_hest_aer_root {
	struct acpi_hest_header header;
	struct acpi_hest_aer_common aer;
	u32 root_error_command;
};

/* 7: PCI Express AER (AER Endpoint) */

struct acpi_hest_aer {
	struct acpi_hest_header header;
	struct acpi_hest_aer_common aer;
};

/* 8: PCI Express/PCI-X Bridge AER */

struct acpi_hest_aer_bridge {
	struct acpi_hest_header header;
	struct acpi_hest_aer_common aer;
	u32 uncorrectable_mask2;
	u32 uncorrectable_severity2;
	u32 advanced_capabilities2;
};

/* 9: Generic Hardware Error Source */

struct acpi_hest_generic {
	struct acpi_hest_header header;
	u16 related_source_id;
	u8 reserved;
	u8 enabled;
	u32 records_to_preallocate;
	u32 max_sections_per_record;
	u32 max_raw_data_length;
	struct acpi_generic_address error_status_address;
	struct acpi_hest_notify notify;
	u32 error_block_length;
};

/* 10: Generic Hardware Error Source, version 2 */

struct acpi_hest_generic_v2 {
	struct acpi_hest_header header;
	u16 related_source_id;
	u8 reserved;
	u8 enabled;
	u32 records_to_preallocate;
	u32 max_sections_per_record;
	u32 max_raw_data_length;
	struct acpi_generic_address error_status_address;
	struct acpi_hest_notify notify;
	u32 error_block_length;
	struct acpi_generic_address read_ack_register;
	u64 read_ack_preserve;
	u64 read_ack_write;
};

/* Generic Error Status block */

struct acpi_hest_generic_status {
	u32 block_status;
	u32 raw_data_offset;
	u32 raw_data_length;
	u32 data_length;
	u32 error_severity;
};

/* Values for block_status flags above */

#define ACPI_HEST_UNCORRECTABLE             (1)
#define ACPI_HEST_CORRECTABLE               (1<<1)
#define ACPI_HEST_MULTIPLE_UNCORRECTABLE    (1<<2)
#define ACPI_HEST_MULTIPLE_CORRECTABLE      (1<<3)
#define ACPI_HEST_ERROR_ENTRY_COUNT         (0xFF<<4)	/* 8 bits, error count */

/* Generic Error Data entry */

struct acpi_hest_generic_data {
	u8 section_type[16];
	u32 error_severity;
	u16 revision;
	u8 validation_bits;
	u8 flags;
	u32 error_data_length;
	u8 fru_id[16];
	u8 fru_text[20];
};

/* Extension for revision 0x0300 */

struct acpi_hest_generic_data_v300 {
	u8 section_type[16];
	u32 error_severity;
	u16 revision;
	u8 validation_bits;
	u8 flags;
	u32 error_data_length;
	u8 fru_id[16];
	u8 fru_text[20];
	u64 time_stamp;
};

/* Values for error_severity above */

#define ACPI_HEST_GEN_ERROR_RECOVERABLE     0
#define ACPI_HEST_GEN_ERROR_FATAL           1
#define ACPI_HEST_GEN_ERROR_CORRECTED       2
#define ACPI_HEST_GEN_ERROR_NONE            3

/* Flags for validation_bits above */

#define ACPI_HEST_GEN_VALID_FRU_ID          (1)
#define ACPI_HEST_GEN_VALID_FRU_STRING      (1<<1)
#define ACPI_HEST_GEN_VALID_TIMESTAMP       (1<<2)

/* 11: IA32 Deferred Machine Check Exception (ACPI 6.2) */

struct acpi_hest_ia_deferred_check {
	struct acpi_hest_header header;
	u16 reserved1;
	u8 flags;		/* See flags ACPI_HEST_GLOBAL, etc. above */
	u8 enabled;
	u32 records_to_preallocate;
	u32 max_sections_per_record;
	struct acpi_hest_notify notify;
	u8 num_hardware_banks;
	u8 reserved2[3];
};

/*******************************************************************************
 *
 * HMAT - Heterogeneous Memory Attributes Table (ACPI 6.2)
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_hmat {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 reserved;
};

/* Values for HMAT structure types */

enum acpi_hmat_type {
	ACPI_HMAT_TYPE_PROXIMITY = 0,	/* Memory proximity domain attributes */
	ACPI_HMAT_TYPE_LOCALITY = 1,	/* System locality latency and bandwidth information */
	ACPI_HMAT_TYPE_CACHE = 2,	/* Memory side cache information */
	ACPI_HMAT_TYPE_RESERVED = 3	/* 3 and greater are reserved */
};

struct acpi_hmat_structure {
	u16 type;
	u16 reserved;
	u32 length;
};

/*
 * HMAT Structures, correspond to Type in struct acpi_hmat_structure
 */

/* 0: Memory proximity domain attributes */

struct acpi_hmat_proximity_domain {
	struct acpi_hmat_structure header;
	u16 flags;
	u16 reserved1;
	u32 processor_PD;	/* Processor proximity domain */
	u32 memory_PD;		/* Memory proximity domain */
	u32 reserved2;
	u64 reserved3;
	u64 reserved4;
};

/* Masks for Flags field above */

#define ACPI_HMAT_PROCESSOR_PD_VALID    (1)	/* 1: processor_PD field is valid */
#define ACPI_HMAT_MEMORY_PD_VALID       (1<<1)	/* 1: memory_PD field is valid */
#define ACPI_HMAT_RESERVATION_HINT      (1<<2)	/* 1: Reservation hint */

/* 1: System locality latency and bandwidth information */

struct acpi_hmat_locality {
	struct acpi_hmat_structure header;
	u8 flags;
	u8 data_type;
	u16 reserved1;
	u32 number_of_initiator_Pds;
	u32 number_of_target_Pds;
	u32 reserved2;
	u64 entry_base_unit;
};

/* Masks for Flags field above */

#define ACPI_HMAT_MEMORY_HIERARCHY  (0x0F)

/* Values for Memory Hierarchy flag */

#define ACPI_HMAT_MEMORY            0
#define ACPI_HMAT_LAST_LEVEL_CACHE  1
#define ACPI_HMAT_1ST_LEVEL_CACHE   2
#define ACPI_HMAT_2ND_LEVEL_CACHE   3
#define ACPI_HMAT_3RD_LEVEL_CACHE   4

/* Values for data_type field above */

#define ACPI_HMAT_ACCESS_LATENCY    0
#define ACPI_HMAT_READ_LATENCY      1
#define ACPI_HMAT_WRITE_LATENCY     2
#define ACPI_HMAT_ACCESS_BANDWIDTH  3
#define ACPI_HMAT_READ_BANDWIDTH    4
#define ACPI_HMAT_WRITE_BANDWIDTH   5

/* 2: Memory side cache information */

struct acpi_hmat_cache {
	struct acpi_hmat_structure header;
	u32 memory_PD;
	u32 reserved1;
	u64 cache_size;
	u32 cache_attributes;
	u16 reserved2;
	u16 number_of_SMBIOShandles;
};

/* Masks for cache_attributes field above */

#define ACPI_HMAT_TOTAL_CACHE_LEVEL     (0x0000000F)
#define ACPI_HMAT_CACHE_LEVEL           (0x000000F0)
#define ACPI_HMAT_CACHE_ASSOCIATIVITY   (0x00000F00)
#define ACPI_HMAT_WRITE_POLICY          (0x0000F000)
#define ACPI_HMAT_CACHE_LINE_SIZE       (0xFFFF0000)

/* Values for cache associativity flag */

#define ACPI_HMAT_CA_NONE                     (0)
#define ACPI_HMAT_CA_DIRECT_MAPPED            (1)
#define ACPI_HMAT_CA_COMPLEX_CACHE_INDEXING   (2)

/* Values for write policy flag */

#define ACPI_HMAT_CP_NONE   (0)
#define ACPI_HMAT_CP_WB     (1)
#define ACPI_HMAT_CP_WT     (2)

/*******************************************************************************
 *
 * HPET - High Precision Event Timer table
 *        Version 1
 *
 * Conforms to "IA-PC HPET (High Precision Event Timers) Specification",
 * Version 1.0a, October 2004
 *
 ******************************************************************************/

struct acpi_table_hpet {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 id;			/* Hardware ID of event timer block */
	struct acpi_generic_address address;	/* Address of event timer block */
	u8 sequence;		/* HPET sequence number */
	u16 minimum_tick;	/* Main counter min tick, periodic mode */
	u8 flags;
};

/* Masks for Flags field above */

#define ACPI_HPET_PAGE_PROTECT_MASK (3)

/* Values for Page Protect flags */

enum acpi_hpet_page_protect {
	ACPI_HPET_NO_PAGE_PROTECT = 0,
	ACPI_HPET_PAGE_PROTECT4 = 1,
	ACPI_HPET_PAGE_PROTECT64 = 2
};

/*******************************************************************************
 *
 * IBFT - Boot Firmware Table
 *        Version 1
 *
 * Conforms to "iSCSI Boot Firmware Table (iBFT) as Defined in ACPI 3.0b
 * Specification", Version 1.01, March 1, 2007
 *
 * Note: It appears that this table is not intended to appear in the RSDT/XSDT.
 * Therefore, it is not currently supported by the disassembler.
 *
 ******************************************************************************/

struct acpi_table_ibft {
	struct acpi_table_header header;	/* Common ACPI table header */
	u8 reserved[12];
};

/* IBFT common subtable header */

struct acpi_ibft_header {
	u8 type;
	u8 version;
	u16 length;
	u8 index;
	u8 flags;
};

/* Values for Type field above */

enum acpi_ibft_type {
	ACPI_IBFT_TYPE_NOT_USED = 0,
	ACPI_IBFT_TYPE_CONTROL = 1,
	ACPI_IBFT_TYPE_INITIATOR = 2,
	ACPI_IBFT_TYPE_NIC = 3,
	ACPI_IBFT_TYPE_TARGET = 4,
	ACPI_IBFT_TYPE_EXTENSIONS = 5,
	ACPI_IBFT_TYPE_RESERVED = 6	/* 6 and greater are reserved */
};

/* IBFT subtables */

struct acpi_ibft_control {
	struct acpi_ibft_header header;
	u16 extensions;
	u16 initiator_offset;
	u16 nic0_offset;
	u16 target0_offset;
	u16 nic1_offset;
	u16 target1_offset;
};

struct acpi_ibft_initiator {
	struct acpi_ibft_header header;
	u8 sns_server[16];
	u8 slp_server[16];
	u8 primary_server[16];
	u8 secondary_server[16];
	u16 name_length;
	u16 name_offset;
};

struct acpi_ibft_nic {
	struct acpi_ibft_header header;
	u8 ip_address[16];
	u8 subnet_mask_prefix;
	u8 origin;
	u8 gateway[16];
	u8 primary_dns[16];
	u8 secondary_dns[16];
	u8 dhcp[16];
	u16 vlan;
	u8 mac_address[6];
	u16 pci_address;
	u16 name_length;
	u16 name_offset;
};

struct acpi_ibft_target {
	struct acpi_ibft_header header;
	u8 target_ip_address[16];
	u16 target_ip_socket;
	u8 target_boot_lun[8];
	u8 chap_type;
	u8 nic_association;
	u16 target_name_length;
	u16 target_name_offset;
	u16 chap_name_length;
	u16 chap_name_offset;
	u16 chap_secret_length;
	u16 chap_secret_offset;
	u16 reverse_chap_name_length;
	u16 reverse_chap_name_offset;
	u16 reverse_chap_secret_length;
	u16 reverse_chap_secret_offset;
};

/* Reset to default packing */

#pragma pack()

#endif				/* __ACTBL1_H__ */
