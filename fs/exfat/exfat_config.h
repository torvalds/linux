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
/*  FILE    : exfat_config.h                                            */
/*  PURPOSE : Header File for exFAT Configuable Policies                */
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

#ifndef _EXFAT_CONFIG_H
#define _EXFAT_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*======================================================================*/
/*                                                                      */
/*                        FFS CONFIGURATIONS                            */
/*                  (CHANGE THIS PART IF REQUIRED)                      */
/*                                                                      */
/*======================================================================*/

/*----------------------------------------------------------------------*/
/*  Target OS Platform                                                  */
/*----------------------------------------------------------------------*/

#define OS_NONOS                1
#define OS_LINUX                2

#define FFS_CONFIG_OS           OS_LINUX

/*----------------------------------------------------------------------*/
/* Set this definition to 1 to support APIs with pointer parameters     */
/*     to 32-bit variables (e.g. read, write, seek, get_filesize)       */
/*----------------------------------------------------------------------*/
#define FFS_CONFIG_LEGACY_32BIT_API     0

/*----------------------------------------------------------------------*/
/* Set this definition to 1 to support APIs with pointer parameters     */
/*     to 32-bit variables (e.g. read, write, seek, get_filesize)       */
/*----------------------------------------------------------------------*/
#define FFS_CONFIG_LEGACY_32BIT_API     0

/*----------------------------------------------------------------------*/
/* Set appropriate definitions to 1's to support the languages          */
/*----------------------------------------------------------------------*/
#define FFS_CONFIG_SUPPORT_CP1250       1       // Central Europe
#define FFS_CONFIG_SUPPORT_CP1251       1       // Cyrillic
#define FFS_CONFIG_SUPPORT_CP1252       1       // Latin I
#define FFS_CONFIG_SUPPORT_CP1253       1       // Greek
#define FFS_CONFIG_SUPPORT_CP1254       1       // Turkish
#define FFS_CONFIG_SUPPORT_CP1255       1       // Hebrew
#define FFS_CONFIG_SUPPORT_CP1256       1       // Arabic
#define FFS_CONFIG_SUPPORT_CP1257       1       // Baltic
#define FFS_CONFIG_SUPPORT_CP1258       1       // Vietnamese
#define FFS_CONFIG_SUPPORT_CP874        1       // Thai
#define FFS_CONFIG_SUPPORT_CP932        1       // Japanese
#define FFS_CONFIG_SUPPORT_CP936        1       // Simplified Chinese
#define FFS_CONFIG_SUPPORT_CP949        1       // Korean
#define FFS_CONFIG_SUPPORT_CP950        1       // Traditional Chinese
#define FFS_CONFIG_SUPPORT_UTF8         1       // UTF8 encoding

/*----------------------------------------------------------------------*/
/* Feature Config                                                       */
/*----------------------------------------------------------------------*/
#define EXFAT_CONFIG_DISCARD		1	// mount option -o discard support
#define EXFAT_CONFIG_KERNEL_DEBUG	1	// kernel debug features via ioctl
#define EXFAT_CONFIG_DEBUG_MSG		0	// debugging message on/off

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _EXFAT_CONFIG_H */

/* end of exfat_config.h */
