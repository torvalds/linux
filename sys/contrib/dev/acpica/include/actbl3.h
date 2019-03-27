/******************************************************************************
 *
 * Name: actbl3.h - ACPI Table Definitions
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

#ifndef __ACTBL3_H__
#define __ACTBL3_H__


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
#define ACPI_SIG_SLIC           "SLIC"      /* Software Licensing Description Table */
#define ACPI_SIG_SLIT           "SLIT"      /* System Locality Distance Information Table */
#define ACPI_SIG_SPCR           "SPCR"      /* Serial Port Console Redirection table */
#define ACPI_SIG_SPMI           "SPMI"      /* Server Platform Management Interface table */
#define ACPI_SIG_SRAT           "SRAT"      /* System Resource Affinity Table */
#define ACPI_SIG_STAO           "STAO"      /* Status Override table */
#define ACPI_SIG_TCPA           "TCPA"      /* Trusted Computing Platform Alliance table */
#define ACPI_SIG_TPM2           "TPM2"      /* Trusted Platform Module 2.0 H/W interface table */
#define ACPI_SIG_UEFI           "UEFI"      /* Uefi Boot Optimization Table */
#define ACPI_SIG_VRTC           "VRTC"      /* Virtual Real Time Clock Table */
#define ACPI_SIG_WAET           "WAET"      /* Windows ACPI Emulated devices Table */
#define ACPI_SIG_WDAT           "WDAT"      /* Watchdog Action Table */
#define ACPI_SIG_WDDT           "WDDT"      /* Watchdog Timer Description Table */
#define ACPI_SIG_WDRT           "WDRT"      /* Watchdog Resource Table */
#define ACPI_SIG_WPBT           "WPBT"      /* Windows Platform Binary Table */
#define ACPI_SIG_WSMT           "WSMT"      /* Windows SMM Security Migrations Table */
#define ACPI_SIG_XENV           "XENV"      /* Xen Environment table */
#define ACPI_SIG_XXXX           "XXXX"      /* Intermediate AML header for ASL/ASL+ converter */

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
 * SLIC - Software Licensing Description Table
 *
 * Conforms to "Microsoft Software Licensing Tables (SLIC and MSDM)",
 * November 29, 2011. Copyright 2011 Microsoft
 *
 ******************************************************************************/

/* Basic SLIC table is only the common ACPI header */

typedef struct acpi_table_slic
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */

} ACPI_TABLE_SLIC;


/*******************************************************************************
 *
 * SLIT - System Locality Distance Information Table
 *        Version 1
 *
 ******************************************************************************/

typedef struct acpi_table_slit
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT64                  LocalityCount;
    UINT8                   Entry[1];           /* Real size = localities^2 */

} ACPI_TABLE_SLIT;


/*******************************************************************************
 *
 * SPCR - Serial Port Console Redirection table
 *        Version 2
 *
 * Conforms to "Serial Port Console Redirection Table",
 * Version 1.03, August 10, 2015
 *
 ******************************************************************************/

typedef struct acpi_table_spcr
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT8                   InterfaceType;      /* 0=full 16550, 1=subset of 16550 */
    UINT8                   Reserved[3];
    ACPI_GENERIC_ADDRESS    SerialPort;
    UINT8                   InterruptType;
    UINT8                   PcInterrupt;
    UINT32                  Interrupt;
    UINT8                   BaudRate;
    UINT8                   Parity;
    UINT8                   StopBits;
    UINT8                   FlowControl;
    UINT8                   TerminalType;
    UINT8                   Reserved1;
    UINT16                  PciDeviceId;
    UINT16                  PciVendorId;
    UINT8                   PciBus;
    UINT8                   PciDevice;
    UINT8                   PciFunction;
    UINT32                  PciFlags;
    UINT8                   PciSegment;
    UINT32                  Reserved2;

} ACPI_TABLE_SPCR;

/* Masks for PciFlags field above */

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

typedef struct acpi_table_spmi
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT8                   InterfaceType;
    UINT8                   Reserved;           /* Must be 1 */
    UINT16                  SpecRevision;       /* Version of IPMI */
    UINT8                   InterruptType;
    UINT8                   GpeNumber;          /* GPE assigned */
    UINT8                   Reserved1;
    UINT8                   PciDeviceFlag;
    UINT32                  Interrupt;
    ACPI_GENERIC_ADDRESS    IpmiRegister;
    UINT8                   PciSegment;
    UINT8                   PciBus;
    UINT8                   PciDevice;
    UINT8                   PciFunction;
    UINT8                   Reserved2;

} ACPI_TABLE_SPMI;

/* Values for InterfaceType above */

enum AcpiSpmiInterfaceTypes
{
    ACPI_SPMI_NOT_USED              = 0,
    ACPI_SPMI_KEYBOARD              = 1,
    ACPI_SPMI_SMI                   = 2,
    ACPI_SPMI_BLOCK_TRANSFER        = 3,
    ACPI_SPMI_SMBUS                 = 4,
    ACPI_SPMI_RESERVED              = 5         /* 5 and above are reserved */
};


/*******************************************************************************
 *
 * SRAT - System Resource Affinity Table
 *        Version 3
 *
 ******************************************************************************/

typedef struct acpi_table_srat
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT32                  TableRevision;      /* Must be value '1' */
    UINT64                  Reserved;           /* Reserved, must be zero */

} ACPI_TABLE_SRAT;

/* Values for subtable type in ACPI_SUBTABLE_HEADER */

enum AcpiSratType
{
    ACPI_SRAT_TYPE_CPU_AFFINITY         = 0,
    ACPI_SRAT_TYPE_MEMORY_AFFINITY      = 1,
    ACPI_SRAT_TYPE_X2APIC_CPU_AFFINITY  = 2,
    ACPI_SRAT_TYPE_GICC_AFFINITY        = 3,
    ACPI_SRAT_TYPE_GIC_ITS_AFFINITY     = 4, /* ACPI 6.2 */
    ACPI_SRAT_TYPE_GENERIC_AFFINITY     = 5, /* ACPI 6.3 */
    ACPI_SRAT_TYPE_RESERVED             = 6  /* 5 and greater are reserved */
};

/*
 * SRAT Subtables, correspond to Type in ACPI_SUBTABLE_HEADER
 */

/* 0: Processor Local APIC/SAPIC Affinity */

typedef struct acpi_srat_cpu_affinity
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT8                   ProximityDomainLo;
    UINT8                   ApicId;
    UINT32                  Flags;
    UINT8                   LocalSapicEid;
    UINT8                   ProximityDomainHi[3];
    UINT32                  ClockDomain;

} ACPI_SRAT_CPU_AFFINITY;

/* Flags */

#define ACPI_SRAT_CPU_USE_AFFINITY  (1)         /* 00: Use affinity structure */


/* 1: Memory Affinity */

typedef struct acpi_srat_mem_affinity
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT32                  ProximityDomain;
    UINT16                  Reserved;           /* Reserved, must be zero */
    UINT64                  BaseAddress;
    UINT64                  Length;
    UINT32                  Reserved1;
    UINT32                  Flags;
    UINT64                  Reserved2;          /* Reserved, must be zero */

} ACPI_SRAT_MEM_AFFINITY;

/* Flags */

#define ACPI_SRAT_MEM_ENABLED       (1)         /* 00: Use affinity structure */
#define ACPI_SRAT_MEM_HOT_PLUGGABLE (1<<1)      /* 01: Memory region is hot pluggable */
#define ACPI_SRAT_MEM_NON_VOLATILE  (1<<2)      /* 02: Memory region is non-volatile */


/* 2: Processor Local X2_APIC Affinity (ACPI 4.0) */

typedef struct acpi_srat_x2apic_cpu_affinity
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT16                  Reserved;           /* Reserved, must be zero */
    UINT32                  ProximityDomain;
    UINT32                  ApicId;
    UINT32                  Flags;
    UINT32                  ClockDomain;
    UINT32                  Reserved2;

} ACPI_SRAT_X2APIC_CPU_AFFINITY;

/* Flags for ACPI_SRAT_CPU_AFFINITY and ACPI_SRAT_X2APIC_CPU_AFFINITY */

#define ACPI_SRAT_CPU_ENABLED       (1)         /* 00: Use affinity structure */


/* 3: GICC Affinity (ACPI 5.1) */

typedef struct acpi_srat_gicc_affinity
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT32                  ProximityDomain;
    UINT32                  AcpiProcessorUid;
    UINT32                  Flags;
    UINT32                  ClockDomain;

} ACPI_SRAT_GICC_AFFINITY;

/* Flags for ACPI_SRAT_GICC_AFFINITY */

#define ACPI_SRAT_GICC_ENABLED     (1)         /* 00: Use affinity structure */


/* 4: GCC ITS Affinity (ACPI 6.2) */

typedef struct acpi_srat_gic_its_affinity
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT32                  ProximityDomain;
    UINT16                  Reserved;
    UINT32                  ItsId;

} ACPI_SRAT_GIC_ITS_AFFINITY;


/* 5: Generic Initiator Affinity Structure (ACPI 6.3) */

typedef struct acpi_srat_generic_affinity
{
    ACPI_SUBTABLE_HEADER    Header;
    UINT8                   Reserved;
    UINT8                   DeviceHandleType;
    UINT32                  ProximityDomain;
    UINT8                   DeviceHandle[16];
    UINT32                  Flags;
    UINT32                  Reserved1;

} ACPI_SRAT_GENERIC_AFFINITY;

/* Flags for ACPI_SRAT_GENERIC_AFFINITY */

#define ACPI_SRAT_GENERIC_AFFINITY_ENABLED (1) /* 00: Use affinity structure */

/*******************************************************************************
 *
 * STAO - Status Override Table (_STA override) - ACPI 6.0
 *        Version 1
 *
 * Conforms to "ACPI Specification for Status Override Table"
 * 6 January 2015
 *
 ******************************************************************************/

typedef struct acpi_table_stao
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT8                   IgnoreUart;

} ACPI_TABLE_STAO;


/*******************************************************************************
 *
 * TCPA - Trusted Computing Platform Alliance table
 *        Version 2
 *
 * TCG Hardware Interface Table for TPM 1.2 Clients and Servers
 *
 * Conforms to "TCG ACPI Specification, Family 1.2 and 2.0",
 * Version 1.2, Revision 8
 * February 27, 2017
 *
 * NOTE: There are two versions of the table with the same signature --
 * the client version and the server version. The common PlatformClass
 * field is used to differentiate the two types of tables.
 *
 ******************************************************************************/

typedef struct acpi_table_tcpa_hdr
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT16                  PlatformClass;

} ACPI_TABLE_TCPA_HDR;

/*
 * Values for PlatformClass above.
 * This is how the client and server subtables are differentiated
 */
#define ACPI_TCPA_CLIENT_TABLE          0
#define ACPI_TCPA_SERVER_TABLE          1


typedef struct acpi_table_tcpa_client
{
    UINT32                  MinimumLogLength;   /* Minimum length for the event log area */
    UINT64                  LogAddress;         /* Address of the event log area */

} ACPI_TABLE_TCPA_CLIENT;

typedef struct acpi_table_tcpa_server
{
    UINT16                  Reserved;
    UINT64                  MinimumLogLength;   /* Minimum length for the event log area */
    UINT64                  LogAddress;         /* Address of the event log area */
    UINT16                  SpecRevision;
    UINT8                   DeviceFlags;
    UINT8                   InterruptFlags;
    UINT8                   GpeNumber;
    UINT8                   Reserved2[3];
    UINT32                  GlobalInterrupt;
    ACPI_GENERIC_ADDRESS    Address;
    UINT32                  Reserved3;
    ACPI_GENERIC_ADDRESS    ConfigAddress;
    UINT8                   Group;
    UINT8                   Bus;                /* PCI Bus/Segment/Function numbers */
    UINT8                   Device;
    UINT8                   Function;

} ACPI_TABLE_TCPA_SERVER;

/* Values for DeviceFlags above */

#define ACPI_TCPA_PCI_DEVICE            (1)
#define ACPI_TCPA_BUS_PNP               (1<<1)
#define ACPI_TCPA_ADDRESS_VALID         (1<<2)

/* Values for InterruptFlags above */

#define ACPI_TCPA_INTERRUPT_MODE        (1)
#define ACPI_TCPA_INTERRUPT_POLARITY    (1<<1)
#define ACPI_TCPA_SCI_VIA_GPE           (1<<2)
#define ACPI_TCPA_GLOBAL_INTERRUPT      (1<<3)


/*******************************************************************************
 *
 * TPM2 - Trusted Platform Module (TPM) 2.0 Hardware Interface Table
 *        Version 4
 *
 * TCG Hardware Interface Table for TPM 2.0 Clients and Servers
 *
 * Conforms to "TCG ACPI Specification, Family 1.2 and 2.0",
 * Version 1.2, Revision 8
 * February 27, 2017
 *
 ******************************************************************************/

/* Revision 3 */

typedef struct acpi_table_tpm23
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT32                  Reserved;
    UINT64                  ControlAddress;
    UINT32                  StartMethod;

} ACPI_TABLE_TPM23;

/* Value for StartMethod above */

#define ACPI_TPM23_ACPI_START_METHOD                 2

/*
 * Optional trailer for revision 3. If start method is 2, there is a 4 byte
 * reserved area of all zeros.
 */
typedef struct acpi_tmp23_trailer
{
    UINT32                  Reserved;

} ACPI_TPM23_TRAILER;


/* Revision 4 */

typedef struct acpi_table_tpm2
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT16                  PlatformClass;
    UINT16                  Reserved;
    UINT64                  ControlAddress;
    UINT32                  StartMethod;

    /* Platform-specific data follows */

} ACPI_TABLE_TPM2;

/* Values for StartMethod above */

#define ACPI_TPM2_NOT_ALLOWED                       0
#define ACPI_TPM2_RESERVED1                         1
#define ACPI_TPM2_START_METHOD                      2
#define ACPI_TPM2_RESERVED3                         3
#define ACPI_TPM2_RESERVED4                         4
#define ACPI_TPM2_RESERVED5                         5
#define ACPI_TPM2_MEMORY_MAPPED                     6
#define ACPI_TPM2_COMMAND_BUFFER                    7
#define ACPI_TPM2_COMMAND_BUFFER_WITH_START_METHOD  8
#define ACPI_TPM2_RESERVED9                         9
#define ACPI_TPM2_RESERVED10                        10
#define ACPI_TPM2_COMMAND_BUFFER_WITH_ARM_SMC       11  /* V1.2 Rev 8 */
#define ACPI_TPM2_RESERVED                          12


/* Optional trailer appears after any StartMethod subtables */

typedef struct acpi_tpm2_trailer
{
    UINT8                   MethodParameters[12];
    UINT32                  MinimumLogLength;   /* Minimum length for the event log area */
    UINT64                  LogAddress;         /* Address of the event log area */

} ACPI_TPM2_TRAILER;


/*
 * Subtables (StartMethod-specific)
 */

/* 11: Start Method for ARM SMC (V1.2 Rev 8) */

typedef struct acpi_tpm2_arm_smc
{
    UINT32                  GlobalInterrupt;
    UINT8                   InterruptFlags;
    UINT8                   OperationFlags;
    UINT16                  Reserved;
    UINT32                  FunctionId;

} ACPI_TPM2_ARM_SMC;

/* Values for InterruptFlags above */

#define ACPI_TPM2_INTERRUPT_SUPPORT     (1)

/* Values for OperationFlags above */

#define ACPI_TPM2_IDLE_SUPPORT          (1)


/*******************************************************************************
 *
 * UEFI - UEFI Boot optimization Table
 *        Version 1
 *
 * Conforms to "Unified Extensible Firmware Interface Specification",
 * Version 2.3, May 8, 2009
 *
 ******************************************************************************/

typedef struct acpi_table_uefi
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT8                   Identifier[16];     /* UUID identifier */
    UINT16                  DataOffset;         /* Offset of remaining data in table */

} ACPI_TABLE_UEFI;


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

typedef struct acpi_table_vrtc
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */

} ACPI_TABLE_VRTC;

/* VRTC entry */

typedef struct acpi_vrtc_entry
{
    ACPI_GENERIC_ADDRESS    PhysicalAddress;
    UINT32                  Irq;

} ACPI_VRTC_ENTRY;


/*******************************************************************************
 *
 * WAET - Windows ACPI Emulated devices Table
 *        Version 1
 *
 * Conforms to "Windows ACPI Emulated Devices Table", version 1.0, April 6, 2009
 *
 ******************************************************************************/

typedef struct acpi_table_waet
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT32                  Flags;

} ACPI_TABLE_WAET;

/* Masks for Flags field above */

#define ACPI_WAET_RTC_NO_ACK        (1)         /* RTC requires no int acknowledge */
#define ACPI_WAET_TIMER_ONE_READ    (1<<1)      /* PM timer requires only one read */


/*******************************************************************************
 *
 * WDAT - Watchdog Action Table
 *        Version 1
 *
 * Conforms to "Hardware Watchdog Timers Design Specification",
 * Copyright 2006 Microsoft Corporation.
 *
 ******************************************************************************/

typedef struct acpi_table_wdat
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT32                  HeaderLength;       /* Watchdog Header Length */
    UINT16                  PciSegment;         /* PCI Segment number */
    UINT8                   PciBus;             /* PCI Bus number */
    UINT8                   PciDevice;          /* PCI Device number */
    UINT8                   PciFunction;        /* PCI Function number */
    UINT8                   Reserved[3];
    UINT32                  TimerPeriod;        /* Period of one timer count (msec) */
    UINT32                  MaxCount;           /* Maximum counter value supported */
    UINT32                  MinCount;           /* Minimum counter value */
    UINT8                   Flags;
    UINT8                   Reserved2[3];
    UINT32                  Entries;            /* Number of watchdog entries that follow */

} ACPI_TABLE_WDAT;

/* Masks for Flags field above */

#define ACPI_WDAT_ENABLED           (1)
#define ACPI_WDAT_STOPPED           0x80


/* WDAT Instruction Entries (actions) */

typedef struct acpi_wdat_entry
{
    UINT8                   Action;
    UINT8                   Instruction;
    UINT16                  Reserved;
    ACPI_GENERIC_ADDRESS    RegisterRegion;
    UINT32                  Value;              /* Value used with Read/Write register */
    UINT32                  Mask;               /* Bitmask required for this register instruction */

} ACPI_WDAT_ENTRY;

/* Values for Action field above */

enum AcpiWdatActions
{
    ACPI_WDAT_RESET                 = 1,
    ACPI_WDAT_GET_CURRENT_COUNTDOWN = 4,
    ACPI_WDAT_GET_COUNTDOWN         = 5,
    ACPI_WDAT_SET_COUNTDOWN         = 6,
    ACPI_WDAT_GET_RUNNING_STATE     = 8,
    ACPI_WDAT_SET_RUNNING_STATE     = 9,
    ACPI_WDAT_GET_STOPPED_STATE     = 10,
    ACPI_WDAT_SET_STOPPED_STATE     = 11,
    ACPI_WDAT_GET_REBOOT            = 16,
    ACPI_WDAT_SET_REBOOT            = 17,
    ACPI_WDAT_GET_SHUTDOWN          = 18,
    ACPI_WDAT_SET_SHUTDOWN          = 19,
    ACPI_WDAT_GET_STATUS            = 32,
    ACPI_WDAT_SET_STATUS            = 33,
    ACPI_WDAT_ACTION_RESERVED       = 34    /* 34 and greater are reserved */
};

/* Values for Instruction field above */

enum AcpiWdatInstructions
{
    ACPI_WDAT_READ_VALUE            = 0,
    ACPI_WDAT_READ_COUNTDOWN        = 1,
    ACPI_WDAT_WRITE_VALUE           = 2,
    ACPI_WDAT_WRITE_COUNTDOWN       = 3,
    ACPI_WDAT_INSTRUCTION_RESERVED  = 4,    /* 4 and greater are reserved */
    ACPI_WDAT_PRESERVE_REGISTER     = 0x80  /* Except for this value */
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

typedef struct acpi_table_wddt
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT16                  SpecVersion;
    UINT16                  TableVersion;
    UINT16                  PciVendorId;
    ACPI_GENERIC_ADDRESS    Address;
    UINT16                  MaxCount;           /* Maximum counter value supported */
    UINT16                  MinCount;           /* Minimum counter value supported */
    UINT16                  Period;
    UINT16                  Status;
    UINT16                  Capability;

} ACPI_TABLE_WDDT;

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

typedef struct acpi_table_wdrt
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    ACPI_GENERIC_ADDRESS    ControlRegister;
    ACPI_GENERIC_ADDRESS    CountRegister;
    UINT16                  PciDeviceId;
    UINT16                  PciVendorId;
    UINT8                   PciBus;             /* PCI Bus number */
    UINT8                   PciDevice;          /* PCI Device number */
    UINT8                   PciFunction;        /* PCI Function number */
    UINT8                   PciSegment;         /* PCI Segment number */
    UINT16                  MaxCount;           /* Maximum counter value supported */
    UINT8                   Units;

} ACPI_TABLE_WDRT;


/*******************************************************************************
 *
 * WPBT - Windows Platform Environment Table (ACPI 6.0)
 *        Version 1
 *
 * Conforms to "Windows Platform Binary Table (WPBT)" 29 November 2011
 *
 ******************************************************************************/

typedef struct acpi_table_wpbt
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT32                  HandoffSize;
    UINT64                  HandoffAddress;
    UINT8                   Layout;
    UINT8                   Type;
    UINT16                  ArgumentsLength;

} ACPI_TABLE_WPBT;


/*******************************************************************************
 *
 * WSMT - Windows SMM Security Migrations Table
 *        Version 1
 *
 * Conforms to "Windows SMM Security Migrations Table",
 * Version 1.0, April 18, 2016
 *
 ******************************************************************************/

typedef struct acpi_table_wsmt
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT32                  ProtectionFlags;

} ACPI_TABLE_WSMT;

/* Flags for ProtectionFlags field above */

#define ACPI_WSMT_FIXED_COMM_BUFFERS                (1)
#define ACPI_WSMT_COMM_BUFFER_NESTED_PTR_PROTECTION (2)
#define ACPI_WSMT_SYSTEM_RESOURCE_PROTECTION        (4)


/*******************************************************************************
 *
 * XENV - Xen Environment Table (ACPI 6.0)
 *        Version 1
 *
 * Conforms to "ACPI Specification for Xen Environment Table" 4 January 2015
 *
 ******************************************************************************/

typedef struct acpi_table_xenv
{
    ACPI_TABLE_HEADER       Header;             /* Common ACPI table header */
    UINT64                  GrantTableAddress;
    UINT64                  GrantTableSize;
    UINT32                  EventInterrupt;
    UINT8                   EventFlags;

} ACPI_TABLE_XENV;


/* Reset to default packing */

#pragma pack()

#endif /* __ACTBL3_H__ */
