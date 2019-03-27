/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id: ip_dns_pxy.c,v 1.1.2.10 2012/07/22 08:04:23 darren_r Exp $
 */

#define	IPF_DNS_PROXY

/*
 * map ... proxy port dns/udp 53 { block .cnn.com; }
 */
typedef	struct	ipf_dns_filter	{
	struct	ipf_dns_filter	*idns_next;
	char			*idns_name;
	int			idns_namelen;
	int			idns_pass;
} ipf_dns_filter_t;


typedef struct ipf_dns_softc_s {
	ipf_dns_filter_t	*ipf_p_dns_list;
	ipfrwlock_t		ipf_p_dns_rwlock;
	u_long			ipf_p_dns_compress;
	u_long			ipf_p_dns_toolong;
	u_long			ipf_p_dns_nospace;
} ipf_dns_softc_t;

int ipf_p_dns_allow_query __P((ipf_dns_softc_t *, dnsinfo_t *));
int ipf_p_dns_ctl __P((ipf_main_softc_t *, void *, ap_ctl_t *));
void ipf_p_dns_del __P((ipf_main_softc_t *, ap_session_t *));
int ipf_p_dns_get_name __P((ipf_dns_softc_t *, char *, int, char *, int));
int ipf_p_dns_inout __P((void *, fr_info_t *, ap_session_t *, nat_t *));
int ipf_p_dns_match __P((fr_info_t *, ap_session_t *, nat_t *));
int ipf_p_dns_match_names __P((ipf_dns_filter_t *, char *, int));
int ipf_p_dns_new __P((void *, fr_info_t *, ap_session_t *, nat_t *));
void *ipf_p_dns_soft_create __P((ipf_main_softc_t *));
void ipf_p_dns_soft_destroy __P((ipf_main_softc_t *, void *));

typedef struct {
	u_char		dns_id[2];
	u_short		dns_ctlword;
	u_short		dns_qdcount;
	u_short		dns_ancount;
	u_short		dns_nscount;
	u_short		dns_arcount;
} ipf_dns_hdr_t;

#define	DNS_QR(x)	((ntohs(x) & 0x8000) >> 15)
#define	DNS_OPCODE(x)	((ntohs(x) & 0x7800) >> 11)
#define	DNS_AA(x)	((ntohs(x) & 0x0400) >> 10)
#define	DNS_TC(x)	((ntohs(x) & 0x0200) >> 9)
#define	DNS_RD(x)	((ntohs(x) & 0x0100) >> 8)
#define	DNS_RA(x)	((ntohs(x) & 0x0080) >> 7)
#define	DNS_Z(x)	((ntohs(x) & 0x0070) >> 4)
#define	DNS_RCODE(x)	((ntohs(x) & 0x000f) >> 0)


void *
ipf_p_dns_soft_create(softc)
	ipf_main_softc_t *softc;
{
	ipf_dns_softc_t *softd;

	KMALLOC(softd, ipf_dns_softc_t *);
	if (softd == NULL)
		return NULL;

	bzero((char *)softd, sizeof(*softd));
	RWLOCK_INIT(&softd->ipf_p_dns_rwlock, "ipf dns rwlock");

	return softd;
}


void
ipf_p_dns_soft_destroy(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_dns_softc_t *softd = arg;
	ipf_dns_filter_t *idns;

	while ((idns = softd->ipf_p_dns_list) != NULL) {
		KFREES(idns->idns_name, idns->idns_namelen);
		idns->idns_name = NULL;
		idns->idns_namelen = 0;
		softd->ipf_p_dns_list = idns->idns_next;
		KFREE(idns);
	}
	RW_DESTROY(&softd->ipf_p_dns_rwlock);

	KFREE(softd);
}


int
ipf_p_dns_ctl(softc, arg, ctl)
	ipf_main_softc_t *softc;
	void *arg;
	ap_ctl_t *ctl;
{
	ipf_dns_softc_t *softd = arg;
	ipf_dns_filter_t *tmp, *idns, **idnsp;
	int error = 0;

	/*
	 * To make locking easier.
	 */
	KMALLOC(tmp, ipf_dns_filter_t *);

	WRITE_ENTER(&softd->ipf_p_dns_rwlock);
	for (idnsp = &softd->ipf_p_dns_list; (idns = *idnsp) != NULL;
	     idnsp = &idns->idns_next) {
		if (idns->idns_namelen != ctl->apc_dsize)
			continue;
		if (!strncmp(ctl->apc_data, idns->idns_name,
		    idns->idns_namelen))
			break;
	}

	switch (ctl->apc_cmd)
	{
	case APC_CMD_DEL :
		if (idns == NULL) {
			IPFERROR(80006);
			error = ESRCH;
			break;
		}
		*idnsp = idns->idns_next;
		idns->idns_next = NULL;
		KFREES(idns->idns_name, idns->idns_namelen);
		idns->idns_name = NULL;
		idns->idns_namelen = 0;
		KFREE(idns);
		break;
	case APC_CMD_ADD :
		if (idns != NULL) {
			IPFERROR(80007);
			error = EEXIST;
			break;
		}
		if (tmp == NULL) {
			IPFERROR(80008);
			error = ENOMEM;
			break;
		}
		idns = tmp;
		tmp = NULL;
		idns->idns_namelen = ctl->apc_dsize;
		idns->idns_name = ctl->apc_data;
		idns->idns_pass = ctl->apc_arg;
		idns->idns_next = NULL;
		*idnsp = idns;
		ctl->apc_data = NULL;
		ctl->apc_dsize = 0;
		break;
	default :
		IPFERROR(80009);
		error = EINVAL;
		break;
	}
	RWLOCK_EXIT(&softd->ipf_p_dns_rwlock);

	if (tmp != NULL) {
		KFREE(tmp);
		tmp = NULL;
	}

	return error;
}


/* ARGSUSED */
int
ipf_p_dns_new(arg, fin, aps, nat)
	void *arg;
	fr_info_t *fin;
	ap_session_t *aps;
	nat_t *nat;
{
	dnsinfo_t *di;
	int dlen;

	if (fin->fin_v != 4)
		return -1;

	dlen = fin->fin_dlen - sizeof(udphdr_t);
	if (dlen < sizeof(ipf_dns_hdr_t)) {
		/*
		 * No real DNS packet is smaller than that.
		 */
		return -1;
	}

	aps->aps_psiz = sizeof(dnsinfo_t);
	KMALLOCS(di, dnsinfo_t *, sizeof(dnsinfo_t));
	if (di == NULL) {
		printf("ipf_dns_new:KMALLOCS(%d) failed\n", sizeof(*di));
		return -1;
        }

	MUTEX_INIT(&di->dnsi_lock, "dns lock");

	aps->aps_data = di;

	dlen = fin->fin_dlen - sizeof(udphdr_t);
	COPYDATA(fin->fin_m, fin->fin_hlen + sizeof(udphdr_t),
		 MIN(dlen, sizeof(di->dnsi_buffer)), di->dnsi_buffer);
	di->dnsi_id = (di->dnsi_buffer[0] << 8) | di->dnsi_buffer[1];
	return 0;
}


/* ARGSUSED */
void
ipf_p_dns_del(softc, aps)
	ipf_main_softc_t *softc;
	ap_session_t *aps;
{
#ifdef USE_MUTEXES
	dnsinfo_t *di = aps->aps_data;

	MUTEX_DESTROY(&di->dnsi_lock);
#endif
	KFREES(aps->aps_data, aps->aps_psiz);
	aps->aps_data = NULL;
	aps->aps_psiz = 0;
}


/*
 * Tries to match the base string (in our ACL) with the query from a packet.
 */
int
ipf_p_dns_match_names(idns, query, qlen)
	ipf_dns_filter_t *idns;
	char *query;
	int qlen;
{
	int blen;
	char *base;

	blen = idns->idns_namelen;
	base = idns->idns_name;

	if (blen > qlen)
		return 1;

	if (blen == qlen)
		return strncasecmp(base, query, qlen);

	/*
	 * If the base string string is shorter than the query, allow the
	 * tail of the base to match the same length tail of the query *if*:
	 * - the base string starts with a '*' (*cnn.com)
	 * - the base string represents a domain (.cnn.com)
	 * as otherwise it would not be possible to block just "cnn.com"
	 * without also impacting "foocnn.com", etc.
	 */
	if (*base == '*') {
		base++;
		blen--;
	} else if (*base != '.')
		return 1;

	return strncasecmp(base, query + qlen - blen, blen);
}


int
ipf_p_dns_get_name(softd, start, len, buffer, buflen)
	ipf_dns_softc_t *softd;
	char *start;
	int len;
	char *buffer;
	int buflen;
{
	char *s, *t, clen;
	int slen, blen;

	s = start;
	t = buffer;
	slen = len;
	blen = buflen - 1;	/* Always make room for trailing \0 */

	while (*s != '\0') {
		clen = *s;
		if ((clen & 0xc0) == 0xc0) {	/* Doesn't do compression */
			softd->ipf_p_dns_compress++;
			return 0;
		}
		if (clen > slen) {
			softd->ipf_p_dns_toolong++;
			return 0;	/* Does the name run off the end? */
		}
		if ((clen + 1) > blen) {
			softd->ipf_p_dns_nospace++;
			return 0;	/* Enough room for name+.? */
		}
		s++;
		bcopy(s, t, clen);
		t += clen;
		s += clen;
		*t++ = '.';
		slen -= clen;
		blen -= (clen + 1);
	}

	*(t - 1) = '\0';
	return s - start;
}


int
ipf_p_dns_allow_query(softd, dnsi)
	ipf_dns_softc_t *softd;
	dnsinfo_t *dnsi;
{
	ipf_dns_filter_t *idns;
	int len;

	len = strlen(dnsi->dnsi_buffer);

	for (idns = softd->ipf_p_dns_list; idns != NULL; idns = idns->idns_next)
		if (ipf_p_dns_match_names(idns, dnsi->dnsi_buffer, len) == 0)
			return idns->idns_pass;
	return 0;
}


/* ARGSUSED */
int
ipf_p_dns_inout(arg, fin, aps, nat)
	void *arg;
	fr_info_t *fin;
	ap_session_t *aps;
	nat_t *nat;
{
	ipf_dns_softc_t *softd = arg;
	ipf_dns_hdr_t *dns;
	dnsinfo_t *di;
	char *data;
	int dlen, q, rc = 0;

	if (fin->fin_dlen < sizeof(*dns))
		return APR_ERR(1);

	dns = (ipf_dns_hdr_t *)((char *)fin->fin_dp + sizeof(udphdr_t));

	q = dns->dns_qdcount;

	data = (char *)(dns + 1);
	dlen = fin->fin_dlen - sizeof(*dns) - sizeof(udphdr_t);

	di = aps->aps_data;

	READ_ENTER(&softd->ipf_p_dns_rwlock);
	MUTEX_ENTER(&di->dnsi_lock);

	for (; (dlen > 0) && (q > 0); q--) {
		int len;

		len = ipf_p_dns_get_name(softd, data, dlen, di->dnsi_buffer,
					 sizeof(di->dnsi_buffer));
		if (len == 0) {
			rc = 1;
			break;
		}
		rc = ipf_p_dns_allow_query(softd, di);
		if (rc != 0)
			break;
		data += len;
		dlen -= len;
	}
	MUTEX_EXIT(&di->dnsi_lock);
	RWLOCK_EXIT(&softd->ipf_p_dns_rwlock);

	return APR_ERR(rc);
}


/* ARGSUSED */
int
ipf_p_dns_match(fin, aps, nat)
	fr_info_t *fin;
	ap_session_t *aps;
	nat_t *nat;
{
	dnsinfo_t *di = aps->aps_data;
	ipf_dns_hdr_t *dnh;

	if ((fin->fin_dlen < sizeof(u_short)) || (fin->fin_flx & FI_FRAG))
                return -1;

	dnh = (ipf_dns_hdr_t *)((char *)fin->fin_dp + sizeof(udphdr_t));
	if (((dnh->dns_id[0] << 8) | dnh->dns_id[1]) != di->dnsi_id)
		return -1;
	return 0;
}
