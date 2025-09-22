/* $OpenBSD: evp_key.c,v 1.37 2025/05/10 05:54:38 tb Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
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
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#include <stdio.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/ui.h>
#include <openssl/x509.h>

#include "err_local.h"
#include "evp_local.h"

/* should be init to zeros. */
static char prompt_string[80];

void
EVP_set_pw_prompt(const char *prompt)
{
	if (prompt == NULL)
		prompt_string[0] = '\0';
	else
		strlcpy(prompt_string, prompt, sizeof(prompt_string));
}
LCRYPTO_ALIAS(EVP_set_pw_prompt);

char *
EVP_get_pw_prompt(void)
{
	if (prompt_string[0] == '\0')
		return NULL;

	return prompt_string;
}
LCRYPTO_ALIAS(EVP_get_pw_prompt);

int
EVP_read_pw_string(char *buf, int len, const char *prompt, int verify)
{
	return EVP_read_pw_string_min(buf, 0, len, prompt, verify);
}
LCRYPTO_ALIAS(EVP_read_pw_string);

int
EVP_read_pw_string_min(char *buf, int min, int len, const char *prompt,
    int verify)
{
	UI *ui = NULL;
	char buff[BUFSIZ];
	int ret = -1;

	if (len > BUFSIZ)
		len = BUFSIZ;
	/* Ensure that 0 <= min <= len - 1. In particular, 1 <= len. */
	if (min < 0 || len - 1 < min)
		goto err;

	if (prompt == NULL && prompt_string[0] != '\0')
		prompt = prompt_string;

	if ((ui = UI_new()) == NULL)
		goto err;
	if (UI_add_input_string(ui, prompt, 0, buf, min, len - 1) < 0)
		goto err;
	if (verify) {
		if (UI_add_verify_string(ui, prompt, 0, buff, min, len - 1,
		    buf) < 0)
			goto err;
	}

	ret = UI_process(ui);

 err:
	UI_free(ui);
	explicit_bzero(buff, BUFSIZ);

	return ret;
}
LCRYPTO_ALIAS(EVP_read_pw_string_min);

int
EVP_BytesToKey(const EVP_CIPHER *type, const EVP_MD *md,
    const unsigned char *salt, const unsigned char *data, int datal,
    int count, unsigned char *key, unsigned char *iv)
{
	EVP_MD_CTX *md_ctx;
	unsigned char md_buf[EVP_MAX_MD_SIZE];
	int niv, nkey, addmd = 0;
	unsigned int mds = 0, i;
	int rv = 0;

	nkey = type->key_len;
	niv = type->iv_len;

	if ((size_t)nkey > EVP_MAX_KEY_LENGTH) {
		EVPerror(EVP_R_BAD_KEY_LENGTH);
		return 0;
	}
	if ((size_t)niv > EVP_MAX_IV_LENGTH) {
		EVPerror(EVP_R_IV_TOO_LARGE);
		return 0;
	}

	if (data == NULL)
		return nkey;

	if ((md_ctx = EVP_MD_CTX_new()) == NULL)
		goto err;

	for (;;) {
		if (!EVP_DigestInit_ex(md_ctx, md, NULL))
			goto err;
		if (addmd++)
			if (!EVP_DigestUpdate(md_ctx, &(md_buf[0]), mds))
				goto err;
		if (!EVP_DigestUpdate(md_ctx, data, datal))
			goto err;
		if (salt != NULL)
			if (!EVP_DigestUpdate(md_ctx, salt, PKCS5_SALT_LEN))
				goto err;
		if (!EVP_DigestFinal_ex(md_ctx, &(md_buf[0]), &mds))
			goto err;

		for (i = 1; i < (unsigned int)count; i++) {
			if (!EVP_DigestInit_ex(md_ctx, md, NULL))
				goto err;
			if (!EVP_DigestUpdate(md_ctx, &(md_buf[0]), mds))
				goto err;
			if (!EVP_DigestFinal_ex(md_ctx, &(md_buf[0]), &mds))
				goto err;
		}
		i = 0;
		if (nkey) {
			for (;;) {
				if (nkey == 0)
					break;
				if (i == mds)
					break;
				if (key != NULL)
					*(key++) = md_buf[i];
				nkey--;
				i++;
			}
		}
		if (niv && (i != mds)) {
			for (;;) {
				if (niv == 0)
					break;
				if (i == mds)
					break;
				if (iv != NULL)
					*(iv++) = md_buf[i];
				niv--;
				i++;
			}
		}
		if ((nkey == 0) && (niv == 0))
			break;
	}
	rv = type->key_len;

err:
	EVP_MD_CTX_free(md_ctx);
	explicit_bzero(md_buf, sizeof md_buf);
	return rv;
}
LCRYPTO_ALIAS(EVP_BytesToKey);
