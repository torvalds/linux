/*-
 * Copyright (c) 2017, Juniper Networks, Inc.
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
/*
 * $FreeBSD$
 */
#ifndef _LIBSECUREBOOT_PRIV_H_
#define _LIBSECUREBOOT_PRIV_H_

/* public api */
#include "libsecureboot.h"

typedef struct {
	unsigned char	*data;
	size_t		hash_size;
} hash_data;

size_t ve_trust_anchors_add(br_x509_certificate *, size_t);
size_t ve_forbidden_anchors_add(br_x509_certificate *, size_t);
void   ve_forbidden_digest_add(hash_data *digest, size_t);
char   *fingerprint_info_lookup(int, const char *);

br_x509_certificate * parse_certificates(unsigned char *, size_t, size_t *);
int  certificate_to_trust_anchor_inner(br_x509_trust_anchor *,
    br_x509_certificate *);

int verify_rsa_digest(br_rsa_public_key *pkey,
    const unsigned char *hash_oid,
    unsigned char *mdata, size_t mlen,
    unsigned char *sdata, size_t slen);

int openpgp_self_tests(void);

int                     efi_secure_boot_enabled(void);
br_x509_certificate*    efi_get_trusted_certs(size_t *count);
br_x509_certificate*    efi_get_forbidden_certs(size_t *count);
hash_data*              efi_get_forbidden_digests(size_t *count);

#endif	/* _LIBSECUREBOOT_PRIV_H_ */
