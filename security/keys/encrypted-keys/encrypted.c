/*
 * Copyright (C) 2010 IBM Corporation
 * Copyright (C) 2010 Politecnico di Torino, Italy
 *                    TORSEC group -- http://security.polito.it
 *
 * Authors:
 * Mimi Zohar <zohar@us.ibm.com>
 * Roberto Sassu <roberto.sassu@polito.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * See Documentation/security/keys-trusted-encrypted.txt
 */

#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/parser.h>
#include <linux/string.h>
#include <linux/err.h>
#include <keys/user-type.h>
#include <keys/trusted-type.h>
#include <keys/encrypted-type.h>
#include <linux/key-type.h>
#include <linux/random.h>
#include <linux/rcupdate.h>
#include <linux/scatterlist.h>
#include <linux/crypto.h>
#include <linux/ctype.h>
#include <crypto/hash.h>
#include <crypto/sha.h>
#include <crypto/aes.h>

#include "encrypted.h"
#include "ecryptfs_format.h"

static const char KEY_TRUSTED_PREFIX[] = "trusted:";
static const char KEY_USER_PREFIX[] = "user:";
static const char hash_alg[] = "sha256";
static const char hmac_alg[] = "hmac(sha256)";
static const char blkcipher_alg[] = "cbc(aes)";
static const char key_format_default[] = "default";
static const char key_format_ecryptfs[] = "ecryptfs";
static unsigned int ivsize;
static int blksize;

#define KEY_TRUSTED_PREFIX_LEN (sizeof (KEY_TRUSTED_PREFIX) - 1)
#define KEY_USER_PREFIX_LEN (sizeof (KEY_USER_PREFIX) - 1)
#define KEY_ECRYPTFS_DESC_LEN 16
#define HASH_SIZE SHA256_DIGEST_SIZE
#define MAX_DATA_SIZE 4096
#define MIN_DATA_SIZE  20

struct sdesc {
	struct shash_desc shash;
	char ctx[];
};

static struct crypto_shash *hashalg;
static struct crypto_shash *hmacalg;

enum {
	Opt_err = -1, Opt_new, Opt_load, Opt_update
};

enum {
	Opt_error = -1, Opt_default, Opt_ecryptfs
};

static const match_table_t key_format_tokens = {
	{Opt_default, "default"},
	{Opt_ecryptfs, "ecryptfs"},
	{Opt_error, NULL}
};

static const match_table_t key_tokens = {
	{Opt_new, "new"},
	{Opt_load, "load"},
	{Opt_update, "update"},
	{Opt_err, NULL}
};

static int aes_get_sizes(void)
{
	struct crypto_blkcipher *tfm;

	tfm = crypto_alloc_blkcipher(blkcipher_alg, 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm)) {
		pr_err("encrypted_key: failed to alloc_cipher (%ld)\n",
		       PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}
	ivsize = crypto_blkcipher_ivsize(tfm);
	blksize = crypto_blkcipher_blocksize(tfm);
	crypto_free_blkcipher(tfm);
	return 0;
}

/*
 * valid_ecryptfs_desc - verify the description of a new/loaded encrypted key
 *
 * The description of a encrypted key with format 'ecryptfs' must contain
 * exactly 16 hexadecimal characters.
 *
 */
static int valid_ecryptfs_desc(const char *ecryptfs_desc)
{
	int i;

	if (strlen(ecryptfs_desc) != KEY_ECRYPTFS_DESC_LEN) {
		pr_err("encrypted_key: key description must be %d hexadecimal "
		       "characters long\n", KEY_ECRYPTFS_DESC_LEN);
		return -EINVAL;
	}

	for (i = 0; i < KEY_ECRYPTFS_DESC_LEN; i++) {
		if (!isxdigit(ecryptfs_desc[i])) {
			pr_err("encrypted_key: key description must contain "
			       "only hexadecimal characters\n");
			return -EINVAL;
		}
	}

	return 0;
}

/*
 * valid_master_desc - verify the 'key-type:desc' of a new/updated master-key
 *
 * key-type:= "trusted:" | "user:"
 * desc:= master-key description
 *
 * Verify that 'key-type' is valid and that 'desc' exists. On key update,
 * only the master key description is permitted to change, not the key-type.
 * The key-type remains constant.
 *
 * On success returns 0, otherwise -EINVAL.
 */
static int valid_master_desc(const char *new_desc, const char *orig_desc)
{
	if (!memcmp(new_desc, KEY_TRUSTED_PREFIX, KEY_TRUSTED_PREFIX_LEN)) {
		if (strlen(new_desc) == KEY_TRUSTED_PREFIX_LEN)
			goto out;
		if (orig_desc)
			if (memcmp(new_desc, orig_desc, KEY_TRUSTED_PREFIX_LEN))
				goto out;
	} else if (!memcmp(new_desc, KEY_USER_PREFIX, KEY_USER_PREFIX_LEN)) {
		if (strlen(new_desc) == KEY_USER_PREFIX_LEN)
			goto out;
		if (orig_desc)
			if (memcmp(new_desc, orig_desc, KEY_USER_PREFIX_LEN))
				goto out;
	} else
		goto out;
	return 0;
out:
	return -EINVAL;
}

/*
 * datablob_parse - parse the keyctl data
 *
 * datablob format:
 * new [<format>] <master-key name> <decrypted data length>
 * load [<format>] <master-key name> <decrypted data length>
 *     <encrypted iv + data>
 * update <new-master-key name>
 *
 * Tokenizes a copy of the keyctl data, returning a pointer to each token,
 * which is null terminated.
 *
 * On success returns 0, otherwise -EINVAL.
 */
static int datablob_parse(char *datablob, const char **format,
			  char **master_desc, char **decrypted_datalen,
			  char **hex_encoded_iv)
{
	substring_t args[MAX_OPT_ARGS];
	int ret = -EINVAL;
	int key_cmd;
	int key_format;
	char *p, *keyword;

	keyword = strsep(&datablob, " \t");
	if (!keyword) {
		pr_info("encrypted_key: insufficient parameters specified\n");
		return ret;
	}
	key_cmd = match_token(keyword, key_tokens, args);

	/* Get optional format: default | ecryptfs */
	p = strsep(&datablob, " \t");
	if (!p) {
		pr_err("encrypted_key: insufficient parameters specified\n");
		return ret;
	}

	key_format = match_token(p, key_format_tokens, args);
	switch (key_format) {
	case Opt_ecryptfs:
	case Opt_default:
		*format = p;
		*master_desc = strsep(&datablob, " \t");
		break;
	case Opt_error:
		*master_desc = p;
		break;
	}

	if (!*master_desc) {
		pr_info("encrypted_key: master key parameter is missing\n");
		goto out;
	}

	if (valid_master_desc(*master_desc, NULL) < 0) {
		pr_info("encrypted_key: master key parameter \'%s\' "
			"is invalid\n", *master_desc);
		goto out;
	}

	if (decrypted_datalen) {
		*decrypted_datalen = strsep(&datablob, " \t");
		if (!*decrypted_datalen) {
			pr_info("encrypted_key: keylen parameter is missing\n");
			goto out;
		}
	}

	switch (key_cmd) {
	case Opt_new:
		if (!decrypted_datalen) {
			pr_info("encrypted_key: keyword \'%s\' not allowed "
				"when called from .update method\n", keyword);
			break;
		}
		ret = 0;
		break;
	case Opt_load:
		if (!decrypted_datalen) {
			pr_info("encrypted_key: keyword \'%s\' not allowed "
				"when called from .update method\n", keyword);
			break;
		}
		*hex_encoded_iv = strsep(&datablob, " \t");
		if (!*hex_encoded_iv) {
			pr_info("encrypted_key: hex blob is missing\n");
			break;
		}
		ret = 0;
		break;
	case Opt_update:
		if (decrypted_datalen) {
			pr_info("encrypted_key: keyword \'%s\' not allowed "
				"when called from .instantiate method\n",
				keyword);
			break;
		}
		ret = 0;
		break;
	case Opt_err:
		pr_info("encrypted_key: keyword \'%s\' not recognized\n",
			keyword);
		break;
	}
out:
	return ret;
}

/*
 * datablob_format - format as an ascii string, before copying to userspace
 */
static char *datablob_format(struct encrypted_key_payload *epayload,
			     size_t asciiblob_len)
{
	char *ascii_buf, *bufp;
	u8 *iv = epayload->iv;
	int len;
	int i;

	ascii_buf = kmalloc(asciiblob_len + 1, GFP_KERNEL);
	if (!ascii_buf)
		goto out;

	ascii_buf[asciiblob_len] = '\0';

	/* copy datablob master_desc and datalen strings */
	len = sprintf(ascii_buf, "%s %s %s ", epayload->format,
		      epayload->master_desc, epayload->datalen);

	/* convert the hex encoded iv, encrypted-data and HMAC to ascii */
	bufp = &ascii_buf[len];
	for (i = 0; i < (asciiblob_len - len) / 2; i++)
		bufp = hex_byte_pack(bufp, iv[i]);
out:
	return ascii_buf;
}

/*
 * request_user_key - request the user key
 *
 * Use a user provided key to encrypt/decrypt an encrypted-key.
 */
static struct key *request_user_key(const char *master_desc, u8 **master_key,
				    size_t *master_keylen)
{
	struct user_key_payload *upayload;
	struct key *ukey;

	ukey = request_key(&key_type_user, master_desc, NULL);
	if (IS_ERR(ukey))
		goto error;

	down_read(&ukey->sem);
	upayload = ukey->payload.data;
	*master_key = upayload->data;
	*master_keylen = upayload->datalen;
error:
	return ukey;
}

static struct sdesc *alloc_sdesc(struct crypto_shash *alg)
{
	struct sdesc *sdesc;
	int size;

	size = sizeof(struct shash_desc) + crypto_shash_descsize(alg);
	sdesc = kmalloc(size, GFP_KERNEL);
	if (!sdesc)
		return ERR_PTR(-ENOMEM);
	sdesc->shash.tfm = alg;
	sdesc->shash.flags = 0x0;
	return sdesc;
}

static int calc_hmac(u8 *digest, const u8 *key, unsigned int keylen,
		     const u8 *buf, unsigned int buflen)
{
	struct sdesc *sdesc;
	int ret;

	sdesc = alloc_sdesc(hmacalg);
	if (IS_ERR(sdesc)) {
		pr_info("encrypted_key: can't alloc %s\n", hmac_alg);
		return PTR_ERR(sdesc);
	}

	ret = crypto_shash_setkey(hmacalg, key, keylen);
	if (!ret)
		ret = crypto_shash_digest(&sdesc->shash, buf, buflen, digest);
	kfree(sdesc);
	return ret;
}

static int calc_hash(u8 *digest, const u8 *buf, unsigned int buflen)
{
	struct sdesc *sdesc;
	int ret;

	sdesc = alloc_sdesc(hashalg);
	if (IS_ERR(sdesc)) {
		pr_info("encrypted_key: can't alloc %s\n", hash_alg);
		return PTR_ERR(sdesc);
	}

	ret = crypto_shash_digest(&sdesc->shash, buf, buflen, digest);
	kfree(sdesc);
	return ret;
}

enum derived_key_type { ENC_KEY, AUTH_KEY };

/* Derive authentication/encryption key from trusted key */
static int get_derived_key(u8 *derived_key, enum derived_key_type key_type,
			   const u8 *master_key, size_t master_keylen)
{
	u8 *derived_buf;
	unsigned int derived_buf_len;
	int ret;

	derived_buf_len = strlen("AUTH_KEY") + 1 + master_keylen;
	if (derived_buf_len < HASH_SIZE)
		derived_buf_len = HASH_SIZE;

	derived_buf = kzalloc(derived_buf_len, GFP_KERNEL);
	if (!derived_buf) {
		pr_err("encrypted_key: out of memory\n");
		return -ENOMEM;
	}
	if (key_type)
		strcpy(derived_buf, "AUTH_KEY");
	else
		strcpy(derived_buf, "ENC_KEY");

	memcpy(derived_buf + strlen(derived_buf) + 1, master_key,
	       master_keylen);
	ret = calc_hash(derived_key, derived_buf, derived_buf_len);
	kfree(derived_buf);
	return ret;
}

static int init_blkcipher_desc(struct blkcipher_desc *desc, const u8 *key,
			       unsigned int key_len, const u8 *iv,
			       unsigned int ivsize)
{
	int ret;

	desc->tfm = crypto_alloc_blkcipher(blkcipher_alg, 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(desc->tfm)) {
		pr_err("encrypted_key: failed to load %s transform (%ld)\n",
		       blkcipher_alg, PTR_ERR(desc->tfm));
		return PTR_ERR(desc->tfm);
	}
	desc->flags = 0;

	ret = crypto_blkcipher_setkey(desc->tfm, key, key_len);
	if (ret < 0) {
		pr_err("encrypted_key: failed to setkey (%d)\n", ret);
		crypto_free_blkcipher(desc->tfm);
		return ret;
	}
	crypto_blkcipher_set_iv(desc->tfm, iv, ivsize);
	return 0;
}

static struct key *request_master_key(struct encrypted_key_payload *epayload,
				      u8 **master_key, size_t *master_keylen)
{
	struct key *mkey = NULL;

	if (!strncmp(epayload->master_desc, KEY_TRUSTED_PREFIX,
		     KEY_TRUSTED_PREFIX_LEN)) {
		mkey = request_trusted_key(epayload->master_desc +
					   KEY_TRUSTED_PREFIX_LEN,
					   master_key, master_keylen);
	} else if (!strncmp(epayload->master_desc, KEY_USER_PREFIX,
			    KEY_USER_PREFIX_LEN)) {
		mkey = request_user_key(epayload->master_desc +
					KEY_USER_PREFIX_LEN,
					master_key, master_keylen);
	} else
		goto out;

	if (IS_ERR(mkey)) {
		int ret = PTR_ERR(mkey);

		if (ret == -ENOTSUPP)
			pr_info("encrypted_key: key %s not supported",
				epayload->master_desc);
		else
			pr_info("encrypted_key: key %s not found",
				epayload->master_desc);
		goto out;
	}

	dump_master_key(*master_key, *master_keylen);
out:
	return mkey;
}

/* Before returning data to userspace, encrypt decrypted data. */
static int derived_key_encrypt(struct encrypted_key_payload *epayload,
			       const u8 *derived_key,
			       unsigned int derived_keylen)
{
	struct scatterlist sg_in[2];
	struct scatterlist sg_out[1];
	struct blkcipher_desc desc;
	unsigned int encrypted_datalen;
	unsigned int padlen;
	char pad[16];
	int ret;

	encrypted_datalen = roundup(epayload->decrypted_datalen, blksize);
	padlen = encrypted_datalen - epayload->decrypted_datalen;

	ret = init_blkcipher_desc(&desc, derived_key, derived_keylen,
				  epayload->iv, ivsize);
	if (ret < 0)
		goto out;
	dump_decrypted_data(epayload);

	memset(pad, 0, sizeof pad);
	sg_init_table(sg_in, 2);
	sg_set_buf(&sg_in[0], epayload->decrypted_data,
		   epayload->decrypted_datalen);
	sg_set_buf(&sg_in[1], pad, padlen);

	sg_init_table(sg_out, 1);
	sg_set_buf(sg_out, epayload->encrypted_data, encrypted_datalen);

	ret = crypto_blkcipher_encrypt(&desc, sg_out, sg_in, encrypted_datalen);
	crypto_free_blkcipher(desc.tfm);
	if (ret < 0)
		pr_err("encrypted_key: failed to encrypt (%d)\n", ret);
	else
		dump_encrypted_data(epayload, encrypted_datalen);
out:
	return ret;
}

static int datablob_hmac_append(struct encrypted_key_payload *epayload,
				const u8 *master_key, size_t master_keylen)
{
	u8 derived_key[HASH_SIZE];
	u8 *digest;
	int ret;

	ret = get_derived_key(derived_key, AUTH_KEY, master_key, master_keylen);
	if (ret < 0)
		goto out;

	digest = epayload->format + epayload->datablob_len;
	ret = calc_hmac(digest, derived_key, sizeof derived_key,
			epayload->format, epayload->datablob_len);
	if (!ret)
		dump_hmac(NULL, digest, HASH_SIZE);
out:
	return ret;
}

/* verify HMAC before decrypting encrypted key */
static int datablob_hmac_verify(struct encrypted_key_payload *epayload,
				const u8 *format, const u8 *master_key,
				size_t master_keylen)
{
	u8 derived_key[HASH_SIZE];
	u8 digest[HASH_SIZE];
	int ret;
	char *p;
	unsigned short len;

	ret = get_derived_key(derived_key, AUTH_KEY, master_key, master_keylen);
	if (ret < 0)
		goto out;

	len = epayload->datablob_len;
	if (!format) {
		p = epayload->master_desc;
		len -= strlen(epayload->format) + 1;
	} else
		p = epayload->format;

	ret = calc_hmac(digest, derived_key, sizeof derived_key, p, len);
	if (ret < 0)
		goto out;
	ret = memcmp(digest, epayload->format + epayload->datablob_len,
		     sizeof digest);
	if (ret) {
		ret = -EINVAL;
		dump_hmac("datablob",
			  epayload->format + epayload->datablob_len,
			  HASH_SIZE);
		dump_hmac("calc", digest, HASH_SIZE);
	}
out:
	return ret;
}

static int derived_key_decrypt(struct encrypted_key_payload *epayload,
			       const u8 *derived_key,
			       unsigned int derived_keylen)
{
	struct scatterlist sg_in[1];
	struct scatterlist sg_out[2];
	struct blkcipher_desc desc;
	unsigned int encrypted_datalen;
	char pad[16];
	int ret;

	encrypted_datalen = roundup(epayload->decrypted_datalen, blksize);
	ret = init_blkcipher_desc(&desc, derived_key, derived_keylen,
				  epayload->iv, ivsize);
	if (ret < 0)
		goto out;
	dump_encrypted_data(epayload, encrypted_datalen);

	memset(pad, 0, sizeof pad);
	sg_init_table(sg_in, 1);
	sg_init_table(sg_out, 2);
	sg_set_buf(sg_in, epayload->encrypted_data, encrypted_datalen);
	sg_set_buf(&sg_out[0], epayload->decrypted_data,
		   epayload->decrypted_datalen);
	sg_set_buf(&sg_out[1], pad, sizeof pad);

	ret = crypto_blkcipher_decrypt(&desc, sg_out, sg_in, encrypted_datalen);
	crypto_free_blkcipher(desc.tfm);
	if (ret < 0)
		goto out;
	dump_decrypted_data(epayload);
out:
	return ret;
}

/* Allocate memory for decrypted key and datablob. */
static struct encrypted_key_payload *encrypted_key_alloc(struct key *key,
							 const char *format,
							 const char *master_desc,
							 const char *datalen)
{
	struct encrypted_key_payload *epayload = NULL;
	unsigned short datablob_len;
	unsigned short decrypted_datalen;
	unsigned short payload_datalen;
	unsigned int encrypted_datalen;
	unsigned int format_len;
	long dlen;
	int ret;

	ret = kstrtol(datalen, 10, &dlen);
	if (ret < 0 || dlen < MIN_DATA_SIZE || dlen > MAX_DATA_SIZE)
		return ERR_PTR(-EINVAL);

	format_len = (!format) ? strlen(key_format_default) : strlen(format);
	decrypted_datalen = dlen;
	payload_datalen = decrypted_datalen;
	if (format && !strcmp(format, key_format_ecryptfs)) {
		if (dlen != ECRYPTFS_MAX_KEY_BYTES) {
			pr_err("encrypted_key: keylen for the ecryptfs format "
			       "must be equal to %d bytes\n",
			       ECRYPTFS_MAX_KEY_BYTES);
			return ERR_PTR(-EINVAL);
		}
		decrypted_datalen = ECRYPTFS_MAX_KEY_BYTES;
		payload_datalen = sizeof(struct ecryptfs_auth_tok);
	}

	encrypted_datalen = roundup(decrypted_datalen, blksize);

	datablob_len = format_len + 1 + strlen(master_desc) + 1
	    + strlen(datalen) + 1 + ivsize + 1 + encrypted_datalen;

	ret = key_payload_reserve(key, payload_datalen + datablob_len
				  + HASH_SIZE + 1);
	if (ret < 0)
		return ERR_PTR(ret);

	epayload = kzalloc(sizeof(*epayload) + payload_datalen +
			   datablob_len + HASH_SIZE + 1, GFP_KERNEL);
	if (!epayload)
		return ERR_PTR(-ENOMEM);

	epayload->payload_datalen = payload_datalen;
	epayload->decrypted_datalen = decrypted_datalen;
	epayload->datablob_len = datablob_len;
	return epayload;
}

static int encrypted_key_decrypt(struct encrypted_key_payload *epayload,
				 const char *format, const char *hex_encoded_iv)
{
	struct key *mkey;
	u8 derived_key[HASH_SIZE];
	u8 *master_key;
	u8 *hmac;
	const char *hex_encoded_data;
	unsigned int encrypted_datalen;
	size_t master_keylen;
	size_t asciilen;
	int ret;

	encrypted_datalen = roundup(epayload->decrypted_datalen, blksize);
	asciilen = (ivsize + 1 + encrypted_datalen + HASH_SIZE) * 2;
	if (strlen(hex_encoded_iv) != asciilen)
		return -EINVAL;

	hex_encoded_data = hex_encoded_iv + (2 * ivsize) + 2;
	ret = hex2bin(epayload->iv, hex_encoded_iv, ivsize);
	if (ret < 0)
		return -EINVAL;
	ret = hex2bin(epayload->encrypted_data, hex_encoded_data,
		      encrypted_datalen);
	if (ret < 0)
		return -EINVAL;

	hmac = epayload->format + epayload->datablob_len;
	ret = hex2bin(hmac, hex_encoded_data + (encrypted_datalen * 2),
		      HASH_SIZE);
	if (ret < 0)
		return -EINVAL;

	mkey = request_master_key(epayload, &master_key, &master_keylen);
	if (IS_ERR(mkey))
		return PTR_ERR(mkey);

	ret = datablob_hmac_verify(epayload, format, master_key, master_keylen);
	if (ret < 0) {
		pr_err("encrypted_key: bad hmac (%d)\n", ret);
		goto out;
	}

	ret = get_derived_key(derived_key, ENC_KEY, master_key, master_keylen);
	if (ret < 0)
		goto out;

	ret = derived_key_decrypt(epayload, derived_key, sizeof derived_key);
	if (ret < 0)
		pr_err("encrypted_key: failed to decrypt key (%d)\n", ret);
out:
	up_read(&mkey->sem);
	key_put(mkey);
	return ret;
}

static void __ekey_init(struct encrypted_key_payload *epayload,
			const char *format, const char *master_desc,
			const char *datalen)
{
	unsigned int format_len;

	format_len = (!format) ? strlen(key_format_default) : strlen(format);
	epayload->format = epayload->payload_data + epayload->payload_datalen;
	epayload->master_desc = epayload->format + format_len + 1;
	epayload->datalen = epayload->master_desc + strlen(master_desc) + 1;
	epayload->iv = epayload->datalen + strlen(datalen) + 1;
	epayload->encrypted_data = epayload->iv + ivsize + 1;
	epayload->decrypted_data = epayload->payload_data;

	if (!format)
		memcpy(epayload->format, key_format_default, format_len);
	else {
		if (!strcmp(format, key_format_ecryptfs))
			epayload->decrypted_data =
				ecryptfs_get_auth_tok_key((struct ecryptfs_auth_tok *)epayload->payload_data);

		memcpy(epayload->format, format, format_len);
	}

	memcpy(epayload->master_desc, master_desc, strlen(master_desc));
	memcpy(epayload->datalen, datalen, strlen(datalen));
}

/*
 * encrypted_init - initialize an encrypted key
 *
 * For a new key, use a random number for both the iv and data
 * itself.  For an old key, decrypt the hex encoded data.
 */
static int encrypted_init(struct encrypted_key_payload *epayload,
			  const char *key_desc, const char *format,
			  const char *master_desc, const char *datalen,
			  const char *hex_encoded_iv)
{
	int ret = 0;

	if (format && !strcmp(format, key_format_ecryptfs)) {
		ret = valid_ecryptfs_desc(key_desc);
		if (ret < 0)
			return ret;

		ecryptfs_fill_auth_tok((struct ecryptfs_auth_tok *)epayload->payload_data,
				       key_desc);
	}

	__ekey_init(epayload, format, master_desc, datalen);
	if (!hex_encoded_iv) {
		get_random_bytes(epayload->iv, ivsize);

		get_random_bytes(epayload->decrypted_data,
				 epayload->decrypted_datalen);
	} else
		ret = encrypted_key_decrypt(epayload, format, hex_encoded_iv);
	return ret;
}

/*
 * encrypted_instantiate - instantiate an encrypted key
 *
 * Decrypt an existing encrypted datablob or create a new encrypted key
 * based on a kernel random number.
 *
 * On success, return 0. Otherwise return errno.
 */
static int encrypted_instantiate(struct key *key,
				 struct key_preparsed_payload *prep)
{
	struct encrypted_key_payload *epayload = NULL;
	char *datablob = NULL;
	const char *format = NULL;
	char *master_desc = NULL;
	char *decrypted_datalen = NULL;
	char *hex_encoded_iv = NULL;
	size_t datalen = prep->datalen;
	int ret;

	if (datalen <= 0 || datalen > 32767 || !prep->data)
		return -EINVAL;

	datablob = kmalloc(datalen + 1, GFP_KERNEL);
	if (!datablob)
		return -ENOMEM;
	datablob[datalen] = 0;
	memcpy(datablob, prep->data, datalen);
	ret = datablob_parse(datablob, &format, &master_desc,
			     &decrypted_datalen, &hex_encoded_iv);
	if (ret < 0)
		goto out;

	epayload = encrypted_key_alloc(key, format, master_desc,
				       decrypted_datalen);
	if (IS_ERR(epayload)) {
		ret = PTR_ERR(epayload);
		goto out;
	}
	ret = encrypted_init(epayload, key->description, format, master_desc,
			     decrypted_datalen, hex_encoded_iv);
	if (ret < 0) {
		kfree(epayload);
		goto out;
	}

	prep->payload[0] = epayload;
out:
	kfree(datablob);
	return ret;
}

static void encrypted_rcu_free(struct rcu_head *rcu)
{
	struct encrypted_key_payload *epayload;

	epayload = container_of(rcu, struct encrypted_key_payload, rcu);
	memset(epayload->decrypted_data, 0, epayload->decrypted_datalen);
	kfree(epayload);
}

/*
 * encrypted_update - update the master key description
 *
 * Change the master key description for an existing encrypted key.
 * The next read will return an encrypted datablob using the new
 * master key description.
 *
 * On success, return 0. Otherwise return errno.
 */
static int encrypted_update(struct key *key, struct key_preparsed_payload *prep)
{
	struct encrypted_key_payload *epayload = key->payload.data;
	struct encrypted_key_payload *new_epayload;
	char *buf;
	char *new_master_desc = NULL;
	const char *format = NULL;
	size_t datalen = prep->datalen;
	int ret = 0;

	if (datalen <= 0 || datalen > 32767 || !prep->data)
		return -EINVAL;

	buf = kmalloc(datalen + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	buf[datalen] = 0;
	memcpy(buf, prep->data, datalen);
	ret = datablob_parse(buf, &format, &new_master_desc, NULL, NULL);
	if (ret < 0)
		goto out;

	ret = valid_master_desc(new_master_desc, epayload->master_desc);
	if (ret < 0)
		goto out;

	new_epayload = encrypted_key_alloc(key, epayload->format,
					   new_master_desc, epayload->datalen);
	if (IS_ERR(new_epayload)) {
		ret = PTR_ERR(new_epayload);
		goto out;
	}

	__ekey_init(new_epayload, epayload->format, new_master_desc,
		    epayload->datalen);

	memcpy(new_epayload->iv, epayload->iv, ivsize);
	memcpy(new_epayload->payload_data, epayload->payload_data,
	       epayload->payload_datalen);

	rcu_assign_keypointer(key, new_epayload);
	call_rcu(&epayload->rcu, encrypted_rcu_free);
out:
	kfree(buf);
	return ret;
}

/*
 * encrypted_read - format and copy the encrypted data to userspace
 *
 * The resulting datablob format is:
 * <master-key name> <decrypted data length> <encrypted iv> <encrypted data>
 *
 * On success, return to userspace the encrypted key datablob size.
 */
static long encrypted_read(const struct key *key, char __user *buffer,
			   size_t buflen)
{
	struct encrypted_key_payload *epayload;
	struct key *mkey;
	u8 *master_key;
	size_t master_keylen;
	char derived_key[HASH_SIZE];
	char *ascii_buf;
	size_t asciiblob_len;
	int ret;

	epayload = rcu_dereference_key(key);

	/* returns the hex encoded iv, encrypted-data, and hmac as ascii */
	asciiblob_len = epayload->datablob_len + ivsize + 1
	    + roundup(epayload->decrypted_datalen, blksize)
	    + (HASH_SIZE * 2);

	if (!buffer || buflen < asciiblob_len)
		return asciiblob_len;

	mkey = request_master_key(epayload, &master_key, &master_keylen);
	if (IS_ERR(mkey))
		return PTR_ERR(mkey);

	ret = get_derived_key(derived_key, ENC_KEY, master_key, master_keylen);
	if (ret < 0)
		goto out;

	ret = derived_key_encrypt(epayload, derived_key, sizeof derived_key);
	if (ret < 0)
		goto out;

	ret = datablob_hmac_append(epayload, master_key, master_keylen);
	if (ret < 0)
		goto out;

	ascii_buf = datablob_format(epayload, asciiblob_len);
	if (!ascii_buf) {
		ret = -ENOMEM;
		goto out;
	}

	up_read(&mkey->sem);
	key_put(mkey);

	if (copy_to_user(buffer, ascii_buf, asciiblob_len) != 0)
		ret = -EFAULT;
	kfree(ascii_buf);

	return asciiblob_len;
out:
	up_read(&mkey->sem);
	key_put(mkey);
	return ret;
}

/*
 * encrypted_destroy - before freeing the key, clear the decrypted data
 *
 * Before freeing the key, clear the memory containing the decrypted
 * key data.
 */
static void encrypted_destroy(struct key *key)
{
	struct encrypted_key_payload *epayload = key->payload.data;

	if (!epayload)
		return;

	memset(epayload->decrypted_data, 0, epayload->decrypted_datalen);
	kfree(key->payload.data);
}

struct key_type key_type_encrypted = {
	.name = "encrypted",
	.instantiate = encrypted_instantiate,
	.update = encrypted_update,
	.match = user_match,
	.destroy = encrypted_destroy,
	.describe = user_describe,
	.read = encrypted_read,
};
EXPORT_SYMBOL_GPL(key_type_encrypted);

static void encrypted_shash_release(void)
{
	if (hashalg)
		crypto_free_shash(hashalg);
	if (hmacalg)
		crypto_free_shash(hmacalg);
}

static int __init encrypted_shash_alloc(void)
{
	int ret;

	hmacalg = crypto_alloc_shash(hmac_alg, 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(hmacalg)) {
		pr_info("encrypted_key: could not allocate crypto %s\n",
			hmac_alg);
		return PTR_ERR(hmacalg);
	}

	hashalg = crypto_alloc_shash(hash_alg, 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(hashalg)) {
		pr_info("encrypted_key: could not allocate crypto %s\n",
			hash_alg);
		ret = PTR_ERR(hashalg);
		goto hashalg_fail;
	}

	return 0;

hashalg_fail:
	crypto_free_shash(hmacalg);
	return ret;
}

static int __init init_encrypted(void)
{
	int ret;

	ret = encrypted_shash_alloc();
	if (ret < 0)
		return ret;
	ret = register_key_type(&key_type_encrypted);
	if (ret < 0)
		goto out;
	return aes_get_sizes();
out:
	encrypted_shash_release();
	return ret;

}

static void __exit cleanup_encrypted(void)
{
	encrypted_shash_release();
	unregister_key_type(&key_type_encrypted);
}

late_initcall(init_encrypted);
module_exit(cleanup_encrypted);

MODULE_LICENSE("GPL");
