/*-
 * Copyright (c) 2018 Conrad Meyer <cem@FreeBSD.org>
 * All rights reserved.
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
 * $FreeBSD$
 */

#include <sys/param.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include <atf-c.h>

/* Be sure to include tree copy rather than system copy. */
#include "cryptodev.h"

#include "freebsd_test_suite/macros.h"

struct poly1305_kat {
	const char *vector_name;
	const char *test_key_hex;
	const char *test_msg_hex;
	const size_t test_msg_len;

	const char *expected_tag_hex;
};

static const struct poly1305_kat rfc7539_kats[] = {
{
	.vector_name = "RFC 7539 \xc2\xa7 2.5.2",
	.test_key_hex = "85:d6:be:78:57:55:6d:33:7f:44:52:fe:42:d5:06:a8"
	    ":01:03:80:8a:fb:0d:b2:fd:4a:bf:f6:af:41:49:f5:1b",
	.test_msg_hex =
"43 72 79 70 74 6f 67 72 61 70 68 69 63 20 46 6f "
"72 75 6d 20 52 65 73 65 61 72 63 68 20 47 72 6f "
"75 70",
	.test_msg_len = 34,
	.expected_tag_hex = "a8:06:1d:c1:30:51:36:c6:c2:2b:8b:af:0c:01:27:a9",
},
{
	.vector_name = "RFC 7539 \xc2\xa7 A.3 #1",
	.test_key_hex =
"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ",
	.test_msg_hex =
"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ",
	.test_msg_len = 64,
	.expected_tag_hex = "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00",
},
{
	.vector_name = "RFC 7539 \xc2\xa7 A.3 #2",
	.test_key_hex =
"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
"36 e5 f6 b5 c5 e0 60 70 f0 ef ca 96 22 7a 86 3e ",
	.test_msg_hex =
"41 6e 79 20 73 75 62 6d 69 73 73 69 6f 6e 20 74 "
"6f 20 74 68 65 20 49 45 54 46 20 69 6e 74 65 6e "
"64 65 64 20 62 79 20 74 68 65 20 43 6f 6e 74 72 "
"69 62 75 74 6f 72 20 66 6f 72 20 70 75 62 6c 69 "
"63 61 74 69 6f 6e 20 61 73 20 61 6c 6c 20 6f 72 "
"20 70 61 72 74 20 6f 66 20 61 6e 20 49 45 54 46 "
"20 49 6e 74 65 72 6e 65 74 2d 44 72 61 66 74 20 "
"6f 72 20 52 46 43 20 61 6e 64 20 61 6e 79 20 73 "
"74 61 74 65 6d 65 6e 74 20 6d 61 64 65 20 77 69 "
"74 68 69 6e 20 74 68 65 20 63 6f 6e 74 65 78 74 "
"20 6f 66 20 61 6e 20 49 45 54 46 20 61 63 74 69 "
"76 69 74 79 20 69 73 20 63 6f 6e 73 69 64 65 72 "
"65 64 20 61 6e 20 22 49 45 54 46 20 43 6f 6e 74 "
"72 69 62 75 74 69 6f 6e 22 2e 20 53 75 63 68 20 "
"73 74 61 74 65 6d 65 6e 74 73 20 69 6e 63 6c 75 "
"64 65 20 6f 72 61 6c 20 73 74 61 74 65 6d 65 6e "
"74 73 20 69 6e 20 49 45 54 46 20 73 65 73 73 69 "
"6f 6e 73 2c 20 61 73 20 77 65 6c 6c 20 61 73 20 "
"77 72 69 74 74 65 6e 20 61 6e 64 20 65 6c 65 63 "
"74 72 6f 6e 69 63 20 63 6f 6d 6d 75 6e 69 63 61 "
"74 69 6f 6e 73 20 6d 61 64 65 20 61 74 20 61 6e "
"79 20 74 69 6d 65 20 6f 72 20 70 6c 61 63 65 2c "
"20 77 68 69 63 68 20 61 72 65 20 61 64 64 72 65 "
"73 73 65 64 20 74 6f",
	.test_msg_len = 375,
	.expected_tag_hex = "36 e5 f6 b5 c5 e0 60 70 f0 ef ca 96 22 7a 86 3e",
},
{
	.vector_name = "RFC 7539 \xc2\xa7 A.3 #3",
	.test_key_hex =
"36 e5 f6 b5 c5 e0 60 70 f0 ef ca 96 22 7a 86 3e "
"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ",
	.test_msg_hex =
"41 6e 79 20 73 75 62 6d 69 73 73 69 6f 6e 20 74 "
"6f 20 74 68 65 20 49 45 54 46 20 69 6e 74 65 6e "
"64 65 64 20 62 79 20 74 68 65 20 43 6f 6e 74 72 "
"69 62 75 74 6f 72 20 66 6f 72 20 70 75 62 6c 69 "
"63 61 74 69 6f 6e 20 61 73 20 61 6c 6c 20 6f 72 "
"20 70 61 72 74 20 6f 66 20 61 6e 20 49 45 54 46 "
"20 49 6e 74 65 72 6e 65 74 2d 44 72 61 66 74 20 "
"6f 72 20 52 46 43 20 61 6e 64 20 61 6e 79 20 73 "
"74 61 74 65 6d 65 6e 74 20 6d 61 64 65 20 77 69 "
"74 68 69 6e 20 74 68 65 20 63 6f 6e 74 65 78 74 "
"20 6f 66 20 61 6e 20 49 45 54 46 20 61 63 74 69 "
"76 69 74 79 20 69 73 20 63 6f 6e 73 69 64 65 72 "
"65 64 20 61 6e 20 22 49 45 54 46 20 43 6f 6e 74 "
"72 69 62 75 74 69 6f 6e 22 2e 20 53 75 63 68 20 "
"73 74 61 74 65 6d 65 6e 74 73 20 69 6e 63 6c 75 "
"64 65 20 6f 72 61 6c 20 73 74 61 74 65 6d 65 6e "
"74 73 20 69 6e 20 49 45 54 46 20 73 65 73 73 69 "
"6f 6e 73 2c 20 61 73 20 77 65 6c 6c 20 61 73 20 "
"77 72 69 74 74 65 6e 20 61 6e 64 20 65 6c 65 63 "
"74 72 6f 6e 69 63 20 63 6f 6d 6d 75 6e 69 63 61 "
"74 69 6f 6e 73 20 6d 61 64 65 20 61 74 20 61 6e "
"79 20 74 69 6d 65 20 6f 72 20 70 6c 61 63 65 2c "
"20 77 68 69 63 68 20 61 72 65 20 61 64 64 72 65 "
"73 73 65 64 20 74 6f",
	.test_msg_len = 375,
	.expected_tag_hex = "f3 47 7e 7c d9 54 17 af 89 a6 b8 79 4c 31 0c f0",
},
{
	.vector_name = "RFC 7539 \xc2\xa7 A.3 #4",
	.test_key_hex =
"1c 92 40 a5 eb 55 d3 8a f3 33 88 86 04 f6 b5 f0 "
"47 39 17 c1 40 2b 80 09 9d ca 5c bc 20 70 75 c0 ",
	.test_msg_hex =
"27 54 77 61 73 20 62 72 69 6c 6c 69 67 2c 20 61 "
"6e 64 20 74 68 65 20 73 6c 69 74 68 79 20 74 6f "
"76 65 73 0a 44 69 64 20 67 79 72 65 20 61 6e 64 "
"20 67 69 6d 62 6c 65 20 69 6e 20 74 68 65 20 77 "
"61 62 65 3a 0a 41 6c 6c 20 6d 69 6d 73 79 20 77 "
"65 72 65 20 74 68 65 20 62 6f 72 6f 67 6f 76 65 "
"73 2c 0a 41 6e 64 20 74 68 65 20 6d 6f 6d 65 20 "
"72 61 74 68 73 20 6f 75 74 67 72 61 62 65 2e",
	.test_msg_len = 127,
	.expected_tag_hex = "45 41 66 9a 7e aa ee 61 e7 08 dc 7c bc c5 eb 62",
},
{
	.vector_name = "RFC 7539 \xc2\xa7 A.3 #5",
	.test_key_hex =
"02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ",
	.test_msg_hex =
"FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF",
	.test_msg_len = 16,
	.expected_tag_hex = "03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00",
},
{
	.vector_name = "RFC 7539 \xc2\xa7 A.3 #6",
	.test_key_hex =
"02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
"FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF ",
	.test_msg_hex =
"02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00",
	.test_msg_len = 16,
	.expected_tag_hex = "03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00",

},
{
	.vector_name = "RFC 7539 \xc2\xa7 A.3 #7",
	.test_key_hex =
"01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ",
	.test_msg_hex =
"FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF "
"F0 FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF "
"11 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00",
	.test_msg_len = 48,
	.expected_tag_hex = "05 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00",
},
{
	.vector_name = "RFC 7539 \xc2\xa7 A.3 #8",
	.test_key_hex =
"01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ",
	.test_msg_hex =
"FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF "
"FB FE FE FE FE FE FE FE FE FE FE FE FE FE FE FE "
"01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01",
	.test_msg_len = 48,
	.expected_tag_hex = "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00",
},
{
	.vector_name = "RFC 7539 \xc2\xa7 A.3 #9",
	.test_key_hex =
"02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ",
	.test_msg_hex =
"FD FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF",
	.test_msg_len = 16,
	.expected_tag_hex = "FA FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF",
},
{
	.vector_name = "RFC 7539 \xc2\xa7 A.3 #10",
	.test_key_hex =
"01 00 00 00 00 00 00 00 04 00 00 00 00 00 00 00 "
"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ",
	.test_msg_hex =
"E3 35 94 D7 50 5E 43 B9 00 00 00 00 00 00 00 00 "
"33 94 D7 50 5E 43 79 CD 01 00 00 00 00 00 00 00 "
"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
"01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00",
	.test_msg_len = 64,
	.expected_tag_hex = "14 00 00 00 00 00 00 00 55 00 00 00 00 00 00 00",
},
{
	.vector_name = "RFC 7539 \xc2\xa7 A.3 #11",
	.test_key_hex =
"01 00 00 00 00 00 00 00 04 00 00 00 00 00 00 00 "
"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ",
	.test_msg_hex =
"E3 35 94 D7 50 5E 43 B9 00 00 00 00 00 00 00 00 "
"33 94 D7 50 5E 43 79 CD 01 00 00 00 00 00 00 00 "
"00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00",
	.test_msg_len = 48,
	.expected_tag_hex = "13 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00",
},
};

static void
parse_hex(const struct poly1305_kat *kat, const char *hexstr, void *voutput,
    size_t explen)
{
	/* Space or colon delimited; may contain a single trailing space;
	 * length should match exactly.
	 */
	const char *sep, *it;
	size_t sym_len, count;
	char hbyte[3], *out;
	int res;

	out = voutput;
	memset(hbyte, 0, sizeof(hbyte));

	it = hexstr;
	count = 0;
	while (true) {
		sep = strpbrk(it, " :");
		if (sep == NULL)
			sym_len = strlen(it);
		else
			sym_len = sep - it;

		ATF_REQUIRE_EQ_MSG(sym_len, 2,
		    "invalid hex byte '%.*s' in vector %s", (int)sym_len, it,
		    kat->vector_name);

		memcpy(hbyte, it, 2);
		res = sscanf(hbyte, "%hhx", &out[count]);
		ATF_REQUIRE_EQ_MSG(res, 1,
		    "invalid hex byte '%s' in vector %s", hbyte,
		    kat->vector_name);

		count++;
		ATF_REQUIRE_MSG(count <= explen,
		    "got longer than expected value at %s", kat->vector_name);

		if (sep == NULL)
			break;
		it = sep;
		while (*it == ' ' || *it == ':')
			it++;
		if (*it == 0)
			break;
	}

	ATF_REQUIRE_EQ_MSG(count, explen, "got short value at %s",
	    kat->vector_name);
}

static void
parse_vector(const struct poly1305_kat *kat,
    uint8_t key[__min_size(POLY1305_KEY_LEN)], char *msg,
    uint8_t exptag[__min_size(POLY1305_HASH_LEN)])
{
	parse_hex(kat, kat->test_key_hex, key, POLY1305_KEY_LEN);
	parse_hex(kat, kat->test_msg_hex, msg, kat->test_msg_len);
	parse_hex(kat, kat->expected_tag_hex, exptag, POLY1305_HASH_LEN);
}

static int
get_handle_fd(void)
{
	int dc_fd, fd;

	dc_fd = open("/dev/crypto", O_RDWR);

	/*
	 * Why do we do this dance instead of just operating on /dev/crypto
	 * directly?  I have no idea.
	 */
	ATF_REQUIRE(dc_fd >= 0);
	ATF_REQUIRE(ioctl(dc_fd, CRIOGET, &fd) != -1);
	close(dc_fd);
	return (fd);
}

static int
create_session(int fd, int alg, int crid, const void *key, size_t klen)
{
	struct session2_op sop;

	memset(&sop, 0, sizeof(sop));

	sop.mac = alg;
	sop.mackey = key;
	sop.mackeylen = klen;
	sop.crid = crid;

	ATF_REQUIRE_MSG(ioctl(fd, CIOCGSESSION2, &sop) >= 0,
	    "alg %d keylen %zu, errno=%d (%s)", alg, klen, errno,
	    strerror(errno));
	return (sop.ses);
}

static void
destroy_session(int fd, int _ses)
{
	uint32_t ses;

	ses = _ses;
	ATF_REQUIRE_MSG(ioctl(fd, CIOCFSESSION, &ses) >= 0,
	    "destroy session failed, errno=%d (%s)", errno, strerror(errno));
}

static void
do_cryptop(int fd, int ses, const void *inp, size_t inlen, void *out)
{
	struct crypt_op cop;

	memset(&cop, 0, sizeof(cop));

	cop.ses = ses;
	cop.len = inlen;
	cop.src = inp;
	cop.mac = out;
	ATF_CHECK_MSG(ioctl(fd, CIOCCRYPT, &cop) >= 0, "ioctl(CIOCCRYPT)");
}

static void
test_rfc7539_poly1305_vectors(int crid, const char *modname)
{
	uint8_t comptag[POLY1305_HASH_LEN], exptag[POLY1305_HASH_LEN],
	    key[POLY1305_KEY_LEN], msg[512];
	int fd, ses;
	size_t i;

	ATF_REQUIRE_KERNEL_MODULE(modname);
	ATF_REQUIRE_KERNEL_MODULE("cryptodev");

	fd = get_handle_fd();

	for (i = 0; i < nitems(rfc7539_kats); i++) {
		const struct poly1305_kat *kat;

		kat = &rfc7539_kats[i];
		parse_vector(kat, key, msg, exptag);

		ses = create_session(fd, CRYPTO_POLY1305, crid, key, sizeof(key));

		do_cryptop(fd, ses, msg, kat->test_msg_len, comptag);
		ATF_CHECK_EQ_MSG(memcmp(comptag, exptag, sizeof(exptag)), 0,
		    "KAT %s failed:", kat->vector_name);

		destroy_session(fd, ses);
	}
}

ATF_TC_WITHOUT_HEAD(poly1305_vectors);
ATF_TC_BODY(poly1305_vectors, tc)
{
	ATF_REQUIRE_SYSCTL_INT("kern.cryptodevallowsoft", 1);
	test_rfc7539_poly1305_vectors(CRYPTO_FLAG_SOFTWARE, "nexus/cryptosoft");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, poly1305_vectors);

	return (atf_no_error());
}
