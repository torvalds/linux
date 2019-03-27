/** @file
  EFI Guid Partition Table Format Definition.

Copyright (c) 2006 - 2011, Intel Corporation. All rights reserved.<BR>
This program and the accompanying materials are licensed and made available under 
the terms and conditions of the BSD License that accompanies this distribution.  
The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php.                                          
    
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#ifndef __UEFI_GPT_H__
#define __UEFI_GPT_H__

///
/// The primary GUID Partition Table Header must be
/// located in LBA 1 (i.e., the second logical block).
///
#define PRIMARY_PART_HEADER_LBA 1
///
/// EFI Partition Table Signature: "EFI PART".
/// 
#define EFI_PTAB_HEADER_ID      SIGNATURE_64 ('E','F','I',' ','P','A','R','T')

#pragma pack(1)

///
/// GPT Partition Table Header.
///
typedef struct {
  ///
  /// The table header for the GPT partition Table.
  /// This header contains EFI_PTAB_HEADER_ID.
  ///
  EFI_TABLE_HEADER  Header;
  ///
  /// The LBA that contains this data structure.
  ///
  EFI_LBA           MyLBA;
  ///
  /// LBA address of the alternate GUID Partition Table Header.
  ///
  EFI_LBA           AlternateLBA;
  ///
  /// The first usable logical block that may be used
  /// by a partition described by a GUID Partition Entry.
  ///
  EFI_LBA           FirstUsableLBA;
  ///
  /// The last usable logical block that may be used
  /// by a partition described by a GUID Partition Entry.
  ///
  EFI_LBA           LastUsableLBA;
  ///
  /// GUID that can be used to uniquely identify the disk.
  ///
  EFI_GUID          DiskGUID;
  ///
  /// The starting LBA of the GUID Partition Entry array.
  ///
  EFI_LBA           PartitionEntryLBA;
  ///
  /// The number of Partition Entries in the GUID Partition Entry array.
  ///
  UINT32            NumberOfPartitionEntries;
  ///
  /// The size, in bytes, of each the GUID Partition
  /// Entry structures in the GUID Partition Entry
  /// array. This field shall be set to a value of 128 x 2^n where n is
  /// an integer greater than or equal to zero (e.g., 128, 256, 512, etc.).
  ///
  UINT32            SizeOfPartitionEntry;
  ///
  /// The CRC32 of the GUID Partition Entry array.
  /// Starts at PartitionEntryLBA and is
  /// computed over a byte length of
  /// NumberOfPartitionEntries * SizeOfPartitionEntry.
  ///
  UINT32            PartitionEntryArrayCRC32;
} EFI_PARTITION_TABLE_HEADER;

///
/// GPT Partition Entry.
///
typedef struct {
  ///
  /// Unique ID that defines the purpose and type of this Partition. A value of
  /// zero defines that this partition entry is not being used.
  ///
  EFI_GUID  PartitionTypeGUID;
  ///
  /// GUID that is unique for every partition entry. Every partition ever
  /// created will have a unique GUID.
  /// This GUID must be assigned when the GUID Partition Entry is created.
  ///
  EFI_GUID  UniquePartitionGUID;
  ///
  /// Starting LBA of the partition defined by this entry
  ///
  EFI_LBA   StartingLBA;
  ///
  /// Ending LBA of the partition defined by this entry.
  ///
  EFI_LBA   EndingLBA;
  ///
  /// Attribute bits, all bits reserved by UEFI
  /// Bit 0:      If this bit is set, the partition is required for the platform to function. The owner/creator of the
  ///             partition indicates that deletion or modification of the contents can result in loss of platform
  ///             features or failure for the platform to boot or operate. The system cannot function normally if
  ///             this partition is removed, and it should be considered part of the hardware of the system.
  ///             Actions such as running diagnostics, system recovery, or even OS install or boot, could
  ///             potentially stop working if this partition is removed. Unless OS software or firmware
  ///             recognizes this partition, it should never be removed or modified as the UEFI firmware or
  ///             platform hardware may become non-functional.
  /// Bit 1:      If this bit is set, then firmware must not produce an EFI_BLOCK_IO_PROTOCOL device for
  ///             this partition. By not producing an EFI_BLOCK_IO_PROTOCOL partition, file system
  ///             mappings will not be created for this partition in UEFI.
  /// Bit 2:      This bit is set aside to let systems with traditional PC-AT BIOS firmware implementations
  ///             inform certain limited, special-purpose software running on these systems that a GPT 
  ///             partition may be bootable. The UEFI boot manager must ignore this bit when selecting
  ///             a UEFI-compliant application, e.g., an OS loader.
  /// Bits 3-47:  Undefined and must be zero. Reserved for expansion by future versions of the UEFI
  ///             specification.
  /// Bits 48-63: Reserved for GUID specific use. The use of these bits will vary depending on the
  ///             PartitionTypeGUID. Only the owner of the PartitionTypeGUID is allowed
  ///             to modify these bits. They must be preserved if Bits 0-47 are modified..
  ///
  UINT64    Attributes;
  ///
  /// Null-terminated name of the partition.
  ///
  CHAR16    PartitionName[36];
} EFI_PARTITION_ENTRY;

#pragma pack()
#endif


