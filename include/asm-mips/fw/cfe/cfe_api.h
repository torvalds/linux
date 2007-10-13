/*
 * Copyright (C) 2000, 2001, 2002 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/*  *********************************************************************
    *
    *  Broadcom Common Firmware Environment (CFE)
    *
    *  Device function prototypes		File: cfe_api.h
    *
    *  This file contains declarations for doing callbacks to
    *  cfe from an application.  It should be the only header
    *  needed by the application to use this library
    *
    *  Authors:  Mitch Lichtenberg, Chris Demetriou
    *
    ********************************************************************* */

#ifndef CFE_API_H
#define CFE_API_H

/*
 * Apply customizations here for different OSes.  These need to:
 *	* typedef uint64_t, int64_t, intptr_t, uintptr_t.
 *	* define cfe_strlen() if use of an existing function is desired.
 *	* define CFE_API_IMPL_NAMESPACE if API functions are to use
 *	  names in the implementation namespace.
 * Also, optionally, if the build environment does not do so automatically,
 * CFE_API_* can be defined here as desired.
 */
/* Begin customization. */
#include <linux/types.h>
#include <linux/string.h>

typedef long intptr_t;

#define cfe_strlen strlen

#define CFE_API_ALL
#define CFE_API_STRLEN_CUSTOM
/* End customization. */


/*  *********************************************************************
    *  Constants
    ********************************************************************* */

/* Seal indicating CFE's presence, passed to user program. */
#define CFE_EPTSEAL 0x43464531

#define CFE_MI_RESERVED	0	/* memory is reserved, do not use */
#define CFE_MI_AVAILABLE 1	/* memory is available */

#define CFE_FLG_WARMSTART     0x00000001
#define CFE_FLG_FULL_ARENA    0x00000001
#define CFE_FLG_ENV_PERMANENT 0x00000001

#define CFE_CPU_CMD_START 1
#define CFE_CPU_CMD_STOP 0

#define CFE_STDHANDLE_CONSOLE	0

#define CFE_DEV_NETWORK 	1
#define CFE_DEV_DISK		2
#define CFE_DEV_FLASH		3
#define CFE_DEV_SERIAL		4
#define CFE_DEV_CPU		5
#define CFE_DEV_NVRAM		6
#define CFE_DEV_CLOCK           7
#define CFE_DEV_OTHER		8
#define CFE_DEV_MASK		0x0F

#define CFE_CACHE_FLUSH_D	1
#define CFE_CACHE_INVAL_I	2
#define CFE_CACHE_INVAL_D	4
#define CFE_CACHE_INVAL_L2	8

#define CFE_FWI_64BIT		0x00000001
#define CFE_FWI_32BIT		0x00000002
#define CFE_FWI_RELOC		0x00000004
#define CFE_FWI_UNCACHED	0x00000008
#define CFE_FWI_MULTICPU	0x00000010
#define CFE_FWI_FUNCSIM		0x00000020
#define CFE_FWI_RTLSIM		0x00000040

typedef struct {
	int64_t fwi_version;		/* major, minor, eco version */
	int64_t fwi_totalmem;		/* total installed mem */
	int64_t fwi_flags;		/* various flags */
	int64_t fwi_boardid;		/* board ID */
	int64_t fwi_bootarea_va;	/* VA of boot area */
	int64_t fwi_bootarea_pa;	/* PA of boot area */
	int64_t fwi_bootarea_size;	/* size of boot area */
} cfe_fwinfo_t;


/*
 * cfe_strlen is handled specially: If already defined, it has been
 * overridden in this environment with a standard strlen-like function.
 */
#ifdef cfe_strlen
# define CFE_API_STRLEN_CUSTOM
#else
# ifdef CFE_API_IMPL_NAMESPACE
#  define cfe_strlen(a)			__cfe_strlen(a)
# endif
int cfe_strlen(char *name);
#endif

/*
 * Defines and prototypes for functions which take no arguments.
 */
#ifdef CFE_API_IMPL_NAMESPACE
int64_t __cfe_getticks(void);
#define cfe_getticks()			__cfe_getticks()
#else
int64_t cfe_getticks(void);
#endif

/*
 * Defines and prototypes for the rest of the functions.
 */
#ifdef CFE_API_IMPL_NAMESPACE
#define cfe_close(a)			__cfe_close(a)
#define cfe_cpu_start(a, b, c, d, e)	__cfe_cpu_start(a, b, c, d, e)
#define cfe_cpu_stop(a)			__cfe_cpu_stop(a)
#define cfe_enumenv(a, b, d, e, f)	__cfe_enumenv(a, b, d, e, f)
#define cfe_enummem(a, b, c, d, e)	__cfe_enummem(a, b, c, d, e)
#define cfe_exit(a, b)			__cfe_exit(a, b)
#define cfe_flushcache(a)		__cfe_cacheflush(a)
#define cfe_getdevinfo(a)		__cfe_getdevinfo(a)
#define cfe_getenv(a, b, c)		__cfe_getenv(a, b, c)
#define cfe_getfwinfo(a)		__cfe_getfwinfo(a)
#define cfe_getstdhandle(a)		__cfe_getstdhandle(a)
#define cfe_init(a, b)			__cfe_init(a, b)
#define cfe_inpstat(a)			__cfe_inpstat(a)
#define cfe_ioctl(a, b, c, d, e, f)	__cfe_ioctl(a, b, c, d, e, f)
#define cfe_open(a)			__cfe_open(a)
#define cfe_read(a, b, c)		__cfe_read(a, b, c)
#define cfe_readblk(a, b, c, d)		__cfe_readblk(a, b, c, d)
#define cfe_setenv(a, b)		__cfe_setenv(a, b)
#define cfe_write(a, b, c)		__cfe_write(a, b, c)
#define cfe_writeblk(a, b, c, d		__cfe_writeblk(a, b, c, d)
#endif				/* CFE_API_IMPL_NAMESPACE */

int cfe_close(int handle);
int cfe_cpu_start(int cpu, void (*fn) (void), long sp, long gp, long a1);
int cfe_cpu_stop(int cpu);
int cfe_enumenv(int idx, char *name, int namelen, char *val, int vallen);
int cfe_enummem(int idx, int flags, uint64_t * start, uint64_t * length,
		uint64_t * type);
int cfe_exit(int warm, int status);
int cfe_flushcache(int flg);
int cfe_getdevinfo(char *name);
int cfe_getenv(char *name, char *dest, int destlen);
int cfe_getfwinfo(cfe_fwinfo_t * info);
int cfe_getstdhandle(int flg);
int cfe_init(uint64_t handle, uint64_t ept);
int cfe_inpstat(int handle);
int cfe_ioctl(int handle, unsigned int ioctlnum, unsigned char *buffer,
	      int length, int *retlen, uint64_t offset);
int cfe_open(char *name);
int cfe_read(int handle, unsigned char *buffer, int length);
int cfe_readblk(int handle, int64_t offset, unsigned char *buffer,
		int length);
int cfe_setenv(char *name, char *val);
int cfe_write(int handle, unsigned char *buffer, int length);
int cfe_writeblk(int handle, int64_t offset, unsigned char *buffer,
		 int length);

#endif				/* CFE_API_H */
