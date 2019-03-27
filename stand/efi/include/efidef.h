/* $FreeBSD$ */
#ifndef _EFI_DEF_H
#define _EFI_DEF_H

/*++

Copyright (c)  1999 - 2002 Intel Corporation. All rights reserved
This software and associated documentation (if any) is furnished
under a license and may only be used or copied in accordance
with the terms of the license. Except as permitted by such
license, no part of this software or documentation may be
reproduced, stored in a retrieval system, or transmitted in any
form or by any means without the express written consent of
Intel Corporation.

Module Name:

    efidef.h

Abstract:

    EFI definitions




Revision History

--*/

typedef UINT16          CHAR16;
typedef UINT8           CHAR8;
#ifndef ACPI_THREAD_ID		/* ACPI's definitions are fine */
typedef UINT8           BOOLEAN;
#endif

#ifndef TRUE
    #define TRUE    ((BOOLEAN) 1)
    #define FALSE   ((BOOLEAN) 0)
#endif

#ifndef NULL
    #define NULL    ((VOID *) 0)
#endif

typedef UINTN           EFI_STATUS;
typedef UINT64          EFI_LBA;
typedef UINTN           EFI_TPL;
typedef VOID            *EFI_HANDLE;
typedef VOID            *EFI_EVENT;


//
// Prototype argument decoration for EFI parameters to indicate
// their direction
//
// IN - argument is passed into the function
// OUT - argument (pointer) is returned from the function
// OPTIONAL - argument is optional
//

#ifndef IN
    #define IN
    #define OUT
    #define OPTIONAL
#endif


//
// A GUID
//

typedef struct {          
    UINT32  Data1;
    UINT16  Data2;
    UINT16  Data3;
    UINT8   Data4[8]; 
} EFI_GUID;


//
// Time
//

typedef struct {          
    UINT16      Year;       // 1998 - 20XX
    UINT8       Month;      // 1 - 12
    UINT8       Day;        // 1 - 31
    UINT8       Hour;       // 0 - 23
    UINT8       Minute;     // 0 - 59
    UINT8       Second;     // 0 - 59
    UINT8       Pad1;
    UINT32      Nanosecond; // 0 - 999,999,999
    INT16       TimeZone;   // -1440 to 1440 or 2047
    UINT8       Daylight;
    UINT8       Pad2;
} EFI_TIME;

// Bit definitions for EFI_TIME.Daylight
#define EFI_TIME_ADJUST_DAYLIGHT    0x01
#define EFI_TIME_IN_DAYLIGHT        0x02

// Value definition for EFI_TIME.TimeZone
#define EFI_UNSPECIFIED_TIMEZONE    0x07FF



//
// Networking
//

typedef struct {
    UINT8                   Addr[4];
} EFI_IPv4_ADDRESS;

typedef struct {
    UINT8                   Addr[16];
} EFI_IPv6_ADDRESS;

typedef struct {
    UINT8                   Addr[32];
} EFI_MAC_ADDRESS;

typedef struct {
    UINT32 ReceivedQueueTimeoutValue;
    UINT32 TransmitQueueTimeoutValue;
    UINT16 ProtocolTypeFilter;
    BOOLEAN EnableUnicastReceive;
    BOOLEAN EnableMulticastReceive;
    BOOLEAN EnableBroadcastReceive;
    BOOLEAN EnablePromiscuousReceive;
    BOOLEAN FlushQueuesOnReset;
    BOOLEAN EnableReceiveTimestamps;
    BOOLEAN DisableBackgroundPolling;
} EFI_MANAGED_NETWORK_CONFIG_DATA;

//
// Memory
//

typedef UINT64          EFI_PHYSICAL_ADDRESS;
typedef UINT64          EFI_VIRTUAL_ADDRESS;

typedef enum {
    AllocateAnyPages,
    AllocateMaxAddress,
    AllocateAddress,
    MaxAllocateType
} EFI_ALLOCATE_TYPE;

//Preseve the attr on any range supplied.
//ConventialMemory must have WB,SR,SW when supplied.
//When allocating from ConventialMemory always make it WB,SR,SW
//When returning to ConventialMemory always make it WB,SR,SW
//When getting the memory map, or on RT for runtime types


typedef enum {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

// possible caching types for the memory range
#define EFI_MEMORY_UC			0x0000000000000001
#define EFI_MEMORY_WC			0x0000000000000002
#define EFI_MEMORY_WT			0x0000000000000004
#define EFI_MEMORY_WB			0x0000000000000008
#define EFI_MEMORY_UCE			0x0000000000000010  

// physical memory protection on range 
#define EFI_MEMORY_WP			0x0000000000001000
#define EFI_MEMORY_RP			0x0000000000002000
#define EFI_MEMORY_XP			0x0000000000004000
#define	EFI_MEMORY_NV			0x0000000000008000
#define	EFI_MEMORY_MORE_RELIABLE	0x0000000000010000
#define	EFI_MEMORY_RO			0x0000000000020000

// range requires a runtime mapping
#define EFI_MEMORY_RUNTIME		0x8000000000000000

#define EFI_MEMORY_DESCRIPTOR_VERSION  1
typedef struct {
    UINT32                          Type;           // Field size is 32 bits followed by 32 bit pad
    UINT32                          Pad;
    EFI_PHYSICAL_ADDRESS            PhysicalStart;  // Field size is 64 bits
    EFI_VIRTUAL_ADDRESS             VirtualStart;   // Field size is 64 bits
    UINT64                          NumberOfPages;  // Field size is 64 bits
    UINT64                          Attribute;      // Field size is 64 bits
} EFI_MEMORY_DESCRIPTOR;

//
// International Language
//

typedef UINT8   ISO_639_2;
#define ISO_639_2_ENTRY_SIZE    3

//
//
//

#define EFI_PAGE_SIZE   4096
#define EFI_PAGE_MASK   0xFFF
#define EFI_PAGE_SHIFT  12

#define EFI_SIZE_TO_PAGES(a)  \
    ( ((a) >> EFI_PAGE_SHIFT) + (((a) & EFI_PAGE_MASK) ? 1 : 0) )

#endif
