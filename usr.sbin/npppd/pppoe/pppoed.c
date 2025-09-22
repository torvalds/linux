/*	$OpenBSD: pppoed.c,v 1.26 2025/02/03 07:46:06 yasuoka Exp $	*/

/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
 * All rights reserved.
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
 */
/**@file
 * This file provides the PPPoE(RFC2516) server(access concentrator)
 * implementation.
 * $Id: pppoed.c,v 1.26 2025/02/03 07:46:06 yasuoka Exp $
 */
#include <sys/param.h>	/* ALIGN */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <net/if.h>
#include <net/if_types.h>
#if defined(__NetBSD__)
#include <net/if_ether.h>
#else
#include <netinet/if_ether.h>
#endif
#include <net/if_dl.h>
#include <net/ethertypes.h>
#include <net/bpf.h>
#include <endian.h>
#include <string.h>
#include <syslog.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <event.h>
#include <signal.h>
#include <stdlib.h>
#include <ifaddrs.h>
#include <stdarg.h>
#include <errno.h>

#include "debugutil.h"
#include "slist.h"
#include "bytebuf.h"
#include "hash.h"
#include "privsep.h"

#include "pppoe.h"
#include "pppoe_local.h"

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))

static int pppoed_seqno = 0;

#ifdef	PPPOED_DEBUG
#define	PPPOED_ASSERT(x)	ASSERT(x)
#define	PPPOED_DBG(x)	pppoed_log x
#else
#define	PPPOED_ASSERT(x)
#define	PPPOED_DBG(x)
#endif

static void      pppoed_log (pppoed *, int, const char *, ...) __printflike(3,4);
static void      pppoed_listener_init(pppoed *, pppoed_listener *);
static int       pppoed_output (pppoed_listener *, u_char *, u_char *, int);
static int       pppoed_listener_start (pppoed_listener *, int);
static void      pppoed_io_event (int, short, void *);
static void      pppoed_input (pppoed_listener *, uint8_t [ETHER_ADDR_LEN], int, u_char *, int);
static void      pppoed_recv_PADR (pppoed_listener *, uint8_t [ETHER_ADDR_LEN], slist *);
static void      pppoed_recv_PADI (pppoed_listener *, uint8_t [ETHER_ADDR_LEN], slist *);
static int       session_id_cmp (void *, void *);
static uint32_t  session_id_hash (void *, size_t);

#ifdef PPPOE_TEST
static void      pppoed_on_sigterm (int, short, void *);
static void      usage (void);
#endif
static const char *pppoe_code_string(int);
#ifdef	PPPOED_DEBUG
static const char *pppoe_tag_string(int);
#endif

/*
 * daemon
 */

/* initialize PPPoE daemon */
int
pppoed_init(pppoed *_this)
{
	int i, off, id;

	memset(_this, 0, sizeof(pppoed));
	_this->id = pppoed_seqno++;

	if ((_this->session_hash = hash_create(
	    (int (*) (const void *, const void *))session_id_cmp,
	    (uint32_t (*) (const void *, int))session_id_hash,
	    PPPOE_SESSION_HASH_SIZ)) == NULL) {
		pppoed_log(_this, LOG_ERR, "hash_create() failed on %s(): %m",
		    __func__);
		goto fail;
	}

	slist_init(&_this->session_free_list);
	if (slist_add(&_this->session_free_list,
	    (void *)PPPOED_SESSION_SHUFFLE_MARK) == NULL) {
		pppoed_log(_this, LOG_ERR, "slist_add() failed on %s(): %m",
		    __func__);
		goto fail;
	}

	/* XXX initialize hash of cookies */
	if ((_this->acookie_hash = hash_create(
	    (int (*) (const void *, const void *))session_id_cmp,
	    (uint32_t (*) (const void *, int))session_id_hash,
	    PPPOE_SESSION_HASH_SIZ)) == NULL) {
		pppoed_log(_this, LOG_WARNING,
		    "hash_create() failed on %s(): %m", __func__);
		pppoed_log(_this, LOG_WARNING, "hash_create() failed on %s(): "
		    "ac-cookie hash create failed.", __func__);
		_this->acookie_hash = NULL;
	}
	_this->acookie_next = arc4random();

#if PPPOE_NSESSION > 0xffff
#error PPPOE_NSESSION must be less than 65536
#endif
	off = arc4random() & 0xffff;
	for (i = 0; i < PPPOE_NSESSION; i++) {
		id = (i + off) & 0xffff;
		if (id == 0)
			id = (off - 1) & 0xffff;
		if (slist_add(&_this->session_free_list, (void *)(intptr_t)id)
		    == NULL) {
			pppoed_log(_this, LOG_ERR,
			    "slist_add() failed on %s(): %m", __func__);
			goto fail;
		}
	}

	_this->state = PPPOED_STATE_INIT;

	return 0;
fail:
	pppoed_uninit(_this);
	return 1;
}

static void
pppoed_listener_init(pppoed *_this, pppoed_listener *listener)
{
	memset(listener, 0, sizeof(pppoed_listener));
	listener->bpf = -1;
	listener->self = _this;
	listener->index = PPPOED_LISTENER_INVALID_INDEX;
}

/* reload listener */
int
pppoed_reload_listeners(pppoed *_this)
{
	int rval = 0;

	if (_this->state == PPPOED_STATE_RUNNING &&
	    _this->listen_incomplete != 0)
		rval = pppoed_start(_this);

	return rval;
}

/*
 * Reject any packet except the packet to self and broadcasts,
 * as bpf(4) potentially receive packets for others.
 */
#define	REJECT_FOREIGN_ADDRESS 1

#define ETHER_FIRST_INT(e)	((e)[0]<<24|(e)[1]<<16|(e)[2]<<8|(e)[3])
#define ETHER_LAST_SHORT(e)	((e)[4]<<8|(e)[5])

static int
pppoed_listener_start(pppoed_listener *_this, int restart)
{
	int log_level;
	struct ifreq ifreq;
	int ival;
	int found;
	struct ifaddrs *ifa0, *ifa;
	struct sockaddr_dl *sdl;
	struct bpf_insn insns[] = {
	    /* check etyer type = PPPOEDESC or PPPOE */
		BPF_STMT(BPF_LD+BPF_H+BPF_ABS, 12),
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, ETHERTYPE_PPPOEDISC, 2, 0),
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, ETHERTYPE_PPPOE, 1, 0),
		BPF_STMT(BPF_RET+BPF_K, (u_int)0),
#ifndef	REJECT_FOREIGN_ADDRESS
		BPF_STMT(BPF_RET+BPF_K, (u_int)-1),
#else
	/* to ff:ff:ff:ff:ff:ff */
		BPF_STMT(BPF_LD+BPF_W+BPF_ABS, 0),
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, 0xffffffff, 0, 3),
		BPF_STMT(BPF_LD+BPF_H+BPF_ABS, 4),
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, 0xffff, 0, 1),
		BPF_STMT(BPF_RET+BPF_K, (u_int)-1),
	/* to self */
		BPF_STMT(BPF_LD+BPF_W+BPF_ABS, 0),
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K,
		    ETHER_FIRST_INT(_this->ether_addr), 0, 3),
		BPF_STMT(BPF_LD+BPF_H+BPF_ABS, 4),
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K,
		    ETHER_LAST_SHORT(_this->ether_addr), 0, 1),
		BPF_STMT(BPF_RET+BPF_K, (u_int)-1),
		BPF_STMT(BPF_RET+BPF_K, (u_int)0),
#endif
	};
	struct bpf_program bf_filter = {
		.bf_len = countof(insns),
		.bf_insns = insns
	};
	pppoed *_pppoed;

	if (restart == 0)
		log_level = LOG_ERR;
	else
		log_level = LOG_INFO;

	_pppoed = _this->self;

	ifa0 = NULL;
	if (getifaddrs(&ifa0) != 0) {
		pppoed_log(_pppoed, log_level,
		    "getifaddrs() failed on %s(): %m", __func__);
		return -1;
	}
	found = 0;
	for (ifa = ifa0; ifa != NULL; ifa = ifa->ifa_next) {
		sdl = (struct sockaddr_dl *)ifa->ifa_addr;
		if (sdl == NULL ||
		    sdl->sdl_family != AF_LINK || sdl->sdl_type != IFT_ETHER ||
		    sdl->sdl_alen != ETHER_ADDR_LEN)
			continue;
		if (strcmp(ifa->ifa_name, _this->listen_ifname) == 0) {
			memcpy(_this->ether_addr,
			    (caddr_t)LLADDR(sdl), ETHER_ADDR_LEN);
			found = 1;
			break;
		}
	}
	freeifaddrs(ifa0);
	if (!found) {
		pppoed_log(_pppoed, log_level, "%s is not available.",
		    _this->listen_ifname);
		goto fail;
	}

	if ((_this->bpf = priv_open("/dev/bpf", O_RDWR)) == -1) {
		pppoed_log(_pppoed, log_level, "Cannot open bpf: %m");
		goto fail;
	}

	ival = BPF_CAPTURE_SIZ;
	if (ioctl(_this->bpf, BIOCSBLEN, &ival) != 0) {
		pppoed_log(_pppoed, log_level, "ioctl(bpf, BIOCSBLEN(%d)): %m",
		    ival);
		goto fail;
	}
	ival = 1;
	if (ioctl(_this->bpf, BIOCIMMEDIATE, &ival) != 0) {
		pppoed_log(_pppoed, log_level, "Cannot start bpf on %s: %m",
		    _this->listen_ifname);
		goto fail;
	}

	/* bind interface */
	memset(&ifreq, 0, sizeof(ifreq));
	strlcpy(ifreq.ifr_name, _this->listen_ifname, sizeof(ifreq.ifr_name));
	if (ioctl(_this->bpf, BIOCSETIF, &ifreq) != 0) {
		pppoed_log(_pppoed, log_level, "Cannot start bpf on %s: %m",
		    _this->listen_ifname);
		goto fail;
	}

	/* set linklocal address */
#ifdef	REJECT_FOREIGN_ADDRESS
	insns[10].k = ETHER_FIRST_INT(_this->ether_addr);
	insns[12].k = ETHER_LAST_SHORT(_this->ether_addr);
#endif

	/* set filter */
	if (ioctl(_this->bpf, BIOCSETF, &bf_filter) != 0) {
		pppoed_log(_pppoed, log_level, "ioctl(bpf, BIOCSETF()): %m");
		goto fail;
	}

	event_set(&_this->ev_bpf, _this->bpf, EV_READ | EV_PERSIST,
	    pppoed_io_event, _this);
	event_add(&_this->ev_bpf, NULL);

	pppoed_log(_pppoed, LOG_INFO, "Listening on %s (PPPoE) [%s] "
	    "address=%02x:%02x:%02x:%02x:%02x:%02x", _this->listen_ifname,
	    _this->tun_name, _this->ether_addr[0], _this->ether_addr[1],
	    _this->ether_addr[2], _this->ether_addr[3], _this->ether_addr[4],
	    _this->ether_addr[5]);

	return 0;
fail:
	if (_this->bpf >= 0) {
		close(_this->bpf);
		_this->bpf = -1;
	}

	return 1;
}

/* start PPPoE daemon */
int
pppoed_start(pppoed *_this)
{
	int rval = 0;
	int nlistener_fail = 0;
	pppoed_listener *plistener;

	slist_itr_first(&_this->listener);
	while (slist_itr_has_next(&_this->listener)) {
		plistener = slist_itr_next(&_this->listener);
		PPPOED_ASSERT(plistener != NULL);
		if (plistener->bpf < 0) {
			if (pppoed_listener_start(plistener,
			    _this->listen_incomplete) != 0)
				nlistener_fail++;
		}
	}
	if (nlistener_fail > 0)
		_this->listen_incomplete = 1;
	else
		_this->listen_incomplete = 0;

	_this->state = PPPOED_STATE_RUNNING;

	return rval;
}

/* stop listener */
static void
pppoed_listener_stop(pppoed_listener *_this)
{
	pppoed *_pppoed;

	PPPOED_ASSERT(_this != NULL);
	_pppoed = _this->self;
	PPPOED_ASSERT(_pppoed != NULL);

	if (_this->bpf >= 0) {
		event_del(&_this->ev_bpf);
		close(_this->bpf);
		pppoed_log(_pppoed, LOG_INFO, "Shutdown %s (PPPoE) [%s] "
		    "address=%02x:%02x:%02x:%02x:%02x:%02x",
		    _this->listen_ifname, _this->tun_name,
		    _this->ether_addr[0], _this->ether_addr[1],
		    _this->ether_addr[2], _this->ether_addr[3],
		    _this->ether_addr[4], _this->ether_addr[5]);
		_this->bpf = -1;
	}
}

/* stop PPPoE daemon */
void
pppoed_stop(pppoed *_this)
{
	pppoed_listener *plistener;
	hash_link *hl;
	pppoe_session *session;

	if (!pppoed_is_running(_this))
		return;

	_this->state = PPPOED_STATE_STOPPED;
	if (_this->session_hash != NULL) {
		for (hl = hash_first(_this->session_hash);
		    hl != NULL;
		    hl = hash_next(_this->session_hash)) {
			session = (pppoe_session *)hl->item;
			pppoe_session_disconnect(session);
			pppoe_session_stop(session);
		}
	}
	for (slist_itr_first(&_this->listener);
	    slist_itr_has_next(&_this->listener);) {
		plistener = slist_itr_next(&_this->listener);
		pppoed_listener_stop(plistener);
		free(plistener);
		slist_itr_remove(&_this->listener);
	}
	PPPOED_DBG((_this, LOG_DEBUG, "Stopped"));
}

/* uninitialize (free) PPPoE daemon */
void
pppoed_uninit(pppoed *_this)
{
	if (_this->session_hash != NULL) {
		hash_free(_this->session_hash);
		_this->session_hash = NULL;
	}
	if (_this->acookie_hash != NULL) {
		hash_free(_this->acookie_hash);
		_this->acookie_hash = NULL;
	}
	slist_fini(&_this->session_free_list);
	/* listener themself has been released already */
	slist_fini(&_this->listener);
}

/* it is called when the PPPoE session was closed */
void
pppoed_pppoe_session_close_notify(pppoed *_this, pppoe_session *session)
{
	slist_add(&_this->session_free_list,
	    (void *)(intptr_t)session->session_id);

	if (_this->acookie_hash != NULL)
		hash_delete(_this->acookie_hash,
		    (void *)(intptr_t)session->acookie, 0);
	if (_this->session_hash != NULL)
		hash_delete(_this->session_hash,
		    (void *)(intptr_t)session->session_id, 0);

	pppoe_session_fini(session);
	free(session);
}

/*
 * PPPoE Configuration
 */
/* reload configurations for the PPPoE daemon */
int
pppoed_reload(pppoed *_this, struct pppoe_confs *pppoe_conf)
{
	int                i, count, do_start, found;
	struct pppoe_conf *conf;
	slist              rmlist, newlist;
	struct {
		char			 ifname[IF_NAMESIZE];
		char			 name[PPPOED_PHY_LABEL_SIZE];
		struct pppoe_conf	*conf;
	} listeners[PPPOE_NLISTENER];
	pppoed_listener   *l;
	pppoe_session     *session;
	hash_link         *hl;

	do_start = 0;
	slist_init(&rmlist);
	slist_init(&newlist);

	count = 0;
	TAILQ_FOREACH(conf, pppoe_conf, entry) {
		strlcpy(listeners[count].ifname, conf->if_name,
		    sizeof(listeners[count].ifname));
		strlcpy(listeners[count].name, conf->name,
		    sizeof(listeners[count].name));
		listeners[count].conf = conf;
		count++;
	}

	if (slist_add_all(&rmlist, &_this->listener) != 0)
		goto fail;

	for (i = 0; i < count; i++) {
		found = 0;
		l = NULL;
		slist_itr_first(&rmlist);
		while (slist_itr_has_next(&rmlist)) {
			l = slist_itr_next(&rmlist);
			if (strcmp(l->listen_ifname, listeners[i].ifname) == 0){
				slist_itr_remove(&rmlist);
				found = 1;
				break;
			}
		}
		if (!found) {
			if ((l = malloc(sizeof(pppoed_listener))) == NULL)
				goto fail;
			pppoed_listener_init(_this, l);
		}
		l->self = _this;
		strlcpy(l->tun_name, listeners[i].name, sizeof(l->tun_name));
		strlcpy(l->listen_ifname, listeners[i].ifname,
		    sizeof(l->listen_ifname));
		l->conf = listeners[i].conf;
		if (slist_add(&newlist, l) == NULL) {
			pppoed_log(_this, LOG_ERR,
			    "slist_add() failed in %s(): %m", __func__);
			goto fail;
		}
	}

	if (slist_set_size(&_this->listener, count) != 0)
		goto fail;

	/* garbage collection of listener context */
	slist_itr_first(&rmlist);
	while (slist_itr_has_next(&rmlist)) {
		l = slist_itr_next(&rmlist);
		/* handle child PPPoE session */
		if (_this->session_hash != NULL) {
			for (hl = hash_first(_this->session_hash); hl != NULL;
			    hl = hash_next(_this->session_hash)) {
				session = (pppoe_session *)hl->item;
				if (session->listener_index == l->index)
					pppoe_session_stop(session);
			}
		}
		pppoed_listener_stop(l);
		free(l);
	}
	slist_remove_all(&_this->listener);
	/* as slist_set_size-ed, it must not fail */
	(void)slist_add_all(&_this->listener, &newlist);

	/* reset indexes */
	slist_itr_first(&newlist);
	for (i = 0; slist_itr_has_next(&newlist); i++) {
		l = slist_itr_next(&newlist);
		if (l->index != i && l->index != PPPOED_LISTENER_INVALID_INDEX){
			PPPOED_DBG((_this, LOG_DEBUG, "listener %d => %d",
			    l->index, i));
			for (hl = hash_first(_this->session_hash); hl != NULL;
			    hl = hash_next(_this->session_hash)) {
				session = (pppoe_session *)hl->item;
				if (session->listener_index == l->index)
					session->listener_index = i;
			}
		}
		l->index = i;
	}

	slist_fini(&rmlist);
	slist_fini(&newlist);

	if (pppoed_start(_this) != 0)
		return 1;

	return 0;
fail:
	slist_fini(&rmlist);
	slist_fini(&newlist);

	return 1;
}

/*
 * I/O
 */

static void
pppoed_io_event(int fd, short evmask, void *ctx)
{
	u_char buf[BPF_CAPTURE_SIZ], *pkt;
	int lpkt, off;
	pppoed_listener *_this;
	struct ether_header *ether;
	struct bpf_hdr *bpf;

	_this = ctx;

	PPPOED_ASSERT(_this != NULL);

	lpkt = read(_this->bpf, buf, sizeof(buf));
	pkt = buf;
	while (lpkt > 0) {
		if (lpkt < sizeof(struct bpf_hdr)) {
			pppoed_log(_this->self, LOG_WARNING,
			    "Received bad PPPoE packet: packet too short(%d)",
			    lpkt);
			break;
		}
		bpf = (struct bpf_hdr *)pkt;
		ether = (struct ether_header *)(pkt + bpf->bh_hdrlen);
		ether->ether_type = ntohs(ether->ether_type);
		if (memcmp(ether->ether_shost, _this->ether_addr,
		    ETHER_ADDR_LEN) == 0)
			/* the packet is from myself */
			goto next_pkt;
		off = bpf->bh_hdrlen + sizeof(struct ether_header);
		if (lpkt < off + sizeof(struct pppoe_header)) {
			pppoed_log(_this->self, LOG_WARNING,
			    "Received bad PPPoE packet: packet too short(%d)",
			    lpkt);
			break;
		}
		pppoed_input(_this, ether->ether_shost,
		    (ether->ether_type == ETHERTYPE_PPPOEDISC)? 1 : 0,
		    pkt + off, lpkt - off);
next_pkt:
		pkt = pkt + BPF_WORDALIGN(bpf->bh_hdrlen +
		    bpf->bh_caplen);
		lpkt -= BPF_WORDALIGN(bpf->bh_hdrlen + bpf->bh_caplen);
	}
	return;
}

static void
pppoed_input(pppoed_listener *_this, uint8_t shost[ETHER_ADDR_LEN], int is_disc,
    u_char *pkt, int lpkt)
{
	hash_link *hl;
	pppoe_session *session;
	struct pppoe_header *pppoe;
	struct pppoe_tlv *tlv;
	u_char tlvspace[2048], *p_tlvspace;
	int session_id;
	slist tag_list;
	const char *reason;
#define tlvspace_remaining() (sizeof(tlvspace) - (p_tlvspace - tlvspace))

	reason = "";
	p_tlvspace = tlvspace;
	session = NULL;

	pppoe = (struct pppoe_header *)pkt;
	session_id = pppoe->session_id = ntohs(pppoe->session_id);
	pppoe->length = ntohs(pppoe->length);

#ifdef PPPOED_DEBUG
	if (is_disc) {
		PPPOED_DBG((_this->self, DEBUG_LEVEL_1,
		    "Recv%s(%02x) ver=%d type=%d session-id=%d if=%s",
		    pppoe_code_string(pppoe->code), pppoe->code,
		    pppoe->ver, pppoe->type, pppoe->session_id,
		    _this->listen_ifname));
	}
#endif
	pkt += sizeof(struct pppoe_header);
	lpkt -= sizeof(struct pppoe_header);

	if (lpkt < pppoe->length) {
		reason = "received packet is shorter than "
		    "pppoe length field.";
		goto bad_packet;
	}
	/* use PPPoE header value as lpkt */
	lpkt = pppoe->length;

	if (pppoe->type != PPPOE_RFC2516_TYPE ||
	    pppoe->ver != PPPOE_RFC2516_VER) {
		reason = "received packet has wrong version or type.";
		goto bad_packet;
	}

	if (session_id != 0) {
		hl = hash_lookup(_this->self->session_hash,
		    (void *)(intptr_t)session_id);
		if (hl != NULL) {
			if (memcmp(((pppoe_session *)hl->item)->ether_addr,
			    shost, ETHER_ADDR_LEN) != 0) {
				reason = "received packet from wrong host.";
				goto bad_packet;
			}
			session = (pppoe_session *)hl->item;
		}
	}
	if (!is_disc) {
		if (session != NULL)
			pppoe_session_input(session, pkt, pppoe->length);
		return;
	}

	/*
	 * PPPoE-Discovery Packet processing.
	 */
	slist_init(&tag_list);
	while (lpkt > 0) {
		if (lpkt < 4) {
			reason = "tlv list is broken.  "
			    "Remaining octet is too short.";
			goto fail;
		}
		if (tlvspace_remaining() < 4) {
			reason = "parsing TAGs reached the buffer size limit.";
			goto fail;
		}
		tlv = (struct pppoe_tlv *)p_tlvspace;
		GETSHORT(tlv->type, pkt);
		GETSHORT(tlv->length, pkt);
		p_tlvspace += 4;
		lpkt -= 4;
		if (tlv->length > lpkt) {
			reason = "tlv list is broken.  length is wrong.";
			goto fail;
		}
		if (tlvspace_remaining() < tlv->length) {
			reason = "parsing TAGs reached the buffer size limit.";
			goto fail;
		}
		if (tlv->length > 0) {
			memcpy(tlv->value, pkt, tlv->length);
			pkt += tlv->length;
			lpkt -= tlv->length;
			p_tlvspace += tlv->length;
			p_tlvspace = (u_char *)ALIGN(p_tlvspace);
		}
#ifdef	PPPOED_DEBUG
		if (debuglevel >= 2)
			pppoed_log(_this->self, DEBUG_LEVEL_2,
			    "Recv%s tag %s(%04x)=%s",
			    pppoe_code_string(pppoe->code),
			    pppoe_tag_string(tlv->type), tlv->type,
			    pppoed_tlv_value_string(tlv));
#endif
		if (tlv->type == PPPOE_TAG_END_OF_LIST)
			break;
		if (slist_add(&tag_list, tlv) == NULL) {
			goto fail;
		}
	}
	switch (pppoe->code) {
	case PPPOE_CODE_PADI:
		if (_this->self->state != PPPOED_STATE_RUNNING)
			break;
		pppoed_recv_PADI(_this, shost, &tag_list);
		break;
	case PPPOE_CODE_PADR:
		if (_this->self->state != PPPOED_STATE_RUNNING)
			break;
		pppoed_recv_PADR(_this, shost, &tag_list);
		break;
	case PPPOE_CODE_PADT:
		PPPOED_DBG((_this->self, LOG_DEBUG, "RecvPADT"));
		if (session != NULL)
			pppoe_session_recv_PADT(session, &tag_list);
		break;
	}
	slist_fini(&tag_list);

	return;
fail:
	slist_fini(&tag_list);
bad_packet:
	pppoed_log(_this->self, LOG_INFO,
	    "Received a bad packet: code=%s(%02x) ver=%d type=%d session-id=%d"
	    " if=%s: %s", pppoe_code_string(pppoe->code), pppoe->code,
	    pppoe->ver, pppoe->type, pppoe->session_id, _this->listen_ifname,
	    reason);
}

static int
pppoed_output(pppoed_listener *_this, u_char *dhost, u_char *pkt, int lpkt)
{
	int sz, iovc;
	struct iovec iov[3];
	struct ether_header ether;
	struct pppoe_header *pppoe;
	u_char pad[ETHERMIN];

	memcpy(ether.ether_dhost, dhost, ETHER_ADDR_LEN);
	memcpy(ether.ether_shost, _this->ether_addr, ETHER_ADDR_LEN);

	iov[0].iov_base = &ether;
	iov[0].iov_len = sizeof(struct ether_header);
	ether.ether_type = htons(ETHERTYPE_PPPOEDISC);
	iov[1].iov_base = pkt;
	iov[1].iov_len = lpkt;
	pppoe = (struct pppoe_header *)pkt;
	pppoe->length = htons(lpkt - sizeof(struct pppoe_header));

	iovc = 2;

	if (lpkt < ETHERMIN) {
		memset(pad, 0, ETHERMIN - lpkt);
		iov[2].iov_base = pad;
		iov[2].iov_len = ETHERMIN - lpkt;
		iovc++;
	}

	sz = writev(_this->bpf, iov, iovc);

	return (sz > 0)? 0 : -1;
}

static void
pppoed_recv_PADR(pppoed_listener *_this, uint8_t shost[ETHER_ADDR_LEN],
    slist *tag_list)
{
	int session_id, shuffle_cnt;
	pppoe_session *session;
	pppoed *_pppoed;

	_pppoed = _this->self;
	if ((session = malloc(sizeof(pppoe_session))) == NULL) {
		pppoed_log(_pppoed, LOG_ERR, "malloc() failed on %s(): %m",
		    __func__);
		goto fail;
	}

	/* create session_id */
	shuffle_cnt = 0;
	do {
		session_id = (intptr_t)slist_remove_first(
		    &_pppoed->session_free_list);
		if (session_id != PPPOED_SESSION_SHUFFLE_MARK)
			break;
		PPPOED_ASSERT(shuffle_cnt == 0);
		if (shuffle_cnt++ > 0) {
			pppoed_log(_pppoed, LOG_ERR,
			    "unexpected error in %s(): session_free_list full",
			    __func__);
			slist_add(&_pppoed->session_free_list,
			    (void *)PPPOED_SESSION_SHUFFLE_MARK);
			goto fail;
		}
		slist_shuffle(&_pppoed->session_free_list);
		slist_add(&_pppoed->session_free_list,
		    (void *)PPPOED_SESSION_SHUFFLE_MARK);
	} while (1);

	if (pppoe_session_init(session, _pppoed, _this->index, session_id,
	    shost) != 0)
		goto fail;

	hash_insert(_pppoed->session_hash, (void *)(intptr_t)session_id,
	    session);

	if (pppoe_session_recv_PADR(session, tag_list) != 0)
		goto fail;

	session = NULL;	/* don't free */
	/* FALLTHROUGH */
fail:
	if (session != NULL)
		pppoe_session_fini(session);
	return;
}

static void
pppoed_recv_PADI(pppoed_listener *_this, uint8_t shost[ETHER_ADDR_LEN],
    slist *tag_list)
{
	int len;
	const char *service_name, *ac_name;
	u_char bufspace[2048];
	u_char sn[2048], ac_name0[40];
	struct pppoe_header pppoe;
	struct pppoe_tlv tlv, *tlv_hostuniq, *tlv0, *tlv_service_name;
	bytebuffer *buf;

	if ((buf = bytebuffer_wrap(bufspace, sizeof(bufspace))) == NULL) {
		pppoed_log(_this->self, LOG_ERR,
		"bytebuffer_wrap() failed on %s(): %m", __func__);
		return;
	}
	bytebuffer_clear(buf);

	tlv_hostuniq = NULL;
	tlv_service_name = NULL;

	service_name = "";
	if (_this->conf->service_name != NULL)
		service_name = _this->conf->service_name;

	for (slist_itr_first(tag_list); slist_itr_has_next(tag_list);) {
		tlv0 = slist_itr_next(tag_list);
		if (tlv0->type == PPPOE_TAG_HOST_UNIQ)
			tlv_hostuniq = tlv0;
		if (tlv0->type == PPPOE_TAG_SERVICE_NAME) {

			len = tlv0->length;
			if (len >= sizeof(sn))
				goto fail;

			memcpy(sn, tlv0->value, len);
			sn[len] = '\0';

			if (strcmp(service_name, sn) == 0 ||
			    (sn[0] == '\0' && _this->conf->accept_any_service))
				tlv_service_name = tlv0;
		}
	}
	if (tlv_service_name == NULL) {
		pppoed_log(_this->self, LOG_INFO,
		    "Deny PADI from=%02x:%02x:%02x:%02x:%02x:%02x "
		    "service-name=%s host-uniq=%s if=%s: serviceName is "
		    "not allowed.", shost[0], shost[1],
		    shost[2], shost[3], shost[4], shost[5], sn, tlv_hostuniq?
		    pppoed_tlv_value_string(tlv_hostuniq) : "none",
		    _this->listen_ifname);
		goto fail;
	}

	pppoed_log(_this->self, LOG_INFO,
	    "RecvPADI from=%02x:%02x:%02x:%02x:%02x:%02x service-name=%s "
	    "host-uniq=%s if=%s", shost[0], shost[1], shost[2], shost[3],
	    shost[4], shost[5], sn, tlv_hostuniq?
	    pppoed_tlv_value_string(tlv_hostuniq) : "none",
	    _this->listen_ifname);

	/*
	 * PPPoE Header
	 */
	memset(&pppoe, 0, sizeof(pppoe));
	pppoe.ver = PPPOE_RFC2516_VER;
	pppoe.type = PPPOE_RFC2516_TYPE;
	pppoe.code = PPPOE_CODE_PADO;
	bytebuffer_put(buf, &pppoe, sizeof(pppoe));

	/*
	 * Tag - Service-Name
	 */
	tlv.type = htons(PPPOE_TAG_SERVICE_NAME);
	len = strlen(service_name);
	tlv.length = htons(len);
	bytebuffer_put(buf, &tlv, sizeof(tlv));
	if (len > 0)
		bytebuffer_put(buf, service_name, len);

	/*
	 * Tag - Access Concentrator Name
	 */
	ac_name = _this->conf->ac_name;
	if (ac_name == NULL) {
		/*
		 * use the ethernet address as default AC-Name.
		 * suggested by RFC 2516.
		 */
		snprintf(ac_name0, sizeof(ac_name0),
		    "%02x:%02x:%02x:%02x:%02x:%02x", _this->ether_addr[0],
		    _this->ether_addr[1], _this->ether_addr[2],
		    _this->ether_addr[3], _this->ether_addr[4],
		    _this->ether_addr[5]);
		ac_name = ac_name0;
	}

	tlv.type = htons(PPPOE_TAG_AC_NAME);
	len = strlen(ac_name);
	tlv.length = htons(len);
	bytebuffer_put(buf, &tlv, sizeof(tlv));
	bytebuffer_put(buf, ac_name, len);

	/*
	 * Tag - ac-cookie
	 */
	if (_this->self->acookie_hash != NULL) {
		/*
		 * search next ac-cookie.
		 * (XXX it will loop in uint32_t boundaly)
		 */
		do {
			_this->self->acookie_next += 1;
		}
		while(hash_lookup(_this->self->acookie_hash,
		    (void *)(intptr_t)_this->self->acookie_next) != NULL);

		tlv.type = htons(PPPOE_TAG_AC_COOKIE);
		tlv.length = ntohs(sizeof(uint32_t));
		bytebuffer_put(buf, &tlv, sizeof(tlv));
		bytebuffer_put(buf, &_this->self->acookie_next,
		    sizeof(uint32_t));
	}

	/*
	 * Tag - Host-Uniq
	 */
	if (tlv_hostuniq != NULL) {
		tlv.type = htons(PPPOE_TAG_HOST_UNIQ);
		tlv.length = ntohs(tlv_hostuniq->length);
		bytebuffer_put(buf, &tlv, sizeof(tlv));
		bytebuffer_put(buf, tlv_hostuniq->value,
		    tlv_hostuniq->length);
	}

	/*
	 * Tag - End-Of-List
	 */
	tlv.type = htons(PPPOE_TAG_END_OF_LIST);
	tlv.length = ntohs(0);
	bytebuffer_put(buf, &tlv, sizeof(tlv));

	bytebuffer_flip(buf);

	if (pppoed_output(_this, shost, bytebuffer_pointer(buf),
	    bytebuffer_remaining(buf)) != 0) {
		pppoed_log(_this->self, LOG_ERR, "pppoed_output() failed:%m");
	}
	pppoed_log(_this->self, LOG_INFO,
	    "SendPADO to=%02x:%02x:%02x:%02x:%02x:%02x serviceName=%s "
	    "acName=%s hostUniq=%s eol if=%s", shost[0], shost[1], shost[2],
	    shost[3], shost[4], shost[5], service_name, ac_name,
	    tlv_hostuniq? pppoed_tlv_value_string(tlv_hostuniq) : "none",
		_this->listen_ifname);
	/* FALLTHROUGH */
fail:
	bytebuffer_unwrap(buf);
	bytebuffer_destroy(buf);
}

/*
 * log
 */
static void
pppoed_log(pppoed *_this, int prio, const char *fmt, ...)
{
	char logbuf[BUFSIZ];
	va_list ap;

	PPPOED_ASSERT(_this != NULL);
	va_start(ap, fmt);
#ifdef	PPPOED_MULTIPLE
	snprintf(logbuf, sizeof(logbuf), "pppoed id=%u %s", _this->id, fmt);
#else
	snprintf(logbuf, sizeof(logbuf), "pppoed %s", fmt);
#endif
	vlog_printf(prio, logbuf, ap);
	va_end(ap);
}

#define	NAME_VAL(x)	{ x, #x }
static struct _label_name {
	int		label;
	const char	*name;
} pppoe_code_labels[] = {
	NAME_VAL(PPPOE_CODE_PADI),
	NAME_VAL(PPPOE_CODE_PADO),
	NAME_VAL(PPPOE_CODE_PADR),
	NAME_VAL(PPPOE_CODE_PADS),
	NAME_VAL(PPPOE_CODE_PADT),
#ifdef PPPOED_DEBUG
}, pppoe_tlv_labels[] = {
	NAME_VAL(PPPOE_TAG_END_OF_LIST),
	NAME_VAL(PPPOE_TAG_SERVICE_NAME),
	NAME_VAL(PPPOE_TAG_AC_NAME),
	NAME_VAL(PPPOE_TAG_HOST_UNIQ),
	NAME_VAL(PPPOE_TAG_AC_COOKIE),
	NAME_VAL(PPPOE_TAG_VENDOR_SPECIFIC),
	NAME_VAL(PPPOE_TAG_RELAY_SESSION_ID),
	NAME_VAL(PPPOE_TAG_SERVICE_NAME_ERROR),
	NAME_VAL(PPPOE_TAG_AC_SYSTEM_ERROR),
	NAME_VAL(PPPOE_TAG_GENERIC_ERROR)
#endif
};
#define LABEL_TO_STRING(func_name, label_names, prefix_len)		\
	static const char *						\
	func_name(int code)						\
	{								\
		int i;							\
									\
		for (i = 0; i < countof(label_names); i++) {		\
			if (label_names[i].label == code)		\
				return label_names[i].name + prefix_len;\
		}							\
									\
		return "UNKNOWN";					\
	}
LABEL_TO_STRING(pppoe_code_string, pppoe_code_labels, 11)
#ifdef PPPOED_DEBUG
LABEL_TO_STRING(pppoe_tag_string, pppoe_tlv_labels, 10)
#endif

const char *
pppoed_tlv_value_string(struct pppoe_tlv *tlv)
{
	int i;
	char buf[3];
	static char _tlv_string_value[8192];

	_tlv_string_value[0] = '\0';
	for (i = 0; i < tlv->length; i++) {
		snprintf(buf, sizeof(buf), "%02x", tlv->value[i]);
		strlcat(_tlv_string_value, buf,
		    sizeof(_tlv_string_value));
	}
	return _tlv_string_value;
}

/*
 * misc
 */
static int
session_id_cmp(void *a, void *b)
{
	int ia, ib;

	ia = (intptr_t)a;
	ib = (intptr_t)b;

	return ib - ia;
}

static uint32_t
session_id_hash(void *a, size_t siz)
{
	int ia;

	ia = (intptr_t)a;

	return ia % siz;
}
