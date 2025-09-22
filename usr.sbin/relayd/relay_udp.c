/*	$OpenBSD: relay_udp.c,v 1.51 2024/05/18 06:34:46 jsg Exp $	*/

/*
 * Copyright (c) 2007 - 2013 Reyk Floeter <reyk@openbsd.org>
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
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/tree.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <event.h>
#include <imsg.h>

#include "relayd.h"

extern volatile sig_atomic_t relay_sessions;
extern objid_t relay_conid;

static struct relayd *env = NULL;
struct shuffle relay_shuffle;

int		 relay_udp_socket(struct sockaddr_storage *, in_port_t,
		    struct protocol *);
void		 relay_udp_timeout(int, short, void *);

void		 relay_dns_log(struct rsession *, u_int8_t *, size_t);
void		*relay_dns_validate(struct rsession *,
		    struct relay *, struct sockaddr_storage *,
		    u_int8_t *, size_t);
int		 relay_dns_request(struct rsession *);
void		 relay_udp_response(int, short, void *);
void		 relay_dns_result(struct rsession *, u_int8_t *, size_t);
int		 relay_dns_cmp(struct rsession *, struct rsession *);

void
relay_udp_privinit(struct relay *rlay)
{
	if (rlay->rl_conf.flags & F_TLS)
		fatalx("tls over udp is not supported");
	rlay->rl_conf.flags |= F_UDP;
}

void
relay_udp_init(struct relayd *x_env, struct relay *rlay)
{
	struct protocol		*proto = rlay->rl_proto;

	if (env == NULL)
		env = x_env;

	switch (proto->type) {
	case RELAY_PROTO_DNS:
		proto->validate = relay_dns_validate;
		proto->request = relay_dns_request;
		proto->cmp = relay_dns_cmp;
		shuffle_init(&relay_shuffle);
		break;
	default:
		fatalx("unsupported udp protocol");
		break;
	}
}

int
relay_udp_bind(struct sockaddr_storage *ss, in_port_t port,
    struct protocol *proto)
{
	int s;

	if ((s = relay_udp_socket(ss, port, proto)) == -1)
		return (-1);

	if (bind(s, (struct sockaddr *)ss, ss->ss_len) == -1)
		goto bad;

	return (s);

 bad:
	close(s);
	return (-1);
}

int
relay_udp_socket(struct sockaddr_storage *ss, in_port_t port,
    struct protocol *proto)
{
	int s = -1, val;

	if (relay_socket_af(ss, port) == -1)
		goto bad;

	if ((s = socket(ss->ss_family, SOCK_DGRAM | SOCK_NONBLOCK,
	    IPPROTO_UDP)) == -1)
		goto bad;

	/*
	 * Socket options
	 */
	if (proto->tcpflags & TCPFLAG_BUFSIZ) {
		val = proto->tcpbufsiz;
		if (setsockopt(s, SOL_SOCKET, SO_RCVBUF,
		    &val, sizeof(val)) == -1)
			goto bad;
		val = proto->tcpbufsiz;
		if (setsockopt(s, SOL_SOCKET, SO_SNDBUF,
		    &val, sizeof(val)) == -1)
			goto bad;
	}

	/*
	 * IP options
	 */
	if (proto->tcpflags & TCPFLAG_IPTTL) {
		val = (int)proto->tcpipttl;
		switch (ss->ss_family) {
		case AF_INET:
			if (setsockopt(s, IPPROTO_IP, IP_TTL,
			    &val, sizeof(val)) == -1)
				goto bad;
			break;
		case AF_INET6:
			if (setsockopt(s, IPPROTO_IPV6, IPV6_UNICAST_HOPS,
			    &val, sizeof(val)) == -1)
				goto bad;
			break;
		}
	}
	if (proto->tcpflags & TCPFLAG_IPMINTTL) {
		val = (int)proto->tcpipminttl;
		switch (ss->ss_family) {
		case AF_INET:
			if (setsockopt(s, IPPROTO_IP, IP_MINTTL,
			    &val, sizeof(val)) == -1)
				goto bad;
			break;
		case AF_INET6:
			if (setsockopt(s, IPPROTO_IPV6, IPV6_MINHOPCOUNT,
			    &val, sizeof(val)) == -1)
				goto bad;
			break;
		}
	}

	return (s);

 bad:
	if (s != -1)
		close(s);
	return (-1);
}

void
relay_udp_response(int fd, short sig, void *arg)
{
	struct rsession		*con = arg;
	struct relay		*rlay = con->se_relay;
	struct protocol		*proto = rlay->rl_proto;
	void			*priv = NULL;
	struct sockaddr_storage	 ss;
	u_int8_t		 buf[IBUF_READ_SIZE];
	ssize_t			 len;
	socklen_t		 slen;

	if (sig == EV_TIMEOUT) {
		relay_udp_timeout(fd, sig, arg);
		return;
	}

	if (rlay->rl_conf.flags & F_DISABLE)
		return;

	slen = sizeof(ss);
	if ((len = recvfrom(fd, buf, sizeof(buf), 0,
	    (struct sockaddr*)&ss, &slen)) < 1)
		return;

	/* Parse and validate the packet header */
	if (proto->validate != NULL &&
	    (priv = (*proto->validate)(con, rlay, &ss, buf, len)) == NULL)
		return;

	relay_close(con, "unknown response", 1);
	free(priv);
}

void
relay_udp_server(int fd, short sig, void *arg)
{
	struct privsep *ps = env->sc_ps;
	struct relay *rlay = arg;
	struct protocol *proto = rlay->rl_proto;
	struct rsession *con = NULL;
	struct ctl_natlook *cnl = NULL;
	socklen_t slen;
	struct timeval tv;
	struct sockaddr_storage ss;
	u_int8_t buf[IBUF_READ_SIZE];
	void *priv = NULL;
	ssize_t len;

	event_add(&rlay->rl_ev, NULL);

	if (rlay->rl_conf.flags & F_DISABLE)
		return;

	slen = sizeof(ss);
	if ((len = recvfrom(fd, buf, sizeof(buf), 0,
	    (struct sockaddr*)&ss, &slen)) < 1)
		return;

	if (proto->validate != NULL &&
	    (priv = (*proto->validate)(NULL, rlay, &ss, buf, len)) == NULL)
		return;

	if ((con = calloc(1, sizeof(*con))) == NULL) {
		free(priv);
		return;
	}

	/*
	 * Replace the DNS request Id with a random Id.
	 */
	con->se_priv = priv;
	con->se_in.s = -1;
	con->se_out.s = -1;
	con->se_in.dst = &con->se_out;
	con->se_out.dst = &con->se_in;
	con->se_in.con = con;
	con->se_out.con = con;
	con->se_relay = rlay;
	con->se_id = ++relay_conid;
	con->se_in.dir = RELAY_DIR_REQUEST;
	con->se_out.dir = RELAY_DIR_RESPONSE;
	con->se_retry = rlay->rl_conf.dstretry;
	con->se_out.port = rlay->rl_conf.dstport;
	switch (ss.ss_family) {
	case AF_INET:
		con->se_in.port = ((struct sockaddr_in *)&ss)->sin_port;
		break;
	case AF_INET6:
		con->se_in.port = ((struct sockaddr_in6 *)&ss)->sin6_port;
		break;
	}
	bcopy(&ss, &con->se_in.ss, sizeof(con->se_in.ss));

	getmonotime(&con->se_tv_start);
	bcopy(&con->se_tv_start, &con->se_tv_last, sizeof(con->se_tv_last));

	relay_sessions++;
	SPLAY_INSERT(session_tree, &rlay->rl_sessions, con);
	relay_session_publish(con);

	/* Increment the per-relay session counter */
	rlay->rl_stats[ps->ps_instance].last++;

	/* Pre-allocate output buffer */
	con->se_out.output = evbuffer_new();
	if (con->se_out.output == NULL) {
		relay_close(con, "failed to allocate output buffer", 1);
		return;
	}

	/* Pre-allocate log buffer */
	con->se_haslog = 0;
	con->se_log = evbuffer_new();
	if (con->se_log == NULL) {
		relay_close(con, "failed to allocate log buffer", 1);
		return;
	}

	if (rlay->rl_conf.flags & F_NATLOOK) {
		if ((cnl = calloc(1, sizeof(*cnl))) == NULL) {
			relay_close(con, "failed to allocate natlookup", 1);
			return;
		}
	}

	/* Save the received data */
	if (evbuffer_add(con->se_out.output, buf, len) == -1) {
		relay_close(con, "failed to store buffer", 1);
		free(cnl);
		return;
	}

	if (cnl != NULL) {
		con->se_cnl = cnl;
		bzero(cnl, sizeof(*cnl));
		cnl->in = -1;
		cnl->id = con->se_id;
		cnl->proc = ps->ps_instance;
		cnl->proto = IPPROTO_UDP;
		bcopy(&con->se_in.ss, &cnl->src, sizeof(cnl->src));
		bcopy(&rlay->rl_conf.ss, &cnl->dst, sizeof(cnl->dst));
		proc_compose(env->sc_ps, PROC_PFE,
		    IMSG_NATLOOK, cnl, sizeof(*cnl));

		/* Schedule timeout */
		evtimer_set(&con->se_ev, relay_natlook, con);
		bcopy(&rlay->rl_conf.timeout, &tv, sizeof(tv));
		evtimer_add(&con->se_ev, &tv);
		return;
	}

	relay_session(con);
}

void
relay_udp_timeout(int fd, short sig, void *arg)
{
	struct rsession		*con = arg;

	if (sig != EV_TIMEOUT)
		fatalx("invalid timeout event");

	relay_close(con, "udp timeout", 1);
}

/*
 * Domain Name System support
 */

struct relay_dns_priv {
	u_int16_t	dp_inkey;
	u_int16_t	dp_outkey;
};

struct relay_dnshdr {
	u_int16_t	dns_id;

	u_int8_t	dns_flags0;
#define  DNS_F0_QR	0x80		/* response flag */
#define  DNS_F0_OPCODE	0x78		/* message type */
#define  DNS_F0_AA	0x04		/* authoritative answer */
#define  DNS_F0_TC	0x02		/* truncated message */
#define  DNS_F0_RD	0x01		/* recursion desired */

	u_int8_t	dns_flags1;
#define  DNS_F1_RA	0x80		/* recursion available */
#define  DNS_F1_RES	0x40		/* reserved */
#define  DNS_F1_AD	0x20		/* authentic data */
#define  DNS_F1_CD	0x10		/* checking disabled */
#define  DNS_F1_RCODE	0x0f		/* response code */

	u_int16_t	dns_qdcount;
	u_int16_t	dns_ancount;
	u_int16_t	dns_nscount;
	u_int16_t	dns_arcount;
} __packed;

void
relay_dns_log(struct rsession *con, u_int8_t *buf, size_t len)
{
	struct relay_dnshdr	*hdr = (struct relay_dnshdr *)buf;

	/* Validate the header length */
	if (len < sizeof(*hdr)) {
		log_debug("%s: session %d: short dns packet", __func__,
		    con->se_id);
		return;
	}

	log_debug("%s: session %d: %s id 0x%x "
	    "flags 0x%x:0x%x qd %u an %u ns %u ar %u", __func__,
	    con->se_id,
	    hdr->dns_flags0 & DNS_F0_QR ? "response" : "request",
	    ntohs(hdr->dns_id),
	    hdr->dns_flags0,
	    hdr->dns_flags1,
	    ntohs(hdr->dns_qdcount),
	    ntohs(hdr->dns_ancount),
	    ntohs(hdr->dns_nscount),
	    ntohs(hdr->dns_arcount));
}

void *
relay_dns_validate(struct rsession *con, struct relay *rlay,
    struct sockaddr_storage *ss, u_int8_t *buf, size_t len)
{
	struct relay_dnshdr	*hdr = (struct relay_dnshdr *)buf;
	struct rsession		 lookup;
	u_int16_t		 key;
	struct relay_dns_priv	*priv, lpriv;

	/* Validate the header length */
	if (len < sizeof(*hdr))
		return (NULL);

	key = ntohs(hdr->dns_id);

	/*
	 * Check if the header has the response flag set, otherwise
	 * return 0 to tell the UDP server to create a new session.
	 */
	if ((hdr->dns_flags0 & DNS_F0_QR) == 0) {
		priv = malloc(sizeof(struct relay_dns_priv));
		if (priv == NULL)
			return (NULL);
		priv->dp_inkey = shuffle_generate16(&relay_shuffle);
		priv->dp_outkey = key;
		return ((void *)priv);
	}

	/*
	 * Lookup if this response is for a known session and if the
	 * remote host matches the original destination of the request.
	 */
	if (con == NULL) {
		lpriv.dp_inkey = key;
		lookup.se_priv = &lpriv;
		if ((con = SPLAY_FIND(session_tree,
		    &rlay->rl_sessions, &lookup)) != NULL &&
		    con->se_priv != NULL &&
		    relay_cmp_af(ss, &con->se_out.ss) == 0)
			relay_dns_result(con, buf, len);
	} else {
		priv = con->se_priv;
		if (priv == NULL || key != priv->dp_inkey) {
			relay_close(con, "invalid response", 1);
			return (NULL);
		}
		relay_dns_result(con, buf, len);
	}

	/*
	 * This is not a new session, ignore it in the UDP server.
	 */
	return (NULL);
}

int
relay_dns_request(struct rsession *con)
{
	struct relay		*rlay = con->se_relay;
	struct relay_dns_priv	*priv = con->se_priv;
	u_int8_t		*buf = EVBUFFER_DATA(con->se_out.output);
	size_t			 len = EVBUFFER_LENGTH(con->se_out.output);
	struct relay_dnshdr	*hdr;
	socklen_t		 slen;

	if (buf == NULL || priv == NULL || len < 1)
		return (-1);
	if (log_getverbose() > 1)
		relay_dns_log(con, buf, len);

	getmonotime(&con->se_tv_start);

	if (!TAILQ_EMPTY(&rlay->rl_tables)) {
		if (relay_from_table(con) != 0)
			return (-1);
	} else if (con->se_out.ss.ss_family == AF_UNSPEC) {
		bcopy(&rlay->rl_conf.dstss, &con->se_out.ss,
		    sizeof(con->se_out.ss));
		con->se_out.port = rlay->rl_conf.dstport;
	}

	if ((con->se_out.s = relay_udp_socket(&con->se_out.ss,
	    con->se_out.port, rlay->rl_proto)) == -1)
		return (-1);
	slen = con->se_out.ss.ss_len;

	hdr = (struct relay_dnshdr *)buf;
	hdr->dns_id = htons(priv->dp_inkey);

 retry:
	if (sendto(con->se_out.s, buf, len, 0,
	    (struct sockaddr *)&con->se_out.ss, slen) == -1) {
		if (con->se_retry) {
			con->se_retry--;
			log_debug("%s: session %d: "
			    "forward failed: %s, %s", __func__,
			    con->se_id, strerror(errno),
			    con->se_retry ? "next retry" : "last retry");
			goto retry;
		}
		log_debug("%s: session %d: forward failed: %s", __func__,
		    con->se_id, strerror(errno));
		return (-1);
	}

	event_again(&con->se_ev, con->se_out.s, EV_TIMEOUT|EV_READ,
	    relay_udp_response, &con->se_tv_start, &rlay->rl_conf.timeout, con);

	return (0);
}

void
relay_dns_result(struct rsession *con, u_int8_t *buf, size_t len)
{
	struct relay		*rlay = con->se_relay;
	struct relay_dns_priv	*priv = con->se_priv;
	struct relay_dnshdr	*hdr;
	socklen_t		 slen;

	if (priv == NULL)
		fatalx("%s: response to invalid session", __func__);

	if (log_getverbose() > 1)
		relay_dns_log(con, buf, len);

	/*
	 * Replace the random DNS request Id with the original Id
	 */
	hdr = (struct relay_dnshdr *)buf;
	hdr->dns_id = htons(priv->dp_outkey);

	slen = con->se_out.ss.ss_len;
	if (sendto(rlay->rl_s, buf, len, 0,
	    (struct sockaddr *)&con->se_in.ss, slen) == -1) {
		relay_close(con, "response failed", 1);
		return;
	}

	relay_close(con, "session closed", 0);
}

int
relay_dns_cmp(struct rsession *a, struct rsession *b)
{
	struct relay_dns_priv	*ap = a->se_priv;
	struct relay_dns_priv	*bp = b->se_priv;

	if (ap == NULL || bp == NULL)
		fatalx("%s: invalid session", __func__);

	return (memcmp(&ap->dp_inkey, &bp->dp_inkey, sizeof(u_int16_t)));
}
