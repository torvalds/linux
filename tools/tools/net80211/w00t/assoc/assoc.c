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
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <net80211/ieee80211.h>
#include <sys/endian.h>
#include "w00t.h"

enum {
	S_START = 0,
	S_SEND_PROBE_REQ,
	S_WAIT_PROBE_RES,
	S_SEND_AUTH,
	S_WAIT_AUTH,
	S_SEND_ASSOC,
	S_WAIT_ASSOC,
	S_ASSOCIATED,
	S_SEND_DATA,
	S_WAIT_ACK
};

struct params {
	int seq;
	int seq_rx;
	char *mac;
	char *ssid;
	char bssid[6];
	char ap[6];
	int tx;
	int rx;
	int tap;
	int aid;
	char packet[4096];
	int packet_len;
	int state;
	char wep_key[13];
	int wep_iv;
	int wep_len;
};

void usage(char *pname)
{
	printf("Usage: %s <opts>\n"
		"-m\t<source mac>\n"
		"-s\t<ssid>\n"
		"-h\tusage\n"
		"-i\t<iface>\n"
		"-w\t<wep key>\n"
		"-t\t<tap>\n"
		"-b\t<bssid>\n"
		, pname);
	exit(0);
}

void fill_basic(struct ieee80211_frame *wh, struct params *p)
{
	short *seq;
	
	wh->i_dur[0] = 0x69;
	wh->i_dur[1] = 0x00;

	memcpy(wh->i_addr1, p->ap, 6);
	memcpy(wh->i_addr2, p->mac, 6);
	memcpy(wh->i_addr3, p->bssid, 6);

	seq = (short*)wh->i_seq;
	*seq = seqfn(p->seq, 0);
}

void send_frame(struct params *p, void *buf, int len)
{
	int rc;

	rc = inject(p->tx, buf, len);
	if (rc == -1) {
		if (errno == EMSGSIZE)
			warnx("inject(len %d)", len);
		else
			err(1, "inject(len %d)", len);
	} else if (rc != len)
		errx(1, "injected %d but only %d sent", rc, len);
	p->seq++;
}

void send_probe_request(struct params *p)
{
	char buf[2048];
	struct ieee80211_frame *wh;
	char *data;
	int len;

	memset(buf, 0, sizeof(buf));

	wh = (struct ieee80211_frame*) buf;
	fill_basic(wh, p);
	wh->i_fc[0] |= IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_PROBE_REQ;

	memset(wh->i_addr1, 0xFF, 6);
	memset(wh->i_addr3, 0xFF, 6);

	data = (char*) (wh + 1);
	*data++ = 0; /* SSID */
	*data++ = strlen(p->ssid);
	strcpy(data, p->ssid);
	data += strlen(p->ssid);

	*data++ = 1; /* rates */
	*data++ = 4;
	*data++ = 2 | 0x80;
	*data++ = 4 | 0x80;
	*data++ = 11;
	*data++ = 22;

	len = data - (char*)wh;

	send_frame(p, buf, len);
}

void send_auth(struct params *p)
{
	char buf[2048];
	struct ieee80211_frame *wh;
	char *data;
	int len;

	memset(buf, 0, sizeof(buf));

	wh = (struct ieee80211_frame*) buf;
	fill_basic(wh, p);
	wh->i_fc[0] |= IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_AUTH;

	data = (char*) (wh + 1);

	/* algo */
	*data++ = 0; 
	*data++ = 0;

	/* transaction no. */
	*data++ = 1;
	*data++ = 0;

	/* status code */
	*data++ = 0;
	*data++ = 0;

	len = data - (char*)wh;

	send_frame(p, buf, len);
}

/* 
 * Add an ssid element to a frame.
 */
static u_int8_t *
ieee80211_add_ssid(u_int8_t *frm, const u_int8_t *ssid, u_int len)
{
	*frm++ = IEEE80211_ELEMID_SSID;
	*frm++ = len;
	memcpy(frm, ssid, len);
	return frm + len;
}

void send_assoc(struct params *p)
{
	union {
		struct ieee80211_frame w;
		char buf[2048];
	} u;
	struct ieee80211_frame *wh;
	char *data;
	int len, capinfo, lintval;

	memset(&u, 0, sizeof(u));

	wh = (struct ieee80211_frame*) &u.w;
	fill_basic(wh, p);
	wh->i_fc[0] |= IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_ASSOC_REQ;

	data = (char*) (wh + 1);
	
	/* capability */
	capinfo = IEEE80211_CAPINFO_ESS;
	if (p->wep_len)
		capinfo |= IEEE80211_CAPINFO_PRIVACY;
	*(uint16_t *)data = htole16(capinfo);
	data += 2;

	/* listen interval */
	*(uint16_t *)data = htole16(100);
	data += 2;

	data = ieee80211_add_ssid(data, p->ssid, strlen(p->ssid));

	*data++ = 1; /* rates */
	*data++ = 4;
	*data++ = 2 | 0x80;
	*data++ = 4 | 0x80;
	*data++ = 11;
	*data++ = 22;

	len = data - (char*)wh;

	send_frame(p, u.buf, len);
}

int for_me(struct ieee80211_frame *wh, char *mac)
{
	return memcmp(wh->i_addr1, mac, 6) == 0;	
}

int from_ap(struct ieee80211_frame *wh, char *mac)
{
	return memcmp(wh->i_addr2, mac, 6) == 0;	
}

void ack(struct params *p, struct ieee80211_frame *wh)
{       
        if (memcmp(wh->i_addr1, p->mac, 6) != 0)
                return;

        if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_CTL)
                return;

        send_ack(p->tx, wh->i_addr2);
}

void generic_process(struct ieee80211_frame *wh, struct params *p, int len)
{
	int type, stype;
	int dup = 0;

#if 0
	ack(p, wh);
#endif

#if 0
	if (!for_me(wh, p->mac))
		return;
#endif
	/* ignore my own shit */
	if (memcmp(wh->i_addr2, p->mac, 6) == 0) {
		return;
	}

	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	stype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

	if (for_me(wh, p->mac) && type == IEEE80211_FC0_TYPE_DATA) {
		/* sequence number & dups */
		if (p->seq_rx == -1)
			p->seq_rx = seqno(wh);
		else {
			int s = seqno(wh);
			
			if (s > p->seq_rx) {
				/* normal case */
				if (p->seq_rx + 1 == s) {
#if 0				
					printf("S=%d\n", s);
#endif					
					p->seq_rx = s;
				}	
				else { /* future */
#if 0				
					printf("Got seq %d, prev %d\n",
					       s, p->seq_rx);
#endif					       
					p->seq_rx = s;
				}
			} else { /* we got pas stuff... */
				if (p->seq_rx - s > 1000) {
#if 0				
					printf("Seqno wrap seq %d, last %d\n",
					       s, p->seq_rx);
#endif					       
					/* seqno wrapping ? */
					p->seq_rx = 0;
				}
				else { /* dup */
					dup = 1;
#if 0
					printf("Got dup seq %d, last %d\n",
					       s, p->seq_rx);
#endif					       
				}
			}
		}
	}
#if 0
	if (wh->i_fc[1] & IEEE80211_FC1_RETRY) {
		printf("Got retry\n");
	}
#endif	
#if 0
	if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) != IEEE80211_FC0_TYPE_CTL) {
		int rc = send_ack(p->tx, wh->i_addr2);
		if (rc == -1)
			err(1, "send_ack()");
		if (rc != 10) {
			printf("Wrote ACK %d/%d\n", rc, 10);
			exit(1);
		}
	}
#endif

	/* data frames */
	if (type == IEEE80211_FC0_TYPE_DATA && !dup) {
		char *ptr;
		char src[6], dst[6];
		int rc;

		if (wh->i_fc[1] & IEEE80211_FC1_DIR_FROMDS) {
			if (memcmp(wh->i_addr2, p->ap, 6) != 0)
				return;
		} else {
			if (memcmp(wh->i_addr1, p->ap, 6) != 0)
				return;
		}
		

		if (p->state < S_ASSOCIATED) {
			printf("Got data when not associated!\n");
			return;
		}
		if (stype != IEEE80211_FC0_SUBTYPE_DATA) {
			printf("Got weird data frame stype=%d\n",
			       stype >> IEEE80211_FC0_SUBTYPE_SHIFT);
			return;
		}

		if (wh->i_fc[1] & IEEE80211_FC1_DIR_FROMDS) {
			memcpy(src, wh->i_addr3, 6);
			memcpy(dst, wh->i_addr1, 6);
		} else {
			memcpy(src, wh->i_addr2, 6);
			memcpy(dst, wh->i_addr3, 6);
		}
		
		ptr = (char*) (wh + 1);

		if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
			if (!p->wep_len) {
				char srca[3*6];
				char dsta[3*6];

				mac2str(srca, src);
				mac2str(dsta, dst);
				printf("Got wep but i aint wep %s->%s %d\n",
				       srca, dsta, len-sizeof(*wh)-8);
				return;
			}
			
			if (wep_decrypt(wh, len, p->wep_key, p->wep_len) == -1){
				char srca[3*6];
				char dsta[3*6];

				mac2str(srca, src);
				mac2str(dsta, dst);
				printf("Can't decrypt %s->%s %d\n", srca, dsta,
				       len-sizeof(*wh)-8);
				return;
			}

			ptr += 4;
			len -= 8;
		}

		/* ether header */
		ptr += 8 - 2;
		ptr -= 6;
		memcpy(ptr, src, 6);
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
}

int get_probe_response(struct params *p)
{
	char buf[4096];
	int rc;
	struct ieee80211_frame *wh;
	char *data;
	int ess;
	int wep;
	char *ssid;
	char from[18];
	char bssid[18];

	rc = sniff(p->rx, buf, sizeof(buf));
	if (rc == -1)
		err(1, "sniff()");

	wh = get_wifi(buf, &rc);
	if (!wh)
		return 0;

	generic_process(wh, p, rc);

	if (!for_me(wh, p->mac))
		return 0;

	if (!frame_type(wh, IEEE80211_FC0_TYPE_MGT,
			IEEE80211_FC0_SUBTYPE_PROBE_RESP))
		return 0;

	data = (char*) (wh+1);
	data += 8; /* Timestamp */
	data += 2; /* Beacon Interval */
	ess = *data & 1;
	wep = (*data & IEEE80211_CAPINFO_PRIVACY) ? 1 : 0;
	data += 2; /* capability */

	/* ssid */
	if (*data != 0) {
		printf("Warning, expecting SSID got %x\n", *data);
		return 0;
	}
	data++;
	ssid = data+1;
	data += 1 + *data;
	if (*data != 1) {
		printf("Warning, expected rates got %x\n", *data);
		return 0;
	}
	*data = 0;

	/* rates */
	data++;

	mac2str(from, wh->i_addr2);
	mac2str(bssid, wh->i_addr3);
	printf("Got response from %s [%s] [%s] ESS=%d WEP=%d\n",
	       from, bssid, ssid, ess, wep);

	if (strcmp(ssid, p->ssid) != 0)
		return 0;

	memcpy(p->ap, wh->i_addr2, 6);
	memcpy(p->bssid, wh->i_addr3, 6);
	return 1;
}

int get_auth(struct params *p)
{
	char buf[4096];
	int rc;
	struct ieee80211_frame *wh;
	short *data;

	rc = sniff(p->rx, buf, sizeof(buf));
	if (rc == -1)
		err(1, "sniff()");

	wh = get_wifi(buf, &rc);
	if (!wh)
		return 0;

	generic_process(wh, p, rc);

	if (!for_me(wh, p->mac))
		return 0;

	if (!from_ap(wh, p->ap))
		return 0;

	if (!frame_type(wh, IEEE80211_FC0_TYPE_MGT,
			IEEE80211_FC0_SUBTYPE_AUTH))
		return 0;

	data = (short*) (wh+1);
	
	/* algo */
	if (le16toh(*data) != 0) {
		printf("Not open-system %d!\n", le16toh(*data));
		return 0;
	}
	data++;
	
	/* transaction no. */
	if (le16toh(*data) != 2) {
		printf("Got transaction %d!\n", le16toh(*data));
		return 0;
	}
	data++;

	/* status code */
	rc = le16toh(*data);
	if (rc == 0) {
		printf("Authenticated\n");
		return 1;
	}

	printf("Authentication failed code=%d\n", rc);
	return 0;
}

int get_assoc(struct params *p)
{
	char buf[4096];
	int rc;
	struct ieee80211_frame *wh;
	unsigned short *data;

	rc = sniff(p->rx, buf, sizeof(buf));
	if (rc == -1)
		err(1, "sniff()");

	wh = get_wifi(buf, &rc);
	if (!wh)
		return 0;

	generic_process(wh, p, rc);

	if (!for_me(wh, p->mac))
		return 0;

	if (!from_ap(wh, p->ap))
		return 0;

	if (!frame_type(wh, IEEE80211_FC0_TYPE_MGT,
			IEEE80211_FC0_SUBTYPE_ASSOC_RESP))
		return 0;
	

	data = (unsigned short*) (wh+1);
	
	data++; /* caps */

	/* status */
	rc = le16toh(*data++);
	if (rc != 0) {
		printf("Assoc failed code %d\n", rc);
		return 0;
	}

	/* aid */
	p->aid = le16toh(*data & ~( (1 << 15) | (1 << 14)));
	printf("Association ID=%d\n", p->aid);
	
	return 1;
}

void read_wifi(struct params *p)
{
	char buf[4096];
	int rc;
	struct ieee80211_frame *wh;
	int type, stype;

	rc = sniff(p->rx, buf, sizeof(buf));
	if (rc == -1)
		err(1, "sniff()");

	wh = get_wifi(buf, &rc);
	if (!wh)
		return;

	generic_process(wh, p, rc);

	if (!for_me(wh, p->mac))
		return;

	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	stype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

	/* control frames */
	if (type == IEEE80211_FC0_TYPE_CTL) {
		switch (stype) {
		case IEEE80211_FC0_SUBTYPE_ACK:
			if (p->state == S_WAIT_ACK)
				p->state = S_ASSOCIATED;
			break;
		
		case IEEE80211_FC0_SUBTYPE_RTS:
#if 0		
			printf("Got RTS\n");
#endif			
			break;
	
		default:
			printf("Unknown CTL frame %d\n",
			       stype >> IEEE80211_FC0_SUBTYPE_SHIFT);
			abort();
			break;
		}
		return;
	}

	if (!from_ap(wh, p->ap))
		return;

	if (type != IEEE80211_FC0_TYPE_MGT)
		return;

	if (stype == IEEE80211_FC0_SUBTYPE_DEAUTH ||
	    stype == IEEE80211_FC0_SUBTYPE_DISASSOC) {
		printf("Got management! %d\n",
		       stype >> IEEE80211_FC0_SUBTYPE_SHIFT);
		p->seq_rx = -1;
		p->state = S_START;
	}

	return;
}

void read_tap(struct params *p)
{
	char *ptr;
	int len = sizeof(p->packet);
	int offset;
	char mac[6];
	struct ieee80211_frame *wh;

	ptr = p->packet;
	offset = sizeof(struct ieee80211_frame) + 8 - 14;
	if (p->wep_len)
		offset += 4;

	ptr += offset;
	len -= offset;

	/* read packet */
	memset(p->packet, 0, sizeof(p->packet));
	p->packet_len = read(p->tap, ptr, len);
	if (p->packet_len == -1)
		err(1, "read()");

	/* 802.11 header */
	wh = (struct ieee80211_frame*) p->packet;
	memcpy(mac, ptr, sizeof(mac));
	fill_basic(wh, p);
	memcpy(wh->i_addr3, mac, sizeof(wh->i_addr3));
	wh->i_fc[0] |= IEEE80211_FC0_TYPE_DATA;
	wh->i_fc[1] |= IEEE80211_FC1_DIR_TODS;
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

	p->packet_len += offset;

	/* WEP */
	if (p->wep_len) {
		ptr = (char*) (wh+1);
		memcpy(ptr, &p->wep_iv, 3);
		ptr[3] = 0;
		p->wep_iv++;

		wep_encrypt(wh, p->packet_len, p->wep_key, p->wep_len);
		p->packet_len += 4; /* ICV */
	}
}

int main(int argc, char *argv[])
{
	char* ssid = 0;
	char mac[] = { 0x00, 0x00, 0xde, 0xfa, 0xce, 0xd };
	int ch;
	struct params p;
	char *iface = "wlan0";
	char *tap = "tap0";
	int timeout = 50*1000;
	struct timeval start;

	memset(&p, 0, sizeof(p));
	p.wep_len = 0;
	p.wep_iv = 0;
	p.state = S_START;

	while ((ch = getopt(argc, argv, "hm:s:i:w:t:b:")) != -1) {
		switch (ch) {
		case 'b':
			if (str2mac(p.bssid, optarg)) {
				printf("Error parsing BSSID\n");
				exit(1);
			}
			memcpy(p.ap, p.bssid, sizeof(p.ap));
			p.state = S_SEND_AUTH;
			break;

		case 's':
			ssid = optarg;
			break;

		case 'm':
			if (str2mac(mac, optarg)) {
				printf("Error parsing MAC\n");
				exit(1);
			}
			break;

		case 'i':
			iface = optarg;
			break;

		case 'w':
			if (str2wep(p.wep_key, &p.wep_len, optarg)) {
				printf("Error parsing WEP key\n");
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

	if (!ssid)
		usage(argv[0]);

	p.mac = mac;
	p.ssid = ssid;
	p.seq = getpid();
	p.seq_rx = -1;
	if (open_rxtx(iface, &p.rx, &p.tx) == -1)
		err(1, "open_rxtx()");
	p.tap = open_tap(tap);
	if (p.tap == -1)
		err(1, "open_tap()");
	if (set_iface_mac(tap, mac) == -1)
		err(1, "set_iface_mac()");

	while (1) {
		/* check for timeouts */
		switch (p.state) {
		case S_WAIT_PROBE_RES:
		case S_WAIT_AUTH:
		case S_WAIT_ASSOC:
		case S_WAIT_ACK:
			do {
				int rc;
				struct timeval tv;
				int elapsed = 0;

				/* check timeout */
				if (gettimeofday(&tv, NULL) == -1)
					err(1, "gettimeofday()");
				elapsed = tv.tv_sec - start.tv_sec;
				if (elapsed == 0) {
					elapsed = tv.tv_usec - start.tv_usec;
				} else {
					elapsed *= (elapsed-1)*1000*1000;
					elapsed += 1000*1000 - start.tv_usec;
					elapsed += tv.tv_usec;
				}
				if (elapsed >= timeout)
					rc = 0;
				else {	
					fd_set fds;

					FD_ZERO(&fds);
					FD_SET(p.rx, &fds);
					
					elapsed = timeout - elapsed;
					tv.tv_sec = elapsed/1000/1000;
					elapsed -= tv.tv_sec*1000*1000;
					tv.tv_usec = elapsed;

					rc = select(p.rx+1, &fds, NULL,
						    NULL, &tv);
					if (rc == -1)
						err(1, "select()");
				}

				/* timeout */
				if (!rc) {
#if 0
					printf("Timeout\n");
#endif					
					p.state--;
				}	

			} while(0);
			break;
		}

		switch (p.state) {
		case S_START:
			p.state = S_SEND_PROBE_REQ;
			break;

		case S_SEND_PROBE_REQ:
			printf("Sending probe request for %s\n", ssid);
			send_probe_request(&p);
			p.state = S_WAIT_PROBE_RES;
			if (gettimeofday(&start, NULL) == -1)
				err(1, "gettimeofday()");
			break;

		case S_WAIT_PROBE_RES:
			if (get_probe_response(&p)) {
				p.state = S_SEND_AUTH;
			}
			break;

		case S_SEND_AUTH:
			do {
				char apmac[18];

				mac2str(apmac, p.ap);
				printf("Sending auth to %s\n", apmac);
				send_auth(&p);
				p.state = S_WAIT_AUTH;
				if (gettimeofday(&start, NULL) == -1)
					err(1, "gettimeofday()");
			} while(0);
			break;

		case S_WAIT_AUTH:
			if (get_auth(&p)) {
				p.state = S_SEND_ASSOC;
			}
			break;

		case S_SEND_ASSOC:
			printf("Sending assoc\n");
			send_assoc(&p);
			p.state = S_WAIT_ASSOC;
			if (gettimeofday(&start, NULL) == -1)
				err(1, "gettimeofday()");
			break;

		case S_WAIT_ASSOC:
			if (get_assoc(&p)) {
				printf("Associated\n");
				p.state = S_ASSOCIATED;
			}
			break;

		case S_ASSOCIATED:
			do {
				fd_set fds;
				int max;

				FD_ZERO(&fds);
				FD_SET(p.rx, &fds);
				FD_SET(p.tap, &fds);
				max = (p.rx > p.tap) ? p.rx : p.tap;
				
				max = select(max+1, &fds, NULL, NULL, NULL);
				if (max == -1)
					err(1, "select()");
					
				if (FD_ISSET(p.tap, &fds)) {
					read_tap(&p);
					p.state = S_SEND_DATA;
				}
				if (FD_ISSET(p.rx, &fds)) {
					read_wifi(&p);
				}
			} while(0);
			break;

		case S_SEND_DATA:
			send_frame(&p, p.packet, p.packet_len);
			do {
				struct ieee80211_frame *wh;

				wh = (struct ieee80211_frame*) p.packet;
				wh->i_fc[1] |= IEEE80211_FC1_RETRY;
			} while (0);
			p.state = S_WAIT_ACK;
			if (gettimeofday(&start, NULL) == -1)
				err(1, "gettimeofday()");
			break;

		case S_WAIT_ACK:
			read_wifi(&p);
			break;

		default:
			printf("Unknown state %d\n", p.state);
			abort();
			break;
		}
	}

	exit(0);
}
