/*	$NetBSD: channel.c,v 1.1 2008/08/17 13:20:57 plunky Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2008 Iain Hibbert
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

/* $FreeBSD$ */

#include <sys/cdefs.h>
__RCSID("$NetBSD: channel.c,v 1.1 2008/08/17 13:20:57 plunky Exp $");

#include <sys/param.h>
#include <sys/ioctl.h>

#include <libutil.h>
#include <unistd.h>
#define L2CAP_SOCKET_CHECKED
#include "btpand.h"

static struct chlist	channel_list;
static int		channel_count;
static int		channel_tick;

static void channel_start(int, short, void *);
static void channel_read(int, short, void *);
static void channel_dispatch(packet_t *);
static void channel_watchdog(int, short, void *);

void
channel_init(void)
{

	LIST_INIT(&channel_list);
}

channel_t *
channel_alloc(void)
{
	channel_t *chan;

	chan = malloc(sizeof(channel_t));
	if (chan == NULL) {
		log_err("%s() failed: %m", __func__);
		return NULL;
	}

	memset(chan, 0, sizeof(channel_t));
	STAILQ_INIT(&chan->pktlist);
	chan->state = CHANNEL_CLOSED;
	LIST_INSERT_HEAD(&channel_list, chan, next);

	server_update(++channel_count);

	return chan;
}

bool
channel_open(channel_t *chan, int fd)
{
	int n;

	assert(chan->refcnt == 0);
	assert(chan->state != CHANNEL_CLOSED);

	if (chan->mtu > 0) {
		chan->sendbuf = malloc(chan->mtu);
		if (chan->sendbuf == NULL) {
			log_err("Could not malloc channel sendbuf: %m");
			return false;
		}
	}

	n = 1;
	if (ioctl(fd, FIONBIO, &n) == -1) {
		log_err("Could not set non-blocking IO: %m");
		return false;
	}

	event_set(&chan->rd_ev, fd, EV_READ | EV_PERSIST, channel_read, chan);
	if (event_add(&chan->rd_ev, NULL) == -1) {
		log_err("Could not add channel read event: %m");
		return false;
	}

	event_set(&chan->wr_ev, fd, EV_WRITE, channel_start, chan);

	chan->refcnt++;
	chan->fd = fd;

	log_debug("(fd#%d)", chan->fd);

	return true;
}

void
channel_close(channel_t *chan)
{
	pkthdr_t *ph;

	assert(chan->state != CHANNEL_CLOSED);

	log_debug("(fd#%d)", chan->fd);

	chan->state = CHANNEL_CLOSED;
	event_del(&chan->rd_ev);
	event_del(&chan->wr_ev);
	close(chan->fd);
	chan->refcnt--;
	chan->tick = 0;

	while ((ph = STAILQ_FIRST(&chan->pktlist)) != NULL) {
		STAILQ_REMOVE_HEAD(&chan->pktlist, next);
		pkthdr_free(ph);
		chan->qlen--;
	}

	if (chan->pfh != NULL) {
		pidfile_remove(chan->pfh);
		chan->pfh = NULL;
	}

	if (chan->refcnt == 0)
		channel_free(chan);
}

void
channel_free(channel_t *chan)
{

	assert(chan->refcnt == 0);
	assert(chan->state == CHANNEL_CLOSED);
	assert(chan->qlen == 0);
	assert(STAILQ_EMPTY(&chan->pktlist));

	LIST_REMOVE(chan, next);
	free(chan->pfilter);
	free(chan->mfilter);
	free(chan->sendbuf);
	free(chan);

	server_update(--channel_count);

	if (server_limit == 0) {
		log_info("connection closed, exiting");
		exit(EXIT_SUCCESS);
	}
}

static void
channel_start(int fd, short ev, void *arg)
{
	channel_t *chan = arg;
	pkthdr_t *ph;

	chan->oactive = true;

	while (chan->qlen > 0) {
		ph = STAILQ_FIRST(&chan->pktlist);

		channel_timeout(chan, 10);
		if (chan->send(chan, ph->data) == false) {
			if (event_add(&chan->wr_ev, NULL) == -1) {
				log_err("Could not add channel write event: %m");
				channel_close(chan);
			}
			return;
		}

		STAILQ_REMOVE_HEAD(&chan->pktlist, next);
		pkthdr_free(ph);
		chan->qlen--;
	}

	channel_timeout(chan, 0);
	chan->oactive = false;
}

static void
channel_read(int fd, short ev, void *arg)
{
	channel_t *chan = arg;
	packet_t *pkt;
	ssize_t nr;

	pkt = packet_alloc(chan);
	if (pkt == NULL) {
		channel_close(chan);
		return;
	}

	nr = read(fd, pkt->buf, chan->mru);
	if (nr == -1) {
		log_err("channel read error: %m");
		packet_free(pkt);
		channel_close(chan);
		return;
	}
	if (nr == 0) {	/* EOF */
		log_debug("(fd#%d) EOF", fd);
		packet_free(pkt);
		channel_close(chan);
		return;
	}
	pkt->len = nr;

	if (chan->recv(pkt) == true)
		channel_dispatch(pkt);

	packet_free(pkt);
}

static void
channel_dispatch(packet_t *pkt)
{
	channel_t *chan;

	/*
	 * This is simple routing. I'm not sure if its allowed by
	 * the PAN or BNEP specifications, but it seems logical
	 * to send unicast packets to connected destinations where
	 * possible.
	 */
	if (!ETHER_IS_MULTICAST(pkt->dst)) {
		LIST_FOREACH(chan, &channel_list, next) {
			if (chan == pkt->chan
			    || chan->state != CHANNEL_OPEN)
				continue;

			if (memcmp(pkt->dst, chan->raddr, ETHER_ADDR_LEN) == 0) {
				if (chan->qlen > CHANNEL_MAXQLEN)
					log_notice("Queue overflow");
				else
					channel_put(chan, pkt);

				return;
			}
		}
	}

	LIST_FOREACH(chan, &channel_list, next) {
		if (chan == pkt->chan
		    || chan->state != CHANNEL_OPEN)
			continue;

		if (chan->qlen > CHANNEL_MAXQLEN) {
			log_notice("Queue overflow");
			continue;
		}

		channel_put(chan, pkt);
	}
}

void
channel_put(channel_t *chan, packet_t *pkt)
{
	pkthdr_t *ph;

	ph = pkthdr_alloc(pkt);
	if (ph == NULL)
		return;

	chan->qlen++;
	STAILQ_INSERT_TAIL(&chan->pktlist, ph, next);

	if (!chan->oactive)
		channel_start(chan->fd, EV_WRITE, chan);
}

/*
 * Simple watchdog timer, only ticks when it is required and
 * closes the channel down if it times out.
 */
void
channel_timeout(channel_t *chan, int to)
{
	static struct event ev;

	if (to == 0)
		chan->tick = 0;
	else
		chan->tick = (channel_tick + to) % 60;

	if (channel_tick == 0) {
		evtimer_set(&ev, channel_watchdog, &ev);
		channel_watchdog(0, 0, &ev);
	}
}

static void
channel_watchdog(int fd, short ev, void *arg)
{
	static struct timeval tv = { .tv_sec = 1 };
	channel_t *chan, *next;
	int tick;

	tick = (channel_tick % 60) + 1;
	channel_tick = 0;

	next = LIST_FIRST(&channel_list);
	while ((chan = next) != NULL) {
		next = LIST_NEXT(chan, next);

		if (chan->tick == tick)
			channel_close(chan);
		else if (chan->tick != 0)
			channel_tick = tick;
	}

	if (channel_tick != 0 && evtimer_add(arg, &tv) < 0) {
		log_err("Could not add watchdog event: %m");
		exit(EXIT_FAILURE);
	}
}
