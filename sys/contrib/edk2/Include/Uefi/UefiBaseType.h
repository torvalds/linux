/** @file
  Defines data types and constants introduced in UEFI.

Copyright (c) 2006 - 2017, Intel Corporation. All rights reserved.<BR>
Portions copyright (c) 2011 - 2016, ARM Ltd. All rights reserved.<BR>

This program and the accompanying materials are licensed and made available under 
the terms and conditions of the BSD License that accompanies this distribution.  
The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php.                                          
    
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#ifndef __UEFI_BASETYPE_H__
#define __UEFI_BASETYPE_H__

#include <Base.h>

//
// Basic data type definitions introduced in UEFI.
//

///
/// 128-bit buffer containing a unique identifier value.
///
typedef GUID                      EFI_GUID;
///
/// Function return status for EFI API.
///
typedef RETURN_STATUS             EFI_STATUS;
///
/// A collection of related interfaces.
///
typedef VOID                      *EFI_HANDLE;
///
/// Handle to an event structure.
///
typedef VOID                      *EFI_EVENT;
///
/// Task priority level.
///
typedef UINTN                     EFI_TPL;
///
/// Logical block address.
///
typedef UINT64                    EFI_LBA;

///
/// 64-bit physical memory address.
///
typedef UINT64                    EFI_PHYSICAL_ADDRESS;

///
/// 64-bit virtual memory address.
///
typedef UINT64                    EFI_VIRTUAL_ADDRESS;

///
/// EFI Time Abstraction:
///  Year:       1900 - 9999
///  Month:      1 - 12
///  Day:        1 - 31
///  Hour:       0 - 23
///  Minute:     0 - 59
///  Second:     0 - 59
///  Nanosecond: 0 - 999,999,999
///  TimeZone:   -1440 to 1440 or 2047
///
typedef struct {
  UINT16  Year;
  UINT8   Month;
  UINT8   Day;
  UINT8   Hour;
  UINT8   Minute;
  UINT8   Second;
  UINT8   Pad1;
  UINT32  Nanosecond;
  INT16   TimeZone;
  UINT8   Daylight;
  UINT8   Pad2;
} EFI_TIME;


///
/// 4-byte buffer. An IPv4 internet protocol address.
///
typedef IPv4_ADDRESS EFI_IPv4_ADDRESS;

///
/// 16-byte buffer. An IPv6 internet protocol address.
///
typedef IPv6_ADDRESS EFI_IPv6_ADDRESS;

///
/// 32-byte buffer containing a network Media Access Control address.
///
typedef struct {
  UINT8 Addr[32];
} EFI_MAC_ADDRESS;

///
/// 16-byte buffer aligned on a 4-byte boundary.
/// An IPv4 or IPv6 internet protocol address.
///
typedef union {
  UINT32            Addr[4];
  EFI_IPv4_ADDRESS  v4;
  EFI_IPv6_ADDRESS  v6;
} EFI_IP_ADDRESS;


///
/// Enumeration of EFI_STATUS.
///@{ 
#define EFI_SUCCESS               RETURN_SUCCESS              
#define EFI_LOAD_ERROR            RETURN_LOAD_ERROR           
#define EFI_INVALID_PARAMETER     RETURN_INVALID_PARAMETER    
#define EFI_UNSUPPORTED           RETURN_UNSUPPORTED          
#define EFI_BAD_BUFFER_SIZE       RETURN_BAD_BUFFER_SIZE      
#define EFI_BUFFER_TOO_SMALL      RETURN_BUFFER_TOO_SMALL     
#define EFI_NOT_READY             RETURN_NOT_READY            
#define EFI_DEVICE_ERROR          RETURN_DEVICE_ERROR         
#define EFI_WRITE_PROTECTED       RETURN_WRITE_PROTECTED      
#define EFI_OUT_OF_RESOURCES      RETURN_OUT_OF_RESOURCES     
#define EFI_VOLUME_CORRUPTED      RETURN_VOLUME_CORRUPTED     
#define EFI_VOLUME_FULL           RETURN_VOLUME_FULL          
#define EFI_NO_MEDIA              RETURN_NO_MEDIA             
#define EFI_MEDIA_CHANGED         RETURN_MEDIA_CHANGED        
#define EFI_NOT_FOUND             RETURN_NOT_FOUND            
#define EFI_ACCESS_DENIED         RETURN_ACCESS_DENIED        
#define EFI_NO_RESPONSE           RETURN_NO_RESPONSE          
#define EFI_NO_MAPPING            RETURN_NO_MAPPING           
#define EFI_TIMEOUT               RETURN_TIMEOUT              
#define EFI_NOT_STARTED           RETURN_NOT_STARTED          
#define EFI_ALREADY_STARTED       RETURN_ALREADY_STARTED      
#define EFI_ABORTED               RETURN_ABORTED              
#define EFI_ICMP_ERROR            RETURN_ICMP_ERROR           
#define EFI_TFTP_ERROR            RETURN_TFTP_ERROR           
#define EFI_PROTOCOL_ERROR        RETURN_PROTOCOL_ERROR       
#define EFI_INCOMPATIBLE_VERSION  RETURN_INCOMPATIBLE_VERSION 
#define EFI_SECURITY_VIOLATION    RETURN_SECURITY_VIOLATION   
#define EFI_CRC_ERROR             RETURN_CRC_ERROR   
#define EFI_END_OF_MEDIA          RETURN_END_OF_MEDIA
#define EFI_END_OF_FILE           RETURN_END_OF_FILE
#define EFI_INVALID_LANGUAGE      RETURN_INVALID_LANGUAGE
#define EFI_COMPROMISED_DATA      RETURN_COMPROMISED_DATA
#define EFI_HTTP_ERROR            RETURN_HTTP_ERROR

#define EFI_WARN_UNKNOWN_GLYPH    RETURN_WARN_UNKNOWN_GLYPH   
#define EFI_WARN_DELETE_FAILURE   RETURN_WARN_DELETE_FAILURE  
#define EFI_WARN_WRITE_FAILURE    RETURN_WARN_WRITE_FAILURE   
#define EFI_WARN_BUFFER_TOO_SMALL RETURN_WARN_BUFFER_TOO_SMALL
#define EFI_WARN_STALE_DATA       RETURN_WARN_STALE_DATA
#define EFI_WARN_FILE_SYSTEM      RETURN_WARN_FILE_SYSTEM
///@}

///
/// Define macro to encode the status code.
/// 
#define EFIERR(_a)                ENCODE_ERROR(_a)

#define EFI_ERROR(A)              RETURN_ERROR(A)

///
/// ICMP error definitions
///@{
#define EFI_NETWORK_UNREACHABLE   EFIERR(100)
#define EFI_HOST_UNREACHABLE      EFIERR(101) 
#define EFI_PROTOCOL_UNREACHABLE  EFIERR(102)
#define EFI_PORT_UNREACHABLE      EFIERR(103)
///@}

///
/// Tcp connection status definitions
///@{
#define EFI_CONNECTION_FIN        EFIERR(104)
#define EFI_CONNECTION_RESET      EFIERR(105)
#define EFI_CONNECTION_REFUSED    EFIERR(106)
///@}

//
// The EFI memory allocation functions work in units of EFI_PAGEs that are
// 4KB. This should in no way be confused with the page size of the processor.
// An EFI_PAGE is just the quanta of memory in EFI.
//
#define EFI_PAGE_SIZE             SIZE_4KB
#define EFI_PAGE_MASK             0xFFF
#define EFI_PAGE_SHIFT            12

/**
  Macro that converts a size, in bytes, to a number of EFI_PAGESs.

  @param  Size      A size in bytes.  This parameter is assumed to be type UINTN.  
                    Passing in a parameter that is larger than UINTN may produce 
                    unexpected results.

  @return  The number of EFI_PAGESs associated with the number of bytes specified
           by Size.

**/
#define EFI_SIZE_TO_PAGES(Size)  (((Size) >> EFI_PAGE_SHIFT) + (((Size) & EFI_PAGE_MASK) ? 1 : 0))

/**
  Macro that converts a number of EFI_PAGEs to a size in bytes.

  @param  Pages     The number of EFI_PAGES.  This parameter is assumed to be 
                    type UINTN.  Passing in a parameter that is larger than 
                    UINTN may produce unexpected results.

  @return  The number of bytes associated with the number of EFI_PAGEs specified 
           by Pages.
  
**/
#define EFI_PAGES_TO_SIZE(Pages)  ((Pages) << EFI_PAGE_SHIFT)

///
/// PE32+ Machine type for IA32 UEFI images.
///
#define EFI_IMAGE_MACHINE_IA32            0x014C

///
/// PE32+ Machine type for IA64 UEFI images.
///
#define EFI_IMAGE_MACHINE_IA64            0x0200

///
/// PE32+ Machine type for EBC UEFI images.
///
#define EFI_IMAGE_MACHINE_EBC             0x0EBC

///
/// PE32+ Machine type for X64 UEFI images.
///
#define EFI_IMAGE_MACHINE_X64             0x8664

///
/// PE32+ Machine type for ARM mixed ARM and Thumb/Thumb2 images.
///
#define EFI_IMAGE_MACHINE_ARMTHUMB_MIXED  0x01C2

///
/// PE32+ Machine type for AARCH64 A64 images.
///
#define EFI_IMAGE_MACHINE_AARCH64  0xAA64


#if   defined (MDE_CPU_IA32)

#define EFI_IMAGE_MACHINE_TYPE_SUPPORTED(Machine) \
  (((Machine) == EFI_IMAGE_MACHINE_IA32) || ((Machine) == EFI_IMAGE_MACHINE_EBC))

#define EFI_IMAGE_MACHINE_CROSS_TYPE_SUPPORTED(Machine) ((Machine) == EFI_IMAGE_MACHINE_X64) 

#elif defined (MDE_CPU_IPF)

#define EFI_IMAGE_MACHINE_TYPE_SUPPORTED(Machine) \
  (((Machine) == EFI_IMAGE_MACHINE_IA64) || ((Machine) == EFI_IMAGE_MACHINE_EBC))

#define EFI_IMAGE_MACHINE_CROSS_TYPE_SUPPORTED(Machine) (FALSE) 

#elif defined (MDE_CPU_X64)

#define EFI_IMAGE_MACHINE_TYPE_SUPPORTED(Machine) \
  (((Machine) == EFI_IMAGE_MACHINE_X64) || ((Machine) == EFI_IMAGE_MACHINE_EBC))

#define EFI_IMAGE_MACHINE_CROSS_TYPE_SUPPORTED(Machine) ((Machine) == EFI_IMAGE_MACHINE_IA32) 

#elif defined (MDE_CPU_ARM)

#define EFI_IMAGE_MACHINE_TYPE_SUPPORTED(Machine) \
  (((Machine) == EFI_IMAGE_MACHINE_ARMTHUMB_MIXED) || ((Machine) == EFI_IMAGE_MACHINE_EBC))

#define EFI_IMAGE_MACHINE_CROSS_TYPE_SUPPORTED(Machine) ((Machine) == EFI_IMAGE_MACHINE_ARMTHUMB_MIXED) 

#elif defined (MDE_CPU_AARCH64)

#define EFI_IMAGE_MACHINE_TYPE_SUPPORTED(Machine) \
  (((Machine) == EFI_IMAGE_MACHINE_AARCH64) || ((Machine) == EFI_IMAGE_MACHINE_EBC))

#define EFI_IMAGE_MACHINE_CROSS_TYPE_SUPPORTED(Machine) (FALSE)

#elif defined (MDE_CPU_EBC)

///
/// This is just to make sure you can cross compile with the EBC compiler.
/// It does not make sense to have a PE loader coded in EBC. 
///
#define EFI_IMAGE_MACHINE_TYPE_SUPPORTED(Machine) ((Machine) == EFI_IMAGE_MACHINE_EBC)

#define EFI_IMAGE_MACHINE_CROSS_TYPE_SUPPORTED(Machine) (FALSE) 

#else
#error Unknown Processor Type
#endif

#endif
