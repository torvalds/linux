/*	$OpenBSD: report_smtp.c,v 1.12 2021/06/14 17:58:16 eric Exp $	*/

/*
 * Copyright (c) 2018 Gilles Chehade <gilles@poolp.org>
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

#include "smtpd.h"

void
report_smtp_link_connect(const char *direction, uint64_t qid, const char *rdns, int fcrdns,
    const struct sockaddr_storage *ss_src,
    const struct sockaddr_storage *ss_dest)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);

	m_create(p_lka, IMSG_REPORT_SMTP_LINK_CONNECT, 0, 0, -1);
	m_add_string(p_lka, direction);
	m_add_timeval(p_lka, &tv);
	m_add_id(p_lka, qid);
	m_add_string(p_lka, rdns);
	m_add_int(p_lka, fcrdns);
	m_add_sockaddr(p_lka, (const struct sockaddr *)ss_src);
	m_add_sockaddr(p_lka, (const struct sockaddr *)ss_dest);
	m_close(p_lka);
}

void
report_smtp_link_greeting(const char *direction, uint64_t qid,
    const char *domain)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);

	m_create(p_lka, IMSG_REPORT_SMTP_LINK_GREETING, 0, 0, -1);
	m_add_string(p_lka, direction);
	m_add_timeval(p_lka, &tv);
	m_add_id(p_lka, qid);
	m_add_string(p_lka, domain);
	m_close(p_lka);
}

void
report_smtp_link_identify(const char *direction, uint64_t qid, const char *method, const char *identity)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);

	m_create(p_lka, IMSG_REPORT_SMTP_LINK_IDENTIFY, 0, 0, -1);
	m_add_string(p_lka, direction);
	m_add_timeval(p_lka, &tv);
	m_add_id(p_lka, qid);
	m_add_string(p_lka, method);
	m_add_string(p_lka, identity);
	m_close(p_lka);
}

void
report_smtp_link_tls(const char *direction, uint64_t qid, const char *ssl)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);

	m_create(p_lka, IMSG_REPORT_SMTP_LINK_TLS, 0, 0, -1);
	m_add_string(p_lka, direction);
	m_add_timeval(p_lka, &tv);
	m_add_id(p_lka, qid);
	m_add_string(p_lka, ssl);
	m_close(p_lka);
}

void
report_smtp_link_disconnect(const char *direction, uint64_t qid)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);

	m_create(p_lka, IMSG_REPORT_SMTP_LINK_DISCONNECT, 0, 0, -1);
	m_add_string(p_lka, direction);
	m_add_timeval(p_lka, &tv);
	m_add_id(p_lka, qid);
	m_close(p_lka);
}

void
report_smtp_link_auth(const char *direction, uint64_t qid, const char *user, const char *result)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);

	m_create(p_lka, IMSG_REPORT_SMTP_LINK_AUTH, 0, 0, -1);
	m_add_string(p_lka, direction);
	m_add_timeval(p_lka, &tv);
	m_add_id(p_lka, qid);
	m_add_string(p_lka, user);
	m_add_string(p_lka, result);
	m_close(p_lka);
}

void
report_smtp_tx_reset(const char *direction, uint64_t qid, uint32_t msgid)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);

	m_create(p_lka, IMSG_REPORT_SMTP_TX_RESET, 0, 0, -1);
	m_add_string(p_lka, direction);
	m_add_timeval(p_lka, &tv);
	m_add_id(p_lka, qid);
	m_add_u32(p_lka, msgid);
	m_close(p_lka);
}

void
report_smtp_tx_begin(const char *direction, uint64_t qid, uint32_t msgid)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);

	m_create(p_lka, IMSG_REPORT_SMTP_TX_BEGIN, 0, 0, -1);
	m_add_string(p_lka, direction);
	m_add_timeval(p_lka, &tv);
	m_add_id(p_lka, qid);
	m_add_u32(p_lka, msgid);
	m_close(p_lka);
}

void
report_smtp_tx_mail(const char *direction, uint64_t qid, uint32_t msgid, const char *address, int ok)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);

	m_create(p_lka, IMSG_REPORT_SMTP_TX_MAIL, 0, 0, -1);
	m_add_string(p_lka, direction);
	m_add_timeval(p_lka, &tv);
	m_add_id(p_lka, qid);
	m_add_u32(p_lka, msgid);
	m_add_string(p_lka, address);
	m_add_int(p_lka, ok);
	m_close(p_lka);
}

void
report_smtp_tx_rcpt(const char *direction, uint64_t qid, uint32_t msgid, const char *address, int ok)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);

	m_create(p_lka, IMSG_REPORT_SMTP_TX_RCPT, 0, 0, -1);
	m_add_string(p_lka, direction);
	m_add_timeval(p_lka, &tv);
	m_add_id(p_lka, qid);
	m_add_u32(p_lka, msgid);
	m_add_string(p_lka, address);
	m_add_int(p_lka, ok);
	m_close(p_lka);
}

void
report_smtp_tx_envelope(const char *direction, uint64_t qid, uint32_t msgid, uint64_t evpid)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);

	m_create(p_lka, IMSG_REPORT_SMTP_TX_ENVELOPE, 0, 0, -1);
	m_add_string(p_lka, direction);
	m_add_timeval(p_lka, &tv);
	m_add_id(p_lka, qid);
	m_add_u32(p_lka, msgid);
	m_add_id(p_lka, evpid);
	m_close(p_lka);
}

void
report_smtp_tx_data(const char *direction, uint64_t qid, uint32_t msgid, int ok)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);

	m_create(p_lka, IMSG_REPORT_SMTP_TX_DATA, 0, 0, -1);
	m_add_string(p_lka, direction);
	m_add_timeval(p_lka, &tv);
	m_add_id(p_lka, qid);
	m_add_u32(p_lka, msgid);
	m_add_int(p_lka, ok);
	m_close(p_lka);
}

void
report_smtp_tx_commit(const char *direction, uint64_t qid, uint32_t msgid, size_t msgsz)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);

	m_create(p_lka, IMSG_REPORT_SMTP_TX_COMMIT, 0, 0, -1);
	m_add_string(p_lka, direction);
	m_add_timeval(p_lka, &tv);
	m_add_id(p_lka, qid);
	m_add_u32(p_lka, msgid);
	m_add_size(p_lka, msgsz);
	m_close(p_lka);
}

void
report_smtp_tx_rollback(const char *direction, uint64_t qid, uint32_t msgid)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);

	m_create(p_lka, IMSG_REPORT_SMTP_TX_ROLLBACK, 0, 0, -1);
	m_add_string(p_lka, direction);
	m_add_timeval(p_lka, &tv);
	m_add_id(p_lka, qid);
	m_add_u32(p_lka, msgid);
	m_close(p_lka);
}

void
report_smtp_protocol_client(const char *direction, uint64_t qid, const char *command)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);

	m_create(p_lka, IMSG_REPORT_SMTP_PROTOCOL_CLIENT, 0, 0, -1);
	m_add_string(p_lka, direction);
	m_add_timeval(p_lka, &tv);
	m_add_id(p_lka, qid);
	m_add_string(p_lka, command);
	m_close(p_lka);
}

void
report_smtp_protocol_server(const char *direction, uint64_t qid, const char *response)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);

	m_create(p_lka, IMSG_REPORT_SMTP_PROTOCOL_SERVER, 0, 0, -1);
	m_add_string(p_lka, direction);
	m_add_timeval(p_lka, &tv);
	m_add_id(p_lka, qid);
	m_add_string(p_lka, response);
	m_close(p_lka);
}

void
report_smtp_filter_response(const char *direction, uint64_t qid, int phase, int response, const char *param)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);

	m_create(p_lka, IMSG_REPORT_SMTP_FILTER_RESPONSE, 0, 0, -1);
	m_add_string(p_lka, direction);
	m_add_timeval(p_lka, &tv);
	m_add_id(p_lka, qid);
	m_add_int(p_lka, phase);
	m_add_int(p_lka, response);
	m_add_string(p_lka, param);
	m_close(p_lka);
}

void
report_smtp_timeout(const char *direction, uint64_t qid)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);

	m_create(p_lka, IMSG_REPORT_SMTP_TIMEOUT, 0, 0, -1);
	m_add_string(p_lka, direction);
	m_add_timeval(p_lka, &tv);
	m_add_id(p_lka, qid);
	m_close(p_lka);
}
