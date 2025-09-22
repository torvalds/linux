/*	$OpenBSD: snmpe.c,v 1.95 2024/05/21 05:00:48 jsg Exp $	*/

/*
 * Copyright (c) 2007, 2008, 2012 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2017 Marco Pfatschbacher <mpf@openbsd.org>
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

#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/tree.h>
#include <sys/types.h>

#include <netinet/in.h>

#include <ber.h>
#include <event.h>
#include <errno.h>
#include <fcntl.h>
#include <imsg.h>
#include <locale.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "application.h"
#include "log.h"
#include "snmpd.h"
#include "snmpe.h"
#include "mib.h"

void	 snmpe_init(struct privsep *, struct privsep_proc *, void *);
int	 snmpe_dispatch_parent(int, struct privsep_proc *, struct imsg *);
int	 snmpe_parse(struct snmp_message *);
void	 snmpe_tryparse(int, struct snmp_message *);
int	 snmpe_parsevarbinds(struct snmp_message *);
int	 snmpe_bind(struct address *);
void	 snmpe_recvmsg(int fd, short, void *);
void	 snmpe_readcb(int fd, short, void *);
void	 snmpe_writecb(int fd, short, void *);
void	 snmpe_acceptcb(int fd, short, void *);
void	 snmpe_prepare_read(struct snmp_message *, int);
int	 snmpe_encode(struct snmp_message *);

struct imsgev	*iev_parent;
static const struct timeval	snmpe_tcp_timeout = { 10, 0 }; /* 10s */

struct snmp_messages snmp_messages = RB_INITIALIZER(&snmp_messages);

static struct privsep_proc procs[] = {
	{ "parent",	PROC_PARENT, snmpe_dispatch_parent }
};

void
snmpe(struct privsep *ps, struct privsep_proc *p)
{
	struct snmpd		*env = ps->ps_env;
	struct address		*h;

	if ((setlocale(LC_CTYPE, "en_US.UTF-8")) == NULL)
		fatal("setlocale(LC_CTYPE, \"en_US.UTF-8\")");

	appl();

	/* bind SNMP UDP/TCP sockets */
	TAILQ_FOREACH(h, &env->sc_addresses, entry)
		if ((h->fd = snmpe_bind(h)) == -1)
			fatal("snmpe: failed to bind SNMP socket");

	proc_run(ps, p, procs, nitems(procs), snmpe_init, NULL);
}

void
snmpe_init(struct privsep *ps, struct privsep_proc *p, void *arg)
{
	struct snmpd		*env = ps->ps_env;
	struct address		*h;

	usm_generate_keys();
	appl_init();

	/* listen for incoming SNMP UDP/TCP messages */
	TAILQ_FOREACH(h, &env->sc_addresses, entry) {
		if (h->type == SOCK_STREAM) {
			if (listen(h->fd, 5) < 0)
				fatalx("snmpe: failed to listen on socket");
			event_set(&h->ev, h->fd, EV_READ, snmpe_acceptcb, h);
			evtimer_set(&h->evt, snmpe_acceptcb, h);
		} else {
			event_set(&h->ev, h->fd, EV_READ|EV_PERSIST,
			    snmpe_recvmsg, h);
		}
		event_add(&h->ev, NULL);
	}

	/* no filesystem visibility */
	if (unveil("/", "") == -1)
		fatal("unveil /");
	if (pledge("stdio recvfd inet unix", NULL) == -1)
		fatal("pledge");

	log_info("snmpe %s: ready",
	    tohexstr(env->sc_engineid, env->sc_engineid_len));
	trap_init();
}

void
snmpe_shutdown(void)
{
	struct address *h;

	TAILQ_FOREACH(h, &snmpd_env->sc_addresses, entry) {
		event_del(&h->ev);
		if (h->type == SOCK_STREAM)
			event_del(&h->evt);
		close(h->fd);
	}
	appl_shutdown();
}

int
snmpe_dispatch_parent(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	switch (imsg->hdr.type) {
	case IMSG_AX_FD:
		appl_agentx_backend(imsg_get_fd(imsg));
		return 0;
	default:
		return -1;
	}
}

int
snmpe_bind(struct address *addr)
{
	char	 buf[512];
	int	 val, s;

	if ((s = snmpd_socket_af(&addr->ss, addr->type)) == -1)
		return (-1);

	/*
	 * Socket options
	 */
	if (fcntl(s, F_SETFL, O_NONBLOCK) == -1)
		goto bad;

	if (addr->type == SOCK_STREAM) {
		val = 1;
		if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
		    &val, sizeof(val)) == -1)
			fatal("setsockopt SO_REUSEADDR");
	} else { /* UDP */
		switch (addr->ss.ss_family) {
		case AF_INET:
			val = 1;
			if (setsockopt(s, IPPROTO_IP, IP_RECVDSTADDR,
			    &val, sizeof(int)) == -1) {
				log_warn("%s: failed to set IPv4 packet info",
				    __func__);
				goto bad;
			}
			break;
		case AF_INET6:
			val = 1;
			if (setsockopt(s, IPPROTO_IPV6, IPV6_RECVPKTINFO,
			    &val, sizeof(int)) == -1) {
				log_warn("%s: failed to set IPv6 packet info",
				    __func__);
				goto bad;
			}
		}
	}

	if (bind(s, (struct sockaddr *)&addr->ss, addr->ss.ss_len) == -1)
		goto bad;

	if (print_host(&addr->ss, buf, sizeof(buf)) == NULL)
		goto bad;

	log_info("snmpe: listening on %s %s:%d",
	    (addr->type == SOCK_STREAM) ? "tcp" : "udp", buf, addr->port);

	return (s);

 bad:
	close(s);
	return (-1);
}

const char *
snmpe_pdutype2string(enum snmp_pdutype pdutype)
{
	static char unknown[sizeof("Unknown (4294967295)")];

	switch (pdutype) {
	case SNMP_C_GETREQ:
		return "GetRequest";
	case SNMP_C_GETNEXTREQ:
		return "GetNextRequest";
	case SNMP_C_RESPONSE:
		return "Response";
	case SNMP_C_SETREQ:
		return "SetRequest";
	case SNMP_C_TRAP:
		return "Trap";
	case SNMP_C_GETBULKREQ:
		return "GetBulkRequest";
	case SNMP_C_INFORMREQ:
		return "InformRequest";
	case SNMP_C_TRAPV2:
		return "SNMPv2-Trap";
	case SNMP_C_REPORT:
		return "Report";
	}

	snprintf(unknown, sizeof(unknown), "Unknown (%u)", pdutype);
	return unknown;
}

int
snmpe_parse(struct snmp_message *msg)
{
	struct snmpd		*env = snmpd_env;
	struct snmp_stats	*stats = &env->sc_stats;
	struct ber_element	*a;
	long long		 ver, req;
	long long		 errval, erridx;
	u_int			 class;
	char			*comn;
	char			*flagstr, *ctxname, *engineid;
	size_t			 len;
	struct sockaddr_storage *ss = &msg->sm_ss;
	struct ber_element	*root = msg->sm_req;

	msg->sm_errstr = "invalid message";

	do {
		msg->sm_transactionid = arc4random();
	} while (msg->sm_transactionid == 0 ||
	    RB_INSERT(snmp_messages, &snmp_messages, msg) != NULL);

	if (ober_scanf_elements(root, "{ie", &ver, &a) != 0)
		goto parsefail;

	/* SNMP version and community */
	msg->sm_version = ver;
	switch (msg->sm_version) {
	case SNMP_V1:
		if (!(msg->sm_aflags & ADDRESS_FLAG_SNMPV1)) {
			msg->sm_errstr = "SNMPv1 disabled";
			goto badversion;
		}
	case SNMP_V2:
		if (msg->sm_version == SNMP_V2 &&
		    !(msg->sm_aflags & ADDRESS_FLAG_SNMPV2)) {
			msg->sm_errstr = "SNMPv2c disabled";
			goto badversion;
		}
		if (ober_scanf_elements(a, "seS$", &comn, &msg->sm_pdu) != 0)
			goto parsefail;
		if (strlcpy(msg->sm_community, comn,
		    sizeof(msg->sm_community)) >= sizeof(msg->sm_community) ||
		    msg->sm_community[0] == '\0') {
			stats->snmp_inbadcommunitynames++;
			msg->sm_errstr = "invalid community name";
			goto fail;
		}
		break;
	case SNMP_V3:
		if (!(msg->sm_aflags & ADDRESS_FLAG_SNMPV3)) {
			msg->sm_errstr = "SNMPv3 disabled";
			goto badversion;
		}
		if (ober_scanf_elements(a, "{iisi$}e",
		    &msg->sm_msgid, &msg->sm_max_msg_size, &flagstr,
		    &msg->sm_secmodel, &a) != 0)
			goto parsefail;

		msg->sm_flags = *flagstr;
		if ((a = usm_decode(msg, a, &msg->sm_errstr)) == NULL)
			goto parsefail;

		if (MSG_SECLEVEL(msg) < env->sc_min_seclevel ||
		    msg->sm_secmodel != SNMP_SEC_USM) {
			/* XXX currently only USM supported */
			msg->sm_errstr = "unsupported security model";
			stats->snmp_usmbadseclevel++;
			msg->sm_usmerr = OIDVAL_usmErrSecLevel;
			goto parsefail;
		}

		if (ober_scanf_elements(a, "{xxeS$}$",
		    &engineid, &msg->sm_ctxengineid_len, &ctxname, &len,
		    &msg->sm_pdu) != 0)
			goto parsefail;
		if (msg->sm_ctxengineid_len > sizeof(msg->sm_ctxengineid))
			goto parsefail;
		memcpy(msg->sm_ctxengineid, engineid, msg->sm_ctxengineid_len);
		if (len > SNMPD_MAXCONTEXNAMELEN)
			goto parsefail;
		memcpy(msg->sm_ctxname, ctxname, len);
		msg->sm_ctxname[len] = '\0';
		break;
	default:
		msg->sm_errstr = "unsupported snmp version";
badversion:
		stats->snmp_inbadversions++;
		goto fail;
	}

	if (ober_scanf_elements(msg->sm_pdu, "t{e", &class, &(msg->sm_pdutype),
	    &a) != 0)
		goto parsefail;

	/* SNMP PDU context */
	if (class != BER_CLASS_CONTEXT)
		goto parsefail;

	switch (msg->sm_pdutype) {
	case SNMP_C_GETBULKREQ:
		if (msg->sm_version == SNMP_V1) {
			stats->snmp_inbadversions++;
			msg->sm_errstr =
			    "invalid request for protocol version 1";
			goto fail;
		}
		/* FALLTHROUGH */

	case SNMP_C_GETREQ:
		stats->snmp_ingetrequests++;
		/* FALLTHROUGH */

	case SNMP_C_GETNEXTREQ:
		if (msg->sm_pdutype == SNMP_C_GETNEXTREQ)
			stats->snmp_ingetnexts++;
		if (!(msg->sm_aflags & ADDRESS_FLAG_READ)) {
			msg->sm_errstr = "read requests disabled";
			goto fail;
		}
		if (msg->sm_version != SNMP_V3 &&
		    strcmp(env->sc_rdcommunity, msg->sm_community) != 0 &&
		    strcmp(env->sc_rwcommunity, msg->sm_community) != 0) {
			stats->snmp_inbadcommunitynames++;
			msg->sm_errstr = "wrong read community";
			goto fail;
		}
		break;

	case SNMP_C_SETREQ:
		stats->snmp_insetrequests++;
		if (!(msg->sm_aflags & ADDRESS_FLAG_WRITE)) {
			msg->sm_errstr = "write requests disabled";
			goto fail;
		}
		if (msg->sm_version != SNMP_V3 &&
		    strcmp(env->sc_rwcommunity, msg->sm_community) != 0) {
			if (strcmp(env->sc_rdcommunity, msg->sm_community) != 0)
				stats->snmp_inbadcommunitynames++;
			else
				stats->snmp_inbadcommunityuses++;
			msg->sm_errstr = "wrong write community";
			goto fail;
		}
		break;

	case SNMP_C_RESPONSE:
		stats->snmp_ingetresponses++;
		msg->sm_errstr = "response without request";
		goto parsefail;

	case SNMP_C_TRAP:
		if (msg->sm_version != SNMP_V1) {
			msg->sm_errstr = "trapv1 request on !SNMPv1 message";
			goto parsefail;
		}
	case SNMP_C_TRAPV2:
		if (msg->sm_pdutype == SNMP_C_TRAPV2 &&
		    !(msg->sm_version == SNMP_V2 ||
		    msg->sm_version == SNMP_V3)) {
			msg->sm_errstr = "trapv2 request on !SNMPv2C or "
			    "!SNMPv3 message";
			goto parsefail;
		}
		if (!(msg->sm_aflags & ADDRESS_FLAG_NOTIFY)) {
			msg->sm_errstr = "notify requests disabled";
			goto fail;
		}
		if (msg->sm_version == SNMP_V3) {
			msg->sm_errstr = "SNMPv3 doesn't support traps yet";
			goto fail;
		}
		if (strcmp(env->sc_trcommunity, msg->sm_community) != 0) {
			stats->snmp_inbadcommunitynames++;
			msg->sm_errstr = "wrong trap community";
			goto fail;
		}
		stats->snmp_intraps++;
		/*
		 * This should probably go into parsevarbinds, but that's for a
		 * next refactor
		 */
		if (traphandler_parse(msg) == -1)
			goto fail;
		/* Shortcircuit */
		return 0;
	default:
		msg->sm_errstr = "invalid context";
		goto parsefail;
	}

	/* SNMP PDU */
	if (ober_scanf_elements(a, "iiie{e{}}$",
	    &req, &errval, &erridx, &msg->sm_pduend,
	    &msg->sm_varbind) != 0) {
		stats->snmp_silentdrops++;
		msg->sm_errstr = "invalid PDU";
		goto fail;
	}

	for (len = 0, a = msg->sm_varbind; a != NULL; a = a->be_next, len++) {
		if (ober_scanf_elements(a, "{oS$}", NULL) == -1)
			goto parsefail;
	}
	/*
	 * error-status == non-repeaters
	 * error-index == max-repetitions
	 */
	if (msg->sm_pdutype == SNMP_C_GETBULKREQ &&
	    (errval < 0 || errval > (long long)len ||
	    erridx < 1 || erridx > UINT16_MAX))
		goto parsefail;

	msg->sm_request = req;
	msg->sm_error = errval;
	msg->sm_errorindex = erridx;

	print_host(ss, msg->sm_host, sizeof(msg->sm_host));
	if (msg->sm_version == SNMP_V3)
		log_debug("%s: %s:%hd: SNMPv3 pdutype %s, flags %#x, "
		    "secmodel %lld, user '%s', ctx-engine %s, ctx-name '%s', "
		    "request %lld", __func__, msg->sm_host, msg->sm_port,
		    snmpe_pdutype2string(msg->sm_pdutype), msg->sm_flags,
		    msg->sm_secmodel, msg->sm_username,
		    tohexstr(msg->sm_ctxengineid, msg->sm_ctxengineid_len),
		    msg->sm_ctxname, msg->sm_request);
	else
		log_debug("%s: %s:%hd: SNMPv%d '%s' pdutype %s request %lld",
		    __func__, msg->sm_host, msg->sm_port, msg->sm_version + 1,
		    msg->sm_community, snmpe_pdutype2string(msg->sm_pdutype),
		    msg->sm_request);

	return (0);

 parsefail:
	stats->snmp_inasnparseerrs++;
 fail:
	print_host(ss, msg->sm_host, sizeof(msg->sm_host));
	log_debug("%s: %s:%hd: %s", __func__, msg->sm_host, msg->sm_port,
	    msg->sm_errstr);
	return (-1);
}

int
snmpe_parsevarbinds(struct snmp_message *msg)
{
	appl_processpdu(msg, msg->sm_ctxname, msg->sm_version, msg->sm_pdu);
	return 0;
}

void
snmpe_acceptcb(int fd, short type, void *arg)
{
	struct address		*h = arg;
	struct sockaddr_storage	 ss;
	socklen_t		 len = sizeof(ss);
	struct snmp_message	*msg;
	int afd;

	event_add(&h->ev, NULL);
	if ((type & EV_TIMEOUT))
		return;

	if ((afd = accept4(fd, (struct sockaddr *)&ss, &len,
	    SOCK_NONBLOCK|SOCK_CLOEXEC)) < 0) {
		/* Pause accept if we are out of file descriptors  */
		if (errno == ENFILE || errno == EMFILE) {
			struct timeval evtpause = { 1, 0 };

			event_del(&h->ev);
			evtimer_add(&h->evt, &evtpause);
		} else if (errno != EAGAIN && errno != EINTR)
			log_debug("%s: accept4", __func__);
		return;
	}
	if ((msg = calloc(1, sizeof(*msg))) == NULL)
		goto fail;

	memcpy(&(msg->sm_ss), &ss, len);
	msg->sm_slen = len;
	msg->sm_aflags = h->flags;
	msg->sm_port = h->port;
	snmpe_prepare_read(msg, afd);
	return;
fail:
	free(msg);
	close(afd);
	return;
}

void
snmpe_prepare_read(struct snmp_message *msg, int fd)
{
	msg->sm_sock = fd;
	msg->sm_sock_tcp = 1;
	event_del(&msg->sm_sockev);
	event_set(&msg->sm_sockev, fd, EV_READ,
	    snmpe_readcb, msg);
	event_add(&msg->sm_sockev, &snmpe_tcp_timeout);
}

void
snmpe_tryparse(int fd, struct snmp_message *msg)
{
	struct snmp_stats	*stats = &snmpd_env->sc_stats;

	ober_set_application(&msg->sm_ber, smi_application);
	ober_set_readbuf(&msg->sm_ber, msg->sm_data, msg->sm_datalen);
	msg->sm_req = ober_read_elements(&msg->sm_ber, NULL);
	if (msg->sm_req == NULL) {
		if (errno == ECANCELED) {
			/* short read; try again */
			snmpe_prepare_read(msg, fd);
			return;
		}
		goto fail;
	}

	if (snmpe_parse(msg) == -1) {
		if (msg->sm_usmerr && MSG_REPORT(msg)) {
			usm_make_report(msg);
			return;
		} else
			goto fail;
	}
	stats->snmp_inpkts++;

	snmpe_dispatchmsg(msg);
	return;
 fail:
	snmp_msgfree(msg);
	close(fd);
}

void
snmpe_readcb(int fd, short type, void *arg)
{
	struct snmp_message *msg = arg;
	ssize_t len;

	if (type == EV_TIMEOUT || msg->sm_datalen >= sizeof(msg->sm_data))
		goto fail;

	len = read(fd, msg->sm_data + msg->sm_datalen,
	    sizeof(msg->sm_data) - msg->sm_datalen);
	if (len <= 0) {
		if (errno != EAGAIN && errno != EINTR)
			goto fail;
		snmpe_prepare_read(msg, fd);
		return;
	}

	msg->sm_datalen += (size_t)len;
	snmpe_tryparse(fd, msg);
	return;

 fail:
	snmp_msgfree(msg);
	close(fd);
}

void
snmpe_writecb(int fd, short type, void *arg)
{
	struct snmp_stats	*stats = &snmpd_env->sc_stats;
	struct snmp_message	*msg = arg;
	struct snmp_message	*nmsg;
	ssize_t			 len;
	size_t			 reqlen;
	struct ber		*ber = &msg->sm_ber;

	if (type == EV_TIMEOUT)
		goto fail;

	len = ber->br_wend - ber->br_wptr;

	log_debug("%s: write fd %d len %zd", __func__, fd, len);

	len = write(fd, ber->br_wptr, len);
	if (len == -1) {
		if (errno == EAGAIN || errno == EINTR)
			return;
		else
			goto fail;
	}

	ber->br_wptr += len;

	if (ber->br_wptr < ber->br_wend) {
		event_del(&msg->sm_sockev);
		event_set(&msg->sm_sockev, msg->sm_sock, EV_WRITE,
		    snmpe_writecb, msg);
		event_add(&msg->sm_sockev, &snmpe_tcp_timeout);
		return;
	}

	stats->snmp_outpkts++;

	if ((nmsg = calloc(1, sizeof(*nmsg))) == NULL)
		goto fail;
	memcpy(&(nmsg->sm_ss), &(msg->sm_ss), msg->sm_slen);
	nmsg->sm_slen = msg->sm_slen;
	nmsg->sm_aflags = msg->sm_aflags;
	nmsg->sm_port = msg->sm_port;

	/*
	 * Reuse the connection.
	 * In case we already read data of the next message, copy it over.
	 */
	reqlen = ober_calc_len(msg->sm_req);
	if (msg->sm_datalen > reqlen) {
		memcpy(nmsg->sm_data, msg->sm_data + reqlen,
		    msg->sm_datalen - reqlen);
		nmsg->sm_datalen = msg->sm_datalen - reqlen;
		snmp_msgfree(msg);
		snmpe_tryparse(fd, nmsg);
	} else {
		snmp_msgfree(msg);
		snmpe_prepare_read(nmsg, fd);
	}
	return;

 fail:
	close(fd);
	snmp_msgfree(msg);
}

void
snmpe_recvmsg(int fd, short sig, void *arg)
{
	struct address		*h = arg;
	struct snmp_stats	*stats = &snmpd_env->sc_stats;
	ssize_t			 len;
	struct snmp_message	*msg;

	if ((msg = calloc(1, sizeof(*msg))) == NULL)
		return;

	msg->sm_aflags = h->flags;
	msg->sm_sock = fd;
	msg->sm_slen = sizeof(msg->sm_ss);
	msg->sm_port = h->port;
	if ((len = recvfromto(fd, msg->sm_data, sizeof(msg->sm_data), 0,
	    (struct sockaddr *)&msg->sm_ss, &msg->sm_slen,
	    (struct sockaddr *)&msg->sm_local_ss, &msg->sm_local_slen)) < 1) {
		free(msg);
		return;
	}

	stats->snmp_inpkts++;
	msg->sm_datalen = (size_t)len;

	bzero(&msg->sm_ber, sizeof(msg->sm_ber));
	ober_set_application(&msg->sm_ber, smi_application);
	ober_set_readbuf(&msg->sm_ber, msg->sm_data, msg->sm_datalen);

	msg->sm_req = ober_read_elements(&msg->sm_ber, NULL);
	if (msg->sm_req == NULL) {
		stats->snmp_inasnparseerrs++;
		snmp_msgfree(msg);
		return;
	}

#ifdef DEBUG
	fprintf(stderr, "recv msg:\n");
	smi_debug_elements(msg->sm_req);
#endif

	if (snmpe_parse(msg) == -1) {
		if (msg->sm_usmerr != 0 && MSG_REPORT(msg)) {
			usm_make_report(msg);
			return;
		} else {
			snmp_msgfree(msg);
			return;
		}
	}

	snmpe_dispatchmsg(msg);
}

void
snmpe_dispatchmsg(struct snmp_message *msg)
{
	if (msg->sm_pdutype == SNMP_C_TRAP ||
	    msg->sm_pdutype == SNMP_C_TRAPV2) {
		snmp_msgfree(msg);
		return;
	}
	/* dispatched to subagent */
	/* XXX Do proper error handling */
	(void) snmpe_parsevarbinds(msg);

	return;
	/*
	 * Leave code here for now so it's easier to switch back in case of
	 * issues.
	 */
	/* respond directly */
	msg->sm_pdutype = SNMP_C_RESPONSE;
	snmpe_response(msg);
}

void
snmpe_send(struct snmp_message *msg, enum snmp_pdutype type, int32_t requestid,
    int32_t error, uint32_t index, struct ber_element *varbindlist)
{
	msg->sm_request = requestid;
	msg->sm_pdutype = type;
	msg->sm_error = error;
	msg->sm_errorindex = index;
	msg->sm_varbindresp = varbindlist;

	snmpe_response(msg);
}

void
snmpe_response(struct snmp_message *msg)
{
	struct snmp_stats	*stats = &snmpd_env->sc_stats;
	u_int8_t		*ptr = NULL;
	ssize_t			 len;

	if (msg->sm_varbindresp == NULL && msg->sm_pduend != NULL)
		msg->sm_varbindresp = ober_unlink_elements(msg->sm_pduend);

	switch (msg->sm_error) {
	case SNMP_ERROR_NONE:
		break;
	case SNMP_ERROR_TOOBIG:
		stats->snmp_intoobigs++;
		break;
	case SNMP_ERROR_NOSUCHNAME:
		stats->snmp_innosuchnames++;
		break;
	case SNMP_ERROR_BADVALUE:
		stats->snmp_inbadvalues++;
		break;
	case SNMP_ERROR_READONLY:
		stats->snmp_inreadonlys++;
		break;
	case SNMP_ERROR_GENERR:
	default:
		stats->snmp_ingenerrs++;
		break;
	}

	/* Create new SNMP packet */
	if (snmpe_encode(msg) < 0)
		goto done;

	len = ober_write_elements(&msg->sm_ber, msg->sm_resp);
	if (ober_get_writebuf(&msg->sm_ber, (void *)&ptr) == -1)
		goto done;

	usm_finalize_digest(msg, ptr, len);
	if (msg->sm_sock_tcp) {
		msg->sm_ber.br_wptr = msg->sm_ber.br_wbuf;
		event_del(&msg->sm_sockev);
		event_set(&msg->sm_sockev, msg->sm_sock, EV_WRITE,
		    snmpe_writecb, msg);
		event_add(&msg->sm_sockev, &snmpe_tcp_timeout);
		return;
	} else {
		len = sendtofrom(msg->sm_sock, ptr, len, 0,
		    (struct sockaddr *)&msg->sm_ss, msg->sm_slen,
		    (struct sockaddr *)&msg->sm_local_ss, msg->sm_local_slen);
		if (len != -1)
			stats->snmp_outpkts++;
	}

 done:
	snmp_msgfree(msg);
}

void
snmp_msgfree(struct snmp_message *msg)
{
	if (msg->sm_transactionid != 0)
		RB_REMOVE(snmp_messages, &snmp_messages, msg);
	event_del(&msg->sm_sockev);
	ober_free(&msg->sm_ber);
	if (msg->sm_req != NULL)
		ober_free_elements(msg->sm_req);
	if (msg->sm_resp != NULL)
		ober_free_elements(msg->sm_resp);
	free(msg);
}

int
snmpe_encode(struct snmp_message *msg)
{
	struct ber_element	*ehdr;
	struct ber_element	*pdu, *epdu;

	msg->sm_resp = ober_add_sequence(NULL);
	if ((ehdr = ober_add_integer(msg->sm_resp, msg->sm_version)) == NULL)
		return -1;
	if (msg->sm_version == SNMP_V3) {
		char	f = MSG_SECLEVEL(msg);

		if ((ehdr = ober_printf_elements(ehdr, "{iixi}", msg->sm_msgid,
		    msg->sm_max_msg_size, &f, sizeof(f),
		    msg->sm_secmodel)) == NULL)
			return -1;

		/* XXX currently only USM supported */
		if ((ehdr = usm_encode(msg, ehdr)) == NULL)
			return -1;
	} else {
		if ((ehdr = ober_add_string(ehdr, msg->sm_community)) == NULL)
			return -1;
	}

	pdu = epdu = ober_add_sequence(NULL);
	if (msg->sm_version == SNMP_V3) {
		if ((epdu = ober_printf_elements(epdu, "xs{",
		    snmpd_env->sc_engineid, snmpd_env->sc_engineid_len,
		    msg->sm_ctxname)) == NULL) {
			ober_free_elements(pdu);
			return -1;
		}
	}

	if (!ober_printf_elements(epdu, "tiii{e}", BER_CLASS_CONTEXT,
	    msg->sm_pdutype, msg->sm_request,
	    msg->sm_error, msg->sm_errorindex,
	    msg->sm_varbindresp)) {
		ober_free_elements(pdu);
		return -1;
	}

	if (MSG_HAS_PRIV(msg))
		pdu = usm_encrypt(msg, pdu);
	ober_link_elements(ehdr, pdu);

#ifdef DEBUG
	fprintf(stderr, "resp msg:\n");
	smi_debug_elements(msg->sm_resp);
#endif
	return 0;
}

int
snmp_messagecmp(struct snmp_message *m1, struct snmp_message *m2)
{
	return (m1->sm_transactionid < m2->sm_transactionid ? -1 :
	    m1->sm_transactionid > m2->sm_transactionid);
}

RB_GENERATE(snmp_messages, snmp_message, sm_entry, snmp_messagecmp)
