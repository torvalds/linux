/*
 *  Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/************************************************************************/
/*                                                                      */
/*  PROJECT : exFAT & FAT12/16/32 File System                           */
/*  FILE    : exfat_global.h                                            */
/*  PURPOSE : Header File for exFAT Global Definitions & Misc Functions */
/*                                                                      */
/*----------------------------------------------------------------------*/
/*  NOTES                                                               */
/*                                                                      */
/*----------------------------------------------------------------------*/
/*  REVISION HISTORY (Ver 0.9)                                          */
/*                                                                      */
/*  - 2010.11.15 [Joosun Hahn] : first writing                          */
/*                                                                      */
/************************************************************************/

#ifndef _EXFAT_GLOBAL_H
#define _EXFAT_GLOBAL_H

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/fs.h>

#include "exfat_config.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

  /*======================================================================*/
	/*                                                                      */
	/*                     CONSTANT & MACRO DEFINITIONS                     */
	/*                                                                      */
	/*======================================================================*/

	/*----------------------------------------------------------------------*/
	/*  Well-Known Constants (DO NOT CHANGE THIS PART !!)                   */
	/*----------------------------------------------------------------------*/

#ifndef TRUE
#define TRUE                    1
#endif
#ifndef FALSE
#define FALSE                   0
#endif
#ifndef OK
#define OK                      0
#endif
#ifndef FAIL
#define FAIL                    1
#endif
#ifndef NULL
#define NULL                    0
#endif

	/* Min/Max macro */
#define MIN(a, b)               (((a) < (b)) ? (a) : (b))
#define MAX(a, b)               (((a) > (b)) ? (a) : (b))

	/*======================================================================*/
	/*                                                                      */
	/*                         TYPE DEFINITIONS                             */
	/*                  (CHANGE THIS PART IF REQUIRED)                      */
	/*                                                                      */
	/*======================================================================*/

	/* type definitions for primitive types;
	   these should be re-defined to meet its size for each OS platform;
	   these should be used instead of primitive types for portability. */

	typedef char                    INT8;   // 1 byte signed integer
	typedef short                   INT16;  // 2 byte signed integer
	typedef int                     INT32;  // 4 byte signed integer
	typedef long long               INT64;  // 8 byte signed integer

	typedef unsigned char           UINT8;  // 1 byte unsigned integer
	typedef unsigned short          UINT16; // 2 byte unsigned integer
	typedef unsigned int            UINT32; // 4 byte unsigned integer
	typedef unsigned long long      UINT64; // 8 byte ussigned integer

	typedef unsigned char           BOOL;


	/*======================================================================*/
	/*                                                                      */
	/*        LIBRARY FUNCTION DECLARATIONS -- WELL-KNOWN FUNCTIONS         */
	/*                  (CHANGE THIS PART IF REQUIRED)                      */
	/*                                                                      */
	/*======================================================================*/

	/*----------------------------------------------------------------------*/
	/*  Memory Manipulation Macros & Functions                              */
	/*----------------------------------------------------------------------*/

#ifdef MALLOC
#undef MALLOC
#endif
#ifdef FREE
#undef FREE
#endif
#ifdef MEMSET
#undef MEMSET
#endif
#ifdef MEMCPY
#undef MEMCPY
#endif
#ifdef MEMCMP
#undef MEMCMP
#endif

#define MALLOC(size)                    kmalloc(size, GFP_KERNEL)
#define FREE(mem)                       if (mem) kfree(mem)
#define MEMSET(mem, value, size)        memset(mem, value, size)
#define MEMCPY(dest, src, size)         memcpy(dest, src, size)
#define MEMCMP(mem1, mem2, size)        memcmp(mem1, mem2, size)
#define COPY_DENTRY(dest, src)				memcpy(dest, src, sizeof(DENTRY_T))

	/*----------------------------------------------------------------------*/
	/*  String Manipulation Macros & Functions                              */
	/*----------------------------------------------------------------------*/

#define STRCPY(dest, src)               strcpy(dest, src)
#define STRNCPY(dest, src, n)           strncpy(dest, src, n)
#define STRCAT(str1, str2)              strcat(str1, str2)
#define STRCMP(str1, str2)              strcmp(str1, str2)
#define STRNCMP(str1, str2, n)          strncmp(str1, str2, n)
#define STRLEN(str)                     strlen(str)

	INT32 __wstrchr(UINT16 *str, UINT16 wchar);
	INT32 __wstrlen(UINT16 *str);

#define WSTRCHR(str, wchar)             __wstrchr(str, wchar)
#define WSTRLEN(str)                    __wstrlen(str)

	/*----------------------------------------------------------------------*/
	/*  Debugging Macros & Functions                              */
	/*  EXFAT_CONFIG_DEBUG_MSG is configured in exfat_config.h                              */
	/*----------------------------------------------------------------------*/
#if EXFAT_CONFIG_DEBUG_MSG
#define PRINTK(...)			\
	do {								\
		printk("[EXFAT] " __VA_ARGS__);	\
	} while(0)
#else
#define PRINTK(...)
#endif

	/*======================================================================*/
	/*                                                                      */
	/*       LIBRARY FUNCTION DECLARATIONS -- OTHER UTILITY FUNCTIONS       */
	/*                    (DO NOT CHANGE THIS PART !!)                      */
	/*                                                                      */
	/*======================================================================*/

	/*----------------------------------------------------------------------*/
	/*  Bitmap Manipulation Functions                                       */
	/*----------------------------------------------------------------------*/

	void    Bitmap_set_all(UINT8 *bitmap, INT32 mapsize);
	void    Bitmap_clear_all(UINT8 *bitmap, INT32 mapsize);
	INT32   Bitmap_test(UINT8 *bitmap, INT32 i);
	void    Bitmap_set(UINT8 *bitmap, INT32 i);
	void    Bitmap_clear(UINT8 *bitmpa, INT32 i);
	void    Bitmap_nbits_set(UINT8 *bitmap, INT32 offset, INT32 nbits);
	void    Bitmap_nbits_clear(UINT8 *bitmap, INT32 offset, INT32 nbits);

	/*----------------------------------------------------------------------*/
	/*  Miscellaneous Library Functions                                     */
	/*----------------------------------------------------------------------*/

	void    my_itoa(INT8 *buf, INT32 v);
	INT32   my_log2(UINT32 v);

	/*======================================================================*/
	/*                                                                      */
	/*                    DEFINITIONS FOR DEBUGGING                         */
	/*                  (CHANGE THIS PART IF REQUIRED)                      */
	/*                                                                      */
	/*======================================================================*/

	/* debug message ouput macro */
#ifdef PRINT
#undef PRINT
#endif

#define PRINT                   printk

#ifdef __cplusplus
}
#endif /* __cplusplus  */

#endif /* _EXFAT_GLOBAL_H */

/* end of exfat_global.h */
