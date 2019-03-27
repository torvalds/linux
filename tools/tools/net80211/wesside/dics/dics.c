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
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#define __FAVOR_BSD
#include <netinet/udp.h>

#if 0
#include <pcap.h>
#endif

#define MAGIC_LEN (20+8+5)

#define PRGA_LEN (1500-14-20-8)

#define BSD
//#define LINUX

#ifdef LINUX
struct ippseudo {
        struct  in_addr ippseudo_src;   /* source internet address */
        struct  in_addr ippseudo_dst;   /* destination internet address */
        u_char          ippseudo_pad;   /* pad, must be zero */
        u_char          ippseudo_p;     /* protocol */
        u_short         ippseudo_len;   /* protocol length */
};      
#endif

#define DPORT 6969
#define TTLSENT 128

int pps = 10;
int poll_rate =5;

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

void hexdump(unsigned char *ptr, int len) {
        while(len > 0) {
                printf("%.2X ", *ptr);
                ptr++; len--;
        }
        printf("\n");
}

int check_signal(int s, char* ip, unsigned char* ttl, unsigned short* port) {
	unsigned char buf[1024];
	int rd;
	struct msghdr msg;
	struct iovec iv;
	struct sockaddr_in s_in;
	struct {
		struct cmsghdr hdr;
		unsigned char ttl;
	} ctl;

	iv.iov_base = buf;
	iv.iov_len = sizeof(buf);

	memset(&msg, 0, sizeof(msg));
	memset(&ctl, 0, sizeof(ctl));
	msg.msg_name = &s_in;
	msg.msg_namelen = sizeof(s_in);
	msg.msg_iov = &iv;
	msg.msg_iovlen = 1;
	msg.msg_control = &ctl;
	msg.msg_controllen = sizeof(ctl);
	
	rd = recvmsg(s, &msg, 0);
	if (rd == -1) {
		perror("recvmsg()");
		exit(1);
	}

	if (rd != 5)
		return 0;

	if ( ctl.hdr.cmsg_level != IPPROTO_IP ||
#ifdef LINUX			
	    ctl.hdr.cmsg_type != IP_TTL
#else
	    ctl.hdr.cmsg_type != IP_RECVTTL
#endif
	    ) {

	    printf("Didn't get ttl! len=%d level=%d type=%d\n",
	    	   ctl.hdr.cmsg_len, ctl.hdr.cmsg_level, ctl.hdr.cmsg_type);
	    exit(1);
	}

	if (memcmp(buf, "sorbo", 5) != 0)
		return 0;

	strcpy(ip, inet_ntoa(s_in.sin_addr));
	*ttl = ctl.ttl;
	*port = ntohs(s_in.sin_port);
	return 1;
}

#if 0
int check_signal(const unsigned char* buf, int rd, 
		 char* ip, char* ttl, unsigned short *port) {
	int got_it;
	struct ip* iph;
	struct udphdr* uh;

	if (rd != MAGIC_LEN)
		return 0;

	iph = (struct ip*) buf;
	uh = (struct udphdr*) ((char*)iph + 20);

	if ( htons(uh->uh_dport) != DPORT)
		return 0;

	got_it = memcmp(&buf[rd-5], "sorbo", 5) == 0;

	strcpy(ip, inet_ntoa(iph->ip_src));
	*ttl = iph->ip_ttl;

	*port = ntohs(uh->uh_sport);
	return got_it;
}
#endif

unsigned int udp_checksum(unsigned char *stuff0, int len, struct in_addr *sip,
                          struct in_addr *dip) {
        unsigned char *stuff;
        struct ippseudo *ph;

        stuff = (unsigned char*) malloc(len + sizeof(struct ippseudo));
        if(!stuff) {
                perror("malloc()");
                exit(1);
        }

        ph = (struct ippseudo*) stuff;

        memcpy(&ph->ippseudo_src, sip, 4);
        memcpy(&ph->ippseudo_dst, dip, 4);
        ph->ippseudo_pad =  0;
        ph->ippseudo_p = IPPROTO_UDP;
        ph->ippseudo_len = htons(len);

        memcpy(stuff + sizeof(struct ippseudo), stuff0, len);

        return in_cksum((unsigned short*)stuff, len+sizeof(struct ippseudo));
}

void send_stuff(int s, char* sip, char* ip, unsigned short port, int dlen) {
	static unsigned char buf[PRGA_LEN+128] = "\x69";
	static int plen = 0;
	static struct sockaddr_in dst;
	int rd;
	struct in_addr tmp_dst;
	int stuff, delay;
	int i;

	stuff = poll_rate*pps;
	delay = (int) ((double)1.0/pps*1000.0*1000.0);

	inet_aton(ip, &tmp_dst);
	if (tmp_dst.s_addr != dst.sin_addr.s_addr ||
	    dlen != (plen - 20 - 8)) {
	    
	    buf[0] = '\x69';
	}	    

	// create packet
	if (buf[0] == '\x69') {
		struct ip* iph;
		struct udphdr* uh;
		char* ptr;
		
//		printf("Initializing packet...\n");
		memset(buf, 0, sizeof(buf));
		iph = (struct ip*) buf;
		iph->ip_hl = 5;
		iph->ip_v = 4;
		iph->ip_tos = 0;
		iph->ip_len = htons(20+8+dlen);
		iph->ip_id = htons(666);
		iph->ip_off = 0;
		iph->ip_ttl = TTLSENT;
		iph->ip_p = IPPROTO_UDP;
		iph->ip_sum = 0;

		inet_aton(sip, &iph->ip_src);
		inet_aton(ip, &iph->ip_dst);

		memset(&dst, 0, sizeof(dst));
		dst.sin_family = PF_INET;
		dst.sin_port = htons(port);
		memcpy(&dst.sin_addr, &iph->ip_dst, sizeof(dst.sin_addr));

		iph->ip_sum = in_cksum((unsigned short*)iph, 20);

		uh = (struct udphdr*) ((char*)iph + 20);
		uh->uh_sport = htons(DPORT);
		uh->uh_dport = htons(port);
		uh->uh_ulen = htons(8+dlen);
		uh->uh_sum = 0;

		ptr = (char*) uh + 8;

		memset(ptr, 0, dlen);

		uh->uh_sum = udp_checksum((unsigned char*)uh, 8+dlen,
					  &iph->ip_src, &iph->ip_dst);

#ifdef BSD
		iph->ip_len = ntohs(iph->ip_len);
#endif
		plen = 20+8+dlen;
	}
#if 0
	printf("Packet %d %s %d\n", plen, inet_ntoa(dst.sin_addr),
	ntohs(dst.sin_port));
	hexdump (buf, plen);
#endif

//	printf("sending stuff to %s\n", ip);
	for (i = 0; i < stuff; i++) {
		rd = sendto(s, buf, plen, 0, (struct sockaddr*)&dst, sizeof(dst));
		if (rd == -1) {
			perror("sendto()");
			exit(1);
		}
		if (rd != plen) {
			printf("wrote %d out of %d\n", rd, plen);
			exit(1);
		}

		// sending ttl..
		if (dlen != PRGA_LEN)
			break;
		usleep(delay);
	}	
}

int main(int argc, char *argv[]) {
	int s, us;
	int rd = 1;

#if 0	
	const u_char* buf;
	char errbuf[PCAP_ERRBUF_SIZE];
	struct pcap_pkthdr phdr;
	pcap_t* p;
	int dtl;
#endif	

	int got_it = 0;
	char ip[16] = "\x00";
	unsigned char ttl = 0;
	unsigned short port;
	struct sockaddr_in s_in;
	struct timeval tv;
	fd_set rfds;
	unsigned char* sip = 0;

	if (argc < 2) {
		printf("Usage: %s <sip> [pps]\n", argv[0]);
		exit(1);
	}

	if (argc > 2) {
		pps = atoi(argv[2]);
	}

	printf("PPS=%d\n", pps);

	sip = argv[1];

	memset(&s_in, 0, sizeof(s_in));
	us = socket (PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == -1) {
		perror("socket()");
		exit(1);
	}
	s_in.sin_family = PF_INET;
	s_in.sin_addr.s_addr = INADDR_ANY;
	s_in.sin_port = htons(DPORT);
	if (bind (us, (struct sockaddr*)&s_in, sizeof(s_in)) == -1) {
		perror("bind()");
		exit(1);
	}

	rd = 1;
	if (setsockopt(us, IPPROTO_IP, IP_RECVTTL, &rd, sizeof(rd)) == -1) {
		perror("setsockopt()");
		exit(1);
	}

	s = socket (PF_INET, SOCK_RAW, IPPROTO_UDP);
	if (s == -1) {
		perror("socket()");
		exit(1);
	}
	
	rd = 1;
	if (setsockopt(s, IPPROTO_IP, IP_HDRINCL, &rd, sizeof(rd)) == -1) {
		perror("setsockopt()");
		exit(1);
	}


#if 0
        p = pcap_open_live(argv[1], 512, 0, 25, errbuf);
	if (!p) {
		printf("pcap_open_live(): %s\n", errbuf);
		exit(1);
	}
	
	dtl = pcap_datalink(p);

	switch (dtl) {
		case DLT_NULL:
			dtl = 4;
			break;

		case DLT_EN10MB:
			dtl = 14;
			break;

		default:
			printf("Unknown datalink %d\n", dtl);
			exit(1);
	}

	printf("Datalink size=%d\n", dtl);
#endif
	while (1) {
#if 0	
		buf = pcap_next(p, &phdr);
		if (buf) {
			if (check_signal(buf+dtl, phdr.caplen-dtl, 
					 ip, &ttl, &port)) {
				got_it = 2;
				printf("Got signal from %s:%d TTL=%d\n", 
				       ip, port, ttl);
			}	
		}
#endif
		FD_ZERO(&rfds);
		FD_SET(us, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 10*1000;
		rd = select(us+1, &rfds, NULL, NULL, &tv);
		if (rd == -1) {
			perror("select()");
			exit(1);
		}
		if (rd == 1 && FD_ISSET(us, &rfds)) {
			char ipnew[16];
			unsigned char ttlnew;
			if (check_signal(us, ipnew, &ttlnew, &port)) {
				int send_ttl = 0;
				if (ttlnew != ttl || strcmp(ipnew, ip) != 0 ||
				    got_it == 0) {
				    	send_ttl = 1;
				}	
				ttl = ttlnew;
				strcpy(ip, ipnew);
				
				printf("Got signal from %s:%d TTL=%d\n", 
				       ip, port, ttl);
				got_it = 2;
				
				if (send_ttl) {
					printf("Sending ttl (%d)...\n", ttl);
					send_stuff(s, sip, ip, port, 69 + (TTLSENT-ttl));
				}	
			}	
		}

		if (got_it) {
			printf("Sending stuff to %s...\n", ip);
			send_stuff(s, sip, ip, port, PRGA_LEN);
			got_it--;

			if (got_it == 0) {
				printf("Stopping send\n");
			}
		}
	}

#if 0
	pcap_close(p);
#endif

	close(s);
	close(us);
	exit(0);
}
