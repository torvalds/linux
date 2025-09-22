/*	$OpenBSD: evp_test.c,v 1.21 2025/05/22 00:13:47 kenjiro Exp $ */
/*
 * Copyright (c) 2017, 2022 Joel Sing <jsing@openbsd.org>
 * Copyright (c) 2023, 2024 Theo Buehler <tb@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <string.h>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/objects.h>
#include <openssl/ossl_typ.h>

static int
evp_asn1_method_test(void)
{
	const EVP_PKEY_ASN1_METHOD *method;
	int count, pkey_id, i;
	int failed = 1;

	if ((count = EVP_PKEY_asn1_get_count()) < 1) {
		fprintf(stderr, "FAIL: failed to get pkey asn1 method count\n");
		goto failure;
	}
	for (i = 0; i < count; i++) {
		if ((method = EVP_PKEY_asn1_get0(i)) == NULL) {
			fprintf(stderr, "FAIL: failed to get pkey %d\n", i);
			goto failure;
		}
	}

	if ((method = EVP_PKEY_asn1_find(NULL, EVP_PKEY_RSA)) == NULL) {
		fprintf(stderr, "FAIL: failed to find RSA method\n");
		goto failure;
	}
	if (!EVP_PKEY_asn1_get0_info(&pkey_id, NULL, NULL, NULL, NULL, method)) {
		fprintf(stderr, "FAIL: failed to get RSA method info\n");
		goto failure;
	}
	if (pkey_id != EVP_PKEY_RSA) {
		fprintf(stderr, "FAIL: method ID mismatch (%d != %d)\n",
		    pkey_id, EVP_PKEY_RSA);
		goto failure;
	}

	if ((method = EVP_PKEY_asn1_find(NULL, EVP_PKEY_RSA_PSS)) == NULL) {
		fprintf(stderr, "FAIL: failed to find RSA-PSS method\n");
		goto failure;
	}
	if (!EVP_PKEY_asn1_get0_info(&pkey_id, NULL, NULL, NULL, NULL, method)) {
		fprintf(stderr, "FAIL: failed to get RSA-PSS method info\n");
		goto failure;
	}
	if (pkey_id != EVP_PKEY_RSA_PSS) {
		fprintf(stderr, "FAIL: method ID mismatch (%d != %d)\n",
		    pkey_id, EVP_PKEY_RSA_PSS);
		goto failure;
	}

	if ((method = EVP_PKEY_asn1_find_str(NULL, "RSA", -1)) == NULL) {
		fprintf(stderr, "FAIL: failed to find RSA method by str\n");
		goto failure;
	}
	if (!EVP_PKEY_asn1_get0_info(&pkey_id, NULL, NULL, NULL, NULL, method)) {
		fprintf(stderr, "FAIL: failed to get RSA method info\n");
		goto failure;
	}
	if (pkey_id != EVP_PKEY_RSA) {
		fprintf(stderr, "FAIL: method ID mismatch (%d != %d)\n",
		    pkey_id, EVP_PKEY_RSA);
		goto failure;
	}

	if ((method = EVP_PKEY_asn1_find_str(NULL, "RSA-PSS", -1)) == NULL) {
		fprintf(stderr, "FAIL: failed to find RSA-PSS method\n");
		goto failure;
	}
	if (!EVP_PKEY_asn1_get0_info(&pkey_id, NULL, NULL, NULL, NULL, method)) {
		fprintf(stderr, "FAIL: failed to get RSA-PSS method info\n");
		goto failure;
	}
	if (pkey_id != EVP_PKEY_RSA_PSS) {
		fprintf(stderr, "FAIL: method ID mismatch (%d != %d)\n",
		    pkey_id, EVP_PKEY_RSA_PSS);
		goto failure;
	}

	failed = 0;

 failure:

	return failed;
}

/* EVP_PKEY_asn1_find() by hand. Allows cross-checking and finding duplicates. */
static const EVP_PKEY_ASN1_METHOD *
evp_pkey_asn1_find(int nid, int skip_id)
{
	const EVP_PKEY_ASN1_METHOD *ameth;
	int count, i, pkey_id;

	count = EVP_PKEY_asn1_get_count();
	for (i = 0; i < count; i++) {
		if (i == skip_id)
			continue;
		if ((ameth = EVP_PKEY_asn1_get0(i)) == NULL)
			return NULL;
		if (!EVP_PKEY_asn1_get0_info(&pkey_id, NULL, NULL,
		    NULL, NULL, ameth))
			return NULL;
		if (pkey_id == nid)
			return ameth;
	}

	return NULL;
}

static int
evp_asn1_method_aliases_test(void)
{
	const EVP_PKEY_ASN1_METHOD *ameth;
	int id, base_id, flags;
	const char *info, *pem_str;
	int count, i;
	int failed = 0;

	if ((count = EVP_PKEY_asn1_get_count()) <= 0) {
		fprintf(stderr, "FAIL: EVP_PKEY_asn1_get_count(): %d\n", count);
		failed |= 1;
	}
	for (i = 0; i < count; i++) {
		if ((ameth = EVP_PKEY_asn1_get0(i)) == NULL) {
			fprintf(stderr, "FAIL: no ameth for index %d < %d\n",
			    i, count);
			failed |= 1;
			continue;
		}
		if (!EVP_PKEY_asn1_get0_info(&id, &base_id, &flags,
		    &info, &pem_str, ameth)) {
			fprintf(stderr, "FAIL: no info for ameth %d\n", i);
			failed |= 1;
			continue;
		}

		/*
		 * The following are all true or all false for any ameth:
		 * 1. ASN1_PKEY_ALIAS is set	2. id != base_id
		 * 3. info == NULL		4. pem_str == NULL
		 */

		if ((flags & ASN1_PKEY_ALIAS) == 0) {
			size_t pem_str_len;

			if (id != base_id) {
				fprintf(stderr, "FAIL: non-alias with "
				    "id %d != base_id %d\n", id, base_id);
				failed |= 1;
				continue;
			}
			if (info == NULL || strlen(info) == 0) {
				fprintf(stderr, "FAIL: missing or empty info %d\n", id);
				failed |= 1;
				continue;
			}
			if (pem_str == NULL) {
				fprintf(stderr, "FAIL: missing pem_str %d\n", id);
				failed |= 1;
				continue;
			}
			if ((pem_str_len = strlen(pem_str)) == 0) {
				fprintf(stderr, "FAIL: empty pem_str %d\n", id);
				failed |= 1;
				continue;
			}

			if (evp_pkey_asn1_find(id, i) != NULL) {
				fprintf(stderr, "FAIL: duplicate ameth %d\n", id);
				failed |= 1;
				continue;
			}

			if (ameth != EVP_PKEY_asn1_find(NULL, id)) {
				fprintf(stderr, "FAIL: EVP_PKEY_asn1_find(%d) "
				    "returned different ameth\n", id);
				failed |= 1;
				continue;
			}
			if (ameth != EVP_PKEY_asn1_find_str(NULL, pem_str, -1)) {
				fprintf(stderr, "FAIL: EVP_PKEY_asn1_find_str(%s) "
				    "returned different ameth\n", pem_str);
				failed |= 1;
				continue;
			}
			if (ameth != EVP_PKEY_asn1_find_str(NULL,
			    pem_str, pem_str_len)) {
				fprintf(stderr, "FAIL: EVP_PKEY_asn1_find_str(%s, %zu) "
				    "returned different ameth\n", pem_str, pem_str_len);
				failed |= 1;
				continue;
			}
			if (EVP_PKEY_asn1_find_str(NULL, pem_str,
			    pem_str_len - 1) != NULL) {
				fprintf(stderr, "FAIL: EVP_PKEY_asn1_find_str(%s, %zu) "
				    "returned an ameth\n", pem_str, pem_str_len - 1);
				failed |= 1;
				continue;
			}
			continue;
		}

		if (id == base_id) {
			fprintf(stderr, "FAIL: alias with id %d == base_id %d\n",
			    id, base_id);
			failed |= 1;
		}
		if (info != NULL) {
			fprintf(stderr, "FAIL: alias %d with info %s\n", id, info);
			failed |= 1;
		}
		if (pem_str != NULL) {
			fprintf(stderr, "FAIL: alias %d with pem_str %s\n",
			    id, pem_str);
			failed |= 1;
		}

		/* Check that ameth resolves to a non-alias. */
		if ((ameth = evp_pkey_asn1_find(base_id, -1)) == NULL) {
			fprintf(stderr, "FAIL: no ameth with pkey_id %d\n",
			    base_id);
			failed |= 1;
			continue;
		}
		if (!EVP_PKEY_asn1_get0_info(NULL, NULL, &flags, NULL, NULL, ameth)) {
			fprintf(stderr, "FAIL: no info for ameth with pkey_id %d\n",
			    base_id);
			failed |= 1;
			continue;
		}
		if ((flags & ASN1_PKEY_ALIAS) != 0) {
			fprintf(stderr, "FAIL: ameth with pkey_id %d "
			    "resolves to another alias\n", base_id);
			failed |= 1;
		}
	}

	return failed;
}

static const struct evp_iv_len_test {
	const EVP_CIPHER *(*cipher)(void);
	int iv_len;
	int setlen;
	int expect;
} evp_iv_len_tests[] = {
	{
		.cipher = EVP_aes_128_ccm,
		.iv_len = 7,
		.setlen = 11,
		.expect = 1,
	},
	{
		.cipher = EVP_aes_128_ccm,
		.iv_len = 7,
		.setlen = 6,
		.expect = 0,
	},
	{
		.cipher = EVP_aes_128_ccm,
		.iv_len = 7,
		.setlen = 13,
		.expect = 1,
	},
	{
		.cipher = EVP_aes_128_ccm,
		.iv_len = 7,
		.setlen = 14,
		.expect = 0,
	},

	{
		.cipher = EVP_aes_192_ccm,
		.iv_len = 7,
		.setlen = 11,
		.expect = 1,
	},
	{
		.cipher = EVP_aes_192_ccm,
		.iv_len = 7,
		.setlen = 6,
		.expect = 0,
	},
	{
		.cipher = EVP_aes_192_ccm,
		.iv_len = 7,
		.setlen = 13,
		.expect = 1,
	},
	{
		.cipher = EVP_aes_192_ccm,
		.iv_len = 7,
		.setlen = 14,
		.expect = 0,
	},

	{
		.cipher = EVP_aes_256_ccm,
		.iv_len = 7,
		.setlen = 11,
		.expect = 1,
	},
	{
		.cipher = EVP_aes_256_ccm,
		.iv_len = 7,
		.setlen = 6,
		.expect = 0,
	},
	{
		.cipher = EVP_aes_256_ccm,
		.iv_len = 7,
		.setlen = 13,
		.expect = 1,
	},
	{
		.cipher = EVP_aes_256_ccm,
		.iv_len = 7,
		.setlen = 14,
		.expect = 0,
	},

	{
		.cipher = EVP_aes_128_gcm,
		.iv_len = 12,
		.setlen = 16,
		.expect = 1,
	},
	{
		.cipher = EVP_aes_128_gcm,
		.iv_len = 12,
		.setlen = 0,
		.expect = 0,
	},
	{
		.cipher = EVP_aes_128_gcm,
		.iv_len = 12,
		.setlen = 1,
		.expect = 1,
	},
	/* XXX - GCM IV length isn't capped... */
	{
		.cipher = EVP_aes_128_gcm,
		.iv_len = 12,
		.setlen = 1024 * 1024,
		.expect = 1,
	},

	{
		.cipher = EVP_aes_192_gcm,
		.iv_len = 12,
		.setlen = 16,
		.expect = 1,
	},
	{
		.cipher = EVP_aes_192_gcm,
		.iv_len = 12,
		.setlen = 0,
		.expect = 0,
	},
	{
		.cipher = EVP_aes_192_gcm,
		.iv_len = 12,
		.setlen = 1,
		.expect = 1,
	},
	/* XXX - GCM IV length isn't capped... */
	{
		.cipher = EVP_aes_128_gcm,
		.iv_len = 12,
		.setlen = 1024 * 1024,
		.expect = 1,
	},

	{
		.cipher = EVP_aes_256_gcm,
		.iv_len = 12,
		.setlen = 16,
		.expect = 1,
	},
	{
		.cipher = EVP_aes_256_gcm,
		.iv_len = 12,
		.setlen = 0,
		.expect = 0,
	},
	{
		.cipher = EVP_aes_256_gcm,
		.iv_len = 12,
		.setlen = 1,
		.expect = 1,
	},
	/* XXX - GCM IV length isn't capped... */
	{
		.cipher = EVP_aes_128_gcm,
		.iv_len = 12,
		.setlen = 1024 * 1024,
		.expect = 1,
	},

	{
		.cipher = EVP_aes_128_ecb,
		.iv_len = 0,
		.setlen = 11,
		.expect = 0,
	},

	{
		.cipher = EVP_chacha20_poly1305,
		.iv_len = 12,
		.setlen = 11,
		.expect = 1,
	},
	{
		.cipher = EVP_chacha20_poly1305,
		.iv_len = 12,
		.setlen = 12,
		.expect = 1,
	},
	{
		.cipher = EVP_chacha20_poly1305,
		.iv_len = 12,
		.setlen = 13,
		.expect = 0,
	},
	{
		.cipher = EVP_chacha20_poly1305,
		.iv_len = 12,
		.setlen = 1,
		.expect = 1,
	},
	{
		.cipher = EVP_chacha20_poly1305,
		.iv_len = 12,
		.setlen = 0,
		.expect = 0,
	},
};

#define N_EVP_IV_LEN_TESTS \
    (sizeof(evp_iv_len_tests) / sizeof(evp_iv_len_tests[0]))

static int
evp_pkey_iv_len_testcase(const struct evp_iv_len_test *test)
{
	const EVP_CIPHER *cipher = test->cipher();
	const char *name;
	EVP_CIPHER_CTX *ctx;
	int ret;
	int failure = 1;

	assert(cipher != NULL);
	name = OBJ_nid2ln(EVP_CIPHER_nid(cipher));
	assert(name != NULL);

	if ((ctx = EVP_CIPHER_CTX_new()) == NULL) {
		fprintf(stderr, "FAIL: %s: EVP_CIPHER_CTX_new()\n", name);
		goto failure;
	}

	if ((ret = EVP_EncryptInit_ex(ctx, cipher, NULL, NULL, NULL)) <= 0) {
		fprintf(stderr, "FAIL: %s: EVP_EncryptInit_ex:"
		    " want %d, got %d\n", name, 1, ret);
		goto failure;
	}
	if ((ret = EVP_CIPHER_CTX_iv_length(ctx)) != test->iv_len) {
		fprintf(stderr, "FAIL: %s EVP_CIPHER_CTX_iv_length (before set)"
		    " want %d, got %d\n", name, test->iv_len, ret);
		goto failure;
	}
	if ((ret = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN,
	    test->setlen, NULL)) != test->expect) {
		fprintf(stderr, "FAIL: %s EVP_CIPHER_CTX_ctrl"
		    " want %d, got %d\n", name, test->expect, ret);
		goto failure;
	}
	if (test->expect == 0)
		goto done;
	if ((ret = EVP_CIPHER_CTX_iv_length(ctx)) != test->setlen) {
		fprintf(stderr, "FAIL: %s EVP_CIPHER_CTX_iv_length (after set)"
		    " want %d, got %d\n", name, test->setlen, ret);
		goto failure;
	}

 done:
	failure = 0;

 failure:
	EVP_CIPHER_CTX_free(ctx);

	return failure;
}

static int
evp_pkey_iv_len_test(void)
{
	size_t i;
	int failure = 0;

	for (i = 0; i < N_EVP_IV_LEN_TESTS; i++)
		failure |= evp_pkey_iv_len_testcase(&evp_iv_len_tests[i]);

	return failure;
}

struct do_all_arg {
	const char *previous;
	int failure;
};

static void
evp_do_all_cb_common(const char *descr, const void *ptr, const char *from,
    const char *to, struct do_all_arg *arg)
{
	const char *previous = arg->previous;

	assert(from != NULL);
	arg->previous = from;

	if (ptr == NULL && to == NULL) {
		arg->failure |= 1;
		fprintf(stderr, "FAIL: %s %s: method and alias both NULL\n",
		    descr, from);
	}
	if (ptr != NULL && to != NULL) {
		arg->failure |= 1;
		fprintf(stderr, "FAIL: %s %s has method and alias \"%s\"\n",
		    descr, from, to);
	}

	if (previous == NULL)
		return;

	if (strcmp(previous, from) >= 0) {
		arg->failure |= 1;
		fprintf(stderr, "FAIL: %ss %s and %s out of order\n", descr,
		    previous, from);
	}
}

static void
evp_cipher_do_all_cb(const EVP_CIPHER *cipher, const char *from, const char *to,
    void *arg)
{
	evp_do_all_cb_common("cipher", cipher, from, to, arg);
}

static void
evp_md_do_all_cb(const EVP_MD *md, const char *from, const char *to, void *arg)
{
	evp_do_all_cb_common("digest", md, from, to, arg);
}

static int
evp_do_all_test(void)
{
	struct do_all_arg arg;
	int failure = 0;

	memset(&arg, 0, sizeof(arg));
	EVP_CIPHER_do_all(evp_cipher_do_all_cb, &arg);
	failure |= arg.failure;

	memset(&arg, 0, sizeof(arg));
	EVP_MD_do_all(evp_md_do_all_cb, &arg);
	failure |= arg.failure;

	return failure;
}

static void
evp_cipher_aliases_cb(const EVP_CIPHER *cipher, const char *from, const char *to,
    void *arg)
{
	struct do_all_arg *do_all = arg;
	const EVP_CIPHER *from_cipher, *to_cipher;

	if (to == NULL)
		return;

	from_cipher = EVP_get_cipherbyname(from);
	to_cipher = EVP_get_cipherbyname(to);

	if (from_cipher != NULL && from_cipher == to_cipher)
		return;

	fprintf(stderr, "FAIL: cipher mismatch from \"%s\" to \"%s\": "
	    "from: %p, to: %p\n", from, to, from_cipher, to_cipher);
	do_all->failure |= 1;
}

static void
evp_digest_aliases_cb(const EVP_MD *digest, const char *from, const char *to,
    void *arg)
{
	struct do_all_arg *do_all = arg;
	const EVP_MD *from_digest, *to_digest;

	if (to == NULL)
		return;

	from_digest = EVP_get_digestbyname(from);
	to_digest = EVP_get_digestbyname(to);

	if (from_digest != NULL && from_digest == to_digest)
		return;

	fprintf(stderr, "FAIL: digest mismatch from \"%s\" to \"%s\": "
	    "from: %p, to: %p\n", from, to, from_digest, to_digest);
	do_all->failure |= 1;
}

static int
evp_aliases_test(void)
{
	struct do_all_arg arg;
	int failure = 0;

	memset(&arg, 0, sizeof(arg));
	EVP_CIPHER_do_all(evp_cipher_aliases_cb, &arg);
	failure |= arg.failure;

	memset(&arg, 0, sizeof(arg));
	EVP_MD_do_all(evp_digest_aliases_cb, &arg);
	failure |= arg.failure;

	return failure;
}

static void
obj_name_cb(const OBJ_NAME *obj_name, void *do_all_arg)
{
	struct do_all_arg *arg = do_all_arg;
	struct do_all_arg arg_copy = *arg;
	const char *previous = arg->previous;
	const char *descr = "OBJ_NAME unknown";

	assert(obj_name->name != NULL);
	arg->previous = obj_name->name;

	if (obj_name->type == OBJ_NAME_TYPE_CIPHER_METH) {
		descr = "OBJ_NAME cipher";

		if (obj_name->alias == 0) {
			const EVP_CIPHER *cipher;

			if ((cipher = EVP_get_cipherbyname(obj_name->name)) !=
			    (const EVP_CIPHER *)obj_name->data) {
				arg->failure |= 1;
				fprintf(stderr, "FAIL: %s by name %p != %p\n",
				    descr, cipher, obj_name->data);
			}

			evp_do_all_cb_common(descr, obj_name->data,
			    obj_name->name, NULL, &arg_copy);
		} else if (obj_name->alias == OBJ_NAME_ALIAS) {
			evp_cipher_aliases_cb(NULL, obj_name->name,
			    obj_name->data, &arg_copy);
		} else {
			fprintf(stderr, "FAIL %s %s: unexpected alias value %d\n",
			    descr, obj_name->name, obj_name->alias);
			arg->failure |= 1;
		}
	} else if (obj_name->type == OBJ_NAME_TYPE_MD_METH) {
		descr = "OBJ_NAME digest";

		if (obj_name->alias == 0) {
			const EVP_MD *evp_md;

			if ((evp_md = EVP_get_digestbyname(obj_name->name)) !=
			    (const EVP_MD *)obj_name->data) {
				arg->failure |= 1;
				fprintf(stderr, "FAIL: %s by name %p != %p\n",
				    descr, evp_md, obj_name->data);
			}

			evp_do_all_cb_common(descr, obj_name->data,
			    obj_name->name, NULL, &arg_copy);
		} else if (obj_name->alias == OBJ_NAME_ALIAS) {
			evp_digest_aliases_cb(NULL, obj_name->name,
			    obj_name->data, &arg_copy);
		} else {
			fprintf(stderr, "FAIL: %s %s: unexpected alias value %d\n",
			    descr, obj_name->name, obj_name->alias);
			arg->failure |= 1;
		}
	} else {
		fprintf(stderr, "FAIL: unexpected OBJ_NAME type %d\n",
		    obj_name->type);
		arg->failure |= 1;
	}

	if (previous != NULL && strcmp(previous, obj_name->name) >= 0) {
		arg->failure |= 1;
		fprintf(stderr, "FAIL: %ss %s and %s out of order\n", descr,
		    previous, obj_name->name);
	}

	arg->failure |= arg_copy.failure;
}

static int
obj_name_do_all_test(void)
{
	struct do_all_arg arg;
	int failure = 0;

	memset(&arg, 0, sizeof(arg));
	OBJ_NAME_do_all(OBJ_NAME_TYPE_CIPHER_METH, obj_name_cb, &arg);
	failure |= arg.failure;

	memset(&arg, 0, sizeof(arg));
	OBJ_NAME_do_all(OBJ_NAME_TYPE_MD_METH, obj_name_cb, &arg);
	failure |= arg.failure;

	return failure;
}

static int
evp_get_cipherbyname_test(void)
{
	int failure = 0;

	/* Should handle NULL gracefully */
	failure |= EVP_get_cipherbyname(NULL) != NULL;

	return failure;
}

static int
evp_get_digestbyname_test(void)
{
	int failure = 0;

	/* Should handle NULL gracefully */
	failure |= EVP_get_digestbyname(NULL) != NULL;

	return failure;
}

static void
hexdump(const unsigned char *buf, int len)
{
	int i;

	if (len <= 0) {
		fprintf(stderr, "<negative length %d>\n", len);
		return;
	}

	for (i = 1; i <= len; i++)
		fprintf(stderr, " 0x%02hhx,%s", buf[i - 1], i % 8 ? "" : "\n");

	fprintf(stderr, "\n");
}

static int
kdf_compare_bytes(const char *label, const unsigned char *d1, int len1,
    const unsigned char *d2, int len2)
{
	if (len1 != len2) {
		fprintf(stderr, "FAIL: %s - byte lengths differ "
		    "(%d != %d)\n", label, len1, len2);
		fprintf(stderr, "Got:\n");
		hexdump(d1, len1);
		fprintf(stderr, "Want:\n");
		hexdump(d2, len2);
		return 0;
	}
	if (memcmp(d1, d2, len1) != 0) {
		fprintf(stderr, "FAIL: %s - bytes differ\n", label);
		fprintf(stderr, "Got:\n");
		hexdump(d1, len1);
		fprintf(stderr, "Want:\n");
		hexdump(d2, len2);
		return 0;
	}
	return 1;
}

static int
evp_kdf_hkdf_basic(void)
{
	EVP_PKEY_CTX *pctx;
	unsigned char out[42];
	size_t outlen = sizeof(out);
	int failed = 1;

	/* Test vector from RFC 5869, Appendix A.1. */
	const unsigned char ikm[] = {
		0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
		0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
		0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
		0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
		0x0b, 0x0b,
	};
	const unsigned char salt[] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
		0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
		0x0c,
	};
	const unsigned char info[] = {
		0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5,
		0xf6, 0xf7, 0xf8, 0xf9,
	};
	const unsigned char expected[42] = {
		0x3c, 0xb2, 0x5f, 0x25, 0xfa, 0xac, 0xd5, 0x7a,
		0x90, 0x43, 0x4f, 0x64, 0xd0, 0x36, 0x2f, 0x2a,
		0x2d, 0x2d, 0x0a, 0x90, 0xcf, 0x1a, 0x5a, 0x4c,
		0x5d, 0xb0, 0x2d, 0x56, 0xec, 0xc4, 0xc5, 0xbf,
		0x34, 0x00, 0x72, 0x08, 0xd5, 0xb8, 0x87, 0x18,
		0x58, 0x65,
	};

	if ((pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, NULL)) == NULL) {
		fprintf(stderr, "FAIL: EVP_PKEY_CTX_new_id\n");
		goto err;
	}

	if (EVP_PKEY_derive_init(pctx) <= 0) {
		fprintf(stderr, "FAIL: EVP_PKEY_derive_init\n");
		goto err;
	}

	if (EVP_PKEY_CTX_set_hkdf_md(pctx, EVP_sha256()) <= 0) {
		fprintf(stderr, "FAIL: EVP_PKEY_CTX_set_hkdf_md\n");
		goto err;
	}

	if (EVP_PKEY_CTX_set1_hkdf_salt(pctx, salt, sizeof(salt)) <= 0) {
		fprintf(stderr, "FAIL: EVP_PKEY_CTX_set1_hkdf_salt\n");
		goto err;
	}

	if (EVP_PKEY_CTX_set1_hkdf_key(pctx, ikm, sizeof(ikm)) <= 0) {
		fprintf(stderr, "FAIL: EVP_PKEY_CTX_set1_hkdf_key\n");
		goto err;
	}

	if (EVP_PKEY_CTX_add1_hkdf_info(pctx, info, sizeof(info)) <= 0) {
		fprintf(stderr, "FAIL: EVP_PKEY_CTX_add1_hkdf_info\n");
		goto err;
	}

	if (EVP_PKEY_derive(pctx, out, &outlen) <= 0) {
		fprintf(stderr, "FAIL: EVP_PKEY_derive\n");
		goto err;
	}

	if (!kdf_compare_bytes("HKDF test", out, outlen, expected, sizeof(expected)))
		goto err;

	failed = 0;

 err:
	EVP_PKEY_CTX_free(pctx);
	return failed;
}

static int
evp_kdf_tls1_prf_basic(void)
{
	EVP_PKEY_CTX *pctx;
	unsigned char got[16];
	size_t got_len = sizeof(got);
	unsigned char want[16] = {
		0x8e, 0x4d, 0x93, 0x25, 0x30, 0xd7, 0x65, 0xa0,
		0xaa, 0xe9, 0x74, 0xc3, 0x04, 0x73, 0x5e, 0xcc,
	};
	int failed = 1;

	if ((pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_TLS1_PRF, NULL)) == NULL) {
		fprintf(stderr, "FAIL: EVP_PKEY_CTX_new_id\n");
		goto err;
	}

	if (EVP_PKEY_derive_init(pctx) <= 0) {
		fprintf(stderr, "FAIL: EVP_PKEY_derive_init\n");
		goto err;
	}

	if (EVP_PKEY_CTX_set_tls1_prf_md(pctx, EVP_sha256()) <= 0) {
		fprintf(stderr, "FAIL: EVP_PKEY_CTX_set1_tls1_prf_md\n");
		goto err;
	}

	if (EVP_PKEY_CTX_set1_tls1_prf_secret(pctx, "secret", 6) <= 0) {
		fprintf(stderr, "FAIL: EVP_PKEY_CTX_set1_tls1_prf_secret\n");
		goto err;
	}

	if (EVP_PKEY_CTX_add1_tls1_prf_seed(pctx, "seed", 4) <= 0) {
		fprintf(stderr, "FAIL: EVP_PKEY_CTX_set1_tls1_prf_seed\n");
		goto err;
	}

	if (EVP_PKEY_derive(pctx, got, &got_len) <= 0) {
		fprintf(stderr, "FAIL: EVP_PKEY_derive\n");
		goto err;
	}

	if (!kdf_compare_bytes("kdf test", got, got_len, want, sizeof(want)))
		goto err;

	failed = 0;

 err:
	EVP_PKEY_CTX_free(pctx);

	return failed;
}

#define TLS_PRF_OUT_LEN 128

static const struct tls_prf_test {
	const unsigned char *desc;
	const EVP_MD *(*md)(void);
	const uint16_t cipher_value;
	const unsigned char out[TLS_PRF_OUT_LEN];
} tls_prf_tests[] = {
	{
		.desc = "MD5+SHA1",
		.md = EVP_md5_sha1,
		.cipher_value = 0x0033,
		.out = {
			0x03, 0xa1, 0xc1, 0x7d, 0x2c, 0xa5, 0x3d, 0xe8,
			0x9d, 0x59, 0x5e, 0x30, 0xf5, 0x71, 0xbb, 0x96,
			0xde, 0x5c, 0x8e, 0xdc, 0x25, 0x8a, 0x7c, 0x05,
			0x9f, 0x7d, 0x35, 0x29, 0x45, 0xae, 0x56, 0xad,
			0x9f, 0x57, 0x15, 0x5c, 0xdb, 0x83, 0x3a, 0xac,
			0x19, 0xa8, 0x2b, 0x40, 0x72, 0x38, 0x1e, 0xed,
			0xf3, 0x25, 0xde, 0x84, 0x84, 0xd8, 0xd1, 0xfc,
			0x31, 0x85, 0x81, 0x12, 0x55, 0x4d, 0x12, 0xb5,
			0xed, 0x78, 0x5e, 0xba, 0xc8, 0xec, 0x8d, 0x28,
			0xa1, 0x21, 0x1e, 0x6e, 0x07, 0xf1, 0xfc, 0xf5,
			0xbf, 0xe4, 0x8e, 0x8e, 0x97, 0x15, 0x93, 0x85,
			0x75, 0xdd, 0x87, 0x09, 0xd0, 0x4e, 0xe5, 0xd5,
			0x9e, 0x1f, 0xd6, 0x1c, 0x3b, 0xe9, 0xad, 0xba,
			0xe0, 0x16, 0x56, 0x62, 0x90, 0xd6, 0x82, 0x84,
			0xec, 0x8a, 0x22, 0xbe, 0xdc, 0x6a, 0x5e, 0x05,
			0x12, 0x44, 0xec, 0x60, 0x61, 0xd1, 0x8a, 0x66,
		},
	},
	{
		.desc = "SHA256 (via TLSv1.2)",
		.md = EVP_sha256,
		.cipher_value = 0x0033,
		.out = {
			 0x37, 0xa7, 0x06, 0x71, 0x6e, 0x19, 0x19, 0xda,
			 0x23, 0x8c, 0xcc, 0xb4, 0x2f, 0x31, 0x64, 0x9d,
			 0x05, 0x29, 0x1c, 0x33, 0x7e, 0x09, 0x1b, 0x0c,
			 0x0e, 0x23, 0xc1, 0xb0, 0x40, 0xcc, 0x31, 0xf7,
			 0x55, 0x66, 0x68, 0xd9, 0xa8, 0xae, 0x74, 0x75,
			 0xf3, 0x46, 0xe9, 0x3a, 0x54, 0x9d, 0xe0, 0x8b,
			 0x7e, 0x6c, 0x63, 0x1c, 0xfa, 0x2f, 0xfd, 0xc9,
			 0xd3, 0xf1, 0xd3, 0xfe, 0x7b, 0x9e, 0x14, 0x95,
			 0xb5, 0xd0, 0xad, 0x9b, 0xee, 0x78, 0x8c, 0x83,
			 0x18, 0x58, 0x7e, 0xa2, 0x23, 0xc1, 0x8b, 0x62,
			 0x94, 0x12, 0xcb, 0xb6, 0x60, 0x69, 0x32, 0xfe,
			 0x98, 0x0e, 0x93, 0xb0, 0x8e, 0x5c, 0xfb, 0x6e,
			 0xdb, 0x9a, 0xc2, 0x9f, 0x8c, 0x5c, 0x43, 0x19,
			 0xeb, 0x4a, 0x52, 0xad, 0x62, 0x2b, 0xdd, 0x9f,
			 0xa3, 0x74, 0xa6, 0x96, 0x61, 0x4d, 0x98, 0x40,
			 0x63, 0xa6, 0xd4, 0xbb, 0x17, 0x11, 0x75, 0xed,
		},
	},
	{
		.desc = "SHA384",
		.md = EVP_sha384,
		.cipher_value = 0x009d,
		.out = {
			 0x00, 0x93, 0xc3, 0xfd, 0xa7, 0xbb, 0xdc, 0x5b,
			 0x13, 0x3a, 0xe6, 0x8b, 0x1b, 0xac, 0xf3, 0xfb,
			 0x3c, 0x9a, 0x78, 0xf6, 0x19, 0xf0, 0x13, 0x0f,
			 0x0d, 0x01, 0x9d, 0xdf, 0x0a, 0x28, 0x38, 0xce,
			 0x1a, 0x9b, 0x43, 0xbe, 0x56, 0x12, 0xa7, 0x16,
			 0x58, 0xe1, 0x8a, 0xe4, 0xc5, 0xbb, 0x10, 0x4c,
			 0x3a, 0xf3, 0x7f, 0xd3, 0xdb, 0xe4, 0xe0, 0x3d,
			 0xcc, 0x83, 0xca, 0xf0, 0xf9, 0x69, 0xcc, 0x70,
			 0x83, 0x32, 0xf6, 0xfc, 0x81, 0x80, 0x02, 0xe8,
			 0x31, 0x1e, 0x7c, 0x3b, 0x34, 0xf7, 0x34, 0xd1,
			 0xcf, 0x2a, 0xc4, 0x36, 0x2f, 0xe9, 0xaa, 0x7f,
			 0x6d, 0x1f, 0x5e, 0x0e, 0x39, 0x05, 0x15, 0xe1,
			 0xa2, 0x9a, 0x4d, 0x97, 0x8c, 0x62, 0x46, 0xf1,
			 0x87, 0x65, 0xd8, 0xe9, 0x14, 0x11, 0xa6, 0x48,
			 0xd7, 0x0e, 0x6e, 0x70, 0xad, 0xfb, 0x3f, 0x36,
			 0x05, 0x76, 0x4b, 0xe4, 0x28, 0x50, 0x4a, 0xf2,
		},
	},
};

#define N_TLS_PRF_TESTS \
    (sizeof(tls_prf_tests) / sizeof(*tls_prf_tests))

#define TLS_PRF_SEED1	"tls prf seed 1"
#define TLS_PRF_SEED2	"tls prf seed 2"
#define TLS_PRF_SEED3	"tls prf seed 3"
#define TLS_PRF_SEED4	"tls prf seed 4"
#define TLS_PRF_SEED5	"tls prf seed 5"
#define TLS_PRF_SECRET	"tls prf secretz"

static int
do_tls_prf_evp_test(int test_no, const struct tls_prf_test *test)
{
	EVP_PKEY_CTX *pkey_ctx = NULL;
	unsigned char *out;
	size_t len, out_len;
	int failed = 1;

	if ((pkey_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_TLS1_PRF, NULL)) == NULL)
		errx(1, "EVP_PKEY_CTX_new_id");

	if ((out = malloc(TLS_PRF_OUT_LEN)) == NULL)
		errx(1, "malloc");

	for (len = 1; len <= TLS_PRF_OUT_LEN; len++) {
		if (EVP_PKEY_derive_init(pkey_ctx) <= 0)
			errx(1, "EVP_PKEY_derive_init");

		if (EVP_PKEY_CTX_set_tls1_prf_md(pkey_ctx, test->md()) <= 0)
			errx(1, "EVP_PKEY_CTX_set_tls1_prf_md");

		if (EVP_PKEY_CTX_set1_tls1_prf_secret(pkey_ctx, TLS_PRF_SECRET,
		    sizeof(TLS_PRF_SECRET)) <= 0)
			errx(1, "EVP_PKEY_CTX_set1_tls1_prf_secret");
		if (EVP_PKEY_CTX_add1_tls1_prf_seed(pkey_ctx, TLS_PRF_SEED1,
		    sizeof(TLS_PRF_SEED1)) <= 0)
			errx(1, "EVP_PKEY_CTX_add1_tls1_prf_seed 1");
		if (EVP_PKEY_CTX_add1_tls1_prf_seed(pkey_ctx, TLS_PRF_SEED2,
		    sizeof(TLS_PRF_SEED2)) <= 0)
			errx(1, "EVP_PKEY_CTX_add1_tls1_prf_seed 2");
		if (EVP_PKEY_CTX_add1_tls1_prf_seed(pkey_ctx, TLS_PRF_SEED3,
		    sizeof(TLS_PRF_SEED3)) <= 0)
			errx(1, "EVP_PKEY_CTX_add1_tls1_prf_seed 3");
		if (EVP_PKEY_CTX_add1_tls1_prf_seed(pkey_ctx, TLS_PRF_SEED4,
		    sizeof(TLS_PRF_SEED4)) <= 0)
			errx(1, "EVP_PKEY_CTX_add1_tls1_prf_seed 4");
		if (EVP_PKEY_CTX_add1_tls1_prf_seed(pkey_ctx, TLS_PRF_SEED5,
		    sizeof(TLS_PRF_SEED5)) <= 0)
			errx(1, "EVP_PKEY_CTX_add1_tls1_prf_seed 5");

		out_len = len;
		if (EVP_PKEY_derive(pkey_ctx, out, &out_len) <= 0)
			errx(1, "EVP_PKEY_derive");

		if (out_len != len) {
			fprintf(stderr, "FAIL: %s: length %zu != %zu\n",
			    __func__, out_len, len);
			goto err;
		}

		if (memcmp(test->out, out, out_len) != 0) {
			fprintf(stderr, "FAIL: tls_PRF output differs for "
			    "len %zu\n", len);
			fprintf(stderr, "output:\n");
			hexdump(out, out_len);
			fprintf(stderr, "test data:\n");
			hexdump(test->out, len);
			goto err;
		}
	}

	failed = 0;

 err:
	EVP_PKEY_CTX_free(pkey_ctx);
	free(out);

	return failed;
}

static int
evp_kdf_tls1_prf(void)
{
	size_t i;
	int failed = 0;

	for (i = 0; i < N_TLS_PRF_TESTS; i++)
		failed |= do_tls_prf_evp_test(i, &tls_prf_tests[i]);

	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	failed |= evp_asn1_method_test();
	failed |= evp_asn1_method_aliases_test();
	failed |= evp_pkey_iv_len_test();
	failed |= evp_do_all_test();
	failed |= evp_aliases_test();
	failed |= obj_name_do_all_test();
	failed |= evp_get_cipherbyname_test();
	failed |= evp_get_digestbyname_test();
	failed |= evp_kdf_hkdf_basic();
	failed |= evp_kdf_tls1_prf_basic();
	failed |= evp_kdf_tls1_prf();

	OPENSSL_cleanup();

	return failed;
}
