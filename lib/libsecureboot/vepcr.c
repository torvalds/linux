/*-
 * Copyright (c) 2018, Juniper Networks, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "libsecureboot-priv.h"

/*
 * To support measured boot without putting a ton
 * of extra code in the loader, we just maintain
 * a hash of all the hashes we (attempt to) verify.
 * The loader can export this for kernel or rc script
 * to feed to a TPM pcr register - hence the name ve_pcr.
 *
 * NOTE: in the current standard the TPM pcr register size is for SHA1,
 * the fact that we provide a SHA256 hash should not matter
 * as long as we are consistent - it can be truncated or hashed
 * before feeding to TPM.
 */

static const br_hash_class *pcr_md = NULL;
static br_hash_compat_context pcr_ctx;
static size_t pcr_hlen = 0;

/**
 * @brief initialize pcr context
 *
 * Real TPM registers only hold a SHA1 hash
 * but we use SHA256
 */
void
ve_pcr_init(void)
{
	pcr_hlen = br_sha256_SIZE;
	pcr_md = &br_sha256_vtable;
	pcr_md->init(&pcr_ctx.vtable);
}

/**
 * @brief update pcr context
 */
void
ve_pcr_update(unsigned char *data, size_t dlen)
{
	if (pcr_md)
		pcr_md->update(&pcr_ctx.vtable, data, dlen);
}

/**
 * @brief get pcr result
 */
ssize_t
ve_pcr_get(unsigned char *buf, size_t sz)
{
	if (!pcr_md)
		return (-1);
	if (sz < pcr_hlen)
		return (-1);
	pcr_md->out(&pcr_ctx.vtable, buf);
	return (pcr_hlen);
}

