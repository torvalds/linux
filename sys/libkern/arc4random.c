/*-
 * Copyright (c) 2017 The FreeBSD Foundation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/linker.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/random.h>
#include <sys/smp.h>
#include <sys/time.h>

#include <crypto/chacha20/chacha.h>

#define	CHACHA20_RESEED_BYTES	65536
#define	CHACHA20_RESEED_SECONDS	300
#define	CHACHA20_KEYBYTES	32
#define	CHACHA20_BUFFER_SIZE	64

CTASSERT(CHACHA20_KEYBYTES*8 >= CHACHA_MINKEYLEN);

int arc4rand_iniseed_state = ARC4_ENTR_NONE;

MALLOC_DEFINE(M_CHACHA20RANDOM, "chacha20random", "chacha20random structures");

struct chacha20_s {
	struct mtx mtx;
	int numbytes;
	int first_time_done;
	time_t t_reseed;
	u_int8_t m_buffer[CHACHA20_BUFFER_SIZE];
	struct chacha_ctx ctx;
} __aligned(CACHE_LINE_SIZE);

static struct chacha20_s *chacha20inst = NULL;

#define CHACHA20_FOREACH(_chacha20) \
	for (_chacha20 = &chacha20inst[0]; \
	     _chacha20 <= &chacha20inst[mp_maxid]; \
	     _chacha20++)

/*
 * Mix up the current context.
 */
static void
chacha20_randomstir(struct chacha20_s* chacha20)
{
	struct timeval tv_now;
	size_t n, size;
	u_int8_t key[CHACHA20_KEYBYTES], *data;
	caddr_t keyfile;

	/*
	 * This is making the best of what may be an insecure
	 * Situation. If the loader(8) did not have an entropy
	 * stash from the previous shutdown to load, then we will
	 * be improperly seeded. The answer is to make sure there
	 * is an entropy stash at shutdown time.
	 */
	(void)read_random(key, CHACHA20_KEYBYTES);
	if (!chacha20->first_time_done) {
		keyfile = preload_search_by_type(RANDOM_CACHED_BOOT_ENTROPY_MODULE);
		if (keyfile != NULL) {
			data = preload_fetch_addr(keyfile);
			size = MIN(preload_fetch_size(keyfile), CHACHA20_KEYBYTES);
			for (n = 0; n < size; n++)
				key[n] ^= data[n];
			explicit_bzero(data, size);
			if (bootverbose)
				printf("arc4random: read %zu bytes from preloaded cache\n", size);
		} else
			printf("arc4random: no preloaded entropy cache\n");
		chacha20->first_time_done = 1;
	}
	getmicrouptime(&tv_now);
	mtx_lock(&chacha20->mtx);
	chacha_keysetup(&chacha20->ctx, key, CHACHA20_KEYBYTES*8);
	chacha_ivsetup(&chacha20->ctx, (u_char *)&tv_now.tv_sec, (u_char *)&tv_now.tv_usec);
	/* Reset for next reseed cycle. */
	chacha20->t_reseed = tv_now.tv_sec + CHACHA20_RESEED_SECONDS;
	chacha20->numbytes = 0;
	mtx_unlock(&chacha20->mtx);
}

/*
 * Initialize the contexts.
 */
static void
chacha20_init(void)
{
	struct chacha20_s *chacha20;

	chacha20inst = malloc((mp_maxid + 1) * sizeof(struct chacha20_s),
			M_CHACHA20RANDOM, M_NOWAIT | M_ZERO);
	KASSERT(chacha20inst != NULL, ("chacha20_init: memory allocation error"));

	CHACHA20_FOREACH(chacha20) {
		mtx_init(&chacha20->mtx, "chacha20_mtx", NULL, MTX_DEF);
		chacha20->t_reseed = -1;
		chacha20->numbytes = 0;
		chacha20->first_time_done = 0;
		explicit_bzero(chacha20->m_buffer, CHACHA20_BUFFER_SIZE);
		explicit_bzero(&chacha20->ctx, sizeof(chacha20->ctx));
	}
}
SYSINIT(chacha20, SI_SUB_LOCK, SI_ORDER_ANY, chacha20_init, NULL);


static void
chacha20_uninit(void)
{
	struct chacha20_s *chacha20;

	CHACHA20_FOREACH(chacha20)
		mtx_destroy(&chacha20->mtx);
	free(chacha20inst, M_CHACHA20RANDOM);
}
SYSUNINIT(chacha20, SI_SUB_LOCK, SI_ORDER_ANY, chacha20_uninit, NULL);


/*
 * MPSAFE
 */
void
arc4rand(void *ptr, u_int len, int reseed)
{
	struct chacha20_s *chacha20;
	struct timeval tv;
	u_int length;
	u_int8_t *p;

	if (reseed || atomic_cmpset_int(&arc4rand_iniseed_state, ARC4_ENTR_HAVE, ARC4_ENTR_SEED))
		CHACHA20_FOREACH(chacha20)
			chacha20_randomstir(chacha20);

	chacha20 = &chacha20inst[curcpu];
	getmicrouptime(&tv);
	/* We may get unlucky and be migrated off this CPU, but that is expected to be infrequent */
	if ((chacha20->numbytes > CHACHA20_RESEED_BYTES) || (tv.tv_sec > chacha20->t_reseed))
		chacha20_randomstir(chacha20);

	mtx_lock(&chacha20->mtx);
	p = ptr;
	while (len) {
		length = MIN(CHACHA20_BUFFER_SIZE, len);
		chacha_encrypt_bytes(&chacha20->ctx, chacha20->m_buffer, p, length);
		p += length;
		len -= length;
		chacha20->numbytes += length;
		if (chacha20->numbytes > CHACHA20_RESEED_BYTES) {
			mtx_unlock(&chacha20->mtx);
			chacha20_randomstir(chacha20);
			mtx_lock(&chacha20->mtx);
		}
	}
	mtx_unlock(&chacha20->mtx);
}

uint32_t
arc4random(void)
{
	uint32_t ret;

	arc4rand(&ret, sizeof(ret), 0);
	return ret;
}

void
arc4random_buf(void *ptr, size_t len)
{

	arc4rand(ptr, len, 0);
}
