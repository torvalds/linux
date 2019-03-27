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
/*
 * $FreeBSD$
 */

#include <sys/queue.h>

/*
 * Structs to represent what we need
 */

typedef struct OpenPGP_user {
	char *id;
	char *name;
} OpenPGP_user;

struct OpenPGP_key_ {
	char *id;
	int sig_alg;
	OpenPGP_user *user;
#ifdef USE_BEARSSL
	br_rsa_public_key *key;
#else
	EVP_PKEY *key;
#endif
	LIST_ENTRY(OpenPGP_key_) entries;
};

typedef struct OpenPGP_key_ OpenPGP_key;

typedef struct OpenPGP_sig {
	char *key_id;
	int sig_type;
	int sig_alg;
	int hash_alg;
	unsigned char *pgpbytes;
	size_t pgpbytes_len;
	unsigned char *sig;
	size_t sig_len;
} OpenPGP_sig;

void openpgp_trust_add(OpenPGP_key *key);
OpenPGP_key * openpgp_trust_get(const char *keyID);
OpenPGP_key * load_key_file(const char *kfile);
OpenPGP_key * load_key_id(const char *keyID);
void initialize(void);
char * get_error_string(void);
int openpgp_verify(const char *filename, unsigned char *fdata, size_t fbytes,
    unsigned char *sdata, size_t sbytes, int flags);
int openpgp_verify_file(const char *filename, unsigned char *fdata,
    size_t nbytes);

/* packet decoders */
#define DECODER_DECL(x)							\
	ssize_t decode_##x(int, unsigned char **, size_t, OpenPGP_##x *)

DECODER_DECL(user);
DECODER_DECL(key);
DECODER_DECL(sig);
