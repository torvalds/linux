/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * @(#)ip_fil.h	1.35 6/5/96
 * $FreeBSD$
 * Id: ip_fil.h,v 2.170.2.51 2007/10/10 09:48:03 darrenr Exp $
 */

#ifndef	__IP_FIL_H__
#define	__IP_FIL_H__

# include <netinet/in.h>

#include "netinet/ip_compat.h"
#include "netinet/ipf_rb.h"
#if NETBSD_GE_REV(104040000)
# include <sys/callout.h>
#endif
#if defined(BSD) && defined(_KERNEL)
#  include <sys/selinfo.h>
#endif

#ifndef	SOLARIS
# if defined(sun) && defined(__SVR4)
#  define	SOLARIS		1
# else
#  define	SOLARIS		0
# endif
#endif

#ifndef	__P
# ifdef	__STDC__
#  define	__P(x)	x
# else
#  define	__P(x)	()
# endif
#endif

#if defined(__STDC__) || defined(__GNUC__)
# define	SIOCADAFR	_IOW('r', 60, struct ipfobj)
# define	SIOCRMAFR	_IOW('r', 61, struct ipfobj)
# define	SIOCSETFF	_IOW('r', 62, u_int)
# define	SIOCGETFF	_IOR('r', 63, u_int)
# define	SIOCGETFS	_IOWR('r', 64, struct ipfobj)
# define	SIOCIPFFL	_IOWR('r', 65, int)
# define	SIOCIPFFB	_IOR('r', 66, int)
# define	SIOCADIFR	_IOW('r', 67, struct ipfobj)
# define	SIOCRMIFR	_IOW('r', 68, struct ipfobj)
# define	SIOCSWAPA	_IOR('r', 69, u_int)
# define	SIOCINAFR	_IOW('r', 70, struct ipfobj)
# define	SIOCINIFR	_IOW('r', 71, struct ipfobj)
# define	SIOCFRENB	_IOW('r', 72, u_int)
# define	SIOCFRSYN	_IOW('r', 73, u_int)
# define	SIOCFRZST	_IOWR('r', 74, struct ipfobj)
# define	SIOCZRLST	_IOWR('r', 75, struct ipfobj)
# define	SIOCAUTHW	_IOWR('r', 76, struct ipfobj)
# define	SIOCAUTHR	_IOWR('r', 77, struct ipfobj)
# define	SIOCSTAT1	_IOWR('r', 78, struct ipfobj)
# define	SIOCSTLCK	_IOWR('r', 79, u_int)
# define	SIOCSTPUT	_IOWR('r', 80, struct ipfobj)
# define	SIOCSTGET	_IOWR('r', 81, struct ipfobj)
# define	SIOCSTGSZ	_IOWR('r', 82, struct ipfobj)
# define	SIOCSTAT2	_IOWR('r', 83, struct ipfobj)
# define	SIOCSETLG	_IOWR('r', 84, int)
# define	SIOCGETLG	_IOWR('r', 85, int)
# define	SIOCFUNCL	_IOWR('r', 86, struct ipfunc_resolve)
# define	SIOCIPFGETNEXT	_IOWR('r', 87, struct ipfobj)
# define	SIOCIPFGET	_IOWR('r', 88, struct ipfobj)
# define	SIOCIPFSET	_IOWR('r', 89, struct ipfobj)
# define	SIOCIPFL6	_IOWR('r', 90, int)
# define	SIOCIPFITER	_IOWR('r', 91, struct ipfobj)
# define	SIOCGENITER	_IOWR('r', 92, struct ipfobj)
# define	SIOCGTABL	_IOWR('r', 93, struct ipfobj)
# define	SIOCIPFDELTOK	_IOWR('r', 94, int)
# define	SIOCLOOKUPITER	_IOWR('r', 95, struct ipfobj)
# define	SIOCGTQTAB	_IOWR('r', 96, struct ipfobj)
# define	SIOCMATCHFLUSH	_IOWR('r', 97, struct ipfobj)
# define	SIOCIPFINTERROR	_IOR('r', 98, int)
#else
# define	SIOCADAFR	_IOW(r, 60, struct ipfobj)
# define	SIOCRMAFR	_IOW(r, 61, struct ipfobj)
# define	SIOCSETFF	_IOW(r, 62, u_int)
# define	SIOCGETFF	_IOR(r, 63, u_int)
# define	SIOCGETFS	_IOWR(r, 64, struct ipfobj)
# define	SIOCIPFFL	_IOWR(r, 65, int)
# define	SIOCIPFFB	_IOR(r, 66, int)
# define	SIOCADIFR	_IOW(r, 67, struct ipfobj)
# define	SIOCRMIFR	_IOW(r, 68, struct ipfobj)
# define	SIOCSWAPA	_IOR(r, 69, u_int)
# define	SIOCINAFR	_IOW(r, 70, struct ipfobj)
# define	SIOCINIFR	_IOW(r, 71, struct ipfobj)
# define	SIOCFRENB	_IOW(r, 72, u_int)
# define	SIOCFRSYN	_IOW(r, 73, u_int)
# define	SIOCFRZST	_IOWR(r, 74, struct ipfobj)
# define	SIOCZRLST	_IOWR(r, 75, struct ipfobj)
# define	SIOCAUTHW	_IOWR(r, 76, struct ipfobj)
# define	SIOCAUTHR	_IOWR(r, 77, struct ipfobj)
# define	SIOCSTAT1	_IOWR(r, 78, struct ipfobj)
# define	SIOCSTLCK	_IOWR(r, 79, u_int)
# define	SIOCSTPUT	_IOWR(r, 80, struct ipfobj)
# define	SIOCSTGET	_IOWR(r, 81, struct ipfobj)
# define	SIOCSTGSZ	_IOWR(r, 82, struct ipfobj)
# define	SIOCSTAT2	_IOWR(r, 83, struct ipfobj)
# define	SIOCSETLG	_IOWR(r, 84, int)
# define	SIOCGETLG	_IOWR(r, 85, int)
# define	SIOCFUNCL	_IOWR(r, 86, struct ipfunc_resolve)
# define	SIOCIPFGETNEXT	_IOWR(r, 87, struct ipfobj)
# define	SIOCIPFGET	_IOWR(r, 88, struct ipfobj)
# define	SIOCIPFSET	_IOWR(r, 89, struct ipfobj)
# define	SIOCIPFL6	_IOWR(r, 90, int)
# define	SIOCIPFITER	_IOWR(r, 91, struct ipfobj)
# define	SIOCGENITER	_IOWR(r, 92, struct ipfobj)
# define	SIOCGTABL	_IOWR(r, 93, struct ipfobj)
# define	SIOCIPFDELTOK	_IOWR(r, 94, int)
# define	SIOCLOOKUPITER	_IOWR(r, 95, struct ipfobj)
# define	SIOCGTQTAB	_IOWR(r, 96, struct ipfobj)
# define	SIOCMATCHFLUSH	_IOWR(r, 97, struct ipfobj)
# define	SIOCIPFINTERROR	_IOR(r, 98, int)
#endif
#define	SIOCADDFR	SIOCADAFR
#define	SIOCDELFR	SIOCRMAFR
#define	SIOCINSFR	SIOCINAFR
#define	SIOCATHST	SIOCSTAT1
#define	SIOCGFRST	SIOCSTAT2


struct ipscan;
struct ifnet;
struct ipf_main_softc_s;

typedef	int	(* lookupfunc_t) __P((struct ipf_main_softc_s *, void *,
				      int, void *, u_int));

/*
 * i6addr is used as a container for both IPv4 and IPv6 addresses, as well
 * as other types of objects, depending on its qualifier.
 */
typedef	union	i6addr	{
	u_32_t	i6[4];
	struct	in_addr	in4;
#ifdef	USE_INET6
	struct	in6_addr in6;
#endif
	void	*vptr[2];
	lookupfunc_t	lptr[2];
	struct {
		u_short	type;
		u_short	subtype;
		int	name;
	} i6un;
} i6addr_t;

#define in4_addr	in4.s_addr
#define	iplookupnum	i6[1]
#define	iplookupname	i6un.name
#define	iplookuptype	i6un.type
#define	iplookupsubtype	i6un.subtype
/*
 * NOTE: These DO overlap the above on 64bit systems and this IS recognised.
 */
#define	iplookupptr	vptr[0]
#define	iplookupfunc	lptr[1]

#define	I60(x)	(((u_32_t *)(x))[0])
#define	I61(x)	(((u_32_t *)(x))[1])
#define	I62(x)	(((u_32_t *)(x))[2])
#define	I63(x)	(((u_32_t *)(x))[3])
#define	HI60(x)	ntohl(((u_32_t *)(x))[0])
#define	HI61(x)	ntohl(((u_32_t *)(x))[1])
#define	HI62(x)	ntohl(((u_32_t *)(x))[2])
#define	HI63(x)	ntohl(((u_32_t *)(x))[3])

#define	IP6_EQ(a,b)	((I63(a) == I63(b)) && (I62(a) == I62(b)) && \
			 (I61(a) == I61(b)) && (I60(a) == I60(b)))
#define	IP6_NEQ(a,b)	((I63(a) != I63(b)) || (I62(a) != I62(b)) || \
			 (I61(a) != I61(b)) || (I60(a) != I60(b)))
#define IP6_ISZERO(a)   ((I60(a) | I61(a) | I62(a) | I63(a)) == 0)
#define IP6_NOTZERO(a)  ((I60(a) | I61(a) | I62(a) | I63(a)) != 0)
#define	IP6_ISONES(a)	((I63(a) == 0xffffffff) && (I62(a) == 0xffffffff) && \
			 (I61(a) == 0xffffffff) && (I60(a) == 0xffffffff))
#define	IP6_GT(a,b)	(ntohl(HI60(a)) > ntohl(HI60(b)) || \
			 (HI60(a) == HI60(b) && \
			  (ntohl(HI61(a)) > ntohl(HI61(b)) || \
			   (HI61(a) == HI61(b) && \
			    (ntohl(HI62(a)) > ntohl(HI62(b)) || \
			     (HI62(a) == HI62(b) && \
			      ntohl(HI63(a)) > ntohl(HI63(b))))))))
#define	IP6_LT(a,b)	(ntohl(HI60(a)) < ntohl(HI60(b)) || \
			 (HI60(a) == HI60(b) && \
			  (ntohl(HI61(a)) < ntohl(HI61(b)) || \
			   (HI61(a) == HI61(b) && \
			    (ntohl(HI62(a)) < ntohl(HI62(b)) || \
			     (HI62(a) == HI62(b) && \
			      ntohl(HI63(a)) < ntohl(HI63(b))))))))
#define	NLADD(n,x)	htonl(ntohl(n) + (x))
#define	IP6_INC(a)	\
		do { u_32_t *_i6 = (u_32_t *)(a); \
		  _i6[3] = NLADD(_i6[3], 1); \
		  if (_i6[3] == 0) { \
			_i6[2] = NLADD(_i6[2], 1); \
			if (_i6[2] == 0) { \
				_i6[1] = NLADD(_i6[1], 1); \
				if (_i6[1] == 0) { \
					_i6[0] = NLADD(_i6[0], 1); \
				} \
			} \
		  } \
		} while (0)
#define	IP6_ADD(a,x,d)	\
		do { i6addr_t *_s = (i6addr_t *)(a); \
		  i6addr_t *_d = (i6addr_t *)(d); \
		  _d->i6[0] = NLADD(_s->i6[0], x); \
		  if (ntohl(_d->i6[0]) < ntohl(_s->i6[0])) { \
			_d->i6[1] = NLADD(_d->i6[1], 1); \
			if (ntohl(_d->i6[1]) < ntohl(_s->i6[1])) { \
				_d->i6[2] = NLADD(_d->i6[2], 1); \
				if (ntohl(_d->i6[2]) < ntohl(_s->i6[2])) { \
					_d->i6[3] = NLADD(_d->i6[3], 1); \
				} \
			} \
		  } \
		} while (0)
#define	IP6_AND(a,b,d)	do { i6addr_t *_s1 = (i6addr_t *)(a); \
			  i6addr_t *_s2 = (i6addr_t *)(b); \
			  i6addr_t *_d = (i6addr_t *)(d); \
			  _d->i6[0] = _s1->i6[0] & _s2->i6[0]; \
			  _d->i6[1] = _s1->i6[1] & _s2->i6[1]; \
			  _d->i6[2] = _s1->i6[2] & _s2->i6[2]; \
			  _d->i6[3] = _s1->i6[3] & _s2->i6[3]; \
			} while (0)
#define	IP6_ANDASSIGN(a,m) \
			do { i6addr_t *_d = (i6addr_t *)(a); \
			  i6addr_t *_m = (i6addr_t *)(m); \
			  _d->i6[0] &= _m->i6[0]; \
			  _d->i6[1] &= _m->i6[1]; \
			  _d->i6[2] &= _m->i6[2]; \
			  _d->i6[3] &= _m->i6[3]; \
			} while (0)
#define	IP6_MASKEQ(a,m,b) \
			(((I60(a) & I60(m)) == I60(b)) && \
			 ((I61(a) & I61(m)) == I61(b)) && \
			 ((I62(a) & I62(m)) == I62(b)) && \
			 ((I63(a) & I63(m)) == I63(b)))
#define	IP6_MASKNEQ(a,m,b) \
			(((I60(a) & I60(m)) != I60(b)) || \
			 ((I61(a) & I61(m)) != I61(b)) || \
			 ((I62(a) & I62(m)) != I62(b)) || \
			 ((I63(a) & I63(m)) != I63(b)))
#define	IP6_MERGE(a,b,c) \
			do { i6addr_t *_d, *_s1, *_s2; \
			  _d = (i6addr_t *)(a); \
			  _s1 = (i6addr_t *)(b); \
			  _s2 = (i6addr_t *)(c); \
			  _d->i6[0] |= _s1->i6[0] & ~_s2->i6[0]; \
			  _d->i6[1] |= _s1->i6[1] & ~_s2->i6[1]; \
			  _d->i6[2] |= _s1->i6[2] & ~_s2->i6[2]; \
			  _d->i6[3] |= _s1->i6[3] & ~_s2->i6[3]; \
			} while (0)
#define	IP6_MASK(a,b,c) \
			do { i6addr_t *_d, *_s1, *_s2; \
			  _d = (i6addr_t *)(a); \
			  _s1 = (i6addr_t *)(b); \
			  _s2 = (i6addr_t *)(c); \
			  _d->i6[0] = _s1->i6[0] & ~_s2->i6[0]; \
			  _d->i6[1] = _s1->i6[1] & ~_s2->i6[1]; \
			  _d->i6[2] = _s1->i6[2] & ~_s2->i6[2]; \
			  _d->i6[3] = _s1->i6[3] & ~_s2->i6[3]; \
			} while (0)
#define	IP6_SETONES(a)	\
			do { i6addr_t *_d = (i6addr_t *)(a); \
			  _d->i6[0] = 0xffffffff; \
			  _d->i6[1] = 0xffffffff; \
			  _d->i6[2] = 0xffffffff; \
			  _d->i6[3] = 0xffffffff; \
			} while (0)

typedef	union ipso_u	{
	u_short	ipso_ripso[2];
	u_32_t	ipso_doi;
} ipso_t;

typedef	struct	fr_ip	{
	u_32_t	fi_v:4;		/* IP version */
	u_32_t	fi_xx:4;	/* spare */
	u_32_t	fi_tos:8;	/* IP packet TOS */
	u_32_t	fi_ttl:8;	/* IP packet TTL */
	u_32_t	fi_p:8;		/* IP packet protocol */
	u_32_t	fi_optmsk;	/* bitmask composed from IP options */
	i6addr_t fi_src;	/* source address from packet */
	i6addr_t fi_dst;	/* destination address from packet */
	ipso_t	fi_ipso;	/* IP security options */
	u_32_t	fi_flx;		/* packet flags */
	u_32_t	fi_tcpmsk;	/* TCP options set/reset */
	u_32_t	fi_ports[2];	/* TCP ports */
	u_char	fi_tcpf;	/* TCP flags */
	u_char	fi_sensitivity;
	u_char	fi_xxx[2];	/* pad */
} fr_ip_t;

/*
 * For use in fi_flx
 */
#define	FI_TCPUDP	0x0001	/* TCP/UCP implied comparison*/
#define	FI_OPTIONS	0x0002
#define	FI_FRAG		0x0004
#define	FI_SHORT	0x0008
#define	FI_NATED	0x0010
#define	FI_MULTICAST	0x0020
#define	FI_BROADCAST	0x0040
#define	FI_MBCAST	0x0080
#define	FI_STATE	0x0100
#define	FI_BADNAT	0x0200
#define	FI_BAD		0x0400
#define	FI_OOW		0x0800	/* Out of state window, else match */
#define	FI_ICMPERR	0x1000
#define	FI_FRAGBODY	0x2000
#define	FI_BADSRC	0x4000
#define	FI_LOWTTL	0x8000
#define	FI_CMP		0x5cfe3	/* Not FI_FRAG,FI_NATED,FI_FRAGTAIL */
#define	FI_ICMPCMP	0x0003	/* Flags we can check for ICMP error packets */
#define	FI_WITH		0x5effe	/* Not FI_TCPUDP */
#define	FI_V6EXTHDR	0x10000
#define	FI_COALESCE	0x20000
#define	FI_NEWNAT	0x40000
#define	FI_ICMPQUERY	0x80000
#define	FI_ENCAP	0x100000	/* encap/decap with NAT */
#define	FI_AH		0x200000	/* AH header present */
#define	FI_DOCKSUM	0x10000000	/* Proxy wants L4 recalculation */
#define	FI_NOCKSUM	0x20000000	/* don't do a L4 checksum validation */
#define	FI_NOWILD	0x40000000	/* Do not do wildcard searches */
#define	FI_IGNORE	0x80000000

#define	fi_secmsk	fi_ipso.ipso_ripso[0]
#define	fi_auth		fi_ipso.ipso_ripso[1]
#define	fi_doi		fi_ipso.ipso_doi
#define	fi_saddr	fi_src.in4.s_addr
#define	fi_daddr	fi_dst.in4.s_addr
#define	fi_srcnum	fi_src.iplookupnum
#define	fi_dstnum	fi_dst.iplookupnum
#define	fi_srcname	fi_src.iplookupname
#define	fi_dstname	fi_dst.iplookupname
#define	fi_srctype	fi_src.iplookuptype
#define	fi_dsttype	fi_dst.iplookuptype
#define	fi_srcsubtype	fi_src.iplookupsubtype
#define	fi_dstsubtype	fi_dst.iplookupsubtype
#define	fi_srcptr	fi_src.iplookupptr
#define	fi_dstptr	fi_dst.iplookupptr
#define	fi_srcfunc	fi_src.iplookupfunc
#define	fi_dstfunc	fi_dst.iplookupfunc


/*
 * These are both used by the state and NAT code to indicate that one port or
 * the other should be treated as a wildcard.
 * NOTE: When updating, check bit masks in ip_state.h and update there too.
 */
#define	SI_W_SPORT	0x00000100
#define	SI_W_DPORT	0x00000200
#define	SI_WILDP	(SI_W_SPORT|SI_W_DPORT)
#define	SI_W_SADDR	0x00000400
#define	SI_W_DADDR	0x00000800
#define	SI_WILDA	(SI_W_SADDR|SI_W_DADDR)
#define	SI_NEWFR	0x00001000
#define	SI_CLONE	0x00002000
#define	SI_CLONED	0x00004000
#define	SI_NEWCLONE	0x00008000

typedef	struct {
	u_short	fda_ports[2];
	u_char	fda_tcpf;		/* TCP header flags (SYN, ACK, etc) */
} frdat_t;

typedef enum fr_breasons_e {
	FRB_BLOCKED = 0,
	FRB_LOGFAIL = 1,
	FRB_PPSRATE = 2,
	FRB_JUMBO = 3,
	FRB_MAKEFRIP = 4,
	FRB_STATEADD = 5,
	FRB_UPDATEIPID = 6,
	FRB_LOGFAIL2 = 7,
	FRB_DECAPFRIP = 8,
	FRB_AUTHNEW = 9,
	FRB_AUTHCAPTURE = 10,
	FRB_COALESCE = 11,
	FRB_PULLUP = 12,
	FRB_AUTHFEEDBACK = 13,
	FRB_BADFRAG = 14,
	FRB_NATV4 = 15,
	FRB_NATV6 = 16,
} fr_breason_t;

#define	FRB_MAX_VALUE	16

typedef enum ipf_cksum_e {
	FI_CK_BAD = -1,
	FI_CK_NEEDED = 0,
	FI_CK_SUMOK = 1,
	FI_CK_L4PART = 2,
	FI_CK_L4FULL = 4
} ipf_cksum_t;

typedef	struct	fr_info	{
	void	*fin_main_soft;
	void	*fin_ifp;		/* interface packet is `on' */
	struct	frentry *fin_fr;	/* last matching rule */
	int	fin_out;		/* in or out ? 1 == out, 0 == in */
	fr_ip_t	fin_fi;			/* IP Packet summary */
	frdat_t	fin_dat;		/* TCP/UDP ports, ICMP code/type */
	int	fin_dlen;		/* length of data portion of packet */
	int	fin_plen;
	u_32_t	fin_rule;		/* rule # last matched */
	u_short	fin_hlen;		/* length of IP header in bytes */
	char	fin_group[FR_GROUPLEN];	/* group number, -1 for none */
	void	*fin_dp;		/* start of data past IP header */
	/*
	 * Fields after fin_dp aren't used for compression of log records.
	 * fin_fi contains the IP version (fin_family)
	 * fin_rule isn't included because adding a new rule can change it but
	 * not change fin_fr. fin_rule is the rule number reported.
	 * It isn't necessary to include fin_crc because that is checked
	 * for explicitly, before calling bcmp.
	 */
	u_32_t	fin_crc;		/* Simple calculation for logging */
	int	fin_family;		/* AF_INET, etc. */
	int	fin_icode;		/* ICMP error to return */
	int	fin_mtu;		/* MTU input for ICMP need-frag */
	int	fin_rev;		/* state only: 1 = reverse */
	int	fin_ipoff;		/* # bytes from buffer start to hdr */
	u_32_t	fin_id;			/* IP packet id field */
	u_short	fin_l4hlen;		/* length of L4 header, if known */
	u_short	fin_off;
	int	fin_depth;		/* Group nesting depth */
	int	fin_error;		/* Error code to return */
	ipf_cksum_t	fin_cksum;	/* -1 = bad, 1 = good, 0 = not done */
	fr_breason_t	fin_reason;	/* why auto blocked */
	u_int	fin_pktnum;
	void	*fin_nattag;
	struct frdest	*fin_dif;
	struct frdest	*fin_tif;
	union {
		ip_t	*fip_ip;
#ifdef USE_INET6
		ip6_t	*fip_ip6;
#endif
	} fin_ipu;
	mb_t	**fin_mp;		/* pointer to pointer to mbuf */
	mb_t	*fin_m;			/* pointer to mbuf */
#ifdef	MENTAT
	mb_t	*fin_qfm;		/* pointer to mblk where pkt starts */
	void	*fin_qpi;
	char	fin_ifname[LIFNAMSIZ];
#endif
	void	*fin_fraghdr;		/* pointer to start of ipv6 frag hdr */
} fr_info_t;

#define	fin_ip		fin_ipu.fip_ip
#define	fin_ip6		fin_ipu.fip_ip6
#define	fin_v		fin_fi.fi_v
#define	fin_p		fin_fi.fi_p
#define	fin_flx		fin_fi.fi_flx
#define	fin_optmsk	fin_fi.fi_optmsk
#define	fin_secmsk	fin_fi.fi_secmsk
#define	fin_doi		fin_fi.fi_doi
#define	fin_auth	fin_fi.fi_auth
#define	fin_src		fin_fi.fi_src.in4
#define	fin_saddr	fin_fi.fi_saddr
#define	fin_dst		fin_fi.fi_dst.in4
#define	fin_daddr	fin_fi.fi_daddr
#define	fin_data	fin_fi.fi_ports
#define	fin_sport	fin_fi.fi_ports[0]
#define	fin_dport	fin_fi.fi_ports[1]
#define	fin_tcpf	fin_fi.fi_tcpf
#define	fin_src6	fin_fi.fi_src
#define	fin_dst6	fin_fi.fi_dst
#define	fin_srcip6	fin_fi.fi_src.in6
#define	fin_dstip6	fin_fi.fi_dst.in6

#define	IPF_IN		0
#define	IPF_OUT		1

typedef	struct frentry	*(*ipfunc_t) __P((fr_info_t *, u_32_t *));
typedef	int		(*ipfuncinit_t) __P((struct ipf_main_softc_s *, struct frentry *));

typedef	struct	ipfunc_resolve	{
	char		ipfu_name[32];
	ipfunc_t	ipfu_addr;
	ipfuncinit_t	ipfu_init;
	ipfuncinit_t	ipfu_fini;
} ipfunc_resolve_t;

/*
 * Size for compares on fr_info structures
 */
#define	FI_CSIZE	offsetof(fr_info_t, fin_icode)
#define	FI_LCSIZE	offsetof(fr_info_t, fin_dp)

/*
 * Size for copying cache fr_info structure
 */
#define	FI_COPYSIZE	offsetof(fr_info_t, fin_dp)

/*
 * Structure for holding IPFilter's tag information
 */
#define	IPFTAG_LEN	16
typedef	struct	{
	union	{
		u_32_t	iptu_num[4];
		char	iptu_tag[IPFTAG_LEN];
	} ipt_un;
	int	ipt_not;
} ipftag_t;

#define	ipt_tag	ipt_un.iptu_tag
#define	ipt_num	ipt_un.iptu_num

/*
 * Structure to define address for pool lookups.
 */
typedef	struct	{
	u_char		adf_len;
	sa_family_t	adf_family;
	u_char		adf_xxx[2];
	i6addr_t	adf_addr;
} addrfamily_t;


RBI_LINK(ipf_rb, host_node_s);

typedef	struct	host_node_s {
	RBI_FIELD(ipf_rb)	hn_entry;
	addrfamily_t		hn_addr;
	int			hn_active;
} host_node_t;

typedef RBI_HEAD(ipf_rb, host_node_s) ipf_rb_head_t;

typedef	struct	host_track_s {
	ipf_rb_head_t	ht_root;
	int		ht_max_nodes;
	int		ht_max_per_node;
	int		ht_netmask;
	int		ht_cur_nodes;
} host_track_t;


typedef enum fr_dtypes_e {
	FRD_NORMAL = 0,
	FRD_DSTLIST
} fr_dtypes_t;
/*
 * This structure is used to hold information about the next hop for where
 * to forward a packet.
 */
typedef	struct	frdest	{
	void		*fd_ptr;
	addrfamily_t	fd_addr;
	fr_dtypes_t	fd_type;
	int		fd_name;
	int		fd_local;
} frdest_t;

#define	fd_ip6	fd_addr.adf_addr
#define	fd_ip	fd_ip6.in4


typedef enum fr_ctypes_e {
	FR_NONE = 0,
	FR_EQUAL,
	FR_NEQUAL,
	FR_LESST,
	FR_GREATERT,
	FR_LESSTE,
	FR_GREATERTE,
	FR_OUTRANGE,
	FR_INRANGE,
	FR_INCRANGE
} fr_ctypes_t;

/*
 * This structure holds information about a port comparison.
 */
typedef	struct	frpcmp	{
	fr_ctypes_t	frp_cmp;	/* data for port comparisons */
	u_32_t		frp_port;	/* top port for <> and >< */
	u_32_t		frp_top;	/* top port for <> and >< */
} frpcmp_t;


/*
 * Structure containing all the relevant TCP things that can be checked in
 * a filter rule.
 */
typedef	struct	frtuc	{
	u_char		ftu_tcpfm;	/* tcp flags mask */
	u_char		ftu_tcpf;	/* tcp flags */
	frpcmp_t	ftu_src;
	frpcmp_t	ftu_dst;
} frtuc_t;

#define	ftu_scmp	ftu_src.frp_cmp
#define	ftu_dcmp	ftu_dst.frp_cmp
#define	ftu_sport	ftu_src.frp_port
#define	ftu_dport	ftu_dst.frp_port
#define	ftu_stop	ftu_src.frp_top
#define	ftu_dtop	ftu_dst.frp_top

#define	FR_TCPFMAX	0x3f

typedef enum fr_atypes_e {
	FRI_NONE = -1,	/* For LHS of NAT */
	FRI_NORMAL = 0,	/* Normal address */
	FRI_DYNAMIC,	/* dynamic address */
	FRI_LOOKUP,	/* address is a pool # */
	FRI_RANGE,	/* address/mask is a range */
	FRI_NETWORK,	/* network address from if */
	FRI_BROADCAST,	/* broadcast address from if */
	FRI_PEERADDR,	/* Peer address for P-to-P */
	FRI_NETMASKED,	/* network address with netmask from if */
	FRI_SPLIT,	/* For NAT compatibility */
	FRI_INTERFACE	/* address is based on interface name */
} fr_atypes_t;

/*
 * This structure makes up what is considered to be the IPFilter specific
 * matching components of a filter rule, as opposed to the data structures
 * used to define the result which are in frentry_t and not here.
 */
typedef	struct	fripf	{
	fr_ip_t		fri_ip;
	fr_ip_t		fri_mip;	/* mask structure */

	u_short		fri_icmpm;	/* data for ICMP packets (mask) */
	u_short		fri_icmp;

	frtuc_t		fri_tuc;
	fr_atypes_t	fri_satype;	/* addres type */
	fr_atypes_t	fri_datype;	/* addres type */
	int		fri_sifpidx;	/* doing dynamic addressing */
	int		fri_difpidx;	/* index into fr_ifps[] to use when */
} fripf_t;

#define	fri_dlookup	fri_mip.fi_dst
#define	fri_slookup	fri_mip.fi_src
#define	fri_dstnum	fri_mip.fi_dstnum
#define	fri_srcnum	fri_mip.fi_srcnum
#define	fri_dstname	fri_mip.fi_dstname
#define	fri_srcname	fri_mip.fi_srcname
#define	fri_dstptr	fri_mip.fi_dstptr
#define	fri_srcptr	fri_mip.fi_srcptr


typedef enum fr_rtypes_e {
	FR_T_NONE = 0,
	FR_T_IPF,		/* IPF structures */
	FR_T_BPFOPC,		/* BPF opcode */
	FR_T_CALLFUNC,		/* callout to function in fr_func only */
	FR_T_COMPIPF,			/* compiled C code */
	FR_T_IPFEXPR,			/* IPF expression */
	FR_T_BUILTIN = 0x40000000,	/* rule is in kernel space */
	FR_T_IPF_BUILTIN,
	FR_T_BPFOPC_BUILTIN,
	FR_T_CALLFUNC_BUILTIN,
	FR_T_COMPIPF_BUILTIN,
	FR_T_IPFEXPR_BUILTIN
} fr_rtypes_t;

typedef	struct	frentry	* (* frentfunc_t) __P((fr_info_t *));

typedef	struct	frentry {
	ipfmutex_t	fr_lock;
	struct	frentry	*fr_next;
	struct	frentry	**fr_pnext;
	struct	frgroup	*fr_grp;
	struct	frgroup	*fr_grphead;
	struct	frgroup	*fr_icmpgrp;
	struct	ipscan	*fr_isc;
	struct	frentry	*fr_dnext;	/* 2 fr_die linked list pointers */
	struct	frentry	**fr_pdnext;
	void	*fr_ifas[4];
	void	*fr_ptr;	/* for use with fr_arg */
	int	fr_comment;	/* text comment for rule */
	int	fr_size;	/* size of this structure */
	int	fr_ref;		/* reference count */
	int	fr_statecnt;	/* state count - for limit rules */
	u_32_t	fr_die;		/* only used on loading the rule */
	u_int	fr_cksum;	/* checksum on filter rules for performance */
	/*
	 * The line number from a file is here because we need to be able to
	 * match the rule generated with ``grep rule ipf.conf | ipf -rf -''
	 * with the rule loaded using ``ipf -f ipf.conf'' - thus it can't be
	 * on the other side of fr_func.
	 */
	int	fr_flineno;	/* line number from conf file */
	/*
	 * These are only incremented when a packet  matches this rule and
	 * it is the last match
	 */
	U_QUAD_T	fr_hits;
	U_QUAD_T	fr_bytes;

	/*
	 * For PPS rate limiting
	 * fr_lpu is used to always have the same size for this field,
	 * allocating 64bits for seconds and 32bits for milliseconds.
	 */
	union {
		struct timeval	frp_lastpkt;
		char	frp_bytes[12];
	} fr_lpu;
	int		fr_curpps;

	union	{
		void		*fru_data;
		char		*fru_caddr;
		fripf_t		*fru_ipf;
		frentfunc_t	fru_func;
	} fr_dun;

	/*
	 * Fields after this may not change whilst in the kernel.
	 */
	ipfunc_t fr_func; 	/* call this function */
	int	fr_dsize;
	int	fr_pps;
	fr_rtypes_t	fr_type;
	u_32_t	fr_flags;	/* per-rule flags && options (see below) */
	u_32_t	fr_logtag;	/* user defined log tag # */
	u_32_t	fr_collect;	/* collection number */
	u_int	fr_arg;		/* misc. numeric arg for rule */
	u_int	fr_loglevel;	/* syslog log facility + priority */
	u_char	fr_family;
	u_char	fr_icode;	/* return ICMP code */
	int	fr_group;	/* group to which this rule belongs */
	int	fr_grhead;	/* group # which this rule starts */
	int	fr_ifnames[4];
	int	fr_isctag;
	int	fr_rpc;		/* XID Filtering */ 
	ipftag_t fr_nattag;
	frdest_t fr_tifs[2];	/* "to"/"reply-to" interface */
	frdest_t fr_dif;	/* duplicate packet interface */
	/*
	 * These are all options related to stateful filtering
	 */
	host_track_t	fr_srctrack;
	int	fr_nostatelog;
	int	fr_statemax;	/* max reference count */
	int	fr_icmphead;	/* ICMP group  for state options */
	u_int	fr_age[2];	/* non-TCP state timeouts */
	/*
	 * How big is the name buffer at the end?
	 */
	int	fr_namelen;
	char	fr_names[1];
} frentry_t;

#define	fr_lastpkt	fr_lpu.frp_lastpkt
#define	fr_caddr	fr_dun.fru_caddr
#define	fr_data		fr_dun.fru_data
#define	fr_dfunc	fr_dun.fru_func
#define	fr_ipf		fr_dun.fru_ipf
#define	fr_ip		fr_ipf->fri_ip
#define	fr_mip		fr_ipf->fri_mip
#define	fr_icmpm	fr_ipf->fri_icmpm
#define	fr_icmp		fr_ipf->fri_icmp
#define	fr_tuc		fr_ipf->fri_tuc
#define	fr_satype	fr_ipf->fri_satype
#define	fr_datype	fr_ipf->fri_datype
#define	fr_sifpidx	fr_ipf->fri_sifpidx
#define	fr_difpidx	fr_ipf->fri_difpidx
#define	fr_proto	fr_ip.fi_p
#define	fr_mproto	fr_mip.fi_p
#define	fr_ttl		fr_ip.fi_ttl
#define	fr_mttl		fr_mip.fi_ttl
#define	fr_tos		fr_ip.fi_tos
#define	fr_mtos		fr_mip.fi_tos
#define	fr_tcpfm	fr_tuc.ftu_tcpfm
#define	fr_tcpf		fr_tuc.ftu_tcpf
#define	fr_scmp		fr_tuc.ftu_scmp
#define	fr_dcmp		fr_tuc.ftu_dcmp
#define	fr_dport	fr_tuc.ftu_dport
#define	fr_sport	fr_tuc.ftu_sport
#define	fr_stop		fr_tuc.ftu_stop
#define	fr_dtop		fr_tuc.ftu_dtop
#define	fr_dst		fr_ip.fi_dst.in4
#define	fr_dst6		fr_ip.fi_dst
#define	fr_daddr	fr_ip.fi_dst.in4.s_addr
#define	fr_src		fr_ip.fi_src.in4
#define	fr_src6		fr_ip.fi_src
#define	fr_saddr	fr_ip.fi_src.in4.s_addr
#define	fr_dmsk		fr_mip.fi_dst.in4
#define	fr_dmsk6	fr_mip.fi_dst
#define	fr_dmask	fr_mip.fi_dst.in4.s_addr
#define	fr_smsk		fr_mip.fi_src.in4
#define	fr_smsk6	fr_mip.fi_src
#define	fr_smask	fr_mip.fi_src.in4.s_addr
#define	fr_dstnum	fr_ip.fi_dstnum
#define	fr_srcnum	fr_ip.fi_srcnum
#define	fr_dlookup	fr_ip.fi_dst
#define	fr_slookup	fr_ip.fi_src
#define	fr_dstname	fr_ip.fi_dstname
#define	fr_srcname	fr_ip.fi_srcname
#define	fr_dsttype	fr_ip.fi_dsttype
#define	fr_srctype	fr_ip.fi_srctype
#define	fr_dstsubtype	fr_ip.fi_dstsubtype
#define	fr_srcsubtype	fr_ip.fi_srcsubtype
#define	fr_dstptr	fr_mip.fi_dstptr
#define	fr_srcptr	fr_mip.fi_srcptr
#define	fr_dstfunc	fr_mip.fi_dstfunc
#define	fr_srcfunc	fr_mip.fi_srcfunc
#define	fr_optbits	fr_ip.fi_optmsk
#define	fr_optmask	fr_mip.fi_optmsk
#define	fr_secbits	fr_ip.fi_secmsk
#define	fr_secmask	fr_mip.fi_secmsk
#define	fr_authbits	fr_ip.fi_auth
#define	fr_authmask	fr_mip.fi_auth
#define	fr_doi		fr_ip.fi_doi
#define	fr_doimask	fr_mip.fi_doi
#define	fr_flx		fr_ip.fi_flx
#define	fr_mflx		fr_mip.fi_flx
#define	fr_ifa		fr_ifas[0]
#define	fr_oifa		fr_ifas[2]
#define	fr_tif		fr_tifs[0]
#define	fr_rif		fr_tifs[1]

#define	FR_NOLOGTAG	0

#define	FR_CMPSIZ	(sizeof(struct frentry) - \
			 offsetof(struct frentry, fr_func))
#define	FR_NAME(_f, _n)	(_f)->fr_names + (_f)->_n


/*
 * fr_flags
 */
#define	FR_BLOCK	0x00001	/* do not allow packet to pass */
#define	FR_PASS		0x00002	/* allow packet to pass */
#define	FR_AUTH		0x00003	/* use authentication */
#define	FR_PREAUTH	0x00004	/* require preauthentication */
#define	FR_ACCOUNT	0x00005	/* Accounting rule */
#define	FR_SKIP		0x00006	/* skip rule */
#define	FR_DECAPSULATE	0x00008	/* decapsulate rule */
#define	FR_CALL		0x00009	/* call rule */
#define	FR_CMDMASK	0x0000f
#define	FR_LOG		0x00010	/* Log */
#define	FR_LOGB		0x00011	/* Log-fail */
#define	FR_LOGP		0x00012	/* Log-pass */
#define	FR_LOGMASK	(FR_LOG|FR_CMDMASK)
#define	FR_CALLNOW	0x00020	/* call another function (fr_func) if matches */
#define	FR_NOTSRCIP	0x00040
#define	FR_NOTDSTIP	0x00080
#define	FR_QUICK	0x00100	/* match & stop processing list */
#define	FR_KEEPFRAG	0x00200	/* keep fragment information */
#define	FR_KEEPSTATE	0x00400	/* keep `connection' state information */
#define	FR_FASTROUTE	0x00800	/* bypass normal routing */
#define	FR_RETRST	0x01000	/* Return TCP RST packet - reset connection */
#define	FR_RETICMP	0x02000	/* Return ICMP unreachable packet */
#define	FR_FAKEICMP	0x03000	/* Return ICMP unreachable with fake source */
#define	FR_OUTQUE	0x04000	/* outgoing packets */
#define	FR_INQUE	0x08000	/* ingoing packets */
#define	FR_LOGBODY	0x10000	/* Log the body */
#define	FR_LOGFIRST	0x20000	/* Log the first byte if state held */
#define	FR_LOGORBLOCK	0x40000	/* block the packet if it can't be logged */
#define	FR_STLOOSE	0x80000	/* loose state checking */
#define	FR_FRSTRICT	0x100000	/* strict frag. cache */
#define	FR_STSTRICT	0x200000	/* strict keep state */
#define	FR_NEWISN	0x400000	/* new ISN for outgoing TCP */
#define	FR_NOICMPERR	0x800000	/* do not match ICMP errors in state */
#define	FR_STATESYNC	0x1000000	/* synchronize state to slave */
#define	FR_COPIED	0x2000000	/* copied from user space */
#define	FR_INACTIVE	0x4000000	/* only used when flush'ing rules */
#define	FR_NOMATCH	0x8000000	/* no match occured */
		/*	0x10000000 	FF_LOGPASS */
		/*	0x20000000 	FF_LOGBLOCK */
		/*	0x40000000 	FF_LOGNOMATCH */
		/*	0x80000000 	FF_BLOCKNONIP */

#define	FR_RETMASK	(FR_RETICMP|FR_RETRST|FR_FAKEICMP)
#define	FR_ISBLOCK(x)	(((x) & FR_CMDMASK) == FR_BLOCK)
#define	FR_ISPASS(x)	(((x) & FR_CMDMASK) == FR_PASS)
#define	FR_ISAUTH(x)	(((x) & FR_CMDMASK) == FR_AUTH)
#define	FR_ISPREAUTH(x)	(((x) & FR_CMDMASK) == FR_PREAUTH)
#define	FR_ISACCOUNT(x)	(((x) & FR_CMDMASK) == FR_ACCOUNT)
#define	FR_ISSKIP(x)	(((x) & FR_CMDMASK) == FR_SKIP)
#define	FR_ISDECAPS(x)	(((x) & FR_CMDMASK) == FR_DECAPSULATE)
#define	FR_ISNOMATCH(x)	((x) & FR_NOMATCH)
#define	FR_INOUT	(FR_INQUE|FR_OUTQUE)

/*
 * recognized flags for SIOCGETFF and SIOCSETFF, and get put in fr_flags
 */
#define	FF_LOGPASS	0x10000000
#define	FF_LOGBLOCK	0x20000000
#define	FF_LOGNOMATCH	0x40000000
#define	FF_LOGGING	(FF_LOGPASS|FF_LOGBLOCK|FF_LOGNOMATCH)
#define	FF_BLOCKNONIP	0x80000000	/* Solaris2 Only */


/*
 * Structure that passes information on what/how to flush to the kernel.
 */
typedef	struct	ipfflush	{
	int		ipflu_how;
	int		ipflu_arg;
} ipfflush_t;


/*
 *
 */
typedef	struct	ipfgetctl	{
	u_int		ipfg_min;	/* min value */
	u_int		ipfg_current;	/* current value */
	u_int		ipfg_max;	/* max value */
	u_int		ipfg_default;	/* default value */
	u_int		ipfg_steps;	/* value increments */
	char		ipfg_name[40];	/* tag name for this control */
} ipfgetctl_t;

typedef	struct	ipfsetctl	{
	int	ipfs_which;	/* 0 = min 1 = current 2 = max 3 = default */
	u_int	ipfs_value;	/* min value */
	char	ipfs_name[40];	/* tag name for this control */
} ipfsetctl_t;


/*
 * Some of the statistics below are in their own counters, but most are kept
 * in this single structure so that they can all easily be collected and
 * copied back as required.
 */
typedef	struct	ipf_statistics {
	u_long	fr_icmp_coalesce;
	u_long	fr_tcp_frag;
	u_long	fr_tcp_pullup;
	u_long	fr_tcp_short;
	u_long	fr_tcp_small;
	u_long	fr_tcp_bad_flags;
	u_long	fr_udp_pullup;
	u_long	fr_ip_freed;
	u_long	fr_v6_ah_bad;
	u_long	fr_v6_bad;
	u_long	fr_v6_badfrag;
	u_long	fr_v6_dst_bad;
	u_long	fr_v6_esp_pullup;
	u_long	fr_v6_ext_short;
	u_long	fr_v6_ext_pullup;
	u_long	fr_v6_ext_hlen;
	u_long	fr_v6_frag_bad;
	u_long	fr_v6_frag_pullup;
	u_long	fr_v6_frag_size;
	u_long	fr_v6_gre_pullup;
	u_long	fr_v6_icmp6_pullup;
	u_long	fr_v6_rh_bad;
	u_long	fr_v6_badttl;	/* TTL in packet doesn't reach minimum */
	u_long	fr_v4_ah_bad;
	u_long	fr_v4_ah_pullup;
	u_long	fr_v4_esp_pullup;
	u_long	fr_v4_cipso_bad;
	u_long	fr_v4_cipso_tlen;
	u_long	fr_v4_gre_frag;
	u_long	fr_v4_gre_pullup;
	u_long	fr_v4_icmp_frag;
	u_long	fr_v4_icmp_pullup;
	u_long	fr_v4_badttl;	/* TTL in packet doesn't reach minimum */
	u_long	fr_v4_badsrc;	/* source received doesn't match route */
	u_long	fr_l4_badcksum;	/* layer 4 header checksum failure */
	u_long	fr_badcoalesces;
	u_long	fr_pass;	/* packets allowed */
	u_long	fr_block;	/* packets denied */
	u_long	fr_nom;		/* packets which don't match any rule */
	u_long	fr_short;	/* packets which are short */
	u_long	fr_ppkl;	/* packets allowed and logged */
	u_long	fr_bpkl;	/* packets denied and logged */
	u_long	fr_npkl;	/* packets unmatched and logged */
	u_long	fr_ret;		/* packets for which a return is sent */
	u_long	fr_acct;	/* packets for which counting was performed */
	u_long	fr_bnfr;	/* bad attempts to allocate fragment state */
	u_long	fr_nfr;		/* new fragment state kept */
	u_long	fr_cfr;		/* add new fragment state but complete pkt */
	u_long	fr_bads;	/* bad attempts to allocate packet state */
	u_long	fr_ads;		/* new packet state kept */
	u_long	fr_chit;	/* cached hit */
	u_long	fr_cmiss;	/* cached miss */
	u_long	fr_tcpbad;	/* TCP checksum check failures */
	u_long	fr_pull[2];	/* good and bad pullup attempts */
	u_long	fr_bad;		/* bad IP packets to the filter */
	u_long	fr_ipv6;	/* IPv6 packets in/out */
	u_long	fr_ppshit;	/* dropped because of pps ceiling */
	u_long	fr_ipud;	/* IP id update failures */
	u_long	fr_blocked[FRB_MAX_VALUE + 1];
} ipf_statistics_t;

/*
 * Log structure.  Each packet header logged is prepended by one of these.
 * Following this in the log records read from the device will be an ipflog
 * structure which is then followed by any packet data.
 */
typedef	struct	iplog	{
	u_32_t		ipl_magic;
	u_int		ipl_count;
	u_32_t		ipl_seqnum;
	struct	timeval	ipl_time;
	size_t		ipl_dsize;
	struct	iplog	*ipl_next;
} iplog_t;

#define	ipl_sec		ipl_time.tv_sec
#define	ipl_usec	ipl_time.tv_usec

#define IPL_MAGIC	0x49504c4d	/* 'IPLM' */
#define IPL_MAGIC_NAT	0x49504c4e	/* 'IPLN' */
#define IPL_MAGIC_STATE	0x49504c53	/* 'IPLS' */
#define	IPLOG_SIZE	sizeof(iplog_t)

typedef	struct	ipflog	{
	u_int		fl_unit;
	u_32_t		fl_rule;
	u_32_t		fl_flags;
	u_32_t		fl_lflags;
	u_32_t		fl_logtag;
	ipftag_t	fl_nattag;
	u_short		fl_plen;	/* extra data after hlen */
	u_short		fl_loglevel;	/* syslog log level */
	char		fl_group[FR_GROUPLEN];
	u_char		fl_hlen;	/* length of IP headers saved */
	u_char		fl_dir;
	u_char		fl_breason;	/* from fin_reason */
	u_char		fl_family;	/* address family of packet logged */
	char		fl_ifname[LIFNAMSIZ];
} ipflog_t;

#ifndef	IPF_LOGGING
# define	IPF_LOGGING	0
#endif
#ifndef	IPF_DEFAULT_PASS
# define	IPF_DEFAULT_PASS	FR_PASS
#endif

#define	DEFAULT_IPFLOGSIZE	32768
#ifndef	IPFILTER_LOGSIZE
# define	IPFILTER_LOGSIZE	DEFAULT_IPFLOGSIZE
#else
# if IPFILTER_LOGSIZE < 8192
#  error IPFILTER_LOGSIZE too small.  Must be >= 8192
# endif
#endif

#define	IPF_OPTCOPY	0x07ff00	/* bit mask of copied options */

/*
 * Device filenames for reading log information.  Use ipf on Solaris2 because
 * ipl is already a name used by something else.
 */
#ifndef	IPL_NAME
# if	SOLARIS
#  define	IPL_NAME	"/dev/ipf"
# else
#  define	IPL_NAME	"/dev/ipl"
# endif
#endif
/*
 * Pathnames for various IP Filter control devices.  Used by LKM
 * and userland, so defined here.
 */
#define	IPNAT_NAME	"/dev/ipnat"
#define	IPSTATE_NAME	"/dev/ipstate"
#define	IPAUTH_NAME	"/dev/ipauth"
#define	IPSYNC_NAME	"/dev/ipsync"
#define	IPSCAN_NAME	"/dev/ipscan"
#define	IPLOOKUP_NAME	"/dev/iplookup"

#define	IPL_LOGIPF	0	/* Minor device #'s for accessing logs */
#define	IPL_LOGNAT	1
#define	IPL_LOGSTATE	2
#define	IPL_LOGAUTH	3
#define	IPL_LOGSYNC	4
#define	IPL_LOGSCAN	5
#define	IPL_LOGLOOKUP	6
#define	IPL_LOGCOUNT	7
#define	IPL_LOGMAX	7
#define	IPL_LOGSIZE	IPL_LOGMAX + 1
#define	IPL_LOGALL	-1
#define	IPL_LOGNONE	-2

/*
 * For SIOCGETFS
 */
typedef	struct	friostat	{
	ipf_statistics_t f_st[2];
	frentry_t	*f_ipf[2][2];
	frentry_t	*f_acct[2][2];
	frentry_t	*f_auth;
	struct frgroup	*f_groups[IPL_LOGSIZE][2];
	u_long		f_froute[2];
	u_long		f_log_ok;
	u_long		f_log_fail;
	u_long		f_rb_no_mem;
	u_long		f_rb_node_max;
	u_32_t		f_ticks;
	int		f_locks[IPL_LOGSIZE];
	int		f_defpass;	/* default pass - from fr_pass */
	int		f_active;	/* 1 or 0 - active rule set */
	int		f_running;	/* 1 if running, else 0 */
	int		f_logging;	/* 1 if enabled, else 0 */
	int		f_features;
	char		f_version[32];	/* version string */
} friostat_t;

#define	f_fin		f_ipf[0]
#define	f_fout		f_ipf[1]
#define	f_acctin	f_acct[0]
#define	f_acctout	f_acct[1]

#define	IPF_FEAT_LKM		0x001
#define	IPF_FEAT_LOG		0x002
#define	IPF_FEAT_LOOKUP		0x004
#define	IPF_FEAT_BPF		0x008
#define	IPF_FEAT_COMPILED	0x010
#define	IPF_FEAT_CKSUM		0x020
#define	IPF_FEAT_SYNC		0x040
#define	IPF_FEAT_SCAN		0x080
#define	IPF_FEAT_IPV6		0x100

typedef struct	optlist {
	u_short ol_val;
	int	ol_bit;
} optlist_t;


/*
 * Group list structure.
 */
typedef	struct frgroup {
	struct frgroup	*fg_next;
	struct frentry	*fg_head;
	struct frentry	*fg_start;
	struct frgroup	**fg_set;
	u_32_t		fg_flags;
	int		fg_ref;
	char		fg_name[FR_GROUPLEN];
} frgroup_t;

#define	FG_NAME(g)	(*(g)->fg_name == '\0' ? "" : (g)->fg_name)


/*
 * Used by state and NAT tables
 */
typedef struct icmpinfo {
	u_short		ici_id;
	u_short		ici_seq;
	u_char		ici_type;
} icmpinfo_t;

typedef struct udpinfo {
	u_short		us_sport;
	u_short		us_dport;
} udpinfo_t;


typedef	struct	tcpdata	{
	u_32_t		td_end;
	u_32_t		td_maxend;
	u_32_t		td_maxwin;
	u_32_t		td_winscale;
	u_32_t		td_maxseg;
	int		td_winflags;
} tcpdata_t;

#define	TCP_WSCALE_MAX		14

#define	TCP_WSCALE_SEEN		0x00000001
#define	TCP_WSCALE_FIRST	0x00000002
#define	TCP_SACK_PERMIT		0x00000004


typedef	struct tcpinfo {
	u_32_t		ts_sport;
	u_32_t		ts_dport;
	tcpdata_t	ts_data[2];
} tcpinfo_t;


/*
 * Structures to define a GRE header as seen in a packet.
 */
struct	grebits	{
#if defined(sparc)
	u_32_t		grb_ver:3;
	u_32_t		grb_flags:3;
	u_32_t		grb_A:1;
	u_32_t		grb_recur:1;
	u_32_t		grb_s:1;
	u_32_t		grb_S:1;
	u_32_t		grb_K:1;
	u_32_t		grb_R:1;
	u_32_t		grb_C:1;
#else
	u_32_t		grb_C:1;
	u_32_t		grb_R:1;
	u_32_t		grb_K:1;
	u_32_t		grb_S:1;
	u_32_t		grb_s:1;
	u_32_t		grb_recur:1;
	u_32_t		grb_A:1;
	u_32_t		grb_flags:3;
	u_32_t		grb_ver:3;
#endif
	u_short		grb_ptype;
};

typedef	struct	grehdr	{
	union	{
		struct	grebits	gru_bits;
		u_short	gru_flags;
	} gr_un;
	u_short		gr_len;
	u_short		gr_call;
} grehdr_t;

#define	gr_flags	gr_un.gru_flags
#define	gr_bits		gr_un.gru_bits
#define	gr_ptype	gr_bits.grb_ptype
#define	gr_C		gr_bits.grb_C
#define	gr_R		gr_bits.grb_R
#define	gr_K		gr_bits.grb_K
#define	gr_S		gr_bits.grb_S
#define	gr_s		gr_bits.grb_s
#define	gr_recur	gr_bits.grb_recur
#define	gr_A		gr_bits.grb_A
#define	gr_ver		gr_bits.grb_ver

/*
 * GRE information tracked by "keep state"
 */
typedef	struct	greinfo	{
	u_short		gs_call[2];
	u_short		gs_flags;
	u_short		gs_ptype;
} greinfo_t;

#define	GRE_REV(x)	((ntohs(x) >> 13) & 7)


/*
 * Format of an Authentication header
 */
typedef	struct	authhdr	{
	u_char		ah_next;
	u_char		ah_plen;
	u_short		ah_reserved;
	u_32_t		ah_spi;
	u_32_t		ah_seq;
	/* Following the sequence number field is 0 or more bytes of */
	/* authentication data, as specified by ah_plen - RFC 2402.  */
} authhdr_t;


/*
 * Timeout tail queue list member
 */
typedef	struct	ipftqent	{
	struct ipftqent **tqe_pnext;
	struct ipftqent *tqe_next;
	struct	ipftq	*tqe_ifq;
	void		*tqe_parent;	/* pointer back to NAT/state struct */
	u_32_t		tqe_die;	/* when this entriy is to die */
	u_32_t		tqe_touched;
	int		tqe_flags;
	int		tqe_state[2];	/* current state of this entry */
} ipftqent_t;

#define	TQE_RULEBASED	0x00000001
#define	TQE_DELETE	0x00000002


/*
 * Timeout tail queue head for IPFilter
 */
typedef struct  ipftq   {
	ipfmutex_t	ifq_lock;
	u_int		ifq_ttl;
	ipftqent_t	*ifq_head;
	ipftqent_t	**ifq_tail;
	struct ipftq	*ifq_next;
	struct ipftq	**ifq_pnext;
	int		ifq_ref;
	u_int		ifq_flags;
} ipftq_t;

#define	IFQF_USER	0x01		/* User defined aging */
#define	IFQF_DELETE	0x02		/* Marked for deletion */
#define	IFQF_PROXY	0x04		/* Timeout queue in use by a proxy */

#define	IPFTQ_INIT(x,y,z)	do {			\
					(x)->ifq_ttl = (y);	\
					(x)->ifq_head = NULL;	\
					(x)->ifq_ref = 1;	\
					(x)->ifq_tail = &(x)->ifq_head; \
					MUTEX_INIT(&(x)->ifq_lock, (z)); \
				} while (0)

#define	IPF_HZ_MULT	1
#define	IPF_HZ_DIVIDE	2		/* How many times a second ipfilter */
					/* checks its timeout queues.       */
#define	IPF_TTLVAL(x)	(((x) / IPF_HZ_MULT) * IPF_HZ_DIVIDE)

typedef	int	(*ipftq_delete_fn_t)(struct ipf_main_softc_s *, void *);


/*
 * Object structure description.  For passing through in ioctls.
 */
typedef	struct	ipfobj	{
	u_32_t		ipfo_rev;	/* IPFilter version number */
	u_32_t		ipfo_size;	/* size of object at ipfo_ptr */
	void		*ipfo_ptr;	/* pointer to object */
	int		ipfo_type;	/* type of object being pointed to */
	int		ipfo_offset;	/* bytes from ipfo_ptr where to start */
	int		ipfo_retval;	/* return value */
	u_char		ipfo_xxxpad[28];	/* reserved for future use */
} ipfobj_t;

#define	IPFOBJ_FRENTRY		0	/* struct frentry */
#define	IPFOBJ_IPFSTAT		1	/* struct friostat */
#define	IPFOBJ_IPFINFO		2	/* struct fr_info */
#define	IPFOBJ_AUTHSTAT		3	/* struct fr_authstat */
#define	IPFOBJ_FRAGSTAT		4	/* struct ipfrstat */
#define	IPFOBJ_IPNAT		5	/* struct ipnat */
#define	IPFOBJ_NATSTAT		6	/* struct natstat */
#define	IPFOBJ_STATESAVE	7	/* struct ipstate_save */
#define	IPFOBJ_NATSAVE		8	/* struct nat_save */
#define	IPFOBJ_NATLOOKUP	9	/* struct natlookup */
#define	IPFOBJ_IPSTATE		10	/* struct ipstate */
#define	IPFOBJ_STATESTAT	11	/* struct ips_stat */
#define	IPFOBJ_FRAUTH		12	/* struct frauth */
#define	IPFOBJ_TUNEABLE		13	/* struct ipftune */
#define	IPFOBJ_NAT		14	/* struct nat */
#define	IPFOBJ_IPFITER		15	/* struct ipfruleiter */
#define	IPFOBJ_GENITER		16	/* struct ipfgeniter */
#define	IPFOBJ_GTABLE		17	/* struct ipftable */
#define	IPFOBJ_LOOKUPITER	18	/* struct ipflookupiter */
#define	IPFOBJ_STATETQTAB	19	/* struct ipftq * NSTATES */
#define	IPFOBJ_IPFEXPR		20
#define	IPFOBJ_PROXYCTL		21	/* strct ap_ctl */
#define	IPFOBJ_FRIPF		22	/* structfripf */
#define	IPFOBJ_COUNT		23	/* How many #defines are above this? */


typedef	union	ipftunevalptr	{
	void		*ipftp_void;
	u_long		*ipftp_long;
	u_int		*ipftp_int;
	u_short		*ipftp_short;
	u_char		*ipftp_char;
	u_long		ipftp_offset;
} ipftunevalptr_t;

typedef	union	ipftuneval	{
	u_long		ipftu_long;
	u_int		ipftu_int;
	u_short		ipftu_short;
	u_char		ipftu_char;
} ipftuneval_t;

struct ipftuneable;
typedef	int (* ipftunefunc_t) __P((struct ipf_main_softc_s *, struct ipftuneable *, ipftuneval_t *));

typedef	struct	ipftuneable	{
	ipftunevalptr_t	ipft_una;
	const char	*ipft_name;
	u_long		ipft_min;
	u_long		ipft_max;
	int		ipft_sz;
	int		ipft_flags;
	struct ipftuneable *ipft_next;
	ipftunefunc_t	ipft_func;
} ipftuneable_t;

#define	ipft_addr	ipft_una.ipftp_void
#define	ipft_plong	ipft_una.ipftp_long
#define	ipft_pint	ipft_una.ipftp_int
#define	ipft_pshort	ipft_una.ipftp_short
#define	ipft_pchar	ipft_una.ipftp_char

#define	IPFT_RDONLY	1	/* read-only */
#define	IPFT_WRDISABLED	2	/* write when disabled only */

typedef	struct	ipftune	{
	void    	*ipft_cookie;
	ipftuneval_t	ipft_un;
	u_long  	ipft_min;
	u_long  	ipft_max;
	int		ipft_sz;
	int		ipft_flags;
	char		ipft_name[80];
} ipftune_t;

#define	ipft_vlong	ipft_un.ipftu_long
#define	ipft_vint	ipft_un.ipftu_int
#define	ipft_vshort	ipft_un.ipftu_short
#define	ipft_vchar	ipft_un.ipftu_char

/*
 * Hash table header
 */
#define	IPFHASH(x,y)	typedef struct { 			\
				ipfrwlock_t	ipfh_lock;	\
				struct	x	*ipfh_head;	\
				} y

/*
** HPUX Port
*/

#if !defined(CDEV_MAJOR) && defined (__FreeBSD_version) && \
    (__FreeBSD_version >= 220000)
# define	CDEV_MAJOR	79
#endif

#ifdef _KERNEL
# define	FR_VERBOSE(verb_pr)
# define	FR_DEBUG(verb_pr)
#else
extern	void	ipfkdebug __P((char *, ...));
extern	void	ipfkverbose __P((char *, ...));
# define	FR_VERBOSE(verb_pr)	ipfkverbose verb_pr
# define	FR_DEBUG(verb_pr)	ipfkdebug verb_pr
#endif

/*
 *
 */
typedef	struct	ipfruleiter {
	int		iri_inout;
	char		iri_group[FR_GROUPLEN];
	int		iri_active;
	int		iri_nrules;
	int		iri_v;		/* No longer used (compatibility) */
	frentry_t	*iri_rule;
} ipfruleiter_t;

/*
 * Values for iri_inout
 */
#define	F_IN	0
#define	F_OUT	1
#define	F_ACIN	2
#define	F_ACOUT	3


typedef	struct	ipfgeniter {
	int	igi_type;
	int	igi_nitems;
	void	*igi_data;
} ipfgeniter_t;

#define	IPFGENITER_IPF		0
#define	IPFGENITER_NAT		1
#define	IPFGENITER_IPNAT	2
#define	IPFGENITER_FRAG		3
#define	IPFGENITER_AUTH		4
#define	IPFGENITER_STATE	5
#define	IPFGENITER_NATFRAG	6
#define	IPFGENITER_HOSTMAP	7
#define	IPFGENITER_LOOKUP	8

typedef	struct	ipftable {
	int	ita_type;
	void	*ita_table;
} ipftable_t;

#define	IPFTABLE_BUCKETS	1
#define	IPFTABLE_BUCKETS_NATIN	2
#define	IPFTABLE_BUCKETS_NATOUT	3


typedef struct ipf_v4_masktab_s {
	u_32_t	imt4_active[33];
	int	imt4_masks[33];
	int	imt4_max;
} ipf_v4_masktab_t;

typedef struct ipf_v6_masktab_s {
	i6addr_t	imt6_active[129];
	int		imt6_masks[129];
	int		imt6_max;
} ipf_v6_masktab_t;


/*
 *
 */
typedef struct ipftoken {
	struct ipftoken	*ipt_next;
	struct ipftoken	**ipt_pnext;
	void		*ipt_ctx;
	void		*ipt_data;
	u_long		ipt_die;
	int		ipt_type;
	int		ipt_uid;
	int		ipt_subtype;
	int		ipt_ref;
	int		ipt_complete;
} ipftoken_t;


/*
 *
 */
typedef struct ipfexp {
	int		ipfe_cmd;
	int		ipfe_not;
	int		ipfe_narg;
	int		ipfe_size;
	int		ipfe_arg0[1];
} ipfexp_t;

/*
 * Currently support commands (ipfe_cmd)
 * 32bits is split up follows:
 * aabbcccc
 * aa = 0 = packet matching, 1 = meta data matching
 * bb = IP protocol number
 * cccc = command
 */
#define	IPF_EXP_IP_PR		0x00000001
#define	IPF_EXP_IP_ADDR		0x00000002
#define	IPF_EXP_IP_SRCADDR	0x00000003
#define	IPF_EXP_IP_DSTADDR	0x00000004
#define	IPF_EXP_IP6_ADDR	0x00000005
#define	IPF_EXP_IP6_SRCADDR	0x00000006
#define	IPF_EXP_IP6_DSTADDR	0x00000007
#define	IPF_EXP_TCP_FLAGS	0x00060001
#define	IPF_EXP_TCP_PORT	0x00060002
#define	IPF_EXP_TCP_SPORT	0x00060003
#define	IPF_EXP_TCP_DPORT	0x00060004
#define	IPF_EXP_UDP_PORT	0x00110002
#define	IPF_EXP_UDP_SPORT	0x00110003
#define	IPF_EXP_UDP_DPORT	0x00110004
#define	IPF_EXP_IDLE_GT		0x01000001
#define	IPF_EXP_TCP_STATE	0x01060002
#define	IPF_EXP_END		0xffffffff

#define	ONE_DAY			IPF_TTLVAL(1 * 86400)   /* 1 day */
#define	FIVE_DAYS		(5 * ONE_DAY)

typedef struct ipf_main_softc_s {
	struct ipf_main_softc_s *ipf_next;
	ipfmutex_t	ipf_rw;
	ipfmutex_t      ipf_timeoutlock;
	ipfrwlock_t     ipf_mutex;
	ipfrwlock_t	ipf_frag;
	ipfrwlock_t	ipf_global;
	ipfrwlock_t	ipf_tokens;
	ipfrwlock_t	ipf_state;
	ipfrwlock_t	ipf_nat;
	ipfrwlock_t	ipf_natfrag;
	ipfrwlock_t	ipf_poolrw;
	int		ipf_dynamic_softc;
	int		ipf_refcnt;
	int		ipf_running;
	int		ipf_flags;
	int		ipf_active;
	int		ipf_control_forwarding;
	int		ipf_update_ipid;
	int		ipf_chksrc;	/* causes a system crash if enabled */
	int		ipf_pass;
	int		ipf_minttl;
	int		ipf_icmpminfragmtu;
	int		ipf_interror;	/* Should be in a struct that is per  */
					/* thread or process. Does not belong */
					/* here but there's a lot more work   */
					/* in doing that properly. For now,   */
					/* it is squatting. */
	u_int		ipf_tcpidletimeout;
	u_int		ipf_tcpclosewait;
	u_int		ipf_tcplastack;
	u_int		ipf_tcptimewait;
	u_int		ipf_tcptimeout;
	u_int		ipf_tcpsynsent;
	u_int		ipf_tcpsynrecv;
	u_int		ipf_tcpclosed;
	u_int		ipf_tcphalfclosed;
	u_int		ipf_udptimeout;
	u_int		ipf_udpacktimeout;
	u_int		ipf_icmptimeout;
	u_int		ipf_icmpacktimeout;
	u_int		ipf_iptimeout;
	u_long		ipf_ticks;
	u_long		ipf_userifqs;
	u_long		ipf_rb_no_mem;
	u_long		ipf_rb_node_max;
	u_long		ipf_frouteok[2];
	ipftuneable_t	*ipf_tuners;
	void		*ipf_frag_soft;
	void		*ipf_nat_soft;
	void		*ipf_state_soft;
	void		*ipf_auth_soft;
	void		*ipf_proxy_soft;
	void		*ipf_sync_soft;
	void		*ipf_lookup_soft;
	void		*ipf_log_soft;
	struct frgroup	*ipf_groups[IPL_LOGSIZE][2];
	frentry_t	*ipf_rules[2][2];
	frentry_t	*ipf_acct[2][2];
	frentry_t	*ipf_rule_explist[2];
	ipftoken_t	*ipf_token_head;
	ipftoken_t	**ipf_token_tail;
#if defined(__FreeBSD_version) && defined(_KERNEL)
	struct callout ipf_slow_ch;
#endif
#if NETBSD_GE_REV(104040000)
	struct callout	ipf_slow_ch;
#endif
#if SOLARIS
	timeout_id_t	ipf_slow_ch;
#endif
#if defined(_KERNEL)
# if SOLARIS
	struct pollhead	ipf_poll_head[IPL_LOGSIZE];
	void		*ipf_dip;
#  if defined(INSTANCES)
	int		ipf_get_loopback;
	u_long		ipf_idnum;
	net_handle_t	ipf_nd_v4;
	net_handle_t	ipf_nd_v6;
	hook_t		*ipf_hk_v4_in;
	hook_t		*ipf_hk_v4_out;
	hook_t		*ipf_hk_v4_nic;
	hook_t		*ipf_hk_v6_in;
	hook_t		*ipf_hk_v6_out;
	hook_t		*ipf_hk_v6_nic;
	hook_t		*ipf_hk_loop_v4_in;
	hook_t		*ipf_hk_loop_v4_out;
	hook_t		*ipf_hk_loop_v6_in;
	hook_t		*ipf_hk_loop_v6_out;
#  endif
# else
	struct selinfo	ipf_selwait[IPL_LOGSIZE];
# endif
#endif
	void		*ipf_slow;
	ipf_statistics_t ipf_stats[2];
	u_char		ipf_iss_secret[32];
	u_short		ipf_ip_id;
} ipf_main_softc_t;

#define	IPFERROR(_e)	do { softc->ipf_interror = (_e); \
			     DT1(user_error, int, _e); \
			} while (0)

#ifndef	_KERNEL
extern	int	ipf_check __P((void *, struct ip *, int, void *, int, mb_t **));
extern	struct	ifnet *get_unit __P((char *, int));
extern	char	*get_ifname __P((struct ifnet *));
extern	int	ipfioctl __P((ipf_main_softc_t *, int, ioctlcmd_t,
			      caddr_t, int));
extern	void	m_freem __P((mb_t *));
extern	size_t	msgdsize __P((mb_t *));
extern	int	bcopywrap __P((void *, void *, size_t));
extern	void	ip_fillid(struct ip *);
#else /* #ifndef _KERNEL */
# if defined(__NetBSD__) && defined(PFIL_HOOKS)
extern	void	ipfilterattach __P((int));
# endif
extern	int	ipl_enable __P((void));
extern	int	ipl_disable __P((void));
# ifdef MENTAT
/* XXX MENTAT is always defined for Solaris */
extern	int	ipf_check __P((void *, struct ip *, int, void *, int, void *,
			       mblk_t **));
#  if SOLARIS
extern	void	ipf_prependmbt(fr_info_t *, mblk_t *);
extern	int	ipfioctl __P((dev_t, int, intptr_t, int, cred_t *, int *));
#  endif
extern	int	ipf_qout __P((queue_t *, mblk_t *));
# else /* MENTAT */
/* XXX MENTAT is never defined for FreeBSD & NetBSD */
extern	int	ipf_check __P((void *, struct ip *, int, void *, int, mb_t **));
extern	int	(*fr_checkp) __P((ip_t *, int, void *, int, mb_t **));
extern	size_t	mbufchainlen __P((mb_t *));
#   ifdef	IPFILTER_LKM
extern	int	ipf_identify __P((char *));
#   endif
#     if defined(__FreeBSD_version)
extern	int	ipfioctl __P((struct cdev*, u_long, caddr_t, int, struct thread *));
#     elif defined(__NetBSD__)
extern	int	ipfioctl __P((dev_t, u_long, void *, int, struct lwp *));
#     endif
# endif /* MENTAT */

# if defined(__FreeBSD_version)
extern	int	ipf_pfil_hook __P((void));
extern	int	ipf_pfil_unhook __P((void));
extern	void	ipf_event_reg __P((void));
extern	void	ipf_event_dereg __P((void));
# endif

# if defined(INSTANCES)
extern	ipf_main_softc_t	*ipf_find_softc __P((u_long));
extern	int	ipf_set_loopback __P((ipf_main_softc_t *, ipftuneable_t *,
				      ipftuneval_t *));
# endif

#endif /* #ifndef _KERNEL */

extern	char	*memstr __P((const char *, char *, size_t, size_t));
extern	int	count4bits __P((u_32_t));
#ifdef USE_INET6
extern	int	count6bits __P((u_32_t *));
#endif
extern	int	frrequest __P((ipf_main_softc_t *, int, ioctlcmd_t, caddr_t,
			       int, int));
extern	char	*getifname __P((struct ifnet *));
extern	int	ipfattach __P((ipf_main_softc_t *));
extern	int	ipfdetach __P((ipf_main_softc_t *));
extern	u_short	ipf_cksum __P((u_short *, int));
extern	int	copyinptr __P((ipf_main_softc_t *, void *, void *, size_t));
extern	int	copyoutptr __P((ipf_main_softc_t *, void *, void *, size_t));
extern	int	ipf_fastroute __P((mb_t *, mb_t **, fr_info_t *, frdest_t *));
extern	int	ipf_inject __P((fr_info_t *, mb_t *));
extern	int	ipf_inobj __P((ipf_main_softc_t *, void *, ipfobj_t *,
			       void *, int));
extern	int	ipf_inobjsz __P((ipf_main_softc_t *, void *, void *,
				 int , int));
extern	int	ipf_ioctlswitch __P((ipf_main_softc_t *, int, void *,
				     ioctlcmd_t, int, int, void *));
extern	int	ipf_ipf_ioctl __P((ipf_main_softc_t *, caddr_t, ioctlcmd_t,
				   int, int, void *));
extern	int	ipf_ipftune __P((ipf_main_softc_t *, ioctlcmd_t, void *));
extern	int	ipf_matcharray_load __P((ipf_main_softc_t *, caddr_t,
					 ipfobj_t *, int **));
extern	int	ipf_matcharray_verify __P((int *, int));
extern	int	ipf_outobj __P((ipf_main_softc_t *, void *, void *, int));
extern	int	ipf_outobjk __P((ipf_main_softc_t *, ipfobj_t *, void *));
extern	int	ipf_outobjsz __P((ipf_main_softc_t *, void *, void *,
				  int, int));
extern	void	*ipf_pullup __P((mb_t *, fr_info_t *, int));
extern	int	ipf_resolvedest __P((ipf_main_softc_t *, char *,
				     struct frdest *, int));
extern	int	ipf_resolvefunc __P((ipf_main_softc_t *, void *));
extern	void	*ipf_resolvenic __P((ipf_main_softc_t *, char *, int));
extern	int	ipf_send_icmp_err __P((int, fr_info_t *, int));
extern	int	ipf_send_reset __P((fr_info_t *));
extern	void	ipf_apply_timeout __P((ipftq_t *, u_int));
extern	ipftq_t	*ipf_addtimeoutqueue __P((ipf_main_softc_t *, ipftq_t **,
					  u_int));
extern	void	ipf_deletequeueentry __P((ipftqent_t *));
extern	int	ipf_deletetimeoutqueue __P((ipftq_t *));
extern	void	ipf_freetimeoutqueue __P((ipf_main_softc_t *, ipftq_t *));
extern	void	ipf_movequeue __P((u_long, ipftqent_t *, ipftq_t *,
				   ipftq_t *));
extern	void	ipf_queueappend __P((u_long, ipftqent_t *, ipftq_t *, void *));
extern	void	ipf_queueback __P((u_long, ipftqent_t *));
extern	int	ipf_queueflush __P((ipf_main_softc_t *, ipftq_delete_fn_t,
				    ipftq_t *, ipftq_t *, u_int *, int, int));
extern	void	ipf_queuefront __P((ipftqent_t *));
extern	int	ipf_settimeout_tcp __P((ipftuneable_t *, ipftuneval_t *,
					ipftq_t *));
extern	int	ipf_checkv4sum __P((fr_info_t *));
extern	int	ipf_checkl4sum __P((fr_info_t *));
extern	int	ipf_ifpfillv4addr __P((int, struct sockaddr_in *,
				      struct sockaddr_in *, struct in_addr *,
				      struct in_addr *));
extern	int	ipf_coalesce __P((fr_info_t *));
#ifdef	USE_INET6
extern	int	ipf_checkv6sum __P((fr_info_t *));
extern	int	ipf_ifpfillv6addr __P((int, struct sockaddr_in6 *,
				      struct sockaddr_in6 *, i6addr_t *,
				      i6addr_t *));
#endif

extern	int	ipf_tune_add __P((ipf_main_softc_t *, ipftuneable_t *));
extern	int	ipf_tune_add_array __P((ipf_main_softc_t *, ipftuneable_t *));
extern	int	ipf_tune_del __P((ipf_main_softc_t *, ipftuneable_t *));
extern	int	ipf_tune_del_array __P((ipf_main_softc_t *, ipftuneable_t *));
extern	int	ipf_tune_array_link __P((ipf_main_softc_t *, ipftuneable_t *));
extern	int	ipf_tune_array_unlink __P((ipf_main_softc_t *,
					   ipftuneable_t *));
extern	ipftuneable_t *ipf_tune_array_copy __P((void *, size_t,
						ipftuneable_t *));

extern int	ipf_pr_pullup __P((fr_info_t *, int));

extern	int	ipf_flush __P((ipf_main_softc_t *, minor_t, int));
extern	frgroup_t *ipf_group_add __P((ipf_main_softc_t *, char *, void *,
				      u_32_t, minor_t, int));
extern	void	ipf_group_del __P((ipf_main_softc_t *, frgroup_t *,
				   frentry_t *));
extern	int	ipf_derefrule __P((ipf_main_softc_t *, frentry_t **));
extern	frgroup_t *ipf_findgroup __P((ipf_main_softc_t *, char *, minor_t,
				      int, frgroup_t ***));

extern	int	ipf_log_init __P((void));
extern	int	ipf_log_bytesused __P((ipf_main_softc_t *, int));
extern	int	ipf_log_canread __P((ipf_main_softc_t *, int));
extern	int	ipf_log_clear __P((ipf_main_softc_t *, minor_t));
extern	u_long  ipf_log_failures __P((ipf_main_softc_t *, int));
extern	int	ipf_log_read __P((ipf_main_softc_t *, minor_t, uio_t *));
extern	int	ipf_log_items __P((ipf_main_softc_t *, int, fr_info_t *,
				   void **, size_t *, int *, int));
extern	u_long  ipf_log_logok __P((ipf_main_softc_t *, int));
extern	void	ipf_log_unload __P((ipf_main_softc_t *));
extern	int 	ipf_log_pkt __P((fr_info_t *, u_int));

extern	frentry_t	*ipf_acctpkt __P((fr_info_t *, u_32_t *));
extern	u_short		fr_cksum __P((fr_info_t *, ip_t *, int, void *));
extern	void		ipf_deinitialise __P((ipf_main_softc_t *));
extern	int		ipf_deliverlocal __P((ipf_main_softc_t *, int, void *,
					      i6addr_t *));
extern	frentry_t 	*ipf_dstgrpmap __P((fr_info_t *, u_32_t *));
extern	void		ipf_fixskip __P((frentry_t **, frentry_t *, int));
extern	void		ipf_forgetifp __P((ipf_main_softc_t *, void *));
extern	frentry_t 	*ipf_getrulen __P((ipf_main_softc_t *, int, char *,
					   u_32_t));
extern	int		ipf_ifpaddr __P((ipf_main_softc_t *, int, int, void *,
					i6addr_t *, i6addr_t *));
extern	void		ipf_inet_mask_add __P((int, ipf_v4_masktab_t *));
extern	void		ipf_inet_mask_del __P((int, ipf_v4_masktab_t *));
#ifdef	USE_INET6
extern	void		ipf_inet6_mask_add __P((int, i6addr_t *,
						ipf_v6_masktab_t *));
extern	void		ipf_inet6_mask_del __P((int, i6addr_t *,
						ipf_v6_masktab_t *));
#endif
extern	int		ipf_initialise __P((void));
extern	int		ipf_lock __P((caddr_t, int *));
extern  int		ipf_makefrip __P((int, ip_t *, fr_info_t *));
extern	int		ipf_matchtag __P((ipftag_t *, ipftag_t *));
extern	int		ipf_matchicmpqueryreply __P((int, icmpinfo_t *,
						     struct icmp *, int));
extern	u_32_t		ipf_newisn __P((fr_info_t *));
extern	u_int		ipf_pcksum __P((fr_info_t *, int, u_int));
extern	void		ipf_rule_expire __P((ipf_main_softc_t *));
extern	int		ipf_scanlist __P((fr_info_t *, u_32_t));
extern	frentry_t 	*ipf_srcgrpmap __P((fr_info_t *, u_32_t *));
extern	int		ipf_tcpudpchk __P((fr_ip_t *, frtuc_t *));
extern	int		ipf_verifysrc __P((fr_info_t *fin));
extern	int		ipf_zerostats __P((ipf_main_softc_t *, char *));
extern	int		ipf_getnextrule __P((ipf_main_softc_t *, ipftoken_t *,
					     void *));
extern	int		ipf_sync __P((ipf_main_softc_t *, void *));
extern	int		ipf_token_deref __P((ipf_main_softc_t *, ipftoken_t *));
extern	void		ipf_token_expire __P((ipf_main_softc_t *));
extern	ipftoken_t	*ipf_token_find __P((ipf_main_softc_t *, int, int,
					    void *));
extern	int		ipf_token_del __P((ipf_main_softc_t *, int, int,
					  void *));
extern	void		ipf_token_mark_complete __P((ipftoken_t *));
extern	int		ipf_genericiter __P((ipf_main_softc_t *, void *,
					     int, void *));
#ifdef	IPFILTER_LOOKUP
extern	void		*ipf_resolvelookup __P((int, u_int, u_int,
						lookupfunc_t *));
#endif
extern	u_32_t		ipf_random __P((void));

extern	int		ipf_main_load __P((void));
extern	void		*ipf_main_soft_create __P((void *));
extern	void		ipf_main_soft_destroy __P((ipf_main_softc_t *));
extern	int		ipf_main_soft_init __P((ipf_main_softc_t *));
extern	int		ipf_main_soft_fini __P((ipf_main_softc_t *));
extern	int		ipf_main_unload __P((void));
extern	int		ipf_load_all __P((void));
extern	int		ipf_unload_all __P((void));
extern	void		ipf_destroy_all __P((ipf_main_softc_t *));
extern	ipf_main_softc_t *ipf_create_all __P((void *));
extern	int		ipf_init_all __P((ipf_main_softc_t *));
extern	int		ipf_fini_all __P((ipf_main_softc_t *));
extern	void		ipf_log_soft_destroy __P((ipf_main_softc_t *, void *));
extern	void		*ipf_log_soft_create __P((ipf_main_softc_t *));
extern	int		ipf_log_soft_init __P((ipf_main_softc_t *, void *));
extern	int		ipf_log_soft_fini __P((ipf_main_softc_t *, void *));
extern	int		ipf_log_main_load __P((void));
extern	int		ipf_log_main_unload __P((void));


extern	char	ipfilter_version[];
#ifdef	USE_INET6
extern	int	icmptoicmp6types[ICMP_MAXTYPE+1];
extern	int	icmptoicmp6unreach[ICMP_MAX_UNREACH];
extern	int	icmpreplytype6[ICMP6_MAXTYPE + 1];
#endif
#ifdef	IPFILTER_COMPAT
extern	int	ipf_in_compat __P((ipf_main_softc_t *, ipfobj_t *, void *,int));
extern	int	ipf_out_compat __P((ipf_main_softc_t *, ipfobj_t *, void *));
#endif
extern	int	icmpreplytype4[ICMP_MAXTYPE + 1];

extern	int	ipf_ht_node_add __P((ipf_main_softc_t *, host_track_t *,
				     int, i6addr_t *));
extern	int	ipf_ht_node_del __P((host_track_t *, int, i6addr_t *));
extern	void	ipf_rb_ht_flush __P((host_track_t *));
extern	void	ipf_rb_ht_freenode __P((host_node_t *, void *));
extern	void	ipf_rb_ht_init __P((host_track_t *));

#endif	/* __IP_FIL_H__ */
