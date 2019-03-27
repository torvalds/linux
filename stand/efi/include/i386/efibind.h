/* $FreeBSD$ */
/*++

Copyright (c)  1999 - 2003 Intel Corporation. All rights reserved
This software and associated documentation (if any) is furnished
under a license and may only be used or copied in accordance
with the terms of the license. Except as permitted by such
license, no part of this software or documentation may be
reproduced, stored in a retrieval system, or transmitted in any
form or by any means without the express written consent of
Intel Corporation.

Module Name:

    efefind.h

Abstract:

    EFI to compile bindings




Revision History

--*/

#pragma pack()


#ifdef __FreeBSD__
#include <sys/stdint.h>
#else
//
// Basic int types of various widths
//

#if (__STDC_VERSION__ < 199901L )

    // No ANSI C 1999/2000 stdint.h integer width declarations 

    #ifdef _MSC_EXTENSIONS

        // Use Microsoft C compiler integer width declarations 

        typedef unsigned __int64    uint64_t;
        typedef __int64             int64_t;
        typedef unsigned __int32    uint32_t;
        typedef __int32             int32_t;
        typedef unsigned short      uint16_t;
        typedef short               int16_t;
        typedef unsigned char       uint8_t;
        typedef char                int8_t;
    #else             
        #ifdef UNIX_LP64

            // Use LP64 programming model from C_FLAGS for integer width declarations 

            typedef unsigned long       uint64_t;
            typedef long                int64_t;
            typedef unsigned int        uint32_t;
            typedef int                 int32_t;
            typedef unsigned short      uint16_t;
            typedef short               int16_t;
            typedef unsigned char       uint8_t;
            typedef char                int8_t;
        #else

            // Assume P64 programming model from C_FLAGS for integer width declarations 

            typedef unsigned long long  uint64_t;
            typedef long long           int64_t;
            typedef unsigned int        uint32_t;
            typedef int                 int32_t;
            typedef unsigned short      uint16_t;
            typedef short               int16_t;
            typedef unsigned char       uint8_t;
            typedef char                int8_t;
        #endif
    #endif
#endif
#endif	/* __FreeBSD__ */

//
// Basic EFI types of various widths
//

#ifndef ACPI_THREAD_ID		/* ACPI's definitions are fine, use those */
#define ACPI_USE_SYSTEM_INTTYPES 1	/* Tell ACPI we've defined types */

typedef uint64_t   UINT64;
typedef int64_t    INT64;

#ifndef _BASETSD_H_
    typedef uint32_t   UINT32;
    typedef int32_t    INT32;
#endif

typedef uint16_t   UINT16;
typedef int16_t    INT16;
typedef uint8_t    UINT8;
typedef int8_t     INT8;

#endif

#undef VOID
#define VOID    void


typedef int32_t    INTN;
typedef uint32_t   UINTN;

#ifdef EFI_NT_EMULATOR
    #define POST_CODE(_Data)
#else    
    #ifdef EFI_DEBUG
#define POST_CODE(_Data)    __asm mov eax,(_Data) __asm out 0x80,al
    #else
        #define POST_CODE(_Data)
    #endif  
#endif

#define EFIERR(a)           (0x80000000 | a)
#define EFI_ERROR_MASK      0x80000000
#define EFIERR_OEM(a)       (0xc0000000 | a)      


#define BAD_POINTER         0xFBFBFBFB
#define MAX_ADDRESS         0xFFFFFFFF

#define BREAKPOINT()        __asm { int 3 }

//
// Pointers must be aligned to these address to function
//

#define MIN_ALIGNMENT_SIZE  4

#define ALIGN_VARIABLE(Value ,Adjustment) \
            (UINTN)Adjustment = 0; \
            if((UINTN)Value % MIN_ALIGNMENT_SIZE) \
                (UINTN)Adjustment = MIN_ALIGNMENT_SIZE - ((UINTN)Value % MIN_ALIGNMENT_SIZE); \
            Value = (UINTN)Value + (UINTN)Adjustment


//
// Define macros to build data structure signatures from characters.
//

#define EFI_SIGNATURE_16(A,B)             ((A) | (B<<8))
#define EFI_SIGNATURE_32(A,B,C,D)         (EFI_SIGNATURE_16(A,B)     | (EFI_SIGNATURE_16(C,D)     << 16))
#define EFI_SIGNATURE_64(A,B,C,D,E,F,G,H) (EFI_SIGNATURE_32(A,B,C,D) | ((UINT64)(EFI_SIGNATURE_32(E,F,G,H)) << 32))

//
// EFIAPI - prototype calling convention for EFI function pointers
// BOOTSERVICE - prototype for implementation of a boot service interface
// RUNTIMESERVICE - prototype for implementation of a runtime service interface
// RUNTIMEFUNCTION - prototype for implementation of a runtime function that is not a service
// RUNTIME_CODE - pragma macro for declaring runtime code    
//

#ifndef EFIAPI                  // Forces EFI calling conventions reguardless of compiler options 
    #ifdef _MSC_EXTENSIONS
        #define EFIAPI __cdecl  // Force C calling convention for Microsoft C compiler 
    #else
        #define EFIAPI          // Substitute expresion to force C calling convention 
    #endif
#endif

#define BOOTSERVICE
//#define RUNTIMESERVICE(proto,a)    alloc_text("rtcode",a); proto a
//#define RUNTIMEFUNCTION(proto,a)   alloc_text("rtcode",a); proto a
#define RUNTIMESERVICE
#define RUNTIMEFUNCTION


#define RUNTIME_CODE(a)         alloc_text("rtcode", a)
#define BEGIN_RUNTIME_DATA()    data_seg("rtdata")
#define END_RUNTIME_DATA()      data_seg()

#define VOLATILE    volatile

#define MEMORY_FENCE()    

#ifdef EFI_NO_INTERFACE_DECL
  #define EFI_FORWARD_DECLARATION(x)
  #define EFI_INTERFACE_DECL(x)
#else
  #define EFI_FORWARD_DECLARATION(x) typedef struct _##x x
  #define EFI_INTERFACE_DECL(x) typedef struct x
#endif

#ifdef EFI_NT_EMULATOR

//
// To help ensure proper coding of integrated drivers, they are
// compiled as DLLs.  In NT they require a dll init entry pointer.
// The macro puts a stub entry point into the DLL so it will load.
//

#define EFI_DRIVER_ENTRY_POINT(InitFunction)            \
    EFI_STATUS                                          \
    InitFunction (                                      \
      EFI_HANDLE  ImageHandle,                          \
      EFI_SYSTEM_TABLE  *SystemTable                    \
      );                                                \
                                                        \
    UINTN                                               \
    __stdcall                                           \
    _DllMainCRTStartup (                                \
        UINTN    Inst,                                  \
        UINTN    reason_for_call,                       \
        VOID    *rserved                                \
        )                                               \
    {                                                   \
        return 1;                                       \
    }                                                   \
                                                        \
    int                                                 \
    __declspec( dllexport )                             \
    __cdecl                                             \
    InitializeDriver (                                  \
        void *ImageHandle,                              \
        void *SystemTable                               \
        )                                               \
    {                                                   \
        return InitFunction(ImageHandle, SystemTable);  \
    }


    #define LOAD_INTERNAL_DRIVER(_if, type, name, entry)      \
        (_if)->LoadInternal(type, name, NULL)             

#else // EFI_NT_EMULATOR 

//
// When build similar to FW, then link everything together as
// one big module.
//

    #define EFI_DRIVER_ENTRY_POINT(InitFunction)

    #define LOAD_INTERNAL_DRIVER(_if, type, name, entry)    \
            (_if)->LoadInternal(type, name, entry)

#endif // EFI_FW_NT 

#ifdef __FreeBSD__
#define INTERFACE_DECL(x) struct x
#else
//
// Some compilers don't support the forward reference construct:
//  typedef struct XXXXX
//
// The following macro provide a workaround for such cases.
//
#ifdef NO_INTERFACE_DECL
#define INTERFACE_DECL(x)
#else
#define INTERFACE_DECL(x) typedef struct x
#endif
#endif	/* __FreeBSD__ */

#ifdef _MSC_EXTENSIONS
#pragma warning ( disable : 4731 )  // Suppress warnings about modification of EBP
#endif

