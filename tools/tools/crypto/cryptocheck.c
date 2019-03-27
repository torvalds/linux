/*-
 * Copyright (c) 2017 Chelsio Communications, Inc.
 * All rights reserved.
 * Written by: John Baldwin <jhb@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*-
 * Copyright (c) 2004 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */

/*
 * A different tool for checking hardware crypto support.  Whereas
 * cryptotest is focused on simple performance numbers, this tool is
 * focused on correctness.  For each crypto operation, it performs the
 * operation once in software via OpenSSL and a second time via
 * OpenCrypto and compares the results.
 *
 * cryptocheck [-vz] [-A aad length] [-a algorithm] [-d dev] [size ...]
 *
 * Options:
 *	-v	Verbose.
 *	-z	Run all algorithms on a variety of buffer sizes.
 *
 * Supported algorithms:
 *	all		Run all tests
 *	hmac		Run all hmac tests
 *	blkcipher	Run all block cipher tests
 *	authenc		Run all authenticated encryption tests
 *	aead		Run all authenticated encryption with associated data
 *			tests
 *
 * HMACs:
 *	sha1		sha1 hmac
 *	sha256		256-bit sha2 hmac
 *	sha384		384-bit sha2 hmac
 *	sha512		512-bit	sha2 hmac
 *	blake2b		Blake2-B
 *	blake2s		Blake2-S
 *
 * Block Ciphers:
 *	aes-cbc		128-bit aes cbc
 *	aes-cbc192	192-bit	aes cbc
 *	aes-cbc256	256-bit aes cbc
 *	aes-ctr		128-bit aes ctr
 *	aes-ctr192	192-bit aes ctr
 *	aes-ctr256	256-bit aes ctr
 *	aes-xts		128-bit aes xts
 *	aes-xts256	256-bit aes xts
 *	chacha20
 *
 * Authenticated Encryption:
 *	<block cipher>+<hmac>
 *
 * Authenticated Encryption with Associated Data:
 *	aes-gcm		128-bit aes gcm
 *	aes-gcm192	192-bit aes gcm
 *	aes-gcm256	256-bit aes gcm
 *	aes-ccm		128-bit aes ccm
 *	aes-ccm192	192-bit aes ccm
 *	aes-ccm256	256-bit aes ccm
 */

#include <sys/param.h>
#include <sys/sysctl.h>
#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <libutil.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/hmac.h>

#include <crypto/cryptodev.h>

/* XXX: Temporary hack */
#ifndef COP_F_CIPHER_FIRST
#define	COP_F_CIPHER_FIRST	0x0001	/* Cipher before MAC. */
#endif

struct alg {
	const char *name;
	int cipher;
	int mac;
	enum { T_HASH, T_HMAC, T_BLKCIPHER, T_AUTHENC, T_GCM, T_CCM } type;
	const EVP_CIPHER *(*evp_cipher)(void);
	const EVP_MD *(*evp_md)(void);
} algs[] = {
	{ .name = "sha1", .mac = CRYPTO_SHA1, .type = T_HASH,
	  .evp_md = EVP_sha1 },
	{ .name = "sha224", .mac = CRYPTO_SHA2_224, .type = T_HASH,
	  .evp_md = EVP_sha224 },
	{ .name = "sha256", .mac = CRYPTO_SHA2_256, .type = T_HASH,
	  .evp_md = EVP_sha256 },
	{ .name = "sha384", .mac = CRYPTO_SHA2_384, .type = T_HASH,
	  .evp_md = EVP_sha384 },
	{ .name = "sha512", .mac = CRYPTO_SHA2_512, .type = T_HASH,
	  .evp_md = EVP_sha512 },
	{ .name = "sha1hmac", .mac = CRYPTO_SHA1_HMAC, .type = T_HMAC,
	  .evp_md = EVP_sha1 },
	{ .name = "sha224hmac", .mac = CRYPTO_SHA2_224_HMAC, .type = T_HMAC,
	  .evp_md = EVP_sha224 },
	{ .name = "sha256hmac", .mac = CRYPTO_SHA2_256_HMAC, .type = T_HMAC,
	  .evp_md = EVP_sha256 },
	{ .name = "sha384hmac", .mac = CRYPTO_SHA2_384_HMAC, .type = T_HMAC,
	  .evp_md = EVP_sha384 },
	{ .name = "sha512hmac", .mac = CRYPTO_SHA2_512_HMAC, .type = T_HMAC,
	  .evp_md = EVP_sha512 },
	{ .name = "blake2b", .mac = CRYPTO_BLAKE2B, .type = T_HASH,
	  .evp_md = EVP_blake2b512 },
	{ .name = "blake2s", .mac = CRYPTO_BLAKE2S, .type = T_HASH,
	  .evp_md = EVP_blake2s256 },
	{ .name = "aes-cbc", .cipher = CRYPTO_AES_CBC, .type = T_BLKCIPHER,
	  .evp_cipher = EVP_aes_128_cbc },
	{ .name = "aes-cbc192", .cipher = CRYPTO_AES_CBC, .type = T_BLKCIPHER,
	  .evp_cipher = EVP_aes_192_cbc },
	{ .name = "aes-cbc256", .cipher = CRYPTO_AES_CBC, .type = T_BLKCIPHER,
	  .evp_cipher = EVP_aes_256_cbc },
	{ .name = "aes-ctr", .cipher = CRYPTO_AES_ICM, .type = T_BLKCIPHER,
	  .evp_cipher = EVP_aes_128_ctr },
	{ .name = "aes-ctr192", .cipher = CRYPTO_AES_ICM, .type = T_BLKCIPHER,
	  .evp_cipher = EVP_aes_192_ctr },
	{ .name = "aes-ctr256", .cipher = CRYPTO_AES_ICM, .type = T_BLKCIPHER,
	  .evp_cipher = EVP_aes_256_ctr },
	{ .name = "aes-xts", .cipher = CRYPTO_AES_XTS, .type = T_BLKCIPHER,
	  .evp_cipher = EVP_aes_128_xts },
	{ .name = "aes-xts256", .cipher = CRYPTO_AES_XTS, .type = T_BLKCIPHER,
	  .evp_cipher = EVP_aes_256_xts },
	{ .name = "chacha20", .cipher = CRYPTO_CHACHA20, .type = T_BLKCIPHER,
	  .evp_cipher = EVP_chacha20 },
	{ .name = "aes-gcm", .cipher = CRYPTO_AES_NIST_GCM_16,
	  .mac = CRYPTO_AES_128_NIST_GMAC, .type = T_GCM,
	  .evp_cipher = EVP_aes_128_gcm },
	{ .name = "aes-gcm192", .cipher = CRYPTO_AES_NIST_GCM_16,
	  .mac = CRYPTO_AES_192_NIST_GMAC, .type = T_GCM,
	  .evp_cipher = EVP_aes_192_gcm },
	{ .name = "aes-gcm256", .cipher = CRYPTO_AES_NIST_GCM_16,
	  .mac = CRYPTO_AES_256_NIST_GMAC, .type = T_GCM,
	  .evp_cipher = EVP_aes_256_gcm },
	{ .name = "aes-ccm", .cipher = CRYPTO_AES_CCM_16,
	  .mac = CRYPTO_AES_CCM_CBC_MAC, .type = T_CCM,
	  .evp_cipher = EVP_aes_128_ccm },
	{ .name = "aes-ccm192", .cipher = CRYPTO_AES_CCM_16,
	  .mac = CRYPTO_AES_CCM_CBC_MAC, .type = T_CCM,
	  .evp_cipher = EVP_aes_192_ccm },
	{ .name = "aes-ccm256", .cipher = CRYPTO_AES_CCM_16,
	  .mac = CRYPTO_AES_CCM_CBC_MAC, .type = T_CCM,
	  .evp_cipher = EVP_aes_256_ccm },
};

static bool verbose;
static int crid;
static size_t aad_len;

static void
usage(void)
{
	fprintf(stderr,
	    "usage: cryptocheck [-z] [-a algorithm] [-d dev] [size ...]\n");
	exit(1);
}

static struct alg *
find_alg(const char *name)
{
	u_int i;

	for (i = 0; i < nitems(algs); i++)
		if (strcasecmp(algs[i].name, name) == 0)
			return (&algs[i]);
	return (NULL);
}

static struct alg *
build_authenc(struct alg *cipher, struct alg *hmac)
{
	static struct alg authenc;
	char *name;

	assert(cipher->type == T_BLKCIPHER);
	assert(hmac->type == T_HMAC);
	memset(&authenc, 0, sizeof(authenc));
	asprintf(&name, "%s+%s", cipher->name, hmac->name);
	authenc.name = name;
	authenc.cipher = cipher->cipher;
	authenc.mac = hmac->mac;
	authenc.type = T_AUTHENC;
	authenc.evp_cipher = cipher->evp_cipher;
	authenc.evp_md = hmac->evp_md;
	return (&authenc);
}

static struct alg *
build_authenc_name(const char *name)
{
	struct alg *cipher, *hmac;
	const char *hmac_name;
	char *cp, *cipher_name;

	cp = strchr(name, '+');
	cipher_name = strndup(name, cp - name);
	hmac_name = cp + 1;
	cipher = find_alg(cipher_name);
	free(cipher_name);
	if (cipher == NULL)
		errx(1, "Invalid cipher %s", cipher_name);
	hmac = find_alg(hmac_name);
	if (hmac == NULL)
		errx(1, "Invalid hash %s", hmac_name);
	return (build_authenc(cipher, hmac));
}

static int
devcrypto(void)
{
	static int fd = -1;

	if (fd < 0) {
		fd = open("/dev/crypto", O_RDWR | O_CLOEXEC, 0);
		if (fd < 0)
			err(1, "/dev/crypto");
	}
	return (fd);
}

/*
 * Called on exit to change kern.cryptodevallowsoft back to 0
 */
#define CRYPT_SOFT_ALLOW	"kern.cryptodevallowsoft"

static void
reset_user_soft(void)
{
	int off = 0;
	sysctlbyname(CRYPT_SOFT_ALLOW, NULL, NULL, &off, sizeof(off));
}

static void
enable_user_soft(void)
{
	int curstate;
	int on = 1;
	size_t cursize = sizeof(curstate);

	if (sysctlbyname(CRYPT_SOFT_ALLOW, &curstate, &cursize,
		&on, sizeof(on)) == 0) {
		if (curstate == 0)
			atexit(reset_user_soft);
	}
}

static int
crlookup(const char *devname)
{
	struct crypt_find_op find;

	if (strncmp(devname, "soft", 4) == 0) {
		enable_user_soft();
		return CRYPTO_FLAG_SOFTWARE;
	}

	find.crid = -1;
	strlcpy(find.name, devname, sizeof(find.name));
	if (ioctl(devcrypto(), CIOCFINDDEV, &find) == -1)
		err(1, "ioctl(CIOCFINDDEV)");
	return (find.crid);
}

const char *
crfind(int crid)
{
	static struct crypt_find_op find;

	if (crid == CRYPTO_FLAG_SOFTWARE)
		return ("soft");
	else if (crid == CRYPTO_FLAG_HARDWARE)
		return ("unknown");

	bzero(&find, sizeof(find));
	find.crid = crid;
	if (ioctl(devcrypto(), CRIOFINDDEV, &find) == -1)
		err(1, "ioctl(CIOCFINDDEV): crid %d", crid);
	return (find.name);
}

static int
crget(void)
{
	int fd;

	if (ioctl(devcrypto(), CRIOGET, &fd) == -1)
		err(1, "ioctl(CRIOGET)");
	if (fcntl(fd, F_SETFD, 1) == -1)
		err(1, "fcntl(F_SETFD) (crget)");
	return fd;
}

static char
rdigit(void)
{
	const char a[] = {
		0x10,0x54,0x11,0x48,0x45,0x12,0x4f,0x13,0x49,0x53,0x14,0x41,
		0x15,0x16,0x4e,0x55,0x54,0x17,0x18,0x4a,0x4f,0x42,0x19,0x01
	};
	return 0x20+a[random()%nitems(a)];
}

static char *
alloc_buffer(size_t len)
{
	char *buf;
	size_t i;

	buf = malloc(len);
	for (i = 0; i < len; i++)
		buf[i] = rdigit();
	return (buf);
}

static char *
generate_iv(size_t len, struct alg *alg)
{
	char *iv;

	iv = alloc_buffer(len);
	switch (alg->cipher) {
	case CRYPTO_AES_ICM:
		/* Clear the low 32 bits of the IV to hold the counter. */
		iv[len - 4] = 0;
		iv[len - 3] = 0;
		iv[len - 2] = 0;
		iv[len - 1] = 0;
		break;
	case CRYPTO_AES_XTS:
		/*
		 * Clear the low 64-bits to only store a 64-bit block
		 * number.
		 */
		iv[len - 8] = 0;
		iv[len - 7] = 0;
		iv[len - 6] = 0;
		iv[len - 5] = 0;
		iv[len - 4] = 0;
		iv[len - 3] = 0;
		iv[len - 2] = 0;
		iv[len - 1] = 0;
		break;
	}
	return (iv);
}

static bool
ocf_hash(struct alg *alg, const char *buffer, size_t size, char *digest,
    int *cridp)
{
	struct session2_op sop;
	struct crypt_op cop;
	int fd;

	memset(&sop, 0, sizeof(sop));
	memset(&cop, 0, sizeof(cop));
	sop.crid = crid;
	sop.mac = alg->mac;
	fd = crget();
	if (ioctl(fd, CIOCGSESSION2, &sop) < 0) {
		warn("cryptodev %s HASH not supported for device %s",
		    alg->name, crfind(crid));
		close(fd);
		return (false);
	}

	cop.ses = sop.ses;
	cop.op = 0;
	cop.len = size;
	cop.src = (char *)buffer;
	cop.dst = NULL;
	cop.mac = digest;
	cop.iv = NULL;

	if (ioctl(fd, CIOCCRYPT, &cop) < 0) {
		warn("cryptodev %s (%zu) HASH failed for device %s", alg->name,
		    size, crfind(crid));
		close(fd);
		return (false);
	}

	if (ioctl(fd, CIOCFSESSION, &sop.ses) < 0)
		warn("ioctl(CIOCFSESSION)");

	close(fd);
	*cridp = sop.crid;
	return (true);
}

static void
openssl_hash(struct alg *alg, const EVP_MD *md, const void *buffer,
    size_t size, void *digest_out, unsigned *digest_sz_out)
{
	EVP_MD_CTX *mdctx;
	const char *errs;
	int rc;

	errs = "";

	mdctx = EVP_MD_CTX_create();
	if (mdctx == NULL)
		goto err_out;

	rc = EVP_DigestInit_ex(mdctx, md, NULL);
	if (rc != 1)
		goto err_out;

	rc = EVP_DigestUpdate(mdctx, buffer, size);
	if (rc != 1)
		goto err_out;

	rc = EVP_DigestFinal_ex(mdctx, digest_out, digest_sz_out);
	if (rc != 1)
		goto err_out;

	EVP_MD_CTX_destroy(mdctx);
	return;

err_out:
	errx(1, "OpenSSL %s HASH failed%s: %s", alg->name, errs,
	    ERR_error_string(ERR_get_error(), NULL));
}

static void
run_hash_test(struct alg *alg, size_t size)
{
	const EVP_MD *md;
	char *buffer;
	u_int digest_len;
	int crid;
	char control_digest[EVP_MAX_MD_SIZE], test_digest[EVP_MAX_MD_SIZE];

	memset(control_digest, 0x3c, sizeof(control_digest));
	memset(test_digest, 0x3c, sizeof(test_digest));

	md = alg->evp_md();
	assert(EVP_MD_size(md) <= sizeof(control_digest));

	buffer = alloc_buffer(size);

	/* OpenSSL HASH. */
	digest_len = sizeof(control_digest);
	openssl_hash(alg, md, buffer, size, control_digest, &digest_len);

	/* cryptodev HASH. */
	if (!ocf_hash(alg, buffer, size, test_digest, &crid))
		goto out;
	if (memcmp(control_digest, test_digest, sizeof(control_digest)) != 0) {
		if (memcmp(control_digest, test_digest, EVP_MD_size(md)) == 0)
			printf("%s (%zu) mismatch in trailer:\n",
			    alg->name, size);
		else
			printf("%s (%zu) mismatch:\n", alg->name, size);
		printf("control:\n");
		hexdump(control_digest, sizeof(control_digest), NULL, 0);
		printf("test (cryptodev device %s):\n", crfind(crid));
		hexdump(test_digest, sizeof(test_digest), NULL, 0);
		goto out;
	}

	if (verbose)
		printf("%s (%zu) matched (cryptodev device %s)\n",
		    alg->name, size, crfind(crid));

out:
	free(buffer);
}

static bool
ocf_hmac(struct alg *alg, const char *buffer, size_t size, const char *key,
    size_t key_len, char *digest, int *cridp)
{
	struct session2_op sop;
	struct crypt_op cop;
	int fd;

	memset(&sop, 0, sizeof(sop));
	memset(&cop, 0, sizeof(cop));
	sop.crid = crid;
	sop.mackeylen = key_len;
	sop.mackey = (char *)key;
	sop.mac = alg->mac;
	fd = crget();
	if (ioctl(fd, CIOCGSESSION2, &sop) < 0) {
		warn("cryptodev %s HMAC not supported for device %s",
		    alg->name, crfind(crid));
		close(fd);
		return (false);
	}

	cop.ses = sop.ses;
	cop.op = 0;
	cop.len = size;
	cop.src = (char *)buffer;
	cop.dst = NULL;
	cop.mac = digest;
	cop.iv = NULL;

	if (ioctl(fd, CIOCCRYPT, &cop) < 0) {
		warn("cryptodev %s (%zu) HMAC failed for device %s", alg->name,
		    size, crfind(crid));
		close(fd);
		return (false);
	}

	if (ioctl(fd, CIOCFSESSION, &sop.ses) < 0)
		warn("ioctl(CIOCFSESSION)");

	close(fd);
	*cridp = sop.crid;
	return (true);
}

static void
run_hmac_test(struct alg *alg, size_t size)
{
	const EVP_MD *md;
	char *key, *buffer;
	u_int key_len, digest_len;
	int crid;
	char control_digest[EVP_MAX_MD_SIZE], test_digest[EVP_MAX_MD_SIZE];

	memset(control_digest, 0x3c, sizeof(control_digest));
	memset(test_digest, 0x3c, sizeof(test_digest));

	md = alg->evp_md();
	key_len = EVP_MD_size(md);
	assert(EVP_MD_size(md) <= sizeof(control_digest));

	key = alloc_buffer(key_len);
	buffer = alloc_buffer(size);

	/* OpenSSL HMAC. */
	digest_len = sizeof(control_digest);
	if (HMAC(md, key, key_len, (u_char *)buffer, size,
	    (u_char *)control_digest, &digest_len) == NULL)
		errx(1, "OpenSSL %s (%zu) HMAC failed: %s", alg->name,
		    size, ERR_error_string(ERR_get_error(), NULL));

	/* cryptodev HMAC. */
	if (!ocf_hmac(alg, buffer, size, key, key_len, test_digest, &crid))
		goto out;
	if (memcmp(control_digest, test_digest, sizeof(control_digest)) != 0) {
		if (memcmp(control_digest, test_digest, EVP_MD_size(md)) == 0)
			printf("%s (%zu) mismatch in trailer:\n",
			    alg->name, size);
		else
			printf("%s (%zu) mismatch:\n", alg->name, size);
		printf("control:\n");
		hexdump(control_digest, sizeof(control_digest), NULL, 0);
		printf("test (cryptodev device %s):\n", crfind(crid));
		hexdump(test_digest, sizeof(test_digest), NULL, 0);
		goto out;
	}

	if (verbose)
		printf("%s (%zu) matched (cryptodev device %s)\n",
		    alg->name, size, crfind(crid));

out:
	free(buffer);
	free(key);
}

static void
openssl_cipher(struct alg *alg, const EVP_CIPHER *cipher, const char *key,
    const char *iv, const char *input, char *output, size_t size, int enc)
{
	EVP_CIPHER_CTX *ctx;
	int outl, total;

	ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL)
		errx(1, "OpenSSL %s (%zu) ctx new failed: %s", alg->name,
		    size, ERR_error_string(ERR_get_error(), NULL));
	if (EVP_CipherInit_ex(ctx, cipher, NULL, (const u_char *)key,
	    (const u_char *)iv, enc) != 1)
		errx(1, "OpenSSL %s (%zu) ctx init failed: %s", alg->name,
		    size, ERR_error_string(ERR_get_error(), NULL));
	EVP_CIPHER_CTX_set_padding(ctx, 0);
	if (EVP_CipherUpdate(ctx, (u_char *)output, &outl,
	    (const u_char *)input, size) != 1)
		errx(1, "OpenSSL %s (%zu) cipher update failed: %s", alg->name,
		    size, ERR_error_string(ERR_get_error(), NULL));
	total = outl;
	if (EVP_CipherFinal_ex(ctx, (u_char *)output + outl, &outl) != 1)
		errx(1, "OpenSSL %s (%zu) cipher final failed: %s", alg->name,
		    size, ERR_error_string(ERR_get_error(), NULL));
	total += outl;
	if (total != size)
		errx(1, "OpenSSL %s (%zu) cipher size mismatch: %d", alg->name,
		    size, total);
	EVP_CIPHER_CTX_free(ctx);
}

static bool
ocf_cipher(struct alg *alg, const char *key, size_t key_len,
    const char *iv, const char *input, char *output, size_t size, int enc,
    int *cridp)
{
	struct session2_op sop;
	struct crypt_op cop;
	int fd;

	memset(&sop, 0, sizeof(sop));
	memset(&cop, 0, sizeof(cop));
	sop.crid = crid;
	sop.keylen = key_len;
	sop.key = (char *)key;
	sop.cipher = alg->cipher;
	fd = crget();
	if (ioctl(fd, CIOCGSESSION2, &sop) < 0) {
		warn("cryptodev %s block cipher not supported for device %s",
		    alg->name, crfind(crid));
		close(fd);
		return (false);
	}

	cop.ses = sop.ses;
	cop.op = enc ? COP_ENCRYPT : COP_DECRYPT;
	cop.len = size;
	cop.src = (char *)input;
	cop.dst = output;
	cop.mac = NULL;
	cop.iv = (char *)iv;

	if (ioctl(fd, CIOCCRYPT, &cop) < 0) {
		warn("cryptodev %s (%zu) block cipher failed for device %s",
		    alg->name, size, crfind(crid));
		close(fd);
		return (false);
	}

	if (ioctl(fd, CIOCFSESSION, &sop.ses) < 0)
		warn("ioctl(CIOCFSESSION)");

	close(fd);
	*cridp = sop.crid;
	return (true);
}

static void
run_blkcipher_test(struct alg *alg, size_t size)
{
	const EVP_CIPHER *cipher;
	char *buffer, *cleartext, *ciphertext;
	char *iv, *key;
	u_int iv_len, key_len;
	int crid;

	cipher = alg->evp_cipher();
	if (size % EVP_CIPHER_block_size(cipher) != 0) {
		if (verbose)
			printf(
			    "%s (%zu): invalid buffer size (block size %d)\n",
			    alg->name, size, EVP_CIPHER_block_size(cipher));
		return;
	}

	key_len = EVP_CIPHER_key_length(cipher);
	iv_len = EVP_CIPHER_iv_length(cipher);

	key = alloc_buffer(key_len);
	iv = generate_iv(iv_len, alg);
	cleartext = alloc_buffer(size);
	buffer = malloc(size);
	ciphertext = malloc(size);

	/* OpenSSL cipher. */
	openssl_cipher(alg, cipher, key, iv, cleartext, ciphertext, size, 1);
	if (size > 0 && memcmp(cleartext, ciphertext, size) == 0)
		errx(1, "OpenSSL %s (%zu): cipher text unchanged", alg->name,
		    size);
	openssl_cipher(alg, cipher, key, iv, ciphertext, buffer, size, 0);
	if (memcmp(cleartext, buffer, size) != 0) {
		printf("OpenSSL %s (%zu): cipher mismatch:", alg->name, size);
		printf("original:\n");
		hexdump(cleartext, size, NULL, 0);
		printf("decrypted:\n");
		hexdump(buffer, size, NULL, 0);
		exit(1);
	}

	/* OCF encrypt. */
	if (!ocf_cipher(alg, key, key_len, iv, cleartext, buffer, size, 1,
	    &crid))
		goto out;
	if (memcmp(ciphertext, buffer, size) != 0) {
		printf("%s (%zu) encryption mismatch:\n", alg->name, size);
		printf("control:\n");
		hexdump(ciphertext, size, NULL, 0);
		printf("test (cryptodev device %s):\n", crfind(crid));
		hexdump(buffer, size, NULL, 0);
		goto out;
	}

	/* OCF decrypt. */
	if (!ocf_cipher(alg, key, key_len, iv, ciphertext, buffer, size, 0,
	    &crid))
		goto out;
	if (memcmp(cleartext, buffer, size) != 0) {
		printf("%s (%zu) decryption mismatch:\n", alg->name, size);
		printf("control:\n");
		hexdump(cleartext, size, NULL, 0);
		printf("test (cryptodev device %s):\n", crfind(crid));
		hexdump(buffer, size, NULL, 0);
		goto out;
	}

	if (verbose)
		printf("%s (%zu) matched (cryptodev device %s)\n",
		    alg->name, size, crfind(crid));

out:
	free(ciphertext);
	free(buffer);
	free(cleartext);
	free(iv);
	free(key);
}

static bool
ocf_authenc(struct alg *alg, const char *cipher_key, size_t cipher_key_len,
    const char *iv, size_t iv_len, const char *auth_key, size_t auth_key_len,
    const char *aad, size_t aad_len, const char *input, char *output,
    size_t size, char *digest, int enc, int *cridp)
{
	struct session2_op sop;
	int fd;

	memset(&sop, 0, sizeof(sop));
	sop.crid = crid;
	sop.keylen = cipher_key_len;
	sop.key = (char *)cipher_key;
	sop.cipher = alg->cipher;
	sop.mackeylen = auth_key_len;
	sop.mackey = (char *)auth_key;
	sop.mac = alg->mac;
	fd = crget();
	if (ioctl(fd, CIOCGSESSION2, &sop) < 0) {
		warn("cryptodev %s AUTHENC not supported for device %s",
		    alg->name, crfind(crid));
		close(fd);
		return (false);
	}

	if (aad_len != 0) {
		struct crypt_aead caead;

		memset(&caead, 0, sizeof(caead));
		caead.ses = sop.ses;
		caead.op = enc ? COP_ENCRYPT : COP_DECRYPT;
		caead.flags = enc ? COP_F_CIPHER_FIRST : 0;
		caead.len = size;
		caead.aadlen = aad_len;
		caead.ivlen = iv_len;
		caead.src = (char *)input;
		caead.dst = output;
		caead.aad = (char *)aad;
		caead.tag = digest;
		caead.iv = (char *)iv;

		if (ioctl(fd, CIOCCRYPTAEAD, &caead) < 0) {
			warn("cryptodev %s (%zu) failed for device %s",
			    alg->name, size, crfind(crid));
			close(fd);
			return (false);
		}
	} else {
		struct crypt_op cop;

		memset(&cop, 0, sizeof(cop));
		cop.ses = sop.ses;
		cop.op = enc ? COP_ENCRYPT : COP_DECRYPT;
		cop.flags = enc ? COP_F_CIPHER_FIRST : 0;
		cop.len = size;
		cop.src = (char *)input;
		cop.dst = output;
		cop.mac = digest;
		cop.iv = (char *)iv;

		if (ioctl(fd, CIOCCRYPT, &cop) < 0) {
			warn("cryptodev %s (%zu) AUTHENC failed for device %s",
			    alg->name, size, crfind(crid));
			close(fd);
			return (false);
		}
	}

	if (ioctl(fd, CIOCFSESSION, &sop.ses) < 0)
		warn("ioctl(CIOCFSESSION)");

	close(fd);
	*cridp = sop.crid;
	return (true);
}

static void
run_authenc_test(struct alg *alg, size_t size)
{
	const EVP_CIPHER *cipher;
	const EVP_MD *md;
	char *aad, *buffer, *cleartext, *ciphertext;
	char *iv, *auth_key, *cipher_key;
	u_int iv_len, auth_key_len, cipher_key_len, digest_len;
	int crid;
	char control_digest[EVP_MAX_MD_SIZE], test_digest[EVP_MAX_MD_SIZE];

	cipher = alg->evp_cipher();
	if (size % EVP_CIPHER_block_size(cipher) != 0) {
		if (verbose)
			printf(
			    "%s (%zu): invalid buffer size (block size %d)\n",
			    alg->name, size, EVP_CIPHER_block_size(cipher));
		return;
	}

	memset(control_digest, 0x3c, sizeof(control_digest));
	memset(test_digest, 0x3c, sizeof(test_digest));

	md = alg->evp_md();

	cipher_key_len = EVP_CIPHER_key_length(cipher);
	iv_len = EVP_CIPHER_iv_length(cipher);
	auth_key_len = EVP_MD_size(md);

	cipher_key = alloc_buffer(cipher_key_len);
	iv = generate_iv(iv_len, alg);
	auth_key = alloc_buffer(auth_key_len);
	cleartext = alloc_buffer(aad_len + size);
	buffer = malloc(aad_len + size);
	ciphertext = malloc(aad_len + size);

	/* OpenSSL encrypt + HMAC. */
	if (aad_len != 0)
		memcpy(ciphertext, cleartext, aad_len);
	openssl_cipher(alg, cipher, cipher_key, iv, cleartext + aad_len,
	    ciphertext + aad_len, size, 1);
	if (size > 0 && memcmp(cleartext + aad_len, ciphertext + aad_len,
	    size) == 0)
		errx(1, "OpenSSL %s (%zu): cipher text unchanged", alg->name,
		    size);
	digest_len = sizeof(control_digest);
	if (HMAC(md, auth_key, auth_key_len, (u_char *)ciphertext,
	    aad_len + size, (u_char *)control_digest, &digest_len) == NULL)
		errx(1, "OpenSSL %s (%zu) HMAC failed: %s", alg->name,
		    size, ERR_error_string(ERR_get_error(), NULL));

	/* OCF encrypt + HMAC. */
	if (!ocf_authenc(alg, cipher_key, cipher_key_len, iv, iv_len, auth_key,
	    auth_key_len, aad_len != 0 ? cleartext : NULL, aad_len,
	    cleartext + aad_len, buffer + aad_len, size, test_digest, 1, &crid))
		goto out;
	if (memcmp(ciphertext + aad_len, buffer + aad_len, size) != 0) {
		printf("%s (%zu) encryption mismatch:\n", alg->name, size);
		printf("control:\n");
		hexdump(ciphertext + aad_len, size, NULL, 0);
		printf("test (cryptodev device %s):\n", crfind(crid));
		hexdump(buffer + aad_len, size, NULL, 0);
		goto out;
	}
	if (memcmp(control_digest, test_digest, sizeof(control_digest)) != 0) {
		if (memcmp(control_digest, test_digest, EVP_MD_size(md)) == 0)
			printf("%s (%zu) enc hash mismatch in trailer:\n",
			    alg->name, size);
		else
			printf("%s (%zu) enc hash mismatch:\n", alg->name,
			    size);
		printf("control:\n");
		hexdump(control_digest, sizeof(control_digest), NULL, 0);
		printf("test (cryptodev device %s):\n", crfind(crid));
		hexdump(test_digest, sizeof(test_digest), NULL, 0);
		goto out;
	}

	/* OCF HMAC + decrypt. */
	memset(test_digest, 0x3c, sizeof(test_digest));
	if (!ocf_authenc(alg, cipher_key, cipher_key_len, iv, iv_len, auth_key,
	    auth_key_len, aad_len != 0 ? ciphertext : NULL, aad_len,
	    ciphertext + aad_len, buffer + aad_len, size, test_digest, 0,
	    &crid))
		goto out;
	if (memcmp(control_digest, test_digest, sizeof(control_digest)) != 0) {
		if (memcmp(control_digest, test_digest, EVP_MD_size(md)) == 0)
			printf("%s (%zu) dec hash mismatch in trailer:\n",
			    alg->name, size);
		else
			printf("%s (%zu) dec hash mismatch:\n", alg->name,
			    size);
		printf("control:\n");
		hexdump(control_digest, sizeof(control_digest), NULL, 0);
		printf("test (cryptodev device %s):\n", crfind(crid));
		hexdump(test_digest, sizeof(test_digest), NULL, 0);
		goto out;
	}
	if (memcmp(cleartext + aad_len, buffer + aad_len, size) != 0) {
		printf("%s (%zu) decryption mismatch:\n", alg->name, size);
		printf("control:\n");
		hexdump(cleartext, size, NULL, 0);
		printf("test (cryptodev device %s):\n", crfind(crid));
		hexdump(buffer, size, NULL, 0);
		goto out;
	}

	if (verbose)
		printf("%s (%zu) matched (cryptodev device %s)\n",
		    alg->name, size, crfind(crid));

out:
	free(ciphertext);
	free(buffer);
	free(cleartext);
	free(auth_key);
	free(iv);
	free(cipher_key);
}

static void
openssl_gcm_encrypt(struct alg *alg, const EVP_CIPHER *cipher, const char *key,
    const char *iv, const char *aad, size_t aad_len, const char *input,
    char *output, size_t size, char *tag)
{
	EVP_CIPHER_CTX *ctx;
	int outl, total;

	ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL)
		errx(1, "OpenSSL %s (%zu) ctx new failed: %s", alg->name,
		    size, ERR_error_string(ERR_get_error(), NULL));
	if (EVP_EncryptInit_ex(ctx, cipher, NULL, (const u_char *)key,
	    (const u_char *)iv) != 1)
		errx(1, "OpenSSL %s (%zu) ctx init failed: %s", alg->name,
		    size, ERR_error_string(ERR_get_error(), NULL));
	EVP_CIPHER_CTX_set_padding(ctx, 0);
	if (aad != NULL) {
		if (EVP_EncryptUpdate(ctx, NULL, &outl, (const u_char *)aad,
		    aad_len) != 1)
			errx(1, "OpenSSL %s (%zu) aad update failed: %s",
			    alg->name, size,
			    ERR_error_string(ERR_get_error(), NULL));
	}
	if (EVP_EncryptUpdate(ctx, (u_char *)output, &outl,
	    (const u_char *)input, size) != 1)
		errx(1, "OpenSSL %s (%zu) encrypt update failed: %s", alg->name,
		    size, ERR_error_string(ERR_get_error(), NULL));
	total = outl;
	if (EVP_EncryptFinal_ex(ctx, (u_char *)output + outl, &outl) != 1)
		errx(1, "OpenSSL %s (%zu) encrypt final failed: %s", alg->name,
		    size, ERR_error_string(ERR_get_error(), NULL));
	total += outl;
	if (total != size)
		errx(1, "OpenSSL %s (%zu) encrypt size mismatch: %d", alg->name,
		    size, total);
	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, AES_GMAC_HASH_LEN,
	    tag) != 1)
		errx(1, "OpenSSL %s (%zu) get tag failed: %s", alg->name,
		    size, ERR_error_string(ERR_get_error(), NULL));
	EVP_CIPHER_CTX_free(ctx);
}

static bool
ocf_gcm(struct alg *alg, const char *key, size_t key_len, const char *iv,
    size_t iv_len, const char *aad, size_t aad_len, const char *input,
    char *output, size_t size, char *tag, int enc, int *cridp)
{
	struct session2_op sop;
	struct crypt_aead caead;
	int fd;

	memset(&sop, 0, sizeof(sop));
	memset(&caead, 0, sizeof(caead));
	sop.crid = crid;
	sop.keylen = key_len;
	sop.key = (char *)key;
	sop.cipher = alg->cipher;
	sop.mackeylen = key_len;
	sop.mackey = (char *)key;
	sop.mac = alg->mac;
	fd = crget();
	if (ioctl(fd, CIOCGSESSION2, &sop) < 0) {
		warn("cryptodev %s not supported for device %s",
		    alg->name, crfind(crid));
		close(fd);
		return (false);
	}

	caead.ses = sop.ses;
	caead.op = enc ? COP_ENCRYPT : COP_DECRYPT;
	caead.len = size;
	caead.aadlen = aad_len;
	caead.ivlen = iv_len;
	caead.src = (char *)input;
	caead.dst = output;
	caead.aad = (char *)aad;
	caead.tag = tag;
	caead.iv = (char *)iv;

	if (ioctl(fd, CIOCCRYPTAEAD, &caead) < 0) {
		warn("cryptodev %s (%zu) failed for device %s",
		    alg->name, size, crfind(crid));
		close(fd);
		return (false);
	}

	if (ioctl(fd, CIOCFSESSION, &sop.ses) < 0)
		warn("ioctl(CIOCFSESSION)");

	close(fd);
	*cridp = sop.crid;
	return (true);
}

#ifdef notused
static bool
openssl_gcm_decrypt(struct alg *alg, const EVP_CIPHER *cipher, const char *key,
    const char *iv, const char *aad, size_t aad_len, const char *input,
    char *output, size_t size, char *tag)
{
	EVP_CIPHER_CTX *ctx;
	int outl, total;
	bool valid;

	ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL)
		errx(1, "OpenSSL %s (%zu) ctx new failed: %s", alg->name,
		    size, ERR_error_string(ERR_get_error(), NULL));
	if (EVP_DecryptInit_ex(ctx, cipher, NULL, (const u_char *)key,
	    (const u_char *)iv) != 1)
		errx(1, "OpenSSL %s (%zu) ctx init failed: %s", alg->name,
		    size, ERR_error_string(ERR_get_error(), NULL));
	EVP_CIPHER_CTX_set_padding(ctx, 0);
	if (aad != NULL) {
		if (EVP_DecryptUpdate(ctx, NULL, &outl, (const u_char *)aad,
		    aad_len) != 1)
			errx(1, "OpenSSL %s (%zu) aad update failed: %s",
			    alg->name, size,
			    ERR_error_string(ERR_get_error(), NULL));
	}
	if (EVP_DecryptUpdate(ctx, (u_char *)output, &outl,
	    (const u_char *)input, size) != 1)
		errx(1, "OpenSSL %s (%zu) decrypt update failed: %s", alg->name,
		    size, ERR_error_string(ERR_get_error(), NULL));
	total = outl;
	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, AES_GMAC_HASH_LEN,
	    tag) != 1)
		errx(1, "OpenSSL %s (%zu) get tag failed: %s", alg->name,
		    size, ERR_error_string(ERR_get_error(), NULL));
	valid = (EVP_DecryptFinal_ex(ctx, (u_char *)output + outl, &outl) != 1);
	total += outl;
	if (total != size)
		errx(1, "OpenSSL %s (%zu) decrypt size mismatch: %d", alg->name,
		    size, total);
	EVP_CIPHER_CTX_free(ctx);
	return (valid);
}
#endif

static void
run_gcm_test(struct alg *alg, size_t size)
{
	const EVP_CIPHER *cipher;
	char *aad, *buffer, *cleartext, *ciphertext;
	char *iv, *key;
	u_int iv_len, key_len;
	int crid;
	char control_tag[AES_GMAC_HASH_LEN], test_tag[AES_GMAC_HASH_LEN];

	cipher = alg->evp_cipher();
	if (size % EVP_CIPHER_block_size(cipher) != 0) {
		if (verbose)
			printf(
			    "%s (%zu): invalid buffer size (block size %d)\n",
			    alg->name, size, EVP_CIPHER_block_size(cipher));
		return;
	}

	memset(control_tag, 0x3c, sizeof(control_tag));
	memset(test_tag, 0x3c, sizeof(test_tag));

	key_len = EVP_CIPHER_key_length(cipher);
	iv_len = EVP_CIPHER_iv_length(cipher);

	key = alloc_buffer(key_len);
	iv = generate_iv(iv_len, alg);
	cleartext = alloc_buffer(size);
	buffer = malloc(size);
	ciphertext = malloc(size);
	if (aad_len != 0)
		aad = alloc_buffer(aad_len);
	else
		aad = NULL;

	/* OpenSSL encrypt */
	openssl_gcm_encrypt(alg, cipher, key, iv, aad, aad_len, cleartext,
	    ciphertext, size, control_tag);

	/* OCF encrypt */
	if (!ocf_gcm(alg, key, key_len, iv, iv_len, aad, aad_len, cleartext,
	    buffer, size, test_tag, 1, &crid))
		goto out;
	if (memcmp(ciphertext, buffer, size) != 0) {
		printf("%s (%zu) encryption mismatch:\n", alg->name, size);
		printf("control:\n");
		hexdump(ciphertext, size, NULL, 0);
		printf("test (cryptodev device %s):\n", crfind(crid));
		hexdump(buffer, size, NULL, 0);
		goto out;
	}
	if (memcmp(control_tag, test_tag, sizeof(control_tag)) != 0) {
		printf("%s (%zu) enc tag mismatch:\n", alg->name, size);
		printf("control:\n");
		hexdump(control_tag, sizeof(control_tag), NULL, 0);
		printf("test (cryptodev device %s):\n", crfind(crid));
		hexdump(test_tag, sizeof(test_tag), NULL, 0);
		goto out;
	}

	/* OCF decrypt */
	if (!ocf_gcm(alg, key, key_len, iv, iv_len, aad, aad_len, ciphertext,
	    buffer, size, control_tag, 0, &crid))
		goto out;
	if (memcmp(cleartext, buffer, size) != 0) {
		printf("%s (%zu) decryption mismatch:\n", alg->name, size);
		printf("control:\n");
		hexdump(cleartext, size, NULL, 0);
		printf("test (cryptodev device %s):\n", crfind(crid));
		hexdump(buffer, size, NULL, 0);
		goto out;
	}

	if (verbose)
		printf("%s (%zu) matched (cryptodev device %s)\n",
		    alg->name, size, crfind(crid));

out:
	free(aad);
	free(ciphertext);
	free(buffer);
	free(cleartext);
	free(iv);
	free(key);
}

static void
openssl_ccm_encrypt(struct alg *alg, const EVP_CIPHER *cipher, const char *key,
    const char *iv, size_t iv_len, const char *aad, size_t aad_len,
		    const char *input, char *output, size_t size, char *tag)
{
	EVP_CIPHER_CTX *ctx;
	int outl, total;

	ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL)
		errx(1, "OpenSSL %s (%zu) ctx new failed: %s", alg->name,
		    size, ERR_error_string(ERR_get_error(), NULL));
	if (EVP_EncryptInit_ex(ctx, cipher, NULL, NULL, NULL) != 1)
		errx(1, "OpenSSL %s (%zu) ctx init failed: %s", alg->name,
		    size, ERR_error_string(ERR_get_error(), NULL));
	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_CCM_SET_IVLEN, iv_len, NULL) != 1)
		errx(1, "OpenSSL %s (%zu) setting iv length failed: %s", alg->name,
		    size, ERR_error_string(ERR_get_error(), NULL));
	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_CCM_SET_TAG, AES_CBC_MAC_HASH_LEN, NULL) != 1)
		errx(1, "OpenSSL %s (%zu) setting tag length failed: %s", alg->name,
		     size, ERR_error_string(ERR_get_error(), NULL));
	if (EVP_EncryptInit_ex(ctx, NULL, NULL, (const u_char *)key,
	    (const u_char *)iv) != 1)
		errx(1, "OpenSSL %s (%zu) ctx init failed: %s", alg->name,
		    size, ERR_error_string(ERR_get_error(), NULL));
	if (EVP_EncryptUpdate(ctx, NULL, &outl, NULL, size) != 1)
		errx(1, "OpenSSL %s (%zu) unable to set data length: %s", alg->name,
		     size, ERR_error_string(ERR_get_error(), NULL));

	if (aad != NULL) {
		if (EVP_EncryptUpdate(ctx, NULL, &outl, (const u_char *)aad,
		    aad_len) != 1)
			errx(1, "OpenSSL %s (%zu) aad update failed: %s",
			    alg->name, size,
			    ERR_error_string(ERR_get_error(), NULL));
	}
	if (EVP_EncryptUpdate(ctx, (u_char *)output, &outl,
	    (const u_char *)input, size) != 1)
		errx(1, "OpenSSL %s (%zu) encrypt update failed: %s", alg->name,
		    size, ERR_error_string(ERR_get_error(), NULL));
	total = outl;
	if (EVP_EncryptFinal_ex(ctx, (u_char *)output + outl, &outl) != 1)
		errx(1, "OpenSSL %s (%zu) encrypt final failed: %s", alg->name,
		    size, ERR_error_string(ERR_get_error(), NULL));
	total += outl;
	if (total != size)
		errx(1, "OpenSSL %s (%zu) encrypt size mismatch: %d", alg->name,
		    size, total);
	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_CCM_GET_TAG, AES_CBC_MAC_HASH_LEN,
	    tag) != 1)
		errx(1, "OpenSSL %s (%zu) get tag failed: %s", alg->name,
		    size, ERR_error_string(ERR_get_error(), NULL));
	EVP_CIPHER_CTX_free(ctx);
}

static bool
ocf_ccm(struct alg *alg, const char *key, size_t key_len, const char *iv,
    size_t iv_len, const char *aad, size_t aad_len, const char *input,
    char *output, size_t size, char *tag, int enc, int *cridp)
{
	struct session2_op sop;
	struct crypt_aead caead;
	int fd;
	bool rv;

	memset(&sop, 0, sizeof(sop));
	memset(&caead, 0, sizeof(caead));
	sop.crid = crid;
	sop.keylen = key_len;
	sop.key = (char *)key;
	sop.cipher = alg->cipher;
	sop.mackeylen = key_len;
	sop.mackey = (char *)key;
	sop.mac = alg->mac;
	fd = crget();
	if (ioctl(fd, CIOCGSESSION2, &sop) < 0) {
		warn("cryptodev %s not supported for device %s",
		    alg->name, crfind(crid));
		close(fd);
		return (false);
	}

	caead.ses = sop.ses;
	caead.op = enc ? COP_ENCRYPT : COP_DECRYPT;
	caead.len = size;
	caead.aadlen = aad_len;
	caead.ivlen = iv_len;
	caead.src = (char *)input;
	caead.dst = output;
	caead.aad = (char *)aad;
	caead.tag = tag;
	caead.iv = (char *)iv;

	if (ioctl(fd, CIOCCRYPTAEAD, &caead) < 0) {
		warn("cryptodev %s (%zu) failed for device %s",
		    alg->name, size, crfind(crid));
		rv = false;
	} else
		rv = true;

	if (ioctl(fd, CIOCFSESSION, &sop.ses) < 0)
		warn("ioctl(CIOCFSESSION)");

	close(fd);
	*cridp = sop.crid;
	return (rv);
}

static void
run_ccm_test(struct alg *alg, size_t size)
{
	const EVP_CIPHER *cipher;
	char *aad, *buffer, *cleartext, *ciphertext;
	char *iv, *key;
	u_int iv_len, key_len;
	int crid;
	char control_tag[AES_CBC_MAC_HASH_LEN], test_tag[AES_CBC_MAC_HASH_LEN];

	cipher = alg->evp_cipher();
	if (size % EVP_CIPHER_block_size(cipher) != 0) {
		if (verbose)
			printf(
			    "%s (%zu): invalid buffer size (block size %d)\n",
			    alg->name, size, EVP_CIPHER_block_size(cipher));
		return;
	}

	memset(control_tag, 0x3c, sizeof(control_tag));
	memset(test_tag, 0x3c, sizeof(test_tag));

	/*
	 * We only have one algorithm constant for CBC-MAC; however, the
	 * alg structure uses the different openssl types, which gives us
	 * the key length.  We need that for the OCF code.
	 */
	key_len = EVP_CIPHER_key_length(cipher);

	/*
	 * AES-CCM can have varying IV lengths; however, for the moment
	 * we only support AES_CCM_IV_LEN (12).  So if the sizes are
	 * different, we'll fail.
	 */
	iv_len = EVP_CIPHER_iv_length(cipher);
	if (iv_len != AES_CCM_IV_LEN) {
		if (verbose)
			printf("OpenSSL CCM IV length (%d) != AES_CCM_IV_LEN",
			    iv_len);
		return;
	} 

	key = alloc_buffer(key_len);
	iv = generate_iv(iv_len, alg);
	cleartext = alloc_buffer(size);
	buffer = malloc(size);
	ciphertext = malloc(size);
	if (aad_len != 0)
		aad = alloc_buffer(aad_len);
	else
		aad = NULL;

	/* OpenSSL encrypt */
	openssl_ccm_encrypt(alg, cipher, key, iv, iv_len, aad, aad_len, cleartext,
	    ciphertext, size, control_tag);

	/* OCF encrypt */
	if (!ocf_ccm(alg, key, key_len, iv, iv_len, aad, aad_len, cleartext,
	    buffer, size, test_tag, 1, &crid))
		goto out;
	if (memcmp(ciphertext, buffer, size) != 0) {
		printf("%s (%zu) encryption mismatch:\n", alg->name, size);
		printf("control:\n");
		hexdump(ciphertext, size, NULL, 0);
		printf("test (cryptodev device %s):\n", crfind(crid));
		hexdump(buffer, size, NULL, 0);
		goto out;
	}
	if (memcmp(control_tag, test_tag, sizeof(control_tag)) != 0) {
		printf("%s (%zu) enc tag mismatch:\n", alg->name, size);
		printf("control:\n");
		hexdump(control_tag, sizeof(control_tag), NULL, 0);
		printf("test (cryptodev device %s):\n", crfind(crid));
		hexdump(test_tag, sizeof(test_tag), NULL, 0);
		goto out;
	}

	/* OCF decrypt */
	if (!ocf_ccm(alg, key, key_len, iv, iv_len, aad, aad_len, ciphertext,
	    buffer, size, control_tag, 0, &crid))
		goto out;
	if (memcmp(cleartext, buffer, size) != 0) {
		printf("%s (%zu) decryption mismatch:\n", alg->name, size);
		printf("control:\n");
		hexdump(cleartext, size, NULL, 0);
		printf("test (cryptodev device %s):\n", crfind(crid));
		hexdump(buffer, size, NULL, 0);
		goto out;
	}

	if (verbose)
		printf("%s (%zu) matched (cryptodev device %s)\n",
		    alg->name, size, crfind(crid));

out:
	free(aad);
	free(ciphertext);
	free(buffer);
	free(cleartext);
	free(iv);
	free(key);
}

static void
run_test(struct alg *alg, size_t size)
{

	switch (alg->type) {
	case T_HASH:
		run_hash_test(alg, size);
		break;
	case T_HMAC:
		run_hmac_test(alg, size);
		break;
	case T_BLKCIPHER:
		run_blkcipher_test(alg, size);
		break;
	case T_AUTHENC:
		run_authenc_test(alg, size);
		break;
	case T_GCM:
		run_gcm_test(alg, size);
		break;
	case T_CCM:
		run_ccm_test(alg, size);
		break;
	}
}

static void
run_test_sizes(struct alg *alg, size_t *sizes, u_int nsizes)
{
	u_int i;

	for (i = 0; i < nsizes; i++)
		run_test(alg, sizes[i]);
}

static void
run_hash_tests(size_t *sizes, u_int nsizes)
{
	u_int i;

	for (i = 0; i < nitems(algs); i++)
		if (algs[i].type == T_HASH)
			run_test_sizes(&algs[i], sizes, nsizes);
}

static void
run_hmac_tests(size_t *sizes, u_int nsizes)
{
	u_int i;

	for (i = 0; i < nitems(algs); i++)
		if (algs[i].type == T_HMAC)
			run_test_sizes(&algs[i], sizes, nsizes);
}

static void
run_blkcipher_tests(size_t *sizes, u_int nsizes)
{
	u_int i;

	for (i = 0; i < nitems(algs); i++)
		if (algs[i].type == T_BLKCIPHER)
			run_test_sizes(&algs[i], sizes, nsizes);
}

static void
run_authenc_tests(size_t *sizes, u_int nsizes)
{
	struct alg *authenc, *cipher, *hmac;
	u_int i, j;

	for (i = 0; i < nitems(algs); i++) {
		cipher = &algs[i];
		if (cipher->type != T_BLKCIPHER)
			continue;
		for (j = 0; j < nitems(algs); j++) {
			hmac = &algs[j];
			if (hmac->type != T_HMAC)
				continue;
			authenc = build_authenc(cipher, hmac);
			run_test_sizes(authenc, sizes, nsizes);
			free((char *)authenc->name);
		}
	}
}

static void
run_aead_tests(size_t *sizes, u_int nsizes)
{
	u_int i;

	for (i = 0; i < nitems(algs); i++)
		if (algs[i].type == T_GCM ||
		    algs[i].type == T_CCM)
			run_test_sizes(&algs[i], sizes, nsizes);
}

int
main(int ac, char **av)
{
	const char *algname;
	struct alg *alg;
	size_t sizes[128];
	u_int i, nsizes;
	bool testall;
	int ch;

	algname = NULL;
	crid = CRYPTO_FLAG_HARDWARE;
	testall = false;
	verbose = false;
	while ((ch = getopt(ac, av, "A:a:d:vz")) != -1)
		switch (ch) {
		case 'A':
			aad_len = atoi(optarg);
			break;
		case 'a':
			algname = optarg;
			break;
		case 'd':
			crid = crlookup(optarg);
			break;
		case 'v':
			verbose = true;
			break;
		case 'z':
			testall = true;
			break;
		default:
			usage();
		}
	ac -= optind;
	av += optind;
	nsizes = 0;
	while (ac > 0) {
		char *cp;

		if (nsizes >= nitems(sizes)) {
			warnx("Too many sizes, ignoring extras");
			break;
		}
		sizes[nsizes] = strtol(av[0], &cp, 0);
		if (*cp != '\0')
			errx(1, "Bad size %s", av[0]);
		nsizes++;
		ac--;
		av++;
	}

	if (algname == NULL)
		errx(1, "Algorithm required");
	if (nsizes == 0) {
		sizes[0] = 16;
		nsizes++;
		if (testall) {
			while (sizes[nsizes - 1] * 2 < 240 * 1024) {
				assert(nsizes < nitems(sizes));
				sizes[nsizes] = sizes[nsizes - 1] * 2;
				nsizes++;
			}
			if (sizes[nsizes - 1] < 240 * 1024) {
				assert(nsizes < nitems(sizes));
				sizes[nsizes] = 240 * 1024;
				nsizes++;
			}
		}
	}

	if (strcasecmp(algname, "hash") == 0)
		run_hash_tests(sizes, nsizes);
	else if (strcasecmp(algname, "hmac") == 0)
		run_hmac_tests(sizes, nsizes);
	else if (strcasecmp(algname, "blkcipher") == 0)
		run_blkcipher_tests(sizes, nsizes);
	else if (strcasecmp(algname, "authenc") == 0)
		run_authenc_tests(sizes, nsizes);
	else if (strcasecmp(algname, "aead") == 0)
		run_aead_tests(sizes, nsizes);
	else if (strcasecmp(algname, "all") == 0) {
		run_hash_tests(sizes, nsizes);
		run_hmac_tests(sizes, nsizes);
		run_blkcipher_tests(sizes, nsizes);
		run_authenc_tests(sizes, nsizes);
		run_aead_tests(sizes, nsizes);
	} else if (strchr(algname, '+') != NULL) {
		alg = build_authenc_name(algname);
		run_test_sizes(alg, sizes, nsizes);
	} else {
		alg = find_alg(algname);
		if (alg == NULL)
			errx(1, "Invalid algorithm %s", algname);
		run_test_sizes(alg, sizes, nsizes);
	}

	return (0);
}
