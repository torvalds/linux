/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
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
 *
 */

/*
 * Software implementation of iSCSI Common Layer kobj(9) interface.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/condvar.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/sx.h>
#include <sys/uio.h>
#include <vm/uma.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <dev/iscsi/icl.h>
#include <dev/iscsi/iscsi_proto.h>
#include <icl_conn_if.h>

static int coalesce = 1;
SYSCTL_INT(_kern_icl, OID_AUTO, coalesce, CTLFLAG_RWTUN,
    &coalesce, 0, "Try to coalesce PDUs before sending");
static int partial_receive_len = 128 * 1024;
SYSCTL_INT(_kern_icl, OID_AUTO, partial_receive_len, CTLFLAG_RWTUN,
    &partial_receive_len, 0, "Minimum read size for partially received "
    "data segment");
static int sendspace = 1048576;
SYSCTL_INT(_kern_icl, OID_AUTO, sendspace, CTLFLAG_RWTUN,
    &sendspace, 0, "Default send socket buffer size");
static int recvspace = 1048576;
SYSCTL_INT(_kern_icl, OID_AUTO, recvspace, CTLFLAG_RWTUN,
    &recvspace, 0, "Default receive socket buffer size");

static MALLOC_DEFINE(M_ICL_SOFT, "icl_soft", "iSCSI software backend");
static uma_zone_t icl_pdu_zone;

static volatile u_int	icl_ncons;

#define ICL_CONN_LOCK(X)		mtx_lock(X->ic_lock)
#define ICL_CONN_UNLOCK(X)		mtx_unlock(X->ic_lock)
#define ICL_CONN_LOCK_ASSERT(X)		mtx_assert(X->ic_lock, MA_OWNED)
#define ICL_CONN_LOCK_ASSERT_NOT(X)	mtx_assert(X->ic_lock, MA_NOTOWNED)

STAILQ_HEAD(icl_pdu_stailq, icl_pdu);

static icl_conn_new_pdu_t	icl_soft_conn_new_pdu;
static icl_conn_pdu_free_t	icl_soft_conn_pdu_free;
static icl_conn_pdu_data_segment_length_t
				    icl_soft_conn_pdu_data_segment_length;
static icl_conn_pdu_append_data_t	icl_soft_conn_pdu_append_data;
static icl_conn_pdu_get_data_t	icl_soft_conn_pdu_get_data;
static icl_conn_pdu_queue_t	icl_soft_conn_pdu_queue;
static icl_conn_handoff_t	icl_soft_conn_handoff;
static icl_conn_free_t		icl_soft_conn_free;
static icl_conn_close_t		icl_soft_conn_close;
static icl_conn_task_setup_t	icl_soft_conn_task_setup;
static icl_conn_task_done_t	icl_soft_conn_task_done;
static icl_conn_transfer_setup_t	icl_soft_conn_transfer_setup;
static icl_conn_transfer_done_t	icl_soft_conn_transfer_done;
#ifdef ICL_KERNEL_PROXY
static icl_conn_connect_t	icl_soft_conn_connect;
#endif

static kobj_method_t icl_soft_methods[] = {
	KOBJMETHOD(icl_conn_new_pdu, icl_soft_conn_new_pdu),
	KOBJMETHOD(icl_conn_pdu_free, icl_soft_conn_pdu_free),
	KOBJMETHOD(icl_conn_pdu_data_segment_length,
	    icl_soft_conn_pdu_data_segment_length),
	KOBJMETHOD(icl_conn_pdu_append_data, icl_soft_conn_pdu_append_data),
	KOBJMETHOD(icl_conn_pdu_get_data, icl_soft_conn_pdu_get_data),
	KOBJMETHOD(icl_conn_pdu_queue, icl_soft_conn_pdu_queue),
	KOBJMETHOD(icl_conn_handoff, icl_soft_conn_handoff),
	KOBJMETHOD(icl_conn_free, icl_soft_conn_free),
	KOBJMETHOD(icl_conn_close, icl_soft_conn_close),
	KOBJMETHOD(icl_conn_task_setup, icl_soft_conn_task_setup),
	KOBJMETHOD(icl_conn_task_done, icl_soft_conn_task_done),
	KOBJMETHOD(icl_conn_transfer_setup, icl_soft_conn_transfer_setup),
	KOBJMETHOD(icl_conn_transfer_done, icl_soft_conn_transfer_done),
#ifdef ICL_KERNEL_PROXY
	KOBJMETHOD(icl_conn_connect, icl_soft_conn_connect),
#endif
	{ 0, 0 }
};

DEFINE_CLASS(icl_soft, icl_soft_methods, sizeof(struct icl_conn));

static void
icl_conn_fail(struct icl_conn *ic)
{
	if (ic->ic_socket == NULL)
		return;

	/*
	 * XXX
	 */
	ic->ic_socket->so_error = EDOOFUS;
	(ic->ic_error)(ic);
}

static struct mbuf *
icl_conn_receive(struct icl_conn *ic, size_t len)
{
	struct uio uio;
	struct socket *so;
	struct mbuf *m;
	int error, flags;

	so = ic->ic_socket;

	memset(&uio, 0, sizeof(uio));
	uio.uio_resid = len;

	flags = MSG_DONTWAIT;
	error = soreceive(so, NULL, &uio, &m, NULL, &flags);
	if (error != 0) {
		ICL_DEBUG("soreceive error %d", error);
		return (NULL);
	}
	if (uio.uio_resid != 0) {
		m_freem(m);
		ICL_DEBUG("short read");
		return (NULL);
	}

	return (m);
}

static int
icl_conn_receive_buf(struct icl_conn *ic, void *buf, size_t len)
{
	struct iovec iov[1];
	struct uio uio;
	struct socket *so;
	int error, flags;

	so = ic->ic_socket;

	memset(&uio, 0, sizeof(uio));
	iov[0].iov_base = buf;
	iov[0].iov_len = len;
	uio.uio_iov = iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = 0;
	uio.uio_resid = len;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_READ;

	flags = MSG_DONTWAIT;
	error = soreceive(so, NULL, &uio, NULL, NULL, &flags);
	if (error != 0) {
		ICL_DEBUG("soreceive error %d", error);
		return (-1);
	}
	if (uio.uio_resid != 0) {
		ICL_DEBUG("short read");
		return (-1);
	}

	return (0);
}

static void
icl_soft_conn_pdu_free(struct icl_conn *ic, struct icl_pdu *ip)
{

	m_freem(ip->ip_bhs_mbuf);
	m_freem(ip->ip_ahs_mbuf);
	m_freem(ip->ip_data_mbuf);
	uma_zfree(icl_pdu_zone, ip);
#ifdef DIAGNOSTIC
	refcount_release(&ic->ic_outstanding_pdus);
#endif
}

/*
 * Allocate icl_pdu with empty BHS to fill up by the caller.
 */
struct icl_pdu *
icl_soft_conn_new_pdu(struct icl_conn *ic, int flags)
{
	struct icl_pdu *ip;

#ifdef DIAGNOSTIC
	refcount_acquire(&ic->ic_outstanding_pdus);
#endif
	ip = uma_zalloc(icl_pdu_zone, flags | M_ZERO);
	if (ip == NULL) {
		ICL_WARN("failed to allocate %zd bytes", sizeof(*ip));
#ifdef DIAGNOSTIC
		refcount_release(&ic->ic_outstanding_pdus);
#endif
		return (NULL);
	}
	ip->ip_conn = ic;

	CTASSERT(sizeof(struct iscsi_bhs) <= MHLEN);
	ip->ip_bhs_mbuf = m_gethdr(flags, MT_DATA);
	if (ip->ip_bhs_mbuf == NULL) {
		ICL_WARN("failed to allocate BHS mbuf");
		icl_soft_conn_pdu_free(ic, ip);
		return (NULL);
	}
	ip->ip_bhs = mtod(ip->ip_bhs_mbuf, struct iscsi_bhs *);
	memset(ip->ip_bhs, 0, sizeof(struct iscsi_bhs));
	ip->ip_bhs_mbuf->m_len = sizeof(struct iscsi_bhs);

	return (ip);
}

static int
icl_pdu_ahs_length(const struct icl_pdu *request)
{

	return (request->ip_bhs->bhs_total_ahs_len * 4);
}

static size_t
icl_pdu_data_segment_length(const struct icl_pdu *request)
{
	uint32_t len = 0;

	len += request->ip_bhs->bhs_data_segment_len[0];
	len <<= 8;
	len += request->ip_bhs->bhs_data_segment_len[1];
	len <<= 8;
	len += request->ip_bhs->bhs_data_segment_len[2];

	return (len);
}

size_t
icl_soft_conn_pdu_data_segment_length(struct icl_conn *ic,
    const struct icl_pdu *request)
{

	return (icl_pdu_data_segment_length(request));
}

static void
icl_pdu_set_data_segment_length(struct icl_pdu *response, uint32_t len)
{

	response->ip_bhs->bhs_data_segment_len[2] = len;
	response->ip_bhs->bhs_data_segment_len[1] = len >> 8;
	response->ip_bhs->bhs_data_segment_len[0] = len >> 16;
}

static size_t
icl_pdu_padding(const struct icl_pdu *ip)
{

	if ((ip->ip_data_len % 4) != 0)
		return (4 - (ip->ip_data_len % 4));

	return (0);
}

static size_t
icl_pdu_size(const struct icl_pdu *response)
{
	size_t len;

	KASSERT(response->ip_ahs_len == 0, ("responding with AHS"));

	len = sizeof(struct iscsi_bhs) + response->ip_data_len +
	    icl_pdu_padding(response);
	if (response->ip_conn->ic_header_crc32c)
		len += ISCSI_HEADER_DIGEST_SIZE;
	if (response->ip_data_len != 0 && response->ip_conn->ic_data_crc32c)
		len += ISCSI_DATA_DIGEST_SIZE;

	return (len);
}

static int
icl_pdu_receive_bhs(struct icl_pdu *request, size_t *availablep)
{

	if (icl_conn_receive_buf(request->ip_conn,
	    request->ip_bhs, sizeof(struct iscsi_bhs))) {
		ICL_DEBUG("failed to receive BHS");
		return (-1);
	}

	*availablep -= sizeof(struct iscsi_bhs);
	return (0);
}

static int
icl_pdu_receive_ahs(struct icl_pdu *request, size_t *availablep)
{

	request->ip_ahs_len = icl_pdu_ahs_length(request);
	if (request->ip_ahs_len == 0)
		return (0);

	request->ip_ahs_mbuf = icl_conn_receive(request->ip_conn,
	    request->ip_ahs_len);
	if (request->ip_ahs_mbuf == NULL) {
		ICL_DEBUG("failed to receive AHS");
		return (-1);
	}

	*availablep -= request->ip_ahs_len;
	return (0);
}

static uint32_t
icl_mbuf_to_crc32c(const struct mbuf *m0)
{
	uint32_t digest = 0xffffffff;
	const struct mbuf *m;

	for (m = m0; m != NULL; m = m->m_next)
		digest = calculate_crc32c(digest,
		    mtod(m, const void *), m->m_len);

	digest = digest ^ 0xffffffff;

	return (digest);
}

static int
icl_pdu_check_header_digest(struct icl_pdu *request, size_t *availablep)
{
	uint32_t received_digest, valid_digest;

	if (request->ip_conn->ic_header_crc32c == false)
		return (0);

	CTASSERT(sizeof(received_digest) == ISCSI_HEADER_DIGEST_SIZE);
	if (icl_conn_receive_buf(request->ip_conn,
	    &received_digest, ISCSI_HEADER_DIGEST_SIZE)) {
		ICL_DEBUG("failed to receive header digest");
		return (-1);
	}
	*availablep -= ISCSI_HEADER_DIGEST_SIZE;

	/* Temporary attach AHS to BHS to calculate header digest. */
	request->ip_bhs_mbuf->m_next = request->ip_ahs_mbuf;
	valid_digest = icl_mbuf_to_crc32c(request->ip_bhs_mbuf);
	request->ip_bhs_mbuf->m_next = NULL;
	if (received_digest != valid_digest) {
		ICL_WARN("header digest check failed; got 0x%x, "
		    "should be 0x%x", received_digest, valid_digest);
		return (-1);
	}

	return (0);
}

/*
 * Return the number of bytes that should be waiting in the receive socket
 * before icl_pdu_receive_data_segment() gets called.
 */
static size_t
icl_pdu_data_segment_receive_len(const struct icl_pdu *request)
{
	size_t len;

	len = icl_pdu_data_segment_length(request);
	if (len == 0)
		return (0);

	/*
	 * Account for the parts of data segment already read from
	 * the socket buffer.
	 */
	KASSERT(len > request->ip_data_len, ("len <= request->ip_data_len"));
	len -= request->ip_data_len;

	/*
	 * Don't always wait for the full data segment to be delivered
	 * to the socket; this might badly affect performance due to
	 * TCP window scaling.
	 */
	if (len > partial_receive_len) {
#if 0
		ICL_DEBUG("need %zd bytes of data, limiting to %zd",
		    len, partial_receive_len));
#endif
		len = partial_receive_len;

		return (len);
	}

	/*
	 * Account for padding.  Note that due to the way code is written,
	 * the icl_pdu_receive_data_segment() must always receive padding
	 * along with the last part of data segment, because it would be
	 * impossible to tell whether we've already received the full data
	 * segment including padding, or without it.
	 */
	if ((len % 4) != 0)
		len += 4 - (len % 4);

#if 0
	ICL_DEBUG("need %zd bytes of data", len));
#endif

	return (len);
}

static int
icl_pdu_receive_data_segment(struct icl_pdu *request,
    size_t *availablep, bool *more_neededp)
{
	struct icl_conn *ic;
	size_t len, padding = 0;
	struct mbuf *m;

	ic = request->ip_conn;

	*more_neededp = false;
	ic->ic_receive_len = 0;

	len = icl_pdu_data_segment_length(request);
	if (len == 0)
		return (0);

	if ((len % 4) != 0)
		padding = 4 - (len % 4);

	/*
	 * Account for already received parts of data segment.
	 */
	KASSERT(len > request->ip_data_len, ("len <= request->ip_data_len"));
	len -= request->ip_data_len;

	if (len + padding > *availablep) {
		/*
		 * Not enough data in the socket buffer.  Receive as much
		 * as we can.  Don't receive padding, since, obviously, it's
		 * not the end of data segment yet.
		 */
#if 0
		ICL_DEBUG("limited from %zd to %zd",
		    len + padding, *availablep - padding));
#endif
		len = *availablep - padding;
		*more_neededp = true;
		padding = 0;
	}

	/*
	 * Must not try to receive padding without at least one byte
	 * of actual data segment.
	 */
	if (len > 0) {
		m = icl_conn_receive(request->ip_conn, len + padding);
		if (m == NULL) {
			ICL_DEBUG("failed to receive data segment");
			return (-1);
		}

		if (request->ip_data_mbuf == NULL)
			request->ip_data_mbuf = m;
		else
			m_cat(request->ip_data_mbuf, m);

		request->ip_data_len += len;
		*availablep -= len + padding;
	} else
		ICL_DEBUG("len 0");

	if (*more_neededp)
		ic->ic_receive_len =
		    icl_pdu_data_segment_receive_len(request);

	return (0);
}

static int
icl_pdu_check_data_digest(struct icl_pdu *request, size_t *availablep)
{
	uint32_t received_digest, valid_digest;

	if (request->ip_conn->ic_data_crc32c == false)
		return (0);

	if (request->ip_data_len == 0)
		return (0);

	CTASSERT(sizeof(received_digest) == ISCSI_DATA_DIGEST_SIZE);
	if (icl_conn_receive_buf(request->ip_conn,
	    &received_digest, ISCSI_DATA_DIGEST_SIZE)) {
		ICL_DEBUG("failed to receive data digest");
		return (-1);
	}
	*availablep -= ISCSI_DATA_DIGEST_SIZE;

	/*
	 * Note that ip_data_mbuf also contains padding; since digest
	 * calculation is supposed to include that, we iterate over
	 * the entire ip_data_mbuf chain, not just ip_data_len bytes of it.
	 */
	valid_digest = icl_mbuf_to_crc32c(request->ip_data_mbuf);
	if (received_digest != valid_digest) {
		ICL_WARN("data digest check failed; got 0x%x, "
		    "should be 0x%x", received_digest, valid_digest);
		return (-1);
	}

	return (0);
}

/*
 * Somewhat contrary to the name, this attempts to receive only one
 * "part" of PDU at a time; call it repeatedly until it returns non-NULL.
 */
static struct icl_pdu *
icl_conn_receive_pdu(struct icl_conn *ic, size_t *availablep)
{
	struct icl_pdu *request;
	struct socket *so;
	size_t len;
	int error;
	bool more_needed;

	so = ic->ic_socket;

	if (ic->ic_receive_state == ICL_CONN_STATE_BHS) {
		KASSERT(ic->ic_receive_pdu == NULL,
		    ("ic->ic_receive_pdu != NULL"));
		request = icl_soft_conn_new_pdu(ic, M_NOWAIT);
		if (request == NULL) {
			ICL_DEBUG("failed to allocate PDU; "
			    "dropping connection");
			icl_conn_fail(ic);
			return (NULL);
		}
		ic->ic_receive_pdu = request;
	} else {
		KASSERT(ic->ic_receive_pdu != NULL,
		    ("ic->ic_receive_pdu == NULL"));
		request = ic->ic_receive_pdu;
	}

	if (*availablep < ic->ic_receive_len) {
#if 0
		ICL_DEBUG("not enough data; need %zd, "
		    "have %zd", ic->ic_receive_len, *availablep);
#endif
		return (NULL);
	}

	switch (ic->ic_receive_state) {
	case ICL_CONN_STATE_BHS:
		//ICL_DEBUG("receiving BHS");
		error = icl_pdu_receive_bhs(request, availablep);
		if (error != 0) {
			ICL_DEBUG("failed to receive BHS; "
			    "dropping connection");
			break;
		}

		/*
		 * We don't enforce any limit for AHS length;
		 * its length is stored in 8 bit field.
		 */

		len = icl_pdu_data_segment_length(request);
		if (len > ic->ic_max_data_segment_length) {
			ICL_WARN("received data segment "
			    "length %zd is larger than negotiated "
			    "MaxDataSegmentLength %zd; "
			    "dropping connection",
			    len, ic->ic_max_data_segment_length);
			error = EINVAL;
			break;
		}

		ic->ic_receive_state = ICL_CONN_STATE_AHS;
		ic->ic_receive_len = icl_pdu_ahs_length(request);
		break;

	case ICL_CONN_STATE_AHS:
		//ICL_DEBUG("receiving AHS");
		error = icl_pdu_receive_ahs(request, availablep);
		if (error != 0) {
			ICL_DEBUG("failed to receive AHS; "
			    "dropping connection");
			break;
		}
		ic->ic_receive_state = ICL_CONN_STATE_HEADER_DIGEST;
		if (ic->ic_header_crc32c == false)
			ic->ic_receive_len = 0;
		else
			ic->ic_receive_len = ISCSI_HEADER_DIGEST_SIZE;
		break;

	case ICL_CONN_STATE_HEADER_DIGEST:
		//ICL_DEBUG("receiving header digest");
		error = icl_pdu_check_header_digest(request, availablep);
		if (error != 0) {
			ICL_DEBUG("header digest failed; "
			    "dropping connection");
			break;
		}

		ic->ic_receive_state = ICL_CONN_STATE_DATA;
		ic->ic_receive_len =
		    icl_pdu_data_segment_receive_len(request);
		break;

	case ICL_CONN_STATE_DATA:
		//ICL_DEBUG("receiving data segment");
		error = icl_pdu_receive_data_segment(request, availablep,
		    &more_needed);
		if (error != 0) {
			ICL_DEBUG("failed to receive data segment;"
			    "dropping connection");
			break;
		}

		if (more_needed)
			break;

		ic->ic_receive_state = ICL_CONN_STATE_DATA_DIGEST;
		if (request->ip_data_len == 0 || ic->ic_data_crc32c == false)
			ic->ic_receive_len = 0;
		else
			ic->ic_receive_len = ISCSI_DATA_DIGEST_SIZE;
		break;

	case ICL_CONN_STATE_DATA_DIGEST:
		//ICL_DEBUG("receiving data digest");
		error = icl_pdu_check_data_digest(request, availablep);
		if (error != 0) {
			ICL_DEBUG("data digest failed; "
			    "dropping connection");
			break;
		}

		/*
		 * We've received complete PDU; reset the receive state machine
		 * and return the PDU.
		 */
		ic->ic_receive_state = ICL_CONN_STATE_BHS;
		ic->ic_receive_len = sizeof(struct iscsi_bhs);
		ic->ic_receive_pdu = NULL;
		return (request);

	default:
		panic("invalid ic_receive_state %d\n", ic->ic_receive_state);
	}

	if (error != 0) {
		/*
		 * Don't free the PDU; it's pointed to by ic->ic_receive_pdu
		 * and will get freed in icl_soft_conn_close().
		 */
		icl_conn_fail(ic);
	}

	return (NULL);
}

static void
icl_conn_receive_pdus(struct icl_conn *ic, size_t available)
{
	struct icl_pdu *response;
	struct socket *so;

	so = ic->ic_socket;

	/*
	 * This can never happen; we're careful to only mess with ic->ic_socket
	 * pointer when the send/receive threads are not running.
	 */
	KASSERT(so != NULL, ("NULL socket"));

	for (;;) {
		if (ic->ic_disconnecting)
			return;

		if (so->so_error != 0) {
			ICL_DEBUG("connection error %d; "
			    "dropping connection", so->so_error);
			icl_conn_fail(ic);
			return;
		}

		/*
		 * Loop until we have a complete PDU or there is not enough
		 * data in the socket buffer.
		 */
		if (available < ic->ic_receive_len) {
#if 0
			ICL_DEBUG("not enough data; have %zd, "
			    "need %zd", available,
			    ic->ic_receive_len);
#endif
			return;
		}

		response = icl_conn_receive_pdu(ic, &available);
		if (response == NULL)
			continue;

		if (response->ip_ahs_len > 0) {
			ICL_WARN("received PDU with unsupported "
			    "AHS; opcode 0x%x; dropping connection",
			    response->ip_bhs->bhs_opcode);
			icl_soft_conn_pdu_free(ic, response);
			icl_conn_fail(ic);
			return;
		}

		(ic->ic_receive)(response);
	}
}

static void
icl_receive_thread(void *arg)
{
	struct icl_conn *ic;
	size_t available;
	struct socket *so;

	ic = arg;
	so = ic->ic_socket;

	for (;;) {
		if (ic->ic_disconnecting) {
			//ICL_DEBUG("terminating");
			break;
		}

		/*
		 * Set the low watermark, to be checked by
		 * soreadable() in icl_soupcall_receive()
		 * to avoid unnecessary wakeups until there
		 * is enough data received to read the PDU.
		 */
		SOCKBUF_LOCK(&so->so_rcv);
		available = sbavail(&so->so_rcv);
		if (available < ic->ic_receive_len) {
			so->so_rcv.sb_lowat = ic->ic_receive_len;
			cv_wait(&ic->ic_receive_cv, &so->so_rcv.sb_mtx);
		} else
			so->so_rcv.sb_lowat = so->so_rcv.sb_hiwat + 1;
		SOCKBUF_UNLOCK(&so->so_rcv);

		icl_conn_receive_pdus(ic, available);
	}

	ICL_CONN_LOCK(ic);
	ic->ic_receive_running = false;
	cv_signal(&ic->ic_send_cv);
	ICL_CONN_UNLOCK(ic);
	kthread_exit();
}

static int
icl_soupcall_receive(struct socket *so, void *arg, int waitflag)
{
	struct icl_conn *ic;

	if (!soreadable(so))
		return (SU_OK);

	ic = arg;
	cv_signal(&ic->ic_receive_cv);
	return (SU_OK);
}

static int
icl_pdu_finalize(struct icl_pdu *request)
{
	size_t padding, pdu_len;
	uint32_t digest, zero = 0;
	int ok;
	struct icl_conn *ic;

	ic = request->ip_conn;

	icl_pdu_set_data_segment_length(request, request->ip_data_len);

	pdu_len = icl_pdu_size(request);

	if (ic->ic_header_crc32c) {
		digest = icl_mbuf_to_crc32c(request->ip_bhs_mbuf);
		ok = m_append(request->ip_bhs_mbuf, sizeof(digest),
		    (void *)&digest);
		if (ok != 1) {
			ICL_WARN("failed to append header digest");
			return (1);
		}
	}

	if (request->ip_data_len != 0) {
		padding = icl_pdu_padding(request);
		if (padding > 0) {
			ok = m_append(request->ip_data_mbuf, padding,
			    (void *)&zero);
			if (ok != 1) {
				ICL_WARN("failed to append padding");
				return (1);
			}
		}

		if (ic->ic_data_crc32c) {
			digest = icl_mbuf_to_crc32c(request->ip_data_mbuf);

			ok = m_append(request->ip_data_mbuf, sizeof(digest),
			    (void *)&digest);
			if (ok != 1) {
				ICL_WARN("failed to append data digest");
				return (1);
			}
		}

		m_cat(request->ip_bhs_mbuf, request->ip_data_mbuf);
		request->ip_data_mbuf = NULL;
	}

	request->ip_bhs_mbuf->m_pkthdr.len = pdu_len;

	return (0);
}

static void
icl_conn_send_pdus(struct icl_conn *ic, struct icl_pdu_stailq *queue)
{
	struct icl_pdu *request, *request2;
	struct socket *so;
	long available, size, size2;
	int coalesced, error;

	ICL_CONN_LOCK_ASSERT_NOT(ic);

	so = ic->ic_socket;

	SOCKBUF_LOCK(&so->so_snd);
	/*
	 * Check how much space do we have for transmit.  We can't just
	 * call sosend() and retry when we get EWOULDBLOCK or EMSGSIZE,
	 * as it always frees the mbuf chain passed to it, even in case
	 * of error.
	 */
	available = sbspace(&so->so_snd);

	/*
	 * Notify the socket upcall that we don't need wakeups
	 * for the time being.
	 */
	so->so_snd.sb_lowat = so->so_snd.sb_hiwat + 1;
	SOCKBUF_UNLOCK(&so->so_snd);

	while (!STAILQ_EMPTY(queue)) {
		request = STAILQ_FIRST(queue);
		size = icl_pdu_size(request);
		if (available < size) {

			/*
			 * Set the low watermark, to be checked by
			 * sowriteable() in icl_soupcall_send()
			 * to avoid unnecessary wakeups until there
			 * is enough space for the PDU to fit.
			 */
			SOCKBUF_LOCK(&so->so_snd);
			available = sbspace(&so->so_snd);
			if (available < size) {
#if 1
				ICL_DEBUG("no space to send; "
				    "have %ld, need %ld",
				    available, size);
#endif
				so->so_snd.sb_lowat = size;
				SOCKBUF_UNLOCK(&so->so_snd);
				return;
			}
			SOCKBUF_UNLOCK(&so->so_snd);
		}
		STAILQ_REMOVE_HEAD(queue, ip_next);
		error = icl_pdu_finalize(request);
		if (error != 0) {
			ICL_DEBUG("failed to finalize PDU; "
			    "dropping connection");
			icl_soft_conn_pdu_free(ic, request);
			icl_conn_fail(ic);
			return;
		}
		if (coalesce) {
			coalesced = 1;
			for (;;) {
				request2 = STAILQ_FIRST(queue);
				if (request2 == NULL)
					break;
				size2 = icl_pdu_size(request2);
				if (available < size + size2)
					break;
				STAILQ_REMOVE_HEAD(queue, ip_next);
				error = icl_pdu_finalize(request2);
				if (error != 0) {
					ICL_DEBUG("failed to finalize PDU; "
					    "dropping connection");
					icl_soft_conn_pdu_free(ic, request);
					icl_soft_conn_pdu_free(ic, request2);
					icl_conn_fail(ic);
					return;
				}
				m_cat(request->ip_bhs_mbuf, request2->ip_bhs_mbuf);
				request2->ip_bhs_mbuf = NULL;
				request->ip_bhs_mbuf->m_pkthdr.len += size2;
				size += size2;
				STAILQ_REMOVE_AFTER(queue, request, ip_next);
				icl_soft_conn_pdu_free(ic, request2);
				coalesced++;
			}
#if 0
			if (coalesced > 1) {
				ICL_DEBUG("coalesced %d PDUs into %ld bytes",
				    coalesced, size);
			}
#endif
		}
		available -= size;
		error = sosend(so, NULL, NULL, request->ip_bhs_mbuf,
		    NULL, MSG_DONTWAIT, curthread);
		request->ip_bhs_mbuf = NULL; /* Sosend consumes the mbuf. */
		if (error != 0) {
			ICL_DEBUG("failed to send PDU, error %d; "
			    "dropping connection", error);
			icl_soft_conn_pdu_free(ic, request);
			icl_conn_fail(ic);
			return;
		}
		icl_soft_conn_pdu_free(ic, request);
	}
}

static void
icl_send_thread(void *arg)
{
	struct icl_conn *ic;
	struct icl_pdu_stailq queue;

	ic = arg;

	STAILQ_INIT(&queue);

	ICL_CONN_LOCK(ic);
	for (;;) {
		for (;;) {
			/*
			 * If the local queue is empty, populate it from
			 * the main one.  This way the icl_conn_send_pdus()
			 * can go through all the queued PDUs without holding
			 * any locks.
			 */
			if (STAILQ_EMPTY(&queue))
				STAILQ_SWAP(&ic->ic_to_send, &queue, icl_pdu);

			ic->ic_check_send_space = false;
			ICL_CONN_UNLOCK(ic);
			icl_conn_send_pdus(ic, &queue);
			ICL_CONN_LOCK(ic);

			/*
			 * The icl_soupcall_send() was called since the last
			 * call to sbspace(); go around;
			 */
			if (ic->ic_check_send_space)
				continue;

			/*
			 * Local queue is empty, but we still have PDUs
			 * in the main one; go around.
			 */
			if (STAILQ_EMPTY(&queue) &&
			    !STAILQ_EMPTY(&ic->ic_to_send))
				continue;

			/*
			 * There might be some stuff in the local queue,
			 * which didn't get sent due to not having enough send
			 * space.  Wait for socket upcall.
			 */
			break;
		}

		if (ic->ic_disconnecting) {
			//ICL_DEBUG("terminating");
			break;
		}

		cv_wait(&ic->ic_send_cv, ic->ic_lock);
	}

	/*
	 * We're exiting; move PDUs back to the main queue, so they can
	 * get freed properly.  At this point ordering doesn't matter.
	 */
	STAILQ_CONCAT(&ic->ic_to_send, &queue);

	ic->ic_send_running = false;
	cv_signal(&ic->ic_send_cv);
	ICL_CONN_UNLOCK(ic);
	kthread_exit();
}

static int
icl_soupcall_send(struct socket *so, void *arg, int waitflag)
{
	struct icl_conn *ic;

	if (!sowriteable(so))
		return (SU_OK);

	ic = arg;

	ICL_CONN_LOCK(ic);
	ic->ic_check_send_space = true;
	ICL_CONN_UNLOCK(ic);

	cv_signal(&ic->ic_send_cv);

	return (SU_OK);
}

static int
icl_soft_conn_pdu_append_data(struct icl_conn *ic, struct icl_pdu *request,
    const void *addr, size_t len, int flags)
{
	struct mbuf *mb, *newmb;
	size_t copylen, off = 0;

	KASSERT(len > 0, ("len == 0"));

	newmb = m_getm2(NULL, len, flags, MT_DATA, 0);
	if (newmb == NULL) {
		ICL_WARN("failed to allocate mbuf for %zd bytes", len);
		return (ENOMEM);
	}

	for (mb = newmb; mb != NULL; mb = mb->m_next) {
		copylen = min(M_TRAILINGSPACE(mb), len - off);
		memcpy(mtod(mb, char *), (const char *)addr + off, copylen);
		mb->m_len = copylen;
		off += copylen;
	}
	KASSERT(off == len, ("%s: off != len", __func__));

	if (request->ip_data_mbuf == NULL) {
		request->ip_data_mbuf = newmb;
		request->ip_data_len = len;
	} else {
		m_cat(request->ip_data_mbuf, newmb);
		request->ip_data_len += len;
	}

	return (0);
}

void
icl_soft_conn_pdu_get_data(struct icl_conn *ic, struct icl_pdu *ip,
    size_t off, void *addr, size_t len)
{

	m_copydata(ip->ip_data_mbuf, off, len, addr);
}

static void
icl_pdu_queue(struct icl_pdu *ip)
{
	struct icl_conn *ic;

	ic = ip->ip_conn;

	ICL_CONN_LOCK_ASSERT(ic);

	if (ic->ic_disconnecting || ic->ic_socket == NULL) {
		ICL_DEBUG("icl_pdu_queue on closed connection");
		icl_soft_conn_pdu_free(ic, ip);
		return;
	}

	if (!STAILQ_EMPTY(&ic->ic_to_send)) {
		STAILQ_INSERT_TAIL(&ic->ic_to_send, ip, ip_next);
		/*
		 * If the queue is not empty, someone else had already
		 * signaled the send thread; no need to do that again,
		 * just return.
		 */
		return;
	}

	STAILQ_INSERT_TAIL(&ic->ic_to_send, ip, ip_next);
	cv_signal(&ic->ic_send_cv);
}

void
icl_soft_conn_pdu_queue(struct icl_conn *ic, struct icl_pdu *ip)
{

	icl_pdu_queue(ip);
}

static struct icl_conn *
icl_soft_new_conn(const char *name, struct mtx *lock)
{
	struct icl_conn *ic;

	refcount_acquire(&icl_ncons);

	ic = (struct icl_conn *)kobj_create(&icl_soft_class, M_ICL_SOFT, M_WAITOK | M_ZERO);

	STAILQ_INIT(&ic->ic_to_send);
	ic->ic_lock = lock;
	cv_init(&ic->ic_send_cv, "icl_tx");
	cv_init(&ic->ic_receive_cv, "icl_rx");
#ifdef DIAGNOSTIC
	refcount_init(&ic->ic_outstanding_pdus, 0);
#endif
	ic->ic_max_data_segment_length = ICL_MAX_DATA_SEGMENT_LENGTH;
	ic->ic_name = name;
	ic->ic_offload = "None";
	ic->ic_unmapped = false;

	return (ic);
}

void
icl_soft_conn_free(struct icl_conn *ic)
{

#ifdef DIAGNOSTIC
	KASSERT(ic->ic_outstanding_pdus == 0,
	    ("destroying session with %d outstanding PDUs",
	     ic->ic_outstanding_pdus));
#endif
	cv_destroy(&ic->ic_send_cv);
	cv_destroy(&ic->ic_receive_cv);
	kobj_delete((struct kobj *)ic, M_ICL_SOFT);
	refcount_release(&icl_ncons);
}

static int
icl_conn_start(struct icl_conn *ic)
{
	size_t minspace;
	struct sockopt opt;
	int error, one = 1;

	ICL_CONN_LOCK(ic);

	/*
	 * XXX: Ugly hack.
	 */
	if (ic->ic_socket == NULL) {
		ICL_CONN_UNLOCK(ic);
		return (EINVAL);
	}

	ic->ic_receive_state = ICL_CONN_STATE_BHS;
	ic->ic_receive_len = sizeof(struct iscsi_bhs);
	ic->ic_disconnecting = false;

	ICL_CONN_UNLOCK(ic);

	/*
	 * For sendspace, this is required because the current code cannot
	 * send a PDU in pieces; thus, the minimum buffer size is equal
	 * to the maximum PDU size.  "+4" is to account for possible padding.
	 *
	 * What we should actually do here is to use autoscaling, but set
	 * some minimal buffer size to "minspace".  I don't know a way to do
	 * that, though.
	 */
	minspace = sizeof(struct iscsi_bhs) + ic->ic_max_data_segment_length +
	    ISCSI_HEADER_DIGEST_SIZE + ISCSI_DATA_DIGEST_SIZE + 4;
	if (sendspace < minspace) {
		ICL_WARN("kern.icl.sendspace too low; must be at least %zd",
		    minspace);
		sendspace = minspace;
	}
	if (recvspace < minspace) {
		ICL_WARN("kern.icl.recvspace too low; must be at least %zd",
		    minspace);
		recvspace = minspace;
	}

	error = soreserve(ic->ic_socket, sendspace, recvspace);
	if (error != 0) {
		ICL_WARN("soreserve failed with error %d", error);
		icl_soft_conn_close(ic);
		return (error);
	}
	ic->ic_socket->so_snd.sb_flags |= SB_AUTOSIZE;
	ic->ic_socket->so_rcv.sb_flags |= SB_AUTOSIZE;

	/*
	 * Disable Nagle.
	 */
	bzero(&opt, sizeof(opt));
	opt.sopt_dir = SOPT_SET;
	opt.sopt_level = IPPROTO_TCP;
	opt.sopt_name = TCP_NODELAY;
	opt.sopt_val = &one;
	opt.sopt_valsize = sizeof(one);
	error = sosetopt(ic->ic_socket, &opt);
	if (error != 0) {
		ICL_WARN("disabling TCP_NODELAY failed with error %d", error);
		icl_soft_conn_close(ic);
		return (error);
	}

	/*
	 * Register socket upcall, to get notified about incoming PDUs
	 * and free space to send outgoing ones.
	 */
	SOCKBUF_LOCK(&ic->ic_socket->so_snd);
	soupcall_set(ic->ic_socket, SO_SND, icl_soupcall_send, ic);
	SOCKBUF_UNLOCK(&ic->ic_socket->so_snd);
	SOCKBUF_LOCK(&ic->ic_socket->so_rcv);
	soupcall_set(ic->ic_socket, SO_RCV, icl_soupcall_receive, ic);
	SOCKBUF_UNLOCK(&ic->ic_socket->so_rcv);

	/*
	 * Start threads.
	 */
	ICL_CONN_LOCK(ic);
	ic->ic_send_running = ic->ic_receive_running = true;
	ICL_CONN_UNLOCK(ic);
	error = kthread_add(icl_send_thread, ic, NULL, NULL, 0, 0, "%stx",
	    ic->ic_name);
	if (error != 0) {
		ICL_WARN("kthread_add(9) failed with error %d", error);
		ICL_CONN_LOCK(ic);
		ic->ic_send_running = ic->ic_receive_running = false;
		cv_signal(&ic->ic_send_cv);
		ICL_CONN_UNLOCK(ic);
		icl_soft_conn_close(ic);
		return (error);
	}
	error = kthread_add(icl_receive_thread, ic, NULL, NULL, 0, 0, "%srx",
	    ic->ic_name);
	if (error != 0) {
		ICL_WARN("kthread_add(9) failed with error %d", error);
		ICL_CONN_LOCK(ic);
		ic->ic_receive_running = false;
		cv_signal(&ic->ic_send_cv);
		ICL_CONN_UNLOCK(ic);
		icl_soft_conn_close(ic);
		return (error);
	}

	return (0);
}

int
icl_soft_conn_handoff(struct icl_conn *ic, int fd)
{
	struct file *fp;
	struct socket *so;
	cap_rights_t rights;
	int error;

	ICL_CONN_LOCK_ASSERT_NOT(ic);

#ifdef ICL_KERNEL_PROXY
	/*
	 * We're transitioning to Full Feature phase, and we don't
	 * really care.
	 */
	if (fd == 0) {
		ICL_CONN_LOCK(ic);
		if (ic->ic_socket == NULL) {
			ICL_CONN_UNLOCK(ic);
			ICL_WARN("proxy handoff without connect"); 
			return (EINVAL);
		}
		ICL_CONN_UNLOCK(ic);
		return (0);
	}
#endif

	/*
	 * Steal the socket from userland.
	 */
	error = fget(curthread, fd,
	    cap_rights_init(&rights, CAP_SOCK_CLIENT), &fp);
	if (error != 0)
		return (error);
	if (fp->f_type != DTYPE_SOCKET) {
		fdrop(fp, curthread);
		return (EINVAL);
	}
	so = fp->f_data;
	if (so->so_type != SOCK_STREAM) {
		fdrop(fp, curthread);
		return (EINVAL);
	}

	ICL_CONN_LOCK(ic);

	if (ic->ic_socket != NULL) {
		ICL_CONN_UNLOCK(ic);
		fdrop(fp, curthread);
		return (EBUSY);
	}

	ic->ic_socket = fp->f_data;
	fp->f_ops = &badfileops;
	fp->f_data = NULL;
	fdrop(fp, curthread);
	ICL_CONN_UNLOCK(ic);

	error = icl_conn_start(ic);

	return (error);
}

void
icl_soft_conn_close(struct icl_conn *ic)
{
	struct icl_pdu *pdu;
	struct socket *so;

	ICL_CONN_LOCK(ic);

	/*
	 * Wake up the threads, so they can properly terminate.
	 */
	ic->ic_disconnecting = true;
	while (ic->ic_receive_running || ic->ic_send_running) {
		cv_signal(&ic->ic_receive_cv);
		cv_signal(&ic->ic_send_cv);
		cv_wait(&ic->ic_send_cv, ic->ic_lock);
	}

	/* Some other thread could close the connection same time. */
	so = ic->ic_socket;
	if (so == NULL) {
		ICL_CONN_UNLOCK(ic);
		return;
	}
	ic->ic_socket = NULL;

	/*
	 * Deregister socket upcalls.
	 */
	ICL_CONN_UNLOCK(ic);
	SOCKBUF_LOCK(&so->so_snd);
	if (so->so_snd.sb_upcall != NULL)
		soupcall_clear(so, SO_SND);
	SOCKBUF_UNLOCK(&so->so_snd);
	SOCKBUF_LOCK(&so->so_rcv);
	if (so->so_rcv.sb_upcall != NULL)
		soupcall_clear(so, SO_RCV);
	SOCKBUF_UNLOCK(&so->so_rcv);
	soclose(so);
	ICL_CONN_LOCK(ic);

	if (ic->ic_receive_pdu != NULL) {
		//ICL_DEBUG("freeing partially received PDU");
		icl_soft_conn_pdu_free(ic, ic->ic_receive_pdu);
		ic->ic_receive_pdu = NULL;
	}

	/*
	 * Remove any outstanding PDUs from the send queue.
	 */
	while (!STAILQ_EMPTY(&ic->ic_to_send)) {
		pdu = STAILQ_FIRST(&ic->ic_to_send);
		STAILQ_REMOVE_HEAD(&ic->ic_to_send, ip_next);
		icl_soft_conn_pdu_free(ic, pdu);
	}

	KASSERT(STAILQ_EMPTY(&ic->ic_to_send),
	    ("destroying session with non-empty send queue"));
	ICL_CONN_UNLOCK(ic);
}

int
icl_soft_conn_task_setup(struct icl_conn *ic, struct icl_pdu *ip,
    struct ccb_scsiio *csio, uint32_t *task_tagp, void **prvp)
{

	return (0);
}

void
icl_soft_conn_task_done(struct icl_conn *ic, void *prv)
{
}

int
icl_soft_conn_transfer_setup(struct icl_conn *ic, union ctl_io *io,
    uint32_t *transfer_tag, void **prvp)
{

	return (0);
}

void
icl_soft_conn_transfer_done(struct icl_conn *ic, void *prv)
{
}

static int
icl_soft_limits(struct icl_drv_limits *idl)
{

	idl->idl_max_recv_data_segment_length = 128 * 1024;
	idl->idl_max_send_data_segment_length = 128 * 1024;
	idl->idl_max_burst_length = 262144;
	idl->idl_first_burst_length = 65536;

	return (0);
}

#ifdef ICL_KERNEL_PROXY
int
icl_soft_conn_connect(struct icl_conn *ic, int domain, int socktype,
    int protocol, struct sockaddr *from_sa, struct sockaddr *to_sa)
{

	return (icl_soft_proxy_connect(ic, domain, socktype, protocol,
	    from_sa, to_sa));
}

int
icl_soft_handoff_sock(struct icl_conn *ic, struct socket *so)
{
	int error;

	ICL_CONN_LOCK_ASSERT_NOT(ic);

	if (so->so_type != SOCK_STREAM)
		return (EINVAL);

	ICL_CONN_LOCK(ic);
	if (ic->ic_socket != NULL) {
		ICL_CONN_UNLOCK(ic);
		return (EBUSY);
	}
	ic->ic_socket = so;
	ICL_CONN_UNLOCK(ic);

	error = icl_conn_start(ic);

	return (error);
}
#endif /* ICL_KERNEL_PROXY */

static int
icl_soft_load(void)
{
	int error;

	icl_pdu_zone = uma_zcreate("icl_pdu",
	    sizeof(struct icl_pdu), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, 0);
	refcount_init(&icl_ncons, 0);

	/*
	 * The reason we call this "none" is that to the user,
	 * it's known as "offload driver"; "offload driver: soft"
	 * doesn't make much sense.
	 */
	error = icl_register("none", false, 0,
	    icl_soft_limits, icl_soft_new_conn);
	KASSERT(error == 0, ("failed to register"));

#if defined(ICL_KERNEL_PROXY) && 0
	/*
	 * Debugging aid for kernel proxy functionality.
	 */
	error = icl_register("proxytest", true, 0,
	    icl_soft_limits, icl_soft_new_conn);
	KASSERT(error == 0, ("failed to register"));
#endif

	return (error);
}

static int
icl_soft_unload(void)
{

	if (icl_ncons != 0)
		return (EBUSY);

	icl_unregister("none", false);
#if defined(ICL_KERNEL_PROXY) && 0
	icl_unregister("proxytest", true);
#endif

	uma_zdestroy(icl_pdu_zone);

	return (0);
}

static int
icl_soft_modevent(module_t mod, int what, void *arg)
{

	switch (what) {
	case MOD_LOAD:
		return (icl_soft_load());
	case MOD_UNLOAD:
		return (icl_soft_unload());
	default:
		return (EINVAL);
	}
}

moduledata_t icl_soft_data = {
	"icl_soft",
	icl_soft_modevent,
	0
};

DECLARE_MODULE(icl_soft, icl_soft_data, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_DEPEND(icl_soft, icl, 1, 1, 1);
MODULE_VERSION(icl_soft, 1);
