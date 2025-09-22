/*	$OpenBSD: net.c,v 1.24 2022/01/28 06:33:27 guenther Exp $	*/

/*
 * Copyright (c) 2005 Håkan Olsson.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
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
 * This code was written under funding by Multicom Security AB.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <signal.h>

#include <openssl/aes.h>
#include <openssl/sha.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sasyncd.h"
#include "net.h"

struct msg {
	u_int8_t	*buf;
	u_int32_t	 len;
	int		 refcnt;
};

struct qmsg {
	SIMPLEQ_ENTRY(qmsg)	next;
	struct msg	*msg;
};

int	*listeners;
AES_KEY	aes_key[2];
#define AES_IV_LEN	AES_BLOCK_SIZE

/* We never send (or expect to receive) messages smaller/larger than this. */
#define MSG_MINLEN	12
#define MSG_MAXLEN	4096

/* Local prototypes. */
static u_int8_t *net_read(struct syncpeer *, u_int32_t *, u_int32_t *);
static int	 net_set_sa(struct sockaddr *, char *, in_port_t);
static void	 net_check_peers(void *);

/* Pretty-print a buffer. */
void
dump_buf(int lvl, u_int8_t *b, u_int32_t len, char *title)
{
	u_int32_t	i, off, blen;
	u_int8_t	*buf;
	const char	def[] = "Buffer:";

	if (cfgstate.verboselevel < lvl)
		return;

	blen = 2 * (len + len / 36) + 3 + (title ? strlen(title) : sizeof def);
	if (!(buf = calloc(1, blen)))
		return;

	snprintf(buf, blen, "%s\n ", title ? title : def);
	off = strlen(buf);
	for (i = 0; i < len; i++, off+=2) {
		snprintf(buf + off, blen - off, "%02x", b[i]);
		if ((i+1) % 36 == 0) {
			off += 2;
			snprintf(buf + off, blen - off, "\n ");
		}
	}
	log_msg(lvl, "%s", buf);
	free(buf);
}

/* Add a listening socket. */
static int
net_add_listener(struct sockaddr *sa)
{
	char	host[NI_MAXHOST], port[NI_MAXSERV];
	int	r, s;

	s = socket(sa->sa_family, SOCK_STREAM, 0);
	if (s < 0) {
		perror("net_add_listener: socket()");
		close(s);
		return -1;
	}

	r = 1;
	if (setsockopt(s, SOL_SOCKET,
		cfgstate.listen_on ? SO_REUSEADDR : SO_REUSEPORT, (void *)&r,
		sizeof r)) {
		perror("net_add_listener: setsockopt()");
		close(s);
		return -1;
	}

	if (bind(s, sa, sa->sa_family == AF_INET ? sizeof(struct sockaddr_in) :
		sizeof (struct sockaddr_in6))) {
		perror("net_add_listener: bind()");
		close(s);
		return -1;
	}

	if (listen(s, 3)) {
		perror("net_add_listener: listen()");
		close(s);
		return -1;
	}

	if (getnameinfo(sa, sa->sa_len, host, sizeof host, port, sizeof port,
		NI_NUMERICHOST | NI_NUMERICSERV))
		log_msg(2, "listening on port %u fd %d", cfgstate.listen_port,
		    s);
	else
		log_msg(2, "listening on %s port %s fd %d", host, port, s);

	return s;
}

/* Allocate and fill in listeners array. */
static int
net_setup_listeners(void)
{
	struct sockaddr_storage	 sa_storage;
	struct sockaddr		*sa = (struct sockaddr *)&sa_storage;
	struct sockaddr_in	*sin = (struct sockaddr_in *)sa;
	struct sockaddr_in6	*sin6 = (struct sockaddr_in6 *)sa;
	struct ifaddrs		*ifap = 0, *ifa;
	int			 i, count;

	/* Setup listening sockets.  */
	memset(&sa_storage, 0, sizeof sa_storage);
	if (net_set_sa(sa, cfgstate.listen_on, cfgstate.listen_port) == 0) {
		listeners = calloc(2, sizeof(int));
		if (!listeners) {
			perror("net_setup_listeners: calloc()");
			goto errout;
		}
		listeners[1] = -1;
		listeners[0] = net_add_listener(sa);
		if (listeners[0] == -1) {
			log_msg(0, "net_setup_listeners: could not find "
			    "listen address (%s)", cfgstate.listen_on);
			goto errout;
		}
		return 0;
	}

	/*
	 * If net_set_sa() failed, cfgstate.listen_on is probably an
	 * interface name, so we should listen on all its addresses.
	 */

	if (getifaddrs(&ifap) != 0) {
		perror("net_setup_listeners: getifaddrs()");
		goto errout;
	}

	/* How many addresses matches? */
	for (count = 0, ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (!ifa->ifa_name || !ifa->ifa_addr ||
		    (ifa->ifa_addr->sa_family != AF_INET &&
			ifa->ifa_addr->sa_family != AF_INET6))
			continue;
		if (cfgstate.listen_family &&
		    cfgstate.listen_family != ifa->ifa_addr->sa_family)
			continue;
		if (strcmp(ifa->ifa_name, cfgstate.listen_on) != 0)
			continue;
		count++;
	}

	if (!count) {
		log_msg(0, "net_setup_listeners: no listeners found for %s",
		    cfgstate.listen_on);
		goto errout;
	}

	/* Allocate one extra slot and set to -1, marking end of array. */
	listeners = calloc(count + 1, sizeof(int));
	if (!listeners) {
		perror("net_setup_listeners: calloc()");
		goto errout;
	}
	for (i = 0; i <= count; i++)
		listeners[i] = -1;

	/* Create listening sockets */
	for (count = 0, ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (!ifa->ifa_name || !ifa->ifa_addr ||
		    (ifa->ifa_addr->sa_family != AF_INET &&
			ifa->ifa_addr->sa_family != AF_INET6))
			continue;
		if (cfgstate.listen_family &&
		    cfgstate.listen_family != ifa->ifa_addr->sa_family)
			continue;
		if (strcmp(ifa->ifa_name, cfgstate.listen_on) != 0)
			continue;

		memset(&sa_storage, 0, sizeof sa_storage);
		sa->sa_family = ifa->ifa_addr->sa_family;
		switch (sa->sa_family) {
		case AF_INET:
			sin->sin_port = htons(cfgstate.listen_port);
			sin->sin_len = sizeof *sin;
			memcpy(&sin->sin_addr,
			    &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr,
			    sizeof sin->sin_addr);
			break;
		case AF_INET6:
			sin6->sin6_port = htons(cfgstate.listen_port);
			sin6->sin6_len = sizeof *sin6;
			memcpy(&sin6->sin6_addr,
			    &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr,
			    sizeof sin6->sin6_addr);
			break;
		}

		listeners[count] = net_add_listener(sa);
		if (listeners[count] == -1) {
			log_msg(2, "net_setup_listeners(setup): failed to "
			    "add listener, count = %d", count);
			goto errout;
		}
		count++;
	}
	freeifaddrs(ifap);
	return 0;

  errout:
	if (ifap)
		freeifaddrs(ifap);
	if (listeners) {
		for (i = 0; listeners[i] != -1; i++)
			close(listeners[i]);
		free(listeners);
	}
	return -1;
}

int
net_init(void)
{
	struct syncpeer *p;

	if (AES_set_encrypt_key(cfgstate.sharedkey, cfgstate.sharedkey_len,
	    &aes_key[0]) ||
	    AES_set_decrypt_key(cfgstate.sharedkey, cfgstate.sharedkey_len,
	    &aes_key[1])) {
		fprintf(stderr, "Bad AES shared key\n");
		return -1;
	}

	if (net_setup_listeners())
		return -1;

	for (p = LIST_FIRST(&cfgstate.peerlist); p; p = LIST_NEXT(p, link)) {
		p->socket = -1;
		SIMPLEQ_INIT(&p->msgs);
	}

	net_check_peers(0);
	return 0;
}

static void
net_enqueue(struct syncpeer *p, struct msg *m)
{
	struct qmsg	*qm;

	if (p->socket < 0)
		return;

	qm = calloc(1, sizeof *qm);
	if (!qm) {
		log_err("net_enqueue: calloc()");
		return;
	}

	qm->msg = m;
	m->refcnt++;

	SIMPLEQ_INSERT_TAIL(&p->msgs, qm, next);
	return;
}

/*
 * Queue a message for transmission to a particular peer,
 * or to all peers if no peer is specified.
 */
int
net_queue(struct syncpeer *p0, u_int32_t msgtype, u_int8_t *buf, u_int32_t len)
{
	struct syncpeer *p = p0;
	struct msg	*m;
	SHA_CTX		 ctx;
	u_int8_t	 hash[SHA_DIGEST_LENGTH];
	u_int8_t	 iv[AES_IV_LEN], tmp_iv[AES_IV_LEN];
	u_int32_t	 v, padlen = 0;
	int		 i, offset;

	m = calloc(1, sizeof *m);
	if (!m) {
		log_err("net_queue: calloc()");
		free(buf);
		return -1;
	}

	/* Generate hash */
	SHA1_Init(&ctx);
	SHA1_Update(&ctx, buf, len);
	SHA1_Final(hash, &ctx);
	dump_buf(2, hash, sizeof hash, "net_queue: computed hash");

	/* Padding required? */
	i = len % AES_IV_LEN;
	if (i) {
		u_int8_t *pbuf;
		i = AES_IV_LEN - i;
		pbuf = realloc(buf, len + i);
		if (!pbuf) {
			log_err("net_queue: realloc()");
			free(buf);
			free(m);
			return -1;
		}
		padlen = i;
		while (i > 0)
			pbuf[len++] = (u_int8_t)i--;
		buf = pbuf;
	}

	/* Get random IV */
	for (i = 0; (size_t)i <= sizeof iv - sizeof v; i += sizeof v) {
		v = arc4random();
		memcpy(&iv[i], &v, sizeof v);
	}
	dump_buf(2, iv, sizeof iv, "net_queue: IV");
	memcpy(tmp_iv, iv, sizeof tmp_iv);

	/* Encrypt */
	dump_buf(2, buf, len, "net_queue: pre encrypt");
	AES_cbc_encrypt(buf, buf, len, &aes_key[0], tmp_iv, AES_ENCRYPT);
	dump_buf(2, buf, len, "net_queue: post encrypt");

	/* Allocate send buffer */
	m->len = len + sizeof iv + sizeof hash + 3 * sizeof(u_int32_t);
	m->buf = malloc(m->len);
	if (!m->buf) {
		free(m);
		free(buf);
		log_err("net_queue: calloc()");
		return -1;
	}
	offset = 0;

	/* Fill it (order must match parsing code in net_read()) */
	v = htonl(m->len - sizeof(u_int32_t));
	memcpy(m->buf + offset, &v, sizeof v);
	offset += sizeof v;
	v = htonl(msgtype);
	memcpy(m->buf + offset, &v, sizeof v);
	offset += sizeof v;
	v = htonl(padlen);
	memcpy(m->buf + offset, &v, sizeof v);
	offset += sizeof v;
	memcpy(m->buf + offset, hash, sizeof hash);
	offset += sizeof hash;
	memcpy(m->buf + offset, iv, sizeof iv);
	offset += sizeof iv;
	memcpy(m->buf + offset, buf, len);
	free(buf);

	if (p)
		net_enqueue(p, m);
	else
		for (p = LIST_FIRST(&cfgstate.peerlist); p;
		     p = LIST_NEXT(p, link))
			net_enqueue(p, m);

	if (!m->refcnt) {
		free(m->buf);
		free(m);
	}

	return 0;
}

/* Set all write pending filedescriptors. */
int
net_set_pending_wfds(fd_set *fds)
{
	struct syncpeer *p;
	int		max_fd = -1;

	for (p = LIST_FIRST(&cfgstate.peerlist); p; p = LIST_NEXT(p, link))
		if (p->socket > -1 && SIMPLEQ_FIRST(&p->msgs)) {
			FD_SET(p->socket, fds);
			if (p->socket > max_fd)
				max_fd = p->socket;
		}
	return max_fd + 1;
}

/*
 * Set readable filedescriptors. They are basically the same as for write,
 * plus the listening socket.
 */
int
net_set_rfds(fd_set *fds)
{
	struct syncpeer *p;
	int		i, max_fd = -1;

	for (p = LIST_FIRST(&cfgstate.peerlist); p; p = LIST_NEXT(p, link)) {
		if (p->socket > -1)
			FD_SET(p->socket, fds);
		if (p->socket > max_fd)
			max_fd = p->socket;
	}
	for (i = 0; listeners[i] != -1; i++) {
		FD_SET(listeners[i], fds);
		if (listeners[i] > max_fd)
			max_fd = listeners[i];
	}
	return max_fd + 1;
}

static void
net_accept(int accept_socket)
{
	struct sockaddr_storage	 sa_storage, sa_storage2;
	struct sockaddr		*sa = (struct sockaddr *)&sa_storage;
	struct sockaddr		*sa2 = (struct sockaddr *)&sa_storage2;
	struct sockaddr_in	*sin, *sin2;
	struct sockaddr_in6	*sin6, *sin62;
	struct syncpeer		*p;
	socklen_t		 socklen;
	int			 s, found;

	/* Accept a new incoming connection */
	socklen = sizeof sa_storage;
	memset(&sa_storage, 0, socklen);
	memset(&sa_storage2, 0, socklen);
	s = accept(accept_socket, sa, &socklen);
	if (s > -1) {
		/* Setup the syncpeer structure */
		found = 0;
		for (p = LIST_FIRST(&cfgstate.peerlist); p && !found;
		     p = LIST_NEXT(p, link)) {

			/* Match? */
			if (net_set_sa(sa2, p->name, 0))
				continue;
			if (sa->sa_family != sa2->sa_family)
				continue;
			if (sa->sa_family == AF_INET) {
				sin = (struct sockaddr_in *)sa;
				sin2 = (struct sockaddr_in *)sa2;
				if (memcmp(&sin->sin_addr, &sin2->sin_addr,
					sizeof(struct in_addr)))
					continue;
			} else {
				sin6 = (struct sockaddr_in6 *)sa;
				sin62 = (struct sockaddr_in6 *)sa2;
				if (memcmp(&sin6->sin6_addr, &sin62->sin6_addr,
					sizeof(struct in6_addr)))
					continue;
			}
			/* Match! */
			found++;
			p->socket = s;
			log_msg(1, "net: peer \"%s\" connected", p->name);
			if (cfgstate.runstate == MASTER)
				timer_add("pfkey_snap", 2, pfkey_snapshot, p);
		}
		if (!found) {
			log_msg(1, "net: found no matching peer for accepted "
			    "socket, closing.");
			close(s);
		}
	} else if (errno != EWOULDBLOCK && errno != EINTR &&
	    errno != ECONNABORTED)
		log_err("net: accept()");
}

void
net_handle_messages(fd_set *fds)
{
	struct syncpeer *p;
	u_int8_t	*msg;
	u_int32_t	 msgtype, msglen;
	int		 i;

	for (i = 0; listeners[i] != -1; i++)
		if (FD_ISSET(listeners[i], fds))
			net_accept(listeners[i]);

	for (p = LIST_FIRST(&cfgstate.peerlist); p; p = LIST_NEXT(p, link)) {
		if (p->socket < 0 || !FD_ISSET(p->socket, fds))
			continue;
		msg = net_read(p, &msgtype, &msglen);
		if (!msg)
			continue;

		log_msg(2, "net_handle_messages: got msg type %u len %u from "
		    "peer %s", msgtype, msglen, p->name);

		switch (msgtype) {
		case MSG_SYNCCTL:
			net_ctl_handle_msg(p, msg, msglen);
			free(msg);
			break;

		case MSG_PFKEYDATA:
			if (p->runstate != MASTER ||
			    cfgstate.runstate == MASTER) {
				log_msg(1, "net: got PFKEY message from "
				    "non-MASTER peer");
				free(msg);
				if (cfgstate.runstate == MASTER)
					net_ctl_send_state(p);
				else
					net_ctl_send_error(p, 0);
			} else if (pfkey_queue_message(msg, msglen))
				free(msg);
			break;

		default:
			log_msg(0, "net: got unknown message type %u len %u "
			    "from peer %s", msgtype, msglen, p->name);
			free(msg);
			net_ctl_send_error(p, 0);
		}
	}
}

void
net_send_messages(fd_set *fds)
{
	struct syncpeer *p;
	struct qmsg	*qm;
	struct msg	*m;
	ssize_t		 r;

	for (p = LIST_FIRST(&cfgstate.peerlist); p; p = LIST_NEXT(p, link)) {
		if (p->socket < 0 || !FD_ISSET(p->socket, fds))
			continue;
		qm = SIMPLEQ_FIRST(&p->msgs);
		if (!qm) {
			/* XXX Log */
			continue;
		}
		m = qm->msg;

		log_msg(2, "net_send_messages: msg %p len %u ref %d "
		    "to peer %s", m, m->len, m->refcnt, p->name);

		/* write message */
		r = write(p->socket, m->buf, m->len);
		if (r == -1) {
			net_disconnect_peer(p);
			log_msg(0, "net_send_messages: write() failed, "
			    "peer disconnected");
		} else if (r < (ssize_t)m->len) {
			/* retransmit later */
			continue;
		}

		/* cleanup */
		SIMPLEQ_REMOVE_HEAD(&p->msgs, next);
		free(qm);

		if (--m->refcnt < 1) {
			log_msg(2, "net_send_messages: freeing msg %p", m);
			free(m->buf);
			free(m);
		}
	}
	return;
}

void
net_disconnect_peer(struct syncpeer *p)
{
	if (p->socket > -1) {
		log_msg(1, "net_disconnect_peer: peer \"%s\" removed",
		    p->name);
		close(p->socket);
	}
	p->socket = -1;
}

void
net_shutdown(void)
{
	struct syncpeer *p;
	struct qmsg	*qm;
	struct msg	*m;
	int		 i;

	while ((p = LIST_FIRST(&cfgstate.peerlist))) {
		while ((qm = SIMPLEQ_FIRST(&p->msgs))) {
			SIMPLEQ_REMOVE_HEAD(&p->msgs, next);
			m = qm->msg;
			if (--m->refcnt < 1) {
				free(m->buf);
				free(m);
			}
			free(qm);
		}
		net_disconnect_peer(p);
		free(p->sa);
		free(p->name);
		LIST_REMOVE(p, link);
		cfgstate.peercnt--;
		free(p);
	}

	if (listeners) {
		for (i = 0; listeners[i] != -1; i++)
			close(listeners[i]);
		free(listeners);
		listeners = 0;
	}
}

/*
 * Helper functions (local) below here.
 */

static u_int8_t *
net_read(struct syncpeer *p, u_int32_t *msgtype, u_int32_t *msglen)
{
	u_int8_t	*msg, *blob, *rhash, *iv, hash[SHA_DIGEST_LENGTH];
	u_int32_t	 v, blob_len, pos = 0;
	int		 padlen = 0, offset = 0;
	ssize_t 	 r;
	SHA_CTX		 ctx;

	/* Read blob length */
	r = read(p->socket, &v, sizeof v);
	if (r != (ssize_t)sizeof v) {
		if (r < 1)
			net_disconnect_peer(p);
		return NULL;
	}

	blob_len = ntohl(v);
	if (blob_len < sizeof hash + AES_IV_LEN + 2 * sizeof(u_int32_t))
		return NULL;
	*msglen = blob_len - sizeof hash - AES_IV_LEN - 2 * sizeof(u_int32_t);
	if (*msglen < MSG_MINLEN || *msglen > MSG_MAXLEN)
		return NULL;

	/* Read message blob */
	blob = malloc(blob_len);
	if (!blob) {
		log_err("net_read: malloc()");
		return NULL;
	}

	while (blob_len > pos) {
		switch (r = read(p->socket, blob + pos, blob_len - pos)) {
		case -1:
			if (errno == EINTR || errno == EAGAIN)
				continue;
                        /* FALLTHROUGH */
		case 0:
			net_disconnect_peer(p);
			free(blob);
			return NULL;
                        /* NOTREACHED */
		default:
			pos += r;
		}
	}

	offset = 0;
	memcpy(&v, blob + offset, sizeof v);
	*msgtype = ntohl(v);
	offset += sizeof v;

	if (*msgtype > MSG_MAXTYPE) {
		free(blob);
		return NULL;
	}

	memcpy(&v, blob + offset, sizeof v);
	padlen = ntohl(v);
	offset += sizeof v;

	rhash = blob + offset;
	iv    = rhash + sizeof hash;
	msg = malloc(*msglen);
	if (!msg) {
		free(blob);
		return NULL;
	}
	memcpy(msg, iv + AES_IV_LEN, *msglen);

	dump_buf(2, rhash, sizeof hash, "net_read: got hash");
	dump_buf(2, iv, AES_IV_LEN, "net_read: got IV");
	dump_buf(2, msg, *msglen, "net_read: pre decrypt");
	AES_cbc_encrypt(msg, msg, *msglen, &aes_key[1], iv, AES_DECRYPT);
	dump_buf(2, msg, *msglen, "net_read: post decrypt");
	*msglen -= padlen;

	SHA1_Init(&ctx);
	SHA1_Update(&ctx, msg, *msglen);
	SHA1_Final(hash, &ctx);
	dump_buf(2, hash, sizeof hash, "net_read: computed hash");

	if (memcmp(hash, rhash, sizeof hash) != 0) {
		free(blob);
		free(msg);
		log_msg(0, "net_read: got bad message (typo in shared key?)");
		return NULL;
	}
	free(blob);
	return msg;
}

static int
net_set_sa(struct sockaddr *sa, char *name, in_port_t port)
{
	struct sockaddr_in	*sin = (struct sockaddr_in *)sa;
	struct sockaddr_in6	*sin6 = (struct sockaddr_in6 *)sa;

	if (!name) {
		/* XXX Assume IPv4 */
		sa->sa_family = AF_INET;
		sin->sin_port = htons(port);
		sin->sin_len = sizeof *sin;
		return 0;
	}

	if (inet_pton(AF_INET, name, &sin->sin_addr) == 1) {
		sa->sa_family = AF_INET;
		sin->sin_port = htons(port);
		sin->sin_len = sizeof *sin;
		return 0;
	}

	if (inet_pton(AF_INET6, name, &sin6->sin6_addr) == 1) {
		sa->sa_family = AF_INET6;
		sin6->sin6_port = htons(port);
		sin6->sin6_len = sizeof *sin6;
		return 0;
	}

	return -1;
}

static void
got_sigalrm(int s)
{
	return;
}

void
net_connect(void)
{
	struct itimerval	iv;
	struct syncpeer		*p;

	signal(SIGALRM, got_sigalrm);
	memset(&iv, 0, sizeof iv);
	iv.it_value.tv_sec = 5;
	iv.it_interval.tv_sec = 5;
	setitimer(ITIMER_REAL, &iv, NULL);

	for (p = LIST_FIRST(&cfgstate.peerlist); p; p = LIST_NEXT(p, link)) {
		if (p->socket > -1)
			continue;
		if (!p->sa) {
			p->sa = calloc(1, sizeof(struct sockaddr_storage));
			if (!p->sa)
				return;
			if (net_set_sa(p->sa, p->name, cfgstate.listen_port))
				continue;
		}
		p->socket = socket(p->sa->sa_family, SOCK_STREAM, 0);
		if (p->socket < 0) {
			log_err("peer \"%s\": socket()", p->name);
			continue;
		}
		if (connect(p->socket, p->sa, p->sa->sa_len)) {
			log_msg(1, "net_connect: peer \"%s\" not ready yet",
			    p->name);
			net_disconnect_peer(p);
			continue;
		}
		if (net_ctl_send_state(p)) {
			log_msg(0, "net_connect: peer \"%s\" failed", p->name);
			net_disconnect_peer(p);
			continue;
		}
		log_msg(1, "net_connect: peer \"%s\" connected, fd %d",
		    p->name, p->socket);

		/* Schedule a pfkey sync to the newly connected peer. */
		if (cfgstate.runstate == MASTER)
			timer_add("pfkey_snapshot", 2, pfkey_snapshot, p);
	}

	timerclear(&iv.it_value);
	timerclear(&iv.it_interval);
	setitimer(ITIMER_REAL, &iv, NULL);
	signal(SIGALRM, SIG_IGN);

	return;
}

static void
net_check_peers(void *arg)
{
	net_connect();
	(void)timer_add("peer recheck", 600, net_check_peers, 0);
}
