/** @file hmac.h
 *  @brief This header file contains function declarations of sha256&hmac
 *
 */

#ifndef _IWN_HMAC_20090216
#define _IWN_HMAC_20090216

#ifdef  __cplusplus
extern "C" {
#endif

int mhash_sha256(unsigned char* data, unsigned length, unsigned char* digest);
int iwn_hmac_sha256(unsigned char* text, int text_len, unsigned char* key, unsigned key_len, unsigned char* digest, unsigned digest_length);
void KD_hmac_sha256(unsigned char* text, unsigned text_len, unsigned char* key, unsigned key_len, unsigned char* output, unsigned length);

#ifdef  __cplusplus
}
#endif

#endif /*_IWN_HMAC_20090216*/

/*int mhash_sha256(unsigned char* d, unsigned l, unsigned char* o);
int hmac_sha256(unsigned char* t, int tl, unsigned char* k, unsigned kl, unsigned char* o, unsigned ol);
void KD_hmac_sha256(unsigned char* t, unsigned tl, unsigned char* k, unsigned kl, unsigned char* o, unsigned ol);*/
