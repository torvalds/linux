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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/uio.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "iscsid.h"
#include "iscsi_proto.h"

#ifdef ICL_KERNEL_PROXY
#include <sys/ioctl.h>
#endif

static int
pdu_ahs_length(const struct pdu *pdu)
{

	return (pdu->pdu_bhs->bhs_total_ahs_len * 4);
}

static int
pdu_data_segment_length(const struct pdu *pdu)
{
	uint32_t len = 0;

	len += pdu->pdu_bhs->bhs_data_segment_len[0];
	len <<= 8;
	len += pdu->pdu_bhs->bhs_data_segment_len[1];
	len <<= 8;
	len += pdu->pdu_bhs->bhs_data_segment_len[2];

	return (len);
}

static void
pdu_set_data_segment_length(struct pdu *pdu, uint32_t len)
{

	pdu->pdu_bhs->bhs_data_segment_len[2] = len;
	pdu->pdu_bhs->bhs_data_segment_len[1] = len >> 8;
	pdu->pdu_bhs->bhs_data_segment_len[0] = len >> 16;
}

struct pdu *
pdu_new(struct connection *conn)
{
	struct pdu *pdu;

	pdu = calloc(1, sizeof(*pdu));
	if (pdu == NULL)
		log_err(1, "calloc");

	pdu->pdu_bhs = calloc(1, sizeof(*pdu->pdu_bhs));
	if (pdu->pdu_bhs == NULL)
		log_err(1, "calloc");

	pdu->pdu_connection = conn;

	return (pdu);
}

struct pdu *
pdu_new_response(struct pdu *request)
{

	return (pdu_new(request->pdu_connection));
}

#ifdef ICL_KERNEL_PROXY

static void
pdu_receive_proxy(struct pdu *pdu)
{
	struct connection *conn;
	struct iscsi_daemon_receive *idr;
	size_t len;
	int error;

	conn = pdu->pdu_connection;
	assert(conn->conn_conf.isc_iser != 0);

	pdu->pdu_data = malloc(conn->conn_max_recv_data_segment_length);
	if (pdu->pdu_data == NULL)
		log_err(1, "malloc");

	idr = calloc(1, sizeof(*idr));
	if (idr == NULL)
		log_err(1, "calloc");

	idr->idr_session_id = conn->conn_session_id;
	idr->idr_bhs = pdu->pdu_bhs;
	idr->idr_data_segment_len = conn->conn_max_recv_data_segment_length;
	idr->idr_data_segment = pdu->pdu_data;

	error = ioctl(conn->conn_iscsi_fd, ISCSIDRECEIVE, idr);
	if (error != 0)
		log_err(1, "ISCSIDRECEIVE");

	len = pdu_ahs_length(pdu);
	if (len > 0)
		log_errx(1, "protocol error: non-empty AHS");

	len = pdu_data_segment_length(pdu);
	assert(len <= (size_t)conn->conn_max_recv_data_segment_length);
	pdu->pdu_data_len = len;

	free(idr);
}

static void
pdu_send_proxy(struct pdu *pdu)
{
	struct connection *conn;
	struct iscsi_daemon_send *ids;
	int error;

	conn = pdu->pdu_connection;
	assert(conn->conn_conf.isc_iser != 0);

	pdu_set_data_segment_length(pdu, pdu->pdu_data_len);

	ids = calloc(1, sizeof(*ids));
	if (ids == NULL)
		log_err(1, "calloc");

	ids->ids_session_id = conn->conn_session_id;
	ids->ids_bhs = pdu->pdu_bhs;
	ids->ids_data_segment_len = pdu->pdu_data_len;
	ids->ids_data_segment = pdu->pdu_data;

	error = ioctl(conn->conn_iscsi_fd, ISCSIDSEND, ids);
	if (error != 0)
		log_err(1, "ISCSIDSEND");

	free(ids);
}

#endif /* ICL_KERNEL_PROXY */

static size_t
pdu_padding(const struct pdu *pdu)
{

	if ((pdu->pdu_data_len % 4) != 0)
		return (4 - (pdu->pdu_data_len % 4));

	return (0);
}

static void
pdu_read(const struct connection *conn, char *data, size_t len)
{
	ssize_t ret;

	while (len > 0) {
		ret = read(conn->conn_socket, data, len);
		if (ret < 0) {
			if (timed_out()) {
				fail(conn, "Login Phase timeout");
				log_errx(1, "exiting due to timeout");
			}
			fail(conn, strerror(errno));
			log_err(1, "read");
		} else if (ret == 0) {
			fail(conn, "connection lost");
			log_errx(1, "read: connection lost");
		}
		len -= ret;
		data += ret;
	}
}

void
pdu_receive(struct pdu *pdu)
{
	struct connection *conn;
	size_t len, padding;
	char dummy[4];

	conn = pdu->pdu_connection;
#ifdef ICL_KERNEL_PROXY
	if (conn->conn_conf.isc_iser != 0)
		return (pdu_receive_proxy(pdu));
#endif
	assert(conn->conn_conf.isc_iser == 0);

	pdu_read(conn, (char *)pdu->pdu_bhs, sizeof(*pdu->pdu_bhs));

	len = pdu_ahs_length(pdu);
	if (len > 0)
		log_errx(1, "protocol error: non-empty AHS");

	len = pdu_data_segment_length(pdu);
	if (len > 0) {
		if (len > (size_t)conn->conn_max_recv_data_segment_length) {
			log_errx(1, "protocol error: received PDU "
			    "with DataSegmentLength exceeding %d",
			    conn->conn_max_recv_data_segment_length);
		}

		pdu->pdu_data_len = len;
		pdu->pdu_data = malloc(len);
		if (pdu->pdu_data == NULL)
			log_err(1, "malloc");

		pdu_read(conn, (char *)pdu->pdu_data, pdu->pdu_data_len);

		padding = pdu_padding(pdu);
		if (padding != 0) {
			assert(padding < sizeof(dummy));
			pdu_read(conn, (char *)dummy, padding);
		}
	}
}

void
pdu_send(struct pdu *pdu)
{
	struct connection *conn;
	ssize_t ret, total_len;
	size_t padding;
	uint32_t zero = 0;
	struct iovec iov[3];
	int iovcnt;

	conn = pdu->pdu_connection;
#ifdef ICL_KERNEL_PROXY
	if (conn->conn_conf.isc_iser != 0)
		return (pdu_send_proxy(pdu));
#endif

	assert(conn->conn_conf.isc_iser == 0);

	pdu_set_data_segment_length(pdu, pdu->pdu_data_len);
	iov[0].iov_base = pdu->pdu_bhs;
	iov[0].iov_len = sizeof(*pdu->pdu_bhs);
	total_len = iov[0].iov_len;
	iovcnt = 1;

	if (pdu->pdu_data_len > 0) {
		iov[1].iov_base = pdu->pdu_data;
		iov[1].iov_len = pdu->pdu_data_len;
		total_len += iov[1].iov_len;
		iovcnt = 2;

		padding = pdu_padding(pdu);
		if (padding > 0) {
			assert(padding < sizeof(zero));
			iov[2].iov_base = &zero;
			iov[2].iov_len = padding;
			total_len += iov[2].iov_len;
			iovcnt = 3;
		}
	}

	ret = writev(conn->conn_socket, iov, iovcnt);
	if (ret < 0) {
		if (timed_out())
			log_errx(1, "exiting due to timeout");
		log_err(1, "writev");
	}
	if (ret != total_len)
		log_errx(1, "short write");
}

void
pdu_delete(struct pdu *pdu)
{

	free(pdu->pdu_data);
	free(pdu->pdu_bhs);
	free(pdu);
}
