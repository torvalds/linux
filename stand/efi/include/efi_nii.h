/* $FreeBSD$ */
#ifndef _EFI_NII_H
#define _EFI_NII_H

/*++
Copyright (c)  1999 - 2002 Intel Corporation. All rights reserved
This software and associated documentation (if any) is furnished
under a license and may only be used or copied in accordance
with the terms of the license. Except as permitted by such
license, no part of this software or documentation may be
reproduced, stored in a retrieval system, or transmitted in any
form or by any means without the express written consent of
Intel Corporation.

Module name:
    efi_nii.h

Abstract:

Revision history:
    2000-Feb-18 M(f)J   GUID updated.
                Structure order changed for machine word alignment.
                Added StringId[4] to structure.
                
    2000-Feb-14 M(f)J   Genesis.
--*/

#define EFI_NETWORK_INTERFACE_IDENTIFIER_PROTOCOL \
    { 0xE18541CD, 0xF755, 0x4f73, {0x92, 0x8D, 0x64, 0x3C, 0x8A, 0x79, 0xB2, 0x29} }
#define EFI_NETWORK_INTERFACE_IDENTIFIER_PROTOCOL_31 \
    { 0x1ACED566, 0x76ED, 0x4218, {0xBC, 0x81, 0x76, 0x7F, 0x1F, 0x97, 0x7A, 0x89} }

#define EFI_NETWORK_INTERFACE_IDENTIFIER_INTERFACE_REVISION 0x00010000
#define EFI_NETWORK_INTERFACE_IDENTIFIER_INTERFACE_REVISION_31 0x00010001

typedef enum {
    EfiNetworkInterfaceUndi = 1
} EFI_NETWORK_INTERFACE_TYPE;

typedef struct {

    UINT64 Revision;
    // Revision of the network interface identifier protocol interface.

    UINT64 ID;
    // Address of the first byte of the identifying structure for this
    // network interface.  This is set to zero if there is no structure.
    //
    // For PXE/UNDI this is the first byte of the !PXE structure.

    UINT64 ImageAddr;
    // Address of the UNrelocated driver/ROM image.  This is set
    // to zero if there is no driver/ROM image.
    //
    // For 16-bit UNDI, this is the first byte of the option ROM in
    // upper memory.
    //
    // For 32/64-bit S/W UNDI, this is the first byte of the EFI ROM
    // image.
    //
    // For H/W UNDI, this is set to zero.

    UINT32 ImageSize;
    // Size of the UNrelocated driver/ROM image of this network interface.
    // This is set to zero if there is no driver/ROM image.

    CHAR8 StringId[4];
    // 4 char ASCII string to go in class identifier (option 60) in DHCP
    // and Boot Server discover packets.
    // For EfiNetworkInterfaceUndi this field is "UNDI".
    // For EfiNetworkInterfaceSnp this field is "SNPN".

    UINT8 Type;
    UINT8 MajorVer;
    UINT8 MinorVer;
    // Information to be placed into the PXE DHCP and Discover packets.
    // This is the network interface type and version number that will
    // be placed into DHCP option 94 (client network interface identifier).
    BOOLEAN Ipv6Supported;
	UINT8   IfNum;	// interface number to be used with pxeid structure
} EFI_NETWORK_INTERFACE_IDENTIFIER_INTERFACE;

extern EFI_GUID NetworkInterfaceIdentifierProtocol;
extern EFI_GUID NetworkInterfaceIdentifierProtocol_31;

#endif // _EFI_NII_H
