/** @file   
  ACPI 6.0 definitions from the ACPI Specification Revision 6.0 Errata A January, 2016.

  Copyright (c) 2015 - 2017, Intel Corporation. All rights reserved.<BR>
  (C) Copyright 2015-2016 Hewlett Packard Enterprise Development LP<BR>
  This program and the accompanying materials                          
  are licensed and made available under the terms and conditions of the BSD License         
  which accompanies this distribution.  The full text of the license may be found at        
  http://opensource.org/licenses/bsd-license.php                                            

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.             
**/

#ifndef _ACPI_6_0_H_
#define _ACPI_6_0_H_

#include <IndustryStandard/Acpi51.h>

//
// Ensure proper structure formats
//
#pragma pack(1)

///
/// ACPI 6.0 Generic Address Space definition
///
typedef struct {
  UINT8   AddressSpaceId;
  UINT8   RegisterBitWidth;
  UINT8   RegisterBitOffset;
  UINT8   AccessSize;
  UINT64  Address;
} EFI_ACPI_6_0_GENERIC_ADDRESS_STRUCTURE;

//
// Generic Address Space Address IDs
//
#define EFI_ACPI_6_0_SYSTEM_MEMORY              0
#define EFI_ACPI_6_0_SYSTEM_IO                  1
#define EFI_ACPI_6_0_PCI_CONFIGURATION_SPACE    2
#define EFI_ACPI_6_0_EMBEDDED_CONTROLLER        3
#define EFI_ACPI_6_0_SMBUS                      4
#define EFI_ACPI_6_0_PLATFORM_COMMUNICATION_CHANNEL  0x0A
#define EFI_ACPI_6_0_FUNCTIONAL_FIXED_HARDWARE       0x7F

//
// Generic Address Space Access Sizes
//
#define EFI_ACPI_6_0_UNDEFINED  0
#define EFI_ACPI_6_0_BYTE       1
#define EFI_ACPI_6_0_WORD       2
#define EFI_ACPI_6_0_DWORD      3
#define EFI_ACPI_6_0_QWORD      4

//
// ACPI 6.0 table structures
//

///
/// Root System Description Pointer Structure
///
typedef struct {
  UINT64  Signature;
  UINT8   Checksum;
  UINT8   OemId[6];
  UINT8   Revision;
  UINT32  RsdtAddress;
  UINT32  Length;
  UINT64  XsdtAddress;
  UINT8   ExtendedChecksum;
  UINT8   Reserved[3];
} EFI_ACPI_6_0_ROOT_SYSTEM_DESCRIPTION_POINTER;

///
/// RSD_PTR Revision (as defined in ACPI 6.0 spec.)
///
#define EFI_ACPI_6_0_ROOT_SYSTEM_DESCRIPTION_POINTER_REVISION 0x02  ///< ACPISpec (Revision 6.0) says current value is 2

///
/// Common table header, this prefaces all ACPI tables, including FACS, but
/// excluding the RSD PTR structure
///
typedef struct {
  UINT32  Signature;
  UINT32  Length;
} EFI_ACPI_6_0_COMMON_HEADER;

//
// Root System Description Table
// No definition needed as it is a common description table header, the same with 
// EFI_ACPI_DESCRIPTION_HEADER, followed by a variable number of UINT32 table pointers.
//

///
/// RSDT Revision (as defined in ACPI 6.0 spec.)
///
#define EFI_ACPI_6_0_ROOT_SYSTEM_DESCRIPTION_TABLE_REVISION 0x01

//
// Extended System Description Table
// No definition needed as it is a common description table header, the same with 
// EFI_ACPI_DESCRIPTION_HEADER, followed by a variable number of UINT64 table pointers.
//

///
/// XSDT Revision (as defined in ACPI 6.0 spec.)
///
#define EFI_ACPI_6_0_EXTENDED_SYSTEM_DESCRIPTION_TABLE_REVISION 0x01

///
/// Fixed ACPI Description Table Structure (FADT)
///
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER             Header;
  UINT32                                  FirmwareCtrl;
  UINT32                                  Dsdt;
  UINT8                                   Reserved0;
  UINT8                                   PreferredPmProfile;
  UINT16                                  SciInt;
  UINT32                                  SmiCmd;
  UINT8                                   AcpiEnable;
  UINT8                                   AcpiDisable;
  UINT8                                   S4BiosReq;
  UINT8                                   PstateCnt;
  UINT32                                  Pm1aEvtBlk;
  UINT32                                  Pm1bEvtBlk;
  UINT32                                  Pm1aCntBlk;
  UINT32                                  Pm1bCntBlk;
  UINT32                                  Pm2CntBlk;
  UINT32                                  PmTmrBlk;
  UINT32                                  Gpe0Blk;
  UINT32                                  Gpe1Blk;
  UINT8                                   Pm1EvtLen;
  UINT8                                   Pm1CntLen;
  UINT8                                   Pm2CntLen;
  UINT8                                   PmTmrLen;
  UINT8                                   Gpe0BlkLen;
  UINT8                                   Gpe1BlkLen;
  UINT8                                   Gpe1Base;
  UINT8                                   CstCnt;
  UINT16                                  PLvl2Lat;
  UINT16                                  PLvl3Lat;
  UINT16                                  FlushSize;
  UINT16                                  FlushStride;
  UINT8                                   DutyOffset;
  UINT8                                   DutyWidth;
  UINT8                                   DayAlrm;
  UINT8                                   MonAlrm;
  UINT8                                   Century;
  UINT16                                  IaPcBootArch;
  UINT8                                   Reserved1;
  UINT32                                  Flags;
  EFI_ACPI_6_0_GENERIC_ADDRESS_STRUCTURE  ResetReg;
  UINT8                                   ResetValue;
  UINT16                                  ArmBootArch;
  UINT8                                   MinorVersion;
  UINT64                                  XFirmwareCtrl;
  UINT64                                  XDsdt;
  EFI_ACPI_6_0_GENERIC_ADDRESS_STRUCTURE  XPm1aEvtBlk;
  EFI_ACPI_6_0_GENERIC_ADDRESS_STRUCTURE  XPm1bEvtBlk;
  EFI_ACPI_6_0_GENERIC_ADDRESS_STRUCTURE  XPm1aCntBlk;
  EFI_ACPI_6_0_GENERIC_ADDRESS_STRUCTURE  XPm1bCntBlk;
  EFI_ACPI_6_0_GENERIC_ADDRESS_STRUCTURE  XPm2CntBlk;
  EFI_ACPI_6_0_GENERIC_ADDRESS_STRUCTURE  XPmTmrBlk;
  EFI_ACPI_6_0_GENERIC_ADDRESS_STRUCTURE  XGpe0Blk;
  EFI_ACPI_6_0_GENERIC_ADDRESS_STRUCTURE  XGpe1Blk;
  EFI_ACPI_6_0_GENERIC_ADDRESS_STRUCTURE  SleepControlReg;
  EFI_ACPI_6_0_GENERIC_ADDRESS_STRUCTURE  SleepStatusReg;
  UINT64                                  HypervisorVendorIdentity;
} EFI_ACPI_6_0_FIXED_ACPI_DESCRIPTION_TABLE;

///
/// FADT Version (as defined in ACPI 6.0 spec.)
///
#define EFI_ACPI_6_0_FIXED_ACPI_DESCRIPTION_TABLE_REVISION  0x06
#define EFI_ACPI_6_0_FIXED_ACPI_DESCRIPTION_TABLE_MINOR_REVISION  0x00

//
// Fixed ACPI Description Table Preferred Power Management Profile
//
#define EFI_ACPI_6_0_PM_PROFILE_UNSPECIFIED         0
#define EFI_ACPI_6_0_PM_PROFILE_DESKTOP             1
#define EFI_ACPI_6_0_PM_PROFILE_MOBILE              2
#define EFI_ACPI_6_0_PM_PROFILE_WORKSTATION         3
#define EFI_ACPI_6_0_PM_PROFILE_ENTERPRISE_SERVER   4
#define EFI_ACPI_6_0_PM_PROFILE_SOHO_SERVER         5
#define EFI_ACPI_6_0_PM_PROFILE_APPLIANCE_PC        6
#define EFI_ACPI_6_0_PM_PROFILE_PERFORMANCE_SERVER  7
#define EFI_ACPI_6_0_PM_PROFILE_TABLET              8

//
// Fixed ACPI Description Table Boot Architecture Flags
// All other bits are reserved and must be set to 0.
//
#define EFI_ACPI_6_0_LEGACY_DEVICES              BIT0
#define EFI_ACPI_6_0_8042                        BIT1
#define EFI_ACPI_6_0_VGA_NOT_PRESENT             BIT2
#define EFI_ACPI_6_0_MSI_NOT_SUPPORTED           BIT3
#define EFI_ACPI_6_0_PCIE_ASPM_CONTROLS          BIT4
#define EFI_ACPI_6_0_CMOS_RTC_NOT_PRESENT        BIT5

//
// Fixed ACPI Description Table Arm Boot Architecture Flags
// All other bits are reserved and must be set to 0.
//
#define EFI_ACPI_6_0_ARM_PSCI_COMPLIANT              BIT0
#define EFI_ACPI_6_0_ARM_PSCI_USE_HVC                BIT1

//
// Fixed ACPI Description Table Fixed Feature Flags
// All other bits are reserved and must be set to 0.
//
#define EFI_ACPI_6_0_WBINVD                                 BIT0
#define EFI_ACPI_6_0_WBINVD_FLUSH                           BIT1
#define EFI_ACPI_6_0_PROC_C1                                BIT2
#define EFI_ACPI_6_0_P_LVL2_UP                              BIT3
#define EFI_ACPI_6_0_PWR_BUTTON                             BIT4
#define EFI_ACPI_6_0_SLP_BUTTON                             BIT5
#define EFI_ACPI_6_0_FIX_RTC                                BIT6
#define EFI_ACPI_6_0_RTC_S4                                 BIT7
#define EFI_ACPI_6_0_TMR_VAL_EXT                            BIT8
#define EFI_ACPI_6_0_DCK_CAP                                BIT9
#define EFI_ACPI_6_0_RESET_REG_SUP                          BIT10
#define EFI_ACPI_6_0_SEALED_CASE                            BIT11
#define EFI_ACPI_6_0_HEADLESS                               BIT12
#define EFI_ACPI_6_0_CPU_SW_SLP                             BIT13
#define EFI_ACPI_6_0_PCI_EXP_WAK                            BIT14
#define EFI_ACPI_6_0_USE_PLATFORM_CLOCK                     BIT15
#define EFI_ACPI_6_0_S4_RTC_STS_VALID                       BIT16
#define EFI_ACPI_6_0_REMOTE_POWER_ON_CAPABLE                BIT17
#define EFI_ACPI_6_0_FORCE_APIC_CLUSTER_MODEL               BIT18
#define EFI_ACPI_6_0_FORCE_APIC_PHYSICAL_DESTINATION_MODE   BIT19
#define EFI_ACPI_6_0_HW_REDUCED_ACPI                        BIT20
#define EFI_ACPI_6_0_LOW_POWER_S0_IDLE_CAPABLE              BIT21

///
/// Firmware ACPI Control Structure
///
typedef struct {
  UINT32  Signature;
  UINT32  Length;
  UINT32  HardwareSignature;
  UINT32  FirmwareWakingVector;
  UINT32  GlobalLock;
  UINT32  Flags;
  UINT64  XFirmwareWakingVector;
  UINT8   Version;
  UINT8   Reserved0[3];
  UINT32  OspmFlags;
  UINT8   Reserved1[24];
} EFI_ACPI_6_0_FIRMWARE_ACPI_CONTROL_STRUCTURE;

///
/// FACS Version (as defined in ACPI 6.0 spec.)
///
#define EFI_ACPI_6_0_FIRMWARE_ACPI_CONTROL_STRUCTURE_VERSION  0x02

///
/// Firmware Control Structure Feature Flags
/// All other bits are reserved and must be set to 0.
///
#define EFI_ACPI_6_0_S4BIOS_F                     BIT0
#define EFI_ACPI_6_0_64BIT_WAKE_SUPPORTED_F       BIT1

///
/// OSPM Enabled Firmware Control Structure Flags
/// All other bits are reserved and must be set to 0.
///
#define EFI_ACPI_6_0_OSPM_64BIT_WAKE_F            BIT0

//
// Differentiated System Description Table,
// Secondary System Description Table
// and Persistent System Description Table,
// no definition needed as they are common description table header, the same with
// EFI_ACPI_DESCRIPTION_HEADER, followed by a definition block.
//
#define EFI_ACPI_6_0_DIFFERENTIATED_SYSTEM_DESCRIPTION_TABLE_REVISION   0x02
#define EFI_ACPI_6_0_SECONDARY_SYSTEM_DESCRIPTION_TABLE_REVISION        0x02

///
/// Multiple APIC Description Table header definition.  The rest of the table
/// must be defined in a platform specific manner.
///
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER Header;
  UINT32                      LocalApicAddress;
  UINT32                      Flags;
} EFI_ACPI_6_0_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER;

///
/// MADT Revision (as defined in ACPI 6.0 Errata A spec.)
///
#define EFI_ACPI_6_0_MULTIPLE_APIC_DESCRIPTION_TABLE_REVISION 0x04

///
/// Multiple APIC Flags
/// All other bits are reserved and must be set to 0.
///
#define EFI_ACPI_6_0_PCAT_COMPAT         BIT0

//
// Multiple APIC Description Table APIC structure types
// All other values between 0x0D and 0x7F are reserved and
// will be ignored by OSPM. 0x80 ~ 0xFF are reserved for OEM.
//
#define EFI_ACPI_6_0_PROCESSOR_LOCAL_APIC           0x00
#define EFI_ACPI_6_0_IO_APIC                        0x01
#define EFI_ACPI_6_0_INTERRUPT_SOURCE_OVERRIDE      0x02
#define EFI_ACPI_6_0_NON_MASKABLE_INTERRUPT_SOURCE  0x03
#define EFI_ACPI_6_0_LOCAL_APIC_NMI                 0x04
#define EFI_ACPI_6_0_LOCAL_APIC_ADDRESS_OVERRIDE    0x05
#define EFI_ACPI_6_0_IO_SAPIC                       0x06
#define EFI_ACPI_6_0_LOCAL_SAPIC                    0x07
#define EFI_ACPI_6_0_PLATFORM_INTERRUPT_SOURCES     0x08
#define EFI_ACPI_6_0_PROCESSOR_LOCAL_X2APIC         0x09
#define EFI_ACPI_6_0_LOCAL_X2APIC_NMI               0x0A
#define EFI_ACPI_6_0_GIC                            0x0B
#define EFI_ACPI_6_0_GICD                           0x0C
#define EFI_ACPI_6_0_GIC_MSI_FRAME                  0x0D
#define EFI_ACPI_6_0_GICR                           0x0E
#define EFI_ACPI_6_0_GIC_ITS                        0x0F

//
// APIC Structure Definitions
//

///
/// Processor Local APIC Structure Definition
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT8   AcpiProcessorUid;
  UINT8   ApicId;
  UINT32  Flags;
} EFI_ACPI_6_0_PROCESSOR_LOCAL_APIC_STRUCTURE;

///
/// Local APIC Flags.  All other bits are reserved and must be 0.
///
#define EFI_ACPI_6_0_LOCAL_APIC_ENABLED        BIT0

///
/// IO APIC Structure
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT8   IoApicId;
  UINT8   Reserved;
  UINT32  IoApicAddress;
  UINT32  GlobalSystemInterruptBase;
} EFI_ACPI_6_0_IO_APIC_STRUCTURE;

///
/// Interrupt Source Override Structure
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT8   Bus;
  UINT8   Source;
  UINT32  GlobalSystemInterrupt;
  UINT16  Flags;
} EFI_ACPI_6_0_INTERRUPT_SOURCE_OVERRIDE_STRUCTURE;

///
/// Platform Interrupt Sources Structure Definition
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT16  Flags;
  UINT8   InterruptType;
  UINT8   ProcessorId;
  UINT8   ProcessorEid;
  UINT8   IoSapicVector;
  UINT32  GlobalSystemInterrupt;
  UINT32  PlatformInterruptSourceFlags;
  UINT8   CpeiProcessorOverride;
  UINT8   Reserved[31];
} EFI_ACPI_6_0_PLATFORM_INTERRUPT_APIC_STRUCTURE;

//
// MPS INTI flags.
// All other bits are reserved and must be set to 0.
//
#define EFI_ACPI_6_0_POLARITY      (3 << 0)
#define EFI_ACPI_6_0_TRIGGER_MODE  (3 << 2)

///
/// Non-Maskable Interrupt Source Structure
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT16  Flags;
  UINT32  GlobalSystemInterrupt;
} EFI_ACPI_6_0_NON_MASKABLE_INTERRUPT_SOURCE_STRUCTURE;

///
/// Local APIC NMI Structure
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT8   AcpiProcessorUid;
  UINT16  Flags;
  UINT8   LocalApicLint;
} EFI_ACPI_6_0_LOCAL_APIC_NMI_STRUCTURE;

///
/// Local APIC Address Override Structure
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT16  Reserved;
  UINT64  LocalApicAddress;
} EFI_ACPI_6_0_LOCAL_APIC_ADDRESS_OVERRIDE_STRUCTURE;

///
/// IO SAPIC Structure
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT8   IoApicId;
  UINT8   Reserved;
  UINT32  GlobalSystemInterruptBase;
  UINT64  IoSapicAddress;
} EFI_ACPI_6_0_IO_SAPIC_STRUCTURE;

///
/// Local SAPIC Structure
/// This struct followed by a null-terminated ASCII string - ACPI Processor UID String
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT8   AcpiProcessorId;
  UINT8   LocalSapicId;
  UINT8   LocalSapicEid;
  UINT8   Reserved[3];
  UINT32  Flags;
  UINT32  ACPIProcessorUIDValue;
} EFI_ACPI_6_0_PROCESSOR_LOCAL_SAPIC_STRUCTURE;

///
/// Platform Interrupt Sources Structure
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT16  Flags;
  UINT8   InterruptType;
  UINT8   ProcessorId;
  UINT8   ProcessorEid;
  UINT8   IoSapicVector;
  UINT32  GlobalSystemInterrupt;
  UINT32  PlatformInterruptSourceFlags;
} EFI_ACPI_6_0_PLATFORM_INTERRUPT_SOURCES_STRUCTURE;

///
/// Platform Interrupt Source Flags.
/// All other bits are reserved and must be set to 0.
///
#define EFI_ACPI_6_0_CPEI_PROCESSOR_OVERRIDE          BIT0

///
/// Processor Local x2APIC Structure Definition
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT8   Reserved[2];
  UINT32  X2ApicId;
  UINT32  Flags;
  UINT32  AcpiProcessorUid;
} EFI_ACPI_6_0_PROCESSOR_LOCAL_X2APIC_STRUCTURE;

///
/// Local x2APIC NMI Structure
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT16  Flags;
  UINT32  AcpiProcessorUid;
  UINT8   LocalX2ApicLint;
  UINT8   Reserved[3];
} EFI_ACPI_6_0_LOCAL_X2APIC_NMI_STRUCTURE;

///
/// GIC Structure
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT16  Reserved;
  UINT32  CPUInterfaceNumber;
  UINT32  AcpiProcessorUid;
  UINT32  Flags;
  UINT32  ParkingProtocolVersion;
  UINT32  PerformanceInterruptGsiv;
  UINT64  ParkedAddress;
  UINT64  PhysicalBaseAddress;
  UINT64  GICV;
  UINT64  GICH;
  UINT32  VGICMaintenanceInterrupt;
  UINT64  GICRBaseAddress;
  UINT64  MPIDR;
  UINT8   ProcessorPowerEfficiencyClass;
  UINT8   Reserved2[3];
} EFI_ACPI_6_0_GIC_STRUCTURE;

///
/// GIC Flags.  All other bits are reserved and must be 0.
///
#define EFI_ACPI_6_0_GIC_ENABLED                              BIT0
#define EFI_ACPI_6_0_PERFORMANCE_INTERRUPT_MODEL              BIT1
#define EFI_ACPI_6_0_VGIC_MAINTENANCE_INTERRUPT_MODE_FLAGS    BIT2

///
/// GIC Distributor Structure
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT16  Reserved1;
  UINT32  GicId;
  UINT64  PhysicalBaseAddress;
  UINT32  SystemVectorBase;
  UINT8   GicVersion;
  UINT8   Reserved2[3];
} EFI_ACPI_6_0_GIC_DISTRIBUTOR_STRUCTURE;

///
/// GIC Version
///
#define EFI_ACPI_6_0_GIC_V1                                   0x01
#define EFI_ACPI_6_0_GIC_V2                                   0x02
#define EFI_ACPI_6_0_GIC_V3                                   0x03
#define EFI_ACPI_6_0_GIC_V4                                   0x04

///
/// GIC MSI Frame Structure
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT16  Reserved1;
  UINT32  GicMsiFrameId;
  UINT64  PhysicalBaseAddress;
  UINT32  Flags;
  UINT16  SPICount;
  UINT16  SPIBase;
} EFI_ACPI_6_0_GIC_MSI_FRAME_STRUCTURE;

///
/// GIC MSI Frame Flags.  All other bits are reserved and must be 0.
///
#define EFI_ACPI_6_0_SPI_COUNT_BASE_SELECT                    BIT0

///
/// GICR Structure
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT16  Reserved;
  UINT64  DiscoveryRangeBaseAddress;
  UINT32  DiscoveryRangeLength;
} EFI_ACPI_6_0_GICR_STRUCTURE;

///
/// GIC Interrupt Translation Service Structure
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT16  Reserved;
  UINT32  GicItsId;
  UINT64  PhysicalBaseAddress;
  UINT32  Reserved2;
} EFI_ACPI_6_0_GIC_ITS_STRUCTURE;

///
/// Smart Battery Description Table (SBST)
///
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER Header;
  UINT32                      WarningEnergyLevel;
  UINT32                      LowEnergyLevel;
  UINT32                      CriticalEnergyLevel;
} EFI_ACPI_6_0_SMART_BATTERY_DESCRIPTION_TABLE;

///
/// SBST Version (as defined in ACPI 6.0 spec.)
///
#define EFI_ACPI_6_0_SMART_BATTERY_DESCRIPTION_TABLE_REVISION 0x01

///
/// Embedded Controller Boot Resources Table (ECDT)
/// The table is followed by a null terminated ASCII string that contains
/// a fully qualified reference to the name space object.
///
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER             Header;
  EFI_ACPI_6_0_GENERIC_ADDRESS_STRUCTURE  EcControl;
  EFI_ACPI_6_0_GENERIC_ADDRESS_STRUCTURE  EcData;
  UINT32                                  Uid;
  UINT8                                   GpeBit;
} EFI_ACPI_6_0_EMBEDDED_CONTROLLER_BOOT_RESOURCES_TABLE;

///
/// ECDT Version (as defined in ACPI 6.0 spec.)
///
#define EFI_ACPI_6_0_EMBEDDED_CONTROLLER_BOOT_RESOURCES_TABLE_REVISION  0x01

///
/// System Resource Affinity Table (SRAT).  The rest of the table
/// must be defined in a platform specific manner.
///
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER Header;
  UINT32                      Reserved1;  ///< Must be set to 1
  UINT64                      Reserved2;
} EFI_ACPI_6_0_SYSTEM_RESOURCE_AFFINITY_TABLE_HEADER;

///
/// SRAT Version (as defined in ACPI 6.0 spec.)
///
#define EFI_ACPI_6_0_SYSTEM_RESOURCE_AFFINITY_TABLE_REVISION  0x03

//
// SRAT structure types.
// All other values between 0x04 an 0xFF are reserved and
// will be ignored by OSPM.
//
#define EFI_ACPI_6_0_PROCESSOR_LOCAL_APIC_SAPIC_AFFINITY  0x00
#define EFI_ACPI_6_0_MEMORY_AFFINITY                      0x01
#define EFI_ACPI_6_0_PROCESSOR_LOCAL_X2APIC_AFFINITY      0x02
#define EFI_ACPI_6_0_GICC_AFFINITY                        0x03

///
/// Processor Local APIC/SAPIC Affinity Structure Definition
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT8   ProximityDomain7To0;
  UINT8   ApicId;
  UINT32  Flags;
  UINT8   LocalSapicEid;
  UINT8   ProximityDomain31To8[3];
  UINT32  ClockDomain;
} EFI_ACPI_6_0_PROCESSOR_LOCAL_APIC_SAPIC_AFFINITY_STRUCTURE;

///
/// Local APIC/SAPIC Flags.  All other bits are reserved and must be 0.
///
#define EFI_ACPI_6_0_PROCESSOR_LOCAL_APIC_SAPIC_ENABLED (1 << 0)

///
/// Memory Affinity Structure Definition
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT32  ProximityDomain;
  UINT16  Reserved1;
  UINT32  AddressBaseLow;
  UINT32  AddressBaseHigh;
  UINT32  LengthLow;
  UINT32  LengthHigh;
  UINT32  Reserved2;
  UINT32  Flags;
  UINT64  Reserved3;
} EFI_ACPI_6_0_MEMORY_AFFINITY_STRUCTURE;

//
// Memory Flags.  All other bits are reserved and must be 0.
//
#define EFI_ACPI_6_0_MEMORY_ENABLED       (1 << 0)
#define EFI_ACPI_6_0_MEMORY_HOT_PLUGGABLE (1 << 1)
#define EFI_ACPI_6_0_MEMORY_NONVOLATILE   (1 << 2)

///
/// Processor Local x2APIC Affinity Structure Definition
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT8   Reserved1[2];
  UINT32  ProximityDomain;
  UINT32  X2ApicId;
  UINT32  Flags;
  UINT32  ClockDomain;
  UINT8   Reserved2[4];
} EFI_ACPI_6_0_PROCESSOR_LOCAL_X2APIC_AFFINITY_STRUCTURE;

///
/// GICC Affinity Structure Definition
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT32  ProximityDomain;
  UINT32  AcpiProcessorUid;
  UINT32  Flags;
  UINT32  ClockDomain;
} EFI_ACPI_6_0_GICC_AFFINITY_STRUCTURE;

///
/// GICC Flags.  All other bits are reserved and must be 0.
///
#define EFI_ACPI_6_0_GICC_ENABLED (1 << 0)

///
/// System Locality Distance Information Table (SLIT).
/// The rest of the table is a matrix.
///
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER Header;
  UINT64                      NumberOfSystemLocalities;
} EFI_ACPI_6_0_SYSTEM_LOCALITY_DISTANCE_INFORMATION_TABLE_HEADER;

///
/// SLIT Version (as defined in ACPI 6.0 spec.)
///
#define EFI_ACPI_6_0_SYSTEM_LOCALITY_DISTANCE_INFORMATION_TABLE_REVISION  0x01

///
/// Corrected Platform Error Polling Table (CPEP)
///
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER Header;
  UINT8                       Reserved[8];
} EFI_ACPI_6_0_CORRECTED_PLATFORM_ERROR_POLLING_TABLE_HEADER;

///
/// CPEP Version (as defined in ACPI 6.0 spec.)
///
#define EFI_ACPI_6_0_CORRECTED_PLATFORM_ERROR_POLLING_TABLE_REVISION 0x01

//
// CPEP processor structure types.
//
#define EFI_ACPI_6_0_CPEP_PROCESSOR_APIC_SAPIC  0x00

///
/// Corrected Platform Error Polling Processor Structure Definition
///
typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT8   ProcessorId;
  UINT8   ProcessorEid;
  UINT32  PollingInterval;
} EFI_ACPI_6_0_CPEP_PROCESSOR_APIC_SAPIC_STRUCTURE;

///
/// Maximum System Characteristics Table (MSCT)
///
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER Header;
  UINT32                      OffsetProxDomInfo;
  UINT32                      MaximumNumberOfProximityDomains;
  UINT32                      MaximumNumberOfClockDomains;
  UINT64                      MaximumPhysicalAddress;
} EFI_ACPI_6_0_MAXIMUM_SYSTEM_CHARACTERISTICS_TABLE_HEADER;

///
/// MSCT Version (as defined in ACPI 6.0 spec.)
///
#define EFI_ACPI_6_0_MAXIMUM_SYSTEM_CHARACTERISTICS_TABLE_REVISION 0x01

///
/// Maximum Proximity Domain Information Structure Definition
///
typedef struct {
  UINT8   Revision;
  UINT8   Length;
  UINT32  ProximityDomainRangeLow;
  UINT32  ProximityDomainRangeHigh;
  UINT32  MaximumProcessorCapacity;
  UINT64  MaximumMemoryCapacity;
} EFI_ACPI_6_0_MAXIMUM_PROXIMITY_DOMAIN_INFORMATION_STRUCTURE;

///
/// ACPI RAS Feature Table definition.
///
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER Header;
  UINT8                       PlatformCommunicationChannelIdentifier[12];
} EFI_ACPI_6_0_RAS_FEATURE_TABLE;

///
/// RASF Version (as defined in ACPI 6.0 spec.)
///
#define EFI_ACPI_6_0_RAS_FEATURE_TABLE_REVISION 0x01

///
/// ACPI RASF Platform Communication Channel Shared Memory Region definition.
///
typedef struct {
  UINT32                      Signature;
  UINT16                      Command;
  UINT16                      Status;
  UINT16                      Version;
  UINT8                       RASCapabilities[16];
  UINT8                       SetRASCapabilities[16];
  UINT16                      NumberOfRASFParameterBlocks;
  UINT32                      SetRASCapabilitiesStatus;
} EFI_ACPI_6_0_RASF_PLATFORM_COMMUNICATION_CHANNEL_SHARED_MEMORY_REGION;

///
/// ACPI RASF PCC command code
///
#define EFI_ACPI_6_0_RASF_PCC_COMMAND_CODE_EXECUTE_RASF_COMMAND  0x01

///
/// ACPI RASF Platform RAS Capabilities
///
#define EFI_ACPI_6_0_RASF_PLATFORM_RAS_CAPABILITY_HARDWARE_BASED_PATROL_SCRUB_SUPPOTED                          0x01
#define EFI_ACPI_6_0_RASF_PLATFORM_RAS_CAPABILITY_HARDWARE_BASED_PATROL_SCRUB_SUPPOTED_AND_EXPOSED_TO_SOFTWARE  0x02

///
/// ACPI RASF Parameter Block structure for PATROL_SCRUB
///
typedef struct {
  UINT16                      Type;
  UINT16                      Version;
  UINT16                      Length;
  UINT16                      PatrolScrubCommand;
  UINT64                      RequestedAddressRange[2];
  UINT64                      ActualAddressRange[2];
  UINT16                      Flags;
  UINT8                       RequestedSpeed;
} EFI_ACPI_6_0_RASF_PATROL_SCRUB_PLATFORM_BLOCK_STRUCTURE;

///
/// ACPI RASF Patrol Scrub command
///
#define EFI_ACPI_6_0_RASF_PATROL_SCRUB_COMMAND_GET_PATROL_PARAMETERS   0x01
#define EFI_ACPI_6_0_RASF_PATROL_SCRUB_COMMAND_START_PATROL_SCRUBBER   0x02
#define EFI_ACPI_6_0_RASF_PATROL_SCRUB_COMMAND_STOP_PATROL_SCRUBBER    0x03

///
/// Memory Power State Table definition.
///
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER Header;
  UINT8                       PlatformCommunicationChannelIdentifier;
  UINT8                       Reserved[3];
// Memory Power Node Structure
// Memory Power State Characteristics
} EFI_ACPI_6_0_MEMORY_POWER_STATUS_TABLE;

///
/// MPST Version (as defined in ACPI 6.0 spec.)
///
#define EFI_ACPI_6_0_MEMORY_POWER_STATE_TABLE_REVISION 0x01

///
/// MPST Platform Communication Channel Shared Memory Region definition.
///
typedef struct {
  UINT32                      Signature;
  UINT16                      Command;
  UINT16                      Status;
  UINT32                      MemoryPowerCommandRegister;
  UINT32                      MemoryPowerStatusRegister;
  UINT32                      PowerStateId;
  UINT32                      MemoryPowerNodeId;
  UINT64                      MemoryEnergyConsumed;
  UINT64                      ExpectedAveragePowerComsuned;
} EFI_ACPI_6_0_MPST_PLATFORM_COMMUNICATION_CHANNEL_SHARED_MEMORY_REGION;

///
/// ACPI MPST PCC command code
///
#define EFI_ACPI_6_0_MPST_PCC_COMMAND_CODE_EXECUTE_MPST_COMMAND  0x03

///
/// ACPI MPST Memory Power command
///
#define EFI_ACPI_6_0_MPST_MEMORY_POWER_COMMAND_GET_MEMORY_POWER_STATE       0x01
#define EFI_ACPI_6_0_MPST_MEMORY_POWER_COMMAND_SET_MEMORY_POWER_STATE       0x02
#define EFI_ACPI_6_0_MPST_MEMORY_POWER_COMMAND_GET_AVERAGE_POWER_CONSUMED   0x03
#define EFI_ACPI_6_0_MPST_MEMORY_POWER_COMMAND_GET_MEMORY_ENERGY_CONSUMED   0x04

///
/// MPST Memory Power Node Table
///
typedef struct {
  UINT8                                             PowerStateValue;
  UINT8                                             PowerStateInformationIndex;
} EFI_ACPI_6_0_MPST_MEMORY_POWER_STATE;

typedef struct {
  UINT8                                             Flag;
  UINT8                                             Reserved;
  UINT16                                            MemoryPowerNodeId;
  UINT32                                            Length;
  UINT64                                            AddressBase;
  UINT64                                            AddressLength;
  UINT32                                            NumberOfPowerStates;
  UINT32                                            NumberOfPhysicalComponents;
//EFI_ACPI_6_0_MPST_MEMORY_POWER_STATE              MemoryPowerState[NumberOfPowerStates];
//UINT16                                            PhysicalComponentIdentifier[NumberOfPhysicalComponents];
} EFI_ACPI_6_0_MPST_MEMORY_POWER_STRUCTURE;

#define EFI_ACPI_6_0_MPST_MEMORY_POWER_STRUCTURE_FLAG_ENABLE          0x01
#define EFI_ACPI_6_0_MPST_MEMORY_POWER_STRUCTURE_FLAG_POWER_MANAGED   0x02
#define EFI_ACPI_6_0_MPST_MEMORY_POWER_STRUCTURE_FLAG_HOT_PLUGGABLE   0x04

typedef struct {
  UINT16                      MemoryPowerNodeCount;
  UINT8                       Reserved[2];
} EFI_ACPI_6_0_MPST_MEMORY_POWER_NODE_TABLE;

///
/// MPST Memory Power State Characteristics Table
///
typedef struct {
  UINT8                                             PowerStateStructureID;
  UINT8                                             Flag;
  UINT16                                            Reserved;
  UINT32                                            AveragePowerConsumedInMPS0;
  UINT32                                            RelativePowerSavingToMPS0;
  UINT64                                            ExitLatencyToMPS0;
} EFI_ACPI_6_0_MPST_MEMORY_POWER_STATE_CHARACTERISTICS_STRUCTURE;

#define EFI_ACPI_6_0_MPST_MEMORY_POWER_STATE_CHARACTERISTICS_STRUCTURE_FLAG_MEMORY_CONTENT_PRESERVED              0x01
#define EFI_ACPI_6_0_MPST_MEMORY_POWER_STATE_CHARACTERISTICS_STRUCTURE_FLAG_AUTONOMOUS_MEMORY_POWER_STATE_ENTRY   0x02
#define EFI_ACPI_6_0_MPST_MEMORY_POWER_STATE_CHARACTERISTICS_STRUCTURE_FLAG_AUTONOMOUS_MEMORY_POWER_STATE_EXIT    0x04

typedef struct {
  UINT16                      MemoryPowerStateCharacteristicsCount;
  UINT8                       Reserved[2];
} EFI_ACPI_6_0_MPST_MEMORY_POWER_STATE_CHARACTERISTICS_TABLE;

///
/// Memory Topology Table definition.
///
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER Header;
  UINT32                      Reserved;
} EFI_ACPI_6_0_MEMORY_TOPOLOGY_TABLE;

///
/// PMTT Version (as defined in ACPI 6.0 spec.)
///
#define EFI_ACPI_6_0_MEMORY_TOPOLOGY_TABLE_REVISION 0x01

///
/// Common Memory Aggregator Device Structure.
///
typedef struct {
  UINT8                       Type;
  UINT8                       Reserved;
  UINT16                      Length;
  UINT16                      Flags;
  UINT16                      Reserved1;
} EFI_ACPI_6_0_PMMT_COMMON_MEMORY_AGGREGATOR_DEVICE_STRUCTURE;

///
/// Memory Aggregator Device Type
///
#define EFI_ACPI_6_0_PMMT_MEMORY_AGGREGATOR_DEVICE_TYPE_SOCKET            0x1
#define EFI_ACPI_6_0_PMMT_MEMORY_AGGREGATOR_DEVICE_TYPE_MEMORY_CONTROLLER 0x2
#define EFI_ACPI_6_0_PMMT_MEMORY_AGGREGATOR_DEVICE_TYPE_DIMM              0x3

///
/// Socket Memory Aggregator Device Structure.
///
typedef struct {
  EFI_ACPI_6_0_PMMT_COMMON_MEMORY_AGGREGATOR_DEVICE_STRUCTURE  Header;
  UINT16                                                       SocketIdentifier;
  UINT16                                                       Reserved;
//EFI_ACPI_6_0_PMMT_MEMORY_CONTROLLER_MEMORY_AGGREGATOR_DEVICE_STRUCTURE  MemoryController[];
} EFI_ACPI_6_0_PMMT_SOCKET_MEMORY_AGGREGATOR_DEVICE_STRUCTURE;

///
/// MemoryController Memory Aggregator Device Structure.
///
typedef struct {
  EFI_ACPI_6_0_PMMT_COMMON_MEMORY_AGGREGATOR_DEVICE_STRUCTURE  Header;
  UINT32                                                       ReadLatency;
  UINT32                                                       WriteLatency;
  UINT32                                                       ReadBandwidth;
  UINT32                                                       WriteBandwidth;
  UINT16                                                       OptimalAccessUnit;
  UINT16                                                       OptimalAccessAlignment;
  UINT16                                                       Reserved;
  UINT16                                                       NumberOfProximityDomains;
//UINT32                                                       ProximityDomain[NumberOfProximityDomains];
//EFI_ACPI_6_0_PMMT_DIMM_MEMORY_AGGREGATOR_DEVICE_STRUCTURE    PhysicalComponent[];
} EFI_ACPI_6_0_PMMT_MEMORY_CONTROLLER_MEMORY_AGGREGATOR_DEVICE_STRUCTURE;

///
/// DIMM Memory Aggregator Device Structure.
///
typedef struct {
  EFI_ACPI_6_0_PMMT_COMMON_MEMORY_AGGREGATOR_DEVICE_STRUCTURE  Header;
  UINT16                                                       PhysicalComponentIdentifier;
  UINT16                                                       Reserved;
  UINT32                                                       SizeOfDimm;
  UINT32                                                       SmbiosHandle;
} EFI_ACPI_6_0_PMMT_DIMM_MEMORY_AGGREGATOR_DEVICE_STRUCTURE;

///
/// Boot Graphics Resource Table definition.
///
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER Header;
  ///
  /// 2-bytes (16 bit) version ID. This value must be 1.
  ///
  UINT16                      Version;
  ///
  /// 1-byte status field indicating current status about the table.
  ///     Bits[7:1] = Reserved (must be zero)
  ///     Bit [0] = Valid. A one indicates the boot image graphic is valid.
  ///
  UINT8                       Status;
  ///
  /// 1-byte enumerated type field indicating format of the image.
  ///     0 = Bitmap
  ///     1 - 255  Reserved (for future use)
  ///
  UINT8                       ImageType;
  ///
  /// 8-byte (64 bit) physical address pointing to the firmware's in-memory copy
  /// of the image bitmap.
  ///
  UINT64                      ImageAddress;
  ///
  /// A 4-byte (32-bit) unsigned long describing the display X-offset of the boot image.
  /// (X, Y) display offset of the top left corner of the boot image.
  /// The top left corner of the display is at offset (0, 0).
  ///
  UINT32                      ImageOffsetX;
  ///
  /// A 4-byte (32-bit) unsigned long describing the display Y-offset of the boot image.
  /// (X, Y) display offset of the top left corner of the boot image.
  /// The top left corner of the display is at offset (0, 0).
  ///
  UINT32                      ImageOffsetY;
} EFI_ACPI_6_0_BOOT_GRAPHICS_RESOURCE_TABLE;

///
/// BGRT Revision
///
#define EFI_ACPI_6_0_BOOT_GRAPHICS_RESOURCE_TABLE_REVISION 1

///
/// BGRT Version
///
#define EFI_ACPI_6_0_BGRT_VERSION         0x01

///
/// BGRT Status
///
#define EFI_ACPI_6_0_BGRT_STATUS_NOT_DISPLAYED 0x00
#define EFI_ACPI_6_0_BGRT_STATUS_DISPLAYED     0x01

///
/// BGRT Image Type
///
#define EFI_ACPI_6_0_BGRT_IMAGE_TYPE_BMP  0x00

///
/// FPDT Version (as defined in ACPI 6.0 spec.)
///
#define EFI_ACPI_6_0_FIRMWARE_PERFORMANCE_DATA_TABLE_REVISION 0x01

///
/// FPDT Performance Record Types
///
#define EFI_ACPI_6_0_FPDT_RECORD_TYPE_FIRMWARE_BASIC_BOOT_POINTER      0x0000
#define EFI_ACPI_6_0_FPDT_RECORD_TYPE_S3_PERFORMANCE_TABLE_POINTER     0x0001

///
/// FPDT Performance Record Revision
///
#define EFI_ACPI_6_0_FPDT_RECORD_REVISION_FIRMWARE_BASIC_BOOT_POINTER  0x01
#define EFI_ACPI_6_0_FPDT_RECORD_REVISION_S3_PERFORMANCE_TABLE_POINTER 0x01

///
/// FPDT Runtime Performance Record Types
///
#define EFI_ACPI_6_0_FPDT_RUNTIME_RECORD_TYPE_S3_RESUME                0x0000
#define EFI_ACPI_6_0_FPDT_RUNTIME_RECORD_TYPE_S3_SUSPEND               0x0001
#define EFI_ACPI_6_0_FPDT_RUNTIME_RECORD_TYPE_FIRMWARE_BASIC_BOOT      0x0002

///
/// FPDT Runtime Performance Record Revision
///
#define EFI_ACPI_6_0_FPDT_RUNTIME_RECORD_REVISION_S3_RESUME            0x01
#define EFI_ACPI_6_0_FPDT_RUNTIME_RECORD_REVISION_S3_SUSPEND           0x01
#define EFI_ACPI_6_0_FPDT_RUNTIME_RECORD_REVISION_FIRMWARE_BASIC_BOOT  0x02

///
/// FPDT Performance Record header
///
typedef struct {
  UINT16           Type;
  UINT8            Length;
  UINT8            Revision;
} EFI_ACPI_6_0_FPDT_PERFORMANCE_RECORD_HEADER;

///
/// FPDT Performance Table header
///
typedef struct {
  UINT32  Signature;
  UINT32  Length;
} EFI_ACPI_6_0_FPDT_PERFORMANCE_TABLE_HEADER;

///
/// FPDT Firmware Basic Boot Performance Pointer Record Structure
///
typedef struct {
  EFI_ACPI_6_0_FPDT_PERFORMANCE_RECORD_HEADER     Header;
  UINT32                                          Reserved;
  ///
  /// 64-bit processor-relative physical address of the Basic Boot Performance Table.
  ///
  UINT64                                          BootPerformanceTablePointer;
} EFI_ACPI_6_0_FPDT_BOOT_PERFORMANCE_TABLE_POINTER_RECORD;

///
/// FPDT S3 Performance Table Pointer Record Structure
///
typedef struct {
  EFI_ACPI_6_0_FPDT_PERFORMANCE_RECORD_HEADER     Header;
  UINT32                                          Reserved;
  ///
  /// 64-bit processor-relative physical address of the S3 Performance Table.
  ///
  UINT64                                          S3PerformanceTablePointer;
} EFI_ACPI_6_0_FPDT_S3_PERFORMANCE_TABLE_POINTER_RECORD;

///
/// FPDT Firmware Basic Boot Performance Record Structure
///
typedef struct {
  EFI_ACPI_6_0_FPDT_PERFORMANCE_RECORD_HEADER     Header;
  UINT32                                          Reserved;
  ///
  /// Timer value logged at the beginning of firmware image execution.
  /// This may not always be zero or near zero.
  ///
  UINT64                                          ResetEnd;
  ///
  /// Timer value logged just prior to loading the OS boot loader into memory.
  /// For non-UEFI compatible boots, this field must be zero.
  ///
  UINT64                                          OsLoaderLoadImageStart;
  ///
  /// Timer value logged just prior to launching the previously loaded OS boot loader image.
  /// For non-UEFI compatible boots, the timer value logged will be just prior
  /// to the INT 19h handler invocation.
  ///
  UINT64                                          OsLoaderStartImageStart;
  ///
  /// Timer value logged at the point when the OS loader calls the
  /// ExitBootServices function for UEFI compatible firmware.
  /// For non-UEFI compatible boots, this field must be zero.
  ///
  UINT64                                          ExitBootServicesEntry;
  ///
  /// Timer value logged at the point just prior towhen the OS loader gaining
  /// control back from calls the ExitBootServices function for UEFI compatible firmware.
  /// For non-UEFI compatible boots, this field must be zero.
  ///
  UINT64                                          ExitBootServicesExit;
} EFI_ACPI_6_0_FPDT_FIRMWARE_BASIC_BOOT_RECORD;

///
/// FPDT Firmware Basic Boot Performance Table signature
///
#define EFI_ACPI_6_0_FPDT_BOOT_PERFORMANCE_TABLE_SIGNATURE  SIGNATURE_32('F', 'B', 'P', 'T')

//
// FPDT Firmware Basic Boot Performance Table
//
typedef struct {
  EFI_ACPI_6_0_FPDT_PERFORMANCE_TABLE_HEADER      Header;
  //
  // one or more Performance Records.
  //
} EFI_ACPI_6_0_FPDT_FIRMWARE_BASIC_BOOT_TABLE;

///
/// FPDT "S3PT" S3 Performance Table
///
#define EFI_ACPI_6_0_FPDT_S3_PERFORMANCE_TABLE_SIGNATURE  SIGNATURE_32('S', '3', 'P', 'T')

//
// FPDT Firmware S3 Boot Performance Table
//
typedef struct {
  EFI_ACPI_6_0_FPDT_PERFORMANCE_TABLE_HEADER      Header;
  //
  // one or more Performance Records.
  //
} EFI_ACPI_6_0_FPDT_FIRMWARE_S3_BOOT_TABLE;

///
/// FPDT Basic S3 Resume Performance Record
///
typedef struct {
  EFI_ACPI_6_0_FPDT_PERFORMANCE_RECORD_HEADER     Header;
  ///
  /// A count of the number of S3 resume cycles since the last full boot sequence.
  ///
  UINT32                                          ResumeCount;
  ///
  /// Timer recorded at the end of BIOS S3 resume, just prior to handoff to the
  /// OS waking vector. Only the most recent resume cycle's time is retained.
  ///
  UINT64                                          FullResume;
  ///
  /// Average timer value of all resume cycles logged since the last full boot
  /// sequence, including the most recent resume.  Note that the entire log of
  /// timer values does not need to be retained in order to calculate this average.
  ///
  UINT64                                          AverageResume;
} EFI_ACPI_6_0_FPDT_S3_RESUME_RECORD;

///
/// FPDT Basic S3 Suspend Performance Record
///
typedef struct {
  EFI_ACPI_6_0_FPDT_PERFORMANCE_RECORD_HEADER     Header;
  ///
  /// Timer value recorded at the OS write to SLP_TYP upon entry to S3.
  /// Only the most recent suspend cycle's timer value is retained.
  ///
  UINT64                                          SuspendStart;
  ///
  /// Timer value recorded at the final firmware write to SLP_TYP (or other
  /// mechanism) used to trigger hardware entry to S3.
  /// Only the most recent suspend cycle's timer value is retained.
  ///
  UINT64                                          SuspendEnd;
} EFI_ACPI_6_0_FPDT_S3_SUSPEND_RECORD;

///
/// Firmware Performance Record Table definition.
///
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER Header;
} EFI_ACPI_6_0_FIRMWARE_PERFORMANCE_RECORD_TABLE;

///
/// Generic Timer Description Table definition.
///
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER Header;
  UINT64                      CntControlBasePhysicalAddress;
  UINT32                      Reserved;
  UINT32                      SecurePL1TimerGSIV;
  UINT32                      SecurePL1TimerFlags;
  UINT32                      NonSecurePL1TimerGSIV;
  UINT32                      NonSecurePL1TimerFlags;
  UINT32                      VirtualTimerGSIV;
  UINT32                      VirtualTimerFlags;
  UINT32                      NonSecurePL2TimerGSIV;
  UINT32                      NonSecurePL2TimerFlags;
  UINT64                      CntReadBasePhysicalAddress;
  UINT32                      PlatformTimerCount;
  UINT32                      PlatformTimerOffset;
} EFI_ACPI_6_0_GENERIC_TIMER_DESCRIPTION_TABLE;

///
/// GTDT Version (as defined in ACPI 6.0 spec.)
///
#define EFI_ACPI_6_0_GENERIC_TIMER_DESCRIPTION_TABLE_REVISION 0x02

///
/// Timer Flags.  All other bits are reserved and must be 0.
///
#define EFI_ACPI_6_0_GTDT_TIMER_FLAG_TIMER_INTERRUPT_MODE          BIT0
#define EFI_ACPI_6_0_GTDT_TIMER_FLAG_TIMER_INTERRUPT_POLARITY      BIT1
#define EFI_ACPI_6_0_GTDT_TIMER_FLAG_ALWAYS_ON_CAPABILITY          BIT2

///
/// Platform Timer Type
///
#define EFI_ACPI_6_0_GTDT_GT_BLOCK                       0
#define EFI_ACPI_6_0_GTDT_SBSA_GENERIC_WATCHDOG          1

///
/// GT Block Structure
///
typedef struct {
  UINT8   Type;
  UINT16  Length;
  UINT8   Reserved;
  UINT64  CntCtlBase;
  UINT32  GTBlockTimerCount;
  UINT32  GTBlockTimerOffset;
} EFI_ACPI_6_0_GTDT_GT_BLOCK_STRUCTURE;

///
/// GT Block Timer Structure
///
typedef struct {
  UINT8   GTFrameNumber;
  UINT8   Reserved[3];
  UINT64  CntBaseX;
  UINT64  CntEL0BaseX;
  UINT32  GTxPhysicalTimerGSIV;
  UINT32  GTxPhysicalTimerFlags;
  UINT32  GTxVirtualTimerGSIV;
  UINT32  GTxVirtualTimerFlags;
  UINT32  GTxCommonFlags;
} EFI_ACPI_6_0_GTDT_GT_BLOCK_TIMER_STRUCTURE;

///
/// GT Block Physical Timers and Virtual Timers Flags.  All other bits are reserved and must be 0.
///
#define EFI_ACPI_6_0_GTDT_GT_BLOCK_TIMER_FLAG_TIMER_INTERRUPT_MODE          BIT0
#define EFI_ACPI_6_0_GTDT_GT_BLOCK_TIMER_FLAG_TIMER_INTERRUPT_POLARITY      BIT1

///
/// Common Flags Flags.  All other bits are reserved and must be 0.
///
#define EFI_ACPI_6_0_GTDT_GT_BLOCK_COMMON_FLAG_SECURE_TIMER              BIT0
#define EFI_ACPI_6_0_GTDT_GT_BLOCK_COMMON_FLAG_ALWAYS_ON_CAPABILITY      BIT1

///
/// SBSA Generic Watchdog Structure
///
typedef struct {
  UINT8   Type;
  UINT16  Length;
  UINT8   Reserved;
  UINT64  RefreshFramePhysicalAddress;
  UINT64  WatchdogControlFramePhysicalAddress;
  UINT32  WatchdogTimerGSIV;
  UINT32  WatchdogTimerFlags;
} EFI_ACPI_6_0_GTDT_SBSA_GENERIC_WATCHDOG_STRUCTURE;

///
/// SBSA Generic Watchdog Timer Flags.  All other bits are reserved and must be 0.
///
#define EFI_ACPI_6_0_GTDT_SBSA_GENERIC_WATCHDOG_FLAG_TIMER_INTERRUPT_MODE          BIT0
#define EFI_ACPI_6_0_GTDT_SBSA_GENERIC_WATCHDOG_FLAG_TIMER_INTERRUPT_POLARITY      BIT1
#define EFI_ACPI_6_0_GTDT_SBSA_GENERIC_WATCHDOG_FLAG_SECURE_TIMER                  BIT2

//
// NVDIMM Firmware Interface Table definition.
//
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER    Header;
  UINT32                         Reserved;
} EFI_ACPI_6_0_NVDIMM_FIRMWARE_INTERFACE_TABLE;

//
// NFIT Version (as defined in ACPI 6.0 spec.)
//
#define EFI_ACPI_6_0_NVDIMM_FIRMWARE_INTERFACE_TABLE_REVISION 0x1

//
// Definition for NFIT Table Structure Types
//
#define EFI_ACPI_6_0_NFIT_SYSTEM_PHYSICAL_ADDRESS_RANGE_STRUCTURE_TYPE              0
#define EFI_ACPI_6_0_NFIT_MEMORY_DEVICE_TO_SYSTEM_ADDRESS_RANGE_MAP_STRUCTURE_TYPE  1
#define EFI_ACPI_6_0_NFIT_INTERLEAVE_STRUCTURE_TYPE                                 2
#define EFI_ACPI_6_0_NFIT_SMBIOS_MANAGEMENT_INFORMATION_STRUCTURE_TYPE              3
#define EFI_ACPI_6_0_NFIT_NVDIMM_CONTROL_REGION_STRUCTURE_TYPE                      4
#define EFI_ACPI_6_0_NFIT_NVDIMM_BLOCK_DATA_WINDOW_REGION_STRUCTURE_TYPE            5
#define EFI_ACPI_6_0_NFIT_FLUSH_HINT_ADDRESS_STRUCTURE_TYPE                         6

//
// Definition for NFIT Structure Header
//
typedef struct {
  UINT16                                      Type;
  UINT16                                      Length;
} EFI_ACPI_6_0_NFIT_STRUCTURE_HEADER;

//
// Definition for System Physical Address Range Structure
//
#define EFI_ACPI_6_0_NFIT_SYSTEM_PHYSICAL_ADDRESS_RANGE_FLAGS_CONTROL_REGION_FOR_MANAGEMENT      BIT0
#define EFI_ACPI_6_0_NFIT_SYSTEM_PHYSICAL_ADDRESS_RANGE_FLAGS_PROXIMITY_DOMAIN_VALID             BIT1
#define EFI_ACPI_6_0_NFIT_GUID_VOLATILE_MEMORY_REGION                             { 0x7305944F, 0xFDDA, 0x44E3, { 0xB1, 0x6C, 0x3F, 0x22, 0xD2, 0x52, 0xE5, 0xD0 }}
#define EFI_ACPI_6_0_NFIT_GUID_BYTE_ADDRESSABLE_PERSISTENT_MEMORY_REGION          { 0x66F0D379, 0xB4F3, 0x4074, { 0xAC, 0x43, 0x0D, 0x33, 0x18, 0xB7, 0x8C, 0xDB }}
#define EFI_ACPI_6_0_NFIT_GUID_NVDIMM_CONTROL_REGION                              { 0x92F701F6, 0x13B4, 0x405D, { 0x91, 0x0B, 0x29, 0x93, 0x67, 0xE8, 0x23, 0x4C }}
#define EFI_ACPI_6_0_NFIT_GUID_NVDIMM_BLOCK_DATA_WINDOW_REGION                    { 0x91AF0530, 0x5D86, 0x470E, { 0xA6, 0xB0, 0x0A, 0x2D, 0xB9, 0x40, 0x82, 0x49 }}
#define EFI_ACPI_6_0_NFIT_GUID_RAM_DISK_SUPPORTING_VIRTUAL_DISK_REGION_VOLATILE   { 0x77AB535A, 0x45FC, 0x624B, { 0x55, 0x60, 0xF7, 0xB2, 0x81, 0xD1, 0xF9, 0x6E }}
#define EFI_ACPI_6_0_NFIT_GUID_RAM_DISK_SUPPORTING_VIRTUAL_CD_REGION_VOLATILE     { 0x3D5ABD30, 0x4175, 0x87CE, { 0x6D, 0x64, 0xD2, 0xAD, 0xE5, 0x23, 0xC4, 0xBB }}
#define EFI_ACPI_6_0_NFIT_GUID_RAM_DISK_SUPPORTING_VIRTUAL_DISK_REGION_PERSISTENT { 0x5CEA02C9, 0x4D07, 0x69D3, { 0x26, 0x9F ,0x44, 0x96, 0xFB, 0xE0, 0x96, 0xF9 }}
#define EFI_ACPI_6_0_NFIT_GUID_RAM_DISK_SUPPORTING_VIRTUAL_CD_REGION_PERSISTENT   { 0x08018188, 0x42CD, 0xBB48, { 0x10, 0x0F, 0x53, 0x87, 0xD5, 0x3D, 0xED, 0x3D }}
typedef struct {
  UINT16                                      Type;
  UINT16                                      Length;
  UINT16                                      SPARangeStructureIndex;
  UINT16                                      Flags;
  UINT32                                      Reserved_8;
  UINT32                                      ProximityDomain;
  GUID                                        AddressRangeTypeGUID;
  UINT64                                      SystemPhysicalAddressRangeBase;
  UINT64                                      SystemPhysicalAddressRangeLength;
  UINT64                                      AddressRangeMemoryMappingAttribute;
} EFI_ACPI_6_0_NFIT_SYSTEM_PHYSICAL_ADDRESS_RANGE_STRUCTURE;

//
// Definition for Memory Device to System Physical Address Range Mapping Structure
//
typedef struct {
  UINT32                                      DIMMNumber:4;
  UINT32                                      MemoryChannelNumber:4;
  UINT32                                      MemoryControllerID:4;
  UINT32                                      SocketID:4;
  UINT32                                      NodeControllerID:12;
  UINT32                                      Reserved_28:4;
} EFI_ACPI_6_0_NFIT_DEVICE_HANDLE;

#define EFI_ACPI_6_0_NFIT_MEMORY_DEVICE_STATE_FLAGS_PREVIOUS_SAVE_FAIL                                      BIT0
#define EFI_ACPI_6_0_NFIT_MEMORY_DEVICE_STATE_FLAGS_LAST_RESTORE_FAIL                                       BIT1
#define EFI_ACPI_6_0_NFIT_MEMORY_DEVICE_STATE_FLAGS_PLATFORM_FLUSH_FAIL                                     BIT2
#define EFI_ACPI_6_0_NFIT_MEMORY_DEVICE_STATE_FLAGS_NOT_ARMED_PRIOR_TO_OSPM_HAND_OFF                        BIT3
#define EFI_ACPI_6_0_NFIT_MEMORY_DEVICE_STATE_FLAGS_SMART_HEALTH_EVENTS_PRIOR_OSPM_HAND_OFF                 BIT4
#define EFI_ACPI_6_0_NFIT_MEMORY_DEVICE_STATE_FLAGS_FIRMWARE_ENABLED_TO_NOTIFY_OSPM_ON_SMART_HEALTH_EVENTS  BIT5
typedef struct {
  UINT16                                      Type;
  UINT16                                      Length;
  EFI_ACPI_6_0_NFIT_DEVICE_HANDLE             NFITDeviceHandle;
  UINT16                                      MemoryDevicePhysicalID;
  UINT16                                      MemoryDeviceRegionID;
  UINT16                                      SPARangeStructureIndex ;
  UINT16                                      NVDIMMControlRegionStructureIndex;
  UINT64                                      MemoryDeviceRegionSize;
  UINT64                                      RegionOffset;
  UINT64                                      MemoryDevicePhysicalAddressRegionBase;
  UINT16                                      InterleaveStructureIndex;
  UINT16                                      InterleaveWays;
  UINT16                                      MemoryDeviceStateFlags;
  UINT16                                      Reserved_46;
} EFI_ACPI_6_0_NFIT_MEMORY_DEVICE_TO_SYSTEM_ADDRESS_RANGE_MAP_STRUCTURE;

//
// Definition for Interleave Structure
//
typedef struct {
  UINT16                                      Type;
  UINT16                                      Length;
  UINT16                                      InterleaveStructureIndex;
  UINT16                                      Reserved_6;
  UINT32                                      NumberOfLines;
  UINT32                                      LineSize;
//UINT32                                      LineOffset[NumberOfLines];
} EFI_ACPI_6_0_NFIT_INTERLEAVE_STRUCTURE;

//
// Definition for SMBIOS Management Information Structure
//
typedef struct {
  UINT16                                      Type;
  UINT16                                      Length;
  UINT32                                      Reserved_4;
//UINT8                                       Data[];
} EFI_ACPI_6_0_NFIT_SMBIOS_MANAGEMENT_INFORMATION_STRUCTURE;

//
// Definition for NVDIMM Control Region Structure
//
#define EFI_ACPI_6_0_NFIT_NVDIMM_CONTROL_REGION_FLAGS_BLOCK_DATA_WINDOWS_BUFFERED    BIT0
typedef struct {
  UINT16                                      Type;
  UINT16                                      Length;
  UINT16                                      NVDIMMControlRegionStructureIndex;
  UINT16                                      VendorID;
  UINT16                                      DeviceID;
  UINT16                                      RevisionID;
  UINT16                                      SubsystemVendorID;
  UINT16                                      SubsystemDeviceID;
  UINT16                                      SubsystemRevisionID;
  UINT8                                       Reserved_18[6];
  UINT32                                      SerialNumber;
  UINT16                                      RegionFormatInterfaceCode;
  UINT16                                      NumberOfBlockControlWindows;
  UINT64                                      SizeOfBlockControlWindow;
  UINT64                                      CommandRegisterOffsetInBlockControlWindow;
  UINT64                                      SizeOfCommandRegisterInBlockControlWindows;
  UINT64                                      StatusRegisterOffsetInBlockControlWindow;
  UINT64                                      SizeOfStatusRegisterInBlockControlWindows;
  UINT16                                      NVDIMMControlRegionFlag;
  UINT8                                       Reserved_74[6];
} EFI_ACPI_6_0_NFIT_NVDIMM_CONTROL_REGION_STRUCTURE;

//
// Definition for NVDIMM Block Data Window Region Structure
//
typedef struct {
  UINT16                                      Type;
  UINT16                                      Length;
  UINT16                                      NVDIMMControlRegionStructureIndex;
  UINT16                                      NumberOfBlockDataWindows;
  UINT64                                      BlockDataWindowStartOffset;
  UINT64                                      SizeOfBlockDataWindow;
  UINT64                                      BlockAccessibleMemoryCapacity;
  UINT64                                      BeginningAddressOfFirstBlockInBlockAccessibleMemory;
} EFI_ACPI_6_0_NFIT_NVDIMM_BLOCK_DATA_WINDOW_REGION_STRUCTURE;

//
// Definition for Flush Hint Address Structure
//
typedef struct {
  UINT16                                      Type;
  UINT16                                      Length;
  EFI_ACPI_6_0_NFIT_DEVICE_HANDLE             NFITDeviceHandle;
  UINT16                                      NumberOfFlushHintAddresses;
  UINT8                                       Reserved_10[6];
//UINT64                                      FlushHintAddress[NumberOfFlushHintAddresses];
} EFI_ACPI_6_0_NFIT_FLUSH_HINT_ADDRESS_STRUCTURE;

///
/// Boot Error Record Table (BERT)
///
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER Header;
  UINT32                      BootErrorRegionLength;
  UINT64                      BootErrorRegion;
} EFI_ACPI_6_0_BOOT_ERROR_RECORD_TABLE_HEADER;

///
/// BERT Version (as defined in ACPI 6.0 spec.)
///
#define EFI_ACPI_6_0_BOOT_ERROR_RECORD_TABLE_REVISION 0x01

///
/// Boot Error Region Block Status Definition
///
typedef struct {
  UINT32       UncorrectableErrorValid:1;
  UINT32       CorrectableErrorValid:1;
  UINT32       MultipleUncorrectableErrors:1;
  UINT32       MultipleCorrectableErrors:1;
  UINT32       ErrorDataEntryCount:10;
  UINT32       Reserved:18;
} EFI_ACPI_6_0_ERROR_BLOCK_STATUS;

///
/// Boot Error Region Definition
///
typedef struct {
  EFI_ACPI_6_0_ERROR_BLOCK_STATUS              BlockStatus;
  UINT32                                       RawDataOffset;
  UINT32                                       RawDataLength;
  UINT32                                       DataLength;
  UINT32                                       ErrorSeverity;
} EFI_ACPI_6_0_BOOT_ERROR_REGION_STRUCTURE;

//
// Boot Error Severity types
//
#define EFI_ACPI_6_0_ERROR_SEVERITY_CORRECTABLE  0x00
#define EFI_ACPI_6_0_ERROR_SEVERITY_FATAL        0x01
#define EFI_ACPI_6_0_ERROR_SEVERITY_CORRECTED    0x02
#define EFI_ACPI_6_0_ERROR_SEVERITY_NONE         0x03

///
/// Generic Error Data Entry Definition
///
typedef struct {
  UINT8    SectionType[16];
  UINT32   ErrorSeverity;
  UINT16   Revision;
  UINT8    ValidationBits;
  UINT8    Flags;
  UINT32   ErrorDataLength;
  UINT8    FruId[16];
  UINT8    FruText[20];
} EFI_ACPI_6_0_GENERIC_ERROR_DATA_ENTRY_STRUCTURE;

///
/// Generic Error Data Entry Version (as defined in ACPI 6.0 spec.)
///
#define EFI_ACPI_6_0_GENERIC_ERROR_DATA_ENTRY_REVISION  0x0201

///
/// HEST - Hardware Error Source Table
///
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER Header;
  UINT32                      ErrorSourceCount;
} EFI_ACPI_6_0_HARDWARE_ERROR_SOURCE_TABLE_HEADER;

///
/// HEST Version (as defined in ACPI 6.0 spec.)
///
#define EFI_ACPI_6_0_HARDWARE_ERROR_SOURCE_TABLE_REVISION 0x01

//
// Error Source structure types.
//
#define EFI_ACPI_6_0_IA32_ARCHITECTURE_MACHINE_CHECK_EXCEPTION  0x00
#define EFI_ACPI_6_0_IA32_ARCHITECTURE_CORRECTED_MACHINE_CHECK  0x01
#define EFI_ACPI_6_0_IA32_ARCHITECTURE_NMI_ERROR                0x02
#define EFI_ACPI_6_0_PCI_EXPRESS_ROOT_PORT_AER                  0x06
#define EFI_ACPI_6_0_PCI_EXPRESS_DEVICE_AER                     0x07
#define EFI_ACPI_6_0_PCI_EXPRESS_BRIDGE_AER                     0x08
#define EFI_ACPI_6_0_GENERIC_HARDWARE_ERROR                     0x09

//
// Error Source structure flags.
//
#define EFI_ACPI_6_0_ERROR_SOURCE_FLAG_FIRMWARE_FIRST       (1 << 0)
#define EFI_ACPI_6_0_ERROR_SOURCE_FLAG_GLOBAL               (1 << 1)

///
/// IA-32 Architecture Machine Check Exception Structure Definition
///
typedef struct {
  UINT16  Type;
  UINT16  SourceId;
  UINT8   Reserved0[2];
  UINT8   Flags;
  UINT8   Enabled;
  UINT32  NumberOfRecordsToPreAllocate;
  UINT32  MaxSectionsPerRecord;
  UINT64  GlobalCapabilityInitData;
  UINT64  GlobalControlInitData;
  UINT8   NumberOfHardwareBanks;
  UINT8   Reserved1[7];
} EFI_ACPI_6_0_IA32_ARCHITECTURE_MACHINE_CHECK_EXCEPTION_STRUCTURE;

///
/// IA-32 Architecture Machine Check Bank Structure Definition
///
typedef struct {
  UINT8   BankNumber;
  UINT8   ClearStatusOnInitialization;
  UINT8   StatusDataFormat;
  UINT8   Reserved0;
  UINT32  ControlRegisterMsrAddress;
  UINT64  ControlInitData;
  UINT32  StatusRegisterMsrAddress;
  UINT32  AddressRegisterMsrAddress;
  UINT32  MiscRegisterMsrAddress;
} EFI_ACPI_6_0_IA32_ARCHITECTURE_MACHINE_CHECK_ERROR_BANK_STRUCTURE;

///
/// IA-32 Architecture Machine Check Bank Structure MCA data format
///
#define EFI_ACPI_6_0_IA32_ARCHITECTURE_MACHINE_CHECK_ERROR_DATA_FORMAT_IA32      0x00
#define EFI_ACPI_6_0_IA32_ARCHITECTURE_MACHINE_CHECK_ERROR_DATA_FORMAT_INTEL64   0x01
#define EFI_ACPI_6_0_IA32_ARCHITECTURE_MACHINE_CHECK_ERROR_DATA_FORMAT_AMD64     0x02

//
// Hardware Error Notification types. All other values are reserved
//
#define EFI_ACPI_6_0_HARDWARE_ERROR_NOTIFICATION_POLLED                0x00
#define EFI_ACPI_6_0_HARDWARE_ERROR_NOTIFICATION_EXTERNAL_INTERRUPT    0x01
#define EFI_ACPI_6_0_HARDWARE_ERROR_NOTIFICATION_LOCAL_INTERRUPT       0x02
#define EFI_ACPI_6_0_HARDWARE_ERROR_NOTIFICATION_SCI                   0x03
#define EFI_ACPI_6_0_HARDWARE_ERROR_NOTIFICATION_NMI                   0x04
#define EFI_ACPI_6_0_HARDWARE_ERROR_NOTIFICATION_CMCI                  0x05
#define EFI_ACPI_6_0_HARDWARE_ERROR_NOTIFICATION_MCE                   0x06
#define EFI_ACPI_6_0_HARDWARE_ERROR_NOTIFICATION_GPIO_SIGNAL           0x07

///
/// Hardware Error Notification Configuration Write Enable Structure Definition
///
typedef struct {
  UINT16    Type:1;
  UINT16    PollInterval:1;
  UINT16    SwitchToPollingThresholdValue:1;
  UINT16    SwitchToPollingThresholdWindow:1;
  UINT16    ErrorThresholdValue:1;
  UINT16    ErrorThresholdWindow:1;
  UINT16    Reserved:10;
} EFI_ACPI_6_0_HARDWARE_ERROR_NOTIFICATION_CONFIGURATION_WRITE_ENABLE_STRUCTURE;

///
/// Hardware Error Notification Structure Definition
///
typedef struct {
  UINT8                                                                          Type;
  UINT8                                                                          Length;
  EFI_ACPI_6_0_HARDWARE_ERROR_NOTIFICATION_CONFIGURATION_WRITE_ENABLE_STRUCTURE  ConfigurationWriteEnable;
  UINT32                                                                         PollInterval;
  UINT32                                                                         Vector;
  UINT32                                                                         SwitchToPollingThresholdValue;
  UINT32                                                                         SwitchToPollingThresholdWindow;
  UINT32                                                                         ErrorThresholdValue;
  UINT32                                                                         ErrorThresholdWindow;
} EFI_ACPI_6_0_HARDWARE_ERROR_NOTIFICATION_STRUCTURE;

///
/// IA-32 Architecture Corrected Machine Check Structure Definition
///
typedef struct {
  UINT16                                                 Type;
  UINT16                                                 SourceId;
  UINT8                                                  Reserved0[2];
  UINT8                                                  Flags;
  UINT8                                                  Enabled;
  UINT32                                                 NumberOfRecordsToPreAllocate;
  UINT32                                                 MaxSectionsPerRecord;
  EFI_ACPI_6_0_HARDWARE_ERROR_NOTIFICATION_STRUCTURE     NotificationStructure;
  UINT8                                                  NumberOfHardwareBanks;
  UINT8                                                  Reserved1[3];
} EFI_ACPI_6_0_IA32_ARCHITECTURE_CORRECTED_MACHINE_CHECK_STRUCTURE;

///
/// IA-32 Architecture NMI Error Structure Definition
///
typedef struct {
  UINT16  Type;
  UINT16  SourceId;
  UINT8   Reserved0[2];
  UINT32  NumberOfRecordsToPreAllocate;
  UINT32  MaxSectionsPerRecord;
  UINT32  MaxRawDataLength;
} EFI_ACPI_6_0_IA32_ARCHITECTURE_NMI_ERROR_STRUCTURE;

///
/// PCI Express Root Port AER Structure Definition
///
typedef struct {
  UINT16  Type;
  UINT16  SourceId;
  UINT8   Reserved0[2];
  UINT8   Flags;
  UINT8   Enabled;
  UINT32  NumberOfRecordsToPreAllocate;
  UINT32  MaxSectionsPerRecord;
  UINT32  Bus;
  UINT16  Device;
  UINT16  Function;
  UINT16  DeviceControl;
  UINT8   Reserved1[2];
  UINT32  UncorrectableErrorMask;
  UINT32  UncorrectableErrorSeverity;
  UINT32  CorrectableErrorMask;
  UINT32  AdvancedErrorCapabilitiesAndControl;
  UINT32  RootErrorCommand;
} EFI_ACPI_6_0_PCI_EXPRESS_ROOT_PORT_AER_STRUCTURE;

///
/// PCI Express Device AER Structure Definition
///
typedef struct {
  UINT16  Type;
  UINT16  SourceId;
  UINT8   Reserved0[2];
  UINT8   Flags;
  UINT8   Enabled;
  UINT32  NumberOfRecordsToPreAllocate;
  UINT32  MaxSectionsPerRecord;
  UINT32  Bus;
  UINT16  Device;
  UINT16  Function;
  UINT16  DeviceControl;
  UINT8   Reserved1[2];
  UINT32  UncorrectableErrorMask;
  UINT32  UncorrectableErrorSeverity;
  UINT32  CorrectableErrorMask;
  UINT32  AdvancedErrorCapabilitiesAndControl;
} EFI_ACPI_6_0_PCI_EXPRESS_DEVICE_AER_STRUCTURE;

///
/// PCI Express Bridge AER Structure Definition
///
typedef struct {
  UINT16  Type;
  UINT16  SourceId;
  UINT8   Reserved0[2];
  UINT8   Flags;
  UINT8   Enabled;
  UINT32  NumberOfRecordsToPreAllocate;
  UINT32  MaxSectionsPerRecord;
  UINT32  Bus;
  UINT16  Device;
  UINT16  Function;
  UINT16  DeviceControl;
  UINT8   Reserved1[2];
  UINT32  UncorrectableErrorMask;
  UINT32  UncorrectableErrorSeverity;
  UINT32  CorrectableErrorMask;
  UINT32  AdvancedErrorCapabilitiesAndControl;
  UINT32  SecondaryUncorrectableErrorMask;
  UINT32  SecondaryUncorrectableErrorSeverity;
  UINT32  SecondaryAdvancedErrorCapabilitiesAndControl;
} EFI_ACPI_6_0_PCI_EXPRESS_BRIDGE_AER_STRUCTURE;

///
/// Generic Hardware Error Source Structure Definition
///
typedef struct {
  UINT16                                                 Type;
  UINT16                                                 SourceId;
  UINT16                                                 RelatedSourceId;
  UINT8                                                  Flags;
  UINT8                                                  Enabled;
  UINT32                                                 NumberOfRecordsToPreAllocate;
  UINT32                                                 MaxSectionsPerRecord;
  UINT32                                                 MaxRawDataLength;
  EFI_ACPI_6_0_GENERIC_ADDRESS_STRUCTURE                 ErrorStatusAddress;
  EFI_ACPI_6_0_HARDWARE_ERROR_NOTIFICATION_STRUCTURE     NotificationStructure;
  UINT32                                                 ErrorStatusBlockLength;
} EFI_ACPI_6_0_GENERIC_HARDWARE_ERROR_SOURCE_STRUCTURE;

///
/// Generic Error Status Definition
///
typedef struct {
  EFI_ACPI_6_0_ERROR_BLOCK_STATUS              BlockStatus;
  UINT32                                       RawDataOffset;
  UINT32                                       RawDataLength;
  UINT32                                       DataLength;
  UINT32                                       ErrorSeverity;
} EFI_ACPI_6_0_GENERIC_ERROR_STATUS_STRUCTURE;

///
/// ERST - Error Record Serialization Table
///
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER Header;
  UINT32                      SerializationHeaderSize;
  UINT8                       Reserved0[4];
  UINT32                      InstructionEntryCount;
} EFI_ACPI_6_0_ERROR_RECORD_SERIALIZATION_TABLE_HEADER;

///
/// ERST Version (as defined in ACPI 6.0 spec.)
///
#define EFI_ACPI_6_0_ERROR_RECORD_SERIALIZATION_TABLE_REVISION 0x01

///
/// ERST Serialization Actions
///
#define EFI_ACPI_6_0_ERST_BEGIN_WRITE_OPERATION                    0x00
#define EFI_ACPI_6_0_ERST_BEGIN_READ_OPERATION                     0x01
#define EFI_ACPI_6_0_ERST_BEGIN_CLEAR_OPERATION                    0x02
#define EFI_ACPI_6_0_ERST_END_OPERATION                            0x03
#define EFI_ACPI_6_0_ERST_SET_RECORD_OFFSET                        0x04
#define EFI_ACPI_6_0_ERST_EXECUTE_OPERATION                        0x05
#define EFI_ACPI_6_0_ERST_CHECK_BUSY_STATUS                        0x06
#define EFI_ACPI_6_0_ERST_GET_COMMAND_STATUS                       0x07
#define EFI_ACPI_6_0_ERST_GET_RECORD_IDENTIFIER                    0x08
#define EFI_ACPI_6_0_ERST_SET_RECORD_IDENTIFIER                    0x09
#define EFI_ACPI_6_0_ERST_GET_RECORD_COUNT                         0x0A
#define EFI_ACPI_6_0_ERST_BEGIN_DUMMY_WRITE_OPERATION              0x0B
#define EFI_ACPI_6_0_ERST_GET_ERROR_LOG_ADDRESS_RANGE              0x0D
#define EFI_ACPI_6_0_ERST_GET_ERROR_LOG_ADDRESS_RANGE_LENGTH       0x0E
#define EFI_ACPI_6_0_ERST_GET_ERROR_LOG_ADDRESS_RANGE_ATTRIBUTES   0x0F

///
/// ERST Action Command Status
///
#define EFI_ACPI_6_0_ERST_STATUS_SUCCESS                           0x00
#define EFI_ACPI_6_0_ERST_STATUS_NOT_ENOUGH_SPACE                  0x01
#define EFI_ACPI_6_0_ERST_STATUS_HARDWARE_NOT_AVAILABLE            0x02
#define EFI_ACPI_6_0_ERST_STATUS_FAILED                            0x03
#define EFI_ACPI_6_0_ERST_STATUS_RECORD_STORE_EMPTY                0x04
#define EFI_ACPI_6_0_ERST_STATUS_RECORD_NOT_FOUND                  0x05

///
/// ERST Serialization Instructions
///
#define EFI_ACPI_6_0_ERST_READ_REGISTER                            0x00
#define EFI_ACPI_6_0_ERST_READ_REGISTER_VALUE                      0x01
#define EFI_ACPI_6_0_ERST_WRITE_REGISTER                           0x02
#define EFI_ACPI_6_0_ERST_WRITE_REGISTER_VALUE                     0x03
#define EFI_ACPI_6_0_ERST_NOOP                                     0x04
#define EFI_ACPI_6_0_ERST_LOAD_VAR1                                0x05
#define EFI_ACPI_6_0_ERST_LOAD_VAR2                                0x06
#define EFI_ACPI_6_0_ERST_STORE_VAR1                               0x07
#define EFI_ACPI_6_0_ERST_ADD                                      0x08
#define EFI_ACPI_6_0_ERST_SUBTRACT                                 0x09
#define EFI_ACPI_6_0_ERST_ADD_VALUE                                0x0A
#define EFI_ACPI_6_0_ERST_SUBTRACT_VALUE                           0x0B
#define EFI_ACPI_6_0_ERST_STALL                                    0x0C
#define EFI_ACPI_6_0_ERST_STALL_WHILE_TRUE                         0x0D
#define EFI_ACPI_6_0_ERST_SKIP_NEXT_INSTRUCTION_IF_TRUE            0x0E
#define EFI_ACPI_6_0_ERST_GOTO                                     0x0F
#define EFI_ACPI_6_0_ERST_SET_SRC_ADDRESS_BASE                     0x10
#define EFI_ACPI_6_0_ERST_SET_DST_ADDRESS_BASE                     0x11
#define EFI_ACPI_6_0_ERST_MOVE_DATA                                0x12

///
/// ERST Instruction Flags
///
#define EFI_ACPI_6_0_ERST_PRESERVE_REGISTER                        0x01

///
/// ERST Serialization Instruction Entry
///
typedef struct {
  UINT8                                    SerializationAction;
  UINT8                                    Instruction;
  UINT8                                    Flags;
  UINT8                                    Reserved0;
  EFI_ACPI_6_0_GENERIC_ADDRESS_STRUCTURE   RegisterRegion;
  UINT64                                   Value;
  UINT64                                   Mask;
} EFI_ACPI_6_0_ERST_SERIALIZATION_INSTRUCTION_ENTRY;

///
/// EINJ - Error Injection Table
///
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER Header;
  UINT32                      InjectionHeaderSize;
  UINT8                       InjectionFlags;
  UINT8                       Reserved0[3];
  UINT32                      InjectionEntryCount;
} EFI_ACPI_6_0_ERROR_INJECTION_TABLE_HEADER;

///
/// EINJ Version (as defined in ACPI 6.0 spec.)
///
#define EFI_ACPI_6_0_ERROR_INJECTION_TABLE_REVISION 0x01

///
/// EINJ Error Injection Actions
///
#define EFI_ACPI_6_0_EINJ_BEGIN_INJECTION_OPERATION                0x00
#define EFI_ACPI_6_0_EINJ_GET_TRIGGER_ERROR_ACTION_TABLE           0x01
#define EFI_ACPI_6_0_EINJ_SET_ERROR_TYPE                           0x02
#define EFI_ACPI_6_0_EINJ_GET_ERROR_TYPE                           0x03
#define EFI_ACPI_6_0_EINJ_END_OPERATION                            0x04
#define EFI_ACPI_6_0_EINJ_EXECUTE_OPERATION                        0x05
#define EFI_ACPI_6_0_EINJ_CHECK_BUSY_STATUS                        0x06
#define EFI_ACPI_6_0_EINJ_GET_COMMAND_STATUS                       0x07
#define EFI_ACPI_6_0_EINJ_TRIGGER_ERROR                            0xFF

///
/// EINJ Action Command Status
///
#define EFI_ACPI_6_0_EINJ_STATUS_SUCCESS                           0x00
#define EFI_ACPI_6_0_EINJ_STATUS_UNKNOWN_FAILURE                   0x01
#define EFI_ACPI_6_0_EINJ_STATUS_INVALID_ACCESS                    0x02

///
/// EINJ Error Type Definition
///
#define EFI_ACPI_6_0_EINJ_ERROR_PROCESSOR_CORRECTABLE                 (1 << 0)
#define EFI_ACPI_6_0_EINJ_ERROR_PROCESSOR_UNCORRECTABLE_NONFATAL      (1 << 1)
#define EFI_ACPI_6_0_EINJ_ERROR_PROCESSOR_UNCORRECTABLE_FATAL         (1 << 2)
#define EFI_ACPI_6_0_EINJ_ERROR_MEMORY_CORRECTABLE                    (1 << 3)
#define EFI_ACPI_6_0_EINJ_ERROR_MEMORY_UNCORRECTABLE_NONFATAL         (1 << 4)
#define EFI_ACPI_6_0_EINJ_ERROR_MEMORY_UNCORRECTABLE_FATAL            (1 << 5)
#define EFI_ACPI_6_0_EINJ_ERROR_PCI_EXPRESS_CORRECTABLE               (1 << 6)
#define EFI_ACPI_6_0_EINJ_ERROR_PCI_EXPRESS_UNCORRECTABLE_NONFATAL    (1 << 7)
#define EFI_ACPI_6_0_EINJ_ERROR_PCI_EXPRESS_UNCORRECTABLE_FATAL       (1 << 8)
#define EFI_ACPI_6_0_EINJ_ERROR_PLATFORM_CORRECTABLE                  (1 << 9)
#define EFI_ACPI_6_0_EINJ_ERROR_PLATFORM_UNCORRECTABLE_NONFATAL       (1 << 10)
#define EFI_ACPI_6_0_EINJ_ERROR_PLATFORM_UNCORRECTABLE_FATAL          (1 << 11)

///
/// EINJ Injection Instructions
///
#define EFI_ACPI_6_0_EINJ_READ_REGISTER                            0x00
#define EFI_ACPI_6_0_EINJ_READ_REGISTER_VALUE                      0x01
#define EFI_ACPI_6_0_EINJ_WRITE_REGISTER                           0x02
#define EFI_ACPI_6_0_EINJ_WRITE_REGISTER_VALUE                     0x03
#define EFI_ACPI_6_0_EINJ_NOOP                                     0x04

///
/// EINJ Instruction Flags
///
#define EFI_ACPI_6_0_EINJ_PRESERVE_REGISTER                        0x01

///
/// EINJ Injection Instruction Entry
///
typedef struct {
  UINT8                                    InjectionAction;
  UINT8                                    Instruction;
  UINT8                                    Flags;
  UINT8                                    Reserved0;
  EFI_ACPI_6_0_GENERIC_ADDRESS_STRUCTURE   RegisterRegion;
  UINT64                                   Value;
  UINT64                                   Mask;
} EFI_ACPI_6_0_EINJ_INJECTION_INSTRUCTION_ENTRY;

///
/// EINJ Trigger Action Table
///
typedef struct {
  UINT32  HeaderSize;
  UINT32  Revision;
  UINT32  TableSize;
  UINT32  EntryCount;
} EFI_ACPI_6_0_EINJ_TRIGGER_ACTION_TABLE;

///
/// Platform Communications Channel Table (PCCT)
///
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER Header;
  UINT32                      Flags;
  UINT64                      Reserved;
} EFI_ACPI_6_0_PLATFORM_COMMUNICATION_CHANNEL_TABLE_HEADER;

///
/// PCCT Version (as defined in ACPI 6.0 spec.)
///
#define EFI_ACPI_6_0_PLATFORM_COMMUNICATION_CHANNEL_TABLE_REVISION 0x01

///
/// PCCT Global Flags
///
#define EFI_ACPI_6_0_PCCT_FLAGS_SCI_DOORBELL                      BIT0

//
// PCCT Subspace type
//
#define EFI_ACPI_6_0_PCCT_SUBSPACE_TYPE_GENERIC  0x00

///
/// PCC Subspace Structure Header
///
typedef struct {
  UINT8        Type;
  UINT8        Length;
} EFI_ACPI_6_0_PCCT_SUBSPACE_HEADER;

///
/// Generic Communications Subspace Structure
///
typedef struct {
  UINT8                                    Type;
  UINT8                                    Length;
  UINT8                                    Reserved[6];
  UINT64                                   BaseAddress;
  UINT64                                   AddressLength;
  EFI_ACPI_6_0_GENERIC_ADDRESS_STRUCTURE   DoorbellRegister;
  UINT64                                   DoorbellPreserve;
  UINT64                                   DoorbellWrite;
  UINT32                                   NominalLatency;
  UINT32                                   MaximumPeriodicAccessRate;
  UINT16                                   MinimumRequestTurnaroundTime;
} EFI_ACPI_6_0_PCCT_SUBSPACE_GENERIC;

///
/// Generic Communications Channel Shared Memory Region
///

typedef struct {
  UINT8                                    Command;
  UINT8                                    Reserved:7;
  UINT8                                    GenerateSci:1;
} EFI_ACPI_6_0_PCCT_GENERIC_SHARED_MEMORY_REGION_COMMAND;

typedef struct {
  UINT8                                    CommandComplete:1;
  UINT8                                    SciDoorbell:1;
  UINT8                                    Error:1;
  UINT8                                    PlatformNotification:1;  
  UINT8                                    Reserved:4;
  UINT8                                    Reserved1;
} EFI_ACPI_6_0_PCCT_GENERIC_SHARED_MEMORY_REGION_STATUS;

typedef struct {
  UINT32                                                    Signature;
  EFI_ACPI_6_0_PCCT_GENERIC_SHARED_MEMORY_REGION_COMMAND    Command;
  EFI_ACPI_6_0_PCCT_GENERIC_SHARED_MEMORY_REGION_STATUS     Status;
} EFI_ACPI_6_0_PCCT_GENERIC_SHARED_MEMORY_REGION_HEADER;

//
// Known table signatures
//

///
/// "RSD PTR " Root System Description Pointer
///
#define EFI_ACPI_6_0_ROOT_SYSTEM_DESCRIPTION_POINTER_SIGNATURE  SIGNATURE_64('R', 'S', 'D', ' ', 'P', 'T', 'R', ' ') 

///
/// "APIC" Multiple APIC Description Table
///
#define EFI_ACPI_6_0_MULTIPLE_APIC_DESCRIPTION_TABLE_SIGNATURE  SIGNATURE_32('A', 'P', 'I', 'C')

///
/// "BERT" Boot Error Record Table
///
#define EFI_ACPI_6_0_BOOT_ERROR_RECORD_TABLE_SIGNATURE  SIGNATURE_32('B', 'E', 'R', 'T')

///
/// "BGRT" Boot Graphics Resource Table
///
#define EFI_ACPI_6_0_BOOT_GRAPHICS_RESOURCE_TABLE_SIGNATURE  SIGNATURE_32('B', 'G', 'R', 'T')

///
/// "CPEP" Corrected Platform Error Polling Table
///
#define EFI_ACPI_6_0_CORRECTED_PLATFORM_ERROR_POLLING_TABLE_SIGNATURE  SIGNATURE_32('C', 'P', 'E', 'P')

///
/// "DSDT" Differentiated System Description Table
///
#define EFI_ACPI_6_0_DIFFERENTIATED_SYSTEM_DESCRIPTION_TABLE_SIGNATURE  SIGNATURE_32('D', 'S', 'D', 'T')

///
/// "ECDT" Embedded Controller Boot Resources Table
///
#define EFI_ACPI_6_0_EMBEDDED_CONTROLLER_BOOT_RESOURCES_TABLE_SIGNATURE  SIGNATURE_32('E', 'C', 'D', 'T')

///
/// "EINJ" Error Injection Table
///
#define EFI_ACPI_6_0_ERROR_INJECTION_TABLE_SIGNATURE  SIGNATURE_32('E', 'I', 'N', 'J')

///
/// "ERST" Error Record Serialization Table
///
#define EFI_ACPI_6_0_ERROR_RECORD_SERIALIZATION_TABLE_SIGNATURE  SIGNATURE_32('E', 'R', 'S', 'T')

///
/// "FACP" Fixed ACPI Description Table
///
#define EFI_ACPI_6_0_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE  SIGNATURE_32('F', 'A', 'C', 'P')

///
/// "FACS" Firmware ACPI Control Structure
///
#define EFI_ACPI_6_0_FIRMWARE_ACPI_CONTROL_STRUCTURE_SIGNATURE  SIGNATURE_32('F', 'A', 'C', 'S')

///
/// "FPDT" Firmware Performance Data Table
///
#define EFI_ACPI_6_0_FIRMWARE_PERFORMANCE_DATA_TABLE_SIGNATURE  SIGNATURE_32('F', 'P', 'D', 'T')

///
/// "GTDT" Generic Timer Description Table
///
#define EFI_ACPI_6_0_GENERIC_TIMER_DESCRIPTION_TABLE_SIGNATURE  SIGNATURE_32('G', 'T', 'D', 'T')

///
/// "HEST" Hardware Error Source Table
///
#define EFI_ACPI_6_0_HARDWARE_ERROR_SOURCE_TABLE_SIGNATURE  SIGNATURE_32('H', 'E', 'S', 'T')

///
/// "MPST" Memory Power State Table
///
#define EFI_ACPI_6_0_MEMORY_POWER_STATE_TABLE_SIGNATURE  SIGNATURE_32('M', 'P', 'S', 'T')

///
/// "MSCT" Maximum System Characteristics Table
///
#define EFI_ACPI_6_0_MAXIMUM_SYSTEM_CHARACTERISTICS_TABLE_SIGNATURE  SIGNATURE_32('M', 'S', 'C', 'T')

///
/// "NFIT" NVDIMM Firmware Interface Table
///
#define EFI_ACPI_6_0_NVDIMM_FIRMWARE_INTERFACE_TABLE_STRUCTURE_SIGNATURE  SIGNATURE_32('N', 'F', 'I', 'T')

///
/// "PMTT" Platform Memory Topology Table
///
#define EFI_ACPI_6_0_PLATFORM_MEMORY_TOPOLOGY_TABLE_SIGNATURE  SIGNATURE_32('P', 'M', 'T', 'T')

///
/// "PSDT" Persistent System Description Table
///
#define EFI_ACPI_6_0_PERSISTENT_SYSTEM_DESCRIPTION_TABLE_SIGNATURE  SIGNATURE_32('P', 'S', 'D', 'T')

///
/// "RASF" ACPI RAS Feature Table
///
#define EFI_ACPI_6_0_ACPI_RAS_FEATURE_TABLE_SIGNATURE  SIGNATURE_32('R', 'A', 'S', 'F')

///
/// "RSDT" Root System Description Table
///
#define EFI_ACPI_6_0_ROOT_SYSTEM_DESCRIPTION_TABLE_SIGNATURE  SIGNATURE_32('R', 'S', 'D', 'T')

///
/// "SBST" Smart Battery Specification Table
///
#define EFI_ACPI_6_0_SMART_BATTERY_SPECIFICATION_TABLE_SIGNATURE  SIGNATURE_32('S', 'B', 'S', 'T')

///
/// "SLIT" System Locality Information Table
///
#define EFI_ACPI_6_0_SYSTEM_LOCALITY_INFORMATION_TABLE_SIGNATURE  SIGNATURE_32('S', 'L', 'I', 'T')

///
/// "SRAT" System Resource Affinity Table
///
#define EFI_ACPI_6_0_SYSTEM_RESOURCE_AFFINITY_TABLE_SIGNATURE  SIGNATURE_32('S', 'R', 'A', 'T')

///
/// "SSDT" Secondary System Description Table
///
#define EFI_ACPI_6_0_SECONDARY_SYSTEM_DESCRIPTION_TABLE_SIGNATURE  SIGNATURE_32('S', 'S', 'D', 'T')

///
/// "XSDT" Extended System Description Table
///
#define EFI_ACPI_6_0_EXTENDED_SYSTEM_DESCRIPTION_TABLE_SIGNATURE  SIGNATURE_32('X', 'S', 'D', 'T')

///
/// "BOOT" MS Simple Boot Spec
///
#define EFI_ACPI_6_0_SIMPLE_BOOT_FLAG_TABLE_SIGNATURE  SIGNATURE_32('B', 'O', 'O', 'T')

///
/// "CSRT" MS Core System Resource Table
///
#define EFI_ACPI_6_0_CORE_SYSTEM_RESOURCE_TABLE_SIGNATURE  SIGNATURE_32('C', 'S', 'R', 'T')

///
/// "DBG2" MS Debug Port 2 Spec
///
#define EFI_ACPI_6_0_DEBUG_PORT_2_TABLE_SIGNATURE  SIGNATURE_32('D', 'B', 'G', '2')

///
/// "DBGP" MS Debug Port Spec
///
#define EFI_ACPI_6_0_DEBUG_PORT_TABLE_SIGNATURE  SIGNATURE_32('D', 'B', 'G', 'P')

///
/// "DMAR" DMA Remapping Table
///
#define EFI_ACPI_6_0_DMA_REMAPPING_TABLE_SIGNATURE  SIGNATURE_32('D', 'M', 'A', 'R')

///
/// "DRTM" Dynamic Root of Trust for Measurement Table
///
#define EFI_ACPI_6_0_DYNAMIC_ROOT_OF_TRUST_FOR_MEASUREMENT_TABLE_SIGNATURE  SIGNATURE_32('D', 'R', 'T', 'M')

///
/// "ETDT" Event Timer Description Table
///
#define EFI_ACPI_6_0_EVENT_TIMER_DESCRIPTION_TABLE_SIGNATURE  SIGNATURE_32('E', 'T', 'D', 'T')

///
/// "HPET" IA-PC High Precision Event Timer Table
///
#define EFI_ACPI_6_0_HIGH_PRECISION_EVENT_TIMER_TABLE_SIGNATURE  SIGNATURE_32('H', 'P', 'E', 'T')

///
/// "iBFT" iSCSI Boot Firmware Table
///
#define EFI_ACPI_6_0_ISCSI_BOOT_FIRMWARE_TABLE_SIGNATURE  SIGNATURE_32('i', 'B', 'F', 'T')

///
/// "IORT" I/O Remapping Table
///
#define EFI_ACPI_6_0_IO_REMAPPING_TABLE_SIGNATURE  SIGNATURE_32('I', 'O', 'R', 'T')

///
/// "IVRS" I/O Virtualization Reporting Structure
///
#define EFI_ACPI_6_0_IO_VIRTUALIZATION_REPORTING_STRUCTURE_SIGNATURE  SIGNATURE_32('I', 'V', 'R', 'S')

///
/// "LPIT" Low Power Idle Table
///
#define EFI_ACPI_6_0_LOW_POWER_IDLE_TABLE_STRUCTURE_SIGNATURE  SIGNATURE_32('L', 'P', 'I', 'T')

///
/// "MCFG" PCI Express Memory Mapped Configuration Space Base Address Description Table
///
#define EFI_ACPI_6_0_PCI_EXPRESS_MEMORY_MAPPED_CONFIGURATION_SPACE_BASE_ADDRESS_DESCRIPTION_TABLE_SIGNATURE  SIGNATURE_32('M', 'C', 'F', 'G')

///
/// "MCHI" Management Controller Host Interface Table
///
#define EFI_ACPI_6_0_MANAGEMENT_CONTROLLER_HOST_INTERFACE_TABLE_SIGNATURE  SIGNATURE_32('M', 'C', 'H', 'I')

///
/// "MSDM" MS Data Management Table
///
#define EFI_ACPI_6_0_DATA_MANAGEMENT_TABLE_SIGNATURE  SIGNATURE_32('M', 'S', 'D', 'M')

///
/// "SLIC" MS Software Licensing Table Specification
///
#define EFI_ACPI_6_0_SOFTWARE_LICENSING_TABLE_SIGNATURE  SIGNATURE_32('S', 'L', 'I', 'C')

///
/// "SPCR" Serial Port Concole Redirection Table
///
#define EFI_ACPI_6_0_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_SIGNATURE  SIGNATURE_32('S', 'P', 'C', 'R')

///
/// "SPMI" Server Platform Management Interface Table
///
#define EFI_ACPI_6_0_SERVER_PLATFORM_MANAGEMENT_INTERFACE_TABLE_SIGNATURE  SIGNATURE_32('S', 'P', 'M', 'I')

///
/// "STAO" _STA Override Table
///
#define EFI_ACPI_6_0_STA_OVERRIDE_TABLE_SIGNATURE  SIGNATURE_32('S', 'T', 'A', 'O')

///
/// "TCPA" Trusted Computing Platform Alliance Capabilities Table
///
#define EFI_ACPI_6_0_TRUSTED_COMPUTING_PLATFORM_ALLIANCE_CAPABILITIES_TABLE_SIGNATURE  SIGNATURE_32('T', 'C', 'P', 'A')

///
/// "TPM2" Trusted Computing Platform 1 Table
///
#define EFI_ACPI_6_0_TRUSTED_COMPUTING_PLATFORM_2_TABLE_SIGNATURE  SIGNATURE_32('T', 'P', 'M', '2')

///
/// "UEFI" UEFI ACPI Data Table
///
#define EFI_ACPI_6_0_UEFI_ACPI_DATA_TABLE_SIGNATURE  SIGNATURE_32('U', 'E', 'F', 'I')

///
/// "WAET" Windows ACPI Emulated Devices Table
///
#define EFI_ACPI_6_0_WINDOWS_ACPI_EMULATED_DEVICES_TABLE_SIGNATURE  SIGNATURE_32('W', 'A', 'E', 'T')

///
/// "WDAT" Watchdog Action Table
///
#define EFI_ACPI_6_0_WATCHDOG_ACTION_TABLE_SIGNATURE  SIGNATURE_32('W', 'D', 'A', 'T')

///
/// "WDRT" Watchdog Resource Table
///
#define EFI_ACPI_6_0_WATCHDOG_RESOURCE_TABLE_SIGNATURE  SIGNATURE_32('W', 'D', 'R', 'T')

///
/// "WPBT" MS Platform Binary Table
///
#define EFI_ACPI_6_0_PLATFORM_BINARY_TABLE_SIGNATURE  SIGNATURE_32('W', 'P', 'B', 'T')

///
/// "XENV" Xen Project Table
///
#define EFI_ACPI_6_0_XEN_PROJECT_TABLE_SIGNATURE  SIGNATURE_32('X', 'E', 'N', 'V')

#pragma pack()

#endif
