/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id: ip_tftp_pxy.c,v 1.1.2.9 2012/07/22 08:04:23 darren_r Exp $
 */

#define IPF_TFTP_PROXY

typedef struct ipf_tftp_softc_s {
        int     	ipf_p_tftp_readonly;
	ipftuneable_t	*ipf_p_tftp_tune;
} ipf_tftp_softc_t;

int ipf_p_tftp_backchannel __P((fr_info_t *, ap_session_t *, nat_t *));
int ipf_p_tftp_client __P((ipf_tftp_softc_t *, fr_info_t *, ap_session_t *,
			   nat_t *));
int ipf_p_tftp_in __P((void *, fr_info_t *, ap_session_t *, nat_t *));
void ipf_p_tftp_main_load __P((void));
void ipf_p_tftp_main_unload __P((void));
int ipf_p_tftp_new __P((void *, fr_info_t *, ap_session_t *, nat_t *));
void ipf_p_tftp_del __P((ipf_main_softc_t *, ap_session_t *));
int ipf_p_tftp_out __P((void *, fr_info_t *, ap_session_t *, nat_t *));
int ipf_p_tftp_server __P((ipf_tftp_softc_t *, fr_info_t *, ap_session_t *,
			   nat_t *));
void *ipf_p_tftp_soft_create __P((ipf_main_softc_t *));
void ipf_p_tftp_soft_destroy __P((ipf_main_softc_t *, void *));

static	frentry_t	tftpfr;
static	int		tftp_proxy_init = 0;

typedef enum tftp_cmd_e {
	TFTP_CMD_READ = 1,
	TFTP_CMD_WRITE = 2,
	TFTP_CMD_DATA = 3,
	TFTP_CMD_ACK = 4,
	TFTP_CMD_ERROR = 5
} tftp_cmd_t;

typedef struct tftpinfo {
	tftp_cmd_t	ti_lastcmd;
	int		ti_nextblk;
	int		ti_lastblk;
	int		ti_lasterror;
	char		ti_filename[80];
	ipnat_t		*ti_rule;
} tftpinfo_t;

static  ipftuneable_t   ipf_tftp_tuneables[] = {
	{ { (void *)offsetof(ipf_tftp_softc_t, ipf_p_tftp_readonly) },
		"tftp_read_only",	0,	1,
		stsizeof(ipf_tftp_softc_t, ipf_p_tftp_readonly),
		0, NULL, NULL },
	{ { NULL }, NULL, 0, 0, 0, 0, NULL, NULL }
};


/*
 * TFTP application proxy initialization.
 */
void
ipf_p_tftp_main_load()
{

	bzero((char *)&tftpfr, sizeof(tftpfr));
	tftpfr.fr_ref = 1;
	tftpfr.fr_flags = FR_INQUE|FR_PASS|FR_QUICK|FR_KEEPSTATE;
	MUTEX_INIT(&tftpfr.fr_lock, "TFTP proxy rule lock");
	tftp_proxy_init = 1;
}


void
ipf_p_tftp_main_unload()
{

	if (tftp_proxy_init == 1) {
		MUTEX_DESTROY(&tftpfr.fr_lock);
		tftp_proxy_init = 0;
	}
}


void *
ipf_p_tftp_soft_create(softc)
	ipf_main_softc_t *softc;
{
	ipf_tftp_softc_t *softt;

	KMALLOC(softt, ipf_tftp_softc_t *);
	if (softt == NULL)
		return NULL;

	bzero((char *)softt, sizeof(*softt));

	softt->ipf_p_tftp_tune = ipf_tune_array_copy(softt,
						     sizeof(ipf_tftp_tuneables),
						     ipf_tftp_tuneables);
	if (softt->ipf_p_tftp_tune == NULL) {
		ipf_p_tftp_soft_destroy(softc, softt);
		return NULL;
	}
	if (ipf_tune_array_link(softc, softt->ipf_p_tftp_tune) == -1) {
		ipf_p_tftp_soft_destroy(softc, softt);
		return NULL;
	}

	softt->ipf_p_tftp_readonly = 1;

	return softt;
}


void
ipf_p_tftp_soft_destroy(softc, arg)
        ipf_main_softc_t *softc;
        void *arg;
{
	ipf_tftp_softc_t *softt = arg;

	if (softt->ipf_p_tftp_tune != NULL) {
		ipf_tune_array_unlink(softc, softt->ipf_p_tftp_tune);
		KFREES(softt->ipf_p_tftp_tune, sizeof(ipf_tftp_tuneables));
		softt->ipf_p_tftp_tune = NULL;
	}

	KFREE(softt);
}


int
ipf_p_tftp_out(arg, fin, aps, nat)
	void *arg;
	fr_info_t *fin;
	ap_session_t *aps;
	nat_t *nat;
{
	ipf_tftp_softc_t *softt = arg;

	fin->fin_flx |= FI_NOWILD;
	if (nat->nat_dir == NAT_OUTBOUND)
		return ipf_p_tftp_client(softt, fin, aps, nat);
	return ipf_p_tftp_server(softt, fin, aps, nat);
}


int
ipf_p_tftp_in(arg, fin, aps, nat)
	void *arg;
	fr_info_t *fin;
	ap_session_t *aps;
	nat_t *nat;
{
	ipf_tftp_softc_t *softt = arg;

	fin->fin_flx |= FI_NOWILD;
	if (nat->nat_dir == NAT_INBOUND)
		return ipf_p_tftp_client(softt, fin, aps, nat);
	return ipf_p_tftp_server(softt, fin, aps, nat);
}


int
ipf_p_tftp_new(arg, fin, aps, nat)
	void *arg;
	fr_info_t *fin;
	ap_session_t *aps;
	nat_t *nat;
{
	udphdr_t *udp;
	tftpinfo_t *ti;
	ipnat_t *ipn;
	ipnat_t *np;
	int size;

	fin = fin;	/* LINT */

	np = nat->nat_ptr;
	size = np->in_size;

	KMALLOC(ti, tftpinfo_t *);
	if (ti == NULL)
		return -1;
	KMALLOCS(ipn, ipnat_t *, size);
	if (ipn == NULL) {
		KFREE(ti);
		return -1;
	}

	aps->aps_data = ti;
	aps->aps_psiz = sizeof(*ti);
	bzero((char *)ti, sizeof(*ti));
	bzero((char *)ipn, size);
	ti->ti_rule = ipn;

	udp = (udphdr_t *)fin->fin_dp;
	aps->aps_sport = udp->uh_sport;
	aps->aps_dport = udp->uh_dport;

	ipn->in_size = size;
	ipn->in_apr = NULL;
	ipn->in_use = 1;
	ipn->in_hits = 1;
	ipn->in_ippip = 1;
	ipn->in_pr[0] = IPPROTO_UDP;
	ipn->in_pr[1] = IPPROTO_UDP;
	ipn->in_ifps[0] = nat->nat_ifps[0];
	ipn->in_ifps[1] = nat->nat_ifps[1];
	ipn->in_v[0] = nat->nat_ptr->in_v[1];
	ipn->in_v[1] = nat->nat_ptr->in_v[0];
	ipn->in_flags = IPN_UDP|IPN_FIXEDDPORT|IPN_PROXYRULE;

	ipn->in_nsrcip6 = nat->nat_odst6;
	ipn->in_osrcip6 = nat->nat_ndst6;

	if ((np->in_redir & NAT_REDIRECT) != 0) {
		ipn->in_redir = NAT_MAP;
		if (ipn->in_v[0] == 4) {
			ipn->in_snip = ntohl(nat->nat_odstaddr);
			ipn->in_dnip = ntohl(nat->nat_nsrcaddr);
		} else {
#ifdef USE_INET6
			ipn->in_snip6 = nat->nat_odst6;
			ipn->in_dnip6 = nat->nat_nsrc6;
#endif
		}
		ipn->in_ndstip6 = nat->nat_nsrc6;
		ipn->in_odstip6 = nat->nat_osrc6;
	} else {
		ipn->in_redir = NAT_REDIRECT;
		if (ipn->in_v[0] == 4) {
			ipn->in_snip = ntohl(nat->nat_odstaddr);
			ipn->in_dnip = ntohl(nat->nat_osrcaddr);
		} else {
#ifdef USE_INET6
			ipn->in_snip6 = nat->nat_odst6;
			ipn->in_dnip6 = nat->nat_osrc6;
#endif
		}
		ipn->in_ndstip6 = nat->nat_osrc6;
		ipn->in_odstip6 = nat->nat_nsrc6;
	}
	ipn->in_odport = htons(fin->fin_sport);
	ipn->in_ndport = htons(fin->fin_sport);

	IP6_SETONES(&ipn->in_osrcmsk6);
	IP6_SETONES(&ipn->in_nsrcmsk6);
	IP6_SETONES(&ipn->in_odstmsk6);
	IP6_SETONES(&ipn->in_ndstmsk6);
	MUTEX_INIT(&ipn->in_lock, "tftp proxy NAT rule");

	ipn->in_namelen = np->in_namelen;
	bcopy(np->in_names, ipn->in_ifnames, ipn->in_namelen);
	ipn->in_ifnames[0] = np->in_ifnames[0];
	ipn->in_ifnames[1] = np->in_ifnames[1];

	ti->ti_lastcmd = 0;

	return 0;
}


void
ipf_p_tftp_del(softc, aps)
	ipf_main_softc_t *softc;
	ap_session_t *aps;
{
	tftpinfo_t *tftp;

	tftp = aps->aps_data;
	if (tftp != NULL) {
		tftp->ti_rule->in_flags |= IPN_DELETE;
		ipf_nat_rule_deref(softc, &tftp->ti_rule);
	}
}


/*
 * Setup for a new TFTP proxy.
 */
int
ipf_p_tftp_backchannel(fin, aps, nat)
	fr_info_t *fin;
	ap_session_t *aps;
	nat_t *nat;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
#ifdef USE_MUTEXES
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
#endif
#ifdef USE_INET6
	i6addr_t swip6, sw2ip6;
	ip6_t *ip6;
#endif
	struct in_addr swip, sw2ip;
	tftpinfo_t *ti;
	udphdr_t udp;
	fr_info_t fi;
	u_short slen = 0; /* silence gcc */
	nat_t *nat2;
	int nflags;
	ip_t *ip;
	int dir;

	ti = aps->aps_data;
	/*
	 * Add skeleton NAT entry for connection which will come back the
	 * other way.
	 */
	bcopy((char *)fin, (char *)&fi, sizeof(fi));
	fi.fin_flx |= FI_IGNORE;
	fi.fin_data[1] = 0;

	bzero((char *)&udp, sizeof(udp));
	udp.uh_sport = 0;	/* XXX - don't specify remote port */
	udp.uh_dport = ti->ti_rule->in_ndport;
	udp.uh_ulen = htons(sizeof(udp));
	udp.uh_sum = 0;

	fi.fin_fr = &tftpfr;
	fi.fin_dp = (char *)&udp;
	fi.fin_sport = 0;
	fi.fin_dport = ntohs(ti->ti_rule->in_ndport);
	fi.fin_dlen = sizeof(udp);
	fi.fin_plen = fi.fin_hlen + sizeof(udp);
	fi.fin_flx &= FI_LOWTTL|FI_FRAG|FI_TCPUDP|FI_OPTIONS|FI_IGNORE;
	nflags = NAT_SLAVE|IPN_UDP|SI_W_SPORT;
#ifdef USE_INET6
	ip6 = (ip6_t *)fin->fin_ip;
#endif
	ip = fin->fin_ip;
	sw2ip.s_addr = 0;
	swip.s_addr = 0;

	fi.fin_src6 = nat->nat_ndst6;
	fi.fin_dst6 = nat->nat_nsrc6;
	if (nat->nat_v[0] == 4) {
		slen = ip->ip_len;
		ip->ip_len = htons(fin->fin_hlen + sizeof(udp));
		swip = ip->ip_src;
		sw2ip = ip->ip_dst;
		ip->ip_src = nat->nat_ndstip;
		ip->ip_dst = nat->nat_nsrcip;
	} else {
#ifdef USE_INET6
		slen = ip6->ip6_plen;
		ip6->ip6_plen = htons(sizeof(udp));
		swip6.in6 = ip6->ip6_src;
		sw2ip6.in6 = ip6->ip6_dst;
		ip6->ip6_src = nat->nat_ndst6.in6;
		ip6->ip6_dst = nat->nat_nsrc6.in6;
#endif
	}

	if (nat->nat_dir == NAT_INBOUND) {
		dir = NAT_OUTBOUND;
		fi.fin_out = 1;
	} else {
		dir = NAT_INBOUND;
		fi.fin_out = 0;
	}
	nflags |= NAT_NOTRULEPORT;

	MUTEX_ENTER(&softn->ipf_nat_new);
#ifdef USE_INET6
	if (nat->nat_v[0] == 6)
		nat2 = ipf_nat6_add(&fi, ti->ti_rule, NULL, nflags, dir);
	else
#endif
		nat2 = ipf_nat_add(&fi, ti->ti_rule, NULL, nflags, dir);
	MUTEX_EXIT(&softn->ipf_nat_new);
	if (nat2 != NULL) {
		(void) ipf_nat_proto(&fi, nat2, IPN_UDP);
		ipf_nat_update(&fi, nat2);
		fi.fin_ifp = NULL;
		if (ti->ti_rule->in_redir == NAT_MAP) {
			fi.fin_src6 = nat->nat_ndst6;
			fi.fin_dst6 = nat->nat_nsrc6;
			if (nat->nat_v[0] == 4) {
				ip->ip_src = nat->nat_ndstip;
				ip->ip_dst = nat->nat_nsrcip;
			} else {
#ifdef USE_INET6
				ip6->ip6_src = nat->nat_ndst6.in6;
				ip6->ip6_dst = nat->nat_nsrc6.in6;
#endif
			}
		} else {
			fi.fin_src6 = nat->nat_odst6;
			fi.fin_dst6 = nat->nat_osrc6;
			if (fin->fin_v == 4) {
				ip->ip_src = nat->nat_odstip;
				ip->ip_dst = nat->nat_osrcip;
			} else {
#ifdef USE_INET6
				ip6->ip6_src = nat->nat_odst6.in6;
				ip6->ip6_dst = nat->nat_osrc6.in6;
#endif
			}
		}
		if (ipf_state_add(softc, &fi, NULL, SI_W_SPORT) != 0) {
			ipf_nat_setpending(softc, nat2);
		}
	}
	if (nat->nat_v[0] == 4) {
		ip->ip_len = slen;
		ip->ip_src = swip;
		ip->ip_dst = sw2ip;
	} else {
#ifdef USE_INET6
		ip6->ip6_plen = slen;
		ip6->ip6_src = swip6.in6;
		ip6->ip6_dst = sw2ip6.in6;
#endif
	}
	return 0;
}


int
ipf_p_tftp_client(softt, fin, aps, nat)
	ipf_tftp_softc_t *softt;
	fr_info_t *fin;
	ap_session_t *aps;
	nat_t *nat;
{
	u_char *msg, *s, *t;
	tftpinfo_t *ti;
	u_short opcode;
	udphdr_t *udp;
	int len;

	if (fin->fin_dlen < 4)
		return 0;

	ti = aps->aps_data;
	msg = fin->fin_dp;
	msg += sizeof(udphdr_t);
	opcode = (msg[0] << 8) | msg[1];
	DT3(tftp_cmd, fr_info_t *, fin, int, opcode, nat_t *, nat);

	switch (opcode)
	{
	case TFTP_CMD_WRITE :
		if (softt->ipf_p_tftp_readonly != 0) 
			break;
		/* FALLTHROUGH */
	case TFTP_CMD_READ :
		len = fin->fin_dlen - sizeof(*udp) - 2;
		if (len > sizeof(ti->ti_filename) - 1)
			len = sizeof(ti->ti_filename) - 1;
		s = msg + 2;
		for (t = (u_char *)ti->ti_filename; (len > 0); len--, s++) {
			*t++ = *s;
			if (*s == '\0')
				break;
		}
		ipf_p_tftp_backchannel(fin, aps, nat);
		break;
	default :
		return -1;
	}

	ti = aps->aps_data;
	ti->ti_lastcmd = opcode;
	return 0;
}


int
ipf_p_tftp_server(softt, fin, aps, nat)
	ipf_tftp_softc_t *softt;
	fr_info_t *fin;
	ap_session_t *aps;
	nat_t *nat;
{
	tftpinfo_t *ti;
	u_short opcode;
	u_short arg;
	u_char *msg;

	if (fin->fin_dlen < 4)
		return 0;

	ti = aps->aps_data;
	msg = fin->fin_dp;
	msg += sizeof(udphdr_t);
	arg = (msg[2] << 8) | msg[3];
	opcode = (msg[0] << 8) | msg[1];

	switch (opcode)
	{
	case TFTP_CMD_ACK :
		ti->ti_lastblk = arg;
		break;

	case TFTP_CMD_ERROR :
		ti->ti_lasterror = arg;
		break;

	default :
		return -1;
	}

	ti->ti_lastcmd = opcode;
	return 0;
}
