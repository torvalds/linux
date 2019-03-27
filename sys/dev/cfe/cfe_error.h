/* from: Broadcom Id: cfe_error.h,v 1.3 2003/02/07 17:27:56 cgd Exp $ */

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
    *  Error codes				File: cfe_error.h
    *  
    *  CFE's global error code list is here.
    *  
    *  Author:  Mitch Lichtenberg
    *  
    ********************************************************************* */


#define CFE_OK			 0
#define CFE_ERR                 -1	/* generic error */
#define CFE_ERR_INV_COMMAND	-2
#define CFE_ERR_EOF		-3
#define CFE_ERR_IOERR		-4
#define CFE_ERR_NOMEM		-5
#define CFE_ERR_DEVNOTFOUND	-6
#define CFE_ERR_DEVOPEN		-7
#define CFE_ERR_INV_PARAM	-8
#define CFE_ERR_ENVNOTFOUND	-9
#define CFE_ERR_ENVREADONLY	-10

#define CFE_ERR_NOTELF		-11
#define CFE_ERR_NOT32BIT 	-12
#define CFE_ERR_WRONGENDIAN 	-13
#define CFE_ERR_BADELFVERS 	-14
#define CFE_ERR_NOTMIPS 	-15
#define CFE_ERR_BADELFFMT 	-16
#define CFE_ERR_BADADDR 	-17

#define CFE_ERR_FILENOTFOUND	-18
#define CFE_ERR_UNSUPPORTED	-19

#define CFE_ERR_HOSTUNKNOWN	-20

#define CFE_ERR_TIMEOUT		-21

#define CFE_ERR_PROTOCOLERR	-22

#define CFE_ERR_NETDOWN		-23
#define CFE_ERR_NONAMESERVER	-24

#define CFE_ERR_NOHANDLES	-25
#define CFE_ERR_ALREADYBOUND	-26

#define CFE_ERR_CANNOTSET	-27
#define CFE_ERR_NOMORE		-28
#define CFE_ERR_BADFILESYS	-29
#define CFE_ERR_FSNOTAVAIL	-30

#define CFE_ERR_INVBOOTBLOCK	-31
#define CFE_ERR_WRONGDEVTYPE	-32
#define CFE_ERR_BBCHECKSUM	-33
#define CFE_ERR_BOOTPROGCHKSUM	-34

#define CFE_ERR_LDRNOTAVAIL	-35

#define CFE_ERR_NOTREADY	-36

#define CFE_ERR_GETMEM          -37
#define CFE_ERR_SETMEM          -38

#define CFE_ERR_NOTCONN		-39
#define CFE_ERR_ADDRINUSE	-40
