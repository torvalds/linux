// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2010 IBM Corporation
 * Copyright (C) 2010 Politecnico di Torino, Italy
 *                    TORSEC group -- https://security.polito.it
 *
 * Authors:
 * Mimi Zohar <zohar@us.ibm.com>
 * Roberto Sassu <roberto.sassu@polito.it>
 *
 * See Documentation/security/keys/trusted-encrypted.rst
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
#include <linux/ctype.h>
#include <crypto/aes.h>
#include <crypto/hash.h>
#include <crypto/sha2.h>
#include <crypto/skcipher.h>
#include <crypto/utils.h>

#include "encrypted.h"
#include "ecryptfs_format.h"

static const char KEY_TRUSTED_PREFIX[] = "trusted:";
static const char KEY_USER_PREFIX[] = "user:";
static const char hash_alg[] = "sha256";
static const char hmac_alg[] = "hmac(sha256)";
static const char blkcipher_alg[] = "cbc(aes)";
static const char key_format_default[] = "default";
static const char key_format_ecryptfs[] = "ecryptfs";
static const char key_format_enc32[] = "enc32";
static unsigned int ivsize;
static int blksize;

#define KEY_TRUSTED_PREFIX_LEN (sizeof (KEY_TRUSTED_PREFIX) - 1)
#define KEY_USER_PREFIX_LEN (sizeof (KEY_USER_PREFIX) - 1)
#define KEY_ECRYPTFS_DESC_LEN 16
#define HASH_SIZE SHA256_DIGEST_SIZE
#define MAX_DATA_SIZE 4096
#define MIN_DATA_SIZE  20
#define KEY_ENC32_PAYLOAD_LEN 32

static struct crypto_shash *hash_tfm;

enum {
	Opt_new, Opt_load, Opt_update, Opt_err
};

enum {
	Opt_default, Opt_ecryptfs, Opt_enc32, Opt_error
};

static const match_table_t key_format_tokens = {
	{Opt_default, "default"},
	{Opt_ecryptfs, "ecryptfs"},
	{Opt_enc32, "enc32"},
	{Opt_error, NULL}
};

static const match_table_t key_tokens = {
	{Opt_new, "new"},
	{Opt_load, "load"},
	{Opt_update, "update"},
	{Opt_err, NULL}
};

static bool user_decrypted_data = IS_ENABLED(CONFIG_USER_DECRYPTED_DATA);
module_param(user_decrypted_data, bool, 0);
MODULE_PARM_DESC(user_decrypted_data,
	"Allow instantiation of encrypted keys using provided decrypted data");

static int aes_get_sizes(void)
{
	struct crypto_skcipher *tfm;

	tfm = crypto_alloc_skcipher(blkcipher_alg, 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm)) {
		pr_err("encrypted_key: failed to alloc_cipher (%ld)\n",
		       PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}
	ivsize = crypto_skcipher_ivsize(tfm);
	blksize = crypto_skcipher_blocksize(tfm);
	crypto_free_skcipher(tfm);
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
	int prefix_len;

	if (!strncmp(new_desc, KEY_TRUSTED_PREFIX, KEY_TRUSTED_PREFIX_LEN))
		prefix_len = KEY_TRUSTED_PREFIX_LEN;
	else if (!strncmp(new_desc, KEY_USER_PREFIX, KEY_USER_PREFIX_LEN))
		prefix_len = KEY_USER_PREFIX_LEN;
	else
		return -EINVAL;

	if (!new_desc[prefix_len])
		return -EINVAL;

	if (orig_desc && strncmp(new_desc, orig_desc, prefix_len))
		return -EINVAL;

	return 0;
}

/*
 * datablob_parse - parse the keyctl data
 *
 * datablob format:
 * new [<format>] <master-key name> <decrypted data length> [<decrypted data>]
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
			  char **hex_encoded_iv, char **decrypted_data)
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
	case Opt_enc32:
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
		*decrypted_data = strsep(&datablob, " \t");
		if (!*decrypted_data) {
			pr_info("encrypted_key: decrypted_data is missing\n");
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
static struct key *request_user_key(const char *master_desc, const u8 **master_key,
				    size_t *master_keylen)
{
	const struct user_key_payload *upayload;
	struct key *ukey;

	ukey = request_key(&key_type_user, master_desc, NULL);
	if (IS_ERR(ukey))
		goto error;

	down_read(&ukey->sem);
	upayload = user_key_payload_locked(ukey);
	if (!upayload) {
		/* key was revoked before we acquired its semaphore */
		up_read(&ukey->sem);
		key_put(ukey);
		ukey = ERR_PTR(-EKEYREVOKED);
		goto error;
	}
	*master_key = upayload->data;
	*master_keylen = upayload->datalen;
error:
	return ukey;
}

static int calc_hmac(u8 *digest, const u8 *key, unsigned int keylen,
		     const u8 *buf, unsigned int buflen)
{
	struct crypto_shash *tfm;
	int err;

	tfm = crypto_alloc_shash(hmac_alg, 0, 0);
	if (IS_ERR(tfm)) {
		pr_err("encrypted_key: can't alloc %s transform: %ld\n",
		       hmac_alg, PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}

	err = crypto_shash_setkey(tfm, key, keylen);
	if (!err)
		err = crypto_shash_tfm_digest(tfm, buf, buflen, digest);
	crypto_free_shash(tfm);
	return err;
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
	if (!derived_buf)
		return -ENOMEM;

	if (key_type)
		strcpy(derived_buf, "AUTH_KEY");
	else
		strcpy(derived_buf, "ENC_KEY");

	memcpy(derived_buf + strlen(derived_buf) + 1, master_key,
	       master_keylen);
	ret = crypto_shash_tfm_digest(hash_tfm, derived_buf, derived_buf_len,
				      derived_key);
	kfree_sensitive(derived_buf);
	return ret;
}

static struct skcipher_request *init_skcipher_req(const u8 *key,
						  unsigned int key_len)
{
	struct skcipher_request *req;
	struct crypto_skcipher *tfm;
	int ret;

	tfm = crypto_alloc_skcipher(blkcipher_alg, 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm)) {
		pr_err("encrypted_key: failed to load %s transform (%ld)\n",
		       blkcipher_alg, PTR_ERR(tfm));
		return ERR_CAST(tfm);
	}

	ret = crypto_skcipher_setkey(tfm, key, key_len);
	if (ret < 0) {
		pr_err("encrypted_key: failed to setkey (%d)\n", ret);
		crypto_free_skcipher(tfm);
		return ERR_PTR(ret);
	}

	req = skcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("encrypted_key: failed to allocate request for %s\n",
		       blkcipher_alg);
		crypto_free_skcipher(tfm);
		return ERR_PTR(-ENOMEM);
	}

	skcipher_request_set_callback(req, 0, NULL, NULL);
	return req;
}

static struct key *request_master_key(struct encrypted_key_payload *epayload,
				      const u8 **master_key, size_t *master_keylen)
{
	struct key *mkey = ERR_PTR(-EINVAL);

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
	struct crypto_skcipher *tfm;
	struct skcipher_request *req;
	unsigned int encrypted_datalen;
	u8 iv[AES_BLOCK_SIZE];
	int ret;

	encrypted_datalen = roundup(epayload->decrypted_datalen, blksize);

	req = init_skcipher_req(derived_key, derived_keylen);
	ret = PTR_ERR(req);
	if (IS_ERR(req))
		goto out;
	dump_decrypted_data(epayload);

	sg_init_table(sg_in, 2);
	sg_set_buf(&sg_in[0], epayload->decrypted_data,
		   epayload->decrypted_datalen);
	sg_set_page(&sg_in[1], ZERO_PAGE(0), AES_BLOCK_SIZE, 0);

	sg_init_table(sg_out, 1);
	sg_set_buf(sg_out, epayload->encrypted_data, encrypted_datalen);

	memcpy(iv, epayload->iv, sizeof(iv));
	skcipher_request_set_crypt(req, sg_in, sg_out, encrypted_datalen, iv);
	ret = crypto_skcipher_encrypt(req);
	tfm = crypto_skcipher_reqtfm(req);
	skcipher_request_free(req);
	crypto_free_skcipher(tfm);
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
	memzero_explicit(derived_key, sizeof(derived_key));
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
	ret = crypto_memneq(digest, epayload->format + epayload->datablob_len,
			    sizeof(digest));
	if (ret) {
		ret = -EINVAL;
		dump_hmac("datablob",
			  epayload->format + epayload->datablob_len,
			  HASH_SIZE);
		dump_hmac("calc", digest, HASH_SIZE);
	}
out:
	memzero_explicit(derived_key, sizeof(derived_key));
	return ret;
}

static int derived_key_decrypt(struct encrypted_key_payload *epayload,
			       const u8 *derived_key,
			       unsigned int derived_keylen)
{
	struct scatterlist sg_in[1];
	struct scatterlist sg_out[2];
	struct crypto_skcipher *tfm;
	struct skcipher_request *req;
	unsigned int encrypted_datalen;
	u8 iv[AES_BLOCK_SIZE];
	u8 *pad;
	int ret;

	/* Throwaway buffer to hold the unused zero padding at the end */
	pad = kmalloc(AES_BLOCK_SIZE, GFP_KERNEL);
	if (!pad)
		return -ENOMEM;

	encrypted_datalen = roundup(epayload->decrypted_datalen, blksize);
	req = init_skcipher_req(derived_key, derived_keylen);
	ret = PTR_ERR(req);
	if (IS_ERR(req))
		goto out;
	dump_encrypted_data(epayload, encrypted_datalen);

	sg_init_table(sg_in, 1);
	sg_init_table(sg_out, 2);
	sg_set_buf(sg_in, epayload->encrypted_data, encrypted_datalen);
	sg_set_buf(&sg_out[0], epayload->decrypted_data,
		   epayload->decrypted_datalen);
	sg_set_buf(&sg_out[1], pad, AES_BLOCK_SIZE);

	memcpy(iv, epayload->iv, sizeof(iv));
	skcipher_request_set_crypt(req, sg_in, sg_out, encrypted_datalen, iv);
	ret = crypto_skcipher_decrypt(req);
	tfm = crypto_skcipher_reqtfm(req);
	skcipher_request_free(req);
	crypto_free_skcipher(tfm);
	if (ret < 0)
		goto out;
	dump_decrypted_data(epayload);
out:
	kfree(pad);
	return ret;
}

/* Allocate memory for decrypted key and datablob. */
static struct encrypted_key_payload *encrypted_key_alloc(struct key *key,
							 const char *format,
							 const char *master_desc,
							 const char *datalen,
							 const char *decrypted_data)
{
	struct encrypted_key_payload *epayload = NULL;
	unsigned short datablob_len;
	unsigned short decrypted_datalen;
	unsigned short payload_datalen;
	unsigned int encrypted_datalen;
	unsigned int format_len;
	long dlen;
	int i;
	int ret;

	ret = kstrtol(datalen, 10, &dlen);
	if (ret < 0 || dlen < MIN_DATA_SIZE || dlen > MAX_DATA_SIZE)
		return ERR_PTR(-EINVAL);

	format_len = (!format) ? strlen(key_format_default) : strlen(format);
	decrypted_datalen = dlen;
	payload_datalen = decrypted_datalen;

	if (decrypted_data) {
		if (!user_decrypted_data) {
			pr_err("encrypted key: instantiation of keys using provided decrypted data is disabled since CONFIG_USER_DECRYPTED_DATA is set to false\n");
			return ERR_PTR(-EINVAL);
		}
		if (strlen(decrypted_data) != decrypted_datalen * 2) {
			pr_err("encrypted key: decrypted data provided does not match decrypted data length provided\n");
			return ERR_PTR(-EINVAL);
		}
		for (i = 0; i < strlen(decrypted_data); i++) {
			if (!isxdigit(decrypted_data[i])) {
				pr_err("encrypted key: decrypted data provided must contain only hexadecimal characters\n");
				return ERR_PTR(-EINVAL);
			}
		}
	}

	if (format) {
		if (!strcmp(format, key_format_ecryptfs)) {
			if (dlen != ECRYPTFS_MAX_KEY_BYTES) {
				pr_err("encrypted_key: keylen for the ecryptfs format must be equal to %d bytes\n",
					ECRYPTFS_MAX_KEY_BYTES);
				return ERR_PTR(-EINVAL);
			}
			decrypted_datalen = ECRYPTFS_MAX_KEY_BYTES;
			payload_datalen = sizeof(struct ecryptfs_auth_tok);
		} else if (!strcmp(format, key_format_enc32)) {
			if (decrypted_datalen != KEY_ENC32_PAYLOAD_LEN) {
				pr_err("encrypted_key: enc32 key payload incorrect length: %d\n",
						decrypted_datalen);
				return ERR_PTR(-EINVAL);
			}
		}
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
	const u8 *master_key;
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
	memzero_explicit(derived_key, sizeof(derived_key));
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
 * For a new key, use either a random number or user-provided decrypted data in
 * case it is provided. A random number is used for the iv in both cases. For
 * an old key, decrypt the hex encoded data.
 */
static int encrypted_init(struct encrypted_key_payload *epayload,
			  const char *key_desc, const char *format,
			  const char *master_desc, const char *datalen,
			  const char *hex_encoded_iv, const char *decrypted_data)
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
	if (hex_encoded_iv) {
		ret = encrypted_key_decrypt(epayload, format, hex_encoded_iv);
	} else if (decrypted_data) {
		get_random_bytes(epayload->iv, ivsize);
		ret = hex2bin(epayload->decrypted_data, decrypted_data,
			      epayload->decrypted_datalen);
	} else {
		get_random_bytes(epayload->iv, ivsize);
		get_random_bytes(epayload->decrypted_data, epayload->decrypted_datalen);
	}
	return ret;
}

/*
 * encrypted_instantiate - instantiate an encrypted key
 *
 * Instantiates the key:
 * - by decrypting an existing encrypted datablob, or
 * - by creating a new encrypted key based on a kernel random number, or
 * - using provided decrypted data.
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
	char *decrypted_data = NULL;
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
			     &decrypted_datalen, &hex_encoded_iv, &decrypted_data);
	if (ret < 0)
		goto out;

	epayload = encrypted_key_alloc(key, format, master_desc,
				       decrypted_datalen, decrypted_data);
	if (IS_ERR(epayload)) {
		ret = PTR_ERR(epayload);
		goto out;
	}
	ret = encrypted_init(epayload, key->description, format, master_desc,
			     decrypted_datalen, hex_encoded_iv, decrypted_data);
	if (ret < 0) {
		kfree_sensitive(epayload);
		goto out;
	}

	rcu_assign_keypointer(key, epayload);
out:
	kfree_sensitive(datablob);
	return ret;
}

static void encrypted_rcu_free(struct rcu_head *rcu)
{
	struct encrypted_key_payload *epayload;

	epayload = container_of(rcu, struct encrypted_key_payload, rcu);
	kfree_sensitive(epayload);
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
	struct encrypted_key_payload *epayload = key->payload.data[0];
	struct encrypted_key_payload *new_epayload;
	char *buf;
	char *new_master_desc = NULL;
	const char *format = NULL;
	size_t datalen = prep->datalen;
	int ret = 0;

	if (key_is_negative(key))
		return -ENOKEY;
	if (datalen <= 0 || datalen > 32767 || !prep->data)
		return -EINVAL;

	buf = kmalloc(datalen + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	buf[datalen] = 0;
	memcpy(buf, prep->data, datalen);
	ret = datablob_parse(buf, &format, &new_master_desc, NULL, NULL, NULL);
	if (ret < 0)
		goto out;

	ret = valid_master_desc(new_master_desc, epayload->master_desc);
	if (ret < 0)
		goto out;

	new_epayload = encrypted_key_alloc(key, epayload->format,
					   new_master_desc, epayload->datalen, NULL);
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
	kfree_sensitive(buf);
	return ret;
}

/*
 * encrypted_read - format and copy out the encrypted data
 *
 * The resulting datablob format is:
 * <master-key name> <decrypted data length> <encrypted iv> <encrypted data>
 *
 * On success, return to userspace the encrypted key datablob size.
 */
static long encrypted_read(const struct key *key, char *buffer,
			   size_t buflen)
{
	struct encrypted_key_payload *epayload;
	struct key *mkey;
	const u8 *master_key;
	size_t master_keylen;
	char derived_key[HASH_SIZE];
	char *ascii_buf;
	size_t asciiblob_len;
	int ret;

	epayload = dereference_key_locked(key);

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
	memzero_explicit(derived_key, sizeof(derived_key));

	memcpy(buffer, ascii_buf, asciiblob_len);
	kfree_sensitive(ascii_buf);

	return asciiblob_len;
out:
	up_read(&mkey->sem);
	key_put(mkey);
	memzero_explicit(derived_key, sizeof(derived_key));
	return ret;
}

/*
 * encrypted_destroy - clear and free the key's payload
 */
static void encrypted_destroy(struct key *key)
{
	kfree_sensitive(key->payload.data[0]);
}

struct key_type key_type_encrypted = {
	.name = "encrypted",
	.instantiate = encrypted_instantiate,
	.update = encrypted_update,
	.destroy = encrypted_destroy,
	.describe = user_describe,
	.read = encrypted_read,
};
EXPORT_SYMBOL_GPL(key_type_encrypted);

static int __init init_encrypted(void)
{
	int ret;

	hash_tfm = crypto_alloc_shash(hash_alg, 0, 0);
	if (IS_ERR(hash_tfm)) {
		pr_err("encrypted_key: can't allocate %s transform: %ld\n",
		       hash_alg, PTR_ERR(hash_tfm));
		return PTR_ERR(hash_tfm);
	}

	ret = aes_get_sizes();
	if (ret < 0)
		goto out;
	ret = register_key_type(&key_type_encrypted);
	if (ret < 0)
		goto out;
	return 0;
out:
	crypto_free_shash(hash_tfm);
	return ret;

}

static void __exit cleanup_encrypted(void)
{
	crypto_free_shash(hash_tfm);
	unregister_key_type(&key_type_encrypted);
}

late_initcall(init_encrypted);
module_exit(cleanup_encrypted);

MODULE_LICENSE("GPL");
