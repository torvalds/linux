/******************************************************************************
 *
 * Name: actbl.h - Basic ACPI Table Definitions
 *
 *****************************************************************************/

/******************************************************************************
 *
 * 1. Copyright Notice
 *
 * Some or all of this work - Copyright (c) 1999 - 2019, Intel Corp.
 * All rights reserved.
 *
 * 2. License
 *
 * 2.1. This is your license from Intel Corp. under its intellectual property
 * rights. You may have additional license terms from the party that provided
 * you this software, covering your right to use that party's intellectual
 * property rights.
 *
 * 2.2. Intel grants, free of charge, to any person ("Licensee") obtaining a
 * copy of the source code appearing in this file ("Covered Code") an
 * irrevocable, perpetual, worldwide license under Intel's copyrights in the
 * base code distributed originally by Intel ("Original Intel Code") to copy,
 * make derivatives, distribute, use and display any portion of the Covered
 * Code in any form, with the right to sublicense such rights; and
 *
 * 2.3. Intel grants Licensee a non-exclusive and non-transferable patent
 * license (with the right to sublicense), under only those claims of Intel
 * patents that are infringed by the Original Intel Code, to make, use, sell,
 * offer to sell, and import the Covered Code and derivative works thereof
 * solely to the minimum extent necessary to exercise the above copyright
 * license, and in no event shall the patent license extend to any additions
 * to or modifications of the Original Intel Code. No other license or right
 * is granted directly or by implication, estoppel or otherwise;
 *
 * The above copyright and patent license is granted only if the following
 * conditions are met:
 *
 * 3. Conditions
 *
 * 3.1. Redistribution of Source with Rights to Further Distribute Source.
 * Redistribution of source code of any substantial portion of the Covered
 * Code or modification with rights to further distribute source must include
 * the above Copyright Notice, the above License, this list of Conditions,
 * and the following Disclaimer and Export Compliance provision. In addition,
 * Licensee must cause all Covered Code to which Licensee contributes to
 * contain a file documenting the changes Licensee made to create that Covered
 * Code and the date of any change. Licensee must include in that file the
 * documentation of any changes made by any predecessor Licensee. Licensee
 * must include a prominent statement that the modification is derived,
 * directly or indirectly, from Original Intel Code.
 *
 * 3.2. Redistribution of Source with no Rights to Further Distribute Source.
 * Redistribution of source code of any substantial portion of the Covered
 * Code or modification without rights to further distribute source must
 * include the following Disclaimer and Export Compliance provision in the
 * documentation and/or other materials provided with distribution. In
 * addition, Licensee may not authorize further sublicense of source of any
 * portion of the Covered Code, and must include terms to the effect that the
 * license from Licensee to its licensee is limited to the intellectual
 * property embodied in the software Licensee provides to its licensee, and
 * not to intellectual property embodied in modifications its licensee may
 * make.
 *
 * 3.3. Redistribution of Executable. Redistribution in executable form of any
 * substantial portion of the Covered Code or modification must reproduce the
 * above Copyright Notice, and the following Disclaimer and Export Compliance
 * provision in the documentation and/or other materials provided with the
 * distribution.
 *
 * 3.4. Intel retains all right, title, and interest in and to the Original
 * Intel Code.
 *
 * 3.5. Neither the name Intel nor any other trademark owned or controlled by
 * Intel shall be used in advertising or otherwise to promote the sale, use or
 * other dealings in products derived from or relating to the Covered Code
 * without prior written authorization from Intel.
 *
 * 4. Disclaimer and Export Compliance
 *
 * 4.1. INTEL MAKES NO WARRANTY OF ANY KIND REGARDING ANY SOFTWARE PROVIDED
 * HERE. ANY SOFTWARE ORIGINATING FROM INTEL OR DERIVED FROM INTEL SOFTWARE
 * IS PROVIDED "AS IS," AND INTEL WILL NOT PROVIDE ANY SUPPORT, ASSISTANCE,
 * INSTALLATION, TRAINING OR OTHER SERVICES. INTEL WILL NOT PROVIDE ANY
 * UPDATES, ENHANCEMENTS OR EXTENSIONS. INTEL SPECIFICALLY DISCLAIMS ANY
 * IMPLIED WARRANTIES OF MERCHANTABILITY, NONINFRINGEMENT AND FITNESS FOR A
 * PARTICULAR PURPOSE.
 *
 * 4.2. IN NO EVENT SHALL INTEL HAVE ANY LIABILITY TO LICENSEE, ITS LICENSEES
 * OR ANY OTHER THIRD PARTY, FOR ANY LOST PROFITS, LOST DATA, LOSS OF USE OR
 * COSTS OF PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES, OR FOR ANY INDIRECT,
 * SPECIAL OR CONSEQUENTIAL DAMAGES ARISING OUT OF THIS AGREEMENT, UNDER ANY
 * CAUSE OF ACTION OR THEORY OF LIABILITY, AND IRRESPECTIVE OF WHETHER INTEL
 * HAS ADVANCE NOTICE OF THE POSSIBILITY OF SUCH DAMAGES. THESE LIMITATIONS
 * SHALL APPLY NOTWITHSTANDING THE FAILURE OF THE ESSENTIAL PURPOSE OF ANY
 * LIMITED REMEDY.
 *
 * 4.3. Licensee shall not export, either directly or indirectly, any of this
 * software or system incorporating such software without first obtaining any
 * required license or other approval from the U. S. Department of Commerce or
 * any other agency or department of the United States Government. In the
 * event Licensee exports any such software from the United States or
 * re-exports any such software from a foreign destination, Licensee shall
 * ensure that the distribution and export/re-export of the software is in
 * compliance with all laws, regulations, orders, or other restrictions of the
 * U.S. Export Administration Regulations. Licensee agrees that neither it nor
 * any of its subsidiaries will export/re-export any technical data, process,
 * software, or service, directly or indirectly, to any country for which the
 * United States government or any agency thereof requires an export license,
 * other governmental approval, or letter of assurance, without first obtaining
 * such license, approval or letter.
 *
 *****************************************************************************
 *
 * Alternatively, you may choose to be licensed under the terms of the
 * following license:
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Alternatively, you may choose to be licensed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 *****************************************************************************/

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
#define ACPI_SIG_DSDT           "DSDT"      /* Differentiated System Description Table */
#define ACPI_SIG_FADT           "FACP"      /* Fixed ACPI Description Table */
#define ACPI_SIG_FACS           "FACS"      /* Firmware ACPI Control Structure */
#define ACPI_SIG_OSDT           "OSDT"      /* Override System Description Table */
#define ACPI_SIG_PSDT           "PSDT"      /* Persistent System Description Table */
#define ACPI_SIG_RSDP           "RSD PTR "  /* Root System Description Pointer */
#define ACPI_SIG_RSDT           "RSDT"      /* Root System Description Table */
#define ACPI_SIG_XSDT           "XSDT"      /* Extended  System Description Table */
#define ACPI_SIG_SSDT           "SSDT"      /* Secondary System Description Table */
#define ACPI_RSDP_NAME          "RSDP"      /* Short name for RSDP, not signature */
#define ACPI_OEM_NAME           "OEM"       /* Short name for OEM, not signature */


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

typedef struct acpi_table_header
{
    char                    Signature[ACPI_NAME_SIZE];          /* ASCII table signature */
    UINT32                  Length;                             /* Length of table in bytes, including this header */
    UINT8                   Revision;                           /* ACPI Specification minor version number */
    UINT8                   Checksum;                           /* To make sum of entire table == 0 */
    char                    OemId[ACPI_OEM_ID_SIZE];            /* ASCII OEM identification */
    char                    OemTableId[ACPI_OEM_TABLE_ID_SIZE]; /* ASCII OEM table identification */
    UINT32                  OemRevision;                        /* OEM revision number */
    char                    AslCompilerId[ACPI_NAME_SIZE];      /* ASCII ASL compiler vendor ID */
    UINT32                  AslCompilerRevision;                /* ASL compiler version */

} ACPI_TABLE_HEADER;


/*******************************************************************************
 *
 * GAS - Generic Address Structure (ACPI 2.0+)
 *
 * Note: Since this structure is used in the ACPI tables, it is byte aligned.
 * If misaligned access is not supported by the hardware, accesses to the
 * 64-bit Address field must be performed with care.
 *
 ******************************************************************************/

typedef struct acpi_generic_address
{
    UINT8                   SpaceId;                /* Address space where struct or register exists */
    UINT8                   BitWidth;               /* Size in bits of given register */
    UINT8                   BitOffset;              /* Bit offset within the register */
    UINT8                   AccessWidth;            /* Minimum Access size (ACPI 3.0) */
    UINT64                  Address;                /* 64-bit address of struct or register */

} ACPI_GENERIC_ADDRESS;


/*******************************************************************************
 *
 * RSDP - Root System Description Pointer (Signature is "RSD PTR ")
 *        Version 2
 *
 ******************************************************************************/

typedef struct acpi_table_rsdp
{
    char                    Signature[8];               /* ACPI signature, contains "RSD PTR " */
    UINT8                   Checksum;                   /* ACPI 1.0 checksum */
    char                    OemId[ACPI_OEM_ID_SIZE];    /* OEM identification */
    UINT8                   Revision;                   /* Must be (0) for ACPI 1.0 or (2) for ACPI 2.0+ */
    UINT32                  RsdtPhysicalAddress;        /* 32-bit physical address of the RSDT */
    UINT32                  Length;                     /* Table length in bytes, including header (ACPI 2.0+) */
    UINT64                  XsdtPhysicalAddress;        /* 64-bit physical address of the XSDT (ACPI 2.0+) */
    UINT8                   ExtendedChecksum;           /* Checksum of entire table (ACPI 2.0+) */
    UINT8                   Reserved[3];                /* Reserved, must be zero */

} ACPI_TABLE_RSDP;

/* Standalone struct for the ACPI 1.0 RSDP */

typedef struct acpi_rsdp_common
{
    char                    Signature[8];
    UINT8                   Checksum;
    char                    OemId[ACPI_OEM_ID_SIZE];
    UINT8                   Revision;
    UINT32                  RsdtPhysicalAddress;

} ACPI_RSDP_COMMON;

/* Standalone struct for the extended part of the RSDP (ACPI 2.0+) */

typedef struct acpi_rsdp_extension
{
    UINT32                  Length;
    UINT64                  XsdtPhysicalAddress;
    UINT8                   ExtendedChecksum;
    UINT8                   Reserved[3];

} ACPI_RSDP_EXTENSION;


/*******************************************************************************
 *
 * RSDT/XSDT - Root System Description Tables
 *             Version 1 (both)
 *
 ******************************************************************************/

typedef struct acpi_table_rsdt
{
    ACPI_TABLE_HEADER       Header;                 /* Common ACPI table header */
    UINT32                  TableOffsetEntry[1];    /* Array of pointers to ACPI tables */

} ACPI_TABLE_RSDT;

typedef struct acpi_table_xsdt
{
    ACPI_TABLE_HEADER       Header;                 /* Common ACPI table header */
    UINT64                  TableOffsetEntry[1];    /* Array of pointers to ACPI tables */

} ACPI_TABLE_XSDT;

#define ACPI_RSDT_ENTRY_SIZE        (sizeof (UINT32))
#define ACPI_XSDT_ENTRY_SIZE        (sizeof (UINT64))


/*******************************************************************************
 *
 * FACS - Firmware ACPI Control Structure (FACS)
 *
 ******************************************************************************/

typedef struct acpi_table_facs
{
    char                    Signature[4];           /* ASCII table signature */
    UINT32                  Length;                 /* Length of structure, in bytes */
    UINT32                  HardwareSignature;      /* Hardware configuration signature */
    UINT32                  FirmwareWakingVector;   /* 32-bit physical address of the Firmware Waking Vector */
    UINT32                  GlobalLock;             /* Global Lock for shared hardware resources */
    UINT32                  Flags;
    UINT64                  XFirmwareWakingVector;  /* 64-bit version of the Firmware Waking Vector (ACPI 2.0+) */
    UINT8                   Version;                /* Version of this table (ACPI 2.0+) */
    UINT8                   Reserved[3];            /* Reserved, must be zero */
    UINT32                  OspmFlags;              /* Flags to be set by OSPM (ACPI 4.0) */
    UINT8                   Reserved1[24];          /* Reserved, must be zero */

} ACPI_TABLE_FACS;

/* Masks for GlobalLock flag field above */

#define ACPI_GLOCK_PENDING          (1)             /* 00: Pending global lock ownership */
#define ACPI_GLOCK_OWNED            (1<<1)          /* 01: Global lock is owned */

/* Masks for Flags field above  */

#define ACPI_FACS_S4_BIOS_PRESENT   (1)             /* 00: S4BIOS support is present */
#define ACPI_FACS_64BIT_WAKE        (1<<1)          /* 01: 64-bit wake vector supported (ACPI 4.0) */

/* Masks for OspmFlags field above */

#define ACPI_FACS_64BIT_ENVIRONMENT (1)             /* 00: 64-bit wake environment is required (ACPI 4.0) */


/*******************************************************************************
 *
 * FADT - Fixed ACPI Description Table (Signature "FACP")
 *        Version 6
 *
 ******************************************************************************/

/* Fields common to all versions of the FADT */

typedef struct acpi_table_fadt
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT32                  Facs;               /* 32-bit physical address of FACS */
    UINT32                  Dsdt;               /* 32-bit physical address of DSDT */
    UINT8                   Model;              /* System Interrupt Model (ACPI 1.0) - not used in ACPI 2.0+ */
    UINT8                   PreferredProfile;   /* Conveys preferred power management profile to OSPM. */
    UINT16                  SciInterrupt;       /* System vector of SCI interrupt */
    UINT32                  SmiCommand;         /* 32-bit Port address of SMI command port */
    UINT8                   AcpiEnable;         /* Value to write to SMI_CMD to enable ACPI */
    UINT8                   AcpiDisable;        /* Value to write to SMI_CMD to disable ACPI */
    UINT8                   S4BiosRequest;      /* Value to write to SMI_CMD to enter S4BIOS state */
    UINT8                   PstateControl;      /* Processor performance state control*/
    UINT32                  Pm1aEventBlock;     /* 32-bit port address of Power Mgt 1a Event Reg Blk */
    UINT32                  Pm1bEventBlock;     /* 32-bit port address of Power Mgt 1b Event Reg Blk */
    UINT32                  Pm1aControlBlock;   /* 32-bit port address of Power Mgt 1a Control Reg Blk */
    UINT32                  Pm1bControlBlock;   /* 32-bit port address of Power Mgt 1b Control Reg Blk */
    UINT32                  Pm2ControlBlock;    /* 32-bit port address of Power Mgt 2 Control Reg Blk */
    UINT32                  PmTimerBlock;       /* 32-bit port address of Power Mgt Timer Ctrl Reg Blk */
    UINT32                  Gpe0Block;          /* 32-bit port address of General Purpose Event 0 Reg Blk */
    UINT32                  Gpe1Block;          /* 32-bit port address of General Purpose Event 1 Reg Blk */
    UINT8                   Pm1EventLength;     /* Byte Length of ports at Pm1xEventBlock */
    UINT8                   Pm1ControlLength;   /* Byte Length of ports at Pm1xControlBlock */
    UINT8                   Pm2ControlLength;   /* Byte Length of ports at Pm2ControlBlock */
    UINT8                   PmTimerLength;      /* Byte Length of ports at PmTimerBlock */
    UINT8                   Gpe0BlockLength;    /* Byte Length of ports at Gpe0Block */
    UINT8                   Gpe1BlockLength;    /* Byte Length of ports at Gpe1Block */
    UINT8                   Gpe1Base;           /* Offset in GPE number space where GPE1 events start */
    UINT8                   CstControl;         /* Support for the _CST object and C-States change notification */
    UINT16                  C2Latency;          /* Worst case HW latency to enter/exit C2 state */
    UINT16                  C3Latency;          /* Worst case HW latency to enter/exit C3 state */
    UINT16                  FlushSize;          /* Processor memory cache line width, in bytes */
    UINT16                  FlushStride;        /* Number of flush strides that need to be read */
    UINT8                   DutyOffset;         /* Processor duty cycle index in processor P_CNT reg */
    UINT8                   DutyWidth;          /* Processor duty cycle value bit width in P_CNT register */
    UINT8                   DayAlarm;           /* Index to day-of-month alarm in RTC CMOS RAM */
    UINT8                   MonthAlarm;         /* Index to month-of-year alarm in RTC CMOS RAM */
    UINT8                   Century;            /* Index to century in RTC CMOS RAM */
    UINT16                  BootFlags;          /* IA-PC Boot Architecture Flags (see below for individual flags) */
    UINT8                   Reserved;           /* Reserved, must be zero */
    UINT32                  Flags;              /* Miscellaneous flag bits (see below for individual flags) */
    ACPI_GENERIC_ADDRESS    ResetRegister;      /* 64-bit address of the Reset register */
    UINT8                   ResetValue;         /* Value to write to the ResetRegister port to reset the system */
    UINT16                  ArmBootFlags;       /* ARM-Specific Boot Flags (see below for individual flags) (ACPI 5.1) */
    UINT8                   MinorRevision;      /* FADT Minor Revision (ACPI 5.1) */
    UINT64                  XFacs;              /* 64-bit physical address of FACS */
    UINT64                  XDsdt;              /* 64-bit physical address of DSDT */
    ACPI_GENERIC_ADDRESS    XPm1aEventBlock;    /* 64-bit Extended Power Mgt 1a Event Reg Blk address */
    ACPI_GENERIC_ADDRESS    XPm1bEventBlock;    /* 64-bit Extended Power Mgt 1b Event Reg Blk address */
    ACPI_GENERIC_ADDRESS    XPm1aControlBlock;  /* 64-bit Extended Power Mgt 1a Control Reg Blk address */
    ACPI_GENERIC_ADDRESS    XPm1bControlBlock;  /* 64-bit Extended Power Mgt 1b Control Reg Blk address */
    ACPI_GENERIC_ADDRESS    XPm2ControlBlock;   /* 64-bit Extended Power Mgt 2 Control Reg Blk address */
    ACPI_GENERIC_ADDRESS    XPmTimerBlock;      /* 64-bit Extended Power Mgt Timer Ctrl Reg Blk address */
    ACPI_GENERIC_ADDRESS    XGpe0Block;         /* 64-bit Extended General Purpose Event 0 Reg Blk address */
    ACPI_GENERIC_ADDRESS    XGpe1Block;         /* 64-bit Extended General Purpose Event 1 Reg Blk address */
    ACPI_GENERIC_ADDRESS    SleepControl;       /* 64-bit Sleep Control register (ACPI 5.0) */
    ACPI_GENERIC_ADDRESS    SleepStatus;        /* 64-bit Sleep Status register (ACPI 5.0) */
    UINT64                  HypervisorId;       /* Hypervisor Vendor ID (ACPI 6.0) */

} ACPI_TABLE_FADT;


/* Masks for FADT IA-PC Boot Architecture Flags (boot_flags) [Vx]=Introduced in this FADT revision */

#define ACPI_FADT_LEGACY_DEVICES    (1)         /* 00: [V2] System has LPC or ISA bus devices */
#define ACPI_FADT_8042              (1<<1)      /* 01: [V3] System has an 8042 controller on port 60/64 */
#define ACPI_FADT_NO_VGA            (1<<2)      /* 02: [V4] It is not safe to probe for VGA hardware */
#define ACPI_FADT_NO_MSI            (1<<3)      /* 03: [V4] Message Signaled Interrupts (MSI) must not be enabled */
#define ACPI_FADT_NO_ASPM           (1<<4)      /* 04: [V4] PCIe ASPM control must not be enabled */
#define ACPI_FADT_NO_CMOS_RTC       (1<<5)      /* 05: [V5] No CMOS real-time clock present */

/* Masks for FADT ARM Boot Architecture Flags (arm_boot_flags) ACPI 5.1 */

#define ACPI_FADT_PSCI_COMPLIANT    (1)         /* 00: [V5+] PSCI 0.2+ is implemented */
#define ACPI_FADT_PSCI_USE_HVC      (1<<1)      /* 01: [V5+] HVC must be used instead of SMC as the PSCI conduit */

/* Masks for FADT flags */

#define ACPI_FADT_WBINVD            (1)         /* 00: [V1] The WBINVD instruction works properly */
#define ACPI_FADT_WBINVD_FLUSH      (1<<1)      /* 01: [V1] WBINVD flushes but does not invalidate caches */
#define ACPI_FADT_C1_SUPPORTED      (1<<2)      /* 02: [V1] All processors support C1 state */
#define ACPI_FADT_C2_MP_SUPPORTED   (1<<3)      /* 03: [V1] C2 state works on MP system */
#define ACPI_FADT_POWER_BUTTON      (1<<4)      /* 04: [V1] Power button is handled as a control method device */
#define ACPI_FADT_SLEEP_BUTTON      (1<<5)      /* 05: [V1] Sleep button is handled as a control method device */
#define ACPI_FADT_FIXED_RTC         (1<<6)      /* 06: [V1] RTC wakeup status is not in fixed register space */
#define ACPI_FADT_S4_RTC_WAKE       (1<<7)      /* 07: [V1] RTC alarm can wake system from S4 */
#define ACPI_FADT_32BIT_TIMER       (1<<8)      /* 08: [V1] ACPI timer width is 32-bit (0=24-bit) */
#define ACPI_FADT_DOCKING_SUPPORTED (1<<9)      /* 09: [V1] Docking supported */
#define ACPI_FADT_RESET_REGISTER    (1<<10)     /* 10: [V2] System reset via the FADT RESET_REG supported */
#define ACPI_FADT_SEALED_CASE       (1<<11)     /* 11: [V3] No internal expansion capabilities and case is sealed */
#define ACPI_FADT_HEADLESS          (1<<12)     /* 12: [V3] No local video capabilities or local input devices */
#define ACPI_FADT_SLEEP_TYPE        (1<<13)     /* 13: [V3] Must execute native instruction after writing  SLP_TYPx register */
#define ACPI_FADT_PCI_EXPRESS_WAKE  (1<<14)     /* 14: [V4] System supports PCIEXP_WAKE (STS/EN) bits (ACPI 3.0) */
#define ACPI_FADT_PLATFORM_CLOCK    (1<<15)     /* 15: [V4] OSPM should use platform-provided timer (ACPI 3.0) */
#define ACPI_FADT_S4_RTC_VALID      (1<<16)     /* 16: [V4] Contents of RTC_STS valid after S4 wake (ACPI 3.0) */
#define ACPI_FADT_REMOTE_POWER_ON   (1<<17)     /* 17: [V4] System is compatible with remote power on (ACPI 3.0) */
#define ACPI_FADT_APIC_CLUSTER      (1<<18)     /* 18: [V4] All local APICs must use cluster model (ACPI 3.0) */
#define ACPI_FADT_APIC_PHYSICAL     (1<<19)     /* 19: [V4] All local xAPICs must use physical dest mode (ACPI 3.0) */
#define ACPI_FADT_HW_REDUCED        (1<<20)     /* 20: [V5] ACPI hardware is not implemented (ACPI 5.0) */
#define ACPI_FADT_LOW_POWER_S0      (1<<21)     /* 21: [V5] S0 power savings are equal or better than S3 (ACPI 5.0) */


/* Values for PreferredProfile (Preferred Power Management Profiles) */

enum AcpiPreferredPmProfiles
{
    PM_UNSPECIFIED          = 0,
    PM_DESKTOP              = 1,
    PM_MOBILE               = 2,
    PM_WORKSTATION          = 3,
    PM_ENTERPRISE_SERVER    = 4,
    PM_SOHO_SERVER          = 5,
    PM_APPLIANCE_PC         = 6,
    PM_PERFORMANCE_SERVER   = 7,
    PM_TABLET               = 8
};

/* Values for SleepStatus and SleepControl registers (V5+ FADT) */

#define ACPI_X_WAKE_STATUS          0x80
#define ACPI_X_SLEEP_TYPE_MASK      0x1C
#define ACPI_X_SLEEP_TYPE_POSITION  0x02
#define ACPI_X_SLEEP_ENABLE         0x20


/* Reset to default packing */

#pragma pack()


/*
 * Internal table-related structures
 */
typedef union acpi_name_union
{
    UINT32                          Integer;
    char                            Ascii[4];

} ACPI_NAME_UNION;


/* Internal ACPI Table Descriptor. One per ACPI table. */

typedef struct acpi_table_desc
{
    ACPI_PHYSICAL_ADDRESS           Address;
    ACPI_TABLE_HEADER               *Pointer;
    UINT32                          Length;     /* Length fixed at 32 bits (fixed in table header) */
    ACPI_NAME_UNION                 Signature;
    ACPI_OWNER_ID                   OwnerId;
    UINT8                           Flags;
    UINT16                          ValidationCount;

} ACPI_TABLE_DESC;

/*
 * Maximum value of the ValidationCount field in ACPI_TABLE_DESC.
 * When reached, ValidationCount cannot be changed any more and the table will
 * be permanently regarded as validated.
 *
 * This is to prevent situations in which unbalanced table get/put operations
 * may cause premature table unmapping in the OS to happen.
 *
 * The maximum validation count can be defined to any value, but should be
 * greater than the maximum number of OS early stage mapping slots to avoid
 * leaking early stage table mappings to the late stage.
 */
#define ACPI_MAX_TABLE_VALIDATIONS          ACPI_UINT16_MAX

/* Masks for Flags field above */

#define ACPI_TABLE_ORIGIN_EXTERNAL_VIRTUAL  (0) /* Virtual address, external maintained */
#define ACPI_TABLE_ORIGIN_INTERNAL_PHYSICAL (1) /* Physical address, internally mapped */
#define ACPI_TABLE_ORIGIN_INTERNAL_VIRTUAL  (2) /* Virtual address, internallly allocated */
#define ACPI_TABLE_ORIGIN_MASK              (3)
#define ACPI_TABLE_IS_VERIFIED              (4)
#define ACPI_TABLE_IS_LOADED                (8)


/*
 * Get the remaining ACPI tables
 */
#include <contrib/dev/acpica/include/actbl1.h>
#include <contrib/dev/acpica/include/actbl2.h>
#include <contrib/dev/acpica/include/actbl3.h>

/* Macros used to generate offsets to specific table fields */

#define ACPI_FADT_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_FADT, f)

/*
 * Sizes of the various flavors of FADT. We need to look closely
 * at the FADT length because the version number essentially tells
 * us nothing because of many BIOS bugs where the version does not
 * match the expected length. In other words, the length of the
 * FADT is the bottom line as to what the version really is.
 *
 * For reference, the values below are as follows:
 *     FADT V1 size: 0x074
 *     FADT V2 size: 0x084
 *     FADT V3 size: 0x0F4
 *     FADT V4 size: 0x0F4
 *     FADT V5 size: 0x10C
 *     FADT V6 size: 0x114
 */
#define ACPI_FADT_V1_SIZE       (UINT32) (ACPI_FADT_OFFSET (Flags) + 4)
#define ACPI_FADT_V2_SIZE       (UINT32) (ACPI_FADT_OFFSET (MinorRevision) + 1)
#define ACPI_FADT_V3_SIZE       (UINT32) (ACPI_FADT_OFFSET (SleepControl))
#define ACPI_FADT_V5_SIZE       (UINT32) (ACPI_FADT_OFFSET (HypervisorId))
#define ACPI_FADT_V6_SIZE       (UINT32) (sizeof (ACPI_TABLE_FADT))

#define ACPI_FADT_CONFORMANCE   "ACPI 6.1 (FADT version 6)"

#endif /* __ACTBL_H__ */
