/*-
 * Copyright (c) 2017 Netflix, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Routines to format EFI_DEVICE_PATHs from the UEFI standard. Much of
 * this file is taken from EDK2 and rototilled.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <ctype.h>
#include <efivar.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "efichar.h"

#include "efi-osdep.h"
#include "efivar-dp.h"

#include "uefi-dplib.h"

/* XXX STUBS -- this stuff doesn't work yet */
#define StrToIpv4Address(str, unk, ipv4ptr, unk2)
#define StrToIpv6Address(str, unk, ipv6ptr, unk2)

/*
 * OK. Now this is evil. Can't typedef it again. Sure beats changing them all.
 * Since we're doing it all as narrow characters since wchar_t can't be used on
 * FreeBSD and CHAR16 strings generally aren't a good fit. Since this parsing
 * doesn't need Unicode for anything, this works out well.
 */
#define CHAR16 char

/*
 * Taken from MdePkg/Library/UefiDevicePathLib/DevicePathFromText.c
 * hash a11928f3310518ab1c6fd34e8d0fdbb72de9602c 2017-Mar-01
 */

/** @file
  DevicePathFromText protocol as defined in the UEFI 2.0 specification.

Copyright (c) 2013 - 2017, Intel Corporation. All rights reserved.<BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

// #include "UefiDevicePathLib.h"

/**

  Duplicates a string.

  @param  Src  Source string.

  @return The duplicated string.

**/
static
CHAR16 *
UefiDevicePathLibStrDuplicate (
  IN CONST CHAR16  *Src
  )
{
  return AllocateCopyPool (StrSize (Src), Src);
}

/**

  Get parameter in a pair of parentheses follow the given node name.
  For example, given the "Pci(0,1)" and NodeName "Pci", it returns "0,1".

  @param  Str      Device Path Text.
  @param  NodeName Name of the node.

  @return Parameter text for the node.

**/
static
CHAR16 *
GetParamByNodeName (
  IN CHAR16 *Str,
  IN const CHAR16 *NodeName
  )
{
  CHAR16  *ParamStr;
  CHAR16  *StrPointer;
  UINTN   NodeNameLength;
  UINTN   ParameterLength;

  //
  // Check whether the node name matchs
  //
  NodeNameLength = StrLen (NodeName);
  if (StrnCmp (Str, NodeName, NodeNameLength) != 0) {
    return NULL;
  }

  ParamStr = Str + NodeNameLength;
  if (!IS_LEFT_PARENTH (*ParamStr)) {
    return NULL;
  }

  //
  // Skip the found '(' and find first occurrence of ')'
  //
  ParamStr++;
  ParameterLength = 0;
  StrPointer = ParamStr;
  while (!IS_NULL (*StrPointer)) {
    if (IS_RIGHT_PARENTH (*StrPointer)) {
      break;
    }
    StrPointer++;
    ParameterLength++;
  }
  if (IS_NULL (*StrPointer)) {
    //
    // ')' not found
    //
    return NULL;
  }

  ParamStr = AllocateCopyPool ((ParameterLength + 1) * sizeof (CHAR16), ParamStr);
  if (ParamStr == NULL) {
    return NULL;
  }
  //
  // Terminate the parameter string
  //
  ParamStr[ParameterLength] = '\0';

  return ParamStr;
}

/**
  Gets current sub-string from a string list, before return
  the list header is moved to next sub-string. The sub-string is separated
  by the specified character. For example, the separator is ',', the string
  list is "2,0,3", it returns "2", the remain list move to "0,3"

  @param  List        A string list separated by the specified separator
  @param  Separator   The separator character

  @return A pointer to the current sub-string

**/
static
CHAR16 *
SplitStr (
  IN OUT CHAR16 **List,
  IN     CHAR16 Separator
  )
{
  CHAR16  *Str;
  CHAR16  *ReturnStr;

  Str = *List;
  ReturnStr = Str;

  if (IS_NULL (*Str)) {
    return ReturnStr;
  }

  //
  // Find first occurrence of the separator
  //
  while (!IS_NULL (*Str)) {
    if (*Str == Separator) {
      break;
    }
    Str++;
  }

  if (*Str == Separator) {
    //
    // Find a sub-string, terminate it
    //
    *Str = '\0';
    Str++;
  }

  //
  // Move to next sub-string
  //
  *List = Str;

  return ReturnStr;
}

/**
  Gets the next parameter string from the list.

  @param List            A string list separated by the specified separator

  @return A pointer to the current sub-string

**/
static
CHAR16 *
GetNextParamStr (
  IN OUT CHAR16 **List
  )
{
  //
  // The separator is comma
  //
  return SplitStr (List, ',');
}

/**
  Get one device node from entire device path text.

  @param DevicePath      On input, the current Device Path node; on output, the next device path node
  @param IsInstanceEnd   This node is the end of a device path instance

  @return A device node text or NULL if no more device node available

**/
static
CHAR16 *
GetNextDeviceNodeStr (
  IN OUT CHAR16   **DevicePath,
  OUT    BOOLEAN  *IsInstanceEnd
  )
{
  CHAR16  *Str;
  CHAR16  *ReturnStr;
  UINTN   ParenthesesStack;

  Str = *DevicePath;
  if (IS_NULL (*Str)) {
    return NULL;
  }

  //
  // Skip the leading '/', '(', ')' and ','
  //
  while (!IS_NULL (*Str)) {
    if (!IS_SLASH (*Str) &&
        !IS_COMMA (*Str) &&
        !IS_LEFT_PARENTH (*Str) &&
        !IS_RIGHT_PARENTH (*Str)) {
      break;
    }
    Str++;
  }

  ReturnStr = Str;

  //
  // Scan for the separator of this device node, '/' or ','
  //
  ParenthesesStack = 0;
  while (!IS_NULL (*Str)) {
    if ((IS_COMMA (*Str) || IS_SLASH (*Str)) && (ParenthesesStack == 0)) {
      break;
    }

    if (IS_LEFT_PARENTH (*Str)) {
      ParenthesesStack++;
    } else if (IS_RIGHT_PARENTH (*Str)) {
      ParenthesesStack--;
    }

    Str++;
  }

  if (ParenthesesStack != 0) {
    //
    // The '(' doesn't pair with ')', invalid device path text
    //
    return NULL;
  }

  if (IS_COMMA (*Str)) {
    *IsInstanceEnd = TRUE;
    *Str = '\0';
    Str++;
  } else {
    *IsInstanceEnd = FALSE;
    if (!IS_NULL (*Str)) {
      *Str = '\0';
      Str++;
    }
  }

  *DevicePath = Str;

  return ReturnStr;
}


#ifndef __FreeBSD__
/**
  Return whether the integer string is a hex string.

  @param Str             The integer string

  @retval TRUE   Hex string
  @retval FALSE  Decimal string

**/
static
BOOLEAN
IsHexStr (
  IN CHAR16   *Str
  )
{
  //
  // skip preceeding white space
  //
  while ((*Str != 0) && *Str == ' ') {
    Str ++;
  }
  //
  // skip preceeding zeros
  //
  while ((*Str != 0) && *Str == '0') {
    Str ++;
  }

  return (BOOLEAN) (*Str == 'x' || *Str == 'X');
}

/**

  Convert integer string to uint.

  @param Str             The integer string. If leading with "0x" or "0X", it's hexadecimal.

  @return A UINTN value represented by Str

**/
static
UINTN
Strtoi (
  IN CHAR16  *Str
  )
{
  if (IsHexStr (Str)) {
    return StrHexToUintn (Str);
  } else {
    return StrDecimalToUintn (Str);
  }
}

/**

  Convert integer string to 64 bit data.

  @param Str             The integer string. If leading with "0x" or "0X", it's hexadecimal.
  @param Data            A pointer to the UINT64 value represented by Str

**/
static
VOID
Strtoi64 (
  IN  CHAR16  *Str,
  OUT UINT64  *Data
  )
{
  if (IsHexStr (Str)) {
    *Data = StrHexToUint64 (Str);
  } else {
    *Data = StrDecimalToUint64 (Str);
  }
}
#endif

/**
  Converts a Unicode string to ASCII string.

  @param Str             The equivalent Unicode string
  @param AsciiStr        On input, it points to destination ASCII string buffer; on output, it points
                         to the next ASCII string next to it

**/
static
VOID
StrToAscii (
  IN     CHAR16 *Str,
  IN OUT CHAR8  **AsciiStr
  )
{
  CHAR8 *Dest;

  Dest = *AsciiStr;
  while (!IS_NULL (*Str)) {
    *(Dest++) = (CHAR8) *(Str++);
  }
  *Dest = 0;

  //
  // Return the string next to it
  //
  *AsciiStr = Dest + 1;
}

/**
  Converts a generic text device path node to device path structure.

  @param Type            The type of the device path node.
  @param TextDeviceNode  The input text device path node.

  @return A pointer to device path structure.
**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextGenericPath (
  IN UINT8  Type,
  IN CHAR16 *TextDeviceNode
  )
{
  EFI_DEVICE_PATH_PROTOCOL *Node;
  CHAR16                   *SubtypeStr;
  CHAR16                   *DataStr;
  UINTN                    DataLength;

  SubtypeStr = GetNextParamStr (&TextDeviceNode);
  DataStr    = GetNextParamStr (&TextDeviceNode);

  if (DataStr == NULL) {
    DataLength = 0;
  } else {
    DataLength = StrLen (DataStr) / 2;
  }
  Node = CreateDeviceNode (
           Type,
           (UINT8) Strtoi (SubtypeStr),
           (UINT16) (sizeof (EFI_DEVICE_PATH_PROTOCOL) + DataLength)
           );

  StrHexToBytes (DataStr, DataLength * 2, (UINT8 *) (Node + 1), DataLength);
  return Node;
}

/**
  Converts a generic text device path node to device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextPath (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                   *TypeStr;

  TypeStr    = GetNextParamStr (&TextDeviceNode);

  return DevPathFromTextGenericPath ((UINT8) Strtoi (TypeStr), TextDeviceNode);
}

/**
  Converts a generic hardware text device path node to Hardware device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to Hardware device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextHardwarePath (
  IN CHAR16 *TextDeviceNode
  )
{
  return DevPathFromTextGenericPath (HARDWARE_DEVICE_PATH, TextDeviceNode);
}

/**
  Converts a text device path node to Hardware PCI device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to Hardware PCI device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextPci (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16          *FunctionStr;
  CHAR16          *DeviceStr;
  PCI_DEVICE_PATH *Pci;

  DeviceStr   = GetNextParamStr (&TextDeviceNode);
  FunctionStr = GetNextParamStr (&TextDeviceNode);
  Pci         = (PCI_DEVICE_PATH *) CreateDeviceNode (
                                      HARDWARE_DEVICE_PATH,
                                      HW_PCI_DP,
                                      (UINT16) sizeof (PCI_DEVICE_PATH)
                                      );

  Pci->Function = (UINT8) Strtoi (FunctionStr);
  Pci->Device   = (UINT8) Strtoi (DeviceStr);

  return (EFI_DEVICE_PATH_PROTOCOL *) Pci;
}

/**
  Converts a text device path node to Hardware PC card device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to Hardware PC card device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextPcCard (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16              *FunctionNumberStr;
  PCCARD_DEVICE_PATH  *Pccard;

  FunctionNumberStr = GetNextParamStr (&TextDeviceNode);
  Pccard            = (PCCARD_DEVICE_PATH *) CreateDeviceNode (
                                               HARDWARE_DEVICE_PATH,
                                               HW_PCCARD_DP,
                                               (UINT16) sizeof (PCCARD_DEVICE_PATH)
                                               );

  Pccard->FunctionNumber  = (UINT8) Strtoi (FunctionNumberStr);

  return (EFI_DEVICE_PATH_PROTOCOL *) Pccard;
}

/**
  Converts a text device path node to Hardware memory map device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to Hardware memory map device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextMemoryMapped (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16              *MemoryTypeStr;
  CHAR16              *StartingAddressStr;
  CHAR16              *EndingAddressStr;
  MEMMAP_DEVICE_PATH  *MemMap;

  MemoryTypeStr      = GetNextParamStr (&TextDeviceNode);
  StartingAddressStr = GetNextParamStr (&TextDeviceNode);
  EndingAddressStr   = GetNextParamStr (&TextDeviceNode);
  MemMap             = (MEMMAP_DEVICE_PATH *) CreateDeviceNode (
                                               HARDWARE_DEVICE_PATH,
                                               HW_MEMMAP_DP,
                                               (UINT16) sizeof (MEMMAP_DEVICE_PATH)
                                               );

  MemMap->MemoryType = (UINT32) Strtoi (MemoryTypeStr);
  Strtoi64 (StartingAddressStr, &MemMap->StartingAddress);
  Strtoi64 (EndingAddressStr, &MemMap->EndingAddress);

  return (EFI_DEVICE_PATH_PROTOCOL *) MemMap;
}

/**
  Converts a text device path node to Vendor device path structure based on the input Type
  and SubType.

  @param TextDeviceNode  The input Text device path node.
  @param Type            The type of device path node.
  @param SubType         The subtype of device path node.

  @return A pointer to the newly-created Vendor device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
ConvertFromTextVendor (
  IN CHAR16 *TextDeviceNode,
  IN UINT8  Type,
  IN UINT8  SubType
  )
{
  CHAR16              *GuidStr;
  CHAR16              *DataStr;
  UINTN               Length;
  VENDOR_DEVICE_PATH  *Vendor;

  GuidStr = GetNextParamStr (&TextDeviceNode);

  DataStr = GetNextParamStr (&TextDeviceNode);
  Length  = StrLen (DataStr);
  //
  // Two hex characters make up 1 buffer byte
  //
  Length  = (Length + 1) / 2;

  Vendor  = (VENDOR_DEVICE_PATH *) CreateDeviceNode (
                                     Type,
                                     SubType,
                                     (UINT16) (sizeof (VENDOR_DEVICE_PATH) + Length)
                                     );

  StrToGuid (GuidStr, &Vendor->Guid);
  StrHexToBytes (DataStr, Length * 2, (UINT8 *) (Vendor + 1), Length);

  return (EFI_DEVICE_PATH_PROTOCOL *) Vendor;
}

/**
  Converts a text device path node to Vendor Hardware device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Vendor Hardware device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextVenHw (
  IN CHAR16 *TextDeviceNode
  )
{
  return ConvertFromTextVendor (
           TextDeviceNode,
           HARDWARE_DEVICE_PATH,
           HW_VENDOR_DP
           );
}

/**
  Converts a text device path node to Hardware Controller device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Hardware Controller device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextCtrl (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                  *ControllerStr;
  CONTROLLER_DEVICE_PATH  *Controller;

  ControllerStr = GetNextParamStr (&TextDeviceNode);
  Controller    = (CONTROLLER_DEVICE_PATH *) CreateDeviceNode (
                                               HARDWARE_DEVICE_PATH,
                                               HW_CONTROLLER_DP,
                                               (UINT16) sizeof (CONTROLLER_DEVICE_PATH)
                                               );
  Controller->ControllerNumber = (UINT32) Strtoi (ControllerStr);

  return (EFI_DEVICE_PATH_PROTOCOL *) Controller;
}

/**
  Converts a text device path node to BMC device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created BMC device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextBmc (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                *InterfaceTypeStr;
  CHAR16                *BaseAddressStr;
  BMC_DEVICE_PATH       *BmcDp;

  InterfaceTypeStr = GetNextParamStr (&TextDeviceNode);
  BaseAddressStr   = GetNextParamStr (&TextDeviceNode);
  BmcDp            = (BMC_DEVICE_PATH *) CreateDeviceNode (
                                           HARDWARE_DEVICE_PATH,
                                           HW_BMC_DP,
                                           (UINT16) sizeof (BMC_DEVICE_PATH)
                                           );

  BmcDp->InterfaceType = (UINT8) Strtoi (InterfaceTypeStr);
  WriteUnaligned64 (
    (UINT64 *) (&BmcDp->BaseAddress),
    StrHexToUint64 (BaseAddressStr)
    );

  return (EFI_DEVICE_PATH_PROTOCOL *) BmcDp;
}

/**
  Converts a generic ACPI text device path node to ACPI device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to ACPI device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextAcpiPath (
  IN CHAR16 *TextDeviceNode
  )
{
  return DevPathFromTextGenericPath (ACPI_DEVICE_PATH, TextDeviceNode);
}

/**
  Converts a string to EisaId.

  @param Text   The input string.

  @return UINT32 EISA ID.
**/
static
UINT32
EisaIdFromText (
  IN CHAR16 *Text
  )
{
  return (((Text[0] - 'A' + 1) & 0x1f) << 10)
       + (((Text[1] - 'A' + 1) & 0x1f) <<  5)
       + (((Text[2] - 'A' + 1) & 0x1f) <<  0)
       + (UINT32) (StrHexToUintn (&Text[3]) << 16)
       ;
}

/**
  Converts a text device path node to ACPI HID device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created ACPI HID device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextAcpi (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                *HIDStr;
  CHAR16                *UIDStr;
  ACPI_HID_DEVICE_PATH  *Acpi;

  HIDStr = GetNextParamStr (&TextDeviceNode);
  UIDStr = GetNextParamStr (&TextDeviceNode);
  Acpi   = (ACPI_HID_DEVICE_PATH *) CreateDeviceNode (
                                      ACPI_DEVICE_PATH,
                                      ACPI_DP,
                                      (UINT16) sizeof (ACPI_HID_DEVICE_PATH)
                                      );

  Acpi->HID = EisaIdFromText (HIDStr);
  Acpi->UID = (UINT32) Strtoi (UIDStr);

  return (EFI_DEVICE_PATH_PROTOCOL *) Acpi;
}

/**
  Converts a text device path node to ACPI HID device path structure.

  @param TextDeviceNode  The input Text device path node.
  @param PnPId           The input plug and play identification.

  @return A pointer to the newly-created ACPI HID device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
ConvertFromTextAcpi (
  IN CHAR16 *TextDeviceNode,
  IN UINT32  PnPId
  )
{
  CHAR16                *UIDStr;
  ACPI_HID_DEVICE_PATH  *Acpi;

  UIDStr = GetNextParamStr (&TextDeviceNode);
  Acpi   = (ACPI_HID_DEVICE_PATH *) CreateDeviceNode (
                                      ACPI_DEVICE_PATH,
                                      ACPI_DP,
                                      (UINT16) sizeof (ACPI_HID_DEVICE_PATH)
                                      );

  Acpi->HID = EFI_PNP_ID (PnPId);
  Acpi->UID = (UINT32) Strtoi (UIDStr);

  return (EFI_DEVICE_PATH_PROTOCOL *) Acpi;
}

/**
  Converts a text device path node to PCI root device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created PCI root device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextPciRoot (
  IN CHAR16 *TextDeviceNode
  )
{
  return ConvertFromTextAcpi (TextDeviceNode, 0x0a03);
}

/**
  Converts a text device path node to PCIE root device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created PCIE root device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextPcieRoot (
  IN CHAR16 *TextDeviceNode
  )
{
  return ConvertFromTextAcpi (TextDeviceNode, 0x0a08);
}

/**
  Converts a text device path node to Floppy device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Floppy device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextFloppy (
  IN CHAR16 *TextDeviceNode
  )
{
  return ConvertFromTextAcpi (TextDeviceNode, 0x0604);
}

/**
  Converts a text device path node to Keyboard device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created  Keyboard device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextKeyboard (
  IN CHAR16 *TextDeviceNode
  )
{
  return ConvertFromTextAcpi (TextDeviceNode, 0x0301);
}

/**
  Converts a text device path node to Serial device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Serial device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextSerial (
  IN CHAR16 *TextDeviceNode
  )
{
  return ConvertFromTextAcpi (TextDeviceNode, 0x0501);
}

/**
  Converts a text device path node to Parallel Port device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Parallel Port device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextParallelPort (
  IN CHAR16 *TextDeviceNode
  )
{
  return ConvertFromTextAcpi (TextDeviceNode, 0x0401);
}

/**
  Converts a text device path node to ACPI extension device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created ACPI extension device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextAcpiEx (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                         *HIDStr;
  CHAR16                         *CIDStr;
  CHAR16                         *UIDStr;
  CHAR16                         *HIDSTRStr;
  CHAR16                         *CIDSTRStr;
  CHAR16                         *UIDSTRStr;
  CHAR8                          *AsciiStr;
  UINT16                         Length;
  ACPI_EXTENDED_HID_DEVICE_PATH  *AcpiEx;

  HIDStr    = GetNextParamStr (&TextDeviceNode);
  CIDStr    = GetNextParamStr (&TextDeviceNode);
  UIDStr    = GetNextParamStr (&TextDeviceNode);
  HIDSTRStr = GetNextParamStr (&TextDeviceNode);
  CIDSTRStr = GetNextParamStr (&TextDeviceNode);
  UIDSTRStr = GetNextParamStr (&TextDeviceNode);

  Length    = (UINT16) (sizeof (ACPI_EXTENDED_HID_DEVICE_PATH) + StrLen (HIDSTRStr) + 1);
  Length    = (UINT16) (Length + StrLen (UIDSTRStr) + 1);
  Length    = (UINT16) (Length + StrLen (CIDSTRStr) + 1);
  AcpiEx = (ACPI_EXTENDED_HID_DEVICE_PATH *) CreateDeviceNode (
                                               ACPI_DEVICE_PATH,
                                               ACPI_EXTENDED_DP,
                                               Length
                                               );

  AcpiEx->HID = EisaIdFromText (HIDStr);
  AcpiEx->CID = EisaIdFromText (CIDStr);
  AcpiEx->UID = (UINT32) Strtoi (UIDStr);

  AsciiStr = (CHAR8 *) ((UINT8 *)AcpiEx + sizeof (ACPI_EXTENDED_HID_DEVICE_PATH));
  StrToAscii (HIDSTRStr, &AsciiStr);
  StrToAscii (UIDSTRStr, &AsciiStr);
  StrToAscii (CIDSTRStr, &AsciiStr);

  return (EFI_DEVICE_PATH_PROTOCOL *) AcpiEx;
}

/**
  Converts a text device path node to ACPI extension device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created ACPI extension device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextAcpiExp (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                         *HIDStr;
  CHAR16                         *CIDStr;
  CHAR16                         *UIDSTRStr;
  CHAR8                          *AsciiStr;
  UINT16                         Length;
  ACPI_EXTENDED_HID_DEVICE_PATH  *AcpiEx;

  HIDStr    = GetNextParamStr (&TextDeviceNode);
  CIDStr    = GetNextParamStr (&TextDeviceNode);
  UIDSTRStr = GetNextParamStr (&TextDeviceNode);
  Length    = (UINT16) (sizeof (ACPI_EXTENDED_HID_DEVICE_PATH) + StrLen (UIDSTRStr) + 3);
  AcpiEx    = (ACPI_EXTENDED_HID_DEVICE_PATH *) CreateDeviceNode (
                                                  ACPI_DEVICE_PATH,
                                                  ACPI_EXTENDED_DP,
                                                  Length
                                                  );

  AcpiEx->HID = EisaIdFromText (HIDStr);
  AcpiEx->CID = EisaIdFromText (CIDStr);
  AcpiEx->UID = 0;

  AsciiStr = (CHAR8 *) ((UINT8 *)AcpiEx + sizeof (ACPI_EXTENDED_HID_DEVICE_PATH));
  //
  // HID string is NULL
  //
  *AsciiStr = '\0';
  //
  // Convert UID string
  //
  AsciiStr++;
  StrToAscii (UIDSTRStr, &AsciiStr);
  //
  // CID string is NULL
  //
  *AsciiStr = '\0';

  return (EFI_DEVICE_PATH_PROTOCOL *) AcpiEx;
}

/**
  Converts a text device path node to ACPI _ADR device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created ACPI _ADR device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextAcpiAdr (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                *DisplayDeviceStr;
  ACPI_ADR_DEVICE_PATH  *AcpiAdr;
  UINTN                 Index;
  UINTN                 Length;

  AcpiAdr = (ACPI_ADR_DEVICE_PATH *) CreateDeviceNode (
                                       ACPI_DEVICE_PATH,
                                       ACPI_ADR_DP,
                                       (UINT16) sizeof (ACPI_ADR_DEVICE_PATH)
                                       );
  ASSERT (AcpiAdr != NULL);

  for (Index = 0; ; Index++) {
    DisplayDeviceStr = GetNextParamStr (&TextDeviceNode);
    if (IS_NULL (*DisplayDeviceStr)) {
      break;
    }
    if (Index > 0) {
      Length  = DevicePathNodeLength (AcpiAdr);
      AcpiAdr = ReallocatePool (
                  Length,
                  Length + sizeof (UINT32),
                  AcpiAdr
                  );
      ASSERT (AcpiAdr != NULL);
      SetDevicePathNodeLength (AcpiAdr, Length + sizeof (UINT32));
    }

    (&AcpiAdr->ADR)[Index] = (UINT32) Strtoi (DisplayDeviceStr);
  }

  return (EFI_DEVICE_PATH_PROTOCOL *) AcpiAdr;
}

/**
  Converts a generic messaging text device path node to messaging device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to messaging device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextMsg (
  IN CHAR16 *TextDeviceNode
  )
{
  return DevPathFromTextGenericPath (MESSAGING_DEVICE_PATH, TextDeviceNode);
}

/**
  Converts a text device path node to Parallel Port device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Parallel Port device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextAta (
IN CHAR16 *TextDeviceNode
)
{
  CHAR16            *PrimarySecondaryStr;
  CHAR16            *SlaveMasterStr;
  CHAR16            *LunStr;
  ATAPI_DEVICE_PATH *Atapi;

  Atapi = (ATAPI_DEVICE_PATH *) CreateDeviceNode (
    MESSAGING_DEVICE_PATH,
    MSG_ATAPI_DP,
    (UINT16) sizeof (ATAPI_DEVICE_PATH)
    );

  PrimarySecondaryStr = GetNextParamStr (&TextDeviceNode);
  SlaveMasterStr      = GetNextParamStr (&TextDeviceNode);
  LunStr              = GetNextParamStr (&TextDeviceNode);

  if (StrCmp (PrimarySecondaryStr, "Primary") == 0) {
    Atapi->PrimarySecondary = 0;
  } else if (StrCmp (PrimarySecondaryStr, "Secondary") == 0) {
    Atapi->PrimarySecondary = 1;
  } else {
    Atapi->PrimarySecondary = (UINT8) Strtoi (PrimarySecondaryStr);
  }
  if (StrCmp (SlaveMasterStr, "Master") == 0) {
    Atapi->SlaveMaster      = 0;
  } else if (StrCmp (SlaveMasterStr, "Slave") == 0) {
    Atapi->SlaveMaster      = 1;
  } else {
    Atapi->SlaveMaster      = (UINT8) Strtoi (SlaveMasterStr);
  }

  Atapi->Lun                = (UINT16) Strtoi (LunStr);

  return (EFI_DEVICE_PATH_PROTOCOL *) Atapi;
}

/**
  Converts a text device path node to SCSI device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created SCSI device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextScsi (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16            *PunStr;
  CHAR16            *LunStr;
  SCSI_DEVICE_PATH  *Scsi;

  PunStr = GetNextParamStr (&TextDeviceNode);
  LunStr = GetNextParamStr (&TextDeviceNode);
  Scsi   = (SCSI_DEVICE_PATH *) CreateDeviceNode (
                                   MESSAGING_DEVICE_PATH,
                                   MSG_SCSI_DP,
                                   (UINT16) sizeof (SCSI_DEVICE_PATH)
                                   );

  Scsi->Pun = (UINT16) Strtoi (PunStr);
  Scsi->Lun = (UINT16) Strtoi (LunStr);

  return (EFI_DEVICE_PATH_PROTOCOL *) Scsi;
}

/**
  Converts a text device path node to Fibre device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Fibre device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextFibre (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                    *WWNStr;
  CHAR16                    *LunStr;
  FIBRECHANNEL_DEVICE_PATH  *Fibre;

  WWNStr = GetNextParamStr (&TextDeviceNode);
  LunStr = GetNextParamStr (&TextDeviceNode);
  Fibre  = (FIBRECHANNEL_DEVICE_PATH *) CreateDeviceNode (
                                          MESSAGING_DEVICE_PATH,
                                          MSG_FIBRECHANNEL_DP,
                                          (UINT16) sizeof (FIBRECHANNEL_DEVICE_PATH)
                                          );

  Fibre->Reserved = 0;
  Strtoi64 (WWNStr, &Fibre->WWN);
  Strtoi64 (LunStr, &Fibre->Lun);

  return (EFI_DEVICE_PATH_PROTOCOL *) Fibre;
}

/**
  Converts a text device path node to FibreEx device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created FibreEx device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextFibreEx (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                      *WWNStr;
  CHAR16                      *LunStr;
  FIBRECHANNELEX_DEVICE_PATH  *FibreEx;

  WWNStr  = GetNextParamStr (&TextDeviceNode);
  LunStr  = GetNextParamStr (&TextDeviceNode);
  FibreEx = (FIBRECHANNELEX_DEVICE_PATH *) CreateDeviceNode (
                                             MESSAGING_DEVICE_PATH,
                                             MSG_FIBRECHANNELEX_DP,
                                             (UINT16) sizeof (FIBRECHANNELEX_DEVICE_PATH)
                                             );

  FibreEx->Reserved = 0;
  Strtoi64 (WWNStr, (UINT64 *) (&FibreEx->WWN));
  Strtoi64 (LunStr, (UINT64 *) (&FibreEx->Lun));

  *(UINT64 *) (&FibreEx->WWN) = SwapBytes64 (*(UINT64 *) (&FibreEx->WWN));
  *(UINT64 *) (&FibreEx->Lun) = SwapBytes64 (*(UINT64 *) (&FibreEx->Lun));

  return (EFI_DEVICE_PATH_PROTOCOL *) FibreEx;
}

/**
  Converts a text device path node to 1394 device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created 1394 device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromText1394 (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16            *GuidStr;
  F1394_DEVICE_PATH *F1394DevPath;

  GuidStr = GetNextParamStr (&TextDeviceNode);
  F1394DevPath  = (F1394_DEVICE_PATH *) CreateDeviceNode (
                                          MESSAGING_DEVICE_PATH,
                                          MSG_1394_DP,
                                          (UINT16) sizeof (F1394_DEVICE_PATH)
                                          );

  F1394DevPath->Reserved = 0;
  F1394DevPath->Guid     = StrHexToUint64 (GuidStr);

  return (EFI_DEVICE_PATH_PROTOCOL *) F1394DevPath;
}

/**
  Converts a text device path node to USB device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created USB device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextUsb (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16          *PortStr;
  CHAR16          *InterfaceStr;
  USB_DEVICE_PATH *Usb;

  PortStr               = GetNextParamStr (&TextDeviceNode);
  InterfaceStr          = GetNextParamStr (&TextDeviceNode);
  Usb                   = (USB_DEVICE_PATH *) CreateDeviceNode (
                                                MESSAGING_DEVICE_PATH,
                                                MSG_USB_DP,
                                                (UINT16) sizeof (USB_DEVICE_PATH)
                                                );

  Usb->ParentPortNumber = (UINT8) Strtoi (PortStr);
  Usb->InterfaceNumber  = (UINT8) Strtoi (InterfaceStr);

  return (EFI_DEVICE_PATH_PROTOCOL *) Usb;
}

/**
  Converts a text device path node to I20 device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created I20 device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextI2O (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16          *TIDStr;
  I2O_DEVICE_PATH *I2ODevPath;

  TIDStr     = GetNextParamStr (&TextDeviceNode);
  I2ODevPath = (I2O_DEVICE_PATH *) CreateDeviceNode (
                                    MESSAGING_DEVICE_PATH,
                                    MSG_I2O_DP,
                                    (UINT16) sizeof (I2O_DEVICE_PATH)
                                    );

  I2ODevPath->Tid  = (UINT32) Strtoi (TIDStr);

  return (EFI_DEVICE_PATH_PROTOCOL *) I2ODevPath;
}

/**
  Converts a text device path node to Infini Band device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Infini Band device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextInfiniband (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                  *FlagsStr;
  CHAR16                  *GuidStr;
  CHAR16                  *SidStr;
  CHAR16                  *TidStr;
  CHAR16                  *DidStr;
  INFINIBAND_DEVICE_PATH  *InfiniBand;

  FlagsStr   = GetNextParamStr (&TextDeviceNode);
  GuidStr    = GetNextParamStr (&TextDeviceNode);
  SidStr     = GetNextParamStr (&TextDeviceNode);
  TidStr     = GetNextParamStr (&TextDeviceNode);
  DidStr     = GetNextParamStr (&TextDeviceNode);
  InfiniBand = (INFINIBAND_DEVICE_PATH *) CreateDeviceNode (
                                            MESSAGING_DEVICE_PATH,
                                            MSG_INFINIBAND_DP,
                                            (UINT16) sizeof (INFINIBAND_DEVICE_PATH)
                                            );

  InfiniBand->ResourceFlags = (UINT32) Strtoi (FlagsStr);
  StrToGuid (GuidStr, (EFI_GUID *) InfiniBand->PortGid);
  Strtoi64 (SidStr, &InfiniBand->ServiceId);
  Strtoi64 (TidStr, &InfiniBand->TargetPortId);
  Strtoi64 (DidStr, &InfiniBand->DeviceId);

  return (EFI_DEVICE_PATH_PROTOCOL *) InfiniBand;
}

/**
  Converts a text device path node to Vendor-Defined Messaging device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Vendor-Defined Messaging device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextVenMsg (
  IN CHAR16 *TextDeviceNode
  )
{
  return ConvertFromTextVendor (
            TextDeviceNode,
            MESSAGING_DEVICE_PATH,
            MSG_VENDOR_DP
            );
}

/**
  Converts a text device path node to Vendor defined PC-ANSI device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Vendor defined PC-ANSI device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextVenPcAnsi (
  IN CHAR16 *TextDeviceNode
  )
{
  VENDOR_DEVICE_PATH  *Vendor;

  Vendor = (VENDOR_DEVICE_PATH *) CreateDeviceNode (
                                    MESSAGING_DEVICE_PATH,
                                    MSG_VENDOR_DP,
                                    (UINT16) sizeof (VENDOR_DEVICE_PATH));
  CopyGuid (&Vendor->Guid, &gEfiPcAnsiGuid);

  return (EFI_DEVICE_PATH_PROTOCOL *) Vendor;
}

/**
  Converts a text device path node to Vendor defined VT100 device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Vendor defined VT100 device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextVenVt100 (
  IN CHAR16 *TextDeviceNode
  )
{
  VENDOR_DEVICE_PATH  *Vendor;

  Vendor = (VENDOR_DEVICE_PATH *) CreateDeviceNode (
                                    MESSAGING_DEVICE_PATH,
                                    MSG_VENDOR_DP,
                                    (UINT16) sizeof (VENDOR_DEVICE_PATH));
  CopyGuid (&Vendor->Guid, &gEfiVT100Guid);

  return (EFI_DEVICE_PATH_PROTOCOL *) Vendor;
}

/**
  Converts a text device path node to Vendor defined VT100 Plus device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Vendor defined VT100 Plus device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextVenVt100Plus (
  IN CHAR16 *TextDeviceNode
  )
{
  VENDOR_DEVICE_PATH  *Vendor;

  Vendor = (VENDOR_DEVICE_PATH *) CreateDeviceNode (
                                    MESSAGING_DEVICE_PATH,
                                    MSG_VENDOR_DP,
                                    (UINT16) sizeof (VENDOR_DEVICE_PATH));
  CopyGuid (&Vendor->Guid, &gEfiVT100PlusGuid);

  return (EFI_DEVICE_PATH_PROTOCOL *) Vendor;
}

/**
  Converts a text device path node to Vendor defined UTF8 device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Vendor defined UTF8 device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextVenUtf8 (
  IN CHAR16 *TextDeviceNode
  )
{
  VENDOR_DEVICE_PATH  *Vendor;

  Vendor = (VENDOR_DEVICE_PATH *) CreateDeviceNode (
                                    MESSAGING_DEVICE_PATH,
                                    MSG_VENDOR_DP,
                                    (UINT16) sizeof (VENDOR_DEVICE_PATH));
  CopyGuid (&Vendor->Guid, &gEfiVTUTF8Guid);

  return (EFI_DEVICE_PATH_PROTOCOL *) Vendor;
}

/**
  Converts a text device path node to UART Flow Control device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created UART Flow Control device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextUartFlowCtrl (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                        *ValueStr;
  UART_FLOW_CONTROL_DEVICE_PATH *UartFlowControl;

  ValueStr        = GetNextParamStr (&TextDeviceNode);
  UartFlowControl = (UART_FLOW_CONTROL_DEVICE_PATH *) CreateDeviceNode (
                                                        MESSAGING_DEVICE_PATH,
                                                        MSG_VENDOR_DP,
                                                        (UINT16) sizeof (UART_FLOW_CONTROL_DEVICE_PATH)
                                                        );

  CopyGuid (&UartFlowControl->Guid, &gEfiUartDevicePathGuid);
  if (StrCmp (ValueStr, "XonXoff") == 0) {
    UartFlowControl->FlowControlMap = 2;
  } else if (StrCmp (ValueStr, "Hardware") == 0) {
    UartFlowControl->FlowControlMap = 1;
  } else {
    UartFlowControl->FlowControlMap = 0;
  }

  return (EFI_DEVICE_PATH_PROTOCOL *) UartFlowControl;
}

/**
  Converts a text device path node to Serial Attached SCSI device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Serial Attached SCSI device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextSAS (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16          *AddressStr;
  CHAR16          *LunStr;
  CHAR16          *RTPStr;
  CHAR16          *SASSATAStr;
  CHAR16          *LocationStr;
  CHAR16          *ConnectStr;
  CHAR16          *DriveBayStr;
  CHAR16          *ReservedStr;
  UINT16          Info;
  UINT16          Uint16;
  SAS_DEVICE_PATH *Sas;

  AddressStr  = GetNextParamStr (&TextDeviceNode);
  LunStr      = GetNextParamStr (&TextDeviceNode);
  RTPStr      = GetNextParamStr (&TextDeviceNode);
  SASSATAStr  = GetNextParamStr (&TextDeviceNode);
  LocationStr = GetNextParamStr (&TextDeviceNode);
  ConnectStr  = GetNextParamStr (&TextDeviceNode);
  DriveBayStr = GetNextParamStr (&TextDeviceNode);
  ReservedStr = GetNextParamStr (&TextDeviceNode);
  Sas         = (SAS_DEVICE_PATH *) CreateDeviceNode (
                                       MESSAGING_DEVICE_PATH,
                                       MSG_VENDOR_DP,
                                       (UINT16) sizeof (SAS_DEVICE_PATH)
                                       );

  CopyGuid (&Sas->Guid, &gEfiSasDevicePathGuid);
  Strtoi64 (AddressStr, &Sas->SasAddress);
  Strtoi64 (LunStr, &Sas->Lun);
  Sas->RelativeTargetPort = (UINT16) Strtoi (RTPStr);

  if (StrCmp (SASSATAStr, "NoTopology") == 0) {
    Info = 0x0;

  } else if ((StrCmp (SASSATAStr, "SATA") == 0) || (StrCmp (SASSATAStr, "SAS") == 0)) {

    Uint16 = (UINT16) Strtoi (DriveBayStr);
    if (Uint16 == 0) {
      Info = 0x1;
    } else {
      Info = (UINT16) (0x2 | ((Uint16 - 1) << 8));
    }

    if (StrCmp (SASSATAStr, "SATA") == 0) {
      Info |= BIT4;
    }

    //
    // Location is an integer between 0 and 1 or else
    // the keyword Internal (0) or External (1).
    //
    if (StrCmp (LocationStr, "External") == 0) {
      Uint16 = 1;
    } else if (StrCmp (LocationStr, "Internal") == 0) {
      Uint16 = 0;
    } else {
      Uint16 = ((UINT16) Strtoi (LocationStr) & BIT0);
    }
    Info |= (Uint16 << 5);

    //
    // Connect is an integer between 0 and 3 or else
    // the keyword Direct (0) or Expanded (1).
    //
    if (StrCmp (ConnectStr, "Expanded") == 0) {
      Uint16 = 1;
    } else if (StrCmp (ConnectStr, "Direct") == 0) {
      Uint16 = 0;
    } else {
      Uint16 = ((UINT16) Strtoi (ConnectStr) & (BIT0 | BIT1));
    }
    Info |= (Uint16 << 6);

  } else {
    Info = (UINT16) Strtoi (SASSATAStr);
  }

  Sas->DeviceTopology = Info;
  Sas->Reserved       = (UINT32) Strtoi (ReservedStr);

  return (EFI_DEVICE_PATH_PROTOCOL *) Sas;
}

/**
  Converts a text device path node to Serial Attached SCSI Ex device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Serial Attached SCSI Ex device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextSasEx (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16            *AddressStr;
  CHAR16            *LunStr;
  CHAR16            *RTPStr;
  CHAR16            *SASSATAStr;
  CHAR16            *LocationStr;
  CHAR16            *ConnectStr;
  CHAR16            *DriveBayStr;
  UINT16            Info;
  UINT16            Uint16;
  UINT64            SasAddress;
  UINT64            Lun;
  SASEX_DEVICE_PATH *SasEx;

  AddressStr  = GetNextParamStr (&TextDeviceNode);
  LunStr      = GetNextParamStr (&TextDeviceNode);
  RTPStr      = GetNextParamStr (&TextDeviceNode);
  SASSATAStr  = GetNextParamStr (&TextDeviceNode);
  LocationStr = GetNextParamStr (&TextDeviceNode);
  ConnectStr  = GetNextParamStr (&TextDeviceNode);
  DriveBayStr = GetNextParamStr (&TextDeviceNode);
  SasEx       = (SASEX_DEVICE_PATH *) CreateDeviceNode (
                                        MESSAGING_DEVICE_PATH,
                                        MSG_SASEX_DP,
                                        (UINT16) sizeof (SASEX_DEVICE_PATH)
                                        );

  Strtoi64 (AddressStr, &SasAddress);
  Strtoi64 (LunStr,     &Lun);
  WriteUnaligned64 ((UINT64 *) &SasEx->SasAddress, SwapBytes64 (SasAddress));
  WriteUnaligned64 ((UINT64 *) &SasEx->Lun,        SwapBytes64 (Lun));
  SasEx->RelativeTargetPort      = (UINT16) Strtoi (RTPStr);

  if (StrCmp (SASSATAStr, "NoTopology") == 0) {
    Info = 0x0;

  } else if ((StrCmp (SASSATAStr, "SATA") == 0) || (StrCmp (SASSATAStr, "SAS") == 0)) {

    Uint16 = (UINT16) Strtoi (DriveBayStr);
    if (Uint16 == 0) {
      Info = 0x1;
    } else {
      Info = (UINT16) (0x2 | ((Uint16 - 1) << 8));
    }

    if (StrCmp (SASSATAStr, "SATA") == 0) {
      Info |= BIT4;
    }

    //
    // Location is an integer between 0 and 1 or else
    // the keyword Internal (0) or External (1).
    //
    if (StrCmp (LocationStr, "External") == 0) {
      Uint16 = 1;
    } else if (StrCmp (LocationStr, "Internal") == 0) {
      Uint16 = 0;
    } else {
      Uint16 = ((UINT16) Strtoi (LocationStr) & BIT0);
    }
    Info |= (Uint16 << 5);

    //
    // Connect is an integer between 0 and 3 or else
    // the keyword Direct (0) or Expanded (1).
    //
    if (StrCmp (ConnectStr, "Expanded") == 0) {
      Uint16 = 1;
    } else if (StrCmp (ConnectStr, "Direct") == 0) {
      Uint16 = 0;
    } else {
      Uint16 = ((UINT16) Strtoi (ConnectStr) & (BIT0 | BIT1));
    }
    Info |= (Uint16 << 6);

  } else {
    Info = (UINT16) Strtoi (SASSATAStr);
  }

  SasEx->DeviceTopology = Info;

  return (EFI_DEVICE_PATH_PROTOCOL *) SasEx;
}

/**
  Converts a text device path node to NVM Express Namespace device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created NVM Express Namespace device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextNVMe (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                     *NamespaceIdStr;
  CHAR16                     *NamespaceUuidStr;
  NVME_NAMESPACE_DEVICE_PATH *Nvme;
  UINT8                      *Uuid;
  UINTN                      Index;

  NamespaceIdStr   = GetNextParamStr (&TextDeviceNode);
  NamespaceUuidStr = GetNextParamStr (&TextDeviceNode);
  Nvme = (NVME_NAMESPACE_DEVICE_PATH *) CreateDeviceNode (
    MESSAGING_DEVICE_PATH,
    MSG_NVME_NAMESPACE_DP,
    (UINT16) sizeof (NVME_NAMESPACE_DEVICE_PATH)
    );

  Nvme->NamespaceId = (UINT32) Strtoi (NamespaceIdStr);
  Uuid = (UINT8 *) &Nvme->NamespaceUuid;

  Index = sizeof (Nvme->NamespaceUuid) / sizeof (UINT8);
  while (Index-- != 0) {
    Uuid[Index] = (UINT8) StrHexToUintn (SplitStr (&NamespaceUuidStr, '-'));
  }

  return (EFI_DEVICE_PATH_PROTOCOL *) Nvme;
}

/**
  Converts a text device path node to UFS device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created UFS device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextUfs (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16            *PunStr;
  CHAR16            *LunStr;
  UFS_DEVICE_PATH   *Ufs;

  PunStr = GetNextParamStr (&TextDeviceNode);
  LunStr = GetNextParamStr (&TextDeviceNode);
  Ufs    = (UFS_DEVICE_PATH *) CreateDeviceNode (
                                 MESSAGING_DEVICE_PATH,
                                 MSG_UFS_DP,
                                 (UINT16) sizeof (UFS_DEVICE_PATH)
                                 );

  Ufs->Pun = (UINT8) Strtoi (PunStr);
  Ufs->Lun = (UINT8) Strtoi (LunStr);

  return (EFI_DEVICE_PATH_PROTOCOL *) Ufs;
}

/**
  Converts a text device path node to SD (Secure Digital) device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created SD device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextSd (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16            *SlotNumberStr;
  SD_DEVICE_PATH    *Sd;

  SlotNumberStr = GetNextParamStr (&TextDeviceNode);
  Sd            = (SD_DEVICE_PATH *) CreateDeviceNode (
                                       MESSAGING_DEVICE_PATH,
                                       MSG_SD_DP,
                                       (UINT16) sizeof (SD_DEVICE_PATH)
                                       );

  Sd->SlotNumber = (UINT8) Strtoi (SlotNumberStr);

  return (EFI_DEVICE_PATH_PROTOCOL *) Sd;
}

/**
  Converts a text device path node to EMMC (Embedded MMC) device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created EMMC device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextEmmc (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16            *SlotNumberStr;
  EMMC_DEVICE_PATH  *Emmc;

  SlotNumberStr = GetNextParamStr (&TextDeviceNode);
  Emmc          = (EMMC_DEVICE_PATH *) CreateDeviceNode (
                                       MESSAGING_DEVICE_PATH,
                                       MSG_EMMC_DP,
                                       (UINT16) sizeof (EMMC_DEVICE_PATH)
                                       );

  Emmc->SlotNumber = (UINT8) Strtoi (SlotNumberStr);

  return (EFI_DEVICE_PATH_PROTOCOL *) Emmc;
}

/**
  Converts a text device path node to Debug Port device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Debug Port device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextDebugPort (
  IN CHAR16 *TextDeviceNode
  )
{
  VENDOR_DEFINED_MESSAGING_DEVICE_PATH  *Vend;

  Vend = (VENDOR_DEFINED_MESSAGING_DEVICE_PATH *) CreateDeviceNode (
                                                    MESSAGING_DEVICE_PATH,
                                                    MSG_VENDOR_DP,
                                                    (UINT16) sizeof (VENDOR_DEFINED_MESSAGING_DEVICE_PATH)
                                                    );

  CopyGuid (&Vend->Guid, &gEfiDebugPortProtocolGuid);

  return (EFI_DEVICE_PATH_PROTOCOL *) Vend;
}

/**
  Converts a text device path node to MAC device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created MAC device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextMAC (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                *AddressStr;
  CHAR16                *IfTypeStr;
  UINTN                 Length;
  MAC_ADDR_DEVICE_PATH  *MACDevPath;

  AddressStr    = GetNextParamStr (&TextDeviceNode);
  IfTypeStr     = GetNextParamStr (&TextDeviceNode);
  MACDevPath    = (MAC_ADDR_DEVICE_PATH *) CreateDeviceNode (
                                              MESSAGING_DEVICE_PATH,
                                              MSG_MAC_ADDR_DP,
                                              (UINT16) sizeof (MAC_ADDR_DEVICE_PATH)
                                              );

  MACDevPath->IfType   = (UINT8) Strtoi (IfTypeStr);

  Length = sizeof (EFI_MAC_ADDRESS);
  StrHexToBytes (AddressStr, Length * 2, MACDevPath->MacAddress.Addr, Length);

  return (EFI_DEVICE_PATH_PROTOCOL *) MACDevPath;
}


/**
  Converts a text format to the network protocol ID.

  @param Text  String of protocol field.

  @return Network protocol ID .

**/
static
UINTN
NetworkProtocolFromText (
  IN CHAR16 *Text
  )
{
  if (StrCmp (Text, "UDP") == 0) {
    return RFC_1700_UDP_PROTOCOL;
  }

  if (StrCmp (Text, "TCP") == 0) {
    return RFC_1700_TCP_PROTOCOL;
  }

  return Strtoi (Text);
}


/**
  Converts a text device path node to IPV4 device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created IPV4 device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextIPv4 (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16            *RemoteIPStr;
  CHAR16            *ProtocolStr;
  CHAR16            *TypeStr;
  CHAR16            *LocalIPStr;
  CHAR16            *GatewayIPStr;
  CHAR16            *SubnetMaskStr;
  IPv4_DEVICE_PATH  *IPv4;

  RemoteIPStr           = GetNextParamStr (&TextDeviceNode);
  ProtocolStr           = GetNextParamStr (&TextDeviceNode);
  TypeStr               = GetNextParamStr (&TextDeviceNode);
  LocalIPStr            = GetNextParamStr (&TextDeviceNode);
  GatewayIPStr          = GetNextParamStr (&TextDeviceNode);
  SubnetMaskStr         = GetNextParamStr (&TextDeviceNode);
  IPv4                  = (IPv4_DEVICE_PATH *) CreateDeviceNode (
                                                 MESSAGING_DEVICE_PATH,
                                                 MSG_IPv4_DP,
                                                 (UINT16) sizeof (IPv4_DEVICE_PATH)
                                                 );

  StrToIpv4Address (RemoteIPStr, NULL, &IPv4->RemoteIpAddress, NULL);
  IPv4->Protocol = (UINT16) NetworkProtocolFromText (ProtocolStr);
  if (StrCmp (TypeStr, "Static") == 0) {
    IPv4->StaticIpAddress = TRUE;
  } else {
    IPv4->StaticIpAddress = FALSE;
  }

  StrToIpv4Address (LocalIPStr, NULL, &IPv4->LocalIpAddress, NULL);
  if (!IS_NULL (*GatewayIPStr) && !IS_NULL (*SubnetMaskStr)) {
    StrToIpv4Address (GatewayIPStr,  NULL, &IPv4->GatewayIpAddress, NULL);
    StrToIpv4Address (SubnetMaskStr, NULL, &IPv4->SubnetMask,       NULL);
  } else {
    ZeroMem (&IPv4->GatewayIpAddress, sizeof (IPv4->GatewayIpAddress));
    ZeroMem (&IPv4->SubnetMask,    sizeof (IPv4->SubnetMask));
  }

  IPv4->LocalPort       = 0;
  IPv4->RemotePort      = 0;

  return (EFI_DEVICE_PATH_PROTOCOL *) IPv4;
}

/**
  Converts a text device path node to IPV6 device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created IPV6 device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextIPv6 (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16            *RemoteIPStr;
  CHAR16            *ProtocolStr;
  CHAR16            *TypeStr;
  CHAR16            *LocalIPStr;
  CHAR16            *GatewayIPStr;
  CHAR16            *PrefixLengthStr;
  IPv6_DEVICE_PATH  *IPv6;

  RemoteIPStr           = GetNextParamStr (&TextDeviceNode);
  ProtocolStr           = GetNextParamStr (&TextDeviceNode);
  TypeStr               = GetNextParamStr (&TextDeviceNode);
  LocalIPStr            = GetNextParamStr (&TextDeviceNode);
  PrefixLengthStr       = GetNextParamStr (&TextDeviceNode);
  GatewayIPStr          = GetNextParamStr (&TextDeviceNode);
  IPv6                  = (IPv6_DEVICE_PATH *) CreateDeviceNode (
                                                 MESSAGING_DEVICE_PATH,
                                                 MSG_IPv6_DP,
                                                 (UINT16) sizeof (IPv6_DEVICE_PATH)
                                                 );

  StrToIpv6Address (RemoteIPStr, NULL, &IPv6->RemoteIpAddress, NULL);
  IPv6->Protocol        = (UINT16) NetworkProtocolFromText (ProtocolStr);
  if (StrCmp (TypeStr, "Static") == 0) {
    IPv6->IpAddressOrigin = 0;
  } else if (StrCmp (TypeStr, "StatelessAutoConfigure") == 0) {
    IPv6->IpAddressOrigin = 1;
  } else {
    IPv6->IpAddressOrigin = 2;
  }

  StrToIpv6Address (LocalIPStr, NULL, &IPv6->LocalIpAddress, NULL);
  if (!IS_NULL (*GatewayIPStr) && !IS_NULL (*PrefixLengthStr)) {
    StrToIpv6Address (GatewayIPStr, NULL, &IPv6->GatewayIpAddress, NULL);
    IPv6->PrefixLength = (UINT8) Strtoi (PrefixLengthStr);
  } else {
    ZeroMem (&IPv6->GatewayIpAddress, sizeof (IPv6->GatewayIpAddress));
    IPv6->PrefixLength = 0;
  }

  IPv6->LocalPort       = 0;
  IPv6->RemotePort      = 0;

  return (EFI_DEVICE_PATH_PROTOCOL *) IPv6;
}

/**
  Converts a text device path node to UART device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created UART device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextUart (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16            *BaudStr;
  CHAR16            *DataBitsStr;
  CHAR16            *ParityStr;
  CHAR16            *StopBitsStr;
  UART_DEVICE_PATH  *Uart;

  BaudStr         = GetNextParamStr (&TextDeviceNode);
  DataBitsStr     = GetNextParamStr (&TextDeviceNode);
  ParityStr       = GetNextParamStr (&TextDeviceNode);
  StopBitsStr     = GetNextParamStr (&TextDeviceNode);
  Uart            = (UART_DEVICE_PATH *) CreateDeviceNode (
                                           MESSAGING_DEVICE_PATH,
                                           MSG_UART_DP,
                                           (UINT16) sizeof (UART_DEVICE_PATH)
                                           );

  if (StrCmp (BaudStr, "DEFAULT") == 0) {
    Uart->BaudRate = 115200;
  } else {
    Strtoi64 (BaudStr, &Uart->BaudRate);
  }
  Uart->DataBits  = (UINT8) ((StrCmp (DataBitsStr, "DEFAULT") == 0) ? 8 : Strtoi (DataBitsStr));
  switch (*ParityStr) {
  case 'D':
    Uart->Parity = 0;
    break;

  case 'N':
    Uart->Parity = 1;
    break;

  case 'E':
    Uart->Parity = 2;
    break;

  case 'O':
    Uart->Parity = 3;
    break;

  case 'M':
    Uart->Parity = 4;
    break;

  case 'S':
    Uart->Parity = 5;
    break;

  default:
    Uart->Parity = (UINT8) Strtoi (ParityStr);
    break;
  }

  if (StrCmp (StopBitsStr, "D") == 0) {
    Uart->StopBits = (UINT8) 0;
  } else if (StrCmp (StopBitsStr, "1") == 0) {
    Uart->StopBits = (UINT8) 1;
  } else if (StrCmp (StopBitsStr, "1.5") == 0) {
    Uart->StopBits = (UINT8) 2;
  } else if (StrCmp (StopBitsStr, "2") == 0) {
    Uart->StopBits = (UINT8) 3;
  } else {
    Uart->StopBits = (UINT8) Strtoi (StopBitsStr);
  }

  return (EFI_DEVICE_PATH_PROTOCOL *) Uart;
}

/**
  Converts a text device path node to USB class device path structure.

  @param TextDeviceNode  The input Text device path node.
  @param UsbClassText    A pointer to USB_CLASS_TEXT structure to be integrated to USB Class Text.

  @return A pointer to the newly-created USB class device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
ConvertFromTextUsbClass (
  IN CHAR16         *TextDeviceNode,
  IN USB_CLASS_TEXT *UsbClassText
  )
{
  CHAR16                *VIDStr;
  CHAR16                *PIDStr;
  CHAR16                *ClassStr;
  CHAR16                *SubClassStr;
  CHAR16                *ProtocolStr;
  USB_CLASS_DEVICE_PATH *UsbClass;

  UsbClass    = (USB_CLASS_DEVICE_PATH *) CreateDeviceNode (
                                            MESSAGING_DEVICE_PATH,
                                            MSG_USB_CLASS_DP,
                                            (UINT16) sizeof (USB_CLASS_DEVICE_PATH)
                                            );

  VIDStr      = GetNextParamStr (&TextDeviceNode);
  PIDStr      = GetNextParamStr (&TextDeviceNode);
  if (UsbClassText->ClassExist) {
    ClassStr = GetNextParamStr (&TextDeviceNode);
    UsbClass->DeviceClass = (UINT8) Strtoi (ClassStr);
  } else {
    UsbClass->DeviceClass = UsbClassText->Class;
  }
  if (UsbClassText->SubClassExist) {
    SubClassStr = GetNextParamStr (&TextDeviceNode);
    UsbClass->DeviceSubClass = (UINT8) Strtoi (SubClassStr);
  } else {
    UsbClass->DeviceSubClass = UsbClassText->SubClass;
  }

  ProtocolStr = GetNextParamStr (&TextDeviceNode);

  UsbClass->VendorId        = (UINT16) Strtoi (VIDStr);
  UsbClass->ProductId       = (UINT16) Strtoi (PIDStr);
  UsbClass->DeviceProtocol  = (UINT8) Strtoi (ProtocolStr);

  return (EFI_DEVICE_PATH_PROTOCOL *) UsbClass;
}


/**
  Converts a text device path node to USB class device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created USB class device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextUsbClass (
  IN CHAR16 *TextDeviceNode
  )
{
  USB_CLASS_TEXT  UsbClassText;

  UsbClassText.ClassExist    = TRUE;
  UsbClassText.SubClassExist = TRUE;

  return ConvertFromTextUsbClass (TextDeviceNode, &UsbClassText);
}

/**
  Converts a text device path node to USB audio device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created USB audio device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextUsbAudio (
  IN CHAR16 *TextDeviceNode
  )
{
  USB_CLASS_TEXT  UsbClassText;

  UsbClassText.ClassExist    = FALSE;
  UsbClassText.Class         = USB_CLASS_AUDIO;
  UsbClassText.SubClassExist = TRUE;

  return ConvertFromTextUsbClass (TextDeviceNode, &UsbClassText);
}

/**
  Converts a text device path node to USB CDC Control device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created USB CDC Control device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextUsbCDCControl (
  IN CHAR16 *TextDeviceNode
  )
{
  USB_CLASS_TEXT  UsbClassText;

  UsbClassText.ClassExist    = FALSE;
  UsbClassText.Class         = USB_CLASS_CDCCONTROL;
  UsbClassText.SubClassExist = TRUE;

  return ConvertFromTextUsbClass (TextDeviceNode, &UsbClassText);
}

/**
  Converts a text device path node to USB HID device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created USB HID device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextUsbHID (
  IN CHAR16 *TextDeviceNode
  )
{
  USB_CLASS_TEXT  UsbClassText;

  UsbClassText.ClassExist    = FALSE;
  UsbClassText.Class         = USB_CLASS_HID;
  UsbClassText.SubClassExist = TRUE;

  return ConvertFromTextUsbClass (TextDeviceNode, &UsbClassText);
}

/**
  Converts a text device path node to USB Image device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created USB Image device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextUsbImage (
  IN CHAR16 *TextDeviceNode
  )
{
  USB_CLASS_TEXT  UsbClassText;

  UsbClassText.ClassExist    = FALSE;
  UsbClassText.Class         = USB_CLASS_IMAGE;
  UsbClassText.SubClassExist = TRUE;

  return ConvertFromTextUsbClass (TextDeviceNode, &UsbClassText);
}

/**
  Converts a text device path node to USB Print device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created USB Print device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextUsbPrinter (
  IN CHAR16 *TextDeviceNode
  )
{
  USB_CLASS_TEXT  UsbClassText;

  UsbClassText.ClassExist    = FALSE;
  UsbClassText.Class         = USB_CLASS_PRINTER;
  UsbClassText.SubClassExist = TRUE;

  return ConvertFromTextUsbClass (TextDeviceNode, &UsbClassText);
}

/**
  Converts a text device path node to USB mass storage device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created USB mass storage device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextUsbMassStorage (
  IN CHAR16 *TextDeviceNode
  )
{
  USB_CLASS_TEXT  UsbClassText;

  UsbClassText.ClassExist    = FALSE;
  UsbClassText.Class         = USB_CLASS_MASS_STORAGE;
  UsbClassText.SubClassExist = TRUE;

  return ConvertFromTextUsbClass (TextDeviceNode, &UsbClassText);
}

/**
  Converts a text device path node to USB HUB device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created USB HUB device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextUsbHub (
  IN CHAR16 *TextDeviceNode
  )
{
  USB_CLASS_TEXT  UsbClassText;

  UsbClassText.ClassExist    = FALSE;
  UsbClassText.Class         = USB_CLASS_HUB;
  UsbClassText.SubClassExist = TRUE;

  return ConvertFromTextUsbClass (TextDeviceNode, &UsbClassText);
}

/**
  Converts a text device path node to USB CDC data device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created USB CDC data device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextUsbCDCData (
  IN CHAR16 *TextDeviceNode
  )
{
  USB_CLASS_TEXT  UsbClassText;

  UsbClassText.ClassExist    = FALSE;
  UsbClassText.Class         = USB_CLASS_CDCDATA;
  UsbClassText.SubClassExist = TRUE;

  return ConvertFromTextUsbClass (TextDeviceNode, &UsbClassText);
}

/**
  Converts a text device path node to USB smart card device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created USB smart card device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextUsbSmartCard (
  IN CHAR16 *TextDeviceNode
  )
{
  USB_CLASS_TEXT  UsbClassText;

  UsbClassText.ClassExist    = FALSE;
  UsbClassText.Class         = USB_CLASS_SMART_CARD;
  UsbClassText.SubClassExist = TRUE;

  return ConvertFromTextUsbClass (TextDeviceNode, &UsbClassText);
}

/**
  Converts a text device path node to USB video device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created USB video device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextUsbVideo (
  IN CHAR16 *TextDeviceNode
  )
{
  USB_CLASS_TEXT  UsbClassText;

  UsbClassText.ClassExist    = FALSE;
  UsbClassText.Class         = USB_CLASS_VIDEO;
  UsbClassText.SubClassExist = TRUE;

  return ConvertFromTextUsbClass (TextDeviceNode, &UsbClassText);
}

/**
  Converts a text device path node to USB diagnostic device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created USB diagnostic device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextUsbDiagnostic (
  IN CHAR16 *TextDeviceNode
  )
{
  USB_CLASS_TEXT  UsbClassText;

  UsbClassText.ClassExist    = FALSE;
  UsbClassText.Class         = USB_CLASS_DIAGNOSTIC;
  UsbClassText.SubClassExist = TRUE;

  return ConvertFromTextUsbClass (TextDeviceNode, &UsbClassText);
}

/**
  Converts a text device path node to USB wireless device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created USB wireless device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextUsbWireless (
  IN CHAR16 *TextDeviceNode
  )
{
  USB_CLASS_TEXT  UsbClassText;

  UsbClassText.ClassExist    = FALSE;
  UsbClassText.Class         = USB_CLASS_WIRELESS;
  UsbClassText.SubClassExist = TRUE;

  return ConvertFromTextUsbClass (TextDeviceNode, &UsbClassText);
}

/**
  Converts a text device path node to USB device firmware update device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created USB device firmware update device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextUsbDeviceFirmwareUpdate (
  IN CHAR16 *TextDeviceNode
  )
{
  USB_CLASS_TEXT  UsbClassText;

  UsbClassText.ClassExist    = FALSE;
  UsbClassText.Class         = USB_CLASS_RESERVE;
  UsbClassText.SubClassExist = FALSE;
  UsbClassText.SubClass      = USB_SUBCLASS_FW_UPDATE;

  return ConvertFromTextUsbClass (TextDeviceNode, &UsbClassText);
}

/**
  Converts a text device path node to USB IRDA bridge device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created USB IRDA bridge device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextUsbIrdaBridge (
  IN CHAR16 *TextDeviceNode
  )
{
  USB_CLASS_TEXT  UsbClassText;

  UsbClassText.ClassExist    = FALSE;
  UsbClassText.Class         = USB_CLASS_RESERVE;
  UsbClassText.SubClassExist = FALSE;
  UsbClassText.SubClass      = USB_SUBCLASS_IRDA_BRIDGE;

  return ConvertFromTextUsbClass (TextDeviceNode, &UsbClassText);
}

/**
  Converts a text device path node to USB text and measurement device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created USB text and measurement device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextUsbTestAndMeasurement (
  IN CHAR16 *TextDeviceNode
  )
{
  USB_CLASS_TEXT  UsbClassText;

  UsbClassText.ClassExist    = FALSE;
  UsbClassText.Class         = USB_CLASS_RESERVE;
  UsbClassText.SubClassExist = FALSE;
  UsbClassText.SubClass      = USB_SUBCLASS_TEST;

  return ConvertFromTextUsbClass (TextDeviceNode, &UsbClassText);
}

/**
  Converts a text device path node to USB WWID device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created USB WWID device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextUsbWwid (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                *VIDStr;
  CHAR16                *PIDStr;
  CHAR16                *InterfaceNumStr;
  CHAR16                *SerialNumberStr;
  USB_WWID_DEVICE_PATH  *UsbWwid;
  UINTN                 SerialNumberStrLen;

  VIDStr                   = GetNextParamStr (&TextDeviceNode);
  PIDStr                   = GetNextParamStr (&TextDeviceNode);
  InterfaceNumStr          = GetNextParamStr (&TextDeviceNode);
  SerialNumberStr          = GetNextParamStr (&TextDeviceNode);
  SerialNumberStrLen       = StrLen (SerialNumberStr);
  if (SerialNumberStrLen >= 2 &&
      SerialNumberStr[0] == '\"' &&
      SerialNumberStr[SerialNumberStrLen - 1] == '\"'
    ) {
    SerialNumberStr[SerialNumberStrLen - 1] = '\0';
    SerialNumberStr++;
    SerialNumberStrLen -= 2;
  }
  UsbWwid                  = (USB_WWID_DEVICE_PATH *) CreateDeviceNode (
                                                         MESSAGING_DEVICE_PATH,
                                                         MSG_USB_WWID_DP,
                                                         (UINT16) (sizeof (USB_WWID_DEVICE_PATH) + SerialNumberStrLen * sizeof (CHAR16))
                                                         );
  UsbWwid->VendorId        = (UINT16) Strtoi (VIDStr);
  UsbWwid->ProductId       = (UINT16) Strtoi (PIDStr);
  UsbWwid->InterfaceNumber = (UINT16) Strtoi (InterfaceNumStr);

  //
  // There is no memory allocated in UsbWwid for the '\0' in SerialNumberStr.
  // Therefore, the '\0' will not be copied.
  //
  CopyMem (
    (UINT8 *) UsbWwid + sizeof (USB_WWID_DEVICE_PATH),
    SerialNumberStr,
    SerialNumberStrLen * sizeof (CHAR16)
    );

  return (EFI_DEVICE_PATH_PROTOCOL *) UsbWwid;
}

/**
  Converts a text device path node to Logic Unit device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Logic Unit device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextUnit (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                          *LunStr;
  DEVICE_LOGICAL_UNIT_DEVICE_PATH *LogicalUnit;

  LunStr      = GetNextParamStr (&TextDeviceNode);
  LogicalUnit = (DEVICE_LOGICAL_UNIT_DEVICE_PATH *) CreateDeviceNode (
                                                      MESSAGING_DEVICE_PATH,
                                                      MSG_DEVICE_LOGICAL_UNIT_DP,
                                                      (UINT16) sizeof (DEVICE_LOGICAL_UNIT_DEVICE_PATH)
                                                      );

  LogicalUnit->Lun  = (UINT8) Strtoi (LunStr);

  return (EFI_DEVICE_PATH_PROTOCOL *) LogicalUnit;
}

/**
  Converts a text device path node to iSCSI device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created iSCSI device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextiSCSI (
  IN CHAR16 *TextDeviceNode
  )
{
  UINT16                      Options;
  CHAR16                      *NameStr;
  CHAR16                      *PortalGroupStr;
  CHAR16                      *LunStr;
  CHAR16                      *HeaderDigestStr;
  CHAR16                      *DataDigestStr;
  CHAR16                      *AuthenticationStr;
  CHAR16                      *ProtocolStr;
  CHAR8                       *AsciiStr;
  ISCSI_DEVICE_PATH_WITH_NAME *ISCSIDevPath;

  NameStr           = GetNextParamStr (&TextDeviceNode);
  PortalGroupStr    = GetNextParamStr (&TextDeviceNode);
  LunStr            = GetNextParamStr (&TextDeviceNode);
  HeaderDigestStr   = GetNextParamStr (&TextDeviceNode);
  DataDigestStr     = GetNextParamStr (&TextDeviceNode);
  AuthenticationStr = GetNextParamStr (&TextDeviceNode);
  ProtocolStr       = GetNextParamStr (&TextDeviceNode);
  ISCSIDevPath      = (ISCSI_DEVICE_PATH_WITH_NAME *) CreateDeviceNode (
                                                        MESSAGING_DEVICE_PATH,
                                                        MSG_ISCSI_DP,
                                                        (UINT16) (sizeof (ISCSI_DEVICE_PATH_WITH_NAME) + StrLen (NameStr))
                                                        );

  AsciiStr = ISCSIDevPath->TargetName;
  StrToAscii (NameStr, &AsciiStr);

  ISCSIDevPath->TargetPortalGroupTag = (UINT16) Strtoi (PortalGroupStr);
  Strtoi64 (LunStr, &ISCSIDevPath->Lun);

  Options = 0x0000;
  if (StrCmp (HeaderDigestStr, "CRC32C") == 0) {
    Options |= 0x0002;
  }

  if (StrCmp (DataDigestStr, "CRC32C") == 0) {
    Options |= 0x0008;
  }

  if (StrCmp (AuthenticationStr, "None") == 0) {
    Options |= 0x0800;
  }

  if (StrCmp (AuthenticationStr, "CHAP_UNI") == 0) {
    Options |= 0x1000;
  }

  ISCSIDevPath->LoginOption      = (UINT16) Options;

  ISCSIDevPath->NetworkProtocol  = (UINT16) StrCmp (ProtocolStr, "TCP");

  return (EFI_DEVICE_PATH_PROTOCOL *) ISCSIDevPath;
}

/**
  Converts a text device path node to VLAN device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created VLAN device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextVlan (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16            *VlanStr;
  VLAN_DEVICE_PATH  *Vlan;

  VlanStr = GetNextParamStr (&TextDeviceNode);
  Vlan    = (VLAN_DEVICE_PATH *) CreateDeviceNode (
                                   MESSAGING_DEVICE_PATH,
                                   MSG_VLAN_DP,
                                   (UINT16) sizeof (VLAN_DEVICE_PATH)
                                   );

  Vlan->VlanId = (UINT16) Strtoi (VlanStr);

  return (EFI_DEVICE_PATH_PROTOCOL *) Vlan;
}

/**
  Converts a text device path node to Bluetooth device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Bluetooth device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextBluetooth (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                  *BluetoothStr;
  CHAR16                  *Walker;
  CHAR16                  *TempNumBuffer;
  UINTN                   TempBufferSize;
  INT32                   Index;
  BLUETOOTH_DEVICE_PATH   *BluetoothDp;

  BluetoothStr = GetNextParamStr (&TextDeviceNode);
  BluetoothDp = (BLUETOOTH_DEVICE_PATH *) CreateDeviceNode (
                                   MESSAGING_DEVICE_PATH,
                                   MSG_BLUETOOTH_DP,
                                   (UINT16) sizeof (BLUETOOTH_DEVICE_PATH)
                                   );

  Index = sizeof (BLUETOOTH_ADDRESS) - 1;
  Walker = BluetoothStr;
  while (!IS_NULL(*Walker) && Index >= 0) {
    TempBufferSize = 2 * sizeof(CHAR16) + StrSize("0x");
    TempNumBuffer = AllocateZeroPool (TempBufferSize);
    if (TempNumBuffer == NULL) {
      break;
    }
    StrCpyS (TempNumBuffer, TempBufferSize / sizeof (CHAR16), "0x");
    StrnCatS (TempNumBuffer, TempBufferSize / sizeof (CHAR16), Walker, 2);
    BluetoothDp->BD_ADDR.Address[Index] = (UINT8)Strtoi (TempNumBuffer);
    FreePool (TempNumBuffer);
    Walker += 2;
    Index--;
  }

  return (EFI_DEVICE_PATH_PROTOCOL *) BluetoothDp;
}

/**
  Converts a text device path node to Wi-Fi device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Wi-Fi device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextWiFi (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                *SSIdStr;
  CHAR8                 AsciiStr[33];
  UINTN                 DataLen;
  WIFI_DEVICE_PATH      *WiFiDp;

  SSIdStr = GetNextParamStr (&TextDeviceNode);
  WiFiDp  = (WIFI_DEVICE_PATH *) CreateDeviceNode (
                                   MESSAGING_DEVICE_PATH,
                                   MSG_WIFI_DP,
                                   (UINT16) sizeof (WIFI_DEVICE_PATH)
                                   );

  if (NULL != SSIdStr) {
    DataLen = StrLen (SSIdStr);
    if (StrLen (SSIdStr) > 32) {
      SSIdStr[32] = '\0';
      DataLen     = 32;
    }

    UnicodeStrToAsciiStrS (SSIdStr, AsciiStr, sizeof (AsciiStr));
    CopyMem (WiFiDp->SSId, AsciiStr, DataLen);
  }

  return (EFI_DEVICE_PATH_PROTOCOL *) WiFiDp;
}

/**
  Converts a text device path node to URI device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created URI device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextUri (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16           *UriStr;
  UINTN            UriLength;
  URI_DEVICE_PATH  *Uri;

  UriStr = GetNextParamStr (&TextDeviceNode);
  UriLength = StrnLenS (UriStr, MAX_UINT16 - sizeof (URI_DEVICE_PATH));
  Uri    = (URI_DEVICE_PATH *) CreateDeviceNode (
                                 MESSAGING_DEVICE_PATH,
                                 MSG_URI_DP,
                                 (UINT16) (sizeof (URI_DEVICE_PATH) + UriLength)
                                 );

  while (UriLength-- != 0) {
    Uri->Uri[UriLength] = (CHAR8) UriStr[UriLength];
  }

  return (EFI_DEVICE_PATH_PROTOCOL *) Uri;
}

/**
  Converts a media text device path node to media device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to media device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextMediaPath (
  IN CHAR16 *TextDeviceNode
  )
{
  return DevPathFromTextGenericPath (MEDIA_DEVICE_PATH, TextDeviceNode);
}

/**
  Converts a text device path node to HD device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created HD device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextHD (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                *PartitionStr;
  CHAR16                *TypeStr;
  CHAR16                *SignatureStr;
  CHAR16                *StartStr;
  CHAR16                *SizeStr;
  UINT32                Signature32;
  HARDDRIVE_DEVICE_PATH *Hd;

  PartitionStr        = GetNextParamStr (&TextDeviceNode);
  TypeStr             = GetNextParamStr (&TextDeviceNode);
  SignatureStr        = GetNextParamStr (&TextDeviceNode);
  StartStr            = GetNextParamStr (&TextDeviceNode);
  SizeStr             = GetNextParamStr (&TextDeviceNode);
  Hd                  = (HARDDRIVE_DEVICE_PATH *) CreateDeviceNode (
                                                    MEDIA_DEVICE_PATH,
                                                    MEDIA_HARDDRIVE_DP,
                                                    (UINT16) sizeof (HARDDRIVE_DEVICE_PATH)
                                                    );

  Hd->PartitionNumber = (UINT32) Strtoi (PartitionStr);

  ZeroMem (Hd->Signature, 16);
  Hd->MBRType = (UINT8) 0;

  if (StrCmp (TypeStr, "MBR") == 0) {
    Hd->SignatureType = SIGNATURE_TYPE_MBR;
    Hd->MBRType       = 0x01;

    Signature32       = (UINT32) Strtoi (SignatureStr);
    CopyMem (Hd->Signature, &Signature32, sizeof (UINT32));
  } else if (StrCmp (TypeStr, "GPT") == 0) {
    Hd->SignatureType = SIGNATURE_TYPE_GUID;
    Hd->MBRType       = 0x02;

    StrToGuid (SignatureStr, (EFI_GUID *) Hd->Signature);
  } else {
    Hd->SignatureType = (UINT8) Strtoi (TypeStr);
  }

  Strtoi64 (StartStr, &Hd->PartitionStart);
  Strtoi64 (SizeStr, &Hd->PartitionSize);

  return (EFI_DEVICE_PATH_PROTOCOL *) Hd;
}

/**
  Converts a text device path node to CDROM device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created CDROM device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextCDROM (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16            *EntryStr;
  CHAR16            *StartStr;
  CHAR16            *SizeStr;
  CDROM_DEVICE_PATH *CDROMDevPath;

  EntryStr              = GetNextParamStr (&TextDeviceNode);
  StartStr              = GetNextParamStr (&TextDeviceNode);
  SizeStr               = GetNextParamStr (&TextDeviceNode);
  CDROMDevPath          = (CDROM_DEVICE_PATH *) CreateDeviceNode (
                                                  MEDIA_DEVICE_PATH,
                                                  MEDIA_CDROM_DP,
                                                  (UINT16) sizeof (CDROM_DEVICE_PATH)
                                                  );

  CDROMDevPath->BootEntry = (UINT32) Strtoi (EntryStr);
  Strtoi64 (StartStr, &CDROMDevPath->PartitionStart);
  Strtoi64 (SizeStr, &CDROMDevPath->PartitionSize);

  return (EFI_DEVICE_PATH_PROTOCOL *) CDROMDevPath;
}

/**
  Converts a text device path node to Vendor-defined media device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Vendor-defined media device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextVenMedia (
  IN CHAR16 *TextDeviceNode
  )
{
  return ConvertFromTextVendor (
           TextDeviceNode,
           MEDIA_DEVICE_PATH,
           MEDIA_VENDOR_DP
           );
}

/**
  Converts a text device path node to File device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created File device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextFilePath (
  IN CHAR16 *TextDeviceNode
  )
{
  FILEPATH_DEVICE_PATH  *File;

#ifndef __FreeBSD__
  File = (FILEPATH_DEVICE_PATH *) CreateDeviceNode (
                                    MEDIA_DEVICE_PATH,
                                    MEDIA_FILEPATH_DP,
                                    (UINT16) (sizeof (FILEPATH_DEVICE_PATH) + StrLen (TextDeviceNode) * 2)
                                    );

  StrCpyS (File->PathName, StrLen (TextDeviceNode) + 1, TextDeviceNode);
#else
  size_t len = (sizeof (FILEPATH_DEVICE_PATH) + StrLen (TextDeviceNode) * 2);
  efi_char * v;
  File = (FILEPATH_DEVICE_PATH *) CreateDeviceNode (
                                    MEDIA_DEVICE_PATH,
                                    MEDIA_FILEPATH_DP,
				    (UINT16)len
                                    );
  v = File->PathName;
  utf8_to_ucs2(TextDeviceNode, &v, &len);
#endif

  return (EFI_DEVICE_PATH_PROTOCOL *) File;
}

/**
  Converts a text device path node to Media protocol device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Media protocol device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextMedia (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                      *GuidStr;
  MEDIA_PROTOCOL_DEVICE_PATH  *Media;

  GuidStr = GetNextParamStr (&TextDeviceNode);
  Media   = (MEDIA_PROTOCOL_DEVICE_PATH *) CreateDeviceNode (
                                             MEDIA_DEVICE_PATH,
                                             MEDIA_PROTOCOL_DP,
                                             (UINT16) sizeof (MEDIA_PROTOCOL_DEVICE_PATH)
                                             );

  StrToGuid (GuidStr, &Media->Protocol);

  return (EFI_DEVICE_PATH_PROTOCOL *) Media;
}

/**
  Converts a text device path node to firmware volume device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created firmware volume device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextFv (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                    *GuidStr;
  MEDIA_FW_VOL_DEVICE_PATH  *Fv;

  GuidStr = GetNextParamStr (&TextDeviceNode);
  Fv      = (MEDIA_FW_VOL_DEVICE_PATH *) CreateDeviceNode (
                                           MEDIA_DEVICE_PATH,
                                           MEDIA_PIWG_FW_VOL_DP,
                                           (UINT16) sizeof (MEDIA_FW_VOL_DEVICE_PATH)
                                           );

  StrToGuid (GuidStr, &Fv->FvName);

  return (EFI_DEVICE_PATH_PROTOCOL *) Fv;
}

/**
  Converts a text device path node to firmware file device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created firmware file device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextFvFile (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                             *GuidStr;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH  *FvFile;

  GuidStr = GetNextParamStr (&TextDeviceNode);
  FvFile  = (MEDIA_FW_VOL_FILEPATH_DEVICE_PATH *) CreateDeviceNode (
                                                    MEDIA_DEVICE_PATH,
                                                    MEDIA_PIWG_FW_FILE_DP,
                                                    (UINT16) sizeof (MEDIA_FW_VOL_FILEPATH_DEVICE_PATH)
                                                    );

  StrToGuid (GuidStr, &FvFile->FvFileName);

  return (EFI_DEVICE_PATH_PROTOCOL *) FvFile;
}

/**
  Converts a text device path node to text relative offset device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Text device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextRelativeOffsetRange (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                                  *StartingOffsetStr;
  CHAR16                                  *EndingOffsetStr;
  MEDIA_RELATIVE_OFFSET_RANGE_DEVICE_PATH *Offset;

  StartingOffsetStr = GetNextParamStr (&TextDeviceNode);
  EndingOffsetStr   = GetNextParamStr (&TextDeviceNode);
  Offset            = (MEDIA_RELATIVE_OFFSET_RANGE_DEVICE_PATH *) CreateDeviceNode (
                                                                    MEDIA_DEVICE_PATH,
                                                                    MEDIA_RELATIVE_OFFSET_RANGE_DP,
                                                                    (UINT16) sizeof (MEDIA_RELATIVE_OFFSET_RANGE_DEVICE_PATH)
                                                                    );

  Strtoi64 (StartingOffsetStr, &Offset->StartingOffset);
  Strtoi64 (EndingOffsetStr, &Offset->EndingOffset);

  return (EFI_DEVICE_PATH_PROTOCOL *) Offset;
}

/**
  Converts a text device path node to text ram disk device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Text device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextRamDisk (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                                  *StartingAddrStr;
  CHAR16                                  *EndingAddrStr;
  CHAR16                                  *TypeGuidStr;
  CHAR16                                  *InstanceStr;
  MEDIA_RAM_DISK_DEVICE_PATH              *RamDisk;
  UINT64                                  StartingAddr;
  UINT64                                  EndingAddr;

  StartingAddrStr = GetNextParamStr (&TextDeviceNode);
  EndingAddrStr   = GetNextParamStr (&TextDeviceNode);
  InstanceStr     = GetNextParamStr (&TextDeviceNode);
  TypeGuidStr     = GetNextParamStr (&TextDeviceNode);
  RamDisk         = (MEDIA_RAM_DISK_DEVICE_PATH *) CreateDeviceNode (
                                                     MEDIA_DEVICE_PATH,
                                                     MEDIA_RAM_DISK_DP,
                                                     (UINT16) sizeof (MEDIA_RAM_DISK_DEVICE_PATH)
                                                     );

  Strtoi64 (StartingAddrStr, &StartingAddr);
  WriteUnaligned64 ((UINT64 *) &(RamDisk->StartingAddr[0]), StartingAddr);
  Strtoi64 (EndingAddrStr, &EndingAddr);
  WriteUnaligned64 ((UINT64 *) &(RamDisk->EndingAddr[0]), EndingAddr);
  RamDisk->Instance = (UINT16) Strtoi (InstanceStr);
  StrToGuid (TypeGuidStr, &RamDisk->TypeGuid);

  return (EFI_DEVICE_PATH_PROTOCOL *) RamDisk;
}

/**
  Converts a text device path node to text virtual disk device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Text device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextVirtualDisk (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                                  *StartingAddrStr;
  CHAR16                                  *EndingAddrStr;
  CHAR16                                  *InstanceStr;
  MEDIA_RAM_DISK_DEVICE_PATH              *RamDisk;
  UINT64                                  StartingAddr;
  UINT64                                  EndingAddr;

  StartingAddrStr = GetNextParamStr (&TextDeviceNode);
  EndingAddrStr   = GetNextParamStr (&TextDeviceNode);
  InstanceStr     = GetNextParamStr (&TextDeviceNode);

  RamDisk         = (MEDIA_RAM_DISK_DEVICE_PATH *) CreateDeviceNode (
                                                     MEDIA_DEVICE_PATH,
                                                     MEDIA_RAM_DISK_DP,
                                                     (UINT16) sizeof (MEDIA_RAM_DISK_DEVICE_PATH)
                                                     );

  Strtoi64 (StartingAddrStr, &StartingAddr);
  WriteUnaligned64 ((UINT64 *) &(RamDisk->StartingAddr[0]), StartingAddr);
  Strtoi64 (EndingAddrStr, &EndingAddr);
  WriteUnaligned64 ((UINT64 *) &(RamDisk->EndingAddr[0]), EndingAddr);
  RamDisk->Instance = (UINT16) Strtoi (InstanceStr);
  CopyGuid (&RamDisk->TypeGuid, &gEfiVirtualDiskGuid);

  return (EFI_DEVICE_PATH_PROTOCOL *) RamDisk;
}

/**
  Converts a text device path node to text virtual cd device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Text device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextVirtualCd (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                                  *StartingAddrStr;
  CHAR16                                  *EndingAddrStr;
  CHAR16                                  *InstanceStr;
  MEDIA_RAM_DISK_DEVICE_PATH              *RamDisk;
  UINT64                                  StartingAddr;
  UINT64                                  EndingAddr;

  StartingAddrStr = GetNextParamStr (&TextDeviceNode);
  EndingAddrStr   = GetNextParamStr (&TextDeviceNode);
  InstanceStr     = GetNextParamStr (&TextDeviceNode);

  RamDisk         = (MEDIA_RAM_DISK_DEVICE_PATH *) CreateDeviceNode (
                                                     MEDIA_DEVICE_PATH,
                                                     MEDIA_RAM_DISK_DP,
                                                     (UINT16) sizeof (MEDIA_RAM_DISK_DEVICE_PATH)
                                                     );

  Strtoi64 (StartingAddrStr, &StartingAddr);
  WriteUnaligned64 ((UINT64 *) &(RamDisk->StartingAddr[0]), StartingAddr);
  Strtoi64 (EndingAddrStr, &EndingAddr);
  WriteUnaligned64 ((UINT64 *) &(RamDisk->EndingAddr[0]), EndingAddr);
  RamDisk->Instance = (UINT16) Strtoi (InstanceStr);
  CopyGuid (&RamDisk->TypeGuid, &gEfiVirtualCdGuid);

  return (EFI_DEVICE_PATH_PROTOCOL *) RamDisk;
}

/**
  Converts a text device path node to text persistent virtual disk device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Text device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextPersistentVirtualDisk (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                                  *StartingAddrStr;
  CHAR16                                  *EndingAddrStr;
  CHAR16                                  *InstanceStr;
  MEDIA_RAM_DISK_DEVICE_PATH              *RamDisk;
  UINT64                                  StartingAddr;
  UINT64                                  EndingAddr;

  StartingAddrStr = GetNextParamStr (&TextDeviceNode);
  EndingAddrStr   = GetNextParamStr (&TextDeviceNode);
  InstanceStr     = GetNextParamStr (&TextDeviceNode);

  RamDisk         = (MEDIA_RAM_DISK_DEVICE_PATH *) CreateDeviceNode (
                                                     MEDIA_DEVICE_PATH,
                                                     MEDIA_RAM_DISK_DP,
                                                     (UINT16) sizeof (MEDIA_RAM_DISK_DEVICE_PATH)
                                                     );

  Strtoi64 (StartingAddrStr, &StartingAddr);
  WriteUnaligned64 ((UINT64 *) &(RamDisk->StartingAddr[0]), StartingAddr);
  Strtoi64 (EndingAddrStr, &EndingAddr);
  WriteUnaligned64 ((UINT64 *) &(RamDisk->EndingAddr[0]), EndingAddr);
  RamDisk->Instance = (UINT16) Strtoi (InstanceStr);
  CopyGuid (&RamDisk->TypeGuid, &gEfiPersistentVirtualDiskGuid);

  return (EFI_DEVICE_PATH_PROTOCOL *) RamDisk;
}

/**
  Converts a text device path node to text persistent virtual cd device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created Text device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextPersistentVirtualCd (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16                                  *StartingAddrStr;
  CHAR16                                  *EndingAddrStr;
  CHAR16                                  *InstanceStr;
  MEDIA_RAM_DISK_DEVICE_PATH              *RamDisk;
  UINT64                                  StartingAddr;
  UINT64                                  EndingAddr;

  StartingAddrStr = GetNextParamStr (&TextDeviceNode);
  EndingAddrStr   = GetNextParamStr (&TextDeviceNode);
  InstanceStr     = GetNextParamStr (&TextDeviceNode);

  RamDisk         = (MEDIA_RAM_DISK_DEVICE_PATH *) CreateDeviceNode (
                                                     MEDIA_DEVICE_PATH,
                                                     MEDIA_RAM_DISK_DP,
                                                     (UINT16) sizeof (MEDIA_RAM_DISK_DEVICE_PATH)
                                                     );

  Strtoi64 (StartingAddrStr, &StartingAddr);
  WriteUnaligned64 ((UINT64 *) &(RamDisk->StartingAddr[0]), StartingAddr);
  Strtoi64 (EndingAddrStr, &EndingAddr);
  WriteUnaligned64 ((UINT64 *) &(RamDisk->EndingAddr[0]), EndingAddr);
  RamDisk->Instance = (UINT16) Strtoi (InstanceStr);
  CopyGuid (&RamDisk->TypeGuid, &gEfiPersistentVirtualCdGuid);

  return (EFI_DEVICE_PATH_PROTOCOL *) RamDisk;
}

/**
  Converts a BBS text device path node to BBS device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to BBS device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextBbsPath (
  IN CHAR16 *TextDeviceNode
  )
{
  return DevPathFromTextGenericPath (BBS_DEVICE_PATH, TextDeviceNode);
}

/**
  Converts a text device path node to BIOS Boot Specification device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created BIOS Boot Specification device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextBBS (
  IN CHAR16 *TextDeviceNode
  )
{
  CHAR16              *TypeStr;
  CHAR16              *IdStr;
  CHAR16              *FlagsStr;
  CHAR8               *AsciiStr;
  BBS_BBS_DEVICE_PATH *Bbs;

  TypeStr   = GetNextParamStr (&TextDeviceNode);
  IdStr     = GetNextParamStr (&TextDeviceNode);
  FlagsStr  = GetNextParamStr (&TextDeviceNode);
  Bbs       = (BBS_BBS_DEVICE_PATH *) CreateDeviceNode (
                                        BBS_DEVICE_PATH,
                                        BBS_BBS_DP,
                                        (UINT16) (sizeof (BBS_BBS_DEVICE_PATH) + StrLen (IdStr))
                                        );

  if (StrCmp (TypeStr, "Floppy") == 0) {
    Bbs->DeviceType = BBS_TYPE_FLOPPY;
  } else if (StrCmp (TypeStr, "HD") == 0) {
    Bbs->DeviceType = BBS_TYPE_HARDDRIVE;
  } else if (StrCmp (TypeStr, "CDROM") == 0) {
    Bbs->DeviceType = BBS_TYPE_CDROM;
  } else if (StrCmp (TypeStr, "PCMCIA") == 0) {
    Bbs->DeviceType = BBS_TYPE_PCMCIA;
  } else if (StrCmp (TypeStr, "USB") == 0) {
    Bbs->DeviceType = BBS_TYPE_USB;
  } else if (StrCmp (TypeStr, "Network") == 0) {
    Bbs->DeviceType = BBS_TYPE_EMBEDDED_NETWORK;
  } else {
    Bbs->DeviceType = (UINT16) Strtoi (TypeStr);
  }

  AsciiStr = Bbs->String;
  StrToAscii (IdStr, &AsciiStr);

  Bbs->StatusFlag = (UINT16) Strtoi (FlagsStr);

  return (EFI_DEVICE_PATH_PROTOCOL *) Bbs;
}

/**
  Converts a text device path node to SATA device path structure.

  @param TextDeviceNode  The input Text device path node.

  @return A pointer to the newly-created SATA device path structure.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
DevPathFromTextSata (
  IN CHAR16 *TextDeviceNode
  )
{
  SATA_DEVICE_PATH *Sata;
  CHAR16           *Param1;
  CHAR16           *Param2;
  CHAR16           *Param3;

  Param1 = GetNextParamStr (&TextDeviceNode);
  Param2 = GetNextParamStr (&TextDeviceNode);
  Param3 = GetNextParamStr (&TextDeviceNode);

  Sata = (SATA_DEVICE_PATH *) CreateDeviceNode (
                                MESSAGING_DEVICE_PATH,
                                MSG_SATA_DP,
                                (UINT16) sizeof (SATA_DEVICE_PATH)
                                );
  Sata->HBAPortNumber            = (UINT16) Strtoi (Param1);
  Sata->PortMultiplierPortNumber = (UINT16) Strtoi (Param2);
  Sata->Lun                      = (UINT16) Strtoi (Param3);

  return (EFI_DEVICE_PATH_PROTOCOL *) Sata;
}

GLOBAL_REMOVE_IF_UNREFERENCED DEVICE_PATH_FROM_TEXT_TABLE mUefiDevicePathLibDevPathFromTextTable[] = {
  {"Path",                    DevPathFromTextPath                    },

  {"HardwarePath",            DevPathFromTextHardwarePath            },
  {"Pci",                     DevPathFromTextPci                     },
  {"PcCard",                  DevPathFromTextPcCard                  },
  {"MemoryMapped",            DevPathFromTextMemoryMapped            },
  {"VenHw",                   DevPathFromTextVenHw                   },
  {"Ctrl",                    DevPathFromTextCtrl                    },
  {"BMC",                     DevPathFromTextBmc                     },

  {"AcpiPath",                DevPathFromTextAcpiPath                },
  {"Acpi",                    DevPathFromTextAcpi                    },
  {"PciRoot",                 DevPathFromTextPciRoot                 },
  {"PcieRoot",                DevPathFromTextPcieRoot                },
  {"Floppy",                  DevPathFromTextFloppy                  },
  {"Keyboard",                DevPathFromTextKeyboard                },
  {"Serial",                  DevPathFromTextSerial                  },
  {"ParallelPort",            DevPathFromTextParallelPort            },
  {"AcpiEx",                  DevPathFromTextAcpiEx                  },
  {"AcpiExp",                 DevPathFromTextAcpiExp                 },
  {"AcpiAdr",                 DevPathFromTextAcpiAdr                 },

  {"Msg",                     DevPathFromTextMsg                     },
  {"Ata",                     DevPathFromTextAta                     },
  {"Scsi",                    DevPathFromTextScsi                    },
  {"Fibre",                   DevPathFromTextFibre                   },
  {"FibreEx",                 DevPathFromTextFibreEx                 },
  {"I1394",                   DevPathFromText1394                    },
  {"USB",                     DevPathFromTextUsb                     },
  {"I2O",                     DevPathFromTextI2O                     },
  {"Infiniband",              DevPathFromTextInfiniband              },
  {"VenMsg",                  DevPathFromTextVenMsg                  },
  {"VenPcAnsi",               DevPathFromTextVenPcAnsi               },
  {"VenVt100",                DevPathFromTextVenVt100                },
  {"VenVt100Plus",            DevPathFromTextVenVt100Plus            },
  {"VenUtf8",                 DevPathFromTextVenUtf8                 },
  {"UartFlowCtrl",            DevPathFromTextUartFlowCtrl            },
  {"SAS",                     DevPathFromTextSAS                     },
  {"SasEx",                   DevPathFromTextSasEx                   },
  {"NVMe",                    DevPathFromTextNVMe                    },
  {"UFS",                     DevPathFromTextUfs                     },
  {"SD",                      DevPathFromTextSd                      },
  {"eMMC",                    DevPathFromTextEmmc                    },
  {"DebugPort",               DevPathFromTextDebugPort               },
  {"MAC",                     DevPathFromTextMAC                     },
  {"IPv4",                    DevPathFromTextIPv4                    },
  {"IPv6",                    DevPathFromTextIPv6                    },
  {"Uart",                    DevPathFromTextUart                    },
  {"UsbClass",                DevPathFromTextUsbClass                },
  {"UsbAudio",                DevPathFromTextUsbAudio                },
  {"UsbCDCControl",           DevPathFromTextUsbCDCControl           },
  {"UsbHID",                  DevPathFromTextUsbHID                  },
  {"UsbImage",                DevPathFromTextUsbImage                },
  {"UsbPrinter",              DevPathFromTextUsbPrinter              },
  {"UsbMassStorage",          DevPathFromTextUsbMassStorage          },
  {"UsbHub",                  DevPathFromTextUsbHub                  },
  {"UsbCDCData",              DevPathFromTextUsbCDCData              },
  {"UsbSmartCard",            DevPathFromTextUsbSmartCard            },
  {"UsbVideo",                DevPathFromTextUsbVideo                },
  {"UsbDiagnostic",           DevPathFromTextUsbDiagnostic           },
  {"UsbWireless",             DevPathFromTextUsbWireless             },
  {"UsbDeviceFirmwareUpdate", DevPathFromTextUsbDeviceFirmwareUpdate },
  {"UsbIrdaBridge",           DevPathFromTextUsbIrdaBridge           },
  {"UsbTestAndMeasurement",   DevPathFromTextUsbTestAndMeasurement   },
  {"UsbWwid",                 DevPathFromTextUsbWwid                 },
  {"Unit",                    DevPathFromTextUnit                    },
  {"iSCSI",                   DevPathFromTextiSCSI                   },
  {"Vlan",                    DevPathFromTextVlan                    },
  {"Uri",                     DevPathFromTextUri                     },
  {"Bluetooth",               DevPathFromTextBluetooth               },
  {"Wi-Fi",                   DevPathFromTextWiFi                    },
  {"MediaPath",               DevPathFromTextMediaPath               },
  {"HD",                      DevPathFromTextHD                      },
  {"CDROM",                   DevPathFromTextCDROM                   },
  {"VenMedia",                DevPathFromTextVenMedia                },
  {"Media",                   DevPathFromTextMedia                   },
  {"Fv",                      DevPathFromTextFv                      },
  {"FvFile",                  DevPathFromTextFvFile                  },
  {"File",                    DevPathFromTextFilePath                },
  {"Offset",                  DevPathFromTextRelativeOffsetRange     },
  {"RamDisk",                 DevPathFromTextRamDisk                 },
  {"VirtualDisk",             DevPathFromTextVirtualDisk             },
  {"VirtualCD",               DevPathFromTextVirtualCd               },
  {"PersistentVirtualDisk",   DevPathFromTextPersistentVirtualDisk   },
  {"PersistentVirtualCD",     DevPathFromTextPersistentVirtualCd     },

  {"BbsPath",                 DevPathFromTextBbsPath                 },
  {"BBS",                     DevPathFromTextBBS                     },
  {"Sata",                    DevPathFromTextSata                    },
  {NULL, NULL}
};

/**
  Convert text to the binary representation of a device node.

  @param TextDeviceNode  TextDeviceNode points to the text representation of a device
                         node. Conversion starts with the first character and continues
                         until the first non-device node character.

  @return A pointer to the EFI device node or NULL if TextDeviceNode is NULL or there was
          insufficient memory or text unsupported.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
EFIAPI
UefiDevicePathLibConvertTextToDeviceNode (
  IN CONST CHAR16 *TextDeviceNode
  )
{
  DEVICE_PATH_FROM_TEXT    FromText;
  CHAR16                   *ParamStr;
  EFI_DEVICE_PATH_PROTOCOL *DeviceNode;
  CHAR16                   *DeviceNodeStr;
  UINTN                    Index;

  if ((TextDeviceNode == NULL) || (IS_NULL (*TextDeviceNode))) {
    return NULL;
  }

  ParamStr      = NULL;
  FromText      = NULL;
  DeviceNodeStr = UefiDevicePathLibStrDuplicate (TextDeviceNode);
  ASSERT (DeviceNodeStr != NULL);

  for (Index = 0; mUefiDevicePathLibDevPathFromTextTable[Index].Function != NULL; Index++) {
    ParamStr = GetParamByNodeName (DeviceNodeStr, mUefiDevicePathLibDevPathFromTextTable[Index].DevicePathNodeText);
    if (ParamStr != NULL) {
      FromText = mUefiDevicePathLibDevPathFromTextTable[Index].Function;
      break;
    }
  }

  if (FromText == NULL) {
    //
    // A file path
    //
    FromText = DevPathFromTextFilePath;
    DeviceNode = FromText (DeviceNodeStr);
  } else {
    DeviceNode = FromText (ParamStr);
    FreePool (ParamStr);
  }

  FreePool (DeviceNodeStr);

  return DeviceNode;
}

/**
  Convert text to the binary representation of a device path.


  @param TextDevicePath  TextDevicePath points to the text representation of a device
                         path. Conversion starts with the first character and continues
                         until the first non-device node character.

  @return A pointer to the allocated device path or NULL if TextDeviceNode is NULL or
          there was insufficient memory.

**/
static
EFI_DEVICE_PATH_PROTOCOL *
EFIAPI
UefiDevicePathLibConvertTextToDevicePath (
  IN CONST CHAR16 *TextDevicePath
  )
{
  EFI_DEVICE_PATH_PROTOCOL *DeviceNode;
  EFI_DEVICE_PATH_PROTOCOL *NewDevicePath;
  CHAR16                   *DevicePathStr;
  CHAR16                   *Str;
  CHAR16                   *DeviceNodeStr;
  BOOLEAN                  IsInstanceEnd;
  EFI_DEVICE_PATH_PROTOCOL *DevicePath;

  if ((TextDevicePath == NULL) || (IS_NULL (*TextDevicePath))) {
    return NULL;
  }

  DevicePath = (EFI_DEVICE_PATH_PROTOCOL *) AllocatePool (END_DEVICE_PATH_LENGTH);
  ASSERT (DevicePath != NULL);
  SetDevicePathEndNode (DevicePath);

  DevicePathStr = UefiDevicePathLibStrDuplicate (TextDevicePath);

  Str           = DevicePathStr;
  while ((DeviceNodeStr = GetNextDeviceNodeStr (&Str, &IsInstanceEnd)) != NULL) {
    DeviceNode = UefiDevicePathLibConvertTextToDeviceNode (DeviceNodeStr);

    NewDevicePath = AppendDevicePathNode (DevicePath, DeviceNode);
    FreePool (DevicePath);
    FreePool (DeviceNode);
    DevicePath = NewDevicePath;

    if (IsInstanceEnd) {
      DeviceNode = (EFI_DEVICE_PATH_PROTOCOL *) AllocatePool (END_DEVICE_PATH_LENGTH);
      ASSERT (DeviceNode != NULL);
      SetDevicePathEndNode (DeviceNode);
      // Fix from https://bugzilla.tianocore.org/show_bug.cgi?id=419
      DeviceNode->SubType = END_INSTANCE_DEVICE_PATH_SUBTYPE;

      NewDevicePath = AppendDevicePathNode (DevicePath, DeviceNode);
      FreePool (DevicePath);
      FreePool (DeviceNode);
      DevicePath = NewDevicePath;
    }
  }

  FreePool (DevicePathStr);
  return DevicePath;
}

ssize_t
efidp_parse_device_path(char *path, efidp out, size_t max)
{
	EFI_DEVICE_PATH_PROTOCOL *dp;
	UINTN len;

	dp = UefiDevicePathLibConvertTextToDevicePath (path);
	if (dp == NULL)
		return -1;
	len = GetDevicePathSize(dp);
	if (len > max) {
		free(dp);
		return -1;
	}
	memcpy(out, dp, len);
	free(dp);

	return len;
}
