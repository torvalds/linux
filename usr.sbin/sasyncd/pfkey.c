/*	$OpenBSD: pfkey.c,v 1.29 2018/06/28 02:37:26 gsoares Exp $	*/

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
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <net/pfkeyv2.h>
#include <netinet/ip_ipsp.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sasyncd.h"
#include "monitor.h"
#include "net.h"

struct pfkey_msg
{
	SIMPLEQ_ENTRY(pfkey_msg)	next;

	u_int8_t	*buf;
	u_int32_t	 len;
};

SIMPLEQ_HEAD(, pfkey_msg)		pfkey_msglist;

static const char *msgtypes[] = {
	"RESERVED", "GETSPI", "UPDATE", "ADD", "DELETE", "GET", "ACQUIRE",
	"REGISTER", "EXPIRE", "FLUSH", "DUMP", "X_PROMISC", "X_ADDFLOW",
	"X_DELFLOW", "X_GRPSPIS", "X_ASKPOLICY", "X_SPDDUMP"
};

#define CHUNK sizeof(u_int64_t)

static const char *pfkey_print_type(struct sadb_msg *);

static int
pfkey_write(u_int8_t *buf, ssize_t len)
{
	struct sadb_msg *msg = (struct sadb_msg *)buf;
	ssize_t n;

	if (cfgstate.pfkey_socket == -1)
		return 0;

	do {
		n = write(cfgstate.pfkey_socket, buf, len);
	} while (n == -1 && (errno == EAGAIN || errno == EINTR));
	if (n == -1) {
		log_err("pfkey: msg %s write() failed on socket %d",
		    pfkey_print_type(msg), cfgstate.pfkey_socket);
		return -1;
	}

	return 0;
}

int
pfkey_set_promisc(void)
{
	struct sadb_msg	msg;
	static u_int32_t seq = 1;

	memset(&msg, 0, sizeof msg);
	msg.sadb_msg_version = PF_KEY_V2;
	msg.sadb_msg_seq = seq++;
	msg.sadb_msg_satype = 1; /* Special; 1 to enable, 0 to disable */
	msg.sadb_msg_type = SADB_X_PROMISC;
	msg.sadb_msg_pid = getpid();
	msg.sadb_msg_len = sizeof msg / CHUNK;

	return pfkey_write((u_int8_t *)&msg, sizeof msg);
}

/* Send a SADB_FLUSH PFKEY message to peer 'p' */
static void
pfkey_send_flush(struct syncpeer *p)
{
	struct sadb_msg *m = calloc(1, sizeof *m);
	static u_int32_t seq = 1;

	if (m) {
		m->sadb_msg_version = PF_KEY_V2;
		m->sadb_msg_seq = seq++;
		m->sadb_msg_type = SADB_FLUSH;
		m->sadb_msg_satype = SADB_SATYPE_UNSPEC;
		m->sadb_msg_pid = getpid();
		m->sadb_msg_len = sizeof *m / CHUNK;

		log_msg(2, "pfkey_send_flush: sending FLUSH to peer %s",
		    p->name);
		net_queue(p, MSG_PFKEYDATA, (u_int8_t *)m, sizeof *m);
	}
}

static const char *
pfkey_print_type(struct sadb_msg *msg)
{
	static char	uk[20];

	if (msg->sadb_msg_type < sizeof msgtypes / sizeof msgtypes[0])
		return msgtypes[msg->sadb_msg_type];
	else {
		snprintf(uk, sizeof uk, "<unknown(%d)>", msg->sadb_msg_type);
		return uk;
	}
}

static struct sadb_ext *
pfkey_find_ext(struct sadb_msg *msg, u_int16_t type)
{
	struct sadb_ext	*ext;
	u_int8_t	*e;

	for (e = (u_int8_t *)msg + sizeof *msg;
	     e < (u_int8_t *)msg + msg->sadb_msg_len * CHUNK;
	     e += ext->sadb_ext_len * CHUNK) {
		ext = (struct sadb_ext *)e;
		if (ext->sadb_ext_len == 0)
			break;
		if (ext->sadb_ext_type != type)
			continue;
		return ext;
	}
	return NULL;
}

/* Return: 0 means ok to sync msg, 1 means to skip it */
static int
pfkey_msg_filter(struct sadb_msg *msg)
{
	struct sockaddr		*src = 0, *dst = 0;
	struct syncpeer		*p;
	struct sadb_ext		*ext;
	u_int8_t		*max;

	switch (msg->sadb_msg_type) {
	case SADB_X_PROMISC:
	case SADB_DUMP:
	case SADB_GET:
	case SADB_GETSPI:
	case SADB_ACQUIRE:
	case SADB_X_ASKPOLICY:
	case SADB_REGISTER:
		/* Some messages should not be synced. */
		return 1;

	case SADB_ADD:
		/* No point in syncing LARVAL SAs */
		if (pfkey_find_ext(msg, SADB_EXT_KEY_ENCRYPT) == 0)
			return 1;
	case SADB_DELETE:
	case SADB_X_ADDFLOW:
	case SADB_X_DELFLOW:
	case SADB_EXPIRE:
		/* Continue below */
		break;
	case SADB_FLUSH:
		if ((cfgstate.flags & FM_MASK) == FM_NEVER)
			return 1;
		break;
	default:
		return 0;
	}

	if ((cfgstate.flags & SKIP_LOCAL_SAS) == 0)
		return 0;

	/* SRC or DST address of this msg must not be one of our peers. */
	ext = pfkey_find_ext(msg, SADB_EXT_ADDRESS_SRC);
	if (ext)
		src = (struct sockaddr *)((struct sadb_address *)ext + 1);
	ext = pfkey_find_ext(msg, SADB_EXT_ADDRESS_DST);
	if (ext)
		dst = (struct sockaddr *)((struct sadb_address *)ext + 1);
	if (!src && !dst)
		return 0;

	max = (u_int8_t *)msg + msg->sadb_msg_len * CHUNK;
	if (src && ((u_int8_t *)src + src->sa_len) > max)
		return 1;
	if (dst && ((u_int8_t *)dst + dst->sa_len) > max)
		return 1;

	/* Found SRC or DST, check it against our peers */
	for (p = LIST_FIRST(&cfgstate.peerlist); p; p = LIST_NEXT(p, link)) {
		if (p->socket < 0 || p->sa->sa_family !=
		    (src ? src->sa_family : dst->sa_family))
			continue;

		switch (p->sa->sa_family) {
		case AF_INET:
			if (src && memcmp(
			    &((struct sockaddr_in *)p->sa)->sin_addr.s_addr,
			    &((struct sockaddr_in *)src)->sin_addr.s_addr,
			    sizeof(struct in_addr)) == 0)
				return 1;
			if (dst && memcmp(
			    &((struct sockaddr_in *)p->sa)->sin_addr.s_addr,
			    &((struct sockaddr_in *)dst)->sin_addr.s_addr,
			    sizeof(struct in_addr)) == 0)
				return 1;
			break;
		case AF_INET6:
			if (src &&
			    memcmp(&((struct sockaddr_in6 *)p->sa)->sin6_addr,
			    &((struct sockaddr_in6 *)src)->sin6_addr,
			    sizeof(struct in_addr)) == 0)
				return 1;
			if (dst &&
			    memcmp(&((struct sockaddr_in6 *)p->sa)->sin6_addr,
			    &((struct sockaddr_in6 *)dst)->sin6_addr,
			    sizeof(struct in_addr)) == 0)
				return 1;
			break;
		}
	}
	return 0;
}

static int
pfkey_handle_message(struct sadb_msg *m)
{
	struct sadb_msg	*msg = m;

	/*
	 * Report errors, but ignore for DELETE (both isakmpd and kernel will
	 * expire the SA, if the kernel is first, DELETE returns failure).
	 */
	if (msg->sadb_msg_errno && msg->sadb_msg_type != SADB_DELETE &&
	    msg->sadb_msg_pid == (u_int32_t)getpid()) {
		errno = msg->sadb_msg_errno;
		log_msg(1, "pfkey error (%s)", pfkey_print_type(msg));
	}

	/* We only want promiscuous messages here, skip all others. */
	if (msg->sadb_msg_type != SADB_X_PROMISC ||
	    (msg->sadb_msg_len * CHUNK) < 2 * sizeof *msg) {
		free(m);
		return 0;
	}
	/* Move next msg to start of the buffer. */
	msg++;

	/*
	 * We should not listen to PFKEY messages when we are not running
	 * as MASTER, or the pid is our own.
	 */
	if (cfgstate.runstate != MASTER ||
	    msg->sadb_msg_pid == (u_int32_t)getpid()) {
		free(m);
		return 0;
	}

	if (pfkey_msg_filter(msg)) {
		free(m);
		return 0;
	}

	switch (msg->sadb_msg_type) {
	case SADB_UPDATE:
		/*
		 * Tweak -- the peers do not have a larval SA to update, so
		 * instead we ADD it here.
		 */
		msg->sadb_msg_type = SADB_ADD;
		/* FALLTHROUGH */

	default:
		/* Pass the rest along to our peers. */
		memmove(m, msg, msg->sadb_msg_len * CHUNK); /* for realloc */
		return net_queue(NULL, MSG_PFKEYDATA, (u_int8_t *)m,
		    m->sadb_msg_len * CHUNK);
	}

	return 0;
}

static int
pfkey_read(void)
{
	struct sadb_msg  hdr, *msg;
	u_int8_t	*data;
	ssize_t		 datalen;
	int		 fd = cfgstate.pfkey_socket;

	if (recv(fd, &hdr, sizeof hdr, MSG_PEEK) != sizeof hdr) {
		log_err("pfkey_read: recv() failed");
		return -1;
	}
	datalen = hdr.sadb_msg_len * CHUNK;
	data = reallocarray(NULL, hdr.sadb_msg_len, CHUNK);
	if (!data) {
		log_err("pfkey_read: malloc(%lu) failed", datalen);
		return -1;
	}
	msg = (struct sadb_msg *)data;

	if (read(fd, data, datalen) != datalen) {
		log_err("pfkey_read: read() failed, %lu bytes", datalen);
		free(data);
		return -1;
	}

	return pfkey_handle_message(msg);
}

int
pfkey_init(int reinit)
{
	int fd;

	fd = socket(PF_KEY, SOCK_RAW, PF_KEY_V2);
	if (fd == -1) {
		perror("failed to open PF_KEY socket");
		return -1;
	}
	cfgstate.pfkey_socket = fd;

	if (cfgstate.runstate == MASTER)
		pfkey_set_promisc();

	if (reinit)
		return (fd > -1 ? 0 : -1);

	SIMPLEQ_INIT(&pfkey_msglist);
	return 0;
}

void
pfkey_set_rfd(fd_set *fds)
{
	if (cfgstate.pfkey_socket != -1)
		FD_SET(cfgstate.pfkey_socket, fds);
}

void
pfkey_set_pending_wfd(fd_set *fds)
{
	if (cfgstate.pfkey_socket != -1 && SIMPLEQ_FIRST(&pfkey_msglist))
		FD_SET(cfgstate.pfkey_socket, fds);
}

void
pfkey_read_message(fd_set *fds)
{
	if (cfgstate.pfkey_socket != -1)
		if (FD_ISSET(cfgstate.pfkey_socket, fds))
			(void)pfkey_read();
}

void
pfkey_send_message(fd_set *fds)
{
	struct pfkey_msg *pmsg = SIMPLEQ_FIRST(&pfkey_msglist);

	if (!pmsg || !FD_ISSET(cfgstate.pfkey_socket, fds))
		return;

	if (cfgstate.pfkey_socket == -1)
		if (pfkey_init(1)) /* Reinit socket */
			return;

	(void)pfkey_write(pmsg->buf, pmsg->len);

	SIMPLEQ_REMOVE_HEAD(&pfkey_msglist, next);
	free(pmsg->buf);
	free(pmsg);

	return;
}

int
pfkey_queue_message(u_int8_t *data, u_int32_t datalen)
{
	struct pfkey_msg	*pmsg;
	struct sadb_msg		*sadb = (struct sadb_msg *)data;
	static u_int32_t	 seq = 1;

	pmsg = malloc(sizeof *pmsg);
	if (!pmsg) {
		log_err("malloc()");
		return -1;
	}
	memset(pmsg, 0, sizeof *pmsg);

	pmsg->buf = data;
	pmsg->len = datalen;

	sadb->sadb_msg_pid = getpid();
	sadb->sadb_msg_seq = seq++;
	log_msg(2, "pfkey_queue_message: pfkey %s len %zu seq %u",
	    pfkey_print_type(sadb), sadb->sadb_msg_len * CHUNK,
	    sadb->sadb_msg_seq);

	SIMPLEQ_INSERT_TAIL(&pfkey_msglist, pmsg, next);
	return 0;
}

void
pfkey_shutdown(void)
{
	struct pfkey_msg *p = SIMPLEQ_FIRST(&pfkey_msglist);

	while ((p = SIMPLEQ_FIRST(&pfkey_msglist))) {
		SIMPLEQ_REMOVE_HEAD(&pfkey_msglist, next);
		free(p->buf);
		free(p);
	}

	if (cfgstate.pfkey_socket > -1)
		close(cfgstate.pfkey_socket);
}

/* ------------------------------------------------------------------------- */

void
pfkey_snapshot(void *v)
{
	struct syncpeer		*p = (struct syncpeer *)v;
	struct sadb_msg		*m;
	u_int8_t		*sadb, *spd, *max, *next, *sendbuf;
	u_int32_t		 sadbsz, spdsz;

	if (!p)
		return;

	if (monitor_get_pfkey_snap(&sadb, &sadbsz, &spd, &spdsz)) {
		log_msg(0, "pfkey_snapshot: failed to get pfkey snapshot");
		return;
	}

	/* XXX needs moving if snapshot is called more than once per peer */
	if ((cfgstate.flags & FM_MASK) == FM_STARTUP)
		pfkey_send_flush(p);

	/* Parse SADB data */
	if (sadbsz && sadb) {
		dump_buf(2, sadb, sadbsz, "pfkey_snapshot: SADB data");
		max = sadb + sadbsz;
		for (next = sadb; next < max;
		     next += m->sadb_msg_len * CHUNK) {
			m = (struct sadb_msg *)next;
			if (m->sadb_msg_len == 0)
				break;

			/* Tweak and send this SA to the peer. */
			m->sadb_msg_type = SADB_ADD;

			if (pfkey_msg_filter(m))
				continue;

			/* Allocate msgbuffer, net_queue() will free it. */
			sendbuf = calloc(m->sadb_msg_len, CHUNK);
			if (sendbuf) {
				memcpy(sendbuf, m, m->sadb_msg_len * CHUNK);
				net_queue(p, MSG_PFKEYDATA, sendbuf,
				    m->sadb_msg_len * CHUNK);
				log_msg(2, "pfkey_snapshot: sync SA %p len %zu "
				    "to peer %s", m,
				    m->sadb_msg_len * CHUNK, p->name);
			}
		}
		freezero(sadb, sadbsz);
	}

	/* Parse SPD data */
	if (spdsz && spd) {
		dump_buf(2, spd, spdsz, "pfkey_snapshot: SPD data");
		max = spd + spdsz;
		for (next = spd; next < max; next += m->sadb_msg_len * CHUNK) {
			m = (struct sadb_msg *)next;
			if (m->sadb_msg_len == 0)
				break;

			/* Tweak msg type. */
			m->sadb_msg_type = SADB_X_ADDFLOW;

			if (pfkey_msg_filter(m))
				continue;

			/* Allocate msgbuffer, freed by net_queue(). */
			sendbuf = calloc(m->sadb_msg_len, CHUNK);
			if (sendbuf) {
				memcpy(sendbuf, m, m->sadb_msg_len * CHUNK);
				net_queue(p, MSG_PFKEYDATA, sendbuf,
				    m->sadb_msg_len * CHUNK);
				log_msg(2, "pfkey_snapshot: sync FLOW %p len "
				    "%zu to peer %s", m,
				    m->sadb_msg_len * CHUNK, p->name);
			}
		}
		/* Cleanup. */
		freezero(spd, spdsz);
	}

	net_ctl_send_endsnap(p);
	return;
}
