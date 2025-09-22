/*	$OpenBSD: rnd.c,v 1.230 2024/12/30 02:46:00 guenther Exp $	*/

/*
 * Copyright (c) 2011,2020 Theo de Raadt.
 * Copyright (c) 2008 Damien Miller.
 * Copyright (c) 1996, 1997, 2000-2002 Michael Shalayeff.
 * Copyright (c) 2013 Markus Friedl.
 * Copyright Theodore Ts'o, 1994, 1995, 1996, 1997, 1998, 1999.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * ALTERNATIVELY, this product may be distributed under the terms of
 * the GNU Public License, in which case the provisions of the GPL are
 * required INSTEAD OF the above restrictions.  (This clause is
 * necessary due to a potential bad interaction between the GPL and
 * the restrictions contained in a BSD-style copyright.)
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * The bootblocks pre-fill the kernel .openbsd.randomdata section with seed
 * material (on-disk from previous boot, hopefully mixed with a hardware rng).
 * The first arc4random(9) call initializes this seed material as a chacha
 * state.  Calls can be done early in kernel bootstrap code -- early use is
 * encouraged.
 *
 * After the kernel timeout subsystem is initialized, random_start() prepares
 * the entropy collection mechanism enqueue_randomness() and timeout-driven
 * mixing into the chacha state.  The first submissions come from device
 * probes, later on interrupt-time submissions are more common.  Entropy
 * data (and timing information) get mixed over the entropy input ring
 * rnd_event_space[] -- the goal is to collect damage.
 *
 * Based upon timeouts, a selection of the entropy ring rnd_event_space[]
 * CRC bit-distributed and XOR mixed into entropy_pool[].
 *
 * From time to time, entropy_pool[] is SHA512-whitened, mixed with time
 * information again, XOR'd with the inner and outer states of the existing
 * chacha state, to create a new chacha state.
 *
 * During early boot (until cold=0), enqueue operations are immediately
 * dequeued, and mixed into the chacha.
 */

#include <sys/param.h>
#include <sys/event.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>
#include <sys/timeout.h>
#include <sys/atomic.h>
#include <sys/task.h>
#include <sys/msgbuf.h>
#include <sys/mount.h>
#include <sys/syscallargs.h>
#include <sys/syslimits.h>

#include <crypto/sha2.h>

#define KEYSTREAM_ONLY
#include <crypto/chacha_private.h>

#include <uvm/uvm_extern.h>

/*
 * For the purposes of better mixing, we use the CRC-32 polynomial as
 * well to make a twisted Generalized Feedback Shift Register
 *
 * (See M. Matsumoto & Y. Kurita, 1992.  Twisted GFSR generators.  ACM
 * Transactions on Modeling and Computer Simulation 2(3):179-194.
 * Also see M. Matsumoto & Y. Kurita, 1994.  Twisted GFSR generators
 * II.  ACM Transactions on Modeling and Computer Simulation 4:254-266)
 */

/*
 * Stirring polynomial over GF(2). Used in add_entropy_words() below.
 *
 * The polynomial terms are chosen to be evenly spaced (minimum RMS
 * distance from evenly spaced; except for the last tap, which is 1 to
 * get the twisting happening as fast as possible.
 *
 * The resultant polynomial is:
 *   2^POOLWORDS + 2^POOL_TAP1 + 2^POOL_TAP2 + 2^POOL_TAP3 + 2^POOL_TAP4 + 1
 */
#define POOLWORDS	2048
#define POOLBYTES	(POOLWORDS*4)
#define POOLMASK	(POOLWORDS - 1)
#define	POOL_TAP1	1638
#define	POOL_TAP2	1231
#define	POOL_TAP3	819
#define	POOL_TAP4	411

/*
 * Raw entropy collection from device drivers; at interrupt context or not.
 * enqueue_randomness() is used to submit data into the entropy input ring.
 */

#define QEVLEN	128		 /* must be a power of 2 */
#define QEVCONSUME 8		 /* how many events to consume a time */

#define KEYSZ	32
#define IVSZ	8
#define BLOCKSZ	64
#define RSBUFSZ	(16*BLOCKSZ)
#define EBUFSIZE KEYSZ + IVSZ

struct rand_event {
	u_int	re_time;
	u_int	re_val;
} rnd_event_space[QEVLEN];

u_int	rnd_event_cons;
u_int	rnd_event_prod;
int	rnd_cold = 1;
int	rnd_slowextract = 1;

void	rnd_reinit(void *v);		/* timeout to start reinit */
void	rnd_init(void *);			/* actually do the reinit */

static u_int32_t entropy_pool[POOLWORDS];
u_int32_t entropy_pool0[POOLWORDS] __attribute__((section(".openbsd.randomdata")));

void	dequeue_randomness(void *);
void	add_entropy_words(const u_int32_t *, u_int);
void	extract_entropy(u_int8_t *)
    __attribute__((__bounded__(__minbytes__,1,EBUFSIZE)));

struct timeout rnd_timeout = TIMEOUT_INITIALIZER(dequeue_randomness, NULL);

int	filt_randomread(struct knote *, long);
void	filt_randomdetach(struct knote *);
int	filt_randomwrite(struct knote *, long);

static void _rs_seed(u_char *, size_t);
static void _rs_clearseed(const void *p, size_t s);

const struct filterops randomread_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_randomdetach,
	.f_event	= filt_randomread,
};

const struct filterops randomwrite_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_randomdetach,
	.f_event	= filt_randomwrite,
};

/*
 * This function mixes entropy and timing into the entropy input ring.
 */
static void
add_event_data(u_int val)
{
	struct rand_event *rep;
	int e;

	e = (atomic_inc_int_nv(&rnd_event_prod) - 1) & (QEVLEN-1);
	rep = &rnd_event_space[e];
	rep->re_time += cpu_rnd_messybits();
	rep->re_val += val;
}

void
enqueue_randomness(u_int val)
{
	add_event_data(val);

	if (rnd_cold) {
		dequeue_randomness(NULL);
		rnd_init(NULL);
		if (!cold)
			rnd_cold = 0;
	} else if (!timeout_pending(&rnd_timeout) &&
	    (rnd_event_prod - rnd_event_cons) > QEVCONSUME) {
		rnd_slowextract = min(rnd_slowextract * 2, 5000);
		timeout_add_msec(&rnd_timeout, rnd_slowextract * 10);
	}
}

/*
 * This function merges entropy ring information into the buffer using
 * a polynomial to spread the bits.
 */
void
add_entropy_words(const u_int32_t *buf, u_int n)
{
	/* derived from IEEE 802.3 CRC-32 */
	static const u_int32_t twist_table[8] = {
		0x00000000, 0x3b6e20c8, 0x76dc4190, 0x4db26158,
		0xedb88320, 0xd6d6a3e8, 0x9b64c2b0, 0xa00ae278
	};
	static u_int	entropy_add_ptr;
	static u_char	entropy_input_rotate;

	for (; n--; buf++) {
		u_int32_t w = (*buf << entropy_input_rotate) |
		    (*buf >> ((32 - entropy_input_rotate) & 31));
		u_int i = entropy_add_ptr =
		    (entropy_add_ptr - 1) & POOLMASK;
		/*
		 * Normally, we add 7 bits of rotation to the pool.
		 * At the beginning of the pool, add an extra 7 bits
		 * rotation, so that successive passes spread the
		 * input bits across the pool evenly.
		 */
		entropy_input_rotate =
		    (entropy_input_rotate + (i ? 7 : 14)) & 31;

		/* XOR pool contents corresponding to polynomial terms */
		w ^= entropy_pool[(i + POOL_TAP1) & POOLMASK] ^
		     entropy_pool[(i + POOL_TAP2) & POOLMASK] ^
		     entropy_pool[(i + POOL_TAP3) & POOLMASK] ^
		     entropy_pool[(i + POOL_TAP4) & POOLMASK] ^
		     entropy_pool[(i + 1) & POOLMASK] ^
		     entropy_pool[i]; /* + 2^POOLWORDS */

		entropy_pool[i] = (w >> 3) ^ twist_table[w & 7];
	}
}

/*
 * Pulls entropy out of the queue and merges it into the pool with the
 * CRC.  This takes a mix of fresh entries from the producer end of the
 * queue and entries from the consumer end of the queue which are
 * likely to have collected more damage.
 */
void
dequeue_randomness(void *v)
{
	u_int32_t buf[2];
	u_int startp, startc, i;

	/* Some very new damage */
	startp = rnd_event_prod - QEVCONSUME;
	for (i = 0; i < QEVCONSUME; i++) {
		u_int e = (startp + i) & (QEVLEN-1);

		buf[0] = rnd_event_space[e].re_time;
		buf[1] = rnd_event_space[e].re_val;
		add_entropy_words(buf, 2);
	}
	/* and some probably more damaged */
	startc = atomic_add_int_nv(&rnd_event_cons, QEVCONSUME) - QEVCONSUME;
	for (i = 0; i < QEVCONSUME; i++) {
		u_int e = (startc + i) & (QEVLEN-1);

		buf[0] = rnd_event_space[e].re_time;
		buf[1] = rnd_event_space[e].re_val;
		add_entropy_words(buf, 2);
	}
}

/*
 * Grabs a chunk from the entropy_pool[] and slams it through SHA512 when
 * requested.
 */
void
extract_entropy(u_int8_t *buf)
{
	static u_int32_t extract_pool[POOLWORDS];
	u_char digest[SHA512_DIGEST_LENGTH];
	SHA2_CTX shactx;

#if SHA512_DIGEST_LENGTH < EBUFSIZE
#error "need more bigger hash output"
#endif

	/*
	 * INTENTIONALLY not protected by any lock.  Races during
	 * memcpy() result in acceptable input data; races during
	 * SHA512Update() would create nasty data dependencies.  We
	 * do not rely on this as a benefit, but if it happens, cool.
	 */
	memcpy(extract_pool, entropy_pool, sizeof(extract_pool));

	/* Hash the pool to get the output */
	SHA512Init(&shactx);
	SHA512Update(&shactx, (u_int8_t *)extract_pool, sizeof(extract_pool));
	SHA512Final(digest, &shactx);

	/* Copy data to destination buffer */
	memcpy(buf, digest, EBUFSIZE);

	/*
	 * Modify pool so next hash will produce different results.
	 */
	add_event_data(extract_pool[0]);
	dequeue_randomness(NULL);

	/* Wipe data from memory */
	explicit_bzero(extract_pool, sizeof(extract_pool));
	explicit_bzero(digest, sizeof(digest));
}

/* random keystream by ChaCha */

struct mutex rndlock = MUTEX_INITIALIZER(IPL_HIGH);
struct timeout rndreinit_timeout = TIMEOUT_INITIALIZER(rnd_reinit, NULL);
struct task rnd_task = TASK_INITIALIZER(rnd_init, NULL);

static chacha_ctx rs;		/* chacha context for random keystream */
/* keystream blocks (also chacha seed from boot) */
static u_char rs_buf[RSBUFSZ];
u_char rs_buf0[RSBUFSZ] __attribute__((section(".openbsd.randomdata")));
static size_t rs_have;		/* valid bytes at end of rs_buf */
static size_t rs_count;		/* bytes till reseed */

void
suspend_randomness(void)
{
	struct timespec ts;

	getnanotime(&ts);
	enqueue_randomness(ts.tv_sec);
	enqueue_randomness(ts.tv_nsec);

	dequeue_randomness(NULL);
	rs_count = 0;
	arc4random_buf(entropy_pool, sizeof(entropy_pool));
}

void
resume_randomness(char *buf, size_t buflen)
{
	struct timespec ts;

	if (buf && buflen)
		_rs_seed(buf, buflen);
	getnanotime(&ts);
	enqueue_randomness(ts.tv_sec);
	enqueue_randomness(ts.tv_nsec);

	dequeue_randomness(NULL);
	rs_count = 0;
}

static inline void _rs_rekey(u_char *dat, size_t datlen);

static inline void
_rs_init(u_char *buf, size_t n)
{
	KASSERT(n >= KEYSZ + IVSZ);
	chacha_keysetup(&rs, buf, KEYSZ * 8);
	chacha_ivsetup(&rs, buf + KEYSZ, NULL);
}

static void
_rs_seed(u_char *buf, size_t n)
{
	_rs_rekey(buf, n);

	/* invalidate rs_buf */
	rs_have = 0;
	memset(rs_buf, 0, sizeof(rs_buf));

	rs_count = 1600000;
}

static void
_rs_stir(int do_lock)
{
	struct timespec ts;
	u_int8_t buf[EBUFSIZE], *p;
	int i;

	/*
	 * Use SHA512 PRNG data and a system timespec; early in the boot
	 * process this is the best we can do -- some architectures do
	 * not collect entropy very well during this time, but may have
	 * clock information which is better than nothing.
	 */
	extract_entropy(buf);

	nanotime(&ts);
	for (p = (u_int8_t *)&ts, i = 0; i < sizeof(ts); i++)
		buf[i] ^= p[i];

	if (do_lock)
		mtx_enter(&rndlock);
	_rs_seed(buf, sizeof(buf));
	if (do_lock)
		mtx_leave(&rndlock);
	explicit_bzero(buf, sizeof(buf));

	/* encourage fast-dequeue again */
	rnd_slowextract = 1;
}

static inline void
_rs_stir_if_needed(size_t len)
{
	static int rs_initialized;

	if (!rs_initialized) {
		memcpy(entropy_pool, entropy_pool0, sizeof(entropy_pool));
		memcpy(rs_buf, rs_buf0, sizeof(rs_buf));
		/* seeds cannot be cleaned yet, random_start() will do so */
		_rs_init(rs_buf, KEYSZ + IVSZ);
		rs_count = 1024 * 1024 * 1024;	/* until main() runs */
		rs_initialized = 1;
	} else if (rs_count <= len)
		_rs_stir(0);
	else
		rs_count -= len;
}

static void
_rs_clearseed(const void *p, size_t s)
{
	struct kmem_dyn_mode kd_avoidalias;
	vaddr_t va = trunc_page((vaddr_t)p);
	vsize_t off = (vaddr_t)p - va;
	vsize_t len;
	vaddr_t rwva;
	paddr_t pa;

	while (s > 0) {
		pmap_extract(pmap_kernel(), va, &pa);

		memset(&kd_avoidalias, 0, sizeof(kd_avoidalias));
		kd_avoidalias.kd_prefer = pa;
		kd_avoidalias.kd_waitok = 1;
		rwva = (vaddr_t)km_alloc(PAGE_SIZE, &kv_any, &kp_none,
		    &kd_avoidalias);
		if (!rwva)
			panic("_rs_clearseed");

		pmap_kenter_pa(rwva, pa, PROT_READ | PROT_WRITE);
		pmap_update(pmap_kernel());

		len = MIN(s, PAGE_SIZE - off);
		explicit_bzero((void *)(rwva + off), len);

		pmap_kremove(rwva, PAGE_SIZE);
		km_free((void *)rwva, PAGE_SIZE, &kv_any, &kp_none);

		va += PAGE_SIZE;
		s -= len;
		off = 0;
	}
}

static inline void
_rs_rekey(u_char *dat, size_t datlen)
{
#ifndef KEYSTREAM_ONLY
	memset(rs_buf, 0, sizeof(rs_buf));
#endif
	/* fill rs_buf with the keystream */
	chacha_encrypt_bytes(&rs, rs_buf, rs_buf, sizeof(rs_buf));
	/* mix in optional user provided data */
	if (dat) {
		size_t i, m;

		m = MIN(datlen, KEYSZ + IVSZ);
		for (i = 0; i < m; i++)
			rs_buf[i] ^= dat[i];
	}
	/* immediately reinit for backtracking resistance */
	_rs_init(rs_buf, KEYSZ + IVSZ);
	memset(rs_buf, 0, KEYSZ + IVSZ);
	rs_have = sizeof(rs_buf) - KEYSZ - IVSZ;
}

static inline void
_rs_random_buf(void *_buf, size_t n)
{
	u_char *buf = (u_char *)_buf;
	size_t m;

	_rs_stir_if_needed(n);
	while (n > 0) {
		if (rs_have > 0) {
			m = MIN(n, rs_have);
			memcpy(buf, rs_buf + sizeof(rs_buf) - rs_have, m);
			memset(rs_buf + sizeof(rs_buf) - rs_have, 0, m);
			buf += m;
			n -= m;
			rs_have -= m;
		}
		if (rs_have == 0)
			_rs_rekey(NULL, 0);
	}
}

static inline void
_rs_random_u32(u_int32_t *val)
{
	_rs_stir_if_needed(sizeof(*val));
	if (rs_have < sizeof(*val))
		_rs_rekey(NULL, 0);
	memcpy(val, rs_buf + sizeof(rs_buf) - rs_have, sizeof(*val));
	memset(rs_buf + sizeof(rs_buf) - rs_have, 0, sizeof(*val));
	rs_have -= sizeof(*val);
}

/* Return one word of randomness from a ChaCha20 generator */
u_int32_t
arc4random(void)
{
	u_int32_t ret;

	mtx_enter(&rndlock);
	_rs_random_u32(&ret);
	mtx_leave(&rndlock);
	return ret;
}

/*
 * Fill a buffer of arbitrary length with ChaCha20-derived randomness.
 */
void
arc4random_buf(void *buf, size_t n)
{
	mtx_enter(&rndlock);
	_rs_random_buf(buf, n);
	mtx_leave(&rndlock);
}

/*
 * Allocate a new ChaCha20 context for the caller to use.
 */
struct arc4random_ctx *
arc4random_ctx_new(void)
{
	char keybuf[KEYSZ + IVSZ];

	chacha_ctx *ctx = malloc(sizeof(chacha_ctx), M_TEMP, M_WAITOK);
	arc4random_buf(keybuf, KEYSZ + IVSZ);
	chacha_keysetup(ctx, keybuf, KEYSZ * 8);
	chacha_ivsetup(ctx, keybuf + KEYSZ, NULL);
	explicit_bzero(keybuf, sizeof(keybuf));
	return (struct arc4random_ctx *)ctx;
}

/*
 * Free a ChaCha20 context created by arc4random_ctx_new()
 */
void
arc4random_ctx_free(struct arc4random_ctx *ctx)
{
	explicit_bzero(ctx, sizeof(chacha_ctx));
	free(ctx, M_TEMP, sizeof(chacha_ctx));
}

/*
 * Use a given ChaCha20 context to fill a buffer
 */
void
arc4random_ctx_buf(struct arc4random_ctx *ctx, void *buf, size_t n)
{
#ifndef KEYSTREAM_ONLY
	memset(buf, 0, n);
#endif
	chacha_encrypt_bytes((chacha_ctx *)ctx, buf, buf, n);
}

/*
 * Calculate a uniformly distributed random number less than upper_bound
 * avoiding "modulo bias".
 *
 * Uniformity is achieved by generating new random numbers until the one
 * returned is outside the range [0, 2**32 % upper_bound).  This
 * guarantees the selected random number will be inside
 * [2**32 % upper_bound, 2**32) which maps back to [0, upper_bound)
 * after reduction modulo upper_bound.
 */
u_int32_t
arc4random_uniform(u_int32_t upper_bound)
{
	u_int32_t r, min;

	if (upper_bound < 2)
		return 0;

	/* 2**32 % x == (2**32 - x) % x */
	min = -upper_bound % upper_bound;

	/*
	 * This could theoretically loop forever but each retry has
	 * p > 0.5 (worst case, usually far better) of selecting a
	 * number inside the range we need, so it should rarely need
	 * to re-roll.
	 */
	for (;;) {
		r = arc4random();
		if (r >= min)
			break;
	}

	return r % upper_bound;
}

void
rnd_init(void *null)
{
	_rs_stir(1);
}

/*
 * Called by timeout to mark arc4 for stirring,
 */
void
rnd_reinit(void *v)
{
	task_add(systq, &rnd_task);
	/* 10 minutes, per dm@'s suggestion */
	timeout_add_sec(&rndreinit_timeout, 10 * 60);
}

/*
 * Start periodic services inside the random subsystem, which pull
 * entropy forward, hash it, and re-seed the random stream as needed.
 */
void
random_start(int goodseed)
{
	extern char etext[];

#if !defined(NO_PROPOLICE)
	extern long __guard_local;

	if (__guard_local == 0)
		printf("warning: no entropy supplied by boot loader\n");
#endif

	_rs_clearseed(entropy_pool0, sizeof(entropy_pool0));
	_rs_clearseed(rs_buf0, sizeof(rs_buf0));

	/* Message buffer may contain data from previous boot */
	if (msgbufp->msg_magic == MSG_MAGIC)
		add_entropy_words((u_int32_t *)msgbufp->msg_bufc,
		    msgbufp->msg_bufs / sizeof(u_int32_t));
	add_entropy_words((u_int32_t *)etext - 32*1024,
	    8192/sizeof(u_int32_t));

	dequeue_randomness(NULL);
	rnd_init(NULL);
	rnd_reinit(NULL);

	if (goodseed)
		printf("random: good seed from bootblocks\n");
	else {
		/* XXX kernel should work harder here */
		printf("random: boothowto does not indicate good seed\n");
	}
}

int
randomopen(dev_t dev, int flag, int mode, struct proc *p)
{
	return 0;
}

int
randomclose(dev_t dev, int flag, int mode, struct proc *p)
{
	return 0;
}

/*
 * Maximum number of bytes to serve directly from the main ChaCha
 * pool. Larger requests are served from a discrete ChaCha instance keyed
 * from the main pool.
 */
#define RND_MAIN_MAX_BYTES	2048

int
randomread(dev_t dev, struct uio *uio, int ioflag)
{
	struct arc4random_ctx *lctx = NULL;
	size_t		total = uio->uio_resid;
	u_char		*buf;
	int		ret = 0;

	if (uio->uio_resid == 0)
		return 0;

	buf = malloc(POOLBYTES, M_TEMP, M_WAITOK);
	if (total > RND_MAIN_MAX_BYTES)
		lctx = arc4random_ctx_new();

	while (ret == 0 && uio->uio_resid > 0) {
		size_t	n = ulmin(POOLBYTES, uio->uio_resid);

		if (lctx != NULL)
			arc4random_ctx_buf(lctx, buf, n);
		else
			arc4random_buf(buf, n);
		ret = uiomove(buf, n, uio);
		if (ret == 0 && uio->uio_resid > 0)
			yield();
	}
	if (lctx != NULL)
		arc4random_ctx_free(lctx);
	explicit_bzero(buf, POOLBYTES);
	free(buf, M_TEMP, POOLBYTES);
	return ret;
}

int
randomwrite(dev_t dev, struct uio *uio, int flags)
{
	int		ret = 0, newdata = 0;
	u_int32_t	*buf;

	if (uio->uio_resid == 0)
		return 0;

	buf = malloc(POOLBYTES, M_TEMP, M_WAITOK);

	while (ret == 0 && uio->uio_resid > 0) {
		size_t	n = ulmin(POOLBYTES, uio->uio_resid);

		ret = uiomove(buf, n, uio);
		if (ret != 0)
			break;
		while (n % sizeof(u_int32_t))
			((u_int8_t *)buf)[n++] = 0;
		add_entropy_words(buf, n / 4);
		if (uio->uio_resid > 0)
			yield();
		newdata = 1;
	}

	if (newdata)
		rnd_init(NULL);

	explicit_bzero(buf, POOLBYTES);
	free(buf, M_TEMP, POOLBYTES);
	return ret;
}

int
randomkqfilter(dev_t dev, struct knote *kn)
{
	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &randomread_filtops;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &randomwrite_filtops;
		break;
	default:
		return (EINVAL);
	}

	return (0);
}

void
filt_randomdetach(struct knote *kn)
{
}

int
filt_randomread(struct knote *kn, long hint)
{
	kn->kn_data = RND_MAIN_MAX_BYTES;
	return (1);
}

int
filt_randomwrite(struct knote *kn, long hint)
{
	kn->kn_data = POOLBYTES;
	return (1);
}

int
randomioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	switch (cmd) {
	case FIOASYNC:
		/* No async flag in softc so this is a no-op. */
		break;
	default:
		return ENOTTY;
	}
	return 0;
}

int
sys_getentropy(struct proc *p, void *v, register_t *retval)
{
	struct sys_getentropy_args /* {
		syscallarg(void *) buf;
		syscallarg(size_t) nbyte;
	} */ *uap = v;
	char buf[GETENTROPY_MAX];
	int error;

	if (SCARG(uap, nbyte) > sizeof(buf))
		return (EINVAL);
	arc4random_buf(buf, SCARG(uap, nbyte));
	if ((error = copyout(buf, SCARG(uap, buf), SCARG(uap, nbyte))) != 0)
		return (error);
	explicit_bzero(buf, sizeof(buf));
	*retval = 0;
	return (0);
}
