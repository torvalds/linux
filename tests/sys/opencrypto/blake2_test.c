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

/*
 * Derived from blake2b-test.c and blake2s-test.c:
 *
 * BLAKE2 reference source code package - optimized C implementations
 *
 * Written in 2012 by Samuel Neves <sneves@dei.uc.pt>
 *
 * To the extent possible under law, the author(s) have dedicated all copyright
 * and related and neighboring rights to this software to the public domain
 * worldwide. This software is distributed without any warranty.
 *
 * You should have received a copy of the CC0 Public Domain Dedication along with
 * this software. If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

#include <sys/param.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include <atf-c.h>

/* Be sure to include tree copy rather than system copy. */
#include "cryptodev.h"

#include "freebsd_test_suite/macros.h"

#include <blake2.h>
#include "blake2-kat.h"

static uint8_t key2b[BLAKE2B_KEYBYTES];
static uint8_t key2s[BLAKE2S_KEYBYTES];
static uint8_t katbuf[KAT_LENGTH];

static void
initialize_constant_buffers(void)
{
	size_t i;

	for (i = 0; i < sizeof(key2b); i++)
		key2b[i] = (uint8_t)i;
	for (i = 0; i < sizeof(key2s); i++)
		key2s[i] = (uint8_t)i;
	for (i = 0; i < sizeof(katbuf); i++)
		katbuf[i] = (uint8_t)i;
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
do_cryptop(int fd, int ses, size_t inlen, void *out)
{
	struct crypt_op cop;

	memset(&cop, 0, sizeof(cop));

	cop.ses = ses;
	cop.len = inlen;
	cop.src = katbuf;
	cop.mac = out;
	ATF_CHECK_MSG(ioctl(fd, CIOCCRYPT, &cop) >= 0, "ioctl(CIOCCRYPT)");
}

static void
test_blake2b_vectors(int crid, const char *modname)
{
	uint8_t hash[BLAKE2B_OUTBYTES];
	int fd, ses;
	size_t i;

	ATF_REQUIRE_KERNEL_MODULE(modname);
	ATF_REQUIRE_KERNEL_MODULE("cryptodev");

	initialize_constant_buffers();
	fd = get_handle_fd();
	ses = create_session(fd, CRYPTO_BLAKE2B, crid, key2b, sizeof(key2b));

	for (i = 0; i < sizeof(katbuf); i++) {
		do_cryptop(fd, ses, i, hash);
		ATF_CHECK_EQ_MSG(
		    memcmp(hash, blake2b_keyed_kat[i], sizeof(hash)),
		    0,
		    "different at %zu", i);
	}
}

static void
test_blake2s_vectors(int crid, const char *modname)
{
	uint8_t hash[BLAKE2S_OUTBYTES];
	int fd, ses;
	size_t i;

	ATF_REQUIRE_KERNEL_MODULE(modname);
	ATF_REQUIRE_KERNEL_MODULE("cryptodev");

	initialize_constant_buffers();
	fd = get_handle_fd();
	ses = create_session(fd, CRYPTO_BLAKE2S, crid, key2s, sizeof(key2s));

	for (i = 0; i < sizeof(katbuf); i++) {
		do_cryptop(fd, ses, i, hash);
		ATF_CHECK_EQ_MSG(
		    memcmp(hash, blake2s_keyed_kat[i], sizeof(hash)),
		    0,
		    "different at %zu", i);
	}
}

ATF_TC_WITHOUT_HEAD(blake2b_vectors);
ATF_TC_BODY(blake2b_vectors, tc)
{
	ATF_REQUIRE_SYSCTL_INT("kern.cryptodevallowsoft", 1);
	test_blake2b_vectors(CRYPTO_FLAG_SOFTWARE, "nexus/cryptosoft");
}

ATF_TC_WITHOUT_HEAD(blake2s_vectors);
ATF_TC_BODY(blake2s_vectors, tc)
{
	ATF_REQUIRE_SYSCTL_INT("kern.cryptodevallowsoft", 1);
	test_blake2s_vectors(CRYPTO_FLAG_SOFTWARE, "nexus/cryptosoft");
}

#if defined(__i386__) || defined(__amd64__)
ATF_TC_WITHOUT_HEAD(blake2b_vectors_x86);
ATF_TC_BODY(blake2b_vectors_x86, tc)
{
	test_blake2b_vectors(CRYPTO_FLAG_HARDWARE, "nexus/blake2");
}

ATF_TC_WITHOUT_HEAD(blake2s_vectors_x86);
ATF_TC_BODY(blake2s_vectors_x86, tc)
{
	test_blake2s_vectors(CRYPTO_FLAG_HARDWARE, "nexus/blake2");
}
#endif

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, blake2b_vectors);
	ATF_TP_ADD_TC(tp, blake2s_vectors);
#if defined(__i386__) || defined(__amd64__)
	ATF_TP_ADD_TC(tp, blake2b_vectors_x86);
	ATF_TP_ADD_TC(tp, blake2s_vectors_x86);
#endif

	return (atf_no_error());
}
