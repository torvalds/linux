/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <resolv.h>
#include <md5.h>

#include "ctld.h"

static void
chap_compute_md5(const char id, const char *secret,
    const void *challenge, size_t challenge_len, void *response,
    size_t response_len)
{
	MD5_CTX ctx;

	assert(response_len == CHAP_DIGEST_LEN);

	MD5Init(&ctx);
	MD5Update(&ctx, &id, sizeof(id));
	MD5Update(&ctx, secret, strlen(secret));
	MD5Update(&ctx, challenge, challenge_len);
	MD5Final(response, &ctx);
}

static int
chap_hex2int(const char hex)
{
	switch (hex) {
	case '0':
		return (0x00);
	case '1':
		return (0x01);
	case '2':
		return (0x02);
	case '3':
		return (0x03);
	case '4':
		return (0x04);
	case '5':
		return (0x05);
	case '6':
		return (0x06);
	case '7':
		return (0x07);
	case '8':
		return (0x08);
	case '9':
		return (0x09);
	case 'a':
	case 'A':
		return (0x0a);
	case 'b':
	case 'B':
		return (0x0b);
	case 'c':
	case 'C':
		return (0x0c);
	case 'd':
	case 'D':
		return (0x0d);
	case 'e':
	case 'E':
		return (0x0e);
	case 'f':
	case 'F':
		return (0x0f);
	default:
		return (-1);
	}
}

static int
chap_b642bin(const char *b64, void **binp, size_t *bin_lenp)
{
	char *bin;
	int b64_len, bin_len;

	b64_len = strlen(b64);
	bin_len = (b64_len + 3) / 4 * 3;
	bin = calloc(bin_len, 1);
	if (bin == NULL)
		log_err(1, "calloc");

	bin_len = b64_pton(b64, bin, bin_len);
	if (bin_len < 0) {
		log_warnx("malformed base64 variable");
		free(bin);
		return (-1);
	}
	*binp = bin;
	*bin_lenp = bin_len;
	return (0);
}

/*
 * XXX: Review this _carefully_.
 */
static int
chap_hex2bin(const char *hex, void **binp, size_t *bin_lenp)
{
	int i, hex_len, nibble;
	bool lo = true; /* As opposed to 'hi'. */
	char *bin;
	size_t bin_off, bin_len;

	if (strncasecmp(hex, "0b", strlen("0b")) == 0)
		return (chap_b642bin(hex + 2, binp, bin_lenp));

	if (strncasecmp(hex, "0x", strlen("0x")) != 0) {
		log_warnx("malformed variable, should start with \"0x\""
		    " or \"0b\"");
		return (-1);
	}

	hex += strlen("0x");
	hex_len = strlen(hex);
	if (hex_len < 1) {
		log_warnx("malformed variable; doesn't contain anything "
		    "but \"0x\"");
		return (-1);
	}

	bin_len = hex_len / 2 + hex_len % 2;
	bin = calloc(bin_len, 1);
	if (bin == NULL)
		log_err(1, "calloc");

	bin_off = bin_len - 1;
	for (i = hex_len - 1; i >= 0; i--) {
		nibble = chap_hex2int(hex[i]);
		if (nibble < 0) {
			log_warnx("malformed variable, invalid char \"%c\"",
			    hex[i]);
			free(bin);
			return (-1);
		}

		assert(bin_off < bin_len);
		if (lo) {
			bin[bin_off] = nibble;
			lo = false;
		} else {
			bin[bin_off] |= nibble << 4;
			bin_off--;
			lo = true;
		}
	}

	*binp = bin;
	*bin_lenp = bin_len;
	return (0);
}

#ifdef USE_BASE64
static char *
chap_bin2hex(const char *bin, size_t bin_len)
{
	unsigned char *b64, *tmp;
	size_t b64_len;

	b64_len = (bin_len + 2) / 3 * 4 + 3; /* +2 for "0b", +1 for '\0'. */
	b64 = malloc(b64_len);
	if (b64 == NULL)
		log_err(1, "malloc");

	tmp = b64;
	tmp += sprintf(tmp, "0b");
	b64_ntop(bin, bin_len, tmp, b64_len - 2);

	return (b64);
}
#else
static char *
chap_bin2hex(const char *bin, size_t bin_len)
{
	unsigned char *hex, *tmp, ch;
	size_t hex_len;
	size_t i;

	hex_len = bin_len * 2 + 3; /* +2 for "0x", +1 for '\0'. */
	hex = malloc(hex_len);
	if (hex == NULL)
		log_err(1, "malloc");

	tmp = hex;
	tmp += sprintf(tmp, "0x");
	for (i = 0; i < bin_len; i++) {
		ch = bin[i];
		tmp += sprintf(tmp, "%02x", ch);
	}

	return (hex);
}
#endif /* !USE_BASE64 */

struct chap *
chap_new(void)
{
	struct chap *chap;

	chap = calloc(1, sizeof(*chap));
	if (chap == NULL)
		log_err(1, "calloc");

	/*
	 * Generate the challenge.
	 */
	arc4random_buf(chap->chap_challenge, sizeof(chap->chap_challenge));
	arc4random_buf(&chap->chap_id, sizeof(chap->chap_id));

	return (chap);
}

char *
chap_get_id(const struct chap *chap)
{
	char *chap_i;
	int ret;

	ret = asprintf(&chap_i, "%d", chap->chap_id);
	if (ret < 0)
		log_err(1, "asprintf");

	return (chap_i);
}

char *
chap_get_challenge(const struct chap *chap)
{
	char *chap_c;

	chap_c = chap_bin2hex(chap->chap_challenge,
	    sizeof(chap->chap_challenge));

	return (chap_c);
}

static int
chap_receive_bin(struct chap *chap, void *response, size_t response_len)
{

	if (response_len != sizeof(chap->chap_response)) {
		log_debugx("got CHAP response with invalid length; "
		    "got %zd, should be %zd",
		    response_len, sizeof(chap->chap_response));
		return (1);
	}

	memcpy(chap->chap_response, response, response_len);
	return (0);
}

int
chap_receive(struct chap *chap, const char *response)
{
	void *response_bin;
	size_t response_bin_len;
	int error;

	error = chap_hex2bin(response, &response_bin, &response_bin_len);
	if (error != 0) {
		log_debugx("got incorrectly encoded CHAP response \"%s\"",
		    response);
		return (1);
	}

	error = chap_receive_bin(chap, response_bin, response_bin_len);
	free(response_bin);

	return (error);
}

int
chap_authenticate(struct chap *chap, const char *secret)
{
	char expected_response[CHAP_DIGEST_LEN];

	chap_compute_md5(chap->chap_id, secret,
	    chap->chap_challenge, sizeof(chap->chap_challenge),
	    expected_response, sizeof(expected_response));

	if (memcmp(chap->chap_response,
	    expected_response, sizeof(expected_response)) != 0) {
		return (-1);
	}

	return (0);
}

void
chap_delete(struct chap *chap)
{

	free(chap);
}

struct rchap *
rchap_new(const char *secret)
{
	struct rchap *rchap;

	rchap = calloc(1, sizeof(*rchap));
	if (rchap == NULL)
		log_err(1, "calloc");

	rchap->rchap_secret = checked_strdup(secret);

	return (rchap);
}

static void
rchap_receive_bin(struct rchap *rchap, const unsigned char id,
    const void *challenge, size_t challenge_len)
{

	rchap->rchap_id = id;
	rchap->rchap_challenge = calloc(challenge_len, 1);
	if (rchap->rchap_challenge == NULL)
		log_err(1, "calloc");
	memcpy(rchap->rchap_challenge, challenge, challenge_len);
	rchap->rchap_challenge_len = challenge_len;
}

int
rchap_receive(struct rchap *rchap, const char *id, const char *challenge)
{
	unsigned char id_bin;
	void *challenge_bin;
	size_t challenge_bin_len;

	int error;

	id_bin = strtoul(id, NULL, 10);

	error = chap_hex2bin(challenge, &challenge_bin, &challenge_bin_len);
	if (error != 0) {
		log_debugx("got incorrectly encoded CHAP challenge \"%s\"",
		    challenge);
		return (1);
	}

	rchap_receive_bin(rchap, id_bin, challenge_bin, challenge_bin_len);
	free(challenge_bin);

	return (0);
}

static void
rchap_get_response_bin(struct rchap *rchap,
    void **responsep, size_t *response_lenp)
{
	void *response_bin;
	size_t response_bin_len = CHAP_DIGEST_LEN;

	response_bin = calloc(response_bin_len, 1);
	if (response_bin == NULL)
		log_err(1, "calloc");

	chap_compute_md5(rchap->rchap_id, rchap->rchap_secret,
	    rchap->rchap_challenge, rchap->rchap_challenge_len,
	    response_bin, response_bin_len);

	*responsep = response_bin;
	*response_lenp = response_bin_len;
}

char *
rchap_get_response(struct rchap *rchap)
{
	void *response;
	size_t response_len;
	char *chap_r;

	rchap_get_response_bin(rchap, &response, &response_len);
	chap_r = chap_bin2hex(response, response_len);
	free(response);

	return (chap_r);
}

void
rchap_delete(struct rchap *rchap)
{

	free(rchap->rchap_secret);
	free(rchap->rchap_challenge);
	free(rchap);
}
