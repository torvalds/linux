/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/******************************************************************************
 *
 * Name: actbl2.h - ACPI Table Definitions (tables not in ACPI spec)
 *
 * Copyright (C) 2000 - 2023, Intel Corp.
 *
 *****************************************************************************/

#ifndef __ACTBL2_H__
#define __ACTBL2_H__

/*******************************************************************************
 *
 * Additional ACPI Tables (2)
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
#define ACPI_SIG_AGDI           "AGDI"	/* Arm Generic Diagnostic Dump and Reset Device Interface */
#define ACPI_SIG_APMT           "APMT"	/* Arm Performance Monitoring Unit table */
#define ACPI_SIG_BDAT           "BDAT"	/* BIOS Data ACPI Table */
#define ACPI_SIG_CCEL           "CCEL"	/* CC Event Log Table */
#define ACPI_SIG_CDAT           "CDAT"	/* Coherent Device Attribute Table */
#define ACPI_SIG_IORT           "IORT"	/* IO Remapping Table */
#define ACPI_SIG_IVRS           "IVRS"	/* I/O Virtualization Reporting Structure */
#define ACPI_SIG_LPIT           "LPIT"	/* Low Power Idle Table */
#define ACPI_SIG_MADT           "APIC"	/* Multiple APIC Description Table */
#define ACPI_SIG_MCFG           "MCFG"	/* PCI Memory Mapped Configuration table */
#define ACPI_SIG_MCHI           "MCHI"	/* Management Controller Host Interface table */
#define ACPI_SIG_MPAM           "MPAM"	/* Memory System Resource Partitioning and Monitoring Table */
#define ACPI_SIG_MPST           "MPST"	/* Memory Power State Table */
#define ACPI_SIG_MSDM           "MSDM"	/* Microsoft Data Management Table */
#define ACPI_SIG_NFIT           "NFIT"	/* NVDIMM Firmware Interface Table */
#define ACPI_SIG_NHLT           "NHLT"	/* Non HD Audio Link Table */
#define ACPI_SIG_PCCT           "PCCT"	/* Platform Communications Channel Table */
#define ACPI_SIG_PDTT           "PDTT"	/* Platform Debug Trigger Table */
#define ACPI_SIG_PHAT           "PHAT"	/* Platform Health Assessment Table */
#define ACPI_SIG_PMTT           "PMTT"	/* Platform Memory Topology Table */
#define ACPI_SIG_PPTT           "PPTT"	/* Processor Properties Topology Table */
#define ACPI_SIG_PRMT           "PRMT"	/* Platform Runtime Mechanism Table */
#define ACPI_SIG_RASF           "RASF"	/* RAS Feature table */
#define ACPI_SIG_RAS2           "RAS2"	/* RAS2 Feature table */
#define ACPI_SIG_RGRT           "RGRT"	/* Regulatory Graphics Resource Table */
#define ACPI_SIG_RHCT           "RHCT"	/* RISC-V Hart Capabilities Table */
#define ACPI_SIG_SBST           "SBST"	/* Smart Battery Specification Table */
#define ACPI_SIG_SDEI           "SDEI"	/* Software Delegated Exception Interface Table */
#define ACPI_SIG_SDEV           "SDEV"	/* Secure Devices table */
#define ACPI_SIG_SVKL           "SVKL"	/* Storage Volume Key Location Table */
#define ACPI_SIG_TDEL           "TDEL"	/* TD Event Log Table */

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
 * AEST - Arm Error Source Table
 *
 * Conforms to: ACPI for the Armv8 RAS Extensions 1.1(Sep 2020) and
 * 2.0(May 2023) Platform Design Document.
 *
 ******************************************************************************/

struct acpi_table_aest {
	struct acpi_table_header header;
};

/* Common Subtable header - one per Node Structure (Subtable) */

struct acpi_aest_hdr {
	u8 type;
	u16 length;
	u8 reserved;
	u32 node_specific_offset;
	u32 node_interface_offset;
	u32 node_interrupt_offset;
	u32 node_interrupt_count;
	u64 timestamp_rate;
	u64 reserved1;
	u64 error_injection_rate;
};

/* Values for Type above */

#define ACPI_AEST_PROCESSOR_ERROR_NODE      0
#define ACPI_AEST_MEMORY_ERROR_NODE         1
#define ACPI_AEST_SMMU_ERROR_NODE           2
#define ACPI_AEST_VENDOR_ERROR_NODE         3
#define ACPI_AEST_GIC_ERROR_NODE            4
#define ACPI_AEST_PCIE_ERROR_NODE           5
#define ACPI_AEST_PROXY_ERROR_NODE          6
#define ACPI_AEST_NODE_TYPE_RESERVED        7 /* 7 and above are reserved */

/*
 * AEST subtables (Error nodes)
 */

/* 0: Processor Error */

typedef struct acpi_aest_processor {
	u32 processor_id;
	u8 resource_type;
	u8 reserved;
	u8 flags;
	u8 revision;
	u64 processor_affinity;

} acpi_aest_processor;

/* Values for resource_type above, related structs below */

#define ACPI_AEST_CACHE_RESOURCE            0
#define ACPI_AEST_TLB_RESOURCE              1
#define ACPI_AEST_GENERIC_RESOURCE          2
#define ACPI_AEST_RESOURCE_RESERVED         3	/* 3 and above are reserved */

/* 0R: Processor Cache Resource Substructure */

typedef struct acpi_aest_processor_cache {
	u32 cache_reference;
	u32 reserved;

} acpi_aest_processor_cache;

/* Values for cache_type above */

#define ACPI_AEST_CACHE_DATA                0
#define ACPI_AEST_CACHE_INSTRUCTION         1
#define ACPI_AEST_CACHE_UNIFIED             2
#define ACPI_AEST_CACHE_RESERVED            3	/* 3 and above are reserved */

/* 1R: Processor TLB Resource Substructure */

typedef struct acpi_aest_processor_tlb {
	u32 tlb_level;
	u32 reserved;

} acpi_aest_processor_tlb;

/* 2R: Processor Generic Resource Substructure */

typedef struct acpi_aest_processor_generic {
	u32 resource;

} acpi_aest_processor_generic;

/* 1: Memory Error */

typedef struct acpi_aest_memory {
	u32 srat_proximity_domain;

} acpi_aest_memory;

/* 2: Smmu Error */

typedef struct acpi_aest_smmu {
	u32 iort_node_reference;
	u32 subcomponent_reference;

} acpi_aest_smmu;

/* 3: Vendor Defined */

typedef struct acpi_aest_vendor {
	u32 acpi_hid;
	u32 acpi_uid;
	u8 vendor_specific_data[16];

} acpi_aest_vendor;

struct acpi_aest_vendor_v2 {
	char acpi_hid[8];
	u32 acpi_uid;
	u8 vendor_specific_data[16];
};

/* 4: Gic Error */

typedef struct acpi_aest_gic {
	u32 interface_type;
	u32 instance_id;

} acpi_aest_gic;

/* Values for interface_type above */

#define ACPI_AEST_GIC_CPU                   0
#define ACPI_AEST_GIC_DISTRIBUTOR           1
#define ACPI_AEST_GIC_REDISTRIBUTOR         2
#define ACPI_AEST_GIC_ITS                   3
#define ACPI_AEST_GIC_RESERVED              4	/* 4 and above are reserved */

/* 5: PCIe Error */

struct acpi_aest_pcie {
	u32 iort_node_reference;
};

/* 6: Proxy Error */

struct acpi_aest_proxy {
	u64 node_address;
};

/* Node Interface Structure */

typedef struct acpi_aest_node_interface {
	u8 type;
	u8 reserved[3];
	u32 flags;
	u64 address;
	u32 error_record_index;
	u32 error_record_count;
	u64 error_record_implemented;
	u64 error_status_reporting;
	u64 addressing_mode;

} acpi_aest_node_interface;

/* Node Interface Structure V2 */

struct acpi_aest_node_interface_header {
	u8 type;
	u8 group_format;
	u8 reserved[2];
	u32 flags;
	u64 address;
	u32 error_record_index;
	u32 error_record_count;
};

#define ACPI_AEST_NODE_GROUP_FORMAT_4K          0
#define ACPI_AEST_NODE_GROUP_FORMAT_16K         1
#define ACPI_AEST_NODE_GROUP_FORMAT_64K         2

struct acpi_aest_node_interface_common {
	u32 error_node_device;
	u32 processor_affinity;
	u64 error_group_register_base;
	u64 fault_inject_register_base;
	u64 interrupt_config_register_base;
};

struct acpi_aest_node_interface_4k {
	u64 error_record_implemented;
	u64 error_status_reporting;
	u64 addressing_mode;
	struct acpi_aest_node_interface_common common;
};

struct acpi_aest_node_interface_16k {
	u64 error_record_implemented[4];
	u64 error_status_reporting[4];
	u64 addressing_mode[4];
	struct acpi_aest_node_interface_common common;
};

struct acpi_aest_node_interface_64k {
	u64 error_record_implemented[14];
	u64 error_status_reporting[14];
	u64 addressing_mode[14];
	struct acpi_aest_node_interface_common common;
};

/* Values for Type field above */

#define ACPI_AEST_NODE_SYSTEM_REGISTER			0
#define ACPI_AEST_NODE_MEMORY_MAPPED			1
#define ACPI_AEST_NODE_SINGLE_RECORD_MEMORY_MAPPED	2
#define ACPI_AEST_XFACE_RESERVED			3   /* 2 and above are reserved */

/* Node Interrupt Structure */

typedef struct acpi_aest_node_interrupt {
	u8 type;
	u8 reserved[2];
	u8 flags;
	u32 gsiv;
	u8 iort_id;
	u8 reserved1[3];

} acpi_aest_node_interrupt;

/* Node Interrupt Structure V2 */

struct acpi_aest_node_interrupt_v2 {
	u8 type;
	u8 reserved[2];
	u8 flags;
	u32 gsiv;
	u8 reserved1[4];
};

/* Values for Type field above */

#define ACPI_AEST_NODE_FAULT_HANDLING       0
#define ACPI_AEST_NODE_ERROR_RECOVERY       1
#define ACPI_AEST_XRUPT_RESERVED            2	/* 2 and above are reserved */

/*******************************************************************************
 * AGDI - Arm Generic Diagnostic Dump and Reset Device Interface
 *
 * Conforms to "ACPI for Arm Components 1.1, Platform Design Document"
 * ARM DEN0093 v1.1
 *
 ******************************************************************************/
struct acpi_table_agdi {
	struct acpi_table_header header;	/* Common ACPI table header */
	u8 flags;
	u8 reserved[3];
	u32 sdei_event;
	u32 gsiv;
};

/* Mask for Flags field above */

#define ACPI_AGDI_SIGNALING_MODE (1)

/*******************************************************************************
 *
 * APMT - ARM Performance Monitoring Unit Table
 *
 * Conforms to:
 * ARM Performance Monitoring Unit Architecture 1.0 Platform Design Document
 * ARM DEN0117 v1.0 November 25, 2021
 *
 ******************************************************************************/

struct acpi_table_apmt {
	struct acpi_table_header header;	/* Common ACPI table header */
};

#define ACPI_APMT_NODE_ID_LENGTH                4

/*
 * APMT subtables
 */
struct acpi_apmt_node {
	u16 length;
	u8 flags;
	u8 type;
	u32 id;
	u64 inst_primary;
	u32 inst_secondary;
	u64 base_address0;
	u64 base_address1;
	u32 ovflw_irq;
	u32 reserved;
	u32 ovflw_irq_flags;
	u32 proc_affinity;
	u32 impl_id;
};

/* Masks for Flags field above */

#define ACPI_APMT_FLAGS_DUAL_PAGE               (1<<0)
#define ACPI_APMT_FLAGS_AFFINITY                (1<<1)
#define ACPI_APMT_FLAGS_ATOMIC                  (1<<2)

/* Values for Flags dual page field above */

#define ACPI_APMT_FLAGS_DUAL_PAGE_NSUPP         (0<<0)
#define ACPI_APMT_FLAGS_DUAL_PAGE_SUPP          (1<<0)

/* Values for Flags processor affinity field above */
#define ACPI_APMT_FLAGS_AFFINITY_PROC           (0<<1)
#define ACPI_APMT_FLAGS_AFFINITY_PROC_CONTAINER (1<<1)

/* Values for Flags 64-bit atomic field above */
#define ACPI_APMT_FLAGS_ATOMIC_NSUPP            (0<<2)
#define ACPI_APMT_FLAGS_ATOMIC_SUPP             (1<<2)

/* Values for Type field above */

enum acpi_apmt_node_type {
	ACPI_APMT_NODE_TYPE_MC = 0x00,
	ACPI_APMT_NODE_TYPE_SMMU = 0x01,
	ACPI_APMT_NODE_TYPE_PCIE_ROOT = 0x02,
	ACPI_APMT_NODE_TYPE_ACPI = 0x03,
	ACPI_APMT_NODE_TYPE_CACHE = 0x04,
	ACPI_APMT_NODE_TYPE_COUNT
};

/* Masks for ovflw_irq_flags field above */

#define ACPI_APMT_OVFLW_IRQ_FLAGS_MODE          (1<<0)
#define ACPI_APMT_OVFLW_IRQ_FLAGS_TYPE          (1<<1)

/* Values for ovflw_irq_flags mode field above */

#define ACPI_APMT_OVFLW_IRQ_FLAGS_MODE_LEVEL    (0<<0)
#define ACPI_APMT_OVFLW_IRQ_FLAGS_MODE_EDGE     (1<<0)

/* Values for ovflw_irq_flags type field above */

#define ACPI_APMT_OVFLW_IRQ_FLAGS_TYPE_WIRED    (0<<1)

/*******************************************************************************
 *
 * BDAT - BIOS Data ACPI Table
 *
 * Conforms to "BIOS Data ACPI Table", Interface Specification v4.0 Draft 5
 * Nov 2020
 *
 ******************************************************************************/

struct acpi_table_bdat {
	struct acpi_table_header header;
	struct acpi_generic_address gas;
};

/*******************************************************************************
 *
 * CCEL - CC-Event Log
 *        From: "Guest-Host-Communication Interface (GHCI) for Intel
 *        Trust Domain Extensions (Intel TDX)". Feb 2022
 *
 ******************************************************************************/

struct acpi_table_ccel {
	struct acpi_table_header header;	/* Common ACPI table header */
	u8 CCtype;
	u8 Ccsub_type;
	u16 reserved;
	u64 log_area_minimum_length;
	u64 log_area_start_address;
};

/*******************************************************************************
 *
 * IORT - IO Remapping Table
 *
 * Conforms to "IO Remapping Table System Software on ARM Platforms",
 * Document number: ARM DEN 0049E.e, Sep 2022
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
	u32 identifier;
	u32 mapping_count;
	u32 mapping_offset;
	char node_data[];
};

/* Values for subtable Type above */

enum acpi_iort_node_type {
	ACPI_IORT_NODE_ITS_GROUP = 0x00,
	ACPI_IORT_NODE_NAMED_COMPONENT = 0x01,
	ACPI_IORT_NODE_PCI_ROOT_COMPLEX = 0x02,
	ACPI_IORT_NODE_SMMU = 0x03,
	ACPI_IORT_NODE_SMMU_V3 = 0x04,
	ACPI_IORT_NODE_PMCG = 0x05,
	ACPI_IORT_NODE_RMR = 0x06,
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
	u32 identifiers[];	/* GIC ITS identifier array */
};

struct acpi_iort_named_component {
	u32 node_flags;
	u64 memory_properties;	/* Memory access properties */
	u8 memory_address_limit;	/* Memory address size limit */
	char device_name[];	/* Path of namespace object */
};

/* Masks for Flags field above */

#define ACPI_IORT_NC_STALL_SUPPORTED    (1)
#define ACPI_IORT_NC_PASID_BITS         (31<<1)

struct acpi_iort_root_complex {
	u64 memory_properties;	/* Memory access properties */
	u32 ats_attribute;
	u32 pci_segment_number;
	u8 memory_address_limit;	/* Memory address size limit */
	u16 pasid_capabilities;	/* PASID Capabilities */
	u8 reserved[];		/* Reserved, must be zero */
};

/* Masks for ats_attribute field above */

#define ACPI_IORT_ATS_SUPPORTED         (1)	/* The root complex ATS support */
#define ACPI_IORT_PRI_SUPPORTED         (1<<1)	/* The root complex PRI support */
#define ACPI_IORT_PASID_FWD_SUPPORTED   (1<<2)	/* The root complex PASID forward support */

/* Masks for pasid_capabilities field above */
#define ACPI_IORT_PASID_MAX_WIDTH       (0x1F)	/* Bits 0-4 */

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
	u64 interrupts[];	/* Interrupt array */
};

/* Values for Model field above */

#define ACPI_IORT_SMMU_V1               0x00000000	/* Generic SMMUv1 */
#define ACPI_IORT_SMMU_V2               0x00000001	/* Generic SMMUv2 */
#define ACPI_IORT_SMMU_CORELINK_MMU400  0x00000002	/* ARM Corelink MMU-400 */
#define ACPI_IORT_SMMU_CORELINK_MMU500  0x00000003	/* ARM Corelink MMU-500 */
#define ACPI_IORT_SMMU_CORELINK_MMU401  0x00000004	/* ARM Corelink MMU-401 */
#define ACPI_IORT_SMMU_CAVIUM_THUNDERX  0x00000005	/* Cavium thunder_x SMMUv2 */

/* Masks for Flags field above */

#define ACPI_IORT_SMMU_DVM_SUPPORTED    (1)
#define ACPI_IORT_SMMU_COHERENT_WALK    (1<<1)

/* Global interrupt format */

struct acpi_iort_smmu_gsi {
	u32 nsg_irpt;
	u32 nsg_irpt_flags;
	u32 nsg_cfg_irpt;
	u32 nsg_cfg_irpt_flags;
};

struct acpi_iort_smmu_v3 {
	u64 base_address;	/* SMMUv3 base address */
	u32 flags;
	u32 reserved;
	u64 vatos_address;
	u32 model;
	u32 event_gsiv;
	u32 pri_gsiv;
	u32 gerr_gsiv;
	u32 sync_gsiv;
	u32 pxm;
	u32 id_mapping_index;
};

/* Values for Model field above */

#define ACPI_IORT_SMMU_V3_GENERIC           0x00000000	/* Generic SMMUv3 */
#define ACPI_IORT_SMMU_V3_HISILICON_HI161X  0x00000001	/* hi_silicon Hi161x SMMUv3 */
#define ACPI_IORT_SMMU_V3_CAVIUM_CN99XX     0x00000002	/* Cavium CN99xx SMMUv3 */

/* Masks for Flags field above */

#define ACPI_IORT_SMMU_V3_COHACC_OVERRIDE   (1)
#define ACPI_IORT_SMMU_V3_HTTU_OVERRIDE     (3<<1)
#define ACPI_IORT_SMMU_V3_PXM_VALID         (1<<3)
#define ACPI_IORT_SMMU_V3_DEVICEID_VALID    (1<<4)

struct acpi_iort_pmcg {
	u64 page0_base_address;
	u32 overflow_gsiv;
	u32 node_reference;
	u64 page1_base_address;
};

struct acpi_iort_rmr {
	u32 flags;
	u32 rmr_count;
	u32 rmr_offset;
};

/* Masks for Flags field above */
#define ACPI_IORT_RMR_REMAP_PERMITTED      (1)
#define ACPI_IORT_RMR_ACCESS_PRIVILEGE     (1<<1)

/*
 * Macro to access the Access Attributes in flags field above:
 *  Access Attributes is encoded in bits 9:2
 */
#define ACPI_IORT_RMR_ACCESS_ATTRIBUTES(flags)          (((flags) >> 2) & 0xFF)

/* Values for above Access Attributes */

#define ACPI_IORT_RMR_ATTR_DEVICE_NGNRNE   0x00
#define ACPI_IORT_RMR_ATTR_DEVICE_NGNRE    0x01
#define ACPI_IORT_RMR_ATTR_DEVICE_NGRE     0x02
#define ACPI_IORT_RMR_ATTR_DEVICE_GRE      0x03
#define ACPI_IORT_RMR_ATTR_NORMAL_NC       0x04
#define ACPI_IORT_RMR_ATTR_NORMAL_IWB_OWB  0x05

struct acpi_iort_rmr_desc {
	u64 base_address;
	u64 length;
	u32 reserved;
};

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
	ACPI_IVRS_TYPE_HARDWARE1 = 0x10,
	ACPI_IVRS_TYPE_HARDWARE2 = 0x11,
	ACPI_IVRS_TYPE_HARDWARE3 = 0x40,
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

struct acpi_ivrs_hardware_10 {
	struct acpi_ivrs_header header;
	u16 capability_offset;	/* Offset for IOMMU control fields */
	u64 base_address;	/* IOMMU control registers */
	u16 pci_segment_group;
	u16 info;		/* MSI number and unit ID */
	u32 feature_reporting;
};

/* 0x11: I/O Virtualization Hardware Definition Block (IVHD) */

struct acpi_ivrs_hardware_11 {
	struct acpi_ivrs_header header;
	u16 capability_offset;	/* Offset for IOMMU control fields */
	u64 base_address;	/* IOMMU control registers */
	u16 pci_segment_group;
	u16 info;		/* MSI number and unit ID */
	u32 attributes;
	u64 efr_register_image;
	u64 reserved;
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
	ACPI_IVRS_TYPE_SPECIAL = 72,	/* Uses struct acpi_ivrs_device8c */

	/* Variable-length device entries */

	ACPI_IVRS_TYPE_HID = 240	/* Uses ACPI_IVRS_DEVICE_HID */
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

/* Type 240: variable-length device entry */

struct acpi_ivrs_device_hid {
	struct acpi_ivrs_de_header header;
	u64 acpi_hid;
	u64 acpi_cid;
	u8 uid_type;
	u8 uid_length;
};

/* Values for uid_type above */

#define ACPI_IVRS_UID_NOT_PRESENT   0
#define ACPI_IVRS_UID_IS_INTEGER    1
#define ACPI_IVRS_UID_IS_STRING     2

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
 * MADT - Multiple APIC Description Table
 *        Version 3
 *
 ******************************************************************************/

struct acpi_table_madt {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 address;		/* Physical address of local APIC */
	u32 flags;
};

/* Masks for Flags field above */

#define ACPI_MADT_PCAT_COMPAT       (1)	/* 00: System also has dual 8259s */

/* Values for PCATCompat flag */

#define ACPI_MADT_DUAL_PIC          1
#define ACPI_MADT_MULTIPLE_APIC     0

/* Values for MADT subtable type in struct acpi_subtable_header */

enum acpi_madt_type {
	ACPI_MADT_TYPE_LOCAL_APIC = 0,
	ACPI_MADT_TYPE_IO_APIC = 1,
	ACPI_MADT_TYPE_INTERRUPT_OVERRIDE = 2,
	ACPI_MADT_TYPE_NMI_SOURCE = 3,
	ACPI_MADT_TYPE_LOCAL_APIC_NMI = 4,
	ACPI_MADT_TYPE_LOCAL_APIC_OVERRIDE = 5,
	ACPI_MADT_TYPE_IO_SAPIC = 6,
	ACPI_MADT_TYPE_LOCAL_SAPIC = 7,
	ACPI_MADT_TYPE_INTERRUPT_SOURCE = 8,
	ACPI_MADT_TYPE_LOCAL_X2APIC = 9,
	ACPI_MADT_TYPE_LOCAL_X2APIC_NMI = 10,
	ACPI_MADT_TYPE_GENERIC_INTERRUPT = 11,
	ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR = 12,
	ACPI_MADT_TYPE_GENERIC_MSI_FRAME = 13,
	ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR = 14,
	ACPI_MADT_TYPE_GENERIC_TRANSLATOR = 15,
	ACPI_MADT_TYPE_MULTIPROC_WAKEUP = 16,
	ACPI_MADT_TYPE_CORE_PIC = 17,
	ACPI_MADT_TYPE_LIO_PIC = 18,
	ACPI_MADT_TYPE_HT_PIC = 19,
	ACPI_MADT_TYPE_EIO_PIC = 20,
	ACPI_MADT_TYPE_MSI_PIC = 21,
	ACPI_MADT_TYPE_BIO_PIC = 22,
	ACPI_MADT_TYPE_LPC_PIC = 23,
	ACPI_MADT_TYPE_RINTC = 24,
	ACPI_MADT_TYPE_IMSIC = 25,
	ACPI_MADT_TYPE_APLIC = 26,
	ACPI_MADT_TYPE_PLIC = 27,
	ACPI_MADT_TYPE_RESERVED = 28,	/* 28 to 0x7F are reserved */
	ACPI_MADT_TYPE_OEM_RESERVED = 0x80	/* 0x80 to 0xFF are reserved for OEM use */
};

/*
 * MADT Subtables, correspond to Type in struct acpi_subtable_header
 */

/* 0: Processor Local APIC */

struct acpi_madt_local_apic {
	struct acpi_subtable_header header;
	u8 processor_id;	/* ACPI processor id */
	u8 id;			/* Processor's local APIC id */
	u32 lapic_flags;
};

/* 1: IO APIC */

struct acpi_madt_io_apic {
	struct acpi_subtable_header header;
	u8 id;			/* I/O APIC ID */
	u8 reserved;		/* reserved - must be zero */
	u32 address;		/* APIC physical address */
	u32 global_irq_base;	/* Global system interrupt where INTI lines start */
};

/* 2: Interrupt Override */

struct acpi_madt_interrupt_override {
	struct acpi_subtable_header header;
	u8 bus;			/* 0 - ISA */
	u8 source_irq;		/* Interrupt source (IRQ) */
	u32 global_irq;		/* Global system interrupt */
	u16 inti_flags;
};

/* 3: NMI Source */

struct acpi_madt_nmi_source {
	struct acpi_subtable_header header;
	u16 inti_flags;
	u32 global_irq;		/* Global system interrupt */
};

/* 4: Local APIC NMI */

struct acpi_madt_local_apic_nmi {
	struct acpi_subtable_header header;
	u8 processor_id;	/* ACPI processor id */
	u16 inti_flags;
	u8 lint;		/* LINTn to which NMI is connected */
};

/* 5: Address Override */

struct acpi_madt_local_apic_override {
	struct acpi_subtable_header header;
	u16 reserved;		/* Reserved, must be zero */
	u64 address;		/* APIC physical address */
};

/* 6: I/O Sapic */

struct acpi_madt_io_sapic {
	struct acpi_subtable_header header;
	u8 id;			/* I/O SAPIC ID */
	u8 reserved;		/* Reserved, must be zero */
	u32 global_irq_base;	/* Global interrupt for SAPIC start */
	u64 address;		/* SAPIC physical address */
};

/* 7: Local Sapic */

struct acpi_madt_local_sapic {
	struct acpi_subtable_header header;
	u8 processor_id;	/* ACPI processor id */
	u8 id;			/* SAPIC ID */
	u8 eid;			/* SAPIC EID */
	u8 reserved[3];		/* Reserved, must be zero */
	u32 lapic_flags;
	u32 uid;		/* Numeric UID - ACPI 3.0 */
	char uid_string[];	/* String UID  - ACPI 3.0 */
};

/* 8: Platform Interrupt Source */

struct acpi_madt_interrupt_source {
	struct acpi_subtable_header header;
	u16 inti_flags;
	u8 type;		/* 1=PMI, 2=INIT, 3=corrected */
	u8 id;			/* Processor ID */
	u8 eid;			/* Processor EID */
	u8 io_sapic_vector;	/* Vector value for PMI interrupts */
	u32 global_irq;		/* Global system interrupt */
	u32 flags;		/* Interrupt Source Flags */
};

/* Masks for Flags field above */

#define ACPI_MADT_CPEI_OVERRIDE     (1)

/* 9: Processor Local X2APIC (ACPI 4.0) */

struct acpi_madt_local_x2apic {
	struct acpi_subtable_header header;
	u16 reserved;		/* reserved - must be zero */
	u32 local_apic_id;	/* Processor x2APIC ID  */
	u32 lapic_flags;
	u32 uid;		/* ACPI processor UID */
};

/* 10: Local X2APIC NMI (ACPI 4.0) */

struct acpi_madt_local_x2apic_nmi {
	struct acpi_subtable_header header;
	u16 inti_flags;
	u32 uid;		/* ACPI processor UID */
	u8 lint;		/* LINTn to which NMI is connected */
	u8 reserved[3];		/* reserved - must be zero */
};

/* 11: Generic interrupt - GICC (ACPI 5.0 + ACPI 6.0 + ACPI 6.3 + ACPI 6.5 changes) */

struct acpi_madt_generic_interrupt {
	struct acpi_subtable_header header;
	u16 reserved;		/* reserved - must be zero */
	u32 cpu_interface_number;
	u32 uid;
	u32 flags;
	u32 parking_version;
	u32 performance_interrupt;
	u64 parked_address;
	u64 base_address;
	u64 gicv_base_address;
	u64 gich_base_address;
	u32 vgic_interrupt;
	u64 gicr_base_address;
	u64 arm_mpidr;
	u8 efficiency_class;
	u8 reserved2[1];
	u16 spe_interrupt;	/* ACPI 6.3 */
	u16 trbe_interrupt;	/* ACPI 6.5 */
};

/* Masks for Flags field above */

/* ACPI_MADT_ENABLED                    (1)      Processor is usable if set */
#define ACPI_MADT_PERFORMANCE_IRQ_MODE  (1<<1)	/* 01: Performance Interrupt Mode */
#define ACPI_MADT_VGIC_IRQ_MODE         (1<<2)	/* 02: VGIC Maintenance Interrupt mode */
#define ACPI_MADT_GICC_ONLINE_CAPABLE   (1<<3)	/* 03: Processor is online capable  */
#define ACPI_MADT_GICC_NON_COHERENT     (1<<4)	/* 04: GIC redistributor is not coherent */

/* 12: Generic Distributor (ACPI 5.0 + ACPI 6.0 changes) */

struct acpi_madt_generic_distributor {
	struct acpi_subtable_header header;
	u16 reserved;		/* reserved - must be zero */
	u32 gic_id;
	u64 base_address;
	u32 global_irq_base;
	u8 version;
	u8 reserved2[3];	/* reserved - must be zero */
};

/* Values for Version field above */

enum acpi_madt_gic_version {
	ACPI_MADT_GIC_VERSION_NONE = 0,
	ACPI_MADT_GIC_VERSION_V1 = 1,
	ACPI_MADT_GIC_VERSION_V2 = 2,
	ACPI_MADT_GIC_VERSION_V3 = 3,
	ACPI_MADT_GIC_VERSION_V4 = 4,
	ACPI_MADT_GIC_VERSION_RESERVED = 5	/* 5 and greater are reserved */
};

/* 13: Generic MSI Frame (ACPI 5.1) */

struct acpi_madt_generic_msi_frame {
	struct acpi_subtable_header header;
	u16 reserved;		/* reserved - must be zero */
	u32 msi_frame_id;
	u64 base_address;
	u32 flags;
	u16 spi_count;
	u16 spi_base;
};

/* Masks for Flags field above */

#define ACPI_MADT_OVERRIDE_SPI_VALUES   (1)

/* 14: Generic Redistributor (ACPI 5.1) */

struct acpi_madt_generic_redistributor {
	struct acpi_subtable_header header;
	u8 flags;
	u8 reserved;		/* reserved - must be zero */
	u64 base_address;
	u32 length;
};

#define ACPI_MADT_GICR_NON_COHERENT     (1)

/* 15: Generic Translator (ACPI 6.0) */

struct acpi_madt_generic_translator {
	struct acpi_subtable_header header;
	u8 flags;
	u8 reserved;		/* reserved - must be zero */
	u32 translation_id;
	u64 base_address;
	u32 reserved2;
};

#define ACPI_MADT_ITS_NON_COHERENT      (1)

/* 16: Multiprocessor wakeup (ACPI 6.4) */

struct acpi_madt_multiproc_wakeup {
	struct acpi_subtable_header header;
	u16 version;
	u32 reserved;		/* reserved - must be zero */
	u64 mailbox_address;
	u64 reset_vector;
};

/* Values for Version field above */

enum acpi_madt_multiproc_wakeup_version {
	ACPI_MADT_MP_WAKEUP_VERSION_NONE = 0,
	ACPI_MADT_MP_WAKEUP_VERSION_V1 = 1,
	ACPI_MADT_MP_WAKEUP_VERSION_RESERVED = 2, /* 2 and greater are reserved */
};

#define ACPI_MADT_MP_WAKEUP_SIZE_V0	16
#define ACPI_MADT_MP_WAKEUP_SIZE_V1	24

#define ACPI_MULTIPROC_WAKEUP_MB_OS_SIZE        2032
#define ACPI_MULTIPROC_WAKEUP_MB_FIRMWARE_SIZE  2048

struct acpi_madt_multiproc_wakeup_mailbox {
	u16 command;
	u16 reserved;		/* reserved - must be zero */
	u32 apic_id;
	u64 wakeup_vector;
	u8 reserved_os[ACPI_MULTIPROC_WAKEUP_MB_OS_SIZE];	/* reserved for OS use */
	u8 reserved_firmware[ACPI_MULTIPROC_WAKEUP_MB_FIRMWARE_SIZE];	/* reserved for firmware use */
};

#define ACPI_MP_WAKE_COMMAND_WAKEUP	1
#define ACPI_MP_WAKE_COMMAND_TEST	2

/* 17: CPU Core Interrupt Controller (ACPI 6.5) */

struct acpi_madt_core_pic {
	struct acpi_subtable_header header;
	u8 version;
	u32 processor_id;
	u32 core_id;
	u32 flags;
};

/* Values for Version field above */

enum acpi_madt_core_pic_version {
	ACPI_MADT_CORE_PIC_VERSION_NONE = 0,
	ACPI_MADT_CORE_PIC_VERSION_V1 = 1,
	ACPI_MADT_CORE_PIC_VERSION_RESERVED = 2	/* 2 and greater are reserved */
};

/* 18: Legacy I/O Interrupt Controller (ACPI 6.5) */

struct acpi_madt_lio_pic {
	struct acpi_subtable_header header;
	u8 version;
	u64 address;
	u16 size;
	u8 cascade[2];
	u32 cascade_map[2];
};

/* Values for Version field above */

enum acpi_madt_lio_pic_version {
	ACPI_MADT_LIO_PIC_VERSION_NONE = 0,
	ACPI_MADT_LIO_PIC_VERSION_V1 = 1,
	ACPI_MADT_LIO_PIC_VERSION_RESERVED = 2	/* 2 and greater are reserved */
};

/* 19: HT Interrupt Controller (ACPI 6.5) */

struct acpi_madt_ht_pic {
	struct acpi_subtable_header header;
	u8 version;
	u64 address;
	u16 size;
	u8 cascade[8];
};

/* Values for Version field above */

enum acpi_madt_ht_pic_version {
	ACPI_MADT_HT_PIC_VERSION_NONE = 0,
	ACPI_MADT_HT_PIC_VERSION_V1 = 1,
	ACPI_MADT_HT_PIC_VERSION_RESERVED = 2	/* 2 and greater are reserved */
};

/* 20: Extend I/O Interrupt Controller (ACPI 6.5) */

struct acpi_madt_eio_pic {
	struct acpi_subtable_header header;
	u8 version;
	u8 cascade;
	u8 node;
	u64 node_map;
};

/* Values for Version field above */

enum acpi_madt_eio_pic_version {
	ACPI_MADT_EIO_PIC_VERSION_NONE = 0,
	ACPI_MADT_EIO_PIC_VERSION_V1 = 1,
	ACPI_MADT_EIO_PIC_VERSION_RESERVED = 2	/* 2 and greater are reserved */
};

/* 21: MSI Interrupt Controller (ACPI 6.5) */

struct acpi_madt_msi_pic {
	struct acpi_subtable_header header;
	u8 version;
	u64 msg_address;
	u32 start;
	u32 count;
};

/* Values for Version field above */

enum acpi_madt_msi_pic_version {
	ACPI_MADT_MSI_PIC_VERSION_NONE = 0,
	ACPI_MADT_MSI_PIC_VERSION_V1 = 1,
	ACPI_MADT_MSI_PIC_VERSION_RESERVED = 2	/* 2 and greater are reserved */
};

/* 22: Bridge I/O Interrupt Controller (ACPI 6.5) */

struct acpi_madt_bio_pic {
	struct acpi_subtable_header header;
	u8 version;
	u64 address;
	u16 size;
	u16 id;
	u16 gsi_base;
};

/* Values for Version field above */

enum acpi_madt_bio_pic_version {
	ACPI_MADT_BIO_PIC_VERSION_NONE = 0,
	ACPI_MADT_BIO_PIC_VERSION_V1 = 1,
	ACPI_MADT_BIO_PIC_VERSION_RESERVED = 2	/* 2 and greater are reserved */
};

/* 23: LPC Interrupt Controller (ACPI 6.5) */

struct acpi_madt_lpc_pic {
	struct acpi_subtable_header header;
	u8 version;
	u64 address;
	u16 size;
	u8 cascade;
};

/* Values for Version field above */

enum acpi_madt_lpc_pic_version {
	ACPI_MADT_LPC_PIC_VERSION_NONE = 0,
	ACPI_MADT_LPC_PIC_VERSION_V1 = 1,
	ACPI_MADT_LPC_PIC_VERSION_RESERVED = 2	/* 2 and greater are reserved */
};

/* 24: RISC-V INTC */
struct acpi_madt_rintc {
	struct acpi_subtable_header header;
	u8 version;
	u8 reserved;
	u32 flags;
	u64 hart_id;
	u32 uid;		/* ACPI processor UID */
	u32 ext_intc_id;	/* External INTC Id */
	u64 imsic_addr;		/* IMSIC base address */
	u32 imsic_size;		/* IMSIC size */
};

/* Values for RISC-V INTC Version field above */

enum acpi_madt_rintc_version {
	ACPI_MADT_RINTC_VERSION_NONE = 0,
	ACPI_MADT_RINTC_VERSION_V1 = 1,
	ACPI_MADT_RINTC_VERSION_RESERVED = 2	/* 2 and greater are reserved */
};

/* 25: RISC-V IMSIC */
struct acpi_madt_imsic {
	struct acpi_subtable_header header;
	u8 version;
	u8 reserved;
	u32 flags;
	u16 num_ids;
	u16 num_guest_ids;
	u8 guest_index_bits;
	u8 hart_index_bits;
	u8 group_index_bits;
	u8 group_index_shift;
};

/* 26: RISC-V APLIC */
struct acpi_madt_aplic {
	struct acpi_subtable_header header;
	u8 version;
	u8 id;
	u32 flags;
	u8 hw_id[8];
	u16 num_idcs;
	u16 num_sources;
	u32 gsi_base;
	u64 base_addr;
	u32 size;
};

/* 27: RISC-V PLIC */
struct acpi_madt_plic {
	struct acpi_subtable_header header;
	u8 version;
	u8 id;
	u8 hw_id[8];
	u16 num_irqs;
	u16 max_prio;
	u32 flags;
	u32 size;
	u64 base_addr;
	u32 gsi_base;
};

/* 80: OEM data */

struct acpi_madt_oem_data {
	ACPI_FLEX_ARRAY(u8, oem_data);
};

/*
 * Common flags fields for MADT subtables
 */

/* MADT Local APIC flags */

#define ACPI_MADT_ENABLED           (1)	/* 00: Processor is usable if set */
#define ACPI_MADT_ONLINE_CAPABLE    (2)	/* 01: System HW supports enabling processor at runtime */

/* MADT MPS INTI flags (inti_flags) */

#define ACPI_MADT_POLARITY_MASK     (3)	/* 00-01: Polarity of APIC I/O input signals */
#define ACPI_MADT_TRIGGER_MASK      (3<<2)	/* 02-03: Trigger mode of APIC input signals */

/* Values for MPS INTI flags */

#define ACPI_MADT_POLARITY_CONFORMS       0
#define ACPI_MADT_POLARITY_ACTIVE_HIGH    1
#define ACPI_MADT_POLARITY_RESERVED       2
#define ACPI_MADT_POLARITY_ACTIVE_LOW     3

#define ACPI_MADT_TRIGGER_CONFORMS        (0)
#define ACPI_MADT_TRIGGER_EDGE            (1<<2)
#define ACPI_MADT_TRIGGER_RESERVED        (2<<2)
#define ACPI_MADT_TRIGGER_LEVEL           (3<<2)

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
 * MPAM - Memory System Resource Partitioning and Monitoring
 *
 * Conforms to "ACPI for Memory System Resource Partitioning and Monitoring 2.0"
 * Document number: ARM DEN 0065, December, 2022.
 *
 ******************************************************************************/

/* MPAM RIS locator types. Table 11, Location types */
enum acpi_mpam_locator_type {
	ACPI_MPAM_LOCATION_TYPE_PROCESSOR_CACHE = 0,
	ACPI_MPAM_LOCATION_TYPE_MEMORY = 1,
	ACPI_MPAM_LOCATION_TYPE_SMMU = 2,
	ACPI_MPAM_LOCATION_TYPE_MEMORY_CACHE = 3,
	ACPI_MPAM_LOCATION_TYPE_ACPI_DEVICE = 4,
	ACPI_MPAM_LOCATION_TYPE_INTERCONNECT = 5,
	ACPI_MPAM_LOCATION_TYPE_UNKNOWN = 0xFF
};

/* MPAM Functional dependency descriptor. Table 10 */
struct acpi_mpam_func_deps {
	u32 producer;
	u32 reserved;
};

/* MPAM Processor cache locator descriptor. Table 13 */
struct acpi_mpam_resource_cache_locator {
	u64 cache_reference;
	u32 reserved;
};

/* MPAM Memory locator descriptor. Table 14 */
struct acpi_mpam_resource_memory_locator {
	u64 proximity_domain;
	u32 reserved;
};

/* MPAM SMMU locator descriptor. Table 15 */
struct acpi_mpam_resource_smmu_locator {
	u64 smmu_interface;
	u32 reserved;
};

/* MPAM Memory-side cache locator descriptor. Table 16 */
struct acpi_mpam_resource_memcache_locator {
	u8 reserved[7];
	u8 level;
	u32 reference;
};

/* MPAM ACPI device locator descriptor. Table 17 */
struct acpi_mpam_resource_acpi_locator {
	u64 acpi_hw_id;
	u32 acpi_unique_id;
};

/* MPAM Interconnect locator descriptor. Table 18 */
struct acpi_mpam_resource_interconnect_locator {
	u64 inter_connect_desc_tbl_off;
	u32 reserved;
};

/* MPAM Locator structure. Table 12 */
struct acpi_mpam_resource_generic_locator {
	u64 descriptor1;
	u32 descriptor2;
};

union acpi_mpam_resource_locator {
	struct acpi_mpam_resource_cache_locator cache_locator;
	struct acpi_mpam_resource_memory_locator memory_locator;
	struct acpi_mpam_resource_smmu_locator smmu_locator;
	struct acpi_mpam_resource_memcache_locator mem_cache_locator;
	struct acpi_mpam_resource_acpi_locator acpi_locator;
	struct acpi_mpam_resource_interconnect_locator interconnect_ifc_locator;
	struct acpi_mpam_resource_generic_locator generic_locator;
};

/* Memory System Component Resource Node Structure Table 9 */
struct acpi_mpam_resource_node {
	u32 identifier;
	u8 ris_index;
	u16 reserved1;
	u8 locator_type;
	union acpi_mpam_resource_locator locator;
	u32 num_functional_deps;
};

/* Memory System Component (MSC) Node Structure. Table 4 */
struct acpi_mpam_msc_node {
	u16 length;
	u8 interface_type;
	u8 reserved;
	u32 identifier;
	u64 base_address;
	u32 mmio_size;
	u32 overflow_interrupt;
	u32 overflow_interrupt_flags;
	u32 reserved1;
	u32 overflow_interrupt_affinity;
	u32 error_interrupt;
	u32 error_interrupt_flags;
	u32 reserved2;
	u32 error_interrupt_affinity;
	u32 max_nrdy_usec;
	u64 hardware_id_linked_device;
	u32 instance_id_linked_device;
	u32 num_resource_nodes;
};

struct acpi_table_mpam {
	struct acpi_table_header header;	/* Common ACPI table header */
};

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
 * MSCT - Maximum System Characteristics Table (ACPI 4.0)
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_msct {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 proximity_offset;	/* Location of proximity info struct(s) */
	u32 max_proximity_domains;	/* Max number of proximity domains */
	u32 max_clock_domains;	/* Max number of clock domains */
	u64 max_address;	/* Max physical address in system */
};

/* subtable - Maximum Proximity Domain Information. Version 1 */

struct acpi_msct_proximity {
	u8 revision;
	u8 length;
	u32 range_start;	/* Start of domain range */
	u32 range_end;		/* End of domain range */
	u32 processor_capacity;
	u64 memory_capacity;	/* In bytes */
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
 * NFIT - NVDIMM Interface Table (ACPI 6.0+)
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_nfit {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 reserved;		/* Reserved, must be zero */
};

/* Subtable header for NFIT */

struct acpi_nfit_header {
	u16 type;
	u16 length;
};

/* Values for subtable type in struct acpi_nfit_header */

enum acpi_nfit_type {
	ACPI_NFIT_TYPE_SYSTEM_ADDRESS = 0,
	ACPI_NFIT_TYPE_MEMORY_MAP = 1,
	ACPI_NFIT_TYPE_INTERLEAVE = 2,
	ACPI_NFIT_TYPE_SMBIOS = 3,
	ACPI_NFIT_TYPE_CONTROL_REGION = 4,
	ACPI_NFIT_TYPE_DATA_REGION = 5,
	ACPI_NFIT_TYPE_FLUSH_ADDRESS = 6,
	ACPI_NFIT_TYPE_CAPABILITIES = 7,
	ACPI_NFIT_TYPE_RESERVED = 8	/* 8 and greater are reserved */
};

/*
 * NFIT Subtables
 */

/* 0: System Physical Address Range Structure */

struct acpi_nfit_system_address {
	struct acpi_nfit_header header;
	u16 range_index;
	u16 flags;
	u32 reserved;		/* Reserved, must be zero */
	u32 proximity_domain;
	u8 range_guid[16];
	u64 address;
	u64 length;
	u64 memory_mapping;
	u64 location_cookie;	/* ACPI 6.4 */
};

/* Flags */

#define ACPI_NFIT_ADD_ONLINE_ONLY       (1)	/* 00: Add/Online Operation Only */
#define ACPI_NFIT_PROXIMITY_VALID       (1<<1)	/* 01: Proximity Domain Valid */
#define ACPI_NFIT_LOCATION_COOKIE_VALID (1<<2)	/* 02: SPA location cookie valid (ACPI 6.4) */

/* Range Type GUIDs appear in the include/acuuid.h file */

/* 1: Memory Device to System Address Range Map Structure */

struct acpi_nfit_memory_map {
	struct acpi_nfit_header header;
	u32 device_handle;
	u16 physical_id;
	u16 region_id;
	u16 range_index;
	u16 region_index;
	u64 region_size;
	u64 region_offset;
	u64 address;
	u16 interleave_index;
	u16 interleave_ways;
	u16 flags;
	u16 reserved;		/* Reserved, must be zero */
};

/* Flags */

#define ACPI_NFIT_MEM_SAVE_FAILED       (1)	/* 00: Last SAVE to Memory Device failed */
#define ACPI_NFIT_MEM_RESTORE_FAILED    (1<<1)	/* 01: Last RESTORE from Memory Device failed */
#define ACPI_NFIT_MEM_FLUSH_FAILED      (1<<2)	/* 02: Platform flush failed */
#define ACPI_NFIT_MEM_NOT_ARMED         (1<<3)	/* 03: Memory Device is not armed */
#define ACPI_NFIT_MEM_HEALTH_OBSERVED   (1<<4)	/* 04: Memory Device observed SMART/health events */
#define ACPI_NFIT_MEM_HEALTH_ENABLED    (1<<5)	/* 05: SMART/health events enabled */
#define ACPI_NFIT_MEM_MAP_FAILED        (1<<6)	/* 06: Mapping to SPA failed */

/* 2: Interleave Structure */

struct acpi_nfit_interleave {
	struct acpi_nfit_header header;
	u16 interleave_index;
	u16 reserved;		/* Reserved, must be zero */
	u32 line_count;
	u32 line_size;
	u32 line_offset[];	/* Variable length */
};

/* 3: SMBIOS Management Information Structure */

struct acpi_nfit_smbios {
	struct acpi_nfit_header header;
	u32 reserved;		/* Reserved, must be zero */
	u8 data[];		/* Variable length */
};

/* 4: NVDIMM Control Region Structure */

struct acpi_nfit_control_region {
	struct acpi_nfit_header header;
	u16 region_index;
	u16 vendor_id;
	u16 device_id;
	u16 revision_id;
	u16 subsystem_vendor_id;
	u16 subsystem_device_id;
	u16 subsystem_revision_id;
	u8 valid_fields;
	u8 manufacturing_location;
	u16 manufacturing_date;
	u8 reserved[2];		/* Reserved, must be zero */
	u32 serial_number;
	u16 code;
	u16 windows;
	u64 window_size;
	u64 command_offset;
	u64 command_size;
	u64 status_offset;
	u64 status_size;
	u16 flags;
	u8 reserved1[6];	/* Reserved, must be zero */
};

/* Flags */

#define ACPI_NFIT_CONTROL_BUFFERED          (1)	/* Block Data Windows implementation is buffered */

/* valid_fields bits */

#define ACPI_NFIT_CONTROL_MFG_INFO_VALID    (1)	/* Manufacturing fields are valid */

/* 5: NVDIMM Block Data Window Region Structure */

struct acpi_nfit_data_region {
	struct acpi_nfit_header header;
	u16 region_index;
	u16 windows;
	u64 offset;
	u64 size;
	u64 capacity;
	u64 start_address;
};

/* 6: Flush Hint Address Structure */

struct acpi_nfit_flush_address {
	struct acpi_nfit_header header;
	u32 device_handle;
	u16 hint_count;
	u8 reserved[6];		/* Reserved, must be zero */
	u64 hint_address[];	/* Variable length */
};

/* 7: Platform Capabilities Structure */

struct acpi_nfit_capabilities {
	struct acpi_nfit_header header;
	u8 highest_capability;
	u8 reserved[3];		/* Reserved, must be zero */
	u32 capabilities;
	u32 reserved2;
};

/* Capabilities Flags */

#define ACPI_NFIT_CAPABILITY_CACHE_FLUSH       (1)	/* 00: Cache Flush to NVDIMM capable */
#define ACPI_NFIT_CAPABILITY_MEM_FLUSH         (1<<1)	/* 01: Memory Flush to NVDIMM capable */
#define ACPI_NFIT_CAPABILITY_MEM_MIRRORING     (1<<2)	/* 02: Memory Mirroring capable */

/*
 * NFIT/DVDIMM device handle support - used as the _ADR for each NVDIMM
 */
struct nfit_device_handle {
	u32 handle;
};

/* Device handle construction and extraction macros */

#define ACPI_NFIT_DIMM_NUMBER_MASK              0x0000000F
#define ACPI_NFIT_CHANNEL_NUMBER_MASK           0x000000F0
#define ACPI_NFIT_MEMORY_ID_MASK                0x00000F00
#define ACPI_NFIT_SOCKET_ID_MASK                0x0000F000
#define ACPI_NFIT_NODE_ID_MASK                  0x0FFF0000

#define ACPI_NFIT_DIMM_NUMBER_OFFSET            0
#define ACPI_NFIT_CHANNEL_NUMBER_OFFSET         4
#define ACPI_NFIT_MEMORY_ID_OFFSET              8
#define ACPI_NFIT_SOCKET_ID_OFFSET              12
#define ACPI_NFIT_NODE_ID_OFFSET                16

/* Macro to construct a NFIT/NVDIMM device handle */

#define ACPI_NFIT_BUILD_DEVICE_HANDLE(dimm, channel, memory, socket, node) \
	((dimm)                                         | \
	((channel) << ACPI_NFIT_CHANNEL_NUMBER_OFFSET)  | \
	((memory)  << ACPI_NFIT_MEMORY_ID_OFFSET)       | \
	((socket)  << ACPI_NFIT_SOCKET_ID_OFFSET)       | \
	((node)    << ACPI_NFIT_NODE_ID_OFFSET))

/* Macros to extract individual fields from a NFIT/NVDIMM device handle */

#define ACPI_NFIT_GET_DIMM_NUMBER(handle) \
	((handle) & ACPI_NFIT_DIMM_NUMBER_MASK)

#define ACPI_NFIT_GET_CHANNEL_NUMBER(handle) \
	(((handle) & ACPI_NFIT_CHANNEL_NUMBER_MASK) >> ACPI_NFIT_CHANNEL_NUMBER_OFFSET)

#define ACPI_NFIT_GET_MEMORY_ID(handle) \
	(((handle) & ACPI_NFIT_MEMORY_ID_MASK)      >> ACPI_NFIT_MEMORY_ID_OFFSET)

#define ACPI_NFIT_GET_SOCKET_ID(handle) \
	(((handle) & ACPI_NFIT_SOCKET_ID_MASK)      >> ACPI_NFIT_SOCKET_ID_OFFSET)

#define ACPI_NFIT_GET_NODE_ID(handle) \
	(((handle) & ACPI_NFIT_NODE_ID_MASK)        >> ACPI_NFIT_NODE_ID_OFFSET)

/*******************************************************************************
 *
 * NHLT - Non HDAudio Link Table
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_nhlt {
	struct acpi_table_header header;	/* Common ACPI table header */
	u8 endpoints_count;
	/*
	 * struct acpi_nhlt_endpoint endpoints[];
	 * struct acpi_nhlt_config oed_config;
	 */
};

struct acpi_nhlt_endpoint {
	u32 length;
	u8 link_type;
	u8 instance_id;
	u16 vendor_id;
	u16 device_id;
	u16 revision_id;
	u32 subsystem_id;
	u8 device_type;
	u8 direction;
	u8 virtual_bus_id;
	/*
	 * struct acpi_nhlt_config device_config;
	 * struct acpi_nhlt_formats_config formats_config;
	 * struct acpi_nhlt_devices_info devices_info;
	 */
};

/*
 * Values for link_type field above
 *
 * Only types PDM and SSP are used
 */
#define ACPI_NHLT_LINKTYPE_HDA			0
#define ACPI_NHLT_LINKTYPE_DSP			1
#define ACPI_NHLT_LINKTYPE_PDM			2
#define ACPI_NHLT_LINKTYPE_SSP			3
#define ACPI_NHLT_LINKTYPE_SLIMBUS		4
#define ACPI_NHLT_LINKTYPE_SDW			5
#define ACPI_NHLT_LINKTYPE_UAOL			6

/* Values for device_id field above */

#define ACPI_NHLT_DEVICEID_DMIC			0xAE20
#define ACPI_NHLT_DEVICEID_BT			0xAE30
#define ACPI_NHLT_DEVICEID_I2S			0xAE34

/* Values for device_type field above */

/*
 * Device types unique to endpoint of link_type=PDM
 *
 * Type PDM used for all SKL+ platforms
 */
#define ACPI_NHLT_DEVICETYPE_PDM		0
#define ACPI_NHLT_DEVICETYPE_PDM_SKL		1
/* Device types unique to endpoint of link_type=SSP */
#define ACPI_NHLT_DEVICETYPE_BT			0
#define ACPI_NHLT_DEVICETYPE_FM			1
#define ACPI_NHLT_DEVICETYPE_MODEM		2
#define ACPI_NHLT_DEVICETYPE_CODEC		4

/* Values for Direction field above */

#define ACPI_NHLT_DIR_RENDER			0
#define ACPI_NHLT_DIR_CAPTURE			1

struct acpi_nhlt_config {
	u32 capabilities_size;
	u8 capabilities[];
};

struct acpi_nhlt_gendevice_config {
	u8 virtual_slot;
	u8 config_type;
};

/* Values for config_type field above */

#define ACPI_NHLT_CONFIGTYPE_GENERIC		0
#define ACPI_NHLT_CONFIGTYPE_MICARRAY		1

struct acpi_nhlt_micdevice_config {
	u8 virtual_slot;
	u8 config_type;
	u8 array_type;
};

/* Values for array_type field above */

#define ACPI_NHLT_ARRAYTYPE_LINEAR2_SMALL	0xA
#define ACPI_NHLT_ARRAYTYPE_LINEAR2_BIG		0xB
#define ACPI_NHLT_ARRAYTYPE_LINEAR4_GEO1	0xC
#define ACPI_NHLT_ARRAYTYPE_PLANAR4_LSHAPED	0xD
#define ACPI_NHLT_ARRAYTYPE_LINEAR4_GEO2	0xE
#define ACPI_NHLT_ARRAYTYPE_VENDOR		0xF

struct acpi_nhlt_vendor_mic_config {
	u8 type;
	u8 panel;
	u16 speaker_position_distance;		/* mm */
	u16 horizontal_offset;			/* mm */
	u16 vertical_offset;			/* mm */
	u8 frequency_low_band;			/* 5*Hz */
	u8 frequency_high_band;			/* 500*Hz */
	u16 direction_angle;			/* -180 - +180 */
	u16 elevation_angle;			/* -180 - +180 */
	u16 work_vertical_angle_begin;		/* -180 - +180 with 2 deg step */
	u16 work_vertical_angle_end;		/* -180 - +180 with 2 deg step */
	u16 work_horizontal_angle_begin;	/* -180 - +180 with 2 deg step */
	u16 work_horizontal_angle_end;		/* -180 - +180 with 2 deg step */
};

/* Values for Type field above */

#define ACPI_NHLT_MICTYPE_OMNIDIRECTIONAL	0
#define ACPI_NHLT_MICTYPE_SUBCARDIOID		1
#define ACPI_NHLT_MICTYPE_CARDIOID		2
#define ACPI_NHLT_MICTYPE_SUPERCARDIOID		3
#define ACPI_NHLT_MICTYPE_HYPERCARDIOID		4
#define ACPI_NHLT_MICTYPE_8SHAPED		5
#define ACPI_NHLT_MICTYPE_RESERVED		6
#define ACPI_NHLT_MICTYPE_VENDORDEFINED		7

/* Values for Panel field above */

#define ACPI_NHLT_MICLOCATION_TOP		0
#define ACPI_NHLT_MICLOCATION_BOTTOM		1
#define ACPI_NHLT_MICLOCATION_LEFT		2
#define ACPI_NHLT_MICLOCATION_RIGHT		3
#define ACPI_NHLT_MICLOCATION_FRONT		4
#define ACPI_NHLT_MICLOCATION_REAR		5

struct acpi_nhlt_vendor_micdevice_config {
	u8 virtual_slot;
	u8 config_type;
	u8 array_type;
	u8 mics_count;
	struct acpi_nhlt_vendor_mic_config mics[];
};

union acpi_nhlt_device_config {
	u8 virtual_slot;
	struct acpi_nhlt_gendevice_config gen;
	struct acpi_nhlt_micdevice_config mic;
	struct acpi_nhlt_vendor_micdevice_config vendor_mic;
};

/* Inherited from Microsoft's WAVEFORMATEXTENSIBLE. */
struct acpi_nhlt_wave_formatext {
	u16 format_tag;
	u16 channel_count;
	u32 samples_per_sec;
	u32 avg_bytes_per_sec;
	u16 block_align;
	u16 bits_per_sample;
	u16 extra_format_size;
	u16 valid_bits_per_sample;
	u32 channel_mask;
	u8 subformat[16];
};

struct acpi_nhlt_format_config {
	struct acpi_nhlt_wave_formatext format;
	struct acpi_nhlt_config config;
};

struct acpi_nhlt_formats_config {
	u8 formats_count;
	struct acpi_nhlt_format_config formats[];
};

struct acpi_nhlt_device_info {
	u8 id[16];
	u8 instance_id;
	u8 port_id;
};

struct acpi_nhlt_devices_info {
	u8 devices_count;
	struct acpi_nhlt_device_info devices[];
};

/*******************************************************************************
 *
 * PCCT - Platform Communications Channel Table (ACPI 5.0)
 *        Version 2 (ACPI 6.2)
 *
 ******************************************************************************/

struct acpi_table_pcct {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 flags;
	u64 reserved;
};

/* Values for Flags field above */

#define ACPI_PCCT_DOORBELL              1

/* Values for subtable type in struct acpi_subtable_header */

enum acpi_pcct_type {
	ACPI_PCCT_TYPE_GENERIC_SUBSPACE = 0,
	ACPI_PCCT_TYPE_HW_REDUCED_SUBSPACE = 1,
	ACPI_PCCT_TYPE_HW_REDUCED_SUBSPACE_TYPE2 = 2,	/* ACPI 6.1 */
	ACPI_PCCT_TYPE_EXT_PCC_MASTER_SUBSPACE = 3,	/* ACPI 6.2 */
	ACPI_PCCT_TYPE_EXT_PCC_SLAVE_SUBSPACE = 4,	/* ACPI 6.2 */
	ACPI_PCCT_TYPE_HW_REG_COMM_SUBSPACE = 5,	/* ACPI 6.4 */
	ACPI_PCCT_TYPE_RESERVED = 6	/* 6 and greater are reserved */
};

/*
 * PCCT Subtables, correspond to Type in struct acpi_subtable_header
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
	u32 latency;
	u32 max_access_rate;
	u16 min_turnaround_time;
};

/* 1: HW-reduced Communications Subspace (ACPI 5.1) */

struct acpi_pcct_hw_reduced {
	struct acpi_subtable_header header;
	u32 platform_interrupt;
	u8 flags;
	u8 reserved;
	u64 base_address;
	u64 length;
	struct acpi_generic_address doorbell_register;
	u64 preserve_mask;
	u64 write_mask;
	u32 latency;
	u32 max_access_rate;
	u16 min_turnaround_time;
};

/* 2: HW-reduced Communications Subspace Type 2 (ACPI 6.1) */

struct acpi_pcct_hw_reduced_type2 {
	struct acpi_subtable_header header;
	u32 platform_interrupt;
	u8 flags;
	u8 reserved;
	u64 base_address;
	u64 length;
	struct acpi_generic_address doorbell_register;
	u64 preserve_mask;
	u64 write_mask;
	u32 latency;
	u32 max_access_rate;
	u16 min_turnaround_time;
	struct acpi_generic_address platform_ack_register;
	u64 ack_preserve_mask;
	u64 ack_write_mask;
};

/* 3: Extended PCC Master Subspace Type 3 (ACPI 6.2) */

struct acpi_pcct_ext_pcc_master {
	struct acpi_subtable_header header;
	u32 platform_interrupt;
	u8 flags;
	u8 reserved1;
	u64 base_address;
	u32 length;
	struct acpi_generic_address doorbell_register;
	u64 preserve_mask;
	u64 write_mask;
	u32 latency;
	u32 max_access_rate;
	u32 min_turnaround_time;
	struct acpi_generic_address platform_ack_register;
	u64 ack_preserve_mask;
	u64 ack_set_mask;
	u64 reserved2;
	struct acpi_generic_address cmd_complete_register;
	u64 cmd_complete_mask;
	struct acpi_generic_address cmd_update_register;
	u64 cmd_update_preserve_mask;
	u64 cmd_update_set_mask;
	struct acpi_generic_address error_status_register;
	u64 error_status_mask;
};

/* 4: Extended PCC Slave Subspace Type 4 (ACPI 6.2) */

struct acpi_pcct_ext_pcc_slave {
	struct acpi_subtable_header header;
	u32 platform_interrupt;
	u8 flags;
	u8 reserved1;
	u64 base_address;
	u32 length;
	struct acpi_generic_address doorbell_register;
	u64 preserve_mask;
	u64 write_mask;
	u32 latency;
	u32 max_access_rate;
	u32 min_turnaround_time;
	struct acpi_generic_address platform_ack_register;
	u64 ack_preserve_mask;
	u64 ack_set_mask;
	u64 reserved2;
	struct acpi_generic_address cmd_complete_register;
	u64 cmd_complete_mask;
	struct acpi_generic_address cmd_update_register;
	u64 cmd_update_preserve_mask;
	u64 cmd_update_set_mask;
	struct acpi_generic_address error_status_register;
	u64 error_status_mask;
};

/* 5: HW Registers based Communications Subspace */

struct acpi_pcct_hw_reg {
	struct acpi_subtable_header header;
	u16 version;
	u64 base_address;
	u64 length;
	struct acpi_generic_address doorbell_register;
	u64 doorbell_preserve;
	u64 doorbell_write;
	struct acpi_generic_address cmd_complete_register;
	u64 cmd_complete_mask;
	struct acpi_generic_address error_status_register;
	u64 error_status_mask;
	u32 nominal_latency;
	u32 min_turnaround_time;
};

/* Values for doorbell flags above */

#define ACPI_PCCT_INTERRUPT_POLARITY    (1)
#define ACPI_PCCT_INTERRUPT_MODE        (1<<1)

/*
 * PCC memory structures (not part of the ACPI table)
 */

/* Shared Memory Region */

struct acpi_pcct_shared_memory {
	u32 signature;
	u16 command;
	u16 status;
};

/* Extended PCC Subspace Shared Memory Region (ACPI 6.2) */

struct acpi_pcct_ext_pcc_shared_memory {
	u32 signature;
	u32 flags;
	u32 length;
	u32 command;
};

/*******************************************************************************
 *
 * PDTT - Platform Debug Trigger Table (ACPI 6.2)
 *        Version 0
 *
 ******************************************************************************/

struct acpi_table_pdtt {
	struct acpi_table_header header;	/* Common ACPI table header */
	u8 trigger_count;
	u8 reserved[3];
	u32 array_offset;
};

/*
 * PDTT Communication Channel Identifier Structure.
 * The number of these structures is defined by trigger_count above,
 * starting at array_offset.
 */
struct acpi_pdtt_channel {
	u8 subchannel_id;
	u8 flags;
};

/* Flags for above */

#define ACPI_PDTT_RUNTIME_TRIGGER           (1)
#define ACPI_PDTT_WAIT_COMPLETION           (1<<1)
#define ACPI_PDTT_TRIGGER_ORDER             (1<<2)

/*******************************************************************************
 *
 * PHAT - Platform Health Assessment Table (ACPI 6.4)
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_phat {
	struct acpi_table_header header;	/* Common ACPI table header */
};

/* Common header for PHAT subtables that follow main table */

struct acpi_phat_header {
	u16 type;
	u16 length;
	u8 revision;
};

/* Values for Type field above */

#define ACPI_PHAT_TYPE_FW_VERSION_DATA  0
#define ACPI_PHAT_TYPE_FW_HEALTH_DATA   1
#define ACPI_PHAT_TYPE_RESERVED         2	/* 0x02-0xFFFF are reserved */

/*
 * PHAT subtables, correspond to Type in struct acpi_phat_header
 */

/* 0: Firmware Version Data Record */

struct acpi_phat_version_data {
	struct acpi_phat_header header;
	u8 reserved[3];
	u32 element_count;
};

struct acpi_phat_version_element {
	u8 guid[16];
	u64 version_value;
	u32 producer_id;
};

/* 1: Firmware Health Data Record */

struct acpi_phat_health_data {
	struct acpi_phat_header header;
	u8 reserved[2];
	u8 health;
	u8 device_guid[16];
	u32 device_specific_offset;	/* Zero if no Device-specific data */
};

/* Values for Health field above */

#define ACPI_PHAT_ERRORS_FOUND          0
#define ACPI_PHAT_NO_ERRORS             1
#define ACPI_PHAT_UNKNOWN_ERRORS        2
#define ACPI_PHAT_ADVISORY              3

/*******************************************************************************
 *
 * PMTT - Platform Memory Topology Table (ACPI 5.0)
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_pmtt {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 memory_device_count;
	/*
	 * Immediately followed by:
	 * MEMORY_DEVICE memory_device_struct[memory_device_count];
	 */
};

/* Common header for PMTT subtables that follow main table */

struct acpi_pmtt_header {
	u8 type;
	u8 reserved1;
	u16 length;
	u16 flags;
	u16 reserved2;
	u32 memory_device_count;	/* Zero means no memory device structs follow */
	/*
	 * Immediately followed by:
	 * u8 type_specific_data[]
	 * MEMORY_DEVICE memory_device_struct[memory_device_count];
	 */
};

/* Values for Type field above */

#define ACPI_PMTT_TYPE_SOCKET           0
#define ACPI_PMTT_TYPE_CONTROLLER       1
#define ACPI_PMTT_TYPE_DIMM             2
#define ACPI_PMTT_TYPE_RESERVED         3	/* 0x03-0xFE are reserved */
#define ACPI_PMTT_TYPE_VENDOR           0xFF

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
	/*
	 * Immediately followed by:
	 * MEMORY_DEVICE memory_device_struct[memory_device_count];
	 */

/* 1: Memory Controller subtable */

struct acpi_pmtt_controller {
	struct acpi_pmtt_header header;
	u16 controller_id;
	u16 reserved;
};
	/*
	 * Immediately followed by:
	 * MEMORY_DEVICE memory_device_struct[memory_device_count];
	 */

/* 2: Physical Component Identifier (DIMM) */

struct acpi_pmtt_physical_component {
	struct acpi_pmtt_header header;
	u32 bios_handle;
};

/* 0xFF: Vendor Specific Data */

struct acpi_pmtt_vendor_specific {
	struct acpi_pmtt_header header;
	u8 type_uuid[16];
	u8 specific[];
	/*
	 * Immediately followed by:
	 * u8 vendor_specific_data[];
	 * MEMORY_DEVICE memory_device_struct[memory_device_count];
	 */
};

/*******************************************************************************
 *
 * PPTT - Processor Properties Topology Table (ACPI 6.2)
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_pptt {
	struct acpi_table_header header;	/* Common ACPI table header */
};

/* Values for Type field above */

enum acpi_pptt_type {
	ACPI_PPTT_TYPE_PROCESSOR = 0,
	ACPI_PPTT_TYPE_CACHE = 1,
	ACPI_PPTT_TYPE_ID = 2,
	ACPI_PPTT_TYPE_RESERVED = 3
};

/* 0: Processor Hierarchy Node Structure */

struct acpi_pptt_processor {
	struct acpi_subtable_header header;
	u16 reserved;
	u32 flags;
	u32 parent;
	u32 acpi_processor_id;
	u32 number_of_priv_resources;
};

/* Flags */

#define ACPI_PPTT_PHYSICAL_PACKAGE          (1)
#define ACPI_PPTT_ACPI_PROCESSOR_ID_VALID   (1<<1)
#define ACPI_PPTT_ACPI_PROCESSOR_IS_THREAD  (1<<2)	/* ACPI 6.3 */
#define ACPI_PPTT_ACPI_LEAF_NODE            (1<<3)	/* ACPI 6.3 */
#define ACPI_PPTT_ACPI_IDENTICAL            (1<<4)	/* ACPI 6.3 */

/* 1: Cache Type Structure */

struct acpi_pptt_cache {
	struct acpi_subtable_header header;
	u16 reserved;
	u32 flags;
	u32 next_level_of_cache;
	u32 size;
	u32 number_of_sets;
	u8 associativity;
	u8 attributes;
	u16 line_size;
};

/* 1: Cache Type Structure for PPTT version 3 */

struct acpi_pptt_cache_v1 {
	u32 cache_id;
};

/* Flags */

#define ACPI_PPTT_SIZE_PROPERTY_VALID       (1)	/* Physical property valid */
#define ACPI_PPTT_NUMBER_OF_SETS_VALID      (1<<1)	/* Number of sets valid */
#define ACPI_PPTT_ASSOCIATIVITY_VALID       (1<<2)	/* Associativity valid */
#define ACPI_PPTT_ALLOCATION_TYPE_VALID     (1<<3)	/* Allocation type valid */
#define ACPI_PPTT_CACHE_TYPE_VALID          (1<<4)	/* Cache type valid */
#define ACPI_PPTT_WRITE_POLICY_VALID        (1<<5)	/* Write policy valid */
#define ACPI_PPTT_LINE_SIZE_VALID           (1<<6)	/* Line size valid */
#define ACPI_PPTT_CACHE_ID_VALID            (1<<7)	/* Cache ID valid */

/* Masks for Attributes */

#define ACPI_PPTT_MASK_ALLOCATION_TYPE      (0x03)	/* Allocation type */
#define ACPI_PPTT_MASK_CACHE_TYPE           (0x0C)	/* Cache type */
#define ACPI_PPTT_MASK_WRITE_POLICY         (0x10)	/* Write policy */

/* Attributes describing cache */
#define ACPI_PPTT_CACHE_READ_ALLOCATE       (0x0)	/* Cache line is allocated on read */
#define ACPI_PPTT_CACHE_WRITE_ALLOCATE      (0x01)	/* Cache line is allocated on write */
#define ACPI_PPTT_CACHE_RW_ALLOCATE         (0x02)	/* Cache line is allocated on read and write */
#define ACPI_PPTT_CACHE_RW_ALLOCATE_ALT     (0x03)	/* Alternate representation of above */

#define ACPI_PPTT_CACHE_TYPE_DATA           (0x0)	/* Data cache */
#define ACPI_PPTT_CACHE_TYPE_INSTR          (1<<2)	/* Instruction cache */
#define ACPI_PPTT_CACHE_TYPE_UNIFIED        (2<<2)	/* Unified I & D cache */
#define ACPI_PPTT_CACHE_TYPE_UNIFIED_ALT    (3<<2)	/* Alternate representation of above */

#define ACPI_PPTT_CACHE_POLICY_WB           (0x0)	/* Cache is write back */
#define ACPI_PPTT_CACHE_POLICY_WT           (1<<4)	/* Cache is write through */

/* 2: ID Structure */

struct acpi_pptt_id {
	struct acpi_subtable_header header;
	u16 reserved;
	u32 vendor_id;
	u64 level1_id;
	u64 level2_id;
	u16 major_rev;
	u16 minor_rev;
	u16 spin_rev;
};

/*******************************************************************************
 *
 * PRMT - Platform Runtime Mechanism Table
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_prmt {
	struct acpi_table_header header;	/* Common ACPI table header */
};

struct acpi_table_prmt_header {
	u8 platform_guid[16];
	u32 module_info_offset;
	u32 module_info_count;
};

struct acpi_prmt_module_header {
	u16 revision;
	u16 length;
};

struct acpi_prmt_module_info {
	u16 revision;
	u16 length;
	u8 module_guid[16];
	u16 major_rev;
	u16 minor_rev;
	u16 handler_info_count;
	u32 handler_info_offset;
	u64 mmio_list_pointer;
};

struct acpi_prmt_handler_info {
	u16 revision;
	u16 length;
	u8 handler_guid[16];
	u64 handler_address;
	u64 static_data_buffer_address;
	u64 acpi_param_buffer_address;
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
	u16 version;
	u8 capabilities[16];
	u8 set_capabilities[16];
	u16 num_parameter_blocks;
	u32 set_capabilities_status;
};

/* RASF Parameter Block Structure Header */

struct acpi_rasf_parameter_block {
	u16 type;
	u16 version;
	u16 length;
};

/* RASF Parameter Block Structure for PATROL_SCRUB */

struct acpi_rasf_patrol_scrub_parameter {
	struct acpi_rasf_parameter_block header;
	u16 patrol_scrub_command;
	u64 requested_address_range[2];
	u64 actual_address_range[2];
	u16 flags;
	u8 requested_speed;
};

/* Masks for Flags and Speed fields above */

#define ACPI_RASF_SCRUBBER_RUNNING      1
#define ACPI_RASF_SPEED                 (7<<1)
#define ACPI_RASF_SPEED_SLOW            (0<<1)
#define ACPI_RASF_SPEED_MEDIUM          (4<<1)
#define ACPI_RASF_SPEED_FAST            (7<<1)

/* Channel Commands */

enum acpi_rasf_commands {
	ACPI_RASF_EXECUTE_RASF_COMMAND = 1
};

/* Platform RAS Capabilities */

enum acpi_rasf_capabiliities {
	ACPI_HW_PATROL_SCRUB_SUPPORTED = 0,
	ACPI_SW_PATROL_SCRUB_EXPOSED = 1
};

/* Patrol Scrub Commands */

enum acpi_rasf_patrol_scrub_commands {
	ACPI_RASF_GET_PATROL_PARAMETERS = 1,
	ACPI_RASF_START_PATROL_SCRUBBER = 2,
	ACPI_RASF_STOP_PATROL_SCRUBBER = 3
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

/*******************************************************************************
 *
 * RAS2 - RAS2 Feature Table (ACPI 6.5)
 *        Version 1
 *
 *
 ******************************************************************************/

struct acpi_table_ras2 {
	struct acpi_table_header header;	/* Common ACPI table header */
	u16 reserved;
	u16 num_pcc_descs;
};

/* RAS2 Platform Communication Channel Descriptor */

struct acpi_ras2_pcc_desc {
	u8 channel_id;
	u16 reserved;
	u8 feature_type;
	u32 instance;
};

/* RAS2 Platform Communication Channel Shared Memory Region */

struct acpi_ras2_shared_memory {
	u32 signature;
	u16 command;
	u16 status;
	u16 version;
	u8 features[16];
	u8 set_capabilities[16];
	u16 num_parameter_blocks;
	u32 set_capabilities_status;
};

/* RAS2 Parameter Block Structure for PATROL_SCRUB */

struct acpi_ras2_parameter_block {
	u16 type;
	u16 version;
	u16 length;
};

/* RAS2 Parameter Block Structure for PATROL_SCRUB */

struct acpi_ras2_patrol_scrub_parameter {
	struct acpi_ras2_parameter_block header;
	u16 patrol_scrub_command;
	u64 requested_address_range[2];
	u64 actual_address_range[2];
	u32 flags;
	u32 scrub_params_out;
	u32 scrub_params_in;
};

/* Masks for Flags field above */

#define ACPI_RAS2_SCRUBBER_RUNNING      1

/* RAS2 Parameter Block Structure for LA2PA_TRANSLATION */

struct acpi_ras2_la2pa_translation_parameter {
	struct acpi_ras2_parameter_block header;
	u16 addr_translation_command;
	u64 sub_inst_id;
	u64 logical_address;
	u64 physical_address;
	u32 status;
};

/* Channel Commands */

enum acpi_ras2_commands {
	ACPI_RAS2_EXECUTE_RAS2_COMMAND = 1
};

/* Platform RAS2 Features */

enum acpi_ras2_features {
	ACPI_RAS2_PATROL_SCRUB_SUPPORTED = 0,
	ACPI_RAS2_LA2PA_TRANSLATION = 1
};

/* RAS2 Patrol Scrub Commands */

enum acpi_ras2_patrol_scrub_commands {
	ACPI_RAS2_GET_PATROL_PARAMETERS = 1,
	ACPI_RAS2_START_PATROL_SCRUBBER = 2,
	ACPI_RAS2_STOP_PATROL_SCRUBBER = 3
};

/* RAS2 LA2PA Translation Commands */

enum acpi_ras2_la2_pa_translation_commands {
	ACPI_RAS2_GET_LA2PA_TRANSLATION = 1,
};

/* RAS2 LA2PA Translation Status values */

enum acpi_ras2_la2_pa_translation_status {
	ACPI_RAS2_LA2PA_TRANSLATION_SUCCESS = 0,
	ACPI_RAS2_LA2PA_TRANSLATION_FAIL = 1,
};

/* Channel Command flags */

#define ACPI_RAS2_GENERATE_SCI          (1<<15)

/* Status values */

enum acpi_ras2_status {
	ACPI_RAS2_SUCCESS = 0,
	ACPI_RAS2_NOT_VALID = 1,
	ACPI_RAS2_NOT_SUPPORTED = 2,
	ACPI_RAS2_BUSY = 3,
	ACPI_RAS2_FAILED = 4,
	ACPI_RAS2_ABORTED = 5,
	ACPI_RAS2_INVALID_DATA = 6
};

/* Status flags */

#define ACPI_RAS2_COMMAND_COMPLETE      (1)
#define ACPI_RAS2_SCI_DOORBELL          (1<<1)
#define ACPI_RAS2_ERROR                 (1<<2)
#define ACPI_RAS2_STATUS                (0x1F<<3)

/*******************************************************************************
 *
 * RGRT - Regulatory Graphics Resource Table
 *        Version 1
 *
 * Conforms to "ACPI RGRT" available at:
 * https://microsoft.github.io/mu/dyn/mu_plus/ms_core_pkg/acpi_RGRT/feature_acpi_rgrt/
 *
 ******************************************************************************/

struct acpi_table_rgrt {
	struct acpi_table_header header;	/* Common ACPI table header */
	u16 version;
	u8 image_type;
	u8 reserved;
	u8 image[];
};

/* image_type values */

enum acpi_rgrt_image_type {
	ACPI_RGRT_TYPE_RESERVED0 = 0,
	ACPI_RGRT_IMAGE_TYPE_PNG = 1,
	ACPI_RGRT_TYPE_RESERVED = 2	/* 2 and greater are reserved */
};

/*******************************************************************************
 *
 * RHCT - RISC-V Hart Capabilities Table
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_rhct {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 flags;		/* RHCT flags */
	u64 time_base_freq;
	u32 node_count;
	u32 node_offset;
};

/* RHCT Flags */

#define ACPI_RHCT_TIMER_CANNOT_WAKEUP_CPU       (1)
/*
 * RHCT subtables
 */
struct acpi_rhct_node_header {
	u16 type;
	u16 length;
	u16 revision;
};

/* Values for RHCT subtable Type above */

enum acpi_rhct_node_type {
	ACPI_RHCT_NODE_TYPE_ISA_STRING = 0x0000,
	ACPI_RHCT_NODE_TYPE_CMO = 0x0001,
	ACPI_RHCT_NODE_TYPE_MMU = 0x0002,
	ACPI_RHCT_NODE_TYPE_RESERVED = 0x0003,
	ACPI_RHCT_NODE_TYPE_HART_INFO = 0xFFFF,
};

/*
 * RHCT node specific subtables
 */

/* ISA string node structure */
struct acpi_rhct_isa_string {
	u16 isa_length;
	char isa[];
};

struct acpi_rhct_cmo_node {
	u8 reserved;		/* Must be zero */
	u8 cbom_size;		/* CBOM size in powerof 2 */
	u8 cbop_size;		/* CBOP size in powerof 2 */
	u8 cboz_size;		/* CBOZ size in powerof 2 */
};

struct acpi_rhct_mmu_node {
	u8 reserved;		/* Must be zero */
	u8 mmu_type;		/* Virtual Address Scheme */
};

enum acpi_rhct_mmu_type {
	ACPI_RHCT_MMU_TYPE_SV39 = 0,
	ACPI_RHCT_MMU_TYPE_SV48 = 1,
	ACPI_RHCT_MMU_TYPE_SV57 = 2
};

/* Hart Info node structure */
struct acpi_rhct_hart_info {
	u16 num_offsets;
	u32 uid;		/* ACPI processor UID */
};

/*******************************************************************************
 *
 * SBST - Smart Battery Specification Table
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_sbst {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 warning_level;
	u32 low_level;
	u32 critical_level;
};

/*******************************************************************************
 *
 * SDEI - Software Delegated Exception Interface Descriptor Table
 *
 * Conforms to "Software Delegated Exception Interface (SDEI)" ARM DEN0054A,
 * May 8th, 2017. Copyright 2017 ARM Ltd.
 *
 ******************************************************************************/

struct acpi_table_sdei {
	struct acpi_table_header header;	/* Common ACPI table header */
};

/*******************************************************************************
 *
 * SDEV - Secure Devices Table (ACPI 6.2)
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_sdev {
	struct acpi_table_header header;	/* Common ACPI table header */
};

struct acpi_sdev_header {
	u8 type;
	u8 flags;
	u16 length;
};

/* Values for subtable type above */

enum acpi_sdev_type {
	ACPI_SDEV_TYPE_NAMESPACE_DEVICE = 0,
	ACPI_SDEV_TYPE_PCIE_ENDPOINT_DEVICE = 1,
	ACPI_SDEV_TYPE_RESERVED = 2	/* 2 and greater are reserved */
};

/* Values for flags above */

#define ACPI_SDEV_HANDOFF_TO_UNSECURE_OS    (1)
#define ACPI_SDEV_SECURE_COMPONENTS_PRESENT (1<<1)

/*
 * SDEV subtables
 */

/* 0: Namespace Device Based Secure Device Structure */

struct acpi_sdev_namespace {
	struct acpi_sdev_header header;
	u16 device_id_offset;
	u16 device_id_length;
	u16 vendor_data_offset;
	u16 vendor_data_length;
};

struct acpi_sdev_secure_component {
	u16 secure_component_offset;
	u16 secure_component_length;
};

/*
 * SDEV sub-subtables ("Components") for above
 */
struct acpi_sdev_component {
	struct acpi_sdev_header header;
};

/* Values for sub-subtable type above */

enum acpi_sac_type {
	ACPI_SDEV_TYPE_ID_COMPONENT = 0,
	ACPI_SDEV_TYPE_MEM_COMPONENT = 1
};

struct acpi_sdev_id_component {
	struct acpi_sdev_header header;
	u16 hardware_id_offset;
	u16 hardware_id_length;
	u16 subsystem_id_offset;
	u16 subsystem_id_length;
	u16 hardware_revision;
	u8 hardware_rev_present;
	u8 class_code_present;
	u8 pci_base_class;
	u8 pci_sub_class;
	u8 pci_programming_xface;
};

struct acpi_sdev_mem_component {
	struct acpi_sdev_header header;
	u32 reserved;
	u64 memory_base_address;
	u64 memory_length;
};

/* 1: PCIe Endpoint Device Based Device Structure */

struct acpi_sdev_pcie {
	struct acpi_sdev_header header;
	u16 segment;
	u16 start_bus;
	u16 path_offset;
	u16 path_length;
	u16 vendor_data_offset;
	u16 vendor_data_length;
};

/* 1a: PCIe Endpoint path entry */

struct acpi_sdev_pcie_path {
	u8 device;
	u8 function;
};

/*******************************************************************************
 *
 * SVKL - Storage Volume Key Location Table (ACPI 6.4)
 *        From: "Guest-Host-Communication Interface (GHCI) for Intel
 *        Trust Domain Extensions (Intel TDX)".
 *        Version 1
 *
 ******************************************************************************/

struct acpi_table_svkl {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 count;
};

struct acpi_svkl_key {
	u16 type;
	u16 format;
	u32 size;
	u64 address;
};

enum acpi_svkl_type {
	ACPI_SVKL_TYPE_MAIN_STORAGE = 0,
	ACPI_SVKL_TYPE_RESERVED = 1	/* 1 and greater are reserved */
};

enum acpi_svkl_format {
	ACPI_SVKL_FORMAT_RAW_BINARY = 0,
	ACPI_SVKL_FORMAT_RESERVED = 1	/* 1 and greater are reserved */
};

/*******************************************************************************
 *
 * TDEL - TD-Event Log
 *        From: "Guest-Host-Communication Interface (GHCI) for Intel
 *        Trust Domain Extensions (Intel TDX)".
 *        September 2020
 *
 ******************************************************************************/

struct acpi_table_tdel {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 reserved;
	u64 log_area_minimum_length;
	u64 log_area_start_address;
};

/* Reset to default packing */

#pragma pack()

#endif				/* __ACTBL2_H__ */
