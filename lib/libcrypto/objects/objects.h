/* $OpenBSD: objects.h,v 1.29 2024/03/02 09:51:36 tb Exp $ */
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

#ifndef HEADER_OBJECTS_H
#define HEADER_OBJECTS_H

#include <openssl/obj_mac.h>

#define SN_ED25519			SN_Ed25519
#define NID_ED25519			NID_Ed25519
#define OBJ_ED25519			OBJ_Ed25519

#include <openssl/bio.h>
#include <openssl/asn1.h>

#define	OBJ_NAME_TYPE_UNDEF		0x00
#define	OBJ_NAME_TYPE_MD_METH		0x01
#define	OBJ_NAME_TYPE_CIPHER_METH	0x02
#define	OBJ_NAME_TYPE_NUM		0x03

#define	OBJ_NAME_ALIAS			0x8000

#ifdef  __cplusplus
extern "C" {
#endif

typedef struct obj_name_st {
	int type;
	int alias;
	const char *name;
	const void *data;
} OBJ_NAME;

void OBJ_NAME_do_all(int type, void (*fn)(const OBJ_NAME *, void *arg),
    void *arg);
void OBJ_NAME_do_all_sorted(int type, void (*fn)(const OBJ_NAME *, void *arg),
    void *arg);

ASN1_OBJECT *	OBJ_dup(const ASN1_OBJECT *o);
ASN1_OBJECT *	OBJ_nid2obj(int n);
const char *	OBJ_nid2ln(int n);
const char *	OBJ_nid2sn(int n);
int		OBJ_obj2nid(const ASN1_OBJECT *o);
ASN1_OBJECT *	OBJ_txt2obj(const char *s, int no_name);
int	OBJ_obj2txt(char *buf, int buf_len, const ASN1_OBJECT *a, int no_name);
int		OBJ_txt2nid(const char *s);
int		OBJ_ln2nid(const char *s);
int		OBJ_sn2nid(const char *s);
int		OBJ_cmp(const ASN1_OBJECT *a, const ASN1_OBJECT *b);

int		OBJ_new_nid(int num);
int		OBJ_create(const char *oid, const char *sn, const char *ln);
void		OBJ_cleanup(void);
int		OBJ_create_objects(BIO *in);

size_t OBJ_length(const ASN1_OBJECT *obj);
const unsigned char *OBJ_get0_data(const ASN1_OBJECT *obj);

int OBJ_find_sigid_algs(int signid, int *pdig_nid, int *ppkey_nid);
int OBJ_find_sigid_by_algs(int *psignid, int dig_nid, int pkey_nid);

void ERR_load_OBJ_strings(void);

/* Error codes for the OBJ functions. */

/* Function codes. */
#define OBJ_F_OBJ_ADD_OBJECT				 105
#define OBJ_F_OBJ_CREATE				 100
#define OBJ_F_OBJ_DUP					 101
#define OBJ_F_OBJ_NAME_NEW_INDEX			 106
#define OBJ_F_OBJ_NID2LN				 102
#define OBJ_F_OBJ_NID2OBJ				 103
#define OBJ_F_OBJ_NID2SN				 104

/* Reason codes. */
#define OBJ_R_MALLOC_FAILURE				 100
#define OBJ_R_UNKNOWN_NID				 101

#ifdef  __cplusplus
}
#endif
#endif
