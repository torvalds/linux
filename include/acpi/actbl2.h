/******************************************************************************
 *
 * Name: actbl2.h - ACPI Table Definitions (tables not in ACPI spec)
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2017, Intel Corp.
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

#ifndef __ACTBL2_H__
#define __ACTBL2_H__

/*******************************************************************************
 *
 * Additional ACPI Tables (2)
 *
 * These tables are not consumed directly by the ACPICA subsystem, but are
 * included here to support device drivers and the AML disassembler.
 *
 * Generally, the tables in this file are defined by third-party specifications,
 * and are not defined directly by the ACPI specification itself.
 *
 ******************************************************************************/

/*
 * Values for description table header signatures for tables defined in this
 * file. Useful because they make it more difficult to inadvertently type in
 * the wrong signature.
 */
#define ACPI_SIG_ASF            "ASF!"	/* Alert Standard Format table */
#define ACPI_SIG_BOOT           "BOOT"	/* Simple Boot Flag Table */
#define ACPI_SIG_CSRT           "CSRT"	/* Core System Resource Table */
#define ACPI_SIG_DBG2           "DBG2"	/* Debug Port table type 2 */
#define ACPI_SIG_DBGP           "DBGP"	/* Debug Port table */
#define ACPI_SIG_DMAR           "DMAR"	/* DMA Remapping table */
#define ACPI_SIG_HPET           "HPET"	/* High Precision Event Timer table */
#define ACPI_SIG_IBFT           "IBFT"	/* iSCSI Boot Firmware Table */
#define ACPI_SIG_IORT           "IORT"	/* IO Remapping Table */
#define ACPI_SIG_IVRS           "IVRS"	/* I/O Virtualization Reporting Structure */
#define ACPI_SIG_LPIT           "LPIT"	/* Low Power Idle Table */
#define ACPI_SIG_MCFG           "MCFG"	/* PCI Memory Mapped Configuration table */
#define ACPI_SIG_MCHI           "MCHI"	/* Management Controller Host Interface table */
#define ACPI_SIG_MSDM           "MSDM"	/* Microsoft Data Management Table */
#define ACPI_SIG_MTMR           "MTMR"	/* MID Timer table */
#define ACPI_SIG_SLIC           "SLIC"	/* Software Licensing Description Table */
#define ACPI_SIG_SPCR           "SPCR"	/* Serial Port Console Redirection table */
#define ACPI_SIG_SPMI           "SPMI"	/* Server Platform Management Interface table */
#define ACPI_SIG_TCPA           "TCPA"	/* Trusted Computing Platform Alliance table */
#define ACPI_SIG_TPM2           "TPM2"	/* Trusted Platform Module 2.0 H/W interface table */
#define ACPI_SIG_UEFI           "UEFI"	/* Uefi Boot Optimization Table */
#define ACPI_SIG_VRTC           "VRTC"	/* Virtual Real Time Clock Table */
#define ACPI_SIG_WAET           "WAET"	/* Windows ACPI Emulated devices Table */
#define ACPI_SIG_WDAT           "WDAT"	/* Watchdog Action Table */
#define ACPI_SIG_WDDT           "WDDT"	/* Watchdog Timer Description Table */
#define ACPI_SIG_WDRT           "WDRT"	/* Watchdog Resource Table */

#ifdef ACPI_UNDEFINED_TABLES
/*
 * These tables have been seen in the field, but no definition has been found
 */
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

/* 1: Reserved Memory Defininition */

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

/*******************************************************************************
 *
 * IORT - IO Remapping Table
 *
 * Conforms to "IO Remapping Table System Software on ARM Platforms",
 * Document number: ARM DEN 0049B, October 2015
 *
 ******************************************************************************/

struct acpi_table_iort {
	struct acpi_table_header header;
	u32 node_count;
	u32 node_offset;
	u32 reserved;
};

/*
 * IORT subtables
 */
struct acpi_iort_node {
	u8 type;
	u16 length;
	u8 revision;
	u32 reserved;
	u32 mapping_count;
	u32 mapping_offset;
	char node_data[1];
};

/* Values for subtable Type above */

enum acpi_iort_node_type {
	ACPI_IORT_NODE_ITS_GROUP = 0x00,
	ACPI_IORT_NODE_NAMED_COMPONENT = 0x01,
	ACPI_IORT_NODE_PCI_ROOT_COMPLEX = 0x02,
	ACPI_IORT_NODE_SMMU = 0x03,
	ACPI_IORT_NODE_SMMU_V3 = 0x04
};

struct acpi_iort_id_mapping {
	u32 input_base;		/* Lowest value in input range */
	u32 id_count;		/* Number of IDs */
	u32 output_base;	/* Lowest value in output range */
	u32 output_reference;	/* A reference to the output node */
	u32 flags;
};

/* Masks for Flags field above for IORT subtable */

#define ACPI_IORT_ID_SINGLE_MAPPING (1)

struct acpi_iort_memory_access {
	u32 cache_coherency;
	u8 hints;
	u16 reserved;
	u8 memory_flags;
};

/* Values for cache_coherency field above */

#define ACPI_IORT_NODE_COHERENT         0x00000001	/* The device node is fully coherent */
#define ACPI_IORT_NODE_NOT_COHERENT     0x00000000	/* The device node is not coherent */

/* Masks for Hints field above */

#define ACPI_IORT_HT_TRANSIENT          (1)
#define ACPI_IORT_HT_WRITE              (1<<1)
#define ACPI_IORT_HT_READ               (1<<2)
#define ACPI_IORT_HT_OVERRIDE           (1<<3)

/* Masks for memory_flags field above */

#define ACPI_IORT_MF_COHERENCY          (1)
#define ACPI_IORT_MF_ATTRIBUTES         (1<<1)

/*
 * IORT node specific subtables
 */
struct acpi_iort_its_group {
	u32 its_count;
	u32 identifiers[1];	/* GIC ITS identifier arrary */
};

struct acpi_iort_named_component {
	u32 node_flags;
	u64 memory_properties;	/* Memory access properties */
	u8 memory_address_limit;	/* Memory address size limit */
	char device_name[1];	/* Path of namespace object */
};

struct acpi_iort_root_complex {
	u64 memory_properties;	/* Memory access properties */
	u32 ats_attribute;
	u32 pci_segment_number;
};

/* Values for ats_attribute field above */

#define ACPI_IORT_ATS_SUPPORTED         0x00000001	/* The root complex supports ATS */
#define ACPI_IORT_ATS_UNSUPPORTED       0x00000000	/* The root complex doesn't support ATS */

struct acpi_iort_smmu {
	u64 base_address;	/* SMMU base address */
	u64 span;		/* Length of memory range */
	u32 model;
	u32 flags;
	u32 global_interrupt_offset;
	u32 context_interrupt_count;
	u32 context_interrupt_offset;
	u32 pmu_interrupt_count;
	u32 pmu_interrupt_offset;
	u64 interrupts[1];	/* Interrupt array */
};

/* Values for Model field above */

#define ACPI_IORT_SMMU_V1               0x00000000	/* Generic SMMUv1 */
#define ACPI_IORT_SMMU_V2               0x00000001	/* Generic SMMUv2 */
#define ACPI_IORT_SMMU_CORELINK_MMU400  0x00000002	/* ARM Corelink MMU-400 */
#define ACPI_IORT_SMMU_CORELINK_MMU500  0x00000003	/* ARM Corelink MMU-500 */

/* Masks for Flags field above */

#define ACPI_IORT_SMMU_DVM_SUPPORTED    (1)
#define ACPI_IORT_SMMU_COHERENT_WALK    (1<<1)

struct acpi_iort_smmu_v3 {
	u64 base_address;	/* SMMUv3 base address */
	u32 flags;
	u32 reserved;
	u64 vatos_address;
	u32 model;		/* O: generic SMMUv3 */
	u32 event_gsiv;
	u32 pri_gsiv;
	u32 gerr_gsiv;
	u32 sync_gsiv;
};

/* Masks for Flags field above */

#define ACPI_IORT_SMMU_V3_COHACC_OVERRIDE   (1)
#define ACPI_IORT_SMMU_V3_HTTU_OVERRIDE     (1<<1)

/*******************************************************************************
 *
 * IVRS - I/O Virtualization Reporting Structure
 *        Version 1
 *
 * Conforms to "AMD I/O Virtualization Technology (IOMMU) Specification",
 * Revision 1.26, February 2009.
 *
 ******************************************************************************/

struct acpi_table_ivrs {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 info;		/* Common virtualization info */
	u64 reserved;
};

/* Values for Info field above */

#define ACPI_IVRS_PHYSICAL_SIZE     0x00007F00	/* 7 bits, physical address size */
#define ACPI_IVRS_VIRTUAL_SIZE      0x003F8000	/* 7 bits, virtual address size */
#define ACPI_IVRS_ATS_RESERVED      0x00400000	/* ATS address translation range reserved */

/* IVRS subtable header */

struct acpi_ivrs_header {
	u8 type;		/* Subtable type */
	u8 flags;
	u16 length;		/* Subtable length */
	u16 device_id;		/* ID of IOMMU */
};

/* Values for subtable Type above */

enum acpi_ivrs_type {
	ACPI_IVRS_TYPE_HARDWARE = 0x10,
	ACPI_IVRS_TYPE_MEMORY1 = 0x20,
	ACPI_IVRS_TYPE_MEMORY2 = 0x21,
	ACPI_IVRS_TYPE_MEMORY3 = 0x22
};

/* Masks for Flags field above for IVHD subtable */

#define ACPI_IVHD_TT_ENABLE         (1)
#define ACPI_IVHD_PASS_PW           (1<<1)
#define ACPI_IVHD_RES_PASS_PW       (1<<2)
#define ACPI_IVHD_ISOC              (1<<3)
#define ACPI_IVHD_IOTLB             (1<<4)

/* Masks for Flags field above for IVMD subtable */

#define ACPI_IVMD_UNITY             (1)
#define ACPI_IVMD_READ              (1<<1)
#define ACPI_IVMD_WRITE             (1<<2)
#define ACPI_IVMD_EXCLUSION_RANGE   (1<<3)

/*
 * IVRS subtables, correspond to Type in struct acpi_ivrs_header
 */

/* 0x10: I/O Virtualization Hardware Definition Block (IVHD) */

struct acpi_ivrs_hardware {
	struct acpi_ivrs_header header;
	u16 capability_offset;	/* Offset for IOMMU control fields */
	u64 base_address;	/* IOMMU control registers */
	u16 pci_segment_group;
	u16 info;		/* MSI number and unit ID */
	u32 reserved;
};

/* Masks for Info field above */

#define ACPI_IVHD_MSI_NUMBER_MASK   0x001F	/* 5 bits, MSI message number */
#define ACPI_IVHD_UNIT_ID_MASK      0x1F00	/* 5 bits, unit_ID */

/*
 * Device Entries for IVHD subtable, appear after struct acpi_ivrs_hardware structure.
 * Upper two bits of the Type field are the (encoded) length of the structure.
 * Currently, only 4 and 8 byte entries are defined. 16 and 32 byte entries
 * are reserved for future use but not defined.
 */
struct acpi_ivrs_de_header {
	u8 type;
	u16 id;
	u8 data_setting;
};

/* Length of device entry is in the top two bits of Type field above */

#define ACPI_IVHD_ENTRY_LENGTH      0xC0

/* Values for device entry Type field above */

enum acpi_ivrs_device_entry_type {
	/* 4-byte device entries, all use struct acpi_ivrs_device4 */

	ACPI_IVRS_TYPE_PAD4 = 0,
	ACPI_IVRS_TYPE_ALL = 1,
	ACPI_IVRS_TYPE_SELECT = 2,
	ACPI_IVRS_TYPE_START = 3,
	ACPI_IVRS_TYPE_END = 4,

	/* 8-byte device entries */

	ACPI_IVRS_TYPE_PAD8 = 64,
	ACPI_IVRS_TYPE_NOT_USED = 65,
	ACPI_IVRS_TYPE_ALIAS_SELECT = 66,	/* Uses struct acpi_ivrs_device8a */
	ACPI_IVRS_TYPE_ALIAS_START = 67,	/* Uses struct acpi_ivrs_device8a */
	ACPI_IVRS_TYPE_EXT_SELECT = 70,	/* Uses struct acpi_ivrs_device8b */
	ACPI_IVRS_TYPE_EXT_START = 71,	/* Uses struct acpi_ivrs_device8b */
	ACPI_IVRS_TYPE_SPECIAL = 72	/* Uses struct acpi_ivrs_device8c */
};

/* Values for Data field above */

#define ACPI_IVHD_INIT_PASS         (1)
#define ACPI_IVHD_EINT_PASS         (1<<1)
#define ACPI_IVHD_NMI_PASS          (1<<2)
#define ACPI_IVHD_SYSTEM_MGMT       (3<<4)
#define ACPI_IVHD_LINT0_PASS        (1<<6)
#define ACPI_IVHD_LINT1_PASS        (1<<7)

/* Types 0-4: 4-byte device entry */

struct acpi_ivrs_device4 {
	struct acpi_ivrs_de_header header;
};

/* Types 66-67: 8-byte device entry */

struct acpi_ivrs_device8a {
	struct acpi_ivrs_de_header header;
	u8 reserved1;
	u16 used_id;
	u8 reserved2;
};

/* Types 70-71: 8-byte device entry */

struct acpi_ivrs_device8b {
	struct acpi_ivrs_de_header header;
	u32 extended_data;
};

/* Values for extended_data above */

#define ACPI_IVHD_ATS_DISABLED      (1<<31)

/* Type 72: 8-byte device entry */

struct acpi_ivrs_device8c {
	struct acpi_ivrs_de_header header;
	u8 handle;
	u16 used_id;
	u8 variety;
};

/* Values for Variety field above */

#define ACPI_IVHD_IOAPIC            1
#define ACPI_IVHD_HPET              2

/* 0x20, 0x21, 0x22: I/O Virtualization Memory Definition Block (IVMD) */

struct acpi_ivrs_memory {
	struct acpi_ivrs_header header;
	u16 aux_data;
	u64 reserved;
	u64 start_address;
	u64 memory_length;
};

/*******************************************************************************
 *
 * LPIT - Low Power Idle Table
 *
 * Conforms to "ACPI Low Power Idle Table (LPIT)" July 2014.
 *
 ******************************************************************************/

struct acpi_table_lpit {
	struct acpi_table_header header;	/* Common ACPI table header */
};

/* LPIT subtable header */

struct acpi_lpit_header {
	u32 type;		/* Subtable type */
	u32 length;		/* Subtable length */
	u16 unique_id;
	u16 reserved;
	u32 flags;
};

/* Values for subtable Type above */

enum acpi_lpit_type {
	ACPI_LPIT_TYPE_NATIVE_CSTATE = 0x00,
	ACPI_LPIT_TYPE_RESERVED = 0x01	/* 1 and above are reserved */
};

/* Masks for Flags field above  */

#define ACPI_LPIT_STATE_DISABLED    (1)
#define ACPI_LPIT_NO_COUNTER        (1<<1)

/*
 * LPIT subtables, correspond to Type in struct acpi_lpit_header
 */

/* 0x00: Native C-state instruction based LPI structure */

struct acpi_lpit_native {
	struct acpi_lpit_header header;
	struct acpi_generic_address entry_trigger;
	u32 residency;
	u32 latency;
	struct acpi_generic_address residency_counter;
	u64 counter_frequency;
};

/*******************************************************************************
 *
 * MCFG - PCI Memory Mapped Configuration table and subtable
 *        Version 1
 *
 * Conforms to "PCI Firmware Specification", Revision 3.0, June 20, 2005
 *
 ******************************************************************************/

struct acpi_table_mcfg {
	struct acpi_table_header header;	/* Common ACPI table header */
	u8 reserved[8];
};

/* Subtable */

struct acpi_mcfg_allocation {
	u64 address;		/* Base address, processor-relative */
	u16 pci_segment;	/* PCI segment group number */
	u8 start_bus_number;	/* Starting PCI Bus number */
	u8 end_bus_number;	/* Final PCI Bus number */
	u32 reserved;
};

/*******************************************************************************
 *
 * MCHI - Management Controller Host Interface Table
 *        Version 1
 *
 * Conforms to "Management Component Transport Protocol (MCTP) Host
 * Interface Specification", Revision 1.0.0a, October 13, 2009
 *
 ******************************************************************************/

struct acpi_table_mchi {
	struct acpi_table_header header;	/* Common ACPI table header */
	u8 interface_type;
	u8 protocol;
	u64 protocol_data;
	u8 interrupt_type;
	u8 gpe;
	u8 pci_device_flag;
	u32 global_interrupt;
	struct acpi_generic_address control_register;
	u8 pci_segment;
	u8 pci_bus;
	u8 pci_device;
	u8 pci_function;
};

/*******************************************************************************
 *
 * MSDM - Microsoft Data Management table
 *
 * Conforms to "Microsoft Software Licensing Tables (SLIC and MSDM)",
 * November 29, 2011. Copyright 2011 Microsoft
 *
 ******************************************************************************/

/* Basic MSDM table is only the common ACPI header */

struct acpi_table_msdm {
	struct acpi_table_header header;	/* Common ACPI table header */
};

/*******************************************************************************
 *
 * MTMR - MID Timer Table
 *        Version 1
 *
 * Conforms to "Simple Firmware Interface Specification",
 * Draft 0.8.2, Oct 19, 2010
 * NOTE: The ACPI MTMR is equivalent to the SFI MTMR table.
 *
 ******************************************************************************/

struct acpi_table_mtmr {
	struct acpi_table_header header;	/* Common ACPI table header */
};

/* MTMR entry */

struct acpi_mtmr_entry {
	struct acpi_generic_address physical_address;
	u32 frequency;
	u32 irq;
};

/*******************************************************************************
 *
 * SLIC - Software Licensing Description Table
 *
 * Conforms to "Microsoft Software Licensing Tables (SLIC and MSDM)",
 * November 29, 2011. Copyright 2011 Microsoft
 *
 ******************************************************************************/

/* Basic SLIC table is only the common ACPI header */

struct acpi_table_slic {
	struct acpi_table_header header;	/* Common ACPI table header */
};

/*******************************************************************************
 *
 * SPCR - Serial Port Console Redirection table
 *        Version 2
 *
 * Conforms to "Serial Port Console Redirection Table",
 * Version 1.03, August 10, 2015
 *
 ******************************************************************************/

struct acpi_table_spcr {
	struct acpi_table_header header;	/* Common ACPI table header */
	u8 interface_type;	/* 0=full 16550, 1=subset of 16550 */
	u8 reserved[3];
	struct acpi_generic_address serial_port;
	u8 interrupt_type;
	u8 pc_interrupt;
	u32 interrupt;
	u8 baud_rate;
	u8 parity;
	u8 stop_bits;
	u8 flow_control;
	u8 terminal_type;
	u8 reserved1;
	u16 pci_device_id;
	u16 pci_vendor_id;
	u8 pci_bus;
	u8 pci_device;
	u8 pci_function;
	u32 pci_flags;
	u8 pci_segment;
	u32 reserved2;
};

/* Masks for pci_flags field above */

#define ACPI_SPCR_DO_NOT_DISABLE    (1)

/* Values for Interface Type: See the definition of the DBG2 table */

/*******************************************************************************
 *
 * SPMI - Server Platform Management Interface table
 *        Version 5
 *
 * Conforms to "Intelligent Platform Management Interface Specification
 * Second Generation v2.0", Document Revision 1.0, February 12, 2004 with
 * June 12, 2009 markup.
 *
 ******************************************************************************/

struct acpi_table_spmi {
	struct acpi_table_header header;	/* Common ACPI table header */
	u8 interface_type;
	u8 reserved;		/* Must be 1 */
	u16 spec_revision;	/* Version of IPMI */
	u8 interrupt_type;
	u8 gpe_number;		/* GPE assigned */
	u8 reserved1;
	u8 pci_device_flag;
	u32 interrupt;
	struct acpi_generic_address ipmi_register;
	u8 pci_segment;
	u8 pci_bus;
	u8 pci_device;
	u8 pci_function;
	u8 reserved2;
};

/* Values for interface_type above */

enum acpi_spmi_interface_types {
	ACPI_SPMI_NOT_USED = 0,
	ACPI_SPMI_KEYBOARD = 1,
	ACPI_SPMI_SMI = 2,
	ACPI_SPMI_BLOCK_TRANSFER = 3,
	ACPI_SPMI_SMBUS = 4,
	ACPI_SPMI_RESERVED = 5	/* 5 and above are reserved */
};

/*******************************************************************************
 *
 * TCPA - Trusted Computing Platform Alliance table
 *        Version 2
 *
 * Conforms to "TCG ACPI Specification, Family 1.2 and 2.0",
 * December 19, 2014
 *
 * NOTE: There are two versions of the table with the same signature --
 * the client version and the server version. The common platform_class
 * field is used to differentiate the two types of tables.
 *
 ******************************************************************************/

struct acpi_table_tcpa_hdr {
	struct acpi_table_header header;	/* Common ACPI table header */
	u16 platform_class;
};

/*
 * Values for platform_class above.
 * This is how the client and server subtables are differentiated
 */
#define ACPI_TCPA_CLIENT_TABLE          0
#define ACPI_TCPA_SERVER_TABLE          1

struct acpi_table_tcpa_client {
	u32 minimum_log_length;	/* Minimum length for the event log area */
	u64 log_address;	/* Address of the event log area */
};

struct acpi_table_tcpa_server {
	u16 reserved;
	u64 minimum_log_length;	/* Minimum length for the event log area */
	u64 log_address;	/* Address of the event log area */
	u16 spec_revision;
	u8 device_flags;
	u8 interrupt_flags;
	u8 gpe_number;
	u8 reserved2[3];
	u32 global_interrupt;
	struct acpi_generic_address address;
	u32 reserved3;
	struct acpi_generic_address config_address;
	u8 group;
	u8 bus;			/* PCI Bus/Segment/Function numbers */
	u8 device;
	u8 function;
};

/* Values for device_flags above */

#define ACPI_TCPA_PCI_DEVICE            (1)
#define ACPI_TCPA_BUS_PNP               (1<<1)
#define ACPI_TCPA_ADDRESS_VALID         (1<<2)

/* Values for interrupt_flags above */

#define ACPI_TCPA_INTERRUPT_MODE        (1)
#define ACPI_TCPA_INTERRUPT_POLARITY    (1<<1)
#define ACPI_TCPA_SCI_VIA_GPE           (1<<2)
#define ACPI_TCPA_GLOBAL_INTERRUPT      (1<<3)

/*******************************************************************************
 *
 * TPM2 - Trusted Platform Module (TPM) 2.0 Hardware Interface Table
 *        Version 4
 *
 * Conforms to "TCG ACPI Specification, Family 1.2 and 2.0",
 * December 19, 2014
 *
 ******************************************************************************/

struct acpi_table_tpm2 {
	struct acpi_table_header header;	/* Common ACPI table header */
	u16 platform_class;
	u16 reserved;
	u64 control_address;
	u32 start_method;

	/* Platform-specific data follows */
};

/* Values for start_method above */

#define ACPI_TPM2_NOT_ALLOWED                       0
#define ACPI_TPM2_START_METHOD                      2
#define ACPI_TPM2_MEMORY_MAPPED                     6
#define ACPI_TPM2_COMMAND_BUFFER                    7
#define ACPI_TPM2_COMMAND_BUFFER_WITH_START_METHOD  8

/*******************************************************************************
 *
 * UEFI - UEFI Boot optimization Table
 *        Version 1
 *
 * Conforms to "Unified Extensible Firmware Interface Specification",
 * Version 2.3, May 8, 2009
 *
 ******************************************************************************/

struct acpi_table_uefi {
	struct acpi_table_header header;	/* Common ACPI table header */
	u8 identifier[16];	/* UUID identifier */
	u16 data_offset;	/* Offset of remaining data in table */
};

/*******************************************************************************
 *
 * VRTC - Virtual Real Time Clock Table
 *        Version 1
 *
 * Conforms to "Simple Firmware Interface Specification",
 * Draft 0.8.2, Oct 19, 2010
 * NOTE: The ACPI VRTC is equivalent to The SFI MRTC table.
 *
 ******************************************************************************/

struct acpi_table_vrtc {
	struct acpi_table_header header;	/* Common ACPI table header */
};

/* VRTC entry */

struct acpi_vrtc_entry {
	struct acpi_generic_address physical_address;
	u32 irq;
};

/*******************************************************************************
 *
 * WAET - Windows ACPI Emulated devices Table
 *        Version 1
 *
 * Conforms to "Windows ACPI Emulated Devices Table", version 1.0, April 6, 2009
 *
 ******************************************************************************/

struct acpi_table_waet {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 flags;
};

/* Masks for Flags field above */

#define ACPI_WAET_RTC_NO_ACK        (1)	/* RTC requires no int acknowledge */
#define ACPI_WAET_TIMER_ONE_READ    (1<<1)	/* PM timer requires only one read */

/*******************************************************************************
 *
 * WDAT - Watchdog Action Table
 *        Version 1
 *
 * Conforms to "Hardware Watchdog Timers Design Specification",
 * Copyright 2006 Microsoft Corporation.
 *
 ******************************************************************************/

struct acpi_table_wdat {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 header_length;	/* Watchdog Header Length */
	u16 pci_segment;	/* PCI Segment number */
	u8 pci_bus;		/* PCI Bus number */
	u8 pci_device;		/* PCI Device number */
	u8 pci_function;	/* PCI Function number */
	u8 reserved[3];
	u32 timer_period;	/* Period of one timer count (msec) */
	u32 max_count;		/* Maximum counter value supported */
	u32 min_count;		/* Minimum counter value */
	u8 flags;
	u8 reserved2[3];
	u32 entries;		/* Number of watchdog entries that follow */
};

/* Masks for Flags field above */

#define ACPI_WDAT_ENABLED           (1)
#define ACPI_WDAT_STOPPED           0x80

/* WDAT Instruction Entries (actions) */

struct acpi_wdat_entry {
	u8 action;
	u8 instruction;
	u16 reserved;
	struct acpi_generic_address register_region;
	u32 value;		/* Value used with Read/Write register */
	u32 mask;		/* Bitmask required for this register instruction */
};

/* Values for Action field above */

enum acpi_wdat_actions {
	ACPI_WDAT_RESET = 1,
	ACPI_WDAT_GET_CURRENT_COUNTDOWN = 4,
	ACPI_WDAT_GET_COUNTDOWN = 5,
	ACPI_WDAT_SET_COUNTDOWN = 6,
	ACPI_WDAT_GET_RUNNING_STATE = 8,
	ACPI_WDAT_SET_RUNNING_STATE = 9,
	ACPI_WDAT_GET_STOPPED_STATE = 10,
	ACPI_WDAT_SET_STOPPED_STATE = 11,
	ACPI_WDAT_GET_REBOOT = 16,
	ACPI_WDAT_SET_REBOOT = 17,
	ACPI_WDAT_GET_SHUTDOWN = 18,
	ACPI_WDAT_SET_SHUTDOWN = 19,
	ACPI_WDAT_GET_STATUS = 32,
	ACPI_WDAT_SET_STATUS = 33,
	ACPI_WDAT_ACTION_RESERVED = 34	/* 34 and greater are reserved */
};

/* Values for Instruction field above */

enum acpi_wdat_instructions {
	ACPI_WDAT_READ_VALUE = 0,
	ACPI_WDAT_READ_COUNTDOWN = 1,
	ACPI_WDAT_WRITE_VALUE = 2,
	ACPI_WDAT_WRITE_COUNTDOWN = 3,
	ACPI_WDAT_INSTRUCTION_RESERVED = 4,	/* 4 and greater are reserved */
	ACPI_WDAT_PRESERVE_REGISTER = 0x80	/* Except for this value */
};

/*******************************************************************************
 *
 * WDDT - Watchdog Descriptor Table
 *        Version 1
 *
 * Conforms to "Using the Intel ICH Family Watchdog Timer (WDT)",
 * Version 001, September 2002
 *
 ******************************************************************************/

struct acpi_table_wddt {
	struct acpi_table_header header;	/* Common ACPI table header */
	u16 spec_version;
	u16 table_version;
	u16 pci_vendor_id;
	struct acpi_generic_address address;
	u16 max_count;		/* Maximum counter value supported */
	u16 min_count;		/* Minimum counter value supported */
	u16 period;
	u16 status;
	u16 capability;
};

/* Flags for Status field above */

#define ACPI_WDDT_AVAILABLE     (1)
#define ACPI_WDDT_ACTIVE        (1<<1)
#define ACPI_WDDT_TCO_OS_OWNED  (1<<2)
#define ACPI_WDDT_USER_RESET    (1<<11)
#define ACPI_WDDT_WDT_RESET     (1<<12)
#define ACPI_WDDT_POWER_FAIL    (1<<13)
#define ACPI_WDDT_UNKNOWN_RESET (1<<14)

/* Flags for Capability field above */

#define ACPI_WDDT_AUTO_RESET    (1)
#define ACPI_WDDT_ALERT_SUPPORT (1<<1)

/*******************************************************************************
 *
 * WDRT - Watchdog Resource Table
 *        Version 1
 *
 * Conforms to "Watchdog Timer Hardware Requirements for Windows Server 2003",
 * Version 1.01, August 28, 2006
 *
 ******************************************************************************/

struct acpi_table_wdrt {
	struct acpi_table_header header;	/* Common ACPI table header */
	struct acpi_generic_address control_register;
	struct acpi_generic_address count_register;
	u16 pci_device_id;
	u16 pci_vendor_id;
	u8 pci_bus;		/* PCI Bus number */
	u8 pci_device;		/* PCI Device number */
	u8 pci_function;	/* PCI Function number */
	u8 pci_segment;		/* PCI Segment number */
	u16 max_count;		/* Maximum counter value supported */
	u8 units;
};

/* Reset to default packing */

#pragma pack()

#endif				/* __ACTBL2_H__ */
