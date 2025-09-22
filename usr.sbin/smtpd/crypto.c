/* $OpenBSD: crypto.c,v 1.10 2021/06/14 17:58:15 eric Exp $	 */

/*
 * Copyright (c) 2013 Gilles Chehade <gilles@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/stat.h>

#include <openssl/evp.h>
#include <string.h>

#define	CRYPTO_BUFFER_SIZE	16384

#define	GCM_TAG_SIZE		16
#define	IV_SIZE			12
#define	KEY_SIZE		32

/* bump if we ever switch from aes-256-gcm to anything else */
#define	API_VERSION    		1


int	crypto_setup(const char *, size_t);
int	crypto_encrypt_file(FILE *, FILE *);
int	crypto_decrypt_file(FILE *, FILE *);
size_t	crypto_encrypt_buffer(const char *, size_t, char *, size_t);
size_t	crypto_decrypt_buffer(const char *, size_t, char *, size_t);

static struct crypto_ctx {
	unsigned char  		key[KEY_SIZE];
} cp;

int
crypto_setup(const char *key, size_t len)
{
	if (len != KEY_SIZE)
		return 0;

	memset(&cp, 0, sizeof cp);

	/* openssl rand -hex 16 */
	memcpy(cp.key, key, sizeof cp.key);

	return 1;
}

int
crypto_encrypt_file(FILE * in, FILE * out)
{
	EVP_CIPHER_CTX	*ctx;
	uint8_t		ibuf[CRYPTO_BUFFER_SIZE];
	uint8_t		obuf[CRYPTO_BUFFER_SIZE];
	uint8_t		iv[IV_SIZE];
	uint8_t		tag[GCM_TAG_SIZE];
	uint8_t		version = API_VERSION;
	size_t		r;
	int		len;
	int		ret = 0;
	struct stat	sb;

	/* XXX - Do NOT encrypt files bigger than 64GB */
	if (fstat(fileno(in), &sb) == -1)
		return 0;
	if (sb.st_size >= 0x1000000000LL)
		return 0;

	/* prepend version byte*/
	if (fwrite(&version, 1, sizeof version, out) != sizeof version)
		return 0;

	/* generate and prepend IV */
	memset(iv, 0, sizeof iv);
	arc4random_buf(iv, sizeof iv);
	if (fwrite(iv, 1, sizeof iv, out) != sizeof iv)
		return 0;

	ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL)
		return 0;

	EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, cp.key, iv);

	/* encrypt until end of file */
	while ((r = fread(ibuf, 1, CRYPTO_BUFFER_SIZE, in)) != 0) {
		if (!EVP_EncryptUpdate(ctx, obuf, &len, ibuf, r))
			goto end;
		if (len && fwrite(obuf, len, 1, out) != 1)
			goto end;
	}
	if (!feof(in))
		goto end;

	/* finalize and write last chunk if any */
	if (!EVP_EncryptFinal_ex(ctx, obuf, &len))
		goto end;
	if (len && fwrite(obuf, len, 1, out) != 1)
		goto end;

	/* get and append tag */
	EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, sizeof tag, tag);
	if (fwrite(tag, sizeof tag, 1, out) != 1)
		goto end;

	fflush(out);
	ret = 1;

end:
	EVP_CIPHER_CTX_free(ctx);
	return ret;
}

int
crypto_decrypt_file(FILE * in, FILE * out)
{
	EVP_CIPHER_CTX	*ctx;
	uint8_t		ibuf[CRYPTO_BUFFER_SIZE];
	uint8_t		obuf[CRYPTO_BUFFER_SIZE];
	uint8_t		iv[IV_SIZE];
	uint8_t		tag[GCM_TAG_SIZE];
	uint8_t		version;
	size_t		r;
	off_t		sz;
	int		len;
	int		ret = 0;
	struct stat	sb;

	/* input file too small to be an encrypted file */
	if (fstat(fileno(in), &sb) == -1)
		return 0;
	if (sb.st_size <= (off_t) (sizeof version + sizeof tag + sizeof iv))
		return 0;
	sz = sb.st_size;

	/* extract tag */
	if (fseek(in, -sizeof(tag), SEEK_END) == -1)
		return 0;
	if ((r = fread(tag, 1, sizeof tag, in)) != sizeof tag)
		return 0;

	if (fseek(in, 0, SEEK_SET) == -1)
		return 0;

	/* extract version */
	if ((r = fread(&version, 1, sizeof version, in)) != sizeof version)
		return 0;
	if (version != API_VERSION)
		return 0;

	/* extract IV */
	memset(iv, 0, sizeof iv);
	if ((r = fread(iv, 1, sizeof iv, in)) != sizeof iv)
		return 0;

	/* real ciphertext length */
	sz -= sizeof version;
	sz -= sizeof iv;
	sz -= sizeof tag;

	ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL)
		return 0;

	EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, cp.key, iv);

	/* set expected tag */
	EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, sizeof tag, tag);

	/* decrypt until end of ciphertext */
	while (sz) {
		if (sz > CRYPTO_BUFFER_SIZE)
			r = fread(ibuf, 1, CRYPTO_BUFFER_SIZE, in);
		else
			r = fread(ibuf, 1, sz, in);
		if (!r)
			break;
		if (!EVP_DecryptUpdate(ctx, obuf, &len, ibuf, r))
			goto end;
		if (len && fwrite(obuf, len, 1, out) != 1)
			goto end;
		sz -= r;
	}
	if (ferror(in))
		goto end;

	/* finalize, write last chunk if any and perform authentication check */
	if (!EVP_DecryptFinal_ex(ctx, obuf, &len))
		goto end;
	if (len && fwrite(obuf, len, 1, out) != 1)
		goto end;

	fflush(out);
	ret = 1;

end:
	EVP_CIPHER_CTX_free(ctx);
	return ret;
}

size_t
crypto_encrypt_buffer(const char *in, size_t inlen, char *out, size_t outlen)
{
	EVP_CIPHER_CTX	*ctx;
	uint8_t		iv[IV_SIZE];
	uint8_t		tag[GCM_TAG_SIZE];
	uint8_t		version = API_VERSION;
	off_t		sz;
	int		olen;
	int		len = 0;
	int		ret = 0;

	/* output buffer does not have enough room */
	if (outlen < inlen + sizeof version + sizeof tag + sizeof iv)
		return 0;

	/* input should not exceed 64GB */
	sz = inlen;
	if (sz >= 0x1000000000LL)
		return 0;

	/* prepend version */
	*out = version;
	len++;

	/* generate IV */
	memset(iv, 0, sizeof iv);
	arc4random_buf(iv, sizeof iv);
	memcpy(out + len, iv, sizeof iv);
	len += sizeof iv;

	ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL)
		return 0;

	EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, cp.key, iv);

	/* encrypt buffer */
	if (!EVP_EncryptUpdate(ctx, out + len, &olen, in, inlen))
		goto end;
	len += olen;

	/* finalize and write last chunk if any */
	if (!EVP_EncryptFinal_ex(ctx, out + len, &olen))
		goto end;
	len += olen;

	/* get and append tag */
	EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, sizeof tag, tag);
	memcpy(out + len, tag, sizeof tag);
	ret = len + sizeof tag;

end:
	EVP_CIPHER_CTX_free(ctx);
	return ret;
}

size_t
crypto_decrypt_buffer(const char *in, size_t inlen, char *out, size_t outlen)
{
	EVP_CIPHER_CTX	*ctx;
	uint8_t		iv[IV_SIZE];
	uint8_t		tag[GCM_TAG_SIZE];
	int		olen;
	int		len = 0;
	int		ret = 0;

	/* out does not have enough room */
	if (outlen < inlen - sizeof tag + sizeof iv)
		return 0;

	/* extract tag */
	memcpy(tag, in + inlen - sizeof tag, sizeof tag);
	inlen -= sizeof tag;

	/* check version */
	if (*in != API_VERSION)
		return 0;
	in++;
	inlen--;

	/* extract IV */
	memset(iv, 0, sizeof iv);
	memcpy(iv, in, sizeof iv);
	inlen -= sizeof iv;
	in += sizeof iv;

	ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL)
		return 0;

	EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, cp.key, iv);

	/* set expected tag */
	EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, sizeof tag, tag);

	/* decrypt buffer */
	if (!EVP_DecryptUpdate(ctx, out, &olen, in, inlen))
		goto end;
	len += olen;

	/* finalize, write last chunk if any and perform authentication check */
	if (!EVP_DecryptFinal_ex(ctx, out + len, &olen))
		goto end;
	ret = len + olen;

end:
	EVP_CIPHER_CTX_free(ctx);
	return ret;
}

#if 0
int
main(int argc, char *argv[])
{
	if (argc != 3) {
		printf("usage: crypto <key> <buffer>\n");
		return 1;
	}

	if (!crypto_setup(argv[1], strlen(argv[1]))) {
		printf("crypto_setup failed\n");
		return 1;
	}

	{
		char            encbuffer[4096];
		size_t          enclen;
		char            decbuffer[4096];
		size_t          declen;

		printf("encrypt/decrypt buffer: ");
		enclen = crypto_encrypt_buffer(argv[2], strlen(argv[2]),
					       encbuffer, sizeof encbuffer);

		/* uncomment below to provoke integrity check failure */
		/*
		 * encbuffer[13] = 0x42;
		 * encbuffer[14] = 0x42;
		 * encbuffer[15] = 0x42;
		 * encbuffer[16] = 0x42;
		 */

		declen = crypto_decrypt_buffer(encbuffer, enclen,
					       decbuffer, sizeof decbuffer);
		if (declen != 0 && !strncmp(argv[2], decbuffer, declen))
			printf("ok\n");
		else
			printf("nope\n");
	}

	{
		FILE           *fpin;
		FILE           *fpout;
		printf("encrypt/decrypt file: ");

		fpin = fopen("/etc/passwd", "r");
		fpout = fopen("/tmp/passwd.enc", "w");
		if (!crypto_encrypt_file(fpin, fpout)) {
			printf("encryption failed\n");
			return 1;
		}
		fclose(fpin);
		fclose(fpout);

		/* uncomment below to provoke integrity check failure */
		/*
		 * fpin = fopen("/tmp/passwd.enc", "a");
		 * fprintf(fpin, "borken");
		 * fclose(fpin);
		 */
		fpin = fopen("/tmp/passwd.enc", "r");
		fpout = fopen("/tmp/passwd.dec", "w");
		if (!crypto_decrypt_file(fpin, fpout))
			printf("nope\n");
		else
			printf("ok\n");
		fclose(fpin);
		fclose(fpout);
	}


	return 0;
}
#endif
