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

#include <efivar.h>
#include <stdio.h>
#include <string.h>

#include "efichar.h"

#include "efi-osdep.h"
#include "efivar-dp.h"

#include "uefi-dplib.h"

/*
 * This is a lie, but since we have converted everything
 * from wide to narrow, it's the right lie now.
 */
#define UnicodeSPrint snprintf

/*
 * Taken from MdePkg/Library/UefiDevicePathLib/DevicePathToText.c
 * hash a11928f3310518ab1c6fd34e8d0fdbb72de9602c 2017-Mar-01
 * heavily modified:
 *	wide strings converted to narrow
 *	Low level printing code redone for narrow strings
 *	Routines made static
 *	%s -> %S in spots (where it is still UCS-2)
 *	%a (ascii) -> %s
 *	%g -> %36s hack to print guid (see above for caveat)
 *	some tidying up of const and deconsting. It's evil, but const
 *	  poisoning the whole file was too much.
 */

/** @file
  DevicePathToText protocol as defined in the UEFI 2.0 specification.

  (C) Copyright 2015 Hewlett-Packard Development Company, L.P.<BR>
Copyright (c) 2013 - 2015, Intel Corporation. All rights reserved.<BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

// #include "UefiDevicePathLib.h"

/**
  Concatenates a formatted unicode string to allocated pool. The caller must
  free the resulting buffer.

  @param Str             Tracks the allocated pool, size in use, and
                         amount of pool allocated.
  @param Fmt             The format string
  @param ...             Variable arguments based on the format string.

  @return Allocated buffer with the formatted string printed in it.
          The caller must free the allocated buffer. The buffer
          allocation is not packed.

**/
static char *
EFIAPI
UefiDevicePathLibCatPrint (
  IN OUT POOL_PRINT   *Str,
  IN const char       *Fmt,
  ...
  )
{
  UINTN   Count;
  VA_LIST Args;

  VA_START (Args, Fmt);
  Count = vsnprintf(NULL, 0, Fmt, Args);
  VA_END(Args);

  if ((Str->Count + (Count + 1)) > Str->Capacity) {
    Str->Capacity = (Str->Count + (Count + 1) * 2);
    Str->Str = reallocf(Str->Str, Str->Capacity);
    ASSERT (Str->Str != NULL);
  }
  VA_START (Args, Fmt);
  vsnprintf(Str->Str + Str->Count, Str->Capacity - Str->Count, Fmt, Args);
  Str->Count += Count;

  VA_END (Args);
  return Str->Str;
}

/**
  Converts a PCI device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextPci (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  PCI_DEVICE_PATH *Pci;

  Pci = DevPath;
  UefiDevicePathLibCatPrint (Str, "Pci(0x%x,0x%x)", Pci->Device, Pci->Function);
}

/**
  Converts a PC Card device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextPccard (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  PCCARD_DEVICE_PATH  *Pccard;

  Pccard = DevPath;
  UefiDevicePathLibCatPrint (Str, "PcCard(0x%x)", Pccard->FunctionNumber);
}

/**
  Converts a Memory Map device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextMemMap (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  MEMMAP_DEVICE_PATH  *MemMap;

  MemMap = DevPath;
  UefiDevicePathLibCatPrint (
    Str,
    "MemoryMapped(0x%x,0x%lx,0x%lx)",
    MemMap->MemoryType,
    MemMap->StartingAddress,
    MemMap->EndingAddress
    );
}

/**
  Converts a Vendor device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextVendor (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  VENDOR_DEVICE_PATH  *Vendor;
  const char          *Type;
  UINTN               Index;
  UINTN               DataLength;
  UINT32              FlowControlMap;
  UINT16              Info;

  Vendor = (VENDOR_DEVICE_PATH *) DevPath;
  switch (DevicePathType (&Vendor->Header)) {
  case HARDWARE_DEVICE_PATH:
    Type = "Hw";
    break;

  case MESSAGING_DEVICE_PATH:
    Type = "Msg";
    if (AllowShortcuts) {
      if (CompareGuid (&Vendor->Guid, &gEfiPcAnsiGuid)) {
        UefiDevicePathLibCatPrint (Str, "VenPcAnsi()");
        return ;
      } else if (CompareGuid (&Vendor->Guid, &gEfiVT100Guid)) {
        UefiDevicePathLibCatPrint (Str, "VenVt100()");
        return ;
      } else if (CompareGuid (&Vendor->Guid, &gEfiVT100PlusGuid)) {
        UefiDevicePathLibCatPrint (Str, "VenVt100Plus()");
        return ;
      } else if (CompareGuid (&Vendor->Guid, &gEfiVTUTF8Guid)) {
        UefiDevicePathLibCatPrint (Str, "VenUft8()");
        return ;
      } else if (CompareGuid (&Vendor->Guid, &gEfiUartDevicePathGuid)) {
        FlowControlMap = (((UART_FLOW_CONTROL_DEVICE_PATH *) Vendor)->FlowControlMap);
        switch (FlowControlMap & 0x00000003) {
        case 0:
          UefiDevicePathLibCatPrint (Str, "UartFlowCtrl(%s)", "None");
          break;

        case 1:
          UefiDevicePathLibCatPrint (Str, "UartFlowCtrl(%s)", "Hardware");
          break;

        case 2:
          UefiDevicePathLibCatPrint (Str, "UartFlowCtrl(%s)", "XonXoff");
          break;

        default:
          break;
        }

        return ;
      } else if (CompareGuid (&Vendor->Guid, &gEfiSasDevicePathGuid)) {
        UefiDevicePathLibCatPrint (
          Str,
          "SAS(0x%lx,0x%lx,0x%x,",
          ((SAS_DEVICE_PATH *) Vendor)->SasAddress,
          ((SAS_DEVICE_PATH *) Vendor)->Lun,
          ((SAS_DEVICE_PATH *) Vendor)->RelativeTargetPort
          );
        Info = (((SAS_DEVICE_PATH *) Vendor)->DeviceTopology);
        if (((Info & 0x0f) == 0) && ((Info & BIT7) == 0)) {
          UefiDevicePathLibCatPrint (Str, "NoTopology,0,0,0,");
        } else if (((Info & 0x0f) <= 2) && ((Info & BIT7) == 0)) {
          UefiDevicePathLibCatPrint (
            Str,
            "%s,%s,%s,",
            ((Info & BIT4) != 0) ? "SATA" : "SAS",
            ((Info & BIT5) != 0) ? "External" : "Internal",
            ((Info & BIT6) != 0) ? "Expanded" : "Direct"
            );
          if ((Info & 0x0f) == 1) {
            UefiDevicePathLibCatPrint (Str, "0,");
          } else {
            //
            // Value 0x0 thru 0xFF -> Drive 1 thru Drive 256
            //
            UefiDevicePathLibCatPrint (Str, "0x%x,", ((Info >> 8) & 0xff) + 1);
          }
        } else {
          UefiDevicePathLibCatPrint (Str, "0x%x,0,0,0,", Info);
        }

        UefiDevicePathLibCatPrint (Str, "0x%x)", ((SAS_DEVICE_PATH *) Vendor)->Reserved);
        return ;
      } else if (CompareGuid (&Vendor->Guid, &gEfiDebugPortProtocolGuid)) {
        UefiDevicePathLibCatPrint (Str, "DebugPort()");
        return ;
      }
    }
    break;

  case MEDIA_DEVICE_PATH:
    Type = "Media";
    break;

  default:
    Type = "?";
    break;
  }

  DataLength = DevicePathNodeLength (&Vendor->Header) - sizeof (VENDOR_DEVICE_PATH);
  UefiDevicePathLibCatPrint (Str, "Ven%s(%36s", Type, G(&Vendor->Guid));
  if (DataLength != 0) {
    UefiDevicePathLibCatPrint (Str, ",");
    for (Index = 0; Index < DataLength; Index++) {
      UefiDevicePathLibCatPrint (Str, "%02x", ((VENDOR_DEVICE_PATH_WITH_DATA *) Vendor)->VendorDefinedData[Index]);
    }
  }

  UefiDevicePathLibCatPrint (Str, ")");
}

/**
  Converts a Controller device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextController (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  CONTROLLER_DEVICE_PATH  *Controller;

  Controller = DevPath;
  UefiDevicePathLibCatPrint (
    Str,
    "Ctrl(0x%x)",
    Controller->ControllerNumber
    );
}

/**
  Converts a BMC device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextBmc (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  BMC_DEVICE_PATH    *Bmc;

  Bmc = DevPath;
  UefiDevicePathLibCatPrint (
    Str,
    "BMC(0x%x,0x%lx)",
    Bmc->InterfaceType,
    ReadUnaligned64 ((&Bmc->BaseAddress))
    );
}

/**
  Converts a ACPI device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextAcpi (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  ACPI_HID_DEVICE_PATH  *Acpi;

  Acpi = DevPath;
  if ((Acpi->HID & PNP_EISA_ID_MASK) == PNP_EISA_ID_CONST) {
    switch (EISA_ID_TO_NUM (Acpi->HID)) {
    case 0x0a03:
      UefiDevicePathLibCatPrint (Str, "PciRoot(0x%x)", Acpi->UID);
      break;

    case 0x0a08:
      UefiDevicePathLibCatPrint (Str, "PcieRoot(0x%x)", Acpi->UID);
      break;

    case 0x0604:
      UefiDevicePathLibCatPrint (Str, "Floppy(0x%x)", Acpi->UID);
      break;

    case 0x0301:
      UefiDevicePathLibCatPrint (Str, "Keyboard(0x%x)", Acpi->UID);
      break;

    case 0x0501:
      UefiDevicePathLibCatPrint (Str, "Serial(0x%x)", Acpi->UID);
      break;

    case 0x0401:
      UefiDevicePathLibCatPrint (Str, "ParallelPort(0x%x)", Acpi->UID);
      break;

    default:
      UefiDevicePathLibCatPrint (Str, "Acpi(PNP%04x,0x%x)", EISA_ID_TO_NUM (Acpi->HID), Acpi->UID);
      break;
    }
  } else {
    UefiDevicePathLibCatPrint (Str, "Acpi(0x%08x,0x%x)", Acpi->HID, Acpi->UID);
  }
}

/**
  Converts a ACPI extended HID device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextAcpiEx (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  ACPI_EXTENDED_HID_DEVICE_PATH  *AcpiEx;
  CHAR8                          *HIDStr;
  CHAR8                          *UIDStr;
  CHAR8                          *CIDStr;
  char                           HIDText[11];
  char                           CIDText[11];

  AcpiEx = DevPath;
  HIDStr = (CHAR8 *) (((UINT8 *) AcpiEx) + sizeof (ACPI_EXTENDED_HID_DEVICE_PATH));
  UIDStr = HIDStr + AsciiStrLen (HIDStr) + 1;
  CIDStr = UIDStr + AsciiStrLen (UIDStr) + 1;

  //
  // Converts EISA identification to string.
  //
  UnicodeSPrint (
    HIDText,
    sizeof (HIDText),
    "%c%c%c%04X",
    ((AcpiEx->HID >> 10) & 0x1f) + 'A' - 1,
    ((AcpiEx->HID >>  5) & 0x1f) + 'A' - 1,
    ((AcpiEx->HID >>  0) & 0x1f) + 'A' - 1,
    (AcpiEx->HID >> 16) & 0xFFFF
    );
  UnicodeSPrint (
    CIDText,
    sizeof (CIDText),
    "%c%c%c%04X",
    ((AcpiEx->CID >> 10) & 0x1f) + 'A' - 1,
    ((AcpiEx->CID >>  5) & 0x1f) + 'A' - 1,
    ((AcpiEx->CID >>  0) & 0x1f) + 'A' - 1,
    (AcpiEx->CID >> 16) & 0xFFFF
    );

  if ((*HIDStr == '\0') && (*CIDStr == '\0') && (AcpiEx->UID == 0)) {
    //
    // use AcpiExp()
    //
    UefiDevicePathLibCatPrint (
      Str,
      "AcpiExp(%s,%s,%s)",
      HIDText,
      CIDText,
      UIDStr
      );
  } else {
    if (AllowShortcuts) {
      //
      // display only
      //
      if (AcpiEx->HID == 0) {
        UefiDevicePathLibCatPrint (Str, "AcpiEx(%s,", HIDStr);
      } else {
        UefiDevicePathLibCatPrint (Str, "AcpiEx(%s,", HIDText);
      }

      if (AcpiEx->UID == 0) {
        UefiDevicePathLibCatPrint (Str, "%s,", UIDStr);
      } else {
        UefiDevicePathLibCatPrint (Str, "0x%x,", AcpiEx->UID);
      }

      if (AcpiEx->CID == 0) {
        UefiDevicePathLibCatPrint (Str, "%s)", CIDStr);
      } else {
        UefiDevicePathLibCatPrint (Str, "%s)", CIDText);
      }
    } else {
      UefiDevicePathLibCatPrint (
        Str,
        "AcpiEx(%s,%s,0x%x,%s,%s,%s)",
        HIDText,
        CIDText,
        AcpiEx->UID,
        HIDStr,
        CIDStr,
        UIDStr
        );
    }
  }
}

/**
  Converts a ACPI address device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextAcpiAdr (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  ACPI_ADR_DEVICE_PATH    *AcpiAdr;
  UINT32                  *Addr;
  UINT16                  Index;
  UINT16                  Length;
  UINT16                  AdditionalAdrCount;

  AcpiAdr            = DevPath;
  Length             = (UINT16) DevicePathNodeLength ((EFI_DEVICE_PATH_PROTOCOL *) AcpiAdr);
  AdditionalAdrCount = (UINT16) ((Length - 8) / 4);

  UefiDevicePathLibCatPrint (Str, "AcpiAdr(0x%x", AcpiAdr->ADR);
  Addr = &AcpiAdr->ADR + 1;
  for (Index = 0; Index < AdditionalAdrCount; Index++) {
    UefiDevicePathLibCatPrint (Str, ",0x%x", Addr[Index]);
  }
  UefiDevicePathLibCatPrint (Str, ")");
}

/**
  Converts a ATAPI device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextAtapi (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  ATAPI_DEVICE_PATH *Atapi;

  Atapi = DevPath;

  if (DisplayOnly) {
    UefiDevicePathLibCatPrint (Str, "Ata(0x%x)", Atapi->Lun);
  } else {
    UefiDevicePathLibCatPrint (
      Str,
      "Ata(%s,%s,0x%x)",
      (Atapi->PrimarySecondary == 1) ? "Secondary" : "Primary",
      (Atapi->SlaveMaster == 1) ? "Slave" : "Master",
      Atapi->Lun
      );
  }
}

/**
  Converts a SCSI device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextScsi (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  SCSI_DEVICE_PATH  *Scsi;

  Scsi = DevPath;
  UefiDevicePathLibCatPrint (Str, "Scsi(0x%x,0x%x)", Scsi->Pun, Scsi->Lun);
}

/**
  Converts a Fibre device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextFibre (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  FIBRECHANNEL_DEVICE_PATH  *Fibre;

  Fibre = DevPath;
  UefiDevicePathLibCatPrint (Str, "Fibre(0x%lx,0x%lx)", Fibre->WWN, Fibre->Lun);
}

/**
  Converts a FibreEx device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextFibreEx (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  FIBRECHANNELEX_DEVICE_PATH  *FibreEx;
  UINTN                       Index;

  FibreEx = DevPath;
  UefiDevicePathLibCatPrint (Str, "FibreEx(0x");
  for (Index = 0; Index < sizeof (FibreEx->WWN) / sizeof (FibreEx->WWN[0]); Index++) {
    UefiDevicePathLibCatPrint (Str, "%02x", FibreEx->WWN[Index]);
  }
  UefiDevicePathLibCatPrint (Str, ",0x");
  for (Index = 0; Index < sizeof (FibreEx->Lun) / sizeof (FibreEx->Lun[0]); Index++) {
    UefiDevicePathLibCatPrint (Str, "%02x", FibreEx->Lun[Index]);
  }
  UefiDevicePathLibCatPrint (Str, ")");
}

/**
  Converts a Sas Ex device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextSasEx (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  SASEX_DEVICE_PATH  *SasEx;
  UINTN              Index;

  SasEx = DevPath;
  UefiDevicePathLibCatPrint (Str, "SasEx(0x");

  for (Index = 0; Index < sizeof (SasEx->SasAddress) / sizeof (SasEx->SasAddress[0]); Index++) {
    UefiDevicePathLibCatPrint (Str, "%02x", SasEx->SasAddress[Index]);
  }
  UefiDevicePathLibCatPrint (Str, ",0x");
  for (Index = 0; Index < sizeof (SasEx->Lun) / sizeof (SasEx->Lun[0]); Index++) {
    UefiDevicePathLibCatPrint (Str, "%02x", SasEx->Lun[Index]);
  }
  UefiDevicePathLibCatPrint (Str, ",0x%x,", SasEx->RelativeTargetPort);

  if (((SasEx->DeviceTopology & 0x0f) == 0) && ((SasEx->DeviceTopology & BIT7) == 0)) {
    UefiDevicePathLibCatPrint (Str, "NoTopology,0,0,0");
  } else if (((SasEx->DeviceTopology & 0x0f) <= 2) && ((SasEx->DeviceTopology & BIT7) == 0)) {
    UefiDevicePathLibCatPrint (
      Str,
      "%s,%s,%s,",
      ((SasEx->DeviceTopology & BIT4) != 0) ? "SATA" : "SAS",
      ((SasEx->DeviceTopology & BIT5) != 0) ? "External" : "Internal",
      ((SasEx->DeviceTopology & BIT6) != 0) ? "Expanded" : "Direct"
      );
    if ((SasEx->DeviceTopology & 0x0f) == 1) {
      UefiDevicePathLibCatPrint (Str, "0");
    } else {
      //
      // Value 0x0 thru 0xFF -> Drive 1 thru Drive 256
      //
      UefiDevicePathLibCatPrint (Str, "0x%x", ((SasEx->DeviceTopology >> 8) & 0xff) + 1);
    }
  } else {
    UefiDevicePathLibCatPrint (Str, "0x%x,0,0,0", SasEx->DeviceTopology);
  }

  UefiDevicePathLibCatPrint (Str, ")");
  return ;

}

/**
  Converts a NVM Express Namespace device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextNVMe (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  NVME_NAMESPACE_DEVICE_PATH *Nvme;
  UINT8                      *Uuid;

  Nvme = DevPath;
  Uuid = (UINT8 *) &Nvme->NamespaceUuid;
  UefiDevicePathLibCatPrint (
    Str,
    "NVMe(0x%x,%02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x)",
    Nvme->NamespaceId,
    Uuid[7], Uuid[6], Uuid[5], Uuid[4],
    Uuid[3], Uuid[2], Uuid[1], Uuid[0]
    );
}

/**
  Converts a UFS device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextUfs (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  UFS_DEVICE_PATH  *Ufs;

  Ufs = DevPath;
  UefiDevicePathLibCatPrint (Str, "UFS(0x%x,0x%x)", Ufs->Pun, Ufs->Lun);
}

/**
  Converts a SD (Secure Digital) device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextSd (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  SD_DEVICE_PATH             *Sd;

  Sd = DevPath;
  UefiDevicePathLibCatPrint (
    Str,
    "SD(0x%x)",
    Sd->SlotNumber
    );
}

/**
  Converts a EMMC (Embedded MMC) device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextEmmc (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  EMMC_DEVICE_PATH             *Emmc;

  Emmc = DevPath;
  UefiDevicePathLibCatPrint (
    Str,
    "eMMC(0x%x)",
    Emmc->SlotNumber
    );
}

/**
  Converts a 1394 device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToText1394 (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  F1394_DEVICE_PATH *F1394DevPath;

  F1394DevPath = DevPath;
  //
  // Guid has format of IEEE-EUI64
  //
  UefiDevicePathLibCatPrint (Str, "I1394(%016lx)", F1394DevPath->Guid);
}

/**
  Converts a USB device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextUsb (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  USB_DEVICE_PATH *Usb;

  Usb = DevPath;
  UefiDevicePathLibCatPrint (Str, "USB(0x%x,0x%x)", Usb->ParentPortNumber, Usb->InterfaceNumber);
}

/**
  Converts a USB WWID device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextUsbWWID (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  USB_WWID_DEVICE_PATH  *UsbWWId;
  CHAR16                *SerialNumberStr;
  CHAR16                *NewStr;
  UINT16                Length;

  UsbWWId = DevPath;

  SerialNumberStr = (CHAR16 *) (&UsbWWId + 1);
  Length = (UINT16) ((DevicePathNodeLength ((EFI_DEVICE_PATH_PROTOCOL *) UsbWWId) - sizeof (USB_WWID_DEVICE_PATH)) / sizeof (CHAR16));
  if (SerialNumberStr [Length - 1] != 0) {
    //
    // In case no NULL terminator in SerialNumber, create a new one with NULL terminator
    //
    NewStr = AllocateCopyPool ((Length + 1) * sizeof (CHAR16), SerialNumberStr);
    ASSERT (NewStr != NULL);
    NewStr [Length] = 0;
    SerialNumberStr = NewStr;
  }

  UefiDevicePathLibCatPrint (
    Str,
    "UsbWwid(0x%x,0x%x,0x%x,\"%S\")",
    UsbWWId->VendorId,
    UsbWWId->ProductId,
    UsbWWId->InterfaceNumber,
    SerialNumberStr
    );
}

/**
  Converts a Logic Unit device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextLogicalUnit (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  DEVICE_LOGICAL_UNIT_DEVICE_PATH *LogicalUnit;

  LogicalUnit = DevPath;
  UefiDevicePathLibCatPrint (Str, "Unit(0x%x)", LogicalUnit->Lun);
}

/**
  Converts a USB class device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextUsbClass (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  USB_CLASS_DEVICE_PATH *UsbClass;
  BOOLEAN               IsKnownSubClass;


  UsbClass = DevPath;

  IsKnownSubClass = TRUE;
  switch (UsbClass->DeviceClass) {
  case USB_CLASS_AUDIO:
    UefiDevicePathLibCatPrint (Str, "UsbAudio");
    break;

  case USB_CLASS_CDCCONTROL:
    UefiDevicePathLibCatPrint (Str, "UsbCDCControl");
    break;

  case USB_CLASS_HID:
    UefiDevicePathLibCatPrint (Str, "UsbHID");
    break;

  case USB_CLASS_IMAGE:
    UefiDevicePathLibCatPrint (Str, "UsbImage");
    break;

  case USB_CLASS_PRINTER:
    UefiDevicePathLibCatPrint (Str, "UsbPrinter");
    break;

  case USB_CLASS_MASS_STORAGE:
    UefiDevicePathLibCatPrint (Str, "UsbMassStorage");
    break;

  case USB_CLASS_HUB:
    UefiDevicePathLibCatPrint (Str, "UsbHub");
    break;

  case USB_CLASS_CDCDATA:
    UefiDevicePathLibCatPrint (Str, "UsbCDCData");
    break;

  case USB_CLASS_SMART_CARD:
    UefiDevicePathLibCatPrint (Str, "UsbSmartCard");
    break;

  case USB_CLASS_VIDEO:
    UefiDevicePathLibCatPrint (Str, "UsbVideo");
    break;

  case USB_CLASS_DIAGNOSTIC:
    UefiDevicePathLibCatPrint (Str, "UsbDiagnostic");
    break;

  case USB_CLASS_WIRELESS:
    UefiDevicePathLibCatPrint (Str, "UsbWireless");
    break;

  default:
    IsKnownSubClass = FALSE;
    break;
  }

  if (IsKnownSubClass) {
    UefiDevicePathLibCatPrint (
      Str,
      "(0x%x,0x%x,0x%x,0x%x)",
      UsbClass->VendorId,
      UsbClass->ProductId,
      UsbClass->DeviceSubClass,
      UsbClass->DeviceProtocol
      );
    return;
  }

  if (UsbClass->DeviceClass == USB_CLASS_RESERVE) {
    if (UsbClass->DeviceSubClass == USB_SUBCLASS_FW_UPDATE) {
      UefiDevicePathLibCatPrint (
        Str,
        "UsbDeviceFirmwareUpdate(0x%x,0x%x,0x%x)",
        UsbClass->VendorId,
        UsbClass->ProductId,
        UsbClass->DeviceProtocol
        );
      return;
    } else if (UsbClass->DeviceSubClass == USB_SUBCLASS_IRDA_BRIDGE) {
      UefiDevicePathLibCatPrint (
        Str,
        "UsbIrdaBridge(0x%x,0x%x,0x%x)",
        UsbClass->VendorId,
        UsbClass->ProductId,
        UsbClass->DeviceProtocol
        );
      return;
    } else if (UsbClass->DeviceSubClass == USB_SUBCLASS_TEST) {
      UefiDevicePathLibCatPrint (
        Str,
        "UsbTestAndMeasurement(0x%x,0x%x,0x%x)",
        UsbClass->VendorId,
        UsbClass->ProductId,
        UsbClass->DeviceProtocol
        );
      return;
    }
  }

  UefiDevicePathLibCatPrint (
    Str,
    "UsbClass(0x%x,0x%x,0x%x,0x%x,0x%x)",
    UsbClass->VendorId,
    UsbClass->ProductId,
    UsbClass->DeviceClass,
    UsbClass->DeviceSubClass,
    UsbClass->DeviceProtocol
    );
}

/**
  Converts a SATA device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextSata (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  SATA_DEVICE_PATH *Sata;

  Sata = DevPath;
  UefiDevicePathLibCatPrint (
    Str,
    "Sata(0x%x,0x%x,0x%x)",
    Sata->HBAPortNumber,
    Sata->PortMultiplierPortNumber,
    Sata->Lun
    );
}

/**
  Converts a I20 device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextI2O (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  I2O_DEVICE_PATH *I2ODevPath;

  I2ODevPath = DevPath;
  UefiDevicePathLibCatPrint (Str, "I2O(0x%x)", I2ODevPath->Tid);
}

/**
  Converts a MAC address device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextMacAddr (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  MAC_ADDR_DEVICE_PATH  *MacDevPath;
  UINTN                 HwAddressSize;
  UINTN                 Index;

  MacDevPath = DevPath;

  HwAddressSize = sizeof (EFI_MAC_ADDRESS);
  if (MacDevPath->IfType == 0x01 || MacDevPath->IfType == 0x00) {
    HwAddressSize = 6;
  }

  UefiDevicePathLibCatPrint (Str, "MAC(");

  for (Index = 0; Index < HwAddressSize; Index++) {
    UefiDevicePathLibCatPrint (Str, "%02x", MacDevPath->MacAddress.Addr[Index]);
  }

  UefiDevicePathLibCatPrint (Str, ",0x%x)", MacDevPath->IfType);
}

/**
  Converts network protocol string to its text representation.

  @param Str             The string representative of input device.
  @param Protocol        The network protocol ID.

**/
static VOID
CatNetworkProtocol (
  IN OUT POOL_PRINT  *Str,
  IN UINT16          Protocol
  )
{
  if (Protocol == RFC_1700_TCP_PROTOCOL) {
    UefiDevicePathLibCatPrint (Str, "TCP");
  } else if (Protocol == RFC_1700_UDP_PROTOCOL) {
    UefiDevicePathLibCatPrint (Str, "UDP");
  } else {
    UefiDevicePathLibCatPrint (Str, "0x%x", Protocol);
  }
}

/**
  Converts IP v4 address to its text representation.

  @param Str             The string representative of input device.
  @param Address         The IP v4 address.
**/
static VOID
CatIPv4Address (
  IN OUT POOL_PRINT   *Str,
  IN EFI_IPv4_ADDRESS *Address
  )
{
  UefiDevicePathLibCatPrint (Str, "%d.%d.%d.%d", Address->Addr[0], Address->Addr[1], Address->Addr[2], Address->Addr[3]);
}

/**
  Converts IP v6 address to its text representation.

  @param Str             The string representative of input device.
  @param Address         The IP v6 address.
**/
static VOID
CatIPv6Address (
  IN OUT POOL_PRINT   *Str,
  IN EFI_IPv6_ADDRESS *Address
  )
{
  UefiDevicePathLibCatPrint (
    Str, "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
    Address->Addr[0],  Address->Addr[1],
    Address->Addr[2],  Address->Addr[3],
    Address->Addr[4],  Address->Addr[5],
    Address->Addr[6],  Address->Addr[7],
    Address->Addr[8],  Address->Addr[9],
    Address->Addr[10], Address->Addr[11],
    Address->Addr[12], Address->Addr[13],
    Address->Addr[14], Address->Addr[15]
  );
}

/**
  Converts a IPv4 device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextIPv4 (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  IPv4_DEVICE_PATH  *IPDevPath;

  IPDevPath = DevPath;
  UefiDevicePathLibCatPrint (Str, "IPv4(");
  CatIPv4Address (Str, &IPDevPath->RemoteIpAddress);

  if (DisplayOnly) {
    UefiDevicePathLibCatPrint (Str, ")");
    return ;
  }

  UefiDevicePathLibCatPrint (Str, ",");
  CatNetworkProtocol (Str, IPDevPath->Protocol);

  UefiDevicePathLibCatPrint (Str, ",%s,", IPDevPath->StaticIpAddress ? "Static" : "DHCP");
  CatIPv4Address (Str, &IPDevPath->LocalIpAddress);
  if (DevicePathNodeLength (IPDevPath) == sizeof (IPv4_DEVICE_PATH)) {
    UefiDevicePathLibCatPrint (Str, ",");
    CatIPv4Address (Str, &IPDevPath->GatewayIpAddress);
    UefiDevicePathLibCatPrint (Str, ",");
    CatIPv4Address (Str, &IPDevPath->SubnetMask);
  }
  UefiDevicePathLibCatPrint (Str, ")");
}

/**
  Converts a IPv6 device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextIPv6 (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  IPv6_DEVICE_PATH  *IPDevPath;

  IPDevPath = DevPath;
  UefiDevicePathLibCatPrint (Str, "IPv6(");
  CatIPv6Address (Str, &IPDevPath->RemoteIpAddress);
  if (DisplayOnly) {
    UefiDevicePathLibCatPrint (Str, ")");
    return ;
  }

  UefiDevicePathLibCatPrint (Str, ",");
  CatNetworkProtocol (Str, IPDevPath->Protocol);

  switch (IPDevPath->IpAddressOrigin) {
    case 0:
      UefiDevicePathLibCatPrint (Str, ",Static,");
      break;
    case 1:
      UefiDevicePathLibCatPrint (Str, ",StatelessAutoConfigure,");
      break;
    default:
      UefiDevicePathLibCatPrint (Str, ",StatefulAutoConfigure,");
      break;
  }

  CatIPv6Address (Str, &IPDevPath->LocalIpAddress);

  if (DevicePathNodeLength (IPDevPath) == sizeof (IPv6_DEVICE_PATH)) {
    UefiDevicePathLibCatPrint (Str, ",0x%x,", IPDevPath->PrefixLength);
    CatIPv6Address (Str, &IPDevPath->GatewayIpAddress);
  }
  UefiDevicePathLibCatPrint (Str, ")");
}

/**
  Converts an Infini Band device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextInfiniBand (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  INFINIBAND_DEVICE_PATH  *InfiniBand;

  InfiniBand = DevPath;
  UefiDevicePathLibCatPrint (
    Str,
    "Infiniband(0x%x,%36s,0x%lx,0x%lx,0x%lx)",
    InfiniBand->ResourceFlags,
    G(InfiniBand->PortGid),
    InfiniBand->ServiceId,
    InfiniBand->TargetPortId,
    InfiniBand->DeviceId
    );
}

/**
  Converts a UART device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextUart (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  UART_DEVICE_PATH  *Uart;
  CHAR8             Parity;

  Uart = DevPath;
  switch (Uart->Parity) {
  case 0:
    Parity = 'D';
    break;

  case 1:
    Parity = 'N';
    break;

  case 2:
    Parity = 'E';
    break;

  case 3:
    Parity = 'O';
    break;

  case 4:
    Parity = 'M';
    break;

  case 5:
    Parity = 'S';
    break;

  default:
    Parity = 'x';
    break;
  }

  if (Uart->BaudRate == 0) {
    UefiDevicePathLibCatPrint (Str, "Uart(DEFAULT,");
  } else {
    UefiDevicePathLibCatPrint (Str, "Uart(%ld,", Uart->BaudRate);
  }

  if (Uart->DataBits == 0) {
    UefiDevicePathLibCatPrint (Str, "DEFAULT,");
  } else {
    UefiDevicePathLibCatPrint (Str, "%d,", Uart->DataBits);
  }

  UefiDevicePathLibCatPrint (Str, "%c,", Parity);

  switch (Uart->StopBits) {
  case 0:
    UefiDevicePathLibCatPrint (Str, "D)");
    break;

  case 1:
    UefiDevicePathLibCatPrint (Str, "1)");
    break;

  case 2:
    UefiDevicePathLibCatPrint (Str, "1.5)");
    break;

  case 3:
    UefiDevicePathLibCatPrint (Str, "2)");
    break;

  default:
    UefiDevicePathLibCatPrint (Str, "x)");
    break;
  }
}

/**
  Converts an iSCSI device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextiSCSI (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  ISCSI_DEVICE_PATH_WITH_NAME *ISCSIDevPath;
  UINT16                      Options;

  ISCSIDevPath = DevPath;
  UefiDevicePathLibCatPrint (
    Str,
    "iSCSI(%s,0x%x,0x%lx,",
    ISCSIDevPath->TargetName,
    ISCSIDevPath->TargetPortalGroupTag,
    ISCSIDevPath->Lun
    );

  Options = ISCSIDevPath->LoginOption;
  UefiDevicePathLibCatPrint (Str, "%s,", (((Options >> 1) & 0x0001) != 0) ? "CRC32C" : "None");
  UefiDevicePathLibCatPrint (Str, "%s,", (((Options >> 3) & 0x0001) != 0) ? "CRC32C" : "None");
  if (((Options >> 11) & 0x0001) != 0) {
    UefiDevicePathLibCatPrint (Str, "%s,", "None");
  } else if (((Options >> 12) & 0x0001) != 0) {
    UefiDevicePathLibCatPrint (Str, "%s,", "CHAP_UNI");
  } else {
    UefiDevicePathLibCatPrint (Str, "%s,", "CHAP_BI");

  }

  UefiDevicePathLibCatPrint (Str, "%s)", (ISCSIDevPath->NetworkProtocol == 0) ? "TCP" : "reserved");
}

/**
  Converts a VLAN device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextVlan (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  VLAN_DEVICE_PATH  *Vlan;

  Vlan = DevPath;
  UefiDevicePathLibCatPrint (Str, "Vlan(%d)", Vlan->VlanId);
}

/**
  Converts a Bluetooth device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextBluetooth (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  BLUETOOTH_DEVICE_PATH  *Bluetooth;

  Bluetooth = DevPath;
  UefiDevicePathLibCatPrint (
    Str,
    "Bluetooth(%02x%02x%02x%02x%02x%02x)",
    Bluetooth->BD_ADDR.Address[5],
    Bluetooth->BD_ADDR.Address[4],
    Bluetooth->BD_ADDR.Address[3],
    Bluetooth->BD_ADDR.Address[2],
    Bluetooth->BD_ADDR.Address[1],
    Bluetooth->BD_ADDR.Address[0]
    );
}

/**
  Converts a Wi-Fi device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextWiFi (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  WIFI_DEVICE_PATH      *WiFi;
  UINT8                 SSId[33];

  WiFi = DevPath;

  SSId[32] = '\0';
  CopyMem (SSId, WiFi->SSId, 32);

  UefiDevicePathLibCatPrint (Str, "Wi-Fi(%s)", SSId);
}

/**
  Converts a URI device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextUri (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  URI_DEVICE_PATH    *Uri;
  UINTN              UriLength;
  CHAR8              *UriStr;

  //
  // Uri in the device path may not be null terminated.
  //
  Uri       = DevPath;
  UriLength = DevicePathNodeLength (Uri) - sizeof (URI_DEVICE_PATH);
  UriStr = AllocatePool (UriLength + 1);
  ASSERT (UriStr != NULL);

  CopyMem (UriStr, Uri->Uri, UriLength);
  UriStr[UriLength] = '\0';
  UefiDevicePathLibCatPrint (Str, "Uri(%s)", UriStr);
  FreePool (UriStr);
}

/**
  Converts a Hard drive device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextHardDrive (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  HARDDRIVE_DEVICE_PATH *Hd;

  Hd = DevPath;
  switch (Hd->SignatureType) {
  case SIGNATURE_TYPE_MBR:
    UefiDevicePathLibCatPrint (
      Str,
      "HD(%d,%s,0x%08x,",
      Hd->PartitionNumber,
      "MBR",
//      *((UINT32 *) (&(Hd->Signature[0])))
      le32dec(&(Hd->Signature[0]))
      );
    break;

  case SIGNATURE_TYPE_GUID:
    UefiDevicePathLibCatPrint (
      Str,
      "HD(%d,%s,%36s,",
      Hd->PartitionNumber,
      "GPT",
      G(&(Hd->Signature[0]))
      );
    break;

  default:
    UefiDevicePathLibCatPrint (
      Str,
      "HD(%d,%d,0,",
      Hd->PartitionNumber,
      Hd->SignatureType
      );
    break;
  }

  UefiDevicePathLibCatPrint (Str, "0x%lx,0x%lx)", Hd->PartitionStart, Hd->PartitionSize);
}

/**
  Converts a CDROM device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextCDROM (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  CDROM_DEVICE_PATH *Cd;

  Cd = DevPath;
  if (DisplayOnly) {
    UefiDevicePathLibCatPrint (Str, "CDROM(0x%x)", Cd->BootEntry);
    return ;
  }

  UefiDevicePathLibCatPrint (Str, "CDROM(0x%x,0x%lx,0x%lx)", Cd->BootEntry, Cd->PartitionStart, Cd->PartitionSize);
}

/**
  Converts a File device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextFilePath (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  FILEPATH_DEVICE_PATH  *Fp;
  char *name = NULL;

  Fp = DevPath;
  ucs2_to_utf8(Fp->PathName, &name);
  UefiDevicePathLibCatPrint (Str, "File(%s)", name);
  free(name);
}

/**
  Converts a Media protocol device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextMediaProtocol (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  MEDIA_PROTOCOL_DEVICE_PATH  *MediaProt;

  MediaProt = DevPath;
  UefiDevicePathLibCatPrint (Str, "Media(%36s)", G(&MediaProt->Protocol));
}

/**
  Converts a Firmware Volume device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextFv (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  MEDIA_FW_VOL_DEVICE_PATH  *Fv;

  Fv = DevPath;
  UefiDevicePathLibCatPrint (Str, "Fv(%36s)", G(&Fv->FvName));
}

/**
  Converts a Firmware Volume File device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextFvFile (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH  *FvFile;

  FvFile = DevPath;
  UefiDevicePathLibCatPrint (Str, "FvFile(%36s)", G(&FvFile->FvFileName));
}

/**
  Converts a Relative Offset device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathRelativeOffsetRange (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath,
  IN BOOLEAN              DisplayOnly,
  IN BOOLEAN              AllowShortcuts
  )
{
  MEDIA_RELATIVE_OFFSET_RANGE_DEVICE_PATH *Offset;

  Offset = DevPath;
  UefiDevicePathLibCatPrint (
    Str,
    "Offset(0x%lx,0x%lx)",
    Offset->StartingOffset,
    Offset->EndingOffset
    );
}

/**
  Converts a Ram Disk device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextRamDisk (
  IN OUT POOL_PRINT       *Str,
  IN VOID                 *DevPath,
  IN BOOLEAN              DisplayOnly,
  IN BOOLEAN              AllowShortcuts
  )
{
  MEDIA_RAM_DISK_DEVICE_PATH *RamDisk;

  RamDisk = DevPath;

  if (CompareGuid (&RamDisk->TypeGuid, &gEfiVirtualDiskGuid)) {
    UefiDevicePathLibCatPrint (
      Str,
      "VirtualDisk(0x%lx,0x%lx,%d)",
      LShiftU64 ((UINT64)RamDisk->StartingAddr[1], 32) | RamDisk->StartingAddr[0],
      LShiftU64 ((UINT64)RamDisk->EndingAddr[1], 32) | RamDisk->EndingAddr[0],
      RamDisk->Instance
      );
  } else if (CompareGuid (&RamDisk->TypeGuid, &gEfiVirtualCdGuid)) {
    UefiDevicePathLibCatPrint (
      Str,
      "VirtualCD(0x%lx,0x%lx,%d)",
      LShiftU64 ((UINT64)RamDisk->StartingAddr[1], 32) | RamDisk->StartingAddr[0],
      LShiftU64 ((UINT64)RamDisk->EndingAddr[1], 32) | RamDisk->EndingAddr[0],
      RamDisk->Instance
      );
  } else if (CompareGuid (&RamDisk->TypeGuid, &gEfiPersistentVirtualDiskGuid)) {
    UefiDevicePathLibCatPrint (
      Str,
      "PersistentVirtualDisk(0x%lx,0x%lx,%d)",
      LShiftU64 ((UINT64)RamDisk->StartingAddr[1], 32) | RamDisk->StartingAddr[0],
      LShiftU64 ((UINT64)RamDisk->EndingAddr[1], 32) | RamDisk->EndingAddr[0],
      RamDisk->Instance
      );
  } else if (CompareGuid (&RamDisk->TypeGuid, &gEfiPersistentVirtualCdGuid)) {
    UefiDevicePathLibCatPrint (
      Str,
      "PersistentVirtualCD(0x%lx,0x%lx,%d)",
      LShiftU64 ((UINT64)RamDisk->StartingAddr[1], 32) | RamDisk->StartingAddr[0],
      LShiftU64 ((UINT64)RamDisk->EndingAddr[1], 32) | RamDisk->EndingAddr[0],
      RamDisk->Instance
      );
  } else {
    UefiDevicePathLibCatPrint (
      Str,
      "RamDisk(0x%lx,0x%lx,%d,%36s)",
      LShiftU64 ((UINT64)RamDisk->StartingAddr[1], 32) | RamDisk->StartingAddr[0],
      LShiftU64 ((UINT64)RamDisk->EndingAddr[1], 32) | RamDisk->EndingAddr[0],
      RamDisk->Instance,
      G(&RamDisk->TypeGuid)
      );
  }
}

/**
  Converts a BIOS Boot Specification device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextBBS (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  BBS_BBS_DEVICE_PATH *Bbs;
  const char          *Type;

  Bbs = DevPath;
  switch (Bbs->DeviceType) {
  case BBS_TYPE_FLOPPY:
    Type = "Floppy";
    break;

  case BBS_TYPE_HARDDRIVE:
    Type = "HD";
    break;

  case BBS_TYPE_CDROM:
    Type = "CDROM";
    break;

  case BBS_TYPE_PCMCIA:
    Type = "PCMCIA";
    break;

  case BBS_TYPE_USB:
    Type = "USB";
    break;

  case BBS_TYPE_EMBEDDED_NETWORK:
    Type = "Network";
    break;

  default:
    Type = NULL;
    break;
  }

  if (Type != NULL) {
    UefiDevicePathLibCatPrint (Str, "BBS(%s,%s", Type, Bbs->String);
  } else {
    UefiDevicePathLibCatPrint (Str, "BBS(0x%x,%s", Bbs->DeviceType, Bbs->String);
  }

  if (DisplayOnly) {
    UefiDevicePathLibCatPrint (Str, ")");
    return ;
  }

  UefiDevicePathLibCatPrint (Str, ",0x%x)", Bbs->StatusFlag);
}

/**
  Converts an End-of-Device-Path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextEndInstance (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  UefiDevicePathLibCatPrint (Str, ",");
}

GLOBAL_REMOVE_IF_UNREFERENCED const DEVICE_PATH_TO_TEXT_GENERIC_TABLE mUefiDevicePathLibToTextTableGeneric[] = {
  {HARDWARE_DEVICE_PATH,  "HardwarePath"   },
  {ACPI_DEVICE_PATH,      "AcpiPath"       },
  {MESSAGING_DEVICE_PATH, "Msg"            },
  {MEDIA_DEVICE_PATH,     "MediaPath"      },
  {BBS_DEVICE_PATH,       "BbsPath"        },
  {0, NULL}
};

/**
  Converts an unknown device path structure to its string representative.

  @param Str             The string representative of input device.
  @param DevPath         The input device path structure.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

**/
static VOID
DevPathToTextNodeGeneric (
  IN OUT POOL_PRINT  *Str,
  IN VOID            *DevPath,
  IN BOOLEAN         DisplayOnly,
  IN BOOLEAN         AllowShortcuts
  )
{
  EFI_DEVICE_PATH_PROTOCOL *Node;
  UINTN                    Index;

  Node = DevPath;

  for (Index = 0; mUefiDevicePathLibToTextTableGeneric[Index].Text != NULL; Index++) {
    if (DevicePathType (Node) == mUefiDevicePathLibToTextTableGeneric[Index].Type) {
      break;
    }
  }

  if (mUefiDevicePathLibToTextTableGeneric[Index].Text == NULL) {
    //
    // It's a node whose type cannot be recognized
    //
    UefiDevicePathLibCatPrint (Str, "Path(%d,%d", DevicePathType (Node), DevicePathSubType (Node));
  } else {
    //
    // It's a node whose type can be recognized
    //
    UefiDevicePathLibCatPrint (Str, "%s(%d", mUefiDevicePathLibToTextTableGeneric[Index].Text, DevicePathSubType (Node));
  }

  Index = sizeof (EFI_DEVICE_PATH_PROTOCOL);
  if (Index < DevicePathNodeLength (Node)) {
    UefiDevicePathLibCatPrint (Str, ",");
    for (; Index < DevicePathNodeLength (Node); Index++) {
      UefiDevicePathLibCatPrint (Str, "%02x", ((UINT8 *) Node)[Index]);
    }
  }

  UefiDevicePathLibCatPrint (Str, ")");
}

static const DEVICE_PATH_TO_TEXT_TABLE mUefiDevicePathLibToTextTable[] = {
  {HARDWARE_DEVICE_PATH,  HW_PCI_DP,                        DevPathToTextPci            },
  {HARDWARE_DEVICE_PATH,  HW_PCCARD_DP,                     DevPathToTextPccard         },
  {HARDWARE_DEVICE_PATH,  HW_MEMMAP_DP,                     DevPathToTextMemMap         },
  {HARDWARE_DEVICE_PATH,  HW_VENDOR_DP,                     DevPathToTextVendor         },
  {HARDWARE_DEVICE_PATH,  HW_CONTROLLER_DP,                 DevPathToTextController     },
  {HARDWARE_DEVICE_PATH,  HW_BMC_DP,                        DevPathToTextBmc            },
  {ACPI_DEVICE_PATH,      ACPI_DP,                          DevPathToTextAcpi           },
  {ACPI_DEVICE_PATH,      ACPI_EXTENDED_DP,                 DevPathToTextAcpiEx         },
  {ACPI_DEVICE_PATH,      ACPI_ADR_DP,                      DevPathToTextAcpiAdr        },
  {MESSAGING_DEVICE_PATH, MSG_ATAPI_DP,                     DevPathToTextAtapi          },
  {MESSAGING_DEVICE_PATH, MSG_SCSI_DP,                      DevPathToTextScsi           },
  {MESSAGING_DEVICE_PATH, MSG_FIBRECHANNEL_DP,              DevPathToTextFibre          },
  {MESSAGING_DEVICE_PATH, MSG_FIBRECHANNELEX_DP,            DevPathToTextFibreEx        },
  {MESSAGING_DEVICE_PATH, MSG_SASEX_DP,                     DevPathToTextSasEx          },
  {MESSAGING_DEVICE_PATH, MSG_NVME_NAMESPACE_DP,            DevPathToTextNVMe           },
  {MESSAGING_DEVICE_PATH, MSG_UFS_DP,                       DevPathToTextUfs            },
  {MESSAGING_DEVICE_PATH, MSG_SD_DP,                        DevPathToTextSd             },
  {MESSAGING_DEVICE_PATH, MSG_EMMC_DP,                      DevPathToTextEmmc           },
  {MESSAGING_DEVICE_PATH, MSG_1394_DP,                      DevPathToText1394           },
  {MESSAGING_DEVICE_PATH, MSG_USB_DP,                       DevPathToTextUsb            },
  {MESSAGING_DEVICE_PATH, MSG_USB_WWID_DP,                  DevPathToTextUsbWWID        },
  {MESSAGING_DEVICE_PATH, MSG_DEVICE_LOGICAL_UNIT_DP,       DevPathToTextLogicalUnit    },
  {MESSAGING_DEVICE_PATH, MSG_USB_CLASS_DP,                 DevPathToTextUsbClass       },
  {MESSAGING_DEVICE_PATH, MSG_SATA_DP,                      DevPathToTextSata           },
  {MESSAGING_DEVICE_PATH, MSG_I2O_DP,                       DevPathToTextI2O            },
  {MESSAGING_DEVICE_PATH, MSG_MAC_ADDR_DP,                  DevPathToTextMacAddr        },
  {MESSAGING_DEVICE_PATH, MSG_IPv4_DP,                      DevPathToTextIPv4           },
  {MESSAGING_DEVICE_PATH, MSG_IPv6_DP,                      DevPathToTextIPv6           },
  {MESSAGING_DEVICE_PATH, MSG_INFINIBAND_DP,                DevPathToTextInfiniBand     },
  {MESSAGING_DEVICE_PATH, MSG_UART_DP,                      DevPathToTextUart           },
  {MESSAGING_DEVICE_PATH, MSG_VENDOR_DP,                    DevPathToTextVendor         },
  {MESSAGING_DEVICE_PATH, MSG_ISCSI_DP,                     DevPathToTextiSCSI          },
  {MESSAGING_DEVICE_PATH, MSG_VLAN_DP,                      DevPathToTextVlan           },
  {MESSAGING_DEVICE_PATH, MSG_URI_DP,                       DevPathToTextUri            },
  {MESSAGING_DEVICE_PATH, MSG_BLUETOOTH_DP,                 DevPathToTextBluetooth      },
  {MESSAGING_DEVICE_PATH, MSG_WIFI_DP,                      DevPathToTextWiFi           },
  {MEDIA_DEVICE_PATH,     MEDIA_HARDDRIVE_DP,               DevPathToTextHardDrive      },
  {MEDIA_DEVICE_PATH,     MEDIA_CDROM_DP,                   DevPathToTextCDROM          },
  {MEDIA_DEVICE_PATH,     MEDIA_VENDOR_DP,                  DevPathToTextVendor         },
  {MEDIA_DEVICE_PATH,     MEDIA_PROTOCOL_DP,                DevPathToTextMediaProtocol  },
  {MEDIA_DEVICE_PATH,     MEDIA_FILEPATH_DP,                DevPathToTextFilePath       },
  {MEDIA_DEVICE_PATH,     MEDIA_PIWG_FW_VOL_DP,             DevPathToTextFv             },
  {MEDIA_DEVICE_PATH,     MEDIA_PIWG_FW_FILE_DP,            DevPathToTextFvFile         },
  {MEDIA_DEVICE_PATH,     MEDIA_RELATIVE_OFFSET_RANGE_DP,   DevPathRelativeOffsetRange  },
  {MEDIA_DEVICE_PATH,     MEDIA_RAM_DISK_DP,                DevPathToTextRamDisk        },
  {BBS_DEVICE_PATH,       BBS_BBS_DP,                       DevPathToTextBBS            },
  {END_DEVICE_PATH_TYPE,  END_INSTANCE_DEVICE_PATH_SUBTYPE, DevPathToTextEndInstance    },
  {0, 0, NULL}
};

/**
  Converts a device node to its string representation.

  @param DeviceNode        A Pointer to the device node to be converted.
  @param DisplayOnly       If DisplayOnly is TRUE, then the shorter text representation
                           of the display node is used, where applicable. If DisplayOnly
                           is FALSE, then the longer text representation of the display node
                           is used.
  @param AllowShortcuts    If AllowShortcuts is TRUE, then the shortcut forms of text
                           representation for a device node can be used, where applicable.

  @return A pointer to the allocated text representation of the device node or NULL if DeviceNode
          is NULL or there was insufficient memory.

**/
static char *
EFIAPI
UefiDevicePathLibConvertDeviceNodeToText (
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *DeviceNode,
  IN BOOLEAN                         DisplayOnly,
  IN BOOLEAN                         AllowShortcuts
  )
{
  POOL_PRINT          Str;
  UINTN               Index;
  DEVICE_PATH_TO_TEXT ToText;
  EFI_DEVICE_PATH_PROTOCOL *Node;

  if (DeviceNode == NULL) {
    return NULL;
  }

  ZeroMem (&Str, sizeof (Str));

  //
  // Process the device path node
  // If not found, use a generic function
  //
  Node = __DECONST(EFI_DEVICE_PATH_PROTOCOL *, DeviceNode);
  ToText = DevPathToTextNodeGeneric;
  for (Index = 0; mUefiDevicePathLibToTextTable[Index].Function != NULL; Index++) {
    if (DevicePathType (DeviceNode) == mUefiDevicePathLibToTextTable[Index].Type &&
        DevicePathSubType (DeviceNode) == mUefiDevicePathLibToTextTable[Index].SubType
        ) {
      ToText = mUefiDevicePathLibToTextTable[Index].Function;
      break;
    }
  }

  //
  // Print this node
  //
  ToText (&Str, (VOID *) Node, DisplayOnly, AllowShortcuts);

  ASSERT (Str.Str != NULL);
  return Str.Str;
}

/**
  Converts a device path to its text representation.

  @param DevicePath      A Pointer to the device to be converted.
  @param DisplayOnly     If DisplayOnly is TRUE, then the shorter text representation
                         of the display node is used, where applicable. If DisplayOnly
                         is FALSE, then the longer text representation of the display node
                         is used.
  @param AllowShortcuts  If AllowShortcuts is TRUE, then the shortcut forms of text
                         representation for a device node can be used, where applicable.

  @return A pointer to the allocated text representation of the device path or
          NULL if DeviceNode is NULL or there was insufficient memory.

**/
static char *
EFIAPI
UefiDevicePathLibConvertDevicePathToText (
  IN CONST EFI_DEVICE_PATH_PROTOCOL   *DevicePath,
  IN BOOLEAN                          DisplayOnly,
  IN BOOLEAN                          AllowShortcuts
  )
{
  POOL_PRINT               Str;
  EFI_DEVICE_PATH_PROTOCOL *Node;
  EFI_DEVICE_PATH_PROTOCOL *AlignedNode;
  UINTN                    Index;
  DEVICE_PATH_TO_TEXT      ToText;

  if (DevicePath == NULL) {
    return NULL;
  }

  ZeroMem (&Str, sizeof (Str));

  //
  // Process each device path node
  //
  Node = __DECONST(EFI_DEVICE_PATH_PROTOCOL *, DevicePath);
  while (!IsDevicePathEnd (Node)) {
    //
    // Find the handler to dump this device path node
    // If not found, use a generic function
    //
    ToText = DevPathToTextNodeGeneric;
    for (Index = 0; mUefiDevicePathLibToTextTable[Index].Function != NULL; Index += 1) {

      if (DevicePathType (Node) == mUefiDevicePathLibToTextTable[Index].Type &&
          DevicePathSubType (Node) == mUefiDevicePathLibToTextTable[Index].SubType
          ) {
        ToText = mUefiDevicePathLibToTextTable[Index].Function;
        break;
      }
    }
    //
    //  Put a path separator in if needed
    //
    if ((Str.Count != 0) && (ToText != DevPathToTextEndInstance)) {
      if (Str.Str[Str.Count] != ',') {
        UefiDevicePathLibCatPrint (&Str, "/");
      }
    }

    AlignedNode = AllocateCopyPool (DevicePathNodeLength (Node), Node);
    //
    // Print this node of the device path
    //
    ToText (&Str, AlignedNode, DisplayOnly, AllowShortcuts);
    FreePool (AlignedNode);

    //
    // Next device path node
    //
    Node = NextDevicePathNode (Node);
  }

  if (Str.Str == NULL) {
    return AllocateZeroPool (sizeof (CHAR16));
  } else {
    return Str.Str;
  }
}

ssize_t
efidp_format_device_path(char *buf, size_t len, const_efidp dp, ssize_t max)
{
	char *str;
	ssize_t retval;

	/*
	 * Basic sanity check on the device path.
	 */
	if (!IsDevicePathValid((CONST EFI_DEVICE_PATH_PROTOCOL *) dp, max)) {
		*buf = '\0';
		return 0;
	}

	str = UefiDevicePathLibConvertDevicePathToText (
		__DECONST(EFI_DEVICE_PATH_PROTOCOL *, dp), FALSE, TRUE);
	if (str == NULL)
		return -1;
	strlcpy(buf, str, len);
	retval = strlen(str);
	free(str);

	return retval;
}

ssize_t
efidp_format_device_path_node(char *buf, size_t len, const_efidp dp)
{
	char *str;
	ssize_t retval;

	str = UefiDevicePathLibConvertDeviceNodeToText (
		__DECONST(EFI_DEVICE_PATH_PROTOCOL *, dp), FALSE, TRUE);
	if (str == NULL)
		return -1;
	strlcpy(buf, str, len);
	retval = strlen(str);
	free(str);

	return retval;
}

size_t
efidp_size(const_efidp dp)
{

	return GetDevicePathSize(__DECONST(EFI_DEVICE_PATH_PROTOCOL *, dp));
}

char *
efidp_extract_file_path(const_efidp dp)
{
	const FILEPATH_DEVICE_PATH  *fp;
	char *name = NULL;

	fp = (const void *)dp;
	ucs2_to_utf8(fp->PathName, &name);
	return name;
}
