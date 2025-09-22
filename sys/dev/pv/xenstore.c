/*	$OpenBSD: xenstore.c,v 1.50 2024/05/24 10:05:55 jsg Exp $	*/

/*
 * Copyright (c) 2015 Mike Belopuhov
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/task.h>

#include <machine/bus.h>

#include <uvm/uvm_extern.h>

#include <dev/pv/pvvar.h>
#include <dev/pv/xenreg.h>
#include <dev/pv/xenvar.h>

/* #define XS_DEBUG */

#ifdef XS_DEBUG
#define DPRINTF(x...)		printf(x)
#else
#define DPRINTF(x...)
#endif

/*
 * The XenStore interface is a simple storage system that is a means of
 * communicating state and configuration data between the Xen Domain 0
 * and the various guest domains.  All configuration data other than
 * a small amount of essential information required during the early
 * boot process of launching a Xen aware guest, is managed using the
 * XenStore.
 *
 * The XenStore is ASCII string based, and has a structure and semantics
 * similar to a filesystem.  There are files and directories that are
 * able to contain files or other directories.  The depth of the hierarchy
 * is only limited by the XenStore's maximum path length.
 *
 * The communication channel between the XenStore service and other
 * domains is via two, guest specific, ring buffers in a shared memory
 * area.  One ring buffer is used for communicating in each direction.
 * The grant table references for this shared memory are given to the
 * guest via HVM hypercalls.
 *
 * The XenStore communication relies on an event channel and thus
 * interrupts. Several Xen services depend on the XenStore, most
 * notably the XenBus used to discover and manage Xen devices.
 */

const struct {
	const char		*xse_errstr;
	int			 xse_errnum;
} xs_errors[] = {
	{ "EINVAL",	EINVAL },
	{ "EACCES",	EACCES },
	{ "EEXIST",	EEXIST },
	{ "EISDIR",	EISDIR },
	{ "ENOENT",	ENOENT },
	{ "ENOMEM",	ENOMEM },
	{ "ENOSPC",	ENOSPC },
	{ "EIO",	EIO },
	{ "ENOTEMPTY",	ENOTEMPTY },
	{ "ENOSYS",	ENOSYS },
	{ "EROFS",	EROFS },
	{ "EBUSY",	EBUSY },
	{ "EAGAIN",	EAGAIN },
	{ "EISCONN",	EISCONN },
	{ NULL,		-1 },
};

struct xs_msghdr {
	/* Message type */
	uint32_t		 xmh_type;
	/* Request identifier, echoed in daemon's response.  */
	uint32_t		 xmh_rid;
	/* Transaction id (0 if not related to a transaction). */
	uint32_t		 xmh_tid;
	/* Length of data following this. */
	uint32_t		 xmh_len;
	/* Generally followed by nul-terminated string(s). */
} __packed;

/*
 * A minimum output buffer size needed to store an error string.
 */
#define XS_ERR_PAYLOAD		16

/*
 * Although the Xen source code implies that the limit is 4k,
 * in practice it turns out that we can only send 2k bytes of
 * payload before receiving a ENOSPC.  We set it to an even
 * smaller value however, because there's no real need to use
 * large buffers for anything.
 */
#define XS_MAX_PAYLOAD		1024

struct xs_msg {
	struct xs_msghdr	 xsm_hdr;
	uint32_t		 xsm_read;
	uint32_t		 xsm_dlen;
	int			 xsm_error;
	uint8_t			*xsm_data;
	TAILQ_ENTRY(xs_msg)	 xsm_link;
};
TAILQ_HEAD(xs_msgq, xs_msg);

#define XS_RING_SIZE		1024

struct xs_ring {
	uint8_t			xsr_req[XS_RING_SIZE];
	uint8_t			xsr_rsp[XS_RING_SIZE];
	uint32_t		xsr_req_cons;
	uint32_t		xsr_req_prod;
	uint32_t		xsr_rsp_cons;
	uint32_t		xsr_rsp_prod;
} __packed;

#define XST_DELAY		1	/* in seconds */

#define XSW_TOKLEN		(sizeof(void *) * 2 + 1)

struct xs_watch {
	TAILQ_ENTRY(xs_watch)	 xsw_entry;
	uint8_t			 xsw_token[XSW_TOKLEN];
	struct task		*xsw_task;
};

/*
 * Container for all XenStore related state.
 */
struct xs_softc {
	struct xen_softc	*xs_sc;

	evtchn_port_t		 xs_port;
	xen_intr_handle_t	 xs_ih;

	struct xs_ring		*xs_ring;

	struct xs_msg		 xs_msgs[10];
	struct xs_msg		*xs_rmsg;

	struct xs_msgq		 xs_free;
	struct xs_msgq		 xs_reqs;
	struct xs_msgq		 xs_rsps;

	volatile uint		 xs_rid;

	const char		*xs_wchan;
	const char		*xs_rchan;

	struct mutex		 xs_reqlck;	/* request queue mutex */
	struct mutex		 xs_rsplck;	/* response queue mutex */
	struct mutex		 xs_frqlck;	/* free queue mutex */

	TAILQ_HEAD(, xs_watch)	 xs_watches;
	struct mutex		 xs_watchlck;
	struct xs_msg		 xs_emsg;
	struct taskq		*xs_watchtq;

	struct rwlock		 xs_rnglck;
};

struct xs_msg *
	xs_get_msg(struct xs_softc *, int);
void	xs_put_msg(struct xs_softc *, struct xs_msg *);
int	xs_ring_get(struct xs_softc *, void *, size_t);
int	xs_ring_put(struct xs_softc *, void *, size_t);
void	xs_intr(void *);
void	xs_poll(struct xs_softc *, int);
int	xs_output(struct xs_transaction *, uint8_t *, int);
int	xs_start(struct xs_transaction *, struct xs_msg *, struct iovec *, int);
struct xs_msg *
	xs_reply(struct xs_transaction *, uint);
int	xs_parse(struct xs_transaction *, struct xs_msg *, struct iovec **,
	    int *);
int	xs_event(struct xs_softc *, struct xs_msg *);

int
xs_attach(struct xen_softc *sc)
{
        struct xen_hvm_param xhv;
	struct xs_softc *xs;
	paddr_t pa;
	int i;

	if ((xs = malloc(sizeof(*xs), M_DEVBUF, M_NOWAIT | M_ZERO)) == NULL) {
		printf(": failed to allocate xenstore softc\n");
		return (-1);
	}
	sc->sc_xs = xs;
	xs->xs_sc = sc;

	/* Fetch event channel port */
	memset(&xhv, 0, sizeof(xhv));
	xhv.domid = DOMID_SELF;
	xhv.index = HVM_PARAM_STORE_EVTCHN;
	if (xen_hypercall(sc, XC_HVM, 2, HVMOP_get_param, &xhv)) {
		printf(": failed to obtain a xenstore event channel\n");
		goto fail_1;
	}
	xs->xs_port = xhv.value;

	printf(", event channel %u\n", xs->xs_port);

	/* Fetch a frame number (PA) of a shared xenstore page */
	memset(&xhv, 0, sizeof(xhv));
	xhv.domid = DOMID_SELF;
	xhv.index = HVM_PARAM_STORE_PFN;
	if (xen_hypercall(sc, XC_HVM, 2, HVMOP_get_param, &xhv))
		goto fail_1;
	pa = ptoa(xhv.value);
	/* Allocate a page of virtual memory */
	xs->xs_ring = km_alloc(PAGE_SIZE, &kv_any, &kp_none, &kd_nowait);
	if (xs->xs_ring == NULL)
		goto fail_1;
	/* Map in the xenstore page into our KVA */
	pa |= PMAP_NOCACHE;
	pmap_kenter_pa((vaddr_t)xs->xs_ring, pa, PROT_READ | PROT_WRITE);
	pmap_update(pmap_kernel());

	if (xen_intr_establish(xs->xs_port, &xs->xs_ih, 0, xs_intr, xs,
	    sc->sc_dev.dv_xname))
		goto fail_2;

	xs->xs_wchan = "xswrite";
	xs->xs_rchan = "xsread";

	TAILQ_INIT(&xs->xs_free);
	TAILQ_INIT(&xs->xs_reqs);
	TAILQ_INIT(&xs->xs_rsps);
	for (i = 0; i < nitems(xs->xs_msgs); i++)
		TAILQ_INSERT_TAIL(&xs->xs_free, &xs->xs_msgs[i], xsm_link);

	mtx_init(&xs->xs_reqlck, IPL_NET);
	mtx_init(&xs->xs_rsplck, IPL_NET);
	mtx_init(&xs->xs_frqlck, IPL_NET);

	rw_init(&xs->xs_rnglck, "xsrnglck");

	xs->xs_watchtq = taskq_create("xenwatch", 1, IPL_NET, 0);

	mtx_init(&xs->xs_watchlck, IPL_NET);
	TAILQ_INIT(&xs->xs_watches);

	xs->xs_emsg.xsm_data = malloc(XS_MAX_PAYLOAD, M_DEVBUF,
	    M_ZERO | M_NOWAIT);
	if (xs->xs_emsg.xsm_data == NULL)
		goto fail_2;
	xs->xs_emsg.xsm_dlen = XS_MAX_PAYLOAD;

	return (0);

 fail_2:
	pmap_kremove((vaddr_t)xs->xs_ring, PAGE_SIZE);
	pmap_update(pmap_kernel());
	km_free(xs->xs_ring, PAGE_SIZE, &kv_any, &kp_none);
	xs->xs_ring = NULL;
 fail_1:
	free(xs, sizeof(*xs), M_DEVBUF);
	sc->sc_xs = NULL;
	return (-1);
}

struct xs_msg *
xs_get_msg(struct xs_softc *xs, int waitok)
{
	static const char *chan = "xsalloc";
	struct xs_msg *xsm;

	mtx_enter(&xs->xs_frqlck);
	for (;;) {
		xsm = TAILQ_FIRST(&xs->xs_free);
		if (xsm != NULL) {
			TAILQ_REMOVE(&xs->xs_free, xsm, xsm_link);
			break;
		}
		if (!waitok) {
			mtx_leave(&xs->xs_frqlck);
			delay(XST_DELAY * 1000 >> 2);
			mtx_enter(&xs->xs_frqlck);
		} else
			msleep_nsec(chan, &xs->xs_frqlck, PRIBIO, chan,
			    SEC_TO_NSEC(XST_DELAY) >> 2);
	}
	mtx_leave(&xs->xs_frqlck);
	return (xsm);
}

void
xs_put_msg(struct xs_softc *xs, struct xs_msg *xsm)
{
	memset(xsm, 0, sizeof(*xsm));
	mtx_enter(&xs->xs_frqlck);
	TAILQ_INSERT_TAIL(&xs->xs_free, xsm, xsm_link);
	mtx_leave(&xs->xs_frqlck);
}

int
xs_geterror(struct xs_msg *xsm)
{
	int i;

	for (i = 0; i < nitems(xs_errors); i++)
		if (strcmp(xs_errors[i].xse_errstr, xsm->xsm_data) == 0)
			return (xs_errors[i].xse_errnum);
	return (EOPNOTSUPP);
}

static inline uint32_t
xs_ring_avail(struct xs_ring *xsr, int req)
{
	uint32_t cons = req ? xsr->xsr_req_cons : xsr->xsr_rsp_cons;
	uint32_t prod = req ? xsr->xsr_req_prod : xsr->xsr_rsp_prod;

	KASSERT(prod - cons <= XS_RING_SIZE);
	return (req ? XS_RING_SIZE - (prod - cons) : prod - cons);
}

void
xs_poll(struct xs_softc *xs, int nosleep)
{
	int s;

	if (nosleep) {
		delay(XST_DELAY * 1000 >> 2);
		s = splnet();
		xs_intr(xs);
		splx(s);
	} else {
		tsleep_nsec(xs->xs_wchan, PRIBIO, xs->xs_wchan,
		    SEC_TO_NSEC(XST_DELAY) >> 2);
	}
}

int
xs_output(struct xs_transaction *xst, uint8_t *bp, int len)
{
	struct xs_softc *xs = xst->xst_cookie;
	int chunk;

	while (len > 0) {
		chunk = xs_ring_put(xs, bp, MIN(len, XS_RING_SIZE));
		if (chunk < 0)
			return (-1);
		if (chunk > 0) {
			len -= chunk;
			bp += chunk;
			if (xs_ring_avail(xs->xs_ring, 1) > 0)
				continue;
		}
		/* Squeaky wheel gets the kick */
		xen_intr_signal(xs->xs_ih);
		/*
		 * chunk == 0: we need to wait for hv to consume
		 * what has already been written;
		 *
		 * Alternatively we have managed to fill the ring
		 * and must wait for HV to collect the data.
		 */
		while (xs->xs_ring->xsr_req_prod != xs->xs_ring->xsr_req_cons)
			xs_poll(xs, 1);
	}
	return (0);
}

int
xs_start(struct xs_transaction *xst, struct xs_msg *xsm, struct iovec *iov,
    int iov_cnt)
{
	struct xs_softc *xs = xst->xst_cookie;
	int i;

	rw_enter_write(&xs->xs_rnglck);

	/* Header */
	if (xs_output(xst, (uint8_t *)&xsm->xsm_hdr,
	    sizeof(xsm->xsm_hdr)) == -1) {
		printf("%s: failed to write the header\n", __func__);
		rw_exit_write(&xs->xs_rnglck);
		return (-1);
	}

	/* Data loop */
	for (i = 0; i < iov_cnt; i++) {
		if (xs_output(xst, iov[i].iov_base, iov[i].iov_len) == -1) {
			printf("%s: failed on iovec #%d len %lu\n", __func__,
			    i, iov[i].iov_len);
			rw_exit_write(&xs->xs_rnglck);
			return (-1);
		}
	}

	mtx_enter(&xs->xs_reqlck);
	TAILQ_INSERT_TAIL(&xs->xs_reqs, xsm, xsm_link);
	mtx_leave(&xs->xs_reqlck);

	xen_intr_signal(xs->xs_ih);

	rw_exit_write(&xs->xs_rnglck);

	return (0);
}

struct xs_msg *
xs_reply(struct xs_transaction *xst, uint rid)
{
	struct xs_softc *xs = xst->xst_cookie;
	struct xs_msg *xsm;
	int s;

	mtx_enter(&xs->xs_rsplck);
	for (;;) {
		TAILQ_FOREACH(xsm, &xs->xs_rsps, xsm_link) {
			if (xsm->xsm_hdr.xmh_tid == xst->xst_id &&
			    xsm->xsm_hdr.xmh_rid == rid)
				break;
		}
		if (xsm != NULL) {
			TAILQ_REMOVE(&xs->xs_rsps, xsm, xsm_link);
			break;
		}
		if (cold) {
			mtx_leave(&xs->xs_rsplck);
			delay(XST_DELAY * 1000 >> 2);
			s = splnet();
			xs_intr(xs);
			splx(s);
			mtx_enter(&xs->xs_rsplck);
		} else
			msleep_nsec(xs->xs_rchan, &xs->xs_rsplck, PRIBIO,
			    xs->xs_rchan, SEC_TO_NSEC(XST_DELAY) >> 2);
	}
	mtx_leave(&xs->xs_rsplck);
	return (xsm);
}

int
xs_ring_put(struct xs_softc *xs, void *src, size_t size)
{
	struct xs_ring *xsr = xs->xs_ring;
	uint32_t prod = xsr->xsr_req_prod & (XS_RING_SIZE - 1);
	uint32_t avail = xs_ring_avail(xsr, 1);
	size_t left;

	if (size > XS_RING_SIZE)
		return (-1);
	if (avail == 0)
		return (0);

	/* Bound the size by the number of available slots */
	size = MIN(size, avail);
	/* How many contiguous bytes can we memcpy... */
	left = XS_RING_SIZE - prod;
	/* ...bounded by how much we need to write? */
	left = MIN(left, size);

	memcpy(&xsr->xsr_req[prod], src, left);
	memcpy(&xsr->xsr_req[0], (caddr_t)src + left, size - left);
	virtio_membar_sync();
	xsr->xsr_req_prod += size;
	return (size);
}

int
xs_ring_get(struct xs_softc *xs, void *dst, size_t size)
{
	struct xs_ring *xsr = xs->xs_ring;
	uint32_t cons = xsr->xsr_rsp_cons & (XS_RING_SIZE - 1);
	uint32_t avail = xs_ring_avail(xsr, 0);
	size_t left;

	if (size > XS_RING_SIZE)
		return (-1);
	if (avail == 0)
		return (0);

	/* Bound the size by the number of available slots */
	size = MIN(size, avail);
	/* How many contiguous bytes can we memcpy... */
	left = XS_RING_SIZE - cons;
	/* ...bounded by how much we need to read? */
	left = MIN(left, size);

	memcpy(dst, &xsr->xsr_rsp[cons], left);
	memcpy((caddr_t)dst + left, &xsr->xsr_rsp[0], size - left);
	virtio_membar_sync();
	xsr->xsr_rsp_cons += size;
	return (size);
}

void
xs_intr(void *arg)
{
	struct xs_softc *xs = arg;
	struct xs_ring *xsr = xs->xs_ring;
	struct xen_softc *sc = xs->xs_sc;
	struct xs_msg *xsm = xs->xs_rmsg;
	struct xs_msghdr xmh;
	uint32_t avail;
	int len;

	virtio_membar_sync();

	if (xsr->xsr_rsp_cons == xsr->xsr_rsp_prod)
		return;

	avail = xs_ring_avail(xsr, 0);

	/* Response processing */

 again:
	if (xs->xs_rmsg == NULL) {
		if (avail < sizeof(xmh)) {
			DPRINTF("%s: incomplete header: %u\n",
			    sc->sc_dev.dv_xname, avail);
			goto out;
		}
		avail -= sizeof(xmh);

		if ((len = xs_ring_get(xs, &xmh, sizeof(xmh))) != sizeof(xmh)) {
			printf("%s: message too short: %d\n",
			    sc->sc_dev.dv_xname, len);
			goto out;
		}

		if (xmh.xmh_type == XS_EVENT) {
			xsm = &xs->xs_emsg;
			xsm->xsm_read = 0;
		} else {
			mtx_enter(&xs->xs_reqlck);
			TAILQ_FOREACH(xsm, &xs->xs_reqs, xsm_link) {
				if (xsm->xsm_hdr.xmh_rid == xmh.xmh_rid) {
					TAILQ_REMOVE(&xs->xs_reqs, xsm,
					    xsm_link);
					break;
				}
			}
			mtx_leave(&xs->xs_reqlck);
			if (xsm == NULL) {
				printf("%s: unexpected message id %u\n",
				    sc->sc_dev.dv_xname, xmh.xmh_rid);
				goto out;
			}
		}
		memcpy(&xsm->xsm_hdr, &xmh, sizeof(xmh));
		xs->xs_rmsg = xsm;
	}

	if (xsm->xsm_hdr.xmh_len > xsm->xsm_dlen)
		xsm->xsm_error = EMSGSIZE;

	len = MIN(xsm->xsm_hdr.xmh_len - xsm->xsm_read, avail);
	if (len) {
		/* Get data if reply is not empty */
		if ((len = xs_ring_get(xs,
		    &xsm->xsm_data[xsm->xsm_read], len)) <= 0) {
			printf("%s: read failure %d\n", sc->sc_dev.dv_xname,
			    len);
			goto out;
		}
		xsm->xsm_read += len;
	}

	/* Notify reader that we've managed to read the whole message */
	if (xsm->xsm_read == xsm->xsm_hdr.xmh_len) {
		xs->xs_rmsg = NULL;
		if (xsm->xsm_hdr.xmh_type == XS_EVENT) {
			xs_event(xs, xsm);
		} else {
			mtx_enter(&xs->xs_rsplck);
			TAILQ_INSERT_TAIL(&xs->xs_rsps, xsm, xsm_link);
			mtx_leave(&xs->xs_rsplck);
			wakeup(xs->xs_rchan);
		}
	}

	if ((avail = xs_ring_avail(xsr, 0)) > 0)
		goto again;

 out:
	/* Wakeup sleeping writes (if any) */
	wakeup(xs->xs_wchan);
	xen_intr_signal(xs->xs_ih);
}

static inline int
xs_get_buf(struct xs_transaction *xst, struct xs_msg *xsm, int len)
{
	unsigned char *buf;

	buf = malloc(len, M_DEVBUF, M_ZERO | (cold ? M_NOWAIT : M_WAITOK));
	if (buf == NULL)
		return (-1);
	xsm->xsm_dlen = len;
	xsm->xsm_data = buf;
	return (0);
}

static inline void
xs_put_buf(struct xs_transaction *xst, struct xs_msg *xsm)
{
	free(xsm->xsm_data, M_DEVBUF, xsm->xsm_dlen);
	xsm->xsm_data = NULL;
}

void
xs_resfree(struct xs_transaction *xst, struct iovec *iov, int iov_cnt)
{
	int i;

	for (i = 0; i < iov_cnt; i++)
		free(iov[i].iov_base, M_DEVBUF, iov[i].iov_len);
	free(iov, M_DEVBUF, sizeof(struct iovec) * iov_cnt);
}

int
xs_parse(struct xs_transaction *xst, struct xs_msg *xsm, struct iovec **iov,
    int *iov_cnt)
{
	char *bp, *cp;
	uint32_t dlen;
	int i, flags;

	/* If the response size is zero, we return an empty string */
	dlen = MAX(xsm->xsm_hdr.xmh_len, 1);
	flags = M_ZERO | (cold ? M_NOWAIT : M_WAITOK);

	*iov_cnt = 0;
	/* Make sure that the data is NUL terminated */
	if (xsm->xsm_data[dlen - 1] != '\0') {
		/*
		 * The XS_READ operation always returns length without
		 * the trailing NUL so we have to adjust the length.
		 */
		dlen = MIN(dlen + 1, xsm->xsm_dlen);
		xsm->xsm_data[dlen - 1] = '\0';
	}
	for (i = 0; i < dlen; i++)
		if (xsm->xsm_data[i] == '\0')
			(*iov_cnt)++;
	*iov = mallocarray(*iov_cnt, sizeof(struct iovec), M_DEVBUF, flags);
	if (*iov == NULL)
		goto cleanup;
	bp = xsm->xsm_data;
	for (i = 0; i < *iov_cnt; i++) {
		cp = bp;
		while (cp - (caddr_t)xsm->xsm_data < dlen && *cp != '\0')
			cp++;
		(*iov)[i].iov_len = cp - bp + 1;
		(*iov)[i].iov_base = malloc((*iov)[i].iov_len, M_DEVBUF, flags);
		if (!(*iov)[i].iov_base) {
			xs_resfree(xst, *iov, *iov_cnt);
			goto cleanup;
		}
		memcpy((*iov)[i].iov_base, bp, (*iov)[i].iov_len);
		bp = ++cp;
	}
	return (0);

 cleanup:
	*iov = NULL;
	*iov_cnt = 0;
	return (ENOMEM);
}

int
xs_event(struct xs_softc *xs, struct xs_msg *xsm)
{
	struct xs_watch *xsw;
	char *token = NULL;
	int i;

	for (i = 0; i < xsm->xsm_read; i++) {
		if (xsm->xsm_data[i] == '\0') {
			token = &xsm->xsm_data[i+1];
			break;
		}
	}
	if (token == NULL) {
		printf("%s: event on \"%s\" without token\n",
		    xs->xs_sc->sc_dev.dv_xname, xsm->xsm_data);
		return (-1);
	}

	mtx_enter(&xs->xs_watchlck);
	TAILQ_FOREACH(xsw, &xs->xs_watches, xsw_entry) {
		if (strcmp(xsw->xsw_token, token))
			continue;
		mtx_leave(&xs->xs_watchlck);
		task_add(xs->xs_watchtq, xsw->xsw_task);
		return (0);
	}
	mtx_leave(&xs->xs_watchlck);

	printf("%s: no watchers for node \"%s\"\n",
	    xs->xs_sc->sc_dev.dv_xname, xsm->xsm_data);
	return (-1);
}

int
xs_cmd(struct xs_transaction *xst, int cmd, const char *path,
    struct iovec **iov, int *iov_cnt)
{
	struct xs_softc *xs = xst->xst_cookie;
	struct xs_msg *xsm;
	struct iovec ov[10];	/* output vector */
	int datalen = XS_ERR_PAYLOAD;
	int ov_cnt = 0;
	enum { READ, WRITE } mode = READ;
	int i, error = 0;

	if (cmd >= XS_MAX)
		return (EINVAL);

	switch (cmd) {
	case XS_TOPEN:
		ov[0].iov_base = "";
		ov[0].iov_len = 1;
		ov_cnt++;
		break;
	case XS_TCLOSE:
	case XS_RM:
	case XS_WATCH:
	case XS_WRITE:
		mode = WRITE;
		/* FALLTHROUGH */
	default:
		if (mode == READ)
			datalen = XS_MAX_PAYLOAD;
		break;
	}

	if (path) {
		ov[ov_cnt].iov_base = (void *)path;
		ov[ov_cnt++].iov_len = strlen(path) + 1; /* +NUL */
	}

	if (mode == WRITE && iov && iov_cnt && *iov_cnt > 0) {
		for (i = 0; i < *iov_cnt && ov_cnt < nitems(ov);
		     i++, ov_cnt++) {
			ov[ov_cnt].iov_base = (*iov)[i].iov_base;
			ov[ov_cnt].iov_len = (*iov)[i].iov_len;
		}
	}

	xsm = xs_get_msg(xs, !cold);

	if (xs_get_buf(xst, xsm, datalen)) {
		xs_put_msg(xs, xsm);
		return (ENOMEM);
	}

	xsm->xsm_hdr.xmh_tid = xst->xst_id;
	xsm->xsm_hdr.xmh_type = cmd;
	xsm->xsm_hdr.xmh_rid = atomic_inc_int_nv(&xs->xs_rid);

	for (i = 0; i < ov_cnt; i++)
		xsm->xsm_hdr.xmh_len += ov[i].iov_len;

	if (xsm->xsm_hdr.xmh_len > XS_MAX_PAYLOAD) {
		printf("%s: message type %d with payload above the limit\n",
		    xs->xs_sc->sc_dev.dv_xname, cmd);
		xs_put_buf(xst, xsm);
		xs_put_msg(xs, xsm);
		return (EIO);
	}

	if (xs_start(xst, xsm, ov, ov_cnt)) {
		printf("%s: message type %d transmission failed\n",
		    xs->xs_sc->sc_dev.dv_xname, cmd);
		xs_put_buf(xst, xsm);
		xs_put_msg(xs, xsm);
		return (EIO);
	}

	xsm = xs_reply(xst, xsm->xsm_hdr.xmh_rid);

	if (xsm->xsm_hdr.xmh_type == XS_ERROR) {
		error = xs_geterror(xsm);
		DPRINTF("%s: xenstore request %d \"%s\" error %s\n",
		    xs->xs_sc->sc_dev.dv_xname, cmd, path, xsm->xsm_data);
	} else if (xsm->xsm_error != 0)
		error = xsm->xsm_error;
	else if (mode == READ) {
		KASSERT(iov && iov_cnt);
		error = xs_parse(xst, xsm, iov, iov_cnt);
	}
#ifdef XS_DEBUG
	else
		if (strcmp(xsm->xsm_data, "OK"))
			printf("%s: xenstore request %d failed: %s\n",
			    xs->xs_sc->sc_dev.dv_xname, cmd, xsm->xsm_data);
#endif

	xs_put_buf(xst, xsm);
	xs_put_msg(xs, xsm);

	return (error);
}

int
xs_watch(void *xsc, const char *path, const char *property, struct task *task,
    void (*cb)(void *), void *arg)
{
	struct xen_softc *sc = xsc;
	struct xs_softc *xs = sc->sc_xs;
	struct xs_transaction xst;
	struct xs_watch *xsw;
	struct iovec iov, *iovp = &iov;
	char key[256];
	int error, iov_cnt, ret;

	memset(&xst, 0, sizeof(xst));
	xst.xst_id = 0;
	xst.xst_cookie = sc->sc_xs;

	xsw = malloc(sizeof(*xsw), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (xsw == NULL)
		return (-1);

	task_set(task, cb, arg);
	xsw->xsw_task = task;

	snprintf(xsw->xsw_token, sizeof(xsw->xsw_token), "%0lx",
	    (unsigned long)xsw);

	if (path)
		ret = snprintf(key, sizeof(key), "%s/%s", path, property);
	else
		ret = snprintf(key, sizeof(key), "%s", property);
	if (ret == -1 || ret >= sizeof(key)) {
		free(xsw, M_DEVBUF, sizeof(*xsw));
		return (EINVAL);
	}

	iov.iov_base = xsw->xsw_token;
	iov.iov_len = sizeof(xsw->xsw_token);
	iov_cnt = 1;

	/*
	 * xs_watches must be prepared pre-emptively because a xenstore
	 * event is raised immediately after a watch is established.
	 */
	mtx_enter(&xs->xs_watchlck);
	TAILQ_INSERT_TAIL(&xs->xs_watches, xsw, xsw_entry);
	mtx_leave(&xs->xs_watchlck);

	if ((error = xs_cmd(&xst, XS_WATCH, key, &iovp, &iov_cnt)) != 0) {
		mtx_enter(&xs->xs_watchlck);
		TAILQ_REMOVE(&xs->xs_watches, xsw, xsw_entry);
		mtx_leave(&xs->xs_watchlck);
		free(xsw, M_DEVBUF, sizeof(*xsw));
		return (error);
	}

	return (0);
}

static unsigned long long
atoull(const char *cp, int *error)
{
	unsigned long long res, cutoff;
	int ch;
	int cutlim;

	res = 0;
	cutoff = ULLONG_MAX / (unsigned long long)10;
	cutlim = ULLONG_MAX % (unsigned long long)10;

	do {
		if (*cp < '0' || *cp > '9') {
			*error = EINVAL;
			return (res);
		}
		ch = *cp - '0';
		if (res > cutoff || (res == cutoff && ch > cutlim)) {
			*error = ERANGE;
			return (res);
		}
		res *= 10;
		res += ch;
	} while (*(++cp) != '\0');

	*error = 0;
	return (res);
}

int
xs_getnum(void *xsc, const char *path, const char *property,
    unsigned long long *val)
{
	char *buf;
	int error = 0;

	buf = malloc(XS_MAX_PAYLOAD, M_DEVBUF, M_ZERO |
	    (cold ? M_NOWAIT : M_WAITOK));
	if (buf == NULL)
		return (ENOMEM);

	error = xs_getprop(xsc, path, property, buf, XS_MAX_PAYLOAD);
	if (error)
		goto out;

	*val = atoull(buf, &error);
	if (error)
		goto out;

 out:
	free(buf, M_DEVBUF, XS_MAX_PAYLOAD);
	return (error);
}

int
xs_setnum(void *xsc, const char *path, const char *property,
    unsigned long long val)
{
	char buf[32];
	int ret;

	ret = snprintf(buf, sizeof(buf), "%llu", val);
	if (ret == -1 || ret >= sizeof(buf))
		return (ERANGE);

	return (xs_setprop(xsc, path, property, buf, strlen(buf)));
}

int
xs_getprop(void *xsc, const char *path, const char *property, char *value,
    int size)
{
	struct xen_softc *sc = xsc;
	struct xs_transaction xst;
	struct iovec *iovp = NULL;
	char key[256];
	int error, ret, iov_cnt = 0;

	if (!property)
		return (EINVAL);

	memset(&xst, 0, sizeof(xst));
	xst.xst_id = 0;
	xst.xst_cookie = sc->sc_xs;

	if (path)
		ret = snprintf(key, sizeof(key), "%s/%s", path, property);
	else
		ret = snprintf(key, sizeof(key), "%s", property);
	if (ret == -1 || ret >= sizeof(key))
		return (EINVAL);

	if ((error = xs_cmd(&xst, XS_READ, key, &iovp, &iov_cnt)) != 0)
		return (error);

	if (iov_cnt > 0)
		strlcpy(value, (char *)iovp->iov_base, size);

	xs_resfree(&xst, iovp, iov_cnt);

	return (0);
}

int
xs_setprop(void *xsc, const char *path, const char *property, char *value,
    int size)
{
	struct xen_softc *sc = xsc;
	struct xs_transaction xst;
	struct iovec iov, *iovp = &iov;
	char key[256];
	int error, ret, iov_cnt = 0;

	if (!property)
		return (EINVAL);

	memset(&xst, 0, sizeof(xst));
	xst.xst_id = 0;
	xst.xst_cookie = sc->sc_xs;

	if (path)
		ret = snprintf(key, sizeof(key), "%s/%s", path, property);
	else
		ret = snprintf(key, sizeof(key), "%s", property);
	if (ret == -1 || ret >= sizeof(key))
		return (EINVAL);

	iov.iov_base = value;
	iov.iov_len = size;
	iov_cnt = 1;

	error = xs_cmd(&xst, XS_WRITE, key, &iovp, &iov_cnt);

	return (error);
}

int
xs_cmpprop(void *xsc, const char *path, const char *property, const char *value,
    int *result)
{
	struct xen_softc *sc = xsc;
	struct xs_transaction xst;
	struct iovec *iovp = NULL;
	char key[256];
	int error, ret, iov_cnt = 0;

	if (!property)
		return (EINVAL);

	memset(&xst, 0, sizeof(xst));
	xst.xst_id = 0;
	xst.xst_cookie = sc->sc_xs;

	if (path)
		ret = snprintf(key, sizeof(key), "%s/%s", path, property);
	else
		ret = snprintf(key, sizeof(key), "%s", property);
	if (ret == -1 || ret >= sizeof(key))
		return (EINVAL);

	if ((error = xs_cmd(&xst, XS_READ, key, &iovp, &iov_cnt)) != 0)
		return (error);

	*result = strcmp(value, (char *)iovp->iov_base);

	xs_resfree(&xst, iovp, iov_cnt);

	return (0);
}

int
xs_await_transition(void *xsc, const char *path, const char *property,
    const char *value, int timo)
{
	struct xen_softc *sc = xsc;
	int error, res;

	do {
		error = xs_cmpprop(xsc, path, property, value, &res);
		if (error)
			return (error);
		if (timo && --timo == 0)
			return (ETIMEDOUT);
		xs_poll(sc->sc_xs, cold);
	} while (res != 0);

	return (0);
}

int
xs_kvop(void *xsc, int op, char *key, char *value, size_t valuelen)
{
	struct xen_softc *sc = xsc;
	struct xs_transaction xst;
	struct iovec iov, *iovp = &iov;
	int error = 0, iov_cnt = 0, cmd, i;

	switch (op) {
	case PVBUS_KVWRITE:
		cmd = XS_WRITE;
		iov.iov_base = value;
		iov.iov_len = strlen(value);
		iov_cnt = 1;
		break;
	case PVBUS_KVREAD:
		cmd = XS_READ;
		break;
	case PVBUS_KVLS:
		cmd = XS_LIST;
		break;
	default:
		return (EOPNOTSUPP);
	}

	memset(&xst, 0, sizeof(xst));
	xst.xst_id = 0;
	xst.xst_cookie = sc->sc_xs;

	if ((error = xs_cmd(&xst, cmd, key, &iovp, &iov_cnt)) != 0)
		return (error);

	memset(value, 0, valuelen);

	switch (cmd) {
	case XS_READ:
		if (iov_cnt == 1 && iovp[0].iov_len == 1) {
			xs_resfree(&xst, iovp, iov_cnt);

			/*
			 * We cannot distinguish if the returned value is
			 * a directory or a file in the xenstore.  The only
			 * indication is that the read value of a directory
			 * returns an empty string (single nul byte),
			 * so try to get the directory list in this case.
			 */
			return (xs_kvop(xsc, PVBUS_KVLS, key, value, valuelen));
		}
		/* FALLTHROUGH */
	case XS_LIST:
		for (i = 0; i < iov_cnt; i++) {
			if (i > 0 && strlcat(value, "\n", valuelen) >=
			    valuelen) {
				error = ERANGE;
				break;
			}
			if (strlcat(value, iovp[i].iov_base,
			    valuelen) >= valuelen) {
				error = ERANGE;
				break;
			}
		}
		xs_resfree(&xst, iovp, iov_cnt);
		break;
	default:
		break;
	}

	return (error);
}
