/*
 * validator/val_secalgo.c - validator security algorithm functions.
 *
 * Copyright (c) 2012, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains helper functions for the validator module.
 * These functions take raw data buffers, formatted for crypto verification,
 * and do the library calls (for the crypto library in use).
 */
#include "config.h"
/* packed_rrset on top to define enum types (forced by c99 standard) */
#include "util/data/packed_rrset.h"
#include "validator/val_secalgo.h"
#include "validator/val_nsec3.h"
#include "util/log.h"
#include "sldns/rrdef.h"
#include "sldns/keyraw.h"
#include "sldns/sbuffer.h"

#if !defined(HAVE_SSL) && !defined(HAVE_NSS) && !defined(HAVE_NETTLE)
#error "Need crypto library to do digital signature cryptography"
#endif

/** fake DSA support for unit tests */
int fake_dsa = 0;
/** fake SHA1 support for unit tests */
int fake_sha1 = 0;

/* OpenSSL implementation */
#ifdef HAVE_SSL
#ifdef HAVE_OPENSSL_ERR_H
#include <openssl/err.h>
#endif

#ifdef HAVE_OPENSSL_RAND_H
#include <openssl/rand.h>
#endif

#ifdef HAVE_OPENSSL_CONF_H
#include <openssl/conf.h>
#endif

#ifdef HAVE_OPENSSL_ENGINE_H
#include <openssl/engine.h>
#endif

#if defined(HAVE_OPENSSL_DSA_H) && defined(USE_DSA)
#include <openssl/dsa.h>
#endif

/**
 * Output a libcrypto openssl error to the logfile.
 * @param str: string to add to it.
 * @param e: the error to output, error number from ERR_get_error().
 */
static void
log_crypto_error(const char* str, unsigned long e)
{
	char buf[128];
	/* or use ERR_error_string if ERR_error_string_n is not avail TODO */
	ERR_error_string_n(e, buf, sizeof(buf));
	/* buf now contains */
	/* error:[error code]:[library name]:[function name]:[reason string] */
	log_err("%s crypto %s", str, buf);
}

/**
 * Output a libcrypto openssl error to the logfile as a debug message.
 * @param level: debug level to use in verbose() call
 * @param str: string to add to it.
 * @param e: the error to output, error number from ERR_get_error().
 */
static void
log_crypto_verbose(enum verbosity_value level, const char* str, unsigned long e)
{
	char buf[128];
	/* or use ERR_error_string if ERR_error_string_n is not avail TODO */
	ERR_error_string_n(e, buf, sizeof(buf));
	/* buf now contains */
	/* error:[error code]:[library name]:[function name]:[reason string] */
	verbose(level, "%s crypto %s", str, buf);
}

/* return size of digest if supported, or 0 otherwise */
size_t
nsec3_hash_algo_size_supported(int id)
{
	switch(id) {
	case NSEC3_HASH_SHA1:
		return SHA_DIGEST_LENGTH;
	default:
		return 0;
	}
}

/* perform nsec3 hash. return false on failure */
int
secalgo_nsec3_hash(int algo, unsigned char* buf, size_t len,
        unsigned char* res)
{
	switch(algo) {
	case NSEC3_HASH_SHA1:
#ifdef OPENSSL_FIPS
		if(!sldns_digest_evp(buf, len, res, EVP_sha1()))
			log_crypto_error("could not digest with EVP_sha1",
				ERR_get_error());
#else
		(void)SHA1(buf, len, res);
#endif
		return 1;
	default:
		return 0;
	}
}

void
secalgo_hash_sha256(unsigned char* buf, size_t len, unsigned char* res)
{
#ifdef OPENSSL_FIPS
	if(!sldns_digest_evp(buf, len, res, EVP_sha256()))
		log_crypto_error("could not digest with EVP_sha256",
			ERR_get_error());
#else
	(void)SHA256(buf, len, res);
#endif
}

/** hash structure for keeping track of running hashes */
struct secalgo_hash {
	/** the openssl message digest context */
	EVP_MD_CTX* ctx;
};

/** create secalgo hash with hash type */
static struct secalgo_hash* secalgo_hash_create_md(const EVP_MD* md)
{
	struct secalgo_hash* h;
	if(!md)
		return NULL;
	h = calloc(1, sizeof(*h));
	if(!h)
		return NULL;
	h->ctx = EVP_MD_CTX_create();
	if(!h->ctx) {
		free(h);
		return NULL;
	}
	if(!EVP_DigestInit_ex(h->ctx, md, NULL)) {
		EVP_MD_CTX_destroy(h->ctx);
		free(h);
		return NULL;
	}
	return h;
}

struct secalgo_hash* secalgo_hash_create_sha384(void)
{
	return secalgo_hash_create_md(EVP_sha384());
}

struct secalgo_hash* secalgo_hash_create_sha512(void)
{
	return secalgo_hash_create_md(EVP_sha512());
}

int secalgo_hash_update(struct secalgo_hash* hash, uint8_t* data, size_t len)
{
	return EVP_DigestUpdate(hash->ctx, (unsigned char*)data,
		(unsigned int)len);
}

int secalgo_hash_final(struct secalgo_hash* hash, uint8_t* result,
        size_t maxlen, size_t* resultlen)
{
	if(EVP_MD_CTX_size(hash->ctx) > (int)maxlen) {
		*resultlen = 0;
		log_err("secalgo_hash_final: hash buffer too small");
		return 0;
	}
	*resultlen = EVP_MD_CTX_size(hash->ctx);
	return EVP_DigestFinal_ex(hash->ctx, result, NULL);
}

void secalgo_hash_delete(struct secalgo_hash* hash)
{
	if(!hash) return;
	EVP_MD_CTX_destroy(hash->ctx);
	free(hash);
}

/**
 * Return size of DS digest according to its hash algorithm.
 * @param algo: DS digest algo.
 * @return size in bytes of digest, or 0 if not supported.
 */
size_t
ds_digest_size_supported(int algo)
{
	switch(algo) {
		case LDNS_SHA1:
#if defined(HAVE_EVP_SHA1) && defined(USE_SHA1)
#ifdef HAVE_EVP_DEFAULT_PROPERTIES_IS_FIPS_ENABLED
			if (EVP_default_properties_is_fips_enabled(NULL))
				return 0;
#endif
			return SHA_DIGEST_LENGTH;
#else
			if(fake_sha1) return 20;
			return 0;
#endif
#ifdef HAVE_EVP_SHA256
		case LDNS_SHA256:
			return SHA256_DIGEST_LENGTH;
#endif
#ifdef USE_GOST
		case LDNS_HASH_GOST:
			/* we support GOST if it can be loaded */
			(void)sldns_key_EVP_load_gost_id();
			if(EVP_get_digestbyname("md_gost94"))
				return 32;
			else	return 0;
#endif
#ifdef USE_ECDSA
		case LDNS_SHA384:
			return SHA384_DIGEST_LENGTH;
#endif
		default: break;
	}
	return 0;
}

#ifdef USE_GOST
/** Perform GOST hash */
static int
do_gost94(unsigned char* data, size_t len, unsigned char* dest)
{
	const EVP_MD* md = EVP_get_digestbyname("md_gost94");
	if(!md) 
		return 0;
	return sldns_digest_evp(data, (unsigned int)len, dest, md);
}
#endif

int
secalgo_ds_digest(int algo, unsigned char* buf, size_t len,
	unsigned char* res)
{
	switch(algo) {
#if defined(HAVE_EVP_SHA1) && defined(USE_SHA1)
		case LDNS_SHA1:
#ifdef OPENSSL_FIPS
			if(!sldns_digest_evp(buf, len, res, EVP_sha1()))
				log_crypto_error("could not digest with EVP_sha1",
					ERR_get_error());
#else
			(void)SHA1(buf, len, res);
#endif
			return 1;
#endif
#ifdef HAVE_EVP_SHA256
		case LDNS_SHA256:
#ifdef OPENSSL_FIPS
			if(!sldns_digest_evp(buf, len, res, EVP_sha256()))
				log_crypto_error("could not digest with EVP_sha256",
					ERR_get_error());
#else
			(void)SHA256(buf, len, res);
#endif
			return 1;
#endif
#ifdef USE_GOST
		case LDNS_HASH_GOST:
			if(do_gost94(buf, len, res))
				return 1;
			break;
#endif
#ifdef USE_ECDSA
		case LDNS_SHA384:
#ifdef OPENSSL_FIPS
			if(!sldns_digest_evp(buf, len, res, EVP_sha384()))
				log_crypto_error("could not digest with EVP_sha384",
					ERR_get_error());
#else
			(void)SHA384(buf, len, res);
#endif
			return 1;
#endif
		default: 
			verbose(VERB_QUERY, "unknown DS digest algorithm %d", 
				algo);
			break;
	}
	return 0;
}

/** return true if DNSKEY algorithm id is supported */
int
dnskey_algo_id_is_supported(int id)
{
	switch(id) {
	case LDNS_RSAMD5:
		/* RFC 6725 deprecates RSAMD5 */
		return 0;
	case LDNS_DSA:
	case LDNS_DSA_NSEC3:
#if defined(USE_DSA) && defined(USE_SHA1)
		return 1;
#else
		if(fake_dsa || fake_sha1) return 1;
		return 0;
#endif

	case LDNS_RSASHA1:
	case LDNS_RSASHA1_NSEC3:
#ifdef USE_SHA1
#ifdef HAVE_EVP_DEFAULT_PROPERTIES_IS_FIPS_ENABLED
		return !EVP_default_properties_is_fips_enabled(NULL);
#else
		return 1;
#endif
#else
		if(fake_sha1) return 1;
		return 0;
#endif

#if defined(HAVE_EVP_SHA256) && defined(USE_SHA2)
	case LDNS_RSASHA256:
#endif
#if defined(HAVE_EVP_SHA512) && defined(USE_SHA2)
	case LDNS_RSASHA512:
#endif
#ifdef USE_ECDSA
	case LDNS_ECDSAP256SHA256:
	case LDNS_ECDSAP384SHA384:
#endif
#if (defined(HAVE_EVP_SHA256) && defined(USE_SHA2)) || (defined(HAVE_EVP_SHA512) && defined(USE_SHA2)) || defined(USE_ECDSA)
		return 1;
#endif
#ifdef USE_ED25519
	case LDNS_ED25519:
#endif
#ifdef USE_ED448
	case LDNS_ED448:
#endif
#if defined(USE_ED25519) || defined(USE_ED448)
#ifdef HAVE_EVP_DEFAULT_PROPERTIES_IS_FIPS_ENABLED
		return !EVP_default_properties_is_fips_enabled(NULL);
#else
		return 1;
#endif
#endif

#ifdef USE_GOST
	case LDNS_ECC_GOST:
		/* we support GOST if it can be loaded */
		return sldns_key_EVP_load_gost_id();
#endif
	default:
		return 0;
	}
}

#ifdef USE_DSA
/**
 * Setup DSA key digest in DER encoding ... 
 * @param sig: input is signature output alloced ptr (unless failure).
 * 	caller must free alloced ptr if this routine returns true.
 * @param len: input is initial siglen, output is output len.
 * @return false on failure.
 */
static int
setup_dsa_sig(unsigned char** sig, unsigned int* len)
{
	unsigned char* orig = *sig;
	unsigned int origlen = *len;
	int newlen;
	BIGNUM *R, *S;
	DSA_SIG *dsasig;

	/* extract the R and S field from the sig buffer */
	if(origlen < 1 + 2*SHA_DIGEST_LENGTH)
		return 0;
	R = BN_new();
	if(!R) return 0;
	(void) BN_bin2bn(orig + 1, SHA_DIGEST_LENGTH, R);
	S = BN_new();
	if(!S) return 0;
	(void) BN_bin2bn(orig + 21, SHA_DIGEST_LENGTH, S);
	dsasig = DSA_SIG_new();
	if(!dsasig) return 0;

#ifdef HAVE_DSA_SIG_SET0
	if(!DSA_SIG_set0(dsasig, R, S)) {
		DSA_SIG_free(dsasig);
		return 0;
	}
#else
#  ifndef S_SPLINT_S
	dsasig->r = R;
	dsasig->s = S;
#  endif /* S_SPLINT_S */
#endif
	*sig = NULL;
	newlen = i2d_DSA_SIG(dsasig, sig);
	if(newlen < 0) {
		DSA_SIG_free(dsasig);
		free(*sig);
		return 0;
	}
	*len = (unsigned int)newlen;
	DSA_SIG_free(dsasig);
	return 1;
}
#endif /* USE_DSA */

#ifdef USE_ECDSA
/**
 * Setup the ECDSA signature in its encoding that the library wants.
 * Converts from plain numbers to ASN formatted.
 * @param sig: input is signature, output alloced ptr (unless failure).
 * 	caller must free alloced ptr if this routine returns true.
 * @param len: input is initial siglen, output is output len.
 * @return false on failure.
 */
static int
setup_ecdsa_sig(unsigned char** sig, unsigned int* len)
{
        /* convert from two BIGNUMs in the rdata buffer, to ASN notation.
	 * ASN preamble: 30440220 <R 32bytefor256> 0220 <S 32bytefor256>
	 * the '20' is the length of that field (=bnsize).
i	 * the '44' is the total remaining length.
	 * if negative, start with leading zero.
	 * if starts with 00s, remove them from the number.
	 */
        uint8_t pre[] = {0x30, 0x44, 0x02, 0x20};
        int pre_len = 4;
        uint8_t mid[] = {0x02, 0x20};
        int mid_len = 2;
        int raw_sig_len, r_high, s_high, r_rem=0, s_rem=0;
	int bnsize = (int)((*len)/2);
        unsigned char* d = *sig;
	uint8_t* p;
	/* if too short or not even length, fails */
	if(*len < 16 || bnsize*2 != (int)*len)
		return 0;

        /* strip leading zeroes from r (but not last one) */
        while(r_rem < bnsize-1 && d[r_rem] == 0)
                r_rem++;
        /* strip leading zeroes from s (but not last one) */
        while(s_rem < bnsize-1 && d[bnsize+s_rem] == 0)
                s_rem++;

        r_high = ((d[0+r_rem]&0x80)?1:0);
        s_high = ((d[bnsize+s_rem]&0x80)?1:0);
        raw_sig_len = pre_len + r_high + bnsize - r_rem + mid_len +
                s_high + bnsize - s_rem;
	*sig = (unsigned char*)malloc((size_t)raw_sig_len);
	if(!*sig)
		return 0;
	p = (uint8_t*)*sig;
	p[0] = pre[0];
	p[1] = (uint8_t)(raw_sig_len-2);
	p[2] = pre[2];
	p[3] = (uint8_t)(bnsize + r_high - r_rem);
	p += 4;
	if(r_high) {
		*p = 0;
		p += 1;
	}
	memmove(p, d+r_rem, (size_t)bnsize-r_rem);
	p += bnsize-r_rem;
	memmove(p, mid, (size_t)mid_len-1);
	p += mid_len-1;
	*p = (uint8_t)(bnsize + s_high - s_rem);
	p += 1;
        if(s_high) {
		*p = 0;
		p += 1;
	}
	memmove(p, d+bnsize+s_rem, (size_t)bnsize-s_rem);
	*len = (unsigned int)raw_sig_len;
	return 1;
}
#endif /* USE_ECDSA */

#ifdef USE_ECDSA_EVP_WORKAROUND
static EVP_MD ecdsa_evp_256_md;
static EVP_MD ecdsa_evp_384_md;
void ecdsa_evp_workaround_init(void)
{
	/* openssl before 1.0.0 fixes RSA with the SHA256
	 * hash in EVP.  We create one for ecdsa_sha256 */
	ecdsa_evp_256_md = *EVP_sha256();
	ecdsa_evp_256_md.required_pkey_type[0] = EVP_PKEY_EC;
	ecdsa_evp_256_md.verify = (void*)ECDSA_verify;

	ecdsa_evp_384_md = *EVP_sha384();
	ecdsa_evp_384_md.required_pkey_type[0] = EVP_PKEY_EC;
	ecdsa_evp_384_md.verify = (void*)ECDSA_verify;
}
#endif /* USE_ECDSA_EVP_WORKAROUND */

/**
 * Setup key and digest for verification. Adjust sig if necessary.
 *
 * @param algo: key algorithm
 * @param evp_key: EVP PKEY public key to create.
 * @param digest_type: digest type to use
 * @param key: key to setup for.
 * @param keylen: length of key.
 * @return false on failure.
 */
static int
setup_key_digest(int algo, EVP_PKEY** evp_key, const EVP_MD** digest_type, 
	unsigned char* key, size_t keylen)
{
	switch(algo) {
#if defined(USE_DSA) && defined(USE_SHA1)
		case LDNS_DSA:
		case LDNS_DSA_NSEC3:
			*evp_key = sldns_key_dsa2pkey_raw(key, keylen);
			if(!*evp_key) {
				verbose(VERB_QUERY, "verify: sldns_key_dsa2pkey failed");
				return 0;
			}
#ifdef HAVE_EVP_DSS1
			*digest_type = EVP_dss1();
#else
			*digest_type = EVP_sha1();
#endif

			break;
#endif /* USE_DSA && USE_SHA1 */

#if defined(USE_SHA1) || (defined(HAVE_EVP_SHA256) && defined(USE_SHA2)) || (defined(HAVE_EVP_SHA512) && defined(USE_SHA2))
#ifdef USE_SHA1
		case LDNS_RSASHA1:
		case LDNS_RSASHA1_NSEC3:
#endif
#if defined(HAVE_EVP_SHA256) && defined(USE_SHA2)
		case LDNS_RSASHA256:
#endif
#if defined(HAVE_EVP_SHA512) && defined(USE_SHA2)
		case LDNS_RSASHA512:
#endif
			*evp_key = sldns_key_rsa2pkey_raw(key, keylen);
			if(!*evp_key) {
				verbose(VERB_QUERY, "verify: sldns_key_rsa2pkey SHA failed");
				return 0;
			}

			/* select SHA version */
#if defined(HAVE_EVP_SHA256) && defined(USE_SHA2)
			if(algo == LDNS_RSASHA256)
				*digest_type = EVP_sha256();
			else
#endif
#if defined(HAVE_EVP_SHA512) && defined(USE_SHA2)
				if(algo == LDNS_RSASHA512)
				*digest_type = EVP_sha512();
			else
#endif
#ifdef USE_SHA1
				*digest_type = EVP_sha1();
#else
				{ verbose(VERB_QUERY, "no digest available"); return 0; }
#endif
			break;
#endif /* defined(USE_SHA1) || (defined(HAVE_EVP_SHA256) && defined(USE_SHA2)) || (defined(HAVE_EVP_SHA512) && defined(USE_SHA2)) */

		case LDNS_RSAMD5:
			*evp_key = sldns_key_rsa2pkey_raw(key, keylen);
			if(!*evp_key) {
				verbose(VERB_QUERY, "verify: sldns_key_rsa2pkey MD5 failed");
				return 0;
			}
			*digest_type = EVP_md5();

			break;
#ifdef USE_GOST
		case LDNS_ECC_GOST:
			*evp_key = sldns_gost2pkey_raw(key, keylen);
			if(!*evp_key) {
				verbose(VERB_QUERY, "verify: "
					"sldns_gost2pkey_raw failed");
				return 0;
			}
			*digest_type = EVP_get_digestbyname("md_gost94");
			if(!*digest_type) {
				verbose(VERB_QUERY, "verify: "
					"EVP_getdigest md_gost94 failed");
				return 0;
			}
			break;
#endif
#ifdef USE_ECDSA
		case LDNS_ECDSAP256SHA256:
			*evp_key = sldns_ecdsa2pkey_raw(key, keylen,
				LDNS_ECDSAP256SHA256);
			if(!*evp_key) {
				verbose(VERB_QUERY, "verify: "
					"sldns_ecdsa2pkey_raw failed");
				return 0;
			}
#ifdef USE_ECDSA_EVP_WORKAROUND
			*digest_type = &ecdsa_evp_256_md;
#else
			*digest_type = EVP_sha256();
#endif
			break;
		case LDNS_ECDSAP384SHA384:
			*evp_key = sldns_ecdsa2pkey_raw(key, keylen,
				LDNS_ECDSAP384SHA384);
			if(!*evp_key) {
				verbose(VERB_QUERY, "verify: "
					"sldns_ecdsa2pkey_raw failed");
				return 0;
			}
#ifdef USE_ECDSA_EVP_WORKAROUND
			*digest_type = &ecdsa_evp_384_md;
#else
			*digest_type = EVP_sha384();
#endif
			break;
#endif /* USE_ECDSA */
#ifdef USE_ED25519
		case LDNS_ED25519:
			*evp_key = sldns_ed255192pkey_raw(key, keylen);
			if(!*evp_key) {
				verbose(VERB_QUERY, "verify: "
					"sldns_ed255192pkey_raw failed");
				return 0;
			}
			*digest_type = NULL;
			break;
#endif /* USE_ED25519 */
#ifdef USE_ED448
		case LDNS_ED448:
			*evp_key = sldns_ed4482pkey_raw(key, keylen);
			if(!*evp_key) {
				verbose(VERB_QUERY, "verify: "
					"sldns_ed4482pkey_raw failed");
				return 0;
			}
			*digest_type = NULL;
			break;
#endif /* USE_ED448 */
		default:
			verbose(VERB_QUERY, "verify: unknown algorithm %d", 
				algo);
			return 0;
	}
	return 1;
}

static void
digest_ctx_free(EVP_MD_CTX* ctx, EVP_PKEY *evp_key,
	unsigned char* sigblock, int dofree, int docrypto_free)
{
#ifdef HAVE_EVP_MD_CTX_NEW
	EVP_MD_CTX_destroy(ctx);
#else
	EVP_MD_CTX_cleanup(ctx);
	free(ctx);
#endif
	EVP_PKEY_free(evp_key);
	if(dofree) free(sigblock);
	else if(docrypto_free) OPENSSL_free(sigblock);
}

static enum sec_status
digest_error_status(const char *str)
{
	unsigned long e = ERR_get_error();
#ifdef EVP_R_INVALID_DIGEST
	if (ERR_GET_LIB(e) == ERR_LIB_EVP &&
		ERR_GET_REASON(e) == EVP_R_INVALID_DIGEST) {
		log_crypto_verbose(VERB_ALGO, str, e);
		return sec_status_indeterminate;
	}
#endif
	log_crypto_verbose(VERB_QUERY, str, e);
	return sec_status_unchecked;
}

/**
 * Check a canonical sig+rrset and signature against a dnskey
 * @param buf: buffer with data to verify, the first rrsig part and the
 *	canonicalized rrset.
 * @param algo: DNSKEY algorithm.
 * @param sigblock: signature rdata field from RRSIG
 * @param sigblock_len: length of sigblock data.
 * @param key: public key data from DNSKEY RR.
 * @param keylen: length of keydata.
 * @param reason: bogus reason in more detail.
 * @return secure if verification succeeded, bogus on crypto failure,
 *	unchecked on format errors and alloc failures, indeterminate
 *	if digest is not supported by the crypto library (openssl3+ only).
 */
enum sec_status
verify_canonrrset(sldns_buffer* buf, int algo, unsigned char* sigblock,
	unsigned int sigblock_len, unsigned char* key, unsigned int keylen,
	char** reason)
{
	const EVP_MD *digest_type;
	EVP_MD_CTX* ctx;
	int res, dofree = 0, docrypto_free = 0;
	EVP_PKEY *evp_key = NULL;

#ifndef USE_DSA
	if((algo == LDNS_DSA || algo == LDNS_DSA_NSEC3) &&(fake_dsa||fake_sha1))
		return sec_status_secure;
#endif
#ifndef USE_SHA1
	if(fake_sha1 && (algo == LDNS_DSA || algo == LDNS_DSA_NSEC3 || algo == LDNS_RSASHA1 || algo == LDNS_RSASHA1_NSEC3))
		return sec_status_secure;
#endif
	
	if(!setup_key_digest(algo, &evp_key, &digest_type, key, keylen)) {
		verbose(VERB_QUERY, "verify: failed to setup key");
		*reason = "use of key for crypto failed";
		EVP_PKEY_free(evp_key);
		return sec_status_bogus;
	}
#ifdef USE_DSA
	/* if it is a DSA signature in bind format, convert to DER format */
	if((algo == LDNS_DSA || algo == LDNS_DSA_NSEC3) && 
		sigblock_len == 1+2*SHA_DIGEST_LENGTH) {
		if(!setup_dsa_sig(&sigblock, &sigblock_len)) {
			verbose(VERB_QUERY, "verify: failed to setup DSA sig");
			*reason = "use of key for DSA crypto failed";
			EVP_PKEY_free(evp_key);
			return sec_status_bogus;
		}
		docrypto_free = 1;
	}
#endif
#if defined(USE_ECDSA) && defined(USE_DSA)
	else 
#endif
#ifdef USE_ECDSA
	if(algo == LDNS_ECDSAP256SHA256 || algo == LDNS_ECDSAP384SHA384) {
		/* EVP uses ASN prefix on sig, which is not in the wire data */
		if(!setup_ecdsa_sig(&sigblock, &sigblock_len)) {
			verbose(VERB_QUERY, "verify: failed to setup ECDSA sig");
			*reason = "use of signature for ECDSA crypto failed";
			EVP_PKEY_free(evp_key);
			return sec_status_bogus;
		}
		dofree = 1;
	}
#endif /* USE_ECDSA */

	/* do the signature cryptography work */
#ifdef HAVE_EVP_MD_CTX_NEW
	ctx = EVP_MD_CTX_new();
#else
	ctx = (EVP_MD_CTX*)malloc(sizeof(*ctx));
	if(ctx) EVP_MD_CTX_init(ctx);
#endif
	if(!ctx) {
		log_err("EVP_MD_CTX_new: malloc failure");
		EVP_PKEY_free(evp_key);
		if(dofree) free(sigblock);
		else if(docrypto_free) OPENSSL_free(sigblock);
		return sec_status_unchecked;
	}
#ifndef HAVE_EVP_DIGESTVERIFY
	if(EVP_DigestInit(ctx, digest_type) == 0) {
		enum sec_status sec;
		sec = digest_error_status("verify: EVP_DigestInit failed");
		digest_ctx_free(ctx, evp_key, sigblock,
			dofree, docrypto_free);
		return sec;
	}
	if(EVP_DigestUpdate(ctx, (unsigned char*)sldns_buffer_begin(buf), 
		(unsigned int)sldns_buffer_limit(buf)) == 0) {
		log_crypto_verbose(VERB_QUERY, "verify: EVP_DigestUpdate failed",
			ERR_get_error());
		digest_ctx_free(ctx, evp_key, sigblock,
			dofree, docrypto_free);
		return sec_status_unchecked;
	}

	res = EVP_VerifyFinal(ctx, sigblock, sigblock_len, evp_key);
#else /* HAVE_EVP_DIGESTVERIFY */
	if(EVP_DigestVerifyInit(ctx, NULL, digest_type, NULL, evp_key) == 0) {
		enum sec_status sec;
		sec = digest_error_status("verify: EVP_DigestVerifyInit failed");
		digest_ctx_free(ctx, evp_key, sigblock,
			dofree, docrypto_free);
		return sec;
	}
	res = EVP_DigestVerify(ctx, sigblock, sigblock_len,
		(unsigned char*)sldns_buffer_begin(buf),
		sldns_buffer_limit(buf));
#endif
	digest_ctx_free(ctx, evp_key, sigblock,
		dofree, docrypto_free);

	if(res == 1) {
		return sec_status_secure;
	} else if(res == 0) {
		verbose(VERB_QUERY, "verify: signature mismatch");
		*reason = "signature crypto failed";
		return sec_status_bogus;
	}

	log_crypto_error("verify:", ERR_get_error());
	return sec_status_unchecked;
}

/**************************************************/
#elif defined(HAVE_NSS)
/* libnss implementation */
/* nss3 */
#include "sechash.h"
#include "pk11pub.h"
#include "keyhi.h"
#include "secerr.h"
#include "cryptohi.h"
/* nspr4 */
#include "prerror.h"

/* return size of digest if supported, or 0 otherwise */
size_t
nsec3_hash_algo_size_supported(int id)
{
	switch(id) {
	case NSEC3_HASH_SHA1:
		return SHA1_LENGTH;
	default:
		return 0;
	}
}

/* perform nsec3 hash. return false on failure */
int
secalgo_nsec3_hash(int algo, unsigned char* buf, size_t len,
        unsigned char* res)
{
	switch(algo) {
	case NSEC3_HASH_SHA1:
		(void)HASH_HashBuf(HASH_AlgSHA1, res, buf, (unsigned long)len);
		return 1;
	default:
		return 0;
	}
}

void
secalgo_hash_sha256(unsigned char* buf, size_t len, unsigned char* res)
{
	(void)HASH_HashBuf(HASH_AlgSHA256, res, buf, (unsigned long)len);
}

/** the secalgo hash structure */
struct secalgo_hash {
	/** hash context */
	HASHContext* ctx;
};

/** create hash struct of type */
static struct secalgo_hash* secalgo_hash_create_type(HASH_HashType tp)
{
	struct secalgo_hash* h = calloc(1, sizeof(*h));
	if(!h)
		return NULL;
	h->ctx = HASH_Create(tp);
	if(!h->ctx) {
		free(h);
		return NULL;
	}
	return h;
}

struct secalgo_hash* secalgo_hash_create_sha384(void)
{
	return secalgo_hash_create_type(HASH_AlgSHA384);
}

struct secalgo_hash* secalgo_hash_create_sha512(void)
{
	return secalgo_hash_create_type(HASH_AlgSHA512);
}

int secalgo_hash_update(struct secalgo_hash* hash, uint8_t* data, size_t len)
{
	HASH_Update(hash->ctx, (unsigned char*)data, (unsigned int)len);
	return 1;
}

int secalgo_hash_final(struct secalgo_hash* hash, uint8_t* result,
        size_t maxlen, size_t* resultlen)
{
	unsigned int reslen = 0;
	if(HASH_ResultLenContext(hash->ctx) > (unsigned int)maxlen) {
		*resultlen = 0;
		log_err("secalgo_hash_final: hash buffer too small");
		return 0;
	}
	HASH_End(hash->ctx, (unsigned char*)result, &reslen,
		(unsigned int)maxlen);
	*resultlen = (size_t)reslen;
	return 1;
}

void secalgo_hash_delete(struct secalgo_hash* hash)
{
	if(!hash) return;
	HASH_Destroy(hash->ctx);
	free(hash);
}

size_t
ds_digest_size_supported(int algo)
{
	/* uses libNSS */
	switch(algo) {
#ifdef USE_SHA1
		case LDNS_SHA1:
			return SHA1_LENGTH;
#endif
#ifdef USE_SHA2
		case LDNS_SHA256:
			return SHA256_LENGTH;
#endif
#ifdef USE_ECDSA
		case LDNS_SHA384:
			return SHA384_LENGTH;
#endif
		/* GOST not supported in NSS */
		case LDNS_HASH_GOST:
		default: break;
	}
	return 0;
}

int
secalgo_ds_digest(int algo, unsigned char* buf, size_t len,
	unsigned char* res)
{
	/* uses libNSS */
	switch(algo) {
#ifdef USE_SHA1
		case LDNS_SHA1:
			return HASH_HashBuf(HASH_AlgSHA1, res, buf, len)
				== SECSuccess;
#endif
#if defined(USE_SHA2)
		case LDNS_SHA256:
			return HASH_HashBuf(HASH_AlgSHA256, res, buf, len)
				== SECSuccess;
#endif
#ifdef USE_ECDSA
		case LDNS_SHA384:
			return HASH_HashBuf(HASH_AlgSHA384, res, buf, len)
				== SECSuccess;
#endif
		case LDNS_HASH_GOST:
		default: 
			verbose(VERB_QUERY, "unknown DS digest algorithm %d", 
				algo);
			break;
	}
	return 0;
}

int
dnskey_algo_id_is_supported(int id)
{
	/* uses libNSS */
	switch(id) {
	case LDNS_RSAMD5:
		/* RFC 6725 deprecates RSAMD5 */
		return 0;
#if defined(USE_SHA1) || defined(USE_SHA2)
#if defined(USE_DSA) && defined(USE_SHA1)
	case LDNS_DSA:
	case LDNS_DSA_NSEC3:
#endif
#ifdef USE_SHA1
	case LDNS_RSASHA1:
	case LDNS_RSASHA1_NSEC3:
#endif
#ifdef USE_SHA2
	case LDNS_RSASHA256:
#endif
#ifdef USE_SHA2
	case LDNS_RSASHA512:
#endif
		return 1;
#endif /* SHA1 or SHA2 */

#ifdef USE_ECDSA
	case LDNS_ECDSAP256SHA256:
	case LDNS_ECDSAP384SHA384:
		return PK11_TokenExists(CKM_ECDSA);
#endif
	case LDNS_ECC_GOST:
	default:
		return 0;
	}
}

/* return a new public key for NSS */
static SECKEYPublicKey* nss_key_create(KeyType ktype)
{
	SECKEYPublicKey* key;
	PLArenaPool* arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
	if(!arena) {
		log_err("out of memory, PORT_NewArena failed");
		return NULL;
	}
	key = PORT_ArenaZNew(arena, SECKEYPublicKey);
	if(!key) {
		log_err("out of memory, PORT_ArenaZNew failed");
		PORT_FreeArena(arena, PR_FALSE);
		return NULL;
	}
	key->arena = arena;
	key->keyType = ktype;
	key->pkcs11Slot = NULL;
	key->pkcs11ID = CK_INVALID_HANDLE;
	return key;
}

static SECKEYPublicKey* nss_buf2ecdsa(unsigned char* key, size_t len, int algo)
{
	SECKEYPublicKey* pk;
	SECItem pub = {siBuffer, NULL, 0};
	SECItem params = {siBuffer, NULL, 0};
	static unsigned char param256[] = {
		/* OBJECTIDENTIFIER 1.2.840.10045.3.1.7 (P-256)
		 * {iso(1) member-body(2) us(840) ansi-x962(10045) curves(3) prime(1) prime256v1(7)} */
		0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07
	};
	static unsigned char param384[] = {
		/* OBJECTIDENTIFIER 1.3.132.0.34 (P-384)
		 * {iso(1) identified-organization(3) certicom(132) curve(0) ansip384r1(34)} */
		0x06, 0x05, 0x2b, 0x81, 0x04, 0x00, 0x22
	};
	unsigned char buf[256+2]; /* sufficient for 2*384/8+1 */

	/* check length, which uncompressed must be 2 bignums */
	if(algo == LDNS_ECDSAP256SHA256) {
		if(len != 2*256/8) return NULL;
		/* ECCurve_X9_62_PRIME_256V1 */
	} else if(algo == LDNS_ECDSAP384SHA384) {
		if(len != 2*384/8) return NULL;
		/* ECCurve_X9_62_PRIME_384R1 */
	} else    return NULL;

	buf[0] = 0x04; /* POINT_FORM_UNCOMPRESSED */
	memmove(buf+1, key, len);
	pub.data = buf;
	pub.len = len+1;
	if(algo == LDNS_ECDSAP256SHA256) {
		params.data = param256;
		params.len = sizeof(param256);
	} else {
		params.data = param384;
		params.len = sizeof(param384);
	}

	pk = nss_key_create(ecKey);
	if(!pk)
		return NULL;
	pk->u.ec.size = (len/2)*8;
	if(SECITEM_CopyItem(pk->arena, &pk->u.ec.publicValue, &pub)) {
		SECKEY_DestroyPublicKey(pk);
		return NULL;
	}
	if(SECITEM_CopyItem(pk->arena, &pk->u.ec.DEREncodedParams, &params)) {
		SECKEY_DestroyPublicKey(pk);
		return NULL;
	}

	return pk;
}

#if defined(USE_DSA) && defined(USE_SHA1)
static SECKEYPublicKey* nss_buf2dsa(unsigned char* key, size_t len)
{
	SECKEYPublicKey* pk;
	uint8_t T;
	uint16_t length;
	uint16_t offset;
	SECItem Q = {siBuffer, NULL, 0};
	SECItem P = {siBuffer, NULL, 0};
	SECItem G = {siBuffer, NULL, 0};
	SECItem Y = {siBuffer, NULL, 0};

	if(len == 0)
		return NULL;
	T = (uint8_t)key[0];
	length = (64 + T * 8);
	offset = 1;

	if (T > 8) {
		return NULL;
	}
	if(len < (size_t)1 + SHA1_LENGTH + 3*length)
		return NULL;

	Q.data = key+offset;
	Q.len = SHA1_LENGTH;
	offset += SHA1_LENGTH;

	P.data = key+offset;
	P.len = length;
	offset += length;

	G.data = key+offset;
	G.len = length;
	offset += length;

	Y.data = key+offset;
	Y.len = length;
	offset += length;

	pk = nss_key_create(dsaKey);
	if(!pk)
		return NULL;
	if(SECITEM_CopyItem(pk->arena, &pk->u.dsa.params.prime, &P)) {
		SECKEY_DestroyPublicKey(pk);
		return NULL;
	}
	if(SECITEM_CopyItem(pk->arena, &pk->u.dsa.params.subPrime, &Q)) {
		SECKEY_DestroyPublicKey(pk);
		return NULL;
	}
	if(SECITEM_CopyItem(pk->arena, &pk->u.dsa.params.base, &G)) {
		SECKEY_DestroyPublicKey(pk);
		return NULL;
	}
	if(SECITEM_CopyItem(pk->arena, &pk->u.dsa.publicValue, &Y)) {
		SECKEY_DestroyPublicKey(pk);
		return NULL;
	}
	return pk;
}
#endif /* USE_DSA && USE_SHA1 */

static SECKEYPublicKey* nss_buf2rsa(unsigned char* key, size_t len)
{
	SECKEYPublicKey* pk;
	uint16_t exp;
	uint16_t offset;
	uint16_t int16;
	SECItem modulus = {siBuffer, NULL, 0};
	SECItem exponent = {siBuffer, NULL, 0};
	if(len == 0)
		return NULL;
	if(key[0] == 0) {
		if(len < 3)
			return NULL;
		/* the exponent is too large so it's places further */
		memmove(&int16, key+1, 2);
		exp = ntohs(int16);
		offset = 3;
	} else {
		exp = key[0];
		offset = 1;
	}

	/* key length at least one */
	if(len < (size_t)offset + exp + 1)
		return NULL;
	
	exponent.data = key+offset;
	exponent.len = exp;
	offset += exp;
	modulus.data = key+offset;
	modulus.len = (len - offset);

	pk = nss_key_create(rsaKey);
	if(!pk)
		return NULL;
	if(SECITEM_CopyItem(pk->arena, &pk->u.rsa.modulus, &modulus)) {
		SECKEY_DestroyPublicKey(pk);
		return NULL;
	}
	if(SECITEM_CopyItem(pk->arena, &pk->u.rsa.publicExponent, &exponent)) {
		SECKEY_DestroyPublicKey(pk);
		return NULL;
	}
	return pk;
}

/**
 * Setup key and digest for verification. Adjust sig if necessary.
 *
 * @param algo: key algorithm
 * @param evp_key: EVP PKEY public key to create.
 * @param digest_type: digest type to use
 * @param key: key to setup for.
 * @param keylen: length of key.
 * @param prefix: if returned, the ASN prefix for the hashblob.
 * @param prefixlen: length of the prefix.
 * @return false on failure.
 */
static int
nss_setup_key_digest(int algo, SECKEYPublicKey** pubkey, HASH_HashType* htype,
	unsigned char* key, size_t keylen, unsigned char** prefix,
	size_t* prefixlen)
{
	/* uses libNSS */

	/* hash prefix for md5, RFC2537 */
	static unsigned char p_md5[] = {0x30, 0x20, 0x30, 0x0c, 0x06, 0x08, 0x2a,
	0x86, 0x48, 0x86, 0xf7, 0x0d, 0x02, 0x05, 0x05, 0x00, 0x04, 0x10};
	/* hash prefix to prepend to hash output, from RFC3110 */
	static unsigned char p_sha1[] = {0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2B,
		0x0E, 0x03, 0x02, 0x1A, 0x05, 0x00, 0x04, 0x14};
	/* from RFC5702 */
	static unsigned char p_sha256[] = {0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60,
	0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20};
	static unsigned char p_sha512[] = {0x30, 0x51, 0x30, 0x0d, 0x06, 0x09, 0x60,
	0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03, 0x05, 0x00, 0x04, 0x40};
	/* from RFC6234 */
	/* for future RSASHA384 .. 
	static unsigned char p_sha384[] = {0x30, 0x51, 0x30, 0x0d, 0x06, 0x09, 0x60,
	0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02, 0x05, 0x00, 0x04, 0x30};
	*/

	switch(algo) {

#if defined(USE_SHA1) || defined(USE_SHA2)
#if defined(USE_DSA) && defined(USE_SHA1)
		case LDNS_DSA:
		case LDNS_DSA_NSEC3:
			*pubkey = nss_buf2dsa(key, keylen);
			if(!*pubkey) {
				log_err("verify: malloc failure in crypto");
				return 0;
			}
			*htype = HASH_AlgSHA1;
			/* no prefix for DSA verification */
			break;
#endif
#ifdef USE_SHA1
		case LDNS_RSASHA1:
		case LDNS_RSASHA1_NSEC3:
#endif
#ifdef USE_SHA2
		case LDNS_RSASHA256:
#endif
#ifdef USE_SHA2
		case LDNS_RSASHA512:
#endif
			*pubkey = nss_buf2rsa(key, keylen);
			if(!*pubkey) {
				log_err("verify: malloc failure in crypto");
				return 0;
			}
			/* select SHA version */
#ifdef USE_SHA2
			if(algo == LDNS_RSASHA256) {
				*htype = HASH_AlgSHA256;
				*prefix = p_sha256;
				*prefixlen = sizeof(p_sha256);
			} else
#endif
#ifdef USE_SHA2
				if(algo == LDNS_RSASHA512) {
				*htype = HASH_AlgSHA512;
				*prefix = p_sha512;
				*prefixlen = sizeof(p_sha512);
			} else
#endif
#ifdef USE_SHA1
			{
				*htype = HASH_AlgSHA1;
				*prefix = p_sha1;
				*prefixlen = sizeof(p_sha1);
			}
#else
			{
				verbose(VERB_QUERY, "verify: no digest algo");
				return 0;
			}
#endif

			break;
#endif /* SHA1 or SHA2 */

		case LDNS_RSAMD5:
			*pubkey = nss_buf2rsa(key, keylen);
			if(!*pubkey) {
				log_err("verify: malloc failure in crypto");
				return 0;
			}
			*htype = HASH_AlgMD5;
			*prefix = p_md5;
			*prefixlen = sizeof(p_md5);

			break;
#ifdef USE_ECDSA
		case LDNS_ECDSAP256SHA256:
			*pubkey = nss_buf2ecdsa(key, keylen,
				LDNS_ECDSAP256SHA256);
			if(!*pubkey) {
				log_err("verify: malloc failure in crypto");
				return 0;
			}
			*htype = HASH_AlgSHA256;
			/* no prefix for DSA verification */
			break;
		case LDNS_ECDSAP384SHA384:
			*pubkey = nss_buf2ecdsa(key, keylen,
				LDNS_ECDSAP384SHA384);
			if(!*pubkey) {
				log_err("verify: malloc failure in crypto");
				return 0;
			}
			*htype = HASH_AlgSHA384;
			/* no prefix for DSA verification */
			break;
#endif /* USE_ECDSA */
		case LDNS_ECC_GOST:
		default:
			verbose(VERB_QUERY, "verify: unknown algorithm %d", 
				algo);
			return 0;
	}
	return 1;
}

/**
 * Check a canonical sig+rrset and signature against a dnskey
 * @param buf: buffer with data to verify, the first rrsig part and the
 *	canonicalized rrset.
 * @param algo: DNSKEY algorithm.
 * @param sigblock: signature rdata field from RRSIG
 * @param sigblock_len: length of sigblock data.
 * @param key: public key data from DNSKEY RR.
 * @param keylen: length of keydata.
 * @param reason: bogus reason in more detail.
 * @return secure if verification succeeded, bogus on crypto failure,
 *	unchecked on format errors and alloc failures.
 */
enum sec_status
verify_canonrrset(sldns_buffer* buf, int algo, unsigned char* sigblock, 
	unsigned int sigblock_len, unsigned char* key, unsigned int keylen,
	char** reason)
{
	/* uses libNSS */
	/* large enough for the different hashes */
	unsigned char hash[HASH_LENGTH_MAX];
	unsigned char hash2[HASH_LENGTH_MAX*2];
	HASH_HashType htype = 0;
	SECKEYPublicKey* pubkey = NULL;
	SECItem secsig = {siBuffer, sigblock, sigblock_len};
	SECItem sechash = {siBuffer, hash, 0};
	SECStatus res;
	unsigned char* prefix = NULL; /* prefix for hash, RFC3110, RFC5702 */
	size_t prefixlen = 0;
	int err;

	if(!nss_setup_key_digest(algo, &pubkey, &htype, key, keylen,
		&prefix, &prefixlen)) {
		verbose(VERB_QUERY, "verify: failed to setup key");
		*reason = "use of key for crypto failed";
		SECKEY_DestroyPublicKey(pubkey);
		return sec_status_bogus;
	}

#if defined(USE_DSA) && defined(USE_SHA1)
	/* need to convert DSA, ECDSA signatures? */
	if((algo == LDNS_DSA || algo == LDNS_DSA_NSEC3)) {
		if(sigblock_len == 1+2*SHA1_LENGTH) {
			secsig.data ++;
			secsig.len --;
		} else {
			SECItem* p = DSAU_DecodeDerSig(&secsig);
			if(!p) {
				verbose(VERB_QUERY, "verify: failed DER decode");
				*reason = "signature DER decode failed";
				SECKEY_DestroyPublicKey(pubkey);
				return sec_status_bogus;
			}
			if(SECITEM_CopyItem(pubkey->arena, &secsig, p)) {
				log_err("alloc failure in DER decode");
				SECKEY_DestroyPublicKey(pubkey);
				SECITEM_FreeItem(p, PR_TRUE);
				return sec_status_unchecked;
			}
			SECITEM_FreeItem(p, PR_TRUE);
		}
	}
#endif /* USE_DSA */

	/* do the signature cryptography work */
	/* hash the data */
	sechash.len = HASH_ResultLen(htype);
	if(sechash.len > sizeof(hash)) {
		verbose(VERB_QUERY, "verify: hash too large for buffer");
		SECKEY_DestroyPublicKey(pubkey);
		return sec_status_unchecked;
	}
	if(HASH_HashBuf(htype, hash, (unsigned char*)sldns_buffer_begin(buf),
		(unsigned int)sldns_buffer_limit(buf)) != SECSuccess) {
		verbose(VERB_QUERY, "verify: HASH_HashBuf failed");
		SECKEY_DestroyPublicKey(pubkey);
		return sec_status_unchecked;
	}
	if(prefix) {
		int hashlen = sechash.len;
		if(prefixlen+hashlen > sizeof(hash2)) {
			verbose(VERB_QUERY, "verify: hashprefix too large");
			SECKEY_DestroyPublicKey(pubkey);
			return sec_status_unchecked;
		}
		sechash.data = hash2;
		sechash.len = prefixlen+hashlen;
		memcpy(sechash.data, prefix, prefixlen);
		memmove(sechash.data+prefixlen, hash, hashlen);
	}

	/* verify the signature */
	res = PK11_Verify(pubkey, &secsig, &sechash, NULL /*wincx*/);
	SECKEY_DestroyPublicKey(pubkey);

	if(res == SECSuccess) {
		return sec_status_secure;
	}
	err = PORT_GetError();
	if(err != SEC_ERROR_BAD_SIGNATURE) {
		/* failed to verify */
		verbose(VERB_QUERY, "verify: PK11_Verify failed: %s",
			PORT_ErrorToString(err));
		/* if it is not supported, like ECC is removed, we get,
		 * SEC_ERROR_NO_MODULE */
		if(err == SEC_ERROR_NO_MODULE)
			return sec_status_unchecked;
		/* but other errors are commonly returned
		 * for a bad signature from NSS.  Thus we return bogus,
		 * not unchecked */
		*reason = "signature crypto failed";
		return sec_status_bogus;
	}
	verbose(VERB_QUERY, "verify: signature mismatch: %s",
		PORT_ErrorToString(err));
	*reason = "signature crypto failed";
	return sec_status_bogus;
}

#elif defined(HAVE_NETTLE)

#include "sha.h"
#include "bignum.h"
#include "macros.h"
#include "rsa.h"
#include "dsa.h"
#ifdef HAVE_NETTLE_DSA_COMPAT_H
#include "dsa-compat.h"
#endif
#include "asn1.h"
#ifdef USE_ECDSA
#include "ecdsa.h"
#include "ecc-curve.h"
#endif
#ifdef HAVE_NETTLE_EDDSA_H
#include "eddsa.h"
#endif

static int
_digest_nettle(int algo, uint8_t* buf, size_t len,
	unsigned char* res)
{
	switch(algo) {
		case SHA1_DIGEST_SIZE:
		{
			struct sha1_ctx ctx;
			sha1_init(&ctx);
			sha1_update(&ctx, len, buf);
			sha1_digest(&ctx, SHA1_DIGEST_SIZE, res);
			return 1;
		}
		case SHA256_DIGEST_SIZE:
		{
			struct sha256_ctx ctx;
			sha256_init(&ctx);
			sha256_update(&ctx, len, buf);
			sha256_digest(&ctx, SHA256_DIGEST_SIZE, res);
			return 1;
		}
		case SHA384_DIGEST_SIZE:
		{
			struct sha384_ctx ctx;
			sha384_init(&ctx);
			sha384_update(&ctx, len, buf);
			sha384_digest(&ctx, SHA384_DIGEST_SIZE, res);
			return 1;
		}
		case SHA512_DIGEST_SIZE:
		{
			struct sha512_ctx ctx;
			sha512_init(&ctx);
			sha512_update(&ctx, len, buf);
			sha512_digest(&ctx, SHA512_DIGEST_SIZE, res);
			return 1;
		}
		default:
			break;
	}
	return 0;
}

/* return size of digest if supported, or 0 otherwise */
size_t
nsec3_hash_algo_size_supported(int id)
{
	switch(id) {
	case NSEC3_HASH_SHA1:
		return SHA1_DIGEST_SIZE;
	default:
		return 0;
	}
}

/* perform nsec3 hash. return false on failure */
int
secalgo_nsec3_hash(int algo, unsigned char* buf, size_t len,
        unsigned char* res)
{
	switch(algo) {
	case NSEC3_HASH_SHA1:
		return _digest_nettle(SHA1_DIGEST_SIZE, (uint8_t*)buf, len,
			res);
	default:
		return 0;
	}
}

void
secalgo_hash_sha256(unsigned char* buf, size_t len, unsigned char* res)
{
	_digest_nettle(SHA256_DIGEST_SIZE, (uint8_t*)buf, len, res);
}

/** secalgo hash structure */
struct secalgo_hash {
	/** if it is 384 or 512 */
	int active;
	/** context for sha384 */
	struct sha384_ctx ctx384;
	/** context for sha512 */
	struct sha512_ctx ctx512;
};

struct secalgo_hash* secalgo_hash_create_sha384(void)
{
	struct secalgo_hash* h = calloc(1, sizeof(*h));
	if(!h)
		return NULL;
	h->active = 384;
	sha384_init(&h->ctx384);
	return h;
}

struct secalgo_hash* secalgo_hash_create_sha512(void)
{
	struct secalgo_hash* h = calloc(1, sizeof(*h));
	if(!h)
		return NULL;
	h->active = 512;
	sha512_init(&h->ctx512);
	return h;
}

int secalgo_hash_update(struct secalgo_hash* hash, uint8_t* data, size_t len)
{
	if(hash->active == 384) {
		sha384_update(&hash->ctx384, len, data);
	} else if(hash->active == 512) {
		sha512_update(&hash->ctx512, len, data);
	} else {
		return 0;
	}
	return 1;
}

int secalgo_hash_final(struct secalgo_hash* hash, uint8_t* result,
        size_t maxlen, size_t* resultlen)
{
	if(hash->active == 384) {
		if(SHA384_DIGEST_SIZE > maxlen) {
			*resultlen = 0;
			log_err("secalgo_hash_final: hash buffer too small");
			return 0;
		}
		*resultlen = SHA384_DIGEST_SIZE;
		sha384_digest(&hash->ctx384, SHA384_DIGEST_SIZE,
			(unsigned char*)result);
	} else if(hash->active == 512) {
		if(SHA512_DIGEST_SIZE > maxlen) {
			*resultlen = 0;
			log_err("secalgo_hash_final: hash buffer too small");
			return 0;
		}
		*resultlen = SHA512_DIGEST_SIZE;
		sha512_digest(&hash->ctx512, SHA512_DIGEST_SIZE,
			(unsigned char*)result);
	} else {
		*resultlen = 0;
		return 0;
	}
	return 1;
}

void secalgo_hash_delete(struct secalgo_hash* hash)
{
	if(!hash) return;
	free(hash);
}

/**
 * Return size of DS digest according to its hash algorithm.
 * @param algo: DS digest algo.
 * @return size in bytes of digest, or 0 if not supported.
 */
size_t
ds_digest_size_supported(int algo)
{
	switch(algo) {
		case LDNS_SHA1:
#ifdef USE_SHA1
			return SHA1_DIGEST_SIZE;
#else
			if(fake_sha1) return 20;
			return 0;
#endif
#ifdef USE_SHA2
		case LDNS_SHA256:
			return SHA256_DIGEST_SIZE;
#endif
#ifdef USE_ECDSA
		case LDNS_SHA384:
			return SHA384_DIGEST_SIZE;
#endif
		/* GOST not supported */
		case LDNS_HASH_GOST:
		default:
			break;
	}
	return 0;
}

int
secalgo_ds_digest(int algo, unsigned char* buf, size_t len,
	unsigned char* res)
{
	switch(algo) {
#ifdef USE_SHA1
		case LDNS_SHA1:
			return _digest_nettle(SHA1_DIGEST_SIZE, buf, len, res);
#endif
#if defined(USE_SHA2)
		case LDNS_SHA256:
			return _digest_nettle(SHA256_DIGEST_SIZE, buf, len, res);
#endif
#ifdef USE_ECDSA
		case LDNS_SHA384:
			return _digest_nettle(SHA384_DIGEST_SIZE, buf, len, res);

#endif
		case LDNS_HASH_GOST:
		default:
			verbose(VERB_QUERY, "unknown DS digest algorithm %d",
				algo);
			break;
	}
	return 0;
}

int
dnskey_algo_id_is_supported(int id)
{
	/* uses libnettle */
	switch(id) {
	case LDNS_DSA:
	case LDNS_DSA_NSEC3:
#if defined(USE_DSA) && defined(USE_SHA1)
		return 1;
#else
		if(fake_dsa || fake_sha1) return 1;
		return 0;
#endif
	case LDNS_RSASHA1:
	case LDNS_RSASHA1_NSEC3:
#ifdef USE_SHA1
		return 1;
#else
		if(fake_sha1) return 1;
		return 0;
#endif
#ifdef USE_SHA2
	case LDNS_RSASHA256:
	case LDNS_RSASHA512:
#endif
#ifdef USE_ECDSA
	case LDNS_ECDSAP256SHA256:
	case LDNS_ECDSAP384SHA384:
#endif
		return 1;
#ifdef USE_ED25519
	case LDNS_ED25519:
		return 1;
#endif
	case LDNS_RSAMD5: /* RFC 6725 deprecates RSAMD5 */
	case LDNS_ECC_GOST:
	default:
		return 0;
	}
}

#if defined(USE_DSA) && defined(USE_SHA1)
static char *
_verify_nettle_dsa(sldns_buffer* buf, unsigned char* sigblock,
	unsigned int sigblock_len, unsigned char* key, unsigned int keylen)
{
	uint8_t digest[SHA1_DIGEST_SIZE];
	uint8_t key_t_value;
	int res = 0;
	size_t offset;
	struct dsa_public_key pubkey;
	struct dsa_signature signature;
	unsigned int expected_len;

	/* Extract DSA signature from the record */
	nettle_dsa_signature_init(&signature);
	/* Signature length: 41 bytes - RFC 2536 sec. 3 */
	if(sigblock_len == 41) {
		if(key[0] != sigblock[0])
			return "invalid T value in DSA signature or pubkey";
		nettle_mpz_set_str_256_u(signature.r, 20, sigblock+1);
		nettle_mpz_set_str_256_u(signature.s, 20, sigblock+1+20);
	} else {
		/* DER encoded, decode the ASN1 notated R and S bignums */
		/* SEQUENCE { r INTEGER, s INTEGER } */
		struct asn1_der_iterator i, seq;
		if(asn1_der_iterator_first(&i, sigblock_len,
			(uint8_t*)sigblock) != ASN1_ITERATOR_CONSTRUCTED
			|| i.type != ASN1_SEQUENCE)
			return "malformed DER encoded DSA signature";
		/* decode this element of i using the seq iterator */
		if(asn1_der_decode_constructed(&i, &seq) !=
			ASN1_ITERATOR_PRIMITIVE || seq.type != ASN1_INTEGER)
			return "malformed DER encoded DSA signature";
		if(!asn1_der_get_bignum(&seq, signature.r, 20*8))
			return "malformed DER encoded DSA signature";
		if(asn1_der_iterator_next(&seq) != ASN1_ITERATOR_PRIMITIVE
			|| seq.type != ASN1_INTEGER)
			return "malformed DER encoded DSA signature";
		if(!asn1_der_get_bignum(&seq, signature.s, 20*8))
			return "malformed DER encoded DSA signature";
		if(asn1_der_iterator_next(&i) != ASN1_ITERATOR_END)
			return "malformed DER encoded DSA signature";
	}

	/* Validate T values constraints - RFC 2536 sec. 2 & sec. 3 */
	key_t_value = key[0];
	if (key_t_value > 8) {
		return "invalid T value in DSA pubkey";
	}

	/* Pubkey minimum length: 21 bytes - RFC 2536 sec. 2 */
	if (keylen < 21) {
		return "DSA pubkey too short";
	}

	expected_len =   1 +		/* T */
		        20 +		/* Q */
		       (64 + key_t_value*8) +	/* P */
		       (64 + key_t_value*8) +	/* G */
		       (64 + key_t_value*8);	/* Y */
	if (keylen != expected_len ) {
		return "invalid DSA pubkey length";
	}

	/* Extract DSA pubkey from the record */
	nettle_dsa_public_key_init(&pubkey);
	offset = 1;
	nettle_mpz_set_str_256_u(pubkey.q, 20, key+offset);
	offset += 20;
	nettle_mpz_set_str_256_u(pubkey.p, (64 + key_t_value*8), key+offset);
	offset += (64 + key_t_value*8);
	nettle_mpz_set_str_256_u(pubkey.g, (64 + key_t_value*8), key+offset);
	offset += (64 + key_t_value*8);
	nettle_mpz_set_str_256_u(pubkey.y, (64 + key_t_value*8), key+offset);

	/* Digest content of "buf" and verify its DSA signature in "sigblock"*/
	res = _digest_nettle(SHA1_DIGEST_SIZE, (unsigned char*)sldns_buffer_begin(buf),
						(unsigned int)sldns_buffer_limit(buf), (unsigned char*)digest);
	res &= dsa_sha1_verify_digest(&pubkey, digest, &signature);

	/* Clear and return */
	nettle_dsa_signature_clear(&signature);
	nettle_dsa_public_key_clear(&pubkey);
	if (!res)
		return "DSA signature verification failed";
	else
		return NULL;
}
#endif /* USE_DSA */

static char *
_verify_nettle_rsa(sldns_buffer* buf, unsigned int digest_size, char* sigblock,
	unsigned int sigblock_len, uint8_t* key, unsigned int keylen)
{
	uint16_t exp_len = 0;
	size_t exp_offset = 0, mod_offset = 0;
	struct rsa_public_key pubkey;
	mpz_t signature;
	int res = 0;

	/* RSA pubkey parsing as per RFC 3110 sec. 2 */
	if( keylen <= 1) {
		return "null RSA key";
	}
	if (key[0] != 0) {
		/* 1-byte length */
		exp_len = key[0];
		exp_offset = 1;
	} else {
		/* 1-byte NUL + 2-bytes exponent length */
		if (keylen < 3) {
			return "incorrect RSA key length";
		}
		exp_len = READ_UINT16(key+1);
		if (exp_len == 0)
			return "null RSA exponent length";
		exp_offset = 3;
	}
	/* Check that we are not over-running input length */
	if (keylen < exp_offset + exp_len + 1) {
		return "RSA key content shorter than expected";
	}
	mod_offset = exp_offset + exp_len;
	nettle_rsa_public_key_init(&pubkey);
	pubkey.size = keylen - mod_offset;
	nettle_mpz_set_str_256_u(pubkey.e, exp_len, &key[exp_offset]);
	nettle_mpz_set_str_256_u(pubkey.n, pubkey.size, &key[mod_offset]);

	/* Digest content of "buf" and verify its RSA signature in "sigblock"*/
	nettle_mpz_init_set_str_256_u(signature, sigblock_len, (uint8_t*)sigblock);
	switch (digest_size) {
		case SHA1_DIGEST_SIZE:
		{
			uint8_t digest[SHA1_DIGEST_SIZE];
			res = _digest_nettle(SHA1_DIGEST_SIZE, (unsigned char*)sldns_buffer_begin(buf),
						(unsigned int)sldns_buffer_limit(buf), (unsigned char*)digest);
			res &= rsa_sha1_verify_digest(&pubkey, digest, signature);
			break;
		}
		case SHA256_DIGEST_SIZE:
		{
			uint8_t digest[SHA256_DIGEST_SIZE];
			res = _digest_nettle(SHA256_DIGEST_SIZE, (unsigned char*)sldns_buffer_begin(buf),
						(unsigned int)sldns_buffer_limit(buf), (unsigned char*)digest);
			res &= rsa_sha256_verify_digest(&pubkey, digest, signature);
			break;
		}
		case SHA512_DIGEST_SIZE:
		{
			uint8_t digest[SHA512_DIGEST_SIZE];
			res = _digest_nettle(SHA512_DIGEST_SIZE, (unsigned char*)sldns_buffer_begin(buf),
						(unsigned int)sldns_buffer_limit(buf), (unsigned char*)digest);
			res &= rsa_sha512_verify_digest(&pubkey, digest, signature);
			break;
		}
		default:
			break;
	}

	/* Clear and return */
	nettle_rsa_public_key_clear(&pubkey);
	mpz_clear(signature);
	if (!res) {
		return "RSA signature verification failed";
	} else {
		return NULL;
	}
}

#ifdef USE_ECDSA
static char *
_verify_nettle_ecdsa(sldns_buffer* buf, unsigned int digest_size, unsigned char* sigblock,
	unsigned int sigblock_len, unsigned char* key, unsigned int keylen)
{
	int res = 0;
	struct ecc_point pubkey;
	struct dsa_signature signature;

	/* Always matched strength, as per RFC 6605 sec. 1 */
	if (sigblock_len != 2*digest_size || keylen != 2*digest_size) {
		return "wrong ECDSA signature length";
	}

	/* Parse ECDSA signature as per RFC 6605 sec. 4 */
	nettle_dsa_signature_init(&signature);
	switch (digest_size) {
		case SHA256_DIGEST_SIZE:
		{
			uint8_t digest[SHA256_DIGEST_SIZE];
			mpz_t x, y;
			nettle_ecc_point_init(&pubkey, nettle_get_secp_256r1());
			nettle_mpz_init_set_str_256_u(x, SHA256_DIGEST_SIZE, key);
			nettle_mpz_init_set_str_256_u(y, SHA256_DIGEST_SIZE, key+SHA256_DIGEST_SIZE);
			nettle_mpz_set_str_256_u(signature.r, SHA256_DIGEST_SIZE, sigblock);
			nettle_mpz_set_str_256_u(signature.s, SHA256_DIGEST_SIZE, sigblock+SHA256_DIGEST_SIZE);
			res = _digest_nettle(SHA256_DIGEST_SIZE, (unsigned char*)sldns_buffer_begin(buf),
						(unsigned int)sldns_buffer_limit(buf), (unsigned char*)digest);
			res &= nettle_ecc_point_set(&pubkey, x, y);
			res &= nettle_ecdsa_verify (&pubkey, SHA256_DIGEST_SIZE, digest, &signature);
			mpz_clear(x);
			mpz_clear(y);
			nettle_ecc_point_clear(&pubkey);
			break;
		}
		case SHA384_DIGEST_SIZE:
		{
			uint8_t digest[SHA384_DIGEST_SIZE];
			mpz_t x, y;
			nettle_ecc_point_init(&pubkey, nettle_get_secp_384r1());
			nettle_mpz_init_set_str_256_u(x, SHA384_DIGEST_SIZE, key);
			nettle_mpz_init_set_str_256_u(y, SHA384_DIGEST_SIZE, key+SHA384_DIGEST_SIZE);
			nettle_mpz_set_str_256_u(signature.r, SHA384_DIGEST_SIZE, sigblock);
			nettle_mpz_set_str_256_u(signature.s, SHA384_DIGEST_SIZE, sigblock+SHA384_DIGEST_SIZE);
			res = _digest_nettle(SHA384_DIGEST_SIZE, (unsigned char*)sldns_buffer_begin(buf),
						(unsigned int)sldns_buffer_limit(buf), (unsigned char*)digest);
			res &= nettle_ecc_point_set(&pubkey, x, y);
			res &= nettle_ecdsa_verify (&pubkey, SHA384_DIGEST_SIZE, digest, &signature);
			mpz_clear(x);
			mpz_clear(y);
			nettle_ecc_point_clear(&pubkey);
			break;
		}
		default:
			return "unknown ECDSA algorithm";
	}

	/* Clear and return */
	nettle_dsa_signature_clear(&signature);
	if (!res)
		return "ECDSA signature verification failed";
	else
		return NULL;
}
#endif

#ifdef USE_ED25519
static char *
_verify_nettle_ed25519(sldns_buffer* buf, unsigned char* sigblock,
	unsigned int sigblock_len, unsigned char* key, unsigned int keylen)
{
	int res = 0;

	if(sigblock_len != ED25519_SIGNATURE_SIZE) {
		return "wrong ED25519 signature length";
	}
	if(keylen != ED25519_KEY_SIZE) {
		return "wrong ED25519 key length";
	}

	res = ed25519_sha512_verify((uint8_t*)key, sldns_buffer_limit(buf),
		sldns_buffer_begin(buf), (uint8_t*)sigblock);

	if (!res)
		return "ED25519 signature verification failed";
	else
		return NULL;
}
#endif

/**
 * Check a canonical sig+rrset and signature against a dnskey
 * @param buf: buffer with data to verify, the first rrsig part and the
 *	canonicalized rrset.
 * @param algo: DNSKEY algorithm.
 * @param sigblock: signature rdata field from RRSIG
 * @param sigblock_len: length of sigblock data.
 * @param key: public key data from DNSKEY RR.
 * @param keylen: length of keydata.
 * @param reason: bogus reason in more detail.
 * @return secure if verification succeeded, bogus on crypto failure,
 *	unchecked on format errors and alloc failures.
 */
enum sec_status
verify_canonrrset(sldns_buffer* buf, int algo, unsigned char* sigblock,
	unsigned int sigblock_len, unsigned char* key, unsigned int keylen,
	char** reason)
{
	unsigned int digest_size = 0;

	if (sigblock_len == 0 || keylen == 0) {
		*reason = "null signature";
		return sec_status_bogus;
	}

#ifndef USE_DSA
	if((algo == LDNS_DSA || algo == LDNS_DSA_NSEC3) &&(fake_dsa||fake_sha1))
		return sec_status_secure;
#endif
#ifndef USE_SHA1
	if(fake_sha1 && (algo == LDNS_DSA || algo == LDNS_DSA_NSEC3 || algo == LDNS_RSASHA1 || algo == LDNS_RSASHA1_NSEC3))
		return sec_status_secure;
#endif

	switch(algo) {
#if defined(USE_DSA) && defined(USE_SHA1)
	case LDNS_DSA:
	case LDNS_DSA_NSEC3:
		*reason = _verify_nettle_dsa(buf, sigblock, sigblock_len, key, keylen);
		if (*reason != NULL)
			return sec_status_bogus;
		else
			return sec_status_secure;
#endif /* USE_DSA */

#ifdef USE_SHA1
	case LDNS_RSASHA1:
	case LDNS_RSASHA1_NSEC3:
		digest_size = (digest_size ? digest_size : SHA1_DIGEST_SIZE);
#endif
		/* double fallthrough annotation to please gcc parser */
		ATTR_FALLTHROUGH
		/* fallthrough */
#ifdef USE_SHA2
		/* fallthrough */
	case LDNS_RSASHA256:
		digest_size = (digest_size ? digest_size : SHA256_DIGEST_SIZE);
		ATTR_FALLTHROUGH
		/* fallthrough */
	case LDNS_RSASHA512:
		digest_size = (digest_size ? digest_size : SHA512_DIGEST_SIZE);

#endif
		*reason = _verify_nettle_rsa(buf, digest_size, (char*)sigblock,
						sigblock_len, key, keylen);
		if (*reason != NULL)
			return sec_status_bogus;
		else
			return sec_status_secure;

#ifdef USE_ECDSA
	case LDNS_ECDSAP256SHA256:
		digest_size = (digest_size ? digest_size : SHA256_DIGEST_SIZE);
		ATTR_FALLTHROUGH
		/* fallthrough */
	case LDNS_ECDSAP384SHA384:
		digest_size = (digest_size ? digest_size : SHA384_DIGEST_SIZE);
		*reason = _verify_nettle_ecdsa(buf, digest_size, sigblock,
						sigblock_len, key, keylen);
		if (*reason != NULL)
			return sec_status_bogus;
		else
			return sec_status_secure;
#endif
#ifdef USE_ED25519
	case LDNS_ED25519:
		*reason = _verify_nettle_ed25519(buf, sigblock, sigblock_len,
			key, keylen);
		if (*reason != NULL)
			return sec_status_bogus;
		else
			return sec_status_secure;
#endif
	case LDNS_RSAMD5:
	case LDNS_ECC_GOST:
	default:
		*reason = "unable to verify signature, unknown algorithm";
		return sec_status_bogus;
	}
}

#endif /* HAVE_SSL or HAVE_NSS or HAVE_NETTLE */
