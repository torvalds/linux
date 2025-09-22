/* $OpenBSD: kqueue-tun.c,v 1.5 2016/09/20 23:05:27 bluhm Exp $ */
/* $Gateweaver: tunkq.c,v 1.2 2003/11/27 22:47:41 cmaxwell Exp $ */
/*
 * Copyright 2003 Christopher J. Maxwell <cmaxwell@themanor.net>
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <netinet/in.h>
#include <net/if.h>
#include <net/if_tun.h>

#include <err.h>
#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "main.h"

#define TUN0		"tun98"
#define TUN1		"tun99"
#define TUN0_ADDR	"192.0.2.1"
#define TUN1_ADDR	"192.0.2.2"
#define TUN_MAXWAIT	5
#define TUN_PINGDEL	1

struct buffer {
	u_char *buf;
	size_t len;
	size_t a;
};

int state;
int tunfd[2];
struct buffer tpkt;
u_char pktbuf[TUNMTU];
struct event tunwev[2];
struct timeval exittv = {TUN_MAXWAIT, 0};

void
tunnel_write(int fd, short which, void *arg)
{
	uint32_t type = htonl(AF_INET);
	struct iovec iv[2];
	int rlen;
	int fdkey = (fd == tunfd[0]) ? 0 : 1;

	iv[0].iov_base = &type;
	iv[0].iov_len = sizeof(type);
	iv[1].iov_base = tpkt.buf;
	iv[1].iov_len = tpkt.len;

	state++;
	if ((rlen = writev(fd, iv, 2)) > 0)
		fprintf(stderr, "Tunnel %d wrote %ld bytes\n",
		    fdkey, (long)(rlen - sizeof(type)));
	else
		errx(1, "Write to tunnel %d failed", fdkey);
}

void
tunnel_read(int fd, short which, void *arg)
{
	struct iovec iv[2];
	uint32_t type;
	int rlen;
	int fdkey = (fd == tunfd[0]) ? 0 : 1;
	int oppfdkey = (fd == tunfd[0]) ? 1 : 0;

	iv[0].iov_base = &type;
	iv[0].iov_len = sizeof(type);
	iv[1].iov_base = tpkt.buf;
	iv[1].iov_len = tpkt.a;

	state++;
	if ((rlen = readv(fd, iv, 2)) > 0) {
		fprintf(stderr, "Tunnel %d read %ld bytes\n",
		    fdkey, (long)(rlen - sizeof(type)));
		tpkt.len = rlen - sizeof(type);

		/* add write event on opposite tunnel */
		event_add(&tunwev[oppfdkey], &exittv);
	} else
		errx(1, "Read from tunnel %d failed", fdkey);
}

void
tunnel_ping(int fd, short which, void *arg)
{
	system("ping -c 1 -I " TUN0_ADDR " " TUN1_ADDR " >/dev/null &");
}

/*
 * +------------+     +------------+
 * |    TUN0    |     |    TUN1    |
 * | TUN0_ADDR  |     | TUN1_ADDR  |
 * +------------+     +------------+
 *
 * Set up both tunnel devices (TUN0, TUN1)
 * This works because the routing table prefers the opposing end of the ptp
 * interfaces.
 * Set up one read and one write event per tunnel.
 * The read events add the write event.
 */
int
do_tun(void)
{
	struct event tunrev[2];
	struct event pingev;
	struct timeval pingtv = {TUN_PINGDEL, 0};

	/* read buffer */
	tpkt.buf = (u_char *)&pktbuf;
	tpkt.len = 0;
	tpkt.a = sizeof(pktbuf);

	event_init();

	/* tun0 */
	if ((tunfd[0] = open("/dev/" TUN0, O_RDWR)) < 0)
		errx(1, "Cannot open /dev/" TUN0);
	event_set(&tunrev[0], tunfd[0], EV_READ, tunnel_read, NULL);
	event_set(&tunwev[0], tunfd[0], EV_WRITE, tunnel_write, NULL);
	event_add(&tunrev[0], &exittv);

	/* tun1 */
	if ((tunfd[1] = open("/dev/" TUN1, O_RDWR)) < 0)
		errx(1, "Cannot open /dev/" TUN1);
	event_set(&tunrev[1], tunfd[1], EV_READ, tunnel_read, NULL);
	event_set(&tunwev[1], tunfd[1], EV_WRITE, tunnel_write, NULL);
	event_add(&tunrev[1], &exittv);

	/* ping */
	evtimer_set(&pingev, tunnel_ping, NULL);
	event_add(&pingev, &pingtv);

	/* configure the interfaces */
	system("ifconfig " TUN0 " " TUN0_ADDR
	    " netmask 255.255.255.255 " TUN1_ADDR);
	system("ifconfig " TUN1 " " TUN1_ADDR
	    " netmask 255.255.255.255 " TUN0_ADDR);

	state = 0;
	if (event_dispatch() < 0)
		errx(errno, "Event handler failed");

	return (state != 4);
}
