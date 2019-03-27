/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Pawel Jakub Dawidek <pawel@dawidek.net>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <errno.h>
#include <string.h>
#include <strings.h>

#include <crc32.h>
#include <hast.h>
#include <nv.h>
#include <sha256.h>
#include <pjdlog.h>

#include "hast_checksum.h"

#define	MAX_HASH_SIZE	SHA256_DIGEST_LENGTH

static void
hast_crc32_checksum(const unsigned char *data, size_t size,
    unsigned char *hash, size_t *hsizep)
{
	uint32_t crc;

	crc = crc32(data, size);
	/* XXXPJD: Do we have to use htole32() on crc first? */
	bcopy(&crc, hash, sizeof(crc));
	*hsizep = sizeof(crc);
}

static void
hast_sha256_checksum(const unsigned char *data, size_t size,
    unsigned char *hash, size_t *hsizep)
{
	SHA256_CTX ctx;

	SHA256_Init(&ctx);
	SHA256_Update(&ctx, data, size);
	SHA256_Final(hash, &ctx);
	*hsizep = SHA256_DIGEST_LENGTH;
}

const char *
checksum_name(int num)
{

	switch (num) {
	case HAST_CHECKSUM_NONE:
		return ("none");
	case HAST_CHECKSUM_CRC32:
		return ("crc32");
	case HAST_CHECKSUM_SHA256:
		return ("sha256");
	}
	return ("unknown");
}

int
checksum_send(const struct hast_resource *res, struct nv *nv, void **datap,
    size_t *sizep, bool *freedatap __unused)
{
	unsigned char hash[MAX_HASH_SIZE];
	size_t hsize;

	switch (res->hr_checksum) {
	case HAST_CHECKSUM_NONE:
		return (0);
	case HAST_CHECKSUM_CRC32:
		hast_crc32_checksum(*datap, *sizep, hash, &hsize);
		break;
	case HAST_CHECKSUM_SHA256:
		hast_sha256_checksum(*datap, *sizep, hash, &hsize);
		break;
	default:
		PJDLOG_ABORT("Invalid checksum: %d.", res->hr_checksum);
	}
	nv_add_string(nv, checksum_name(res->hr_checksum), "checksum");
	nv_add_uint8_array(nv, hash, hsize, "hash");
	if (nv_error(nv) != 0) {
		errno = nv_error(nv);
		return (-1);
	}
	return (0);
}

int
checksum_recv(const struct hast_resource *res __unused, struct nv *nv,
    void **datap, size_t *sizep, bool *freedatap __unused)
{
	unsigned char chash[MAX_HASH_SIZE];
	const unsigned char *rhash;
	size_t chsize, rhsize;
	const char *algo;

	algo = nv_get_string(nv, "checksum");
	if (algo == NULL)
		return (0);	/* No checksum. */
	rhash = nv_get_uint8_array(nv, &rhsize, "hash");
	if (rhash == NULL) {
		pjdlog_error("Hash is missing.");
		return (-1);	/* Hash not found. */
	}
	if (strcmp(algo, "crc32") == 0)
		hast_crc32_checksum(*datap, *sizep, chash, &chsize);
	else if (strcmp(algo, "sha256") == 0)
		hast_sha256_checksum(*datap, *sizep, chash, &chsize);
	else {
		pjdlog_error("Unknown checksum algorithm '%s'.", algo);
		return (-1);	/* Unknown checksum algorithm. */
	}
	if (rhsize != chsize) {
		pjdlog_error("Invalid hash size (%zu) for %s, should be %zu.",
		    rhsize, algo, chsize);
		return (-1);	/* Different hash size. */
	}
	if (bcmp(rhash, chash, chsize) != 0) {
		pjdlog_error("Hash mismatch.");
		return (-1);	/* Hash mismatch. */
	}

	return (0);
}
