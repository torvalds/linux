/*-
 * Copyright (c) 2005-2014 Sandvine Incorporated. All rights reserved.
 * Copyright (c) 2000 Darrell Anderson
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
 */

/*
 * netdump_client.c
 * FreeBSD subsystem supporting netdump network dumps.
 * A dedicated server must be running to accept client dumps.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/endian.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/kerneldump.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/if_var.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_options.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/netdump/netdump.h>

#include <machine/in_cksum.h>
#include <machine/pcb.h>

#define	NETDDEBUG(f, ...) do {						\
	if (nd_debug > 0)						\
		printf(("%s: " f), __func__, ## __VA_ARGS__);		\
} while (0)
#define	NETDDEBUG_IF(i, f, ...) do {					\
	if (nd_debug > 0)						\
		if_printf((i), ("%s: " f), __func__, ## __VA_ARGS__);	\
} while (0)
#define	NETDDEBUGV(f, ...) do {						\
	if (nd_debug > 1)						\
		printf(("%s: " f), __func__, ## __VA_ARGS__);		\
} while (0)
#define	NETDDEBUGV_IF(i, f, ...) do {					\
	if (nd_debug > 1)						\
		if_printf((i), ("%s: " f), __func__, ## __VA_ARGS__);	\
} while (0)

static int	 netdump_arp_gw(void);
static void	 netdump_cleanup(void);
static int	 netdump_configure(struct netdump_conf *, struct thread *);
static int	 netdump_dumper(void *priv __unused, void *virtual,
		    vm_offset_t physical __unused, off_t offset, size_t length);
static int	 netdump_ether_output(struct mbuf *m, struct ifnet *ifp,
		    struct ether_addr dst, u_short etype);
static void	 netdump_handle_arp(struct mbuf **mb);
static void	 netdump_handle_ip(struct mbuf **mb);
static int	 netdump_ioctl(struct cdev *dev __unused, u_long cmd,
		    caddr_t addr, int flags __unused, struct thread *td);
static int	 netdump_modevent(module_t mod, int type, void *priv);
static void	 netdump_network_poll(void);
static void	 netdump_pkt_in(struct ifnet *ifp, struct mbuf *m);
static int	 netdump_send(uint32_t type, off_t offset, unsigned char *data,
		    uint32_t datalen);
static int	 netdump_send_arp(in_addr_t dst);
static int	 netdump_start(struct dumperinfo *di);
static int	 netdump_udp_output(struct mbuf *m);

/* Must be at least as big as the chunks dumpsys() gives us. */
static unsigned char nd_buf[MAXDUMPPGS * PAGE_SIZE];
static uint32_t nd_seqno;
static int dump_failed, have_gw_mac;
static void (*drv_if_input)(struct ifnet *, struct mbuf *);
static int restore_gw_addr;

static uint64_t rcvd_acks;
CTASSERT(sizeof(rcvd_acks) * NBBY == NETDUMP_MAX_IN_FLIGHT);

/* Configuration parameters. */
static struct netdump_conf nd_conf;
#define	nd_server	nd_conf.ndc_server
#define	nd_client	nd_conf.ndc_client
#define	nd_gateway	nd_conf.ndc_gateway

/* General dynamic settings. */
static struct ether_addr nd_gw_mac;
static struct ifnet *nd_ifp;
static uint16_t nd_server_port = NETDUMP_PORT;

FEATURE(netdump, "Netdump client support");

static SYSCTL_NODE(_net, OID_AUTO, netdump, CTLFLAG_RD, NULL,
    "netdump parameters");

static int nd_debug;
SYSCTL_INT(_net_netdump, OID_AUTO, debug, CTLFLAG_RWTUN,
    &nd_debug, 0,
    "Debug message verbosity");
static int nd_enabled;
SYSCTL_INT(_net_netdump, OID_AUTO, enabled, CTLFLAG_RD,
    &nd_enabled, 0,
    "netdump configuration status");
static char nd_path[MAXPATHLEN];
SYSCTL_STRING(_net_netdump, OID_AUTO, path, CTLFLAG_RW,
    nd_path, sizeof(nd_path),
    "Server path for output files");
static int nd_polls = 2000;
SYSCTL_INT(_net_netdump, OID_AUTO, polls, CTLFLAG_RWTUN,
    &nd_polls, 0,
    "Number of times to poll before assuming packet loss (0.5ms per poll)");
static int nd_retries = 10;
SYSCTL_INT(_net_netdump, OID_AUTO, retries, CTLFLAG_RWTUN,
    &nd_retries, 0,
    "Number of retransmit attempts before giving up");
static int nd_arp_retries = 3;
SYSCTL_INT(_net_netdump, OID_AUTO, arp_retries, CTLFLAG_RWTUN,
    &nd_arp_retries, 0,
    "Number of ARP attempts before giving up");

/*
 * Checks for netdump support on a network interface
 *
 * Parameters:
 *	ifp	The network interface that is being tested for support
 *
 * Returns:
 *	int	1 if the interface is supported, 0 if not
 */
static bool
netdump_supported_nic(struct ifnet *ifp)
{

	return (ifp->if_netdump_methods != NULL);
}

/*-
 * Network specific primitives.
 * Following down the code they are divided ordered as:
 * - Packet buffer primitives
 * - Output primitives
 * - Input primitives
 * - Polling primitives
 */

/*
 * Handles creation of the ethernet header, then places outgoing packets into
 * the tx buffer for the NIC
 *
 * Parameters:
 *	m	The mbuf containing the packet to be sent (will be freed by
 *		this function or the NIC driver)
 *	ifp	The interface to send on
 *	dst	The destination ethernet address (source address will be looked
 *		up using ifp)
 *	etype	The ETHERTYPE_* value for the protocol that is being sent
 *
 * Returns:
 *	int	see errno.h, 0 for success
 */
static int
netdump_ether_output(struct mbuf *m, struct ifnet *ifp, struct ether_addr dst,
    u_short etype)
{
	struct ether_header *eh;

	if (((ifp->if_flags & (IFF_MONITOR | IFF_UP)) != IFF_UP) ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) != IFF_DRV_RUNNING) {
		if_printf(ifp, "netdump_ether_output: interface isn't up\n");
		m_freem(m);
		return (ENETDOWN);
	}

	/* Fill in the ethernet header. */
	M_PREPEND(m, ETHER_HDR_LEN, M_NOWAIT);
	if (m == NULL) {
		printf("%s: out of mbufs\n", __func__);
		return (ENOBUFS);
	}
	eh = mtod(m, struct ether_header *);
	memcpy(eh->ether_shost, IF_LLADDR(ifp), ETHER_ADDR_LEN);
	memcpy(eh->ether_dhost, dst.octet, ETHER_ADDR_LEN);
	eh->ether_type = htons(etype);
	return ((ifp->if_netdump_methods->nd_transmit)(ifp, m));
}

/*
 * Unreliable transmission of an mbuf chain to the netdump server
 * Note: can't handle fragmentation; fails if the packet is larger than
 *	 nd_ifp->if_mtu after adding the UDP/IP headers
 *
 * Parameters:
 *	m	mbuf chain
 *
 * Returns:
 *	int	see errno.h, 0 for success
 */
static int
netdump_udp_output(struct mbuf *m)
{
	struct udpiphdr *ui;
	struct ip *ip;

	MPASS(nd_ifp != NULL);

	M_PREPEND(m, sizeof(struct udpiphdr), M_NOWAIT);
	if (m == NULL) {
		printf("%s: out of mbufs\n", __func__);
		return (ENOBUFS);
	}

	if (m->m_pkthdr.len > nd_ifp->if_mtu) {
		printf("netdump_udp_output: Packet is too big: %d > MTU %u\n",
		    m->m_pkthdr.len, nd_ifp->if_mtu);
		m_freem(m);
		return (ENOBUFS);
	}

	ui = mtod(m, struct udpiphdr *);
	bzero(ui->ui_x1, sizeof(ui->ui_x1));
	ui->ui_pr = IPPROTO_UDP;
	ui->ui_len = htons(m->m_pkthdr.len - sizeof(struct ip));
	ui->ui_ulen = ui->ui_len;
	ui->ui_src = nd_client;
	ui->ui_dst = nd_server;
	/* Use this src port so that the server can connect() the socket */
	ui->ui_sport = htons(NETDUMP_ACKPORT);
	ui->ui_dport = htons(nd_server_port);
	ui->ui_sum = 0;
	if ((ui->ui_sum = in_cksum(m, m->m_pkthdr.len)) == 0)
		ui->ui_sum = 0xffff;

	ip = mtod(m, struct ip *);
	ip->ip_v = IPVERSION;
	ip->ip_hl = sizeof(struct ip) >> 2;
	ip->ip_tos = 0;
	ip->ip_len = htons(m->m_pkthdr.len);
	ip->ip_id = 0;
	ip->ip_off = htons(IP_DF);
	ip->ip_ttl = 255;
	ip->ip_sum = 0;
	ip->ip_sum = in_cksum(m, sizeof(struct ip));

	return (netdump_ether_output(m, nd_ifp, nd_gw_mac, ETHERTYPE_IP));
}

/*
 * Builds and sends a single ARP request to locate the server
 *
 * Return value:
 *	0 on success
 *	errno on error
 */
static int
netdump_send_arp(in_addr_t dst)
{
	struct ether_addr bcast;
	struct mbuf *m;
	struct arphdr *ah;
	int pktlen;

	MPASS(nd_ifp != NULL);

	/* Fill-up a broadcast address. */
	memset(&bcast, 0xFF, ETHER_ADDR_LEN);
	m = m_gethdr(M_NOWAIT, MT_DATA);
	if (m == NULL) {
		printf("netdump_send_arp: Out of mbufs\n");
		return (ENOBUFS);
	}
	pktlen = arphdr_len2(ETHER_ADDR_LEN, sizeof(struct in_addr));
	m->m_len = pktlen;
	m->m_pkthdr.len = pktlen;
	MH_ALIGN(m, pktlen);
	ah = mtod(m, struct arphdr *);
	ah->ar_hrd = htons(ARPHRD_ETHER);
	ah->ar_pro = htons(ETHERTYPE_IP);
	ah->ar_hln = ETHER_ADDR_LEN;
	ah->ar_pln = sizeof(struct in_addr);
	ah->ar_op = htons(ARPOP_REQUEST);
	memcpy(ar_sha(ah), IF_LLADDR(nd_ifp), ETHER_ADDR_LEN);
	((struct in_addr *)ar_spa(ah))->s_addr = nd_client.s_addr;
	bzero(ar_tha(ah), ETHER_ADDR_LEN);
	((struct in_addr *)ar_tpa(ah))->s_addr = dst;
	return (netdump_ether_output(m, nd_ifp, bcast, ETHERTYPE_ARP));
}

/*
 * Sends ARP requests to locate the server and waits for a response.
 * We first try to ARP the server itself, and fall back to the provided
 * gateway if the server appears to be off-link.
 *
 * Return value:
 *	0 on success
 *	errno on error
 */
static int
netdump_arp_gw(void)
{
	in_addr_t dst;
	int error, polls, retries;

	dst = nd_server.s_addr;
restart:
	for (retries = 0; retries < nd_arp_retries && have_gw_mac == 0;
	    retries++) {
		error = netdump_send_arp(dst);
		if (error != 0)
			return (error);
		for (polls = 0; polls < nd_polls && have_gw_mac == 0; polls++) {
			netdump_network_poll();
			DELAY(500);
		}
		if (have_gw_mac == 0)
			printf("(ARP retry)");
	}
	if (have_gw_mac != 0)
		return (0);
	if (dst == nd_server.s_addr && nd_server.s_addr != nd_gateway.s_addr) {
		printf("Failed to ARP server, trying to reach gateway...\n");
		dst = nd_gateway.s_addr;
		goto restart;
	}

	printf("\nARP timed out.\n");
	return (ETIMEDOUT);
}

/*
 * Dummy free function for netdump clusters.
 */
static void
netdump_mbuf_free(struct mbuf *m __unused)
{
}

/*
 * Construct and reliably send a netdump packet.  May fail from a resource
 * shortage or extreme number of unacknowledged retransmissions.  Wait for
 * an acknowledgement before returning.  Splits packets into chunks small
 * enough to be sent without fragmentation (looks up the interface MTU)
 *
 * Parameters:
 *	type	netdump packet type (HERALD, FINISHED, or VMCORE)
 *	offset	vmcore data offset (bytes)
 *	data	vmcore data
 *	datalen	vmcore data size (bytes)
 *
 * Returns:
 *	int see errno.h, 0 for success
 */
static int
netdump_send(uint32_t type, off_t offset, unsigned char *data, uint32_t datalen)
{
	struct netdump_msg_hdr *nd_msg_hdr;
	struct mbuf *m, *m2;
	uint64_t want_acks;
	uint32_t i, pktlen, sent_so_far;
	int retries, polls, error;

	want_acks = 0;
	rcvd_acks = 0;
	retries = 0;

	MPASS(nd_ifp != NULL);

retransmit:
	/* Chunks can be too big to fit in packets. */
	for (i = sent_so_far = 0; sent_so_far < datalen ||
	    (i == 0 && datalen == 0); i++) {
		pktlen = datalen - sent_so_far;

		/* First bound: the packet structure. */
		pktlen = min(pktlen, NETDUMP_DATASIZE);

		/* Second bound: the interface MTU (assume no IP options). */
		pktlen = min(pktlen, nd_ifp->if_mtu - sizeof(struct udpiphdr) -
		    sizeof(struct netdump_msg_hdr));

		/*
		 * Check if it is retransmitting and this has been ACKed
		 * already.
		 */
		if ((rcvd_acks & (1 << i)) != 0) {
			sent_so_far += pktlen;
			continue;
		}

		/*
		 * Get and fill a header mbuf, then chain data as an extended
		 * mbuf.
		 */
		m = m_gethdr(M_NOWAIT, MT_DATA);
		if (m == NULL) {
			printf("netdump_send: Out of mbufs\n");
			return (ENOBUFS);
		}
		m->m_len = sizeof(struct netdump_msg_hdr);
		m->m_pkthdr.len = sizeof(struct netdump_msg_hdr);
		MH_ALIGN(m, sizeof(struct netdump_msg_hdr));
		nd_msg_hdr = mtod(m, struct netdump_msg_hdr *);
		nd_msg_hdr->mh_seqno = htonl(nd_seqno + i);
		nd_msg_hdr->mh_type = htonl(type);
		nd_msg_hdr->mh_offset = htobe64(offset + sent_so_far);
		nd_msg_hdr->mh_len = htonl(pktlen);
		nd_msg_hdr->mh__pad = 0;

		if (pktlen != 0) {
			m2 = m_get(M_NOWAIT, MT_DATA);
			if (m2 == NULL) {
				m_freem(m);
				printf("netdump_send: Out of mbufs\n");
				return (ENOBUFS);
			}
			MEXTADD(m2, data + sent_so_far, pktlen,
			    netdump_mbuf_free, NULL, NULL, 0, EXT_DISPOSABLE);
			m2->m_len = pktlen;

			m_cat(m, m2);
			m->m_pkthdr.len += pktlen;
		}
		error = netdump_udp_output(m);
		if (error != 0)
			return (error);

		/* Note that we're waiting for this packet in the bitfield. */
		want_acks |= (1 << i);
		sent_so_far += pktlen;
	}
	if (i >= NETDUMP_MAX_IN_FLIGHT)
		printf("Warning: Sent more than %d packets (%d). "
		    "Acknowledgements will fail unless the size of "
		    "rcvd_acks/want_acks is increased.\n",
		    NETDUMP_MAX_IN_FLIGHT, i);

	/*
	 * Wait for acks.  A *real* window would speed things up considerably.
	 */
	polls = 0;
	while (rcvd_acks != want_acks) {
		if (polls++ > nd_polls) {
			if (retries++ > nd_retries)
				return (ETIMEDOUT);
			printf(". ");
			goto retransmit;
		}
		netdump_network_poll();
		DELAY(500);
	}
	nd_seqno += i;
	return (0);
}

/*
 * Handler for IP packets: checks their sanity and then processes any netdump
 * ACK packets it finds.
 *
 * It needs to replicate partially the behaviour of ip_input() and
 * udp_input().
 *
 * Parameters:
 *	mb	a pointer to an mbuf * containing the packet received
 *		Updates *mb if m_pullup et al change the pointer
 *		Assumes the calling function will take care of freeing the mbuf
 */
static void
netdump_handle_ip(struct mbuf **mb)
{
	struct ip *ip;
	struct udpiphdr *udp;
	struct netdump_ack *nd_ack;
	struct mbuf *m;
	int rcv_ackno;
	unsigned short hlen;

	/* IP processing. */
	m = *mb;
	if (m->m_pkthdr.len < sizeof(struct ip)) {
		NETDDEBUG("dropping packet too small for IP header\n");
		return;
	}
	if (m->m_len < sizeof(struct ip)) {
		m = m_pullup(m, sizeof(struct ip));
		*mb = m;
		if (m == NULL) {
			NETDDEBUG("m_pullup failed\n");
			return;
		}
	}
	ip = mtod(m, struct ip *);

	/* IP version. */
	if (ip->ip_v != IPVERSION) {
		NETDDEBUG("bad IP version %d\n", ip->ip_v);
		return;
	}

	/* Header length. */
	hlen = ip->ip_hl << 2;
	if (hlen < sizeof(struct ip)) {
		NETDDEBUG("bad IP header length (%hu)\n", hlen);
		return;
	}
	if (hlen > m->m_len) {
		m = m_pullup(m, hlen);
		*mb = m;
		if (m == NULL) {
			NETDDEBUG("m_pullup failed\n");
			return;
		}
		ip = mtod(m, struct ip *);
	}
	/* Ignore packets with IP options. */
	if (hlen > sizeof(struct ip)) {
		NETDDEBUG("drop packet with IP options\n");
		return;
	}

#ifdef INVARIANTS
	if (((ntohl(ip->ip_dst.s_addr) >> IN_CLASSA_NSHIFT) == IN_LOOPBACKNET ||
	    (ntohl(ip->ip_src.s_addr) >> IN_CLASSA_NSHIFT) == IN_LOOPBACKNET) &&
	    (m->m_pkthdr.rcvif->if_flags & IFF_LOOPBACK) == 0) {
		NETDDEBUG("Bad IP header (RFC1122)\n");
		return;
	}
#endif

	/* Checksum. */
	if ((m->m_pkthdr.csum_flags & CSUM_IP_CHECKED) != 0) {
		if ((m->m_pkthdr.csum_flags & CSUM_IP_VALID) == 0) {
			NETDDEBUG("bad IP checksum\n");
			return;
		}
	} else {
		/* XXX */ ;
	}

	/* Convert fields to host byte order. */
	ip->ip_len = ntohs(ip->ip_len);
	if (ip->ip_len < hlen) {
		NETDDEBUG("IP packet smaller (%hu) than header (%hu)\n",
		    ip->ip_len, hlen);
		return;
	}
	if (m->m_pkthdr.len < ip->ip_len) {
		NETDDEBUG("IP packet bigger (%hu) than ethernet packet (%d)\n",
		    ip->ip_len, m->m_pkthdr.len);
		return;
	}
	if (m->m_pkthdr.len > ip->ip_len) {

		/* Truncate the packet to the IP length. */
		if (m->m_len == m->m_pkthdr.len) {
			m->m_len = ip->ip_len;
			m->m_pkthdr.len = ip->ip_len;
		} else
			m_adj(m, ip->ip_len - m->m_pkthdr.len);
	}

	ip->ip_off = ntohs(ip->ip_off);

	/* Check that the source is the server's IP. */
	if (ip->ip_src.s_addr != nd_server.s_addr) {
		NETDDEBUG("drop packet not from server (from 0x%x)\n",
		    ip->ip_src.s_addr);
		return;
	}

	/* Check if the destination IP is ours. */
	if (ip->ip_dst.s_addr != nd_client.s_addr) {
		NETDDEBUGV("drop packet not to our IP\n");
		return;
	}

	if (ip->ip_p != IPPROTO_UDP) {
		NETDDEBUG("drop non-UDP packet\n");
		return;
	}

	/* Do not deal with fragments. */
	if ((ip->ip_off & (IP_MF | IP_OFFMASK)) != 0) {
		NETDDEBUG("drop fragmented packet\n");
		return;
	}

	/* UDP custom is to have packet length not include IP header. */
	ip->ip_len -= hlen;

	/* UDP processing. */

	/* Get IP and UDP headers together, along with the netdump packet. */
	if (m->m_pkthdr.len <
	    sizeof(struct udpiphdr) + sizeof(struct netdump_ack)) {
		NETDDEBUG("ignoring small packet\n");
		return;
	}
	if (m->m_len < sizeof(struct udpiphdr) + sizeof(struct netdump_ack)) {
		m = m_pullup(m, sizeof(struct udpiphdr) +
		    sizeof(struct netdump_ack));
		*mb = m;
		if (m == NULL) {
			NETDDEBUG("m_pullup failed\n");
			return;
		}
	}
	udp = mtod(m, struct udpiphdr *);

	if (ntohs(udp->ui_u.uh_dport) != NETDUMP_ACKPORT) {
		NETDDEBUG("not on the netdump port.\n");
		return;
	}

	/* Netdump processing. */

	/*
	 * Packet is meant for us.  Extract the ack sequence number and the
	 * port number if necessary.
	 */
	nd_ack = (struct netdump_ack *)(mtod(m, caddr_t) +
	    sizeof(struct udpiphdr));
	rcv_ackno = ntohl(nd_ack->na_seqno);
	if (nd_server_port == NETDUMP_PORT)
		nd_server_port = ntohs(udp->ui_u.uh_sport);
	if (rcv_ackno >= nd_seqno + NETDUMP_MAX_IN_FLIGHT)
		printf("%s: ACK %d too far in future!\n", __func__, rcv_ackno);
	else if (rcv_ackno >= nd_seqno) {
		/* We're interested in this ack. Record it. */
		rcvd_acks |= 1 << (rcv_ackno - nd_seqno);
	}
}

/*
 * Handler for ARP packets: checks their sanity and then
 * 1. If the ARP is a request for our IP, respond with our MAC address
 * 2. If the ARP is a response from our server, record its MAC address
 *
 * It needs to replicate partially the behaviour of arpintr() and
 * in_arpinput().
 *
 * Parameters:
 *	mb	a pointer to an mbuf * containing the packet received
 *		Updates *mb if m_pullup et al change the pointer
 *		Assumes the calling function will take care of freeing the mbuf
 */
static void
netdump_handle_arp(struct mbuf **mb)
{
	char buf[INET_ADDRSTRLEN];
	struct in_addr isaddr, itaddr, myaddr;
	struct ether_addr dst;
	struct mbuf *m;
	struct arphdr *ah;
	struct ifnet *ifp;
	uint8_t *enaddr;
	int req_len, op;

	m = *mb;
	ifp = m->m_pkthdr.rcvif;
	if (m->m_len < sizeof(struct arphdr)) {
		m = m_pullup(m, sizeof(struct arphdr));
		*mb = m;
		if (m == NULL) {
			NETDDEBUG("runt packet: m_pullup failed\n");
			return;
		}
	}

	ah = mtod(m, struct arphdr *);
	if (ntohs(ah->ar_hrd) != ARPHRD_ETHER) {
		NETDDEBUG("unknown hardware address 0x%2D)\n",
		    (unsigned char *)&ah->ar_hrd, "");
		return;
	}
	if (ntohs(ah->ar_pro) != ETHERTYPE_IP) {
		NETDDEBUG("drop ARP for unknown protocol %d\n",
		    ntohs(ah->ar_pro));
		return;
	}
	req_len = arphdr_len2(ifp->if_addrlen, sizeof(struct in_addr));
	if (m->m_len < req_len) {
		m = m_pullup(m, req_len);
		*mb = m;
		if (m == NULL) {
			NETDDEBUG("runt packet: m_pullup failed\n");
			return;
		}
	}
	ah = mtod(m, struct arphdr *);

	op = ntohs(ah->ar_op);
	memcpy(&isaddr, ar_spa(ah), sizeof(isaddr));
	memcpy(&itaddr, ar_tpa(ah), sizeof(itaddr));
	enaddr = (uint8_t *)IF_LLADDR(ifp);
	myaddr = nd_client;

	if (memcmp(ar_sha(ah), enaddr, ifp->if_addrlen) == 0) {
		NETDDEBUG("ignoring ARP from myself\n");
		return;
	}

	if (isaddr.s_addr == nd_client.s_addr) {
		printf("%s: %*D is using my IP address %s!\n", __func__,
		    ifp->if_addrlen, (u_char *)ar_sha(ah), ":",
		    inet_ntoa_r(isaddr, buf));
		return;
	}

	if (memcmp(ar_sha(ah), ifp->if_broadcastaddr, ifp->if_addrlen) == 0) {
		NETDDEBUG("ignoring ARP from broadcast address\n");
		return;
	}

	if (op == ARPOP_REPLY) {
		if (isaddr.s_addr != nd_gateway.s_addr &&
		    isaddr.s_addr != nd_server.s_addr) {
			inet_ntoa_r(isaddr, buf);
			NETDDEBUG(
			    "ignoring ARP reply from %s (not netdump server)\n",
			    buf);
			return;
		}
		memcpy(nd_gw_mac.octet, ar_sha(ah),
		    min(ah->ar_hln, ETHER_ADDR_LEN));
		have_gw_mac = 1;
		NETDDEBUG("got server MAC address %6D\n", nd_gw_mac.octet, ":");
		return;
	}

	if (op != ARPOP_REQUEST) {
		NETDDEBUG("ignoring ARP non-request/reply\n");
		return;
	}

	if (itaddr.s_addr != nd_client.s_addr) {
		NETDDEBUG("ignoring ARP not to our IP\n");
		return;
	}

	memcpy(ar_tha(ah), ar_sha(ah), ah->ar_hln);
	memcpy(ar_sha(ah), enaddr, ah->ar_hln);
	memcpy(ar_tpa(ah), ar_spa(ah), ah->ar_pln);
	memcpy(ar_spa(ah), &itaddr, ah->ar_pln);
	ah->ar_op = htons(ARPOP_REPLY);
	ah->ar_pro = htons(ETHERTYPE_IP);
	m->m_flags &= ~(M_BCAST|M_MCAST);
	m->m_len = arphdr_len(ah);
	m->m_pkthdr.len = m->m_len;

	memcpy(dst.octet, ar_tha(ah), ETHER_ADDR_LEN);
	netdump_ether_output(m, ifp, dst, ETHERTYPE_ARP);
	*mb = NULL;
}

/*
 * Handler for incoming packets directly from the network adapter
 * Identifies the packet type (IP or ARP) and passes it along to one of the
 * helper functions netdump_handle_ip or netdump_handle_arp.
 *
 * It needs to replicate partially the behaviour of ether_input() and
 * ether_demux().
 *
 * Parameters:
 *	ifp	the interface the packet came from (should be nd_ifp)
 *	m	an mbuf containing the packet received
 */
static void
netdump_pkt_in(struct ifnet *ifp, struct mbuf *m)
{
	struct ifreq ifr;
	struct ether_header *eh;
	u_short etype;

	/* Ethernet processing. */
	if ((m->m_flags & M_PKTHDR) == 0) {
		NETDDEBUG_IF(ifp, "discard frame without packet header\n");
		goto done;
	}
	if (m->m_len < ETHER_HDR_LEN) {
		NETDDEBUG_IF(ifp,
	    "discard frame without leading eth header (len %u pktlen %u)\n",
		    m->m_len, m->m_pkthdr.len);
		goto done;
	}
	if ((m->m_flags & M_HASFCS) != 0) {
		m_adj(m, -ETHER_CRC_LEN);
		m->m_flags &= ~M_HASFCS;
	}
	eh = mtod(m, struct ether_header *);
	etype = ntohs(eh->ether_type);
	if ((m->m_flags & M_VLANTAG) != 0 || etype == ETHERTYPE_VLAN) {
		NETDDEBUG_IF(ifp, "ignoring vlan packets\n");
		goto done;
	}
	if (if_gethwaddr(ifp, &ifr) != 0) {
		NETDDEBUG_IF(ifp, "failed to get hw addr for interface\n");
		goto done;
	}
	if (memcmp(ifr.ifr_addr.sa_data, eh->ether_dhost,
	    ETHER_ADDR_LEN) != 0) {
		NETDDEBUG_IF(ifp,
		    "discard frame with incorrect destination addr\n");
		goto done;
	}

	/* Done ethernet processing. Strip off the ethernet header. */
	m_adj(m, ETHER_HDR_LEN);
	switch (etype) {
	case ETHERTYPE_ARP:
		netdump_handle_arp(&m);
		break;
	case ETHERTYPE_IP:
		netdump_handle_ip(&m);
		break;
	default:
		NETDDEBUG_IF(ifp, "dropping unknown ethertype %hu\n", etype);
		break;
	}
done:
	if (m != NULL)
		m_freem(m);
}

/*
 * After trapping, instead of assuming that most of the network stack is sane,
 * we just poll the driver directly for packets.
 */
static void
netdump_network_poll(void)
{

	MPASS(nd_ifp != NULL);

	nd_ifp->if_netdump_methods->nd_poll(nd_ifp, 1000);
}

/*-
 * Dumping specific primitives.
 */

/*
 * Callback from dumpsys() to dump a chunk of memory.
 * Copies it out to our static buffer then sends it across the network.
 * Detects the initial KDH and makes sure it is given a special packet type.
 *
 * Parameters:
 *	priv	 Unused. Optional private pointer.
 *	virtual  Virtual address (where to read the data from)
 *	physical Unused. Physical memory address.
 *	offset	 Offset from start of core file
 *	length	 Data length
 *
 * Return value:
 *	0 on success
 *	errno on error
 */
static int
netdump_dumper(void *priv __unused, void *virtual,
    vm_offset_t physical __unused, off_t offset, size_t length)
{
	int error;

	NETDDEBUGV("netdump_dumper(NULL, %p, NULL, %ju, %zu)\n",
	    virtual, (uintmax_t)offset, length);

	if (virtual == NULL) {
		if (dump_failed != 0)
			printf("failed to dump the kernel core\n");
		else if (netdump_send(NETDUMP_FINISHED, 0, NULL, 0) != 0)
			printf("failed to close the transaction\n");
		else
			printf("\nnetdump finished.\n");
		netdump_cleanup();
		return (0);
	}
	if (length > sizeof(nd_buf))
		return (ENOSPC);

	memmove(nd_buf, virtual, length);
	error = netdump_send(NETDUMP_VMCORE, offset, nd_buf, length);
	if (error != 0) {
		dump_failed = 1;
		return (error);
	}
	return (0);
}

/*
 * Perform any initalization needed prior to transmitting the kernel core.
 */
static int
netdump_start(struct dumperinfo *di)
{
	char *path;
	char buf[INET_ADDRSTRLEN];
	uint32_t len;
	int error;

	error = 0;

	/* Check if the dumping is allowed to continue. */
	if (nd_enabled == 0)
		return (EINVAL);

	if (panicstr == NULL) {
		printf(
		    "netdump_start: netdump may only be used after a panic\n");
		return (EINVAL);
	}

	MPASS(nd_ifp != NULL);

	if (nd_server.s_addr == INADDR_ANY) {
		printf("netdump_start: can't netdump; no server IP given\n");
		return (EINVAL);
	}
	if (nd_client.s_addr == INADDR_ANY) {
		printf("netdump_start: can't netdump; no client IP given\n");
		return (EINVAL);
	}

	/* We start dumping at offset 0. */
	di->dumpoff = 0;

	nd_seqno = 1;

	/*
	 * nd_server_port could have switched after the first ack the
	 * first time it gets called.  Adjust it accordingly.
	 */
	nd_server_port = NETDUMP_PORT;

	/* Switch to the netdump mbuf zones. */
	netdump_mbuf_dump();

	nd_ifp->if_netdump_methods->nd_event(nd_ifp, NETDUMP_START);

	/* Make the card use *our* receive callback. */
	drv_if_input = nd_ifp->if_input;
	nd_ifp->if_input = netdump_pkt_in;

	if (nd_gateway.s_addr == INADDR_ANY) {
		restore_gw_addr = 1;
		nd_gateway.s_addr = nd_server.s_addr;
	}

	printf("netdump in progress. searching for server...\n");
	if (netdump_arp_gw()) {
		printf("failed to locate server MAC address\n");
		error = EINVAL;
		goto trig_abort;
	}

	if (nd_path[0] != '\0') {
		path = nd_path;
		len = strlen(path) + 1;
	} else {
		path = NULL;
		len = 0;
	}
	if (netdump_send(NETDUMP_HERALD, 0, path, len) != 0) {
		printf("failed to contact netdump server\n");
		error = EINVAL;
		goto trig_abort;
	}
	printf("netdumping to %s (%6D)\n", inet_ntoa_r(nd_server, buf),
	    nd_gw_mac.octet, ":");
	return (0);

trig_abort:
	netdump_cleanup();
	return (error);
}

static int
netdump_write_headers(struct dumperinfo *di, struct kerneldumpheader *kdh,
    void *key, uint32_t keysize)
{
	int error;

	memcpy(nd_buf, kdh, sizeof(*kdh));
	error = netdump_send(NETDUMP_KDH, 0, nd_buf, sizeof(*kdh));
	if (error == 0 && keysize > 0) {
		if (keysize > sizeof(nd_buf))
			return (EINVAL);
		memcpy(nd_buf, key, keysize);
		error = netdump_send(NETDUMP_EKCD_KEY, 0, nd_buf, keysize);
	}
	return (error);
}

/*
 * Cleanup routine for a possibly failed netdump.
 */
static void
netdump_cleanup(void)
{

	if (restore_gw_addr != 0) {
		nd_gateway.s_addr = INADDR_ANY;
		restore_gw_addr = 0;
	}
	if (drv_if_input != NULL) {
		nd_ifp->if_input = drv_if_input;
		drv_if_input = NULL;
	}
	nd_ifp->if_netdump_methods->nd_event(nd_ifp, NETDUMP_END);
}

/*-
 * KLD specific code.
 */

static struct cdevsw netdump_cdevsw = {
	.d_version =	D_VERSION,
	.d_ioctl =	netdump_ioctl,
	.d_name =	"netdump",
};

static struct cdev *netdump_cdev;

static int
netdump_configure(struct netdump_conf *conf, struct thread *td)
{
	struct epoch_tracker et;
	struct ifnet *ifp;

	CURVNET_SET(TD_TO_VNET(td));
	if (!IS_DEFAULT_VNET(curvnet)) {
		CURVNET_RESTORE();
		return (EINVAL);
	}
	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifp, &V_ifnet, if_link) {
		if (strcmp(ifp->if_xname, conf->ndc_iface) == 0)
			break;
	}
	/* XXX ref */
	NET_EPOCH_EXIT(et);
	CURVNET_RESTORE();

	if (ifp == NULL)
		return (ENOENT);
	if ((if_getflags(ifp) & IFF_UP) == 0)
		return (ENXIO);
	if (!netdump_supported_nic(ifp) || ifp->if_type != IFT_ETHER)
		return (EINVAL);

	nd_ifp = ifp;
	netdump_reinit(ifp);
	memcpy(&nd_conf, conf, sizeof(nd_conf));
	nd_enabled = 1;
	return (0);
}

/*
 * Reinitialize the mbuf pool used by drivers while dumping. This is called
 * from the generic ioctl handler for SIOCSIFMTU after the driver has
 * reconfigured itself.
 */
void
netdump_reinit(struct ifnet *ifp)
{
	int clsize, nmbuf, ncl, nrxr;

	if (ifp != nd_ifp)
		return;

	ifp->if_netdump_methods->nd_init(ifp, &nrxr, &ncl, &clsize);
	KASSERT(nrxr > 0, ("invalid receive ring count %d", nrxr));

	/*
	 * We need two headers per message on the transmit side. Multiply by
	 * four to give us some breathing room.
	 */
	nmbuf = ncl * (4 + nrxr);
	ncl *= nrxr;
	netdump_mbuf_reinit(nmbuf, ncl, clsize);
}

/*
 * ioctl(2) handler for the netdump device. This is currently only used to
 * register netdump as a dump device.
 *
 * Parameters:
 *     dev, Unused.
 *     cmd, The ioctl to be handled.
 *     addr, The parameter for the ioctl.
 *     flags, Unused.
 *     td, The thread invoking this ioctl.
 *
 * Returns:
 *     0 on success, and an errno value on failure.
 */
static int
netdump_ioctl(struct cdev *dev __unused, u_long cmd, caddr_t addr,
    int flags __unused, struct thread *td)
{
	struct diocskerneldump_arg *kda;
	struct dumperinfo dumper;
	struct netdump_conf *conf;
	uint8_t *encryptedkey;
	int error;
	u_int u;

	error = 0;
	switch (cmd) {
	case DIOCSKERNELDUMP:
		u = *(u_int *)addr;
		if (u != 0) {
			error = ENXIO;
			break;
		}

		if (nd_enabled) {
			nd_enabled = 0;
			netdump_mbuf_drain();
		}
		break;
	case NETDUMPGCONF:
		conf = (struct netdump_conf *)addr;
		if (!nd_enabled) {
			error = ENXIO;
			break;
		}

		strlcpy(conf->ndc_iface, nd_ifp->if_xname,
		    sizeof(conf->ndc_iface));
		memcpy(&conf->ndc_server, &nd_server, sizeof(nd_server));
		memcpy(&conf->ndc_client, &nd_client, sizeof(nd_client));
		memcpy(&conf->ndc_gateway, &nd_gateway, sizeof(nd_gateway));
		break;
	case NETDUMPSCONF:
		conf = (struct netdump_conf *)addr;
		encryptedkey = NULL;
		kda = &conf->ndc_kda;

		conf->ndc_iface[sizeof(conf->ndc_iface) - 1] = '\0';
		if (kda->kda_enable == 0) {
			if (nd_enabled) {
				error = clear_dumper(td);
				if (error == 0) {
					nd_enabled = 0;
					netdump_mbuf_drain();
				}
			}
			break;
		}

		error = netdump_configure(conf, td);
		if (error != 0)
			break;

		if (kda->kda_encryption != KERNELDUMP_ENC_NONE) {
			if (kda->kda_encryptedkeysize <= 0 ||
			    kda->kda_encryptedkeysize >
			    KERNELDUMP_ENCKEY_MAX_SIZE)
				return (EINVAL);
			encryptedkey = malloc(kda->kda_encryptedkeysize, M_TEMP,
			    M_WAITOK);
			error = copyin(kda->kda_encryptedkey, encryptedkey,
			    kda->kda_encryptedkeysize);
			if (error != 0) {
				free(encryptedkey, M_TEMP);
				return (error);
			}
		}

		memset(&dumper, 0, sizeof(dumper));
		dumper.dumper_start = netdump_start;
		dumper.dumper_hdr = netdump_write_headers;
		dumper.dumper = netdump_dumper;
		dumper.priv = NULL;
		dumper.blocksize = NETDUMP_DATASIZE;
		dumper.maxiosize = MAXDUMPPGS * PAGE_SIZE;
		dumper.mediaoffset = 0;
		dumper.mediasize = 0;

		error = set_dumper(&dumper, conf->ndc_iface, td,
		    kda->kda_compression, kda->kda_encryption,
		    kda->kda_key, kda->kda_encryptedkeysize,
		    encryptedkey);
		if (encryptedkey != NULL) {
			explicit_bzero(encryptedkey, kda->kda_encryptedkeysize);
			free(encryptedkey, M_TEMP);
		}
		if (error != 0) {
			nd_enabled = 0;
			netdump_mbuf_drain();
		}
		break;
	default:
		error = EINVAL;
		break;
	}
	return (error);
}

/*
 * Called upon system init or kld load.  Initializes the netdump parameters to
 * sane defaults (locates the first available NIC and uses the first IPv4 IP on
 * that card as the client IP).  Leaves the server IP unconfigured.
 *
 * Parameters:
 *	mod, Unused.
 *	what, The module event type.
 *	priv, Unused.
 *
 * Returns:
 *	int, An errno value if an error occured, 0 otherwise.
 */
static int
netdump_modevent(module_t mod __unused, int what, void *priv __unused)
{
	struct netdump_conf conf;
	char *arg;
	int error;

	error = 0;
	switch (what) {
	case MOD_LOAD:
		error = make_dev_p(MAKEDEV_WAITOK, &netdump_cdev,
		    &netdump_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600, "netdump");
		if (error != 0)
			return (error);

		if ((arg = kern_getenv("net.dump.iface")) != NULL) {
			strlcpy(conf.ndc_iface, arg, sizeof(conf.ndc_iface));
			freeenv(arg);

			if ((arg = kern_getenv("net.dump.server")) != NULL) {
				inet_aton(arg, &conf.ndc_server);
				freeenv(arg);
			}
			if ((arg = kern_getenv("net.dump.client")) != NULL) {
				inet_aton(arg, &conf.ndc_server);
				freeenv(arg);
			}
			if ((arg = kern_getenv("net.dump.gateway")) != NULL) {
				inet_aton(arg, &conf.ndc_server);
				freeenv(arg);
			}

			/* Ignore errors; we print a message to the console. */
			(void)netdump_configure(&conf, curthread);
		}
		break;
	case MOD_UNLOAD:
		destroy_dev(netdump_cdev);
		if (nd_enabled) {
			printf("netdump: disabling dump device for unload\n");
			(void)clear_dumper(curthread);
			nd_enabled = 0;
		}
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}

static moduledata_t netdump_mod = {
	"netdump",
	netdump_modevent,
	NULL,
};

MODULE_VERSION(netdump, 1);
DECLARE_MODULE(netdump, netdump_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
