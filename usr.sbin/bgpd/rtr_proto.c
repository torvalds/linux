/*	$OpenBSD: rtr_proto.c,v 1.52 2025/04/14 14:50:29 claudio Exp $ */

/*
 * Copyright (c) 2020 Claudio Jeker <claudio@openbsd.org>
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
#include <sys/tree.h>
#include <errno.h>
#include <stdint.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bgpd.h"
#include "session.h"
#include "log.h"

struct rtr_header {
	uint8_t		version;
	uint8_t		type;
	union {
		uint16_t	session_id;
		uint16_t	errcode;
		struct {
			uint8_t	flags;
			uint8_t	zero;
		};
	};
	uint32_t	length;
} __packed;

#define RTR_MAX_PDU_SIZE	65535
#define RTR_MAX_PDU_ERROR_SIZE	256
#define RTR_DEFAULT_REFRESH	3600
#define RTR_DEFAULT_RETRY	600
#define RTR_DEFAULT_EXPIRE	7200
#define RTR_DEFAULT_ACTIVE	60

enum rtr_pdu_type {
	SERIAL_NOTIFY = 0,
	SERIAL_QUERY,
	RESET_QUERY,
	CACHE_RESPONSE,
	IPV4_PREFIX,
	IPV6_PREFIX = 6,
	END_OF_DATA = 7,
	CACHE_RESET = 8,
	ROUTER_KEY = 9,
	ERROR_REPORT = 10,
	ASPA = 11,
};

struct rtr_notify {
	struct rtr_header	hdr;
	uint32_t		serial;
} __packed;

struct rtr_query {
	struct rtr_header	hdr;
	uint32_t		serial;
} __packed;

struct rtr_reset {
	struct rtr_header	hdr;
} __packed;

struct rtr_response {
	struct rtr_header	hdr;
} __packed;

#define FLAG_ANNOUNCE	0x1
#define FLAG_MASK	FLAG_ANNOUNCE
struct rtr_ipv4 {
	struct rtr_header	hdr;
	uint8_t			flags;
	uint8_t			prefixlen;
	uint8_t			maxlen;
	uint8_t			zero;
	uint32_t		prefix;
	uint32_t		asnum;
} __packed;

struct rtr_ipv6 {
	struct rtr_header	hdr;
	uint8_t			flags;
	uint8_t			prefixlen;
	uint8_t			maxlen;
	uint8_t			zero;
	uint32_t		prefix[4];
	uint32_t		asnum;
} __packed;

struct rtr_routerkey {
	struct rtr_header	hdr;
	uint8_t			ski[20];
	uint32_t		asnum;
	/* followed by Subject Public Key Info */
} __packed;

struct rtr_aspa {
	struct rtr_header	hdr;
	uint32_t		cas;
	/* array of spas filling the rest of the packet */
} __packed;

struct rtr_endofdata {
	struct rtr_header	hdr;
	uint32_t		serial;
	uint32_t		refresh;
	uint32_t		retry;
	uint32_t		expire;
} __packed;

struct rtr_endofdata_v0 {
	struct rtr_header	hdr;
	uint32_t		serial;
} __packed;

enum rtr_event {
	RTR_EVNT_START,
	RTR_EVNT_CON_OPEN,
	RTR_EVNT_CON_CLOSE,
	RTR_EVNT_TIMER_REFRESH,
	RTR_EVNT_TIMER_RETRY,
	RTR_EVNT_TIMER_EXPIRE,
	RTR_EVNT_TIMER_ACTIVE,
	RTR_EVNT_SEND_ERROR,
	RTR_EVNT_SERIAL_NOTIFY,
	RTR_EVNT_CACHE_RESPONSE,
	RTR_EVNT_END_OF_DATA,
	RTR_EVNT_CACHE_RESET,
	RTR_EVNT_NO_DATA,
	RTR_EVNT_RESET_AND_CLOSE,
	RTR_EVNT_UNSUPP_PROTO_VERSION,
	RTR_EVNT_NEGOTIATION_DONE,
};

static const char *rtr_eventnames[] = {
	"start",
	"connection open",
	"connection closed",
	"refresh timer expired",
	"retry timer expired",
	"expire timer expired",
	"activity timer expired",
	"sent error",
	"serial notify received",
	"cache response received",
	"end of data received",
	"cache reset received",
	"no data",
	"connection closed with reset",
	"unsupported protocol version",
	"negotiation done",
};

enum rtr_state {
	RTR_STATE_CLOSED,
	RTR_STATE_ERROR,
	/* sessions with a state below this line will poll for incoming data */
	RTR_STATE_ESTABLISHED,
	RTR_STATE_EXCHANGE,
	RTR_STATE_NEGOTIATION,
};

static const char *rtr_statenames[] = {
	"closed",
	"error",
	"established",
	"exchange",
	"negotiation",
};

struct rtr_session {
	TAILQ_ENTRY(rtr_session)	entry;
	char				descr[PEER_DESCR_LEN];
	struct roa_tree			roa_set;
	struct aspa_tree		aspa;
	struct timer_head		timers;
	struct msgbuf			*w;
	uint32_t			id;		/* rtr_config id */
	uint32_t			serial;
	uint32_t			refresh;
	uint32_t			retry;
	uint32_t			expire;
	uint32_t			active;
	int				session_id;
	int				fd;
	int				active_lock;
	enum rtr_state			state;
	enum reconf_action		reconf_action;
	enum rtr_error			last_sent_error;
	enum rtr_error			last_recv_error;
	char				last_sent_msg[REASON_LEN];
	char				last_recv_msg[REASON_LEN];
	uint8_t				version;
	uint8_t				prev_version;
	uint8_t				min_version;
	uint8_t				errored;

};

TAILQ_HEAD(, rtr_session) rtrs = TAILQ_HEAD_INITIALIZER(rtrs);

static void	rtr_fsm(struct rtr_session *, enum rtr_event);

static const char *
log_rtr(struct rtr_session *rs)
{
	return rs->descr;
}

static const char *
log_rtr_type(enum rtr_pdu_type type)
{
	static char buf[20];

	switch (type) {
	case SERIAL_NOTIFY:
		return "serial notify";
	case SERIAL_QUERY:
		return "serial query";
	case RESET_QUERY:
		return "reset query";
	case CACHE_RESPONSE:
		return "cache response";
	case IPV4_PREFIX:
		return "IPv4 prefix";
	case IPV6_PREFIX:
		return "IPv6 prefix";
	case END_OF_DATA:
		return "end of data";
	case CACHE_RESET:
		return "cache reset";
	case ROUTER_KEY:
		return "router key";
	case ERROR_REPORT:
		return "error report";
	case ASPA:
		return "aspa";
	default:
		snprintf(buf, sizeof(buf), "unknown %u", type);
		return buf;
	}
};

static uint8_t
rtr_max_session_version(struct rtr_session *rs)
{
	if (rs->min_version > RTR_DEFAULT_VERSION)
		return rs->min_version;
	return RTR_DEFAULT_VERSION;
}

static void
rtr_reset_cache(struct rtr_session *rs)
{
	/* reset session */
	rs->session_id = -1;
	timer_stop(&rs->timers, Timer_Rtr_Expire);
	free_roatree(&rs->roa_set);
	free_aspatree(&rs->aspa);
}

static struct ibuf *
rtr_newmsg(struct rtr_session *rs, enum rtr_pdu_type type, uint32_t len,
    uint16_t session_id)
{
	struct ibuf *buf;
	int saved_errno;

	if (len > RTR_MAX_PDU_SIZE) {
		errno = ERANGE;
		return NULL;
	}
	len += sizeof(struct rtr_header);
	if ((buf = ibuf_open(len)) == NULL)
		goto fail;
	if (ibuf_add_n8(buf, rs->version) == -1)
		goto fail;
	if (ibuf_add_n8(buf, type) == -1)
		goto fail;
	if (ibuf_add_n16(buf, session_id) == -1)
		goto fail;
	if (ibuf_add_n32(buf, len) == -1)
		goto fail;

	return buf;

 fail:
	saved_errno = errno;
	ibuf_free(buf);
	errno = saved_errno;
	return NULL;
}

static void rtr_send_error(struct rtr_session *, struct ibuf *, enum rtr_error,
    const char *, ...) __attribute__((__format__ (printf, 4, 5)));

/*
 * Try to send an error PDU to cache, put connection into error
 * state.
 */
static void
rtr_send_error(struct rtr_session *rs, struct ibuf *pdu, enum rtr_error err,
    const char *fmt, ...)
{
	struct ibuf *buf;
	va_list ap;
	size_t len = 0, mlen = 0;

	rs->last_sent_error = err;
	memset(rs->last_sent_msg, 0, sizeof(rs->last_sent_msg));
	if (fmt != NULL) {
		va_start(ap, fmt);
		vsnprintf(rs->last_sent_msg, sizeof(rs->last_sent_msg),
		    fmt, ap);
		mlen = strlen(rs->last_sent_msg);
		va_end(ap);
	}

	log_warnx("rtr %s: sending error: %s%s%s", log_rtr(rs),
	    log_rtr_error(err), mlen > 0 ? ": " : "", rs->last_sent_msg);

	if (pdu != NULL) {
		ibuf_rewind(pdu);
		len = ibuf_size(pdu);
		if (len > RTR_MAX_PDU_ERROR_SIZE) {
			len = RTR_MAX_PDU_ERROR_SIZE;
			/* truncate down can not fail */
			ibuf_truncate(pdu, RTR_MAX_PDU_ERROR_SIZE);
		}
	}

	buf = rtr_newmsg(rs, ERROR_REPORT, 2 * sizeof(uint32_t) + len + mlen,
	    err);
	if (buf == NULL)
		goto fail;
	if (ibuf_add_n32(buf, len) == -1)
		goto fail;
	if (pdu != NULL) {
		if (ibuf_add_ibuf(buf, pdu) == -1)
			goto fail;
	}
	if (ibuf_add_n32(buf, mlen) == -1)
		goto fail;
	if (ibuf_add(buf, rs->last_sent_msg, mlen) == -1)
		goto fail;
	ibuf_close(rs->w, buf);

	rtr_fsm(rs, RTR_EVNT_SEND_ERROR);
	return;

 fail:
	log_warn("rtr %s: send error report", log_rtr(rs));
	ibuf_free(buf);
}

static void
rtr_send_reset_query(struct rtr_session *rs)
{
	struct ibuf *buf;

	buf = rtr_newmsg(rs, RESET_QUERY, 0, 0);
	if (buf == NULL)
		goto fail;
	ibuf_close(rs->w, buf);
	return;

 fail:
	rtr_send_error(rs, NULL, INTERNAL_ERROR,
	    "send %s: %s", log_rtr_type(RESET_QUERY), strerror(errno));
	ibuf_free(buf);
}

static void
rtr_send_serial_query(struct rtr_session *rs)
{
	struct ibuf *buf;

	buf = rtr_newmsg(rs, SERIAL_QUERY, sizeof(uint32_t), rs->session_id);
	if (buf == NULL)
		goto fail;
	if (ibuf_add_n32(buf, rs->serial) == -1)
		goto fail;
	ibuf_close(rs->w, buf);
	return;

 fail:
	rtr_send_error(rs, NULL, INTERNAL_ERROR,
	    "send %s: %s", log_rtr_type(SERIAL_QUERY), strerror(errno));
	ibuf_free(buf);
}

/*
 * Check the session_id of the rtr_header to match the expected value.
 * Returns -1 on failure and 0 on success.
 */
static int
rtr_check_session_id(struct rtr_session *rs, uint16_t session_id,
    struct rtr_header *rh, struct ibuf *pdu)
{
	if (session_id != ntohs(rh->session_id)) {
		rtr_send_error(rs, pdu, CORRUPT_DATA,
		    "%s: bad session_id %d (expected %d)",
		    log_rtr_type(rh->type), ntohs(rh->session_id), session_id);
		return -1;
	}
	return 0;
}

/*
 * Callback for ibuf_read to get the size of a PDU.
 */
static struct ibuf *
rtr_reader_callback(struct ibuf *hdr, void *arg, int *fd)
{
	struct rtr_session *rs = arg;
	struct rtr_header rh;
	struct ibuf *b;
	ssize_t len;

	if (ibuf_get(hdr, &rh, sizeof(rh)) == -1)
		return NULL;

	len = ntohl(rh.length);

	if (len > RTR_MAX_PDU_SIZE) {
		rtr_send_error(rs, hdr, CORRUPT_DATA, "%s: too big: %zu bytes",
		    log_rtr_type(rh.type), len);
		errno = ERANGE;
		return NULL;
	}

	if ((b = ibuf_open(len)) == NULL)
		return NULL;
	return b;
}

/*
 * Parse the common rtr header (first 8 bytes) including the
 * included length field.
 * Returns -1 on failure. On success msgtype and msglen are set
 * and the function return 0.
 */
static int
rtr_parse_header(struct rtr_session *rs, struct ibuf *msg,
    enum rtr_pdu_type *msgtype)
{
	struct rtr_header rh;
	struct ibuf hdr;
	size_t len;
	uint16_t errcode;

	len = ibuf_size(msg);

	ibuf_from_ibuf(&hdr, msg);
	if (ibuf_get(&hdr, &rh, sizeof(rh)) == -1)
		fatal("%s: ibuf_get", __func__);

	if (rs->state == RTR_STATE_NEGOTIATION) {
		switch (rh.type) {
		case CACHE_RESPONSE:
		case CACHE_RESET:
			/* implicit downgrade */
			if (rh.version < rs->version) {
				rs->prev_version = rs->version;
				rs->version = rh.version;
			}
			rtr_fsm(rs, RTR_EVNT_NEGOTIATION_DONE);
			break;
		case ERROR_REPORT:
			errcode = ntohs(rh.session_id);
			if (errcode == UNSUPP_PROTOCOL_VERS ||
			    errcode == NO_DATA_AVAILABLE) {
				if (rh.version < rs->version) {
					rs->prev_version = rs->version;
					rs->version = rh.version;
				}
			}
			break;
		case SERIAL_NOTIFY:
			/* ignore SERIAL_NOTIFY */
			break;
		default:
			rtr_send_error(rs, msg, CORRUPT_DATA,
			    "%s: out of context", log_rtr_type(rh.type));
			return -1;
		}
	} else if (rh.version != rs->version && rh.type != ERROR_REPORT) {
		goto badversion;
	}

	switch (rh.type) {
	case SERIAL_NOTIFY:
		if (len != sizeof(struct rtr_notify))
			goto badlen;
		break;
	case CACHE_RESPONSE:
		if (len != sizeof(struct rtr_response))
			goto badlen;
		break;
	case IPV4_PREFIX:
		if (len != sizeof(struct rtr_ipv4))
			goto badlen;
		break;
	case IPV6_PREFIX:
		if (len != sizeof(struct rtr_ipv6))
			goto badlen;
		break;
	case END_OF_DATA:
		if (rs->version == 0) {
			if (len != sizeof(struct rtr_endofdata_v0))
				goto badlen;
		} else {
			if (len != sizeof(struct rtr_endofdata))
				goto badlen;
		}
		break;
	case CACHE_RESET:
		if (len != sizeof(struct rtr_reset))
			goto badlen;
		break;
	case ROUTER_KEY:
		if (rs->version < 1)
			goto badversion;
		if (len < sizeof(struct rtr_routerkey))
			goto badlen;
		break;
	case ERROR_REPORT:
		if (len < 16)
			goto badlen;
		break;
	case ASPA:
		if (rs->version < 2)
			goto badversion;
		if (len < sizeof(struct rtr_aspa) || (len % 4) != 0)
			goto badlen;
		break;
	default:
		rtr_send_error(rs, msg, UNSUPP_PDU_TYPE, "type %s",
		    log_rtr_type(rh.type));
		return -1;
	}

	*msgtype = rh.type;

	return 0;

 badlen:
	rtr_send_error(rs, msg, CORRUPT_DATA, "%s: bad length: %zu bytes",
	    log_rtr_type(rh.type), len);
	return -1;

 badversion:
	rtr_send_error(rs, msg, UNEXP_PROTOCOL_VERS, "%s: version %d",
	    log_rtr_type(rh.type), rh.version);
	return -1;
}

static int
rtr_parse_notify(struct rtr_session *rs, struct ibuf *pdu)
{
	struct rtr_notify notify;

	/* ignore SERIAL_NOTIFY during startup */
	if (rs->state == RTR_STATE_NEGOTIATION)
		return 0;

	if (ibuf_get(pdu, &notify, sizeof(notify)) == -1)
		goto badlen;

	/* set session_id if not yet happened */
	if (rs->session_id == -1)
		rs->session_id = ntohs(notify.hdr.session_id);

	if (rtr_check_session_id(rs, rs->session_id, &notify.hdr, pdu) == -1)
		return -1;

	if (rs->state != RTR_STATE_ESTABLISHED) {
		log_warnx("rtr %s: received %s: while in state %s (ignored)",
		    log_rtr(rs), log_rtr_type(SERIAL_NOTIFY),
		    rtr_statenames[rs->state]);
		return 0;
	}

	rtr_fsm(rs, RTR_EVNT_SERIAL_NOTIFY);
	return 0;

 badlen:
	rtr_send_error(rs, pdu, CORRUPT_DATA, "%s: bad length",
	    log_rtr_type(SERIAL_NOTIFY));
	return -1;
}

static int
rtr_parse_cache_response(struct rtr_session *rs, struct ibuf *pdu)
{
	struct rtr_response resp;

	if (ibuf_get(pdu, &resp, sizeof(resp)) == -1)
		goto badlen;

	/* set session_id if not yet happened */
	if (rs->session_id == -1)
		rs->session_id = ntohs(resp.hdr.session_id);

	if (rtr_check_session_id(rs, rs->session_id, &resp.hdr, pdu) == -1)
		return -1;

	if (rs->state != RTR_STATE_ESTABLISHED) {
		rtr_send_error(rs, pdu, CORRUPT_DATA, "%s: out of context",
		    log_rtr_type(CACHE_RESPONSE));
		return -1;
	}

	rtr_fsm(rs, RTR_EVNT_CACHE_RESPONSE);
	return 0;

 badlen:
	rtr_send_error(rs, pdu, CORRUPT_DATA, "%s: bad length",
	    log_rtr_type(CACHE_RESPONSE));
	return -1;
}

static int
rtr_parse_ipv4_prefix(struct rtr_session *rs, struct ibuf *pdu)
{
	struct rtr_ipv4 ip4;
	struct roa *roa;

	if (ibuf_get(pdu, &ip4, sizeof(ip4)) == -1)
		goto badlen;

	if (rtr_check_session_id(rs, 0, &ip4.hdr, pdu) == -1)
		return -1;

	if (rs->state != RTR_STATE_EXCHANGE) {
		rtr_send_error(rs, pdu, CORRUPT_DATA, "%s: out of context",
		    log_rtr_type(IPV4_PREFIX));
		return -1;
	}

	if (ip4.prefixlen > 32 || ip4.maxlen > 32 ||
	    ip4.prefixlen > ip4.maxlen) {
		rtr_send_error(rs, pdu, CORRUPT_DATA,
		    "%s: bad prefixlen / maxlen", log_rtr_type(IPV4_PREFIX));
		return -1;
	}

	if ((roa = calloc(1, sizeof(*roa))) == NULL) {
		rtr_send_error(rs, NULL, INTERNAL_ERROR, "out of memory");
		return -1;
	}
	roa->aid = AID_INET;
	roa->prefixlen = ip4.prefixlen;
	roa->maxlen = ip4.maxlen;
	roa->asnum = ntohl(ip4.asnum);
	roa->prefix.inet.s_addr = ip4.prefix;

	if (ip4.flags & FLAG_ANNOUNCE) {
		if (RB_INSERT(roa_tree, &rs->roa_set, roa) != NULL) {
			rtr_send_error(rs, pdu, DUP_REC_RECV, "%s %s",
			    log_rtr_type(IPV4_PREFIX), log_roa(roa));
			free(roa);
			return -1;
		}
	} else {
		struct roa *r;

		r = RB_FIND(roa_tree, &rs->roa_set, roa);
		if (r == NULL) {
			rtr_send_error(rs, pdu, UNK_REC_WDRAWL, "%s %s",
			    log_rtr_type(IPV4_PREFIX), log_roa(roa));
			free(roa);
			return -1;
		}
		RB_REMOVE(roa_tree, &rs->roa_set, r);
		free(r);
		free(roa);
	}

	return 0;

 badlen:
	rtr_send_error(rs, pdu, CORRUPT_DATA, "%s: bad length",
	    log_rtr_type(IPV4_PREFIX));
	return -1;
}

static int
rtr_parse_ipv6_prefix(struct rtr_session *rs, struct ibuf *pdu)
{
	struct rtr_ipv6 ip6;
	struct roa *roa;

	if (ibuf_get(pdu, &ip6, sizeof(ip6)) == -1)
		goto badlen;

	if (rtr_check_session_id(rs, 0, &ip6.hdr, pdu) == -1)
		return -1;

	if (rs->state != RTR_STATE_EXCHANGE) {
		rtr_send_error(rs, pdu, CORRUPT_DATA, "%s: out of context",
		    log_rtr_type(IPV6_PREFIX));
		return -1;
	}

	if (ip6.prefixlen > 128 || ip6.maxlen > 128 ||
	    ip6.prefixlen > ip6.maxlen) {
		rtr_send_error(rs, pdu, CORRUPT_DATA,
		    "%s: bad prefixlen / maxlen", log_rtr_type(IPV6_PREFIX));
		return -1;
	}

	if ((roa = calloc(1, sizeof(*roa))) == NULL) {
		rtr_send_error(rs, NULL, INTERNAL_ERROR, "out of memory");
		return -1;
	}
	roa->aid = AID_INET6;
	roa->prefixlen = ip6.prefixlen;
	roa->maxlen = ip6.maxlen;
	roa->asnum = ntohl(ip6.asnum);
	memcpy(&roa->prefix.inet6, ip6.prefix, sizeof(roa->prefix.inet6));

	if (ip6.flags & FLAG_ANNOUNCE) {
		if (RB_INSERT(roa_tree, &rs->roa_set, roa) != NULL) {
			rtr_send_error(rs, pdu, DUP_REC_RECV, "%s %s",
			    log_rtr_type(IPV6_PREFIX), log_roa(roa));
			free(roa);
			return -1;
		}
	} else {
		struct roa *r;

		r = RB_FIND(roa_tree, &rs->roa_set, roa);
		if (r == NULL) {
			rtr_send_error(rs, pdu, UNK_REC_WDRAWL, "%s %s",
			    log_rtr_type(IPV6_PREFIX), log_roa(roa));
			free(roa);
			return -1;
		}
		RB_REMOVE(roa_tree, &rs->roa_set, r);
		free(r);
		free(roa);
	}
	return 0;

 badlen:
	rtr_send_error(rs, pdu, CORRUPT_DATA, "%s: bad length",
	    log_rtr_type(IPV6_PREFIX));
	return -1;
}

static int
rtr_parse_aspa(struct rtr_session *rs, struct ibuf *pdu)
{
	struct rtr_aspa rtr_aspa;
	struct aspa_set *aspa, *a;
	uint32_t cnt, i;
	uint8_t flags;

	if (ibuf_get(pdu, &rtr_aspa, sizeof(rtr_aspa)) == -1)
		goto badlen;

	flags = rtr_aspa.hdr.flags;
	cnt = ibuf_size(pdu) / sizeof(uint32_t);

	if ((flags & FLAG_ANNOUNCE) && cnt == 0) {
		rtr_send_error(rs, pdu, CORRUPT_DATA, "%s: "
		    "announce with empty SPAS", log_rtr_type(ASPA));
		return -1;
	}
	if ((flags & FLAG_ANNOUNCE) == 0 && cnt != 0) {
		rtr_send_error(rs, pdu, CORRUPT_DATA, "%s: "
		    "withdraw with non-empty SPAS", log_rtr_type(ASPA));
		return -1;
	}

	if (rs->state != RTR_STATE_EXCHANGE) {
		rtr_send_error(rs, pdu, CORRUPT_DATA, "%s: out of context",
		    log_rtr_type(ASPA));
		return -1;
	}

	/* treat ASPA records with too many SPAS like a withdraw */
	if (cnt > MAX_ASPA_SPAS_COUNT) {
		struct aspa_set needle = { 0 };
		needle.as = ntohl(rtr_aspa.cas);

		log_warnx("rtr %s: oversized ASPA PDU: "
		    "implicit withdraw of customerAS %s",
		    log_rtr(rs), log_as(needle.as));
		a = RB_FIND(aspa_tree, &rs->aspa, &needle);
		if (a != NULL) {
			RB_REMOVE(aspa_tree, &rs->aspa, a);
			free_aspa(a);
		}
		return 0;
	}

	/* create aspa_set entry from the rtr aspa pdu */
	if ((aspa = calloc(1, sizeof(*aspa))) == NULL) {
		rtr_send_error(rs, NULL, INTERNAL_ERROR, "out of memory");
		return -1;
	}
	aspa->as = ntohl(rtr_aspa.cas);
	aspa->num = cnt;
	if (cnt > 0) {
		if ((aspa->tas = calloc(cnt, sizeof(uint32_t))) == NULL) {
			free_aspa(aspa);
			rtr_send_error(rs, NULL, INTERNAL_ERROR,
			    "out of memory");
			return -1;
		}
		for (i = 0; i < cnt; i++) {
			if (ibuf_get_n32(pdu, &aspa->tas[i]) == -1) {
				free_aspa(aspa);
				goto badlen;
			}
		}
	}

	if (flags & FLAG_ANNOUNCE) {
		a = RB_INSERT(aspa_tree, &rs->aspa, aspa);
		if (a != NULL) {
			RB_REMOVE(aspa_tree, &rs->aspa, a);
			free_aspa(a);

			if (RB_INSERT(aspa_tree, &rs->aspa, aspa) != NULL) {
				rtr_send_error(rs, NULL, INTERNAL_ERROR,
				    "corrupt aspa tree");
				free_aspa(aspa);
				return -1;
			}
		}
	} else {
		a = RB_FIND(aspa_tree, &rs->aspa, aspa);
		if (a == NULL) {
			rtr_send_error(rs, pdu, UNK_REC_WDRAWL, "%s %s",
			    log_rtr_type(ASPA), log_aspa(aspa));
			free_aspa(aspa);
			return -1;
		}
		RB_REMOVE(aspa_tree, &rs->aspa, a);
		free_aspa(a);
		free_aspa(aspa);
	}

	return 0;

 badlen:
	rtr_send_error(rs, pdu, CORRUPT_DATA, "%s: bad length",
	    log_rtr_type(ASPA));
	return -1;
}

static int
rtr_parse_end_of_data_v0(struct rtr_session *rs, struct ibuf *pdu)
{
	struct rtr_endofdata_v0 eod;

	if (ibuf_get(pdu, &eod, sizeof(eod)) == -1)
		goto badlen;

	if (rtr_check_session_id(rs, rs->session_id, &eod.hdr, pdu) == -1)
		return -1;

	if (rs->state != RTR_STATE_EXCHANGE) {
		rtr_send_error(rs, pdu, CORRUPT_DATA, "%s: out of context",
		    log_rtr_type(END_OF_DATA));
		return -1;
	}

	rs->serial = ntohl(eod.serial);

	rtr_fsm(rs, RTR_EVNT_END_OF_DATA);
	return 0;

 badlen:
	rtr_send_error(rs, pdu, CORRUPT_DATA, "%s: bad length",
	    log_rtr_type(END_OF_DATA));
	return -1;
}

static int
rtr_parse_end_of_data(struct rtr_session *rs, struct ibuf *pdu)
{
	struct rtr_endofdata eod;
	uint32_t t;

	/* version 0 does not have the timing values */
	if (rs->version == 0)
		return rtr_parse_end_of_data_v0(rs, pdu);

	if (ibuf_get(pdu, &eod, sizeof(eod)) == -1)
		goto badlen;

	if (rtr_check_session_id(rs, rs->session_id, &eod.hdr, pdu) == -1)
		return -1;

	if (rs->state != RTR_STATE_EXCHANGE) {
		rtr_send_error(rs, pdu, CORRUPT_DATA, "%s: out of context",
		    log_rtr_type(END_OF_DATA));
		return -1;
	}

	rs->serial = ntohl(eod.serial);
	/* validate timer values to be in the right range */
	t = ntohl(eod.refresh);
	if (t < 1 || t > 86400)
		goto bad;
	rs->refresh = t;
	t = ntohl(eod.retry);
	if (t < 1 || t > 7200)
		goto bad;
	rs->retry = t;
	t = ntohl(eod.expire);
	if (t < 600 || t > 172800)
		goto bad;
	if (t <= rs->retry || t <= rs->refresh)
		goto bad;
	rs->expire = t;

	rtr_fsm(rs, RTR_EVNT_END_OF_DATA);
	return 0;

bad:
	rtr_send_error(rs, pdu, CORRUPT_DATA, "%s: bad timeout values",
	    log_rtr_type(END_OF_DATA));
	return -1;

badlen:
	rtr_send_error(rs, pdu, CORRUPT_DATA, "%s: bad length",
	    log_rtr_type(END_OF_DATA));
	return -1;
}

static int
rtr_parse_cache_reset(struct rtr_session *rs, struct ibuf *pdu)
{
	struct rtr_reset reset;

	if (ibuf_get(pdu, &reset, sizeof(reset)) == -1)
		goto badlen;

	if (rtr_check_session_id(rs, 0, &reset.hdr, pdu) == -1)
		return -1;

	if (rs->state != RTR_STATE_ESTABLISHED) {
		rtr_send_error(rs, pdu, CORRUPT_DATA, "%s: out of context",
		    log_rtr_type(CACHE_RESET));
		return -1;
	}

	rtr_fsm(rs, RTR_EVNT_CACHE_RESET);
	return 0;

 badlen:
	rtr_send_error(rs, pdu, CORRUPT_DATA, "%s: bad length",
	    log_rtr_type(CACHE_RESET));
	return -1;
}

/*
 * Parse an Error Response message. This function behaves a bit different
 * from other parse functions since on error the connection needs to be
 * dropped without sending an error response back.
 */
static int
rtr_parse_error(struct rtr_session *rs, struct ibuf *pdu)
{
	struct rtr_header rh;
	struct ibuf err_pdu;
	uint32_t pdu_len, msg_len;
	char *str = NULL;
	uint16_t errcode;
	int rv = -1;

	if (ibuf_get(pdu, &rh, sizeof(rh)) == -1)
		goto fail;
	errcode = ntohs(rh.errcode);

	if (ibuf_get_n32(pdu, &pdu_len) == -1)
		goto fail;

	/* for now just ignore the embedded pdu */
	if (ibuf_get_ibuf(pdu, pdu_len, &err_pdu) == -1)
		goto fail;

	if (ibuf_get_n32(pdu, &msg_len) == -1)
		goto fail;

	/* optional error msg */
	if (msg_len != 0)
		if ((str = ibuf_get_string(pdu, msg_len)) == NULL)
			goto fail;

	log_warnx("rtr %s: received error: %s%s%s", log_rtr(rs),
	    log_rtr_error(errcode), str ? ": " : "", str ? str : "");

	if (errcode == NO_DATA_AVAILABLE) {
		rtr_fsm(rs, RTR_EVNT_NO_DATA);
		rv = 0;
	} else if (errcode == UNSUPP_PROTOCOL_VERS) {
		rtr_fsm(rs, RTR_EVNT_UNSUPP_PROTO_VERSION);
		rv = 0;
	} else
		rtr_fsm(rs, RTR_EVNT_RESET_AND_CLOSE);

	rs->last_recv_error = errcode;
	if (str)
		strlcpy(rs->last_recv_msg, str, sizeof(rs->last_recv_msg));
	else
		memset(rs->last_recv_msg, 0, sizeof(rs->last_recv_msg));

	free(str);
	return rv;

 fail:
	log_warnx("rtr %s: received %s: bad encoding", log_rtr(rs),
	    log_rtr_type(ERROR_REPORT));
	rtr_fsm(rs, RTR_EVNT_RESET_AND_CLOSE);
	return -1;
}

/*
 * Try to process received rtr message, it is possible that not a full
 * message is in the buffer. In that case stop, once new data is available
 * a retry will be done.
 */
static void
rtr_process_msg(struct rtr_session *rs, struct ibuf *msg)
{
	enum rtr_pdu_type msgtype;

	/* parse and check header */
	if (rtr_parse_header(rs, msg, &msgtype) == -1)
		return;

	switch (msgtype) {
	case SERIAL_NOTIFY:
		if (rtr_parse_notify(rs, msg) == -1)
			return;
		break;
	case CACHE_RESPONSE:
		if (rtr_parse_cache_response(rs, msg) == -1)
			return;
		break;
	case IPV4_PREFIX:
		if (rtr_parse_ipv4_prefix(rs, msg) == -1)
			return;
		break;
	case IPV6_PREFIX:
		if (rtr_parse_ipv6_prefix(rs, msg) == -1)
			return;
		break;
	case END_OF_DATA:
		if (rtr_parse_end_of_data(rs, msg) == -1)
			return;
		break;
	case CACHE_RESET:
		if (rtr_parse_cache_reset(rs, msg) == -1)
			return;
		break;
	case ROUTER_KEY:
		/* silently ignore router key */
		break;
	case ERROR_REPORT:
		if (rtr_parse_error(rs, msg) == -1) {
			/* no need to send back an error */
			return;
		}
		break;
	case ASPA:
		if (rtr_parse_aspa(rs, msg) == -1)
			return;
		break;
	default:
		/* unreachable, checked in rtr_parse_header() */
		rtr_send_error(rs, msg, UNSUPP_PDU_TYPE, "type %s",
		    log_rtr_type(msgtype));
		return;
	}
}

/*
 * Simple FSM for RTR sessions
 */
static void
rtr_fsm(struct rtr_session *rs, enum rtr_event event)
{
	enum rtr_state prev_state = rs->state;

	switch (event) {
	case RTR_EVNT_UNSUPP_PROTO_VERSION:
		if (rs->prev_version == rs->version ||
		    rs->version < rs->min_version) {
			/*
			 * Can't downgrade anymore, fail connection.
			 * RFC requires sending the error with the
			 * highest supported version number.
			 */
			rs->version = rtr_max_session_version(rs);
			rtr_send_error(rs, NULL, UNSUPP_PROTOCOL_VERS,
			    "negotiation failed");
			return;
		}
		/* try again with new version */
		if (rs->session_id == -1)
			rtr_send_reset_query(rs);
		else
			rtr_send_serial_query(rs);
		break;
	case RTR_EVNT_RESET_AND_CLOSE:
		rtr_reset_cache(rs);
		rtr_recalc();
		/* FALLTHROUGH */
	case RTR_EVNT_CON_CLOSE:
		if (rs->fd != -1) {
			/* flush buffers */
			msgbuf_clear(rs->w);
			close(rs->fd);
			rs->fd = -1;
			rtr_imsg_compose(IMSG_SOCKET_TEARDOWN, rs->id, 0,
			    NULL, 0);
		}
		/* try to reopen session */
		if (!rs->errored)
			timer_set(&rs->timers, Timer_Rtr_Retry,
			    arc4random_uniform(10));
		else
			timer_set(&rs->timers, Timer_Rtr_Retry, rs->retry);

		rs->errored = 1;
		/*
		 * A close event during version negotiation needs to remain
		 * in the negotiation state else the same error will happen
		 * over and over again. The RFC is utterly underspecified
		 * and some RTR caches close the connection after sending
		 * the error PDU.
		 */
		if (rs->state != RTR_STATE_NEGOTIATION)
			rs->state = RTR_STATE_CLOSED;
		break;
	case RTR_EVNT_START:
	case RTR_EVNT_TIMER_RETRY:
		switch (rs->state) {
		case RTR_STATE_ERROR:
			rtr_fsm(rs, RTR_EVNT_CON_CLOSE);
			break;
		case RTR_STATE_CLOSED:
		case RTR_STATE_NEGOTIATION:
			timer_set(&rs->timers, Timer_Rtr_Retry, rs->retry);
			rtr_imsg_compose(IMSG_SOCKET_SETUP, rs->id, 0, NULL, 0);
			break;
		case RTR_STATE_ESTABLISHED:
			if (rs->session_id == -1)
				rtr_send_reset_query(rs);
			else
				rtr_send_serial_query(rs);
		default:
			break;
		}
		break;
	case RTR_EVNT_CON_OPEN:
		timer_stop(&rs->timers, Timer_Rtr_Retry);
		rs->state = RTR_STATE_NEGOTIATION;
		if (rs->session_id == -1)
			rtr_send_reset_query(rs);
		else
			rtr_send_serial_query(rs);
		break;
	case RTR_EVNT_SERIAL_NOTIFY:
		/* schedule a refresh after a quick wait */
		timer_set(&rs->timers, Timer_Rtr_Refresh,
		    arc4random_uniform(10));
		break;
	case RTR_EVNT_TIMER_REFRESH:
		rtr_send_serial_query(rs);
		break;
	case RTR_EVNT_TIMER_EXPIRE:
		rtr_reset_cache(rs);
		rtr_recalc();
		break;
	case RTR_EVNT_TIMER_ACTIVE:
		log_warnx("rtr %s: activity timer fired", log_rtr(rs));
		rtr_sem_release(rs->active_lock);
		rtr_recalc();
		rs->active_lock = 0;
		break;
	case RTR_EVNT_CACHE_RESPONSE:
		rs->state = RTR_STATE_EXCHANGE;
		timer_stop(&rs->timers, Timer_Rtr_Refresh);
		timer_stop(&rs->timers, Timer_Rtr_Retry);
		timer_set(&rs->timers, Timer_Rtr_Active, rs->active);
		/* prevent rtr_recalc from running while active */
		rs->active_lock = 1;
		rtr_sem_acquire(rs->active_lock);
		break;
	case RTR_EVNT_END_OF_DATA:
		/* start refresh and expire timers */
		timer_set(&rs->timers, Timer_Rtr_Refresh, rs->refresh);
		timer_set(&rs->timers, Timer_Rtr_Expire, rs->expire);
		timer_stop(&rs->timers, Timer_Rtr_Active);
		rs->state = RTR_STATE_ESTABLISHED;
		rtr_sem_release(rs->active_lock);
		rtr_recalc();
		rs->active_lock = 0;
		rs->errored = 0;
		/* clear the last errors */
		rs->last_sent_error = NO_ERROR;
		rs->last_recv_error = NO_ERROR;
		rs->last_sent_msg[0] = '\0';
		rs->last_recv_msg[0] = '\0';
		break;
	case RTR_EVNT_CACHE_RESET:
		rtr_reset_cache(rs);
		rtr_recalc();
		/* retry after a quick wait */
		timer_set(&rs->timers, Timer_Rtr_Retry,
		    arc4random_uniform(10));
		break;
	case RTR_EVNT_NO_DATA:
		/* start retry timer */
		timer_set(&rs->timers, Timer_Rtr_Retry, rs->retry);
		/* stop refresh timer just to be sure */
		timer_stop(&rs->timers, Timer_Rtr_Refresh);
		rs->state = RTR_STATE_ESTABLISHED;
		break;
	case RTR_EVNT_SEND_ERROR:
		rtr_reset_cache(rs);
		rtr_recalc();
		rs->state = RTR_STATE_ERROR;
		break;
	case RTR_EVNT_NEGOTIATION_DONE:
		rs->state = RTR_STATE_ESTABLISHED;
		break;
	}

	log_debug("rtr %s: state change %s -> %s, reason: %s",
	    log_rtr(rs), rtr_statenames[prev_state], rtr_statenames[rs->state],
	    rtr_eventnames[event]);
}

/*
 * IO handler for RTR sessions
 */
static void
rtr_dispatch_msg(struct pollfd *pfd, struct rtr_session *rs)
{
	struct ibuf *b;

	if (pfd->revents & POLLHUP) {
		log_warnx("rtr %s: Connection closed, hangup", log_rtr(rs));
		rtr_fsm(rs, RTR_EVNT_CON_CLOSE);
		return;
	}
	if (pfd->revents & (POLLERR|POLLNVAL)) {
		log_warnx("rtr %s: Connection closed, error", log_rtr(rs));
		rtr_fsm(rs, RTR_EVNT_CON_CLOSE);
		return;
	}
	if (pfd->revents & POLLOUT && msgbuf_queuelen(rs->w) > 0) {
		if (ibuf_write(rs->fd, rs->w) == -1) {
			log_warn("rtr %s: write error", log_rtr(rs));
			rtr_fsm(rs, RTR_EVNT_CON_CLOSE);
			return;
		}
		if (rs->state == RTR_STATE_ERROR &&
		    msgbuf_queuelen(rs->w) == 0)
			rtr_fsm(rs, RTR_EVNT_CON_CLOSE);
	}
	if (pfd->revents & POLLIN) {
		switch (ibuf_read(rs->fd, rs->w)) {
		case -1:
			/* if already in error state, ignore */
			if (rs->state == RTR_STATE_ERROR)
				return;
			log_warn("rtr %s: read error", log_rtr(rs));
			rtr_fsm(rs, RTR_EVNT_CON_CLOSE);
			return;
		case 0:
			rtr_fsm(rs, RTR_EVNT_CON_CLOSE);
			return;
		}
		/* new data arrived, try to process it */
		while ((b = msgbuf_get(rs->w)) != NULL) {
			rtr_process_msg(rs, b);
			ibuf_free(b);
		}
	}
}

void
rtr_check_events(struct pollfd *pfds, size_t npfds)
{
	struct rtr_session *rs;
	struct timer *t;
	monotime_t now;
	size_t i = 0;

	for (i = 0; i < npfds; i++) {
		if (pfds[i].revents == 0)
			continue;
		TAILQ_FOREACH(rs, &rtrs, entry)
			if (rs->fd == pfds[i].fd) {
				rtr_dispatch_msg(&pfds[i], rs);
				break;
			}
		if (rs == NULL)
			log_warnx("%s: unknown fd in pollfds", __func__);
	}

	/* run all timers */
	now = getmonotime();
	TAILQ_FOREACH(rs, &rtrs, entry)
		if ((t = timer_nextisdue(&rs->timers, now)) != NULL) {
			/* stop timer so it does not trigger again */
			timer_stop(&rs->timers, t->type);
			switch (t->type) {
			case Timer_Rtr_Refresh:
				rtr_fsm(rs, RTR_EVNT_TIMER_REFRESH);
				break;
			case Timer_Rtr_Retry:
				rtr_fsm(rs, RTR_EVNT_TIMER_RETRY);
				break;
			case Timer_Rtr_Expire:
				rtr_fsm(rs, RTR_EVNT_TIMER_EXPIRE);
				break;
			case Timer_Rtr_Active:
				rtr_fsm(rs, RTR_EVNT_TIMER_ACTIVE);
				break;
			default:
				fatalx("King Bula lost in time");
			}
		}
}

size_t
rtr_count(void)
{
	struct rtr_session *rs;
	size_t count = 0;

	TAILQ_FOREACH(rs, &rtrs, entry)
		count++;
	return count;
}

size_t
rtr_poll_events(struct pollfd *pfds, size_t npfds, monotime_t *timeout)
{
	struct rtr_session *rs;
	size_t i = 0;

	TAILQ_FOREACH(rs, &rtrs, entry) {
		monotime_t nextaction;
		struct pollfd *pfd = pfds + i++;

		if (i > npfds)
			fatalx("%s: too many sessions for pollfd", __func__);

		nextaction = timer_nextduein(&rs->timers);
		if (monotime_valid(nextaction)) {
			if (monotime_cmp(nextaction, *timeout) < 0)
				*timeout = nextaction;
		}

		if (rs->state == RTR_STATE_CLOSED) {
			pfd->fd = -1;
			continue;
		}

		pfd->fd = rs->fd;
		pfd->events = 0;

		if (msgbuf_queuelen(rs->w) > 0)
			pfd->events |= POLLOUT;
		if (rs->state >= RTR_STATE_ESTABLISHED)
			pfd->events |= POLLIN;
	}

	return i;
}

struct rtr_session *
rtr_new(uint32_t id, struct rtr_config_msg *conf)
{
	struct rtr_session *rs;

	if ((rs = calloc(1, sizeof(*rs))) == NULL)
		fatal("RTR session %s", conf->descr);
	if ((rs->w = msgbuf_new_reader(sizeof(struct rtr_header),
	    rtr_reader_callback, rs)) == NULL)
		fatal("RTR session %s", conf->descr);

	RB_INIT(&rs->roa_set);
	RB_INIT(&rs->aspa);
	TAILQ_INIT(&rs->timers);

	strlcpy(rs->descr, conf->descr, sizeof(rs->descr));
	rs->id = id;
	rs->session_id = -1;
	rs->min_version = conf->min_version;	/* must be set before version */
	rs->version = rtr_max_session_version(rs);
	rs->prev_version = rtr_max_session_version(rs);
	rs->refresh = RTR_DEFAULT_REFRESH;
	rs->retry = RTR_DEFAULT_RETRY;
	rs->expire = RTR_DEFAULT_EXPIRE;
	rs->active = RTR_DEFAULT_ACTIVE;
	rs->state = RTR_STATE_CLOSED;
	rs->reconf_action = RECONF_REINIT;
	rs->last_recv_error = NO_ERROR;
	rs->last_sent_error = NO_ERROR;

	/* make sure that some timer is running to abort bad sessions */
	timer_set(&rs->timers, Timer_Rtr_Expire, rs->expire);

	log_debug("rtr %s: new session, start", log_rtr(rs));
	TAILQ_INSERT_TAIL(&rtrs, rs, entry);
	rtr_fsm(rs, RTR_EVNT_START);

	return rs;
}

struct rtr_session *
rtr_get(uint32_t id)
{
	struct rtr_session *rs;

	TAILQ_FOREACH(rs, &rtrs, entry)
		if (rs->id == id)
			return rs;
	return NULL;
}

void
rtr_free(struct rtr_session *rs)
{
	if (rs == NULL)
		return;

	rtr_reset_cache(rs);
	rtr_fsm(rs, RTR_EVNT_CON_CLOSE);
	timer_remove_all(&rs->timers);
	msgbuf_free(rs->w);
	free(rs);
}

void
rtr_open(struct rtr_session *rs, int fd)
{
	if (rs->state != RTR_STATE_CLOSED &&
	    rs->state != RTR_STATE_NEGOTIATION) {
		log_warnx("rtr %s: bad session state", log_rtr(rs));
		rtr_fsm(rs, RTR_EVNT_CON_CLOSE);
	}

	if (rs->state == RTR_STATE_CLOSED) {
		rs->version = rtr_max_session_version(rs);
		rs->prev_version = rtr_max_session_version(rs);
	}

	rs->fd = fd;
	rtr_fsm(rs, RTR_EVNT_CON_OPEN);
}

void
rtr_config_prep(void)
{
	struct rtr_session *rs;

	TAILQ_FOREACH(rs, &rtrs, entry)
		rs->reconf_action = RECONF_DELETE;
}

void
rtr_config_merge(void)
{
	struct rtr_session *rs, *nrs;

	TAILQ_FOREACH_SAFE(rs, &rtrs, entry, nrs)
		if (rs->reconf_action == RECONF_DELETE) {
			TAILQ_REMOVE(&rtrs, rs, entry);
			rtr_free(rs);
		}
}

void
rtr_config_keep(struct rtr_session *rs, struct rtr_config_msg *conf)
{
	strlcpy(rs->descr, conf->descr, sizeof(rs->descr));
	rs->min_version = conf->min_version;
	rs->reconf_action = RECONF_KEEP;
}

void
rtr_roa_merge(struct roa_tree *rt)
{
	struct rtr_session *rs;
	struct roa *roa;

	TAILQ_FOREACH(rs, &rtrs, entry) {
		RB_FOREACH(roa, roa_tree, &rs->roa_set)
			rtr_roa_insert(rt, roa);
	}
}

void
rtr_aspa_merge(struct aspa_tree *at)
{
	struct rtr_session *rs;
	struct aspa_set *aspa;

	TAILQ_FOREACH(rs, &rtrs, entry) {
		RB_FOREACH(aspa, aspa_tree, &rs->aspa)
			rtr_aspa_insert(at, aspa);
	}
}

void
rtr_shutdown(void)
{
	struct rtr_session *rs, *nrs;

	TAILQ_FOREACH_SAFE(rs, &rtrs, entry, nrs)
		rtr_free(rs);
}

void
rtr_show(struct rtr_session *rs, pid_t pid)
{
	struct ctl_show_rtr msg;
	struct ctl_timer ct;
	u_int i;
	monotime_t d;

	memset(&msg, 0, sizeof(msg));

	/* descr, remote_addr, local_addr and remote_port set by parent */
	msg.version = rs->version;
	msg.min_version = rs->min_version;
	msg.serial = rs->serial;
	msg.refresh = rs->refresh;
	msg.retry = rs->retry;
	msg.expire = rs->expire;
	msg.session_id = rs->session_id;
	msg.last_sent_error = rs->last_sent_error;
	msg.last_recv_error = rs->last_recv_error;
	strlcpy(msg.state, rtr_statenames[rs->state], sizeof(msg.state));
	strlcpy(msg.last_sent_msg, rs->last_sent_msg,
	    sizeof(msg.last_sent_msg));
	strlcpy(msg.last_recv_msg, rs->last_recv_msg,
	    sizeof(msg.last_recv_msg));

	/* send back imsg */
	rtr_imsg_compose(IMSG_CTL_SHOW_RTR, rs->id, pid, &msg, sizeof(msg));

	/* send back timer imsgs */
	for (i = 1; i < Timer_Max; i++) {
		if (!timer_running(&rs->timers, i, &d))
			continue;
		ct.type = i;
		ct.val = d;
		rtr_imsg_compose(IMSG_CTL_SHOW_TIMER, rs->id, pid,
		    &ct, sizeof(ct));
	}
}
