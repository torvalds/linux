#ifndef __ENCRYPTED_KEY_H
#define __ENCRYPTED_KEY_H

#define ENCRYPTED_DEBUG 0
#if defined(CONFIG_TRUSTED_KEYS) || \
  (defined(CONFIG_TRUSTED_KEYS_MODULE) && defined(CONFIG_ENCRYPTED_KEYS_MODULE))
extern struct key *request_trusted_key(const char *trusted_desc,
				       u8 **master_key, size_t *master_keylen);
#else
static inline struct key *request_trusted_key(const char *trusted_desc,
					      u8 **master_key,
					      size_t *master_keylen)
{
	return ERR_PTR(-EOPNOTSUPP);
}
#endif

#if ENCRYPTED_DEBUG
static inline void dump_master_key(const u8 *master_key, size_t master_keylen)
{
	print_hex_dump(KERN_ERR, "master key: ", DUMP_PREFIX_NONE, 32, 1,
		       master_key, master_keylen, 0);
}

static inline void dump_decrypted_data(struct encrypted_key_payload *epayload)
{
	print_hex_dump(KERN_ERR, "decrypted data: ", DUMP_PREFIX_NONE, 32, 1,
		       epayload->decrypted_data,
		       epayload->decrypted_datalen, 0);
}

static inline void dump_encrypted_data(struct encrypted_key_payload *epayload,
				       unsigned int encrypted_datalen)
{
	print_hex_dump(KERN_ERR, "encrypted data: ", DUMP_PREFIX_NONE, 32, 1,
		       epayload->encrypted_data, encrypted_datalen, 0);
}

static inline void dump_hmac(const char *str, const u8 *digest,
			     unsigned int hmac_size)
{
	if (str)
		pr_info("encrypted_key: %s", str);
	print_hex_dump(KERN_ERR, "hmac: ", DUMP_PREFIX_NONE, 32, 1, digest,
		       hmac_size, 0);
}
#else
static inline void dump_master_key(const u8 *master_key, size_t master_keylen)
{
}

static inline void dump_decrypted_data(struct encrypted_key_payload *epayload)
{
}

static inline void dump_encrypted_data(struct encrypted_key_payload *epayload,
				       unsigned int encrypted_datalen)
{
}

static inline void dump_hmac(const char *str, const u8 *digest,
			     unsigned int hmac_size)
{
}
#endif
#endif
