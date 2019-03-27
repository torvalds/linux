/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2012-2013 Intel Corporation
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
#include <sys/bio.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/kthread.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/syscallsubr.h>
#include <sys/sysctl.h>
#include <sys/sysproto.h>
#include <sys/systm.h>
#include <sys/unistd.h>

#include <geom/geom.h>

#include "nvme_private.h"

struct nvme_io_test_thread {

	uint32_t		idx;
	struct nvme_namespace	*ns;
	enum nvme_nvm_opcode	opc;
	struct timeval		start;
	void			*buf;
	uint32_t		size;
	uint32_t		time;
	uint64_t		io_completed;
};

struct nvme_io_test_internal {

	struct nvme_namespace	*ns;
	enum nvme_nvm_opcode	opc;
	struct timeval		start;
	uint32_t		time;
	uint32_t		size;
	uint32_t		td_active;
	uint32_t		td_idx;
	uint32_t		flags;
	uint64_t		io_completed[NVME_TEST_MAX_THREADS];
};

static void
nvme_ns_bio_test_cb(struct bio *bio)
{
	struct mtx *mtx;

	mtx = mtx_pool_find(mtxpool_sleep, bio);
	mtx_lock(mtx);
	wakeup(bio);
	mtx_unlock(mtx);
}

static void
nvme_ns_bio_test(void *arg)
{
	struct nvme_io_test_internal	*io_test = arg;
	struct cdevsw			*csw;
	struct mtx			*mtx;
	struct bio			*bio;
	struct cdev			*dev;
	void				*buf;
	struct timeval			t;
	uint64_t			io_completed = 0, offset;
	uint32_t			idx;
	int				ref;

	buf = malloc(io_test->size, M_NVME, M_WAITOK);
	idx = atomic_fetchadd_int(&io_test->td_idx, 1);
	dev = io_test->ns->cdev;

	offset = idx * 2048 * nvme_ns_get_sector_size(io_test->ns);

	while (1) {

		bio = g_alloc_bio();

		memset(bio, 0, sizeof(*bio));
		bio->bio_cmd = (io_test->opc == NVME_OPC_READ) ?
		    BIO_READ : BIO_WRITE;
		bio->bio_done = nvme_ns_bio_test_cb;
		bio->bio_dev = dev;
		bio->bio_offset = offset;
		bio->bio_data = buf;
		bio->bio_bcount = io_test->size;

		if (io_test->flags & NVME_TEST_FLAG_REFTHREAD) {
			csw = dev_refthread(dev, &ref);
		} else
			csw = dev->si_devsw;

		mtx = mtx_pool_find(mtxpool_sleep, bio);
		mtx_lock(mtx);
		(*csw->d_strategy)(bio);
		msleep(bio, mtx, PRIBIO, "biotestwait", 0);
		mtx_unlock(mtx);

		if (io_test->flags & NVME_TEST_FLAG_REFTHREAD) {
			dev_relthread(dev, ref);
		}

		if ((bio->bio_flags & BIO_ERROR) || (bio->bio_resid > 0))
			break;

		g_destroy_bio(bio);

		io_completed++;

		getmicrouptime(&t);
		timevalsub(&t, &io_test->start);

		if (t.tv_sec >= io_test->time)
			break;

		offset += io_test->size;
		if ((offset + io_test->size) > nvme_ns_get_size(io_test->ns))
			offset = 0;
	}

	io_test->io_completed[idx] = io_completed;
	wakeup_one(io_test);

	free(buf, M_NVME);

	atomic_subtract_int(&io_test->td_active, 1);
	mb();

	kthread_exit();
}

static void
nvme_ns_io_test_cb(void *arg, const struct nvme_completion *cpl)
{
	struct nvme_io_test_thread	*tth = arg;
	struct timeval			t;

	tth->io_completed++;

	if (nvme_completion_is_error(cpl)) {
		printf("%s: error occurred\n", __func__);
		wakeup_one(tth);
		return;
	}

	getmicrouptime(&t);
	timevalsub(&t, &tth->start);

	if (t.tv_sec >= tth->time) {
		wakeup_one(tth);
		return;
	}

	switch (tth->opc) {
	case NVME_OPC_WRITE:
		nvme_ns_cmd_write(tth->ns, tth->buf, tth->idx * 2048,
		    tth->size/nvme_ns_get_sector_size(tth->ns),
		    nvme_ns_io_test_cb, tth);
		break;
	case NVME_OPC_READ:
		nvme_ns_cmd_read(tth->ns, tth->buf, tth->idx * 2048,
		    tth->size/nvme_ns_get_sector_size(tth->ns),
		    nvme_ns_io_test_cb, tth);
		break;
	default:
		break;
	}
}

static void
nvme_ns_io_test(void *arg)
{
	struct nvme_io_test_internal	*io_test = arg;
	struct nvme_io_test_thread	*tth;
	struct nvme_completion		cpl;
	int				error;

	tth = malloc(sizeof(*tth), M_NVME, M_WAITOK | M_ZERO);
	tth->ns = io_test->ns;
	tth->opc = io_test->opc;
	memcpy(&tth->start, &io_test->start, sizeof(tth->start));
	tth->buf = malloc(io_test->size, M_NVME, M_WAITOK);
	tth->size = io_test->size;
	tth->time = io_test->time;
	tth->idx = atomic_fetchadd_int(&io_test->td_idx, 1);

	memset(&cpl, 0, sizeof(cpl));

	nvme_ns_io_test_cb(tth, &cpl);

	error = tsleep(tth, 0, "test_wait", tth->time*hz*2);

	if (error)
		printf("%s: error = %d\n", __func__, error);

	io_test->io_completed[tth->idx] = tth->io_completed;
	wakeup_one(io_test);

	free(tth->buf, M_NVME);
	free(tth, M_NVME);

	atomic_subtract_int(&io_test->td_active, 1);
	mb();

	kthread_exit();
}

void
nvme_ns_test(struct nvme_namespace *ns, u_long cmd, caddr_t arg)
{
	struct nvme_io_test		*io_test;
	struct nvme_io_test_internal	*io_test_internal;
	void				(*fn)(void *);
	int				i;

	io_test = (struct nvme_io_test *)arg;

	if ((io_test->opc != NVME_OPC_READ) &&
	    (io_test->opc != NVME_OPC_WRITE))
		return;

	if (io_test->size % nvme_ns_get_sector_size(ns))
		return;

	io_test_internal = malloc(sizeof(*io_test_internal), M_NVME,
	    M_WAITOK | M_ZERO);
	io_test_internal->opc = io_test->opc;
	io_test_internal->ns = ns;
	io_test_internal->td_active = io_test->num_threads;
	io_test_internal->time = io_test->time;
	io_test_internal->size = io_test->size;
	io_test_internal->flags = io_test->flags;

	if (cmd == NVME_IO_TEST)
		fn = nvme_ns_io_test;
	else
		fn = nvme_ns_bio_test;

	getmicrouptime(&io_test_internal->start);

	for (i = 0; i < io_test->num_threads; i++)
		kthread_add(fn, io_test_internal,
		    NULL, NULL, 0, 0, "nvme_io_test[%d]", i);

	tsleep(io_test_internal, 0, "nvme_test", io_test->time * 2 * hz);

	while (io_test_internal->td_active > 0)
		DELAY(10);

	memcpy(io_test->io_completed, io_test_internal->io_completed,
	    sizeof(io_test->io_completed));

	free(io_test_internal, M_NVME);
}
