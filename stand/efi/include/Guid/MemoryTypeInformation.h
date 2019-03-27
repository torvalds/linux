/* $FreeBSD$ */
/** @file
  This file defines:
  * Memory Type Information GUID for HOB and Variable.
  * Memory Type Information Variable Name.
  * Memory Type Information GUID HOB data structure.

  The memory type information HOB and variable can
  be used to store the information for each memory type in Variable or HOB.

Copyright (c) 2006 - 2010, Intel Corporation. All rights reserved.<BR>
This program and the accompanying materials are licensed and made available under
the terms and conditions of the BSD License that accompanies this distribution.
The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php.

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#ifndef __MEMORY_TYPE_INFORMATION_GUID_H__
#define __MEMORY_TYPE_INFORMATION_GUID_H__

#define EFI_MEMORY_TYPE_INFORMATION_GUID \
  { 0x4c19049f,0x4137,0x4dd3, { 0x9c,0x10,0x8b,0x97,0xa8,0x3f,0xfd,0xfa } }

#define EFI_MEMORY_TYPE_INFORMATION_VARIABLE_NAME "MemoryTypeInformation"

extern EFI_GUID gEfiMemoryTypeInformationGuid;

typedef struct {
  UINT32  Type;             ///< EFI memory type defined in UEFI specification.
  UINT32  NumberOfPages;    ///< The pages of this type memory.
} EFI_MEMORY_TYPE_INFORMATION;

#endif
