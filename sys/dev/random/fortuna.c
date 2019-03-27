/*-
 * Copyright (c) 2017 W. Dean Freeman
 * Copyright (c) 2013-2015 Mark R V Murray
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

/*
 * This implementation of Fortuna is based on the descriptions found in
 * ISBN 978-0-470-47424-2 "Cryptography Engineering" by Ferguson, Schneier
 * and Kohno ("FS&K").
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/limits.h>

#ifdef _KERNEL
#include <sys/fail.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/random.h>
#include <sys/sdt.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <machine/cpu.h>
#else /* !_KERNEL */
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "unit_test.h"
#endif /* _KERNEL */

#include <crypto/rijndael/rijndael-api-fst.h>
#include <crypto/sha2/sha256.h>

#include <dev/random/hash.h>
#include <dev/random/randomdev.h>
#ifdef _KERNEL
#include <dev/random/random_harvestq.h>
#endif
#include <dev/random/uint128.h>
#include <dev/random/fortuna.h>

/* Defined in FS&K */
#define	RANDOM_FORTUNA_NPOOLS 32		/* The number of accumulation pools */
#define	RANDOM_FORTUNA_DEFPOOLSIZE 64		/* The default pool size/length for a (re)seed */
#define	RANDOM_FORTUNA_MAX_READ (1 << 20)	/* Max bytes in a single read */

/*
 * The allowable range of RANDOM_FORTUNA_DEFPOOLSIZE. The default value is above.
 * Making RANDOM_FORTUNA_DEFPOOLSIZE too large will mean a long time between reseeds,
 * and too small may compromise initial security but get faster reseeds.
 */
#define	RANDOM_FORTUNA_MINPOOLSIZE 16
#define	RANDOM_FORTUNA_MAXPOOLSIZE INT_MAX 
CTASSERT(RANDOM_FORTUNA_MINPOOLSIZE <= RANDOM_FORTUNA_DEFPOOLSIZE);
CTASSERT(RANDOM_FORTUNA_DEFPOOLSIZE <= RANDOM_FORTUNA_MAXPOOLSIZE);

/* This algorithm (and code) presumes that RANDOM_KEYSIZE is twice as large as RANDOM_BLOCKSIZE */
CTASSERT(RANDOM_BLOCKSIZE == sizeof(uint128_t));
CTASSERT(RANDOM_KEYSIZE == 2*RANDOM_BLOCKSIZE);

/* Probes for dtrace(1) */
#ifdef _KERNEL
SDT_PROVIDER_DECLARE(random);
SDT_PROVIDER_DEFINE(random);
SDT_PROBE_DEFINE2(random, fortuna, event_processor, debug, "u_int", "struct fs_pool *");
#endif /* _KERNEL */

/*
 * This is the beastie that needs protecting. It contains all of the
 * state that we are excited about. Exactly one is instantiated.
 */
static struct fortuna_state {
	struct fs_pool {		/* P_i */
		u_int fsp_length;	/* Only the first one is used by Fortuna */
		struct randomdev_hash fsp_hash;
	} fs_pool[RANDOM_FORTUNA_NPOOLS];
	u_int fs_reseedcount;		/* ReseedCnt */
	uint128_t fs_counter;		/* C */
	union randomdev_key fs_key;	/* K */
	u_int fs_minpoolsize;		/* Extras */
	/* Extras for the OS */
#ifdef _KERNEL
	/* For use when 'pacing' the reseeds */
	sbintime_t fs_lasttime;
#endif
	/* Reseed lock */
	mtx_t fs_mtx;
} fortuna_state;

#ifdef _KERNEL
static struct sysctl_ctx_list random_clist;
RANDOM_CHECK_UINT(fs_minpoolsize, RANDOM_FORTUNA_MINPOOLSIZE, RANDOM_FORTUNA_MAXPOOLSIZE);
#else
static uint8_t zero_region[RANDOM_ZERO_BLOCKSIZE];
#endif

static void random_fortuna_pre_read(void);
static void random_fortuna_read(uint8_t *, u_int);
static bool random_fortuna_seeded(void);
static void random_fortuna_process_event(struct harvest_event *);
static void random_fortuna_init_alg(void *);
static void random_fortuna_deinit_alg(void *);

static void random_fortuna_reseed_internal(uint32_t *entropy_data, u_int blockcount);

struct random_algorithm random_alg_context = {
	.ra_ident = "Fortuna",
	.ra_init_alg = random_fortuna_init_alg,
	.ra_deinit_alg = random_fortuna_deinit_alg,
	.ra_pre_read = random_fortuna_pre_read,
	.ra_read = random_fortuna_read,
	.ra_seeded = random_fortuna_seeded,
	.ra_event_processor = random_fortuna_process_event,
	.ra_poolcount = RANDOM_FORTUNA_NPOOLS,
};

/* ARGSUSED */
static void
random_fortuna_init_alg(void *unused __unused)
{
	int i;
#ifdef _KERNEL
	struct sysctl_oid *random_fortuna_o;
#endif

	RANDOM_RESEED_INIT_LOCK();
	/*
	 * Fortuna parameters. Do not adjust these unless you have
	 * have a very good clue about what they do!
	 */
	fortuna_state.fs_minpoolsize = RANDOM_FORTUNA_DEFPOOLSIZE;
#ifdef _KERNEL
	fortuna_state.fs_lasttime = 0;
	random_fortuna_o = SYSCTL_ADD_NODE(&random_clist,
		SYSCTL_STATIC_CHILDREN(_kern_random),
		OID_AUTO, "fortuna", CTLFLAG_RW, 0,
		"Fortuna Parameters");
	SYSCTL_ADD_PROC(&random_clist,
		SYSCTL_CHILDREN(random_fortuna_o), OID_AUTO,
		"minpoolsize", CTLTYPE_UINT | CTLFLAG_RWTUN,
		&fortuna_state.fs_minpoolsize, RANDOM_FORTUNA_DEFPOOLSIZE,
		random_check_uint_fs_minpoolsize, "IU",
		"Minimum pool size necessary to cause a reseed");
	KASSERT(fortuna_state.fs_minpoolsize > 0, ("random: Fortuna threshold must be > 0 at startup"));
#endif

	/*-
	 * FS&K - InitializePRNG()
	 *      - P_i = \epsilon
	 *      - ReseedCNT = 0
	 */
	for (i = 0; i < RANDOM_FORTUNA_NPOOLS; i++) {
		randomdev_hash_init(&fortuna_state.fs_pool[i].fsp_hash);
		fortuna_state.fs_pool[i].fsp_length = 0;
	}
	fortuna_state.fs_reseedcount = 0;
	/*-
	 * FS&K - InitializeGenerator()
	 *      - C = 0
	 *      - K = 0
	 */
	fortuna_state.fs_counter = UINT128_ZERO;
	explicit_bzero(&fortuna_state.fs_key, sizeof(fortuna_state.fs_key));
}

/* ARGSUSED */
static void
random_fortuna_deinit_alg(void *unused __unused)
{

	RANDOM_RESEED_DEINIT_LOCK();
	explicit_bzero(&fortuna_state, sizeof(fortuna_state));
#ifdef _KERNEL
	sysctl_ctx_free(&random_clist);
#endif
}

/*-
 * FS&K - AddRandomEvent()
 * Process a single stochastic event off the harvest queue
 */
static void
random_fortuna_process_event(struct harvest_event *event)
{
	u_int pl;

	RANDOM_RESEED_LOCK();
	/*-
	 * FS&K - P_i = P_i|<harvested stuff>
	 * Accumulate the event into the appropriate pool
	 * where each event carries the destination information.
	 *
	 * The hash_init() and hash_finish() calls are done in
	 * random_fortuna_pre_read().
	 *
	 * We must be locked against pool state modification which can happen
	 * during accumulation/reseeding and reading/regating.
	 */
	pl = event->he_destination % RANDOM_FORTUNA_NPOOLS;
	/*
	 * We ignore low entropy static/counter fields towards the end of the
	 * he_event structure in order to increase measurable entropy when
	 * conducting SP800-90B entropy analysis measurements of seed material
	 * fed into PRNG.
	 * -- wdf
	 */
	KASSERT(event->he_size <= sizeof(event->he_entropy),
	    ("%s: event->he_size: %hhu > sizeof(event->he_entropy): %zu\n",
	    __func__, event->he_size, sizeof(event->he_entropy)));
	randomdev_hash_iterate(&fortuna_state.fs_pool[pl].fsp_hash,
	    &event->he_somecounter, sizeof(event->he_somecounter));
	randomdev_hash_iterate(&fortuna_state.fs_pool[pl].fsp_hash,
	    event->he_entropy, event->he_size);

	/*-
	 * Don't wrap the length.  This is a "saturating" add.
	 * XXX: FIX!!: We don't actually need lengths for anything but fs_pool[0],
	 * but it's been useful debugging to see them all.
	 */
	fortuna_state.fs_pool[pl].fsp_length = MIN(RANDOM_FORTUNA_MAXPOOLSIZE,
	    fortuna_state.fs_pool[pl].fsp_length +
	    sizeof(event->he_somecounter) + event->he_size);
	explicit_bzero(event, sizeof(*event));
	RANDOM_RESEED_UNLOCK();
}

/*-
 * FS&K - Reseed()
 * This introduces new key material into the output generator.
 * Additionally it increments the output generator's counter
 * variable C. When C > 0, the output generator is seeded and
 * will deliver output.
 * The entropy_data buffer passed is a very specific size; the
 * product of RANDOM_FORTUNA_NPOOLS and RANDOM_KEYSIZE.
 */
static void
random_fortuna_reseed_internal(uint32_t *entropy_data, u_int blockcount)
{
	struct randomdev_hash context;
	uint8_t hash[RANDOM_KEYSIZE];
	const void *keymaterial;
	size_t keysz;
	bool seeded;

	RANDOM_RESEED_ASSERT_LOCK_OWNED();

	seeded = random_fortuna_seeded();
	if (seeded) {
		randomdev_getkey(&fortuna_state.fs_key, &keymaterial, &keysz);
		KASSERT(keysz == RANDOM_KEYSIZE, ("%s: key size %zu not %u",
			__func__, keysz, (unsigned)RANDOM_KEYSIZE));
	}

	/*-
	 * FS&K - K = Hd(K|s) where Hd(m) is H(H(0^512|m))
	 *      - C = C + 1
	 */
	randomdev_hash_init(&context);
	randomdev_hash_iterate(&context, zero_region, RANDOM_ZERO_BLOCKSIZE);
	if (seeded)
		randomdev_hash_iterate(&context, keymaterial, keysz);
	randomdev_hash_iterate(&context, entropy_data, RANDOM_KEYSIZE*blockcount);
	randomdev_hash_finish(&context, hash);
	randomdev_hash_init(&context);
	randomdev_hash_iterate(&context, hash, RANDOM_KEYSIZE);
	randomdev_hash_finish(&context, hash);
	randomdev_encrypt_init(&fortuna_state.fs_key, hash);
	explicit_bzero(hash, sizeof(hash));
	/* Unblock the device if this is the first time we are reseeding. */
	if (uint128_is_zero(fortuna_state.fs_counter))
		randomdev_unblock();
	uint128_increment(&fortuna_state.fs_counter);
}

/*-
 * FS&K - GenerateBlocks()
 * Generate a number of complete blocks of random output.
 */
static __inline void
random_fortuna_genblocks(uint8_t *buf, u_int blockcount)
{

	RANDOM_RESEED_ASSERT_LOCK_OWNED();
	KASSERT(!uint128_is_zero(fortuna_state.fs_counter), ("FS&K: C != 0"));

	/*
	 * Fills buf with RANDOM_BLOCKSIZE * blockcount bytes of keystream.
	 * Increments fs_counter as it goes.
	 */
	randomdev_keystream(&fortuna_state.fs_key, &fortuna_state.fs_counter,
	    buf, blockcount);
}

/*-
 * FS&K - PseudoRandomData()
 * This generates no more than 2^20 bytes of data, and cleans up its
 * internal state when finished. It is assumed that a whole number of
 * blocks are available for writing; any excess generated will be
 * ignored.
 */
static __inline void
random_fortuna_genrandom(uint8_t *buf, u_int bytecount)
{
	uint8_t temp[RANDOM_BLOCKSIZE * RANDOM_KEYS_PER_BLOCK];
	u_int blockcount;

	RANDOM_RESEED_ASSERT_LOCK_OWNED();
	/*-
	 * FS&K - assert(n < 2^20 (== 1 MB)
	 *      - r = first-n-bytes(GenerateBlocks(ceil(n/16)))
	 *      - K = GenerateBlocks(2)
	 */
	KASSERT((bytecount <= RANDOM_FORTUNA_MAX_READ), ("invalid single read request to Fortuna of %d bytes", bytecount));
	blockcount = howmany(bytecount, RANDOM_BLOCKSIZE);
	random_fortuna_genblocks(buf, blockcount);
	random_fortuna_genblocks(temp, RANDOM_KEYS_PER_BLOCK);
	randomdev_encrypt_init(&fortuna_state.fs_key, temp);
	explicit_bzero(temp, sizeof(temp));
}

/*-
 * FS&K - RandomData() (Part 1)
 * Used to return processed entropy from the PRNG. There is a pre_read
 * required to be present (but it can be a stub) in order to allow
 * specific actions at the begin of the read.
 */
void
random_fortuna_pre_read(void)
{
#ifdef _KERNEL
	sbintime_t now;
#endif
	struct randomdev_hash context;
	uint32_t s[RANDOM_FORTUNA_NPOOLS*RANDOM_KEYSIZE_WORDS];
	uint8_t temp[RANDOM_KEYSIZE];
	u_int i;

	KASSERT(fortuna_state.fs_minpoolsize > 0, ("random: Fortuna threshold must be > 0"));
	RANDOM_RESEED_LOCK();
#ifdef _KERNEL
	/* FS&K - Use 'getsbinuptime()' to prevent reseed-spamming. */
	now = getsbinuptime();
#endif

	if (fortuna_state.fs_pool[0].fsp_length < fortuna_state.fs_minpoolsize
#ifdef _KERNEL
	    /* FS&K - Use 'getsbinuptime()' to prevent reseed-spamming. */
	    || (now - fortuna_state.fs_lasttime <= SBT_1S/10)
#endif
	) {
		RANDOM_RESEED_UNLOCK();
		return;
	}

#ifdef _KERNEL
	/*
	 * When set, pretend we do not have enough entropy to reseed yet.
	 */
	KFAIL_POINT_CODE(DEBUG_FP, random_fortuna_pre_read, {
		if (RETURN_VALUE != 0) {
			RANDOM_RESEED_UNLOCK();
			return;
		}
	});
#endif

#ifdef _KERNEL
	fortuna_state.fs_lasttime = now;
#endif

	/* FS&K - ReseedCNT = ReseedCNT + 1 */
	fortuna_state.fs_reseedcount++;
	/* s = \epsilon at start */
	for (i = 0; i < RANDOM_FORTUNA_NPOOLS; i++) {
		/* FS&K - if Divides(ReseedCnt, 2^i) ... */
		if ((fortuna_state.fs_reseedcount % (1 << i)) == 0) {
			/*-
			    * FS&K - temp = (P_i)
			    *      - P_i = \epsilon
			    *      - s = s|H(temp)
			    */
			randomdev_hash_finish(&fortuna_state.fs_pool[i].fsp_hash, temp);
			randomdev_hash_init(&fortuna_state.fs_pool[i].fsp_hash);
			fortuna_state.fs_pool[i].fsp_length = 0;
			randomdev_hash_init(&context);
			randomdev_hash_iterate(&context, temp, RANDOM_KEYSIZE);
			randomdev_hash_finish(&context, s + i*RANDOM_KEYSIZE_WORDS);
		} else
			break;
	}
#ifdef _KERNEL
	SDT_PROBE2(random, fortuna, event_processor, debug, fortuna_state.fs_reseedcount, fortuna_state.fs_pool);
#endif
	/* FS&K */
	random_fortuna_reseed_internal(s, i);
	RANDOM_RESEED_UNLOCK();

	/* Clean up and secure */
	explicit_bzero(s, sizeof(s));
	explicit_bzero(temp, sizeof(temp));
}

/*-
 * FS&K - RandomData() (Part 2)
 * Main read from Fortuna, continued. May be called multiple times after
 * the random_fortuna_pre_read() above.
 * The supplied buf MUST be a multiple of RANDOM_BLOCKSIZE in size.
 * Lots of code presumes this for efficiency, both here and in other
 * routines. You are NOT allowed to break this!
 */
void
random_fortuna_read(uint8_t *buf, u_int bytecount)
{

	KASSERT((bytecount % RANDOM_BLOCKSIZE) == 0, ("%s(): bytecount (= %d) must be a multiple of %d", __func__, bytecount, RANDOM_BLOCKSIZE ));
	RANDOM_RESEED_LOCK();
	random_fortuna_genrandom(buf, bytecount);
	RANDOM_RESEED_UNLOCK();
}

bool
random_fortuna_seeded(void)
{

#ifdef _KERNEL
	/* When set, act as if we are not seeded. */
	KFAIL_POINT_CODE(DEBUG_FP, random_fortuna_seeded, {
		if (RETURN_VALUE != 0)
			fortuna_state.fs_counter = UINT128_ZERO;
	});
#endif

	return (!uint128_is_zero(fortuna_state.fs_counter));
}
