/*-
 * Copyright (c) 1998 Andrzej Bialecki
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


/*
 * Small replacement for netstat. Uses only sysctl(3) to get the info.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <net/if.h>
#include <net/route.h>
#include <net/if_dl.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp_var.h>
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <osreldate.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char *progname;
int iflag = 0;
int lflag = 0;			/* print cpu load info */
int rflag = 0;
int sflag = 0;
int pflag = 0;
int wflag = 0;			/* repeat every wait seconds */
int delta = 0 ;

extern char *optarg;
extern int optind;

void print_load_stats(void);

void
usage()
{
	fprintf(stderr, "\n%s [-nrsil] [-p proto] [-w wait]\n", progname);
	fprintf(stderr, "  proto: {ip|tcp|udp|icmp}\n\n");
}


/*
 * The following parts related to retrieving the routing table and
 * interface information, were borrowed from R. Stevens' code examples
 * accompanying his excellent book. Thanks!
 */
char *
sock_ntop(const struct sockaddr *sa, size_t salen)
{
	char	portstr[7];
	static	char str[128];	/* Unix domain is largest */

	switch (sa->sa_family) {
	case 255: {
		int	i = 0;
		u_long	mask;
		u_int	index = 1 << 31;
		u_short	new_mask = 0;

		mask = ntohl(((struct sockaddr_in *)sa)->sin_addr.s_addr);

		while (mask & index) {
			new_mask++;
			index >>= 1;
		}
		sprintf(str, "/%hu", new_mask);
		return (str);
	}
	case AF_UNSPEC:
	case AF_INET: {
		struct	sockaddr_in *sin = (struct sockaddr_in *)sa;

		if (inet_ntop(AF_INET, &sin->sin_addr, str, sizeof(str))
		    == NULL)
			return (NULL);
		if (ntohs(sin->sin_port) != 0) {
			snprintf(portstr, sizeof(portstr), ".%d",
			    ntohs(sin->sin_port));
			strcat(str, portstr);
		}
		if (strcmp(str, "0.0.0.0") == 0)
			sprintf(str, "default");
		return (str);
	}

	case AF_UNIX: {
		struct	sockaddr_un *unp = (struct sockaddr_un *)sa;

		/*
		 * OK to have no pathname bound to the socket:
		 * happens on every connect() unless client calls
		 * bind() first.
		 */
		if (unp->sun_path[0] == 0)
			strcpy(str, "(no pathname bound)");
		else
			snprintf(str, sizeof(str), "%s", unp->sun_path);
		return (str);
	}

	case AF_LINK: {
		struct	sockaddr_dl *sdl = (struct sockaddr_dl *)sa;

		if (sdl->sdl_nlen > 0) {
			bcopy(&sdl->sdl_data[0], str, sdl->sdl_nlen);
			str[sdl->sdl_nlen] = '\0';
		} else
			snprintf(str, sizeof(str), "link#%d", sdl->sdl_index);
		return (str);
	}

	default:
		snprintf(str, sizeof(str),
		    "sock_ntop: unknown AF_xxx: %d, len %d", sa->sa_family,
		    salen);
		return (str);
	}
	return (NULL);
}

char *
Sock_ntop(const struct sockaddr *sa, size_t salen)
{
	char	*ptr;

	if ((ptr = sock_ntop(sa, salen)) == NULL)
		err(1, "sock_ntop error");	/* inet_ntop() sets errno */
	return (ptr);
}


#define ROUNDUP(a,size) (((a) & ((size)-1))?(1+((a)|((size)-1))):(a))

#define NEXT_SA(ap) 							\
	ap=(struct sockaddr *)						\
	    ((caddr_t)ap+(ap->sa_len?ROUNDUP(ap->sa_len,sizeof(u_long)):\
	    sizeof(u_long)))

void
get_rtaddrs(int addrs, struct sockaddr *sa, struct sockaddr **rti_info)
{
	int	i;

	for (i = 0; i < RTAX_MAX; i++) {
		if (addrs & (1 << i)) {
			rti_info[i] = sa;
			NEXT_SA(sa);
		} else
			rti_info[i] = NULL;
	}
}

void
get_flags(char *buf, int flags)
{
	if (flags & 0x1)
		strcat(buf, "U");
	if (flags & 0x2)
		strcat(buf, "G");
	if (flags & 0x4)
		strcat(buf, "H");
	if (flags & 0x8)
		strcat(buf, "r");
	if (flags & 0x10)
		strcat(buf, "d");
#ifdef NEVER
	if (flags & 0x20)
		strcat(buf, "mod,");
#endif /*NEVER*/
	if (flags & 0x100)
		strcat(buf, "C");
	if (flags & 0x400)
		strcat(buf, "L");
	if (flags & 0x800)
		strcat(buf, "S");
	if (flags & 0x10000)
		strcat(buf, "c");
	if (flags & 0x20000)
		strcat(buf, "W");
#ifdef NEVER
	if (flags & 0x200000)
		strcat(buf, ",LOC");
#endif /*NEVER*/
	if (flags & 0x400000)
		strcat(buf, "b");
#ifdef NEVER
	if (flags & 0x800000)
		strcat(buf, ",MCA");
#endif /*NEVER*/
}

void
print_routing(char *proto)
{
	int	mib[6];
	int	i = 0;
	int	rt_len;
	int	if_len;
	int	if_num;
	char	*rt_buf;
	char	*if_buf;
	char	*next;
	char	*lim;
	struct	rt_msghdr *rtm;
	struct	if_msghdr *ifm;
	struct	if_msghdr **ifm_table;
	struct	ifa_msghdr *ifam;
	struct	sockaddr *sa;
	struct	sockaddr *sa1;
	struct	sockaddr *rti_info[RTAX_MAX];
	struct	sockaddr **if_table;
	struct	rt_metrics rm;
	char	fbuf[50];

	/* keep a copy of statistics here for future use */
	static unsigned *base_stats = NULL ;
	static unsigned base_len = 0 ;

	/* Get the routing table */
	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = 0;
	mib[4] = NET_RT_DUMP;
	mib[5] = 0;

	/*Estimate the size of table */
	if (sysctl(mib, 6, NULL, &rt_len, NULL, 0) == -1) {
		perror("sysctl size");
		exit(-1);
	}
	if ((rt_buf = (char *)malloc(rt_len)) == NULL) {
		perror("malloc");
		exit(-1);
	}

	/* Now get it. */
	if (sysctl(mib, 6, rt_buf, &rt_len, NULL, 0) == -1) {
		perror("sysctl get");
		exit(-1);
	}

	/* Get the interfaces table */
	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = 0;
	mib[4] = NET_RT_IFLIST;
	mib[5] = 0;

	/* Estimate the size of table */
	if (sysctl(mib, 6, NULL, &if_len, NULL, 0) == -1) {
		perror("sysctl size");
		exit(-1);
	}
	if ((if_buf = (char *)malloc(if_len)) == NULL) {
		perror("malloc");
		exit(-1);
	}

	/* Now get it. */
	if (sysctl(mib, 6, if_buf, &if_len, NULL, 0) == -1) {
		perror("sysctl get");
		exit(-1);
	}
	lim = if_buf + if_len;
	i = 0;
	for (next = if_buf, i = 0; next < lim; next += ifm->ifm_msglen) {
		ifm = (struct if_msghdr *)next;
		i++;
	}
	if_num = i;
	if_table = (struct sockaddr **)calloc(i, sizeof(struct sockaddr));
	ifm_table = (struct if_msghdr **)calloc(i, sizeof(struct if_msghdr));
	if (iflag) {
		printf("\nInterface table:\n");
		printf("----------------\n");
		printf("Name  Mtu   Network       Address            "
		    "Ipkts Ierrs    Opkts Oerrs  Coll\n");
	}
        /* scan the list and store base values */
	i = 0 ;
	for (next = if_buf; next < lim; next += ifm->ifm_msglen) {
		ifm = (struct if_msghdr *)next;
		i++ ;
	}
	if (base_stats == NULL || i != base_len) {
		base_stats = calloc(i*5, sizeof(unsigned));
		base_len = i ;
	}
	i = 0;
	for (next = if_buf; next < lim; next += ifm->ifm_msglen) {
		ifm = (struct if_msghdr *)next;
		if_table[i] = (struct sockaddr *)(ifm + 1);
		ifm_table[i] = ifm;

		sa = if_table[i];
		if (iflag && sa->sa_family == AF_LINK) {
			struct	sockaddr_dl *sdl = (struct sockaddr_dl *)sa;
			unsigned *bp = &base_stats[i*5];

			printf("%-4s  %-5d <Link>   ",
			    sock_ntop(if_table[i], if_table[i]->sa_len),
			    ifm->ifm_data.ifi_mtu);
			if (sdl->sdl_alen == 6) {
				unsigned char *p =
				    sdl->sdl_data + sdl->sdl_nlen;
				printf("%02x:%02x:%02x:%02x:%02x:%02x   ",
				    p[0], p[1], p[2], p[3], p[4], p[5]);
			} else
				printf("                    ");
			printf("%9d%6d%9d%6d%6d\n",
			    ifm->ifm_data.ifi_ipackets - bp[0],
			    ifm->ifm_data.ifi_ierrors - bp[1],
			    ifm->ifm_data.ifi_opackets - bp[2],
			    ifm->ifm_data.ifi_oerrors - bp[3],
			    ifm->ifm_data.ifi_collisions -bp[4]);
			if (delta > 0) {
			    bp[0] = ifm->ifm_data.ifi_ipackets ;
			    bp[1] = ifm->ifm_data.ifi_ierrors ;
			    bp[2] = ifm->ifm_data.ifi_opackets ;
			    bp[3] = ifm->ifm_data.ifi_oerrors ;
			    bp[4] = ifm->ifm_data.ifi_collisions ;
			}
		}
		i++;
	}
	if (!rflag) {
		free(rt_buf);
		free(if_buf);
		free(if_table);
		free(ifm_table);
		return;
	}

	/* Now dump the routing table */
	printf("\nRouting table:\n");
	printf("--------------\n");
	printf
	    ("Destination        Gateway            Flags       Netif  Use\n");
	lim = rt_buf + rt_len;
	for (next = rt_buf; next < lim; next += rtm->rtm_msglen) {
		rtm = (struct rt_msghdr *)next;
		sa = (struct sockaddr *)(rtm + 1);
		get_rtaddrs(rtm->rtm_addrs, sa, rti_info);
		if ((sa = rti_info[RTAX_DST]) != NULL) {
			sprintf(fbuf, "%s", sock_ntop(sa, sa->sa_len));
			if (((sa1 = rti_info[RTAX_NETMASK]) != NULL)
			    && sa1->sa_family == 255) {
				strcat(fbuf, sock_ntop(sa1, sa1->sa_len));
			}
			printf("%-19s", fbuf);
		}
		if ((sa = rti_info[RTAX_GATEWAY]) != NULL) {
			printf("%-19s", sock_ntop(sa, sa->sa_len));
		}
		memset(fbuf, 0, sizeof(fbuf));
		get_flags(fbuf, rtm->rtm_flags);
		printf("%-10s", fbuf);
		for (i = 0; i < if_num; i++) {
			ifm = ifm_table[i];
			if ((ifm->ifm_index == rtm->rtm_index) &&
			    (ifm->ifm_data.ifi_type > 0)) {
				sa = if_table[i];
				break;
			}
		}
		if (ifm->ifm_type == RTM_IFINFO) {
			get_rtaddrs(ifm->ifm_addrs, sa, rti_info);
			printf("  %s", Sock_ntop(sa, sa->sa_len));
		} else if (ifm->ifm_type == RTM_NEWADDR) {
			ifam =
			    (struct ifa_msghdr *)ifm_table[rtm->rtm_index - 1];
			sa = (struct sockaddr *)(ifam + 1);
			get_rtaddrs(ifam->ifam_addrs, sa, rti_info);
			printf("  %s", Sock_ntop(sa, sa->sa_len));
		}
		/* printf("    %u", rtm->rtm_use); */
		printf("\n");
	}
	free(rt_buf);
	free(if_buf);
	free(if_table);
	free(ifm_table);
}

void
print_ip_stats(void)
{
	int	mib[4];
	int	len;
	struct	ipstat s;

	mib[0] = CTL_NET;
	mib[1] = PF_INET;
	mib[2] = IPPROTO_IP;
#ifndef IPCTL_STATS
	printf("sorry, ip stats not available\n");
	return -1;
#else
	mib[3] = IPCTL_STATS;
	len = sizeof(struct ipstat);
	if (sysctl(mib, 4, &s, &len, NULL, 0) < 0) {
		perror("sysctl");
		return;
	}
	printf("\nIP statistics:\n");
	printf("--------------\n");
	printf("  %10lu total packets received\n", s.ips_total);
	printf("* Packets ok:\n");
	printf("  %10lu fragments received\n", s.ips_fragments);
	printf("  %10lu forwarded\n", s.ips_forward);
#if __FreeBSD_version > 300001
	printf("  %10lu fast forwarded\n", s.ips_fastforward);
#endif
	printf("  %10lu forwarded on same net (redirect)\n",
	    s.ips_redirectsent);
	printf("  %10lu delivered to upper level\n", s.ips_delivered);
	printf("  %10lu total ip packets generated here\n", s.ips_localout);
	printf("  %10lu total packets reassembled ok\n", s.ips_reassembled);
	printf("  %10lu total datagrams successfully fragmented\n",
	    s.ips_fragmented);
	printf("  %10lu output fragments created\n", s.ips_ofragments);
	printf("  %10lu total raw IP packets generated\n", s.ips_rawout);
	printf("\n* Bad packets:\n");
	printf("  %10lu bad checksum\n", s.ips_badsum);
	printf("  %10lu too short\n", s.ips_tooshort);
	printf("  %10lu not enough data (too small)\n", s.ips_toosmall);
	printf("  %10lu more data than declared in header\n", s.ips_badhlen);
	printf("  %10lu less data than declared in header\n", s.ips_badlen);
	printf("  %10lu fragments dropped (dups, no mbuf)\n",
	    s.ips_fragdropped);
	printf("  %10lu fragments timed out in reassembly\n",
	    s.ips_fragtimeout);
	printf("  %10lu received for unreachable dest.\n", s.ips_cantforward);
	printf("  %10lu unknown or unsupported protocol\n", s.ips_noproto);
	printf("  %10lu lost due to no bufs etc.\n", s.ips_odropped);
	printf("  %10lu couldn't fragment (DF set, etc.)\n", s.ips_cantfrag);
	printf("  %10lu error in IP options processing\n", s.ips_badoptions);
	printf("  %10lu dropped due to no route\n", s.ips_noroute);
	printf("  %10lu bad IP version\n", s.ips_badvers);
	printf("  %10lu too long (more than max IP size)\n", s.ips_toolong);
#if __FreeBSD_version > 300001
	printf("  %10lu multicast for unregistered groups\n", s.ips_notmember);
#endif
#endif
}

void
print_tcp_stats(void)
{
	int	mib[4];
	int	len;
	struct	tcpstat s;

	mib[0] = CTL_NET;
	mib[1] = PF_INET;
	mib[2] = IPPROTO_TCP;
#ifndef TCPCTL_STATS
	printf("sorry, tcp stats not available\n");
	return;
#else
	mib[3] = TCPCTL_STATS;
	len = sizeof(struct tcpstat);
	if (sysctl(mib, 4, &s, &len, NULL, 0) < 0) {
		perror("sysctl");
		return;
	}
	printf("\nTCP statistics:\n");
	printf("---------------\n");
	printf("* Connections:\n");
	printf("  %10lu initiated\n", s.tcps_connattempt);
	printf("  %10lu accepted\n", s.tcps_accepts);
	printf("  %10lu established\n", s.tcps_connects);
	printf("  %10lu dropped\n", s.tcps_drops);
	printf("  %10lu embryonic connections dropped\n", s.tcps_conndrops);
	printf("  %10lu closed (includes dropped)\n", s.tcps_closed);
	printf("  %10lu segments where we tried to get RTT\n",
	    s.tcps_segstimed);
	printf("  %10lu times RTT successfully updated\n", s.tcps_rttupdated);
	printf("  %10lu delayed ACKs sent\n", s.tcps_delack);
	printf("  %10lu dropped in rxmt timeout\n", s.tcps_timeoutdrop);
	printf("  %10lu retrasmit timeouts\n", s.tcps_rexmttimeo);
	printf("  %10lu persist timeouts\n", s.tcps_persisttimeo);
	printf("  %10lu keepalive timeouts\n", s.tcps_keeptimeo);
	printf("  %10lu keepalive probes sent\n", s.tcps_keepprobe);
	printf("  %10lu dropped in keepalive\n", s.tcps_keepdrops);

	printf("* Packets sent:\n");
	printf("  %10lu total packets sent\n", s.tcps_sndtotal);
	printf("  %10lu data packets sent\n", s.tcps_sndpack);
	printf("  %10lu data bytes sent\n", s.tcps_sndbyte);
	printf("  %10lu data packets retransmitted\n", s.tcps_sndrexmitpack);
	printf("  %10lu data bytes retransmitted\n", s.tcps_sndrexmitbyte);
	printf("  %10lu ACK-only packets sent\n", s.tcps_sndacks);
	printf("  %10lu window probes sent\n", s.tcps_sndprobe);
	printf("  %10lu URG-only packets sent\n", s.tcps_sndurg);
	printf("  %10lu window update-only packets sent\n", s.tcps_sndwinup);
	printf("  %10lu control (SYN,FIN,RST) packets sent\n", s.tcps_sndctrl);
	printf("* Packets received:\n");
	printf("  %10lu total packets received\n", s.tcps_rcvtotal);
	printf("  %10lu packets in sequence\n", s.tcps_rcvpack);
	printf("  %10lu bytes in sequence\n", s.tcps_rcvbyte);
	printf("  %10lu packets with bad checksum\n", s.tcps_rcvbadsum);
	printf("  %10lu packets with bad offset\n", s.tcps_rcvbadoff);
	printf("  %10lu packets too short\n", s.tcps_rcvshort);
	printf("  %10lu duplicate-only packets\n", s.tcps_rcvduppack);
	printf("  %10lu duplicate-only bytes\n", s.tcps_rcvdupbyte);
	printf("  %10lu packets with some duplicate data\n",
	    s.tcps_rcvpartduppack);
	printf("  %10lu duplicate bytes in partially dup. packets\n",
	    s.tcps_rcvpartdupbyte);
	printf("  %10lu out-of-order packets\n", s.tcps_rcvoopack);
	printf("  %10lu out-of-order bytes\n", s.tcps_rcvoobyte);
	printf("  %10lu packets with data after window\n",
	    s.tcps_rcvpackafterwin);
	printf("  %10lu bytes received after window\n",
	    s.tcps_rcvbyteafterwin);
	printf("  %10lu packets received after 'close'\n",
	    s.tcps_rcvafterclose);
	printf("  %10lu window probe packets\n", s.tcps_rcvwinprobe);
	printf("  %10lu duplicate ACKs\n", s.tcps_rcvdupack);
	printf("  %10lu ACKs for unsent data\n", s.tcps_rcvacktoomuch);
	printf("  %10lu ACK packets\n", s.tcps_rcvackpack);
	printf("  %10lu bytes ACKed by received ACKs\n", s.tcps_rcvackbyte);
	printf("  %10lu window update packets\n", s.tcps_rcvwinupd);
	printf("  %10lu segments dropped due to PAWS\n", s.tcps_pawsdrop);
	printf("  %10lu times header predict ok for ACKs\n", s.tcps_predack);
	printf("  %10lu times header predict ok for data packets\n",
	    s.tcps_preddat);
	printf("  %10lu PCB cache misses\n", s.tcps_pcbcachemiss);
	printf("  %10lu times cached RTT in route updated\n",
	    s.tcps_cachedrtt);
	printf("  %10lu times cached RTTVAR updated\n", s.tcps_cachedrttvar);
	printf("  %10lu times ssthresh updated\n", s.tcps_cachedssthresh);
	printf("  %10lu times RTT initialized from route\n", s.tcps_usedrtt);
	printf("  %10lu times RTTVAR initialized from route\n",
	    s.tcps_usedrttvar);
	printf("  %10lu times ssthresh initialized from route\n",
	    s.tcps_usedssthresh);
	printf("  %10lu timeout in persist state\n", s.tcps_persistdrop);
	printf("  %10lu bogus SYN, e.g. premature ACK\n", s.tcps_badsyn);
	printf("  %10lu resends due to MTU discovery\n", s.tcps_mturesent);
	printf("  %10lu listen queue overflows\n", s.tcps_listendrop);
#endif
}

void
print_udp_stats(void)
{
	int	mib[4];
	int	len;
	struct	udpstat s;

	mib[0] = CTL_NET;
	mib[1] = PF_INET;
	mib[2] = IPPROTO_UDP;
	mib[3] = UDPCTL_STATS;
	len = sizeof(struct udpstat);
	if (sysctl(mib, 4, &s, &len, NULL, 0) < 0) {
		perror("sysctl");
		return;
	}
	printf("\nUDP statistics:\n");
	printf("---------------\n");
	printf("* Packets received:\n");
	printf("  %10lu total input packets\n", s.udps_ipackets);
	printf("  %10lu packets shorter than header (dropped)\n",
	    s.udps_hdrops);
	printf("  %10lu bad checksum\n", s.udps_badsum);
	printf("  %10lu data length larger than packet\n", s.udps_badlen);
	printf("  %10lu no socket on specified port\n", s.udps_noport);
	printf("  %10lu of above, arrived as broadcast\n", s.udps_noportbcast);
	printf("  %10lu not delivered, input socket full\n", s.udps_fullsock);
	printf("  %10lu packets missing PCB cache\n", s.udpps_pcbcachemiss);
	printf("  %10lu packets not for hashed PCBs\n", s.udpps_pcbhashmiss);
	printf("* Packets sent:\n");
	printf("  %10lu total output packets\n", s.udps_opackets);
#if __FreeBSD_version > 300001
	printf("  %10lu output packets on fast path\n", s.udps_fastout);
#endif
}

char *icmp_names[] = {
	"echo reply",
	"#1",
	"#2",
	"destination unreachable",
	"source quench",
	"routing redirect",
	"#6",
	"#7",
	"echo",
	"router advertisement",
	"router solicitation",
	"time exceeded",
	"parameter problem",
	"time stamp",
	"time stamp reply",
	"information request",
	"information request reply",
	"address mask request",
	"address mask reply",
};

print_icmp_stats()
{
	int	mib[4];
	int	len;
	int	i;
	struct	icmpstat s;

	mib[0] = CTL_NET;
	mib[1] = PF_INET;
	mib[2] = IPPROTO_ICMP;
	mib[3] = ICMPCTL_STATS;
	len = sizeof(struct icmpstat);
	if (sysctl(mib, 4, &s, &len, NULL, 0) < 0) {
		perror("sysctl");
		return (-1);
	}
	printf("\nICMP statistics:\n");
	printf("----------------\n");
	printf("* Output histogram:\n");
	for (i = 0; i < (ICMP_MAXTYPE + 1); i++) {
		if (s.icps_outhist[i] > 0)
			printf("\t%10lu %s\n",
			    s.icps_outhist[i], icmp_names[i]);
	}
	printf("* Input histogram:\n");
	for (i = 0; i < (ICMP_MAXTYPE + 1); i++) {
		if (s.icps_inhist[i] > 0)
			printf("\t%10lu %s\n",
			    s.icps_inhist[i], icmp_names[i]);
	}
	printf("* Other stats:\n");
	printf("  %10lu calls to icmp_error\n", s.icps_error);
	printf("  %10lu no error 'cuz old ip too short\n", s.icps_oldshort);
	printf("  %10lu no error 'cuz old was icmp\n", s.icps_oldicmp);

	printf("  %10lu icmp code out of range\n", s.icps_badcode);
	printf("  %10lu packets shorter than min length\n", s.icps_tooshort);
	printf("  %10lu bad checksum\n", s.icps_checksum);
	printf("  %10lu calculated bound mismatch\n", s.icps_badlen);
	printf("  %10lu number of responses\n", s.icps_reflect);
	printf("  %10lu broad/multi-cast echo requests dropped\n",
	    s.icps_bmcastecho);
	printf("  %10lu broad/multi-cast timestamp requests dropped\n",
	    s.icps_bmcasttstamp);
}

int
stats(char *proto)
{
	if (!sflag)
		return 0;
	if (pflag) {
		if (proto == NULL) {
			fprintf(stderr, "Option '-p' requires parameter.\n");
			usage();
			exit(-1);
		}
		if (strcmp(proto, "ip") == 0)
			print_ip_stats();
		if (strcmp(proto, "icmp") == 0)
			print_icmp_stats();
		if (strcmp(proto, "udp") == 0)
			print_udp_stats();
		if (strcmp(proto, "tcp") == 0)
			print_tcp_stats();
		return (0);
	}
	print_ip_stats();
	print_icmp_stats();
	print_udp_stats();
	print_tcp_stats();
	return (0);
}

int
main(int argc, char *argv[])
{
	char	c;
	char	*proto = NULL;

	progname = argv[0];

	while ((c = getopt(argc, argv, "dilnrsp:w:")) != -1) {
		switch (c) {
		case 'd': /* print deltas in stats every w seconds */
			delta++ ;
			break;
		case 'w':
			wflag = atoi(optarg);
			break;
		case 'n': /* ignored, just for compatibility with std netstat */
			break;
		case 'r':
			rflag++;
			break;
		case 'i':
			iflag++;
			break;
		case 'l':
			lflag++;
			break;
		case 's':
			sflag++;
			rflag = 0;
			break;
		case 'p':
			pflag++;
			sflag++;
			proto = optarg;
			break;
		case '?':
		default:
			usage();
			exit(0);
			break;
		}
	}
	if (rflag == 0 && sflag == 0 && iflag == 0)
		rflag = 1;
	argc -= optind;

	if (argc > 0) {
		usage();
		exit(-1);
	}
	if (wflag)
		printf("\033[H\033[J");
again:
	if (wflag) {
		struct timeval t;

		gettimeofday(&t, NULL);
		printf("\033[H%s", ctime(&t.tv_sec));
	}
	print_routing(proto);
	print_load_stats();
	stats(proto);
	if (wflag) {
		sleep(wflag);
		goto again;
	}
	exit(0);
}

void
print_load_stats(void)
{
	static u_int32_t cp_time[5];
	u_int32_t new_cp_time[5];
	int l;
	int shz;
	static int stathz ;

	if (!lflag || !wflag)
		return;
	l = sizeof(new_cp_time) ;
	bzero(new_cp_time, l);
	if (sysctlbyname("kern.cp_time", new_cp_time, &l, NULL, 0) < 0) {
		warn("sysctl: retrieving cp_time length");
		return;
	}
	if (stathz == 0) {
		struct clockinfo ci;

		bzero (&ci, sizeof(ci));
		l = sizeof(ci) ;
		if (sysctlbyname("kern.clockrate", &ci, &l, NULL, 0) < 0) {
			warn("sysctl: retrieving clockinfo length");
			return;
		}
		stathz = ci.stathz ;
		bcopy(new_cp_time, cp_time, sizeof(cp_time));
	}
	shz = stathz * wflag ;
	if (shz == 0)
		shz = 1;
#define X(i)   ( (double)(new_cp_time[i] - cp_time[i])*100/shz )
	printf("\nUSER %5.2f%% NICE %5.2f%% SYS %5.2f%% "
			"INTR %5.2f%% IDLE %5.2f%%\n",
		X(0), X(1), X(2), X(3), X(4) );
	bcopy(new_cp_time, cp_time, sizeof(cp_time));
}
