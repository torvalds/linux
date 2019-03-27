/*
 * @(#)des_crypt.h	2.1 88/08/11 4.0 RPCSRC;	from 1.4 88/02/08 (C) 1986 SMI
 * $FreeBSD$
 *
 * des_crypt.h, des library routine interface
 * Copyright (C) 1986, Sun Microsystems, Inc.
 */
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2009, Sun Microsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of Sun Microsystems, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Copyright (c) 1986 - 1991 by Sun Microsystems, Inc.
 */

/*
 * des_crypt.h, des library routine interface
 */

#ifndef _DES_DES_CRYPT_H
#define _DES_DES_CRYPT_H

#include <sys/cdefs.h>
#include <rpc/rpc.h>

#define DES_MAXDATA 8192	/* max bytes encrypted in one call */
#define DES_DIRMASK (1 << 0)
#define DES_ENCRYPT (0*DES_DIRMASK)	/* Encrypt */
#define DES_DECRYPT (1*DES_DIRMASK)	/* Decrypt */


#define DES_DEVMASK (1 << 1)
#define	DES_HW (0*DES_DEVMASK)	/* Use hardware device */ 
#define DES_SW (1*DES_DEVMASK)	/* Use software device */


#define DESERR_NONE 0	/* succeeded */
#define DESERR_NOHWDEVICE 1	/* succeeded, but hw device not available */
#define DESERR_HWERROR 2	/* failed, hardware/driver error */
#define DESERR_BADPARAM 3	/* failed, bad parameter to call */

#define DES_FAILED(err) \
	((err) > DESERR_NOHWDEVICE)

/*
 * cbc_crypt()
 * ecb_crypt()
 *
 * Encrypt (or decrypt) len bytes of a buffer buf.
 * The length must be a multiple of eight.
 * The key should have odd parity in the low bit of each byte.
 * ivec is the input vector, and is updated to the new one (cbc only).
 * The mode is created by oring together the appropriate parameters.
 * DESERR_NOHWDEVICE is returned if DES_HW was specified but
 * there was no hardware to do it on (the data will still be
 * encrypted though, in software).
 */


/*
 * Cipher Block Chaining mode
 */
__BEGIN_DECLS
int cbc_crypt( char *, char *, unsigned int, unsigned int, char *);
__END_DECLS

/*
 * Electronic Code Book mode
 */
__BEGIN_DECLS
int ecb_crypt( char *, char *, unsigned int, unsigned int );
__END_DECLS

/* 
 * Set des parity for a key.
 * DES parity is odd and in the low bit of each byte
 */
__BEGIN_DECLS
void des_setparity( char *);
__END_DECLS

#endif  /* _DES_DES_CRYPT_H */
