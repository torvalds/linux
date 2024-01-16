#ifndef _CRYPTO_GCM_H
#define _CRYPTO_GCM_H

#include <linux/errno.h>

#define GCM_AES_IV_SIZE 12
#define GCM_RFC4106_IV_SIZE 8
#define GCM_RFC4543_IV_SIZE 8

/*
 * validate authentication tag for GCM
 */
static inline int crypto_gcm_check_authsize(unsigned int authsize)
{
	switch (authsize) {
	case 4:
	case 8:
	case 12:
	case 13:
	case 14:
	case 15:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * validate authentication tag for RFC4106
 */
static inline int crypto_rfc4106_check_authsize(unsigned int authsize)
{
	switch (authsize) {
	case 8:
	case 12:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * validate assoclen for RFC4106/RFC4543
 */
static inline int crypto_ipsec_check_assoclen(unsigned int assoclen)
{
	switch (assoclen) {
	case 16:
	case 20:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
#endif
