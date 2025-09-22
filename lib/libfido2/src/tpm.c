/*
 * Copyright (c) 2021 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

/*
 * Trusted Platform Module (TPM) 2.0 attestation support. Documentation
 * references are relative to revision 01.38 of the TPM 2.0 specification.
 */

#include <openssl/sha.h>

#include "packed.h"
#include "fido.h"

/* Part 1, 4.89: TPM_GENERATED_VALUE */
#define TPM_MAGIC	0xff544347

/* Part 2, 6.3: TPM_ALG_ID */
#define TPM_ALG_RSA	0x0001
#define TPM_ALG_SHA256	0x000b
#define TPM_ALG_NULL	0x0010
#define TPM_ALG_ECC	0x0023

/* Part 2, 6.4: TPM_ECC_CURVE */
#define TPM_ECC_P256	0x0003

/* Part 2, 6.9: TPM_ST_ATTEST_CERTIFY */
#define TPM_ST_CERTIFY	0x8017

/* Part 2, 8.3: TPMA_OBJECT */
#define TPMA_RESERVED	0xfff8f309	/* reserved bits; must be zero */
#define TPMA_FIXED	0x00000002	/* object has fixed hierarchy */
#define TPMA_CLEAR	0x00000004	/* object persists */
#define TPMA_FIXED_P	0x00000010	/* object has fixed parent */
#define TPMA_SENSITIVE	0x00000020	/* data originates within tpm */
#define TPMA_SIGN	0x00040000	/* object may sign */

/* Part 2, 10.4.2: TPM2B_DIGEST */
PACKED_TYPE(tpm_sha256_digest_t,
struct tpm_sha256_digest {
	uint16_t size; /* sizeof(body) */
	uint8_t  body[32];
})

/* Part 2, 10.4.3: TPM2B_DATA */
PACKED_TYPE(tpm_sha1_data_t,
struct tpm_sha1_data {
	uint16_t size; /* sizeof(body */
	uint8_t  body[20];
})

/* Part 2, 10.5.3: TPM2B_NAME */
PACKED_TYPE(tpm_sha256_name_t,
struct tpm_sha256_name {
	uint16_t size; /* sizeof(alg) + sizeof(body) */
	uint16_t alg;  /* TPM_ALG_SHA256 */
	uint8_t  body[32];
})

/* Part 2, 10.11.1: TPMS_CLOCK_INFO */
PACKED_TYPE(tpm_clock_info_t,
struct tpm_clock_info {
	uint64_t timestamp_ms;
	uint32_t reset_count;   /* obfuscated by tpm */
	uint32_t restart_count; /* obfuscated by tpm */
	uint8_t  safe;          /* 1 if timestamp_ms is current */
})

/* Part 2, 10.12.8 TPMS_ATTEST */
PACKED_TYPE(tpm_sha1_attest_t,
struct tpm_sha1_attest {
	uint32_t          magic;     /* TPM_MAGIC */
	uint16_t          type;      /* TPM_ST_ATTEST_CERTIFY */
	tpm_sha256_name_t signer;    /* full tpm path of signing key */
	tpm_sha1_data_t   data;      /* signed sha1 */
	tpm_clock_info_t  clock;
	uint64_t          fwversion; /* obfuscated by tpm */
	tpm_sha256_name_t name;      /* sha256 of tpm_rs256_pubarea_t */
	tpm_sha256_name_t qual_name; /* full tpm path of attested key */
})

/* Part 2, 11.2.4.5: TPM2B_PUBLIC_KEY_RSA */
PACKED_TYPE(tpm_rs256_key_t,
struct tpm_rs256_key {
	uint16_t size; /* sizeof(body) */
	uint8_t  body[256];
})

/* Part 2, 11.2.5.1: TPM2B_ECC_PARAMETER */
PACKED_TYPE(tpm_es256_coord_t,
struct tpm_es256_coord {
	uint16_t size; /* sizeof(body) */
	uint8_t  body[32];
})

/* Part 2, 11.2.5.2: TPMS_ECC_POINT */
PACKED_TYPE(tpm_es256_point_t,
struct tpm_es256_point {
	tpm_es256_coord_t x;
	tpm_es256_coord_t y;
})

/* Part 2, 12.2.3.5: TPMS_RSA_PARMS */
PACKED_TYPE(tpm_rs256_param_t,
struct tpm_rs256_param {
	uint16_t symmetric; /* TPM_ALG_NULL */
	uint16_t scheme;    /* TPM_ALG_NULL */
	uint16_t keybits;   /* 2048 */
	uint32_t exponent;  /* zero (meaning 2^16 + 1) */
})

/* Part 2, 12.2.3.6: TPMS_ECC_PARMS */
PACKED_TYPE(tpm_es256_param_t,
struct tpm_es256_param {
	uint16_t symmetric; /* TPM_ALG_NULL */
	uint16_t scheme;    /* TPM_ALG_NULL */
	uint16_t curve_id;  /* TPM_ECC_P256 */
	uint16_t kdf;       /* TPM_ALG_NULL */
})

/* Part 2, 12.2.4: TPMT_PUBLIC */
PACKED_TYPE(tpm_rs256_pubarea_t,
struct tpm_rs256_pubarea {
	uint16_t            alg;    /* TPM_ALG_RSA */
	uint16_t            hash;   /* TPM_ALG_SHA256 */
	uint32_t            attr;
	tpm_sha256_digest_t policy; /* must be present? */
	tpm_rs256_param_t   param;
	tpm_rs256_key_t     key;
})

/* Part 2, 12.2.4: TPMT_PUBLIC */
PACKED_TYPE(tpm_es256_pubarea_t,
struct tpm_es256_pubarea {
	uint16_t            alg;    /* TPM_ALG_ECC */
	uint16_t            hash;   /* TPM_ALG_SHA256 */
	uint32_t            attr;
	tpm_sha256_digest_t policy; /* must be present? */
	tpm_es256_param_t   param;
	tpm_es256_point_t   point;
})

static int
get_signed_sha1(tpm_sha1_data_t *dgst, const fido_blob_t *authdata,
    const fido_blob_t *clientdata)
{
	const EVP_MD	*md = NULL;
	EVP_MD_CTX	*ctx = NULL;
	int		 ok = -1;

	if ((dgst->size = sizeof(dgst->body)) != SHA_DIGEST_LENGTH ||
	    (md = EVP_sha1()) == NULL ||
	    (ctx = EVP_MD_CTX_new()) == NULL ||
	    EVP_DigestInit_ex(ctx, md, NULL) != 1 ||
	    EVP_DigestUpdate(ctx, authdata->ptr, authdata->len) != 1 ||
	    EVP_DigestUpdate(ctx, clientdata->ptr, clientdata->len) != 1 ||
	    EVP_DigestFinal_ex(ctx, dgst->body, NULL) != 1) {
		fido_log_debug("%s: sha1", __func__);
		goto fail;
	}

	ok = 0;
fail:
	EVP_MD_CTX_free(ctx);

	return (ok);
}

static int
get_signed_name(tpm_sha256_name_t *name, const fido_blob_t *pubarea)
{
	name->alg = TPM_ALG_SHA256;
	name->size = sizeof(name->alg) + sizeof(name->body);
	if (sizeof(name->body) != SHA256_DIGEST_LENGTH ||
	    SHA256(pubarea->ptr, pubarea->len, name->body) != name->body) {
		fido_log_debug("%s: sha256", __func__);
		return -1;
	}

	return 0;
}

static void
bswap_rs256_pubarea(tpm_rs256_pubarea_t *x)
{
	x->alg = htobe16(x->alg);
	x->hash = htobe16(x->hash);
	x->attr = htobe32(x->attr);
	x->policy.size = htobe16(x->policy.size);
	x->param.symmetric = htobe16(x->param.symmetric);
	x->param.scheme = htobe16(x->param.scheme);
	x->param.keybits = htobe16(x->param.keybits);
	x->key.size = htobe16(x->key.size);
}

static void
bswap_es256_pubarea(tpm_es256_pubarea_t *x)
{
	x->alg = htobe16(x->alg);
	x->hash = htobe16(x->hash);
	x->attr = htobe32(x->attr);
	x->policy.size = htobe16(x->policy.size);
	x->param.symmetric = htobe16(x->param.symmetric);
	x->param.scheme = htobe16(x->param.scheme);
	x->param.curve_id = htobe16(x->param.curve_id);
	x->param.kdf = htobe16(x->param.kdf);
	x->point.x.size = htobe16(x->point.x.size);
	x->point.y.size = htobe16(x->point.y.size);
}

static void
bswap_sha1_certinfo(tpm_sha1_attest_t *x)
{
	x->magic = htobe32(x->magic);
	x->type = htobe16(x->type);
	x->signer.size = htobe16(x->signer.size);
	x->data.size = htobe16(x->data.size);
	x->name.alg = htobe16(x->name.alg);
	x->name.size = htobe16(x->name.size);
}

static int
check_rs256_pubarea(const fido_blob_t *buf, const rs256_pk_t *pk)
{
	const tpm_rs256_pubarea_t	*actual;
	tpm_rs256_pubarea_t		 expected;
	int				 ok;

	if (buf->len != sizeof(*actual)) {
		fido_log_debug("%s: buf->len=%zu", __func__, buf->len);
		return -1;
	}
	actual = (const void *)buf->ptr;

	memset(&expected, 0, sizeof(expected));
	expected.alg = TPM_ALG_RSA;
	expected.hash = TPM_ALG_SHA256;
	expected.attr = be32toh(actual->attr);
	expected.attr &= ~(TPMA_RESERVED|TPMA_CLEAR);
	expected.attr |= (TPMA_FIXED|TPMA_FIXED_P|TPMA_SENSITIVE|TPMA_SIGN);
	expected.policy = actual->policy;
	expected.policy.size = sizeof(expected.policy.body);
	expected.param.symmetric = TPM_ALG_NULL;
	expected.param.scheme = TPM_ALG_NULL;
	expected.param.keybits = 2048;
	expected.param.exponent = 0; /* meaning 2^16+1 */
	expected.key.size = sizeof(expected.key.body);
	memcpy(&expected.key.body, &pk->n, sizeof(expected.key.body));
	bswap_rs256_pubarea(&expected);

	ok = timingsafe_bcmp(&expected, actual, sizeof(expected));
	explicit_bzero(&expected, sizeof(expected));

	return ok != 0 ? -1 : 0;
}

static int
check_es256_pubarea(const fido_blob_t *buf, const es256_pk_t *pk)
{
	const tpm_es256_pubarea_t	*actual;
	tpm_es256_pubarea_t		 expected;
	int				 ok;

	if (buf->len != sizeof(*actual)) {
		fido_log_debug("%s: buf->len=%zu", __func__, buf->len);
		return -1;
	}
	actual = (const void *)buf->ptr;

	memset(&expected, 0, sizeof(expected));
	expected.alg = TPM_ALG_ECC;
	expected.hash = TPM_ALG_SHA256;
	expected.attr = be32toh(actual->attr);
	expected.attr &= ~(TPMA_RESERVED|TPMA_CLEAR);
	expected.attr |= (TPMA_FIXED|TPMA_FIXED_P|TPMA_SENSITIVE|TPMA_SIGN);
	expected.policy = actual->policy;
	expected.policy.size = sizeof(expected.policy.body);
	expected.param.symmetric = TPM_ALG_NULL;
	expected.param.scheme = TPM_ALG_NULL; /* TCG Alg. Registry, 5.2.4 */
	expected.param.curve_id = TPM_ECC_P256;
	expected.param.kdf = TPM_ALG_NULL;
	expected.point.x.size = sizeof(expected.point.x.body);
	expected.point.y.size = sizeof(expected.point.y.body);
	memcpy(&expected.point.x.body, &pk->x, sizeof(expected.point.x.body));
	memcpy(&expected.point.y.body, &pk->y, sizeof(expected.point.y.body));
	bswap_es256_pubarea(&expected);

	ok = timingsafe_bcmp(&expected, actual, sizeof(expected));
	explicit_bzero(&expected, sizeof(expected));

	return ok != 0 ? -1 : 0;
}

static int
check_sha1_certinfo(const fido_blob_t *buf, const fido_blob_t *clientdata_hash,
    const fido_blob_t *authdata_raw, const fido_blob_t *pubarea)
{
	const tpm_sha1_attest_t	*actual;
	tpm_sha1_attest_t	 expected;
	tpm_sha1_data_t		 signed_data;
	tpm_sha256_name_t	 signed_name;
	int			 ok = -1;

	memset(&signed_data, 0, sizeof(signed_data));
	memset(&signed_name, 0, sizeof(signed_name));

	if (get_signed_sha1(&signed_data, authdata_raw, clientdata_hash) < 0 ||
	    get_signed_name(&signed_name, pubarea) < 0) {
		fido_log_debug("%s: get_signed_sha1/name", __func__);
		goto fail;
	}
	if (buf->len != sizeof(*actual)) {
		fido_log_debug("%s: buf->len=%zu", __func__, buf->len);
		goto fail;
	}
	actual = (const void *)buf->ptr;

	memset(&expected, 0, sizeof(expected));
	expected.magic = TPM_MAGIC;
	expected.type = TPM_ST_CERTIFY;
	expected.signer = actual->signer;
	expected.signer.size = sizeof(expected.signer.alg) +
	    sizeof(expected.signer.body);
	expected.data = signed_data;
	expected.clock = actual->clock;
	expected.clock.safe = 1;
	expected.fwversion = actual->fwversion;
	expected.name = signed_name;
	expected.qual_name = actual->qual_name;
	bswap_sha1_certinfo(&expected);

	ok = timingsafe_bcmp(&expected, actual, sizeof(expected));
fail:
	explicit_bzero(&expected, sizeof(expected));
	explicit_bzero(&signed_data, sizeof(signed_data));
	explicit_bzero(&signed_name, sizeof(signed_name));

	return ok != 0 ? -1 : 0;
}

int
fido_get_signed_hash_tpm(fido_blob_t *dgst, const fido_blob_t *clientdata_hash,
    const fido_blob_t *authdata_raw, const fido_attstmt_t *attstmt,
    const fido_attcred_t *attcred)
{
	const fido_blob_t *pubarea = &attstmt->pubarea;
	const fido_blob_t *certinfo = &attstmt->certinfo;

	if (attstmt->alg != COSE_RS1) {
		fido_log_debug("%s: unsupported alg %d", __func__,
		    attstmt->alg);
		return -1;
	}

	switch (attcred->type) {
	case COSE_ES256:
		if (check_es256_pubarea(pubarea, &attcred->pubkey.es256) < 0) {
			fido_log_debug("%s: check_es256_pubarea", __func__);
			return -1;
		}
		break;
	case COSE_RS256:
		if (check_rs256_pubarea(pubarea, &attcred->pubkey.rs256) < 0) {
			fido_log_debug("%s: check_rs256_pubarea", __func__);
			return -1;
		}
		break;
	default:
		fido_log_debug("%s: unsupported type %d", __func__,
		    attcred->type);
		return -1;
	}

	if (check_sha1_certinfo(certinfo, clientdata_hash, authdata_raw,
	    pubarea) < 0) {
		fido_log_debug("%s: check_sha1_certinfo", __func__);
		return -1;
	}

	if (dgst->len < SHA_DIGEST_LENGTH ||
	    SHA1(certinfo->ptr, certinfo->len, dgst->ptr) != dgst->ptr) {
		fido_log_debug("%s: sha1", __func__);
		return -1;
	}
	dgst->len = SHA_DIGEST_LENGTH;

	return 0;
}
