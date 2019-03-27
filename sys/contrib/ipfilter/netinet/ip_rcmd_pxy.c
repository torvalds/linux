/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id$
 *
 * Simple RCMD transparent proxy for in-kernel use.  For use with the NAT
 * code.
 * $FreeBSD$
 */

#define	IPF_RCMD_PROXY

typedef struct rcmdinfo {
	u_32_t	rcmd_port;	/* Port number seen */
	u_32_t	rcmd_portseq;	/* Sequence number where port is first seen */
	ipnat_t	*rcmd_rule;	/* Template rule for back connection */
} rcmdinfo_t;

void ipf_p_rcmd_main_load __P((void));
void ipf_p_rcmd_main_unload __P((void));

int ipf_p_rcmd_init __P((void));
void ipf_p_rcmd_fini __P((void));
void ipf_p_rcmd_del __P((ipf_main_softc_t *, ap_session_t *));
int ipf_p_rcmd_new __P((void *, fr_info_t *, ap_session_t *, nat_t *));
int ipf_p_rcmd_out __P((void *, fr_info_t *, ap_session_t *, nat_t *));
int ipf_p_rcmd_in __P((void *, fr_info_t *, ap_session_t *, nat_t *));
u_short ipf_rcmd_atoi __P((char *));
int ipf_p_rcmd_portmsg __P((fr_info_t *, ap_session_t *, nat_t *));

static	frentry_t	rcmdfr;

static	int		rcmd_proxy_init = 0;


/*
 * RCMD application proxy initialization.
 */
void
ipf_p_rcmd_main_load()
{
	bzero((char *)&rcmdfr, sizeof(rcmdfr));
	rcmdfr.fr_ref = 1;
	rcmdfr.fr_flags = FR_INQUE|FR_PASS|FR_QUICK|FR_KEEPSTATE;
	MUTEX_INIT(&rcmdfr.fr_lock, "RCMD proxy rule lock");
	rcmd_proxy_init = 1;
}


void
ipf_p_rcmd_main_unload()
{
	if (rcmd_proxy_init == 1) {
		MUTEX_DESTROY(&rcmdfr.fr_lock);
		rcmd_proxy_init = 0;
	}
}


/*
 * Setup for a new RCMD proxy.
 */
int
ipf_p_rcmd_new(arg, fin, aps, nat)
	void *arg;
	fr_info_t *fin;
	ap_session_t *aps;
	nat_t *nat;
{
	tcphdr_t *tcp = (tcphdr_t *)fin->fin_dp;
	rcmdinfo_t *rc;
	ipnat_t *ipn;
	ipnat_t *np;
	int size;

	fin = fin;	/* LINT */

	np = nat->nat_ptr;
	size = np->in_size;
	KMALLOC(rc, rcmdinfo_t *);
	if (rc == NULL) {
#ifdef IP_RCMD_PROXY_DEBUG
		printf("ipf_p_rcmd_new:KMALLOCS(%d) failed\n", sizeof(*rc));
#endif
		return -1;
	}
	aps->aps_sport = tcp->th_sport;
	aps->aps_dport = tcp->th_dport;

	ipn = ipf_proxy_rule_rev(nat);
	if (ipn == NULL) {
		KFREE(rc);
		return -1;
	}

	aps->aps_data = rc;
	aps->aps_psiz = sizeof(*rc);
	bzero((char *)rc, sizeof(*rc));

	rc->rcmd_rule = ipn;

	return 0;
}


void
ipf_p_rcmd_del(softc, aps)
	ipf_main_softc_t *softc;
	ap_session_t *aps;
{
	rcmdinfo_t *rci;

	rci = aps->aps_data;
	if (rci != NULL) {
		rci->rcmd_rule->in_flags |= IPN_DELETE;
		ipf_nat_rule_deref(softc, &rci->rcmd_rule);
	}
}


/*
 * ipf_rcmd_atoi - implement a simple version of atoi
 */
u_short
ipf_rcmd_atoi(ptr)
	char *ptr;
{
	register char *s = ptr, c;
	register u_short i = 0;

	while (((c = *s++) != '\0') && ISDIGIT(c)) {
		i *= 10;
		i += c - '0';
	}
	return i;
}


int
ipf_p_rcmd_portmsg(fin, aps, nat)
	fr_info_t *fin;
	ap_session_t *aps;
	nat_t *nat;
{
	tcphdr_t *tcp, tcph, *tcp2 = &tcph;
	int off, dlen, nflags, direction;
	ipf_main_softc_t *softc;
	ipf_nat_softc_t *softn;
	char portbuf[8], *s;
	rcmdinfo_t *rc;
	fr_info_t fi;
	u_short sp;
	nat_t *nat2;
#ifdef USE_INET6
	ip6_t *ip6;
#endif
	int tcpsz;
	int slen = 0; /* silence gcc */
	ip_t *ip;
	mb_t *m;

	tcp = (tcphdr_t *)fin->fin_dp;

	m = fin->fin_m;
	ip = fin->fin_ip;
	tcpsz = TCP_OFF(tcp) << 2;
#ifdef USE_INET6
	ip6 = (ip6_t *)fin->fin_ip;
#endif
	softc = fin->fin_main_soft;
	softn = softc->ipf_nat_soft;
	off = (char *)tcp - (char *)ip + tcpsz + fin->fin_ipoff;

	dlen = fin->fin_dlen - tcpsz;
	if (dlen <= 0)
		return 0;

	rc = (rcmdinfo_t *)aps->aps_data;
	if ((rc->rcmd_portseq != 0) &&
	    (tcp->th_seq != rc->rcmd_portseq))
		return 0;

	bzero(portbuf, sizeof(portbuf));
	COPYDATA(m, off, MIN(sizeof(portbuf), dlen), portbuf);

	portbuf[sizeof(portbuf) - 1] = '\0';
	s = portbuf;
	sp = ipf_rcmd_atoi(s);
	if (sp == 0) {
#ifdef IP_RCMD_PROXY_DEBUG
		printf("ipf_p_rcmd_portmsg:sp == 0 dlen %d [%s]\n",
		       dlen, portbuf);
#endif
		return 0;
	}

	if (rc->rcmd_port != 0 && sp != rc->rcmd_port) {
#ifdef IP_RCMD_PROXY_DEBUG
		printf("ipf_p_rcmd_portmsg:sp(%d) != rcmd_port(%d)\n",
		       sp, rc->rcmd_port);
#endif
		return 0;
	}

	rc->rcmd_port = sp;
	rc->rcmd_portseq = tcp->th_seq;

	/*
	 * Initialise the packet info structure so we can search the NAT
	 * table to see if there already is soemthing present that matches
	 * up with what we want to add.
	 */
	bcopy((char *)fin, (char *)&fi, sizeof(fi));
	fi.fin_flx |= FI_IGNORE;
	fi.fin_data[0] = 0;
	fi.fin_data[1] = sp;
	fi.fin_src6 = nat->nat_ndst6;
	fi.fin_dst6 = nat->nat_nsrc6;

	if (nat->nat_v[0] == 6) {
#ifdef USE_INET6
		if (nat->nat_dir == NAT_OUTBOUND) {
			nat2 = ipf_nat6_outlookup(&fi, NAT_SEARCH|IPN_TCP,
						  nat->nat_pr[1],
						  &nat->nat_osrc6.in6,
						  &nat->nat_odst6.in6);
		} else {
			nat2 = ipf_nat6_inlookup(&fi, NAT_SEARCH|IPN_TCP,
						 nat->nat_pr[0],
						 &nat->nat_osrc6.in6,
						 &nat->nat_odst6.in6);
		}
#else
		nat2 = (void *)-1;
#endif
	} else {
		if (nat->nat_dir == NAT_OUTBOUND) {
			nat2 = ipf_nat_outlookup(&fi, NAT_SEARCH|IPN_TCP,
						 nat->nat_pr[1],
						 nat->nat_osrcip,
						 nat->nat_odstip);
		} else {
			nat2 = ipf_nat_inlookup(&fi, NAT_SEARCH|IPN_TCP,
						nat->nat_pr[0],
						nat->nat_osrcip,
						nat->nat_odstip);
		}
	}
	if (nat2 != NULL)
		return APR_ERR(1);

	/*
	 * Add skeleton NAT entry for connection which will come
	 * back the other way.
	 */

	if (nat->nat_v[0] == 6) {
#ifdef USE_INET6
		slen = ip6->ip6_plen;
		ip6->ip6_plen = htons(sizeof(*tcp));
#endif
	} else {
		slen = ip->ip_len;
		ip->ip_len = htons(fin->fin_hlen + sizeof(*tcp));
	}

	/*
	 * Fill out the fake TCP header with a few fields that ipfilter
	 * considers to be important.
	 */
	bzero((char *)tcp2, sizeof(*tcp2));
	tcp2->th_win = htons(8192);
	TCP_OFF_A(tcp2, 5);
	tcp2->th_flags = TH_SYN;

	fi.fin_dp = (char *)tcp2;
	fi.fin_fr = &rcmdfr;
	fi.fin_dlen = sizeof(*tcp2);
	fi.fin_plen = fi.fin_hlen + sizeof(*tcp2);
	fi.fin_flx &= FI_LOWTTL|FI_FRAG|FI_TCPUDP|FI_OPTIONS|FI_IGNORE;

	if (nat->nat_dir == NAT_OUTBOUND) {
		fi.fin_out = 0;
		direction = NAT_INBOUND;
	} else {
		fi.fin_out = 1;
		direction = NAT_OUTBOUND;
	}
	nflags = SI_W_SPORT|NAT_SLAVE|IPN_TCP;

	MUTEX_ENTER(&softn->ipf_nat_new);
	if (fin->fin_v == 4)
		nat2 = ipf_nat_add(&fi, rc->rcmd_rule, NULL, nflags,
				   direction);
#ifdef USE_INET6
	else
		nat2 = ipf_nat6_add(&fi, rc->rcmd_rule, NULL, nflags,
				    direction);
#endif
	MUTEX_EXIT(&softn->ipf_nat_new);

	if (nat2 != NULL) {
		(void) ipf_nat_proto(&fi, nat2, IPN_TCP);
		MUTEX_ENTER(&nat2->nat_lock);
		ipf_nat_update(&fi, nat2);
		MUTEX_EXIT(&nat2->nat_lock);
		fi.fin_ifp = NULL;
		if (nat2->nat_dir == NAT_INBOUND)
			fi.fin_dst6 = nat->nat_osrc6;
		(void) ipf_state_add(softc, &fi, NULL, SI_W_SPORT);
	}
	if (nat->nat_v[0] == 6) {
#ifdef USE_INET6
		ip6->ip6_plen = slen;
#endif
	} else {
		ip->ip_len = slen;
	}
	if (nat2 == NULL)
		return APR_ERR(1);
	return 0;
}


int
ipf_p_rcmd_out(arg, fin, aps, nat)
	void *arg;
	fr_info_t *fin;
	ap_session_t *aps;
	nat_t *nat;
{
	if (nat->nat_dir == NAT_OUTBOUND)
		return ipf_p_rcmd_portmsg(fin, aps, nat);
	return 0;
}


int
ipf_p_rcmd_in(arg, fin, aps, nat)
	void *arg;
	fr_info_t *fin;
	ap_session_t *aps;
	nat_t *nat;
{
	if (nat->nat_dir == NAT_INBOUND)
		return ipf_p_rcmd_portmsg(fin, aps, nat);
	return 0;
}
