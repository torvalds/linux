//------------------------------------------------------------------------------
//
// This file contains the definitions of the basic atheros data types.
// It is used to map the data types in atheros files to a platform specific
// type.
// Copyright (c) 2004-2010 Atheros Communications Inc.
// All rights reserved.
//
// 
// The software source and binaries included in this development package are
// licensed, not sold. You, or your company, received the package under one
// or more license agreements. The rights granted to you are specifically
// listed in these license agreement(s). All other rights remain with Atheros
// Communications, Inc., its subsidiaries, or the respective owner including
// those listed on the included copyright notices.  Distribution of any
// portion of this package must be in strict compliance with the license
// agreement(s) terms.
// </copyright>
// 
// <summary>
// 	Wifi driver for AR6002
// </summary>
//
//
// Author(s): ="Atheros"
//------------------------------------------------------------------------------

#ifndef _ATHTYPES_LINUX_H_
#define _ATHTYPES_LINUX_H_

#include <sys/types.h>

typedef int8_t      A_INT8;
typedef int16_t     A_INT16;
typedef int32_t     A_INT32;
typedef int64_t     A_INT64;

typedef u_int8_t     A_UINT8;
typedef u_int16_t    A_UINT16;
typedef u_int32_t    A_UINT32;
typedef u_int64_t    A_UINT64;

typedef int             A_BOOL;
typedef char            A_CHAR;
typedef unsigned char   A_UCHAR;
typedef unsigned long   A_ATH_TIMER;


#endif /* _ATHTYPES_LINUX_H_ */
