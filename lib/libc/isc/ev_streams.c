/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1996-1999 by Internet Software Consortium
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* ev_streams.c - implement asynch stream file IO for the eventlib
 * vix 04mar96 [initial]
 */

#if !defined(LINT) && !defined(CODECENTER)
static const char rcsid[] = "$Id: ev_streams.c,v 1.5 2005/04/27 04:56:36 sra Exp $";
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "port_before.h"
#ifndef _LIBC
#include "fd_setsize.h"
#endif

#include <sys/types.h>
#include <sys/uio.h>

#include <errno.h>

#include <isc/eventlib.h>
#ifndef _LIBC
#include <isc/assertions.h>
#endif
#include "eventlib_p.h"

#include "port_after.h"

#ifndef _LIBC
static int	copyvec(evStream *str, const struct iovec *iov, int iocnt);
static void	consume(evStream *str, size_t bytes);
static void	done(evContext opaqueCtx, evStream *str);
static void	writable(evContext opaqueCtx, void *uap, int fd, int evmask);
static void	readable(evContext opaqueCtx, void *uap, int fd, int evmask);
#endif

struct iovec
evConsIovec(void *buf, size_t cnt) {
	struct iovec ret;

	memset(&ret, 0xf5, sizeof ret);
	ret.iov_base = buf;
	ret.iov_len = cnt;
	return (ret);
}

#ifndef _LIBC
int
evWrite(evContext opaqueCtx, int fd, const struct iovec *iov, int iocnt,
	evStreamFunc func, void *uap, evStreamID *id)
{
	evContext_p *ctx = opaqueCtx.opaque;
	evStream *new;
	int save;

	OKNEW(new);
	new->func = func;
	new->uap = uap;
	new->fd = fd;
	new->flags = 0;
	if (evSelectFD(opaqueCtx, fd, EV_WRITE, writable, new, &new->file) < 0)
		goto free;
	if (copyvec(new, iov, iocnt) < 0)
		goto free;
	new->prevDone = NULL;
	new->nextDone = NULL;
	if (ctx->streams != NULL)
		ctx->streams->prev = new;
	new->prev = NULL;
	new->next = ctx->streams;
	ctx->streams = new;
	if (id != NULL)
		id->opaque = new;
	return (0);
 free:
	save = errno;
	FREE(new);
	errno = save;
	return (-1);
}

int
evRead(evContext opaqueCtx, int fd, const struct iovec *iov, int iocnt,
       evStreamFunc func, void *uap, evStreamID *id)
{
	evContext_p *ctx = opaqueCtx.opaque;
	evStream *new;
	int save;

	OKNEW(new);
	new->func = func;
	new->uap = uap;
	new->fd = fd;
	new->flags = 0;
	if (evSelectFD(opaqueCtx, fd, EV_READ, readable, new, &new->file) < 0)
		goto free;
	if (copyvec(new, iov, iocnt) < 0)
		goto free;
	new->prevDone = NULL;
	new->nextDone = NULL;
	if (ctx->streams != NULL)
		ctx->streams->prev = new;
	new->prev = NULL;
	new->next = ctx->streams;
	ctx->streams = new;
	if (id)
		id->opaque = new;
	return (0);
 free:
	save = errno;
	FREE(new);
	errno = save;
	return (-1);
}

int
evTimeRW(evContext opaqueCtx, evStreamID id, evTimerID timer) /*ARGSUSED*/ {
	evStream *str = id.opaque;

	UNUSED(opaqueCtx);

	str->timer = timer;
	str->flags |= EV_STR_TIMEROK;
	return (0);
}

int
evUntimeRW(evContext opaqueCtx, evStreamID id) /*ARGSUSED*/ {
	evStream *str = id.opaque;

	UNUSED(opaqueCtx);

	str->flags &= ~EV_STR_TIMEROK;
	return (0);
}

int
evCancelRW(evContext opaqueCtx, evStreamID id) {
	evContext_p *ctx = opaqueCtx.opaque;
	evStream *old = id.opaque;

	/*
	 * The streams list is doubly threaded.  First, there's ctx->streams
	 * that's used by evDestroy() to find and cancel all streams.  Second,
	 * there's ctx->strDone (head) and ctx->strLast (tail) which thread
	 * through the potentially smaller number of "IO completed" streams,
	 * used in evGetNext() to avoid scanning the entire list.
	 */

	/* Unlink from ctx->streams. */
	if (old->prev != NULL)
		old->prev->next = old->next;
	else
		ctx->streams = old->next;
	if (old->next != NULL)
		old->next->prev = old->prev;

	/*
	 * If 'old' is on the ctx->strDone list, remove it.  Update
	 * ctx->strLast if necessary.
	 */
	if (old->prevDone == NULL && old->nextDone == NULL) {
		/*
		 * Either 'old' is the only item on the done list, or it's
		 * not on the done list.  If the former, then we unlink it
		 * from the list.  If the latter, we leave the list alone.
		 */
		if (ctx->strDone == old) {
			ctx->strDone = NULL;
			ctx->strLast = NULL;
		}
	} else {
		if (old->prevDone != NULL)
			old->prevDone->nextDone = old->nextDone;
		else
			ctx->strDone = old->nextDone;
		if (old->nextDone != NULL)
			old->nextDone->prevDone = old->prevDone;
		else
			ctx->strLast = old->prevDone;
	}

	/* Deallocate the stream. */
	if (old->file.opaque)
		evDeselectFD(opaqueCtx, old->file);
	memput(old->iovOrig, sizeof (struct iovec) * old->iovOrigCount);
	FREE(old);
	return (0);
}

/* Copy a scatter/gather vector and initialize a stream handler's IO. */
static int
copyvec(evStream *str, const struct iovec *iov, int iocnt) {
	int i;

	str->iovOrig = (struct iovec *)memget(sizeof(struct iovec) * iocnt);
	if (str->iovOrig == NULL) {
		errno = ENOMEM;
		return (-1);
	}
	str->ioTotal = 0;
	for (i = 0; i < iocnt; i++) {
		str->iovOrig[i] = iov[i];
		str->ioTotal += iov[i].iov_len;
	}
	str->iovOrigCount = iocnt;
	str->iovCur = str->iovOrig;
	str->iovCurCount = str->iovOrigCount;
	str->ioDone = 0;
	return (0);
}

/* Pull off or truncate lead iovec(s). */
static void
consume(evStream *str, size_t bytes) {
	while (bytes > 0U) {
		if (bytes < (size_t)str->iovCur->iov_len) {
			str->iovCur->iov_len -= bytes;
			str->iovCur->iov_base = (void *)
				((u_char *)str->iovCur->iov_base + bytes);
			str->ioDone += bytes;
			bytes = 0;
		} else {
			bytes -= str->iovCur->iov_len;
			str->ioDone += str->iovCur->iov_len;
			str->iovCur++;
			str->iovCurCount--;
		}
	}
}

/* Add a stream to Done list and deselect the FD. */
static void
done(evContext opaqueCtx, evStream *str) {
	evContext_p *ctx = opaqueCtx.opaque;

	if (ctx->strLast != NULL) {
		str->prevDone = ctx->strLast;
		ctx->strLast->nextDone = str;
		ctx->strLast = str;
	} else {
		INSIST(ctx->strDone == NULL);
		ctx->strDone = ctx->strLast = str;
	}
	evDeselectFD(opaqueCtx, str->file);
	str->file.opaque = NULL;
	/* evDrop() will call evCancelRW() on us. */
}

/* Dribble out some bytes on the stream.  (Called by evDispatch().) */
static void
writable(evContext opaqueCtx, void *uap, int fd, int evmask) {
	evStream *str = uap;
	int bytes;

	UNUSED(evmask);

	bytes = writev(fd, str->iovCur, str->iovCurCount);
	if (bytes > 0) {
		if ((str->flags & EV_STR_TIMEROK) != 0)
			evTouchIdleTimer(opaqueCtx, str->timer);
		consume(str, bytes);
	} else {
		if (bytes < 0 && errno != EINTR) {
			str->ioDone = -1;
			str->ioErrno = errno;
		}
	}
	if (str->ioDone == -1 || str->ioDone == str->ioTotal)
		done(opaqueCtx, str);
}

/* Scoop up some bytes from the stream.  (Called by evDispatch().) */
static void
readable(evContext opaqueCtx, void *uap, int fd, int evmask) {
	evStream *str = uap;
	int bytes;

	UNUSED(evmask);

	bytes = readv(fd, str->iovCur, str->iovCurCount);
	if (bytes > 0) {
		if ((str->flags & EV_STR_TIMEROK) != 0)
			evTouchIdleTimer(opaqueCtx, str->timer);
		consume(str, bytes);
	} else {
		if (bytes == 0)
			str->ioDone = 0;
		else {
			if (errno != EINTR) {
				str->ioDone = -1;
				str->ioErrno = errno;
			}
		}
	}
	if (str->ioDone <= 0 || str->ioDone == str->ioTotal)
		done(opaqueCtx, str);
}
#endif

/*! \file */
