/******************************************************************************
 *
 * Name: actbl.h - Basic ACPI Table Definitions
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
 * Values for description table header signatures. Useful because they make
 * it more difficult to inadvertently type in the wrong signature.
 */
#define DSDT_SIG                "DSDT"	/* Differentiated System Description Table */
#define FADT_SIG                "FACP"	/* Fixed ACPI Description Table */
#define FACS_SIG                "FACS"	/* Firmware ACPI Control Structure */
#define PSDT_SIG                "PSDT"	/* Persistent System Description Table */
#define RSDP_SIG                "RSD PTR "	/* Root System Description Pointer */
#define RSDT_SIG                "RSDT"	/* Root System Description Table */
#define XSDT_SIG                "XSDT"	/* Extended  System Description Table */
#define SSDT_SIG                "SSDT"	/* Secondary System Description Table */
#define RSDP_NAME               "RSDP"

/*
 * All tables and structures must be byte-packed to match the ACPI
 * specification, since the tables are provided by the system BIOS
 */
#pragma pack(1)

/*
 * These are the ACPI tables that are directly consumed by the subsystem.
 *
 * The RSDP and FACS do not use the common ACPI table header. All other ACPI
 * tables use the header.
 *
 * Note about bitfields: The u8 type is used for bitfields in ACPI tables.
 * This is the only type that is even remotely portable. Anything else is not
 * portable, so do not use any other bitfield types.
 */

/*******************************************************************************
 *
 * ACPI Table Header. This common header is used by all tables except the
 * RSDP and FACS. The define is used for direct inclusion of header into
 * other ACPI tables
 *
 ******************************************************************************/

#define ACPI_TABLE_HEADER_DEF \
	char                            signature[4];           /* ASCII table signature */\
	u32                             length;                 /* Length of table in bytes, including this header */\
	u8                              revision;               /* ACPI Specification minor version # */\
	u8                              checksum;               /* To make sum of entire table == 0 */\
	char                            oem_id[6];              /* ASCII OEM identification */\
	char                            oem_table_id[8];        /* ASCII OEM table identification */\
	u32                             oem_revision;           /* OEM revision number */\
	char                            asl_compiler_id[4];     /* ASCII ASL compiler vendor ID */\
	u32                             asl_compiler_revision;	/* ASL compiler version */

struct acpi_table_header {
ACPI_TABLE_HEADER_DEF};

/*
 * GAS - Generic Address Structure (ACPI 2.0+)
 */
struct acpi_generic_address {
	u8 address_space_id;	/* Address space where struct or register exists */
	u8 register_bit_width;	/* Size in bits of given register */
	u8 register_bit_offset;	/* Bit offset within the register */
	u8 access_width;	/* Minimum Access size (ACPI 3.0) */
	u64 address;		/* 64-bit address of struct or register */
};

/*******************************************************************************
 *
 * RSDP - Root System Description Pointer (Signature is "RSD PTR ")
 *
 ******************************************************************************/

struct rsdp_descriptor {
	char signature[8];	/* ACPI signature, contains "RSD PTR " */
	u8 checksum;		/* ACPI 1.0 checksum */
	char oem_id[6];		/* OEM identification */
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
 *
 ******************************************************************************/

struct rsdt_descriptor {
	ACPI_TABLE_HEADER_DEF u32 table_offset_entry[1];	/* Array of pointers to ACPI tables */
};

struct xsdt_descriptor {
	ACPI_TABLE_HEADER_DEF u64 table_offset_entry[1];	/* Array of pointers to ACPI tables */
};

/*******************************************************************************
 *
 * FACS - Firmware ACPI Control Structure (FACS)
 *
 ******************************************************************************/

struct facs_descriptor {
	char signature[4];	/* ASCII table signature */
	u32 length;		/* Length of structure, in bytes */
	u32 hardware_signature;	/* Hardware configuration signature */
	u32 firmware_waking_vector;	/* 32-bit physical address of the Firmware Waking Vector */
	u32 global_lock;	/* Global Lock for shared hardware resources */

	/* Flags (32 bits) */

	u8 S4bios_f:1;		/* 00:    S4BIOS support is present */
	 u8:7;			/* 01-07: Reserved, must be zero */
	u8 reserved1[3];	/* 08-31: Reserved, must be zero */

	u64 xfirmware_waking_vector;	/* 64-bit version of the Firmware Waking Vector (ACPI 2.0+) */
	u8 version;		/* Version of this table (ACPI 2.0+) */
	u8 reserved[31];	/* Reserved, must be zero */
};

#define ACPI_GLOCK_PENDING      0x01	/* 00: Pending global lock ownership */
#define ACPI_GLOCK_OWNED        0x02	/* 01: Global lock is owned */

/*
 * Common FACS - This is a version-independent FACS structure used for internal use only
 */
struct acpi_common_facs {
	u32 *global_lock;
	u64 *firmware_waking_vector;
	u8 vector_width;
};

/*******************************************************************************
 *
 * FADT - Fixed ACPI Description Table (Signature "FACP")
 *
 ******************************************************************************/

/* Fields common to all versions of the FADT */

#define ACPI_FADT_COMMON \
	ACPI_TABLE_HEADER_DEF \
	u32                             V1_firmware_ctrl;   /* 32-bit physical address of FACS */ \
	u32                             V1_dsdt;            /* 32-bit physical address of DSDT */ \
	u8                              reserved1;          /* System Interrupt Model isn't used in ACPI 2.0*/ \
	u8                              prefer_PM_profile;  /* Conveys preferred power management profile to OSPM. */ \
	u16                             sci_int;            /* System vector of SCI interrupt */ \
	u32                             smi_cmd;            /* Port address of SMI command port */ \
	u8                              acpi_enable;        /* Value to write to smi_cmd to enable ACPI */ \
	u8                              acpi_disable;       /* Value to write to smi_cmd to disable ACPI */ \
	u8                              S4bios_req;         /* Value to write to SMI CMD to enter S4BIOS state */ \
	u8                              pstate_cnt;         /* Processor performance state control*/ \
	u32                             V1_pm1a_evt_blk;    /* Port address of Power Mgt 1a Event Reg Blk */ \
	u32                             V1_pm1b_evt_blk;    /* Port address of Power Mgt 1b Event Reg Blk */ \
	u32                             V1_pm1a_cnt_blk;    /* Port address of Power Mgt 1a Control Reg Blk */ \
	u32                             V1_pm1b_cnt_blk;    /* Port address of Power Mgt 1b Control Reg Blk */ \
	u32                             V1_pm2_cnt_blk;     /* Port address of Power Mgt 2 Control Reg Blk */ \
	u32                             V1_pm_tmr_blk;      /* Port address of Power Mgt Timer Ctrl Reg Blk */ \
	u32                             V1_gpe0_blk;        /* Port addr of General Purpose acpi_event 0 Reg Blk */ \
	u32                             V1_gpe1_blk;        /* Port addr of General Purpose acpi_event 1 Reg Blk */ \
	u8                              pm1_evt_len;        /* Byte length of ports at pm1_x_evt_blk */ \
	u8                              pm1_cnt_len;        /* Byte length of ports at pm1_x_cnt_blk */ \
	u8                              pm2_cnt_len;        /* Byte Length of ports at pm2_cnt_blk */ \
	u8                              pm_tm_len;          /* Byte Length of ports at pm_tm_blk */ \
	u8                              gpe0_blk_len;       /* Byte Length of ports at gpe0_blk */ \
	u8                              gpe1_blk_len;       /* Byte Length of ports at gpe1_blk */ \
	u8                              gpe1_base;          /* Offset in gpe model where gpe1 events start */ \
	u8                              cst_cnt;            /* Support for the _CST object and C States change notification.*/ \
	u16                             plvl2_lat;          /* Worst case HW latency to enter/exit C2 state */ \
	u16                             plvl3_lat;          /* Worst case HW latency to enter/exit C3 state */ \
	u16                             flush_size;         /* Processor's memory cache line width, in bytes */ \
	u16                             flush_stride;       /* Number of flush strides that need to be read */ \
	u8                              duty_offset;        /* Processor's duty cycle index in processor's P_CNT reg*/ \
	u8                              duty_width;         /* Processor's duty cycle value bit width in P_CNT register.*/ \
	u8                              day_alrm;           /* Index to day-of-month alarm in RTC CMOS RAM */ \
	u8                              mon_alrm;           /* Index to month-of-year alarm in RTC CMOS RAM */ \
	u8                              century;            /* Index to century in RTC CMOS RAM */ \
	u16                             iapc_boot_arch;     /* IA-PC Boot Architecture Flags. See Table 5-10 for description*/ \
	u8                              reserved2;          /* Reserved, must be zero */

/*
 * ACPI 2.0+ FADT
 */
struct fadt_descriptor {
	ACPI_FADT_COMMON
	    /* Flags (32 bits) */
	u8 wb_invd:1;		/* 00:    The wbinvd instruction works properly */
	u8 wb_invd_flush:1;	/* 01:    The wbinvd flushes but does not invalidate */
	u8 proc_c1:1;		/* 02:    All processors support C1 state */
	u8 plvl2_up:1;		/* 03:    C2 state works on MP system */
	u8 pwr_button:1;	/* 04:    Power button is handled as a generic feature */
	u8 sleep_button:1;	/* 05:    Sleep button is handled as a generic feature, or not present */
	u8 fixed_rTC:1;		/* 06:    RTC wakeup stat not in fixed register space */
	u8 rtcs4:1;		/* 07:    RTC wakeup stat not possible from S4 */
	u8 tmr_val_ext:1;	/* 08:    tmr_val is 32 bits 0=24-bits */
	u8 dock_cap:1;		/* 09:    Docking supported */
	u8 reset_reg_sup:1;	/* 10:    System reset via the FADT RESET_REG supported */
	u8 sealed_case:1;	/* 11:    No internal expansion capabilities and case is sealed */
	u8 headless:1;		/* 12:    No local video capabilities or local input devices */
	u8 cpu_sw_sleep:1;	/* 13:    Must execute native instruction after writing SLP_TYPx register */

	u8 pci_exp_wak:1;	/* 14:    System supports PCIEXP_WAKE (STS/EN) bits (ACPI 3.0) */
	u8 use_platform_clock:1;	/* 15:    OSPM should use platform-provided timer (ACPI 3.0) */
	u8 S4rtc_sts_valid:1;	/* 16:    Contents of RTC_STS valid after S4 wake (ACPI 3.0) */
	u8 remote_power_on_capable:1;	/* 17:    System is compatible with remote power on (ACPI 3.0) */
	u8 force_apic_cluster_model:1;	/* 18:    All local APICs must use cluster model (ACPI 3.0) */
	u8 force_apic_physical_destination_mode:1;	/* 19:   all local x_aPICs must use physical dest mode (ACPI 3.0) */
	 u8:4;			/* 20-23: Reserved, must be zero */
	u8 reserved3;		/* 24-31: Reserved, must be zero */

	struct acpi_generic_address reset_register;	/* Reset register address in GAS format */
	u8 reset_value;		/* Value to write to the reset_register port to reset the system */
	u8 reserved4[3];	/* These three bytes must be zero */
	u64 xfirmware_ctrl;	/* 64-bit physical address of FACS */
	u64 Xdsdt;		/* 64-bit physical address of DSDT */
	struct acpi_generic_address xpm1a_evt_blk;	/* Extended Power Mgt 1a acpi_event Reg Blk address */
	struct acpi_generic_address xpm1b_evt_blk;	/* Extended Power Mgt 1b acpi_event Reg Blk address */
	struct acpi_generic_address xpm1a_cnt_blk;	/* Extended Power Mgt 1a Control Reg Blk address */
	struct acpi_generic_address xpm1b_cnt_blk;	/* Extended Power Mgt 1b Control Reg Blk address */
	struct acpi_generic_address xpm2_cnt_blk;	/* Extended Power Mgt 2 Control Reg Blk address */
	struct acpi_generic_address xpm_tmr_blk;	/* Extended Power Mgt Timer Ctrl Reg Blk address */
	struct acpi_generic_address xgpe0_blk;	/* Extended General Purpose acpi_event 0 Reg Blk address */
	struct acpi_generic_address xgpe1_blk;	/* Extended General Purpose acpi_event 1 Reg Blk address */
};

/*
 * "Down-revved" ACPI 2.0 FADT descriptor
 * Defined here to allow compiler to generate the length of the struct
 */
struct fadt_descriptor_rev2_minus {
	ACPI_FADT_COMMON u32 flags;
	struct acpi_generic_address reset_register;	/* Reset register address in GAS format */
	u8 reset_value;		/* Value to write to the reset_register port to reset the system. */
	u8 reserved7[3];	/* Reserved, must be zero */
};

/*
 * ACPI 1.0 FADT
 * Defined here to allow compiler to generate the length of the struct
 */
struct fadt_descriptor_rev1 {
	ACPI_FADT_COMMON u32 flags;
};

/* FADT: Prefered Power Management Profiles */

#define PM_UNSPECIFIED                  0
#define PM_DESKTOP                      1
#define PM_MOBILE                       2
#define PM_WORKSTATION                  3
#define PM_ENTERPRISE_SERVER            4
#define PM_SOHO_SERVER                  5
#define PM_APPLIANCE_PC                 6

/* FADT: Boot Arch Flags */

#define BAF_LEGACY_DEVICES              0x0001
#define BAF_8042_KEYBOARD_CONTROLLER    0x0002

#define FADT2_REVISION_ID               3
#define FADT2_MINUS_REVISION_ID         2

/* Reset to default packing */

#pragma pack()

/*
 * This macro is temporary until the table bitfield flag definitions
 * are removed and replaced by a Flags field.
 */
#define ACPI_FLAG_OFFSET(d,f,o)         (u8) (ACPI_OFFSET (d,f) + \
			  sizeof(((d *)0)->f) + o)
/*
 * Get the remaining ACPI tables
 */
#include "actbl1.h"

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

extern u8 acpi_fadt_is_v1;	/* is set to 1 if FADT is revision 1,
				 * needed for certain workarounds */
/* Macros used to generate offsets to specific table fields */

#define ACPI_FACS_OFFSET(f)             (u8) ACPI_OFFSET (struct facs_descriptor,f)
#define ACPI_FADT_OFFSET(f)             (u8) ACPI_OFFSET (struct fadt_descriptor, f)
#define ACPI_GAS_OFFSET(f)              (u8) ACPI_OFFSET (struct acpi_generic_address,f)
#define ACPI_HDR_OFFSET(f)              (u8) ACPI_OFFSET (struct acpi_table_header,f)
#define ACPI_RSDP_OFFSET(f)             (u8) ACPI_OFFSET (struct rsdp_descriptor,f)

#define ACPI_FADT_FLAG_OFFSET(f,o)      ACPI_FLAG_OFFSET (struct fadt_descriptor,f,o)
#define ACPI_FACS_FLAG_OFFSET(f,o)      ACPI_FLAG_OFFSET (struct facs_descriptor,f,o)

#endif				/* __ACTBL_H__ */
