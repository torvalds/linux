/*
 * Octeon Crypto for OCF
 *
 * Written by David McCullough <david_mccullough@securecomputing.com>
 * Copyright (C) 2009 David McCullough
 *
 * LICENSE TERMS
 *
 * The free distribution and use of this software in both source and binary
 * form is allowed (with or without changes) provided that:
 *
 *   1. distributions of this source code include the above copyright
 *      notice, this list of conditions and the following disclaimer;
 *
 *   2. distributions in binary form include the above copyright
 *      notice, this list of conditions and the following disclaimer
 *      in the documentation and/or other associated materials;
 *
 *   3. the copyright holder's name is not used to endorse products
 *      built using this software without specific written permission.
 *
 * DISCLAIMER
 *
 * This software is provided 'as is' with no explicit or implied warranties
 * in respect of its properties, including, but not limited to, correctness
 * and/or fitness for purpose.
 * ---------------------------------------------------------------------------
 *
 * $FreeBSD$
 */

#ifndef	_MIPS_CAVIUM_CRYPTOCTEON_CRYPTOCTEONVAR_H_
#define	_MIPS_CAVIUM_CRYPTOCTEON_CRYPTOCTEONVAR_H_

struct octo_sess;

typedef	int octo_encrypt_t(struct octo_sess *od, struct iovec *iov, size_t iovcnt, size_t iovlen, int auth_off, int auth_len, int crypt_off, int crypt_len, int icv_off, uint8_t *ivp);
typedef	int octo_decrypt_t(struct octo_sess *od, struct iovec *iov, size_t iovcnt, size_t iovlen, int auth_off, int auth_len, int crypt_off, int crypt_len, int icv_off, uint8_t *ivp);

struct octo_sess {
	int					 octo_encalg;
	#define MAX_CIPHER_KEYLEN	64
	char				 octo_enckey[MAX_CIPHER_KEYLEN];
	int					 octo_encklen;

	int					 octo_macalg;
	#define MAX_HASH_KEYLEN	64
	char				 octo_mackey[MAX_HASH_KEYLEN];
	int					 octo_macklen;
	int					 octo_mackey_set;

	int					 octo_mlen;
	int					 octo_ivsize;

	octo_encrypt_t				*octo_encrypt;
	octo_decrypt_t				*octo_decrypt;

	uint64_t			 octo_hminner[3];
	uint64_t			 octo_hmouter[3];

	struct iovec				octo_iov[UIO_MAXIOV];
};

#define	dprintf(fmt, ...)						\
	do {								\
		if (cryptocteon_debug)					\
			printf("%s: " fmt, __func__, ## __VA_ARGS__);	\
	} while (0)

extern int cryptocteon_debug;

void octo_calc_hash(uint8_t, unsigned char *, uint64_t *, uint64_t *);

/* XXX Actually just hashing functions, not encryption.  */
octo_encrypt_t octo_null_md5_encrypt;
octo_encrypt_t octo_null_sha1_encrypt;

octo_encrypt_t octo_des_cbc_encrypt;
octo_encrypt_t octo_des_cbc_md5_encrypt;
octo_encrypt_t octo_des_cbc_sha1_encrypt;

octo_decrypt_t octo_des_cbc_decrypt;
octo_decrypt_t octo_des_cbc_md5_decrypt;
octo_decrypt_t octo_des_cbc_sha1_decrypt;

octo_encrypt_t octo_aes_cbc_encrypt;
octo_encrypt_t octo_aes_cbc_md5_encrypt;
octo_encrypt_t octo_aes_cbc_sha1_encrypt;

octo_decrypt_t octo_aes_cbc_decrypt;
octo_decrypt_t octo_aes_cbc_md5_decrypt;
octo_decrypt_t octo_aes_cbc_sha1_decrypt;

#endif /* !_MIPS_CAVIUM_CRYPTOCTEON_CRYPTOCTEONVAR_H_ */
