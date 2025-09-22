/*	$OpenBSD: check_tls.c,v 1.3 2023/07/03 09:38:08 claudio Exp $	*/

/*
 * Copyright (c) 2017 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2007 - 2014 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2006 Pierre-Yves Ritschard <pyr@openbsd.org>
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

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/uio.h>

#include <limits.h>
#include <event.h>
#include <unistd.h>
#include <string.h>
#include <imsg.h>

#include "relayd.h"

void	check_tls_read(int, short, void *);
void	check_tls_write(int, short, void *);
void	check_tls_handshake(int, short, void *);
void	check_tls_cleanup(struct ctl_tcp_event *);
void	check_tls_error(struct ctl_tcp_event *, const char *, const char *);

void
check_tls_read(int s, short event, void *arg)
{
	char			 rbuf[SMALL_READ_BUF_SIZE];
	struct ctl_tcp_event	*cte = arg;
	int			 retry_flag = EV_READ;
	int			 ret;

	if (event == EV_TIMEOUT) {
		cte->host->up = HOST_DOWN;
		check_tls_cleanup(cte);
		hce_notify_done(cte->host, HCE_TLS_READ_TIMEOUT);
		return;
	}

	bzero(rbuf, sizeof(rbuf));

	ret = tls_read(cte->tls, rbuf, sizeof(rbuf));
	if (ret > 0) {
		if (ibuf_add(cte->buf, rbuf, ret) == -1)
			fatal("check_tls_read: buf_add error");
		if (cte->validate_read != NULL &&
		    cte->validate_read(cte) == 0) {
			check_tls_cleanup(cte);
			hce_notify_done(cte->host, cte->host->he);
			return;
		}
	} else if (ret == 0) {
		cte->host->up = HOST_DOWN;
		(void)cte->validate_close(cte);
		check_tls_cleanup(cte);
		hce_notify_done(cte->host, cte->host->he);
		return;
	} else if (ret == TLS_WANT_POLLIN) {
		retry_flag = EV_READ;
	} else if (ret == TLS_WANT_POLLOUT) {
		retry_flag = EV_WRITE;
	} else {
		cte->host->up = HOST_DOWN;
		check_tls_error(cte, cte->host->conf.name, "cannot read");
		check_tls_cleanup(cte);
		hce_notify_done(cte->host, HCE_TLS_READ_ERROR);
		return;
	}

	event_again(&cte->ev, s, EV_TIMEOUT|retry_flag, check_tls_read,
	    &cte->tv_start, &cte->table->conf.timeout, cte);
	return;
}

void
check_tls_write(int s, short event, void *arg)
{
	struct ctl_tcp_event	*cte = arg;
	int			 retry_flag = EV_WRITE;
	int			 len;
	int			 ret;
	void			*buf;

	if (event == EV_TIMEOUT) {
		cte->host->up = HOST_DOWN;
		check_tls_cleanup(cte);
		hce_notify_done(cte->host, HCE_TLS_WRITE_TIMEOUT);
		return;
	}

	if (cte->table->sendbinbuf != NULL) {
		len = ibuf_size(cte->table->sendbinbuf);
		buf = ibuf_data(cte->table->sendbinbuf);
		log_debug("%s: table %s sending binary", __func__,
		    cte->table->conf.name);
		print_hex(buf, 0, len);
	} else {
		len = strlen(cte->table->sendbuf);
		buf = cte->table->sendbuf;
	}

	ret = tls_write(cte->tls, buf, len);

	if (ret > 0) {
		if ((cte->buf = ibuf_dynamic(SMALL_READ_BUF_SIZE, UINT_MAX)) ==
		    NULL)
			fatalx("ssl_write: cannot create dynamic buffer");

		event_again(&cte->ev, s, EV_TIMEOUT|EV_READ, check_tls_read,
		    &cte->tv_start, &cte->table->conf.timeout, cte);
		return;
	} else if (ret == TLS_WANT_POLLIN) {
		retry_flag = EV_READ;
	} else if (ret == TLS_WANT_POLLOUT) {
		retry_flag = EV_WRITE;
	} else {
		cte->host->up = HOST_DOWN;
		check_tls_error(cte, cte->host->conf.name, "cannot write");
		check_tls_cleanup(cte);
		hce_notify_done(cte->host, HCE_TLS_WRITE_ERROR);
		return;
	}

	event_again(&cte->ev, s, EV_TIMEOUT|retry_flag, check_tls_write,
	    &cte->tv_start, &cte->table->conf.timeout, cte);
}

void
check_tls_handshake(int fd, short event, void *arg)
{
	struct ctl_tcp_event	*cte = arg;
	int			 retry_flag = 0;
	int			 ret;

	if (event == EV_TIMEOUT) {
		cte->host->up = HOST_DOWN;
		hce_notify_done(cte->host, HCE_TLS_CONNECT_TIMEOUT);
		check_tls_cleanup(cte);
		return;
	}

	ret = tls_handshake(cte->tls);
	if (ret == 0) {
		if (cte->table->conf.check == CHECK_TCP) {
			cte->host->up = HOST_UP;
			hce_notify_done(cte->host, HCE_TLS_CONNECT_OK);
			check_tls_cleanup(cte);
			return;
		}
		if (cte->table->sendbuf != NULL) {
			event_again(&cte->ev, cte->s, EV_TIMEOUT|EV_WRITE,
			    check_tls_write, &cte->tv_start,
			    &cte->table->conf.timeout, cte);
			return;
		}
		if ((cte->buf = ibuf_dynamic(SMALL_READ_BUF_SIZE, UINT_MAX)) ==
		    NULL)
			fatalx("ssl_connect: cannot create dynamic buffer");
		event_again(&cte->ev, cte->s, EV_TIMEOUT|EV_READ,
		    check_tls_read, &cte->tv_start, &cte->table->conf.timeout,
		    cte);
		return;
	} else if (ret == TLS_WANT_POLLIN) {
		retry_flag = EV_READ;
	} else if (ret == TLS_WANT_POLLOUT) {
		retry_flag = EV_WRITE;
	} else {
		cte->host->up = HOST_DOWN;
		check_tls_error(cte, cte->host->conf.name,
		   "cannot connect");
		hce_notify_done(cte->host, HCE_TLS_CONNECT_FAIL);
		check_tls_cleanup(cte);
		return;
	}

	event_again(&cte->ev, cte->s, EV_TIMEOUT|retry_flag,
	    check_tls_handshake,
	    &cte->tv_start, &cte->table->conf.timeout, cte);
}

void
check_tls_cleanup(struct ctl_tcp_event *cte)
{
	tls_close(cte->tls);
	tls_reset(cte->tls);
	close(cte->s);
	ibuf_free(cte->buf);
	cte->buf = NULL;
}

void
check_tls_error(struct ctl_tcp_event *cte, const char *where, const char *what)
{
	if (log_getverbose() < 2)
		return;
	log_debug("TLS error: %s: %s: %s", where, what, tls_error(cte->tls));
}

void
check_tls(struct ctl_tcp_event *cte)
{
	if (cte->tls == NULL) {
		cte->tls = tls_client();
		if (cte->tls == NULL)
			fatal("cannot create TLS connection");
	}
	/* need to re-configure because of tls_reset */
	if (tls_configure(cte->tls, cte->table->tls_cfg) == -1)
		fatal("cannot configure TLS connection");

	if (tls_connect_socket(cte->tls, cte->s, NULL) == -1) {
		check_tls_error(cte, cte->host->conf.name,
		    "cannot connect");
		tls_close(cte->tls);
		tls_reset(cte->tls);
		cte->host->up = HOST_UNKNOWN;
		hce_notify_done(cte->host, HCE_TLS_CONNECT_ERROR);
		return;
	}

	event_again(&cte->ev, cte->s, EV_TIMEOUT|EV_WRITE, check_tls_handshake,
	    &cte->tv_start, &cte->table->conf.timeout, cte);
}
