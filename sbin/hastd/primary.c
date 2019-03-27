/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 The FreeBSD Foundation
 * Copyright (c) 2010-2011 Pawel Jakub Dawidek <pawel@dawidek.net>
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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
#include <sys/time.h>
#include <sys/bio.h>
#include <sys/disk.h>
#include <sys/stat.h>

#include <geom/gate/g_gate.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgeom.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <activemap.h>
#include <nv.h>
#include <rangelock.h>

#include "control.h"
#include "event.h"
#include "hast.h"
#include "hast_proto.h"
#include "hastd.h"
#include "hooks.h"
#include "metadata.h"
#include "proto.h"
#include "pjdlog.h"
#include "refcnt.h"
#include "subr.h"
#include "synch.h"

/* The is only one remote component for now. */
#define	ISREMOTE(no)	((no) == 1)

struct hio {
	/*
	 * Number of components we are still waiting for.
	 * When this field goes to 0, we can send the request back to the
	 * kernel. Each component has to decrease this counter by one
	 * even on failure.
	 */
	refcnt_t		 hio_countdown;
	/*
	 * Each component has a place to store its own error.
	 * Once the request is handled by all components we can decide if the
	 * request overall is successful or not.
	 */
	int			*hio_errors;
	/*
	 * Structure used to communicate with GEOM Gate class.
	 */
	struct g_gate_ctl_io	 hio_ggio;
	/*
	 * Request was already confirmed to GEOM Gate.
	 */
	bool			 hio_done;
	/*
	 * Number of components we are still waiting before sending write
	 * completion ack to GEOM Gate. Used for memsync.
	 */
	refcnt_t		 hio_writecount;
	/*
	 * Memsync request was acknowleged by remote.
	 */
	bool			 hio_memsyncacked;
	/*
	 * Remember replication from the time the request was initiated,
	 * so we won't get confused when replication changes on reload.
	 */
	int			 hio_replication;
	TAILQ_ENTRY(hio)	*hio_next;
};
#define	hio_free_next	hio_next[0]
#define	hio_done_next	hio_next[0]

/*
 * Free list holds unused structures. When free list is empty, we have to wait
 * until some in-progress requests are freed.
 */
static TAILQ_HEAD(, hio) hio_free_list;
static size_t hio_free_list_size;
static pthread_mutex_t hio_free_list_lock;
static pthread_cond_t hio_free_list_cond;
/*
 * There is one send list for every component. One requests is placed on all
 * send lists - each component gets the same request, but each component is
 * responsible for managing his own send list.
 */
static TAILQ_HEAD(, hio) *hio_send_list;
static size_t *hio_send_list_size;
static pthread_mutex_t *hio_send_list_lock;
static pthread_cond_t *hio_send_list_cond;
#define	hio_send_local_list_size	hio_send_list_size[0]
#define	hio_send_remote_list_size	hio_send_list_size[1]
/*
 * There is one recv list for every component, although local components don't
 * use recv lists as local requests are done synchronously.
 */
static TAILQ_HEAD(, hio) *hio_recv_list;
static size_t *hio_recv_list_size;
static pthread_mutex_t *hio_recv_list_lock;
static pthread_cond_t *hio_recv_list_cond;
#define	hio_recv_remote_list_size	hio_recv_list_size[1]
/*
 * Request is placed on done list by the slowest component (the one that
 * decreased hio_countdown from 1 to 0).
 */
static TAILQ_HEAD(, hio) hio_done_list;
static size_t hio_done_list_size;
static pthread_mutex_t hio_done_list_lock;
static pthread_cond_t hio_done_list_cond;
/*
 * Structure below are for interaction with sync thread.
 */
static bool sync_inprogress;
static pthread_mutex_t sync_lock;
static pthread_cond_t sync_cond;
/*
 * The lock below allows to synchornize access to remote connections.
 */
static pthread_rwlock_t *hio_remote_lock;

/*
 * Lock to synchronize metadata updates. Also synchronize access to
 * hr_primary_localcnt and hr_primary_remotecnt fields.
 */
static pthread_mutex_t metadata_lock;

/*
 * Maximum number of outstanding I/O requests.
 */
#define	HAST_HIO_MAX	256
/*
 * Number of components. At this point there are only two components: local
 * and remote, but in the future it might be possible to use multiple local
 * and remote components.
 */
#define	HAST_NCOMPONENTS	2

#define	ISCONNECTED(res, no)	\
	((res)->hr_remotein != NULL && (res)->hr_remoteout != NULL)

#define	QUEUE_INSERT1(hio, name, ncomp)	do {				\
	mtx_lock(&hio_##name##_list_lock[(ncomp)]);			\
	if (TAILQ_EMPTY(&hio_##name##_list[(ncomp)]))			\
		cv_broadcast(&hio_##name##_list_cond[(ncomp)]);		\
	TAILQ_INSERT_TAIL(&hio_##name##_list[(ncomp)], (hio),		\
	    hio_next[(ncomp)]);						\
	hio_##name##_list_size[(ncomp)]++;				\
	mtx_unlock(&hio_##name##_list_lock[(ncomp)]);			\
} while (0)
#define	QUEUE_INSERT2(hio, name)	do {				\
	mtx_lock(&hio_##name##_list_lock);				\
	if (TAILQ_EMPTY(&hio_##name##_list))				\
		cv_broadcast(&hio_##name##_list_cond);			\
	TAILQ_INSERT_TAIL(&hio_##name##_list, (hio), hio_##name##_next);\
	hio_##name##_list_size++;					\
	mtx_unlock(&hio_##name##_list_lock);				\
} while (0)
#define	QUEUE_TAKE1(hio, name, ncomp, timeout)	do {			\
	bool _last;							\
									\
	mtx_lock(&hio_##name##_list_lock[(ncomp)]);			\
	_last = false;							\
	while (((hio) = TAILQ_FIRST(&hio_##name##_list[(ncomp)])) == NULL && !_last) { \
		cv_timedwait(&hio_##name##_list_cond[(ncomp)],		\
		    &hio_##name##_list_lock[(ncomp)], (timeout));	\
		if ((timeout) != 0)					\
			_last = true;					\
	}								\
	if (hio != NULL) {						\
		PJDLOG_ASSERT(hio_##name##_list_size[(ncomp)] != 0);	\
		hio_##name##_list_size[(ncomp)]--;			\
		TAILQ_REMOVE(&hio_##name##_list[(ncomp)], (hio),	\
		    hio_next[(ncomp)]);					\
	}								\
	mtx_unlock(&hio_##name##_list_lock[(ncomp)]);			\
} while (0)
#define	QUEUE_TAKE2(hio, name)	do {					\
	mtx_lock(&hio_##name##_list_lock);				\
	while (((hio) = TAILQ_FIRST(&hio_##name##_list)) == NULL) {	\
		cv_wait(&hio_##name##_list_cond,			\
		    &hio_##name##_list_lock);				\
	}								\
	PJDLOG_ASSERT(hio_##name##_list_size != 0);			\
	hio_##name##_list_size--;					\
	TAILQ_REMOVE(&hio_##name##_list, (hio), hio_##name##_next);	\
	mtx_unlock(&hio_##name##_list_lock);				\
} while (0)

#define ISFULLSYNC(hio)	((hio)->hio_replication == HAST_REPLICATION_FULLSYNC)
#define ISMEMSYNC(hio)	((hio)->hio_replication == HAST_REPLICATION_MEMSYNC)
#define ISASYNC(hio)	((hio)->hio_replication == HAST_REPLICATION_ASYNC)

#define	SYNCREQ(hio)		do {					\
	(hio)->hio_ggio.gctl_unit = -1;					\
	(hio)->hio_ggio.gctl_seq = 1;					\
} while (0)
#define	ISSYNCREQ(hio)		((hio)->hio_ggio.gctl_unit == -1)
#define	SYNCREQDONE(hio)	do { (hio)->hio_ggio.gctl_unit = -2; } while (0)
#define	ISSYNCREQDONE(hio)	((hio)->hio_ggio.gctl_unit == -2)

#define ISMEMSYNCWRITE(hio)	(ISMEMSYNC(hio) &&			\
	    (hio)->hio_ggio.gctl_cmd == BIO_WRITE && !ISSYNCREQ(hio))

static struct hast_resource *gres;

static pthread_mutex_t range_lock;
static struct rangelocks *range_regular;
static bool range_regular_wait;
static pthread_cond_t range_regular_cond;
static struct rangelocks *range_sync;
static bool range_sync_wait;
static pthread_cond_t range_sync_cond;
static bool fullystarted;

static void *ggate_recv_thread(void *arg);
static void *local_send_thread(void *arg);
static void *remote_send_thread(void *arg);
static void *remote_recv_thread(void *arg);
static void *ggate_send_thread(void *arg);
static void *sync_thread(void *arg);
static void *guard_thread(void *arg);

static void
output_status_aux(struct nv *nvout)
{

	nv_add_uint64(nvout, (uint64_t)hio_free_list_size,
	    "idle_queue_size");
	nv_add_uint64(nvout, (uint64_t)hio_send_local_list_size,
	    "local_queue_size");
	nv_add_uint64(nvout, (uint64_t)hio_send_remote_list_size,
	    "send_queue_size");
	nv_add_uint64(nvout, (uint64_t)hio_recv_remote_list_size,
	    "recv_queue_size");
	nv_add_uint64(nvout, (uint64_t)hio_done_list_size,
	    "done_queue_size");
}

static void
cleanup(struct hast_resource *res)
{
	int rerrno;

	/* Remember errno. */
	rerrno = errno;

	/* Destroy ggate provider if we created one. */
	if (res->hr_ggateunit >= 0) {
		struct g_gate_ctl_destroy ggiod;

		bzero(&ggiod, sizeof(ggiod));
		ggiod.gctl_version = G_GATE_VERSION;
		ggiod.gctl_unit = res->hr_ggateunit;
		ggiod.gctl_force = 1;
		if (ioctl(res->hr_ggatefd, G_GATE_CMD_DESTROY, &ggiod) == -1) {
			pjdlog_errno(LOG_WARNING,
			    "Unable to destroy hast/%s device",
			    res->hr_provname);
		}
		res->hr_ggateunit = -1;
	}

	/* Restore errno. */
	errno = rerrno;
}

static __dead2 void
primary_exit(int exitcode, const char *fmt, ...)
{
	va_list ap;

	PJDLOG_ASSERT(exitcode != EX_OK);
	va_start(ap, fmt);
	pjdlogv_errno(LOG_ERR, fmt, ap);
	va_end(ap);
	cleanup(gres);
	exit(exitcode);
}

static __dead2 void
primary_exitx(int exitcode, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	pjdlogv(exitcode == EX_OK ? LOG_INFO : LOG_ERR, fmt, ap);
	va_end(ap);
	cleanup(gres);
	exit(exitcode);
}

static int
hast_activemap_flush(struct hast_resource *res) __unlocks(res->hr_amp_lock)
{
	const unsigned char *buf;
	size_t size;
	int ret;

	mtx_lock(&res->hr_amp_diskmap_lock);
	buf = activemap_bitmap(res->hr_amp, &size);
	mtx_unlock(&res->hr_amp_lock);
	PJDLOG_ASSERT(buf != NULL);
	PJDLOG_ASSERT((size % res->hr_local_sectorsize) == 0);
	ret = 0;
	if (pwrite(res->hr_localfd, buf, size, METADATA_SIZE) !=
	    (ssize_t)size) {
		pjdlog_errno(LOG_ERR, "Unable to flush activemap to disk");
		res->hr_stat_activemap_write_error++;
		ret = -1;
	}
	if (ret == 0 && res->hr_metaflush == 1 &&
	    g_flush(res->hr_localfd) == -1) {
		if (errno == EOPNOTSUPP) {
			pjdlog_warning("The %s provider doesn't support flushing write cache. Disabling it.",
			    res->hr_localpath);
			res->hr_metaflush = 0;
		} else {
			pjdlog_errno(LOG_ERR,
			    "Unable to flush disk cache on activemap update");
			res->hr_stat_activemap_flush_error++;
			ret = -1;
		}
	}
	mtx_unlock(&res->hr_amp_diskmap_lock);
	return (ret);
}

static bool
real_remote(const struct hast_resource *res)
{

	return (strcmp(res->hr_remoteaddr, "none") != 0);
}

static void
init_environment(struct hast_resource *res __unused)
{
	struct hio *hio;
	unsigned int ii, ncomps;

	/*
	 * In the future it might be per-resource value.
	 */
	ncomps = HAST_NCOMPONENTS;

	/*
	 * Allocate memory needed by lists.
	 */
	hio_send_list = malloc(sizeof(hio_send_list[0]) * ncomps);
	if (hio_send_list == NULL) {
		primary_exitx(EX_TEMPFAIL,
		    "Unable to allocate %zu bytes of memory for send lists.",
		    sizeof(hio_send_list[0]) * ncomps);
	}
	hio_send_list_size = malloc(sizeof(hio_send_list_size[0]) * ncomps);
	if (hio_send_list_size == NULL) {
		primary_exitx(EX_TEMPFAIL,
		    "Unable to allocate %zu bytes of memory for send list counters.",
		    sizeof(hio_send_list_size[0]) * ncomps);
	}
	hio_send_list_lock = malloc(sizeof(hio_send_list_lock[0]) * ncomps);
	if (hio_send_list_lock == NULL) {
		primary_exitx(EX_TEMPFAIL,
		    "Unable to allocate %zu bytes of memory for send list locks.",
		    sizeof(hio_send_list_lock[0]) * ncomps);
	}
	hio_send_list_cond = malloc(sizeof(hio_send_list_cond[0]) * ncomps);
	if (hio_send_list_cond == NULL) {
		primary_exitx(EX_TEMPFAIL,
		    "Unable to allocate %zu bytes of memory for send list condition variables.",
		    sizeof(hio_send_list_cond[0]) * ncomps);
	}
	hio_recv_list = malloc(sizeof(hio_recv_list[0]) * ncomps);
	if (hio_recv_list == NULL) {
		primary_exitx(EX_TEMPFAIL,
		    "Unable to allocate %zu bytes of memory for recv lists.",
		    sizeof(hio_recv_list[0]) * ncomps);
	}
	hio_recv_list_size = malloc(sizeof(hio_recv_list_size[0]) * ncomps);
	if (hio_recv_list_size == NULL) {
		primary_exitx(EX_TEMPFAIL,
		    "Unable to allocate %zu bytes of memory for recv list counters.",
		    sizeof(hio_recv_list_size[0]) * ncomps);
	}
	hio_recv_list_lock = malloc(sizeof(hio_recv_list_lock[0]) * ncomps);
	if (hio_recv_list_lock == NULL) {
		primary_exitx(EX_TEMPFAIL,
		    "Unable to allocate %zu bytes of memory for recv list locks.",
		    sizeof(hio_recv_list_lock[0]) * ncomps);
	}
	hio_recv_list_cond = malloc(sizeof(hio_recv_list_cond[0]) * ncomps);
	if (hio_recv_list_cond == NULL) {
		primary_exitx(EX_TEMPFAIL,
		    "Unable to allocate %zu bytes of memory for recv list condition variables.",
		    sizeof(hio_recv_list_cond[0]) * ncomps);
	}
	hio_remote_lock = malloc(sizeof(hio_remote_lock[0]) * ncomps);
	if (hio_remote_lock == NULL) {
		primary_exitx(EX_TEMPFAIL,
		    "Unable to allocate %zu bytes of memory for remote connections locks.",
		    sizeof(hio_remote_lock[0]) * ncomps);
	}

	/*
	 * Initialize lists, their counters, locks and condition variables.
	 */
	TAILQ_INIT(&hio_free_list);
	mtx_init(&hio_free_list_lock);
	cv_init(&hio_free_list_cond);
	for (ii = 0; ii < HAST_NCOMPONENTS; ii++) {
		TAILQ_INIT(&hio_send_list[ii]);
		hio_send_list_size[ii] = 0;
		mtx_init(&hio_send_list_lock[ii]);
		cv_init(&hio_send_list_cond[ii]);
		TAILQ_INIT(&hio_recv_list[ii]);
		hio_recv_list_size[ii] = 0;
		mtx_init(&hio_recv_list_lock[ii]);
		cv_init(&hio_recv_list_cond[ii]);
		rw_init(&hio_remote_lock[ii]);
	}
	TAILQ_INIT(&hio_done_list);
	mtx_init(&hio_done_list_lock);
	cv_init(&hio_done_list_cond);
	mtx_init(&metadata_lock);

	/*
	 * Allocate requests pool and initialize requests.
	 */
	for (ii = 0; ii < HAST_HIO_MAX; ii++) {
		hio = malloc(sizeof(*hio));
		if (hio == NULL) {
			primary_exitx(EX_TEMPFAIL,
			    "Unable to allocate %zu bytes of memory for hio request.",
			    sizeof(*hio));
		}
		refcnt_init(&hio->hio_countdown, 0);
		hio->hio_errors = malloc(sizeof(hio->hio_errors[0]) * ncomps);
		if (hio->hio_errors == NULL) {
			primary_exitx(EX_TEMPFAIL,
			    "Unable allocate %zu bytes of memory for hio errors.",
			    sizeof(hio->hio_errors[0]) * ncomps);
		}
		hio->hio_next = malloc(sizeof(hio->hio_next[0]) * ncomps);
		if (hio->hio_next == NULL) {
			primary_exitx(EX_TEMPFAIL,
			    "Unable allocate %zu bytes of memory for hio_next field.",
			    sizeof(hio->hio_next[0]) * ncomps);
		}
		hio->hio_ggio.gctl_version = G_GATE_VERSION;
		hio->hio_ggio.gctl_data = malloc(MAXPHYS);
		if (hio->hio_ggio.gctl_data == NULL) {
			primary_exitx(EX_TEMPFAIL,
			    "Unable to allocate %zu bytes of memory for gctl_data.",
			    MAXPHYS);
		}
		hio->hio_ggio.gctl_length = MAXPHYS;
		hio->hio_ggio.gctl_error = 0;
		TAILQ_INSERT_HEAD(&hio_free_list, hio, hio_free_next);
		hio_free_list_size++;
	}
}

static bool
init_resuid(struct hast_resource *res)
{

	mtx_lock(&metadata_lock);
	if (res->hr_resuid != 0) {
		mtx_unlock(&metadata_lock);
		return (false);
	} else {
		/* Initialize unique resource identifier. */
		arc4random_buf(&res->hr_resuid, sizeof(res->hr_resuid));
		mtx_unlock(&metadata_lock);
		if (metadata_write(res) == -1)
			exit(EX_NOINPUT);
		return (true);
	}
}

static void
init_local(struct hast_resource *res)
{
	unsigned char *buf;
	size_t mapsize;

	if (metadata_read(res, true) == -1)
		exit(EX_NOINPUT);
	mtx_init(&res->hr_amp_lock);
	if (activemap_init(&res->hr_amp, res->hr_datasize, res->hr_extentsize,
	    res->hr_local_sectorsize, res->hr_keepdirty) == -1) {
		primary_exit(EX_TEMPFAIL, "Unable to create activemap");
	}
	mtx_init(&range_lock);
	cv_init(&range_regular_cond);
	if (rangelock_init(&range_regular) == -1)
		primary_exit(EX_TEMPFAIL, "Unable to create regular range lock");
	cv_init(&range_sync_cond);
	if (rangelock_init(&range_sync) == -1)
		primary_exit(EX_TEMPFAIL, "Unable to create sync range lock");
	mapsize = activemap_ondisk_size(res->hr_amp);
	buf = calloc(1, mapsize);
	if (buf == NULL) {
		primary_exitx(EX_TEMPFAIL,
		    "Unable to allocate buffer for activemap.");
	}
	if (pread(res->hr_localfd, buf, mapsize, METADATA_SIZE) !=
	    (ssize_t)mapsize) {
		primary_exit(EX_NOINPUT, "Unable to read activemap");
	}
	activemap_copyin(res->hr_amp, buf, mapsize);
	free(buf);
	if (res->hr_resuid != 0)
		return;
	/*
	 * We're using provider for the first time. Initialize local and remote
	 * counters. We don't initialize resuid here, as we want to do it just
	 * in time. The reason for this is that we want to inform secondary
	 * that there were no writes yet, so there is no need to synchronize
	 * anything.
	 */
	res->hr_primary_localcnt = 0;
	res->hr_primary_remotecnt = 0;
	if (metadata_write(res) == -1)
		exit(EX_NOINPUT);
}

static int
primary_connect(struct hast_resource *res, struct proto_conn **connp)
{
	struct proto_conn *conn;
	int16_t val;

	val = 1;
	if (proto_send(res->hr_conn, &val, sizeof(val)) == -1) {
		primary_exit(EX_TEMPFAIL,
		    "Unable to send connection request to parent");
	}
	if (proto_recv(res->hr_conn, &val, sizeof(val)) == -1) {
		primary_exit(EX_TEMPFAIL,
		    "Unable to receive reply to connection request from parent");
	}
	if (val != 0) {
		errno = val;
		pjdlog_errno(LOG_WARNING, "Unable to connect to %s",
		    res->hr_remoteaddr);
		return (-1);
	}
	if (proto_connection_recv(res->hr_conn, true, &conn) == -1) {
		primary_exit(EX_TEMPFAIL,
		    "Unable to receive connection from parent");
	}
	if (proto_connect_wait(conn, res->hr_timeout) == -1) {
		pjdlog_errno(LOG_WARNING, "Unable to connect to %s",
		    res->hr_remoteaddr);
		proto_close(conn);
		return (-1);
	}
	/* Error in setting timeout is not critical, but why should it fail? */
	if (proto_timeout(conn, res->hr_timeout) == -1)
		pjdlog_errno(LOG_WARNING, "Unable to set connection timeout");

	*connp = conn;

	return (0);
}

/*
 * Function instructs GEOM_GATE to handle reads directly from within the kernel.
 */
static void
enable_direct_reads(struct hast_resource *res)
{
	struct g_gate_ctl_modify ggiomodify;

	bzero(&ggiomodify, sizeof(ggiomodify));
	ggiomodify.gctl_version = G_GATE_VERSION;
	ggiomodify.gctl_unit = res->hr_ggateunit;
	ggiomodify.gctl_modify = GG_MODIFY_READPROV | GG_MODIFY_READOFFSET;
	strlcpy(ggiomodify.gctl_readprov, res->hr_localpath,
	    sizeof(ggiomodify.gctl_readprov));
	ggiomodify.gctl_readoffset = res->hr_localoff;
	if (ioctl(res->hr_ggatefd, G_GATE_CMD_MODIFY, &ggiomodify) == 0)
		pjdlog_debug(1, "Direct reads enabled.");
	else
		pjdlog_errno(LOG_WARNING, "Failed to enable direct reads");
}

static int
init_remote(struct hast_resource *res, struct proto_conn **inp,
    struct proto_conn **outp)
{
	struct proto_conn *in, *out;
	struct nv *nvout, *nvin;
	const unsigned char *token;
	unsigned char *map;
	const char *errmsg;
	int32_t extentsize;
	int64_t datasize;
	uint32_t mapsize;
	uint8_t version;
	size_t size;
	int error;

	PJDLOG_ASSERT((inp == NULL && outp == NULL) || (inp != NULL && outp != NULL));
	PJDLOG_ASSERT(real_remote(res));

	in = out = NULL;
	errmsg = NULL;

	if (primary_connect(res, &out) == -1)
		return (ECONNREFUSED);

	error = ECONNABORTED;

	/*
	 * First handshake step.
	 * Setup outgoing connection with remote node.
	 */
	nvout = nv_alloc();
	nv_add_string(nvout, res->hr_name, "resource");
	nv_add_uint8(nvout, HAST_PROTO_VERSION, "version");
	if (nv_error(nvout) != 0) {
		pjdlog_common(LOG_WARNING, 0, nv_error(nvout),
		    "Unable to allocate header for connection with %s",
		    res->hr_remoteaddr);
		nv_free(nvout);
		goto close;
	}
	if (hast_proto_send(res, out, nvout, NULL, 0) == -1) {
		pjdlog_errno(LOG_WARNING,
		    "Unable to send handshake header to %s",
		    res->hr_remoteaddr);
		nv_free(nvout);
		goto close;
	}
	nv_free(nvout);
	if (hast_proto_recv_hdr(out, &nvin) == -1) {
		pjdlog_errno(LOG_WARNING,
		    "Unable to receive handshake header from %s",
		    res->hr_remoteaddr);
		goto close;
	}
	errmsg = nv_get_string(nvin, "errmsg");
	if (errmsg != NULL) {
		pjdlog_warning("%s", errmsg);
		if (nv_exists(nvin, "wait"))
			error = EBUSY;
		nv_free(nvin);
		goto close;
	}
	version = nv_get_uint8(nvin, "version");
	if (version == 0) {
		/*
		 * If no version is sent, it means this is protocol version 1.
		 */
		version = 1;
	}
	if (version > HAST_PROTO_VERSION) {
		pjdlog_warning("Invalid version received (%hhu).", version);
		nv_free(nvin);
		goto close;
	}
	res->hr_version = version;
	pjdlog_debug(1, "Negotiated protocol version %d.", res->hr_version);
	token = nv_get_uint8_array(nvin, &size, "token");
	if (token == NULL) {
		pjdlog_warning("Handshake header from %s has no 'token' field.",
		    res->hr_remoteaddr);
		nv_free(nvin);
		goto close;
	}
	if (size != sizeof(res->hr_token)) {
		pjdlog_warning("Handshake header from %s contains 'token' of wrong size (got %zu, expected %zu).",
		    res->hr_remoteaddr, size, sizeof(res->hr_token));
		nv_free(nvin);
		goto close;
	}
	bcopy(token, res->hr_token, sizeof(res->hr_token));
	nv_free(nvin);

	/*
	 * Second handshake step.
	 * Setup incoming connection with remote node.
	 */
	if (primary_connect(res, &in) == -1)
		goto close;

	nvout = nv_alloc();
	nv_add_string(nvout, res->hr_name, "resource");
	nv_add_uint8_array(nvout, res->hr_token, sizeof(res->hr_token),
	    "token");
	if (res->hr_resuid == 0) {
		/*
		 * The resuid field was not yet initialized.
		 * Because we do synchronization inside init_resuid(), it is
		 * possible that someone already initialized it, the function
		 * will return false then, but if we successfully initialized
		 * it, we will get true. True means that there were no writes
		 * to this resource yet and we want to inform secondary that
		 * synchronization is not needed by sending "virgin" argument.
		 */
		if (init_resuid(res))
			nv_add_int8(nvout, 1, "virgin");
	}
	nv_add_uint64(nvout, res->hr_resuid, "resuid");
	nv_add_uint64(nvout, res->hr_primary_localcnt, "localcnt");
	nv_add_uint64(nvout, res->hr_primary_remotecnt, "remotecnt");
	if (nv_error(nvout) != 0) {
		pjdlog_common(LOG_WARNING, 0, nv_error(nvout),
		    "Unable to allocate header for connection with %s",
		    res->hr_remoteaddr);
		nv_free(nvout);
		goto close;
	}
	if (hast_proto_send(res, in, nvout, NULL, 0) == -1) {
		pjdlog_errno(LOG_WARNING,
		    "Unable to send handshake header to %s",
		    res->hr_remoteaddr);
		nv_free(nvout);
		goto close;
	}
	nv_free(nvout);
	if (hast_proto_recv_hdr(out, &nvin) == -1) {
		pjdlog_errno(LOG_WARNING,
		    "Unable to receive handshake header from %s",
		    res->hr_remoteaddr);
		goto close;
	}
	errmsg = nv_get_string(nvin, "errmsg");
	if (errmsg != NULL) {
		pjdlog_warning("%s", errmsg);
		nv_free(nvin);
		goto close;
	}
	datasize = nv_get_int64(nvin, "datasize");
	if (datasize != res->hr_datasize) {
		pjdlog_warning("Data size differs between nodes (local=%jd, remote=%jd).",
		    (intmax_t)res->hr_datasize, (intmax_t)datasize);
		nv_free(nvin);
		goto close;
	}
	extentsize = nv_get_int32(nvin, "extentsize");
	if (extentsize != res->hr_extentsize) {
		pjdlog_warning("Extent size differs between nodes (local=%zd, remote=%zd).",
		    (ssize_t)res->hr_extentsize, (ssize_t)extentsize);
		nv_free(nvin);
		goto close;
	}
	res->hr_secondary_localcnt = nv_get_uint64(nvin, "localcnt");
	res->hr_secondary_remotecnt = nv_get_uint64(nvin, "remotecnt");
	res->hr_syncsrc = nv_get_uint8(nvin, "syncsrc");
	if (res->hr_syncsrc == HAST_SYNCSRC_PRIMARY)
		enable_direct_reads(res);
	if (nv_exists(nvin, "virgin")) {
		/*
		 * Secondary was reinitialized, bump localcnt if it is 0 as
		 * only we have the data.
		 */
		PJDLOG_ASSERT(res->hr_syncsrc == HAST_SYNCSRC_PRIMARY);
		PJDLOG_ASSERT(res->hr_secondary_localcnt == 0);

		if (res->hr_primary_localcnt == 0) {
			PJDLOG_ASSERT(res->hr_secondary_remotecnt == 0);

			mtx_lock(&metadata_lock);
			res->hr_primary_localcnt++;
			pjdlog_debug(1, "Increasing localcnt to %ju.",
			    (uintmax_t)res->hr_primary_localcnt);
			(void)metadata_write(res);
			mtx_unlock(&metadata_lock);
		}
	}
	map = NULL;
	mapsize = nv_get_uint32(nvin, "mapsize");
	if (mapsize > 0) {
		map = malloc(mapsize);
		if (map == NULL) {
			pjdlog_error("Unable to allocate memory for remote activemap (mapsize=%ju).",
			    (uintmax_t)mapsize);
			nv_free(nvin);
			goto close;
		}
		/*
		 * Remote node have some dirty extents on its own, lets
		 * download its activemap.
		 */
		if (hast_proto_recv_data(res, out, nvin, map,
		    mapsize) == -1) {
			pjdlog_errno(LOG_ERR,
			    "Unable to receive remote activemap");
			nv_free(nvin);
			free(map);
			goto close;
		}
		mtx_lock(&res->hr_amp_lock);
		/*
		 * Merge local and remote bitmaps.
		 */
		activemap_merge(res->hr_amp, map, mapsize);
		free(map);
		/*
		 * Now that we merged bitmaps from both nodes, flush it to the
		 * disk before we start to synchronize.
		 */
		(void)hast_activemap_flush(res);
	}
	nv_free(nvin);
#ifdef notyet
	/* Setup directions. */
	if (proto_send(out, NULL, 0) == -1)
		pjdlog_errno(LOG_WARNING, "Unable to set connection direction");
	if (proto_recv(in, NULL, 0) == -1)
		pjdlog_errno(LOG_WARNING, "Unable to set connection direction");
#endif
	pjdlog_info("Connected to %s.", res->hr_remoteaddr);
	if (res->hr_original_replication == HAST_REPLICATION_MEMSYNC &&
	    res->hr_version < 2) {
		pjdlog_warning("The 'memsync' replication mode is not supported by the remote node, falling back to 'fullsync' mode.");
		res->hr_replication = HAST_REPLICATION_FULLSYNC;
	} else if (res->hr_replication != res->hr_original_replication) {
		/*
		 * This is in case hastd disconnected and was upgraded.
		 */
		res->hr_replication = res->hr_original_replication;
	}
	if (inp != NULL && outp != NULL) {
		*inp = in;
		*outp = out;
	} else {
		res->hr_remotein = in;
		res->hr_remoteout = out;
	}
	event_send(res, EVENT_CONNECT);
	return (0);
close:
	if (errmsg != NULL && strcmp(errmsg, "Split-brain condition!") == 0)
		event_send(res, EVENT_SPLITBRAIN);
	proto_close(out);
	if (in != NULL)
		proto_close(in);
	return (error);
}

static void
sync_start(void)
{

	mtx_lock(&sync_lock);
	sync_inprogress = true;
	mtx_unlock(&sync_lock);
	cv_signal(&sync_cond);
}

static void
sync_stop(void)
{

	mtx_lock(&sync_lock);
	if (sync_inprogress)
		sync_inprogress = false;
	mtx_unlock(&sync_lock);
}

static void
init_ggate(struct hast_resource *res)
{
	struct g_gate_ctl_create ggiocreate;
	struct g_gate_ctl_cancel ggiocancel;

	/*
	 * We communicate with ggate via /dev/ggctl. Open it.
	 */
	res->hr_ggatefd = open("/dev/" G_GATE_CTL_NAME, O_RDWR);
	if (res->hr_ggatefd == -1)
		primary_exit(EX_OSFILE, "Unable to open /dev/" G_GATE_CTL_NAME);
	/*
	 * Create provider before trying to connect, as connection failure
	 * is not critical, but may take some time.
	 */
	bzero(&ggiocreate, sizeof(ggiocreate));
	ggiocreate.gctl_version = G_GATE_VERSION;
	ggiocreate.gctl_mediasize = res->hr_datasize;
	ggiocreate.gctl_sectorsize = res->hr_local_sectorsize;
	ggiocreate.gctl_flags = 0;
	ggiocreate.gctl_maxcount = 0;
	ggiocreate.gctl_timeout = 0;
	ggiocreate.gctl_unit = G_GATE_NAME_GIVEN;
	snprintf(ggiocreate.gctl_name, sizeof(ggiocreate.gctl_name), "hast/%s",
	    res->hr_provname);
	if (ioctl(res->hr_ggatefd, G_GATE_CMD_CREATE, &ggiocreate) == 0) {
		pjdlog_info("Device hast/%s created.", res->hr_provname);
		res->hr_ggateunit = ggiocreate.gctl_unit;
		return;
	}
	if (errno != EEXIST) {
		primary_exit(EX_OSERR, "Unable to create hast/%s device",
		    res->hr_provname);
	}
	pjdlog_debug(1,
	    "Device hast/%s already exists, we will try to take it over.",
	    res->hr_provname);
	/*
	 * If we received EEXIST, we assume that the process who created the
	 * provider died and didn't clean up. In that case we will start from
	 * where he left of.
	 */
	bzero(&ggiocancel, sizeof(ggiocancel));
	ggiocancel.gctl_version = G_GATE_VERSION;
	ggiocancel.gctl_unit = G_GATE_NAME_GIVEN;
	snprintf(ggiocancel.gctl_name, sizeof(ggiocancel.gctl_name), "hast/%s",
	    res->hr_provname);
	if (ioctl(res->hr_ggatefd, G_GATE_CMD_CANCEL, &ggiocancel) == 0) {
		pjdlog_info("Device hast/%s recovered.", res->hr_provname);
		res->hr_ggateunit = ggiocancel.gctl_unit;
		return;
	}
	primary_exit(EX_OSERR, "Unable to take over hast/%s device",
	    res->hr_provname);
}

void
hastd_primary(struct hast_resource *res)
{
	pthread_t td;
	pid_t pid;
	int error, mode, debuglevel;

	/*
	 * Create communication channel for sending control commands from
	 * parent to child.
	 */
	if (proto_client(NULL, "socketpair://", &res->hr_ctrl) == -1) {
		/* TODO: There's no need for this to be fatal error. */
		KEEP_ERRNO((void)pidfile_remove(pfh));
		pjdlog_exit(EX_OSERR,
		    "Unable to create control sockets between parent and child");
	}
	/*
	 * Create communication channel for sending events from child to parent.
	 */
	if (proto_client(NULL, "socketpair://", &res->hr_event) == -1) {
		/* TODO: There's no need for this to be fatal error. */
		KEEP_ERRNO((void)pidfile_remove(pfh));
		pjdlog_exit(EX_OSERR,
		    "Unable to create event sockets between child and parent");
	}
	/*
	 * Create communication channel for sending connection requests from
	 * child to parent.
	 */
	if (proto_client(NULL, "socketpair://", &res->hr_conn) == -1) {
		/* TODO: There's no need for this to be fatal error. */
		KEEP_ERRNO((void)pidfile_remove(pfh));
		pjdlog_exit(EX_OSERR,
		    "Unable to create connection sockets between child and parent");
	}

	pid = fork();
	if (pid == -1) {
		/* TODO: There's no need for this to be fatal error. */
		KEEP_ERRNO((void)pidfile_remove(pfh));
		pjdlog_exit(EX_TEMPFAIL, "Unable to fork");
	}

	if (pid > 0) {
		/* This is parent. */
		/* Declare that we are receiver. */
		proto_recv(res->hr_event, NULL, 0);
		proto_recv(res->hr_conn, NULL, 0);
		/* Declare that we are sender. */
		proto_send(res->hr_ctrl, NULL, 0);
		res->hr_workerpid = pid;
		return;
	}

	gres = res;
	res->output_status_aux = output_status_aux;
	mode = pjdlog_mode_get();
	debuglevel = pjdlog_debug_get();

	/* Declare that we are sender. */
	proto_send(res->hr_event, NULL, 0);
	proto_send(res->hr_conn, NULL, 0);
	/* Declare that we are receiver. */
	proto_recv(res->hr_ctrl, NULL, 0);
	descriptors_cleanup(res);

	descriptors_assert(res, mode);

	pjdlog_init(mode);
	pjdlog_debug_set(debuglevel);
	pjdlog_prefix_set("[%s] (%s) ", res->hr_name, role2str(res->hr_role));
	setproctitle("%s (%s)", res->hr_name, role2str(res->hr_role));

	init_local(res);
	init_ggate(res);
	init_environment(res);

	if (drop_privs(res) != 0) {
		cleanup(res);
		exit(EX_CONFIG);
	}
	pjdlog_info("Privileges successfully dropped.");

	/*
	 * Create the guard thread first, so we can handle signals from the
	 * very beginning.
	 */
	error = pthread_create(&td, NULL, guard_thread, res);
	PJDLOG_ASSERT(error == 0);
	/*
	 * Create the control thread before sending any event to the parent,
	 * as we can deadlock when parent sends control request to worker,
	 * but worker has no control thread started yet, so parent waits.
	 * In the meantime worker sends an event to the parent, but parent
	 * is unable to handle the event, because it waits for control
	 * request response.
	 */
	error = pthread_create(&td, NULL, ctrl_thread, res);
	PJDLOG_ASSERT(error == 0);
	if (real_remote(res)) {
		error = init_remote(res, NULL, NULL);
		if (error == 0) {
			sync_start();
		} else if (error == EBUSY) {
			time_t start = time(NULL);

			pjdlog_warning("Waiting for remote node to become %s for %ds.",
			    role2str(HAST_ROLE_SECONDARY),
			    res->hr_timeout);
			for (;;) {
				sleep(1);
				error = init_remote(res, NULL, NULL);
				if (error != EBUSY)
					break;
				if (time(NULL) > start + res->hr_timeout)
					break;
			}
			if (error == EBUSY) {
				pjdlog_warning("Remote node is still %s, starting anyway.",
				    role2str(HAST_ROLE_PRIMARY));
			}
		}
	}
	error = pthread_create(&td, NULL, ggate_recv_thread, res);
	PJDLOG_ASSERT(error == 0);
	error = pthread_create(&td, NULL, local_send_thread, res);
	PJDLOG_ASSERT(error == 0);
	error = pthread_create(&td, NULL, remote_send_thread, res);
	PJDLOG_ASSERT(error == 0);
	error = pthread_create(&td, NULL, remote_recv_thread, res);
	PJDLOG_ASSERT(error == 0);
	error = pthread_create(&td, NULL, ggate_send_thread, res);
	PJDLOG_ASSERT(error == 0);
	fullystarted = true;
	(void)sync_thread(res);
}

static void
reqlog(int loglevel, int debuglevel, struct g_gate_ctl_io *ggio,
    const char *fmt, ...)
{
	char msg[1024];
	va_list ap;

	va_start(ap, fmt);
	(void)vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	switch (ggio->gctl_cmd) {
	case BIO_READ:
		(void)snprlcat(msg, sizeof(msg), "READ(%ju, %ju).",
		    (uintmax_t)ggio->gctl_offset, (uintmax_t)ggio->gctl_length);
		break;
	case BIO_DELETE:
		(void)snprlcat(msg, sizeof(msg), "DELETE(%ju, %ju).",
		    (uintmax_t)ggio->gctl_offset, (uintmax_t)ggio->gctl_length);
		break;
	case BIO_FLUSH:
		(void)snprlcat(msg, sizeof(msg), "FLUSH.");
		break;
	case BIO_WRITE:
		(void)snprlcat(msg, sizeof(msg), "WRITE(%ju, %ju).",
		    (uintmax_t)ggio->gctl_offset, (uintmax_t)ggio->gctl_length);
		break;
	default:
		(void)snprlcat(msg, sizeof(msg), "UNKNOWN(%u).",
		    (unsigned int)ggio->gctl_cmd);
		break;
	}
	pjdlog_common(loglevel, debuglevel, -1, "%s", msg);
}

static void
remote_close(struct hast_resource *res, int ncomp)
{

	rw_wlock(&hio_remote_lock[ncomp]);
	/*
	 * Check for a race between dropping rlock and acquiring wlock -
	 * another thread can close connection in-between.
	 */
	if (!ISCONNECTED(res, ncomp)) {
		PJDLOG_ASSERT(res->hr_remotein == NULL);
		PJDLOG_ASSERT(res->hr_remoteout == NULL);
		rw_unlock(&hio_remote_lock[ncomp]);
		return;
	}

	PJDLOG_ASSERT(res->hr_remotein != NULL);
	PJDLOG_ASSERT(res->hr_remoteout != NULL);

	pjdlog_debug(2, "Closing incoming connection to %s.",
	    res->hr_remoteaddr);
	proto_close(res->hr_remotein);
	res->hr_remotein = NULL;
	pjdlog_debug(2, "Closing outgoing connection to %s.",
	    res->hr_remoteaddr);
	proto_close(res->hr_remoteout);
	res->hr_remoteout = NULL;

	rw_unlock(&hio_remote_lock[ncomp]);

	pjdlog_warning("Disconnected from %s.", res->hr_remoteaddr);

	/*
	 * Stop synchronization if in-progress.
	 */
	sync_stop();

	event_send(res, EVENT_DISCONNECT);
}

/*
 * Acknowledge write completion to the kernel, but don't update activemap yet.
 */
static void
write_complete(struct hast_resource *res, struct hio *hio)
{
	struct g_gate_ctl_io *ggio;
	unsigned int ncomp;

	PJDLOG_ASSERT(!hio->hio_done);

	ggio = &hio->hio_ggio;
	PJDLOG_ASSERT(ggio->gctl_cmd == BIO_WRITE);

	/*
	 * Bump local count if this is first write after
	 * connection failure with remote node.
	 */
	ncomp = 1;
	rw_rlock(&hio_remote_lock[ncomp]);
	if (!ISCONNECTED(res, ncomp)) {
		mtx_lock(&metadata_lock);
		if (res->hr_primary_localcnt == res->hr_secondary_remotecnt) {
			res->hr_primary_localcnt++;
			pjdlog_debug(1, "Increasing localcnt to %ju.",
			    (uintmax_t)res->hr_primary_localcnt);
			(void)metadata_write(res);
		}
		mtx_unlock(&metadata_lock);
	}
	rw_unlock(&hio_remote_lock[ncomp]);
	if (ioctl(res->hr_ggatefd, G_GATE_CMD_DONE, ggio) == -1)
		primary_exit(EX_OSERR, "G_GATE_CMD_DONE failed");
	hio->hio_done = true;
}

/*
 * Thread receives ggate I/O requests from the kernel and passes them to
 * appropriate threads:
 * WRITE - always goes to both local_send and remote_send threads
 * READ (when the block is up-to-date on local component) -
 *	only local_send thread
 * READ (when the block isn't up-to-date on local component) -
 *	only remote_send thread
 * DELETE - always goes to both local_send and remote_send threads
 * FLUSH - always goes to both local_send and remote_send threads
 */
static void *
ggate_recv_thread(void *arg)
{
	struct hast_resource *res = arg;
	struct g_gate_ctl_io *ggio;
	struct hio *hio;
	unsigned int ii, ncomp, ncomps;
	int error;

	for (;;) {
		pjdlog_debug(2, "ggate_recv: Taking free request.");
		QUEUE_TAKE2(hio, free);
		pjdlog_debug(2, "ggate_recv: (%p) Got free request.", hio);
		ggio = &hio->hio_ggio;
		ggio->gctl_unit = res->hr_ggateunit;
		ggio->gctl_length = MAXPHYS;
		ggio->gctl_error = 0;
		hio->hio_done = false;
		hio->hio_replication = res->hr_replication;
		pjdlog_debug(2,
		    "ggate_recv: (%p) Waiting for request from the kernel.",
		    hio);
		if (ioctl(res->hr_ggatefd, G_GATE_CMD_START, ggio) == -1) {
			if (sigexit_received)
				pthread_exit(NULL);
			primary_exit(EX_OSERR, "G_GATE_CMD_START failed");
		}
		error = ggio->gctl_error;
		switch (error) {
		case 0:
			break;
		case ECANCELED:
			/* Exit gracefully. */
			if (!sigexit_received) {
				pjdlog_debug(2,
				    "ggate_recv: (%p) Received cancel from the kernel.",
				    hio);
				pjdlog_info("Received cancel from the kernel, exiting.");
			}
			pthread_exit(NULL);
		case ENOMEM:
			/*
			 * Buffer too small? Impossible, we allocate MAXPHYS
			 * bytes - request can't be bigger than that.
			 */
			/* FALLTHROUGH */
		case ENXIO:
		default:
			primary_exitx(EX_OSERR, "G_GATE_CMD_START failed: %s.",
			    strerror(error));
		}

		ncomp = 0;
		ncomps = HAST_NCOMPONENTS;

		for (ii = 0; ii < ncomps; ii++)
			hio->hio_errors[ii] = EINVAL;
		reqlog(LOG_DEBUG, 2, ggio,
		    "ggate_recv: (%p) Request received from the kernel: ",
		    hio);

		/*
		 * Inform all components about new write request.
		 * For read request prefer local component unless the given
		 * range is out-of-date, then use remote component.
		 */
		switch (ggio->gctl_cmd) {
		case BIO_READ:
			res->hr_stat_read++;
			ncomps = 1;
			mtx_lock(&metadata_lock);
			if (res->hr_syncsrc == HAST_SYNCSRC_UNDEF ||
			    res->hr_syncsrc == HAST_SYNCSRC_PRIMARY) {
				/*
				 * This range is up-to-date on local component,
				 * so handle request locally.
				 */
				 /* Local component is 0 for now. */
				ncomp = 0;
			} else /* if (res->hr_syncsrc ==
			    HAST_SYNCSRC_SECONDARY) */ {
				PJDLOG_ASSERT(res->hr_syncsrc ==
				    HAST_SYNCSRC_SECONDARY);
				/*
				 * This range is out-of-date on local component,
				 * so send request to the remote node.
				 */
				 /* Remote component is 1 for now. */
				ncomp = 1;
			}
			mtx_unlock(&metadata_lock);
			break;
		case BIO_WRITE:
			res->hr_stat_write++;
			if (res->hr_resuid == 0 &&
			    res->hr_primary_localcnt == 0) {
				/* This is first write. */
				res->hr_primary_localcnt = 1;
			}
			for (;;) {
				mtx_lock(&range_lock);
				if (rangelock_islocked(range_sync,
				    ggio->gctl_offset, ggio->gctl_length)) {
					pjdlog_debug(2,
					    "regular: Range offset=%jd length=%zu locked.",
					    (intmax_t)ggio->gctl_offset,
					    (size_t)ggio->gctl_length);
					range_regular_wait = true;
					cv_wait(&range_regular_cond, &range_lock);
					range_regular_wait = false;
					mtx_unlock(&range_lock);
					continue;
				}
				if (rangelock_add(range_regular,
				    ggio->gctl_offset, ggio->gctl_length) == -1) {
					mtx_unlock(&range_lock);
					pjdlog_debug(2,
					    "regular: Range offset=%jd length=%zu is already locked, waiting.",
					    (intmax_t)ggio->gctl_offset,
					    (size_t)ggio->gctl_length);
					sleep(1);
					continue;
				}
				mtx_unlock(&range_lock);
				break;
			}
			mtx_lock(&res->hr_amp_lock);
			if (activemap_write_start(res->hr_amp,
			    ggio->gctl_offset, ggio->gctl_length)) {
				res->hr_stat_activemap_update++;
				(void)hast_activemap_flush(res);
			} else {
				mtx_unlock(&res->hr_amp_lock);
			}
			if (ISMEMSYNC(hio)) {
				hio->hio_memsyncacked = false;
				refcnt_init(&hio->hio_writecount, ncomps);
			}
			break;
		case BIO_DELETE:
			res->hr_stat_delete++;
			break;
		case BIO_FLUSH:
			res->hr_stat_flush++;
			break;
		}
		pjdlog_debug(2,
		    "ggate_recv: (%p) Moving request to the send queues.", hio);
		refcnt_init(&hio->hio_countdown, ncomps);
		for (ii = ncomp; ii < ncomps; ii++)
			QUEUE_INSERT1(hio, send, ii);
	}
	/* NOTREACHED */
	return (NULL);
}

/*
 * Thread reads from or writes to local component.
 * If local read fails, it redirects it to remote_send thread.
 */
static void *
local_send_thread(void *arg)
{
	struct hast_resource *res = arg;
	struct g_gate_ctl_io *ggio;
	struct hio *hio;
	unsigned int ncomp, rncomp;
	ssize_t ret;

	/* Local component is 0 for now. */
	ncomp = 0;
	/* Remote component is 1 for now. */
	rncomp = 1;

	for (;;) {
		pjdlog_debug(2, "local_send: Taking request.");
		QUEUE_TAKE1(hio, send, ncomp, 0);
		pjdlog_debug(2, "local_send: (%p) Got request.", hio);
		ggio = &hio->hio_ggio;
		switch (ggio->gctl_cmd) {
		case BIO_READ:
			ret = pread(res->hr_localfd, ggio->gctl_data,
			    ggio->gctl_length,
			    ggio->gctl_offset + res->hr_localoff);
			if (ret == ggio->gctl_length)
				hio->hio_errors[ncomp] = 0;
			else if (!ISSYNCREQ(hio)) {
				/*
				 * If READ failed, try to read from remote node.
				 */
				if (ret == -1) {
					reqlog(LOG_WARNING, 0, ggio,
					    "Local request failed (%s), trying remote node. ",
					    strerror(errno));
				} else if (ret != ggio->gctl_length) {
					reqlog(LOG_WARNING, 0, ggio,
					    "Local request failed (%zd != %jd), trying remote node. ",
					    ret, (intmax_t)ggio->gctl_length);
				}
				QUEUE_INSERT1(hio, send, rncomp);
				continue;
			}
			break;
		case BIO_WRITE:
			ret = pwrite(res->hr_localfd, ggio->gctl_data,
			    ggio->gctl_length,
			    ggio->gctl_offset + res->hr_localoff);
			if (ret == -1) {
				hio->hio_errors[ncomp] = errno;
				reqlog(LOG_WARNING, 0, ggio,
				    "Local request failed (%s): ",
				    strerror(errno));
			} else if (ret != ggio->gctl_length) {
				hio->hio_errors[ncomp] = EIO;
				reqlog(LOG_WARNING, 0, ggio,
				    "Local request failed (%zd != %jd): ",
				    ret, (intmax_t)ggio->gctl_length);
			} else {
				hio->hio_errors[ncomp] = 0;
				if (ISASYNC(hio)) {
					ggio->gctl_error = 0;
					write_complete(res, hio);
				}
			}
			break;
		case BIO_DELETE:
			ret = g_delete(res->hr_localfd,
			    ggio->gctl_offset + res->hr_localoff,
			    ggio->gctl_length);
			if (ret == -1) {
				hio->hio_errors[ncomp] = errno;
				reqlog(LOG_WARNING, 0, ggio,
				    "Local request failed (%s): ",
				    strerror(errno));
			} else {
				hio->hio_errors[ncomp] = 0;
			}
			break;
		case BIO_FLUSH:
			if (!res->hr_localflush) {
				ret = -1;
				errno = EOPNOTSUPP;
				break;
			}
			ret = g_flush(res->hr_localfd);
			if (ret == -1) {
				if (errno == EOPNOTSUPP)
					res->hr_localflush = false;
				hio->hio_errors[ncomp] = errno;
				reqlog(LOG_WARNING, 0, ggio,
				    "Local request failed (%s): ",
				    strerror(errno));
			} else {
				hio->hio_errors[ncomp] = 0;
			}
			break;
		}
		if (ISMEMSYNCWRITE(hio)) {
			if (refcnt_release(&hio->hio_writecount) == 0) {
				write_complete(res, hio);
			}
		}
		if (refcnt_release(&hio->hio_countdown) > 0)
			continue;
		if (ISSYNCREQ(hio)) {
			mtx_lock(&sync_lock);
			SYNCREQDONE(hio);
			mtx_unlock(&sync_lock);
			cv_signal(&sync_cond);
		} else {
			pjdlog_debug(2,
			    "local_send: (%p) Moving request to the done queue.",
			    hio);
			QUEUE_INSERT2(hio, done);
		}
	}
	/* NOTREACHED */
	return (NULL);
}

static void
keepalive_send(struct hast_resource *res, unsigned int ncomp)
{
	struct nv *nv;

	rw_rlock(&hio_remote_lock[ncomp]);

	if (!ISCONNECTED(res, ncomp)) {
		rw_unlock(&hio_remote_lock[ncomp]);
		return;
	}

	PJDLOG_ASSERT(res->hr_remotein != NULL);
	PJDLOG_ASSERT(res->hr_remoteout != NULL);

	nv = nv_alloc();
	nv_add_uint8(nv, HIO_KEEPALIVE, "cmd");
	if (nv_error(nv) != 0) {
		rw_unlock(&hio_remote_lock[ncomp]);
		nv_free(nv);
		pjdlog_debug(1,
		    "keepalive_send: Unable to prepare header to send.");
		return;
	}
	if (hast_proto_send(res, res->hr_remoteout, nv, NULL, 0) == -1) {
		rw_unlock(&hio_remote_lock[ncomp]);
		pjdlog_common(LOG_DEBUG, 1, errno,
		    "keepalive_send: Unable to send request");
		nv_free(nv);
		remote_close(res, ncomp);
		return;
	}

	rw_unlock(&hio_remote_lock[ncomp]);
	nv_free(nv);
	pjdlog_debug(2, "keepalive_send: Request sent.");
}

/*
 * Thread sends request to secondary node.
 */
static void *
remote_send_thread(void *arg)
{
	struct hast_resource *res = arg;
	struct g_gate_ctl_io *ggio;
	time_t lastcheck, now;
	struct hio *hio;
	struct nv *nv;
	unsigned int ncomp;
	bool wakeup;
	uint64_t offset, length;
	uint8_t cmd;
	void *data;

	/* Remote component is 1 for now. */
	ncomp = 1;
	lastcheck = time(NULL);

	for (;;) {
		pjdlog_debug(2, "remote_send: Taking request.");
		QUEUE_TAKE1(hio, send, ncomp, HAST_KEEPALIVE);
		if (hio == NULL) {
			now = time(NULL);
			if (lastcheck + HAST_KEEPALIVE <= now) {
				keepalive_send(res, ncomp);
				lastcheck = now;
			}
			continue;
		}
		pjdlog_debug(2, "remote_send: (%p) Got request.", hio);
		ggio = &hio->hio_ggio;
		switch (ggio->gctl_cmd) {
		case BIO_READ:
			cmd = HIO_READ;
			data = NULL;
			offset = ggio->gctl_offset;
			length = ggio->gctl_length;
			break;
		case BIO_WRITE:
			cmd = HIO_WRITE;
			data = ggio->gctl_data;
			offset = ggio->gctl_offset;
			length = ggio->gctl_length;
			break;
		case BIO_DELETE:
			cmd = HIO_DELETE;
			data = NULL;
			offset = ggio->gctl_offset;
			length = ggio->gctl_length;
			break;
		case BIO_FLUSH:
			cmd = HIO_FLUSH;
			data = NULL;
			offset = 0;
			length = 0;
			break;
		default:
			PJDLOG_ABORT("invalid condition");
		}
		nv = nv_alloc();
		nv_add_uint8(nv, cmd, "cmd");
		nv_add_uint64(nv, (uint64_t)ggio->gctl_seq, "seq");
		nv_add_uint64(nv, offset, "offset");
		nv_add_uint64(nv, length, "length");
		if (ISMEMSYNCWRITE(hio))
			nv_add_uint8(nv, 1, "memsync");
		if (nv_error(nv) != 0) {
			hio->hio_errors[ncomp] = nv_error(nv);
			pjdlog_debug(2,
			    "remote_send: (%p) Unable to prepare header to send.",
			    hio);
			reqlog(LOG_ERR, 0, ggio,
			    "Unable to prepare header to send (%s): ",
			    strerror(nv_error(nv)));
			/* Move failed request immediately to the done queue. */
			goto done_queue;
		}
		/*
		 * Protect connection from disappearing.
		 */
		rw_rlock(&hio_remote_lock[ncomp]);
		if (!ISCONNECTED(res, ncomp)) {
			rw_unlock(&hio_remote_lock[ncomp]);
			hio->hio_errors[ncomp] = ENOTCONN;
			goto done_queue;
		}
		/*
		 * Move the request to recv queue before sending it, because
		 * in different order we can get reply before we move request
		 * to recv queue.
		 */
		pjdlog_debug(2,
		    "remote_send: (%p) Moving request to the recv queue.",
		    hio);
		mtx_lock(&hio_recv_list_lock[ncomp]);
		wakeup = TAILQ_EMPTY(&hio_recv_list[ncomp]);
		TAILQ_INSERT_TAIL(&hio_recv_list[ncomp], hio, hio_next[ncomp]);
		hio_recv_list_size[ncomp]++;
		mtx_unlock(&hio_recv_list_lock[ncomp]);
		if (hast_proto_send(res, res->hr_remoteout, nv, data,
		    data != NULL ? length : 0) == -1) {
			hio->hio_errors[ncomp] = errno;
			rw_unlock(&hio_remote_lock[ncomp]);
			pjdlog_debug(2,
			    "remote_send: (%p) Unable to send request.", hio);
			reqlog(LOG_ERR, 0, ggio,
			    "Unable to send request (%s): ",
			    strerror(hio->hio_errors[ncomp]));
			remote_close(res, ncomp);
		} else {
			rw_unlock(&hio_remote_lock[ncomp]);
		}
		nv_free(nv);
		if (wakeup)
			cv_signal(&hio_recv_list_cond[ncomp]);
		continue;
done_queue:
		nv_free(nv);
		if (ISSYNCREQ(hio)) {
			if (refcnt_release(&hio->hio_countdown) > 0)
				continue;
			mtx_lock(&sync_lock);
			SYNCREQDONE(hio);
			mtx_unlock(&sync_lock);
			cv_signal(&sync_cond);
			continue;
		}
		if (ggio->gctl_cmd == BIO_WRITE) {
			mtx_lock(&res->hr_amp_lock);
			if (activemap_need_sync(res->hr_amp, ggio->gctl_offset,
			    ggio->gctl_length)) {
				(void)hast_activemap_flush(res);
			} else {
				mtx_unlock(&res->hr_amp_lock);
			}
			if (ISMEMSYNCWRITE(hio)) {
				if (refcnt_release(&hio->hio_writecount) == 0) {
					if (hio->hio_errors[0] == 0)
						write_complete(res, hio);
				}
			}
		}
		if (refcnt_release(&hio->hio_countdown) > 0)
			continue;
		pjdlog_debug(2,
		    "remote_send: (%p) Moving request to the done queue.",
		    hio);
		QUEUE_INSERT2(hio, done);
	}
	/* NOTREACHED */
	return (NULL);
}

/*
 * Thread receives answer from secondary node and passes it to ggate_send
 * thread.
 */
static void *
remote_recv_thread(void *arg)
{
	struct hast_resource *res = arg;
	struct g_gate_ctl_io *ggio;
	struct hio *hio;
	struct nv *nv;
	unsigned int ncomp;
	uint64_t seq;
	bool memsyncack;
	int error;

	/* Remote component is 1 for now. */
	ncomp = 1;

	for (;;) {
		/* Wait until there is anything to receive. */
		mtx_lock(&hio_recv_list_lock[ncomp]);
		while (TAILQ_EMPTY(&hio_recv_list[ncomp])) {
			pjdlog_debug(2, "remote_recv: No requests, waiting.");
			cv_wait(&hio_recv_list_cond[ncomp],
			    &hio_recv_list_lock[ncomp]);
		}
		mtx_unlock(&hio_recv_list_lock[ncomp]);

		memsyncack = false;

		rw_rlock(&hio_remote_lock[ncomp]);
		if (!ISCONNECTED(res, ncomp)) {
			rw_unlock(&hio_remote_lock[ncomp]);
			/*
			 * Connection is dead, so move all pending requests to
			 * the done queue (one-by-one).
			 */
			mtx_lock(&hio_recv_list_lock[ncomp]);
			hio = TAILQ_FIRST(&hio_recv_list[ncomp]);
			PJDLOG_ASSERT(hio != NULL);
			TAILQ_REMOVE(&hio_recv_list[ncomp], hio,
			    hio_next[ncomp]);
			hio_recv_list_size[ncomp]--;
			mtx_unlock(&hio_recv_list_lock[ncomp]);
			hio->hio_errors[ncomp] = ENOTCONN;
			goto done_queue;
		}
		if (hast_proto_recv_hdr(res->hr_remotein, &nv) == -1) {
			pjdlog_errno(LOG_ERR,
			    "Unable to receive reply header");
			rw_unlock(&hio_remote_lock[ncomp]);
			remote_close(res, ncomp);
			continue;
		}
		rw_unlock(&hio_remote_lock[ncomp]);
		seq = nv_get_uint64(nv, "seq");
		if (seq == 0) {
			pjdlog_error("Header contains no 'seq' field.");
			nv_free(nv);
			continue;
		}
		memsyncack = nv_exists(nv, "received");
		mtx_lock(&hio_recv_list_lock[ncomp]);
		TAILQ_FOREACH(hio, &hio_recv_list[ncomp], hio_next[ncomp]) {
			if (hio->hio_ggio.gctl_seq == seq) {
				TAILQ_REMOVE(&hio_recv_list[ncomp], hio,
				    hio_next[ncomp]);
				hio_recv_list_size[ncomp]--;
				break;
			}
		}
		mtx_unlock(&hio_recv_list_lock[ncomp]);
		if (hio == NULL) {
			pjdlog_error("Found no request matching received 'seq' field (%ju).",
			    (uintmax_t)seq);
			nv_free(nv);
			continue;
		}
		ggio = &hio->hio_ggio;
		error = nv_get_int16(nv, "error");
		if (error != 0) {
			/* Request failed on remote side. */
			hio->hio_errors[ncomp] = error;
			reqlog(LOG_WARNING, 0, ggio,
			    "Remote request failed (%s): ", strerror(error));
			nv_free(nv);
			goto done_queue;
		}
		switch (ggio->gctl_cmd) {
		case BIO_READ:
			rw_rlock(&hio_remote_lock[ncomp]);
			if (!ISCONNECTED(res, ncomp)) {
				rw_unlock(&hio_remote_lock[ncomp]);
				nv_free(nv);
				goto done_queue;
			}
			if (hast_proto_recv_data(res, res->hr_remotein, nv,
			    ggio->gctl_data, ggio->gctl_length) == -1) {
				hio->hio_errors[ncomp] = errno;
				pjdlog_errno(LOG_ERR,
				    "Unable to receive reply data");
				rw_unlock(&hio_remote_lock[ncomp]);
				nv_free(nv);
				remote_close(res, ncomp);
				goto done_queue;
			}
			rw_unlock(&hio_remote_lock[ncomp]);
			break;
		case BIO_WRITE:
		case BIO_DELETE:
		case BIO_FLUSH:
			break;
		default:
			PJDLOG_ABORT("invalid condition");
		}
		hio->hio_errors[ncomp] = 0;
		nv_free(nv);
done_queue:
		if (ISMEMSYNCWRITE(hio)) {
			if (!hio->hio_memsyncacked) {
				PJDLOG_ASSERT(memsyncack ||
				    hio->hio_errors[ncomp] != 0);
				/* Remote ack arrived. */
				if (refcnt_release(&hio->hio_writecount) == 0) {
					if (hio->hio_errors[0] == 0)
						write_complete(res, hio);
				}
				hio->hio_memsyncacked = true;
				if (hio->hio_errors[ncomp] == 0) {
					pjdlog_debug(2,
					    "remote_recv: (%p) Moving request "
					    "back to the recv queue.", hio);
					mtx_lock(&hio_recv_list_lock[ncomp]);
					TAILQ_INSERT_TAIL(&hio_recv_list[ncomp],
					    hio, hio_next[ncomp]);
					hio_recv_list_size[ncomp]++;
					mtx_unlock(&hio_recv_list_lock[ncomp]);
					continue;
				}
			} else {
				PJDLOG_ASSERT(!memsyncack);
				/* Remote final reply arrived. */
			}
		}
		if (refcnt_release(&hio->hio_countdown) > 0)
			continue;
		if (ISSYNCREQ(hio)) {
			mtx_lock(&sync_lock);
			SYNCREQDONE(hio);
			mtx_unlock(&sync_lock);
			cv_signal(&sync_cond);
		} else {
			pjdlog_debug(2,
			    "remote_recv: (%p) Moving request to the done queue.",
			    hio);
			QUEUE_INSERT2(hio, done);
		}
	}
	/* NOTREACHED */
	return (NULL);
}

/*
 * Thread sends answer to the kernel.
 */
static void *
ggate_send_thread(void *arg)
{
	struct hast_resource *res = arg;
	struct g_gate_ctl_io *ggio;
	struct hio *hio;
	unsigned int ii, ncomps;

	ncomps = HAST_NCOMPONENTS;

	for (;;) {
		pjdlog_debug(2, "ggate_send: Taking request.");
		QUEUE_TAKE2(hio, done);
		pjdlog_debug(2, "ggate_send: (%p) Got request.", hio);
		ggio = &hio->hio_ggio;
		for (ii = 0; ii < ncomps; ii++) {
			if (hio->hio_errors[ii] == 0) {
				/*
				 * One successful request is enough to declare
				 * success.
				 */
				ggio->gctl_error = 0;
				break;
			}
		}
		if (ii == ncomps) {
			/*
			 * None of the requests were successful.
			 * Use the error from local component except the
			 * case when we did only remote request.
			 */
			if (ggio->gctl_cmd == BIO_READ &&
			    res->hr_syncsrc == HAST_SYNCSRC_SECONDARY)
				ggio->gctl_error = hio->hio_errors[1];
			else
				ggio->gctl_error = hio->hio_errors[0];
		}
		if (ggio->gctl_error == 0 && ggio->gctl_cmd == BIO_WRITE) {
			mtx_lock(&res->hr_amp_lock);
			if (activemap_write_complete(res->hr_amp,
			    ggio->gctl_offset, ggio->gctl_length)) {
				res->hr_stat_activemap_update++;
				(void)hast_activemap_flush(res);
			} else {
				mtx_unlock(&res->hr_amp_lock);
			}
		}
		if (ggio->gctl_cmd == BIO_WRITE) {
			/*
			 * Unlock range we locked.
			 */
			mtx_lock(&range_lock);
			rangelock_del(range_regular, ggio->gctl_offset,
			    ggio->gctl_length);
			if (range_sync_wait)
				cv_signal(&range_sync_cond);
			mtx_unlock(&range_lock);
			if (!hio->hio_done)
				write_complete(res, hio);
		} else {
			if (ioctl(res->hr_ggatefd, G_GATE_CMD_DONE, ggio) == -1) {
				primary_exit(EX_OSERR,
				    "G_GATE_CMD_DONE failed");
			}
		}
		if (hio->hio_errors[0]) {
			switch (ggio->gctl_cmd) {
			case BIO_READ:
				res->hr_stat_read_error++;
				break;
			case BIO_WRITE:
				res->hr_stat_write_error++;
				break;
			case BIO_DELETE:
				res->hr_stat_delete_error++;
				break;
			case BIO_FLUSH:
				res->hr_stat_flush_error++;
				break;
			}
		}
		pjdlog_debug(2,
		    "ggate_send: (%p) Moving request to the free queue.", hio);
		QUEUE_INSERT2(hio, free);
	}
	/* NOTREACHED */
	return (NULL);
}

/*
 * Thread synchronize local and remote components.
 */
static void *
sync_thread(void *arg __unused)
{
	struct hast_resource *res = arg;
	struct hio *hio;
	struct g_gate_ctl_io *ggio;
	struct timeval tstart, tend, tdiff;
	unsigned int ii, ncomp, ncomps;
	off_t offset, length, synced;
	bool dorewind, directreads;
	int syncext;

	ncomps = HAST_NCOMPONENTS;
	dorewind = true;
	synced = 0;
	offset = -1;
	directreads = false;

	for (;;) {
		mtx_lock(&sync_lock);
		if (offset >= 0 && !sync_inprogress) {
			gettimeofday(&tend, NULL);
			timersub(&tend, &tstart, &tdiff);
			pjdlog_info("Synchronization interrupted after %#.0T. "
			    "%NB synchronized so far.", &tdiff,
			    (intmax_t)synced);
			event_send(res, EVENT_SYNCINTR);
		}
		while (!sync_inprogress) {
			dorewind = true;
			synced = 0;
			cv_wait(&sync_cond, &sync_lock);
		}
		mtx_unlock(&sync_lock);
		/*
		 * Obtain offset at which we should synchronize.
		 * Rewind synchronization if needed.
		 */
		mtx_lock(&res->hr_amp_lock);
		if (dorewind)
			activemap_sync_rewind(res->hr_amp);
		offset = activemap_sync_offset(res->hr_amp, &length, &syncext);
		if (syncext != -1) {
			/*
			 * We synchronized entire syncext extent, we can mark
			 * it as clean now.
			 */
			if (activemap_extent_complete(res->hr_amp, syncext))
				(void)hast_activemap_flush(res);
			else
				mtx_unlock(&res->hr_amp_lock);
		} else {
			mtx_unlock(&res->hr_amp_lock);
		}
		if (dorewind) {
			dorewind = false;
			if (offset == -1)
				pjdlog_info("Nodes are in sync.");
			else {
				pjdlog_info("Synchronization started. %NB to go.",
				    (intmax_t)(res->hr_extentsize *
				    activemap_ndirty(res->hr_amp)));
				event_send(res, EVENT_SYNCSTART);
				gettimeofday(&tstart, NULL);
			}
		}
		if (offset == -1) {
			sync_stop();
			pjdlog_debug(1, "Nothing to synchronize.");
			/*
			 * Synchronization complete, make both localcnt and
			 * remotecnt equal.
			 */
			ncomp = 1;
			rw_rlock(&hio_remote_lock[ncomp]);
			if (ISCONNECTED(res, ncomp)) {
				if (synced > 0) {
					int64_t bps;

					gettimeofday(&tend, NULL);
					timersub(&tend, &tstart, &tdiff);
					bps = (int64_t)((double)synced /
					    ((double)tdiff.tv_sec +
					    (double)tdiff.tv_usec / 1000000));
					pjdlog_info("Synchronization complete. "
					    "%NB synchronized in %#.0lT (%NB/sec).",
					    (intmax_t)synced, &tdiff,
					    (intmax_t)bps);
					event_send(res, EVENT_SYNCDONE);
				}
				mtx_lock(&metadata_lock);
				if (res->hr_syncsrc == HAST_SYNCSRC_SECONDARY)
					directreads = true;
				res->hr_syncsrc = HAST_SYNCSRC_UNDEF;
				res->hr_primary_localcnt =
				    res->hr_secondary_remotecnt;
				res->hr_primary_remotecnt =
				    res->hr_secondary_localcnt;
				pjdlog_debug(1,
				    "Setting localcnt to %ju and remotecnt to %ju.",
				    (uintmax_t)res->hr_primary_localcnt,
				    (uintmax_t)res->hr_primary_remotecnt);
				(void)metadata_write(res);
				mtx_unlock(&metadata_lock);
			}
			rw_unlock(&hio_remote_lock[ncomp]);
			if (directreads) {
				directreads = false;
				enable_direct_reads(res);
			}
			continue;
		}
		pjdlog_debug(2, "sync: Taking free request.");
		QUEUE_TAKE2(hio, free);
		pjdlog_debug(2, "sync: (%p) Got free request.", hio);
		/*
		 * Lock the range we are going to synchronize. We don't want
		 * race where someone writes between our read and write.
		 */
		for (;;) {
			mtx_lock(&range_lock);
			if (rangelock_islocked(range_regular, offset, length)) {
				pjdlog_debug(2,
				    "sync: Range offset=%jd length=%jd locked.",
				    (intmax_t)offset, (intmax_t)length);
				range_sync_wait = true;
				cv_wait(&range_sync_cond, &range_lock);
				range_sync_wait = false;
				mtx_unlock(&range_lock);
				continue;
			}
			if (rangelock_add(range_sync, offset, length) == -1) {
				mtx_unlock(&range_lock);
				pjdlog_debug(2,
				    "sync: Range offset=%jd length=%jd is already locked, waiting.",
				    (intmax_t)offset, (intmax_t)length);
				sleep(1);
				continue;
			}
			mtx_unlock(&range_lock);
			break;
		}
		/*
		 * First read the data from synchronization source.
		 */
		SYNCREQ(hio);
		ggio = &hio->hio_ggio;
		ggio->gctl_cmd = BIO_READ;
		ggio->gctl_offset = offset;
		ggio->gctl_length = length;
		ggio->gctl_error = 0;
		hio->hio_done = false;
		hio->hio_replication = res->hr_replication;
		for (ii = 0; ii < ncomps; ii++)
			hio->hio_errors[ii] = EINVAL;
		reqlog(LOG_DEBUG, 2, ggio, "sync: (%p) Sending sync request: ",
		    hio);
		pjdlog_debug(2, "sync: (%p) Moving request to the send queue.",
		    hio);
		mtx_lock(&metadata_lock);
		if (res->hr_syncsrc == HAST_SYNCSRC_PRIMARY) {
			/*
			 * This range is up-to-date on local component,
			 * so handle request locally.
			 */
			 /* Local component is 0 for now. */
			ncomp = 0;
		} else /* if (res->hr_syncsrc == HAST_SYNCSRC_SECONDARY) */ {
			PJDLOG_ASSERT(res->hr_syncsrc == HAST_SYNCSRC_SECONDARY);
			/*
			 * This range is out-of-date on local component,
			 * so send request to the remote node.
			 */
			 /* Remote component is 1 for now. */
			ncomp = 1;
		}
		mtx_unlock(&metadata_lock);
		refcnt_init(&hio->hio_countdown, 1);
		QUEUE_INSERT1(hio, send, ncomp);

		/*
		 * Let's wait for READ to finish.
		 */
		mtx_lock(&sync_lock);
		while (!ISSYNCREQDONE(hio))
			cv_wait(&sync_cond, &sync_lock);
		mtx_unlock(&sync_lock);

		if (hio->hio_errors[ncomp] != 0) {
			pjdlog_error("Unable to read synchronization data: %s.",
			    strerror(hio->hio_errors[ncomp]));
			goto free_queue;
		}

		/*
		 * We read the data from synchronization source, now write it
		 * to synchronization target.
		 */
		SYNCREQ(hio);
		ggio->gctl_cmd = BIO_WRITE;
		for (ii = 0; ii < ncomps; ii++)
			hio->hio_errors[ii] = EINVAL;
		reqlog(LOG_DEBUG, 2, ggio, "sync: (%p) Sending sync request: ",
		    hio);
		pjdlog_debug(2, "sync: (%p) Moving request to the send queue.",
		    hio);
		mtx_lock(&metadata_lock);
		if (res->hr_syncsrc == HAST_SYNCSRC_PRIMARY) {
			/*
			 * This range is up-to-date on local component,
			 * so we update remote component.
			 */
			 /* Remote component is 1 for now. */
			ncomp = 1;
		} else /* if (res->hr_syncsrc == HAST_SYNCSRC_SECONDARY) */ {
			PJDLOG_ASSERT(res->hr_syncsrc == HAST_SYNCSRC_SECONDARY);
			/*
			 * This range is out-of-date on local component,
			 * so we update it.
			 */
			 /* Local component is 0 for now. */
			ncomp = 0;
		}
		mtx_unlock(&metadata_lock);

		pjdlog_debug(2, "sync: (%p) Moving request to the send queue.",
		    hio);
		refcnt_init(&hio->hio_countdown, 1);
		QUEUE_INSERT1(hio, send, ncomp);

		/*
		 * Let's wait for WRITE to finish.
		 */
		mtx_lock(&sync_lock);
		while (!ISSYNCREQDONE(hio))
			cv_wait(&sync_cond, &sync_lock);
		mtx_unlock(&sync_lock);

		if (hio->hio_errors[ncomp] != 0) {
			pjdlog_error("Unable to write synchronization data: %s.",
			    strerror(hio->hio_errors[ncomp]));
			goto free_queue;
		}

		synced += length;
free_queue:
		mtx_lock(&range_lock);
		rangelock_del(range_sync, offset, length);
		if (range_regular_wait)
			cv_signal(&range_regular_cond);
		mtx_unlock(&range_lock);
		pjdlog_debug(2, "sync: (%p) Moving request to the free queue.",
		    hio);
		QUEUE_INSERT2(hio, free);
	}
	/* NOTREACHED */
	return (NULL);
}

void
primary_config_reload(struct hast_resource *res, struct nv *nv)
{
	unsigned int ii, ncomps;
	int modified, vint;
	const char *vstr;

	pjdlog_info("Reloading configuration...");

	PJDLOG_ASSERT(res->hr_role == HAST_ROLE_PRIMARY);
	PJDLOG_ASSERT(gres == res);
	nv_assert(nv, "remoteaddr");
	nv_assert(nv, "sourceaddr");
	nv_assert(nv, "replication");
	nv_assert(nv, "checksum");
	nv_assert(nv, "compression");
	nv_assert(nv, "timeout");
	nv_assert(nv, "exec");
	nv_assert(nv, "metaflush");

	ncomps = HAST_NCOMPONENTS;

#define MODIFIED_REMOTEADDR	0x01
#define MODIFIED_SOURCEADDR	0x02
#define MODIFIED_REPLICATION	0x04
#define MODIFIED_CHECKSUM	0x08
#define MODIFIED_COMPRESSION	0x10
#define MODIFIED_TIMEOUT	0x20
#define MODIFIED_EXEC		0x40
#define MODIFIED_METAFLUSH	0x80
	modified = 0;

	vstr = nv_get_string(nv, "remoteaddr");
	if (strcmp(gres->hr_remoteaddr, vstr) != 0) {
		/*
		 * Don't copy res->hr_remoteaddr to gres just yet.
		 * We want remote_close() to log disconnect from the old
		 * addresses, not from the new ones.
		 */
		modified |= MODIFIED_REMOTEADDR;
	}
	vstr = nv_get_string(nv, "sourceaddr");
	if (strcmp(gres->hr_sourceaddr, vstr) != 0) {
		strlcpy(gres->hr_sourceaddr, vstr, sizeof(gres->hr_sourceaddr));
		modified |= MODIFIED_SOURCEADDR;
	}
	vint = nv_get_int32(nv, "replication");
	if (gres->hr_replication != vint) {
		gres->hr_replication = vint;
		modified |= MODIFIED_REPLICATION;
	}
	vint = nv_get_int32(nv, "checksum");
	if (gres->hr_checksum != vint) {
		gres->hr_checksum = vint;
		modified |= MODIFIED_CHECKSUM;
	}
	vint = nv_get_int32(nv, "compression");
	if (gres->hr_compression != vint) {
		gres->hr_compression = vint;
		modified |= MODIFIED_COMPRESSION;
	}
	vint = nv_get_int32(nv, "timeout");
	if (gres->hr_timeout != vint) {
		gres->hr_timeout = vint;
		modified |= MODIFIED_TIMEOUT;
	}
	vstr = nv_get_string(nv, "exec");
	if (strcmp(gres->hr_exec, vstr) != 0) {
		strlcpy(gres->hr_exec, vstr, sizeof(gres->hr_exec));
		modified |= MODIFIED_EXEC;
	}
	vint = nv_get_int32(nv, "metaflush");
	if (gres->hr_metaflush != vint) {
		gres->hr_metaflush = vint;
		modified |= MODIFIED_METAFLUSH;
	}

	/*
	 * Change timeout for connected sockets.
	 * Don't bother if we need to reconnect.
	 */
	if ((modified & MODIFIED_TIMEOUT) != 0 &&
	    (modified & (MODIFIED_REMOTEADDR | MODIFIED_SOURCEADDR)) == 0) {
		for (ii = 0; ii < ncomps; ii++) {
			if (!ISREMOTE(ii))
				continue;
			rw_rlock(&hio_remote_lock[ii]);
			if (!ISCONNECTED(gres, ii)) {
				rw_unlock(&hio_remote_lock[ii]);
				continue;
			}
			rw_unlock(&hio_remote_lock[ii]);
			if (proto_timeout(gres->hr_remotein,
			    gres->hr_timeout) == -1) {
				pjdlog_errno(LOG_WARNING,
				    "Unable to set connection timeout");
			}
			if (proto_timeout(gres->hr_remoteout,
			    gres->hr_timeout) == -1) {
				pjdlog_errno(LOG_WARNING,
				    "Unable to set connection timeout");
			}
		}
	}
	if ((modified & (MODIFIED_REMOTEADDR | MODIFIED_SOURCEADDR)) != 0) {
		for (ii = 0; ii < ncomps; ii++) {
			if (!ISREMOTE(ii))
				continue;
			remote_close(gres, ii);
		}
		if (modified & MODIFIED_REMOTEADDR) {
			vstr = nv_get_string(nv, "remoteaddr");
			strlcpy(gres->hr_remoteaddr, vstr,
			    sizeof(gres->hr_remoteaddr));
		}
	}
#undef	MODIFIED_REMOTEADDR
#undef	MODIFIED_SOURCEADDR
#undef	MODIFIED_REPLICATION
#undef	MODIFIED_CHECKSUM
#undef	MODIFIED_COMPRESSION
#undef	MODIFIED_TIMEOUT
#undef	MODIFIED_EXEC
#undef	MODIFIED_METAFLUSH

	pjdlog_info("Configuration reloaded successfully.");
}

static void
guard_one(struct hast_resource *res, unsigned int ncomp)
{
	struct proto_conn *in, *out;

	if (!ISREMOTE(ncomp))
		return;

	rw_rlock(&hio_remote_lock[ncomp]);

	if (!real_remote(res)) {
		rw_unlock(&hio_remote_lock[ncomp]);
		return;
	}

	if (ISCONNECTED(res, ncomp)) {
		PJDLOG_ASSERT(res->hr_remotein != NULL);
		PJDLOG_ASSERT(res->hr_remoteout != NULL);
		rw_unlock(&hio_remote_lock[ncomp]);
		pjdlog_debug(2, "remote_guard: Connection to %s is ok.",
		    res->hr_remoteaddr);
		return;
	}

	PJDLOG_ASSERT(res->hr_remotein == NULL);
	PJDLOG_ASSERT(res->hr_remoteout == NULL);
	/*
	 * Upgrade the lock. It doesn't have to be atomic as no other thread
	 * can change connection status from disconnected to connected.
	 */
	rw_unlock(&hio_remote_lock[ncomp]);
	pjdlog_debug(2, "remote_guard: Reconnecting to %s.",
	    res->hr_remoteaddr);
	in = out = NULL;
	if (init_remote(res, &in, &out) == 0) {
		rw_wlock(&hio_remote_lock[ncomp]);
		PJDLOG_ASSERT(res->hr_remotein == NULL);
		PJDLOG_ASSERT(res->hr_remoteout == NULL);
		PJDLOG_ASSERT(in != NULL && out != NULL);
		res->hr_remotein = in;
		res->hr_remoteout = out;
		rw_unlock(&hio_remote_lock[ncomp]);
		pjdlog_info("Successfully reconnected to %s.",
		    res->hr_remoteaddr);
		sync_start();
	} else {
		/* Both connections should be NULL. */
		PJDLOG_ASSERT(res->hr_remotein == NULL);
		PJDLOG_ASSERT(res->hr_remoteout == NULL);
		PJDLOG_ASSERT(in == NULL && out == NULL);
		pjdlog_debug(2, "remote_guard: Reconnect to %s failed.",
		    res->hr_remoteaddr);
	}
}

/*
 * Thread guards remote connections and reconnects when needed, handles
 * signals, etc.
 */
static void *
guard_thread(void *arg)
{
	struct hast_resource *res = arg;
	unsigned int ii, ncomps;
	struct timespec timeout;
	time_t lastcheck, now;
	sigset_t mask;
	int signo;

	ncomps = HAST_NCOMPONENTS;
	lastcheck = time(NULL);

	PJDLOG_VERIFY(sigemptyset(&mask) == 0);
	PJDLOG_VERIFY(sigaddset(&mask, SIGINT) == 0);
	PJDLOG_VERIFY(sigaddset(&mask, SIGTERM) == 0);

	timeout.tv_sec = HAST_KEEPALIVE;
	timeout.tv_nsec = 0;
	signo = -1;

	for (;;) {
		switch (signo) {
		case SIGINT:
		case SIGTERM:
			sigexit_received = true;
			primary_exitx(EX_OK,
			    "Termination signal received, exiting.");
			break;
		default:
			break;
		}

		/*
		 * Don't check connections until we fully started,
		 * as we may still be looping, waiting for remote node
		 * to switch from primary to secondary.
		 */
		if (fullystarted) {
			pjdlog_debug(2, "remote_guard: Checking connections.");
			now = time(NULL);
			if (lastcheck + HAST_KEEPALIVE <= now) {
				for (ii = 0; ii < ncomps; ii++)
					guard_one(res, ii);
				lastcheck = now;
			}
		}
		signo = sigtimedwait(&mask, NULL, &timeout);
	}
	/* NOTREACHED */
	return (NULL);
}
