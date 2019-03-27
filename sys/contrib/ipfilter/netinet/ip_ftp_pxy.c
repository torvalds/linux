/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * Simple FTP transparent proxy for in-kernel use.  For use with the NAT
 * code.
 *
 * $FreeBSD$
 * Id: ip_ftp_pxy.c,v 2.88.2.19 2006/04/01 10:14:53 darrenr Exp $
 */

#define	IPF_FTP_PROXY

#define	IPF_MINPORTLEN	18
#define	IPF_MINEPRTLEN	20
#define	IPF_MAXPORTLEN	30
#define	IPF_MIN227LEN	39
#define	IPF_MAX227LEN	51
#define	IPF_MIN229LEN	47
#define	IPF_MAX229LEN	51

#define	FTPXY_GO	0
#define	FTPXY_INIT	1
#define	FTPXY_USER_1	2
#define	FTPXY_USOK_1	3
#define	FTPXY_PASS_1	4
#define	FTPXY_PAOK_1	5
#define	FTPXY_AUTH_1	6
#define	FTPXY_AUOK_1	7
#define	FTPXY_ADAT_1	8
#define	FTPXY_ADOK_1	9
#define	FTPXY_ACCT_1	10
#define	FTPXY_ACOK_1	11
#define	FTPXY_USER_2	12
#define	FTPXY_USOK_2	13
#define	FTPXY_PASS_2	14
#define	FTPXY_PAOK_2	15

#define	FTPXY_JUNK_OK	0
#define	FTPXY_JUNK_BAD	1	/* Ignore all commands for this connection */
#define	FTPXY_JUNK_EOL	2	/* consume the rest of this line only */
#define	FTPXY_JUNK_CONT	3	/* Saerching for next numeric */

/*
 * Values for FTP commands.  Numerics cover 0-999
 */
#define	FTPXY_C_PASV	1000
#define	FTPXY_C_PORT	1001
#define	FTPXY_C_EPSV	1002
#define	FTPXY_C_EPRT	1003


typedef struct ipf_ftp_softc_s {
	int	ipf_p_ftp_pasvonly;
	/* Do not require logins before transfers */
	int	ipf_p_ftp_insecure;
	int	ipf_p_ftp_pasvrdr;
	/* PASV must be last command prior to 227 */
	int	ipf_p_ftp_forcepasv;
	int	ipf_p_ftp_debug;
	int	ipf_p_ftp_single_xfer;
	void	*ipf_p_ftp_tune;
} ipf_ftp_softc_t;


void ipf_p_ftp_main_load __P((void));
void ipf_p_ftp_main_unload __P((void));
void *ipf_p_ftp_soft_create __P((ipf_main_softc_t *));
void ipf_p_ftp_soft_destroy __P((ipf_main_softc_t *, void *));

int ipf_p_ftp_client __P((ipf_ftp_softc_t *, fr_info_t *, ip_t *, nat_t *,
			  ftpinfo_t *, int));
int ipf_p_ftp_complete __P((char *, size_t));
int ipf_p_ftp_in __P((void *, fr_info_t *, ap_session_t *, nat_t *));
int ipf_p_ftp_new __P((void *, fr_info_t *, ap_session_t *, nat_t *));
void ipf_p_ftp_del __P((ipf_main_softc_t *, ap_session_t *));
int ipf_p_ftp_out __P((void *, fr_info_t *, ap_session_t *, nat_t *));
int ipf_p_ftp_pasv __P((ipf_ftp_softc_t *, fr_info_t *, ip_t *, nat_t *,
			ftpinfo_t *, int));
int ipf_p_ftp_epsv __P((ipf_ftp_softc_t *, fr_info_t *, ip_t *, nat_t *,
			ftpinfo_t *, int));
int ipf_p_ftp_port __P((ipf_ftp_softc_t *, fr_info_t *, ip_t *, nat_t *,
			ftpinfo_t *, int));
int ipf_p_ftp_process __P((ipf_ftp_softc_t *, fr_info_t *, nat_t *,
			   ftpinfo_t *, int));
int ipf_p_ftp_server __P((ipf_ftp_softc_t *, fr_info_t *, ip_t *, nat_t *,
			  ftpinfo_t *, int));
int ipf_p_ftp_valid __P((ipf_ftp_softc_t *, ftpinfo_t *, int, char *, size_t));
int ipf_p_ftp_server_valid __P((ipf_ftp_softc_t *, ftpside_t *, char *,
				size_t));
int ipf_p_ftp_client_valid __P((ipf_ftp_softc_t *, ftpside_t *, char *,
				size_t));
u_short ipf_p_ftp_atoi __P((char **));
int ipf_p_ftp_pasvreply __P((ipf_ftp_softc_t *, fr_info_t *, ip_t *, nat_t *,
			     ftpinfo_t *, u_int, char *, char *));
int ipf_p_ftp_eprt __P((ipf_ftp_softc_t *, fr_info_t *, ip_t *, nat_t *,
			ftpinfo_t *, int));
int ipf_p_ftp_eprt4 __P((ipf_ftp_softc_t *, fr_info_t *, ip_t *, nat_t *,
			 ftpinfo_t *, int));
int ipf_p_ftp_eprt6 __P((ipf_ftp_softc_t *, fr_info_t *, ip_t *, nat_t *,
			 ftpinfo_t *, int));
int ipf_p_ftp_addport __P((ipf_ftp_softc_t *, fr_info_t *, ip_t *, nat_t *,
			   ftpinfo_t *, int, int, int));
void ipf_p_ftp_setpending __P((ipf_main_softc_t *, ftpinfo_t *));

/*
 * Debug levels
 */
#define	DEBUG_SECURITY		0x01
#define	DEBUG_ERROR		0x02
#define	DEBUG_INFO		0x04
#define	DEBUG_PARSE_ERR		0x08
#define	DEBUG_PARSE_INFO	0x10
#define	DEBUG_PARSE		0x20

static	int	ipf_p_ftp_proxy_init = 0;
static	frentry_t	ftppxyfr;
static	ipftuneable_t	ipf_ftp_tuneables[] = {
	{ { (void *)offsetof(ipf_ftp_softc_t, ipf_p_ftp_debug) },
		"ftp_debug",	0,	0x7f,
		stsizeof(ipf_ftp_softc_t, ipf_p_ftp_debug),
		0, NULL, NULL },
	{ { (void *)offsetof(ipf_ftp_softc_t, ipf_p_ftp_pasvonly) },
		"ftp_pasvonly",	0,	1,
		stsizeof(ipf_ftp_softc_t, ipf_p_ftp_pasvonly),
		0, NULL, NULL },
	{ { (void *)offsetof(ipf_ftp_softc_t, ipf_p_ftp_insecure) },
		"ftp_insecure",	0,	1,
		stsizeof(ipf_ftp_softc_t, ipf_p_ftp_insecure),
		0, NULL, NULL },
	{ { (void *)offsetof(ipf_ftp_softc_t, ipf_p_ftp_pasvrdr) },
		"ftp_pasvrdr",	0,	1,
		stsizeof(ipf_ftp_softc_t, ipf_p_ftp_pasvrdr),
		0, NULL, NULL },
	{ { (void *)offsetof(ipf_ftp_softc_t, ipf_p_ftp_forcepasv) },
		"ftp_forcepasv", 0,	1,
		stsizeof(ipf_ftp_softc_t, ipf_p_ftp_forcepasv),
		0, NULL, NULL },
	{ { (void *)offsetof(ipf_ftp_softc_t, ipf_p_ftp_single_xfer) },
		"ftp_single_xfer", 0,	1,
		stsizeof(ipf_ftp_softc_t, ipf_p_ftp_single_xfer),
		0, NULL, NULL },
	{ { NULL }, NULL, 0, 0, 0, 0, NULL, NULL }
};


void
ipf_p_ftp_main_load()
{
	bzero((char *)&ftppxyfr, sizeof(ftppxyfr));
	ftppxyfr.fr_ref = 1;
	ftppxyfr.fr_flags = FR_INQUE|FR_PASS|FR_QUICK|FR_KEEPSTATE;

	MUTEX_INIT(&ftppxyfr.fr_lock, "FTP Proxy Mutex");
	ipf_p_ftp_proxy_init = 1;
}


void
ipf_p_ftp_main_unload()
{

	if (ipf_p_ftp_proxy_init == 1) {
		MUTEX_DESTROY(&ftppxyfr.fr_lock);
		ipf_p_ftp_proxy_init = 0;
	}
}


/*
 * Initialize local structures.
 */
void *
ipf_p_ftp_soft_create(softc)
	ipf_main_softc_t *softc;
{
	ipf_ftp_softc_t *softf;

	KMALLOC(softf, ipf_ftp_softc_t *);
	if (softf == NULL)
		return NULL;

	bzero((char *)softf, sizeof(*softf));
#if defined(_KERNEL)
	softf->ipf_p_ftp_debug = 0;
#else
	softf->ipf_p_ftp_debug = DEBUG_PARSE_ERR;
#endif
	softf->ipf_p_ftp_forcepasv = 1;

	softf->ipf_p_ftp_tune = ipf_tune_array_copy(softf,
						    sizeof(ipf_ftp_tuneables),
						    ipf_ftp_tuneables);
	if (softf->ipf_p_ftp_tune == NULL) {
		ipf_p_ftp_soft_destroy(softc, softf);
		return NULL;
	}
	if (ipf_tune_array_link(softc, softf->ipf_p_ftp_tune) == -1) {
		ipf_p_ftp_soft_destroy(softc, softf);
		return NULL;
	}

	return softf;
}


void
ipf_p_ftp_soft_destroy(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_ftp_softc_t *softf = arg;

	if (softf->ipf_p_ftp_tune != NULL) {
		ipf_tune_array_unlink(softc, softf->ipf_p_ftp_tune);
		KFREES(softf->ipf_p_ftp_tune, sizeof(ipf_ftp_tuneables));
		softf->ipf_p_ftp_tune = NULL;
	}

	KFREE(softf);
}


int
ipf_p_ftp_new(arg, fin, aps, nat)
	void *arg;
	fr_info_t *fin;
	ap_session_t *aps;
	nat_t *nat;
{
	ftpinfo_t *ftp;
	ftpside_t *f;

	KMALLOC(ftp, ftpinfo_t *);
	if (ftp == NULL)
		return -1;

	nat = nat;	/* LINT */

	aps->aps_data = ftp;
	aps->aps_psiz = sizeof(ftpinfo_t);
	aps->aps_sport = htons(fin->fin_sport);
	aps->aps_dport = htons(fin->fin_dport);

	bzero((char *)ftp, sizeof(*ftp));
	f = &ftp->ftp_side[0];
	f->ftps_rptr = f->ftps_buf;
	f->ftps_wptr = f->ftps_buf;
	f = &ftp->ftp_side[1];
	f->ftps_rptr = f->ftps_buf;
	f->ftps_wptr = f->ftps_buf;
	ftp->ftp_passok = FTPXY_INIT;
	ftp->ftp_incok = 0;
	return 0;
}


void
ipf_p_ftp_setpending(ipf_main_softc_t *softc, ftpinfo_t *ftp)
{
	if (ftp->ftp_pendnat != NULL)
		ipf_nat_setpending(softc, ftp->ftp_pendnat);

	if (ftp->ftp_pendstate != NULL) {
		READ_ENTER(&softc->ipf_state);
		ipf_state_setpending(softc, ftp->ftp_pendstate);
		RWLOCK_EXIT(&softc->ipf_state);
	}
}


void
ipf_p_ftp_del(softc, aps)
	ipf_main_softc_t *softc;
	ap_session_t *aps;
{
	ftpinfo_t *ftp;

	ftp = aps->aps_data;
	if (ftp != NULL)
		ipf_p_ftp_setpending(softc, ftp);
}


int
ipf_p_ftp_port(softf, fin, ip, nat, ftp, dlen)
	ipf_ftp_softc_t *softf;
	fr_info_t *fin;
	ip_t *ip;
	nat_t *nat;
	ftpinfo_t *ftp;
	int dlen;
{
	char newbuf[IPF_FTPBUFSZ], *s;
	u_int a1, a2, a3, a4;
	u_short a5, a6, sp;
	size_t nlen, olen;
	tcphdr_t *tcp;
	int inc, off;
	ftpside_t *f;
	mb_t *m;

	m = fin->fin_m;
	f = &ftp->ftp_side[0];
	tcp = (tcphdr_t *)fin->fin_dp;
	off = (char *)tcp - (char *)ip + (TCP_OFF(tcp) << 2) + fin->fin_ipoff;

	/*
	 * Check for client sending out PORT message.
	 */
	if (dlen < IPF_MINPORTLEN) {
		DT3(ftp_PORT_error_dlen, nat_t *, nat, ftpside_t *, f,
		    u_int, dlen);
		if (softf->ipf_p_ftp_debug & DEBUG_PARSE)
			printf("ipf_p_ftp_port:dlen(%d) < IPF_MINPORTLEN\n",
			       dlen);
		return 0;
	}
	/*
	 * Skip the PORT command + space
	 */
	s = f->ftps_rptr + 5;
	/*
	 * Pick out the address components, two at a time.
	 */
	a1 = ipf_p_ftp_atoi(&s);
	if (s == NULL) {
		DT2(ftp_PORT_error_atoi_1, nat_t *, nat, ftpside_t *, f);
		if (softf->ipf_p_ftp_debug & DEBUG_PARSE_ERR)
			printf("ipf_p_ftp_port:ipf_p_ftp_atoi(%d) failed\n", 1);
		return 0;
	}
	a2 = ipf_p_ftp_atoi(&s);
	if (s == NULL) {
		DT2(ftp_PORT_error_atoi_2, nat_t *, nat, ftpside_t *, f);
		if (softf->ipf_p_ftp_debug & DEBUG_PARSE_ERR)
			printf("ipf_p_ftp_port:ipf_p_ftp_atoi(%d) failed\n", 2);
		return 0;
	}

	/*
	 * Check that IP address in the PORT/PASV reply is the same as the
	 * sender of the command - prevents using PORT for port scanning.
	 */
	a1 <<= 16;
	a1 |= a2;
	if (((nat->nat_dir == NAT_OUTBOUND) &&
	     (a1 != ntohl(nat->nat_osrcaddr))) ||
	    ((nat->nat_dir == NAT_INBOUND) &&
	     (a1 != ntohl(nat->nat_nsrcaddr)))) {
		DT3(ftp_PORT_error_address, nat_t *, nat, ftpside_t *, f,
		    u_int, a1);
		if (softf->ipf_p_ftp_debug & DEBUG_ERROR)
			printf("ipf_p_ftp_port:%s != nat->nat_inip\n", "a1");
		return APR_ERR(1);
	}

	a5 = ipf_p_ftp_atoi(&s);
	if (s == NULL) {
		DT2(ftp_PORT_error_atoi_3, nat_t *, nat, ftpside_t *, f);
		if (softf->ipf_p_ftp_debug & DEBUG_PARSE_ERR)
			printf("ipf_p_ftp_port:ipf_p_ftp_atoi(%d) failed\n", 3);
		return 0;
	}
	if (*s == ')')
		s++;

	/*
	 * check for CR-LF at the end.
	 */
	if (*s == '\n')
		s--;
	if ((*s != '\r') || (*(s + 1) != '\n')) {
		DT2(ftp_PORT_error_no_crlf, nat_t *, nat, ftpside_t *, f);
		if (softf->ipf_p_ftp_debug & DEBUG_PARSE_ERR)
			printf("ipf_p_ftp_port:missing %s\n", "cr-lf");
		return 0;
	}
	s += 2;
	a6 = a5 & 0xff;

	/*
	 * Calculate the source port. Verification of > 1024 is in
	 * ipf_p_ftp_addport.
	 */
	a5 >>= 8;
	a5 &= 0xff;
	sp = a5 << 8 | a6;

	/*
	 * Calculate new address parts for PORT command
	 */
	if (nat->nat_dir == NAT_INBOUND)
		a1 = ntohl(nat->nat_ndstaddr);
	else
		a1 = ntohl(ip->ip_src.s_addr);
	a1 = ntohl(ip->ip_src.s_addr);
	a2 = (a1 >> 16) & 0xff;
	a3 = (a1 >> 8) & 0xff;
	a4 = a1 & 0xff;
	a1 >>= 24;
	olen = s - f->ftps_rptr;
	/* DO NOT change this to snprintf! */
#if defined(SNPRINTF) && defined(_KERNEL)
	SNPRINTF(newbuf, sizeof(newbuf), "%s %u,%u,%u,%u,%u,%u\r\n",
		 "PORT", a1, a2, a3, a4, a5, a6);
#else
	(void) sprintf(newbuf, "%s %u,%u,%u,%u,%u,%u\r\n",
		       "PORT", a1, a2, a3, a4, a5, a6);
#endif

	nlen = strlen(newbuf);
	inc = nlen - olen;
	if ((inc + fin->fin_plen) > 65535) {
		DT3(ftp_PORT_error_inc, nat_t *, nat, ftpside_t *, f,
		    int, inc);
		if (softf->ipf_p_ftp_debug & DEBUG_ERROR)
			printf("ipf_p_ftp_port:inc(%d) + ip->ip_len > 65535\n",
			       inc);
		return 0;
	}

#if !defined(_KERNEL)
	M_ADJ(m, inc);
#else
	/*
	 * m_adj takes care of pkthdr.len, if required and treats inc<0 to
	 * mean remove -len bytes from the end of the packet.
	 * The mbuf chain will be extended if necessary by m_copyback().
	 */
	if (inc < 0)
		M_ADJ(m, inc);
#endif /* !defined(_KERNEL) */
	COPYBACK(m, off, nlen, newbuf);
	fin->fin_flx |= FI_DOCKSUM;

	if (inc != 0) {
		fin->fin_plen += inc;
		ip->ip_len = htons(fin->fin_plen);
		fin->fin_dlen += inc;
	}

	f->ftps_cmd = FTPXY_C_PORT;
	return ipf_p_ftp_addport(softf, fin, ip, nat, ftp, dlen, sp, inc);
}


int
ipf_p_ftp_addport(softf, fin, ip, nat, ftp, dlen, nport, inc)
	ipf_ftp_softc_t *softf;
	fr_info_t *fin;
	ip_t *ip;
	nat_t *nat;
	ftpinfo_t *ftp;
	int dlen, nport, inc;
{
	tcphdr_t tcph, *tcp2 = &tcph;
	ipf_main_softc_t *softc;
	ipf_nat_softc_t *softn;
	int direction;
	fr_info_t fi;
	ipnat_t *ipn;
	nat_t *nat2;
	u_short sp;
	int flags;

	softc = fin->fin_main_soft;
	softn = softc->ipf_nat_soft;

	if ((ftp->ftp_pendnat != NULL)  || (ftp->ftp_pendstate != NULL)) {
		if (softf->ipf_p_ftp_single_xfer != 0) {
			DT2(ftp_PORT_error_add_active, nat_t *, nat,
			    ftpinfo_t *, ftp);
			if (softf->ipf_p_ftp_debug & DEBUG_ERROR)
				printf("ipf_p_ftp_addport:xfer active %p/%p\n",
				       ftp->ftp_pendnat, ftp->ftp_pendstate);
			return 0;
		}
		ipf_p_ftp_setpending(softc, ftp);
	}

	/*
	 * Add skeleton NAT entry for connection which will come back the
	 * other way.
	 */
	sp = nport;
	/*
	 * Don't allow the PORT command to specify a port < 1024 due to
	 * security risks.
	 */
	if (sp < 1024) {
		DT3(ftp_PORT_error_port, nat_t *, nat, ftpinfo_t *, ftp,
		    u_int, sp);
		if (softf->ipf_p_ftp_debug & DEBUG_SECURITY)
			printf("ipf_p_ftp_addport:sp(%d) < 1024\n", sp);
		return 0;
	}
	/*
	 * The server may not make the connection back from port 20, but
	 * it is the most likely so use it here to check for a conflicting
	 * mapping.
	 */
	bcopy((char *)fin, (char *)&fi, sizeof(fi));
	fi.fin_flx |= FI_IGNORE;
	fi.fin_data[0] = sp;
	fi.fin_data[1] = fin->fin_data[1] - 1;
	fi.fin_src6 = nat->nat_ndst6;
	fi.fin_dst6 = nat->nat_nsrc6;

	if (nat->nat_v[0] == 6) {
#ifndef USE_INET6
		return APR_INC(inc);
#endif
	}

	/*
	 * Add skeleton NAT entry for connection which will come back the
	 * other way.
	 */
#ifdef USE_INET6
	if (nat->nat_v[0] == 6) {
		if (nat->nat_dir == NAT_OUTBOUND) {
			nat2 = ipf_nat6_outlookup(&fi, IPN_TCP|NAT_SEARCH,
						  nat->nat_pr[1],
						  &nat->nat_osrc6.in6,
						  &nat->nat_odst6.in6);
		} else {
			nat2 = ipf_nat6_inlookup(&fi, IPN_TCP|NAT_SEARCH,
						 nat->nat_pr[0],
						 &nat->nat_odst6.in6,
						 &nat->nat_osrc6.in6);
		}
	} else
#endif
	{
		if (nat->nat_dir == NAT_OUTBOUND) {
			nat2 = ipf_nat_outlookup(&fi, IPN_TCP|NAT_SEARCH,
						 nat->nat_pr[1],
						 nat->nat_osrcip,
						 nat->nat_odstip);
		} else {
			nat2 = ipf_nat_inlookup(&fi, IPN_TCP|NAT_SEARCH,
						nat->nat_pr[0],
						nat->nat_odstip,
						nat->nat_osrcip);
		}
	}
	if (nat2 != NULL)
		return APR_INC(inc);

	ipn = ipf_proxy_rule_rev(nat);
	if (ipn == NULL)
		return APR_ERR(1);
	ipn->in_use = 0;

	fi.fin_fr = &ftppxyfr;
	fi.fin_dp = (char *)tcp2;
	fi.fin_dlen = sizeof(*tcp2);
	fi.fin_plen = fi.fin_hlen + sizeof(*tcp2);
	fi.fin_flx &= FI_LOWTTL|FI_FRAG|FI_TCPUDP|FI_OPTIONS|FI_IGNORE;
	fi.fin_data[1] = sp;
	fi.fin_data[0] = 0;

	bzero((char *)tcp2, sizeof(*tcp2));
	tcp2->th_sport = 0;
	tcp2->th_dport = htons(sp);

	tcp2->th_win = htons(8192);
	TCP_OFF_A(tcp2, 5);
	tcp2->th_flags = TH_SYN;

	if (nat->nat_dir == NAT_INBOUND) {
		fi.fin_out = 1;
		direction = NAT_OUTBOUND;
	} else {
		fi.fin_out = 0;
		direction = NAT_INBOUND;
	}
	flags = SI_W_SPORT|NAT_SLAVE|IPN_TCP;

	MUTEX_ENTER(&softn->ipf_nat_new);
	if (nat->nat_v[0] == 6) {
#ifdef USE_INET6
		nat2 = ipf_nat6_add(&fi, ipn, &ftp->ftp_pendnat, flags,
				    direction);
#endif
	} else {
		nat2 = ipf_nat_add(&fi, ipn, &ftp->ftp_pendnat, flags,
				   direction);
	}
	MUTEX_EXIT(&softn->ipf_nat_new);

	if (nat2 == NULL) {
		KFREES(ipn, ipn->in_size);
		return APR_ERR(1);
	}

	(void) ipf_nat_proto(&fi, nat2, IPN_TCP);
	MUTEX_ENTER(&nat2->nat_lock);
	ipf_nat_update(&fi, nat2);
	MUTEX_EXIT(&nat2->nat_lock);
	fi.fin_ifp = NULL;
	if (nat2->nat_dir == NAT_INBOUND)
		fi.fin_dst6 = nat->nat_osrc6;
	if (ipf_state_add(softc, &fi, (ipstate_t **)&ftp->ftp_pendstate,
			  SI_W_SPORT) != 0)
		ipf_nat_setpending(softc, nat2);

	return APR_INC(inc);
}


int
ipf_p_ftp_client(softf, fin, ip, nat, ftp, dlen)
	ipf_ftp_softc_t *softf;
	fr_info_t *fin;
	nat_t *nat;
	ftpinfo_t *ftp;
	ip_t *ip;
	int dlen;
{
	char *rptr, *wptr, cmd[6], c;
	ftpside_t *f;
	int inc, i;

	inc = 0;
	f = &ftp->ftp_side[0];
	rptr = f->ftps_rptr;
	wptr = f->ftps_wptr;

	for (i = 0; (i < 5) && (i < dlen); i++) {
		c = rptr[i];
		if (ISALPHA(c)) {
			cmd[i] = TOUPPER(c);
		} else {
			cmd[i] = c;
		}
	}
	cmd[i] = '\0';

	ftp->ftp_incok = 0;
	DT2(ftp_client_command, char [], cmd, int, ftp->ftp_passok);
	if (!strncmp(cmd, "USER ", 5) || !strncmp(cmd, "XAUT ", 5)) {
		if (ftp->ftp_passok == FTPXY_ADOK_1 ||
		    ftp->ftp_passok == FTPXY_AUOK_1) {
			ftp->ftp_passok = FTPXY_USER_2;
			ftp->ftp_incok = 1;
		} else {
			ftp->ftp_passok = FTPXY_USER_1;
			ftp->ftp_incok = 1;
		}
	} else if (!strncmp(cmd, "AUTH ", 5)) {
		ftp->ftp_passok = FTPXY_AUTH_1;
		ftp->ftp_incok = 1;
	} else if (!strncmp(cmd, "PASS ", 5)) {
		if (ftp->ftp_passok == FTPXY_USOK_1) {
			ftp->ftp_passok = FTPXY_PASS_1;
			ftp->ftp_incok = 1;
		} else if (ftp->ftp_passok == FTPXY_USOK_2) {
			ftp->ftp_passok = FTPXY_PASS_2;
			ftp->ftp_incok = 1;
		}
	} else if ((ftp->ftp_passok == FTPXY_AUOK_1) &&
		   !strncmp(cmd, "ADAT ", 5)) {
		ftp->ftp_passok = FTPXY_ADAT_1;
		ftp->ftp_incok = 1;
	} else if ((ftp->ftp_passok == FTPXY_PAOK_1 ||
		    ftp->ftp_passok == FTPXY_PAOK_2) &&
		 !strncmp(cmd, "ACCT ", 5)) {
		ftp->ftp_passok = FTPXY_ACCT_1;
		ftp->ftp_incok = 1;
	} else if ((ftp->ftp_passok == FTPXY_GO) &&
		   !softf->ipf_p_ftp_pasvonly &&
		 !strncmp(cmd, "PORT ", 5)) {
		inc = ipf_p_ftp_port(softf, fin, ip, nat, ftp, dlen);
	} else if ((ftp->ftp_passok == FTPXY_GO) &&
		   !softf->ipf_p_ftp_pasvonly &&
		 !strncmp(cmd, "EPRT ", 5)) {
		inc = ipf_p_ftp_eprt(softf, fin, ip, nat, ftp, dlen);
	} else if (softf->ipf_p_ftp_insecure &&
		   !softf->ipf_p_ftp_pasvonly &&
		   !strncmp(cmd, "PORT ", 5)) {
		inc = ipf_p_ftp_port(softf, fin, ip, nat, ftp, dlen);
	}
	if (softf->ipf_p_ftp_debug & DEBUG_PARSE)
		printf("ipf_p_ftp_client: cmd[%s] passok %d incok %d inc %d\n",
		       cmd, ftp->ftp_passok, ftp->ftp_incok, inc);

	DT2(ftp_client_passok, char *, cmd, int, ftp->ftp_passok);
	while ((*rptr++ != '\n') && (rptr < wptr))
		;
	f->ftps_rptr = rptr;
	return inc;
}


int
ipf_p_ftp_pasv(softf, fin, ip, nat, ftp, dlen)
	ipf_ftp_softc_t *softf;
	fr_info_t *fin;
	ip_t *ip;
	nat_t *nat;
	ftpinfo_t *ftp;
	int dlen;
{
	u_int a1, a2, a3, a4, data_ip;
	char newbuf[IPF_FTPBUFSZ];
	const char *brackets[2];
	u_short a5, a6;
	ftpside_t *f;
	char *s;

	if ((softf->ipf_p_ftp_forcepasv != 0) &&
	    (ftp->ftp_side[0].ftps_cmd != FTPXY_C_PASV)) {
		DT2(ftp_PASV_error_state, nat_t *, nat, ftpinfo_t *, ftp);
		if (softf->ipf_p_ftp_debug & DEBUG_ERROR)
			printf("ipf_p_ftp_pasv:ftps_cmd(%d) != FTPXY_C_PASV\n",
			       ftp->ftp_side[0].ftps_cmd);
		return 0;
	}

	f = &ftp->ftp_side[1];

#define	PASV_REPLEN	24
	/*
	 * Check for PASV reply message.
	 */
	if (dlen < IPF_MIN227LEN) {
		DT3(ftp_PASV_error_short, nat_t *, nat, ftpinfo_t *, ftp,
		    int, dlen);
		if (softf->ipf_p_ftp_debug & DEBUG_ERROR)
			printf("ipf_p_ftp_pasv:dlen(%d) < IPF_MIN227LEN\n",
			       dlen);
		return 0;
	} else if (strncmp(f->ftps_rptr,
			   "227 Entering Passive Mod", PASV_REPLEN)) {
		DT2(ftp_PASV_error_string, nat_t *, nat, ftpinfo_t *, ftp);
		if (softf->ipf_p_ftp_debug & DEBUG_PARSE_ERR)
			printf("ipf_p_ftp_pasv:%d reply wrong\n", 227);
		return 0;
	}

	brackets[0] = "";
	brackets[1] = "";
	/*
	 * Skip the PASV reply + space
	 */
	s = f->ftps_rptr + PASV_REPLEN;
	while (*s && !ISDIGIT(*s)) {
		if (*s == '(') {
			brackets[0] = "(";
			brackets[1] = ")";
		}
		s++;
	}

	/*
	 * Pick out the address components, two at a time.
	 */
	a1 = ipf_p_ftp_atoi(&s);
	if (s == NULL) {
		DT2(ftp_PASV_error_atoi_1, nat_t *, nat, ftpside_t *, f);
		if (softf->ipf_p_ftp_debug & DEBUG_PARSE_ERR)
			printf("ipf_p_ftp_pasv:ipf_p_ftp_atoi(%d) failed\n", 1);
		return 0;
	}
	a2 = ipf_p_ftp_atoi(&s);
	if (s == NULL) {
		DT2(ftp_PASV_error_atoi_2, nat_t *, nat, ftpside_t *, f);
		if (softf->ipf_p_ftp_debug & DEBUG_PARSE_ERR)
			printf("ipf_p_ftp_pasv:ipf_p_ftp_atoi(%d) failed\n", 2);
		return 0;
	}

	/*
	 * check that IP address in the PASV reply is the same as the
	 * sender of the command - prevents using PASV for port scanning.
	 */
	a1 <<= 16;
	a1 |= a2;

	if (((nat->nat_dir == NAT_INBOUND) &&
	     (a1 != ntohl(nat->nat_ndstaddr))) ||
	    ((nat->nat_dir == NAT_OUTBOUND) &&
	     (a1 != ntohl(nat->nat_odstaddr)))) {
		DT3(ftp_PASV_error_address, nat_t *, nat, ftpside_t *, f,
		    u_int, a1);
		if (softf->ipf_p_ftp_debug & DEBUG_ERROR)
			printf("ipf_p_ftp_pasv:%s != nat->nat_oip\n", "a1");
		return 0;
	}

	a5 = ipf_p_ftp_atoi(&s);
	if (s == NULL) {
		DT2(ftp_PASV_error_atoi_3, nat_t *, nat, ftpside_t *, f);
		if (softf->ipf_p_ftp_debug & DEBUG_PARSE_ERR)
			printf("ipf_p_ftp_pasv:ipf_p_ftp_atoi(%d) failed\n", 3);
		return 0;
	}

	if (*s == ')')
		s++;
	if (*s == '.')
		s++;
	if (*s == '\n')
		s--;
	/*
	 * check for CR-LF at the end.
	 */
	if ((*s != '\r') || (*(s + 1) != '\n')) {
		DT(pasv_missing_crlf);
		if (softf->ipf_p_ftp_debug & DEBUG_PARSE_ERR)
			printf("ipf_p_ftp_pasv:missing %s", "cr-lf\n");
		return 0;
	}
	s += 2;

	a6 = a5 & 0xff;
	a5 >>= 8;
	/*
	 * Calculate new address parts for 227 reply
	 */
	if (nat->nat_dir == NAT_INBOUND) {
		data_ip = nat->nat_odstaddr;
		a1 = ntohl(data_ip);
	} else
		data_ip = htonl(a1);

	a2 = (a1 >> 16) & 0xff;
	a3 = (a1 >> 8) & 0xff;
	a4 = a1 & 0xff;
	a1 >>= 24;

#if defined(SNPRINTF) && defined(_KERNEL)
	SNPRINTF(newbuf, sizeof(newbuf), "%s %s%u,%u,%u,%u,%u,%u%s\r\n",
		"227 Entering Passive Mode", brackets[0], a1, a2, a3, a4,
		a5, a6, brackets[1]);
#else
	(void) sprintf(newbuf, "%s %s%u,%u,%u,%u,%u,%u%s\r\n",
		"227 Entering Passive Mode", brackets[0], a1, a2, a3, a4,
		a5, a6, brackets[1]);
#endif
	return ipf_p_ftp_pasvreply(softf, fin, ip, nat, ftp, (a5 << 8 | a6),
				   newbuf, s);
}

int
ipf_p_ftp_pasvreply(softf, fin, ip, nat, ftp, port, newmsg, s)
	ipf_ftp_softc_t *softf;
	fr_info_t *fin;
	ip_t *ip;
	nat_t *nat;
	ftpinfo_t *ftp;
	u_int port;
	char *newmsg;
	char *s;
{
	int inc, off, nflags;
	tcphdr_t *tcp, tcph, *tcp2;
	ipf_main_softc_t *softc;
	ipf_nat_softc_t *softn;
	size_t nlen, olen;
#ifdef USE_INET6
	ip6_t *ip6;
#endif
	ipnat_t *ipn;
	fr_info_t fi;
	ftpside_t *f;
	nat_t *nat2;
	mb_t *m;

	softc = fin->fin_main_soft;
	softn = softc->ipf_nat_soft;

	if ((ftp->ftp_pendnat != NULL) || (ftp->ftp_pendstate != NULL))
		ipf_p_ftp_setpending(softc, ftp);

	m = fin->fin_m;
	tcp = (tcphdr_t *)fin->fin_dp;
	off = (char *)tcp - (char *)ip + (TCP_OFF(tcp) << 2) + fin->fin_ipoff;

	tcp2 = &tcph;
	inc = 0;

	f = &ftp->ftp_side[1];
	olen = s - f->ftps_rptr;
	nlen = strlen(newmsg);
	inc = nlen - olen;
	if ((inc + fin->fin_plen) > 65535) {
		DT3(ftp_PASV_error_inc, nat_t *, nat, ftpside_t *, f,
		    int, inc);
		if (softf->ipf_p_ftp_debug & DEBUG_ERROR)
			printf("ipf_p_ftp_pasv:inc(%d) + ip->ip_len > 65535\n",
			       inc);
		return 0;
	}

	ipn = ipf_proxy_rule_fwd(nat);
	if (ipn == NULL)
		return APR_ERR(1);
	ipn->in_use = 0;

	/*
	 * Add skeleton NAT entry for connection which will come back the
	 * other way.
	 */
	bzero((char *)tcp2, sizeof(*tcp2));
	bcopy((char *)fin, (char *)&fi, sizeof(fi));
	fi.fin_flx |= FI_IGNORE;
	fi.fin_data[0] = 0;
	fi.fin_data[1] = port;
	nflags = IPN_TCP|SI_W_SPORT;

	fi.fin_fr = &ftppxyfr;
	fi.fin_dp = (char *)tcp2;
	fi.fin_out = 1 - fin->fin_out;
	fi.fin_dlen = sizeof(*tcp2);
	fi.fin_src6 = nat->nat_osrc6;
	fi.fin_dst6 = nat->nat_odst6;
	fi.fin_plen = fi.fin_hlen + sizeof(*tcp);
	fi.fin_flx &= FI_LOWTTL|FI_FRAG|FI_TCPUDP|FI_OPTIONS|FI_IGNORE;

	TCP_OFF_A(tcp2, 5);
	tcp2->th_flags = TH_SYN;
	tcp2->th_win = htons(8192);
	tcp2->th_dport = htons(port);

	MUTEX_ENTER(&softn->ipf_nat_new);
#ifdef USE_INET6
	if (nat->nat_v[0] == 6)
		nat2 = ipf_nat6_add(&fi, ipn, &ftp->ftp_pendnat,
				    nflags, nat->nat_dir);
	else
#endif
		nat2 = ipf_nat_add(&fi, ipn, &ftp->ftp_pendnat,
				   nflags, nat->nat_dir);
	MUTEX_EXIT(&softn->ipf_nat_new);

	if (nat2 == NULL) {
		KFREES(ipn, ipn->in_size);
		return APR_ERR(1);
	}

	(void) ipf_nat_proto(&fi, nat2, IPN_TCP);
	MUTEX_ENTER(&nat2->nat_lock);
	ipf_nat_update(&fi, nat2);
	MUTEX_EXIT(&nat2->nat_lock);
	fi.fin_ifp = NULL;
	if (nat->nat_dir == NAT_INBOUND) {
		if (nat->nat_v[0] == 6) {
#ifdef USE_INET6
			fi.fin_dst6 = nat->nat_ndst6;
#endif
		} else {
			fi.fin_daddr = nat->nat_ndstaddr;
		}
	}
	if (ipf_state_add(softc, &fi, (ipstate_t **)&ftp->ftp_pendstate,
			  SI_W_SPORT) != 0)
		ipf_nat_setpending(softc, nat2);

#if !defined(_KERNEL)
	M_ADJ(m, inc);
#else
	/*
	 * m_adj takes care of pkthdr.len, if required and treats inc<0 to
	 * mean remove -len bytes from the end of the packet.
	 * The mbuf chain will be extended if necessary by m_copyback().
	 */
	if (inc < 0)
		M_ADJ(m, inc);
#endif /* !defined(_KERNEL) */
	COPYBACK(m, off, nlen, newmsg);
	fin->fin_flx |= FI_DOCKSUM;

	if (inc != 0) {
		fin->fin_plen += inc;
		fin->fin_dlen += inc;
		if (nat->nat_v[0] == 6) {
#ifdef USE_INET6
			ip6 = (ip6_t *)fin->fin_ip;
			u_short len = ntohs(ip6->ip6_plen) + inc;
			ip6->ip6_plen = htons(len);
#endif
		} else {
			ip->ip_len = htons(fin->fin_plen);
		}
	}

	return APR_INC(inc);
}


int
ipf_p_ftp_server(softf, fin, ip, nat, ftp, dlen)
	ipf_ftp_softc_t *softf;
	fr_info_t *fin;
	ip_t *ip;
	nat_t *nat;
	ftpinfo_t *ftp;
	int dlen;
{
	char *rptr, *wptr;
	ftpside_t *f;
	int inc;

	inc = 0;
	f = &ftp->ftp_side[1];
	rptr = f->ftps_rptr;
	wptr = f->ftps_wptr;

	DT2(ftp_server_response, char *, rptr, int, ftp->ftp_passok);
	if (*rptr == ' ')
		goto server_cmd_ok;
	if (!ISDIGIT(*rptr) || !ISDIGIT(*(rptr + 1)) || !ISDIGIT(*(rptr + 2)))
		return 0;
	if (softf->ipf_p_ftp_debug & DEBUG_PARSE)
		printf("ipf_p_ftp_server_1: cmd[%4.4s] passok %d\n",
		       rptr, ftp->ftp_passok);
	if (ftp->ftp_passok == FTPXY_GO) {
		if (!strncmp(rptr, "227 ", 4))
			inc = ipf_p_ftp_pasv(softf, fin, ip, nat, ftp, dlen);
		else if (!strncmp(rptr, "229 ", 4))
			inc = ipf_p_ftp_epsv(softf, fin, ip, nat, ftp, dlen);
		else if (strncmp(rptr, "200", 3)) {
			/*
			 * 200 is returned for a successful command.
			 */
			;
		}
	} else if (softf->ipf_p_ftp_insecure && !strncmp(rptr, "227 ", 4)) {
		inc = ipf_p_ftp_pasv(softf, fin, ip, nat, ftp, dlen);
	} else if (softf->ipf_p_ftp_insecure && !strncmp(rptr, "229 ", 4)) {
		inc = ipf_p_ftp_epsv(softf, fin, ip, nat, ftp, dlen);
	} else if (*rptr == '5' || *rptr == '4')
		ftp->ftp_passok = FTPXY_INIT;
	else if (ftp->ftp_incok) {
		if (*rptr == '3') {
			if (ftp->ftp_passok == FTPXY_ACCT_1)
				ftp->ftp_passok = FTPXY_GO;
			else
				ftp->ftp_passok++;
		} else if (*rptr == '2') {
			switch (ftp->ftp_passok)
			{
			case FTPXY_USER_1 :
			case FTPXY_USER_2 :
			case FTPXY_PASS_1 :
			case FTPXY_PASS_2 :
			case FTPXY_ACCT_1 :
				ftp->ftp_passok = FTPXY_GO;
				break;
			default :
				ftp->ftp_passok += 3;
				break;
			}
		}
	}
	ftp->ftp_incok = 0;
server_cmd_ok:
	if (softf->ipf_p_ftp_debug & DEBUG_PARSE)
		printf("ipf_p_ftp_server_2: cmd[%4.4s] passok %d\n",
		       rptr, ftp->ftp_passok);
	DT3(ftp_server_passok, char *,rptr, int, ftp->ftp_incok,
	    int, ftp->ftp_passok);

	while ((*rptr++ != '\n') && (rptr < wptr))
		;
	f->ftps_rptr = rptr;
	return inc;
}


/*
 * 0 FTPXY_JUNK_OK
 * 1 FTPXY_JUNK_BAD
 * 2 FTPXY_JUNK_EOL
 * 3 FTPXY_JUNK_CONT
 *
 * Look to see if the buffer starts with something which we recognise as
 * being the correct syntax for the FTP protocol.
 */
int
ipf_p_ftp_client_valid(softf, ftps, buf, len)
	ipf_ftp_softc_t *softf;
	ftpside_t *ftps;
	char *buf;
	size_t len;
{
	register char *s, c, pc;
	register size_t i = len;
	char cmd[5];

	s = buf;

	if (ftps->ftps_junk == FTPXY_JUNK_BAD)
		return FTPXY_JUNK_BAD;

	if (i < 5) {
		DT1(client_valid, int, i);
		if (softf->ipf_p_ftp_debug & DEBUG_ERROR)
			printf("ipf_p_ftp_client_valid:i(%d) < 5\n", (int)i);
		return 2;
	}

	i--;
	c = *s++;

	if (ISALPHA(c)) {
		cmd[0] = TOUPPER(c);
		c = *s++;
		i--;
		if (ISALPHA(c)) {
			cmd[1] = TOUPPER(c);
			c = *s++;
			i--;
			if (ISALPHA(c)) {
				cmd[2] = TOUPPER(c);
				c = *s++;
				i--;
				if (ISALPHA(c)) {
					cmd[3] = TOUPPER(c);
					c = *s++;
					i--;
					if ((c != ' ') && (c != '\r'))
						goto bad_client_command;
				} else if ((c != ' ') && (c != '\r'))
					goto bad_client_command;
			} else
				goto bad_client_command;
		} else
			goto bad_client_command;
	} else {
bad_client_command:
		DT4(client_junk, int, len, int, i, int, c, char *, buf);
		if (softf->ipf_p_ftp_debug & DEBUG_PARSE_ERR)
			printf("%s:bad:junk %d len %d/%d c 0x%x buf [%*.*s]\n",
			       "ipf_p_ftp_client_valid",
			       ftps->ftps_junk, (int)len, (int)i, c,
			       (int)len, (int)len, buf);
		return FTPXY_JUNK_BAD;
	}

	for (; i; i--) {
		pc = c;
		c = *s++;
		if ((pc == '\r') && (c == '\n')) {
			cmd[4] = '\0';
			if (!strcmp(cmd, "PASV")) {
				ftps->ftps_cmd = FTPXY_C_PASV;
			} else if (!strcmp(cmd, "EPSV")) {
				ftps->ftps_cmd = FTPXY_C_EPSV;
			} else {
				ftps->ftps_cmd = 0;
			}
			return 0;
		}
	}
#if !defined(_KERNEL)
	printf("ipf_p_ftp_client_valid:junk after cmd[%*.*s]\n",
	       (int)len, (int)len, buf);
#endif
	return FTPXY_JUNK_EOL;
}


int
ipf_p_ftp_server_valid(softf, ftps, buf, len)
	ipf_ftp_softc_t *softf;
	ftpside_t *ftps;
	char *buf;
	size_t len;
{
	register char *s, c, pc;
	register size_t i = len;
	int cmd;

	s = buf;
	cmd = 0;

	if (ftps->ftps_junk == FTPXY_JUNK_BAD)
		return FTPXY_JUNK_BAD;

	if (i < 5) {
		DT1(server_valid, int, i);
		if (softf->ipf_p_ftp_debug & DEBUG_PARSE_ERR)
			printf("ipf_p_ftp_servert_valid:i(%d) < 5\n", (int)i);
		return 2;
	}

	c = *s++;
	i--;
	if (c == ' ') {
		cmd = -1;
		goto search_eol;
	}

	if (ISDIGIT(c)) {
		cmd = (c - '0') * 100;
		c = *s++;
		i--;
		if (ISDIGIT(c)) {
			cmd += (c - '0') * 10;
			c = *s++;
			i--;
			if (ISDIGIT(c)) {
				cmd += (c - '0');
				c = *s++;
				i--;
				if ((c != '-') && (c != ' '))
					goto bad_server_command;
				if (c == '-')
					return FTPXY_JUNK_CONT;
			} else
				goto bad_server_command;
		} else
			goto bad_server_command;
	} else {
bad_server_command:
		DT4(server_junk, int len, buf, int, i, int, c, char *, buf);
		if (softf->ipf_p_ftp_debug & DEBUG_PARSE_INFO)
			printf("%s:bad:junk %d len %d/%d c 0x%x buf [%*.*s]\n",
			       "ipf_p_ftp_server_valid",
			       ftps->ftps_junk, (int)len, (int)i,
			       c, (int)len, (int)len, buf);
		if (ftps->ftps_junk == FTPXY_JUNK_CONT)
			return FTPXY_JUNK_CONT;
		return FTPXY_JUNK_BAD;
	}
search_eol:
	for (; i; i--) {
		pc = c;
		c = *s++;
		if ((pc == '\r') && (c == '\n')) {
			if (cmd == -1) {
				if (ftps->ftps_junk == FTPXY_JUNK_CONT)
					return FTPXY_JUNK_CONT;
			} else {
				ftps->ftps_cmd = cmd;
			}
			return FTPXY_JUNK_OK;
		}
	}

	DT2(junk_eol, int, len, char *, buf);
	if (softf->ipf_p_ftp_debug & DEBUG_PARSE_INFO)
		printf("ipf_p_ftp_server_valid:junk after cmd[%*.*s]\n",
		       (int)len, (int)len, buf);
	return FTPXY_JUNK_EOL;
}


int
ipf_p_ftp_valid(softf, ftp, side, buf, len)
	ipf_ftp_softc_t *softf;
	ftpinfo_t *ftp;
	int side;
	char *buf;
	size_t len;
{
	ftpside_t *ftps;
	int ret;

	ftps = &ftp->ftp_side[side];

	if (side == 0)
		ret = ipf_p_ftp_client_valid(softf, ftps, buf, len);
	else
		ret = ipf_p_ftp_server_valid(softf, ftps, buf, len);
	return ret;
}


/*
 * For map rules, the following applies:
 * rv == 0 for outbound processing,
 * rv == 1 for inbound processing.
 * For rdr rules, the following applies:
 * rv == 0 for inbound processing,
 * rv == 1 for outbound processing.
 */
int
ipf_p_ftp_process(softf, fin, nat, ftp, rv)
	ipf_ftp_softc_t *softf;
	fr_info_t *fin;
	nat_t *nat;
	ftpinfo_t *ftp;
	int rv;
{
	int mlen, len, off, inc, i, sel, sel2, ok, ackoff, seqoff, retry;
	char *rptr, *wptr, *s;
	u_32_t thseq, thack;
	ap_session_t *aps;
	ftpside_t *f, *t;
	tcphdr_t *tcp;
	ip_t *ip;
	mb_t *m;

	m = fin->fin_m;
	ip = fin->fin_ip;
	tcp = (tcphdr_t *)fin->fin_dp;
	off = (char *)tcp - (char *)ip + (TCP_OFF(tcp) << 2) + fin->fin_ipoff;

	f = &ftp->ftp_side[rv];
	t = &ftp->ftp_side[1 - rv];
	thseq = ntohl(tcp->th_seq);
	thack = ntohl(tcp->th_ack);
	mlen = MSGDSIZE(m) - off;

	DT3(process_debug, tcphdr_t *, tcp, int, off, int, mlen);
	if (softf->ipf_p_ftp_debug & DEBUG_INFO)
		printf("ipf_p_ftp_process: %d:%d,%d, mlen %d flags %x\n",
		       fin->fin_out, fin->fin_sport, fin->fin_dport,
		       mlen, tcp->th_flags);

	if ((mlen == 0) && ((tcp->th_flags & TH_OPENING) == TH_OPENING)) {
		f->ftps_seq[0] = thseq + 1;
		t->ftps_seq[0] = thack;
		return 0;
	} else if (mlen < 0) {
		return 0;
	}

	aps = nat->nat_aps;

	sel = aps->aps_sel[1 - rv];
	sel2 = aps->aps_sel[rv];
	if (rv == 1) {
		seqoff = aps->aps_seqoff[sel];
		if (aps->aps_seqmin[sel] > seqoff + thseq)
			seqoff = aps->aps_seqoff[!sel];
		ackoff = aps->aps_ackoff[sel2];
		if (aps->aps_ackmin[sel2] > ackoff + thack)
			ackoff = aps->aps_ackoff[!sel2];
	} else {
		seqoff = aps->aps_ackoff[sel];
		if (softf->ipf_p_ftp_debug & DEBUG_INFO)
			printf("seqoff %d thseq %x ackmin %x\n", seqoff, thseq,
			       aps->aps_ackmin[sel]);
		if (aps->aps_ackmin[sel] > seqoff + thseq)
			seqoff = aps->aps_ackoff[!sel];

		ackoff = aps->aps_seqoff[sel2];
		if (softf->ipf_p_ftp_debug & DEBUG_INFO)
			printf("ackoff %d thack %x seqmin %x\n", ackoff, thack,
			       aps->aps_seqmin[sel2]);
		if (ackoff > 0) {
			if (aps->aps_seqmin[sel2] > ackoff + thack)
				ackoff = aps->aps_seqoff[!sel2];
		} else {
			if (aps->aps_seqmin[sel2] > thack)
				ackoff = aps->aps_seqoff[!sel2];
		}
	}
	if (softf->ipf_p_ftp_debug & DEBUG_INFO) {
		printf("%s: %x seq %x/%d ack %x/%d len %d/%d off %d\n",
		       rv ? "IN" : "OUT", tcp->th_flags, thseq, seqoff,
		       thack, ackoff, mlen, fin->fin_plen, off);
		printf("sel %d seqmin %x/%x offset %d/%d\n", sel,
		       aps->aps_seqmin[sel], aps->aps_seqmin[sel2],
		       aps->aps_seqoff[sel], aps->aps_seqoff[sel2]);
		printf("sel %d ackmin %x/%x offset %d/%d\n", sel2,
		       aps->aps_ackmin[sel], aps->aps_ackmin[sel2],
		       aps->aps_ackoff[sel], aps->aps_ackoff[sel2]);
	}

	/*
	 * XXX - Ideally, this packet should get dropped because we now know
	 * that it is out of order (and there is no real danger in doing so
	 * apart from causing packets to go through here ordered).
	 */
	if (softf->ipf_p_ftp_debug & DEBUG_INFO) {
		printf("rv %d t:seq[0] %x seq[1] %x %d/%d\n",
		       rv, t->ftps_seq[0], t->ftps_seq[1], seqoff, ackoff);
	}

	ok = 0;
	if (t->ftps_seq[0] == 0) {
		t->ftps_seq[0] = thack;
		ok = 1;
	} else {
		if (ackoff == 0) {
			if (t->ftps_seq[0] == thack)
				ok = 1;
			else if (t->ftps_seq[1] == thack) {
				t->ftps_seq[0] = thack;
				ok = 1;
			}
		} else {
			if (t->ftps_seq[0] + ackoff == thack) {
				t->ftps_seq[0] = thack;
				ok = 1;
			} else if (t->ftps_seq[0] == thack + ackoff) {
				t->ftps_seq[0] = thack + ackoff;
				ok = 1;
			} else if (t->ftps_seq[1] + ackoff == thack) {
				t->ftps_seq[0] = thack;
				ok = 1;
			} else if (t->ftps_seq[1] == thack + ackoff) {
				t->ftps_seq[0] = thack + ackoff;
				ok = 1;
			}
		}
	}

	if (softf->ipf_p_ftp_debug & DEBUG_INFO) {
		if (!ok)
			printf("%s ok\n", "not");
	}

	if (!mlen) {
		if (t->ftps_seq[0] + ackoff != thack &&
		    t->ftps_seq[1] + ackoff != thack) {
			DT3(thack, ftpside_t *t, t, int, ackoff, u_32_t, thack);
			if (softf->ipf_p_ftp_debug & DEBUG_ERROR) {
				printf("%s:seq[0](%u) + (%d) != (%u)\n",
				       "ipf_p_ftp_process", t->ftps_seq[0],
				       ackoff, thack);
				printf("%s:seq[0](%u) + (%d) != (%u)\n",
				       "ipf_p_ftp_process", t->ftps_seq[1],
				       ackoff, thack);
			}
			return APR_ERR(1);
		}

		if (softf->ipf_p_ftp_debug & DEBUG_PARSE) {
			printf("ipf_p_ftp_process:f:seq[0] %x seq[1] %x\n",
				f->ftps_seq[0], f->ftps_seq[1]);
		}

		if (tcp->th_flags & TH_FIN) {
			if (thseq == f->ftps_seq[1]) {
				f->ftps_seq[0] = f->ftps_seq[1] - seqoff;
				f->ftps_seq[1] = thseq + 1 - seqoff;
			} else {
				DT2(thseq, ftpside_t *t, t, u_32_t, thseq);
				if (softf->ipf_p_ftp_debug & DEBUG_ERROR) {
					printf("FIN: thseq %x seqoff %d ftps_seq %x\n",
					       thseq, seqoff, f->ftps_seq[0]);
				}
				return APR_ERR(1);
			}
		}
		f->ftps_len = 0;
		return 0;
	}

	ok = 0;
	if ((thseq == f->ftps_seq[0]) || (thseq == f->ftps_seq[1])) {
		ok = 1;
	/*
	 * Retransmitted data packet.
	 */
	} else if ((thseq + mlen == f->ftps_seq[0]) ||
		   (thseq + mlen == f->ftps_seq[1])) {
		ok = 1;
	}

	if (ok == 0) {
		DT3(ok_0, ftpside_t *, f, u_32_t, thseq, int, mlen);
		inc = thseq - f->ftps_seq[0];
		if (softf->ipf_p_ftp_debug & DEBUG_ERROR) {
			printf("inc %d sel %d rv %d\n", inc, sel, rv);
			printf("th_seq %x ftps_seq %x/%x\n",
			       thseq, f->ftps_seq[0], f->ftps_seq[1]);
			printf("ackmin %x ackoff %d\n", aps->aps_ackmin[sel],
			       aps->aps_ackoff[sel]);
			printf("seqmin %x seqoff %d\n", aps->aps_seqmin[sel],
			       aps->aps_seqoff[sel]);
		}

		return APR_ERR(1);
	}

	inc = 0;
	rptr = f->ftps_rptr;
	wptr = f->ftps_wptr;
	f->ftps_seq[0] = thseq;
	f->ftps_seq[1] = f->ftps_seq[0] + mlen;
	f->ftps_len = mlen;

	while (mlen > 0) {
		len = MIN(mlen, sizeof(f->ftps_buf) - (wptr - rptr));
		if (len == 0)
			break;
		COPYDATA(m, off, len, wptr);
		mlen -= len;
		off += len;
		wptr += len;

whilemore:
		if (softf->ipf_p_ftp_debug & DEBUG_PARSE)
			printf("%s:len %d/%d off %d wptr %lx junk %d [%*.*s]\n",
			       "ipf_p_ftp_process",
			       len, mlen, off, (u_long)wptr, f->ftps_junk,
			       len, len, rptr);

		f->ftps_wptr = wptr;
		if (f->ftps_junk != FTPXY_JUNK_OK) {
			i = f->ftps_junk;
			f->ftps_junk = ipf_p_ftp_valid(softf, ftp, rv, rptr,
						      wptr - rptr);
			DT2(junk_transit, int, i, int, f->ftps_junk);

			if (softf->ipf_p_ftp_debug & DEBUG_PARSE)
				printf("%s:junk %d -> %d\n",
				       "ipf_p_ftp_process", i, f->ftps_junk);

			if (f->ftps_junk == FTPXY_JUNK_BAD) {
				DT(buffer_full);
				if (wptr - rptr == sizeof(f->ftps_buf)) {
					if (softf->ipf_p_ftp_debug &
					    DEBUG_PARSE_INFO)
						printf("%s:full buffer\n",
						       "ipf_p_ftp_process");
					f->ftps_rptr = f->ftps_buf;
					f->ftps_wptr = f->ftps_buf;
					rptr = f->ftps_rptr;
					wptr = f->ftps_wptr;
					continue;
				}
			}
		}

		while ((f->ftps_junk == FTPXY_JUNK_OK) && (wptr > rptr)) {
			len = wptr - rptr;
			f->ftps_junk = ipf_p_ftp_valid(softf, ftp, rv,
						       rptr, len);

			if (softf->ipf_p_ftp_debug & DEBUG_PARSE) {
				printf("%s=%d len %d rv %d ptr %lx/%lx ",
				       "ipf_p_ftp_valid",
				       f->ftps_junk, len, rv, (u_long)rptr,
				       (u_long)wptr);
				printf("buf [%*.*s]\n", len, len, rptr);
			}

			if (f->ftps_junk == FTPXY_JUNK_OK) {
				f->ftps_cmds++;
				f->ftps_rptr = rptr;
				if (rv)
					inc += ipf_p_ftp_server(softf, fin, ip,
								nat, ftp, len);
				else
					inc += ipf_p_ftp_client(softf, fin, ip,
								nat, ftp, len);
				rptr = f->ftps_rptr;
				wptr = f->ftps_wptr;
			}
		}

		/*
		 * Off to a bad start so lets just forget about using the
		 * ftp proxy for this connection.
		 */
		if ((f->ftps_cmds == 0) && (f->ftps_junk == FTPXY_JUNK_BAD)) {
			/* f->ftps_seq[1] += inc; */

			DT(ftp_junk_cmd);
			if (softf->ipf_p_ftp_debug & DEBUG_ERROR)
				printf("%s:cmds == 0 junk == 1\n",
				       "ipf_p_ftp_process");
			return APR_ERR(2);
		}

		retry = 0;
		if ((f->ftps_junk != FTPXY_JUNK_OK) && (rptr < wptr)) {
			for (s = rptr; s < wptr; s++) {
				if ((*s == '\r') && (s + 1 < wptr) &&
				    (*(s + 1) == '\n')) {
					rptr = s + 2;
					retry = 1;
					if (f->ftps_junk != FTPXY_JUNK_CONT)
						f->ftps_junk = FTPXY_JUNK_OK;
					break;
				}
			}
		}

		if (rptr == wptr) {
			rptr = wptr = f->ftps_buf;
		} else {
			/*
			 * Compact the buffer back to the start.  The junk
			 * flag should already be set and because we're not
			 * throwing away any data, it is preserved from its
			 * current state.
			 */
			if (rptr > f->ftps_buf) {
				bcopy(rptr, f->ftps_buf, wptr - rptr);
				wptr -= rptr - f->ftps_buf;
				rptr = f->ftps_buf;
			}
		}
		f->ftps_rptr = rptr;
		f->ftps_wptr = wptr;
		if (retry)
			goto whilemore;
	}

	/* f->ftps_seq[1] += inc; */
	if (tcp->th_flags & TH_FIN)
		f->ftps_seq[1]++;
	if (softf->ipf_p_ftp_debug & DEBUG_PARSE_INFO) {
		mlen = MSGDSIZE(m);
		mlen -= off;
		printf("ftps_seq[1] = %x inc %d len %d\n",
		       f->ftps_seq[1], inc, mlen);
	}

	f->ftps_rptr = rptr;
	f->ftps_wptr = wptr;
	return APR_INC(inc);
}


int
ipf_p_ftp_out(arg, fin, aps, nat)
	void *arg;
	fr_info_t *fin;
	ap_session_t *aps;
	nat_t *nat;
{
	ipf_ftp_softc_t *softf = arg;
	ftpinfo_t *ftp;
	int rev;

	ftp = aps->aps_data;
	if (ftp == NULL)
		return 0;

	rev = (nat->nat_dir == NAT_OUTBOUND) ? 0 : 1;
	if (ftp->ftp_side[1 - rev].ftps_ifp == NULL)
		ftp->ftp_side[1 - rev].ftps_ifp = fin->fin_ifp;

	return ipf_p_ftp_process(softf, fin, nat, ftp, rev);
}


int
ipf_p_ftp_in(arg, fin, aps, nat)
	void *arg;
	fr_info_t *fin;
	ap_session_t *aps;
	nat_t *nat;
{
	ipf_ftp_softc_t *softf = arg;
	ftpinfo_t *ftp;
	int rev;

	ftp = aps->aps_data;
	if (ftp == NULL)
		return 0;

	rev = (nat->nat_dir == NAT_OUTBOUND) ? 0 : 1;
	if (ftp->ftp_side[rev].ftps_ifp == NULL)
		ftp->ftp_side[rev].ftps_ifp = fin->fin_ifp;

	return ipf_p_ftp_process(softf, fin, nat, ftp, 1 - rev);
}


/*
 * ipf_p_ftp_atoi - implement a version of atoi which processes numbers in
 * pairs separated by commas (which are expected to be in the range 0 - 255),
 * returning a 16 bit number combining either side of the , as the MSB and
 * LSB.
 */
u_short
ipf_p_ftp_atoi(ptr)
	char **ptr;
{
	register char *s = *ptr, c;
	register u_char i = 0, j = 0;

	while (((c = *s++) != '\0') && ISDIGIT(c)) {
		i *= 10;
		i += c - '0';
	}
	if (c != ',') {
		*ptr = NULL;
		return 0;
	}
	while (((c = *s++) != '\0') && ISDIGIT(c)) {
		j *= 10;
		j += c - '0';
	}
	*ptr = s;
	i &= 0xff;
	j &= 0xff;
	return (i << 8) | j;
}


int
ipf_p_ftp_eprt(softf, fin, ip, nat, ftp, dlen)
	ipf_ftp_softc_t *softf;
	fr_info_t *fin;
	ip_t *ip;
	nat_t *nat;
	ftpinfo_t *ftp;
	int dlen;
{
	ftpside_t *f;

	/*
	 * Check for client sending out EPRT message.
	 */
	if (dlen < IPF_MINEPRTLEN) {
		DT1(epert_dlen, int, dlen);
		if (softf->ipf_p_ftp_debug & DEBUG_PARSE_ERR)
			printf("ipf_p_ftp_eprt:dlen(%d) < IPF_MINEPRTLEN\n",
				dlen);
		return 0;
	}

	/*
	 * Parse the EPRT command.  Format is:
	 * "EPRT |1|1.2.3.4|2000|" for IPv4 and
	 * "EPRT |2|ef00::1:2|2000|" for IPv6
	 */
	f = &ftp->ftp_side[0];
	if (f->ftps_rptr[5] != '|')
		return 0;
	if (f->ftps_rptr[5] == f->ftps_rptr[7]) {
		if (f->ftps_rptr[6] == '1' && nat->nat_v[0] == 4)
			return ipf_p_ftp_eprt4(softf, fin, ip, nat, ftp, dlen);
#ifdef USE_INET6
		if (f->ftps_rptr[6] == '2' && nat->nat_v[0] == 6)
			return ipf_p_ftp_eprt6(softf, fin, ip, nat, ftp, dlen);
#endif
	}
	return 0;
}


int
ipf_p_ftp_eprt4(softf, fin, ip, nat, ftp, dlen)
	ipf_ftp_softc_t *softf;
	fr_info_t *fin;
	ip_t *ip;
	nat_t *nat;
	ftpinfo_t *ftp;
	int dlen;
{
	int a1, a2, a3, a4, port, olen, nlen, inc, off;
	char newbuf[IPF_FTPBUFSZ];
	char *s, c, delim;
	u_32_t addr, i;
	tcphdr_t *tcp;
	ftpside_t *f;
	mb_t *m;

	m = fin->fin_m;
	tcp = (tcphdr_t *)fin->fin_dp;
	off = (char *)tcp - (char *)ip + (TCP_OFF(tcp) << 2) + fin->fin_ipoff;
	f = &ftp->ftp_side[0];
	delim = f->ftps_rptr[5];
	s = f->ftps_rptr + 8;

	/*
	 * get the IP address.
	 */
	i = 0;
	while (((c = *s++) != '\0') && ISDIGIT(c)) {
		i *= 10;
		i += c - '0';
	}
	if (i > 255)
		return 0;
	if (c != '.')
		return 0;
	addr = (i << 24);

	i = 0;
	while (((c = *s++) != '\0') && ISDIGIT(c)) {
		i *= 10;
		i += c - '0';
	}
	if (i > 255)
		return 0;
	if (c != '.')
		return 0;
	addr |= (addr << 16);

	i = 0;
	while (((c = *s++) != '\0') && ISDIGIT(c)) {
		i *= 10;
		i += c - '0';
	}
	if (i > 255)
		return 0;
	if (c != '.')
		return 0;
	addr |= (addr << 8);

	i = 0;
	while (((c = *s++) != '\0') && ISDIGIT(c)) {
		i *= 10;
		i += c - '0';
	}
	if (i > 255)
		return 0;
	if (c != delim)
		return 0;
	addr |= addr;

	/*
	 * Get the port number
	 */
	i = 0;
	while (((c = *s++) != '\0') && ISDIGIT(c)) {
		i *= 10;
		i += c - '0';
	}
	if (i > 65535)
		return 0;
	if (c != delim)
		return 0;
	port = i;

	/*
	 * Check for CR-LF at the end of the command string.
	 */
	if ((*s != '\r') || (*(s + 1) != '\n')) {
		DT(eprt4_no_crlf);
		if (softf->ipf_p_ftp_debug & DEBUG_PARSE_ERR)
			printf("ipf_p_ftp_eprt4:missing %s\n", "cr-lf");
		return 0;
	}
	s += 2;

	/*
	 * Calculate new address parts for PORT command
	 */
	if (nat->nat_dir == NAT_INBOUND)
		a1 = ntohl(nat->nat_odstaddr);
	else
		a1 = ntohl(ip->ip_src.s_addr);
	a2 = (a1 >> 16) & 0xff;
	a3 = (a1 >> 8) & 0xff;
	a4 = a1 & 0xff;
	a1 >>= 24;
	olen = s - f->ftps_rptr;
	/* DO NOT change this to snprintf! */
	/*
	 * While we could force the use of | as a delimiter here, it makes
	 * sense to preserve whatever character is being used by the systems
	 * involved in the communication.
	 */
#if defined(SNPRINTF) && defined(_KERNEL)
	SNPRINTF(newbuf, sizeof(newbuf), "%s %c1%c%u.%u.%u.%u%c%u%c\r\n",
		 "EPRT", delim, delim, a1, a2, a3, a4, delim, port, delim);
#else
	(void) sprintf(newbuf, "%s %c1%c%u.%u.%u.%u%c%u%c\r\n",
		       "EPRT", delim, delim, a1, a2, a3, a4, delim, port,
			delim);
#endif

	nlen = strlen(newbuf);
	inc = nlen - olen;
	if ((inc + fin->fin_plen) > 65535) {
		DT2(eprt4_len, int, inc, int, fin->fin_plen);
		if (softf->ipf_p_ftp_debug & DEBUG_ERROR)
			printf("ipf_p_ftp_eprt4:inc(%d) + ip->ip_len > 65535\n",
				inc);
		return 0;
	}

	off = (char *)tcp - (char *)ip + (TCP_OFF(tcp) << 2) + fin->fin_ipoff;
#if !defined(_KERNEL)
	M_ADJ(m, inc);
#else
	if (inc < 0)
		M_ADJ(m, inc);
#endif
	/* the mbuf chain will be extended if necessary by m_copyback() */
	COPYBACK(m, off, nlen, newbuf);
	fin->fin_flx |= FI_DOCKSUM;

	if (inc != 0) {
		fin->fin_plen += inc;
		ip->ip_len = htons(fin->fin_plen);
		fin->fin_dlen += inc;
	}

	f->ftps_cmd = FTPXY_C_EPRT;
	return ipf_p_ftp_addport(softf, fin, ip, nat, ftp, dlen, port, inc);
}


int
ipf_p_ftp_epsv(softf, fin, ip, nat, ftp, dlen)
	ipf_ftp_softc_t *softf;
	fr_info_t *fin;
	ip_t *ip;
	nat_t *nat;
	ftpinfo_t *ftp;
	int dlen;
{
	char newbuf[IPF_FTPBUFSZ];
	u_short ap = 0;
	ftpside_t *f;
	char *s;

	if ((softf->ipf_p_ftp_forcepasv != 0) &&
	    (ftp->ftp_side[0].ftps_cmd != FTPXY_C_EPSV)) {
		DT1(epsv_cmd, int, ftp->ftp_side[0].ftps_cmd);
		if (softf->ipf_p_ftp_debug & DEBUG_PARSE_ERR)
			printf("ipf_p_ftp_epsv:ftps_cmd(%d) != FTPXY_C_EPSV\n",
			       ftp->ftp_side[0].ftps_cmd);
		return 0;
	}
	f = &ftp->ftp_side[1];

#define EPSV_REPLEN	33
	/*
	 * Check for EPSV reply message.
	 */
	if (dlen < IPF_MIN229LEN) {
		return (0);
	} else if (strncmp(f->ftps_rptr,
			 "229 Entering Extended Passive Mode", EPSV_REPLEN)) {
		return (0);
}

	/*
	 * Skip the EPSV command + space
	 */
	s = f->ftps_rptr + 33;
	while (*s && !ISDIGIT(*s))
		s++;

	/*
	 * As per RFC 2428, there are no addres components in the EPSV
	 * response.  So we'll go straight to getting the port.
	 */
	while (*s && ISDIGIT(*s)) {
		ap *= 10;
		ap += *s++ - '0';
	}

	if (*s == '|')
		s++;
	if (*s == ')')
		s++;
	if (*s == '\n')
		s--;
	/*
	 * check for CR-LF at the end.
	 */
	if ((*s != '\r') || (*(s + 1) != '\n')) {
		return 0;
	}
	s += 2;

#if defined(SNPRINTF) && defined(_KERNEL)
	SNPRINTF(newbuf, sizeof(newbuf), "%s (|||%u|)\r\n",
		 "229 Entering Extended Passive Mode", ap);
#else
	(void) sprintf(newbuf, "%s (|||%u|)\r\n",
		       "229 Entering Extended Passive Mode", ap);
#endif

	return ipf_p_ftp_pasvreply(softf, fin, ip, nat, ftp, (u_int)ap,
				   newbuf, s);
}

#ifdef USE_INET6
int
ipf_p_ftp_eprt6(softf, fin, ip, nat, ftp, dlen)
	ipf_ftp_softc_t *softf;
	fr_info_t *fin;
	ip_t *ip;
	nat_t *nat;
	ftpinfo_t *ftp;
	int dlen;
{
	int port, olen, nlen, inc, off, left, i;
	char newbuf[IPF_FTPBUFSZ];
	char *s, c;
	i6addr_t addr, *a6;
	tcphdr_t *tcp;
	ip6_t *ip6;
	char delim;
	u_short whole;
	u_short part;
	ftpside_t *f;
	u_short *t;
	int fwd;
	mb_t *m;
	u_32_t a;

	m = fin->fin_m;
	ip6 = (ip6_t *)ip;
	f = &ftp->ftp_side[0];
	s = f->ftps_rptr + 8;
	f = &ftp->ftp_side[0];
	delim = f->ftps_rptr[5];
	tcp = (tcphdr_t *)fin->fin_dp;
	off = (char *)tcp - (char *)ip + (TCP_OFF(tcp) << 2) + fin->fin_ipoff;

	addr.i6[0] = 0;
	addr.i6[1] = 0;
	addr.i6[2] = 0;
	addr.i6[3] = 0;
	/*
	 * Parse an IPv6 address.
	 * Go forward until either :: or | is found. If :: is found,
	 * reverse direction. Direction change is performed to ease
	 * parsing an unknown number of 0s in the middle.
	 */
	whole = 0;
	t = (u_short *)&addr;
	fwd = 1;
	for (part = 0; (c = *s) != '\0'; ) {
		if (c == delim) {
			*t = htons((u_short)whole);
			break;
		}
		if (c == ':') {
			*t = part;
			if (fwd) {
				*t = htons((u_short)whole);
				t++;
			} else {
				*t = htons((u_short)(whole >> 16));
				t--;
			}
			whole = 0;
			if (fwd == 1 && s[1] == ':') {
				while (*s && *s != '|')
					s++;
				if ((c = *s) != delim)
					break;
				t = (u_short *)&addr.i6[3];
				t++;
				fwd = 0;
			} else if (fwd == 0 && s[-1] == ':') {
				break;
			}
		} else {
			if (c >= '0' && c <= '9') {
				c -= '0';
			} else if (c >= 'a' && c <= 'f') {
				c -= 'a' + 10;
			} else if (c >= 'A' && c <= 'F') {
				c -= 'A' + 10;
			}
			if (fwd) {
				whole <<= 8;
				whole |= c;
			} else {
				whole >>= 8;
				whole |= ((u_32_t)c) << 24;
			}
		}
		if (fwd)
			s++;
		else
			s--;
	}
	if (c != ':' && c != delim)
		return 0;

	while (*s != '|')
		s++;
	s++;

	/*
	 * Get the port number
	 */
	i = 0;
	while (((c = *s++) != '\0') && ISDIGIT(c)) {
		i *= 10;
		i += c - '0';
	}
	if (i > 65535)
		return 0;
	if (c != delim)
		return 0;
	port = (u_short)(i & 0xffff);

	/*
	 * Check for CR-LF at the end of the command string.
	 */
	if ((*s != '\r') || (*(s + 1) != '\n')) {
		DT(eprt6_no_crlf);
		if (softf->ipf_p_ftp_debug & DEBUG_PARSE_ERR)
			printf("ipf_p_ftp_eprt6:missing %s\n", "cr-lf");
		return 0;
	}
	s += 2;

	/*
	 * Calculate new address parts for PORT command
	 */
	a6 = (i6addr_t *)&ip6->ip6_src;
	olen = s - f->ftps_rptr;
	/* DO NOT change this to snprintf! */
	/*
	 * While we could force the use of | as a delimiter here, it makes
	 * sense to preserve whatever character is being used by the systems
	 * involved in the communication.
	 */
	s = newbuf;
	left = sizeof(newbuf);
#if defined(SNPRINTF) && defined(_KERNEL)
	SNPRINTF(newbuf, left, "EPRT %c2%c", delim, delim);
	left -= strlen(s) + 1;
	s += strlen(s);
	a = ntohl(a6->i6[0]);
	SNPRINTF(s, left, "%x:%x:", a >> 16, a & 0xffff);
	left -= strlen(s);
	s += strlen(s);
	a = ntohl(a6->i6[1]);
	SNPRINTF(s, left, "%x:%x:", a >> 16, a & 0xffff);
	left -= strlen(s);
	s += strlen(s);
	a = ntohl(a6->i6[2]);
	SNPRINTF(s, left, "%x:%x:", a >> 16, a & 0xffff);
	left -= strlen(s);
	s += strlen(s);
	a = ntohl(a6->i6[3]);
	SNPRINTF(s, left, "%x:%x", a >> 16, a & 0xffff);
	left -= strlen(s);
	s += strlen(s);
	SNPRINTF(s, left, "|%d|\r\n", port);
#else
	(void) sprintf(s, "EPRT %c2%c", delim, delim);
	s += strlen(s);
	a = ntohl(a6->i6[0]);
	sprintf(s, "%x:%x:", a >> 16, a & 0xffff);
	s += strlen(s);
	a = ntohl(a6->i6[1]);
	sprintf(s, "%x:%x:", a >> 16, a & 0xffff);
	left -= strlen(s);
	s += strlen(s);
	a = ntohl(a6->i6[2]);
	sprintf(s, "%x:%x:", a >> 16, a & 0xffff);
	left -= strlen(s);
	s += strlen(s);
	a = ntohl(a6->i6[3]);
	sprintf(s, "%x:%x", a >> 16, a & 0xffff);
	left -= strlen(s);
	s += strlen(s);
	sprintf(s, "|%d|\r\n", port);
#endif
	nlen = strlen(newbuf);
	inc = nlen - olen;
	if ((inc + fin->fin_plen) > 65535) {
		DT2(eprt6_len, int, inc, int, fin->fin_plen);
		if (softf->ipf_p_ftp_debug & DEBUG_ERROR)
			printf("ipf_p_ftp_eprt6:inc(%d) + ip->ip_len > 65535\n",
				inc);
		return 0;
	}

	off = (char *)tcp - (char *)ip + (TCP_OFF(tcp) << 2) + fin->fin_ipoff;
#if !defined(_KERNEL)
	M_ADJ(m, inc);
#else
	if (inc < 0)
		M_ADJ(m, inc);
#endif
	/* the mbuf chain will be extended if necessary by m_copyback() */
	COPYBACK(m, off, nlen, newbuf);
	fin->fin_flx |= FI_DOCKSUM;

	if (inc != 0) {
		fin->fin_plen += inc;
		ip6->ip6_plen = htons(fin->fin_plen - fin->fin_hlen);
		fin->fin_dlen += inc;
	}

	f->ftps_cmd = FTPXY_C_EPRT;
	return ipf_p_ftp_addport(softf, fin, ip, nat, ftp, dlen, port, inc);
}
#endif
