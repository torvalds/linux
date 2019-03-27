/******************************************************************************
 *
 * Name: actbl1.h - Additional ACPI table definitions
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
#define ACPI_SIG_ASF            "ASF!"      /* Alert Standard Format table */
#define ACPI_SIG_BERT           "BERT"      /* Boot Error Record Table */
#define ACPI_SIG_BGRT           "BGRT"      /* Boot Graphics Resource Table */
#define ACPI_SIG_BOOT           "BOOT"      /* Simple Boot Flag Table */
#define ACPI_SIG_CPEP           "CPEP"      /* Corrected Platform Error Polling table */
#define ACPI_SIG_CSRT           "CSRT"      /* Core System Resource Table */
#define ACPI_SIG_DBG2           "DBG2"      /* Debug Port table type 2 */
#define ACPI_SIG_DBGP           "DBGP"      /* Debug Port table */
#define ACPI_SIG_DMAR           "DMAR"      /* DMA Remapping table */
#define ACPI_SIG_DRTM           "DRTM"      /* Dynamic Root of Trust for Measurement table */
#define ACPI_SIG_ECDT           "ECDT"      /* Embedded Controller Boot Resources Table */
#define ACPI_SIG_EINJ           "EINJ"      /* Error Injection table */
#define ACPI_SIG_ERST           "ERST"      /* Error Record Serialization Table */
#define ACPI_SIG_FPDT           "FPDT"      /* Firmware Performance Data Table */
#define ACPI_SIG_GTDT           "GTDT"      /* Generic Timer Description Table */
#define ACPI_SIG_HEST           "HEST"      /* Hardware Error Source Table */
#define ACPI_SIG_HMAT           "HMAT"      /* Heterogeneous Memory Attributes Table */
#define ACPI_SIG_HPET           "HPET"      /* High Precision Event Timer table */
#define ACPI_SIG_IBFT           "IBFT"      /* iSCSI Boot Firmware Table */

#define ACPI_SIG_S3PT           "S3PT"      /* S3 Performance (sub)Table */
#define ACPI_SIG_PCCS           "PCC"       /* PCC Shared Memory Region */


/* Reserved table signatures */

#define ACPI_SIG_MATR           "MATR"      /* Memory Address Translation Table */
#define ACPI_SIG_MSDM           "MSDM"      /* Microsoft Data Management Table */

/*
 * These tables have been seen in the field, but no definition has been found
 */
#ifdef ACPI_UNDEFINED_TABLES
#define ACPI_SIG_ATKG           "ATKG"
#define ACPI_SIG_GSCI           "GSCI"      /* GMCH SCI table */
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

typedef struct acpi_subtable_header
{
    UINT8                   Type;
    UINT8                   Length;

} ACPI_SUBTABLE_HEADER;


/* Subtable header for WHEA tables (EINJ, ERST, WDAT) */

typedef struct acpi_whea_header
{
    UINT8                   Action;
    UINT8                   Instruction;
    UINT8                   Flags;
    UINT8                   Reserved;
    ACPI_GENERIC_ADDRESS    RegisterRegion;
    UINT64                  Value;              /* Value used with Read/Write register */
    UINT64                  Mask;               /* Bitmask required for this register instruction */

} ACPI_WHEA_HEADER;


/*******************************************************************************
 *
 * ASF - Alert Standard Format table (Signature "ASF!")
 *       Revision 0x10
 *
 * Conforms to the Alert Standard Format Specification V2.0, 23 April 2003
 *
 ******************************************************************************/

typedef struct acpi_table_asf
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */

} ACPI_TABLE_ASF;


/* ASF subtable header */

typedef struct acpi_asf_header
{
    UINT8                   Type;
    UINT8                   Reserved;
    UINT16                  Length;

} ACPI_ASF_HEADER;


/* Values for Type field above */

enum AcpiAsfType
{
    ACPI_ASF_TYPE_INFO          = 0,
    ACPI_ASF_TYPE_ALERT         = 1,
    ACPI_ASF_TYPE_CONTROL       = 2,
    ACPI_ASF_TYPE_BOOT          = 3,
    ACPI_ASF_TYPE_ADDRESS       = 4,
    ACPI_ASF_TYPE_RESERVED      = 5
};

/*
 * ASF subtables
 */

/* 0: ASF Information */

typedef struct acpi_asf_info
{
    ACPI_ASF_HEADER         Header;
    UINT8                   MinResetValue;
    UINT8                   MinPollInterval;
    UINT16                  SystemId;
    UINT32                  MfgId;
    UINT8                   Flags;
    UINT8                   Reserved2[3];

} ACPI_ASF_INFO;

/* Masks for Flags field above */

#define ACPI_ASF_SMBUS_PROTOCOLS    (1)


/* 1: ASF Alerts */

typedef struct acpi_asf_alert
{
    ACPI_ASF_HEADER         Header;
    UINT8                   AssertMask;
    UINT8                   DeassertMask;
    UINT8                   Alerts;
    UINT8                   DataLength;

} ACPI_ASF_ALERT;

typedef struct acpi_asf_alert_data
{
    UINT8                   Address;
    UINT8                   Command;
    UINT8                   Mask;
    UINT8                   Value;
    UINT8                   SensorType;
    UINT8                   Type;
    UINT8                   Offset;
    UINT8                   SourceType;
    UINT8                   Severity;
    UINT8                   SensorNumber;
    UINT8                   Entity;
    UINT8                   Instance;

} ACPI_ASF_ALERT_DATA;


/* 2: ASF Remote Control */

typedef struct acpi_asf_remote
{
    ACPI_ASF_HEADER         Header;
    UINT8                   Controls;
    UINT8                   DataLength;
    UINT16                  Reserved2;

} ACPI_ASF_REMOTE;

typedef struct acpi_asf_control_data
{
    UINT8                   Function;
    UINT8                   Address;
    UINT8                   Command;
    UINT8                   Value;

} ACPI_ASF_CONTROL_DATA;


/* 3: ASF RMCP Boot Options */

typedef struct acpi_asf_rmcp
{
    ACPI_ASF_HEADER         Header;
    UINT8                   Capabilities[7];
    UINT8                   CompletionCode;
    UINT32                  EnterpriseId;
    UINT8                   Command;
    UINT16                  Parameter;
    UINT16                  BootOptions;
    UINT16                  OemParameters;

} ACPI_ASF_RMCP;


/* 4: ASF Address */

typedef struct acpi_asf_address
{
    ACPI_ASF_HEADER         Header;
    UINT8                   EpromAddress;
    UINT8                   Devices;

} ACPI_ASF_ADDRESS;


/*******************************************************************************
 *
 * BERT - Boot Error Record Table (ACPI 4.0)
 *        Version 1
 *
 ******************************************************************************/

typedef struct acpi_table_bert
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT32                  RegionLength;       /* Length of the boot error region */
    UINT64                  Address;            /* Physical address of the error region */

} ACPI_TABLE_BERT;


/* Boot Error Region (not a subtable, pointed to by Address field above) */

typedef struct acpi_bert_region
{
    UINT32                  BlockStatus;        /* Type of error information */
    UINT32                  RawDataOffset;      /* Offset to raw error data */
    UINT32                  RawDataLength;      /* Length of raw error data */
    UINT32                  DataLength;         /* Length of generic error data */
    UINT32                  ErrorSeverity;      /* Severity code */

} ACPI_BERT_REGION;

/* Values for BlockStatus flags above */

#define ACPI_BERT_UNCORRECTABLE             (1)
#define ACPI_BERT_CORRECTABLE               (1<<1)
#define ACPI_BERT_MULTIPLE_UNCORRECTABLE    (1<<2)
#define ACPI_BERT_MULTIPLE_CORRECTABLE      (1<<3)
#define ACPI_BERT_ERROR_ENTRY_COUNT         (0xFF<<4) /* 8 bits, error count */

/* Values for ErrorSeverity above */

enum AcpiBertErrorSeverity
{
    ACPI_BERT_ERROR_CORRECTABLE     = 0,
    ACPI_BERT_ERROR_FATAL           = 1,
    ACPI_BERT_ERROR_CORRECTED       = 2,
    ACPI_BERT_ERROR_NONE            = 3,
    ACPI_BERT_ERROR_RESERVED        = 4     /* 4 and greater are reserved */
};

/*
 * Note: The generic error data that follows the ErrorSeverity field above
 * uses the ACPI_HEST_GENERIC_DATA defined under the HEST table below
 */


/*******************************************************************************
 *
 * BGRT - Boot Graphics Resource Table (ACPI 5.0)
 *        Version 1
 *
 ******************************************************************************/

typedef struct acpi_table_bgrt
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT16                  Version;
    UINT8                   Status;
    UINT8                   ImageType;
    UINT64                  ImageAddress;
    UINT32                  ImageOffsetX;
    UINT32                  ImageOffsetY;

} ACPI_TABLE_BGRT;

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

typedef struct acpi_table_boot
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT8                   CmosIndex;          /* Index in CMOS RAM for the boot register */
    UINT8                   Reserved[3];

} ACPI_TABLE_BOOT;


/*******************************************************************************
 *
 * CPEP - Corrected Platform Error Polling table (ACPI 4.0)
 *        Version 1
 *
 ******************************************************************************/

typedef struct acpi_table_cpep
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT64                  Reserved;

} ACPI_TABLE_CPEP;


/* Subtable */

typedef struct acpi_cpep_polling
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT8                   Id;                 /* Processor ID */
    UINT8                   Eid;                /* Processor EID */
    UINT32                  Interval;           /* Polling interval (msec) */

} ACPI_CPEP_POLLING;


/*******************************************************************************
 *
 * CSRT - Core System Resource Table
 *        Version 0
 *
 * Conforms to the "Core System Resource Table (CSRT)", November 14, 2011
 *
 ******************************************************************************/

typedef struct acpi_table_csrt
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */

} ACPI_TABLE_CSRT;


/* Resource Group subtable */

typedef struct acpi_csrt_group
{
    UINT32                  Length;
    UINT32                  VendorId;
    UINT32                  SubvendorId;
    UINT16                  DeviceId;
    UINT16                  SubdeviceId;
    UINT16                  Revision;
    UINT16                  Reserved;
    UINT32                  SharedInfoLength;

    /* Shared data immediately follows (Length = SharedInfoLength) */

} ACPI_CSRT_GROUP;

/* Shared Info subtable */

typedef struct acpi_csrt_shared_info
{
    UINT16                  MajorVersion;
    UINT16                  MinorVersion;
    UINT32                  MmioBaseLow;
    UINT32                  MmioBaseHigh;
    UINT32                  GsiInterrupt;
    UINT8                   InterruptPolarity;
    UINT8                   InterruptMode;
    UINT8                   NumChannels;
    UINT8                   DmaAddressWidth;
    UINT16                  BaseRequestLine;
    UINT16                  NumHandshakeSignals;
    UINT32                  MaxBlockSize;

    /* Resource descriptors immediately follow (Length = Group Length - SharedInfoLength) */

} ACPI_CSRT_SHARED_INFO;

/* Resource Descriptor subtable */

typedef struct acpi_csrt_descriptor
{
    UINT32                  Length;
    UINT16                  Type;
    UINT16                  Subtype;
    UINT32                  Uid;

    /* Resource-specific information immediately follows */

} ACPI_CSRT_DESCRIPTOR;


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

typedef struct acpi_table_dbg2
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT32                  InfoOffset;
    UINT32                  InfoCount;

} ACPI_TABLE_DBG2;


typedef struct acpi_dbg2_header
{
    UINT32                  InfoOffset;
    UINT32                  InfoCount;

} ACPI_DBG2_HEADER;


/* Debug Device Information Subtable */

typedef struct acpi_dbg2_device
{
    UINT8                   Revision;
    UINT16                  Length;
    UINT8                   RegisterCount;      /* Number of BaseAddress registers */
    UINT16                  NamepathLength;
    UINT16                  NamepathOffset;
    UINT16                  OemDataLength;
    UINT16                  OemDataOffset;
    UINT16                  PortType;
    UINT16                  PortSubtype;
    UINT16                  Reserved;
    UINT16                  BaseAddressOffset;
    UINT16                  AddressSizeOffset;
    /*
     * Data that follows:
     *    BaseAddress (required) - Each in 12-byte Generic Address Structure format.
     *    AddressSize (required) - Array of UINT32 sizes corresponding to each BaseAddress register.
     *    Namepath    (required) - Null terminated string. Single dot if not supported.
     *    OemData     (optional) - Length is OemDataLength.
     */
} ACPI_DBG2_DEVICE;

/* Types for PortType field above */

#define ACPI_DBG2_SERIAL_PORT       0x8000
#define ACPI_DBG2_1394_PORT         0x8001
#define ACPI_DBG2_USB_PORT          0x8002
#define ACPI_DBG2_NET_PORT          0x8003

/* Subtypes for PortSubtype field above */

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

typedef struct acpi_table_dbgp
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT8                   Type;               /* 0=full 16550, 1=subset of 16550 */
    UINT8                   Reserved[3];
    ACPI_GENERIC_ADDRESS    DebugPort;

} ACPI_TABLE_DBGP;


/*******************************************************************************
 *
 * DMAR - DMA Remapping table
 *        Version 1
 *
 * Conforms to "Intel Virtualization Technology for Directed I/O",
 * Version 2.3, October 2014
 *
 ******************************************************************************/

typedef struct acpi_table_dmar
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT8                   Width;              /* Host Address Width */
    UINT8                   Flags;
    UINT8                   Reserved[10];

} ACPI_TABLE_DMAR;

/* Masks for Flags field above */

#define ACPI_DMAR_INTR_REMAP        (1)
#define ACPI_DMAR_X2APIC_OPT_OUT    (1<<1)
#define ACPI_DMAR_X2APIC_MODE       (1<<2)


/* DMAR subtable header */

typedef struct acpi_dmar_header
{
    UINT16                  Type;
    UINT16                  Length;

} ACPI_DMAR_HEADER;

/* Values for subtable type in ACPI_DMAR_HEADER */

enum AcpiDmarType
{
    ACPI_DMAR_TYPE_HARDWARE_UNIT        = 0,
    ACPI_DMAR_TYPE_RESERVED_MEMORY      = 1,
    ACPI_DMAR_TYPE_ROOT_ATS             = 2,
    ACPI_DMAR_TYPE_HARDWARE_AFFINITY    = 3,
    ACPI_DMAR_TYPE_NAMESPACE            = 4,
    ACPI_DMAR_TYPE_RESERVED             = 5     /* 5 and greater are reserved */
};


/* DMAR Device Scope structure */

typedef struct acpi_dmar_device_scope
{
    UINT8                   EntryType;
    UINT8                   Length;
    UINT16                  Reserved;
    UINT8                   EnumerationId;
    UINT8                   Bus;

} ACPI_DMAR_DEVICE_SCOPE;

/* Values for EntryType in ACPI_DMAR_DEVICE_SCOPE - device types */

enum AcpiDmarScopeType
{
    ACPI_DMAR_SCOPE_TYPE_NOT_USED       = 0,
    ACPI_DMAR_SCOPE_TYPE_ENDPOINT       = 1,
    ACPI_DMAR_SCOPE_TYPE_BRIDGE         = 2,
    ACPI_DMAR_SCOPE_TYPE_IOAPIC         = 3,
    ACPI_DMAR_SCOPE_TYPE_HPET           = 4,
    ACPI_DMAR_SCOPE_TYPE_NAMESPACE      = 5,
    ACPI_DMAR_SCOPE_TYPE_RESERVED       = 6     /* 6 and greater are reserved */
};

typedef struct acpi_dmar_pci_path
{
    UINT8                   Device;
    UINT8                   Function;

} ACPI_DMAR_PCI_PATH;


/*
 * DMAR Subtables, correspond to Type in ACPI_DMAR_HEADER
 */

/* 0: Hardware Unit Definition */

typedef struct acpi_dmar_hardware_unit
{
    ACPI_DMAR_HEADER        Header;
    UINT8                   Flags;
    UINT8                   Reserved;
    UINT16                  Segment;
    UINT64                  Address;            /* Register Base Address */

} ACPI_DMAR_HARDWARE_UNIT;

/* Masks for Flags field above */

#define ACPI_DMAR_INCLUDE_ALL       (1)


/* 1: Reserved Memory Definition */

typedef struct acpi_dmar_reserved_memory
{
    ACPI_DMAR_HEADER        Header;
    UINT16                  Reserved;
    UINT16                  Segment;
    UINT64                  BaseAddress;        /* 4K aligned base address */
    UINT64                  EndAddress;         /* 4K aligned limit address */

} ACPI_DMAR_RESERVED_MEMORY;

/* Masks for Flags field above */

#define ACPI_DMAR_ALLOW_ALL         (1)


/* 2: Root Port ATS Capability Reporting Structure */

typedef struct acpi_dmar_atsr
{
    ACPI_DMAR_HEADER        Header;
    UINT8                   Flags;
    UINT8                   Reserved;
    UINT16                  Segment;

} ACPI_DMAR_ATSR;

/* Masks for Flags field above */

#define ACPI_DMAR_ALL_PORTS         (1)


/* 3: Remapping Hardware Static Affinity Structure */

typedef struct acpi_dmar_rhsa
{
    ACPI_DMAR_HEADER        Header;
    UINT32                  Reserved;
    UINT64                  BaseAddress;
    UINT32                  ProximityDomain;

} ACPI_DMAR_RHSA;


/* 4: ACPI Namespace Device Declaration Structure */

typedef struct acpi_dmar_andd
{
    ACPI_DMAR_HEADER        Header;
    UINT8                   Reserved[3];
    UINT8                   DeviceNumber;
    char                    DeviceName[1];

} ACPI_DMAR_ANDD;


/*******************************************************************************
 *
 * DRTM - Dynamic Root of Trust for Measurement table
 * Conforms to "TCG D-RTM Architecture" June 17 2013, Version 1.0.0
 * Table version 1
 *
 ******************************************************************************/

typedef struct acpi_table_drtm
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT64                  EntryBaseAddress;
    UINT64                  EntryLength;
    UINT32                  EntryAddress32;
    UINT64                  EntryAddress64;
    UINT64                  ExitAddress;
    UINT64                  LogAreaAddress;
    UINT32                  LogAreaLength;
    UINT64                  ArchDependentAddress;
    UINT32                  Flags;

} ACPI_TABLE_DRTM;

/* Flag Definitions for above */

#define ACPI_DRTM_ACCESS_ALLOWED            (1)
#define ACPI_DRTM_ENABLE_GAP_CODE           (1<<1)
#define ACPI_DRTM_INCOMPLETE_MEASUREMENTS   (1<<2)
#define ACPI_DRTM_AUTHORITY_ORDER           (1<<3)


/* 1) Validated Tables List (64-bit addresses) */

typedef struct acpi_drtm_vtable_list
{
    UINT32                  ValidatedTableCount;
    UINT64                  ValidatedTables[1];

} ACPI_DRTM_VTABLE_LIST;

/* 2) Resources List (of Resource Descriptors) */

/* Resource Descriptor */

typedef struct acpi_drtm_resource
{
    UINT8                   Size[7];
    UINT8                   Type;
    UINT64                  Address;

} ACPI_DRTM_RESOURCE;

typedef struct acpi_drtm_resource_list
{
    UINT32                  ResourceCount;
    ACPI_DRTM_RESOURCE      Resources[1];

} ACPI_DRTM_RESOURCE_LIST;

/* 3) Platform-specific Identifiers List */

typedef struct acpi_drtm_dps_id
{
    UINT32                  DpsIdLength;
    UINT8                   DpsId[16];

} ACPI_DRTM_DPS_ID;


/*******************************************************************************
 *
 * ECDT - Embedded Controller Boot Resources Table
 *        Version 1
 *
 ******************************************************************************/

typedef struct acpi_table_ecdt
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    ACPI_GENERIC_ADDRESS    Control;            /* Address of EC command/status register */
    ACPI_GENERIC_ADDRESS    Data;               /* Address of EC data register */
    UINT32                  Uid;                /* Unique ID - must be same as the EC _UID method */
    UINT8                   Gpe;                /* The GPE for the EC */
    UINT8                   Id[1];              /* Full namepath of the EC in the ACPI namespace */

} ACPI_TABLE_ECDT;


/*******************************************************************************
 *
 * EINJ - Error Injection Table (ACPI 4.0)
 *        Version 1
 *
 ******************************************************************************/

typedef struct acpi_table_einj
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT32                  HeaderLength;
    UINT8                   Flags;
    UINT8                   Reserved[3];
    UINT32                  Entries;

} ACPI_TABLE_EINJ;


/* EINJ Injection Instruction Entries (actions) */

typedef struct acpi_einj_entry
{
    ACPI_WHEA_HEADER        WheaHeader;         /* Common header for WHEA tables */

} ACPI_EINJ_ENTRY;

/* Masks for Flags field above */

#define ACPI_EINJ_PRESERVE          (1)

/* Values for Action field above */

enum AcpiEinjActions
{
    ACPI_EINJ_BEGIN_OPERATION               = 0,
    ACPI_EINJ_GET_TRIGGER_TABLE             = 1,
    ACPI_EINJ_SET_ERROR_TYPE                = 2,
    ACPI_EINJ_GET_ERROR_TYPE                = 3,
    ACPI_EINJ_END_OPERATION                 = 4,
    ACPI_EINJ_EXECUTE_OPERATION             = 5,
    ACPI_EINJ_CHECK_BUSY_STATUS             = 6,
    ACPI_EINJ_GET_COMMAND_STATUS            = 7,
    ACPI_EINJ_SET_ERROR_TYPE_WITH_ADDRESS   = 8,
    ACPI_EINJ_GET_EXECUTE_TIMINGS           = 9,
    ACPI_EINJ_ACTION_RESERVED               = 10,    /* 10 and greater are reserved */
    ACPI_EINJ_TRIGGER_ERROR                 = 0xFF   /* Except for this value */
};

/* Values for Instruction field above */

enum AcpiEinjInstructions
{
    ACPI_EINJ_READ_REGISTER         = 0,
    ACPI_EINJ_READ_REGISTER_VALUE   = 1,
    ACPI_EINJ_WRITE_REGISTER        = 2,
    ACPI_EINJ_WRITE_REGISTER_VALUE  = 3,
    ACPI_EINJ_NOOP                  = 4,
    ACPI_EINJ_FLUSH_CACHELINE       = 5,
    ACPI_EINJ_INSTRUCTION_RESERVED  = 6     /* 6 and greater are reserved */
};

typedef struct acpi_einj_error_type_with_addr
{
    UINT32                  ErrorType;
    UINT32                  VendorStructOffset;
    UINT32                  Flags;
    UINT32                  ApicId;
    UINT64                  Address;
    UINT64                  Range;
    UINT32                  PcieId;

} ACPI_EINJ_ERROR_TYPE_WITH_ADDR;

typedef struct acpi_einj_vendor
{
    UINT32                  Length;
    UINT32                  PcieId;
    UINT16                  VendorId;
    UINT16                  DeviceId;
    UINT8                   RevisionId;
    UINT8                   Reserved[3];

} ACPI_EINJ_VENDOR;


/* EINJ Trigger Error Action Table */

typedef struct acpi_einj_trigger
{
    UINT32                  HeaderSize;
    UINT32                  Revision;
    UINT32                  TableSize;
    UINT32                  EntryCount;

} ACPI_EINJ_TRIGGER;

/* Command status return values */

enum AcpiEinjCommandStatus
{
    ACPI_EINJ_SUCCESS               = 0,
    ACPI_EINJ_FAILURE               = 1,
    ACPI_EINJ_INVALID_ACCESS        = 2,
    ACPI_EINJ_STATUS_RESERVED       = 3     /* 3 and greater are reserved */
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

typedef struct acpi_table_erst
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT32                  HeaderLength;
    UINT32                  Reserved;
    UINT32                  Entries;

} ACPI_TABLE_ERST;


/* ERST Serialization Entries (actions) */

typedef struct acpi_erst_entry
{
    ACPI_WHEA_HEADER        WheaHeader;         /* Common header for WHEA tables */

} ACPI_ERST_ENTRY;

/* Masks for Flags field above */

#define ACPI_ERST_PRESERVE          (1)

/* Values for Action field above */

enum AcpiErstActions
{
    ACPI_ERST_BEGIN_WRITE           = 0,
    ACPI_ERST_BEGIN_READ            = 1,
    ACPI_ERST_BEGIN_CLEAR           = 2,
    ACPI_ERST_END                   = 3,
    ACPI_ERST_SET_RECORD_OFFSET     = 4,
    ACPI_ERST_EXECUTE_OPERATION     = 5,
    ACPI_ERST_CHECK_BUSY_STATUS     = 6,
    ACPI_ERST_GET_COMMAND_STATUS    = 7,
    ACPI_ERST_GET_RECORD_ID         = 8,
    ACPI_ERST_SET_RECORD_ID         = 9,
    ACPI_ERST_GET_RECORD_COUNT      = 10,
    ACPI_ERST_BEGIN_DUMMY_WRIITE    = 11,
    ACPI_ERST_NOT_USED              = 12,
    ACPI_ERST_GET_ERROR_RANGE       = 13,
    ACPI_ERST_GET_ERROR_LENGTH      = 14,
    ACPI_ERST_GET_ERROR_ATTRIBUTES  = 15,
    ACPI_ERST_EXECUTE_TIMINGS       = 16,
    ACPI_ERST_ACTION_RESERVED       = 17    /* 17 and greater are reserved */
};

/* Values for Instruction field above */

enum AcpiErstInstructions
{
    ACPI_ERST_READ_REGISTER         = 0,
    ACPI_ERST_READ_REGISTER_VALUE   = 1,
    ACPI_ERST_WRITE_REGISTER        = 2,
    ACPI_ERST_WRITE_REGISTER_VALUE  = 3,
    ACPI_ERST_NOOP                  = 4,
    ACPI_ERST_LOAD_VAR1             = 5,
    ACPI_ERST_LOAD_VAR2             = 6,
    ACPI_ERST_STORE_VAR1            = 7,
    ACPI_ERST_ADD                   = 8,
    ACPI_ERST_SUBTRACT              = 9,
    ACPI_ERST_ADD_VALUE             = 10,
    ACPI_ERST_SUBTRACT_VALUE        = 11,
    ACPI_ERST_STALL                 = 12,
    ACPI_ERST_STALL_WHILE_TRUE      = 13,
    ACPI_ERST_SKIP_NEXT_IF_TRUE     = 14,
    ACPI_ERST_GOTO                  = 15,
    ACPI_ERST_SET_SRC_ADDRESS_BASE  = 16,
    ACPI_ERST_SET_DST_ADDRESS_BASE  = 17,
    ACPI_ERST_MOVE_DATA             = 18,
    ACPI_ERST_INSTRUCTION_RESERVED  = 19    /* 19 and greater are reserved */
};

/* Command status return values */

enum AcpiErstCommandStatus
{
    ACPI_ERST_SUCESS                = 0,
    ACPI_ERST_NO_SPACE              = 1,
    ACPI_ERST_NOT_AVAILABLE         = 2,
    ACPI_ERST_FAILURE               = 3,
    ACPI_ERST_RECORD_EMPTY          = 4,
    ACPI_ERST_NOT_FOUND             = 5,
    ACPI_ERST_STATUS_RESERVED       = 6     /* 6 and greater are reserved */
};


/* Error Record Serialization Information */

typedef struct acpi_erst_info
{
    UINT16                  Signature;          /* Should be "ER" */
    UINT8                   Data[48];

} ACPI_ERST_INFO;


/*******************************************************************************
 *
 * FPDT - Firmware Performance Data Table (ACPI 5.0)
 *        Version 1
 *
 ******************************************************************************/

typedef struct acpi_table_fpdt
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */

} ACPI_TABLE_FPDT;


/* FPDT subtable header (Performance Record Structure) */

typedef struct acpi_fpdt_header
{
    UINT16                  Type;
    UINT8                   Length;
    UINT8                   Revision;

} ACPI_FPDT_HEADER;

/* Values for Type field above */

enum AcpiFpdtType
{
    ACPI_FPDT_TYPE_BOOT                 = 0,
    ACPI_FPDT_TYPE_S3PERF               = 1
};


/*
 * FPDT subtables
 */

/* 0: Firmware Basic Boot Performance Record */

typedef struct acpi_fpdt_boot_pointer
{
    ACPI_FPDT_HEADER        Header;
    UINT8                   Reserved[4];
    UINT64                  Address;

} ACPI_FPDT_BOOT_POINTER;


/* 1: S3 Performance Table Pointer Record */

typedef struct acpi_fpdt_s3pt_pointer
{
    ACPI_FPDT_HEADER        Header;
    UINT8                   Reserved[4];
    UINT64                  Address;

} ACPI_FPDT_S3PT_POINTER;


/*
 * S3PT - S3 Performance Table. This table is pointed to by the
 * S3 Pointer Record above.
 */
typedef struct acpi_table_s3pt
{
    UINT8                   Signature[4]; /* "S3PT" */
    UINT32                  Length;

} ACPI_TABLE_S3PT;


/*
 * S3PT Subtables (Not part of the actual FPDT)
 */

/* Values for Type field in S3PT header */

enum AcpiS3ptType
{
    ACPI_S3PT_TYPE_RESUME               = 0,
    ACPI_S3PT_TYPE_SUSPEND              = 1,
    ACPI_FPDT_BOOT_PERFORMANCE          = 2
};

typedef struct acpi_s3pt_resume
{
    ACPI_FPDT_HEADER        Header;
    UINT32                  ResumeCount;
    UINT64                  FullResume;
    UINT64                  AverageResume;

} ACPI_S3PT_RESUME;

typedef struct acpi_s3pt_suspend
{
    ACPI_FPDT_HEADER        Header;
    UINT64                  SuspendStart;
    UINT64                  SuspendEnd;

} ACPI_S3PT_SUSPEND;


/*
 * FPDT Boot Performance Record (Not part of the actual FPDT)
 */
typedef struct acpi_fpdt_boot
{
    ACPI_FPDT_HEADER        Header;
    UINT8                   Reserved[4];
    UINT64                  ResetEnd;
    UINT64                  LoadStart;
    UINT64                  StartupStart;
    UINT64                  ExitServicesEntry;
    UINT64                  ExitServicesExit;

} ACPI_FPDT_BOOT;


/*******************************************************************************
 *
 * GTDT - Generic Timer Description Table (ACPI 5.1)
 *        Version 2
 *
 ******************************************************************************/

typedef struct acpi_table_gtdt
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT64                  CounterBlockAddresss;
    UINT32                  Reserved;
    UINT32                  SecureEl1Interrupt;
    UINT32                  SecureEl1Flags;
    UINT32                  NonSecureEl1Interrupt;
    UINT32                  NonSecureEl1Flags;
    UINT32                  VirtualTimerInterrupt;
    UINT32                  VirtualTimerFlags;
    UINT32                  NonSecureEl2Interrupt;
    UINT32                  NonSecureEl2Flags;
    UINT64                  CounterReadBlockAddress;
    UINT32                  PlatformTimerCount;
    UINT32                  PlatformTimerOffset;

} ACPI_TABLE_GTDT;

/* Flag Definitions: Timer Block Physical Timers and Virtual timers */

#define ACPI_GTDT_INTERRUPT_MODE        (1)
#define ACPI_GTDT_INTERRUPT_POLARITY    (1<<1)
#define ACPI_GTDT_ALWAYS_ON             (1<<2)

typedef struct acpi_gtdt_el2
{
    UINT32                  VirtualEL2TimerGsiv;
    UINT32                  VirtualEL2TimerFlags;
} ACPI_GTDT_EL2;


/* Common GTDT subtable header */

typedef struct acpi_gtdt_header
{
    UINT8                   Type;
    UINT16                  Length;

} ACPI_GTDT_HEADER;

/* Values for GTDT subtable type above */

enum AcpiGtdtType
{
    ACPI_GTDT_TYPE_TIMER_BLOCK      = 0,
    ACPI_GTDT_TYPE_WATCHDOG         = 1,
    ACPI_GTDT_TYPE_RESERVED         = 2    /* 2 and greater are reserved */
};


/* GTDT Subtables, correspond to Type in acpi_gtdt_header */

/* 0: Generic Timer Block */

typedef struct acpi_gtdt_timer_block
{
    ACPI_GTDT_HEADER        Header;
    UINT8                   Reserved;
    UINT64                  BlockAddress;
    UINT32                  TimerCount;
    UINT32                  TimerOffset;

} ACPI_GTDT_TIMER_BLOCK;

/* Timer Sub-Structure, one per timer */

typedef struct acpi_gtdt_timer_entry
{
    UINT8                   FrameNumber;
    UINT8                   Reserved[3];
    UINT64                  BaseAddress;
    UINT64                  El0BaseAddress;
    UINT32                  TimerInterrupt;
    UINT32                  TimerFlags;
    UINT32                  VirtualTimerInterrupt;
    UINT32                  VirtualTimerFlags;
    UINT32                  CommonFlags;

} ACPI_GTDT_TIMER_ENTRY;

/* Flag Definitions: TimerFlags and VirtualTimerFlags above */

#define ACPI_GTDT_GT_IRQ_MODE               (1)
#define ACPI_GTDT_GT_IRQ_POLARITY           (1<<1)

/* Flag Definitions: CommonFlags above */

#define ACPI_GTDT_GT_IS_SECURE_TIMER        (1)
#define ACPI_GTDT_GT_ALWAYS_ON              (1<<1)


/* 1: SBSA Generic Watchdog Structure */

typedef struct acpi_gtdt_watchdog
{
    ACPI_GTDT_HEADER        Header;
    UINT8                   Reserved;
    UINT64                  RefreshFrameAddress;
    UINT64                  ControlFrameAddress;
    UINT32                  TimerInterrupt;
    UINT32                  TimerFlags;

} ACPI_GTDT_WATCHDOG;

/* Flag Definitions: TimerFlags above */

#define ACPI_GTDT_WATCHDOG_IRQ_MODE         (1)
#define ACPI_GTDT_WATCHDOG_IRQ_POLARITY     (1<<1)
#define ACPI_GTDT_WATCHDOG_SECURE           (1<<2)


/*******************************************************************************
 *
 * HEST - Hardware Error Source Table (ACPI 4.0)
 *        Version 1
 *
 ******************************************************************************/

typedef struct acpi_table_hest
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT32                  ErrorSourceCount;

} ACPI_TABLE_HEST;


/* HEST subtable header */

typedef struct acpi_hest_header
{
    UINT16                  Type;
    UINT16                  SourceId;

} ACPI_HEST_HEADER;


/* Values for Type field above for subtables */

enum AcpiHestTypes
{
    ACPI_HEST_TYPE_IA32_CHECK           = 0,
    ACPI_HEST_TYPE_IA32_CORRECTED_CHECK = 1,
    ACPI_HEST_TYPE_IA32_NMI             = 2,
    ACPI_HEST_TYPE_NOT_USED3            = 3,
    ACPI_HEST_TYPE_NOT_USED4            = 4,
    ACPI_HEST_TYPE_NOT_USED5            = 5,
    ACPI_HEST_TYPE_AER_ROOT_PORT        = 6,
    ACPI_HEST_TYPE_AER_ENDPOINT         = 7,
    ACPI_HEST_TYPE_AER_BRIDGE           = 8,
    ACPI_HEST_TYPE_GENERIC_ERROR        = 9,
    ACPI_HEST_TYPE_GENERIC_ERROR_V2     = 10,
    ACPI_HEST_TYPE_IA32_DEFERRED_CHECK  = 11,
    ACPI_HEST_TYPE_RESERVED             = 12    /* 12 and greater are reserved */
};


/*
 * HEST substructures contained in subtables
 */

/*
 * IA32 Error Bank(s) - Follows the ACPI_HEST_IA_MACHINE_CHECK and
 * ACPI_HEST_IA_CORRECTED structures.
 */
typedef struct acpi_hest_ia_error_bank
{
    UINT8                   BankNumber;
    UINT8                   ClearStatusOnInit;
    UINT8                   StatusFormat;
    UINT8                   Reserved;
    UINT32                  ControlRegister;
    UINT64                  ControlData;
    UINT32                  StatusRegister;
    UINT32                  AddressRegister;
    UINT32                  MiscRegister;

} ACPI_HEST_IA_ERROR_BANK;


/* Common HEST sub-structure for PCI/AER structures below (6,7,8) */

typedef struct acpi_hest_aer_common
{
    UINT16                  Reserved1;
    UINT8                   Flags;
    UINT8                   Enabled;
    UINT32                  RecordsToPreallocate;
    UINT32                  MaxSectionsPerRecord;
    UINT32                  Bus;                    /* Bus and Segment numbers */
    UINT16                  Device;
    UINT16                  Function;
    UINT16                  DeviceControl;
    UINT16                  Reserved2;
    UINT32                  UncorrectableMask;
    UINT32                  UncorrectableSeverity;
    UINT32                  CorrectableMask;
    UINT32                  AdvancedCapabilities;

} ACPI_HEST_AER_COMMON;

/* Masks for HEST Flags fields */

#define ACPI_HEST_FIRMWARE_FIRST        (1)
#define ACPI_HEST_GLOBAL                (1<<1)
#define ACPI_HEST_GHES_ASSIST           (1<<2)

/*
 * Macros to access the bus/segment numbers in Bus field above:
 *  Bus number is encoded in bits 7:0
 *  Segment number is encoded in bits 23:8
 */
#define ACPI_HEST_BUS(Bus)              ((Bus) & 0xFF)
#define ACPI_HEST_SEGMENT(Bus)          (((Bus) >> 8) & 0xFFFF)


/* Hardware Error Notification */

typedef struct acpi_hest_notify
{
    UINT8                   Type;
    UINT8                   Length;
    UINT16                  ConfigWriteEnable;
    UINT32                  PollInterval;
    UINT32                  Vector;
    UINT32                  PollingThresholdValue;
    UINT32                  PollingThresholdWindow;
    UINT32                  ErrorThresholdValue;
    UINT32                  ErrorThresholdWindow;

} ACPI_HEST_NOTIFY;

/* Values for Notify Type field above */

enum AcpiHestNotifyTypes
{
    ACPI_HEST_NOTIFY_POLLED             = 0,
    ACPI_HEST_NOTIFY_EXTERNAL           = 1,
    ACPI_HEST_NOTIFY_LOCAL              = 2,
    ACPI_HEST_NOTIFY_SCI                = 3,
    ACPI_HEST_NOTIFY_NMI                = 4,
    ACPI_HEST_NOTIFY_CMCI               = 5,    /* ACPI 5.0 */
    ACPI_HEST_NOTIFY_MCE                = 6,    /* ACPI 5.0 */
    ACPI_HEST_NOTIFY_GPIO               = 7,    /* ACPI 6.0 */
    ACPI_HEST_NOTIFY_SEA                = 8,    /* ACPI 6.1 */
    ACPI_HEST_NOTIFY_SEI                = 9,    /* ACPI 6.1 */
    ACPI_HEST_NOTIFY_GSIV               = 10,   /* ACPI 6.1 */
    ACPI_HEST_NOTIFY_SOFTWARE_DELEGATED = 11,   /* ACPI 6.2 */
    ACPI_HEST_NOTIFY_RESERVED           = 12    /* 12 and greater are reserved */
};

/* Values for ConfigWriteEnable bitfield above */

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

typedef struct acpi_hest_ia_machine_check
{
    ACPI_HEST_HEADER        Header;
    UINT16                  Reserved1;
    UINT8                   Flags;              /* See flags ACPI_HEST_GLOBAL, etc. above */
    UINT8                   Enabled;
    UINT32                  RecordsToPreallocate;
    UINT32                  MaxSectionsPerRecord;
    UINT64                  GlobalCapabilityData;
    UINT64                  GlobalControlData;
    UINT8                   NumHardwareBanks;
    UINT8                   Reserved3[7];

} ACPI_HEST_IA_MACHINE_CHECK;


/* 1: IA32 Corrected Machine Check */

typedef struct acpi_hest_ia_corrected
{
    ACPI_HEST_HEADER        Header;
    UINT16                  Reserved1;
    UINT8                   Flags;              /* See flags ACPI_HEST_GLOBAL, etc. above */
    UINT8                   Enabled;
    UINT32                  RecordsToPreallocate;
    UINT32                  MaxSectionsPerRecord;
    ACPI_HEST_NOTIFY        Notify;
    UINT8                   NumHardwareBanks;
    UINT8                   Reserved2[3];

} ACPI_HEST_IA_CORRECTED;


/* 2: IA32 Non-Maskable Interrupt */

typedef struct acpi_hest_ia_nmi
{
    ACPI_HEST_HEADER        Header;
    UINT32                  Reserved;
    UINT32                  RecordsToPreallocate;
    UINT32                  MaxSectionsPerRecord;
    UINT32                  MaxRawDataLength;

} ACPI_HEST_IA_NMI;


/* 3,4,5: Not used */

/* 6: PCI Express Root Port AER */

typedef struct acpi_hest_aer_root
{
    ACPI_HEST_HEADER        Header;
    ACPI_HEST_AER_COMMON    Aer;
    UINT32                  RootErrorCommand;

} ACPI_HEST_AER_ROOT;


/* 7: PCI Express AER (AER Endpoint) */

typedef struct acpi_hest_aer
{
    ACPI_HEST_HEADER        Header;
    ACPI_HEST_AER_COMMON    Aer;

} ACPI_HEST_AER;


/* 8: PCI Express/PCI-X Bridge AER */

typedef struct acpi_hest_aer_bridge
{
    ACPI_HEST_HEADER        Header;
    ACPI_HEST_AER_COMMON    Aer;
    UINT32                  UncorrectableMask2;
    UINT32                  UncorrectableSeverity2;
    UINT32                  AdvancedCapabilities2;

} ACPI_HEST_AER_BRIDGE;


/* 9: Generic Hardware Error Source */

typedef struct acpi_hest_generic
{
    ACPI_HEST_HEADER        Header;
    UINT16                  RelatedSourceId;
    UINT8                   Reserved;
    UINT8                   Enabled;
    UINT32                  RecordsToPreallocate;
    UINT32                  MaxSectionsPerRecord;
    UINT32                  MaxRawDataLength;
    ACPI_GENERIC_ADDRESS    ErrorStatusAddress;
    ACPI_HEST_NOTIFY        Notify;
    UINT32                  ErrorBlockLength;

} ACPI_HEST_GENERIC;


/* 10: Generic Hardware Error Source, version 2 */

typedef struct acpi_hest_generic_v2
{
    ACPI_HEST_HEADER        Header;
    UINT16                  RelatedSourceId;
    UINT8                   Reserved;
    UINT8                   Enabled;
    UINT32                  RecordsToPreallocate;
    UINT32                  MaxSectionsPerRecord;
    UINT32                  MaxRawDataLength;
    ACPI_GENERIC_ADDRESS    ErrorStatusAddress;
    ACPI_HEST_NOTIFY        Notify;
    UINT32                  ErrorBlockLength;
    ACPI_GENERIC_ADDRESS    ReadAckRegister;
    UINT64                  ReadAckPreserve;
    UINT64                  ReadAckWrite;

} ACPI_HEST_GENERIC_V2;


/* Generic Error Status block */

typedef struct acpi_hest_generic_status
{
    UINT32                  BlockStatus;
    UINT32                  RawDataOffset;
    UINT32                  RawDataLength;
    UINT32                  DataLength;
    UINT32                  ErrorSeverity;

} ACPI_HEST_GENERIC_STATUS;

/* Values for BlockStatus flags above */

#define ACPI_HEST_UNCORRECTABLE             (1)
#define ACPI_HEST_CORRECTABLE               (1<<1)
#define ACPI_HEST_MULTIPLE_UNCORRECTABLE    (1<<2)
#define ACPI_HEST_MULTIPLE_CORRECTABLE      (1<<3)
#define ACPI_HEST_ERROR_ENTRY_COUNT         (0xFF<<4) /* 8 bits, error count */


/* Generic Error Data entry */

typedef struct acpi_hest_generic_data
{
    UINT8                   SectionType[16];
    UINT32                  ErrorSeverity;
    UINT16                  Revision;
    UINT8                   ValidationBits;
    UINT8                   Flags;
    UINT32                  ErrorDataLength;
    UINT8                   FruId[16];
    UINT8                   FruText[20];

} ACPI_HEST_GENERIC_DATA;

/* Extension for revision 0x0300 */

typedef struct acpi_hest_generic_data_v300
{
    UINT8                   SectionType[16];
    UINT32                  ErrorSeverity;
    UINT16                  Revision;
    UINT8                   ValidationBits;
    UINT8                   Flags;
    UINT32                  ErrorDataLength;
    UINT8                   FruId[16];
    UINT8                   FruText[20];
    UINT64                  TimeStamp;

} ACPI_HEST_GENERIC_DATA_V300;

/* Values for ErrorSeverity above */

#define ACPI_HEST_GEN_ERROR_RECOVERABLE     0
#define ACPI_HEST_GEN_ERROR_FATAL           1
#define ACPI_HEST_GEN_ERROR_CORRECTED       2
#define ACPI_HEST_GEN_ERROR_NONE            3

/* Flags for ValidationBits above */

#define ACPI_HEST_GEN_VALID_FRU_ID          (1)
#define ACPI_HEST_GEN_VALID_FRU_STRING      (1<<1)
#define ACPI_HEST_GEN_VALID_TIMESTAMP       (1<<2)


/* 11: IA32 Deferred Machine Check Exception (ACPI 6.2) */

typedef struct acpi_hest_ia_deferred_check
{
    ACPI_HEST_HEADER        Header;
    UINT16                  Reserved1;
    UINT8                   Flags;              /* See flags ACPI_HEST_GLOBAL, etc. above */
    UINT8                   Enabled;
    UINT32                  RecordsToPreallocate;
    UINT32                  MaxSectionsPerRecord;
    ACPI_HEST_NOTIFY        Notify;
    UINT8                   NumHardwareBanks;
    UINT8                   Reserved2[3];

} ACPI_HEST_IA_DEFERRED_CHECK;


/*******************************************************************************
 *
 * HMAT - Heterogeneous Memory Attributes Table (ACPI 6.2)
 *        Version 1
 *
 ******************************************************************************/

typedef struct acpi_table_hmat
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT32                  Reserved;

} ACPI_TABLE_HMAT;


/* Values for HMAT structure types */

enum AcpiHmatType
{
    ACPI_HMAT_TYPE_ADDRESS_RANGE        = 0,   /* Memory subsystem address range */
    ACPI_HMAT_TYPE_LOCALITY             = 1,   /* System locality latency and bandwidth information */
    ACPI_HMAT_TYPE_CACHE                = 2,   /* Memory side cache information */
    ACPI_HMAT_TYPE_RESERVED             = 3    /* 3 and greater are reserved */
};

typedef struct acpi_hmat_structure
{
    UINT16                  Type;
    UINT16                  Reserved;
    UINT32                  Length;

} ACPI_HMAT_STRUCTURE;


/*
 * HMAT Structures, correspond to Type in ACPI_HMAT_STRUCTURE
 */

/* 0: Memory proximity domain attributes */

typedef struct acpi_hmat_proximity_domain
{
    ACPI_HMAT_STRUCTURE     Header;
    UINT16                  Flags;
    UINT16                  Reserved1;
    UINT32                  ProcessorPD;            /* Processor proximity domain */
    UINT32                  MemoryPD;               /* Memory proximity domain */
    UINT32                  Reserved2;
    UINT64                  Reserved3;
    UINT64                  Reserved4;

} ACPI_HMAT_PROXIMITY_DOMAIN;

/* Masks for Flags field above */

#define ACPI_HMAT_PROCESSOR_PD_VALID    (1)     /* 1: ProcessorPD field is valid */
#define ACPI_HMAT_MEMORY_PD_VALID       (1<<1)  /* 1: MemoryPD field is valid */
#define ACPI_HMAT_RESERVATION_HINT      (1<<2)  /* 1: Reservation hint */


/* 1: System locality latency and bandwidth information */

typedef struct acpi_hmat_locality
{
    ACPI_HMAT_STRUCTURE     Header;
    UINT8                   Flags;
    UINT8                   DataType;
    UINT16                  Reserved1;
    UINT32                  NumberOfInitiatorPDs;
    UINT32                  NumberOfTargetPDs;
    UINT32                  Reserved2;
    UINT64                  EntryBaseUnit;

} ACPI_HMAT_LOCALITY;

/* Masks for Flags field above */

#define ACPI_HMAT_MEMORY_HIERARCHY  (0x0F)

/* Values for Memory Hierarchy flag */

#define ACPI_HMAT_MEMORY            0
#define ACPI_HMAT_LAST_LEVEL_CACHE  1
#define ACPI_HMAT_1ST_LEVEL_CACHE   2
#define ACPI_HMAT_2ND_LEVEL_CACHE   3
#define ACPI_HMAT_3RD_LEVEL_CACHE   4

/* Values for DataType field above */

#define ACPI_HMAT_ACCESS_LATENCY    0
#define ACPI_HMAT_READ_LATENCY      1
#define ACPI_HMAT_WRITE_LATENCY     2
#define ACPI_HMAT_ACCESS_BANDWIDTH  3
#define ACPI_HMAT_READ_BANDWIDTH    4
#define ACPI_HMAT_WRITE_BANDWIDTH   5


/* 2: Memory side cache information */

typedef struct acpi_hmat_cache
{
    ACPI_HMAT_STRUCTURE     Header;
    UINT32                  MemoryPD;
    UINT32                  Reserved1;
    UINT64                  CacheSize;
    UINT32                  CacheAttributes;
    UINT16                  Reserved2;
    UINT16                  NumberOfSMBIOSHandles;

} ACPI_HMAT_CACHE;

/* Masks for CacheAttributes field above */

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

typedef struct acpi_table_hpet
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT32                  Id;                 /* Hardware ID of event timer block */
    ACPI_GENERIC_ADDRESS    Address;            /* Address of event timer block */
    UINT8                   Sequence;           /* HPET sequence number */
    UINT16                  MinimumTick;        /* Main counter min tick, periodic mode */
    UINT8                   Flags;

} ACPI_TABLE_HPET;

/* Masks for Flags field above */

#define ACPI_HPET_PAGE_PROTECT_MASK (3)

/* Values for Page Protect flags */

enum AcpiHpetPageProtect
{
    ACPI_HPET_NO_PAGE_PROTECT       = 0,
    ACPI_HPET_PAGE_PROTECT4         = 1,
    ACPI_HPET_PAGE_PROTECT64        = 2
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

typedef struct acpi_table_ibft
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT8                   Reserved[12];

} ACPI_TABLE_IBFT;


/* IBFT common subtable header */

typedef struct acpi_ibft_header
{
    UINT8                   Type;
    UINT8                   Version;
    UINT16                  Length;
    UINT8                   Index;
    UINT8                   Flags;

} ACPI_IBFT_HEADER;

/* Values for Type field above */

enum AcpiIbftType
{
    ACPI_IBFT_TYPE_NOT_USED         = 0,
    ACPI_IBFT_TYPE_CONTROL          = 1,
    ACPI_IBFT_TYPE_INITIATOR        = 2,
    ACPI_IBFT_TYPE_NIC              = 3,
    ACPI_IBFT_TYPE_TARGET           = 4,
    ACPI_IBFT_TYPE_EXTENSIONS       = 5,
    ACPI_IBFT_TYPE_RESERVED         = 6     /* 6 and greater are reserved */
};


/* IBFT subtables */

typedef struct acpi_ibft_control
{
    ACPI_IBFT_HEADER        Header;
    UINT16                  Extensions;
    UINT16                  InitiatorOffset;
    UINT16                  Nic0Offset;
    UINT16                  Target0Offset;
    UINT16                  Nic1Offset;
    UINT16                  Target1Offset;

} ACPI_IBFT_CONTROL;

typedef struct acpi_ibft_initiator
{
    ACPI_IBFT_HEADER        Header;
    UINT8                   SnsServer[16];
    UINT8                   SlpServer[16];
    UINT8                   PrimaryServer[16];
    UINT8                   SecondaryServer[16];
    UINT16                  NameLength;
    UINT16                  NameOffset;

} ACPI_IBFT_INITIATOR;

typedef struct acpi_ibft_nic
{
    ACPI_IBFT_HEADER        Header;
    UINT8                   IpAddress[16];
    UINT8                   SubnetMaskPrefix;
    UINT8                   Origin;
    UINT8                   Gateway[16];
    UINT8                   PrimaryDns[16];
    UINT8                   SecondaryDns[16];
    UINT8                   Dhcp[16];
    UINT16                  Vlan;
    UINT8                   MacAddress[6];
    UINT16                  PciAddress;
    UINT16                  NameLength;
    UINT16                  NameOffset;

} ACPI_IBFT_NIC;

typedef struct acpi_ibft_target
{
    ACPI_IBFT_HEADER        Header;
    UINT8                   TargetIpAddress[16];
    UINT16                  TargetIpSocket;
    UINT8                   TargetBootLun[8];
    UINT8                   ChapType;
    UINT8                   NicAssociation;
    UINT16                  TargetNameLength;
    UINT16                  TargetNameOffset;
    UINT16                  ChapNameLength;
    UINT16                  ChapNameOffset;
    UINT16                  ChapSecretLength;
    UINT16                  ChapSecretOffset;
    UINT16                  ReverseChapNameLength;
    UINT16                  ReverseChapNameOffset;
    UINT16                  ReverseChapSecretLength;
    UINT16                  ReverseChapSecretOffset;

} ACPI_IBFT_TARGET;


/* Reset to default packing */

#pragma pack()

#endif /* __ACTBL1_H__ */
