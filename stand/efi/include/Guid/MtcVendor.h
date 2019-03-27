/* $FreeBSD$ */
/** @file
  GUID is for MTC variable.

Copyright (c) 2011, Intel Corporation. All rights reserved.<BR>
This program and the accompanying materials are licensed and made available under
the terms and conditions of the BSD License that accompanies this distribution.
The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php.

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#ifndef __MTC_VENDOR_GUID_H__
#define __MTC_VENDOR_GUID_H__

//
// Vendor GUID of the variable for the high part of monotonic counter (UINT32).
//
#define MTC_VENDOR_GUID \
  { 0xeb704011, 0x1402, 0x11d3, { 0x8e, 0x77, 0x0, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

//
// Name of the variable for the high part of monotonic counter
//
#define MTC_VARIABLE_NAME "MTC"

extern EFI_GUID gMtcVendorGuid;

#endif
