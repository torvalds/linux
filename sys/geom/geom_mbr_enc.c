/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Poul-Henning Kamp
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* Functions to encode or decode struct dos_partition into a bytestream
 * of correct endianness and packing.  These functions do no validation
 * or sanity checking, they only pack/unpack the fields correctly.
 *
 * NB!  This file must be usable both in kernel and userland.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/diskmbr.h>
#include <sys/endian.h>

void
dos_partition_dec(void const *pp, struct dos_partition *d)
{
	unsigned char const *p = pp;

	d->dp_flag = p[0];
	d->dp_shd = p[1];
	d->dp_ssect = p[2];
	d->dp_scyl = p[3];
	d->dp_typ = p[4];
	d->dp_ehd = p[5];
	d->dp_esect = p[6];
	d->dp_ecyl = p[7];
	d->dp_start = le32dec(p + 8);
	d->dp_size = le32dec(p + 12);
}

void
dos_partition_enc(void *pp, struct dos_partition *d)
{
	unsigned char *p = pp;

	p[0] = d->dp_flag;
	p[1] = d->dp_shd;
	p[2] = d->dp_ssect;
	p[3] = d->dp_scyl;
	p[4] = d->dp_typ;
	p[5] = d->dp_ehd;
	p[6] = d->dp_esect;
	p[7] = d->dp_ecyl;
	le32enc(p + 8, d->dp_start);
	le32enc(p + 12, d->dp_size);
}
