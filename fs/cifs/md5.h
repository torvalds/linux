#ifndef MD5_H
#define MD5_H
#ifndef HEADER_MD5_H
/* Try to avoid clashes with OpenSSL */
#define HEADER_MD5_H
#endif

struct MD5Context {
	__u32 buf[4];
	__u32 bits[2];
	unsigned char in[64];
};
#endif				/* !MD5_H */

#ifndef _HMAC_MD5_H
struct HMACMD5Context {
	struct MD5Context ctx;
	unsigned char k_ipad[65];
	unsigned char k_opad[65];
};
#endif				/* _HMAC_MD5_H */

void cifs_MD5_init(struct MD5Context *context);
void cifs_MD5_update(struct MD5Context *context, unsigned char const *buf,
			unsigned len);
void cifs_MD5_final(unsigned char digest[16], struct MD5Context *context);

/* The following definitions come from lib/hmacmd5.c  */

/* void hmac_md5_init_rfc2104(unsigned char *key, int key_len,
			struct HMACMD5Context *ctx);*/
void hmac_md5_init_limK_to_64(const unsigned char *key, int key_len,
			struct HMACMD5Context *ctx);
void hmac_md5_update(const unsigned char *text, int text_len,
			struct HMACMD5Context *ctx);
void hmac_md5_final(unsigned char *digest, struct HMACMD5Context *ctx);
/* void hmac_md5(unsigned char key[16], unsigned char *data, int data_len,
			unsigned char *digest);*/
