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
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/endian.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <assert.h>
#include "w00t.h"

struct client {
	char mac[6];
	int seq;

	struct client *next;
};

struct params {
	/* fds */
	int tx;
	int rx;
	int tap;

	/* ap params */
	char mac[6];
	char ssid[256];
	int chan;

	/* beacon */
	int bint;
	struct timeval blast;

	int seq;

	/* wep */
	int wep_len;
	char wep_key[13];
	int wep_iv;

	struct client *clients;

	/* lame window */
	char packet[4096];
	int packet_len;
	int packet_try;
	struct timeval plast;
};

void usage(char *name)
{
	printf("Usage: %s <opts>\n"
	       "-h\thelp\n"
	       "-i\t<iface>\n"
	       "-s\t<ssid>\n"
	       "-m\t<mac>\n"
	       "-w\t<wep key>\n"
	       "-c\t<chan>\n"
	       "-t\t<tap>\n"
	       , name);
	exit(0);
}

void fill_basic(struct ieee80211_frame *wh, struct params *p)
{       
        short *seq;
       
        wh->i_dur[0] = 0x69;
        wh->i_dur[1] = 0x00;
       
        memcpy(wh->i_addr2, p->mac, 6);
       
        seq = (short*)wh->i_seq;
        *seq = seqfn(p->seq, 0);
}

void send_frame(struct params *p, void *buf, int len)
{       
        int rc;
        
        rc = inject(p->tx, buf, len);
        if (rc == -1)
                err(1, "inject()");
        if (rc != len) {
                printf("injected %d/%d\n", rc, len);
                exit(1);
        }
        p->seq++;
}

int fill_beacon(struct params *p, struct ieee80211_frame *wh)
{
	int len;
	char *ptr;

	ptr = (char*) (wh+1);
	ptr += 8; /* timestamp */
	ptr += 2; /* bint */
	*ptr |= IEEE80211_CAPINFO_ESS;
	ptr += 2; /* capa */

	/* ssid */
	len = strlen(p->ssid);
	*ptr++ = 0;
	*ptr++ = len;
	memcpy(ptr, p->ssid, len);
	ptr += len;

	/* rates */
        *ptr++ = 1;
        *ptr++ = 4;
        *ptr++ = 2 | 0x80;
        *ptr++ = 4 | 0x80;
        *ptr++ = 11;
        *ptr++ = 22;

	/* ds param */
	*ptr++ = 3;
	*ptr++ = 1;
	*ptr++ = p->chan;
	
	return ptr - ((char*) wh);
}

void send_beacon(struct params *p)
{
	char buf[4096];
	struct ieee80211_frame *wh;
	int len;
	char *ptr;

	wh = (struct ieee80211_frame*) buf;

	memset(buf, 0, sizeof(buf));
	fill_basic(wh, p);
	memset(wh->i_addr1, 0xff, 6);
	memcpy(wh->i_addr3, p->mac, 6);

	wh->i_fc[0] |= IEEE80211_FC0_TYPE_MGT;
	wh->i_fc[0] |= IEEE80211_FC0_SUBTYPE_BEACON;

	len = fill_beacon(p, wh);

	/* TIM */
	ptr = (char*)wh + len;
	*ptr++ = 5;
	*ptr++ = 4;
	len +=  2+4;
#if 0
	printf("sending beacon\n");
#endif	
	send_frame(p, wh, len);

	if (gettimeofday(&p->blast, NULL) == -1)
		err(1, "gettimeofday()");
}


void send_pres(struct params *p, char *mac)
{
	char buf[4096];
	struct ieee80211_frame *wh;
	int len;

	wh = (struct ieee80211_frame*) buf;

	memset(buf, 0, sizeof(buf));
	fill_basic(wh, p);
	memcpy(wh->i_addr1, mac, 6);
	memcpy(wh->i_addr3, p->mac, 6);

	wh->i_fc[0] |= IEEE80211_FC0_TYPE_MGT;
	wh->i_fc[0] |= IEEE80211_FC0_SUBTYPE_PROBE_RESP;

	len = fill_beacon(p, wh);

	printf("sending probe response\n");
	send_frame(p, wh, len);
}

void read_preq(struct params *p, struct ieee80211_frame *wh, int len)
{
	unsigned char *ptr;
	unsigned char *end;
	unsigned char macs[6*3];

	ptr = (unsigned char*) (wh+1);

	/* ssid */
	if (*ptr != 0) {
		printf("weird pr %x\n", *ptr);
		return;
	}
	ptr++;

	end = ptr + (*ptr) + 1;
	*end = 0;
	ptr++;

	mac2str(macs, wh->i_addr2);
	printf("Probe request for [%s] from %s\n", ptr, macs);

	if ((strcmp(ptr, "") == 0) || (strcmp(ptr, p->ssid) == 0))
		send_pres(p, wh->i_addr2);
}

void send_auth(struct params* p, char *mac)
{
	char buf[4096];
	struct ieee80211_frame *wh;
	unsigned short *ptr;
	int len;

	wh = (struct ieee80211_frame*) buf;

	memset(buf, 0, sizeof(buf));
	fill_basic(wh, p);
	memcpy(wh->i_addr1, mac, 6);
	memcpy(wh->i_addr3, p->mac, 6);

	wh->i_fc[0] |= IEEE80211_FC0_TYPE_MGT;
	wh->i_fc[0] |= IEEE80211_FC0_SUBTYPE_AUTH;

	ptr = (unsigned short*) (wh+1);
	*ptr++ = htole16(0);
	*ptr++ = htole16(2);
	*ptr++ = htole16(0);

	len = ((char*)ptr) - ((char*) wh);
	printf("sending auth\n");
	send_frame(p, wh, len);
}

void read_auth(struct params *p, struct ieee80211_frame *wh, int len)
{
	unsigned short *ptr;
	char mac[6*3];

	if (memcmp(wh->i_addr1, p->mac, 6) != 0)
		return;

	ptr = (unsigned short*) (wh+1);
	if (le16toh(*ptr) != 0) {
		printf("Unknown auth algo %d\n", le16toh(*ptr));
		return;
	}
	ptr++;
	if (le16toh(*ptr) == 1) {
		mac2str(mac, wh->i_addr2);
		printf("Got auth from %s\n", mac);
		send_auth(p, wh->i_addr2);
	} else {
		printf("Weird seq in auth %d\n", le16toh(*ptr));
	}
}

void send_assoc(struct params *p, char *mac)
{
	char buf[4096];
	struct ieee80211_frame *wh;
	char *ptr;
	int len;

	wh = (struct ieee80211_frame*) buf;

	memset(buf, 0, sizeof(buf));
	fill_basic(wh, p);
	memcpy(wh->i_addr1, mac, 6);
	memcpy(wh->i_addr3, p->mac, 6);

	wh->i_fc[0] |= IEEE80211_FC0_TYPE_MGT;
	wh->i_fc[0] |= IEEE80211_FC0_SUBTYPE_ASSOC_RESP;

	ptr = (char*) (wh+1);
	*ptr |= IEEE80211_CAPINFO_ESS;
	ptr += 2; /* cap */
	ptr += 2; /* status */
	ptr += 2; /* aid */
	
	/* rates */
        *ptr++ = 1;
        *ptr++ = 4;
        *ptr++ = 2 | 0x80;
        *ptr++ = 4 | 0x80;
        *ptr++ = 11;
        *ptr++ = 22;

	len = ptr - ((char*) wh);
	printf("sending assoc response\n");
	send_frame(p, wh, len);
}

void read_assoc(struct params *p, struct ieee80211_frame *wh, int len)
{
	unsigned char *ptr;
	unsigned char *end;
	unsigned char macs[6*3];

	if (memcmp(wh->i_addr1, p->mac, 6) != 0)
		return;
	
	ptr = (unsigned char*) (wh+1);
	ptr += 2; /* capa */
	ptr += 2; /* list interval */

	/* ssid */
	if (*ptr != 0) {
		printf("weird pr %x\n", *ptr);
		return;
	}
	ptr++;

	end = ptr + (*ptr) + 1;
	*end = 0;
	ptr++;

	mac2str(macs, wh->i_addr2);
	printf("Assoc request for [%s] from %s\n", ptr, macs);

	if (strcmp(ptr, p->ssid) == 0)
		send_assoc(p, wh->i_addr2);
}

void read_mgt(struct params *p, struct ieee80211_frame *wh, int len)
{
	switch (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) {
	case IEEE80211_FC0_SUBTYPE_PROBE_REQ:
		read_preq(p, wh, len);
		break;
		
	case IEEE80211_FC0_SUBTYPE_PROBE_RESP:
		break;

	case IEEE80211_FC0_SUBTYPE_AUTH:
		read_auth(p, wh, len);
		break;

	case IEEE80211_FC0_SUBTYPE_ASSOC_REQ:
		read_assoc(p, wh, len);
		break;

	case IEEE80211_FC0_SUBTYPE_ASSOC_RESP:
	case IEEE80211_FC0_SUBTYPE_BEACON:
		break;

	default:
		printf("wtf %d\n", (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) >>
				   IEEE80211_FC0_SUBTYPE_SHIFT);
		abort();
		break;
	}
}

void send_cts(struct params *p, char *mac)
{
	char buf[64];
	struct ieee80211_frame *wh;

	memset(buf, 0, sizeof(buf));
	wh = (struct ieee80211_frame*) buf;
	wh->i_fc[0] |= IEEE80211_FC0_TYPE_CTL;
	wh->i_fc[0] |= IEEE80211_FC0_SUBTYPE_CTS;
	wh->i_dur[0] = 0x69;
	wh->i_dur[1] = 0x00;
	memcpy(wh->i_addr1, mac, 6);

	send_frame(p, wh, 10);
}

void read_rts(struct params *p, struct ieee80211_frame *wh, int len)
{
	if (memcmp(wh->i_addr1, p->mac, 6) != 0)
		return;

	send_cts(p, wh->i_addr2);
}

void read_ack(struct params *p, struct ieee80211_frame *wh, int len)
{
	if (memcmp(wh->i_addr1, p->mac, 6) == 0)
		p->packet_try = 0;
}

void read_ctl(struct params *p, struct ieee80211_frame *wh, int len)
{
	switch (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) {
	case IEEE80211_FC0_SUBTYPE_RTS:
		read_rts(p, wh, len);
		break;

	case IEEE80211_FC0_SUBTYPE_ACK:
		read_ack(p, wh, len);
		break;

	case IEEE80211_FC0_SUBTYPE_CTS:
		break;

	default:
		printf("wtf %d\n", (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) >>
		       IEEE80211_FC0_SUBTYPE_SHIFT);
		abort();
		break;
	}
#if 0
	printf("ctl\n");
#endif	
}

int broadcast(struct ieee80211_frame *wh)
{
	/* XXX multicast */

	if (memcmp(wh->i_addr1, "\xff\xff\xff\xff\xff\xff", 6) == 0)
		return 1;

	return 0;
}

void enque(struct params *p, struct ieee80211_frame *wh, int len)
{
	if (broadcast(wh))
		return;

	assert(sizeof(p->packet) >= len);

	memcpy(p->packet, wh, len);
	p->packet_len = len;
	p->packet_try = 1;

	wh = (struct ieee80211_frame*) p->packet;
	wh->i_fc[1] |= IEEE80211_FC1_RETRY;

	if (gettimeofday(&p->plast, NULL) == -1)
		err(1, "gettimeofday()");
}

void relay_data(struct params *p, struct ieee80211_frame *wh, int len)
{
	char seq[2];
	char fc[2];
	unsigned short *ps;

	/* copy crap */
	memcpy(fc, wh->i_fc, 2);
	memcpy(seq, wh->i_seq, 2);

	/* relay frame */
	wh->i_fc[1] &= ~(IEEE80211_FC1_DIR_TODS | IEEE80211_FC1_RETRY);
	wh->i_fc[1] |= IEEE80211_FC1_DIR_FROMDS;
	memcpy(wh->i_addr1, wh->i_addr3, sizeof(wh->i_addr1));
	memcpy(wh->i_addr3, wh->i_addr2, sizeof(wh->i_addr3));
	memcpy(wh->i_addr2, p->mac, sizeof(wh->i_addr2));
        ps = (unsigned short*)wh->i_seq;
        *ps = seqfn(p->seq, 0);
	
	send_frame(p, wh, len);
	enque(p, wh, len);

	/* restore */
	memcpy(wh->i_fc, fc, sizeof(fc));
	memcpy(wh->i_addr2, wh->i_addr3, sizeof(wh->i_addr2));
	memcpy(wh->i_addr3, wh->i_addr1, sizeof(wh->i_addr2));
	memcpy(wh->i_addr1, p->mac, sizeof(wh->i_addr1));
	memcpy(wh->i_seq, seq, sizeof(seq));
}

void read_real_data(struct params *p, struct ieee80211_frame *wh, int len)
{
	char dst[6];
	int rc;
	char *ptr = (char*) (wh+1);

	/* stuff not for this net */
	if (memcmp(wh->i_addr1, p->mac, 6) != 0)
		return;

	/* relay data */
	if (memcmp(wh->i_addr3, p->mac, 6) != 0)
		relay_data(p, wh, len);

	memcpy(dst, wh->i_addr3, 6);


	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		if (!p->wep_len) {
			printf("Got wep but i aint wep\n");
			return;
		}
		
		if (wep_decrypt(wh, len, p->wep_key, p->wep_len) == -1){
			printf("Can't decrypt\n");
			return;
		}

		ptr += 4;
		len -= 8;
	}

	/* ether header */
	ptr += 8 - 2;
	ptr -= 6;
	memcpy(ptr, wh->i_addr2, 6);
	ptr -= 6;
	memcpy(ptr, dst, 6);

	len -= sizeof(*wh);
	len -= 8;
	len += 14;

	/* send to tap */
	rc = write(p->tap, ptr, len);
	if (rc == -1)
		err(1, "write()");
	if (rc != len) {
		printf("Wrote %d/%d\n", rc, len);
		exit(1);
	}
}

void read_data(struct params *p, struct ieee80211_frame *wh, int len)
{
	switch (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) {
	case IEEE80211_FC0_SUBTYPE_DATA:
		read_real_data(p, wh, len);
		break;

	case IEEE80211_FC0_SUBTYPE_NODATA:
		break;

	default:
		printf("wtf %d\n", (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) >>
				   IEEE80211_FC0_SUBTYPE_SHIFT);
		abort();
		break;
	}
}

struct client* client_find(struct params *p, char *mac)
{
	struct client* c = p->clients;

	while (c) {
		if (memcmp(c->mac, mac, 6) == 0)
			return c;

		c = c->next;
	}

	return NULL;
}

void client_insert(struct params *p, struct client *c)
{
#if 1
	do {
		char mac[6*3];
		
		mac2str(mac, c->mac);
		printf("Adding client %s\n", mac);
	} while(0);
#endif

	c->next = p->clients;
	p->clients = c;
}

int duplicate(struct params *p, struct ieee80211_frame *wh, int rc)
{
	struct client *c;
	int s;

	if (!frame_type(wh, IEEE80211_FC0_TYPE_DATA,
			IEEE80211_FC0_SUBTYPE_DATA))
		return 0;

	s = seqno(wh);

	c = client_find(p, wh->i_addr2);
	if (!c) {
		c = malloc(sizeof(*c));
		if (!c)
			err(1, "malloc()");

		memset(c, 0, sizeof(*c));
		memcpy(c->mac, wh->i_addr2, 6);

		c->seq = s-1;
		client_insert(p, c);
	}

	if (wh->i_fc[1] & IEEE80211_FC1_RETRY) {
		if ( (s <= c->seq) && ((c->seq - s ) < 5)) {
#if 0
			printf("Dup seq %d prev %d\n",
			       s, c->seq);
#endif
			return 1;
		}	
	}	

#if 0
	do {
		char mac[3*6];

		mac2str(mac, c->mac);
		printf("%s seq %d prev %d\n", mac, s, c->seq);
	} while (0);
#endif
	
	c->seq = s;
	return 0;
}

void ack(struct params *p, struct ieee80211_frame *wh)
{
	if (memcmp(wh->i_addr1, p->mac, 6) != 0)
		return;

	if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_CTL)
		return;

	send_ack(p->tx, wh->i_addr2);
}

void read_wifi(struct params *p)
{
	char buf[4096];
	int rc;
	struct ieee80211_frame *wh;

	rc = sniff(p->rx, buf, sizeof(buf));
	if (rc == -1)
		err(1, "sniff()");
        
	wh = get_wifi(buf, &rc);
	if (!wh)
		return;

	/* filter my own shit */
	if (memcmp(wh->i_addr2, p->mac, 6) == 0) {
		/* XXX CTL frames */
		if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) !=
		    IEEE80211_FC0_TYPE_CTL)
			return;
	}

#if 1
	ack(p, wh);
#endif

	if (duplicate(p, wh, rc)) {
#if 0
		printf("Dup\n");
#endif		
		return;
	}

	switch (wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) {
	case IEEE80211_FC0_TYPE_MGT:
		read_mgt(p, wh, rc);
		break;
		
	case IEEE80211_FC0_TYPE_CTL:
		read_ctl(p, wh, rc);
		break;
	
	case IEEE80211_FC0_TYPE_DATA:
		read_data(p, wh, rc);
		break;
	
	default:
		printf("wtf\n");
		abort();
		break;
	}
}

void read_tap(struct params *p)
{
	char buf[4096];
	char *ptr;
	int len = sizeof(buf);
	int offset;
	char src[6], dst[6];
	struct ieee80211_frame *wh;
	int rd;

	ptr = buf;
	offset = sizeof(struct ieee80211_frame) + 8 - 14;
	if (p->wep_len)
		offset += 4;

	ptr += offset;
	len -= offset;

	/* read packet */
	memset(buf, 0, sizeof(buf));
	rd = read(p->tap, ptr, len);
	if (rd == -1)
		err(1, "read()");

	/* 802.11 header */
	wh = (struct ieee80211_frame*) buf;
	memcpy(dst, ptr, sizeof(dst));
	memcpy(src, ptr+6, sizeof(src));
	fill_basic(wh, p);
	memcpy(wh->i_addr3, src, sizeof(wh->i_addr3));
	memcpy(wh->i_addr1, dst, sizeof(wh->i_addr1));
	wh->i_fc[0] |= IEEE80211_FC0_TYPE_DATA;
	wh->i_fc[1] |= IEEE80211_FC1_DIR_FROMDS;
	if (p->wep_len)
		wh->i_fc[1] |= IEEE80211_FC1_PROTECTED;

	/* LLC & SNAP */
	ptr = (char*) (wh+1);
	if (p->wep_len)
		ptr += 4;
	*ptr++ = 0xAA;
	*ptr++ = 0xAA;
	*ptr++ = 0x03;
	*ptr++ = 0x00;
	*ptr++ = 0x00;
	*ptr++ = 0x00;
	/* ether type overlaps w00t */

	rd += offset;

	/* WEP */
	if (p->wep_len) {
		ptr = (char*) (wh+1);
		memcpy(ptr, &p->wep_iv, 3);
		ptr[3] = 0;
		p->wep_iv++;

		wep_encrypt(wh, rd, p->wep_key, p->wep_len);
		rd += 4; /* ICV */
	}

	send_frame(p, wh, rd);
}

int retransmit(struct params *p)
{
#if 0
	printf("RETRANS %d\n", p->packet_try);
#endif

	send_frame(p, p->packet, p->packet_len);
	p->packet_try++;

	if (p->packet_try > 3)
		p->packet_try = 0;
	else {
		if (gettimeofday(&p->plast, NULL) == -1)
			err(1, "gettimeofday()");
	}

	return p->packet_try;
}

void next_event(struct params *p)
{
	struct timeval to, now;
	int el;
	int max;
	fd_set fds;
	int rtr = 3*1000;

	/* figure out select timeout */
	if (gettimeofday(&now, NULL) == -1)
		err(1, "gettimeofday()");

	/* check beacon timeout */
	el = elapsed(&p->blast, &now);
	if (el >= p->bint) {
		send_beacon(p);
		el = 0;
	}
	el = p->bint - el;
	to.tv_sec = el/1000/1000;
	to.tv_usec = el - to.tv_sec*1000*1000;

	/* check tx timeout */
	if (p->packet_try) {
		el = elapsed(&p->plast, &now);
		if (el >= rtr) {
			/* check if we gotta retransmit more */
			if (retransmit(p)) {
				el = 0;
			}
			else
				el = -1;
		}

		/* gotta retransmit in future */
		if (el != -1) {
			el = rtr - el;
			if ((to.tv_sec*1000*1000 + to.tv_usec) > el) {
				to.tv_sec = el/1000/1000;
				to.tv_usec = el - to.tv_sec*1000*1000;
			}
		}
	}

	/* select */
	FD_ZERO(&fds);
	FD_SET(p->rx, &fds);
	FD_SET(p->tap, &fds);
	max = p->rx > p->tap ? p->rx : p->tap;
	if (select(max+1, &fds, NULL, NULL, &to) == -1)
		err(1, "select()");

	if (FD_ISSET(p->tap, &fds))
		read_tap(p);
	if (FD_ISSET(p->rx, &fds))
		read_wifi(p);
}

int main(int argc, char *argv[])
{
	char *iface = "wlan0";
	char *tap = "tap0";
	struct params p;
	int ch;

	/* default params */
	memset(&p, 0, sizeof(p));
	memcpy(p.mac, "\x00\x00\xde\xfa\xce\x0d", 6);
	strcpy(p.ssid, "sorbo");
	p.bint = 500*1000;
	p.seq = getpid();
	if (gettimeofday(&p.blast, NULL) == -1)
		err(1, "gettimeofday()");
	p.chan = 3;

	while ((ch = getopt(argc, argv, "hi:s:m:w:c:t:")) != -1) {
		switch (ch) {
		case 'i':
			iface = optarg;
			break;
		case 't':
			tap = optarg;
			break;

		case 'c':
			p.chan = atoi(optarg);
			break;

		case 's':
			strncpy(p.ssid, optarg, sizeof(p.ssid)-1);
			p.ssid[sizeof(p.ssid)-1] = 0; 
			break;

		case 'm':
			str2mac(p.mac, optarg);
			break;

		case 'w':
			if (str2wep(p.wep_key, &p.wep_len, optarg)) {
				printf("Error parsing WEP key\n");
				exit(1);
			}
			break;

		case 'h':
		default:
			usage(argv[0]);
			break;
		}
	}

	/* init */
	if ((p.tx = open_tx(iface)) == -1)
		err(1, "open_tx()");
	if ((p.rx = open_rx(iface)) == -1)
		err(1, "open_rx()");

	if ((p.tap = open_tap(tap)) == -1)
		err(1, "open_tap()");
	if (set_iface_mac(tap, p.mac) == -1)
		err(1, "set_iface_mac()");

	while (1) {
		next_event(&p);
	}

	exit(0);
}
