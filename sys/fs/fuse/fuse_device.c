/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007-2009 Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following disclaimer
 *   in the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of Google Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Copyright (C) 2005 Csaba Henk.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <sys/types.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/sx.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/sysctl.h>
#include <sys/poll.h>
#include <sys/selinfo.h>

#include "fuse.h"
#include "fuse_ipc.h"

#define FUSE_DEBUG_MODULE DEVICE
#include "fuse_debug.h"

static struct cdev *fuse_dev;

static d_open_t fuse_device_open;
static d_close_t fuse_device_close;
static d_poll_t fuse_device_poll;
static d_read_t fuse_device_read;
static d_write_t fuse_device_write;

static struct cdevsw fuse_device_cdevsw = {
	.d_open = fuse_device_open,
	.d_close = fuse_device_close,
	.d_name = "fuse",
	.d_poll = fuse_device_poll,
	.d_read = fuse_device_read,
	.d_write = fuse_device_write,
	.d_version = D_VERSION,
};

/****************************
 *
 * >>> Fuse device op defs
 *
 ****************************/

static void
fdata_dtor(void *arg)
{
	struct fuse_data *fdata;

	fdata = arg;
	fdata_trydestroy(fdata);
}

/*
 * Resources are set up on a per-open basis
 */
static int
fuse_device_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct fuse_data *fdata;
	int error;

	FS_DEBUG("device %p\n", dev);

	fdata = fdata_alloc(dev, td->td_ucred);
	error = devfs_set_cdevpriv(fdata, fdata_dtor);
	if (error != 0)
		fdata_trydestroy(fdata);
	else
		FS_DEBUG("%s: device opened by thread %d.\n", dev->si_name,
		    td->td_tid);
	return (error);
}

static int
fuse_device_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	struct fuse_data *data;
	struct fuse_ticket *tick;
	int error;

	error = devfs_get_cdevpriv((void **)&data);
	if (error != 0)
		return (error);
	if (!data)
		panic("no fuse data upon fuse device close");
	fdata_set_dead(data);

	FUSE_LOCK();
	fuse_lck_mtx_lock(data->aw_mtx);
	/* wakup poll()ers */
	selwakeuppri(&data->ks_rsel, PZERO + 1);
	/* Don't let syscall handlers wait in vain */
	while ((tick = fuse_aw_pop(data))) {
		fuse_lck_mtx_lock(tick->tk_aw_mtx);
		fticket_set_answered(tick);
		tick->tk_aw_errno = ENOTCONN;
		wakeup(tick);
		fuse_lck_mtx_unlock(tick->tk_aw_mtx);
		FUSE_ASSERT_AW_DONE(tick);
		fuse_ticket_drop(tick);
	}
	fuse_lck_mtx_unlock(data->aw_mtx);
	FUSE_UNLOCK();

	FS_DEBUG("%s: device closed by thread %d.\n", dev->si_name, td->td_tid);
	return (0);
}

int
fuse_device_poll(struct cdev *dev, int events, struct thread *td)
{
	struct fuse_data *data;
	int error, revents = 0;

	error = devfs_get_cdevpriv((void **)&data);
	if (error != 0)
		return (events &
		    (POLLHUP|POLLIN|POLLRDNORM|POLLOUT|POLLWRNORM));

	if (events & (POLLIN | POLLRDNORM)) {
		fuse_lck_mtx_lock(data->ms_mtx);
		if (fdata_get_dead(data) || STAILQ_FIRST(&data->ms_head))
			revents |= events & (POLLIN | POLLRDNORM);
		else
			selrecord(td, &data->ks_rsel);
		fuse_lck_mtx_unlock(data->ms_mtx);
	}
	if (events & (POLLOUT | POLLWRNORM)) {
		revents |= events & (POLLOUT | POLLWRNORM);
	}
	return (revents);
}

/*
 * fuse_device_read hangs on the queue of VFS messages.
 * When it's notified that there is a new one, it picks that and
 * passes up to the daemon
 */
int
fuse_device_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	int err;
	struct fuse_data *data;
	struct fuse_ticket *tick;
	void *buf[] = {NULL, NULL, NULL};
	int buflen[3];
	int i;

	FS_DEBUG("fuse device being read on thread %d\n", uio->uio_td->td_tid);

	err = devfs_get_cdevpriv((void **)&data);
	if (err != 0)
		return (err);

	fuse_lck_mtx_lock(data->ms_mtx);
again:
	if (fdata_get_dead(data)) {
		FS_DEBUG2G("we know early on that reader should be kicked so we don't wait for news\n");
		fuse_lck_mtx_unlock(data->ms_mtx);
		return (ENODEV);
	}
	if (!(tick = fuse_ms_pop(data))) {
		/* check if we may block */
		if (ioflag & O_NONBLOCK) {
			/* get outa here soon */
			fuse_lck_mtx_unlock(data->ms_mtx);
			return (EAGAIN);
		} else {
			err = msleep(data, &data->ms_mtx, PCATCH, "fu_msg", 0);
			if (err != 0) {
				fuse_lck_mtx_unlock(data->ms_mtx);
				return (fdata_get_dead(data) ? ENODEV : err);
			}
			tick = fuse_ms_pop(data);
		}
	}
	if (!tick) {
		/*
		 * We can get here if fuse daemon suddenly terminates,
		 * eg, by being hit by a SIGKILL
		 * -- and some other cases, too, tho not totally clear, when
		 * (cv_signal/wakeup_one signals the whole process ?)
		 */
		FS_DEBUG("no message on thread #%d\n", uio->uio_td->td_tid);
		goto again;
	}
	fuse_lck_mtx_unlock(data->ms_mtx);

	if (fdata_get_dead(data)) {
		/*
		 * somebody somewhere -- eg., umount routine --
		 * wants this liaison finished off
		 */
		FS_DEBUG2G("reader is to be sacked\n");
		if (tick) {
			FS_DEBUG2G("weird -- \"kick\" is set tho there is message\n");
			FUSE_ASSERT_MS_DONE(tick);
			fuse_ticket_drop(tick);
		}
		return (ENODEV);	/* This should make the daemon get off
					 * of us */
	}
	FS_DEBUG("message got on thread #%d\n", uio->uio_td->td_tid);

	KASSERT(tick->tk_ms_bufdata || tick->tk_ms_bufsize == 0,
	    ("non-null buf pointer with positive size"));

	switch (tick->tk_ms_type) {
	case FT_M_FIOV:
		buf[0] = tick->tk_ms_fiov.base;
		buflen[0] = tick->tk_ms_fiov.len;
		break;
	case FT_M_BUF:
		buf[0] = tick->tk_ms_fiov.base;
		buflen[0] = tick->tk_ms_fiov.len;
		buf[1] = tick->tk_ms_bufdata;
		buflen[1] = tick->tk_ms_bufsize;
		break;
	default:
		panic("unknown message type for fuse_ticket %p", tick);
	}

	for (i = 0; buf[i]; i++) {
		/*
		 * Why not ban mercilessly stupid daemons who can't keep up
		 * with us? (There is no much use of a partial read here...)
		 */
		/*
		 * XXX note that in such cases Linux FUSE throws EIO at the
		 * syscall invoker and stands back to the message queue. The
		 * rationale should be made clear (and possibly adopt that
		 * behaviour). Keeping the current scheme at least makes
		 * fallacy as loud as possible...
		 */
		if (uio->uio_resid < buflen[i]) {
			fdata_set_dead(data);
			FS_DEBUG2G("daemon is stupid, kick it off...\n");
			err = ENODEV;
			break;
		}
		err = uiomove(buf[i], buflen[i], uio);
		if (err)
			break;
	}

	FUSE_ASSERT_MS_DONE(tick);
	fuse_ticket_drop(tick);

	return (err);
}

static inline int
fuse_ohead_audit(struct fuse_out_header *ohead, struct uio *uio)
{
	FS_DEBUG("Out header -- len: %i, error: %i, unique: %llu; iovecs: %d\n",
	    ohead->len, ohead->error, (unsigned long long)ohead->unique,
	    uio->uio_iovcnt);

	if (uio->uio_resid + sizeof(struct fuse_out_header) != ohead->len) {
		FS_DEBUG("Format error: body size differs from size claimed by header\n");
		return (EINVAL);
	}
	if (uio->uio_resid && ohead->error) {
		FS_DEBUG("Format error: non zero error but message had a body\n");
		return (EINVAL);
	}
	/* Sanitize the linuxism of negative errnos */
	ohead->error = -(ohead->error);

	return (0);
}

/*
 * fuse_device_write first reads the header sent by the daemon.
 * If that's OK, looks up ticket/callback node by the unique id seen in header.
 * If the callback node contains a handler function, the uio is passed over
 * that.
 */
static int
fuse_device_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct fuse_out_header ohead;
	int err = 0;
	struct fuse_data *data;
	struct fuse_ticket *tick, *x_tick;
	int found = 0;

	FS_DEBUG("resid: %zd, iovcnt: %d, thread: %d\n",
	    uio->uio_resid, uio->uio_iovcnt, uio->uio_td->td_tid);

	err = devfs_get_cdevpriv((void **)&data);
	if (err != 0)
		return (err);

	if (uio->uio_resid < sizeof(struct fuse_out_header)) {
		FS_DEBUG("got less than a header!\n");
		fdata_set_dead(data);
		return (EINVAL);
	}
	if ((err = uiomove(&ohead, sizeof(struct fuse_out_header), uio)) != 0)
		return (err);

	/*
	 * We check header information (which is redundant) and compare it
	 * with what we see. If we see some inconsistency we discard the
	 * whole answer and proceed on as if it had never existed. In
	 * particular, no pretender will be woken up, regardless the
	 * "unique" value in the header.
	 */
	if ((err = fuse_ohead_audit(&ohead, uio))) {
		fdata_set_dead(data);
		return (err);
	}
	/* Pass stuff over to callback if there is one installed */

	/* Looking for ticket with the unique id of header */
	fuse_lck_mtx_lock(data->aw_mtx);
	TAILQ_FOREACH_SAFE(tick, &data->aw_head, tk_aw_link,
	    x_tick) {
		FS_DEBUG("bumped into callback #%llu\n",
		    (unsigned long long)tick->tk_unique);
		if (tick->tk_unique == ohead.unique) {
			found = 1;
			fuse_aw_remove(tick);
			break;
		}
	}
	fuse_lck_mtx_unlock(data->aw_mtx);

	if (found) {
		if (tick->tk_aw_handler) {
			/*
			 * We found a callback with proper handler. In this
			 * case the out header will be 0wnd by the callback,
			 * so the fun of freeing that is left for her.
			 * (Then, by all chance, she'll just get that's done
			 * via ticket_drop(), so no manual mucking
			 * around...)
			 */
			FS_DEBUG("pass ticket to a callback\n");
			memcpy(&tick->tk_aw_ohead, &ohead, sizeof(ohead));
			err = tick->tk_aw_handler(tick, uio);
		} else {
			/* pretender doesn't wanna do anything with answer */
			FS_DEBUG("stuff devalidated, so we drop it\n");
		}

		/*
		 * As aw_mtx was not held during the callback execution the
		 * ticket may have been inserted again.  However, this is safe
		 * because fuse_ticket_drop() will deal with refcount anyway.
		 */
		fuse_ticket_drop(tick);
	} else {
		/* no callback at all! */
		FS_DEBUG("erhm, no handler for this response\n");
		err = EINVAL;
	}

	return (err);
}

int
fuse_device_init(void)
{

	fuse_dev = make_dev(&fuse_device_cdevsw, 0, UID_ROOT, GID_OPERATOR,
	    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, "fuse");
	if (fuse_dev == NULL)
		return (ENOMEM);
	return (0);
}

void
fuse_device_destroy(void)
{

	MPASS(fuse_dev != NULL);
	destroy_dev(fuse_dev);
}
