/* $FreeBSD$ */
/** @file
  EDID Discovered Protocol from the UEFI 2.0 specification.

  This protocol is placed on the video output device child handle. It represents
  the EDID information being used for the output device represented by the child handle.

  Copyright (c) 2006 - 2012, Intel Corporation. All rights reserved.<BR>
  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#ifndef __EDID_DISCOVERED_H__
#define __EDID_DISCOVERED_H__

#define EFI_EDID_DISCOVERED_PROTOCOL_GUID \
  { \
    0x1c0c34f6, 0xd380, 0x41fa, {0xa0, 0x49, 0x8a, 0xd0, 0x6c, 0x1a, 0x66, 0xaa } \
  }

///
/// This protocol contains the EDID information retrieved from a video output device.
///
typedef struct {
  ///
  /// The size, in bytes, of the Edid buffer. 0 if no EDID information
  /// is available from the video output device. Otherwise, it must be a
  /// minimum of 128 bytes.
  ///
  UINT32   SizeOfEdid;

  ///
  /// A pointer to a read-only array of bytes that contains the EDID
  /// information for an active video output device. This pointer is
  /// NULL if no EDID information is available for the video output
  /// device. The minimum size of a valid Edid buffer is 128 bytes.
  /// EDID information is defined in the E-EDID EEPROM
  /// specification published by VESA (www.vesa.org).
  ///
  UINT8    *Edid;
} EFI_EDID_DISCOVERED_PROTOCOL;

extern EFI_GUID gEfiEdidDiscoveredProtocolGuid;

#endif
