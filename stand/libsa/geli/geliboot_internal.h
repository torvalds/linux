/*-
 * Copyright (c) 2015 Allan Jude <allanjude@FreeBSD.org>
 * Copyright (c) 2005-2011 Pawel Jakub Dawidek <pawel@dawidek.net>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _GELIBOOT_INTERNAL_H_
#define _GELIBOOT_INTERNAL_H_

#define _STRING_H_
#define _STRINGS_H_
#define _STDIO_H_

#include <sys/endian.h>
#include <sys/queue.h>

#include <geom/eli/g_eli.h>
#include <geom/eli/pkcs5v2.h>

#include <bootstrap.h>

/* Pull in the md5, sha256, and sha512 implementations */
#include <sys/md5.h>
#include <crypto/sha2/sha256.h>
#include <crypto/sha2/sha512.h>

/* Pull in AES implementation */
#include <crypto/rijndael/rijndael-api-fst.h>

/* AES-XTS implementation */
#define _STAND 1
#define STAND_H /* We don't want stand.h in {gpt,zfs,gptzfs}boot */
#include <opencrypto/xform_enc.h>

#define GELIDEV_NAMELEN	32

struct geli_dev {
	off_t			part_end;
	struct g_eli_softc	sc;
	struct g_eli_metadata	md;
	int			keybuf_slot;
	char                    *name; /* for prompting; it ends in ':' */
};

int geliboot_crypt(u_int algo, int enc, u_char *data, size_t datasize,
    const u_char *key, size_t keysize, u_char *iv);

#endif /* _GELIBOOT_INTERNAL_H_ */
