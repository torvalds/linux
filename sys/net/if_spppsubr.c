/*
 * Synchronous PPP/Cisco/Frame Relay link level subroutines.
 * Keepalive protocol implemented in both Cisco and PPP modes.
 */
/*-
 * Copyright (C) 1994-2000 Cronyx Engineering.
 * Author: Serge Vakulenko, <vak@cronyx.ru>
 *
 * Heavily revamped to conform to RFC 1661.
 * Copyright (C) 1997, 2001 Joerg Wunsch.
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations permission to use
 * or modify this software as long as this message is kept with the software,
 * all derivative works or modified versions.
 *
 * From: Version 2.4, Thu Apr 30 17:17:21 MSD 1997
 *
 * $FreeBSD$
 */

#include <sys/param.h>

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/rmlock.h>
#include <sys/sockio.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/random.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>

#include <sys/md5.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/netisr.h>
#include <net/if_types.h>
#include <net/route.h>
#include <net/vnet.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <net/slcompress.h>

#include <machine/stdarg.h>

#include <netinet/in_var.h>

#ifdef INET
#include <netinet/ip.h>
#include <netinet/tcp.h>
#endif

#ifdef INET6
#include <netinet6/scope6_var.h>
#endif

#include <netinet/if_ether.h>

#include <net/if_sppp.h>

#define IOCTL_CMD_T	u_long
#define MAXALIVECNT     3               /* max. alive packets */

/*
 * Interface flags that can be set in an ifconfig command.
 *
 * Setting link0 will make the link passive, i.e. it will be marked
 * as being administrative openable, but won't be opened to begin
 * with.  Incoming calls will be answered, or subsequent calls with
 * -link1 will cause the administrative open of the LCP layer.
 *
 * Setting link1 will cause the link to auto-dial only as packets
 * arrive to be sent.
 *
 * Setting IFF_DEBUG will syslog the option negotiation and state
 * transitions at level kern.debug.  Note: all logs consistently look
 * like
 *
 *   <if-name><unit>: <proto-name> <additional info...>
 *
 * with <if-name><unit> being something like "bppp0", and <proto-name>
 * being one of "lcp", "ipcp", "cisco", "chap", "pap", etc.
 */

#define IFF_PASSIVE	IFF_LINK0	/* wait passively for connection */
#define IFF_AUTO	IFF_LINK1	/* auto-dial on output */
#define IFF_CISCO	IFF_LINK2	/* auto-dial on output */

#define PPP_ALLSTATIONS 0xff		/* All-Stations broadcast address */
#define PPP_UI		0x03		/* Unnumbered Information */
#define PPP_IP		0x0021		/* Internet Protocol */
#define PPP_ISO		0x0023		/* ISO OSI Protocol */
#define PPP_XNS		0x0025		/* Xerox NS Protocol */
#define PPP_IPX		0x002b		/* Novell IPX Protocol */
#define PPP_VJ_COMP	0x002d		/* VJ compressed TCP/IP */
#define PPP_VJ_UCOMP	0x002f		/* VJ uncompressed TCP/IP */
#define PPP_IPV6	0x0057		/* Internet Protocol Version 6 */
#define PPP_LCP		0xc021		/* Link Control Protocol */
#define PPP_PAP		0xc023		/* Password Authentication Protocol */
#define PPP_CHAP	0xc223		/* Challenge-Handshake Auth Protocol */
#define PPP_IPCP	0x8021		/* Internet Protocol Control Protocol */
#define PPP_IPV6CP	0x8057		/* IPv6 Control Protocol */

#define CONF_REQ	1		/* PPP configure request */
#define CONF_ACK	2		/* PPP configure acknowledge */
#define CONF_NAK	3		/* PPP configure negative ack */
#define CONF_REJ	4		/* PPP configure reject */
#define TERM_REQ	5		/* PPP terminate request */
#define TERM_ACK	6		/* PPP terminate acknowledge */
#define CODE_REJ	7		/* PPP code reject */
#define PROTO_REJ	8		/* PPP protocol reject */
#define ECHO_REQ	9		/* PPP echo request */
#define ECHO_REPLY	10		/* PPP echo reply */
#define DISC_REQ	11		/* PPP discard request */

#define LCP_OPT_MRU		1	/* maximum receive unit */
#define LCP_OPT_ASYNC_MAP	2	/* async control character map */
#define LCP_OPT_AUTH_PROTO	3	/* authentication protocol */
#define LCP_OPT_QUAL_PROTO	4	/* quality protocol */
#define LCP_OPT_MAGIC		5	/* magic number */
#define LCP_OPT_RESERVED	6	/* reserved */
#define LCP_OPT_PROTO_COMP	7	/* protocol field compression */
#define LCP_OPT_ADDR_COMP	8	/* address/control field compression */

#define IPCP_OPT_ADDRESSES	1	/* both IP addresses; deprecated */
#define IPCP_OPT_COMPRESSION	2	/* IP compression protocol (VJ) */
#define IPCP_OPT_ADDRESS	3	/* local IP address */

#define IPV6CP_OPT_IFID	1	/* interface identifier */
#define IPV6CP_OPT_COMPRESSION	2	/* IPv6 compression protocol */

#define IPCP_COMP_VJ		0x2d	/* Code for VJ compression */

#define PAP_REQ			1	/* PAP name/password request */
#define PAP_ACK			2	/* PAP acknowledge */
#define PAP_NAK			3	/* PAP fail */

#define CHAP_CHALLENGE		1	/* CHAP challenge request */
#define CHAP_RESPONSE		2	/* CHAP challenge response */
#define CHAP_SUCCESS		3	/* CHAP response ok */
#define CHAP_FAILURE		4	/* CHAP response failed */

#define CHAP_MD5		5	/* hash algorithm - MD5 */

#define CISCO_MULTICAST		0x8f	/* Cisco multicast address */
#define CISCO_UNICAST		0x0f	/* Cisco unicast address */
#define CISCO_KEEPALIVE		0x8035	/* Cisco keepalive protocol */
#define CISCO_ADDR_REQ		0	/* Cisco address request */
#define CISCO_ADDR_REPLY	1	/* Cisco address reply */
#define CISCO_KEEPALIVE_REQ	2	/* Cisco keepalive request */

/* states are named and numbered according to RFC 1661 */
#define STATE_INITIAL	0
#define STATE_STARTING	1
#define STATE_CLOSED	2
#define STATE_STOPPED	3
#define STATE_CLOSING	4
#define STATE_STOPPING	5
#define STATE_REQ_SENT	6
#define STATE_ACK_RCVD	7
#define STATE_ACK_SENT	8
#define STATE_OPENED	9

static MALLOC_DEFINE(M_SPPP, "sppp", "synchronous PPP interface internals");

struct ppp_header {
	u_char address;
	u_char control;
	u_short protocol;
} __packed;
#define PPP_HEADER_LEN          sizeof (struct ppp_header)

struct lcp_header {
	u_char type;
	u_char ident;
	u_short len;
} __packed;
#define LCP_HEADER_LEN          sizeof (struct lcp_header)

struct cisco_packet {
	u_long type;
	u_long par1;
	u_long par2;
	u_short rel;
	u_short time0;
	u_short time1;
} __packed;
#define CISCO_PACKET_LEN	sizeof (struct cisco_packet)

/*
 * We follow the spelling and capitalization of RFC 1661 here, to make
 * it easier comparing with the standard.  Please refer to this RFC in
 * case you can't make sense out of these abbreviation; it will also
 * explain the semantics related to the various events and actions.
 */
struct cp {
	u_short	proto;		/* PPP control protocol number */
	u_char protoidx;	/* index into state table in struct sppp */
	u_char flags;
#define CP_LCP		0x01	/* this is the LCP */
#define CP_AUTH		0x02	/* this is an authentication protocol */
#define CP_NCP		0x04	/* this is a NCP */
#define CP_QUAL		0x08	/* this is a quality reporting protocol */
	const char *name;	/* name of this control protocol */
	/* event handlers */
	void	(*Up)(struct sppp *sp);
	void	(*Down)(struct sppp *sp);
	void	(*Open)(struct sppp *sp);
	void	(*Close)(struct sppp *sp);
	void	(*TO)(void *sp);
	int	(*RCR)(struct sppp *sp, struct lcp_header *h, int len);
	void	(*RCN_rej)(struct sppp *sp, struct lcp_header *h, int len);
	void	(*RCN_nak)(struct sppp *sp, struct lcp_header *h, int len);
	/* actions */
	void	(*tlu)(struct sppp *sp);
	void	(*tld)(struct sppp *sp);
	void	(*tls)(struct sppp *sp);
	void	(*tlf)(struct sppp *sp);
	void	(*scr)(struct sppp *sp);
};

#define	SPP_FMT		"%s: "
#define	SPP_ARGS(ifp)	(ifp)->if_xname

#define SPPP_LOCK(sp)	mtx_lock (&(sp)->mtx)
#define SPPP_UNLOCK(sp)	mtx_unlock (&(sp)->mtx)
#define SPPP_LOCK_ASSERT(sp)	mtx_assert (&(sp)->mtx, MA_OWNED)
#define SPPP_LOCK_OWNED(sp)	mtx_owned (&(sp)->mtx)

#ifdef INET
/*
 * The following disgusting hack gets around the problem that IP TOS
 * can't be set yet.  We want to put "interactive" traffic on a high
 * priority queue.  To decide if traffic is interactive, we check that
 * a) it is TCP and b) one of its ports is telnet, rlogin or ftp control.
 *
 * XXX is this really still necessary?  - joerg -
 */
static const u_short interactive_ports[8] = {
	0,	513,	0,	0,
	0,	21,	0,	23,
};
#define INTERACTIVE(p) (interactive_ports[(p) & 7] == (p))
#endif

/* almost every function needs these */
#define STDDCL							\
	struct ifnet *ifp = SP2IFP(sp);				\
	int debug = ifp->if_flags & IFF_DEBUG

static int sppp_output(struct ifnet *ifp, struct mbuf *m,
	const struct sockaddr *dst, struct route *ro);

static void sppp_cisco_send(struct sppp *sp, int type, long par1, long par2);
static void sppp_cisco_input(struct sppp *sp, struct mbuf *m);

static void sppp_cp_input(const struct cp *cp, struct sppp *sp,
			  struct mbuf *m);
static void sppp_cp_send(struct sppp *sp, u_short proto, u_char type,
			 u_char ident, u_short len, void *data);
/* static void sppp_cp_timeout(void *arg); */
static void sppp_cp_change_state(const struct cp *cp, struct sppp *sp,
				 int newstate);
static void sppp_auth_send(const struct cp *cp,
			   struct sppp *sp, unsigned int type, unsigned int id,
			   ...);

static void sppp_up_event(const struct cp *cp, struct sppp *sp);
static void sppp_down_event(const struct cp *cp, struct sppp *sp);
static void sppp_open_event(const struct cp *cp, struct sppp *sp);
static void sppp_close_event(const struct cp *cp, struct sppp *sp);
static void sppp_to_event(const struct cp *cp, struct sppp *sp);

static void sppp_null(struct sppp *sp);

static void sppp_pp_up(struct sppp *sp);
static void sppp_pp_down(struct sppp *sp);

static void sppp_lcp_init(struct sppp *sp);
static void sppp_lcp_up(struct sppp *sp);
static void sppp_lcp_down(struct sppp *sp);
static void sppp_lcp_open(struct sppp *sp);
static void sppp_lcp_close(struct sppp *sp);
static void sppp_lcp_TO(void *sp);
static int sppp_lcp_RCR(struct sppp *sp, struct lcp_header *h, int len);
static void sppp_lcp_RCN_rej(struct sppp *sp, struct lcp_header *h, int len);
static void sppp_lcp_RCN_nak(struct sppp *sp, struct lcp_header *h, int len);
static void sppp_lcp_tlu(struct sppp *sp);
static void sppp_lcp_tld(struct sppp *sp);
static void sppp_lcp_tls(struct sppp *sp);
static void sppp_lcp_tlf(struct sppp *sp);
static void sppp_lcp_scr(struct sppp *sp);
static void sppp_lcp_check_and_close(struct sppp *sp);
static int sppp_ncp_check(struct sppp *sp);

static void sppp_ipcp_init(struct sppp *sp);
static void sppp_ipcp_up(struct sppp *sp);
static void sppp_ipcp_down(struct sppp *sp);
static void sppp_ipcp_open(struct sppp *sp);
static void sppp_ipcp_close(struct sppp *sp);
static void sppp_ipcp_TO(void *sp);
static int sppp_ipcp_RCR(struct sppp *sp, struct lcp_header *h, int len);
static void sppp_ipcp_RCN_rej(struct sppp *sp, struct lcp_header *h, int len);
static void sppp_ipcp_RCN_nak(struct sppp *sp, struct lcp_header *h, int len);
static void sppp_ipcp_tlu(struct sppp *sp);
static void sppp_ipcp_tld(struct sppp *sp);
static void sppp_ipcp_tls(struct sppp *sp);
static void sppp_ipcp_tlf(struct sppp *sp);
static void sppp_ipcp_scr(struct sppp *sp);

static void sppp_ipv6cp_init(struct sppp *sp);
static void sppp_ipv6cp_up(struct sppp *sp);
static void sppp_ipv6cp_down(struct sppp *sp);
static void sppp_ipv6cp_open(struct sppp *sp);
static void sppp_ipv6cp_close(struct sppp *sp);
static void sppp_ipv6cp_TO(void *sp);
static int sppp_ipv6cp_RCR(struct sppp *sp, struct lcp_header *h, int len);
static void sppp_ipv6cp_RCN_rej(struct sppp *sp, struct lcp_header *h, int len);
static void sppp_ipv6cp_RCN_nak(struct sppp *sp, struct lcp_header *h, int len);
static void sppp_ipv6cp_tlu(struct sppp *sp);
static void sppp_ipv6cp_tld(struct sppp *sp);
static void sppp_ipv6cp_tls(struct sppp *sp);
static void sppp_ipv6cp_tlf(struct sppp *sp);
static void sppp_ipv6cp_scr(struct sppp *sp);

static void sppp_pap_input(struct sppp *sp, struct mbuf *m);
static void sppp_pap_init(struct sppp *sp);
static void sppp_pap_open(struct sppp *sp);
static void sppp_pap_close(struct sppp *sp);
static void sppp_pap_TO(void *sp);
static void sppp_pap_my_TO(void *sp);
static void sppp_pap_tlu(struct sppp *sp);
static void sppp_pap_tld(struct sppp *sp);
static void sppp_pap_scr(struct sppp *sp);

static void sppp_chap_input(struct sppp *sp, struct mbuf *m);
static void sppp_chap_init(struct sppp *sp);
static void sppp_chap_open(struct sppp *sp);
static void sppp_chap_close(struct sppp *sp);
static void sppp_chap_TO(void *sp);
static void sppp_chap_tlu(struct sppp *sp);
static void sppp_chap_tld(struct sppp *sp);
static void sppp_chap_scr(struct sppp *sp);

static const char *sppp_auth_type_name(u_short proto, u_char type);
static const char *sppp_cp_type_name(u_char type);
#ifdef INET
static const char *sppp_dotted_quad(u_long addr);
static const char *sppp_ipcp_opt_name(u_char opt);
#endif
#ifdef INET6
static const char *sppp_ipv6cp_opt_name(u_char opt);
#endif
static const char *sppp_lcp_opt_name(u_char opt);
static const char *sppp_phase_name(enum ppp_phase phase);
static const char *sppp_proto_name(u_short proto);
static const char *sppp_state_name(int state);
static int sppp_params(struct sppp *sp, u_long cmd, void *data);
static int sppp_strnlen(u_char *p, int max);
static void sppp_keepalive(void *dummy);
static void sppp_phase_network(struct sppp *sp);
static void sppp_print_bytes(const u_char *p, u_short len);
static void sppp_print_string(const char *p, u_short len);
static void sppp_qflush(struct ifqueue *ifq);
#ifdef INET
static void sppp_set_ip_addr(struct sppp *sp, u_long src);
#endif
#ifdef INET6
static void sppp_get_ip6_addrs(struct sppp *sp, struct in6_addr *src,
			       struct in6_addr *dst, struct in6_addr *srcmask);
#ifdef IPV6CP_MYIFID_DYN
static void sppp_set_ip6_addr(struct sppp *sp, const struct in6_addr *src);
static void sppp_gen_ip6_addr(struct sppp *sp, const struct in6_addr *src);
#endif
static void sppp_suggest_ip6_addr(struct sppp *sp, struct in6_addr *src);
#endif

/* if_start () wrapper */
static void sppp_ifstart (struct ifnet *ifp);

/* our control protocol descriptors */
static const struct cp lcp = {
	PPP_LCP, IDX_LCP, CP_LCP, "lcp",
	sppp_lcp_up, sppp_lcp_down, sppp_lcp_open, sppp_lcp_close,
	sppp_lcp_TO, sppp_lcp_RCR, sppp_lcp_RCN_rej, sppp_lcp_RCN_nak,
	sppp_lcp_tlu, sppp_lcp_tld, sppp_lcp_tls, sppp_lcp_tlf,
	sppp_lcp_scr
};

static const struct cp ipcp = {
	PPP_IPCP, IDX_IPCP,
#ifdef INET	/* don't run IPCP if there's no IPv4 support */
	CP_NCP,
#else
	0,
#endif
	"ipcp",
	sppp_ipcp_up, sppp_ipcp_down, sppp_ipcp_open, sppp_ipcp_close,
	sppp_ipcp_TO, sppp_ipcp_RCR, sppp_ipcp_RCN_rej, sppp_ipcp_RCN_nak,
	sppp_ipcp_tlu, sppp_ipcp_tld, sppp_ipcp_tls, sppp_ipcp_tlf,
	sppp_ipcp_scr
};

static const struct cp ipv6cp = {
	PPP_IPV6CP, IDX_IPV6CP,
#ifdef INET6	/*don't run IPv6CP if there's no IPv6 support*/
	CP_NCP,
#else
	0,
#endif
	"ipv6cp",
	sppp_ipv6cp_up, sppp_ipv6cp_down, sppp_ipv6cp_open, sppp_ipv6cp_close,
	sppp_ipv6cp_TO, sppp_ipv6cp_RCR, sppp_ipv6cp_RCN_rej, sppp_ipv6cp_RCN_nak,
	sppp_ipv6cp_tlu, sppp_ipv6cp_tld, sppp_ipv6cp_tls, sppp_ipv6cp_tlf,
	sppp_ipv6cp_scr
};

static const struct cp pap = {
	PPP_PAP, IDX_PAP, CP_AUTH, "pap",
	sppp_null, sppp_null, sppp_pap_open, sppp_pap_close,
	sppp_pap_TO, 0, 0, 0,
	sppp_pap_tlu, sppp_pap_tld, sppp_null, sppp_null,
	sppp_pap_scr
};

static const struct cp chap = {
	PPP_CHAP, IDX_CHAP, CP_AUTH, "chap",
	sppp_null, sppp_null, sppp_chap_open, sppp_chap_close,
	sppp_chap_TO, 0, 0, 0,
	sppp_chap_tlu, sppp_chap_tld, sppp_null, sppp_null,
	sppp_chap_scr
};

static const struct cp *cps[IDX_COUNT] = {
	&lcp,			/* IDX_LCP */
	&ipcp,			/* IDX_IPCP */
	&ipv6cp,		/* IDX_IPV6CP */
	&pap,			/* IDX_PAP */
	&chap,			/* IDX_CHAP */
};

static void*
sppp_alloc(u_char type, struct ifnet *ifp)
{
	struct sppp	*sp;

        sp = malloc(sizeof(struct sppp), M_SPPP, M_WAITOK | M_ZERO);
	sp->pp_ifp = ifp;

	return (sp);
}

static void
sppp_free(void *com, u_char type)
{

	free(com, M_SPPP);
}

static int
sppp_modevent(module_t mod, int type, void *unused)
{
	switch (type) {
	case MOD_LOAD:
		/*
		 * XXX: should probably be IFT_SPPP, but it's fairly
		 * harmless to allocate struct sppp's for non-sppp
		 * interfaces.
		 */

		if_register_com_alloc(IFT_PPP, sppp_alloc, sppp_free);
		break;
	case MOD_UNLOAD:
		/* if_deregister_com_alloc(IFT_PPP); */
		return EACCES;
	default:
		return EOPNOTSUPP;
	}
	return 0;
}
static moduledata_t spppmod = {
	"sppp",
	sppp_modevent,
	0
};
MODULE_VERSION(sppp, 1);
DECLARE_MODULE(sppp, spppmod, SI_SUB_DRIVERS, SI_ORDER_ANY);

/*
 * Exported functions, comprising our interface to the lower layer.
 */

/*
 * Process the received packet.
 */
void
sppp_input(struct ifnet *ifp, struct mbuf *m)
{
	struct ppp_header *h;
	int isr = -1;
	struct sppp *sp = IFP2SP(ifp);
	int debug, do_account = 0;
#ifdef INET
	int hlen, vjlen;
	u_char *iphdr;
#endif

	SPPP_LOCK(sp);
	debug = ifp->if_flags & IFF_DEBUG;

	if (ifp->if_flags & IFF_UP)
		/* Count received bytes, add FCS and one flag */
		if_inc_counter(ifp, IFCOUNTER_IBYTES, m->m_pkthdr.len + 3);

	if (m->m_pkthdr.len <= PPP_HEADER_LEN) {
		/* Too small packet, drop it. */
		if (debug)
			log(LOG_DEBUG,
			    SPP_FMT "input packet is too small, %d bytes\n",
			    SPP_ARGS(ifp), m->m_pkthdr.len);
	  drop:
		m_freem (m);
		SPPP_UNLOCK(sp);
	  drop2:
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
		return;
	}

	if (sp->pp_mode == PP_FR) {
		sppp_fr_input (sp, m);
		SPPP_UNLOCK(sp);
		return;
	}

	/* Get PPP header. */
	h = mtod (m, struct ppp_header*);
	m_adj (m, PPP_HEADER_LEN);

	switch (h->address) {
	case PPP_ALLSTATIONS:
		if (h->control != PPP_UI)
			goto invalid;
		if (sp->pp_mode == IFF_CISCO) {
			if (debug)
				log(LOG_DEBUG,
				    SPP_FMT "PPP packet in Cisco mode "
				    "<addr=0x%x ctrl=0x%x proto=0x%x>\n",
				    SPP_ARGS(ifp),
				    h->address, h->control, ntohs(h->protocol));
			goto drop;
		}
		switch (ntohs (h->protocol)) {
		default:
			if (debug)
				log(LOG_DEBUG,
				    SPP_FMT "rejecting protocol "
				    "<addr=0x%x ctrl=0x%x proto=0x%x>\n",
				    SPP_ARGS(ifp),
				    h->address, h->control, ntohs(h->protocol));
			if (sp->state[IDX_LCP] == STATE_OPENED)
				sppp_cp_send (sp, PPP_LCP, PROTO_REJ,
					++sp->pp_seq[IDX_LCP], m->m_pkthdr.len + 2,
					&h->protocol);
			if_inc_counter(ifp, IFCOUNTER_NOPROTO, 1);
			goto drop;
		case PPP_LCP:
			sppp_cp_input(&lcp, sp, m);
			m_freem (m);
			SPPP_UNLOCK(sp);
			return;
		case PPP_PAP:
			if (sp->pp_phase >= PHASE_AUTHENTICATE)
				sppp_pap_input(sp, m);
			m_freem (m);
			SPPP_UNLOCK(sp);
			return;
		case PPP_CHAP:
			if (sp->pp_phase >= PHASE_AUTHENTICATE)
				sppp_chap_input(sp, m);
			m_freem (m);
			SPPP_UNLOCK(sp);
			return;
#ifdef INET
		case PPP_IPCP:
			if (sp->pp_phase == PHASE_NETWORK)
				sppp_cp_input(&ipcp, sp, m);
			m_freem (m);
			SPPP_UNLOCK(sp);
			return;
		case PPP_IP:
			if (sp->state[IDX_IPCP] == STATE_OPENED) {
				isr = NETISR_IP;
			}
			do_account++;
			break;
		case PPP_VJ_COMP:
			if (sp->state[IDX_IPCP] == STATE_OPENED) {
				if ((vjlen =
				     sl_uncompress_tcp_core(mtod(m, u_char *),
							    m->m_len, m->m_len,
							    TYPE_COMPRESSED_TCP,
							    sp->pp_comp,
							    &iphdr, &hlen)) <= 0) {
					if (debug)
						log(LOG_INFO,
			    SPP_FMT "VJ uncompress failed on compressed packet\n",
						    SPP_ARGS(ifp));
					goto drop;
				}

				/*
				 * Trim the VJ header off the packet, and prepend
				 * the uncompressed IP header (which will usually
				 * end up in two chained mbufs since there's not
				 * enough leading space in the existing mbuf).
				 */
				m_adj(m, vjlen);
				M_PREPEND(m, hlen, M_NOWAIT);
				if (m == NULL) {
					SPPP_UNLOCK(sp);
					goto drop2;
				}
				bcopy(iphdr, mtod(m, u_char *), hlen);
				isr = NETISR_IP;
			}
			do_account++;
			break;
		case PPP_VJ_UCOMP:
			if (sp->state[IDX_IPCP] == STATE_OPENED) {
				if (sl_uncompress_tcp_core(mtod(m, u_char *),
							   m->m_len, m->m_len,
							   TYPE_UNCOMPRESSED_TCP,
							   sp->pp_comp,
							   &iphdr, &hlen) != 0) {
					if (debug)
						log(LOG_INFO,
			    SPP_FMT "VJ uncompress failed on uncompressed packet\n",
						    SPP_ARGS(ifp));
					goto drop;
				}
				isr = NETISR_IP;
			}
			do_account++;
			break;
#endif
#ifdef INET6
		case PPP_IPV6CP:
			if (sp->pp_phase == PHASE_NETWORK)
			    sppp_cp_input(&ipv6cp, sp, m);
			m_freem (m);
			SPPP_UNLOCK(sp);
			return;

		case PPP_IPV6:
			if (sp->state[IDX_IPV6CP] == STATE_OPENED)
				isr = NETISR_IPV6;
			do_account++;
			break;
#endif
		}
		break;
	case CISCO_MULTICAST:
	case CISCO_UNICAST:
		/* Don't check the control field here (RFC 1547). */
		if (sp->pp_mode != IFF_CISCO) {
			if (debug)
				log(LOG_DEBUG,
				    SPP_FMT "Cisco packet in PPP mode "
				    "<addr=0x%x ctrl=0x%x proto=0x%x>\n",
				    SPP_ARGS(ifp),
				    h->address, h->control, ntohs(h->protocol));
			goto drop;
		}
		switch (ntohs (h->protocol)) {
		default:
			if_inc_counter(ifp, IFCOUNTER_NOPROTO, 1);
			goto invalid;
		case CISCO_KEEPALIVE:
			sppp_cisco_input (sp, m);
			m_freem (m);
			SPPP_UNLOCK(sp);
			return;
#ifdef INET
		case ETHERTYPE_IP:
			isr = NETISR_IP;
			do_account++;
			break;
#endif
#ifdef INET6
		case ETHERTYPE_IPV6:
			isr = NETISR_IPV6;
			do_account++;
			break;
#endif
		}
		break;
	default:        /* Invalid PPP packet. */
	  invalid:
		if (debug)
			log(LOG_DEBUG,
			    SPP_FMT "invalid input packet "
			    "<addr=0x%x ctrl=0x%x proto=0x%x>\n",
			    SPP_ARGS(ifp),
			    h->address, h->control, ntohs(h->protocol));
		goto drop;
	}

	if (! (ifp->if_flags & IFF_UP) || isr == -1)
		goto drop;

	SPPP_UNLOCK(sp);
	M_SETFIB(m, ifp->if_fib);
	/* Check queue. */
	if (netisr_queue(isr, m)) {	/* (0) on success. */
		if (debug)
			log(LOG_DEBUG, SPP_FMT "protocol queue overflow\n",
				SPP_ARGS(ifp));
		goto drop2;
	}

	if (do_account)
		/*
		 * Do only account for network packets, not for control
		 * packets.  This is used by some subsystems to detect
		 * idle lines.
		 */
		sp->pp_last_recv = time_uptime;
}

static void
sppp_ifstart_sched(void *dummy)
{
	struct sppp *sp = dummy;
	
	sp->if_start(SP2IFP(sp));
}

/* if_start () wrapper function. We use it to schedule real if_start () for
 * execution. We can't call it directly
 */
static void
sppp_ifstart(struct ifnet *ifp)
{
	struct sppp *sp = IFP2SP(ifp);

	if (SPPP_LOCK_OWNED(sp)) {
		if (callout_pending(&sp->ifstart_callout))
			return;
		callout_reset(&sp->ifstart_callout, 1, sppp_ifstart_sched,
		    (void *)sp); 
	} else {
		sp->if_start(ifp);
	}
}

/*
 * Enqueue transmit packet.
 */
static int
sppp_output(struct ifnet *ifp, struct mbuf *m, const struct sockaddr *dst,
	struct route *ro)
{
	struct sppp *sp = IFP2SP(ifp);
	struct ppp_header *h;
	struct ifqueue *ifq = NULL;
	int error, rv = 0;
#ifdef INET
	int ipproto = PPP_IP;
#endif
	int debug = ifp->if_flags & IFF_DEBUG;

	SPPP_LOCK(sp);

	if (!(ifp->if_flags & IFF_UP) ||
	    (!(ifp->if_flags & IFF_AUTO) &&
	    !(ifp->if_drv_flags & IFF_DRV_RUNNING))) {
#ifdef INET6
	  drop:
#endif
		m_freem (m);
		SPPP_UNLOCK(sp);
		return (ENETDOWN);
	}

	if ((ifp->if_flags & IFF_AUTO) &&
	    !(ifp->if_drv_flags & IFF_DRV_RUNNING)) {
#ifdef INET6
		/*
		 * XXX
		 *
		 * Hack to prevent the initialization-time generated
		 * IPv6 multicast packet to erroneously cause a
		 * dialout event in case IPv6 has been
		 * administratively disabled on that interface.
		 */
		if (dst->sa_family == AF_INET6 &&
		    !(sp->confflags & CONF_ENABLE_IPV6))
			goto drop;
#endif
		/*
		 * Interface is not yet running, but auto-dial.  Need
		 * to start LCP for it.
		 */
		ifp->if_drv_flags |= IFF_DRV_RUNNING;
		lcp.Open(sp);
	}

#ifdef INET
	if (dst->sa_family == AF_INET) {
		/* XXX Check mbuf length here? */
		struct ip *ip = mtod (m, struct ip*);
		struct tcphdr *tcp = (struct tcphdr*) ((long*)ip + ip->ip_hl);

		/*
		 * When using dynamic local IP address assignment by using
		 * 0.0.0.0 as a local address, the first TCP session will
		 * not connect because the local TCP checksum is computed
		 * using 0.0.0.0 which will later become our real IP address
		 * so the TCP checksum computed at the remote end will
		 * become invalid. So we
		 * - don't let packets with src ip addr 0 thru
		 * - we flag TCP packets with src ip 0 as an error
		 */

		if(ip->ip_src.s_addr == INADDR_ANY)	/* -hm */
		{
			m_freem(m);
			SPPP_UNLOCK(sp);
			if(ip->ip_p == IPPROTO_TCP)
				return(EADDRNOTAVAIL);
			else
				return(0);
		}

		/*
		 * Put low delay, telnet, rlogin and ftp control packets
		 * in front of the queue or let ALTQ take care.
		 */
		if (ALTQ_IS_ENABLED(&ifp->if_snd))
			;
		else if (_IF_QFULL(&sp->pp_fastq))
			;
		else if (ip->ip_tos & IPTOS_LOWDELAY)
			ifq = &sp->pp_fastq;
		else if (m->m_len < sizeof *ip + sizeof *tcp)
			;
		else if (ip->ip_p != IPPROTO_TCP)
			;
		else if (INTERACTIVE (ntohs (tcp->th_sport)))
			ifq = &sp->pp_fastq;
		else if (INTERACTIVE (ntohs (tcp->th_dport)))
			ifq = &sp->pp_fastq;

		/*
		 * Do IP Header compression
		 */
		if (sp->pp_mode != IFF_CISCO && sp->pp_mode != PP_FR &&
		    (sp->ipcp.flags & IPCP_VJ) && ip->ip_p == IPPROTO_TCP)
			switch (sl_compress_tcp(m, ip, sp->pp_comp,
						sp->ipcp.compress_cid)) {
			case TYPE_COMPRESSED_TCP:
				ipproto = PPP_VJ_COMP;
				break;
			case TYPE_UNCOMPRESSED_TCP:
				ipproto = PPP_VJ_UCOMP;
				break;
			case TYPE_IP:
				ipproto = PPP_IP;
				break;
			default:
				m_freem(m);
				SPPP_UNLOCK(sp);
				return (EINVAL);
			}
	}
#endif

#ifdef INET6
	if (dst->sa_family == AF_INET6) {
		/* XXX do something tricky here? */
	}
#endif

	if (sp->pp_mode == PP_FR) {
		/* Add frame relay header. */
		m = sppp_fr_header (sp, m, dst->sa_family);
		if (! m)
			goto nobufs;
		goto out;
	}

	/*
	 * Prepend general data packet PPP header. For now, IP only.
	 */
	M_PREPEND (m, PPP_HEADER_LEN, M_NOWAIT);
	if (! m) {
nobufs:		if (debug)
			log(LOG_DEBUG, SPP_FMT "no memory for transmit header\n",
				SPP_ARGS(ifp));
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		SPPP_UNLOCK(sp);
		return (ENOBUFS);
	}
	/*
	 * May want to check size of packet
	 * (albeit due to the implementation it's always enough)
	 */
	h = mtod (m, struct ppp_header*);
	if (sp->pp_mode == IFF_CISCO) {
		h->address = CISCO_UNICAST;        /* unicast address */
		h->control = 0;
	} else {
		h->address = PPP_ALLSTATIONS;        /* broadcast address */
		h->control = PPP_UI;                 /* Unnumbered Info */
	}

	switch (dst->sa_family) {
#ifdef INET
	case AF_INET:   /* Internet Protocol */
		if (sp->pp_mode == IFF_CISCO)
			h->protocol = htons (ETHERTYPE_IP);
		else {
			/*
			 * Don't choke with an ENETDOWN early.  It's
			 * possible that we just started dialing out,
			 * so don't drop the packet immediately.  If
			 * we notice that we run out of buffer space
			 * below, we will however remember that we are
			 * not ready to carry IP packets, and return
			 * ENETDOWN, as opposed to ENOBUFS.
			 */
			h->protocol = htons(ipproto);
			if (sp->state[IDX_IPCP] != STATE_OPENED)
				rv = ENETDOWN;
		}
		break;
#endif
#ifdef INET6
	case AF_INET6:   /* Internet Protocol */
		if (sp->pp_mode == IFF_CISCO)
			h->protocol = htons (ETHERTYPE_IPV6);
		else {
			/*
			 * Don't choke with an ENETDOWN early.  It's
			 * possible that we just started dialing out,
			 * so don't drop the packet immediately.  If
			 * we notice that we run out of buffer space
			 * below, we will however remember that we are
			 * not ready to carry IP packets, and return
			 * ENETDOWN, as opposed to ENOBUFS.
			 */
			h->protocol = htons(PPP_IPV6);
			if (sp->state[IDX_IPV6CP] != STATE_OPENED)
				rv = ENETDOWN;
		}
		break;
#endif
	default:
		m_freem (m);
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		SPPP_UNLOCK(sp);
		return (EAFNOSUPPORT);
	}

	/*
	 * Queue message on interface, and start output if interface
	 * not yet active.
	 */
out:
	if (ifq != NULL)
		error = !(IF_HANDOFF_ADJ(ifq, m, ifp, 3));
	else
		IFQ_HANDOFF_ADJ(ifp, m, 3, error);
	if (error) {
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		SPPP_UNLOCK(sp);
		return (rv? rv: ENOBUFS);
	}
	SPPP_UNLOCK(sp);
	/*
	 * Unlike in sppp_input(), we can always bump the timestamp
	 * here since sppp_output() is only called on behalf of
	 * network-layer traffic; control-layer traffic is handled
	 * by sppp_cp_send().
	 */
	sp->pp_last_sent = time_uptime;
	return (0);
}

void
sppp_attach(struct ifnet *ifp)
{
	struct sppp *sp = IFP2SP(ifp);

	/* Initialize mtx lock */
	mtx_init(&sp->mtx, "sppp", MTX_NETWORK_LOCK, MTX_DEF | MTX_RECURSE);
	
	/* Initialize keepalive handler. */
 	callout_init(&sp->keepalive_callout, 1);
	callout_reset(&sp->keepalive_callout, hz * 10, sppp_keepalive,
 		    (void *)sp); 

	ifp->if_mtu = PP_MTU;
	ifp->if_flags = IFF_POINTOPOINT | IFF_MULTICAST;
	ifp->if_output = sppp_output;
#if 0
	sp->pp_flags = PP_KEEPALIVE;
#endif
 	ifp->if_snd.ifq_maxlen = 32;
 	sp->pp_fastq.ifq_maxlen = 32;
 	sp->pp_cpq.ifq_maxlen = 20;
	sp->pp_loopcnt = 0;
	sp->pp_alivecnt = 0;
	bzero(&sp->pp_seq[0], sizeof(sp->pp_seq));
	bzero(&sp->pp_rseq[0], sizeof(sp->pp_rseq));
	sp->pp_phase = PHASE_DEAD;
	sp->pp_up = sppp_pp_up;
	sp->pp_down = sppp_pp_down;
	if(!mtx_initialized(&sp->pp_cpq.ifq_mtx))
		mtx_init(&sp->pp_cpq.ifq_mtx, "sppp_cpq", NULL, MTX_DEF);
	if(!mtx_initialized(&sp->pp_fastq.ifq_mtx))
		mtx_init(&sp->pp_fastq.ifq_mtx, "sppp_fastq", NULL, MTX_DEF);
	sp->pp_last_recv = sp->pp_last_sent = time_uptime;
	sp->confflags = 0;
#ifdef INET
	sp->confflags |= CONF_ENABLE_VJ;
#endif
#ifdef INET6
	sp->confflags |= CONF_ENABLE_IPV6;
#endif
 	callout_init(&sp->ifstart_callout, 1);
	sp->if_start = ifp->if_start;
	ifp->if_start = sppp_ifstart;
	sp->pp_comp = malloc(sizeof(struct slcompress), M_TEMP, M_WAITOK);
	sl_compress_init(sp->pp_comp, -1);
	sppp_lcp_init(sp);
	sppp_ipcp_init(sp);
	sppp_ipv6cp_init(sp);
	sppp_pap_init(sp);
	sppp_chap_init(sp);
}

void
sppp_detach(struct ifnet *ifp)
{
	struct sppp *sp = IFP2SP(ifp);
	int i;

	KASSERT(mtx_initialized(&sp->mtx), ("sppp mutex is not initialized"));

	/* Stop keepalive handler. */
 	if (!callout_drain(&sp->keepalive_callout))
		callout_stop(&sp->keepalive_callout);

	for (i = 0; i < IDX_COUNT; i++) {
		if (!callout_drain(&sp->ch[i]))
			callout_stop(&sp->ch[i]);
	}
	if (!callout_drain(&sp->pap_my_to_ch))
		callout_stop(&sp->pap_my_to_ch);
	mtx_destroy(&sp->pp_cpq.ifq_mtx);
	mtx_destroy(&sp->pp_fastq.ifq_mtx);
	mtx_destroy(&sp->mtx);
}

/*
 * Flush the interface output queue.
 */
static void
sppp_flush_unlocked(struct ifnet *ifp)
{
	struct sppp *sp = IFP2SP(ifp);

	sppp_qflush ((struct ifqueue *)&SP2IFP(sp)->if_snd);
	sppp_qflush (&sp->pp_fastq);
	sppp_qflush (&sp->pp_cpq);
}

void
sppp_flush(struct ifnet *ifp)
{
	struct sppp *sp = IFP2SP(ifp);

	SPPP_LOCK(sp);
	sppp_flush_unlocked (ifp);
	SPPP_UNLOCK(sp);
}

/*
 * Check if the output queue is empty.
 */
int
sppp_isempty(struct ifnet *ifp)
{
	struct sppp *sp = IFP2SP(ifp);
	int empty;

	SPPP_LOCK(sp);
	empty = !sp->pp_fastq.ifq_head && !sp->pp_cpq.ifq_head &&
		!SP2IFP(sp)->if_snd.ifq_head;
	SPPP_UNLOCK(sp);
	return (empty);
}

/*
 * Get next packet to send.
 */
struct mbuf *
sppp_dequeue(struct ifnet *ifp)
{
	struct sppp *sp = IFP2SP(ifp);
	struct mbuf *m;

	SPPP_LOCK(sp);
	/*
	 * Process only the control protocol queue until we have at
	 * least one NCP open.
	 *
	 * Do always serve all three queues in Cisco mode.
	 */
	IF_DEQUEUE(&sp->pp_cpq, m);
	if (m == NULL &&
	    (sppp_ncp_check(sp) || sp->pp_mode == IFF_CISCO ||
	     sp->pp_mode == PP_FR)) {
		IF_DEQUEUE(&sp->pp_fastq, m);
		if (m == NULL)
			IF_DEQUEUE (&SP2IFP(sp)->if_snd, m);
	}
	SPPP_UNLOCK(sp);
	return m;
}

/*
 * Pick the next packet, do not remove it from the queue.
 */
struct mbuf *
sppp_pick(struct ifnet *ifp)
{
	struct sppp *sp = IFP2SP(ifp);
	struct mbuf *m;

	SPPP_LOCK(sp);

	m = sp->pp_cpq.ifq_head;
	if (m == NULL &&
	    (sp->pp_phase == PHASE_NETWORK ||
	     sp->pp_mode == IFF_CISCO ||
	     sp->pp_mode == PP_FR))
		if ((m = sp->pp_fastq.ifq_head) == NULL)
			m = SP2IFP(sp)->if_snd.ifq_head;
	SPPP_UNLOCK(sp);
	return (m);
}

/*
 * Process an ioctl request.  Called on low priority level.
 */
int
sppp_ioctl(struct ifnet *ifp, IOCTL_CMD_T cmd, void *data)
{
	struct ifreq *ifr = (struct ifreq*) data;
	struct sppp *sp = IFP2SP(ifp);
	int rv, going_up, going_down, newmode;

	SPPP_LOCK(sp);
	rv = 0;
	switch (cmd) {
	case SIOCAIFADDR:
		break;

	case SIOCSIFADDR:
		/* set the interface "up" when assigning an IP address */
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */

	case SIOCSIFFLAGS:
		going_up = ifp->if_flags & IFF_UP &&
			(ifp->if_drv_flags & IFF_DRV_RUNNING) == 0;
		going_down = (ifp->if_flags & IFF_UP) == 0 &&
			ifp->if_drv_flags & IFF_DRV_RUNNING;

		newmode = ifp->if_flags & IFF_PASSIVE;
		if (!newmode)
			newmode = ifp->if_flags & IFF_AUTO;
		if (!newmode)
			newmode = ifp->if_flags & IFF_CISCO;
		ifp->if_flags &= ~(IFF_PASSIVE | IFF_AUTO | IFF_CISCO);
		ifp->if_flags |= newmode;

		if (!newmode)
			newmode = sp->pp_flags & PP_FR;

		if (newmode != sp->pp_mode) {
			going_down = 1;
			if (!going_up)
				going_up = ifp->if_drv_flags & IFF_DRV_RUNNING;
		}

		if (going_down) {
			if (sp->pp_mode != IFF_CISCO &&
			    sp->pp_mode != PP_FR)
				lcp.Close(sp);
			else if (sp->pp_tlf)
				(sp->pp_tlf)(sp);
			sppp_flush_unlocked(ifp);
			ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			sp->pp_mode = newmode;
		}

		if (going_up) {
			if (sp->pp_mode != IFF_CISCO &&
			    sp->pp_mode != PP_FR)
				lcp.Close(sp);
			sp->pp_mode = newmode;
			if (sp->pp_mode == 0) {
				ifp->if_drv_flags |= IFF_DRV_RUNNING;
				lcp.Open(sp);
			}
			if ((sp->pp_mode == IFF_CISCO) ||
			    (sp->pp_mode == PP_FR)) {
				if (sp->pp_tls)
					(sp->pp_tls)(sp);
				ifp->if_drv_flags |= IFF_DRV_RUNNING;
			}
		}

		break;

#ifdef SIOCSIFMTU
#ifndef ifr_mtu
#define ifr_mtu ifr_metric
#endif
	case SIOCSIFMTU:
		if (ifr->ifr_mtu < 128 || ifr->ifr_mtu > sp->lcp.their_mru)
			return (EINVAL);
		ifp->if_mtu = ifr->ifr_mtu;
		break;
#endif
#ifdef SLIOCSETMTU
	case SLIOCSETMTU:
		if (*(short*)data < 128 || *(short*)data > sp->lcp.their_mru)
			return (EINVAL);
		ifp->if_mtu = *(short*)data;
		break;
#endif
#ifdef SIOCGIFMTU
	case SIOCGIFMTU:
		ifr->ifr_mtu = ifp->if_mtu;
		break;
#endif
#ifdef SLIOCGETMTU
	case SLIOCGETMTU:
		*(short*)data = ifp->if_mtu;
		break;
#endif
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;

	case SIOCGIFGENERIC:
	case SIOCSIFGENERIC:
		rv = sppp_params(sp, cmd, data);
		break;

	default:
		rv = ENOTTY;
	}
	SPPP_UNLOCK(sp);
	return rv;
}

/*
 * Cisco framing implementation.
 */

/*
 * Handle incoming Cisco keepalive protocol packets.
 */
static void
sppp_cisco_input(struct sppp *sp, struct mbuf *m)
{
	STDDCL;
	struct cisco_packet *h;
	u_long me, mymask;

	if (m->m_pkthdr.len < CISCO_PACKET_LEN) {
		if (debug)
			log(LOG_DEBUG,
			    SPP_FMT "cisco invalid packet length: %d bytes\n",
			    SPP_ARGS(ifp), m->m_pkthdr.len);
		return;
	}
	h = mtod (m, struct cisco_packet*);
	if (debug)
		log(LOG_DEBUG,
		    SPP_FMT "cisco input: %d bytes "
		    "<0x%lx 0x%lx 0x%lx 0x%x 0x%x-0x%x>\n",
		    SPP_ARGS(ifp), m->m_pkthdr.len,
		    (u_long)ntohl (h->type), (u_long)h->par1, (u_long)h->par2, (u_int)h->rel,
		    (u_int)h->time0, (u_int)h->time1);
	switch (ntohl (h->type)) {
	default:
		if (debug)
			log(-1, SPP_FMT "cisco unknown packet type: 0x%lx\n",
			       SPP_ARGS(ifp), (u_long)ntohl (h->type));
		break;
	case CISCO_ADDR_REPLY:
		/* Reply on address request, ignore */
		break;
	case CISCO_KEEPALIVE_REQ:
		sp->pp_alivecnt = 0;
		sp->pp_rseq[IDX_LCP] = ntohl (h->par1);
		if (sp->pp_seq[IDX_LCP] == sp->pp_rseq[IDX_LCP]) {
			/* Local and remote sequence numbers are equal.
			 * Probably, the line is in loopback mode. */
			if (sp->pp_loopcnt >= MAXALIVECNT) {
				printf (SPP_FMT "loopback\n",
					SPP_ARGS(ifp));
				sp->pp_loopcnt = 0;
				if (ifp->if_flags & IFF_UP) {
					if_down (ifp);
					sppp_qflush (&sp->pp_cpq);
				}
			}
			++sp->pp_loopcnt;

			/* Generate new local sequence number */
			sp->pp_seq[IDX_LCP] = random();
			break;
		}
		sp->pp_loopcnt = 0;
		if (! (ifp->if_flags & IFF_UP) &&
		    (ifp->if_drv_flags & IFF_DRV_RUNNING)) {
			if_up(ifp);
			printf (SPP_FMT "up\n", SPP_ARGS(ifp));
		}
		break;
	case CISCO_ADDR_REQ:
		sppp_get_ip_addrs(sp, &me, 0, &mymask);
		if (me != 0L)
			sppp_cisco_send(sp, CISCO_ADDR_REPLY, me, mymask);
		break;
	}
}

/*
 * Send Cisco keepalive packet.
 */
static void
sppp_cisco_send(struct sppp *sp, int type, long par1, long par2)
{
	STDDCL;
	struct ppp_header *h;
	struct cisco_packet *ch;
	struct mbuf *m;
	struct timeval tv;

	getmicrouptime(&tv);

	MGETHDR (m, M_NOWAIT, MT_DATA);
	if (! m)
		return;
	m->m_pkthdr.len = m->m_len = PPP_HEADER_LEN + CISCO_PACKET_LEN;
	m->m_pkthdr.rcvif = 0;

	h = mtod (m, struct ppp_header*);
	h->address = CISCO_MULTICAST;
	h->control = 0;
	h->protocol = htons (CISCO_KEEPALIVE);

	ch = (struct cisco_packet*) (h + 1);
	ch->type = htonl (type);
	ch->par1 = htonl (par1);
	ch->par2 = htonl (par2);
	ch->rel = -1;

	ch->time0 = htons ((u_short) (tv.tv_sec >> 16));
	ch->time1 = htons ((u_short) tv.tv_sec);

	if (debug)
		log(LOG_DEBUG,
		    SPP_FMT "cisco output: <0x%lx 0x%lx 0x%lx 0x%x 0x%x-0x%x>\n",
			SPP_ARGS(ifp), (u_long)ntohl (ch->type), (u_long)ch->par1,
			(u_long)ch->par2, (u_int)ch->rel, (u_int)ch->time0, (u_int)ch->time1);

	if (! IF_HANDOFF_ADJ(&sp->pp_cpq, m, ifp, 3))
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
}

/*
 * PPP protocol implementation.
 */

/*
 * Send PPP control protocol packet.
 */
static void
sppp_cp_send(struct sppp *sp, u_short proto, u_char type,
	     u_char ident, u_short len, void *data)
{
	STDDCL;
	struct ppp_header *h;
	struct lcp_header *lh;
	struct mbuf *m;

	if (len > MHLEN - PPP_HEADER_LEN - LCP_HEADER_LEN)
		len = MHLEN - PPP_HEADER_LEN - LCP_HEADER_LEN;
	MGETHDR (m, M_NOWAIT, MT_DATA);
	if (! m)
		return;
	m->m_pkthdr.len = m->m_len = PPP_HEADER_LEN + LCP_HEADER_LEN + len;
	m->m_pkthdr.rcvif = 0;

	h = mtod (m, struct ppp_header*);
	h->address = PPP_ALLSTATIONS;        /* broadcast address */
	h->control = PPP_UI;                 /* Unnumbered Info */
	h->protocol = htons (proto);         /* Link Control Protocol */

	lh = (struct lcp_header*) (h + 1);
	lh->type = type;
	lh->ident = ident;
	lh->len = htons (LCP_HEADER_LEN + len);
	if (len)
		bcopy (data, lh+1, len);

	if (debug) {
		log(LOG_DEBUG, SPP_FMT "%s output <%s id=0x%x len=%d",
		    SPP_ARGS(ifp),
		    sppp_proto_name(proto),
		    sppp_cp_type_name (lh->type), lh->ident,
		    ntohs (lh->len));
		sppp_print_bytes ((u_char*) (lh+1), len);
		log(-1, ">\n");
	}
	if (! IF_HANDOFF_ADJ(&sp->pp_cpq, m, ifp, 3))
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
}

/*
 * Handle incoming PPP control protocol packets.
 */
static void
sppp_cp_input(const struct cp *cp, struct sppp *sp, struct mbuf *m)
{
	STDDCL;
	struct lcp_header *h;
	int len = m->m_pkthdr.len;
	int rv;
	u_char *p;

	if (len < 4) {
		if (debug)
			log(LOG_DEBUG,
			    SPP_FMT "%s invalid packet length: %d bytes\n",
			    SPP_ARGS(ifp), cp->name, len);
		return;
	}
	h = mtod (m, struct lcp_header*);
	if (debug) {
		log(LOG_DEBUG,
		    SPP_FMT "%s input(%s): <%s id=0x%x len=%d",
		    SPP_ARGS(ifp), cp->name,
		    sppp_state_name(sp->state[cp->protoidx]),
		    sppp_cp_type_name (h->type), h->ident, ntohs (h->len));
		sppp_print_bytes ((u_char*) (h+1), len-4);
		log(-1, ">\n");
	}
	if (len > ntohs (h->len))
		len = ntohs (h->len);
	p = (u_char *)(h + 1);
	switch (h->type) {
	case CONF_REQ:
		if (len < 4) {
			if (debug)
				log(-1, SPP_FMT "%s invalid conf-req length %d\n",
				       SPP_ARGS(ifp), cp->name,
				       len);
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			break;
		}
		/* handle states where RCR doesn't get a SCA/SCN */
		switch (sp->state[cp->protoidx]) {
		case STATE_CLOSING:
		case STATE_STOPPING:
			return;
		case STATE_CLOSED:
			sppp_cp_send(sp, cp->proto, TERM_ACK, h->ident,
				     0, 0);
			return;
		}
		rv = (cp->RCR)(sp, h, len);
		switch (sp->state[cp->protoidx]) {
		case STATE_OPENED:
			(cp->tld)(sp);
			(cp->scr)(sp);
			/* FALLTHROUGH */
		case STATE_ACK_SENT:
		case STATE_REQ_SENT:
			/*
			 * sppp_cp_change_state() have the side effect of
			 * restarting the timeouts. We want to avoid that
			 * if the state don't change, otherwise we won't
			 * ever timeout and resend a configuration request
			 * that got lost.
			 */
			if (sp->state[cp->protoidx] == (rv ? STATE_ACK_SENT:
			    STATE_REQ_SENT))
				break;
			sppp_cp_change_state(cp, sp, rv?
					     STATE_ACK_SENT: STATE_REQ_SENT);
			break;
		case STATE_STOPPED:
			sp->rst_counter[cp->protoidx] = sp->lcp.max_configure;
			(cp->scr)(sp);
			sppp_cp_change_state(cp, sp, rv?
					     STATE_ACK_SENT: STATE_REQ_SENT);
			break;
		case STATE_ACK_RCVD:
			if (rv) {
				sppp_cp_change_state(cp, sp, STATE_OPENED);
				if (debug)
					log(LOG_DEBUG, SPP_FMT "%s tlu\n",
					    SPP_ARGS(ifp),
					    cp->name);
				(cp->tlu)(sp);
			} else
				sppp_cp_change_state(cp, sp, STATE_ACK_RCVD);
			break;
		default:
			printf(SPP_FMT "%s illegal %s in state %s\n",
			       SPP_ARGS(ifp), cp->name,
			       sppp_cp_type_name(h->type),
			       sppp_state_name(sp->state[cp->protoidx]));
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		}
		break;
	case CONF_ACK:
		if (h->ident != sp->confid[cp->protoidx]) {
			if (debug)
				log(-1, SPP_FMT "%s id mismatch 0x%x != 0x%x\n",
				       SPP_ARGS(ifp), cp->name,
				       h->ident, sp->confid[cp->protoidx]);
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			break;
		}
		switch (sp->state[cp->protoidx]) {
		case STATE_CLOSED:
		case STATE_STOPPED:
			sppp_cp_send(sp, cp->proto, TERM_ACK, h->ident, 0, 0);
			break;
		case STATE_CLOSING:
		case STATE_STOPPING:
			break;
		case STATE_REQ_SENT:
			sp->rst_counter[cp->protoidx] = sp->lcp.max_configure;
			sppp_cp_change_state(cp, sp, STATE_ACK_RCVD);
			break;
		case STATE_OPENED:
			(cp->tld)(sp);
			/* FALLTHROUGH */
		case STATE_ACK_RCVD:
			(cp->scr)(sp);
			sppp_cp_change_state(cp, sp, STATE_REQ_SENT);
			break;
		case STATE_ACK_SENT:
			sp->rst_counter[cp->protoidx] = sp->lcp.max_configure;
			sppp_cp_change_state(cp, sp, STATE_OPENED);
			if (debug)
				log(LOG_DEBUG, SPP_FMT "%s tlu\n",
				       SPP_ARGS(ifp), cp->name);
			(cp->tlu)(sp);
			break;
		default:
			printf(SPP_FMT "%s illegal %s in state %s\n",
			       SPP_ARGS(ifp), cp->name,
			       sppp_cp_type_name(h->type),
			       sppp_state_name(sp->state[cp->protoidx]));
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		}
		break;
	case CONF_NAK:
	case CONF_REJ:
		if (h->ident != sp->confid[cp->protoidx]) {
			if (debug)
				log(-1, SPP_FMT "%s id mismatch 0x%x != 0x%x\n",
				       SPP_ARGS(ifp), cp->name,
				       h->ident, sp->confid[cp->protoidx]);
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			break;
		}
		if (h->type == CONF_NAK)
			(cp->RCN_nak)(sp, h, len);
		else /* CONF_REJ */
			(cp->RCN_rej)(sp, h, len);

		switch (sp->state[cp->protoidx]) {
		case STATE_CLOSED:
		case STATE_STOPPED:
			sppp_cp_send(sp, cp->proto, TERM_ACK, h->ident, 0, 0);
			break;
		case STATE_REQ_SENT:
		case STATE_ACK_SENT:
			sp->rst_counter[cp->protoidx] = sp->lcp.max_configure;
			/*
			 * Slow things down a bit if we think we might be
			 * in loopback. Depend on the timeout to send the
			 * next configuration request.
			 */
			if (sp->pp_loopcnt)
				break;
			(cp->scr)(sp);
			break;
		case STATE_OPENED:
			(cp->tld)(sp);
			/* FALLTHROUGH */
		case STATE_ACK_RCVD:
			sppp_cp_change_state(cp, sp, STATE_REQ_SENT);
			(cp->scr)(sp);
			break;
		case STATE_CLOSING:
		case STATE_STOPPING:
			break;
		default:
			printf(SPP_FMT "%s illegal %s in state %s\n",
			       SPP_ARGS(ifp), cp->name,
			       sppp_cp_type_name(h->type),
			       sppp_state_name(sp->state[cp->protoidx]));
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		}
		break;

	case TERM_REQ:
		switch (sp->state[cp->protoidx]) {
		case STATE_ACK_RCVD:
		case STATE_ACK_SENT:
			sppp_cp_change_state(cp, sp, STATE_REQ_SENT);
			/* FALLTHROUGH */
		case STATE_CLOSED:
		case STATE_STOPPED:
		case STATE_CLOSING:
		case STATE_STOPPING:
		case STATE_REQ_SENT:
		  sta:
			/* Send Terminate-Ack packet. */
			if (debug)
				log(LOG_DEBUG, SPP_FMT "%s send terminate-ack\n",
				    SPP_ARGS(ifp), cp->name);
			sppp_cp_send(sp, cp->proto, TERM_ACK, h->ident, 0, 0);
			break;
		case STATE_OPENED:
			(cp->tld)(sp);
			sp->rst_counter[cp->protoidx] = 0;
			sppp_cp_change_state(cp, sp, STATE_STOPPING);
			goto sta;
			break;
		default:
			printf(SPP_FMT "%s illegal %s in state %s\n",
			       SPP_ARGS(ifp), cp->name,
			       sppp_cp_type_name(h->type),
			       sppp_state_name(sp->state[cp->protoidx]));
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		}
		break;
	case TERM_ACK:
		switch (sp->state[cp->protoidx]) {
		case STATE_CLOSED:
		case STATE_STOPPED:
		case STATE_REQ_SENT:
		case STATE_ACK_SENT:
			break;
		case STATE_CLOSING:
			sppp_cp_change_state(cp, sp, STATE_CLOSED);
			(cp->tlf)(sp);
			break;
		case STATE_STOPPING:
			sppp_cp_change_state(cp, sp, STATE_STOPPED);
			(cp->tlf)(sp);
			break;
		case STATE_ACK_RCVD:
			sppp_cp_change_state(cp, sp, STATE_REQ_SENT);
			break;
		case STATE_OPENED:
			(cp->tld)(sp);
			(cp->scr)(sp);
			sppp_cp_change_state(cp, sp, STATE_ACK_RCVD);
			break;
		default:
			printf(SPP_FMT "%s illegal %s in state %s\n",
			       SPP_ARGS(ifp), cp->name,
			       sppp_cp_type_name(h->type),
			       sppp_state_name(sp->state[cp->protoidx]));
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		}
		break;
	case CODE_REJ:
		/* XXX catastrophic rejects (RXJ-) aren't handled yet. */
		log(LOG_INFO,
		    SPP_FMT "%s: ignoring RXJ (%s) for proto 0x%x, "
		    "danger will robinson\n",
		    SPP_ARGS(ifp), cp->name,
		    sppp_cp_type_name(h->type), ntohs(*((u_short *)p)));
		switch (sp->state[cp->protoidx]) {
		case STATE_CLOSED:
		case STATE_STOPPED:
		case STATE_REQ_SENT:
		case STATE_ACK_SENT:
		case STATE_CLOSING:
		case STATE_STOPPING:
		case STATE_OPENED:
			break;
		case STATE_ACK_RCVD:
			sppp_cp_change_state(cp, sp, STATE_REQ_SENT);
			break;
		default:
			printf(SPP_FMT "%s illegal %s in state %s\n",
			       SPP_ARGS(ifp), cp->name,
			       sppp_cp_type_name(h->type),
			       sppp_state_name(sp->state[cp->protoidx]));
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		}
		break;
	case PROTO_REJ:
	    {
		int catastrophic;
		const struct cp *upper;
		int i;
		u_int16_t proto;

		catastrophic = 0;
		upper = NULL;
		proto = ntohs(*((u_int16_t *)p));
		for (i = 0; i < IDX_COUNT; i++) {
			if (cps[i]->proto == proto) {
				upper = cps[i];
				break;
			}
		}
		if (upper == NULL)
			catastrophic++;

		if (catastrophic || debug)
			log(catastrophic? LOG_INFO: LOG_DEBUG,
			    SPP_FMT "%s: RXJ%c (%s) for proto 0x%x (%s/%s)\n",
			    SPP_ARGS(ifp), cp->name, catastrophic ? '-' : '+',
			    sppp_cp_type_name(h->type), proto,
			    upper ? upper->name : "unknown",
			    upper ? sppp_state_name(sp->state[upper->protoidx]) : "?");

		/*
		 * if we got RXJ+ against conf-req, the peer does not implement
		 * this particular protocol type.  terminate the protocol.
		 */
		if (upper && !catastrophic) {
			if (sp->state[upper->protoidx] == STATE_REQ_SENT) {
				upper->Close(sp);
				break;
			}
		}

		/* XXX catastrophic rejects (RXJ-) aren't handled yet. */
		switch (sp->state[cp->protoidx]) {
		case STATE_CLOSED:
		case STATE_STOPPED:
		case STATE_REQ_SENT:
		case STATE_ACK_SENT:
		case STATE_CLOSING:
		case STATE_STOPPING:
		case STATE_OPENED:
			break;
		case STATE_ACK_RCVD:
			sppp_cp_change_state(cp, sp, STATE_REQ_SENT);
			break;
		default:
			printf(SPP_FMT "%s illegal %s in state %s\n",
			       SPP_ARGS(ifp), cp->name,
			       sppp_cp_type_name(h->type),
			       sppp_state_name(sp->state[cp->protoidx]));
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		}
		break;
	    }
	case DISC_REQ:
		if (cp->proto != PPP_LCP)
			goto illegal;
		/* Discard the packet. */
		break;
	case ECHO_REQ:
		if (cp->proto != PPP_LCP)
			goto illegal;
		if (sp->state[cp->protoidx] != STATE_OPENED) {
			if (debug)
				log(-1, SPP_FMT "lcp echo req but lcp closed\n",
				       SPP_ARGS(ifp));
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			break;
		}
		if (len < 8) {
			if (debug)
				log(-1, SPP_FMT "invalid lcp echo request "
				       "packet length: %d bytes\n",
				       SPP_ARGS(ifp), len);
			break;
		}
		if ((sp->lcp.opts & (1 << LCP_OPT_MAGIC)) &&
		    ntohl (*(long*)(h+1)) == sp->lcp.magic) {
			/* Line loopback mode detected. */
			printf(SPP_FMT "loopback\n", SPP_ARGS(ifp));
			sp->pp_loopcnt = MAXALIVECNT * 5;
			if_down (ifp);
			sppp_qflush (&sp->pp_cpq);

			/* Shut down the PPP link. */
			/* XXX */
			lcp.Down(sp);
			lcp.Up(sp);
			break;
		}
		*(long*)(h+1) = htonl (sp->lcp.magic);
		if (debug)
			log(-1, SPP_FMT "got lcp echo req, sending echo rep\n",
			       SPP_ARGS(ifp));
		sppp_cp_send (sp, PPP_LCP, ECHO_REPLY, h->ident, len-4, h+1);
		break;
	case ECHO_REPLY:
		if (cp->proto != PPP_LCP)
			goto illegal;
		if (h->ident != sp->lcp.echoid) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			break;
		}
		if (len < 8) {
			if (debug)
				log(-1, SPP_FMT "lcp invalid echo reply "
				       "packet length: %d bytes\n",
				       SPP_ARGS(ifp), len);
			break;
		}
		if (debug)
			log(-1, SPP_FMT "lcp got echo rep\n",
			       SPP_ARGS(ifp));
		if (!(sp->lcp.opts & (1 << LCP_OPT_MAGIC)) ||
		    ntohl (*(long*)(h+1)) != sp->lcp.magic)
			sp->pp_alivecnt = 0;
		break;
	default:
		/* Unknown packet type -- send Code-Reject packet. */
	  illegal:
		if (debug)
			log(-1, SPP_FMT "%s send code-rej for 0x%x\n",
			       SPP_ARGS(ifp), cp->name, h->type);
		sppp_cp_send(sp, cp->proto, CODE_REJ,
			     ++sp->pp_seq[cp->protoidx], m->m_pkthdr.len, h);
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
	}
}


/*
 * The generic part of all Up/Down/Open/Close/TO event handlers.
 * Basically, the state transition handling in the automaton.
 */
static void
sppp_up_event(const struct cp *cp, struct sppp *sp)
{
	STDDCL;

	if (debug)
		log(LOG_DEBUG, SPP_FMT "%s up(%s)\n",
		    SPP_ARGS(ifp), cp->name,
		    sppp_state_name(sp->state[cp->protoidx]));

	switch (sp->state[cp->protoidx]) {
	case STATE_INITIAL:
		sppp_cp_change_state(cp, sp, STATE_CLOSED);
		break;
	case STATE_STARTING:
		sp->rst_counter[cp->protoidx] = sp->lcp.max_configure;
		(cp->scr)(sp);
		sppp_cp_change_state(cp, sp, STATE_REQ_SENT);
		break;
	default:
		printf(SPP_FMT "%s illegal up in state %s\n",
		       SPP_ARGS(ifp), cp->name,
		       sppp_state_name(sp->state[cp->protoidx]));
	}
}

static void
sppp_down_event(const struct cp *cp, struct sppp *sp)
{
	STDDCL;

	if (debug)
		log(LOG_DEBUG, SPP_FMT "%s down(%s)\n",
		    SPP_ARGS(ifp), cp->name,
		    sppp_state_name(sp->state[cp->protoidx]));

	switch (sp->state[cp->protoidx]) {
	case STATE_CLOSED:
	case STATE_CLOSING:
		sppp_cp_change_state(cp, sp, STATE_INITIAL);
		break;
	case STATE_STOPPED:
		sppp_cp_change_state(cp, sp, STATE_STARTING);
		(cp->tls)(sp);
		break;
	case STATE_STOPPING:
	case STATE_REQ_SENT:
	case STATE_ACK_RCVD:
	case STATE_ACK_SENT:
		sppp_cp_change_state(cp, sp, STATE_STARTING);
		break;
	case STATE_OPENED:
		(cp->tld)(sp);
		sppp_cp_change_state(cp, sp, STATE_STARTING);
		break;
	default:
		printf(SPP_FMT "%s illegal down in state %s\n",
		       SPP_ARGS(ifp), cp->name,
		       sppp_state_name(sp->state[cp->protoidx]));
	}
}


static void
sppp_open_event(const struct cp *cp, struct sppp *sp)
{
	STDDCL;

	if (debug)
		log(LOG_DEBUG, SPP_FMT "%s open(%s)\n",
		    SPP_ARGS(ifp), cp->name,
		    sppp_state_name(sp->state[cp->protoidx]));

	switch (sp->state[cp->protoidx]) {
	case STATE_INITIAL:
		sppp_cp_change_state(cp, sp, STATE_STARTING);
		(cp->tls)(sp);
		break;
	case STATE_STARTING:
		break;
	case STATE_CLOSED:
		sp->rst_counter[cp->protoidx] = sp->lcp.max_configure;
		(cp->scr)(sp);
		sppp_cp_change_state(cp, sp, STATE_REQ_SENT);
		break;
	case STATE_STOPPED:
		/*
		 * Try escaping stopped state.  This seems to bite
		 * people occasionally, in particular for IPCP,
		 * presumably following previous IPCP negotiation
		 * aborts.  Somehow, we must have missed a Down event
		 * which would have caused a transition into starting
		 * state, so as a bandaid we force the Down event now.
		 * This effectively implements (something like the)
		 * `restart' option mentioned in the state transition
		 * table of RFC 1661.
		 */
		sppp_cp_change_state(cp, sp, STATE_STARTING);
		(cp->tls)(sp);
		break;
	case STATE_STOPPING:
	case STATE_REQ_SENT:
	case STATE_ACK_RCVD:
	case STATE_ACK_SENT:
	case STATE_OPENED:
		break;
	case STATE_CLOSING:
		sppp_cp_change_state(cp, sp, STATE_STOPPING);
		break;
	}
}


static void
sppp_close_event(const struct cp *cp, struct sppp *sp)
{
	STDDCL;

	if (debug)
		log(LOG_DEBUG, SPP_FMT "%s close(%s)\n",
		    SPP_ARGS(ifp), cp->name,
		    sppp_state_name(sp->state[cp->protoidx]));

	switch (sp->state[cp->protoidx]) {
	case STATE_INITIAL:
	case STATE_CLOSED:
	case STATE_CLOSING:
		break;
	case STATE_STARTING:
		sppp_cp_change_state(cp, sp, STATE_INITIAL);
		(cp->tlf)(sp);
		break;
	case STATE_STOPPED:
		sppp_cp_change_state(cp, sp, STATE_CLOSED);
		break;
	case STATE_STOPPING:
		sppp_cp_change_state(cp, sp, STATE_CLOSING);
		break;
	case STATE_OPENED:
		(cp->tld)(sp);
		/* FALLTHROUGH */
	case STATE_REQ_SENT:
	case STATE_ACK_RCVD:
	case STATE_ACK_SENT:
		sp->rst_counter[cp->protoidx] = sp->lcp.max_terminate;
		sppp_cp_send(sp, cp->proto, TERM_REQ,
			     ++sp->pp_seq[cp->protoidx], 0, 0);
		sppp_cp_change_state(cp, sp, STATE_CLOSING);
		break;
	}
}

static void
sppp_to_event(const struct cp *cp, struct sppp *sp)
{
	STDDCL;

	SPPP_LOCK(sp);
	if (debug)
		log(LOG_DEBUG, SPP_FMT "%s TO(%s) rst_counter = %d\n",
		    SPP_ARGS(ifp), cp->name,
		    sppp_state_name(sp->state[cp->protoidx]),
		    sp->rst_counter[cp->protoidx]);

	if (--sp->rst_counter[cp->protoidx] < 0)
		/* TO- event */
		switch (sp->state[cp->protoidx]) {
		case STATE_CLOSING:
			sppp_cp_change_state(cp, sp, STATE_CLOSED);
			(cp->tlf)(sp);
			break;
		case STATE_STOPPING:
			sppp_cp_change_state(cp, sp, STATE_STOPPED);
			(cp->tlf)(sp);
			break;
		case STATE_REQ_SENT:
		case STATE_ACK_RCVD:
		case STATE_ACK_SENT:
			sppp_cp_change_state(cp, sp, STATE_STOPPED);
			(cp->tlf)(sp);
			break;
		}
	else
		/* TO+ event */
		switch (sp->state[cp->protoidx]) {
		case STATE_CLOSING:
		case STATE_STOPPING:
			sppp_cp_send(sp, cp->proto, TERM_REQ,
				     ++sp->pp_seq[cp->protoidx], 0, 0);
			callout_reset(&sp->ch[cp->protoidx], sp->lcp.timeout,
				      cp->TO, (void *)sp);
			break;
		case STATE_REQ_SENT:
		case STATE_ACK_RCVD:
			(cp->scr)(sp);
			/* sppp_cp_change_state() will restart the timer */
			sppp_cp_change_state(cp, sp, STATE_REQ_SENT);
			break;
		case STATE_ACK_SENT:
			(cp->scr)(sp);
			callout_reset(&sp->ch[cp->protoidx], sp->lcp.timeout,
				      cp->TO, (void *)sp);
			break;
		}

	SPPP_UNLOCK(sp);
}

/*
 * Change the state of a control protocol in the state automaton.
 * Takes care of starting/stopping the restart timer.
 */
static void
sppp_cp_change_state(const struct cp *cp, struct sppp *sp, int newstate)
{
	sp->state[cp->protoidx] = newstate;

	callout_stop (&sp->ch[cp->protoidx]);

	switch (newstate) {
	case STATE_INITIAL:
	case STATE_STARTING:
	case STATE_CLOSED:
	case STATE_STOPPED:
	case STATE_OPENED:
		break;
	case STATE_CLOSING:
	case STATE_STOPPING:
	case STATE_REQ_SENT:
	case STATE_ACK_RCVD:
	case STATE_ACK_SENT:
		callout_reset(&sp->ch[cp->protoidx], sp->lcp.timeout,
			      cp->TO, (void *)sp);
		break;
	}
}

/*
 *--------------------------------------------------------------------------*
 *                                                                          *
 *                         The LCP implementation.                          *
 *                                                                          *
 *--------------------------------------------------------------------------*
 */
static void
sppp_pp_up(struct sppp *sp)
{
	SPPP_LOCK(sp);
	lcp.Up(sp);
	SPPP_UNLOCK(sp);
}

static void
sppp_pp_down(struct sppp *sp)
{
	SPPP_LOCK(sp);
	lcp.Down(sp);
	SPPP_UNLOCK(sp);
}

static void
sppp_lcp_init(struct sppp *sp)
{
	sp->lcp.opts = (1 << LCP_OPT_MAGIC);
	sp->lcp.magic = 0;
	sp->state[IDX_LCP] = STATE_INITIAL;
	sp->fail_counter[IDX_LCP] = 0;
	sp->pp_seq[IDX_LCP] = 0;
	sp->pp_rseq[IDX_LCP] = 0;
	sp->lcp.protos = 0;
	sp->lcp.mru = sp->lcp.their_mru = PP_MTU;

	/* Note that these values are  relevant for all control protocols */
	sp->lcp.timeout = 3 * hz;
	sp->lcp.max_terminate = 2;
	sp->lcp.max_configure = 10;
	sp->lcp.max_failure = 10;
 	callout_init(&sp->ch[IDX_LCP], 1);
}

static void
sppp_lcp_up(struct sppp *sp)
{
	STDDCL;

	sp->pp_alivecnt = 0;
	sp->lcp.opts = (1 << LCP_OPT_MAGIC);
	sp->lcp.magic = 0;
	sp->lcp.protos = 0;
	sp->lcp.mru = sp->lcp.their_mru = PP_MTU;
	/*
	 * If we are authenticator, negotiate LCP_AUTH
	 */
	if (sp->hisauth.proto != 0)
		sp->lcp.opts |= (1 << LCP_OPT_AUTH_PROTO);
	else
		sp->lcp.opts &= ~(1 << LCP_OPT_AUTH_PROTO);
	sp->pp_flags &= ~PP_NEEDAUTH;
	/*
	 * If this interface is passive or dial-on-demand, and we are
	 * still in Initial state, it means we've got an incoming
	 * call.  Activate the interface.
	 */
	if ((ifp->if_flags & (IFF_AUTO | IFF_PASSIVE)) != 0) {
		if (debug)
			log(LOG_DEBUG,
			    SPP_FMT "Up event", SPP_ARGS(ifp));
		ifp->if_drv_flags |= IFF_DRV_RUNNING;
		if (sp->state[IDX_LCP] == STATE_INITIAL) {
			if (debug)
				log(-1, "(incoming call)\n");
			sp->pp_flags |= PP_CALLIN;
			lcp.Open(sp);
		} else if (debug)
			log(-1, "\n");
	} else if ((ifp->if_flags & (IFF_AUTO | IFF_PASSIVE)) == 0 &&
		   (sp->state[IDX_LCP] == STATE_INITIAL)) {
		ifp->if_drv_flags |= IFF_DRV_RUNNING;
		lcp.Open(sp);
	}

	sppp_up_event(&lcp, sp);
}

static void
sppp_lcp_down(struct sppp *sp)
{
	STDDCL;

	sppp_down_event(&lcp, sp);

	/*
	 * If this is neither a dial-on-demand nor a passive
	 * interface, simulate an ``ifconfig down'' action, so the
	 * administrator can force a redial by another ``ifconfig
	 * up''.  XXX For leased line operation, should we immediately
	 * try to reopen the connection here?
	 */
	if ((ifp->if_flags & (IFF_AUTO | IFF_PASSIVE)) == 0) {
		log(LOG_INFO,
		    SPP_FMT "Down event, taking interface down.\n",
		    SPP_ARGS(ifp));
		if_down(ifp);
	} else {
		if (debug)
			log(LOG_DEBUG,
			    SPP_FMT "Down event (carrier loss)\n",
			    SPP_ARGS(ifp));
		sp->pp_flags &= ~PP_CALLIN;
		if (sp->state[IDX_LCP] != STATE_INITIAL)
			lcp.Close(sp);
		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	}
}

static void
sppp_lcp_open(struct sppp *sp)
{
	sppp_open_event(&lcp, sp);
}

static void
sppp_lcp_close(struct sppp *sp)
{
	sppp_close_event(&lcp, sp);
}

static void
sppp_lcp_TO(void *cookie)
{
	sppp_to_event(&lcp, (struct sppp *)cookie);
}

/*
 * Analyze a configure request.  Return true if it was agreeable, and
 * caused action sca, false if it has been rejected or nak'ed, and
 * caused action scn.  (The return value is used to make the state
 * transition decision in the state automaton.)
 */
static int
sppp_lcp_RCR(struct sppp *sp, struct lcp_header *h, int len)
{
	STDDCL;
	u_char *buf, *r, *p;
	int origlen, rlen;
	u_long nmagic;
	u_short authproto;

	len -= 4;
	origlen = len;
	buf = r = malloc (len, M_TEMP, M_NOWAIT);
	if (! buf)
		return (0);

	if (debug)
		log(LOG_DEBUG, SPP_FMT "lcp parse opts: ",
		    SPP_ARGS(ifp));

	/* pass 1: check for things that need to be rejected */
	p = (void*) (h+1);
	for (rlen=0; len >= 2 && p[1] >= 2 && len >= p[1];
	    len-=p[1], p+=p[1]) {
		if (debug)
			log(-1, " %s ", sppp_lcp_opt_name(*p));
		switch (*p) {
		case LCP_OPT_MAGIC:
			/* Magic number. */
			if (len >= 6 && p[1] == 6)
				continue;
			if (debug)
				log(-1, "[invalid] ");
			break;
		case LCP_OPT_ASYNC_MAP:
			/* Async control character map. */
			if (len >= 6 && p[1] == 6)
				continue;
			if (debug)
				log(-1, "[invalid] ");
			break;
		case LCP_OPT_MRU:
			/* Maximum receive unit. */
			if (len >= 4 && p[1] == 4)
				continue;
			if (debug)
				log(-1, "[invalid] ");
			break;
		case LCP_OPT_AUTH_PROTO:
			if (len < 4) {
				if (debug)
					log(-1, "[invalid] ");
				break;
			}
			authproto = (p[2] << 8) + p[3];
			if (authproto == PPP_CHAP && p[1] != 5) {
				if (debug)
					log(-1, "[invalid chap len] ");
				break;
			}
			if (sp->myauth.proto == 0) {
				/* we are not configured to do auth */
				if (debug)
					log(-1, "[not configured] ");
				break;
			}
			/*
			 * Remote want us to authenticate, remember this,
			 * so we stay in PHASE_AUTHENTICATE after LCP got
			 * up.
			 */
			sp->pp_flags |= PP_NEEDAUTH;
			continue;
		default:
			/* Others not supported. */
			if (debug)
				log(-1, "[rej] ");
			break;
		}
		/* Add the option to rejected list. */
		bcopy (p, r, p[1]);
		r += p[1];
		rlen += p[1];
	}
	if (rlen) {
		if (debug)
			log(-1, " send conf-rej\n");
		sppp_cp_send (sp, PPP_LCP, CONF_REJ, h->ident, rlen, buf);
		return 0;
	} else if (debug)
		log(-1, "\n");

	/*
	 * pass 2: check for option values that are unacceptable and
	 * thus require to be nak'ed.
	 */
	if (debug)
		log(LOG_DEBUG, SPP_FMT "lcp parse opt values: ",
		    SPP_ARGS(ifp));

	p = (void*) (h+1);
	len = origlen;
	for (rlen=0; len >= 2 && p[1] >= 2 && len >= p[1];
	    len-=p[1], p+=p[1]) {
		if (debug)
			log(-1, " %s ", sppp_lcp_opt_name(*p));
		switch (*p) {
		case LCP_OPT_MAGIC:
			/* Magic number -- extract. */
			nmagic = (u_long)p[2] << 24 |
				(u_long)p[3] << 16 | p[4] << 8 | p[5];
			if (nmagic != sp->lcp.magic) {
				sp->pp_loopcnt = 0;
				if (debug)
					log(-1, "0x%lx ", nmagic);
				continue;
			}
			if (debug && sp->pp_loopcnt < MAXALIVECNT*5)
				log(-1, "[glitch] ");
			++sp->pp_loopcnt;
			/*
			 * We negate our magic here, and NAK it.  If
			 * we see it later in an NAK packet, we
			 * suggest a new one.
			 */
			nmagic = ~sp->lcp.magic;
			/* Gonna NAK it. */
			p[2] = nmagic >> 24;
			p[3] = nmagic >> 16;
			p[4] = nmagic >> 8;
			p[5] = nmagic;
			break;

		case LCP_OPT_ASYNC_MAP:
			/*
			 * Async control character map -- just ignore it.
			 *
			 * Quote from RFC 1662, chapter 6:
			 * To enable this functionality, synchronous PPP
			 * implementations MUST always respond to the
			 * Async-Control-Character-Map Configuration
			 * Option with the LCP Configure-Ack.  However,
			 * acceptance of the Configuration Option does
			 * not imply that the synchronous implementation
			 * will do any ACCM mapping.  Instead, all such
			 * octet mapping will be performed by the
			 * asynchronous-to-synchronous converter.
			 */
			continue;

		case LCP_OPT_MRU:
			/*
			 * Maximum receive unit.  Always agreeable,
			 * but ignored by now.
			 */
			sp->lcp.their_mru = p[2] * 256 + p[3];
			if (debug)
				log(-1, "%lu ", sp->lcp.their_mru);
			continue;

		case LCP_OPT_AUTH_PROTO:
			authproto = (p[2] << 8) + p[3];
			if (sp->myauth.proto != authproto) {
				/* not agreed, nak */
				if (debug)
					log(-1, "[mine %s != his %s] ",
					       sppp_proto_name(sp->hisauth.proto),
					       sppp_proto_name(authproto));
				p[2] = sp->myauth.proto >> 8;
				p[3] = sp->myauth.proto;
				break;
			}
			if (authproto == PPP_CHAP && p[4] != CHAP_MD5) {
				if (debug)
					log(-1, "[chap not MD5] ");
				p[4] = CHAP_MD5;
				break;
			}
			continue;
		}
		/* Add the option to nak'ed list. */
		bcopy (p, r, p[1]);
		r += p[1];
		rlen += p[1];
	}
	if (rlen) {
		/*
		 * Local and remote magics equal -- loopback?
		 */
		if (sp->pp_loopcnt >= MAXALIVECNT*5) {
			if (sp->pp_loopcnt == MAXALIVECNT*5)
				printf (SPP_FMT "loopback\n",
					SPP_ARGS(ifp));
			if (ifp->if_flags & IFF_UP) {
				if_down(ifp);
				sppp_qflush(&sp->pp_cpq);
				/* XXX ? */
				lcp.Down(sp);
				lcp.Up(sp);
			}
		} else if (!sp->pp_loopcnt &&
			   ++sp->fail_counter[IDX_LCP] >= sp->lcp.max_failure) {
			if (debug)
				log(-1, " max_failure (%d) exceeded, "
				       "send conf-rej\n",
				       sp->lcp.max_failure);
			sppp_cp_send(sp, PPP_LCP, CONF_REJ, h->ident, rlen, buf);
		} else {
			if (debug)
				log(-1, " send conf-nak\n");
			sppp_cp_send (sp, PPP_LCP, CONF_NAK, h->ident, rlen, buf);
		}
	} else {
		if (debug)
			log(-1, " send conf-ack\n");
		sp->fail_counter[IDX_LCP] = 0;
		sp->pp_loopcnt = 0;
		sppp_cp_send (sp, PPP_LCP, CONF_ACK,
			      h->ident, origlen, h+1);
	}

	free (buf, M_TEMP);
	return (rlen == 0);
}

/*
 * Analyze the LCP Configure-Reject option list, and adjust our
 * negotiation.
 */
static void
sppp_lcp_RCN_rej(struct sppp *sp, struct lcp_header *h, int len)
{
	STDDCL;
	u_char *buf, *p;

	len -= 4;
	buf = malloc (len, M_TEMP, M_NOWAIT);
	if (!buf)
		return;

	if (debug)
		log(LOG_DEBUG, SPP_FMT "lcp rej opts: ",
		    SPP_ARGS(ifp));

	p = (void*) (h+1);
	for (; len >= 2 && p[1] >= 2 && len >= p[1];
	    len -= p[1], p += p[1]) {
		if (debug)
			log(-1, " %s ", sppp_lcp_opt_name(*p));
		switch (*p) {
		case LCP_OPT_MAGIC:
			/* Magic number -- can't use it, use 0 */
			sp->lcp.opts &= ~(1 << LCP_OPT_MAGIC);
			sp->lcp.magic = 0;
			break;
		case LCP_OPT_MRU:
			/*
			 * Should not be rejected anyway, since we only
			 * negotiate a MRU if explicitly requested by
			 * peer.
			 */
			sp->lcp.opts &= ~(1 << LCP_OPT_MRU);
			break;
		case LCP_OPT_AUTH_PROTO:
			/*
			 * Peer doesn't want to authenticate himself,
			 * deny unless this is a dialout call, and
			 * AUTHFLAG_NOCALLOUT is set.
			 */
			if ((sp->pp_flags & PP_CALLIN) == 0 &&
			    (sp->hisauth.flags & AUTHFLAG_NOCALLOUT) != 0) {
				if (debug)
					log(-1, "[don't insist on auth "
					       "for callout]");
				sp->lcp.opts &= ~(1 << LCP_OPT_AUTH_PROTO);
				break;
			}
			if (debug)
				log(-1, "[access denied]\n");
			lcp.Close(sp);
			break;
		}
	}
	if (debug)
		log(-1, "\n");
	free (buf, M_TEMP);
	return;
}

/*
 * Analyze the LCP Configure-NAK option list, and adjust our
 * negotiation.
 */
static void
sppp_lcp_RCN_nak(struct sppp *sp, struct lcp_header *h, int len)
{
	STDDCL;
	u_char *buf, *p;
	u_long magic;

	len -= 4;
	buf = malloc (len, M_TEMP, M_NOWAIT);
	if (!buf)
		return;

	if (debug)
		log(LOG_DEBUG, SPP_FMT "lcp nak opts: ",
		    SPP_ARGS(ifp));

	p = (void*) (h+1);
	for (; len >= 2 && p[1] >= 2 && len >= p[1];
	    len -= p[1], p += p[1]) {
		if (debug)
			log(-1, " %s ", sppp_lcp_opt_name(*p));
		switch (*p) {
		case LCP_OPT_MAGIC:
			/* Magic number -- renegotiate */
			if ((sp->lcp.opts & (1 << LCP_OPT_MAGIC)) &&
			    len >= 6 && p[1] == 6) {
				magic = (u_long)p[2] << 24 |
					(u_long)p[3] << 16 | p[4] << 8 | p[5];
				/*
				 * If the remote magic is our negated one,
				 * this looks like a loopback problem.
				 * Suggest a new magic to make sure.
				 */
				if (magic == ~sp->lcp.magic) {
					if (debug)
						log(-1, "magic glitch ");
					sp->lcp.magic = random();
				} else {
					sp->lcp.magic = magic;
					if (debug)
						log(-1, "%lu ", magic);
				}
			}
			break;
		case LCP_OPT_MRU:
			/*
			 * Peer wants to advise us to negotiate an MRU.
			 * Agree on it if it's reasonable, or use
			 * default otherwise.
			 */
			if (len >= 4 && p[1] == 4) {
				u_int mru = p[2] * 256 + p[3];
				if (debug)
					log(-1, "%d ", mru);
				if (mru < PP_MTU || mru > PP_MAX_MRU)
					mru = PP_MTU;
				sp->lcp.mru = mru;
				sp->lcp.opts |= (1 << LCP_OPT_MRU);
			}
			break;
		case LCP_OPT_AUTH_PROTO:
			/*
			 * Peer doesn't like our authentication method,
			 * deny.
			 */
			if (debug)
				log(-1, "[access denied]\n");
			lcp.Close(sp);
			break;
		}
	}
	if (debug)
		log(-1, "\n");
	free (buf, M_TEMP);
	return;
}

static void
sppp_lcp_tlu(struct sppp *sp)
{
	STDDCL;
	int i;
	u_long mask;

	/* XXX ? */
	if (! (ifp->if_flags & IFF_UP) &&
	    (ifp->if_drv_flags & IFF_DRV_RUNNING)) {
		/* Coming out of loopback mode. */
		if_up(ifp);
		printf (SPP_FMT "up\n", SPP_ARGS(ifp));
	}

	for (i = 0; i < IDX_COUNT; i++)
		if ((cps[i])->flags & CP_QUAL)
			(cps[i])->Open(sp);

	if ((sp->lcp.opts & (1 << LCP_OPT_AUTH_PROTO)) != 0 ||
	    (sp->pp_flags & PP_NEEDAUTH) != 0)
		sp->pp_phase = PHASE_AUTHENTICATE;
	else
		sp->pp_phase = PHASE_NETWORK;

	if (debug)
		log(LOG_DEBUG, SPP_FMT "phase %s\n", SPP_ARGS(ifp),
		    sppp_phase_name(sp->pp_phase));

	/*
	 * Open all authentication protocols.  This is even required
	 * if we already proceeded to network phase, since it might be
	 * that remote wants us to authenticate, so we might have to
	 * send a PAP request.  Undesired authentication protocols
	 * don't do anything when they get an Open event.
	 */
	for (i = 0; i < IDX_COUNT; i++)
		if ((cps[i])->flags & CP_AUTH)
			(cps[i])->Open(sp);

	if (sp->pp_phase == PHASE_NETWORK) {
		/* Notify all NCPs. */
		for (i = 0; i < IDX_COUNT; i++)
			if (((cps[i])->flags & CP_NCP) &&
			    /*
			     * XXX
			     * Hack to administratively disable IPv6 if
			     * not desired.  Perhaps we should have another
			     * flag for this, but right now, we can make
			     * all struct cp's read/only.
			     */
			    (cps[i] != &ipv6cp ||
			     (sp->confflags & CONF_ENABLE_IPV6)))
				(cps[i])->Open(sp);
	}

	/* Send Up events to all started protos. */
	for (i = 0, mask = 1; i < IDX_COUNT; i++, mask <<= 1)
		if ((sp->lcp.protos & mask) && ((cps[i])->flags & CP_LCP) == 0)
			(cps[i])->Up(sp);

	/* notify low-level driver of state change */
	if (sp->pp_chg)
		sp->pp_chg(sp, (int)sp->pp_phase);
	
	if (sp->pp_phase == PHASE_NETWORK)
		/* if no NCP is starting, close down */
		sppp_lcp_check_and_close(sp);
}

static void
sppp_lcp_tld(struct sppp *sp)
{
	STDDCL;
	int i;
	u_long mask;

	sp->pp_phase = PHASE_TERMINATE;

	if (debug)
		log(LOG_DEBUG, SPP_FMT "phase %s\n", SPP_ARGS(ifp),
		    sppp_phase_name(sp->pp_phase));

	/*
	 * Take upper layers down.  We send the Down event first and
	 * the Close second to prevent the upper layers from sending
	 * ``a flurry of terminate-request packets'', as the RFC
	 * describes it.
	 */
	for (i = 0, mask = 1; i < IDX_COUNT; i++, mask <<= 1)
		if ((sp->lcp.protos & mask) && ((cps[i])->flags & CP_LCP) == 0) {
			(cps[i])->Down(sp);
			(cps[i])->Close(sp);
		}
}

static void
sppp_lcp_tls(struct sppp *sp)
{
	STDDCL;

	sp->pp_phase = PHASE_ESTABLISH;

	if (debug)
		log(LOG_DEBUG, SPP_FMT "phase %s\n", SPP_ARGS(ifp),
		    sppp_phase_name(sp->pp_phase));

	/* Notify lower layer if desired. */
	if (sp->pp_tls)
		(sp->pp_tls)(sp);
	else
		(sp->pp_up)(sp);
}

static void
sppp_lcp_tlf(struct sppp *sp)
{
	STDDCL;

	sp->pp_phase = PHASE_DEAD;
	if (debug)
		log(LOG_DEBUG, SPP_FMT "phase %s\n", SPP_ARGS(ifp),
		    sppp_phase_name(sp->pp_phase));

	/* Notify lower layer if desired. */
	if (sp->pp_tlf)
		(sp->pp_tlf)(sp);
	else
		(sp->pp_down)(sp);
}

static void
sppp_lcp_scr(struct sppp *sp)
{
	char opt[6 /* magicnum */ + 4 /* mru */ + 5 /* chap */];
	int i = 0;
	u_short authproto;

	if (sp->lcp.opts & (1 << LCP_OPT_MAGIC)) {
		if (! sp->lcp.magic)
			sp->lcp.magic = random();
		opt[i++] = LCP_OPT_MAGIC;
		opt[i++] = 6;
		opt[i++] = sp->lcp.magic >> 24;
		opt[i++] = sp->lcp.magic >> 16;
		opt[i++] = sp->lcp.magic >> 8;
		opt[i++] = sp->lcp.magic;
	}

	if (sp->lcp.opts & (1 << LCP_OPT_MRU)) {
		opt[i++] = LCP_OPT_MRU;
		opt[i++] = 4;
		opt[i++] = sp->lcp.mru >> 8;
		opt[i++] = sp->lcp.mru;
	}

	if (sp->lcp.opts & (1 << LCP_OPT_AUTH_PROTO)) {
		authproto = sp->hisauth.proto;
		opt[i++] = LCP_OPT_AUTH_PROTO;
		opt[i++] = authproto == PPP_CHAP? 5: 4;
		opt[i++] = authproto >> 8;
		opt[i++] = authproto;
		if (authproto == PPP_CHAP)
			opt[i++] = CHAP_MD5;
	}

	sp->confid[IDX_LCP] = ++sp->pp_seq[IDX_LCP];
	sppp_cp_send (sp, PPP_LCP, CONF_REQ, sp->confid[IDX_LCP], i, &opt);
}

/*
 * Check the open NCPs, return true if at least one NCP is open.
 */
static int
sppp_ncp_check(struct sppp *sp)
{
	int i, mask;

	for (i = 0, mask = 1; i < IDX_COUNT; i++, mask <<= 1)
		if ((sp->lcp.protos & mask) && (cps[i])->flags & CP_NCP)
			return 1;
	return 0;
}

/*
 * Re-check the open NCPs and see if we should terminate the link.
 * Called by the NCPs during their tlf action handling.
 */
static void
sppp_lcp_check_and_close(struct sppp *sp)
{

	if (sp->pp_phase < PHASE_NETWORK)
		/* don't bother, we are already going down */
		return;

	if (sppp_ncp_check(sp))
		return;

	lcp.Close(sp);
}

/*
 *--------------------------------------------------------------------------*
 *                                                                          *
 *                        The IPCP implementation.                          *
 *                                                                          *
 *--------------------------------------------------------------------------*
 */

#ifdef INET
static void
sppp_ipcp_init(struct sppp *sp)
{
	sp->ipcp.opts = 0;
	sp->ipcp.flags = 0;
	sp->state[IDX_IPCP] = STATE_INITIAL;
	sp->fail_counter[IDX_IPCP] = 0;
	sp->pp_seq[IDX_IPCP] = 0;
	sp->pp_rseq[IDX_IPCP] = 0;
 	callout_init(&sp->ch[IDX_IPCP], 1);
}

static void
sppp_ipcp_up(struct sppp *sp)
{
	sppp_up_event(&ipcp, sp);
}

static void
sppp_ipcp_down(struct sppp *sp)
{
	sppp_down_event(&ipcp, sp);
}

static void
sppp_ipcp_open(struct sppp *sp)
{
	STDDCL;
	u_long myaddr, hisaddr;

	sp->ipcp.flags &= ~(IPCP_HISADDR_SEEN | IPCP_MYADDR_SEEN |
			    IPCP_MYADDR_DYN | IPCP_VJ);
	sp->ipcp.opts = 0;

	sppp_get_ip_addrs(sp, &myaddr, &hisaddr, 0);
	/*
	 * If we don't have his address, this probably means our
	 * interface doesn't want to talk IP at all.  (This could
	 * be the case if somebody wants to speak only IPX, for
	 * example.)  Don't open IPCP in this case.
	 */
	if (hisaddr == 0L) {
		/* XXX this message should go away */
		if (debug)
			log(LOG_DEBUG, SPP_FMT "ipcp_open(): no IP interface\n",
			    SPP_ARGS(ifp));
		return;
	}
	if (myaddr == 0L) {
		/*
		 * I don't have an assigned address, so i need to
		 * negotiate my address.
		 */
		sp->ipcp.flags |= IPCP_MYADDR_DYN;
		sp->ipcp.opts |= (1 << IPCP_OPT_ADDRESS);
	} else
		sp->ipcp.flags |= IPCP_MYADDR_SEEN;
	if (sp->confflags & CONF_ENABLE_VJ) {
		sp->ipcp.opts |= (1 << IPCP_OPT_COMPRESSION);
		sp->ipcp.max_state = MAX_STATES - 1;
		sp->ipcp.compress_cid = 1;
	}
	sppp_open_event(&ipcp, sp);
}

static void
sppp_ipcp_close(struct sppp *sp)
{
	sppp_close_event(&ipcp, sp);
	if (sp->ipcp.flags & IPCP_MYADDR_DYN)
		/*
		 * My address was dynamic, clear it again.
		 */
		sppp_set_ip_addr(sp, 0L);
}

static void
sppp_ipcp_TO(void *cookie)
{
	sppp_to_event(&ipcp, (struct sppp *)cookie);
}

/*
 * Analyze a configure request.  Return true if it was agreeable, and
 * caused action sca, false if it has been rejected or nak'ed, and
 * caused action scn.  (The return value is used to make the state
 * transition decision in the state automaton.)
 */
static int
sppp_ipcp_RCR(struct sppp *sp, struct lcp_header *h, int len)
{
	u_char *buf, *r, *p;
	struct ifnet *ifp = SP2IFP(sp);
	int rlen, origlen, debug = ifp->if_flags & IFF_DEBUG;
	u_long hisaddr, desiredaddr;
	int gotmyaddr = 0;
	int desiredcomp;

	len -= 4;
	origlen = len;
	/*
	 * Make sure to allocate a buf that can at least hold a
	 * conf-nak with an `address' option.  We might need it below.
	 */
	buf = r = malloc ((len < 6? 6: len), M_TEMP, M_NOWAIT);
	if (! buf)
		return (0);

	/* pass 1: see if we can recognize them */
	if (debug)
		log(LOG_DEBUG, SPP_FMT "ipcp parse opts: ",
		    SPP_ARGS(ifp));
	p = (void*) (h+1);
	for (rlen=0; len >= 2 && p[1] >= 2 && len >= p[1];
	    len-=p[1], p+=p[1]) {
		if (debug)
			log(-1, " %s ", sppp_ipcp_opt_name(*p));
		switch (*p) {
		case IPCP_OPT_COMPRESSION:
			if (!(sp->confflags & CONF_ENABLE_VJ)) {
				/* VJ compression administratively disabled */
				if (debug)
					log(-1, "[locally disabled] ");
				break;
			}
			/*
			 * In theory, we should only conf-rej an
			 * option that is shorter than RFC 1618
			 * requires (i.e. < 4), and should conf-nak
			 * anything else that is not VJ.  However,
			 * since our algorithm always uses the
			 * original option to NAK it with new values,
			 * things would become more complicated.  In
			 * practice, the only commonly implemented IP
			 * compression option is VJ anyway, so the
			 * difference is negligible.
			 */
			if (len >= 6 && p[1] == 6) {
				/*
				 * correctly formed compression option
				 * that could be VJ compression
				 */
				continue;
			}
			if (debug)
				log(-1,
				    "optlen %d [invalid/unsupported] ",
				    p[1]);
			break;
		case IPCP_OPT_ADDRESS:
			if (len >= 6 && p[1] == 6) {
				/* correctly formed address option */
				continue;
			}
			if (debug)
				log(-1, "[invalid] ");
			break;
		default:
			/* Others not supported. */
			if (debug)
				log(-1, "[rej] ");
			break;
		}
		/* Add the option to rejected list. */
		bcopy (p, r, p[1]);
		r += p[1];
		rlen += p[1];
	}
	if (rlen) {
		if (debug)
			log(-1, " send conf-rej\n");
		sppp_cp_send (sp, PPP_IPCP, CONF_REJ, h->ident, rlen, buf);
		return 0;
	} else if (debug)
		log(-1, "\n");

	/* pass 2: parse option values */
	sppp_get_ip_addrs(sp, 0, &hisaddr, 0);
	if (debug)
		log(LOG_DEBUG, SPP_FMT "ipcp parse opt values: ",
		       SPP_ARGS(ifp));
	p = (void*) (h+1);
	len = origlen;
	for (rlen=0; len >= 2 && p[1] >= 2 && len >= p[1];
	    len-=p[1], p+=p[1]) {
		if (debug)
			log(-1, " %s ", sppp_ipcp_opt_name(*p));
		switch (*p) {
		case IPCP_OPT_COMPRESSION:
			desiredcomp = p[2] << 8 | p[3];
			/* We only support VJ */
			if (desiredcomp == IPCP_COMP_VJ) {
				if (debug)
					log(-1, "VJ [ack] ");
				sp->ipcp.flags |= IPCP_VJ;
				sl_compress_init(sp->pp_comp, p[4]);
				sp->ipcp.max_state = p[4];
				sp->ipcp.compress_cid = p[5];
				continue;
			}
			if (debug)
				log(-1,
				    "compproto %#04x [not supported] ",
				    desiredcomp);
			p[2] = IPCP_COMP_VJ >> 8;
			p[3] = IPCP_COMP_VJ;
			p[4] = sp->ipcp.max_state;
			p[5] = sp->ipcp.compress_cid;
			break;
		case IPCP_OPT_ADDRESS:
			/* This is the address he wants in his end */
			desiredaddr = p[2] << 24 | p[3] << 16 |
				p[4] << 8 | p[5];
			if (desiredaddr == hisaddr ||
			    (hisaddr >= 1 && hisaddr <= 254 && desiredaddr != 0)) {
				/*
				 * Peer's address is same as our value,
				 * or we have set it to 0.0.0.* to
				 * indicate that we do not really care,
				 * this is agreeable.  Gonna conf-ack
				 * it.
				 */
				if (debug)
					log(-1, "%s [ack] ",
						sppp_dotted_quad(hisaddr));
				/* record that we've seen it already */
				sp->ipcp.flags |= IPCP_HISADDR_SEEN;
				continue;
			}
			/*
			 * The address wasn't agreeable.  This is either
			 * he sent us 0.0.0.0, asking to assign him an
			 * address, or he send us another address not
			 * matching our value.  Either case, we gonna
			 * conf-nak it with our value.
			 * XXX: we should "rej" if hisaddr == 0
			 */
			if (debug) {
				if (desiredaddr == 0)
					log(-1, "[addr requested] ");
				else
					log(-1, "%s [not agreed] ",
						sppp_dotted_quad(desiredaddr));

			}
			p[2] = hisaddr >> 24;
			p[3] = hisaddr >> 16;
			p[4] = hisaddr >> 8;
			p[5] = hisaddr;
			break;
		}
		/* Add the option to nak'ed list. */
		bcopy (p, r, p[1]);
		r += p[1];
		rlen += p[1];
	}

	/*
	 * If we are about to conf-ack the request, but haven't seen
	 * his address so far, gonna conf-nak it instead, with the
	 * `address' option present and our idea of his address being
	 * filled in there, to request negotiation of both addresses.
	 *
	 * XXX This can result in an endless req - nak loop if peer
	 * doesn't want to send us his address.  Q: What should we do
	 * about it?  XXX  A: implement the max-failure counter.
	 */
	if (rlen == 0 && !(sp->ipcp.flags & IPCP_HISADDR_SEEN) && !gotmyaddr) {
		buf[0] = IPCP_OPT_ADDRESS;
		buf[1] = 6;
		buf[2] = hisaddr >> 24;
		buf[3] = hisaddr >> 16;
		buf[4] = hisaddr >> 8;
		buf[5] = hisaddr;
		rlen = 6;
		if (debug)
			log(-1, "still need hisaddr ");
	}

	if (rlen) {
		if (debug)
			log(-1, " send conf-nak\n");
		sppp_cp_send (sp, PPP_IPCP, CONF_NAK, h->ident, rlen, buf);
	} else {
		if (debug)
			log(-1, " send conf-ack\n");
		sppp_cp_send (sp, PPP_IPCP, CONF_ACK,
			      h->ident, origlen, h+1);
	}

	free (buf, M_TEMP);
	return (rlen == 0);
}

/*
 * Analyze the IPCP Configure-Reject option list, and adjust our
 * negotiation.
 */
static void
sppp_ipcp_RCN_rej(struct sppp *sp, struct lcp_header *h, int len)
{
	u_char *buf, *p;
	struct ifnet *ifp = SP2IFP(sp);
	int debug = ifp->if_flags & IFF_DEBUG;

	len -= 4;
	buf = malloc (len, M_TEMP, M_NOWAIT);
	if (!buf)
		return;

	if (debug)
		log(LOG_DEBUG, SPP_FMT "ipcp rej opts: ",
		    SPP_ARGS(ifp));

	p = (void*) (h+1);
	for (; len >= 2 && p[1] >= 2 && len >= p[1];
	    len -= p[1], p += p[1]) {
		if (debug)
			log(-1, " %s ", sppp_ipcp_opt_name(*p));
		switch (*p) {
		case IPCP_OPT_COMPRESSION:
			sp->ipcp.opts &= ~(1 << IPCP_OPT_COMPRESSION);
			break;
		case IPCP_OPT_ADDRESS:
			/*
			 * Peer doesn't grok address option.  This is
			 * bad.  XXX  Should we better give up here?
			 * XXX We could try old "addresses" option...
			 */
			sp->ipcp.opts &= ~(1 << IPCP_OPT_ADDRESS);
			break;
		}
	}
	if (debug)
		log(-1, "\n");
	free (buf, M_TEMP);
	return;
}

/*
 * Analyze the IPCP Configure-NAK option list, and adjust our
 * negotiation.
 */
static void
sppp_ipcp_RCN_nak(struct sppp *sp, struct lcp_header *h, int len)
{
	u_char *buf, *p;
	struct ifnet *ifp = SP2IFP(sp);
	int debug = ifp->if_flags & IFF_DEBUG;
	int desiredcomp;
	u_long wantaddr;

	len -= 4;
	buf = malloc (len, M_TEMP, M_NOWAIT);
	if (!buf)
		return;

	if (debug)
		log(LOG_DEBUG, SPP_FMT "ipcp nak opts: ",
		    SPP_ARGS(ifp));

	p = (void*) (h+1);
	for (; len >= 2 && p[1] >= 2 && len >= p[1];
	    len -= p[1], p += p[1]) {
		if (debug)
			log(-1, " %s ", sppp_ipcp_opt_name(*p));
		switch (*p) {
		case IPCP_OPT_COMPRESSION:
			if (len >= 6 && p[1] == 6) {
				desiredcomp = p[2] << 8 | p[3];
				if (debug)
					log(-1, "[wantcomp %#04x] ",
						desiredcomp);
				if (desiredcomp == IPCP_COMP_VJ) {
					sl_compress_init(sp->pp_comp, p[4]);
					sp->ipcp.max_state = p[4];
					sp->ipcp.compress_cid = p[5];
					if (debug)
						log(-1, "[agree] ");
				} else
					sp->ipcp.opts &=
						~(1 << IPCP_OPT_COMPRESSION);
			}
			break;
		case IPCP_OPT_ADDRESS:
			/*
			 * Peer doesn't like our local IP address.  See
			 * if we can do something for him.  We'll drop
			 * him our address then.
			 */
			if (len >= 6 && p[1] == 6) {
				wantaddr = p[2] << 24 | p[3] << 16 |
					p[4] << 8 | p[5];
				sp->ipcp.opts |= (1 << IPCP_OPT_ADDRESS);
				if (debug)
					log(-1, "[wantaddr %s] ",
					       sppp_dotted_quad(wantaddr));
				/*
				 * When doing dynamic address assignment,
				 * we accept his offer.  Otherwise, we
				 * ignore it and thus continue to negotiate
				 * our already existing value.
			 	 * XXX: Bogus, if he said no once, he'll
				 * just say no again, might as well die.
				 */
				if (sp->ipcp.flags & IPCP_MYADDR_DYN) {
					sppp_set_ip_addr(sp, wantaddr);
					if (debug)
						log(-1, "[agree] ");
					sp->ipcp.flags |= IPCP_MYADDR_SEEN;
				}
			}
			break;
		}
	}
	if (debug)
		log(-1, "\n");
	free (buf, M_TEMP);
	return;
}

static void
sppp_ipcp_tlu(struct sppp *sp)
{
	/* we are up - notify isdn daemon */
	if (sp->pp_con)
		sp->pp_con(sp);
}

static void
sppp_ipcp_tld(struct sppp *sp)
{
}

static void
sppp_ipcp_tls(struct sppp *sp)
{
	/* indicate to LCP that it must stay alive */
	sp->lcp.protos |= (1 << IDX_IPCP);
}

static void
sppp_ipcp_tlf(struct sppp *sp)
{
	/* we no longer need LCP */
	sp->lcp.protos &= ~(1 << IDX_IPCP);
	sppp_lcp_check_and_close(sp);
}

static void
sppp_ipcp_scr(struct sppp *sp)
{
	char opt[6 /* compression */ + 6 /* address */];
	u_long ouraddr;
	int i = 0;

	if (sp->ipcp.opts & (1 << IPCP_OPT_COMPRESSION)) {
		opt[i++] = IPCP_OPT_COMPRESSION;
		opt[i++] = 6;
		opt[i++] = IPCP_COMP_VJ >> 8;
		opt[i++] = IPCP_COMP_VJ;
		opt[i++] = sp->ipcp.max_state;
		opt[i++] = sp->ipcp.compress_cid;
	}
	if (sp->ipcp.opts & (1 << IPCP_OPT_ADDRESS)) {
		sppp_get_ip_addrs(sp, &ouraddr, 0, 0);
		opt[i++] = IPCP_OPT_ADDRESS;
		opt[i++] = 6;
		opt[i++] = ouraddr >> 24;
		opt[i++] = ouraddr >> 16;
		opt[i++] = ouraddr >> 8;
		opt[i++] = ouraddr;
	}

	sp->confid[IDX_IPCP] = ++sp->pp_seq[IDX_IPCP];
	sppp_cp_send(sp, PPP_IPCP, CONF_REQ, sp->confid[IDX_IPCP], i, &opt);
}
#else /* !INET */
static void
sppp_ipcp_init(struct sppp *sp)
{
}

static void
sppp_ipcp_up(struct sppp *sp)
{
}

static void
sppp_ipcp_down(struct sppp *sp)
{
}

static void
sppp_ipcp_open(struct sppp *sp)
{
}

static void
sppp_ipcp_close(struct sppp *sp)
{
}

static void
sppp_ipcp_TO(void *cookie)
{
}

static int
sppp_ipcp_RCR(struct sppp *sp, struct lcp_header *h, int len)
{
	return (0);
}

static void
sppp_ipcp_RCN_rej(struct sppp *sp, struct lcp_header *h, int len)
{
}

static void
sppp_ipcp_RCN_nak(struct sppp *sp, struct lcp_header *h, int len)
{
}

static void
sppp_ipcp_tlu(struct sppp *sp)
{
}

static void
sppp_ipcp_tld(struct sppp *sp)
{
}

static void
sppp_ipcp_tls(struct sppp *sp)
{
}

static void
sppp_ipcp_tlf(struct sppp *sp)
{
}

static void
sppp_ipcp_scr(struct sppp *sp)
{
}
#endif

/*
 *--------------------------------------------------------------------------*
 *                                                                          *
 *                      The IPv6CP implementation.                          *
 *                                                                          *
 *--------------------------------------------------------------------------*
 */

#ifdef INET6
static void
sppp_ipv6cp_init(struct sppp *sp)
{
	sp->ipv6cp.opts = 0;
	sp->ipv6cp.flags = 0;
	sp->state[IDX_IPV6CP] = STATE_INITIAL;
	sp->fail_counter[IDX_IPV6CP] = 0;
	sp->pp_seq[IDX_IPV6CP] = 0;
	sp->pp_rseq[IDX_IPV6CP] = 0;
 	callout_init(&sp->ch[IDX_IPV6CP], 1);
}

static void
sppp_ipv6cp_up(struct sppp *sp)
{
	sppp_up_event(&ipv6cp, sp);
}

static void
sppp_ipv6cp_down(struct sppp *sp)
{
	sppp_down_event(&ipv6cp, sp);
}

static void
sppp_ipv6cp_open(struct sppp *sp)
{
	STDDCL;
	struct in6_addr myaddr, hisaddr;

#ifdef IPV6CP_MYIFID_DYN
	sp->ipv6cp.flags &= ~(IPV6CP_MYIFID_SEEN|IPV6CP_MYIFID_DYN);
#else
	sp->ipv6cp.flags &= ~IPV6CP_MYIFID_SEEN;
#endif

	sppp_get_ip6_addrs(sp, &myaddr, &hisaddr, 0);
	/*
	 * If we don't have our address, this probably means our
	 * interface doesn't want to talk IPv6 at all.  (This could
	 * be the case if somebody wants to speak only IPX, for
	 * example.)  Don't open IPv6CP in this case.
	 */
	if (IN6_IS_ADDR_UNSPECIFIED(&myaddr)) {
		/* XXX this message should go away */
		if (debug)
			log(LOG_DEBUG, SPP_FMT "ipv6cp_open(): no IPv6 interface\n",
			    SPP_ARGS(ifp));
		return;
	}

	sp->ipv6cp.flags |= IPV6CP_MYIFID_SEEN;
	sp->ipv6cp.opts |= (1 << IPV6CP_OPT_IFID);
	sppp_open_event(&ipv6cp, sp);
}

static void
sppp_ipv6cp_close(struct sppp *sp)
{
	sppp_close_event(&ipv6cp, sp);
}

static void
sppp_ipv6cp_TO(void *cookie)
{
	sppp_to_event(&ipv6cp, (struct sppp *)cookie);
}

/*
 * Analyze a configure request.  Return true if it was agreeable, and
 * caused action sca, false if it has been rejected or nak'ed, and
 * caused action scn.  (The return value is used to make the state
 * transition decision in the state automaton.)
 */
static int
sppp_ipv6cp_RCR(struct sppp *sp, struct lcp_header *h, int len)
{
	u_char *buf, *r, *p;
	struct ifnet *ifp = SP2IFP(sp);
	int rlen, origlen, debug = ifp->if_flags & IFF_DEBUG;
	struct in6_addr myaddr, desiredaddr, suggestaddr;
	int ifidcount;
	int type;
	int collision, nohisaddr;
	char ip6buf[INET6_ADDRSTRLEN];

	len -= 4;
	origlen = len;
	/*
	 * Make sure to allocate a buf that can at least hold a
	 * conf-nak with an `address' option.  We might need it below.
	 */
	buf = r = malloc ((len < 6? 6: len), M_TEMP, M_NOWAIT);
	if (! buf)
		return (0);

	/* pass 1: see if we can recognize them */
	if (debug)
		log(LOG_DEBUG, SPP_FMT "ipv6cp parse opts:",
		    SPP_ARGS(ifp));
	p = (void*) (h+1);
	ifidcount = 0;
	for (rlen=0; len >= 2 && p[1] >= 2 && len >= p[1];
	    len-=p[1], p+=p[1]) {
		if (debug)
			log(-1, " %s", sppp_ipv6cp_opt_name(*p));
		switch (*p) {
		case IPV6CP_OPT_IFID:
			if (len >= 10 && p[1] == 10 && ifidcount == 0) {
				/* correctly formed address option */
				ifidcount++;
				continue;
			}
			if (debug)
				log(-1, " [invalid]");
			break;
#ifdef notyet
		case IPV6CP_OPT_COMPRESSION:
			if (len >= 4 && p[1] >= 4) {
				/* correctly formed compress option */
				continue;
			}
			if (debug)
				log(-1, " [invalid]");
			break;
#endif
		default:
			/* Others not supported. */
			if (debug)
				log(-1, " [rej]");
			break;
		}
		/* Add the option to rejected list. */
		bcopy (p, r, p[1]);
		r += p[1];
		rlen += p[1];
	}
	if (rlen) {
		if (debug)
			log(-1, " send conf-rej\n");
		sppp_cp_send (sp, PPP_IPV6CP, CONF_REJ, h->ident, rlen, buf);
		goto end;
	} else if (debug)
		log(-1, "\n");

	/* pass 2: parse option values */
	sppp_get_ip6_addrs(sp, &myaddr, 0, 0);
	if (debug)
		log(LOG_DEBUG, SPP_FMT "ipv6cp parse opt values: ",
		    SPP_ARGS(ifp));
	p = (void*) (h+1);
	len = origlen;
	type = CONF_ACK;
	for (rlen=0; len >= 2 && p[1] >= 2 && len >= p[1];
	    len-=p[1], p+=p[1]) {
		if (debug)
			log(-1, " %s", sppp_ipv6cp_opt_name(*p));
		switch (*p) {
#ifdef notyet
		case IPV6CP_OPT_COMPRESSION:
			continue;
#endif
		case IPV6CP_OPT_IFID:
			bzero(&desiredaddr, sizeof(desiredaddr));
			bcopy(&p[2], &desiredaddr.s6_addr[8], 8);
			collision = (bcmp(&desiredaddr.s6_addr[8],
					  &myaddr.s6_addr[8], 8) == 0);
			nohisaddr = IN6_IS_ADDR_UNSPECIFIED(&desiredaddr);

			desiredaddr.s6_addr16[0] = htons(0xfe80);
			(void)in6_setscope(&desiredaddr, SP2IFP(sp), NULL);

			if (!collision && !nohisaddr) {
				/* no collision, hisaddr known - Conf-Ack */
				type = CONF_ACK;

				if (debug) {
					log(-1, " %s [%s]",
					    ip6_sprintf(ip6buf, &desiredaddr),
					    sppp_cp_type_name(type));
				}
				continue;
			}

			bzero(&suggestaddr, sizeof(suggestaddr));
			if (collision && nohisaddr) {
				/* collision, hisaddr unknown - Conf-Rej */
				type = CONF_REJ;
				bzero(&p[2], 8);
			} else {
				/*
				 * - no collision, hisaddr unknown, or
				 * - collision, hisaddr known
				 * Conf-Nak, suggest hisaddr
				 */
				type = CONF_NAK;
				sppp_suggest_ip6_addr(sp, &suggestaddr);
				bcopy(&suggestaddr.s6_addr[8], &p[2], 8);
			}
			if (debug)
				log(-1, " %s [%s]",
				    ip6_sprintf(ip6buf, &desiredaddr),
				    sppp_cp_type_name(type));
			break;
		}
		/* Add the option to nak'ed list. */
		bcopy (p, r, p[1]);
		r += p[1];
		rlen += p[1];
	}

	if (rlen == 0 && type == CONF_ACK) {
		if (debug)
			log(-1, " send %s\n", sppp_cp_type_name(type));
		sppp_cp_send (sp, PPP_IPV6CP, type, h->ident, origlen, h+1);
	} else {
#ifdef DIAGNOSTIC
		if (type == CONF_ACK)
			panic("IPv6CP RCR: CONF_ACK with non-zero rlen");
#endif

		if (debug) {
			log(-1, " send %s suggest %s\n",
			    sppp_cp_type_name(type),
			    ip6_sprintf(ip6buf, &suggestaddr));
		}
		sppp_cp_send (sp, PPP_IPV6CP, type, h->ident, rlen, buf);
	}

 end:
	free (buf, M_TEMP);
	return (rlen == 0);
}

/*
 * Analyze the IPv6CP Configure-Reject option list, and adjust our
 * negotiation.
 */
static void
sppp_ipv6cp_RCN_rej(struct sppp *sp, struct lcp_header *h, int len)
{
	u_char *buf, *p;
	struct ifnet *ifp = SP2IFP(sp);
	int debug = ifp->if_flags & IFF_DEBUG;

	len -= 4;
	buf = malloc (len, M_TEMP, M_NOWAIT);
	if (!buf)
		return;

	if (debug)
		log(LOG_DEBUG, SPP_FMT "ipv6cp rej opts:",
		    SPP_ARGS(ifp));

	p = (void*) (h+1);
	for (; len >= 2 && p[1] >= 2 && len >= p[1];
	    len -= p[1], p += p[1]) {
		if (debug)
			log(-1, " %s", sppp_ipv6cp_opt_name(*p));
		switch (*p) {
		case IPV6CP_OPT_IFID:
			/*
			 * Peer doesn't grok address option.  This is
			 * bad.  XXX  Should we better give up here?
			 */
			sp->ipv6cp.opts &= ~(1 << IPV6CP_OPT_IFID);
			break;
#ifdef notyet
		case IPV6CP_OPT_COMPRESS:
			sp->ipv6cp.opts &= ~(1 << IPV6CP_OPT_COMPRESS);
			break;
#endif
		}
	}
	if (debug)
		log(-1, "\n");
	free (buf, M_TEMP);
	return;
}

/*
 * Analyze the IPv6CP Configure-NAK option list, and adjust our
 * negotiation.
 */
static void
sppp_ipv6cp_RCN_nak(struct sppp *sp, struct lcp_header *h, int len)
{
	u_char *buf, *p;
	struct ifnet *ifp = SP2IFP(sp);
	int debug = ifp->if_flags & IFF_DEBUG;
	struct in6_addr suggestaddr;
	char ip6buf[INET6_ADDRSTRLEN];

	len -= 4;
	buf = malloc (len, M_TEMP, M_NOWAIT);
	if (!buf)
		return;

	if (debug)
		log(LOG_DEBUG, SPP_FMT "ipv6cp nak opts:",
		    SPP_ARGS(ifp));

	p = (void*) (h+1);
	for (; len >= 2 && p[1] >= 2 && len >= p[1];
	    len -= p[1], p += p[1]) {
		if (debug)
			log(-1, " %s", sppp_ipv6cp_opt_name(*p));
		switch (*p) {
		case IPV6CP_OPT_IFID:
			/*
			 * Peer doesn't like our local ifid.  See
			 * if we can do something for him.  We'll drop
			 * him our address then.
			 */
			if (len < 10 || p[1] != 10)
				break;
			bzero(&suggestaddr, sizeof(suggestaddr));
			suggestaddr.s6_addr16[0] = htons(0xfe80);
			(void)in6_setscope(&suggestaddr, SP2IFP(sp), NULL);
			bcopy(&p[2], &suggestaddr.s6_addr[8], 8);

			sp->ipv6cp.opts |= (1 << IPV6CP_OPT_IFID);
			if (debug)
				log(-1, " [suggestaddr %s]",
				       ip6_sprintf(ip6buf, &suggestaddr));
#ifdef IPV6CP_MYIFID_DYN
			/*
			 * When doing dynamic address assignment,
			 * we accept his offer.
			 */
			if (sp->ipv6cp.flags & IPV6CP_MYIFID_DYN) {
				struct in6_addr lastsuggest;
				/*
				 * If <suggested myaddr from peer> equals to
				 * <hisaddr we have suggested last time>,
				 * we have a collision.  generate new random
				 * ifid.
				 */
				sppp_suggest_ip6_addr(&lastsuggest);
				if (IN6_ARE_ADDR_EQUAL(&suggestaddr,
						       lastsuggest)) {
					if (debug)
						log(-1, " [random]");
					sppp_gen_ip6_addr(sp, &suggestaddr);
				}
				sppp_set_ip6_addr(sp, &suggestaddr, 0);
				if (debug)
					log(-1, " [agree]");
				sp->ipv6cp.flags |= IPV6CP_MYIFID_SEEN;
			}
#else
			/*
			 * Since we do not do dynamic address assignment,
			 * we ignore it and thus continue to negotiate
			 * our already existing value.  This can possibly
			 * go into infinite request-reject loop.
			 *
			 * This is not likely because we normally use
			 * ifid based on MAC-address.
			 * If you have no ethernet card on the node, too bad.
			 * XXX should we use fail_counter?
			 */
#endif
			break;
#ifdef notyet
		case IPV6CP_OPT_COMPRESS:
			/*
			 * Peer wants different compression parameters.
			 */
			break;
#endif
		}
	}
	if (debug)
		log(-1, "\n");
	free (buf, M_TEMP);
	return;
}
static void
sppp_ipv6cp_tlu(struct sppp *sp)
{
	/* we are up - notify isdn daemon */
	if (sp->pp_con)
		sp->pp_con(sp);
}

static void
sppp_ipv6cp_tld(struct sppp *sp)
{
}

static void
sppp_ipv6cp_tls(struct sppp *sp)
{
	/* indicate to LCP that it must stay alive */
	sp->lcp.protos |= (1 << IDX_IPV6CP);
}

static void
sppp_ipv6cp_tlf(struct sppp *sp)
{

#if 0	/* need #if 0 to close IPv6CP properly */
	/* we no longer need LCP */
	sp->lcp.protos &= ~(1 << IDX_IPV6CP);
	sppp_lcp_check_and_close(sp);
#endif
}

static void
sppp_ipv6cp_scr(struct sppp *sp)
{
	char opt[10 /* ifid */ + 4 /* compression, minimum */];
	struct in6_addr ouraddr;
	int i = 0;

	if (sp->ipv6cp.opts & (1 << IPV6CP_OPT_IFID)) {
		sppp_get_ip6_addrs(sp, &ouraddr, 0, 0);
		opt[i++] = IPV6CP_OPT_IFID;
		opt[i++] = 10;
		bcopy(&ouraddr.s6_addr[8], &opt[i], 8);
		i += 8;
	}

#ifdef notyet
	if (sp->ipv6cp.opts & (1 << IPV6CP_OPT_COMPRESSION)) {
		opt[i++] = IPV6CP_OPT_COMPRESSION;
		opt[i++] = 4;
		opt[i++] = 0;   /* TBD */
		opt[i++] = 0;   /* TBD */
		/* variable length data may follow */
	}
#endif

	sp->confid[IDX_IPV6CP] = ++sp->pp_seq[IDX_IPV6CP];
	sppp_cp_send(sp, PPP_IPV6CP, CONF_REQ, sp->confid[IDX_IPV6CP], i, &opt);
}
#else /*INET6*/
static void sppp_ipv6cp_init(struct sppp *sp)
{
}

static void sppp_ipv6cp_up(struct sppp *sp)
{
}

static void sppp_ipv6cp_down(struct sppp *sp)
{
}


static void sppp_ipv6cp_open(struct sppp *sp)
{
}

static void sppp_ipv6cp_close(struct sppp *sp)
{
}

static void sppp_ipv6cp_TO(void *sp)
{
}

static int sppp_ipv6cp_RCR(struct sppp *sp, struct lcp_header *h, int len)
{
	return 0;
}

static void sppp_ipv6cp_RCN_rej(struct sppp *sp, struct lcp_header *h, int len)
{
}

static void sppp_ipv6cp_RCN_nak(struct sppp *sp, struct lcp_header *h, int len)
{
}

static void sppp_ipv6cp_tlu(struct sppp *sp)
{
}

static void sppp_ipv6cp_tld(struct sppp *sp)
{
}

static void sppp_ipv6cp_tls(struct sppp *sp)
{
}

static void sppp_ipv6cp_tlf(struct sppp *sp)
{
}

static void sppp_ipv6cp_scr(struct sppp *sp)
{
}
#endif /*INET6*/

/*
 *--------------------------------------------------------------------------*
 *                                                                          *
 *                        The CHAP implementation.                          *
 *                                                                          *
 *--------------------------------------------------------------------------*
 */

/*
 * The authentication protocols don't employ a full-fledged state machine as
 * the control protocols do, since they do have Open and Close events, but
 * not Up and Down, nor are they explicitly terminated.  Also, use of the
 * authentication protocols may be different in both directions (this makes
 * sense, think of a machine that never accepts incoming calls but only
 * calls out, it doesn't require the called party to authenticate itself).
 *
 * Our state machine for the local authentication protocol (we are requesting
 * the peer to authenticate) looks like:
 *
 *						    RCA-
 *	      +--------------------------------------------+
 *	      V					    scn,tld|
 *	  +--------+			       Close   +---------+ RCA+
 *	  |	   |<----------------------------------|	 |------+
 *   +--->| Closed |				TO*    | Opened	 | sca	|
 *   |	  |	   |-----+		       +-------|	 |<-----+
 *   |	  +--------+ irc |		       |       +---------+
 *   |	    ^		 |		       |	   ^
 *   |	    |		 |		       |	   |
 *   |	    |		 |		       |	   |
 *   |	 TO-|		 |		       |	   |
 *   |	    |tld  TO+	 V		       |	   |
 *   |	    |	+------->+		       |	   |
 *   |	    |	|	 |		       |	   |
 *   |	  +--------+	 V		       |	   |
 *   |	  |	   |<----+<--------------------+	   |
 *   |	  | Req-   | scr				   |
 *   |	  | Sent   |					   |
 *   |	  |	   |					   |
 *   |	  +--------+					   |
 *   | RCA- |	| RCA+					   |
 *   +------+	+------------------------------------------+
 *   scn,tld	  sca,irc,ict,tlu
 *
 *
 *   with:
 *
 *	Open:	LCP reached authentication phase
 *	Close:	LCP reached terminate phase
 *
 *	RCA+:	received reply (pap-req, chap-response), acceptable
 *	RCN:	received reply (pap-req, chap-response), not acceptable
 *	TO+:	timeout with restart counter >= 0
 *	TO-:	timeout with restart counter < 0
 *	TO*:	reschedule timeout for CHAP
 *
 *	scr:	send request packet (none for PAP, chap-challenge)
 *	sca:	send ack packet (pap-ack, chap-success)
 *	scn:	send nak packet (pap-nak, chap-failure)
 *	ict:	initialize re-challenge timer (CHAP only)
 *
 *	tlu:	this-layer-up, LCP reaches network phase
 *	tld:	this-layer-down, LCP enters terminate phase
 *
 * Note that in CHAP mode, after sending a new challenge, while the state
 * automaton falls back into Req-Sent state, it doesn't signal a tld
 * event to LCP, so LCP remains in network phase.  Only after not getting
 * any response (or after getting an unacceptable response), CHAP closes,
 * causing LCP to enter terminate phase.
 *
 * With PAP, there is no initial request that can be sent.  The peer is
 * expected to send one based on the successful negotiation of PAP as
 * the authentication protocol during the LCP option negotiation.
 *
 * Incoming authentication protocol requests (remote requests
 * authentication, we are peer) don't employ a state machine at all,
 * they are simply answered.  Some peers [Ascend P50 firmware rev
 * 4.50] react allergically when sending IPCP requests while they are
 * still in authentication phase (thereby violating the standard that
 * demands that these NCP packets are to be discarded), so we keep
 * track of the peer demanding us to authenticate, and only proceed to
 * phase network once we've seen a positive acknowledge for the
 * authentication.
 */

/*
 * Handle incoming CHAP packets.
 */
static void
sppp_chap_input(struct sppp *sp, struct mbuf *m)
{
	STDDCL;
	struct lcp_header *h;
	int len;
	u_char *value, *name, digest[AUTHKEYLEN], dsize;
	int value_len, name_len;
	MD5_CTX ctx;

	len = m->m_pkthdr.len;
	if (len < 4) {
		if (debug)
			log(LOG_DEBUG,
			    SPP_FMT "chap invalid packet length: %d bytes\n",
			    SPP_ARGS(ifp), len);
		return;
	}
	h = mtod (m, struct lcp_header*);
	if (len > ntohs (h->len))
		len = ntohs (h->len);

	switch (h->type) {
	/* challenge, failure and success are his authproto */
	case CHAP_CHALLENGE:
		value = 1 + (u_char*)(h+1);
		value_len = value[-1];
		name = value + value_len;
		name_len = len - value_len - 5;
		if (name_len < 0) {
			if (debug) {
				log(LOG_DEBUG,
				    SPP_FMT "chap corrupted challenge "
				    "<%s id=0x%x len=%d",
				    SPP_ARGS(ifp),
				    sppp_auth_type_name(PPP_CHAP, h->type),
				    h->ident, ntohs(h->len));
				sppp_print_bytes((u_char*) (h+1), len-4);
				log(-1, ">\n");
			}
			break;
		}

		if (debug) {
			log(LOG_DEBUG,
			    SPP_FMT "chap input <%s id=0x%x len=%d name=",
			    SPP_ARGS(ifp),
			    sppp_auth_type_name(PPP_CHAP, h->type), h->ident,
			    ntohs(h->len));
			sppp_print_string((char*) name, name_len);
			log(-1, " value-size=%d value=", value_len);
			sppp_print_bytes(value, value_len);
			log(-1, ">\n");
		}

		/* Compute reply value. */
		MD5Init(&ctx);
		MD5Update(&ctx, &h->ident, 1);
		MD5Update(&ctx, sp->myauth.secret,
			  sppp_strnlen(sp->myauth.secret, AUTHKEYLEN));
		MD5Update(&ctx, value, value_len);
		MD5Final(digest, &ctx);
		dsize = sizeof digest;

		sppp_auth_send(&chap, sp, CHAP_RESPONSE, h->ident,
			       sizeof dsize, (const char *)&dsize,
			       sizeof digest, digest,
			       (size_t)sppp_strnlen(sp->myauth.name, AUTHNAMELEN),
			       sp->myauth.name,
			       0);
		break;

	case CHAP_SUCCESS:
		if (debug) {
			log(LOG_DEBUG, SPP_FMT "chap success",
			    SPP_ARGS(ifp));
			if (len > 4) {
				log(-1, ": ");
				sppp_print_string((char*)(h + 1), len - 4);
			}
			log(-1, "\n");
		}
		SPPP_LOCK(sp);
		sp->pp_flags &= ~PP_NEEDAUTH;
		if (sp->myauth.proto == PPP_CHAP &&
		    (sp->lcp.opts & (1 << LCP_OPT_AUTH_PROTO)) &&
		    (sp->lcp.protos & (1 << IDX_CHAP)) == 0) {
			/*
			 * We are authenticator for CHAP but didn't
			 * complete yet.  Leave it to tlu to proceed
			 * to network phase.
			 */
			SPPP_UNLOCK(sp);
			break;
		}
		SPPP_UNLOCK(sp);
		sppp_phase_network(sp);
		break;

	case CHAP_FAILURE:
		if (debug) {
			log(LOG_INFO, SPP_FMT "chap failure",
			    SPP_ARGS(ifp));
			if (len > 4) {
				log(-1, ": ");
				sppp_print_string((char*)(h + 1), len - 4);
			}
			log(-1, "\n");
		} else
			log(LOG_INFO, SPP_FMT "chap failure\n",
			    SPP_ARGS(ifp));
		/* await LCP shutdown by authenticator */
		break;

	/* response is my authproto */
	case CHAP_RESPONSE:
		value = 1 + (u_char*)(h+1);
		value_len = value[-1];
		name = value + value_len;
		name_len = len - value_len - 5;
		if (name_len < 0) {
			if (debug) {
				log(LOG_DEBUG,
				    SPP_FMT "chap corrupted response "
				    "<%s id=0x%x len=%d",
				    SPP_ARGS(ifp),
				    sppp_auth_type_name(PPP_CHAP, h->type),
				    h->ident, ntohs(h->len));
				sppp_print_bytes((u_char*)(h+1), len-4);
				log(-1, ">\n");
			}
			break;
		}
		if (h->ident != sp->confid[IDX_CHAP]) {
			if (debug)
				log(LOG_DEBUG,
				    SPP_FMT "chap dropping response for old ID "
				    "(got %d, expected %d)\n",
				    SPP_ARGS(ifp),
				    h->ident, sp->confid[IDX_CHAP]);
			break;
		}
		if (name_len != sppp_strnlen(sp->hisauth.name, AUTHNAMELEN)
		    || bcmp(name, sp->hisauth.name, name_len) != 0) {
			log(LOG_INFO, SPP_FMT "chap response, his name ",
			    SPP_ARGS(ifp));
			sppp_print_string(name, name_len);
			log(-1, " != expected ");
			sppp_print_string(sp->hisauth.name,
					  sppp_strnlen(sp->hisauth.name, AUTHNAMELEN));
			log(-1, "\n");
		}
		if (debug) {
			log(LOG_DEBUG, SPP_FMT "chap input(%s) "
			    "<%s id=0x%x len=%d name=",
			    SPP_ARGS(ifp),
			    sppp_state_name(sp->state[IDX_CHAP]),
			    sppp_auth_type_name(PPP_CHAP, h->type),
			    h->ident, ntohs (h->len));
			sppp_print_string((char*)name, name_len);
			log(-1, " value-size=%d value=", value_len);
			sppp_print_bytes(value, value_len);
			log(-1, ">\n");
		}
		if (value_len != AUTHKEYLEN) {
			if (debug)
				log(LOG_DEBUG,
				    SPP_FMT "chap bad hash value length: "
				    "%d bytes, should be %d\n",
				    SPP_ARGS(ifp), value_len,
				    AUTHKEYLEN);
			break;
		}

		MD5Init(&ctx);
		MD5Update(&ctx, &h->ident, 1);
		MD5Update(&ctx, sp->hisauth.secret,
			  sppp_strnlen(sp->hisauth.secret, AUTHKEYLEN));
		MD5Update(&ctx, sp->myauth.challenge, AUTHKEYLEN);
		MD5Final(digest, &ctx);

#define FAILMSG "Failed..."
#define SUCCMSG "Welcome!"

		if (value_len != sizeof digest ||
		    bcmp(digest, value, value_len) != 0) {
			/* action scn, tld */
			sppp_auth_send(&chap, sp, CHAP_FAILURE, h->ident,
				       sizeof(FAILMSG) - 1, (u_char *)FAILMSG,
				       0);
			chap.tld(sp);
			break;
		}
		/* action sca, perhaps tlu */
		if (sp->state[IDX_CHAP] == STATE_REQ_SENT ||
		    sp->state[IDX_CHAP] == STATE_OPENED)
			sppp_auth_send(&chap, sp, CHAP_SUCCESS, h->ident,
				       sizeof(SUCCMSG) - 1, (u_char *)SUCCMSG,
				       0);
		if (sp->state[IDX_CHAP] == STATE_REQ_SENT) {
			sppp_cp_change_state(&chap, sp, STATE_OPENED);
			chap.tlu(sp);
		}
		break;

	default:
		/* Unknown CHAP packet type -- ignore. */
		if (debug) {
			log(LOG_DEBUG, SPP_FMT "chap unknown input(%s) "
			    "<0x%x id=0x%xh len=%d",
			    SPP_ARGS(ifp),
			    sppp_state_name(sp->state[IDX_CHAP]),
			    h->type, h->ident, ntohs(h->len));
			sppp_print_bytes((u_char*)(h+1), len-4);
			log(-1, ">\n");
		}
		break;

	}
}

static void
sppp_chap_init(struct sppp *sp)
{
	/* Chap doesn't have STATE_INITIAL at all. */
	sp->state[IDX_CHAP] = STATE_CLOSED;
	sp->fail_counter[IDX_CHAP] = 0;
	sp->pp_seq[IDX_CHAP] = 0;
	sp->pp_rseq[IDX_CHAP] = 0;
 	callout_init(&sp->ch[IDX_CHAP], 1);
}

static void
sppp_chap_open(struct sppp *sp)
{
	if (sp->myauth.proto == PPP_CHAP &&
	    (sp->lcp.opts & (1 << LCP_OPT_AUTH_PROTO)) != 0) {
		/* we are authenticator for CHAP, start it */
		chap.scr(sp);
		sp->rst_counter[IDX_CHAP] = sp->lcp.max_configure;
		sppp_cp_change_state(&chap, sp, STATE_REQ_SENT);
	}
	/* nothing to be done if we are peer, await a challenge */
}

static void
sppp_chap_close(struct sppp *sp)
{
	if (sp->state[IDX_CHAP] != STATE_CLOSED)
		sppp_cp_change_state(&chap, sp, STATE_CLOSED);
}

static void
sppp_chap_TO(void *cookie)
{
	struct sppp *sp = (struct sppp *)cookie;
	STDDCL;

	SPPP_LOCK(sp);
	if (debug)
		log(LOG_DEBUG, SPP_FMT "chap TO(%s) rst_counter = %d\n",
		    SPP_ARGS(ifp),
		    sppp_state_name(sp->state[IDX_CHAP]),
		    sp->rst_counter[IDX_CHAP]);

	if (--sp->rst_counter[IDX_CHAP] < 0)
		/* TO- event */
		switch (sp->state[IDX_CHAP]) {
		case STATE_REQ_SENT:
			chap.tld(sp);
			sppp_cp_change_state(&chap, sp, STATE_CLOSED);
			break;
		}
	else
		/* TO+ (or TO*) event */
		switch (sp->state[IDX_CHAP]) {
		case STATE_OPENED:
			/* TO* event */
			sp->rst_counter[IDX_CHAP] = sp->lcp.max_configure;
			/* FALLTHROUGH */
		case STATE_REQ_SENT:
			chap.scr(sp);
			/* sppp_cp_change_state() will restart the timer */
			sppp_cp_change_state(&chap, sp, STATE_REQ_SENT);
			break;
		}

	SPPP_UNLOCK(sp);
}

static void
sppp_chap_tlu(struct sppp *sp)
{
	STDDCL;
	int i;

	i = 0;
	sp->rst_counter[IDX_CHAP] = sp->lcp.max_configure;

	/*
	 * Some broken CHAP implementations (Conware CoNet, firmware
	 * 4.0.?) don't want to re-authenticate their CHAP once the
	 * initial challenge-response exchange has taken place.
	 * Provide for an option to avoid rechallenges.
	 */
	if ((sp->hisauth.flags & AUTHFLAG_NORECHALLENGE) == 0) {
		/*
		 * Compute the re-challenge timeout.  This will yield
		 * a number between 300 and 810 seconds.
		 */
		i = 300 + ((unsigned)(random() & 0xff00) >> 7);
		callout_reset(&sp->ch[IDX_CHAP], i * hz, chap.TO, (void *)sp);
	}

	if (debug) {
		log(LOG_DEBUG,
		    SPP_FMT "chap %s, ",
		    SPP_ARGS(ifp),
		    sp->pp_phase == PHASE_NETWORK? "reconfirmed": "tlu");
		if ((sp->hisauth.flags & AUTHFLAG_NORECHALLENGE) == 0)
			log(-1, "next re-challenge in %d seconds\n", i);
		else
			log(-1, "re-challenging suppressed\n");
	}

	SPPP_LOCK(sp);
	/* indicate to LCP that we need to be closed down */
	sp->lcp.protos |= (1 << IDX_CHAP);

	if (sp->pp_flags & PP_NEEDAUTH) {
		/*
		 * Remote is authenticator, but his auth proto didn't
		 * complete yet.  Defer the transition to network
		 * phase.
		 */
		SPPP_UNLOCK(sp);
		return;
	}
	SPPP_UNLOCK(sp);

	/*
	 * If we are already in phase network, we are done here.  This
	 * is the case if this is a dummy tlu event after a re-challenge.
	 */
	if (sp->pp_phase != PHASE_NETWORK)
		sppp_phase_network(sp);
}

static void
sppp_chap_tld(struct sppp *sp)
{
	STDDCL;

	if (debug)
		log(LOG_DEBUG, SPP_FMT "chap tld\n", SPP_ARGS(ifp));
	callout_stop(&sp->ch[IDX_CHAP]);
	sp->lcp.protos &= ~(1 << IDX_CHAP);

	lcp.Close(sp);
}

static void
sppp_chap_scr(struct sppp *sp)
{
	u_long *ch, seed;
	u_char clen;

	/* Compute random challenge. */
	ch = (u_long *)sp->myauth.challenge;
	read_random(&seed, sizeof seed);
	ch[0] = seed ^ random();
	ch[1] = seed ^ random();
	ch[2] = seed ^ random();
	ch[3] = seed ^ random();
	clen = AUTHKEYLEN;

	sp->confid[IDX_CHAP] = ++sp->pp_seq[IDX_CHAP];

	sppp_auth_send(&chap, sp, CHAP_CHALLENGE, sp->confid[IDX_CHAP],
		       sizeof clen, (const char *)&clen,
		       (size_t)AUTHKEYLEN, sp->myauth.challenge,
		       (size_t)sppp_strnlen(sp->myauth.name, AUTHNAMELEN),
		       sp->myauth.name,
		       0);
}

/*
 *--------------------------------------------------------------------------*
 *                                                                          *
 *                        The PAP implementation.                           *
 *                                                                          *
 *--------------------------------------------------------------------------*
 */
/*
 * For PAP, we need to keep a little state also if we are the peer, not the
 * authenticator.  This is since we don't get a request to authenticate, but
 * have to repeatedly authenticate ourself until we got a response (or the
 * retry counter is expired).
 */

/*
 * Handle incoming PAP packets.  */
static void
sppp_pap_input(struct sppp *sp, struct mbuf *m)
{
	STDDCL;
	struct lcp_header *h;
	int len;
	u_char *name, *passwd, mlen;
	int name_len, passwd_len;

	len = m->m_pkthdr.len;
	if (len < 5) {
		if (debug)
			log(LOG_DEBUG,
			    SPP_FMT "pap invalid packet length: %d bytes\n",
			    SPP_ARGS(ifp), len);
		return;
	}
	h = mtod (m, struct lcp_header*);
	if (len > ntohs (h->len))
		len = ntohs (h->len);
	switch (h->type) {
	/* PAP request is my authproto */
	case PAP_REQ:
		name = 1 + (u_char*)(h+1);
		name_len = name[-1];
		passwd = name + name_len + 1;
		if (name_len > len - 6 ||
		    (passwd_len = passwd[-1]) > len - 6 - name_len) {
			if (debug) {
				log(LOG_DEBUG, SPP_FMT "pap corrupted input "
				    "<%s id=0x%x len=%d",
				    SPP_ARGS(ifp),
				    sppp_auth_type_name(PPP_PAP, h->type),
				    h->ident, ntohs(h->len));
				sppp_print_bytes((u_char*)(h+1), len-4);
				log(-1, ">\n");
			}
			break;
		}
		if (debug) {
			log(LOG_DEBUG, SPP_FMT "pap input(%s) "
			    "<%s id=0x%x len=%d name=",
			    SPP_ARGS(ifp),
			    sppp_state_name(sp->state[IDX_PAP]),
			    sppp_auth_type_name(PPP_PAP, h->type),
			    h->ident, ntohs(h->len));
			sppp_print_string((char*)name, name_len);
			log(-1, " passwd=");
			sppp_print_string((char*)passwd, passwd_len);
			log(-1, ">\n");
		}
		if (name_len != sppp_strnlen(sp->hisauth.name, AUTHNAMELEN) ||
		    passwd_len != sppp_strnlen(sp->hisauth.secret, AUTHKEYLEN) ||
		    bcmp(name, sp->hisauth.name, name_len) != 0 ||
		    bcmp(passwd, sp->hisauth.secret, passwd_len) != 0) {
			/* action scn, tld */
			mlen = sizeof(FAILMSG) - 1;
			sppp_auth_send(&pap, sp, PAP_NAK, h->ident,
				       sizeof mlen, (const char *)&mlen,
				       sizeof(FAILMSG) - 1, (u_char *)FAILMSG,
				       0);
			pap.tld(sp);
			break;
		}
		/* action sca, perhaps tlu */
		if (sp->state[IDX_PAP] == STATE_REQ_SENT ||
		    sp->state[IDX_PAP] == STATE_OPENED) {
			mlen = sizeof(SUCCMSG) - 1;
			sppp_auth_send(&pap, sp, PAP_ACK, h->ident,
				       sizeof mlen, (const char *)&mlen,
				       sizeof(SUCCMSG) - 1, (u_char *)SUCCMSG,
				       0);
		}
		if (sp->state[IDX_PAP] == STATE_REQ_SENT) {
			sppp_cp_change_state(&pap, sp, STATE_OPENED);
			pap.tlu(sp);
		}
		break;

	/* ack and nak are his authproto */
	case PAP_ACK:
		callout_stop(&sp->pap_my_to_ch);
		if (debug) {
			log(LOG_DEBUG, SPP_FMT "pap success",
			    SPP_ARGS(ifp));
			name_len = *((char *)h);
			if (len > 5 && name_len) {
				log(-1, ": ");
				sppp_print_string((char*)(h+1), name_len);
			}
			log(-1, "\n");
		}
		SPPP_LOCK(sp);
		sp->pp_flags &= ~PP_NEEDAUTH;
		if (sp->myauth.proto == PPP_PAP &&
		    (sp->lcp.opts & (1 << LCP_OPT_AUTH_PROTO)) &&
		    (sp->lcp.protos & (1 << IDX_PAP)) == 0) {
			/*
			 * We are authenticator for PAP but didn't
			 * complete yet.  Leave it to tlu to proceed
			 * to network phase.
			 */
			SPPP_UNLOCK(sp);
			break;
		}
		SPPP_UNLOCK(sp);
		sppp_phase_network(sp);
		break;

	case PAP_NAK:
		callout_stop (&sp->pap_my_to_ch);
		if (debug) {
			log(LOG_INFO, SPP_FMT "pap failure",
			    SPP_ARGS(ifp));
			name_len = *((char *)h);
			if (len > 5 && name_len) {
				log(-1, ": ");
				sppp_print_string((char*)(h+1), name_len);
			}
			log(-1, "\n");
		} else
			log(LOG_INFO, SPP_FMT "pap failure\n",
			    SPP_ARGS(ifp));
		/* await LCP shutdown by authenticator */
		break;

	default:
		/* Unknown PAP packet type -- ignore. */
		if (debug) {
			log(LOG_DEBUG, SPP_FMT "pap corrupted input "
			    "<0x%x id=0x%x len=%d",
			    SPP_ARGS(ifp),
			    h->type, h->ident, ntohs(h->len));
			sppp_print_bytes((u_char*)(h+1), len-4);
			log(-1, ">\n");
		}
		break;

	}
}

static void
sppp_pap_init(struct sppp *sp)
{
	/* PAP doesn't have STATE_INITIAL at all. */
	sp->state[IDX_PAP] = STATE_CLOSED;
	sp->fail_counter[IDX_PAP] = 0;
	sp->pp_seq[IDX_PAP] = 0;
	sp->pp_rseq[IDX_PAP] = 0;
 	callout_init(&sp->ch[IDX_PAP], 1);
 	callout_init(&sp->pap_my_to_ch, 1);
}

static void
sppp_pap_open(struct sppp *sp)
{
	if (sp->hisauth.proto == PPP_PAP &&
	    (sp->lcp.opts & (1 << LCP_OPT_AUTH_PROTO)) != 0) {
		/* we are authenticator for PAP, start our timer */
		sp->rst_counter[IDX_PAP] = sp->lcp.max_configure;
		sppp_cp_change_state(&pap, sp, STATE_REQ_SENT);
	}
	if (sp->myauth.proto == PPP_PAP) {
		/* we are peer, send a request, and start a timer */
		pap.scr(sp);
		callout_reset(&sp->pap_my_to_ch, sp->lcp.timeout,
			      sppp_pap_my_TO, (void *)sp);
	}
}

static void
sppp_pap_close(struct sppp *sp)
{
	if (sp->state[IDX_PAP] != STATE_CLOSED)
		sppp_cp_change_state(&pap, sp, STATE_CLOSED);
}

/*
 * That's the timeout routine if we are authenticator.  Since the
 * authenticator is basically passive in PAP, we can't do much here.
 */
static void
sppp_pap_TO(void *cookie)
{
	struct sppp *sp = (struct sppp *)cookie;
	STDDCL;

	SPPP_LOCK(sp);
	if (debug)
		log(LOG_DEBUG, SPP_FMT "pap TO(%s) rst_counter = %d\n",
		    SPP_ARGS(ifp),
		    sppp_state_name(sp->state[IDX_PAP]),
		    sp->rst_counter[IDX_PAP]);

	if (--sp->rst_counter[IDX_PAP] < 0)
		/* TO- event */
		switch (sp->state[IDX_PAP]) {
		case STATE_REQ_SENT:
			pap.tld(sp);
			sppp_cp_change_state(&pap, sp, STATE_CLOSED);
			break;
		}
	else
		/* TO+ event, not very much we could do */
		switch (sp->state[IDX_PAP]) {
		case STATE_REQ_SENT:
			/* sppp_cp_change_state() will restart the timer */
			sppp_cp_change_state(&pap, sp, STATE_REQ_SENT);
			break;
		}

	SPPP_UNLOCK(sp);
}

/*
 * That's the timeout handler if we are peer.  Since the peer is active,
 * we need to retransmit our PAP request since it is apparently lost.
 * XXX We should impose a max counter.
 */
static void
sppp_pap_my_TO(void *cookie)
{
	struct sppp *sp = (struct sppp *)cookie;
	STDDCL;

	if (debug)
		log(LOG_DEBUG, SPP_FMT "pap peer TO\n",
		    SPP_ARGS(ifp));

	SPPP_LOCK(sp);
	pap.scr(sp);
	SPPP_UNLOCK(sp);
}

static void
sppp_pap_tlu(struct sppp *sp)
{
	STDDCL;

	sp->rst_counter[IDX_PAP] = sp->lcp.max_configure;

	if (debug)
		log(LOG_DEBUG, SPP_FMT "%s tlu\n",
		    SPP_ARGS(ifp), pap.name);

	SPPP_LOCK(sp);
	/* indicate to LCP that we need to be closed down */
	sp->lcp.protos |= (1 << IDX_PAP);

	if (sp->pp_flags & PP_NEEDAUTH) {
		/*
		 * Remote is authenticator, but his auth proto didn't
		 * complete yet.  Defer the transition to network
		 * phase.
		 */
		SPPP_UNLOCK(sp);
		return;
	}
	SPPP_UNLOCK(sp);
	sppp_phase_network(sp);
}

static void
sppp_pap_tld(struct sppp *sp)
{
	STDDCL;

	if (debug)
		log(LOG_DEBUG, SPP_FMT "pap tld\n", SPP_ARGS(ifp));
	callout_stop (&sp->ch[IDX_PAP]);
	callout_stop (&sp->pap_my_to_ch);
	sp->lcp.protos &= ~(1 << IDX_PAP);

	lcp.Close(sp);
}

static void
sppp_pap_scr(struct sppp *sp)
{
	u_char idlen, pwdlen;

	sp->confid[IDX_PAP] = ++sp->pp_seq[IDX_PAP];
	pwdlen = sppp_strnlen(sp->myauth.secret, AUTHKEYLEN);
	idlen = sppp_strnlen(sp->myauth.name, AUTHNAMELEN);

	sppp_auth_send(&pap, sp, PAP_REQ, sp->confid[IDX_PAP],
		       sizeof idlen, (const char *)&idlen,
		       (size_t)idlen, sp->myauth.name,
		       sizeof pwdlen, (const char *)&pwdlen,
		       (size_t)pwdlen, sp->myauth.secret,
		       0);
}

/*
 * Random miscellaneous functions.
 */

/*
 * Send a PAP or CHAP proto packet.
 *
 * Varadic function, each of the elements for the ellipsis is of type
 * ``size_t mlen, const u_char *msg''.  Processing will stop iff
 * mlen == 0.
 * NOTE: never declare variadic functions with types subject to type
 * promotion (i.e. u_char). This is asking for big trouble depending
 * on the architecture you are on...
 */

static void
sppp_auth_send(const struct cp *cp, struct sppp *sp,
               unsigned int type, unsigned int id,
	       ...)
{
	STDDCL;
	struct ppp_header *h;
	struct lcp_header *lh;
	struct mbuf *m;
	u_char *p;
	int len;
	unsigned int mlen;
	const char *msg;
	va_list ap;

	MGETHDR (m, M_NOWAIT, MT_DATA);
	if (! m)
		return;
	m->m_pkthdr.rcvif = 0;

	h = mtod (m, struct ppp_header*);
	h->address = PPP_ALLSTATIONS;		/* broadcast address */
	h->control = PPP_UI;			/* Unnumbered Info */
	h->protocol = htons(cp->proto);

	lh = (struct lcp_header*)(h + 1);
	lh->type = type;
	lh->ident = id;
	p = (u_char*) (lh+1);

	va_start(ap, id);
	len = 0;

	while ((mlen = (unsigned int)va_arg(ap, size_t)) != 0) {
		msg = va_arg(ap, const char *);
		len += mlen;
		if (len > MHLEN - PPP_HEADER_LEN - LCP_HEADER_LEN) {
			va_end(ap);
			m_freem(m);
			return;
		}

		bcopy(msg, p, mlen);
		p += mlen;
	}
	va_end(ap);

	m->m_pkthdr.len = m->m_len = PPP_HEADER_LEN + LCP_HEADER_LEN + len;
	lh->len = htons (LCP_HEADER_LEN + len);

	if (debug) {
		log(LOG_DEBUG, SPP_FMT "%s output <%s id=0x%x len=%d",
		    SPP_ARGS(ifp), cp->name,
		    sppp_auth_type_name(cp->proto, lh->type),
		    lh->ident, ntohs(lh->len));
		sppp_print_bytes((u_char*) (lh+1), len);
		log(-1, ">\n");
	}
	if (! IF_HANDOFF_ADJ(&sp->pp_cpq, m, ifp, 3))
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
}

/*
 * Flush interface queue.
 */
static void
sppp_qflush(struct ifqueue *ifq)
{
	struct mbuf *m, *n;

	n = ifq->ifq_head;
	while ((m = n)) {
		n = m->m_nextpkt;
		m_freem (m);
	}
	ifq->ifq_head = 0;
	ifq->ifq_tail = 0;
	ifq->ifq_len = 0;
}

/*
 * Send keepalive packets, every 10 seconds.
 */
static void
sppp_keepalive(void *dummy)
{
	struct sppp *sp = (struct sppp*)dummy;
	struct ifnet *ifp = SP2IFP(sp);

	SPPP_LOCK(sp);
	/* Keepalive mode disabled or channel down? */
	if (! (sp->pp_flags & PP_KEEPALIVE) ||
	    ! (ifp->if_drv_flags & IFF_DRV_RUNNING))
		goto out;

	if (sp->pp_mode == PP_FR) {
		sppp_fr_keepalive (sp);
		goto out;
	}

	/* No keepalive in PPP mode if LCP not opened yet. */
	if (sp->pp_mode != IFF_CISCO &&
	    sp->pp_phase < PHASE_AUTHENTICATE)
		goto out;

	if (sp->pp_alivecnt == MAXALIVECNT) {
		/* No keepalive packets got.  Stop the interface. */
		printf (SPP_FMT "down\n", SPP_ARGS(ifp));
		if_down (ifp);
		sppp_qflush (&sp->pp_cpq);
		if (sp->pp_mode != IFF_CISCO) {
			/* XXX */
			/* Shut down the PPP link. */
			lcp.Down(sp);
			/* Initiate negotiation. XXX */
			lcp.Up(sp);
		}
	}
	if (sp->pp_alivecnt <= MAXALIVECNT)
		++sp->pp_alivecnt;
	if (sp->pp_mode == IFF_CISCO)
		sppp_cisco_send (sp, CISCO_KEEPALIVE_REQ,
			 ++sp->pp_seq[IDX_LCP],	sp->pp_rseq[IDX_LCP]);
	else if (sp->pp_phase >= PHASE_AUTHENTICATE) {
		long nmagic = htonl (sp->lcp.magic);
		sp->lcp.echoid = ++sp->pp_seq[IDX_LCP];
		sppp_cp_send (sp, PPP_LCP, ECHO_REQ,
			sp->lcp.echoid, 4, &nmagic);
	}
out:
	SPPP_UNLOCK(sp);
 	callout_reset(&sp->keepalive_callout, hz * 10, sppp_keepalive,
		      (void *)sp);
}

/*
 * Get both IP addresses.
 */
void
sppp_get_ip_addrs(struct sppp *sp, u_long *src, u_long *dst, u_long *srcmask)
{
	struct ifnet *ifp = SP2IFP(sp);
	struct ifaddr *ifa;
	struct sockaddr_in *si, *sm;
	u_long ssrc, ddst;

	sm = NULL;
	ssrc = ddst = 0L;
	/*
	 * Pick the first AF_INET address from the list,
	 * aliases don't make any sense on a p2p link anyway.
	 */
	si = NULL;
	if_addr_rlock(ifp);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link)
		if (ifa->ifa_addr->sa_family == AF_INET) {
			si = (struct sockaddr_in *)ifa->ifa_addr;
			sm = (struct sockaddr_in *)ifa->ifa_netmask;
			if (si)
				break;
		}
	if (ifa) {
		if (si && si->sin_addr.s_addr) {
			ssrc = si->sin_addr.s_addr;
			if (srcmask)
				*srcmask = ntohl(sm->sin_addr.s_addr);
		}

		si = (struct sockaddr_in *)ifa->ifa_dstaddr;
		if (si && si->sin_addr.s_addr)
			ddst = si->sin_addr.s_addr;
	}
	if_addr_runlock(ifp);

	if (dst) *dst = ntohl(ddst);
	if (src) *src = ntohl(ssrc);
}

#ifdef INET
/*
 * Set my IP address.
 */
static void
sppp_set_ip_addr(struct sppp *sp, u_long src)
{
	STDDCL;
	struct ifaddr *ifa;
	struct sockaddr_in *si;
	struct in_ifaddr *ia;

	/*
	 * Pick the first AF_INET address from the list,
	 * aliases don't make any sense on a p2p link anyway.
	 */
	si = NULL;
	if_addr_rlock(ifp);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		if (ifa->ifa_addr->sa_family == AF_INET) {
			si = (struct sockaddr_in *)ifa->ifa_addr;
			if (si != NULL) {
				ifa_ref(ifa);
				break;
			}
		}
	}
	if_addr_runlock(ifp);

	if (ifa != NULL) {
		int error;

		/* delete old route */
		error = rtinit(ifa, (int)RTM_DELETE, RTF_HOST);
		if (debug && error) {
			log(LOG_DEBUG, SPP_FMT "sppp_set_ip_addr: rtinit DEL failed, error=%d\n",
		    		SPP_ARGS(ifp), error);
		}

		/* set new address */
		si->sin_addr.s_addr = htonl(src);
		ia = ifatoia(ifa);
		IN_IFADDR_WLOCK();
		LIST_REMOVE(ia, ia_hash);
		LIST_INSERT_HEAD(INADDR_HASH(si->sin_addr.s_addr), ia, ia_hash);
		IN_IFADDR_WUNLOCK();

		/* add new route */
		error = rtinit(ifa, (int)RTM_ADD, RTF_HOST);
		if (debug && error) {
			log(LOG_DEBUG, SPP_FMT "sppp_set_ip_addr: rtinit ADD failed, error=%d",
		    		SPP_ARGS(ifp), error);
		}
		ifa_free(ifa);
	}
}
#endif

#ifdef INET6
/*
 * Get both IPv6 addresses.
 */
static void
sppp_get_ip6_addrs(struct sppp *sp, struct in6_addr *src, struct in6_addr *dst,
		   struct in6_addr *srcmask)
{
	struct ifnet *ifp = SP2IFP(sp);
	struct ifaddr *ifa;
	struct sockaddr_in6 *si, *sm;
	struct in6_addr ssrc, ddst;

	sm = NULL;
	bzero(&ssrc, sizeof(ssrc));
	bzero(&ddst, sizeof(ddst));
	/*
	 * Pick the first link-local AF_INET6 address from the list,
	 * aliases don't make any sense on a p2p link anyway.
	 */
	si = NULL;
	if_addr_rlock(ifp);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link)
		if (ifa->ifa_addr->sa_family == AF_INET6) {
			si = (struct sockaddr_in6 *)ifa->ifa_addr;
			sm = (struct sockaddr_in6 *)ifa->ifa_netmask;
			if (si && IN6_IS_ADDR_LINKLOCAL(&si->sin6_addr))
				break;
		}
	if (ifa) {
		if (si && !IN6_IS_ADDR_UNSPECIFIED(&si->sin6_addr)) {
			bcopy(&si->sin6_addr, &ssrc, sizeof(ssrc));
			if (srcmask) {
				bcopy(&sm->sin6_addr, srcmask,
				      sizeof(*srcmask));
			}
		}

		si = (struct sockaddr_in6 *)ifa->ifa_dstaddr;
		if (si && !IN6_IS_ADDR_UNSPECIFIED(&si->sin6_addr))
			bcopy(&si->sin6_addr, &ddst, sizeof(ddst));
	}

	if (dst)
		bcopy(&ddst, dst, sizeof(*dst));
	if (src)
		bcopy(&ssrc, src, sizeof(*src));
	if_addr_runlock(ifp);
}

#ifdef IPV6CP_MYIFID_DYN
/*
 * Generate random ifid.
 */
static void
sppp_gen_ip6_addr(struct sppp *sp, struct in6_addr *addr)
{
	/* TBD */
}

/*
 * Set my IPv6 address.
 */
static void
sppp_set_ip6_addr(struct sppp *sp, const struct in6_addr *src)
{
	STDDCL;
	struct ifaddr *ifa;
	struct sockaddr_in6 *sin6;

	/*
	 * Pick the first link-local AF_INET6 address from the list,
	 * aliases don't make any sense on a p2p link anyway.
	 */

	sin6 = NULL;
	if_addr_rlock(ifp);
	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		if (ifa->ifa_addr->sa_family == AF_INET6) {
			sin6 = (struct sockaddr_in6 *)ifa->ifa_addr;
			if (sin6 && IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)) {
				ifa_ref(ifa);
				break;
			}
		}
	}
	if_addr_runlock(ifp);

	if (ifa != NULL) {
		int error;
		struct sockaddr_in6 new_sin6 = *sin6;

		bcopy(src, &new_sin6.sin6_addr, sizeof(new_sin6.sin6_addr));
		error = in6_ifinit(ifp, ifatoia6(ifa), &new_sin6, 1);
		if (debug && error) {
			log(LOG_DEBUG, SPP_FMT "sppp_set_ip6_addr: in6_ifinit "
			    " failed, error=%d\n", SPP_ARGS(ifp), error);
		}
		ifa_free(ifa);
	}
}
#endif

/*
 * Suggest a candidate address to be used by peer.
 */
static void
sppp_suggest_ip6_addr(struct sppp *sp, struct in6_addr *suggest)
{
	struct in6_addr myaddr;
	struct timeval tv;

	sppp_get_ip6_addrs(sp, &myaddr, 0, 0);

	myaddr.s6_addr[8] &= ~0x02;	/* u bit to "local" */
	microtime(&tv);
	if ((tv.tv_usec & 0xff) == 0 && (tv.tv_sec & 0xff) == 0) {
		myaddr.s6_addr[14] ^= 0xff;
		myaddr.s6_addr[15] ^= 0xff;
	} else {
		myaddr.s6_addr[14] ^= (tv.tv_usec & 0xff);
		myaddr.s6_addr[15] ^= (tv.tv_sec & 0xff);
	}
	if (suggest)
		bcopy(&myaddr, suggest, sizeof(myaddr));
}
#endif /*INET6*/

static int
sppp_params(struct sppp *sp, u_long cmd, void *data)
{
	u_long subcmd;
	struct ifreq *ifr = (struct ifreq *)data;
	struct spppreq *spr;
	int rv = 0;

	if ((spr = malloc(sizeof(struct spppreq), M_TEMP, M_NOWAIT)) == NULL)
		return (EAGAIN);
	/*
	 * ifr_data_get_ptr(ifr) is supposed to point to a struct spppreq.
	 * Check the cmd word first before attempting to fetch all the
	 * data.
	 */
	rv = fueword(ifr_data_get_ptr(ifr), &subcmd);
	if (rv == -1) {
		rv = EFAULT;
		goto quit;
	}

	if (copyin(ifr_data_get_ptr(ifr), spr, sizeof(struct spppreq)) != 0) {
		rv = EFAULT;
		goto quit;
	}

	switch (subcmd) {
	case (u_long)SPPPIOGDEFS:
		if (cmd != SIOCGIFGENERIC) {
			rv = EINVAL;
			break;
		}
		/*
		 * We copy over the entire current state, but clean
		 * out some of the stuff we don't wanna pass up.
		 * Remember, SIOCGIFGENERIC is unprotected, and can be
		 * called by any user.  No need to ever get PAP or
		 * CHAP secrets back to userland anyway.
		 */
		spr->defs.pp_phase = sp->pp_phase;
		spr->defs.enable_vj = (sp->confflags & CONF_ENABLE_VJ) != 0;
		spr->defs.enable_ipv6 = (sp->confflags & CONF_ENABLE_IPV6) != 0;
		spr->defs.lcp = sp->lcp;
		spr->defs.ipcp = sp->ipcp;
		spr->defs.ipv6cp = sp->ipv6cp;
		spr->defs.myauth = sp->myauth;
		spr->defs.hisauth = sp->hisauth;
		bzero(spr->defs.myauth.secret, AUTHKEYLEN);
		bzero(spr->defs.myauth.challenge, AUTHKEYLEN);
		bzero(spr->defs.hisauth.secret, AUTHKEYLEN);
		bzero(spr->defs.hisauth.challenge, AUTHKEYLEN);
		/*
		 * Fixup the LCP timeout value to milliseconds so
		 * spppcontrol doesn't need to bother about the value
		 * of "hz".  We do the reverse calculation below when
		 * setting it.
		 */
		spr->defs.lcp.timeout = sp->lcp.timeout * 1000 / hz;
		rv = copyout(spr, ifr_data_get_ptr(ifr),
		    sizeof(struct spppreq));
		break;

	case (u_long)SPPPIOSDEFS:
		if (cmd != SIOCSIFGENERIC) {
			rv = EINVAL;
			break;
		}
		/*
		 * We have a very specific idea of which fields we
		 * allow being passed back from userland, so to not
		 * clobber our current state.  For one, we only allow
		 * setting anything if LCP is in dead or establish
		 * phase.  Once the authentication negotiations
		 * started, the authentication settings must not be
		 * changed again.  (The administrator can force an
		 * ifconfig down in order to get LCP back into dead
		 * phase.)
		 *
		 * Also, we only allow for authentication parameters to be
		 * specified.
		 *
		 * XXX Should allow to set or clear pp_flags.
		 *
		 * Finally, if the respective authentication protocol to
		 * be used is set differently than 0, but the secret is
		 * passed as all zeros, we don't trash the existing secret.
		 * This allows an administrator to change the system name
		 * only without clobbering the secret (which he didn't get
		 * back in a previous SPPPIOGDEFS call).  However, the
		 * secrets are cleared if the authentication protocol is
		 * reset to 0.  */
		if (sp->pp_phase != PHASE_DEAD &&
		    sp->pp_phase != PHASE_ESTABLISH) {
			rv = EBUSY;
			break;
		}

		if ((spr->defs.myauth.proto != 0 && spr->defs.myauth.proto != PPP_PAP &&
		     spr->defs.myauth.proto != PPP_CHAP) ||
		    (spr->defs.hisauth.proto != 0 && spr->defs.hisauth.proto != PPP_PAP &&
		     spr->defs.hisauth.proto != PPP_CHAP)) {
			rv = EINVAL;
			break;
		}

		if (spr->defs.myauth.proto == 0)
			/* resetting myauth */
			bzero(&sp->myauth, sizeof sp->myauth);
		else {
			/* setting/changing myauth */
			sp->myauth.proto = spr->defs.myauth.proto;
			bcopy(spr->defs.myauth.name, sp->myauth.name, AUTHNAMELEN);
			if (spr->defs.myauth.secret[0] != '\0')
				bcopy(spr->defs.myauth.secret, sp->myauth.secret,
				      AUTHKEYLEN);
		}
		if (spr->defs.hisauth.proto == 0)
			/* resetting hisauth */
			bzero(&sp->hisauth, sizeof sp->hisauth);
		else {
			/* setting/changing hisauth */
			sp->hisauth.proto = spr->defs.hisauth.proto;
			sp->hisauth.flags = spr->defs.hisauth.flags;
			bcopy(spr->defs.hisauth.name, sp->hisauth.name, AUTHNAMELEN);
			if (spr->defs.hisauth.secret[0] != '\0')
				bcopy(spr->defs.hisauth.secret, sp->hisauth.secret,
				      AUTHKEYLEN);
		}
		/* set LCP restart timer timeout */
		if (spr->defs.lcp.timeout != 0)
			sp->lcp.timeout = spr->defs.lcp.timeout * hz / 1000;
		/* set VJ enable and IPv6 disable flags */
#ifdef INET
		if (spr->defs.enable_vj)
			sp->confflags |= CONF_ENABLE_VJ;
		else
			sp->confflags &= ~CONF_ENABLE_VJ;
#endif
#ifdef INET6
		if (spr->defs.enable_ipv6)
			sp->confflags |= CONF_ENABLE_IPV6;
		else
			sp->confflags &= ~CONF_ENABLE_IPV6;
#endif
		break;

	default:
		rv = EINVAL;
	}

 quit:
	free(spr, M_TEMP);

	return (rv);
}

static void
sppp_phase_network(struct sppp *sp)
{
	STDDCL;
	int i;
	u_long mask;

	sp->pp_phase = PHASE_NETWORK;

	if (debug)
		log(LOG_DEBUG, SPP_FMT "phase %s\n", SPP_ARGS(ifp),
		    sppp_phase_name(sp->pp_phase));

	/* Notify NCPs now. */
	for (i = 0; i < IDX_COUNT; i++)
		if ((cps[i])->flags & CP_NCP)
			(cps[i])->Open(sp);

	/* Send Up events to all NCPs. */
	for (i = 0, mask = 1; i < IDX_COUNT; i++, mask <<= 1)
		if ((sp->lcp.protos & mask) && ((cps[i])->flags & CP_NCP))
			(cps[i])->Up(sp);

	/* if no NCP is starting, all this was in vain, close down */
	sppp_lcp_check_and_close(sp);
}


static const char *
sppp_cp_type_name(u_char type)
{
	static char buf[12];
	switch (type) {
	case CONF_REQ:   return "conf-req";
	case CONF_ACK:   return "conf-ack";
	case CONF_NAK:   return "conf-nak";
	case CONF_REJ:   return "conf-rej";
	case TERM_REQ:   return "term-req";
	case TERM_ACK:   return "term-ack";
	case CODE_REJ:   return "code-rej";
	case PROTO_REJ:  return "proto-rej";
	case ECHO_REQ:   return "echo-req";
	case ECHO_REPLY: return "echo-reply";
	case DISC_REQ:   return "discard-req";
	}
	snprintf (buf, sizeof(buf), "cp/0x%x", type);
	return buf;
}

static const char *
sppp_auth_type_name(u_short proto, u_char type)
{
	static char buf[12];
	switch (proto) {
	case PPP_CHAP:
		switch (type) {
		case CHAP_CHALLENGE:	return "challenge";
		case CHAP_RESPONSE:	return "response";
		case CHAP_SUCCESS:	return "success";
		case CHAP_FAILURE:	return "failure";
		}
	case PPP_PAP:
		switch (type) {
		case PAP_REQ:		return "req";
		case PAP_ACK:		return "ack";
		case PAP_NAK:		return "nak";
		}
	}
	snprintf (buf, sizeof(buf), "auth/0x%x", type);
	return buf;
}

static const char *
sppp_lcp_opt_name(u_char opt)
{
	static char buf[12];
	switch (opt) {
	case LCP_OPT_MRU:		return "mru";
	case LCP_OPT_ASYNC_MAP:		return "async-map";
	case LCP_OPT_AUTH_PROTO:	return "auth-proto";
	case LCP_OPT_QUAL_PROTO:	return "qual-proto";
	case LCP_OPT_MAGIC:		return "magic";
	case LCP_OPT_PROTO_COMP:	return "proto-comp";
	case LCP_OPT_ADDR_COMP:		return "addr-comp";
	}
	snprintf (buf, sizeof(buf), "lcp/0x%x", opt);
	return buf;
}

#ifdef INET
static const char *
sppp_ipcp_opt_name(u_char opt)
{
	static char buf[12];
	switch (opt) {
	case IPCP_OPT_ADDRESSES:	return "addresses";
	case IPCP_OPT_COMPRESSION:	return "compression";
	case IPCP_OPT_ADDRESS:		return "address";
	}
	snprintf (buf, sizeof(buf), "ipcp/0x%x", opt);
	return buf;
}
#endif

#ifdef INET6
static const char *
sppp_ipv6cp_opt_name(u_char opt)
{
	static char buf[12];
	switch (opt) {
	case IPV6CP_OPT_IFID:		return "ifid";
	case IPV6CP_OPT_COMPRESSION:	return "compression";
	}
	sprintf (buf, "0x%x", opt);
	return buf;
}
#endif

static const char *
sppp_state_name(int state)
{
	switch (state) {
	case STATE_INITIAL:	return "initial";
	case STATE_STARTING:	return "starting";
	case STATE_CLOSED:	return "closed";
	case STATE_STOPPED:	return "stopped";
	case STATE_CLOSING:	return "closing";
	case STATE_STOPPING:	return "stopping";
	case STATE_REQ_SENT:	return "req-sent";
	case STATE_ACK_RCVD:	return "ack-rcvd";
	case STATE_ACK_SENT:	return "ack-sent";
	case STATE_OPENED:	return "opened";
	}
	return "illegal";
}

static const char *
sppp_phase_name(enum ppp_phase phase)
{
	switch (phase) {
	case PHASE_DEAD:	return "dead";
	case PHASE_ESTABLISH:	return "establish";
	case PHASE_TERMINATE:	return "terminate";
	case PHASE_AUTHENTICATE: return "authenticate";
	case PHASE_NETWORK:	return "network";
	}
	return "illegal";
}

static const char *
sppp_proto_name(u_short proto)
{
	static char buf[12];
	switch (proto) {
	case PPP_LCP:	return "lcp";
	case PPP_IPCP:	return "ipcp";
	case PPP_PAP:	return "pap";
	case PPP_CHAP:	return "chap";
	case PPP_IPV6CP: return "ipv6cp";
	}
	snprintf(buf, sizeof(buf), "proto/0x%x", (unsigned)proto);
	return buf;
}

static void
sppp_print_bytes(const u_char *p, u_short len)
{
	if (len)
		log(-1, " %*D", len, p, "-");
}

static void
sppp_print_string(const char *p, u_short len)
{
	u_char c;

	while (len-- > 0) {
		c = *p++;
		/*
		 * Print only ASCII chars directly.  RFC 1994 recommends
		 * using only them, but we don't rely on it.  */
		if (c < ' ' || c > '~')
			log(-1, "\\x%x", c);
		else
			log(-1, "%c", c);
	}
}

#ifdef INET
static const char *
sppp_dotted_quad(u_long addr)
{
	static char s[16];
	sprintf(s, "%d.%d.%d.%d",
		(int)((addr >> 24) & 0xff),
		(int)((addr >> 16) & 0xff),
		(int)((addr >> 8) & 0xff),
		(int)(addr & 0xff));
	return s;
}
#endif

static int
sppp_strnlen(u_char *p, int max)
{
	int len;

	for (len = 0; len < max && *p; ++p)
		++len;
	return len;
}

/* a dummy, used to drop uninteresting events */
static void
sppp_null(struct sppp *unused)
{
	/* do just nothing */
}
