/******************************************************************************
 *
 * Name: actbl1.h - Additional ACPI table definitions
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2006, R. Byron Moore
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
 * Values for description table header signatures. Useful because they make
 * it more difficult to inadvertently type in the wrong signature.
 */
#define ACPI_SIG_ASF            "ASF!"	/* Alert Standard Format table */
#define ACPI_SIG_BOOT           "BOOT"	/* Simple Boot Flag Table */
#define ACPI_SIG_CPEP           "CPEP"	/* Corrected Platform Error Polling table */
#define ACPI_SIG_DBGP           "DBGP"	/* Debug Port table */
#define ACPI_SIG_ECDT           "ECDT"	/* Embedded Controller Boot Resources Table */
#define ACPI_SIG_HPET           "HPET"	/* High Precision Event Timer table */
#define ACPI_SIG_MADT           "APIC"	/* Multiple APIC Description Table */
#define ACPI_SIG_MCFG           "MCFG"	/* PCI Memory Mapped Configuration table */
#define ACPI_SIG_SBST           "SBST"	/* Smart Battery Specification Table */
#define ACPI_SIG_SLIT           "SLIT"	/* System Locality Distance Information Table */
#define ACPI_SIG_SPCR           "SPCR"	/* Serial Port Console Redirection table */
#define ACPI_SIG_SPMI           "SPMI"	/* Server Platform Management Interface table */
#define ACPI_SIG_SRAT           "SRAT"	/* System Resource Affinity Table */
#define ACPI_SIG_TCPA           "TCPA"	/* Trusted Computing Platform Alliance table */
#define ACPI_SIG_WDRT           "WDRT"	/* Watchdog Resource Table */

/* Legacy names */

#define APIC_SIG                "APIC"	/* Multiple APIC Description Table */
#define BOOT_SIG                "BOOT"	/* Simple Boot Flag Table */
#define SBST_SIG                "SBST"	/* Smart Battery Specification Table */

/*
 * All tables must be byte-packed to match the ACPI specification, since
 * the tables are provided by the system BIOS.
 */
#pragma pack(1)

/*
 * Note about bitfields: The u8 type is used for bitfields in ACPI tables.
 * This is the only type that is even remotely portable. Anything else is not
 * portable, so do not use any other bitfield types.
 */

/*******************************************************************************
 *
 * ASF - Alert Standard Format table (Signature "ASF!")
 *
 ******************************************************************************/

struct acpi_table_asf {
ACPI_TABLE_HEADER_DEF};

#define ACPI_ASF_HEADER_DEF \
	u8                              type; \
	u8                              reserved; \
	u16                             length;

struct acpi_asf_header {
ACPI_ASF_HEADER_DEF};

/* Values for Type field */

#define ASF_INFO                0
#define ASF_ALERT               1
#define ASF_CONTROL             2
#define ASF_BOOT                3
#define ASF_ADDRESS             4
#define ASF_RESERVED            5

/*
 * ASF subtables
 */

/* 0: ASF Information */

struct acpi_asf_info {
	ACPI_ASF_HEADER_DEF u8 min_reset_value;
	u8 min_poll_interval;
	u16 system_id;
	u32 mfg_id;
	u8 flags;
	u8 reserved2[3];
};

/* 1: ASF Alerts */

struct acpi_asf_alert {
	ACPI_ASF_HEADER_DEF u8 assert_mask;
	u8 deassert_mask;
	u8 alerts;
	u8 data_length;
	u8 array[1];
};

/* 2: ASF Remote Control */

struct acpi_asf_remote {
	ACPI_ASF_HEADER_DEF u8 controls;
	u8 data_length;
	u16 reserved2;
	u8 array[1];
};

/* 3: ASF RMCP Boot Options */

struct acpi_asf_rmcp {
	ACPI_ASF_HEADER_DEF u8 capabilities[7];
	u8 completion_code;
	u32 enterprise_id;
	u8 command;
	u16 parameter;
	u16 boot_options;
	u16 oem_parameters;
};

/* 4: ASF Address */

struct acpi_asf_address {
	ACPI_ASF_HEADER_DEF u8 eprom_address;
	u8 devices;
	u8 smbus_addresses[1];
};

/*******************************************************************************
 *
 * BOOT - Simple Boot Flag Table
 *
 ******************************************************************************/

struct acpi_table_boot {
	ACPI_TABLE_HEADER_DEF u8 cmos_index;	/* Index in CMOS RAM for the boot register */
	u8 reserved[3];
};

/*******************************************************************************
 *
 * CPEP - Corrected Platform Error Polling table
 *
 ******************************************************************************/

struct acpi_table_cpep {
	ACPI_TABLE_HEADER_DEF u64 reserved;
};

/* Subtable */

struct acpi_cpep_polling {
	u8 type;
	u8 length;
	u8 processor_id;	/* Processor ID */
	u8 processor_eid;	/* Processor EID */
	u32 polling_interval;	/* Polling interval (msec) */
};

/*******************************************************************************
 *
 * DBGP - Debug Port table
 *
 ******************************************************************************/

struct acpi_table_dbgp {
	ACPI_TABLE_HEADER_DEF u8 interface_type;	/* 0=full 16550, 1=subset of 16550 */
	u8 reserved[3];
	struct acpi_generic_address debug_port;
};

/*******************************************************************************
 *
 * ECDT - Embedded Controller Boot Resources Table
 *
 ******************************************************************************/

struct ec_boot_resources {
	ACPI_TABLE_HEADER_DEF struct acpi_generic_address ec_control;	/* Address of EC command/status register */
	struct acpi_generic_address ec_data;	/* Address of EC data register */
	u32 uid;		/* Unique ID - must be same as the EC _UID method */
	u8 gpe_bit;		/* The GPE for the EC */
	u8 ec_id[1];		/* Full namepath of the EC in the ACPI namespace */
};

/*******************************************************************************
 *
 * HPET - High Precision Event Timer table
 *
 ******************************************************************************/

struct acpi_hpet_table {
	ACPI_TABLE_HEADER_DEF u32 hardware_id;	/* Hardware ID of event timer block */
	struct acpi_generic_address base_address;	/* Address of event timer block */
	u8 hpet_number;		/* HPET sequence number */
	u16 clock_tick;		/* Main counter min tick, periodic mode */
	u8 attributes;
};

#if 0				/* HPET flags to be converted to macros */
struct {			/* Flags (8 bits) */
	u8 page_protect:1;	/* 00:    No page protection */
	u8 page_protect4:1;	/* 01:    4_kB page protected */
	u8 page_protect64:1;	/* 02:    64_kB page protected */
	 u8:5;			/* 03-07: Reserved, must be zero */
} flags;
#endif

/*******************************************************************************
 *
 * MADT - Multiple APIC Description Table
 *
 ******************************************************************************/

struct multiple_apic_table {
	ACPI_TABLE_HEADER_DEF u32 local_apic_address;	/* Physical address of local APIC */

	/* Flags (32 bits) */

	u8 PCATcompat:1;	/* 00:    System also has dual 8259s */
	 u8:7;			/* 01-07: Reserved, must be zero */
	u8 reserved1[3];	/* 08-31: Reserved, must be zero */
};

/* Values for MADT PCATCompat */

#define DUAL_PIC                0
#define MULTIPLE_APIC           1

/* Common MADT Sub-table header */

#define APIC_HEADER_DEF \
	u8                              type; \
	u8                              length;

struct apic_header {
APIC_HEADER_DEF};

/* Values for Type in struct apic_header */

#define APIC_PROCESSOR          0
#define APIC_IO                 1
#define APIC_XRUPT_OVERRIDE     2
#define APIC_NMI                3
#define APIC_LOCAL_NMI          4
#define APIC_ADDRESS_OVERRIDE   5
#define APIC_IO_SAPIC           6
#define APIC_LOCAL_SAPIC        7
#define APIC_XRUPT_SOURCE       8
#define APIC_RESERVED           9	/* 9 and greater are reserved */

/* Flag definitions for MADT sub-tables */

#define ACPI_MADT_IFLAGS /* INTI flags (16 bits) */ \
	u8                              polarity        : 2;    /* 00-01: Polarity of APIC I/O input signals */\
	u8                              trigger_mode    : 2;    /* 02-03: Trigger mode of APIC input signals */\
	u8                                              : 4;    /* 04-07: Reserved, must be zero */\
	u8                              reserved1;	/* 08-15: Reserved, must be zero */

#define ACPI_MADT_LFLAGS /* Local Sapic flags (32 bits) */ \
	u8                              processor_enabled: 1;   /* 00:    Processor is usable if set */\
	u8                                              : 7;    /* 01-07: Reserved, must be zero */\
	u8                              reserved2[3];	/* 08-31: Reserved, must be zero */

/* Values for MPS INTI flags */

#define POLARITY_CONFORMS       0
#define POLARITY_ACTIVE_HIGH    1
#define POLARITY_RESERVED       2
#define POLARITY_ACTIVE_LOW     3

#define TRIGGER_CONFORMS        0
#define TRIGGER_EDGE            1
#define TRIGGER_RESERVED        2
#define TRIGGER_LEVEL           3

/*
 * MADT Sub-tables, correspond to Type in struct apic_header
 */

/* 0: processor APIC */

struct madt_processor_apic {
	APIC_HEADER_DEF u8 processor_id;	/* ACPI processor id */
	u8 local_apic_id;	/* Processor's local APIC id */
 ACPI_MADT_LFLAGS};

/* 1: IO APIC */

struct madt_io_apic {
	APIC_HEADER_DEF u8 io_apic_id;	/* I/O APIC ID */
	u8 reserved;		/* Reserved - must be zero */
	u32 address;		/* APIC physical address */
	u32 interrupt;		/* Global system interrupt where INTI lines start */
};

/* 2: Interrupt Override */

struct madt_interrupt_override {
	APIC_HEADER_DEF u8 bus;	/* 0 - ISA */
	u8 source;		/* Interrupt source (IRQ) */
	u32 interrupt;		/* Global system interrupt */
 ACPI_MADT_IFLAGS};

/* 3: NMI Sources */

struct madt_nmi_source {
	APIC_HEADER_DEF ACPI_MADT_IFLAGS u32 interrupt;	/* Global system interrupt */
};

/* 4: Local APIC NMI */

struct madt_local_apic_nmi {
	APIC_HEADER_DEF u8 processor_id;	/* ACPI processor id */
	ACPI_MADT_IFLAGS u8 lint;	/* LINTn to which NMI is connected */
};

/* 5: Address Override */

struct madt_address_override {
	APIC_HEADER_DEF u16 reserved;	/* Reserved, must be zero */
	u64 address;		/* APIC physical address */
};

/* 6: I/O Sapic */

struct madt_io_sapic {
	APIC_HEADER_DEF u8 io_sapic_id;	/* I/O SAPIC ID */
	u8 reserved;		/* Reserved, must be zero */
	u32 interrupt_base;	/* Glocal interrupt for SAPIC start */
	u64 address;		/* SAPIC physical address */
};

/* 7: Local Sapic */

struct madt_local_sapic {
	APIC_HEADER_DEF u8 processor_id;	/* ACPI processor id */
	u8 local_sapic_id;	/* SAPIC ID */
	u8 local_sapic_eid;	/* SAPIC EID */
	u8 reserved[3];		/* Reserved, must be zero */
	 ACPI_MADT_LFLAGS u32 processor_uID;	/* Numeric UID - ACPI 3.0 */
	char processor_uIDstring[1];	/* String UID  - ACPI 3.0 */
};

/* 8: Platform Interrupt Source */

struct madt_interrupt_source {
	APIC_HEADER_DEF ACPI_MADT_IFLAGS u8 interrupt_type;	/* 1=PMI, 2=INIT, 3=corrected */
	u8 processor_id;	/* Processor ID */
	u8 processor_eid;	/* Processor EID */
	u8 io_sapic_vector;	/* Vector value for PMI interrupts */
	u32 interrupt;		/* Global system interrupt */
	u32 flags;		/* Interrupt Source Flags */
};

#ifdef DUPLICATE_DEFINITION_WITH_LINUX_ACPI_H
/*******************************************************************************
 *
 * MCFG - PCI Memory Mapped Configuration table and sub-table
 *
 ******************************************************************************/

struct acpi_table_mcfg {
	ACPI_TABLE_HEADER_DEF u8 reserved[8];
};

struct acpi_mcfg_allocation {
	u64 base_address;	/* Base address, processor-relative */
	u16 pci_segment;	/* PCI segment group number */
	u8 start_bus_number;	/* Starting PCI Bus number */
	u8 end_bus_number;	/* Final PCI Bus number */
	u32 reserved;
};
#endif

/*******************************************************************************
 *
 * SBST - Smart Battery Specification Table
 *
 ******************************************************************************/

struct smart_battery_table {
	ACPI_TABLE_HEADER_DEF u32 warning_level;
	u32 low_level;
	u32 critical_level;
};

/*******************************************************************************
 *
 * SLIT - System Locality Distance Information Table
 *
 ******************************************************************************/

struct system_locality_info {
	ACPI_TABLE_HEADER_DEF u64 locality_count;
	u8 entry[1][1];
};

/*******************************************************************************
 *
 * SPCR - Serial Port Console Redirection table
 *
 ******************************************************************************/

struct acpi_table_spcr {
	ACPI_TABLE_HEADER_DEF u8 interface_type;	/* 0=full 16550, 1=subset of 16550 */
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
	u8 reserved2;
	u16 pci_device_id;
	u16 pci_vendor_id;
	u8 pci_bus;
	u8 pci_device;
	u8 pci_function;
	u32 pci_flags;
	u8 pci_segment;
	u32 reserved3;
};

/*******************************************************************************
 *
 * SPMI - Server Platform Management Interface table
 *
 ******************************************************************************/

struct acpi_table_spmi {
	ACPI_TABLE_HEADER_DEF u8 reserved;
	u8 interface_type;
	u16 spec_revision;	/* Version of IPMI */
	u8 interrupt_type;
	u8 gpe_number;		/* GPE assigned */
	u8 reserved2;
	u8 pci_device_flag;
	u32 interrupt;
	struct acpi_generic_address ipmi_register;
	u8 pci_segment;
	u8 pci_bus;
	u8 pci_device;
	u8 pci_function;
};

/*******************************************************************************
 *
 * SRAT - System Resource Affinity Table
 *
 ******************************************************************************/

struct system_resource_affinity {
	ACPI_TABLE_HEADER_DEF u32 reserved1;	/* Must be value '1' */
	u64 reserved2;		/* Reserved, must be zero */
};

/* SRAT common sub-table header */

#define SRAT_SUBTABLE_HEADER \
	u8                              type; \
	u8                              length;

/* Values for Type above */

#define SRAT_CPU_AFFINITY       0
#define SRAT_MEMORY_AFFINITY    1
#define SRAT_RESERVED           2

/* SRAT sub-tables */

struct static_resource_alloc {
	SRAT_SUBTABLE_HEADER u8 proximity_domain_lo;
	u8 apic_id;

	/* Flags (32 bits) */

	u8 enabled:1;		/* 00:    Use affinity structure */
	 u8:7;			/* 01-07: Reserved, must be zero */
	u8 reserved3[3];	/* 08-31: Reserved, must be zero */

	u8 local_sapic_eid;
	u8 proximity_domain_hi[3];
	u32 reserved4;		/* Reserved, must be zero */
};

struct memory_affinity {
	SRAT_SUBTABLE_HEADER u32 proximity_domain;
	u16 reserved3;
	u64 base_address;
	u64 address_length;
	u32 reserved4;

	/* Flags (32 bits) */

	u8 enabled:1;		/* 00:    Use affinity structure */
	u8 hot_pluggable:1;	/* 01:    Memory region is hot pluggable */
	u8 non_volatile:1;	/* 02:    Memory is non-volatile */
	 u8:5;			/* 03-07: Reserved, must be zero */
	u8 reserved5[3];	/* 08-31: Reserved, must be zero */

	u64 reserved6;		/* Reserved, must be zero */
};

/*******************************************************************************
 *
 * TCPA - Trusted Computing Platform Alliance table
 *
 ******************************************************************************/

struct acpi_table_tcpa {
	ACPI_TABLE_HEADER_DEF u16 reserved;
	u32 max_log_length;	/* Maximum length for the event log area */
	u64 log_address;	/* Address of the event log area */
};

/*******************************************************************************
 *
 * WDRT - Watchdog Resource Table
 *
 ******************************************************************************/

struct acpi_table_wdrt {
	ACPI_TABLE_HEADER_DEF u32 header_length;	/* Watchdog Header Length */
	u8 pci_segment;		/* PCI Segment number */
	u8 pci_bus;		/* PCI Bus number */
	u8 pci_device;		/* PCI Device number */
	u8 pci_function;	/* PCI Function number */
	u32 timer_period;	/* Period of one timer count (msec) */
	u32 max_count;		/* Maximum counter value supported */
	u32 min_count;		/* Minimum counter value */
	u8 flags;
	u8 reserved[3];
	u32 entries;		/* Number of watchdog entries that follow */
};

#if 0				/* Flags, will be converted to macros */
u8 enabled:1;			/* 00:    Timer enabled */
u8:6;				/* 01-06: Reserved */
u8 sleep_stop:1;		/* 07:    Timer stopped in sleep state */
#endif

/* Macros used to generate offsets to specific table fields */

#define ACPI_ASF0_OFFSET(f)             (u8) ACPI_OFFSET (struct acpi_asf_info,f)
#define ACPI_ASF1_OFFSET(f)             (u8) ACPI_OFFSET (struct acpi_asf_alert,f)
#define ACPI_ASF2_OFFSET(f)             (u8) ACPI_OFFSET (struct acpi_asf_remote,f)
#define ACPI_ASF3_OFFSET(f)             (u8) ACPI_OFFSET (struct acpi_asf_rmcp,f)
#define ACPI_ASF4_OFFSET(f)             (u8) ACPI_OFFSET (struct acpi_asf_address,f)
#define ACPI_BOOT_OFFSET(f)             (u8) ACPI_OFFSET (struct acpi_table_boot,f)
#define ACPI_CPEP_OFFSET(f)             (u8) ACPI_OFFSET (struct acpi_table_cpep,f)
#define ACPI_CPEP0_OFFSET(f)            (u8) ACPI_OFFSET (struct acpi_cpep_polling,f)
#define ACPI_DBGP_OFFSET(f)             (u8) ACPI_OFFSET (struct acpi_table_dbgp,f)
#define ACPI_ECDT_OFFSET(f)             (u8) ACPI_OFFSET (struct ec_boot_resources,f)
#define ACPI_HPET_OFFSET(f)             (u8) ACPI_OFFSET (struct hpet_table,f)
#define ACPI_MADT_OFFSET(f)             (u8) ACPI_OFFSET (struct multiple_apic_table,f)
#define ACPI_MADT0_OFFSET(f)            (u8) ACPI_OFFSET (struct madt_processor_apic,f)
#define ACPI_MADT1_OFFSET(f)            (u8) ACPI_OFFSET (struct madt_io_apic,f)
#define ACPI_MADT2_OFFSET(f)            (u8) ACPI_OFFSET (struct madt_interrupt_override,f)
#define ACPI_MADT3_OFFSET(f)            (u8) ACPI_OFFSET (struct madt_nmi_source,f)
#define ACPI_MADT4_OFFSET(f)            (u8) ACPI_OFFSET (struct madt_local_apic_nmi,f)
#define ACPI_MADT5_OFFSET(f)            (u8) ACPI_OFFSET (struct madt_address_override,f)
#define ACPI_MADT6_OFFSET(f)            (u8) ACPI_OFFSET (struct madt_io_sapic,f)
#define ACPI_MADT7_OFFSET(f)            (u8) ACPI_OFFSET (struct madt_local_sapic,f)
#define ACPI_MADT8_OFFSET(f)            (u8) ACPI_OFFSET (struct madt_interrupt_source,f)
#define ACPI_MADTH_OFFSET(f)            (u8) ACPI_OFFSET (struct apic_header,f)
#define ACPI_MCFG_OFFSET(f)             (u8) ACPI_OFFSET (struct acpi_table_mcfg,f)
#define ACPI_MCFG0_OFFSET(f)            (u8) ACPI_OFFSET (struct acpi_mcfg_allocation,f)
#define ACPI_SBST_OFFSET(f)             (u8) ACPI_OFFSET (struct smart_battery_table,f)
#define ACPI_SLIT_OFFSET(f)             (u8) ACPI_OFFSET (struct system_locality_info,f)
#define ACPI_SPCR_OFFSET(f)             (u8) ACPI_OFFSET (struct acpi_table_spcr,f)
#define ACPI_SPMI_OFFSET(f)             (u8) ACPI_OFFSET (struct acpi_table_spmi,f)
#define ACPI_SRAT_OFFSET(f)             (u8) ACPI_OFFSET (struct system_resource_affinity,f)
#define ACPI_SRAT0_OFFSET(f)            (u8) ACPI_OFFSET (struct static_resource_alloc,f)
#define ACPI_SRAT1_OFFSET(f)            (u8) ACPI_OFFSET (struct memory_affinity,f)
#define ACPI_TCPA_OFFSET(f)             (u8) ACPI_OFFSET (struct acpi_table_tcpa,f)
#define ACPI_WDRT_OFFSET(f)             (u8) ACPI_OFFSET (struct acpi_table_wdrt,f)

#define ACPI_HPET_FLAG_OFFSET(f,o)      ACPI_FLAG_OFFSET (struct hpet_table,f,o)
#define ACPI_SRAT0_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (struct static_resource_alloc,f,o)
#define ACPI_SRAT1_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (struct memory_affinity,f,o)
#define ACPI_MADT_FLAG_OFFSET(f,o)      ACPI_FLAG_OFFSET (struct multiple_apic_table,f,o)
#define ACPI_MADT0_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (struct madt_processor_apic,f,o)
#define ACPI_MADT2_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (struct madt_interrupt_override,f,o)
#define ACPI_MADT3_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (struct madt_nmi_source,f,o)
#define ACPI_MADT4_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (struct madt_local_apic_nmi,f,o)
#define ACPI_MADT7_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (struct madt_local_sapic,f,o)
#define ACPI_MADT8_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (struct madt_interrupt_source,f,o)

/* Reset to default packing */

#pragma pack()

#endif				/* __ACTBL1_H__ */
