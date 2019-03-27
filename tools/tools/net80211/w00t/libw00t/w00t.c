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
#include <stdio.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/endian.h>
#include <sys/uio.h>
#include <unistd.h>
#include <net/if.h>
#include <string.h>
#include <sys/ioctl.h>
#include <net/bpf.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211.h>
#include <openssl/rc4.h>
#include <zlib.h>
#include "w00t.h"

int str2mac(char *mac, char *str)
{
	unsigned int macf[6];
	int i;

	if (sscanf(str, "%x:%x:%x:%x:%x:%x",
		   &macf[0], &macf[1], &macf[2],
		   &macf[3], &macf[4], &macf[5]) != 6)
		return -1;

	for (i = 0; i < 6; i++)
		*mac++ = (char) macf[i];
	
	return 0;
}

void mac2str(char *str, char* m)
{
	unsigned char *mac = m;
	sprintf(str, "%.2X:%.2X:%.2X:%.2X:%.2X:%.2X",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

short seqfn(unsigned short seq, unsigned short fn)
{
	unsigned short r = 0;

	assert(fn < 16);

	r = fn;
	r |=  ( (seq % 4096) << IEEE80211_SEQ_SEQ_SHIFT);
	return r;
}

unsigned short seqno(struct ieee80211_frame *wh)
{
	unsigned short *s = (unsigned short*) wh->i_seq;

	return (*s & IEEE80211_SEQ_SEQ_MASK) >> IEEE80211_SEQ_SEQ_SHIFT;
}

int open_bpf(char *dev, int dlt)
{
	int i;
	char buf[64];
	int fd = -1;
	struct ifreq ifr;

	for(i = 0;i < 16; i++) {
		sprintf(buf, "/dev/bpf%d", i);

		fd = open(buf, O_RDWR);
		if(fd == -1) {
			if(errno != EBUSY)
				return -1;
			continue;
		}
		else
			break;
	}

	if(fd == -1)
		return -1;

	strncpy(ifr.ifr_name, dev, sizeof(ifr.ifr_name)-1);
	ifr.ifr_name[sizeof(ifr.ifr_name)-1] = 0;

	if(ioctl(fd, BIOCSETIF, &ifr) < 0)
		return -1;

	if (ioctl(fd, BIOCSDLT, &dlt) < 0)
		return -1;

	i = 1;
	if (ioctl(fd, BIOCIMMEDIATE, &i) < 0)
		return -1;

	return fd;
}

int open_tx(char *iface)
{
	return open_bpf(iface, DLT_IEEE802_11_RADIO);
}

int open_rx(char *iface)
{
	return open_bpf(iface, DLT_IEEE802_11_RADIO);
}

int open_rxtx(char *iface, int *rx, int *tx)
{
	*rx = open_bpf(iface, DLT_IEEE802_11_RADIO);
	*tx = *rx;

	return *rx;
}

int inject(int fd, void *buf, int len)
{
	return inject_params(fd, buf, len, NULL);
}

int inject_params(int fd, void *buf, int len,
		  struct ieee80211_bpf_params *params)
{
	static struct ieee80211_bpf_params defaults = {
		.ibp_vers = IEEE80211_BPF_VERSION,
		/* NB: no need to pass series 2-4 rate+try */
		.ibp_len = sizeof(struct ieee80211_bpf_params) - 6,
		.ibp_rate0 = 2,		/* 1 MB/s XXX */
		.ibp_try0 = 1,		/* no retransmits */
		.ibp_flags = IEEE80211_BPF_NOACK,
		.ibp_power = 100,	/* nominal max */
		.ibp_pri = WME_AC_VO,	/* high priority */
	};
	struct iovec iov[2];
	int rc;

	if (params == NULL)
		params = &defaults;
	iov[0].iov_base = params;
	iov[0].iov_len = params->ibp_len;
	iov[1].iov_base = buf;
	iov[1].iov_len = len;

	rc = writev(fd, iov, 2);
	if (rc == -1)
		return rc;

	rc -= iov[0].iov_len; /* XXX could be negative */
	return rc;
}

int sniff(int fd, void *buf, int len)
{
	return read(fd, buf, len);
}

void *get_wifi(void *buf, int *len)
{
#define	BIT(n)	(1<<(n))
	struct bpf_hdr* bpfh = (struct bpf_hdr*) buf;
	struct ieee80211_radiotap_header* rth;
	uint32_t present;
	uint8_t rflags;
	void *ptr;

	/* bpf */
	*len -= bpfh->bh_hdrlen;

	if (bpfh->bh_caplen != *len) {
		assert(bpfh->bh_caplen < *len);
		*len = bpfh->bh_caplen;
	}
	assert(bpfh->bh_caplen == *len);

	/* radiotap */
	rth = (struct ieee80211_radiotap_header*)
	      ((char*)bpfh + bpfh->bh_hdrlen);
	/* XXX cache; drivers won't change this per-packet */
	/* check if FCS/CRC is included in packet */
	present = le32toh(rth->it_present);
	if (present & BIT(IEEE80211_RADIOTAP_FLAGS)) {
		if (present & BIT(IEEE80211_RADIOTAP_TSFT))
			rflags = ((const uint8_t *)rth)[8];
		else
			rflags = ((const uint8_t *)rth)[0];
	} else
		rflags = 0;
	*len -= rth->it_len;

	/* 802.11 CRC */
	if (rflags & IEEE80211_RADIOTAP_F_FCS)
		*len -= IEEE80211_CRC_LEN;

	ptr = (char*)rth + rth->it_len;
	return ptr;
#undef BIT
}

int send_ack(int fd, char *mac)
{
	static char buf[2+2+6];
	static char *p = 0;
	int rc;

	if (!p) {
		memset(buf, 0, sizeof(buf));
		buf[0] |= IEEE80211_FC0_TYPE_CTL | IEEE80211_FC0_SUBTYPE_ACK;
		p = &buf[4];
	}

	memcpy(p, mac, 6);

	rc = inject(fd, buf, sizeof(buf));
	return rc;
}

int open_tap(char *iface)
{
	char buf[64];

	snprintf(buf, sizeof(buf), "/dev/%s", iface);
	return open(buf, O_RDWR);
}

int set_iface_mac(char *iface, char *mac)
{
	int s, rc;
	struct ifreq ifr;

	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s == -1)
		return -1;
	
	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, iface);

	ifr.ifr_addr.sa_family = AF_LINK;
	ifr.ifr_addr.sa_len = 6;
	memcpy(ifr.ifr_addr.sa_data, mac, 6);

	rc = ioctl(s, SIOCSIFLLADDR, &ifr);

	close(s);

	return rc;
}

int str2wep(char *wep, int *len, char *str)
{
	int klen;

	klen = strlen(str);
	if (klen % 2)
		return -1;
	klen /= 2;

	if (klen != 5 && klen != 13)
		return -1;

	*len = klen;

	while (klen--) {
		unsigned int x;

		if (sscanf(str, "%2x", &x) != 1)
			return -1;
		
		*wep = (unsigned char) x;
		wep++;
		str += 2;
	}

	return 0;
}

int wep_decrypt(struct ieee80211_frame *wh, int len, char *key, int klen)
{
	RC4_KEY k;
	char seed[64];
	char *p = (char*) (wh+1);
	uLong crc = crc32(0L, Z_NULL, 0);
	uLong *pcrc;

	assert(sizeof(seed) >= klen + 3);
	memcpy(seed, p, 3);
	memcpy(&seed[3], key, klen);

	RC4_set_key(&k, klen+3, seed);
	
	len -= sizeof(*wh);
	len -= 4;
	p += 4;
	RC4(&k, len, p, p);

	crc = crc32(crc, p, len - 4);
	pcrc = (uLong*) (p+len-4);

	if (*pcrc == crc)
		return 0;

	return -1;
}

void wep_encrypt(struct ieee80211_frame *wh, int len, char *key, int klen)
{
	RC4_KEY k;
	char seed[64];
	char *p = (char*) (wh+1);
	uLong crc = crc32(0L, Z_NULL, 0);
	uLong *pcrc;

	assert(sizeof(seed) >= klen + 3);
	memcpy(seed, p, 3);
	memcpy(&seed[3], key, klen);

	RC4_set_key(&k, klen+3, seed);
	
	len -= sizeof(*wh);
	p += 4;
	crc = crc32(crc, p, len - 4);
	pcrc = (uLong*) (p+len-4);
	*pcrc = crc;

	RC4(&k, len, p, p);
}

int frame_type(struct ieee80211_frame *wh, int type, int stype)
{       
        if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) != type)
                return 0;

        if ((wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) != stype)
                return 0;

        return 1;
}

void hexdump(void *b, int len)
{
	unsigned char *p = (unsigned char*) b;

	while (len--)
		printf("%.2X ", *p++);
	printf("\n");	
}

int elapsed(struct timeval *past, struct timeval *now)
{       
        int el;
        
        el = now->tv_sec - past->tv_sec;
        assert(el >= 0);
        if (el == 0) {
                el = now->tv_usec - past->tv_usec;
        } else {
                el = (el - 1)*1000*1000;
                el += 1000*1000-past->tv_usec;
                el += now->tv_usec;
        }
        
        return el;
}

static int is_arp(struct ieee80211_frame *wh, int len)
{       
        /* XXX */
        if (len > (sizeof(*wh) + 4 + 4 + 39))
                return 0;

        return 1;
}

char *known_pt(struct ieee80211_frame *wh, int *len)
{
	static char *known_pt_arp = "\xAA\xAA\x03\x00\x00\x00\x08\x06";
	static char *known_pt_ip = "\xAA\xAA\x03\x00\x00\x00\x08\x00";
	int arp;

	arp = is_arp(wh, *len);
	*len = 8;
	if (arp)
		return known_pt_arp;
	else
		return known_pt_ip;
}
