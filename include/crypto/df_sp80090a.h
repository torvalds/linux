/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright Stephan Mueller <smueller@chronox.de>, 2014
 */

#ifndef _CRYPTO_DF80090A_H
#define _CRYPTO_DF80090A_H

#include <crypto/internal/cipher.h>
#include <crypto/aes.h>

static inline int crypto_drbg_ctr_df_datalen(u8 statelen, u8 blocklen)
{
	return statelen +       /* df_data */
		blocklen +      /* pad */
		blocklen +      /* iv */
		statelen + blocklen;  /* temp */
}

int crypto_drbg_ctr_df(struct aes_enckey *aes,
		       unsigned char *df_data,
		       size_t bytes_to_return,
		       struct list_head *seedlist,
		       u8 blocklen_bytes,
		       u8 statelen);

#endif /* _CRYPTO_DF80090A_H */
