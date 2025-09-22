/*	$OpenBSD: pf_osfp.c,v 1.48 2024/04/13 23:44:11 jsg Exp $ */

/*
 * Copyright (c) 2003 Mike Frantzen <frantzen@w4g.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <sys/param.h>
#include <sys/socket.h>
#ifdef _KERNEL
#include <sys/systm.h>
#include <sys/pool.h>
#endif /* _KERNEL */
#include <sys/queue.h>
#include <sys/mbuf.h>
#include <sys/syslog.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#ifdef INET6
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#endif /* INET6 */

#include <net/pfvar.h>
#include <net/pfvar_priv.h>

#ifdef _KERNEL
typedef struct pool pool_t;

#else	/* !_KERNEL */
/* Userland equivalents so we can lend code to tcpdump et al. */

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#define pool_t			int
#define pool_get(pool, flags)	malloc(*(pool))
#define pool_put(pool, item)	free(item)
#define pool_init(pool, size, a, ao, f, m, p)	(*(pool)) = (size)

#define PF_LOCK()
#define PF_UNLOCK()
#define PF_ASSERT_LOCKED()

#ifdef PFDEBUG
#include <sys/stdarg.h>	/* for DPFPRINTF() */
#endif /* PFDEBUG */

#endif /* _KERNEL */

/*
 * Protection/ownership:
 *	I	immutable after pf_osfp_initialize()
 *	p	pf_lock
 */

SLIST_HEAD(pf_osfp_list, pf_os_fingerprint) pf_osfp_list =
    SLIST_HEAD_INITIALIZER(pf_osfp_list);	/* [p] */
pool_t pf_osfp_entry_pl;			/* [I] */
pool_t pf_osfp_pl;				/* [I] */

struct pf_os_fingerprint	*pf_osfp_find(struct pf_os_fingerprint *,
				    u_int8_t);
struct pf_os_fingerprint	*pf_osfp_find_exact(struct pf_os_fingerprint *);
void				 pf_osfp_insert(struct pf_os_fingerprint *);


#ifdef _KERNEL
/*
 * Passively fingerprint the OS of the host (IPv4 TCP SYN packets only)
 * Returns the list of possible OSes.
 */
struct pf_osfp_enlist *
pf_osfp_fingerprint(struct pf_pdesc *pd)
{
	struct tcphdr	*th = &pd->hdr.tcp;
	struct ip	*ip = NULL;
	struct ip6_hdr	*ip6 = NULL;
	char		 hdr[60];

	if (pd->proto != IPPROTO_TCP)
		return (NULL);

	switch (pd->af) {
	case AF_INET:
		ip = mtod(pd->m, struct ip *);
		break;
	case AF_INET6:
		ip6 = mtod(pd->m, struct ip6_hdr *);
		break;
	}
	if (!pf_pull_hdr(pd->m, pd->off, hdr, th->th_off << 2, NULL, pd->af))
		return (NULL);

	return (pf_osfp_fingerprint_hdr(ip, ip6, (struct tcphdr *)hdr));
}
#endif /* _KERNEL */

struct pf_osfp_enlist *
pf_osfp_fingerprint_hdr(const struct ip *ip, const struct ip6_hdr *ip6,
    const struct tcphdr *tcp)
{
	struct pf_os_fingerprint fp, *fpresult;
	int cnt, optlen = 0;
	const u_int8_t *optp;
#ifdef _KERNEL
	char srcname[128];
#else	/* !_KERNEL */
	char srcname[NI_MAXHOST];
#endif	/* _KERNEL */

	if ((tcp->th_flags & (TH_SYN|TH_ACK)) != TH_SYN)
		return (NULL);
	if (ip) {
		if ((ip->ip_off & htons(IP_OFFMASK)) != 0)
			return (NULL);
	}

	memset(&fp, 0, sizeof(fp));

	if (ip) {
#ifndef _KERNEL
		struct sockaddr_in sin;
#endif	/* _KERNEL */

		fp.fp_psize = ntohs(ip->ip_len);
		fp.fp_ttl = ip->ip_ttl;
		if (ip->ip_off & htons(IP_DF))
			fp.fp_flags |= PF_OSFP_DF;
#ifdef _KERNEL
		inet_ntop(AF_INET, &ip->ip_src, srcname, sizeof(srcname));
#else	/* !_KERNEL */
		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_len = sizeof(struct sockaddr_in);
		sin.sin_addr = ip->ip_src;
		(void)getnameinfo((struct sockaddr *)&sin,
		    sizeof(struct sockaddr_in), srcname, sizeof(srcname),
		    NULL, 0, NI_NUMERICHOST);
#endif	/* _KERNEL */
	}
#ifdef INET6
	else if (ip6) {
#ifndef _KERNEL
		struct sockaddr_in6 sin6;
#endif	/* !_KERNEL */

		/* jumbo payload? */
		fp.fp_psize = sizeof(struct ip6_hdr) + ntohs(ip6->ip6_plen);
		fp.fp_ttl = ip6->ip6_hlim;
		fp.fp_flags |= PF_OSFP_DF;
		fp.fp_flags |= PF_OSFP_INET6;
#ifdef _KERNEL
		inet_ntop(AF_INET6, &ip6->ip6_src, srcname, sizeof(srcname));
#else	/* !_KERNEL */
		memset(&sin6, 0, sizeof(sin6));
		sin6.sin6_family = AF_INET6;
		sin6.sin6_len = sizeof(struct sockaddr_in6);
		sin6.sin6_addr = ip6->ip6_src;
		(void)getnameinfo((struct sockaddr *)&sin6,
		    sizeof(struct sockaddr_in6), srcname, sizeof(srcname),
		    NULL, 0, NI_NUMERICHOST);
#endif	/* !_KERNEL */
	}
#endif	/* INET6 */
	else
		return (NULL);
	fp.fp_wsize = ntohs(tcp->th_win);


	cnt = (tcp->th_off << 2) - sizeof(*tcp);
	optp = (const u_int8_t *)((const char *)tcp + sizeof(*tcp));
	for (; cnt > 0; cnt -= optlen, optp += optlen) {
		if (*optp == TCPOPT_EOL)
			break;

		fp.fp_optcnt++;
		if (*optp == TCPOPT_NOP) {
			fp.fp_tcpopts = (fp.fp_tcpopts << PF_OSFP_TCPOPT_BITS) |
			    PF_OSFP_TCPOPT_NOP;
			optlen = 1;
		} else {
			if (cnt < 2)
				return (NULL);
			optlen = optp[1];
			if (optlen > cnt || optlen < 2)
				return (NULL);
			switch (*optp) {
			case TCPOPT_MAXSEG:
				if (optlen >= TCPOLEN_MAXSEG)
					memcpy(&fp.fp_mss, &optp[2],
					    sizeof(fp.fp_mss));
				fp.fp_tcpopts = (fp.fp_tcpopts <<
				    PF_OSFP_TCPOPT_BITS) | PF_OSFP_TCPOPT_MSS;
				fp.fp_mss = ntohs(fp.fp_mss);
				break;
			case TCPOPT_WINDOW:
				if (optlen >= TCPOLEN_WINDOW)
					memcpy(&fp.fp_wscale, &optp[2],
					    sizeof(fp.fp_wscale));
				fp.fp_tcpopts = (fp.fp_tcpopts <<
				    PF_OSFP_TCPOPT_BITS) |
				    PF_OSFP_TCPOPT_WSCALE;
				break;
			case TCPOPT_SACK_PERMITTED:
				fp.fp_tcpopts = (fp.fp_tcpopts <<
				    PF_OSFP_TCPOPT_BITS) | PF_OSFP_TCPOPT_SACK;
				break;
			case TCPOPT_TIMESTAMP:
				if (optlen >= TCPOLEN_TIMESTAMP) {
					u_int32_t ts;
					memcpy(&ts, &optp[2], sizeof(ts));
					if (ts == 0)
						fp.fp_flags |= PF_OSFP_TS0;

				}
				fp.fp_tcpopts = (fp.fp_tcpopts <<
				    PF_OSFP_TCPOPT_BITS) | PF_OSFP_TCPOPT_TS;
				break;
			default:
				return (NULL);
			}
		}
		optlen = MAX(optlen, 1);	/* paranoia */
	}

	DPFPRINTF(LOG_INFO,
	    "fingerprinted %s:%d  %d:%d:%d:%d:%llx (%d) "
	    "(TS=%s,M=%s%d,W=%s%d)",
	    srcname, ntohs(tcp->th_sport),
	    fp.fp_wsize, fp.fp_ttl, (fp.fp_flags & PF_OSFP_DF) != 0,
	    fp.fp_psize, (long long int)fp.fp_tcpopts, fp.fp_optcnt,
	    (fp.fp_flags & PF_OSFP_TS0) ? "0" : "",
	    (fp.fp_flags & PF_OSFP_MSS_MOD) ? "%" :
	    (fp.fp_flags & PF_OSFP_MSS_DC) ? "*" : "",
	    fp.fp_mss,
	    (fp.fp_flags & PF_OSFP_WSCALE_MOD) ? "%" :
	    (fp.fp_flags & PF_OSFP_WSCALE_DC) ? "*" : "",
	    fp.fp_wscale);

	if ((fpresult = pf_osfp_find(&fp, PF_OSFP_MAXTTL_OFFSET)))
		return (&fpresult->fp_oses);
	return (NULL);
}

/* Match a fingerprint ID against a list of OSes */
int
pf_osfp_match(struct pf_osfp_enlist *list, pf_osfp_t os)
{
	struct pf_osfp_entry *entry;
	int os_class, os_version, os_subtype;
	int en_class, en_version, en_subtype;

	if (os == PF_OSFP_ANY)
		return (1);
	if (list == NULL) {
		DPFPRINTF(LOG_INFO, "osfp no match against %x", os);
		return (os == PF_OSFP_UNKNOWN);
	}
	PF_OSFP_UNPACK(os, os_class, os_version, os_subtype);
	SLIST_FOREACH(entry, list, fp_entry) {
		PF_OSFP_UNPACK(entry->fp_os, en_class, en_version, en_subtype);
		if ((os_class == PF_OSFP_ANY || en_class == os_class) &&
		    (os_version == PF_OSFP_ANY || en_version == os_version) &&
		    (os_subtype == PF_OSFP_ANY || en_subtype == os_subtype)) {
			DPFPRINTF(LOG_INFO,
			    "osfp matched %s %s %s  %x==%x",
			    entry->fp_class_nm, entry->fp_version_nm,
			    entry->fp_subtype_nm, os, entry->fp_os);
			return (1);
		}
	}
	DPFPRINTF(LOG_INFO, "fingerprint 0x%x didn't match", os);
	return (0);
}

/* Initialize the OS fingerprint system */
void
pf_osfp_initialize(void)
{
	pool_init(&pf_osfp_entry_pl, sizeof(struct pf_osfp_entry), 0,
	    IPL_NONE, PR_WAITOK, "pfosfpen", NULL);
	pool_init(&pf_osfp_pl, sizeof(struct pf_os_fingerprint), 0,
	    IPL_NONE, PR_WAITOK, "pfosfp", NULL);
}

/* Flush the fingerprint list */
void
pf_osfp_flush(void)
{
	struct pf_os_fingerprint *fp;
	struct pf_osfp_entry *entry;

	PF_LOCK();
	while ((fp = SLIST_FIRST(&pf_osfp_list))) {
		SLIST_REMOVE_HEAD(&pf_osfp_list, fp_next);
		while ((entry = SLIST_FIRST(&fp->fp_oses))) {
			SLIST_REMOVE_HEAD(&fp->fp_oses, fp_entry);
			pool_put(&pf_osfp_entry_pl, entry);
		}
		pool_put(&pf_osfp_pl, fp);
	}
	PF_UNLOCK();
}


/* Add a fingerprint */
int
pf_osfp_add(struct pf_osfp_ioctl *fpioc)
{
	struct pf_os_fingerprint *fp, *fp_prealloc, fpadd;
	struct pf_osfp_entry *entry;

	memset(&fpadd, 0, sizeof(fpadd));
	fpadd.fp_tcpopts = fpioc->fp_tcpopts;
	fpadd.fp_wsize = fpioc->fp_wsize;
	fpadd.fp_psize = fpioc->fp_psize;
	fpadd.fp_mss = fpioc->fp_mss;
	fpadd.fp_flags = fpioc->fp_flags;
	fpadd.fp_optcnt = fpioc->fp_optcnt;
	fpadd.fp_wscale = fpioc->fp_wscale;
	fpadd.fp_ttl = fpioc->fp_ttl;

	DPFPRINTF(LOG_DEBUG,
	    "adding osfp %s %s %s = %s%d:%d:%d:%s%d:0x%llx %d "
	    "(TS=%s,M=%s%d,W=%s%d) %x",
	    fpioc->fp_os.fp_class_nm, fpioc->fp_os.fp_version_nm,
	    fpioc->fp_os.fp_subtype_nm,
	    (fpadd.fp_flags & PF_OSFP_WSIZE_MOD) ? "%" :
	    (fpadd.fp_flags & PF_OSFP_WSIZE_MSS) ? "S" :
	    (fpadd.fp_flags & PF_OSFP_WSIZE_MTU) ? "T" :
	    (fpadd.fp_flags & PF_OSFP_WSIZE_DC) ? "*" : "",
	    fpadd.fp_wsize,
	    fpadd.fp_ttl,
	    (fpadd.fp_flags & PF_OSFP_DF) ? 1 : 0,
	    (fpadd.fp_flags & PF_OSFP_PSIZE_MOD) ? "%" :
	    (fpadd.fp_flags & PF_OSFP_PSIZE_DC) ? "*" : "",
	    fpadd.fp_psize,
	    (long long int)fpadd.fp_tcpopts, fpadd.fp_optcnt,
	    (fpadd.fp_flags & PF_OSFP_TS0) ? "0" : "",
	    (fpadd.fp_flags & PF_OSFP_MSS_MOD) ? "%" :
	    (fpadd.fp_flags & PF_OSFP_MSS_DC) ? "*" : "",
	    fpadd.fp_mss,
	    (fpadd.fp_flags & PF_OSFP_WSCALE_MOD) ? "%" :
	    (fpadd.fp_flags & PF_OSFP_WSCALE_DC) ? "*" : "",
	    fpadd.fp_wscale,
	    fpioc->fp_os.fp_os);

	entry = pool_get(&pf_osfp_entry_pl, PR_WAITOK|PR_LIMITFAIL);
	if (entry == NULL)
		return (ENOMEM);

	fp_prealloc = pool_get(&pf_osfp_pl, PR_WAITOK|PR_ZERO|PR_LIMITFAIL);
	if (fp_prealloc == NULL) {
		pool_put(&pf_osfp_entry_pl, entry);
		return (ENOMEM);
	}

	PF_LOCK();
	if ((fp = pf_osfp_find_exact(&fpadd))) {
		struct pf_osfp_entry *tentry;

		SLIST_FOREACH(tentry, &fp->fp_oses, fp_entry) {
			if (PF_OSFP_ENTRY_EQ(tentry, &fpioc->fp_os)) {
				PF_UNLOCK();
				pool_put(&pf_osfp_entry_pl, entry);
				pool_put(&pf_osfp_pl, fp_prealloc);
				return (EEXIST);
			}
		}
	} else {
		fp = fp_prealloc;
		fp_prealloc = NULL;
		fp->fp_tcpopts = fpioc->fp_tcpopts;
		fp->fp_wsize = fpioc->fp_wsize;
		fp->fp_psize = fpioc->fp_psize;
		fp->fp_mss = fpioc->fp_mss;
		fp->fp_flags = fpioc->fp_flags;
		fp->fp_optcnt = fpioc->fp_optcnt;
		fp->fp_wscale = fpioc->fp_wscale;
		fp->fp_ttl = fpioc->fp_ttl;
		SLIST_INIT(&fp->fp_oses);
		pf_osfp_insert(fp);
	}
	memcpy(entry, &fpioc->fp_os, sizeof(*entry));

	/* Make sure the strings are NUL terminated */
	entry->fp_class_nm[sizeof(entry->fp_class_nm)-1] = '\0';
	entry->fp_version_nm[sizeof(entry->fp_version_nm)-1] = '\0';
	entry->fp_subtype_nm[sizeof(entry->fp_subtype_nm)-1] = '\0';

	SLIST_INSERT_HEAD(&fp->fp_oses, entry, fp_entry);
	PF_UNLOCK();

#ifdef PFDEBUG
	if ((fp = pf_osfp_validate()))
		DPFPRINTF(LOG_NOTICE,
		    "Invalid fingerprint list");
#endif /* PFDEBUG */

	if (fp_prealloc != NULL)
		pool_put(&pf_osfp_pl, fp_prealloc);

	return (0);
}


/* Find a fingerprint in the list */
struct pf_os_fingerprint *
pf_osfp_find(struct pf_os_fingerprint *find, u_int8_t ttldiff)
{
	struct pf_os_fingerprint *f;

	PF_ASSERT_LOCKED();

#define MATCH_INT(_MOD, _DC, _field)					\
	if ((f->fp_flags & _DC) == 0) {					\
		if ((f->fp_flags & _MOD) == 0) {			\
			if (f->_field != find->_field)			\
				continue;				\
		} else {						\
			if (f->_field == 0 || find->_field % f->_field)	\
				continue;				\
		}							\
	}

	SLIST_FOREACH(f, &pf_osfp_list, fp_next) {
		if (f->fp_tcpopts != find->fp_tcpopts ||
		    f->fp_optcnt != find->fp_optcnt ||
		    f->fp_ttl < find->fp_ttl ||
		    f->fp_ttl - find->fp_ttl > ttldiff ||
		    (f->fp_flags & (PF_OSFP_DF|PF_OSFP_TS0)) !=
		    (find->fp_flags & (PF_OSFP_DF|PF_OSFP_TS0)))
			continue;

		MATCH_INT(PF_OSFP_PSIZE_MOD, PF_OSFP_PSIZE_DC, fp_psize)
		MATCH_INT(PF_OSFP_MSS_MOD, PF_OSFP_MSS_DC, fp_mss)
		MATCH_INT(PF_OSFP_WSCALE_MOD, PF_OSFP_WSCALE_DC, fp_wscale)
		if ((f->fp_flags & PF_OSFP_WSIZE_DC) == 0) {
			if (f->fp_flags & PF_OSFP_WSIZE_MSS) {
				if (find->fp_mss == 0)
					continue;

/* Some "smart" NAT devices and DSL routers will tweak the MSS size and
 * will set it to whatever is suitable for the link type.
 */
#define SMART_MSS	1460
				if ((find->fp_wsize % find->fp_mss ||
				    find->fp_wsize / find->fp_mss !=
				    f->fp_wsize) &&
				    (find->fp_wsize % SMART_MSS ||
				    find->fp_wsize / SMART_MSS !=
				    f->fp_wsize))
					continue;
			} else if (f->fp_flags & PF_OSFP_WSIZE_MTU) {
				if (find->fp_mss == 0)
					continue;

#define MTUOFF	(sizeof(struct ip) + sizeof(struct tcphdr))
#define SMART_MTU	(SMART_MSS + MTUOFF)
				if ((find->fp_wsize % (find->fp_mss + MTUOFF) ||
				    find->fp_wsize / (find->fp_mss + MTUOFF) !=
				    f->fp_wsize) &&
				    (find->fp_wsize % SMART_MTU ||
				    find->fp_wsize / SMART_MTU !=
				    f->fp_wsize))
					continue;
			} else if (f->fp_flags & PF_OSFP_WSIZE_MOD) {
				if (f->fp_wsize == 0 || find->fp_wsize %
				    f->fp_wsize)
					continue;
			} else {
				if (f->fp_wsize != find->fp_wsize)
					continue;
			}
		}
		return (f);
	}

	return (NULL);
}

/* Find an exact fingerprint in the list */
struct pf_os_fingerprint *
pf_osfp_find_exact(struct pf_os_fingerprint *find)
{
	struct pf_os_fingerprint *f;

	PF_ASSERT_LOCKED();

	SLIST_FOREACH(f, &pf_osfp_list, fp_next) {
		if (f->fp_tcpopts == find->fp_tcpopts &&
		    f->fp_wsize == find->fp_wsize &&
		    f->fp_psize == find->fp_psize &&
		    f->fp_mss == find->fp_mss &&
		    f->fp_flags == find->fp_flags &&
		    f->fp_optcnt == find->fp_optcnt &&
		    f->fp_wscale == find->fp_wscale &&
		    f->fp_ttl == find->fp_ttl)
			return (f);
	}

	return (NULL);
}

/* Insert a fingerprint into the list */
void
pf_osfp_insert(struct pf_os_fingerprint *ins)
{
	struct pf_os_fingerprint *f, *prev = NULL;

	PF_ASSERT_LOCKED();

	/* XXX need to go semi tree based.  can key on tcp options */

	SLIST_FOREACH(f, &pf_osfp_list, fp_next)
		prev = f;
	if (prev)
		SLIST_INSERT_AFTER(prev, ins, fp_next);
	else
		SLIST_INSERT_HEAD(&pf_osfp_list, ins, fp_next);
}

/* Fill a fingerprint by its number (from an ioctl) */
int
pf_osfp_get(struct pf_osfp_ioctl *fpioc)
{
	struct pf_os_fingerprint *fp;
	struct pf_osfp_entry *entry;
	int num = fpioc->fp_getnum;
	int i = 0;

	memset(fpioc, 0, sizeof(*fpioc));
	PF_LOCK();
	SLIST_FOREACH(fp, &pf_osfp_list, fp_next) {
		SLIST_FOREACH(entry, &fp->fp_oses, fp_entry) {
			if (i++ == num) {
				fpioc->fp_mss = fp->fp_mss;
				fpioc->fp_wsize = fp->fp_wsize;
				fpioc->fp_flags = fp->fp_flags;
				fpioc->fp_psize = fp->fp_psize;
				fpioc->fp_ttl = fp->fp_ttl;
				fpioc->fp_wscale = fp->fp_wscale;
				fpioc->fp_getnum = num;
				memcpy(&fpioc->fp_os, entry,
				    sizeof(fpioc->fp_os));
				PF_UNLOCK();
				return (0);
			}
		}
	}
	PF_UNLOCK();

	return (EBUSY);
}


/* Validate that each signature is reachable */
struct pf_os_fingerprint *
pf_osfp_validate(void)
{
	struct pf_os_fingerprint *f, *f2, find;

	PF_ASSERT_LOCKED();

	SLIST_FOREACH(f, &pf_osfp_list, fp_next) {
		memcpy(&find, f, sizeof(find));

		/* We do a few MSS/th_win percolations to make things unique */
		if (find.fp_mss == 0)
			find.fp_mss = 128;
		if (f->fp_flags & PF_OSFP_WSIZE_MSS)
			find.fp_wsize *= find.fp_mss;
		else if (f->fp_flags & PF_OSFP_WSIZE_MTU)
			find.fp_wsize *= (find.fp_mss + 40);
		else if (f->fp_flags & PF_OSFP_WSIZE_MOD)
			find.fp_wsize *= 2;
		if (f != (f2 = pf_osfp_find(&find, 0))) {
			if (f2)
				DPFPRINTF(LOG_NOTICE,
				    "Found \"%s %s %s\" instead of "
				    "\"%s %s %s\"\n",
				    SLIST_FIRST(&f2->fp_oses)->fp_class_nm,
				    SLIST_FIRST(&f2->fp_oses)->fp_version_nm,
				    SLIST_FIRST(&f2->fp_oses)->fp_subtype_nm,
				    SLIST_FIRST(&f->fp_oses)->fp_class_nm,
				    SLIST_FIRST(&f->fp_oses)->fp_version_nm,
				    SLIST_FIRST(&f->fp_oses)->fp_subtype_nm);
			else
				DPFPRINTF(LOG_NOTICE,
				    "Couldn't find \"%s %s %s\"\n",
				    SLIST_FIRST(&f->fp_oses)->fp_class_nm,
				    SLIST_FIRST(&f->fp_oses)->fp_version_nm,
				    SLIST_FIRST(&f->fp_oses)->fp_subtype_nm);
			return (f);
		}
	}
	return (NULL);
}
