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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "../libsecureboot-priv.h"

#include "decode.h"
#include "packet.h"

/**
 * @brief decode user-id packet
 *
 * This is trivial
 *
 * @sa rfc4880:5.11
 */
ssize_t
decode_user(int tag, unsigned char **pptr, size_t len, OpenPGP_user *user)
{
	char *cp;

	if (tag == 13) {
		user->id = malloc(len + 1);
		strncpy(user->id, (char *)*pptr, len);
		user->id[len] = '\0';
		user->name = user->id;
		cp = strchr(user->id, '<');
		if (cp > user->id) {
			user->id = strdup(user->id);
			cp[-1] = '\0';
		}
	}
	*pptr += len;
	return ((ssize_t)len);
}

/**
 * @brief decode a key packet
 *
 * We only really support v4 and RSA
 *
 * @sa rfc4880:5.5.1.1
 */
ssize_t
decode_key(int tag, unsigned char **pptr, size_t len, OpenPGP_key *key)
{
	unsigned char *ptr;
	int version;
#ifdef USE_BEARSSL
	br_sha1_context mctx;
	unsigned char mdata[br_sha512_SIZE];
	size_t mlen;
#else
	RSA *rsa = NULL;
	const EVP_MD *md = NULL;
	EVP_MD_CTX mctx;
	unsigned char mdata[EVP_MAX_MD_SIZE];
	unsigned int mlen;
#endif
    
	if (tag != 6)
		return (-1);

	key->key = NULL;
	ptr = *pptr;
	version = *ptr;
	if (version == 4) {		/* all we support really */
		/* comput key fingerprint and id @sa rfc4880:12.2 */
		mdata[0] = 0x99;	/* rfc4880: 12.2.a.1 */
		mdata[1] = (len >> 8) & 0xff;
		mdata[2] = len & 0xff;
	
#ifdef USE_BEARSSL
		br_sha1_init(&mctx);
		br_sha1_update(&mctx, mdata, 3);
		br_sha1_update(&mctx, ptr, len);
		br_sha1_out(&mctx, mdata);
		mlen = br_sha1_SIZE;
#else
		md = EVP_get_digestbyname("sha1");
		EVP_DigestInit(&mctx, md);
		EVP_DigestUpdate(&mctx, mdata, 3);
		EVP_DigestUpdate(&mctx, ptr, len);
		mlen = (unsigned int)sizeof(mdata);
		EVP_DigestFinal(&mctx, mdata, &mlen);
#endif
		key->id = octets2hex(&mdata[mlen - 8], 8);
	}
	ptr += 1;			/* done with version */
	ptr += 4;			/* skip ctime */
	if (version == 3)
		ptr += 2;		/* valid days */
	key->sig_alg = *ptr++;
	if (key->sig_alg == 1) {	/* RSA */
#ifdef USE_BEARSSL
		key->key = NEW(br_rsa_public_key);
		if (!key->key)
			goto oops;
		key->key->n = mpi2bn(&ptr, &key->key->nlen);
		key->key->e = mpi2bn(&ptr, &key->key->elen);
#else
		rsa = RSA_new();
		if (!rsa)
			goto oops;
		rsa->n = mpi2bn(&ptr);
		rsa->e = mpi2bn(&ptr);
		key->key = EVP_PKEY_new();
		if (!key->key || !rsa->n || !rsa->e) {
			goto oops;
		}
		if (!EVP_PKEY_set1_RSA(key->key, rsa))
			goto oops;
#endif
	}
	/* we are done */
	return ((ssize_t)len);
oops:
#ifdef USE_BEARSSL
	free(key->key);
	key->key = NULL;
#else
	if (rsa)
		RSA_free(rsa);
	if (key->key) {
		EVP_PKEY_free(key->key);
		key->key = NULL;
	}
#endif
	return (-1);
}

static OpenPGP_key *
load_key_buf(unsigned char *buf, size_t nbytes)
{
	unsigned char *data = NULL;
	unsigned char *ptr;
	ssize_t rc;
	int tag;
	OpenPGP_key *key;
    
	if (!buf)
		return (NULL);

	initialize();

	if (!(buf[0] & OPENPGP_TAG_ISTAG)) {
		data = dearmor((char *)buf, nbytes, &nbytes);
		ptr = data;
	} else
		ptr = buf;
	key = NEW(OpenPGP_key);
	if (key) {
		rc = decode_packet(0, &ptr, nbytes, (decoder_t)decode_key,
		    key);
		if (rc < 0) {
			free(key);
			key = NULL;
		} else if (rc > 8) {
			int isnew, ltype;

			tag = decode_tag(ptr, &isnew, &ltype);
			if (tag == 13) {
				key->user = NEW(OpenPGP_user);
				rc = decode_packet(0, &ptr, (size_t)rc,
				    (decoder_t)decode_user, key->user);
			}
		}
	}
	free(data);
	return (key);
}

static LIST_HEAD(, OpenPGP_key_) trust_list;

/**
 * @brief add a key to our list
 */
void
openpgp_trust_add(OpenPGP_key *key)
{
	static int once = 0;

	if (!once) {
		once = 1;

		LIST_INIT(&trust_list);
	}
	if (key)
		LIST_INSERT_HEAD(&trust_list, key, entries);
}

/**
 * @brief if keyID is in our list return the key
 *
 * @return key or NULL
 */
OpenPGP_key *
openpgp_trust_get(const char *keyID)
{
	OpenPGP_key *key;

	openpgp_trust_add(NULL);	/* initialize if needed */

	LIST_FOREACH(key, &trust_list, entries) {
		if (strcmp(key->id, keyID) == 0)
			return (key);
	}
	return (NULL);
}

/**
 * @brief load a key from file
 */
OpenPGP_key *
load_key_file(const char *kfile)
{
	unsigned char *data = NULL;
	size_t n;
	OpenPGP_key *key;

	data = read_file(kfile, &n);
	key = load_key_buf(data, n);
	free(data);
	openpgp_trust_add(key);
	return (key);
}

#include <ta_asc.h>

#ifndef _STANDALONE
/* we can lookup keyID in filesystem */

static const char *trust_store[] = {
	"/var/db/trust",
	"/etc/db/trust",
	NULL,
};

/**
 * @brief lookup key id in trust store
 *
 */
static OpenPGP_key *
load_trusted_key_id(const char *keyID)
{
	char kfile[MAXPATHLEN];
	const char **tp;
	size_t n;

	for (tp = trust_store; *tp; tp++) {
		n = (size_t)snprintf(kfile, sizeof(kfile), "%s/%s", *tp, keyID);
		if (n >= sizeof(kfile))
			return (NULL);
		if (access(kfile, R_OK) == 0) {
			return (load_key_file(kfile));
		}
	}
	return (NULL);
}
#endif

/**
 * @brief return key if trusted
 */
OpenPGP_key *
load_key_id(const char *keyID)
{
	static int once = 0;
	OpenPGP_key *key;

	if (!once) {
#ifdef HAVE_TA_ASC
		const char **tp;
		char *cp;
		size_t n;

		for (tp = ta_ASC; *tp; tp++) {
			if ((cp = strdup(*tp))) {
				n = strlen(cp);
				key = load_key_buf((unsigned char *)cp, n);
				free(cp);
				openpgp_trust_add(key);
			}
		}
#endif
		once = 1;
	}
	key = openpgp_trust_get(keyID);
#ifndef _STANDALONE
	if (!key)
		key = load_trusted_key_id(keyID);
#endif
	return (key);
}

/**
 * @brief test that we can verify a signature
 *
 * Unlike X.509 certificates, we only support RSA keys
 * so we stop after first successful signature verification
 * (which should also be the first attempt ;-)
 */
int
openpgp_self_tests(void)
{
	static int rc = -1;		/* remember result */
#ifdef HAVE_VC_ASC
	const char **vp, **tp;
	char *fdata, *sdata = NULL;
	size_t fbytes, sbytes;

	for (tp = ta_ASC, vp = vc_ASC; *tp && *vp && rc; tp++, vp++) {
		if ((fdata = strdup(*tp)) &&
		    (sdata = strdup(*vp))) {
			fbytes = strlen(fdata);
			sbytes = strlen(sdata);
			rc = openpgp_verify("ta_ASC",
			    (unsigned char *)fdata, fbytes,
			    (unsigned char *)sdata, sbytes, 0);
			printf("Testing verify OpenPGP signature:\t\t%s\n",
			    rc ? "Failed" : "Passed");
		}
		free(fdata);
		free(sdata);
	}
#endif
	return (rc);
}
