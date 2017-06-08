/* Values for NULL algorithms */

#ifndef _CRYPTO_NULL_H
#define _CRYPTO_NULL_H

#define NULL_KEY_SIZE		0
#define NULL_BLOCK_SIZE		1
#define NULL_DIGEST_SIZE	0
#define NULL_IV_SIZE		0

struct crypto_skcipher *crypto_get_default_null_skcipher(void);
void crypto_put_default_null_skcipher(void);

static inline struct crypto_skcipher *crypto_get_default_null_skcipher2(void)
{
	return crypto_get_default_null_skcipher();
}

static inline void crypto_put_default_null_skcipher2(void)
{
	crypto_put_default_null_skcipher();
}

#endif
