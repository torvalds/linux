/******************************************************************************
 *
 * Name: actbl.h - Table data structures defined in ACPI specification
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

#ifndef __ACTBL_H__
#define __ACTBL_H__

/*
 * Note about bitfields: The u8 type is used for bitfields in ACPI tables.
 * This is the only type that is even remotely portable. Anything else is not
 * portable, so do not use any other bitfield types.
 */

/*
 *  Values for description table header signatures
 */
#define RSDP_NAME               "RSDP"
#define RSDP_SIG                "RSD PTR "	/* RSDT Pointer signature */
#define APIC_SIG                "APIC"	/* Multiple APIC Description Table */
#define DSDT_SIG                "DSDT"	/* Differentiated System Description Table */
#define FADT_SIG                "FACP"	/* Fixed ACPI Description Table */
#define FACS_SIG                "FACS"	/* Firmware ACPI Control Structure */
#define PSDT_SIG                "PSDT"	/* Persistent System Description Table */
#define RSDT_SIG                "RSDT"	/* Root System Description Table */
#define XSDT_SIG                "XSDT"	/* Extended  System Description Table */
#define SSDT_SIG                "SSDT"	/* Secondary System Description Table */
#define SBST_SIG                "SBST"	/* Smart Battery Specification Table */
#define SPIC_SIG                "SPIC"	/* IOSAPIC table */
#define BOOT_SIG                "BOOT"	/* Boot table */

#define GL_OWNED                0x02	/* Ownership of global lock is bit 1 */

/*
 * Common table types.  The base code can remain
 * constant if the underlying tables are changed
 */
#define RSDT_DESCRIPTOR         struct rsdt_descriptor_rev2
#define XSDT_DESCRIPTOR         struct xsdt_descriptor_rev2
#define FACS_DESCRIPTOR         struct facs_descriptor_rev2
#define FADT_DESCRIPTOR         struct fadt_descriptor_rev2

#pragma pack(1)

/*
 * ACPI Version-independent tables
 *
 * NOTE: The tables that are specific to ACPI versions (1.0, 2.0, etc.)
 * are in separate files.
 */
struct rsdp_descriptor {	/* Root System Descriptor Pointer */
	char signature[8];	/* ACPI signature, contains "RSD PTR " */
	u8 checksum;		/* ACPI 1.0 checksum */
	char oem_id[6];		/* OEM identification */
	u8 revision;		/* Must be (0) for ACPI 1.0 or (2) for ACPI 2.0+ */
	u32 rsdt_physical_address;	/* 32-bit physical address of the RSDT */
	u32 length;		/* XSDT Length in bytes, including header */
	u64 xsdt_physical_address;	/* 64-bit physical address of the XSDT */
	u8 extended_checksum;	/* Checksum of entire table (ACPI 2.0) */
	char reserved[3];	/* Reserved, must be zero */
};

struct acpi_common_facs {	/* Common FACS for internal use */
	u32 *global_lock;
	u64 *firmware_waking_vector;
	u8 vector_width;
};

#define ACPI_TABLE_HEADER_DEF   /* ACPI common table header */ \
	char                            signature[4];           /* ASCII table signature */\
	u32                             length;                 /* Length of table in bytes, including this header */\
	u8                              revision;               /* ACPI Specification minor version # */\
	u8                              checksum;               /* To make sum of entire table == 0 */\
	char                            oem_id[6];              /* ASCII OEM identification */\
	char                            oem_table_id[8];        /* ASCII OEM table identification */\
	u32                             oem_revision;           /* OEM revision number */\
	char                            asl_compiler_id [4];    /* ASCII ASL compiler vendor ID */\
	u32                             asl_compiler_revision;	/* ASL compiler version */

struct acpi_table_header {	/* ACPI common table header */
ACPI_TABLE_HEADER_DEF};

/*
 * MADT values and structures
 */

/* Values for MADT PCATCompat */

#define DUAL_PIC                0
#define MULTIPLE_APIC           1

/* Master MADT */

struct multiple_apic_table {
	ACPI_TABLE_HEADER_DEF	/* ACPI common table header */
	u32 local_apic_address;	/* Physical address of local APIC */

	/* Flags (32 bits) */

	u8 PCATcompat:1;	/* 00:    System also has dual 8259s */
	 u8:7;			/* 01-07: Reserved, must be zero */
	u8 reserved1[3];	/* 08-31: Reserved, must be zero */
};

/* Values for Type in APIC_HEADER_DEF */

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

/*
 * MADT sub-structures (Follow MULTIPLE_APIC_DESCRIPTION_TABLE)
 */
#define APIC_HEADER_DEF                     /* Common APIC sub-structure header */\
	u8                              type; \
	u8                              length;

struct apic_header {
APIC_HEADER_DEF};

/* Values for MPS INTI flags */

#define POLARITY_CONFORMS       0
#define POLARITY_ACTIVE_HIGH    1
#define POLARITY_RESERVED       2
#define POLARITY_ACTIVE_LOW     3

#define TRIGGER_CONFORMS        0
#define TRIGGER_EDGE            1
#define TRIGGER_RESERVED        2
#define TRIGGER_LEVEL           3

/* Common flag definitions (16 bits each) */

#define MPS_INTI_FLAGS \
	u8                              polarity        : 2;    /* 00-01: Polarity of APIC I/O input signals */\
	u8                              trigger_mode    : 2;    /* 02-03: Trigger mode of APIC input signals */\
	u8                                              : 4;    /* 04-07: Reserved, must be zero */\
	u8                              reserved1;	/* 08-15: Reserved, must be zero */

#define LOCAL_APIC_FLAGS \
	u8                              processor_enabled: 1;   /* 00:    Processor is usable if set */\
	u8                                              : 7;    /* 01-07: Reserved, must be zero */\
	u8                              reserved2;	/* 08-15: Reserved, must be zero */

/* Sub-structures for MADT */

struct madt_processor_apic {
	APIC_HEADER_DEF u8 processor_id;	/* ACPI processor id */
	u8 local_apic_id;	/* Processor's local APIC id */
 LOCAL_APIC_FLAGS};

struct madt_io_apic {
	APIC_HEADER_DEF u8 io_apic_id;	/* I/O APIC ID */
	u8 reserved;		/* Reserved - must be zero */
	u32 address;		/* APIC physical address */
	u32 interrupt;		/* Global system interrupt where INTI
				 * lines start */
};

struct madt_interrupt_override {
	APIC_HEADER_DEF u8 bus;	/* 0 - ISA */
	u8 source;		/* Interrupt source (IRQ) */
	u32 interrupt;		/* Global system interrupt */
 MPS_INTI_FLAGS};

struct madt_nmi_source {
	APIC_HEADER_DEF MPS_INTI_FLAGS u32 interrupt;	/* Global system interrupt */
};

struct madt_local_apic_nmi {
	APIC_HEADER_DEF u8 processor_id;	/* ACPI processor id */
	MPS_INTI_FLAGS u8 lint;	/* LINTn to which NMI is connected */
};

struct madt_address_override {
	APIC_HEADER_DEF u16 reserved;	/* Reserved, must be zero */
	u64 address;		/* APIC physical address */
};

struct madt_io_sapic {
	APIC_HEADER_DEF u8 io_sapic_id;	/* I/O SAPIC ID */
	u8 reserved;		/* Reserved, must be zero */
	u32 interrupt_base;	/* Glocal interrupt for SAPIC start */
	u64 address;		/* SAPIC physical address */
};

struct madt_local_sapic {
	APIC_HEADER_DEF u8 processor_id;	/* ACPI processor id */
	u8 local_sapic_id;	/* SAPIC ID */
	u8 local_sapic_eid;	/* SAPIC EID */
	u8 reserved[3];		/* Reserved, must be zero */
	 LOCAL_APIC_FLAGS u32 processor_uID;	/* Numeric UID - ACPI 3.0 */
	char processor_uIDstring[1];	/* String UID  - ACPI 3.0 */
};

struct madt_interrupt_source {
	APIC_HEADER_DEF MPS_INTI_FLAGS u8 interrupt_type;	/* 1=PMI, 2=INIT, 3=corrected */
	u8 processor_id;	/* Processor ID */
	u8 processor_eid;	/* Processor EID */
	u8 io_sapic_vector;	/* Vector value for PMI interrupts */
	u32 interrupt;		/* Global system interrupt */
	u32 flags;		/* Interrupt Source Flags */
};

/*
 * Smart Battery
 */
struct smart_battery_table {
	ACPI_TABLE_HEADER_DEF u32 warning_level;
	u32 low_level;
	u32 critical_level;
};

#pragma pack()

/*
 * ACPI Table information.  We save the table address, length,
 * and type of memory allocation (mapped or allocated) for each
 * table for 1) when we exit, and 2) if a new table is installed
 */
#define ACPI_MEM_NOT_ALLOCATED  0
#define ACPI_MEM_ALLOCATED      1
#define ACPI_MEM_MAPPED         2

/* Definitions for the Flags bitfield member of struct acpi_table_support */

#define ACPI_TABLE_SINGLE       0x00
#define ACPI_TABLE_MULTIPLE     0x01
#define ACPI_TABLE_EXECUTABLE   0x02

#define ACPI_TABLE_ROOT         0x00
#define ACPI_TABLE_PRIMARY      0x10
#define ACPI_TABLE_SECONDARY    0x20
#define ACPI_TABLE_ALL          0x30
#define ACPI_TABLE_TYPE_MASK    0x30

/* Data about each known table type */

struct acpi_table_support {
	char *name;
	char *signature;
	void **global_ptr;
	u8 sig_length;
	u8 flags;
};

/*
 * Get the ACPI version-specific tables
 */
#include "actbl1.h"		/* Acpi 1.0 table definitions */
#include "actbl2.h"		/* Acpi 2.0 table definitions */

extern u8 acpi_fadt_is_v1;	/* is set to 1 if FADT is revision 1,
				 * needed for certain workarounds */

#pragma pack(1)
/*
 * High performance timer
 */
struct hpet_table {
	ACPI_TABLE_HEADER_DEF u32 hardware_id;
	struct acpi_generic_address base_address;
	u8 hpet_number;
	u16 clock_tick;
	u8 attributes;
};

#pragma pack()

#endif				/* __ACTBL_H__ */
