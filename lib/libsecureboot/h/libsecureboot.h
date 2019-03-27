/*-
 * Copyright (c) 2017-2018, Juniper Networks, Inc.
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
#ifndef _LIBSECUREBOOT_H_
#define _LIBSECUREBOOT_H_

#include <sys/param.h>
#ifdef _STANDALONE
#include <stand.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#include <bearssl.h>

#ifndef NEED_BRSSL_H
unsigned char * read_file(const char *, size_t *);
#endif

extern int DebugVe;

#define DEBUG_PRINTF(n, x) if (DebugVe >= n) printf x

int ve_trust_init(void);
int ve_trust_add(const char *);
void ve_debug_set(int);
void ve_utc_set(time_t utc);
char *ve_error_get(void);
int ve_error_set(const char *, ...) __printflike(1,2);
int  ve_self_tests(void);

void fingerprint_info_add(const char *, const char *, const char *,
    const char *, struct stat *);

int ve_check_hash(br_hash_compat_context *, const br_hash_class *,
    const char *, const char *, size_t);

struct vectx;
struct vectx* vectx_open(int, const char *, off_t, struct stat *, int *);
ssize_t vectx_read(struct vectx *, void *, size_t);
off_t vectx_lseek(struct vectx *, off_t, int);
int vectx_close(struct vectx *);

char * hexdigest(char *, size_t, unsigned char *, size_t);
int  verify_fd(int, const char *, off_t, struct stat *);
int  verify_open(const char *, int);

unsigned char *verify_signed(const char *, int);
unsigned char *verify_sig(const char *, int);
unsigned char *verify_asc(const char *, int); /* OpenPGP */

void ve_pcr_init(void);
void ve_pcr_update(unsigned char *, size_t);
ssize_t ve_pcr_get(unsigned char *, size_t);

/* flags for verify_{asc,sig,signed} */
#define VEF_VERBOSE		1

#define VE_FINGERPRINT_OK	1
/* errors from verify_fd */
#define VE_FINGERPRINT_NONE	-2
#define VE_FINGERPRINT_WRONG	-3
#define VE_FINGERPRINT_UNKNOWN	-4	/* may not be an error */

#endif	/* _LIBSECUREBOOT_H_ */
