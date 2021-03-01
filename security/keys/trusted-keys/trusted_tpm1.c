// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2010 IBM Corporation
 * Copyright (c) 2019-2021, Linaro Limited
 *
 * See Documentation/security/keys/trusted-encrypted.rst
 */

#include <crypto/hash_info.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/parser.h>
#include <linux/string.h>
#include <linux/err.h>
#include <keys/trusted-type.h>
#include <linux/key-type.h>
#include <linux/crypto.h>
#include <crypto/hash.h>
#include <crypto/sha1.h>
#include <linux/tpm.h>
#include <linux/tpm_command.h>

#include <keys/trusted_tpm.h>

static const char hmac_alg[] = "hmac(sha1)";
static const char hash_alg[] = "sha1";
static struct tpm_chip *chip;
static struct tpm_digest *digests;

struct sdesc {
	struct shash_desc shash;
	char ctx[];
};

static struct crypto_shash *hashalg;
static struct crypto_shash *hmacalg;

static struct sdesc *init_sdesc(struct crypto_shash *alg)
{
	struct sdesc *sdesc;
	int size;

	size = sizeof(struct shash_desc) + crypto_shash_descsize(alg);
	sdesc = kmalloc(size, GFP_KERNEL);
	if (!sdesc)
		return ERR_PTR(-ENOMEM);
	sdesc->shash.tfm = alg;
	return sdesc;
}

static int TSS_sha1(const unsigned char *data, unsigned int datalen,
		    unsigned char *digest)
{
	struct sdesc *sdesc;
	int ret;

	sdesc = init_sdesc(hashalg);
	if (IS_ERR(sdesc)) {
		pr_info("can't alloc %s\n", hash_alg);
		return PTR_ERR(sdesc);
	}

	ret = crypto_shash_digest(&sdesc->shash, data, datalen, digest);
	kfree_sensitive(sdesc);
	return ret;
}

static int TSS_rawhmac(unsigned char *digest, const unsigned char *key,
		       unsigned int keylen, ...)
{
	struct sdesc *sdesc;
	va_list argp;
	unsigned int dlen;
	unsigned char *data;
	int ret;

	sdesc = init_sdesc(hmacalg);
	if (IS_ERR(sdesc)) {
		pr_info("can't alloc %s\n", hmac_alg);
		return PTR_ERR(sdesc);
	}

	ret = crypto_shash_setkey(hmacalg, key, keylen);
	if (ret < 0)
		goto out;
	ret = crypto_shash_init(&sdesc->shash);
	if (ret < 0)
		goto out;

	va_start(argp, keylen);
	for (;;) {
		dlen = va_arg(argp, unsigned int);
		if (dlen == 0)
			break;
		data = va_arg(argp, unsigned char *);
		if (data == NULL) {
			ret = -EINVAL;
			break;
		}
		ret = crypto_shash_update(&sdesc->shash, data, dlen);
		if (ret < 0)
			break;
	}
	va_end(argp);
	if (!ret)
		ret = crypto_shash_final(&sdesc->shash, digest);
out:
	kfree_sensitive(sdesc);
	return ret;
}

/*
 * calculate authorization info fields to send to TPM
 */
int TSS_authhmac(unsigned char *digest, const unsigned char *key,
			unsigned int keylen, unsigned char *h1,
			unsigned char *h2, unsigned int h3, ...)
{
	unsigned char paramdigest[SHA1_DIGEST_SIZE];
	struct sdesc *sdesc;
	unsigned int dlen;
	unsigned char *data;
	unsigned char c;
	int ret;
	va_list argp;

	if (!chip)
		return -ENODEV;

	sdesc = init_sdesc(hashalg);
	if (IS_ERR(sdesc)) {
		pr_info("can't alloc %s\n", hash_alg);
		return PTR_ERR(sdesc);
	}

	c = !!h3;
	ret = crypto_shash_init(&sdesc->shash);
	if (ret < 0)
		goto out;
	va_start(argp, h3);
	for (;;) {
		dlen = va_arg(argp, unsigned int);
		if (dlen == 0)
			break;
		data = va_arg(argp, unsigned char *);
		if (!data) {
			ret = -EINVAL;
			break;
		}
		ret = crypto_shash_update(&sdesc->shash, data, dlen);
		if (ret < 0)
			break;
	}
	va_end(argp);
	if (!ret)
		ret = crypto_shash_final(&sdesc->shash, paramdigest);
	if (!ret)
		ret = TSS_rawhmac(digest, key, keylen, SHA1_DIGEST_SIZE,
				  paramdigest, TPM_NONCE_SIZE, h1,
				  TPM_NONCE_SIZE, h2, 1, &c, 0, 0);
out:
	kfree_sensitive(sdesc);
	return ret;
}
EXPORT_SYMBOL_GPL(TSS_authhmac);

/*
 * verify the AUTH1_COMMAND (Seal) result from TPM
 */
int TSS_checkhmac1(unsigned char *buffer,
			  const uint32_t command,
			  const unsigned char *ononce,
			  const unsigned char *key,
			  unsigned int keylen, ...)
{
	uint32_t bufsize;
	uint16_t tag;
	uint32_t ordinal;
	uint32_t result;
	unsigned char *enonce;
	unsigned char *continueflag;
	unsigned char *authdata;
	unsigned char testhmac[SHA1_DIGEST_SIZE];
	unsigned char paramdigest[SHA1_DIGEST_SIZE];
	struct sdesc *sdesc;
	unsigned int dlen;
	unsigned int dpos;
	va_list argp;
	int ret;

	if (!chip)
		return -ENODEV;

	bufsize = LOAD32(buffer, TPM_SIZE_OFFSET);
	tag = LOAD16(buffer, 0);
	ordinal = command;
	result = LOAD32N(buffer, TPM_RETURN_OFFSET);
	if (tag == TPM_TAG_RSP_COMMAND)
		return 0;
	if (tag != TPM_TAG_RSP_AUTH1_COMMAND)
		return -EINVAL;
	authdata = buffer + bufsize - SHA1_DIGEST_SIZE;
	continueflag = authdata - 1;
	enonce = continueflag - TPM_NONCE_SIZE;

	sdesc = init_sdesc(hashalg);
	if (IS_ERR(sdesc)) {
		pr_info("can't alloc %s\n", hash_alg);
		return PTR_ERR(sdesc);
	}
	ret = crypto_shash_init(&sdesc->shash);
	if (ret < 0)
		goto out;
	ret = crypto_shash_update(&sdesc->shash, (const u8 *)&result,
				  sizeof result);
	if (ret < 0)
		goto out;
	ret = crypto_shash_update(&sdesc->shash, (const u8 *)&ordinal,
				  sizeof ordinal);
	if (ret < 0)
		goto out;
	va_start(argp, keylen);
	for (;;) {
		dlen = va_arg(argp, unsigned int);
		if (dlen == 0)
			break;
		dpos = va_arg(argp, unsigned int);
		ret = crypto_shash_update(&sdesc->shash, buffer + dpos, dlen);
		if (ret < 0)
			break;
	}
	va_end(argp);
	if (!ret)
		ret = crypto_shash_final(&sdesc->shash, paramdigest);
	if (ret < 0)
		goto out;

	ret = TSS_rawhmac(testhmac, key, keylen, SHA1_DIGEST_SIZE, paramdigest,
			  TPM_NONCE_SIZE, enonce, TPM_NONCE_SIZE, ononce,
			  1, continueflag, 0, 0);
	if (ret < 0)
		goto out;

	if (memcmp(testhmac, authdata, SHA1_DIGEST_SIZE))
		ret = -EINVAL;
out:
	kfree_sensitive(sdesc);
	return ret;
}
EXPORT_SYMBOL_GPL(TSS_checkhmac1);

/*
 * verify the AUTH2_COMMAND (unseal) result from TPM
 */
static int TSS_checkhmac2(unsigned char *buffer,
			  const uint32_t command,
			  const unsigned char *ononce,
			  const unsigned char *key1,
			  unsigned int keylen1,
			  const unsigned char *key2,
			  unsigned int keylen2, ...)
{
	uint32_t bufsize;
	uint16_t tag;
	uint32_t ordinal;
	uint32_t result;
	unsigned char *enonce1;
	unsigned char *continueflag1;
	unsigned char *authdata1;
	unsigned char *enonce2;
	unsigned char *continueflag2;
	unsigned char *authdata2;
	unsigned char testhmac1[SHA1_DIGEST_SIZE];
	unsigned char testhmac2[SHA1_DIGEST_SIZE];
	unsigned char paramdigest[SHA1_DIGEST_SIZE];
	struct sdesc *sdesc;
	unsigned int dlen;
	unsigned int dpos;
	va_list argp;
	int ret;

	bufsize = LOAD32(buffer, TPM_SIZE_OFFSET);
	tag = LOAD16(buffer, 0);
	ordinal = command;
	result = LOAD32N(buffer, TPM_RETURN_OFFSET);

	if (tag == TPM_TAG_RSP_COMMAND)
		return 0;
	if (tag != TPM_TAG_RSP_AUTH2_COMMAND)
		return -EINVAL;
	authdata1 = buffer + bufsize - (SHA1_DIGEST_SIZE + 1
			+ SHA1_DIGEST_SIZE + SHA1_DIGEST_SIZE);
	authdata2 = buffer + bufsize - (SHA1_DIGEST_SIZE);
	continueflag1 = authdata1 - 1;
	continueflag2 = authdata2 - 1;
	enonce1 = continueflag1 - TPM_NONCE_SIZE;
	enonce2 = continueflag2 - TPM_NONCE_SIZE;

	sdesc = init_sdesc(hashalg);
	if (IS_ERR(sdesc)) {
		pr_info("can't alloc %s\n", hash_alg);
		return PTR_ERR(sdesc);
	}
	ret = crypto_shash_init(&sdesc->shash);
	if (ret < 0)
		goto out;
	ret = crypto_shash_update(&sdesc->shash, (const u8 *)&result,
				  sizeof result);
	if (ret < 0)
		goto out;
	ret = crypto_shash_update(&sdesc->shash, (const u8 *)&ordinal,
				  sizeof ordinal);
	if (ret < 0)
		goto out;

	va_start(argp, keylen2);
	for (;;) {
		dlen = va_arg(argp, unsigned int);
		if (dlen == 0)
			break;
		dpos = va_arg(argp, unsigned int);
		ret = crypto_shash_update(&sdesc->shash, buffer + dpos, dlen);
		if (ret < 0)
			break;
	}
	va_end(argp);
	if (!ret)
		ret = crypto_shash_final(&sdesc->shash, paramdigest);
	if (ret < 0)
		goto out;

	ret = TSS_rawhmac(testhmac1, key1, keylen1, SHA1_DIGEST_SIZE,
			  paramdigest, TPM_NONCE_SIZE, enonce1,
			  TPM_NONCE_SIZE, ononce, 1, continueflag1, 0, 0);
	if (ret < 0)
		goto out;
	if (memcmp(testhmac1, authdata1, SHA1_DIGEST_SIZE)) {
		ret = -EINVAL;
		goto out;
	}
	ret = TSS_rawhmac(testhmac2, key2, keylen2, SHA1_DIGEST_SIZE,
			  paramdigest, TPM_NONCE_SIZE, enonce2,
			  TPM_NONCE_SIZE, ononce, 1, continueflag2, 0, 0);
	if (ret < 0)
		goto out;
	if (memcmp(testhmac2, authdata2, SHA1_DIGEST_SIZE))
		ret = -EINVAL;
out:
	kfree_sensitive(sdesc);
	return ret;
}

/*
 * For key specific tpm requests, we will generate and send our
 * own TPM command packets using the drivers send function.
 */
int trusted_tpm_send(unsigned char *cmd, size_t buflen)
{
	int rc;

	if (!chip)
		return -ENODEV;

	dump_tpm_buf(cmd);
	rc = tpm_send(chip, cmd, buflen);
	dump_tpm_buf(cmd);
	if (rc > 0)
		/* Can't return positive return codes values to keyctl */
		rc = -EPERM;
	return rc;
}
EXPORT_SYMBOL_GPL(trusted_tpm_send);

/*
 * Lock a trusted key, by extending a selected PCR.
 *
 * Prevents a trusted key that is sealed to PCRs from being accessed.
 * This uses the tpm driver's extend function.
 */
static int pcrlock(const int pcrnum)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	return tpm_pcr_extend(chip, pcrnum, digests) ? -EINVAL : 0;
}

/*
 * Create an object specific authorisation protocol (OSAP) session
 */
static int osap(struct tpm_buf *tb, struct osapsess *s,
		const unsigned char *key, uint16_t type, uint32_t handle)
{
	unsigned char enonce[TPM_NONCE_SIZE];
	unsigned char ononce[TPM_NONCE_SIZE];
	int ret;

	ret = tpm_get_random(chip, ononce, TPM_NONCE_SIZE);
	if (ret < 0)
		return ret;

	if (ret != TPM_NONCE_SIZE)
		return -EIO;

	tpm_buf_reset(tb, TPM_TAG_RQU_COMMAND, TPM_ORD_OSAP);
	tpm_buf_append_u16(tb, type);
	tpm_buf_append_u32(tb, handle);
	tpm_buf_append(tb, ononce, TPM_NONCE_SIZE);

	ret = trusted_tpm_send(tb->data, MAX_BUF_SIZE);
	if (ret < 0)
		return ret;

	s->handle = LOAD32(tb->data, TPM_DATA_OFFSET);
	memcpy(s->enonce, &(tb->data[TPM_DATA_OFFSET + sizeof(uint32_t)]),
	       TPM_NONCE_SIZE);
	memcpy(enonce, &(tb->data[TPM_DATA_OFFSET + sizeof(uint32_t) +
				  TPM_NONCE_SIZE]), TPM_NONCE_SIZE);
	return TSS_rawhmac(s->secret, key, SHA1_DIGEST_SIZE, TPM_NONCE_SIZE,
			   enonce, TPM_NONCE_SIZE, ononce, 0, 0);
}

/*
 * Create an object independent authorisation protocol (oiap) session
 */
int oiap(struct tpm_buf *tb, uint32_t *handle, unsigned char *nonce)
{
	int ret;

	if (!chip)
		return -ENODEV;

	tpm_buf_reset(tb, TPM_TAG_RQU_COMMAND, TPM_ORD_OIAP);
	ret = trusted_tpm_send(tb->data, MAX_BUF_SIZE);
	if (ret < 0)
		return ret;

	*handle = LOAD32(tb->data, TPM_DATA_OFFSET);
	memcpy(nonce, &tb->data[TPM_DATA_OFFSET + sizeof(uint32_t)],
	       TPM_NONCE_SIZE);
	return 0;
}
EXPORT_SYMBOL_GPL(oiap);

struct tpm_digests {
	unsigned char encauth[SHA1_DIGEST_SIZE];
	unsigned char pubauth[SHA1_DIGEST_SIZE];
	unsigned char xorwork[SHA1_DIGEST_SIZE * 2];
	unsigned char xorhash[SHA1_DIGEST_SIZE];
	unsigned char nonceodd[TPM_NONCE_SIZE];
};

/*
 * Have the TPM seal(encrypt) the trusted key, possibly based on
 * Platform Configuration Registers (PCRs). AUTH1 for sealing key.
 */
static int tpm_seal(struct tpm_buf *tb, uint16_t keytype,
		    uint32_t keyhandle, const unsigned char *keyauth,
		    const unsigned char *data, uint32_t datalen,
		    unsigned char *blob, uint32_t *bloblen,
		    const unsigned char *blobauth,
		    const unsigned char *pcrinfo, uint32_t pcrinfosize)
{
	struct osapsess sess;
	struct tpm_digests *td;
	unsigned char cont;
	uint32_t ordinal;
	uint32_t pcrsize;
	uint32_t datsize;
	int sealinfosize;
	int encdatasize;
	int storedsize;
	int ret;
	int i;

	/* alloc some work space for all the hashes */
	td = kmalloc(sizeof *td, GFP_KERNEL);
	if (!td)
		return -ENOMEM;

	/* get session for sealing key */
	ret = osap(tb, &sess, keyauth, keytype, keyhandle);
	if (ret < 0)
		goto out;
	dump_sess(&sess);

	/* calculate encrypted authorization value */
	memcpy(td->xorwork, sess.secret, SHA1_DIGEST_SIZE);
	memcpy(td->xorwork + SHA1_DIGEST_SIZE, sess.enonce, SHA1_DIGEST_SIZE);
	ret = TSS_sha1(td->xorwork, SHA1_DIGEST_SIZE * 2, td->xorhash);
	if (ret < 0)
		goto out;

	ret = tpm_get_random(chip, td->nonceodd, TPM_NONCE_SIZE);
	if (ret < 0)
		return ret;

	if (ret != TPM_NONCE_SIZE)
		return -EIO;

	ordinal = htonl(TPM_ORD_SEAL);
	datsize = htonl(datalen);
	pcrsize = htonl(pcrinfosize);
	cont = 0;

	/* encrypt data authorization key */
	for (i = 0; i < SHA1_DIGEST_SIZE; ++i)
		td->encauth[i] = td->xorhash[i] ^ blobauth[i];

	/* calculate authorization HMAC value */
	if (pcrinfosize == 0) {
		/* no pcr info specified */
		ret = TSS_authhmac(td->pubauth, sess.secret, SHA1_DIGEST_SIZE,
				   sess.enonce, td->nonceodd, cont,
				   sizeof(uint32_t), &ordinal, SHA1_DIGEST_SIZE,
				   td->encauth, sizeof(uint32_t), &pcrsize,
				   sizeof(uint32_t), &datsize, datalen, data, 0,
				   0);
	} else {
		/* pcr info specified */
		ret = TSS_authhmac(td->pubauth, sess.secret, SHA1_DIGEST_SIZE,
				   sess.enonce, td->nonceodd, cont,
				   sizeof(uint32_t), &ordinal, SHA1_DIGEST_SIZE,
				   td->encauth, sizeof(uint32_t), &pcrsize,
				   pcrinfosize, pcrinfo, sizeof(uint32_t),
				   &datsize, datalen, data, 0, 0);
	}
	if (ret < 0)
		goto out;

	/* build and send the TPM request packet */
	tpm_buf_reset(tb, TPM_TAG_RQU_AUTH1_COMMAND, TPM_ORD_SEAL);
	tpm_buf_append_u32(tb, keyhandle);
	tpm_buf_append(tb, td->encauth, SHA1_DIGEST_SIZE);
	tpm_buf_append_u32(tb, pcrinfosize);
	tpm_buf_append(tb, pcrinfo, pcrinfosize);
	tpm_buf_append_u32(tb, datalen);
	tpm_buf_append(tb, data, datalen);
	tpm_buf_append_u32(tb, sess.handle);
	tpm_buf_append(tb, td->nonceodd, TPM_NONCE_SIZE);
	tpm_buf_append_u8(tb, cont);
	tpm_buf_append(tb, td->pubauth, SHA1_DIGEST_SIZE);

	ret = trusted_tpm_send(tb->data, MAX_BUF_SIZE);
	if (ret < 0)
		goto out;

	/* calculate the size of the returned Blob */
	sealinfosize = LOAD32(tb->data, TPM_DATA_OFFSET + sizeof(uint32_t));
	encdatasize = LOAD32(tb->data, TPM_DATA_OFFSET + sizeof(uint32_t) +
			     sizeof(uint32_t) + sealinfosize);
	storedsize = sizeof(uint32_t) + sizeof(uint32_t) + sealinfosize +
	    sizeof(uint32_t) + encdatasize;

	/* check the HMAC in the response */
	ret = TSS_checkhmac1(tb->data, ordinal, td->nonceodd, sess.secret,
			     SHA1_DIGEST_SIZE, storedsize, TPM_DATA_OFFSET, 0,
			     0);

	/* copy the returned blob to caller */
	if (!ret) {
		memcpy(blob, tb->data + TPM_DATA_OFFSET, storedsize);
		*bloblen = storedsize;
	}
out:
	kfree_sensitive(td);
	return ret;
}

/*
 * use the AUTH2_COMMAND form of unseal, to authorize both key and blob
 */
static int tpm_unseal(struct tpm_buf *tb,
		      uint32_t keyhandle, const unsigned char *keyauth,
		      const unsigned char *blob, int bloblen,
		      const unsigned char *blobauth,
		      unsigned char *data, unsigned int *datalen)
{
	unsigned char nonceodd[TPM_NONCE_SIZE];
	unsigned char enonce1[TPM_NONCE_SIZE];
	unsigned char enonce2[TPM_NONCE_SIZE];
	unsigned char authdata1[SHA1_DIGEST_SIZE];
	unsigned char authdata2[SHA1_DIGEST_SIZE];
	uint32_t authhandle1 = 0;
	uint32_t authhandle2 = 0;
	unsigned char cont = 0;
	uint32_t ordinal;
	int ret;

	/* sessions for unsealing key and data */
	ret = oiap(tb, &authhandle1, enonce1);
	if (ret < 0) {
		pr_info("oiap failed (%d)\n", ret);
		return ret;
	}
	ret = oiap(tb, &authhandle2, enonce2);
	if (ret < 0) {
		pr_info("oiap failed (%d)\n", ret);
		return ret;
	}

	ordinal = htonl(TPM_ORD_UNSEAL);
	ret = tpm_get_random(chip, nonceodd, TPM_NONCE_SIZE);
	if (ret < 0)
		return ret;

	if (ret != TPM_NONCE_SIZE) {
		pr_info("tpm_get_random failed (%d)\n", ret);
		return -EIO;
	}
	ret = TSS_authhmac(authdata1, keyauth, TPM_NONCE_SIZE,
			   enonce1, nonceodd, cont, sizeof(uint32_t),
			   &ordinal, bloblen, blob, 0, 0);
	if (ret < 0)
		return ret;
	ret = TSS_authhmac(authdata2, blobauth, TPM_NONCE_SIZE,
			   enonce2, nonceodd, cont, sizeof(uint32_t),
			   &ordinal, bloblen, blob, 0, 0);
	if (ret < 0)
		return ret;

	/* build and send TPM request packet */
	tpm_buf_reset(tb, TPM_TAG_RQU_AUTH2_COMMAND, TPM_ORD_UNSEAL);
	tpm_buf_append_u32(tb, keyhandle);
	tpm_buf_append(tb, blob, bloblen);
	tpm_buf_append_u32(tb, authhandle1);
	tpm_buf_append(tb, nonceodd, TPM_NONCE_SIZE);
	tpm_buf_append_u8(tb, cont);
	tpm_buf_append(tb, authdata1, SHA1_DIGEST_SIZE);
	tpm_buf_append_u32(tb, authhandle2);
	tpm_buf_append(tb, nonceodd, TPM_NONCE_SIZE);
	tpm_buf_append_u8(tb, cont);
	tpm_buf_append(tb, authdata2, SHA1_DIGEST_SIZE);

	ret = trusted_tpm_send(tb->data, MAX_BUF_SIZE);
	if (ret < 0) {
		pr_info("authhmac failed (%d)\n", ret);
		return ret;
	}

	*datalen = LOAD32(tb->data, TPM_DATA_OFFSET);
	ret = TSS_checkhmac2(tb->data, ordinal, nonceodd,
			     keyauth, SHA1_DIGEST_SIZE,
			     blobauth, SHA1_DIGEST_SIZE,
			     sizeof(uint32_t), TPM_DATA_OFFSET,
			     *datalen, TPM_DATA_OFFSET + sizeof(uint32_t), 0,
			     0);
	if (ret < 0) {
		pr_info("TSS_checkhmac2 failed (%d)\n", ret);
		return ret;
	}
	memcpy(data, tb->data + TPM_DATA_OFFSET + sizeof(uint32_t), *datalen);
	return 0;
}

/*
 * Have the TPM seal(encrypt) the symmetric key
 */
static int key_seal(struct trusted_key_payload *p,
		    struct trusted_key_options *o)
{
	struct tpm_buf tb;
	int ret;

	ret = tpm_buf_init(&tb, 0, 0);
	if (ret)
		return ret;

	/* include migratable flag at end of sealed key */
	p->key[p->key_len] = p->migratable;

	ret = tpm_seal(&tb, o->keytype, o->keyhandle, o->keyauth,
		       p->key, p->key_len + 1, p->blob, &p->blob_len,
		       o->blobauth, o->pcrinfo, o->pcrinfo_len);
	if (ret < 0)
		pr_info("srkseal failed (%d)\n", ret);

	tpm_buf_destroy(&tb);
	return ret;
}

/*
 * Have the TPM unseal(decrypt) the symmetric key
 */
static int key_unseal(struct trusted_key_payload *p,
		      struct trusted_key_options *o)
{
	struct tpm_buf tb;
	int ret;

	ret = tpm_buf_init(&tb, 0, 0);
	if (ret)
		return ret;

	ret = tpm_unseal(&tb, o->keyhandle, o->keyauth, p->blob, p->blob_len,
			 o->blobauth, p->key, &p->key_len);
	if (ret < 0)
		pr_info("srkunseal failed (%d)\n", ret);
	else
		/* pull migratable flag out of sealed key */
		p->migratable = p->key[--p->key_len];

	tpm_buf_destroy(&tb);
	return ret;
}

enum {
	Opt_err,
	Opt_keyhandle, Opt_keyauth, Opt_blobauth,
	Opt_pcrinfo, Opt_pcrlock, Opt_migratable,
	Opt_hash,
	Opt_policydigest,
	Opt_policyhandle,
};

static const match_table_t key_tokens = {
	{Opt_keyhandle, "keyhandle=%s"},
	{Opt_keyauth, "keyauth=%s"},
	{Opt_blobauth, "blobauth=%s"},
	{Opt_pcrinfo, "pcrinfo=%s"},
	{Opt_pcrlock, "pcrlock=%s"},
	{Opt_migratable, "migratable=%s"},
	{Opt_hash, "hash=%s"},
	{Opt_policydigest, "policydigest=%s"},
	{Opt_policyhandle, "policyhandle=%s"},
	{Opt_err, NULL}
};

/* can have zero or more token= options */
static int getoptions(char *c, struct trusted_key_payload *pay,
		      struct trusted_key_options *opt)
{
	substring_t args[MAX_OPT_ARGS];
	char *p = c;
	int token;
	int res;
	unsigned long handle;
	unsigned long lock;
	unsigned long token_mask = 0;
	unsigned int digest_len;
	int i;
	int tpm2;

	tpm2 = tpm_is_tpm2(chip);
	if (tpm2 < 0)
		return tpm2;

	opt->hash = tpm2 ? HASH_ALGO_SHA256 : HASH_ALGO_SHA1;

	while ((p = strsep(&c, " \t"))) {
		if (*p == '\0' || *p == ' ' || *p == '\t')
			continue;
		token = match_token(p, key_tokens, args);
		if (test_and_set_bit(token, &token_mask))
			return -EINVAL;

		switch (token) {
		case Opt_pcrinfo:
			opt->pcrinfo_len = strlen(args[0].from) / 2;
			if (opt->pcrinfo_len > MAX_PCRINFO_SIZE)
				return -EINVAL;
			res = hex2bin(opt->pcrinfo, args[0].from,
				      opt->pcrinfo_len);
			if (res < 0)
				return -EINVAL;
			break;
		case Opt_keyhandle:
			res = kstrtoul(args[0].from, 16, &handle);
			if (res < 0)
				return -EINVAL;
			opt->keytype = SEAL_keytype;
			opt->keyhandle = handle;
			break;
		case Opt_keyauth:
			if (strlen(args[0].from) != 2 * SHA1_DIGEST_SIZE)
				return -EINVAL;
			res = hex2bin(opt->keyauth, args[0].from,
				      SHA1_DIGEST_SIZE);
			if (res < 0)
				return -EINVAL;
			break;
		case Opt_blobauth:
			/*
			 * TPM 1.2 authorizations are sha1 hashes passed in as
			 * hex strings.  TPM 2.0 authorizations are simple
			 * passwords (although it can take a hash as well)
			 */
			opt->blobauth_len = strlen(args[0].from);

			if (opt->blobauth_len == 2 * TPM_DIGEST_SIZE) {
				res = hex2bin(opt->blobauth, args[0].from,
					      TPM_DIGEST_SIZE);
				if (res < 0)
					return -EINVAL;

				opt->blobauth_len = TPM_DIGEST_SIZE;
				break;
			}

			if (tpm2 && opt->blobauth_len <= sizeof(opt->blobauth)) {
				memcpy(opt->blobauth, args[0].from,
				       opt->blobauth_len);
				break;
			}

			return -EINVAL;

			break;

		case Opt_migratable:
			if (*args[0].from == '0')
				pay->migratable = 0;
			else if (*args[0].from != '1')
				return -EINVAL;
			break;
		case Opt_pcrlock:
			res = kstrtoul(args[0].from, 10, &lock);
			if (res < 0)
				return -EINVAL;
			opt->pcrlock = lock;
			break;
		case Opt_hash:
			if (test_bit(Opt_policydigest, &token_mask))
				return -EINVAL;
			for (i = 0; i < HASH_ALGO__LAST; i++) {
				if (!strcmp(args[0].from, hash_algo_name[i])) {
					opt->hash = i;
					break;
				}
			}
			if (i == HASH_ALGO__LAST)
				return -EINVAL;
			if  (!tpm2 && i != HASH_ALGO_SHA1) {
				pr_info("TPM 1.x only supports SHA-1.\n");
				return -EINVAL;
			}
			break;
		case Opt_policydigest:
			digest_len = hash_digest_size[opt->hash];
			if (!tpm2 || strlen(args[0].from) != (2 * digest_len))
				return -EINVAL;
			res = hex2bin(opt->policydigest, args[0].from,
				      digest_len);
			if (res < 0)
				return -EINVAL;
			opt->policydigest_len = digest_len;
			break;
		case Opt_policyhandle:
			if (!tpm2)
				return -EINVAL;
			res = kstrtoul(args[0].from, 16, &handle);
			if (res < 0)
				return -EINVAL;
			opt->policyhandle = handle;
			break;
		default:
			return -EINVAL;
		}
	}
	return 0;
}

static struct trusted_key_options *trusted_options_alloc(void)
{
	struct trusted_key_options *options;
	int tpm2;

	tpm2 = tpm_is_tpm2(chip);
	if (tpm2 < 0)
		return NULL;

	options = kzalloc(sizeof *options, GFP_KERNEL);
	if (options) {
		/* set any non-zero defaults */
		options->keytype = SRK_keytype;

		if (!tpm2)
			options->keyhandle = SRKHANDLE;
	}
	return options;
}

static int trusted_tpm_seal(struct trusted_key_payload *p, char *datablob)
{
	struct trusted_key_options *options = NULL;
	int ret = 0;
	int tpm2;

	tpm2 = tpm_is_tpm2(chip);
	if (tpm2 < 0)
		return tpm2;

	options = trusted_options_alloc();
	if (!options)
		return -ENOMEM;

	ret = getoptions(datablob, p, options);
	if (ret < 0)
		goto out;
	dump_options(options);

	if (!options->keyhandle && !tpm2) {
		ret = -EINVAL;
		goto out;
	}

	if (tpm2)
		ret = tpm2_seal_trusted(chip, p, options);
	else
		ret = key_seal(p, options);
	if (ret < 0) {
		pr_info("key_seal failed (%d)\n", ret);
		goto out;
	}

	if (options->pcrlock) {
		ret = pcrlock(options->pcrlock);
		if (ret < 0) {
			pr_info("pcrlock failed (%d)\n", ret);
			goto out;
		}
	}
out:
	kfree_sensitive(options);
	return ret;
}

static int trusted_tpm_unseal(struct trusted_key_payload *p, char *datablob)
{
	struct trusted_key_options *options = NULL;
	int ret = 0;
	int tpm2;

	tpm2 = tpm_is_tpm2(chip);
	if (tpm2 < 0)
		return tpm2;

	options = trusted_options_alloc();
	if (!options)
		return -ENOMEM;

	ret = getoptions(datablob, p, options);
	if (ret < 0)
		goto out;
	dump_options(options);

	if (!options->keyhandle) {
		ret = -EINVAL;
		goto out;
	}

	if (tpm2)
		ret = tpm2_unseal_trusted(chip, p, options);
	else
		ret = key_unseal(p, options);
	if (ret < 0)
		pr_info("key_unseal failed (%d)\n", ret);

	if (options->pcrlock) {
		ret = pcrlock(options->pcrlock);
		if (ret < 0) {
			pr_info("pcrlock failed (%d)\n", ret);
			goto out;
		}
	}
out:
	kfree_sensitive(options);
	return ret;
}

static int trusted_tpm_get_random(unsigned char *key, size_t key_len)
{
	return tpm_get_random(chip, key, key_len);
}

static void trusted_shash_release(void)
{
	if (hashalg)
		crypto_free_shash(hashalg);
	if (hmacalg)
		crypto_free_shash(hmacalg);
}

static int __init trusted_shash_alloc(void)
{
	int ret;

	hmacalg = crypto_alloc_shash(hmac_alg, 0, 0);
	if (IS_ERR(hmacalg)) {
		pr_info("could not allocate crypto %s\n",
			hmac_alg);
		return PTR_ERR(hmacalg);
	}

	hashalg = crypto_alloc_shash(hash_alg, 0, 0);
	if (IS_ERR(hashalg)) {
		pr_info("could not allocate crypto %s\n",
			hash_alg);
		ret = PTR_ERR(hashalg);
		goto hashalg_fail;
	}

	return 0;

hashalg_fail:
	crypto_free_shash(hmacalg);
	return ret;
}

static int __init init_digests(void)
{
	int i;

	digests = kcalloc(chip->nr_allocated_banks, sizeof(*digests),
			  GFP_KERNEL);
	if (!digests)
		return -ENOMEM;

	for (i = 0; i < chip->nr_allocated_banks; i++)
		digests[i].alg_id = chip->allocated_banks[i].alg_id;

	return 0;
}

static int __init trusted_tpm_init(void)
{
	int ret;

	chip = tpm_default_chip();
	if (!chip)
		return -ENODEV;

	ret = init_digests();
	if (ret < 0)
		goto err_put;
	ret = trusted_shash_alloc();
	if (ret < 0)
		goto err_free;
	ret = register_key_type(&key_type_trusted);
	if (ret < 0)
		goto err_release;
	return 0;
err_release:
	trusted_shash_release();
err_free:
	kfree(digests);
err_put:
	put_device(&chip->dev);
	return ret;
}

static void trusted_tpm_exit(void)
{
	if (chip) {
		put_device(&chip->dev);
		kfree(digests);
		trusted_shash_release();
		unregister_key_type(&key_type_trusted);
	}
}

struct trusted_key_ops trusted_key_tpm_ops = {
	.migratable = 1, /* migratable by default */
	.init = trusted_tpm_init,
	.seal = trusted_tpm_seal,
	.unseal = trusted_tpm_unseal,
	.get_random = trusted_tpm_get_random,
	.exit = trusted_tpm_exit,
};
