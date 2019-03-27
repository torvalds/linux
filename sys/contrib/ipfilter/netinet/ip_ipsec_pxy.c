/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * Simple ISAKMP transparent proxy for in-kernel use.  For use with the NAT
 * code.
 *
 * $Id$
 *
 */
#define	IPF_IPSEC_PROXY


/*
 * IPSec proxy
 */
typedef struct ipf_ipsec_softc_s {
	frentry_t	ipsec_fr;
	int		ipsec_proxy_init;
	int		ipsec_proxy_ttl;
	ipftq_t		*ipsec_nat_tqe;
	ipftq_t		*ipsec_state_tqe;
	char		ipsec_buffer[1500];
} ipf_ipsec_softc_t;


void *ipf_p_ipsec_soft_create __P((ipf_main_softc_t *));
void ipf_p_ipsec_soft_destroy __P((ipf_main_softc_t *, void *));
int ipf_p_ipsec_soft_init __P((ipf_main_softc_t *, void *));
void ipf_p_ipsec_soft_fini __P((ipf_main_softc_t *, void *));
int ipf_p_ipsec_init __P((void));
void ipf_p_ipsec_fini __P((void));
int ipf_p_ipsec_new __P((void *, fr_info_t *, ap_session_t *, nat_t *));
void ipf_p_ipsec_del __P((ipf_main_softc_t *, ap_session_t *));
int ipf_p_ipsec_inout __P((void *, fr_info_t *, ap_session_t *, nat_t *));
int ipf_p_ipsec_match __P((fr_info_t *, ap_session_t *, nat_t *));


/*
 * IPSec application proxy initialization.
 */
void *
ipf_p_ipsec_soft_create(softc)
	ipf_main_softc_t *softc;
{
	ipf_ipsec_softc_t *softi;

	KMALLOC(softi, ipf_ipsec_softc_t *);
	if (softi == NULL)
		return NULL;

	bzero((char *)softi, sizeof(*softi));
	softi->ipsec_fr.fr_ref = 1;
	softi->ipsec_fr.fr_flags = FR_OUTQUE|FR_PASS|FR_QUICK|FR_KEEPSTATE;
	MUTEX_INIT(&softi->ipsec_fr.fr_lock, "IPsec proxy rule lock");
	softi->ipsec_proxy_init = 1;
	softi->ipsec_proxy_ttl = 60;

	return softi;
}


int
ipf_p_ipsec_soft_init(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_ipsec_softc_t *softi = arg;

	softi->ipsec_nat_tqe = ipf_state_add_tq(softc, softi->ipsec_proxy_ttl);
	if (softi->ipsec_nat_tqe == NULL)
		return -1;
	softi->ipsec_state_tqe = ipf_nat_add_tq(softc, softi->ipsec_proxy_ttl);
	if (softi->ipsec_state_tqe == NULL) {
		if (ipf_deletetimeoutqueue(softi->ipsec_nat_tqe) == 0)
			ipf_freetimeoutqueue(softc, softi->ipsec_nat_tqe);
		softi->ipsec_nat_tqe = NULL;
		return -1;
	}

	softi->ipsec_nat_tqe->ifq_flags |= IFQF_PROXY;
	softi->ipsec_state_tqe->ifq_flags |= IFQF_PROXY;
	softi->ipsec_fr.fr_age[0] = softi->ipsec_proxy_ttl;
	softi->ipsec_fr.fr_age[1] = softi->ipsec_proxy_ttl;
	return 0;
}


void
ipf_p_ipsec_soft_fini(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_ipsec_softc_t *softi = arg;

	if (arg == NULL)
		return;

	if (softi->ipsec_nat_tqe != NULL) {
		if (ipf_deletetimeoutqueue(softi->ipsec_nat_tqe) == 0)
			ipf_freetimeoutqueue(softc, softi->ipsec_nat_tqe);
	}
	softi->ipsec_nat_tqe = NULL;
	if (softi->ipsec_state_tqe != NULL) {
		if (ipf_deletetimeoutqueue(softi->ipsec_state_tqe) == 0)
			ipf_freetimeoutqueue(softc, softi->ipsec_state_tqe);
	}
	softi->ipsec_state_tqe = NULL;
}


void
ipf_p_ipsec_soft_destroy(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_ipsec_softc_t *softi = arg;

	if (softi->ipsec_proxy_init == 1) {
		MUTEX_DESTROY(&softi->ipsec_fr.fr_lock);
		softi->ipsec_proxy_init = 0;
	}

	KFREE(softi);
}


/*
 * Setup for a new IPSEC proxy.
 */
int
ipf_p_ipsec_new(arg, fin, aps, nat)
	void *arg;
	fr_info_t *fin;
	ap_session_t *aps;
	nat_t *nat;
{
	ipf_ipsec_softc_t *softi = arg;
	ipf_main_softc_t *softc = fin->fin_main_soft;
#ifdef USE_MUTEXES
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
#endif
	int p, off, dlen, ttl;
	ipsec_pxy_t *ipsec;
	ipnat_t *ipn, *np;
	fr_info_t fi;
	char *ptr;
	int size;
	ip_t *ip;
	mb_t *m;

	if (fin->fin_v != 4)
		return -1;

	off = fin->fin_plen - fin->fin_dlen + fin->fin_ipoff;
	bzero(softi->ipsec_buffer, sizeof(softi->ipsec_buffer));
	ip = fin->fin_ip;
	m = fin->fin_m;

	dlen = M_LEN(m) - off;
	if (dlen < 16)
		return -1;
	COPYDATA(m, off, MIN(sizeof(softi->ipsec_buffer), dlen),
		 softi->ipsec_buffer);

	if (ipf_nat_outlookup(fin, 0, IPPROTO_ESP, nat->nat_nsrcip,
			  ip->ip_dst) != NULL)
		return -1;

	np = nat->nat_ptr;
	size = np->in_size;
	KMALLOC(ipsec, ipsec_pxy_t *);
	if (ipsec == NULL)
		return -1;

	KMALLOCS(ipn, ipnat_t *, size);
	if (ipn == NULL) {
		KFREE(ipsec);
		return -1;
	}

	aps->aps_data = ipsec;
	aps->aps_psiz = sizeof(*ipsec);
	bzero((char *)ipsec, sizeof(*ipsec));
	bzero((char *)ipn, size);
	ipsec->ipsc_rule = ipn;

	/*
	 * Create NAT rule against which the tunnel/transport mapping is
	 * created.  This is required because the current NAT rule does not
	 * describe ESP but UDP instead.
	 */
	ipn->in_size = size;
	ttl = IPF_TTLVAL(softi->ipsec_nat_tqe->ifq_ttl);
	ipn->in_tqehead[0] = ipf_nat_add_tq(softc, ttl);
	ipn->in_tqehead[1] = ipf_nat_add_tq(softc, ttl);
	ipn->in_ifps[0] = fin->fin_ifp;
	ipn->in_apr = NULL;
	ipn->in_use = 1;
	ipn->in_hits = 1;
	ipn->in_snip = ntohl(nat->nat_nsrcaddr);
	ipn->in_ippip = 1;
	ipn->in_osrcip = nat->nat_osrcip;
	ipn->in_osrcmsk = 0xffffffff;
	ipn->in_nsrcip = nat->nat_nsrcip;
	ipn->in_nsrcmsk = 0xffffffff;
	ipn->in_odstip = nat->nat_odstip;
	ipn->in_odstmsk = 0xffffffff;
	ipn->in_ndstip = nat->nat_ndstip;
	ipn->in_ndstmsk = 0xffffffff;
	ipn->in_redir = NAT_MAP;
	ipn->in_pr[0] = IPPROTO_ESP;
	ipn->in_pr[1] = IPPROTO_ESP;
	ipn->in_flags = (np->in_flags | IPN_PROXYRULE);
	MUTEX_INIT(&ipn->in_lock, "IPSec proxy NAT rule");

	ipn->in_namelen = np->in_namelen;
	bcopy(np->in_names, ipn->in_ifnames, ipn->in_namelen);
	ipn->in_ifnames[0] = np->in_ifnames[0];
	ipn->in_ifnames[1] = np->in_ifnames[1];

	bcopy((char *)fin, (char *)&fi, sizeof(fi));
	fi.fin_fi.fi_p = IPPROTO_ESP;
	fi.fin_fr = &softi->ipsec_fr;
	fi.fin_data[0] = 0;
	fi.fin_data[1] = 0;
	p = ip->ip_p;
	ip->ip_p = IPPROTO_ESP;
	fi.fin_flx &= ~(FI_TCPUDP|FI_STATE|FI_FRAG);
	fi.fin_flx |= FI_IGNORE;

	ptr = softi->ipsec_buffer;
	bcopy(ptr, (char *)ipsec->ipsc_icookie, sizeof(ipsec_cookie_t));
	ptr += sizeof(ipsec_cookie_t);
	bcopy(ptr, (char *)ipsec->ipsc_rcookie, sizeof(ipsec_cookie_t));
	/*
	 * The responder cookie should only be non-zero if the initiator
	 * cookie is non-zero.  Therefore, it is safe to assume(!) that the
	 * cookies are both set after copying if the responder is non-zero.
	 */
	if ((ipsec->ipsc_rcookie[0]|ipsec->ipsc_rcookie[1]) != 0)
		ipsec->ipsc_rckset = 1;

	MUTEX_ENTER(&softn->ipf_nat_new);
	ipsec->ipsc_nat = ipf_nat_add(&fi, ipn, &ipsec->ipsc_nat,
				      NAT_SLAVE|SI_WILDP, NAT_OUTBOUND);
	MUTEX_EXIT(&softn->ipf_nat_new);
	if (ipsec->ipsc_nat != NULL) {
		(void) ipf_nat_proto(&fi, ipsec->ipsc_nat, 0);
		MUTEX_ENTER(&ipsec->ipsc_nat->nat_lock);
		ipf_nat_update(&fi, ipsec->ipsc_nat);
		MUTEX_EXIT(&ipsec->ipsc_nat->nat_lock);

		fi.fin_data[0] = 0;
		fi.fin_data[1] = 0;
		(void) ipf_state_add(softc, &fi, &ipsec->ipsc_state, SI_WILDP);
	}
	ip->ip_p = p & 0xff;
	return 0;
}


/*
 * For outgoing IKE packets.  refresh timeouts for NAT & state entries, if
 * we can.  If they have disappeared, recreate them.
 */
int
ipf_p_ipsec_inout(arg, fin, aps, nat)
	void *arg;
	fr_info_t *fin;
	ap_session_t *aps;
	nat_t *nat;
{
	ipf_ipsec_softc_t *softi = arg;
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipsec_pxy_t *ipsec;
	fr_info_t fi;
	ip_t *ip;
	int p;

	if ((fin->fin_out == 1) && (nat->nat_dir == NAT_INBOUND))
		return 0;

	if ((fin->fin_out == 0) && (nat->nat_dir == NAT_OUTBOUND))
		return 0;

	ipsec = aps->aps_data;

	if (ipsec != NULL) {
		ip = fin->fin_ip;
		p = ip->ip_p;

		if ((ipsec->ipsc_nat == NULL) || (ipsec->ipsc_state == NULL)) {
			bcopy((char *)fin, (char *)&fi, sizeof(fi));
			fi.fin_fi.fi_p = IPPROTO_ESP;
			fi.fin_fr = &softi->ipsec_fr;
			fi.fin_data[0] = 0;
			fi.fin_data[1] = 0;
			ip->ip_p = IPPROTO_ESP;
			fi.fin_flx &= ~(FI_TCPUDP|FI_STATE|FI_FRAG);
			fi.fin_flx |= FI_IGNORE;
		}

		/*
		 * Update NAT timeout/create NAT if missing.
		 */
		if (ipsec->ipsc_nat != NULL)
			ipf_queueback(softc->ipf_ticks,
				      &ipsec->ipsc_nat->nat_tqe);
		else {
#ifdef USE_MUTEXES
			ipf_nat_softc_t *softn = softc->ipf_nat_soft;
#endif

			MUTEX_ENTER(&softn->ipf_nat_new);
			ipsec->ipsc_nat = ipf_nat_add(&fi, ipsec->ipsc_rule,
						      &ipsec->ipsc_nat,
						      NAT_SLAVE|SI_WILDP,
						      nat->nat_dir);
			MUTEX_EXIT(&softn->ipf_nat_new);
			if (ipsec->ipsc_nat != NULL) {
				(void) ipf_nat_proto(&fi, ipsec->ipsc_nat, 0);
				MUTEX_ENTER(&ipsec->ipsc_nat->nat_lock);
				ipf_nat_update(&fi, ipsec->ipsc_nat);
				MUTEX_EXIT(&ipsec->ipsc_nat->nat_lock);
			}
		}

		/*
		 * Update state timeout/create state if missing.
		 */
		READ_ENTER(&softc->ipf_state);
		if (ipsec->ipsc_state != NULL) {
			ipf_queueback(softc->ipf_ticks,
				      &ipsec->ipsc_state->is_sti);
			ipsec->ipsc_state->is_die = nat->nat_age;
			RWLOCK_EXIT(&softc->ipf_state);
		} else {
			RWLOCK_EXIT(&softc->ipf_state);
			fi.fin_data[0] = 0;
			fi.fin_data[1] = 0;
			(void) ipf_state_add(softc, &fi, &ipsec->ipsc_state,
					     SI_WILDP);
		}
		ip->ip_p = p;
	}
	return 0;
}


/*
 * This extends the NAT matching to be based on the cookies associated with
 * a session and found at the front of IKE packets.  The cookies are always
 * in the same order (not reversed depending on packet flow direction as with
 * UDP/TCP port numbers).
 */
int
ipf_p_ipsec_match(fin, aps, nat)
	fr_info_t *fin;
	ap_session_t *aps;
	nat_t *nat;
{
	ipsec_pxy_t *ipsec;
	u_32_t cookies[4];
	mb_t *m;
	int off;

	nat = nat;	/* LINT */

	if ((fin->fin_dlen < sizeof(cookies)) || (fin->fin_flx & FI_FRAG))
		return -1;

	off = fin->fin_plen - fin->fin_dlen + fin->fin_ipoff;
	ipsec = aps->aps_data;
	m = fin->fin_m;
	COPYDATA(m, off, sizeof(cookies), (char *)cookies);

	if ((cookies[0] != ipsec->ipsc_icookie[0]) ||
	    (cookies[1] != ipsec->ipsc_icookie[1]))
		return -1;

	if (ipsec->ipsc_rckset == 0) {
		if ((cookies[2]|cookies[3]) == 0) {
			return 0;
		}
		ipsec->ipsc_rckset = 1;
		ipsec->ipsc_rcookie[0] = cookies[2];
		ipsec->ipsc_rcookie[1] = cookies[3];
		return 0;
	}

	if ((cookies[2] != ipsec->ipsc_rcookie[0]) ||
	    (cookies[3] != ipsec->ipsc_rcookie[1]))
		return -1;
	return 0;
}


/*
 * clean up after ourselves.
 */
void
ipf_p_ipsec_del(softc, aps)
	ipf_main_softc_t *softc;
	ap_session_t *aps;
{
	ipsec_pxy_t *ipsec;

	ipsec = aps->aps_data;

	if (ipsec != NULL) {
		/*
		 * Don't bother changing any of the NAT structure details,
		 * *_del() is on a callback from aps_free(), from nat_delete()
		 */

		READ_ENTER(&softc->ipf_state);
		if (ipsec->ipsc_state != NULL) {
			ipsec->ipsc_state->is_die = softc->ipf_ticks + 1;
			ipsec->ipsc_state->is_me = NULL;
			ipf_queuefront(&ipsec->ipsc_state->is_sti);
		}
		RWLOCK_EXIT(&softc->ipf_state);

		ipsec->ipsc_state = NULL;
		ipsec->ipsc_nat = NULL;
		ipsec->ipsc_rule->in_flags |= IPN_DELETE;
		ipf_nat_rule_deref(softc, &ipsec->ipsc_rule);
	}
}
