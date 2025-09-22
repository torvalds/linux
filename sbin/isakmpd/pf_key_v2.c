/* $OpenBSD: pf_key_v2.c,v 1.205 2023/08/07 04:01:30 dlg Exp $  */
/* $EOM: pf_key_v2.c,v 1.79 2000/12/12 00:33:19 niklas Exp $	 */

/*
 * Copyright (c) 1999, 2000, 2001 Niklas Hallqvist.  All rights reserved.
 * Copyright (c) 1999, 2000, 2001 Angelos D. Keromytis.  All rights reserved.
 * Copyright (c) 2001 Håkan Olsson.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This code was written under funding by Ericsson Radio Systems.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <net/pfkeyv2.h>
#include <netinet/in.h>
#include <netinet/ip_ipsp.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <errno.h>
#include <bitstring.h>

#include "cert.h"
#include "conf.h"
#include "connection.h"
#include "exchange.h"
#include "ipsec.h"
#include "ipsec_num.h"
#include "key.h"
#include "log.h"
#include "pf_key_v2.h"
#include "sa.h"
#include "timer.h"
#include "transport.h"
#include "ui.h"
#include "util.h"

#include "policy.h"

#include "udp_encap.h"

#define IN6_IS_ADDR_FULL(a)						\
	((*(u_int32_t *)(void *)(&(a)->s6_addr[0]) == 0xffffffff) &&	\
	(*(u_int32_t *)(void *)(&(a)->s6_addr[4]) == 0xffffffff) &&	\
	(*(u_int32_t *)(void *)(&(a)->s6_addr[8]) == 0xffffffff) &&	\
	(*(u_int32_t *)(void *)(&(a)->s6_addr[12]) == 0xffffffff))

#define ADDRESS_MAX sizeof "ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255"

/*
 * PF_KEY v2 always work with 64-bit entities and aligns on 64-bit boundaries.
 */
#define PF_KEY_V2_CHUNK 8
#define PF_KEY_V2_ROUND(x)						\
	(((x) + PF_KEY_V2_CHUNK - 1) & ~(PF_KEY_V2_CHUNK - 1))

/* How many microseconds we will wait for a reply from the PF_KEY socket.  */
#define PF_KEY_REPLY_TIMEOUT 1000

struct pf_key_v2_node {
	TAILQ_ENTRY(pf_key_v2_node) link;
	void           *seg;
	size_t		sz;
	int		cnt;
	u_int16_t       type;
	u_int8_t	flags;
};

TAILQ_HEAD(pf_key_v2_msg, pf_key_v2_node);

#define PF_KEY_V2_NODE_MALLOCED 1
#define PF_KEY_V2_NODE_MARK 2

/* Used to derive "unique" connection identifiers. */
int		connection_seq = 0;

static u_int8_t *pf_key_v2_convert_id(u_int8_t *, int, size_t *, int *);
static struct pf_key_v2_msg *pf_key_v2_call(struct pf_key_v2_msg *);
static struct pf_key_v2_node *pf_key_v2_find_ext(struct pf_key_v2_msg *,
		    u_int16_t);
static void     pf_key_v2_notify(struct pf_key_v2_msg *);
static struct pf_key_v2_msg *pf_key_v2_read(u_int32_t);
static u_int32_t pf_key_v2_seq(void);
static u_int32_t pf_key_v2_write(struct pf_key_v2_msg *);
static int      pf_key_v2_remove_conf(char *);
static int      pf_key_v2_conf_refhandle(int, char *);

static int      pf_key_v2_conf_refinc(int, char *);

/* The socket to use for PF_KEY interactions.  */
int      pf_key_v2_socket;

static struct pf_key_v2_msg *
pf_key_v2_msg_new(struct sadb_msg *msg, int flags)
{
	struct pf_key_v2_node *node;
	struct pf_key_v2_msg *ret;

	node = malloc(sizeof *node);
	if (!node)
		goto cleanup;
	ret = malloc(sizeof *ret);
	if (!ret)
		goto cleanup;
	TAILQ_INIT(ret);
	node->seg = msg;
	node->sz = sizeof *msg;
	node->type = 0;
	node->cnt = 1;
	node->flags = flags;
	TAILQ_INSERT_HEAD(ret, node, link);
	return ret;

cleanup:
	free(node);
	return 0;
}

/* Add a SZ sized segment SEG to the PF_KEY message MSG.  */
static int
pf_key_v2_msg_add(struct pf_key_v2_msg *msg, struct sadb_ext *ext, int flags)
{
	struct pf_key_v2_node *node;

	node = malloc(sizeof *node);
	if (!node)
		return -1;
	node->seg = ext;
	node->sz = ext->sadb_ext_len * PF_KEY_V2_CHUNK;
	node->type = ext->sadb_ext_type;
	node->flags = flags;
	TAILQ_FIRST(msg)->cnt++;
	TAILQ_INSERT_TAIL(msg, node, link);
	return 0;
}

/* Deallocate the PF_KEY message MSG.  */
static void
pf_key_v2_msg_free(struct pf_key_v2_msg *msg)
{
	struct pf_key_v2_node *np;

	np = TAILQ_FIRST(msg);
	while (np) {
		TAILQ_REMOVE(msg, np, link);
		if (np->flags & PF_KEY_V2_NODE_MALLOCED)
			free(np->seg);
		free(np);
		np = TAILQ_FIRST(msg);
	}
	free(msg);
}

/* Just return a new sequence number.  */
static u_int32_t
pf_key_v2_seq(void)
{
	static u_int32_t seq = 0;

	return ++seq;
}

/*
 * Read a PF_KEY packet with SEQ as the sequence number, looping if necessary.
 * If SEQ is zero just read the first message we see, otherwise we queue
 * messages up until both the PID and the sequence number match.
 */
static struct pf_key_v2_msg *
pf_key_v2_read(u_int32_t seq)
{
	ssize_t		n;
	u_int8_t       *buf = 0;
	struct pf_key_v2_msg *ret = 0;
	struct sadb_msg *msg;
	struct sadb_msg hdr;
	struct sadb_ext *ext;
	struct timespec	ts;
	struct pollfd	pfd[1];

	pfd[0].fd = pf_key_v2_socket;
	pfd[0].events = POLLIN;

	while (1) {
		/*
		 * If this is a read of a reply we should actually expect the
		 * reply to get lost as PF_KEY is an unreliable service per
		 * the specs. Currently we do this by setting a short timeout,
		 * and if it is not readable in that time, we fail the read.
		 */
		if (seq) {
			n = poll(pfd, 1, PF_KEY_REPLY_TIMEOUT / 1000);
			if (n == -1) {
				log_error("pf_key_v2_read: poll() failed");
				goto cleanup;
			}
			if (!n) {
				log_print("pf_key_v2_read: "
				    "no reply from PF_KEY");
				goto cleanup;
			}
		}
		n = recv(pf_key_v2_socket, &hdr, sizeof hdr, MSG_PEEK);
		if (n == -1) {
			log_error("pf_key_v2_read: recv (%d, ...) failed",
			    pf_key_v2_socket);
			goto cleanup;
		}
		if (n != sizeof hdr) {
			log_error("pf_key_v2_read: recv (%d, ...) "
			    "returned short packet (%lu bytes)",
			    pf_key_v2_socket, (unsigned long) n);
			goto cleanup;
		}
		buf = reallocarray(NULL, hdr.sadb_msg_len, PF_KEY_V2_CHUNK);
		if (!buf) {
			log_error("pf_key_v2_read: reallocarray (%d, %d) failed",
			    hdr.sadb_msg_len, PF_KEY_V2_CHUNK);
			goto cleanup;
		}
		n = hdr.sadb_msg_len * PF_KEY_V2_CHUNK;

		n = read(pf_key_v2_socket, buf, n);
		if (n == -1) {
			log_error("pf_key_v2_read: read (%d, ...) failed",
				  pf_key_v2_socket);
			goto cleanup;
		}
		if (n != hdr.sadb_msg_len * PF_KEY_V2_CHUNK) {
			log_print("pf_key_v2_read: read (%d, ...) "
			    "returned short packet (%lu bytes)",
			    pf_key_v2_socket, (unsigned long) n);
			goto cleanup;
		}
		LOG_DBG_BUF((LOG_SYSDEP, 80, "pf_key_v2_read: msg", buf, n));

		/* We drop all messages that is not what we expect.  */
		msg = (struct sadb_msg *) buf;
		if (msg->sadb_msg_version != PF_KEY_V2 ||
		    (msg->sadb_msg_pid != 0 &&
		    msg->sadb_msg_pid != (u_int32_t) getpid())) {
			if (seq) {
				free(buf);
				buf = 0;
				continue;
			} else {
				LOG_DBG((LOG_SYSDEP, 90, "pf_key_v2_read:"
				    "bad version (%d) or PID (%d, mine is "
				    "%ld), ignored", msg->sadb_msg_version,
				    msg->sadb_msg_pid, (long) getpid()));
				goto cleanup;
			}
		}
		/* Parse the message.  */
		ret = pf_key_v2_msg_new(msg, PF_KEY_V2_NODE_MALLOCED);
		if (!ret)
			goto cleanup;
		buf = 0;
		for (ext = (struct sadb_ext *) (msg + 1);
		    (u_int8_t *) ext - (u_int8_t *) msg <
		    msg->sadb_msg_len * PF_KEY_V2_CHUNK;
		    ext = (struct sadb_ext *) ((u_int8_t *) ext +
		    ext->sadb_ext_len * PF_KEY_V2_CHUNK))
			pf_key_v2_msg_add(ret, ext, 0);

		/*
		 * If the message is not the one we are waiting for, queue it
		 * up.
		 */
		if (seq && (msg->sadb_msg_pid != (u_int32_t) getpid() ||
		    msg->sadb_msg_seq != seq)) {
			clock_gettime(CLOCK_MONOTONIC, &ts);
			timer_add_event("pf_key_v2_notify",
			    (void (*) (void *)) pf_key_v2_notify, ret, &ts);
			ret = 0;
			continue;
		}
		return ret;
	}

cleanup:
	free(buf);
	if (ret)
		pf_key_v2_msg_free(ret);
	return 0;
}

/* Write the message in PMSG to the PF_KEY socket.  */
u_int32_t
pf_key_v2_write(struct pf_key_v2_msg *pmsg)
{
	struct iovec   *iov = 0;
	ssize_t		n;
	size_t		len;
	int		i, cnt = TAILQ_FIRST(pmsg)->cnt;
	char		header[80];
	struct sadb_msg *msg = TAILQ_FIRST(pmsg)->seg;
	struct pf_key_v2_node *np = TAILQ_FIRST(pmsg);

	iov = calloc(cnt, sizeof *iov);
	if (!iov) {
		log_error("pf_key_v2_write: malloc (%lu) failed",
		    cnt * (unsigned long) sizeof *iov);
		return 0;
	}
	msg->sadb_msg_version = PF_KEY_V2;
	msg->sadb_msg_errno = 0;
	msg->sadb_msg_reserved = 0;
	msg->sadb_msg_pid = getpid();
	if (!msg->sadb_msg_seq)
		msg->sadb_msg_seq = pf_key_v2_seq();

	/* Compute the iovec segments as well as the message length.  */
	len = 0;
	for (i = 0; i < cnt; i++) {
		iov[i].iov_base = np->seg;
		len += iov[i].iov_len = np->sz;

		/*
		 * XXX One can envision setting specific extension fields,
		 * like *_reserved ones here.  For now we require them to be
		 * set by the caller.
		 */

		np = TAILQ_NEXT(np, link);
	}
	msg->sadb_msg_len = len / PF_KEY_V2_CHUNK;

	for (i = 0; i < cnt; i++) {
		snprintf(header, sizeof header, "pf_key_v2_write: iov[%d]", i);
		LOG_DBG_BUF((LOG_SYSDEP, 80, header,
		    (u_int8_t *) iov[i].iov_base, iov[i].iov_len));
	}

	do {
		n = writev(pf_key_v2_socket, iov, cnt);
	} while (n == -1 && (errno == EAGAIN || errno == EINTR));
	if (n == -1) {
		log_error("pf_key_v2_write: writev (%d, %p, %d) failed",
		    pf_key_v2_socket, iov, cnt);
		goto cleanup;
	}
	if ((size_t) n != len) {
		log_error("pf_key_v2_write: "
		    "writev (%d, ...) returned prematurely (%lu)",
		    pf_key_v2_socket, (unsigned long) n);
		goto cleanup;
	}
	free(iov);
	return msg->sadb_msg_seq;

cleanup:
	free(iov);
	return 0;
}

/*
 * Do a PF_KEY "call", i.e. write a message MSG, read the reply and return
 * it to the caller.
 */
static struct pf_key_v2_msg *
pf_key_v2_call(struct pf_key_v2_msg *msg)
{
	u_int32_t       seq;

	seq = pf_key_v2_write(msg);
	if (!seq)
		return 0;
	return pf_key_v2_read(seq);
}

/* Find the TYPE extension in MSG.  Return zero if none found.  */
static struct pf_key_v2_node *
pf_key_v2_find_ext(struct pf_key_v2_msg *msg, u_int16_t type)
{
	struct pf_key_v2_node *ext;

	for (ext = TAILQ_NEXT(TAILQ_FIRST(msg), link); ext;
	    ext = TAILQ_NEXT(ext, link))
		if (ext->type == type)
			return ext;
	return 0;
}

/*
 * Open the PF_KEYv2 sockets and return the descriptor used for notifies.
 * Return -1 for failure and -2 if no notifies will show up.
 */
int
pf_key_v2_open(void)
{
	int		fd = -1, err;
	struct sadb_msg msg;
	struct pf_key_v2_msg *regmsg = 0, *ret = 0;

	/* Open the socket we use to speak to IPsec. */
	pf_key_v2_socket = -1;
	fd = socket(PF_KEY, SOCK_RAW, PF_KEY_V2);
	if (fd == -1) {
		log_error("pf_key_v2_open: "
		    "socket (PF_KEY, SOCK_RAW, PF_KEY_V2) failed");
		goto cleanup;
	}
	pf_key_v2_socket = fd;

	/* Register it to get ESP and AH acquires from the kernel.  */
	msg.sadb_msg_seq = 0;
	msg.sadb_msg_type = SADB_REGISTER;
	msg.sadb_msg_satype = SADB_SATYPE_ESP;
	regmsg = pf_key_v2_msg_new(&msg, 0);
	if (!regmsg)
		goto cleanup;
	ret = pf_key_v2_call(regmsg);
	pf_key_v2_msg_free(regmsg);
	if (!ret)
		goto cleanup;
	err = ((struct sadb_msg *)TAILQ_FIRST(ret)->seg)->sadb_msg_errno;
	if (err) {
		log_print("pf_key_v2_open: REGISTER: %s", strerror(err));
		goto cleanup;
	}
	/* XXX Register the accepted transforms.  */

	pf_key_v2_msg_free(ret);
	ret = 0;

	msg.sadb_msg_seq = 0;
	msg.sadb_msg_type = SADB_REGISTER;
	msg.sadb_msg_satype = SADB_SATYPE_AH;
	regmsg = pf_key_v2_msg_new(&msg, 0);
	if (!regmsg)
		goto cleanup;
	ret = pf_key_v2_call(regmsg);
	pf_key_v2_msg_free(regmsg);
	if (!ret)
		goto cleanup;
	err = ((struct sadb_msg *)TAILQ_FIRST(ret)->seg)->sadb_msg_errno;
	if (err) {
		log_print("pf_key_v2_open: REGISTER: %s", strerror(err));
		goto cleanup;
	}
	/* XXX Register the accepted transforms.  */

	pf_key_v2_msg_free(ret);
	ret = 0;

	msg.sadb_msg_seq = 0;
	msg.sadb_msg_type = SADB_REGISTER;
	msg.sadb_msg_satype = SADB_X_SATYPE_IPCOMP;
	regmsg = pf_key_v2_msg_new(&msg, 0);
	if (!regmsg)
		goto cleanup;
	ret = pf_key_v2_call(regmsg);
	pf_key_v2_msg_free(regmsg);
	if (!ret)
		goto cleanup;
	err = ((struct sadb_msg *)TAILQ_FIRST(ret)->seg)->sadb_msg_errno;
	if (err) {
		log_print("pf_key_v2_open: REGISTER: %s", strerror(err));
		goto cleanup;
	}
	/* XXX Register the accepted transforms.  */

	pf_key_v2_msg_free(ret);

	return fd;

cleanup:
	if (pf_key_v2_socket != -1) {
		close(pf_key_v2_socket);
		pf_key_v2_socket = -1;
	}
	if (ret)
		pf_key_v2_msg_free(ret);
	return -1;
}

/*
 * Generate a SPI for protocol PROTO and the source/destination pair given by
 * SRC, SRCLEN, DST & DSTLEN.  Stash the SPI size in SZ.
 */
u_int8_t *
pf_key_v2_get_spi(size_t *sz, u_int8_t proto, struct sockaddr *src,
    struct sockaddr *dst, u_int32_t seq)
{
	struct sadb_msg msg;
	struct sadb_sa *sa;
	struct sadb_address *addr = 0;
	struct sadb_spirange spirange;
	struct pf_key_v2_msg *getspi = 0, *ret = 0;
	struct pf_key_v2_node *ext;
	u_int8_t       *spi = 0;
	int		len, err;

	msg.sadb_msg_type = SADB_GETSPI;
	switch (proto) {
	case IPSEC_PROTO_IPSEC_ESP:
		msg.sadb_msg_satype = SADB_SATYPE_ESP;
		break;
	case IPSEC_PROTO_IPSEC_AH:
		msg.sadb_msg_satype = SADB_SATYPE_AH;
		break;
	case IPSEC_PROTO_IPCOMP:
		msg.sadb_msg_satype = SADB_X_SATYPE_IPCOMP;
		break;
	default:
		log_print("pf_key_v2_get_spi: invalid proto %d", proto);
		goto cleanup;
	}

	/* Set the sequence number from the ACQUIRE message. */
	msg.sadb_msg_seq = seq;
	getspi = pf_key_v2_msg_new(&msg, 0);
	if (!getspi)
		goto cleanup;

	/* Setup the ADDRESS extensions.  */
	len =
	    sizeof(struct sadb_address) + PF_KEY_V2_ROUND(SA_LEN(src));
	addr = calloc(1, len);
	if (!addr)
		goto cleanup;
	addr->sadb_address_exttype = SADB_EXT_ADDRESS_SRC;
	addr->sadb_address_len = len / PF_KEY_V2_CHUNK;
	addr->sadb_address_reserved = 0;
	memcpy(addr + 1, src, SA_LEN(src));
	switch (((struct sockaddr *) (addr + 1))->sa_family) {
	case AF_INET:
		((struct sockaddr_in *) (addr + 1))->sin_port = 0;
		break;
	case AF_INET6:
		((struct sockaddr_in6 *) (addr + 1))->sin6_port = 0;
		break;
	}
	if (pf_key_v2_msg_add(getspi, (struct sadb_ext *) addr,
	    PF_KEY_V2_NODE_MALLOCED) == -1)
		goto cleanup;
	addr = 0;

	len = sizeof(struct sadb_address) + PF_KEY_V2_ROUND(SA_LEN(dst));
	addr = calloc(1, len);
	if (!addr)
		goto cleanup;
	addr->sadb_address_exttype = SADB_EXT_ADDRESS_DST;
	addr->sadb_address_len = len / PF_KEY_V2_CHUNK;
	addr->sadb_address_reserved = 0;
	memcpy(addr + 1, dst, SA_LEN(dst));
	switch (((struct sockaddr *) (addr + 1))->sa_family) {
	case AF_INET:
		((struct sockaddr_in *) (addr + 1))->sin_port = 0;
		break;
	case AF_INET6:
		((struct sockaddr_in6 *) (addr + 1))->sin6_port = 0;
		break;
	}
	if (pf_key_v2_msg_add(getspi, (struct sadb_ext *) addr,
	    PF_KEY_V2_NODE_MALLOCED) == -1)
		goto cleanup;
	addr = 0;

	/* Setup the SPIRANGE extension.  */
	spirange.sadb_spirange_exttype = SADB_EXT_SPIRANGE;
	spirange.sadb_spirange_len = sizeof spirange / PF_KEY_V2_CHUNK;
	if (proto == IPSEC_PROTO_IPCOMP) {
		spirange.sadb_spirange_min = CPI_RESERVED_MAX + 1;
		spirange.sadb_spirange_max = CPI_PRIVATE_MIN - 1;
	} else {
		spirange.sadb_spirange_min = IPSEC_SPI_LOW;
		spirange.sadb_spirange_max = 0xffffffff;
	}
	spirange.sadb_spirange_reserved = 0;
	if (pf_key_v2_msg_add(getspi, (struct sadb_ext *)&spirange, 0) == -1)
		goto cleanup;

	ret = pf_key_v2_call(getspi);
	pf_key_v2_msg_free(getspi);
	getspi = 0;
	if (!ret)
		goto cleanup;
	err = ((struct sadb_msg *)TAILQ_FIRST(ret)->seg)->sadb_msg_errno;
	if (err) {
		log_print("pf_key_v2_get_spi: GETSPI: %s", strerror(err));
		goto cleanup;
	}
	ext = pf_key_v2_find_ext(ret, SADB_EXT_SA);
	if (!ext) {
		log_print("pf_key_v2_get_spi: no SA extension found");
		goto cleanup;
	}
	sa = ext->seg;

	/* IPCOMP CPIs are only 16 bits long.  */
	*sz = (proto == IPSEC_PROTO_IPCOMP) ? sizeof(u_int16_t)
		: sizeof sa->sadb_sa_spi;
	spi = malloc(*sz);
	if (!spi)
		goto cleanup;
	/* XXX This is ugly.  */
	if (proto == IPSEC_PROTO_IPCOMP) {
		u_int32_t       tspi = ntohl(sa->sadb_sa_spi);
		*(u_int16_t *) spi = htons((u_int16_t) tspi);
	} else
		memcpy(spi, &sa->sadb_sa_spi, *sz);

	pf_key_v2_msg_free(ret);

	LOG_DBG_BUF((LOG_SYSDEP, 50, "pf_key_v2_get_spi: spi", spi, *sz));
	return spi;

cleanup:
	free(spi);
	free(addr);
	if (getspi)
		pf_key_v2_msg_free(getspi);
	if (ret)
		pf_key_v2_msg_free(ret);
	return 0;
}

/* Fetch SA information from the kernel. XXX OpenBSD only?  */
struct sa_kinfo *
pf_key_v2_get_kernel_sa(u_int8_t *spi, size_t spi_sz, u_int8_t proto,
    struct sockaddr *dst)
{
	struct sadb_msg msg;
	struct sadb_sa *ssa;
	struct sadb_address *addr = 0;
	struct sockaddr *sa;
	struct sadb_lifetime *life;
	struct pf_key_v2_msg *gettdb = 0, *ret = 0;
	struct pf_key_v2_node *ext;
	static struct sa_kinfo ksa;
	struct sadb_x_udpencap *udpencap;
	int len, err;

	if (spi_sz != sizeof (ssa->sadb_sa_spi))
		return 0;

	msg.sadb_msg_type = SADB_GET;
	switch (proto) {
	case IPSEC_PROTO_IPSEC_ESP:
		msg.sadb_msg_satype = SADB_SATYPE_ESP;
		break;
	case IPSEC_PROTO_IPSEC_AH:
		msg.sadb_msg_satype = SADB_SATYPE_AH;
		break;
	case IPSEC_PROTO_IPCOMP:
		msg.sadb_msg_satype = SADB_X_SATYPE_IPCOMP;
		break;
	default:
		log_print("pf_key_v2_get_kernel_sa: invalid proto %d", proto);
		goto cleanup;
	}

	gettdb = pf_key_v2_msg_new(&msg, 0);
	if (!gettdb)
		goto cleanup;

	/* SPI */
	ssa = calloc(1, sizeof *ssa);
	if (!ssa) {
		log_print("pf_key_v2_get_kernel_sa: calloc(1, %lu) failed",
		    (unsigned long)sizeof *ssa);
		goto cleanup;
	}

	ssa->sadb_sa_exttype = SADB_EXT_SA;
	ssa->sadb_sa_len = sizeof *ssa / PF_KEY_V2_CHUNK;
	memcpy(&ssa->sadb_sa_spi, spi, sizeof ssa->sadb_sa_spi);
	ssa->sadb_sa_state = SADB_SASTATE_MATURE;
	if (pf_key_v2_msg_add(gettdb, (struct sadb_ext *)ssa,
	    PF_KEY_V2_NODE_MALLOCED) == -1)
		goto cleanup;
	ssa = 0;

	/* Address */
	len =
	    sizeof(struct sadb_address) + PF_KEY_V2_ROUND(SA_LEN(dst));
	addr = calloc(1, len);
	if (!addr)
		goto cleanup;
	addr->sadb_address_exttype = SADB_EXT_ADDRESS_DST;
	addr->sadb_address_len = len / PF_KEY_V2_CHUNK;
	addr->sadb_address_reserved = 0;
	memcpy(addr + 1, dst, SA_LEN(dst));
	switch (((struct sockaddr *) (addr + 1))->sa_family) {
	case AF_INET:
		((struct sockaddr_in *) (addr + 1))->sin_port = 0;
		break;
	case AF_INET6:
		((struct sockaddr_in6 *) (addr + 1))->sin6_port = 0;
		break;
	}
	if (pf_key_v2_msg_add(gettdb, (struct sadb_ext *)addr,
	    PF_KEY_V2_NODE_MALLOCED) == -1)
		goto cleanup;
	addr = 0;

	ret = pf_key_v2_call(gettdb);
	pf_key_v2_msg_free(gettdb);
	gettdb = 0;
	if (!ret)
		goto cleanup;
	err = ((struct sadb_msg *)TAILQ_FIRST(ret)->seg)->sadb_msg_errno;
	if (err) {
		log_print("pf_key_v2_get_kernel_sa: SADB_GET: %s",
		    strerror(err));
		goto cleanup;
	}

	/* Extract the data.  */
	bzero(&ksa, sizeof ksa);

	ext = pf_key_v2_find_ext(ret, SADB_EXT_SA);
	if (!ext)
		goto cleanup;

	ssa = (struct sadb_sa *)ext;
	ksa.spi = ssa->sadb_sa_spi;
	ksa.wnd = ssa->sadb_sa_replay;
	ksa.flags = ssa->sadb_sa_flags;

	ext = pf_key_v2_find_ext(ret, SADB_EXT_LIFETIME_CURRENT);
	if (ext) {
		life = (struct sadb_lifetime *)ext->seg;
		ksa.cur_allocations = life->sadb_lifetime_allocations;
		ksa.cur_bytes =	life->sadb_lifetime_bytes;
		ksa.first_use = life->sadb_lifetime_usetime;
		ksa.established = life->sadb_lifetime_addtime;
	}

	ext = pf_key_v2_find_ext(ret, SADB_EXT_LIFETIME_SOFT);
	if (ext) {
		life = (struct sadb_lifetime *)ext->seg;
		ksa.soft_allocations = life->sadb_lifetime_allocations;
		ksa.soft_bytes = life->sadb_lifetime_bytes;
		ksa.soft_timeout = life->sadb_lifetime_addtime;
		ksa.soft_first_use = life->sadb_lifetime_usetime;
	}

	ext = pf_key_v2_find_ext(ret, SADB_EXT_LIFETIME_HARD);
	if (ext) {
		life = (struct sadb_lifetime *)ext->seg;
		ksa.exp_allocations = life->sadb_lifetime_allocations;
		ksa.exp_bytes = life->sadb_lifetime_bytes;
		ksa.exp_timeout = life->sadb_lifetime_addtime;
		ksa.exp_first_use = life->sadb_lifetime_usetime;
	}

	ext = pf_key_v2_find_ext(ret, SADB_X_EXT_LIFETIME_LASTUSE);
	if (ext) {
		life = (struct sadb_lifetime *)ext->seg;
		ksa.last_used = life->sadb_lifetime_usetime;
	}

	ext = pf_key_v2_find_ext(ret, SADB_EXT_ADDRESS_SRC);
	if (ext) {
		sa = (struct sockaddr *)ext->seg;
		memcpy(&ksa.src, sa,
		    sa->sa_family == AF_INET ? sizeof(struct sockaddr_in) :
		    sizeof(struct sockaddr_in6));
	}

	ext = pf_key_v2_find_ext(ret, SADB_EXT_ADDRESS_DST);
	if (ext) {
		sa = (struct sockaddr *)ext->seg;
		memcpy(&ksa.dst, sa,
		    sa->sa_family == AF_INET ? sizeof(struct sockaddr_in) :
		    sizeof(struct sockaddr_in6));
	}

	ext = pf_key_v2_find_ext(ret, SADB_X_EXT_UDPENCAP);
	if (ext) {
		udpencap = (struct sadb_x_udpencap *)ext->seg;
		ksa.udpencap_port = udpencap->sadb_x_udpencap_port;
	}

	pf_key_v2_msg_free(ret);

	LOG_DBG_BUF((LOG_SYSDEP, 50, "pf_key_v2_get_kernel_sa: spi", spi,
	    spi_sz));

	return &ksa;

  cleanup:
	free(addr);
	if (gettdb)
		pf_key_v2_msg_free(gettdb);
	if (ret)
		pf_key_v2_msg_free(ret);
	return 0;
}

static void
pf_key_v2_setup_sockaddr(void *res, struct sockaddr *src,
    struct sockaddr *dst, in_port_t port, int ingress)
{
	struct sockaddr_in *ip4_sa;
	struct sockaddr_in6 *ip6_sa;
	u_int8_t       *p;

	switch (src->sa_family) {
	case AF_INET:
		ip4_sa = (struct sockaddr_in *) res;
		ip4_sa->sin_family = AF_INET;
		ip4_sa->sin_len = sizeof *ip4_sa;
		ip4_sa->sin_port = port;
		if (dst)
			p = (u_int8_t *) (ingress ?
			    &((struct sockaddr_in *)src)->sin_addr.s_addr :
			    &((struct sockaddr_in *)dst)->sin_addr.s_addr);
		else
			p = (u_int8_t *)&((struct sockaddr_in *)src)->sin_addr.s_addr;
		ip4_sa->sin_addr.s_addr = *((in_addr_t *) p);
		break;

	case AF_INET6:
		ip6_sa = (struct sockaddr_in6 *) res;
		ip6_sa->sin6_family = AF_INET6;
		ip6_sa->sin6_len = sizeof *ip6_sa;
		ip6_sa->sin6_port = port;
		if (dst)
			p = (u_int8_t *) (ingress ?
			    &((struct sockaddr_in6 *)src)->sin6_addr.s6_addr :
			    &((struct sockaddr_in6 *)dst)->sin6_addr.s6_addr);
		else
			p = (u_int8_t *)&((struct sockaddr_in6 *)src)->sin6_addr.s6_addr;
		memcpy(ip6_sa->sin6_addr.s6_addr, p, sizeof(struct in6_addr));
		break;

	default:
		log_print("pf_key_v2_setup_sockaddr: unknown family %d\n",
		    src->sa_family);
		break;
	}
}

/*
 * Store/update a PF_KEY_V2 security association with full information from the
 * IKE SA and PROTO into the kernel.  INCOMING is set if we are setting the
 * parameters for the incoming SA, and cleared otherwise.
 */
int
pf_key_v2_set_spi(struct sa *sa, struct proto *proto, int incoming,
    struct sa *isakmp_sa)
{
	struct sadb_msg msg;
	struct sadb_sa  ssa;
	struct sadb_x_tag *stag = NULL;
	struct sadb_lifetime *life = 0;
	struct sadb_address *addr = 0;
	struct sadb_key *key = 0;
	struct sadb_ident *sid = 0;
	struct sockaddr *src, *dst;
	struct pf_key_v2_msg *update = 0, *ret = 0;
	struct ipsec_proto *iproto = proto->data;
	size_t		len;
	int		keylen, hashlen, err;
	u_int8_t       *pp;
	int		idtype;
	struct ipsec_sa *isa = sa->data;
	struct sadb_protocol flowtype, tprotocol;
	struct sadb_x_udpencap udpencap;
	char           *addr_str, *s;
	char		iface_str[32];

	msg.sadb_msg_type = incoming ? SADB_UPDATE : SADB_ADD;
	switch (proto->proto) {
	case IPSEC_PROTO_IPSEC_ESP:
		msg.sadb_msg_satype = SADB_SATYPE_ESP;
		keylen = ipsec_esp_enckeylength(proto);
		hashlen = ipsec_esp_authkeylength(proto);

		switch (proto->id) {
		case IPSEC_ESP_3DES:
			ssa.sadb_sa_encrypt = SADB_EALG_3DESCBC;
			break;

		case IPSEC_ESP_AES:
			ssa.sadb_sa_encrypt = SADB_X_EALG_AES;
			break;

		case IPSEC_ESP_AES_CTR:
			ssa.sadb_sa_encrypt = SADB_X_EALG_AESCTR;
			break;

		case IPSEC_ESP_AES_GCM_16:
			ssa.sadb_sa_encrypt = SADB_X_EALG_AESGCM16;
			break;

		case IPSEC_ESP_AES_GMAC:
			ssa.sadb_sa_encrypt = SADB_X_EALG_AESGMAC;
			break;

		case IPSEC_ESP_CAST:
			ssa.sadb_sa_encrypt = SADB_X_EALG_CAST;
			break;

		case IPSEC_ESP_BLOWFISH:
			ssa.sadb_sa_encrypt = SADB_X_EALG_BLF;
			break;

		case IPSEC_ESP_NULL:
			ssa.sadb_sa_encrypt = SADB_EALG_NULL;
			break;

		default:
			LOG_DBG((LOG_SYSDEP, 50, "pf_key_v2_set_spi: "
			    "unknown encryption algorithm %d", proto->id));
			return -1;
		}

		switch (iproto->auth) {
		case IPSEC_AUTH_HMAC_MD5:
			ssa.sadb_sa_auth = SADB_AALG_MD5HMAC;
			break;

		case IPSEC_AUTH_HMAC_SHA:
			ssa.sadb_sa_auth = SADB_AALG_SHA1HMAC;
			break;

		case IPSEC_AUTH_HMAC_RIPEMD:
			ssa.sadb_sa_auth = SADB_X_AALG_RIPEMD160HMAC;
			break;

		case IPSEC_AUTH_HMAC_SHA2_256:
			ssa.sadb_sa_auth = SADB_X_AALG_SHA2_256;
			break;

		case IPSEC_AUTH_HMAC_SHA2_384:
			ssa.sadb_sa_auth = SADB_X_AALG_SHA2_384;
			break;

		case IPSEC_AUTH_HMAC_SHA2_512:
			ssa.sadb_sa_auth = SADB_X_AALG_SHA2_512;
			break;

		case IPSEC_AUTH_DES_MAC:
		case IPSEC_AUTH_KPDK:
			/* XXX We should be supporting KPDK */
			LOG_DBG((LOG_SYSDEP, 50, "pf_key_v2_set_spi: "
			    "unknown authentication algorithm %d",
			    iproto->auth));
			return -1;

		default:
			ssa.sadb_sa_auth = SADB_AALG_NONE;
		}
		break;

	case IPSEC_PROTO_IPSEC_AH:
		msg.sadb_msg_satype = SADB_SATYPE_AH;
		hashlen = ipsec_ah_keylength(proto);
		keylen = 0;

		ssa.sadb_sa_encrypt = SADB_EALG_NONE;
		switch (proto->id) {
		case IPSEC_AH_MD5:
			ssa.sadb_sa_auth = SADB_AALG_MD5HMAC;
			break;

		case IPSEC_AH_SHA:
			ssa.sadb_sa_auth = SADB_AALG_SHA1HMAC;
			break;

		case IPSEC_AH_RIPEMD:
			ssa.sadb_sa_auth = SADB_X_AALG_RIPEMD160HMAC;
			break;

		case IPSEC_AH_SHA2_256:
			ssa.sadb_sa_auth = SADB_X_AALG_SHA2_256;
			break;

		case IPSEC_AH_SHA2_384:
			ssa.sadb_sa_auth = SADB_X_AALG_SHA2_384;
			break;

		case IPSEC_AH_SHA2_512:
			ssa.sadb_sa_auth = SADB_X_AALG_SHA2_512;
			break;

		default:
			LOG_DBG((LOG_SYSDEP, 50, "pf_key_v2_set_spi: "
			    "unknown authentication algorithm %d", proto->id));
			goto cleanup;
		}
		break;

	case IPSEC_PROTO_IPCOMP:
		msg.sadb_msg_satype = SADB_X_SATYPE_IPCOMP;
		ssa.sadb_sa_auth = SADB_AALG_NONE;
		keylen = 0;
		hashlen = 0;

		/*
		 * Put compression algorithm type in the sadb_sa_encrypt
		 * field.
		 */
		switch (proto->id) {
		case IPSEC_IPCOMP_OUI:
			ssa.sadb_sa_encrypt = SADB_X_CALG_OUI;
			break;

		case IPSEC_IPCOMP_DEFLATE:
			ssa.sadb_sa_encrypt = SADB_X_CALG_DEFLATE;
			break;

		default:
			break;
		}
		break;

	default:
		log_print("pf_key_v2_set_spi: invalid proto %d", proto->proto);
		goto cleanup;
	}
	if (incoming)
		sa->transport->vtbl->get_src(sa->transport, &dst);
	else
		sa->transport->vtbl->get_dst(sa->transport, &dst);
	msg.sadb_msg_seq = sa->seq;
	update = pf_key_v2_msg_new(&msg, 0);
	if (!update)
		goto cleanup;

	/* Setup the rest of the SA extension.  */
	ssa.sadb_sa_exttype = SADB_EXT_SA;
	ssa.sadb_sa_len = sizeof ssa / PF_KEY_V2_CHUNK;
	if (proto->spi_sz[incoming] == 2)	/* IPCOMP uses 16bit CPIs.  */
		ssa.sadb_sa_spi = htonl(proto->spi[incoming][0] << 8 |
		    proto->spi[incoming][1]);
	else
		memcpy(&ssa.sadb_sa_spi, proto->spi[incoming],
		    sizeof ssa.sadb_sa_spi);
	ssa.sadb_sa_replay = conf_get_str("General", "Shared-SADB") ? 0 :
	    iproto->replay_window;
	ssa.sadb_sa_state = SADB_SASTATE_MATURE;
	ssa.sadb_sa_flags = 0;
	if (iproto->encap_mode == IPSEC_ENCAP_TUNNEL ||
	    iproto->encap_mode == IPSEC_ENCAP_UDP_ENCAP_TUNNEL ||
	    iproto->encap_mode == IPSEC_ENCAP_UDP_ENCAP_TUNNEL_DRAFT)
		ssa.sadb_sa_flags = SADB_X_SAFLAGS_TUNNEL;

	if (isakmp_sa->flags & SA_FLAG_NAT_T_ENABLE) {
		bzero(&udpencap, sizeof udpencap);
		ssa.sadb_sa_flags |= SADB_X_SAFLAGS_UDPENCAP;
		udpencap.sadb_x_udpencap_exttype = SADB_X_EXT_UDPENCAP;
		udpencap.sadb_x_udpencap_len =
		    sizeof udpencap / PF_KEY_V2_CHUNK;
		udpencap.sadb_x_udpencap_port = sockaddr_port(dst);
		if (pf_key_v2_msg_add(update, (struct sadb_ext *)&udpencap, 0)
		    == -1)
			goto cleanup;
	}

	if (pf_key_v2_msg_add(update, (struct sadb_ext *)&ssa, 0) == -1)
		goto cleanup;

	if (sa->seconds || sa->kilobytes) {
		/* Setup the hard limits.  */
		life = malloc(sizeof *life);
		if (!life)
			goto cleanup;
		life->sadb_lifetime_len = sizeof *life / PF_KEY_V2_CHUNK;
		life->sadb_lifetime_exttype = SADB_EXT_LIFETIME_HARD;
		life->sadb_lifetime_allocations = 0;
		life->sadb_lifetime_bytes = sa->kilobytes * 1024;
		/*
		 * XXX I am not sure which one is best in security respect.
		 * Maybe the RFCs actually mandate what a lifetime really is.
		 */
#if 0
		life->sadb_lifetime_addtime = 0;
		life->sadb_lifetime_usetime = sa->seconds;
#else
		life->sadb_lifetime_addtime = sa->seconds;
		life->sadb_lifetime_usetime = 0;
#endif
		if (pf_key_v2_msg_add(update, (struct sadb_ext *) life,
		    PF_KEY_V2_NODE_MALLOCED) == -1)
			goto cleanup;
		life = 0;

		/*
		 * Setup the soft limits, we use 90 % of the hard ones.
		 * XXX A configurable ratio would be better.
		 */
		life = malloc(sizeof *life);
		if (!life)
			goto cleanup;
		life->sadb_lifetime_len = sizeof *life / PF_KEY_V2_CHUNK;
		life->sadb_lifetime_exttype = SADB_EXT_LIFETIME_SOFT;
		life->sadb_lifetime_allocations = 0;
		life->sadb_lifetime_bytes = sa->kilobytes * 1024 * 9 / 10;
		/*
		 * XXX I am not sure which one is best in security respect.
		 * Maybe the RFCs actually mandate what a lifetime really is.
		 */
#if 0
		life->sadb_lifetime_addtime = 0;
		life->sadb_lifetime_usetime = sa->seconds * 9 / 10;
#else
		life->sadb_lifetime_addtime = sa->seconds * 9 / 10;
		life->sadb_lifetime_usetime = 0;
#endif
		if (pf_key_v2_msg_add(update, (struct sadb_ext *) life,
		    PF_KEY_V2_NODE_MALLOCED) == -1)
			goto cleanup;
		life = 0;
	}
	/*
	 * Setup the ADDRESS extensions.
	 */
	if (incoming)
		sa->transport->vtbl->get_dst(sa->transport, &src);
	else
		sa->transport->vtbl->get_src(sa->transport, &src);
	len = sizeof *addr + PF_KEY_V2_ROUND(SA_LEN(src));
	addr = calloc(1, len);
	if (!addr)
		goto cleanup;
	addr->sadb_address_exttype = SADB_EXT_ADDRESS_SRC;
	addr->sadb_address_len = len / PF_KEY_V2_CHUNK;
	addr->sadb_address_reserved = 0;
	memcpy(addr + 1, src, SA_LEN(src));
	switch (((struct sockaddr *) (addr + 1))->sa_family) {
	case AF_INET:
		((struct sockaddr_in *) (addr + 1))->sin_port = 0;
		break;
	case AF_INET6:
		((struct sockaddr_in6 *) (addr + 1))->sin6_port = 0;
		break;
	}
	if (pf_key_v2_msg_add(update, (struct sadb_ext *) addr,
	    PF_KEY_V2_NODE_MALLOCED) == -1)
		goto cleanup;
	addr = 0;

	len = sizeof *addr + PF_KEY_V2_ROUND(SA_LEN(dst));
	addr = calloc(1, len);
	if (!addr)
		goto cleanup;
	addr->sadb_address_exttype = SADB_EXT_ADDRESS_DST;
	addr->sadb_address_len = len / PF_KEY_V2_CHUNK;
	addr->sadb_address_reserved = 0;
	memcpy(addr + 1, dst, SA_LEN(dst));
	switch (((struct sockaddr *) (addr + 1))->sa_family) {
	case AF_INET:
		((struct sockaddr_in *) (addr + 1))->sin_port = 0;
		break;
	case AF_INET6:
		((struct sockaddr_in6 *) (addr + 1))->sin6_port = 0;
		break;
	}
	if (pf_key_v2_msg_add(update, (struct sadb_ext *) addr,
	    PF_KEY_V2_NODE_MALLOCED) == -1)
		goto cleanup;
	addr = 0;

	if (proto->proto != IPSEC_PROTO_IPCOMP) {
		/* Setup the KEY extensions.  */
		if (hashlen) {
			len = sizeof *key + PF_KEY_V2_ROUND(hashlen);
			key = malloc(len);
			if (!key)
				goto cleanup;
			key->sadb_key_exttype = SADB_EXT_KEY_AUTH;
			key->sadb_key_len = len / PF_KEY_V2_CHUNK;
			key->sadb_key_bits = hashlen * 8;
			key->sadb_key_reserved = 0;
			memcpy(key + 1,
			    iproto->keymat[incoming] +
			    (proto->proto ==
				IPSEC_PROTO_IPSEC_ESP ? keylen : 0),
			    hashlen);
			if (pf_key_v2_msg_add(update, (struct sadb_ext *) key,
			    PF_KEY_V2_NODE_MALLOCED) == -1)
				goto cleanup;
			key = 0;
		}
		if (keylen) {
			len = sizeof *key + PF_KEY_V2_ROUND(keylen);
			key = malloc(len);
			if (!key)
				goto cleanup;
			key->sadb_key_exttype = SADB_EXT_KEY_ENCRYPT;
			key->sadb_key_len = len / PF_KEY_V2_CHUNK;
			key->sadb_key_bits = keylen * 8;
			key->sadb_key_reserved = 0;
			memcpy(key + 1, iproto->keymat[incoming], keylen);
			if (pf_key_v2_msg_add(update, (struct sadb_ext *) key,
			    PF_KEY_V2_NODE_MALLOCED) == -1)
				goto cleanup;
			key = 0;
		}
	}
	/* Setup identity extensions. */
	if (isakmp_sa->id_i) {
		pp = pf_key_v2_convert_id(isakmp_sa->id_i, isakmp_sa->id_i_len,
		    &len, &idtype);
		if (!pp)
			goto nosid;

		sid = calloc(PF_KEY_V2_ROUND(len + 1) + sizeof *sid,
		    sizeof(u_int8_t));
		if (!sid) {
			free(pp);
			goto cleanup;
		}
		sid->sadb_ident_type = idtype;
		sid->sadb_ident_len = ((sizeof *sid) / PF_KEY_V2_CHUNK) +
		    PF_KEY_V2_ROUND(len + 1) / PF_KEY_V2_CHUNK;
		if ((isakmp_sa->initiator && !incoming) ||
		    (!isakmp_sa->initiator && incoming))
			sid->sadb_ident_exttype = SADB_EXT_IDENTITY_SRC;
		else
			sid->sadb_ident_exttype = SADB_EXT_IDENTITY_DST;

		memcpy(sid + 1, pp, len);
		free(pp);

		if (pf_key_v2_msg_add(update, (struct sadb_ext *) sid,
		    PF_KEY_V2_NODE_MALLOCED) == -1)
			goto cleanup;
		sid = 0;

nosid:
		free(sid);
		sid = 0;
	}
	if (isakmp_sa->id_r) {
		pp = pf_key_v2_convert_id(isakmp_sa->id_r, isakmp_sa->id_r_len,
		    &len, &idtype);
		if (!pp)
			goto nodid;

		sid = calloc(PF_KEY_V2_ROUND(len + 1) + sizeof *sid,
		    sizeof(u_int8_t));
		if (!sid) {
			free(pp);
			goto cleanup;
		}
		sid->sadb_ident_type = idtype;
		sid->sadb_ident_len = ((sizeof *sid) / PF_KEY_V2_CHUNK) +
		    PF_KEY_V2_ROUND(len + 1) / PF_KEY_V2_CHUNK;
		if ((isakmp_sa->initiator && !incoming) ||
		    (!isakmp_sa->initiator && incoming))
			sid->sadb_ident_exttype = SADB_EXT_IDENTITY_DST;
		else
			sid->sadb_ident_exttype = SADB_EXT_IDENTITY_SRC;

		memcpy(sid + 1, pp, len);
		free(pp);

		if (pf_key_v2_msg_add(update, (struct sadb_ext *) sid,
		    PF_KEY_V2_NODE_MALLOCED) == -1)
			goto cleanup;
		sid = 0;

nodid:
		free(sid);
		sid = 0;
	}

	/* Setup the flow type extension.  */
	bzero(&flowtype, sizeof flowtype);
	flowtype.sadb_protocol_exttype = SADB_X_EXT_FLOW_TYPE;
	flowtype.sadb_protocol_len = sizeof flowtype / PF_KEY_V2_CHUNK;
	flowtype.sadb_protocol_direction = incoming ?
	    IPSP_DIRECTION_IN : IPSP_DIRECTION_OUT;

	if (pf_key_v2_msg_add(update, (struct sadb_ext *)&flowtype, 0) == -1)
		goto cleanup;

	bzero(&tprotocol, sizeof tprotocol);
	tprotocol.sadb_protocol_exttype = SADB_X_EXT_PROTOCOL;
	tprotocol.sadb_protocol_len = sizeof tprotocol / PF_KEY_V2_CHUNK;
	tprotocol.sadb_protocol_proto = isa->tproto;

	if (pf_key_v2_msg_add(update, (struct sadb_ext *)&tprotocol,
	    0) == -1)
		goto cleanup;

	len = sizeof *addr + PF_KEY_V2_ROUND(SA_LEN(isa->src_net));
	addr = calloc(1, len);
	if (!addr)
		goto cleanup;
	addr->sadb_address_exttype = incoming ?
	    SADB_X_EXT_DST_FLOW : SADB_X_EXT_SRC_FLOW;
	addr->sadb_address_len = len / PF_KEY_V2_CHUNK;
	addr->sadb_address_reserved = 0;
	pf_key_v2_setup_sockaddr(addr + 1, isa->src_net, 0, isa->sport, 0);
	if (pf_key_v2_msg_add(update, (struct sadb_ext *) addr,
	    PF_KEY_V2_NODE_MALLOCED) == -1)
		goto cleanup;
	addr = 0;

	addr = calloc(1, len);
	if (!addr)
		goto cleanup;
	addr->sadb_address_exttype =
		incoming ? SADB_X_EXT_DST_MASK : SADB_X_EXT_SRC_MASK;
	addr->sadb_address_len = len / PF_KEY_V2_CHUNK;
	addr->sadb_address_reserved = 0;
	pf_key_v2_setup_sockaddr(addr + 1, isa->src_mask, 0,
	    isa->sport ? 0xffff : 0, 0);
	if (pf_key_v2_msg_add(update, (struct sadb_ext *) addr,
	    PF_KEY_V2_NODE_MALLOCED) == -1)
		goto cleanup;
	addr = 0;

	addr = calloc(1, len);
	if (!addr)
		goto cleanup;
	addr->sadb_address_exttype = incoming ?
	    SADB_X_EXT_SRC_FLOW : SADB_X_EXT_DST_FLOW;
	addr->sadb_address_len = len / PF_KEY_V2_CHUNK;
	addr->sadb_address_reserved = 0;
	pf_key_v2_setup_sockaddr(addr + 1, isa->dst_net, 0, isa->dport, 0);
	if (pf_key_v2_msg_add(update, (struct sadb_ext *) addr,
	    PF_KEY_V2_NODE_MALLOCED) == -1)
		goto cleanup;
	addr = 0;

	addr = calloc(1, len);
	if (!addr)
		goto cleanup;
	addr->sadb_address_exttype =
		incoming ? SADB_X_EXT_SRC_MASK : SADB_X_EXT_DST_MASK;
	addr->sadb_address_len = len / PF_KEY_V2_CHUNK;
	addr->sadb_address_reserved = 0;
	pf_key_v2_setup_sockaddr(addr + 1, isa->dst_mask, 0,
	    isa->dport ? 0xffff : 0, 0);
	if (pf_key_v2_msg_add(update, (struct sadb_ext *) addr,
	    PF_KEY_V2_NODE_MALLOCED) == -1)
		goto cleanup;
	addr = 0;

	/* Add a pf tag to matching packets of this SA. */
	if (sa->tag != NULL) {
		len = sizeof(*stag) + PF_KEY_V2_ROUND(strlen(sa->tag) + 1);
		if ((stag = calloc(1, len)) == NULL)
			goto cleanup;
		stag->sadb_x_tag_exttype = SADB_X_EXT_TAG;
		stag->sadb_x_tag_len = len / PF_KEY_V2_CHUNK;
		stag->sadb_x_tag_taglen = strlen(sa->tag) + 1;
		s = (char *)(stag + 1);
		strlcpy(s, sa->tag, stag->sadb_x_tag_taglen);
		if (pf_key_v2_msg_add(update, (struct sadb_ext *)stag,
		    PF_KEY_V2_NODE_MALLOCED) == -1)
			goto cleanup;
	}

	if (sa->flags & SA_FLAG_IFACE) {
		struct sadb_x_iface *siface;

		len = sizeof(*siface);
		siface = calloc(1, len);
		if (siface == NULL)
			goto cleanup;

		siface->sadb_x_iface_len = len / PF_KEY_V2_CHUNK;
		siface->sadb_x_iface_exttype = SADB_X_EXT_IFACE;
		siface->sadb_x_iface_unit = sa->iface;
		siface->sadb_x_iface_direction = incoming ?
		    IPSP_DIRECTION_IN : IPSP_DIRECTION_OUT;

		if (pf_key_v2_msg_add(update, (struct sadb_ext *)siface,
		    PF_KEY_V2_NODE_MALLOCED) == -1)
			goto cleanup;

		snprintf(iface_str, sizeof(iface_str), "iface %u", sa->iface);
	}

	/* XXX Here can sensitivity extensions be setup.  */

	if (sockaddr2text(dst, &addr_str, 0))
		addr_str = 0;

	LOG_DBG((LOG_SYSDEP, 10, "pf_key_v2_set_spi: "
	    "satype %d dst %s SPI 0x%x%s%s%s", msg.sadb_msg_satype,
	    addr_str ? addr_str : "unknown",
	    ntohl(ssa.sadb_sa_spi), sa->tag ? " tag " : "",
	    sa->tag ? sa->tag : "", iface_str));

	free(addr_str);

	/*
	 * Although PF_KEY knows about expirations, it is unreliable per the
	 * specs thus we need to do them inside isakmpd as well.
	 */
	if (sa->seconds)
		if (sa_setup_expirations(sa))
			goto cleanup;

	ret = pf_key_v2_call(update);
	pf_key_v2_msg_free(update);
	update = 0;
	if (!ret)
		goto cleanup;
	err = ((struct sadb_msg *)TAILQ_FIRST(ret)->seg)->sadb_msg_errno;
	pf_key_v2_msg_free(ret);
	ret = 0;

	/*
	 * If we are doing an addition into an SADB shared with our peer,
	 * errors here are to be expected as the peer will already have
	 * created the SA, and can thus be ignored.
	 */
	if (err && !(msg.sadb_msg_type == SADB_ADD &&
	    conf_get_str("General", "Shared-SADB"))) {
		log_print("pf_key_v2_set_spi: %s: %s",
		    msg.sadb_msg_type == SADB_ADD ? "ADD" : "UPDATE",
		    strerror(err));
		goto cleanup;
	}
	LOG_DBG((LOG_SYSDEP, 50, "pf_key_v2_set_spi: done"));

	return 0;

cleanup:
	free(sid);
	free(addr);
	free(life);
	free(key);
	if (update)
		pf_key_v2_msg_free(update);
	if (ret)
		pf_key_v2_msg_free(ret);
	return -1;
}

static __inline__ int
pf_key_v2_mask_to_bits(u_int32_t mask)
{
	u_int32_t       hmask = ntohl(mask);

	return (33 - ffs(~hmask + 1)) % 33;
}

static int
pf_key_v2_mask6_to_bits(u_int8_t *mask)
{
	int		n;

	bit_ffc(mask, 128, &n);
	return n == -1 ? 128 : n;
}

/*
 * Enable/disable a flow.
 * XXX Assumes OpenBSD {ADD,DEL}FLOW extensions.
 */
static int
pf_key_v2_flow(struct sockaddr *laddr, struct sockaddr *lmask,
    struct sockaddr *raddr, struct sockaddr *rmask,
    u_int8_t tproto, u_int16_t sport, u_int16_t dport,
    u_int8_t *spi, u_int8_t proto, struct sockaddr *dst,
    struct sockaddr *src, int delete, int ingress,
    u_int8_t srcid_type, u_int8_t *srcid, int srcid_len,
    u_int8_t dstid_type, u_int8_t *dstid, int dstid_len,
    struct ipsec_proto *iproto)
{
	char           *laddr_str, *lmask_str, *raddr_str, *rmask_str;

	struct sadb_msg msg;
	struct sadb_protocol flowtype;
	struct sadb_ident *sid = 0;
	struct sadb_address *addr = 0;
	struct sadb_protocol tprotocol;
	struct pf_key_v2_msg *flow = 0, *ret = 0;
	size_t		len;
	int		err;

	msg.sadb_msg_type = delete ? SADB_X_DELFLOW : SADB_X_ADDFLOW;
	switch (proto) {
	case IPSEC_PROTO_IPSEC_ESP:
		msg.sadb_msg_satype = SADB_SATYPE_ESP;
		break;
	case IPSEC_PROTO_IPSEC_AH:
		msg.sadb_msg_satype = SADB_SATYPE_AH;
		break;
	case IPSEC_PROTO_IPCOMP:
		msg.sadb_msg_satype = SADB_X_SATYPE_IPCOMP;
		break;
	default:
		log_print("pf_key_v2_flow: invalid proto %d", proto);
		goto cleanup;
	}
	msg.sadb_msg_seq = 0;
	flow = pf_key_v2_msg_new(&msg, 0);
	if (!flow)
		goto cleanup;

	if (!delete) {
		/* Setup the source ID, if provided. */
		if (srcid) {
			sid = calloc(
			    PF_KEY_V2_ROUND(srcid_len + 1) + sizeof *sid,
			    sizeof(u_int8_t));
			if (!sid)
				goto cleanup;

			sid->sadb_ident_len = ((sizeof *sid) / PF_KEY_V2_CHUNK)
			    + PF_KEY_V2_ROUND(srcid_len + 1) / PF_KEY_V2_CHUNK;
			sid->sadb_ident_exttype = SADB_EXT_IDENTITY_SRC;
			sid->sadb_ident_type = srcid_type;

			memcpy(sid + 1, srcid, srcid_len);

			if (pf_key_v2_msg_add(flow, (struct sadb_ext *) sid,
			    PF_KEY_V2_NODE_MALLOCED) == -1)
				goto cleanup;

			sid = 0;
		}
		/* Setup the destination ID, if provided. */
		if (dstid) {
			sid = calloc(
			    PF_KEY_V2_ROUND(dstid_len + 1) + sizeof *sid,
			    sizeof(u_int8_t));
			if (!sid)
				goto cleanup;

			sid->sadb_ident_len = ((sizeof *sid) / PF_KEY_V2_CHUNK)
			    + PF_KEY_V2_ROUND(dstid_len + 1) / PF_KEY_V2_CHUNK;
			sid->sadb_ident_exttype = SADB_EXT_IDENTITY_DST;
			sid->sadb_ident_type = dstid_type;

			memcpy(sid + 1, dstid, dstid_len);

			if (pf_key_v2_msg_add(flow, (struct sadb_ext *) sid,
			    PF_KEY_V2_NODE_MALLOCED) == -1)
				goto cleanup;

			sid = 0;
		}
	}
	/* Setup the flow type extension.  */
	bzero(&flowtype, sizeof flowtype);
	flowtype.sadb_protocol_exttype = SADB_X_EXT_FLOW_TYPE;
	flowtype.sadb_protocol_len = sizeof flowtype / PF_KEY_V2_CHUNK;
	flowtype.sadb_protocol_direction =
	    ingress ? IPSP_DIRECTION_IN : IPSP_DIRECTION_OUT;
	flowtype.sadb_protocol_proto = SADB_X_FLOW_TYPE_REQUIRE;

	if (pf_key_v2_msg_add(flow, (struct sadb_ext *)&flowtype, 0) == -1)
		goto cleanup;

	/*
	 * Setup the ADDRESS extensions.
	 */
	len = sizeof *addr + PF_KEY_V2_ROUND(SA_LEN(src));
	if (!delete)
	{
		addr = calloc(1, len);
		if (!addr)
			goto cleanup;
		addr->sadb_address_exttype = SADB_EXT_ADDRESS_DST;
		addr->sadb_address_len = len / PF_KEY_V2_CHUNK;
		addr->sadb_address_reserved = 0;
		pf_key_v2_setup_sockaddr(addr + 1, src, dst, 0, ingress);
		if (pf_key_v2_msg_add(flow, (struct sadb_ext *) addr,
		    PF_KEY_V2_NODE_MALLOCED) == -1)
			goto cleanup;
		addr = 0;
	}
	len = sizeof *addr + PF_KEY_V2_ROUND(SA_LEN(laddr));
	addr = calloc(1, len);
	if (!addr)
		goto cleanup;
	addr->sadb_address_exttype = SADB_X_EXT_SRC_FLOW;
	addr->sadb_address_len = len / PF_KEY_V2_CHUNK;
	addr->sadb_address_reserved = 0;
	pf_key_v2_setup_sockaddr(addr + 1, laddr, 0, sport, 0);
	if (pf_key_v2_msg_add(flow, (struct sadb_ext *) addr,
	    PF_KEY_V2_NODE_MALLOCED) == -1)
		goto cleanup;
	addr = 0;

	addr = calloc(1, len);
	if (!addr)
		goto cleanup;
	addr->sadb_address_exttype = SADB_X_EXT_SRC_MASK;
	addr->sadb_address_len = len / PF_KEY_V2_CHUNK;
	addr->sadb_address_reserved = 0;
	pf_key_v2_setup_sockaddr(addr + 1, lmask, 0, sport ? 0xffff : 0, 0);
	if (pf_key_v2_msg_add(flow, (struct sadb_ext *) addr,
	    PF_KEY_V2_NODE_MALLOCED) == -1)
		goto cleanup;
	addr = 0;

	addr = calloc(1, len);
	if (!addr)
		goto cleanup;
	addr->sadb_address_exttype = SADB_X_EXT_DST_FLOW;
	addr->sadb_address_len = len / PF_KEY_V2_CHUNK;
	addr->sadb_address_reserved = 0;
	pf_key_v2_setup_sockaddr(addr + 1, raddr, 0, dport, 0);
	if (pf_key_v2_msg_add(flow, (struct sadb_ext *) addr,
	    PF_KEY_V2_NODE_MALLOCED) == -1)
		goto cleanup;
	addr = 0;

	addr = calloc(1, len);
	if (!addr)
		goto cleanup;
	addr->sadb_address_exttype = SADB_X_EXT_DST_MASK;
	addr->sadb_address_len = len / PF_KEY_V2_CHUNK;
	addr->sadb_address_reserved = 0;
	pf_key_v2_setup_sockaddr(addr + 1, rmask, 0, dport ? 0xffff : 0, 0);
	if (pf_key_v2_msg_add(flow, (struct sadb_ext *) addr,
	    PF_KEY_V2_NODE_MALLOCED) == -1)
		goto cleanup;
	addr = 0;

	/* Setup the protocol extension.  */
	bzero(&tprotocol, sizeof tprotocol);
	tprotocol.sadb_protocol_exttype = SADB_X_EXT_PROTOCOL;
	tprotocol.sadb_protocol_len = sizeof tprotocol / PF_KEY_V2_CHUNK;
	tprotocol.sadb_protocol_proto = tproto;

	if (pf_key_v2_msg_add(flow, (struct sadb_ext *)&tprotocol, 0) == -1)
		goto cleanup;

	if (sockaddr2text(laddr, &laddr_str, 0))
		laddr_str = 0;
	if (sockaddr2text(lmask, &lmask_str, 0))
		lmask_str = 0;
	if (sockaddr2text(raddr, &raddr_str, 0))
		raddr_str = 0;
	if (sockaddr2text(rmask, &rmask_str, 0))
		rmask_str = 0;

	LOG_DBG((LOG_SYSDEP, 50,
	   "pf_key_v2_flow: src %s %s dst %s %s proto %u sport %u dport %u",
	 laddr_str ? laddr_str : "<??\?>", lmask_str ? lmask_str : "<??\?>",
	 raddr_str ? raddr_str : "<??\?>", rmask_str ? rmask_str : "<??\?>",
		 tproto, ntohs(sport), ntohs(dport)));

	free(laddr_str);
	free(lmask_str);
	free(raddr_str);
	free(rmask_str);

	ret = pf_key_v2_call(flow);
	pf_key_v2_msg_free(flow);
	flow = 0;
	if (!ret)
		goto cleanup;
	err = ((struct sadb_msg *)TAILQ_FIRST(ret)->seg)->sadb_msg_errno;
	if (err) {
		if (err == ESRCH)	/* These are common and usually
					 * harmless.  */
			LOG_DBG((LOG_SYSDEP, 10, "pf_key_v2_flow: %sFLOW: %s",
				 delete ? "DEL" : "ADD", strerror(err)));
		else
			log_print("pf_key_v2_flow: %sFLOW: %s",
			    delete ? "DEL" : "ADD", strerror(err));
		goto cleanup;
	}
	pf_key_v2_msg_free(ret);

	LOG_DBG((LOG_MISC, 50, "pf_key_v2_flow: %sFLOW: done",
		 delete ? "DEL" : "ADD"));

	return 0;

cleanup:
	free(sid);
	free(addr);
	if (flow)
		pf_key_v2_msg_free(flow);
	if (ret)
		pf_key_v2_msg_free(ret);
	return -1;
}

static u_int8_t *
pf_key_v2_convert_id(u_int8_t *id, int idlen, size_t *reslen, int *idtype)
{
	u_int8_t       *addr, *res = 0;
	char		addrbuf[ADDRESS_MAX + 5];

	switch (id[0]) {
	case IPSEC_ID_FQDN:
		res = calloc(idlen - ISAKMP_ID_DATA_OFF + ISAKMP_GEN_SZ,
		    sizeof(u_int8_t));
		if (!res)
			return 0;

		*reslen = idlen - ISAKMP_ID_DATA_OFF + ISAKMP_GEN_SZ;
		memcpy(res, id + ISAKMP_ID_DATA_OFF - ISAKMP_GEN_SZ, *reslen);
		*idtype = SADB_IDENTTYPE_FQDN;
		LOG_DBG((LOG_SYSDEP, 40, "pf_key_v2_convert_id: FQDN %.*s",
		    (int) *reslen, res));
		return res;

	case IPSEC_ID_USER_FQDN:
		res = calloc(idlen - ISAKMP_ID_DATA_OFF + ISAKMP_GEN_SZ,
		    sizeof(u_int8_t));
		if (!res)
			return 0;

		*reslen = idlen - ISAKMP_ID_DATA_OFF + ISAKMP_GEN_SZ;
		memcpy(res, id + ISAKMP_ID_DATA_OFF - ISAKMP_GEN_SZ, *reslen);
		*idtype = SADB_IDENTTYPE_USERFQDN;
		LOG_DBG((LOG_SYSDEP, 40, "pf_key_v2_convert_id: UFQDN %.*s",
		    (int) *reslen, res));
		return res;

	case IPSEC_ID_IPV4_ADDR:
		if (inet_ntop(AF_INET, id + ISAKMP_ID_DATA_OFF - ISAKMP_GEN_SZ,
		    addrbuf, ADDRESS_MAX) == NULL)
			return 0;
		*reslen = strlen(addrbuf) + 3;
		strlcat(addrbuf, "/32", ADDRESS_MAX + 5);
		res = (u_int8_t *) strdup(addrbuf);
		if (!res)
			return 0;
		*idtype = SADB_IDENTTYPE_PREFIX;
		LOG_DBG((LOG_SYSDEP, 40, "pf_key_v2_convert_id: "
		    "IPv4 address %s", res));
		return res;

	case IPSEC_ID_IPV6_ADDR:
		if (inet_ntop(AF_INET6,
		    id + ISAKMP_ID_DATA_OFF - ISAKMP_GEN_SZ,
		    addrbuf, ADDRESS_MAX) == NULL)
			return 0;
		*reslen = strlen(addrbuf) + 4;
		strlcat(addrbuf, "/128", ADDRESS_MAX + 5);
		res = (u_int8_t *) strdup(addrbuf);
		if (!res)
			return 0;
		LOG_DBG((LOG_SYSDEP, 40, "pf_key_v2_convert_id: "
		    "IPv6 address %s", res));
		*idtype = SADB_IDENTTYPE_PREFIX;
		return res;

	case IPSEC_ID_IPV4_ADDR_SUBNET:	/* XXX PREFIX */
		addr = id + ISAKMP_ID_DATA_OFF - ISAKMP_GEN_SZ;
		if (inet_ntop(AF_INET, addr, addrbuf, ADDRESS_MAX) == NULL)
			return 0;
		snprintf(addrbuf + strlen(addrbuf),
		    ADDRESS_MAX - strlen(addrbuf), "/%d",
		    pf_key_v2_mask_to_bits(*(u_int32_t *)(addr +
			sizeof(struct in_addr))));
		*reslen = strlen(addrbuf);
		res = (u_int8_t *) strdup(addrbuf);
		if (!res)
			return 0;
		*idtype = SADB_IDENTTYPE_PREFIX;
		LOG_DBG((LOG_SYSDEP, 40, "pf_key_v2_convert_id: "
		    "IPv4 subnet %s", res));
		return res;

	case IPSEC_ID_IPV6_ADDR_SUBNET:	/* XXX PREFIX */
		addr = id + ISAKMP_ID_DATA_OFF - ISAKMP_GEN_SZ;
		if (inet_ntop(AF_INET6, addr, addrbuf, ADDRESS_MAX) == NULL)
			return 0;
		snprintf(addrbuf + strlen(addrbuf),
		    ADDRESS_MAX - strlen(addrbuf), "/%d",
		    pf_key_v2_mask6_to_bits(addr +
			sizeof(struct in6_addr)));
		*reslen = strlen(addrbuf);
		res = (u_int8_t *) strdup(addrbuf);
		if (!res)
			return 0;
		LOG_DBG((LOG_SYSDEP, 40, "pf_key_v2_convert_id: "
		    "IPv6 subnet %s", res));
		*idtype = SADB_IDENTTYPE_PREFIX;
		return res;

	case IPSEC_ID_IPV4_RANGE:
	case IPSEC_ID_IPV6_RANGE:
	case IPSEC_ID_DER_ASN1_DN:
	case IPSEC_ID_DER_ASN1_GN:
	case IPSEC_ID_KEY_ID:
		/* XXX Not implemented yet.  */
		return 0;
	}

	return 0;
}

/* Enable a flow given an SA.  */
int
pf_key_v2_enable_sa(struct sa *sa, struct sa *isakmp_sa)
{
	struct ipsec_sa *isa = sa->data;
	struct sockaddr *dst, *src;
	int		error;
	struct proto   *proto = TAILQ_FIRST(&sa->protos);
	int		sidtype = 0, didtype = 0;
	size_t		sidlen = 0, didlen = 0;
	u_int8_t       *sid = 0, *did = 0;

	if (proto == NULL) {
		log_print("pf_key_v2_enable_sa: no proto");
		return EINVAL;
	}

	sa->transport->vtbl->get_dst(sa->transport, &dst);
	sa->transport->vtbl->get_src(sa->transport, &src);

	if (isakmp_sa->id_i) {
		if (isakmp_sa->initiator)
			sid = pf_key_v2_convert_id(isakmp_sa->id_i,
			    isakmp_sa->id_i_len, &sidlen, &sidtype);
		else
			did = pf_key_v2_convert_id(isakmp_sa->id_i,
			    isakmp_sa->id_i_len, &didlen, &didtype);
	}
	if (isakmp_sa->id_r) {
		if (isakmp_sa->initiator)
			did = pf_key_v2_convert_id(isakmp_sa->id_r,
			    isakmp_sa->id_r_len, &didlen, &didtype);
		else
			sid = pf_key_v2_convert_id(isakmp_sa->id_r,
			    isakmp_sa->id_r_len, &sidlen, &sidtype);
	}

	error = pf_key_v2_flow(isa->src_net, isa->src_mask, isa->dst_net,
	    isa->dst_mask, isa->tproto, isa->sport, isa->dport, proto->spi[0],
	    proto->proto, dst, src, 0, 0, sidtype, sid, sidlen, didtype, did,
	    didlen, proto->data);
	if (error)
		goto cleanup;

	error = pf_key_v2_flow(isa->dst_net, isa->dst_mask, isa->src_net,
	    isa->src_mask, isa->tproto, isa->dport, isa->sport, proto->spi[1],
	    proto->proto, src, dst, 0, 1, sidtype, sid, sidlen, didtype, did,
	    didlen, proto->data);

cleanup:
	free(sid);
	free(did);

	return error;
}

/* Increase reference count of refcounted sections. */
static int
pf_key_v2_conf_refinc(int af, char *section)
{
	char		conn[22];
	int		num;

	if (!section)
		return 0;

	num = conf_get_num(section, "Refcount", 0);
	if (num == 0)
		return 0;

	snprintf(conn, sizeof conn, "%d", num + 1);
	conf_set(af, section, "Refcount", conn, 1, 0);
	return 0;
}

/*
 * Return 0 if the section didn't exist or was removed, non-zero otherwise.
 * Don't touch non-refcounted (statically defined) sections.
 */
static int
pf_key_v2_conf_refhandle(int af, char *section)
{
	char		conn[22];
	int		num;

	if (!section)
		return 0;

	num = conf_get_num(section, "Refcount", 0);
	if (num == 1) {
		conf_remove_section(af, section);
		num--;
	} else if (num != 0) {
		snprintf(conn, sizeof conn, "%d", num - 1);
		conf_set(af, section, "Refcount", conn, 1, 0);
	}
	return num;
}

/* Remove all dynamically-established configuration entries.  */
static int
pf_key_v2_remove_conf(char *section)
{
	char           *ikepeer, *localid, *remoteid, *configname;
	struct conf_list_node *attr;
	struct conf_list *attrs;
	int		af;

	if (!section)
		return 0;

	if (!conf_get_str(section, "Phase"))
		return 0;

	/* Only remove dynamically-established entries. */
	attrs = conf_get_list(section, "Flags");
	if (attrs) {
		for (attr = TAILQ_FIRST(&attrs->fields); attr;
		    attr = TAILQ_NEXT(attr, link))
			if (!strcasecmp(attr->field, "__ondemand"))
				goto passed;

		conf_free_list(attrs);
	}
	return 0;

passed:
	conf_free_list(attrs);

	af = conf_begin();

	configname = conf_get_str(section, "Configuration");
	pf_key_v2_conf_refhandle(af, configname);

	/* These are the Phase 2 Local/Remote IDs. */
	localid = conf_get_str(section, "Local-ID");
	pf_key_v2_conf_refhandle(af, localid);

	remoteid = conf_get_str(section, "Remote-ID");
	pf_key_v2_conf_refhandle(af, remoteid);

	ikepeer = conf_get_str(section, "ISAKMP-peer");

	pf_key_v2_conf_refhandle(af, section);

	if (ikepeer) {
		remoteid = conf_get_str(ikepeer, "Remote-ID");
		localid = conf_get_str(ikepeer, "ID");
		configname = conf_get_str(ikepeer, "Configuration");

		pf_key_v2_conf_refhandle(af, ikepeer);
		pf_key_v2_conf_refhandle(af, configname);

		/* Phase 1 IDs */
		pf_key_v2_conf_refhandle(af, localid);
		pf_key_v2_conf_refhandle(af, remoteid);
	}
	conf_end(af, 1);
	return 0;
}

/* Disable a flow given a SA.  */
int
pf_key_v2_disable_sa(struct sa *sa, int incoming)
{
	struct ipsec_sa *isa = sa->data;
	struct sockaddr *dst, *src;
	struct proto   *proto = TAILQ_FIRST(&sa->protos);

	sa->transport->vtbl->get_dst(sa->transport, &dst);
	sa->transport->vtbl->get_src(sa->transport, &src);

	if (!incoming)
		return pf_key_v2_flow(isa->src_net, isa->src_mask,
		    isa->dst_net, isa->dst_mask, isa->tproto, isa->sport,
		    isa->dport, proto->spi[0], proto->proto, src, dst, 1, 0,
		    0, 0, 0, 0, 0, 0, proto->data);
	else {
		return pf_key_v2_flow(isa->dst_net, isa->dst_mask,
		    isa->src_net, isa->src_mask, isa->tproto, isa->dport,
		    isa->sport, proto->spi[1], proto->proto, src, dst, 1, 1,
		    0, 0, 0, 0, 0, 0, proto->data);
	}
}

/*
 * Delete the IPsec SA represented by the INCOMING direction in protocol PROTO
 * of the IKE security association SA.  Also delete potential flows tied to it.
 */
int
pf_key_v2_delete_spi(struct sa *sa, struct proto *proto, int incoming)
{
	struct sadb_msg msg;
	struct sadb_sa  ssa;
	struct sadb_address *addr = 0;
	struct sockaddr *saddr;
	int		len, err;
	struct pf_key_v2_msg *delete = 0, *ret = 0;

	/* If it's not an established SA, don't proceed. */
	if (!(sa->flags & SA_FLAG_READY))
		return 0;

	if (sa->name && !(sa->flags & SA_FLAG_REPLACED)) {
		LOG_DBG((LOG_SYSDEP, 50,
			 "pf_key_v2_delete_spi: removing configuration %s",
			 sa->name));
		pf_key_v2_remove_conf(sa->name);
	}
	msg.sadb_msg_type = SADB_DELETE;
	switch (proto->proto) {
	case IPSEC_PROTO_IPSEC_ESP:
		msg.sadb_msg_satype = SADB_SATYPE_ESP;
		break;
	case IPSEC_PROTO_IPSEC_AH:
		msg.sadb_msg_satype = SADB_SATYPE_AH;
		break;
	case IPSEC_PROTO_IPCOMP:
		msg.sadb_msg_satype = SADB_X_SATYPE_IPCOMP;
		break;
	default:
		log_print("pf_key_v2_delete_spi: invalid proto %d",
		    proto->proto);
		goto cleanup;
	}
	msg.sadb_msg_seq = 0;
	delete = pf_key_v2_msg_new(&msg, 0);
	if (!delete)
		goto cleanup;

	/* Setup the SA extension.  */
	ssa.sadb_sa_exttype = SADB_EXT_SA;
	ssa.sadb_sa_len = sizeof ssa / PF_KEY_V2_CHUNK;
	memcpy(&ssa.sadb_sa_spi, proto->spi[incoming], sizeof ssa.sadb_sa_spi);
	ssa.sadb_sa_replay = 0;
	ssa.sadb_sa_state = 0;
	ssa.sadb_sa_auth = 0;
	ssa.sadb_sa_encrypt = 0;
	ssa.sadb_sa_flags = 0;
	if (pf_key_v2_msg_add(delete, (struct sadb_ext *)&ssa, 0) == -1)
		goto cleanup;

	/*
	 * Setup the ADDRESS extensions.
	 */
	if (incoming)
		sa->transport->vtbl->get_dst(sa->transport, &saddr);
	else
		sa->transport->vtbl->get_src(sa->transport, &saddr);
	len = sizeof *addr + PF_KEY_V2_ROUND(SA_LEN(saddr));
	addr = calloc(1, len);
	if (!addr)
		goto cleanup;
	addr->sadb_address_exttype = SADB_EXT_ADDRESS_SRC;
	addr->sadb_address_len = len / PF_KEY_V2_CHUNK;
	addr->sadb_address_reserved = 0;
	memcpy(addr + 1, saddr, SA_LEN(saddr));
	switch (saddr->sa_family) {
	case AF_INET:
		((struct sockaddr_in *) (addr + 1))->sin_port = 0;
		break;
	case AF_INET6:
		((struct sockaddr_in6 *) (addr + 1))->sin6_port = 0;
		break;
	}
	if (pf_key_v2_msg_add(delete, (struct sadb_ext *) addr,
	    PF_KEY_V2_NODE_MALLOCED) == -1)
		goto cleanup;
	addr = 0;

	if (incoming)
		sa->transport->vtbl->get_src(sa->transport, &saddr);
	else
		sa->transport->vtbl->get_dst(sa->transport, &saddr);
	len = sizeof *addr + PF_KEY_V2_ROUND(SA_LEN(saddr));
	addr = calloc(1, len);
	if (!addr)
		goto cleanup;
	addr->sadb_address_exttype = SADB_EXT_ADDRESS_DST;
	addr->sadb_address_len = len / PF_KEY_V2_CHUNK;
	addr->sadb_address_reserved = 0;
	memcpy(addr + 1, saddr, SA_LEN(saddr));
	switch (saddr->sa_family) {
	case AF_INET:
		((struct sockaddr_in *) (addr + 1))->sin_port = 0;
		break;
	case AF_INET6:
		((struct sockaddr_in6 *) (addr + 1))->sin6_port = 0;
		break;
	}
	if (pf_key_v2_msg_add(delete, (struct sadb_ext *) addr,
	    PF_KEY_V2_NODE_MALLOCED) == -1)
		goto cleanup;
	addr = 0;

	ret = pf_key_v2_call(delete);
	pf_key_v2_msg_free(delete);
	delete = 0;
	if (!ret)
		goto cleanup;
	err = ((struct sadb_msg *)TAILQ_FIRST(ret)->seg)->sadb_msg_errno;
	if (err) {
		LOG_DBG((LOG_SYSDEP, 10, "pf_key_v2_delete_spi: DELETE: %s",
			 strerror(err)));
		goto cleanup;
	}
	pf_key_v2_msg_free(ret);

	LOG_DBG((LOG_SYSDEP, 50, "pf_key_v2_delete_spi: done"));

	return 0;

cleanup:
	free(addr);
	if (delete)
		pf_key_v2_msg_free(delete);
	if (ret)
		pf_key_v2_msg_free(ret);
	return -1;
}

static void
pf_key_v2_stayalive(struct exchange *exchange, void *vconn, int fail)
{
	char           *conn = vconn;
	struct sa      *sa;

	/* XXX What if it is phase 1 ? */
	sa = sa_lookup_by_name(conn, 2);
	if (sa)
		sa->flags |= SA_FLAG_STAYALIVE;

	/*
	 * Remove failed configuration entry -- call twice because it is
	 * created with a Refcount of 2.
	 */
	if (fail && (!exchange || exchange->name)) {
		pf_key_v2_remove_conf(conn);
		pf_key_v2_remove_conf(conn);
	}
	free(conn);
}

/* Check if a connection CONN exists, otherwise establish it.  */
void
pf_key_v2_connection_check(char *conn)
{
	if (!sa_lookup_by_name(conn, 2)) {
		LOG_DBG((LOG_SYSDEP, 70,
		    "pf_key_v2_connection_check: SA for %s missing", conn));
		exchange_establish(conn, pf_key_v2_stayalive, conn, 0);
	} else {
		LOG_DBG((LOG_SYSDEP, 70, "pf_key_v2_connection_check: "
		    "SA for %s exists", conn));
		free(conn);
	}
}

/* Handle a PF_KEY lifetime expiration message PMSG.  */
static void
pf_key_v2_expire(struct pf_key_v2_msg *pmsg)
{
	struct sadb_msg *msg;
	struct sadb_sa *ssa;
	struct sadb_address *dst;
	struct sockaddr *dstaddr;
	struct sadb_lifetime *life, *lifecurrent;
	struct sa      *sa;
	struct pf_key_v2_node *lifenode, *ext;
	char           *dst_str;

	msg = (struct sadb_msg *)TAILQ_FIRST(pmsg)->seg;
	ext = pf_key_v2_find_ext(pmsg, SADB_EXT_SA);
	if (!ext) {
		log_print("pf_key_v2_expire: no SA extension found");
		return;
	}
	ssa = ext->seg;
	ext = pf_key_v2_find_ext(pmsg, SADB_EXT_ADDRESS_DST);
	if (!ext) {
		log_print("pf_key_v2_expire: "
		    "no destination address extension found");
		return;
	}
	dst = ext->seg;
	dstaddr = (struct sockaddr *) (dst + 1);
	lifenode = pf_key_v2_find_ext(pmsg, SADB_EXT_LIFETIME_HARD);
	if (!lifenode)
		lifenode = pf_key_v2_find_ext(pmsg, SADB_EXT_LIFETIME_SOFT);
	if (!lifenode) {
		log_print("pf_key_v2_expire: no lifetime extension found");
		return;
	}
	life = lifenode->seg;

	lifenode = pf_key_v2_find_ext(pmsg, SADB_EXT_LIFETIME_CURRENT);
	if (!lifenode) {
		log_print("pf_key_v2_expire: "
		    "no current lifetime extension found");
		return;
	}
	lifecurrent = lifenode->seg;

	if (sockaddr2text(dstaddr, &dst_str, 0))
		dst_str = 0;

	LOG_DBG((LOG_SYSDEP, 20, "pf_key_v2_expire: "
	    "%s dst %s SPI %x sproto %d",
	    life->sadb_lifetime_exttype == SADB_EXT_LIFETIME_SOFT ? "SOFT"
	    : "HARD", dst_str ? dst_str : "<unknown>",
	    ntohl(ssa->sadb_sa_spi), msg->sadb_msg_satype));

	free(dst_str);

	/*
	 * Find the IPsec SA.  The IPsec stack has two SAs for every IKE SA,
	 * one outgoing and one incoming, we regard expirations for any of
	 * them as an expiration of the full IKE SA.  Likewise, in
	 * protection suites consisting of more than one protocol, any
	 * expired individual IPsec stack SA will be seen as an expiration
	 * of the full suite.
	 */
	switch (msg->sadb_msg_satype) {
	case SADB_SATYPE_ESP:
		sa = ipsec_sa_lookup(dstaddr, ssa->sadb_sa_spi,
		    IPSEC_PROTO_IPSEC_ESP);
		break;

	case SADB_SATYPE_AH:
		sa = ipsec_sa_lookup(dstaddr, ssa->sadb_sa_spi,
		    IPSEC_PROTO_IPSEC_AH);
		break;

	case SADB_X_SATYPE_IPCOMP:
		sa = ipsec_sa_lookup(dstaddr, ssa->sadb_sa_spi,
		    IPSEC_PROTO_IPCOMP);
		break;

	default:
		/* XXX Log? */
		sa = 0;
		break;
	}

	/* If the SA is already gone, don't do anything.  */
	if (!sa)
		return;

	/*
	 * If we got a notification, try to renegotiate the SA -- unless of
	 * course it has already been replaced by another.
	 * Also, ignore SAs that were not dynamically established, or that
	 * did not see any use.
	 */
	if (!(sa->flags & SA_FLAG_REPLACED) &&
	    (sa->flags & SA_FLAG_ONDEMAND) &&
	    lifecurrent->sadb_lifetime_bytes)
		exchange_establish(sa->name, 0, 0, 0);

	if (life->sadb_lifetime_exttype == SADB_EXT_LIFETIME_HARD) {
		/* Remove the old SA, it isn't useful anymore.  */
		sa_free(sa);
	}
}

static int
mask4len(const struct sockaddr_in *mask)
{
	int len;
	u_int32_t m;

	len = 0;
	for (m = 0x80000000; m & ntohl(mask->sin_addr.s_addr); m >>= 1)
		len++;
	if (len == 32)
		len = -1;
	return len;
}

#ifndef s6_addr8
#define s6_addr8 __u6_addr.__u6_addr8
#endif

static int
mask6len(const struct sockaddr_in6 *mask)
{
	int i, len;
	u_int8_t m;

	len = 0;
	for (i = 0, m = 0; i < 16 && !m; i++)
		for (m = 0x80; m & mask->sin6_addr.s6_addr8[i]; m >>= 1)
			len++;
	if (len == 128)
		len = -1;
	return len;
}

static int
phase2id(char *str, size_t size, const char *side, const char *sflow,
    int masklen, u_int8_t proto, u_int16_t port)
{
	char smasklen[10], sproto[10], sport[10];

	smasklen[0] = sproto[0] = sport[0] = 0;
	if (masklen != -1)
		snprintf(smasklen, sizeof smasklen, "/%d", masklen);
	if (proto)
		snprintf(sproto, sizeof sproto, "=%u", proto);
	if (port)
		snprintf(sport, sizeof sport, ":%u", ntohs(port));

	return snprintf(str, size, "%s-%s%s%s%s", side, sflow, smasklen,
	    sproto, sport);
}

/* Handle a PF_KEY SA ACQUIRE message PMSG.  */
static void
pf_key_v2_acquire(struct pf_key_v2_msg *pmsg)
{
	struct sadb_msg *msg, askpolicy_msg;
	struct pf_key_v2_msg *askpolicy = 0, *ret = 0;
	struct sadb_x_policy policy;
	struct sadb_address *dst = 0, *src = 0;
	struct sockaddr *dstaddr, *srcaddr = 0;
	struct sadb_ident *srcident = 0, *dstident = 0;
	char		dstbuf[ADDRESS_MAX], srcbuf[ADDRESS_MAX], *peer = 0;
	char		confname[120], *conn = 0;
	char           *srcid = 0, *dstid = 0, *prefstring = 0;
	int		slen, af, afamily, masklen;
	struct sockaddr *smask, *sflow, *dmask, *dflow;
	struct sadb_protocol *sproto;
	char		ssflow[ADDRESS_MAX], sdflow[ADDRESS_MAX];
	char		sdmask[ADDRESS_MAX], ssmask[ADDRESS_MAX];
	int		dmasklen, smasklen;
	char           *sidtype = 0, *didtype = 0;
	char		lname[100], dname[100], configname[200];
	int		shostflag = 0, dhostflag = 0;
	struct pf_key_v2_node *ext;
	struct passwd  *pwd = 0;
	u_int16_t       sport = 0, dport = 0;
	u_int8_t	tproto = 0;
	char		tmbuf[sizeof sport * 3 + 1], *xform;
	int		connlen;

	/* This needs to be dynamically allocated. */
	connlen = 22;
	conn = malloc(connlen);
	if (!conn) {
		log_error("pf_key_v2_acquire: malloc (%d) failed", connlen);
		return;
	}
	msg = (struct sadb_msg *)TAILQ_FIRST(pmsg)->seg;

	ext = pf_key_v2_find_ext(pmsg, SADB_EXT_ADDRESS_DST);
	if (!ext) {
		log_print("pf_key_v2_acquire: "
		    "no destination address specified");
		free(conn);
		return;
	}
	dst = ext->seg;

	ext = pf_key_v2_find_ext(pmsg, SADB_EXT_ADDRESS_SRC);
	if (ext)
		src = ext->seg;

	ext = pf_key_v2_find_ext(pmsg, SADB_EXT_IDENTITY_SRC);
	if (ext)
		srcident = ext->seg;

	ext = pf_key_v2_find_ext(pmsg, SADB_EXT_IDENTITY_DST);
	if (ext)
		dstident = ext->seg;

	/* Ask the kernel for the matching policy. */
	bzero(&askpolicy_msg, sizeof askpolicy_msg);
	askpolicy_msg.sadb_msg_type = SADB_X_ASKPOLICY;
	askpolicy = pf_key_v2_msg_new(&askpolicy_msg, 0);
	if (!askpolicy)
		goto fail;

	policy.sadb_x_policy_exttype = SADB_X_EXT_POLICY;
	policy.sadb_x_policy_len = sizeof policy / PF_KEY_V2_CHUNK;
	policy.sadb_x_policy_seq = msg->sadb_msg_seq;
	if (pf_key_v2_msg_add(askpolicy, (struct sadb_ext *)&policy, 0) == -1)
		goto fail;

	ret = pf_key_v2_call(askpolicy);
	if (!ret)
		goto fail;

	/* Now we have all the information needed. */

	ext = pf_key_v2_find_ext(ret, SADB_X_EXT_SRC_FLOW);
	if (!ext) {
		log_print("pf_key_v2_acquire: no source flow extension found");
		goto fail;
	}
	sflow = (struct sockaddr *) (((struct sadb_address *) ext->seg) + 1);

	ext = pf_key_v2_find_ext(ret, SADB_X_EXT_DST_FLOW);
	if (!ext) {
		log_print("pf_key_v2_acquire: "
		    "no destination flow extension found");
		goto fail;
	}
	dflow = (struct sockaddr *) (((struct sadb_address *) ext->seg) + 1);
	ext = pf_key_v2_find_ext(ret, SADB_X_EXT_SRC_MASK);
	if (!ext) {
		log_print("pf_key_v2_acquire: no source mask extension found");
		goto fail;
	}
	smask = (struct sockaddr *) (((struct sadb_address *) ext->seg) + 1);

	ext = pf_key_v2_find_ext(ret, SADB_X_EXT_DST_MASK);
	if (!ext) {
		log_print("pf_key_v2_acquire: "
		    "no destination mask extension found");
		goto fail;
	}
	dmask = (struct sockaddr *) (((struct sadb_address *) ext->seg) + 1);

	ext = pf_key_v2_find_ext(ret, SADB_X_EXT_FLOW_TYPE);
	if (!ext) {
		log_print("pf_key_v2_acquire: no flow type extension found");
		goto fail;
	}
	sproto = ext->seg;
	tproto = sproto->sadb_protocol_proto;

	bzero(ssflow, sizeof ssflow);
	bzero(sdflow, sizeof sdflow);
	bzero(ssmask, sizeof ssmask);
	bzero(sdmask, sizeof sdmask);
	smasklen = dmasklen = -1;

	sidtype = didtype = "IPV4_ADDR_SUBNET";	/* default */

	switch (sflow->sa_family) {
	case AF_INET:
		if (inet_ntop(AF_INET,
		    &((struct sockaddr_in *) sflow)->sin_addr, ssflow,
		    ADDRESS_MAX) == NULL) {
			log_print("pf_key_v2_acquire: inet_ntop failed");
			goto fail;
		}
		sport = ((struct sockaddr_in *) sflow)->sin_port;
		if (inet_ntop(AF_INET,
		    &((struct sockaddr_in *) dflow)->sin_addr, sdflow,
		    ADDRESS_MAX) == NULL) {
			log_print("pf_key_v2_acquire: inet_ntop failed");
			goto fail;
		}
		dport = ((struct sockaddr_in *) dflow)->sin_port;
		if (inet_ntop(AF_INET,
		    &((struct sockaddr_in *) smask)->sin_addr, ssmask,
		    ADDRESS_MAX) == NULL) {
			log_print("pf_key_v2_acquire: inet_ntop failed");
			goto fail;
		}
		if (inet_ntop(AF_INET,
		    &((struct sockaddr_in *) dmask)->sin_addr, sdmask,
		    ADDRESS_MAX) == NULL) {
			log_print("pf_key_v2_acquire: inet_ntop failed");
			goto fail;
		}
		smasklen = mask4len((struct sockaddr_in *) smask);
		dmasklen = mask4len((struct sockaddr_in *) dmask);
		if (((struct sockaddr_in *) smask)->sin_addr.s_addr ==
		    INADDR_BROADCAST) {
			shostflag = 1;
			sidtype = "IPV4_ADDR";
		}
		if (((struct sockaddr_in *) dmask)->sin_addr.s_addr ==
		    INADDR_BROADCAST) {
			dhostflag = 1;
			didtype = "IPV4_ADDR";
		}
		break;

	case AF_INET6:
		if (inet_ntop(AF_INET6,
		    &((struct sockaddr_in6 *) sflow)->sin6_addr,
		    ssflow, ADDRESS_MAX) == NULL) {
			log_print("pf_key_v2_acquire: inet_ntop failed");
			goto fail;
		}
		sport = ((struct sockaddr_in6 *) sflow)->sin6_port;
		if (inet_ntop(AF_INET6,
		    &((struct sockaddr_in6 *) dflow)->sin6_addr,
		    sdflow, ADDRESS_MAX) == NULL) {
			log_print("pf_key_v2_acquire: inet_ntop failed");
			goto fail;
		}
		dport = ((struct sockaddr_in6 *) dflow)->sin6_port;
		if (inet_ntop(AF_INET6,
		    &((struct sockaddr_in6 *) smask)->sin6_addr,
		    ssmask, ADDRESS_MAX) == NULL) {
			log_print("pf_key_v2_acquire: inet_ntop failed");
			goto fail;
		}
		if (inet_ntop(AF_INET6,
		    &((struct sockaddr_in6 *) dmask)->sin6_addr,
		    sdmask, ADDRESS_MAX) == NULL) {
			log_print("pf_key_v2_acquire: inet_ntop failed");
			goto fail;
		}
		smasklen = mask6len((struct sockaddr_in6 *) smask);
		dmasklen = mask6len((struct sockaddr_in6 *) dmask);
		sidtype = didtype = "IPV6_ADDR_SUBNET";
		if (IN6_IS_ADDR_FULL(&((struct sockaddr_in6 *)smask)->sin6_addr)) {
			shostflag = 1;
			sidtype = "IPV6_ADDR";
		}
		if (IN6_IS_ADDR_FULL(&((struct sockaddr_in6 *)dmask)->sin6_addr)) {
			dhostflag = 1;
			didtype = "IPV6_ADDR";
		}
		break;
	}

	dstaddr = (struct sockaddr *)(dst + 1);
	bzero(dstbuf, sizeof dstbuf);
	bzero(srcbuf, sizeof srcbuf);

	if (dstaddr->sa_family == 0) {
		/*
		 * Destination was not specified in the flow -- can we derive
		 * it?
		 */
		if (dhostflag == 0) {
			log_print("pf_key_v2_acquire: "
			    "Cannot determine precise destination");
			goto fail;
		}
		dstaddr = dflow;
	}
	switch (dstaddr->sa_family) {
	case AF_INET:
		if (inet_ntop(AF_INET,
		    &((struct sockaddr_in *) dstaddr)->sin_addr,
		    dstbuf, ADDRESS_MAX) == NULL) {
			log_print("pf_key_v2_acquire: inet_ntop failed");
			goto fail;
		}
		LOG_DBG((LOG_SYSDEP, 20,
		    "pf_key_v2_acquire: dst=%s sproto %d", dstbuf,
		    msg->sadb_msg_satype));
		break;

	case AF_INET6:
		if (inet_ntop(AF_INET6,
		    &((struct sockaddr_in6 *) dstaddr)->sin6_addr,
		    dstbuf, ADDRESS_MAX) == NULL) {
			log_print("pf_key_v2_acquire: inet_ntop failed");
			goto fail;
		}
		LOG_DBG((LOG_SYSDEP, 20,
		    "pf_key_v2_acquire: dst=%s sproto %d", dstbuf,
		    msg->sadb_msg_satype));
		break;
	}

	if (src) {
		srcaddr = (struct sockaddr *) (src + 1);

		switch (srcaddr->sa_family) {
		case AF_INET:
			if (inet_ntop(AF_INET,
			    &((struct sockaddr_in *) srcaddr)->sin_addr,
			    srcbuf, ADDRESS_MAX) == NULL) {
				log_print("pf_key_v2_acquire: "
				    "inet_ntop failed");
				goto fail;
			}
			break;

		case AF_INET6:
			if (inet_ntop(AF_INET6,
			    &((struct sockaddr_in6 *)srcaddr)->sin6_addr,
			    srcbuf, ADDRESS_MAX) == NULL) {
				log_print("pf_key_v2_acquire: "
				    "inet_ntop failed");
				goto fail;
			}
			break;

		default:
			/*
			 * The kernel will pass an all '0' EXT_ADDRESS_SRC if
			 * it wasn't specified for the flow. In that case, do
			 * NOT specify the srcaddr in the Peer-name below
			 */
			srcbuf[0] = 0;
			srcaddr = NULL;
			break;
		}
	}
	/* Insert source ID. */
	if (srcident) {
		slen = (srcident->sadb_ident_len * sizeof(u_int64_t))
			- sizeof(struct sadb_ident);
		if (((unsigned char *) (srcident + 1))[slen - 1] != '\0') {
			log_print("pf_key_v2_acquire: "
			    "source identity not NUL-terminated");
			goto fail;
		}
		/* Check for valid type. */
		switch (srcident->sadb_ident_type) {
		case SADB_IDENTTYPE_PREFIX:
			/* Determine what the address family is. */
			srcid = memchr(srcident + 1, ':', slen);
			if (srcid)
				afamily = AF_INET6;
			else
				afamily = AF_INET;

			srcid = memchr(srcident + 1, '/', slen);
			if (!srcid) {
				log_print("pf_key_v2_acquire: "
				    "badly formatted PREFIX identity");
				goto fail;
			}
			masklen = atoi(srcid + 1);

			/* XXX We only support host addresses. */
			if ((afamily == AF_INET6 && masklen != 128) ||
			    (afamily == AF_INET && masklen != 32)) {
				log_print("pf_key_v2_acquire: "
				    "non-host address specified in source "
				    "identity (mask length %d), ignoring "
				    "request", masklen);
				goto fail;
			}
			/*
			 * NUL-terminate the PREFIX string at the separator,
			 * then dup.
			 */
			*srcid = '\0';
			if (asprintf(&srcid, "id-%s",
			    (char *) (srcident + 1)) == -1) {
				log_error("pf_key_v2_acquire: asprintf() failed");
				goto fail;
			}

			/* Set the section if it doesn't already exist. */
			af = conf_begin();
			if (!conf_get_str(srcid, "ID-type")) {
				if (conf_set(af, srcid, "ID-type",
				    afamily == AF_INET ? "IPV4_ADDR" :
				    "IPV6_ADDR", 1, 0) ||
				    conf_set(af, srcid, "Refcount", "1", 1, 0) ||
				    conf_set(af, srcid, "Address",
					(char *) (srcident + 1), 1, 0)) {
					conf_end(af, 0);
					goto fail;
				}
			} else
				pf_key_v2_conf_refinc(af, srcid);
			conf_end(af, 1);
			break;

		case SADB_IDENTTYPE_FQDN:
			prefstring = "FQDN";
			/*FALLTHROUGH*/
		case SADB_IDENTTYPE_USERFQDN:
			if (!prefstring) {
				prefstring = "USER_FQDN";

				/*
				 * Check whether there is a string following
				 * the header; if no, that there is a user ID
				 * (and acquire the login name). If there is
				 * both a string and a user ID, check that
				 * they match.
				 */
				if ((slen == 0) &&
				    (srcident->sadb_ident_id == 0)) {
					log_print("pf_key_v2_acquire: "
					    "no user FQDN or ID provided");
					goto fail;
				}
				if (srcident->sadb_ident_id) {
					pwd =
					    getpwuid(srcident->sadb_ident_id);
					if (!pwd) {
						log_error("pf_key_v2_acquire: "
						    "could not acquire "
						    "username from provided "
						    "ID %llu",
						    srcident->sadb_ident_id);
						goto fail;
					}
					if (slen != 0)
						if (strcmp(pwd->pw_name,
						    (char *) (srcident + 1))
						    != 0) {
							log_print("pf_key_v2_acquire: "
							    "provided user "
							    "name and ID do "
							    "not match (%s != "
							    "%s)",
							    (char *) (srcident + 1),
							    pwd->pw_name);
							/*
							 * String has
							 * precedence, per
							 * RFC 2367.
							 */
						}
				}
			}
			if (asprintf(&srcid, "id-%s",
			    slen ? (char *) (srcident + 1) : pwd->pw_name) == -1) {
				log_error("pf_key_v2_acquire: asprintf() failed");
				goto fail;
			}
			pwd = 0;

			/* Set the section if it doesn't already exist. */
			af = conf_begin();
			if (!conf_get_str(srcid, "ID-type")) {
				if (conf_set(af, srcid, "ID-type", prefstring,
				    1, 0) ||
				    conf_set(af, srcid, "Refcount", "1", 1, 0) ||
				    conf_set(af, srcid, "Name",
					srcid + 3, 1, 0)) {
					conf_end(af, 0);
					goto fail;
				}
			} else
				pf_key_v2_conf_refinc(af, srcid);
			conf_end(af, 1);
			break;

		default:
			LOG_DBG((LOG_SYSDEP, 20,
			    "pf_key_v2_acquire: invalid source ID type %d",
			    srcident->sadb_ident_type));
			goto fail;
		}

		LOG_DBG((LOG_SYSDEP, 50,
		    "pf_key_v2_acquire: constructed source ID \"%s\"", srcid));
		prefstring = 0;
	}
	/* Insert destination ID. */
	if (dstident) {
		slen = (dstident->sadb_ident_len * sizeof(u_int64_t))
			- sizeof(struct sadb_ident);

		/* Check for valid type. */
		switch (dstident->sadb_ident_type) {
		case SADB_IDENTTYPE_PREFIX:
			/* Determine what the address family is. */
			dstid = memchr(dstident + 1, ':', slen);
			if (dstid)
				afamily = AF_INET6;
			else
				afamily = AF_INET;

			dstid = memchr(dstident + 1, '/', slen);
			if (!dstid) {
				log_print("pf_key_v2_acquire: "
				    "badly formatted PREFIX identity");
				goto fail;
			}
			masklen = atoi(dstid + 1);

			/* XXX We only support host addresses. */
			if ((afamily == AF_INET6 && masklen != 128) ||
			    (afamily == AF_INET && masklen != 32)) {
				log_print("pf_key_v2_acquire: "
				    "non-host address specified in "
				    "destination identity (mask length %d), "
				    "ignoring request", masklen);
				goto fail;
			}
			/*
			 * NUL-terminate the PREFIX string at the separator,
			 * then dup.
			 */
			*dstid = '\0';
			if (asprintf(&dstid, "id-%s",
			    (char *) (dstident + 1)) == -1) {
				log_error("pf_key_v2_acquire: asprintf() failed");
				goto fail;
			}

			/* Set the section if it doesn't already exist. */
			af = conf_begin();
			if (!conf_get_str(dstid, "ID-type")) {
				if (conf_set(af, dstid, "ID-type",
				    afamily == AF_INET ? "IPV4_ADDR" :
				    "IPV6_ADDR", 1, 0) ||
				    conf_set(af, dstid, "Refcount", "1", 1, 0) ||
				    conf_set(af, dstid, "Address",
					(char *) (dstident + 1), 1, 0)) {
					conf_end(af, 0);
					goto fail;
				}
			} else
				pf_key_v2_conf_refinc(af, dstid);
			conf_end(af, 1);
			break;

		case SADB_IDENTTYPE_FQDN:
			prefstring = "FQDN";
			/*FALLTHROUGH*/
		case SADB_IDENTTYPE_USERFQDN:
			if (!prefstring) {
				prefstring = "USER_FQDN";

				/*
				 * Check whether there is a string following
				 * the header; if no, that there is a user ID
				 * (and acquire the login name). If there is
				 * both a string and a user ID, check that
				 * they match.
				 */
				if (slen == 0 &&
				    dstident->sadb_ident_id == 0) {
					log_print("pf_key_v2_acquire: "
					    "no user FQDN or ID provided");
					goto fail;
				}
				if (dstident->sadb_ident_id) {
					pwd = getpwuid(dstident->sadb_ident_id);
					if (!pwd) {
						log_error("pf_key_v2_acquire: "
						    "could not acquire "
						    "username from provided "
						    "ID %llu",
						    dstident->sadb_ident_id);
						goto fail;
					}
					if (slen != 0)
						if (strcmp(pwd->pw_name,
						    (char *) (dstident + 1))
						    != 0) {
							log_print("pf_key_v2_acquire: "
							    "provided user "
							    "name and ID do "
							    "not match (%s != "
							    "%s)",
							    (char *) (dstident + 1),
							    pwd->pw_name);
							/*
							 * String has
							 * precedence, per RF
							 * 2367.
							 */
						}
				}
			}
			if (asprintf(&dstid, "id-%s",
			    slen ? (char *) (dstident + 1) : pwd->pw_name) == -1) {
				log_error("pf_key_v2_acquire: asprintf() failed");
				goto fail;
			}
			pwd = 0;

			/* Set the section if it doesn't already exist. */
			af = conf_begin();
			if (!conf_get_str(dstid, "ID-type")) {
				if (conf_set(af, dstid, "ID-type", prefstring,
				    1, 0) ||
				    conf_set(af, dstid, "Refcount", "1", 1, 0) ||
				    conf_set(af, dstid, "Name",
					dstid + 3, 1, 0)) {
					conf_end(af, 0);
					goto fail;
				}
			} else
				pf_key_v2_conf_refinc(af, dstid);
			conf_end(af, 1);
			break;

		default:
			LOG_DBG((LOG_SYSDEP, 20, "pf_key_v2_acquire: "
			    "invalid destination ID type %d",
			    dstident->sadb_ident_type));
			goto fail;
		}

		LOG_DBG((LOG_SYSDEP, 50,
		    "pf_key_v2_acquire: constructed destination ID \"%s\"",
		    dstid));
	}
	/* Now we've placed the necessary IDs in the configuration space. */

	/* Get a new connection sequence number. */
	for (;; connection_seq++) {
		snprintf(conn, connlen, "Connection-%u", connection_seq);

		/* Does it exist ? */
		if (!conf_get_str(conn, "Phase"))
			break;
	}

	/*
	 * Set the IPsec connection entry. In particular, the following fields:
	 * - Phase
	 * - ISAKMP-peer
	 * - Local-ID/Remote-ID (if provided)
	 * - Acquire-ID (sequence number of kernel message, e.g., PF_KEYv2)
	 * - Configuration
	 *
	 * Also set the following section:
	 *    [peer-dstaddr(-local-srcaddr)]
	 * with these fields:
	 * - Phase
	 * - ID (if provided)
	 * - Remote-ID (if provided)
	 * - Local-address (if provided)
	 * - Address
	 * - Configuration (if an entry phase1-dstaddr-srcadd)
	 *                  exists -- otherwise use the defaults)
	 */

	/*
	 * The various cases:
	 * - peer-dstaddr
	 * - peer-dstaddr-local-srcaddr
	 */
	if (asprintf(&peer, "peer-%s%s%s", dstbuf, srcaddr ? "-local-" : "",
	    srcaddr ? srcbuf : "") == -1)
		goto fail;

	/*
	 * Set the IPsec connection section. Refcount is set to 2, because
	 * it will be linked both to the incoming and the outgoing SA.
	 */
	af = conf_begin();
	if (conf_set(af, conn, "Phase", "2", 0, 0) ||
	    conf_set(af, conn, "Flags", "__ondemand", 0, 0) ||
	    conf_set(af, conn, "Refcount", "2", 0, 0) ||
	    conf_set(af, conn, "ISAKMP-peer", peer, 0, 0)) {
		conf_end(af, 0);
		goto fail;
	}
	/* Set the sequence number. */
	snprintf(lname, sizeof lname, "%u", msg->sadb_msg_seq);
	if (conf_set(af, conn, "Acquire-ID", lname, 0, 0)) {
		conf_end(af, 0);
		goto fail;
	}
	/*
	 * Set Phase 2 IDs -- this is the Local-ID section.
	 * - from-address
	 * - from-address=proto
	 * - from-address=proto:port
	 * - from-network/masklen
	 * - from-network/masklen=proto
	 * - from-network/masklen=proto:port
	 */
	phase2id(lname, sizeof lname, "from", ssflow, smasklen, tproto, sport);
	if (conf_set(af, conn, "Local-ID", lname, 0, 0)) {
		conf_end(af, 0);
		goto fail;
	}
	if (!conf_get_str(lname, "ID-type")) {
		if (conf_set(af, lname, "Refcount", "1", 0, 0)) {
			conf_end(af, 0);
			goto fail;
		}
		if (shostflag) {
			if (conf_set(af, lname, "ID-type", sidtype, 0, 0) ||
			    conf_set(af, lname, "Address", ssflow, 0, 0)) {
				conf_end(af, 0);
				goto fail;
			}
		} else {
			if (conf_set(af, lname, "ID-type", sidtype, 0, 0) ||
			    conf_set(af, lname, "Network", ssflow, 0, 0) ||
			    conf_set(af, lname, "Netmask", ssmask, 0, 0)) {
				conf_end(af, 0);
				goto fail;
			}
		}
		if (tproto) {
			snprintf(tmbuf, sizeof sport * 3 + 1, "%u", tproto);
			if (conf_set(af, lname, "Protocol", tmbuf, 0, 0)) {
				conf_end(af, 0);
				goto fail;
			}
			if (sport) {
				snprintf(tmbuf, sizeof sport * 3 + 1, "%u",
				    ntohs(sport));
				if (conf_set(af, lname, "Port", tmbuf, 0, 0)) {
					conf_end(af, 0);
					goto fail;
				}
			}
		}
	} else
		pf_key_v2_conf_refinc(af, lname);

	/*
	 * Set Remote-ID section.
	 * to-address
	 * to-address=proto
	 * to-address=proto:port
	 * to-network/masklen
	 * to-network/masklen=proto
	 * to-network/masklen=proto:port
	 */
	phase2id(dname, sizeof dname, "to", sdflow, dmasklen, tproto, dport);
	if (conf_set(af, conn, "Remote-ID", dname, 0, 0)) {
		conf_end(af, 0);
		goto fail;
	}
	if (!conf_get_str(dname, "ID-type")) {
		if (conf_set(af, dname, "Refcount", "1", 0, 0)) {
			conf_end(af, 0);
			goto fail;
		}
		if (dhostflag) {
			if (conf_set(af, dname, "ID-type", didtype, 0, 0) ||
			    conf_set(af, dname, "Address", sdflow, 0, 0)) {
				conf_end(af, 0);
				goto fail;
			}
		} else {
			if (conf_set(af, dname, "ID-type", didtype, 0, 0) ||
			    conf_set(af, dname, "Network", sdflow, 0, 0) ||
			    conf_set(af, dname, "Netmask", sdmask, 0, 0)) {
				conf_end(af, 0);
				goto fail;
			}
		}

		if (tproto) {
			snprintf(tmbuf, sizeof dport * 3 + 1, "%u", tproto);
			if (conf_set(af, dname, "Protocol", tmbuf, 0, 0)) {
				conf_end(af, 0);
				goto fail;
			}
			if (dport) {
				snprintf(tmbuf, sizeof dport * 3 + 1, "%u",
				    ntohs(dport));
				if (conf_set(af, dname, "Port", tmbuf, 0, 0)) {
					conf_end(af, 0);
					goto fail;
				}
			}
		}
	} else
		pf_key_v2_conf_refinc(af, dname);

	/*
	 * XXX
	 * We should be using information from the proposal to set this up.
	 * At least, we should make this selectable.
	 */

	/*
	 * Phase 2 configuration.
	 * - phase2-from-address-to-address
	 * - ...
	 * - phase2-from-net/len=proto:port-to-net/len=proto:port
	 */
	snprintf(configname, sizeof configname, "phase2-%s-%s", lname, dname);
	if (conf_set(af, conn, "Configuration", configname, 0, 0)) {
		conf_end(af, 0);
		goto fail;
	}
	if (!conf_get_str(configname, "Exchange_type")) {
		if (conf_set(af, configname, "Exchange_type", "Quick_mode",
		    0, 0) ||
		    conf_set(af, peer, "Refcount", "1", 0, 0) ||
		    conf_set(af, configname, "DOI", "IPSEC", 0, 0)) {
			conf_end(af, 0);
			goto fail;
		}
		if (conf_get_str("General", "Default-phase-2-suites")) {
			if (conf_set(af, configname, "Suites",
			    conf_get_str("General", "Default-phase-2-suites"),
			    0, 0)) {
				conf_end(af, 0);
				goto fail;
			}
		} else {
			if (conf_set(af, configname, "Suites",
			    "QM-ESP-3DES-SHA-PFS-SUITE", 0, 0)) {
				conf_end(af, 0);
				goto fail;
			}
		}
	} else 
		pf_key_v2_conf_refinc(af, configname);

	/* Set the ISAKMP-peer section. */
	if (!conf_get_str(peer, "Phase")) {
		if (conf_set(af, peer, "Phase", "1", 0, 0) ||
		    conf_set(af, peer, "Refcount", "1", 0, 0) ||
		    conf_set(af, peer, "Address", dstbuf, 0, 0)) {
			conf_end(af, 0);
			goto fail;
		}
		if (srcaddr && conf_set(af, peer, "Local-address", srcbuf, 0,
		    0)) {
			conf_end(af, 0);
			goto fail;
		}
		snprintf(confname, sizeof confname, "phase1-%s", peer);
		if (conf_set(af, peer, "Configuration", confname, 0, 0)) {
			conf_end(af, 0);
			goto fail;
		}

		/* Phase 1 configuration. */
		if (!conf_get_str(confname, "exchange_type")) {
			xform = conf_get_str("Default-phase-1-configuration",
			"Transforms");
			if (conf_set(af, confname, "Transforms", xform ? xform :
			    "3DES-SHA-RSA_SIG", 0, 0)) {
				conf_end(af, 0);
				goto fail;
			}

			if (conf_set(af, confname, "Exchange_Type", "ID_PROT",
			    0, 0) ||
			    conf_set(af, confname, "DOI", "IPSEC", 0, 0) ||
			    conf_set(af, confname, "Refcount", "1", 0, 0)) {
				conf_end(af, 0);
				goto fail;
			}
		} else
			pf_key_v2_conf_refinc(af, confname);

		/* The ID we should use in Phase 1. */
		if (srcid && conf_set(af, peer, "ID", srcid, 0, 0)) {
			conf_end(af, 0);
			goto fail;
		}
		/* The ID the other side should use in Phase 1. */
		if (dstid && conf_set(af, peer, "Remote-ID", dstid, 0, 0)) {
			conf_end(af, 0);
			goto fail;
		}
	} else
		pf_key_v2_conf_refinc(af, peer);

	/* All done. */
	conf_end(af, 1);

	/* Let's rock 'n roll. */
	connection_record_passive(conn);
	pf_key_v2_connection_check(conn);
	conn = 0;

	/* Fall-through to cleanup. */
fail:
	if (ret)
		pf_key_v2_msg_free(ret);
	if (askpolicy)
		pf_key_v2_msg_free(askpolicy);
	free(srcid);
	free(dstid);
	free(peer);
	free(conn);
	return;
}

static void
pf_key_v2_notify(struct pf_key_v2_msg *msg)
{
	switch (((struct sadb_msg *)TAILQ_FIRST(msg)->seg)->sadb_msg_type) {
	case SADB_EXPIRE:
		pf_key_v2_expire(msg);
		break;

	case SADB_ACQUIRE:
		if (!ui_daemon_passive)
			pf_key_v2_acquire(msg);
		break;

	default:
		log_print("pf_key_v2_notify: unexpected message type (%d)",
		    ((struct sadb_msg *)TAILQ_FIRST(msg)->seg)->sadb_msg_type);
	}
	pf_key_v2_msg_free(msg);
}

void
pf_key_v2_handler(int fd)
{
	struct pf_key_v2_msg *msg;
	int		n;

	/*
	 * As synchronous read/writes to the socket can have taken place
	 * between the select(2) call of the main loop and this handler, we
	 * need to recheck the readability.
	 */
	if (ioctl(pf_key_v2_socket, FIONREAD, &n) == -1) {
		log_error("pf_key_v2_handler: ioctl (%d, FIONREAD, &n) failed",
		    pf_key_v2_socket);
		return;
	}
	if (!n)
		return;

	msg = pf_key_v2_read(0);
	if (msg)
		pf_key_v2_notify(msg);
}

/*
 * Group 2 IPsec SAs given by the PROTO1 and PROTO2 protocols of the SA IKE
 * security association in a chain.
 * XXX Assumes OpenBSD GRPSPIS extension.
 */
int
pf_key_v2_group_spis(struct sa *sa, struct proto *proto1,
    struct proto *proto2, int incoming)
{
	struct sadb_msg msg;
	struct sadb_sa  sa1, sa2;
	struct sadb_address *addr = 0;
	struct sadb_protocol protocol;
	struct pf_key_v2_msg *grpspis = 0, *ret = 0;
	struct sockaddr *saddr;
	int		err;
	size_t		len;

	msg.sadb_msg_type = SADB_X_GRPSPIS;
	switch (proto1->proto) {
	case IPSEC_PROTO_IPSEC_ESP:
		msg.sadb_msg_satype = SADB_SATYPE_ESP;
		break;
	case IPSEC_PROTO_IPSEC_AH:
		msg.sadb_msg_satype = SADB_SATYPE_AH;
		break;
	case IPSEC_PROTO_IPCOMP:
		msg.sadb_msg_satype = SADB_X_SATYPE_IPCOMP;
		break;
	default:
		log_print("pf_key_v2_group_spis: invalid proto %d",
		    proto1->proto);
		goto cleanup;
	}
	msg.sadb_msg_seq = 0;
	grpspis = pf_key_v2_msg_new(&msg, 0);
	if (!grpspis)
		goto cleanup;

	/* Setup the SA extensions.  */
	sa1.sadb_sa_exttype = SADB_EXT_SA;
	sa1.sadb_sa_len = sizeof sa1 / PF_KEY_V2_CHUNK;
	memcpy(&sa1.sadb_sa_spi, proto1->spi[incoming],
	    sizeof sa1.sadb_sa_spi);
	sa1.sadb_sa_replay = 0;
	sa1.sadb_sa_state = 0;
	sa1.sadb_sa_auth = 0;
	sa1.sadb_sa_encrypt = 0;
	sa1.sadb_sa_flags = 0;
	if (pf_key_v2_msg_add(grpspis, (struct sadb_ext *)&sa1, 0) == -1)
		goto cleanup;

	sa2.sadb_sa_exttype = SADB_X_EXT_SA2;
	sa2.sadb_sa_len = sizeof sa2 / PF_KEY_V2_CHUNK;
	memcpy(&sa2.sadb_sa_spi, proto2->spi[incoming],
	    sizeof sa2.sadb_sa_spi);
	sa2.sadb_sa_replay = 0;
	sa2.sadb_sa_state = 0;
	sa2.sadb_sa_auth = 0;
	sa2.sadb_sa_encrypt = 0;
	sa2.sadb_sa_flags = 0;
	if (pf_key_v2_msg_add(grpspis, (struct sadb_ext *)&sa2, 0) == -1)
		goto cleanup;

	/*
	 * Setup the ADDRESS extensions.
	 */
	if (incoming)
		sa->transport->vtbl->get_src(sa->transport, &saddr);
	else
		sa->transport->vtbl->get_dst(sa->transport, &saddr);
	len = sizeof *addr + PF_KEY_V2_ROUND(SA_LEN(saddr));
	addr = calloc(1, len);
	if (!addr)
		goto cleanup;
	addr->sadb_address_exttype = SADB_EXT_ADDRESS_DST;
	addr->sadb_address_len = len / PF_KEY_V2_CHUNK;
	addr->sadb_address_reserved = 0;
	memcpy(addr + 1, saddr, SA_LEN(saddr));
	((struct sockaddr_in *) (addr + 1))->sin_port = 0;
	if (pf_key_v2_msg_add(grpspis, (struct sadb_ext *) addr,
	    PF_KEY_V2_NODE_MALLOCED) == -1)
		goto cleanup;
	addr = 0;

	addr = calloc(1, len);
	if (!addr)
		goto cleanup;
	addr->sadb_address_exttype = SADB_X_EXT_DST2;
	addr->sadb_address_len = len / PF_KEY_V2_CHUNK;
	addr->sadb_address_reserved = 0;
	memcpy(addr + 1, saddr, SA_LEN(saddr));
	((struct sockaddr_in *) (addr + 1))->sin_port = 0;
	if (pf_key_v2_msg_add(grpspis, (struct sadb_ext *) addr,
	    PF_KEY_V2_NODE_MALLOCED) == -1)
		goto cleanup;
	addr = 0;

	/* Setup the sa type extension.  */
	protocol.sadb_protocol_exttype = SADB_X_EXT_SATYPE2;
	protocol.sadb_protocol_len = sizeof protocol / PF_KEY_V2_CHUNK;
	switch (proto2->proto) {
	case IPSEC_PROTO_IPSEC_ESP:
		protocol.sadb_protocol_proto = SADB_SATYPE_ESP;
		break;
	case IPSEC_PROTO_IPSEC_AH:
		protocol.sadb_protocol_proto = SADB_SATYPE_AH;
		break;
	case IPSEC_PROTO_IPCOMP:
		protocol.sadb_protocol_proto = SADB_X_SATYPE_IPCOMP;
		break;
	default:
		log_print("pf_key_v2_group_spis: invalid proto %d",
		    proto2->proto);
		goto cleanup;
	}
	protocol.sadb_protocol_reserved2 = 0;
	if (pf_key_v2_msg_add(grpspis,
	    (struct sadb_ext *)&protocol, 0) == -1)
		goto cleanup;

	ret = pf_key_v2_call(grpspis);
	pf_key_v2_msg_free(grpspis);
	grpspis = 0;
	if (!ret)
		goto cleanup;
	err = ((struct sadb_msg *)TAILQ_FIRST(ret)->seg)->sadb_msg_errno;
	if (err) {
		log_print("pf_key_v2_group_spis: GRPSPIS: %s", strerror(err));
		goto cleanup;
	}
	pf_key_v2_msg_free(ret);

	LOG_DBG((LOG_SYSDEP, 50, "pf_key_v2_group_spis: done"));

	return 0;

cleanup:
	free(addr);
	if (grpspis)
		pf_key_v2_msg_free(grpspis);
	if (ret)
		pf_key_v2_msg_free(ret);
	return -1;
}
