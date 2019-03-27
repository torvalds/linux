/* from: Broadcom Id: cfe_api_int.h,v 1.22 2003/02/07 17:27:56 cgd Exp $ */

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2000, 2001, 2002
 * Broadcom Corporation. All rights reserved.
 *
 * This software is furnished under license and may be used and copied only
 * in accordance with the following terms and conditions.  Subject to these
 * conditions, you may download, copy, install, use, modify and distribute
 * modified or unmodified copies of this software in source and/or binary
 * form. No title or ownership is transferred hereby.
 *
 * 1) Any source code used, modified or distributed must reproduce and
 *    retain this copyright notice and list of conditions as they appear in
 *    the source file.
 *
 * 2) No right is granted to use any trade name, trademark, or logo of
 *    Broadcom Corporation.  The "Broadcom Corporation" name may not be
 *    used to endorse or promote products derived from this software
 *    without the prior written permission of Broadcom Corporation.
 *
 * 3) THIS SOFTWARE IS PROVIDED "AS-IS" AND ANY EXPRESS OR IMPLIED
 *    WARRANTIES, INCLUDING BUT NOT LIMITED TO, ANY IMPLIED WARRANTIES OF
 *    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR
 *    NON-INFRINGEMENT ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM BE LIABLE
 *    FOR ANY DAMAGES WHATSOEVER, AND IN PARTICULAR, BROADCOM SHALL NOT BE
 *    LIABLE FOR DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *    BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *    WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 *    OR OTHERWISE), EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*  *********************************************************************
    *
    *  Broadcom Common Firmware Environment (CFE)
    *
    *  Device function prototypes		File: cfe_api_int.h
    *
    *  This header defines all internal types and macros for the
    *  library.  This is stuff that's not exported to an app
    *  using the library.
    *
    *  Authors:  Mitch Lichtenberg, Chris Demetriou
    *
    ********************************************************************* */

#ifndef CFE_API_INT_H
#define CFE_API_INT_H

/*  *********************************************************************
    *  Constants
    ********************************************************************* */

#define CFE_CMD_FW_GETINFO	0
#define CFE_CMD_FW_RESTART	1
#define CFE_CMD_FW_BOOT		2
#define CFE_CMD_FW_CPUCTL	3
#define CFE_CMD_FW_GETTIME      4
#define CFE_CMD_FW_MEMENUM	5
#define CFE_CMD_FW_FLUSHCACHE	6

#define CFE_CMD_DEV_GETHANDLE	9
#define CFE_CMD_DEV_ENUM	10
#define CFE_CMD_DEV_OPEN	11
#define CFE_CMD_DEV_INPSTAT	12
#define CFE_CMD_DEV_READ	13
#define CFE_CMD_DEV_WRITE	14
#define CFE_CMD_DEV_IOCTL	15
#define CFE_CMD_DEV_CLOSE	16
#define CFE_CMD_DEV_GETINFO	17

#define CFE_CMD_ENV_ENUM	20
#define CFE_CMD_ENV_GET		22
#define CFE_CMD_ENV_SET		23
#define CFE_CMD_ENV_DEL		24

#define CFE_CMD_MAX		32

#define CFE_CMD_VENDOR_USE	0x8000	/* codes above this are for customer use */

/*  *********************************************************************
    *  Structures
    ********************************************************************* */

typedef uint64_t cfe_xuint_t;
typedef int64_t cfe_xint_t;
typedef int64_t cfe_xptr_t;

typedef struct xiocb_buffer_s {
    cfe_xuint_t   buf_offset;		/* offset on device (bytes) */
    cfe_xptr_t 	  buf_ptr;		/* pointer to a buffer */
    cfe_xuint_t   buf_length;		/* length of this buffer */
    cfe_xuint_t   buf_retlen;		/* returned length (for read ops) */
    cfe_xuint_t   buf_ioctlcmd;		/* IOCTL command (used only for IOCTLs) */
} xiocb_buffer_t;

#define buf_devflags buf_ioctlcmd	/* returned device info flags */

typedef struct xiocb_inpstat_s {
    cfe_xuint_t inp_status;		/* 1 means input available */
} xiocb_inpstat_t;

typedef struct xiocb_envbuf_s {
    cfe_xint_t enum_idx;		/* 0-based enumeration index */
    cfe_xptr_t name_ptr;		/* name string buffer */
    cfe_xint_t name_length;		/* size of name buffer */
    cfe_xptr_t val_ptr;			/* value string buffer */
    cfe_xint_t val_length;		/* size of value string buffer */
} xiocb_envbuf_t;

typedef struct xiocb_cpuctl_s {
    cfe_xuint_t  cpu_number;		/* cpu number to control */
    cfe_xuint_t  cpu_command;		/* command to issue to CPU */
    cfe_xuint_t  start_addr;		/* CPU start address */
    cfe_xuint_t  gp_val;		/* starting GP value */
    cfe_xuint_t  sp_val;		/* starting SP value */
    cfe_xuint_t  a1_val;		/* starting A1 value */
} xiocb_cpuctl_t;

typedef struct xiocb_time_s {
    cfe_xint_t ticks;			/* current time in ticks */
} xiocb_time_t;

typedef struct xiocb_exitstat_s {
    cfe_xint_t status;
} xiocb_exitstat_t;

typedef struct xiocb_meminfo_s {
    cfe_xint_t  mi_idx;			/* 0-based enumeration index */
    cfe_xint_t  mi_type;		/* type of memory block */
    cfe_xuint_t mi_addr;		/* physical start address */
    cfe_xuint_t mi_size;		/* block size */
} xiocb_meminfo_t;

typedef struct xiocb_fwinfo_s {
    cfe_xint_t fwi_version;		/* major, minor, eco version */
    cfe_xint_t fwi_totalmem;		/* total installed mem */
    cfe_xint_t fwi_flags;		/* various flags */
    cfe_xint_t fwi_boardid;		/* board ID */
    cfe_xint_t fwi_bootarea_va;		/* VA of boot area */
    cfe_xint_t fwi_bootarea_pa;		/* PA of boot area */
    cfe_xint_t fwi_bootarea_size;	/* size of boot area */
    cfe_xint_t fwi_reserved1;
    cfe_xint_t fwi_reserved2;
    cfe_xint_t fwi_reserved3;
} xiocb_fwinfo_t;

typedef struct cfe_xiocb_s {
    cfe_xuint_t xiocb_fcode;		/* IOCB function code */
    cfe_xint_t  xiocb_status;		/* return status */
    cfe_xint_t  xiocb_handle;		/* file/device handle */
    cfe_xuint_t xiocb_flags;		/* flags for this IOCB */
    cfe_xuint_t xiocb_psize;		/* size of parameter list */
    union {
	xiocb_buffer_t  xiocb_buffer;	/* buffer parameters */
	xiocb_inpstat_t xiocb_inpstat;	/* input status parameters */
	xiocb_envbuf_t  xiocb_envbuf;	/* environment function parameters */
	xiocb_cpuctl_t  xiocb_cpuctl;	/* CPU control parameters */
	xiocb_time_t    xiocb_time;	/* timer parameters */
	xiocb_meminfo_t xiocb_meminfo;	/* memory arena info parameters */
	xiocb_fwinfo_t  xiocb_fwinfo;	/* firmware information */
	xiocb_exitstat_t xiocb_exitstat; /* Exit Status */
    } plist;
} cfe_xiocb_t;

#endif /* CFE_API_INT_H */
