/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Tom Truscott.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__SCCSID("@(#)crypt.c	5.11 (Berkeley) 6/25/91");
__FBSDID("$FreeBSD$");

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * UNIX password, and DES, encryption.
 *
 * since this is non-exportable, this is just a dummy.  if you want real
 * encryption, make sure you've got libcrypt.a around.
 */

/* ARGSUSED */
int
__freebsd11_des_setkey(const char *key __unused)
{
	fprintf(stderr, "WARNING!  des_setkey(3) not present in the system!\n");
	return (0);
}

/* ARGSUSED */
int
__freebsd11_des_cipher(const char *in, char *out, long salt __unused,
    int num_iter __unused)
{
	fprintf(stderr, "WARNING!  des_cipher(3) not present in the system!\n");
	bcopy(in, out, 8);
	return (0);
}

/* ARGSUSED */
int
__freebsd11_setkey(const char *key __unused)
{
	fprintf(stderr, "WARNING!  setkey(3) not present in the system!\n");
	return (0);
}

/* ARGSUSED */
int
__freebsd11_encrypt(char *block __unused, int flag __unused)
{
	fprintf(stderr, "WARNING!  encrypt(3) not present in the system!\n");
	return (0);
}

__sym_compat(des_setkey, __freebsd11_des_setkey, FBSD_1.0);
__sym_compat(des_cipher, __freebsd11_des_cipher, FBSD_1.0);
__sym_compat(setkey, __freebsd11_setkey, FBSD_1.0);
__sym_compat(encrypt, __freebsd11_encrypt, FBSD_1.0);
