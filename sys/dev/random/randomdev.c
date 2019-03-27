/*-
 * Copyright (c) 2017 Oliver Pinter
 * Copyright (c) 2000-2015 Mark R V Murray
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/filio.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/random.h>
#include <sys/sbuf.h>
#include <sys/selinfo.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/unistd.h>

#include <crypto/rijndael/rijndael-api-fst.h>
#include <crypto/sha2/sha256.h>

#include <dev/random/hash.h>
#include <dev/random/randomdev.h>
#include <dev/random/random_harvestq.h>

#define	RANDOM_UNIT	0

#if defined(RANDOM_LOADABLE)
#define READ_RANDOM_UIO	_read_random_uio
#define READ_RANDOM	_read_random
static int READ_RANDOM_UIO(struct uio *, bool);
static u_int READ_RANDOM(void *, u_int);
#else
#define READ_RANDOM_UIO	read_random_uio
#define READ_RANDOM	read_random
#endif

static d_read_t randomdev_read;
static d_write_t randomdev_write;
static d_poll_t randomdev_poll;
static d_ioctl_t randomdev_ioctl;

static struct cdevsw random_cdevsw = {
	.d_name = "random",
	.d_version = D_VERSION,
	.d_read = randomdev_read,
	.d_write = randomdev_write,
	.d_poll = randomdev_poll,
	.d_ioctl = randomdev_ioctl,
};

/* For use with make_dev(9)/destroy_dev(9). */
static struct cdev *random_dev;

static void
random_alg_context_ra_init_alg(void *data)
{

	p_random_alg_context = &random_alg_context;
	p_random_alg_context->ra_init_alg(data);
#if defined(RANDOM_LOADABLE)
	random_infra_init(READ_RANDOM_UIO, READ_RANDOM);
#endif
}

static void
random_alg_context_ra_deinit_alg(void *data)
{

#if defined(RANDOM_LOADABLE)
	random_infra_uninit();
#endif
	p_random_alg_context->ra_deinit_alg(data);
	p_random_alg_context = NULL;
}

SYSINIT(random_device, SI_SUB_RANDOM, SI_ORDER_THIRD, random_alg_context_ra_init_alg, NULL);
SYSUNINIT(random_device, SI_SUB_RANDOM, SI_ORDER_THIRD, random_alg_context_ra_deinit_alg, NULL);

static struct selinfo rsel;

/*
 * This is the read uio(9) interface for random(4).
 */
/* ARGSUSED */
static int
randomdev_read(struct cdev *dev __unused, struct uio *uio, int flags)
{

	return (READ_RANDOM_UIO(uio, (flags & O_NONBLOCK) != 0));
}

int
READ_RANDOM_UIO(struct uio *uio, bool nonblock)
{
	uint8_t *random_buf;
	int error, spamcount;
	ssize_t read_len, total_read, c;
	/* 16 MiB takes about 0.08 s CPU time on my 2017 AMD Zen CPU */
#define SIGCHK_PERIOD (16 * 1024 * 1024)
	const size_t sigchk_period = SIGCHK_PERIOD;

	CTASSERT(SIGCHK_PERIOD % PAGE_SIZE == 0);
#undef SIGCHK_PERIOD

	random_buf = malloc(PAGE_SIZE, M_ENTROPY, M_WAITOK);
	p_random_alg_context->ra_pre_read();
	error = 0;
	spamcount = 0;
	/* (Un)Blocking logic */
	while (!p_random_alg_context->ra_seeded()) {
		if (nonblock) {
			error = EWOULDBLOCK;
			break;
		}
		/* keep tapping away at the pre-read until we seed/unblock. */
		p_random_alg_context->ra_pre_read();
		/* Only bother the console every 10 seconds or so */
		if (spamcount == 0)
			printf("random: %s unblock wait\n", __func__);
		spamcount = (spamcount + 1)%100;
		error = tsleep(&random_alg_context, PCATCH, "randseed", hz/10);
		if (error == ERESTART || error == EINTR)
			break;
		/* Squash tsleep timeout condition */
		if (error == EWOULDBLOCK)
			error = 0;
		KASSERT(error == 0, ("unexpected tsleep error %d", error));
	}
	if (error == 0) {
		read_rate_increment((uio->uio_resid + sizeof(uint32_t))/sizeof(uint32_t));
		total_read = 0;
		while (uio->uio_resid && !error) {
			read_len = uio->uio_resid;
			/*
			 * Belt-and-braces.
			 * Round up the read length to a crypto block size multiple,
			 * which is what the underlying generator is expecting.
			 * See the random_buf size requirements in the Fortuna code.
			 */
			read_len = roundup(read_len, RANDOM_BLOCKSIZE);
			/* Work in chunks page-sized or less */
			read_len = MIN(read_len, PAGE_SIZE);
			p_random_alg_context->ra_read(random_buf, read_len);
			c = MIN(uio->uio_resid, read_len);
			/*
			 * uiomove() may yield the CPU before each 'c' bytes
			 * (up to PAGE_SIZE) are copied out.
			 */
			error = uiomove(random_buf, c, uio);
			total_read += c;
			/*
			 * Poll for signals every few MBs to avoid very long
			 * uninterruptible syscalls.
			 */
			if (error == 0 && uio->uio_resid != 0 &&
			    total_read % sigchk_period == 0) {
				error = tsleep_sbt(&random_alg_context, PCATCH,
				    "randrd", SBT_1NS, 0, C_HARDCLOCK);
				/* Squash tsleep timeout condition */
				if (error == EWOULDBLOCK)
					error = 0;
			}
		}
		if (error == ERESTART || error == EINTR)
			error = 0;
	}
	free(random_buf, M_ENTROPY);
	return (error);
}

/*-
 * Kernel API version of read_random().
 * This is similar to random_alg_read(),
 * except it doesn't interface with uio(9).
 * It cannot assumed that random_buf is a multiple of
 * RANDOM_BLOCKSIZE bytes.
 */
u_int
READ_RANDOM(void *random_buf, u_int len)
{
	u_int read_len;
	uint8_t local_buf[len + RANDOM_BLOCKSIZE];

	KASSERT(random_buf != NULL, ("No suitable random buffer in %s", __func__));
	p_random_alg_context->ra_pre_read();
	/* (Un)Blocking logic; if not seeded, return nothing. */
	if (p_random_alg_context->ra_seeded()) {
		read_rate_increment((len + sizeof(uint32_t))/sizeof(uint32_t));
		if (len > 0) {
			/*
			 * Belt-and-braces.
			 * Round up the read length to a crypto block size multiple,
			 * which is what the underlying generator is expecting.
			 */
			read_len = roundup(len, RANDOM_BLOCKSIZE);
			p_random_alg_context->ra_read(local_buf, read_len);
			memcpy(random_buf, local_buf, len);
		}
	} else
		len = 0;
	return (len);
}

static __inline void
randomdev_accumulate(uint8_t *buf, u_int count)
{
	static u_int destination = 0;
	static struct harvest_event event;
	static struct randomdev_hash hash;
	static uint32_t entropy_data[RANDOM_KEYSIZE_WORDS];
	uint32_t timestamp;
	int i;

	/* Extra timing here is helpful to scrape scheduler jitter entropy */
	randomdev_hash_init(&hash);
	timestamp = (uint32_t)get_cyclecount();
	randomdev_hash_iterate(&hash, &timestamp, sizeof(timestamp));
	randomdev_hash_iterate(&hash, buf, count);
	timestamp = (uint32_t)get_cyclecount();
	randomdev_hash_iterate(&hash, &timestamp, sizeof(timestamp));
	randomdev_hash_finish(&hash, entropy_data);
	explicit_bzero(&hash, sizeof(hash));
	for (i = 0; i < RANDOM_KEYSIZE_WORDS; i += sizeof(event.he_entropy)/sizeof(event.he_entropy[0])) {
		event.he_somecounter = (uint32_t)get_cyclecount();
		event.he_size = sizeof(event.he_entropy);
		event.he_source = RANDOM_CACHED;
		event.he_destination = destination++; /* Harmless cheating */
		memcpy(event.he_entropy, entropy_data + i, sizeof(event.he_entropy));
		p_random_alg_context->ra_event_processor(&event);
	}
	explicit_bzero(entropy_data, sizeof(entropy_data));
}

/* ARGSUSED */
static int
randomdev_write(struct cdev *dev __unused, struct uio *uio, int flags __unused)
{
	uint8_t *random_buf;
	int c, error = 0;
	ssize_t nbytes;

	random_buf = malloc(PAGE_SIZE, M_ENTROPY, M_WAITOK);
	nbytes = uio->uio_resid;
	while (uio->uio_resid > 0 && error == 0) {
		c = MIN(uio->uio_resid, PAGE_SIZE);
		error = uiomove(random_buf, c, uio);
		if (error)
			break;
		randomdev_accumulate(random_buf, c);
		tsleep(&random_alg_context, 0, "randwr", hz/10);
	}
	if (nbytes != uio->uio_resid && (error == ERESTART || error == EINTR))
		/* Partial write, not error. */
		error = 0;
	free(random_buf, M_ENTROPY);
	return (error);
}

/* ARGSUSED */
static int
randomdev_poll(struct cdev *dev __unused, int events, struct thread *td __unused)
{

	if (events & (POLLIN | POLLRDNORM)) {
		if (p_random_alg_context->ra_seeded())
			events &= (POLLIN | POLLRDNORM);
		else
			selrecord(td, &rsel);
	}
	return (events);
}

/* This will be called by the entropy processor when it seeds itself and becomes secure */
void
randomdev_unblock(void)
{

	selwakeuppri(&rsel, PUSER);
	wakeup(&random_alg_context);
	printf("random: unblocking device.\n");
	/* Do random(9) a favour while we are about it. */
	(void)atomic_cmpset_int(&arc4rand_iniseed_state, ARC4_ENTR_NONE, ARC4_ENTR_HAVE);
}

/* ARGSUSED */
static int
randomdev_ioctl(struct cdev *dev __unused, u_long cmd, caddr_t addr __unused,
    int flags __unused, struct thread *td __unused)
{
	int error = 0;

	switch (cmd) {
		/* Really handled in upper layer */
	case FIOASYNC:
	case FIONBIO:
		break;
	default:
		error = ENOTTY;
	}

	return (error);
}

void
random_source_register(struct random_source *rsource)
{
	struct random_sources *rrs;

	KASSERT(rsource != NULL, ("invalid input to %s", __func__));

	rrs = malloc(sizeof(*rrs), M_ENTROPY, M_WAITOK);
	rrs->rrs_source = rsource;

	random_harvest_register_source(rsource->rs_source);

	printf("random: registering fast source %s\n", rsource->rs_ident);
	LIST_INSERT_HEAD(&source_list, rrs, rrs_entries);
}

void
random_source_deregister(struct random_source *rsource)
{
	struct random_sources *rrs = NULL;

	KASSERT(rsource != NULL, ("invalid input to %s", __func__));

	random_harvest_deregister_source(rsource->rs_source);

	LIST_FOREACH(rrs, &source_list, rrs_entries)
		if (rrs->rrs_source == rsource) {
			LIST_REMOVE(rrs, rrs_entries);
			break;
		}
	if (rrs != NULL)
		free(rrs, M_ENTROPY);
}

static int
random_source_handler(SYSCTL_HANDLER_ARGS)
{
	struct random_sources *rrs;
	struct sbuf sbuf;
	int error, count;

	sbuf_new_for_sysctl(&sbuf, NULL, 64, req);
	count = 0;
	LIST_FOREACH(rrs, &source_list, rrs_entries) {
		sbuf_cat(&sbuf, (count++ ? ",'" : "'"));
		sbuf_cat(&sbuf, rrs->rrs_source->rs_ident);
		sbuf_cat(&sbuf, "'");
	}
	error = sbuf_finish(&sbuf);
	sbuf_delete(&sbuf);
	return (error);
}
SYSCTL_PROC(_kern_random, OID_AUTO, random_sources, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE,
	    NULL, 0, random_source_handler, "A",
	    "List of active fast entropy sources.");

/* ARGSUSED */
static int
randomdev_modevent(module_t mod __unused, int type, void *data __unused)
{
	int error = 0;

	switch (type) {
	case MOD_LOAD:
		printf("random: entropy device external interface\n");
		random_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &random_cdevsw,
		    RANDOM_UNIT, NULL, UID_ROOT, GID_WHEEL, 0644, "random");
		make_dev_alias(random_dev, "urandom"); /* compatibility */
		break;
	case MOD_UNLOAD:
		destroy_dev(random_dev);
		break;
	case MOD_SHUTDOWN:
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}

static moduledata_t randomdev_mod = {
	"random_device",
	randomdev_modevent,
	0
};

DECLARE_MODULE(random_device, randomdev_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);
MODULE_VERSION(random_device, 1);
MODULE_DEPEND(random_device, crypto, 1, 1, 1);
MODULE_DEPEND(random_device, random_harvestq, 1, 1, 1);
