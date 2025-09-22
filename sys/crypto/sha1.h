/*	$OpenBSD: sha1.h,v 1.6 2014/11/16 17:39:09 tedu Exp $	*/

/*
 * SHA-1 in C
 * By Steve Reid <steve@edmweb.com>
 * 100% Public Domain
 */

#ifndef _SHA1_H_
#define _SHA1_H_

#define	SHA1_BLOCK_LENGTH		64
#define	SHA1_DIGEST_LENGTH		20

typedef struct {
	u_int32_t	state[5];
	u_int64_t	count;
	unsigned char	buffer[SHA1_BLOCK_LENGTH];
} SHA1_CTX;
  
void SHA1Init(SHA1_CTX * context);
void SHA1Transform(u_int32_t state[5], const unsigned char buffer[SHA1_BLOCK_LENGTH]);
void SHA1Update(SHA1_CTX *context, const void *data, unsigned int len);
void SHA1Final(unsigned char digest[SHA1_DIGEST_LENGTH], SHA1_CTX *context);

#endif /* _SHA1_H_ */
