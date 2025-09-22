/* $OpenBSD: x509.h,v 1.22 2007/08/05 09:43:09 tom Exp $	 */
/* $EOM: x509.h,v 1.11 2000/09/28 12:53:27 niklas Exp $	 */

/*
 * Copyright (c) 1998, 1999 Niels Provos.  All rights reserved.
 * Copyright (c) 1999 Angelos D. Keromytis.  All rights reserved.
 * Copyright (c) 2000, 2001 Niklas Hallqvist.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This code was written under funding by Ericsson Radio Systems.
 */

#ifndef _X509_H_
#define _X509_H_

#include "libcrypto.h"

#define X509v3_RFC_NAME		1
#define X509v3_DNS_NAME		2
#define X509v3_IP_ADDR		7

struct x509_attribval {
	char           *type;
	char           *val;
};

/*
 * The acceptable certification authority.
 * XXX We only support two names at the moment, as of ASN this can
 * be dynamic but we don't care for now.
 */
struct x509_aca {
	struct x509_attribval name1;
	struct x509_attribval name2;
};

struct X509;
struct X509_STORE;

/* Functions provided by cert handler.  */

int             x509_certreq_validate(u_int8_t *, u_int32_t);
int             x509_certreq_decode(void **, u_int8_t *, u_int32_t);
void            x509_cert_free(void *);
void           *x509_cert_get(u_int8_t *, u_int32_t);
int             x509_cert_get_key(void *, void *);
int             x509_cert_get_subjects(void *, int *, u_int8_t ***, u_int32_t **);
int             x509_cert_init(void);
int             x509_crl_init(void);
int             x509_cert_obtain(u_int8_t *, size_t, void *, u_int8_t **,
		    u_int32_t *);
int             x509_cert_validate(void *);
void            x509_free_aca(void *);
void           *x509_cert_dup(void *);
void            x509_serialize(void *, u_int8_t **, u_int32_t *);
char           *x509_printable(void *);
void           *x509_from_printable(char *);
int		x509_ca_count(void);

/* Misc. X509 certificate functions.  */

char           *x509_DN_string(u_int8_t *, size_t);
int             x509_cert_insert(int, void *);
int             x509_cert_subjectaltname(X509 * cert, u_char **, u_int *);
X509           *x509_from_asn(u_char *, u_int);
int             x509_generate_kn(int, X509 *);
int             x509_read_from_dir(X509_STORE *, char *, int, int *);
int             x509_read_crls_from_dir(X509_STORE *, char *);

#endif				/* _X509_H_ */
