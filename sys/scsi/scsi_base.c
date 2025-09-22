/*	$OpenBSD: scsi_base.c,v 1.284 2024/09/04 07:54:53 mglocker Exp $	*/
/*	$NetBSD: scsi_base.c,v 1.43 1997/04/02 02:29:36 mycroft Exp $	*/

/*
 * Copyright (c) 1994, 1995, 1997 Charles M. Hannum.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Charles M. Hannum.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 */

/*
 * Originally written by Julian Elischer (julian@dialix.oz.au)
 * Detailed SCSI error printing Copyright 1997 by Matthew Jacob.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/uio.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/pool.h>
#include <sys/task.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_debug.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>

static __inline void asc2ascii(u_int8_t, u_int8_t ascq, char *result,
    size_t len);
int	scsi_xs_error(struct scsi_xfer *);
char   *scsi_decode_sense(struct scsi_sense_data *, int);

void	scsi_xs_sync_done(struct scsi_xfer *);

/* Values for flag parameter to scsi_decode_sense. */
#define	DECODE_SENSE_KEY	1
#define	DECODE_ASC_ASCQ		2
#define DECODE_SKSV		3

struct pool		scsi_xfer_pool;
struct pool		scsi_plug_pool;

struct scsi_plug {
	struct task		task;
	struct scsibus_softc	*sb;
	int			target;
	int			lun;
	int			how;
};

void	scsi_plug_probe(void *);
void	scsi_plug_detach(void *);

struct scsi_xfer *	scsi_xs_io(struct scsi_link *, void *, int);

int			scsi_ioh_pending(struct scsi_iopool *);
struct scsi_iohandler *	scsi_ioh_deq(struct scsi_iopool *);

void			scsi_xsh_runqueue(struct scsi_link *);
void			scsi_xsh_ioh(void *, void *);

int			scsi_link_open(struct scsi_link *);
void			scsi_link_close(struct scsi_link *);

void *			scsi_iopool_get(struct scsi_iopool *);
void			scsi_iopool_put(struct scsi_iopool *, void *);

/* Various helper functions for scsi_do_mode_sense() */
int			scsi_mode_sense(struct scsi_link *, int,
			    union scsi_mode_sense_buf *, int);
int			scsi_mode_sense_big(struct scsi_link *, int,
			    union scsi_mode_sense_buf *, int);
void *			scsi_mode_sense_page(struct scsi_mode_header *, int,
			    int);
void *			scsi_mode_sense_big_page(struct scsi_mode_header_big *,
			    int, int);

/* ioh/xsh queue state */
#define RUNQ_IDLE	0
#define RUNQ_LINKQ	1
#define RUNQ_POOLQ	2

/* synchronous api for allocating an io. */
struct scsi_io_mover {
	struct mutex mtx;
	void *io;
	u_int done;
};
#define SCSI_IO_MOVER_INITIALIZER { MUTEX_INITIALIZER(IPL_BIO), NULL, 0 }

void scsi_move(struct scsi_io_mover *);
void scsi_move_done(void *, void *);

void scsi_io_get_done(void *, void *);
void scsi_xs_get_done(void *, void *);

/*
 * Called when a scsibus is attached to initialize global data.
 */
void
scsi_init(void)
{
	static int scsi_init_done;

	if (scsi_init_done)
		return;
	scsi_init_done = 1;

#if defined(SCSI_DELAY) && SCSI_DELAY > 0
	/* Historical. Older buses may need a moment to stabilize. */
	delay(1000000 * SCSI_DELAY);
#endif /* SCSI_DELAY && SCSI_DELAY > 0 */

	/* Initialize the scsi_xfer pool. */
	pool_init(&scsi_xfer_pool, sizeof(struct scsi_xfer), 0, IPL_BIO, 0,
	    "scxspl", NULL);
	pool_setlowat(&scsi_xfer_pool, 8);
	pool_prime(&scsi_xfer_pool, 8);
	pool_init(&scsi_plug_pool, sizeof(struct scsi_plug), 0, IPL_BIO, 0,
	    "scsiplug", NULL);
}

int
scsi_req_probe(struct scsibus_softc *sb, int target, int lun)
{
	struct scsi_plug *p;

	p = pool_get(&scsi_plug_pool, PR_NOWAIT);
	if (p == NULL)
		return ENOMEM;

	task_set(&p->task, scsi_plug_probe, p);
	p->sb = sb;
	p->target = target;
	p->lun = lun;

	task_add(systq, &p->task);

	return 0;
}

int
scsi_req_detach(struct scsibus_softc *sb, int target, int lun, int how)
{
	struct scsi_plug *p;

	p = pool_get(&scsi_plug_pool, PR_NOWAIT);
	if (p == NULL)
		return ENOMEM;

	task_set(&p->task, scsi_plug_detach, p);
	p->sb = sb;
	p->target = target;
	p->lun = lun;
	p->how = how;

	task_add(systq, &p->task);

	return 0;
}

void
scsi_plug_probe(void *xp)
{
	struct scsi_plug	*p = xp;
	struct scsibus_softc	*sb = p->sb;
	int			 target = p->target, lun = p->lun;

	pool_put(&scsi_plug_pool, p);

	scsi_probe(sb, target, lun);
}

void
scsi_plug_detach(void *xp)
{
	struct scsi_plug	*p = xp;
	struct scsibus_softc	*sb = p->sb;
	int			 target = p->target, lun = p->lun;
	int			 how = p->how;

	pool_put(&scsi_plug_pool, p);

	scsi_detach(sb, target, lun, how);
}

int
scsi_pending_start(struct mutex *mtx, u_int *running)
{
	int rv = 1;

	mtx_enter(mtx);
	(*running)++;
	if ((*running) > 1)
		rv = 0;
	mtx_leave(mtx);

	return rv;
}

int
scsi_pending_finish(struct mutex *mtx, u_int *running)
{
	int rv = 1;

	mtx_enter(mtx);
	(*running)--;
	if ((*running) > 0) {
		(*running) = 1;
		rv = 0;
	}
	mtx_leave(mtx);

	return rv;
}

void
scsi_iopool_init(struct scsi_iopool *iopl, void *iocookie,
    void *(*io_get)(void *), void (*io_put)(void *, void *))
{
	iopl->iocookie = iocookie;
	iopl->io_get = io_get;
	iopl->io_put = io_put;

	TAILQ_INIT(&iopl->queue);
	iopl->running = 0;
	mtx_init(&iopl->mtx, IPL_BIO);
}

void *
scsi_iopool_get(struct scsi_iopool *iopl)
{
	void *io;

	KERNEL_LOCK();
	io = iopl->io_get(iopl->iocookie);
	KERNEL_UNLOCK();

	return io;
}

void
scsi_iopool_put(struct scsi_iopool *iopl, void *io)
{
	KERNEL_LOCK();
	iopl->io_put(iopl->iocookie, io);
	KERNEL_UNLOCK();
}

void
scsi_iopool_destroy(struct scsi_iopool *iopl)
{
	struct scsi_runq	 sleepers = TAILQ_HEAD_INITIALIZER(sleepers);
	struct scsi_iohandler	*ioh = NULL;

	mtx_enter(&iopl->mtx);
	while ((ioh = TAILQ_FIRST(&iopl->queue)) != NULL) {
		TAILQ_REMOVE(&iopl->queue, ioh, q_entry);
		ioh->q_state = RUNQ_IDLE;

		if (ioh->handler == scsi_io_get_done)
			TAILQ_INSERT_TAIL(&sleepers, ioh, q_entry);
#ifdef DIAGNOSTIC
		else
			panic("scsi_iopool_destroy: scsi_iohandler on pool");
#endif /* DIAGNOSTIC */
	}
	mtx_leave(&iopl->mtx);

	while ((ioh = TAILQ_FIRST(&sleepers)) != NULL) {
		TAILQ_REMOVE(&sleepers, ioh, q_entry);
		ioh->handler(ioh->cookie, NULL);
	}
}

void *
scsi_default_get(void *iocookie)
{
	return SCSI_IOPOOL_POISON;
}

void
scsi_default_put(void *iocookie, void *io)
{
#ifdef DIAGNOSTIC
	if (io != SCSI_IOPOOL_POISON)
		panic("unexpected opening returned");
#endif /* DIAGNOSTIC */
}

/*
 * public interface to the ioh api.
 */

void
scsi_ioh_set(struct scsi_iohandler *ioh, struct scsi_iopool *iopl,
    void (*handler)(void *, void *), void *cookie)
{
	ioh->q_state = RUNQ_IDLE;
	ioh->pool = iopl;
	ioh->handler = handler;
	ioh->cookie = cookie;
}

int
scsi_ioh_add(struct scsi_iohandler *ioh)
{
	struct scsi_iopool	*iopl = ioh->pool;
	int			 rv = 0;

	mtx_enter(&iopl->mtx);
	switch (ioh->q_state) {
	case RUNQ_IDLE:
		TAILQ_INSERT_TAIL(&iopl->queue, ioh, q_entry);
		ioh->q_state = RUNQ_POOLQ;
		rv = 1;
		break;
#ifdef DIAGNOSTIC
	case RUNQ_POOLQ:
		break;
	default:
		panic("scsi_ioh_add: unexpected state %u", ioh->q_state);
#endif /* DIAGNOSTIC */
	}
	mtx_leave(&iopl->mtx);

	/* lets get some io up in the air */
	scsi_iopool_run(iopl);

	return rv;
}

int
scsi_ioh_del(struct scsi_iohandler *ioh)
{
	struct scsi_iopool	*iopl = ioh->pool;
	int			 rv = 0;

	mtx_enter(&iopl->mtx);
	switch (ioh->q_state) {
	case RUNQ_POOLQ:
		TAILQ_REMOVE(&iopl->queue, ioh, q_entry);
		ioh->q_state = RUNQ_IDLE;
		rv = 1;
		break;
#ifdef DIAGNOSTIC
	case RUNQ_IDLE:
		break;
	default:
		panic("scsi_ioh_del: unexpected state %u", ioh->q_state);
#endif /* DIAGNOSTIC */
	}
	mtx_leave(&iopl->mtx);

	return rv;
}

/*
 * internal iopool runqueue handling.
 */

struct scsi_iohandler *
scsi_ioh_deq(struct scsi_iopool *iopl)
{
	struct scsi_iohandler *ioh = NULL;

	mtx_enter(&iopl->mtx);
	ioh = TAILQ_FIRST(&iopl->queue);
	if (ioh != NULL) {
		TAILQ_REMOVE(&iopl->queue, ioh, q_entry);
		ioh->q_state = RUNQ_IDLE;
	}
	mtx_leave(&iopl->mtx);

	return ioh;
}

int
scsi_ioh_pending(struct scsi_iopool *iopl)
{
	int rv;

	mtx_enter(&iopl->mtx);
	rv = !TAILQ_EMPTY(&iopl->queue);
	mtx_leave(&iopl->mtx);

	return rv;
}

void
scsi_iopool_run(struct scsi_iopool *iopl)
{
	struct scsi_iohandler	*ioh;
	void			*io;

	if (!scsi_pending_start(&iopl->mtx, &iopl->running))
		return;
	do {
		while (scsi_ioh_pending(iopl)) {
			io = scsi_iopool_get(iopl);
			if (io == NULL)
				break;

			ioh = scsi_ioh_deq(iopl);
			if (ioh == NULL) {
				scsi_iopool_put(iopl, io);
				break;
			}

			ioh->handler(ioh->cookie, io);
		}
	} while (!scsi_pending_finish(&iopl->mtx, &iopl->running));
}

/*
 * move an io from a runq to a proc that's waiting for an io.
 */

void
scsi_move(struct scsi_io_mover *m)
{
	mtx_enter(&m->mtx);
	while (!m->done)
		msleep_nsec(m, &m->mtx, PRIBIO, "scsiiomv", INFSLP);
	mtx_leave(&m->mtx);
}

void
scsi_move_done(void *cookie, void *io)
{
	struct scsi_io_mover *m = cookie;

	mtx_enter(&m->mtx);
	m->io = io;
	m->done = 1;
	wakeup_one(m);
	mtx_leave(&m->mtx);
}

/*
 * synchronous api for allocating an io.
 */

void *
scsi_io_get(struct scsi_iopool *iopl, int flags)
{
	struct scsi_io_mover	 m = SCSI_IO_MOVER_INITIALIZER;
	struct scsi_iohandler	 ioh;
	void			*io;

	/* try and sneak an io off the backend immediately */
	io = scsi_iopool_get(iopl);
	if (io != NULL)
		return io;
	else if (ISSET(flags, SCSI_NOSLEEP))
		return NULL;

	/* otherwise sleep until we get one */
	scsi_ioh_set(&ioh, iopl, scsi_io_get_done, &m);
	scsi_ioh_add(&ioh);
	scsi_move(&m);

	return m.io;
}

void
scsi_io_get_done(void *cookie, void *io)
{
	scsi_move_done(cookie, io);
}

void
scsi_io_put(struct scsi_iopool *iopl, void *io)
{
	scsi_iopool_put(iopl, io);
	scsi_iopool_run(iopl);
}

/*
 * public interface to the xsh api.
 */

void
scsi_xsh_set(struct scsi_xshandler *xsh, struct scsi_link *link,
    void (*handler)(struct scsi_xfer *))
{
	scsi_ioh_set(&xsh->ioh, link->pool, scsi_xsh_ioh, xsh);

	xsh->link = link;
	xsh->handler = handler;
}

int
scsi_xsh_add(struct scsi_xshandler *xsh)
{
	struct scsi_link	*link = xsh->link;
	int			 rv = 0;

	if (ISSET(link->state, SDEV_S_DYING))
		return 0;

	mtx_enter(&link->pool->mtx);
	if (xsh->ioh.q_state == RUNQ_IDLE) {
		TAILQ_INSERT_TAIL(&link->queue, &xsh->ioh, q_entry);
		xsh->ioh.q_state = RUNQ_LINKQ;
		rv = 1;
	}
	mtx_leave(&link->pool->mtx);

	/* lets get some io up in the air */
	scsi_xsh_runqueue(link);

	return rv;
}

int
scsi_xsh_del(struct scsi_xshandler *xsh)
{
	struct scsi_link	*link = xsh->link;
	int			 rv = 1;

	mtx_enter(&link->pool->mtx);
	switch (xsh->ioh.q_state) {
	case RUNQ_IDLE:
		rv = 0;
		break;
	case RUNQ_LINKQ:
		TAILQ_REMOVE(&link->queue, &xsh->ioh, q_entry);
		break;
	case RUNQ_POOLQ:
		TAILQ_REMOVE(&link->pool->queue, &xsh->ioh, q_entry);
		link->pending--;
		if (ISSET(link->state, SDEV_S_DYING) && link->pending == 0)
			wakeup_one(&link->pending);
		break;
	default:
		panic("unexpected xsh state %u", xsh->ioh.q_state);
	}
	xsh->ioh.q_state = RUNQ_IDLE;
	mtx_leave(&link->pool->mtx);

	return rv;
}

/*
 * internal xs runqueue handling.
 */

void
scsi_xsh_runqueue(struct scsi_link *link)
{
	struct scsi_iohandler	*ioh;
	int			 runq;

	if (!scsi_pending_start(&link->pool->mtx, &link->running))
		return;
	do {
		runq = 0;

		mtx_enter(&link->pool->mtx);
		while (!ISSET(link->state, SDEV_S_DYING) &&
		    link->pending < link->openings &&
		    ((ioh = TAILQ_FIRST(&link->queue)) != NULL)) {
			link->pending++;

			TAILQ_REMOVE(&link->queue, ioh, q_entry);
			TAILQ_INSERT_TAIL(&link->pool->queue, ioh, q_entry);
			ioh->q_state = RUNQ_POOLQ;

			runq = 1;
		}
		mtx_leave(&link->pool->mtx);

		if (runq)
			scsi_iopool_run(link->pool);
	} while (!scsi_pending_finish(&link->pool->mtx, &link->running));
}

void
scsi_xsh_ioh(void *cookie, void *io)
{
	struct scsi_xshandler	*xsh = cookie;
	struct scsi_xfer	*xs;

	xs = scsi_xs_io(xsh->link, io, SCSI_NOSLEEP);
	if (xs == NULL) {
		/*
		 * in this situation we should queue things waiting for an
		 * xs and then give them xses when they were supposed be to
		 * returned to the pool.
		 */

		printf("scsi_xfer pool exhausted!\n");
		scsi_xsh_add(xsh);
		return;
	}

	xsh->handler(xs);
}

/*
 * Get a scsi transfer structure for the caller.
 * Go to the iopool backend for an "opening" and then attach an xs to it.
 */

struct scsi_xfer *
scsi_xs_get(struct scsi_link *link, int flags)
{
	struct scsi_xshandler	 xsh;
	struct scsi_io_mover	 m = SCSI_IO_MOVER_INITIALIZER;
	struct scsi_iopool	*iopl = link->pool;
	void			*io;

	if (ISSET(link->state, SDEV_S_DYING))
		return NULL;

	/* really custom xs handler to avoid scsi_xsh_ioh */
	scsi_ioh_set(&xsh.ioh, iopl, scsi_xs_get_done, &m);
	xsh.link = link;

	if (!scsi_link_open(link)) {
		if (ISSET(flags, SCSI_NOSLEEP))
			return NULL;

		scsi_xsh_add(&xsh);
		scsi_move(&m);
		if (m.io == NULL)
			return NULL;

		io = m.io;
	} else if ((io = scsi_iopool_get(iopl)) == NULL) {
		if (ISSET(flags, SCSI_NOSLEEP)) {
			scsi_link_close(link);
			return NULL;
		}

		scsi_ioh_add(&xsh.ioh);
		scsi_move(&m);
		if (m.io == NULL)
			return NULL;

		io = m.io;
	}

	return scsi_xs_io(link, io, flags);
}

void
scsi_xs_get_done(void *cookie, void *io)
{
	scsi_move_done(cookie, io);
}

void
scsi_link_shutdown(struct scsi_link *link)
{
	struct scsi_runq	 sleepers = TAILQ_HEAD_INITIALIZER(sleepers);
	struct scsi_iopool	*iopl = link->pool;
	struct scsi_iohandler	*ioh;
	struct scsi_xshandler	*xsh;

	mtx_enter(&iopl->mtx);
	while ((ioh = TAILQ_FIRST(&link->queue)) != NULL) {
		TAILQ_REMOVE(&link->queue, ioh, q_entry);
		ioh->q_state = RUNQ_IDLE;

		if (ioh->handler == scsi_xs_get_done)
			TAILQ_INSERT_TAIL(&sleepers, ioh, q_entry);
#ifdef DIAGNOSTIC
		else
			panic("scsi_link_shutdown: scsi_xshandler on link");
#endif /* DIAGNOSTIC */
	}

	ioh = TAILQ_FIRST(&iopl->queue);
	while (ioh != NULL) {
		xsh = (struct scsi_xshandler *)ioh;
		ioh = TAILQ_NEXT(ioh, q_entry);

#ifdef DIAGNOSTIC
		if (xsh->ioh.handler == scsi_xsh_ioh &&
		    xsh->link == link)
			panic("scsi_link_shutdown: scsi_xshandler on pool");
#endif /* DIAGNOSTIC */

		if (xsh->ioh.handler == scsi_xs_get_done &&
		    xsh->link == link) {
			TAILQ_REMOVE(&iopl->queue, &xsh->ioh, q_entry);
			xsh->ioh.q_state = RUNQ_IDLE;
			link->pending--;

			TAILQ_INSERT_TAIL(&sleepers, &xsh->ioh, q_entry);
		}
	}

	while (link->pending > 0)
		msleep_nsec(&link->pending, &iopl->mtx, PRIBIO, "pendxs",
		    INFSLP);
	mtx_leave(&iopl->mtx);

	while ((ioh = TAILQ_FIRST(&sleepers)) != NULL) {
		TAILQ_REMOVE(&sleepers, ioh, q_entry);
		ioh->handler(ioh->cookie, NULL);
	}
}

int
scsi_link_open(struct scsi_link *link)
{
	int open = 0;

	mtx_enter(&link->pool->mtx);
	if (link->pending < link->openings) {
		link->pending++;
		open = 1;
	}
	mtx_leave(&link->pool->mtx);

	return open;
}

void
scsi_link_close(struct scsi_link *link)
{
	mtx_enter(&link->pool->mtx);
	link->pending--;
	if (ISSET(link->state, SDEV_S_DYING) && link->pending == 0)
		wakeup_one(&link->pending);
	mtx_leave(&link->pool->mtx);

	scsi_xsh_runqueue(link);
}

struct scsi_xfer *
scsi_xs_io(struct scsi_link *link, void *io, int flags)
{
	struct scsi_xfer *xs;

	xs = pool_get(&scsi_xfer_pool, PR_ZERO |
	    (ISSET(flags, SCSI_NOSLEEP) ? PR_NOWAIT : PR_WAITOK));
	if (xs == NULL) {
		scsi_io_put(link->pool, io);
		scsi_link_close(link);
	} else {
		xs->flags = flags;
		xs->sc_link = link;
		xs->retries = SCSI_RETRIES;
		xs->timeout = 10000;
		xs->io = io;
	}

	return xs;
}

void
scsi_xs_put(struct scsi_xfer *xs)
{
	struct scsi_link	*link = xs->sc_link;
	void			*io = xs->io;

	pool_put(&scsi_xfer_pool, xs);

	scsi_io_put(link->pool, io);
	scsi_link_close(link);
}

/*
 * Get scsi driver to send a "are you ready?" command
 */
int
scsi_test_unit_ready(struct scsi_link *link, int retries, int flags)
{
	struct scsi_test_unit_ready	*cmd;
	struct scsi_xfer		*xs;
	int				 error;

	xs = scsi_xs_get(link, flags);
	if (xs == NULL)
		return ENOMEM;
	xs->cmdlen = sizeof(*cmd);
	xs->retries = retries;
	xs->timeout = 10000;

	cmd = (struct scsi_test_unit_ready *)&xs->cmd;
	cmd->opcode = TEST_UNIT_READY;

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	return error;
}

void
scsi_init_inquiry(struct scsi_xfer *xs, u_int8_t flags, u_int8_t pagecode,
    void *data, size_t len)
{
	struct scsi_inquiry *cmd;

	cmd = (struct scsi_inquiry *)&xs->cmd;
	cmd->opcode = INQUIRY;
	cmd->flags = flags;
	cmd->pagecode = pagecode;
	_lto2b(len, cmd->length);

	xs->cmdlen = sizeof(*cmd);

	SET(xs->flags, SCSI_DATA_IN);
	xs->data = data;
	xs->datalen = len;
}

/*
 * Do a scsi operation asking a device what it is.
 * Use the scsi_cmd routine in the switch table.
 */
int
scsi_inquire(struct scsi_link *link, struct scsi_inquiry_data *inqbuf,
    int flags)
{
	struct scsi_xfer	*xs;
	size_t			 bytes;
	int			 avail, retries, error, received;

	/*
	 * Start by asking for only the basic 36 bytes of SCSI2 inquiry
	 * information. This avoids problems with devices that choke trying to
	 * supply more.
	 */
	bytes = SID_SCSI2_HDRLEN + SID_SCSI2_ALEN;
	retries = 0;

again:
	xs = scsi_xs_get(link, flags);
	if (xs == NULL)
		return EBUSY;

	if (bytes > sizeof(*inqbuf))
		bytes = sizeof(*inqbuf);
	scsi_init_inquiry(xs, 0, 0, inqbuf, bytes);

	error = scsi_xs_sync(xs);
	received = xs->datalen - xs->resid;
	scsi_xs_put(xs);

	if (error != 0) {
		SC_DEBUG(link, SDEV_DB2, ("INQUIRE error %d\n", error));
		return error;
	}
	if (received < SID_SCSI2_HDRLEN) {
		SC_DEBUG(link, SDEV_DB2, ("INQUIRE data < SID_SCSI2_HDRLEN\n"));
		return EINVAL;
	}

	avail = SID_SCSI2_HDRLEN + inqbuf->additional_length;

	if (received < avail && retries == 0) {
		retries++;
		bytes = avail;
		goto again;
	}

#ifdef SCSIDEBUG
	sc_print_addr(link);
	printf("got %d of %d bytes of inquiry data:\n", received,
	    avail);
	scsi_show_mem((u_char *)inqbuf, received);
	sc_print_addr(link);
	scsi_show_inquiry_header(inqbuf);
	sc_print_addr(link);
	scsi_show_inquiry_match(inqbuf);
#endif /* SCSIDEBUG */

	if (avail > received)
		inqbuf->additional_length = received - SID_SCSI2_HDRLEN;

	return 0;
}

/*
 * Query a VPD inquiry page
 */
int
scsi_inquire_vpd(struct scsi_link *link, void *buf, u_int buflen,
    u_int8_t page, int flags)
{
	struct scsi_xfer	*xs;
	int			 error;
#ifdef SCSIDEBUG
	u_int32_t		 bytes;
#endif /* SCSIDEBUG */

	if (ISSET(link->flags, SDEV_UMASS))
		return EJUSTRETURN;

	xs = scsi_xs_get(link, flags | SCSI_DATA_IN | SCSI_SILENT);
	if (xs == NULL)
		return ENOMEM;

	xs->retries = 2;
	xs->timeout = 10000;

	scsi_init_inquiry(xs, SI_EVPD, page, buf, buflen);

	error = scsi_xs_sync(xs);

	scsi_xs_put(xs);
#ifdef SCSIDEBUG
	sc_print_addr(link);
	if (error == 0) {
		bytes = sizeof(struct scsi_vpd_hdr) +
		    _2btol(((struct scsi_vpd_hdr *)buf)->page_length);
		if (bytes < buflen)
			buflen = bytes;
		printf("got %u of %u bytes of VPD inquiry page %u data:\n",
		    buflen, bytes, page);
		scsi_show_mem(buf, buflen);
	} else {
		printf("VPD inquiry page %u not available\n", page);
	}
#endif /* SCSIDEBUG */
	return error;
}

int
scsi_read_cap_10(struct scsi_link *link, struct scsi_read_cap_data *rdcap,
    int flags)
{
	struct scsi_read_capacity	  cdb;
	struct scsi_xfer		 *xs;
	int				  rv;

	xs = scsi_xs_get(link, flags | SCSI_DATA_IN | SCSI_SILENT);
	if (xs == NULL)
		return ENOMEM;

	memset(&cdb, 0, sizeof(cdb));
	cdb.opcode = READ_CAPACITY;

	memcpy(&xs->cmd, &cdb, sizeof(cdb));
	xs->cmdlen = sizeof(cdb);
	xs->data = (void *)rdcap;
	xs->datalen = sizeof(*rdcap);
	xs->timeout = 20000;

	rv = scsi_xs_sync(xs);
	scsi_xs_put(xs);

#ifdef SCSIDEBUG
	if (rv == 0) {
		sc_print_addr(link);
		printf("read capacity 10 data:\n");
		scsi_show_mem((u_char *)rdcap, sizeof(*rdcap));
	}
#endif /* SCSIDEBUG */

	return rv;
}

int
scsi_read_cap_16(struct scsi_link *link, struct scsi_read_cap_data_16 *rdcap,
    int flags)
{
	struct scsi_read_capacity_16	 cdb;
	struct scsi_xfer		*xs;
	int				 rv;

	xs = scsi_xs_get(link, flags | SCSI_DATA_IN | SCSI_SILENT);
	if (xs == NULL)
		return ENOMEM;

	memset(&cdb, 0, sizeof(cdb));
	cdb.opcode = READ_CAPACITY_16;
	cdb.byte2 = SRC16_SERVICE_ACTION;
	_lto4b(sizeof(*rdcap), cdb.length);

	memcpy(&xs->cmd, &cdb, sizeof(cdb));
	xs->cmdlen = sizeof(cdb);
	xs->data = (void *)rdcap;
	xs->datalen = sizeof(*rdcap);
	xs->timeout = 20000;

	rv = scsi_xs_sync(xs);
	scsi_xs_put(xs);

#ifdef SCSIDEBUG
	if (rv == 0) {
		sc_print_addr(link);
		printf("read capacity 16 data:\n");
		scsi_show_mem((u_char *)rdcap, sizeof(*rdcap));
	}
#endif /* SCSIDEBUG */

	return rv;
}

/*
 * Prevent or allow the user to remove the media
 */
int
scsi_prevent(struct scsi_link *link, int type, int flags)
{
	struct scsi_prevent	*cmd;
	struct scsi_xfer	*xs;
	int			 error;

	if (ISSET(link->quirks, ADEV_NODOORLOCK))
		return 0;

	xs = scsi_xs_get(link, flags);
	if (xs == NULL)
		return ENOMEM;
	xs->cmdlen = sizeof(*cmd);
	xs->retries = 2;
	xs->timeout = 5000;

	cmd = (struct scsi_prevent *)&xs->cmd;
	cmd->opcode = PREVENT_ALLOW;
	cmd->how = type;

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	return error;
}

/*
 * Get scsi driver to send a "start up" command
 */
int
scsi_start(struct scsi_link *link, int type, int flags)
{
	struct scsi_start_stop	*cmd;
	struct scsi_xfer	*xs;
	int			 error;

	xs = scsi_xs_get(link, flags);
	if (xs == NULL)
		return ENOMEM;
	xs->cmdlen = sizeof(*cmd);
	xs->retries = 2;
	xs->timeout = (type == SSS_START) ? 30000 : 10000;

	cmd = (struct scsi_start_stop *)&xs->cmd;
	cmd->opcode = START_STOP;
	cmd->how = type;

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	return error;
}

int
scsi_mode_sense(struct scsi_link *link, int pg_code,
    union scsi_mode_sense_buf *data, int flags)
{
	struct scsi_mode_sense	*cmd;
	struct scsi_xfer	*xs;
	size_t			 len;
	int			 error;
#ifdef SCSIDEBUG
	size_t			 bytes;
#endif /* SCSIDEBUG */

	len = sizeof(*data);

	xs = scsi_xs_get(link, flags | SCSI_DATA_IN);
	if (xs == NULL)
		return ENOMEM;
	xs->cmdlen = sizeof(*cmd);
	xs->data = (void *)data;
	xs->datalen = len;
	xs->timeout = 20000;

	/*
	 * Make sure the sense buffer is clean before we do the mode sense, so
	 * that checks for bogus values of 0 will work in case the mode sense
	 * fails.
	 */
	memset(data, 0, len);

	cmd = (struct scsi_mode_sense *)&xs->cmd;
	cmd->opcode = MODE_SENSE;
	cmd->page = pg_code;

	if (len > 0xff)
		len = 0xff;
	cmd->length = len;

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	if (error == 0 && !VALID_MODE_HDR(&data->hdr))
		error = EIO;

#ifdef SCSIDEBUG
	sc_print_addr(link);
	if (error == 0) {
		bytes = sizeof(data->hdr.data_length) + data->hdr.data_length;
		if (bytes < len)
			len = bytes;
		printf("got %zu of %zu bytes of mode sense (6) page %d data:\n",
		    len, bytes, pg_code);
		scsi_show_mem((u_char *)data, len);
	} else
		printf("mode sense (6) page %d not available\n", pg_code);
#endif /* SCSIDEBUG */

	return error;
}

int
scsi_mode_sense_big(struct scsi_link *link, int pg_code,
    union scsi_mode_sense_buf *data, int flags)
{
	struct scsi_mode_sense_big	*cmd;
	struct scsi_xfer		*xs;
	size_t				 len;
	int				 error;
#ifdef SCSIDEBUG
	size_t				 bytes;
#endif /* SCSIDEBUG */

	len = sizeof(*data);

	xs = scsi_xs_get(link, flags | SCSI_DATA_IN);
	if (xs == NULL)
		return ENOMEM;
	xs->cmdlen = sizeof(*cmd);
	xs->data = (void *)data;
	xs->datalen = len;
	xs->timeout = 20000;

	/*
	 * Make sure the sense buffer is clean before we do the mode sense, so
	 * that checks for bogus values of 0 will work in case the mode sense
	 * fails.
	 */
	memset(data, 0, len);

	cmd = (struct scsi_mode_sense_big *)&xs->cmd;
	cmd->opcode = MODE_SENSE_BIG;
	cmd->page = pg_code;

	if (len > 0xffff)
		len = 0xffff;
	_lto2b(len, cmd->length);

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	if (error == 0 && !VALID_MODE_HDR_BIG(&data->hdr_big))
		error = EIO;

#ifdef SCSIDEBUG
	sc_print_addr(link);
	if (error == 0) {
		bytes = sizeof(data->hdr_big.data_length) +
		    _2btol(data->hdr_big.data_length);
		if (bytes < len)
			len = bytes;
		printf("got %zu bytes of %zu bytes of mode sense (10) page %d "
		    "data:\n", len, bytes, pg_code);
		scsi_show_mem((u_char *)data, len);
	} else
		printf("mode sense (10) page %d not available\n", pg_code);
#endif /* SCSIDEBUG */

	return error;
}

void *
scsi_mode_sense_page(struct scsi_mode_header *hdr, int pg_code, int pg_length)
{
	u_int8_t	*page;
	int		 total_length, header_length;

	total_length = hdr->data_length + sizeof(hdr->data_length);
	header_length = sizeof(*hdr) + hdr->blk_desc_len;
	page = (u_int8_t *)hdr + header_length;

	if ((total_length - header_length) < pg_length)
		return NULL;

	if ((*page & SMS_PAGE_CODE) != pg_code)
		return NULL;

	return page;
}

void *
scsi_mode_sense_big_page(struct scsi_mode_header_big *hdr, int pg_code,
    int pg_length)
{
	u_int8_t	*page;
	int		 total_length, header_length;

	total_length = _2btol(hdr->data_length) + sizeof(hdr->data_length);
	header_length = sizeof(*hdr) + _2btol(hdr->blk_desc_len);
	page = (u_int8_t *)hdr + header_length;

	if ((total_length - header_length) < pg_length)
		return NULL;

	if ((*page & SMS_PAGE_CODE) != pg_code)
		return NULL;

	return page;
}

void
scsi_parse_blkdesc(struct scsi_link *link, union scsi_mode_sense_buf *buf,
    int big, u_int32_t *density, u_int64_t *block_count, u_int32_t *block_size)
{
	struct scsi_direct_blk_desc	*direct;
	struct scsi_blk_desc		*general;
	size_t				 offset;
	unsigned int			 blk_desc_len;

	if (big == 0) {
		offset = sizeof(struct scsi_mode_header);
		blk_desc_len = buf->hdr.blk_desc_len;
	} else {
		offset = sizeof(struct scsi_mode_header_big);
		blk_desc_len = _2btol(buf->hdr_big.blk_desc_len);
	}

	/* Both scsi_blk_desc and scsi_direct_blk_desc are 8 bytes. */
	if (blk_desc_len == 0 || (blk_desc_len % 8 != 0))
		return;

	switch (link->inqdata.device & SID_TYPE) {
	case T_SEQUENTIAL:
		/*
		 * XXX What other device types return general block descriptors?
		 */
		general = (struct scsi_blk_desc *)&buf->buf[offset];
		if (density != NULL)
			*density = general->density;
		if (block_size != NULL)
			*block_size = _3btol(general->blklen);
		if (block_count != NULL)
			*block_count = (u_int64_t)_3btol(general->nblocks);
		break;

	default:
		direct = (struct scsi_direct_blk_desc *)&buf->buf[offset];
		if (density != NULL)
			*density = direct->density;
		if (block_size != NULL)
			*block_size = _3btol(direct->blklen);
		if (block_count != NULL)
			*block_count = (u_int64_t)_4btol(direct->nblocks);
		break;
	}
}

int
scsi_do_mode_sense(struct scsi_link *link, int pg_code,
    union scsi_mode_sense_buf *buf, void **page_data,
    int pg_length, int flags, int *big)
{
	int error = 0;

	*page_data = NULL;
	*big = 0;

	if (!ISSET(link->flags, SDEV_ATAPI) ||
	    (link->inqdata.device & SID_TYPE) == T_SEQUENTIAL) {
		/*
		 * Try 6 byte mode sense request first. Some devices don't
		 * distinguish between 6 and 10 byte MODE SENSE commands,
		 * returning 6 byte data for 10 byte requests. ATAPI tape
		 * drives use MODE SENSE (6) even though ATAPI uses 10 byte
		 * everything else. Don't bother with SMS_DBD. Check returned
		 * data length to ensure that at least a header (3 additional
		 * bytes) is returned.
		 */
		error = scsi_mode_sense(link, pg_code, buf, flags);
		if (error == 0) {
			/*
			 * Page data may be invalid (e.g. all zeros) but we
			 * accept the device's word that this is the best it can
			 * do. Some devices will freak out if their word is not
			 * accepted and MODE_SENSE_BIG is attempted.
			 */
			*page_data = scsi_mode_sense_page(&buf->hdr, pg_code,
			    pg_length);
			return 0;
		}
	}

	/*
	 * non-ATAPI, non-USB devices that don't support SCSI-2 commands
	 * (i.e. MODE SENSE (10)) are done.
	 */
	if (!ISSET(link->flags, (SDEV_ATAPI | SDEV_UMASS)) &&
	    SID_ANSII_REV(&link->inqdata) < SCSI_REV_2)
		return error;

	/*
	 * Try 10 byte mode sense request.
	 */
	error = scsi_mode_sense_big(link, pg_code, buf, flags);
	if (error != 0)
		return error;

	*big = 1;
	*page_data = scsi_mode_sense_big_page(&buf->hdr_big, pg_code,
	    pg_length);

	return 0;
}

int
scsi_mode_select(struct scsi_link *link, int byte2,
    struct scsi_mode_header *data, int flags, int timeout)
{
	struct scsi_mode_select		*cmd;
	struct scsi_xfer		*xs;
	int				 error;
	u_int32_t			 len;

	len = data->data_length + 1; /* 1 == sizeof(data_length) */

	xs = scsi_xs_get(link, flags | SCSI_DATA_OUT);
	if (xs == NULL)
		return ENOMEM;
	xs->cmdlen = sizeof(*cmd);
	xs->data = (void *)data;
	xs->datalen = len;
	xs->timeout = timeout;

	cmd = (struct scsi_mode_select *)&xs->cmd;
	cmd->opcode = MODE_SELECT;
	cmd->byte2 = byte2;
	cmd->length = len;

	/* Length is reserved when doing mode select so zero it. */
	data->data_length = 0;

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	SC_DEBUG(link, SDEV_DB2, ("scsi_mode_select: error = %d\n", error));

	return error;
}

int
scsi_mode_select_big(struct scsi_link *link, int byte2,
    struct scsi_mode_header_big *data, int flags, int timeout)
{
	struct scsi_mode_select_big	*cmd;
	struct scsi_xfer		*xs;
	int				 error;
	u_int32_t			 len;

	len = _2btol(data->data_length) + 2; /* 2 == sizeof data_length */

	xs = scsi_xs_get(link, flags | SCSI_DATA_OUT);
	if (xs == NULL)
		return ENOMEM;
	xs->cmdlen = sizeof(*cmd);
	xs->data = (void *)data;
	xs->datalen = len;
	xs->timeout = timeout;

	cmd = (struct scsi_mode_select_big *)&xs->cmd;
	cmd->opcode = MODE_SELECT_BIG;
	cmd->byte2 = byte2;
	_lto2b(len, cmd->length);

	/* Length is reserved when doing mode select so zero it. */
	_lto2b(0, data->data_length);

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	SC_DEBUG(link, SDEV_DB2, ("scsi_mode_select_big: error = %d\n",
	    error));

	return error;
}

int
scsi_report_luns(struct scsi_link *link, int selectreport,
    struct scsi_report_luns_data *data, u_int32_t datalen, int flags,
    int timeout)
{
	struct scsi_report_luns		*cmd;
	struct scsi_xfer		*xs;
	int				 error;

	xs = scsi_xs_get(link, flags | SCSI_DATA_IN);
	if (xs == NULL)
		return ENOMEM;
	xs->cmdlen = sizeof(*cmd);
	xs->data = (void *)data;
	xs->datalen = datalen;
	xs->timeout = timeout;

	bzero(data, datalen);

	cmd = (struct scsi_report_luns *)&xs->cmd;
	cmd->opcode = REPORT_LUNS;
	cmd->selectreport = selectreport;
	_lto4b(datalen, cmd->length);

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	SC_DEBUG(link, SDEV_DB2, ("scsi_report_luns: error = %d\n", error));

	return error;
}

void
scsi_xs_exec(struct scsi_xfer *xs)
{
	xs->error = XS_NOERROR;
	xs->resid = xs->datalen;
	xs->status = 0;
	CLR(xs->flags, ITSDONE);

#ifdef SCSIDEBUG
	scsi_show_xs(xs);
#endif /* SCSIDEBUG */

	/* The adapter's scsi_cmd() is responsible for calling scsi_done(). */
	KERNEL_LOCK();
	xs->sc_link->bus->sb_adapter->scsi_cmd(xs);
	KERNEL_UNLOCK();
}

/*
 * Used by device drivers that fake various scsi commands.
 */
void
scsi_copy_internal_data(struct scsi_xfer *xs, void *data, size_t datalen)
{
	size_t copy_cnt;

	SC_DEBUG(xs->sc_link, SDEV_DB3, ("scsi_copy_internal_data\n"));

	if (xs->datalen == 0) {
		sc_print_addr(xs->sc_link);
		printf("uio internal data copy not supported\n");
	} else {
		copy_cnt = MIN(datalen, xs->datalen);
		memcpy(xs->data, data, copy_cnt);
		xs->resid = xs->datalen - copy_cnt;
	}
}

/*
 * This routine is called by the adapter when its xs handling is done.
 */
void
scsi_done(struct scsi_xfer *xs)
{
#ifdef SCSIDEBUG
	if (ISSET(xs->sc_link->flags, SDEV_DB1)) {
		if (xs->datalen && ISSET(xs->flags, SCSI_DATA_IN))
			scsi_show_mem(xs->data, min(64, xs->datalen));
	}
#endif /* SCSIDEBUG */

	SET(xs->flags, ITSDONE);
	KERNEL_LOCK();
	xs->done(xs);
	KERNEL_UNLOCK();
}

int
scsi_xs_sync(struct scsi_xfer *xs)
{
	struct mutex	cookie;
	int		error;

	mtx_init(&cookie, IPL_BIO);

#ifdef DIAGNOSTIC
	if (xs->cookie != NULL)
		panic("xs->cookie != NULL in scsi_xs_sync");
	if (xs->done != NULL)
		panic("xs->done != NULL in scsi_xs_sync");
#endif /* DIAGNOSTIC */

	/*
	 * If we can't sleep while waiting for completion, get the adapter to
	 * complete it for us.
	 */
	if (ISSET(xs->flags, SCSI_NOSLEEP))
		SET(xs->flags, SCSI_POLL);

	xs->done = scsi_xs_sync_done;

	do {
		xs->cookie = &cookie;

		scsi_xs_exec(xs);

		mtx_enter(&cookie);
		while (xs->cookie != NULL)
			msleep_nsec(xs, &cookie, PRIBIO, "syncxs", INFSLP);
		mtx_leave(&cookie);

		error = scsi_xs_error(xs);
	} while (error == ERESTART);

	return error;
}

void
scsi_xs_sync_done(struct scsi_xfer *xs)
{
	struct mutex *cookie = xs->cookie;

	if (cookie == NULL)
		panic("scsi_done called twice on xs(%p)", xs);

	mtx_enter(cookie);
	xs->cookie = NULL;
	if (!ISSET(xs->flags, SCSI_NOSLEEP))
		wakeup_one(xs);
	mtx_leave(cookie);
}

int
scsi_xs_error(struct scsi_xfer *xs)
{
	int error = EIO;

	SC_DEBUG(xs->sc_link, SDEV_DB3, ("scsi_xs_error,err = 0x%x\n",
	    xs->error));

	if (ISSET(xs->sc_link->state, SDEV_S_DYING))
		return ENXIO;

	switch (xs->error) {
	case XS_NOERROR:	/* nearly always hit this one */
		error = 0;
		break;

	case XS_SENSE:
	case XS_SHORTSENSE:
		SC_DEBUG_SENSE(xs);
		error = xs->sc_link->interpret_sense(xs);
		SC_DEBUG(xs->sc_link, SDEV_DB3,
		    ("scsi_interpret_sense returned %#x\n", error));
		break;

	case XS_BUSY:
		error = scsi_delay(xs, 1);
		break;

	case XS_TIMEOUT:
	case XS_RESET:
		error = ERESTART;
		break;

	case XS_DRIVER_STUFFUP:
	case XS_SELTIMEOUT:
		break;

	default:
		sc_print_addr(xs->sc_link);
		printf("unknown error category (0x%x) from scsi driver\n",
		    xs->error);
		break;
	}

	if (error == ERESTART && xs->retries-- < 1)
		return EIO;
	else
		return error;
}

int
scsi_delay(struct scsi_xfer *xs, int seconds)
{
	int ret;

	switch (xs->flags & (SCSI_POLL | SCSI_NOSLEEP)) {
	case SCSI_POLL:
		delay(1000000 * seconds);
		return ERESTART;
	case SCSI_NOSLEEP:
		/* Retry the command immediately since we can't delay. */
		return ERESTART;
	case (SCSI_POLL | SCSI_NOSLEEP):
		/* Invalid combination! */
		return EIO;
	}

	ret = tsleep_nsec(&ret, PRIBIO|PCATCH, "scbusy", SEC_TO_NSEC(seconds));

	/* Signal == abort xs. */
	if (ret == ERESTART || ret == EINTR)
		return EIO;

	return ERESTART;
}

/*
 * Look at the returned sense and act on the error, determining
 * the unix error number to pass back.  (0 = report no error)
 *
 * THIS IS THE DEFAULT ERROR HANDLER
 */
int
scsi_interpret_sense(struct scsi_xfer *xs)
{
	struct scsi_sense_data			*sense = &xs->sense;
	struct scsi_link			*link = xs->sc_link;
	u_int8_t				serr, skey;
	int					error;

	/* Default sense interpretation. */
	serr = sense->error_code & SSD_ERRCODE;
	if (serr != SSD_ERRCODE_CURRENT && serr != SSD_ERRCODE_DEFERRED)
		skey = 0xff;	/* Invalid value, since key is 4 bit value. */
	else
		skey = sense->flags & SSD_KEY;

	/*
	 * Interpret the key/asc/ascq information where appropriate.
	 */
	error = 0;
	switch (skey) {
	case SKEY_NO_SENSE:
	case SKEY_RECOVERED_ERROR:
		if (xs->resid == xs->datalen)
			xs->resid = 0;	/* not short read */
		break;
	case SKEY_BLANK_CHECK:
	case SKEY_EQUAL:
		break;
	case SKEY_NOT_READY:
		if (ISSET(xs->flags, SCSI_IGNORE_NOT_READY))
			return 0;
		error = EIO;
		if (xs->retries) {
			switch (ASC_ASCQ(sense)) {
			case SENSE_NOT_READY_BECOMING_READY:
			case SENSE_NOT_READY_FORMAT:
			case SENSE_NOT_READY_REBUILD:
			case SENSE_NOT_READY_RECALC:
			case SENSE_NOT_READY_INPROGRESS:
			case SENSE_NOT_READY_LONGWRITE:
			case SENSE_NOT_READY_SELFTEST:
			case SENSE_NOT_READY_INIT_REQUIRED:
				SC_DEBUG(link, SDEV_DB1,
				    ("not ready (ASC_ASCQ == %#x)\n",
				    ASC_ASCQ(sense)));
				return scsi_delay(xs, 1);
			case SENSE_NOMEDIUM:
			case SENSE_NOMEDIUM_TCLOSED:
			case SENSE_NOMEDIUM_TOPEN:
			case SENSE_NOMEDIUM_LOADABLE:
			case SENSE_NOMEDIUM_AUXMEM:
				CLR(link->flags, SDEV_MEDIA_LOADED);
				error = ENOMEDIUM;
				break;
			default:
				break;
			}
		}
		break;
	case SKEY_MEDIUM_ERROR:
		switch (ASC_ASCQ(sense)) {
		case SENSE_NOMEDIUM:
		case SENSE_NOMEDIUM_TCLOSED:
		case SENSE_NOMEDIUM_TOPEN:
		case SENSE_NOMEDIUM_LOADABLE:
		case SENSE_NOMEDIUM_AUXMEM:
			CLR(link->flags, SDEV_MEDIA_LOADED);
			error = ENOMEDIUM;
			break;
		case SENSE_BAD_MEDIUM:
		case SENSE_NR_MEDIUM_UNKNOWN_FORMAT:
		case SENSE_NR_MEDIUM_INCOMPATIBLE_FORMAT:
		case SENSE_NW_MEDIUM_UNKNOWN_FORMAT:
		case SENSE_NW_MEDIUM_INCOMPATIBLE_FORMAT:
		case SENSE_NF_MEDIUM_INCOMPATIBLE_FORMAT:
		case SENSE_NW_MEDIUM_AC_MISMATCH:
			error = EMEDIUMTYPE;
			break;
		default:
			error = EIO;
			break;
		}
		break;
	case SKEY_ILLEGAL_REQUEST:
		if (ISSET(xs->flags, SCSI_IGNORE_ILLEGAL_REQUEST))
			return 0;
		if (ASC_ASCQ(sense) == SENSE_MEDIUM_REMOVAL_PREVENTED)
			return EBUSY;
		error = EINVAL;
		break;
	case SKEY_UNIT_ATTENTION:
		switch (ASC_ASCQ(sense)) {
		case SENSE_POWER_RESET_OR_BUS:
		case SENSE_POWER_ON:
		case SENSE_BUS_RESET:
		case SENSE_BUS_DEVICE_RESET:
		case SENSE_DEVICE_INTERNAL_RESET:
		case SENSE_TSC_CHANGE_SE:
		case SENSE_TSC_CHANGE_LVD:
		case SENSE_IT_NEXUS_LOSS:
			return scsi_delay(xs, 1);
		default:
			break;
		}
		if (ISSET(link->flags, SDEV_REMOVABLE))
			CLR(link->flags, SDEV_MEDIA_LOADED);
		if (ISSET(xs->flags, SCSI_IGNORE_MEDIA_CHANGE) ||
		    /* XXX Should reupload any transient state. */
		    !ISSET(link->flags, SDEV_REMOVABLE)) {
			return scsi_delay(xs, 1);
		}
		error = EIO;
		break;
	case SKEY_WRITE_PROTECT:
		error = EROFS;
		break;
	case SKEY_ABORTED_COMMAND:
		error = ERESTART;
		break;
	case SKEY_VOLUME_OVERFLOW:
		error = ENOSPC;
		break;
	case SKEY_HARDWARE_ERROR:
		if (ASC_ASCQ(sense) == SENSE_CARTRIDGE_FAULT)
			return EMEDIUMTYPE;
		error = EIO;
		break;
	default:
		error = EIO;
		break;
	}

#ifndef SCSIDEBUG
	/* SCSIDEBUG would mean it has already been printed. */
	if (skey && !ISSET(xs->flags, SCSI_SILENT))
		scsi_print_sense(xs);
#endif /* ~SCSIDEBUG */

	return error;
}

/*
 * Utility routines often used in SCSI stuff
 */


/*
 * Print out the scsi_link structure's address info.
 */
void
sc_print_addr(struct scsi_link *link)
{
	struct device *adapter_device = link->bus->sc_dev.dv_parent;

	printf("%s(%s:%d:%d): ",
	    link->device_softc ?
	    ((struct device *)link->device_softc)->dv_xname : "probe",
	    adapter_device->dv_xname,
	    link->target, link->lun);
}

static const char *sense_keys[16] = {
	"No Additional Sense",
	"Soft Error",
	"Not Ready",
	"Media Error",
	"Hardware Error",
	"Illegal Request",
	"Unit Attention",
	"Write Protected",
	"Blank Check",
	"Vendor Unique",
	"Copy Aborted",
	"Aborted Command",
	"Equal Error",
	"Volume Overflow",
	"Miscompare Error",
	"Reserved"
};

#ifdef SCSITERSE
static __inline void
asc2ascii(u_int8_t asc, u_int8_t ascq, char *result, size_t len)
{
	snprintf(result, len, "ASC 0x%02x ASCQ 0x%02x", asc, ascq);
}
#else
static const struct {
	u_int8_t	 asc, ascq;
	char		*description;
} adesc[] = {
	/* www.t10.org/lists/asc-num.txt as of 11/15/10. */
	{ 0x00, 0x00, "No Additional Sense Information" },
	{ 0x00, 0x01, "Filemark Detected" },
	{ 0x00, 0x02, "End-Of-Partition/Medium Detected" },
	{ 0x00, 0x03, "Setmark Detected" },
	{ 0x00, 0x04, "Beginning-Of-Partition/Medium Detected" },
	{ 0x00, 0x05, "End-Of-Data Detected" },
	{ 0x00, 0x06, "I/O Process Terminated" },
	{ 0x00, 0x11, "Audio Play Operation In Progress" },
	{ 0x00, 0x12, "Audio Play Operation Paused" },
	{ 0x00, 0x13, "Audio Play Operation Successfully Completed" },
	{ 0x00, 0x14, "Audio Play Operation Stopped Due to Error" },
	{ 0x00, 0x15, "No Current Audio Status To Return" },
	{ 0x00, 0x16, "Operation In Progress" },
	{ 0x00, 0x17, "Cleaning Requested" },
	{ 0x00, 0x18, "Erase Operation In Progress" },
	{ 0x00, 0x19, "Locate Operation In Progress" },
	{ 0x00, 0x1A, "Rewind Operation In Progress" },
	{ 0x00, 0x1B, "Set Capacity Operation In Progress" },
	{ 0x00, 0x1C, "Verify Operation In Progress" },
	{ 0x01, 0x00, "No Index/Sector Signal" },
	{ 0x02, 0x00, "No Seek Complete" },
	{ 0x03, 0x00, "Peripheral Device Write Fault" },
	{ 0x03, 0x01, "No Write Current" },
	{ 0x03, 0x02, "Excessive Write Errors" },
	{ 0x04, 0x00, "Logical Unit Not Ready, Cause Not Reportable" },
	{ 0x04, 0x01, "Logical Unit Is in Process Of Becoming Ready" },
	{ 0x04, 0x02, "Logical Unit Not Ready, Initialization Command Required" },
	{ 0x04, 0x03, "Logical Unit Not Ready, Manual Intervention Required" },
	{ 0x04, 0x04, "Logical Unit Not Ready, Format In Progress" },
	{ 0x04, 0x05, "Logical Unit Not Ready, Rebuild In Progress" },
	{ 0x04, 0x06, "Logical Unit Not Ready, Recalculation In Progress" },
	{ 0x04, 0x07, "Logical Unit Not Ready, Operation In Progress" },
	{ 0x04, 0x08, "Logical Unit Not Ready, Long Write In Progress" },
	{ 0x04, 0x09, "Logical Unit Not Ready, Self-Test In Progress" },
	{ 0x04, 0x0A, "Logical Unit Not Accessible, Asymmetric Access State Transition" },
	{ 0x04, 0x0B, "Logical Unit Not Accessible, Target Port In Standby State" },
	{ 0x04, 0x0C, "Logical Unit Not Accessible, Target Port In Unavailable State" },
	{ 0x04, 0x0D, "Logical Unit Not Ready, Structure Check Required" },
	{ 0x04, 0x10, "Logical Unit Not Ready, Auxiliary Memory Not Accessible" },
	{ 0x04, 0x11, "Logical Unit Not Ready, Notify (Enable Spinup) Required" },
	{ 0x04, 0x12, "Logical Unit Not Ready, Offline" },
	{ 0x04, 0x13, "Logical Unit Not Ready, SA Creation In Progress" },
	{ 0x04, 0x14, "Logical Unit Not Ready, Space Allocation In Progress" },
	{ 0x04, 0x15, "Logical Unit Not Ready, Robotics Disabled" },
	{ 0x04, 0x16, "Logical Unit Not Ready, Configuration Required" },
	{ 0x04, 0x17, "Logical Unit Not Ready, Calibration Required" },
	{ 0x04, 0x18, "Logical Unit Not Ready, A Door Is Open" },
	{ 0x04, 0x19, "Logical Unit Not Ready, Operating In Sequential Mode" },
	{ 0x04, 0x1A, "Logical Unit Not Ready, Start Stop Unit Command In Progress" },
	{ 0x05, 0x00, "Logical Unit Does Not Respond To Selection" },
	{ 0x06, 0x00, "No Reference Position Found" },
	{ 0x07, 0x00, "Multiple Peripheral Devices Selected" },
	{ 0x08, 0x00, "Logical Unit Communication Failure" },
	{ 0x08, 0x01, "Logical Unit Communication Timeout" },
	{ 0x08, 0x02, "Logical Unit Communication Parity Error" },
	{ 0x08, 0x03, "Logical Unit Communication CRC Error (ULTRA-DMA/32)" },
	{ 0x08, 0x04, "Unreachable Copy Target" },
	{ 0x09, 0x00, "Track Following Error" },
	{ 0x09, 0x01, "Tracking Servo Failure" },
	{ 0x09, 0x02, "Focus Servo Failure" },
	{ 0x09, 0x03, "Spindle Servo Failure" },
	{ 0x09, 0x04, "Head Select Fault" },
	{ 0x0A, 0x00, "Error Log Overflow" },
	{ 0x0B, 0x00, "Warning" },
	{ 0x0B, 0x01, "Warning - Specified Temperature Exceeded" },
	{ 0x0B, 0x02, "Warning - Enclosure Degraded" },
	{ 0x0B, 0x03, "Warning - Background Self-Test Failed" },
	{ 0x0B, 0x04, "Warning - Background Pre-Scan Detected Medium Error" },
	{ 0x0B, 0x05, "Warning - Background Medium Scan Detected Medium Error" },
	{ 0x0B, 0x06, "Warning - Non-Volatile Cache Now Volatile" },
	{ 0x0B, 0x07, "Warning - Degraded Power To Non-Volatile Cache" },
	{ 0x0B, 0x08, "Warning - Power Loss Expected" },
	{ 0x0C, 0x00, "Write Error" },
	{ 0x0C, 0x01, "Write Error Recovered with Auto Reallocation" },
	{ 0x0C, 0x02, "Write Error - Auto Reallocate Failed" },
	{ 0x0C, 0x03, "Write Error - Recommend Reassignment" },
	{ 0x0C, 0x04, "Compression Check Miscompare Error" },
	{ 0x0C, 0x05, "Data Expansion Occurred During Compression" },
	{ 0x0C, 0x06, "Block Not Compressible" },
	{ 0x0C, 0x07, "Write Error - Recovery Needed" },
	{ 0x0C, 0x08, "Write Error - Recovery Failed" },
	{ 0x0C, 0x09, "Write Error - Loss Of Streaming" },
	{ 0x0C, 0x0A, "Write Error - Padding Blocks Added" },
	{ 0x0C, 0x0B, "Auxiliary Memory Write Error" },
	{ 0x0C, 0x0C, "Write Error - Unexpected Unsolicited Data" },
	{ 0x0C, 0x0D, "Write Error - Not Enough Unsolicited Data" },
	{ 0x0C, 0x0F, "Defects In Error Window" },
	{ 0x0D, 0x00, "Error Detected By Third Party Temporary Initiator" },
	{ 0x0D, 0x01, "Third Party Device Failure" },
	{ 0x0D, 0x02, "Copy Target Device Not Reachable" },
	{ 0x0D, 0x03, "Incorrect Copy Target Device Type" },
	{ 0x0D, 0x04, "Copy Target Device Data Underrun" },
	{ 0x0D, 0x05, "Copy Target Device Data Overrun" },
	{ 0x0E, 0x00, "Invalid Information Unit" },
	{ 0x0E, 0x01, "Information Unit Too Short" },
	{ 0x0E, 0x02, "Information Unit Too Long" },
	{ 0x10, 0x00, "ID CRC Or ECC Error" },
	{ 0x10, 0x01, "Logical Block Guard Check Failed" },
	{ 0x10, 0x02, "Logical Block Application Tag Check Failed" },
	{ 0x10, 0x03, "Logical Block Reference Tag Check Failed" },
	{ 0x10, 0x04, "Logical Block Protection Error On Recover Buffered Data" },
	{ 0x10, 0x05, "Logical Block Protection Method Error" },
	{ 0x11, 0x00, "Unrecovered Read Error" },
	{ 0x11, 0x01, "Read Retries Exhausted" },
	{ 0x11, 0x02, "Error Too Long To Correct" },
	{ 0x11, 0x03, "Multiple Read Errors" },
	{ 0x11, 0x04, "Unrecovered Read Error - Auto Reallocate Failed" },
	{ 0x11, 0x05, "L-EC Uncorrectable Error" },
	{ 0x11, 0x06, "CIRC Unrecovered Error" },
	{ 0x11, 0x07, "Data Resynchronization Error" },
	{ 0x11, 0x08, "Incomplete Block Read" },
	{ 0x11, 0x09, "No Gap Found" },
	{ 0x11, 0x0A, "Miscorrected Error" },
	{ 0x11, 0x0B, "Uncorrected Read Error - Recommend Reassignment" },
	{ 0x11, 0x0C, "Uncorrected Read Error - Recommend Rewrite The Data" },
	{ 0x11, 0x0D, "De-Compression CRC Error" },
	{ 0x11, 0x0E, "Cannot Decompress Using Declared Algorithm" },
	{ 0x11, 0x0F, "Error Reading UPC/EAN Number" },
	{ 0x11, 0x10, "Error Reading ISRC Number" },
	{ 0x11, 0x11, "Read Error - Loss Of Streaming" },
	{ 0x11, 0x12, "Auxiliary Memory Read Error" },
	{ 0x11, 0x13, "Read Error - Failed Retransmission Request" },
	{ 0x11, 0x14, "Read Error - LBA Marked Bad By Application Client" },
	{ 0x12, 0x00, "Address Mark Not Found for ID Field" },
	{ 0x13, 0x00, "Address Mark Not Found for Data Field" },
	{ 0x14, 0x00, "Recorded Entity Not Found" },
	{ 0x14, 0x01, "Record Not Found" },
	{ 0x14, 0x02, "Filemark or Setmark Not Found" },
	{ 0x14, 0x03, "End-Of-Data Not Found" },
	{ 0x14, 0x04, "Block Sequence Error" },
	{ 0x14, 0x05, "Record Not Found - Recommend Reassignment" },
	{ 0x14, 0x06, "Record Not Found - Data Auto-Reallocated" },
	{ 0x14, 0x07, "Locate Operation Failure" },
	{ 0x15, 0x00, "Random Positioning Error" },
	{ 0x15, 0x01, "Mechanical Positioning Error" },
	{ 0x15, 0x02, "Positioning Error Detected By Read of Medium" },
	{ 0x16, 0x00, "Data Synchronization Mark Error" },
	{ 0x16, 0x01, "Data Sync Error - Data Rewritten" },
	{ 0x16, 0x02, "Data Sync Error - Recommend Rewrite" },
	{ 0x16, 0x03, "Data Sync Error - Data Auto-Reallocated" },
	{ 0x16, 0x04, "Data Sync Error - Recommend Reassignment" },
	{ 0x17, 0x00, "Recovered Data With No Error Correction Applied" },
	{ 0x17, 0x01, "Recovered Data With Retries" },
	{ 0x17, 0x02, "Recovered Data With Positive Head Offset" },
	{ 0x17, 0x03, "Recovered Data With Negative Head Offset" },
	{ 0x17, 0x04, "Recovered Data With Retries and/or CIRC Applied" },
	{ 0x17, 0x05, "Recovered Data Using Previous Sector ID" },
	{ 0x17, 0x06, "Recovered Data Without ECC - Data Auto-Reallocated" },
	{ 0x17, 0x07, "Recovered Data Without ECC - Recommend Reassignment" },
	{ 0x17, 0x08, "Recovered Data Without ECC - Recommend Rewrite" },
	{ 0x17, 0x09, "Recovered Data Without ECC - Data Rewritten" },
	{ 0x18, 0x00, "Recovered Data With Error Correction Applied" },
	{ 0x18, 0x01, "Recovered Data With Error Correction & Retries Applied" },
	{ 0x18, 0x02, "Recovered Data - Data Auto-Reallocated" },
	{ 0x18, 0x03, "Recovered Data With CIRC" },
	{ 0x18, 0x04, "Recovered Data With L-EC" },
	{ 0x18, 0x05, "Recovered Data - Recommend Reassignment" },
	{ 0x18, 0x06, "Recovered Data - Recommend Rewrite" },
	{ 0x18, 0x07, "Recovered Data With ECC - Data Rewritten" },
	{ 0x18, 0x08, "Recovered Data With Linking" },
	{ 0x19, 0x00, "Defect List Error" },
	{ 0x19, 0x01, "Defect List Not Available" },
	{ 0x19, 0x02, "Defect List Error in Primary List" },
	{ 0x19, 0x03, "Defect List Error in Grown List" },
	{ 0x1A, 0x00, "Parameter List Length Error" },
	{ 0x1B, 0x00, "Synchronous Data Transfer Error" },
	{ 0x1C, 0x00, "Defect List Not Found" },
	{ 0x1C, 0x01, "Primary Defect List Not Found" },
	{ 0x1C, 0x02, "Grown Defect List Not Found" },
	{ 0x1D, 0x00, "Miscompare During Verify Operation" },
	{ 0x1D, 0x01, "Miscompare Verify Of Unmapped Lba" },
	{ 0x1E, 0x00, "Recovered ID with ECC" },
	{ 0x1F, 0x00, "Partial Defect List Transfer" },
	{ 0x20, 0x00, "Invalid Command Operation Code" },
	{ 0x20, 0x01, "Access Denied - Initiator Pending-Enrolled" },
	{ 0x20, 0x02, "Access Denied - No Access rights" },
	{ 0x20, 0x03, "Access Denied - Invalid Mgmt ID Key" },
	{ 0x20, 0x04, "Illegal Command While In Write Capable State" },
	{ 0x20, 0x05, "Obsolete" },
	{ 0x20, 0x06, "Illegal Command While In Explicit Address Mode" },
	{ 0x20, 0x07, "Illegal Command While In Implicit Address Mode" },
	{ 0x20, 0x08, "Access Denied - Enrollment Conflict" },
	{ 0x20, 0x09, "Access Denied - Invalid LU Identifier" },
	{ 0x20, 0x0A, "Access Denied - Invalid Proxy Token" },
	{ 0x20, 0x0B, "Access Denied - ACL LUN Conflict" },
	{ 0x20, 0x0C, "Illegal Command When Not In Append-Only Mode" },
	{ 0x21, 0x00, "Logical Block Address Out of Range" },
	{ 0x21, 0x01, "Invalid Element Address" },
	{ 0x21, 0x02, "Invalid Address For Write" },
	{ 0x21, 0x03, "Invalid Write Crossing Layer Jump" },
	{ 0x22, 0x00, "Illegal Function (Should 20 00, 24 00, or 26 00)" },
	{ 0x24, 0x00, "Illegal Field in CDB" },
	{ 0x24, 0x01, "CDB Decryption Error" },
	{ 0x24, 0x02, "Obsolete" },
	{ 0x24, 0x03, "Obsolete" },
	{ 0x24, 0x04, "Security Audit Value Frozen" },
	{ 0x24, 0x05, "Security Working Key Frozen" },
	{ 0x24, 0x06, "Nonce Not Unique" },
	{ 0x24, 0x07, "Nonce Timestamp Out Of Range" },
	{ 0x24, 0x08, "Invalid XCDB" },
	{ 0x25, 0x00, "Logical Unit Not Supported" },
	{ 0x26, 0x00, "Invalid Field In Parameter List" },
	{ 0x26, 0x01, "Parameter Not Supported" },
	{ 0x26, 0x02, "Parameter Value Invalid" },
	{ 0x26, 0x03, "Threshold Parameters Not Supported" },
	{ 0x26, 0x04, "Invalid Release Of Persistent Reservation" },
	{ 0x26, 0x05, "Data Decryption Error" },
	{ 0x26, 0x06, "Too Many Target Descriptors" },
	{ 0x26, 0x07, "Unsupported Target Descriptor Type Code" },
	{ 0x26, 0x08, "Too Many Segment Descriptors" },
	{ 0x26, 0x09, "Unsupported Segment Descriptor Type Code" },
	{ 0x26, 0x0A, "Unexpected Inexact Segment" },
	{ 0x26, 0x0B, "Inline Data Length Exceeded" },
	{ 0x26, 0x0C, "Invalid Operation For Copy Source Or Destination" },
	{ 0x26, 0x0D, "Copy Segment Granularity Violation" },
	{ 0x26, 0x0E, "Invalid Parameter While Port Is Enabled" },
	{ 0x26, 0x0F, "Invalid Data-Out Buffer Integrity Check Value" },
	{ 0x26, 0x10, "Data Decryption Key Fail Limit Reached" },
	{ 0x26, 0x11, "Incomplete Key-Associated Data Set" },
	{ 0x26, 0x12, "Vendor Specific Key Reference Not Found" },
	{ 0x27, 0x00, "Write Protected" },
	{ 0x27, 0x01, "Hardware Write Protected" },
	{ 0x27, 0x02, "Logical Unit Software Write Protected" },
	{ 0x27, 0x03, "Associated Write Protect" },
	{ 0x27, 0x04, "Persistent Write Protect" },
	{ 0x27, 0x05, "Permanent Write Protect" },
	{ 0x27, 0x06, "Conditional Write Protect" },
	{ 0x27, 0x07, "Space Allocation Failed Write Protect" },
	{ 0x28, 0x00, "Not Ready To Ready Transition (Medium May Have Changed)" },
	{ 0x28, 0x01, "Import Or Export Element Accessed" },
	{ 0x28, 0x02, "Format-Layer May Have Changed" },
	{ 0x28, 0x03, "Import/Export Element Accessed, Medium Changed" },
	{ 0x29, 0x00, "Power On, Reset, or Bus Device Reset Occurred" },
	{ 0x29, 0x01, "Power On Occurred" },
	{ 0x29, 0x02, "SCSI Bus Reset Occurred" },
	{ 0x29, 0x03, "Bus Device Reset Function Occurred" },
	{ 0x29, 0x04, "Device Internal Reset" },
	{ 0x29, 0x05, "Transceiver Mode Changed to Single Ended" },
	{ 0x29, 0x06, "Transceiver Mode Changed to LVD" },
	{ 0x29, 0x07, "I_T Nexus Loss Occurred" },
	{ 0x2A, 0x00, "Parameters Changed" },
	{ 0x2A, 0x01, "Mode Parameters Changed" },
	{ 0x2A, 0x02, "Log Parameters Changed" },
	{ 0x2A, 0x03, "Reservations Preempted" },
	{ 0x2A, 0x04, "Reservations Released" },
	{ 0x2A, 0x05, "Registrations Preempted" },
	{ 0x2A, 0x06, "Asymmetric Access State Changed" },
	{ 0x2A, 0x07, "Implicit Asymmetric Access State Transition Failed" },
	{ 0x2A, 0x08, "Priority Changed" },
	{ 0x2A, 0x09, "Capacity Data Has Changed" },
	{ 0x2A, 0x0A, "Error History I_T Nexus Cleared" },
	{ 0x2A, 0x0B, "Error History Snapshot Released" },
	{ 0x2A, 0x0C, "Error Recovery Attributes Have Changed" },
	{ 0x2A, 0x0D, "Data Encryption Capabilities Changed" },
	{ 0x2A, 0x10, "Timestamp Changed" },
	{ 0x2A, 0x11, "Data Encryption Parameters Changed By Another I_T Nexus" },
	{ 0x2A, 0x12, "Data Encryption Parameters Changed By Vendor Specific Event" },
	{ 0x2A, 0x13, "Data Encryption Key Instance Counter Has Changed" },
	{ 0x2A, 0x14, "SA Creation Capabilities Data Has Changed" },
	{ 0x2B, 0x00, "Copy Cannot Execute Since Host Cannot Disconnect" },
	{ 0x2C, 0x00, "Command Sequence Error" },
	{ 0x2C, 0x01, "Too Many Windows Specified" },
	{ 0x2C, 0x02, "Invalid Combination of Windows Specified" },
	{ 0x2C, 0x03, "Current Program Area Is Not Empty" },
	{ 0x2C, 0x04, "Current Program Area Is Empty" },
	{ 0x2C, 0x05, "Illegal Power Condition Request" },
	{ 0x2C, 0x06, "Persistent Prevent Conflict" },
	{ 0x2C, 0x07, "Previous Busy Status" },
	{ 0x2C, 0x08, "Previous Task Set Full Status" },
	{ 0x2C, 0x09, "Previous Reservation Conflict Status" },
	{ 0x2C, 0x0A, "Partition Or Collection Contains User Objects" },
	{ 0x2C, 0x0B, "Not Reserved" },
	{ 0x2C, 0x0C, "ORWrite Generation Does Not Match" },
	{ 0x2D, 0x00, "Overwrite Error On Update In Place" },
	{ 0x2E, 0x00, "Insufficient Time For Operation" },
	{ 0x2F, 0x00, "Commands Cleared By Another Initiator" },
	{ 0x2F, 0x01, "Commands Cleared By Power Loss Notification" },
	{ 0x2F, 0x02, "Commands Cleared By Device Server" },
	{ 0x30, 0x00, "Incompatible Medium Installed" },
	{ 0x30, 0x01, "Cannot Read Medium - Unknown Format" },
	{ 0x30, 0x02, "Cannot Read Medium - Incompatible Format" },
	{ 0x30, 0x03, "Cleaning Cartridge Installed" },
	{ 0x30, 0x04, "Cannot Write Medium - Unknown Format" },
	{ 0x30, 0x05, "Cannot Write Medium - Incompatible Format" },
	{ 0x30, 0x06, "Cannot Format Medium - Incompatible Medium" },
	{ 0x30, 0x07, "Cleaning Failure" },
	{ 0x30, 0x08, "Cannot Write - Application Code Mismatch" },
	{ 0x30, 0x09, "Current Session Not Fixated For Append" },
	{ 0x30, 0x0A, "Cleaning Request Rejected" },
	{ 0x30, 0x10, "Medium Not Formatted" },
	{ 0x30, 0x11, "Incompatible Volume Type" },
	{ 0x30, 0x12, "Incompatible Volume Qualifier" },
	{ 0x30, 0x13, "Cleaning Volume Expired" },
	{ 0x31, 0x00, "Medium Format Corrupted" },
	{ 0x31, 0x01, "Format Command Failed" },
	{ 0x31, 0x02, "Zoned Formatting Failed Due To Spare Linking" },
	{ 0x32, 0x00, "No Defect Spare Location Available" },
	{ 0x32, 0x01, "Defect List Update Failure" },
	{ 0x33, 0x00, "Tape Length Error" },
	{ 0x34, 0x00, "Enclosure Failure" },
	{ 0x35, 0x00, "Enclosure Services Failure" },
	{ 0x35, 0x01, "Unsupported Enclosure Function" },
	{ 0x35, 0x02, "Enclosure Services Unavailable" },
	{ 0x35, 0x03, "Enclosure Services Transfer Failure" },
	{ 0x35, 0x04, "Enclosure Services Transfer Refused" },
	{ 0x36, 0x00, "Ribbon, Ink, or Toner Failure" },
	{ 0x37, 0x00, "Rounded Parameter" },
	{ 0x38, 0x00, "Event Status Notification" },
	{ 0x38, 0x02, "ESN - Power Management Class Event" },
	{ 0x38, 0x04, "ESN - Media Class Event" },
	{ 0x38, 0x06, "ESN - Device Busy Class Event" },
	{ 0x39, 0x00, "Saving Parameters Not Supported" },
	{ 0x3A, 0x00, "Medium Not Present" },
	{ 0x3A, 0x01, "Medium Not Present - Tray Closed" },
	{ 0x3A, 0x02, "Medium Not Present - Tray Open" },
	{ 0x3A, 0x03, "Medium Not Present - Loadable" },
	{ 0x3A, 0x04, "Medium Not Present - Medium Auxiliary Memory Accessible" },
	{ 0x3B, 0x00, "Sequential Positioning Error" },
	{ 0x3B, 0x01, "Tape Position Error At Beginning-of-Medium" },
	{ 0x3B, 0x02, "Tape Position Error At End-of-Medium" },
	{ 0x3B, 0x03, "Tape or Electronic Vertical Forms Unit Not Ready" },
	{ 0x3B, 0x04, "Slew Failure" },
	{ 0x3B, 0x05, "Paper Jam" },
	{ 0x3B, 0x06, "Failed To Sense Top-Of-Form" },
	{ 0x3B, 0x07, "Failed To Sense Bottom-Of-Form" },
	{ 0x3B, 0x08, "Reposition Error" },
	{ 0x3B, 0x09, "Read Past End Of Medium" },
	{ 0x3B, 0x0A, "Read Past Beginning Of Medium" },
	{ 0x3B, 0x0B, "Position Past End Of Medium" },
	{ 0x3B, 0x0C, "Position Past Beginning Of Medium" },
	{ 0x3B, 0x0D, "Medium Destination Element Full" },
	{ 0x3B, 0x0E, "Medium Source Element Empty" },
	{ 0x3B, 0x0F, "End Of Medium Reached" },
	{ 0x3B, 0x11, "Medium Magazine Not Accessible" },
	{ 0x3B, 0x12, "Medium Magazine Removed" },
	{ 0x3B, 0x13, "Medium Magazine Inserted" },
	{ 0x3B, 0x14, "Medium Magazine Locked" },
	{ 0x3B, 0x15, "Medium Magazine Unlocked" },
	{ 0x3B, 0x16, "Mechanical Positioning Or Changer Error" },
	{ 0x3B, 0x17, "Read Past End Of User Object" },
	{ 0x3B, 0x18, "Element Disabled" },
	{ 0x3B, 0x19, "Element Enabled" },
	{ 0x3B, 0x1A, "Data Transfer Device Removed" },
	{ 0x3B, 0x1B, "Data Transfer Device Inserted" },
	{ 0x3D, 0x00, "Invalid Bits In IDENTIFY Message" },
	{ 0x3E, 0x00, "Logical Unit Has Not Self-Configured Yet" },
	{ 0x3E, 0x01, "Logical Unit Failure" },
	{ 0x3E, 0x02, "Timeout On Logical Unit" },
	{ 0x3E, 0x03, "Logical Unit Failed Self-Test" },
	{ 0x3E, 0x04, "Logical Unit Unable To Update Self-Test Log" },
	{ 0x3F, 0x00, "Target Operating Conditions Have Changed" },
	{ 0x3F, 0x01, "Microcode Has Changed" },
	{ 0x3F, 0x02, "Changed Operating Definition" },
	{ 0x3F, 0x03, "INQUIRY Data Has Changed" },
	{ 0x3F, 0x04, "component Device Attached" },
	{ 0x3F, 0x05, "Device Identifier Changed" },
	{ 0x3F, 0x06, "Redundancy Group Created Or Modified" },
	{ 0x3F, 0x07, "Redundancy Group Deleted" },
	{ 0x3F, 0x08, "Spare Created Or Modified" },
	{ 0x3F, 0x09, "Spare Deleted" },
	{ 0x3F, 0x0A, "Volume Set Created Or Modified" },
	{ 0x3F, 0x0B, "Volume Set Deleted" },
	{ 0x3F, 0x0C, "Volume Set Deassigned" },
	{ 0x3F, 0x0D, "Volume Set Reassigned" },
	{ 0x3F, 0x0E, "Reported LUNs Data Has Changed" },
	{ 0x3F, 0x0F, "Echo Buffer Overwritten" },
	{ 0x3F, 0x10, "Medium Loadable" },
	{ 0x3F, 0x11, "Medium Auxiliary Memory Accessible" },
	{ 0x3F, 0x12, "iSCSI IP Address Added" },
	{ 0x3F, 0x13, "iSCSI IP Address Removed" },
	{ 0x3F, 0x14, "iSCSI IP Address Changed" },
	{ 0x40, 0x00, "RAM FAILURE (Should Use 40 NN)" },
	/*
	 * ASC 0x40 also has an ASCQ range from 0x80 to 0xFF.
	 * 0x40 0xNN DIAGNOSTIC FAILURE ON COMPONENT NN
	 */
	{ 0x41, 0x00, "Data Path FAILURE (Should Use 40 NN)" },
	{ 0x42, 0x00, "Power-On or Self-Test FAILURE (Should Use 40 NN)" },
	{ 0x43, 0x00, "Message Error" },
	{ 0x44, 0x00, "Internal Target Failure" },
	{ 0x44, 0x71, "ATA Device Failed Set Features" },
	{ 0x45, 0x00, "Select Or Reselect Failure" },
	{ 0x46, 0x00, "Unsuccessful Soft Reset" },
	{ 0x47, 0x00, "SCSI Parity Error" },
	{ 0x47, 0x01, "Data Phase CRC Error Detected" },
	{ 0x47, 0x02, "SCSI Parity Error Detected During ST Data Phase" },
	{ 0x47, 0x03, "Information Unit iuCRC Error Detected" },
	{ 0x47, 0x04, "Asynchronous Information Protection Error Detected" },
	{ 0x47, 0x05, "Protocol Service CRC Error" },
	{ 0x47, 0x06, "PHY Test Function In Progress" },
	{ 0x47, 0x7F, "Some Commands Cleared By iSCSI Protocol Event" },
	{ 0x48, 0x00, "Initiator Detected Error Message Received" },
	{ 0x49, 0x00, "Invalid Message Error" },
	{ 0x4A, 0x00, "Command Phase Error" },
	{ 0x4B, 0x00, "Data Phase Error" },
	{ 0x4B, 0x01, "Invalid Target Port Transfer Tag Received" },
	{ 0x4B, 0x02, "Too Much Write Data" },
	{ 0x4B, 0x03, "ACK/NAK Timeout" },
	{ 0x4B, 0x04, "NAK Received" },
	{ 0x4B, 0x05, "Data Offset Error" },
	{ 0x4B, 0x06, "Initiator Response Timeout" },
	{ 0x4B, 0x07, "Connection Lost" },
	{ 0x4C, 0x00, "Logical Unit Failed Self-Configuration" },
	/*
	 * ASC 0x4D has an ASCQ range from 0x00 to 0xFF.
	 * 0x4D 0xNN TAGGED OVERLAPPED COMMANDS (NN = TASK TAG)
	 */
	{ 0x4E, 0x00, "Overlapped Commands Attempted" },
	{ 0x50, 0x00, "Write Append Error" },
	{ 0x50, 0x01, "Write Append Position Error" },
	{ 0x50, 0x02, "Position Error Related To Timing" },
	{ 0x51, 0x00, "Erase Failure" },
	{ 0x51, 0x01, "Erase Failure - Incomplete Erase Operation Detected" },
	{ 0x52, 0x00, "Cartridge Fault" },
	{ 0x53, 0x00, "Media Load or Eject Failed" },
	{ 0x53, 0x01, "Unload Tape Failure" },
	{ 0x53, 0x02, "Medium Removal Prevented" },
	{ 0x53, 0x03, "Medium Removal Prevented By Data Transfer Element" },
	{ 0x53, 0x04, "Medium Thread Or Unthread Failure" },
	{ 0x53, 0x05, "Volume Identifier Invalid" },
	{ 0x53, 0x06, "Volume Identifier Missing" },
	{ 0x53, 0x07, "Duplicate Volume Identifier" },
	{ 0x53, 0x08, "Element Status Unknown" },
	{ 0x54, 0x00, "SCSI To Host System Interface Failure" },
	{ 0x55, 0x00, "System Resource Failure" },
	{ 0x55, 0x01, "System Buffer Full" },
	{ 0x55, 0x02, "Insufficient Reservation Resources" },
	{ 0x55, 0x03, "Insufficient Resources" },
	{ 0x55, 0x04, "Insufficient Registration Resources" },
	{ 0x55, 0x05, "Insufficient Access Control Resources" },
	{ 0x55, 0x06, "Auxiliary Memory Out Of Space" },
	{ 0x55, 0x07, "Quota Error" },
	{ 0x55, 0x08, "Maximum Number Of Supplemental Decryption Keys Exceeded" },
	{ 0x55, 0x09, "Medium Auxiliary Memory Not Accessible" },
	{ 0x55, 0x0A, "Data Currently Unavailable" },
	{ 0x55, 0x0B, "Insufficient Power For Operation" },
	{ 0x57, 0x00, "Unable To Recover Table-Of-Contents" },
	{ 0x58, 0x00, "Generation Does Not Exist" },
	{ 0x59, 0x00, "Updated Block Read" },
	{ 0x5A, 0x00, "Operator Request or State Change Input" },
	{ 0x5A, 0x01, "Operator Medium Removal Requested" },
	{ 0x5A, 0x02, "Operator Selected Write Protect" },
	{ 0x5A, 0x03, "Operator Selected Write Permit" },
	{ 0x5B, 0x00, "Log Exception" },
	{ 0x5B, 0x01, "Threshold Condition Met" },
	{ 0x5B, 0x02, "Log Counter At Maximum" },
	{ 0x5B, 0x03, "Log List Codes Exhausted" },
	{ 0x5C, 0x00, "RPL Status Change" },
	{ 0x5C, 0x01, "Spindles Synchronized" },
	{ 0x5C, 0x02, "Spindles Not Synchronized" },
	{ 0x5D, 0x00, "Failure Prediction Threshold Exceeded" },
	{ 0x5D, 0x01, "Media Failure Prediction Threshold Exceeded" },
	{ 0x5D, 0x02, "Logical Unit Failure Prediction Threshold Exceeded" },
	{ 0x5D, 0x03, "Spare Area Exhaustion Prediction Threshold Exceeded" },
	{ 0x5D, 0x10, "Hardware Impending Failure General Hard Drive Failure" },
	{ 0x5D, 0x11, "Hardware Impending Failure Drive Error Rate Too High" },
	{ 0x5D, 0x12, "Hardware Impending Failure Data Error Rate Too High" },
	{ 0x5D, 0x13, "Hardware Impending Failure Seek Error Rate Too High" },
	{ 0x5D, 0x14, "Hardware Impending Failure Too Many Block Reassigns" },
	{ 0x5D, 0x15, "Hardware Impending Failure Access Times Too High" },
	{ 0x5D, 0x16, "Hardware Impending Failure Start Unit Times Too High" },
	{ 0x5D, 0x17, "Hardware Impending Failure Channel Parametrics" },
	{ 0x5D, 0x18, "Hardware Impending Failure Controller Detected" },
	{ 0x5D, 0x19, "Hardware Impending Failure Throughput Performance" },
	{ 0x5D, 0x1A, "Hardware Impending Failure Seek Time Performance" },
	{ 0x5D, 0x1B, "Hardware Impending Failure Spin-Up Retry Count" },
	{ 0x5D, 0x1C, "Hardware Impending Failure Drive Calibration Retry Count" },
	{ 0x5D, 0x20, "Controller Impending Failure General Hard Drive Failure" },
	{ 0x5D, 0x21, "Controller Impending Failure Drive Error Rate Too High" },
	{ 0x5D, 0x22, "Controller Impending Failure Data Error Rate Too High" },
	{ 0x5D, 0x23, "Controller Impending Failure Seek Error Rate Too High" },
	{ 0x5D, 0x24, "Controller Impending Failure Too Many Block Reassigns" },
	{ 0x5D, 0x25, "Controller Impending Failure Access Times Too High" },
	{ 0x5D, 0x26, "Controller Impending Failure Start Unit Times Too High" },
	{ 0x5D, 0x27, "Controller Impending Failure Channel Parametrics" },
	{ 0x5D, 0x28, "Controller Impending Failure Controller Detected" },
	{ 0x5D, 0x29, "Controller Impending Failure Throughput Performance" },
	{ 0x5D, 0x2A, "Controller Impending Failure Seek Time Performance" },
	{ 0x5D, 0x2B, "Controller Impending Failure Spin-Up Retry Count" },
	{ 0x5D, 0x2C, "Controller Impending Failure Drive Calibration Retry Count" },
	{ 0x5D, 0x30, "Data Channel Impending Failure General Hard Drive Failure" },
	{ 0x5D, 0x31, "Data Channel Impending Failure Drive Error Rate Too High" },
	{ 0x5D, 0x32, "Data Channel Impending Failure Data Error Rate Too High" },
	{ 0x5D, 0x33, "Data Channel Impending Failure Seek Error Rate Too High" },
	{ 0x5D, 0x34, "Data Channel Impending Failure Too Many Block Reassigns" },
	{ 0x5D, 0x35, "Data Channel Impending Failure Access Times Too High" },
	{ 0x5D, 0x36, "Data Channel Impending Failure Start Unit Times Too High" },
	{ 0x5D, 0x37, "Data Channel Impending Failure Channel Parametrics" },
	{ 0x5D, 0x38, "Data Channel Impending Failure Controller Detected" },
	{ 0x5D, 0x39, "Data Channel Impending Failure Throughput Performance" },
	{ 0x5D, 0x3A, "Data Channel Impending Failure Seek Time Performance" },
	{ 0x5D, 0x3B, "Data Channel Impending Failure Spin-Up Retry Count" },
	{ 0x5D, 0x3C, "Data Channel Impending Failure Drive Calibration Retry Count" },
	{ 0x5D, 0x40, "Servo Impending Failure General Hard Drive Failure" },
	{ 0x5D, 0x41, "Servo Impending Failure Drive Error Rate Too High" },
	{ 0x5D, 0x42, "Servo Impending Failure Data Error Rate Too High" },
	{ 0x5D, 0x43, "Servo Impending Failure Seek Error Rate Too High" },
	{ 0x5D, 0x44, "Servo Impending Failure Too Many Block Reassigns" },
	{ 0x5D, 0x45, "Servo Impending Failure Access Times Too High" },
	{ 0x5D, 0x46, "Servo Impending Failure Start Unit Times Too High" },
	{ 0x5D, 0x47, "Servo Impending Failure Channel Parametrics" },
	{ 0x5D, 0x48, "Servo Impending Failure Controller Detected" },
	{ 0x5D, 0x49, "Servo Impending Failure Throughput Performance" },
	{ 0x5D, 0x4A, "Servo Impending Failure Seek Time Performance" },
	{ 0x5D, 0x4B, "Servo Impending Failure Spin-Up Retry Count" },
	{ 0x5D, 0x4C, "Servo Impending Failure Drive Calibration Retry Count" },
	{ 0x5D, 0x50, "Spindle Impending Failure General Hard Drive Failure" },
	{ 0x5D, 0x51, "Spindle Impending Failure Drive Error Rate Too High" },
	{ 0x5D, 0x52, "Spindle Impending Failure Data Error Rate Too High" },
	{ 0x5D, 0x53, "Spindle Impending Failure Seek Error Rate Too High" },
	{ 0x5D, 0x54, "Spindle Impending Failure Too Many Block Reassigns" },
	{ 0x5D, 0x55, "Spindle Impending Failure Access Times Too High" },
	{ 0x5D, 0x56, "Spindle Impending Failure Start Unit Times Too High" },
	{ 0x5D, 0x57, "Spindle Impending Failure Channel Parametrics" },
	{ 0x5D, 0x58, "Spindle Impending Failure Controller Detected" },
	{ 0x5D, 0x59, "Spindle Impending Failure Throughput Performance" },
	{ 0x5D, 0x5A, "Spindle Impending Failure Seek Time Performance" },
	{ 0x5D, 0x5B, "Spindle Impending Failure Spin-Up Retry Count" },
	{ 0x5D, 0x5C, "Spindle Impending Failure Drive Calibration Retry Count" },
	{ 0x5D, 0x60, "Firmware Impending Failure General Hard Drive Failure" },
	{ 0x5D, 0x61, "Firmware Impending Failure Drive Error Rate Too High" },
	{ 0x5D, 0x62, "Firmware Impending Failure Data Error Rate Too High" },
	{ 0x5D, 0x63, "Firmware Impending Failure Seek Error Rate Too High" },
	{ 0x5D, 0x64, "Firmware Impending Failure Too Many Block Reassigns" },
	{ 0x5D, 0x65, "Firmware Impending Failure Access Times Too High" },
	{ 0x5D, 0x66, "Firmware Impending Failure Start Unit Times Too High" },
	{ 0x5D, 0x67, "Firmware Impending Failure Channel Parametrics" },
	{ 0x5D, 0x68, "Firmware Impending Failure Controller Detected" },
	{ 0x5D, 0x69, "Firmware Impending Failure Throughput Performance" },
	{ 0x5D, 0x6A, "Firmware Impending Failure Seek Time Performance" },
	{ 0x5D, 0x6B, "Firmware Impending Failure Spin-Up Retry Count" },
	{ 0x5D, 0x6C, "Firmware Impending Failure Drive Calibration Retry Count" },
	{ 0x5D, 0xFF, "Failure Prediction Threshold Exceeded (false)" },
	{ 0x5E, 0x00, "Low Power Condition On" },
	{ 0x5E, 0x01, "Idle Condition Activated By Timer" },
	{ 0x5E, 0x02, "Standby Condition Activated By Timer" },
	{ 0x5E, 0x03, "Idle Condition Activated By Command" },
	{ 0x5E, 0x04, "Standby Condition Activated By Command" },
	{ 0x5E, 0x05, "IDLE_B Condition Activated By Timer" },
	{ 0x5E, 0x06, "IDLE_B Condition Activated By Command" },
	{ 0x5E, 0x07, "IDLE_C Condition Activated By Timer" },
	{ 0x5E, 0x08, "IDLE_C Condition Activated By Command" },
	{ 0x5E, 0x09, "STANDBY_Y Condition Activated By Timer" },
	{ 0x5E, 0x0A, "STANDBY_Y Condition Activated By Command" },
	{ 0x5E, 0x41, "Power State Change To Active" },
	{ 0x5E, 0x42, "Power State Change To Idle" },
	{ 0x5E, 0x43, "Power State Change To Standby" },
	{ 0x5E, 0x45, "Power State Change To Sleep" },
	{ 0x5E, 0x47, "Power State Change To Device Control" },
	{ 0x60, 0x00, "Lamp Failure" },
	{ 0x61, 0x00, "Video Acquisition Error" },
	{ 0x61, 0x01, "Unable To Acquire Video" },
	{ 0x61, 0x02, "Out Of Focus" },
	{ 0x62, 0x00, "Scan Head Positioning Error" },
	{ 0x63, 0x00, "End Of User Area Encountered On This Track" },
	{ 0x63, 0x01, "Packet Does Not Fit In Available Space" },
	{ 0x64, 0x00, "Illegal Mode For This Track" },
	{ 0x64, 0x01, "Invalid Packet Size" },
	{ 0x65, 0x00, "Voltage Fault" },
	{ 0x66, 0x00, "Automatic Document Feeder Cover Up" },
	{ 0x66, 0x01, "Automatic Document Feeder Lift Up" },
	{ 0x66, 0x02, "Document Jam In Automatic Document Feeder" },
	{ 0x66, 0x03, "Document Miss Feed Automatic In Document Feeder" },
	{ 0x67, 0x00, "Configuration Failure" },
	{ 0x67, 0x01, "Configuration Of Incapable Logical Units Failed" },
	{ 0x67, 0x02, "Add Logical Unit Failed" },
	{ 0x67, 0x03, "Modification Of Logical Unit Failed" },
	{ 0x67, 0x04, "Exchange Of Logical Unit Failed" },
	{ 0x67, 0x05, "Remove Of Logical Unit Failed" },
	{ 0x67, 0x06, "Attachment Of Logical Unit Failed" },
	{ 0x67, 0x07, "Creation Of Logical Unit Failed" },
	{ 0x67, 0x08, "Assign Failure Occurred" },
	{ 0x67, 0x09, "Multiply Assigned Logical Unit" },
	{ 0x67, 0x0A, "Set Target Port Groups Command Failed" },
	{ 0x67, 0x0B, "ATA Device Feature Not Enabled" },
	{ 0x68, 0x00, "Logical Unit Not Configured" },
	{ 0x69, 0x00, "Data Loss On Logical Unit" },
	{ 0x69, 0x01, "Multiple Logical Unit Failures" },
	{ 0x69, 0x02, "Parity/Data Mismatch" },
	{ 0x6A, 0x00, "Informational, Refer To Log" },
	{ 0x6B, 0x00, "State Change Has Occurred" },
	{ 0x6B, 0x01, "Redundancy Level Got Better" },
	{ 0x6B, 0x02, "Redundancy Level Got Worse" },
	{ 0x6C, 0x00, "Rebuild Failure Occurred" },
	{ 0x6D, 0x00, "Recalculate Failure Occurred" },
	{ 0x6E, 0x00, "Command To Logical Unit Failed" },
	{ 0x6F, 0x00, "Copy Protection Key Exchange Failure - Authentication Failure" },
	{ 0x6F, 0x01, "Copy Protection Key Exchange Failure - Key Not Present" },
	{ 0x6F, 0x02, "Copy Protection Key Exchange Failure - Key Not Established" },
	{ 0x6F, 0x03, "Read Of Scrambled Sector Without Authentication" },
	{ 0x6F, 0x04, "Media Region Code Is Mismatched To Logical Unit Region" },
	{ 0x6F, 0x05, "Drive Region Must Be Permanent/Region Reset Count Error" },
	/*
	 * ASC 0x70 has an ASCQ range from 0x00 to 0xFF.
	 * 0x70 0xNN DECOMPRESSION EXCEPTION SHORT ALGORITHM ID Of NN
	 */
	{ 0x71, 0x00, "Decompression Exception Long Algorithm ID" },
	{ 0x72, 0x00, "Session Fixation Error" },
	{ 0x72, 0x01, "Session Fixation Error Writing Lead-In" },
	{ 0x72, 0x02, "Session Fixation Error Writing Lead-Out" },
	{ 0x72, 0x03, "Session Fixation Error - Incomplete Track In Session" },
	{ 0x72, 0x04, "Empty Or Partially Written Reserved Track" },
	{ 0x72, 0x05, "No More Track Reservations Allowed" },
	{ 0x72, 0x06, "RMZ Extension Is Not Allowed" },
	{ 0x72, 0x07, "No More Test Zone Extensions Are Allowed" },
	{ 0x73, 0x00, "CD Control Error" },
	{ 0x73, 0x01, "Power Calibration Area Almost Full" },
	{ 0x73, 0x02, "Power Calibration Area Is Full" },
	{ 0x73, 0x03, "Power Calibration Area Error" },
	{ 0x73, 0x04, "Program Memory Area Update Failure" },
	{ 0x73, 0x05, "Program Memory Area Is Full" },
	{ 0x73, 0x06, "RMA/PMA Is Almost Full" },
	{ 0x73, 0x10, "Current Power Calibration Area Almost Full" },
	{ 0x73, 0x11, "Current Power Calibration Area Is Full" },
	{ 0x73, 0x17, "RDZ Is Full" },
	{ 0x74, 0x00, "Security Error" },
	{ 0x74, 0x01, "Unable To Decrypt Data" },
	{ 0x74, 0x02, "Unencrypted Data Encountered While Decrypting" },
	{ 0x74, 0x03, "Incorrect Data Encryption Key" },
	{ 0x74, 0x04, "Cryptographic Integrity Validation Failed" },
	{ 0x74, 0x05, "Error Decrypting Data" },
	{ 0x74, 0x06, "Unknown Signature Verification Key" },
	{ 0x74, 0x07, "Encryption Parameters Not Useable" },
	{ 0x74, 0x08, "Digital Signature Validation Failure" },
	{ 0x74, 0x09, "Encryption Mode Mismatch On Read" },
	{ 0x74, 0x0A, "Encrypted Block Not Raw Read Enabled" },
	{ 0x74, 0x0B, "Incorrect Encryption Parameters" },
	{ 0x74, 0x0C, "Unable To Decrypt Parameter List" },
	{ 0x74, 0x0D, "Encryption Algorithm Disabled" },
	{ 0x74, 0x10, "SA Creation Parameter Value Invalid" },
	{ 0x74, 0x11, "SA Creation Parameter Value Rejected" },
	{ 0x74, 0x12, "Invalid SA Usage" },
	{ 0x74, 0x21, "Data Encryption Configuration Prevented" },
	{ 0x74, 0x30, "SA Creation Parameter Not Supported" },
	{ 0x74, 0x40, "Authentication Failed" },
	{ 0x74, 0x61, "External Data Encryption Key Manager Access Error" },
	{ 0x74, 0x62, "External Data Encryption Key Manager Error" },
	{ 0x74, 0x63, "External Data Encryption Key Not Found" },
	{ 0x74, 0x64, "External Data Encryption Request Not Authorized" },
	{ 0x74, 0x6E, "External Data Encryption Control Timeout" },
	{ 0x74, 0x6F, "External Data Encryption Control Error" },
	{ 0x74, 0x71, "Logical Unit Access Not Authorized" },
	{ 0x74, 0x79, "Security Conflict In Translated Device" },
	{ 0x00, 0x00, NULL }
};

static __inline void
asc2ascii(u_int8_t asc, u_int8_t ascq, char *result, size_t len)
{
	int i;

	/* Check for a dynamically built description. */
	switch (asc) {
	case 0x40:
		if (ascq >= 0x80) {
			snprintf(result, len,
		            "Diagnostic Failure on Component 0x%02x", ascq);
			return;
		}
		break;
	case 0x4d:
		snprintf(result, len,
		    "Tagged Overlapped Commands (0x%02x = TASK TAG)", ascq);
		return;
	case 0x70:
		snprintf(result, len,
		    "Decompression Exception Short Algorithm ID OF 0x%02x",
		    ascq);
		return;
	default:
		break;
	}

	/* Check for a fixed description. */
	for (i = 0; adesc[i].description != NULL; i++) {
		if (adesc[i].asc == asc && adesc[i].ascq == ascq) {
			strlcpy(result, adesc[i].description, len);
			return;
		}
	}

	/* Just print out the ASC and ASCQ values as a description. */
	snprintf(result, len, "ASC 0x%02x ASCQ 0x%02x", asc, ascq);
}
#endif /* SCSITERSE */

void
scsi_print_sense(struct scsi_xfer *xs)
{
	struct scsi_sense_data		*sense = &xs->sense;
	char				*sbs;
	int32_t				 info;
	u_int8_t			 serr = sense->error_code & SSD_ERRCODE;

	sc_print_addr(xs->sc_link);

	/* XXX For error 0x71, current opcode is not the relevant one. */
	printf("%sCheck Condition (error %#x) on opcode 0x%x\n",
	    (serr == SSD_ERRCODE_DEFERRED) ? "DEFERRED " : "", serr,
	    xs->cmd.opcode);

	if (serr != SSD_ERRCODE_CURRENT && serr != SSD_ERRCODE_DEFERRED) {
		if (ISSET(sense->error_code, SSD_ERRCODE_VALID)) {
			struct scsi_sense_data_unextended *usense =
			    (struct scsi_sense_data_unextended *)sense;
			printf("   AT BLOCK #: %d (decimal)",
			    _3btol(usense->block));
		}
		return;
	}

	printf("    SENSE KEY: %s\n", scsi_decode_sense(sense,
	    DECODE_SENSE_KEY));

	if (sense->flags & (SSD_FILEMARK | SSD_EOM | SSD_ILI)) {
		char pad = ' ';

		printf("             ");
		if (ISSET(sense->flags, SSD_FILEMARK)) {
			printf("%c Filemark Detected", pad);
			pad = ',';
		}
		if (ISSET(sense->flags, SSD_EOM)) {
			printf("%c EOM Detected", pad);
			pad = ',';
		}
		if (ISSET(sense->flags, SSD_ILI))
			printf("%c Incorrect Length Indicator Set", pad);
		printf("\n");
	}

	/*
	 * It is inconvenient to use device type to figure out how to
	 * format the info fields. So print them as 32 bit integers.
	 */
	info = _4btol(&sense->info[0]);
	if (info)
		printf("         INFO: 0x%x (VALID flag %s)\n", info,
		    ISSET(sense->error_code, SSD_ERRCODE_VALID) ? "on" : "off");

	if (sense->extra_len < 4)
		return;

	info = _4btol(&sense->cmd_spec_info[0]);
	if (info)
		printf(" COMMAND INFO: 0x%x\n", info);
	sbs = scsi_decode_sense(sense, DECODE_ASC_ASCQ);
	if (strlen(sbs) > 0)
		printf("     ASC/ASCQ: %s\n", sbs);
	if (sense->fru != 0)
		printf("     FRU CODE: 0x%x\n", sense->fru);
	sbs = scsi_decode_sense(sense, DECODE_SKSV);
	if (strlen(sbs) > 0)
		printf("         SKSV: %s\n", sbs);
}

char *
scsi_decode_sense(struct scsi_sense_data *sense, int flag)
{
	static char				rqsbuf[132];
	u_int16_t				count;
	u_int8_t				skey, spec_1;
	int					len;

	bzero(rqsbuf, sizeof(rqsbuf));

	skey = sense->flags & SSD_KEY;
	spec_1 = sense->sense_key_spec_1;
	count = _2btol(&sense->sense_key_spec_2);

	switch (flag) {
	case DECODE_SENSE_KEY:
		strlcpy(rqsbuf, sense_keys[skey], sizeof(rqsbuf));
		break;
	case DECODE_ASC_ASCQ:
		asc2ascii(sense->add_sense_code, sense->add_sense_code_qual,
		    rqsbuf, sizeof(rqsbuf));
		break;
	case DECODE_SKSV:
		if (sense->extra_len < 9 || !ISSET(spec_1, SSD_SCS_VALID))
			break;
		switch (skey) {
		case SKEY_ILLEGAL_REQUEST:
			len = snprintf(rqsbuf, sizeof rqsbuf,
			    "Error in %s, Offset %d",
			    ISSET(spec_1, SSD_SCS_CDB_ERROR) ? "CDB" :
			    "Parameters", count);
			if ((len != -1 && len < sizeof rqsbuf) &&
			    ISSET(spec_1, SSD_SCS_VALID_BIT_INDEX))
				snprintf(rqsbuf+len, sizeof rqsbuf - len,
				    ", bit %d", spec_1 & SSD_SCS_BIT_INDEX);
			break;
		case SKEY_RECOVERED_ERROR:
		case SKEY_MEDIUM_ERROR:
		case SKEY_HARDWARE_ERROR:
			snprintf(rqsbuf, sizeof rqsbuf,
			    "Actual Retry Count: %d", count);
			break;
		case SKEY_NOT_READY:
			snprintf(rqsbuf, sizeof rqsbuf,
			    "Progress Indicator: %d", count);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return rqsbuf;
}

void
scsi_cmd_rw_decode(struct scsi_generic *cmd, u_int64_t *blkno,
    u_int32_t *nblks)
{
	switch (cmd->opcode) {
	case READ_COMMAND:
	case WRITE_COMMAND: {
		struct scsi_rw *rw = (struct scsi_rw *)cmd;
		*blkno = _3btol(rw->addr) & (SRW_TOPADDR << 16 | 0xffff);
		*nblks = rw->length ? rw->length : 0x100;
		break;
	}
	case READ_10:
	case WRITE_10: {
		struct scsi_rw_10 *rw10 = (struct scsi_rw_10 *)cmd;
		*blkno = _4btol(rw10->addr);
		*nblks = _2btol(rw10->length);
		break;
	}
	case READ_12:
	case WRITE_12: {
		struct scsi_rw_12 *rw12 = (struct scsi_rw_12 *)cmd;
		*blkno = _4btol(rw12->addr);
		*nblks = _4btol(rw12->length);
		break;
	}
	case READ_16:
	case WRITE_16: {
		struct scsi_rw_16 *rw16 = (struct scsi_rw_16 *)cmd;
		*blkno = _8btol(rw16->addr);
		*nblks = _4btol(rw16->length);
		break;
	}
	default:
		panic("scsi_cmd_rw_decode: bad opcode 0x%02x", cmd->opcode);
	}
}

#ifdef SCSIDEBUG
u_int32_t scsidebug_buses = SCSIDEBUG_BUSES;
u_int32_t scsidebug_targets = SCSIDEBUG_TARGETS;
u_int32_t scsidebug_luns = SCSIDEBUG_LUNS;
int scsidebug_level = SCSIDEBUG_LEVEL;

const char *flagnames[] = {
	"REMOVABLE",
	"MEDIA LOADED",
	"READONLY",
	"OPEN",
	"DB1",
	"DB2",
	"DB3",
	"DB4",
	"EJECTING",
	"ATAPI",
	"UMASS",
	"VIRTUAL",
	"OWN_IOPL",
	NULL
};

const char *quirknames[] = {
	"AUTOSAVE",
	"NOSYNC",
	"NOWIDE",
	"NOTAGS",
	"NOSYNCCACHE",
	"NOSENSE",
	"LITTLETOC",
	"NOCAPACITY",
	"NODOORLOCK",
	NULL
};

const char *devicetypenames[32] = {
	"T_DIRECT",
	"T_SEQUENTIAL",
	"T_PRINTER",
	"T_PROCESSOR",
	"T_WORM",
	"T_CDROM",
	"T_SCANNER",
	"T_OPTICAL",
	"T_CHANGER",
	"T_COMM",
	"T_ASC0",
	"T_ASC1",
	"T_STROARRAY",
	"T_ENCLOSURE",
	"T_RDIRECT",
	"T_OCRW",
	"T_BCC",
	"T_OSD",
	"T_ADC",
	"T_RESERVED",
	"T_RESERVED",
	"T_RESERVED",
	"T_RESERVED",
	"T_RESERVED",
	"T_RESERVED",
	"T_RESERVED",
	"T_RESERVED",
	"T_RESERVED",
	"T_RESERVED",
	"T_RESERVED",
	"T_WELL_KNOWN_LU",
	"T_NODEVICE"
};

/*
 * Print out sense data details.
 */
void
scsi_show_sense(struct scsi_xfer *xs)
{
	struct scsi_sense_data	*sense = &xs->sense;
	struct scsi_link	*link = xs->sc_link;

	SC_DEBUG(link, SDEV_DB1,
	    ("code:%#x valid:%d key:%#x ili:%d eom:%d fmark:%d extra:%d\n",
	    sense->error_code & SSD_ERRCODE,
	    sense->error_code & SSD_ERRCODE_VALID ? 1 : 0,
	    sense->flags & SSD_KEY,
	    sense->flags & SSD_ILI ? 1 : 0,
	    sense->flags & SSD_EOM ? 1 : 0,
	    sense->flags & SSD_FILEMARK ? 1 : 0,
	    sense->extra_len));

	if (ISSET(xs->sc_link->flags, SDEV_DB1))
		scsi_show_mem((u_char *)&xs->sense, sizeof(xs->sense));

	scsi_print_sense(xs);
}

/*
 * Given a scsi_xfer, dump the request, in all its glory
 */
void
scsi_show_xs(struct scsi_xfer *xs)
{
	u_char		*b = (u_char *)&xs->cmd;
	int		 i = 0;

	if (!ISSET(xs->sc_link->flags, SDEV_DB1))
		return;

	sc_print_addr(xs->sc_link);
	printf("xs  (%p): ", xs);

	printf("flg(0x%x)", xs->flags);
	printf("link(%p)", xs->sc_link);
	printf("retr(0x%x)", xs->retries);
	printf("timo(0x%x)", xs->timeout);
	printf("data(%p)", xs->data);
	printf("res(0x%zx)", xs->resid);
	printf("err(0x%x)", xs->error);
	printf("bp(%p)\n", xs->bp);

	sc_print_addr(xs->sc_link);
	printf("cmd (%p): ", &xs->cmd);

	if (!ISSET(xs->flags, SCSI_RESET)) {
		while (i < xs->cmdlen) {
			if (i)
				printf(",");
			printf("%x", b[i++]);
		}
		printf("-[%d bytes]\n", xs->datalen);
	} else
		printf("-RESET-\n");

	if (xs->datalen && ISSET(xs->flags, SCSI_DATA_OUT))
		scsi_show_mem(xs->data, min(64, xs->datalen));
}

void
scsi_show_mem(u_char *address, int num)
{
	int x;

	printf("------------------------------");
	for (x = 0; x < num; x++) {
		if ((x % 16) == 0)
			printf("\n%03d: ", x);
		printf("%02x ", *address++);
	}
	printf("\n------------------------------\n");
}

void
scsi_show_flags(u_int32_t flags, const char **names)
{
	int		i, first, exhausted;
	u_int32_t	unnamed;

	printf("<");
	for (first = 1, exhausted = 0, unnamed = 0, i = 0; i < 32; i++) {
		if (!ISSET(flags, 1 << i))
			continue;
		if (exhausted == 0 && names[i] == NULL)
			exhausted = 1;
		if (exhausted || strlen(names[i]) == 0) {
			SET(unnamed, 1 << i);
			continue;
		}
		if (first == 0)
			printf(", ");
		else
			first = 0;
		printf("%s", names[i]);
	}
	if (unnamed != 0)
		printf("%s0x%08x", first ? "" : ", ", unnamed);
	printf(">");
}

void
scsi_show_inquiry_header(struct scsi_inquiry_data *inqbuf)
{
	switch (inqbuf->device & SID_QUAL) {
	case SID_QUAL_RSVD:
		printf("SID_QUAL_RSVD, ");
		break;
	case SID_QUAL_BAD_LU:
		printf("SID_QUAL_BAD_LU, ");
		break;
	case SID_QUAL_LU_OFFLINE:
		printf("SID_QUAL_LU_OFFLINE, ");
		break;
	case SID_QUAL_LU_OK:
		printf("SID_QUAL_LU_OK, ");
		break;
	default:
		printf("SID_QUAL = 0x%02x, ", inqbuf->device & SID_QUAL);
		break;
	}
	printf("%s, ", devicetypenames[inqbuf->device & SID_TYPE]);
	if (ISSET(inqbuf->dev_qual2, SID_REMOVABLE))
		printf("T_REMOV, ");
	else
		printf("T_FIXED, ");
	printf("SID_ANSII_REV %u, SID_RESPONSE_DATA_FMT %u\n",
	    SID_ANSII_REV(inqbuf),
	    inqbuf->response_format & SID_RESPONSE_DATA_FMT);
}

void
scsi_show_inquiry_match(struct scsi_inquiry_data *inqbuf)
{
	char				 visbuf[65];
	unsigned int			 inqbytes;

	inqbytes = SID_SCSI2_HDRLEN + inqbuf->additional_length;
	printf("<");
	if (inqbytes >= offsetof(struct scsi_inquiry_data, product))
		scsi_strvis(visbuf, inqbuf->vendor, sizeof(inqbuf->vendor));
	else
		visbuf[0] = '\0';
	printf("\"%s\", ", visbuf);
	if (inqbytes >= offsetof(struct scsi_inquiry_data, revision))
		scsi_strvis(visbuf, inqbuf->product, sizeof(inqbuf->product));
	else
		visbuf[0] = '\0';
	printf("\"%s\", ", visbuf);
	if (inqbytes >= offsetof(struct scsi_inquiry_data, extra))
		scsi_strvis(visbuf, inqbuf->revision, sizeof(inqbuf->revision));
	else
		visbuf[0] = '\0';
	printf("\"%s\">\n", visbuf);
}
#endif /* SCSIDEBUG */
