// SPDX-License-Identifier: GPL-2.0
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/md5.h>

int main(void)
{
	EVP_MD_CTX *mdctx;
	unsigned char md[MD5_DIGEST_LENGTH + SHA_DIGEST_LENGTH];
	unsigned char dat[] = "12345";
	unsigned int digest_len;

	mdctx = EVP_MD_CTX_new();
	if (!mdctx)
		return 0;

	EVP_DigestInit_ex(mdctx, EVP_md5(), NULL);
	EVP_DigestUpdate(mdctx, &dat[0], sizeof(dat));
	EVP_DigestFinal_ex(mdctx, &md[0], &digest_len);
	EVP_MD_CTX_free(mdctx);

	SHA1(&dat[0], sizeof(dat), &md[0]);

	return 0;
}
