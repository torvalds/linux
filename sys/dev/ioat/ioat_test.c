/*-
 * Copyright (C) 2012 Intel Corporation
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/ioccom.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/stdarg.h>
#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>

#include "ioat.h"
#include "ioat_hw.h"
#include "ioat_internal.h"
#include "ioat_test.h"

#ifndef time_after
#define	time_after(a,b)		((long)(b) - (long)(a) < 0)
#endif

MALLOC_DEFINE(M_IOAT_TEST, "ioat_test", "ioat test allocations");

#define	IOAT_MAX_BUFS	256

struct test_transaction {
	void			*buf[IOAT_MAX_BUFS];
	uint32_t		length;
	uint32_t		depth;
	struct ioat_test	*test;
	TAILQ_ENTRY(test_transaction)	entry;
};

#define	IT_LOCK()	mtx_lock(&ioat_test_lk)
#define	IT_UNLOCK()	mtx_unlock(&ioat_test_lk)
#define	IT_ASSERT()	mtx_assert(&ioat_test_lk, MA_OWNED)
static struct mtx ioat_test_lk;
MTX_SYSINIT(ioat_test_lk, &ioat_test_lk, "test coordination mtx", MTX_DEF);

static int g_thread_index = 1;
static struct cdev *g_ioat_cdev = NULL;

#define	ioat_test_log(v, ...)	_ioat_test_log((v), "ioat_test: " __VA_ARGS__)
static void _ioat_test_log(int verbosity, const char *fmt, ...);

static void
ioat_test_transaction_destroy(struct test_transaction *tx)
{
	struct ioat_test *test;
	int i;

	test = tx->test;

	for (i = 0; i < IOAT_MAX_BUFS; i++) {
		if (tx->buf[i] != NULL) {
			if (test->testkind == IOAT_TEST_DMA_8K)
				free(tx->buf[i], M_IOAT_TEST);
			else
				contigfree(tx->buf[i], tx->length, M_IOAT_TEST);
			tx->buf[i] = NULL;
		}
	}

	free(tx, M_IOAT_TEST);
}

static struct
test_transaction *ioat_test_transaction_create(struct ioat_test *test,
    unsigned num_buffers)
{
	struct test_transaction *tx;
	unsigned i;

	tx = malloc(sizeof(*tx), M_IOAT_TEST, M_NOWAIT | M_ZERO);
	if (tx == NULL)
		return (NULL);

	tx->length = test->buffer_size;

	for (i = 0; i < num_buffers; i++) {
		if (test->testkind == IOAT_TEST_DMA_8K)
			tx->buf[i] = malloc(test->buffer_size, M_IOAT_TEST,
			    M_NOWAIT);
		else
			tx->buf[i] = contigmalloc(test->buffer_size,
			    M_IOAT_TEST, M_NOWAIT, 0, BUS_SPACE_MAXADDR,
			    PAGE_SIZE, 0);

		if (tx->buf[i] == NULL) {
			ioat_test_transaction_destroy(tx);
			return (NULL);
		}
	}
	return (tx);
}

static void
dump_hex(void *p, size_t chunks)
{
	size_t i, j;

	for (i = 0; i < chunks; i++) {
		for (j = 0; j < 8; j++)
			printf("%08x ", ((uint32_t *)p)[i * 8 + j]);
		printf("\n");
	}
}

static bool
ioat_compare_ok(struct test_transaction *tx)
{
	struct ioat_test *test;
	char *dst, *src;
	uint32_t i, j;

	test = tx->test;

	for (i = 0; i < tx->depth; i++) {
		dst = tx->buf[2 * i + 1];
		src = tx->buf[2 * i];

		if (test->testkind == IOAT_TEST_FILL) {
			for (j = 0; j < tx->length; j += sizeof(uint64_t)) {
				if (memcmp(src, &dst[j],
					MIN(sizeof(uint64_t), tx->length - j))
				    != 0)
					return (false);
			}
		} else if (test->testkind == IOAT_TEST_DMA) {
			if (memcmp(src, dst, tx->length) != 0)
				return (false);
		} else if (test->testkind == IOAT_TEST_RAW_DMA) {
			if (test->raw_write)
				dst = test->raw_vtarget;
			dump_hex(dst, tx->length / 32);
		}
	}
	return (true);
}

static void
ioat_dma_test_callback(void *arg, int error)
{
	struct test_transaction *tx;
	struct ioat_test *test;

	if (error != 0)
		ioat_test_log(0, "%s: Got error: %d\n", __func__, error);

	tx = arg;
	test = tx->test;

	if (test->verify && !ioat_compare_ok(tx)) {
		ioat_test_log(0, "miscompare found\n");
		atomic_add_32(&test->status[IOAT_TEST_MISCOMPARE], tx->depth);
	} else if (!test->too_late)
		atomic_add_32(&test->status[IOAT_TEST_OK], tx->depth);

	IT_LOCK();
	TAILQ_REMOVE(&test->pend_q, tx, entry);
	TAILQ_INSERT_TAIL(&test->free_q, tx, entry);
	wakeup(&test->free_q);
	IT_UNLOCK();
}

static int
ioat_test_prealloc_memory(struct ioat_test *test, int index)
{
	uint32_t i, j, k;
	struct test_transaction *tx;

	for (i = 0; i < test->transactions; i++) {
		tx = ioat_test_transaction_create(test, test->chain_depth * 2);
		if (tx == NULL) {
			ioat_test_log(0, "tx == NULL - memory exhausted\n");
			test->status[IOAT_TEST_NO_MEMORY]++;
			return (ENOMEM);
		}

		TAILQ_INSERT_HEAD(&test->free_q, tx, entry);

		tx->test = test;
		tx->depth = test->chain_depth;

		/* fill in source buffers */
		for (j = 0; j < (tx->length / sizeof(uint32_t)); j++) {
			uint32_t val = j + (index << 28);

			for (k = 0; k < test->chain_depth; k++) {
				((uint32_t *)tx->buf[2*k])[j] = ~val;
				((uint32_t *)tx->buf[2*k+1])[j] = val;
			}
		}
	}
	return (0);
}

static void
ioat_test_release_memory(struct ioat_test *test)
{
	struct test_transaction *tx, *s;

	TAILQ_FOREACH_SAFE(tx, &test->free_q, entry, s)
		ioat_test_transaction_destroy(tx);
	TAILQ_INIT(&test->free_q);

	TAILQ_FOREACH_SAFE(tx, &test->pend_q, entry, s)
		ioat_test_transaction_destroy(tx);
	TAILQ_INIT(&test->pend_q);
}

static void
ioat_test_submit_1_tx(struct ioat_test *test, bus_dmaengine_t dma)
{
	struct test_transaction *tx;
	struct bus_dmadesc *desc;
	bus_dmaengine_callback_t cb;
	bus_addr_t src, dest;
	uint64_t fillpattern;
	uint32_t i, flags;

	desc = NULL;

	IT_LOCK();
	while (TAILQ_EMPTY(&test->free_q))
		msleep(&test->free_q, &ioat_test_lk, 0, "test_submit", 0);

	tx = TAILQ_FIRST(&test->free_q);
	TAILQ_REMOVE(&test->free_q, tx, entry);
	TAILQ_INSERT_HEAD(&test->pend_q, tx, entry);
	IT_UNLOCK();

	if (test->testkind != IOAT_TEST_MEMCPY)
		ioat_acquire(dma);
	for (i = 0; i < tx->depth; i++) {
		if (test->testkind == IOAT_TEST_MEMCPY) {
			memcpy(tx->buf[2 * i + 1], tx->buf[2 * i], tx->length);
			if (i == tx->depth - 1)
				ioat_dma_test_callback(tx, 0);
			continue;
		}

		src = vtophys((vm_offset_t)tx->buf[2*i]);
		dest = vtophys((vm_offset_t)tx->buf[2*i+1]);

		if (test->testkind == IOAT_TEST_RAW_DMA) {
			if (test->raw_write)
				dest = test->raw_target;
			else
				src = test->raw_target;
		}

		if (i == tx->depth - 1) {
			cb = ioat_dma_test_callback;
			flags = DMA_INT_EN;
		} else {
			cb = NULL;
			flags = 0;
		}

		if (test->testkind == IOAT_TEST_DMA ||
		    test->testkind == IOAT_TEST_RAW_DMA)
			desc = ioat_copy(dma, dest, src, tx->length, cb, tx,
			    flags);
		else if (test->testkind == IOAT_TEST_FILL) {
			fillpattern = *(uint64_t *)tx->buf[2*i];
			desc = ioat_blockfill(dma, dest, fillpattern,
			    tx->length, cb, tx, flags);
		} else if (test->testkind == IOAT_TEST_DMA_8K) {
			bus_addr_t src2, dst2;

			src2 = vtophys((vm_offset_t)tx->buf[2*i] + PAGE_SIZE);
			dst2 = vtophys((vm_offset_t)tx->buf[2*i+1] + PAGE_SIZE);

			desc = ioat_copy_8k_aligned(dma, dest, dst2, src, src2,
			    cb, tx, flags);
		}
		if (desc == NULL)
			break;
	}
	if (test->testkind == IOAT_TEST_MEMCPY)
		return;
	ioat_release(dma);

	/*
	 * We couldn't issue an IO -- either the device is being detached or
	 * the HW reset.  Essentially spin until the device comes back up or
	 * our timer expires.
	 */
	if (desc == NULL && tx->depth > 0) {
		atomic_add_32(&test->status[IOAT_TEST_NO_DMA_ENGINE], tx->depth);
		IT_LOCK();
		TAILQ_REMOVE(&test->pend_q, tx, entry);
		TAILQ_INSERT_HEAD(&test->free_q, tx, entry);
		IT_UNLOCK();
	}
}

static void
ioat_dma_test(void *arg)
{
	struct ioat_softc *ioat;
	struct ioat_test *test;
	bus_dmaengine_t dmaengine;
	uint32_t loops;
	int index, rc, start, end, error;

	test = arg;
	memset(__DEVOLATILE(void *, test->status), 0, sizeof(test->status));

	if (test->testkind == IOAT_TEST_DMA_8K &&
	    test->buffer_size != 2 * PAGE_SIZE) {
		ioat_test_log(0, "Asked for 8k test and buffer size isn't 8k\n");
		test->status[IOAT_TEST_INVALID_INPUT]++;
		return;
	}

	if (test->buffer_size > 1024 * 1024) {
		ioat_test_log(0, "Buffer size too large >1MB\n");
		test->status[IOAT_TEST_NO_MEMORY]++;
		return;
	}

	if (test->chain_depth * 2 > IOAT_MAX_BUFS) {
		ioat_test_log(0, "Depth too large (> %u)\n",
		    (unsigned)IOAT_MAX_BUFS / 2);
		test->status[IOAT_TEST_NO_MEMORY]++;
		return;
	}

	if (btoc((uint64_t)test->buffer_size * test->chain_depth *
	    test->transactions) > (physmem / 4)) {
		ioat_test_log(0, "Sanity check failed -- test would "
		    "use more than 1/4 of phys mem.\n");
		test->status[IOAT_TEST_NO_MEMORY]++;
		return;
	}

	if ((uint64_t)test->transactions * test->chain_depth > (1<<16)) {
		ioat_test_log(0, "Sanity check failed -- test would "
		    "use more than available IOAT ring space.\n");
		test->status[IOAT_TEST_NO_MEMORY]++;
		return;
	}

	if (test->testkind >= IOAT_NUM_TESTKINDS) {
		ioat_test_log(0, "Invalid kind %u\n",
		    (unsigned)test->testkind);
		test->status[IOAT_TEST_INVALID_INPUT]++;
		return;
	}

	dmaengine = ioat_get_dmaengine(test->channel_index, M_NOWAIT);
	if (dmaengine == NULL) {
		ioat_test_log(0, "Couldn't acquire dmaengine\n");
		test->status[IOAT_TEST_NO_DMA_ENGINE]++;
		return;
	}
	ioat = to_ioat_softc(dmaengine);

	if (test->testkind == IOAT_TEST_FILL &&
	    (ioat->capabilities & IOAT_DMACAP_BFILL) == 0)
	{
		ioat_test_log(0,
		    "Hardware doesn't support block fill, aborting test\n");
		test->status[IOAT_TEST_INVALID_INPUT]++;
		goto out;
	}

	if (test->coalesce_period > ioat->intrdelay_max) {
		ioat_test_log(0,
		    "Hardware doesn't support intrdelay of %u us.\n",
		    (unsigned)test->coalesce_period);
		test->status[IOAT_TEST_INVALID_INPUT]++;
		goto out;
	}
	error = ioat_set_interrupt_coalesce(dmaengine, test->coalesce_period);
	if (error == ENODEV && test->coalesce_period == 0)
		error = 0;
	if (error != 0) {
		ioat_test_log(0, "ioat_set_interrupt_coalesce: %d\n", error);
		test->status[IOAT_TEST_INVALID_INPUT]++;
		goto out;
	}

	if (test->zero_stats)
		memset(&ioat->stats, 0, sizeof(ioat->stats));

	if (test->testkind == IOAT_TEST_RAW_DMA) {
		if (test->raw_is_virtual) {
			test->raw_vtarget = (void *)test->raw_target;
			test->raw_target = vtophys(test->raw_vtarget);
		} else {
			test->raw_vtarget = pmap_mapdev(test->raw_target,
			    test->buffer_size);
		}
	}

	index = g_thread_index++;
	TAILQ_INIT(&test->free_q);
	TAILQ_INIT(&test->pend_q);

	if (test->duration == 0)
		ioat_test_log(1, "Thread %d: num_loops remaining: 0x%08x\n",
		    index, test->transactions);
	else
		ioat_test_log(1, "Thread %d: starting\n", index);

	rc = ioat_test_prealloc_memory(test, index);
	if (rc != 0) {
		ioat_test_log(0, "prealloc_memory: %d\n", rc);
		goto out;
	}
	wmb();

	test->too_late = false;
	start = ticks;
	end = start + (((sbintime_t)test->duration * hz) / 1000);

	for (loops = 0;; loops++) {
		if (test->duration == 0 && loops >= test->transactions)
			break;
		else if (test->duration != 0 && time_after(ticks, end)) {
			test->too_late = true;
			break;
		}

		ioat_test_submit_1_tx(test, dmaengine);
	}

	ioat_test_log(1, "Test Elapsed: %d ticks (overrun %d), %d sec.\n",
	    ticks - start, ticks - end, (ticks - start) / hz);

	IT_LOCK();
	while (!TAILQ_EMPTY(&test->pend_q))
		msleep(&test->free_q, &ioat_test_lk, 0, "ioattestcompl", hz);
	IT_UNLOCK();

	ioat_test_log(1, "Test Elapsed2: %d ticks (overrun %d), %d sec.\n",
	    ticks - start, ticks - end, (ticks - start) / hz);

	ioat_test_release_memory(test);
out:
	if (test->testkind == IOAT_TEST_RAW_DMA && !test->raw_is_virtual)
		pmap_unmapdev((vm_offset_t)test->raw_vtarget,
		    test->buffer_size);
	ioat_put_dmaengine(dmaengine);
}

static int
ioat_test_open(struct cdev *dev, int flags, int fmt, struct thread *td)
{

	return (0);
}

static int
ioat_test_close(struct cdev *dev, int flags, int fmt, struct thread *td)
{

	return (0);
}

static int
ioat_test_ioctl(struct cdev *dev, unsigned long cmd, caddr_t arg, int flag,
    struct thread *td)
{

	switch (cmd) {
	case IOAT_DMATEST:
		ioat_dma_test(arg);
		break;
	default:
		return (EINVAL);
	}
	return (0);
}

static struct cdevsw ioat_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	0,
	.d_open =	ioat_test_open,
	.d_close =	ioat_test_close,
	.d_ioctl =	ioat_test_ioctl,
	.d_name =	"ioat_test",
};

static int
enable_ioat_test(bool enable)
{

	mtx_assert(&Giant, MA_OWNED);

	if (enable && g_ioat_cdev == NULL) {
		g_ioat_cdev = make_dev(&ioat_cdevsw, 0, UID_ROOT, GID_WHEEL,
		    0600, "ioat_test");
	} else if (!enable && g_ioat_cdev != NULL) {
		destroy_dev(g_ioat_cdev);
		g_ioat_cdev = NULL;
	}
	return (0);
}

static int
sysctl_enable_ioat_test(SYSCTL_HANDLER_ARGS)
{
	int error, enabled;

	enabled = (g_ioat_cdev != NULL);
	error = sysctl_handle_int(oidp, &enabled, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	enable_ioat_test(enabled);
	return (0);
}
SYSCTL_PROC(_hw_ioat, OID_AUTO, enable_ioat_test, CTLTYPE_INT | CTLFLAG_RW,
    0, 0, sysctl_enable_ioat_test, "I",
    "Non-zero: Enable the /dev/ioat_test device");

void
ioat_test_attach(void)
{
	char *val;

	val = kern_getenv("hw.ioat.enable_ioat_test");
	if (val != NULL && strcmp(val, "0") != 0) {
		mtx_lock(&Giant);
		enable_ioat_test(true);
		mtx_unlock(&Giant);
	}
	freeenv(val);
}

void
ioat_test_detach(void)
{

	mtx_lock(&Giant);
	enable_ioat_test(false);
	mtx_unlock(&Giant);
}

static void
_ioat_test_log(int verbosity, const char *fmt, ...)
{
	va_list argp;

	if (verbosity > g_ioat_debug_level)
		return;

	va_start(argp, fmt);
	vprintf(fmt, argp);
	va_end(argp);
}
