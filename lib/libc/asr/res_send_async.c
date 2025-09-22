/*	$OpenBSD: res_send_async.c,v 1.41 2022/06/20 06:45:31 jca Exp $	*/
/*
 * Copyright (c) 2012 Eric Faurot <eric@openbsd.org>
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
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <netdb.h>

#include <asr.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <resolv.h> /* for res_random */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asr_private.h"

#define OP_QUERY    (0)

static int res_send_async_run(struct asr_query *, struct asr_result *);
static int sockaddr_connect(const struct sockaddr *, int);
static int udp_send(struct asr_query *);
static int udp_recv(struct asr_query *);
static int tcp_write(struct asr_query *);
static int tcp_read(struct asr_query *);
static int validate_packet(struct asr_query *);
static void clear_ad(struct asr_result *);
static int setup_query(struct asr_query *, const char *, const char *, int, int);
static int ensure_ibuf(struct asr_query *, size_t);
static int iter_ns(struct asr_query *);

#define AS_NS_SA(p) ((p)->as_ctx->ac_ns[(p)->as.dns.nsidx - 1])


struct asr_query *
res_send_async(const unsigned char *buf, int buflen, void *asr)
{
	struct asr_ctx		*ac;
	struct asr_query	*as;
	struct asr_unpack	 p;
	struct asr_dns_header	 h;
	struct asr_dns_query	 q;

	DPRINT_PACKET("asr: res_send_async()", buf, buflen);

	ac = _asr_use_resolver(asr);
	if ((as = _asr_async_new(ac, ASR_SEND)) == NULL) {
		_asr_ctx_unref(ac);
		return (NULL); /* errno set */
	}
	as->as_run = res_send_async_run;

	as->as_flags |= ASYNC_EXTOBUF;
	as->as.dns.obuf = (unsigned char *)buf;
	as->as.dns.obuflen = buflen;
	as->as.dns.obufsize = buflen;

	_asr_unpack_init(&p, buf, buflen);
	_asr_unpack_header(&p, &h);
	_asr_unpack_query(&p, &q);
	if (p.err) {
		errno = EINVAL;
		goto err;
	}
	as->as.dns.reqid = h.id;
	as->as.dns.type = q.q_type;
	as->as.dns.class = q.q_class;
	as->as.dns.dname = strdup(q.q_dname);
	if (as->as.dns.dname == NULL)
		goto err; /* errno set */

	_asr_ctx_unref(ac);
	return (as);
    err:
	if (as)
		_asr_async_free(as);
	_asr_ctx_unref(ac);
	return (NULL);
}
DEF_WEAK(res_send_async);

/*
 * Unlike res_query(), this version will actually return the packet
 * if it has received a valid one (errno == 0) even if h_errno is
 * not NETDB_SUCCESS. So the packet *must* be freed if necessary.
 */
struct asr_query *
res_query_async(const char *name, int class, int type, void *asr)
{
	struct asr_ctx	 *ac;
	struct asr_query *as;

	DPRINT("asr: res_query_async(\"%s\", %i, %i)\n", name, class, type);

	ac = _asr_use_resolver(asr);
	as = _res_query_async_ctx(name, class, type, ac);
	_asr_ctx_unref(ac);

	return (as);
}
DEF_WEAK(res_query_async);

struct asr_query *
_res_query_async_ctx(const char *name, int class, int type, struct asr_ctx *a_ctx)
{
	struct asr_query	*as;

	DPRINT("asr: res_query_async_ctx(\"%s\", %i, %i)\n", name, class, type);

	if ((as = _asr_async_new(a_ctx, ASR_SEND)) == NULL)
		return (NULL); /* errno set */
	as->as_run = res_send_async_run;

	/* This adds a "." to name if it doesn't already have one.
	 * That's how res_query() behaves (through res_mkquery).
	 */
	if (setup_query(as, name, NULL, class, type) == -1)
		goto err; /* errno set */

	return (as);

    err:
	if (as)
		_asr_async_free(as);

	return (NULL);
}

static int
res_send_async_run(struct asr_query *as, struct asr_result *ar)
{
    next:
	switch (as->as_state) {

	case ASR_STATE_INIT:

		if (as->as_ctx->ac_nscount == 0) {
			ar->ar_errno = ECONNREFUSED;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		async_set_state(as, ASR_STATE_NEXT_NS);
		break;

	case ASR_STATE_NEXT_NS:

		if (iter_ns(as) == -1) {
			ar->ar_errno = ETIMEDOUT;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		if (as->as_ctx->ac_options & RES_USEVC ||
		    as->as.dns.obuflen > PACKETSZ)
			async_set_state(as, ASR_STATE_TCP_WRITE);
		else
			async_set_state(as, ASR_STATE_UDP_SEND);
		break;

	case ASR_STATE_UDP_SEND:

		if (udp_send(as) == -1) {
			async_set_state(as, ASR_STATE_NEXT_NS);
			break;
		}
		async_set_state(as, ASR_STATE_UDP_RECV);
		ar->ar_cond = ASR_WANT_READ;
		ar->ar_fd = as->as_fd;
		ar->ar_timeout = as->as_timeout;
		return (ASYNC_COND);
		break;

	case ASR_STATE_UDP_RECV:

		if (udp_recv(as) == -1) {
			if (errno == ENOMEM) {
				ar->ar_errno = errno;
				async_set_state(as, ASR_STATE_HALT);
				break;
			}
			if (errno != EOVERFLOW) {
				/* Fail or timeout */
				async_set_state(as, ASR_STATE_NEXT_NS);
				break;
			}
			if (as->as_ctx->ac_options & RES_IGNTC)
				async_set_state(as, ASR_STATE_PACKET);
			else
				async_set_state(as, ASR_STATE_TCP_WRITE);
		} else
			async_set_state(as, ASR_STATE_PACKET);
		break;

	case ASR_STATE_TCP_WRITE:

		switch (tcp_write(as)) {
		case -1: /* fail or timeout */
			async_set_state(as, ASR_STATE_NEXT_NS);
			break;
		case 0:
			async_set_state(as, ASR_STATE_TCP_READ);
			ar->ar_cond = ASR_WANT_READ;
			ar->ar_fd = as->as_fd;
			ar->ar_timeout = as->as_timeout;
			return (ASYNC_COND);
		case 1:
			ar->ar_cond = ASR_WANT_WRITE;
			ar->ar_fd = as->as_fd;
			ar->ar_timeout = as->as_timeout;
			return (ASYNC_COND);
		}
		break;

	case ASR_STATE_TCP_READ:

		switch (tcp_read(as)) {
		case -1: /* Fail or timeout */
			if (errno == ENOMEM) {
				ar->ar_errno = errno;
				async_set_state(as, ASR_STATE_HALT);
			} else
				async_set_state(as, ASR_STATE_NEXT_NS);
			break;
		case 0:
			async_set_state(as, ASR_STATE_PACKET);
			break;
		case 1:
			ar->ar_cond = ASR_WANT_READ;
			ar->ar_fd = as->as_fd;
			ar->ar_timeout = as->as_timeout;
			return (ASYNC_COND);
		}
		break;

	case ASR_STATE_PACKET:

		memmove(&ar->ar_ns, AS_NS_SA(as), AS_NS_SA(as)->sa_len);
		ar->ar_datalen = as->as.dns.ibuflen;
		ar->ar_data = as->as.dns.ibuf;
		as->as.dns.ibuf = NULL;
		ar->ar_errno = 0;
		ar->ar_rcode = as->as.dns.rcode;
		if (!(as->as_ctx->ac_options & RES_TRUSTAD))
			clear_ad(ar);
		async_set_state(as, ASR_STATE_HALT);
		break;

	case ASR_STATE_HALT:

		if (ar->ar_errno) {
			ar->ar_h_errno = TRY_AGAIN;
			ar->ar_count = 0;
			ar->ar_datalen = -1;
			ar->ar_data = NULL;
		} else if (as->as.dns.ancount) {
			ar->ar_h_errno = NETDB_SUCCESS;
			ar->ar_count = as->as.dns.ancount;
		} else {
			ar->ar_count = 0;
			switch (as->as.dns.rcode) {
			case NXDOMAIN:
				ar->ar_h_errno = HOST_NOT_FOUND;
				break;
			case SERVFAIL:
				ar->ar_h_errno = TRY_AGAIN;
				break;
			case NOERROR:
				ar->ar_h_errno = NO_DATA;
				break;
			default:
				ar->ar_h_errno = NO_RECOVERY;
			}
		}
		return (ASYNC_DONE);

	default:

		ar->ar_errno = EOPNOTSUPP;
		ar->ar_h_errno = NETDB_INTERNAL;
		async_set_state(as, ASR_STATE_HALT);
		break;
	}
	goto next;
}

static int
sockaddr_connect(const struct sockaddr *sa, int socktype)
{
	int errno_save, sock;

	if ((sock = socket(sa->sa_family,
	    socktype | SOCK_NONBLOCK | SOCK_DNS, 0)) == -1)
		goto fail;

	if (connect(sock, sa, sa->sa_len) == -1) {
		/*
		 * In the TCP case, the caller will be asked to poll for
		 * POLLOUT so that we start writing the packet in tcp_write()
		 * when the connection is established, or fail there on error.
		 */
		if (errno == EINPROGRESS)
			return (sock);
		goto fail;
	}

	return (sock);

    fail:

	if (sock != -1) {
		errno_save = errno;
		close(sock);
		errno = errno_save;
	}

	return (-1);
}

/*
 * Prepare the DNS packet for the query type "type", class "class" and domain
 * name created by the concatenation on "name" and "dom".
 * Return 0 on success, set errno and return -1 on error.
 */
static int
setup_query(struct asr_query *as, const char *name, const char *dom,
	int class, int type)
{
	struct asr_pack		p;
	struct asr_dns_header	h;
	char			fqdn[MAXDNAME];
	char			dname[MAXDNAME];

	if (as->as_flags & ASYNC_EXTOBUF) {
		errno = EINVAL;
		DPRINT("attempting to write in user packet");
		return (-1);
	}

	if (_asr_make_fqdn(name, dom, fqdn, sizeof(fqdn)) > sizeof(fqdn)) {
		errno = EINVAL;
		DPRINT("asr_make_fqdn: name too long\n");
		return (-1);
	}

	if (_asr_dname_from_fqdn(fqdn, dname, sizeof(dname)) == -1) {
		errno = EINVAL;
		DPRINT("asr_dname_from_fqdn: invalid\n");
		return (-1);
	}

	if (as->as.dns.obuf == NULL) {
		as->as.dns.obufsize = PACKETSZ;
		as->as.dns.obuf = malloc(as->as.dns.obufsize);
		if (as->as.dns.obuf == NULL)
			return (-1); /* errno set */
	}
	as->as.dns.obuflen = 0;

	memset(&h, 0, sizeof h);
	h.id = res_randomid();
	if (as->as_ctx->ac_options & RES_RECURSE)
		h.flags |= RD_MASK;
	if (as->as_ctx->ac_options & RES_USE_CD)
		h.flags |= CD_MASK;
	if (as->as_ctx->ac_options & RES_TRUSTAD)
		h.flags |= AD_MASK;

	h.qdcount = 1;
	if (as->as_ctx->ac_options & (RES_USE_EDNS0 | RES_USE_DNSSEC))
		h.arcount = 1;

	_asr_pack_init(&p, as->as.dns.obuf, as->as.dns.obufsize);
	_asr_pack_header(&p, &h);
	_asr_pack_query(&p, type, class, dname);
	if (as->as_ctx->ac_options & (RES_USE_EDNS0 | RES_USE_DNSSEC))
		_asr_pack_edns0(&p, MAXPACKETSZ,
		    as->as_ctx->ac_options & RES_USE_DNSSEC);
	if (p.err) {
		DPRINT("error packing query");
		errno = EINVAL;
		return (-1);
	}

	/* Remember the parameters. */
	as->as.dns.reqid = h.id;
	as->as.dns.type = type;
	as->as.dns.class = class;
	if (as->as.dns.dname)
		free(as->as.dns.dname);
	as->as.dns.dname = strdup(dname);
	if (as->as.dns.dname == NULL) {
		DPRINT("strdup");
		return (-1); /* errno set */
	}
	as->as.dns.obuflen = p.offset;

	DPRINT_PACKET("asr_setup_query", as->as.dns.obuf, as->as.dns.obuflen);

	return (0);
}

/*
 * Create a connect UDP socket and send the output packet.
 *
 * Return 0 on success, or -1 on error (errno set).
 */
static int
udp_send(struct asr_query *as)
{
	ssize_t	n;
	int	save_errno;
#ifdef DEBUG
	char		buf[256];
#endif

	DPRINT("asr: [%p] connecting to %s UDP\n", as,
	    _asr_print_sockaddr(AS_NS_SA(as), buf, sizeof buf));

	as->as_fd = sockaddr_connect(AS_NS_SA(as), SOCK_DGRAM);
	if (as->as_fd == -1)
		return (-1); /* errno set */

	n = send(as->as_fd, as->as.dns.obuf, as->as.dns.obuflen, 0);
	if (n == -1) {
		save_errno = errno;
		close(as->as_fd);
		errno = save_errno;
		as->as_fd = -1;
		return (-1);
	}

	return (0);
}

/*
 * Try to receive a valid packet from the current UDP socket.
 *
 * Return 0 if a full packet could be read, or -1 on error (errno set).
 */
static int
udp_recv(struct asr_query *as)
{
	ssize_t		 n;
	int		 save_errno;

	if (ensure_ibuf(as, MAXPACKETSZ) == -1) {
		save_errno = errno;
		close(as->as_fd);
		errno = save_errno;
		as->as_fd = -1;
		return (-1);
	}

	n = recv(as->as_fd, as->as.dns.ibuf, as->as.dns.ibufsize, 0);
	save_errno = errno;
	close(as->as_fd);
	errno = save_errno;
	as->as_fd = -1;
	if (n == -1)
		return (-1);

	as->as.dns.ibuflen = n;

	DPRINT_PACKET("asr_udp_recv()", as->as.dns.ibuf, as->as.dns.ibuflen);

	if (validate_packet(as) == -1)
		return (-1); /* errno set */

	return (0);
}

/*
 * Write the output packet to the TCP socket.
 *
 * Return 0 when all bytes have been sent, 1 there is no buffer space on the
 * socket or it is not connected yet, or -1 on error (errno set).
 */
static int
tcp_write(struct asr_query *as)
{
	struct msghdr	msg;
	struct iovec	iov[2];
	uint16_t	len;
	ssize_t		n;
	size_t		offset;
	int		i;
#ifdef DEBUG
	char		buf[256];
#endif

	/* First try to connect if not already */
	if (as->as_fd == -1) {
		DPRINT("asr: [%p] connecting to %s TCP\n", as,
		    _asr_print_sockaddr(AS_NS_SA(as), buf, sizeof buf));
		as->as_fd = sockaddr_connect(AS_NS_SA(as), SOCK_STREAM);
		if (as->as_fd == -1)
			return (-1); /* errno set */
		as->as.dns.datalen = 0; /* bytes sent */
		return (1);
	}

	i = 0;

	/* Prepend de packet length if not sent already. */
	if (as->as.dns.datalen < sizeof(len)) {
		offset = 0;
		len = htons(as->as.dns.obuflen);
		iov[i].iov_base = (char *)(&len) + as->as.dns.datalen;
		iov[i].iov_len = sizeof(len) - as->as.dns.datalen;
		i++;
	} else
		offset = as->as.dns.datalen - sizeof(len);

	iov[i].iov_base = as->as.dns.obuf + offset;
	iov[i].iov_len = as->as.dns.obuflen - offset;
	i++;

	memset(&msg, 0, sizeof msg);
	msg.msg_iov = iov;
	msg.msg_iovlen = i;

    send_again:
	n = sendmsg(as->as_fd, &msg, MSG_NOSIGNAL);
	if (n == -1) {
		if (errno == EINTR)
			goto send_again;
		goto close; /* errno set */
	}

	as->as.dns.datalen += n;

	if (as->as.dns.datalen == as->as.dns.obuflen + sizeof(len)) {
		/* All sent. Prepare for TCP read */
		as->as.dns.datalen = 0;
		return (0);
	}

	/* More data to write */
	return (1);

close:
	close(as->as_fd);
	as->as_fd = -1;
	return (-1);
}

/*
 * Try to read a valid packet from the current TCP socket.
 *
 * Return 0 if a full packet could be read, 1 if more data is needed and the
 * socket must be read again, or -1 on error (errno set).
 */
static int
tcp_read(struct asr_query *as)
{
	ssize_t		 n;
	size_t		 offset, len;
	char		*pos;
	int		 save_errno, nfds;
	struct pollfd	 pfd;

	/* We must read the packet len first */
	if (as->as.dns.datalen < sizeof(as->as.dns.pktlen)) {

		pos = (char *)(&as->as.dns.pktlen) + as->as.dns.datalen;
		len = sizeof(as->as.dns.pktlen) - as->as.dns.datalen;

    read_again0:
		n = read(as->as_fd, pos, len);
		if (n == -1) {
			if (errno == EINTR)
				goto read_again0;
			goto close; /* errno set */
		}
		if (n == 0) {
			errno = ECONNRESET;
			goto close;
		}
		as->as.dns.datalen += n;
		if (as->as.dns.datalen < sizeof(as->as.dns.pktlen))
			return (1); /* need more data */

		as->as.dns.ibuflen = ntohs(as->as.dns.pktlen);
		if (ensure_ibuf(as, as->as.dns.ibuflen) == -1)
			goto close; /* errno set */

		pfd.fd = as->as_fd;
		pfd.events = POLLIN;
	    poll_again:
		nfds = poll(&pfd, 1, 0);
		if (nfds == -1) {
			if (errno == EINTR)
				goto poll_again;
			goto close; /* errno set */
		}
		if (nfds == 0)
			return (1); /* no more data available */
	}

	offset = as->as.dns.datalen - sizeof(as->as.dns.pktlen);
	pos = as->as.dns.ibuf + offset;
	len =  as->as.dns.ibuflen - offset;

    read_again:
	n = read(as->as_fd, pos, len);
	if (n == -1) {
		if (errno == EINTR)
			goto read_again;
		goto close; /* errno set */
	}
	if (n == 0) {
		errno = ECONNRESET;
		goto close;
	}
	as->as.dns.datalen += n;

	/* See if we got all the advertised bytes. */
	if (as->as.dns.datalen != as->as.dns.ibuflen + sizeof(as->as.dns.pktlen))
		return (1);

	DPRINT_PACKET("asr_tcp_read()", as->as.dns.ibuf, as->as.dns.ibuflen);

	if (validate_packet(as) == -1)
		goto close; /* errno set */

	errno = 0;
close:
	save_errno = errno;
	close(as->as_fd);
	errno = save_errno;
	as->as_fd = -1;
	return (errno ? -1 : 0);
}

/*
 * Make sure the input buffer is at least "n" bytes long, and allocate or
 * extend it if necessary. Return 0 on success, or set errno and return -1.
 */
static int
ensure_ibuf(struct asr_query *as, size_t n)
{
	char	*t;

	if (as->as.dns.ibufsize >= n)
		return (0);

	t = recallocarray(as->as.dns.ibuf, as->as.dns.ibufsize, n, 1);
	if (t == NULL)
		return (-1); /* errno set */
	as->as.dns.ibuf = t;
	as->as.dns.ibufsize = n;

	return (0);
}

/*
 * Check if the received packet is valid.
 * Return 0 on success, or set errno and return -1.
 */
static int
validate_packet(struct asr_query *as)
{
	struct asr_unpack	 p;
	struct asr_dns_header	 h;
	struct asr_dns_query	 q;
	struct asr_dns_rr	 rr;
	int			 r;

	_asr_unpack_init(&p, as->as.dns.ibuf, as->as.dns.ibuflen);

	_asr_unpack_header(&p, &h);
	if (p.err)
		goto inval;

	if (h.id != as->as.dns.reqid) {
		DPRINT("incorrect reqid\n");
		goto inval;
	}
	if (h.qdcount != 1)
		goto inval;
	/* Should be zero, we could allow this */
	if ((h.flags & Z_MASK) != 0)
		goto inval;
	/* Actually, it depends on the request but we only use OP_QUERY */
	if (OPCODE(h.flags) != OP_QUERY)
		goto inval;
	/* Must be a response */
	if ((h.flags & QR_MASK) == 0)
		goto inval;

	as->as.dns.rcode = RCODE(h.flags);
	as->as.dns.ancount = h.ancount;

	_asr_unpack_query(&p, &q);
	if (p.err)
		goto inval;

	if (q.q_type != as->as.dns.type ||
	    q.q_class != as->as.dns.class ||
	    strcasecmp(q.q_dname, as->as.dns.dname)) {
		DPRINT("incorrect type/class/dname '%s' != '%s'\n",
		    q.q_dname, as->as.dns.dname);
		goto inval;
	}

	/* Check for truncation */
	if (h.flags & TC_MASK && !(as->as_ctx->ac_options & RES_IGNTC)) {
		DPRINT("truncated\n");
		errno = EOVERFLOW;
		return (-1);
	}

	/* Validate the rest of the packet */
	for (r = h.ancount + h.nscount + h.arcount; r; r--)
		_asr_unpack_rr(&p, &rr);

	/* Report any error found when unpacking the RRs. */
	if (p.err) {
		DPRINT("unpack: %s\n", strerror(p.err));
		errno = p.err;
		return (-1);
	}

	if (p.offset != as->as.dns.ibuflen) {
		DPRINT("trailing garbage\n");
		errno = EMSGSIZE;
		return (-1);
	}

	return (0);

    inval:
	errno = EINVAL;
	return (-1);
}

/*
 * Clear AD flag in the answer.
 */
static void
clear_ad(struct asr_result *ar)
{
	struct asr_dns_header	*h;
	uint16_t		 flags;

	h = (struct asr_dns_header *)ar->ar_data;
	flags = ntohs(h->flags);
	flags &= ~(AD_MASK);
	h->flags = htons(flags);
}

/*
 * Set the async context nameserver index to the next nameserver, cycling
 * over the list until the maximum retry counter is reached.  Return 0 on
 * success, or -1 if all nameservers were used.
 */
static int
iter_ns(struct asr_query *as)
{
	for (;;) {
		if (as->as.dns.nsloop >= as->as_ctx->ac_nsretries)
			return (-1);

		as->as.dns.nsidx += 1;
		if (as->as.dns.nsidx <= as->as_ctx->ac_nscount)
			break;
		as->as.dns.nsidx = 0;
		as->as.dns.nsloop++;
		DPRINT("asr: iter_ns(): cycle %i\n", as->as.dns.nsloop);
	}

	as->as_timeout = 1000 * (as->as_ctx->ac_nstimeout << as->as.dns.nsloop);
	if (as->as.dns.nsloop > 0)
		as->as_timeout /= as->as_ctx->ac_nscount;
	if (as->as_timeout < 1000)
		as->as_timeout = 1000;

	return (0);
}
