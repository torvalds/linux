/******************************************************************************
 *
 * Name: actbl.h - Basic ACPI Table Definitions
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2013, Intel Corp.
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

/*******************************************************************************
 *
 * Fundamental ACPI tables
 *
 * This file contains definitions for the ACPI tables that are directly consumed
 * by ACPICA. All other tables are consumed by the OS-dependent ACPI-related
 * device drivers and other OS support code.
 *
 * The RSDP and FACS do not use the common ACPI table header. All other ACPI
 * tables use the header.
 *
 ******************************************************************************/

/*
 * Values for description table header signatures for tables defined in this
 * file. Useful because they make it more difficult to inadvertently type in
 * the wrong signature.
 */
#define ACPI_SIG_DSDT           "DSDT"	/* Differentiated System Description Table */
#define ACPI_SIG_FADT           "FACP"	/* Fixed ACPI Description Table */
#define ACPI_SIG_FACS           "FACS"	/* Firmware ACPI Control Structure */
#define ACPI_SIG_PSDT           "PSDT"	/* Persistent System Description Table */
#define ACPI_SIG_RSDP           "RSD PTR "	/* Root System Description Pointer */
#define ACPI_SIG_RSDT           "RSDT"	/* Root System Description Table */
#define ACPI_SIG_XSDT           "XSDT"	/* Extended  System Description Table */
#define ACPI_SIG_SSDT           "SSDT"	/* Secondary System Description Table */
#define ACPI_RSDP_NAME          "RSDP"	/* Short name for RSDP, not signature */

/*
 * All tables and structures must be byte-packed to match the ACPI
 * specification, since the tables are provided by the system BIOS
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
 * Master ACPI Table Header. This common header is used by all ACPI tables
 * except the RSDP and FACS.
 *
 ******************************************************************************/

struct acpi_table_header {
	char signature[ACPI_NAME_SIZE];	/* ASCII table signature */
	u32 length;		/* Length of table in bytes, including this header */
	u8 revision;		/* ACPI Specification minor version number */
	u8 checksum;		/* To make sum of entire table == 0 */
	char oem_id[ACPI_OEM_ID_SIZE];	/* ASCII OEM identification */
	char oem_table_id[ACPI_OEM_TABLE_ID_SIZE];	/* ASCII OEM table identification */
	u32 oem_revision;	/* OEM revision number */
	char asl_compiler_id[ACPI_NAME_SIZE];	/* ASCII ASL compiler vendor ID */
	u32 asl_compiler_revision;	/* ASL compiler version */
};

/*******************************************************************************
 *
 * GAS - Generic Address Structure (ACPI 2.0+)
 *
 * Note: Since this structure is used in the ACPI tables, it is byte aligned.
 * If misaligned access is not supported by the hardware, accesses to the
 * 64-bit Address field must be performed with care.
 *
 ******************************************************************************/

struct acpi_generic_address {
	u8 space_id;		/* Address space where struct or register exists */
	u8 bit_width;		/* Size in bits of given register */
	u8 bit_offset;		/* Bit offset within the register */
	u8 access_width;	/* Minimum Access size (ACPI 3.0) */
	u64 address;		/* 64-bit address of struct or register */
};

/*******************************************************************************
 *
 * RSDP - Root System Description Pointer (Signature is "RSD PTR ")
 *        Version 2
 *
 ******************************************************************************/

struct acpi_table_rsdp {
	char signature[8];	/* ACPI signature, contains "RSD PTR " */
	u8 checksum;		/* ACPI 1.0 checksum */
	char oem_id[ACPI_OEM_ID_SIZE];	/* OEM identification */
	u8 revision;		/* Must be (0) for ACPI 1.0 or (2) for ACPI 2.0+ */
	u32 rsdt_physical_address;	/* 32-bit physical address of the RSDT */
	u32 length;		/* Table length in bytes, including header (ACPI 2.0+) */
	u64 xsdt_physical_address;	/* 64-bit physical address of the XSDT (ACPI 2.0+) */
	u8 extended_checksum;	/* Checksum of entire table (ACPI 2.0+) */
	u8 reserved[3];		/* Reserved, must be zero */
};

#define ACPI_RSDP_REV0_SIZE     20	/* Size of original ACPI 1.0 RSDP */

/*******************************************************************************
 *
 * RSDT/XSDT - Root System Description Tables
 *             Version 1 (both)
 *
 ******************************************************************************/

struct acpi_table_rsdt {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 table_offset_entry[1];	/* Array of pointers to ACPI tables */
};

struct acpi_table_xsdt {
	struct acpi_table_header header;	/* Common ACPI table header */
	u64 table_offset_entry[1];	/* Array of pointers to ACPI tables */
};

/*******************************************************************************
 *
 * FACS - Firmware ACPI Control Structure (FACS)
 *
 ******************************************************************************/

struct acpi_table_facs {
	char signature[4];	/* ASCII table signature */
	u32 length;		/* Length of structure, in bytes */
	u32 hardware_signature;	/* Hardware configuration signature */
	u32 firmware_waking_vector;	/* 32-bit physical address of the Firmware Waking Vector */
	u32 global_lock;	/* Global Lock for shared hardware resources */
	u32 flags;
	u64 xfirmware_waking_vector;	/* 64-bit version of the Firmware Waking Vector (ACPI 2.0+) */
	u8 version;		/* Version of this table (ACPI 2.0+) */
	u8 reserved[3];		/* Reserved, must be zero */
	u32 ospm_flags;		/* Flags to be set by OSPM (ACPI 4.0) */
	u8 reserved1[24];	/* Reserved, must be zero */
};

/* Masks for global_lock flag field above */

#define ACPI_GLOCK_PENDING          (1)	/* 00: Pending global lock ownership */
#define ACPI_GLOCK_OWNED            (1<<1)	/* 01: Global lock is owned */

/* Masks for Flags field above  */

#define ACPI_FACS_S4_BIOS_PRESENT   (1)	/* 00: S4BIOS support is present */
#define ACPI_FACS_64BIT_WAKE        (1<<1)	/* 01: 64-bit wake vector supported (ACPI 4.0) */

/* Masks for ospm_flags field above */

#define ACPI_FACS_64BIT_ENVIRONMENT (1)	/* 00: 64-bit wake environment is required (ACPI 4.0) */

/*******************************************************************************
 *
 * FADT - Fixed ACPI Description Table (Signature "FACP")
 *        Version 4
 *
 ******************************************************************************/

/* Fields common to all versions of the FADT */

struct acpi_table_fadt {
	struct acpi_table_header header;	/* Common ACPI table header */
	u32 facs;		/* 32-bit physical address of FACS */
	u32 dsdt;		/* 32-bit physical address of DSDT */
	u8 model;		/* System Interrupt Model (ACPI 1.0) - not used in ACPI 2.0+ */
	u8 preferred_profile;	/* Conveys preferred power management profile to OSPM. */
	u16 sci_interrupt;	/* System vector of SCI interrupt */
	u32 smi_command;	/* 32-bit Port address of SMI command port */
	u8 acpi_enable;		/* Value to write to SMI_CMD to enable ACPI */
	u8 acpi_disable;	/* Value to write to SMI_CMD to disable ACPI */
	u8 s4_bios_request;	/* Value to write to SMI_CMD to enter S4BIOS state */
	u8 pstate_control;	/* Processor performance state control */
	u32 pm1a_event_block;	/* 32-bit port address of Power Mgt 1a Event Reg Blk */
	u32 pm1b_event_block;	/* 32-bit port address of Power Mgt 1b Event Reg Blk */
	u32 pm1a_control_block;	/* 32-bit port address of Power Mgt 1a Control Reg Blk */
	u32 pm1b_control_block;	/* 32-bit port address of Power Mgt 1b Control Reg Blk */
	u32 pm2_control_block;	/* 32-bit port address of Power Mgt 2 Control Reg Blk */
	u32 pm_timer_block;	/* 32-bit port address of Power Mgt Timer Ctrl Reg Blk */
	u32 gpe0_block;		/* 32-bit port address of General Purpose Event 0 Reg Blk */
	u32 gpe1_block;		/* 32-bit port address of General Purpose Event 1 Reg Blk */
	u8 pm1_event_length;	/* Byte Length of ports at pm1x_event_block */
	u8 pm1_control_length;	/* Byte Length of ports at pm1x_control_block */
	u8 pm2_control_length;	/* Byte Length of ports at pm2_control_block */
	u8 pm_timer_length;	/* Byte Length of ports at pm_timer_block */
	u8 gpe0_block_length;	/* Byte Length of ports at gpe0_block */
	u8 gpe1_block_length;	/* Byte Length of ports at gpe1_block */
	u8 gpe1_base;		/* Offset in GPE number space where GPE1 events start */
	u8 cst_control;		/* Support for the _CST object and C-States change notification */
	u16 c2_latency;		/* Worst case HW latency to enter/exit C2 state */
	u16 c3_latency;		/* Worst case HW latency to enter/exit C3 state */
	u16 flush_size;		/* Processor memory cache line width, in bytes */
	u16 flush_stride;	/* Number of flush strides that need to be read */
	u8 duty_offset;		/* Processor duty cycle index in processor P_CNT reg */
	u8 duty_width;		/* Processor duty cycle value bit width in P_CNT register */
	u8 day_alarm;		/* Index to day-of-month alarm in RTC CMOS RAM */
	u8 month_alarm;		/* Index to month-of-year alarm in RTC CMOS RAM */
	u8 century;		/* Index to century in RTC CMOS RAM */
	u16 boot_flags;		/* IA-PC Boot Architecture Flags (see below for individual flags) */
	u8 reserved;		/* Reserved, must be zero */
	u32 flags;		/* Miscellaneous flag bits (see below for individual flags) */
	struct acpi_generic_address reset_register;	/* 64-bit address of the Reset register */
	u8 reset_value;		/* Value to write to the reset_register port to reset the system */
	u8 reserved4[3];	/* Reserved, must be zero */
	u64 Xfacs;		/* 64-bit physical address of FACS */
	u64 Xdsdt;		/* 64-bit physical address of DSDT */
	struct acpi_generic_address xpm1a_event_block;	/* 64-bit Extended Power Mgt 1a Event Reg Blk address */
	struct acpi_generic_address xpm1b_event_block;	/* 64-bit Extended Power Mgt 1b Event Reg Blk address */
	struct acpi_generic_address xpm1a_control_block;	/* 64-bit Extended Power Mgt 1a Control Reg Blk address */
	struct acpi_generic_address xpm1b_control_block;	/* 64-bit Extended Power Mgt 1b Control Reg Blk address */
	struct acpi_generic_address xpm2_control_block;	/* 64-bit Extended Power Mgt 2 Control Reg Blk address */
	struct acpi_generic_address xpm_timer_block;	/* 64-bit Extended Power Mgt Timer Ctrl Reg Blk address */
	struct acpi_generic_address xgpe0_block;	/* 64-bit Extended General Purpose Event 0 Reg Blk address */
	struct acpi_generic_address xgpe1_block;	/* 64-bit Extended General Purpose Event 1 Reg Blk address */
	struct acpi_generic_address sleep_control;	/* 64-bit Sleep Control register (ACPI 5.0) */
	struct acpi_generic_address sleep_status;	/* 64-bit Sleep Status register (ACPI 5.0) */
};

/* Masks for FADT Boot Architecture Flags (boot_flags) [Vx]=Introduced in this FADT revision */

#define ACPI_FADT_LEGACY_DEVICES    (1)  	/* 00: [V2] System has LPC or ISA bus devices */
#define ACPI_FADT_8042              (1<<1)	/* 01: [V3] System has an 8042 controller on port 60/64 */
#define ACPI_FADT_NO_VGA            (1<<2)	/* 02: [V4] It is not safe to probe for VGA hardware */
#define ACPI_FADT_NO_MSI            (1<<3)	/* 03: [V4] Message Signaled Interrupts (MSI) must not be enabled */
#define ACPI_FADT_NO_ASPM           (1<<4)	/* 04: [V4] PCIe ASPM control must not be enabled */
#define ACPI_FADT_NO_CMOS_RTC       (1<<5)	/* 05: [V5] No CMOS real-time clock present */

#define FADT2_REVISION_ID               3

/* Masks for FADT flags */

#define ACPI_FADT_WBINVD            (1)	/* 00: [V1] The WBINVD instruction works properly */
#define ACPI_FADT_WBINVD_FLUSH      (1<<1)	/* 01: [V1] WBINVD flushes but does not invalidate caches */
#define ACPI_FADT_C1_SUPPORTED      (1<<2)	/* 02: [V1] All processors support C1 state */
#define ACPI_FADT_C2_MP_SUPPORTED   (1<<3)	/* 03: [V1] C2 state works on MP system */
#define ACPI_FADT_POWER_BUTTON      (1<<4)	/* 04: [V1] Power button is handled as a control method device */
#define ACPI_FADT_SLEEP_BUTTON      (1<<5)	/* 05: [V1] Sleep button is handled as a control method device */
#define ACPI_FADT_FIXED_RTC         (1<<6)	/* 06: [V1] RTC wakeup status is not in fixed register space */
#define ACPI_FADT_S4_RTC_WAKE       (1<<7)	/* 07: [V1] RTC alarm can wake system from S4 */
#define ACPI_FADT_32BIT_TIMER       (1<<8)	/* 08: [V1] ACPI timer width is 32-bit (0=24-bit) */
#define ACPI_FADT_DOCKING_SUPPORTED (1<<9)	/* 09: [V1] Docking supported */
#define ACPI_FADT_RESET_REGISTER    (1<<10)	/* 10: [V2] System reset via the FADT RESET_REG supported */
#define ACPI_FADT_SEALED_CASE       (1<<11)	/* 11: [V3] No internal expansion capabilities and case is sealed */
#define ACPI_FADT_HEADLESS          (1<<12)	/* 12: [V3] No local video capabilities or local input devices */
#define ACPI_FADT_SLEEP_TYPE        (1<<13)	/* 13: [V3] Must execute native instruction after writing  SLP_TYPx register */
#define ACPI_FADT_PCI_EXPRESS_WAKE  (1<<14)	/* 14: [V4] System supports PCIEXP_WAKE (STS/EN) bits (ACPI 3.0) */
#define ACPI_FADT_PLATFORM_CLOCK    (1<<15)	/* 15: [V4] OSPM should use platform-provided timer (ACPI 3.0) */
#define ACPI_FADT_S4_RTC_VALID      (1<<16)	/* 16: [V4] Contents of RTC_STS valid after S4 wake (ACPI 3.0) */
#define ACPI_FADT_REMOTE_POWER_ON   (1<<17)	/* 17: [V4] System is compatible with remote power on (ACPI 3.0) */
#define ACPI_FADT_APIC_CLUSTER      (1<<18)	/* 18: [V4] All local APICs must use cluster model (ACPI 3.0) */
#define ACPI_FADT_APIC_PHYSICAL     (1<<19)	/* 19: [V4] All local xAPICs must use physical dest mode (ACPI 3.0) */
#define ACPI_FADT_HW_REDUCED        (1<<20)	/* 20: [V5] ACPI hardware is not implemented (ACPI 5.0) */
#define ACPI_FADT_LOW_POWER_S0      (1<<21)	/* 21: [V5] S0 power savings are equal or better than S3 (ACPI 5.0) */

/* Values for preferred_profile (Preferred Power Management Profiles) */

enum acpi_preferred_pm_profiles {
	PM_UNSPECIFIED = 0,
	PM_DESKTOP = 1,
	PM_MOBILE = 2,
	PM_WORKSTATION = 3,
	PM_ENTERPRISE_SERVER = 4,
	PM_SOHO_SERVER = 5,
	PM_APPLIANCE_PC = 6,
	PM_PERFORMANCE_SERVER = 7,
	PM_TABLET = 8
};

/* Values for sleep_status and sleep_control registers (V5 FADT) */

#define ACPI_X_WAKE_STATUS          0x80
#define ACPI_X_SLEEP_TYPE_MASK      0x1C
#define ACPI_X_SLEEP_TYPE_POSITION  0x02
#define ACPI_X_SLEEP_ENABLE         0x20

/* Reset to default packing */

#pragma pack()

/*
 * Internal table-related structures
 */
union acpi_name_union {
	u32 integer;
	char ascii[4];
};

/* Internal ACPI Table Descriptor. One per ACPI table. */

struct acpi_table_desc {
	acpi_physical_address address;
	struct acpi_table_header *pointer;
	u32 length;		/* Length fixed at 32 bits (fixed in table header) */
	union acpi_name_union signature;
	acpi_owner_id owner_id;
	u8 flags;
};

/* Masks for Flags field above */

#define ACPI_TABLE_ORIGIN_UNKNOWN       (0)
#define ACPI_TABLE_ORIGIN_MAPPED        (1)
#define ACPI_TABLE_ORIGIN_ALLOCATED     (2)
#define ACPI_TABLE_ORIGIN_OVERRIDE      (4)
#define ACPI_TABLE_ORIGIN_MASK          (7)
#define ACPI_TABLE_IS_LOADED            (8)

/*
 * Get the remaining ACPI tables
 */
#include <acpi/actbl1.h>
#include <acpi/actbl2.h>
#include <acpi/actbl3.h>

/* Macros used to generate offsets to specific table fields */

#define ACPI_FADT_OFFSET(f)             (u16) ACPI_OFFSET (struct acpi_table_fadt, f)

/*
 * Sizes of the various flavors of FADT. We need to look closely
 * at the FADT length because the version number essentially tells
 * us nothing because of many BIOS bugs where the version does not
 * match the expected length. In other words, the length of the
 * FADT is the bottom line as to what the version really is.
 *
 * For reference, the values below are as follows:
 *     FADT V1  size: 0x074
 *     FADT V2  size: 0x084
 *     FADT V3  size: 0x0F4
 *     FADT V4  size: 0x0F4
 *     FADT V5  size: 0x10C
 */
#define ACPI_FADT_V1_SIZE       (u32) (ACPI_FADT_OFFSET (flags) + 4)
#define ACPI_FADT_V2_SIZE       (u32) (ACPI_FADT_OFFSET (reserved4[0]) + 3)
#define ACPI_FADT_V3_SIZE       (u32) (ACPI_FADT_OFFSET (sleep_control))
#define ACPI_FADT_V5_SIZE       (u32) (sizeof (struct acpi_table_fadt))

#endif				/* __ACTBL_H__ */
