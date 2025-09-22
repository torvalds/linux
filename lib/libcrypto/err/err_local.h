/*	$OpenBSD: err_local.h,v 1.5 2025/05/10 06:45:46 tb Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
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
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */
/* ====================================================================
 * Copyright (c) 1998-2006 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */

#include <openssl/err.h>

#ifndef HEADER_ERR_LOCAL_H
#define HEADER_ERR_LOCAL_H

__BEGIN_HIDDEN_DECLS

void ERR_load_const_strings(const ERR_STRING_DATA *str);

#define ERR_PUT_ERROR(l, r) ERR_PUT_error((l), 0xfff, (r), OPENSSL_FILE, OPENSSL_LINE)

#define ASN1error(r)	ERR_PUT_ERROR(ERR_LIB_ASN1, (r))
#define BIOerror(r)	ERR_PUT_ERROR(ERR_LIB_BIO, (r))
#define BNerror(r)	ERR_PUT_ERROR(ERR_LIB_BN, (r))
#define BUFerror(r)	ERR_PUT_ERROR(ERR_LIB_BUF, (r))
#define CMSerror(r)	ERR_PUT_ERROR(ERR_LIB_CMS, (r))
#define CONFerror(r)	ERR_PUT_ERROR(ERR_LIB_CONF, (r))
#define CRYPTOerror(r)	ERR_PUT_ERROR(ERR_LIB_CRYPTO, (r))
#define CTerror(r)	ERR_PUT_ERROR(ERR_LIB_CT, (r))
#define DHerror(r)	ERR_PUT_ERROR(ERR_LIB_DH, (r))
#define DSAerror(r)	ERR_PUT_ERROR(ERR_LIB_DSA, (r))
#define ECerror(r)	ERR_PUT_ERROR(ERR_LIB_EC, (r))
#define EVPerror(r)	ERR_PUT_ERROR(ERR_LIB_EVP, (r))
#define KDFerror(r)	ERR_PUT_ERROR(ERR_LIB_KDF, (r))
#define OBJerror(r)	ERR_PUT_ERROR(ERR_LIB_OBJ, (r))
#define OCSPerror(r)	ERR_PUT_ERROR(ERR_LIB_OCSP, (r))
#define PEMerror(r)	ERR_PUT_ERROR(ERR_LIB_PEM, (r))
#define PKCS12error(r)	ERR_PUT_ERROR(ERR_LIB_PKCS12, (r))
#define PKCS7error(r)	ERR_PUT_ERROR(ERR_LIB_PKCS7, (r))
#define RANDerror(r)	ERR_PUT_ERROR(ERR_LIB_RAND, (r))
#define RSAerror(r)	ERR_PUT_ERROR(ERR_LIB_RSA, (r))
#define SYSerror(r)	ERR_PUT_ERROR(ERR_LIB_SYS, (r))
#define TSerror(r)	ERR_PUT_ERROR(ERR_LIB_TS, (r))
#define UIerror(r)	ERR_PUT_ERROR(ERR_LIB_UI, (r))
#define X509V3error(r)	ERR_PUT_ERROR(ERR_LIB_X509V3, (r))
#define X509error(r)	ERR_PUT_ERROR(ERR_LIB_X509, (r))

__END_HIDDEN_DECLS

#endif /* HEADER_ERR_LOCAL_H */
