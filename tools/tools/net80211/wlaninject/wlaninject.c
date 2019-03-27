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
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <net/bpf.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/if_media.h>
#include <string.h>
#include <net80211/ieee80211.h>
#include <net80211/ieee80211_ioctl.h>
#include <net80211/ieee80211_freebsd.h>
#include <net80211/ieee80211_radiotap.h>
#include <sys/endian.h>
#include <assert.h>

void setup_if(char *dev, int chan) {
	int s;
	struct ifreq ifr;
	unsigned int flags;
	struct ifmediareq ifmr;
	int *mwords;
	struct ieee80211req ireq;

	if ((s = socket(PF_INET, SOCK_DGRAM, 0)) == -1)
		err(1, "socket()");

	/* chan */
	memset(&ireq, 0, sizeof(ireq));
	snprintf(ireq.i_name, sizeof(ireq.i_name), "%s", dev);
	ireq.i_type = IEEE80211_IOC_CHANNEL;
	ireq.i_val = chan;
	if (ioctl(s, SIOCS80211, &ireq) == -1)
		err(1, "ioctl(SIOCS80211)");

	/* UP & PROMISC */
	memset(&ifr, 0, sizeof(ifr));
	snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", dev);
	if (ioctl(s, SIOCGIFFLAGS, &ifr) == -1)
		err(1, "ioctl(SIOCGIFFLAGS)");
	flags = (ifr.ifr_flags & 0xffff) | (ifr.ifr_flagshigh << 16);
	flags |= IFF_UP | IFF_PPROMISC;
	ifr.ifr_flags = flags & 0xffff;
	ifr.ifr_flagshigh = flags >> 16;
	if (ioctl(s, SIOCSIFFLAGS, &ifr) == -1)
		err(1, "ioctl(SIOCSIFFLAGS)");

	close(s);
}

int open_bpf(char *dev)
{
	char buf[64];
	int i;
	int fd;
	struct ifreq ifr;
	unsigned int dlt = DLT_IEEE802_11_RADIO;

	for (i = 0; i < 64; i++) {
		sprintf(buf, "/dev/bpf%d", i);

		fd = open(buf, O_RDWR);
		if (fd != -1)
			break;
		else if (errno != EBUSY)
			err(1, "open()");
	}
	if (fd == -1) {
		printf("Can't find bpf\n");
		exit(1);
	}

	memset(&ifr, 0, sizeof(ifr));
	snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", dev);
	if (ioctl(fd, BIOCSETIF, &ifr) == -1)
		err(1, "ioctl(BIOCSETIF)");

	if (ioctl(fd, BIOCSDLT, &dlt) == -1)
		err(1, "ioctl(BIOCSDLT)");

	i = 1;
	if (ioctl(fd, BIOCIMMEDIATE, &i) == -1)
		err(1, "ioctl(BIOCIMMEDIATE)");

	return fd;
}

void inject(int fd, void *buf, int buflen, struct ieee80211_bpf_params *p)
{
	struct iovec iov[2];
	int totlen;
	int rc;

	iov[0].iov_base = p;
	iov[0].iov_len = p->ibp_len;

	iov[1].iov_base = buf;
	iov[1].iov_len = buflen;
	totlen = iov[0].iov_len + iov[1].iov_len;

	rc = writev(fd, iov, sizeof(iov)/sizeof(struct iovec));
	if (rc == -1)
		err(1, "writev()");
	if (rc != totlen) {
		printf("Wrote only %d/%d\n", rc, totlen);
		exit(1);
	}
}

void usage(char *progname)
{
	printf("Usage: %s <opts>\n"
		"Physical:\n"
		"\t-i\t<iface>\n"
		"\t-c\t<chan>\n"
		"\t-N\tno ack\n"
		"\t-V\t<iface> [verify via iface whether packet was mangled]\n"
		"\t-W\tWME AC\n"
		"\t-X\ttransmit rate (Mbps)\n"
		"\t-P\ttransmit power (device units)\n"
		"802.11:\n"
		"\t-h\tthis lame message\n"
		"\t-v\t<version>\n"
		"\t-t\t<type>\n"
		"\t-s\t<subtype>\n"
		"\t-T\tto ds\n"
		"\t-F\tfrom ds\n"
		"\t-m\tmore frags\n"
		"\t-r\tretry\n"
		"\t-p\tpower\n"
		"\t-d\tmore data\n"
		"\t-w\twep\n"
		"\t-o\torder\n"
		"\t-u\t<duration>\n"
		"\t-1\t<addr 1>\n"
		"\t-2\t<addr 2>\n"
		"\t-3\t<addr 3>\n"
		"\t-n\t<seqno>\n"
		"\t-f\t<fragno>\n"
		"\t-4\t<addr 4>\n"
		"\t-b\t<payload file>\n"
		"\t-l\t<len>\n"
		"Management:\n"
		"\t-e\t<info element [hex digits 010203... first is type]>\n"
		"\t-S\t<SSID>\n"
		"\t-a\t<algo no>\n"
		"\t-A\t<transaction>\n"
		"\t-C\t<status code>\n"
		"\t-R\tstandard rates\n"
	       , progname);	
	exit(1);	
}

int str2type(const char *type)
{
#define	equal(a,b)	(strcasecmp(a,b) == 0)
	if (equal(type, "mgt"))
		return IEEE80211_FC0_TYPE_MGT >> IEEE80211_FC0_TYPE_SHIFT;
	else if (equal(type, "ctl"))
		return IEEE80211_FC0_TYPE_CTL >> IEEE80211_FC0_TYPE_SHIFT;
	else if (equal(type, "data"))
		return IEEE80211_FC0_TYPE_DATA >> IEEE80211_FC0_TYPE_SHIFT;

	return atoi(type) & 3;
#undef equal
}

int str2subtype(const char *subtype)
{
#define	equal(a,b)	(strcasecmp(a,b) == 0)
	if (equal(subtype, "preq") || equal(subtype, "probereq"))
		return IEEE80211_FC0_SUBTYPE_PROBE_REQ >>
		       IEEE80211_FC0_SUBTYPE_SHIFT;
	else if (equal(subtype, "auth"))
		return IEEE80211_FC0_SUBTYPE_AUTH >>
		       IEEE80211_FC0_SUBTYPE_SHIFT;
	else if (equal(subtype, "areq") || equal(subtype, "assocreq"))
		return IEEE80211_FC0_SUBTYPE_ASSOC_REQ >>
		       IEEE80211_FC0_SUBTYPE_SHIFT;
	else if (equal(subtype, "data"))
		return IEEE80211_FC0_SUBTYPE_DATA >>
		       IEEE80211_FC0_SUBTYPE_SHIFT;

	return atoi(subtype) & 0xf;
#undef equal
}

void str2mac(unsigned char *mac, char *str)
{
	unsigned int macf[6];
	int i;
        
	if (sscanf(str, "%x:%x:%x:%x:%x:%x",
		   &macf[0], &macf[1], &macf[2],
		   &macf[3], &macf[4], &macf[5]) != 6) {
		   printf("can't parse mac %s\n", str);
		   exit(1);
	}
	
	for (i = 0; i < 6; i++)
		*mac++ = (unsigned char) macf[i];
}

int str2wmeac(const char *ac)
{
#define	equal(a,b)	(strcasecmp(a,b) == 0)
	if (equal(ac, "ac_be") || equal(ac, "be"))
		return WME_AC_BE;
	if (equal(ac, "ac_bk") || equal(ac, "bk"))
		return WME_AC_BK;
	if (equal(ac, "ac_vi") || equal(ac, "vi"))
		return WME_AC_VI;
	if (equal(ac, "ac_vo") || equal(ac, "vo"))
		return WME_AC_VO;
	errx(1, "unknown wme access class %s", ac);
#undef equal
}

int str2rate(const char *rate)
{
	switch (atoi(rate)) {
	case 54: return 54*2;
	case 48: return 48*2;
	case 36: return 36*2;
	case 24: return 24*2;
	case 18: return 18*2;
	case 12: return 12*2;
	case 9: return 9*2;
	case 6: return 6*2;
	case 11: return 11*2;
	case 5: return 11;
	case 2: return 2*2;
	case 1: return 1*2;
	}
	errx(1, "unknown transmit rate %s", rate);
}

const char *rate2str(int rate)
{
	static char buf[30];

	if (rate == 11)
		return "5.5";
	snprintf(buf, sizeof(buf), "%u", rate/2);
	return buf;
}

int load_payload(char *fname, void *buf, int len)
{
	int fd;
	int rc;

	if ((fd = open(fname, O_RDONLY)) == -1)
		err(1, "open()");

	if ((rc = read(fd, buf, len)) == -1)
		err(1, "read()");

	close(fd);
	printf("Read %d bytes from %s\n", rc, fname);
	return rc;
}

int header_len(struct ieee80211_frame *wh)
{
	int len = sizeof(*wh);

	switch (wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) {
	case IEEE80211_FC0_TYPE_MGT:
		switch (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) {
		case IEEE80211_FC0_SUBTYPE_ASSOC_REQ:
			len += 2 + 2; /* capa & listen */
			break;

		case IEEE80211_FC0_SUBTYPE_ASSOC_RESP:
			len += 2 + 2 + 2; /* capa & status & assoc */
			break;

		case IEEE80211_FC0_SUBTYPE_REASSOC_REQ:
			len += 2 + 2 + 6; /* capa & listen & AP */
			break;

		case IEEE80211_FC0_SUBTYPE_REASSOC_RESP:
			len += 2 + 2 + 2; /* capa & status & assoc */
			break;
			
		case IEEE80211_FC0_SUBTYPE_PROBE_REQ:
		case IEEE80211_FC0_SUBTYPE_ATIM:
			break;

		case IEEE80211_FC0_SUBTYPE_PROBE_RESP:
		case IEEE80211_FC0_SUBTYPE_BEACON:
			len += 8 + 2 + 2; /* time & bint & capa */
			break;

		case IEEE80211_FC0_SUBTYPE_DISASSOC:
			len += 2; /* reason */
			break;

		case IEEE80211_FC0_SUBTYPE_AUTH:
			len += 2 + 2 + 2; /* algo & seq & status */
			break;

		case IEEE80211_FC0_SUBTYPE_DEAUTH:
			len += 2; /* reason */
			break;

		default:
			errx(1, "Unknown MGT subtype 0x%x",
				wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK);
		}
		break;
	
	case IEEE80211_FC0_TYPE_CTL:
		switch (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) {
		case IEEE80211_FC0_SUBTYPE_PS_POLL:
			len = sizeof(struct ieee80211_frame_pspoll);
			break;
		
		case IEEE80211_FC0_SUBTYPE_RTS:
			len = sizeof(struct ieee80211_frame_rts);
			break;
		
		case IEEE80211_FC0_SUBTYPE_CTS:
			len = sizeof(struct ieee80211_frame_cts);
			break;
		
		case IEEE80211_FC0_SUBTYPE_ACK:
			len = sizeof(struct ieee80211_frame_ack);
			break;
		
		case IEEE80211_FC0_SUBTYPE_CF_END_ACK:
		case IEEE80211_FC0_SUBTYPE_CF_END:
			len = sizeof(struct ieee80211_frame_cfend);
			break;
		
		default:
			errx(1, "Unknown CTL subtype 0x%x",
				wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK);
		}
		break;
	
	case IEEE80211_FC0_TYPE_DATA:
		if (wh->i_fc[1] & IEEE80211_FC1_DIR_DSTODS)
			len += sizeof(wh->i_addr1);
		break;

	default:
		errx(1, "Unknown type 0x%x",
			wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK);
		exit(1);
	}

	return len;
}

int parse_ie(char *str, unsigned char *ie, int len)
{
	int digits = 0;
	char num[3];
	int conv = 0;
	int ielen;

	ielen = strlen(str)/2;
	if (ielen < 1 || (strlen(str) % 2)) {
		printf("Invalid IE %s\n", str);
		exit(1);
	}

	num[2] = 0;
	while (ielen) {
		num[digits++] = *str;
		str++;
		if (digits == 2) {
			unsigned int x;

			sscanf(num, "%x", &x);

			if (len <= 0) {
				printf("No space for IE\n");
				exit(1);
			}

			*ie++ = (unsigned char) x;
			len--;
			ielen--;

			/* first char */
			if (conv == 0) {
				if (len == 0) {
					printf("No space for IE\n");
					exit(1);
				}
				*ie++ = (unsigned char) ielen;
				len--;
				conv++;
			}
			conv++;
			digits = 0;
		}
	}

	return conv;
}

int possible_match(struct ieee80211_frame *sent, int slen,
		   struct ieee80211_frame *got, int glen)
{
	if (slen != glen)
		return 0;

	if (memcmp(sent->i_addr1, got->i_addr1, 6) != 0)
		printf("Addr1 doesn't match\n");

	if ((sent->i_fc[0] & IEEE80211_FC0_TYPE_MASK) !=
	    (got->i_fc[0] & IEEE80211_FC0_TYPE_MASK))
		return 0;

	if ((sent->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) !=
	    (got->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK))
		return 0;

	/* Good enough for CTL frames I guess */
	if ((got->i_fc[0] & IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_CTL)
		return 1;

	if (memcmp(sent->i_addr2, got->i_addr2, 6) == 0 &&
	    memcmp(sent->i_addr3, got->i_addr3, 6) == 0)
	    	return 1;

	return 0;
}

int do_verify(struct ieee80211_frame *sent, int slen, void *got, int glen)
{
#define BIT(n)  (1<<(n))
	struct bpf_hdr *bpfh = got;
	struct ieee80211_frame *wh;
	struct ieee80211_radiotap_header *rth;
	int i;
	unsigned char *ptr, *ptr2;
	uint32_t present;
	uint8_t rflags;

	/* get the 802.11 header */
	glen -= bpfh->bh_hdrlen;
	assert(glen > 0);
	if (bpfh->bh_caplen != glen) {
		abort();
	}
	rth = (struct ieee80211_radiotap_header*)
	      ((char*) bpfh + bpfh->bh_hdrlen);
	glen -= rth->it_len;
	assert(glen > 0);
	wh = (struct ieee80211_frame*) ((char*)rth + rth->it_len);

        /* check if FCS/CRC is included in packet */
	present = le32toh(rth->it_present);
	if (present & BIT(IEEE80211_RADIOTAP_FLAGS)) {
		if (present & BIT(IEEE80211_RADIOTAP_TSFT))
			rflags = ((const uint8_t *)rth)[8];
		else
			rflags = ((const uint8_t *)rth)[0];
	} else  
		rflags = 0;
	if (rflags & IEEE80211_RADIOTAP_F_FCS)
		glen -= IEEE80211_CRC_LEN;
	assert(glen > 0);
	
	/* did we receive the packet we sent? */
	if (!possible_match(sent, slen, wh, glen))
		return 0;

	/* check if it got mangled */
	if (memcmp(sent, wh, slen) == 0) {
		printf("No mangling---got it perfect\n");
		return 1;
	}

	/* print differences */
	printf("Got mangled:\n");
	ptr = (unsigned char*) sent;
	ptr2 = (unsigned char *) wh;
	for (i = 0; i < slen; i++, ptr++, ptr2++) {
		if (*ptr != *ptr2)
			printf("Position: %d Was: %.2X Got: %.2X\n",
			       i, *ptr, *ptr2);
	}
	return -1;
#undef BIT
}

int main(int argc, char *argv[])
{
	int fd, fd2;
	char *iface = "wlan0";
	char *verify = NULL;
	int chan = 1;
	struct {
		struct ieee80211_frame w;
		unsigned char buf[2048];
	} __packed u;
	int len = 0;
	int ch;
	struct ieee80211_bpf_params params;
	struct ieee80211_frame *wh = &u.w;
	unsigned char *body = u.buf;

	memset(&u, 0, sizeof(u));
	memset(&params, 0, sizeof(params));
	params.ibp_vers = IEEE80211_BPF_VERSION;
	params.ibp_len = sizeof(struct ieee80211_bpf_params) - 6,
	params.ibp_rate0 = 2;		/* 1 MB/s XXX */
	params.ibp_try0 = 1;		/* no retransmits */
	params.ibp_power = 100;		/* nominal max */
	params.ibp_pri = WME_AC_VO;	/* high priority */
	
	while ((ch = getopt(argc, argv,
	    "hv:t:s:TFmpdwou:1:2:3:4:b:i:c:l:n:f:e:S:a:A:C:NRV:W:X:P:")) != -1) {
		switch (ch) {
		case 'i':
			iface = optarg;
			break;
		
		case 'c':
			chan = atoi(optarg);
			break;

		case 'v':
			wh->i_fc[0] |= atoi(optarg)& IEEE80211_FC0_VERSION_MASK;
			break;

		case 't':
			wh->i_fc[0] |= str2type(optarg) <<
				       IEEE80211_FC0_TYPE_SHIFT;
			break;
		
		case 's':
			wh->i_fc[0] |= str2subtype(optarg) <<
				       IEEE80211_FC0_SUBTYPE_SHIFT;
			len = header_len(wh);
			body += len;
			break;

		case 'T':
			wh->i_fc[1] |= IEEE80211_FC1_DIR_TODS;
			break;
		
		case 'F':
			wh->i_fc[1] |= IEEE80211_FC1_DIR_FROMDS;
			break;

		case 'm':
			wh->i_fc[1] |= IEEE80211_FC1_MORE_FRAG;
			break;

		case 'r':
			wh->i_fc[1] |= IEEE80211_FC1_RETRY;
			break;

		case 'p':
			wh->i_fc[1] |= IEEE80211_FC1_PWR_MGT;
			break;

		case 'd':
			wh->i_fc[1] |= IEEE80211_FC1_MORE_DATA;
			break;

		case 'w':
			wh->i_fc[1] |= IEEE80211_FC1_PROTECTED;
			break;

		case 'o':
			wh->i_fc[1] |= IEEE80211_FC1_ORDER;
			break;

		case 'u':
			*(uint16_t*)wh->i_dur = htole16(atoi(optarg));
			break;

		case '1':
			str2mac(wh->i_addr1, optarg);
			break;
		
		case '2':
			str2mac(wh->i_addr2, optarg);
			break;

		case '3':
			str2mac(wh->i_addr3, optarg);
			break;
		
		case '4':
			str2mac(body, optarg);
			break;

		case 'n':
			*(uint16_t*)wh->i_seq |= htole16((atoi(optarg) & 0xfff)
				<< IEEE80211_SEQ_SEQ_SHIFT);
			break;

		case 'f':
			wh->i_seq[0] |= atoi(optarg) & 0xf;
			break;

		case 'b':
			len += load_payload(optarg, body,
					    u.buf + sizeof(u.buf) - body);
			break;

		case 'l':
			len = atoi(optarg);
			break;

		case 'e':
			do {
				int ln;

				ln = parse_ie(optarg, body,
					      u.buf + sizeof(u.buf) - body);
				len += ln;
				body += ln;
			} while(0);
			break;

		case 'S':
			do {
				int ln;
				int left = u.buf + sizeof(u.buf) - body;

				ln = strlen(optarg) & 0xff;
				if ((ln + 2) > left) {
					printf("No space for SSID\n");
					exit(1);
				}

				*body++ = 0;
				*body++ = ln;
				memcpy(body, optarg, ln);
				body += ln;
				len += ln + 2;
			} while(0);
			break;
		
		case 'R':
			do {
				unsigned char rates[] = "\x1\x4\x82\x84\xb\x16";
				int left = u.buf + sizeof(u.buf) - body;

				if ((sizeof(rates) - 1) > left) {
					printf("No space for rates\n");
					exit(1);
				}
				
				memcpy(body, rates, sizeof(rates) - 1);
				body += sizeof(rates) - 1;
				len += sizeof(rates) - 1;
			} while(0);
			break;

		case 'a':
			do {
				uint16_t *x = (uint16_t*) (wh+1);
				*x = htole16(atoi(optarg));
			} while(0);
			break;
		
		case 'A':
			do {
				uint16_t *x = (uint16_t*) (wh+1);
				x += 1;
				*x = htole16(atoi(optarg));
			} while(0);
			break;

		case 'C':
			do {
				uint16_t *x = (uint16_t*) (wh+1);

				if ((wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK)
				    == IEEE80211_FC0_SUBTYPE_AUTH)
				    x += 1;
				x += 1;
				*x = htole16(atoi(optarg));
			} while(0);
			break;

		case 'N':
			params.ibp_flags |= IEEE80211_BPF_NOACK;
			break;

		case 'V':
			verify = optarg;
			break;

		case 'W':
			params.ibp_pri = str2wmeac(optarg);
			break;

		case 'X':
			params.ibp_rate0 = str2rate(optarg);
			break;

		case 'P':
			params.ibp_power = atoi(optarg);
			break;

		case 'h':
		default:
			usage(argv[0]);
			break;
		}
	}

	if (!len) {
		usage(argv[0]);
		exit(1);
	}

	printf("Using interface %s on chan %d, transmit at %s Mbp/s\n",
		iface, chan, rate2str(params.ibp_rate0));
	setup_if(iface, chan);
	fd = open_bpf(iface);
	printf("Dose: %db\n", len);

	if (verify) {
		setup_if(verify, chan);
		fd2 = open_bpf(verify);
	}
	inject(fd, wh, len, &params);
	close(fd);
	if (verify) {
		char buf2[4096];
		int rc;
		int max = 10;
		int timeout = 2;
		fd_set fds;
		struct timeval tv;
		time_t start;

		printf("Verifying via %s\n", verify);
		start = time(NULL);
		while (max--) {
			FD_ZERO(&fds);
			FD_SET(fd2, &fds);

			tv.tv_usec = 0;
			tv.tv_sec = time(NULL) - start;
			if (tv.tv_sec >= timeout) {
				timeout = 0;
				break;
			}
			tv.tv_sec = timeout - tv.tv_sec;
			if (select(fd2+1, &fds, NULL, NULL, &tv) == -1)
				err(1, "select()");
			if (!FD_ISSET(fd2, &fds))
				continue;

			if ((rc = read(fd2, buf2, sizeof(buf2))) == -1)
				err(1, "read()");

			if (do_verify(wh, len, buf2, rc)) {
				max = 666;
				break;
			}
		}
		if (max != 666 || !timeout)
			printf("No luck\n");
		close(fd2);
	}

	exit(0);
}
