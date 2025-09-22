/*	$OpenBSD: print-tcp.c,v 1.39 2020/01/24 22:46:37 procter Exp $	*/

/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <sys/time.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <net/if.h>
#include <net/pfvar.h>

#include <rpc/rpc.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "interface.h"
#include "addrtoname.h"
#include "extract.h"

#include "nfs.h"

static void print_tcp_rst_data(const u_char *sp, u_int length);

#define MAX_RST_DATA_LEN	30

/* Compatibility */
#ifndef TCPOPT_WSCALE
#define	TCPOPT_WSCALE		3	/* window scale factor (rfc1072) */
#endif
#ifndef TCPOPT_SACKOK
#define	TCPOPT_SACKOK		4	/* selective ack ok (rfc2018) */
#endif
#ifndef TCPOPT_SACK
#define	TCPOPT_SACK		5	/* selective ack (rfc2018) */
#endif
#ifndef TCPOLEN_SACK
#define TCPOLEN_SACK		8	/* length of a SACK block */
#endif
#ifndef TCPOPT_ECHO
#define	TCPOPT_ECHO		6	/* echo (rfc1072) */
#endif
#ifndef TCPOPT_ECHOREPLY
#define	TCPOPT_ECHOREPLY	7	/* echo (rfc1072) */
#endif
#ifndef TCPOPT_TIMESTAMP
#define TCPOPT_TIMESTAMP	8	/* timestamps (rfc1323) */
#endif
#ifndef TCPOPT_CC
#define TCPOPT_CC		11	/* T/TCP CC options (rfc1644) */
#endif
#ifndef TCPOPT_CCNEW
#define TCPOPT_CCNEW		12	/* T/TCP CC options (rfc1644) */
#endif
#ifndef TCPOPT_CCECHO
#define TCPOPT_CCECHO		13	/* T/TCP CC options (rfc1644) */
#endif

/* Definitions required for ECN
   for use if the OS running tcpdump does not have ECN */
#ifndef TH_ECNECHO
#define TH_ECNECHO		0x40	/* ECN Echo in tcp header */
#endif
#ifndef TH_CWR
#define TH_CWR			0x80	/* ECN Cwnd Reduced in tcp header*/
#endif

struct tha {
	struct in6_addr src;
	struct in6_addr dst;
	u_int port;
};

struct tcp_seq_hash {
	struct tcp_seq_hash *nxt;
	struct tha addr;
	tcp_seq seq;
	tcp_seq ack;
};

#define TSEQ_HASHSIZE 919

/* These tcp optinos do not have the size octet */
#define ZEROLENOPT(o) ((o) == TCPOPT_EOL || (o) == TCPOPT_NOP)

static struct tcp_seq_hash tcp_seq_hash[TSEQ_HASHSIZE];

#ifndef BGP_PORT
#define BGP_PORT        179
#endif
#define NETBIOS_SSN_PORT 139

/* OpenFlow TCP ports. */
#define OLD_OFP_PORT	6633
#define OFP_PORT	6653

static int tcp_cksum(const struct ip *ip, const struct tcphdr *tp, int len)
{
	union phu {
		struct phdr {
			u_int32_t src;
			u_int32_t dst;
			u_char mbz;
			u_char proto;
			u_int16_t len;
		} ph;
		u_int16_t pa[6];
	} phu;
	const u_int16_t *sp;
	u_int32_t sum;

	/* pseudo-header.. */
	phu.ph.len = htons((u_int16_t)len);
	phu.ph.mbz = 0;
	phu.ph.proto = IPPROTO_TCP;
	memcpy(&phu.ph.src, &ip->ip_src.s_addr, sizeof(u_int32_t));
	memcpy(&phu.ph.dst, &ip->ip_dst.s_addr, sizeof(u_int32_t));

	sp = &phu.pa[0];
	sum = sp[0]+sp[1]+sp[2]+sp[3]+sp[4]+sp[5];

	return in_cksum((u_short *)tp, len, sum);
}

static int tcp6_cksum(const struct ip6_hdr *ip6, const struct tcphdr *tp,
		      u_int len)
{
	union {
		struct {
			struct in6_addr ph_src;
			struct in6_addr ph_dst;
			u_int32_t       ph_len;
			u_int8_t        ph_zero[3];
			u_int8_t        ph_nxt;
		} ph;
		u_int16_t pa[20];
	} phu;
	size_t i;
	u_int32_t sum = 0;

	/* pseudo-header */
	memset(&phu, 0, sizeof(phu));
	phu.ph.ph_src = ip6->ip6_src;
	phu.ph.ph_dst = ip6->ip6_dst;
	phu.ph.ph_len = htonl(len);
	phu.ph.ph_nxt = IPPROTO_TCP;

	for (i = 0; i < sizeof(phu.pa) / sizeof(phu.pa[0]); i++)
		sum += phu.pa[i];

	return in_cksum((u_short *)tp, len, sum);
}

void
tcp_print(const u_char *bp, u_int length, const u_char *bp2)
{
	const struct tcphdr *tp;
	const struct ip *ip;
	u_char flags;
	int hlen;
	char ch;
	struct tcp_seq_hash *th = NULL;
	int rev = 0;
	u_int16_t sport, dport, win, urp;
	tcp_seq seq, ack;
	const struct ip6_hdr *ip6;

	tp = (struct tcphdr *)bp;
	switch (((struct ip *)bp2)->ip_v) {
	case 4:
		ip = (struct ip *)bp2;
		ip6 = NULL;
		break;
	case 6:
		ip = NULL;
		ip6 = (struct ip6_hdr *)bp2;
		break;
	default:
		printf("invalid ip version");
		return;
	}

	ch = '\0';
	if (length < sizeof(*tp)) {
		printf("truncated-tcp %u", length);
		return;
	}

	if (!TTEST(tp->th_dport)) {
		if (ip6) {
			printf("%s > %s: [|tcp]",
			    ip6addr_string(&ip6->ip6_src),
			    ip6addr_string(&ip6->ip6_dst));
		} else {
			printf("%s > %s: [|tcp]",
			    ipaddr_string(&ip->ip_src),
			    ipaddr_string(&ip->ip_dst));
		}
		return;
	}

	sport = ntohs(tp->th_sport);
	dport = ntohs(tp->th_dport);

	if (ip6) {
		if (ip6->ip6_nxt == IPPROTO_TCP) {
			printf("%s.%s > %s.%s: ",
			    ip6addr_string(&ip6->ip6_src),
			    tcpport_string(sport),
			    ip6addr_string(&ip6->ip6_dst),
			    tcpport_string(dport));
		} else {
			printf("%s > %s: ",
			    tcpport_string(sport), tcpport_string(dport));
		}
	} else {
		if (ip->ip_p == IPPROTO_TCP) {
			printf("%s.%s > %s.%s: ",
			    ipaddr_string(&ip->ip_src),
			    tcpport_string(sport),
			    ipaddr_string(&ip->ip_dst),
			    tcpport_string(dport));
		} else {
			printf("%s > %s: ",
			    tcpport_string(sport), tcpport_string(dport));
		}
	}

	if (!qflag && TTEST(tp->th_seq) && !TTEST(tp->th_ack))
		printf("%u ", ntohl(tp->th_seq));

	TCHECK(*tp);
	seq = ntohl(tp->th_seq);
	ack = ntohl(tp->th_ack);
	win = ntohs(tp->th_win);
	urp = ntohs(tp->th_urp);
	hlen = tp->th_off * 4;

	if (qflag) {
		printf("tcp %d", length - tp->th_off * 4);
		return;
	} else if (packettype != PT_TCP) {

		/*
		 * If data present and NFS port used, assume NFS.
		 * Pass offset of data plus 4 bytes for RPC TCP msg length
		 * to NFS print routines.
		 */
		u_int len = length - hlen;
		if ((u_char *)tp + 4 + sizeof(struct rpc_msg) <= snapend &&
		    dport == NFS_PORT) {
			nfsreq_print((u_char *)tp + hlen + 4, len, bp2);
			return;
		} else if ((u_char *)tp + 4 + 
		    sizeof(struct rpc_msg) <= snapend && sport == NFS_PORT) {
			nfsreply_print((u_char *)tp + hlen + 4, len, bp2);
			return;
		}
	}
	if ((flags = tp->th_flags) & (TH_SYN|TH_FIN|TH_RST|TH_PUSH|
				      TH_ECNECHO|TH_CWR)) {
		if (flags & TH_SYN)
			putchar('S');
		if (flags & TH_FIN)
			putchar('F');
		if (flags & TH_RST)
			putchar('R');
		if (flags & TH_PUSH)
			putchar('P');
		if (flags & TH_CWR)
			putchar('W');	/* congestion _W_indow reduced (ECN) */
		if (flags & TH_ECNECHO)
			putchar('E');	/* ecn _E_cho sent (ECN) */
	} else
		putchar('.');

	if (!Sflag && (flags & TH_ACK)) {
		struct tha tha;
		/*
		 * Find (or record) the initial sequence numbers for
		 * this conversation.  (we pick an arbitrary
		 * collating order so there's only one entry for
		 * both directions).
		 */
		bzero(&tha, sizeof(tha));
		rev = 0;
		if (ip6) {
			if (sport > dport) {
				rev = 1;
			} else if (sport == dport) {
			    int i;

			    for (i = 0; i < 4; i++) {
				if (((u_int32_t *)(&ip6->ip6_src))[i] >
				    ((u_int32_t *)(&ip6->ip6_dst))[i]) {
					rev = 1;
					break;
				}
			    }
			}
			if (rev) {
				tha.src = ip6->ip6_dst;
				tha.dst = ip6->ip6_src;
				tha.port = dport << 16 | sport;
			} else {
				tha.dst = ip6->ip6_dst;
				tha.src = ip6->ip6_src;
				tha.port = sport << 16 | dport;
			}
		} else {
			if (sport > dport ||
			    (sport == dport &&
			     ip->ip_src.s_addr > ip->ip_dst.s_addr)) {
				rev = 1;
			}
			if (rev) {
				*(struct in_addr *)&tha.src = ip->ip_dst;
				*(struct in_addr *)&tha.dst = ip->ip_src;
				tha.port = dport << 16 | sport;
			} else {
				*(struct in_addr *)&tha.dst = ip->ip_dst;
				*(struct in_addr *)&tha.src = ip->ip_src;
				tha.port = sport << 16 | dport;
			}
		}

		for (th = &tcp_seq_hash[tha.port % TSEQ_HASHSIZE];
		     th->nxt; th = th->nxt)
			if (!memcmp((char *)&tha, (char *)&th->addr,
				  sizeof(th->addr)))
				break;

		if (!th->nxt || flags & TH_SYN) {
			/* didn't find it or new conversation */
			if (th->nxt == NULL) {
				th->nxt = calloc(1, sizeof(*th));
				if (th->nxt == NULL)
					error("tcp_print: calloc");
			}
			th->addr = tha;
			if (rev)
				th->ack = seq, th->seq = ack - 1;
			else
				th->seq = seq, th->ack = ack - 1;
		} else {
			if (rev)
				seq -= th->ack, ack -= th->seq;
			else
				seq -= th->seq, ack -= th->ack;
		}
	}
	hlen = tp->th_off * 4;
	if (hlen > length) {
		printf(" [bad hdr length]");
		return;
	}

	if (ip && ip->ip_v == 4 && vflag) {
		if (TTEST2(tp->th_sport, length)) {
			u_int16_t sum, tcp_sum;
			sum = tcp_cksum(ip, tp, length);
			if (sum != 0) {
				tcp_sum = EXTRACT_16BITS(&tp->th_sum);
				printf(" [bad tcp cksum %x! -> %x]", tcp_sum,
				    in_cksum_shouldbe(tcp_sum, sum));
			} else
				printf(" [tcp sum ok]");
		}
	}
	if (ip6 && ip6->ip6_plen && vflag) {
		if (TTEST2(tp->th_sport, length)) {
			u_int16_t sum, tcp_sum;
			sum = tcp6_cksum(ip6, tp, length);
			if (sum != 0) {
				tcp_sum = EXTRACT_16BITS(&tp->th_sum);
				printf(" [bad tcp cksum %x! -> %x]", tcp_sum,
				    in_cksum_shouldbe(tcp_sum, sum));
			} else
				printf(" [tcp sum ok]");
		}
	}

	/* OS Fingerprint */
	if (oflag && (flags & (TH_SYN|TH_ACK)) == TH_SYN) {
		struct pf_osfp_enlist *head = NULL;
		struct pf_osfp_entry *fp;
		unsigned long left;
		left = (unsigned long)(snapend - (const u_char *)tp);

		if (left >= hlen)
			head = pf_osfp_fingerprint_hdr(ip, ip6, tp);
		if (head) {
			int prev = 0;
			printf(" (src OS:");
			SLIST_FOREACH(fp, head, fp_entry) {
				if (fp->fp_enflags & PF_OSFP_EXPANDED)
					continue;
				if (prev)
					printf(",");
				printf(" %s", fp->fp_class_nm);
				if (fp->fp_version_nm[0])
					printf(" %s", fp->fp_version_nm);
				if (fp->fp_subtype_nm[0])
					printf(" %s", fp->fp_subtype_nm);
				prev = 1;
			}
			printf(")");
		} else {
			if (left < hlen)
				printf(" (src OS: short-pkt)");
			else
				printf(" (src OS: unknown)");
		}
	}

	length -= hlen;
	if (vflag > 1 || length > 0 || flags & (TH_SYN | TH_FIN | TH_RST))
		printf(" %u:%u(%u)", seq, seq + length, length);
	if (flags & TH_ACK)
		printf(" ack %u", ack);

	printf(" win %u", win);

	if (flags & TH_URG)
		printf(" urg %u", urp);
	/*
	 * Handle any options.
	 */
	if ((hlen -= sizeof(*tp)) > 0) {
		const u_char *cp;
		int i, opt, len, datalen;

		cp = (const u_char *)tp + sizeof(*tp);
		putchar(' ');
		ch = '<';
		while (hlen > 0) {
			putchar(ch);
			TCHECK(*cp);
			opt = *cp++;
			if (ZEROLENOPT(opt))
				len = 1;
			else {
				TCHECK(*cp);
				len = *cp++;	/* total including type, len */
				if (len < 2 || len > hlen)
					goto bad;
				--hlen;		/* account for length byte */
			}
			--hlen;			/* account for type byte */
			datalen = 0;

/* Bail if "l" bytes of data are not left or were not captured  */
#define LENCHECK(l) { if ((l) > hlen) goto bad; TCHECK2(*cp, l); }

			switch (opt) {

			case TCPOPT_MAXSEG:
				printf("mss");
				datalen = 2;
				LENCHECK(datalen);
				printf(" %u", EXTRACT_16BITS(cp));

				break;

			case TCPOPT_EOL:
				printf("eol");
				break;

			case TCPOPT_NOP:
				printf("nop");
				break;

			case TCPOPT_WSCALE:
				printf("wscale");
				datalen = 1;
				LENCHECK(datalen);
				printf(" %u", *cp);
				break;

			case TCPOPT_SACKOK:
				printf("sackOK");
				if (len != 2)
					printf("[len %d]", len);
				break;

			case TCPOPT_SACK:
			{
				u_long s, e;

				datalen = len - 2;
				if ((datalen % TCPOLEN_SACK) != 0 ||
				    !(flags & TH_ACK)) {
				         printf("malformed sack ");
					 printf("[len %d] ", datalen);
					 break;
				}
				printf("sack %d ", datalen/TCPOLEN_SACK);
				for (i = 0; i < datalen; i += TCPOLEN_SACK) {
					LENCHECK (i + TCPOLEN_SACK);
					s = EXTRACT_32BITS(cp + i);
					e = EXTRACT_32BITS(cp + i + 4);
					if (!Sflag) {
						if (rev) {
							s -= th->seq;
							e -= th->seq;
						} else {
							s -= th->ack;
							e -= th->ack;
						}
					}
					printf("{%lu:%lu} ", s, e);
				}
				break;
			}
			case TCPOPT_ECHO:
				printf("echo");
				datalen = 4;
				LENCHECK(datalen);
				printf(" %u", EXTRACT_32BITS(cp));
				break;

			case TCPOPT_ECHOREPLY:
				printf("echoreply");
				datalen = 4;
				LENCHECK(datalen);
				printf(" %u", EXTRACT_32BITS(cp));
				break;

			case TCPOPT_TIMESTAMP:
				printf("timestamp");
				datalen = 8;
				LENCHECK(4);
				printf(" %u", EXTRACT_32BITS(cp));
				LENCHECK(datalen);
				printf(" %u", EXTRACT_32BITS(cp + 4));
				break;

			case TCPOPT_CC:
				printf("cc");
				datalen = 4;
				LENCHECK(datalen);
				printf(" %u", EXTRACT_32BITS(cp));
				break;

			case TCPOPT_CCNEW:
				printf("ccnew");
				datalen = 4;
				LENCHECK(datalen);
				printf(" %u", EXTRACT_32BITS(cp));
				break;

			case TCPOPT_CCECHO:
				printf("ccecho");
				datalen = 4;
				LENCHECK(datalen);
				printf(" %u", EXTRACT_32BITS(cp));
				break;

			case TCPOPT_SIGNATURE:
				printf("tcpmd5:");
				datalen = len - 2;
				for (i = 0; i < datalen; ++i) {
					LENCHECK(i+1);
					printf("%02x", cp[i]);
				}
				break;

			default:
				printf("opt-%d:", opt);
				datalen = len - 2;
				for (i = 0; i < datalen; ++i) {
					LENCHECK(i+1);
					printf("%02x", cp[i]);
				}
				break;
			}

			/* Account for data printed */
			cp += datalen;
			hlen -= datalen;

			/* Check specification against observed length */
			++datalen;			/* option octet */
			if (!ZEROLENOPT(opt))
				++datalen;		/* size octet */
			if (datalen != len)
				printf("[len %d]", len);
			ch = ',';
			if (opt == TCPOPT_EOL)
				break;
		}
		putchar('>');
	}

	if (length <= 0)
		return;

	/*
	 * Decode payload if necessary.
	*/
	bp += (tp->th_off * 4);
	if (flags & TH_RST) {
		if (vflag)
			print_tcp_rst_data(bp, length);
	} else {
		if (sport == BGP_PORT || dport == BGP_PORT)
			bgp_print(bp, length);
		else if (sport == OLD_OFP_PORT || dport == OLD_OFP_PORT ||
		    sport == OFP_PORT || dport == OFP_PORT)
			ofp_print(bp, length);
#if 0
		else if (sport == NETBIOS_SSN_PORT || dport == NETBIOS_SSN_PORT)
			nbt_tcp_print(bp, length);
#endif
	}
	return;
bad:
	printf("[bad opt]");
	if (ch != '\0')
		putchar('>');
	return;
trunc:
	printf("[|tcp]");
	if (ch != '\0')
		putchar('>');
}


/*
 * RFC1122 says the following on data in RST segments:
 *
 *         4.2.2.12  RST Segment: RFC-793 Section 3.4
 *
 *            A TCP SHOULD allow a received RST segment to include data.
 *
 *            DISCUSSION
 *                 It has been suggested that a RST segment could contain
 *                 ASCII text that encoded and explained the cause of the
 *                 RST.  No standard has yet been established for such
 *                 data.
 *
 */

static void
print_tcp_rst_data(const u_char *sp, u_int length)
{
	int c;

	if (TTEST2(*sp, length))
		printf(" [RST");
	else
		printf(" [!RST");
	if (length > MAX_RST_DATA_LEN) {
		length = MAX_RST_DATA_LEN;	/* can use -X for longer */
		putchar('+');			/* indicate we truncate */
	}
	putchar(' ');
	while (length-- && sp < snapend) {
		c = *sp++;
		safeputchar(c);
	}
	putchar(']');
}
