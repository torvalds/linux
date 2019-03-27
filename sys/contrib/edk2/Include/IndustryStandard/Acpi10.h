/** @file   
  ACPI 1.0b definitions from the ACPI Specification, revision 1.0b

Copyright (c) 2006 - 2011, Intel Corporation. All rights reserved.<BR>
This program and the accompanying materials are licensed and made available under 
the terms and conditions of the BSD License that accompanies this distribution.  
The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php.                                          
    
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.             
**/

#ifndef _ACPI_1_0_H_
#define _ACPI_1_0_H_

#include <IndustryStandard/AcpiAml.h>

///
/// Common table header, this prefaces all ACPI tables, including FACS, but
/// excluding the RSD PTR structure.
///
typedef struct {
  UINT32  Signature;
  UINT32  Length;
} EFI_ACPI_COMMON_HEADER;

#pragma pack(1)
///
/// The common ACPI description table header.  This structure prefaces most ACPI tables.
///
typedef struct {
  UINT32  Signature;
  UINT32  Length;
  UINT8   Revision;
  UINT8   Checksum;
  UINT8   OemId[6];
  UINT64  OemTableId;
  UINT32  OemRevision;
  UINT32  CreatorId;
  UINT32  CreatorRevision;
} EFI_ACPI_DESCRIPTION_HEADER;
#pragma pack()

//
// Define for Desriptor
//
#define ACPI_SMALL_ITEM_FLAG                   0x00
#define ACPI_LARGE_ITEM_FLAG                   0x01

//
// Small Item Descriptor Name
//
#define ACPI_SMALL_IRQ_DESCRIPTOR_NAME                       0x04
#define ACPI_SMALL_DMA_DESCRIPTOR_NAME                       0x05
#define ACPI_SMALL_START_DEPENDENT_DESCRIPTOR_NAME           0x06
#define ACPI_SMALL_END_DEPENDENT_DESCRIPTOR_NAME             0x07
#define ACPI_SMALL_IO_PORT_DESCRIPTOR_NAME                   0x08
#define ACPI_SMALL_FIXED_IO_PORT_DESCRIPTOR_NAME             0x09
#define ACPI_SMALL_VENDOR_DEFINED_DESCRIPTOR_NAME            0x0E
#define ACPI_SMALL_END_TAG_DESCRIPTOR_NAME                   0x0F

//
// Large Item Descriptor Name
//
#define ACPI_LARGE_24_BIT_MEMORY_RANGE_DESCRIPTOR_NAME       0x01
#define ACPI_LARGE_VENDOR_DEFINED_DESCRIPTOR_NAME            0x04
#define ACPI_LARGE_32_BIT_MEMORY_RANGE_DESCRIPTOR_NAME       0x05
#define ACPI_LARGE_32_BIT_FIXED_MEMORY_RANGE_DESCRIPTOR_NAME 0x06
#define ACPI_LARGE_DWORD_ADDRESS_SPACE_DESCRIPTOR_NAME       0x07
#define ACPI_LARGE_WORD_ADDRESS_SPACE_DESCRIPTOR_NAME        0x08
#define ACPI_LARGE_EXTENDED_IRQ_DESCRIPTOR_NAME              0x09
#define ACPI_LARGE_QWORD_ADDRESS_SPACE_DESCRIPTOR_NAME       0x0A

//
// Small Item Descriptor Value
//
#define ACPI_IRQ_NOFLAG_DESCRIPTOR                0x22
#define ACPI_IRQ_DESCRIPTOR                       0x23
#define ACPI_DMA_DESCRIPTOR                       0x2A
#define ACPI_START_DEPENDENT_DESCRIPTOR           0x30
#define ACPI_START_DEPENDENT_EX_DESCRIPTOR        0x31
#define ACPI_END_DEPENDENT_DESCRIPTOR             0x38
#define ACPI_IO_PORT_DESCRIPTOR                   0x47
#define ACPI_FIXED_LOCATION_IO_PORT_DESCRIPTOR    0x4B
#define ACPI_END_TAG_DESCRIPTOR                   0x79

//
// Large Item Descriptor Value
//
#define ACPI_24_BIT_MEMORY_RANGE_DESCRIPTOR       0x81
#define ACPI_32_BIT_MEMORY_RANGE_DESCRIPTOR       0x85
#define ACPI_32_BIT_FIXED_MEMORY_RANGE_DESCRIPTOR 0x86
#define ACPI_DWORD_ADDRESS_SPACE_DESCRIPTOR       0x87
#define ACPI_WORD_ADDRESS_SPACE_DESCRIPTOR        0x88
#define ACPI_EXTENDED_INTERRUPT_DESCRIPTOR        0x89
#define ACPI_QWORD_ADDRESS_SPACE_DESCRIPTOR       0x8A
#define ACPI_ADDRESS_SPACE_DESCRIPTOR             0x8A

//
// Resource Type
//
#define ACPI_ADDRESS_SPACE_TYPE_MEM   0x00
#define ACPI_ADDRESS_SPACE_TYPE_IO    0x01
#define ACPI_ADDRESS_SPACE_TYPE_BUS   0x02

///
/// Power Management Timer frequency is fixed at 3.579545MHz.
///
#define ACPI_TIMER_FREQUENCY       3579545

//
// Ensure proper structure formats
//
#pragma pack(1)

///
/// The commond definition of QWORD, DWORD, and WORD
/// Address Space Descriptors.
///
typedef PACKED struct {
  UINT8   Desc;
  UINT16  Len;
  UINT8   ResType;
  UINT8   GenFlag;
  UINT8   SpecificFlag;
  UINT64  AddrSpaceGranularity;
  UINT64  AddrRangeMin;
  UINT64  AddrRangeMax;
  UINT64  AddrTranslationOffset;
  UINT64  AddrLen;
} EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR;

typedef PACKED union {
  UINT8     Byte;
  PACKED struct {
    UINT8 Length : 3;
    UINT8 Name : 4;
    UINT8 Type : 1;
  } Bits;
} ACPI_SMALL_RESOURCE_HEADER;

typedef PACKED struct {
  PACKED union {
    UINT8 Byte;
    PACKED struct {
      UINT8 Name : 7;
      UINT8 Type : 1;
    }Bits;
  } Header;
  UINT16 Length;
} ACPI_LARGE_RESOURCE_HEADER;

///
/// IRQ Descriptor.
///
typedef PACKED struct {
  ACPI_SMALL_RESOURCE_HEADER   Header;
  UINT16                       Mask;
} EFI_ACPI_IRQ_NOFLAG_DESCRIPTOR;

///
/// IRQ Descriptor.
///
typedef PACKED struct {
  ACPI_SMALL_RESOURCE_HEADER   Header;
  UINT16                       Mask;
  UINT8                        Information;
} EFI_ACPI_IRQ_DESCRIPTOR;

///
/// DMA Descriptor.
///
typedef PACKED struct {
  ACPI_SMALL_RESOURCE_HEADER   Header;
  UINT8                        ChannelMask;
  UINT8                        Information;
} EFI_ACPI_DMA_DESCRIPTOR;

///
/// I/O Port Descriptor
///
typedef PACKED struct {
  ACPI_SMALL_RESOURCE_HEADER   Header;
  UINT8                        Information;
  UINT16                       BaseAddressMin;
  UINT16                       BaseAddressMax;
  UINT8                        Alignment;
  UINT8                        Length;
} EFI_ACPI_IO_PORT_DESCRIPTOR;

///
/// Fixed Location I/O Port Descriptor.
///
typedef PACKED struct {
  ACPI_SMALL_RESOURCE_HEADER   Header;
  UINT16                       BaseAddress;
  UINT8                        Length;
} EFI_ACPI_FIXED_LOCATION_IO_PORT_DESCRIPTOR;

///
/// 24-Bit Memory Range Descriptor
///
typedef PACKED struct {
  ACPI_LARGE_RESOURCE_HEADER    Header;
  UINT8                         Information;
  UINT16                        BaseAddressMin;
  UINT16                        BaseAddressMax;
  UINT16                        Alignment;
  UINT16                        Length;
} EFI_ACPI_24_BIT_MEMORY_RANGE_DESCRIPTOR;

///
/// 32-Bit Memory Range Descriptor
///
typedef PACKED struct {
  ACPI_LARGE_RESOURCE_HEADER    Header;
  UINT8                         Information;
  UINT32                        BaseAddressMin;
  UINT32                        BaseAddressMax;
  UINT32                        Alignment;
  UINT32                        Length;
} EFI_ACPI_32_BIT_MEMORY_RANGE_DESCRIPTOR;

///
/// Fixed 32-Bit Fixed Memory Range Descriptor
///
typedef PACKED struct {
  ACPI_LARGE_RESOURCE_HEADER    Header;
  UINT8                         Information;
  UINT32                        BaseAddress;
  UINT32                        Length;
} EFI_ACPI_32_BIT_FIXED_MEMORY_RANGE_DESCRIPTOR;

///
/// QWORD Address Space Descriptor
///
typedef PACKED struct {
  ACPI_LARGE_RESOURCE_HEADER    Header;
  UINT8                         ResType;
  UINT8                         GenFlag;
  UINT8                         SpecificFlag;
  UINT64                        AddrSpaceGranularity;
  UINT64                        AddrRangeMin;
  UINT64                        AddrRangeMax;
  UINT64                        AddrTranslationOffset;
  UINT64                        AddrLen;
} EFI_ACPI_QWORD_ADDRESS_SPACE_DESCRIPTOR;

///
/// DWORD Address Space Descriptor
///
typedef PACKED struct {
  ACPI_LARGE_RESOURCE_HEADER    Header;
  UINT8                         ResType;
  UINT8                         GenFlag;
  UINT8                         SpecificFlag;
  UINT32                        AddrSpaceGranularity;
  UINT32                        AddrRangeMin;
  UINT32                        AddrRangeMax;
  UINT32                        AddrTranslationOffset;
  UINT32                        AddrLen;
} EFI_ACPI_DWORD_ADDRESS_SPACE_DESCRIPTOR;

///
/// WORD Address Space Descriptor
///
typedef PACKED struct {
  ACPI_LARGE_RESOURCE_HEADER    Header;
  UINT8                         ResType;
  UINT8                         GenFlag;
  UINT8                         SpecificFlag;
  UINT16                        AddrSpaceGranularity;
  UINT16                        AddrRangeMin;
  UINT16                        AddrRangeMax;
  UINT16                        AddrTranslationOffset;
  UINT16                        AddrLen;
} EFI_ACPI_WORD_ADDRESS_SPACE_DESCRIPTOR;

///
/// Extended Interrupt Descriptor
///
typedef PACKED struct {
  ACPI_LARGE_RESOURCE_HEADER    Header;
  UINT8                         InterruptVectorFlags;
  UINT8                         InterruptTableLength;
  UINT32                        InterruptNumber[1];
} EFI_ACPI_EXTENDED_INTERRUPT_DESCRIPTOR;

#pragma pack()

///
/// The End tag identifies an end of resource data.
///
typedef struct {
  UINT8 Desc;
  UINT8 Checksum;
} EFI_ACPI_END_TAG_DESCRIPTOR;

//
// General use definitions
//
#define EFI_ACPI_RESERVED_BYTE  0x00
#define EFI_ACPI_RESERVED_WORD  0x0000
#define EFI_ACPI_RESERVED_DWORD 0x00000000
#define EFI_ACPI_RESERVED_QWORD 0x0000000000000000

//
// Resource Type Specific Flags
// Ref ACPI specification 6.4.3.5.5
//
// Bit [0]    : Write Status, _RW
//
#define EFI_ACPI_MEMORY_RESOURCE_SPECIFIC_FLAG_READ_WRITE                (1 << 0)
#define EFI_ACPI_MEMORY_RESOURCE_SPECIFIC_FLAG_READ_ONLY                 (0 << 0)
//
// Bit [2:1]  : Memory Attributes, _MEM
//
#define EFI_ACPI_MEMORY_RESOURCE_SPECIFIC_FLAG_NON_CACHEABLE             (0 << 1)
#define EFI_ACPI_MEMORY_RESOURCE_SPECIFIC_FLAG_CACHEABLE                 (1 << 1)
#define EFI_ACPI_MEMORY_RESOURCE_SPECIFIC_FLAG_CACHEABLE_WRITE_COMBINING (2 << 1)
#define EFI_ACPI_MEMORY_RESOURCE_SPECIFIC_FLAG_CACHEABLE_PREFETCHABLE    (3 << 1)
//
// Bit [4:3]  : Memory Attributes, _MTP
//
#define EFI_ACPI_MEMORY_RESOURCE_SPECIFIC_FLAG_ADDRESS_RANGE_MEMORY      (0 << 3)
#define EFI_ACPI_MEMORY_RESOURCE_SPECIFIC_FLAG_ADDRESS_RANGE_RESERVED    (1 << 3)
#define EFI_ACPI_MEMORY_RESOURCE_SPECIFIC_FLAG_ADDRESS_RANGE_ACPI        (2 << 3)
#define EFI_APCI_MEMORY_RESOURCE_SPECIFIC_FLAG_ADDRESS_RANGE_NVS         (3 << 3)
//
// Bit [5]    : Memory to I/O Translation, _TTP
//
#define EFI_ACPI_MEMORY_RESOURCE_SPECIFIC_FLAG_TYPE_TRANSLATION          (1 << 5)
#define EFI_ACPI_MEMORY_RESOURCE_SPECIFIC_FLAG_TYPE_STATIC               (0 << 5)

//
// IRQ Information
// Ref ACPI specification 6.4.2.1
//
#define EFI_ACPI_IRQ_SHARABLE_MASK                      0x10
#define   EFI_ACPI_IRQ_SHARABLE                         0x10

#define EFI_ACPI_IRQ_POLARITY_MASK                      0x08
#define   EFI_ACPI_IRQ_HIGH_TRUE                        0x00
#define   EFI_ACPI_IRQ_LOW_FALSE                        0x08

#define EFI_ACPI_IRQ_MODE                               0x01
#define   EFI_ACPI_IRQ_LEVEL_TRIGGERED                  0x00
#define   EFI_ACPI_IRQ_EDGE_TRIGGERED                   0x01

//
// DMA Information
// Ref ACPI specification 6.4.2.2
//
#define EFI_ACPI_DMA_SPEED_TYPE_MASK                    0x60
#define   EFI_ACPI_DMA_SPEED_TYPE_COMPATIBILITY         0x00
#define   EFI_ACPI_DMA_SPEED_TYPE_A                     0x20
#define   EFI_ACPI_DMA_SPEED_TYPE_B                     0x40
#define   EFI_ACPI_DMA_SPEED_TYPE_F                     0x60
                                               
#define EFI_ACPI_DMA_BUS_MASTER_MASK                    0x04
#define   EFI_ACPI_DMA_BUS_MASTER                       0x04

#define EFI_ACPI_DMA_TRANSFER_TYPE_MASK                 0x03
#define   EFI_ACPI_DMA_TRANSFER_TYPE_8_BIT              0x00
#define   EFI_ACPI_DMA_TRANSFER_TYPE_8_BIT_AND_16_BIT   0x01
#define   EFI_ACPI_DMA_TRANSFER_TYPE_16_BIT             0x10

//
// IO Information
// Ref ACPI specification 6.4.2.5
//
#define EFI_ACPI_IO_DECODE_MASK                         0x01
#define   EFI_ACPI_IO_DECODE_16_BIT                     0x01
#define   EFI_ACPI_IO_DECODE_10_BIT                     0x00

//
// Memory Information
// Ref ACPI specification 6.4.3.4
//
#define EFI_ACPI_MEMORY_WRITE_STATUS_MASK               0x01
#define   EFI_ACPI_MEMORY_WRITABLE                      0x01
#define   EFI_ACPI_MEMORY_NON_WRITABLE                  0x00

//
// Ensure proper structure formats
//
#pragma pack(1)
//
// ACPI 1.0b table structures
//

///
/// Root System Description Pointer Structure.
///
typedef struct {
  UINT64  Signature;
  UINT8   Checksum;
  UINT8   OemId[6];
  UINT8   Reserved;
  UINT32  RsdtAddress;
} EFI_ACPI_1_0_ROOT_SYSTEM_DESCRIPTION_POINTER;

//
// Root System Description Table
// No definition needed as it is a common description table header, the same with 
// EFI_ACPI_DESCRIPTION_HEADER, followed by a variable number of UINT32 table pointers.
//

///
/// RSDT Revision (as defined in ACPI 1.0b specification).
///
#define EFI_ACPI_1_0_ROOT_SYSTEM_DESCRIPTION_TABLE_REVISION 0x01

///
/// Fixed ACPI Description Table Structure (FADT).
///
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER Header;
  UINT32                      FirmwareCtrl;
  UINT32                      Dsdt;
  UINT8                       IntModel;
  UINT8                       Reserved1;
  UINT16                      SciInt;
  UINT32                      SmiCmd;
  UINT8                       AcpiEnable;
  UINT8                       AcpiDisable;
  UINT8                       S4BiosReq;
  UINT8                       Reserved2;
  UINT32                      Pm1aEvtBlk;
  UINT32                      Pm1bEvtBlk;
  UINT32                      Pm1aCntBlk;
  UINT32                      Pm1bCntBlk;
  UINT32                      Pm2CntBlk;
  UINT32                      PmTmrBlk;
  UINT32                      Gpe0Blk;
  UINT32                      Gpe1Blk;
  UINT8                       Pm1EvtLen;
  UINT8                       Pm1CntLen;
  UINT8                       Pm2CntLen;
  UINT8                       PmTmLen;
  UINT8                       Gpe0BlkLen;
  UINT8                       Gpe1BlkLen;
  UINT8                       Gpe1Base;
  UINT8                       Reserved3;
  UINT16                      PLvl2Lat;
  UINT16                      PLvl3Lat;
  UINT16                      FlushSize;
  UINT16                      FlushStride;
  UINT8                       DutyOffset;
  UINT8                       DutyWidth;
  UINT8                       DayAlrm;
  UINT8                       MonAlrm;
  UINT8                       Century;
  UINT8                       Reserved4;
  UINT8                       Reserved5;
  UINT8                       Reserved6;
  UINT32                      Flags;
} EFI_ACPI_1_0_FIXED_ACPI_DESCRIPTION_TABLE;

///
/// FADT Version (as defined in ACPI 1.0b specification).
///
#define EFI_ACPI_1_0_FIXED_ACPI_DESCRIPTION_TABLE_REVISION  0x01

#define EFI_ACPI_1_0_INT_MODE_DUAL_PIC         0
#define EFI_ACPI_1_0_INT_MODE_MULTIPLE_APIC    1

//
// Fixed ACPI Description Table Fixed Feature Flags
// All other bits are reserved and must be set to 0.
//
#define EFI_ACPI_1_0_WBINVD               BIT0
#define EFI_ACPI_1_0_WBINVD_FLUSH         BIT1
#define EFI_ACPI_1_0_PROC_C1              BIT2
#define EFI_ACPI_1_0_P_LVL2_UP            BIT3
#define EFI_ACPI_1_0_PWR_BUTTON           BIT4
#define EFI_ACPI_1_0_SLP_BUTTON           BIT5
#define EFI_ACPI_1_0_FIX_RTC              BIT6
#define EFI_ACPI_1_0_RTC_S4               BIT7
#define EFI_ACPI_1_0_TMR_VAL_EXT          BIT8
#define EFI_ACPI_1_0_DCK_CAP              BIT9

///
/// Firmware ACPI Control Structure.
///
typedef struct {
  UINT32  Signature;
  UINT32  Length;
  UINT32  HardwareSignature;
  UINT32  FirmwareWakingVector;
  UINT32  GlobalLock;
  UINT32  Flags;
  UINT8   Reserved[40];
} EFI_ACPI_1_0_FIRMWARE_ACPI_CONTROL_STRUCTURE;

///
/// Firmware Control Structure Feature Flags.
/// All other bits are reserved and must be set to 0.
///
#define EFI_ACPI_1_0_S4BIOS_F             BIT0

///
/// Multiple APIC Description Table header definition.  The rest of the table
/// must be defined in a platform-specific manner.
///
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER Header;
  UINT32                      LocalApicAddress;
  UINT32                      Flags;
} EFI_ACPI_1_0_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER;

///
/// MADT Revision (as defined in ACPI 1.0b specification).
///
#define EFI_ACPI_1_0_MULTIPLE_APIC_DESCRIPTION_TABLE_REVISION 0x01

///
/// Multiple APIC Flags
/// All other bits are reserved and must be set to 0.
///
#define EFI_ACPI_1_0_PCAT_COMPAT           BIT0

//
// Multiple APIC Description Table APIC structure types
// All other values between 0x05 an 0xFF are reserved and
// will be ignored by OSPM.
//
#define EFI_ACPI_1_0_PROCESSOR_LOCAL_APIC           0x00
#define EFI_ACPI_1_0_IO_APIC                        0x01
#define EFI_ACPI_1_0_INTERRUPT_SOURCE_OVERRIDE      0x02
#define EFI_ACPI_1_0_NON_MASKABLE_INTERRUPT_SOURCE  0x03
#define EFI_ACPI_1_0_LOCAL_APIC_NMI                 0x04

//
// APIC Structure Definitions
//

///
/// Processor Local APIC Structure Definition.
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT8   AcpiProcessorId;
  UINT8   ApicId;
  UINT32  Flags;
} EFI_ACPI_1_0_PROCESSOR_LOCAL_APIC_STRUCTURE;

///
/// Local APIC Flags.  All other bits are reserved and must be 0.
///
#define EFI_ACPI_1_0_LOCAL_APIC_ENABLED      BIT0

///
/// IO APIC Structure.
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT8   IoApicId;
  UINT8   Reserved;
  UINT32  IoApicAddress;
  UINT32  SystemVectorBase;
} EFI_ACPI_1_0_IO_APIC_STRUCTURE;

///
/// Interrupt Source Override Structure.
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT8   Bus;
  UINT8   Source;
  UINT32  GlobalSystemInterruptVector;
  UINT16  Flags;
} EFI_ACPI_1_0_INTERRUPT_SOURCE_OVERRIDE_STRUCTURE;

///
/// Non-Maskable Interrupt Source Structure.
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT16  Flags;
  UINT32  GlobalSystemInterruptVector;
} EFI_ACPI_1_0_NON_MASKABLE_INTERRUPT_SOURCE_STRUCTURE;

///
/// Local APIC NMI Structure.
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT8   AcpiProcessorId;
  UINT16  Flags;
  UINT8   LocalApicInti;
} EFI_ACPI_1_0_LOCAL_APIC_NMI_STRUCTURE;

///
/// Smart Battery Description Table (SBST)
///
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER Header;
  UINT32                      WarningEnergyLevel;
  UINT32                      LowEnergyLevel;
  UINT32                      CriticalEnergyLevel;
} EFI_ACPI_1_0_SMART_BATTERY_DESCRIPTION_TABLE;

//
// Known table signatures
//

///
/// "RSD PTR " Root System Description Pointer.
///
#define EFI_ACPI_1_0_ROOT_SYSTEM_DESCRIPTION_POINTER_SIGNATURE  SIGNATURE_64('R', 'S', 'D', ' ', 'P', 'T', 'R', ' ')

///
/// "APIC" Multiple APIC Description Table.
///
#define EFI_ACPI_1_0_APIC_SIGNATURE  SIGNATURE_32('A', 'P', 'I', 'C')

///
/// "DSDT" Differentiated System Description Table.
///
#define EFI_ACPI_1_0_DIFFERENTIATED_SYSTEM_DESCRIPTION_TABLE_SIGNATURE  SIGNATURE_32('D', 'S', 'D', 'T')

///
/// "FACS" Firmware ACPI Control Structure.
///
#define EFI_ACPI_1_0_FIRMWARE_ACPI_CONTROL_STRUCTURE_SIGNATURE  SIGNATURE_32('F', 'A', 'C', 'S')

///
/// "FACP" Fixed ACPI Description Table.
///
#define EFI_ACPI_1_0_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE  SIGNATURE_32('F', 'A', 'C', 'P')

///
/// "PSDT" Persistent System Description Table.
///
#define EFI_ACPI_1_0_PERSISTENT_SYSTEM_DESCRIPTION_TABLE_SIGNATURE  SIGNATURE_32('P', 'S', 'D', 'T')

///
/// "RSDT" Root System Description Table.
///
#define EFI_ACPI_1_0_ROOT_SYSTEM_DESCRIPTION_TABLE_SIGNATURE  SIGNATURE_32('R', 'S', 'D', 'T')

///
/// "SBST" Smart Battery Specification Table.
///
#define EFI_ACPI_1_0_SMART_BATTERY_SPECIFICATION_TABLE_SIGNATURE  SIGNATURE_32('S', 'B', 'S', 'T')

///
/// "SSDT" Secondary System Description Table.
///
#define EFI_ACPI_1_0_SECONDARY_SYSTEM_DESCRIPTION_TABLE_SIGNATURE  SIGNATURE_32('S', 'S', 'D', 'T')

#pragma pack()

#endif
