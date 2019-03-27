/*  @(#)des.h	2.2 88/08/10 4.0 RPCSRC; from 2.7 88/02/08 SMI  */
/* $FreeBSD$ */
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
 * Generic DES driver interface
 * Keep this file hardware independent!
 * Copyright (c) 1986 by Sun Microsystems, Inc.
 */

#define DES_MAXLEN 	65536	/* maximum # of bytes to encrypt  */
#define DES_QUICKLEN	16	/* maximum # of bytes to encrypt quickly */

enum desdir { ENCRYPT, DECRYPT };
enum desmode { CBC, ECB };

/*
 * parameters to ioctl call
 */
struct desparams {
	u_char des_key[8];	/* key (with low bit parity) */
	enum desdir des_dir;	/* direction */
	enum desmode des_mode;	/* mode */
	u_char des_ivec[8];	/* input vector */
	unsigned des_len;	/* number of bytes to crypt */
	union {
		u_char UDES_data[DES_QUICKLEN];
		u_char *UDES_buf;
	} UDES;
#	define des_data UDES.UDES_data	/* direct data here if quick */
#	define des_buf	UDES.UDES_buf	/* otherwise, pointer to data */
};

#ifdef notdef

/*
 * These ioctls are only implemented in SunOS. Maybe someday
 * if somebody writes a driver for DES hardware that works
 * with FreeBSD, we can being that back.
 */

/*
 * Encrypt an arbitrary sized buffer
 */
#define	DESIOCBLOCK	_IOWR('d', 6, struct desparams)

/* 
 * Encrypt of small amount of data, quickly
 */
#define DESIOCQUICK	_IOWR('d', 7, struct desparams) 

#endif

/*
 * Software DES.
 */
extern int _des_crypt( char *, int, struct desparams * );
