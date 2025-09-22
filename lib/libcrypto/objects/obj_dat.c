/* $OpenBSD: obj_dat.c,v 1.95 2025/05/10 05:54:38 tb Exp $ */
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

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/opensslconf.h>

#include <openssl/asn1.h>
#include <openssl/bn.h>
#include <openssl/lhash.h>
#include <openssl/objects.h>

#include "asn1_local.h"
#include "err_local.h"

/* obj_dat.h is generated from objects.h by obj_dat.pl */
#include "obj_dat.h"

#define ADDED_DATA	0
#define ADDED_SNAME	1
#define ADDED_LNAME	2
#define ADDED_NID	3

typedef struct added_obj_st {
	int type;
	ASN1_OBJECT *obj;
} ADDED_OBJ;
DECLARE_LHASH_OF(ADDED_OBJ);

static int new_nid = NUM_NID;
static LHASH_OF(ADDED_OBJ) *added = NULL;

static unsigned long
added_obj_hash(const ADDED_OBJ *ca)
{
	const ASN1_OBJECT *a;
	int i;
	unsigned long ret = 0;
	unsigned char *p;

	a = ca->obj;
	switch (ca->type) {
	case ADDED_DATA:
		ret = (unsigned long)a->length << 20L;
		p = (unsigned char *)a->data;
		for (i = 0; i < a->length; i++)
			ret ^= p[i] << ((i * 3) % 24);
		break;
	case ADDED_SNAME:
		ret = lh_strhash(a->sn);
		break;
	case ADDED_LNAME:
		ret = lh_strhash(a->ln);
		break;
	case ADDED_NID:
		ret = a->nid;
		break;
	default:
		return 0;
	}
	ret &= 0x3fffffffL;
	ret |= (unsigned long)ca->type << 30L;
	return ret;
}
static IMPLEMENT_LHASH_HASH_FN(added_obj, ADDED_OBJ)

static int
added_obj_cmp(const ADDED_OBJ *ca, const ADDED_OBJ *cb)
{
	const ASN1_OBJECT *a, *b;
	int cmp;

	if ((cmp = ca->type - cb->type) != 0)
		return cmp;

	a = ca->obj;
	b = cb->obj;
	switch (ca->type) {
	case ADDED_DATA:
		return OBJ_cmp(a, b);
	case ADDED_SNAME:
		if (a->sn == NULL)
			return -1;
		if (b->sn == NULL)
			return 1;
		return strcmp(a->sn, b->sn);
	case ADDED_LNAME:
		if (a->ln == NULL)
			return -1;
		if (b->ln == NULL)
			return 1;
		return strcmp(a->ln, b->ln);
	case ADDED_NID:
		return a->nid - b->nid;
	default:
		return 0;
	}
}
static IMPLEMENT_LHASH_COMP_FN(added_obj, ADDED_OBJ)

static void
cleanup1_doall(ADDED_OBJ *a)
{
	a->obj->nid = 0;
	a->obj->flags |= ASN1_OBJECT_FLAG_DYNAMIC |
	    ASN1_OBJECT_FLAG_DYNAMIC_STRINGS |
	    ASN1_OBJECT_FLAG_DYNAMIC_DATA;
}

static void
cleanup2_doall(ADDED_OBJ *a)
{
	a->obj->nid++;
}

static void
cleanup3_doall(ADDED_OBJ *a)
{
	if (--a->obj->nid == 0)
		ASN1_OBJECT_free(a->obj);
	free(a);
}

static IMPLEMENT_LHASH_DOALL_FN(cleanup1, ADDED_OBJ)
static IMPLEMENT_LHASH_DOALL_FN(cleanup2, ADDED_OBJ)
static IMPLEMENT_LHASH_DOALL_FN(cleanup3, ADDED_OBJ)

void
OBJ_cleanup(void)
{
	if (added == NULL)
		return;

	lh_ADDED_OBJ_doall(added, LHASH_DOALL_FN(cleanup1)); /* zero counters */
	lh_ADDED_OBJ_doall(added, LHASH_DOALL_FN(cleanup2)); /* set counters */
	lh_ADDED_OBJ_doall(added, LHASH_DOALL_FN(cleanup3)); /* free objects */
	lh_ADDED_OBJ_free(added);
	added = NULL;
}
LCRYPTO_ALIAS(OBJ_cleanup);

int
OBJ_new_nid(int num)
{
	int i;

	i = new_nid;
	new_nid += num;
	return i;
}
LCRYPTO_ALIAS(OBJ_new_nid);

static int
OBJ_add_object(const ASN1_OBJECT *obj)
{
	ASN1_OBJECT *o = NULL;
	ADDED_OBJ *ao[4] = {NULL, NULL, NULL, NULL}, *aop;
	int i;

	if (added == NULL)
		added = lh_ADDED_OBJ_new();
	if (added == NULL)
		goto err;
	if (obj == NULL || obj->nid == NID_undef)
		goto err;
	if ((o = OBJ_dup(obj)) == NULL)
		goto err;
	if (!(ao[ADDED_NID] = malloc(sizeof(ADDED_OBJ))))
		goto err2;
	if ((o->length != 0) && (obj->data != NULL))
		if (!(ao[ADDED_DATA] = malloc(sizeof(ADDED_OBJ))))
			goto err2;
	if (o->sn != NULL)
		if (!(ao[ADDED_SNAME] = malloc(sizeof(ADDED_OBJ))))
			goto err2;
	if (o->ln != NULL)
		if (!(ao[ADDED_LNAME] = malloc(sizeof(ADDED_OBJ))))
			goto err2;

	for (i = ADDED_DATA; i <= ADDED_NID; i++) {
		if (ao[i] != NULL) {
			ao[i]->type = i;
			ao[i]->obj = o;
			aop = lh_ADDED_OBJ_insert(added, ao[i]);
			/* memory leak, but should not normally matter */
			free(aop);
		}
	}
	o->flags &= ~(ASN1_OBJECT_FLAG_DYNAMIC |
	    ASN1_OBJECT_FLAG_DYNAMIC_STRINGS |
	    ASN1_OBJECT_FLAG_DYNAMIC_DATA);

	return o->nid;

 err2:
	OBJerror(ERR_R_MALLOC_FAILURE);
 err:
	for (i = ADDED_DATA; i <= ADDED_NID; i++)
		free(ao[i]);
	ASN1_OBJECT_free(o);
	return NID_undef;
}

ASN1_OBJECT *
OBJ_nid2obj(int nid)
{
	if (nid >= 0 && nid < NUM_NID) {
		if (nid == NID_undef || nid_objs[nid].nid != NID_undef)
			return (ASN1_OBJECT *)&nid_objs[nid];

		goto unknown;
	}

	/* XXX - locking. */
	if (added != NULL) {
		ASN1_OBJECT aobj = {
			.nid = nid,
		};
		ADDED_OBJ needle = {
			.type = ADDED_NID,
			.obj = &aobj,
		};
		ADDED_OBJ *found;

		if ((found = lh_ADDED_OBJ_retrieve(added, &needle)) != NULL)
			return found->obj;
	}

 unknown:
	OBJerror(OBJ_R_UNKNOWN_NID);

	return NULL;
}
LCRYPTO_ALIAS(OBJ_nid2obj);

const char *
OBJ_nid2sn(int nid)
{
	ASN1_OBJECT *aobj;

	if ((aobj = OBJ_nid2obj(nid)) == NULL)
		return NULL;

	return aobj->sn;
}
LCRYPTO_ALIAS(OBJ_nid2sn);

const char *
OBJ_nid2ln(int nid)
{
	ASN1_OBJECT *aobj;

	if ((aobj = OBJ_nid2obj(nid)) == NULL)
		return NULL;

	return aobj->ln;
}
LCRYPTO_ALIAS(OBJ_nid2ln);

static int
obj_objs_cmp(const void *aobj, const void *b)
{
	const unsigned int *nid = b;

	OPENSSL_assert(*nid < NUM_NID);

	return OBJ_cmp(aobj, &nid_objs[*nid]);
}

int
OBJ_obj2nid(const ASN1_OBJECT *aobj)
{
	const unsigned int *nid;

	if (aobj == NULL || aobj->length == 0)
		return NID_undef;

	if (aobj->nid != NID_undef)
		return aobj->nid;

	/* XXX - locking. OpenSSL 3 moved this after built-in object lookup. */
	if (added != NULL) {
		ADDED_OBJ needle = {
			.type = ADDED_DATA,
			.obj = (ASN1_OBJECT *)aobj,
		};
		ADDED_OBJ *found;

		if ((found = lh_ADDED_OBJ_retrieve(added, &needle)) != NULL)
			return found->obj->nid;
	}

	/* obj_objs holds built-in obj NIDs in ascending OBJ_cmp() order. */
	nid = bsearch(aobj, obj_objs, NUM_OBJ, sizeof(unsigned int), obj_objs_cmp);
	if (nid != NULL)
		return *nid;

	return NID_undef;
}
LCRYPTO_ALIAS(OBJ_obj2nid);

static int
sn_objs_cmp(const void *sn, const void *b)
{
	const unsigned int *nid = b;

	OPENSSL_assert(*nid < NUM_NID);

	return strcmp(sn, nid_objs[*nid].sn);
}

int
OBJ_sn2nid(const char *sn)
{
	const unsigned int *nid;

	/* XXX - locking. OpenSSL 3 moved this after built-in object lookup. */
	if (added != NULL) {
		ASN1_OBJECT aobj = {
			.sn = sn,
		};
		ADDED_OBJ needle = {
			.type = ADDED_SNAME,
			.obj = &aobj,
		};
		ADDED_OBJ *found;

		if ((found = lh_ADDED_OBJ_retrieve(added, &needle)) != NULL)
			return found->obj->nid;
	}

	/* sn_objs holds NIDs in ascending alphabetical order of SN. */
	nid = bsearch(sn, sn_objs, NUM_SN, sizeof(unsigned int), sn_objs_cmp);
	if (nid != NULL)
		return *nid;

	return NID_undef;
}
LCRYPTO_ALIAS(OBJ_sn2nid);

static int
ln_objs_cmp(const void *ln, const void *b)
{
	const unsigned int *nid = b;

	OPENSSL_assert(*nid < NUM_NID);

	return strcmp(ln, nid_objs[*nid].ln);
}

int
OBJ_ln2nid(const char *ln)
{
	const unsigned int *nid;

	/* XXX - locking. OpenSSL 3 moved this after built-in object lookup. */
	if (added != NULL) {
		ASN1_OBJECT aobj = {
			.ln = ln,
		};
		ADDED_OBJ needle = {
			.type = ADDED_LNAME,
			.obj = &aobj,
		};
		ADDED_OBJ *found;

		if ((found = lh_ADDED_OBJ_retrieve(added, &needle)) != NULL)
			return found->obj->nid;
	}

	/* ln_objs holds NIDs in ascending alphabetical order of LN. */
	nid = bsearch(ln, ln_objs, NUM_LN, sizeof(unsigned int), ln_objs_cmp);
	if (nid != NULL)
		return *nid;

	return NID_undef;
}
LCRYPTO_ALIAS(OBJ_ln2nid);

/* Convert an object name into an ASN1_OBJECT
 * if "noname" is not set then search for short and long names first.
 * This will convert the "dotted" form into an object: unlike OBJ_txt2nid
 * it can be used with any objects, not just registered ones.
 */

ASN1_OBJECT *
OBJ_txt2obj(const char *s, int no_name)
{
	int nid;

	if (!no_name) {
		if ((nid = OBJ_sn2nid(s)) != NID_undef ||
		    (nid = OBJ_ln2nid(s)) != NID_undef)
			return OBJ_nid2obj(nid);
	}

	return t2i_ASN1_OBJECT_internal(s);
}
LCRYPTO_ALIAS(OBJ_txt2obj);

int
OBJ_obj2txt(char *buf, int buf_len, const ASN1_OBJECT *aobj, int no_name)
{
	return i2t_ASN1_OBJECT_internal(aobj, buf, buf_len, no_name);
}
LCRYPTO_ALIAS(OBJ_obj2txt);

int
OBJ_txt2nid(const char *s)
{
	ASN1_OBJECT *obj;
	int nid;

	obj = OBJ_txt2obj(s, 0);
	nid = OBJ_obj2nid(obj);
	ASN1_OBJECT_free(obj);
	return nid;
}
LCRYPTO_ALIAS(OBJ_txt2nid);

int
OBJ_create_objects(BIO *in)
{
	char buf[512];
	int i, num = 0;
	char *o, *s, *l = NULL;

	for (;;) {
		s = o = NULL;
		i = BIO_gets(in, buf, 512);
		if (i <= 0)
			return num;
		buf[i - 1] = '\0';
		if (!isalnum((unsigned char)buf[0]))
			return num;
		o = s=buf;
		while (isdigit((unsigned char)*s) || (*s == '.'))
			s++;
		if (*s != '\0') {
			*(s++) = '\0';
			while (isspace((unsigned char)*s))
				s++;
			if (*s == '\0')
				s = NULL;
			else {
				l = s;
				while ((*l != '\0') &&
				    !isspace((unsigned char)*l))
					l++;
				if (*l != '\0') {
					*(l++) = '\0';
					while (isspace((unsigned char)*l))
						l++;
					if (*l == '\0')
						l = NULL;
				} else
					l = NULL;
			}
		} else
			s = NULL;
		if ((o == NULL) || (*o == '\0'))
			return num;
		if (!OBJ_create(o, s, l))
			return num;
		num++;
	}
	/* return(num); */
}
LCRYPTO_ALIAS(OBJ_create_objects);

int
OBJ_create(const char *oid, const char *sn, const char *ln)
{
	ASN1_OBJECT *aobj = NULL;
	unsigned char *buf = NULL;
	int len, nid;
	int ret = 0;

	if ((len = a2d_ASN1_OBJECT(NULL, 0, oid, -1)) <= 0)
		goto err;

	if ((buf = calloc(1, len)) == NULL) {
		OBJerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if ((len = a2d_ASN1_OBJECT(buf, len, oid, -1)) == 0)
		goto err;

	nid = OBJ_new_nid(1);
	if ((aobj = ASN1_OBJECT_create(nid, buf, len, sn, ln)) == NULL)
		goto err;

	ret = OBJ_add_object(aobj);

 err:
	ASN1_OBJECT_free(aobj);
	free(buf);

	return ret;
}
LCRYPTO_ALIAS(OBJ_create);

size_t
OBJ_length(const ASN1_OBJECT *obj)
{
	if (obj == NULL)
		return 0;

	if (obj->length < 0)
		return 0;

	return obj->length;
}
LCRYPTO_ALIAS(OBJ_length);

const unsigned char *
OBJ_get0_data(const ASN1_OBJECT *obj)
{
	if (obj == NULL)
		return NULL;

	return obj->data;
}
LCRYPTO_ALIAS(OBJ_get0_data);
