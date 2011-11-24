/*******************************************************************************

            Copyright (c) 2004 by Nanoradio AB 

This software is copyrighted by and is the sole property of Nanoradio AB.
All rights, title, ownership, or other interests in the software remain the
property of Nanoradio AB.  This software may only be used in accordance
with the corresponding license agreement.  Any unauthorized use, duplication,
transmission, distribution, or disclosure of this software is expressly forbidden.

This Copyright notice may not be removed or modified without prior written
consent of Nanoradio AB.

Nanoradio AB reserves the right to modify this software without notice.

Nanoradio AB 
Torshamnsgatan 39                       info@nanoradio.se
164 40 Kista                            http://www.nanoradio.se
SWEDEN
******************************************************************************/
/*---------------------------------------------------------------------------*/
/*! \file systypes.h
\brief  This module contains type definitions common to the complete system. */
/*---------------------------------------------------------------------------*/
#ifndef SYSTYPES_H
#define SYSTYPES_H

/* No external includes in kernel mode (for Linux) */
#if HAVE_ANSI_INTTYPES 
#ifdef  __KERNEL__
#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19)
#include <linux/config.h>
#endif
#include <linux/types.h>

/* these probably don't belong here, but what the heck */
#include <stdarg.h>
#include <linux/string.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24)
#if defined(__i386) || defined(__arm__)
typedef unsigned int uintptr_t;
#else
#error define uintptr_t
#endif
#endif

#else
#include <inttypes.h>
#endif
#endif


/* C O N S T A N T S / M A C R O S *******************************************/
#define __far   /*!< Old style definition for cr16 far. */
#ifndef FAR
#define FAR     /*!< New style definition for cr16 far. */
#endif
#define LARGE

#define TRACE_SIM   printf
#define TRACE_SIM1  printf
#define TRACE_SIM2  printf
#define TRACE_SIM3  printf
#define TRACE_SIM4  printf
#define TRACE_SIM5  printf
#define TRACE_SIM6  printf
#define TRACE_SIM16 printf
#define TRACE_SIM17 printf

/*! If TEST_LOCAL is defined all static declarations are removed to expose the
module internal functions to the test engine. It is expected that TEST_LOCAL
is defined as a make parameter "-D". */
#if defined(TEST_LOCAL)
#define STATIC
/*! Avoid using static_t in new code. The _t implies that this is a type. But
it is not. It is a MACRO!! */
#define static_t
#else
#define STATIC static
#define static_t static
#endif

/* Defined when compiling with:
crcc #define __CR__
cl #define _WIN32
tcc #define __thumb
armcc #define __arm */

#ifndef NULL
#define NULL  0
#endif

#ifndef NIL
#define NIL   0
#endif

/* Type BOOL literals */
#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE  1
#endif

#ifndef DUMMY
#define DUMMY  0
#endif

#define MAX_UINT8  0xFF
#define MAX_INT8   0x7F
#define MAX_UINT16 0xFFFF
#define MAX_INT16  0x7FFF
#define MAX_UINT32 0xFFFFFFFF
#define MAX_INT32  0x7FFFFFFF

/* Used in splitword */
#define W_LSB	0
#define W_MSB	1

/*! Use the TOUCH macro to get rid of compiler warings when function arguments
are unused */
#define TOUCH(x) x=x

#define ARRAY_SZ( array ) ( sizeof( array ) / sizeof( array[ 0 ] ) )

/*---------------------------------------------------------------------------*/
/*! With NORMAL_CAST all cast MACROS are expanded to the appropriate cast.
Undef NORMAL_CAST to get compiler warnings for code inspection. */
/*---------------------------------------------------------------------------*/
#define NORMAL_CAST

#if defined (NORMAL_CAST)
#define INT8_CAST(value) (int8_t)(value)
#define UINT8_CAST(value) (uint8_t)(value)
#define INT16_CAST(value) (int16_t)(value)
#define UINT16_CAST(value) (uint16_t)(value)
#define INT32_CAST(value) (int32_t)(value)
#define UINT32_CAST(value) (uint32_t)(value)
#else
#define INT8_CAST(value)
#define UINT8_CAST(value)
#define INT16_CAST(value)
#define UINT16_CAST(value)
#define INT32_CAST(value)
#define UINT32_CAST(value)
#endif

/*---------------------------------------------------------------------------*/
/*! Usage of the following cast macros is recommended to increase readability
as well as make it possible to efficient search of casts. */
/*---------------------------------------------------------------------------*/
/*! This cast macro shall be used for conversion between different structure
representations of the same data. E.g. Package and package header. */
#define STATIC_CAST(cast, value) (cast)(value)

/*! This cast macro shall be used to remove the const and volatile type
qualifiers. */
#define CONST_CAST(cast, value) (cast)(value)   /*!< Remove const */

/*! This cast macro allows any pointer to be converted into any other pointer
type. Choose one of the above if applicable. */
#define REINTERPRET_CAST(cast, value) (cast)(value)

/* T Y P E D E F ' S *********************************************************/
typedef int bool_t;
typedef unsigned long bool32; /*< Who needs this !! */

typedef struct
{
   uint32_t lo;
   uint16_t hi;
}uint48_lo32_t;

   #define CONST        const
   #define DATA_UBYTEINT            unsigned char
   #define IDATA_UBYTEINT           unsigned char
   #define IDATA_UWORDINT           unsigned short int 
   #define DATA_CHAR                unsigned char
   #define IDATA_CHAR               unsigned char
   #define XRAM_UBYTEINT            unsigned char
   #define XRAM_BYTEINT             signed   char
   #define XRAM_UWORDINT            unsigned short int 
   
   #define DATA                                  
   #define IO_CHAR         volatile unsigned char
   #define IO_UBYTEINT     volatile unsigned char
   #define IO_BYTEINT      volatile signed   char
   #define IO_UWORDINT     volatile unsigned short int  
   #define IO_WORDINT      volatile signed   short int 

   #define ROM_STORAGE(_type)           _type             /* Object in read-only memory */
   #define ROM_STORAGE_PTR(_type)       _type *           /* Pointer to object in read-only memory */
   #define ROM_STORAGE_ROM_PTR(_type)   _type *          /* Read-only pointer to object in read-only memory */

   #define STATIC_CONST static const 

#endif /*#ifndef SYSTYPES_H*/
/******************************** END OF FILE *********************************/
