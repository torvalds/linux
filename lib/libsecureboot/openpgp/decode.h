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

#ifdef USE_BEARSSL
unsigned char * mpi2bn(unsigned char **pptr, size_t *sz);
#else
# include <openssl/bn.h>
# include <openssl/rsa.h>
# include <openssl/evp.h>

BIGNUM * mpi2bn(unsigned char **pptr);
#endif

#define NEW(x) calloc(1,sizeof(x))

#define OPENPGP_TAG_ISTAG	0200
#define OPENPGP_TAG_ISNEW	0100
#define OPENPGP_TAG_NEW_MASK	0077
#define OPENPGP_TAG_OLD_MASK	0074
#define OPENPGP_TAG_OLD_TYPE	0003

typedef int (*decoder_t)(int, unsigned char **, int, void *);

unsigned char * i2octets(int n, size_t i);
int octets2i(unsigned char *ptr, size_t n);
char * octets2hex(unsigned char *ptr, size_t n);
int decode_tag(unsigned char *ptr, int *isnew, int *ltype);
unsigned char * decode_mpi(unsigned char **pptr, size_t *sz);
unsigned char * dearmor(char *pem, size_t nbytes, size_t *len);
int decode_packet(int want, unsigned char **pptr, size_t nbytes,
		  decoder_t decoder, void *decoder_arg);
unsigned char * decode_subpacket(unsigned char **pptr, int *stag, int *sz);
